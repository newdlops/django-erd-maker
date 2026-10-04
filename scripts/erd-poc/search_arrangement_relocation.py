#!/usr/bin/env python3
"""Relocate real nodes through critical line-arrangement cells.

For a moved node v, the crossing status of edge (v,u) changes only when v
crosses a line through u and an endpoint of another relationship.  Sampling
adjacent cells around intersections of those critical lines targets the exact
combinatorial boundaries that uniform coordinate sampling usually misses.
"""

from __future__ import annotations

import argparse
import importlib.util
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
scope_helper = load_module(
    "optimize_direct_crossings_torch",
    ROOT / "scripts/erd-poc/optimize_direct_crossings_torch.py",
)


def crossed_partner_nodes(
    node: int,
    positions: np.ndarray,
    evaluator: direct.DirectNodeMoveEvaluator,
) -> np.ndarray:
    left, right = evaluator.pairs_by_node[node]
    if left.size == 0:
        return np.empty(0, dtype=np.int32)
    left_edges = evaluator.edges[left]
    right_edges = evaluator.edges[right]
    flags = direct.proper_crossings(
        positions[left_edges[:, 0]],
        positions[left_edges[:, 1]],
        positions[right_edges[:, 0]],
        positions[right_edges[:, 1]],
    )
    if not np.any(flags):
        return np.empty(0, dtype=np.int32)
    partner_edges = right_edges[flags]
    return np.unique(partner_edges.ravel()).astype(np.int32)


def line_intersections(
    origins: np.ndarray,
    directions: np.ndarray,
    origin_keys: np.ndarray,
    max_pairs: int,
    rng: np.random.Generator,
) -> np.ndarray:
    line_count = origins.shape[0]
    if line_count < 2:
        return np.empty((0, 2), dtype=np.float64)
    left, right = np.triu_indices(line_count, k=1)
    keep = origin_keys[left] != origin_keys[right]
    left = left[keep]
    right = right[keep]
    if left.size > max_pairs:
        selected = rng.choice(left.size, size=max_pairs, replace=False)
        left = left[selected]
        right = right[selected]
    p = origins[left]
    r = directions[left]
    q = origins[right]
    s = directions[right]
    denominator = r[:, 0] * s[:, 1] - r[:, 1] * s[:, 0]
    scale = np.linalg.norm(r, axis=1) * np.linalg.norm(s, axis=1)
    valid = np.abs(denominator) > np.maximum(1e-12, scale * 1e-9)
    p = p[valid]
    r = r[valid]
    q = q[valid]
    s = s[valid]
    denominator = denominator[valid]
    q_minus_p = q - p
    parameter = (
        q_minus_p[:, 0] * s[:, 1] - q_minus_p[:, 1] * s[:, 0]
    ) / denominator
    return p + parameter[:, None] * r


