#!/usr/bin/env python3
"""Absorb graph changes by relocating the affected real-node frontier.

The reference snapshot supplies only a warm start.  Nodes added to the graph,
nodes removed from it, and endpoints whose relationships changed define a
movable frontier.  Every current relationship remains an independent straight
segment; no proxy, bend, or relationship aggregation is introduced.

The evaluator materializes crossing pairs for only one moved node at a time.
That keeps memory proportional to ``degree(node) * edge_count`` instead of
retaining the all-node candidate tensor used by the historical optimizer.
"""

from __future__ import annotations

import argparse
import importlib.util
import math
import sys
import time
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


def relationship_pairs(layout: dict) -> set[tuple[str, str]]:
    result: set[tuple[str, str]] = set()
    for edge in layout.get("routedEdges", []):
        source = str(edge.get("sourceModelId", ""))
        target = str(edge.get("targetModelId", ""))
        if source and target and source != target:
            result.add(tuple(sorted((source, target))))
    return result


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

    first = orient(a, b, c)
    second = orient(a, b, d)
    third = orient(c, d, a)
    fourth = orient(c, d, b)
    return (
        (np.sign(first) != np.sign(second))
        & (np.sign(third) != np.sign(fourth))
        & (first != 0.0)
        & (second != 0.0)
        & (third != 0.0)
        & (fourth != 0.0)
    )


class LazyNodeCrossingEvaluator:
    def __init__(self, edges: np.ndarray):
        self.edges = np.asarray(edges, dtype=np.int32)
        self.sources = self.edges[:, 0]
        self.targets = self.edges[:, 1]
        self.all_edges = np.arange(self.edges.shape[0], dtype=np.int32)

    def pairs(self, node: int) -> tuple[np.ndarray, np.ndarray]:
        incident = self.all_edges[
            (self.sources == node) | (self.targets == node)
        ]
        left_parts: list[np.ndarray] = []
        right_parts: list[np.ndarray] = []
        for edge_index_raw in incident:
            edge_index = int(edge_index_raw)
            source, target = self.edges[edge_index]
            other_mask = (
                (self.all_edges != edge_index)
                & (self.sources != source)
                & (self.targets != source)
                & (self.sources != target)
                & (self.targets != target)
            )
            others = self.all_edges[other_mask]
            if others.size:
                left_parts.append(
                    np.full(others.size, edge_index, dtype=np.int32)
                )
                right_parts.append(others)
        if not left_parts:
            empty = np.empty(0, dtype=np.int32)
            return empty, empty
        return np.concatenate(left_parts), np.concatenate(right_parts)

    def counts(
        self,
        node: int,
        candidates: np.ndarray,
        positions: np.ndarray,
        max_tensor_mib: float,
    ) -> np.ndarray:
        left, right = self.pairs(node)
        if left.size == 0:
            return np.zeros(candidates.shape[0], dtype=np.int32)
        left_edges = self.edges[left]
        right_edges = self.edges[right]
        # Four endpoint tensors, two coordinates, float64.  A conservative
        # divisor leaves room for masks and orientation temporaries.
        bytes_per_candidate = max(1, left.size * 2 * 8 * 10)
        chunk_size = max(
            1,
            min(
                candidates.shape[0],
                int(max_tensor_mib * 1024 * 1024 / bytes_per_candidate),
            ),
        )
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
            for endpoint, endpoint_nodes in (
                (a, left_edges[:, 0]),
                (b, left_edges[:, 1]),
                (c, right_edges[:, 0]),
                (d, right_edges[:, 1]),
            ):
                mask = endpoint_nodes == node
                if np.any(mask):
                    endpoint[:, mask, :] = trial[:, None, :]
            result[start:stop] = proper_crossings(a, b, c, d).sum(axis=1)
        return result


