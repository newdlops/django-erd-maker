#!/usr/bin/env python3
"""RL agent for ERD layout improvement: REINFORCE on swap actions.

Environment:
  state    = graph G (fixed) + current positions p ∈ ℝ^{N×2}
  action   = pick one of K candidate swaps (i, j) — exchanges p[i] and p[j]
  reward   = -Δsoft_cross_loss (positive when crossings decrease)
  episode  = up to MAX_STEPS swaps starting from initial positions

Policy network:
  GAT encoder → node embeddings → for each candidate pair (i, j),
  attention score = MLP(emb[i] || emb[j] || pos[i] || pos[j])
  softmax over K candidates → action distribution.

REINFORCE training:
  baseline = moving avg episode reward
  loss = -E[Σ (R - b) * log π(a | s)]
"""

import argparse
import json
import math
import random
import time
from pathlib import Path
from typing import Tuple

import torch
import torch.nn as nn
import torch.nn.functional as F
from torch_geometric.nn import GATv2Conv

import importlib.util
spec = importlib.util.spec_from_file_location(
    "train_gnn", Path(__file__).parent / "train_gnn.py"
)
train_gnn = importlib.util.module_from_spec(spec)
spec.loader.exec_module(train_gnn)


# ---------------- Environment ----------------


def candidate_pairs(pos, edge_index_fwd, K=50, sharpness=10.0,
                    device="mps") -> torch.Tensor:
    """Pick top-K node pairs likely to benefit from swap.

    Vectorized: per-edge danger score = soft-cross-sum against a sample of
    other edges. Pick top-2K nodes by accumulated danger; emit K random
    pairs from them.
    """
    E = edge_index_fwd.shape[1]
    N = pos.shape[0]
    if E < 2 or N < 2:
        return torch.zeros(K, 2, dtype=torch.long, device=device)

    # Sample a manageable number of edges for danger calc.
    sample_size = min(E, 80)
    if E > sample_size:
        rng = torch.randperm(E, device=device)[:sample_size]
    else:
        rng = torch.arange(E, device=device)
    a_s = pos[edge_index_fwd[0, rng]]  # [Es, 2]
    b_s = pos[edge_index_fwd[1, rng]]
    Es = a_s.shape[0]
    eps = 1.0

    # Vectorized pairwise soft cross matrix [Es, Es]:
    abx = (b_s[:, 0] - a_s[:, 0]).unsqueeze(1)  # [Es, 1]
    aby = (b_s[:, 1] - a_s[:, 1]).unsqueeze(1)
    ab_len = (abx ** 2 + aby ** 2 + eps).sqrt()  # [Es, 1]
    cdx = (b_s[:, 0] - a_s[:, 0]).unsqueeze(0)  # [1, Es]
    cdy = (b_s[:, 1] - a_s[:, 1]).unsqueeze(0)
    cd_len = (cdx ** 2 + cdy ** 2 + eps).sqrt()
    ac_x = a_s[:, 0].unsqueeze(0) - a_s[:, 0].unsqueeze(1)  # [Es, Es]
    ac_y = a_s[:, 1].unsqueeze(0) - a_s[:, 1].unsqueeze(1)
    ad_x = b_s[:, 0].unsqueeze(0) - a_s[:, 0].unsqueeze(1)
    ad_y = b_s[:, 1].unsqueeze(0) - a_s[:, 1].unsqueeze(1)
    ac_len = (ac_x ** 2 + ac_y ** 2 + eps).sqrt()
    ad_len = (ad_x ** 2 + ad_y ** 2 + eps).sqrt()
    c1 = (abx * ac_y - aby * ac_x) / (ab_len * ac_len)
    c2 = (abx * ad_y - aby * ad_x) / (ab_len * ad_len)
    s = (torch.sigmoid(sharpness * c1) * torch.sigmoid(-sharpness * c2)
         + torch.sigmoid(-sharpness * c1) * torch.sigmoid(sharpness * c2))
    # Zero diagonal (self-pair).
    s.fill_diagonal_(0.0)
    danger = s.sum(dim=1)  # [Es]

    # Accumulate danger per node (scatter-add via index_add_).
    danger_per_node = torch.zeros(N, device=device)
    src = edge_index_fwd[0, rng]
    tgt = edge_index_fwd[1, rng]
    danger_per_node.index_add_(0, src, danger)
    danger_per_node.index_add_(0, tgt, danger)

    top_k = min(2 * K, N)
    top_nodes = torch.topk(danger_per_node, k=top_k).indices  # [top_k]

    # Random pairs from top nodes (vectorized).
    i_idx = torch.randint(0, top_k, (K,), device=device)
    j_idx = torch.randint(0, top_k, (K,), device=device)
    # Ensure i != j: for any i==j, increment j by 1 mod top_k.
    same = i_idx == j_idx
    j_idx = torch.where(same, (j_idx + 1) % top_k, j_idx)
    pairs = torch.stack([top_nodes[i_idx], top_nodes[j_idx]], dim=1)  # [K, 2]
    return pairs


