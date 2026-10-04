#!/usr/bin/env python3
"""Pack real degree-2 model nodes into anchor-to-anchor node bundles."""

from __future__ import annotations

import argparse
import importlib.util
import math
import sys
from collections import defaultdict
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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--positions", type=Path, default=None)
    parser.add_argument("--out-tsv", type=Path, required=True)
    parser.add_argument("--spacing", type=float, default=260.0)
    parser.add_argument("--min-component", type=int, default=3)
    args = parser.parse_args()

    layout = v34.load_layout(args.layout)
    positions = (
        v34.read_positions_tsv(args.positions, layout)
        if args.positions
        else v34.layout_positions(layout)
    )
    edges = v34.graph_edges(layout)
    graph = nx.Graph()
    graph.add_nodes_from(range(len(layout["nodes"])))
    graph.add_edges_from((int(source), int(target)) for source, target in edges)

    moved = positions.copy()
    path_count = 0
    member_count = 0
    parallel_group_count = 0
    for component_nodes in nx.connected_components(graph):
        if len(component_nodes) < args.min_component:
            continue
        component = graph.subgraph(component_nodes).copy()
        paths = degree_two_paths(component)
        by_pair: dict[tuple[int, int], list[list[int]]] = defaultdict(list)
        for path in paths:
            if len(path) <= 2 or path[0] == path[-1]:
                continue
            if path[0] > path[-1]:
                path = list(reversed(path))
            by_pair[(path[0], path[-1])].append(path)
        for (source, target), pair_paths in by_pair.items():
            direction = positions[target] - positions[source]
            distance = float(np.linalg.norm(direction))
            if distance <= 1e-6:
                continue
            perpendicular = np.array([-direction[1], direction[0]]) / distance
            # Preserve the current side-to-side order of parallel paths.
            pair_paths.sort(
                key=lambda path: float(
                    np.dot(
                        positions[path[1:-1]].mean(axis=0)
                        - (positions[source] + positions[target]) * 0.5,
                        perpendicular,
                    )
                )
            )
            if len(pair_paths) >= 2:
                parallel_group_count += 1
            for rank, path in enumerate(pair_paths):
                offset = (rank - (len(pair_paths) - 1) * 0.5) * args.spacing
                internal = path[1:-1]
                for index, node in enumerate(internal, start=1):
                    fraction = index / (len(internal) + 1)
                    moved[node] = (
                        positions[source]
                        + direction * fraction
                        + perpendicular * offset
                    )
                path_count += 1
                member_count += len(internal)

    write_positions(args.out_tsv, layout, moved)
    measured = measure(layout, edges, moved)
    print(
        f"spacing={args.spacing:g} paths={path_count} members={member_count} "
        f"parallelGroups={parallel_group_count} crossings={measured.cross} "
        f"edgeNode={measured.edge_node} overlaps={measured.overlaps} "
        f"visual={measured.visual_cross}"
    )


def degree_two_paths(graph: nx.Graph) -> list[list[int]]:
    anchors = {node for node, degree in graph.degree() if degree != 2}
    visited_edges: set[tuple[int, int]] = set()
    paths: list[list[int]] = []
    for start in sorted(anchors):
        for neighbor in sorted(graph.neighbors(start)):
            edge_key = tuple(sorted((start, neighbor)))
            if edge_key in visited_edges:
                continue
            visited_edges.add(edge_key)
            path = [start, neighbor]
            previous = start
            current = neighbor
            while current not in anchors:
                candidates = [node for node in graph.neighbors(current) if node != previous]
                if len(candidates) != 1:
                    break
                next_node = candidates[0]
                edge_key = tuple(sorted((current, next_node)))
                if edge_key in visited_edges:
                    break
                visited_edges.add(edge_key)
                path.append(next_node)
                previous, current = current, next_node
            paths.append(path)
    return paths


def measure(layout: dict, edges: np.ndarray, positions: np.ndarray):
    evaluator = v34.fce.FastCrossEval(edges, positions.shape[0])
    collision_geometry = v34.build_render_collision_geometry(
        layout,
        edges,
        count_bundle_nodes=True,
        direct_scene=True,
    )
    return v34.measure(
        positions,
        evaluator,
        collision_geometry.raw_node_widths,
        collision_geometry.raw_node_heights,
        np.ones(positions.shape[0], dtype=bool),
        collision_geometry.overlap_pairs,
        0.0,
        0.0,
        0.0,
        0.0,
        edge_node_weight=1.0,
        collision_geometry=collision_geometry,
    )


def write_positions(path: Path, layout: dict, positions: np.ndarray) -> None:
    rows = [
        f"{node['modelId']}\t{positions[index, 0]:.6f}\t{positions[index, 1]:.6f}"
        for index, node in enumerate(layout["nodes"])
    ]
    path.write_text("\n".join(rows) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
