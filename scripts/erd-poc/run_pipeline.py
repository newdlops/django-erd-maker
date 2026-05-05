#!/usr/bin/env python3
"""Run C++ binary on each synthetic graph, save expert layouts.

More resilient than the bash version — Python doesn't get SIGHUP'd by parent
shell exit. Supports partitioning for parallel workers:
  worker 0/4: process indices 0, 4, 8, ...
  worker 1/4: process indices 1, 5, 9, ...
  ...
"""

import argparse
import os
import re
import subprocess
import sys
import time
from pathlib import Path


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--worker", type=int, default=0)
    p.add_argument("--num-workers", type=int, default=1)
    args = p.parse_args()

    root = Path(__file__).resolve().parents[2]
    os.chdir(root)
    bin_path = root / "bin/ogdf/darwin-arm64/django-erd-ogdf-layout"
    graph_dir = root / "data/erd-poc/graphs"
    layout_dir = root / "data/erd-poc/layouts"
    layout_dir.mkdir(parents=True, exist_ok=True)

    graphs = sorted([d for d in graph_dir.iterdir() if d.is_dir()])
    # Partition by index modulo worker count.
    graphs = [g for i, g in enumerate(graphs)
              if i % args.num_workers == args.worker]
    total = len(graphs)
    done = 0
    failed = 0
    skipped = 0
    started = time.time()
    tag = f"[w{args.worker}/{args.num_workers}]"

    for i, g in enumerate(graphs, 1):
        name = g.name
        out = layout_dir / f"{name}.json"
        err = layout_dir / f"{name}.stderr.log"

        if out.exists() and out.stat().st_size > 0:
            skipped += 1
            continue

        t0 = time.time()
        try:
            proc = subprocess.run(
                [
                    str(bin_path), "layout",
                    "--mode", "hierarchical_barycenter",
                    "--nodes-file", str(g / "nodes.tsv"),
                    "--edges-file", str(g / "edges.tsv"),
                    "--edge-routing", "straight",
                    "--cluster-graph", "1",
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=90,  # 90s per graph; skip slow ones
            )
        except subprocess.TimeoutExpired:
            failed += 1
            print(f"{tag} [{i}/{total}] {name}  TIMEOUT (>90s)", flush=True)
            continue
        elapsed = time.time() - t0
        if proc.returncode != 0:
            failed += 1
            err.write_bytes(proc.stderr)
            print(f"{tag} [{i}/{total}] {name}  FAILED ({elapsed:.0f}s)",
                  flush=True)
            continue
        out.write_bytes(proc.stdout)
        err.write_bytes(proc.stderr)
        m = re.search(rb"carrier-grouped (\d+)", proc.stderr)
        cross = int(m.group(1)) if m else "?"
        done += 1
        print(
            f"{tag} [{i}/{total}] {name}  {elapsed:.0f}s  cross={cross}",
            flush=True,
        )

    print(f"\n{tag} Done: {done} new, {skipped} skipped, {failed} failed, "
          f"total elapsed {time.time() - started:.0f}s", flush=True)


if __name__ == "__main__":
    sys.exit(main())
