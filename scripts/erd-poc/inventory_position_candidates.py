#!/usr/bin/env python3
"""Re-score preserved TSV positions against the current direct ERD scene."""

from __future__ import annotations

import argparse
import importlib.util
import sys
from pathlib import Path

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
    parser.add_argument("--root", type=Path, action="append", required=True)
    parser.add_argument("--top", type=int, default=30)
    args = parser.parse_args()

    layout = v34.load_layout(args.layout)
    edges = v34.graph_edges(layout)
    evaluator = v34.fce.FastCrossEval(edges, len(layout["nodes"]))
    collision_geometry = v34.build_render_collision_geometry(
        layout,
        edges,
        count_bundle_nodes=True,
        direct_scene=True,
    )
    active = np.ones(len(layout["nodes"]), dtype=bool)
    candidates: list[tuple[int, int, int, int, Path]] = []
    skipped = 0
    paths: set[Path] = set()
    for root in args.root:
        if root.is_file():
            paths.add(root)
        elif root.is_dir():
            paths.update(root.rglob("*.tsv"))
    for path in sorted(paths):
        try:
            positions = v34.read_positions_tsv(path, layout)
            measured = v34.measure(
                positions,
                evaluator,
                collision_geometry.raw_node_widths,
                collision_geometry.raw_node_heights,
                active,
                collision_geometry.overlap_pairs,
                0.0,
                0.0,
                0.0,
                0.0,
                edge_node_weight=1.0,
                collision_geometry=collision_geometry,
            )
        except Exception:
            skipped += 1
            continue
        candidates.append(
            (
                measured.visual_cross,
                measured.cross,
                measured.edge_node,
                measured.overlaps,
                path,
            )
        )
    candidates.sort()
    print(f"scored={len(candidates)} skipped={skipped}")
    print("visual\tcross\tedgeNode\toverlaps\tpath")
    for visual, cross, edge_node, overlaps, path in candidates[: args.top]:
        print(f"{visual}\t{cross}\t{edge_node}\t{overlaps}\t{path}")


if __name__ == "__main__":
    main()
