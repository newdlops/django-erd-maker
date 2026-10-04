#!/usr/bin/env python3
"""Warm-start a changed ERD by inserting only the changed real nodes.

Common model ids retain their proven center coordinates. New model nodes are
inserted near already placed graph neighbors using direct-segment crossing and
rectangle-clearance costs. Removed models simply disappear. No proxy node,
edge aggregation, bend, or data-set-specific id is introduced.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


def load_template(path: Path) -> dict[str, tuple[float, float]]:
    result: dict[str, tuple[float, float]] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        fields = line.split("\t")
        if fields and fields[0] == "N" and len(fields) >= 4:
            fields = fields[1:]
        if len(fields) < 3 or fields[0] == "modelId":
            continue
        try:
            result[fields[0]] = (float(fields[1]), float(fields[2]))
        except ValueError:
            continue
    return result


def stable_phase(value: str) -> float:
    state = 2166136261
    for byte in value.encode("utf-8"):
        state ^= byte
        state = (state * 16777619) & 0xFFFFFFFF
    return (state / 2**32) * math.tau


def orientation(a, b, c) -> float:
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def proper_cross(a, b, c, d) -> bool:
    first = orientation(a, b, c)
    second = orientation(a, b, d)
    third = orientation(c, d, a)
    fourth = orientation(c, d, b)
    return (
        first != 0.0
        and second != 0.0
        and third != 0.0
        and fourth != 0.0
        and (first < 0.0) != (second < 0.0)
        and (third < 0.0) != (fourth < 0.0)
    )


def rect(center, width: float, height: float, margin: float = 0.0):
    return (
        center[0] - width * 0.5 - margin,
        center[1] - height * 0.5 - margin,
        center[0] + width * 0.5 + margin,
        center[1] + height * 0.5 + margin,
    )


def rects_overlap(left, right) -> bool:
    return not (
        left[2] <= right[0]
        or right[2] <= left[0]
        or left[3] <= right[1]
        or right[3] <= left[1]
    )


def point_in_rect(point, box) -> bool:
    return box[0] < point[0] < box[2] and box[1] < point[1] < box[3]


def segment_intersects_rect(source, target, box) -> bool:
    if point_in_rect(source, box) or point_in_rect(target, box):
        return True
    corners = (
        (box[0], box[1]),
        (box[2], box[1]),
        (box[2], box[3]),
        (box[0], box[3]),
    )
    return any(
        proper_cross(source, target, corners[index], corners[(index + 1) % 4])
        for index in range(4)
    )


def candidate_points(
    model_id: str,
    neighbors: list[int],
    positions: list[tuple[float, float] | None],
    widths: list[float],
    heights: list[float],
    node_index: int,
) -> list[tuple[float, float]]:
    neighbor_positions = [positions[index] for index in neighbors if positions[index] is not None]
    assert neighbor_positions
    base = (
        sum(point[0] for point in neighbor_positions) / len(neighbor_positions),
        sum(point[1] for point in neighbor_positions) / len(neighbor_positions),
    )
    clearance = max(widths[node_index], heights[node_index]) * 0.75 + 240.0
    if len(neighbor_positions) == 1:
        neighbor = neighbors[0]
        clearance += max(widths[neighbor], heights[neighbor]) * 0.55
    spread = max(
        (math.dist(base, point) for point in neighbor_positions),
        default=clearance,
    )
    unit = max(clearance, spread * 0.18)
    phase = stable_phase(model_id)
    candidates = [base]
    for point in neighbor_positions:
        candidates.append(((base[0] + point[0]) * 0.5, (base[1] + point[1]) * 0.5))
    for multiplier in (1.0, 1.75, 3.0, 5.0, 8.0, 13.0):
        radius = unit * multiplier
        samples = 32 if multiplier <= 5.0 else 48
        for sample in range(samples):
            angle = phase + math.tau * sample / samples
            candidates.append((
                base[0] + math.cos(angle) * radius,
                base[1] + math.sin(angle) * radius,
            ))
    return candidates


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--edges", type=Path, required=True)
    parser.add_argument("--template", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--margin", type=float, default=28.0)
    args = parser.parse_args()

    layout = json.loads(args.layout.read_text(encoding="utf-8"))
    model_ids = [str(node["modelId"]) for node in layout["nodes"]]
    index_by_id = {model_id: index for index, model_id in enumerate(model_ids)}
    widths = [float(node["size"]["width"]) for node in layout["nodes"]]
    heights = [float(node["size"]["height"]) for node in layout["nodes"]]
    template = load_template(args.template)
    positions: list[tuple[float, float] | None] = [
        template.get(model_id) for model_id in model_ids
    ]

    edges: list[tuple[int, int]] = []
    adjacency: list[set[int]] = [set() for _ in model_ids]
    for line in args.edges.read_text(encoding="utf-8").splitlines():
        fields = line.split("\t")
        if len(fields) < 3:
            continue
        source = index_by_id.get(fields[1])
        target = index_by_id.get(fields[2])
        if source is None or target is None or source == target:
            continue
        edges.append((source, target))
        adjacency[source].add(target)
        adjacency[target].add(source)

    common = sum(position is not None for position in positions)
    pending = {index for index, position in enumerate(positions) if position is None}
    inserted = 0
    while pending:
        node_index = max(
            pending,
            key=lambda index: (
                sum(positions[neighbor] is not None for neighbor in adjacency[index]),
                len(adjacency[index]),
                model_ids[index],
            ),
        )
        placed_neighbors = [
            neighbor
            for neighbor in sorted(adjacency[node_index])
            if positions[neighbor] is not None
        ]
        if not placed_neighbors:
            placed_points = [point for point in positions if point is not None]
            right = max((point[0] for point in placed_points), default=0.0)
            top = min((point[1] for point in placed_points), default=0.0)
            positions[node_index] = (
                right + 1200.0,
                top + inserted * (heights[node_index] + 180.0),
            )
            pending.remove(node_index)
            inserted += 1
            continue

        placed_nodes = [index for index, point in enumerate(positions) if point is not None]
        placed_edges = [
            (source, target)
            for source, target in edges
            if positions[source] is not None and positions[target] is not None
        ]
        best_point = None
        best_cost = None
        for candidate in candidate_points(
            model_ids[node_index],
            placed_neighbors,
            positions,
            widths,
            heights,
            node_index,
        ):
            candidate_rect = rect(
                candidate, widths[node_index], heights[node_index], args.margin
            )
            overlaps = sum(
                rects_overlap(
                    candidate_rect,
                    rect(
                        positions[other],
                        widths[other],
                        heights[other],
                        args.margin,
                    ),
                )
                for other in placed_nodes
            )
            crossings = 0
            edge_node = 0
            length = 0.0
            for neighbor in placed_neighbors:
                neighbor_position = positions[neighbor]
                length += math.dist(candidate, neighbor_position)
                for source, target in placed_edges:
                    if node_index in (source, target) or neighbor in (source, target):
                        continue
                    crossings += proper_cross(
                        candidate,
                        neighbor_position,
                        positions[source],
                        positions[target],
                    )
                for other in placed_nodes:
                    if other == neighbor:
                        continue
                    edge_node += segment_intersects_rect(
                        candidate,
                        neighbor_position,
                        rect(positions[other], widths[other], heights[other]),
                    )
            for source, target in placed_edges:
                if node_index in (source, target):
                    continue
                edge_node += segment_intersects_rect(
                    positions[source], positions[target], candidate_rect
                )
            cost = (overlaps, crossings + edge_node * 4, crossings, edge_node, length)
            if best_cost is None or cost < best_cost:
                best_cost = cost
                best_point = candidate
        assert best_point is not None
        positions[node_index] = best_point
        pending.remove(node_index)
        inserted += 1

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        "".join(
            f"{model_ids[index]}\t{positions[index][0]:.6f}\t{positions[index][1]:.6f}\n"
            for index in range(len(model_ids))
        ),
        encoding="utf-8",
    )
    print(
        f"absorbed current={len(model_ids)} common={common} "
        f"inserted={len(model_ids) - common} removed={len(template) - common} "
        f"out={args.out}"
    )


if __name__ == "__main__":
    main()
