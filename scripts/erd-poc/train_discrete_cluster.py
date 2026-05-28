#!/usr/bin/env python3
"""Discrete-action cluster-move policy — matches §13's algorithm shape.

Each step the policy picks ONE move: (cluster, direction, stride). This
mirrors §13's greedy cluster-swap which tries one cluster translation
at a time. Discrete categorical action eliminates the high-variance
Gaussian noise that hurt v18/v19.

Action space: 720 clusters × 8 directions × 3 stride magnitudes ≈ 17k.

State = cluster super-graph features (per-cluster centroid + size + drift).
Policy GAT outputs per-cluster logits (8 dirs × 3 strides = 24);
softmax over the full 720*24 categorical → sample one (cluster, dir, stride).

Reward per step: -Δcross from C++ rigid reroute (positive = improvement).
Episode: T steps. REINFORCE with EMA baseline.
"""

import argparse
import json
import math
import subprocess
import tempfile
import time
from collections import defaultdict
from pathlib import Path

import torch
import torch.nn as nn
import torch.nn.functional as F
from torch_geometric.nn import GATv2Conv

ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / "bin/ogdf/darwin-arm64/django-erd-ogdf-layout"
NODES_TSV = ROOT / "data/erd-poc/graphs/real-main/nodes.tsv"
EDGES_TSV = ROOT / "data/erd-poc/graphs/real-main/edges.tsv"

NUM_DIRS = 8
STRIDES = [200.0, 600.0, 1800.0]  # small / medium / large


def build_cluster_supergraph(layout: dict):
    nodes = layout["nodes"]
    routed = layout["routedEdges"]
    cluster_by = {}
    for n in nodes:
        cid = n.get("clusterId") or f"_singleton_{n['modelId']}"
        cluster_by[n["modelId"]] = cid
    members = defaultdict(list)
    for n in nodes:
        members[cluster_by[n["modelId"]]].append(n["modelId"])
    node_pos = {}
    for n in nodes:
        node_pos[n["modelId"]] = (
            n["position"]["x"] + n["size"]["width"] / 2.0,
            n["position"]["y"] + n["size"]["height"] / 2.0,
        )
    clusters = []
    for cid, mids in members.items():
        xs = [node_pos[m][0] for m in mids]
        ys = [node_pos[m][1] for m in mids]
        clusters.append({
            "cid": cid, "members": mids,
            "centroid": (sum(xs)/len(xs), sum(ys)/len(ys)),
            "size": len(mids),
            "is_singleton": cid.startswith("_singleton_"),
        })
    clusters.sort(key=lambda c: c["cid"])
    cid_to_idx = {c["cid"]: i for i, c in enumerate(clusters)}
    inter = defaultdict(int)
    for re in routed:
        s = cluster_by.get(re.get("sourceModelId"))
        t = cluster_by.get(re.get("targetModelId"))
        if s is None or t is None or s == t:
            continue
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
    """GAT → per-cluster (NUM_DIRS × NUM_STRIDES) logits."""
    def __init__(self, node_feat_dim=8, hidden=64, num_layers=3,
                 num_heads=4, num_actions_per_cluster=NUM_DIRS * len(STRIDES)):
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
            nn.Linear(hidden, num_actions_per_cluster),
        )

    def forward(self, state, edge_index, edge_attr):
        h = self.input_proj(state)
        e = self.edge_proj(edge_attr) if edge_attr.numel() > 0 else None
        for layer in self.layers:
            h = h + F.elu(layer(h, edge_index, e))
        # [C, NUM_ACTIONS_PER_CLUSTER]
        return self.action_head(h)


