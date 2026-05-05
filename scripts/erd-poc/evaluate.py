#!/usr/bin/env python3
"""Evaluate trained GNN on held-out graphs:
  - GNN predicts positions
  - Save as positions.tsv
  - Run C++ binary with --positions-tsv to re-route + report edgeCrossings
  - Compare to baseline (no positions override)

Usage:
  python scripts/erd-poc/evaluate.py
  python scripts/erd-poc/evaluate.py --graph synth-0042-n123-e145
"""

import argparse
import json
import re
import subprocess
import tempfile
from pathlib import Path

import torch

# Re-import the model + dataset from train_gnn.
import importlib.util
spec = importlib.util.spec_from_file_location(
    "train_gnn", Path(__file__).parent / "train_gnn.py"
)
train_gnn = importlib.util.module_from_spec(spec)
spec.loader.exec_module(train_gnn)


def predict_positions(model, data, device):
    """Returns list of (modelId, cx, cy) un-normalized."""
    model.eval()
    with torch.no_grad():
        pred = model(data.to(device))
    pred = pred.cpu().numpy()
    return pred


def parse_edge_crossings(stderr_text: str) -> int | None:
    """Extract `carrier-grouped <N>` from C++ stderr."""
    m = re.search(r"carrier-grouped (\d+)", stderr_text)
    return int(m.group(1)) if m else None


def parse_baseline_layout_crossings(layout_path: Path) -> int | None:
    """Read engineMetadata.edgeCrossings from C++ output JSON."""
    if not layout_path.exists():
        return None
    with layout_path.open() as f:
        layout = json.load(f)
    return layout.get("engineMetadata", {}).get("edgeCrossings")


def write_positions_tsv(graph_dir: Path, pred, target_path: Path):
    """Write modelId\tcx\tcy lines based on prediction.

    pred is normalized [0-mean, /std]; we need to scale back to a reasonable
    coordinate range. Use a fixed scale (200 units per std-unit).
    """
    with (graph_dir / "graph.json").open() as f:
        g = json.load(f)
    SCALE = 5000.0  # rough coordinate range for typical ERD
    with target_path.open("w") as f:
        for i, nd in enumerate(g["nodes"]):
            cx = float(pred[i, 0]) * SCALE
            cy = float(pred[i, 1]) * SCALE
            f.write(f"{nd['modelId']}\t{cx}\t{cy}\n")


def run_cpp_with_positions(graph_dir: Path, positions_tsv: Path):
    """Invoke C++ binary with --positions-tsv. Returns (layout_dict, stderr_str)."""
    bin_path = Path("bin/ogdf/darwin-arm64/django-erd-ogdf-layout").resolve()
    proc = subprocess.run(
        [
            str(bin_path), "layout",
            "--mode", "hierarchical_barycenter",
            "--nodes-file", str(graph_dir / "nodes.tsv"),
            "--edges-file", str(graph_dir / "edges.tsv"),
            "--edge-routing", "straight",
            "--cluster-graph", "1",
            "--positions-tsv", str(positions_tsv),
        ],
        capture_output=True,
        text=True,
        timeout=600,
    )
    if proc.returncode != 0:
        return None, proc.stderr
    try:
        return json.loads(proc.stdout), proc.stderr
    except json.JSONDecodeError:
        return None, proc.stderr


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--graphs", type=Path, default=Path("data/erd-poc/graphs"))
    p.add_argument("--layouts", type=Path, default=Path("data/erd-poc/layouts"))
    p.add_argument("--ckpt", type=Path,
                   default=Path("data/erd-poc/checkpoints/best.pt"))
    p.add_argument("--tag", type=str, default="",
                   help="optional label to print in summary")
    p.add_argument("--device", default="mps")
    p.add_argument("--graph", default=None,
                   help="evaluate on single graph (default: all val graphs)")
    p.add_argument("--limit", type=int, default=20,
                   help="max graphs to evaluate")
    p.add_argument("--seed", type=int, default=0,
                   help="match training seed for val split")
    p.add_argument("--val-frac", type=float, default=0.2)
    args = p.parse_args()

    device = torch.device(args.device)
    model = train_gnn.ERDLayoutGAT()
    model.load_state_dict(torch.load(args.ckpt, map_location=device,
                                      weights_only=True))
    model.to(device)
    print(f"Loaded model from {args.ckpt}")

    full_ds = train_gnn.ERDDataset(args.graphs, args.layouts)
    valid_indices = [
        i for i in range(full_ds.len()) if full_ds.get(i) is not None
    ]
    if args.graph is not None:
        # Single graph by name.
        target_idx = None
        for i in valid_indices:
            if full_ds.graph_dirs[i].name == args.graph:
                target_idx = i; break
        if target_idx is None:
            print(f"Graph {args.graph} not found")
            return
        eval_indices = [target_idx]
    else:
        # Match the training seed's val split.
        rng = torch.Generator().manual_seed(args.seed)
        perm = torch.randperm(len(valid_indices), generator=rng).tolist()
        valid_indices = [valid_indices[i] for i in perm]
        n_val = max(1, int(len(valid_indices) * args.val_frac))
        eval_indices = valid_indices[:n_val][: args.limit]

    print(f"Evaluating on {len(eval_indices)} graphs")
    print(f"  {'name':40s}  {'baseline':>10s}  {'gnn':>10s}  {'delta':>8s}  "
          f"{'%':>6s}")

    deltas = []
    for i in eval_indices:
        data = full_ds.get(i)
        g_dir = full_ds.graph_dirs[i]
        name = g_dir.name
        baseline_path = args.layouts / f"{name}.json"
        baseline_cross = parse_baseline_layout_crossings(baseline_path)

        pred = predict_positions(model, data, device)
        with tempfile.NamedTemporaryFile(
            mode="w", suffix=".positions.tsv", delete=False
        ) as f:
            tsv_path = Path(f.name)
        write_positions_tsv(g_dir, pred, tsv_path)
        layout, stderr = run_cpp_with_positions(g_dir, tsv_path)
        tsv_path.unlink(missing_ok=True)

        if layout is None:
            print(f"  {name:40s}  C++ failed")
            continue
        gnn_cross = layout.get("engineMetadata", {}).get("edgeCrossings")
        if baseline_cross is None or gnn_cross is None:
            print(f"  {name:40s}  baseline={baseline_cross} gnn={gnn_cross}")
            continue
        delta = gnn_cross - baseline_cross
        pct = (100.0 * delta / baseline_cross) if baseline_cross > 0 else 0.0
        deltas.append((name, baseline_cross, gnn_cross, delta, pct))
        sign = "+" if delta >= 0 else ""
        print(
            f"  {name:40s}  {baseline_cross:10d}  {gnn_cross:10d}  "
            f"{sign}{delta:7d}  {sign}{pct:5.1f}%"
        )

    if deltas:
        avg_delta = sum(d[3] for d in deltas) / len(deltas)
        avg_pct = sum(d[4] for d in deltas) / len(deltas)
        better = sum(1 for d in deltas if d[3] < 0)
        worse = sum(1 for d in deltas if d[3] > 0)
        print(f"\nSummary: {len(deltas)} graphs")
        print(f"  better than heuristic:  {better}")
        print(f"  worse than heuristic:   {worse}")
        print(f"  avg delta:  {avg_delta:+.1f}  ({avg_pct:+.1f}%)")


if __name__ == "__main__":
    main()
