#!/usr/bin/env python3
"""End-to-end eval of distillation model.

For each val graph:
  1. Load baseline layout (C++ pipeline output)
  2. GNN distill → predicted polished positions
  3. Save positions.tsv
  4. Run C++ binary --positions-tsv → reported edgeCrossings
  5. Compare to: heuristic baseline, ml-polish, distill-GNN
"""

import argparse
import importlib.util
import json
import re
import subprocess
import tempfile
import random
import time
from pathlib import Path

import torch

ROOT = Path(__file__).resolve().parents[2]

spec = importlib.util.spec_from_file_location(
    "train_distill", Path(__file__).parent / "train_distill.py"
)
train_distill = importlib.util.module_from_spec(spec)
spec.loader.exec_module(train_distill)


def parse_baseline_cross(layout_path: Path):
    if not layout_path.exists():
        return None
    with layout_path.open() as f:
        layout = json.load(f)
    return layout.get("engineMetadata", {}).get("edgeCrossings")


def run_cpp_with_positions(graph_dir: Path, positions_tsv: Path):
    bin_path = ROOT / "bin/ogdf/darwin-arm64/django-erd-ogdf-layout"
    proc = subprocess.run(
        [str(bin_path), "layout",
         "--mode", "hierarchical_barycenter",
         "--nodes-file", str(graph_dir / "nodes.tsv"),
         "--edges-file", str(graph_dir / "edges.tsv"),
         "--edge-routing", "straight",
         "--cluster-graph", "1",
         "--positions-tsv", str(positions_tsv)],
        capture_output=True, text=True, timeout=120,
    )
    if proc.returncode != 0:
        return None, proc.stderr
    try:
        return json.loads(proc.stdout), proc.stderr
    except json.JSONDecodeError:
        return None, proc.stderr


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--graphs", type=Path, default=ROOT / "data/erd-poc/graphs")
    p.add_argument("--baseline", type=Path,
                   default=ROOT / "data/erd-poc/layouts")
    p.add_argument("--polished", type=Path,
                   default=ROOT / "data/erd-poc/polished")
    p.add_argument("--ckpt", type=Path,
                   default=ROOT / "data/erd-poc/checkpoints/distill.pt")
    p.add_argument("--device", default="mps")
    p.add_argument("--num", type=int, default=15)
    p.add_argument("--val-frac", type=float, default=0.1)
    p.add_argument("--seed", type=int, default=0)
    args = p.parse_args()

    torch.manual_seed(args.seed)
    random.seed(args.seed)

    ds = train_distill.ERDDistillDataset(
        args.graphs, args.baseline, args.polished
    )
    valid_indices = [i for i in range(ds.len()) if ds.get(i) is not None]
    rng_split = torch.Generator().manual_seed(args.seed)
    perm = torch.randperm(len(valid_indices), generator=rng_split).tolist()
    valid_indices = [valid_indices[i] for i in perm]
    n_val = max(1, int(len(valid_indices) * args.val_frac))
    val_idx = valid_indices[:n_val][: args.num]
    print(f"Evaluating on {len(val_idx)} val graphs")

    device = torch.device(args.device)
    model = train_distill.DistillGAT()
    model.load_state_dict(
        torch.load(args.ckpt, map_location=device, weights_only=True)
    )
    model.to(device).eval()

    print(f"\n  {'name':40s}  "
          f"{'baseline':>9s}  {'polish':>9s}  {'distill':>9s}  "
          f"{'Δ-vs-base':>10s}  {'Δ-vs-pol':>10s}")
    deltas_vs_base = []
    deltas_vs_pol = []
    for vi in val_idx:
        d = ds.get(vi)
        g_dir = ds.graph_dirs[vi]
        baseline_path = args.baseline / f"{g_dir.name}.json"
        polished_path = args.polished / f"{g_dir.name}.json"

        # Baseline reported edgeCrossings (no override).
        bc = parse_baseline_cross(baseline_path)
        # Polish reported (run polished JSON through C++ if available).
        pc = parse_baseline_cross(polished_path)

        # Recover real-coord scale.
        with (g_dir / "graph.json").open() as f:
            g = json.load(f)
        ids = [nd["modelId"] for nd in g["nodes"]]
        with baseline_path.open() as f:
            blayout = json.load(f)
        layout_centers_by_id = {}
        for nd in blayout["nodes"]:
            cx = nd["position"]["x"] + nd["size"]["width"] / 2.0
            cy = nd["position"]["y"] + nd["size"]["height"] / 2.0
            layout_centers_by_id[nd["modelId"]] = (cx, cy)
        real_centers = torch.tensor(
            [layout_centers_by_id[mid] for mid in ids], dtype=torch.float32
        )
        pos_mean = real_centers.mean(dim=0, keepdim=True)
        pos_std = real_centers.std() + 1e-3

        # Run distill model.
        d_dev = d.to(device)
        with torch.no_grad():
            pred = model(d_dev)
        pred_real = pred.cpu() * pos_std + pos_mean

        # Save TSV.
        with tempfile.NamedTemporaryFile(
            mode="w", suffix=".positions.tsv", delete=False
        ) as f:
            tsv_path = Path(f.name)
            for i, mid in enumerate(ids):
                cx = pred_real[i, 0].item()
                cy = pred_real[i, 1].item()
                f.write(f"{mid}\t{cx}\t{cy}\n")
        layout_distill, _ = run_cpp_with_positions(g_dir, tsv_path)
        tsv_path.unlink(missing_ok=True)
        if layout_distill is None:
            print(f"  {g_dir.name:40s}  C++ FAILED")
            continue
        dc = layout_distill.get("engineMetadata", {}).get("edgeCrossings")
        if bc is None or pc is None or dc is None:
            print(f"  {g_dir.name:40s}  bc={bc} pc={pc} dc={dc}")
            continue
        delta_vs_base = dc - bc
        delta_vs_pol = dc - pc
        sign_b = "+" if delta_vs_base >= 0 else ""
        sign_p = "+" if delta_vs_pol >= 0 else ""
        print(f"  {g_dir.name:40s}  "
              f"{bc:9d}  {pc:9d}  {dc:9d}  "
              f"{sign_b}{delta_vs_base:9d}  {sign_p}{delta_vs_pol:9d}")
        deltas_vs_base.append(delta_vs_base)
        deltas_vs_pol.append(delta_vs_pol)

    if deltas_vs_base:
        nB = len(deltas_vs_base)
        better_base = sum(1 for d in deltas_vs_base if d < 0)
        better_pol = sum(1 for d in deltas_vs_pol if d < 0)
        print(f"\nSummary: {nB} graphs")
        print(f"  vs heuristic baseline: {better_base} better, "
              f"avg {sum(deltas_vs_base)/nB:+.1f}")
        print(f"  vs ml-polish:          {better_pol} better, "
              f"avg {sum(deltas_vs_pol)/nB:+.1f}")


if __name__ == "__main__":
    main()
