#!/usr/bin/env python3
"""Classify exact straight-route crossings against a prior graph snapshot."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path


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


def route_pair(edge: dict) -> tuple[str, str]:
    return tuple(sorted((str(edge["sourceModelId"]), str(edge["targetModelId"]))))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--reference-layout", type=Path, required=True)
    parser.add_argument("--edges", type=Path, required=True)
    parser.add_argument("--top", type=int, default=20)
    args = parser.parse_args()

    layout = json.loads(args.layout.read_text(encoding="utf-8"))
    reference = json.loads(args.reference_layout.read_text(encoding="utf-8"))
    old_pairs = {
        route_pair(edge)
        for edge in reference.get("routedEdges", [])
        if edge.get("sourceModelId") and edge.get("targetModelId")
    }
    kind_by_id: dict[str, str] = {}
    kind_by_pair: dict[tuple[str, str], str] = {}
    for line in args.edges.read_text(encoding="utf-8").splitlines():
        fields = line.split("\t")
        if len(fields) < 4:
            continue
        kind_by_id[fields[0]] = fields[3]
        kind_by_pair[tuple(sorted((fields[1], fields[2])))] = fields[3]

    routes: list[tuple[str, tuple[str, str], str, tuple[float, float], tuple[float, float]]] = []
    for edge in layout.get("routedEdges", []):
        points = edge.get("points") or []
        if len(points) != 2 or not edge.get("sourceModelId") or not edge.get("targetModelId"):
            continue
        edge_id = str(edge.get("edgeId") or edge.get("id") or "")
        pair = route_pair(edge)
        routes.append((
            edge_id,
            pair,
            kind_by_id.get(edge_id, kind_by_pair.get(pair, "unknown")),
            (float(points[0]["x"]), float(points[0]["y"])),
            (float(points[1]["x"]), float(points[1]["y"])),
        ))

    classes: Counter[tuple[str, str]] = Counter()
    kinds: Counter[tuple[str, str]] = Counter()
    edge_pressure: Counter[str] = Counter()
    node_pressure: Counter[str] = Counter()
    total = 0
    for left_index, left in enumerate(routes):
        left_id, left_pair, left_kind, left_start, left_end = left
        left_class = "retained" if left_pair in old_pairs else "added"
        for right in routes[left_index + 1 :]:
            right_id, right_pair, right_kind, right_start, right_end = right
            if set(left_pair) & set(right_pair):
                continue
            if not proper_cross(left_start, left_end, right_start, right_end):
                continue
            total += 1
            right_class = "retained" if right_pair in old_pairs else "added"
            classes[tuple(sorted((left_class, right_class)))] += 1
            kinds[tuple(sorted((left_kind, right_kind)))] += 1
            edge_pressure[left_id] += 1
            edge_pressure[right_id] += 1
            for model_id in left_pair + right_pair:
                node_pressure[model_id] += 1

    print(f"routes={len(routes)} properCrossings={total}")
    print(f"classes={dict(classes)}")
    print(f"kinds={dict(kinds)}")
    print("top edge pressure")
    route_by_id = {edge_id: (pair, kind) for edge_id, pair, kind, _a, _b in routes}
    for edge_id, count in edge_pressure.most_common(args.top):
        pair, kind = route_by_id[edge_id]
        marker = "retained" if pair in old_pairs else "added"
        print(f"  {count:5d} {marker:8s} {kind:14s} {pair[0]} -- {pair[1]}")
    print("top node pressure")
    for model_id, count in node_pressure.most_common(args.top):
        print(f"  {count:5d} {model_id}")


if __name__ == "__main__":
    main()