# ---------------- Policy Network ----------------


class SwapPolicy(nn.Module):
    """GNN-based policy that scores candidate swap pairs."""

    def __init__(self, node_feat_dim=5, app_vocab=16, edge_feat_dim=6,
                 hidden=64, num_layers=3, num_heads=4, dropout=0.1):
        super().__init__()
        self.app_embed = nn.Embedding(app_vocab, 8)
        self.pos_proj = nn.Linear(2, 8)
        self.input_proj = nn.Linear(node_feat_dim + 8 + 8, hidden)
        self.edge_proj = nn.Linear(edge_feat_dim, hidden // 2)
        self.layers = nn.ModuleList([
            GATv2Conv(hidden, hidden // num_heads, heads=num_heads,
                      edge_dim=hidden // 2, dropout=dropout)
            for _ in range(num_layers)
        ])
        # Pair scorer: input = emb_i || emb_j || pos_i || pos_j
        self.pair_scorer = nn.Sequential(
            nn.Linear(hidden * 2 + 4, hidden),
            nn.ReLU(),
            nn.Linear(hidden, 1),
        )

    def forward(self, data, pos, candidate_pairs):
        """Returns logits over candidate_pairs [K]."""
        app_emb = self.app_embed(data.app_idx)
        pos_emb = self.pos_proj(pos)
        x = torch.cat([data.x, app_emb, pos_emb], dim=1)
        h = self.input_proj(x)
        e = self.edge_proj(data.edge_attr) if data.edge_attr.numel() > 0 \
            else None
        for layer in self.layers:
            h_new = layer(h, data.edge_index, e)
            h = h + F.elu(h_new)
        # Per-pair score.
        i_idx = candidate_pairs[:, 0]
        j_idx = candidate_pairs[:, 1]
        pair_features = torch.cat([
            h[i_idx], h[j_idx], pos[i_idx], pos[j_idx]
        ], dim=1)
        return self.pair_scorer(pair_features).squeeze(-1)


# ---------------- Soft cross helpers ----------------


def total_soft_cross(pos, edge_index_fwd, sharpness=10.0):
    """Total soft cross across ALL non-shared edge pairs (used for reward)."""
    E = edge_index_fwd.shape[1]
    if E < 2:
        return torch.tensor(0.0, device=pos.device)
    # For tractable speed, sample 200 edges to approximate.
    if E > 200:
        idx = torch.randperm(E)[:200]
        edges = edge_index_fwd[:, idx]
        E = 200
    else:
        edges = edge_index_fwd

    a = pos[edges[0]]
    b = pos[edges[1]]
    i_idx, j_idx = torch.triu_indices(E, E, offset=1)
    a1, b1 = a[i_idx], b[i_idx]
    a2, b2 = a[j_idx], b[j_idx]
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
    return (s1 * s2).sum()


# ---------------- Training loop ----------------


def run_episode(policy, data, init_pos, edge_index_fwd, max_steps=30, K=30,
                stochastic=True, device="mps"):
    """Run one episode. Returns (log_probs, rewards, final_pos, init_cross)."""
    pos = init_pos.clone().detach()
    log_probs = []
    rewards = []
    cur_cross = total_soft_cross(pos, edge_index_fwd).item()
    init_cross = cur_cross
    for step in range(max_steps):
        cands = candidate_pairs(pos, edge_index_fwd, K=K, device=device)
        logits = policy(data, pos, cands)
        if stochastic:
            dist = torch.distributions.Categorical(logits=logits)
            action = dist.sample()
            log_p = dist.log_prob(action)
        else:
            action = logits.argmax()
            log_p = torch.tensor(0.0, device=device)
        i = cands[action, 0].item()
        j = cands[action, 1].item()
        # Apply swap.
        new_pos = pos.clone()
        new_pos[i] = pos[j]
        new_pos[j] = pos[i]
        new_cross = total_soft_cross(new_pos, edge_index_fwd).item()
        reward = cur_cross - new_cross
        pos = new_pos
        cur_cross = new_cross
        log_probs.append(log_p)
        rewards.append(reward)
    return log_probs, rewards, pos, init_cross, cur_cross


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--graphs", type=Path, default=Path("data/erd-poc/graphs"))
    p.add_argument("--layouts", type=Path, default=Path("data/erd-poc/layouts"))
    p.add_argument("--ckpt", type=Path,
                   default=Path("data/erd-poc/checkpoints/rl-policy.pt"))
    p.add_argument("--device", default="mps")
    p.add_argument("--lr", type=float, default=3e-4)
    p.add_argument("--episodes-per-graph", type=int, default=20)
    p.add_argument("--max-steps", type=int, default=20)
    p.add_argument("--num-train-graphs", type=int, default=50)
    p.add_argument("--num-val-graphs", type=int, default=10)
    p.add_argument("--K", type=int, default=30)
    p.add_argument("--max-graph-size", type=int, default=200,
                   help="skip graphs larger than this for PoC speed")
    p.add_argument("--total-iters", type=int, default=300,
                   help="number of training iterations")
    p.add_argument("--seed", type=int, default=42)
    args = p.parse_args()

    torch.manual_seed(args.seed)
    random.seed(args.seed)
    args.ckpt.parent.mkdir(parents=True, exist_ok=True)

    # Load dataset.
    full_ds = train_gnn.ERDDataset(args.graphs, args.layouts)
    valid_indices = []
    for i in range(full_ds.len()):
        d = full_ds.get(i)
        if d is None: continue
        if d.x.shape[0] > args.max_graph_size: continue
        valid_indices.append(i)
    print(f"Valid graphs (≤{args.max_graph_size} nodes): {len(valid_indices)}")
    rng = random.Random(args.seed)
    rng.shuffle(valid_indices)
    train_idx = valid_indices[: args.num_train_graphs]
    val_idx = valid_indices[args.num_train_graphs:
                            args.num_train_graphs + args.num_val_graphs]
    print(f"  train={len(train_idx)} val={len(val_idx)}")

    device = torch.device(args.device)
    policy = SwapPolicy().to(device)
    print(f"Policy params: {sum(p.numel() for p in policy.parameters()):,}")
    optim = torch.optim.Adam(policy.parameters(), lr=args.lr)

    # Forward-direction edge indices (cache).
    fwd_edges_cache = {}
    for i in valid_indices:
        d = full_ds.get(i)
        fwd_edges_cache[i] = d.edge_index[:, ::2].clone()

    baseline = 0.0  # moving avg reward

    for it in range(args.total_iters):
        graph_idx = train_idx[it % len(train_idx)]
        data = full_ds.get(graph_idx).to(device)
        # Initial positions: use expert layout (already loaded as data.y).
        init_pos = data.y.clone()
        fwd_edges = fwd_edges_cache[graph_idx].to(device)

        # Collect rollouts.
        all_log_probs = []
        all_returns = []
        ep_returns = []
        improvements = []
        for ep in range(args.episodes_per_graph):
            log_ps, rewards, _, init_c, end_c = run_episode(
                policy, data, init_pos, fwd_edges,
                max_steps=args.max_steps, K=args.K,
                stochastic=True, device=device,
            )
            R = sum(rewards)
            ep_returns.append(R)
            improvements.append(init_c - end_c)
            # Discounted returns.
            gamma = 0.99
            returns = []
            G = 0.0
            for r in reversed(rewards):
                G = r + gamma * G
                returns.insert(0, G)
            all_log_probs.extend(log_ps)
            all_returns.extend(returns)

        # Baseline update.
        avg_R = sum(ep_returns) / max(len(ep_returns), 1)
        baseline = 0.9 * baseline + 0.1 * avg_R

        # REINFORCE loss.
        loss = torch.tensor(0.0, device=device)
        for lp, R in zip(all_log_probs, all_returns):
            loss = loss - lp * (R - baseline)
        loss = loss / max(len(all_log_probs), 1)
        optim.zero_grad()
        loss.backward()
        torch.nn.utils.clip_grad_norm_(policy.parameters(), 1.0)
        optim.step()

        if it % 10 == 0 or it == args.total_iters - 1:
            avg_imp = sum(improvements) / max(len(improvements), 1)
            print(
                f"  iter {it+1:4d}/{args.total_iters}  "
                f"avg_R={avg_R:+.3f}  baseline={baseline:+.3f}  "
                f"avg_imp={avg_imp:+.3f}  loss={loss.item():.4f}"
            )

    torch.save(policy.state_dict(), args.ckpt)
    print(f"\nSaved policy to {args.ckpt}")

    # Quick val eval (greedy).
    print("\nEvaluating on val graphs (greedy policy)...")
    policy.eval()
    val_imps = []
    with torch.no_grad():
        for vi in val_idx:
            data = full_ds.get(vi).to(device)
            init_pos = data.y.clone()
            fwd_edges = fwd_edges_cache[vi].to(device)
            _, _, _, init_c, end_c = run_episode(
                policy, data, init_pos, fwd_edges,
                max_steps=args.max_steps, K=args.K,
                stochastic=False, device=device,
            )
            imp = init_c - end_c
            val_imps.append(imp)
            tag = "✓" if imp > 0 else "✗"
            print(f"  {full_ds.graph_dirs[vi].name:40s}  "
                  f"init_cross={init_c:.1f}  end={end_c:.1f}  "
                  f"Δ={imp:+.2f}  {tag}")
    pos_count = sum(1 for x in val_imps if x > 0)
    avg_imp = sum(val_imps) / max(len(val_imps), 1)
    print(f"\nVal summary: {pos_count}/{len(val_imps)} improved, "
          f"avg Δsoft_cross={avg_imp:+.2f}")


if __name__ == "__main__":
    main()
