#!/usr/bin/env python3
"""DDPM-style diffusion model for graph layout.

Forward process: gradual Gaussian noise injection into node positions.
Reverse process: GNN denoiser predicts noise ε given (noisy_pos, t, graph).
Inference: sample x_T ~ N(0, I), iteratively denoise to x_0 (layout).

Each diffusion step uses message-passing over the graph so positions
co-adapt to topology. Time embedded via sinusoidal projection +
FiLM-style conditioning into each GAT layer.

The hope: diffusion learns the global multimodal distribution of
"good" layouts (low cross, compact bbox) without needing per-step
local optimization like §13.

Phase 1 goal: prove the pipeline works end-to-end on small graphs.
Phase 2: scale to captain (1250 nodes) + cross-guided sampling.
"""

import argparse
import json
import math
import time
from collections import defaultdict
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
from torch_geometric.nn import GATv2Conv

import importlib.util
spec = importlib.util.spec_from_file_location(
    "train_distill", Path(__file__).parent / "train_distill.py"
)
td = importlib.util.module_from_spec(spec); spec.loader.exec_module(td)

ROOT = Path(__file__).resolve().parents[2]
EDGE_KIND_VOCAB = ["foreign_key", "many_to_many", "one_to_one", "inheritance"]


def linear_beta_schedule(T: int, beta_start=1e-4, beta_end=0.02):
    """Standard linear noise schedule."""
    return torch.linspace(beta_start, beta_end, T)


