#!/usr/bin/env python3
"""Measure node-group structure inside the crossing-heavy ERD core."""

from __future__ import annotations

import argparse
import importlib.util
import sys
from collections import Counter, defaultdict
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
    parser.add_argument("--top", type=int, default=30)
    parser.add_argument("--hub-degree", type=int, default=10)
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
    graph.add_edges_from((int(s), int(t)) for s, t in edges)
    ids = [str(node["modelId"]) for node in layout["nodes"]]
    degree = dict(graph.degree())

    blocks = list(nx.biconnected_components(graph))
    giant = max(blocks, key=len)
    core = graph.subgraph(giant).copy()
    core_degree = dict(core.degree())
    hub_nodes = {node for node, deg in core_degree.items() if deg >= args.hub_degree}

    crossing_pairs = list(v34.fce.iter_crossing_pairs(positions, edges))
    edge_crossings = np.zeros(edges.shape[0], dtype=np.int32)
    for a_raw, b_raw in crossing_pairs:
        edge_crossings[int(a_raw)] += 1
        edge_crossings[int(b_raw)] += 1
    incident_edges: dict[int, list[int]] = defaultdict(list)
    for edge_idx, (s_raw, t_raw) in enumerate(edges):
        incident_edges[int(s_raw)].append(edge_idx)
        incident_edges[int(t_raw)].append(edge_idx)

    print(
        f"giantNodes={core.number_of_nodes()} giantEdges={core.number_of_edges()} "
        f"hubs(deg>={args.hub_degree})={len(hub_nodes)}"
    )
    print("degree distribution")
    degree_counts = Counter(core_degree.values())
    for deg, count in sorted(degree_counts.items()):
        print(f"  degree={deg:3d} nodes={count:4d}")
    print("k-core sizes")
    core_numbers = nx.core_number(core)
    for k in sorted(set(core_numbers.values())):
        print(f"  k={k} nodes={sum(value >= k for value in core_numbers.values())}")
    core3_nodes = {node for node, value in core_numbers.items() if value >= 3}
    core3 = core.subgraph(core3_nodes).copy()
    core3_planar = nx.check_planarity(core3, counterexample=False)[0]
    shell_components = list(nx.connected_components(core.subgraph(set(core) - core3_nodes)))
    shell_attachment_counts = Counter()
    shell_sizes = Counter()
    attachment_sets: list[set[int]] = []
    for component in shell_components:
        attachments = {
            neighbor
            for node in component
            for neighbor in core.neighbors(node)
            if neighbor in core3_nodes
        }
        attachment_sets.append(attachments)
        shell_attachment_counts[len(attachments)] += 1
        shell_sizes[len(component)] += 1
    print(
        f"3-core graph nodes={core3.number_of_nodes()} edges={core3.number_of_edges()} "
        f"planar={core3_planar} shellComponents={len(shell_components)} "
        f"attachmentCounts={dict(sorted(shell_attachment_counts.items()))} "
        f"shellSizes={dict(sorted(shell_sizes.items())[:12])}"
    )
    attachment_skeleton = core3.copy()
    attachment_pair_weights: Counter[tuple[int, int]] = Counter()
    for attachments in attachment_sets:
        ordered = sorted(attachments)
        if len(ordered) == 2:
            attachment_pair_weights[(ordered[0], ordered[1])] += 1
        elif len(ordered) > 2:
            attachment_metric = nx.Graph()
            attachment_metric.add_nodes_from(ordered)
            for left_index, left in enumerate(ordered):
                for right in ordered[left_index + 1 :]:
                    try:
                        distance = nx.shortest_path_length(core3, left, right)
                    except nx.NetworkXNoPath:
                        distance = 10**9
                    attachment_metric.add_edge(left, right, weight=distance)
            for left, right in nx.minimum_spanning_edges(
                attachment_metric, data=False
            ):
                attachment_pair_weights[tuple(sorted((left, right)))] += 1
    for (source, target), weight in attachment_pair_weights.items():
        if attachment_skeleton.has_edge(source, target):
            attachment_skeleton[source][target]["shellWeight"] = weight
        else:
            attachment_skeleton.add_edge(source, target, shellWeight=weight)
    print(
        f"3-core attachment skeleton edges={attachment_skeleton.number_of_edges()} "
        f"virtualPairs={len(attachment_pair_weights)} "
        f"planar={nx.check_planarity(attachment_skeleton, counterexample=False)[0]}"
    )
    print("3-core Louvain partitions")
    for resolution in (0.1, 0.2, 0.35, 0.5, 0.75, 1.0, 1.5, 2.0):
        communities = nx.community.louvain_communities(
            core3,
            weight=None,
            resolution=resolution,
            seed=42,
        )
        by_node = {
            node: index
            for index, members in enumerate(communities)
            for node in members
        }
        quotient = nx.Graph()
        quotient.add_nodes_from(range(len(communities)))
        boundary = 0
        for source, target in core3.edges():
            if by_node[source] != by_node[target]:
                boundary += 1
                quotient.add_edge(by_node[source], by_node[target])
        print(
            f"  resolution={resolution:g} groups={len(communities)} "
            f"boundary={boundary} quotientEdges={quotient.number_of_edges()} "
            f"quotientPlanar={nx.check_planarity(quotient, counterexample=False)[0]} "
            f"sizes={sorted(map(len, communities), reverse=True)}"
        )
    print("networkx Louvain partitions")
    for resolution in (0.25, 0.5, 1.0, 2.0, 4.0, 8.0):
        communities = nx.community.louvain_communities(
            core,
            weight=None,
            resolution=resolution,
            seed=42,
        )
        community_by_node = {
            node: community_idx
            for community_idx, members in enumerate(communities)
            for node in members
        }
        boundary_edges = sum(
            community_by_node[source] != community_by_node[target]
            for source, target in core.edges()
        )
        quotient = nx.Graph()
        quotient.add_nodes_from(range(len(communities)))
        quotient.add_edges_from(
            (community_by_node[source], community_by_node[target])
            for source, target in core.edges()
            if community_by_node[source] != community_by_node[target]
        )
        quotient_planar = nx.check_planarity(quotient, counterexample=False)[0]
        group_by_node = {
            node: ("community", community_by_node[node])
            if node in community_by_node
            else ("outside", node)
            for node in graph
        }
        edge_group_pairs = [
            (group_by_node[int(source)], group_by_node[int(target)])
            for source, target in edges
        ]
        crossing_classes: Counter[str] = Counter()
        for left_raw, right_raw in crossing_pairs:
            left = edge_group_pairs[int(left_raw)]
            right = edge_group_pairs[int(right_raw)]
            left_internal = left[0] == left[1]
            right_internal = right[0] == right[1]
            if left_internal and right_internal:
                key = "sameInternal" if left[0] == right[0] else "differentInternal"
            elif left_internal or right_internal:
                key = "internalBoundary"
            else:
                key = "boundaryBoundary"
            crossing_classes[key] += 1
        sizes = sorted((len(members) for members in communities), reverse=True)
        group_shapes = sorted(
            (
                len(members),
                core.subgraph(members).number_of_edges(),
                nx.check_planarity(core.subgraph(members), counterexample=False)[0],
            )
            for members in communities
        )[::-1]
        print(
            f"  resolution={resolution:g} groups={len(communities)} "
            f"boundaryEdges={boundary_edges} quotientEdges={quotient.number_of_edges()} "
            f"quotientPlanar={quotient_planar} crossings="
            f"{dict(crossing_classes)} sizes={sizes[:12]} "
            f"shapes={group_shapes[:8]}"
        )

    paths = degree_two_paths(core)
    path_pair_counts = Counter(
        tuple(sorted((path[0], path[-1])))
        for path in paths
        if path[0] != path[-1]
    )
    parallel_pairs = [count for count in path_pair_counts.values() if count >= 2]
    print(
        "degree-2 path skeleton: "
        f"anchors={sum(deg != 2 for deg in core_degree.values())} "
        f"paths={len(paths)} pathsWithMembers="
        f"{sum(len(path) > 2 for path in paths)} "
        f"absorbedNodes={sum(max(0, len(path) - 2) for path in paths)} "
        f"parallelPairs={len(parallel_pairs)} "
        f"parallelPaths={sum(parallel_pairs)} "
        f"maxParallel={max(parallel_pairs, default=0)}"
    )
    direct_skeleton = nx.Graph()
    direct_skeleton.add_nodes_from(
        node for node, deg in core_degree.items() if deg != 2
    )
    direct_skeleton.add_edges_from(
        (path[0], path[-1])
        for path in paths
        if len(path) == 2 and path[0] != path[-1]
    )
    direct_planar, _direct_embedding = nx.check_planarity(
        direct_skeleton,
        counterexample=False,
    )
    print(
        "direct-anchor skeleton: "
        f"nodes={direct_skeleton.number_of_nodes()} "
        f"edges={direct_skeleton.number_of_edges()} planar={direct_planar}"
    )
    weighted_paths = sorted(
        paths,
        key=lambda path: (len(path) > 2, -len(path), path[0], path[-1]),
    )
    planar_backbone = nx.Graph()
    planar_backbone.add_nodes_from(direct_skeleton.nodes())
    rejected_direct = 0
    rejected_flexible = 0
    kept_direct = 0
    kept_flexible = 0
    for path in weighted_paths:
        source, target = path[0], path[-1]
        if source == target or planar_backbone.has_edge(source, target):
            continue
        planar_backbone.add_edge(source, target)
        planar, _embedding = nx.check_planarity(planar_backbone, counterexample=False)
        if planar:
            if len(path) == 2:
                kept_direct += 1
            else:
                kept_flexible += 1
        else:
            planar_backbone.remove_edge(source, target)
            if len(path) == 2:
                rejected_direct += 1
            else:
                rejected_flexible += 1
    print(
        "direct-first planar backbone: "
        f"edges={planar_backbone.number_of_edges()} "
        f"keptDirect={kept_direct} rejectedDirect={rejected_direct} "
        f"keptFlexible={kept_flexible} rejectedFlexible={rejected_flexible}"
    )

    exact_neighborhoods: dict[tuple[int, ...], list[int]] = defaultdict(list)
    hub_signatures: dict[tuple[int, ...], list[int]] = defaultdict(list)
    dominant_hub_groups: dict[int, list[int]] = defaultdict(list)
    for node in core:
        neighbors = tuple(sorted(core.neighbors(node)))
        exact_neighborhoods[neighbors].append(node)
        signature = tuple(neighbor for neighbor in neighbors if neighbor in hub_nodes)
        if signature:
            hub_signatures[signature].append(node)
        candidate_hubs = [neighbor for neighbor in neighbors if neighbor in hub_nodes]
        if candidate_hubs:
            dominant = max(candidate_hubs, key=lambda neighbor: (core_degree[neighbor], -neighbor))
            dominant_hub_groups[dominant].append(node)

    print_group_summary(
        "exact-neighborhood node bundles",
        [members for members in exact_neighborhoods.values() if len(members) >= 2],
        ids,
        degree,
        incident_edges,
        edge_crossings,
        args.top,
    )
    print_group_summary(
        "shared-high-hub-signature node bundles",
        [members for members in hub_signatures.values() if len(members) >= 2],
        ids,
        degree,
        incident_edges,
        edge_crossings,
        args.top,
    )
    print_group_summary(
        "dominant-hub node bundles",
        [members for members in dominant_hub_groups.values() if len(members) >= 2],
        ids,
        degree,
        incident_edges,
        edge_crossings,
        args.top,
    )

    print("top hubs")
    ranked_hubs = sorted(
        hub_nodes,
        key=lambda node: (
            sum(int(edge_crossings[idx]) for idx in incident_edges[node]),
            core_degree[node],
        ),
        reverse=True,
    )
    for node in ranked_hubs[: args.top]:
        owned = dominant_hub_groups.get(node, [])
        pressure = sum(int(edge_crossings[idx]) for idx in incident_edges[node])
        print(
            f"  {ids[node]} degree={core_degree[node]} owned={len(owned)} "
            f"incidentCrossPressure={pressure}"
        )


