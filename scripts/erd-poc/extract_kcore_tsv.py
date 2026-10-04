#!/usr/bin/env python3
"""Extract a graph k-core from native node/edge TSV inputs."""

from __future__ import annotations

import argparse
from pathlib import Path

import networkx as nx


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nodes", type=Path, required=True)
    parser.add_argument("--edges", type=Path, required=True)
    parser.add_argument("--k", type=int, default=3)
    parser.add_argument("--include-single-shell", action="store_true")
    parser.add_argument("--out-nodes", type=Path, required=True)
    parser.add_argument("--out-edges", type=Path, required=True)
    args = parser.parse_args()

    node_rows = {
        parts[0]: line
        for line in args.nodes.read_text(encoding="utf-8").splitlines()
        if len(parts := line.split("\t")) >= 3
    }
    edge_records: list[tuple[str, str, str, str]] = []
    graph = nx.Graph()
    graph.add_nodes_from(node_rows)
    for line in args.edges.read_text(encoding="utf-8").splitlines():
        parts = line.split("\t")
        if len(parts) < 5 or parts[1] == parts[2]:
            continue
        edge_records.append((parts[1], parts[2], parts[0], line))
        graph.add_edge(parts[1], parts[2])

    blocks = list(nx.biconnected_components(graph))
    giant = graph.subgraph(max(blocks, key=len)).copy()
    core = nx.k_core(giant, k=args.k)
    member_ids = set(core)
    if args.include_single_shell:
        shell = set(giant) - member_ids
        member_ids.update(
            next(iter(component))
            for component in nx.connected_components(giant.subgraph(shell))
            if len(component) == 1
        )
    kept_edges = [
        line
        for source, target, _edge_id, line in edge_records
        if source in member_ids and target in member_ids
    ]
    args.out_nodes.parent.mkdir(parents=True, exist_ok=True)
    args.out_edges.parent.mkdir(parents=True, exist_ok=True)
    args.out_nodes.write_text(
        "\n".join(node_rows[model_id] for model_id in sorted(member_ids)) + "\n",
        encoding="utf-8",
    )
    args.out_edges.write_text("\n".join(kept_edges) + "\n", encoding="utf-8")
    print(
        f"wrote k={args.k} core singleShell={int(args.include_single_shell)} "
        f"nodes={len(member_ids)} edges={len(kept_edges)} "
        f"to {args.out_nodes} and {args.out_edges}"
    )


if __name__ == "__main__":
    main()
