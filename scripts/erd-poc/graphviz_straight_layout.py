#!/usr/bin/env python3
"""Generate real-node centers with a Graphviz engine for straight-edge scoring."""

from __future__ import annotations

import argparse
import shlex
import subprocess
import tempfile
from pathlib import Path


def quote(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nodes", type=Path, required=True)
    parser.add_argument("--edges", type=Path, required=True)
    parser.add_argument("--engine", choices=("sfdp", "neato", "fdp", "dot", "circo"), required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--overlap", default="prism")
    args = parser.parse_args()

    nodes: list[tuple[str, float, float]] = []
    for line in args.nodes.read_text(encoding="utf-8").splitlines():
        fields = line.split("\t")
        if len(fields) >= 3:
            nodes.append((fields[0], float(fields[1]), float(fields[2])))
    edges: set[tuple[str, str]] = set()
    for line in args.edges.read_text(encoding="utf-8").splitlines():
        fields = line.split("\t")
        if len(fields) < 3 or fields[1] == fields[2]:
            continue
        edges.add(tuple(sorted((fields[1], fields[2]))))

    lines = [
        "graph ERD {",
        (
            f"graph [overlap={quote(args.overlap)}, sep=\"+24\", "
            "pack=true, packmode=\"array\", outputorder=edgesfirst, splines=line];"
        ),
        "node [shape=box, fixedsize=true, label=\"\"] ;",
    ]
    for model_id, width, height in nodes:
        lines.append(
            f"{quote(model_id)} [width={max(width / 72.0, 0.02):.9f}, "
            f"height={max(height / 72.0, 0.02):.9f}];"
        )
    for source, target in sorted(edges):
        lines.append(f"{quote(source)} -- {quote(target)};")
    lines.append("}")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="django-erd-graphviz-") as temporary:
        dot_path = Path(temporary) / "graph.dot"
        dot_path.write_text("\n".join(lines), encoding="utf-8")
        result = subprocess.run(
            [args.engine, "-Tplain", str(dot_path)],
            check=False,
            capture_output=True,
            text=True,
        )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or f"{args.engine} failed")

    positions: dict[str, tuple[float, float]] = {}
    for line in result.stdout.splitlines():
        fields = shlex.split(line)
        if len(fields) >= 4 and fields[0] == "node":
            positions[fields[1]] = (float(fields[2]) * 72.0, -float(fields[3]) * 72.0)
    missing = [model_id for model_id, _width, _height in nodes if model_id not in positions]
    if missing:
        raise RuntimeError(f"Graphviz omitted {len(missing)} nodes")
    args.out.write_text(
        "".join(
            f"{model_id}\t{positions[model_id][0]:.9f}\t{positions[model_id][1]:.9f}\n"
            for model_id, _width, _height in nodes
        ),
        encoding="utf-8",
    )
    print(
        f"engine={args.engine} nodes={len(nodes)} edges={len(edges)} out={args.out}"
    )


if __name__ == "__main__":
    main()
