#!/usr/bin/env python3
"""Enhance the synthetic expert corpus with stronger post-passes.

Per-graph pipeline (~2-3 min on 500-node graphs):
  1. CPT (cluster-pair-target) max-iter 1 → tsv 1
  2. bbox-pack max-iter 1 → tsv 2 (chains tsv 1 positions)
  3. C++ binary --positions-tsv (final reroute) → new expert JSON

Output: data/erd-poc/expert-strong/<graph>.json
"""
import argparse
import json
import os
import subprocess
import tempfile
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def find_binary() -> Path:
    for plat in ("darwin-arm64", "darwin-x64", "linux-x64", "win32-x64"):
        p = ROOT / "bin" / "ogdf" / plat / "django-erd-ogdf-layout"
        if p.exists():
            return p
    raise FileNotFoundError("django-erd-ogdf-layout binary not found")


def process_graph(args: tuple) -> tuple:
    graph_dir, expert_path, out_path, binary, cpt_iter, bbox_iter, cpt_skip_above = args
    graph_id = Path(graph_dir).name
    if not Path(expert_path).exists():
        return (graph_id, None, "missing expert")
    nodes_tsv = Path(graph_dir) / "nodes.tsv"
    edges_tsv = Path(graph_dir) / "edges.tsv"
    if not (nodes_tsv.exists() and edges_tsv.exists()):
        return (graph_id, None, "missing tsv")

    n_nodes = sum(1 for _ in open(nodes_tsv))
    cpt_tsv = None

    work = Path(tempfile.mkdtemp(prefix=f"enh-{graph_id}-"))
    try:
        # 1. CPT (skip on very large graphs to save time)
        if n_nodes <= cpt_skip_above:
            cpt_tsv = work / "cpt.tsv"
            r = subprocess.run(
                [
                    "python",
                    str(ROOT / "scripts/cluster-pair-target.py"),
                    str(expert_path),
                    str(cpt_tsv),
                    "--max-iter",
                    str(cpt_iter),
                ],
                capture_output=True,
                text=True,
                timeout=600,
            )
            if r.returncode != 0:
                cpt_tsv = None  # fall through to expert positions

        # 2. bbox-pack
        bp_tsv = work / "bp.tsv"
        bp_input = expert_path  # bbox-pack reads JSON
        # If CPT produced a tsv, route through binary first to materialise
        # the CPT positions as a new JSON, then bbox-pack on that.
        if cpt_tsv is not None and cpt_tsv.exists():
            cpt_json = work / "cpt.json"
            r = subprocess.run(
                [
                    str(binary),
                    "layout",
                    "--mode",
                    "hierarchical_barycenter",
                    "--nodes-file",
                    str(nodes_tsv),
                    "--edges-file",
                    str(edges_tsv),
                    "--edge-routing",
                    "straight",
                    "--cluster-graph",
                    "1",
                    "--positions-tsv",
                    str(cpt_tsv),
                ],
                capture_output=True,
                text=True,
                timeout=600,
            )
            if r.returncode == 0:
                cpt_json.write_text(r.stdout)
                bp_input = str(cpt_json)
        r = subprocess.run(
            [
                "python",
                str(ROOT / "scripts/bbox-pack.py"),
                bp_input,
                str(bp_tsv),
                "--max-iter",
                str(bbox_iter),
            ],
            capture_output=True,
            text=True,
            timeout=600,
        )
        if r.returncode != 0 or not bp_tsv.exists():
            return (graph_id, None, f"bbox-pack failed")

        # 3. Final reroute via binary
        r = subprocess.run(
            [
                str(binary),
                "layout",
                "--mode",
                "hierarchical_barycenter",
                "--nodes-file",
                str(nodes_tsv),
                "--edges-file",
                str(edges_tsv),
                "--edge-routing",
                "straight",
                "--cluster-graph",
                "1",
                "--positions-tsv",
                str(bp_tsv),
            ],
            capture_output=True,
            text=True,
            timeout=600,
        )
        if r.returncode != 0:
            return (graph_id, None, f"final reroute failed: {r.stderr[:200]}")
        layout = json.loads(r.stdout)
        Path(out_path).write_text(json.dumps(layout, separators=(",", ":")))
        md = layout.get("engineMetadata", {})
        return (
            graph_id,
            {
                "nodes": n_nodes,
                "edgeCrossings": md.get("edgeCrossings"),
                "boundingBoxArea": md.get("boundingBoxArea"),
                "nodeOverlaps": md.get("nodeOverlaps"),
                "cpt_applied": cpt_tsv is not None,
            },
            None,
        )
    except subprocess.TimeoutExpired:
        return (graph_id, None, "timeout")
    except Exception as e:
        return (graph_id, None, f"exception: {e}")
    finally:
        import shutil
        shutil.rmtree(work, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--limit", type=int, default=None)
    ap.add_argument("--cpt-iter", type=int, default=1)
    ap.add_argument("--bbox-iter", type=int, default=1)
    ap.add_argument("--cpt-skip-above", type=int, default=600,
                    help="skip CPT on graphs larger than this many nodes")
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--skip-existing", action="store_true")
    args = ap.parse_args()

    binary = find_binary()
    graphs_root = ROOT / "data/erd-poc/graphs"
    expert_root = ROOT / "data/erd-poc/expert"
    out_root = ROOT / "data/erd-poc/expert-strong"
    out_root.mkdir(exist_ok=True)

    graphs = sorted(p for p in graphs_root.iterdir() if p.is_dir())
    if args.limit:
        graphs = graphs[: args.limit]

    tasks = []
    for g in graphs:
        ex = expert_root / f"{g.name}.json"
        out = out_root / f"{g.name}.json"
        if args.skip_existing and out.exists():
            continue
        tasks.append((str(g), str(ex), str(out), str(binary),
                      args.cpt_iter, args.bbox_iter, args.cpt_skip_above))

    print(f"Processing {len(tasks)} graphs (workers={args.workers}, "
          f"cpt-iter={args.cpt_iter}, bbox-iter={args.bbox_iter}, "
          f"cpt-skip-above={args.cpt_skip_above})")
    successes = []
    failures = []
    cpt_skipped = 0
    with ProcessPoolExecutor(max_workers=args.workers) as ex:
        futures = {ex.submit(process_graph, t): t for t in tasks}
        for i, fut in enumerate(as_completed(futures), 1):
            graph_id, md, err = fut.result()
            if err:
                failures.append((graph_id, err))
                print(f"  [{i}/{len(tasks)}] {graph_id} FAIL: {err}")
            else:
                successes.append((graph_id, md))
                if not md.get("cpt_applied"): cpt_skipped += 1
                cross = md.get("edgeCrossings", 0) or 0
                bbox = (md.get("boundingBoxArea", 0) or 0) / 1e9
                print(f"  [{i}/{len(tasks)}] {graph_id} cross={cross} "
                      f"bbox={bbox:.2f}B  n={md['nodes']} cpt={'Y' if md.get('cpt_applied') else 'N'}")
    print(f"\nDone. Success={len(successes)} Fail={len(failures)} "
          f"CPT-skipped={cpt_skipped}")
    summary = {
        "successes": [{"graph": g, "metrics": m} for g, m in successes],
        "failures": [{"graph": g, "error": e} for g, e in failures],
    }
    (out_root / "_summary.json").write_text(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
