#!/usr/bin/env python3
"""v34 prototype: exact-verified generic move search for ERD layouts.

This is intentionally not a §13 clone. It uses generic move primitives
(node nudges, cluster translations, local swaps), ranks candidates by exact
straight-line crossing / rectangle-overlap / bbox score, and accepts only
measured improvements. The next step is to log these counterfactuals and
train an ML scorer to rank candidates before exact verification.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import subprocess
import tempfile
import time
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path

import numpy as np

# Directory of this file — used to locate sibling modules (e.g.,
# metrics_extended) when composite-weighted scoring is enabled.
_THIS_DIR = Path(__file__).resolve().parent

import importlib.util

SPEC = importlib.util.spec_from_file_location(
    "fast_cross_eval", Path(__file__).parent / "fast_cross_eval.py"
)
fce = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(fce)

ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / "bin/ogdf/darwin-arm64/django-erd-ogdf-layout"
NODES_TSV = ROOT / "data/erd-poc/graphs/real-main/nodes.tsv"
EDGES_TSV = ROOT / "data/erd-poc/graphs/real-main/edges.tsv"


@dataclass
class Metrics:
    cross: int
    overlaps: int
    bbox_b: float
    score: float
    edge_node: int = 0

    @property
    def visual_cross(self) -> int:
        return int(self.cross) + int(self.edge_node)


@dataclass
class Candidate:
    kind: str
    indices: np.ndarray
    dx: float = 0.0
    dy: float = 0.0
    other: np.ndarray | None = None
    targets: np.ndarray | None = None
    group_key: str = ""
    other_group_key: str = ""
    source_group_key: str = ""
    semantic_score: float = 0.0
    semantic_token_jaccard: float = 0.0
    semantic_shared_tokens: int = 0
    semantic_app_match: float = 0.0
    semantic_target_size: int = 0
    semantic_target_cross_incident: float = 0.0


@dataclass
class CompositeContext:
    """Bundle the layout-static data needed to compute composite quality
    inside `measure`. Passed in once per run; reused for every candidate
    evaluation so the per-call cost is just one metrics_extended call."""
    widths: np.ndarray
    heights: np.ndarray
    cluster_ids: list[str]
    weight: float  # composite_weight in the score formula


@dataclass
class CollisionGeometry:
    """Visible rectangles used for render-faithful collision scoring.

    Edge crossing still uses the original graph node centers. Overlap,
    edge-node, and bbox scoring use these rectangles because the webview may
    render a catalog-sized table or a synthetic leaf-bundle table whose size
    differs from the raw OGDF node size.
    """

    rect_node: np.ndarray
    rect_widths: np.ndarray
    rect_heights: np.ndarray
    rect_raw_widths: np.ndarray
    rect_raw_heights: np.ndarray
    raw_node_widths: np.ndarray
    raw_node_heights: np.ndarray
    rect_bundle_leaves: tuple[np.ndarray, ...]
    rect_effect_nodes_by_rect: tuple[tuple[int, ...], ...]
    rect_effect_nodes_by_node: tuple[tuple[int, ...], ...]
    rect_edge_exempt_nodes: tuple[frozenset[int], ...]
    overlap_pairs: tuple[np.ndarray, np.ndarray]
    edge_node_pairs: tuple[np.ndarray, np.ndarray]


def group_has_louvain(key: str) -> bool:
    return "_louv" in key


def group_is_pseudo(key: str) -> bool:
    return key.startswith("__pseudo_")


def group_is_component(key: str) -> bool:
    return key.startswith("__component__")


def compact_group_key(keys: list[str], prefix: str, limit: int = 8) -> str:
    head = "|".join(keys[:limit])
    if len(keys) > limit:
        head += f"|+{len(keys) - limit}"
    return prefix + head


def candidate_priority(cand: Candidate, node_counts: np.ndarray) -> float:
    total = float(node_counts[cand.indices].sum())
    if cand.other is not None:
        total += float(node_counts[cand.other].sum())
    # Large group moves can be powerful, but raw sums over-rank huge clusters.
    crossing_priority = total / math.sqrt(max(1, cand.indices.shape[0]))
    if cand.kind in {
        "cross_edge_translate",
        "cross_pair_edge_spread",
        "cross_fan_edge_translate",
        "cross_fan_endpoint_translate",
        "cross_group_fan_translate",
        "cross_carrier_pair_separate",
        "cross_hotspot_endpoint_bypass",
        "cross_endpoint_partner_orbit",
        "cross_pair_endpoint_swap",
        "cross_long_edge_endpoint_anchor",
        "cross_long_edge_group_anchor",
        "cross_long_edge_group_pair_anchor",
        "edge_node_blocker_translate",
        "edge_node_blocker_precise_clear",
        "edge_node_neighborhood_precise_clear",
        "edge_node_corridor_clear",
        "edge_node_force_relax",
        "edge_node_group_translate",
        "edge_node_edge_translate",
        "edge_node_endpoint_translate",
    }:
        crossing_priority *= 4.0
        crossing_priority = max(
            crossing_priority,
            float(cand.semantic_target_cross_incident) * 4.0,
        )
        if cand.kind in {"cross_endpoint_partner_orbit", "cross_pair_endpoint_swap"}:
            crossing_priority = max(
                crossing_priority,
                float(cand.semantic_target_cross_incident) * 12.0,
            )
    semantic_priority = cand.semantic_score * 2500.0
    return max(crossing_priority, semantic_priority)


def candidate_features(cand: Candidate, positions: np.ndarray, node_counts: np.ndarray) -> dict:
    idx = cand.indices
    pts = positions[idx]
    counts = node_counts[idx]
    centroid = pts.mean(axis=0)
    payload = {
        "kind": cand.kind,
        "groupKey": cand.group_key,
        "otherGroupKey": cand.other_group_key,
        "sourceGroupKey": cand.source_group_key,
        "groupIsLouvain": 1.0 if group_has_louvain(cand.group_key) else 0.0,
        "otherGroupIsLouvain": 1.0 if group_has_louvain(cand.other_group_key) else 0.0,
        "sourceGroupIsLouvain": 1.0 if group_has_louvain(cand.source_group_key) else 0.0,
        "groupIsPseudo": 1.0 if group_is_pseudo(cand.group_key) else 0.0,
        "otherGroupIsPseudo": 1.0 if group_is_pseudo(cand.other_group_key) else 0.0,
        "groupIsComponent": 1.0 if group_is_component(cand.group_key) else 0.0,
        "semanticScore": cand.semantic_score,
        "semanticTokenJaccard": cand.semantic_token_jaccard,
        "semanticSharedTokens": cand.semantic_shared_tokens,
        "semanticAppMatch": cand.semantic_app_match,
        "semanticTargetSize": cand.semantic_target_size,
        "semanticTargetCrossIncident": cand.semantic_target_cross_incident,
        "moved": int(idx.shape[0]),
        "dx": cand.dx,
        "dy": cand.dy,
        "priority": candidate_priority(cand, node_counts),
        "meanCrossIncident": float(counts.mean()) if counts.size else 0.0,
        "maxCrossIncident": int(counts.max()) if counts.size else 0,
        "centroidX": float(centroid[0]),
        "centroidY": float(centroid[1]),
        "spreadX": float(pts[:, 0].max() - pts[:, 0].min()),
        "spreadY": float(pts[:, 1].max() - pts[:, 1].min()),
    }
    if cand.targets is not None:
        target_centroid = cand.targets.mean(axis=0)
        target_delta = cand.targets - pts
        target_dist = np.linalg.norm(target_delta, axis=1)
        payload["targetSpreadX"] = float(cand.targets[:, 0].max() - cand.targets[:, 0].min())
        payload["targetSpreadY"] = float(cand.targets[:, 1].max() - cand.targets[:, 1].min())
        payload["targetCentroidX"] = float(target_centroid[0])
        payload["targetCentroidY"] = float(target_centroid[1])
        payload["targetDeltaX"] = float(target_centroid[0] - centroid[0])
        payload["targetDeltaY"] = float(target_centroid[1] - centroid[1])
        payload["targetMoveDistMean"] = float(target_dist.mean()) if target_dist.size else 0.0
        payload["targetMoveDistMax"] = float(target_dist.max()) if target_dist.size else 0.0
        payload["targetMoveDistStd"] = float(target_dist.std()) if target_dist.size else 0.0
    else:
        move_dist = math.hypot(cand.dx, cand.dy)
        payload["targetCentroidX"] = float(centroid[0] + cand.dx)
        payload["targetCentroidY"] = float(centroid[1] + cand.dy)
        payload["targetDeltaX"] = float(cand.dx)
        payload["targetDeltaY"] = float(cand.dy)
        payload["targetMoveDistMean"] = float(move_dist)
        payload["targetMoveDistMax"] = float(move_dist)
        payload["targetMoveDistStd"] = 0.0
    if cand.other is not None:
        other = cand.other
        other_pts = positions[other]
        payload["swapDistance"] = float(np.linalg.norm(pts.mean(axis=0) - other_pts.mean(axis=0)))
        payload["otherMeanCrossIncident"] = float(node_counts[other].mean())
    return payload


ACTION_TYPES = {
    "node": "node_translate",
    "neighbor_anchor": "node_anchor_to_neighbors",
    "edge_endpoint": "edge_endpoint_translate",
    "edge_translate": "edge_translate",
    "cross_edge_translate": "crossing_edge_translate",
    "cross_pair_edge_spread": "crossing_pair_edge_spread",
    "cross_fan_edge_translate": "crossing_fan_edge_translate",
    "cross_fan_endpoint_translate": "crossing_fan_endpoint_translate",
    "cross_group_fan_translate": "crossing_group_fan_translate",
    "cross_carrier_pair_separate": "crossing_carrier_pair_separate",
    "cross_hotspot_endpoint_bypass": "crossing_hotspot_endpoint_bypass",
    "cross_endpoint_partner_orbit": "crossing_endpoint_partner_orbit",
    "cross_pair_endpoint_swap": "crossing_pair_endpoint_swap",
    "cross_long_edge_endpoint_anchor": "crossing_long_edge_endpoint_anchor",
    "cross_long_edge_group_anchor": "crossing_long_edge_group_anchor",
    "cross_long_edge_group_pair_anchor": "crossing_long_edge_group_pair_anchor",
    "edge_node_blocker_translate": "edge_node_blocker_translate",
    "edge_node_blocker_precise_clear": "edge_node_blocker_precise_clear",
    "edge_node_neighborhood_precise_clear": "edge_node_neighborhood_precise_clear",
    "edge_node_corridor_clear": "edge_node_corridor_clear",
    "edge_node_force_relax": "edge_node_force_relax",
    "edge_node_group_translate": "edge_node_group_translate",
    "edge_node_edge_translate": "edge_node_edge_translate",
    "edge_node_endpoint_translate": "edge_node_endpoint_translate",
    "cluster": "group_translate",
    "group_anchor": "group_anchor_to_neighbors",
    "component_anchor": "component_anchor_to_neighbors",
    "cluster_swap": "group_centroid_swap",
    "swap": "node_position_swap",
    "overlap_push": "overlap_single_push",
    "overlap_spread_line": "overlap_component_line",
    "overlap_spread_ring": "overlap_component_ring",
    "overlap_spread_batch": "overlap_components_batch_line",
    "set_positions": "set_positions",
}


def candidate_action_type(cand: Candidate) -> str:
    group_louvain = group_has_louvain(cand.group_key)
    other_louvain = group_has_louvain(cand.other_group_key)
    if cand.kind == "cluster" and group_louvain:
        return "louvain_cluster_translate"
    if cand.kind == "group_anchor" and group_louvain:
        return "louvain_cluster_anchor_to_neighbors"
    if cand.kind == "cluster_swap" and (group_louvain or other_louvain):
        return "louvain_cluster_centroid_swap"
    if cand.kind == "cross_group_fan_translate" and group_louvain:
        return "louvain_crossing_group_fan_translate"
    if cand.kind == "cross_carrier_pair_separate" and (
        group_louvain or other_louvain
    ):
        return "louvain_crossing_carrier_pair_separate"
    if cand.kind == "cross_long_edge_group_anchor" and group_louvain:
        return "louvain_crossing_long_edge_group_anchor"
    if cand.kind == "cross_long_edge_group_pair_anchor" and (
        group_louvain or other_louvain
    ):
        return "louvain_crossing_long_edge_group_pair_anchor"
    if cand.kind == "edge_node_group_translate" and group_louvain:
        return "louvain_edge_node_group_translate"
    if cand.kind == "cross_hotspot_endpoint_bypass" and group_louvain:
        return "louvain_crossing_hotspot_endpoint_bypass"
    if cand.kind == "component_anchor" and group_louvain:
        return "louvain_component_anchor_to_neighbors"
    if cand.kind == "semantic_anchor":
        if group_louvain:
            return "semantic_orphan_to_louvain_cluster"
        if group_is_pseudo(cand.group_key):
            return "semantic_orphan_to_pseudo_group"
        return "semantic_orphan_to_group"
    if cand.kind == "set_positions":
        if cand.indices.shape[0] == 1:
            return "node_set_position"
        if group_louvain:
            return "louvain_cluster_set_positions"
        return "group_set_positions"
    return ACTION_TYPES.get(cand.kind, cand.kind)


def compact_float(value: float) -> float:
    return round(float(value), 3)


def compact_points(points: np.ndarray) -> list[list[float]]:
    return [
        [compact_float(row[0]), compact_float(row[1])]
        for row in points
    ]


def candidate_action_record(
    cand: Candidate,
    model_ids: list[str],
    positions: np.ndarray,
) -> dict:
    """Serializable action contract for future graph-aware scorers.

    The scalar candidate feature block is intentionally lossy. This record
    preserves the actual action identity: moved node indices, optional paired
    nodes, and per-node target deltas for non-rigid set-position actions.
    """
    idx = [int(i) for i in cand.indices.tolist()]
    record: dict = {
        "type": candidate_action_type(cand),
        "kind": cand.kind,
        "groupKey": cand.group_key,
        "otherGroupKey": cand.other_group_key,
        "sourceGroupKey": cand.source_group_key,
        "groupIsLouvain": group_has_louvain(cand.group_key),
        "otherGroupIsLouvain": group_has_louvain(cand.other_group_key),
        "sourceGroupIsLouvain": group_has_louvain(cand.source_group_key),
        "groupIsPseudo": group_is_pseudo(cand.group_key),
        "otherGroupIsPseudo": group_is_pseudo(cand.other_group_key),
        "groupIsComponent": group_is_component(cand.group_key),
        "semanticScore": compact_float(cand.semantic_score),
        "semanticTokenJaccard": compact_float(cand.semantic_token_jaccard),
        "semanticSharedTokens": int(cand.semantic_shared_tokens),
        "semanticAppMatch": compact_float(cand.semantic_app_match),
        "semanticTargetSize": int(cand.semantic_target_size),
        "semanticTargetCrossIncident": compact_float(
            cand.semantic_target_cross_incident
        ),
        "moved": len(idx),
        "indices": idx,
        "modelIds": [model_ids[i] for i in idx],
    }
    if cand.other is not None:
        other = [int(i) for i in cand.other.tolist()]
        record["otherIndices"] = other
        record["otherModelIds"] = [model_ids[i] for i in other]
    if cand.targets is not None:
        deltas = cand.targets - positions[cand.indices]
        record["mode"] = "set_positions"
        record["targetDeltas"] = compact_points(deltas)
        record["targets"] = compact_points(cand.targets)
    else:
        record["mode"] = "translate"
        record["dx"] = compact_float(cand.dx)
        record["dy"] = compact_float(cand.dy)
    return record


def load_layout(path: Path) -> dict:
    return json.loads(path.read_text())


def layout_positions(layout: dict) -> np.ndarray:
    return np.array(
        [
            [
                nd["position"]["x"] + nd["size"]["width"] / 2.0,
                nd["position"]["y"] + nd["size"]["height"] / 2.0,
            ]
            for nd in layout["nodes"]
        ],
        dtype=np.float64,
    )


def read_positions_tsv(path: Path, layout: dict) -> np.ndarray:
    base = layout_positions(layout)
    id2 = {nd["modelId"]: i for i, nd in enumerate(layout["nodes"])}
    for line in path.read_text().splitlines():
        parts = line.split("\t")
        if len(parts) < 3:
            continue
        if parts[0] == "modelId":
            continue
        idx = id2.get(parts[0])
        if idx is None:
            continue
        try:
            base[idx, 0] = float(parts[1])
            base[idx, 1] = float(parts[2])
        except ValueError:
            continue
    return base


def write_positions_tsv(path: Path, layout: dict, positions: np.ndarray) -> None:
    with path.open("w") as out:
        for idx, nd in enumerate(layout["nodes"]):
            out.write(
                f"{nd['modelId']}\t{positions[idx, 0]:.3f}"
                f"\t{positions[idx, 1]:.3f}\n"
            )


def graph_edges(layout: dict) -> np.ndarray:
    id2 = {nd["modelId"]: i for i, nd in enumerate(layout["nodes"])}
    edges: list[tuple[int, int]] = []
    for re in layout.get("routedEdges", []):
        s = id2.get(re.get("sourceModelId"))
        t = id2.get(re.get("targetModelId"))
        if s is None or t is None or s == t:
            continue
        edges.append((s, t))
    return np.array(edges, dtype=np.int32)


def node_model_ids(layout: dict) -> list[str]:
    return [nd["modelId"] for nd in layout["nodes"]]


def node_sizes(layout: dict) -> tuple[np.ndarray, np.ndarray]:
    widths = np.array([nd["size"]["width"] for nd in layout["nodes"]], dtype=np.float64)
    heights = np.array([nd["size"]["height"] for nd in layout["nodes"]], dtype=np.float64)
    return widths, heights


MODEL_CATALOG_MODE_THRESHOLD = 500
CATALOG_BASE_TABLE_HEIGHT = 74.0
CATALOG_BASE_TABLE_WIDTH = 236.0
CATALOG_MAX_TABLE_HEIGHT = 434.0
CATALOG_MAX_TABLE_WIDTH = 396.0
LEAF_CELL_W = 200.0
LEAF_CELL_H = 56.0
LEAF_GAP_X = 10.0
LEAF_GAP_Y = 8.0
BUNDLE_HEADER = 48.0
BUNDLE_PAD = 16.0


def model_name_from_id(model_id: str) -> str:
    return model_id.rsplit(".", 1)[-1]


def database_name_from_id(model_id: str) -> str:
    if "." not in model_id:
        return model_name_from_id(model_id).lower()
    app, leaf = model_id.split(".", 1)
    return f"{app}_{leaf.rsplit('.', 1)[-1].lower()}"


def catalog_table_size(model_id: str, relation_degree: int) -> tuple[float, float]:
    pressure = max(0, int(relation_degree) - 4)
    label_width = max(len(model_name_from_id(model_id)), len(database_name_from_id(model_id)))
    width_from_text = max(
        CATALOG_BASE_TABLE_WIDTH,
        math.ceil(label_width * 7.4 + 32.0),
    )
    width = min(
        CATALOG_MAX_TABLE_WIDTH,
        width_from_text + min(144.0, math.ceil(pressure / 8.0) * 12.0),
    )
    height = min(
        CATALOG_MAX_TABLE_HEIGHT,
        CATALOG_BASE_TABLE_HEIGHT + min(360.0, math.ceil(pressure / 2.0) * 8.0),
    )
    return float(width), float(height)


def relation_degrees(edges: np.ndarray, n_nodes: int) -> np.ndarray:
    degree = np.zeros(n_nodes, dtype=np.int32)
    if edges.size:
        np.add.at(degree, edges[:, 0], 1)
        np.add.at(degree, edges[:, 1], 1)
    return degree


def bundle_leaf_index_sets(layout: dict) -> tuple[set[str], list[dict]]:
    leaf_ids: set[str] = set()
    bundles: list[dict] = []
    for bundle in (layout.get("engineMetadata") or {}).get("leafBundles") or []:
        leaves = [str(mid) for mid in (bundle.get("leafModelIds") or [])]
        if not leaves:
            continue
        leaf_ids.update(leaves)
        bundles.append(bundle)
    return leaf_ids, bundles


def render_node_sizes(layout: dict, edges: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Return webview-visible sizes for original layout nodes only.

    This mirrors createDiagramRenderModel.ts for the pieces the scorer can
    know from layout JSON: catalog-mode compact table sizing and bundled leaf
    tiles. Synthetic bundle tables are represented by CollisionGeometry.
    """
    raw_w, raw_h = node_sizes(layout)
    n_nodes = len(layout["nodes"])
    if n_nodes == 0:
        return raw_w, raw_h
    leaf_ids, _bundles = bundle_leaf_index_sets(layout)
    degrees = relation_degrees(edges, n_nodes)
    widths = raw_w.copy()
    heights = raw_h.copy()
    catalog_mode = n_nodes > MODEL_CATALOG_MODE_THRESHOLD
    for idx, nd in enumerate(layout["nodes"]):
        model_id = str(nd.get("modelId") or "")
        if model_id in leaf_ids:
            widths[idx] = LEAF_CELL_W
            heights[idx] = LEAF_CELL_H
        elif catalog_mode:
            widths[idx], heights[idx] = catalog_table_size(model_id, int(degrees[idx]))
    return widths, heights


def render_active_overlap_mask(layout: dict, count_bundle_nodes: bool = False) -> np.ndarray:
    """Original-node mask for render-aware candidate generation.

    Bundle leaves are represented by a synthetic bundle table in the webview,
    so default render scoring skips the raw leaf positions. Bundle parents
    stay visible as normal tables and must remain active.
    """
    if count_bundle_nodes:
        return np.ones(len(layout["nodes"]), dtype=bool)
    leaf_ids, _bundles = bundle_leaf_index_sets(layout)
    return np.array(
        [str(nd.get("modelId") or "") not in leaf_ids for nd in layout["nodes"]],
        dtype=bool,
    )


def active_overlap_mask(layout: dict) -> np.ndarray:
    absorbed: set[str] = set()
    for bundle in (layout.get("engineMetadata") or {}).get("leafBundles") or []:
        parent = bundle.get("parentModelId")
        if parent:
            absorbed.add(parent)
        for leaf in bundle.get("leafModelIds") or []:
            absorbed.add(leaf)
    return np.array(
        [nd["modelId"] not in absorbed for nd in layout["nodes"]],
        dtype=bool,
    )


