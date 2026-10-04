#!/usr/bin/env python3
"""Prototype a hierarchy of *node* bundles for the direct-edge ERD scene.

The hierarchy affects coordinates only. It never contracts, drops, duplicates,
or reroutes an ERD relationship: the output contains one center coordinate for
every original model and the normal direct-scene evaluator draws every original
source-target edge as one straight segment.

At each non-planar bundle we look for a Louvain partition whose simple quotient
is planar. Child bundles are then drawn far apart at the quotient vertices while
their induced subgraphs are laid out recursively. This makes cross-bundle lines
follow a planar macro topology without turning those lines into a shared trunk.
"""

from __future__ import annotations

import argparse
import importlib.util
import math
import sys
from dataclasses import dataclass
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


@dataclass
class BundleLayout:
    positions: dict[int, np.ndarray]
    groups: list[np.ndarray]
    description: str


@dataclass
class PartitionCandidate:
    communities: list[set[int]]
    quotient: nx.Graph
    boundary_edges: int
    resolution: float
    seed: int
    planar: bool


def normalized_embedding_positions(graph: nx.Graph) -> dict[int, np.ndarray]:
    """Return deterministic straight-line coordinates for a planar graph."""
    nodes = sorted(int(node) for node in graph.nodes())
    if not nodes:
        return {}
    if len(nodes) == 1:
        return {nodes[0]: np.zeros(2, dtype=np.float64)}
    if len(nodes) == 2:
        return {
            nodes[0]: np.array([-0.5, 0.0], dtype=np.float64),
            nodes[1]: np.array([0.5, 0.0], dtype=np.float64),
        }
    planar, embedding = nx.check_planarity(graph, counterexample=False)
    if not planar:
        raise ValueError("normalized_embedding_positions requires a planar graph")
    raw = nx.combinatorial_embedding_to_pos(embedding, fully_triangulate=True)
    points = np.asarray([raw[node] for node in nodes], dtype=np.float64)
    points -= points.mean(axis=0)
    pair_delta = points[:, None, :] - points[None, :, :]
    pair_distance = np.linalg.norm(pair_delta, axis=2)
    positive = pair_distance[pair_distance > 1e-9]
    unit = float(np.min(positive)) if positive.size else 1.0
    points /= unit
    return {node: points[index] for index, node in enumerate(nodes)}


def quotient_graph(
    graph: nx.Graph,
    communities: list[set[int]],
) -> tuple[nx.Graph, int]:
    community_by_node = {
        node: community_index
        for community_index, members in enumerate(communities)
        for node in members
    }
    quotient = nx.Graph()
    quotient.add_nodes_from(range(len(communities)))
    boundary_edges = 0
    for source, target in graph.edges():
        left = community_by_node[source]
        right = community_by_node[target]
        if left == right:
            continue
        boundary_edges += 1
        if quotient.has_edge(left, right):
            quotient[left][right]["weight"] += 1
        else:
            quotient.add_edge(left, right, weight=1)
    return quotient, boundary_edges


def louvain_partition_candidate(
    graph: nx.Graph,
    resolution: float,
    seed: int,
) -> PartitionCandidate | None:
    communities = [
        set(int(node) for node in members)
        for members in nx.community.louvain_communities(
            graph,
            weight=None,
            resolution=resolution,
            seed=seed,
        )
    ]
    communities.sort(key=lambda members: (-len(members), min(members)))
    if len(communities) < 2 or max(map(len, communities)) >= graph.number_of_nodes():
        return None
    quotient, boundary_edges = quotient_graph(graph, communities)
    planar = nx.check_planarity(quotient, counterexample=False)[0]
    return PartitionCandidate(
        boundary_edges=boundary_edges,
        communities=communities,
        planar=planar,
        quotient=quotient,
        resolution=resolution,
        seed=seed,
    )


