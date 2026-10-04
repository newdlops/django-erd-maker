#!/usr/bin/env python3
"""Lay out structural shell components as groups of real model nodes.

The quotient exists only inside this coordinate search.  No quotient vertex is
written to the output and no relationship is contracted in the scored scene:
the final TSV contains every original model and the evaluator receives every
original direct source-target edge.
"""

from __future__ import annotations

import argparse
import importlib.util
import math
import sys
import time
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


def normalized(raw: dict[int, np.ndarray], node_count: int) -> np.ndarray:
    points = np.asarray([raw[node] for node in range(node_count)], dtype=np.float64)
    points -= points.mean(axis=0)
    scale = float(np.std(points))
    if scale > 1e-12:
        points /= scale
    return points


def quotient_positions(
    quotient: nx.Graph,
    engine: str,
    seed: int,
    iterations: int,
    fixed_core: np.ndarray | None = None,
) -> np.ndarray:
    if engine == "fixed-spring":
        if fixed_core is None:
            raise ValueError("fixed-spring requires core positions")
        rng = np.random.default_rng(seed)
        initial: dict[int, np.ndarray] = {
            node: fixed_core[node].copy()
            for node in range(fixed_core.shape[0])
        }
        for node in range(fixed_core.shape[0], quotient.number_of_nodes()):
            neighbors = [neighbor for neighbor in quotient.neighbors(node) if neighbor < fixed_core.shape[0]]
            center = (
                fixed_core[neighbors].mean(axis=0)
                if neighbors
                else fixed_core.mean(axis=0)
            )
            initial[node] = center + rng.normal(0.0, 0.35, size=2)
        raw = nx.spring_layout(
            quotient,
            pos=initial,
            fixed=list(range(fixed_core.shape[0])),
            seed=seed,
            iterations=iterations,
            threshold=1e-6,
            k=0.22,
            scale=None,
            weight="weight",
        )
        return np.asarray([raw[node] for node in range(quotient.number_of_nodes())])
    if engine == "spring":
        raw = nx.spring_layout(
            quotient,
            seed=seed,
            iterations=iterations,
            threshold=1e-6,
            weight="weight",
        )
    elif engine == "forceatlas2":
        raw = nx.forceatlas2_layout(
            quotient,
            seed=seed,
            max_iter=iterations,
            scaling_ratio=2.0,
            gravity=1.0,
            distributed_action=True,
            weight="weight",
        )
    elif engine == "arf":
        raw = nx.arf_layout(quotient, seed=seed, max_iter=iterations, etol=1e-6)
    elif engine == "spectral":
        raw = nx.spectral_layout(quotient, weight="weight")
    else:
        raw = nx.circular_layout(quotient)
    return normalized(raw, quotient.number_of_nodes())


