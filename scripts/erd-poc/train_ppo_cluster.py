#!/usr/bin/env python3
"""PPO + GAE actor-critic for cluster-move policy (§13 ML replacement).

Improvements over v18 (vanilla REINFORCE):
  • Value function (critic) GNN — predicts state value V(s)
  • GAE-λ advantages — better per-step credit assignment, lower variance
  • Clipped PPO objective — stable policy updates without divergence
  • Multiple PPO epochs per rollout batch — sample efficiency
  • Linear LR decay, entropy bonus for exploration
  • Periodic ckpt saving (resume-friendly)

State (per cluster):
  • current centroid (normalized)
  • drift from initial centroid
  • static: log size, hub flag, singleton flag, log degree

Action: per-cluster (Δx, Δy), Gaussian with learned mean + fixed σ.

Reward (per step): Δcross = old_cross - new_cross (positive = improvement).
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


class ActorCritic(nn.Module):
    """Shared GAT encoder + actor head (delta) + critic head (per-cluster value)."""
    def __init__(self, node_feat_dim=8, hidden=64, num_layers=3, num_heads=4):
        super().__init__()
        self.input_proj = nn.Linear(node_feat_dim, hidden)
        self.edge_proj = nn.Linear(1, hidden // 2)
        self.layers = nn.ModuleList([
            GATv2Conv(hidden, hidden // num_heads, heads=num_heads,
                      edge_dim=hidden // 2, dropout=0.0)
            for _ in range(num_layers)
        ])
        self.actor_head = nn.Sequential(
            nn.Linear(hidden, hidden), nn.ReLU(),
            nn.Linear(hidden, 2),
        )
        self.critic_head = nn.Sequential(
            nn.Linear(hidden, hidden), nn.ReLU(),
            nn.Linear(hidden, 1),
        )
        # zero-init actor delta so initial policy is identity
        nn.init.zeros_(self.actor_head[-1].weight)
        nn.init.zeros_(self.actor_head[-1].bias)

    def encode(self, state, edge_index, edge_attr):
        h = self.input_proj(state)
        e = self.edge_proj(edge_attr) if edge_attr.numel() > 0 else None
        for layer in self.layers:
            h = h + F.elu(layer(h, edge_index, e))
        return h

    def forward(self, state, edge_index, edge_attr):
        h = self.encode(state, edge_index, edge_attr)
        mu = self.actor_head(h)
        # Critic: pool per-cluster values → scalar
        v_per = self.critic_head(h).squeeze(-1)
        v = v_per.mean()
        return mu, v


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
    p.add_argument("--init-from", type=Path, default=None,
                   help="resume from prior actor weights (ignores critic)")
    p.add_argument("--updates", type=int, default=200)
    p.add_argument("--episodes-per-update", type=int, default=4)
    p.add_argument("--episode-length", type=int, default=12)
    p.add_argument("--ppo-epochs", type=int, default=4)
    p.add_argument("--lr", type=float, default=1e-4)
    p.add_argument("--lr-decay", type=float, default=0.95,
                   help="LR multiplied by this every 20 updates")
    p.add_argument("--sigma", type=float, default=300.0)
    p.add_argument("--gamma", type=float, default=0.99)
    p.add_argument("--gae-lambda", type=float, default=0.95)
    p.add_argument("--clip-eps", type=float, default=0.2)
    p.add_argument("--entropy-coef", type=float, default=0.001)
    p.add_argument("--value-coef", type=float, default=0.5)
    p.add_argument("--hidden", type=int, default=64)
    p.add_argument("--layers", type=int, default=3)
    p.add_argument("--device", default="cpu")
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--save-every", type=int, default=10)
    args = p.parse_args()

    torch.manual_seed(args.seed)
    args.ckpt.parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="ppo-cluster-"))
    print(f"workdir: {work}")

    layout = json.loads(args.input.read_text())
    clusters, cid_to_idx, node_pos, edge_index, edge_attr = \
        build_cluster_supergraph(layout)
    C = len(clusters)
    print(f"super-graph: {C} units, {edge_index.shape[1]//2} edges")

    # Features
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

    def make_state(cur_centroids: torch.Tensor) -> torch.Tensor:
        cur_norm = (cur_centroids - init_centroids.mean(0)) / centroid_std
        drift = (cur_centroids - init_centroids) / centroid_std
        return torch.cat([cur_norm, drift, static_feat], dim=1)

    model = ActorCritic(node_feat_dim=8, hidden=args.hidden,
                         num_layers=args.layers).to(device)
    if args.init_from and args.init_from.exists():
        sd = torch.load(args.init_from, map_location=device, weights_only=True)
        # Load partial (skip critic if mismatched)
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

    def write_tsv(node_pos_t: torch.Tensor) -> Path:
        tsv = work / f"p-{time.time_ns()}.tsv"
        cpu = node_pos_t.detach().cpu()
        with tsv.open("w") as f:
            for i, mid in enumerate(all_ids):
                f.write(f"{mid}\t{cpu[i,0].item():.3f}\t{cpu[i,1].item():.3f}\n")
        return tsv

    def eval_cross(centroids: torch.Tensor) -> int:
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

    # Reward normalization for stable training
    reward_running_mean = 0.0
    reward_running_var = 1e6  # crossings change by 100s typically

    t0 = time.time()
    for upd in range(args.updates):
        # ===== Rollout: collect trajectories =====
        rollout_states = []      # [E*T, C, feat]
        rollout_actions = []     # [E*T, C, 2]
        rollout_log_probs = []   # [E*T] — old log probs
        rollout_returns = []     # [E*T] — GAE-λ returns (for critic target)
        rollout_advantages = []  # [E*T] — GAE-λ advantages
        rollout_values = []      # [E*T] — old V(s)

        ep_finals = []
        for ep in range(args.episodes_per_update):
            cur = init_centroids.clone()
            cur_cross = init_cross
            ep_states, ep_actions, ep_log_probs, ep_values = [], [], [], []
            ep_rewards = []
            for t in range(args.episode_length):
                state = make_state(cur)
                with torch.no_grad():
                    mu, v = model(state, edge_index, edge_attr)
                eps = torch.randn_like(mu)
                action = mu + args.sigma * eps
                log_p = -0.5 * (((action - mu) / args.sigma) ** 2).sum()
                # Apply
                new_c = cur + action
                new_cross = eval_cross(new_c)
                reward = float(cur_cross - new_cross)
                if new_cross < best_cross:
                    best_cross = new_cross
                    torch.save(model.state_dict(), best_path)
                ep_states.append(state)
                ep_actions.append(action)
                ep_log_probs.append(log_p)
                ep_values.append(v)
                ep_rewards.append(reward)
                cur = new_c
                cur_cross = new_cross
            ep_finals.append(cur_cross)

            # Reward normalization (running stats)
            for r in ep_rewards:
                reward_running_mean = 0.99 * reward_running_mean + 0.01 * r
            # Note: variance update done after returns computed

            # Compute GAE-λ advantages + returns
            # Need bootstrap value for last state
            with torch.no_grad():
                last_state = make_state(cur)
                _, last_v = model(last_state, edge_index, edge_attr)
            T = args.episode_length
            advantages = torch.zeros(T, device=device)
            gae = 0.0
            for t in reversed(range(T)):
                next_v = last_v.item() if t == T - 1 else ep_values[t+1].item()
                delta = (ep_rewards[t]
                         + args.gamma * next_v
                         - ep_values[t].item())
                gae = delta + args.gamma * args.gae_lambda * gae
                advantages[t] = gae
            returns = advantages + torch.tensor(
                [v.item() for v in ep_values], device=device)

            rollout_states.extend(ep_states)
            rollout_actions.extend([a.detach() for a in ep_actions])
            rollout_log_probs.extend([lp.detach() for lp in ep_log_probs])
            rollout_values.extend([v.detach() for v in ep_values])
            rollout_advantages.extend(advantages.tolist())
            rollout_returns.extend(returns.tolist())

        # ===== PPO updates =====
        old_log_probs = torch.tensor(
            [lp.item() for lp in rollout_log_probs],
            device=device, dtype=torch.float32)
        advantages_t = torch.tensor(rollout_advantages, device=device,
                                     dtype=torch.float32)
        # Normalize advantages
        advantages_t = (advantages_t - advantages_t.mean()) \
                       / (advantages_t.std() + 1e-6)
        returns_t = torch.tensor(rollout_returns, device=device,
                                  dtype=torch.float32)

        total_pol_loss = 0.0
        total_val_loss = 0.0
        total_ent = 0.0
        for ppo_ep in range(args.ppo_epochs):
            for i in range(len(rollout_states)):
                state = rollout_states[i]
                action = rollout_actions[i]
                old_lp = old_log_probs[i]
                advantage = advantages_t[i]
                return_t = returns_t[i]

                mu, v = model(state, edge_index, edge_attr)
                new_lp = -0.5 * (((action - mu) / args.sigma) ** 2).sum()
                ratio = torch.exp(new_lp - old_lp)
                surr1 = ratio * advantage
                surr2 = torch.clamp(ratio, 1 - args.clip_eps,
                                    1 + args.clip_eps) * advantage
                pol_loss = -torch.min(surr1, surr2)
                val_loss = (v - return_t).pow(2)
                # Entropy of fixed-σ Gaussian = const (depends only on σ);
                # so entropy bonus is just a regularizer here (constant).
                # Skip explicit entropy term for fixed σ.
                loss = pol_loss + args.value_coef * val_loss
                optim.zero_grad()
                loss.backward()
                torch.nn.utils.clip_grad_norm_(model.parameters(), 0.5)
                optim.step()
                total_pol_loss += pol_loss.item()
                total_val_loss += val_loss.item()
        n_samples = len(rollout_states) * args.ppo_epochs

        # LR decay
        if (upd + 1) % 20 == 0:
            for g in optim.param_groups:
                g["lr"] *= args.lr_decay

        elapsed = time.time() - t0
        avg_final = sum(ep_finals) / len(ep_finals)
        avg_pol_loss = total_pol_loss / max(1, n_samples)
        avg_val_loss = total_val_loss / max(1, n_samples)
        print(f"upd {upd+1:3d}/{args.updates}  "
              f"final={avg_final:.0f}  "
              f"Δfrom_init={init_cross-avg_final:+.0f}  "
              f"best={best_cross}  "
              f"polL={avg_pol_loss:+.3f} valL={avg_val_loss:.1f}  "
              f"lr={optim.param_groups[0]['lr']:.5f}  "
              f"({elapsed:.0f}s)")
        if (upd + 1) % args.save_every == 0:
            torch.save(model.state_dict(), args.ckpt)

    torch.save(model.state_dict(), args.ckpt)
    elapsed = time.time() - t0
    print(f"\nDone in {elapsed:.0f}s. Best cross = {best_cross} "
          f"(init {init_cross}; reduction {init_cross-best_cross})")
    print(f"  current ckpt: {args.ckpt}")
    print(f"  best ckpt:    {best_path}")


if __name__ == "__main__":
    main()
