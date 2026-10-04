#!/usr/bin/env python3
"""Export routed-edge points from a layout JSON as native route TSV."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    layout = json.loads(args.layout.read_text(encoding="utf-8"))
    rows: list[str] = []
    for edge in layout.get("routedEdges", []):
        edge_id = edge.get("edgeId") or edge.get("id")
        points = edge.get("points") or []
        if not isinstance(edge_id, str) or len(points) < 2:
            continue
        values = [edge_id]
        for point in points:
            values.extend((f"{float(point['x']):.9f}", f"{float(point['y']):.9f}"))
        rows.append("\t".join(values))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(rows) + "\n", encoding="utf-8")
    print(f"wrote {len(rows)} routes to {args.out}")


if __name__ == "__main__":
    main()
