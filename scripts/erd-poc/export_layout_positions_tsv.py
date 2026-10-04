#!/usr/bin/env python3
"""Export model center coordinates from a layout JSON as positions TSV."""

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
    rows = []
    for node in layout.get("nodes", []):
        position = node["position"]
        size = node["size"]
        rows.append(
            f"{node['modelId']}\t{float(position['x']) + float(size['width']) / 2.0:.6f}"
            f"\t{float(position['y']) + float(size['height']) / 2.0:.6f}"
        )
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(rows) + "\n", encoding="utf-8")
    print(f"wrote {len(rows)} positions to {args.out}")


if __name__ == "__main__":
    main()
