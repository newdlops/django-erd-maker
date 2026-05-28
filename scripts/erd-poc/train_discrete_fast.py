#!/usr/bin/env python3
"""Discrete-action cluster policy with FAST in-process cross evaluator.

Same algorithm as train_discrete_cluster.py but eval = FastCrossEval
(numpy, ~66ms) instead of C++ subprocess (~3s). 45x speedup → can run
much longer episodes (100s of moves) and more updates.

Verification: after training, evaluate the best ckpt via C++ binary
for ground-truth cross count.
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
    "fast_cross_eval", Path(__file__).parent / "fast_cross_eval.py"
)
fce = importlib.util.module_from_spec(spec); spec.loader.exec_module(fce)

ROOT = Path(__file__).resolve().parents[2]

NUM_DIRS = 8
STRIDES = [200.0, 600.0, 1800.0]


def build_cluster_supergraph(layout):
    nodes = layout["nodes"]
    routed = layout["routedEdges"]
    cluster_by = {}
    for n in nodes:
        cid = n.get("clusterId") or f"_singleton_{n['modelId']}"
        cluster_by[n["modelId"]] = cid
    members = defaultdict(list)
    for n in nodes:
        members[cluster_by[n["modelId"]]].append(n["modelId"])
    node_pos = {n["modelId"]: (
        n["position"]["x"] + n["size"]["width"] / 2.0,
        n["position"]["y"] + n["size"]["height"] / 2.0)
        for n in nodes}
    clusters = []
    for cid, mids in members.items():
        xs = [node_pos[m][0] for m in mids]
        ys = [node_pos[m][1] for m in mids]
        clusters.append({"cid": cid, "members": mids,
            "centroid": (sum(xs)/len(xs), sum(ys)/len(ys)),
            "size": len(mids),
            "is_singleton": cid.startswith("_singleton_")})
    clusters.sort(key=lambda c: c["cid"])
    cid_to_idx = {c["cid"]: i for i, c in enumerate(clusters)}
    inter = defaultdict(int)
    for re in routed:
        s = cluster_by.get(re.get("sourceModelId"))
        t = cluster_by.get(re.get("targetModelId"))
        if s is None or t is None or s == t: continue
        a, b = sorted([s, t])
        inter[(a, b)] += 1
    edges_a, edges_b, edge_w = [], [], []
    for (a, b), cnt in inter.items():
        ia, ib = cid_to_idx[a], cid_to_idx[b]
        edges_a.append(ia); edges_b.append(ib)
        edges_a.append(ib); edges_b.append(ia)
        edge_w.append(cnt); edge_w.append(cnt)
    edge_index = (torch.tensor([edges_a, edges_b], dtype=torch.long)
                  if edges_a else torch.zeros(2, 0, dtype=torch.long))
    edge_attr = (torch.log(torch.tensor(edge_w, dtype=torch.float32)
                            .unsqueeze(1) + 1)
                 if edges_a else torch.zeros(0, 1))
    return clusters, cid_to_idx, node_pos, edge_index, edge_attr


class DiscreteClusterPolicy(nn.Module):
    def __init__(self, node_feat_dim=8, hidden=64, num_layers=3,
                 num_heads=4, num_actions=NUM_DIRS * len(STRIDES)):
        super().__init__()
        self.input_proj = nn.Linear(node_feat_dim, hidden)
        self.edge_proj = nn.Linear(1, hidden // 2)
        self.layers = nn.ModuleList([
            GATv2Conv(hidden, hidden // num_heads, heads=num_heads,
                      edge_dim=hidden // 2, dropout=0.0)
            for _ in range(num_layers)
        ])
        self.action_head = nn.Sequential(
            nn.Linear(hidden, hidden), nn.ReLU(),
            nn.Linear(hidden, num_actions),
        )

    def forward(self, state, edge_index, edge_attr):
        h = self.input_proj(state)
        e = self.edge_proj(edge_attr) if edge_attr.numel() > 0 else None
        for layer in self.layers:
            h = h + F.elu(layer(h, edge_index, e))
        return self.action_head(h)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--input", type=Path, required=True)
    p.add_argument("--ckpt", type=Path, required=True)
    p.add_argument("--init-from", type=Path, default=None)
    p.add_argument("--updates", type=int, default=200)
    p.add_argument("--episodes-per-update", type=int, default=4)
    p.add_argument("--episode-length", type=int, default=200,
                   help="single-move steps per episode")
    p.add_argument("--lr", type=float, default=3e-4)
    p.add_argument("--gamma", type=float, default=0.99)
    p.add_argument("--entropy-coef", type=float, default=0.01)
    p.add_argument("--entropy-decay", type=float, default=0.995,
                   help="entropy coef × this every update (exploit over time)")
    p.add_argument("--hidden", type=int, default=64)
    p.add_argument("--layers", type=int, default=3)
    p.add_argument("--device", default="cpu")
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--save-every", type=int, default=3)
    args = p.parse_args()

    torch.manual_seed(args.seed)
    np.random.seed(args.seed)
    args.ckpt.parent.mkdir(parents=True, exist_ok=True)

    layout = json.loads(args.input.read_text())
    clusters, cid_to_idx, node_pos, edge_index, edge_attr = \
        build_cluster_supergraph(layout)
    C = len(clusters)
    A = NUM_DIRS * len(STRIDES)
    print(f"super-graph: {C} units, {A} actions/cluster, {C*A} total action space")
    print(f"strides: {STRIDES}, NUM_DIRS={NUM_DIRS}")

    # Initial centroids + features
    init_centroids = np.array([c["centroid"] for c in clusters], dtype=np.float64)
    centroid_std = np.std(init_centroids) + 1e-3
    static_feat = np.zeros((C, 4), dtype=np.float32)
    for i, c in enumerate(clusters):
        in_deg = int((edge_index[1] == i).sum().item())
        static_feat[i] = [
            math.log(c["size"] + 1),
            1.0 if c["size"] >= 5 else 0.0,
            1.0 if c["is_singleton"] else 0.0,
            math.log(in_deg + 1),
        ]

    # Node → cluster mapping + base positions
    all_ids = list(node_pos.keys())
    id_to_idx = {mid: i for i, mid in enumerate(all_ids)}
    n_nodes = len(all_ids)
    init_node_pos = np.zeros((n_nodes, 2), dtype=np.float64)
    member_cluster_np = np.zeros(n_nodes, dtype=np.int32)
    for i, mid in enumerate(all_ids):
        init_node_pos[i] = node_pos[mid]
    for cidx, c in enumerate(clusters):
        for mid in c["members"]:
            member_cluster_np[id_to_idx[mid]] = cidx

    # Build edge array for FastCrossEval (use forward edges from routedEdges)
    edges_for_eval = []
    for re in layout["routedEdges"]:
        s = id_to_idx.get(re.get("sourceModelId"))
        t = id_to_idx.get(re.get("targetModelId"))
        if s is None or t is None or s == t: continue
        edges_for_eval.append((s, t))
    edges_arr = np.array(edges_for_eval, dtype=np.int32)
    evaluator = fce.FastCrossEval(edges_arr, n_nodes)
    print(f"Fast eval: {evaluator.K} edge pairs to check")

    device = torch.device(args.device)
    edge_index_t = edge_index.to(device)
    edge_attr_t = edge_attr.to(device)
    init_centroids_t = torch.tensor(init_centroids, dtype=torch.float32,
                                     device=device)
    centroid_std_t = float(centroid_std)
    centroid_mean_t = init_centroids_t.mean(0)
    static_feat_t = torch.tensor(static_feat, device=device)

    def make_state(cur_t):
        cur_norm = (cur_t - centroid_mean_t) / centroid_std_t
        drift = (cur_t - init_centroids_t) / centroid_std_t
        return torch.cat([cur_norm, drift, static_feat_t], dim=1)

    model = DiscreteClusterPolicy(node_feat_dim=8, hidden=args.hidden,
                                   num_layers=args.layers, num_actions=A).to(device)
    if args.init_from and args.init_from.exists():
        sd = torch.load(args.init_from, map_location=device, weights_only=True)
        own = model.state_dict()
        loaded = 0
        for k, v in sd.items():
            if k in own and own[k].shape == v.shape:
                own[k] = v; loaded += 1
        model.load_state_dict(own)
        print(f"  loaded {loaded}/{len(sd)} from {args.init_from}")
    optim = torch.optim.AdamW(model.parameters(), lr=args.lr)

    # Action LUT
    action_dx = np.zeros(A); action_dy = np.zeros(A)
    for d in range(NUM_DIRS):
        angle = 2 * math.pi * d / NUM_DIRS
        for s, stride in enumerate(STRIDES):
            idx = d * len(STRIDES) + s
            action_dx[idx] = stride * math.cos(angle)
            action_dy[idx] = stride * math.sin(angle)

    def eval_cross(centroids_np):
        delta = centroids_np - init_centroids
        pos_t = init_node_pos + delta[member_cluster_np]
        return evaluator.count_crossings(pos_t)

    init_cross = eval_cross(init_centroids)
    print(f"init cross (fast): {init_cross}")
    best_cross = init_cross
    torch.save(model.state_dict(), args.ckpt)
    best_path = args.ckpt.with_name(args.ckpt.stem + "-best.pt")
    torch.save(model.state_dict(), best_path)

    reward_baseline = 0.0
    cur_entropy_coef = args.entropy_coef
    t0 = time.time()

    for upd in range(args.updates):
        optim.zero_grad()
        upd_finals = []
        upd_loss = 0.0
        upd_entropy = 0.0
        for ep in range(args.episodes_per_update):
            cur_centroids = init_centroids.copy()
            cur_cross = init_cross
            log_probs = []
            entropies = []
            rewards = []
            for t in range(args.episode_length):
                cur_t = torch.tensor(cur_centroids, dtype=torch.float32,
                                      device=device)
                state = make_state(cur_t)
                logits = model(state, edge_index_t, edge_attr_t)
                flat = logits.reshape(-1)
                dist = torch.distributions.Categorical(logits=flat)
                action = dist.sample()
                lp = dist.log_prob(action)
                ent = dist.entropy()
                ai_global = action.item()
                ci = ai_global // A
                ai = ai_global % A
                # Apply
                cur_centroids[ci, 0] += action_dx[ai]
                cur_centroids[ci, 1] += action_dy[ai]
                new_cross = eval_cross(cur_centroids)
                reward = float(cur_cross - new_cross)
                if new_cross < best_cross:
                    best_cross = new_cross
                    torch.save(model.state_dict(), best_path)
                log_probs.append(lp)
                entropies.append(ent)
                rewards.append(reward)
                cur_cross = new_cross
            upd_finals.append(cur_cross)
            # Returns
            returns = []
            R = 0.0
            for r in reversed(rewards):
                R = r + args.gamma * R
                returns.append(R)
            returns = list(reversed(returns))
            returns_t = torch.tensor(returns, dtype=torch.float32, device=device)
            adv = returns_t - reward_baseline
            if adv.std() > 1.0:
                adv = adv / (adv.std() + 1e-6)
            log_probs_t = torch.stack(log_probs)
            entropies_t = torch.stack(entropies)
            pol_loss = -(adv * log_probs_t).sum()
            ent_loss = -entropies_t.mean()
            ep_loss = pol_loss + cur_entropy_coef * ent_loss
            upd_loss = upd_loss + ep_loss
            upd_entropy += float(entropies_t.mean().item())
            reward_baseline = 0.95 * reward_baseline + \
                0.05 * float(returns_t.mean().item())
        upd_loss = upd_loss / args.episodes_per_update
        upd_loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        optim.step()

        cur_entropy_coef *= args.entropy_decay

        elapsed = time.time() - t0
        avg_final = sum(upd_finals) / len(upd_finals)
        avg_ent = upd_entropy / args.episodes_per_update
        print(f"upd {upd+1:3d}/{args.updates}  "
              f"final={avg_final:.0f}  "
              f"Δfrom_init={init_cross-avg_final:+.0f}  "
              f"best={best_cross}  "
              f"loss={upd_loss.item():+.2f}  ent={avg_ent:.2f}  "
              f"ec={cur_entropy_coef:.4f}  "
              f"({elapsed:.0f}s)", flush=True)
        if (upd + 1) % args.save_every == 0:
            torch.save(model.state_dict(), args.ckpt)

    torch.save(model.state_dict(), args.ckpt)
    elapsed = time.time() - t0
    print(f"\nDone in {elapsed:.0f}s. Best = {best_cross} "
          f"(init {init_cross}; reduction {init_cross-best_cross})")
    print(f"  best ckpt: {best_path}")


if __name__ == "__main__":
    main()
