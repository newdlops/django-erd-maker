#!/usr/bin/env python3
"""Render a static visual diagnostics HTML for an ERD layout JSON."""

from __future__ import annotations

import argparse
import html
import importlib.util
import json
import math
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Iterable

SPEC = importlib.util.spec_from_file_location(
    "v34_move_search",
    Path(__file__).parent / "v34_move_search.py",
)
v34 = importlib.util.module_from_spec(SPEC)
sys.modules["v34_move_search"] = v34
assert SPEC.loader is not None
SPEC.loader.exec_module(v34)


def esc(value: object) -> str:
    return html.escape(str(value), quote=True)


def finite_float(value: object, default: float = 0.0) -> float:
    try:
        got = float(value)
    except (TypeError, ValueError):
        return default
    return got if math.isfinite(got) else default


def node_rect(node: dict) -> tuple[float, float, float, float]:
    pos = node.get("position") or {}
    size = node.get("size") or {}
    x = finite_float(pos.get("x"))
    y = finite_float(pos.get("y"))
    w = finite_float(size.get("width"), 180.0)
    h = finite_float(size.get("height"), 76.0)
    return x, y, w, h


def render_nodes_for_layout(layout: dict) -> list[dict]:
    """Return rectangles matching the webview's rendered table geometry."""
    nodes = layout.get("nodes") or []
    if not nodes:
        return []
    edges = v34.graph_edges(layout)
    positions = v34.layout_positions(layout)
    geometry = v34.build_render_collision_geometry(layout, edges)
    rect_positions = v34.collision_positions(positions, geometry)
    out: list[dict] = []
    bundle_idx = 0
    for rect_idx, node_idx_raw in enumerate(geometry.rect_node):
        node_idx = int(node_idx_raw)
        w = float(geometry.rect_widths[rect_idx])
        h = float(geometry.rect_heights[rect_idx])
        cx = float(rect_positions[rect_idx, 0])
        cy = float(rect_positions[rect_idx, 1])
        if node_idx >= 0:
            model_id = str(nodes[node_idx].get("modelId") or "")
        else:
            model_id = f"__leafbundle.rendered.{bundle_idx}"
            bundle_idx += 1
        out.append(
            {
                "modelId": model_id,
                "position": {"x": cx - w / 2.0, "y": cy - h / 2.0},
                "size": {"width": w, "height": h},
            }
        )
    return out


def edge_points(edge: dict) -> list[tuple[float, float]]:
    points = []
    for point in edge.get("points") or []:
        points.append((finite_float(point.get("x")), finite_float(point.get("y"))))
    return points


def short_id(model_id: str) -> str:
    return model_id.rsplit(".", 1)[-1]


def segment_intersects_rect(
    x1: float,
    y1: float,
    x2: float,
    y2: float,
    rx: float,
    ry: float,
    rw: float,
    rh: float,
) -> bool:
    xmin = rx
    xmax = rx + rw
    ymin = ry
    ymax = ry + rh
    dx = x2 - x1
    dy = y2 - y1
    u1 = 0.0
    u2 = 1.0
    for p, q in (
        (-dx, x1 - xmin),
        (dx, xmax - x1),
        (-dy, y1 - ymin),
        (dy, ymax - y1),
    ):
        if abs(p) < 1e-9:
            if q < 0:
                return False
            continue
        r = q / p
        if p < 0:
            if r > u2:
                return False
            if r > u1:
                u1 = r
        else:
            if r < u1:
                return False
            if r < u2:
                u2 = r
    return True


def polyline_intersects_window(
    points: list[tuple[float, float]],
    window: tuple[float, float, float, float],
) -> bool:
    if len(points) < 2:
        return False
    x, y, w, h = window
    for (x1, y1), (x2, y2) in zip(points, points[1:]):
        if segment_intersects_rect(x1, y1, x2, y2, x, y, w, h):
            return True
    return False


def rect_intersects_window(
    rect: tuple[float, float, float, float],
    window: tuple[float, float, float, float],
) -> bool:
    x, y, w, h = rect
    wx, wy, ww, wh = window
    return x <= wx + ww and x + w >= wx and y <= wy + wh and y + h >= wy


def layout_bounds(nodes: list[dict]) -> tuple[float, float, float, float]:
    xs = []
    ys = []
    for node in nodes:
        x, y, w, h = node_rect(node)
        xs.extend([x, x + w])
        ys.extend([y, y + h])
    min_x = min(xs)
    max_x = max(xs)
    min_y = min(ys)
    max_y = max(ys)
    pad = max(max_x - min_x, max_y - min_y) * 0.03
    return min_x - pad, min_y - pad, (max_x - min_x) + pad * 2, (max_y - min_y) + pad * 2


