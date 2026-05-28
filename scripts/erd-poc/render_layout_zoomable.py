#!/usr/bin/env python3
"""Render an interactive zoom/pan HTML view for an ERD layout JSON."""

from __future__ import annotations

import argparse
import html
import importlib.util
import json
import math
import sys
from pathlib import Path

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


def finite(value: object, default: float = 0.0) -> float:
    try:
        got = float(value)
    except (TypeError, ValueError):
        return default
    return got if math.isfinite(got) else default


def leaf_bundle_render_size(member_count: int) -> tuple[float, float]:
    leaf_cell_w = 200.0
    leaf_cell_h = 56.0
    leaf_gap_x = 10.0
    leaf_gap_y = 8.0
    bundle_header = 48.0
    bundle_pad = 16.0
    n = max(1, member_count)
    cols = max(1, math.ceil(math.sqrt(n)))
    rows = max(1, math.ceil(n / cols))
    inner_w = cols * leaf_cell_w + (cols - 1) * leaf_gap_x
    inner_h = rows * leaf_cell_h + (rows - 1) * leaf_gap_y
    return inner_w + bundle_pad * 2.0, bundle_header + inner_h + bundle_pad


def bundle_render_rect(bundle: dict) -> dict:
    bbox = bundle.get("bbox") or {}
    width, height = leaf_bundle_render_size(len(bundle.get("leafModelIds") or []))
    cx = finite(bbox.get("x")) + finite(bbox.get("width")) / 2.0
    cy = finite(bbox.get("y")) + finite(bbox.get("height")) / 2.0
    return {
        "x": cx - width / 2.0,
        "y": cy - height / 2.0,
        "w": width,
        "h": height,
    }


def rect_center(rect: dict) -> tuple[float, float]:
    return (
        finite(rect.get("x")) + finite(rect.get("w")) / 2.0,
        finite(rect.get("y")) + finite(rect.get("h")) / 2.0,
    )


def boundary_port(rect: dict, toward: tuple[float, float]) -> dict:
    cx, cy = rect_center(rect)
    dx = toward[0] - cx
    dy = toward[1] - cy
    if abs(dx) < 0.01 and abs(dy) < 0.01:
        return {"x": cx, "y": cy}
    scale = float("inf")
    if abs(dx) >= 0.01:
        sx = (
            (finite(rect.get("x")) + finite(rect.get("w")) - cx) / dx
            if dx > 0.0
            else (finite(rect.get("x")) - cx) / dx
        )
        if sx > 0.0:
            scale = min(scale, sx)
    if abs(dy) >= 0.01:
        sy = (
            (finite(rect.get("y")) + finite(rect.get("h")) - cy) / dy
            if dy > 0.0
            else (finite(rect.get("y")) - cy) / dy
        )
        if sy > 0.0:
            scale = min(scale, sy)
    if not math.isfinite(scale):
        scale = 0.0
    return {"x": round(cx + dx * scale, 2), "y": round(cy + dy * scale, 2)}


def clipped_edge_points(
    points: list[dict],
    source_rect: dict | None,
    target_rect: dict | None,
) -> list[dict]:
    if len(points) < 2:
        return points
    clipped = [dict(point) for point in points]
    if source_rect is not None:
        toward = (finite(clipped[1].get("x")), finite(clipped[1].get("y")))
        clipped[0] = boundary_port(source_rect, toward)
    if target_rect is not None:
        toward = (finite(clipped[-2].get("x")), finite(clipped[-2].get("y")))
        clipped[-1] = boundary_port(target_rect, toward)
    return clipped


