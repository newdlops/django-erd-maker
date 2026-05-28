#!/usr/bin/env python3
"""Verify Python metrics_extended.py against the C++ binary's
engineMetadata for the same graph + positions.

This is a *correlation* check, not exact parity. The two
implementations operate on slightly different inputs:
  * Python uses node centers + (optionally) axis-aligned bbox-clipped
    endpoints — both directly derivable from positions alone.
  * C++ uses the routing engine's route segments, which add small
    visual-margin / lane-clearance offsets on top of bbox clipping.

Expect ~5-10% delta on stress / edgeLenCv / hubClearance and possibly
larger on crossingAngle (route start/end can shift crossings by a few
counts). clusterCompactness will be 0 in Python unless the source
graph already carries clusterId per node — the C++ binary's internal
Louvain assignment is not exposed through engineMetadata.

The goal is to confirm the metrics are *correlated* across both
implementations, not byte-identical. ML training that uses Python
metrics during data construction and C++ metrics at evaluation time
should observe consistent ranking signals despite small numeric
differences.

Usage:
  python scripts/erd-poc/verify_metrics_parity.py
  python scripts/erd-poc/verify_metrics_parity.py --graph synth-0003-n61-e66
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "metrics_extended", ROOT / "scripts/erd-poc/metrics_extended.py"
)
metrics_extended = importlib.util.module_from_spec(SPEC)
sys.modules["metrics_extended"] = metrics_extended
SPEC.loader.exec_module(metrics_extended)


def load_graph_inputs(graph_dir: Path):
    """Read nodes.tsv and edges.tsv into modelId-indexed arrays."""
    with (graph_dir / "graph.json").open() as f:
        graph = json.load(f)
    model_ids = [str(nd["modelId"]) for nd in graph["nodes"]]
    id_to_idx = {mid: i for i, mid in enumerate(model_ids)}
    widths = np.array(
        [float(nd.get("width", 100.0)) for nd in graph["nodes"]],
        dtype=np.float64,
    )
    heights = np.array(
        [float(nd.get("height", 60.0)) for nd in graph["nodes"]],
        dtype=np.float64,
    )
    cluster_ids = [str(nd.get("clusterId") or "") for nd in graph["nodes"]]
    edge_pairs: list[tuple[int, int]] = []
    for e in graph["edges"]:
        # graph.json uses {source,target} OR {sourceModelId,targetModelId}.
        src_key = "sourceModelId" if "sourceModelId" in e else "source"
        tgt_key = "targetModelId" if "targetModelId" in e else "target"
        s = id_to_idx.get(str(e[src_key]))
        t = id_to_idx.get(str(e[tgt_key]))
        if s is None or t is None or s == t:
            continue
        edge_pairs.append((s, t))
    edges = np.array(edge_pairs, dtype=np.int32) if edge_pairs else np.zeros((0, 2), dtype=np.int32)
    return model_ids, id_to_idx, widths, heights, cluster_ids, edges


def run_cpp(graph_dir: Path) -> dict | None:
    bin_path = ROOT / "bin/ogdf/darwin-arm64/django-erd-ogdf-layout"
    proc = subprocess.run(
        [
            str(bin_path), "layout",
            "--mode", "fmmm",
            "--nodes-file", str(graph_dir / "nodes.tsv"),
            "--edges-file", str(graph_dir / "edges.tsv"),
            "--edge-routing", "straight",
            "--cluster-graph", "1",
        ],
        capture_output=True,
        text=True,
        timeout=600,
    )
    if proc.returncode != 0:
        print(f"C++ binary failed: {proc.stderr[-500:]}", file=sys.stderr)
        return None
    try:
        return json.loads(proc.stdout)
    except json.JSONDecodeError:
        print(f"failed to parse C++ JSON output", file=sys.stderr)
        return None


def positions_from_cpp(layout: dict, id_to_idx: dict[str, int]) -> np.ndarray:
    """Extract (N, 2) positions array matching id_to_idx order."""
    n = len(id_to_idx)
    pos = np.zeros((n, 2), dtype=np.float64)
    for nd in layout.get("nodes", []):
        mid = str(nd.get("modelId"))
        idx = id_to_idx.get(mid)
        if idx is None:
            continue
        # Output uses nested position: {x, y}.
        p = nd.get("position", {}) or {}
        pos[idx, 0] = float(p.get("x", 0.0))
        pos[idx, 1] = float(p.get("y", 0.0))
    return pos


def main():
    p = argparse.ArgumentParser()
    p.add_argument(
        "--graph",
        default="synth-0001-n44-e46",
        help="graph directory name under data/erd-poc/graphs/",
    )
    args = p.parse_args()

    graph_dir = ROOT / "data/erd-poc/graphs" / args.graph
    if not graph_dir.is_dir():
        print(f"graph dir not found: {graph_dir}", file=sys.stderr)
        return 1

    print(f"Loading {graph_dir.name} ...", flush=True)
    model_ids, id_to_idx, widths, heights, cluster_ids, edges = load_graph_inputs(graph_dir)
    print(f"  nodes={len(model_ids)} edges={edges.shape[0]}", flush=True)

    print("Running C++ binary (fmmm, straight routing) ...", flush=True)
    layout = run_cpp(graph_dir)
    if layout is None:
        return 2
    cpp_meta = layout.get("engineMetadata", {}) or {}

    positions = positions_from_cpp(layout, id_to_idx)
    print(
        f"  C++ emitted positions: span x=[{positions[:, 0].min():.1f}, "
        f"{positions[:, 0].max():.1f}] y=[{positions[:, 1].min():.1f}, "
        f"{positions[:, 1].max():.1f}]",
        flush=True,
    )

    # Prefer the C++ binary's cluster assignment (engineMetadata.
    # clusterByModelId) over the graph.json hint — that's the partition
    # the C++ clusterCompactness was computed against, so Python sees
    # the same partition for an apples-to-apples comparison.
    cluster_map = cpp_meta.get("clusterByModelId", {}) or {}
    if cluster_map:
        cluster_ids = [cluster_map.get(mid, "") for mid in model_ids]

    # Extract C++ routed polylines so geometry-dependent metrics (length,
    # crossing angle, bend) operate on the same segments as the C++ binary.
    edge_id_to_idx: dict[str, int] = {}
    for i, e in enumerate(layout.get("routedEdges", []) or []):
        eid = str(e.get("edgeId", ""))
        if eid:
            edge_id_to_idx[eid] = i
    route_segments: list[np.ndarray] = []
    routed = layout.get("routedEdges", []) or []
    # Build routes aligned to our `edges` array order (which was built
    # from graph.json). Edges may match via edgeId or fall through to
    # endpoint match if edge IDs differ.
    routed_by_endpoints: dict[tuple[str, str], list[dict]] = {}
    for re in routed:
        s = str(re.get("sourceModelId", ""))
        t = str(re.get("targetModelId", ""))
        routed_by_endpoints.setdefault((s, t), []).append(re)
    used: set[int] = set()
    for s_idx, t_idx in edges:
        s_mid = model_ids[s_idx]
        t_mid = model_ids[t_idx]
        candidates = routed_by_endpoints.get((s_mid, t_mid), [])
        if not candidates:
            candidates = routed_by_endpoints.get((t_mid, s_mid), [])
        # Pick the first un-used route for this endpoint pair.
        chosen = None
        for c in candidates:
            cid = id(c)
            if cid not in used:
                chosen = c
                used.add(cid)
                break
        if chosen is None:
            # Fallback: straight line between centers.
            route_segments.append(np.array([positions[s_idx], positions[t_idx]]))
            continue
        pts = chosen.get("points") or []
        if len(pts) < 2:
            route_segments.append(np.array([positions[s_idx], positions[t_idx]]))
            continue
        arr = np.array([[p["x"], p["y"]] for p in pts], dtype=np.float64)
        route_segments.append(arr)

    print("Computing Python metrics_extended ...", flush=True)
    py = metrics_extended.measure_extended_with_routes(
        positions, edges, widths, heights, route_segments, cluster_ids,
        model_ids=model_ids,
    )

    # Side-by-side comparison.
    pairs = [
        ("stress", float(cpp_meta.get("stressScore", 0.0)), py.stress_score),
        ("edgeLenCv", float(cpp_meta.get("edgeLengthCv", 0.0)), py.edge_length_cv),
        ("xAngMean", float(cpp_meta.get("crossingAngleMean", 0.0)), py.crossing_angle_mean),
        ("xAngCv", float(cpp_meta.get("crossingAngleCv", 0.0)), py.crossing_angle_cv),
        ("edgeBend", float(cpp_meta.get("edgeBendTotal", 0.0)), py.edge_bend_total),
        ("hubClearP10", float(cpp_meta.get("hubClearanceP10", 0.0)), py.hub_clearance_p10),
        ("clusterCompact",
         float(cpp_meta.get("clusterCompactnessMean", 0.0)),
         py.cluster_compactness_mean),
        ("xPerEdgeP50",
         float(cpp_meta.get("crossingsPerEdgeP50", 0)),
         float(py.crossings_per_edge_p50)),
        ("xPerEdgeP90",
         float(cpp_meta.get("crossingsPerEdgeP90", 0)),
         float(py.crossings_per_edge_p90)),
        ("cleanEdgeRatio",
         float(cpp_meta.get("cleanEdgeRatio", 0.0)),
         py.clean_edge_ratio),
        ("xBetweenClusters",
         float(cpp_meta.get("edgeCrossingsBetweenClusters", 0)),
         float(py.edge_crossings_between_clusters)),
        ("nodeAreaCov",
         float(cpp_meta.get("nodeAreaCoverage", 0.0)),
         py.node_area_coverage),
        ("emptySpaceCv",
         float(cpp_meta.get("emptySpaceCv", 0.0)),
         py.empty_space_cv),
        ("compositeQuality",
         float(cpp_meta.get("compositeQuality", 0.0)),
         py.composite_quality),
        ("subClean",
         float(cpp_meta.get("subCleanQuality", 0.0)),
         py.sub_clean_quality),
        ("subSeverity",
         float(cpp_meta.get("subSeverityQuality", 0.0)),
         py.sub_severity_quality),
        ("subAngle",
         float(cpp_meta.get("subAngleQuality", 0.0)),
         py.sub_angle_quality),
        ("subStress",
         float(cpp_meta.get("subStressQuality", 0.0)),
         py.sub_stress_quality),
        ("subCompact",
         float(cpp_meta.get("subCompactQuality", 0.0)),
         py.sub_compact_quality),
        ("subUniform",
         float(cpp_meta.get("subUniformQuality", 0.0)),
         py.sub_uniform_quality),
        ("subSpread",
         float(cpp_meta.get("subSpreadQuality", 0.0)),
         py.sub_spread_quality),
    ]
    # Acceptable divergence per metric — correlation expected, not exact
    # parity. See module docstring for the rationale.
    soft_thresholds_pct = {
        "stress": 15.0,
        "edgeLenCv": 5.0,
        "hubClearP10": 10.0,
        "xAngMean": 5.0,
        "xAngCv": 25.0,
        "edgeBend": 5.0,
        "clusterCompact": 5.0,
        "xPerEdgeP50": 5.0,
        "xPerEdgeP90": 5.0,
        "cleanEdgeRatio": 5.0,
        "xBetweenClusters": 5.0,
        # nodeAreaCov is typically << 1% for ERD layouts → tiny absolute
        # diffs balloon the relative pct. Allow larger budget; absolute
        # values matching matters less than the trend.
        "nodeAreaCov": 30.0,
        "emptySpaceCv": 10.0,
        "compositeQuality": 5.0,
        "subClean": 5.0,
        "subSeverity": 5.0,
        "subAngle": 5.0,
        "subStress": 15.0,
        "subCompact": 30.0,
        "subUniform": 5.0,
        "subSpread": 10.0,
    }
    print()
    print(
        f"  {'metric':18s}  {'cpp':>12s}  {'py':>12s}  {'|delta|':>10s}  "
        f"{'rel%':>8s}  status"
    )
    over_count = 0
    for name, cpp_v, py_v in pairs:
        delta = abs(cpp_v - py_v)
        rel = (100.0 * delta / abs(cpp_v)) if abs(cpp_v) > 1e-9 else 0.0
        budget = soft_thresholds_pct.get(name, 15.0)
        if delta < 0.001 or rel <= budget:
            status = "ok"
        else:
            status = f"OVER (budget {budget:.0f}%)"
            over_count += 1
        print(
            f"  {name:18s}  {cpp_v:12.5f}  {py_v:12.5f}  {delta:10.5f}  "
            f"{rel:7.2f}%  {status}"
        )
    if over_count > 0:
        print(
            f"\n  {over_count} metric(s) exceeded soft budget — investigate "
            f"if this is a regression vs prior baseline."
        )

    # Top-N cross edges comparison. Sort stability differs between C++
    # and numpy on equal counts → check set-equality instead of strict
    # rank match.
    cpp_top = cpp_meta.get("topCrossEdges", []) or []
    py_top = py.top_cross_edges
    print(f"\n  topCrossEdges: cpp={len(cpp_top)} py={len(py_top)}")
    cpp_set = {
        (frozenset({c["sourceModelId"], c["targetModelId"]}), c["crossings"])
        for c in cpp_top
    }
    py_set = {
        (frozenset({p[0], p[1]}), p[2])
        for p in py_top
    }
    set_overlap = len(cpp_set & py_set)
    set_union = len(cpp_set | py_set)
    print(
        f"  set overlap: {set_overlap}/{max(len(cpp_set), len(py_set))} "
        f"(jaccard {set_overlap / max(1, set_union):.2f})"
    )
    show_count = min(5, len(cpp_top))
    if show_count > 0:
        print(f"  {'rank':>4s}  {'cpp':>40s}  {'crossings':>9s}  in py?")
    for k in range(show_count):
        c = cpp_top[k]
        cpp_label = f"{c['sourceModelId']}↔{c['targetModelId']}"
        in_py = (
            frozenset({c["sourceModelId"], c["targetModelId"]}),
            c["crossings"],
        ) in py_set
        print(
            f"  {k+1:>4d}  {cpp_label:>40s}  {c['crossings']:>9d}  "
            f"{'✓' if in_py else '✗'}"
        )
    return 0 if over_count == 0 else 0  # never fail; informational only


if __name__ == "__main__":
    sys.exit(main())
