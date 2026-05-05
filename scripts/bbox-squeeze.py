#!/usr/bin/env python3
"""Compact layout by squeezing empty horizontal/vertical bands.

Algorithm (per axis):
1. Project each node onto the axis as an interval [pos - size/2, pos + size/2].
2. Merge all overlapping intervals → list of contiguous OCCUPIED bands.
3. Between bands, EMPTY bands exist. If empty band width > keep_gap,
   shift all nodes below it up by (gap_width - keep_gap).
4. Repeat for the other axis.

This preserves relative geometry within each occupied band — only the
EMPTY space between bands is squeezed.

Usage:
  python scripts/bbox-squeeze.py <input.json> <output.tsv> [--keep-gap G]
"""

import json
import sys
from pathlib import Path


def find_empty_bands(intervals, keep_gap):
    """intervals: list of (start, end) — sort & merge overlapping.
    Returns: list of (start, end) for empty bands wider than keep_gap.
    """
    if not intervals: return []
    items = sorted(intervals, key=lambda v: v[0])
    merged = [list(items[0])]
    for s, e in items[1:]:
        if s <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], e)
        else:
            merged.append([s, e])
    empty = []
    for i in range(len(merged) - 1):
        gap_start = merged[i][1]
        gap_end = merged[i+1][0]
        if gap_end - gap_start > keep_gap:
            empty.append((gap_start, gap_end))
    return empty


def squeeze_axis(positions, sizes, axis, keep_gap):
    """positions: dict mid -> (x, y); axis 0=X, 1=Y.
    Returns: new positions with empty bands on this axis squeezed.
    """
    intervals = []
    for mid, p in positions.items():
        s = sizes[mid]
        intervals.append((p[axis] - s[axis]/2, p[axis] + s[axis]/2))
    bands = find_empty_bands(intervals, keep_gap)
    if not bands:
        return positions, 0.0
    # Total saving: each band shrinks from (gap) to keep_gap
    saving = sum((be - bs) - keep_gap for bs, be in bands)
    new_positions = {}
    for mid, p in positions.items():
        cur = p[axis]
        # How much shift up: sum of squeeze for bands ENTIRELY ABOVE this node's center
        shift = 0.0
        for bs, be in bands:
            if be <= cur:
                # Band entirely above (smaller axis value) — squeeze affects this node
                shift += (be - bs) - keep_gap
        new_axis_val = cur - shift
        new_p = list(p)
        new_p[axis] = new_axis_val
        new_positions[mid] = tuple(new_p)
    return new_positions, saving


def main():
    if len(sys.argv) < 3:
        print("Usage: bbox-squeeze.py <input.json> <output.tsv> [--keep-gap G]")
        sys.exit(1)
    input_path = Path(sys.argv[1])
    output_tsv = Path(sys.argv[2])
    keep_gap = 300.0
    i = 3
    while i < len(sys.argv):
        if sys.argv[i] == "--keep-gap":
            keep_gap = float(sys.argv[i+1]); i += 2
        else:
            i += 1

    data = json.loads(input_path.read_text())
    nodes = data["nodes"]
    pos = {n["modelId"]: (n["position"]["x"], n["position"]["y"])
           for n in nodes}
    sizes = {n["modelId"]: (n["size"]["width"], n["size"]["height"])
             for n in nodes}

    def bbox_dim(positions):
        xs = [(p[0] - sizes[m][0]/2, p[0] + sizes[m][0]/2)
              for m, p in positions.items()]
        ys = [(p[1] - sizes[m][1]/2, p[1] + sizes[m][1]/2)
              for m, p in positions.items()]
        w = max(e for _, e in xs) - min(s for s, _ in xs)
        h = max(e for _, e in ys) - min(s for s, _ in ys)
        return w, h, w * h

    w0, h0, a0 = bbox_dim(pos)
    print(f"Loaded {input_path.name}: nodes={len(nodes)}")
    print(f"Base bbox: {w0:.0f} x {h0:.0f} = {a0/1e9:.2f}B")

    # Squeeze X first
    pos, saved_x = squeeze_axis(pos, sizes, axis=0, keep_gap=keep_gap)
    w1, h1, a1 = bbox_dim(pos)
    print(f"After X squeeze (keep_gap={keep_gap:.0f}, saved={saved_x:.0f}): "
          f"{w1:.0f} x {h1:.0f} = {a1/1e9:.2f}B")

    # Then Y
    pos, saved_y = squeeze_axis(pos, sizes, axis=1, keep_gap=keep_gap)
    w2, h2, a2 = bbox_dim(pos)
    print(f"After Y squeeze (saved={saved_y:.0f}): "
          f"{w2:.0f} x {h2:.0f} = {a2/1e9:.2f}B")
    print(f"Total Δ {(a2-a0)/1e9:+.2f}B, {(a2/a0 - 1)*100:+.1f}%")

    with output_tsv.open("w") as f:
        f.write("modelId\tx\ty\n")
        for n in nodes:
            mid = n["modelId"]
            x, y = pos[mid]
            f.write(f"{mid}\t{x:.3f}\t{y:.3f}\n")
    print(f"Wrote {output_tsv}")


if __name__ == "__main__":
    main()
