#!/usr/bin/env python3
"""ML-based polish pass for ERD layout.

Reads a layout JSON (output of native/ogdf-layout) and refines node positions
via differentiable optimization (PyTorch + MPS). Loss combines a soft
edge-crossing approximation with anchor (locality) and node-overlap penalties.

Usage:
  python ml-layout-polish.py --input layout.json --output polished.json
  python ml-layout-polish.py --synthetic       # Phase 1 self-test

Phase 1 self-test generates a small synthetic graph with deliberate crossings,
runs polish, and prints hard-cross deltas to verify soft-cross loss tracks the
real metric.
"""

import argparse
import json
import math
import sys
import time
from pathlib import Path
from typing import Tuple

import torch
import torch.nn as nn

# ---------------- Hard cross counting (for verification) ----------------

def _ccw(ax, ay, bx, by, cx, cy):
    """Sign of cross product (b-a) x (c-a)."""
    v = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)
    if v > 0: return 1
    if v < 0: return -1
    return 0


def poly_cross_count(routes, edge_pairs, carriers=None, count_same_carrier=True):
    """Count proper polyline-segment-vs-segment crossings between edges.

    routes: list of polylines (list of (x,y) pairs) per edge.
    edge_pairs: list of (src_idx, tgt_idx) per edge — only used to find shared
                endpoints to skip.
    Mirrors C++ detectRouteCrossings(): two edges cross if any segment of one
    properly intersects any segment of the other. Same-carrier filter mirrors
    the carrier-grouped reported metric.
    """
    n = len(routes)
    total = 0
    for i in range(n):
        ri = routes[i]
        if len(ri) < 2:
            continue
        si, ti = edge_pairs[i]
        for j in range(i + 1, n):
            rj = routes[j]
            if len(rj) < 2:
                continue
            sj, tj = edge_pairs[j]
            if si == sj or si == tj or ti == sj or ti == tj:
                continue
            if (not count_same_carrier) and carriers is not None \
                    and carriers[i] == carriers[j]:
                continue
            crossed = False
            for li in range(1, len(ri)):
                ax, ay = ri[li - 1]
                bx, by = ri[li]
                for rk in range(1, len(rj)):
                    cx, cy = rj[rk - 1]
                    dx_, dy_ = rj[rk]
                    o1 = _ccw(ax, ay, bx, by, cx, cy)
                    o2 = _ccw(ax, ay, bx, by, dx_, dy_)
                    o3 = _ccw(cx, cy, dx_, dy_, ax, ay)
                    o4 = _ccw(cx, cy, dx_, dy_, bx, by)
                    if o1 != o2 and o3 != o4 and o1 != 0 and o3 != 0:
                        crossed = True
                        break
                if crossed:
                    break
            if crossed:
                total += 1
    return total


def hard_cross_count(coords, edges, carriers=None, count_same_carrier=True):
    """Count proper segment-segment crossings on straight-line edges.

    coords: [N,2] tensor or list of (x,y).
    edges: list of (src_idx, tgt_idx).
    carriers: optional list of carrier strings per edge.
    count_same_carrier: if False, skip pairs with same carrier (matches
    reported edgeCrossings semantics).
    """
    if torch.is_tensor(coords):
        coords = coords.detach().cpu().numpy()
    n = len(edges)
    total = 0
    for i in range(n):
        a, b = edges[i]
        ax, ay = coords[a]
        bx, by = coords[b]
        for j in range(i + 1, n):
            c, d = edges[j]
            if a == c or a == d or b == c or b == d:
                continue
            if (not count_same_carrier) and carriers is not None \
                    and carriers[i] == carriers[j]:
                continue
            cx, cy = coords[c]
            dx_, dy_ = coords[d]
            o1 = _ccw(ax, ay, bx, by, cx, cy)
            o2 = _ccw(ax, ay, bx, by, dx_, dy_)
            o3 = _ccw(cx, cy, dx_, dy_, ax, ay)
            o4 = _ccw(cx, cy, dx_, dy_, bx, by)
            if o1 != o2 and o3 != o4 and o1 != 0 and o3 != 0:
                total += 1
    return total


# ---------------- Soft cross loss ----------------

