#!/usr/bin/env python3
"""Prototype hub aliases that preserve every relation edge.

Carrier collapse reduces crossings but hides relation meaning.  This variant
keeps every original relation visible and instead creates visual aliases of
high-degree hub nodes near their source groups.  An edge that originally went
to db.Company can render to "Company alias" near the source cluster while the
box records aliasOf=db.Company.
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

HUB_RE = importlib.util.spec_from_file_location(
    "v35_hub_carrier_view",
    ROOT / "v35_hub_carrier_view.py",
)
hub_view = importlib.util.module_from_spec(HUB_RE)
sys.modules["v35_hub_carrier_view"] = hub_view
assert HUB_RE.loader is not None
HUB_RE.loader.exec_module(hub_view)


def rect_center(rect: dict) -> np.ndarray:
    return np.array(
        [float(rect["x"]) + float(rect["w"]) / 2.0, float(rect["y"]) + float(rect["h"]) / 2.0],
        dtype=np.float64,
    )


def edge_metric(rects: list[dict], edges: list[dict]) -> dict:
    return hub_view.metric(rects, edges)


def semantic_key(rect: dict) -> str:
    cluster = str(rect.get("cluster") or "")
    if cluster:
        return f"cluster:{cluster}"
    rid = str(rect["id"])
    app, tokens = exact.v34.semantic_name_parts(rid) if "." in rid else (rid, set())
    token = sorted(tokens)[0] if tokens else rid.rsplit(".", 1)[-1].lower()
    return f"app:{app}:token:{token}"


def make_segment(rects_by_id: dict[str, dict], source_id: str, target_id: str) -> list[dict]:
    return hub_view.make_segment(rects_by_id, source_id, target_id)


def degree_hubs(edges: list[dict], min_degree: int, max_hubs: int) -> list[str]:
    degree: Counter[str] = Counter()
    for edge in edges:
        degree[str(edge["source"])] += 1
        degree[str(edge["target"])] += 1
    return [node for node, count in degree.most_common(max_hubs) if count >= min_degree]


def edge_issue_counts(rects: list[dict], edges: list[dict]) -> Counter[str]:
    counts: Counter[str] = Counter()
    if not edges:
        return counts
    segments = hub_view.edge_segments(edges)
    pair_i, pair_j = hub_view.build_edge_pairs(edges)
    cross_flags = exact.pair_cross_flags(segments, pair_i, pair_j)
    for edge_idx in pair_i[cross_flags]:
        counts[str(edges[int(edge_idx)]["id"])] += 1
    for edge_idx in pair_j[cross_flags]:
        counts[str(edges[int(edge_idx)]["id"])] += 1

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
    edge_rect_flags = exact.edge_rect_flags(
        segments,
        rect_positions,
        widths,
        heights,
        np.array(pair_edge, dtype=np.int32),
        np.array(pair_rect, dtype=np.int32),
        0.0,
    )
    pair_edge_arr = np.array(pair_edge, dtype=np.int32)
    for edge_idx in pair_edge_arr[edge_rect_flags]:
        counts[str(edges[int(edge_idx)]["id"])] += 1
    return counts


def grouped_hub_edges(
    rects_by_id: dict[str, dict],
    edges: list[dict],
    hub_id: str,
    *,
    min_group_size: int,
    mode: str,
) -> list[dict]:
    buckets: dict[str, list[tuple[dict, str]]] = defaultdict(list)
    for edge in edges:
        source = str(edge["source"])
        target = str(edge["target"])
        other = ""
        if source == hub_id:
            other = target
        elif target == hub_id:
            other = source
        if not other or other not in rects_by_id:
            continue
        other_rect = rects_by_id[other]
        if mode == "edge":
            key = str(edge["id"])
        elif mode == "hub":
            key = "hub"
        elif mode == "app":
            key = other.split(".", 1)[0] if "." in other else other
        else:
            key = semantic_key(other_rect)
        buckets[key].append((edge, other))

    groups: list[dict] = []
    for key, rows in buckets.items():
        sources = sorted({other for _edge, other in rows})
        if len(rows) < min_group_size or len(sources) < min_group_size:
            continue
        groups.append({"hub": hub_id, "key": key, "rows": rows, "sources": sources})
    groups.sort(reverse=True, key=lambda group: len(group["rows"]))
    return groups


def candidate_alias_groups(
    rects: list[dict],
    edges: list[dict],
    *,
    max_hubs: int,
    min_degree: int,
    min_group_size: int,
    group_mode: str,
    max_aliases_per_hub: int,
) -> list[dict]:
    rects_by_id = {str(rect["id"]): dict(rect) for rect in rects}
    groups: list[dict] = []
    for hub_id in degree_hubs(edges, min_degree, max_hubs):
        if hub_id not in rects_by_id:
            continue
        groups.extend(
            grouped_hub_edges(
                rects_by_id,
                edges,
                hub_id,
                min_group_size=min_group_size,
                mode=group_mode,
            )[:max_aliases_per_hub]
        )
    return groups


def alias_rect(
    alias_id: str,
    hub: dict,
    center: np.ndarray,
    count: int,
    group_key: str,
) -> dict:
    if count == 1:
        label = str(hub["label"])
        width = max(96.0, min(190.0, 34.0 + len(label) * 9.0))
        height = 44.0
    else:
        label = f"{str(hub['label'])} a{count}"
        width = max(170.0, min(320.0, 130.0 + len(label) * 10.0))
        height = 62.0 if count <= 5 else 78.0
    return {
        "id": alias_id,
        "label": label,
        "cluster": str(hub.get("cluster") or ""),
        "kind": "alias",
        "aliasOf": str(hub["id"]),
        "aliasKey": group_key,
        "x": float(center[0] - width / 2.0),
        "y": float(center[1] - height / 2.0),
        "w": width,
        "h": height,
    }


def collides(center: np.ndarray, width: float, height: float, rects: list[dict]) -> int:
    x0 = float(center[0] - width / 2.0)
    x1 = float(center[0] + width / 2.0)
    y0 = float(center[1] - height / 2.0)
    y1 = float(center[1] + height / 2.0)
    count = 0
    for rect in rects:
        rx0 = float(rect["x"])
        rx1 = rx0 + float(rect["w"])
        ry0 = float(rect["y"])
        ry1 = ry0 + float(rect["h"])
        if x0 < rx1 and x1 > rx0 and y0 < ry1 and y1 > ry0:
            count += 1
    return count


def choose_alias_center(
    hub: dict,
    source_rects: list[dict],
    occupied: list[dict],
    *,
    width: float,
    height: float,
) -> np.ndarray:
    source_center = np.mean([rect_center(rect) for rect in source_rects], axis=0)
    hub_center = rect_center(hub)
    away = exact.unit(source_center - hub_center)
    if away is None:
        away = np.array([1.0, 0.0], dtype=np.float64)
    normal = np.array([-away[1], away[0]], dtype=np.float64)
    candidates: list[np.ndarray] = []
    for distance in (160.0, 300.0, 520.0, 820.0, 1250.0):
        for offset in (0.0, 220.0, -220.0, 460.0, -460.0, 780.0, -780.0):
            candidates.append(source_center + away * distance + normal * offset)
    best: tuple[int, float, np.ndarray] | None = None
    for center in candidates:
        overlap = collides(center, width, height, occupied)
        dist = float(np.linalg.norm(center - source_center))
        score = (overlap, dist)
        if best is None or score < (best[0], best[1]):
            best = (overlap, dist, center)
    assert best is not None
    return best[2]


def apply_aliases(
    rects: list[dict],
    edges: list[dict],
    *,
    max_hubs: int,
    min_degree: int,
    min_group_size: int,
    group_mode: str,
    max_aliases_per_hub: int,
) -> tuple[list[dict], list[dict], list[dict]]:
    groups = candidate_alias_groups(
        rects,
        edges,
        max_hubs=max_hubs,
        min_degree=min_degree,
        min_group_size=min_group_size,
        group_mode=group_mode,
        max_aliases_per_hub=max_aliases_per_hub,
    )
    return materialize_aliases(rects, edges, groups)


def materialize_aliases(
    rects: list[dict],
    edges: list[dict],
    groups: list[dict],
) -> tuple[list[dict], list[dict], list[dict]]:
    rects_by_id = {str(rect["id"]): dict(rect) for rect in rects}
    aliases: list[dict] = []
    alias_groups: list[dict] = []
    edge_rewrite: dict[str, str] = {}
    used_edges: set[str] = set()

    for group in groups:
        hub_id = str(group["hub"])
        if hub_id not in rects_by_id:
            continue
        hub = rects_by_id[hub_id]
        rows = [
            (edge, other)
            for edge, other in group["rows"]
            if str(edge["id"]) not in used_edges
        ]
        if not rows:
            continue
        sources = sorted({other for _edge, other in rows})
        alias_id = f"alias:{len(aliases) + 1}:{hub_id}:{group['key']}"
        width = max(120.0, min(float(hub["w"]), 220.0))
        height = max(44.0, min(float(hub["h"]), 126.0))
        source_rects = [rects_by_id[source] for source in sources if source in rects_by_id]
        if not source_rects:
            continue
        center = choose_alias_center(
            hub,
            source_rects,
            list(rects_by_id.values()) + aliases,
            width=width,
            height=height,
        )
        alias = alias_rect(alias_id, hub, center, len(rows), str(group["key"]))
        aliases.append(alias)
        rects_by_id[alias_id] = alias
        relations = []
        for edge, _other in rows:
            edge_id = str(edge["id"])
            edge_rewrite[edge_id] = alias_id
            used_edges.add(edge_id)
            relations.append(
                {
                    "id": edge_id,
                    "source": str(edge["source"]),
                    "target": str(edge["target"]),
                    "field": hub_view.relation_label(edge.get("id")),
                    "points": edge.get("points") or [],
                }
            )
        alias_groups.append(
            {
                "hub": hub_id,
                "alias": alias_id,
                "key": group["key"],
                "count": len(rows),
                "sources": sources,
                "relations": relations,
            }
        )

    out_rects = [dict(rect) for rect in rects] + aliases
    out_by_id = {str(rect["id"]): rect for rect in out_rects}
    out_edges: list[dict] = []
    for edge in edges:
        edge_id = str(edge["id"])
        if edge_id not in edge_rewrite:
            out_edges.append(dict(edge))
            continue
        alias_id = edge_rewrite[edge_id]
        source = str(edge["source"])
        target = str(edge["target"])
        alias_of = str(out_by_id[alias_id]["aliasOf"])
        if source == alias_of:
            source = alias_id
        if target == alias_of:
            target = alias_id
        row = dict(edge)
        row["source"] = source
        row["target"] = target
        row["aliasOf"] = alias_of
        row["points"] = make_segment(out_by_id, source, target)
        out_edges.append(row)
    return out_rects, out_edges, alias_groups


def apply_aliases_greedy(
    rects: list[dict],
    edges: list[dict],
    *,
    max_hubs: int,
    min_degree: int,
    min_group_size: int,
    group_mode: str,
    max_aliases_per_hub: int,
    greedy_rounds: int,
    candidate_limit: int,
    min_gain: int,
    target_visual_cross: int,
) -> tuple[list[dict], list[dict], list[dict]]:
    candidates = candidate_alias_groups(
        rects,
        edges,
        max_hubs=max_hubs,
        min_degree=min_degree,
        min_group_size=min_group_size,
        group_mode=group_mode,
        max_aliases_per_hub=max_aliases_per_hub,
    )
    current_rects = [dict(rect) for rect in rects]
    current_edges = [dict(edge) for edge in edges]
    selected: list[dict] = []
    used_edge_ids: set[str] = set()
    current_metric = edge_metric(current_rects, current_edges)
    for round_idx in range(max(0, greedy_rounds)):
        issues = edge_issue_counts(current_rects, current_edges)

        def candidate_score(group: dict) -> int:
            return sum(issues[str(edge["id"])] for edge, _other in group["rows"])

        available = [
            group
            for group in candidates
            if not any(str(edge["id"]) in used_edge_ids for edge, _other in group["rows"])
        ]
        available.sort(reverse=True, key=candidate_score)
        best_gain = 0
        best_group: dict | None = None
        best_layout: tuple[list[dict], list[dict], list[dict]] | None = None
        best_metric: dict | None = None
        measured = 0
        for group in available[:candidate_limit]:
            if candidate_score(group) <= 0:
                continue
            test_groups = selected + [group]
            test_rects, test_edges, test_alias_groups = materialize_aliases(rects, edges, test_groups)
            test_metric = edge_metric(test_rects, test_edges)
            measured += 1
            gain = int(current_metric["visualCross"]) - int(test_metric["visualCross"])
            if gain > best_gain:
                best_gain = gain
                best_group = group
                best_layout = (test_rects, test_edges, test_alias_groups)
                best_metric = test_metric
        if best_group is None or best_layout is None or best_metric is None or best_gain < min_gain:
            print(
                f"greedy round {round_idx + 1}: stop bestGain={best_gain} measured={measured} "
                f"visual={current_metric['visualCross']}",
                flush=True,
            )
            break
        selected.append(best_group)
        for edge, _other in best_group["rows"]:
            used_edge_ids.add(str(edge["id"]))
        current_rects, current_edges, alias_groups = best_layout
        current_metric = best_metric
        print(
            f"greedy round {round_idx + 1}: accept hub={best_group['hub']} "
            f"key={best_group['key']} edges={len(best_group['rows'])} "
            f"gain={best_gain} visual={current_metric['visualCross']} measured={measured}",
            flush=True,
        )
        if target_visual_cross > 0 and int(current_metric["visualCross"]) <= target_visual_cross:
            print(
                f"greedy round {round_idx + 1}: reached target visual={current_metric['visualCross']}",
                flush=True,
            )
            break
    if not selected:
        return current_rects, current_edges, []
    return current_rects, current_edges, alias_groups


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-json", type=Path, required=True)
    parser.add_argument("--out-html", type=Path, required=True)
    parser.add_argument("--out-json", type=Path, default=None)
    parser.add_argument("--title", default="v35 hub alias view")
    parser.add_argument("--max-hubs", type=int, default=5)
    parser.add_argument("--min-degree", type=int, default=12)
    parser.add_argument("--min-group-size", type=int, default=3)
    parser.add_argument("--group-mode", choices=["semantic", "app", "hub", "edge"], default="semantic")
    parser.add_argument("--max-aliases-per-hub", type=int, default=12)
    parser.add_argument("--select-mode", choices=["all", "greedy"], default="all")
    parser.add_argument("--greedy-rounds", type=int, default=24)
    parser.add_argument("--candidate-limit", type=int, default=32)
    parser.add_argument("--min-gain", type=int, default=1)
    parser.add_argument("--target-visual-cross", type=int, default=0)
    args = parser.parse_args()

    started = time.time()
    base = json.loads(args.input_json.read_text())
    rects = [dict(rect) for rect in base["rects"]]
    edges = [dict(edge) for edge in base["edges"]]
    base_metric = edge_metric(rects, edges)
    if args.select_mode == "greedy":
        alias_rects, alias_edges, alias_groups = apply_aliases_greedy(
            rects,
            edges,
            max_hubs=args.max_hubs,
            min_degree=args.min_degree,
            min_group_size=args.min_group_size,
            group_mode=args.group_mode,
            max_aliases_per_hub=args.max_aliases_per_hub,
            greedy_rounds=args.greedy_rounds,
            candidate_limit=args.candidate_limit,
            min_gain=args.min_gain,
            target_visual_cross=args.target_visual_cross,
        )
    else:
        alias_rects, alias_edges, alias_groups = apply_aliases(
            rects,
            edges,
            max_hubs=args.max_hubs,
            min_degree=args.min_degree,
            min_group_size=args.min_group_size,
            group_mode=args.group_mode,
            max_aliases_per_hub=args.max_aliases_per_hub,
        )
    final_metric = edge_metric(alias_rects, alias_edges)
    final_metric["elapsedSec"] = round(time.time() - started, 2)
    data = {
        "title": args.title,
        "metrics": final_metric,
        "baseMetrics": base_metric,
        "aliasGroups": alias_groups,
        "rects": alias_rects,
        "edges": alias_edges,
        "bounds": port_view.bounds(alias_rects, alias_edges),
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
        f"aliases={len(alias_groups)} rects={len(alias_rects)} edges={len(alias_edges)} "
        f"edgeCross={final_metric['edgeCross']} edgeRect={final_metric['edgeRect']} "
        f"overlaps={final_metric['overlaps']} visual={final_metric['visualCross']} "
        f"html={args.out_html}",
        flush=True,
    )


if __name__ == "__main__":
    main()