def place_bundle_members(
    graph: nx.Graph,
    component: set[int],
    anchors: set[int],
    center: np.ndarray,
    core_positions: np.ndarray,
    original_positions: np.ndarray,
    base_radius: float,
    ring_spacing: float,
    slots_per_ring: int,
) -> dict[int, np.ndarray]:
    preferred: list[tuple[float, int, bool]] = []
    for node in component:
        node_anchors = [neighbor for neighbor in graph.neighbors(node) if neighbor in anchors]
        if node_anchors:
            vectors = core_positions[node_anchors] - center
            lengths = np.linalg.norm(vectors, axis=1)
            lengths[lengths <= 1e-9] = 1.0
            vector = (vectors / lengths[:, None]).sum(axis=0)
            boundary = True
        else:
            vector = original_positions[node] - original_positions[list(component)].mean(axis=0)
            boundary = False
        angle = math.atan2(float(vector[1]), float(vector[0]))
        preferred.append((angle, node, boundary))
    # Boundary models occupy the outer-facing slots first. Internal models use
    # inner rings but remain part of the same real-node group.
    preferred.sort(key=lambda item: (not item[2], item[0], item[1]))
    result: dict[int, np.ndarray] = {}
    boundary_count = sum(1 for _angle, _node, boundary in preferred if boundary)
    for rank, (angle, node, boundary) in enumerate(preferred):
        if boundary:
            layer = rank // max(1, slots_per_ring)
            radius = base_radius + layer * ring_spacing
        else:
            internal_rank = rank - boundary_count
            layer = internal_rank // max(1, slots_per_ring)
            radius = max(base_radius * 0.28, base_radius - (layer + 1) * ring_spacing * 0.55)
        # A deterministic sub-slot offset separates nodes aimed at the same
        # anchor without changing which macro direction they face.
        subslot = ((node * 2654435761) % 17 - 8) / 8.0
        adjusted_angle = angle + subslot * min(0.12, ring_spacing / max(radius, 1.0) * 0.25)
        result[node] = center + np.array([
            math.cos(adjusted_angle) * radius,
            math.sin(adjusted_angle) * radius,
        ])
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--out-tsv", type=Path, required=True)
    parser.add_argument("--seeds", type=int, default=24)
    parser.add_argument("--iterations", type=int, default=1200)
    parser.add_argument("--macro-scales", default="6000,10000,16000,24000")
    parser.add_argument("--bundle-ratios", default="0.04,0.07,0.11,0.16,0.24")
    parser.add_argument("--engines", default="spring,forceatlas2,arf,spectral")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--core-positions", type=Path, default=None)
    args = parser.parse_args()

    layout = v34.load_layout(args.layout)
    edges = v34.graph_edges(layout)
    graph = nx.Graph()
    graph.add_nodes_from(range(len(layout["nodes"])))
    graph.add_edges_from((int(source), int(target)) for source, target in edges)
    giant = set(int(node) for node in max(nx.biconnected_components(graph), key=len))
    giant_graph = graph.subgraph(giant).copy()
    core_numbers = nx.core_number(giant_graph)
    core3 = sorted(node for node, value in core_numbers.items() if value >= 3)
    core_set = set(core3)
    shell = giant - core_set
    components: list[tuple[set[int], set[int]]] = []
    for component_raw in nx.connected_components(giant_graph.subgraph(shell)):
        component = set(int(node) for node in component_raw)
        anchors = {
            int(neighbor)
            for node in component
            for neighbor in giant_graph.neighbors(node)
            if neighbor in core_set
        }
        components.append((component, anchors))
    components.sort(key=lambda record: (-len(record[0]), min(record[0])))

    core_local = {node: index for index, node in enumerate(core3)}
    bundle_offset = len(core3)
    quotient = nx.Graph()
    quotient.add_nodes_from(range(len(core3) + len(components)))
    for source, target in giant_graph.subgraph(core_set).edges():
        quotient.add_edge(core_local[source], core_local[target], weight=1.0)
    for bundle_index, (component, anchors) in enumerate(components):
        quotient_node = bundle_offset + bundle_index
        for anchor in anchors:
            boundary_count = sum(
                1 for node in component if giant_graph.has_edge(node, anchor)
            )
            quotient.add_edge(
                quotient_node,
                core_local[anchor],
                weight=float(max(1, boundary_count)),
            )

    original = v34.layout_positions(layout)
    fixed_core: np.ndarray | None = None
    if args.core_positions is not None:
        supplied = v34.read_positions_tsv(args.core_positions, layout)[core3].copy()
        supplied -= supplied.mean(axis=0)
        supplied_scale = float(np.std(supplied))
        if supplied_scale > 1e-12:
            supplied /= supplied_scale
        fixed_core = supplied
    giant_edges = np.asarray([
        (int(source), int(target))
        for source, target in edges
        if int(source) in giant and int(target) in giant
    ], dtype=np.int32)
    evaluator = v34.fce.FastCrossEval(giant_edges, len(layout["nodes"]))
    macro_scales = [float(value) for value in args.macro_scales.split(",")]
    bundle_ratios = [float(value) for value in args.bundle_ratios.split(",")]
    engines = [value.strip() for value in args.engines.split(",") if value.strip()]
    best_cross = 1 << 60
    best_positions: np.ndarray | None = None
    best_name = ""
    started = time.time()
    for engine in engines:
        engine_seeds = 1 if engine == "spectral" else args.seeds
        for seed_offset in range(engine_seeds):
            macro = quotient_positions(
                quotient,
                engine,
                args.seed + seed_offset,
                args.iterations,
                fixed_core,
            )
            for macro_scale in macro_scales:
                core_positions = original.copy()
                core_positions[core3] = macro[:len(core3)] * macro_scale
                for bundle_ratio in bundle_ratios:
                    positions = core_positions.copy()
                    base_radius = macro_scale * bundle_ratio
                    ring_spacing = base_radius * 0.42
                    slots_per_ring = max(8, int(2.0 * math.pi / max(0.12, bundle_ratio)))
                    for bundle_index, (component, anchors) in enumerate(components):
                        center = macro[bundle_offset + bundle_index] * macro_scale
                        mapped = place_bundle_members(
                            giant_graph,
                            component,
                            anchors,
                            center,
                            core_positions,
                            original,
                            base_radius * max(0.7, math.sqrt(len(component)) / 3.0),
                            ring_spacing,
                            slots_per_ring,
                        )
                        for node, point in mapped.items():
                            positions[node] = point
                    crossings = evaluator.count_crossings(positions)
                    if crossings < best_cross:
                        best_cross = crossings
                        best_positions = positions.copy()
                        best_name = (
                            f"{engine}-s{args.seed + seed_offset}-macro{macro_scale:g}"
                            f"-ratio{bundle_ratio:g}"
                        )
                        print(
                            f"best name={best_name} giantCross={best_cross} "
                            f"elapsed={time.time() - started:.1f}s",
                            flush=True,
                        )
    assert best_positions is not None
    # Nodes outside the giant block keep their original positions for this
    # structural experiment; later block packing can move them as real-node
    # groups around their articulation points.
    args.out_tsv.parent.mkdir(parents=True, exist_ok=True)
    v34.write_positions_tsv(args.out_tsv, layout, best_positions)
    print(
        f"done quotientNodes={quotient.number_of_nodes()} quotientEdges={quotient.number_of_edges()} "
        f"best={best_name} giantCross={best_cross} out={args.out_tsv}",
        flush=True,
    )


if __name__ == "__main__":
    main()