def soft_cross_loss(
    pos: torch.Tensor,
    edge_pairs: torch.Tensor,
    pair_idx: torch.Tensor,
    sharpness: float,
):
    """Differentiable approximation of edge-crossing count.

    pos: [N,2]
    edge_pairs: [E,2] long tensor of (src_idx, tgt_idx) per edge
    pair_idx: [P,2] long tensor of (i, j) — non-shared, non-same-carrier edge pairs
    sharpness: K in sigmoid(K * normalized_cross). Cross products are
    normalized by |AB| * (|AC|+eps) so the value lies in [-1, 1] and
    K is dimensionless.

    Two segments AB and CD cross iff:
      sign(AB x AC) != sign(AB x AD)  AND  sign(CD x CA) != sign(CD x CB)
    """
    eps = 1e-3
    e1 = pair_idx[:, 0]
    e2 = pair_idx[:, 1]
    a = pos[edge_pairs[e1, 0]]  # [P,2]
    b = pos[edge_pairs[e1, 1]]
    c = pos[edge_pairs[e2, 0]]
    d = pos[edge_pairs[e2, 1]]

    abx = b[:, 0] - a[:, 0]
    aby = b[:, 1] - a[:, 1]
    cdx = d[:, 0] - c[:, 0]
    cdy = d[:, 1] - c[:, 1]
    ab_len = torch.sqrt(abx * abx + aby * aby + eps)
    cd_len = torch.sqrt(cdx * cdx + cdy * cdy + eps)

    # Normalized cross products: divide by |AB| (or |CD|) and the third
    # vector's length. Result is sin(angle) ∈ [-1,1] essentially.
    ac_x = c[:, 0] - a[:, 0]
    ac_y = c[:, 1] - a[:, 1]
    ad_x = d[:, 0] - a[:, 0]
    ad_y = d[:, 1] - a[:, 1]
    ca_x = a[:, 0] - c[:, 0]
    ca_y = a[:, 1] - c[:, 1]
    cb_x = b[:, 0] - c[:, 0]
    cb_y = b[:, 1] - c[:, 1]
    ac_len = torch.sqrt(ac_x * ac_x + ac_y * ac_y + eps)
    ad_len = torch.sqrt(ad_x * ad_x + ad_y * ad_y + eps)
    ca_len = ac_len  # same magnitude
    cb_len = torch.sqrt(cb_x * cb_x + cb_y * cb_y + eps)

    cross1 = (abx * ac_y - aby * ac_x) / (ab_len * ac_len)
    cross2 = (abx * ad_y - aby * ad_x) / (ab_len * ad_len)
    cross3 = (cdx * ca_y - cdy * ca_x) / (cd_len * ca_len)
    cross4 = (cdx * cb_y - cdy * cb_x) / (cd_len * cb_len)

    s1 = torch.sigmoid(sharpness * cross1) * torch.sigmoid(-sharpness * cross2) \
       + torch.sigmoid(-sharpness * cross1) * torch.sigmoid(sharpness * cross2)
    s2 = torch.sigmoid(sharpness * cross3) * torch.sigmoid(-sharpness * cross4) \
       + torch.sigmoid(-sharpness * cross3) * torch.sigmoid(sharpness * cross4)

    soft = s1 * s2  # [P]
    return soft.sum()


