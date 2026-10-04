#!/usr/bin/env python3
"""Explain which graph structures account for straight-line crossings."""

from __future__ import annotations

import argparse
import importlib.util
import sys
from collections import Counter
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
    parser.add_argument("--top", type=int, default=20)
    parser.add_argument(
        "--scope",
        choices=("all", "core3", "core3-single", "giant"),
        default="all",
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
    graph.add_edges_from((int(s), int(t)) for s, t in edges)
    giant_nodes = set(max(nx.biconnected_components(graph), key=len))
    core_numbers_giant = nx.core_number(graph.subgraph(giant_nodes))
    core3_nodes = {
        node for node, value in core_numbers_giant.items() if value >= 3
    }
    if args.scope == "giant":
        scope_nodes = giant_nodes
    elif args.scope == "core3":
        scope_nodes = core3_nodes
    elif args.scope == "core3-single":
        shell = giant_nodes - core3_nodes
        scope_nodes = core3_nodes | {
            next(iter(component))
            for component in nx.connected_components(graph.subgraph(shell))
            if len(component) == 1
        }
    else:
        scope_nodes = set(graph)
    edge_scope = np.asarray([
        int(source) in scope_nodes and int(target) in scope_nodes
        for source, target in edges
    ], dtype=bool)
    degree = dict(graph.degree())
    core = nx.core_number(graph)
    bridges = {tuple(sorted(edge)) for edge in nx.bridges(graph)}
    block_edges = [
        [tuple(sorted((int(s), int(t)))) for s, t in component]
        for component in nx.biconnected_component_edges(graph)
    ]
    block_nodes = [
        {node for edge in component for node in edge}
        for component in block_edges
    ]
    edge_block: dict[tuple[int, int], int] = {}
    for block_idx, component in enumerate(block_edges):
        for edge_key in component:
            edge_block[edge_key] = block_idx
    articulation_points = set(nx.articulation_points(graph))

    all_routes = [
        route
        for route in layout.get("routedEdges", [])
        if route.get("sourceModelId")
        and route.get("targetModelId")
        and route.get("sourceModelId") != route.get("targetModelId")
    ]
    if len(all_routes) != edges.shape[0]:
        raise RuntimeError("route order does not match evaluator edge order")
    routes = [route for route, keep in zip(all_routes, edge_scope, strict=True) if keep]
    edges = edges[edge_scope]

    labels: list[tuple[str, ...]] = []
    primary: list[str] = []
    for edge_idx, (s_raw, t_raw) in enumerate(edges):
        s = int(s_raw)
        t = int(t_raw)
        edge_labels: list[str] = []
        edge_labels.append(
            "inheritance" if ":inheritance:" in str(routes[edge_idx].get("edgeId"))
            else "declared"
        )
        if min(degree[s], degree[t]) == 1:
            edge_labels.append("leaf")
            edge_primary = "leaf"
        elif tuple(sorted((s, t))) in bridges:
            edge_labels.append("bridge")
            edge_primary = "bridge"
        elif min(core[s], core[t]) >= 3:
            edge_labels.append("core3")
            edge_primary = "core3"
        else:
            edge_labels.append("core2")
            edge_primary = "core2"
        if max(degree[s], degree[t]) >= 20:
            edge_labels.append("hub20")
        labels.append(tuple(edge_labels))
        primary.append(edge_primary)

    # Keep memory proportional to the crossings that actually exist. The old
    # FastCrossEval path materialised every eligible edge pair and several
    # endpoint arrays, exceeding the research process's 256 MiB budget on the
    # canonical ERD.
    crossing_pairs = list(v34.fce.iter_crossing_pairs(positions, edges))
    pair_i = [pair[0] for pair in crossing_pairs]
    pair_j = [pair[1] for pair in crossing_pairs]
    pair_counts: Counter[str] = Counter()
    tag_counts: Counter[str] = Counter()
    block_pair_counts: Counter[tuple[int, int]] = Counter()
    block_incident_counts: Counter[int] = Counter()
    same_block_crossings = 0
    different_block_crossings = 0
    edge_counts = np.zeros(edges.shape[0], dtype=np.int32)
    for a_raw, b_raw in zip(pair_i, pair_j, strict=True):
        a = int(a_raw)
        b = int(b_raw)
        pair_counts[" × ".join(sorted((primary[a], primary[b])))] += 1
        for tag_a in labels[a]:
            for tag_b in labels[b]:
                tag_counts[" × ".join(sorted((tag_a, tag_b)))] += 1
        a_key = tuple(sorted((int(edges[a, 0]), int(edges[a, 1]))))
        b_key = tuple(sorted((int(edges[b, 0]), int(edges[b, 1]))))
        a_block = edge_block[a_key]
        b_block = edge_block[b_key]
        if a_block == b_block:
            same_block_crossings += 1
        else:
            different_block_crossings += 1
        block_pair_counts[tuple(sorted((a_block, b_block)))] += 1
        block_incident_counts[a_block] += 1
        block_incident_counts[b_block] += 1
        edge_counts[a] += 1
        edge_counts[b] += 1

    ids = [str(node["modelId"]) for node in layout["nodes"]]
    metadata = layout.get("engineMetadata") or {}
    cluster_by_model = metadata.get("clusterByModelId") or {}
    cluster_by_node = [
        str(cluster_by_model.get(model_id) or f"__unclustered__:{model_id}")
        for model_id in ids
    ]
    edge_cluster_pairs = [
        tuple(sorted((cluster_by_node[int(source)], cluster_by_node[int(target)])))
        for source, target in edges
    ]
    cluster_crossing_classes: Counter[str] = Counter()
    cluster_signature_crossings: Counter[str] = Counter()
    for a_raw, b_raw in zip(pair_i, pair_j, strict=True):
        a = int(a_raw)
        b = int(b_raw)
        a_pair = edge_cluster_pairs[a]
        b_pair = edge_cluster_pairs[b]
        a_internal = a_pair[0] == a_pair[1]
        b_internal = b_pair[0] == b_pair[1]
        if a_internal and b_internal and a_pair[0] == b_pair[0]:
            category = "same-bundle internal × internal"
        elif a_internal and b_internal:
            category = "different-bundle internal × internal"
        elif a_internal or b_internal:
            category = "internal × boundary"
        else:
            category = "boundary × boundary"
        cluster_crossing_classes[category] += 1
        signature = " | ".join(sorted(set((*a_pair, *b_pair))))
        cluster_signature_crossings[signature] += 1
    print(
        f"scope={args.scope} scopeNodes={len(scope_nodes)} "
        f"nodes={graph.number_of_nodes()} edges={edges.shape[0]} "
        f"crossings={len(crossing_pairs)} bridges={len(bridges)} "
        f"components={nx.number_connected_components(graph)} "
        f"articulations={len(articulation_points)} blocks={len(block_edges)}"
    )
    print(
        "block crossing split "
        f"same={same_block_crossings} different={different_block_crossings}"
    )
    print("top biconnected blocks")
    ranked_blocks = sorted(
        range(len(block_edges)),
        key=lambda idx: (len(block_edges[idx]), len(block_nodes[idx])),
        reverse=True,
    )
    for block_idx in ranked_blocks[: args.top]:
        component = nx.Graph()
        component.add_edges_from(block_edges[block_idx])
        planar, _embedding = nx.check_planarity(component, counterexample=False)
        internal = block_pair_counts[(block_idx, block_idx)]
        incident = block_incident_counts[block_idx]
        external = incident - internal * 2
        print(
            f"  block={block_idx:4d} nodes={len(block_nodes[block_idx]):4d} "
            f"edges={len(block_edges[block_idx]):4d} planar={str(planar).lower():5s} "
            f"internalCross={internal:4d} externalCross={external:4d}"
        )
    print("top crossing block pairs")
    for (a_block, b_block), count in block_pair_counts.most_common(args.top):
        relation = "same" if a_block == b_block else "different"
        print(
            f"  {count:4d} blocks=({a_block},{b_block}) {relation} "
            f"sizes=({len(block_nodes[a_block])},{len(block_nodes[b_block])})"
        )
    print("primary crossing pairs")
    for key, count in pair_counts.most_common():
        print(f"  {key}: {count}")
    print("node-bundle crossing classes")
    for key, count in cluster_crossing_classes.most_common():
        print(f"  {key}: {count}")
    print("top node-bundle crossing signatures")
    for key, count in cluster_signature_crossings.most_common(args.top):
        print(f"  {count:4d} {key}")
    print("selected tag crossing pairs")
    for key, count in tag_counts.most_common(24):
        print(f"  {key}: {count}")
    print("top crossing edges")
    for edge_idx in np.argsort(-edge_counts)[: args.top]:
        count = int(edge_counts[edge_idx])
        if count <= 0:
            break
        s, t = (int(value) for value in edges[edge_idx])
        print(
            f"  {count:4d} {ids[s]} -> {ids[t]} "
            f"degree=({degree[s]},{degree[t]}) tags={','.join(labels[edge_idx])}"
        )


if __name__ == "__main__":
    main()
