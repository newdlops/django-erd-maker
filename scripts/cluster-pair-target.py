#!/usr/bin/env python3
"""Cluster-pair targeting post-pass.

Given a layout JSON, identify clusters (and bundles) that contribute most
to crossing carrier-pairs, then try translating each by various offsets to
reduce the carrier-grouped crossing count.

Cost = unique (carrier_X, carrier_Y) pairs with at least one route-segment
crossing (i.e., the user-facing edgeCrossings metric, computed at route
level).

Move strategy: 8-direction × 5 strides (small to large) per cluster/bundle.
Accept iff total carrier-grouped count drops.

Output: positions TSV ready for C++ rerouter (`--positions-tsv`).
"""

import json
import sys
from collections import defaultdict
from pathlib import Path
import importlib.util


# Reuse helpers from analyze-cross-carriers.py
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "analyze", ROOT / "scripts/analyze-cross-carriers.py"
)
analyze_mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analyze_mod)

proper_segment_intersection = analyze_mod.proper_segment_intersection
routes_cross = analyze_mod.routes_cross
build_carrier_ids = analyze_mod.build_carrier_ids


def cross_count_for_carrier_pair(carriers, routes, edge_endpoints,
                                  cluster_pair_set=None):
    """Compute carrier-grouped crossing count.

    If cluster_pair_set provided, only count pairs whose (cX, cY) is in
    the set — useful for incremental delta computation.
    Returns (total, cp_count) where cp_count[(cX,cY)] = segment cross count.
    """
    cp_count = defaultdict(int)
    n = len(routes)
    for i in range(n):
        if len(routes[i]) < 2:
            continue
        si, ti = edge_endpoints[i]
        for j in range(i+1, n):
            if len(routes[j]) < 2:
                continue
            sj, tj = edge_endpoints[j]
            if si == sj or si == tj or ti == sj or ti == tj:
                continue
            ci, cj = carriers[i], carriers[j]
            if ci == cj:
                continue
            key = (ci, cj) if ci <= cj else (cj, ci)
            if cluster_pair_set is not None and key not in cluster_pair_set:
                continue
            if not routes_cross(routes[i], routes[j]):
                continue
            cp_count[key] += 1
    return len(cp_count), cp_count