def soft_cross_loss_carrier_grouped(
    pos: torch.Tensor,
    edge_pairs: torch.Tensor,
    pair_idx: torch.Tensor,
    carrier_pair_id: torch.Tensor,
    num_carrier_pairs: int,
    sharpness: float,
):
    """Carrier-grouped soft cross loss using Noisy-OR per carrier-pair.

    For each carrier-pair (cX, cY), contribution = 1 - prod_i(1 - soft_i)
    where soft_i are the per-edge-pair soft cross indicators within that
    carrier-pair group. Aligns the polish target with the user-facing
    edgeCrossings metric (carrier-grouped count: each (cX, cY) counted
    once regardless of how many segment pairs cross).

    pos, edge_pairs, pair_idx, sharpness: as in soft_cross_loss
    carrier_pair_id: [P] long tensor mapping each pair index to a unique
                     carrier-pair id ∈ [0, num_carrier_pairs)
    num_carrier_pairs: total number of distinct carrier-pairs
    """
    eps = 1e-3
    e1 = pair_idx[:, 0]
    e2 = pair_idx[:, 1]
    a = pos[edge_pairs[e1, 0]]
    b = pos[edge_pairs[e1, 1]]
    c = pos[edge_pairs[e2, 0]]
    d = pos[edge_pairs[e2, 1]]
    abx = b[:, 0] - a[:, 0]
    aby = b[:, 1] - a[:, 1]
    cdx = d[:, 0] - c[:, 0]
    cdy = d[:, 1] - c[:, 1]
    ab_len = torch.sqrt(abx * abx + aby * aby + eps)
    cd_len = torch.sqrt(cdx * cdx + cdy * cdy + eps)
    ac_x = c[:, 0] - a[:, 0]
    ac_y = c[:, 1] - a[:, 1]
    ad_x = d[:, 0] - a[:, 0]
    ad_y = d[:, 1] - a[:, 1]
    ca_x = a[:, 0] - c[:, 0]
    ca_y = a[:, 1] - c[:, 1]
    cb_x = b[:, 0] - c[:, 0]
    cb_y = b[:, 1] - c[:, 1]
    ac_len = torch.sqrt(ac_x * ac_x + ac_y * ac_y + eps)
    ad_len = torch.sqrt(ad_x * ad_x + ad_y * ad_y + eps)
    ca_len = ac_len
    cb_len = torch.sqrt(cb_x * cb_x + cb_y * cb_y + eps)
    cross1 = (abx * ac_y - aby * ac_x) / (ab_len * ac_len)
    cross2 = (abx * ad_y - aby * ad_x) / (ab_len * ad_len)
    cross3 = (cdx * ca_y - cdy * ca_x) / (cd_len * ca_len)
    cross4 = (cdx * cb_y - cdy * cb_x) / (cd_len * cb_len)
    s1 = torch.sigmoid(sharpness * cross1) * torch.sigmoid(-sharpness * cross2) \
       + torch.sigmoid(-sharpness * cross1) * torch.sigmoid(sharpness * cross2)
    s2 = torch.sigmoid(sharpness * cross3) * torch.sigmoid(-sharpness * cross4) \
       + torch.sigmoid(-sharpness * cross3) * torch.sigmoid(sharpness * cross4)
    soft = s1 * s2  # [P]
    # Carrier-grouped Noisy-OR: 1 - prod_i(1 - soft_i) per carrier-pair.
    # Use log space for numerical stability.
    soft_clamped = torch.clamp(soft, min=0.0, max=1.0 - 1e-6)
    log_no_cross = torch.log1p(-soft_clamped)  # log(1 - soft), [P]
    log_prod = torch.zeros(num_carrier_pairs, device=pos.device)
    log_prod = log_prod.scatter_add(0, carrier_pair_id, log_no_cross)
    prod_no_cross = torch.exp(log_prod)  # [K]
    return (1.0 - prod_no_cross).sum()


def cluster_spread_loss(
    pos: torch.Tensor,
    pos_init: torch.Tensor,
    cluster_member_idx: list,
):
    """Penalize clusters that contract relative to baseline.

    For each cluster, compute the std of member positions (a proxy for
    cluster bbox area). If std drops below baseline std → penalty.
    Doesn't penalize expansion (only contraction) — cross-resolving may
    benefit from slight expansion.

    cluster_member_idx: list of LongTensor, one per cluster, listing
    member node indices.
    """
    total = torch.tensor(0.0, device=pos.device)
    for members in cluster_member_idx:
        if members.numel() < 2:
            continue
        cur = pos[members]
        base = pos_init[members]
        cur_std = cur.std(dim=0).sum() + 1e-3
        base_std = base.std(dim=0).sum() + 1e-3
        # penalty if cur_std < base_std (contraction)
        ratio = cur_std / base_std
        penalty = torch.relu(1.0 - ratio) ** 2
        total = total + penalty
    return total


def overlap_loss(pos: torch.Tensor, sizes: torch.Tensor, margin: float):
    """Soft penalty when node bboxes (with margin) overlap.

    pos:   [N,2] center positions
    sizes: [N,2] (width, height)

    For each pair (i,j), penalty = max(0, half_w_sum - |dx|)^2 + same for y.
    O(N^2). Use radius pre-filter to keep gradient only on nearby pairs.
    """
    n = pos.shape[0]
    diff = pos.unsqueeze(0) - pos.unsqueeze(1)  # [N,N,2]
    abs_diff = diff.abs()
    half_w = sizes[:, 0] * 0.5 + margin
    half_h = sizes[:, 1] * 0.5 + margin
    min_dx = half_w.unsqueeze(0) + half_w.unsqueeze(1)
    min_dy = half_h.unsqueeze(0) + half_h.unsqueeze(1)
    overlap_x = torch.relu(min_dx - abs_diff[:, :, 0])
    overlap_y = torch.relu(min_dy - abs_diff[:, :, 1])
    pair_overlap = overlap_x * overlap_y  # [N,N], product = area-like measure
    # Diagonal i==i is 0 since diff is 0 → relu(min_dx - 0) > 0 spurious;
    # mask diagonal explicitly.
    mask = 1.0 - torch.eye(n, device=pos.device)
    return (pair_overlap * mask).sum() * 0.5  # /2 for double-count


