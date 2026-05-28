#!/usr/bin/env python3
"""Generate captain layouts with FMMM seed diversity for v37 training.

Existing captain-corpus-v3x/ varies diffusion seed (cw/bw guidance) but
always starts from the same cluster_graph topology. multistart proved
that different FMMM seeds → different cluster_graph local optima
([[multistart-fmmm-success]]). This script captures that diversity as
training data.

Approach:
  For each FMMM seed, run cluster_graph directly (no positions-tsv,
  no diffusion). Each run ~3 min on captain. Output one JSON per seed
  in captain-corpus-multistart/, file naming capt-ms<seed>.json so
  build_v35 globbing patterns work as-is.

The seed list mixes:
  * proven winners (43)
  * neighbours (44, 45, 50, 51)
  * a far seed_base area (100, 101)
  * default fallback (100 again — for sanity)
That way the training set gets both "good" and "mediocre" topologies
which is what teaches the family-prior model what each starting state
looks like.

Usage:
  python scripts/erd-poc/build_multistart_corpus.py \\
    --seeds 43,44,45,50,51,100,101,200 \\
    --out data/erd-poc/captain-corpus-multistart
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / "bin/ogdf/darwin-arm64/django-erd-ogdf-layout"


def run_one(seed: int, out_dir: Path, nodes_tsv: Path, edges_tsv: Path) -> tuple[bool, dict]:
    """Run cluster_graph at one FMMM seed. Returns (ok, summary)."""
    out_path = out_dir / f"capt-ms{seed:04d}.json"
    if out_path.exists():
        try:
            d = json.loads(out_path.read_text())
            em = d.get("engineMetadata", {}) or {}
            return True, {
                "seed": seed, "skipped": True,
                "edgeCrossings": em.get("edgeCrossings"),
                "visualCrossings": em.get("visualCrossings"),
            }
        except (json.JSONDecodeError, OSError):
            out_path.unlink(missing_ok=True)

    env = dict(os.environ)
    env["DJERD_FMMM_SEED"] = str(seed)
    # Keep multistart itself off — we drive seed diversity from this
    # outer loop. Otherwise each inner run would spawn 4 sub-runs and
    # we'd lose the per-seed correspondence.
    env["DJERD_MULTISTART_RUNS"] = "1"

    t0 = time.time()
    r = subprocess.run(
        [
            str(BINARY), "layout",
            "--mode", "hierarchical_barycenter",
            "--nodes-file", str(nodes_tsv),
            "--edges-file", str(edges_tsv),
            "--edge-routing", "straight",
            "--cluster-graph", "1",
        ],
        capture_output=True, text=True, env=env,
    )
    elapsed = time.time() - t0

    if r.returncode != 0:
        return False, {
            "seed": seed, "elapsed_s": elapsed,
            "stderr_tail": r.stderr[-400:] if r.stderr else "",
        }

    try:
        d = json.loads(r.stdout)
    except json.JSONDecodeError as exc:
        return False, {
            "seed": seed, "elapsed_s": elapsed,
            "decode_error": str(exc),
        }

    out_path.write_text(r.stdout)
    em = d.get("engineMetadata", {}) or {}
    return True, {
        "seed": seed,
        "elapsed_s": elapsed,
        "edgeCrossings": em.get("edgeCrossings"),
        "visualCrossings": em.get("visualCrossings"),
        "bundleEdgeIntersections": em.get("bundleEdgeIntersections"),
        "quality": em.get("quality"),
        "nodes": len(d.get("nodes", [])),
    }


def main():
    p = argparse.ArgumentParser()
    p.add_argument(
        "--seeds",
        default="43,44,45,50,51,100,101,200,300,500",
        help="Comma-separated FMMM seed list",
    )
    p.add_argument(
        "--out",
        type=Path,
        default=ROOT / "data/erd-poc/captain-corpus-multistart",
    )
    p.add_argument(
        "--nodes-tsv",
        type=Path,
        default=ROOT / "data/erd-poc/graphs/real-main/nodes.tsv",
        help="nodes TSV (default real-main 1250-node bundle-less view)",
    )
    p.add_argument(
        "--edges-tsv",
        type=Path,
        default=ROOT / "data/erd-poc/graphs/real-main/edges.tsv",
    )
    p.add_argument("--manifest", type=Path,
                   help="Optional path to write a JSON summary "
                        "(default: <out>/manifest.json)")
    args = p.parse_args()

    seeds = [int(s) for s in args.seeds.split(",") if s.strip()]
    args.out.mkdir(parents=True, exist_ok=True)
    manifest_path = args.manifest or (args.out / "manifest.json")

    if not BINARY.exists():
        sys.exit(f"binary not found: {BINARY}")
    if not args.nodes_tsv.exists():
        sys.exit(f"nodes.tsv not found: {args.nodes_tsv}")
    if not args.edges_tsv.exists():
        sys.exit(f"edges.tsv not found: {args.edges_tsv}")

    print(f"Building multistart corpus into {args.out}")
    print(f"  seeds: {seeds}")
    print(f"  binary: {BINARY}")
    print(f"  graph:  {args.nodes_tsv.parent.name} "
          f"({sum(1 for _ in args.nodes_tsv.open())} node rows)")
    print()

    summaries: list[dict] = []
    fails = 0
    overall_start = time.time()
    for seed in seeds:
        ok, summary = run_one(seed, args.out, args.nodes_tsv, args.edges_tsv)
        if not ok:
            fails += 1
        summaries.append(summary)
        tag = "SKIP" if summary.get("skipped") else ("OK  " if ok else "FAIL")
        elapsed = summary.get("elapsed_s")
        elapsed_str = f"{elapsed:>5.1f}s" if elapsed is not None else "  ---"
        print(
            f"  [{tag}] seed={seed:>4} {elapsed_str} "
            f"vCross={summary.get('visualCrossings')} "
            f"bundleX={summary.get('bundleEdgeIntersections')} "
            f"quality={summary.get('quality')}",
            flush=True,
        )

    overall = time.time() - overall_start
    print()
    print(
        f"Done. {len(seeds) - fails}/{len(seeds)} ok, "
        f"{fails} failed, total {overall / 60:.1f} min."
    )

    manifest_path.write_text(json.dumps({
        "binary": str(BINARY),
        "nodes_tsv": str(args.nodes_tsv),
        "edges_tsv": str(args.edges_tsv),
        "seeds": seeds,
        "summaries": summaries,
        "total_elapsed_s": overall,
    }, indent=2))
    print(f"Manifest: {manifest_path}")


if __name__ == "__main__":
    main()
