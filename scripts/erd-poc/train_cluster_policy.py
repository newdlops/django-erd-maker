#!/usr/bin/env python3
"""§13 replacement: ML cluster-move policy trained via REINFORCE on rigid cross.

The C++ §13-cluster-swap is a greedy heuristic that gets stuck in local
minima. This model learns a global cluster-translation policy that
operates on the post-§12 layout (or v12-fnv ML output).

Architecture:
  • Super-graph: 1 node per Louvain cluster (the model's movable units).
  • Per-cluster features: centroid (normalized), size, hub flag,
    # external edges, # internal edges.
  • GAT message passing over inter-cluster edges (weighted by edge count).
  • Output: per-cluster (Δx, Δy) — translation applied to all members.

Inference is single-shot: forward once, apply, route.
Training is REINFORCE: sample Gaussian noise around predicted Δ, evaluate
real cross via C++ rigid reroute, gradient ascent on advantage·log_prob.
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
from torch_geometric.data import Data
from torch_geometric.nn import GATv2Conv

ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / "bin/ogdf/darwin-arm64/django-erd-ogdf-layout"
NODES_TSV = ROOT / "data/erd-poc/graphs/real-main/nodes.tsv"
EDGES_TSV = ROOT / "data/erd-poc/graphs/real-main/edges.tsv"
CAPTAIN_BASELINE = ROOT / "data/erd-poc/layouts/real-main.json"


def build_cluster_supergraph(layout: dict):
    """From a layout JSON, extract:
       - per-cluster centroid + size + member node-ids
       - inter-cluster edge counts
       - per-node initial position (centers)
    Returns (clusters: list, node_pos: dict, edges_t: tensor).
    """
    nodes = layout["nodes"]
    routed = layout["routedEdges"]
    cluster_by = {}
    for n in nodes:
        cid = n.get("clusterId") or ""
        # Skip empty cluster ID: each such node becomes its own singleton
        if not cid:
            cid = f"_singleton_{n['modelId']}"
        cluster_by[n["modelId"]] = cid
    # Cluster members
    members = defaultdict(list)
    for n in nodes:
        members[cluster_by[n["modelId"]]].append(n["modelId"])
    # Per-node initial position
    node_pos = {}
    for n in nodes:
        node_pos[n["modelId"]] = (
            n["position"]["x"] + n["size"]["width"] / 2.0,
            n["position"]["y"] + n["size"]["height"] / 2.0,
        )
    # Cluster centroids + features
    clusters = []
    for cid, mids in members.items():
        xs = [node_pos[m][0] for m in mids]
        ys = [node_pos[m][1] for m in mids]
        cx = sum(xs) / len(xs)
        cy = sum(ys) / len(ys)
        is_singleton = cid.startswith("_singleton_")
        clusters.append({
            "cid": cid,
            "members": mids,
            "centroid": (cx, cy),
            "size": len(mids),
            "is_singleton": is_singleton,
        })
    # Stable order
    clusters.sort(key=lambda c: c["cid"])
    cid_to_idx = {c["cid"]: i for i, c in enumerate(clusters)}
    # Inter-cluster edges (undirected, weighted by count)
    inter = defaultdict(int)
    for re in routed:
        s = cluster_by.get(re.get("sourceModelId"))
        t = cluster_by.get(re.get("targetModelId"))
        if s is None or t is None or s == t:
            continue
        a, b = sorted([s, t])
        inter[(a, b)] += 1
    # PyG-style edge_index (bidirectional)
    edges_a, edges_b, edge_weights = [], [], []
    for (a, b), cnt in inter.items():
        ia, ib = cid_to_idx[a], cid_to_idx[b]
        edges_a.append(ia); edges_b.append(ib)
        edges_a.append(ib); edges_b.append(ia)
        edge_weights.append(cnt)
        edge_weights.append(cnt)
    edge_index = torch.tensor([edges_a, edges_b], dtype=torch.long) \
        if edges_a else torch.zeros(2, 0, dtype=torch.long)
    edge_attr = torch.tensor(edge_weights,
                              dtype=torch.float32).unsqueeze(1) \
        if edges_a else torch.zeros(0, 1)
    # Log-normalize weights
    if edge_attr.numel() > 0:
        edge_attr = torch.log(edge_attr + 1)
    return clusters, cid_to_idx, node_pos, edge_index, edge_attr


class ClusterPolicyGAT(nn.Module):
    """GAT on the cluster super-graph, outputs (Δx, Δy) per cluster."""
    def __init__(self, node_feat_dim=6, hidden=64, num_layers=3, num_heads=4,
                 edge_feat_dim=1, dropout=0.0):
        super().__init__()
        self.input_proj = nn.Linear(node_feat_dim, hidden)
        self.edge_proj = nn.Linear(edge_feat_dim, hidden // 2)
        self.layers = nn.ModuleList([
            GATv2Conv(hidden, hidden // num_heads, heads=num_heads,
                      edge_dim=hidden // 2, dropout=dropout)
            for _ in range(num_layers)
        ])
        self.delta_head = nn.Sequential(
            nn.Linear(hidden, hidden),
            nn.ReLU(),
            nn.Linear(hidden, 2),
        )
        nn.init.zeros_(self.delta_head[-1].weight)
        nn.init.zeros_(self.delta_head[-1].bias)

    def forward(self, x, edge_index, edge_attr):
        h = self.input_proj(x)
        e = self.edge_proj(edge_attr) if edge_attr.numel() > 0 else None
        for layer in self.layers:
            h_new = layer(h, edge_index, e)
            h = h + F.elu(h_new)
        return self.delta_head(h)  # [C, 2]


def reroute_rigid(positions_tsv: Path) -> tuple[int, float]:
    """C++ rigid reroute → (cross, bbox_B)."""
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
    p.add_argument("--input", type=Path, default=CAPTAIN_BASELINE,
                   help="initial layout JSON (typically v12-fnv ML output)")
    p.add_argument("--ckpt", type=Path, required=True)
    p.add_argument("--steps", type=int, default=50)
    p.add_argument("--samples-per-step", type=int, default=4)
    p.add_argument("--lr", type=float, default=1e-4)
    p.add_argument("--sigma", type=float, default=2000.0,
                   help="Gaussian noise std on cluster Δ (real units)")
    p.add_argument("--hidden", type=int, default=64)
    p.add_argument("--layers", type=int, default=3)
    p.add_argument("--device", default="cpu")
    p.add_argument("--seed", type=int, default=0)
    args = p.parse_args()

    torch.manual_seed(args.seed)
    args.ckpt.parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="cluster-policy-"))
    print(f"workdir: {work}")

    layout = json.loads(args.input.read_text())
    clusters, cid_to_idx, node_pos, edge_index, edge_attr = \
        build_cluster_supergraph(layout)
    C = len(clusters)
    n_singletons = sum(1 for c in clusters if c["is_singleton"])
    print(f"Cluster super-graph: {C} units "
          f"({C - n_singletons} louvain + {n_singletons} singletons), "
          f"{edge_index.shape[1]//2} inter-cluster edges")

    # Per-cluster features
    centroids = torch.tensor([c["centroid"] for c in clusters],
                              dtype=torch.float32)
    sizes = torch.tensor([c["size"] for c in clusters],
                         dtype=torch.float32)
    cluster_features = []
    # Normalize centroid + log size + flags
    cmean = centroids.mean(dim=0)
    cstd = centroids.std() + 1e-3
    centroids_norm = (centroids - cmean) / cstd
    for i, c in enumerate(clusters):
        cluster_features.append([
            centroids_norm[i, 0].item(),
            centroids_norm[i, 1].item(),
            math.log(c["size"] + 1),
            1.0 if c["size"] >= 5 else 0.0,
            1.0 if c["is_singleton"] else 0.0,
            math.log(1 + edge_index[1].eq(i).sum().item()),  # log in-degree
        ])
    cluster_x = torch.tensor(cluster_features, dtype=torch.float32)
    print(f"cluster feature shape: {cluster_x.shape}")
    print(f"cluster centroids: mean=({cmean[0]:.0f},{cmean[1]:.0f}) "
          f"std={cstd:.0f}")

    device = torch.device(args.device)
    cluster_x = cluster_x.to(device)
    edge_index = edge_index.to(device)
    edge_attr = edge_attr.to(device)

    model = ClusterPolicyGAT(
        node_feat_dim=6, hidden=args.hidden, num_layers=args.layers,
        edge_feat_dim=1, dropout=0.0,
    ).to(device)
    optim = torch.optim.AdamW(model.parameters(), lr=args.lr)

    # Helper: positions tensor for all nodes given cluster Δ
    # Pre-compute member → cluster index mapping + initial pos array
    all_ids = list(node_pos.keys())
    id_to_idx = {mid: i for i, mid in enumerate(all_ids)}
    n_nodes = len(all_ids)
    init_pos = torch.zeros(n_nodes, 2)
    for i, mid in enumerate(all_ids):
        init_pos[i] = torch.tensor(node_pos[mid])
    # Each node belongs to one cluster; map node_idx → cluster_idx
    member_cluster_idx = torch.zeros(n_nodes, dtype=torch.long)
    for cidx, c in enumerate(clusters):
        for mid in c["members"]:
            member_cluster_idx[id_to_idx[mid]] = cidx

    def apply_cluster_delta(delta_c: torch.Tensor) -> torch.Tensor:
        """delta_c [C, 2] → node positions [N, 2] = init + delta_c[member_cluster]."""
        return init_pos + delta_c[member_cluster_idx]

    def write_tsv(pos_t: torch.Tensor) -> Path:
        tsv = work / f"p-{time.time_ns()}.tsv"
        with tsv.open("w") as f:
            for i, mid in enumerate(all_ids):
                x = pos_t[i, 0].item()
                y = pos_t[i, 1].item()
                f.write(f"{mid}\t{x:.3f}\t{y:.3f}\n")
        return tsv

    # Initial eval (model output = zero delta initially due to zero init)
    with torch.no_grad():
        mu = model(cluster_x, edge_index, edge_attr)
    pos = apply_cluster_delta(mu.cpu())
    tsv = write_tsv(pos)
    init_cross, init_bbox = reroute_rigid(tsv)
    tsv.unlink()
    print(f"init (rigid, zero delta): cross={init_cross} bbox={init_bbox:.2f}B")
    best_cross = init_cross
    baseline_reward = -float(init_cross)
    torch.save(model.state_dict(), args.ckpt)
    best_path = args.ckpt.with_name(args.ckpt.stem + "-best.pt")
    torch.save(model.state_dict(), best_path)

    t0 = time.time()
    for step in range(args.steps):
        optim.zero_grad()
        mu = model(cluster_x, edge_index, edge_attr)  # [C, 2]
        # Sample K perturbed cluster Δ
        eps = torch.randn(args.samples_per_step, C, 2, device=device)
        samples = (mu.unsqueeze(0) + args.sigma * eps).detach()
        # log π(a|s) = -0.5 ((a-μ)/σ)²
        log_probs = -0.5 * (((samples - mu.unsqueeze(0)) / args.sigma) ** 2
                            ).sum(dim=(1, 2))  # [K]

        rewards = []
        for k in range(args.samples_per_step):
            pos = apply_cluster_delta(samples[k].cpu())
            tsv = write_tsv(pos)
            try:
                c, b = reroute_rigid(tsv)
            except Exception as e:
                print(f"  eval failed sample {k}: {e}")
                c = 999999
            tsv.unlink(missing_ok=True)
            rewards.append(-c)
            if c < best_cross:
                best_cross = c
                torch.save(model.state_dict(), best_path)

        rewards_t = torch.tensor(rewards, dtype=torch.float32, device=device)
        advantage = rewards_t - baseline_reward
        # Normalize advantage for variance reduction
        if advantage.std() > 1.0:
            advantage = advantage / (advantage.std() + 1e-6)
        loss = -(advantage * log_probs).mean()
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        optim.step()
        baseline_reward = 0.9 * baseline_reward + 0.1 * rewards_t.mean().item()

        elapsed = time.time() - t0
        mean_cross = -rewards_t.mean().item()
        min_sample = -rewards_t.max().item()
        print(f"step {step+1:3d}/{args.steps}  "
              f"mean_cross={mean_cross:.0f}  "
              f"min_sample={min_sample:.0f}  "
              f"best={best_cross}  loss={loss.item():+.2f}  "
              f"({elapsed:.0f}s)")
        if (step + 1) % 10 == 0:
            torch.save(model.state_dict(), args.ckpt)

    torch.save(model.state_dict(), args.ckpt)
    elapsed = time.time() - t0
    print(f"\nDone in {elapsed:.0f}s. Best rigid cross = {best_cross} "
          f"(init was {init_cross})")
    print(f"  current ckpt: {args.ckpt}")
    print(f"  best ckpt:    {best_path}")


if __name__ == "__main__":
    main()
