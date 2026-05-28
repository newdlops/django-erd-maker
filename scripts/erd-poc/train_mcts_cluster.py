#!/usr/bin/env python3
"""AlphaZero-style MCTS + policy/value network for cluster-move layout.

This is a research-grade approach addressing the fundamental issue with
plain policy gradient: in a 17k-action space where 95% of moves hurt,
random sampling can't discover the rare improving moves consistently.

MCTS uses the policy network as a prior to focus search on promising
moves, and the value network bootstraps long-horizon credit assignment
without expensive rollouts. Self-play (the agent plays its own
moves with MCTS-improved policy) produces training data.

Cross evaluator: FastCrossEval (numpy, ~60ms) so each MCTS expansion
that touches a new state is feasible.

Algorithm (simplified):
  1. For each "decision step" in an episode:
       a. Build a search tree rooted at current state.
       b. Run K MCTS simulations:
          - Walk from root using PUCT until hitting an unvisited child.
          - Expand: evaluate state with NN → (policy prior, value).
          - Backpropagate value up the path.
       c. Action distribution = visit counts at root; sample action.
       d. Record (state, search_policy, current_cross) for training.
  2. Run T decision steps per episode → final cross.
  3. Terminal value: how much the episode reduced cross.
  4. Train NN to fit:
       - Policy head: predict search_policy distribution
       - Value head: predict (init - cross_at_state) / scale
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
A_PER_C = NUM_DIRS * len(STRIDES)


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


class PolicyValueNet(nn.Module):
    """GAT encoder + policy head (per-cluster logits) + value head (scalar)."""
    def __init__(self, node_feat_dim=8, hidden=64, num_layers=3, num_heads=4):
        super().__init__()
        self.input_proj = nn.Linear(node_feat_dim, hidden)
        self.edge_proj = nn.Linear(1, hidden // 2)
        self.layers = nn.ModuleList([
            GATv2Conv(hidden, hidden // num_heads, heads=num_heads,
                      edge_dim=hidden // 2, dropout=0.0)
            for _ in range(num_layers)
        ])
        self.policy_head = nn.Sequential(
            nn.Linear(hidden, hidden), nn.ReLU(),
            nn.Linear(hidden, A_PER_C),
        )
        self.value_head = nn.Sequential(
            nn.Linear(hidden, hidden), nn.ReLU(),
            nn.Linear(hidden, 1),
        )

    def encode(self, state, edge_index, edge_attr):
        h = self.input_proj(state)
        e = self.edge_proj(edge_attr) if edge_attr.numel() > 0 else None
        for layer in self.layers:
            h = h + F.elu(layer(h, edge_index, e))
        return h

    def forward(self, state, edge_index, edge_attr):
        h = self.encode(state, edge_index, edge_attr)
        # Policy: per-cluster logits → flatten to single categorical
        policy_logits = self.policy_head(h).reshape(-1)  # [C*A]
        # Value: pool to scalar
        v = self.value_head(h.mean(0)).squeeze()
        return policy_logits, v


# ---------- MCTS ----------

class Node:
    """One node in MCTS tree. Children indexed by action (flat 0..C*A-1).

    For memory efficiency, children dict is sparse (only visited actions).
    """
    __slots__ = ("parent", "prior", "visits", "value_sum", "children",
                  "state_hash", "is_expanded", "_priors")

    def __init__(self, parent=None, prior=0.0):
        self.parent = parent
        self.prior = prior
        self.visits = 0
        self.value_sum = 0.0
        self.children = {}  # action -> Node
        self.is_expanded = False

    @property
    def q(self):
        return self.value_sum / max(1, self.visits)


def select_action_puct(node: "Node", priors: np.ndarray, c_puct: float,
                        legal_mask: np.ndarray = None) -> int:
    """Select action with max PUCT: Q(s,a) + c * P(s,a) * sqrt(N(s))/(1+N(s,a))."""
    sqrt_n = math.sqrt(max(1, node.visits))
    n_actions = priors.shape[0]
    best = -float("inf")
    best_a = 0
    for a in range(n_actions):
        if legal_mask is not None and not legal_mask[a]:
            continue
        child = node.children.get(a)
        if child is None:
            q = 0.0
            n_child = 0
        else:
            q = child.q
            n_child = child.visits
        u = c_puct * priors[a] * sqrt_n / (1 + n_child)
        score = q + u
        if score > best:
            best = score
            best_a = a
    return best_a


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--input", type=Path, required=True)
    p.add_argument("--ckpt", type=Path, required=True)
    p.add_argument("--init-from", type=Path, default=None)
    p.add_argument("--iterations", type=int, default=20,
                   help="train iterations (each iter = self-play + sgd)")
    p.add_argument("--episodes-per-iter", type=int, default=10)
    p.add_argument("--episode-length", type=int, default=30,
                   help="MCTS decisions per episode")
    p.add_argument("--mcts-simulations", type=int, default=50,
                   help="MCTS sims per decision (more = stronger but slower)")
    p.add_argument("--c-puct", type=float, default=2.0)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--hidden", type=int, default=64)
    p.add_argument("--layers", type=int, default=3)
    p.add_argument("--value-scale", type=float, default=1000.0,
                   help="reward unit (target value = cross_reduction / scale)")
    p.add_argument("--device", default="cpu")
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--top-k-actions", type=int, default=64,
                   help="restrict MCTS to top-K policy actions (full=17280 too slow)")
    args = p.parse_args()

    torch.manual_seed(args.seed)
    np.random.seed(args.seed)
    args.ckpt.parent.mkdir(parents=True, exist_ok=True)

    layout = json.loads(args.input.read_text())
    clusters, cid_to_idx, node_pos, edge_index, edge_attr = \
        build_cluster_supergraph(layout)
    C = len(clusters)
    A = A_PER_C
    print(f"super-graph: {C} units, {C*A} total actions, "
          f"top-K MCTS={args.top_k_actions}")

    init_centroids = np.array([c["centroid"] for c in clusters], dtype=np.float64)
    centroid_std = float(np.std(init_centroids) + 1e-3)
    static_feat = np.zeros((C, 4), dtype=np.float32)
    for i, c in enumerate(clusters):
        in_deg = int((edge_index[1] == i).sum().item())
        static_feat[i] = [
            math.log(c["size"] + 1),
            1.0 if c["size"] >= 5 else 0.0,
            1.0 if c["is_singleton"] else 0.0,
            math.log(in_deg + 1),
        ]

    all_ids = list(node_pos.keys())
    id_to_idx = {mid: i for i, mid in enumerate(all_ids)}
    n_nodes = len(all_ids)
    init_node_pos = np.zeros((n_nodes, 2), dtype=np.float64)
    for i, mid in enumerate(all_ids):
        init_node_pos[i] = node_pos[mid]
    member_cluster_np = np.zeros(n_nodes, dtype=np.int32)
    for cidx, c in enumerate(clusters):
        for mid in c["members"]:
            member_cluster_np[id_to_idx[mid]] = cidx

    edges_for_eval = []
    for re in layout["routedEdges"]:
        s = id_to_idx.get(re.get("sourceModelId"))
        t = id_to_idx.get(re.get("targetModelId"))
        if s is None or t is None or s == t: continue
        edges_for_eval.append((s, t))
    edges_arr = np.array(edges_for_eval, dtype=np.int32)
    evaluator = fce.FastCrossEval(edges_arr, n_nodes)
    print(f"FastCrossEval: {evaluator.K} edge pairs")

    device = torch.device(args.device)
    edge_index_t = edge_index.to(device)
    edge_attr_t = edge_attr.to(device)
    init_centroids_t = torch.tensor(init_centroids, dtype=torch.float32, device=device)
    centroid_mean_t = init_centroids_t.mean(0)
    static_feat_t = torch.tensor(static_feat, device=device)

    def state_tensor(centroids_np):
        cur_t = torch.tensor(centroids_np, dtype=torch.float32, device=device)
        cur_norm = (cur_t - centroid_mean_t) / centroid_std
        drift = (cur_t - init_centroids_t) / centroid_std
        return torch.cat([cur_norm, drift, static_feat_t], dim=1)

    model = PolicyValueNet(node_feat_dim=8, hidden=args.hidden,
                            num_layers=args.layers).to(device)
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
    action_dx = np.zeros(A)
    action_dy = np.zeros(A)
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

    def apply_action(centroids, flat_action):
        """flat_action in 0..C*A-1 → (cluster ci, action_idx ai). Returns new centroids."""
        ci = flat_action // A
        ai = flat_action % A
        new_c = centroids.copy()
        new_c[ci, 0] += action_dx[ai]
        new_c[ci, 1] += action_dy[ai]
        return new_c

    def net_predict(centroids_np):
        """Run NN → (policy_probs, value) for one state."""
        with torch.no_grad():
            state = state_tensor(centroids_np)
            logits, v = model(state, edge_index_t, edge_attr_t)
            probs = F.softmax(logits, dim=0).cpu().numpy()
        return probs, float(v.item())

    init_cross = eval_cross(init_centroids)
    print(f"init cross: {init_cross}")
    best_cross = init_cross
    torch.save(model.state_dict(), args.ckpt)
    best_path = args.ckpt.with_name(args.ckpt.stem + "-best.pt")
    torch.save(model.state_dict(), best_path)

    t0 = time.time()
    for it in range(args.iterations):
        # ===== Self-play: collect training examples =====
        training_states = []        # state tensors
        training_policies = []      # search policy distributions (flat)
        training_returns = []       # terminal value targets
        ep_finals = []

        for ep in range(args.episodes_per_iter):
            cur_centroids = init_centroids.copy()
            cur_cross = init_cross
            episode_records = []  # (state, search_policy)
            for t in range(args.episode_length):
                # Run MCTS from current state
                root = Node()
                # Expand root with NN prior
                priors, v = net_predict(cur_centroids)
                root.is_expanded = True
                # Top-K mask (limit branching factor)
                topk_idx = np.argpartition(priors, -args.top_k_actions)[-args.top_k_actions:]
                legal_mask = np.zeros(len(priors), dtype=bool)
                legal_mask[topk_idx] = True
                # Renormalize priors over masked actions
                masked = np.where(legal_mask, priors, 0)
                if masked.sum() > 0:
                    masked = masked / masked.sum()
                else:
                    masked = legal_mask.astype(float) / legal_mask.sum()
                root_priors = masked

                # Cache: action → resulting cross (avoid re-evaluating same move)
                action_cross_cache = {}

                for sim in range(args.mcts_simulations):
                    # === Selection ===
                    path = [root]
                    node = root
                    while node.is_expanded:
                        a = select_action_puct(node, root_priors if node is root
                                                else node._priors, args.c_puct,
                                                legal_mask=legal_mask)
                        child = node.children.get(a)
                        if child is None:
                            child = Node(parent=node, prior=root_priors[a]
                                          if node is root else node._priors[a])
                            node.children[a] = child
                        path.append(child)
                        node = child
                        if not node.is_expanded:
                            # We'll expand it next; first eval the state
                            break
                    # === Expansion ===
                    # Reconstruct state by applying actions from root
                    sim_centroids = cur_centroids.copy()
                    actions_so_far = []
                    for k in range(1, len(path)):
                        # find which action led to path[k] from path[k-1]
                        parent = path[k-1]
                        for act, ch in parent.children.items():
                            if ch is path[k]:
                                actions_so_far.append(act)
                                break
                    for a in actions_so_far:
                        sim_centroids = apply_action(sim_centroids, a)
                    # Evaluate cross at this state
                    sim_cross = eval_cross(sim_centroids)
                    # NN predict at this state
                    leaf_priors, leaf_v = net_predict(sim_centroids)
                    # Restrict to same legal mask (or recompute? keep same for simplicity)
                    leaf_priors_masked = np.where(legal_mask, leaf_priors, 0)
                    if leaf_priors_masked.sum() > 0:
                        leaf_priors_masked = leaf_priors_masked / leaf_priors_masked.sum()
                    else:
                        leaf_priors_masked = legal_mask.astype(float) / legal_mask.sum()
                    node._priors = leaf_priors_masked
                    node.is_expanded = True
                    # Value: use NN value PLUS realized cross reduction so far
                    realized = (init_cross - sim_cross) / args.value_scale
                    value = realized + leaf_v * 0.5  # blend with NN value
                    # === Backprop ===
                    for n in path:
                        n.visits += 1
                        n.value_sum += value

                # Convert root visits to action distribution
                search_policy = np.zeros(len(root_priors))
                for a, child in root.children.items():
                    search_policy[a] = child.visits
                if search_policy.sum() > 0:
                    search_policy /= search_policy.sum()
                else:
                    search_policy = root_priors

                # Record example
                episode_records.append((cur_centroids.copy(), search_policy.copy()))

                # Sample action (greedy or sample from search_policy)
                # Use greedy from MCTS visits for stronger play in self-play
                action = int(np.argmax(search_policy))
                cur_centroids = apply_action(cur_centroids, action)
                cur_cross = eval_cross(cur_centroids)
                if cur_cross < best_cross:
                    best_cross = cur_cross
                    torch.save(model.state_dict(), best_path)

            # Episode complete: assign terminal return to all states
            terminal_value = (init_cross - cur_cross) / args.value_scale
            for (s, p) in episode_records:
                training_states.append(s)
                training_policies.append(p)
                training_returns.append(terminal_value)
            ep_finals.append(cur_cross)

        # ===== Train NN on collected data =====
        n_examples = len(training_states)
        states_t_batch = [state_tensor(s) for s in training_states]
        policies_t = torch.tensor(np.array(training_policies),
                                   dtype=torch.float32, device=device)
        returns_t = torch.tensor(training_returns, dtype=torch.float32, device=device)

        total_pol_loss = 0.0
        total_val_loss = 0.0
        # Single epoch over examples (could do more)
        for i in range(n_examples):
            state = states_t_batch[i]
            target_policy = policies_t[i]
            target_value = returns_t[i]
            optim.zero_grad()
            logits, v_pred = model(state, edge_index_t, edge_attr_t)
            log_probs = F.log_softmax(logits, dim=0)
            # Cross-entropy: -Σ target * log_probs
            pol_loss = -(target_policy * log_probs).sum()
            val_loss = (v_pred - target_value).pow(2)
            loss = pol_loss + val_loss
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            optim.step()
            total_pol_loss += pol_loss.item()
            total_val_loss += val_loss.item()

        elapsed = time.time() - t0
        avg_final = sum(ep_finals) / len(ep_finals)
        print(f"iter {it+1:2d}/{args.iterations}  "
              f"final={avg_final:.0f}  Δfrom_init={init_cross-avg_final:+.0f}  "
              f"best={best_cross}  polL={total_pol_loss/n_examples:.3f}  "
              f"valL={total_val_loss/n_examples:.4f}  "
              f"examples={n_examples}  ({elapsed:.0f}s)",
              flush=True)
        torch.save(model.state_dict(), args.ckpt)

    elapsed = time.time() - t0
    print(f"\nDone in {elapsed:.0f}s. Best = {best_cross} "
          f"(init {init_cross}; reduction {init_cross-best_cross})")
    print(f"  best ckpt: {best_path}")


if __name__ == "__main__":
    main()
