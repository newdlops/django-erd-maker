#!/usr/bin/env python3
"""Search placements for the ERD's small 3-core using real nodes/edges only."""

from __future__ import annotations

import argparse
import importlib.util
import random
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


def normalized_positions(raw: dict[int, np.ndarray], nodes: list[int]) -> np.ndarray:
    positions = np.asarray([raw[node] for node in nodes], dtype=np.float64)
    positions -= positions.mean(axis=0)
    scale = float(np.std(positions))
    if scale > 1e-12:
        positions /= scale
    return positions


def greedy_planar_backbone(
    graph: nx.Graph,
    seed: int,
    strategy: str,
) -> nx.Graph:
    rng = random.Random(seed)
    degree = dict(graph.degree())
    edges = list(graph.edges())
    if strategy == "random":
        rng.shuffle(edges)
    elif strategy == "degree":
        tie = {tuple(sorted(edge)): rng.random() for edge in edges}
        edges.sort(
            key=lambda edge: (
                max(degree[edge[0]], degree[edge[1]]),
                degree[edge[0]] + degree[edge[1]],
                tie[tuple(sorted(edge))],
            ),
            reverse=True,
        )
    elif strategy == "low-degree":
        tie = {tuple(sorted(edge)): rng.random() for edge in edges}
        edges.sort(
            key=lambda edge: (
                max(degree[edge[0]], degree[edge[1]]),
                degree[edge[0]] + degree[edge[1]],
                tie[tuple(sorted(edge))],
            ),
        )
    backbone = nx.Graph()
    backbone.add_nodes_from(graph)
    for source, target in edges:
        backbone.add_edge(source, target)
        if not nx.check_planarity(backbone, counterexample=False)[0]:
            backbone.remove_edge(source, target)
    return backbone


