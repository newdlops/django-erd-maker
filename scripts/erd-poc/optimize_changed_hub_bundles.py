#!/usr/bin/env python3
"""Jointly arrange changed real-node families around their structural hubs.

Unlike a sequence of single-node moves, a whole family may cross a temporary
barrier and still be accepted when the final group geometry reduces crossings.
The grouping is derived from the relationship delta, not from model names or a
fixed data set.  Hubs and all members remain ordinary rendered model nodes.
"""

from __future__ import annotations

import argparse
import importlib.util
import math
import sys
import time
from collections import deque
from pathlib import Path

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


def relationship_pairs(layout: dict) -> set[tuple[str, str]]:
    pairs: set[tuple[str, str]] = set()
    for edge in layout.get("routedEdges", []):
        source = str(edge.get("sourceModelId", ""))
        target = str(edge.get("targetModelId", ""))
        if source and target and source != target:
            pairs.add(tuple(sorted((source, target))))
    return pairs


def proper_crossings(a, b, c, d) -> np.ndarray:
    def orient(p, q, r):
        return (
            (q[..., 0] - p[..., 0]) * (r[..., 1] - p[..., 1])
            - (q[..., 1] - p[..., 1]) * (r[..., 0] - p[..., 0])
        )

    first = orient(a, b, c)
    second = orient(a, b, d)
    third = orient(c, d, a)
    fourth = orient(c, d, b)
    return (
        (np.sign(first) != np.sign(second))
        & (np.sign(third) != np.sign(fourth))
        & (first != 0.0)
        & (second != 0.0)
        & (third != 0.0)
        & (fourth != 0.0)
    )


