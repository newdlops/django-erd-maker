#!/usr/bin/env python3
"""Cluster-rigid polish: only cluster *translations* and hub positions are
optimized; the relative layout inside each cluster is preserved exactly.

Motivation: heuristic (cluster_graph) builds careful intra-cluster placement
(polar arrangements, ring layouts, matrix bundles). Generic per-node polish
breaks those structures by squeezing nodes towards the cluster centroid.
This script keeps every cluster member at its baseline offset relative to
the cluster centroid, and only learns:
  - cluster_translation[c]: 2D offset applied to all members of cluster c
  - hub_positions[h]:        free 2D position for each non-cluster node
                             (hubs/connectors/router/independent)

Loss matches ml-layout-polish.py (soft cross + overlap), but the variable
space is ~10× smaller and changes are *macro*: clusters slide as rigid
blocks. Useful when local polish over-compresses or can't unkink because
cluster placement itself needs to shift.

Usage:
  python scripts/ml-layout-polish-rigid.py \
      --input  /tmp/layout-pre-ml.json \
      --output /tmp/layout-rigid.json \
      --iters 1500
"""

import argparse
import json
import math
import sys
import time
from pathlib import Path

import torch

# Reuse utilities from ml-layout-polish.py.
import importlib.util
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "polish", ROOT / "scripts/ml-layout-polish.py"
)
polish_mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(polish_mod)

