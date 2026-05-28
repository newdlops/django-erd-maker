#!/usr/bin/env python3
"""Pair-wise cluster compression for hub-bridge crossing reduction.

Hypothesis: worst edges live on bridges between hub clusters that are
geometrically far apart. Single-cluster local search can't escape this
because moving either hub alone increases other-direction crossings.
Moving BOTH hub clusters TOWARD EACH OTHER simultaneously shortens
their bridge without disturbing the relative geometry.

Procedure:
  1. Load layout + engineMetadata (clusterByModelId, topCrossEdges,
     leafBundles).
  2. Identify hot cluster PAIRS from topCrossEdges.
  3. For each round:
     a. For each hot pair (A, B):
        - Compute centroids cA, cB and direction d = (cB - cA)
        - Sweep compression fractions f ∈ [0.1, 0.2, 0.4, 0.6]
        - Move A members by  +f * d / 2
        - Move B members by  -f * d / 2  (toward each other)
        - Measure total crossings
     b. Pick best (pair, f) that reduces total cross; apply.
  4. Write final positions; caller re-routes via binary.

Usage:
  python scripts/erd-poc/topn_cluster_pair_compress.py \\
      --layout data/erd-poc/layouts/real-main.json \\
      --topn-pairs 8 --rounds 6 --out /tmp/pair-compress.tsv
"""

from __future__ import annotations

import argparse
import importlib.util
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