def overlap_loss(pos_real, widths, heights, sample_pairs=3000, margin=20.0):
    """Differentiable rectangle-overlap penalty.

    For sampled node pairs, compute axis-aligned bbox overlap with a
    safety margin (nodes shouldn't touch). Sum smooth penalty over
    overlapping pairs. Lower = no overlaps.

    pos_real: [N, 2] node centers in REAL coords.
    widths/heights: [N] node dimensions.
    """
    n = pos_real.shape[0]
    if n < 2:
        return torch.tensor(0.0, device=pos_real.device, requires_grad=True)
    K = min(sample_pairs, n * (n - 1) // 2)
    if K <= 0:
        return torch.tensor(0.0, device=pos_real.device, requires_grad=True)
    i_idx = torch.randint(0, n, (K,), device=pos_real.device)
    j_idx = torch.randint(0, n, (K,), device=pos_real.device)
    keep = i_idx != j_idx
    if keep.sum() == 0:
        return torch.tensor(0.0, device=pos_real.device, requires_grad=True)
    i_idx = i_idx[keep]; j_idx = j_idx[keep]
    pi = pos_real[i_idx]; pj = pos_real[j_idx]
    # Required x-distance = (w_i + w_j)/2 + margin
    req_dx = (widths[i_idx] + widths[j_idx]) / 2.0 + margin
    req_dy = (heights[i_idx] + heights[j_idx]) / 2.0 + margin
    dx = (pi[:, 0] - pj[:, 0]).abs()
    dy = (pi[:, 1] - pj[:, 1]).abs()
    # Overlap on each axis: max(0, req - actual)
    over_x = torch.relu(req_dx - dx)
    over_y = torch.relu(req_dy - dy)
    # Both axes must overlap for a rect-overlap. Use product (smooth).
    pair_overlap = over_x * over_y
    return pair_overlap.sum() / max(1, K)


def overlap_subset_loss(pos_real, widths, heights, sample_nodes=384, margin=20.0):
    """Rectangle-overlap penalty over all pairs in a random node subset.

    Uniform random edge-pair sampling often misses rare collision pairs on
    1k+ node graphs. Sampling a node subset and evaluating every internal
    pair keeps the same average-over-pairs scale, but gives a much lower
    chance of a zero-gradient overlap step.
    """
    n = pos_real.shape[0]
    if n < 2:
        return torch.tensor(0.0, device=pos_real.device, requires_grad=True)
    m = min(sample_nodes, n)
    if m < n:
        idx = torch.randperm(n, device=pos_real.device)[:m]
        pos = pos_real[idx]
        w = widths[idx]
        h = heights[idx]
    else:
        pos = pos_real
        w = widths
        h = heights
    ii, jj = torch.triu_indices(m, m, offset=1, device=pos_real.device)
    if ii.numel() == 0:
        return torch.tensor(0.0, device=pos_real.device, requires_grad=True)
    dx = (pos[ii, 0] - pos[jj, 0]).abs()
    dy = (pos[ii, 1] - pos[jj, 1]).abs()
    req_dx = (w[ii] + w[jj]) / 2.0 + margin
    req_dy = (h[ii] + h[jj]) / 2.0 + margin
    over_x = torch.relu(req_dx - dx)
    over_y = torch.relu(req_dy - dy)
    return (over_x * over_y).sum() / ii.numel()


def get_diffusion_constants(T: int, device="cpu"):
    betas = linear_beta_schedule(T).to(device)
    alphas = 1.0 - betas
    alpha_bars = torch.cumprod(alphas, dim=0)
    return {
        "betas": betas, "alphas": alphas, "alpha_bars": alpha_bars,
        "sqrt_alpha_bars": torch.sqrt(alpha_bars),
        "sqrt_one_minus_alpha_bars": torch.sqrt(1 - alpha_bars),
        "sqrt_recip_alphas": torch.sqrt(1.0 / alphas),
    }


def time_embedding(t: torch.Tensor, dim: int):
    """Sinusoidal time embedding (Vaswani-style).
    t: [B] integer timesteps. Returns [B, dim].
    """
    device = t.device
    half = dim // 2
    freqs = torch.exp(
        -math.log(10000.0) * torch.arange(half, device=device) / half
    )
    args = t.float().unsqueeze(-1) * freqs.unsqueeze(0)
    emb = torch.cat([torch.sin(args), torch.cos(args)], dim=-1)
    if dim % 2 == 1:
        emb = F.pad(emb, (0, 1))
    return emb


class GraphDenoiser(nn.Module):
    """GNN denoiser: predicts ε given (x_t, t, graph)."""
    def __init__(self, node_feat_dim=10, hidden=128, num_layers=4,
                 num_heads=4, edge_feat_dim=6, time_dim=64):
        super().__init__()
        self.time_dim = time_dim
        # Time MLP
        self.time_mlp = nn.Sequential(
            nn.Linear(time_dim, hidden), nn.SiLU(),
            nn.Linear(hidden, hidden),
        )
        # Position encoding (current noisy position)
        self.pos_proj = nn.Linear(2, hidden // 2)
        # Static node feature encoding
        self.feat_proj = nn.Linear(node_feat_dim, hidden // 2)
        # GAT stack
        self.edge_proj = nn.Linear(edge_feat_dim, hidden // 2)
        self.layers = nn.ModuleList([
            GATv2Conv(hidden, hidden // num_heads, heads=num_heads,
                      edge_dim=hidden // 2, dropout=0.0)
            for _ in range(num_layers)
        ])
        # FiLM-style time conditioning per layer (scale + shift)
        self.time_films = nn.ModuleList([
            nn.Linear(hidden, 2 * hidden) for _ in range(num_layers)
        ])
        # Output: predict ε [2]
        self.out_proj = nn.Sequential(
            nn.Linear(hidden, hidden), nn.SiLU(),
            nn.Linear(hidden, 2),
        )

    def forward(self, x_t, t, node_feat, edge_index, edge_attr):
        """
        x_t: [N, 2] noisy positions
        t:   [1] timestep (scalar tensor)
        node_feat: [N, node_feat_dim]
        edge_index: [2, E]
        edge_attr: [E, edge_feat_dim]

        Returns ε̂ [N, 2].
        """
        n = x_t.shape[0]
        # Time embedding (single value, broadcast)
        t_emb = time_embedding(t, self.time_dim)  # [1, time_dim]
        t_emb = self.time_mlp(t_emb).expand(n, -1)  # [N, hidden]
        # Encode position + features
        h = torch.cat([self.pos_proj(x_t), self.feat_proj(node_feat)],
                      dim=-1)  # [N, hidden]
        e = self.edge_proj(edge_attr) if edge_attr.numel() > 0 else None
        # GNN with time conditioning
        for i, layer in enumerate(self.layers):
            film = self.time_films[i](t_emb)
            scale, shift = film.chunk(2, dim=-1)
            h = h * (1 + scale) + shift
            h = h + F.silu(layer(h, edge_index, e))
        return self.out_proj(h)


def build_graph_data(layout: dict, expert_layout: dict | None = None):
    """Build PyG-like Data dict for a layout.
    Returns: x [N, 10], edge_index [2, E*2], edge_attr [E*2, 6],
             positions_norm [N, 2] (normalized), pos_mean, pos_std
    """
    nodes = layout["nodes"]
    routed = layout.get("routedEdges", [])
    n = len(nodes)
    id2 = {nd["modelId"]: i for i, nd in enumerate(nodes)}
    # Positions
    centers = np.array([
        [nd["position"]["x"] + nd["size"]["width"] / 2.0,
         nd["position"]["y"] + nd["size"]["height"] / 2.0]
        for nd in nodes], dtype=np.float32)
    base_t = torch.from_numpy(centers)
    # Normalize: use expert frame if provided (same scheme as v12)
    if expert_layout is not None:
        exp_c = np.array([
            [nd["position"]["x"] + nd["size"]["width"] / 2.0,
             nd["position"]["y"] + nd["size"]["height"] / 2.0]
            for nd in expert_layout["nodes"]], dtype=np.float32)
        pos_mean = torch.from_numpy(exp_c.mean(0)).unsqueeze(0)
        pos_std = float(np.std(exp_c) + 1e-3)
    else:
        pos_mean = base_t.mean(0, keepdim=True)
        pos_std = float(base_t.std().item() + 1e-3)
    pos_norm = (base_t - pos_mean) / pos_std

    # Edges + features (same as train_distill)
    cluster_by = {nd["modelId"]: (nd.get("clusterId") or "")
                  for nd in nodes}
    deg = torch.zeros(n, dtype=torch.long)
    inter_cluster_deg = torch.zeros(n, dtype=torch.long)
    edges_a, edges_b = [], []
    edge_attrs = []
    for re in routed:
        s = id2.get(re.get("sourceModelId"))
        t = id2.get(re.get("targetModelId"))
        if s is None or t is None or s == t: continue
        edges_a.extend([s, t]); edges_b.extend([t, s])
        deg[s] += 1; deg[t] += 1
        kind = re.get("kind", "foreign_key")
        kind_idx = EDGE_KIND_VOCAB.index(kind) if kind in EDGE_KIND_VOCAB else 0
        kind_oh = [0.0] * len(EDGE_KIND_VOCAB)
        kind_oh[kind_idx] = 1.0
        app_s = nodes[s]["modelId"].split(".")[0]
        app_t = nodes[t]["modelId"].split(".")[0]
        intra = 1.0 if app_s == app_t else 0.0
        sc = cluster_by.get(re.get("sourceModelId"))
        tc = cluster_by.get(re.get("targetModelId"))
        if sc and tc and sc != tc:
            inter_cluster_deg[s] += 1
            inter_cluster_deg[t] += 1
        edge_attrs.append(kind_oh + [intra, 1.0])
        edge_attrs.append(kind_oh + [intra, 0.0])
    edge_index = (torch.tensor([edges_a, edges_b], dtype=torch.long)
                  if edges_a else torch.zeros(2, 0, dtype=torch.long))
    edge_attr = (torch.tensor(edge_attrs, dtype=torch.float32)
                 if edge_attrs else torch.zeros(0, 6))

    # Node features
    log_deg = torch.log(deg.float() + 1)
    is_hub = (deg >= 10).float()
    is_leaf = (deg == 1).float()
    width = torch.tensor([nd["size"]["width"] for nd in nodes],
                          dtype=torch.float32)
    height = torch.tensor([nd["size"]["height"] for nd in nodes],
                           dtype=torch.float32)
    is_bundle_parent = torch.zeros(n)
    is_bundle_leaf = torch.zeros(n)
    bundle_size = torch.zeros(n)
    leaf_bundles = (layout.get("engineMetadata") or {}).get("leafBundles") or []
    for b in leaf_bundles:
        pid = b.get("parentModelId")
        if pid in id2:
            is_bundle_parent[id2[pid]] = 1.0
            bundle_size[id2[pid]] = float(len(b.get("leafModelIds", [])))
        for lid in (b.get("leafModelIds") or []):
            if lid in id2:
                is_bundle_leaf[id2[lid]] = 1.0
                bundle_size[id2[lid]] = float(len(b.get("leafModelIds", [])))
    log_bundle_size = torch.log(bundle_size + 1)
    log_inter_cluster_deg = torch.log(inter_cluster_deg.float() + 1)
    is_cluster_boundary = (inter_cluster_deg > 0).float()
    node_feat = torch.stack([
        log_deg, is_hub, is_leaf,
        torch.log(width + 1) - 5.0,
        torch.log(height + 1) - 4.0,
        is_bundle_parent, is_bundle_leaf, log_bundle_size,
        is_cluster_boundary, log_inter_cluster_deg,
    ], dim=1)
    return {
        "x": node_feat, "edge_index": edge_index, "edge_attr": edge_attr,
        "positions_norm": pos_norm, "pos_mean": pos_mean, "pos_std": pos_std,
        "node_w": width, "node_h": height,
    }


def load_corpus(graphs_dir: Path, layouts_dir: Path, max_graphs=None,
                 upweight_name=None, upweight_factor=1):
    """Load (graph_features, target_positions) pairs from corpus.

    upweight_name: if set, file matching this name is replicated upweight_factor times.
    """
    layout_files = sorted(layouts_dir.glob("*.json"))
    if max_graphs:
        layout_files = layout_files[:max_graphs]
    corpus = []
    upweight_data = None
    for lf in layout_files:
        try:
            layout = json.loads(lf.read_text())
            if not layout.get("nodes"):
                continue
            data = build_graph_data(layout)
            if data["x"].shape[0] < 5 or data["edge_index"].shape[1] < 2:
                continue
            corpus.append(data)
            if upweight_name and lf.stem == upweight_name:
                upweight_data = data
        except Exception as e:
            print(f"  skip {lf.name}: {e}")
    if upweight_data and upweight_factor > 1:
        for _ in range(upweight_factor - 1):
            corpus.append(upweight_data)
        print(f"  upweighted '{upweight_name}' x{upweight_factor}")
    return corpus


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--layouts", type=Path,
                   default=ROOT / "data/erd-poc/expert-strong")
    p.add_argument("--ckpt", type=Path, required=True)
    p.add_argument("--init-from", type=Path, default=None)
    p.add_argument("--T", type=int, default=200, help="diffusion timesteps")
    p.add_argument("--epochs", type=int, default=50)
    p.add_argument("--lr", type=float, default=3e-4)
    p.add_argument("--hidden", type=int, default=128)
    p.add_argument("--layers", type=int, default=4)
    p.add_argument("--max-graphs", type=int, default=None)
    p.add_argument("--upweight", type=str, default=None,
                   help="layout filename stem to upweight (e.g. 'real-main')")
    p.add_argument("--upweight-factor", type=int, default=30)
    p.add_argument("--device", default="cpu")
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--save-every", type=int, default=5)
    p.add_argument("--w-cross", type=float, default=0.0,
                   help="weight of soft cross loss on x_0 estimate (0=off)")
    p.add_argument("--cross-warmup", type=int, default=10,
                   help="add cross loss only after this many epochs")
    p.add_argument("--cross-sample-pairs", type=int, default=2000)
    p.add_argument("--node-drop-prob", type=float, default=0.0,
                   help="per-node drop probability during training (0=off). "
                        "Augments model robustness to variable node sets — "
                        "captain ERD grows post-training so model must handle "
                        "missing/new nodes gracefully.")
    p.add_argument("--w-overlap", type=float, default=0.0,
                   help="weight of overlap loss on x_0 estimate (0=off)")
    p.add_argument("--overlap-margin", type=float, default=20.0,
                   help="extra spacing (real units) required between nodes")
    p.add_argument("--overlap-sample-pairs", type=int, default=3000)
    p.add_argument("--overlap-mode", choices=("random", "subset"),
                   default="random",
                   help="random = sampled pairs; subset = all pairs among a "
                        "random node subset")
    p.add_argument("--overlap-sample-nodes", type=int, default=384,
                   help="node subset size for --overlap-mode subset")
    args = p.parse_args()

    torch.manual_seed(args.seed)
    args.ckpt.parent.mkdir(parents=True, exist_ok=True)

    print(f"Loading corpus from {args.layouts}...")
    corpus = load_corpus(args.layouts, args.layouts, args.max_graphs,
                          upweight_name=args.upweight,
                          upweight_factor=args.upweight_factor)
    print(f"  loaded {len(corpus)} valid graphs")
    sizes = [d["x"].shape[0] for d in corpus]
    print(f"  graph sizes: min={min(sizes)} max={max(sizes)} mean={sum(sizes)/len(sizes):.0f}")

    device = torch.device(args.device)
    diff = get_diffusion_constants(args.T, device=device)

    model = GraphDenoiser(node_feat_dim=10, hidden=args.hidden,
                           num_layers=args.layers, edge_feat_dim=6).to(device)
    if args.init_from and args.init_from.exists():
        sd = torch.load(args.init_from, map_location=device, weights_only=True)
        own = model.state_dict()
        loaded = 0
        for k, v in sd.items():
            if k in own and own[k].shape == v.shape:
                own[k] = v; loaded += 1
        model.load_state_dict(own)
        print(f"  loaded {loaded}/{len(sd)} from init")

    optim = torch.optim.AdamW(model.parameters(), lr=args.lr,
                                weight_decay=1e-5)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(
        optim, T_max=args.epochs)

    n_params = sum(p.numel() for p in model.parameters())
    print(f"  model params: {n_params:,}")

    t0 = time.time()
    for epoch in range(args.epochs):
        model.train()
        epoch_eps_loss = 0.0
        epoch_cross_loss = 0.0
        epoch_overlap_loss = 0.0
        n_steps = 0
        # Apply cross loss only after warmup (let model first learn denoising)
        use_cross = args.w_cross > 0 and epoch >= args.cross_warmup
        # Shuffle corpus each epoch
        perm = torch.randperm(len(corpus))
        for idx in perm:
            d = corpus[idx]
            x_0 = d["positions_norm"].to(device)  # [N, 2]
            n = x_0.shape[0]
            node_feat = d["x"].to(device)
            edge_index = d["edge_index"].to(device)
            edge_attr = d["edge_attr"].to(device)
            pos_std = d["pos_std"]
            pos_mean = d["pos_mean"].to(device)
            node_w = d["node_w"].to(device)
            node_h = d["node_h"].to(device)
            # Optional node-drop augmentation: forces model to be robust to
            # variable node sets (captain ERD grows over time; runtime has
            # nodes not seen during training).
            if args.node_drop_prob > 0:
                keep_mask = torch.rand(n, device=device) > args.node_drop_prob
                # Keep at least 80% of nodes (don't drop too aggressively)
                if keep_mask.sum() < int(n * 0.8):
                    # Re-sample to keep more
                    keep_mask = torch.rand(n, device=device) > (args.node_drop_prob * 0.5)
                kept = keep_mask.nonzero(as_tuple=True)[0]
                if kept.shape[0] < n:
                    # Build idx remap: old idx → new idx
                    new_idx = torch.full((n,), -1, dtype=torch.long, device=device)
                    new_idx[kept] = torch.arange(kept.shape[0], device=device)
                    # Filter edges: both endpoints kept
                    src = edge_index[0]
                    tgt = edge_index[1]
                    edge_keep = (new_idx[src] >= 0) & (new_idx[tgt] >= 0)
                    edge_index = torch.stack([new_idx[src[edge_keep]],
                                               new_idx[tgt[edge_keep]]])
                    edge_attr = edge_attr[edge_keep]
                    # Filter node tensors
                    x_0 = x_0[kept]
                    node_feat = node_feat[kept]
                    node_w = node_w[kept]
                    node_h = node_h[kept]
                    n = kept.shape[0]
            # Sample random timestep
            t = torch.randint(0, args.T, (1,), device=device)
            # Sample ε ~ N(0, I)
            eps = torch.randn_like(x_0)
            # Forward: x_t = √(α_bar_t) · x_0 + √(1 - α_bar_t) · ε
            sab = diff["sqrt_alpha_bars"][t]
            soab = diff["sqrt_one_minus_alpha_bars"][t]
            x_t = sab * x_0 + soab * eps
            # Predict ε̂
            eps_hat = model(x_t, t, node_feat, edge_index, edge_attr)
            # Base loss: MSE on noise prediction
            eps_loss = F.mse_loss(eps_hat, eps)
            loss = eps_loss
            # Optional cross + overlap loss on estimated x_0
            cross_loss_val = 0.0
            overlap_loss_val = 0.0
            use_overlap = args.w_overlap > 0 and epoch >= args.cross_warmup
            if (use_cross or use_overlap) and 0.05 < soab.item() < 0.7:
                x_0_est = (x_t - soab * eps_hat) / sab
                x_real = x_0_est * pos_std + pos_mean
                if use_cross and edge_index.shape[1] > 0:
                    edge_fwd = edge_index[:, ::2]
                    import importlib.util as _ilu
                    if not hasattr(main, "_scl"):
                        _spec = _ilu.spec_from_file_location(
                            "train_distill",
                            Path(__file__).parent / "train_distill.py")
                        _td = _ilu.module_from_spec(_spec)
                        _spec.loader.exec_module(_td)
                        main._scl = _td.soft_cross_loss
                    cross_val = main._scl(
                        x_real, edge_fwd,
                        sample_pairs=args.cross_sample_pairs)
                    e_count = max(1, edge_fwd.shape[1])
                    cross_norm = cross_val / e_count
                    loss = loss + args.w_cross * cross_norm
                    cross_loss_val = cross_norm.item()
                if use_overlap:
                    if args.overlap_mode == "subset":
                        ov = overlap_subset_loss(
                            x_real, node_w, node_h,
                            sample_nodes=args.overlap_sample_nodes,
                            margin=args.overlap_margin)
                    else:
                        ov = overlap_loss(
                            x_real, node_w, node_h,
                            sample_pairs=args.overlap_sample_pairs,
                            margin=args.overlap_margin)
                    loss = loss + args.w_overlap * ov
                    overlap_loss_val = ov.item()
            optim.zero_grad()
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            optim.step()
            epoch_eps_loss += eps_loss.item()
            epoch_cross_loss += cross_loss_val
            epoch_overlap_loss += overlap_loss_val
            n_steps += 1
        sched.step()
        elapsed = time.time() - t0
        avg_eps = epoch_eps_loss / max(1, n_steps)
        avg_cross = epoch_cross_loss / max(1, n_steps)
        avg_overlap = epoch_overlap_loss / max(1, n_steps)
        cross_str = f" cross={avg_cross:.3f}" if use_cross else ""
        overlap_str = f" overlap={avg_overlap:.1f}" if use_overlap else ""
        print(f"epoch {epoch+1:3d}/{args.epochs}  eps={avg_eps:.4f}"
              f"{cross_str}{overlap_str}  "
              f"lr={optim.param_groups[0]['lr']:.5f}  ({elapsed:.0f}s)",
              flush=True)
        if (epoch + 1) % args.save_every == 0:
            torch.save(model.state_dict(), args.ckpt)
    torch.save(model.state_dict(), args.ckpt)
    print(f"\nDone in {time.time()-t0:.0f}s.")


if __name__ == "__main__":
    main()
