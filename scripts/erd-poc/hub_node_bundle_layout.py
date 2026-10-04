#!/usr/bin/env python3
"""Prototype visible node bundles around structural hub models.

Every model remains a real node and every relationship remains a direct edge.
The only abstraction is placement: non-hub nodes in the giant biconnected core
are assigned to a nearby hub constellation and ordered so their external
neighbors face the corresponding constellations.
"""

from __future__ import annotations

import argparse
import importlib.util
import math
import sys
from collections import defaultdict, deque
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
    parser.add_argument("--hub-degree", type=int, default=10)
    parser.add_argument("--center-scale", type=float, default=4.0)
    parser.add_argument("--base-radius", type=float, default=700.0)
    parser.add_argument("--radial-spacing", type=float, default=420.0)
    parser.add_argument("--tangential-spacing", type=float, default=310.0)
    parser.add_argument(
        "--assignment",
        choices=("nearest", "direct"),
        default="nearest",
    )
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
    blocks = list(nx.biconnected_components(graph))
    giant_nodes = max(blocks, key=len)
    core = graph.subgraph(giant_nodes).copy()
    degree = dict(core.degree())
    hubs = {node for node, value in degree.items() if value >= args.hub_degree}
    if len(hubs) < 2:
        raise RuntimeError("hub threshold produced fewer than two constellations")

    owner = assign_owners(core, hubs, degree, args.assignment)
    groups: dict[int, list[int]] = defaultdict(list)
    for node, hub in owner.items():
        if node != hub:
            groups[hub].append(node)

    moved = positions.copy()
    hub_indices = np.asarray(sorted(hubs), dtype=np.int32)
    old_hub_centroid = positions[hub_indices].mean(axis=0)
    for hub in hubs:
        moved[hub] = (
            old_hub_centroid
            + (positions[hub] - old_hub_centroid) * args.center_scale
        )

    for hub, members in groups.items():
        place_constellation(
            hub,
            members,
            core,
            owner,
            positions,
            moved,
            args.base_radius,
            args.radial_spacing,
            args.tangential_spacing,
        )

    write_positions(args.out_tsv, layout, moved)
    measured = measure(layout, edges, moved)
    sizes = sorted((len(members) for members in groups.values()), reverse=True)
    print(
        f"hubs={len(hubs)} assigned={len(owner) - len(hubs)} "
        f"largest={sizes[:5]} centerScale={args.center_scale:g} "
        f"baseRadius={args.base_radius:g} crossings={measured.cross} "
        f"edgeNode={measured.edge_node} overlaps={measured.overlaps} "
        f"visual={measured.visual_cross}"
    )


def assign_owners(
    graph: nx.Graph,
    hubs: set[int],
    degree: dict[int, int],
    mode: str,
) -> dict[int, int]:
    owner = {hub: hub for hub in hubs}
    if mode == "direct":
        for node in graph:
            if node in hubs:
                continue
            adjacent = [neighbor for neighbor in graph.neighbors(node) if neighbor in hubs]
            if adjacent:
                owner[node] = max(adjacent, key=lambda hub: (degree[hub], -hub))
        return owner

    # Deterministic multi-source BFS. Higher-degree hubs win equal-distance
    # ties, which keeps large FK families together.
    queue: deque[int] = deque(
        sorted(hubs, key=lambda hub: (-degree[hub], hub))
    )
    distance = {hub: 0 for hub in hubs}
    while queue:
        node = queue.popleft()
        for neighbor in sorted(graph.neighbors(node)):
            proposed_distance = distance[node] + 1
            proposed_owner = owner[node]
            current_distance = distance.get(neighbor)
            if current_distance is None or proposed_distance < current_distance:
                distance[neighbor] = proposed_distance
                owner[neighbor] = proposed_owner
                queue.append(neighbor)
            elif proposed_distance == current_distance:
                current_owner = owner[neighbor]
                if (degree[proposed_owner], -proposed_owner) > (
                    degree[current_owner],
                    -current_owner,
                ):
                    owner[neighbor] = proposed_owner
                    queue.append(neighbor)
    return owner


def place_constellation(
    hub: int,
    members: list[int],
    graph: nx.Graph,
    owner: dict[int, int],
    original: np.ndarray,
    moved: np.ndarray,
    base_radius: float,
    radial_spacing: float,
    tangential_spacing: float,
) -> None:
    if not members:
        return
    preferred: list[tuple[float, int]] = []
    for node in members:
        vectors: list[np.ndarray] = []
        for neighbor in graph.neighbors(node):
            neighbor_owner = owner.get(neighbor)
            if neighbor_owner is None or neighbor_owner == hub:
                continue
            vectors.append(moved[neighbor_owner] - moved[hub])
        if vectors:
            vector = np.asarray(vectors).sum(axis=0)
        else:
            vector = original[node] - original[hub]
        angle = math.atan2(float(vector[1]), float(vector[0]))
        preferred.append((angle, node))
    preferred.sort()

    cursor = 0
    ring = 0
    while cursor < len(preferred):
        radius = base_radius + ring * radial_spacing
        capacity = max(
            6,
            int(math.floor(2.0 * math.pi * radius / tangential_spacing)),
        )
        count = min(capacity, len(preferred) - cursor)
        batch = preferred[cursor : cursor + count]
        slot_angles = np.linspace(-math.pi, math.pi, count, endpoint=False)
        best_shift = 0
        best_cost = math.inf
        desired = np.asarray([angle for angle, _node in batch])
        for shift in range(count):
            assigned = np.roll(slot_angles, shift)
            delta = np.angle(np.exp(1j * (assigned - desired)))
            cost = float(np.square(delta).sum())
            if cost < best_cost:
                best_cost = cost
                best_shift = shift
        assigned = np.roll(slot_angles, best_shift)
        for angle, (_desired, node) in zip(assigned, batch, strict=True):
            moved[node] = moved[hub] + np.array(
                [math.cos(float(angle)) * radius, math.sin(float(angle)) * radius]
            )
        cursor += count
        ring += 1


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
