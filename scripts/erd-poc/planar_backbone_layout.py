#!/usr/bin/env python3
"""Build a straight-line layout from a maximal planar graph backbone.

All original nodes and relationships remain in the output.  Planarity is used
only to choose node coordinates: edges rejected from the backbone are restored
as ordinary straight relationships by the normal direct-scene evaluator.
"""

from __future__ import annotations

import argparse
import importlib.util
import math
import random
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


v34 = load_module(
    "v34_move_search",
    ROOT / "scripts/erd-poc/v34_move_search.py",
)


def edge_order(
    graph: nx.Graph,
    edges: list[tuple[int, int]],
    strategy: str,
    seed: int,
) -> list[tuple[int, int]]:
    degree = dict(graph.degree())
    core = nx.core_number(graph)
    rng = random.Random(seed)
    tie = {tuple(sorted(edge)): rng.random() for edge in edges}

    if strategy == "random":
        ordered = list(edges)
        rng.shuffle(ordered)
        return ordered
    if strategy == "input":
        return list(edges)

    def key(edge: tuple[int, int]):
        u, v = edge
        shared_core = min(core[u], core[v])
        degree_sum = degree[u] + degree[v]
        degree_max = max(degree[u], degree[v])
        random_tie = tie[tuple(sorted(edge))]
        if strategy == "core":
            return (shared_core, degree_sum, degree_max, random_tie)
        if strategy == "periphery":
            return (-shared_core, -degree_sum, -degree_max, random_tie)
        return (degree_max, degree_sum, shared_core, random_tie)

    return sorted(edges, key=key, reverse=True)


def maximal_planar_backbone(
    graph: nx.Graph,
    strategy: str,
    seed: int,
) -> tuple[nx.Graph, list[tuple[int, int]]]:
    backbone = nx.Graph()
    backbone.add_nodes_from(graph.nodes())
    rejected: list[tuple[int, int]] = []
    ordered = edge_order(graph, list(graph.edges()), strategy, seed)
    checked = 0
    started = time.time()
    for u, v in ordered:
        backbone.add_edge(u, v)
        planar, _embedding = nx.check_planarity(backbone, counterexample=False)
        checked += 1
        if not planar:
            backbone.remove_edge(u, v)
            rejected.append((u, v))
        if checked % 250 == 0:
            print(
                f"backbone checked={checked}/{len(ordered)} "
                f"kept={backbone.number_of_edges()} rejected={len(rejected)} "
                f"elapsed={time.time() - started:.1f}s",
                flush=True,
            )
    return backbone, rejected


def component_planar_positions(
    backbone: nx.Graph,
    component: set[int],
) -> dict[int, np.ndarray]:
    subgraph = backbone.subgraph(component).copy()
    count = subgraph.number_of_nodes()
    if count == 1:
        node = next(iter(subgraph.nodes()))
        return {node: np.array([0.0, 0.0], dtype=np.float64)}
    if count == 2 and subgraph.number_of_edges() <= 1:
        nodes = list(subgraph.nodes())
        return {
            nodes[0]: np.array([0.0, 0.0], dtype=np.float64),
            nodes[1]: np.array([1.0, 0.0], dtype=np.float64),
        }
    planar, embedding = nx.check_planarity(subgraph, counterexample=False)
    if not planar:
        raise RuntimeError("backbone component unexpectedly became non-planar")
    raw = nx.combinatorial_embedding_to_pos(
        embedding,
        fully_triangulate=False,
    )
    return {
        int(node): np.asarray(point, dtype=np.float64)
        for node, point in raw.items()
    }


def point_segment_distances(
    points: np.ndarray,
    source: np.ndarray,
    target: np.ndarray,
) -> np.ndarray:
    delta = target - source
    denom = float(np.dot(delta, delta))
    if denom <= 1e-18:
        return np.linalg.norm(points - source, axis=1)
    along = np.clip(((points - source) @ delta) / denom, 0.0, 1.0)
    closest = source + along[:, None] * delta
    return np.linalg.norm(points - closest, axis=1)


