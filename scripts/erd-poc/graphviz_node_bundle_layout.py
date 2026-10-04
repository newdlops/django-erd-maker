#!/usr/bin/env python3
"""Prototype direct-edge layouts with Graphviz node placement.

Graphviz is used only to place real model nodes. Every ERD relationship stays
one straight segment between its declared endpoints; Graphviz edge splines are
discarded.
"""

from __future__ import annotations

import argparse
import importlib.util
import subprocess
import sys
import tempfile
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
    parser.add_argument(
        "--engine",
        choices=("dot", "neato", "fdp", "sfdp", "circo", "osage"),
        required=True,
    )
    parser.add_argument("--out-tsv", type=Path, required=True)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--nodesep", type=float, default=0.35)
    parser.add_argument("--ranksep", type=float, default=0.7)
    parser.add_argument("--suppress-degree-two", action="store_true")
    parser.add_argument("--bundle-spacing", type=float, default=260.0)
    parser.add_argument(
        "--scope",
        choices=("all", "core3", "giant"),
        default="all",
        help="place only this induced set of real nodes",
    )
    args = parser.parse_args()

    layout = v34.load_layout(args.layout)
    edges = v34.graph_edges(layout)
    widths, heights = v34.render_node_sizes(layout, edges, direct_scene=True)
    graphviz_nodes = set(range(len(layout["nodes"])))
    graphviz_edges = [(int(source), int(target)) for source, target in edges]
    if args.scope != "all":
        scope_graph = nx.Graph()
        scope_graph.add_nodes_from(graphviz_nodes)
        scope_graph.add_edges_from(graphviz_edges)
        giant = set(max(nx.biconnected_components(scope_graph), key=len))
        if args.scope == "giant":
            graphviz_nodes = giant
        else:
            core_numbers = nx.core_number(scope_graph.subgraph(giant))
            graphviz_nodes = {
                node for node, value in core_numbers.items() if value >= 3
            }
        graphviz_edges = [
            (source, target)
            for source, target in graphviz_edges
            if source in graphviz_nodes and target in graphviz_nodes
        ]
    suppressed_paths: list[list[int]] = []
    if args.suppress_degree_two:
        graph = nx.Graph()
        graph.add_nodes_from(graphviz_nodes)
        graph.add_edges_from(graphviz_edges)
        suppressed_paths = [
            path for path in degree_two_paths(graph) if path[0] != path[-1]
        ]
        suppressed_nodes = {
            node for path in suppressed_paths for node in path[1:-1]
        }
        graphviz_nodes -= suppressed_nodes
        graphviz_edges = [
            (int(source), int(target))
            for source, target in edges
            if int(source) not in suppressed_nodes
            and int(target) not in suppressed_nodes
        ]
        graphviz_edges.extend(
            (path[0], path[-1])
            for path in suppressed_paths
            if len(path) > 2
        )
    dot_source = build_dot(
        graphviz_nodes,
        graphviz_edges,
        widths,
        heights,
        args.engine,
        args.seed,
        args.nodesep,
        args.ranksep,
    )
    with tempfile.TemporaryDirectory(prefix="djerd-graphviz-") as temp_dir:
        dot_path = Path(temp_dir) / "graph.dot"
        dot_path.write_text(dot_source, encoding="utf-8")
        completed = subprocess.run(
            [args.engine, "-Tplain", str(dot_path)],
            check=True,
            capture_output=True,
            text=True,
        )
    positions = parse_plain(
        completed.stdout,
        len(layout["nodes"]),
        require_all=args.scope == "all" and not args.suppress_degree_two,
    )
    if args.suppress_degree_two:
        expand_degree_two_paths(
            positions,
            suppressed_paths,
            args.bundle_spacing,
        )
    write_positions(args.out_tsv, layout, positions, graphviz_nodes)

    metric_nodes = sorted(graphviz_nodes)
    metric_index = {node: index for index, node in enumerate(metric_nodes)}
    metric_edges = np.asarray([
        (metric_index[source], metric_index[target])
        for source, target in graphviz_edges
    ], dtype=np.int32)
    metric_positions = positions[metric_nodes]
    evaluator = v34.fce.FastCrossEval(metric_edges, len(metric_nodes))
    crossings = evaluator.count_crossings(metric_positions)
    if args.scope == "all":
        collision_geometry = v34.build_render_collision_geometry(
            layout,
            edges,
            count_bundle_nodes=True,
            direct_scene=True,
        )
        measured = v34.measure(
            metric_positions,
            evaluator,
            collision_geometry.raw_node_widths,
            collision_geometry.raw_node_heights,
            np.ones(len(metric_nodes), dtype=bool),
            collision_geometry.overlap_pairs,
            0.0,
            0.0,
            0.0,
            0.0,
            edge_node_weight=1.0,
            collision_geometry=collision_geometry,
        )
    else:
        measured = v34.measure(
            metric_positions,
            evaluator,
            widths[metric_nodes],
            heights[metric_nodes],
            np.ones(len(metric_nodes), dtype=bool),
            (np.empty(0, dtype=np.int32), np.empty(0, dtype=np.int32)),
            0.0,
            0.0,
            0.0,
            0.0,
        )
    print(
        f"engine={args.engine} seed={args.seed} "
        f"suppressDegreeTwo={args.suppress_degree_two} "
        f"crossings={crossings} "
        f"edgeNode={measured.edge_node} overlaps={measured.overlaps} "
        f"visual={crossings + measured.edge_node}"
    )


