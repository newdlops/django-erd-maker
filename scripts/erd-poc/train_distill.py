#!/usr/bin/env python3
"""Train GNN to distill ml-layout-polish.py outputs.

Inputs (per graph):
  - graph structure + node/edge features
  - BASELINE positions (C++ pipeline output, normalized)
Output (target):
  - POLISHED positions (after ml-layout-polish.py, normalized to same frame)

Loss: MSE between predicted and polished positions, in shared normalization.
The model predicts a *delta* on top of baseline (residual head) so the
identity mapping (no-change) is the trivial init.

Usage:
  python scripts/erd-poc/train_distill.py --epochs 200 --hidden 96 --layers 4
"""

import argparse
import json
import math
from pathlib import Path
from typing import List, Tuple

import torch
import torch.nn as nn
import torch.nn.functional as F
from torch_geometric.data import Data
from torch_geometric.nn import GATv2Conv

EDGE_KIND_VOCAB = ["foreign_key", "many_to_many", "one_to_one", "inheritance"]


def app_idx_for_label(label: str, vocab_size: int = 16) -> int:
    """Deterministic app_label → embedding index (FNV-1a 32-bit % vocab).

    Replaces Python's built-in hash() which is randomized per process
    (PYTHONHASHSEED) — non-reproducible across training runs and
    inference languages. This FNV-1a implementation matches the JS
    side bit-for-bit so PyTorch / Python ONNX / Node.js ONNX agree.
    """
    h = 0x811c9dc5
    for c in label:
        h = ((h ^ ord(c)) * 0x01000193) & 0xFFFFFFFF
    return h % vocab_size


