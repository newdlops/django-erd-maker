#!/usr/bin/env python3
"""Compact bundle leaves into a tight grid near their parent.

Each leaf bundle's leaves are originally spread by cluster_graph layout
to satisfy edge-routing constraints, but visually they end up far from
the parent — creating a HUGE bundle bbox that overlaps unrelated nodes
and inflates the layout area.

This pass repositions bundle leaves into a compact rows×cols grid
adjacent to the parent. Edge crossings within the bundle's "carrier"
edges to shared root are unaffected (collapse into one carrier line).
External edges (leaf-to-non-root) are dropped by the renderer anyway.

Usage:
  python scripts/bundle-compact.py <input.json> <output.tsv> \
      [--gap 30] [--col-gap 16] [--side auto]
"""
import json, sys, math
from pathlib import Path
from collections import defaultdict


def main():
    if len(sys.argv) < 3:
        print("Usage: bundle-compact.py <input.json> <output.tsv> "
              "[--gap G] [--col-gap CG] [--side auto|right|below]")
        sys.exit(1)
    inp = Path(sys.argv[1])
    out_tsv = Path(sys.argv[2])
    row_gap = 18.0
    col_gap = 16.0
    side = "auto"
    min_spread_area = 0.0  # only compact bundles whose current spread > this
    i = 3
    while i < len(sys.argv):
        if sys.argv[i] == "--gap":
            row_gap = float(sys.argv[i+1]); i += 2
        elif sys.argv[i] == "--col-gap":
            col_gap = float(sys.argv[i+1]); i += 2
        elif sys.argv[i] == "--side":
            side = sys.argv[i+1]; i += 2
        elif sys.argv[i] == "--min-spread-area":
            min_spread_area = float(sys.argv[i+1]); i += 2
        else:
            i += 1

    data = json.loads(inp.read_text())
    nodes = data["nodes"]
    bundles = data["engineMetadata"].get("leafBundles", [])
    pos = {n["modelId"]: (n["position"]["x"], n["position"]["y"])
           for n in nodes}
    sizes = {n["modelId"]: (n["size"]["width"], n["size"]["height"])
             for n in nodes}

    print(f"Loaded {inp.name}: nodes={len(nodes)} bundles={len(bundles)}")

    new_pos = dict(pos)
    moved_total = 0
    bundle_savings = 0.0
    bundle_summary = []

    for bi, b in enumerate(bundles):
        parent = b["parentModelId"]
        leaves = [l for l in b["leafModelIds"] if l in pos]
        if not leaves: continue
        if parent not in pos: continue

        # Old bbox area (for reporting)
        old_xs = [pos[l][0] for l in leaves]
        old_ys = [pos[l][1] for l in leaves]
        old_w = max(old_xs) - min(old_xs)
        old_h = max(old_ys) - min(old_ys)
        old_area = old_w * old_h
        if old_area < min_spread_area:
            continue  # already compact, skip

        # Compute compact grid: square-ish, one cell per leaf
        n = len(leaves)
        cols = max(1, int(math.ceil(math.sqrt(n))))
        rows = max(1, int(math.ceil(n / cols)))

        # Use smallest leaf size for grid cell (can pack tightly even with
        # uneven sizes — actual rendering uses fixed leaf tile size anyway)
        cell_w = max(sizes[l][0] for l in leaves) + col_gap
        cell_h = max(sizes[l][1] for l in leaves) + row_gap
        grid_w = cols * cell_w
        grid_h = rows * cell_h

        # Place to the right of parent (or below)
        px, py = pos[parent]
        pw, ph = sizes[parent]
        if side == "right" or (side == "auto" and grid_w <= grid_h * 1.5):
            grid_x = px + pw / 2 + 60.0
            grid_y = py - grid_h / 2
        else:
            grid_x = px - grid_w / 2
            grid_y = py + ph / 2 + 60.0

        # Sort leaves by their CURRENT position to preserve relative order
        # (so the compact grid roughly matches their original row/column)
        leaves_sorted = sorted(leaves, key=lambda l: (pos[l][1], pos[l][0]))
        for k, lid in enumerate(leaves_sorted):
            col = k % cols
            row = k // cols
            new_pos[lid] = (
                grid_x + col * cell_w + sizes[lid][0] / 2,
                grid_y + row * cell_h + sizes[lid][1] / 2,
            )
            moved_total += 1

        new_w = grid_w
        new_h = grid_h
        new_area = new_w * new_h
        bundle_savings += old_area - new_area
        bundle_summary.append((bi, parent, n, old_area, new_area))

    print(f"\nMoved {moved_total} leaves across {len(bundles)} bundles")
    print(f"Total bundle bbox area saved: {bundle_savings/1e9:.2f}B")
    bundle_summary.sort(key=lambda t: t[3], reverse=True)
    print(f"\nTop 5 bundle bbox shrinks:")
    for bi, par, cnt, old, new in bundle_summary[:5]:
        print(f"  bundle {bi:>2} ({par[:30]:<30}) leaves={cnt} "
              f"old={old/1e6:>7.0f}M new={new/1e6:>5.0f}M")

    with out_tsv.open("w") as f:
        f.write("modelId\tx\ty\n")
        for n in nodes:
            mid = n["modelId"]
            x, y = new_pos[mid]
            f.write(f"{mid}\t{x:.3f}\t{y:.3f}\n")
    print(f"Wrote {out_tsv}")


if __name__ == "__main__":
    main()
