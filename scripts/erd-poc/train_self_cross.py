#!/usr/bin/env python3
"""Train policy to directly minimize edge crossings — no expert imitation.

The model takes baseline positions and outputs a delta; the loss is
soft_cross_loss (differentiable approximation of segment crossings).
A small anchor term (MSE to baseline scaled by per-node mean position
std) prevents catastrophic collapse to a single point.

Periodically evaluates ACTUAL edge crossings via the C++ rigid reroute
on captain and tracks the best-actual checkpoint independently of the
training loss.

Usage:
  python scripts/erd-poc/train_self_cross.py \\
    --init-from data/erd-poc/checkpoints/v12-general-best.pt \\
    --ckpt data/erd-poc/checkpoints/v14-self.pt \\
    --epochs 100 --lr 1e-5
"""

import argparse
import importlib.util
import json
import os
import subprocess
import tempfile
import time
from pathlib import Path

import torch

ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / "bin/ogdf/darwin-arm64/django-erd-ogdf-layout"
NODES_TSV = ROOT / "data/erd-poc/graphs/real-main/nodes.tsv"
EDGES_TSV = ROOT / "data/erd-poc/graphs/real-main/edges.tsv"
APPLY_SCRIPT = ROOT / "scripts/erd-poc/apply_distill_real.py"
CAPTAIN_BASELINE = ROOT / "data/erd-poc/layouts/real-main.json"
EXPERT_LAYOUT = ROOT / "data/erd-poc/expert-strong/real-main.json"

spec = importlib.util.spec_from_file_location(
    "train_distill", Path(__file__).parent / "train_distill.py"
)
train_distill = importlib.util.module_from_spec(spec)
spec.loader.exec_module(train_distill)