def crossing_positions(layout: dict) -> list[tuple[float, float]]:
    out = []
    for crossing in layout.get("crossings") or []:
        pos = crossing.get("position") or {}
        out.append((finite_float(pos.get("x")), finite_float(pos.get("y"))))
    return out


def select_hotspots(
    crossings: list[tuple[float, float]],
    cell_size: float,
    count: int,
    window_w: float,
    window_h: float,
) -> list[tuple[int, float, float, int]]:
    buckets: dict[tuple[int, int], list[tuple[float, float]]] = defaultdict(list)
    for x, y in crossings:
        buckets[(math.floor(x / cell_size), math.floor(y / cell_size))].append((x, y))
    ranked = sorted(buckets.items(), key=lambda item: len(item[1]), reverse=True)
    selected: list[tuple[int, float, float, int]] = []
    for (_cell, pts) in ranked:
        cx = sum(x for x, _y in pts) / len(pts)
        cy = sum(y for _x, y in pts) / len(pts)
        if any(abs(cx - ox) < window_w * 0.55 and abs(cy - oy) < window_h * 0.55 for _rank, ox, oy, _n in selected):
            continue
        selected.append((len(selected) + 1, cx, cy, len(pts)))
        if len(selected) >= count:
            break
    return selected


def approximate_edge_node_intersections(
    nodes: list[dict],
    routed_edges: list[dict],
) -> tuple[Counter[str], Counter[str], int]:
    by_id = {str(node.get("modelId")): node for node in nodes}
    node_counts: Counter[str] = Counter()
    edge_counts: Counter[str] = Counter()
    total = 0
    node_items = [
        (str(node.get("modelId")), node_rect(node))
        for node in nodes
        if node.get("modelId")
    ]
    for edge in routed_edges:
        edge_id = str(edge.get("edgeId") or "")
        source = str(edge.get("sourceModelId") or "")
        target = str(edge.get("targetModelId") or "")
        points = edge_points(edge)
        if len(points) < 2:
            continue
        for model_id, rect in node_items:
            if model_id == source or model_id == target:
                continue
            hit = False
            for (x1, y1), (x2, y2) in zip(points, points[1:]):
                if segment_intersects_rect(x1, y1, x2, y2, *rect):
                    hit = True
                    break
            if not hit:
                continue
            if model_id not in by_id:
                continue
            node_counts[model_id] += 1
            edge_counts[edge_id] += 1
            total += 1
    return node_counts, edge_counts, total


def points_attr(points: Iterable[tuple[float, float]]) -> str:
    return " ".join(f"{x:.1f},{y:.1f}" for x, y in points)


def render_svg(
    title: str,
    nodes: list[dict],
    routed_edges: list[dict],
    crossings: list[tuple[float, float]],
    bundle_boxes: list[tuple[float, float, float, float]],
    edge_node_counts: Counter[str],
    window: tuple[float, float, float, float],
    labels: bool,
    width: int,
    height: int,
) -> str:
    wx, wy, ww, wh = window
    node_parts: list[str] = []
    edge_parts: list[str] = []
    crossing_parts: list[str] = []
    bundle_parts: list[str] = []
    stroke = max(1.0, ww / width * 0.65)
    node_stroke = max(1.0, ww / width * 0.8)
    cross_r = max(22.0, ww / width * 2.2)

    for bbox in bundle_boxes:
        if not rect_intersects_window(bbox, window):
            continue
        x, y, w, h = bbox
        bundle_parts.append(
            f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" '
            'fill="none" stroke="#7c3aed" stroke-width="42" '
            'stroke-opacity="0.22" stroke-dasharray="180 120" />'
        )

    for edge in routed_edges:
        pts = edge_points(edge)
        if not polyline_intersects_window(pts, window):
            continue
        crossing_count = len(edge.get("crossingIds") or [])
        opacity = 0.09 + min(0.22, crossing_count / 60.0)
        color = "#0f5eb8" if crossing_count < 8 else "#9f1239"
        edge_parts.append(
            f'<polyline points="{points_attr(pts)}" fill="none" stroke="{color}" '
            f'stroke-width="{stroke:.2f}" stroke-opacity="{opacity:.3f}" />'
        )

    for x, y in crossings:
        if not (wx <= x <= wx + ww and wy <= y <= wy + wh):
            continue
        crossing_parts.append(
            f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{cross_r:.1f}" '
            'fill="#ef4444" fill-opacity="0.76" />'
        )

    visible_nodes = [
        node for node in nodes if rect_intersects_window(node_rect(node), window)
    ]
    show_all_labels = labels and len(visible_nodes) <= 90
    for node in visible_nodes:
        model_id = str(node.get("modelId") or "")
        x, y, w, h = node_rect(node)
        hits = edge_node_counts.get(model_id, 0)
        stroke_color = "#f59e0b" if hits else "#374151"
        stroke_width = node_stroke * (2.2 if hits else 1.0)
        fill = "#fff7ed" if hits else "#ffffff"
        node_parts.append(
            f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" '
            f'fill="{fill}" fill-opacity="0.92" stroke="{stroke_color}" '
            f'stroke-width="{stroke_width:.2f}" rx="8" />'
        )
        if show_all_labels or (labels and hits):
            font_size = max(75.0, min(140.0, ww / width * 18.0))
            node_parts.append(
                f'<text x="{x + 10:.1f}" y="{y + min(h - 8, font_size + 8):.1f}" '
                f'font-size="{font_size:.1f}" fill="#111827">{esc(short_id(model_id))}</text>'
            )

    return f"""
      <section class="panel">
        <h2>{esc(title)}</h2>
        <svg viewBox="{wx:.1f} {wy:.1f} {ww:.1f} {wh:.1f}" width="{width}" height="{height}" role="img">
          <rect x="{wx:.1f}" y="{wy:.1f}" width="{ww:.1f}" height="{wh:.1f}" fill="#f8fafc" />
          {''.join(bundle_parts)}
          {''.join(edge_parts)}
          {''.join(node_parts)}
          {''.join(crossing_parts)}
        </svg>
      </section>
    """


