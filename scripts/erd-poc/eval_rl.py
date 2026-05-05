#!/usr/bin/env python3
"""End-to-end evaluation of trained RL agent.

For each val graph:
  1. Load expert layout positions (from C++ pipeline run earlier)
  2. Run RL agent for N swap steps → optimized positions
  3. Denormalize positions back to real coordinates
  4. Save as positions.tsv
  5. Run C++ binary --positions-tsv → reported edgeCrossings (RL result)
  6. Compare to baseline (C++ pipeline alone)

Usage:
  python scripts/erd-poc/eval_rl.py --num 10
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
    "train_gnn", Path(__file__).parent / "train_gnn.py"
)
train_gnn = importlib.util.module_from_spec(spec)
spec.loader.exec_module(train_gnn)

spec2 = importlib.util.spec_from_file_location(
    "rl_agent", Path(__file__).parent / "rl_agent.py"
)
rl_agent = importlib.util.module_from_spec(spec2)
spec2.loader.exec_module(rl_agent)


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


def denormalize_positions(normalized_pos, pos_mean, pos_std):
    """Inverse of train_gnn.load_graph_pair normalization."""
    return normalized_pos * pos_std + pos_mean


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--graphs", type=Path, default=ROOT / "data/erd-poc/graphs")
    p.add_argument("--layouts", type=Path, default=ROOT / "data/erd-poc/layouts")
    p.add_argument("--ckpt", type=Path,
                   default=ROOT / "data/erd-poc/checkpoints/rl-policy.pt")
    p.add_argument("--device", default="mps")
    p.add_argument("--num", type=int, default=10)
    p.add_argument("--max-steps", type=int, default=40,
                   help="more steps at eval than train (no exploration)")
    p.add_argument("--K", type=int, default=30)
    p.add_argument("--max-graph-size", type=int, default=200)
    p.add_argument("--seed", type=int, default=42)
    args = p.parse_args()

    torch.manual_seed(args.seed)
    random.seed(args.seed)

    # Same val split as training.
    full_ds = train_gnn.ERDDataset(args.graphs, args.layouts)
    valid_indices = []
    for i in range(full_ds.len()):
        d = full_ds.get(i)
        if d is None: continue
        if d.x.shape[0] > args.max_graph_size: continue
        valid_indices.append(i)
    rng_split = random.Random(args.seed)
    rng_split.shuffle(valid_indices)
    # Skip first 50 (train), take next 20 (val) — same split as rl_agent.py.
    val_idx = valid_indices[50:50 + 20]
    eval_idx = val_idx[: args.num]
    print(f"Evaluating on {len(eval_idx)} val graphs")

    device = torch.device(args.device)
    policy = rl_agent.SwapPolicy().to(device)
    policy.load_state_dict(
        torch.load(args.ckpt, map_location=device, weights_only=True)
    )
    policy.eval()

    print(f"\n  {'name':40s}  {'baseline':>8s}  {'rl':>8s}  {'delta':>8s}  "
          f"{'%':>7s}")
    deltas = []
    for vi in eval_idx:
        data = full_ds.get(vi)
        g_dir = full_ds.graph_dirs[vi]
        baseline_path = args.layouts / f"{g_dir.name}.json"

        # Recover original (real-coord) positions from layout JSON.
        with baseline_path.open() as f:
            layout = json.load(f)
        id2idx = {nd["modelId"]: i for i, nd in enumerate(data.x)} \
            if False else None  # noqa: F841
        # Read from graph.json for the modelId order.
        with (g_dir / "graph.json").open() as f:
            g_json = json.load(f)
        ids_in_order = [nd["modelId"] for nd in g_json["nodes"]]
        layout_centers_by_id = {}
        for nd in layout["nodes"]:
            cx = nd["position"]["x"] + nd["size"]["width"] / 2.0
            cy = nd["position"]["y"] + nd["size"]["height"] / 2.0
            layout_centers_by_id[nd["modelId"]] = (cx, cy)
        real_centers = [layout_centers_by_id[mid] for mid in ids_in_order]
        real_centers_t = torch.tensor(real_centers, dtype=torch.float32)
        pos_mean = real_centers_t.mean(dim=0, keepdim=True)
        pos_std = real_centers_t.std() + 1e-3

        # Run RL agent (greedy).
        d_dev = data.to(device)
        init_pos = d_dev.y.clone()  # already normalized
        fwd_edges = d_dev.edge_index[:, ::2]
        with torch.no_grad():
            _, _, final_pos, init_c, end_c = rl_agent.run_episode(
                policy, d_dev, init_pos, fwd_edges,
                max_steps=args.max_steps, K=args.K,
                stochastic=False, device=device,
            )

        # Denormalize → real coords.
        real_final = denormalize_positions(
            final_pos.cpu(), pos_mean, pos_std
        )

        # Save TSV.
        with tempfile.NamedTemporaryFile(
            mode="w", suffix=".positions.tsv", delete=False
        ) as f:
            tsv_path = Path(f.name)
            for i, mid in enumerate(ids_in_order):
                cx = real_final[i, 0].item()
                cy = real_final[i, 1].item()
                f.write(f"{mid}\t{cx}\t{cy}\n")

        # Run C++ with positions override.
        baseline_cross = parse_baseline_cross(baseline_path)
        rl_layout, _ = run_cpp_with_positions(g_dir, tsv_path)
        tsv_path.unlink(missing_ok=True)
        if rl_layout is None:
            print(f"  {g_dir.name:40s}  C++ failed")
            continue
        rl_cross = rl_layout.get("engineMetadata", {}).get("edgeCrossings")
        if baseline_cross is None or rl_cross is None:
            print(f"  {g_dir.name:40s}  baseline={baseline_cross} rl={rl_cross}")
            continue
        delta = rl_cross - baseline_cross
        pct = (100.0 * delta / baseline_cross) if baseline_cross > 0 else 0.0
        deltas.append((g_dir.name, baseline_cross, rl_cross, delta, pct))
        sign = "+" if delta >= 0 else ""
        print(f"  {g_dir.name:40s}  {baseline_cross:8d}  {rl_cross:8d}  "
              f"{sign}{delta:7d}  {sign}{pct:6.1f}%  "
              f"(soft Δ {init_c - end_c:+.1f})")

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
