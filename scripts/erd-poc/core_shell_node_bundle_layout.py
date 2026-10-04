#!/usr/bin/env python3
"""Expand real shell-node bundles around a searched 3-core placement.

The giant biconnected block has a 131-node 3-core and 148 connected shell
components. Most shell components attach to exactly two 3-core nodes. Each such
component is kept as a bundle of its *real model nodes* and placed in its own
corridor between those two real anchors. No relationship is contracted or
represented by a proxy edge.
"""

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


def planar_positions(graph: nx.Graph) -> dict[int, np.ndarray]:
    nodes = sorted(int(node) for node in graph)
    if len(nodes) == 1:
        return {nodes[0]: np.zeros(2, dtype=np.float64)}
    if len(nodes) == 2:
        return {
            nodes[0]: np.array([-0.5, 0.0], dtype=np.float64),
            nodes[1]: np.array([0.5, 0.0], dtype=np.float64),
        }
    planar, embedding = nx.check_planarity(graph, counterexample=False)
    if not planar:
        raw = nx.spring_layout(graph, seed=42, iterations=1000, weight=None)
    else:
        raw = nx.combinatorial_embedding_to_pos(embedding, fully_triangulate=True)
    return {
        int(node): np.asarray(point, dtype=np.float64)
        for node, point in raw.items()
    }


def component_with_anchors_graph(
    graph: nx.Graph,
    component: set[int],
    anchors: set[int],
) -> nx.Graph:
    members = component | anchors
    subgraph = nx.Graph()
    subgraph.add_nodes_from(members)
    subgraph.add_edges_from(
        (source, target)
        for source, target in graph.subgraph(members).edges()
        if source in component or target in component
    )
    return subgraph


def two_anchor_bundle_positions(
    graph: nx.Graph,
    component: set[int],
    source: int,
    target: int,
    positions: np.ndarray,
    lane_offset: float,
    corridor_fraction: float,
) -> dict[int, np.ndarray]:
    subgraph = component_with_anchors_graph(graph, component, {source, target})
    raw = planar_positions(subgraph)
    raw_source = raw[source]
    raw_target = raw[target]
    raw_direction = raw_target - raw_source
    raw_length = float(np.linalg.norm(raw_direction))
    if raw_length <= 1e-9:
        raw_direction = np.array([1.0, 0.0])
        raw_length = 1.0
    raw_axis = raw_direction / raw_length
    raw_perpendicular = np.array([-raw_axis[1], raw_axis[0]])

    target_source = positions[source]
    target_target = positions[target]
    target_direction = target_target - target_source
    target_length = float(np.linalg.norm(target_direction))
    if target_length <= 1e-9:
        target_direction = np.array([1.0, 0.0])
        target_length = 1.0
    target_axis = target_direction / target_length
    target_perpendicular = np.array([-target_axis[1], target_axis[0]])

    raw_perp = {
        node: float(np.dot(point - raw_source, raw_perpendicular))
        for node, point in raw.items()
    }
    raw_perp_scale = max(
        1e-9,
        max((abs(value) for node, value in raw_perp.items() if node in component), default=1.0),
    )
    distance_from_source = nx.single_source_shortest_path_length(subgraph, source)
    distance_from_target = nx.single_source_shortest_path_length(subgraph, target)
    result: dict[int, np.ndarray] = {}
    for node in component:
        left_distance = float(distance_from_source.get(node, 1))
        right_distance = float(distance_from_target.get(node, 1))
        denominator = left_distance + right_distance
        along = left_distance / denominator if denominator > 0 else 0.5
        # Keep every real shell node between the two anchor planes. The smooth
        # sine taper makes a lane offset vanish exactly at both real anchors.
        taper = math.sin(math.pi * min(1.0, max(0.0, along)))
        local_perp = raw_perp.get(node, 0.0) / raw_perp_scale
        perpendicular_distance = (
            lane_offset * taper
            + local_perp * target_length * corridor_fraction * taper
        )
        result[node] = (
            target_source
            + target_direction * along
            + target_perpendicular * perpendicular_distance
        )
    return result