# ---------------- Layout JSON IO ----------------

def load_layout(path: Path):
    with open(path) as f:
        layout = json.load(f)

    # Build node-modelId → idx map.
    node_modelids = []
    centers = []
    sizes = []
    cluster_ids = []
    for nd in layout["nodes"]:
        node_modelids.append(nd["modelId"])
        # JSON 'position' is top-left corner.
        cx = nd["position"]["x"] + nd["size"]["width"] / 2.0
        cy = nd["position"]["y"] + nd["size"]["height"] / 2.0
        centers.append((cx, cy))
        sizes.append((nd["size"]["width"], nd["size"]["height"]))
        cluster_ids.append(nd.get("clusterId", ""))

    id2idx = {mid: i for i, mid in enumerate(node_modelids)}

    edges = []
    edge_ids = []
    for re in layout["routedEdges"]:
        src = re.get("sourceModelId")
        tgt = re.get("targetModelId")
        if src is None or tgt is None:
            # Fallback: infer from first/last route point matching node center.
            pts = re["points"]
            if len(pts) < 2:
                continue
            first, last = pts[0], pts[-1]
            src_idx = _nearest_node(first, centers)
            tgt_idx = _nearest_node(last, centers)
        else:
            src_idx = id2idx[src]
            tgt_idx = id2idx[tgt]
        if src_idx == tgt_idx:
            continue
        edges.append((src_idx, tgt_idx))
        edge_ids.append(re["edgeId"])

    return {
        "layout": layout,
        "node_modelids": node_modelids,
        "centers": centers,
        "sizes": sizes,
        "cluster_ids": cluster_ids,
        "edges": edges,
        "edge_ids": edge_ids,
    }


def _nearest_node(point, centers):
    px, py = point["x"], point["y"]
    best = 0
    best_d2 = float("inf")
    for i, (cx, cy) in enumerate(centers):
        d2 = (cx - px) ** 2 + (cy - py) ** 2
        if d2 < best_d2:
            best_d2 = d2
            best = i
    return best


def save_layout(layout, centers, output_path: Path):
    """Write a copy of layout with updated node positions and clean routes.

    Routes are regenerated as straight lines from new node centers — any
    intermediate waypoints from a previous routing pass (xings-detour bends)
    are dropped because they'd be stale relative to the polished positions
    (visible as zigzag / disconnected edges in the IDE preview). The C++
    rerouter regenerates proper waypoints downstream from these straight-line
    starting routes.
    """
    out = json.loads(json.dumps(layout))
    id2idx = {mid: i for i, mid in enumerate(nd["modelId"] for nd in out["nodes"])}

    # Update node positions (back to top-left from center).
    for i, nd in enumerate(out["nodes"]):
        cx, cy = centers[i]
        nd["position"]["x"] = cx - nd["size"]["width"] / 2.0
        nd["position"]["y"] = cy - nd["size"]["height"] / 2.0

    # Replace each route with a clean straight-line src→tgt; drop stale
    # intermediate waypoints. (Was: only updated first/last; intermediates
    # stayed at OLD positions creating visible ghost trails.)
    for re in out["routedEdges"]:
        src = re.get("sourceModelId")
        tgt = re.get("targetModelId")
        if src is None or tgt is None:
            continue
        if src not in id2idx or tgt not in id2idx:
            continue
        si = id2idx[src]
        ti = id2idx[tgt]
        sx, sy = centers[si]
        tx, ty = centers[ti]
        re["points"] = [{"x": sx, "y": sy}, {"x": tx, "y": ty}]

    with open(output_path, "w") as f:
        json.dump(out, f, separators=(",", ":"))

    # Sidecar TSV with center positions, format: modelId\tcenterX\tcenterY.
    # Used by C++ binary's --positions-tsv flag for round-trip re-routing.
    tsv_path = output_path.with_suffix(output_path.suffix + ".positions.tsv")
    with open(tsv_path, "w") as f:
        for i, nd in enumerate(out["nodes"]):
            cx = nd["position"]["x"] + nd["size"]["width"] / 2.0
            cy = nd["position"]["y"] + nd["size"]["height"] / 2.0
            f.write(f"{nd['modelId']}\t{cx}\t{cy}\n")


# ---------------- Carrier filter (mirrors C++ Plan A) ----------------