def candidate_points(
    node: int,
    positions: np.ndarray,
    adjacency: list[np.ndarray],
    widths: np.ndarray,
    heights: np.ndarray,
    rng: np.random.Generator,
    angular_samples: int,
    random_samples: int,
) -> np.ndarray:
    current = positions[node]
    neighbors = adjacency[node]
    if neighbors.size:
        neighbor_positions = positions[neighbors]
        neighbor_center = neighbor_positions.mean(axis=0)
    else:
        neighbor_positions = np.empty((0, 2), dtype=np.float64)
        neighbor_center = current
    low = positions.min(axis=0)
    high = positions.max(axis=0)
    span = np.maximum(high - low, 1.0)
    diagonal = float(np.linalg.norm(span))
    points: list[np.ndarray] = [current[None, :], neighbor_center[None, :]]

    phase = rng.uniform(0.0, math.tau)
    bases = [(neighbor_center, None)]
    bases.extend((positions[int(neighbor)], int(neighbor)) for neighbor in neighbors)
    for base, neighbor in bases:
        if neighbor is None:
            clearance = max(widths[node], heights[node]) + diagonal * 0.006
        else:
            clearance = 0.5 * math.hypot(
                widths[node] + widths[neighbor],
                heights[node] + heights[neighbor],
            ) + diagonal * 0.003
        for multiplier in (1.0, 1.6, 2.6, 4.2, 7.0):
            angles = phase + np.arange(angular_samples) * math.tau / angular_samples
            radius = clearance * multiplier
            points.append(
                base
                + np.column_stack((np.cos(angles), np.sin(angles))) * radius
            )

    for scale in (0.01, 0.025, 0.06, 0.14, 0.3):
        count = max(4, random_samples // 5)
        points.append(
            neighbor_center
            + rng.normal(0.0, span * scale, size=(count, 2))
        )
    if random_samples > 0:
        points.append(
            rng.uniform(low - span * 0.05, high + span * 0.05, size=(random_samples, 2))
        )
    candidates = np.concatenate(points, axis=0)
    candidates = candidates[np.isfinite(candidates).all(axis=1)]
    # Rounded uniqueness removes repeated ring locations without building a
    # quadratic distance matrix.
    rounded = np.round(candidates, decimals=5)
    _, unique_indices = np.unique(rounded, axis=0, return_index=True)
    return candidates[np.sort(unique_indices)]


def collision_free_candidates(
    node: int,
    candidates: np.ndarray,
    positions: np.ndarray,
    widths: np.ndarray,
    heights: np.ndarray,
    margin: float,
) -> np.ndarray:
    other_mask = np.ones(positions.shape[0], dtype=bool)
    other_mask[node] = False
    other_positions = positions[other_mask]
    half_width = (widths[node] + widths[other_mask]) * 0.5 + margin
    half_height = (heights[node] + heights[other_mask]) * 0.5 + margin
    keep = np.ones(candidates.shape[0], dtype=bool)
    for start in range(0, candidates.shape[0], 256):
        trial = candidates[start:start + 256]
        delta = np.abs(trial[:, None, :] - other_positions[None, :, :])
        overlaps = (delta[:, :, 0] < half_width) & (delta[:, :, 1] < half_height)
        keep[start:start + trial.shape[0]] = ~overlaps.any(axis=1)
    # The warm-start coordinate remains a legal non-regression choice even if
    # the reference snapshot already contained a spacing defect.
    keep[0] = True
    return candidates[keep]


def total_edge_length(node: int, point: np.ndarray, positions: np.ndarray, neighbors: np.ndarray) -> float:
    if neighbors.size == 0:
        return 0.0
    return float(np.linalg.norm(positions[neighbors] - point, axis=1).sum())


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--positions", type=Path, required=True)
    parser.add_argument("--reference-layout", type=Path, required=True)
    parser.add_argument("--out-tsv", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=4)
    parser.add_argument("--node-limit", type=int, default=96)
    parser.add_argument("--frontier-hops", type=int, default=0)
    parser.add_argument("--angular-samples", type=int, default=16)
    parser.add_argument("--random-samples", type=int, default=40)
    parser.add_argument("--margin", type=float, default=18.0)
    parser.add_argument("--max-tensor-mib", type=float, default=48.0)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    layout = v34.load_layout(args.layout)
    reference = v34.load_layout(args.reference_layout)
    positions = v34.read_positions_tsv(args.positions, layout)
    edges = v34.graph_edges(layout)
    model_ids = [str(node["modelId"]) for node in layout["nodes"]]
    index_by_id = {model_id: index for index, model_id in enumerate(model_ids)}
    current_pairs = {
        tuple(sorted((model_ids[int(source)], model_ids[int(target)])))
        for source, target in edges
    }
    reference_pairs = relationship_pairs(reference)
    changed_ids = {
        model_id
        for pair in current_pairs ^ reference_pairs
        for model_id in pair
        if model_id in index_by_id
    }
    reference_ids = {
        str(node["modelId"]) for node in reference.get("nodes", [])
    }
    changed_ids.update(set(model_ids) - reference_ids)

    adjacency_sets: list[set[int]] = [set() for _ in model_ids]
    for source_raw, target_raw in edges:
        source = int(source_raw)
        target = int(target_raw)
        adjacency_sets[source].add(target)
        adjacency_sets[target].add(source)
    frontier = {index_by_id[model_id] for model_id in changed_ids}
    for _ in range(max(0, args.frontier_hops)):
        frontier.update(
            neighbor
            for node in tuple(frontier)
            for neighbor in adjacency_sets[node]
        )
    adjacency = [
        np.asarray(sorted(neighbors), dtype=np.int32)
        for neighbors in adjacency_sets
    ]
    widths, heights = v34.render_node_sizes(layout, edges, direct_scene=True)
    evaluator = LazyNodeCrossingEvaluator(edges)
    total_crossings = v34.fce.count_crossings_streaming(positions, edges)
    rng = np.random.default_rng(args.seed)
    started = time.time()
    print(
        f"start nodes={len(model_ids)} edges={len(edges)} frontier={len(frontier)} "
        f"cross={total_crossings}",
        flush=True,
    )

    for round_index in range(max(0, args.rounds)):
        scored_nodes: list[tuple[int, int]] = []
        for node in sorted(frontier):
            current_count = int(evaluator.counts(
                node,
                positions[node][None, :],
                positions,
                args.max_tensor_mib,
            )[0])
            scored_nodes.append((current_count, node))
        scored_nodes.sort(key=lambda item: (-item[0], model_ids[item[1]]))
        if args.node_limit > 0:
            scored_nodes = scored_nodes[:args.node_limit]

        accepted = 0
        gain = 0
        for _pressure, node in scored_nodes:
            current_count = int(evaluator.counts(
                node,
                positions[node][None, :],
                positions,
                args.max_tensor_mib,
            )[0])
            candidates = candidate_points(
                node,
                positions,
                adjacency,
                widths,
                heights,
                rng,
                args.angular_samples,
                args.random_samples,
            )
            candidates = collision_free_candidates(
                node,
                candidates,
                positions,
                widths,
                heights,
                args.margin,
            )
            counts = evaluator.counts(
                node,
                candidates,
                positions,
                args.max_tensor_mib,
            )
            best_count = int(counts.min())
            if best_count >= current_count:
                continue
            best_indices = np.flatnonzero(counts == best_count)
            best_index = min(
                (int(index) for index in best_indices),
                key=lambda index: total_edge_length(
                    node, candidates[index], positions, adjacency[node]
                ),
            )
            positions[node] = candidates[best_index]
            delta = current_count - best_count
            total_crossings -= delta
            gain += delta
            accepted += 1

        verified = v34.fce.count_crossings_streaming(positions, edges)
        if verified != total_crossings:
            raise RuntimeError(
                f"incremental crossing drift: tracked={total_crossings} verified={verified}"
            )
        v34.write_positions_tsv(args.out_tsv, layout, positions)
        print(
            f"round={round_index + 1} accepted={accepted} gain={gain} "
            f"cross={verified} elapsed={time.time() - started:.1f}s",
            flush=True,
        )
        if accepted == 0:
            break
    print(
        f"done cross={total_crossings} frontier={len(frontier)} "
        f"out={args.out_tsv}",
        flush=True,
    )


if __name__ == "__main__":
    main()
