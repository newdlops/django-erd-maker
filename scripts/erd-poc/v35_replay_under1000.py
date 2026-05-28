#!/usr/bin/env python3
"""Replay the v34/v35 under-1000 Captain ERD path.

Default quick mode starts from the archived v34 repair positions and reruns
the final affine scale + C++ skip-CG postpass that produced edgeCrossings=999.

Full mode replays the long exact-search chain from groupanchor2:
  groupanchor2 -> carrier1 -> bundleorbit1 -> C++ fixed point -> repair1
  -> affine scale -> final C++ postpass.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / "bin/ogdf/darwin-arm64/django-erd-ogdf-layout"
V34 = ROOT / "scripts/erd-poc/v34_move_search.py"
DEFAULT_ARTIFACT_DIR = ROOT / "data/erd-poc/v34-under1000"
DEFAULT_OUT_DIR = Path("/tmp/v35-under1000-replay")
NODES_TSV = ROOT / "data/erd-poc/graphs/real-main/nodes.tsv"
EDGES_TSV = ROOT / "data/erd-poc/graphs/real-main/edges.tsv"
LAYOUT = ROOT / "data/erd-poc/layouts/real-main.json"


def python_exe() -> str:
    venv_python = ROOT / ".venv-ml/bin/python"
    return str(venv_python if venv_python.exists() else Path(sys.executable))


def metrics(path: Path) -> dict:
    data = json.loads(path.read_text())
    em = data.get("engineMetadata") or {}
    bbox = em.get("boundingBoxArea")
    return {
        "edgeCrossings": em.get("edgeCrossings"),
        "nodeOverlaps": em.get("nodeOverlaps"),
        "bundleNodeOverlaps": em.get("bundleNodeOverlaps"),
        "bboxB": bbox / 1e9 if isinstance(bbox, (int, float)) else None,
        "visualCrossings": em.get("visualCrossings"),
        "edgeNodeIntersections": em.get("edgeNodeIntersections"),
        "bundleEdgeIntersections": em.get("bundleEdgeIntersections"),
    }


def print_metrics(label: str, path: Path) -> None:
    m = metrics(path)
    bbox = m["bboxB"]
    bbox_text = f"{bbox:.3f}B" if bbox is not None else "?"
    print(
        f"{label}: edgeCrossings={m['edgeCrossings']} "
        f"nodeOverlaps={m['nodeOverlaps']} "
        f"bundleNodeOverlaps={m['bundleNodeOverlaps']} "
        f"bbox={bbox_text} visualCrossings={m['visualCrossings']}",
        flush=True,
    )


def run_checked(
    cmd: list[str],
    *,
    env: dict[str, str] | None = None,
    stdout_path: Path | None = None,
    stderr_path: Path | None = None,
    timeout: int,
) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(
        cmd,
        cwd=ROOT,
        env=env,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    if stdout_path is not None:
        stdout_path.write_text(proc.stdout)
    if stderr_path is not None:
        stderr_path.write_text(proc.stderr)
    if proc.returncode != 0:
        raise RuntimeError(
            f"command failed ({proc.returncode}): {' '.join(cmd)}\n"
            f"{proc.stderr[-1200:]}"
        )
    return proc


def run_postpass(positions_tsv: Path, out_json: Path, timeout: int) -> None:
    env = os.environ.copy()
    env["DJERD_SKIP_CG_OPT"] = "1"
    env["DJERD_KNOT_2NDPASS"] = "1"
    run_checked(
        [
            str(BINARY),
            "layout",
            "--mode",
            "hierarchical_barycenter",
            "--nodes-file",
            str(NODES_TSV),
            "--edges-file",
            str(EDGES_TSV),
            "--edge-routing",
            "straight",
            "--cluster-graph",
            "1",
            "--positions-tsv",
            str(positions_tsv),
        ],
        env=env,
        stdout_path=out_json,
        stderr_path=out_json.with_suffix(".stderr"),
        timeout=timeout,
    )


def extract_positions(layout_json: Path, out_tsv: Path) -> None:
    layout = json.loads(layout_json.read_text())
    with out_tsv.open("w") as out:
        for nd in layout["nodes"]:
            pos = nd["position"]
            size = nd["size"]
            cx = pos["x"] + size["width"] / 2.0
            cy = pos["y"] + size["height"] / 2.0
            out.write(f"{nd['modelId']}\t{cx:.3f}\t{cy:.3f}\n")


def scale_positions(src: Path, out: Path, sx: float, sy: float) -> None:
    rows: list[tuple[str, float, float]] = []
    for line in src.read_text().splitlines():
        parts = line.split("\t")
        if len(parts) < 3:
            continue
        rows.append((parts[0], float(parts[1]), float(parts[2])))
    if not rows:
        raise ValueError(f"no positions in {src}")
    xs = [x for _mid, x, _y in rows]
    ys = [y for _mid, _x, y in rows]
    center_x = (min(xs) + max(xs)) / 2.0
    center_y = (min(ys) + max(ys)) / 2.0
    with out.open("w") as handle:
        for model_id, x, y in rows:
            tx = center_x + (x - center_x) * sx
            ty = center_y + (y - center_y) * sy
            handle.write(f"{model_id}\t{tx:.3f}\t{ty:.3f}\n")


def run_v34_stage(
    label: str,
    in_tsv: Path,
    out_tsv: Path,
    extra_args: list[str],
    timeout: int,
) -> None:
    log_path = out_tsv.with_suffix(f".{label}.log")
    proc = run_checked(
        [
            python_exe(),
            str(V34),
            "--layout",
            str(LAYOUT),
            "--positions",
            str(in_tsv),
            "--out-tsv",
            str(out_tsv),
            *extra_args,
        ],
        timeout=timeout,
    )
    log_path.write_text(proc.stdout + proc.stderr)


def carrier_stage_args() -> list[str]:
    return [
        "--rounds", "70",
        "--top-nodes", "0",
        "--top-edges", "0",
        "--top-clusters", "90",
        "--steps", "300,800,1600,3200,6400,10000",
        "--anchor-radii", "150",
        "--swap-pairs", "0",
        "--cluster-swap-pairs", "1000",
        "--overlap-candidates", "160",
        "--loose-pack-groups", "0",
        "--loose-anchor-nodes", "0",
        "--group-anchor-groups", "280",
        "--group-anchor-max-size", "100",
        "--group-anchor-radii",
        "0,300,600,1000,1800,3000,5000,8000,12000,18000,26000,38000,52000,68000,86000",
        "--count-bundle-nodes",
        "--overlap-margin", "16",
        "--overlap-weight", "200",
        "--bbox-weight", "500",
        "--bbox-target-b", "4.0",
        "--final-max-candidates", "14000",
        "--min-gain", "1",
        "--carrier-cross",
    ]


def bundle_stage_args() -> list[str]:
    return [
        "--rounds", "50",
        "--top-nodes", "0",
        "--top-edges", "0",
        "--top-clusters", "60",
        "--steps", "300,800,1600,3200,6400",
        "--anchor-radii", "150",
        "--swap-pairs", "0",
        "--cluster-swap-pairs", "500",
        "--overlap-candidates", "180",
        "--loose-pack-groups", "0",
        "--loose-anchor-nodes", "0",
        "--group-anchor-groups", "180",
        "--group-anchor-max-size", "100",
        "--group-anchor-radii",
        "0,300,600,1000,1800,3000,5000,8000,12000,18000,26000,38000,52000,68000",
        "--bundle-orbit-groups", "32",
        "--bundle-orbit-rotations", "16",
        "--bundle-orbit-radius-scales", "0.7,1.0,1.4",
        "--count-bundle-nodes",
        "--overlap-margin", "16",
        "--overlap-weight", "200",
        "--bbox-weight", "500",
        "--bbox-target-b", "4.0",
        "--final-max-candidates", "14000",
        "--min-gain", "1",
        "--carrier-cross",
    ]


def repair_stage_args() -> list[str]:
    return [
        "--rounds", "60",
        "--top-nodes", "0",
        "--top-edges", "0",
        "--top-clusters", "80",
        "--steps", "300,800,1600,3200,6400",
        "--anchor-radii", "150",
        "--swap-pairs", "0",
        "--cluster-swap-pairs", "800",
        "--overlap-candidates", "260",
        "--loose-pack-groups", "40",
        "--loose-pack-rotations", "8",
        "--loose-pack-spacing-scale", "2.0",
        "--loose-anchor-nodes", "120",
        "--loose-anchor-radii", "150,300,600,1000,1800,3000,5000,8000",
        "--group-anchor-groups", "240",
        "--group-anchor-max-size", "100",
        "--group-anchor-radii",
        "0,300,600,1000,1800,3000,5000,8000,12000,18000,26000,38000,52000",
        "--bundle-orbit-groups", "32",
        "--bundle-orbit-rotations", "16",
        "--bundle-orbit-radius-scales", "0.7,1.0,1.4",
        "--count-bundle-nodes",
        "--overlap-margin", "16",
        "--overlap-weight", "300",
        "--bbox-weight", "800",
        "--bbox-target-b", "4.0",
        "--final-max-candidates", "15000",
        "--min-gain", "1",
        "--carrier-cross",
    ]


def replay_quick(args: argparse.Namespace) -> Path:
    args.out_dir.mkdir(parents=True, exist_ok=True)
    scaled = args.out_dir / "under1000-scaled.tsv"
    final_json = args.out_dir / "under1000.json"
    scale_positions(args.repair_positions, scaled, args.scale_x, args.scale_y)
    run_postpass(scaled, final_json, args.timeout)
    print_metrics("final", final_json)
    return final_json


def replay_full(args: argparse.Namespace) -> Path:
    args.out_dir.mkdir(parents=True, exist_ok=True)
    carrier_tsv = args.out_dir / "carrier1.tsv"
    bundle_tsv = args.out_dir / "bundleorbit1.tsv"
    run_v34_stage("carrier", args.start_positions, carrier_tsv, carrier_stage_args(), args.timeout)
    run_v34_stage("bundleorbit", carrier_tsv, bundle_tsv, bundle_stage_args(), args.timeout)

    post_in = bundle_tsv
    for idx in range(1, args.fixed_point_iters + 1):
        post_json = args.out_dir / f"postpass-iter{idx}.json"
        run_postpass(post_in, post_json, args.timeout)
        print_metrics(f"postpass-iter{idx}", post_json)
        post_in = args.out_dir / f"postpass-iter{idx}.tsv"
        extract_positions(post_json, post_in)

    repair_tsv = args.out_dir / "repair1.tsv"
    run_v34_stage("repair", post_in, repair_tsv, repair_stage_args(), args.timeout)
    args.repair_positions = repair_tsv
    return replay_quick(args)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("quick", "full"), default="quick")
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument(
        "--start-positions",
        type=Path,
        default=DEFAULT_ARTIFACT_DIR / "groupanchor2.tsv",
        help="full mode input positions",
    )
    parser.add_argument(
        "--repair-positions",
        type=Path,
        default=DEFAULT_ARTIFACT_DIR / "repair1.tsv",
        help="quick mode input positions",
    )
    parser.add_argument("--scale-x", type=float, default=0.86)
    parser.add_argument("--scale-y", type=float, default=0.88)
    parser.add_argument("--fixed-point-iters", type=int, default=3)
    parser.add_argument("--timeout", type=int, default=7200)
    parser.add_argument("--copy-final-to", type=Path, default=None)
    args = parser.parse_args()

    final_json = replay_full(args) if args.mode == "full" else replay_quick(args)
    if args.copy_final_to is not None:
        args.copy_final_to.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(final_json, args.copy_final_to)
        print(f"copied final JSON to {args.copy_final_to}")


if __name__ == "__main__":
    main()