def build_carrier_ids(layout, edges, edge_ids=None):
    """Replicate C++ carrier-id grouping (mirrors carrier-cross logic).

    Carriers (priority order):
      1. Bundle leaves vs root → "B<idx>|<root>"
      2. Cluster pair → "C|<a>|<b>" (sorted) or "Cself|<c>" intra-cluster.
         Non-cluster nodes fall back to nearest-cluster centroid.
      3. Else own edge id (individual carrier).
    """
    nodes = layout["nodes"]
    id2idx = {nd["modelId"]: i for i, nd in enumerate(nodes)}
    centers = []
    for nd in nodes:
        cx = nd["position"]["x"] + nd["size"]["width"] / 2.0
        cy = nd["position"]["y"] + nd["size"]["height"] / 2.0
        centers.append((cx, cy))

    cluster_by_id = {}
    for nd in nodes:
        cid = nd.get("clusterId")
        if cid:
            cluster_by_id[nd["modelId"]] = cid

    # Cluster centroid cache for nearest-cluster fallback.
    sum_by_cluster = {}
    cnt_by_cluster = {}
    for mid, cid in cluster_by_id.items():
        i = id2idx[mid]
        cx, cy = centers[i]
        s = sum_by_cluster.get(cid, (0.0, 0.0))
        sum_by_cluster[cid] = (s[0] + cx, s[1] + cy)
        cnt_by_cluster[cid] = cnt_by_cluster.get(cid, 0) + 1
    centroid_by_cluster = {
        c: (s[0] / cnt_by_cluster[c], s[1] / cnt_by_cluster[c])
        for c, s in sum_by_cluster.items()
    }

    def nearest_cluster(idx):
        if not centroid_by_cluster: return ""
        cx, cy = centers[idx]
        best = ""
        bestd = float("inf")
        for cid, (mx, my) in centroid_by_cluster.items():
            d = (cx - mx) ** 2 + (cy - my) ** 2
            if d < bestd:
                bestd = d
                best = cid
        return best

    # Bundle index from engineMetadata.
    leaf_to_bundle = {}
    bundles = layout.get("engineMetadata", {}).get("leafBundles", []) or []
    for bi, b in enumerate(bundles):
        for leaf in b.get("leafModelIds", []):
            leaf_to_bundle[leaf] = bi

    carriers = []
    for k, (s, t) in enumerate(edges):
        s_mid = nodes[s]["modelId"]
        t_mid = nodes[t]["modelId"]
        # Bundle carrier.
        cid = None
        sBI = leaf_to_bundle.get(s_mid)
        tBI = leaf_to_bundle.get(t_mid)
        if sBI is not None:
            b = bundles[sBI]
            roots = b.get("sharedRootModelIds") or [b.get("parentModelId")]
            if t_mid in roots:
                cid = f"B{sBI}|{t_mid}"
        if cid is None and tBI is not None:
            b = bundles[tBI]
            roots = b.get("sharedRootModelIds") or [b.get("parentModelId")]
            if s_mid in roots:
                cid = f"B{tBI}|{s_mid}"
        if cid is None:
            sc = cluster_by_id.get(s_mid, "") or nearest_cluster(s)
            tc = cluster_by_id.get(t_mid, "") or nearest_cluster(t)
            if sc and tc:
                if sc == tc:
                    cid = f"Cself|{sc}"
                else:
                    a, b = (sc, tc) if sc < tc else (tc, sc)
                    cid = f"C|{a}|{b}"
            else:
                cid = edge_ids[k] if edge_ids and k < len(edge_ids) else f"E|{s}|{t}"
        carriers.append(cid)
    return carriers


def build_pair_idx(edges, carriers, exclude_same_carrier=True):
    """All edge index pairs (i<j) with shared-endpoint and same-carrier excluded."""
    n = len(edges)
    pairs = []
    for i in range(n):
        si, ti = edges[i]
        for j in range(i + 1, n):
            sj, tj = edges[j]
            if si == sj or si == tj or ti == sj or ti == tj:
                continue
            if exclude_same_carrier and carriers[i] == carriers[j]:
                continue
            pairs.append((i, j))
    return pairs


def build_carrier_pair_groups(pair_idx, carriers):
    """Map each (i,j) in pair_idx to a unique carrier-pair id.

    Returns (carrier_pair_id, num_carrier_pairs) where carrier_pair_id is
    a list of int (one per entry in pair_idx) indexing into [0, K).
    Used by soft_cross_loss_carrier_grouped to apply Noisy-OR per group.
    """
    pair_key_to_id = {}
    carrier_pair_id = []
    for (i, j) in pair_idx:
        ci = carriers[i]
        cj = carriers[j]
        key = (ci, cj) if ci <= cj else (cj, ci)
        if key not in pair_key_to_id:
            pair_key_to_id[key] = len(pair_key_to_id)
        carrier_pair_id.append(pair_key_to_id[key])
    return carrier_pair_id, len(pair_key_to_id)


