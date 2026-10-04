#!/usr/bin/env python3
"""Place disjoint real-node hub bundles through a weighted meta graph."""

from __future__ import annotations

import argparse
import importlib.util
import math
import sys
from collections import deque
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
    parser.add_argument(
        "--engine",
        choices=("spring", "kamada", "spectral", "circular"),
        default="spring",
    )
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--iterations", type=int, default=1000)
    parser.add_argument("--center-scale", type=float, default=1.0)
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
    degree = dict(graph.degree())
    hubs = sorted(node for node, value in degree.items() if value >= args.hub_degree)
    owner = assign_voronoi(graph, hubs, degree)

    meta = nx.Graph()
    meta.add_nodes_from(hubs)
    for source_raw, target_raw in edges:
        source = int(source_raw)
        target = int(target_raw)
        source_owner = owner.get(source)
        target_owner = owner.get(target)
        if source_owner is None or target_owner is None or source_owner == target_owner:
            continue
        if meta.has_edge(source_owner, target_owner):
            meta[source_owner][target_owner]["weight"] += 1.0
        else:
            meta.add_edge(source_owner, target_owner, weight=1.0)

    raw_centers = meta_positions(meta, args.engine, args.seed, args.iterations)
    active_hubs = [hub for hub in hubs if hub in raw_centers]
    target = np.asarray([raw_centers[hub] for hub in active_hubs], dtype=np.float64)
    reference = positions[np.asarray(active_hubs, dtype=np.int32)]
    fitted = similarity_fit(target, reference, args.center_scale)
    fitted_by_hub = {hub: fitted[index] for index, hub in enumerate(active_hubs)}

    moved = positions.copy()
    group_sizes: list[int] = []
    for hub in active_hubs:
        members = np.asarray(
            [node for node, node_owner in owner.items() if node_owner == hub],
            dtype=np.int32,
        )
        if members.size == 0:
            continue
        moved[members] += fitted_by_hub[hub] - positions[hub]
        group_sizes.append(int(members.size))

    write_positions(args.out_tsv, layout, moved)
    measured = measure(layout, edges, moved)
    print(
        f"engine={args.engine} seed={args.seed} hubs={len(active_hubs)} "
        f"metaEdges={meta.number_of_edges()} groups={sorted(group_sizes, reverse=True)[:5]} "
        f"scale={args.center_scale:g} crossings={measured.cross} "
        f"edgeNode={measured.edge_node} overlaps={measured.overlaps} "
        f"visual={measured.visual_cross}"
    )


def assign_voronoi(
    graph: nx.Graph,
    hubs: list[int],
    degree: dict[int, int],
) -> dict[int, int]:
    owner = {hub: hub for hub in hubs}
    distance = {hub: 0 for hub in hubs}
    queue: deque[int] = deque(
        sorted(hubs, key=lambda hub: (-degree[hub], hub))
    )
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


def meta_positions(
    graph: nx.Graph,
    engine: str,
    seed: int,
    iterations: int,
) -> dict[int, np.ndarray]:
    if engine == "spring":
        return nx.spring_layout(
            graph,
            seed=seed,
            iterations=iterations,
            weight="weight",
            scale=1.0,
        )
    if engine == "kamada":
        # Stronger semantic ties should be shorter in the distance objective.
        distances = dict(nx.all_pairs_dijkstra_path_length(
            graph,
            weight=lambda _u, _v, data: 1.0 / max(1.0, float(data["weight"])),
        ))
        return nx.kamada_kawai_layout(graph, dist=distances, weight="weight", scale=1.0)
    if engine == "spectral":
        return nx.spectral_layout(graph, weight="weight", scale=1.0)
    ordered = sorted(graph)
    values = nx.circular_layout(ordered, scale=1.0)
    return {node: values[node] for node in ordered}


def similarity_fit(
    source: np.ndarray,
    reference: np.ndarray,
    center_scale: float,
) -> np.ndarray:
    source_center = source.mean(axis=0)
    reference_center = reference.mean(axis=0)
    source_zero = source - source_center
    reference_zero = reference - reference_center
    covariance = source_zero.T @ reference_zero
    u, _singular, vt = np.linalg.svd(covariance)
    rotation = u @ vt
    if np.linalg.det(rotation) < 0:
        u[:, -1] *= -1.0
        rotation = u @ vt
    rotated = source_zero @ rotation
    source_rms = math.sqrt(float(np.square(rotated).sum()) / max(1, source.shape[0]))
    reference_rms = math.sqrt(
        float(np.square(reference_zero).sum()) / max(1, reference.shape[0])
    )
    scale = reference_rms / max(source_rms, 1e-9) * center_scale
    return rotated * scale + reference_center


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
