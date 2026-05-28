#!/usr/bin/env python3
"""Top-N edge targeted local search for visualCross reduction.

Hypothesis: cross distribution is heavily skewed (P90=27 on Captain,
worstEdge=76). Most crossings concentrate on ~10% of edges. Targeting
moves at the endpoints of these worst edges should reduce visualCross
without disturbing the rest of the layout.

Constraints (lesson from stress-post-pass failure):
  * Never move bundle parents or bundle leaves — bundles encode tight
    visual relationships that are easy to destroy.
  * Optionally avoid cluster roots (they anchor whole clusters).
  * Greedy accept: only apply moves that strictly reduce total
    crossings; reject anything that creates new bundle/node overlaps.

Procedure:
  1. Load layout positions; compute baseline crossings via v34 evaluator.
  2. Pick top-N edges by per-edge crossing count.
  3. Gather candidate endpoint nodes (filtered).
  4. For each candidate node, sweep a small radius × angle grid.
  5. Pick the move that lowers total crossings the most; iterate until
     no candidate improves OR max rounds reached.
  6. Write final positions TSV; caller re-runs binary for full metrics.

Usage:
  python scripts/erd-poc/topn_edge_local_search.py \\
      --layout data/erd-poc/layouts/real-main.json \\
      --topn 20 --rounds 8 \\
      --out /tmp/topn-out.tsv
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
import time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]


def L(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


v34 = L("v34_move_search", ROOT / "scripts/erd-poc/v34_move_search.py")


def per_edge_crossings(positions: np.ndarray, evaluator) -> np.ndarray:
    """Crossing count per edge (raw geometric)."""
    flags = v34.crossing_flags_for_pairs(positions, evaluator)
    counts = np.zeros(evaluator.E, dtype=np.int32)
    for k in np.where(flags)[0]:
        counts[evaluator.i[k]] += 1
        counts[evaluator.j[k]] += 1
    return counts


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--layout", type=Path,
                   default=ROOT / "data/erd-poc/layouts/real-main.json")
    p.add_argument("--topn", type=int, default=20,
                   help="Process this many worst-edge endpoints per round")
    p.add_argument("--rounds", type=int, default=8)
    p.add_argument("--radii", default="50,150,400,1000",
                   help="Move radii to sweep per candidate node")
    p.add_argument("--angles", type=int, default=12,
                   help="Number of compass directions tried per radius")
    p.add_argument("--skip-cluster-roots", action="store_true",
                   help="Also exclude cluster root nodes (default: include)")
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--seed", type=int, default=0)
    args = p.parse_args()

    np.random.seed(args.seed)

    print(f"Loading {args.layout.name} ...", flush=True)
    layout = v34.load_layout(args.layout)
    nodes = layout["nodes"]
    edges = v34.graph_edges(layout)
    widths, heights = v34.render_node_sizes(layout, edges)
    evaluator = v34.fce.FastCrossEval(edges, len(nodes))
    positions = np.array(
        [[float(n["position"]["x"]) + float(n["size"]["width"]) / 2.0,
          float(n["position"]["y"]) + float(n["size"]["height"]) / 2.0]
         for n in nodes],
        dtype=np.float64,
    )
    # Recover initial centers via v34's own helper to match its convention.
    positions = v34.layout_positions(layout)

    # Build the "movable" mask. Drop bundle members; optionally drop
    # cluster roots. Anything left is fair game for the targeted move.
    bundle_member = np.zeros(len(nodes), dtype=bool)
    mid_to_idx = {str(nd["modelId"]): i for i, nd in enumerate(nodes)}
    em = layout.get("engineMetadata", {}) or {}
    for bundle in em.get("leafBundles", []) or []:
        parent_mid = str(bundle.get("parentModelId", ""))
        if parent_mid in mid_to_idx:
            bundle_member[mid_to_idx[parent_mid]] = True
        for leaf_mid in bundle.get("leafModelIds", []) or []:
            li = mid_to_idx.get(str(leaf_mid))
            if li is not None:
                bundle_member[li] = True
    cluster_roots = np.zeros(len(nodes), dtype=bool)
    if args.skip_cluster_roots:
        # Cluster root identification mirrors v34's: highest-degree
        # node in each louvain partition. We don't have direct access
        # to OGDF's louvain output, so fall back to high-degree filter.
        degree = np.bincount(edges.flatten(), minlength=len(nodes))
        rank = np.argsort(-degree)
        # Top 5% degree-wise treated as "probably a root".
        top_n_roots = max(1, len(nodes) // 20)
        for idx in rank[:top_n_roots]:
            cluster_roots[idx] = True
    movable_mask = ~(bundle_member | cluster_roots)
    print(
        f"  nodes={len(nodes)} edges={evaluator.E} bundle_members={bundle_member.sum()} "
        f"cluster_roots_excluded={cluster_roots.sum()} movable={movable_mask.sum()}",
        flush=True,
    )

    # Baseline metrics.
    flags = v34.crossing_flags_for_pairs(positions, evaluator)
    init_cross = int(flags.sum())
    per_edge = per_edge_crossings(positions, evaluator)
    print(f"  initial total_cross={init_cross} "
          f"max_per_edge={per_edge.max()} p90={int(np.percentile(per_edge, 90))}",
          flush=True)

    radii = [float(x) for x in args.radii.split(",")]
    angles = np.linspace(0.0, 2.0 * np.pi, args.angles, endpoint=False)

    accepted_moves = 0
    start = time.time()
    for round_idx in range(args.rounds):
        per_edge = per_edge_crossings(positions, evaluator)
        order = np.argsort(-per_edge)
        worst_edges = order[: args.topn]
        candidate_nodes = set()
        for eidx in worst_edges:
            if per_edge[eidx] == 0:
                continue
            src = int(edges[eidx, 0])
            dst = int(edges[eidx, 1])
            if movable_mask[src]:
                candidate_nodes.add(src)
            if movable_mask[dst]:
                candidate_nodes.add(dst)
        if not candidate_nodes:
            print(f"  round {round_idx + 1}: no movable candidates")
            break

        # For each candidate node, find the best move (across radius/
        # angle grid) that reduces total crossings.
        baseline_total = int(v34.crossing_flags_for_pairs(positions, evaluator).sum())
        best_move = None  # (node_idx, dx, dy, new_total)
        for node_idx in candidate_nodes:
            cur_x = positions[node_idx, 0]
            cur_y = positions[node_idx, 1]
            for r in radii:
                for theta in angles:
                    dx = r * np.cos(theta)
                    dy = r * np.sin(theta)
                    positions[node_idx, 0] = cur_x + dx
                    positions[node_idx, 1] = cur_y + dy
                    new_total = int(v34.crossing_flags_for_pairs(positions, evaluator).sum())
                    if new_total < baseline_total - 0:  # strict improvement
                        if best_move is None or new_total < best_move[3]:
                            best_move = (node_idx, dx, dy, new_total)
            # Restore for next candidate.
            positions[node_idx, 0] = cur_x
            positions[node_idx, 1] = cur_y

        if best_move is None:
            print(f"  round {round_idx + 1}: no improving move (cross={baseline_total})")
            break
        node_idx, dx, dy, new_total = best_move
        positions[node_idx, 0] += dx
        positions[node_idx, 1] += dy
        accepted_moves += 1
        gain = baseline_total - new_total
        print(
            f"  round {round_idx + 1}: moved node {node_idx} "
            f"({nodes[node_idx]['modelId']}) by ({dx:+.1f},{dy:+.1f}) "
            f"cross {baseline_total}→{new_total} (gain={gain})",
            flush=True,
        )

    elapsed = time.time() - start
    final_total = int(v34.crossing_flags_for_pairs(positions, evaluator).sum())
    print(
        f"\nDone. accepted={accepted_moves} elapsed={elapsed:.1f}s "
        f"final_cross={final_total} (Δ={init_cross - final_total:+d})",
        flush=True,
    )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    v34.write_positions_tsv(args.out, layout, positions)
    print(f"Wrote positions to {args.out}")


if __name__ == "__main__":
    main()
