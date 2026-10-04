#!/usr/bin/env python3
"""Anneal rigid groups of real nodes; relationships remain direct edges."""

from __future__ import annotations

import argparse
import importlib.util
import math
import resource
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
direct = load_module(
    "search_core_direct_relocation",
    ROOT / "scripts/erd-poc/search_core_direct_relocation.py",
)


def transform_group(
    positions: np.ndarray,
    members: np.ndarray,
    target_center: np.ndarray,
    angle: float,
    scale: float,
    reflect: bool,
) -> np.ndarray:
    points = positions[members]
    center = points.mean(axis=0)
    local = points - center
    if reflect:
        local[:, 0] *= -1.0
    cosine = math.cos(angle)
    sine = math.sin(angle)
    rotation = np.array([[cosine, -sine], [sine, cosine]])
    return local @ rotation.T * scale + target_center


def has_external_spacing(
    positions: np.ndarray,
    members: np.ndarray,
    candidate: np.ndarray,
    min_distance: float,
) -> bool:
    if min_distance <= 0.0:
        return True
    outside_mask = np.ones(positions.shape[0], dtype=bool)
    outside_mask[members] = False
    outside = positions[outside_mask]
    if outside.size:
        nearest = np.linalg.norm(
            candidate[:, None, :] - outside[None, :, :],
            axis=2,
        ).min()
        if nearest < min_distance:
            return False
    if candidate.shape[0] > 1:
        delta = candidate[:, None, :] - candidate[None, :, :]
        distances = np.linalg.norm(delta, axis=2)
        distances[distances <= 1e-12] = np.inf
        if float(distances.min()) < min_distance:
            return False
    return True


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--positions", type=Path, required=True)
    parser.add_argument("--out-tsv", type=Path, required=True)
    parser.add_argument(
        "--scope",
        choices=("core3", "core3-single", "giant"),
        default="core3",
    )
    parser.add_argument("--resolution", type=float, default=0.7)
    parser.add_argument("--steps", type=int, default=80_000)
    parser.add_argument("--cycles", type=int, default=4)
    parser.add_argument("--start-temperature", type=float, default=30.0)
    parser.add_argument("--end-temperature", type=float, default=0.05)
    parser.add_argument("--min-distance-ratio", type=float, default=0.006)
    parser.add_argument("--report-every", type=int, default=2000)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    layout = v34.load_layout(args.layout)
    global_edges = v34.graph_edges(layout)
    graph = nx.Graph()
    graph.add_nodes_from(range(len(layout["nodes"])))
    graph.add_edges_from((int(source), int(target)) for source, target in global_edges)
    giant = set(int(node) for node in max(nx.biconnected_components(graph), key=len))
    core_numbers = nx.core_number(graph.subgraph(giant))
    core3 = {node for node, value in core_numbers.items() if value >= 3}
    if args.scope == "giant":
        selected = giant
    elif args.scope == "core3-single":
        shell = giant - core3
        selected = core3 | {
            next(iter(component))
            for component in nx.connected_components(graph.subgraph(shell))
            if len(component) == 1
        }
    else:
        selected = core3
    core_nodes = sorted(selected)
    global_to_local = {node: index for index, node in enumerate(core_nodes)}
    edges = np.asarray([
        (global_to_local[int(source)], global_to_local[int(target)])
        for source, target in global_edges
        if int(source) in global_to_local and int(target) in global_to_local
    ], dtype=np.int32)
    local_graph = nx.Graph()
    local_graph.add_nodes_from(range(len(core_nodes)))
    local_graph.add_edges_from((int(source), int(target)) for source, target in edges)
    groups = [
        np.asarray(sorted(members), dtype=np.int32)
        for members in nx.community.louvain_communities(
            local_graph,
            weight=None,
            resolution=args.resolution,
            seed=args.seed,
        )
    ]
    groups.sort(key=lambda members: (-members.size, int(members.min())))
    positions = v34.read_positions_tsv(args.positions, layout)[core_nodes].copy()
    evaluator = v34.fce.FastCrossEval(edges, len(core_nodes))
    current = evaluator.count_crossings(positions)
    best = current
    best_positions = positions.copy()
    diagonal = float(np.linalg.norm(positions.max(axis=0) - positions.min(axis=0)))
    min_distance = diagonal * args.min_distance_ratio
    rng = np.random.default_rng(args.seed + 991)
    started = time.time()
    cycle_length = max(1, args.steps // max(1, args.cycles))
    accepted = 0
    uphill = 0
    print(
        f"start scope={args.scope} groups={len(groups)} "
        f"sizes={list(map(len, groups))} cross={current} "
        f"resolution={args.resolution:g}",
        flush=True,
    )
    for step in range(1, args.steps + 1):
        progress = ((step - 1) % cycle_length) / max(1, cycle_length - 1)
        temperature = args.start_temperature * math.pow(
            args.end_temperature / args.start_temperature,
            progress,
        )
        group_index = int(rng.integers(0, len(groups)))
        members = groups[group_index]
        center = positions[members].mean(axis=0)
        low = positions.min(axis=0)
        high = positions.max(axis=0)
        span = np.maximum(high - low, 1e-6)
        mode = int(rng.integers(0, 5))
        target_center = center.copy()
        angle = 0.0
        scale = 1.0
        reflect = False
        if mode == 0:
            angle = rng.uniform(-math.pi, math.pi)
            reflect = bool(rng.integers(0, 2))
        elif mode == 1:
            target_center = rng.uniform(low - span * 0.15, high + span * 0.15)
            angle = rng.uniform(-math.pi, math.pi)
            reflect = bool(rng.integers(0, 2))
        elif mode == 2:
            local_scale = 0.03 + 0.35 * min(
                1.0,
                temperature / max(args.start_temperature, 1e-9),
            )
            target_center = center + rng.normal(0.0, span * local_scale)
            angle = rng.normal(0.0, math.pi * local_scale * 2.0)
        elif mode == 3:
            other_index = int(rng.integers(0, len(groups) - 1))
            if other_index >= group_index:
                other_index += 1
            target_center = positions[groups[other_index]].mean(axis=0)
            angle = rng.uniform(-math.pi, math.pi)
            reflect = bool(rng.integers(0, 2))
        else:
            scale = float(rng.choice([0.55, 0.7, 0.85, 1.15, 1.4, 1.8]))
            angle = rng.uniform(-math.pi / 3.0, math.pi / 3.0)
        candidate = transform_group(
            positions,
            members,
            target_center,
            angle,
            scale,
            reflect,
        )
        if not has_external_spacing(positions, members, candidate, min_distance):
            continue
        previous = positions[members].copy()
        positions[members] = candidate
        proposed = evaluator.count_crossings(positions)
        delta = proposed - current
        accept = delta <= 0 or rng.random() < math.exp(-delta / max(temperature, 1e-9))
        if accept:
            current = proposed
            accepted += 1
            if delta > 0:
                uphill += 1
            if current < best:
                best = current
                best_positions = positions.copy()
                direct.write_scope_positions(
                    args.out_tsv,
                    layout,
                    core_nodes,
                    best_positions,
                )
        else:
            positions[members] = previous
        if step % args.report_every == 0:
            print(
                f"step={step} current={current} best={best} temp={temperature:.3f} "
                f"accepted={accepted} uphill={uphill} elapsed={time.time() - started:.1f}s",
                flush=True,
            )
        if step % cycle_length == 0 and step < args.steps:
            positions = best_positions.copy()
            current = best
    direct.write_scope_positions(args.out_tsv, layout, core_nodes, best_positions)
    peak_raw = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    peak_mib = peak_raw / (1024.0 * 1024.0) if sys.platform == "darwin" else peak_raw / 1024.0
    print(
        f"done best={best} out={args.out_tsv} "
        f"elapsed={time.time() - started:.1f}s peakMiB={peak_mib:.1f}"
    )


if __name__ == "__main__":
    main()
