#!/usr/bin/env python3
"""PoC: train tiny GAT on synthetic ERD corpus, imitation MSE loss.

Inputs:
  data/erd-poc/graphs/<name>/{nodes.tsv,edges.tsv,graph.json}
  data/erd-poc/layouts/<name>.json   (output of C++ pipeline)

Outputs:
  data/erd-poc/checkpoints/best.pt
"""

import argparse
import json
import math
from pathlib import Path
from typing import List, Tuple

import torch
import torch.nn as nn
import torch.nn.functional as F
from torch_geometric.data import Data, Dataset
from torch_geometric.loader import DataLoader
from torch_geometric.nn import GATv2Conv


# ---------------- Data loading ----------------

EDGE_KIND_VOCAB = ["foreign_key", "many_to_many", "one_to_one", "inheritance"]


def load_graph_pair(graph_dir: Path, layout_path: Path) -> Data | None:
    """Load (graph + expert layout) → PyG Data with normalized targets."""
    if not layout_path.exists():
        return None
    with (graph_dir / "graph.json").open() as f:
        g = json.load(f)
    with layout_path.open() as f:
        layout = json.load(f)

    # Map modelId → idx, gather expert positions (centers).
    id2idx = {nd["modelId"]: i for i, nd in enumerate(g["nodes"])}
    centers = [None] * len(g["nodes"])
    for nd in layout["nodes"]:
        if nd["modelId"] not in id2idx:
            continue
        cx = nd["position"]["x"] + nd["size"]["width"] / 2.0
        cy = nd["position"]["y"] + nd["size"]["height"] / 2.0
        centers[id2idx[nd["modelId"]]] = (cx, cy)
    if any(c is None for c in centers):
        return None  # skip if missing positions
    pos = torch.tensor(centers, dtype=torch.float32)

    # Normalize: center at 0, scale by std → unit-variance per graph.
    pos_mean = pos.mean(dim=0, keepdim=True)
    pos = pos - pos_mean
    pos_std = pos.std() + 1e-3
    pos = pos / pos_std

    # Compute degree.
    n = len(g["nodes"])
    deg = torch.zeros(n, dtype=torch.long)
    edges_pyg: List[List[int]] = [[], []]  # [src...], [tgt...]
    edge_attrs = []
    for e in g["edges"]:
        s = id2idx.get(e["sourceModelId"])
        t = id2idx.get(e["targetModelId"])
        if s is None or t is None or s == t:
            continue
        edges_pyg[0].extend([s, t])  # undirected → both directions
        edges_pyg[1].extend([t, s])
        deg[s] += 1
        deg[t] += 1
        # Edge features: kind one-hot + is_intra_app + dir flag (2 entries
        # per edge — one per direction).
        s_app = g["nodes"][s].get("appLabel", "")
        t_app = g["nodes"][t].get("appLabel", "")
        intra = 1.0 if s_app == t_app else 0.0
        kind_idx = EDGE_KIND_VOCAB.index(e["kind"]) \
            if e["kind"] in EDGE_KIND_VOCAB else 0
        kind_onehot = [0.0] * len(EDGE_KIND_VOCAB)
        kind_onehot[kind_idx] = 1.0
        feat = kind_onehot + [intra, 1.0]  # forward
        edge_attrs.append(feat)
        feat_back = kind_onehot + [intra, 0.0]  # backward
        edge_attrs.append(feat_back)

    edge_index = torch.tensor(edges_pyg, dtype=torch.long)
    edge_attr = torch.tensor(edge_attrs, dtype=torch.float32) \
        if edge_attrs else torch.zeros(0, len(EDGE_KIND_VOCAB) + 2)

    # Node features.
    log_deg = torch.log(deg.float() + 1)
    is_hub = (deg >= 10).float()
    is_leaf = (deg == 1).float()
    # App label embed — for PoC use index. Hash to 16 buckets.
    app_idx = torch.tensor(
        [hash(nd.get("appLabel", "")) % 16 for nd in g["nodes"]],
        dtype=torch.long,
    )
    width = torch.tensor(
        [nd.get("width", 200.0) for nd in g["nodes"]],
        dtype=torch.float32,
    )
    height = torch.tensor(
        [nd.get("height", 60.0) for nd in g["nodes"]],
        dtype=torch.float32,
    )
    node_feat = torch.stack([
        log_deg,
        is_hub,
        is_leaf,
        torch.log(width + 1) - 5.0,
        torch.log(height + 1) - 4.0,
    ], dim=1)  # [N, 5] continuous

    # PyG Data — split features into continuous + app idx for embedding lookup.
    data = Data(
        x=node_feat,
        edge_index=edge_index,
        edge_attr=edge_attr,
        y=pos,
        app_idx=app_idx,
        n=torch.tensor([n]),
    )
    return data