def print_group_summary(
    title: str,
    groups: list[list[int]],
    ids: list[str],
    degree: dict[int, int],
    incident_edges: dict[int, list[int]],
    edge_crossings: np.ndarray,
    top: int,
) -> None:
    covered = {node for members in groups for node in members}
    print(
        f"{title}: groups={len(groups)} nodes={len(covered)} "
        f"largest={max((len(group) for group in groups), default=0)}"
    )
    ranked = sorted(
        groups,
        key=lambda members: (
            sum(
                int(edge_crossings[edge_idx])
                for node in members
                for edge_idx in incident_edges[node]
            ),
            len(members),
        ),
        reverse=True,
    )
    for members in ranked[:top]:
        pressure = sum(
            int(edge_crossings[edge_idx])
            for node in members
            for edge_idx in incident_edges[node]
        )
        sample = ",".join(ids[node] for node in members[:4])
        print(
            f"  size={len(members):3d} pressure={pressure:5d} "
            f"degrees={sorted(degree[node] for node in members)} sample={sample}"
        )


def degree_two_paths(graph: nx.Graph) -> list[list[int]]:
    """Return maximal paths whose internal vertices all have degree two."""
    anchors = {node for node, degree in graph.degree() if degree != 2}
    visited_edges: set[tuple[int, int]] = set()
    paths: list[list[int]] = []
    for start in sorted(anchors):
        for neighbor in sorted(graph.neighbors(start)):
            first_edge = tuple(sorted((start, neighbor)))
            if first_edge in visited_edges:
                continue
            visited_edges.add(first_edge)
            path = [start, neighbor]
            previous = start
            current = neighbor
            while current not in anchors:
                next_nodes = [node for node in graph.neighbors(current) if node != previous]
                if len(next_nodes) != 1:
                    break
                next_node = next_nodes[0]
                edge_key = tuple(sorted((current, next_node)))
                if edge_key in visited_edges:
                    break
                visited_edges.add(edge_key)
                path.append(next_node)
                previous, current = current, next_node
            paths.append(path)
    return paths


if __name__ == "__main__":
    main()