def measure_real_cross(ckpt_path: Path, device="cpu",
                       hidden=256, layers=8) -> int:
    """Apply ckpt to captain baseline, reroute via C++ rigid, return cross."""
    work = Path(tempfile.mkdtemp(prefix="self-eval-"))
    pos_tsv = work / "positions.tsv"
    out_json = work / "final.json"
    venv_py = ROOT / ".venv-ml/bin/python"
    r = subprocess.run(
        [str(venv_py), str(APPLY_SCRIPT),
         "--input", str(CAPTAIN_BASELINE),
         "--output", str(pos_tsv),
         "--ckpt", str(ckpt_path),
         "--expert-layout", str(EXPERT_LAYOUT),
         "--device", device,
         "--hidden", str(hidden),
         "--layers", str(layers),
         "--dropout", "0.1"],
        capture_output=True, text=True,
    )
    if r.returncode != 0:
        raise RuntimeError(f"apply failed: {r.stderr[:500]}")
    r2 = subprocess.run(
        [str(BINARY), "layout",
         "--mode", "hierarchical_barycenter",
         "--nodes-file", str(NODES_TSV),
         "--edges-file", str(EDGES_TSV),
         "--edge-routing", "straight",
         "--cluster-graph", "1",
         "--positions-tsv", str(pos_tsv),
         "--rigid-positions", "1"],
        capture_output=True, text=True,
    )
    if r2.returncode != 0:
        raise RuntimeError(f"reroute failed: {r2.stderr[:500]}")
    d = json.loads(r2.stdout)
    cross = int(d["engineMetadata"]["edgeCrossings"])
    nodes = d["nodes"]
    xs = [n["position"]["x"] + n["size"]["width"]/2 for n in nodes]
    ys = [n["position"]["y"] + n["size"]["height"]/2 for n in nodes]
    bbox = (max(xs)-min(xs)) * (max(ys)-min(ys)) / 1e9
    return cross, bbox


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--init-from", type=Path, required=True)
    p.add_argument("--ckpt", type=Path, required=True)
    p.add_argument("--graphs", type=Path,
                   default=ROOT / "data/erd-poc/graphs")
    p.add_argument("--baseline", type=Path,
                   default=ROOT / "data/erd-poc/layouts")
    p.add_argument("--polished", type=Path,
                   default=ROOT / "data/erd-poc/expert-strong")
    p.add_argument("--epochs", type=int, default=100)
    p.add_argument("--lr", type=float, default=1e-5)
    p.add_argument("--hidden", type=int, default=256)
    p.add_argument("--layers", type=int, default=8)
    p.add_argument("--dropout", type=float, default=0.1)
    p.add_argument("--weight-decay", type=float, default=0.0)
    p.add_argument("--w-cross", type=float, default=1.0)
    p.add_argument("--w-anchor", type=float, default=0.1,
                   help="weight on baseline anchor (prevents collapse)")
    p.add_argument("--cross-sample-pairs", type=int, default=5000)
    p.add_argument("--eval-every", type=int, default=5,
                   help="real-cross eval every N epochs (slow)")
    p.add_argument("--upweight", type=str, default="real-main")
    p.add_argument("--upweight-factor", type=int, default=10)
    p.add_argument("--device", default="cpu")
    p.add_argument("--seed", type=int, default=0)
    args = p.parse_args()

    torch.manual_seed(args.seed)
    args.ckpt.parent.mkdir(parents=True, exist_ok=True)

    ds = train_distill.ERDDistillDataset(args.graphs, args.baseline,
                                          args.polished)
    valid = [i for i in range(ds.len()) if ds.get(i) is not None]
    print(f"Valid pairs: {len(valid)}/{ds.len()}")

    # Upweight real-main in training (still want to focus on captain)
    name_to_idx = {ds.graph_dirs[i].name: i for i in valid}
    train_idx = list(valid)
    if args.upweight and args.upweight in name_to_idx:
        idx = name_to_idx[args.upweight]
        for _ in range(args.upweight_factor - 1):
            train_idx.append(idx)
        print(f"  upweighted {args.upweight} x{args.upweight_factor}")
    print(f"  train={len(train_idx)}")

    device = torch.device(args.device)
    model = train_distill.DistillGAT(
        node_feat_dim=10, hidden=args.hidden,
        num_layers=args.layers, dropout=args.dropout,
    ).to(device)
    sd = torch.load(args.init_from, map_location=device, weights_only=True)
    model.load_state_dict(sd)
    print(f"  loaded init weights from {args.init_from}")

    optim = torch.optim.AdamW(model.parameters(), lr=args.lr,
                              weight_decay=args.weight_decay)

    # Initial real-cross baseline
    print("Measuring initial real cross on captain...")
    torch.save(model.state_dict(), args.ckpt)
    init_cross, init_bbox = measure_real_cross(
        args.ckpt, hidden=args.hidden, layers=args.layers)
    print(f"  init: cross={init_cross} bbox={init_bbox:.2f}B")
    best_cross = init_cross
    best_bbox = init_bbox

    t0 = time.time()
    for epoch in range(args.epochs):
        model.train()
        cross_losses = []
        anchor_losses = []
        for i in train_idx:
            d = ds.get(i).to(device)
            optim.zero_grad()
            pred = model(d)
            # Cross loss (differentiable proxy)
            cross = train_distill.soft_cross_loss(
                pred, d.edge_index,
                sample_pairs=args.cross_sample_pairs)
            e_count = max(1, d.edge_index.shape[1] // 2)
            cross_norm = cross / e_count
            # Anchor: stay close to baseline scale (prevents collapse)
            anchor = ((pred - d.baseline) ** 2).mean()
            loss = args.w_cross * cross_norm + args.w_anchor * anchor
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            optim.step()
            cross_losses.append(cross_norm.item())
            anchor_losses.append(anchor.item())

        avg_cross = sum(cross_losses) / max(len(cross_losses), 1)
        avg_anchor = sum(anchor_losses) / max(len(anchor_losses), 1)

        # Real cross eval
        do_eval = (epoch + 1) % args.eval_every == 0 or epoch == 0
        if do_eval:
            torch.save(model.state_dict(), args.ckpt)
            try:
                real_cross, real_bbox = measure_real_cross(
                    args.ckpt, hidden=args.hidden, layers=args.layers)
                tag = ""
                if real_cross < best_cross:
                    best_cross = real_cross
                    best_bbox = real_bbox
                    torch.save(model.state_dict(),
                               args.ckpt.with_name(args.ckpt.stem +
                                                    "-best.pt"))
                    tag = " ★"
                elapsed = time.time() - t0
                print(f"epoch {epoch+1:3d}/{args.epochs}  "
                      f"soft_cross={avg_cross:.4f} anchor={avg_anchor:.4f}  "
                      f"REAL: cross={real_cross} bbox={real_bbox:.2f}B  "
                      f"best={best_cross} ({elapsed:.0f}s){tag}")
            except Exception as e:
                print(f"epoch {epoch+1:3d}: eval failed: {e}")
        else:
            print(f"epoch {epoch+1:3d}/{args.epochs}  "
                  f"soft_cross={avg_cross:.4f} anchor={avg_anchor:.4f}")

    elapsed = time.time() - t0
    print(f"\nDone in {elapsed:.0f}s. "
          f"Best real cross = {best_cross} (init was {init_cross})")
    print(f"  best ckpt: {args.ckpt.with_name(args.ckpt.stem + '-best.pt')}")


if __name__ == "__main__":
    main()
