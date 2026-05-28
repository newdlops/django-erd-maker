#!/usr/bin/env python3
"""Generate diverse captain-scale layouts post-§13 for diffusion training.

Approach:
  1. For each seed: sample ML positions via diffusion (varied guidance)
  2. Run §13 full post-pass on each sample → "good" captain layout
  3. Save layout JSON to corpus dir

Each invocation generates one layout (~135s: 5s ML sample + 130s §13).
Run in parallel via multiple invocations.

Usage:
  python scripts/erd-poc/generate_captain_corpus.py \\
    --start-seed 0 --count 50 --out data/erd-poc/captain-corpus
"""
import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

import importlib.util
sample_spec = importlib.util.spec_from_file_location(
    "sample_diffusion", Path(__file__).parent / "sample_diffusion.py"
)
sd = importlib.util.module_from_spec(sample_spec); sample_spec.loader.exec_module(sd)
import torch
tdf = sd.tdf

ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / "bin/ogdf/darwin-arm64/django-erd-ogdf-layout"
NODES_TSV = ROOT / "data/erd-poc/graphs/real-main/nodes.tsv"
EDGES_TSV = ROOT / "data/erd-poc/graphs/real-main/edges.tsv"
LAYOUT = ROOT / "data/erd-poc/layouts/real-main.json"
EXPERT = ROOT / "data/erd-poc/expert-strong/real-main.json"


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--ckpt", type=Path,
                   default=ROOT / "data/erd-poc/checkpoints/v25-diff-captain.pt")
    p.add_argument("--start-seed", type=int, default=0)
    p.add_argument("--count", type=int, default=10)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--T", type=int, default=200)
    p.add_argument("--hidden", type=int, default=256)
    p.add_argument("--layers", type=int, default=6)
    args = p.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)

    layout = json.loads(LAYOUT.read_text())
    expert = json.loads(EXPERT.read_text())
    data = tdf.build_graph_data(layout, expert_layout=expert)

    device = torch.device("cpu")
    diff = tdf.get_diffusion_constants(args.T, device=device)
    model = tdf.GraphDenoiser(node_feat_dim=10, hidden=args.hidden,
                                num_layers=args.layers,
                                edge_feat_dim=6).to(device)
    state = torch.load(args.ckpt, map_location=device, weights_only=True)
    model.load_state_dict(state)
    print(f"loaded {args.ckpt}")

    pos_mean = data["pos_mean"]
    pos_std = data["pos_std"]

    for k in range(args.count):
        seed = args.start_seed + k
        out_path = args.out / f"capt-{seed:04d}.json"
        if out_path.exists():
            print(f"  skip {out_path.name} (exists)")
            continue
        t0 = time.time()
        # Vary guidance per seed for diversity:
        # cross 0.3-0.7, bbox 0-500
        cross_w = 0.3 + (seed % 5) * 0.1
        bbox_w = (seed % 3) * 200
        x_0_norm = sd.sample(
            model, data, diff, args.T, device,
            guidance_strength=cross_w,
            bbox_guidance=bbox_w,
            guidance_start_t=int(args.T * 0.5),
            pos_mean=pos_mean, pos_std=pos_std, seed=seed,
        )
        x_real = x_0_norm.cpu() * pos_std + pos_mean
        # Write tsv + reroute through §13
        tsv = args.out / f"_capt-{seed:04d}.tsv"
        with tsv.open("w") as f:
            for i, nd in enumerate(layout["nodes"]):
                f.write(f"{nd['modelId']}\t{x_real[i,0].item():.3f}"
                        f"\t{x_real[i,1].item():.3f}\n")
        r = subprocess.run(
            [str(BINARY), "layout",
             "--mode", "hierarchical_barycenter",
             "--nodes-file", str(NODES_TSV),
             "--edges-file", str(EDGES_TSV),
             "--edge-routing", "straight",
             "--cluster-graph", "1",
             "--positions-tsv", str(tsv)],
            capture_output=True, text=True,
        )
        tsv.unlink(missing_ok=True)
        if r.returncode != 0:
            print(f"  seed {seed}: §13 failed, skip")
            continue
        d = json.loads(r.stdout)
        em = d["engineMetadata"]
        cross = em.get("edgeCrossings")
        out_path.write_text(r.stdout)
        elapsed = time.time() - t0
        print(f"  seed {seed:4d} (cw={cross_w:.1f} bw={bbox_w}): "
              f"cross={cross} ({elapsed:.0f}s)",
              flush=True)


if __name__ == "__main__":
    main()