class ERDDataset(Dataset):
    def __init__(self, graphs_dir: Path, layouts_dir: Path):
        super().__init__()
        self.graphs_dir = graphs_dir
        self.layouts_dir = layouts_dir
        self.graph_dirs = sorted([d for d in graphs_dir.iterdir() if d.is_dir()])
        self._cache = {}

    def len(self):  # noqa: A003
        return len(self.graph_dirs)

    def get(self, idx):
        if idx not in self._cache:
            g_dir = self.graph_dirs[idx]
            layout_path = self.layouts_dir / f"{g_dir.name}.json"
            self._cache[idx] = load_graph_pair(g_dir, layout_path)
        return self._cache[idx]


# ---------------- Model ----------------


class ERDLayoutGAT(nn.Module):
    def __init__(self, node_feat_dim=5, app_vocab=16, edge_feat_dim=6,
                 hidden=64, num_layers=4, num_heads=4, dropout=0.2):
        super().__init__()
        self.dropout = dropout
        self.app_embed = nn.Embedding(app_vocab, 8)
        self.input_proj = nn.Linear(node_feat_dim + 8, hidden)
        self.edge_proj = nn.Linear(edge_feat_dim, hidden // 2)
        self.layers = nn.ModuleList([
            GATv2Conv(hidden, hidden // num_heads, heads=num_heads,
                      edge_dim=hidden // 2, dropout=dropout)
            for _ in range(num_layers)
        ])
        self.head = nn.Sequential(
            nn.Linear(hidden, hidden),
            nn.ReLU(),
            nn.Dropout(dropout),
            nn.Linear(hidden, 2),
        )

    def forward(self, data):
        app_emb = self.app_embed(data.app_idx)
        x = torch.cat([data.x, app_emb], dim=1)
        h = self.input_proj(x)
        h = F.dropout(h, p=self.dropout, training=self.training)
        e = self.edge_proj(data.edge_attr) if data.edge_attr.numel() > 0 \
            else None
        for layer in self.layers:
            h_new = layer(h, data.edge_index, e)
            h_new = F.elu(h_new)
            # Residual connection (helps depth without overfitting).
            h = h + h_new
        return self.head(h)  # [N, 2]


# ---------------- Training ----------------


def soft_cross_loss(pos, edge_index, sharpness=10.0):
    """Differentiable cross-loss. Stable form with larger eps + nan-guard."""
    fwd = edge_index[:, ::2]  # [2, E]
    E = fwd.shape[1]
    if E < 2:
        return torch.tensor(0.0, device=pos.device)
    a = pos[fwd[0]]
    b = pos[fwd[1]]
    if E > 200:
        idx = torch.randperm(E)[:200]
        a = a[idx]; b = b[idx]
        E = 200
    i_idx, j_idx = torch.triu_indices(E, E, offset=1)
    a1, b1 = a[i_idx], b[i_idx]
    a2, b2 = a[j_idx], b[j_idx]
    # Generous eps — random GNN init can have ~zero distances. eps=1.0 means
    # min meaningful length is 1 unit (fine after we normalize positions).
    eps = 1.0
    abx = b1[:, 0] - a1[:, 0]; aby = b1[:, 1] - a1[:, 1]
    cdx = b2[:, 0] - a2[:, 0]; cdy = b2[:, 1] - a2[:, 1]
    ab_len = (abx ** 2 + aby ** 2 + eps).sqrt()
    cd_len = (cdx ** 2 + cdy ** 2 + eps).sqrt()
    ac_x = a2[:, 0] - a1[:, 0]; ac_y = a2[:, 1] - a1[:, 1]
    ad_x = b2[:, 0] - a1[:, 0]; ad_y = b2[:, 1] - a1[:, 1]
    ca_x = -ac_x; ca_y = -ac_y
    cb_x = b1[:, 0] - a2[:, 0]; cb_y = b1[:, 1] - a2[:, 1]
    ac_len = (ac_x ** 2 + ac_y ** 2 + eps).sqrt()
    ad_len = (ad_x ** 2 + ad_y ** 2 + eps).sqrt()
    cb_len = (cb_x ** 2 + cb_y ** 2 + eps).sqrt()
    c1 = (abx * ac_y - aby * ac_x) / (ab_len * ac_len)
    c2 = (abx * ad_y - aby * ad_x) / (ab_len * ad_len)
    c3 = (cdx * ca_y - cdy * ca_x) / (cd_len * ac_len)
    c4 = (cdx * cb_y - cdy * cb_x) / (cd_len * cb_len)
    s1 = torch.sigmoid(sharpness * c1) * torch.sigmoid(-sharpness * c2) \
       + torch.sigmoid(-sharpness * c1) * torch.sigmoid(sharpness * c2)
    s2 = torch.sigmoid(sharpness * c3) * torch.sigmoid(-sharpness * c4) \
       + torch.sigmoid(-sharpness * c3) * torch.sigmoid(sharpness * c4)
    out = (s1 * s2).sum()
    # NaN guard.
    if torch.isnan(out) or torch.isinf(out):
        return torch.tensor(0.0, device=pos.device, requires_grad=True)
    return out


def imitation_loss(pred, target, batch=None):
    """MSE on predicted vs target positions, accounting for free rotation/flip.

    ERD layouts are equivalent under rotation & reflection. Naive MSE penalizes
    orientation differences. Use Procrustes-aligned MSE: for each graph, find
    optimal rotation R that minimizes ||R*pred - target||² and report aligned
    residual. This frees the model from learning a specific orientation.
    """
    # For PoC simplicity: per-graph orthogonal Procrustes alignment.
    if batch is None:
        # Single graph.
        return _procrustes_mse(pred, target)
    # Multiple graphs in batch — split by graph.
    losses = []
    for g_idx in range(int(batch.max().item()) + 1):
        mask = batch == g_idx
        losses.append(_procrustes_mse(pred[mask], target[mask]))
    return torch.stack(losses).mean()


def _procrustes_mse(pred, target):
    # Both shape [N, 2]. Center.
    pred_c = pred - pred.mean(dim=0, keepdim=True)
    target_c = target - target.mean(dim=0, keepdim=True)
    # Solve R that minimizes ||R pred - target||²: R = U V^T where pred^T target = U S V^T.
    M = pred_c.T @ target_c  # [2, 2]
    U, _, Vh = torch.linalg.svd(M)
    R = Vh.T @ U.T  # [2, 2]
    # Allow reflection (det may be -1) — ERDs are flip-invariant.
    pred_aligned = pred_c @ R.T
    return ((pred_aligned - target_c) ** 2).mean()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--graphs", type=Path, default=Path("data/erd-poc/graphs"))
    p.add_argument("--layouts", type=Path, default=Path("data/erd-poc/layouts"))
    p.add_argument("--ckpt", type=Path,
                   default=Path("data/erd-poc/checkpoints/best.pt"))
    p.add_argument("--epochs", type=int, default=200)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--hidden", type=int, default=128)
    p.add_argument("--layers", type=int, default=4)
    p.add_argument("--device", default="mps")
    p.add_argument("--val-frac", type=float, default=0.2)
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--w-cross", type=float, default=0.0,
                   help="self-supervised soft cross loss weight (0=imit only)")
    p.add_argument("--cross-warmup-epochs", type=int, default=0,
                   help="epochs of imitation-only before adding cross loss")
    p.add_argument("--dropout", type=float, default=0.2)
    p.add_argument("--weight-decay", type=float, default=1e-3)
    p.add_argument("--patience", type=int, default=30,
                   help="early stop after N epochs without val improvement")
    p.add_argument("--load", type=Path, default=None,
                   help="load weights from checkpoint before training")
    args = p.parse_args()

    torch.manual_seed(args.seed)
    args.ckpt.parent.mkdir(parents=True, exist_ok=True)

    full_ds = ERDDataset(args.graphs, args.layouts)
    # Filter to only graphs where layout exists.
    valid_indices = [i for i in range(full_ds.len()) if full_ds.get(i) is not None]
    print(f"Found {len(valid_indices)}/{full_ds.len()} graphs with layouts")
    if len(valid_indices) < 5:
        raise RuntimeError("Not enough graphs with layouts. Run pipeline first.")

    # Train/val split.
    rng = torch.Generator().manual_seed(args.seed)
    perm = torch.randperm(len(valid_indices), generator=rng).tolist()
    valid_indices = [valid_indices[i] for i in perm]
    n_val = max(1, int(len(valid_indices) * args.val_frac))
    val_indices = valid_indices[:n_val]
    train_indices = valid_indices[n_val:]
    print(f"  train={len(train_indices)}, val={len(val_indices)}")

    device = torch.device(args.device)
    model = ERDLayoutGAT(
        hidden=args.hidden, num_layers=args.layers, dropout=args.dropout
    ).to(device)
    if args.load and args.load.exists():
        model.load_state_dict(torch.load(args.load, map_location=device,
                                          weights_only=True))
        print(f"  loaded weights from {args.load}")
    print(f"Model params: {sum(p.numel() for p in model.parameters()):,}")
    print(f"  dropout={args.dropout} weight_decay={args.weight_decay} "
          f"w_cross={args.w_cross} warmup={args.cross_warmup_epochs}")
    optim = torch.optim.AdamW(
        model.parameters(), lr=args.lr, weight_decay=args.weight_decay
    )
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(optim, T_max=args.epochs)

    best_val = float("inf")
    best_epoch = -1
    no_improve = 0
    last_phase = "imit"
    for epoch in range(args.epochs):
        # Train.
        model.train()
        train_losses = []
        train_imit = []
        train_cross = []
        cross_active = (
            args.w_cross > 0 and epoch >= args.cross_warmup_epochs
        )
        cur_phase = "cross" if cross_active else "imit"
        if cur_phase != last_phase:
            # Phase transition — reset early-stop tracking, save best again.
            print(f"  >>> phase change at epoch {epoch+1}: "
                  f"{last_phase} → {cur_phase}, resetting early-stop")
            best_val = float("inf")
            no_improve = 0
            last_phase = cur_phase
        for i in train_indices:
            data = full_ds.get(i).to(device)
            optim.zero_grad()
            pred = model(data)
            L_imit = imitation_loss(pred, data.y)
            if cross_active:
                L_cross = soft_cross_loss(pred, data.edge_index)
                L_cross = L_cross / max(data.edge_index.shape[1] // 2, 1)
                loss = L_imit + args.w_cross * L_cross
                train_cross.append(L_cross.item())
            else:
                loss = L_imit
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            optim.step()
            train_losses.append(loss.item())
            train_imit.append(L_imit.item())
        sched.step()

        # Validate (imitation MSE only — primary metric).
        model.eval()
        val_losses = []
        with torch.no_grad():
            for i in val_indices:
                data = full_ds.get(i).to(device)
                pred = model(data)
                loss = imitation_loss(pred, data.y)
                val_losses.append(loss.item())

        train_loss = sum(train_losses) / max(len(train_losses), 1)
        train_imit_loss = sum(train_imit) / max(len(train_imit), 1)
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
            extra = f"  cross={sum(train_cross)/max(len(train_cross),1):.3f}" \
                if train_cross else ""
            print(
                f"  epoch {epoch+1:3d}/{args.epochs}  "
                f"train={train_loss:.4f}/imit={train_imit_loss:.4f}{extra}  "
                f"val={val_loss:.4f}  best={best_val:.4f}{tag}"
            )
        if no_improve >= args.patience:
            print(f"  early stop at epoch {epoch+1} "
                  f"(no improvement for {args.patience} epochs)")
            break

    print(f"\nDone. Best val MSE = {best_val:.4f} (epoch {best_epoch+1})")
    print(f"Checkpoint: {args.ckpt}")


if __name__ == "__main__":
    main()
