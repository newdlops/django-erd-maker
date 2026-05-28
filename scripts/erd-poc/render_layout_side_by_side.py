#!/usr/bin/env python3
"""Render side-by-side hotspot comparisons for ERD layout JSONs."""

from __future__ import annotations

import argparse
import importlib.util
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "render_layout_visual_diagnostics",
    Path(__file__).parent / "render_layout_visual_diagnostics.py",
)
diag = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(diag)


def parse_layout_arg(text: str) -> tuple[str, Path]:
    if "=" not in text:
        path = Path(text)
        return path.stem, path
    name, raw_path = text.split("=", 1)
    return name, Path(raw_path)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", action="append", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--title", default="ERD side-by-side diagnostics")
    parser.add_argument("--hotspots", type=int, default=5)
    parser.add_argument("--cell-size", type=float, default=5000.0)
    parser.add_argument("--window-width", type=float, default=11000.0)
    parser.add_argument("--window-height", type=float, default=7600.0)
    args = parser.parse_args()

    layouts = []
    for item in args.layout:
        name, path = parse_layout_arg(item)
        data = diag.json.loads(path.read_text())
        nodes = diag.render_nodes_for_layout(data)
        routed_edges = data.get("routedEdges") or []
        metadata = data.get("engineMetadata") or {}
        crossings = diag.crossing_positions(data)
        edge_node_counts, _edge_node_edge_counts, approx_edge_node_total = (
            diag.approximate_edge_node_intersections(nodes, routed_edges)
        )
        bundles = []
        layouts.append(
            {
                "name": name,
                "path": path,
                "data": data,
                "nodes": nodes,
                "edges": routed_edges,
                "metadata": metadata,
                "crossings": crossings,
                "edgeNodeCounts": edge_node_counts,
                "approxEdgeNodeTotal": approx_edge_node_total,
                "bundles": bundles,
            }
        )

    anchor = layouts[0]
    hotspots = diag.select_hotspots(
        anchor["crossings"],
        args.cell_size,
        args.hotspots,
        args.window_width,
        args.window_height,
    )
    metric_rows = [(item["name"], item["metadata"]) for item in layouts]

    full_panels = []
    for item in layouts:
        full_panels.append(
            diag.render_svg(
                f"{item['name']} overview",
                item["nodes"],
                item["edges"],
                item["crossings"],
                item["bundles"],
                item["edgeNodeCounts"],
                diag.layout_bounds(item["nodes"]),
                labels=False,
                width=680,
                height=430,
            )
        )

    hotspot_sections = []
    for rank, cx, cy, cell_count in hotspots:
        window = (
            cx - args.window_width / 2.0,
            cy - args.window_height / 2.0,
            args.window_width,
            args.window_height,
        )
        panels = []
        for item in layouts:
            panels.append(
                diag.render_svg(
                    f"{item['name']} / hotspot {rank}",
                    item["nodes"],
                    item["edges"],
                    item["crossings"],
                    item["bundles"],
                    item["edgeNodeCounts"],
                    window,
                    labels=True,
                    width=680,
                    height=430,
                )
            )
        hotspot_sections.append(
            f"""
            <section class="row">
              <h2>Hotspot {rank}: anchor cell crossings={cell_count}, center=({cx:.0f}, {cy:.0f})</h2>
              <div class="compare-grid">{''.join(panels)}</div>
            </section>
            """
        )

    approx_rows = "".join(
        f"<tr><td>{diag.esc(item['name'])}</td>"
        f"<td>{item['approxEdgeNodeTotal']}</td>"
        f"<td>{diag.esc(item['path'])}</td></tr>"
        for item in layouts
    )

    html_text = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>{diag.esc(args.title)}</title>
  <style>
    body {{ margin: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; background: #eef2f7; color: #111827; }}
    main {{ max-width: 1760px; margin: 0 auto; padding: 24px; }}
    h1 {{ margin: 0 0 10px; font-size: 24px; }}
    h2 {{ margin: 0 0 10px; font-size: 16px; }}
    .summary, .row, .panel {{ background: #fff; border: 1px solid #d1d5db; border-radius: 8px; padding: 14px; margin: 14px 0; }}
    .compare-grid {{ display: grid; grid-template-columns: repeat({len(layouts)}, minmax(0, 1fr)); gap: 12px; align-items: start; }}
    svg {{ width: 100%; height: auto; border: 1px solid #e5e7eb; background: #f8fafc; }}
    table {{ width: 100%; border-collapse: collapse; font-size: 12px; }}
    th, td {{ border-bottom: 1px solid #e5e7eb; padding: 6px 8px; text-align: left; vertical-align: top; }}
    th {{ background: #f3f4f6; }}
    .note {{ color: #4b5563; font-size: 13px; margin: 8px 0 0; }}
    @media (max-width: 1200px) {{ .compare-grid {{ grid-template-columns: 1fr; }} }}
  </style>
</head>
<body>
<main>
  <h1>{diag.esc(args.title)}</h1>
  <section class="summary">
    {diag.metric_table(metric_rows)}
    <p class="note">Approx edge-node hits are simple visual locators, not the authoritative C++ metric.</p>
    <table><thead><tr><th>layout</th><th>approx edge-node hits</th><th>path</th></tr></thead><tbody>{approx_rows}</tbody></table>
  </section>
  <section class="row">
    <h2>Overview</h2>
    <div class="compare-grid">{''.join(full_panels)}</div>
  </section>
  {''.join(hotspot_sections)}
</main>
</body>
</html>
"""
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(html_text)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
