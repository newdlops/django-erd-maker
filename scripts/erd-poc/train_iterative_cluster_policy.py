#!/usr/bin/env python3
"""Iterative cluster-move policy: ML replacement for §13 (cluster-swap).

Unlike single-shot v17, the policy is applied MANY TIMES per episode:
each step takes current cluster centroids as state, predicts a small
move per cluster, applies it, and measures Δcross. The reward is the
crossing reduction per step. Over T steps the layout iteratively
improves — mirroring §13's 1900+ greedy cluster-swap moves.

This is REINFORCE with episodic returns:
    G_t = Σ_{k=t..T} γ^{k-t} · r_k
    loss = -E[(G_t - baseline) · log π(a_t | s_t)]

State (per cluster):
  • current centroid (normalized by initial pos_std)
  • size, hub flag, log degree
  • initial centroid offset (so policy can reason about drift)

Action: per-cluster (Δx, Δy), Gaussian with learned mean.

Init: zero-init delta head → first episode is identity (init cross).
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
CAPTAIN_BASELINE = ROOT / "data/erd-poc/layouts/real-main.json"


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
            "cid": cid,
            "members": mids,
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


class IterativeClusterPolicy(nn.Module):
    """GAT on cluster super-graph; takes dynamic state, outputs per-cluster Δ."""
    def __init__(self, node_feat_dim=8, hidden=64, num_layers=3,
                 num_heads=4, edge_feat_dim=1):
        super().__init__()
        self.input_proj = nn.Linear(node_feat_dim, hidden)
        self.edge_proj = nn.Linear(edge_feat_dim, hidden // 2)
        self.layers = nn.ModuleList([
            GATv2Conv(hidden, hidden // num_heads, heads=num_heads,
                      edge_dim=hidden // 2, dropout=0.0)
            for _ in range(num_layers)
        ])
        self.delta_head = nn.Sequential(
            nn.Linear(hidden, hidden),
            nn.ReLU(),
            nn.Linear(hidden, 2),
        )
        nn.init.zeros_(self.delta_head[-1].weight)
        nn.init.zeros_(self.delta_head[-1].bias)

    def forward(self, state, edge_index, edge_attr):
        h = self.input_proj(state)
        e = self.edge_proj(edge_attr) if edge_attr.numel() > 0 else None
        for layer in self.layers:
            h = h + F.elu(layer(h, edge_index, e))
        return self.delta_head(h)


def reroute_rigid(positions_tsv: Path) -> tuple[int, float]:
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
    em = d["engineMetadata"]
    nodes = d["nodes"]
    xs = [n["position"]["x"] + n["size"]["width"]/2 for n in nodes]
    ys = [n["position"]["y"] + n["size"]["height"]/2 for n in nodes]
    bbox = (max(xs)-min(xs)) * (max(ys)-min(ys)) / 1e9
    return int(em["edgeCrossings"]), bbox


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--input", type=Path, default=CAPTAIN_BASELINE)
    p.add_argument("--ckpt", type=Path, required=True)
    p.add_argument("--updates", type=int, default=50)
    p.add_argument("--episodes-per-update", type=int, default=2)
    p.add_argument("--episode-length", type=int, default=10,
                   help="number of cluster-move steps per episode")
    p.add_argument("--lr", type=float, default=1e-4)
    p.add_argument("--sigma", type=float, default=300.0)
    p.add_argument("--gamma", type=float, default=0.95)
    p.add_argument("--hidden", type=int, default=64)
    p.add_argument("--layers", type=int, default=3)
    p.add_argument("--device", default="cpu")
    p.add_argument("--seed", type=int, default=0)
    args = p.parse_args()

    torch.manual_seed(args.seed)
    args.ckpt.parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="iter-cluster-"))
    print(f"workdir: {work}")

    layout = json.loads(args.input.read_text())
    clusters, cid_to_idx, node_pos, edge_index, edge_attr = \
        build_cluster_supergraph(layout)
    C = len(clusters)
    n_singletons = sum(1 for c in clusters if c["is_singleton"])
    print(f"Cluster super-graph: {C} units "
          f"({C - n_singletons} louvain + {n_singletons} singletons)")

    # Initial centroids (real coords) for state normalization
    init_centroids = torch.tensor([c["centroid"] for c in clusters],
                                   dtype=torch.float32)
    centroid_std = init_centroids.std() + 1e-3
    # Per-cluster static features (size, hub, etc.)
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

    def make_state(current_centroids: torch.Tensor) -> torch.Tensor:
        """4 dynamic (norm cur xy, drift from init xy) + 4 static = 8 dims."""
        cur_norm = (current_centroids - init_centroids.mean(0)) / centroid_std
        drift = (current_centroids - init_centroids) / centroid_std
        return torch.cat([cur_norm, drift, static_feat], dim=1)

    model = IterativeClusterPolicy(
        node_feat_dim=8, hidden=args.hidden, num_layers=args.layers,
    ).to(device)
    optim = torch.optim.AdamW(model.parameters(), lr=args.lr)

    # Build node-level mapping
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

    def write_tsv(node_pos_t: torch.Tensor) -> Path:
        tsv = work / f"p-{time.time_ns()}.tsv"
        cpu = node_pos_t.detach().cpu()
        with tsv.open("w") as f:
            for i, mid in enumerate(all_ids):
                f.write(f"{mid}\t{cpu[i,0].item():.3f}\t{cpu[i,1].item():.3f}\n")
        return tsv

    def eval_cross_at_centroids(centroids: torch.Tensor) -> int:
        """Compute node positions from centroids (broadcast to members),
        write tsv, rigid reroute, return cross."""
        # Position = init_node_pos + (centroids - init_centroids)[member_cluster]
        delta_c = centroids - init_centroids
        node_pos_t = init_node_pos + delta_c[member_cluster]
        tsv = write_tsv(node_pos_t)
        try:
            cross, _ = reroute_rigid(tsv)
        finally:
            tsv.unlink(missing_ok=True)
        return cross

    # Initial cross at zero-delta
    init_cross = eval_cross_at_centroids(init_centroids)
    print(f"init (rigid, zero delta): cross={init_cross}")
    best_cross = init_cross
    torch.save(model.state_dict(), args.ckpt)
    best_path = args.ckpt.with_name(args.ckpt.stem + "-best.pt")
    torch.save(model.state_dict(), best_path)

    # EMA baseline (per-step reward)
    reward_baseline = 0.0
    t0 = time.time()

    for upd in range(args.updates):
        optim.zero_grad()
        upd_losses = []
        upd_episode_finals = []

        for ep in range(args.episodes_per_update):
            cur_centroids = init_centroids.clone()
            cur_cross = init_cross
            episode_log_probs = []
            episode_rewards = []
            for t in range(args.episode_length):
                state = make_state(cur_centroids)
                mu = model(state, edge_index, edge_attr)
                eps = torch.randn_like(mu)
                action = (mu + args.sigma * eps).detach()
                # log prob (Gaussian, fixed σ)
                log_p = -0.5 * (((action - mu) / args.sigma) ** 2).sum()
                # Apply action
                new_centroids = cur_centroids + action
                # Eval
                new_cross = eval_cross_at_centroids(new_centroids)
                reward = float(cur_cross - new_cross)
                # Update best
                if new_cross < best_cross:
                    best_cross = new_cross
                    torch.save(model.state_dict(), best_path)
                # Record
                episode_log_probs.append(log_p)
                episode_rewards.append(reward)
                cur_centroids = new_centroids
                cur_cross = new_cross

            upd_episode_finals.append(cur_cross)
            # Compute discounted returns (advantages)
            returns = []
            R = 0.0
            for r in reversed(episode_rewards):
                R = r + args.gamma * R
                returns.append(R)
            returns = list(reversed(returns))
            returns_t = torch.tensor(returns, dtype=torch.float32,
                                      device=device)
            advantages = returns_t - reward_baseline
            if advantages.std() > 1.0:
                advantages = advantages / (advantages.std() + 1e-6)
            log_probs_t = torch.stack(episode_log_probs)
            ep_loss = -(advantages * log_probs_t).mean()
            upd_losses.append(ep_loss)
            # Update baseline EMA on raw mean return
            reward_baseline = 0.95 * reward_baseline + \
                0.05 * float(returns_t.mean().item())

        # Combined loss
        loss = torch.stack(upd_losses).mean()
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        optim.step()

        elapsed = time.time() - t0
        avg_final = sum(upd_episode_finals) / len(upd_episode_finals)
        improvement = init_cross - avg_final
        print(f"upd {upd+1:3d}/{args.updates}  "
              f"avg_final_cross={avg_final:.0f}  "
              f"Δfrom_init={improvement:+.0f}  "
              f"best={best_cross}  loss={loss.item():+.2f}  "
              f"({elapsed:.0f}s)")
        if (upd + 1) % 5 == 0:
            torch.save(model.state_dict(), args.ckpt)

    torch.save(model.state_dict(), args.ckpt)
    elapsed = time.time() - t0
    print(f"\nDone in {elapsed:.0f}s. Best cross = {best_cross} "
          f"(init {init_cross}; reduction {init_cross-best_cross})")
    print(f"  best ckpt: {best_path}")


if __name__ == "__main__":
    main()
