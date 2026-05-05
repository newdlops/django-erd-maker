#!/usr/bin/env python3
"""Pack the layout into a smaller bounding box.

Strategy: identify clusters (and bundles) whose centroids lie far from the
main mass center, try translating them inward by various strides; accept
iff bbox area drops AND carrier-grouped crossing count doesn't worsen by
more than --max-cross-uptick (default 10).

Output: positions TSV ready for C++ rerouter (`--positions-tsv`).
"""

import json
import sys
from collections import defaultdict
from pathlib import Path
import importlib.util


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "analyze", ROOT / "scripts/analyze-cross-carriers.py"
)
analyze_mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analyze_mod)
build_carrier_ids = analyze_mod.build_carrier_ids
routes_cross = analyze_mod.routes_cross


def cp_count_total(carriers, routes, edge_endpoints):
    cp_count = defaultdict(int)
    n = len(routes)
    for i in range(n):
        if len(routes[i]) < 2: continue
        si, ti = edge_endpoints[i]
        for j in range(i+1, n):
            if len(routes[j]) < 2: continue
            sj, tj = edge_endpoints[j]
            if si == sj or si == tj or ti == sj or ti == tj: continue
            ci, cj = carriers[i], carriers[j]
            if ci == cj: continue
            if not routes_cross(routes[i], routes[j]): continue
            key = (ci, cj) if ci <= cj else (cj, ci)
            cp_count[key] += 1
    return len(cp_count)


def compute_bbox_area(node_pos, sizes):
    xs_min = min(node_pos[m][0] - sizes[m][0]/2 for m in node_pos)
    xs_max = max(node_pos[m][0] + sizes[m][0]/2 for m in node_pos)
    ys_min = min(node_pos[m][1] - sizes[m][1]/2 for m in node_pos)
    ys_max = max(node_pos[m][1] + sizes[m][1]/2 for m in node_pos)
    return (xs_max - xs_min) * (ys_max - ys_min)