def bundle_render_size(member_count: int) -> tuple[float, float]:
    n = max(1, int(member_count))
    cols = max(1, math.ceil(math.sqrt(n)))
    rows = max(1, math.ceil(n / cols))
    inner_w = cols * LEAF_CELL_W + (cols - 1) * LEAF_GAP_X
    inner_h = rows * LEAF_CELL_H + (rows - 1) * LEAF_GAP_Y
    return inner_w + BUNDLE_PAD * 2.0, BUNDLE_HEADER + inner_h + BUNDLE_PAD


def build_render_collision_geometry(
    layout: dict,
    edges: np.ndarray,
    count_bundle_nodes: bool = False,
) -> CollisionGeometry:
    """Build render-faithful rectangles for overlap / edge-node scoring."""
    nodes = layout["nodes"]
    n_nodes = len(nodes)
    raw_w, raw_h = node_sizes(layout)
    render_w, render_h = render_node_sizes(layout, edges)
    id_to_idx = {str(nd.get("modelId") or ""): idx for idx, nd in enumerate(nodes)}
    leaf_ids, bundles = bundle_leaf_index_sets(layout)

    rect_node: list[int] = []
    rect_widths: list[float] = []
    rect_heights: list[float] = []
    rect_raw_widths: list[float] = []
    rect_raw_heights: list[float] = []
    rect_bundle_leaves: list[np.ndarray] = []
    rect_effect_sets: list[set[int]] = []
    rect_edge_exempt_nodes: list[frozenset[int]] = []

    for idx, nd in enumerate(nodes):
        model_id = str(nd.get("modelId") or "")
        if model_id in leaf_ids and not count_bundle_nodes:
            continue
        rect_node.append(idx)
        rect_widths.append(float(render_w[idx]))
        rect_heights.append(float(render_h[idx]))
        rect_raw_widths.append(float(raw_w[idx]))
        rect_raw_heights.append(float(raw_h[idx]))
        rect_bundle_leaves.append(np.zeros(0, dtype=np.int32))
        rect_effect_sets.append({idx})
        rect_edge_exempt_nodes.append(frozenset({idx}))

    for bundle in bundles:
        leaf_indices = np.array(
            [
                id_to_idx[str(mid)]
                for mid in (bundle.get("leafModelIds") or [])
                if str(mid) in id_to_idx
            ],
            dtype=np.int32,
        )
        if leaf_indices.size == 0:
            continue
        width, height = bundle_render_size(int(leaf_indices.size))
        exempt: set[int] = set(int(i) for i in leaf_indices.tolist())
        parent = str(bundle.get("parentModelId") or "")
        if parent in id_to_idx:
            exempt.add(int(id_to_idx[parent]))
        for root in bundle.get("sharedRootModelIds") or []:
            root_id = str(root)
            if root_id in id_to_idx:
                exempt.add(int(id_to_idx[root_id]))
        rect_node.append(-1)
        rect_widths.append(float(width))
        rect_heights.append(float(height))
        rect_raw_widths.append(0.0)
        rect_raw_heights.append(0.0)
        rect_bundle_leaves.append(leaf_indices)
        rect_effect_sets.append(set(int(i) for i in leaf_indices.tolist()))
        rect_edge_exempt_nodes.append(frozenset(exempt))

    rect_count = len(rect_node)
    effect_by_node: list[list[int]] = [[] for _ in range(n_nodes)]
    for rect_idx, effect_set in enumerate(rect_effect_sets):
        for node_idx in effect_set:
            if 0 <= node_idx < n_nodes:
                effect_by_node[node_idx].append(rect_idx)

    overlap_pairs = build_overlap_pairs(np.ones(rect_count, dtype=bool))
    edge_node_pairs = build_render_edge_node_pairs(
        edges,
        tuple(rect_edge_exempt_nodes),
    )
    return CollisionGeometry(
        rect_node=np.array(rect_node, dtype=np.int32),
        rect_widths=np.array(rect_widths, dtype=np.float64),
        rect_heights=np.array(rect_heights, dtype=np.float64),
        rect_raw_widths=np.array(rect_raw_widths, dtype=np.float64),
        rect_raw_heights=np.array(rect_raw_heights, dtype=np.float64),
        raw_node_widths=raw_w.astype(np.float64, copy=True),
        raw_node_heights=raw_h.astype(np.float64, copy=True),
        rect_bundle_leaves=tuple(rect_bundle_leaves),
        rect_effect_nodes_by_rect=tuple(tuple(sorted(xs)) for xs in rect_effect_sets),
        rect_effect_nodes_by_node=tuple(tuple(xs) for xs in effect_by_node),
        rect_edge_exempt_nodes=tuple(rect_edge_exempt_nodes),
        overlap_pairs=overlap_pairs,
        edge_node_pairs=edge_node_pairs,
    )


def build_render_edge_node_pairs(
    edges: np.ndarray,
    rect_edge_exempt_nodes: tuple[frozenset[int], ...],
) -> tuple[np.ndarray, np.ndarray]:
    if edges.shape[0] == 0 or not rect_edge_exempt_nodes:
        return np.zeros(0, dtype=np.int32), np.zeros(0, dtype=np.int32)
    pair_edges: list[int] = []
    pair_rects: list[int] = []
    for edge_idx, (src_raw, dst_raw) in enumerate(edges):
        src = int(src_raw)
        dst = int(dst_raw)
        for rect_idx, exempt in enumerate(rect_edge_exempt_nodes):
            if src in exempt or dst in exempt:
                continue
            pair_edges.append(edge_idx)
            pair_rects.append(rect_idx)
    return np.array(pair_edges, dtype=np.int32), np.array(pair_rects, dtype=np.int32)


def collision_positions(
    positions: np.ndarray,
    geometry: CollisionGeometry,
) -> np.ndarray:
    out = np.zeros((geometry.rect_node.shape[0], 2), dtype=np.float64)
    for rect_idx, node_idx_raw in enumerate(geometry.rect_node):
        out[rect_idx] = collision_position_for_rect(
            positions,
            geometry,
            int(rect_idx),
        )
    return out


def collision_position_for_rect(
    positions: np.ndarray,
    geometry: CollisionGeometry,
    rect_idx: int,
) -> np.ndarray:
    node_idx = int(geometry.rect_node[rect_idx])
    if node_idx >= 0:
        return np.array(
            [
                positions[node_idx, 0]
                - geometry.rect_raw_widths[rect_idx] / 2.0
                + geometry.rect_widths[rect_idx] / 2.0,
                positions[node_idx, 1]
                - geometry.rect_raw_heights[rect_idx] / 2.0
                + geometry.rect_heights[rect_idx] / 2.0,
            ],
            dtype=np.float64,
        )
    leaves = geometry.rect_bundle_leaves[rect_idx]
    if leaves.size == 0:
        return np.zeros(2, dtype=np.float64)
    min_x = np.inf
    min_y = np.inf
    max_x = -np.inf
    max_y = -np.inf
    for leaf in leaves:
        lf = int(leaf)
        w = float(geometry.raw_node_widths[lf])
        h = float(geometry.raw_node_heights[lf])
        min_x = min(min_x, positions[lf, 0] - w / 2.0)
        max_x = max(max_x, positions[lf, 0] + w / 2.0)
        min_y = min(min_y, positions[lf, 1] - h / 2.0)
        max_y = max(max_y, positions[lf, 1] + h / 2.0)
    return np.array([(min_x + max_x) / 2.0, (min_y + max_y) / 2.0], dtype=np.float64)


def moved_collision_positions(
    current_rect_positions: np.ndarray,
    moved_positions: np.ndarray,
    geometry: CollisionGeometry,
    impacted_rects: np.ndarray,
) -> np.ndarray:
    moved_rect_positions = current_rect_positions.copy()
    for rect_idx_raw in np.flatnonzero(impacted_rects):
        rect_idx = int(rect_idx_raw)
        moved_rect_positions[rect_idx] = collision_position_for_rect(
            moved_positions,
            geometry,
            rect_idx,
        )
    return moved_rect_positions


def impacted_collision_rect_mask(
    moved_nodes: np.ndarray,
    geometry: CollisionGeometry,
) -> np.ndarray:
    impacted = np.zeros(geometry.rect_node.shape[0], dtype=bool)
    for node_raw in np.unique(moved_nodes.astype(np.int32, copy=False)):
        node = int(node_raw)
        if 0 <= node < len(geometry.rect_effect_nodes_by_node):
            for rect_idx in geometry.rect_effect_nodes_by_node[node]:
                impacted[int(rect_idx)] = True
    return impacted


def bbox_rect_b(
    rect_positions: np.ndarray,
    widths: np.ndarray,
    heights: np.ndarray,
) -> float:
    if rect_positions.shape[0] == 0:
        return 0.0
    min_x = np.min(rect_positions[:, 0] - widths / 2.0)
    max_x = np.max(rect_positions[:, 0] + widths / 2.0)
    min_y = np.min(rect_positions[:, 1] - heights / 2.0)
    max_y = np.max(rect_positions[:, 1] + heights / 2.0)
    return float((max_x - min_x) * (max_y - min_y) / 1e9)


def cluster_members(layout: dict) -> dict[str, np.ndarray]:
    members: dict[str, list[int]] = {}
    for idx, nd in enumerate(layout["nodes"]):
        cid = nd.get("clusterId") or ""
        if not cid:
            continue
        members.setdefault(cid, []).append(idx)
    return {cid: np.array(xs, dtype=np.int32) for cid, xs in members.items()}


def no_cluster_pseudo_groups(layout: dict, edges: np.ndarray) -> dict[str, np.ndarray]:
    nodes = layout["nodes"]
    cluster_by_idx = [nd.get("clusterId") or "" for nd in nodes]
    neighbor_clusters: list[list[str]] = [[] for _ in nodes]
    for s, t in edges:
        sc = cluster_by_idx[int(s)]
        tc = cluster_by_idx[int(t)]
        if tc:
            neighbor_clusters[int(s)].append(tc)
        if sc:
            neighbor_clusters[int(t)].append(sc)
    groups: dict[str, list[int]] = {}
    for idx, nd in enumerate(nodes):
        if cluster_by_idx[idx]:
            continue
        counts: dict[str, int] = {}
        for cid in neighbor_clusters[idx]:
            counts[cid] = counts.get(cid, 0) + 1
        if counts:
            sig_parts = [
                cid for cid, _count in sorted(
                    counts.items(), key=lambda item: (-item[1], item[0])
                )[:3]
            ]
            sig = "__pseudo_nc__" + "|".join(sig_parts)
        else:
            app = nd["modelId"].split(".")[0]
            sig = "__pseudo_orphan__" + app
        groups.setdefault(sig, []).append(idx)
    return {
        key: np.array(value, dtype=np.int32)
        for key, value in groups.items()
        if 2 <= len(value) <= 80
    }


NAME_TOKEN_RE = re.compile(r"[A-Z]+(?=[A-Z][a-z]|$)|[A-Z]?[a-z]+|[0-9]+")
SEMANTIC_STOP_TOKENS = {
    "base",
    "model",
    "models",
    "abstract",
    "common",
    "core",
}


def semantic_name_parts(model_id: str) -> tuple[str, set[str]]:
    app = model_id.split(".", 1)[0].lower()
    leaf = model_id.rsplit(".", 1)[-1]
    tokens: set[str] = set()
    for chunk in re.split(r"[^A-Za-z0-9]+", leaf):
        for raw in NAME_TOKEN_RE.findall(chunk):
            token = raw.lower()
            if len(token) <= 1 or token in SEMANTIC_STOP_TOKENS:
                continue
            tokens.add(token)
    tokens.discard(app)
    return app, tokens


def semantic_anchor_candidates(
    layout: dict,
    positions: np.ndarray,
    node_counts: np.ndarray,
    adjacency: list[np.ndarray],
    groups: dict[str, np.ndarray],
    max_nodes: int,
    top_clusters_per_node: int,
    max_degree: int,
    max_group_size: int,
    min_score: float,
    radii: list[float],
    directions: list[tuple[float, float]],
) -> list[Candidate]:
    """Name-based placement candidates for graph-orphan or weakly-linked nodes.

    This deliberately uses reusable string features rather than memorizing
    model ids. The target is a Louvain cluster whose app/token profile matches
    the orphan node name.
    """
    if max_nodes <= 0 or top_clusters_per_node <= 0:
        return []
    model_ids = node_model_ids(layout)
    node_parts = [semantic_name_parts(model_id) for model_id in model_ids]
    cluster_by_idx = [nd.get("clusterId") or "" for nd in layout["nodes"]]

    profiles: list[dict] = []
    for key, members in groups.items():
        if group_is_pseudo(key) or not group_has_louvain(key) or members.shape[0] == 0:
            continue
        if members.shape[0] > max_group_size:
            continue
        token_counts: Counter[str] = Counter()
        app_counts: Counter[str] = Counter()
        for raw_idx in members:
            app, tokens = node_parts[int(raw_idx)]
            app_counts[app] += 1
            token_counts.update(tokens)
        if not token_counts and not app_counts:
            continue
        profiles.append(
            {
                "key": key,
                "members": members,
                "tokens": set(token_counts.keys()),
                "apps": app_counts,
                "size": int(members.shape[0]),
                "centroid": positions[members].mean(axis=0),
                "crossIncident": float(node_counts[members].mean()),
            }
        )
    if not profiles:
        return []

    ranked_nodes: list[tuple[float, int]] = []
    for idx, (app, tokens) in enumerate(node_parts):
        degree = int(adjacency[idx].shape[0])
        if degree > max_degree:
            continue
        if not tokens and not app:
            continue
        # Process low-graph-signal nodes first; high crossing nodes already
        # have geometric candidates that are more relevant.
        ranked_nodes.append((float(node_counts[idx]) + degree * 1000.0, idx))
    ranked_nodes.sort(key=lambda item: item[0])

    candidates: list[Candidate] = []
    for _rank, idx in ranked_nodes[:max_nodes]:
        app, tokens = node_parts[idx]
        current_group = cluster_by_idx[idx]
        scored: list[tuple[float, float, int, float, dict]] = []
        for profile in profiles:
            if profile["key"] == current_group:
                continue
            shared = tokens & profile["tokens"]
            union = tokens | profile["tokens"]
            jaccard = float(len(shared) / len(union)) if union else 0.0
            app_match = float(profile["apps"].get(app, 0) / profile["size"])
            score = 1.5 * jaccard + 0.75 * app_match + 0.05 * len(shared)
            if score < min_score:
                continue
            scored.append((score, jaccard, len(shared), app_match, profile))
        scored.sort(reverse=True, key=lambda item: item[0])
        arr = np.array([idx], dtype=np.int32)
        for score, jaccard, shared_count, app_match, profile in scored[:top_clusters_per_node]:
            for radius in radii:
                if radius <= 0:
                    candidates.append(
                        Candidate(
                            "semantic_anchor",
                            arr,
                            targets=profile["centroid"].reshape(1, 2),
                            group_key=profile["key"],
                            source_group_key=current_group,
                            semantic_score=float(score),
                            semantic_token_jaccard=float(jaccard),
                            semantic_shared_tokens=int(shared_count),
                            semantic_app_match=float(app_match),
                            semantic_target_size=int(profile["size"]),
                            semantic_target_cross_incident=float(
                                profile["crossIncident"]
                            ),
                        )
                    )
                    continue
                for dx, dy in directions:
                    target = profile["centroid"] + np.array([dx * radius, dy * radius])
                    candidates.append(
                        Candidate(
                            "semantic_anchor",
                            arr,
                            targets=target.reshape(1, 2),
                            group_key=profile["key"],
                            source_group_key=current_group,
                            semantic_score=float(score),
                            semantic_token_jaccard=float(jaccard),
                            semantic_shared_tokens=int(shared_count),
                            semantic_app_match=float(app_match),
                            semantic_target_size=int(profile["size"]),
                            semantic_target_cross_incident=float(
                                profile["crossIncident"]
                            ),
                        )
                    )
    return candidates


def adjacency_from_edges(edges: np.ndarray, n_nodes: int) -> list[np.ndarray]:
    adj: list[list[int]] = [[] for _ in range(n_nodes)]
    for s, t in edges:
        adj[int(s)].append(int(t))
        adj[int(t)].append(int(s))
    return [np.array(sorted(set(xs)), dtype=np.int32) for xs in adj]


def bbox_b(positions: np.ndarray) -> float:
    w = positions[:, 0].max() - positions[:, 0].min()
    h = positions[:, 1].max() - positions[:, 1].min()
    return float(w * h / 1e9)


def count_overlaps(
    positions: np.ndarray,
    widths: np.ndarray,
    heights: np.ndarray,
    margin: float,
    active_mask: np.ndarray,
) -> int:
    pair_i, pair_j = build_overlap_pairs(active_mask)
    return int(
        overlap_flags_for_pairs(
            positions, widths, heights, margin, pair_i, pair_j
        ).sum()
    )