def clearance_scale(
    local_nodes: list[int],
    local_positions: np.ndarray,
    graph: nx.Graph,
    widths: np.ndarray,
    heights: np.ndarray,
    base_spacing: float,
    margin: float,
) -> tuple[float, int]:
    """Conservative uniform scale for rectangle and edge-node clearance."""
    required = max(1.0, base_spacing)
    exact_hits = 0
    local_index = {node: idx for idx, node in enumerate(local_nodes)}
    radii = np.hypot(widths[local_nodes], heights[local_nodes]) / 2.0 + margin

    for idx in range(len(local_nodes)):
        delta = local_positions[idx + 1 :] - local_positions[idx]
        distances = np.linalg.norm(delta, axis=1)
        if distances.size == 0:
            continue
        needs = radii[idx] + radii[idx + 1 :]
        positive = distances > 1e-12
        if np.any(positive):
            required = max(required, float(np.max(needs[positive] / distances[positive])))

    node_array = np.asarray(local_nodes, dtype=np.int32)
    for u, v in graph.subgraph(local_nodes).edges():
        source_idx = local_index[int(u)]
        target_idx = local_index[int(v)]
        distances = point_segment_distances(
            local_positions,
            local_positions[source_idx],
            local_positions[target_idx],
        )
        mask = (node_array != int(u)) & (node_array != int(v))
        positive = mask & (distances > 1e-12)
        if np.any(positive):
            required = max(
                required,
                float(np.max(radii[positive] / distances[positive])),
            )
        exact_hits += int(np.count_nonzero(mask & (distances <= 1e-12)))
    return required * 1.05, exact_hits


def pack_components(
    graph: nx.Graph,
    backbone: nx.Graph,
    widths: np.ndarray,
    heights: np.ndarray,
    base_spacing: float,
    component_gap: float,
    margin: float,
) -> tuple[np.ndarray, int]:
    positions = np.zeros((graph.number_of_nodes(), 2), dtype=np.float64)
    components = sorted(nx.connected_components(graph), key=len, reverse=True)
    cursor_x = 0.0
    exact_hits = 0
    for component_idx, component in enumerate(components):
        raw = component_planar_positions(backbone, component)
        nodes = sorted(component)
        local = np.asarray([raw[node] for node in nodes], dtype=np.float64)
        local -= local.mean(axis=0)
        scale, component_hits = clearance_scale(
            nodes,
            local,
            graph,
            widths,
            heights,
            base_spacing,
            margin,
        )
        local *= scale
        half_width = widths[nodes] / 2.0
        half_height = heights[nodes] / 2.0
        min_x = float(np.min(local[:, 0] - half_width))
        max_x = float(np.max(local[:, 0] + half_width))
        min_y = float(np.min(local[:, 1] - half_height))
        local[:, 0] += cursor_x - min_x
        local[:, 1] += -min_y
        positions[nodes] = local
        cursor_x += max_x - min_x + component_gap
        exact_hits += component_hits
        if component_idx < 12 or component_hits:
            print(
                f"component={component_idx} nodes={len(nodes)} "
                f"scale={scale:.1f} exactEdgeNodeHits={component_hits}",
                flush=True,
            )
    return positions, exact_hits


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--out-tsv", type=Path, required=True)
    parser.add_argument(
        "--strategy",
        choices=("degree", "core", "periphery", "random", "input"),
        default="degree",
    )
    parser.add_argument("--seed", type=int, default=43)
    parser.add_argument("--base-spacing", type=float, default=900.0)
    parser.add_argument("--component-gap", type=float, default=5000.0)
    parser.add_argument("--clearance-margin", type=float, default=24.0)
    args = parser.parse_args()

    layout = v34.load_layout(args.layout)
    edges = v34.graph_edges(layout)
    graph = nx.Graph()
    graph.add_nodes_from(range(len(layout["nodes"])))
    graph.add_edges_from((int(s), int(t)) for s, t in edges)
    parallel_relationships = int(edges.shape[0] - graph.number_of_edges())

    widths, heights = v34.render_node_sizes(layout, edges, direct_scene=True)
    print(
        f"graph nodes={graph.number_of_nodes()} edges={graph.number_of_edges()} "
        f"parallelRelationships={parallel_relationships} "
        f"components={nx.number_connected_components(graph)}",
        flush=True,
    )
    backbone, rejected = maximal_planar_backbone(
        graph,
        args.strategy,
        args.seed,
    )
    positions, exact_hits = pack_components(
        graph,
        backbone,
        widths,
        heights,
        args.base_spacing,
        args.component_gap,
        args.clearance_margin,
    )
    args.out_tsv.parent.mkdir(parents=True, exist_ok=True)
    v34.write_positions_tsv(args.out_tsv, layout, positions)
    print(
        f"done kept={backbone.number_of_edges()} rejected={len(rejected)} "
        f"exactEdgeNodeHitsBeforeScale={exact_hits} wrote={args.out_tsv}",
        flush=True,
    )


if __name__ == "__main__":
    main()
