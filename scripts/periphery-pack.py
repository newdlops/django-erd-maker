#!/usr/bin/env python3
"""Pack peripheral structures (independent components + 0-deg isolated)
into a compact corner adjacent to the main mass.

The cluster_graph layout places independent components in a SINGLE ROW
below the main cluster (FMMM behavior), and isolated (0-deg) nodes in
a tall stash strip on the right. Both contribute large empty bands to
the bbox without participating in any cross-graph edges. This pass:

1. Identifies the largest connected component (main mass).
2. Identifies smaller connected components (independent).
3. Identifies 0-deg nodes (isolated edgeless).
4. Packs (2) into a 2D grid below main mass (keeping each component's
   internal layout intact — translates the whole component as a block).
5. Packs (3) into a tight grid in the corner.

Cross-invariant: independent components have no edges to the main
mass, so moving them doesn't change any inter-cluster crossings.
0-deg nodes have no edges at all.

Usage:
  python scripts/periphery-pack.py <input.json> <output.tsv>
"""
import json, sys, math
from pathlib import Path
from collections import defaultdict


def main():
    if len(sys.argv) < 3:
        print("Usage: periphery-pack.py <input.json> <output.tsv>")
        sys.exit(1)
    inp = Path(sys.argv[1])
    out_tsv = Path(sys.argv[2])

    data = json.loads(inp.read_text())
    nodes = data["nodes"]
    edges = data.get("routedEdges", [])
    pos = {n["modelId"]: (n["position"]["x"], n["position"]["y"])
           for n in nodes}
    sizes = {n["modelId"]: (n["size"]["width"], n["size"]["height"])
             for n in nodes}

    # Adjacency
    adj = defaultdict(set)
    for e in edges:
        s, t = e["sourceModelId"], e["targetModelId"]
        adj[s].add(t); adj[t].add(s)

    # Connected components (BFS)
    visited = set()
    components = []
    for n in nodes:
        nid = n["modelId"]
        if nid in visited: continue
        if nid not in adj:
            components.append([nid])
            visited.add(nid)
            continue
        comp = []
        queue = [nid]
        while queue:
            cur = queue.pop()
            if cur in visited: continue
            visited.add(cur)
            comp.append(cur)
            for nb in adj[cur]:
                if nb not in visited: queue.append(nb)
        components.append(comp)

    components.sort(key=len, reverse=True)
    main_comp = components[0]
    indep_comps = [c for c in components[1:] if len(c) > 1]
    isolated = [c[0] for c in components if len(c) == 1]
    print(f"Main: {len(main_comp)} nodes")
    print(f"Independent components: {len(indep_comps)} "
          f"(largest={len(indep_comps[0]) if indep_comps else 0})")
    print(f"Isolated: {len(isolated)}")

    # Main mass bbox
    mxs = [(pos[m][0]-sizes[m][0]/2, pos[m][0]+sizes[m][0]/2)
           for m in main_comp]
    mys = [(pos[m][1]-sizes[m][1]/2, pos[m][1]+sizes[m][1]/2)
           for m in main_comp]
    main_xmin = min(s for s, _ in mxs)
    main_xmax = max(e for _, e in mxs)
    main_ymin = min(s for s, _ in mys)
    main_ymax = max(e for _, e in mys)
    main_w = main_xmax - main_xmin
    main_h = main_ymax - main_ymin
    print(f"Main bbox: ({main_xmin:.0f},{main_ymin:.0f}) → "
          f"({main_xmax:.0f},{main_ymax:.0f}) ({main_w:.0f}x{main_h:.0f})")

    new_pos = dict(pos)

    # === Pack independent components below main mass ===
    # Each component is a block. Compute bbox per component, sort by area,
    # skyline-pack into width=main_w (so total layout stays within main_w).
    GAP = 100  # gap between blocks
    COMP_GAP = 200  # gap between component group and main mass

    def comp_bbox(comp):
        cxs = [(pos[m][0]-sizes[m][0]/2, pos[m][0]+sizes[m][0]/2) for m in comp]
        cys = [(pos[m][1]-sizes[m][1]/2, pos[m][1]+sizes[m][1]/2) for m in comp]
        return (min(s for s, _ in cxs), min(s for s, _ in cys),
                max(e for _, e in cxs), max(e for _, e in cys))

    # For each indep component: bbox + width/height + members
    indep_blocks = []
    for c in indep_comps:
        x0, y0, x1, y1 = comp_bbox(c)
        indep_blocks.append({
            "members": c, "x0": x0, "y0": y0,
            "w": x1 - x0, "h": y1 - y0,
        })
    indep_blocks.sort(key=lambda b: b["w"] * b["h"], reverse=True)

    # Skyline pack into width=main_w
    skyline = [(0.0, main_w, 0.0)]
    pack_origin_x = main_xmin
    pack_origin_y = main_ymax + COMP_GAP

    def pack_block(w, h):
        best_x, best_y = None, math.inf
        for sx in range(len(skyline)):
            sx0, sx1, sy = skyline[sx]
            if sx0 + w > main_w + 1e-3: continue
            max_y = sy
            x_used = sx0
            covered = sx1 - sx0
            si = sx + 1
            while covered < w and si < len(skyline):
                sxa, sxb, sya = skyline[si]
                max_y = max(max_y, sya)
                covered += sxb - sxa
                si += 1
            if covered < w - 1e-3: continue
            if max_y < best_y:
                best_y = max_y
                best_x = x_used
        if best_x is None:
            # Doesn't fit — extend below
            return (0.0, max(s[2] for s in skyline))
        return (best_x, best_y)

    def update_skyline(x, y, w, h):
        nonlocal skyline
        x1 = x + w + GAP
        new_top = y + h + GAP
        new_skyline = []
        added = False
        for (sx0, sx1, sy) in skyline:
            if sx1 <= x:
                new_skyline.append((sx0, sx1, sy))
            elif sx0 >= x1:
                if not added:
                    new_skyline.append((x, x1, new_top)); added = True
                new_skyline.append((sx0, sx1, sy))
            else:
                if sx0 < x:
                    new_skyline.append((sx0, x, sy))
                if not added:
                    new_skyline.append((x, x1, new_top)); added = True
                if sx1 > x1:
                    new_skyline.append((x1, sx1, sy))
        if not added:
            new_skyline.append((x, x1, new_top))
        # Merge same-y contiguous
        merged = []
        for s in new_skyline:
            if merged and merged[-1][2] == s[2] and abs(merged[-1][1] - s[0]) < 1e-3:
                merged[-1] = (merged[-1][0], s[1], s[2])
            else:
                merged.append(s)
        skyline = merged

    for blk in indep_blocks:
        px, py = pack_block(blk["w"], blk["h"])
        # Apply translation: each member moves by (px - x0, py - y0)
        dx = pack_origin_x + px - blk["x0"]
        dy = pack_origin_y + py - blk["y0"]
        for m in blk["members"]:
            ox, oy = pos[m]
            new_pos[m] = (ox + dx, oy + dy)
        update_skyline(px, py, blk["w"], blk["h"])

    indep_height = max(s[2] for s in skyline) if skyline else 0
    indep_packed_y = pack_origin_y + indep_height
    print(f"Indep packed into ({main_w:.0f} x {indep_height:.0f}) "
          f"(was 1-row stretched)")

    # === Pack isolated 0-deg nodes ===
    if isolated:
        ISO_COLS = 30
        avg_w = sum(sizes[m][0] for m in isolated) / len(isolated)
        avg_h = sum(sizes[m][1] for m in isolated) / len(isolated)
        cell_w = avg_w + 16
        cell_h = avg_h + 12
        rows = (len(isolated) + ISO_COLS - 1) // ISO_COLS
        iso_w = ISO_COLS * cell_w
        iso_h = rows * cell_h
        # Place at bottom-right of indep packed area, or new row below
        iso_origin_x = main_xmin
        iso_origin_y = indep_packed_y + COMP_GAP
        for k, mid in enumerate(isolated):
            col = k % ISO_COLS
            row = k // ISO_COLS
            new_pos[mid] = (
                iso_origin_x + col * cell_w + sizes[mid][0]/2,
                iso_origin_y + row * cell_h + sizes[mid][1]/2,
            )
        print(f"Isolated packed: {ISO_COLS}×{rows} ({iso_w:.0f}x{iso_h:.0f})")

    # Compute new bbox
    fxs = [(p[0]-sizes[m][0]/2, p[0]+sizes[m][0]/2)
           for m, p in new_pos.items()]
    fys = [(p[1]-sizes[m][1]/2, p[1]+sizes[m][1]/2)
           for m, p in new_pos.items()]
    fw = max(e for _, e in fxs) - min(s for s, _ in fxs)
    fh = max(e for _, e in fys) - min(s for s, _ in fys)
    fa = fw * fh
    oxs = [(pos[m][0]-sizes[m][0]/2, pos[m][0]+sizes[m][0]/2) for m in pos]
    oys = [(pos[m][1]-sizes[m][1]/2, pos[m][1]+sizes[m][1]/2) for m in pos]
    ow = max(e for _, e in oxs) - min(s for s, _ in oxs)
    oh = max(e for _, e in oys) - min(s for s, _ in oys)
    oa = ow * oh
    print(f"\nBefore: {ow:.0f}x{oh:.0f} = {oa/1e9:.2f}B")
    print(f"After:  {fw:.0f}x{fh:.0f} = {fa/1e9:.2f}B")
    print(f"Δ {(fa-oa)/1e9:+.2f}B, {(fa/oa - 1)*100:+.1f}%")

    with out_tsv.open("w") as f:
        f.write("modelId\tx\ty\n")
        for n in nodes:
            mid = n["modelId"]
            x, y = new_pos[mid]
            f.write(f"{mid}\t{x:.3f}\t{y:.3f}\n")
    print(f"Wrote {out_tsv}")


if __name__ == "__main__":
    main()