def complex_bundle_positions(
    graph: nx.Graph,
    component: set[int],
    anchors: set[int],
    positions: np.ndarray,
    seed: int,
) -> dict[int, np.ndarray]:
    subgraph = component_with_anchors_graph(graph, component, anchors)
    anchor_center = positions[sorted(anchors)].mean(axis=0)
    span = max(
        1000.0,
        float(np.linalg.norm(
            positions[sorted(anchors)] - anchor_center,
            axis=1,
        ).max()),
    )
    initial: dict[int, np.ndarray] = {
        anchor: positions[anchor].copy()
        for anchor in anchors
    }
    rng = np.random.default_rng(seed)
    for node in component:
        initial[node] = anchor_center + rng.normal(0.0, span * 0.12, size=2)
    raw = nx.spring_layout(
        subgraph,
        pos=initial,
        fixed=sorted(anchors),
        seed=seed,
        iterations=1500,
        threshold=1e-6,
        k=span * 0.28 / max(1.0, math.sqrt(len(component))),
        weight=None,
        scale=None,
    )
    return {
        node: np.asarray(raw[node], dtype=np.float64)
        for node in component
    }


def place_shell_bundles(
    graph: nx.Graph,
    giant: set[int],
    core3: set[int],
    positions: np.ndarray,
    lane_gap: float,
    corridor_fraction: float,
    seed: int,
) -> tuple[int, int, list[np.ndarray]]:
    shell = giant - core3
    records: list[tuple[set[int], set[int]]] = []
    for component_raw in nx.connected_components(graph.subgraph(shell)):
        component = set(int(node) for node in component_raw)
        anchors = {
            int(neighbor)
            for node in component
            for neighbor in graph.neighbors(node)
            if neighbor in core3
        }
        records.append((component, anchors))

    two_anchor_by_pair: dict[tuple[int, int], list[set[int]]] = defaultdict(list)
    complex_records: list[tuple[set[int], set[int]]] = []
    for component, anchors in records:
        if len(anchors) == 2:
            two_anchor_by_pair[tuple(sorted(anchors))].append(component)
        else:
            complex_records.append((component, anchors))

    groups: list[np.ndarray] = []
    placed_two = 0
    for pair, components in sorted(two_anchor_by_pair.items()):
        components.sort(key=lambda members: (len(members), min(members)))
        source, target = pair
        direct_pair = graph.has_edge(source, target)
        lane_count = len(components) + (1 if direct_pair else 0)
        first_lane = -(lane_count - 1) * 0.5
        lane_cursor = 0
        for component in components:
            if direct_pair and lane_cursor >= lane_count // 2:
                lane_cursor += 1
            lane = first_lane + lane_cursor
            lane_cursor += 1
            mapped = two_anchor_bundle_positions(
                graph,
                component,
                source,
                target,
                positions,
                lane * lane_gap,
                corridor_fraction,
            )
            for node, point in mapped.items():
                positions[node] = point
            groups.append(np.asarray(sorted(component), dtype=np.int32))
            placed_two += len(component)

    placed_complex = 0
    for record_index, (component, anchors) in enumerate(complex_records):
        if not anchors:
            continue
        mapped = complex_bundle_positions(
            graph,
            component,
            anchors,
            positions,
            seed + record_index * 1009,
        )
        for node, point in mapped.items():
            positions[node] = point
        groups.append(np.asarray(sorted(component), dtype=np.int32))
        placed_complex += len(component)
    return placed_two, placed_complex, groups


def attach_outside_giant(
    graph: nx.Graph,
    giant: set[int],
    old_positions: np.ndarray,
    positions: np.ndarray,
) -> tuple[int, list[set[int]]]:
    outside = set(graph) - giant
    attached = 0
    detached: list[set[int]] = []
    for component_raw in nx.connected_components(graph.subgraph(outside)):
        component = set(int(node) for node in component_raw)
        anchors = {
            int(neighbor)
            for node in component
            for neighbor in graph.neighbors(node)
            if neighbor in giant
        }
        if len(anchors) == 1:
            anchor = next(iter(anchors))
            indices = np.asarray(sorted(component), dtype=np.int32)
            positions[indices] = (
                old_positions[indices]
                + positions[anchor]
                - old_positions[anchor]
            )
            attached += len(component)
        else:
            detached.append(component)
    return attached, detached