def build_dot(
    node_indices: set[int],
    edges: list[tuple[int, int]],
    widths: np.ndarray,
    heights: np.ndarray,
    engine: str,
    seed: int,
    nodesep: float,
    ranksep: float,
) -> str:
    lines = [
        "graph G {",
        "  graph [",
        f"    layout={engine}, overlap=false, splines=line, pack=true, packmode=\"graph\",",
        f"    nodesep={nodesep}, ranksep={ranksep}, start={seed}, maxiter=5000",
        "  ];",
        "  node [shape=box, fixedsize=true, label=\"\", margin=0];",
        "  edge [weight=1, len=2];",
    ]
    for node in sorted(node_indices):
        lines.append(
            f"  n{node} [width={max(0.1, widths[node] / 72.0):.6f}, "
            f"height={max(0.1, heights[node] / 72.0):.6f}];"
        )
    for source, target in edges:
        lines.append(f"  n{int(source)} -- n{int(target)};")
    lines.append("}")
    return "\n".join(lines)


def parse_plain(
    output: str,
    node_count: int,
    require_all: bool = True,
) -> np.ndarray:
    positions = np.full((node_count, 2), np.nan, dtype=np.float64)
    for line in output.splitlines():
        parts = line.split()
        if len(parts) < 4 or parts[0] != "node" or not parts[1].startswith("n"):
            continue
        node = int(parts[1][1:])
        positions[node] = (float(parts[2]) * 72.0, -float(parts[3]) * 72.0)
    if require_all and not np.isfinite(positions).all():
        missing = np.flatnonzero(~np.isfinite(positions).all(axis=1))
        raise RuntimeError(f"Graphviz omitted {missing.size} nodes")
    return positions


def degree_two_paths(graph: nx.Graph) -> list[list[int]]:
    paths: list[list[int]] = []
    for component_nodes in nx.connected_components(graph):
        component = graph.subgraph(component_nodes)
        anchors = {node for node, degree in component.degree() if degree != 2}
        if not anchors:
            continue
        visited_edges: set[tuple[int, int]] = set()
        for start in sorted(anchors):
            for neighbor in sorted(component.neighbors(start)):
                edge_key = tuple(sorted((start, neighbor)))
                if edge_key in visited_edges:
                    continue
                visited_edges.add(edge_key)
                path = [start, neighbor]
                previous = start
                current = neighbor
                while current not in anchors:
                    candidates = [
                        node for node in component.neighbors(current) if node != previous
                    ]
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


def expand_degree_two_paths(
    positions: np.ndarray,
    paths: list[list[int]],
    spacing: float,
) -> None:
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
        if not np.isfinite(distance) or distance <= 1e-6:
            continue
        perpendicular = np.array([-direction[1], direction[0]]) / distance
        pair_paths.sort(key=lambda path: tuple(path))
        for rank, path in enumerate(pair_paths):
            offset = (rank - (len(pair_paths) - 1) * 0.5) * spacing
            internal = path[1:-1]
            for index, node in enumerate(internal, start=1):
                fraction = index / (len(internal) + 1)
                positions[node] = (
                    positions[source]
                    + direction * fraction
                    + perpendicular * offset
                )
    if not np.isfinite(positions).all():
        missing = np.flatnonzero(~np.isfinite(positions).all(axis=1))
        raise RuntimeError(f"degree-two expansion omitted {missing.size} nodes")


def write_positions(
    path: Path,
    layout: dict,
    positions: np.ndarray,
    included_nodes: set[int],
) -> None:
    rows = [
        f"{node['modelId']}\t{positions[idx, 0]:.6f}\t{positions[idx, 1]:.6f}"
        for idx, node in enumerate(layout["nodes"])
        if idx in included_nodes and np.isfinite(positions[idx]).all()
    ]
    path.write_text("\n".join(rows) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
