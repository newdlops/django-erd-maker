#!/usr/bin/env python3
"""Export a low-memory anchor graph for tolerant real-node bundles.

Every model connected to the selected core is assigned to its nearest real
core anchor.  Equal-distance choices prefer the higher-degree anchor and then
the stable model id.  Models do not need matching fields or relationship
signatures: their differing external relationships become weights between
anchor families.  The anchor graph is optimizer-only; the renderer still gets
every original model and every original relationship.
"""

from __future__ import annotations

import argparse
import heapq
from collections import Counter, defaultdict, deque
from pathlib import Path


def read_nodes(path: Path) -> tuple[list[str], dict[str, tuple[float, float]]]:
    order: list[str] = []
    sizes: dict[str, tuple[float, float]] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        if not raw:
            continue
        model_id, width, height, *_rest = raw.split("\t")
        order.append(model_id)
        sizes[model_id] = (float(width), float(height))
    return order, sizes


def read_edges(path: Path) -> list[tuple[str, str, str]]:
    result: list[tuple[str, str, str]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        if not raw:
            continue
        fields = raw.split("\t")
        if len(fields) < 4 or fields[1] == fields[2]:
            continue
        result.append((fields[1], fields[2], fields[3]))
    return result


def read_core_ids(path: Path) -> set[str]:
    return {
        raw.split("\t", 1)[0]
        for raw in path.read_text(encoding="utf-8").splitlines()
        if raw
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nodes", type=Path, required=True)
    parser.add_argument("--edges", type=Path, required=True)
    parser.add_argument("--core-nodes", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()

    node_order, sizes = read_nodes(args.nodes)
    edges = read_edges(args.edges)
    core = read_core_ids(args.core_nodes)
    adjacency: dict[str, set[str]] = {model_id: set() for model_id in node_order}
    for source, target, _kind in edges:
        adjacency[source].add(target)
        adjacency[target].add(source)

    # Multi-source shortest paths.  The priority tuple is also the complete
    # deterministic tie-break, so a data node may differ arbitrarily without
    # being ejected from the nearest semantic family.
    best: dict[str, tuple[int, int, str]] = {}
    queue: list[tuple[int, int, str, str]] = []
    for anchor in sorted(core):
        rank = -len(adjacency[anchor])
        best[anchor] = (0, rank, anchor)
        heapq.heappush(queue, (0, rank, anchor, anchor))
    while queue:
        distance, rank, anchor, node = heapq.heappop(queue)
        if best.get(node) != (distance, rank, anchor):
            continue
        for neighbor in adjacency[node]:
            proposal = (distance + 1, rank, anchor)
            if neighbor in core and neighbor != anchor:
                continue
            if neighbor not in best or proposal < best[neighbor]:
                best[neighbor] = proposal
                heapq.heappush(queue, (*proposal, neighbor))

    assignment = {node: value[2] for node, value in best.items()}
    members: dict[str, list[str]] = defaultdict(list)
    for node, anchor in assignment.items():
        members[anchor].append(node)
    for family in members.values():
        family.sort()

    meta_weights: Counter[tuple[str, str]] = Counter()
    boundary_nodes: set[str] = set()
    internal_edges = 0
    detached_edges = 0
    for source, target, _kind in edges:
        source_anchor = assignment.get(source)
        target_anchor = assignment.get(target)
        if source_anchor is None or target_anchor is None:
            detached_edges += 1
            continue
        if source_anchor == target_anchor:
            internal_edges += 1
            continue
        boundary_nodes.add(source)
        boundary_nodes.add(target)
        meta_weights[tuple(sorted((source_anchor, target_anchor)))] += 1

    args.out_dir.mkdir(parents=True, exist_ok=True)
    (args.out_dir / "nodes.tsv").write_text(
        "".join(
            f"{anchor}\t{sizes[anchor][0]:.9f}\t{sizes[anchor][1]:.9f}\n"
            for anchor in sorted(core)
        ),
        encoding="utf-8",
    )
    (args.out_dir / "edges.tsv").write_text(
        "".join(
            f"anchor-edge:{index}\t{source}\t{target}\tanchor\t{weight}\n"
            for index, ((source, target), weight) in enumerate(
                sorted(meta_weights.items())
            )
        ),
        encoding="utf-8",
    )
    (args.out_dir / "edges-unweighted.tsv").write_text(
        "".join(
            f"anchor-edge:{index}\t{source}\t{target}\tanchor\n"
            for index, (source, target) in enumerate(sorted(meta_weights))
        ),
        encoding="utf-8",
    )
    (args.out_dir / "assignments.tsv").write_text(
        "".join(
            f"{node}\t{assignment.get(node, '')}\n"
            for node in node_order
        ),
        encoding="utf-8",
    )
    (args.out_dir / "families.tsv").write_text(
        "".join(
            f"{anchor}\t{node}\n"
            for anchor in sorted(core)
            for node in members.get(anchor, [])
        ),
        encoding="utf-8",
    )
    covered = len(assignment)
    family_sizes = sorted((len(value) for value in members.values()), reverse=True)
    print(
        f"anchors={len(core)} covered={covered}/{len(node_order)} "
        f"families={len(members)} metaEdges={len(meta_weights)} "
        f"metaWeight={sum(meta_weights.values())} internalEdges={internal_edges} "
        f"boundaryNodes={len(boundary_nodes)} detachedEdges={detached_edges} "
        f"largest={family_sizes[:12]}"
    )


if __name__ == "__main__":
    main()
