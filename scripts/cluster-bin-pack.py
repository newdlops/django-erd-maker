#!/usr/bin/env python3
"""Cluster-rigid bin packing.

Treats each Louvain cluster as a rigid block (preserving relative
member positions) and packs blocks into a compact rectangle using a
shelf/skyline heuristic. Hubs (non-cluster nodes) are packed as
singleton blocks. Bundles (parent + leaves) are kept together.

Result:
- Cluster INTERNAL geometry untouched (cluster cohesion preserved).
- Inter-cluster gaps minimized (just `--gap` between block edges).
- Massive bbox reduction without disturbing crossings within clusters.

Usage:
  python scripts/cluster-bin-pack.py <input.json> <output.tsv> \
      [--gap 60] [--target-aspect 1.0]
"""
import json, sys, math
from pathlib import Path
from collections import defaultdict


def main():
    if len(sys.argv) < 3:
        print("Usage: cluster-bin-pack.py <input.json> <output.tsv> "
              "[--gap G] [--target-aspect A]")
        sys.exit(1)
    inp = Path(sys.argv[1])
    out_tsv = Path(sys.argv[2])
    gap = 80.0
    target_aspect = 1.0
    i = 3
    while i < len(sys.argv):
        if sys.argv[i] == "--gap":
            gap = float(sys.argv[i+1]); i += 2
        elif sys.argv[i] == "--target-aspect":
            target_aspect = float(sys.argv[i+1]); i += 2
        else:
            i += 1

    data = json.loads(inp.read_text())
    nodes = data["nodes"]
    bundles = data["engineMetadata"].get("leafBundles", [])
    pos = {n["modelId"]: (n["position"]["x"], n["position"]["y"])
           for n in nodes}
    sizes = {n["modelId"]: (n["size"]["width"], n["size"]["height"])
            for n in nodes}

    # Identify connected nodes
    edges = data.get("routedEdges", [])
    edge_endpoints = set()
    for e in edges:
        edge_endpoints.add(e["sourceModelId"])
        edge_endpoints.add(e["targetModelId"])

    # Group: each cluster + each bundle as a rigid block
    block_membership = {}  # mid -> block_key
    blocks = defaultdict(list)  # block_key -> list of mid

    # First, assign bundle members to bundle blocks (priority over cluster)
    for bi, b in enumerate(bundles):
        bkey = f"bundle:{bi}"
        members = set([b["parentModelId"]]) | set(b["leafModelIds"]) | set(b.get("sharedRootModelIds") or [])
        for mid in members:
            if mid in pos and mid not in block_membership:
                block_membership[mid] = bkey
                blocks[bkey].append(mid)

    # Then cluster members (Louvain), if not already in a bundle
    for n in nodes:
        cid = n.get("clusterId")
        if cid and n["modelId"] in pos and n["modelId"] not in block_membership:
            ckey = f"cluster:{cid}"
            block_membership[n["modelId"]] = ckey
            blocks[ckey].append(n["modelId"])

    # Remaining nodes (hubs/connectors): each gets its own singleton block
    # — but skip edgeless isolated nodes (they're stashed separately by C++)
    isolated_nodes = []
    for n in nodes:
        mid = n["modelId"]
        if mid in block_membership: continue
        if mid not in edge_endpoints:
            isolated_nodes.append(mid)
            continue
        skey = f"hub:{mid}"
        block_membership[mid] = skey
        blocks[skey].append(mid)

    print(f"Loaded {inp.name}: nodes={len(nodes)} edges={len(edges)}")
    print(f"Blocks: {len(blocks)} ({sum(1 for k in blocks if k.startswith('bundle:'))} bundle, "
          f"{sum(1 for k in blocks if k.startswith('cluster:'))} cluster, "
          f"{sum(1 for k in blocks if k.startswith('hub:'))} hub)")
    print(f"Isolated edgeless skipped: {len(isolated_nodes)}")

    # Compute each block's bbox
    block_info = []  # list of (key, w, h, members, orig_minX, orig_minY)
    for key, members in blocks.items():
        xMin = min(pos[m][0] - sizes[m][0]/2 for m in members)
        yMin = min(pos[m][1] - sizes[m][1]/2 for m in members)
        xMax = max(pos[m][0] + sizes[m][0]/2 for m in members)
        yMax = max(pos[m][1] + sizes[m][1]/2 for m in members)
        w = xMax - xMin
        h = yMax - yMin
        block_info.append((key, w, h, members, xMin, yMin))

    # Sort by area desc (larger blocks first)
    block_info.sort(key=lambda b: b[1] * b[2], reverse=True)

    # Skyline / shelf packing into target-aspect rectangle.
    # Estimate canvas width: target sqrt(total_area) * sqrt(aspect)
    total_area = sum(b[1] * b[2] for b in block_info) + gap * gap * len(block_info)
    canvas_w = math.sqrt(total_area * target_aspect) * 1.05
    print(f"Total block area: {total_area/1e9:.2f}B; canvas width target: {canvas_w:.0f}")

    # Skyline: list of (x_left, x_right, y_top) — current top profile
    # Find lowest spot wide enough for each block
    skyline = [(0.0, canvas_w, 0.0)]

    placements = {}  # block_key -> (x_min, y_min)
    for (key, w, h, members, ox, oy) in block_info:
        # Find leftmost spot of width w with min y
        best_x = None
        best_y = math.inf
        for sx in range(0, len(skyline)):
            sx0, sx1, sy = skyline[sx]
            if sx0 + w > canvas_w: continue
            # max y over the strip [sx0, sx0 + w]
            max_y_in_strip = sy
            x_used = sx0
            covered = sx1 - sx0
            si = sx + 1
            while covered < w and si < len(skyline):
                sxa, sxb, sya = skyline[si]
                max_y_in_strip = max(max_y_in_strip, sya)
                covered += sxb - sxa
                si += 1
            if covered < w: continue
            if max_y_in_strip < best_y:
                best_y = max_y_in_strip
                best_x = x_used
        if best_x is None:
            # Extend canvas — place at right edge bottom
            rightmost = max(s[1] for s in skyline)
            best_x = rightmost
            best_y = 0.0
            canvas_w = rightmost + w + gap
        placements[key] = (best_x, best_y)
        # Update skyline with new placement
        new_top = best_y + h + gap
        new_skyline = []
        x0 = best_x
        x1 = best_x + w + gap
        added = False
        for (sx0, sx1, sy) in skyline:
            if sx1 <= x0:
                new_skyline.append((sx0, sx1, sy))
            elif sx0 >= x1:
                if not added:
                    new_skyline.append((x0, x1, new_top))
                    added = True
                new_skyline.append((sx0, sx1, sy))
            else:
                # split
                if sx0 < x0:
                    new_skyline.append((sx0, x0, sy))
                if not added:
                    new_skyline.append((x0, x1, new_top))
                    added = True
                if sx1 > x1:
                    new_skyline.append((x1, sx1, sy))
        if not added:
            new_skyline.append((x0, x1, new_top))
        # merge contiguous segments with same y
        merged = []
        for seg in new_skyline:
            if merged and merged[-1][2] == seg[2] and merged[-1][1] == seg[0]:
                merged[-1] = (merged[-1][0], seg[1], seg[2])
            else:
                merged.append(seg)
        skyline = merged

    # Compute new positions
    new_pos = {}
    for (key, w, h, members, ox, oy) in block_info:
        nx, ny = placements[key]
        dx = nx - ox
        dy = ny - oy
        for m in members:
            x, y = pos[m]
            new_pos[m] = (x + dx, y + dy)

    # Add isolated edgeless nodes as a tail strip
    if isolated_nodes:
        rightmost = max(p[0] + sizes[m][0]/2
                       for m, p in new_pos.items())
        bottom = max(p[1] + sizes[m][1]/2
                    for m, p in new_pos.items())
        col_w = max(sizes[m][0] for m in isolated_nodes) + gap
        cols = 6
        for k, mid in enumerate(isolated_nodes):
            col = k % cols
            row = k // cols
            new_pos[mid] = (
                rightmost + gap + col * col_w + sizes[mid][0]/2,
                col * 0 + row * (sizes[mid][1] + 30) + sizes[mid][1]/2
            )

    # Compute final bbox
    fxs = [(p[0] - sizes[m][0]/2, p[0] + sizes[m][0]/2)
           for m, p in new_pos.items()]
    fys = [(p[1] - sizes[m][1]/2, p[1] + sizes[m][1]/2)
           for m, p in new_pos.items()]
    fw = max(e for _, e in fxs) - min(s for s, _ in fxs)
    fh = max(e for _, e in fys) - min(s for s, _ in fys)
    fa = fw * fh
    # Original bbox
    oxs = [(pos[m][0] - sizes[m][0]/2, pos[m][0] + sizes[m][0]/2)
           for m in pos]
    oys = [(pos[m][1] - sizes[m][1]/2, pos[m][1] + sizes[m][1]/2)
           for m in pos]
    ow = max(e for _, e in oxs) - min(s for s, _ in oxs)
    oh = max(e for _, e in oys) - min(s for s, _ in oys)
    oa = ow * oh
    print(f"\nBefore: {ow:.0f} x {oh:.0f} = {oa/1e9:.2f}B")
    print(f"After:  {fw:.0f} x {fh:.0f} = {fa/1e9:.2f}B")
    print(f"Δ {(fa-oa)/1e9:+.2f}B, {(fa/oa - 1)*100:+.1f}%")

    with out_tsv.open("w") as f:
        f.write("modelId\tx\ty\n")
        for n in nodes:
            mid = n["modelId"]
            x, y = new_pos.get(mid, (pos[mid][0], pos[mid][1]))
            f.write(f"{mid}\t{x:.3f}\t{y:.3f}\n")
    print(f"Wrote {out_tsv}")


if __name__ == "__main__":
    main()
