#!/usr/bin/env python3
"""Relocate edgeless isolated nodes (cluster_graph stash) to a compact
rectangle inside or adjacent to the main bbox.

The cluster_graph C++ stash places 153 edgeless nodes in a tall column
at x = mainBbox.right + 300, contributing ~2k width but ~28k height
extra to the layout bbox. Re-pack into a wider, shorter grid placed at
the BOTTOM-RIGHT of the main bbox so the layout footprint shrinks
without affecting any edge (these nodes have zero incident edges, so
crossings are scale-invariant under their movement).

Usage:
  python scripts/stash-relocate.py <input.json> <output.tsv> \
      [--cols N] [--gap G] [--side bottom-right|right]
"""
import json, sys
from pathlib import Path


def main():
    if len(sys.argv) < 3:
        print("Usage: stash-relocate.py <input.json> <output.tsv> "
              "[--cols N] [--gap G] [--side bottom-right|right]")
        sys.exit(1)
    inp = Path(sys.argv[1])
    out_tsv = Path(sys.argv[2])
    cols = 30
    gap = 16
    side = "bottom-right"
    i = 3
    while i < len(sys.argv):
        if sys.argv[i] == "--cols":
            cols = int(sys.argv[i+1]); i += 2
        elif sys.argv[i] == "--gap":
            gap = float(sys.argv[i+1]); i += 2
        elif sys.argv[i] == "--side":
            side = sys.argv[i+1]; i += 2
        else:
            i += 1

    data = json.loads(inp.read_text())
    nodes = data["nodes"]
    edges = data.get("routedEdges", [])
    pos = {n["modelId"]: (n["position"]["x"], n["position"]["y"])
           for n in nodes}
    sizes = {n["modelId"]: (n["size"]["width"], n["size"]["height"])
             for n in nodes}
    connected = set()
    for e in edges:
        connected.add(e["sourceModelId"])
        connected.add(e["targetModelId"])

    isolated_ids = [n["modelId"] for n in nodes if n["modelId"] not in connected]
    print(f"Loaded {inp.name}: nodes={len(nodes)} isolated={len(isolated_ids)}")

    # Compute connected-graph bbox (excluding isolated)
    if isolated_ids:
        c_xs = [(pos[m][0] - sizes[m][0]/2, pos[m][0] + sizes[m][0]/2)
                for m in pos if m not in set(isolated_ids)]
        c_ys = [(pos[m][1] - sizes[m][1]/2, pos[m][1] + sizes[m][1]/2)
                for m in pos if m not in set(isolated_ids)]
        c_xmin = min(s for s, _ in c_xs)
        c_xmax = max(e for _, e in c_xs)
        c_ymin = min(s for s, _ in c_ys)
        c_ymax = max(e for _, e in c_ys)
        c_w = c_xmax - c_xmin
        c_h = c_ymax - c_ymin
        print(f"Connected bbox: ({c_xmin:.0f},{c_ymin:.0f}) → ({c_xmax:.0f},{c_ymax:.0f}) "
              f"({c_w:.0f}x{c_h:.0f})")

        # Pack isolated into grid
        # Use the smallest avg size for tight packing
        avg_w = sum(sizes[m][0] for m in isolated_ids) / len(isolated_ids)
        avg_h = sum(sizes[m][1] for m in isolated_ids) / len(isolated_ids)
        cell_w = avg_w + gap
        cell_h = avg_h + gap
        rows = (len(isolated_ids) + cols - 1) // cols
        grid_w = cols * cell_w
        grid_h = rows * cell_h
        print(f"Grid: {cols} cols × {rows} rows = {grid_w:.0f} × {grid_h:.0f}")

        if side == "bottom-right":
            # Place at bottom-right corner: x just inside right edge,
            # y below connected graph bottom
            base_x = c_xmax - grid_w
            base_y = c_ymax + gap * 2
        elif side == "below":
            base_x = c_xmin
            base_y = c_ymax + gap * 2
        else:  # right
            base_x = c_xmax + gap * 2
            base_y = c_ymin

        new_pos = dict(pos)
        for k, mid in enumerate(isolated_ids):
            col = k % cols
            row = k // cols
            new_pos[mid] = (
                base_x + col * cell_w + sizes[mid][0]/2,
                base_y + row * cell_h + sizes[mid][1]/2,
            )

        # Verify new bbox vs old
        nxs = [(p[0]-sizes[m][0]/2, p[0]+sizes[m][0]/2)
               for m, p in new_pos.items()]
        nys = [(p[1]-sizes[m][1]/2, p[1]+sizes[m][1]/2)
               for m, p in new_pos.items()]
        nw = max(e for _, e in nxs) - min(s for s, _ in nxs)
        nh = max(e for _, e in nys) - min(s for s, _ in nys)
        oxs = [(p[0]-sizes[m][0]/2, p[0]+sizes[m][0]/2)
               for m, p in pos.items()]
        oys = [(p[1]-sizes[m][1]/2, p[1]+sizes[m][1]/2)
               for m, p in pos.items()]
        ow = max(e for _, e in oxs) - min(s for s, _ in oxs)
        oh = max(e for _, e in oys) - min(s for s, _ in oys)
        print(f"\nBefore: {ow:.0f}x{oh:.0f} = {ow*oh/1e9:.2f}B")
        print(f"After:  {nw:.0f}x{nh:.0f} = {nw*nh/1e9:.2f}B")
        print(f"Δ {((nw*nh) - (ow*oh))/1e9:+.2f}B, {(nw*nh / (ow*oh) - 1)*100:+.1f}%")
    else:
        new_pos = pos

    with out_tsv.open("w") as f:
        f.write("modelId\tx\ty\n")
        for n in nodes:
            mid = n["modelId"]
            x, y = new_pos[mid]
            f.write(f"{mid}\t{x:.3f}\t{y:.3f}\n")
    print(f"Wrote {out_tsv}")


if __name__ == "__main__":
    main()
