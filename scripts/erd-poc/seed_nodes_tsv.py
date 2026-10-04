#!/usr/bin/env python3
"""Replace native node TSV centers with a measured position candidate."""

from __future__ import annotations

import argparse
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nodes", type=Path, required=True)
    parser.add_argument("--positions", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    positions: dict[str, tuple[str, str]] = {}
    for line in args.positions.read_text(encoding="utf-8").splitlines():
        parts = line.split("\t")
        if len(parts) >= 4 and parts[0] == "N":
            positions[parts[1]] = (parts[2], parts[3])
        elif len(parts) >= 3:
            positions[parts[0]] = (parts[1], parts[2])

    rows: list[str] = []
    missing: list[str] = []
    for line in args.nodes.read_text(encoding="utf-8").splitlines():
        parts = line.split("\t")
        if len(parts) < 6:
            continue
        model_id = parts[0]
        center = positions.get(model_id)
        if center is None:
            missing.append(model_id)
            continue
        # nodes.tsv stores the table's top-left corner, while optimizer TSVs
        # store node centers.  Preserve the measured center when the native
        # reader later adds half the width/height.
        parts[3] = str(float(center[0]) - float(parts[1]) / 2.0)
        parts[4] = str(float(center[1]) - float(parts[2]) / 2.0)
        rows.append("\t".join(parts))
    if missing:
        raise RuntimeError(f"positions missing {len(missing)} node ids")
    args.out.write_text("\n".join(rows) + "\n", encoding="utf-8")
    print(f"wrote {len(rows)} seeded nodes to {args.out}")


if __name__ == "__main__":
    main()
