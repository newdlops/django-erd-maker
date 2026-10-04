#!/usr/bin/env python3
"""Explain crossings introduced when a related ERD snapshot changes.

Model ids and undirected direct relationships are compared with a reference
layout.  The report distinguishes crossings among retained relationships from
those involving newly introduced relationships.  It never changes geometry.
"""

from __future__ import annotations

import argparse
import importlib.util
import sys
from collections import Counter
from pathlib import Path

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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--positions", type=Path, required=True)
    parser.add_argument("--reference-layout", type=Path, required=True)
    parser.add_argument("--edge-file", type=Path, default=None)
    parser.add_argument("--top", type=int, default=20)
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
    retained_pairs = current_pairs & reference_pairs
    added_pairs = current_pairs - reference_pairs
    current_pair_kind: dict[tuple[str, str], str] = {}
    if args.edge_file is not None:
        for line in args.edge_file.read_text(encoding="utf-8").splitlines():
            fields = line.split("\t")
            if len(fields) < 4 or fields[1] == fields[2]:
                continue
            current_pair_kind[tuple(sorted((fields[1], fields[2])))] = fields[3]
    else:
        for edge in layout.get("routedEdges", []):
            source = str(edge.get("sourceModelId", ""))
            target = str(edge.get("targetModelId", ""))
            if not source or not target or source == target:
                continue
            pair = tuple(sorted((source, target)))
            edge_id = str(edge.get("id", ""))
            kind = "inheritance" if edge_id.startswith("edge:inheritance:") else "declared"
            current_pair_kind[pair] = kind

    edge_class: list[str] = []
    edge_pair: list[tuple[str, str]] = []
    for source_raw, target_raw in edges:
        source = int(source_raw)
        target = int(target_raw)
        pair = tuple(sorted((model_ids[source], model_ids[target])))
        edge_pair.append(pair)
        edge_class.append("retained" if pair in retained_pairs else "added")

    crossing_classes: Counter[tuple[str, str]] = Counter()
    crossing_kinds: Counter[tuple[str, str]] = Counter()
    added_edge_pressure: Counter[tuple[str, str]] = Counter()
    node_pressure: Counter[str] = Counter()
    total = 0
    for left_raw, right_raw in v34.fce.iter_crossing_pairs(positions, edges):
        left = int(left_raw)
        right = int(right_raw)
        total += 1
        crossing_classes[tuple(sorted((edge_class[left], edge_class[right])))] += 1
        crossing_kinds[tuple(sorted((
            current_pair_kind.get(edge_pair[left], "unknown"),
            current_pair_kind.get(edge_pair[right], "unknown"),
        )))] += 1
        for edge_index in (left, right):
            source_raw, target_raw = edges[edge_index]
            node_pressure[model_ids[int(source_raw)]] += 1
            node_pressure[model_ids[int(target_raw)]] += 1
            if edge_class[edge_index] == "added":
                added_edge_pressure[edge_pair[edge_index]] += 1

    current_ids = set(model_ids)
    reference_ids = {
        str(node["modelId"]) for node in reference.get("nodes", [])
    }
    changed_frontier = {
        model_id
        for pair in added_pairs | (reference_pairs - current_pairs)
        for model_id in pair
        if model_id in current_ids
    }
    changed_adjacency = {model_id: set() for model_id in changed_frontier}
    for source, target in current_pairs ^ reference_pairs:
        if source in changed_adjacency and target in changed_adjacency:
            changed_adjacency[source].add(target)
            changed_adjacency[target].add(source)
    changed_components: list[set[str]] = []
    unseen = set(changed_adjacency)
    while unseen:
        start = min(unseen)
        component = {start}
        pending = [start]
        unseen.remove(start)
        while pending:
            node = pending.pop()
            for neighbor in changed_adjacency[node]:
                if neighbor not in unseen:
                    continue
                unseen.remove(neighbor)
                component.add(neighbor)
                pending.append(neighbor)
        changed_components.append(component)
    changed_components.sort(key=lambda component: (-len(component), min(component)))
    print(
        f"currentNodes={len(current_ids)} commonNodes={len(current_ids & reference_ids)} "
        f"newNodes={len(current_ids - reference_ids)} "
        f"removedNodes={len(reference_ids - current_ids)}"
    )
    print(
        f"currentEdges={len(current_pairs)} retainedEdges={len(retained_pairs)} "
        f"addedEdges={len(added_pairs)} removedEdges={len(reference_pairs - current_pairs)} "
        f"changedFrontierNodes={len(changed_frontier)}"
    )
    print(
        "addedEdgeKinds="
        + str(dict(Counter(current_pair_kind.get(pair, "unknown") for pair in added_pairs)))
    )
    print(
        f"changedComponents={len(changed_components)} "
        f"largestChangedComponents={[len(component) for component in changed_components[:12]]}"
    )
    print(
        f"totalCrossings={total} classes={dict(crossing_classes)} "
        f"relationshipKinds={dict(crossing_kinds)}"
    )
    print("top added relationship pressure")
    for (source, target), count in added_edge_pressure.most_common(args.top):
        print(f"  {count:5d} {source} -- {target}")
    print("top node pressure")
    for model_id, count in node_pressure.most_common(args.top):
        marker = "changed" if model_id in changed_frontier else "stable"
        print(f"  {count:5d} {marker:7s} {model_id}")
    print("top changed-frontier degree")
    for model_id, neighbors in sorted(
        changed_adjacency.items(),
        key=lambda item: (-len(item[1]), item[0]),
    )[: args.top]:
        print(f"  {len(neighbors):5d} {model_id}")


if __name__ == "__main__":
    main()
