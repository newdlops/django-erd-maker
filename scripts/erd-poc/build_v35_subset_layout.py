#!/usr/bin/env python3
"""Build reproducible smaller ERD layouts for v35 scale checks."""

from __future__ import annotations

import argparse
import copy
import importlib.util
import json
import sys
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "v34_move_search", ROOT / "scripts/erd-poc/v34_move_search.py"
)
v34 = importlib.util.module_from_spec(SPEC)
sys.modules["v34_move_search"] = v34
assert SPEC.loader is not None
SPEC.loader.exec_module(v34)


def choose_hot_cluster_nodes(
    layout: dict,
    positions: np.ndarray,
    edges: np.ndarray,
    count: int,
) -> np.ndarray:
    evaluator = v34.fce.FastCrossEval(edges, positions.shape[0])
    _cross, node_counts, _edge_counts = v34.crossing_incident_counts(
        positions,
        evaluator,
    )
    clusters = v34.cluster_members(layout)
    covered = np.zeros(positions.shape[0], dtype=bool)
    groups: list[tuple[int, str, np.ndarray]] = []
    for key, members in clusters.items():
        if members.shape[0] == 0:
            continue
        covered[members] = True
        groups.append((int(node_counts[members].sum()), key, members))
    for idx in np.flatnonzero(~covered):
        groups.append((int(node_counts[idx]), f"__single__{idx}", np.array([idx], dtype=np.int32)))
    groups.sort(reverse=True, key=lambda item: (item[0], item[2].shape[0]))

    selected: list[int] = []
    used = np.zeros(positions.shape[0], dtype=bool)
    for _score, _key, members in groups:
        if len(selected) >= count:
            break
        remaining = count - len(selected)
        if members.shape[0] <= remaining:
            take = members
        else:
            order = np.argsort(-node_counts[members])
            take = members[order[:remaining]]
        for raw_idx in take:
            idx = int(raw_idx)
            if not used[idx]:
                used[idx] = True
                selected.append(idx)
            if len(selected) >= count:
                break
    if len(selected) < count:
        order = np.argsort(-node_counts)
        for raw_idx in order:
            idx = int(raw_idx)
            if not used[idx]:
                used[idx] = True
                selected.append(idx)
            if len(selected) >= count:
                break
    return np.array(sorted(selected), dtype=np.int32)


def choose_random_nodes(layout: dict, count: int, seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    n = len(layout["nodes"])
    return np.array(sorted(rng.choice(n, size=min(count, n), replace=False)), dtype=np.int32)


def filter_leaf_bundles(layout: dict, selected_ids: set[str]) -> list[dict]:
    out: list[dict] = []
    for bundle in (layout.get("engineMetadata") or {}).get("leafBundles") or []:
        got = copy.deepcopy(bundle)
        leaves = [mid for mid in got.get("leafModelIds") or [] if mid in selected_ids]
        roots = [mid for mid in got.get("sharedRootModelIds") or [] if mid in selected_ids]
        parent = got.get("parentModelId")
        if parent and parent not in selected_ids:
            got.pop("parentModelId", None)
            parent = None
        if len(leaves) < 2:
            continue
        if not roots and not parent:
            continue
        got["leafModelIds"] = leaves
        got["sharedRootModelIds"] = roots
        out.append(got)
    return out


def subset_layout(layout: dict, selected: np.ndarray) -> dict:
    selected_ids = {layout["nodes"][int(idx)]["modelId"] for idx in selected}
    out = copy.deepcopy(layout)
    out["nodes"] = [copy.deepcopy(layout["nodes"][int(idx)]) for idx in selected]
    out["routedEdges"] = [
        copy.deepcopy(edge)
        for edge in layout.get("routedEdges", [])
        if edge.get("sourceModelId") in selected_ids
        and edge.get("targetModelId") in selected_ids
    ]
    out["crossings"] = []
    meta = copy.deepcopy(layout.get("engineMetadata") or {})
    meta["leafBundles"] = filter_leaf_bundles(layout, selected_ids)
    meta["edgeCrossings"] = None
    meta["nodeOverlaps"] = None
    meta["boundingBoxArea"] = None
    meta["strategyReason"] = "v35 subset layout for scale evaluation"
    out["engineMetadata"] = meta
    return out


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, default=ROOT / "data/erd-poc/layouts/real-main.json")
    parser.add_argument("--positions", type=Path, default=None)
    parser.add_argument("--out-layout", type=Path, required=True)
    parser.add_argument("--out-tsv", type=Path, required=True)
    parser.add_argument("--count", type=int, default=700)
    parser.add_argument("--strategy", choices=["hot-clusters", "random"], default="hot-clusters")
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--count-bundle-nodes", action="store_true")
    parser.add_argument("--overlap-margin", type=float, default=16.0)
    parser.add_argument("--overlap-weight", type=float, default=5000.0)
    parser.add_argument("--bbox-weight", type=float, default=800.0)
    parser.add_argument("--bbox-target-b", type=float, default=4.0)
    args = parser.parse_args()

    layout = v34.load_layout(args.layout)
    positions = (
        v34.read_positions_tsv(args.positions, layout)
        if args.positions
        else v34.layout_positions(layout)
    )
    full_edges = v34.graph_edges(layout)
    if args.strategy == "hot-clusters":
        selected = choose_hot_cluster_nodes(layout, positions, full_edges, args.count)
    else:
        selected = choose_random_nodes(layout, args.count, args.seed)
    out_layout = subset_layout(layout, selected)
    out_positions = positions[selected].copy()

    args.out_layout.parent.mkdir(parents=True, exist_ok=True)
    args.out_tsv.parent.mkdir(parents=True, exist_ok=True)
    args.out_layout.write_text(json.dumps(out_layout, separators=(",", ":")))
    v34.write_positions_tsv(args.out_tsv, out_layout, out_positions)

    edges = v34.graph_edges(out_layout)
    widths, heights = v34.render_node_sizes(out_layout, edges)
    evaluator = v34.fce.FastCrossEval(edges, out_positions.shape[0])
    active_mask = v34.render_active_overlap_mask(
        out_layout,
        args.count_bundle_nodes,
    )
    collision_geometry = v34.build_render_collision_geometry(
        out_layout,
        edges,
        args.count_bundle_nodes,
    )
    overlap_pairs = collision_geometry.overlap_pairs
    metrics = v34.measure(
        out_positions,
        evaluator,
        widths,
        heights,
        active_mask,
        overlap_pairs,
        args.overlap_margin,
        args.overlap_weight,
        args.bbox_weight,
        args.bbox_target_b,
        collision_geometry=collision_geometry,
    )
    print(
        f"wrote nodes={len(out_layout['nodes'])} edges={edges.shape[0]} "
        f"cross={metrics.cross} overlaps={metrics.overlaps} "
        f"bbox={metrics.bbox_b:.2f}B score={metrics.score:.1f} "
        f"layout={args.out_layout} tsv={args.out_tsv}",
        flush=True,
    )


if __name__ == "__main__":
    main()