def graph_rects(layout: dict) -> list[dict]:
    nodes = layout.get("nodes") or []
    edges = v34.graph_edges(layout)
    positions = v34.layout_positions(layout)
    geometry = v34.build_render_collision_geometry(layout, edges)
    rect_positions = v34.collision_positions(positions, geometry)
    rects = []
    bundle_idx = 0
    for rect_idx, node_idx_raw in enumerate(geometry.rect_node):
        node_idx = int(node_idx_raw)
        w = float(geometry.rect_widths[rect_idx])
        h = float(geometry.rect_heights[rect_idx])
        cx = float(rect_positions[rect_idx, 0])
        cy = float(rect_positions[rect_idx, 1])
        if node_idx >= 0:
            node = nodes[node_idx]
            model_id = str(node.get("modelId") or "")
            cluster_id = str(node.get("clusterId") or "")
            kind = "node"
        else:
            model_id = f"leaf-bundle-{bundle_idx + 1}"
            cluster_id = ""
            kind = "bundle"
            bundle_idx += 1
        rects.append(
            {
                "id": model_id,
                "label": model_id.rsplit(".", 1)[-1],
                "cluster": cluster_id,
                "kind": kind,
                "x": cx - w / 2.0,
                "y": cy - h / 2.0,
                "w": w,
                "h": h,
            }
        )
    return rects


def hub_carrier_ids(layout: dict, hub_threshold: int) -> list[str]:
    nodes = layout.get("nodes") or []
    routed_edges = layout.get("routedEdges") or []
    positions = {
        str(node.get("modelId") or ""): (
            finite((node.get("position") or {}).get("x")),
            finite((node.get("position") or {}).get("y")),
        )
        for node in nodes
    }
    cluster_by_model = {
        str(node.get("modelId") or ""): str(node.get("clusterId") or "")
        for node in nodes
    }
    cluster_sum: dict[str, list[float]] = {}
    for node in nodes:
        cluster = str(node.get("clusterId") or "")
        if not cluster:
            continue
        pos = node.get("position") or {}
        got = cluster_sum.setdefault(cluster, [0.0, 0.0, 0.0])
        got[0] += finite(pos.get("x"))
        got[1] += finite(pos.get("y"))
        got[2] += 1.0
    cluster_centroids = {
        key: (value[0] / value[2], value[1] / value[2])
        for key, value in cluster_sum.items()
        if value[2] > 0
    }

    def nearest_cluster(model_id: str) -> str:
        point = positions.get(model_id)
        if point is None or not cluster_centroids:
            return ""
        best = ""
        best_d2 = float("inf")
        for cluster, center in cluster_centroids.items():
            dx = point[0] - center[0]
            dy = point[1] - center[1]
            d2 = dx * dx + dy * dy
            if d2 < best_d2:
                best_d2 = d2
                best = cluster
        return best

    leaf_to_bundle: dict[str, int] = {}
    bundle_roots: list[set[str]] = []
    for bundle_idx, bundle in enumerate((layout.get("engineMetadata") or {}).get("leafBundles") or []):
        roots = {str(root) for root in (bundle.get("sharedRootModelIds") or [])}
        parent = str(bundle.get("parentModelId") or "")
        if parent:
            roots.add(parent)
        bundle_roots.append(roots)
        for leaf in bundle.get("leafModelIds") or []:
            leaf_to_bundle[str(leaf)] = bundle_idx

    ids: list[str] = []
    cluster_pairs: list[tuple[str, str]] = []
    for edge_index, edge in enumerate(routed_edges):
        source = str(edge.get("sourceModelId") or "")
        target = str(edge.get("targetModelId") or "")
        source_bundle = leaf_to_bundle.get(source)
        if source_bundle is not None and target in bundle_roots[source_bundle]:
            ids.append("")
            cluster_pairs.append(("", ""))
            continue
        target_bundle = leaf_to_bundle.get(target)
        if target_bundle is not None and source in bundle_roots[target_bundle]:
            ids.append("")
            cluster_pairs.append(("", ""))
            continue
        source_cluster = cluster_by_model.get(source) or nearest_cluster(source)
        target_cluster = cluster_by_model.get(target) or nearest_cluster(target)
        if source_cluster and target_cluster:
            cluster_pairs.append((source_cluster, target_cluster))
            ids.append("")
        else:
            ids.append("")
            cluster_pairs.append(("", ""))

    if hub_threshold > 0:
        incident: dict[str, int] = {}
        for left, right in cluster_pairs:
            if not left or not right or left == right:
                continue
            incident[left] = incident.get(left, 0) + 1
            incident[right] = incident.get(right, 0) + 1
        for index, (left, right) in enumerate(cluster_pairs):
            if not left or not right or left == right:
                continue
            left_count = incident.get(left, 0)
            right_count = incident.get(right, 0)
            if left_count < hub_threshold and right_count < hub_threshold:
                continue
            hub = left if (left_count > right_count or (left_count == right_count and left < right)) else right
            ids[index] = f"H|{hub}"
    return ids


