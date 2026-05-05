#!/usr/bin/env python3
"""Apply trained RL agent to a real ERD layout JSON.

Reads C++ pipeline output (e.g., /tmp/layout-pre-ml.json), runs RL swap
policy, writes positions.tsv that can be fed to the C++ binary's
--positions-tsv flag for re-routing.

Usage:
  python scripts/erd-poc/apply_rl_real.py \\
    --input /tmp/layout-pre-ml.json \\
    --output /tmp/layout-rl.json.positions.tsv
"""

import argparse
import importlib.util
import json
import random
import time
from pathlib import Path

import torch
from torch_geometric.data import Data

ROOT = Path(__file__).resolve().parents[2]

spec = importlib.util.spec_from_file_location(
    "train_gnn", Path(__file__).parent / "train_gnn.py"
)
train_gnn = importlib.util.module_from_spec(spec)
spec.loader.exec_module(train_gnn)

spec2 = importlib.util.spec_from_file_location(
    "rl_agent", Path(__file__).parent / "rl_agent.py"
)
rl_agent = importlib.util.module_from_spec(spec2)
spec2.loader.exec_module(rl_agent)


EDGE_KIND_VOCAB = train_gnn.EDGE_KIND_VOCAB


def build_data_from_layout(layout: dict) -> tuple:
    """Build PyG Data from a C++ output layout JSON.

    Returns (data, ids_in_order, real_centers, pos_mean, pos_std).
    """
    nodes = layout["nodes"]
    routed_edges = layout["routedEdges"]
    n = len(nodes)
    id2idx = {nd["modelId"]: i for i, nd in enumerate(nodes)}
    ids_in_order = [nd["modelId"] for nd in nodes]

    centers = []
    for nd in nodes:
        cx = nd["position"]["x"] + nd["size"]["width"] / 2.0
        cy = nd["position"]["y"] + nd["size"]["height"] / 2.0
        centers.append((cx, cy))
    real_centers_t = torch.tensor(centers, dtype=torch.float32)
    pos_mean = real_centers_t.mean(dim=0, keepdim=True)
    pos_std = real_centers_t.std() + 1e-3
    normalized = (real_centers_t - pos_mean) / pos_std

    deg = torch.zeros(n, dtype=torch.long)
    edges_pyg = [[], []]
    edge_attrs = []
    for re in routed_edges:
        s = id2idx.get(re.get("sourceModelId"))
        t = id2idx.get(re.get("targetModelId"))
        if s is None or t is None or s == t:
            continue
        edges_pyg[0].extend([s, t])
        edges_pyg[1].extend([t, s])
        deg[s] += 1
        deg[t] += 1
        # Edge features (need cluster id from layout if available).
        kind = re.get("kind", "foreign_key")
        kind_idx = (EDGE_KIND_VOCAB.index(kind)
                    if kind in EDGE_KIND_VOCAB else 0)
        kind_oh = [0.0] * len(EDGE_KIND_VOCAB)
        kind_oh[kind_idx] = 1.0
        # intra_app: same cluster.
        s_cid = nodes[s].get("clusterId", "")
        t_cid = nodes[t].get("clusterId", "")
        intra = 1.0 if s_cid == t_cid and s_cid != "" else 0.0
        feat_fwd = kind_oh + [intra, 1.0]
        feat_bwd = kind_oh + [intra, 0.0]
        edge_attrs.append(feat_fwd)
        edge_attrs.append(feat_bwd)

    edge_index = torch.tensor(edges_pyg, dtype=torch.long)
    edge_attr = torch.tensor(edge_attrs, dtype=torch.float32) \
        if edge_attrs else torch.zeros(0, len(EDGE_KIND_VOCAB) + 2)

    log_deg = torch.log(deg.float() + 1)
    is_hub = (deg >= 10).float()
    is_leaf = (deg == 1).float()
    width = torch.tensor([nd["size"]["width"] for nd in nodes], dtype=torch.float32)
    height = torch.tensor([nd["size"]["height"] for nd in nodes], dtype=torch.float32)
    node_feat = torch.stack([
        log_deg, is_hub, is_leaf,
        torch.log(width + 1) - 5.0,
        torch.log(height + 1) - 4.0,
    ], dim=1)

    # App label hash bucket.
    app_idx = torch.tensor(
        [hash(nd.get("clusterId", "") or nd["modelId"].split(".")[0]) % 16
         for nd in nodes],
        dtype=torch.long,
    )

    data = Data(
        x=node_feat,
        edge_index=edge_index,
        edge_attr=edge_attr,
        y=normalized,
        app_idx=app_idx,
        n=torch.tensor([n]),
    )
    return data, ids_in_order, centers, pos_mean, pos_std


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--input", type=Path, required=True,
                   help="C++ pipeline output JSON")
    p.add_argument("--output", type=Path, required=True,
                   help="positions.tsv to write (modelId\\tcx\\tcy)")
    p.add_argument("--ckpt", type=Path,
                   default=ROOT / "data/erd-poc/checkpoints/rl-policy.pt")
    p.add_argument("--device", default="mps")
    p.add_argument("--max-steps", type=int, default=100,
                   help="more steps for real ERD than synthetic (200+ nodes)")
    p.add_argument("--K", type=int, default=50)
    p.add_argument("--seed", type=int, default=42)
    args = p.parse_args()

    torch.manual_seed(args.seed)
    random.seed(args.seed)

    print(f"Loading {args.input}")
    with args.input.open() as f:
        layout = json.load(f)

    data, ids, centers, pos_mean, pos_std = build_data_from_layout(layout)
    n = data.x.shape[0]
    e = data.edge_index.shape[1] // 2
    print(f"  N={n}  E={e}")

    device = torch.device(args.device)
    policy = rl_agent.SwapPolicy().to(device)
    policy.load_state_dict(
        torch.load(args.ckpt, map_location=device, weights_only=True)
    )
    policy.eval()

    d_dev = data.to(device)
    init_pos = d_dev.y.clone()
    fwd_edges = d_dev.edge_index[:, ::2]

    print(f"Running RL agent: {args.max_steps} swap steps...")
    t0 = time.time()
    with torch.no_grad():
        _, _, final_pos, init_c, end_c = rl_agent.run_episode(
            policy, d_dev, init_pos, fwd_edges,
            max_steps=args.max_steps, K=args.K,
            stochastic=False, device=device,
        )
    elapsed = time.time() - t0
    print(f"  init_soft_cross={init_c:.1f}  end_soft_cross={end_c:.1f}  "
          f"Δ={init_c - end_c:+.1f}  ({elapsed:.1f}s)")

    # Denormalize.
    real_final = final_pos.cpu() * pos_std + pos_mean

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w") as f:
        for i, mid in enumerate(ids):
            cx = real_final[i, 0].item()
            cy = real_final[i, 1].item()
            f.write(f"{mid}\t{cx}\t{cy}\n")
    print(f"  → wrote positions.tsv to {args.output}")


if __name__ == "__main__":
    main()
