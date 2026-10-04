#!/usr/bin/env python3
"""Expand optimizer-only anchor families back to every real model node."""

from __future__ import annotations

import argparse
import math
import resource
import sys
from collections import defaultdict, deque
from pathlib import Path

import networkx as nx


Point = tuple[float, float]


def read_nodes(path: Path) -> tuple[list[str], dict[str, tuple[float, float]]]:
    order: list[str] = []
    sizes: dict[str, tuple[float, float]] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        if not raw:
            continue
        model_id, width, height, *_ = raw.split("\t")
        order.append(model_id)
        sizes[model_id] = (float(width), float(height))
    return order, sizes


def read_edges(path: Path) -> list[tuple[str, str]]:
    result: list[tuple[str, str]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        if not raw:
            continue
        fields = raw.split("\t")
        if len(fields) >= 3 and fields[1] != fields[2]:
            result.append((fields[1], fields[2]))
    return result


def read_positions(path: Path) -> dict[str, Point]:
    result: dict[str, Point] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        if not raw:
            continue
        model_id, x, y, *_ = raw.split("\t")
        result[model_id] = (float(x), float(y))
    return result


def read_assignments(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        if not raw:
            continue
        node, anchor = raw.split("\t", 1)
        if anchor:
            result[node] = anchor
    return result


def rotate(point: Point, angle: float) -> Point:
    cosine = math.cos(angle)
    sine = math.sin(angle)
    return (
        point[0] * cosine - point[1] * sine,
        point[0] * sine + point[1] * cosine,
    )


def local_planar_positions(graph: nx.Graph, anchor: str) -> dict[str, Point]:
    if graph.number_of_nodes() == 1:
        return {anchor: (0.0, 0.0)}
    planar, embedding = nx.check_planarity(graph, counterexample=False)
    if planar:
        raw = nx.combinatorial_embedding_to_pos(embedding, fully_triangulate=True)
    else:
        raw = nx.spring_layout(graph, seed=42, iterations=500, weight=None)
    anchor_point = raw[anchor]
    return {
        node: (
            float(point[0] - anchor_point[0]),
            float(point[1] - anchor_point[1]),
        )
        for node, point in raw.items()
    }


def scaled_without_card_overlap(
    raw: dict[str, Point],
    sizes: dict[str, tuple[float, float]],
    gap: float,
) -> dict[str, Point]:
    nodes = sorted(raw)
    if len(nodes) <= 1:
        return dict(raw)
    scale = 1.0
    for _ in range(80):
        overlap = False
        for left_index, left in enumerate(nodes):
            for right in nodes[left_index + 1 :]:
                dx = abs(raw[left][0] - raw[right][0]) * scale
                dy = abs(raw[left][1] - raw[right][1]) * scale
                if (
                    dx < (sizes[left][0] + sizes[right][0]) * 0.5 + gap
                    and dy < (sizes[left][1] + sizes[right][1]) * 0.5 + gap
                ):
                    overlap = True
                    break
            if overlap:
                break
        if not overlap:
            break
        scale *= 1.35
    return {node: (point[0] * scale, point[1] * scale) for node, point in raw.items()}


def bounds(
    positions: dict[str, Point],
    sizes: dict[str, tuple[float, float]],
) -> tuple[float, float, float, float]:
    return (
        min(positions[node][0] - sizes[node][0] * 0.5 for node in positions),
        min(positions[node][1] - sizes[node][1] * 0.5 for node in positions),
        max(positions[node][0] + sizes[node][0] * 0.5 for node in positions),
        max(positions[node][1] + sizes[node][1] * 0.5 for node in positions),
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nodes", type=Path, required=True)
    parser.add_argument("--edges", type=Path, required=True)
    parser.add_argument("--assignments", type=Path, required=True)
    parser.add_argument("--anchor-positions", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--card-gap", type=float, default=24.0)
    parser.add_argument("--family-gap", type=float, default=180.0)
    args = parser.parse_args()

    node_order, sizes = read_nodes(args.nodes)
    edges = read_edges(args.edges)
    assignment = read_assignments(args.assignments)
    anchor_positions = read_positions(args.anchor_positions)
    graph = nx.Graph()
    graph.add_nodes_from(node_order)
    graph.add_edges_from(edges)
    families: dict[str, list[str]] = defaultdict(list)
    for node, anchor in assignment.items():
        families[anchor].append(node)

    local_by_anchor: dict[str, dict[str, Point]] = {}
    local_bounds: dict[str, tuple[float, float, float, float]] = {}
    nonplanar_families = 0
    for anchor in sorted(families):
        members = sorted(families[anchor])
        family_graph = graph.subgraph(members).copy()
        planar, _ = nx.check_planarity(family_graph, counterexample=False)
        if not planar:
            nonplanar_families += 1
        local = local_planar_positions(family_graph, anchor)
        local = scaled_without_card_overlap(local, sizes, args.card_gap)

        # A single rigid rotation lets members with differing relationships
        # face their actual target families.  No member is excluded merely
        # because its extra relationships differ from its neighbours'.
        real_sum = 0.0
        imaginary_sum = 0.0
        member_set = set(members)
        for source, target in edges:
            if source in member_set and target not in member_set:
                node, other = source, target
            elif target in member_set and source not in member_set:
                node, other = target, source
            else:
                continue
            other_anchor = assignment.get(other)
            if other_anchor is None or other_anchor == anchor:
                continue
            point = local[node]
            if abs(point[0]) + abs(point[1]) < 1e-9:
                continue
            desired = (
                anchor_positions[other_anchor][0] - anchor_positions[anchor][0],
                anchor_positions[other_anchor][1] - anchor_positions[anchor][1],
            )
            delta = math.atan2(desired[1], desired[0]) - math.atan2(point[1], point[0])
            real_sum += math.cos(delta)
            imaginary_sum += math.sin(delta)
        angle = math.atan2(imaginary_sum, real_sum) if real_sum or imaginary_sum else 0.0
        local = {node: rotate(point, angle) for node, point in local.items()}
        local = scaled_without_card_overlap(local, sizes, args.card_gap)
        local_by_anchor[anchor] = local
        local_bounds[anchor] = bounds(local, sizes)

    # Uniformly expand the optimizer's real-anchor layout only as much as the
    # visible family rectangles require.  This is card clearance, not an
    # arbitrary score-improving scale multiplier.
    anchor_scale = 1.0
    anchors = sorted(families)
    for _ in range(80):
        collision = False
        for left_index, left in enumerate(anchors):
            left_bounds = local_bounds[left]
            for right in anchors[left_index + 1 :]:
                right_bounds = local_bounds[right]
                dx = (anchor_positions[right][0] - anchor_positions[left][0]) * anchor_scale
                dy = (anchor_positions[right][1] - anchor_positions[left][1]) * anchor_scale
                separated = (
                    left_bounds[2] + args.family_gap < right_bounds[0] + dx
                    or right_bounds[2] + dx + args.family_gap < left_bounds[0]
                    or left_bounds[3] + args.family_gap < right_bounds[1] + dy
                    or right_bounds[3] + dy + args.family_gap < left_bounds[1]
                )
                if not separated:
                    collision = True
                    break
            if collision:
                break
        if not collision:
            break
        anchor_scale *= 1.2

    result: dict[str, Point] = {}
    for anchor, local in local_by_anchor.items():
        origin = (
            anchor_positions[anchor][0] * anchor_scale,
            anchor_positions[anchor][1] * anchor_scale,
        )
        for node, point in local.items():
            result[node] = (origin[0] + point[0], origin[1] + point[1])

    # Components not connected to the selected core cannot cross the anchored
    # graph.  Lay each one out independently and pack it to the right.
    detached = set(node_order) - set(result)
    if result:
        main_bounds = bounds(result, sizes)
        cursor_x = main_bounds[2] + args.family_gap * 4.0
        cursor_y = main_bounds[1]
        column_width = 0.0
        column_limit = max(4000.0, main_bounds[3] - main_bounds[1])
    else:
        cursor_x = cursor_y = 0.0
        column_width = 0.0
        column_limit = 10000.0
    for component in sorted(
        nx.connected_components(graph.subgraph(detached)),
        key=lambda value: (-len(value), min(value)),
    ):
        component_graph = graph.subgraph(component).copy()
        component_anchor = min(component)
        local = scaled_without_card_overlap(
            local_planar_positions(component_graph, component_anchor),
            sizes,
            args.card_gap,
        )
        local_box = bounds(local, sizes)
        width = local_box[2] - local_box[0]
        height = local_box[3] - local_box[1]
        if cursor_y > main_bounds[1] and cursor_y + height > main_bounds[1] + column_limit:
            cursor_x += column_width + args.family_gap * 2.0
            cursor_y = main_bounds[1]
            column_width = 0.0
        for node, point in local.items():
            result[node] = (
                cursor_x + point[0] - local_box[0],
                cursor_y + point[1] - local_box[1],
            )
        cursor_y += height + args.family_gap
        column_width = max(column_width, width)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        "".join(f"{node}\t{result[node][0]:.9f}\t{result[node][1]:.9f}\n" for node in node_order),
        encoding="utf-8",
    )
    peak_raw = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    peak_mib = peak_raw / (1024.0 * 1024.0) if sys.platform == "darwin" else peak_raw / 1024.0
    print(
        f"families={len(families)} covered={len(assignment)} detached={len(detached)} "
        f"nonplanarFamilies={nonplanar_families} anchorScale={anchor_scale:.3f} "
        f"peakMiB={peak_mib:.1f} out={args.out}"
    )


if __name__ == "__main__":
    main()
