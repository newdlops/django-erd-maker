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

    # Use BASELINE's frame for normalization (same for both → comparable).
    pos_mean = base_t.mean(dim=0, keepdim=True)
    pos_std = base_t.std() + 1e-3
    base_norm = (base_t - pos_mean) / pos_std
    pol_norm = (pol_t - pos_mean) / pos_std

    # Edge / node features (similar to train_gnn.py).
    deg = torch.zeros(n, dtype=torch.long)
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
    node_feat = torch.stack([
        log_deg, is_hub, is_leaf,
        torch.log(width + 1) - 5.0,
        torch.log(height + 1) - 4.0,
    ], dim=1)
    app_idx = torch.tensor(
        [hash(nd.get("appLabel", "")) % 16 for nd in g["nodes"]],
        dtype=torch.long,
    )
    data = Data(
        x=node_feat,
        edge_index=edge_index,
        edge_attr=edge_attr,
        y=pol_norm,            # target: polished positions
        baseline=base_norm,    # input: baseline positions
        app_idx=app_idx,
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

    def __init__(self, node_feat_dim=5, app_vocab=16, edge_feat_dim=6,
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
    args = p.parse_args()

    torch.manual_seed(args.seed)
    args.ckpt.parent.mkdir(parents=True, exist_ok=True)

    ds = ERDDistillDataset(args.graphs, args.baseline, args.polished)
    valid_indices = [i for i in range(ds.len()) if ds.get(i) is not None]
    print(f"Valid distill pairs: {len(valid_indices)}/{ds.len()}")
    if len(valid_indices) < 5:
        raise RuntimeError("Not enough pairs.")

    rng = torch.Generator().manual_seed(args.seed)
    perm = torch.randperm(len(valid_indices), generator=rng).tolist()
    valid_indices = [valid_indices[i] for i in perm]
    n_val = max(1, int(len(valid_indices) * args.val_frac))
    val_idx = valid_indices[:n_val]
    train_idx = valid_indices[n_val:]
    print(f"  train={len(train_idx)} val={len(val_idx)}")

    device = torch.device(args.device)
    model = DistillGAT(
        hidden=args.hidden, num_layers=args.layers, dropout=args.dropout
    ).to(device)
    print(f"Model params: {sum(p.numel() for p in model.parameters()):,}")

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
        for i in train_idx:
            d = ds.get(i).to(device)
            optim.zero_grad()
            pred = model(d)
            loss = ((pred - d.y) ** 2).mean()
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            optim.step()
            train_losses.append(loss.item())
        sched.step()

        model.eval()
        val_losses = []
        with torch.no_grad():
            for i in val_idx:
                d = ds.get(i).to(device)
                pred = model(d)
                val_losses.append(((pred - d.y) ** 2).mean().item())

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
            print(
                f"  epoch {epoch+1:3d}/{args.epochs}  "
                f"train_mse={train_loss:.4f}  val_mse={val_loss:.4f}"
                f"  best={best_val:.4f}{tag}"
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