def cluster_centroid(positions: np.ndarray, members: list[int]) -> tuple[float, float]:
    arr = positions[members]
    return float(arr[:, 0].mean()), float(arr[:, 1].mean())


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--layout", type=Path,
                   default=ROOT / "data/erd-poc/layouts/real-main.json")
    p.add_argument("--topn-pairs", type=int, default=8,
                   help="Process this many hot cluster pairs per round")
    p.add_argument("--rounds", type=int, default=8)
    p.add_argument("--fractions", default="0.10,0.20,0.40,0.60",
                   help="Compression fractions to sweep (1.0 = full overlap)")
    p.add_argument("--allow-overlap", action="store_true",
                   help="Allow A/B cluster bboxes to overlap after compression")
    p.add_argument("--out", type=Path, required=True)
    args = p.parse_args()

    print(f"Loading {args.layout.name} ...", flush=True)
    layout = v34.load_layout(args.layout)
    nodes = layout["nodes"]
    edges = v34.graph_edges(layout)
    evaluator = v34.fce.FastCrossEval(edges, len(nodes))
    positions = v34.layout_positions(layout)

    # Cluster membership.
    mid_to_idx = {str(nd["modelId"]): i for i, nd in enumerate(nodes)}
    cluster_by_mid = {
        str(nd["modelId"]): str(nd.get("clusterId") or "") for nd in nodes
    }
    cluster_members: dict[str, list[int]] = {}
    for mid, cid in cluster_by_mid.items():
        if cid:
            cluster_members.setdefault(cid, []).append(mid_to_idx[mid])
    em = layout.get("engineMetadata", {}) or {}

    # Hot pairs from topCrossEdges if available; otherwise compute from
    # per-edge crossing counts on current positions.
    hot_pairs: list[tuple[str, str]] = []
    seen = set()
    edge_pairs_source: list[tuple[str, str]] = []
    if em.get("topCrossEdges"):
        for rec in em["topCrossEdges"]:
            edge_pairs_source.append(
                (str(rec.get("sourceModelId")), str(rec.get("targetModelId")))
            )
    else:
        # Compute top-N worst edges in Python from current positions.
        per_edge = np.zeros(evaluator.E, dtype=np.int64)
        flags = v34.crossing_flags_for_pairs(positions, evaluator)
        for k in np.where(flags)[0]:
            per_edge[evaluator.i[k]] += 1
            per_edge[evaluator.j[k]] += 1
        order = np.argsort(-per_edge)
        # Walk worst edges; collect endpoint modelIds.
        for eidx in order:
            if per_edge[eidx] == 0:
                break
            src_idx = int(edges[eidx, 0])
            tgt_idx = int(edges[eidx, 1])
            edge_pairs_source.append((
                str(nodes[src_idx]["modelId"]),
                str(nodes[tgt_idx]["modelId"]),
            ))
            if len(edge_pairs_source) >= 40:  # plenty for picking unique pairs
                break
    for src_mid, tgt_mid in edge_pairs_source:
        sa = cluster_by_mid.get(src_mid, "")
        sb = cluster_by_mid.get(tgt_mid, "")
        if not sa or not sb or sa == sb:
            continue
        key = tuple(sorted([sa, sb]))
        if key in seen:
            continue
        seen.add(key)
        hot_pairs.append(key)
    hot_pairs = hot_pairs[: args.topn_pairs]
    print(
        f"  nodes={len(nodes)} clusters={len(cluster_members)} hot_pairs={len(hot_pairs)}",
        flush=True,
    )
    for a, b in hot_pairs:
        ra = nodes[max(cluster_members[a], key=lambda i: len(cluster_members[a]))]["modelId"] if cluster_members[a] else "?"
        rb = nodes[max(cluster_members[b], key=lambda i: len(cluster_members[b]))]["modelId"] if cluster_members[b] else "?"
        print(f"    pair: {a}({ra}) ↔ {b}({rb}) | sizes {len(cluster_members[a])}/{len(cluster_members[b])}")

    fractions = [float(x) for x in args.fractions.split(",")]
    init_total = int(v34.crossing_flags_for_pairs(positions, evaluator).sum())
    print(f"  initial total_cross={init_total}", flush=True)

    accepted = 0
    start = time.time()
    for round_idx in range(args.rounds):
        baseline_total = int(v34.crossing_flags_for_pairs(positions, evaluator).sum())
        best = None  # (pair_idx, f, new_total)
        for pi, (a, b) in enumerate(hot_pairs):
            if not cluster_members[a] or not cluster_members[b]:
                continue
            mA = cluster_members[a]
            mB = cluster_members[b]
            cA = cluster_centroid(positions, mA)
            cB = cluster_centroid(positions, mB)
            dx = cB[0] - cA[0]
            dy = cB[1] - cA[1]
            origA = positions[mA].copy()
            origB = positions[mB].copy()
            for f in fractions:
                shift = f / 2.0
                positions[mA, 0] = origA[:, 0] + shift * dx
                positions[mA, 1] = origA[:, 1] + shift * dy
                positions[mB, 0] = origB[:, 0] - shift * dx
                positions[mB, 1] = origB[:, 1] - shift * dy
                new_total = int(v34.crossing_flags_for_pairs(positions, evaluator).sum())
                if new_total < baseline_total:
                    if best is None or new_total < best[2]:
                        best = (pi, f, new_total)
            positions[mA] = origA
            positions[mB] = origB
        if best is None:
            print(f"  round {round_idx + 1}: no improving pair (cross={baseline_total})",
                  flush=True)
            break
        pi, f, new_total = best
        a, b = hot_pairs[pi]
        mA = cluster_members[a]
        mB = cluster_members[b]
        cA = cluster_centroid(positions, mA)
        cB = cluster_centroid(positions, mB)
        dx = cB[0] - cA[0]
        dy = cB[1] - cA[1]
        shift = f / 2.0
        positions[mA, 0] += shift * dx
        positions[mA, 1] += shift * dy
        positions[mB, 0] -= shift * dx
        positions[mB, 1] -= shift * dy
        accepted += 1
        gain = baseline_total - new_total
        print(
            f"  round {round_idx + 1}: compressed pair {a}↔{b} "
            f"by f={f:.2f} (Δd≈{f * (dx**2 + dy**2)**0.5:.0f}) "
            f"cross {baseline_total}→{new_total} (gain={gain})",
            flush=True,
        )

    final_total = int(v34.crossing_flags_for_pairs(positions, evaluator).sum())
    elapsed = time.time() - start
    print(
        f"\nDone. accepted={accepted} elapsed={elapsed:.1f}s "
        f"final_cross={final_total} (Δ={init_total - final_total:+d})",
        flush=True,
    )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    v34.write_positions_tsv(args.out, layout, positions)
    print(f"Wrote positions to {args.out}")


if __name__ == "__main__":
    main()