def build_overlap_pairs(active_mask: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    active = np.flatnonzero(active_mask)
    if active.shape[0] < 2:
        return np.zeros(0, dtype=np.int32), np.zeros(0, dtype=np.int32)
    ai, aj = np.triu_indices(active.shape[0], k=1)
    return active[ai].astype(np.int32), active[aj].astype(np.int32)


def build_edge_node_pairs(
    edges: np.ndarray,
    n_nodes: int,
    active_mask: np.ndarray | None = None,
) -> tuple[np.ndarray, np.ndarray]:
    if edges.shape[0] == 0 or n_nodes == 0:
        return np.zeros(0, dtype=np.int32), np.zeros(0, dtype=np.int32)
    if active_mask is None:
        nodes = np.arange(n_nodes, dtype=np.int32)
    else:
        nodes = np.flatnonzero(active_mask).astype(np.int32)
    if nodes.size == 0:
        return np.zeros(0, dtype=np.int32), np.zeros(0, dtype=np.int32)
    pair_edge = np.repeat(np.arange(edges.shape[0], dtype=np.int32), nodes.size)
    pair_node = np.tile(nodes, edges.shape[0]).astype(np.int32, copy=False)
    src = edges[pair_edge, 0]
    dst = edges[pair_edge, 1]
    keep = (pair_node != src) & (pair_node != dst)
    return pair_edge[keep].astype(np.int32), pair_node[keep].astype(np.int32)


def overlap_flags_for_pairs(
    positions: np.ndarray,
    widths: np.ndarray,
    heights: np.ndarray,
    margin: float,
    pair_i: np.ndarray,
    pair_j: np.ndarray,
    pair_mask: np.ndarray | None = None,
) -> np.ndarray:
    if pair_mask is not None:
        ii = pair_i[pair_mask]
        jj = pair_j[pair_mask]
    else:
        ii = pair_i
        jj = pair_j
    if ii.size == 0:
        return np.zeros(0, dtype=bool)
    dx = np.abs(positions[ii, 0] - positions[jj, 0])
    dy = np.abs(positions[ii, 1] - positions[jj, 1])
    req_dx = (widths[ii] + widths[jj]) / 2.0 + margin
    req_dy = (heights[ii] + heights[jj]) / 2.0 + margin
    return (dx < req_dx) & (dy < req_dy)


def edge_node_intersection_flags(
    positions: np.ndarray,
    widths: np.ndarray,
    heights: np.ndarray,
    edges: np.ndarray,
    pair_edge: np.ndarray,
    pair_node: np.ndarray,
    margin: float = 0.0,
    pair_mask: np.ndarray | None = None,
) -> np.ndarray:
    if pair_mask is not None:
        ee = pair_edge[pair_mask]
        nn = pair_node[pair_mask]
    else:
        ee = pair_edge
        nn = pair_node
    if ee.size == 0:
        return np.zeros(0, dtype=bool)

    src = edges[ee, 0]
    dst = edges[ee, 1]
    a = positions[src]
    b = positions[dst]
    c = positions[nn]
    half_w = widths[nn] / 2.0 + margin
    half_h = heights[nn] / 2.0 + margin
    xmin = c[:, 0] - half_w
    xmax = c[:, 0] + half_w
    ymin = c[:, 1] - half_h
    ymax = c[:, 1] + half_h
    dx = b[:, 0] - a[:, 0]
    dy = b[:, 1] - a[:, 1]

    def slab(
        start: np.ndarray,
        delta: np.ndarray,
        low: np.ndarray,
        high: np.ndarray,
    ) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        parallel = np.abs(delta) < 1e-9
        valid = (~parallel) | ((start >= low) & (start <= high))
        inv = np.zeros_like(delta, dtype=np.float64)
        np.divide(1.0, delta, out=inv, where=~parallel)
        t1 = (low - start) * inv
        t2 = (high - start) * inv
        enter = np.minimum(t1, t2)
        exit_ = np.maximum(t1, t2)
        enter[parallel] = -np.inf
        exit_[parallel] = np.inf
        return valid, enter, exit_

    valid_x, enter_x, exit_x = slab(a[:, 0], dx, xmin, xmax)
    valid_y, enter_y, exit_y = slab(a[:, 1], dy, ymin, ymax)
    t_enter = np.maximum.reduce([np.zeros_like(dx), enter_x, enter_y])
    t_exit = np.minimum.reduce([np.ones_like(dx), exit_x, exit_y])
    return valid_x & valid_y & (t_exit >= t_enter)


def edge_rect_intersection_flags(
    edge_positions: np.ndarray,
    rect_positions: np.ndarray,
    rect_widths: np.ndarray,
    rect_heights: np.ndarray,
    edges: np.ndarray,
    pair_edge: np.ndarray,
    pair_rect: np.ndarray,
    margin: float = 0.0,
    pair_mask: np.ndarray | None = None,
) -> np.ndarray:
    if pair_mask is not None:
        ee = pair_edge[pair_mask]
        rr = pair_rect[pair_mask]
    else:
        ee = pair_edge
        rr = pair_rect
    if ee.size == 0:
        return np.zeros(0, dtype=bool)

    src = edges[ee, 0]
    dst = edges[ee, 1]
    a = edge_positions[src]
    b = edge_positions[dst]
    c = rect_positions[rr]
    half_w = rect_widths[rr] / 2.0 + margin
    half_h = rect_heights[rr] / 2.0 + margin
    xmin = c[:, 0] - half_w
    xmax = c[:, 0] + half_w
    ymin = c[:, 1] - half_h
    ymax = c[:, 1] + half_h
    dx = b[:, 0] - a[:, 0]
    dy = b[:, 1] - a[:, 1]

    def slab(
        start: np.ndarray,
        delta: np.ndarray,
        low: np.ndarray,
        high: np.ndarray,
    ) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        parallel = np.abs(delta) < 1e-9
        valid = (~parallel) | ((start >= low) & (start <= high))
        inv = np.zeros_like(delta, dtype=np.float64)
        np.divide(1.0, delta, out=inv, where=~parallel)
        t1 = (low - start) * inv
        t2 = (high - start) * inv
        enter = np.minimum(t1, t2)
        exit_ = np.maximum(t1, t2)
        enter[parallel] = -np.inf
        exit_[parallel] = np.inf
        return valid, enter, exit_

    valid_x, enter_x, exit_x = slab(a[:, 0], dx, xmin, xmax)
    valid_y, enter_y, exit_y = slab(a[:, 1], dy, ymin, ymax)
    t_enter = np.maximum.reduce([np.zeros_like(dx), enter_x, enter_y])
    t_exit = np.minimum.reduce([np.ones_like(dx), exit_x, exit_y])
    return valid_x & valid_y & (t_exit >= t_enter)


def impacted_overlap_pair_mask(
    moved_nodes: np.ndarray,
    pair_i: np.ndarray,
    pair_j: np.ndarray,
    n_nodes: int,
) -> np.ndarray:
    moved = np.zeros(n_nodes, dtype=bool)
    moved[moved_nodes] = True
    return moved[pair_i] | moved[pair_j]


def impacted_edge_node_pair_mask(
    moved_nodes: np.ndarray,
    edges: np.ndarray,
    pair_edge: np.ndarray,
    pair_node: np.ndarray,
    n_nodes: int,
) -> np.ndarray:
    if pair_edge.size == 0:
        return np.zeros(0, dtype=bool)
    moved = np.zeros(n_nodes, dtype=bool)
    moved[moved_nodes] = True
    edge_nodes = edges[pair_edge]
    moved_edge = moved[edge_nodes[:, 0]] | moved[edge_nodes[:, 1]]
    moved_rect = moved[pair_node]
    return moved_edge | moved_rect


def impacted_edge_node_pairs(
    moved_nodes: np.ndarray,
    edges: np.ndarray,
    active_nodes: np.ndarray,
    n_nodes: int,
) -> tuple[np.ndarray, np.ndarray]:
    if edges.shape[0] == 0 or active_nodes.size == 0 or moved_nodes.size == 0:
        return np.zeros(0, dtype=np.int32), np.zeros(0, dtype=np.int32)
    moved_nodes = np.unique(moved_nodes.astype(np.int32, copy=False))
    moved = np.zeros(n_nodes, dtype=bool)
    moved[moved_nodes] = True
    edge_ids = np.arange(edges.shape[0], dtype=np.int32)
    edge_moved = moved[edges[:, 0]] | moved[edges[:, 1]]
    incident_edges = np.flatnonzero(edge_moved).astype(np.int32)

    moved_active = moved_nodes[
        (moved_nodes >= 0)
        & (moved_nodes < n_nodes)
        & np.isin(moved_nodes, active_nodes, assume_unique=False)
    ]
    pieces_edge: list[np.ndarray] = []
    pieces_node: list[np.ndarray] = []
    if moved_active.size:
        pe = np.repeat(edge_ids, moved_active.size)
        pn = np.tile(moved_active, edges.shape[0]).astype(np.int32, copy=False)
        keep = (pn != edges[pe, 0]) & (pn != edges[pe, 1])
        pieces_edge.append(pe[keep].astype(np.int32))
        pieces_node.append(pn[keep].astype(np.int32))
    if incident_edges.size:
        pe = np.repeat(incident_edges, active_nodes.size)
        pn = np.tile(active_nodes, incident_edges.size).astype(np.int32, copy=False)
        keep = (pn != edges[pe, 0]) & (pn != edges[pe, 1])
        pieces_edge.append(pe[keep].astype(np.int32))
        pieces_node.append(pn[keep].astype(np.int32))
    if not pieces_edge:
        return np.zeros(0, dtype=np.int32), np.zeros(0, dtype=np.int32)
    pair_edge = np.concatenate(pieces_edge)
    pair_node = np.concatenate(pieces_node)
    keys = pair_edge.astype(np.int64) * int(n_nodes) + pair_node.astype(np.int64)
    _uniq, first = np.unique(keys, return_index=True)
    order = np.sort(first)
    return pair_edge[order].astype(np.int32), pair_node[order].astype(np.int32)


def generate_overlap_candidates(
    positions: np.ndarray,
    widths: np.ndarray,
    heights: np.ndarray,
    node_counts: np.ndarray,
    margin: float,
    active_mask: np.ndarray,
    max_pairs: int,
    padding: float = 8.0,
) -> list[Candidate]:
    active = np.flatnonzero(active_mask)
    if active.shape[0] < 2:
        return []
    ai, aj = np.triu_indices(active.shape[0], k=1)
    ii = active[ai]
    jj = active[aj]
    dx_abs = np.abs(positions[ii, 0] - positions[jj, 0])
    dy_abs = np.abs(positions[ii, 1] - positions[jj, 1])
    req_dx = (widths[ii] + widths[jj]) / 2.0 + margin
    req_dy = (heights[ii] + heights[jj]) / 2.0 + margin
    overlap_mask = (dx_abs < req_dx) & (dy_abs < req_dy)
    if not np.any(overlap_mask):
        return []
    oi = ii[overlap_mask]
    oj = jj[overlap_mask]
    ox = req_dx[overlap_mask] - dx_abs[overlap_mask]
    oy = req_dy[overlap_mask] - dy_abs[overlap_mask]
    # Prioritize overlaps touching low crossing-incident nodes; they are
    # cheaper to move without hurting crossings.
    priority = np.minimum(node_counts[oi], node_counts[oj])
    order = np.argsort(priority)[:max_pairs]
    candidates: list[Candidate] = []
    for k in order:
        a = int(oi[k])
        b = int(oj[k])
        move_a = node_counts[a] <= node_counts[b]
        node = a if move_a else b
        other = b if move_a else a
        if ox[k] <= oy[k]:
            sign = 1.0 if positions[node, 0] >= positions[other, 0] else -1.0
            dx = sign * (float(ox[k]) + padding)
            dy = 0.0
        else:
            sign = 1.0 if positions[node, 1] >= positions[other, 1] else -1.0
            dx = 0.0
            dy = sign * (float(oy[k]) + padding)
        candidates.append(
            Candidate(
                "overlap_push",
                np.array([node], dtype=np.int32),
                dx=dx,
                dy=dy,
                group_key="__overlap_pair__",
            )
        )
    return candidates


def score_metrics(
    cross: int,
    overlaps: int,
    bbox: float,
    overlap_weight: float,
    bbox_weight: float,
    bbox_target_b: float,
    edge_node: int = 0,
    edge_node_weight: float = 0.0,
    composite_weight: float = 0.0,
    composite_quality: float = 0.0,
) -> float:
    bbox_penalty = max(0.0, bbox - bbox_target_b)
    # A table/edge collision is visually the same class of defect as an
    # edge/edge crossing: the diagram route is obstructed. Count it in the
    # cross bucket instead of treating it as a weak secondary penalty.
    visual_cross = cross + edge_node
    base = (
        visual_cross
        + overlap_weight * overlaps
        + bbox_weight * bbox_penalty
    )
    # Composite quality (Bennett-weighted, ∈ [0, 1] with 1 = best). When
    # weight > 0, adds a smooth penalty equal to weight * (1 - quality)
    # so otherwise-equivalent moves (e.g. all reducing the same overlap
    # count) get tiebroken by fine-grained quality differences. Skip
    # entirely when weight == 0 so the legacy cost function and any
    # pre-trained ckpts keep their original gain semantics.
    if composite_weight > 0.0:
        base += composite_weight * (1.0 - max(0.0, min(1.0, composite_quality)))
    return base


def build_carrier_pair_keys(
    layout: dict,
    positions: np.ndarray,
    edges: np.ndarray,
    evaluator: fce.FastCrossEval,
) -> np.ndarray:
    model_ids = node_model_ids(layout)
    cluster_by_idx = [nd.get("clusterId") or "" for nd in layout["nodes"]]
    leaf_to_bundle: dict[str, int] = {}
    bundle_roots: list[set[str]] = []
    for bi, bundle in enumerate((layout.get("engineMetadata") or {}).get("leafBundles") or []):
        roots = set(bundle.get("sharedRootModelIds") or [])
        parent = bundle.get("parentModelId")
        if parent:
            roots.add(parent)
        bundle_roots.append(roots)
        for leaf in bundle.get("leafModelIds") or []:
            leaf_to_bundle[leaf] = bi

    cluster_sum: dict[str, np.ndarray] = {}
    cluster_count: dict[str, int] = {}
    for idx, cid in enumerate(cluster_by_idx):
        if not cid:
            continue
        cluster_sum.setdefault(cid, np.zeros(2, dtype=np.float64))
        cluster_sum[cid] += positions[idx]
        cluster_count[cid] = cluster_count.get(cid, 0) + 1
    cluster_centroids = {
        cid: total / float(cluster_count[cid])
        for cid, total in cluster_sum.items()
        if cluster_count.get(cid, 0) > 0
    }

    nearest_cache: dict[int, str] = {}

    def nearest_cluster(idx: int) -> str:
        if idx in nearest_cache:
            return nearest_cache[idx]
        if not cluster_centroids:
            nearest_cache[idx] = ""
            return ""
        p = positions[idx]
        best = ""
        best_d2 = float("inf")
        for cid, c in cluster_centroids.items():
            d = p - c
            d2 = float(d[0] * d[0] + d[1] * d[1])
            if d2 < best_d2:
                best_d2 = d2
                best = cid
        nearest_cache[idx] = best
        return best

    carrier_to_id: dict[str, int] = {}

    def intern(carrier: str) -> int:
        got = carrier_to_id.get(carrier)
        if got is not None:
            return got
        got = len(carrier_to_id)
        carrier_to_id[carrier] = got
        return got

    edge_carriers = np.zeros(edges.shape[0], dtype=np.int64)
    for ei, (s_idx_raw, t_idx_raw) in enumerate(edges):
        s_idx = int(s_idx_raw)
        t_idx = int(t_idx_raw)
        s = model_ids[s_idx]
        t = model_ids[t_idx]
        s_bi = leaf_to_bundle.get(s)
        if s_bi is not None and t in bundle_roots[s_bi]:
            edge_carriers[ei] = intern(f"B{s_bi}|{t}")
            continue
        t_bi = leaf_to_bundle.get(t)
        if t_bi is not None and s in bundle_roots[t_bi]:
            edge_carriers[ei] = intern(f"B{t_bi}|{s}")
            continue
        s_cluster = cluster_by_idx[s_idx] or nearest_cluster(s_idx)
        t_cluster = cluster_by_idx[t_idx] or nearest_cluster(t_idx)
        if s_cluster and t_cluster:
            if s_cluster == t_cluster:
                edge_carriers[ei] = intern(f"Cself|{s_cluster}")
            elif s_cluster < t_cluster:
                edge_carriers[ei] = intern(f"C|{s_cluster}|{t_cluster}")
            else:
                edge_carriers[ei] = intern(f"C|{t_cluster}|{s_cluster}")
        else:
            edge_carriers[ei] = intern(f"E|{ei}")

    a = edge_carriers[evaluator.i]
    b = edge_carriers[evaluator.j]
    lo = np.minimum(a, b)
    hi = np.maximum(a, b)
    keys = lo * (len(carrier_to_id) + 1) + hi
    keys[a == b] = -1
    return keys.astype(np.int64)


def carrier_count_map(cross_flags: np.ndarray, carrier_pair_keys: np.ndarray) -> dict[int, int]:
    ids = carrier_pair_keys[cross_flags]
    ids = ids[ids >= 0]
    if ids.size == 0:
        return {}
    uniq, counts = np.unique(ids, return_counts=True)
    return {int(k): int(v) for k, v in zip(uniq, counts)}


def carrier_cross_count(cross_flags: np.ndarray, carrier_pair_keys: np.ndarray) -> int:
    ids = carrier_pair_keys[cross_flags]
    ids = ids[ids >= 0]
    if ids.size == 0:
        return 0
    return int(np.unique(ids).shape[0])


def measure(
    positions: np.ndarray,
    evaluator: fce.FastCrossEval,
    widths: np.ndarray,
    heights: np.ndarray,
    active_mask: np.ndarray,
    overlap_pairs: tuple[np.ndarray, np.ndarray],
    overlap_margin: float,
    overlap_weight: float,
    bbox_weight: float,
    bbox_target_b: float,
    carrier_pair_keys: np.ndarray | None = None,
    edge_node_pairs: tuple[np.ndarray, np.ndarray] | None = None,
    edge_node_margin: float = 0.0,
    edge_node_weight: float = 0.0,
    collision_geometry: CollisionGeometry | None = None,
    current_rect_positions: np.ndarray | None = None,
    composite_context: CompositeContext | None = None,
) -> Metrics:
    if carrier_pair_keys is None:
        cross = evaluator.count_crossings(positions)
    else:
        cross = carrier_cross_count(
            crossing_flags_for_pairs(positions, evaluator),
            carrier_pair_keys,
        )
    if collision_geometry is None:
        overlaps = int(
            overlap_flags_for_pairs(
                positions,
                widths,
                heights,
                overlap_margin,
                overlap_pairs[0],
                overlap_pairs[1],
            ).sum()
        )
        if edge_node_pairs is None:
            edge_node = 0
        else:
            edge_node = int(
                edge_node_intersection_flags(
                    positions,
                    widths,
                    heights,
                    evaluator.edges,
                    edge_node_pairs[0],
                    edge_node_pairs[1],
                    edge_node_margin,
                ).sum()
            )
        bb = bbox_b(positions)
    else:
        rect_positions = collision_positions(positions, collision_geometry)
        overlaps = int(
            overlap_flags_for_pairs(
                rect_positions,
                collision_geometry.rect_widths,
                collision_geometry.rect_heights,
                overlap_margin,
                collision_geometry.overlap_pairs[0],
                collision_geometry.overlap_pairs[1],
            ).sum()
        )
        edge_node = int(
            edge_rect_intersection_flags(
                positions,
                rect_positions,
                collision_geometry.rect_widths,
                collision_geometry.rect_heights,
                evaluator.edges,
                collision_geometry.edge_node_pairs[0],
                collision_geometry.edge_node_pairs[1],
                edge_node_margin,
            ).sum()
        )
        bb = bbox_rect_b(
            rect_positions,
            collision_geometry.rect_widths,
            collision_geometry.rect_heights,
        )
    composite_q = 0.0
    composite_w = 0.0
    if composite_context is not None and composite_context.weight > 0.0:
        # Lazy import — only paid when composite weighting is active.
        from importlib.util import spec_from_file_location, module_from_spec
        global _MEASURE_METRICS_EXTENDED_MOD
        if "_MEASURE_METRICS_EXTENDED_MOD" not in globals():
            _spec = spec_from_file_location(
                "metrics_extended",
                str(_THIS_DIR / "metrics_extended.py"),
            )
            _mod = module_from_spec(_spec)
            _spec.loader.exec_module(_mod)
            _MEASURE_METRICS_EXTENDED_MOD = _mod
        ext = _MEASURE_METRICS_EXTENDED_MOD.measure_extended(
            positions,
            evaluator.edges,
            composite_context.widths,
            composite_context.heights,
            composite_context.cluster_ids,
        )
        composite_q = float(ext.composite_quality)
        composite_w = float(composite_context.weight)
    score = score_metrics(
        cross,
        overlaps,
        bb,
        overlap_weight,
        bbox_weight,
        bbox_target_b,
        edge_node=edge_node,
        edge_node_weight=edge_node_weight,
        composite_weight=composite_w,
        composite_quality=composite_q,
    )
    return Metrics(
        cross=cross,
        overlaps=overlaps,
        bbox_b=bb,
        score=score,
        edge_node=edge_node,
    )


def crossing_flags_for_pairs(
    positions: np.ndarray,
    evaluator: fce.FastCrossEval,
    pair_mask: np.ndarray | None = None,
) -> np.ndarray:
    pos = np.asarray(positions, dtype=np.float64)
    if pair_mask is None:
        i_idx = evaluator.i
        j_idx = evaluator.j
    else:
        i_idx = evaluator.i[pair_mask]
        j_idx = evaluator.j[pair_mask]
    e_i = evaluator.edges[i_idx]
    e_j = evaluator.edges[j_idx]
    a = pos[e_i[:, 0]]
    b = pos[e_i[:, 1]]
    c = pos[e_j[:, 0]]
    d = pos[e_j[:, 1]]

    def orient(p: np.ndarray, q: np.ndarray, r: np.ndarray) -> np.ndarray:
        return (
            (q[:, 0] - p[:, 0]) * (r[:, 1] - p[:, 1])
            - (q[:, 1] - p[:, 1]) * (r[:, 0] - p[:, 0])
        )

    o1 = orient(a, b, c)
    o2 = orient(a, b, d)
    o3 = orient(c, d, a)
    o4 = orient(c, d, b)
    s1 = np.sign(o1)
    s2 = np.sign(o2)
    s3 = np.sign(o3)
    s4 = np.sign(o4)
    return (
        (s1 != s2)
        & (s3 != s4)
        & (s1 != 0)
        & (s2 != 0)
        & (s3 != 0)
        & (s4 != 0)
    )


def crossing_points_for_pairs(
    positions: np.ndarray,
    evaluator: fce.FastCrossEval,
    pair_mask: np.ndarray | None = None,
) -> np.ndarray:
    """Return straight-segment intersection points for evaluator edge pairs."""
    pos = np.asarray(positions, dtype=np.float64)
    if pair_mask is None:
        i_idx = evaluator.i
        j_idx = evaluator.j
    else:
        i_idx = evaluator.i[pair_mask]
        j_idx = evaluator.j[pair_mask]
    e_i = evaluator.edges[i_idx]
    e_j = evaluator.edges[j_idx]
    a = pos[e_i[:, 0]]
    b = pos[e_i[:, 1]]
    c = pos[e_j[:, 0]]
    d = pos[e_j[:, 1]]
    r = b - a
    s = d - c
    denom = r[:, 0] * s[:, 1] - r[:, 1] * s[:, 0]
    numer = (c[:, 0] - a[:, 0]) * s[:, 1] - (c[:, 1] - a[:, 1]) * s[:, 0]
    t = np.divide(
        numer,
        denom,
        out=np.zeros_like(numer, dtype=np.float64),
        where=np.abs(denom) > 1e-9,
    )
    return a + r * t.reshape(-1, 1)


def impacted_pair_mask(
    moved_nodes: np.ndarray,
    evaluator: fce.FastCrossEval,
    n_nodes: int,
) -> np.ndarray:
    moved = np.zeros(n_nodes, dtype=bool)
    moved[moved_nodes] = True
    edge_nodes = evaluator.edges
    impacted_edges = moved[edge_nodes[:, 0]] | moved[edge_nodes[:, 1]]
    return impacted_edges[evaluator.i] | impacted_edges[evaluator.j]


def measure_candidate_incremental(
    base_positions: np.ndarray,
    moved_positions: np.ndarray,
    cand: Candidate,
    current: Metrics,
    current_cross_flags: np.ndarray,
    evaluator: fce.FastCrossEval,
    widths: np.ndarray,
    heights: np.ndarray,
    active_mask: np.ndarray,
    overlap_pairs: tuple[np.ndarray, np.ndarray],
    current_overlap_flags: np.ndarray,
    overlap_margin: float,
    overlap_weight: float,
    bbox_weight: float,
    bbox_target_b: float,
    carrier_pair_keys: np.ndarray | None = None,
    current_carrier_counts: dict[int, int] | None = None,
    edge_node_pairs: tuple[np.ndarray, np.ndarray] | None = None,
    current_edge_node_flags: np.ndarray | None = None,
    edge_node_active_nodes: np.ndarray | None = None,
    edge_node_margin: float = 0.0,
    edge_node_weight: float = 0.0,
    collision_geometry: CollisionGeometry | None = None,
    current_rect_positions: np.ndarray | None = None,
    composite_context: CompositeContext | None = None,
) -> Metrics:
    moved_nodes = cand.indices
    if cand.other is not None:
        moved_nodes = np.concatenate([moved_nodes, cand.other])
    mask = impacted_pair_mask(moved_nodes, evaluator, base_positions.shape[0])
    new_local = int(crossing_flags_for_pairs(moved_positions, evaluator, mask).sum())
    if carrier_pair_keys is None:
        old_local = int(current_cross_flags[mask].sum())
        cross = current.cross - old_local + new_local
    else:
        assert current_carrier_counts is not None
        local_keys = carrier_pair_keys[mask]
        old_ids = local_keys[current_cross_flags[mask]]
        old_ids = old_ids[old_ids >= 0]
        new_flags = crossing_flags_for_pairs(moved_positions, evaluator, mask)
        new_ids = local_keys[new_flags]
        new_ids = new_ids[new_ids >= 0]
        old_counts = (
            dict(zip(*np.unique(old_ids, return_counts=True)))
            if old_ids.size > 0
            else {}
        )
        new_counts = (
            dict(zip(*np.unique(new_ids, return_counts=True)))
            if new_ids.size > 0
            else {}
        )
        cross = current.cross
        remaining: dict[int, int] = {}
        for key_raw, count_raw in old_counts.items():
            key = int(key_raw)
            count = int(count_raw)
            after = current_carrier_counts.get(key, 0) - count
            remaining[key] = after
            if after <= 0:
                cross -= 1
        for key_raw, count_raw in new_counts.items():
            key = int(key_raw)
            if remaining.get(key, current_carrier_counts.get(key, 0)) <= 0:
                cross += 1
    if collision_geometry is None:
        overlap_mask = impacted_overlap_pair_mask(
            moved_nodes, overlap_pairs[0], overlap_pairs[1], base_positions.shape[0]
        )
        old_overlap_local = int(current_overlap_flags[overlap_mask].sum())
        new_overlap_local = int(
            overlap_flags_for_pairs(
                moved_positions,
                widths,
                heights,
                overlap_margin,
                overlap_pairs[0],
                overlap_pairs[1],
                overlap_mask,
            ).sum()
        )
        overlaps = current.overlaps - old_overlap_local + new_overlap_local
        if edge_node_pairs is None:
            edge_node = current.edge_node
        else:
            active_nodes = (
                edge_node_active_nodes
                if edge_node_active_nodes is not None
                else np.unique(edge_node_pairs[1]).astype(np.int32)
            )
            local_edge, local_node = impacted_edge_node_pairs(
                moved_nodes,
                evaluator.edges,
                active_nodes,
                base_positions.shape[0],
            )
            old_edge_node_local = int(
                edge_node_intersection_flags(
                    base_positions,
                    widths,
                    heights,
                    evaluator.edges,
                    local_edge,
                    local_node,
                    edge_node_margin,
                ).sum()
            )
            new_edge_node_local = int(
                edge_node_intersection_flags(
                    moved_positions,
                    widths,
                    heights,
                    evaluator.edges,
                    local_edge,
                    local_node,
                    edge_node_margin,
                ).sum()
            )
            edge_node = current.edge_node - old_edge_node_local + new_edge_node_local
        bb = bbox_b(moved_positions)
    else:
        impacted_rects = impacted_collision_rect_mask(moved_nodes, collision_geometry)
        base_rect_positions = (
            current_rect_positions
            if current_rect_positions is not None
            else collision_positions(base_positions, collision_geometry)
        )
        moved_rect_positions = moved_collision_positions(
            base_rect_positions,
            moved_positions,
            collision_geometry,
            impacted_rects,
        )
        rect_pair_i, rect_pair_j = collision_geometry.overlap_pairs
        overlap_mask = impacted_rects[rect_pair_i] | impacted_rects[rect_pair_j]
        old_overlap_local = int(current_overlap_flags[overlap_mask].sum())
        new_overlap_local = int(
            overlap_flags_for_pairs(
                moved_rect_positions,
                collision_geometry.rect_widths,
                collision_geometry.rect_heights,
                overlap_margin,
                rect_pair_i,
                rect_pair_j,
                overlap_mask,
            ).sum()
        )
        overlaps = current.overlaps - old_overlap_local + new_overlap_local

        edge_pair, rect_pair = collision_geometry.edge_node_pairs
        if edge_pair.size == 0:
            edge_node = current.edge_node
        else:
            moved_edge_nodes = np.zeros(base_positions.shape[0], dtype=bool)
            moved_edge_nodes[moved_nodes] = True
            edge_nodes = evaluator.edges[edge_pair]
            incident_edge = moved_edge_nodes[edge_nodes[:, 0]] | moved_edge_nodes[edge_nodes[:, 1]]
            edge_rect_mask = incident_edge | impacted_rects[rect_pair]
            if current_edge_node_flags is None:
                old_edge_node_local = int(
                    edge_rect_intersection_flags(
                        base_positions,
                        base_rect_positions,
                        collision_geometry.rect_widths,
                        collision_geometry.rect_heights,
                        evaluator.edges,
                        edge_pair,
                        rect_pair,
                        edge_node_margin,
                        edge_rect_mask,
                    ).sum()
                )
            else:
                old_edge_node_local = int(current_edge_node_flags[edge_rect_mask].sum())
            new_edge_node_local = int(
                edge_rect_intersection_flags(
                    moved_positions,
                    moved_rect_positions,
                    collision_geometry.rect_widths,
                    collision_geometry.rect_heights,
                    evaluator.edges,
                    edge_pair,
                    rect_pair,
                    edge_node_margin,
                    edge_rect_mask,
                ).sum()
            )
            edge_node = current.edge_node - old_edge_node_local + new_edge_node_local
        bb = bbox_rect_b(
            moved_rect_positions,
            collision_geometry.rect_widths,
            collision_geometry.rect_heights,
        )
    score = score_metrics(
        cross,
        overlaps,
        bb,
        overlap_weight,
        bbox_weight,
        bbox_target_b,
        edge_node=edge_node,
        edge_node_weight=edge_node_weight,
    )
    if composite_context is not None and composite_context.weight > 0.0:
        # Lazy import (mirrors `measure`).
        from importlib.util import spec_from_file_location, module_from_spec
        global _MEASURE_METRICS_EXTENDED_MOD
        if "_MEASURE_METRICS_EXTENDED_MOD" not in globals():
            _spec = spec_from_file_location(
                "metrics_extended",
                str(_THIS_DIR / "metrics_extended.py"),
            )
            _mod = module_from_spec(_spec)
            _spec.loader.exec_module(_mod)
            _MEASURE_METRICS_EXTENDED_MOD = _mod
        ext = _MEASURE_METRICS_EXTENDED_MOD.measure_extended(
            moved_positions,
            evaluator.edges,
            composite_context.widths,
            composite_context.heights,
            composite_context.cluster_ids,
        )
        score += composite_context.weight * (1.0 - float(ext.composite_quality))
    return Metrics(
        cross=cross,
        overlaps=overlaps,
        bbox_b=bb,
        score=score,
        edge_node=edge_node,
    )


def crossing_incident_counts(
    positions: np.ndarray,
    evaluator: fce.FastCrossEval,
) -> tuple[int, np.ndarray, np.ndarray]:
    """Return total crossings, per-node endpoint counts, and per-edge counts."""
    pos = np.asarray(positions, dtype=np.float64)
    crosses = crossing_flags_for_pairs(pos, evaluator)
    pair_i = evaluator.i[crosses]
    pair_j = evaluator.j[crosses]
    edge_counts = np.zeros(evaluator.E, dtype=np.int32)
    np.add.at(edge_counts, pair_i, 1)
    np.add.at(edge_counts, pair_j, 1)
    node_counts = np.zeros(evaluator.n_nodes, dtype=np.int32)
    hot_edges = evaluator.edges[np.concatenate([pair_i, pair_j])]
    np.add.at(node_counts, hot_edges[:, 0], 1)
    np.add.at(node_counts, hot_edges[:, 1], 1)
    return int(crosses.sum()), node_counts, edge_counts


def move_positions(positions: np.ndarray, cand: Candidate) -> np.ndarray:
    out = positions.copy()
    if cand.targets is not None:
        out[cand.indices] = cand.targets
    elif cand.kind == "cluster_swap":
        assert cand.other is not None
        a_centroid = out[cand.indices].mean(axis=0)
        b_centroid = out[cand.other].mean(axis=0)
        delta = b_centroid - a_centroid
        out[cand.indices] += delta
        out[cand.other] -= delta
    elif cand.kind == "swap":
        assert cand.other is not None
        tmp = out[cand.indices].copy()
        out[cand.indices] = out[cand.other]
        out[cand.other] = tmp
    else:
        out[cand.indices, 0] += cand.dx
        out[cand.indices, 1] += cand.dy
    return out


def unit_directions() -> list[tuple[float, float]]:
    inv = 1.0 / math.sqrt(2.0)
    return [
        (1.0, 0.0),
        (-1.0, 0.0),
        (0.0, 1.0),
        (0.0, -1.0),
        (inv, inv),
        (inv, -inv),
        (-inv, inv),
        (-inv, -inv),
    ]


def radial_points(
    anchor: np.ndarray,
    count: int,
    spacing: float,
    rotation: float,
) -> np.ndarray:
    if count <= 0:
        return np.zeros((0, 2), dtype=np.float64)
    golden = math.pi * (3.0 - math.sqrt(5.0))
    pts = np.zeros((count, 2), dtype=np.float64)
    for k in range(count):
        if k == 0:
            radius = 0.0
        else:
            radius = spacing * math.sqrt(float(k))
        angle = rotation + k * golden
        pts[k, 0] = anchor[0] + math.cos(angle) * radius
        pts[k, 1] = anchor[1] + math.sin(angle) * radius
    return pts


def line_points(
    anchor: np.ndarray,
    count: int,
    spacing: float,
    rotation: float,
) -> np.ndarray:
    if count <= 0:
        return np.zeros((0, 2), dtype=np.float64)
    offsets = (np.arange(count, dtype=np.float64) - (count - 1) / 2.0) * spacing
    direction = np.array([math.cos(rotation), math.sin(rotation)], dtype=np.float64)
    return anchor.reshape(1, 2) + offsets.reshape(-1, 1) * direction.reshape(1, 2)


def ring_points(
    anchor: np.ndarray,
    count: int,
    spacing: float,
    rotation: float,
) -> np.ndarray:
    if count <= 0:
        return np.zeros((0, 2), dtype=np.float64)
    if count == 1:
        return anchor.reshape(1, 2).copy()
    radius = max(spacing, spacing * count / (2.0 * math.pi))
    angles = rotation + np.arange(count, dtype=np.float64) * (2.0 * math.pi / count)
    pts = np.zeros((count, 2), dtype=np.float64)
    pts[:, 0] = anchor[0] + np.cos(angles) * radius
    pts[:, 1] = anchor[1] + np.sin(angles) * radius
    return pts


def overlap_component_spread_candidates(
    positions: np.ndarray,
    widths: np.ndarray,
    heights: np.ndarray,
    node_counts: np.ndarray,
    active_mask: np.ndarray,
    margin: float,
    max_components: int,
    spacing_scale: float,
    rotations: int,
    batch_sizes: list[int],
) -> list[Candidate]:
    """Repack connected components of the current overlap graph.

    Single-node overlap pushes often trade one overlap for many crossings on
    dense ERDs. This primitive keeps a small collided island together and
    tests several generic spread patterns around its current centroid.
    """
    pair_i, pair_j = build_overlap_pairs(active_mask)
    flags = overlap_flags_for_pairs(
        positions, widths, heights, margin, pair_i, pair_j
    )
    if not np.any(flags):
        return []

    overlap_i = pair_i[flags]
    overlap_j = pair_j[flags]
    parent = np.arange(positions.shape[0], dtype=np.int32)

    def find(x: int) -> int:
        while int(parent[x]) != x:
            parent[x] = parent[int(parent[x])]
            x = int(parent[x])
        return x

    def union(a: int, b: int) -> None:
        ra = find(a)
        rb = find(b)
        if ra != rb:
            parent[rb] = ra

    for a_raw, b_raw in zip(overlap_i, overlap_j):
        union(int(a_raw), int(b_raw))

    comp_members: dict[int, set[int]] = {}
    comp_pairs: dict[int, int] = {}
    for a_raw, b_raw in zip(overlap_i, overlap_j):
        a = int(a_raw)
        b = int(b_raw)
        root = find(a)
        comp_members.setdefault(root, set()).update((a, b))
        comp_pairs[root] = comp_pairs.get(root, 0) + 1

    ranked: list[tuple[int, int, np.ndarray]] = []
    for root, members_set in comp_members.items():
        members = np.array(sorted(members_set), dtype=np.int32)
        if members.shape[0] < 2:
            continue
        pair_count = comp_pairs.get(root, 0)
        cross_cost = int(node_counts[members].sum())
        score = pair_count * 1000 - cross_cost
        ranked.append((score, pair_count, members))
    ranked.sort(reverse=True, key=lambda item: item[0])

    prepared: list[tuple[np.ndarray, np.ndarray, float, list[np.ndarray]]] = []
    candidates: list[Candidate] = []
    for comp_idx, (_score, _pair_count, members) in enumerate(ranked[:max_components]):
        pts = positions[members]
        center = pts.mean(axis=0)
        centered = pts - center
        angles = np.arctan2(centered[:, 1], centered[:, 0])
        orderings = [
            members[np.argsort(pts[:, 0])],
            members[np.argsort(pts[:, 1])],
            members[np.argsort(angles)],
            members[np.argsort(node_counts[members])],
        ]
        max_dim = float(np.median(np.maximum(widths[members], heights[members])))
        spacing = max(120.0, (max_dim + margin * 2.0) * spacing_scale)
        prepared.append((members, center, spacing, orderings))
        for ordered in orderings:
            for r in range(max(1, rotations)):
                angle = (math.pi * r) / max(1, rotations)
                candidates.append(
                    Candidate(
                        "overlap_spread_line",
                        ordered,
                        targets=line_points(center, ordered.shape[0], spacing, angle),
                        group_key=f"__overlap_component__{comp_idx}",
                    )
                )
                if ordered.shape[0] >= 4:
                    candidates.append(
                        Candidate(
                            "overlap_spread_ring",
                            ordered,
                            targets=ring_points(center, ordered.shape[0], spacing, angle),
                            group_key=f"__overlap_component__{comp_idx}",
                        )
                    )
    for raw_size in batch_sizes:
        size = min(max_components, max(0, raw_size), len(prepared))
        if size <= 1:
            continue
        for ordering_idx in range(4):
            for r in range(max(1, rotations)):
                angle = (math.pi * r) / max(1, rotations)
                all_indices: list[np.ndarray] = []
                all_targets: list[np.ndarray] = []
                for _members, center, spacing, orderings in prepared[:size]:
                    ordered = orderings[ordering_idx]
                    all_indices.append(ordered)
                    all_targets.append(
                        line_points(center, ordered.shape[0], spacing, angle)
                    )
                candidates.append(
                    Candidate(
                        "overlap_spread_batch",
                        np.concatenate(all_indices).astype(np.int32),
                        targets=np.concatenate(all_targets).astype(np.float64),
                        group_key=f"__overlap_batch__{size}",
                    )
                )
    return candidates


def loose_pack_candidates(
    positions: np.ndarray,
    widths: np.ndarray,
    heights: np.ndarray,
    node_counts: np.ndarray,
    adjacency: list[np.ndarray],
    pseudo_groups: dict[str, np.ndarray],
    max_groups: int,
    spacing_scale: float,
    rotations: int,
) -> list[Candidate]:
    groups: list[tuple[int, str, np.ndarray]] = []
    for key, members in pseudo_groups.items():
        score = int(node_counts[members].sum())
        if score <= 0:
            continue
        groups.append((score, key, members))
    groups.sort(reverse=True, key=lambda item: item[0])
    candidates: list[Candidate] = []
    for _score, _key, members in groups[:max_groups]:
        neighbor_set: set[int] = set()
        member_set = set(int(x) for x in members)
        for idx in members:
            for nbr in adjacency[int(idx)]:
                if int(nbr) not in member_set:
                    neighbor_set.add(int(nbr))
        if not neighbor_set:
            continue
        nbr_idx = np.array(sorted(neighbor_set), dtype=np.int32)
        anchor = positions[nbr_idx].mean(axis=0)
        order = np.argsort(-node_counts[members])
        ordered_members = members[order]
        base_spacing = float(
            max(np.median(widths[ordered_members]), np.median(heights[ordered_members]))
            * spacing_scale
        )
        base_spacing = max(120.0, base_spacing)
        for factor in (0.8, 1.1, 1.5):
            for r in range(max(1, rotations)):
                rotation = (2.0 * math.pi * r) / max(1, rotations)
                targets = radial_points(
                    anchor,
                    ordered_members.shape[0],
                    base_spacing * factor,
                    rotation,
                )
                candidates.append(
                    Candidate(
                        "set_positions",
                        ordered_members,
                        targets=targets,
                        group_key=_key,
                    )
                )
    return candidates


def loose_node_anchor_candidates(
    positions: np.ndarray,
    node_counts: np.ndarray,
    adjacency: list[np.ndarray],
    cluster_mask: np.ndarray,
    top_loose_nodes: int,
    radii: list[float],
    directions: list[tuple[float, float]],
) -> list[Candidate]:
    loose = np.flatnonzero(~cluster_mask)
    if loose.shape[0] == 0:
        return []
    ordered = loose[np.argsort(-node_counts[loose])]
    ordered = ordered[node_counts[ordered] > 0][:top_loose_nodes]
    candidates: list[Candidate] = []
    for idx in ordered:
        nbrs = adjacency[int(idx)]
        if nbrs.size == 0:
            continue
        anchor = positions[nbrs].mean(axis=0)
        arr = np.array([int(idx)], dtype=np.int32)
        for radius in radii:
            for dx, dy in directions:
                target = anchor + np.array([dx * radius, dy * radius])
                candidates.append(
                    Candidate(
                        "set_positions",
                        arr,
                        targets=target.reshape(1, 2),
                        group_key="__loose_node__",
                    )
                )
    return candidates


def bundle_orbit_candidates(
    positions: np.ndarray,
    widths: np.ndarray,
    heights: np.ndarray,
    node_counts: np.ndarray,
    layout: dict,
    max_bundles: int,
    rotations: int,
    radius_scales: list[float],
) -> list[Candidate]:
    id_to_idx = {nd["modelId"]: i for i, nd in enumerate(layout["nodes"])}
    ranked: list[tuple[int, int, np.ndarray, np.ndarray]] = []
    for bi, bundle in enumerate((layout.get("engineMetadata") or {}).get("leafBundles") or []):
        leaf_ids = bundle.get("leafModelIds") or []
        leaf_idx = np.array(
            [id_to_idx[mid] for mid in leaf_ids if mid in id_to_idx],
            dtype=np.int32,
        )
        if leaf_idx.shape[0] < 2:
            continue
        root_ids = list(bundle.get("sharedRootModelIds") or [])
        parent = bundle.get("parentModelId")
        if parent:
            root_ids.append(parent)
        root_idx = np.array(
            [id_to_idx[mid] for mid in root_ids if mid in id_to_idx],
            dtype=np.int32,
        )
        if root_idx.shape[0] == 0:
            continue
        score = int(node_counts[leaf_idx].sum() + node_counts[root_idx].sum())
        if score <= 0:
            continue
        ranked.append((score, bi, leaf_idx, root_idx))
    ranked.sort(reverse=True, key=lambda item: item[0])

    candidates: list[Candidate] = []
    for _score, _bi, leaves, roots in ranked[:max_bundles]:
        anchor = positions[roots].mean(axis=0)
        cur = positions[leaves] - anchor
        cur_angles = np.arctan2(cur[:, 1], cur[:, 0])
        order = np.argsort(cur_angles)
        ordered = leaves[order]
        max_dim = float(np.median(np.maximum(widths[ordered], heights[ordered])) + 20.0)
        for spread in (2.0 * math.pi, math.pi, 2.0 * math.pi / 3.0):
            base_radius = max(160.0, max_dim * ordered.shape[0] / max(spread, 0.25))
            for scale in radius_scales:
                radius = base_radius * scale
                for r in range(max(1, rotations)):
                    center = 2.0 * math.pi * r / max(1, rotations)
                    if spread >= 2.0 * math.pi - 1e-6:
                        angles = center + np.arange(ordered.shape[0]) * spread / ordered.shape[0]
                    else:
                        angles = np.linspace(
                            center - spread / 2.0,
                            center + spread / 2.0,
                            ordered.shape[0],
                        )
                    targets = np.zeros((ordered.shape[0], 2), dtype=np.float64)
                    targets[:, 0] = anchor[0] + np.cos(angles) * radius
                    targets[:, 1] = anchor[1] + np.sin(angles) * radius
                    candidates.append(
                        Candidate(
                            "set_positions",
                            ordered,
                            targets=targets,
                            group_key=f"__bundle__{_bi}",
                        )
                    )
    return candidates


def group_anchor_candidates(
    positions: np.ndarray,
    node_counts: np.ndarray,
    adjacency: list[np.ndarray],
    groups: dict[str, np.ndarray],
    max_groups: int,
    max_group_size: int,
    radii: list[float],
    directions: list[tuple[float, float]],
) -> list[Candidate]:
    """Move a whole structural group near its external graph neighborhood.

    This is a generic global-placement primitive: small groups that create many
    crossings are tested near the centroid of the nodes they connect to. The
    exact scorer decides whether the teleport is actually useful.
    """
    ranked: list[tuple[int, str, np.ndarray]] = []
    for key, members in groups.items():
        if members.shape[0] == 0 or members.shape[0] > max_group_size:
            continue
        score = int(node_counts[members].sum())
        if score <= 0:
            continue
        ranked.append((score, key, members))
    ranked.sort(reverse=True, key=lambda item: item[0])

    candidates: list[Candidate] = []
    for _score, _key, members in ranked[:max_groups]:
        member_set = set(int(x) for x in members)
        neighbor_set: set[int] = set()
        for idx in members:
            for nbr in adjacency[int(idx)]:
                if int(nbr) not in member_set:
                    neighbor_set.add(int(nbr))
        if not neighbor_set:
            continue
        nbr_idx = np.array(sorted(neighbor_set), dtype=np.int32)
        anchor = positions[nbr_idx].mean(axis=0)
        centroid = positions[members].mean(axis=0)
        for radius in radii:
            if radius <= 0:
                delta = anchor - centroid
                candidates.append(
                    Candidate(
                        "group_anchor",
                        members,
                        dx=float(delta[0]),
                        dy=float(delta[1]),
                        group_key=_key,
                    )
                )
                continue
            for dx, dy in directions:
                target = anchor + np.array([dx * radius, dy * radius])
                delta = target - centroid
                candidates.append(
                    Candidate(
                        "group_anchor",
                        members,
                        dx=float(delta[0]),
                        dy=float(delta[1]),
                        group_key=_key,
                    )
                )
    return candidates


def component_anchor_candidates(
    positions: np.ndarray,
    node_counts: np.ndarray,
    edges: np.ndarray,
    adjacency: list[np.ndarray],
    groups: dict[str, np.ndarray],
    max_components: int,
    max_group_size: int,
    max_total_nodes: int,
    radii: list[float],
    directions: list[tuple[float, float]],
) -> list[Candidate]:
    """Move connected islands of small structural groups as one unit."""
    small_groups: list[tuple[str, np.ndarray]] = []
    owner = np.full(positions.shape[0], -1, dtype=np.int32)
    for key, members in groups.items():
        if members.shape[0] == 0 or members.shape[0] > max_group_size:
            continue
        if int(node_counts[members].sum()) <= 0:
            continue
        gid = len(small_groups)
        small_groups.append((key, members))
        owner[members] = gid
    if len(small_groups) < 2:
        return []

    meta_adj: list[set[int]] = [set() for _ in small_groups]
    for s, t in edges:
        gs = int(owner[int(s)])
        gt = int(owner[int(t)])
        if gs < 0 or gt < 0 or gs == gt:
            continue
        meta_adj[gs].add(gt)
        meta_adj[gt].add(gs)

    seen = np.zeros(len(small_groups), dtype=bool)
    comps: list[tuple[int, np.ndarray, str]] = []
    for start in range(len(small_groups)):
        if seen[start] or not meta_adj[start]:
            continue
        stack = [start]
        seen[start] = True
        gids: list[int] = []
        while stack:
            gid = stack.pop()
            gids.append(gid)
            for nbr in meta_adj[gid]:
                if not seen[nbr]:
                    seen[nbr] = True
                    stack.append(nbr)
        group_keys = [small_groups[gid][0] for gid in gids]
        members = np.concatenate([small_groups[gid][1] for gid in gids])
        if members.shape[0] < 2 or members.shape[0] > max_total_nodes:
            continue
        score = int(node_counts[members].sum())
        if score <= 0:
            continue
        group_key = compact_group_key(sorted(group_keys), "__component__")
        comps.append((score, members.astype(np.int32), group_key))
    comps.sort(reverse=True, key=lambda item: item[0])

    candidates: list[Candidate] = []
    for _score, members, group_key in comps[:max_components]:
        member_set = set(int(x) for x in members)
        neighbor_set: set[int] = set()
        for idx in members:
            for nbr in adjacency[int(idx)]:
                if int(nbr) not in member_set:
                    neighbor_set.add(int(nbr))
        if not neighbor_set:
            continue
        nbr_idx = np.array(sorted(neighbor_set), dtype=np.int32)
        anchor = positions[nbr_idx].mean(axis=0)
        centroid = positions[members].mean(axis=0)
        for radius in radii:
            if radius <= 0:
                delta = anchor - centroid
                candidates.append(
                    Candidate(
                        "component_anchor",
                        members,
                        dx=float(delta[0]),
                        dy=float(delta[1]),
                        group_key=group_key,
                    )
                )
                continue
            for dx, dy in directions:
                target = anchor + np.array([dx * radius, dy * radius])
                delta = target - centroid
                candidates.append(
                    Candidate(
                        "component_anchor",
                        members,
                        dx=float(delta[0]),
                        dy=float(delta[1]),
                        group_key=group_key,
                    )
                )
    return candidates


def generate_candidates(
    positions: np.ndarray,
    node_counts: np.ndarray,
    edge_counts: np.ndarray,
    edges: np.ndarray,
    adjacency: list[np.ndarray],
    clusters: dict[str, np.ndarray],
    top_nodes: int,
    top_edges: int,
    top_clusters: int,
    steps: list[float],
    anchor_radii: list[float],
    swap_pairs: int,
    cluster_swap_pairs: int,
    max_candidates: int,
    cooldown: np.ndarray | None,
) -> list[Candidate]:
    candidates: list[Candidate] = []
    dirs = unit_directions()

    hot_nodes = np.argsort(-node_counts)[:top_nodes]
    hot_nodes = hot_nodes[node_counts[hot_nodes] > 0]
    for idx in hot_nodes:
        if cooldown is not None and cooldown[idx] > 0:
            continue
        arr = np.array([idx], dtype=np.int32)
        for step in steps:
            for dx, dy in dirs:
                candidates.append(
                    Candidate("node", arr, dx=dx * step, dy=dy * step)
                )
        nbrs = adjacency[int(idx)]
        if nbrs.size > 0:
            anchor = positions[nbrs].mean(axis=0)
            cur = positions[int(idx)]
            for radius in anchor_radii:
                for dx, dy in dirs:
                    target = anchor + np.array([dx * radius, dy * radius])
                    delta = target - cur
                    candidates.append(
                        Candidate("neighbor_anchor", arr, dx=float(delta[0]), dy=float(delta[1]))
                    )

    hot_edges = np.argsort(-edge_counts)[:top_edges]
    hot_edges = hot_edges[edge_counts[hot_edges] > 0]
    for edge_idx in hot_edges:
        s, t = edges[int(edge_idx)]
        p0 = positions[s]
        p1 = positions[t]
        vec = p1 - p0
        norm = float(np.linalg.norm(vec))
        if norm < 1e-6:
            continue
        along = vec / norm
        normal = np.array([-along[1], along[0]])
        edge_dirs = [along, -along, normal, -normal]
        for endpoint in (int(s), int(t)):
            if cooldown is not None and cooldown[endpoint] > 0:
                continue
            arr = np.array([endpoint], dtype=np.int32)
            for step in steps:
                for direction in edge_dirs:
                    candidates.append(
                        Candidate(
                            "edge_endpoint",
                            arr,
                            dx=float(direction[0] * step),
                            dy=float(direction[1] * step),
                        )
                    )
        if cooldown is not None and (cooldown[int(s)] > 0 or cooldown[int(t)] > 0):
            continue
        arr = np.array([int(s), int(t)], dtype=np.int32)
        for step in steps:
            for direction in (normal, -normal):
                candidates.append(
                    Candidate(
                        "edge_translate",
                        arr,
                        dx=float(direction[0] * step),
                        dy=float(direction[1] * step),
                    )
                )

    cluster_scores: list[tuple[int, str, np.ndarray]] = []
    for cid, members in clusters.items():
        if members.shape[0] < 2:
            continue
        cluster_scores.append((int(node_counts[members].sum()), cid, members))
    cluster_scores.sort(reverse=True, key=lambda item: item[0])
    hot_cluster_members: list[tuple[str, np.ndarray]] = []
    for score, cid, members in cluster_scores[:top_clusters]:
        if score <= 0:
            continue
        if cooldown is not None and np.any(cooldown[members] > 0):
            continue
        hot_cluster_members.append((cid, members))
        scale = 0.7 if members.shape[0] > 20 else 1.0
        for step in steps:
            for dx, dy in dirs:
                candidates.append(
                    Candidate(
                        "cluster",
                        members,
                        dx=dx * step * scale,
                        dy=dy * step * scale,
                        group_key=cid,
                    )
                )

    if cluster_swap_pairs > 0 and len(hot_cluster_members) >= 2:
        made = 0
        for a_i in range(len(hot_cluster_members)):
            for b_i in range(a_i + 1, len(hot_cluster_members)):
                if made >= cluster_swap_pairs:
                    break
                a_key, a = hot_cluster_members[a_i]
                b_key, b = hot_cluster_members[b_i]
                candidates.append(
                    Candidate(
                        "cluster_swap",
                        a,
                        other=b,
                        group_key=a_key,
                        other_group_key=b_key,
                    )
                )
                made += 1
            if made >= cluster_swap_pairs:
                break

    # Local swaps are generic but risky; keep them among hot nodes only.
    if swap_pairs > 0 and hot_nodes.shape[0] >= 2:
        ordered = hot_nodes[: min(hot_nodes.shape[0], 16)]
        made = 0
        for a_i in range(ordered.shape[0]):
            for b_i in range(a_i + 1, ordered.shape[0]):
                if made >= swap_pairs:
                    break
                a = int(ordered[a_i])
                b = int(ordered[b_i])
                if np.linalg.norm(positions[a] - positions[b]) > max(steps) * 6:
                    continue
                if cooldown is not None and (cooldown[a] > 0 or cooldown[b] > 0):
                    continue
                candidates.append(
                    Candidate(
                        "swap",
                        np.array([a], dtype=np.int32),
                        other=np.array([b], dtype=np.int32),
                    )
                )
                made += 1
            if made >= swap_pairs:
                break
    if max_candidates > 0 and len(candidates) > max_candidates:
        candidates.sort(
            key=lambda cand: candidate_priority(cand, node_counts),
            reverse=True,
        )
        candidates = candidates[:max_candidates]
    return candidates


def truncate_candidates(
    candidates: list[Candidate],
    node_counts: np.ndarray,
    max_candidates: int,
    per_action_cap: int = 0,
) -> list[Candidate]:
    if max_candidates <= 0 or len(candidates) <= max_candidates:
        return candidates
    ranked = sorted(
        candidates,
        key=lambda cand: candidate_priority(cand, node_counts),
        reverse=True,
    )
    if per_action_cap <= 0:
        return ranked[:max_candidates]

    selected: list[Candidate] = []
    by_action: Counter[str] = Counter()
    for cand in ranked:
        action = candidate_action_type(cand)
        if by_action[action] >= per_action_cap:
            continue
        selected.append(cand)
        by_action[action] += 1
        if len(selected) >= max_candidates:
            break
    if len(selected) >= max_candidates:
        return selected

    selected_ids = {id(cand) for cand in selected}
    for cand in ranked:
        if id(cand) in selected_ids:
            continue
        selected.append(cand)
        if len(selected) >= max_candidates:
            break
    return selected


def crossing_pair_candidates(
    positions: np.ndarray,
    node_counts: np.ndarray,
    edge_counts: np.ndarray,
    edges: np.ndarray,
    evaluator: fce.FastCrossEval,
    max_pairs: int,
    steps: list[float],
) -> list[Candidate]:
    """Generate pairwise edge moves aimed at escaping single-move minima.

    The regular edge candidates move one hot edge at a time. At a local
    crossing minimum, two crossing edges often need a coordinated separation
    before either single-edge move looks profitable. These candidates are still
    generic: they are derived only from current crossing geometry and are
    accepted only after exact verification.
    """
    if max_pairs <= 0 or not steps:
        return []
    cross_flags = crossing_flags_for_pairs(positions, evaluator)
    if not np.any(cross_flags):
        return []
    pair_i = evaluator.i[cross_flags]
    pair_j = evaluator.j[cross_flags]
    priority = edge_counts[pair_i] + edge_counts[pair_j]
    order = np.argsort(-priority)[:max_pairs]
    candidates: list[Candidate] = []

    def unit(vec: np.ndarray) -> np.ndarray | None:
        norm = float(np.linalg.norm(vec))
        if norm < 1e-6:
            return None
        return vec / norm

    def add_unique_direction(dirs: list[np.ndarray], direction: np.ndarray | None) -> None:
        if direction is None:
            return
        for existing in dirs:
            if abs(float(np.dot(existing, direction))) > 0.985:
                return
        dirs.append(direction)

    for rank, pair_idx in enumerate(order):
        e1 = int(pair_i[pair_idx])
        e2 = int(pair_j[pair_idx])
        edge1 = edges[e1].astype(np.int32)
        edge2 = edges[e2].astype(np.int32)
        all_nodes = np.concatenate([edge1, edge2]).astype(np.int32)
        if np.unique(all_nodes).shape[0] != 4:
            continue

        a, b = positions[edge1[0]], positions[edge1[1]]
        c, d = positions[edge2[0]], positions[edge2[1]]
        u1 = unit(b - a)
        u2 = unit(d - c)
        if u1 is None or u2 is None:
            continue
        n1 = np.array([-u1[1], u1[0]], dtype=np.float64)
        n2 = np.array([-u2[1], u2[0]], dtype=np.float64)
        group_key = f"__cross_pair__{rank}:{e1}:{e2}"

        spread_dirs: list[np.ndarray] = []
        add_unique_direction(spread_dirs, unit(n1 + n2))
        add_unique_direction(spread_dirs, unit(n1 - n2))
        add_unique_direction(spread_dirs, n1)
        add_unique_direction(spread_dirs, n2)

        for step in steps:
            for direction in (n2, -n2):
                candidates.append(
                    Candidate(
                        "cross_edge_translate",
                        edge1,
                        dx=float(direction[0] * step),
                        dy=float(direction[1] * step),
                        group_key=group_key,
                        semantic_target_cross_incident=float(priority[pair_idx]),
                    )
                )
            for direction in (n1, -n1):
                candidates.append(
                    Candidate(
                        "cross_edge_translate",
                        edge2,
                        dx=float(direction[0] * step),
                        dy=float(direction[1] * step),
                        group_key=group_key,
                        semantic_target_cross_incident=float(priority[pair_idx]),
                    )
                )
            for direction in spread_dirs:
                targets = positions[all_nodes].copy()
                targets[:2] += direction.reshape(1, 2) * step
                targets[2:] -= direction.reshape(1, 2) * step
                candidates.append(
                    Candidate(
                        "cross_pair_edge_spread",
                        all_nodes,
                        targets=targets,
                        group_key=group_key,
                        semantic_target_cross_incident=float(priority[pair_idx]),
                    )
                )
    return candidates


def crossing_fan_candidates(
    positions: np.ndarray,
    edge_counts: np.ndarray,
    edges: np.ndarray,
    evaluator: fce.FastCrossEval,
    max_edges: int,
    steps: list[float],
) -> list[Candidate]:
    """Move hot crossing edges away from their local crossing fan.

    Pairwise crossing moves can be too local: an edge crossing many roughly
    parallel carriers needs a coherent push away from the whole fan before
    single-pair spread moves become useful. Directions are derived only from
    current crossing geometry and still pass through exact behavior gates.
    """
    if max_edges <= 0 or not steps:
        return []
    cross_flags = crossing_flags_for_pairs(positions, evaluator)
    if not np.any(cross_flags):
        return []

    pair_i = evaluator.i[cross_flags]
    pair_j = evaluator.j[cross_flags]
    by_edge: dict[int, list[int]] = {}
    for a_raw, b_raw in zip(pair_i, pair_j):
        a = int(a_raw)
        b = int(b_raw)
        by_edge.setdefault(a, []).append(b)
        by_edge.setdefault(b, []).append(a)
    if not by_edge:
        return []

    ranked = sorted(
        by_edge.keys(),
        key=lambda edge_idx: edge_counts[int(edge_idx)],
        reverse=True,
    )[:max_edges]
    candidates: list[Candidate] = []

    def unit(vec: np.ndarray) -> np.ndarray | None:
        norm = float(np.linalg.norm(vec))
        if norm < 1e-6:
            return None
        return vec / norm

    def add_direction(dirs: list[np.ndarray], direction: np.ndarray | None) -> None:
        if direction is None:
            return
        for existing in dirs:
            if abs(float(np.dot(existing, direction))) > 0.985:
                return
        dirs.append(direction)

    for rank, edge_idx in enumerate(ranked):
        s_raw, t_raw = edges[int(edge_idx)]
        s = int(s_raw)
        t = int(t_raw)
        p0 = positions[s]
        p1 = positions[t]
        vec = p1 - p0
        along = unit(vec)
        if along is None:
            continue
        normal = np.array([-along[1], along[0]], dtype=np.float64)
        edge_mid = (p0 + p1) / 2.0
        partners = np.array(by_edge[int(edge_idx)], dtype=np.int32)
        if partners.size == 0:
            continue
        partner_edges = edges[partners]
        partner_a = positions[partner_edges[:, 0]]
        partner_b = positions[partner_edges[:, 1]]
        partner_mid = (partner_a + partner_b) / 2.0
        partner_vec = partner_b - partner_a

        dirs: list[np.ndarray] = []
        add_direction(dirs, normal)
        add_direction(dirs, -normal)
        add_direction(dirs, unit(edge_mid - partner_mid.mean(axis=0)))

        # Aggregate signed normals of the crossing partner lines. This points
        # toward the side of the local bundle where the current edge already
        # lies, so it tends to peel the edge out instead of pushing through.
        partner_norm = np.linalg.norm(partner_vec, axis=1)
        good = partner_norm > 1e-6
        if np.any(good):
            partner_unit = partner_vec[good] / partner_norm[good].reshape(-1, 1)
            partner_normal = np.stack(
                [-partner_unit[:, 1], partner_unit[:, 0]],
                axis=1,
            )
            rel = edge_mid.reshape(1, 2) - partner_mid[good]
            sign = np.sign(np.sum(rel * partner_normal, axis=1))
            sign[sign == 0] = 1.0
            aggregate = (partner_normal * sign.reshape(-1, 1)).sum(axis=0)
            add_direction(dirs, unit(aggregate))

            weights = np.maximum(1.0, edge_counts[partners[good]].astype(np.float64))
            weighted = (partner_normal * sign.reshape(-1, 1) * weights.reshape(-1, 1)).sum(axis=0)
            add_direction(dirs, unit(weighted))

        group_key = f"__cross_fan__{rank}:{edge_idx}"
        edge_nodes = np.array([s, t], dtype=np.int32)
        for step in steps:
            for direction in dirs:
                candidates.append(
                    Candidate(
                        "cross_fan_edge_translate",
                        edge_nodes,
                        dx=float(direction[0] * step),
                        dy=float(direction[1] * step),
                        group_key=group_key,
                        semantic_target_cross_incident=float(edge_counts[int(edge_idx)]),
                    )
                )
                for endpoint in (s, t):
                    candidates.append(
                        Candidate(
                            "cross_fan_endpoint_translate",
                            np.array([endpoint], dtype=np.int32),
                            dx=float(direction[0] * step),
                            dy=float(direction[1] * step),
                            group_key=group_key,
                            semantic_target_cross_incident=float(edge_counts[int(edge_idx)]),
                    )
                )
    return candidates


def crossing_group_fan_candidates(
    positions: np.ndarray,
    node_counts: np.ndarray,
    edge_counts: np.ndarray,
    edges: np.ndarray,
    evaluator: fce.FastCrossEval,
    groups: dict[str, np.ndarray],
    max_groups: int,
    max_group_size: int,
    steps: list[float],
) -> list[Candidate]:
    """Move whole hot groups away from the crossing carrier fan.

    Single-edge moves can get trapped when a cluster is sitting on top of a
    dense bundle of external carriers. This action keeps the group shape intact
    and only derives directions from current crossing geometry, so it remains a
    generic behavior candidate rather than a fixed final-layout answer.
    """
    if max_groups <= 0 or max_group_size <= 0 or not steps:
        return []
    cross_flags = crossing_flags_for_pairs(positions, evaluator)
    if not np.any(cross_flags):
        return []

    pair_i = evaluator.i[cross_flags]
    pair_j = evaluator.j[cross_flags]
    n_nodes = positions.shape[0]

    ranked: list[tuple[int, str, np.ndarray, np.ndarray, np.ndarray]] = []
    for key, raw_members in groups.items():
        members = np.asarray(raw_members, dtype=np.int32)
        if members.shape[0] < 2 or members.shape[0] > max_group_size:
            continue
        member_mask = np.zeros(n_nodes, dtype=bool)
        member_mask[members] = True
        edge_member_count = (
            member_mask[edges[:, 0]].astype(np.int8)
            + member_mask[edges[:, 1]].astype(np.int8)
        )
        incident_edges = edge_member_count > 0
        if not np.any(incident_edges):
            continue
        pair_touches_group = incident_edges[pair_i] | incident_edges[pair_j]
        pair_count = int(pair_touches_group.sum())
        if pair_count <= 0:
            continue
        score = (
            int(edge_counts[incident_edges].sum())
            + int(node_counts[members].sum())
            + pair_count
        )
        if score <= 0:
            continue
        ranked.append((score, key, members, incident_edges, edge_member_count))

    ranked.sort(reverse=True, key=lambda item: item[0])
    candidates: list[Candidate] = []

    def unit(vec: np.ndarray) -> np.ndarray | None:
        norm = float(np.linalg.norm(vec))
        if norm < 1e-6:
            return None
        return vec / norm

    def add_direction(dirs: list[np.ndarray], direction: np.ndarray | None) -> None:
        if direction is None:
            return
        for existing in dirs:
            if float(np.dot(existing, direction)) > 0.985:
                return
        dirs.append(direction)

    def add_bidirectional(dirs: list[np.ndarray], direction: np.ndarray | None) -> None:
        direction = unit(direction) if direction is not None else None
        if direction is None:
            return
        add_direction(dirs, direction)
        add_direction(dirs, -direction)

    for score, key, members, incident_edges, edge_member_count in ranked[:max_groups]:
        in_i = incident_edges[pair_i]
        in_j = incident_edges[pair_j]
        external_partner_edges = np.concatenate(
            [
                pair_j[in_i & ~in_j],
                pair_i[in_j & ~in_i],
            ]
        )
        if external_partner_edges.size == 0:
            continue
        group_cross_edges = np.concatenate([pair_i[in_i], pair_j[in_j]])
        group_cross_edges = np.unique(group_cross_edges).astype(np.int32)
        external_partner_edges = np.unique(external_partner_edges).astype(np.int32)

        centroid = positions[members].mean(axis=0)
        partner_pairs = edges[external_partner_edges]
        partner_a = positions[partner_pairs[:, 0]]
        partner_b = positions[partner_pairs[:, 1]]
        partner_mid = (partner_a + partner_b) / 2.0
        partner_vec = partner_b - partner_a
        partner_weights = np.maximum(
            1.0,
            edge_counts[external_partner_edges].astype(np.float64),
        )
        partner_center = np.average(partner_mid, axis=0, weights=partner_weights)

        group_pairs = edges[group_cross_edges]
        group_mid = (
            positions[group_pairs[:, 0]] + positions[group_pairs[:, 1]]
        ) / 2.0
        group_weights = np.maximum(1.0, edge_counts[group_cross_edges].astype(np.float64))
        group_center = np.average(group_mid, axis=0, weights=group_weights)

        dirs: list[np.ndarray] = []
        add_bidirectional(dirs, centroid - partner_center)
        add_bidirectional(dirs, group_center - partner_center)

        partner_norm = np.linalg.norm(partner_vec, axis=1)
        good = partner_norm > 1e-6
        if np.any(good):
            partner_unit = partner_vec[good] / partner_norm[good].reshape(-1, 1)
            partner_normal = np.stack(
                [-partner_unit[:, 1], partner_unit[:, 0]],
                axis=1,
            )
            rel = centroid.reshape(1, 2) - partner_mid[good]
            sign = np.sign(np.sum(rel * partner_normal, axis=1))
            sign[sign == 0] = 1.0
            weights = partner_weights[good].reshape(-1, 1)
            aggregate = (partner_normal * sign.reshape(-1, 1) * weights).sum(axis=0)
            add_bidirectional(dirs, aggregate)

        boundary_edges = np.flatnonzero(edge_member_count == 1).astype(np.int32)
        if boundary_edges.size:
            boundary_pairs = edges[boundary_edges]
            first_in = np.isin(boundary_pairs[:, 0], members)
            internal_nodes = np.where(first_in, boundary_pairs[:, 0], boundary_pairs[:, 1])
            external_nodes = np.where(first_in, boundary_pairs[:, 1], boundary_pairs[:, 0])
            external_center = positions[external_nodes].mean(axis=0)
            internal_center = positions[internal_nodes].mean(axis=0)
            add_bidirectional(dirs, centroid - external_center)

            boundary_vec = positions[external_nodes] - positions[internal_nodes]
            boundary_norm = np.linalg.norm(boundary_vec, axis=1)
            good_boundary = boundary_norm > 1e-6
            if np.any(good_boundary):
                boundary_unit = (
                    boundary_vec[good_boundary]
                    / boundary_norm[good_boundary].reshape(-1, 1)
                )
                boundary_normal = np.stack(
                    [-boundary_unit[:, 1], boundary_unit[:, 0]],
                    axis=1,
                )
                rel = centroid.reshape(1, 2) - internal_center.reshape(1, 2)
                sign = np.sign(np.sum(rel * boundary_normal, axis=1))
                sign[sign == 0] = 1.0
                add_bidirectional(
                    dirs,
                    (boundary_normal * sign.reshape(-1, 1)).sum(axis=0),
                )

        if not dirs:
            continue
        for step in steps:
            for direction in dirs[:6]:
                candidates.append(
                    Candidate(
                        "cross_group_fan_translate",
                        members,
                        dx=float(direction[0] * step),
                        dy=float(direction[1] * step),
                        group_key=key,
                        semantic_target_cross_incident=float(score),
                    )
                )
    return candidates


def crossing_carrier_pair_separation_candidates(
    positions: np.ndarray,
    node_counts: np.ndarray,
    edge_counts: np.ndarray,
    edges: np.ndarray,
    evaluator: fce.FastCrossEval,
    groups: dict[str, np.ndarray],
    max_pairs: int,
    max_group_size: int,
    steps: list[float],
) -> list[Candidate]:
    """Separate two crossing-heavy carrier groups as a coordinated action."""
    if max_pairs <= 0 or max_group_size <= 0 or not steps:
        return []
    cross_flags = crossing_flags_for_pairs(positions, evaluator)
    if not np.any(cross_flags):
        return []

    n_nodes = positions.shape[0]
    group_entries: list[tuple[str, np.ndarray, np.ndarray]] = []
    owner = np.full(n_nodes, -1, dtype=np.int32)
    for key, raw_members in groups.items():
        members = np.asarray(raw_members, dtype=np.int32)
        if members.shape[0] < 2 or members.shape[0] > max_group_size:
            continue
        if np.any(owner[members] >= 0):
            continue
        group_idx = len(group_entries)
        owner[members] = group_idx
        group_entries.append((key, members, positions[members].mean(axis=0)))
    if len(group_entries) < 2:
        return []

    edge_groups: list[tuple[int, ...]] = []
    for s_raw, t_raw in edges:
        vals: list[int] = []
        for node in (int(s_raw), int(t_raw)):
            gid = int(owner[node])
            if gid >= 0 and gid not in vals:
                vals.append(gid)
        edge_groups.append(tuple(vals))

    pair_i = evaluator.i[cross_flags]
    pair_j = evaluator.j[cross_flags]
    pair_score: Counter[tuple[int, int]] = Counter()
    pair_edges: dict[tuple[int, int], set[int]] = defaultdict(set)
    for e1_raw, e2_raw in zip(pair_i, pair_j):
        e1 = int(e1_raw)
        e2 = int(e2_raw)
        g1 = edge_groups[e1]
        g2 = edge_groups[e2]
        if not g1 or not g2:
            continue
        for a in g1:
            for b in g2:
                if a == b:
                    continue
                key = (a, b) if a < b else (b, a)
                pair_score[key] += 1
                pair_edges.setdefault(key, set()).update((e1, e2))
    if not pair_score:
        return []

    ranked = sorted(pair_score.items(), key=lambda item: item[1], reverse=True)
    candidates: list[Candidate] = []

    def unit(vec: np.ndarray) -> np.ndarray | None:
        norm = float(np.linalg.norm(vec))
        if norm < 1e-6:
            return None
        return vec / norm

    def add_direction(dirs: list[np.ndarray], direction: np.ndarray | None) -> None:
        if direction is None:
            return
        for existing in dirs:
            if abs(float(np.dot(existing, direction))) > 0.985:
                return
        dirs.append(direction)

    for (a_gid, b_gid), cross_count in ranked[:max_pairs]:
        a_key, a_members, a_centroid = group_entries[a_gid]
        b_key, b_members, b_centroid = group_entries[b_gid]
        if np.intersect1d(a_members, b_members, assume_unique=False).size > 0:
            continue
        sep = unit(a_centroid - b_centroid)
        if sep is None:
            continue
        edge_idx = np.array(sorted(pair_edges.get((a_gid, b_gid), set())), dtype=np.int32)
        if edge_idx.size == 0:
            continue
        edge_pairs = edges[edge_idx]
        edge_a = positions[edge_pairs[:, 0]]
        edge_b = positions[edge_pairs[:, 1]]
        edge_mid = (edge_a + edge_b) / 2.0
        edge_vec = edge_b - edge_a
        weights = np.maximum(1.0, edge_counts[edge_idx].astype(np.float64))
        hot_center = np.average(edge_mid, axis=0, weights=weights)

        dirs: list[np.ndarray] = []
        add_direction(dirs, sep)
        normal = np.array([-sep[1], sep[0]], dtype=np.float64)
        add_direction(dirs, normal)
        add_direction(dirs, -normal)

        away_hot = unit(a_centroid - hot_center)
        if away_hot is not None:
            add_direction(dirs, away_hot)
            add_direction(dirs, -away_hot)

        edge_norm = np.linalg.norm(edge_vec, axis=1)
        good = edge_norm > 1e-6
        if np.any(good):
            edge_unit = edge_vec[good] / edge_norm[good].reshape(-1, 1)
            edge_normal = np.stack([-edge_unit[:, 1], edge_unit[:, 0]], axis=1)
            rel = ((a_centroid + b_centroid) / 2.0).reshape(1, 2) - edge_mid[good]
            sign = np.sign(np.sum(rel * edge_normal, axis=1))
            sign[sign == 0] = 1.0
            aggregate = (
                edge_normal
                * sign.reshape(-1, 1)
                * weights[good].reshape(-1, 1)
            ).sum(axis=0)
            aggregate = unit(aggregate)
            add_direction(dirs, aggregate)
            if aggregate is not None:
                add_direction(dirs, -aggregate)

        if not dirs:
            continue
        indices = np.concatenate([a_members, b_members]).astype(np.int32)
        a_len = int(a_members.shape[0])
        total_len = max(1, int(indices.shape[0]))
        a_scale = math.sqrt(float(b_members.shape[0]) / float(total_len))
        b_scale = math.sqrt(float(a_members.shape[0]) / float(total_len))
        semantic_score = (
            float(cross_count) * 4.0
            + float(node_counts[a_members].sum())
            + float(node_counts[b_members].sum())
        )
        for step in steps:
            for direction in dirs[:6]:
                targets = positions[indices].copy()
                targets[:a_len] += direction.reshape(1, 2) * step * a_scale
                targets[a_len:] -= direction.reshape(1, 2) * step * b_scale
                candidates.append(
                    Candidate(
                        "cross_carrier_pair_separate",
                        indices,
                        targets=targets,
                        group_key=a_key,
                        other_group_key=b_key,
                        semantic_target_size=int(indices.shape[0]),
                        semantic_target_cross_incident=semantic_score,
                    )
                )
    return candidates


def crossing_long_edge_anchor_candidates(
    positions: np.ndarray,
    node_counts: np.ndarray,
    edge_counts: np.ndarray,
    edges: np.ndarray,
    groups: dict[str, np.ndarray],
    max_edges: int,
    max_group_size: int,
    radii: list[float],
    directions: list[tuple[float, float]],
    min_edge_crossings: int = 8,
) -> list[Candidate]:
    """Anchor endpoints/groups of long crossing-heavy edges closer together.

    Most remaining crossings tend to be caused by a few long carrier edges
    connecting distant semantic islands. Fan pushes only nudge those carriers;
    this action tests whether moving one endpoint or its local group near the
    other side removes a whole corridor of crossings.
    """
    if max_edges <= 0 or max_group_size <= 0 or not radii:
        return []
    hot_edges = np.argsort(-edge_counts)
    hot_edges = hot_edges[edge_counts[hot_edges] >= min_edge_crossings][:max_edges]
    if hot_edges.size == 0:
        return []

    n_nodes = positions.shape[0]
    node_groups: list[list[tuple[str, np.ndarray]]] = [[] for _ in range(n_nodes)]
    for key, raw_members in groups.items():
        members = np.asarray(raw_members, dtype=np.int32)
        if members.size < 2 or members.size > max_group_size:
            continue
        for node_raw in members:
            node = int(node_raw)
            if 0 <= node < n_nodes:
                node_groups[node].append((key, members))
    for entries in node_groups:
        entries.sort(
            key=lambda item: (
                0 if group_has_louvain(item[0]) else 1,
                int(item[1].shape[0]),
            )
        )

    dirs = [np.array([dx, dy], dtype=np.float64) for dx, dy in directions]
    if not dirs:
        dirs = [np.array([0.0, 0.0], dtype=np.float64)]
    candidates: list[Candidate] = []

    def add_endpoint_anchor(
        edge_rank: int,
        edge_idx: int,
        node: int,
        partner: int,
        priority: float,
    ) -> None:
        arr = np.array([node], dtype=np.int32)
        anchor = positions[partner]
        for radius in radii:
            for direction in dirs:
                target = anchor + direction.reshape(2) * radius
                candidates.append(
                    Candidate(
                        "cross_long_edge_endpoint_anchor",
                        arr,
                        targets=target.reshape(1, 2),
                        group_key=f"__cross_long_edge__{edge_rank}:{edge_idx}",
                        semantic_target_cross_incident=priority,
                    )
                )

    def add_group_anchor(
        edge_rank: int,
        edge_idx: int,
        node: int,
        partner: int,
        priority: float,
    ) -> None:
        anchor = positions[partner]
        seen: set[tuple[str, ...]] = set()
        for key, members in node_groups[node][:4]:
            if partner in set(int(x) for x in members.tolist()):
                continue
            token = tuple(str(x) for x in members.tolist())
            if token in seen:
                continue
            seen.add(token)
            centroid = positions[members].mean(axis=0)
            for radius in radii:
                for direction in dirs:
                    target_centroid = anchor + direction.reshape(2) * radius
                    delta = target_centroid - centroid
                    candidates.append(
                        Candidate(
                            "cross_long_edge_group_anchor",
                            members,
                            dx=float(delta[0]),
                            dy=float(delta[1]),
                            group_key=key,
                            semantic_target_size=int(members.shape[0]),
                            semantic_target_cross_incident=priority,
                        )
                    )

    def add_pair_anchor(
        edge_rank: int,
        edge_idx: int,
        s: int,
        t: int,
        priority: float,
    ) -> None:
        s_groups = node_groups[s][:3]
        t_groups = node_groups[t][:3]
        for s_key, s_members in s_groups:
            s_set = set(int(x) for x in s_members.tolist())
            for t_key, t_members in t_groups:
                t_set = set(int(x) for x in t_members.tolist())
                if s_set & t_set:
                    continue
                total = int(s_members.size + t_members.size)
                if total > max_group_size * 2:
                    continue
                indices = np.concatenate([s_members, t_members]).astype(np.int32)
                s_len = int(s_members.size)
                s_centroid = positions[s_members].mean(axis=0)
                t_centroid = positions[t_members].mean(axis=0)
                midpoint = (positions[s] + positions[t]) / 2.0
                sep = t_centroid - s_centroid
                sep_norm = float(np.linalg.norm(sep))
                if sep_norm < 1e-6:
                    continue
                along = sep / sep_norm
                normal = np.array([-along[1], along[0]], dtype=np.float64)
                pair_dirs = [along, -along, normal, -normal]
                for radius in radii[: max(1, min(4, len(radii)))]:
                    for direction in pair_dirs:
                        center_a = midpoint + direction * radius
                        center_b = midpoint - direction * radius
                        targets = positions[indices].copy()
                        targets[:s_len] += (center_a - s_centroid).reshape(1, 2)
                        targets[s_len:] += (center_b - t_centroid).reshape(1, 2)
                        candidates.append(
                            Candidate(
                                "cross_long_edge_group_pair_anchor",
                                indices,
                                targets=targets,
                                group_key=s_key,
                                other_group_key=t_key,
                                semantic_target_size=total,
                                semantic_target_cross_incident=priority,
                            )
                        )

    for rank, edge_idx_raw in enumerate(hot_edges):
        edge_idx = int(edge_idx_raw)
        s, t = edges[edge_idx]
        s_i = int(s)
        t_i = int(t)
        priority = float(edge_counts[edge_idx])
        add_endpoint_anchor(rank, edge_idx, s_i, t_i, priority)
        add_endpoint_anchor(rank, edge_idx, t_i, s_i, priority)
        add_group_anchor(rank, edge_idx, s_i, t_i, priority)
        add_group_anchor(rank, edge_idx, t_i, s_i, priority)
        add_pair_anchor(rank, edge_idx, s_i, t_i, priority)
    return candidates


def edge_node_relief_candidates(
    positions: np.ndarray,
    node_counts: np.ndarray,
    edge_counts: np.ndarray,
    edges: np.ndarray,
    collision_geometry: CollisionGeometry,
    current_edge_node_flags: np.ndarray,
    groups: dict[str, np.ndarray],
    max_hits: int,
    max_group_size: int,
    steps: list[float],
) -> list[Candidate]:
    """Generate moves from the current rendered edge/node collisions.

    This is the node-edge equivalent of crossing fan actions. It uses only the
    currently intersecting rendered rectangles and straight edge carriers, then
    lets the exact renderer gate decide whether the move survives.
    """
    if max_hits <= 0 or max_group_size <= 0 or not steps:
        return []
    pair_edge, pair_rect = collision_geometry.edge_node_pairs
    if pair_edge.size == 0 or current_edge_node_flags.size == 0:
        return []
    hit_idx = np.flatnonzero(current_edge_node_flags)
    if hit_idx.size == 0:
        return []

    rect_positions = collision_positions(positions, collision_geometry)
    rect_to_groups: dict[int, list[tuple[str, np.ndarray]]] = {}
    for key, raw_members in groups.items():
        members = np.asarray(raw_members, dtype=np.int32)
        if members.size < 2 or members.size > max_group_size:
            continue
        for node in members:
            node_i = int(node)
            if 0 <= node_i < len(collision_geometry.rect_effect_nodes_by_node):
                for rect_idx in collision_geometry.rect_effect_nodes_by_node[node_i]:
                    rect_to_groups.setdefault(int(rect_idx), []).append((key, members))

    def unit(vec: np.ndarray) -> np.ndarray | None:
        norm = float(np.linalg.norm(vec))
        if norm < 1e-6:
            return None
        return vec / norm

    def add_direction(dirs: list[np.ndarray], direction: np.ndarray | None) -> None:
        if direction is None:
            return
        for existing in dirs:
            if abs(float(np.dot(existing, direction))) > 0.985:
                return
        dirs.append(direction)

    ranked: list[tuple[float, int, int, int]] = []
    for hit in hit_idx:
        edge_idx = int(pair_edge[int(hit)])
        rect_idx = int(pair_rect[int(hit)])
        src, dst = edges[edge_idx]
        rect_node = int(collision_geometry.rect_node[rect_idx])
        rect_effect = collision_geometry.rect_effect_nodes_by_rect[rect_idx]
        rect_score = 0.0
        if rect_node >= 0:
            rect_score += float(node_counts[rect_node])
        elif rect_effect:
            effect = np.array(rect_effect, dtype=np.int32)
            rect_score += float(node_counts[effect].sum()) / math.sqrt(max(1, effect.size))
        priority = (
            float(edge_counts[edge_idx]) * 4.0
            + rect_score
            + float(node_counts[int(src)] + node_counts[int(dst)])
        )
        ranked.append((priority, int(hit), edge_idx, rect_idx))
    ranked.sort(reverse=True, key=lambda item: item[0])

    candidates: list[Candidate] = []
    seen_blockers: set[tuple[str, tuple[int, ...], int]] = set()
    for rank, (priority, _hit, edge_idx, rect_idx) in enumerate(ranked[:max_hits]):
        src, dst = edges[edge_idx]
        src_i = int(src)
        dst_i = int(dst)
        a = positions[src_i]
        b = positions[dst_i]
        edge_vec = b - a
        edge_unit = unit(edge_vec)
        if edge_unit is None:
            continue
        normal = np.array([-edge_unit[1], edge_unit[0]], dtype=np.float64)
        center = rect_positions[rect_idx]
        denom = float(np.dot(edge_vec, edge_vec))
        t = 0.0 if denom < 1e-9 else float(np.dot(center - a, edge_vec) / denom)
        t = max(0.0, min(1.0, t))
        closest = a + edge_vec * t
        away = unit(center - closest)
        dirs: list[np.ndarray] = []
        add_direction(dirs, away)
        add_direction(dirs, normal)
        add_direction(dirs, -normal)

        rect_effect = tuple(
            int(x) for x in collision_geometry.rect_effect_nodes_by_rect[rect_idx]
        )
        if not rect_effect:
            continue
        blocker_nodes = np.array(rect_effect, dtype=np.int32)
        group_key = f"__edge_node_hit__{rank}:{edge_idx}:{rect_idx}"
        for step in steps:
            for direction in dirs:
                key = ("blocker", tuple(blocker_nodes.tolist()), int(round(step)))
                if key not in seen_blockers:
                    seen_blockers.add(key)
                    candidates.append(
                        Candidate(
                            "edge_node_blocker_translate",
                            blocker_nodes,
                            dx=float(direction[0] * step),
                            dy=float(direction[1] * step),
                            group_key=group_key,
                            semantic_target_cross_incident=float(priority),
                        )
                    )
                edge_nodes = np.array([src_i, dst_i], dtype=np.int32)
                candidates.append(
                    Candidate(
                        "edge_node_edge_translate",
                        edge_nodes,
                        dx=float(-direction[0] * step),
                        dy=float(-direction[1] * step),
                        group_key=group_key,
                        semantic_target_cross_incident=float(priority),
                    )
                )
                for endpoint in (src_i, dst_i):
                    candidates.append(
                        Candidate(
                            "edge_node_endpoint_translate",
                            np.array([endpoint], dtype=np.int32),
                            dx=float(-direction[0] * step),
                            dy=float(-direction[1] * step),
                            group_key=group_key,
                            semantic_target_cross_incident=float(priority),
                        )
                    )

        for key, members in rect_to_groups.get(rect_idx, [])[:4]:
            if np.intersect1d(members, np.array([src_i, dst_i], dtype=np.int32)).size:
                continue
            for step in steps:
                for direction in dirs[:2]:
                    candidates.append(
                        Candidate(
                            "edge_node_group_translate",
                            members,
                            dx=float(direction[0] * step),
                            dy=float(direction[1] * step),
                            group_key=key,
                            semantic_target_size=int(members.shape[0]),
                            semantic_target_cross_incident=float(priority),
                        )
                    )
    return candidates


def _unit_vec(vec: np.ndarray) -> np.ndarray | None:
    norm = float(np.linalg.norm(vec))
    if norm < 1e-6:
        return None
    return vec / norm


def _add_unique_direction(dirs: list[np.ndarray], direction: np.ndarray | None) -> None:
    if direction is None:
        return
    for existing in dirs:
        if abs(float(np.dot(existing, direction))) > 0.985:
            return
    dirs.append(direction)


def _ranked_edge_node_hits(
    positions: np.ndarray,
    node_counts: np.ndarray,
    edge_counts: np.ndarray,
    edges: np.ndarray,
    collision_geometry: CollisionGeometry,
    current_edge_node_flags: np.ndarray,
) -> list[tuple[float, int, int, int]]:
    pair_edge, pair_rect = collision_geometry.edge_node_pairs
    if pair_edge.size == 0 or current_edge_node_flags.size == 0:
        return []
    hit_idx = np.flatnonzero(current_edge_node_flags)
    ranked: list[tuple[float, int, int, int]] = []
    for hit in hit_idx:
        edge_idx = int(pair_edge[int(hit)])
        rect_idx = int(pair_rect[int(hit)])
        src, dst = edges[edge_idx]
        rect_effect = collision_geometry.rect_effect_nodes_by_rect[rect_idx]
        rect_score = 0.0
        if rect_effect:
            effect = np.array(rect_effect, dtype=np.int32)
            rect_score = float(node_counts[effect].sum()) / math.sqrt(max(1, effect.size))
        priority = (
            float(edge_counts[edge_idx]) * 5.0
            + rect_score
            + float(node_counts[int(src)] + node_counts[int(dst)])
        )
        ranked.append((priority, int(hit), edge_idx, rect_idx))
    ranked.sort(reverse=True, key=lambda item: item[0])
    return ranked


def _edge_rect_clear_dirs(
    edge_a: np.ndarray,
    edge_b: np.ndarray,
    rect_center: np.ndarray,
) -> tuple[np.ndarray, list[np.ndarray], np.ndarray]:
    edge_vec = edge_b - edge_a
    edge_unit = _unit_vec(edge_vec)
    if edge_unit is None:
        return edge_vec, [], edge_a
    normal = np.array([-edge_unit[1], edge_unit[0]], dtype=np.float64)
    denom = float(np.dot(edge_vec, edge_vec))
    t = 0.0 if denom < 1e-9 else float(np.dot(rect_center - edge_a, edge_vec) / denom)
    t = max(0.0, min(1.0, t))
    closest = edge_a + edge_vec * t
    away = _unit_vec(rect_center - closest)
    signed = float(np.dot(rect_center - closest, normal))
    dirs: list[np.ndarray] = []
    _add_unique_direction(dirs, away)
    _add_unique_direction(dirs, normal if signed >= 0.0 else -normal)
    _add_unique_direction(dirs, -normal if signed >= 0.0 else normal)
    return edge_vec, dirs, closest


def edge_node_precise_clear_candidates(
    positions: np.ndarray,
    node_counts: np.ndarray,
    edge_counts: np.ndarray,
    edges: np.ndarray,
    collision_geometry: CollisionGeometry,
    current_edge_node_flags: np.ndarray,
    max_hits: int,
    pads: list[float],
    *,
    max_neighborhood_nodes: int,
    edge_node_margin: float,
    clear_padding: float,
) -> list[Candidate]:
    """Clear rendered edge-node collisions using actual rectangle extents."""
    if max_hits <= 0 or not pads:
        return []
    rect_positions = collision_positions(positions, collision_geometry)
    ranked = _ranked_edge_node_hits(
        positions,
        node_counts,
        edge_counts,
        edges,
        collision_geometry,
        current_edge_node_flags,
    )
    candidates: list[Candidate] = []
    for rank, (priority, _hit, edge_idx, rect_idx) in enumerate(ranked[:max_hits]):
        src, dst = edges[edge_idx]
        src_i = int(src)
        dst_i = int(dst)
        blocker_tuple = tuple(int(x) for x in collision_geometry.rect_effect_nodes_by_rect[rect_idx])
        if not blocker_tuple:
            continue
        blocker_nodes = np.array(blocker_tuple, dtype=np.int32)
        center = rect_positions[rect_idx]
        _edge_vec, dirs, closest = _edge_rect_clear_dirs(
            positions[src_i],
            positions[dst_i],
            center,
        )
        if not dirs:
            continue
        neighborhood = blocker_nodes
        if max_neighborhood_nodes > int(blocker_nodes.size):
            used = {int(x) for x in blocker_nodes.tolist()}
            incident = np.flatnonzero(
                np.isin(edges[:, 0], list(used)) | np.isin(edges[:, 1], list(used))
            )
            candidate_nodes = np.unique(edges[incident].reshape(-1)).astype(np.int32)
            ordered = sorted(
                [int(x) for x in candidate_nodes.tolist() if int(x) not in used],
                key=lambda node: float(node_counts[node]),
                reverse=True,
            )
            expanded = sorted(used)
            for node in ordered:
                if len(expanded) >= max_neighborhood_nodes:
                    break
                expanded.append(node)
            neighborhood = np.array(sorted(set(expanded)), dtype=np.int32)

        rect_w = float(collision_geometry.rect_widths[rect_idx])
        rect_h = float(collision_geometry.rect_heights[rect_idx])
        for direction in dirs[:3]:
            half_extent = (
                abs(float(direction[0])) * rect_w
                + abs(float(direction[1])) * rect_h
            ) / 2.0
            current_clearance = float(np.dot(center - closest, direction))
            needed = max(
                0.0,
                half_extent + edge_node_margin + clear_padding - current_clearance,
            )
            if needed <= 1e-6:
                continue
            for pad in pads:
                delta = direction * float(needed + pad)
                group_key = f"__edge_node_precise__{rank}:{edge_idx}:{rect_idx}"
                candidates.append(
                    Candidate(
                        "edge_node_blocker_precise_clear",
                        blocker_nodes,
                        dx=float(delta[0]),
                        dy=float(delta[1]),
                        group_key=group_key,
                        semantic_target_cross_incident=float(priority),
                    )
                )
                if neighborhood.size > blocker_nodes.size:
                    candidates.append(
                        Candidate(
                            "edge_node_neighborhood_precise_clear",
                            neighborhood,
                            targets=positions[neighborhood] + delta.reshape(1, 2),
                            group_key=group_key,
                            semantic_target_size=int(neighborhood.size),
                            semantic_target_cross_incident=float(priority),
                        )
                    )
    return candidates


def edge_node_corridor_clear_candidates(
    positions: np.ndarray,
    node_counts: np.ndarray,
    edge_counts: np.ndarray,
    edges: np.ndarray,
    collision_geometry: CollisionGeometry,
    current_edge_node_flags: np.ndarray,
    *,
    max_edges: int,
    max_blockers_per_edge: int,
    pads: list[float],
    edge_node_margin: float,
    clear_padding: float,
) -> list[Candidate]:
    """Move multiple blocker rectangles out of the same edge corridor."""
    if max_edges <= 0 or max_blockers_per_edge <= 0 or not pads:
        return []
    pair_edge, pair_rect = collision_geometry.edge_node_pairs
    hit_idx = np.flatnonzero(current_edge_node_flags)
    by_edge: dict[int, list[int]] = {}
    for hit_raw in hit_idx:
        hit = int(hit_raw)
        by_edge.setdefault(int(pair_edge[hit]), []).append(int(pair_rect[hit]))
    if not by_edge:
        return []
    ranked_edges = sorted(
        (
            (
                float(edge_counts[edge_idx]) * 4.0 + float(len(rects)) * 12.0,
                edge_idx,
                sorted(set(rects)),
            )
            for edge_idx, rects in by_edge.items()
        ),
        reverse=True,
        key=lambda item: item[0],
    )[:max_edges]
    rect_positions = collision_positions(positions, collision_geometry)
    candidates: list[Candidate] = []
    for edge_rank, (edge_priority, edge_idx, rects) in enumerate(ranked_edges):
        src, dst = edges[edge_idx]
        a = positions[int(src)]
        b = positions[int(dst)]
        edge_vec = b - a
        edge_unit = _unit_vec(edge_vec)
        if edge_unit is None:
            continue
        normal = np.array([-edge_unit[1], edge_unit[0]], dtype=np.float64)
        denom = float(np.dot(edge_vec, edge_vec))
        rows: list[tuple[float, int, np.ndarray, np.ndarray, float, float]] = []
        for rect_idx in rects:
            blocker_tuple = collision_geometry.rect_effect_nodes_by_rect[rect_idx]
            if not blocker_tuple:
                continue
            nodes = np.array(blocker_tuple, dtype=np.int32)
            center = rect_positions[rect_idx]
            t = 0.0 if denom < 1e-9 else float(np.dot(center - a, edge_vec) / denom)
            t = max(0.0, min(1.0, t))
            closest = a + edge_vec * t
            rect_w = float(collision_geometry.rect_widths[rect_idx])
            rect_h = float(collision_geometry.rect_heights[rect_idx])
            half_extent = (abs(float(normal[0])) * rect_w + abs(float(normal[1])) * rect_h) / 2.0
            signed = float(np.dot(center - closest, normal))
            score = (
                edge_priority
                + float(node_counts[nodes].sum()) / math.sqrt(max(1, int(nodes.size)))
                + half_extent * 0.01
            )
            rows.append((score, rect_idx, nodes, center, signed, half_extent))
        rows.sort(reverse=True, key=lambda item: item[0])
        if not rows:
            continue
        sizes = []
        for size in (2, 3, 4, 6, 8, 12, 16, max_blockers_per_edge):
            got = min(size, len(rows), max_blockers_per_edge)
            if got > 0 and got not in sizes:
                sizes.append(got)
        for batch_size in sizes:
            batch = rows[:batch_size]
            for mode in ("split", "positive", "negative"):
                for pad in pads:
                    targets_by_node: dict[int, tuple[float, np.ndarray]] = {}
                    for _score, _rect_idx, nodes, center, signed, half_extent in batch:
                        if mode == "split":
                            side = 1.0 if signed >= 0.0 else -1.0
                        else:
                            side = 1.0 if mode == "positive" else -1.0
                        direction = normal * side
                        t = 0.0 if denom < 1e-9 else float(np.dot(center - a, edge_vec) / denom)
                        t = max(0.0, min(1.0, t))
                        closest = a + edge_vec * t
                        clearance = float(np.dot(center - closest, direction))
                        needed = max(
                            0.0,
                            half_extent + edge_node_margin + clear_padding - clearance,
                        )
                        delta = direction * float(needed + pad)
                        if float(np.linalg.norm(delta)) <= 1e-6:
                            continue
                        delta_norm = float(np.linalg.norm(delta))
                        for node_raw in nodes:
                            node = int(node_raw)
                            target = positions[node] + delta
                            old = targets_by_node.get(node)
                            if old is None or delta_norm > old[0]:
                                targets_by_node[node] = (delta_norm, target)
                    if not targets_by_node:
                        continue
                    items = sorted(targets_by_node.items())
                    cand_nodes = np.array([node for node, _row in items], dtype=np.int32)
                    targets = np.vstack([row[1] for _node, row in items]).astype(np.float64)
                    candidates.append(
                        Candidate(
                            "edge_node_corridor_clear",
                            cand_nodes,
                            targets=targets,
                            group_key=f"__edge_node_corridor__{edge_rank}:{edge_idx}:{mode}",
                            semantic_target_size=int(cand_nodes.size),
                            semantic_target_cross_incident=float(edge_priority),
                        )
                    )
    return candidates


def edge_node_force_relax_candidates(
    positions: np.ndarray,
    node_counts: np.ndarray,
    edge_counts: np.ndarray,
    edges: np.ndarray,
    collision_geometry: CollisionGeometry,
    current_edge_node_flags: np.ndarray,
    *,
    max_hits: int,
    max_nodes: int,
    scales: list[float],
    edge_node_margin: float,
    clear_padding: float,
) -> list[Candidate]:
    """Accumulate collision forces and try smooth blocker moves."""
    if max_hits <= 0 or max_nodes <= 0 or not scales:
        return []
    ranked = _ranked_edge_node_hits(
        positions,
        node_counts,
        edge_counts,
        edges,
        collision_geometry,
        current_edge_node_flags,
    )
    if not ranked:
        return []
    rect_positions = collision_positions(positions, collision_geometry)
    forces = np.zeros_like(positions, dtype=np.float64)
    weights = np.zeros(positions.shape[0], dtype=np.float64)
    for priority, _hit, edge_idx, rect_idx in ranked[:max_hits]:
        src, dst = edges[edge_idx]
        a = positions[int(src)]
        b = positions[int(dst)]
        edge_vec = b - a
        edge_unit = _unit_vec(edge_vec)
        if edge_unit is None:
            continue
        normal = np.array([-edge_unit[1], edge_unit[0]], dtype=np.float64)
        center = rect_positions[rect_idx]
        denom = float(np.dot(edge_vec, edge_vec))
        t = 0.0 if denom < 1e-9 else float(np.dot(center - a, edge_vec) / denom)
        t = max(0.0, min(1.0, t))
        closest = a + edge_vec * t
        signed = float(np.dot(center - closest, normal))
        direction = normal if signed >= 0.0 else -normal
        rect_w = float(collision_geometry.rect_widths[rect_idx])
        rect_h = float(collision_geometry.rect_heights[rect_idx])
        half_extent = (abs(float(direction[0])) * rect_w + abs(float(direction[1])) * rect_h) / 2.0
        clearance = float(np.dot(center - closest, direction))
        penetration = max(0.0, half_extent + edge_node_margin + clear_padding - clearance)
        if penetration <= 1e-6:
            continue
        rect_effect = collision_geometry.rect_effect_nodes_by_rect[rect_idx]
        share = max(1.0, math.sqrt(float(len(rect_effect))))
        delta = direction * min(900.0, penetration) * (1.0 + min(5.0, priority / 50.0) * 0.08) / share
        for node_raw in rect_effect:
            node = int(node_raw)
            forces[node] += delta
            weights[node] += 1.0
    active = np.flatnonzero(weights > 0.0)
    if active.size == 0:
        return []
    norms = np.linalg.norm(forces[active], axis=1)
    order = active[np.argsort(-norms)][:max_nodes]
    candidates: list[Candidate] = []
    emitted: set[int] = set()
    for wanted in (8, 16, 32, 48, 64, 96, 128, max_nodes):
        size = min(int(wanted), int(order.size))
        if size <= 0 or size in emitted:
            continue
        emitted.add(size)
        nodes = np.array(order[:size], dtype=np.int32)
        base_delta = forces[nodes] / np.maximum(weights[nodes], 1.0).reshape(-1, 1)
        for scale in scales:
            clipped = base_delta * float(scale)
            lengths = np.linalg.norm(clipped, axis=1)
            over = lengths > 1200.0
            if np.any(over):
                clipped[over] *= (1200.0 / lengths[over]).reshape(-1, 1)
            candidates.append(
                Candidate(
                    "edge_node_force_relax",
                    nodes,
                    targets=positions[nodes] + clipped,
                    group_key=f"__edge_node_force__n={size}",
                    semantic_target_size=int(nodes.size),
                    semantic_target_cross_incident=float(norms[:size].sum()),
                )
            )
    return candidates


def crossing_hotspot_endpoint_bypass_candidates(
    positions: np.ndarray,
    node_counts: np.ndarray,
    edge_counts: np.ndarray,
    edges: np.ndarray,
    evaluator: fce.FastCrossEval,
    groups: dict[str, np.ndarray],
    max_hotspots: int,
    edges_per_hotspot: int,
    max_group_size: int,
    cell_size: float,
    steps: list[float],
) -> list[Candidate]:
    """Pivot endpoint-side nodes/groups away from dense crossing hotspots."""
    if (
        max_hotspots <= 0
        or edges_per_hotspot <= 0
        or max_group_size <= 0
        or cell_size <= 0
        or not steps
    ):
        return []
    cross_flags = crossing_flags_for_pairs(positions, evaluator)
    if not np.any(cross_flags):
        return []

    pair_i = evaluator.i[cross_flags]
    pair_j = evaluator.j[cross_flags]
    points = crossing_points_for_pairs(positions, evaluator, cross_flags)
    if points.shape[0] == 0:
        return []

    buckets: dict[tuple[int, int], list[int]] = defaultdict(list)
    for idx, point in enumerate(points):
        buckets[
            (
                int(math.floor(float(point[0]) / cell_size)),
                int(math.floor(float(point[1]) / cell_size)),
            )
        ].append(idx)
    ranked_buckets = sorted(buckets.items(), key=lambda item: len(item[1]), reverse=True)

    selected_hotspots: list[tuple[np.ndarray, np.ndarray]] = []
    for _cell, raw_indices in ranked_buckets:
        pair_indices = np.array(raw_indices, dtype=np.int32)
        center = points[pair_indices].mean(axis=0)
        if any(
            abs(float(center[0] - other_center[0])) < cell_size * 0.8
            and abs(float(center[1] - other_center[1])) < cell_size * 0.8
            for _other_pairs, other_center in selected_hotspots
        ):
            continue
        selected_hotspots.append((pair_indices, center))
        if len(selected_hotspots) >= max_hotspots:
            break
    if not selected_hotspots:
        return []

    n_nodes = positions.shape[0]
    owner = np.full(n_nodes, -1, dtype=np.int32)
    group_entries: list[tuple[str, np.ndarray]] = []
    for key, raw_members in groups.items():
        members = np.asarray(raw_members, dtype=np.int32)
        if members.shape[0] < 2 or members.shape[0] > max_group_size:
            continue
        if np.any(owner[members] >= 0):
            continue
        gid = len(group_entries)
        owner[members] = gid
        group_entries.append((key, members))

    candidates: list[Candidate] = []

    def unit(vec: np.ndarray) -> np.ndarray | None:
        norm = float(np.linalg.norm(vec))
        if norm < 1e-6:
            return None
        return vec / norm

    def add_direction(dirs: list[np.ndarray], direction: np.ndarray | None) -> None:
        if direction is None:
            return
        for existing in dirs:
            if abs(float(np.dot(existing, direction))) > 0.985:
                return
        dirs.append(direction)

    def moved_options(endpoint: int) -> list[tuple[str, np.ndarray]]:
        opts = [("__hotspot_endpoint__", np.array([endpoint], dtype=np.int32))]
        gid = int(owner[endpoint])
        if gid >= 0:
            key, members = group_entries[gid]
            opts.append((key, members))
        return opts

    for hotspot_rank, (pair_indices, center) in enumerate(selected_hotspots):
        local_edges = np.concatenate([pair_i[pair_indices], pair_j[pair_indices]])
        if local_edges.size == 0:
            continue
        uniq_edges, counts = np.unique(local_edges, return_counts=True)
        priority = counts.astype(np.float64) * np.maximum(
            1.0,
            edge_counts[uniq_edges].astype(np.float64),
        )
        order = np.argsort(-priority)[:edges_per_hotspot]
        for edge_order, local_idx in enumerate(order):
            edge_idx = int(uniq_edges[local_idx])
            local_cross_count = int(counts[local_idx])
            s_raw, t_raw = edges[edge_idx]
            s = int(s_raw)
            t = int(t_raw)
            p0 = positions[s]
            p1 = positions[t]
            edge_vec = p1 - p0
            along = unit(edge_vec)
            if along is None:
                continue
            normal = np.array([-along[1], along[0]], dtype=np.float64)
            edge_mid = (p0 + p1) / 2.0
            rel_center = unit(edge_mid - center)

            dirs: list[np.ndarray] = []
            add_direction(dirs, normal)
            add_direction(dirs, -normal)
            add_direction(dirs, rel_center)
            if rel_center is not None:
                add_direction(dirs, -rel_center)
            # Diagonal pivots keep the endpoint from simply sliding along the
            # same congested line when the edge is nearly parallel to a fan.
            add_direction(dirs, unit(normal + along * 0.45))
            add_direction(dirs, unit(-normal + along * 0.45))
            add_direction(dirs, unit(normal - along * 0.45))
            add_direction(dirs, unit(-normal - along * 0.45))

            semantic_score = (
                float(local_cross_count) * 8.0
                + float(edge_counts[edge_idx]) * 2.0
            )
            group_key_base = f"__hotspot_bypass__{hotspot_rank}:{edge_idx}:{edge_order}"
            for endpoint in (s, t):
                for option_key, members in moved_options(endpoint):
                    if members.shape[0] > 1 and int(node_counts[members].sum()) <= 0:
                        continue
                    group_key = option_key if option_key != "__hotspot_endpoint__" else group_key_base
                    for step in steps:
                        for direction in dirs[:6]:
                            candidates.append(
                                Candidate(
                                    "cross_hotspot_endpoint_bypass",
                                    members,
                                    dx=float(direction[0] * step),
                                    dy=float(direction[1] * step),
                                    group_key=group_key,
                                    semantic_target_cross_incident=semantic_score
                                    + float(node_counts[members].sum()),
                                )
                            )
    return candidates


def crossing_endpoint_partner_orbit_candidates(
    positions: np.ndarray,
    edge_counts: np.ndarray,
    edges: np.ndarray,
    max_edges: int,
    radii: list[float],
    directions: list[tuple[float, float]],
) -> list[Candidate]:
    """Place hot crossing endpoints around the other endpoint of that edge."""
    if max_edges <= 0 or not radii:
        return []
    ranked = np.argsort(-edge_counts)[:max_edges]
    ranked = ranked[edge_counts[ranked] > 0]
    candidates: list[Candidate] = []
    for rank, edge_idx_raw in enumerate(ranked):
        edge_idx = int(edge_idx_raw)
        s = int(edges[edge_idx, 0])
        t = int(edges[edge_idx, 1])
        group_key = f"__cross_partner_orbit__{rank}:{edge_idx}"
        for endpoint, partner in ((s, t), (t, s)):
            arr = np.array([endpoint], dtype=np.int32)
            anchor = positions[partner]
            for radius in radii:
                if radius <= 0:
                    targets = anchor.reshape(1, 2).copy()
                    candidates.append(
                        Candidate(
                            "cross_endpoint_partner_orbit",
                            arr,
                            targets=targets,
                            group_key=group_key,
                            semantic_target_cross_incident=float(edge_counts[edge_idx]),
                        )
                    )
                    continue
                for dx, dy in directions:
                    target = anchor + np.array([dx * radius, dy * radius])
                    candidates.append(
                        Candidate(
                            "cross_endpoint_partner_orbit",
                            arr,
                            targets=target.reshape(1, 2),
                            group_key=group_key,
                            semantic_target_cross_incident=float(edge_counts[edge_idx]),
                        )
                    )
    return candidates


def crossing_pair_endpoint_swap_candidates(
    positions: np.ndarray,
    edge_counts: np.ndarray,
    edges: np.ndarray,
    evaluator: fce.FastCrossEval,
    max_pairs: int,
) -> list[Candidate]:
    """Swap endpoints of currently crossing edge pairs.

    This is intentionally drastic but still generic and exact-gated. It tests
    whether two endpoint placements are simply on the wrong side of a local
    crossing pair without relying on model ids or final-layout coordinates.
    """
    if max_pairs <= 0:
        return []
    cross_flags = crossing_flags_for_pairs(positions, evaluator)
    if not np.any(cross_flags):
        return []
    pair_i = evaluator.i[cross_flags]
    pair_j = evaluator.j[cross_flags]
    priority = edge_counts[pair_i] + edge_counts[pair_j]
    order = np.argsort(-priority)[:max_pairs]
    candidates: list[Candidate] = []
    for rank, pair_idx in enumerate(order):
        e1 = int(pair_i[pair_idx])
        e2 = int(pair_j[pair_idx])
        edge1 = [int(edges[e1, 0]), int(edges[e1, 1])]
        edge2 = [int(edges[e2, 0]), int(edges[e2, 1])]
        if len(set(edge1 + edge2)) != 4:
            continue
        group_key = f"__cross_endpoint_swap__{rank}:{e1}:{e2}"
        for a in edge1:
            for b in edge2:
                indices = np.array([a, b], dtype=np.int32)
                targets = np.array([positions[b], positions[a]], dtype=np.float64)
                candidates.append(
                    Candidate(
                        "cross_pair_endpoint_swap",
                        indices,
                        targets=targets,
                        group_key=group_key,
                        semantic_target_cross_incident=float(priority[pair_idx]),
                    )
                )
    return candidates


def write_graph_tsvs(layout: dict, out_dir: Path) -> tuple[Path, Path]:
    nodes_path = out_dir / "nodes.tsv"
    edges_path = out_dir / "edges.tsv"
    with nodes_path.open("w") as out:
        for nd in layout["nodes"]:
            model_id = str(nd["modelId"])
            size = nd.get("size") or {}
            pos = nd.get("position") or {}
            app_label = nd.get("appLabel") or model_id.split(".", 1)[0]
            out.write(
                f"{model_id}\t{float(size.get('width', 180.0)):.3f}"
                f"\t{float(size.get('height', 76.0)):.3f}"
                f"\t{float(pos.get('x', 0.0)):.3f}"
                f"\t{float(pos.get('y', 0.0)):.3f}"
                f"\t{app_label}\n"
            )
    with edges_path.open("w") as out:
        for edge in layout.get("routedEdges", []):
            edge_id = str(edge.get("edgeId") or "")
            source = str(edge.get("sourceModelId") or "")
            target = str(edge.get("targetModelId") or "")
            if not edge_id or not source or not target or source == target:
                continue
            kind = str(edge.get("kind") or "foreign_key")
            provenance = str(edge.get("provenance") or "")
            if not provenance:
                parts = edge_id.split(":")
                provenance = parts[1] if len(parts) > 2 else "declared"
            out.write(f"{edge_id}\t{source}\t{target}\t{kind}\t{provenance}\n")
    return nodes_path, edges_path


def run_rigid_measure(positions_tsv: Path, out_json: Path, layout: dict) -> dict:
    with tempfile.TemporaryDirectory(prefix="v34-rigid-graph-") as tmp:
        nodes_tsv, edges_tsv = write_graph_tsvs(layout, Path(tmp))
        proc = subprocess.run(
            [
                str(BINARY),
                "layout",
                "--mode",
                "hierarchical_barycenter",
                "--nodes-file",
                str(nodes_tsv),
                "--edges-file",
                str(edges_tsv),
                "--edge-routing",
                "straight",
                "--cluster-graph",
                "1",
                "--positions-tsv",
                str(positions_tsv),
                "--rigid-positions",
                "1",
            ],
            capture_output=True,
            text=True,
        )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr[:800])
    out_json.write_text(proc.stdout)
    return json.loads(proc.stdout).get("engineMetadata", {})


