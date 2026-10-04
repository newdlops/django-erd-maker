#!/usr/bin/env python3
"""Benchmark OGDF node placements under the canonical direct-edge contract."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / "bin/ogdf/darwin-arm64/django-erd-ogdf-layout"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nodes", type=Path, required=True)
    parser.add_argument("--edges", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument(
        "--modes",
        default=(
            "hierarchical_barycenter,hierarchical_sifting,"
            "hierarchical_global_sifting,hierarchical_greedy_insert,"
            "hierarchical_greedy_switch,hierarchical_grid_sifting,"
            "hierarchical_split,circular,fmmm,fast_multipole,"
            "fast_multipole_multilevel,stress_minimization,pivot_mds,"
            "davidson_harel,planarization,planarization_grid,ortho,"
            "upward_layer_based,upward_planarization,visibility"
        ),
    )
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    modes = [mode.strip() for mode in args.modes.split(",") if mode.strip()]
    env = {
        **os.environ,
        "DJERD_CARRIER_AWARE_COST": "0",
        "DJERD_HUB_CARRIER_CROSS_FINAL": "0",
        "DJERD_INHERITANCE_CARRIER_FINAL": "0",
        "DJERD_INTRA_CLUSTER_CARRIER_FINAL": "0",
        "DJERD_NO_CARRIER_CROSS": "1",
        "DJERD_NO_BENDS": "1",
        "DJERD_FORCE_STRAIGHT": "1",
        "DJERD_RENDERED_CARRIER_METRICS_FINAL": "1",
        "DJERD_RENDERED_CARRIER_GEOMETRY_OPT_FINAL": "0",
        "DJERD_RENDERED_CARRIER_NODE_TARGET_FINAL": "0",
        "DJERD_DISABLE_WALL_CLOCK_BUDGETS": "1",
    }
    best: tuple[int, str, dict] | None = None
    for mode in modes:
        started = time.time()
        completed = subprocess.run(
            [
                str(BINARY),
                "layout",
                "--mode", mode,
                "--nodes-file", str(args.nodes),
                "--edges-file", str(args.edges),
                "--edge-routing", "straight",
                "--cluster-graph", "0",
            ],
            check=False,
            capture_output=True,
            text=True,
            env=env,
        )
        if completed.returncode != 0:
            detail = completed.stderr.strip().splitlines()[-1:] or ["unknown error"]
            print(f"mode={mode} failed code={completed.returncode} detail={detail[0]}", flush=True)
            continue
        try:
            payload = json.loads(completed.stdout)
        except json.JSONDecodeError as error:
            print(f"mode={mode} invalid-json error={error}", flush=True)
            continue
        metadata = payload.get("engineMetadata") or {}
        crossings = int(metadata.get("edgeCrossings") or 0)
        edge_node = int(metadata.get("edgeNodeIntersections") or 0)
        visual = int(metadata.get("visualCrossings") or crossings + edge_node)
        output_path = args.out_dir / f"{mode}.json"
        output_path.write_text(json.dumps(payload), encoding="utf-8")
        print(
            f"mode={mode} cross={crossings} edgeNode={edge_node} visual={visual} "
            f"overlap={metadata.get('nodeOverlaps')} elapsed={time.time() - started:.1f}s",
            flush=True,
        )
        candidate = (visual, mode, payload)
        if best is None or candidate[0] < best[0]:
            best = candidate
    if best is None:
        raise SystemExit("no native mode completed")
    best_path = args.out_dir / "best.json"
    best_path.write_text(json.dumps(best[2]), encoding="utf-8")
    print(f"best mode={best[1]} visual={best[0]} out={best_path}", flush=True)


if __name__ == "__main__":
    main()
