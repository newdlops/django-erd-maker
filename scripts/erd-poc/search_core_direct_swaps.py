#!/usr/bin/env python3
"""Anneal permutations of real 3-core node positions for direct edges only."""

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
relocate = load_module(
    "search_core_direct_relocation",
    ROOT / "scripts/erd-poc/search_core_direct_relocation.py",
)


class SwapEvaluator:
    def __init__(self, edges: np.ndarray, node_count: int):
        self.edges = np.asarray(edges, dtype=np.int32)
        self.base = v34.fce.FastCrossEval(self.edges, node_count)
        pair_left = self.base.i
        pair_right = self.base.j
        left_edges = self.edges[pair_left]
        right_edges = self.edges[pair_right]
        self.pair_indices_by_node: list[np.ndarray] = []
        for node in range(node_count):
            affected = (
                (left_edges[:, 0] == node)
                | (left_edges[:, 1] == node)
                | (right_edges[:, 0] == node)
                | (right_edges[:, 1] == node)
            )
            self.pair_indices_by_node.append(
                np.flatnonzero(affected).astype(np.int32)
            )

    def pair_count(self, pair_indices: np.ndarray, positions: np.ndarray) -> int:
        if pair_indices.size == 0:
            return 0
        left = self.edges[self.base.i[pair_indices]]
        right = self.edges[self.base.j[pair_indices]]
        return int(relocate.proper_crossings(
            positions[left[:, 0]],
            positions[left[:, 1]],
            positions[right[:, 0]],
            positions[right[:, 1]],
        ).sum())

    def swap_delta(
        self,
        left_node: int,
        right_node: int,
        positions: np.ndarray,
    ) -> int:
        pair_indices = np.union1d(
            self.pair_indices_by_node[left_node],
            self.pair_indices_by_node[right_node],
        )
        before = self.pair_count(pair_indices, positions)
        positions[[left_node, right_node]] = positions[[right_node, left_node]]
        after = self.pair_count(pair_indices, positions)
        positions[[left_node, right_node]] = positions[[right_node, left_node]]
        return after - before


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--positions", type=Path, required=True)
    parser.add_argument("--out-tsv", type=Path, required=True)
    parser.add_argument("--steps", type=int, default=250_000)
    parser.add_argument("--start-temperature", type=float, default=6.0)
    parser.add_argument("--end-temperature", type=float, default=0.03)
    parser.add_argument("--hot-probability", type=float, default=0.7)
    parser.add_argument("--report-every", type=int, default=10_000)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    layout = v34.load_layout(args.layout)
    global_edges = v34.graph_edges(layout)
    graph = nx.Graph()
    graph.add_nodes_from(range(len(layout["nodes"])))
    graph.add_edges_from((int(source), int(target)) for source, target in global_edges)
    giant = set(int(node) for node in max(nx.biconnected_components(graph), key=len))
    core_numbers = nx.core_number(graph.subgraph(giant))
    core_nodes = sorted(node for node, value in core_numbers.items() if value >= 3)
    global_to_local = {node: index for index, node in enumerate(core_nodes)}
    edges = np.asarray([
        (global_to_local[int(source)], global_to_local[int(target)])
        for source, target in global_edges
        if int(source) in global_to_local and int(target) in global_to_local
    ], dtype=np.int32)
    positions = v34.read_positions_tsv(args.positions, layout)[core_nodes].copy()
    evaluator = SwapEvaluator(edges, len(core_nodes))
    current = evaluator.base.count_crossings(positions)
    best = current
    best_positions = positions.copy()
    rng = np.random.default_rng(args.seed)
    started = time.time()
    endpoint_scores = np.zeros(len(core_nodes), dtype=np.float64)

    def refresh_scores() -> None:
        endpoint_scores.fill(1.0)
        for node, pair_indices in enumerate(evaluator.pair_indices_by_node):
            endpoint_scores[node] += evaluator.pair_count(pair_indices, positions)
        endpoint_scores[:] = np.power(endpoint_scores, 1.35)
        endpoint_scores[:] /= endpoint_scores.sum()

    refresh_scores()
    print(
        f"start nodes={len(core_nodes)} edges={len(edges)} cross={current} "
        f"steps={args.steps}",
        flush=True,
    )
    accepted = 0
    uphill = 0
    for step in range(1, args.steps + 1):
        progress = (step - 1) / max(1, args.steps - 1)
        temperature = args.start_temperature * math.pow(
            args.end_temperature / args.start_temperature,
            progress,
        )
        if rng.random() < args.hot_probability:
            left = int(rng.choice(len(core_nodes), p=endpoint_scores))
        else:
            left = int(rng.integers(0, len(core_nodes)))
        right = int(rng.integers(0, len(core_nodes) - 1))
        if right >= left:
            right += 1
        delta = evaluator.swap_delta(left, right, positions)
        accept = delta <= 0 or rng.random() < math.exp(-delta / max(temperature, 1e-9))
        if accept:
            positions[[left, right]] = positions[[right, left]]
            current += delta
            accepted += 1
            if delta > 0:
                uphill += 1
            if current < best:
                best = current
                best_positions = positions.copy()
                relocate.write_scope_positions(
                    args.out_tsv,
                    layout,
                    core_nodes,
                    best_positions,
                )
        if step % args.report_every == 0:
            verified = evaluator.base.count_crossings(positions)
            if verified != current:
                raise RuntimeError(
                    f"crossing drift at step {step}: tracked={current}, verified={verified}"
                )
            refresh_scores()
            print(
                f"step={step} current={current} best={best} temp={temperature:.3f} "
                f"accepted={accepted} uphill={uphill} elapsed={time.time() - started:.1f}s",
                flush=True,
            )
    relocate.write_scope_positions(args.out_tsv, layout, core_nodes, best_positions)
    print(f"done best={best} out={args.out_tsv} elapsed={time.time() - started:.1f}s")


if __name__ == "__main__":
    main()