def soft_cross_loss(pos, edge_index, sharpness=10.0, sample_pairs=2000):
    """Differentiable approximation of edge-segment crossing count.

    For each pair of (forward) edges that don't share endpoints, score the
    likelihood of their straight-line segments intersecting via the cross-
    product sign convention, smoothed with sigmoids. Sum over a random
    subset of pairs (full E^2 is too costly for large graphs).
    """
    fwd = edge_index[:, ::2]  # [2, E]
    E = fwd.shape[1]
    if E < 2:
        return torch.tensor(0.0, device=pos.device)
    a = pos[fwd[0]]
    b = pos[fwd[1]]
    # Sample a subset of edge pairs uniformly to keep cost O(K) rather than
    # O(E^2). K=sample_pairs covers enough crossings to estimate the loss.
    K = min(sample_pairs, E * (E - 1) // 2)
    if K <= 0:
        return torch.tensor(0.0, device=pos.device)
    i_idx = torch.randint(0, E, (K,), device=pos.device)
    j_idx = torch.randint(0, E, (K,), device=pos.device)
    keep = i_idx != j_idx
    if keep.sum() == 0:
        return torch.tensor(0.0, device=pos.device)
    i_idx = i_idx[keep]; j_idx = j_idx[keep]
    # Filter shared-endpoint pairs (those don't form crossings).
    si, ti = fwd[0][i_idx], fwd[1][i_idx]
    sj, tj = fwd[0][j_idx], fwd[1][j_idx]
    keep = (si != sj) & (si != tj) & (ti != sj) & (ti != tj)
    if keep.sum() == 0:
        return torch.tensor(0.0, device=pos.device)
    i_idx = i_idx[keep]; j_idx = j_idx[keep]
    a1 = a[i_idx]; b1 = b[i_idx]; a2 = a[j_idx]; b2 = b[j_idx]
    eps = 1.0
    abx = b1[:, 0] - a1[:, 0]; aby = b1[:, 1] - a1[:, 1]
    cdx = b2[:, 0] - a2[:, 0]; cdy = b2[:, 1] - a2[:, 1]
    ab_len = (abx * abx + aby * aby + eps).sqrt()
    cd_len = (cdx * cdx + cdy * cdy + eps).sqrt()
    ac_x = a2[:, 0] - a1[:, 0]; ac_y = a2[:, 1] - a1[:, 1]
    ad_x = b2[:, 0] - a1[:, 0]; ad_y = b2[:, 1] - a1[:, 1]
    cb_x = b1[:, 0] - a2[:, 0]; cb_y = b1[:, 1] - a2[:, 1]
    ac_len = (ac_x * ac_x + ac_y * ac_y + eps).sqrt()
    ad_len = (ad_x * ad_x + ad_y * ad_y + eps).sqrt()
    cb_len = (cb_x * cb_x + cb_y * cb_y + eps).sqrt()
    c1 = (abx * ac_y - aby * ac_x) / (ab_len * ac_len)
    c2 = (abx * ad_y - aby * ad_x) / (ab_len * ad_len)
    ca_x = -ac_x; ca_y = -ac_y
    c3 = (cdx * ca_y - cdy * ca_x) / (cd_len * ac_len)
    c4 = (cdx * cb_y - cdy * cb_x) / (cd_len * cb_len)
    s1 = torch.sigmoid(sharpness * c1) * torch.sigmoid(-sharpness * c2) \
       + torch.sigmoid(-sharpness * c1) * torch.sigmoid(sharpness * c2)
    s2 = torch.sigmoid(sharpness * c3) * torch.sigmoid(-sharpness * c4) \
       + torch.sigmoid(-sharpness * c3) * torch.sigmoid(sharpness * c4)
    out = (s1 * s2).sum() / max(1, K) * E * (E - 1) / 2  # extrapolate to all pairs
    if torch.isnan(out) or torch.isinf(out):
        return torch.tensor(0.0, device=pos.device, requires_grad=True)
    return out


LEGACY_FEATURES = False  # set by --legacy-features (8 feat instead of 10)


def load_distill_pair(graph_dir: Path, baseline_path: Path,
                      polished_path: Path):
    if not baseline_path.exists() or not polished_path.exists():
        return None
    with (graph_dir / "graph.json").open() as f:
        g = json.load(f)
    with baseline_path.open() as f:
        baseline = json.load(f)
    with polished_path.open() as f:
        polished = json.load(f)

    id2idx = {nd["modelId"]: i for i, nd in enumerate(g["nodes"])}
    n = len(g["nodes"])

    # Baseline + polished centers.
    base_centers = [None] * n
    for nd in baseline["nodes"]:
        if nd["modelId"] not in id2idx: continue
        cx = nd["position"]["x"] + nd["size"]["width"] / 2.0
        cy = nd["position"]["y"] + nd["size"]["height"] / 2.0
        base_centers[id2idx[nd["modelId"]]] = (cx, cy)
    pol_centers = [None] * n
    for nd in polished["nodes"]:
        if nd["modelId"] not in id2idx: continue
        cx = nd["position"]["x"] + nd["size"]["width"] / 2.0
        cy = nd["position"]["y"] + nd["size"]["height"] / 2.0
        pol_centers[id2idx[nd["modelId"]]] = (cx, cy)
    if any(c is None for c in base_centers) or any(c is None for c in pol_centers):
        return None

    base_t = torch.tensor(base_centers, dtype=torch.float32)
    pol_t = torch.tensor(pol_centers, dtype=torch.float32)

    # Use EXPERT's frame for normalization. Earlier we used the baseline
    # frame, but baseline pos_std varies wildly with the user's
    # environment (post-pass settings, cluster fallback, ERD edits) —
    # 16k → 38k → 112k seen on the captain project. The expert frame is
    # stable because it's the "good" target layout and stays compact
    # regardless of baseline noise. Scale-invariant ML: the model
    # learns a delta from baseline (in expert scale) to expert
    # coordinates (in expert scale), and inference unnormalizes with
    # expert pos_std too.
    pos_mean = pol_t.mean(dim=0, keepdim=True)
    pos_std = pol_t.std() + 1e-3
    base_norm = (base_t - pos_mean) / pos_std
    pol_norm = (pol_t - pos_mean) / pos_std

    # Cluster id per node (read from each node's clusterId field in the
    # polished layout). Used to detect inter-cluster edges, which dominate
    # the routing-cross count because they cross long distances through
    # other clusters' regions.
    cluster_by_id = {
        nd["modelId"]: nd.get("clusterId", "")
        for nd in polished.get("nodes", [])
        if nd.get("modelId")
    }

    # B path note: v6 found that 10-feature cluster boundary doesn't help
    # the small-graph corpus. Roll back to 8 features for the seed sweep
    # so we reproduce v3-deep-ft-w's setting with different seeds.
    # Edge / node features (similar to train_gnn.py).
    deg = torch.zeros(n, dtype=torch.long)
    inter_cluster_deg = torch.zeros(n, dtype=torch.long)
    edges_pyg = [[], []]
    edge_attrs = []
    for e in g["edges"]:
        s = id2idx.get(e["sourceModelId"])
        t = id2idx.get(e["targetModelId"])
        if s is None or t is None or s == t:
            continue
        edges_pyg[0].extend([s, t])
        edges_pyg[1].extend([t, s])
        deg[s] += 1
        deg[t] += 1
        kind = e.get("kind", "foreign_key")
        kind_idx = (EDGE_KIND_VOCAB.index(kind)
                    if kind in EDGE_KIND_VOCAB else 0)
        kind_oh = [0.0] * len(EDGE_KIND_VOCAB)
        kind_oh[kind_idx] = 1.0
        s_app = g["nodes"][s].get("appLabel", "")
        t_app = g["nodes"][t].get("appLabel", "")
        intra = 1.0 if s_app == t_app else 0.0
        # Inter-cluster: source/target have different cluster ids.
        s_cid = cluster_by_id.get(e["sourceModelId"], "")
        t_cid = cluster_by_id.get(e["targetModelId"], "")
        if s_cid and t_cid and s_cid != t_cid:
            inter_cluster_deg[s] += 1
            inter_cluster_deg[t] += 1
        edge_attrs.append(kind_oh + [intra, 1.0])
        edge_attrs.append(kind_oh + [intra, 0.0])

    edge_index = torch.tensor(edges_pyg, dtype=torch.long)
    edge_attr = torch.tensor(edge_attrs, dtype=torch.float32) \
        if edge_attrs else torch.zeros(0, len(EDGE_KIND_VOCAB) + 2)

    log_deg = torch.log(deg.float() + 1)
    is_hub = (deg >= 10).float()
    is_leaf = (deg == 1).float()
    width = torch.tensor([nd.get("width", 200.0) for nd in g["nodes"]],
                         dtype=torch.float32)
    height = torch.tensor([nd.get("height", 60.0) for nd in g["nodes"]],
                          dtype=torch.float32)
    # Bundle membership features. Read leafBundles from polished layout's
    # engineMetadata: same hub + leaves merged into one big visual node.
    is_bundle_parent = torch.zeros(n)
    is_bundle_leaf = torch.zeros(n)
    bundle_size = torch.zeros(n)  # # leaves in the bundle the node belongs to
    pol_meta = polished.get("engineMetadata", {}) if isinstance(polished, dict) else {}
    for bundle in pol_meta.get("leafBundles", []) or []:
        parent_mid = bundle.get("parentModelId")
        if parent_mid in id2idx:
            is_bundle_parent[id2idx[parent_mid]] = 1.0
            bundle_size[id2idx[parent_mid]] = float(len(bundle.get("leafModelIds", [])))
        for leaf_mid in bundle.get("leafModelIds", []) or []:
            if leaf_mid in id2idx:
                is_bundle_leaf[id2idx[leaf_mid]] = 1.0
                bundle_size[id2idx[leaf_mid]] = float(len(bundle.get("leafModelIds", [])))
    log_bundle_size = torch.log(bundle_size + 1)
    log_inter_cluster_deg = torch.log(inter_cluster_deg.float() + 1)
    is_cluster_boundary = (inter_cluster_deg > 0).float()
    if LEGACY_FEATURES:
        node_feat = torch.stack([
            log_deg, is_hub, is_leaf,
            torch.log(width + 1) - 5.0,
            torch.log(height + 1) - 4.0,
            is_bundle_parent,
            is_bundle_leaf,
            log_bundle_size,
        ], dim=1)
    else:
        node_feat = torch.stack([
            log_deg, is_hub, is_leaf,
            torch.log(width + 1) - 5.0,
            torch.log(height + 1) - 4.0,
            is_bundle_parent,
            is_bundle_leaf,
            log_bundle_size,
            is_cluster_boundary,
            log_inter_cluster_deg,
        ], dim=1)
    # Per-node weight for cross-critical loss. The dominant sources of
    # routing-crossings are: (1) hubs (many edges), (2) bundle parents
    # (anchor of carrier edges), and (3) cluster-boundary nodes (edges
    # leaving their cluster cross other clusters' territories). Weight
    # each contribution roughly proportional to its expected cross share.
    node_weight = (
        1.0
        + log_deg * 1.5
        + is_bundle_parent * (2.0 + 2.0 * log_bundle_size)
        + is_cluster_boundary * 1.5
        + log_inter_cluster_deg * 1.5
    )
    app_idx = torch.tensor(
        [app_idx_for_label(nd.get("appLabel", "")) for nd in g["nodes"]],
        dtype=torch.long,
    )
    data = Data(
        x=node_feat,
        edge_index=edge_index,
        edge_attr=edge_attr,
        y=pol_norm,            # target: polished positions
        baseline=base_norm,    # input: baseline positions
        app_idx=app_idx,
        node_weight=node_weight,
        n=torch.tensor([n]),
    )
    return data


class ERDDistillDataset:
    def __init__(self, graphs_dir: Path, baseline_dir: Path, polished_dir: Path):
        self.graph_dirs = sorted([d for d in graphs_dir.iterdir() if d.is_dir()])
        self.baseline_dir = baseline_dir
        self.polished_dir = polished_dir
        self._cache = {}

    def len(self):
        return len(self.graph_dirs)

    def get(self, idx):
        if idx not in self._cache:
            g_dir = self.graph_dirs[idx]
            b = self.baseline_dir / f"{g_dir.name}.json"
            p = self.polished_dir / f"{g_dir.name}.json"
            self._cache[idx] = load_distill_pair(g_dir, b, p)
        return self._cache[idx]


class DistillGAT(nn.Module):
    """GNN that takes baseline positions as input and predicts a delta."""

    def __init__(self, node_feat_dim=10, app_vocab=16, edge_feat_dim=6,
                 hidden=96, num_layers=4, num_heads=4, dropout=0.1):
        super().__init__()
        self.app_embed = nn.Embedding(app_vocab, 8)
        self.pos_proj = nn.Linear(2, 16)
        # Input: continuous + app embed + baseline pos embed.
        self.input_proj = nn.Linear(node_feat_dim + 8 + 16, hidden)
        self.edge_proj = nn.Linear(edge_feat_dim, hidden // 2)
        self.layers = nn.ModuleList([
            GATv2Conv(hidden, hidden // num_heads, heads=num_heads,
                      edge_dim=hidden // 2, dropout=dropout)
            for _ in range(num_layers)
        ])
        # Delta head: predict small positional adjustment.
        self.delta_head = nn.Sequential(
            nn.Linear(hidden, hidden),
            nn.ReLU(),
            nn.Dropout(dropout),
            nn.Linear(hidden, 2),
        )
        # Initialize delta head to near-zero for trivial-identity init.
        nn.init.zeros_(self.delta_head[-1].weight)
        nn.init.zeros_(self.delta_head[-1].bias)

    def forward(self, data):
        app_emb = self.app_embed(data.app_idx)
        pos_emb = self.pos_proj(data.baseline)
        x = torch.cat([data.x, app_emb, pos_emb], dim=1)
        h = self.input_proj(x)
        e = self.edge_proj(data.edge_attr) if data.edge_attr.numel() > 0 \
            else None
        for layer in self.layers:
            h_new = layer(h, data.edge_index, e)
            h = h + F.elu(h_new)
        delta = self.delta_head(h)
        return data.baseline + delta  # residual prediction


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--graphs", type=Path, default=Path("data/erd-poc/graphs"))
    p.add_argument("--baseline", type=Path, default=Path("data/erd-poc/layouts"))
    p.add_argument("--polished", type=Path, default=Path("data/erd-poc/polished"))
    p.add_argument("--ckpt", type=Path,
                   default=Path("data/erd-poc/checkpoints/distill.pt"))
    p.add_argument("--epochs", type=int, default=200)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--hidden", type=int, default=96)
    p.add_argument("--layers", type=int, default=4)
    p.add_argument("--dropout", type=float, default=0.1)
    p.add_argument("--weight-decay", type=float, default=1e-4)
    p.add_argument("--patience", type=int, default=30)
    p.add_argument("--device", default="mps")
    p.add_argument("--val-frac", type=float, default=0.1)
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--upweight", type=str, default=None,
                   help="comma-sep graph names to oversample in train set")
    p.add_argument("--upweight-factor", type=int, default=20)
    p.add_argument("--force-val", type=str, default=None,
                   help="comma-sep graph names to force into val set")
    p.add_argument("--w-cross", type=float, default=0.0,
                   help="weight of soft cross loss (0=imitation only)")
    p.add_argument("--cross-sample-pairs", type=int, default=2000)
    p.add_argument("--cross-warmup", type=int, default=20,
                   help="apply cross loss only after this many epochs (let "
                        "imitation establish a sane layout first)")
    p.add_argument("--legacy-features", action="store_true",
                   help="use 8-feature setup (no cluster boundary) — "
                        "matches v3-deep checkpoint")
    p.add_argument("--init-from", type=Path, default=None,
                   help="load weights from this ckpt before training (for FT)")
    args = p.parse_args()
    if args.legacy_features:
        global LEGACY_FEATURES
        LEGACY_FEATURES = True

    torch.manual_seed(args.seed)
    args.ckpt.parent.mkdir(parents=True, exist_ok=True)

    ds = ERDDistillDataset(args.graphs, args.baseline, args.polished)
    valid_indices = [i for i in range(ds.len()) if ds.get(i) is not None]
    print(f"Valid distill pairs: {len(valid_indices)}/{ds.len()}")
    if len(valid_indices) < 1:
        raise RuntimeError("Not enough pairs.")

    rng = torch.Generator().manual_seed(args.seed)
    perm = torch.randperm(len(valid_indices), generator=rng).tolist()
    valid_indices = [valid_indices[i] for i in perm]
    if len(valid_indices) == 1:
        # Single-graph overfit: train and val are both the only graph
        val_idx = list(valid_indices)
        train_idx = list(valid_indices)
    else:
        n_val = max(1, int(len(valid_indices) * args.val_frac))
        val_idx = valid_indices[:n_val]
        train_idx = valid_indices[n_val:]

    name_to_idx = {ds.graph_dirs[i].name: i for i in valid_indices}
    if args.force_val:
        for nm in args.force_val.split(","):
            nm = nm.strip()
            if nm in name_to_idx:
                idx = name_to_idx[nm]
                if idx in train_idx:
                    train_idx.remove(idx)
                if idx not in val_idx:
                    val_idx.append(idx)
                print(f"  forced into val: {nm} (idx={idx})")
    if args.upweight:
        for nm in args.upweight.split(","):
            nm = nm.strip()
            if nm in name_to_idx:
                idx = name_to_idx[nm]
                if idx in train_idx:
                    for _ in range(args.upweight_factor - 1):
                        train_idx.append(idx)
                    print(f"  upweighted {nm} x{args.upweight_factor}")

    print(f"  train={len(train_idx)} val={len(val_idx)}")

    device = torch.device(args.device)
    node_feat_dim = 8 if LEGACY_FEATURES else 10
    model = DistillGAT(
        node_feat_dim=node_feat_dim,
        hidden=args.hidden, num_layers=args.layers, dropout=args.dropout
    ).to(device)
    print(f"Model params: {sum(p.numel() for p in model.parameters()):,}")
    if args.init_from is not None and args.init_from.exists():
        sd = torch.load(args.init_from, map_location=device, weights_only=True)
        model.load_state_dict(sd)
        print(f"  loaded init weights from {args.init_from}")

    optim = torch.optim.AdamW(
        model.parameters(), lr=args.lr, weight_decay=args.weight_decay
    )
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(optim, T_max=args.epochs)

    # Identity baseline: how much does y differ from baseline already?
    baseline_mse = []
    for i in val_idx:
        d = ds.get(i).to(device)
        baseline_mse.append(((d.baseline - d.y) ** 2).mean().item())
    print(f"  identity-baseline val MSE = "
          f"{sum(baseline_mse)/len(baseline_mse):.4f}")

    best_val = float("inf")
    best_epoch = -1
    no_improve = 0
    for epoch in range(args.epochs):
        model.train()
        train_losses = []
        train_imit = []
        train_cross = []
        # Cross-loss kicks in after warmup so the model first learns a
        # reasonable layout via imitation; soft cross is too non-convex to
        # train from scratch.
        cross_w = args.w_cross if epoch >= args.cross_warmup else 0.0
        for i in train_idx:
            d = ds.get(i).to(device)
            optim.zero_grad()
            pred = model(d)
            # Weighted MSE: hub/bundle-parent positions matter more for cross.
            err_sq_node = ((pred - d.y) ** 2).sum(dim=1)
            w = d.node_weight if hasattr(d, "node_weight") else None
            if w is not None:
                imit = (w * err_sq_node).sum() / w.sum()
            else:
                imit = err_sq_node.mean() / 2.0  # /2 since we summed dims
            if cross_w > 0:
                cross = soft_cross_loss(
                    pred, d.edge_index, sample_pairs=args.cross_sample_pairs
                )
                # Normalize cross by per-graph edge count so the weight is
                # graph-size-invariant.
                e_count = max(1, d.edge_index.shape[1] // 2)
                loss = imit + cross_w * (cross / e_count)
                train_cross.append(cross.item() / e_count)
            else:
                loss = imit
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            optim.step()
            train_losses.append(loss.item())
            train_imit.append(imit.item())
        sched.step()

        model.eval()
        val_losses = []
        with torch.no_grad():
            for i in val_idx:
                d = ds.get(i).to(device)
                pred = model(d)
                err_sq_node = ((pred - d.y) ** 2).sum(dim=1)
                w = d.node_weight if hasattr(d, "node_weight") else None
                if w is not None:
                    val_losses.append(((w * err_sq_node).sum() / w.sum()).item())
                else:
                    val_losses.append(err_sq_node.mean().item() / 2.0)

        train_loss = sum(train_losses) / max(len(train_losses), 1)
        val_loss = sum(val_losses) / max(len(val_losses), 1)
        if val_loss < best_val:
            best_val = val_loss
            best_epoch = epoch
            torch.save(model.state_dict(), args.ckpt)
            no_improve = 0
            tag = " ★"
        else:
            no_improve += 1
            tag = ""

        if epoch == 0 or (epoch + 1) % 5 == 0:
            avg_imit = sum(train_imit) / max(len(train_imit), 1)
            avg_cross = sum(train_cross) / max(len(train_cross), 1) if train_cross else 0.0
            cross_str = f"  cross={avg_cross:.4f}" if train_cross else ""
            print(
                f"  epoch {epoch+1:3d}/{args.epochs}  "
                f"train_mse={avg_imit:.4f}{cross_str}  "
                f"val_mse={val_loss:.4f}  best={best_val:.4f}{tag}"
            )
        if no_improve >= args.patience:
            print(f"  early stop at epoch {epoch+1}")
            break

    print(f"\nDone. Best val MSE = {best_val:.4f} (epoch {best_epoch+1})")
    print(f"Identity baseline MSE = "
          f"{sum(baseline_mse)/len(baseline_mse):.4f}")
    print(f"Checkpoint: {args.ckpt}")


if __name__ == "__main__":
    main()