def main():
    if len(sys.argv) < 3:
        print("Usage: bbox-pack.py <input.json> <output.tsv> "
              "[--strides s1,s2,...] [--max-iter K] "
              "[--max-cross-uptick N] [--target-frac F] "
              "[--connected-only]")
        sys.exit(1)
    input_path = Path(sys.argv[1])
    output_tsv = Path(sys.argv[2])
    strides = [200, 800, 2000, 5000]
    max_iter = 30
    max_cross_uptick = 10
    top_k = 30
    i = 3
    while i < len(sys.argv):
        if sys.argv[i] == "--strides":
            strides = [float(s) for s in sys.argv[i+1].split(",")]; i += 2
        elif sys.argv[i] == "--max-iter":
            max_iter = int(sys.argv[i+1]); i += 2
        elif sys.argv[i] == "--max-cross-uptick":
            max_cross_uptick = int(sys.argv[i+1]); i += 2
        elif sys.argv[i] == "--top-k":
            top_k = int(sys.argv[i+1]); i += 2
        else:
            i += 1

    data = json.loads(input_path.read_text())
    nodes = data["nodes"]
    routed_edges = data["routedEdges"]
    leaf_bundles = data["engineMetadata"].get("leafBundles", [])
    cluster_by_model = {n["modelId"]: n.get("clusterId", "")
                       for n in nodes if n.get("clusterId")}

    # Identify connected nodes
    connected_set = set()
    for e in routed_edges:
        connected_set.add(e["sourceModelId"])
        connected_set.add(e["targetModelId"])

    cur_pos = {n["modelId"]: (n["position"]["x"], n["position"]["y"])
              for n in nodes}
    sizes = {n["modelId"]: (n["size"]["width"], n["size"]["height"])
            for n in nodes}

    # Bbox computed over CONNECTED nodes only (so isolated stash doesn't dominate)
    def bbox_area_connected():
        pos = {m: cur_pos[m] for m in connected_set if m in cur_pos}
        sz = {m: sizes[m] for m in pos}
        if not pos:
            return 0.0
        return compute_bbox_area(pos, sz)

    print(f"Loaded {input_path.name}: nodes={len(nodes)} edges={len(routed_edges)}")
    print(f"Connected nodes: {len(connected_set)}")

    # Compute cluster + bundle centroids
    cluster_members = defaultdict(list)
    for n in nodes:
        cid = n.get("clusterId")
        if cid:
            cluster_members[cid].append(n["modelId"])
    bundle_members_list = []
    for b in leaf_bundles:
        members = set([b["parentModelId"]])
        for l in b["leafModelIds"]: members.add(l)
        for r in b.get("sharedRootModelIds") or []: members.add(r)
        bundle_members_list.append(list(members))

    # Connected-graph centroid (median)
    cxs = [cur_pos[m][0] for m in connected_set if m in cur_pos]
    cys = [cur_pos[m][1] for m in connected_set if m in cur_pos]
    cxs.sort(); cys.sort()
    main_cx = cxs[len(cxs)//2]
    main_cy = cys[len(cys)//2]
    print(f"Main mass center (median): ({main_cx:.0f}, {main_cy:.0f})")

    # Compute initial cost
    carriers = build_carrier_ids(routed_edges, leaf_bundles, cluster_by_model)
    edge_endpoints = [(e["sourceModelId"], e["targetModelId"])
                     for e in routed_edges]
    routes = [[(p["x"], p["y"]) for p in (e.get("points") or [])]
             for e in routed_edges]
    base_cp = cp_count_total(carriers, routes, edge_endpoints)
    base_bbox = bbox_area_connected()
    print(f"Baseline: bbox_area={base_bbox/1e9:.2f}B, cp_count={base_cp}")

    # Target list: clusters + bundles, sorted by |centroid - main_center| desc
    targets = []
    for cid, members in cluster_members.items():
        cx = sum(cur_pos[m][0] for m in members) / len(members)
        cy = sum(cur_pos[m][1] for m in members) / len(members)
        dx = cx - main_cx
        dy = cy - main_cy
        dist = (dx*dx + dy*dy) ** 0.5
        targets.append(("cluster", cid, members, dist))
    for bi, members in enumerate(bundle_members_list):
        if not members: continue
        cx = sum(cur_pos[m][0] for m in members) / len(members)
        cy = sum(cur_pos[m][1] for m in members) / len(members)
        dx = cx - main_cx
        dy = cy - main_cy
        dist = (dx*dx + dy*dy) ** 0.5
        targets.append(("bundle", bi, members, dist))
    targets.sort(key=lambda t: t[3], reverse=True)
    targets = targets[:top_k]
    print(f"Top {len(targets)} targets ranked by distance from main center:")
    for kind, ident, members, dist in targets[:10]:
        print(f"  {kind}={ident} dist={dist:.0f} members={len(members)}")

    def apply_move(member_ids, dx, dy):
        for m in member_ids:
            x, y = cur_pos[m]
            cur_pos[m] = (x + dx, y + dy)

    def rebuild_routes(member_ids):
        member_set = set(member_ids)
        for ei, e in enumerate(routed_edges):
            s, t = edge_endpoints[ei]
            if s not in member_set and t not in member_set: continue
            sx, sy = cur_pos[s]
            tx, ty = cur_pos[t]
            routes[ei] = [(sx, sy), (tx, ty)]

    def snapshot_routes(member_ids):
        member_set = set(member_ids)
        return {ei: list(routes[ei])
                for ei, e in enumerate(routed_edges)
                if edge_endpoints[ei][0] in member_set
                or edge_endpoints[ei][1] in member_set}

    def restore_routes(snap):
        for ei, r in snap.items():
            routes[ei] = list(r)

    cur_bbox = base_bbox
    cur_cp = base_cp
    accepted = 0

    for it in range(max_iter):
        improved = False
        for kind, ident, members, _ in targets:
            # Direction toward main center, normalized
            cx = sum(cur_pos[m][0] for m in members) / len(members)
            cy = sum(cur_pos[m][1] for m in members) / len(members)
            ddx = main_cx - cx
            ddy = main_cy - cy
            dist = (ddx*ddx + ddy*ddy) ** 0.5
            if dist < 1e-3: continue
            ux, uy = ddx/dist, ddy/dist
            snap = snapshot_routes(members)
            best_combo = None  # (new_bbox, new_cp, dx, dy)
            for stride in strides:
                if stride > dist: continue  # don't overshoot
                dx, dy = ux*stride, uy*stride
                apply_move(members, dx, dy)
                rebuild_routes(members)
                new_bbox = bbox_area_connected()
                new_cp = cp_count_total(carriers, routes, edge_endpoints)
                apply_move(members, -dx, -dy)
                if (new_bbox < cur_bbox
                    and new_cp <= cur_cp + max_cross_uptick):
                    if best_combo is None or new_bbox < best_combo[0]:
                        best_combo = (new_bbox, new_cp, dx, dy)
            restore_routes(snap)
            if best_combo is not None:
                new_bbox, new_cp, dx, dy = best_combo
                apply_move(members, dx, dy)
                rebuild_routes(members)
                cur_bbox = new_bbox
                cur_cp = new_cp
                accepted += 1
                improved = True
                print(f"  [iter {it+1}] {kind}={ident} +({dx:.0f},{dy:.0f}) "
                      f"bbox={cur_bbox/1e9:.2f}B cp={cur_cp}")
        if not improved:
            print(f"  [iter {it+1}] no improving move, stop")
            break

    print(f"\nFinal: bbox {base_bbox/1e9:.2f}B → {cur_bbox/1e9:.2f}B "
          f"(Δ {(cur_bbox-base_bbox)/1e9:+.2f}B, "
          f"{(cur_bbox/base_bbox - 1) * 100:+.1f}%); "
          f"cp {base_cp} → {cur_cp}; "
          f"{accepted} moves accepted")

    with output_tsv.open("w") as f:
        f.write("modelId\tx\ty\n")
        for n in nodes:
            mid = n["modelId"]
            x, y = cur_pos[mid]
            f.write(f"{mid}\t{x:.3f}\t{y:.3f}\n")
    print(f"Wrote {output_tsv}")


if __name__ == "__main__":
    main()