soft_cross_loss = polish_mod.soft_cross_loss
soft_cross_loss_carrier_grouped = polish_mod.soft_cross_loss_carrier_grouped
overlap_loss = polish_mod.overlap_loss
hard_cross_count = polish_mod.hard_cross_count
load_layout = polish_mod.load_layout
save_layout = polish_mod.save_layout
build_carrier_ids = polish_mod.build_carrier_ids
build_pair_idx = polish_mod.build_pair_idx
build_carrier_pair_groups = polish_mod.build_carrier_pair_groups


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--input", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--device", default="mps")
    p.add_argument("--iters", type=int, default=1500)
    p.add_argument("--lr", type=float, default=80.0)
    p.add_argument("--sharpness", type=float, default=10.0)
    p.add_argument("--sharpness-end", type=float, default=None,
                   help="If set, anneal linearly from --sharpness")
    p.add_argument("--w-cross", type=float, default=1.0)
    p.add_argument("--w-overlap", type=float, default=0.001)
    p.add_argument("--w-anchor-trans", type=float, default=1e-7,
                   help="anchor cluster translations to (0,0)")
    p.add_argument("--w-anchor-hub", type=float, default=1e-7,
                   help="anchor hub positions to baseline")
    p.add_argument("--enable-rotation", action="store_true",
                   help="Allow per-cluster rotation (rigid body in SE(2))")
    p.add_argument("--enable-scale", action="store_true",
                   help="Allow per-cluster scaling (SIM(2))")
    p.add_argument("--w-anchor-rot", type=float, default=1e-3,
                   help="anchor cluster rotations to 0 (suppresses spinning)")
    p.add_argument("--w-anchor-scale", type=float, default=1.0,
                   help="anchor cluster log-scale to 0 (no shrink/expand)")
    p.add_argument("--no-carrier", action="store_true")
    p.add_argument("--carrier-grouped-loss", action="store_true",
                   help="Use Noisy-OR per carrier-pair instead of summing"
                        " all soft crosses (aligns with edgeCrossings metric)")
    p.add_argument("--num-restarts", type=int, default=1,
                   help="Run polish N times with jittered init; pick best")
    p.add_argument("--jitter-std", type=float, default=200.0,
                   help="Stddev of init jitter (in layout units) per restart")
    p.add_argument("--seed", type=int, default=0xC0FFEE,
                   help="Base RNG seed; restart k uses seed+k")
    args = p.parse_args()

    data = load_layout(args.input)
    centers = data["centers"]
    sizes = data["sizes"]
    edges = data["edges"]
    layout = data["layout"]

    print(f"Loaded {args.input.name}: N={len(centers)} E={len(edges)}")

    # Identify cluster membership.
    cluster_to_members = {}
    for i, nd in enumerate(layout["nodes"]):
        cid = nd.get("clusterId")
        if cid:
            cluster_to_members.setdefault(cid, []).append(i)

    cluster_ids = list(cluster_to_members.keys())
    cluster_idx_of_node = [-1] * len(centers)  # -1 = hub (no cluster)
    for ci, cid in enumerate(cluster_ids):
        for ni in cluster_to_members[cid]:
            cluster_idx_of_node[ni] = ci

    num_clusters = len(cluster_ids)
    hub_node_indices = [
        i for i, c in enumerate(cluster_idx_of_node) if c < 0
    ]
    num_hubs = len(hub_node_indices)
    print(f"  clusters: {num_clusters}, hubs (non-cluster): {num_hubs}")

    # Carriers + pair_idx.
    carriers = build_carrier_ids(layout, edges, edge_ids=data["edge_ids"])
    pair_idx = build_pair_idx(edges, carriers,
                               exclude_same_carrier=not args.no_carrier)
    print(f"  carriers={len(set(carriers))}  pair_idx={len(pair_idx)}")
    # For carrier-grouped loss: map each pair to a carrier-pair id.
    cp_id_list = None
    num_carrier_pairs = 0
    if args.carrier_grouped_loss:
        cp_id_list, num_carrier_pairs = build_carrier_pair_groups(
            pair_idx, carriers)
        print(f"  carrier-pair groups (Noisy-OR): {num_carrier_pairs}")

    device = torch.device(args.device)
    centers_t = torch.tensor(centers, dtype=torch.float32, device=device)
    sizes_t = torch.tensor(sizes, dtype=torch.float32, device=device)
    edges_t = torch.tensor(edges, dtype=torch.long, device=device)
    pair_t = torch.tensor(pair_idx, dtype=torch.long, device=device)
    cp_id_t = (torch.tensor(cp_id_list, dtype=torch.long, device=device)
               if cp_id_list is not None else None)
    cluster_idx_t = torch.tensor(
        cluster_idx_of_node, dtype=torch.long, device=device
    )
    hub_idx_t = torch.tensor(
        hub_node_indices, dtype=torch.long, device=device
    )
    is_cluster_member = cluster_idx_t >= 0  # [N]

    # Baseline (frozen). For cluster members, also precompute the local
    # offset relative to cluster centroid (used for rotation).
    baseline = centers_t.clone()
    cluster_centroid = torch.zeros(num_clusters, 2, device=device)
    for ci, cid in enumerate(cluster_ids):
        members = cluster_to_members[cid]
        cluster_centroid[ci] = centers_t[members].mean(dim=0)
    # Local offset (baseline) per cluster member relative to its centroid.
    local_offset = baseline - cluster_centroid[cluster_idx_t.clamp(min=0)]

    # Sharpness annealing: linear ramp.
    sharpness_end = args.sharpness_end if args.sharpness_end is not None \
                    else args.sharpness
    print(f"  sharpness={args.sharpness}→{sharpness_end}, "
          f"lr={args.lr}, iters={args.iters}, "
          f"restarts={args.num_restarts}")

    # Identity initial check.
    with torch.no_grad():
        pos = baseline.clone()
        # No translation yet; pos should equal baseline.
        h0 = hard_cross_count(pos, edges, carriers=carriers,
                               count_same_carrier=False)
        print(f"  initial carrier-filtered hard_cross={h0}")

    best_centers = None
    best_h = None

    for restart in range(args.num_restarts):
        seed = args.seed + restart
        torch.manual_seed(seed)
        # Jitter init: per-cluster translation Gaussian, per-hub position
        # Gaussian around baseline. Restart 0 uses zero jitter (reproduces
        # legacy behavior).
        jitter_scale = 0.0 if restart == 0 else args.jitter_std
        # Optimizable variables (re-created each restart).
        cluster_trans_init = torch.zeros(num_clusters, 2, device=device)
        if jitter_scale > 0:
            cluster_trans_init = torch.randn(
                num_clusters, 2, device=device) * jitter_scale
        cluster_trans = cluster_trans_init.detach().clone().requires_grad_(True)
        cluster_rot = torch.zeros(num_clusters, device=device,
                                  requires_grad=args.enable_rotation)
        cluster_log_scale = torch.zeros(num_clusters, device=device,
                                        requires_grad=args.enable_scale)
        hub_init = centers_t[hub_idx_t].clone()
        if jitter_scale > 0:
            hub_init = hub_init + torch.randn_like(hub_init) * jitter_scale
        hub_pos = hub_init.detach().clone().requires_grad_(True)

        params = [cluster_trans, hub_pos]
        if args.enable_rotation:
            params.append(cluster_rot)
        if args.enable_scale:
            params.append(cluster_log_scale)
        optim = torch.optim.Adam(params, lr=args.lr)

        def assemble_pos(_ct=cluster_trans, _cr=cluster_rot,
                         _cls=cluster_log_scale, _hp=hub_pos):
            """Build full pos tensor from cluster transforms + hub_pos."""
            idx = cluster_idx_t.clamp(min=0)
            if args.enable_rotation or args.enable_scale:
                theta = _cr[idx]
                cos_t = torch.cos(theta).unsqueeze(-1)
                sin_t = torch.sin(theta).unsqueeze(-1)
                ox = local_offset[:, 0:1]
                oy = local_offset[:, 1:2]
                rotated_x = ox * cos_t - oy * sin_t
                rotated_y = ox * sin_t + oy * cos_t
                rotated = torch.cat([rotated_x, rotated_y], dim=1)
                if args.enable_scale:
                    scale = torch.exp(_cls[idx]).unsqueeze(-1)
                    rotated = rotated * scale
                member_pos = (cluster_centroid[idx] + _ct[idx] + rotated)
            else:
                member_pos = baseline + _ct[idx]
            pos = member_pos.clone()
            pos = pos.index_copy(0, hub_idx_t, _hp)
            return pos

        if args.num_restarts > 1:
            print(f"  --- restart {restart + 1}/{args.num_restarts} "
                  f"(seed={seed}, jitter_std={jitter_scale:.0f}) ---")

        for it in range(args.iters):
            optim.zero_grad()
            cur_sharp = (args.sharpness
                         + (sharpness_end - args.sharpness)
                         * (it / max(args.iters - 1, 1)))
            pos = assemble_pos()
            if args.carrier_grouped_loss:
                L_cross = soft_cross_loss_carrier_grouped(
                    pos, edges_t, pair_t, cp_id_t, num_carrier_pairs,
                    cur_sharp)
            else:
                L_cross = soft_cross_loss(pos, edges_t, pair_t, cur_sharp)
            ovl = overlap_loss(pos, sizes_t, margin=8.0)
            anchor_trans = (cluster_trans ** 2).sum()
            anchor_hub = ((hub_pos - centers_t[hub_idx_t]) ** 2).sum()
            L = (args.w_cross * L_cross + args.w_overlap * ovl
                 + args.w_anchor_trans * anchor_trans
                 + args.w_anchor_hub * anchor_hub)
            if args.enable_rotation:
                anchor_rot = (cluster_rot ** 2).sum()
                L = L + args.w_anchor_rot * anchor_rot
            if args.enable_scale:
                anchor_scale = (cluster_log_scale ** 2).sum()
                L = L + args.w_anchor_scale * anchor_scale
            L.backward()
            torch.nn.utils.clip_grad_norm_(params, max_norm=500.0)
            optim.step()

            if it == 0 or (it + 1) % max(1, args.iters // 10) == 0:
                with torch.no_grad():
                    pos_now = assemble_pos()
                    hc = hard_cross_count(pos_now, edges,
                                           carriers=carriers,
                                           count_same_carrier=False)
                print(f"  step {it+1:5d}/{args.iters}  K={cur_sharp:.1f}  "
                      f"L={L.item():.2f}  L_cross={L_cross.item():.2f}  "
                      f"ovl={ovl.item():.0f}  hard_cross={hc}")

        with torch.no_grad():
            new_centers = assemble_pos().cpu().numpy().tolist()
        h_after = hard_cross_count(new_centers, edges, carriers=carriers,
                                    count_same_carrier=False)
        if best_h is None or h_after < best_h:
            best_h = h_after
            best_centers = new_centers
            if args.num_restarts > 1:
                print(f"  ✓ restart {restart + 1} new best: "
                      f"hard_cross={h_after}")
        else:
            if args.num_restarts > 1:
                print(f"  ✗ restart {restart + 1}: hard_cross={h_after} "
                      f"(best so far: {best_h})")

    new_centers = best_centers
    h_after = best_h
    print(f"\n  final carrier-filtered hard_cross={h_after}  Δ={h_after - h0:+d}")

    save_layout(layout, new_centers, args.output)
    print(f"  → wrote {args.output}")
    print(f"  → wrote {args.output}.positions.tsv")


if __name__ == "__main__":
    sys.exit(main())
