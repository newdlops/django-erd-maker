#!/usr/bin/env python3
"""Minimize straight relationship crossings by moving real 3-core nodes.

This search never changes, contracts, or reroutes an edge.  For one real node
at a time it samples candidate coordinates and scores the exact set of direct
source-to-target segments affected by that move.  It is deliberately separate
from rendered edge bundling: the only optimized variables are node positions.
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


def proper_crossings(
    a: np.ndarray,
    b: np.ndarray,
    c: np.ndarray,
    d: np.ndarray,
) -> np.ndarray:
    def orient(p: np.ndarray, q: np.ndarray, r: np.ndarray) -> np.ndarray:
        return (
            (q[..., 0] - p[..., 0]) * (r[..., 1] - p[..., 1])
            - (q[..., 1] - p[..., 1]) * (r[..., 0] - p[..., 0])
        )

    o1 = orient(a, b, c)
    o2 = orient(a, b, d)
    o3 = orient(c, d, a)
    o4 = orient(c, d, b)
    return (
        (np.sign(o1) != np.sign(o2))
        & (np.sign(o3) != np.sign(o4))
        & (o1 != 0.0)
        & (o2 != 0.0)
        & (o3 != 0.0)
        & (o4 != 0.0)
    )


class DirectNodeMoveEvaluator:
    def __init__(self, edges: np.ndarray, node_count: int):
        self.edges = np.asarray(edges, dtype=np.int32)
        self.node_count = node_count
        self.global_evaluator = v34.fce.FastCrossEval(self.edges, node_count)
        self.pairs_by_node: list[tuple[np.ndarray, np.ndarray]] = []
        sources = self.edges[:, 0]
        targets = self.edges[:, 1]
        all_edges = np.arange(self.edges.shape[0], dtype=np.int32)
        for node in range(node_count):
            incident = all_edges[(sources == node) | (targets == node)]
            left_parts: list[np.ndarray] = []
            right_parts: list[np.ndarray] = []
            for edge_index in incident:
                source, target = self.edges[edge_index]
                other_mask = (
                    (all_edges != edge_index)
                    & (sources != source)
                    & (targets != source)
                    & (sources != target)
                    & (targets != target)
                )
                others = all_edges[other_mask]
                if others.size:
                    left_parts.append(np.full(others.size, edge_index, dtype=np.int32))
                    right_parts.append(others)
            if left_parts:
                left = np.concatenate(left_parts)
                right = np.concatenate(right_parts)
                # Each affected pair contains exactly one edge incident to the
                # moved node, so it occurs once.  Sorting makes batch reads a
                # little more cache-friendly without changing semantics.
                order = np.lexsort((right, left))
                self.pairs_by_node.append((left[order], right[order]))
            else:
                self.pairs_by_node.append((
                    np.empty(0, dtype=np.int32),
                    np.empty(0, dtype=np.int32),
                ))

    def node_pair_crossings(self, node: int, positions: np.ndarray) -> int:
        left, right = self.pairs_by_node[node]
        if left.size == 0:
            return 0
        left_edges = self.edges[left]
        right_edges = self.edges[right]
        return int(proper_crossings(
            positions[left_edges[:, 0]],
            positions[left_edges[:, 1]],
            positions[right_edges[:, 0]],
            positions[right_edges[:, 1]],
        ).sum())

    def candidate_counts(
        self,
        node: int,
        candidates: np.ndarray,
        positions: np.ndarray,
        chunk_size: int,
    ) -> np.ndarray:
        left, right = self.pairs_by_node[node]
        if left.size == 0:
            return np.zeros(candidates.shape[0], dtype=np.int32)
        left_edges = self.edges[left]
        right_edges = self.edges[right]
        result = np.empty(candidates.shape[0], dtype=np.int32)
        for start in range(0, candidates.shape[0], chunk_size):
            stop = min(candidates.shape[0], start + chunk_size)
            trial = candidates[start:stop]
            count = stop - start
            a = np.broadcast_to(
                positions[left_edges[:, 0]][None, :, :],
                (count, left.size, 2),
            ).copy()
            b = np.broadcast_to(
                positions[left_edges[:, 1]][None, :, :],
                (count, left.size, 2),
            ).copy()
            c = np.broadcast_to(
                positions[right_edges[:, 0]][None, :, :],
                (count, left.size, 2),
            ).copy()
            d = np.broadcast_to(
                positions[right_edges[:, 1]][None, :, :],
                (count, left.size, 2),
            ).copy()
            for endpoint, values in (
                (a, left_edges[:, 0]),
                (b, left_edges[:, 1]),
                (c, right_edges[:, 0]),
                (d, right_edges[:, 1]),
            ):
                mask = values == node
                if np.any(mask):
                    endpoint[:, mask, :] = trial[:, None, :]
            result[start:stop] = proper_crossings(a, b, c, d).sum(axis=1)
        return result


def candidate_points(
    node: int,
    positions: np.ndarray,
    graph: nx.Graph,
    rng: np.random.Generator,
    random_count: int,
    jitter_count: int,
    min_distance: float,
) -> np.ndarray:
    low = positions.min(axis=0)
    high = positions.max(axis=0)
    span = np.maximum(high - low, 1e-6)
    current = positions[node]
    neighbors = np.asarray(sorted(graph.neighbors(node)), dtype=np.int32)
    neighbor_center = (
        positions[neighbors].mean(axis=0) if neighbors.size else current
    )
    points: list[np.ndarray] = [current[None, :], neighbor_center[None, :]]
    # Uniform samples explore different cells in the line arrangement.
    points.append(rng.uniform(low - span * 0.08, high + span * 0.08, size=(random_count, 2)))
    # Multi-scale samples exploit the current and graph-barycentric regions.
    per_scale = max(4, jitter_count // 6)
    for scale in (0.015, 0.04, 0.09, 0.18, 0.36, 0.7):
        points.append(current + rng.normal(0.0, span * scale, size=(per_scale, 2)))
        points.append(neighbor_center + rng.normal(0.0, span * scale, size=(per_scale, 2)))
    # Positions beside other real nodes are useful cell representatives, but
    # never coincide with them.  Four deterministic offsets retain spacing.
    base_indices = rng.integers(0, positions.shape[0], size=min(positions.shape[0], jitter_count))
    bases = positions[base_indices]
    offset_scale = max(min_distance * 1.35, float(np.linalg.norm(span)) * 0.01)
    angles = rng.uniform(0.0, 2.0 * np.pi, size=bases.shape[0])
    points.append(bases + np.column_stack((np.cos(angles), np.sin(angles))) * offset_scale)
    candidates = np.concatenate(points, axis=0)
    candidates = candidates[np.isfinite(candidates).all(axis=1)]
    others = np.delete(positions, node, axis=0)
    if others.size and min_distance > 0.0:
        keep_parts: list[np.ndarray] = []
        for start in range(0, candidates.shape[0], 512):
            trial = candidates[start:start + 512]
            nearest = np.linalg.norm(trial[:, None, :] - others[None, :, :], axis=2).min(axis=1)
            keep_parts.append(nearest >= min_distance)
        keep = np.concatenate(keep_parts)
        keep[0] = True
        candidates = candidates[keep]
    return candidates


def write_scope_positions(
    path: Path,
    layout: dict,
    scope_nodes: list[int],
    positions: np.ndarray,
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    model_ids = [str(node["modelId"]) for node in layout["nodes"]]
    path.write_text(
        "".join(
            f"{model_ids[global_node]}\t{positions[local_node, 0]:.6f}"
            f"\t{positions[local_node, 1]:.6f}\n"
            for local_node, global_node in enumerate(scope_nodes)
        ),
        encoding="utf-8",
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--positions", type=Path, required=True)
    parser.add_argument("--out-tsv", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=30)
    parser.add_argument("--random-candidates", type=int, default=900)
    parser.add_argument("--jitter-candidates", type=int, default=240)
    parser.add_argument("--node-limit", type=int, default=0)
    parser.add_argument("--min-distance-ratio", type=float, default=0.012)
    parser.add_argument("--chunk-size", type=int, default=48)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument(
        "--include-shell-links",
        action="store_true",
        help=(
            "When optimizing the 3-core, also score structural links induced "
            "by the real shell-node components attached to it."
        ),
    )
    parser.add_argument(
        "--scope",
        choices=("core3", "core3-single", "giant", "all"),
        default="core3",
        help="real nodes whose induced direct relationships are optimized",
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
    elif args.scope == "giant":
        scope_nodes = sorted(giant)
    else:
        scope_nodes = list(range(len(layout["nodes"])))
    global_to_local = {node: index for index, node in enumerate(scope_nodes)}
    scope_global_edges = {
        tuple(sorted((int(source), int(target))))
        for source, target in global_edges
        if int(source) in global_to_local
        and int(target) in global_to_local
        and int(source) != int(target)
    }
    if args.include_shell_links:
        if args.scope not in {"core3", "core3-single"}:
            raise ValueError(
                "--include-shell-links is only meaningful for a 3-core scope"
            )
        shell = giant - core3
        for component in nx.connected_components(graph.subgraph(shell)):
            attachments = sorted({
                int(neighbor)
                for node in component
                for neighbor in graph.neighbors(node)
                if neighbor in core3
            })
            if len(attachments) == 2:
                scope_global_edges.add(tuple(attachments))
            elif len(attachments) > 2:
                attachment_metric = nx.Graph()
                attachment_metric.add_nodes_from(attachments)
                for left_index, left in enumerate(attachments):
                    for right in attachments[left_index + 1 :]:
                        attachment_metric.add_edge(
                            left,
                            right,
                            weight=nx.shortest_path_length(
                                graph.subgraph(giant), left, right
                            ),
                        )
                for source, target in nx.minimum_spanning_edges(
                    attachment_metric,
                    data=False,
                ):
                    scope_global_edges.add(tuple(sorted((source, target))))
    scope_edges = np.asarray([
        (global_to_local[source], global_to_local[target])
        for source, target in sorted(scope_global_edges)
    ], dtype=np.int32)
    scope_graph = nx.Graph()
    scope_graph.add_nodes_from(range(len(scope_nodes)))
    scope_graph.add_edges_from((int(source), int(target)) for source, target in scope_edges)
    global_positions = v34.read_positions_tsv(args.positions, layout)
    positions = global_positions[scope_nodes].copy()
    evaluator = DirectNodeMoveEvaluator(scope_edges, len(scope_nodes))
    total_crossings = evaluator.global_evaluator.count_crossings(positions)
    diagonal = float(np.linalg.norm(positions.max(axis=0) - positions.min(axis=0)))
    min_distance = diagonal * args.min_distance_ratio
    rng = np.random.default_rng(args.seed)
    started = time.time()
    print(
        f"start scope={args.scope} nodes={len(scope_nodes)} edges={len(scope_edges)} "
        f"cross={total_crossings} "
        f"minDistance={min_distance:.3f}",
        flush=True,
    )
    for round_index in range(args.rounds):
        node_scores = np.asarray([
            evaluator.node_pair_crossings(node, positions)
            for node in range(len(scope_nodes))
        ], dtype=np.int32)
        order = np.argsort(-node_scores, kind="stable")
        if args.node_limit > 0:
            order = order[:args.node_limit]
        accepted = 0
        gain = 0
        for node_raw in order:
            node = int(node_raw)
            current_local = evaluator.node_pair_crossings(node, positions)
            candidates = candidate_points(
                node,
                positions,
                scope_graph,
                rng,
                args.random_candidates,
                args.jitter_candidates,
                min_distance,
            )
            counts = evaluator.candidate_counts(
                node,
                candidates,
                positions,
                args.chunk_size,
            )
            best_index = int(np.argmin(counts))
            best_local = int(counts[best_index])
            if best_local >= current_local:
                continue
            positions[node] = candidates[best_index]
            delta = current_local - best_local
            total_crossings -= delta
            gain += delta
            accepted += 1
        verified = evaluator.global_evaluator.count_crossings(positions)
        if verified != total_crossings:
            raise RuntimeError(
                f"incremental crossing drift: tracked={total_crossings}, verified={verified}"
            )
        write_scope_positions(args.out_tsv, layout, scope_nodes, positions)
        print(
            f"round={round_index + 1} accepted={accepted} gain={gain} "
            f"cross={verified} elapsed={time.time() - started:.1f}s",
            flush=True,
        )
        if accepted == 0:
            break
    print(f"done cross={total_crossings} out={args.out_tsv}")


if __name__ == "__main__":
    main()
