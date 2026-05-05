#!/usr/bin/env python3
"""Aggressive bbox compaction by SCALING all positions inward.

Crossings are scale-invariant (segment intersection topology unchanged
under uniform scaling), so scaling can give large bbox reduction at
zero cost in cross count — UNTIL nodes overlap (because node sizes are
fixed; only positions scale).

Strategy:
1. Compute centroid of all nodes.
2. For each scale factor s ∈ (0, 1] (binary-search or sweep), test:
   - Scale every position toward centroid by s.
   - Check for overlaps (with margin).
   - If no overlap, accept; reduce s further.
   - Else: this is the floor.
3. Output positions TSV with scaled positions.

Usage:
  python scripts/bbox-compact.py <input.json> <output.tsv> \
      [--margin 16] [--target-frac 0.25]
"""

import json
import sys
from pathlib import Path


def main():
    if len(sys.argv) < 3:
        print("Usage: bbox-compact.py <input.json> <output.tsv> "
              "[--margin M] [--target-frac F]")
        sys.exit(1)
    input_path = Path(sys.argv[1])
    output_tsv = Path(sys.argv[2])
    margin = 16.0  # min spacing between node bboxes after compaction
    target_frac = 0.25  # target = current * target_frac (area)
    i = 3
    while i < len(sys.argv):
        if sys.argv[i] == "--margin":
            margin = float(sys.argv[i+1]); i += 2
        elif sys.argv[i] == "--target-frac":
            target_frac = float(sys.argv[i+1]); i += 2
        else:
            i += 1

    data = json.loads(input_path.read_text())
    nodes = data["nodes"]
    sizes = {n["modelId"]: (n["size"]["width"], n["size"]["height"])
             for n in nodes}
    pos = {n["modelId"]: (n["position"]["x"], n["position"]["y"])
           for n in nodes}

    # Centroid of nodes
    cx = sum(p[0] for p in pos.values()) / len(pos)
    cy = sum(p[1] for p in pos.values()) / len(pos)

    # Current bbox area
    def bbox_area(positions):
        xs_min = min(positions[m][0] - sizes[m][0]/2 for m in positions)
        xs_max = max(positions[m][0] + sizes[m][0]/2 for m in positions)
        ys_min = min(positions[m][1] - sizes[m][1]/2 for m in positions)
        ys_max = max(positions[m][1] + sizes[m][1]/2 for m in positions)
        return (xs_max - xs_min) * (ys_max - ys_min)

    base_area = bbox_area(pos)
    target_area = base_area * target_frac
    target_scale_lin = (target_area / base_area) ** 0.5  # linear scale to hit
    print(f"Loaded {input_path.name}: nodes={len(nodes)}")
    print(f"Base bbox area={base_area/1e9:.2f}B")
    print(f"Target area={target_area/1e9:.2f}B (linear scale={target_scale_lin:.3f})")

    # Overlap detection (axis-aligned)
    def has_overlap(positions, margin):
        """O(N^2) — but with spatial bucket grid acceleration."""
        # Bucket grid based on max-size to limit comparisons
        max_w = max(s[0] for s in sizes.values())
        max_h = max(s[1] for s in sizes.values())
        cell_w = max_w + margin
        cell_h = max_h + margin
        buckets = {}
        for mid, (x, y) in positions.items():
            cx_, cy_ = int(x // cell_w), int(y // cell_h)
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    buckets.setdefault((cx_+dx, cy_+dy), []).append(mid)
        # For each pair in same/adjacent bucket, test overlap
        seen = set()
        for cell_mids in buckets.values():
            for i_ in range(len(cell_mids)):
                for j_ in range(i_+1, len(cell_mids)):
                    a = cell_mids[i_]; b = cell_mids[j_]
                    if a == b: continue
                    pair = (a, b) if a < b else (b, a)
                    if pair in seen: continue
                    seen.add(pair)
                    ax, ay = positions[a]
                    bx, by = positions[b]
                    aw = sizes[a][0]/2 + margin/2
                    ah = sizes[a][1]/2 + margin/2
                    bw = sizes[b][0]/2 + margin/2
                    bh = sizes[b][1]/2 + margin/2
                    if abs(ax - bx) < aw + bw and abs(ay - by) < ah + bh:
                        return True
        return False

    def scaled_positions(s):
        return {mid: (cx + (x - cx) * s, cy + (y - cy) * s)
                for mid, (x, y) in pos.items()}

    # Binary search for max compression with no overlap
    lo = target_scale_lin
    hi = 1.0
    print(f"Binary searching scale ∈ [{lo:.3f}, {hi:.3f}] for no-overlap...")
    for _ in range(20):
        mid = (lo + hi) / 2
        scaled = scaled_positions(mid)
        ovl = has_overlap(scaled, margin)
        if ovl:
            lo = mid
        else:
            hi = mid
        print(f"  scale={mid:.3f}: overlap={ovl}, "
              f"new lo={lo:.3f} hi={hi:.3f}")
        if hi - lo < 0.005:
            break
    # hi is the smallest scale we've confirmed no-overlap; use that
    chosen = hi
    chosen_positions = scaled_positions(chosen)
    final_area = bbox_area(chosen_positions)
    print(f"\nChosen scale={chosen:.3f}")
    print(f"Final bbox area={final_area/1e9:.2f}B "
          f"(Δ {(final_area-base_area)/1e9:+.2f}B, "
          f"{(final_area/base_area - 1)*100:+.1f}%)")

    with output_tsv.open("w") as f:
        f.write("modelId\tx\ty\n")
        for n in nodes:
            mid = n["modelId"]
            x, y = chosen_positions[mid]
            f.write(f"{mid}\t{x:.3f}\t{y:.3f}\n")
    print(f"Wrote {output_tsv}")


if __name__ == "__main__":
    main()
