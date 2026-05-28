#!/usr/bin/env python3
"""Prototype hub-fan carriers for the v35 exact visual layout.

The current straight-edge layout is locally stuck around visualCross 941.  The
remaining hotspots are long fan edges into high-degree hubs such as
VentureCapital, Address, Company, and Meeting.  This prototype keeps straight
segments, but replaces selected fan groups with a small rendered carrier box:

    source -> carrier -> hub

The carrier is a visual relation object with a count label, not a polyline
detour.  The script is intentionally standalone so the existing exact and port
views remain comparable.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import math
import sys
import time
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np

ROOT = Path(__file__).parent

EXACT_SPEC = importlib.util.spec_from_file_location(
    "v35_exact_relation_search",
    ROOT / "v35_exact_relation_search.py",
)
exact = importlib.util.module_from_spec(EXACT_SPEC)
sys.modules["v35_exact_relation_search"] = exact
assert EXACT_SPEC.loader is not None
EXACT_SPEC.loader.exec_module(exact)

PORT_SPEC = importlib.util.spec_from_file_location(
    "v35_port_assignment_view",
    ROOT / "v35_port_assignment_view.py",
)
port_view = importlib.util.module_from_spec(PORT_SPEC)
sys.modules["v35_port_assignment_view"] = port_view
assert PORT_SPEC.loader is not None
PORT_SPEC.loader.exec_module(port_view)


def model_app(model_id: str) -> str:
    if model_id.startswith("leaf-bundle-"):
        return "bundle"
    return model_id.split(".", 1)[0] if "." in model_id else "misc"


def relation_label(edge_id: object) -> str:
    text = str(edge_id or "")
    if ":declared:" not in text:
        return text
    return text.rsplit(":", 1)[-1]


def semantic_key(rect: dict) -> str:
    rid = str(rect["id"])
    cluster = str(rect.get("cluster") or "")
    if cluster:
        return f"cluster:{cluster}"
    app = model_app(rid)
    if "." not in rid:
        return f"app:{app}"
    _app, tokens = exact.v34.semantic_name_parts(rid)
    token = sorted(tokens)[0] if tokens else rid.rsplit(".", 1)[-1].lower()
    return f"app:{app}:token:{token}"


def rect_center(rect: dict) -> np.ndarray:
    return np.array(
        [float(rect["x"]) + float(rect["w"]) / 2.0, float(rect["y"]) + float(rect["h"]) / 2.0],
        dtype=np.float64,
    )


def edge_segments(edges: list[dict]) -> np.ndarray:
    return np.array(
        [
            [
                [float(edge["points"][0]["x"]), float(edge["points"][0]["y"])],
                [float(edge["points"][1]["x"]), float(edge["points"][1]["y"])],
            ]
            for edge in edges
        ],
        dtype=np.float64,
    )


def build_edge_pairs(edges: list[dict]) -> tuple[np.ndarray, np.ndarray]:
    pair_i: list[int] = []
    pair_j: list[int] = []
    for i, left in enumerate(edges):
        ls = str(left["source"])
        lt = str(left["target"])
        for j in range(i + 1, len(edges)):
            right = edges[j]
            if ls in (right["source"], right["target"]) or lt in (right["source"], right["target"]):
                continue
            pair_i.append(i)
            pair_j.append(j)
    return np.array(pair_i, dtype=np.int32), np.array(pair_j, dtype=np.int32)


def overlap_count(rects: list[dict]) -> int:
    count = 0
    for i, left in enumerate(rects):
        lx0 = float(left["x"])
        ly0 = float(left["y"])
        lx1 = lx0 + float(left["w"])
        ly1 = ly0 + float(left["h"])
        for right in rects[i + 1 :]:
            rx0 = float(right["x"])
            ry0 = float(right["y"])
            rx1 = rx0 + float(right["w"])
            ry1 = ry0 + float(right["h"])
            if lx0 < rx1 and lx1 > rx0 and ly0 < ry1 and ly1 > ry0:
                count += 1
    return count


def metric(rects: list[dict], edges: list[dict]) -> dict:
    segments = edge_segments(edges)
    pair_i, pair_j = build_edge_pairs(edges)
    cross = int(exact.pair_cross_flags(segments, pair_i, pair_j).sum())
    rect_positions = np.array([rect_center(rect) for rect in rects], dtype=np.float64)
    widths = np.array([float(rect["w"]) for rect in rects], dtype=np.float64)
    heights = np.array([float(rect["h"]) for rect in rects], dtype=np.float64)
    rect_index = {str(rect["id"]): idx for idx, rect in enumerate(rects)}
    pair_edge: list[int] = []
    pair_rect: list[int] = []
    for edge_idx, edge in enumerate(edges):
        source_idx = rect_index[str(edge["source"])]
        target_idx = rect_index[str(edge["target"])]
        for rect_idx in range(len(rects)):
            if rect_idx == source_idx or rect_idx == target_idx:
                continue
            pair_edge.append(edge_idx)
            pair_rect.append(rect_idx)
    edge_rect = int(
        exact.edge_rect_flags(
            segments,
            rect_positions,
            widths,
            heights,
            np.array(pair_edge, dtype=np.int32),
            np.array(pair_rect, dtype=np.int32),
            0.0,
        ).sum()
    )
    overlaps = overlap_count(rects)
    return {
        "edgeCross": cross,
        "edgeRect": edge_rect,
        "overlaps": overlaps,
        "visualCross": cross + edge_rect + overlaps,
    }


def make_segment(rects_by_id: dict[str, dict], source_id: str, target_id: str) -> list[dict]:
    source = rects_by_id[source_id]
    target = rects_by_id[target_id]
    source_center = rect_center(source)
    target_center = rect_center(target)
    a = exact.rect_port(source_center, float(source["w"]), float(source["h"]), target_center)
    b = exact.rect_port(target_center, float(target["w"]), float(target["h"]), source_center)
    return [{"x": float(a[0]), "y": float(a[1])}, {"x": float(b[0]), "y": float(b[1])}]


def carrier_rect(
    carrier_id: str,
    label: str,
    cluster: str,
    center: np.ndarray,
    count: int,
) -> dict:
    width = max(210.0, min(360.0, 150.0 + 22.0 * len(label)))
    height = 74.0 if count <= 5 else 90.0
    return {
        "id": carrier_id,
        "label": label,
        "cluster": cluster,
        "kind": "carrier",
        "x": float(center[0] - width / 2.0),
        "y": float(center[1] - height / 2.0),
        "w": width,
        "h": height,
    }


def choose_carrier_center(
    rects: list[dict],
    hub: dict,
    sources: list[dict],
    occupied_rects: list[dict],
) -> np.ndarray:
    source_center = np.mean([rect_center(rect) for rect in sources], axis=0)
    hub_center = rect_center(hub)
    toward_hub = exact.unit(hub_center - source_center)
    if toward_hub is None:
        toward_hub = np.array([1.0, 0.0], dtype=np.float64)
    normal = np.array([-toward_hub[1], toward_hub[0]], dtype=np.float64)
    distances = [280.0, 520.0, 820.0, 1180.0]
    offsets = [0.0, 260.0, -260.0, 520.0, -520.0, 900.0, -900.0]
    best: tuple[int, float, np.ndarray] | None = None
    probe_w = 280.0
    probe_h = 82.0
    for distance in distances:
        for offset in offsets:
            center = source_center + toward_hub * distance + normal * offset
            x0 = center[0] - probe_w / 2.0
            x1 = center[0] + probe_w / 2.0
            y0 = center[1] - probe_h / 2.0
            y1 = center[1] + probe_h / 2.0
            overlaps = 0
            clearance = float("inf")
            for rect in occupied_rects:
                rx0 = float(rect["x"])
                rx1 = rx0 + float(rect["w"])
                ry0 = float(rect["y"])
                ry1 = ry0 + float(rect["h"])
                if x0 < rx1 and x1 > rx0 and y0 < ry1 and y1 > ry0:
                    overlaps += 1
                dx = max(rx0 - x1, x0 - rx1, 0.0)
                dy = max(ry0 - y1, y0 - ry1, 0.0)
                clearance = min(clearance, math.hypot(dx, dy))
            score = (overlaps, -clearance)
            if best is None or score < (best[0], best[1]):
                best = (overlaps, -clearance, center)
    assert best is not None
    return best[2]


def candidate_groups(
    rects: list[dict],
    edges: list[dict],
    min_group_size: int,
    max_hubs: int,
    *,
    group_by: str,
    include_bundles: bool,
) -> list[dict]:
    rects_by_id = {str(rect["id"]): rect for rect in rects}
    by_hub: dict[str, list[tuple[dict, str]]] = defaultdict(list)
    degree: Counter[str] = Counter()
    for edge in edges:
        degree[str(edge["source"])] += 1
        degree[str(edge["target"])] += 1
    hubs = {hub for hub, count in degree.most_common(max_hubs) if count >= min_group_size}
    for edge in edges:
        source = str(edge["source"])
        target = str(edge["target"])
        if source in hubs:
            by_hub[source].append((edge, target))
        if target in hubs:
            by_hub[target].append((edge, source))

    groups: list[dict] = []
    for hub_id, rows in by_hub.items():
        buckets: dict[str, list[tuple[dict, str]]] = defaultdict(list)
        for edge, source_id in rows:
            if not include_bundles and source_id.startswith("leaf-bundle-"):
                continue
            source_rect = rects_by_id[source_id]
            key = "hub" if group_by == "hub" else semantic_key(source_rect)
            buckets[key].append((edge, source_id))
        for key, bucket in buckets.items():
            unique_sources = sorted({source for _edge, source in bucket})
            if len(bucket) < min_group_size or len(unique_sources) < min_group_size:
                continue
            priority = float(len(bucket) * degree[hub_id])
            groups.append(
                {
                    "hub": hub_id,
                    "key": key,
                    "rows": bucket,
                    "sources": unique_sources,
                    "priority": priority,
                }
            )
    groups.sort(reverse=True, key=lambda row: row["priority"])
    return groups


def apply_carriers(
    rects: list[dict],
    edges: list[dict],
    *,
    max_groups: int,
    min_group_size: int,
    max_hubs: int,
    mode: str,
    group_by: str,
    include_bundles: bool,
) -> tuple[list[dict], list[dict], list[dict]]:
    rects_by_id = {str(rect["id"]): dict(rect) for rect in rects}
    used_edges: set[str] = set()
    carriers: list[dict] = []
    chosen_groups: list[dict] = []
    groups = candidate_groups(
        rects,
        edges,
        min_group_size,
        max_hubs,
        group_by=group_by,
        include_bundles=include_bundles,
    )
    for group in groups:
        available = [row for row in group["rows"] if str(row[0]["id"]) not in used_edges]
        sources = sorted({source for _edge, source in available})
        if len(available) < min_group_size or len(sources) < min_group_size:
            continue
        hub_id = str(group["hub"])
        hub = rects_by_id[hub_id]
        source_rects = [rects_by_id[source] for source in sources]
        carrier_id = f"hub-carrier:{len(carriers) + 1}:{hub_id}:{group['key']}"
        label = f"{hub_id.rsplit('.', 1)[-1]} x{len(available)}"
        center = choose_carrier_center(
            rects,
            hub,
            source_rects,
            list(rects_by_id.values()) + carriers,
        )
        carrier = carrier_rect(
            carrier_id,
            label,
            str(hub.get("cluster") or ""),
            center,
            len(available),
        )
        carriers.append(carrier)
        rects_by_id[carrier_id] = carrier
        for edge, _source in available:
            used_edges.add(str(edge["id"]))
        chosen_groups.append({**group, "rows": available, "sources": sources, "carrier": carrier})
        if len(chosen_groups) >= max_groups:
            break

    out_rects = [dict(rect) for rect in rects] + carriers
    out_by_id = {str(rect["id"]): rect for rect in out_rects}
    out_edges: list[dict] = []
    for edge in edges:
        if str(edge["id"]) not in used_edges:
            out_edges.append(dict(edge))

    for group in chosen_groups:
        carrier_id = str(group["carrier"]["id"])
        hub_id = str(group["hub"])
        if mode == "collapse":
            out_edges.append(
                {
                    "id": f"carrier-hub:{carrier_id}",
                    "source": carrier_id,
                    "target": hub_id,
                    "carrier": carrier_id,
                    "count": len(group["rows"]),
                    "points": make_segment(out_by_id, carrier_id, hub_id),
                }
            )
            continue
        for edge, source_id in group["rows"]:
            original_source = str(edge["source"])
            if original_source == hub_id:
                left, right = hub_id, str(source_id)
            else:
                left, right = str(source_id), carrier_id
            out_edges.append(
                {
                    "id": f"carrier-local:{edge['id']}",
                    "source": left,
                    "target": right,
                    "carrier": carrier_id,
                    "count": 1,
                    "points": make_segment(out_by_id, left, right),
                }
            )
        out_edges.append(
            {
                "id": f"carrier-hub:{carrier_id}",
                "source": carrier_id,
                "target": hub_id,
                "carrier": carrier_id,
                "count": len(group["rows"]),
                "points": make_segment(out_by_id, carrier_id, hub_id),
            }
        )
    return out_rects, out_edges, chosen_groups


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-json", type=Path, required=True)
    parser.add_argument("--out-html", type=Path, required=True)
    parser.add_argument("--out-json", type=Path, default=None)
    parser.add_argument("--title", default="v35 hub fan carrier view")
    parser.add_argument("--max-groups", type=int, default=16)
    parser.add_argument("--min-group-size", type=int, default=3)
    parser.add_argument("--max-hubs", type=int, default=30)
    parser.add_argument("--mode", choices=["two-hop", "collapse"], default="two-hop")
    parser.add_argument("--group-by", choices=["semantic", "hub"], default="semantic")
    parser.add_argument("--include-bundles", action="store_true")
    args = parser.parse_args()

    started = time.time()
    base = json.loads(args.input_json.read_text())
    rects = [dict(rect) for rect in base["rects"]]
    edges = [dict(edge) for edge in base["edges"]]
    base_metric = metric(rects, edges)
    carrier_rects, carrier_edges, groups = apply_carriers(
        rects,
        edges,
        max_groups=args.max_groups,
        min_group_size=args.min_group_size,
        max_hubs=args.max_hubs,
        mode=args.mode,
        group_by=args.group_by,
        include_bundles=args.include_bundles,
    )
    final_metric = metric(carrier_rects, carrier_edges)
    final_metric["elapsedSec"] = round(time.time() - started, 2)
    data = {
        "title": args.title,
        "metrics": final_metric,
        "baseMetrics": base_metric,
        "carrierGroups": [
            {
                "hub": group["hub"],
                "key": group["key"],
                "carrier": group["carrier"]["id"],
                "count": len(group["rows"]),
                "sources": sorted({source for _edge, source in group["rows"]}),
                "edgeIds": [str(edge["id"]) for edge, _source in group["rows"]],
                "relations": [
                    {
                        "id": str(edge["id"]),
                        "source": str(edge["source"]),
                        "target": str(edge["target"]),
                        "field": relation_label(edge.get("id")),
                        "points": edge.get("points") or [],
                    }
                    for edge, _source in group["rows"]
                ],
            }
            for group in groups
        ],
        "rects": carrier_rects,
        "edges": carrier_edges,
        "bounds": port_view.bounds(carrier_rects, carrier_edges),
    }
    port_view.write_html(args.out_html, data)
    if args.out_json is not None:
        args.out_json.write_text(json.dumps(data, indent=2))
    print(
        "base "
        f"edgeCross={base_metric['edgeCross']} edgeRect={base_metric['edgeRect']} "
        f"overlaps={base_metric['overlaps']} visual={base_metric['visualCross']}",
        flush=True,
    )
    print(
        "done "
        f"groups={len(groups)} rects={len(carrier_rects)} edges={len(carrier_edges)} "
        f"edgeCross={final_metric['edgeCross']} edgeRect={final_metric['edgeRect']} "
        f"overlaps={final_metric['overlaps']} visual={final_metric['visualCross']} "
        f"html={args.out_html}",
        flush=True,
    )


if __name__ == "__main__":
    main()
