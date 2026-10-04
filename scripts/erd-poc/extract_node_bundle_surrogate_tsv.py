#!/usr/bin/env python3
"""Build a layout-only 3-core surrogate for real two-anchor node bundles.

Each eligible one-model shell component contributes one temporary anchor pair
to coordinate search.  The output node set contains only real 3-core models;
the final scene must expand every temporary pair back to its real model and
its two original relationships before it is scored or rendered.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import networkx as nx


def read_positions(path: Path | None) -> dict[str, tuple[float, float]]:
    if path is None:
        return {}
    result: dict[str, tuple[float, float]] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        parts = line.split("\t")
        if len(parts) >= 4 and parts[0] == "N":
            model_id, x_raw, y_raw = parts[1], parts[2], parts[3]
        elif len(parts) >= 3:
            model_id, x_raw, y_raw = parts[0], parts[1], parts[2]
        else:
            continue
        try:
            result[model_id] = (float(x_raw), float(y_raw))
        except ValueError:
            continue
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nodes", type=Path, required=True)
    parser.add_argument("--edges", type=Path, required=True)
    parser.add_argument("--positions", type=Path, default=None)
    parser.add_argument("--out-nodes", type=Path, required=True)
    parser.add_argument("--out-edges", type=Path, required=True)
    args = parser.parse_args()

    node_parts = {
        parts[0]: parts
        for line in args.nodes.read_text(encoding="utf-8").splitlines()
        if len(parts := line.split("\t")) >= 6
    }
    edge_rows: list[tuple[str, str, str]] = []
    graph = nx.Graph()
    graph.add_nodes_from(node_parts)
    for line in args.edges.read_text(encoding="utf-8").splitlines():
        parts = line.split("\t")
        if len(parts) < 5 or parts[1] == parts[2]:
            continue
        edge_rows.append((parts[1], parts[2], line))
        graph.add_edge(parts[1], parts[2])

    giant_nodes = max(nx.biconnected_components(graph), key=len)
    giant = graph.subgraph(giant_nodes).copy()
    core = nx.k_core(giant, k=3)
    core_ids = set(core)
    shell_ids = set(giant) - core_ids
    bundles: list[tuple[str, str, str]] = []
    for component in nx.connected_components(giant.subgraph(shell_ids)):
        if len(component) != 1:
            continue
        model_id = next(iter(component))
        anchors = sorted(set(giant.neighbors(model_id)) & core_ids)
        if len(anchors) == 2:
            bundles.append((model_id, anchors[0], anchors[1]))
    bundles.sort()

    supplied = read_positions(args.positions)
    output_nodes: list[str] = []
    for model_id in sorted(core_ids):
        parts = node_parts[model_id].copy()
        center = supplied.get(model_id)
        if center is not None:
            parts[3] = str(center[0] - float(parts[1]) * 0.5)
            parts[4] = str(center[1] - float(parts[2]) * 0.5)
        output_nodes.append("\t".join(parts))

    output_edges = [
        line
        for source, target, line in edge_rows
        if source in core_ids and target in core_ids
    ]
    output_edges.extend(
        f"layout-node-bundle:{model_id}\t{source}\t{target}"
        "\tnode_bundle_surrogate\tlayout_only"
        for model_id, source, target in bundles
    )
    args.out_nodes.parent.mkdir(parents=True, exist_ok=True)
    args.out_edges.parent.mkdir(parents=True, exist_ok=True)
    args.out_nodes.write_text("\n".join(output_nodes) + "\n", encoding="utf-8")
    args.out_edges.write_text("\n".join(output_edges) + "\n", encoding="utf-8")
    print(
        f"coreNodes={len(core_ids)} coreEdges={len(output_edges) - len(bundles)} "
        f"realNodeBundles={len(bundles)} surrogateEdges={len(output_edges)}"
    )


if __name__ == "__main__":
    main()