# ---------------- Optimization ----------------

def polish(
    centers,
    sizes,
    edges,
    pair_idx,
    *,
    device="mps",
    iters=1000,
    lr=10.0,
    sharpness=0.01,
    sharpness_end=None,
    w_cross=1.0,
    w_anchor=0.0001,
    w_overlap=0.001,
    w_edge_len=0.0,
    w_cluster_spread=0.0,
    cluster_member_idx=None,
    log_every=50,
    verify_every=200,
    edges_for_verify=None,
    seed=0,
    jitter_std=0.0,
):
    """Run gradient descent. Returns updated centers (np array)."""
    pos_init = torch.tensor(centers, dtype=torch.float32, device=device)
    if jitter_std > 0:
        gen = torch.Generator(device=device)
        gen.manual_seed(seed)
        jitter = torch.randn(
            pos_init.shape, generator=gen, dtype=torch.float32, device=device
        ) * jitter_std
        pos = (pos_init + jitter).clone().requires_grad_(True)
    else:
        pos = pos_init.clone().requires_grad_(True)
    sizes_t = torch.tensor(sizes, dtype=torch.float32, device=device)
    edges_t = torch.tensor(edges, dtype=torch.long, device=device)
    pair_t = torch.tensor(pair_idx, dtype=torch.long, device=device)

    # Sharpness now applied to NORMALIZED cross products (∈ [-1,1]).
    # Higher sharpness = harder boundary; lower = wider transition for
    # gradient signal. Default 10.0 makes sigmoid(10 * 0.1) ≈ 0.73 — good
    # transition for near-crossing pairs.
    with torch.no_grad():
        a = pos[edges_t[:, 0]]
        b = pos[edges_t[:, 1]]
        edge_lens = (a - b).norm(dim=1)
        median_len = edge_lens.median().item()
    auto_sharpness = sharpness  # No scale dependence — normalized cross.
    print(f"  [polish] median edge len={median_len:.0f}, sharpness={auto_sharpness:.3f}")

    optim = torch.optim.Adam([pos], lr=lr)

    # Sharpness annealing: linear ramp from sharpness → sharpness_end.
    # Lower sharpness at start = wider gradient signal so far-apart edges
    # can "feel" each other and move apart. Higher at end = sharper
    # boundary so the loss approximates the hard cross count more closely.
    if sharpness_end is None:
        sharpness_end = sharpness

    history = []
    for it in range(iters):
        optim.zero_grad()
        cur_sharp = sharpness + (sharpness_end - sharpness) * (it / max(iters - 1, 1))
        L_cross = soft_cross_loss(pos, edges_t, pair_t, cur_sharp)
        anchor = ((pos - pos_init) ** 2).sum()
        ovl = overlap_loss(pos, sizes_t, margin=8.0)
        L = w_cross * L_cross + w_anchor * anchor + w_overlap * ovl
        if w_edge_len > 0:
            ea = pos[edges_t[:, 0]]
            eb = pos[edges_t[:, 1]]
            L_len = ((ea - eb) ** 2).sum(dim=1).sqrt().sum()
            L = L + w_edge_len * L_len
        if w_cluster_spread > 0 and cluster_member_idx is not None:
            L_spread = cluster_spread_loss(pos, pos_init, cluster_member_idx)
            L = L + w_cluster_spread * L_spread
        L.backward()
        # Gradient norm clipping for stability.
        torch.nn.utils.clip_grad_norm_([pos], max_norm=500.0)
        optim.step()

        if it == 0 or (it + 1) % log_every == 0:
            extra = ""
            if edges_for_verify is not None and ((it + 1) % verify_every == 0 or it == 0):
                hard = hard_cross_count(pos, edges_for_verify)
                extra = f"  hard_cross={hard}"
            print(f"  [polish] step {it+1:5d}/{iters}  "
                  f"K={cur_sharp:.1f}  L={L.item():.2f}  "
                  f"L_cross={L_cross.item():.2f}  "
                  f"anchor={anchor.item():.2e}  ovl={ovl.item():.2e}{extra}")
            history.append((it + 1, L.item(), L_cross.item()))

    return pos.detach().cpu().numpy().tolist(), history


# ---------------- Synthetic test (Phase 1) ----------------