def graph_edges(
    layout: dict,
    hub_threshold: int = 0,
    relation_mode: str = "carrier",
    rects: list[dict] | None = None,
) -> list[dict]:
    if rects is None:
        rects = graph_rects(layout)
    node_rects = {
        str(rect.get("id") or ""): rect
        for rect in rects
        if rect.get("kind") == "node"
    }
    bundle_rects = {
        int(str(rect.get("id") or "leaf-bundle-0").rsplit("-", 1)[-1]) - 1: rect
        for rect in rects
        if rect.get("kind") == "bundle"
    }
    leaf_bundles = (layout.get("engineMetadata") or {}).get("leafBundles") or []
    leaf_to_bundle: dict[str, int] = {}
    bundle_roots: list[set[str]] = []
    for bundle_idx, bundle in enumerate(leaf_bundles):
        roots = {str(root) for root in (bundle.get("sharedRootModelIds") or [])}
        parent = str(bundle.get("parentModelId") or "")
        if parent:
            roots.add(parent)
        bundle_roots.append(roots)
        for leaf in bundle.get("leafModelIds") or []:
            leaf_to_bundle[str(leaf)] = bundle_idx

    raw_edges = []
    bundle_groups: dict[str, dict] = {}
    for edge_index, edge in enumerate(layout.get("routedEdges") or []):
        source = str(edge.get("sourceModelId") or "")
        target = str(edge.get("targetModelId") or "")
        source_bundle = leaf_to_bundle.get(source)
        if source_bundle is not None and target in bundle_roots[source_bundle]:
            bundle_groups.setdefault(
                f"B{source_bundle}|{target}",
                {
                    "bundle": source_bundle,
                    "count": 0,
                    "id": f"B{source_bundle}|{target}",
                    "root": target,
                    "source": source,
                    "target": target,
                },
            )["count"] += 1
            continue
        target_bundle = leaf_to_bundle.get(target)
        if target_bundle is not None and source in bundle_roots[target_bundle]:
            bundle_groups.setdefault(
                f"B{target_bundle}|{source}",
                {
                    "bundle": target_bundle,
                    "count": 0,
                    "id": f"B{target_bundle}|{source}",
                    "root": source,
                    "source": source,
                    "target": target,
                },
            )["count"] += 1
            continue
        if source_bundle is not None or target_bundle is not None:
            continue

        points = [
            {"x": finite(point.get("x")), "y": finite(point.get("y"))}
            for point in edge.get("points") or []
        ]
        if len(points) < 2:
            continue
        points = clipped_edge_points(
            points,
            node_rects.get(source),
            node_rects.get(target),
        )
        raw_edges.append(
            {
                "id": str(edge.get("edgeId") or ""),
                "source": source,
                "target": target,
                "points": points,
                "carrier": "",
                "count": 1,
                "rawIndex": edge_index,
            }
        )

    out = []
    for group in bundle_groups.values():
        bundle_idx = int(group["bundle"])
        root_rect = node_rects.get(str(group["root"]))
        if root_rect is None or bundle_idx >= len(leaf_bundles):
            continue
        b_rect = bundle_rects.get(bundle_idx) or bundle_render_rect(leaf_bundles[bundle_idx])
        b_center = rect_center(b_rect)
        r_center = rect_center(root_rect)
        out.append(
            {
                "id": str(group["id"]),
                "source": str(group["source"]),
                "target": str(group["target"]),
                "points": [boundary_port(b_rect, r_center), boundary_port(root_rect, b_center)],
                "carrier": str(group["id"]),
                "count": int(group["count"]),
            }
        )

    if relation_mode == "exact" or hub_threshold <= 0:
        return out + raw_edges

    ids = hub_carrier_ids(layout, hub_threshold)
    grouped: dict[str, list[dict]] = {}
    for edge in raw_edges:
        raw_index = int(edge.get("rawIndex") or 0)
        carrier = ids[raw_index] if raw_index < len(ids) else ""
        if carrier:
            grouped.setdefault(carrier, []).append(edge)
        else:
            out.append(edge)

    for carrier, members in grouped.items():
        start_x = sum(edge["points"][0]["x"] for edge in members) / len(members)
        start_y = sum(edge["points"][0]["y"] for edge in members) / len(members)
        end_x = sum(edge["points"][-1]["x"] for edge in members) / len(members)
        end_y = sum(edge["points"][-1]["y"] for edge in members) / len(members)
        out.append(
            {
                "id": carrier,
                "source": members[0]["source"],
                "target": members[0]["target"],
                "points": [{"x": start_x, "y": start_y}, {"x": end_x, "y": end_y}],
                "carrier": carrier,
                "count": len(members),
            }
        )
    return out