def metric_table(rows: list[tuple[str, dict]]) -> str:
    keys = [
        "edgeCrossings",
        "visualCrossings",
        "nodeOverlaps",
        "edgeNodeIntersections",
        "bundleNodeOverlaps",
        "bundleEdgeIntersections",
        "boundingBoxArea",
    ]
    head = "".join(f"<th>{esc(key)}</th>" for key in keys)
    body = []
    for name, meta in rows:
        cells = [f"<td>{esc(name)}</td>"]
        for key in keys:
            value = meta.get(key)
            if key == "boundingBoxArea" and isinstance(value, (int, float)):
                value = f"{value / 1e9:.2f}B"
            cells.append(f"<td>{esc(value)}</td>")
        body.append(f"<tr>{''.join(cells)}</tr>")
    return f"<table><thead><tr><th>layout</th>{head}</tr></thead><tbody>{''.join(body)}</tbody></table>"


def load_compare(text: str) -> tuple[str, dict]:
    if "=" not in text:
        path = Path(text)
        name = path.stem
    else:
        name, raw_path = text.split("=", 1)
        path = Path(raw_path)
    with path.open() as handle:
        data = json.load(handle)
    return name, data.get("engineMetadata") or {}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("layout", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--title", default="ERD visual diagnostics")
    parser.add_argument("--hotspots", type=int, default=6)
    parser.add_argument("--cell-size", type=float, default=5000.0)
    parser.add_argument("--window-width", type=float, default=11000.0)
    parser.add_argument("--window-height", type=float, default=7600.0)
    parser.add_argument("--compare", action="append", default=[])
    args = parser.parse_args()

    with args.layout.open() as handle:
        layout = json.load(handle)

    nodes = render_nodes_for_layout(layout)
    routed_edges = layout.get("routedEdges") or []
    crossings = crossing_positions(layout)
    metadata = layout.get("engineMetadata") or {}
    edge_node_counts, edge_node_edge_counts, approx_edge_node_total = (
        approximate_edge_node_intersections(nodes, routed_edges)
    )
    bundles = []

    bounds = layout_bounds(nodes)
    hotspots = select_hotspots(
        crossings,
        args.cell_size,
        args.hotspots,
        args.window_width,
        args.window_height,
    )
    compare_rows = [(args.layout.stem, metadata)]
    for item in args.compare:
        compare_rows.append(load_compare(item))

    panels = [
        render_svg(
            "Full overview",
            nodes,
            routed_edges,
            crossings,
            bundles,
            edge_node_counts,
            bounds,
            labels=False,
            width=1400,
            height=900,
        )
    ]
    for rank, cx, cy, cell_count in hotspots:
        window = (
            cx - args.window_width / 2.0,
            cy - args.window_height / 2.0,
            args.window_width,
            args.window_height,
        )
        panels.append(
            render_svg(
                f"Hotspot {rank}: {cell_count} crossings in source cell",
                nodes,
                routed_edges,
                crossings,
                bundles,
                edge_node_counts,
                window,
                labels=True,
                width=980,
                height=680,
            )
        )

    top_edges = sorted(
        routed_edges,
        key=lambda edge: len(edge.get("crossingIds") or []),
        reverse=True,
    )[:20]
    top_edge_rows = "".join(
        "<tr>"
        f"<td>{idx}</td>"
        f"<td>{len(edge.get('crossingIds') or [])}</td>"
        f"<td>{esc(edge.get('sourceModelId'))}</td>"
        f"<td>{esc(edge.get('targetModelId'))}</td>"
        f"<td>{esc(edge.get('edgeId'))}</td>"
        "</tr>"
        for idx, edge in enumerate(top_edges, 1)
    )
    top_node_rows = "".join(
        f"<tr><td>{idx}</td><td>{count}</td><td>{esc(model_id)}</td></tr>"
        for idx, (model_id, count) in enumerate(edge_node_counts.most_common(20), 1)
    )
    top_edge_node_rows = "".join(
        f"<tr><td>{idx}</td><td>{count}</td><td>{esc(edge_id)}</td></tr>"
        for idx, (edge_id, count) in enumerate(edge_node_edge_counts.most_common(20), 1)
    )
    hotspot_rows = "".join(
        f"<tr><td>{rank}</td><td>{cell_count}</td><td>{cx:.0f}</td><td>{cy:.0f}</td></tr>"
        for rank, cx, cy, cell_count in hotspots
    )

    html_text = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>{esc(args.title)}</title>
  <style>
    body {{ margin: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; background: #eef2f7; color: #111827; }}
    main {{ max-width: 1480px; margin: 0 auto; padding: 24px; }}
    h1 {{ margin: 0 0 8px; font-size: 24px; }}
    h2 {{ margin: 0 0 10px; font-size: 16px; }}
    .summary, .panel, .tables {{ background: #ffffff; border: 1px solid #d1d5db; border-radius: 8px; padding: 14px; margin: 14px 0; }}
    .grid {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(520px, 1fr)); gap: 14px; align-items: start; }}
    svg {{ width: 100%; height: auto; border: 1px solid #e5e7eb; background: #f8fafc; }}
    table {{ width: 100%; border-collapse: collapse; font-size: 12px; }}
    th, td {{ border-bottom: 1px solid #e5e7eb; padding: 6px 8px; text-align: left; vertical-align: top; }}
    th {{ background: #f3f4f6; position: sticky; top: 0; }}
    .tables {{ overflow-x: auto; }}
    .note {{ color: #4b5563; font-size: 13px; margin: 6px 0 0; }}
    .legend span {{ display: inline-block; margin-right: 18px; font-size: 13px; }}
  </style>
</head>
<body>
<main>
  <h1>{esc(args.title)}</h1>
  <section class="summary">
    {metric_table(compare_rows)}
    <p class="note">Approx edge-node hits from this HTML pass: {approx_edge_node_total}. C++ metric: {esc(metadata.get("edgeNodeIntersections"))}.</p>
    <p class="legend"><span style="color:#ef4444">red dots = edge crossings</span><span style="color:#9f1239">dark red edges = high crossing edges</span><span style="color:#f59e0b">orange nodes = edge-node hit concentration</span></p>
  </section>
  {panels[0]}
  <div class="grid">
    {''.join(panels[1:])}
  </div>
  <section class="tables">
    <h2>Hotspot Centers</h2>
    <table><thead><tr><th>rank</th><th>cell crossings</th><th>x</th><th>y</th></tr></thead><tbody>{hotspot_rows}</tbody></table>
  </section>
  <section class="tables">
    <h2>Top Crossing Edges</h2>
    <table><thead><tr><th>rank</th><th>crossings</th><th>source</th><th>target</th><th>edge</th></tr></thead><tbody>{top_edge_rows}</tbody></table>
  </section>
  <section class="tables">
    <h2>Top Edge-Node Hit Nodes</h2>
    <table><thead><tr><th>rank</th><th>hits</th><th>node</th></tr></thead><tbody>{top_node_rows}</tbody></table>
  </section>
  <section class="tables">
    <h2>Top Edge-Node Hit Edges</h2>
    <table><thead><tr><th>rank</th><th>hits</th><th>edge</th></tr></thead><tbody>{top_edge_node_rows}</tbody></table>
  </section>
</main>
</body>
</html>
"""
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(html_text)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