def gen_synthetic(n=50, m=80, seed=1, span=10000.0):
    """Generate random graph: n nodes uniformly placed, m random edges."""
    import random
    random.seed(seed)
    centers = [(random.uniform(0, span), random.uniform(0, span)) for _ in range(n)]
    sizes = [(180.0, 60.0)] * n
    edges = set()
    while len(edges) < m:
        a = random.randint(0, n - 1)
        b = random.randint(0, n - 1)
        if a == b: continue
        if (a, b) in edges or (b, a) in edges: continue
        edges.add((min(a, b), max(a, b)))
    return centers, sizes, list(edges)


def run_synthetic():
    print("=== Phase 1: Synthetic test ===")
    centers, sizes, edges = gen_synthetic(n=50, m=80)
    pair_idx = []
    for i in range(len(edges)):
        si, ti = edges[i]
        for j in range(i + 1, len(edges)):
            sj, tj = edges[j]
            if si == sj or si == tj or ti == sj or ti == tj:
                continue
            pair_idx.append((i, j))

    hard_before = hard_cross_count(centers, edges)
    print(f"  N={len(centers)} edges={len(edges)} pair_idx={len(pair_idx)}")
    print(f"  initial hard_cross={hard_before}")

    new_centers, history = polish(
        centers, sizes, edges, pair_idx,
        iters=1000, lr=50.0, sharpness=10.0,
        w_cross=1.0, w_anchor=1e-7, w_overlap=0.01,
        edges_for_verify=edges,
    )
    hard_after = hard_cross_count(new_centers, edges)
    print(f"  final hard_cross={hard_after} (delta {hard_after - hard_before})")
    if hard_after < hard_before:
        print(f"  ✅ soft cross loss reduced hard crossings ({hard_before} → {hard_after})")
    else:
        print(f"  ⚠ hard crossings did not improve")
    return hard_before, hard_after


# ---------------- Real ERD polish (Phase 2) ----------------

