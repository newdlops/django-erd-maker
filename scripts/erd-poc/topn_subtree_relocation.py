#!/usr/bin/env python3
"""Coordinated multi-node moves for worst-edge hub relocation.

Builds on top-N edge analysis but moves an entire CLUSTER (hub +
members) as a rigid block instead of individual nodes. The hypothesis
is that worst edges live on bridges between hub clusters; relocating
a whole hub cluster preserves intra-cluster geometry while shortening
or rearranging the bridge.

Constraints:
  * Translate entire louvain cluster as a rigid block (positions
    relative to cluster centroid preserved).
  * Only consider clusters whose root participates in a top-N
    crossing edge.
  * Skip any cluster that contains bundle parents (since the bundle
    bbox is anchored separately and would visually drift).

Procedure:
  1. Load layout + engineMetadata.clusterByModelId + leafBundles.
  2. Identify "hot clusters" from topCrossEdges endpoints.
  3. For N rounds:
     a. For each hot cluster, sweep (radius × angle) translations.
     b. Pick the (cluster, dx, dy) that lowers total crossings most.
     c. Apply translation; if no improvement, stop.
  4. Write final positions TSV; caller re-routes via binary.

Usage:
  python scripts/erd-poc/topn_subtree_relocation.py \\
      --layout data/erd-poc/layouts/real-main.json \\
      --topn-clusters 6 --rounds 6 --out /tmp/subtree.tsv
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


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--layout", type=Path,
                   default=ROOT / "data/erd-poc/layouts/real-main.json")
    p.add_argument("--topn-clusters", type=int, default=6,
                   help="Process this many hot clusters per round")
    p.add_argument("--rounds", type=int, default=6)
    p.add_argument("--radii", default="200,600,1500,3500",
                   help="Translation radii to sweep")
    p.add_argument("--angles", type=int, default=12)
    p.add_argument("--skip-bundle-clusters", action="store_true",
                   default=False,
                   help="Skip clusters whose root is a bundle parent (default off)")
    p.add_argument("--out", type=Path, required=True)
    args = p.parse_args()

    print(f"Loading {args.layout.name} ...", flush=True)
    layout = v34.load_layout(args.layout)
    nodes = layout["nodes"]
    edges = v34.graph_edges(layout)
    evaluator = v34.fce.FastCrossEval(edges, len(nodes))
    positions = v34.layout_positions(layout)

    # Build node-id maps.
    mid_to_idx = {str(nd["modelId"]): i for i, nd in enumerate(nodes)}
    cluster_by_mid = {
        str(nd["modelId"]): str(nd.get("clusterId") or "")
        for nd in nodes
    }
    # Cluster → set of node indices.
    cluster_members: dict[str, list[int]] = {}
    for mid, cid in cluster_by_mid.items():
        if not cid:
            continue
        cluster_members.setdefault(cid, []).append(mid_to_idx[mid])
    print(f"  nodes={len(nodes)} clusters={len(cluster_members)}", flush=True)

    # Bundle parents (don't move clusters whose root is a bundle parent —
    # the bundle bbox would drift relative to the rest of the layout).
    em = layout.get("engineMetadata", {}) or {}
    bundle_parents = {
        str(b.get("parentModelId", ""))
        for b in (em.get("leafBundles", []) or [])
    }
    # Cluster id → root modelId. The root is the highest-degree node in
    # the cluster (matching the OGDF binary's louvain root selection).
    degree = np.bincount(edges.flatten(), minlength=len(nodes))
    cluster_root: dict[str, str] = {}
    for cid, members in cluster_members.items():
        best = max(members, key=lambda i: degree[i])
        cluster_root[cid] = str(nodes[best]["modelId"])

    # Find hot clusters from the engineMetadata.topCrossEdges if present;
    # otherwise compute from the current positions.
    hot_cluster_ids: list[str] = []
    if em.get("topCrossEdges"):
        for rec in em["topCrossEdges"]:
            for mid in (rec.get("sourceModelId"), rec.get("targetModelId")):
                cid = cluster_by_mid.get(str(mid), "")
                if cid and cid not in hot_cluster_ids:
                    hot_cluster_ids.append(cid)
    else:
        # Fall back: order clusters by total incident worst-edge crossings.
        per_edge = np.zeros(evaluator.E, dtype=np.int64)
        flags = v34.crossing_flags_for_pairs(positions, evaluator)
        for k in np.where(flags)[0]:
            per_edge[evaluator.i[k]] += 1
            per_edge[evaluator.j[k]] += 1
        order = np.argsort(-per_edge)
        for eidx in order[:20]:
            for j in range(2):
                node_idx = int(edges[eidx, j])
                cid = cluster_by_mid.get(str(nodes[node_idx]["modelId"]), "")
                if cid and cid not in hot_cluster_ids:
                    hot_cluster_ids.append(cid)
    # Skip clusters whose root is a bundle parent.
    if args.skip_bundle_clusters:
        hot_cluster_ids = [
            cid for cid in hot_cluster_ids
            if cluster_root.get(cid, "") not in bundle_parents
        ]
    hot_cluster_ids = hot_cluster_ids[: args.topn_clusters]
    print(
        f"  hot clusters ({len(hot_cluster_ids)}): "
        + ", ".join(f"{cid}({cluster_root.get(cid, '?')})" for cid in hot_cluster_ids),
        flush=True,
    )

    radii = [float(x) for x in args.radii.split(",")]
    angles = np.linspace(0.0, 2.0 * np.pi, args.angles, endpoint=False)

    init_total = int(v34.crossing_flags_for_pairs(positions, evaluator).sum())
    print(f"  initial total_cross={init_total}", flush=True)

    accepted = 0
    start = time.time()
    for round_idx in range(args.rounds):
        baseline_total = int(v34.crossing_flags_for_pairs(positions, evaluator).sum())
        best = None  # (cid, dx, dy, new_total)
        for cid in hot_cluster_ids:
            members = cluster_members[cid]
            if not members:
                continue
            orig = positions[members].copy()
            for r in radii:
                for theta in angles:
                    dx = r * np.cos(theta)
                    dy = r * np.sin(theta)
                    positions[members, 0] = orig[:, 0] + dx
                    positions[members, 1] = orig[:, 1] + dy
                    new_total = int(v34.crossing_flags_for_pairs(positions, evaluator).sum())
                    if new_total < baseline_total:
                        if best is None or new_total < best[3]:
                            best = (cid, dx, dy, new_total)
            positions[members] = orig
        if best is None:
            print(f"  round {round_idx + 1}: no improving cluster move (cross={baseline_total})",
                  flush=True)
            break
        cid, dx, dy, new_total = best
        members = cluster_members[cid]
        positions[members, 0] += dx
        positions[members, 1] += dy
        accepted += 1
        gain = baseline_total - new_total
        print(
            f"  round {round_idx + 1}: moved cluster {cid} "
            f"(root={cluster_root.get(cid)}, members={len(members)}) "
            f"by ({dx:+.0f},{dy:+.0f}) cross {baseline_total}→{new_total} (gain={gain})",
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
