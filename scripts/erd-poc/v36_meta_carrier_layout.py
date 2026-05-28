#!/usr/bin/env python3
"""Generic meta-carrier layout proposal for Captain-scale ERDs.

This is a research prototype, not a §13 clone. It builds a coarse graph whose
nodes are structural groups (clusters plus no-cluster pseudo groups/singletons),
runs a small force-directed optimization on those group centroids, then writes
node positions by translating each group rigidly.

The intent is to create one large, generic batch proposal that greedy local
v34 moves cannot reach.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import math
import sys
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "v34_move_search", ROOT / "scripts/erd-poc/v34_move_search.py"
)
v34 = importlib.util.module_from_spec(SPEC)
sys.modules["v34_move_search"] = v34
SPEC.loader.exec_module(v34)


def build_groups(layout: dict, edges: np.ndarray) -> list[np.ndarray]:
    groups: list[np.ndarray] = []
    assigned = np.zeros(len(layout["nodes"]), dtype=bool)
    for _cid, members in v34.cluster_members(layout).items():
        groups.append(members)
        assigned[members] = True
    for _key, members in v34.no_cluster_pseudo_groups(layout, edges).items():
        free = members[~assigned[members]]
        if free.size > 0:
            groups.append(free)
            assigned[free] = True
    for idx in np.flatnonzero(~assigned):
        groups.append(np.array([idx], dtype=np.int32))
    return groups


def group_graph(groups: list[np.ndarray], edges: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    owner = np.full(max(int(edges.max()) + 1 if edges.size else 0, sum(len(g) for g in groups)), -1, dtype=np.int32)
    for gi, members in enumerate(groups):
        owner[members] = gi
    weights: dict[tuple[int, int], int] = {}
    for s_raw, t_raw in edges:
        s = int(owner[int(s_raw)])
        t = int(owner[int(t_raw)])
        if s < 0 or t < 0 or s == t:
            continue
        if s > t:
            s, t = t, s
        weights[(s, t)] = weights.get((s, t), 0) + 1
    if not weights:
        return np.zeros((0, 2), dtype=np.int32), np.zeros(0, dtype=np.float64)
    pairs = np.array(list(weights.keys()), dtype=np.int32)
    w = np.array([weights[tuple(p)] for p in pairs], dtype=np.float64)
    return pairs, w


def force_layout(
    centers: np.ndarray,
    sizes: np.ndarray,
    meta_edges: np.ndarray,
    edge_weights: np.ndarray,
    *,
    iterations: int,
    step: float,
    edge_len: float,
    attract: float,
    repel: float,
    anchor: float,
    movable_mask: np.ndarray,
) -> np.ndarray:
    pos = centers.copy()
    origin = centers.copy()
    n = pos.shape[0]
    soft_size = np.sqrt(np.maximum(sizes, 1.0))
    for it in range(iterations):
        force = np.zeros_like(pos)
        # Repulsion. O(G^2), but G is small enough for this prototype.
        diff = pos[:, None, :] - pos[None, :, :]
        d2 = np.sum(diff * diff, axis=2) + 1e-6
        np.fill_diagonal(d2, np.inf)
        desired = edge_len * (0.4 + 0.08 * (soft_size[:, None] + soft_size[None, :]))
        rep = repel * desired * desired / d2
        force += np.sum(diff / np.sqrt(d2)[:, :, None] * rep[:, :, None], axis=1)

        if meta_edges.size:
            s = meta_edges[:, 0]
            t = meta_edges[:, 1]
            vec = pos[t] - pos[s]
            dist = np.linalg.norm(vec, axis=1) + 1e-6
            target = edge_len * (1.0 + 0.08 * (soft_size[s] + soft_size[t]))
            mag = attract * edge_weights * (dist - target)
            delta = vec / dist[:, None] * mag[:, None]
            np.add.at(force, s, delta)
            np.add.at(force, t, -delta)

        force += anchor * (origin - pos)
        force[~movable_mask] = 0.0
        lr = step * (1.0 - it / max(1, iterations))
        pos += np.clip(force, -edge_len, edge_len) * lr
    return pos


def scale_to_bbox(positions: np.ndarray, target_b: float) -> np.ndarray:
    span = positions.max(axis=0) - positions.min(axis=0)
    area = float(span[0] * span[1] / 1e9)
    if area <= target_b or area <= 1e-9:
        return positions
    center = (positions.min(axis=0) + positions.max(axis=0)) / 2.0
    scale = math.sqrt(target_b / area)
    return center + (positions - center) * scale


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--layout", type=Path, required=True)
    ap.add_argument("--positions", type=Path, required=True)
    ap.add_argument("--out-tsv", type=Path, required=True)
    ap.add_argument("--iterations", type=int, default=300)
    ap.add_argument("--step", type=float, default=0.015)
    ap.add_argument("--edge-len", type=float, default=2200.0)
    ap.add_argument("--attract", type=float, default=0.08)
    ap.add_argument("--repel", type=float, default=0.25)
    ap.add_argument("--anchor", type=float, default=0.015)
    ap.add_argument("--move-max-group-size", type=int, default=12)
    ap.add_argument("--bbox-target-b", type=float, default=4.0)
    args = ap.parse_args()

    layout = v34.load_layout(args.layout)
    positions = v34.read_positions_tsv(args.positions, layout)
    edges = v34.graph_edges(layout)
    groups = build_groups(layout, edges)
    centers = np.array([positions[g].mean(axis=0) for g in groups], dtype=np.float64)
    sizes = np.array([g.shape[0] for g in groups], dtype=np.float64)
    meta_edges, edge_weights = group_graph(groups, edges)
    movable = sizes <= args.move_max_group_size
    new_centers = force_layout(
        centers,
        sizes,
        meta_edges,
        edge_weights,
        iterations=args.iterations,
        step=args.step,
        edge_len=args.edge_len,
        attract=args.attract,
        repel=args.repel,
        anchor=args.anchor,
        movable_mask=movable,
    )
    out = positions.copy()
    for gi, members in enumerate(groups):
        if not movable[gi]:
            continue
        delta = new_centers[gi] - centers[gi]
        out[members] += delta
    out = scale_to_bbox(out, args.bbox_target_b)
    v34.write_positions_tsv(args.out_tsv, layout, out)
    print(
        f"groups={len(groups)} metaEdges={meta_edges.shape[0]} "
        f"movedGroups={int(movable.sum())} wrote={args.out_tsv}",
        flush=True,
    )


if __name__ == "__main__":
    main()
