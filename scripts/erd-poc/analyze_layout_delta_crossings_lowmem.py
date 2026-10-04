#!/usr/bin/env python3
"""Explain changed-snapshot straight crossings without NumPy.

The analyzer intentionally keeps only O(nodes + edges) state.  It treats
added, removed, and relationship-changed model data as normal input and never
requires the current and reference node sets to match.
"""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path


def load_positions(path: Path) -> dict[str, tuple[float, float]]:
    result: dict[str, tuple[float, float]] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        fields = line.split("\t")
        if fields[:1] == ["N"]:
            fields = fields[1:]
        if len(fields) < 3:
            continue
        try:
            result[fields[0]] = (float(fields[1]), float(fields[2]))
        except ValueError:
            continue
    return result


def reference_pairs(path: Path) -> set[tuple[str, str]]:
    layout = json.loads(path.read_text(encoding="utf-8"))
    return {
        tuple(sorted((str(edge["sourceModelId"]), str(edge["targetModelId"]))))
        for edge in layout.get("routedEdges", [])
        if edge.get("sourceModelId")
        and edge.get("targetModelId")
        and edge["sourceModelId"] != edge["targetModelId"]
    }


def reference_nodes(path: Path) -> set[str]:
    layout = json.loads(path.read_text(encoding="utf-8"))
    return {str(node["modelId"]) for node in layout.get("nodes", [])}


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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--positions", type=Path, required=True)
    parser.add_argument("--reference-layout", type=Path, required=True)
    parser.add_argument("--edges", type=Path, required=True)
    parser.add_argument("--top", type=int, default=20)
    args = parser.parse_args()

    current_layout = json.loads(args.layout.read_text(encoding="utf-8"))
    current_nodes = {str(node["modelId"]) for node in current_layout.get("nodes", [])}
    positions = load_positions(args.positions)
    missing_positions = current_nodes - positions.keys()
    if missing_positions:
        raise ValueError(f"missing positions: {len(missing_positions)}")

    edges: list[tuple[str, str, str]] = []
    seen: set[tuple[str, str]] = set()
    for line in args.edges.read_text(encoding="utf-8").splitlines():
        fields = line.split("\t")
        if len(fields) < 4 or fields[1] == fields[2]:
            continue
        pair = tuple(sorted((fields[1], fields[2])))
        if pair in seen:
            continue
        seen.add(pair)
        edges.append((pair[0], pair[1], fields[3]))

    old_pairs = reference_pairs(args.reference_layout)
    old_nodes = reference_nodes(args.reference_layout)
    current_pairs = {(source, target) for source, target, _kind in edges}
    retained = current_pairs & old_pairs
    added = current_pairs - old_pairs
    removed = old_pairs - current_pairs
    frontier = {
        node
        for pair in added | removed
        for node in pair
        if node in current_nodes
    }

    classes: Counter[tuple[str, str]] = Counter()
    kinds: Counter[tuple[str, str]] = Counter()
    edge_pressure: Counter[tuple[str, str]] = Counter()
    node_pressure: Counter[str] = Counter()
    total = 0
    for left_index, (left_source, left_target, left_kind) in enumerate(edges):
        left_pair = (left_source, left_target)
        left_class = "retained" if left_pair in retained else "added"
        for right_source, right_target, right_kind in edges[left_index + 1 :]:
            if len({left_source, left_target, right_source, right_target}) < 4:
                continue
            if not proper_cross(
                positions[left_source], positions[left_target],
                positions[right_source], positions[right_target],
            ):
                continue
            total += 1
            right_pair = (right_source, right_target)
            right_class = "retained" if right_pair in retained else "added"
            classes[tuple(sorted((left_class, right_class)))] += 1
            kinds[tuple(sorted((left_kind, right_kind)))] += 1
            for pair, edge_class in ((left_pair, left_class), (right_pair, right_class)):
                node_pressure[pair[0]] += 1
                node_pressure[pair[1]] += 1
                if edge_class == "added":
                    edge_pressure[pair] += 1

    print(
        f"currentNodes={len(current_nodes)} commonNodes={len(current_nodes & old_nodes)} "
        f"newNodes={len(current_nodes - old_nodes)} removedNodes={len(old_nodes - current_nodes)}"
    )
    print(
        f"currentEdges={len(current_pairs)} retainedEdges={len(retained)} "
        f"addedEdges={len(added)} removedEdges={len(removed)} "
        f"changedFrontierNodes={len(frontier)}"
    )
    print(f"addedKinds={dict(Counter(kind for source, target, kind in edges if (source, target) in added))}")
    print(f"totalCrossings={total} classes={dict(classes)} kinds={dict(kinds)}")
    print("top added relationship pressure")
    for (source, target), count in edge_pressure.most_common(args.top):
        print(f"  {count:5d} {source} -- {target}")
    print("top node pressure")
    for model_id, count in node_pressure.most_common(args.top):
        marker = "changed" if model_id in frontier else "stable"
        print(f"  {count:5d} {marker:7s} {model_id}")


if __name__ == "__main__":
    main()
