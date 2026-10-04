#!/usr/bin/env python3
"""Insert one-model node bundles around the real 3-core using direct edges."""

from __future__ import annotations

import argparse
import importlib.util
import sys
import time
from pathlib import Path

import networkx as nx
import numpy as np

ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


v34 = load_module("v34_move_search", ROOT / "scripts/erd-poc/v34_move_search.py")
direct = load_module(
    "search_core_direct_relocation",
    ROOT / "scripts/erd-poc/search_core_direct_relocation.py",
)


def candidate_crossings(
    node: int,
    candidates: np.ndarray,
    new_edges: list[tuple[int, int]],
    active_edges: list[tuple[int, int]],
    positions: np.ndarray,
) -> np.ndarray:
    counts = np.zeros(candidates.shape[0], dtype=np.int32)
    existing = np.asarray(active_edges, dtype=np.int32)
    for source, target in new_edges:
        anchor = target if source == node else source
        valid = (existing[:, 0] != anchor) & (existing[:, 1] != anchor)
        other_edges = existing[valid]
        if other_edges.size == 0:
            continue
        other_source = positions[other_edges[:, 0]]
        other_target = positions[other_edges[:, 1]]
        anchor_points = np.broadcast_to(positions[anchor], candidates.shape)
        for start in range(0, candidates.shape[0], 256):
            stop = min(candidates.shape[0], start + 256)
            counts[start:stop] += direct.proper_crossings(
                candidates[start:stop, None, :],
                anchor_points[start:stop, None, :],
                other_source[None, :, :],
                other_target[None, :, :],
            ).sum(axis=1)
    return counts


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--core-positions", type=Path, required=True)
    parser.add_argument("--out-tsv", type=Path, required=True)
    parser.add_argument("--random-candidates", type=int, default=8000)
    parser.add_argument("--anchor-candidates", type=int, default=1200)
    parser.add_argument("--min-distance-ratio", type=float, default=0.003)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    layout = v34.load_layout(args.layout)
    edges = v34.graph_edges(layout)
    graph = nx.Graph()
    graph.add_nodes_from(range(len(layout["nodes"])))
    graph.add_edges_from((int(source), int(target)) for source, target in edges)
    giant = set(int(node) for node in max(nx.biconnected_components(graph), key=len))
    core_numbers = nx.core_number(graph.subgraph(giant))
    core3 = {node for node, value in core_numbers.items() if value >= 3}
    shell = giant - core3
    records: list[tuple[int, set[int]]] = []
    for component_raw in nx.connected_components(graph.subgraph(shell)):
        component = set(int(node) for node in component_raw)
        if len(component) != 1:
            continue
        node = next(iter(component))
        anchors = {int(neighbor) for neighbor in graph.neighbors(node) if neighbor in core3}
        records.append((node, anchors))
    records.sort(key=lambda record: (-len(record[1]), record[0]))

    original = v34.layout_positions(layout)
    positions = v34.read_positions_tsv(args.core_positions, layout)
    placed = set(core3)
    active_edges = [
        (int(source), int(target))
        for source, target in edges
        if int(source) in placed and int(target) in placed
    ]
    rng = np.random.default_rng(args.seed)
    added_crossings = 0
    started = time.time()
    for record_index, (node, anchors) in enumerate(records):
        new_edges = [
            (int(source), int(target))
            for source, target in edges
            if (
                (int(source) == node and int(target) in placed)
                or (int(target) == node and int(source) in placed)
            )
        ]
        placed_points = positions[np.asarray(sorted(placed), dtype=np.int32)]
        low = placed_points.min(axis=0)
        high = placed_points.max(axis=0)
        span = np.maximum(high - low, 1e-6)
        anchor_points = positions[np.asarray(sorted(anchors), dtype=np.int32)]
        anchor_center = anchor_points.mean(axis=0)
        anchor_radius = max(
            float(np.linalg.norm(anchor_points - anchor_center, axis=1).max()),
            float(np.linalg.norm(span)) * 0.05,
        )
        candidate_parts = [
            rng.uniform(
                low - span * 0.6,
                high + span * 0.6,
                size=(args.random_candidates, 2),
            ),
        ]
        for scale in (0.08, 0.18, 0.35, 0.7, 1.4, 2.8, 5.6):
            candidate_parts.append(
                anchor_center + rng.normal(
                    0.0,
                    anchor_radius * scale,
                    size=(args.anchor_candidates, 2),
                )
            )
        candidates = np.concatenate(candidate_parts, axis=0)
        min_distance = float(np.linalg.norm(span)) * args.min_distance_ratio
        keep_parts: list[np.ndarray] = []
        for start in range(0, candidates.shape[0], 256):
            trial = candidates[start:start + 256]
            nearest = np.linalg.norm(
                trial[:, None, :] - placed_points[None, :, :],
                axis=2,
            ).min(axis=1)
            keep_parts.append(nearest >= min_distance)
        candidates = candidates[np.concatenate(keep_parts)]
        counts = candidate_crossings(
            node,
            candidates,
            new_edges,
            active_edges,
            positions,
        )
        relationship_length = sum(
            np.linalg.norm(candidates - positions[anchor], axis=1)
            for anchor in anchors
        )
        best_index = int(np.lexsort((relationship_length, counts))[0])
        positions[node] = candidates[best_index]
        placed.add(node)
        active_edges.extend(new_edges)
        added_crossings += int(counts[best_index])
        if (record_index + 1) % 20 == 0:
            print(
                f"placed={record_index + 1}/{len(records)} "
                f"addedCross={added_crossings} activeEdges={len(active_edges)} "
                f"elapsed={time.time() - started:.1f}s",
                flush=True,
            )
    # Keep every not-yet-optimized real node at its original coordinate. The
    # output remains a complete position file and can be expanded component by
    # component without ever inventing a relationship endpoint.
    unplaced = np.asarray(sorted(set(range(len(layout["nodes"]))) - placed), dtype=np.int32)
    positions[unplaced] = original[unplaced]
    args.out_tsv.parent.mkdir(parents=True, exist_ok=True)
    v34.write_positions_tsv(args.out_tsv, layout, positions)
    active_array = np.asarray(active_edges, dtype=np.int32)
    crossing_count = v34.fce.FastCrossEval(
        active_array,
        len(layout["nodes"]),
    ).count_crossings(positions)
    print(
        f"done coreNodes={len(core3)} singleNodeBundles={len(records)} "
        f"activeEdges={len(active_edges)} crossings={crossing_count} "
        f"out={args.out_tsv}",
        flush=True,
    )


if __name__ == "__main__":
    main()
