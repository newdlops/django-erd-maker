#!/usr/bin/env python3
"""Anneal real-node coordinates against exact direct-edge crossings."""

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
direct = load_module(
    "search_core_direct_relocation",
    ROOT / "scripts/erd-poc/search_core_direct_relocation.py",
)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--positions", type=Path, required=True)
    parser.add_argument("--out-tsv", type=Path, required=True)
    parser.add_argument("--steps", type=int, default=120_000)
    parser.add_argument("--candidates", type=int, default=12)
    parser.add_argument("--cycles", type=int, default=6)
    parser.add_argument("--start-temperature", type=float, default=8.0)
    parser.add_argument("--end-temperature", type=float, default=0.08)
    parser.add_argument("--min-distance-ratio", type=float, default=0.008)
    parser.add_argument("--report-every", type=int, default=5000)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument(
        "--scope",
        choices=("core3", "core3-single", "giant"),
        default="core3",
    )
    args = parser.parse_args()

    layout = v34.load_layout(args.layout)
    global_edges = v34.graph_edges(layout)
    graph = nx.Graph()
    graph.add_nodes_from(range(len(layout["nodes"])))
    graph.add_edges_from((int(source), int(target)) for source, target in global_edges)
    giant = set(int(node) for node in max(nx.biconnected_components(graph), key=len))
    core_numbers = nx.core_number(graph.subgraph(giant))
    core3 = {node for node, value in core_numbers.items() if value >= 3}
    if args.scope == "core3":
        scope_nodes = sorted(core3)
    elif args.scope == "core3-single":
        shell = giant - core3
        single_shell_nodes = {
            next(iter(component))
            for component in nx.connected_components(graph.subgraph(shell))
            if len(component) == 1
        }
        scope_nodes = sorted(core3 | single_shell_nodes)
    else:
        scope_nodes = sorted(giant)
    global_to_local = {node: index for index, node in enumerate(scope_nodes)}
    edges = np.asarray([
        (global_to_local[int(source)], global_to_local[int(target)])
        for source, target in global_edges
        if int(source) in global_to_local and int(target) in global_to_local
    ], dtype=np.int32)
    local_graph = nx.Graph()
    local_graph.add_nodes_from(range(len(scope_nodes)))
    local_graph.add_edges_from((int(source), int(target)) for source, target in edges)
    positions = v34.read_positions_tsv(args.positions, layout)[scope_nodes].copy()
    evaluator = direct.DirectNodeMoveEvaluator(edges, len(scope_nodes))
    current = evaluator.global_evaluator.count_crossings(positions)
    best = current
    best_positions = positions.copy()
    diagonal = float(np.linalg.norm(positions.max(axis=0) - positions.min(axis=0)))
    min_distance = diagonal * args.min_distance_ratio
    rng = np.random.default_rng(args.seed)
    started = time.time()
    weights = np.ones(len(scope_nodes), dtype=np.float64)

    def refresh_weights() -> None:
        for node in range(len(scope_nodes)):
            weights[node] = 1.0 + evaluator.node_pair_crossings(node, positions)
        weights[:] = np.power(weights, 1.15)
        weights[:] /= weights.sum()

    refresh_weights()
    print(
        f"start scope={args.scope} nodes={len(scope_nodes)} edges={len(edges)} cross={current} "
        f"steps={args.steps} cycles={args.cycles}",
        flush=True,
    )
    accepted = 0
    uphill = 0
    cycle_length = max(1, args.steps // max(1, args.cycles))
    for step in range(1, args.steps + 1):
        cycle_progress = ((step - 1) % cycle_length) / max(1, cycle_length - 1)
        temperature = args.start_temperature * math.pow(
            args.end_temperature / args.start_temperature,
            cycle_progress,
        )
        node = int(rng.choice(len(scope_nodes), p=weights))
        low = positions.min(axis=0)
        high = positions.max(axis=0)
        span = np.maximum(high - low, 1e-6)
        current_point = positions[node].copy()
        neighbors = np.asarray(sorted(local_graph.neighbors(node)), dtype=np.int32)
        neighbor_center = (
            positions[neighbors].mean(axis=0) if neighbors.size else current_point
        )
        count = max(4, args.candidates)
        candidates = np.empty((count + 1, 2), dtype=np.float64)
        candidates[0] = current_point
        # Mix local, graph-barycentric, global, and near-node cells.  The
        # temperature changes the local radius but never alters relationships.
        local_scale = 0.015 + 0.35 * min(1.0, temperature / max(args.start_temperature, 1e-9))
        for candidate_index in range(1, count + 1):
            mode = candidate_index % 4
            if mode == 0:
                candidates[candidate_index] = rng.uniform(low - span * 0.08, high + span * 0.08)
            elif mode == 1:
                candidates[candidate_index] = current_point + rng.normal(0.0, span * local_scale)
            elif mode == 2:
                candidates[candidate_index] = neighbor_center + rng.normal(0.0, span * local_scale)
            else:
                other = int(rng.integers(0, len(scope_nodes)))
                angle = rng.uniform(0.0, 2.0 * np.pi)
                candidates[candidate_index] = positions[other] + np.array([
                    math.cos(angle), math.sin(angle)
                ]) * max(min_distance * 1.3, diagonal * 0.01)
        others = np.delete(positions, node, axis=0)
        distances = np.linalg.norm(
            candidates[:, None, :] - others[None, :, :],
            axis=2,
        ).min(axis=1)
        valid = distances >= min_distance
        valid[0] = True
        candidates = candidates[valid]
        local_counts = evaluator.candidate_counts(
            node,
            candidates,
            positions,
            chunk_size=max(1, len(candidates)),
        )
        current_local = int(local_counts[0])
        deltas = local_counts.astype(np.float64) - current_local
        logits = -deltas / max(temperature, 1e-9)
        logits -= logits.max()
        probabilities = np.exp(np.clip(logits, -60.0, 0.0))
        probabilities /= probabilities.sum()
        chosen = int(rng.choice(len(candidates), p=probabilities))
        delta = int(local_counts[chosen]) - current_local
        if chosen != 0:
            positions[node] = candidates[chosen]
            current += delta
            accepted += 1
            if delta > 0:
                uphill += 1
            if current < best:
                best = current
                best_positions = positions.copy()
                direct.write_scope_positions(
                    args.out_tsv,
                    layout,
                    scope_nodes,
                    best_positions,
                )
        if step % args.report_every == 0:
            verified = evaluator.global_evaluator.count_crossings(positions)
            if verified != current:
                raise RuntimeError(
                    f"crossing drift at step {step}: tracked={current}, verified={verified}"
                )
            refresh_weights()
            print(
                f"step={step} current={current} best={best} temp={temperature:.3f} "
                f"accepted={accepted} uphill={uphill} elapsed={time.time() - started:.1f}s",
                flush=True,
            )
        if step % cycle_length == 0 and step < args.steps:
            # Each reheating starts from the best known valid node layout.
            positions = best_positions.copy()
            current = best
            refresh_weights()
    direct.write_scope_positions(args.out_tsv, layout, scope_nodes, best_positions)
    print(f"done best={best} out={args.out_tsv} elapsed={time.time() - started:.1f}s")


if __name__ == "__main__":
    main()
