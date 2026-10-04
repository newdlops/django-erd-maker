#!/usr/bin/env python3
"""Export the 3-core plus real shell-component centroids for meta-layout.

Centroid records are optimizer-only variables for groups of real model nodes;
they are never emitted to the ERD renderer.  The expansion step maps every
centroid back to all of its real members and preserves every original edge.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import math
import sys
from pathlib import Path

import networkx as nx
import numpy as np

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
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--centroid-unit", type=float, default=180.0)
    args = parser.parse_args()

    layout = v34.load_layout(args.layout)
    positions = v34.read_positions_tsv(args.positions, layout)
    edges = v34.graph_edges(layout)
    model_ids = [str(node["modelId"]) for node in layout["nodes"]]
    widths, heights = v34.render_node_sizes(layout, edges, direct_scene=True)
    graph = nx.Graph()
    graph.add_nodes_from(range(len(model_ids)))
    graph.add_edges_from((int(source), int(target)) for source, target in edges)
    giant = set(int(node) for node in max(nx.biconnected_components(graph), key=len))
    core_numbers = nx.core_number(graph.subgraph(giant))
    core = {node for node, value in core_numbers.items() if value >= 3}
    shell = giant - core

    components: list[tuple[list[int], list[int]]] = []
    for raw_component in nx.connected_components(graph.subgraph(shell)):
        members = sorted(int(node) for node in raw_component)
        anchors = sorted({
            int(neighbor)
            for node in members
            for neighbor in graph.neighbors(node)
            if neighbor in core
        })
        components.append((members, anchors))
    components.sort(key=lambda item: (-len(item[0]), model_ids[item[0][0]]))

    bundle_id_by_component: list[str] = []
    node_rows: list[tuple[str, float, float]] = []
    position_rows: list[tuple[str, float, float]] = []
    for node in sorted(core):
        node_rows.append((model_ids[node], float(widths[node]), float(heights[node])))
        position_rows.append((model_ids[node], positions[node, 0], positions[node, 1]))
    for component_index, (members, _anchors) in enumerate(components):
        bundle_id = f"__node_bundle__{component_index}"
        bundle_id_by_component.append(bundle_id)
        side = max(
            args.centroid_unit,
            math.sqrt(len(members)) * args.centroid_unit,
        )
        center = positions[np.asarray(members, dtype=np.int32)].mean(axis=0)
        node_rows.append((bundle_id, side, side))
        position_rows.append((bundle_id, float(center[0]), float(center[1])))

    quotient_edges: set[tuple[str, str]] = set()
    for source_raw, target_raw in edges:
        source = int(source_raw)
        target = int(target_raw)
        if source in core and target in core:
            quotient_edges.add(tuple(sorted((model_ids[source], model_ids[target]))))
    for component_index, (_members, anchors) in enumerate(components):
        bundle_id = bundle_id_by_component[component_index]
        for anchor in anchors:
            quotient_edges.add(tuple(sorted((bundle_id, model_ids[anchor]))))

    args.out_dir.mkdir(parents=True, exist_ok=True)
    (args.out_dir / "nodes.tsv").write_text(
        "".join(f"{node_id}\t{width:.9f}\t{height:.9f}\n" for node_id, width, height in node_rows),
        encoding="utf-8",
    )
    (args.out_dir / "positions.tsv").write_text(
        "".join(f"{node_id}\t{x:.9f}\t{y:.9f}\n" for node_id, x, y in position_rows),
        encoding="utf-8",
    )
    (args.out_dir / "edges.tsv").write_text(
        "".join(
            f"bundle-edge:{index}\t{source}\t{target}\tbundle\tstructural\n"
            for index, (source, target) in enumerate(sorted(quotient_edges))
        ),
        encoding="utf-8",
    )
    core_ids = {model_ids[node] for node in core}
    (args.out_dir / "core-nodes.tsv").write_text(
        "".join(
            f"{node_id}\t{width:.9f}\t{height:.9f}\n"
            for node_id, width, height in node_rows
            if node_id in core_ids
        ),
        encoding="utf-8",
    )
    (args.out_dir / "core-positions.tsv").write_text(
        "".join(
            f"{node_id}\t{x:.9f}\t{y:.9f}\n"
            for node_id, x, y in position_rows
            if node_id in core_ids
        ),
        encoding="utf-8",
    )
    core_edges = [
        (source, target)
        for source, target in sorted(quotient_edges)
        if source in core_ids and target in core_ids
    ]
    (args.out_dir / "core-edges.tsv").write_text(
        "".join(
            f"core-edge:{index}\t{source}\t{target}\tcore\tstructural\n"
            for index, (source, target) in enumerate(core_edges)
        ),
        encoding="utf-8",
    )
    giant_ids = {model_ids[node] for node in giant}
    (args.out_dir / "giant-nodes.tsv").write_text(
        "".join(
            f"{model_ids[node]}\t{float(widths[node]):.9f}\t{float(heights[node]):.9f}\n"
            for node in sorted(giant)
        ),
        encoding="utf-8",
    )
    (args.out_dir / "giant-positions.tsv").write_text(
        "".join(
            f"{model_ids[node]}\t{positions[node, 0]:.9f}\t{positions[node, 1]:.9f}\n"
            for node in sorted(giant)
        ),
        encoding="utf-8",
    )
    giant_edges = sorted({
        tuple(sorted((model_ids[int(source)], model_ids[int(target)])))
        for source, target in edges
        if model_ids[int(source)] in giant_ids and model_ids[int(target)] in giant_ids
    })
    (args.out_dir / "giant-edges.tsv").write_text(
        "".join(
            f"giant-edge:{index}\t{source}\t{target}\tgiant\tstructural\n"
            for index, (source, target) in enumerate(giant_edges)
        ),
        encoding="utf-8",
    )
    for name, cost_for_pair in (
        (
            "giant-edges-core-weighted.tsv",
            lambda source, target: 64 if source in core_ids and target in core_ids else 1,
        ),
        (
            "giant-edges-shell-weighted.tsv",
            lambda source, target: 1 if source in core_ids and target in core_ids else 64,
        ),
    ):
        (args.out_dir / name).write_text(
            "".join(
                f"giant-edge:{index}\t{source}\t{target}\tgiant\tstructural"
                f"\t{cost_for_pair(source, target)}\n"
                for index, (source, target) in enumerate(giant_edges)
            ),
            encoding="utf-8",
        )
    (args.out_dir / "mapping.json").write_text(
        json.dumps(
            {
                "coreModelIds": [model_ids[node] for node in sorted(core)],
                "giantModelIds": [model_ids[node] for node in sorted(giant)],
                "bundles": [
                    {
                        "id": bundle_id_by_component[index],
                        "memberModelIds": [model_ids[node] for node in members],
                        "anchorModelIds": [model_ids[node] for node in anchors],
                    }
                    for index, (members, anchors) in enumerate(components)
                ],
            },
            ensure_ascii=False,
            separators=(",", ":"),
        ),
        encoding="utf-8",
    )
    print(
        f"core={len(core)} bundles={len(components)} "
        f"quotientNodes={len(node_rows)} quotientEdges={len(quotient_edges)} "
        f"largest={[len(members) for members, _anchors in components[:10]]} "
        f"out={args.out_dir}"
    )


if __name__ == "__main__":
    main()
