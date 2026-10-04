#!/usr/bin/env python3
"""Export deterministic Louvain labels near a requested community count."""

from __future__ import annotations

import argparse
from pathlib import Path

import networkx as nx


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nodes", type=Path, required=True)
    parser.add_argument("--edges", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--target", type=int, default=150)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    nodes = [
        raw.split("\t", 1)[0]
        for raw in args.nodes.read_text(encoding="utf-8").splitlines()
        if raw
    ]
    graph = nx.Graph()
    graph.add_nodes_from(nodes)
    for raw in args.edges.read_text(encoding="utf-8").splitlines():
        if not raw:
            continue
        fields = raw.split("\t")
        if len(fields) >= 3 and fields[1] != fields[2]:
            graph.add_edge(fields[1], fields[2])

    resolutions = [
        0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0,
        8.0, 12.0, 16.0, 24.0, 32.0, 36.0, 40.0, 44.0, 48.0, 64.0,
    ]
    choices: list[tuple[int, float, list[set[str]]]] = []
    for resolution in resolutions:
        communities = [
            set(value)
            for value in nx.community.louvain_communities(
                graph,
                weight=None,
                resolution=resolution,
                seed=args.seed,
            )
        ]
        choices.append((abs(len(communities) - args.target), resolution, communities))
        print(f"resolution={resolution:g} communities={len(communities)}")
    _distance, resolution, communities = min(
        choices,
        key=lambda item: (item[0], item[1]),
    )
    communities.sort(key=lambda members: min(members))
    label_by_node = {
        node: f"_louv_probe_{index}"
        for index, members in enumerate(communities)
        for node in members
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        "".join(f"{node}\t{label_by_node[node]}\n" for node in nodes),
        encoding="utf-8",
    )
    print(
        f"selectedResolution={resolution:g} communities={len(communities)} "
        f"target={args.target} out={args.out}"
    )


if __name__ == "__main__":
    main()
