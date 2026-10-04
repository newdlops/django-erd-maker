#!/usr/bin/env python3
"""Run the production straight-scene scorer with research-safe defaults.

This is a small argv/env adapter for ``run_memory_bounded.py``.  It avoids a
fragile, very long shell command and captures the potentially large JSON
stream directly to disk inside the monitored process group.
"""

from __future__ import annotations

import argparse
import os
import subprocess
from pathlib import Path


DIRECT_ENV = {
    "DJERD_CG_SKIP_POSITIONING": "1",
    "DJERD_SKIP_CG_OPT": "1",
    "DJERD_MULTISTART_RUNS": "1",
    "DJERD_CARRIER_AWARE_COST": "0",
    "DJERD_HUB_CARRIER_CROSS_FINAL": "0",
    "DJERD_INHERITANCE_CARRIER_FINAL": "0",
    "DJERD_INTRA_CLUSTER_CARRIER_FINAL": "0",
    "DJERD_NO_CARRIER_CROSS": "1",
    "DJERD_NO_BENDS": "1",
    "DJERD_FORCE_STRAIGHT": "1",
    "DJERD_NO_KNOT_MIN": "1",
    "DJERD_NO_PD_KNOT": "1",
    "DJERD_XINGS_DETOUR": "0",
    "DJERD_VISUAL_KNOT": "0",
    "DJERD_ISOLATED_STASH": "0",
    "DJERD_FACE_RASTER": "0",
    "DJERD_FACE_UNTANGLE": "0",
    "DJERD_NO_BUNDLE_CLEAR": "1",
    "DJERD_NO_LEAF_UNTANGLE": "1",
    "DJERD_FINAL_ROUTE_SYNC_GAP": "0",
    "DJERD_RENDERED_CARRIER_METRICS_FINAL": "1",
    "DJERD_RENDERED_CARRIER_GEOMETRY_OPT_FINAL": "0",
    "DJERD_RENDERED_CARRIER_NODE_TARGET_FINAL": "0",
    "DJERD_RIGID_COMPACT_BBOX_FINAL": "0",
    "DJERD_ATTACH_ISOLATED_BY_NAME_FINAL": "0",
    "DJERD_ISOLATED_BBOX_COMPACT_FINAL": "0",
    "DJERD_SIDECAR_BBOX_COMPACT_FINAL": "0",
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--nodes", type=Path, required=True)
    parser.add_argument("--edges", type=Path, required=True)
    parser.add_argument("--positions", type=Path, required=True)
    parser.add_argument("--routes", type=Path)
    parser.add_argument("--stdout", type=Path, required=True)
    parser.add_argument("--stderr", type=Path, required=True)
    args = parser.parse_args()

    args.stdout.parent.mkdir(parents=True, exist_ok=True)
    args.stderr.parent.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment.update(DIRECT_ENV)
    command = [
        str(args.binary),
        "layout",
        "--mode", "fmmm",
        "--nodes-file", str(args.nodes),
        "--edges-file", str(args.edges),
        "--edge-routing", "straight",
        "--cluster-graph", "1",
        "--positions-tsv", str(args.positions),
    ]
    if args.routes is not None:
        command.extend(("--routes-tsv", str(args.routes)))
        # The ordinary route pipeline understands partial route overrides.
        # Restore both supplied centers and retained routes immediately before
        # final measurement so exploratory post-passes cannot desynchronize
        # the two geometries.
        environment["DJERD_RESTORE_LAYOUT_TSV_BEFORE_RETOUCH"] = "1"
    else:
        command.extend(("--rigid-positions", "1"))
    with args.stdout.open("wb") as stdout, args.stderr.open("wb") as stderr:
        return subprocess.run(
            command,
            stdout=stdout,
            stderr=stderr,
            env=environment,
            check=False,
        ).returncode


if __name__ == "__main__":
    raise SystemExit(main())
