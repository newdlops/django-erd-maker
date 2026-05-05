#!/usr/bin/env python3
"""Run ml-layout-polish.py on each baseline layout to produce polished targets.

Inputs:  data/erd-poc/layouts/<name>.json (C++ pipeline outputs)
Outputs: data/erd-poc/polished/<name>.json

Supports partitioning for parallel workers (--worker, --num-workers).
"""

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--worker", type=int, default=0)
    p.add_argument("--num-workers", type=int, default=1)
    p.add_argument("--iters", type=int, default=300)
    p.add_argument("--restarts", type=int, default=1)
    args = p.parse_args()

    root = Path(__file__).resolve().parents[2]
    os.chdir(root)
    layout_dir = root / "data/erd-poc/layouts"
    polish_dir = root / "data/erd-poc/polished"
    polish_dir.mkdir(parents=True, exist_ok=True)
    polish_script = root / "scripts/ml-layout-polish.py"
    py = root / ".venv-ml/bin/python"

    layouts = sorted(layout_dir.glob("*.json"))
    layouts = [l for i, l in enumerate(layouts)
               if i % args.num_workers == args.worker]
    total = len(layouts)
    done = 0
    skipped = 0
    failed = 0
    started = time.time()
    tag = f"[w{args.worker}/{args.num_workers}]"

    for i, lp in enumerate(layouts, 1):
        out_path = polish_dir / lp.name
        if out_path.exists() and out_path.stat().st_size > 0:
            skipped += 1
            continue
        t0 = time.time()
        try:
            proc = subprocess.run(
                [str(py), str(polish_script),
                 "--input", str(lp),
                 "--output", str(out_path),
                 "--iters", str(args.iters),
                 "--lr", "80",
                 "--sharpness", "10",
                 "--w-anchor", "1e-6",
                 "--num-restarts", str(args.restarts)],
                capture_output=True, text=True, timeout=300,
            )
        except subprocess.TimeoutExpired:
            failed += 1
            print(f"{tag} [{i}/{total}] {lp.name}  TIMEOUT", flush=True)
            continue
        elapsed = time.time() - t0
        if proc.returncode != 0:
            failed += 1
            print(f"{tag} [{i}/{total}] {lp.name}  FAILED ({elapsed:.0f}s)",
                  flush=True)
            (polish_dir / f"{lp.name}.stderr.log").write_text(proc.stderr)
            continue
        done += 1
        print(f"{tag} [{i}/{total}] {lp.name}  {elapsed:.0f}s",
              flush=True)

    print(f"\n{tag} Done: {done} new, {skipped} skipped, {failed} failed, "
          f"total elapsed {time.time() - started:.0f}s", flush=True)


if __name__ == "__main__":
    sys.exit(main())