def choose_partition(
    graph: nx.Graph,
    seed: int,
    forced_resolution: float | None = None,
) -> PartitionCandidate:
    resolutions = (
        [forced_resolution]
        if forced_resolution is not None
        else [0.05, 0.08, 0.12, 0.18, 0.25, 0.35, 0.5, 0.75, 1.0, 1.5, 2.0]
    )
    candidates: list[PartitionCandidate] = []
    for resolution in resolutions:
        for seed_offset in range(3 if forced_resolution is None else 1):
            candidate = louvain_partition_candidate(
                graph,
                float(resolution),
                seed + seed_offset * 1009,
            )
            if candidate is not None:
                candidates.append(candidate)
    if not candidates:
        # Always make progress. A binary spectral split has a planar two-node
        # quotient and still groups real nodes rather than relationships.
        ordered = list(nx.spectral_ordering(graph))
        midpoint = max(1, len(ordered) // 2)
        communities = [set(ordered[:midpoint]), set(ordered[midpoint:])]
        quotient, boundary_edges = quotient_graph(graph, communities)
        return PartitionCandidate(
            boundary_edges=boundary_edges,
            communities=communities,
            planar=True,
            quotient=quotient,
            resolution=-1.0,
            seed=seed,
        )

    node_count = graph.number_of_nodes()

    def score(candidate: PartitionCandidate) -> tuple[float, ...]:
        largest = max(map(len, candidate.communities))
        largest_ratio = largest / node_count
        # Prefer a planar macro topology first. Among those, avoid both an
        # almost-unsplit giant child and excessive boundary relationships.
        return (
            0.0 if candidate.planar else 1.0,
            1.0 if largest_ratio > 0.78 else 0.0,
            largest_ratio + candidate.boundary_edges / max(1, graph.number_of_edges()) * 0.35,
            float(candidate.boundary_edges),
            float(len(candidate.communities)),
            candidate.resolution,
        )

    return min(candidates, key=score)


def child_radius(positions: dict[int, np.ndarray]) -> float:
    if not positions:
        return 0.5
    points = np.asarray(list(positions.values()), dtype=np.float64)
    center = points.mean(axis=0)
    return max(0.5, float(np.linalg.norm(points - center, axis=1).max()))


def place_child_bundles(
    child_layouts: list[BundleLayout],
    quotient: nx.Graph,
    separation: float,
) -> dict[int, np.ndarray]:
    if len(child_layouts) == 1:
        return dict(child_layouts[0].positions)
    if nx.check_planarity(quotient, counterexample=False)[0]:
        quotient_positions = normalized_embedding_positions(quotient)
    else:
        raw = nx.spring_layout(quotient, seed=42, weight="weight", iterations=1000)
        quotient_positions = {
            int(node): np.asarray(point, dtype=np.float64)
            for node, point in raw.items()
        }
    radii = [child_radius(child.positions) for child in child_layouts]
    center_scale = 1.0
    for left in range(len(child_layouts)):
        for right in range(left + 1, len(child_layouts)):
            distance = float(np.linalg.norm(
                quotient_positions[left] - quotient_positions[right]
            ))
            if distance <= 1e-9:
                continue
            required = separation * (radii[left] + radii[right] + 1.0) / distance
            center_scale = max(center_scale, required)
    combined: dict[int, np.ndarray] = {}
    for child_index, child in enumerate(child_layouts):
        points = np.asarray(list(child.positions.values()), dtype=np.float64)
        center = points.mean(axis=0) if points.size else np.zeros(2)
        offset = quotient_positions[child_index] * center_scale
        for node, point in child.positions.items():
            combined[node] = point - center + offset
    return combined


def recursive_bundle_layout(
    graph: nx.Graph,
    nodes: set[int],
    seed: int,
    separation: float,
    depth: int,
    root_resolution: float | None,
    trace: list[str],
) -> BundleLayout:
    subgraph = graph.subgraph(nodes).copy()
    planar = nx.check_planarity(subgraph, counterexample=False)[0]
    if planar:
        positions = normalized_embedding_positions(subgraph)
        trace.append(
            f"depth={depth} leaf nodes={len(nodes)} edges={subgraph.number_of_edges()} planar=1"
        )
        return BundleLayout(
            description=f"planar:{len(nodes)}",
            groups=[],
            positions=positions,
        )

    partition = choose_partition(
        subgraph,
        seed + depth * 7919,
        forced_resolution=root_resolution if depth == 0 else None,
    )
    trace.append(
        f"depth={depth} bundle nodes={len(nodes)} edges={subgraph.number_of_edges()} "
        f"children={len(partition.communities)} boundary={partition.boundary_edges} "
        f"qEdges={partition.quotient.number_of_edges()} qPlanar={int(partition.planar)} "
        f"resolution={partition.resolution:g} sizes="
        f"{sorted(map(len, partition.communities), reverse=True)}"
    )
    child_layouts = [
        recursive_bundle_layout(
            graph,
            members,
            seed + child_index * 104729,
            separation,
            depth + 1,
            None,
            trace,
        )
        for child_index, members in enumerate(partition.communities)
    ]
    positions = place_child_bundles(child_layouts, partition.quotient, separation)
    groups = [
        group
        for child in child_layouts
        for group in child.groups
    ]
    groups.extend(
        np.asarray(sorted(members), dtype=np.int32)
        for members in partition.communities
        if 1 < len(members) < len(nodes)
    )
    return BundleLayout(
        description=f"bundle:{len(nodes)}",
        groups=groups,
        positions=positions,
    )


def core_clearance_scale(
    positions: np.ndarray,
    core_nodes: list[int],
    widths: np.ndarray,
    heights: np.ndarray,
    target_spacing: float,
) -> float:
    local = positions[core_nodes]
    delta = local[:, None, :] - local[None, :, :]
    distances = np.linalg.norm(delta, axis=2)
    radii = np.hypot(widths[core_nodes], heights[core_nodes]) / 2.0
    required = 1.0
    for left in range(len(core_nodes)):
        positive = distances[left] > 1e-9
        if not np.any(positive):
            continue
        needs = radii[left] + radii + target_spacing
        required = max(required, float(np.max(needs[positive] / distances[left, positive])))
    return required * 1.05


def attach_noncore_nodes(
    graph: nx.Graph,
    core: set[int],
    old_positions: np.ndarray,
    positions: np.ndarray,
) -> tuple[int, list[set[int]]]:
    outside = set(graph) - core
    attached = 0
    detached_components: list[set[int]] = []
    for component_raw in nx.connected_components(graph.subgraph(outside)):
        component = set(int(node) for node in component_raw)
        anchors = {
            int(neighbor)
            for node in component
            for neighbor in graph.neighbors(node)
            if neighbor in core
        }
        if len(anchors) == 1:
            anchor = next(iter(anchors))
            delta = positions[anchor] - old_positions[anchor]
            indices = np.asarray(sorted(component), dtype=np.int32)
            positions[indices] = old_positions[indices] + delta
            attached += len(component)
        else:
            detached_components.append(component)
    return attached, detached_components


def pack_detached_components(
    detached_components: list[set[int]],
    old_positions: np.ndarray,
    positions: np.ndarray,
    widths: np.ndarray,
    heights: np.ndarray,
    core_nodes: list[int],
    gap: float,
) -> None:
    if not detached_components:
        return
    core_right = float(np.max(positions[core_nodes, 0] + widths[core_nodes] / 2.0))
    core_top = float(np.min(positions[core_nodes, 1] - heights[core_nodes] / 2.0))
    cursor_x = core_right + gap
    cursor_y = core_top
    column_width = 0.0
    max_column_height = max(
        gap * 4.0,
        float(np.ptp(positions[core_nodes, 1])) + gap,
    )
    for component in sorted(detached_components, key=len, reverse=True):
        indices = np.asarray(sorted(component), dtype=np.int32)
        local = old_positions[indices].copy()
        local[:, 0] -= float(np.min(local[:, 0] - widths[indices] / 2.0))
        local[:, 1] -= float(np.min(local[:, 1] - heights[indices] / 2.0))
        component_width = float(np.max(local[:, 0] + widths[indices] / 2.0))
        component_height = float(np.max(local[:, 1] + heights[indices] / 2.0))
        if cursor_y > core_top and cursor_y + component_height > core_top + max_column_height:
            cursor_x += column_width + gap
            cursor_y = core_top
            column_width = 0.0
        positions[indices] = local + np.array([cursor_x, cursor_y])
        cursor_y += component_height + gap
        column_width = max(column_width, component_width)


def optimize_group_orientations(
    positions: np.ndarray,
    groups: list[np.ndarray],
    evaluator,
    rotations: int,
    rounds: int,
) -> tuple[int, int]:
    current_flags = v34.crossing_flags_for_pairs(positions, evaluator)
    current_crossings = int(current_flags.sum())
    accepted = 0
    groups = sorted(groups, key=lambda members: len(members), reverse=True)
    for _round in range(rounds):
        improved = False
        for members in groups:
            if members.size < 2 or members.size >= positions.shape[0]:
                continue
            pair_mask = v34.impacted_pair_mask(members, evaluator, positions.shape[0])
            old_local = int(current_flags[pair_mask].sum())
            original = positions[members].copy()
            centroid = original.mean(axis=0)
            centered = original - centroid
            best_points = original
            best_flags = current_flags[pair_mask]
            best_local = old_local
            for reflected in (False, True):
                reflected_points = centered.copy()
                if reflected:
                    reflected_points[:, 0] *= -1.0
                for rotation_index in range(rotations):
                    if not reflected and rotation_index == 0:
                        continue
                    angle = 2.0 * math.pi * rotation_index / rotations
                    cosine = math.cos(angle)
                    sine = math.sin(angle)
                    rotation = np.array([[cosine, -sine], [sine, cosine]])
                    candidate = reflected_points @ rotation.T + centroid
                    positions[members] = candidate
                    candidate_flags = v34.crossing_flags_for_pairs(
                        positions,
                        evaluator,
                        pair_mask,
                    )
                    candidate_local = int(candidate_flags.sum())
                    if candidate_local < best_local:
                        best_local = candidate_local
                        best_points = candidate.copy()
                        best_flags = candidate_flags.copy()
            positions[members] = best_points
            if best_local < old_local:
                current_crossings += best_local - old_local
                current_flags[pair_mask] = best_flags
                accepted += 1
                improved = True
            else:
                positions[members] = original
        if not improved:
            break
    return current_crossings, accepted


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--out-tsv", type=Path, required=True)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--root-resolution", type=float, default=0.25)
    parser.add_argument("--separation", type=float, default=6.0)
    parser.add_argument("--target-spacing", type=float, default=80.0)
    parser.add_argument("--component-gap", type=float, default=5000.0)
    parser.add_argument("--orient-rotations", type=int, default=12)
    parser.add_argument("--orient-rounds", type=int, default=2)
    args = parser.parse_args()

    layout = v34.load_layout(args.layout)
    old_positions = v34.layout_positions(layout)
    edges = v34.graph_edges(layout)
    graph = nx.Graph()
    graph.add_nodes_from(range(len(layout["nodes"])))
    graph.add_edges_from((int(source), int(target)) for source, target in edges)
    blocks = list(nx.biconnected_components(graph))
    core = set(int(node) for node in max(blocks, key=len))
    core_nodes = sorted(core)
    trace: list[str] = []
    hierarchy = recursive_bundle_layout(
        graph,
        core,
        args.seed,
        args.separation,
        0,
        args.root_resolution,
        trace,
    )
    for line in trace:
        print(line)

    positions = old_positions.copy()
    for node, point in hierarchy.positions.items():
        positions[node] = point
    widths, heights = v34.render_node_sizes(layout, edges, direct_scene=True)
    scale = core_clearance_scale(
        positions,
        core_nodes,
        widths,
        heights,
        args.target_spacing,
    )
    core_center = positions[core_nodes].mean(axis=0)
    positions[core_nodes] = (positions[core_nodes] - core_center) * scale
    attached, detached = attach_noncore_nodes(
        graph,
        core,
        old_positions,
        positions,
    )
    pack_detached_components(
        detached,
        old_positions,
        positions,
        widths,
        heights,
        core_nodes,
        args.component_gap,
    )

    evaluator = v34.fce.FastCrossEval(edges, positions.shape[0])
    before_orientation = evaluator.count_crossings(positions)
    final_crossings, accepted = optimize_group_orientations(
        positions,
        hierarchy.groups,
        evaluator,
        max(4, args.orient_rotations),
        max(0, args.orient_rounds),
    )
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
    args.out_tsv.parent.mkdir(parents=True, exist_ok=True)
    v34.write_positions_tsv(args.out_tsv, layout, positions)
    print(
        f"result core={len(core_nodes)} hierarchyGroups={len(hierarchy.groups)} "
        f"scale={scale:.3f} attached={attached} detachedComponents={len(detached)} "
        f"cross={before_orientation}->{final_crossings} orientations={accepted} "
        f"edgeNode={measured.edge_node} overlaps={measured.overlaps} "
        f"visual={final_crossings + measured.edge_node} bboxB={measured.bbox_b:.3f} "
        f"out={args.out_tsv}"
    )


if __name__ == "__main__":
    main()
