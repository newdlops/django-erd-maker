#!/usr/bin/env python3
"""Optimize real node bundles against direct straight-edge crossings.

The bundle is a parameterization of *nodes*: every node has a learnable local
offset and every Louvain community has a learnable center.  Edges are never
grouped, replaced, hidden, or bent.  Exact crossing counts are used for
checkpoint selection; the differentiable loss only supplies coordinated moves
that can escape the flat basins of one-node discrete search.
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
import torch
import torch.nn.functional as F

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


def scope_graph(layout: dict, scope: str):
    global_edges = v34.graph_edges(layout)
    graph = nx.Graph()
    graph.add_nodes_from(range(len(layout["nodes"])))
    graph.add_edges_from((int(source), int(target)) for source, target in global_edges)
    giant = set(int(node) for node in max(nx.biconnected_components(graph), key=len))
    core_numbers = nx.core_number(graph.subgraph(giant))
    core3 = {node for node, value in core_numbers.items() if value >= 3}
    shell = giant - core3
    single_shell = {
        next(iter(component))
        for component in nx.connected_components(graph.subgraph(shell))
        if len(component) == 1
    }
    if scope == "core3":
        nodes = sorted(core3)
    elif scope == "core3-single":
        nodes = sorted(core3 | single_shell)
    elif scope == "giant":
        nodes = sorted(giant)
    else:
        nodes = list(range(len(layout["nodes"])))
    node_set = set(nodes)
    global_to_local = {node: index for index, node in enumerate(nodes)}
    edges = np.asarray(
        [
            (global_to_local[int(source)], global_to_local[int(target)])
            for source, target in global_edges
            if int(source) in node_set and int(target) in node_set
        ],
        dtype=np.int64,
    )
    local_graph = nx.Graph()
    local_graph.add_nodes_from(range(len(nodes)))
    local_graph.add_edges_from((int(source), int(target)) for source, target in edges)
    return nodes, edges, local_graph


def edge_pair_indices(edges: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    left, right = np.triu_indices(edges.shape[0], k=1)
    a = edges[left]
    b = edges[right]
    keep = (
        (a[:, 0] != b[:, 0])
        & (a[:, 0] != b[:, 1])
        & (a[:, 1] != b[:, 0])
        & (a[:, 1] != b[:, 1])
    )
    return left[keep].astype(np.int64), right[keep].astype(np.int64)


def normalized_positions(positions: np.ndarray) -> np.ndarray:
    result = positions.astype(np.float64, copy=True)
    result -= result.mean(axis=0)
    scale = float(np.sqrt(np.mean(np.sum(result * result, axis=1))))
    if scale > 1e-12:
        result /= scale
    return result


def make_groups(graph: nx.Graph, resolution: float, seed: int):
    communities = list(
        nx.community.louvain_communities(
            graph,
            weight=None,
            resolution=resolution,
            seed=seed,
        )
    )
    communities.sort(key=lambda members: (-len(members), min(members)))
    group_of = np.zeros(graph.number_of_nodes(), dtype=np.int64)
    for group_index, members in enumerate(communities):
        group_of[list(members)] = group_index
    return communities, group_of


def cross_probability(
    positions: torch.Tensor,
    edges: torch.Tensor,
    pair_left: torch.Tensor,
    pair_right: torch.Tensor,
    sharpness: float,
) -> torch.Tensor:
    first = edges[pair_left]
    second = edges[pair_right]
    a = positions[first[:, 0]]
    b = positions[first[:, 1]]
    c = positions[second[:, 0]]
    d = positions[second[:, 1]]

    ab = b - a
    cd = d - c
    ac = c - a
    ad = d - a
    ca = a - c
    cb = b - c
    epsilon = 1e-8

    def sine(left: torch.Tensor, right: torch.Tensor) -> torch.Tensor:
        cross = left[:, 0] * right[:, 1] - left[:, 1] * right[:, 0]
        denom = torch.linalg.vector_norm(left, dim=1) * torch.linalg.vector_norm(
            right, dim=1
        )
        return cross / denom.clamp_min(epsilon)

    side_c = sine(ab, ac)
    side_d = sine(ab, ad)
    side_a = sine(cd, ca)
    side_b = sine(cd, cb)

    opposite_first = (
        torch.sigmoid(sharpness * side_c) * torch.sigmoid(-sharpness * side_d)
        + torch.sigmoid(-sharpness * side_c) * torch.sigmoid(sharpness * side_d)
    )
    opposite_second = (
        torch.sigmoid(sharpness * side_a) * torch.sigmoid(-sharpness * side_b)
        + torch.sigmoid(-sharpness * side_a) * torch.sigmoid(sharpness * side_b)
    )
    return opposite_first * opposite_second


def write_positions(
    path: Path,
    layout: dict,
    scope_nodes: list[int],
    positions: np.ndarray,
) -> None:
    direct.write_scope_positions(path, layout, scope_nodes, positions)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--positions", type=Path, required=True)
    parser.add_argument("--out-tsv", type=Path, required=True)
    parser.add_argument(
        "--scope", choices=("core3", "core3-single", "giant", "all"), default="core3"
    )
    parser.add_argument("--steps", type=int, default=6000)
    parser.add_argument("--restarts", type=int, default=8)
    parser.add_argument("--eval-every", type=int, default=25)
    parser.add_argument("--report-every", type=int, default=250)
    parser.add_argument("--lr-center", type=float, default=0.025)
    parser.add_argument("--lr-offset", type=float, default=0.012)
    parser.add_argument("--sharpness-start", type=float, default=3.0)
    parser.add_argument("--sharpness-end", type=float, default=24.0)
    parser.add_argument("--noise", type=float, default=0.12)
    parser.add_argument("--resolution", type=float, default=0.7)
    parser.add_argument("--separation", type=float, default=0.07)
    parser.add_argument("--separation-weight", type=float, default=240.0)
    parser.add_argument("--edge-weight", type=float, default=0.08)
    parser.add_argument("--bundle-weight", type=float, default=0.015)
    parser.add_argument("--scale-weight", type=float, default=2.0)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--device", default="cpu")
    args = parser.parse_args()

    torch.set_num_threads(max(1, min(8, torch.get_num_threads())))
    layout = v34.load_layout(args.layout)
    scope_nodes, edges_np, graph = scope_graph(layout, args.scope)
    all_positions = v34.read_positions_tsv(args.positions, layout)
    initial = normalized_positions(all_positions[scope_nodes])
    evaluator = v34.fce.FastCrossEval(edges_np.astype(np.int32), len(scope_nodes))
    initial_cross = evaluator.count_crossings(initial)
    best_cross = initial_cross
    best_positions = initial.copy()

    pair_left_np, pair_right_np = edge_pair_indices(edges_np)
    communities, group_of_np = make_groups(graph, args.resolution, args.seed)
    device = torch.device(args.device)
    edges = torch.as_tensor(edges_np, dtype=torch.long, device=device)
    pair_left = torch.as_tensor(pair_left_np, dtype=torch.long, device=device)
    pair_right = torch.as_tensor(pair_right_np, dtype=torch.long, device=device)
    group_of = torch.as_tensor(group_of_np, dtype=torch.long, device=device)
    node_pair_left_np, node_pair_right_np = np.triu_indices(len(scope_nodes), k=1)
    node_pair_left = torch.as_tensor(node_pair_left_np, dtype=torch.long, device=device)
    node_pair_right = torch.as_tensor(node_pair_right_np, dtype=torch.long, device=device)
    started = time.time()
    print(
        f"start scope={args.scope} nodes={len(scope_nodes)} edges={len(edges_np)} "
        f"edgePairs={len(pair_left_np)} groups={len(communities)} "
        f"groupSizes={[len(group) for group in communities]} exact={initial_cross}",
        flush=True,
    )

    for restart in range(args.restarts):
        rng = np.random.default_rng(args.seed + restart * 104729)
        if restart == 0:
            seed_positions = best_positions.copy()
        else:
            seed_positions = best_positions + rng.normal(
                0.0, args.noise * (1.0 + 0.2 * restart), best_positions.shape
            )
            angle = float(rng.uniform(-math.pi, math.pi))
            rotation = np.asarray(
                [[math.cos(angle), -math.sin(angle)], [math.sin(angle), math.cos(angle)]]
            )
            seed_positions = seed_positions @ rotation.T
        seed_positions = normalized_positions(seed_positions)

        group_centers_np = np.zeros((len(communities), 2), dtype=np.float64)
        for group_index, members in enumerate(communities):
            group_centers_np[group_index] = seed_positions[list(members)].mean(axis=0)
        offsets_np = seed_positions - group_centers_np[group_of_np]
        centers = torch.nn.Parameter(
            torch.as_tensor(group_centers_np, dtype=torch.float32, device=device)
        )
        offsets = torch.nn.Parameter(
            torch.as_tensor(offsets_np, dtype=torch.float32, device=device)
        )
        optimizer = torch.optim.Adam(
            [
                {"params": [centers], "lr": args.lr_center},
                {"params": [offsets], "lr": args.lr_offset},
            ]
        )
        restart_best = evaluator.count_crossings(seed_positions)
        last_exact = restart_best

        for step in range(1, args.steps + 1):
            progress = (step - 1) / max(1, args.steps - 1)
            sharpness = args.sharpness_start * math.pow(
                args.sharpness_end / args.sharpness_start, progress
            )
            positions = centers[group_of] + offsets
            probabilities = cross_probability(
                positions, edges, pair_left, pair_right, sharpness
            )
            cross_loss = probabilities.sum()

            differences = positions[node_pair_left] - positions[node_pair_right]
            distance_sq = torch.sum(differences * differences, dim=1)
            separation_loss = torch.square(
                F.relu(args.separation * args.separation - distance_sq)
            ).sum()
            edge_delta = positions[edges[:, 0]] - positions[edges[:, 1]]
            edge_lengths = torch.linalg.vector_norm(edge_delta, dim=1)
            desired_edge = 0.34
            edge_loss = torch.square(edge_lengths - desired_edge).mean()
            bundle_loss = torch.square(offsets).mean()
            center_loss = torch.square(positions.mean(dim=0)).sum()
            radius = torch.mean(torch.sum(positions * positions, dim=1))
            scale_loss = torch.square(radius - 1.0)
            loss = (
                cross_loss
                + args.separation_weight * separation_loss
                + args.edge_weight * edge_loss
                + args.bundle_weight * bundle_loss
                + args.scale_weight * (center_loss + scale_loss)
            )
            optimizer.zero_grad(set_to_none=True)
            loss.backward()
            torch.nn.utils.clip_grad_norm_([centers, offsets], max_norm=100.0)
            optimizer.step()

            if step % args.eval_every == 0 or step == args.steps:
                candidate = (centers[group_of] + offsets).detach().cpu().numpy()
                exact = evaluator.count_crossings(candidate)
                last_exact = exact
                restart_best = min(restart_best, exact)
                if exact < best_cross:
                    best_cross = exact
                    best_positions = normalized_positions(candidate)
                    write_positions(args.out_tsv, layout, scope_nodes, best_positions)
                    print(
                        f"best restart={restart} step={step} exact={exact} "
                        f"soft={float(cross_loss.detach()):.2f} "
                        f"elapsed={time.time() - started:.1f}s",
                        flush=True,
                    )
            if step % args.report_every == 0:
                print(
                    f"progress restart={restart + 1}/{args.restarts} "
                    f"step={step}/{args.steps} restartBest={restart_best} "
                    f"currentExact={last_exact} globalBest={best_cross} "
                    f"soft={float(cross_loss.detach()):.2f} "
                    f"sharp={sharpness:.2f} elapsed={time.time() - started:.1f}s",
                    flush=True,
                )

    write_positions(args.out_tsv, layout, scope_nodes, best_positions)
    print(
        f"done initial={initial_cross} best={best_cross} out={args.out_tsv} "
        f"elapsed={time.time() - started:.1f}s",
        flush=True,
    )


if __name__ == "__main__":
    main()