def run_real(input_path: Path, output_path: Path, args):
    print(f"=== Phase 2: Polishing {input_path} ===")
    data = load_layout(input_path)
    centers = data["centers"]
    sizes = data["sizes"]
    edges = data["edges"]
    print(f"  N={len(centers)} edges={len(edges)}")

    carriers = build_carrier_ids(data["layout"], edges, edge_ids=data["edge_ids"])
    pair_idx = build_pair_idx(edges, carriers, exclude_same_carrier=not args.no_carrier)
    print(f"  carriers={len(set(carriers))}  pair_idx={len(pair_idx)}")

    hard_before = hard_cross_count(centers, edges)
    hard_before_filt = hard_cross_count(
        centers, edges, carriers=carriers, count_same_carrier=False)
    print(f"  initial straight-line hard_cross={hard_before} "
          f"(carrier-filtered={hard_before_filt})")

    # Build cluster member index for cluster_spread_loss.
    cluster_to_members = {}
    for i, nd in enumerate(data["layout"]["nodes"]):
        cid = nd.get("clusterId")
        if cid:
            cluster_to_members.setdefault(cid, []).append(i)
    cluster_member_idx = [
        torch.tensor(m, dtype=torch.long, device=args.device)
        for m in cluster_to_members.values()
        if len(m) >= 2
    ]
    print(f"  clusters with ≥2 members: {len(cluster_member_idx)}")

    t0 = time.time()
    # Multi-restart: jittered re-inits, take best by carrier-filtered hard cross.
    best_centers = None
    best_metric = float("inf")
    best_run_idx = -1
    for run in range(args.num_restarts):
        run_seed = args.seed + run
        # Run 0 = unjittered. Subsequent runs = jittered.
        run_jitter = 0.0 if run == 0 else args.jitter_std
        print(f"\n  [polish] === run {run + 1}/{args.num_restarts}  "
              f"seed={run_seed}  jitter_std={run_jitter:.0f} ===")
        run_centers, _ = polish(
            centers, sizes, edges, pair_idx,
            device=args.device,
            iters=args.iters, lr=args.lr,
            sharpness=args.sharpness,
            sharpness_end=args.sharpness_end,
            w_cross=args.w_cross,
            w_anchor=args.w_anchor,
            w_overlap=args.w_overlap,
            w_edge_len=args.w_edge_len,
            w_cluster_spread=args.w_cluster_spread,
            cluster_member_idx=cluster_member_idx,
            log_every=max(1, args.iters // 10),
            verify_every=max(1, args.iters // 5),
            edges_for_verify=edges if args.iters <= 5000 else None,
            seed=run_seed,
            jitter_std=run_jitter,
        )
        run_metric = hard_cross_count(
            run_centers, edges, carriers=carriers, count_same_carrier=False)
        print(f"  [polish] run {run + 1} carrier-filtered hard_cross={run_metric}")
        if run_metric < best_metric:
            best_metric = run_metric
            best_centers = run_centers
            best_run_idx = run
    new_centers = best_centers
    print(f"\n  [polish] best run = {best_run_idx + 1}/{args.num_restarts}, "
          f"carrier-filtered hard_cross = {best_metric}")
    elapsed = time.time() - t0

    hard_after = hard_cross_count(new_centers, edges)
    hard_after_filt = hard_cross_count(
        new_centers, edges, carriers=carriers, count_same_carrier=False)
    print(f"  final straight-line hard_cross={hard_after} "
          f"(carrier-filtered={hard_after_filt}, "
          f"delta {hard_after - hard_before} / "
          f"{hard_after_filt - hard_before_filt}, {elapsed:.1f}s)")

    # Polyline cross — uses ORIGINAL interior waypoints (from xings-detour)
    # with updated endpoints. Pessimistic estimate: interior waypoints may
    # be in stale positions, but gives a lower bound on the improvement
    # the IDE would see if it re-runs polyline routing on these positions.
    routes_before = []
    routes_after = []
    layout = data["layout"]
    for re in layout["routedEdges"]:
        pts = [(p["x"], p["y"]) for p in re["points"]]
        routes_before.append(list(pts))
    # For routes_after: same waypoints but pull endpoints to new positions.
    id2idx = {nd["modelId"]: i for i, nd in enumerate(layout["nodes"])}
    for re in layout["routedEdges"]:
        pts = [(p["x"], p["y"]) for p in re["points"]]
        if len(pts) >= 2:
            si = id2idx[re["sourceModelId"]]
            ti = id2idx[re["targetModelId"]]
            pts[0] = tuple(new_centers[si])
            pts[-1] = tuple(new_centers[ti])
        routes_after.append(pts)
    poly_before = poly_cross_count(routes_before, edges)
    poly_before_filt = poly_cross_count(
        routes_before, edges, carriers=carriers, count_same_carrier=False)
    poly_after = poly_cross_count(routes_after, edges)
    poly_after_filt = poly_cross_count(
        routes_after, edges, carriers=carriers, count_same_carrier=False)
    print(f"  polyline (with pre-existing waypoints, new endpoints):")
    print(f"    before:  all={poly_before:6d}  carrier-filtered={poly_before_filt}")
    print(f"    after:   all={poly_after:6d}  carrier-filtered={poly_after_filt}")
    print(f"    delta:   all={poly_after - poly_before:+d}  "
          f"carrier-filtered={poly_after_filt - poly_before_filt:+d}")

    save_layout(data["layout"], new_centers, output_path)
    print(f"  → wrote {output_path}")
    return hard_before, hard_after


# ---------------- Main ----------------

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--input", type=Path, help="Layout JSON (C++ output)")
    p.add_argument("--output", type=Path, help="Polished layout JSON")
    p.add_argument("--synthetic", action="store_true", help="Run Phase 1 self-test")
    p.add_argument("--device", default="mps", choices=["mps", "cpu", "cuda"])
    p.add_argument("--iters", type=int, default=1000)
    p.add_argument("--lr", type=float, default=20.0)
    p.add_argument("--sharpness", type=float, default=10.0,
                   help="K in sigmoid(K * normalized_cross), normalized cross ∈ [-1,1]")
    p.add_argument("--sharpness-end", type=float, default=None,
                   help="Final sharpness; if set, anneals linearly from --sharpness")
    p.add_argument("--w-cross", type=float, default=1.0)
    p.add_argument("--w-anchor", type=float, default=1e-6)
    p.add_argument("--w-overlap", type=float, default=0.001)
    p.add_argument("--w-edge-len", type=float, default=0.0,
                   help="Edge-length penalty weight (compaction)")
    p.add_argument("--w-cluster-spread", type=float, default=0.0,
                   help="Penalize clusters that contract relative to baseline")
    p.add_argument("--num-restarts", type=int, default=1,
                   help="Number of polish runs with different seeds; best wins")
    p.add_argument("--seed", type=int, default=0,
                   help="Base RNG seed for jittered restarts")
    p.add_argument("--jitter-std", type=float, default=200.0,
                   help="Std of position jitter for restarts > 0")
    p.add_argument("--no-carrier", action="store_true",
                   help="Disable same-carrier filter (count all crosses)")
    args = p.parse_args()

    if args.synthetic:
        run_synthetic()
        return 0

    if not args.input or not args.output:
        print("error: --input and --output required (or use --synthetic)", file=sys.stderr)
        return 1

    run_real(args.input, args.output, args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
