#!/usr/bin/env python3
"""Build expert layout corpus by applying our session's optimization
pipeline to each synthetic graph.

Lightweight expert pipeline (per graph, ~3 min):
  1. Read polished layout (from data/erd-poc/polished/) — already has
     ml-layout-polish-rigid output positions.
  2. Apply Y-axis compression (scale 0.62 around centroid) to positions.
  3. C++ binary re-route via --positions-tsv.
  4. Save resulting layout as expert sample.

The full session pipeline (CPT chain + bbox-pack + squeeze + bundle-
compact + Y-scale) takes 60+ min per graph and is too slow to run on
all 550 synthetic graphs. The Y-scale step alone captures the most
important geometric optimization (input-TSV based Y compression
reduces bbox 40% with cross typically dropping or holding under 1000).

Output: data/erd-poc/expert/synth-XXXX-...json — final layout JSONs.

Usage:
  python scripts/erd-poc/build_expert_corpus.py [--limit N] [--y-scale 0.62]
"""
import argparse
import json
import os
import sys
from pathlib import Path
from concurrent.futures import ProcessPoolExecutor, as_completed
import subprocess


def find_binary(extension_root: Path) -> Path:
    for plat in ("darwin-arm64", "darwin-x64", "linux-x64", "win32-x64"):
        p = extension_root / "bin" / "ogdf" / plat / "django-erd-ogdf-layout"
        if p.exists():
            return p
        p_exe = p.with_suffix(".exe")
        if p_exe.exists():
            return p_exe
    raise FileNotFoundError("django-erd-ogdf-layout binary not found")


def y_scale_positions(layout_json: dict, scale: float) -> str:
    """Return a positions TSV after applying Y-axis compression."""
    nodes = layout_json["nodes"]
    if not nodes:
        return "modelId\tx\ty\n"
    cy_sum = sum(
        (n["position"]["y"] + n["size"]["height"] / 2) for n in nodes
    )
    cy = cy_sum / len(nodes)
    lines = ["modelId\tx\ty"]
    for n in nodes:
        cx_pos = n["position"]["x"] + n["size"]["width"] / 2
        center_y = n["position"]["y"] + n["size"]["height"] / 2
        new_y = cy + (center_y - cy) * scale
        lines.append(f'{n["modelId"]}\t{cx_pos:.3f}\t{new_y:.3f}')
    return "\n".join(lines) + "\n"


def process_graph(
    args: tuple,
) -> tuple[str, dict | None, str | None]:
    """Per-graph worker. Returns (graph_id, metadata_dict_or_None, error)."""
    graph_dir, polished_path, expert_path, binary_path, y_scale = args
    graph_id = Path(graph_dir).name
    nodes_tsv = Path(graph_dir) / "nodes.tsv"
    edges_tsv = Path(graph_dir) / "edges.tsv"
    if not (nodes_tsv.exists() and edges_tsv.exists()):
        return (graph_id, None, "missing nodes.tsv or edges.tsv")
    if not Path(polished_path).exists():
        return (graph_id, None, "missing polished layout")
    try:
        polished = json.loads(Path(polished_path).read_text())
    except Exception as e:
        return (graph_id, None, f"polished parse: {e}")
    tsv_text = y_scale_positions(polished, y_scale)
    tsv_path = Path(f"/tmp/expert-{graph_id}.tsv")
    tsv_path.write_text(tsv_text)
    cmd = [
        str(binary_path), "layout",
        "--mode", "hierarchical_barycenter",
        "--nodes-file", str(nodes_tsv),
        "--edges-file", str(edges_tsv),
        "--edge-routing", "straight",
        "--cluster-graph", "1",
        "--positions-tsv", str(tsv_path),
    ]
    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=300,
        )
    except subprocess.TimeoutExpired:
        return (graph_id, None, "binary timeout")
    finally:
        try: tsv_path.unlink()
        except: pass
    if result.returncode != 0:
        return (graph_id, None, f"binary exit {result.returncode}: {result.stderr[:200]}")
    try:
        out_layout = json.loads(result.stdout)
    except Exception as e:
        return (graph_id, None, f"stdout parse: {e}")
    Path(expert_path).write_text(json.dumps(out_layout, separators=(",", ":")))
    md = out_layout.get("engineMetadata", {})
    return (graph_id, {
        "edgeCrossings": md.get("edgeCrossings"),
        "boundingBoxArea": md.get("boundingBoxArea"),
        "edgeNodeIntersections": md.get("edgeNodeIntersections"),
        "nodeOverlaps": md.get("nodeOverlaps"),
    }, None)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--limit", type=int, default=None,
                    help="Process only first N graphs (default: all)")
    ap.add_argument("--y-scale", type=float, default=0.62)
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--skip-existing", action="store_true",
                    help="Skip graphs whose expert layout already exists")
    args = ap.parse_args()
    repo_root = Path(__file__).resolve().parents[2]
    graphs_root = repo_root / "data" / "erd-poc" / "graphs"
    polished_root = repo_root / "data" / "erd-poc" / "polished"
    expert_root = repo_root / "data" / "erd-poc" / "expert"
    expert_root.mkdir(exist_ok=True)
    binary = find_binary(repo_root)

    graphs = sorted([p for p in graphs_root.iterdir() if p.is_dir()])
    if args.limit:
        graphs = graphs[: args.limit]

    tasks = []
    for g in graphs:
        polished = polished_root / f"{g.name}.json"
        expert = expert_root / f"{g.name}.json"
        if args.skip_existing and expert.exists():
            continue
        tasks.append((str(g), str(polished), str(expert), str(binary), args.y_scale))

    print(f"Processing {len(tasks)} graphs (y_scale={args.y_scale}, workers={args.workers})")
    successes = []
    failures = []
    with ProcessPoolExecutor(max_workers=args.workers) as ex:
        futures = {ex.submit(process_graph, t): t for t in tasks}
        for i, fut in enumerate(as_completed(futures), 1):
            graph_id, md, err = fut.result()
            if err:
                failures.append((graph_id, err))
                print(f"  [{i}/{len(tasks)}] {graph_id} FAIL: {err}")
            else:
                successes.append((graph_id, md))
                print(f"  [{i}/{len(tasks)}] {graph_id} cross={md['edgeCrossings']} "
                      f"bbox={md['boundingBoxArea']/1e9:.2f}B")

    print(f"\nDone. Success={len(successes)} Fail={len(failures)}")
    summary = {
        "y_scale": args.y_scale,
        "successes": [{"graph": g, "metrics": m} for g, m in successes],
        "failures": [{"graph": g, "error": e} for g, e in failures],
    }
    summary_path = expert_root / "_corpus_summary.json"
    summary_path.write_text(json.dumps(summary, indent=2))
    print(f"Summary: {summary_path}")


if __name__ == "__main__":
    main()
