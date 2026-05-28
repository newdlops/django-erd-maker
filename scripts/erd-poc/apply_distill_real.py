#!/usr/bin/env python3
"""Apply trained distillation GNN to a real ERD layout JSON.

  python scripts/erd-poc/apply_distill_real.py \\
    --input /tmp/layout-pre-ml.json --output /tmp/layout-distill.positions.tsv
"""

import argparse
import importlib.util
import json
import os
import time
from pathlib import Path

import torch

# Use all available cores for tensor ops (and any backed BLAS).
_threads = int(os.environ.get("DJERD_ML_THREADS", "16"))
torch.set_num_threads(_threads)
try:
    torch.set_num_interop_threads(min(4, _threads))
except RuntimeError:
    pass  # already set, fine

ROOT = Path(__file__).resolve().parents[2]

spec = importlib.util.spec_from_file_location(
    "train_distill", Path(__file__).parent / "train_distill.py"
)
train_distill = importlib.util.module_from_spec(spec)
spec.loader.exec_module(train_distill)


def build_data_from_layout(
    layout: dict,
    edges_override: list | None = None,
    expert_layout: dict | None = None,
):
    nodes = layout["nodes"]
    routed = edges_override if edges_override is not None else layout["routedEdges"]
    n = len(nodes)
    id2idx = {nd["modelId"]: i for i, nd in enumerate(nodes)}
    ids = [nd["modelId"] for nd in nodes]
    centers = []
    for nd in nodes:
        cx = nd["position"]["x"] + nd["size"]["width"] / 2.0
        cy = nd["position"]["y"] + nd["size"]["height"] / 2.0
        centers.append((cx, cy))
    base_t = torch.tensor(centers, dtype=torch.float32)
    # Match train_distill: normalize in EXPERT's frame so the model sees
    # input in the same scale it was trained on. If no expert layout is
    # supplied (other ERDs), fall back to baseline frame — less accurate
    # but better than nothing.
    if expert_layout is not None:
        exp_centers = []
        for nd in expert_layout["nodes"]:
            ex = nd["position"]["x"] + nd["size"]["width"] / 2.0
            ey = nd["position"]["y"] + nd["size"]["height"] / 2.0
            exp_centers.append((ex, ey))
        exp_t = torch.tensor(exp_centers, dtype=torch.float32)
        pos_mean = exp_t.mean(dim=0, keepdim=True)
        pos_std = exp_t.std() + 1e-3
    else:
        pos_mean = base_t.mean(dim=0, keepdim=True)
        pos_std = base_t.std() + 1e-3
    base_norm = (base_t - pos_mean) / pos_std

    # Match train_distill feature extraction: appLabel-based intra/app_idx
    # + cluster-boundary features (cluster id from per-node clusterId).
    app_labels = [nd["modelId"].split(".")[0] for nd in nodes]
    cluster_by_id = {
        nd["modelId"]: nd.get("clusterId", "")
        for nd in nodes if nd.get("modelId")
    }
    deg = torch.zeros(n, dtype=torch.long)
    inter_cluster_deg = torch.zeros(n, dtype=torch.long)
    edges_pyg = [[], []]
    edge_attrs = []
    EDGE_KIND_VOCAB = train_distill.EDGE_KIND_VOCAB
    for re in routed:
        s_mid = re.get("sourceModelId")
        t_mid = re.get("targetModelId")
        s = id2idx.get(s_mid)
        t = id2idx.get(t_mid)
        if s is None or t is None or s == t: continue
        edges_pyg[0].extend([s, t]); edges_pyg[1].extend([t, s])
        deg[s] += 1; deg[t] += 1
        kind = re.get("kind", "foreign_key")
        kind_idx = (EDGE_KIND_VOCAB.index(kind)
                    if kind in EDGE_KIND_VOCAB else 0)
        kind_oh = [0.0] * len(EDGE_KIND_VOCAB); kind_oh[kind_idx] = 1.0
        intra = 1.0 if app_labels[s] == app_labels[t] else 0.0
        s_cid = cluster_by_id.get(s_mid, "")
        t_cid = cluster_by_id.get(t_mid, "")
        if s_cid and t_cid and s_cid != t_cid:
            inter_cluster_deg[s] += 1
            inter_cluster_deg[t] += 1
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
    # Bundle features (same as train_distill). leafBundles come from the
    # input layout's engineMetadata (cluster_graph baseline already detects
    # them).
    is_bundle_parent = torch.zeros(n)
    is_bundle_leaf = torch.zeros(n)
    bundle_size = torch.zeros(n)
    for bundle in (layout.get("engineMetadata") or {}).get("leafBundles", []) or []:
        pid = bundle.get("parentModelId")
        if pid in id2idx:
            is_bundle_parent[id2idx[pid]] = 1.0
            bundle_size[id2idx[pid]] = float(len(bundle.get("leafModelIds", [])))
        for lid in bundle.get("leafModelIds", []) or []:
            if lid in id2idx:
                is_bundle_leaf[id2idx[lid]] = 1.0
                bundle_size[id2idx[lid]] = float(len(bundle.get("leafModelIds", [])))
    log_bundle_size = torch.log(bundle_size + 1)
    log_inter_cluster_deg = torch.log(inter_cluster_deg.float() + 1)
    is_cluster_boundary = (inter_cluster_deg > 0).float()
    if globals().get("LEGACY_FEATURES", False):
        # 8-feat layout matching train_distill --legacy-features (used by
        # the v3-deep / v3repro-s* checkpoints).
        node_feat = torch.stack([
            log_deg, is_hub, is_leaf,
            torch.log(width + 1) - 5.0,
            torch.log(height + 1) - 4.0,
            is_bundle_parent,
            is_bundle_leaf,
            log_bundle_size,
        ], dim=1)
    else:
        node_feat = torch.stack([
            log_deg, is_hub, is_leaf,
            torch.log(width + 1) - 5.0,
            torch.log(height + 1) - 4.0,
            is_bundle_parent,
            is_bundle_leaf,
            log_bundle_size,
            is_cluster_boundary,
            log_inter_cluster_deg,
        ], dim=1)
    app_idx = torch.tensor(
        [train_distill.app_idx_for_label(lbl) for lbl in app_labels],
        dtype=torch.long,
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
    p.add_argument("--edges-tsv", type=Path, default=None,
                   help="Optional edges.tsv (training-style edges). When set, "
                        "use it instead of layout.routedEdges so feature "
                        "extraction matches training.")
    p.add_argument("--ckpt", type=Path,
                   default=ROOT / "data/erd-poc/checkpoints/distill.pt")
    p.add_argument("--legacy-features", action="store_true",
                   help="use 8-feature input (no cluster boundary) — "
                        "matches v3-deep / v3repro-s* checkpoints")
    p.add_argument("--known-graph-json", type=Path, default=None,
                   help="graph.json path; nodes outside this list keep "
                        "their baseline position instead of the ML output. "
                        "Used to handle ERD models added after checkpoint "
                        "training (model has no signal for them).")
    p.add_argument("--expert-layout", type=Path, default=None,
                   help="path to a known-good expert layout JSON. When "
                        "supplied, normalization uses the expert's pos_mean/"
                        "pos_std (matches training); otherwise the baseline's "
                        "frame is used (works but less accurate when the "
                        "baseline scale drifts vs training).")
    p.add_argument("--device", default="mps")
    p.add_argument("--hidden", type=int, default=96)
    p.add_argument("--layers", type=int, default=4)
    p.add_argument("--dropout", type=float, default=0.1)
    args = p.parse_args()
    global LEGACY_FEATURES
    LEGACY_FEATURES = bool(args.legacy_features)

    print(f"Loading {args.input}")
    with args.input.open() as f:
        layout = json.load(f)
    edges_override = None
    if args.edges_tsv:
        edges_override = []
        for line in args.edges_tsv.read_text().splitlines():
            parts = line.split("\t")
            if len(parts) < 4: continue
            edges_override.append({
                "edgeId": parts[0],
                "sourceModelId": parts[1],
                "targetModelId": parts[2],
                "kind": parts[3],
            })
        print(f"  using edges.tsv: {len(edges_override)} edges")
    expert_layout = None
    if args.expert_layout and args.expert_layout.exists():
        try:
            expert_layout = json.loads(args.expert_layout.read_text())
            print(f"  using expert layout for normalization frame: {args.expert_layout.name}")
        except Exception as e:
            print(f"  warning: could not load expert layout: {e}")
    data, ids, pos_mean, pos_std = build_data_from_layout(
        layout, edges_override, expert_layout
    )
    print(f"  N={data.x.shape[0]}  E={data.edge_index.shape[1]//2}")

    device = torch.device(args.device)
    node_feat_dim = 8 if LEGACY_FEATURES else 10
    model = train_distill.DistillGAT(
        node_feat_dim=node_feat_dim,
        hidden=args.hidden, num_layers=args.layers, dropout=args.dropout,
    )
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

    # Node sets in the layout are variable — the model should predict
    # positions for ALL nodes via GAT message passing, regardless of
    # whether they appeared in any historical training graph. The
    # earlier "unknown-node fallback to FMMM baseline" hid generalization
    # failures and produced far-flung outliers that blew up bbox. We
    # write the model's prediction for every node.
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w") as f:
        for i, mid in enumerate(ids):
            cx = pred_real[i, 0].item()
            cy = pred_real[i, 1].item()
            f.write(f"{mid}\t{cx}\t{cy}\n")
    print(f"  → wrote {args.output} ({len(ids)} positions, all ML-predicted)")


if __name__ == "__main__":
    main()