def reroute_rigid(positions_tsv: Path) -> int:
    r = subprocess.run(
        [str(BINARY), "layout",
         "--mode", "hierarchical_barycenter",
         "--nodes-file", str(NODES_TSV),
         "--edges-file", str(EDGES_TSV),
         "--edge-routing", "straight",
         "--cluster-graph", "1",
         "--positions-tsv", str(positions_tsv),
         "--rigid-positions", "1"],
        capture_output=True, text=True,
    )
    if r.returncode != 0:
        raise RuntimeError(f"binary failed: {r.stderr[:300]}")
    d = json.loads(r.stdout)
    return int(d["engineMetadata"]["edgeCrossings"])


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--input", type=Path, required=True)
    p.add_argument("--ckpt", type=Path, required=True)
    p.add_argument("--init-from", type=Path, default=None)
    p.add_argument("--updates", type=int, default=300)
    p.add_argument("--episodes-per-update", type=int, default=4)
    p.add_argument("--episode-length", type=int, default=30,
                   help="number of greedy single-move steps per episode")
    p.add_argument("--lr", type=float, default=3e-4)
    p.add_argument("--gamma", type=float, default=0.99)
    p.add_argument("--entropy-coef", type=float, default=0.01)
    p.add_argument("--hidden", type=int, default=64)
    p.add_argument("--layers", type=int, default=3)
    p.add_argument("--device", default="cpu")
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--save-every", type=int, default=5)
    args = p.parse_args()

    torch.manual_seed(args.seed)
    args.ckpt.parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="discrete-cluster-"))
    print(f"workdir: {work}")

    layout = json.loads(args.input.read_text())
    clusters, cid_to_idx, node_pos, edge_index, edge_attr = \
        build_cluster_supergraph(layout)
    C = len(clusters)
    A = NUM_DIRS * len(STRIDES)  # 24 actions per cluster
    print(f"super-graph: {C} units, {A} actions per cluster, "
          f"{C*A} total action space")
    print(f"strides: {STRIDES}, NUM_DIRS={NUM_DIRS}")

    init_centroids = torch.tensor([c["centroid"] for c in clusters],
                                   dtype=torch.float32)
    centroid_std = init_centroids.std() + 1e-3
    static_feat = []
    for i, c in enumerate(clusters):
        in_deg = int(edge_index[1].eq(i).sum().item())
        static_feat.append([
            math.log(c["size"] + 1),
            1.0 if c["size"] >= 5 else 0.0,
            1.0 if c["is_singleton"] else 0.0,
            math.log(in_deg + 1),
        ])
    static_feat = torch.tensor(static_feat, dtype=torch.float32)

    device = torch.device(args.device)
    edge_index = edge_index.to(device)
    edge_attr = edge_attr.to(device)
    init_centroids = init_centroids.to(device)
    static_feat = static_feat.to(device)

    def make_state(cur):
        cur_norm = (cur - init_centroids.mean(0)) / centroid_std
        drift = (cur - init_centroids) / centroid_std
        return torch.cat([cur_norm, drift, static_feat], dim=1)

    model = DiscreteClusterPolicy(node_feat_dim=8, hidden=args.hidden,
                                   num_layers=args.layers,
                                   num_actions_per_cluster=A).to(device)
    if args.init_from and args.init_from.exists():
        sd = torch.load(args.init_from, map_location=device, weights_only=True)
        own_sd = model.state_dict()
        loaded = 0
        for k, v in sd.items():
            if k in own_sd and own_sd[k].shape == v.shape:
                own_sd[k] = v
                loaded += 1
        model.load_state_dict(own_sd)
        print(f"  loaded {loaded}/{len(sd)} params from {args.init_from}")
    optim = torch.optim.AdamW(model.parameters(), lr=args.lr)

    all_ids = list(node_pos.keys())
    id_to_idx = {mid: i for i, mid in enumerate(all_ids)}
    n_nodes = len(all_ids)
    init_node_pos = torch.zeros(n_nodes, 2, device=device)
    for i, mid in enumerate(all_ids):
        init_node_pos[i] = torch.tensor(node_pos[mid], device=device)
    member_cluster = torch.zeros(n_nodes, dtype=torch.long, device=device)
    for cidx, c in enumerate(clusters):
        for mid in c["members"]:
            member_cluster[id_to_idx[mid]] = cidx

    # Action → (dx, dy) lookup table
    action_dx = torch.zeros(A, device=device)
    action_dy = torch.zeros(A, device=device)
    for d in range(NUM_DIRS):
        angle = 2 * math.pi * d / NUM_DIRS
        for s, stride in enumerate(STRIDES):
            idx = d * len(STRIDES) + s
            action_dx[idx] = stride * math.cos(angle)
            action_dy[idx] = stride * math.sin(angle)

    def write_tsv(node_pos_t):
        tsv = work / f"p-{time.time_ns()}.tsv"
        cpu = node_pos_t.detach().cpu()
        with tsv.open("w") as f:
            for i, mid in enumerate(all_ids):
                f.write(f"{mid}\t{cpu[i,0].item():.3f}\t{cpu[i,1].item():.3f}\n")
        return tsv

    def eval_cross(centroids):
        delta_c = centroids - init_centroids
        pos_t = init_node_pos + delta_c[member_cluster]
        tsv = write_tsv(pos_t)
        try:
            return reroute_rigid(tsv)
        finally:
            tsv.unlink(missing_ok=True)

    init_cross = eval_cross(init_centroids)
    print(f"init (rigid, zero delta): cross={init_cross}")
    best_cross = init_cross
    torch.save(model.state_dict(), args.ckpt)
    best_path = args.ckpt.with_name(args.ckpt.stem + "-best.pt")
    torch.save(model.state_dict(), best_path)

    reward_baseline = 0.0
    t0 = time.time()
    for upd in range(args.updates):
        optim.zero_grad()
        upd_loss = 0.0
        upd_entropy = 0.0
        upd_finals = []
        for ep in range(args.episodes_per_update):
            cur = init_centroids.clone()
            cur_cross = init_cross
            log_probs = []
            entropies = []
            rewards = []
            for t in range(args.episode_length):
                state = make_state(cur)
                logits = model(state, edge_index, edge_attr)  # [C, A]
                # Flatten to single categorical over C*A
                flat = logits.reshape(-1)
                dist = torch.distributions.Categorical(logits=flat)
                action = dist.sample()
                lp = dist.log_prob(action)
                ent = dist.entropy()
                ci = (action // A).item()
                ai = (action % A).item()
                dx = action_dx[ai]
                dy = action_dy[ai]
                # Apply move to cluster ci
                new_c = cur.clone()
                new_c[ci, 0] = new_c[ci, 0] + dx
                new_c[ci, 1] = new_c[ci, 1] + dy
                new_cross = eval_cross(new_c)
                reward = float(cur_cross - new_cross)
                if new_cross < best_cross:
                    best_cross = new_cross
                    torch.save(model.state_dict(), best_path)
                log_probs.append(lp)
                entropies.append(ent)
                rewards.append(reward)
                cur = new_c
                cur_cross = new_cross
            upd_finals.append(cur_cross)
            # Discounted returns
            returns = []
            R = 0.0
            for r in reversed(rewards):
                R = r + args.gamma * R
                returns.append(R)
            returns = list(reversed(returns))
            returns_t = torch.tensor(returns, dtype=torch.float32,
                                      device=device)
            advantages = returns_t - reward_baseline
            if advantages.std() > 1.0:
                advantages = advantages / (advantages.std() + 1e-6)
            log_probs_t = torch.stack(log_probs)
            entropies_t = torch.stack(entropies)
            pol_loss = -(advantages * log_probs_t).sum()
            ent_loss = -entropies_t.mean()
            ep_loss = pol_loss + args.entropy_coef * ent_loss
            upd_loss = upd_loss + ep_loss
            upd_entropy += float(entropies_t.mean().item())
            reward_baseline = 0.95 * reward_baseline + \
                0.05 * float(returns_t.mean().item())
        upd_loss = upd_loss / args.episodes_per_update
        upd_loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        optim.step()

        elapsed = time.time() - t0
        avg_final = sum(upd_finals) / len(upd_finals)
        avg_ent = upd_entropy / args.episodes_per_update
        print(f"upd {upd+1:3d}/{args.updates}  "
              f"final={avg_final:.0f}  "
              f"Δfrom_init={init_cross-avg_final:+.0f}  "
              f"best={best_cross}  "
              f"loss={upd_loss.item():+.2f}  ent={avg_ent:.2f}  "
              f"({elapsed:.0f}s)")
        if (upd + 1) % args.save_every == 0:
            torch.save(model.state_dict(), args.ckpt)

    torch.save(model.state_dict(), args.ckpt)
    elapsed = time.time() - t0
    print(f"\nDone in {elapsed:.0f}s. Best = {best_cross} "
          f"(init {init_cross}; reduction {init_cross-best_cross})")
    print(f"  best ckpt: {best_path}")


if __name__ == "__main__":
    main()
