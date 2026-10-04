#!/usr/bin/env python3
"""Report exact center-segment crossing pressure without dense matrices."""

from __future__ import annotations

import argparse
from pathlib import Path


def orientation(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def proper_cross(a, b, c, d):
    first = orientation(a, b, c)
    second = orientation(a, b, d)
    third = orientation(c, d, a)
    fourth = orientation(c, d, b)
    return (
        first != 0
        and second != 0
        and third != 0
        and fourth != 0
        and (first < 0) != (second < 0)
        and (third < 0) != (fourth < 0)
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--edges", type=Path, required=True)
    parser.add_argument("--positions", type=Path, required=True)
    parser.add_argument("--top", type=int, default=30)
    args = parser.parse_args()
    positions = {}
    for raw in args.positions.read_text(encoding="utf-8").splitlines():
        fields = raw.split("\t")
        if len(fields) >= 3:
            positions[fields[0]] = (float(fields[1]), float(fields[2]))
    edges = []
    for raw in args.edges.read_text(encoding="utf-8").splitlines():
        fields = raw.split("\t")
        if len(fields) < 3 or fields[1] == fields[2]:
            continue
        weight = int(fields[4]) if len(fields) >= 5 and fields[4].isdigit() else 1
        edges.append((fields[0], fields[1], fields[2], weight))
    pressure = [0] * len(edges)
    pairs = []
    total = 0
    for left, first in enumerate(edges):
        for right in range(left + 1, len(edges)):
            second = edges[right]
            if len({first[1], first[2], second[1], second[2]}) < 4:
                continue
            if proper_cross(
                positions[first[1]], positions[first[2]],
                positions[second[1]], positions[second[2]],
            ):
                cost = first[3] * second[3]
                total += cost
                pressure[left] += cost
                pressure[right] += cost
                pairs.append((cost, left, right))
    print(f"edges={len(edges)} crossingPairs={len(pairs)} weightedCross={total}")
    for value, index in sorted(
        ((value, index) for index, value in enumerate(pressure)),
        reverse=True,
    )[: args.top]:
        edge = edges[index]
        print(
            f"edgePressure={value} weight={edge[3]} id={edge[0]} "
            f"source={edge[1]} target={edge[2]}"
        )
    for cost, left, right in sorted(pairs, reverse=True)[: args.top]:
        print(f"pairCost={cost} left={edges[left][0]} right={edges[right][0]}")


if __name__ == "__main__":
    main()
