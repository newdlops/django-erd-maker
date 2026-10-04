#!/usr/bin/env python3
"""Summarize crossings between structural shell-node bundles."""

from __future__ import annotations

import argparse
import importlib.util
import sys
from collections import Counter
from pathlib import Path

import networkx as nx

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
    parser.add_argument("--positions", type=Path, required=True)
    parser.add_argument("--top", type=int, default=20)
    args = parser.parse_args()

    layout = v34.load_layout(args.layout)
    positions = v34.read_positions_tsv(args.positions, layout)
    edges = v34.graph_edges(layout)
    graph = nx.Graph()
    graph.add_nodes_from(range(len(layout["nodes"])))
    graph.add_edges_from((int(source), int(target)) for source, target in edges)
    giant = set(max(nx.biconnected_components(graph), key=len))
    giant_graph = graph.subgraph(giant).copy()
    core_numbers = nx.core_number(giant_graph)
    core3 = {node for node, value in core_numbers.items() if value >= 3}
    shell = giant - core3

    component_by_node: dict[int, int] = {}
    component_info: dict[int, tuple[int, tuple[int, ...]]] = {}
    for component_id, raw_component in enumerate(
        nx.connected_components(giant_graph.subgraph(shell))
    ):
        component = {int(node) for node in raw_component}
        anchors = tuple(sorted({
            int(neighbor)
            for node in component
            for neighbor in giant_graph.neighbors(node)
            if neighbor in core3
        }))
        component_info[component_id] = (len(component), anchors)
        for node in component:
            component_by_node[node] = component_id

    def edge_bundle(edge_index: int) -> str:
        source_raw, target_raw = edges[edge_index]
        source = int(source_raw)
        target = int(target_raw)
        if source not in giant or target not in giant:
            return "outside"
        shell_components = {
            component_by_node[node]
            for node in (source, target)
            if node in component_by_node
        }
        if not shell_components:
            return "core"
        if len(shell_components) != 1:
            return "mixed"
        return f"shell:{next(iter(shell_components))}"

    pair_counts: Counter[tuple[str, str]] = Counter()
    class_counts: Counter[str] = Counter()
    incident_counts: Counter[str] = Counter()
    total_crossings = 0
    giant_crossings = 0
    for left_raw, right_raw in v34.fce.iter_crossing_pairs(positions, edges):
        total_crossings += 1
        left_index = int(left_raw)
        right_index = int(right_raw)
        left = edge_bundle(left_index)
        right = edge_bundle(right_index)
        if left == "outside" or right == "outside":
            continue
        giant_crossings += 1
        pair = tuple(sorted((left, right)))
        pair_counts[pair] += 1
        incident_counts[left] += 1
        incident_counts[right] += 1
        if left == "core" and right == "core":
            class_counts["core-core"] += 1
        elif left == right:
            class_counts["inside-shell"] += 1
        elif left == "core" or right == "core":
            class_counts["core-shell"] += 1
        else:
            class_counts["shell-shell"] += 1

    def describe(bundle: str) -> str:
        if not bundle.startswith("shell:"):
            return bundle
        component_id = int(bundle.split(":", 1)[1])
        size, anchors = component_info[component_id]
        return f"{bundle}(nodes={size},anchors={len(anchors)})"

    print(
        f"giantNodes={len(giant)} core3={len(core3)} "
        f"shellComponents={len(component_info)} totalCrossings={total_crossings} "
        f"giantCrossings={giant_crossings} "
        f"classes={dict(class_counts)}"
    )
    print("top bundle pairs")
    for (left, right), count in pair_counts.most_common(args.top):
        print(f"  {count:5d} {describe(left)} x {describe(right)}")
    print("top bundle pressure")
    for bundle, count in incident_counts.most_common(args.top):
        print(f"  {count:5d} {describe(bundle)}")


if __name__ == "__main__":
    main()
