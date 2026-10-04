#!/usr/bin/env python3
"""Search planar-backbone embeddings for an anchor node-bundle graph."""

from __future__ import annotations

import argparse
import math
import random
import resource
import sys
from pathlib import Path

import networkx as nx


def orient(a: tuple[float, float], b: tuple[float, float], c: tuple[float, float]) -> float:
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def proper_cross(
    a: tuple[float, float],
    b: tuple[float, float],
    c: tuple[float, float],
    d: tuple[float, float],
) -> bool:
    first = orient(a, b, c)
    second = orient(a, b, d)
    third = orient(c, d, a)
    fourth = orient(c, d, b)
    return (
        first != 0.0
        and second != 0.0
        and third != 0.0
        and fourth != 0.0
        and (first < 0.0) != (second < 0.0)
        and (third < 0.0) != (fourth < 0.0)
    )


def weighted_crossings(
    edges: list[tuple[str, str, int]],
    positions: dict[str, tuple[float, float]],
) -> int:
    result = 0
    for left, (source, target, weight) in enumerate(edges):
        for other_source, other_target, other_weight in edges[left + 1 :]:
            if len({source, target, other_source, other_target}) < 4:
                continue
            if proper_cross(
                positions[source],
                positions[target],
                positions[other_source],
                positions[other_target],
            ):
                result += weight * other_weight
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nodes", type=Path, required=True)
    parser.add_argument("--edges", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=16)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--scale", type=float, default=1200.0)
    args = parser.parse_args()

    nodes = [
        raw.split("\t", 1)[0]
        for raw in args.nodes.read_text(encoding="utf-8").splitlines()
        if raw
    ]
    edges: list[tuple[str, str, int]] = []
    for raw in args.edges.read_text(encoding="utf-8").splitlines():
        if not raw:
            continue
        fields = raw.split("\t")
        edges.append((fields[1], fields[2], int(fields[4]) if len(fields) >= 5 else 1))

    best_cost = math.inf
    best_positions: dict[str, tuple[float, float]] = {}
    rng = random.Random(args.seed)
    for run in range(max(1, args.runs)):
        tie = {tuple(sorted((source, target))): rng.random() for source, target, _ in edges}
        ordered = sorted(
            edges,
            key=lambda edge: (
                -edge[2],
                tie[tuple(sorted((edge[0], edge[1])))],
                edge[0],
                edge[1],
            ),
        )
        backbone = nx.Graph()
        backbone.add_nodes_from(nodes)
        deleted = 0
        for source, target, _weight in ordered:
            backbone.add_edge(source, target)
            planar, _embedding = nx.check_planarity(backbone, counterexample=False)
            if not planar:
                backbone.remove_edge(source, target)
                deleted += 1
        planar, embedding = nx.check_planarity(backbone, counterexample=False)
        if not planar:
            raise RuntimeError("constructed backbone is not planar")
        raw_positions = nx.combinatorial_embedding_to_pos(
            embedding,
            fully_triangulate=True,
        )
        positions = {
            node: (float(point[0]), float(point[1]))
            for node, point in raw_positions.items()
        }
        cost = weighted_crossings(edges, positions)
        print(
            f"run={run + 1}/{args.runs} kept={backbone.number_of_edges()} "
            f"deleted={deleted} weightedCross={cost}",
            flush=True,
        )
        if cost < best_cost:
            best_cost = cost
            best_positions = positions

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        "".join(
            f"{node}\t{best_positions[node][0] * args.scale:.9f}"
            f"\t{best_positions[node][1] * args.scale:.9f}\n"
            for node in nodes
        ),
        encoding="utf-8",
    )
    peak_raw = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    peak_mib = peak_raw / (1024.0 * 1024.0) if sys.platform == "darwin" else peak_raw / 1024.0
    print(f"done best={best_cost} out={args.out} peakMiB={peak_mib:.1f}")


if __name__ == "__main__":
    main()
