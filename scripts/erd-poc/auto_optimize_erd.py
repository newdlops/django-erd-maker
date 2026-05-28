#!/usr/bin/env python3
"""End-to-end ERD layout optimization for any project.

The key insight from this codebase: a simple two-step post-pass
(Y-axis compression of node centers + C++ rerouter) cuts edgeCrossings
substantially on real ERDs. On the captain project (1241 nodes,
1476 edges) it goes 1724 → 987 (−43%) and bboxArea 17.24B → 3.06B
(−82%) without any ML model.

This is therefore the *real* generalization tool — it depends only on:
  - the ERD's nodes.tsv / edges.tsv
  - one cluster_graph layout pass for a starting position
  - a Y-scale factor (0.62 by default; tuned by the user's session)

ML is intentionally NOT used here: distill models trained on a corpus
of small synthetic graphs (50–500 nodes) failed to generalize to a
real 1000+ node ERD, because the synthetic distribution doesn't match
real-ERD topology (large hubs, dense leaf bundles, multi-app clusters).
The post-pass below works on any ERD because it operates on the
already-clustered layout, not on graph structure features.

Usage:
  python scripts/erd-poc/auto_optimize_erd.py \\
    --nodes-tsv data/erd-poc/graphs/real-main/nodes.tsv \\
    --edges-tsv data/erd-poc/graphs/real-main/edges.tsv \\
    --output /tmp/layout-optimized.json [--y-scale 0.62]
"""
import argparse
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def find_binary() -> Path:
    for plat in ("darwin-arm64", "darwin-x64", "linux-x64", "win32-x64"):
        p = ROOT / "bin" / "ogdf" / plat / "django-erd-ogdf-layout"
        if p.exists():
            return p
        p_exe = p.with_suffix(".exe")
        if p_exe.exists():
            return p_exe
    raise FileNotFoundError("django-erd-ogdf-layout binary not found")


def y_scale_positions_tsv(layout: dict, scale: float, out_path: Path):
    nodes = layout["nodes"]
    cy_sum = sum(n["position"]["y"] + n["size"]["height"] / 2 for n in nodes)
    cy = cy_sum / len(nodes)
    lines = []
    for n in nodes:
        cx_pos = n["position"]["x"] + n["size"]["width"] / 2
        center_y = n["position"]["y"] + n["size"]["height"] / 2
        new_y = cy + (center_y - cy) * scale
        lines.append(f'{n["modelId"]}\t{cx_pos:.3f}\t{new_y:.3f}')
    out_path.write_text("\n".join(lines) + "\n")


def run_binary(
    binary: Path, nodes_tsv: Path, edges_tsv: Path,
    positions_tsv: Path | None,
) -> dict:
    cmd = [
        str(binary), "layout",
        "--mode", "hierarchical_barycenter",
        "--nodes-file", str(nodes_tsv),
        "--edges-file", str(edges_tsv),
        "--edge-routing", "straight",
        "--cluster-graph", "1",
    ]
    if positions_tsv:
        cmd += ["--positions-tsv", str(positions_tsv)]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    if result.returncode != 0:
        raise RuntimeError(
            f"binary failed (exit {result.returncode}): "
            f"{result.stderr[:500]}"
        )
    return json.loads(result.stdout)


def metric_summary(layout: dict, label: str) -> str:
    md = layout.get("engineMetadata", {})
    return (
        f"{label:32s} cross={md.get('edgeCrossings'):>5}  "
        f"bbox={md.get('boundingBoxArea', 0) / 1e9:>6.2f}B  "
        f"overlaps={md.get('nodeOverlaps')}"
    )


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--nodes-tsv", type=Path, required=True)
    p.add_argument("--edges-tsv", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True,
                   help="optimized layout JSON")
    p.add_argument("--y-scale", type=float, default=0.62,
                   help="Y-axis compression factor (0.62 = 38%% squeeze)")
    p.add_argument("--baseline-out", type=Path, default=None,
                   help="optional path to also write the unoptimized baseline")
    p.add_argument("--keep-tmp", action="store_true")
    args = p.parse_args()

    binary = find_binary()
    work = Path(tempfile.mkdtemp(prefix="erd-opt-"))
    try:
        # Step 1: baseline cluster_graph layout.
        print("[1/2] cluster_graph baseline")
        baseline = run_binary(binary, args.nodes_tsv, args.edges_tsv, None)
        if args.baseline_out:
            args.baseline_out.parent.mkdir(parents=True, exist_ok=True)
            args.baseline_out.write_text(json.dumps(baseline))
        print("      " + metric_summary(baseline, "baseline"))

        # Step 2: Y-scale node centers + C++ rerouter on those positions.
        print(f"[2/2] Y-scale={args.y_scale} + reroute")
        seed = work / "y-scaled.tsv"
        y_scale_positions_tsv(baseline, args.y_scale, seed)
        optimized = run_binary(binary, args.nodes_tsv, args.edges_tsv, seed)

        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(optimized))
        print()
        print(metric_summary(baseline,  "baseline"))
        print(metric_summary(optimized, "optimized"))
        # Delta summary.
        b = baseline.get("engineMetadata", {})
        o = optimized.get("engineMetadata", {})
        bc = b.get("edgeCrossings", 0) or 1
        ba = b.get("boundingBoxArea", 0) or 1
        oc = o.get("edgeCrossings", 0)
        oa = o.get("boundingBoxArea", 0)
        print(f"\ndelta:  cross {bc} → {oc} ({(oc - bc) / bc * 100:+.1f}%)  "
              f"bbox {ba / 1e9:.2f}B → {oa / 1e9:.2f}B "
              f"({(oa - ba) / ba * 100:+.1f}%)")
        print(f"\nWrote {args.output}")
    finally:
        if not args.keep_tmp:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