def arrangement_candidates(
    node: int,
    positions: np.ndarray,
    graph: nx.Graph,
    evaluator: direct.DirectNodeMoveEvaluator,
    rng: np.random.Generator,
    max_line_pairs: int,
    background_endpoints: int,
    jitter_directions: int,
    extent: float,
    min_distance: float,
) -> np.ndarray:
    neighbors = np.asarray(sorted(graph.neighbors(node)), dtype=np.int32)
    hot_endpoints = crossed_partner_nodes(node, positions, evaluator)
    background = np.asarray(
        [index for index in range(len(positions)) if index != node],
        dtype=np.int32,
    )
    if background.size > background_endpoints:
        background = rng.choice(
            background, size=background_endpoints, replace=False
        ).astype(np.int32)
    endpoints = np.unique(np.concatenate((hot_endpoints, background))).astype(
        np.int32
    )
    line_origins: list[np.ndarray] = []
    line_directions: list[np.ndarray] = []
    origin_keys: list[int] = []
    for neighbor in neighbors:
        valid = endpoints[(endpoints != neighbor) & (endpoints != node)]
        directions = positions[valid] - positions[neighbor]
        nonzero = np.linalg.norm(directions, axis=1) > 1e-12
        if np.any(nonzero):
            count = int(nonzero.sum())
            line_origins.append(
                np.repeat(positions[neighbor][None, :], count, axis=0)
            )
            line_directions.append(directions[nonzero])
            origin_keys.extend([int(neighbor)] * count)
    if not line_origins:
        return positions[node][None, :]
    intersections = line_intersections(
        np.concatenate(line_origins),
        np.concatenate(line_directions),
        np.asarray(origin_keys, dtype=np.int32),
        max_line_pairs,
        rng,
    )
    low = positions.min(axis=0)
    high = positions.max(axis=0)
    span = np.maximum(high - low, 1e-9)
    inside = np.all(
        (intersections >= low - span * extent)
        & (intersections <= high + span * extent),
        axis=1,
    )
    intersections = intersections[inside]
    diagonal = float(np.linalg.norm(span))
    angles = np.linspace(
        0.0, 2.0 * np.pi, max(4, jitter_directions), endpoint=False
    )
    unit = np.column_stack((np.cos(angles), np.sin(angles)))
    parts = [positions[node][None, :]]
    for epsilon_ratio in (2e-6, 2e-5, 2e-4, 2e-3):
        epsilon = max(1e-10, diagonal * epsilon_ratio)
        parts.append(
            (intersections[:, None, :] + unit[None, :, :] * epsilon).reshape(
                -1, 2
            )
        )
    # Unbounded cells are represented by points outside each side/corner.
    outside = np.asarray(
        [
            [low[0] - span[0] * extent, low[1] - span[1] * extent],
            [low[0] - span[0] * extent, high[1] + span[1] * extent],
            [high[0] + span[0] * extent, low[1] - span[1] * extent],
            [high[0] + span[0] * extent, high[1] + span[1] * extent],
            [(low[0] + high[0]) * 0.5, low[1] - span[1] * extent],
            [(low[0] + high[0]) * 0.5, high[1] + span[1] * extent],
            [low[0] - span[0] * extent, (low[1] + high[1]) * 0.5],
            [high[0] + span[0] * extent, (low[1] + high[1]) * 0.5],
        ],
        dtype=np.float64,
    )
    parts.append(outside)
    candidates = np.concatenate(parts)
    candidates = candidates[np.isfinite(candidates).all(axis=1)]
    if candidates.size:
        # Grid-key dedup is only a performance optimization; the retained
        # points stay at their original non-collinear coordinates.
        quantum = max(diagonal * 1e-8, 1e-12)
        keys = np.round(candidates / quantum).astype(np.int64)
        _, unique = np.unique(keys, axis=0, return_index=True)
        candidates = candidates[np.sort(unique)]
    others = np.delete(positions, node, axis=0)
    if min_distance > 0.0 and others.size:
        keep_parts = []
        for start in range(0, len(candidates), 256):
            trial = candidates[start : start + 256]
            nearest = np.linalg.norm(
                trial[:, None, :] - others[None, :, :], axis=2
            ).min(axis=1)
            keep_parts.append(nearest >= min_distance)
        keep = np.concatenate(keep_parts)
        keep[0] = True
        candidates = candidates[keep]
    return candidates


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--positions", type=Path, required=True)
    parser.add_argument("--out-tsv", type=Path, required=True)
    parser.add_argument(
        "--scope", choices=("core3", "core3-single", "giant", "all"), default="core3"
    )
    parser.add_argument("--rounds", type=int, default=12)
    parser.add_argument("--node-limit", type=int, default=32)
    parser.add_argument("--max-line-pairs", type=int, default=5000)
    parser.add_argument("--background-endpoints", type=int, default=64)
    parser.add_argument("--jitter-directions", type=int, default=8)
    parser.add_argument("--extent", type=float, default=0.35)
    parser.add_argument("--min-distance-ratio", type=float, default=0.003)
    parser.add_argument("--chunk-size", type=int, default=24)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    layout = v34.load_layout(args.layout)
    scope_nodes, edges64, graph = scope_helper.scope_graph(layout, args.scope)
    edges = edges64.astype(np.int32)
    positions = v34.read_positions_tsv(args.positions, layout)[scope_nodes].copy()
    evaluator = direct.DirectNodeMoveEvaluator(edges, len(scope_nodes))
    tracked = evaluator.global_evaluator.count_crossings(positions)
    diagonal = float(np.linalg.norm(positions.max(axis=0) - positions.min(axis=0)))
    min_distance = diagonal * args.min_distance_ratio
    rng = np.random.default_rng(args.seed)
    started = time.time()
    print(
        f"start scope={args.scope} nodes={len(scope_nodes)} edges={len(edges)} "
        f"cross={tracked} minDistance={min_distance:.6f}",
        flush=True,
    )
    for round_index in range(args.rounds):
        scores = np.asarray(
            [
                evaluator.node_pair_crossings(node, positions)
                for node in range(len(scope_nodes))
            ],
            dtype=np.int32,
        )
        order = np.argsort(-scores, kind="stable")
        if args.node_limit > 0:
            order = order[: args.node_limit]
        accepted = 0
        gain = 0
        candidate_total = 0
        for node_raw in order:
            node = int(node_raw)
            before = evaluator.node_pair_crossings(node, positions)
            candidates = arrangement_candidates(
                node,
                positions,
                graph,
                evaluator,
                rng,
                args.max_line_pairs,
                args.background_endpoints,
                args.jitter_directions,
                args.extent,
                min_distance,
            )
            candidate_total += len(candidates)
            counts = evaluator.candidate_counts(
                node, candidates, positions, args.chunk_size
            )
            best_index = int(np.argmin(counts))
            after = int(counts[best_index])
            if after >= before:
                continue
            positions[node] = candidates[best_index]
            tracked -= before - after
            gain += before - after
            accepted += 1
        verified = evaluator.global_evaluator.count_crossings(positions)
        if verified != tracked:
            raise RuntimeError(
                f"crossing drift tracked={tracked} verified={verified}"
            )
        direct.write_scope_positions(args.out_tsv, layout, scope_nodes, positions)
        print(
            f"round={round_index + 1} accepted={accepted} gain={gain} "
            f"cross={tracked} candidates={candidate_total} "
            f"elapsed={time.time() - started:.1f}s",
            flush=True,
        )
        if accepted == 0:
            break
    print(f"done cross={tracked} out={args.out_tsv}", flush=True)


if __name__ == "__main__":
    main()