def main():
    if len(sys.argv) < 3:
        print("Usage: cluster-pair-target.py <input.json> <output.tsv> "
              "[--top-k N] [--strides s1,s2,...] [--max-iter K]")
        sys.exit(1)
    input_path = Path(sys.argv[1])
    output_tsv = Path(sys.argv[2])
    top_k = 30
    strides = [50.0, 150.0, 400.0, 900.0, 2000.0]
    max_iter = 50
    i = 3
    while i < len(sys.argv):
        if sys.argv[i] == "--top-k":
            top_k = int(sys.argv[i+1]); i += 2
        elif sys.argv[i] == "--strides":
            strides = [float(s) for s in sys.argv[i+1].split(",")]
            i += 2
        elif sys.argv[i] == "--max-iter":
            max_iter = int(sys.argv[i+1]); i += 2
        else:
            i += 1

    data = json.loads(input_path.read_text())
    nodes = data["nodes"]
    routed_edges = data["routedEdges"]
    leaf_bundles = data["engineMetadata"].get("leafBundles", [])
    cluster_by_model = {n["modelId"]: n.get("clusterId", "")
                       for n in nodes if n.get("clusterId")}

    print(f"Loaded {input_path.name}: nodes={len(nodes)} edges={len(routed_edges)}")

    # Build initial state
    carriers = build_carrier_ids(routed_edges, leaf_bundles, cluster_by_model)
    edge_endpoints = [(e["sourceModelId"], e["targetModelId"])
                     for e in routed_edges]
    routes = [[(p["x"], p["y"]) for p in (e.get("points") or [])]
             for e in routed_edges]

    # Map model id -> node index for fast pos update
    model_to_idx = {n["modelId"]: i for i, n in enumerate(nodes)}

    # Compute baseline carrier-grouped count
    cur_node_pos = {n["modelId"]: (n["position"]["x"], n["position"]["y"])
                   for n in nodes}
    baseline_total, _ = cross_count_for_carrier_pair(
        carriers, routes, edge_endpoints)
    print(f"Baseline carrier-grouped count: {baseline_total}")

    # Identify top-contributing clusters (across top-K pairs)
    _, cp_count = cross_count_for_carrier_pair(
        carriers, routes, edge_endpoints)
    items = sorted(cp_count.items(), key=lambda x: x[1], reverse=True)

    def carrier_clusters_or_bundle(c):
        """Return identifier for the clusters/bundle the carrier belongs to."""
        if c.startswith("Cself|"):
            return [("cluster", c[6:])]
        if c.startswith("C|"):
            parts = c.split("|", 2)
            if len(parts) >= 3:
                return [("cluster", parts[1]), ("cluster", parts[2])]
        if c.startswith("B"):
            parts = c.split("|", 1)
            bundle_idx_str = parts[0][1:]  # strip leading 'B'
            try:
                return [("bundle", int(bundle_idx_str))]
            except ValueError:
                return []
        return []

    contribution = defaultdict(int)
    for (cX, cY), cnt in items[:top_k * 5]:
        for kind_id in carrier_clusters_or_bundle(cX) \
                     + carrier_clusters_or_bundle(cY):
            contribution[kind_id] += cnt
    targets = sorted(contribution.items(), key=lambda x: x[1],
                    reverse=True)[:top_k]

    print(f"Top {top_k} translation targets (cluster or bundle):")
    for (kind, ident), score in targets[:10]:
        print(f"  {kind}={ident} score={score}")

    # Build cluster_id -> [model ids]
    cluster_members = defaultdict(list)
    for n in nodes:
        cid = n.get("clusterId")
        if cid:
            cluster_members[cid].append(n["modelId"])

    # Bundle index -> [model ids] (parent + leaves + sharedRoots)
    bundle_members = []
    for b in leaf_bundles:
        members = set()
        members.add(b["parentModelId"])
        for l in b["leafModelIds"]:
            members.add(l)
        for r in b.get("sharedRootModelIds") or []:
            members.add(r)
        bundle_members.append(list(members))

    def members_for(kind, ident):
        if kind == "cluster":
            return cluster_members.get(ident, [])
        if kind == "bundle":
            if 0 <= ident < len(bundle_members):
                return bundle_members[ident]
        return []

    # 8 directions
    DIRS = [(1, 0), (-1, 0), (0, 1), (0, -1),
            (1, 1), (-1, 1), (1, -1), (-1, -1)]

    def normalize(dx, dy):
        d = (dx*dx + dy*dy) ** 0.5
        return (dx/d, dy/d)
    DIRS = [normalize(dx, dy) for dx, dy in DIRS]

    def apply_move(member_ids, dx, dy):
        """Update cur_node_pos in place for member_ids by (dx, dy)."""
        for m in member_ids:
            x, y = cur_node_pos[m]
            cur_node_pos[m] = (x + dx, y + dy)

    def rebuild_routes_for_members(member_ids):
        """Rebuild straight-line routes for edges that have any member as
        endpoint. Other routes stay as-is."""
        member_set = set(member_ids)
        for ei, e in enumerate(routed_edges):
            s, t = edge_endpoints[ei]
            if s not in member_set and t not in member_set:
                continue
            sx, sy = cur_node_pos[s]
            tx, ty = cur_node_pos[t]
            routes[ei] = [(sx, sy), (tx, ty)]

    def snapshot_routes(member_ids):
        """Snapshot original routes for edges incident to any member."""
        member_set = set(member_ids)
        snap = {}
        for ei, e in enumerate(routed_edges):
            s, t = edge_endpoints[ei]
            if s in member_set or t in member_set:
                snap[ei] = list(routes[ei])
        return snap

    def restore_routes(snap):
        for ei, route in snap.items():
            routes[ei] = list(route)

    moves_applied = 0
    accepted = 0
    cur_total = baseline_total
    for it in range(max_iter):
        improved_this_round = False
        for (kind, ident), score in targets:
            members = members_for(kind, ident)
            if not members:
                continue
            snap = snapshot_routes(members)
            best_delta = 0
            best_offset = None
            for stride in strides:
                for ux, uy in DIRS:
                    dx, dy = ux * stride, uy * stride
                    apply_move(members, dx, dy)
                    rebuild_routes_for_members(members)
                    new_total, _ = cross_count_for_carrier_pair(
                        carriers, routes, edge_endpoints)
                    delta = new_total - cur_total
                    # Revert pos
                    apply_move(members, -dx, -dy)
                    if delta < best_delta:
                        best_delta = delta
                        best_offset = (dx, dy)
            # Restore original routes (was overwritten to straight-line by trial)
            restore_routes(snap)
            if best_offset is not None and best_delta < 0:
                dx, dy = best_offset
                apply_move(members, dx, dy)
                rebuild_routes_for_members(members)
                # Confirm
                cur_total, _ = cross_count_for_carrier_pair(
                    carriers, routes, edge_endpoints)
                accepted += 1
                improved_this_round = True
                print(f"  [iter {it+1}] move {kind}={ident} "
                      f"by ({dx:.0f},{dy:.0f}) → cp_count={cur_total} "
                      f"(Δ={best_delta:+d})")
            moves_applied += 1
        if not improved_this_round:
            print(f"  [iter {it+1}] no improving move, stopping")
            break

    print(f"\nFinal: {baseline_total} → {cur_total} "
          f"(Δ {cur_total - baseline_total:+d}, {accepted} moves accepted)")

    # Write positions TSV
    with output_tsv.open("w") as f:
        f.write("modelId\tx\ty\n")
        for n in nodes:
            mid = n["modelId"]
            x, y = cur_node_pos[mid]
            f.write(f"{mid}\t{x:.3f}\t{y:.3f}\n")
    print(f"Wrote {output_tsv}")


if __name__ == "__main__":
    main()