def affected_pairs(edges: np.ndarray, moved: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    moved_mask = np.zeros(int(edges.max()) + 1, dtype=bool)
    moved_mask[moved] = True
    affected = moved_mask[edges[:, 0]] | moved_mask[edges[:, 1]]
    affected_indices = np.flatnonzero(affected).astype(np.int32)
    affected_lookup = np.zeros(edges.shape[0], dtype=bool)
    affected_lookup[affected_indices] = True
    left_parts: list[np.ndarray] = []
    right_parts: list[np.ndarray] = []
    all_indices = np.arange(edges.shape[0], dtype=np.int32)
    for left in range(edges.shape[0]):
        if affected_lookup[left]:
            right = all_indices[left + 1 :]
        else:
            right = affected_indices[affected_indices > left]
        if right.size == 0:
            continue
        source, target = edges[left]
        other = edges[right]
        keep = (
            (other[:, 0] != source)
            & (other[:, 1] != source)
            & (other[:, 0] != target)
            & (other[:, 1] != target)
        )
        right = right[keep]
        if right.size:
            left_parts.append(np.full(right.size, left, dtype=np.int32))
            right_parts.append(right)
    if not left_parts:
        empty = np.empty(0, dtype=np.int32)
        return empty, empty
    return np.concatenate(left_parts), np.concatenate(right_parts)


def crossing_count_for_group(
    edges: np.ndarray,
    pairs: tuple[np.ndarray, np.ndarray],
    positions: np.ndarray,
    moved: np.ndarray,
    candidate: np.ndarray,
) -> int:
    left, right = pairs
    if left.size == 0:
        return 0
    trial_positions = positions.copy()
    trial_positions[moved] = candidate
    first = edges[left]
    second = edges[right]
    return int(proper_crossings(
        trial_positions[first[:, 0]],
        trial_positions[first[:, 1]],
        trial_positions[second[:, 0]],
        trial_positions[second[:, 1]],
    ).sum())


def changed_groups(
    model_ids: list[str],
    current_pairs: set[tuple[str, str]],
    reference_pairs: set[tuple[str, str]],
    minimum_hub_degree: int,
) -> tuple[list[tuple[int, list[int]]], set[int]]:
    index_by_id = {model_id: index for index, model_id in enumerate(model_ids)}
    adjacency: dict[int, set[int]] = {}
    for source_id, target_id in current_pairs ^ reference_pairs:
        source = index_by_id.get(source_id)
        target = index_by_id.get(target_id)
        if source is None or target is None:
            continue
        adjacency.setdefault(source, set()).add(target)
        adjacency.setdefault(target, set()).add(source)
    frontier = set(adjacency)
    hubs = {
        node for node, neighbors in adjacency.items()
        if len(neighbors) >= minimum_hub_degree
    }
    if not hubs:
        return [], frontier

    owner: dict[int, int] = {hub: hub for hub in hubs}
    distance: dict[int, int] = {hub: 0 for hub in hubs}
    pending: deque[int] = deque(sorted(
        hubs,
        key=lambda node: (-len(adjacency[node]), model_ids[node]),
    ))
    while pending:
        node = pending.popleft()
        for neighbor in sorted(adjacency.get(node, ())):
            proposed_distance = distance[node] + 1
            proposed_owner = owner[node]
            current_distance = distance.get(neighbor)
            if current_distance is None or proposed_distance < current_distance:
                distance[neighbor] = proposed_distance
                owner[neighbor] = proposed_owner
                pending.append(neighbor)
            elif proposed_distance == current_distance:
                current_owner = owner[neighbor]
                proposed_key = (
                    len(adjacency[proposed_owner]),
                    model_ids[proposed_owner],
                )
                current_key = (
                    len(adjacency[current_owner]),
                    model_ids[current_owner],
                )
                if proposed_key > current_key:
                    owner[neighbor] = proposed_owner
                    pending.append(neighbor)

    members_by_hub: dict[int, list[int]] = {hub: [] for hub in hubs}
    for node, hub in owner.items():
        if node != hub and node not in hubs:
            members_by_hub[hub].append(node)
    groups = [
        (hub, sorted(members))
        for hub, members in members_by_hub.items()
        if len(members) >= 2
    ]
    groups.sort(key=lambda item: (-len(item[1]), model_ids[item[0]]))
    return groups, frontier


def preferred_member_order(
    hub: int,
    members: list[int],
    adjacency: list[np.ndarray],
    positions: np.ndarray,
    model_ids: list[str],
) -> list[int]:
    member_set = set(members)
    hub_position = positions[hub]

    def key(node: int) -> tuple[float, str]:
        external = np.asarray([
            neighbor for neighbor in adjacency[node]
            if int(neighbor) not in member_set and int(neighbor) != hub
        ], dtype=np.int32)
        if external.size:
            direction = positions[external].mean(axis=0) - hub_position
        else:
            direction = positions[node] - hub_position
        if float(np.linalg.norm(direction)) <= 1e-9:
            phase = 0.0
            for byte in model_ids[node].encode("utf-8"):
                phase = (phase * 131.0 + byte) % 104729.0
            angle = phase / 104729.0 * math.tau
        else:
            angle = math.atan2(float(direction[1]), float(direction[0]))
        return angle, model_ids[node]

    return sorted(members, key=key)


def ring_candidate(
    hub: int,
    ordered_members: list[int],
    positions: np.ndarray,
    widths: np.ndarray,
    heights: np.ndarray,
    capacity: int,
    rotation: float,
    reverse: bool,
    radius_scale: float,
    gap: float,
) -> tuple[np.ndarray, np.ndarray]:
    members = list(reversed(ordered_members)) if reverse else ordered_members
    ring_count = max(1, math.ceil(len(members) / capacity))
    buckets = [members[index::ring_count] for index in range(ring_count)]
    node_span = float(np.max(np.maximum(widths[members], heights[members])))
    hub_radius = 0.5 * math.hypot(widths[hub], heights[hub])
    mapped: dict[int, np.ndarray] = {}
    for ring_index, bucket in enumerate(buckets):
        count = len(bucket)
        circumference_radius = count * (node_span + gap) / math.tau
        layer_radius = hub_radius + (ring_index + 1) * (node_span + gap)
        radius = max(circumference_radius, layer_radius) * radius_scale
        for slot, node in enumerate(bucket):
            angle = rotation + math.tau * slot / max(1, count)
            mapped[node] = positions[hub] + np.array([
                math.cos(angle) * radius,
                math.sin(angle) * radius,
            ])
    moved = np.asarray(sorted(mapped), dtype=np.int32)
    candidate = np.asarray([mapped[int(node)] for node in moved], dtype=np.float64)
    return moved, candidate


def collision_free(
    moved: np.ndarray,
    candidate: np.ndarray,
    positions: np.ndarray,
    widths: np.ndarray,
    heights: np.ndarray,
    margin: float,
) -> bool:
    moved_lookup = np.zeros(positions.shape[0], dtype=bool)
    moved_lookup[moved] = True
    fixed = np.flatnonzero(~moved_lookup)
    if fixed.size:
        delta = np.abs(candidate[:, None, :] - positions[fixed][None, :, :])
        half_width = (
            widths[moved][:, None] + widths[fixed][None, :]
        ) * 0.5 + margin
        half_height = (
            heights[moved][:, None] + heights[fixed][None, :]
        ) * 0.5 + margin
        if np.any((delta[:, :, 0] < half_width) & (delta[:, :, 1] < half_height)):
            return False
    for left in range(moved.size):
        delta = np.abs(candidate[left + 1 :] - candidate[left])
        if delta.size == 0:
            continue
        half_width = (
            widths[moved[left + 1 :]] + widths[moved[left]]
        ) * 0.5 + margin
        half_height = (
            heights[moved[left + 1 :]] + heights[moved[left]]
        ) * 0.5 + margin
        if np.any((delta[:, 0] < half_width) & (delta[:, 1] < half_height)):
            return False
    return True


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--positions", type=Path, required=True)
    parser.add_argument("--reference-layout", type=Path, required=True)
    parser.add_argument("--out-tsv", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=2)
    parser.add_argument("--hub-limit", type=int, default=12)
    parser.add_argument("--minimum-hub-degree", type=int, default=4)
    parser.add_argument("--ring-capacity", type=int, default=12)
    parser.add_argument("--rotation-steps", type=int, default=12)
    parser.add_argument("--radius-scales", default="1,1.35,1.8")
    parser.add_argument("--gap", type=float, default=60.0)
    parser.add_argument("--margin", type=float, default=18.0)
    args = parser.parse_args()

    layout = v34.load_layout(args.layout)
    reference = v34.load_layout(args.reference_layout)
    positions = v34.read_positions_tsv(args.positions, layout)
    edges = v34.graph_edges(layout)
    model_ids = [str(node["modelId"]) for node in layout["nodes"]]
    current_pairs = {
        tuple(sorted((model_ids[int(source)], model_ids[int(target)])))
        for source, target in edges
    }
    reference_pairs = relationship_pairs(reference)
    groups, frontier = changed_groups(
        model_ids,
        current_pairs,
        reference_pairs,
        args.minimum_hub_degree,
    )
    adjacency_sets: list[set[int]] = [set() for _ in model_ids]
    for source_raw, target_raw in edges:
        source = int(source_raw)
        target = int(target_raw)
        adjacency_sets[source].add(target)
        adjacency_sets[target].add(source)
    adjacency = [
        np.asarray(sorted(neighbors), dtype=np.int32)
        for neighbors in adjacency_sets
    ]
    widths, heights = v34.render_node_sizes(layout, edges, direct_scene=True)
    radius_scales = [
        float(value) for value in args.radius_scales.split(",") if value.strip()
    ]
    total_crossings = v34.fce.count_crossings_streaming(positions, edges)
    started = time.time()
    print(
        f"start frontier={len(frontier)} groups={len(groups)} "
        f"sizes={[len(members) for _hub, members in groups[:12]]} "
        f"cross={total_crossings}",
        flush=True,
    )

    for round_index in range(max(0, args.rounds)):
        accepted = 0
        gain = 0
        for hub, members in groups[: args.hub_limit or None]:
            ordered = preferred_member_order(
                hub, members, adjacency, positions, model_ids
            )
            moved = np.asarray(sorted(members), dtype=np.int32)
            pairs = affected_pairs(edges, moved)
            current_count = crossing_count_for_group(
                edges, pairs, positions, moved, positions[moved]
            )
            best_count = current_count
            best_candidate = None
            for radius_scale in radius_scales:
                for reverse in (False, True):
                    for step in range(max(1, args.rotation_steps)):
                        rotation = math.tau * step / max(1, args.rotation_steps)
                        candidate_moved, candidate = ring_candidate(
                            hub,
                            ordered,
                            positions,
                            widths,
                            heights,
                            args.ring_capacity,
                            rotation,
                            reverse,
                            radius_scale,
                            args.gap,
                        )
                        if not np.array_equal(candidate_moved, moved):
                            raise RuntimeError("bundle candidate node order drift")
                        if not collision_free(
                            moved,
                            candidate,
                            positions,
                            widths,
                            heights,
                            args.margin,
                        ):
                            continue
                        count = crossing_count_for_group(
                            edges, pairs, positions, moved, candidate
                        )
                        if count < best_count:
                            best_count = count
                            best_candidate = candidate.copy()
            if best_candidate is None:
                continue
            positions[moved] = best_candidate
            delta = current_count - best_count
            total_crossings -= delta
            gain += delta
            accepted += 1
            print(
                f"  accepted hub={model_ids[hub]} members={len(members)} "
                f"gain={delta} tracked={total_crossings}",
                flush=True,
            )
        verified = v34.fce.count_crossings_streaming(positions, edges)
        if verified != total_crossings:
            raise RuntimeError(
                f"incremental crossing drift: tracked={total_crossings} verified={verified}"
            )
        v34.write_positions_tsv(args.out_tsv, layout, positions)
        print(
            f"round={round_index + 1} accepted={accepted} gain={gain} "
            f"cross={verified} elapsed={time.time() - started:.1f}s",
            flush=True,
        )
        if accepted == 0:
            break
    print(f"done cross={total_crossings} out={args.out_tsv}", flush=True)


if __name__ == "__main__":
    main()