def pack_detached(
    detached: list[set[int]],
    old_positions: np.ndarray,
    positions: np.ndarray,
    widths: np.ndarray,
    heights: np.ndarray,
    giant: set[int],
    gap: float,
) -> None:
    giant_nodes = np.asarray(sorted(giant), dtype=np.int32)
    right = float(np.max(positions[giant_nodes, 0] + widths[giant_nodes] / 2.0)) + gap
    top = float(np.min(positions[giant_nodes, 1] - heights[giant_nodes] / 2.0))
    cursor_y = top
    column_width = 0.0
    max_height = max(gap * 4.0, float(np.ptp(positions[giant_nodes, 1])) + gap)
    for component in sorted(detached, key=len, reverse=True):
        indices = np.asarray(sorted(component), dtype=np.int32)
        local = old_positions[indices].copy()
        local[:, 0] -= float(np.min(local[:, 0] - widths[indices] / 2.0))
        local[:, 1] -= float(np.min(local[:, 1] - heights[indices] / 2.0))
        width = float(np.max(local[:, 0] + widths[indices] / 2.0))
        height = float(np.max(local[:, 1] + heights[indices] / 2.0))
        if cursor_y > top and cursor_y + height > top + max_height:
            right += column_width + gap
            cursor_y = top
            column_width = 0.0
        positions[indices] = local + np.array([right, cursor_y])
        cursor_y += height + gap
        column_width = max(column_width, width)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--core-positions", type=Path, required=True)
    parser.add_argument("--out-tsv", type=Path, required=True)
    parser.add_argument("--core-scale", type=float, default=8.0)
    parser.add_argument("--lane-gap", type=float, default=900.0)
    parser.add_argument("--corridor-fraction", type=float, default=0.12)
    parser.add_argument("--component-gap", type=float, default=5000.0)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument(
        "--skip-measure",
        action="store_true",
        help=(
            "Write the structural node-bundle placement without allocating "
            "the dense all-edge-pair evaluator. Use the bounded native "
            "canonical scorer as a separate step."
        ),
    )
    args = parser.parse_args()

    layout = v34.load_layout(args.layout)
    old_positions = v34.layout_positions(layout)
    positions = old_positions.copy()
    positions = v34.read_positions_tsv(args.core_positions, {
        **layout,
        "nodes": layout["nodes"],
    })
    edges = v34.graph_edges(layout)
    graph = nx.Graph()
    graph.add_nodes_from(range(len(layout["nodes"])))
    graph.add_edges_from((int(source), int(target)) for source, target in edges)
    giant = set(int(node) for node in max(nx.biconnected_components(graph), key=len))
    giant_graph = graph.subgraph(giant).copy()
    core_numbers = nx.core_number(giant_graph)
    core3 = {node for node, value in core_numbers.items() if value >= 3}
    core_nodes = np.asarray(sorted(core3), dtype=np.int32)
    core_center = positions[core_nodes].mean(axis=0)
    positions[core_nodes] = (
        (positions[core_nodes] - core_center) * args.core_scale + core_center
    )
    placed_two, placed_complex, shell_groups = place_shell_bundles(
        graph,
        giant,
        core3,
        positions,
        args.lane_gap,
        args.corridor_fraction,
        args.seed,
    )
    attached, detached = attach_outside_giant(
        graph,
        giant,
        old_positions,
        positions,
    )
    widths, heights = v34.render_node_sizes(layout, edges, direct_scene=True)
    pack_detached(
        detached,
        old_positions,
        positions,
        widths,
        heights,
        giant,
        args.component_gap,
    )

    args.out_tsv.parent.mkdir(parents=True, exist_ok=True)
    v34.write_positions_tsv(args.out_tsv, layout, positions)
    if args.skip_measure:
        print(
            f"result core3={len(core3)} shellGroups={len(shell_groups)} "
            f"shellTwoNodes={placed_two} shellComplexNodes={placed_complex} "
            f"outsideAttached={attached} detachedComponents={len(detached)} "
            f"measure=skipped out={args.out_tsv}"
        )
        return

    evaluator = v34.fce.FastCrossEval(edges, positions.shape[0])
    crossings = evaluator.count_crossings(positions)
    collision_geometry = v34.build_render_collision_geometry(
        layout,
        edges,
        count_bundle_nodes=True,
        direct_scene=True,
    )
    measured = v34.measure(
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
    giant_edge_mask = np.asarray([
        int(source) in giant and int(target) in giant
        for source, target in edges
    ])
    giant_edges = edges[giant_edge_mask]
    giant_evaluator = v34.fce.FastCrossEval(giant_edges, positions.shape[0])
    giant_crossings = giant_evaluator.count_crossings(positions)
    print(
        f"result core3={len(core3)} shellGroups={len(shell_groups)} "
        f"shellTwoNodes={placed_two} shellComplexNodes={placed_complex} "
        f"outsideAttached={attached} detachedComponents={len(detached)} "
        f"giantCross={giant_crossings} cross={crossings} "
        f"edgeNode={measured.edge_node} overlaps={measured.overlaps} "
        f"visual={crossings + measured.edge_node} bboxB={measured.bbox_b:.3f} "
        f"out={args.out_tsv}"
    )


if __name__ == "__main__":
    main()