def planar_backbone_positions(graph: nx.Graph, seed: int, strategy: str):
    backbone = greedy_planar_backbone(graph, seed, strategy)
    planar, embedding = nx.check_planarity(backbone, counterexample=False)
    assert planar
    raw = nx.combinatorial_embedding_to_pos(embedding, fully_triangulate=True)
    return raw, backbone.number_of_edges()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--out-npz", type=Path, required=True)
    parser.add_argument("--out-tsv", type=Path, default=None)
    parser.add_argument("--seeds", type=int, default=64)
    parser.add_argument("--iterations", type=int, default=1200)
    parser.add_argument("--include-shell-links", action="store_true")
    parser.add_argument(
        "--scope",
        choices=("core3", "core3-single"),
        default="core3",
    )
    args = parser.parse_args()

    layout = v34.load_layout(args.layout)
    edges = v34.graph_edges(layout)
    graph = nx.Graph()
    graph.add_nodes_from(range(len(layout["nodes"])))
    graph.add_edges_from((int(source), int(target)) for source, target in edges)
    giant = set(max(nx.biconnected_components(graph), key=len))
    giant_graph = graph.subgraph(giant).copy()
    core_numbers = nx.core_number(giant_graph)
    core3 = {node for node, value in core_numbers.items() if value >= 3}
    if args.scope == "core3-single":
        shell = giant - core3
        single_shell_nodes = {
            next(iter(component))
            for component in nx.connected_components(giant_graph.subgraph(shell))
            if len(component) == 1
        }
        core_nodes = sorted(core3 | single_shell_nodes)
    else:
        core_nodes = sorted(core3)
    core_graph_global = giant_graph.subgraph(core_nodes).copy()
    global_to_local = {node: index for index, node in enumerate(core_nodes)}
    core_graph = nx.relabel_nodes(core_graph_global, global_to_local, copy=True)
    if args.include_shell_links:
        shell_nodes = set(giant_graph) - core3
        for component in nx.connected_components(giant_graph.subgraph(shell_nodes)):
            attachments = sorted({
                neighbor
                for node in component
                for neighbor in giant_graph.neighbors(node)
                if neighbor in global_to_local
            })
            if len(attachments) == 2:
                attachment_pairs = [(attachments[0], attachments[1])]
            elif len(attachments) > 2:
                attachment_metric = nx.Graph()
                attachment_metric.add_nodes_from(attachments)
                for left_index, left in enumerate(attachments):
                    for right in attachments[left_index + 1 :]:
                        attachment_metric.add_edge(
                            left,
                            right,
                            weight=nx.shortest_path_length(
                                giant_graph, left, right
                            ),
                        )
                attachment_pairs = list(nx.minimum_spanning_edges(
                    attachment_metric,
                    data=False,
                ))
            else:
                attachment_pairs = []
            for source_global, target_global in attachment_pairs:
                source = global_to_local[source_global]
                target = global_to_local[target_global]
                if core_graph.has_edge(source, target):
                    core_graph[source][target]["weight"] = (
                        core_graph[source][target].get("weight", 1.0) + 1.0
                    )
                else:
                    core_graph.add_edge(source, target, weight=1.0)
    local_nodes = list(range(len(core_nodes)))
    core_edges = np.asarray(list(core_graph.edges()), dtype=np.int32)
    evaluator = v34.fce.FastCrossEval(core_edges, len(core_nodes))
    old_global = v34.layout_positions(layout)
    current = old_global[core_nodes]
    current_cross = evaluator.count_crossings(current)
    print(
        f"core nodes={len(core_nodes)} edges={core_edges.shape[0]} "
        f"currentCross={current_cross}",
        flush=True,
    )

    best_name = "current"
    best_positions = current.copy()
    best_cross = current_cross
    started = time.time()

    def consider(name: str, candidate: np.ndarray) -> None:
        nonlocal best_cross, best_name, best_positions
        cross = evaluator.count_crossings(candidate)
        if cross < best_cross:
            best_cross = cross
            best_name = name
            best_positions = candidate.copy()
            print(
                f"best name={name} cross={cross} elapsed={time.time() - started:.1f}s",
                flush=True,
            )

    consider(
        "spectral",
        normalized_positions(nx.spectral_layout(core_graph, dim=2), local_nodes),
    )
    try:
        kamada = nx.kamada_kawai_layout(
            core_graph,
            pos={index: current[index] for index in local_nodes},
            dim=2,
        )
    except ModuleNotFoundError:
        kamada = None
    if kamada is not None:
        consider(
            "kamada-current",
            normalized_positions(kamada, local_nodes),
        )
    for seed in range(args.seeds):
        for k_factor in (0.6, 1.0, 1.6, 2.4):
            k = k_factor / np.sqrt(len(core_nodes))
            spring = nx.spring_layout(
                core_graph,
                seed=seed,
                k=k,
                iterations=args.iterations,
                threshold=1e-6,
                weight="weight" if args.include_shell_links else None,
            )
            consider(
                f"spring-s{seed}-k{k_factor:g}",
                normalized_positions(spring, local_nodes),
            )
        if seed < max(8, args.seeds // 4):
            force = nx.forceatlas2_layout(
                core_graph,
                pos=None,
                max_iter=args.iterations,
                seed=seed,
                scaling_ratio=2.0,
                gravity=1.0,
                distributed_action=True,
                node_mass=None,
                node_size=None,
                weight="weight" if args.include_shell_links else None,
            )
            consider(
                f"forceatlas2-s{seed}",
                normalized_positions(force, local_nodes),
            )
            arf = nx.arf_layout(
                core_graph,
                seed=seed,
                max_iter=args.iterations,
                etol=1e-6,
            )
            consider(
                f"arf-s{seed}",
                normalized_positions(arf, local_nodes),
            )
        for strategy in ("random", "degree", "low-degree"):
            raw, kept = planar_backbone_positions(core_graph, seed, strategy)
            consider(
                f"planar-{strategy}-s{seed}-kept{kept}",
                normalized_positions(raw, local_nodes),
            )

    args.out_npz.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        args.out_npz,
        core_nodes=np.asarray(core_nodes, dtype=np.int32),
        positions=best_positions,
        crossings=np.asarray([best_cross], dtype=np.int32),
        name=np.asarray([best_name]),
    )
    if args.out_tsv is not None:
        args.out_tsv.parent.mkdir(parents=True, exist_ok=True)
        model_ids = [str(node["modelId"]) for node in layout["nodes"]]
        args.out_tsv.write_text(
            "".join(
                f"{model_ids[global_node]}\t{best_positions[local_index, 0] * 1200:.6f}"
                f"\t{best_positions[local_index, 1] * 1200:.6f}\n"
                for local_index, global_node in enumerate(core_nodes)
            ),
            encoding="utf-8",
        )
    print(
        f"done best={best_name} cross={best_cross} out={args.out_npz} "
        f"elapsed={time.time() - started:.1f}s"
    )


if __name__ == "__main__":
    main()