def graph_crossings(layout: dict) -> list[dict]:
    out = []
    for crossing in layout.get("crossings") or []:
        pos = crossing.get("position") or {}
        out.append({"x": finite(pos.get("x")), "y": finite(pos.get("y"))})
    return out


def bounds(rects: list[dict], edges: list[dict]) -> dict:
    xs: list[float] = []
    ys: list[float] = []
    for rect in rects:
        x = float(rect["x"])
        y = float(rect["y"])
        w = float(rect["w"])
        h = float(rect["h"])
        xs.extend([x, x + w])
        ys.extend([y, y + h])
    for edge in edges:
        for point in edge["points"]:
            xs.append(float(point["x"]))
            ys.append(float(point["y"]))
    min_x = min(xs)
    max_x = max(xs)
    min_y = min(ys)
    max_y = max(ys)
    pad = max(max_x - min_x, max_y - min_y) * 0.04
    return {
        "x": min_x - pad,
        "y": min_y - pad,
        "w": (max_x - min_x) + pad * 2.0,
        "h": (max_y - min_y) + pad * 2.0,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("layout", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--title", default="ERD Zoomable View")
    parser.add_argument("--hub-carrier-threshold", type=int, default=0)
    parser.add_argument(
        "--relation-mode",
        choices=["carrier", "exact"],
        default="carrier",
        help=(
            "carrier: render metric carrier abstraction; "
            "exact: render all visible relations clipped to node/bundle boxes"
        ),
    )
    args = parser.parse_args()

    layout = json.loads(args.layout.read_text())
    rects = graph_rects(layout)
    edges = graph_edges(layout, args.hub_carrier_threshold, args.relation_mode, rects)
    crossings = graph_crossings(layout)
    data = {
        "title": args.title,
        "metrics": layout.get("engineMetadata") or {},
        "relationMode": args.relation_mode,
        "rects": rects,
        "edges": edges,
        "crossings": crossings,
        "bounds": bounds(rects, edges),
    }

    payload = json.dumps(data, separators=(",", ":"))
    html_text = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{esc(args.title)}</title>
  <style>
    html, body {{ height: 100%; margin: 0; font-family: ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; background: #101418; color: #edf2f7; }}
    body {{ overflow: hidden; }}
    .shell {{ display: grid; grid-template-rows: auto 1fr; height: 100%; }}
    .bar {{ display: flex; gap: 10px; align-items: center; padding: 10px 12px; background: #18202a; border-bottom: 1px solid #334155; }}
    .title {{ font-weight: 700; margin-right: 12px; white-space: nowrap; }}
    .metric {{ color: #cbd5e1; font-size: 13px; white-space: nowrap; }}
    .spacer {{ flex: 1; }}
    button, input {{ height: 30px; border-radius: 6px; border: 1px solid #475569; background: #0f172a; color: #e2e8f0; }}
    button {{ padding: 0 10px; cursor: pointer; }}
    button:hover {{ background: #1e293b; }}
    input {{ width: 260px; padding: 0 8px; }}
    #canvas {{ width: 100%; height: 100%; display: block; background: #f8fafc; cursor: grab; }}
    #canvas.dragging {{ cursor: grabbing; }}
    .hint {{ color: #94a3b8; font-size: 12px; }}
  </style>
</head>
<body>
  <div class="shell">
    <div class="bar">
      <div class="title">{esc(args.title)}</div>
      <div class="metric" id="metrics"></div>
      <div class="spacer"></div>
      <input id="search" placeholder="model name search">
      <button id="fit">Fit</button>
      <button id="zin">+</button>
      <button id="zout">-</button>
      <span class="hint" id="zoomLabel"></span>
    </div>
    <canvas id="canvas"></canvas>
  </div>
  <script id="layout-data" type="application/json">{payload}</script>
  <script>
    const data = JSON.parse(document.getElementById('layout-data').textContent);
    const canvas = document.getElementById('canvas');
    const ctx = canvas.getContext('2d');
    const metrics = data.metrics || {{}};
    const metricText = [
      `view=${{data.relationMode || 'carrier'}}`,
      `edge=${{metrics.edgeCrossings ?? '?'}}`,
      `visual=${{metrics.visualCrossings ?? '?'}}`,
      `edgeNode=${{metrics.edgeNodeIntersections ?? '?'}}`,
      `overlap=${{metrics.nodeOverlaps ?? '?'}}`,
      `segments=${{metrics.routeSegments ?? '?'}}`
    ].join(' · ');
    document.getElementById('metrics').textContent = metricText;

    let dpr = window.devicePixelRatio || 1;
    let panX = 0, panY = 0, zoom = 1;
    let dragging = false;
    let lastX = 0, lastY = 0;
    let selected = '';

    function resize() {{
      const rect = canvas.getBoundingClientRect();
      dpr = window.devicePixelRatio || 1;
      canvas.width = Math.max(1, Math.floor(rect.width * dpr));
      canvas.height = Math.max(1, Math.floor(rect.height * dpr));
      draw();
    }}

    function fit() {{
      const rect = canvas.getBoundingClientRect();
      const b = data.bounds;
      const zx = rect.width / b.w;
      const zy = rect.height / b.h;
      zoom = Math.max(0.01, Math.min(zx, zy) * 0.92);
      panX = (rect.width - b.w * zoom) / 2 - b.x * zoom;
      panY = (rect.height - b.h * zoom) / 2 - b.y * zoom;
      draw();
    }}

    function worldToScreen(x, y) {{
      return [x * zoom + panX, y * zoom + panY];
    }}

    function screenToWorld(x, y) {{
      return [(x - panX) / zoom, (y - panY) / zoom];
    }}

    function visibleWorld() {{
      const rect = canvas.getBoundingClientRect();
      const a = screenToWorld(0, 0);
      const b = screenToWorld(rect.width, rect.height);
      const pad = 600 / Math.max(zoom, 0.01);
      return {{ left: a[0] - pad, top: a[1] - pad, right: b[0] + pad, bottom: b[1] + pad }};
    }}

    function rectVisible(r, v) {{
      return r.x <= v.right && r.x + r.w >= v.left && r.y <= v.bottom && r.y + r.h >= v.top;
    }}

    function edgeVisible(edge, v) {{
      for (const p of edge.points) {{
        if (p.x >= v.left && p.x <= v.right && p.y >= v.top && p.y <= v.bottom) return true;
      }}
      return false;
    }}

    function colorForCluster(cluster) {{
      if (!cluster) return '#64748b';
      let hash = 2166136261;
      for (let i = 0; i < cluster.length; i++) hash = Math.imul(hash ^ cluster.charCodeAt(i), 16777619);
      const hue = Math.abs(hash) % 360;
      return `hsl(${{hue}} 58% 44%)`;
    }}

    function draw() {{
      const rect = canvas.getBoundingClientRect();
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, rect.width, rect.height);
      ctx.fillStyle = '#f8fafc';
      ctx.fillRect(0, 0, rect.width, rect.height);
      ctx.save();
      ctx.translate(panX, panY);
      ctx.scale(zoom, zoom);
      const v = visibleWorld();

      ctx.lineCap = 'round';
      ctx.lineJoin = 'round';
      for (const edge of data.edges) {{
        if (!edgeVisible(edge, v)) continue;
        const isHub = String(edge.carrier || '').startsWith('H|');
        const count = Number(edge.count || 1);
        ctx.strokeStyle = isHub ? 'rgba(15, 23, 42, 0.34)' : 'rgba(71, 85, 105, 0.22)';
        ctx.lineWidth = Math.max((isHub ? 2.2 : 1.0) / zoom, isHub ? 1.6 : 0.8)
          + Math.min(10, Math.sqrt(count)) / Math.max(zoom, 0.08) * 0.16;
        ctx.beginPath();
        edge.points.forEach((p, i) => i === 0 ? ctx.moveTo(p.x, p.y) : ctx.lineTo(p.x, p.y));
        ctx.stroke();
      }}

      if (zoom >= 0.08) {{
        ctx.fillStyle = 'rgba(239, 68, 68, 0.75)';
        for (const c of data.crossings) {{
          if (c.x < v.left || c.x > v.right || c.y < v.top || c.y > v.bottom) continue;
          ctx.beginPath();
          ctx.arc(c.x, c.y, Math.max(3 / zoom, 10), 0, Math.PI * 2);
          ctx.fill();
        }}
      }}

      for (const r of data.rects) {{
        if (!rectVisible(r, v)) continue;
        const isSelected = selected && r.id.toLowerCase().includes(selected);
        const fill = r.kind === 'bundle' ? '#fff7ed' : '#ffffff';
        ctx.fillStyle = isSelected ? '#fef08a' : fill;
        ctx.strokeStyle = isSelected ? '#dc2626' : (r.kind === 'bundle' ? '#ea580c' : colorForCluster(r.cluster));
        ctx.lineWidth = isSelected ? Math.max(5 / zoom, 2) : Math.max(1.5 / zoom, 1);
        ctx.beginPath();
        ctx.rect(r.x, r.y, r.w, r.h);
        ctx.fill();
        ctx.stroke();
        if (zoom >= 0.22 || isSelected) {{
          ctx.fillStyle = '#0f172a';
          ctx.font = `${{Math.max(10 / zoom, 12)}}px ui-sans-serif, system-ui`;
          ctx.fillText(r.label, r.x + 10 / zoom, r.y + Math.min(r.h - 8 / zoom, 22 / zoom));
        }}
      }}

      ctx.restore();
      document.getElementById('zoomLabel').textContent = `zoom ${{zoom.toFixed(3)}}`;
    }}

    canvas.addEventListener('wheel', (event) => {{
      event.preventDefault();
      const rect = canvas.getBoundingClientRect();
      const x = event.clientX - rect.left;
      const y = event.clientY - rect.top;
      const before = screenToWorld(x, y);
      const factor = Math.exp(-event.deltaY * 0.001);
      zoom = Math.max(0.01, Math.min(3, zoom * factor));
      panX = x - before[0] * zoom;
      panY = y - before[1] * zoom;
      draw();
    }}, {{ passive: false }});

    canvas.addEventListener('pointerdown', (event) => {{
      dragging = true;
      lastX = event.clientX;
      lastY = event.clientY;
      canvas.classList.add('dragging');
      canvas.setPointerCapture(event.pointerId);
    }});
    canvas.addEventListener('pointermove', (event) => {{
      if (!dragging) return;
      panX += event.clientX - lastX;
      panY += event.clientY - lastY;
      lastX = event.clientX;
      lastY = event.clientY;
      draw();
    }});
    canvas.addEventListener('pointerup', (event) => {{
      dragging = false;
      canvas.classList.remove('dragging');
      canvas.releasePointerCapture(event.pointerId);
    }});

    document.getElementById('fit').addEventListener('click', fit);
    document.getElementById('zin').addEventListener('click', () => {{ zoom *= 1.25; draw(); }});
    document.getElementById('zout').addEventListener('click', () => {{ zoom /= 1.25; draw(); }});
    document.getElementById('search').addEventListener('input', (event) => {{
      selected = String(event.target.value || '').trim().toLowerCase();
      if (selected) {{
        const hit = data.rects.find((r) => r.id.toLowerCase().includes(selected));
        if (hit) {{
          const rect = canvas.getBoundingClientRect();
          zoom = Math.max(0.12, Math.min(1.1, zoom));
          panX = rect.width / 2 - (hit.x + hit.w / 2) * zoom;
          panY = rect.height / 2 - (hit.y + hit.h / 2) * zoom;
        }}
      }}
      draw();
    }});
    window.addEventListener('resize', resize);
    resize();
    fit();
  </script>
</body>
</html>
"""
    args.out.write_text(html_text)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
