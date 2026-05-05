#!/usr/bin/env python3
"""Apply trained distillation GNN to a real ERD layout JSON.

  python scripts/erd-poc/apply_distill_real.py \\
    --input /tmp/layout-pre-ml.json --output /tmp/layout-distill.positions.tsv
"""

import argparse
import importlib.util
import json
import time
from pathlib import Path

import torch

ROOT = Path(__file__).resolve().parents[2]

spec = importlib.util.spec_from_file_location(
    "train_distill", Path(__file__).parent / "train_distill.py"
)
train_distill = importlib.util.module_from_spec(spec)
spec.loader.exec_module(train_distill)


def build_data_from_layout(layout: dict):
    nodes = layout["nodes"]
    routed = layout["routedEdges"]
    n = len(nodes)
    id2idx = {nd["modelId"]: i for i, nd in enumerate(nodes)}
    ids = [nd["modelId"] for nd in nodes]
    centers = []
    for nd in nodes:
        cx = nd["position"]["x"] + nd["size"]["width"] / 2.0
        cy = nd["position"]["y"] + nd["size"]["height"] / 2.0
        centers.append((cx, cy))
    base_t = torch.tensor(centers, dtype=torch.float32)
    pos_mean = base_t.mean(dim=0, keepdim=True)
    pos_std = base_t.std() + 1e-3
    base_norm = (base_t - pos_mean) / pos_std

    deg = torch.zeros(n, dtype=torch.long)
    edges_pyg = [[], []]
    edge_attrs = []
    EDGE_KIND_VOCAB = train_distill.EDGE_KIND_VOCAB
    for re in routed:
        s = id2idx.get(re.get("sourceModelId"))
        t = id2idx.get(re.get("targetModelId"))
        if s is None or t is None or s == t: continue
        edges_pyg[0].extend([s, t]); edges_pyg[1].extend([t, s])
        deg[s] += 1; deg[t] += 1
        kind = re.get("kind", "foreign_key")
        kind_idx = (EDGE_KIND_VOCAB.index(kind)
                    if kind in EDGE_KIND_VOCAB else 0)
        kind_oh = [0.0] * len(EDGE_KIND_VOCAB); kind_oh[kind_idx] = 1.0
        s_cid = nodes[s].get("clusterId", "")
        t_cid = nodes[t].get("clusterId", "")
        intra = 1.0 if s_cid == t_cid and s_cid != "" else 0.0
        edge_attrs.append(kind_oh + [intra, 1.0])
        edge_attrs.append(kind_oh + [intra, 0.0])

    edge_index = torch.tensor(edges_pyg, dtype=torch.long)
    edge_attr = torch.tensor(edge_attrs, dtype=torch.float32) \
        if edge_attrs else torch.zeros(0, len(EDGE_KIND_VOCAB) + 2)
    log_deg = torch.log(deg.float() + 1)
    is_hub = (deg >= 10).float()
    is_leaf = (deg == 1).float()
    width = torch.tensor([nd["size"]["width"] for nd in nodes],
                         dtype=torch.float32)
    height = torch.tensor([nd["size"]["height"] for nd in nodes],
                          dtype=torch.float32)
    node_feat = torch.stack([
        log_deg, is_hub, is_leaf,
        torch.log(width + 1) - 5.0,
        torch.log(height + 1) - 4.0,
    ], dim=1)
    app_idx = torch.tensor(
        [hash(nd.get("clusterId", "") or nd["modelId"].split(".")[0]) % 16
         for nd in nodes], dtype=torch.long
    )
    from torch_geometric.data import Data
    data = Data(
        x=node_feat, edge_index=edge_index, edge_attr=edge_attr,
        baseline=base_norm, app_idx=app_idx, n=torch.tensor([n]),
    )
    return data, ids, pos_mean, pos_std


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--input", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--ckpt", type=Path,
                   default=ROOT / "data/erd-poc/checkpoints/distill.pt")
    p.add_argument("--device", default="mps")
    args = p.parse_args()

    print(f"Loading {args.input}")
    with args.input.open() as f:
        layout = json.load(f)
    data, ids, pos_mean, pos_std = build_data_from_layout(layout)
    print(f"  N={data.x.shape[0]}  E={data.edge_index.shape[1]//2}")

    device = torch.device(args.device)
    model = train_distill.DistillGAT()
    model.load_state_dict(
        torch.load(args.ckpt, map_location=device, weights_only=True)
    )
    model.to(device).eval()

    t0 = time.time()
    with torch.no_grad():
        pred = model(data.to(device))
    elapsed = time.time() - t0
    print(f"  inference: {elapsed*1000:.0f}ms")
    pred_real = pred.cpu() * pos_std + pos_mean

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w") as f:
        for i, mid in enumerate(ids):
            cx = pred_real[i, 0].item()
            cy = pred_real[i, 1].item()
            f.write(f"{mid}\t{cx}\t{cy}\n")
    print(f"  → wrote {args.output}")


if __name__ == "__main__":
    main()
