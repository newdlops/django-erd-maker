#!/usr/bin/env python3
"""Analyze top crossing carrier-pairs in a layout JSON.

Mirrors the C++ carrier-cross logic from main.cpp (line ~10720) at the
route level. Reports top-K (carrier_X, carrier_Y) pairs with the most
crossings, plus involved clusters / nodes — guides cluster-pair targeting
optimizations.

Usage:
  python scripts/analyze-cross-carriers.py /tmp/layout-best-1041.json [K=20]
"""

import json
import sys
from collections import defaultdict
from pathlib import Path


def proper_segment_intersection(p, q, r, s):
    """Returns True iff segments PQ and RS properly cross (no endpoint
    sharing). Mirrors C++ properSegmentIntersection semantics."""
    def sgn(x):
        return (x > 0) - (x < 0)
    o1 = sgn((q[0]-p[0])*(r[1]-p[1]) - (q[1]-p[1])*(r[0]-p[0]))
    o2 = sgn((q[0]-p[0])*(s[1]-p[1]) - (q[1]-p[1])*(s[0]-p[0]))
    o3 = sgn((s[0]-r[0])*(p[1]-r[1]) - (s[1]-r[1])*(p[0]-r[0]))
    o4 = sgn((s[0]-r[0])*(q[1]-r[1]) - (s[1]-r[1])*(q[0]-r[0]))
    return o1 != o2 and o3 != o4 and o1 != 0 and o3 != 0


def routes_cross(route_i, route_j):
    """Test if any segment of route_i crosses any segment of route_j."""
    for li in range(1, len(route_i)):
        for rj in range(1, len(route_j)):
            if proper_segment_intersection(
                route_i[li-1], route_i[li],
                route_j[rj-1], route_j[rj],
            ):
                return True
    return False


def build_carrier_ids(routed_edges, leaf_bundles, cluster_by_model):
    """Compute carrier ID per edge — mirrors C++ logic in main.cpp.
    Returns list of carrier strings, indexed parallel to routed_edges.
    """
    leaf_to_bundle_idx = {}
    for bi, b in enumerate(leaf_bundles):
        for leaf in b["leafModelIds"]:
            leaf_to_bundle_idx[leaf] = bi
    carriers = []
    for e in routed_edges:
        s = e["sourceModelId"]
        t = e["targetModelId"]
        # Bundle carrier
        sBI = leaf_to_bundle_idx.get(s)
        tBI = leaf_to_bundle_idx.get(t)
        cid = None
        if sBI is not None:
            bundle = leaf_bundles[sBI]
            roots = bundle.get("sharedRootModelIds") or [bundle["parentModelId"]]
            if t in roots:
                cid = f"B{sBI}|{t}"
        if cid is None and tBI is not None:
            bundle = leaf_bundles[tBI]
            roots = bundle.get("sharedRootModelIds") or [bundle["parentModelId"]]
            if s in roots:
                cid = f"B{tBI}|{s}"
        if cid is None:
            sC = cluster_by_model.get(s, "")
            tC = cluster_by_model.get(t, "")
            if sC and tC:
                if sC == tC:
                    cid = f"Cself|{sC}"
                else:
                    cid = f"C|{sC}|{tC}" if sC < tC else f"C|{tC}|{sC}"
            else:
                cid = e["edgeId"]
        carriers.append(cid)
    return carriers


def main():
    if len(sys.argv) < 2:
        print("Usage: analyze-cross-carriers.py <layout.json> [K=20]")
        sys.exit(1)
    layout_path = Path(sys.argv[1])
    K = int(sys.argv[2]) if len(sys.argv) > 2 else 20

    data = json.loads(layout_path.read_text())
    nodes = data["nodes"]
    routed_edges = data["routedEdges"]
    leaf_bundles = data["engineMetadata"].get("leafBundles", [])

    cluster_by_model = {n["modelId"]: n.get("clusterId", "")
                       for n in nodes if n.get("clusterId")}

    carriers = build_carrier_ids(routed_edges, leaf_bundles, cluster_by_model)
    print(f"Loaded {len(routed_edges)} edges, "
          f"{len(set(carriers))} distinct carriers")

    # Convert routes to point tuples
    routes = []
    for e in routed_edges:
        pts = e.get("points") or []
        routes.append([(p["x"], p["y"]) for p in pts])

    # Build edge endpoint map for shared-endpoint filter
    edge_endpoints = []
    for e in routed_edges:
        edge_endpoints.append((e["sourceModelId"], e["targetModelId"]))

    # Compute carrier-pair cross count
    cp_count = defaultdict(int)
    cp_to_edges = defaultdict(list)
    n_segment_cross = 0
    n = len(routed_edges)
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
            if not routes_cross(routes[i], routes[j]):
                continue
            n_segment_cross += 1
            key = (ci, cj) if ci <= cj else (cj, ci)
            cp_count[key] += 1
            cp_to_edges[key].append((i, j))

    print(f"Total segment crosses: {n_segment_cross}")
    print(f"Unique carrier-pairs with cross: {len(cp_count)} "
          f"(this is edgeCrossings)")

    # Sort by count desc
    items = sorted(cp_count.items(), key=lambda x: x[1], reverse=True)

    # Helper: extract clusters from carrier id
    def carrier_clusters(c):
        if c.startswith("Cself|"):
            return [c[6:]]
        if c.startswith("C|"):
            parts = c.split("|", 2)
            if len(parts) >= 3:
                return [parts[1], parts[2]]
        if c.startswith("B"):
            # Bundle — root is after the |
            parts = c.split("|", 1)
            root = parts[1] if len(parts) > 1 else ""
            return [f"<bundle:{c[1:].split('|')[0]}>{root}"]
        return [c]

    # Helper: cluster member count
    cluster_members = defaultdict(list)
    for n_ in nodes:
        cid = n_.get("clusterId")
        if cid:
            cluster_members[cid].append(n_["modelId"])

    print(f"\nTop {K} crossing carrier-pairs:")
    print(f"{'cnt':>4} {'cX':<35} {'cY':<35} clustersX/Y")
    print("-" * 100)
    for (cX, cY), cnt in items[:K]:
        clX = carrier_clusters(cX)
        clY = carrier_clusters(cY)
        # Show cluster sizes
        clX_str = ",".join(f"{c}({len(cluster_members.get(c, []))})"
                          for c in clX)
        clY_str = ",".join(f"{c}({len(cluster_members.get(c, []))})"
                          for c in clY)
        print(f"{cnt:>4} {cX[:35]:<35} {cY[:35]:<35} "
              f"{clX_str} | {clY_str}")

    # Aggregate: which clusters appear most across top-K pairs?
    cluster_appearance = defaultdict(int)
    for (cX, cY), cnt in items[:K * 5]:  # top 5K
        for c in carrier_clusters(cX) + carrier_clusters(cY):
            cluster_appearance[c] += cnt
    top_clusters = sorted(cluster_appearance.items(),
                         key=lambda x: x[1], reverse=True)[:15]
    print(f"\nTop 15 clusters by total cross-contribution (across top {K*5} pairs):")
    for c, score in top_clusters:
        members = cluster_members.get(c, [])
        print(f"  {c} (members={len(members)}, score={score})")


if __name__ == "__main__":
    main()
