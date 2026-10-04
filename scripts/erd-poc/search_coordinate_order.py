#!/usr/bin/env python3
"""Anneal x/y orderings of real nodes with exact direct-edge deltas.

Continuous node relocation easily stalls because crossing count is constant
inside each arrangement cell.  Swapping just the x- or y-rank jumps directly
between cells.  Occasional community reversals change a whole node bundle's
port order atomically.  Relationships remain untouched direct segments.
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
torch_search = load_module(
    "optimize_direct_crossings_torch",
    ROOT / "scripts/erd-poc/optimize_direct_crossings_torch.py",
)
direct = load_module(
    "search_core_direct_relocation",
    ROOT / "scripts/erd-poc/search_core_direct_relocation.py",
)


def flags_for_indices(
    positions: np.ndarray,
    evaluator,
    pair_indices: np.ndarray,
) -> np.ndarray:
    left_edges = evaluator.edges[evaluator.i[pair_indices]]
    right_edges = evaluator.edges[evaluator.j[pair_indices]]
    return direct.proper_crossings(
        positions[left_edges[:, 0]],
        positions[left_edges[:, 1]],
        positions[right_edges[:, 0]],
        positions[right_edges[:, 1]],
    )


def impacted_pairs_by_node(evaluator, node_count: int) -> list[np.ndarray]:
    left = evaluator.edges[evaluator.i]
    right = evaluator.edges[evaluator.j]
    pair_nodes = np.column_stack((left, right))
    return [
        np.flatnonzero(np.any(pair_nodes == node, axis=1)).astype(np.int32)
        for node in range(node_count)
    ]


def union_impacted(
    by_node: list[np.ndarray],
    moved: np.ndarray,
) -> np.ndarray:
    if moved.size == 1:
        return by_node[int(moved[0])]
    return np.unique(np.concatenate([by_node[int(node)] for node in moved])).astype(
        np.int32, copy=False
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--positions", type=Path, required=True)
    parser.add_argument("--out-tsv", type=Path, required=True)
    parser.add_argument(
        "--scope", choices=("core3", "core3-single", "giant", "all"), default="core3"
    )
    parser.add_argument("--steps", type=int, default=500_000)
    parser.add_argument("--cycles", type=int, default=10)
    parser.add_argument("--start-temperature", type=float, default=12.0)
    parser.add_argument("--end-temperature", type=float, default=0.03)
    parser.add_argument("--bundle-move-rate", type=float, default=0.04)
    parser.add_argument("--resolution", type=float, default=0.7)
    parser.add_argument("--report-every", type=int, default=10_000)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    layout = v34.load_layout(args.layout)
    scope_nodes, edges64, graph = torch_search.scope_graph(layout, args.scope)
    edges = edges64.astype(np.int32)
    positions = v34.read_positions_tsv(args.positions, layout)[scope_nodes].copy()
    positions -= positions.mean(axis=0)
    scale = float(np.sqrt(np.mean(np.sum(positions * positions, axis=1))))
    if scale > 1e-12:
        positions /= scale
    evaluator = v34.fce.FastCrossEval(edges, len(scope_nodes))
    flags = v34.crossing_flags_for_pairs(positions, evaluator)
    current = int(flags.sum())
    best = current
    best_positions = positions.copy()
    by_node = impacted_pairs_by_node(evaluator, len(scope_nodes))
    groups = [
        np.asarray(sorted(group), dtype=np.int32)
        for group in nx.community.louvain_communities(
            graph, weight=None, resolution=args.resolution, seed=args.seed
        )
        if len(group) >= 2
    ]
    groups.sort(key=lambda group: (-len(group), int(group.min())))
    rng = np.random.default_rng(args.seed)
    cycle_length = max(1, args.steps // max(1, args.cycles))
    accepted = 0
    uphill = 0
    started = time.time()

    def node_weights() -> np.ndarray:
        incident = np.zeros(len(scope_nodes), dtype=np.float64)
        crossing_pairs = np.flatnonzero(flags)
        if crossing_pairs.size:
            crossing_edges = np.concatenate(
                (evaluator.i[crossing_pairs], evaluator.j[crossing_pairs])
            )
            crossing_nodes = edges[crossing_edges].ravel()
            np.add.at(incident, crossing_nodes, 1.0)
        incident += 1.0
        incident = np.power(incident, 0.72)
        return incident / incident.sum()

    weights = node_weights()
    print(
        f"start scope={args.scope} nodes={len(scope_nodes)} edges={len(edges)} "
        f"pairs={evaluator.K} groups={[len(group) for group in groups]} "
        f"cross={current} steps={args.steps}",
        flush=True,
    )

    for step in range(1, args.steps + 1):
        cycle_progress = ((step - 1) % cycle_length) / max(1, cycle_length - 1)
        temperature = args.start_temperature * math.pow(
            args.end_temperature / args.start_temperature,
            cycle_progress,
        )
        do_bundle = bool(groups) and rng.random() < args.bundle_move_rate
        if do_bundle:
            members = groups[int(rng.integers(len(groups)))]
            if members.size > 36:
                members = rng.choice(members, size=36, replace=False)
            moved = np.asarray(members, dtype=np.int32)
            previous = positions[moved].copy()
            axis = int(rng.integers(2))
            mode = int(rng.integers(4))
            order = np.argsort(positions[moved, axis])
            values = positions[moved[order], axis].copy()
            if mode == 0:
                values = values[::-1]
            elif mode == 1:
                values = np.roll(values, int(rng.integers(1, len(values))))
            elif mode == 2:
                rng.shuffle(values)
            else:
                center = positions[moved].mean(axis=0)
                local = positions[moved] - center
                positions[moved] = center + np.column_stack((-local[:, 1], local[:, 0]))
                values = positions[moved[order], axis]
            positions[moved[order], axis] = values
        else:
            first = int(rng.choice(len(scope_nodes), p=weights))
            second = int(rng.integers(len(scope_nodes) - 1))
            if second >= first:
                second += 1
            moved = np.asarray([first, second], dtype=np.int32)
            previous = positions[moved].copy()
            mode = int(rng.integers(3))
            if mode == 0:
                positions[first, 0], positions[second, 0] = (
                    positions[second, 0],
                    positions[first, 0],
                )
            elif mode == 1:
                positions[first, 1], positions[second, 1] = (
                    positions[second, 1],
                    positions[first, 1],
                )
            else:
                positions[[first, second]] = positions[[second, first]]

        impacted = union_impacted(by_node, moved)
        old_local = int(flags[impacted].sum())
        new_local_flags = flags_for_indices(positions, evaluator, impacted)
        new_local = int(new_local_flags.sum())
        delta = new_local - old_local
        accept = delta <= 0 or rng.random() < math.exp(
            -delta / max(temperature, 1e-12)
        )
        if accept:
            flags[impacted] = new_local_flags
            current += delta
            accepted += 1
            if delta > 0:
                uphill += 1
            if current < best:
                best = current
                best_positions = positions.copy()
                direct.write_scope_positions(
                    args.out_tsv, layout, scope_nodes, best_positions
                )
                print(
                    f"best step={step} cross={best} temp={temperature:.3f} "
                    f"elapsed={time.time() - started:.1f}s",
                    flush=True,
                )
        else:
            positions[moved] = previous

        if step % args.report_every == 0:
            verified_flags = v34.crossing_flags_for_pairs(positions, evaluator)
            verified = int(verified_flags.sum())
            if verified != current or not np.array_equal(verified_flags, flags):
                raise RuntimeError(
                    f"delta drift at step {step}: tracked={current} verified={verified}"
                )
            weights = node_weights()
            print(
                f"progress step={step}/{args.steps} current={current} best={best} "
                f"temp={temperature:.3f} accepted={accepted} uphill={uphill} "
                f"elapsed={time.time() - started:.1f}s",
                flush=True,
            )
        if step % cycle_length == 0 and step < args.steps:
            positions = best_positions.copy()
            flags = v34.crossing_flags_for_pairs(positions, evaluator)
            current = int(flags.sum())
            weights = node_weights()

    direct.write_scope_positions(args.out_tsv, layout, scope_nodes, best_positions)
    print(
        f"done best={best} out={args.out_tsv} elapsed={time.time() - started:.1f}s",
        flush=True,
    )


if __name__ == "__main__":
    main()