def run_rigid_measure_legacy(positions_tsv: Path, out_json: Path) -> dict:
    proc = subprocess.run(
        [
            str(BINARY),
            "layout",
            "--mode",
            "hierarchical_barycenter",
            "--nodes-file",
            str(NODES_TSV),
            "--edges-file",
            str(EDGES_TSV),
            "--edge-routing",
            "straight",
            "--cluster-graph",
            "1",
            "--positions-tsv",
            str(positions_tsv),
            "--rigid-positions",
            "1",
        ],
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr[:800])
    out_json.write_text(proc.stdout)
    return json.loads(proc.stdout).get("engineMetadata", {})


def parse_steps(text: str) -> list[float]:
    return [float(part) for part in text.split(",") if part.strip()]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--positions", type=Path, default=None)
    parser.add_argument("--out-tsv", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=12)
    parser.add_argument("--top-nodes", type=int, default=28)
    parser.add_argument("--top-edges", type=int, default=20)
    parser.add_argument("--top-clusters", type=int, default=16)
    parser.add_argument("--steps", default="250,500,1000,1800")
    parser.add_argument("--anchor-radii", default="250,500,900,1500")
    parser.add_argument("--cross-pair-candidates", type=int, default=0)
    parser.add_argument(
        "--cross-pair-steps",
        default="",
        help="comma-separated steps for crossing-pair moves; defaults to --steps",
    )
    parser.add_argument("--cross-group-fan-groups", type=int, default=0)
    parser.add_argument("--cross-group-fan-max-size", type=int, default=100)
    parser.add_argument("--cross-carrier-pairs", type=int, default=0)
    parser.add_argument("--cross-carrier-max-size", type=int, default=100)
    parser.add_argument("--cross-long-edge-anchors", type=int, default=0)
    parser.add_argument("--cross-long-edge-max-group-size", type=int, default=100)
    parser.add_argument("--cross-long-edge-min-crossings", type=int, default=8)
    parser.add_argument(
        "--cross-long-edge-radii",
        default="0,300,700,1200,2200,3600,5600",
    )
    parser.add_argument("--cross-hotspots", type=int, default=0)
    parser.add_argument("--cross-hotspot-edges", type=int, default=8)
    parser.add_argument("--cross-hotspot-max-group-size", type=int, default=100)
    parser.add_argument("--cross-hotspot-cell-size", type=float, default=900.0)
    parser.add_argument("--cross-endpoint-orbit-edges", type=int, default=0)
    parser.add_argument("--cross-pair-endpoint-swaps", type=int, default=0)
    parser.add_argument("--swap-pairs", type=int, default=20)
    parser.add_argument("--cluster-swap-pairs", type=int, default=20)
    parser.add_argument("--overlap-candidates", type=int, default=0)
    parser.add_argument("--overlap-spread-components", type=int, default=0)
    parser.add_argument("--overlap-spread-spacing-scale", type=float, default=1.6)
    parser.add_argument("--overlap-spread-rotations", type=int, default=6)
    parser.add_argument("--overlap-spread-batch-sizes", default="")
    parser.add_argument("--loose-pack-groups", type=int, default=0)
    parser.add_argument("--loose-pack-spacing-scale", type=float, default=1.8)
    parser.add_argument("--loose-pack-rotations", type=int, default=4)
    parser.add_argument("--loose-anchor-nodes", type=int, default=0)
    parser.add_argument("--loose-anchor-radii", default="150,300,600,1000,1800,3000")
    parser.add_argument("--semantic-anchor-nodes", type=int, default=0)
    parser.add_argument("--semantic-anchor-top-clusters", type=int, default=4)
    parser.add_argument("--semantic-anchor-max-degree", type=int, default=0)
    parser.add_argument("--semantic-anchor-max-group-size", type=int, default=160)
    parser.add_argument("--semantic-anchor-min-score", type=float, default=0.35)
    parser.add_argument("--semantic-anchor-radii", default="0,250,600,1200")
    parser.add_argument("--group-anchor-groups", type=int, default=0)
    parser.add_argument("--group-anchor-max-size", type=int, default=80)
    parser.add_argument(
        "--group-anchor-radii",
        default="0,300,600,1000,1800,3000,5000,8000,12000,18000",
    )
    parser.add_argument("--component-anchor-components", type=int, default=0)
    parser.add_argument("--component-anchor-max-group-size", type=int, default=8)
    parser.add_argument("--component-anchor-max-total-nodes", type=int, default=80)
    parser.add_argument(
        "--component-anchor-radii",
        default="0,300,600,1000,1800,3000,5000,8000,12000,18000,26000,38000",
    )
    parser.add_argument("--bundle-orbit-groups", type=int, default=0)
    parser.add_argument("--bundle-orbit-rotations", type=int, default=12)
    parser.add_argument("--bundle-orbit-radius-scales", default="0.75,1.0,1.35")
    parser.add_argument("--edge-node-relief-hits", type=int, default=0)
    parser.add_argument("--edge-node-relief-max-group-size", type=int, default=120)
    parser.add_argument("--edge-node-relief-steps", default="")
    parser.add_argument("--max-candidates", type=int, default=0)
    parser.add_argument("--final-max-candidates", type=int, default=0)
    parser.add_argument("--final-per-action-candidates", type=int, default=0)
    parser.add_argument("--log-jsonl", type=Path, default=None)
    parser.add_argument("--cooldown-rounds", type=int, default=0)
    parser.add_argument("--full-candidate-measure", action="store_true")
    parser.add_argument(
        "--count-bundle-nodes",
        action="store_true",
        help="count all node pairs for overlap repair instead of skipping "
             "nodes absorbed by leaf bundles",
    )
    parser.add_argument("--overlap-margin", type=float, default=8.0)
    parser.add_argument("--overlap-weight", type=float, default=30.0)
    parser.add_argument("--edge-node-margin", type=float, default=0.0)
    parser.add_argument("--edge-node-weight", type=float, default=0.0)
    parser.add_argument("--bbox-weight", type=float, default=120.0)
    parser.add_argument("--bbox-target-b", type=float, default=4.0)
    parser.add_argument(
        "--carrier-cross",
        action="store_true",
        help="optimize carrier-grouped crossing count, matching the C++ "
             "non-rigid edgeCrossings metric more closely",
    )
    parser.add_argument("--min-gain", type=float, default=1.0)
    parser.add_argument("--measure-rigid", action="store_true")
    args = parser.parse_args()

    layout = load_layout(args.layout)
    model_ids = node_model_ids(layout)
    positions = (
        read_positions_tsv(args.positions, layout)
        if args.positions
        else layout_positions(layout)
    )
    edges = graph_edges(layout)
    widths, heights = render_node_sizes(layout, edges)
    active_mask = render_active_overlap_mask(layout, args.count_bundle_nodes)
    collision_geometry = build_render_collision_geometry(
        layout,
        edges,
        args.count_bundle_nodes,
    )
    overlap_pairs = collision_geometry.overlap_pairs
    evaluator = fce.FastCrossEval(edges, positions.shape[0])
    adjacency = adjacency_from_edges(edges, positions.shape[0])
    clusters = cluster_members(layout)
    pseudo_groups = no_cluster_pseudo_groups(layout, edges)
    clusters.update(pseudo_groups)
    cluster_mask = np.array(
        [bool(nd.get("clusterId") or "") for nd in layout["nodes"]],
        dtype=bool,
    )
    steps = parse_steps(args.steps)
    anchor_radii = parse_steps(args.anchor_radii)
    cross_pair_steps = (
        parse_steps(args.cross_pair_steps)
        if args.cross_pair_steps.strip()
        else steps
    )
    cross_long_edge_radii = parse_steps(args.cross_long_edge_radii)
    loose_anchor_radii = parse_steps(args.loose_anchor_radii)
    semantic_anchor_radii = parse_steps(args.semantic_anchor_radii)
    group_anchor_radii = parse_steps(args.group_anchor_radii)
    component_anchor_radii = parse_steps(args.component_anchor_radii)
    bundle_orbit_radius_scales = parse_steps(args.bundle_orbit_radius_scales)
    edge_node_relief_steps = (
        parse_steps(args.edge_node_relief_steps)
        if args.edge_node_relief_steps.strip()
        else []
    )
    overlap_spread_batch_sizes = [
        int(float(part))
        for part in args.overlap_spread_batch_sizes.split(",")
        if part.strip()
    ]

    metrics = measure(
        positions,
        evaluator,
        widths,
        heights,
        active_mask,
        overlap_pairs,
        args.overlap_margin,
        args.overlap_weight,
        args.bbox_weight,
        args.bbox_target_b,
        build_carrier_pair_keys(layout, positions, edges, evaluator)
        if args.carrier_cross
        else None,
        edge_node_margin=args.edge_node_margin,
        edge_node_weight=args.edge_node_weight,
        collision_geometry=collision_geometry,
    )
    print(
        f"initial cross={metrics.cross} overlaps={metrics.overlaps} "
        f"edgeNode={metrics.edge_node} visualCross={metrics.visual_cross} "
        f"bbox={metrics.bbox_b:.2f}B score={metrics.score:.1f}"
    )
    best_metrics = metrics
    best_positions = positions.copy()
    started = time.time()
    accepted = 0
    log_handle = args.log_jsonl.open("w") if args.log_jsonl else None
    cooldown = np.zeros(positions.shape[0], dtype=np.int32)
    for round_idx in range(args.rounds):
        if args.cooldown_rounds > 0:
            cooldown = np.maximum(0, cooldown - 1)
        _cross, node_counts, edge_counts = crossing_incident_counts(
            positions, evaluator
        )
        carrier_pair_keys = (
            build_carrier_pair_keys(layout, positions, edges, evaluator)
            if args.carrier_cross
            else None
        )
        current_rect_positions = collision_positions(positions, collision_geometry)
        current_edge_node_flags = edge_rect_intersection_flags(
            positions,
            current_rect_positions,
            collision_geometry.rect_widths,
            collision_geometry.rect_heights,
            edges,
            collision_geometry.edge_node_pairs[0],
            collision_geometry.edge_node_pairs[1],
            args.edge_node_margin,
        )
        candidates = generate_candidates(
            positions,
            node_counts,
            edge_counts,
            edges,
            adjacency,
            clusters,
            args.top_nodes,
            args.top_edges,
            args.top_clusters,
            steps,
            anchor_radii,
            args.swap_pairs,
            args.cluster_swap_pairs,
            args.max_candidates,
            cooldown if args.cooldown_rounds > 0 else None,
        )
        if args.cross_pair_candidates > 0:
            candidates.extend(
                crossing_pair_candidates(
                    positions,
                    node_counts,
                    edge_counts,
                    edges,
                    evaluator,
                    args.cross_pair_candidates,
                    cross_pair_steps,
                )
            )
        if args.cross_group_fan_groups > 0:
            candidates.extend(
                crossing_group_fan_candidates(
                    positions,
                    node_counts,
                    edge_counts,
                    edges,
                    evaluator,
                    clusters,
                    args.cross_group_fan_groups,
                    args.cross_group_fan_max_size,
                    cross_pair_steps,
                )
            )
        if args.cross_carrier_pairs > 0:
            candidates.extend(
                crossing_carrier_pair_separation_candidates(
                    positions,
                    node_counts,
                    edge_counts,
                    edges,
                    evaluator,
                    clusters,
                    args.cross_carrier_pairs,
                    args.cross_carrier_max_size,
                    cross_pair_steps,
                )
            )
        if args.cross_long_edge_anchors > 0:
            candidates.extend(
                crossing_long_edge_anchor_candidates(
                    positions,
                    node_counts,
                    edge_counts,
                    edges,
                    clusters,
                    args.cross_long_edge_anchors,
                    args.cross_long_edge_max_group_size,
                    cross_long_edge_radii,
                    unit_directions(),
                    args.cross_long_edge_min_crossings,
                )
            )
        if args.cross_hotspots > 0:
            candidates.extend(
                crossing_hotspot_endpoint_bypass_candidates(
                    positions,
                    node_counts,
                    edge_counts,
                    edges,
                    evaluator,
                    clusters,
                    args.cross_hotspots,
                    args.cross_hotspot_edges,
                    args.cross_hotspot_max_group_size,
                    args.cross_hotspot_cell_size,
                    cross_pair_steps,
                )
            )
        if args.cross_endpoint_orbit_edges > 0:
            candidates.extend(
                crossing_endpoint_partner_orbit_candidates(
                    positions,
                    edge_counts,
                    edges,
                    args.cross_endpoint_orbit_edges,
                    cross_long_edge_radii,
                    unit_directions(),
                )
            )
        if args.cross_pair_endpoint_swaps > 0:
            candidates.extend(
                crossing_pair_endpoint_swap_candidates(
                    positions,
                    edge_counts,
                    edges,
                    evaluator,
                    args.cross_pair_endpoint_swaps,
                )
            )
        if args.overlap_candidates > 0:
            candidates.extend(
                generate_overlap_candidates(
                    positions,
                    widths,
                    heights,
                    node_counts,
                    args.overlap_margin,
                    active_mask,
                    args.overlap_candidates,
                )
            )
        if args.overlap_spread_components > 0:
            candidates.extend(
                overlap_component_spread_candidates(
                    positions,
                    widths,
                    heights,
                    node_counts,
                    active_mask,
                    args.overlap_margin,
                    args.overlap_spread_components,
                    args.overlap_spread_spacing_scale,
                    args.overlap_spread_rotations,
                    overlap_spread_batch_sizes,
                )
            )
        if args.loose_pack_groups > 0:
            candidates.extend(
                loose_pack_candidates(
                    positions,
                    widths,
                    heights,
                    node_counts,
                    adjacency,
                    pseudo_groups,
                    args.loose_pack_groups,
                    args.loose_pack_spacing_scale,
                    args.loose_pack_rotations,
                )
            )
        if args.loose_anchor_nodes > 0:
            candidates.extend(
                loose_node_anchor_candidates(
                    positions,
                    node_counts,
                    adjacency,
                    cluster_mask,
                    args.loose_anchor_nodes,
                    loose_anchor_radii,
                    unit_directions(),
                )
            )
        if args.semantic_anchor_nodes > 0:
            candidates.extend(
                semantic_anchor_candidates(
                    layout,
                    positions,
                    node_counts,
                    adjacency,
                    clusters,
                    args.semantic_anchor_nodes,
                    args.semantic_anchor_top_clusters,
                    args.semantic_anchor_max_degree,
                    args.semantic_anchor_max_group_size,
                    args.semantic_anchor_min_score,
                    semantic_anchor_radii,
                    unit_directions(),
                )
            )
        if args.group_anchor_groups > 0:
            candidates.extend(
                group_anchor_candidates(
                    positions,
                    node_counts,
                    adjacency,
                    clusters,
                    args.group_anchor_groups,
                    args.group_anchor_max_size,
                    group_anchor_radii,
                    unit_directions(),
                )
            )
        if args.component_anchor_components > 0:
            candidates.extend(
                component_anchor_candidates(
                    positions,
                    node_counts,
                    edges,
                    adjacency,
                    clusters,
                    args.component_anchor_components,
                    args.component_anchor_max_group_size,
                    args.component_anchor_max_total_nodes,
                    component_anchor_radii,
                    unit_directions(),
                )
            )
        if args.bundle_orbit_groups > 0:
            candidates.extend(
                bundle_orbit_candidates(
                    positions,
                    widths,
                    heights,
                    node_counts,
                    layout,
                    args.bundle_orbit_groups,
                    args.bundle_orbit_rotations,
                    bundle_orbit_radius_scales,
                )
            )
        if args.edge_node_relief_hits > 0:
            candidates.extend(
                edge_node_relief_candidates(
                    positions,
                    node_counts,
                    edge_counts,
                    edges,
                    collision_geometry,
                    current_edge_node_flags,
                    clusters,
                    args.edge_node_relief_hits,
                    args.edge_node_relief_max_group_size,
                    edge_node_relief_steps,
                )
            )
        candidates = truncate_candidates(
            candidates,
            node_counts,
            args.final_max_candidates,
            args.final_per_action_candidates,
        )
        if not candidates:
            print(f"round {round_idx+1}: no candidates", flush=True)
            break
        current_cross_flags = crossing_flags_for_pairs(positions, evaluator)
        current_carrier_counts = (
            carrier_count_map(current_cross_flags, carrier_pair_keys)
            if carrier_pair_keys is not None
            else None
        )
        current_overlap_flags = overlap_flags_for_pairs(
            current_rect_positions,
            collision_geometry.rect_widths,
            collision_geometry.rect_heights,
            args.overlap_margin,
            overlap_pairs[0],
            overlap_pairs[1],
        )
        best_candidate: Candidate | None = None
        best_candidate_metrics: Metrics | None = None
        for cand in candidates:
            moved = move_positions(positions, cand)
            if args.full_candidate_measure:
                cand_metrics = measure(
                    moved,
                    evaluator,
                    widths,
                    heights,
                    active_mask,
                    overlap_pairs,
                    args.overlap_margin,
                    args.overlap_weight,
                    args.bbox_weight,
                    args.bbox_target_b,
                    carrier_pair_keys,
                    edge_node_margin=args.edge_node_margin,
                    edge_node_weight=args.edge_node_weight,
                    collision_geometry=collision_geometry,
                )
            else:
                cand_metrics = measure_candidate_incremental(
                    positions,
                    moved,
                    cand,
                    metrics,
                    current_cross_flags,
                    evaluator,
                    widths,
                    heights,
                    active_mask,
                    overlap_pairs,
                    current_overlap_flags,
                    args.overlap_margin,
                    args.overlap_weight,
                    args.bbox_weight,
                    args.bbox_target_b,
                    carrier_pair_keys,
                    current_carrier_counts,
                    current_edge_node_flags=current_edge_node_flags,
                    edge_node_margin=args.edge_node_margin,
                    edge_node_weight=args.edge_node_weight,
                    collision_geometry=collision_geometry,
                    current_rect_positions=current_rect_positions,
                )
            if best_candidate_metrics is None or cand_metrics.score < best_candidate_metrics.score:
                best_candidate = cand
                best_candidate_metrics = cand_metrics
            if log_handle is not None:
                log_handle.write(
                    json.dumps(
                        {
                            "round": round_idx + 1,
                            "graph": {
                                "nodes": int(positions.shape[0]),
                                "edges": int(edges.shape[0]),
                                "carrierCross": bool(args.carrier_cross),
                            },
                            "base": metrics.__dict__,
                            "candidate": candidate_features(cand, positions, node_counts),
                            "action": candidate_action_record(cand, model_ids, positions),
                            "result": cand_metrics.__dict__,
                            "deltaCross": cand_metrics.cross - metrics.cross,
                            "deltaOverlaps": cand_metrics.overlaps - metrics.overlaps,
                            "deltaBboxB": cand_metrics.bbox_b - metrics.bbox_b,
                            "deltaScore": cand_metrics.score - metrics.score,
                        },
                        separators=(",", ":"),
                    )
                    + "\n"
                )
        assert best_candidate_metrics is not None
        gain = metrics.score - best_candidate_metrics.score
        if gain < args.min_gain:
            print(
                f"round {round_idx+1}: no improving move "
                f"(best gain={gain:.1f}, candidates={len(candidates)})"
                ,
                flush=True,
            )
            break
        positions = move_positions(positions, best_candidate)
        if args.cooldown_rounds > 0:
            cooldown[best_candidate.indices] = args.cooldown_rounds
            if best_candidate.other is not None:
                cooldown[best_candidate.other] = args.cooldown_rounds
        metrics = best_candidate_metrics
        accepted += 1
        if metrics.score < best_metrics.score:
            best_metrics = metrics
            best_positions = positions.copy()
        moved_count = best_candidate.indices.shape[0]
        print(
            f"round {round_idx+1:02d}: {best_candidate.kind}"
            f"[{moved_count}] dx={best_candidate.dx:+.0f} "
            f"dy={best_candidate.dy:+.0f} gain={gain:.1f} "
            f"cross={metrics.cross} overlaps={metrics.overlaps} "
            f"edgeNode={metrics.edge_node} "
            f"visualCross={metrics.visual_cross} "
            f"bbox={metrics.bbox_b:.2f}B score={metrics.score:.1f} "
            f"candidates={len(candidates)}"
            ,
            flush=True,
        )
    if log_handle is not None:
        log_handle.close()
    args.out_tsv.parent.mkdir(parents=True, exist_ok=True)
    write_positions_tsv(args.out_tsv, layout, best_positions)
    elapsed = time.time() - started
    print(
        f"done accepted={accepted} elapsed={elapsed:.1f}s "
        f"best cross={best_metrics.cross} overlaps={best_metrics.overlaps} "
        f"edgeNode={best_metrics.edge_node} "
        f"visualCross={best_metrics.visual_cross} "
        f"bbox={best_metrics.bbox_b:.2f}B score={best_metrics.score:.1f}"
        ,
        flush=True,
    )
    print(f"wrote {args.out_tsv}", flush=True)
    if args.measure_rigid:
        out_json = args.out_tsv.with_suffix(".rigid.json")
        em = run_rigid_measure(args.out_tsv, out_json, layout)
        bb = em.get("boundingBoxArea")
        bb_b = bb / 1e9 if isinstance(bb, (int, float)) else None
        bb_text = f"{bb_b:.2f}B" if bb_b is not None else "?"
        print(
            "rigid "
            f"cross={em.get('edgeCrossings')} "
            f"overlap={em.get('nodeOverlaps')} "
            f"bbox={bb_text} "
            f"json={out_json}"
            ,
            flush=True,
        )


if __name__ == "__main__":
    main()
