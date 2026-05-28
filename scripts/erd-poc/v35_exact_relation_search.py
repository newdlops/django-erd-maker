#!/usr/bin/env python3
"""Exact-render relation search for the v35 ERD layout.

This scorer follows the zoomable exact relation view:

* leaf bundle members are hidden behind one rendered bundle table,
* leaf-to-root relations are collapsed into one B carrier per bundle/root,
* all other visible relations are clipped to the rendered node/table box, and
* edge/table collisions count in the same visual-cross bucket as edge/edge
  crossings.

It deliberately stays as a Python exploration tool.  The expensive part is
kept vectorized and candidate evaluation is incremental: only visual edges and
rectangles affected by a proposed node move are remeasured.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import math
import random
import sys
import time
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

import numpy as np

SPEC = importlib.util.spec_from_file_location(
    "v34_move_search",
    Path(__file__).parent / "v34_move_search.py",
)
v34 = importlib.util.module_from_spec(SPEC)
sys.modules["v34_move_search"] = v34
assert SPEC.loader is not None
SPEC.loader.exec_module(v34)


@dataclass(frozen=True)
class Metrics:
    edge_cross: int
    edge_rect: int
    overlaps: int
    bbox_b: float
    score: float

    @property
    def visual_cross(self) -> int:
        return int(self.edge_cross) + int(self.edge_rect) + int(self.overlaps)


@dataclass
class Candidate:
    kind: str
    nodes: np.ndarray
    dx: float = 0.0
    dy: float = 0.0
    targets: np.ndarray | None = None
    priority: float = 0.0
    label: str = ""


@dataclass
class VisualEdge:
    edge_id: str
    source_rect: int
    target_rect: int
    source_nodes: np.ndarray
    target_nodes: np.ndarray
    count: int = 1
    raw_index: int = -1

    @property
    def moved_nodes(self) -> np.ndarray:
        if self.source_nodes.size == 0:
            return self.target_nodes
        if self.target_nodes.size == 0:
            return self.source_nodes
        return np.unique(np.concatenate([self.source_nodes, self.target_nodes])).astype(np.int32)


@dataclass(frozen=True)
class PortSpec:
    side: str
    fraction: float


def unit(vec: np.ndarray) -> np.ndarray | None:
    norm = float(np.linalg.norm(vec))
    if norm < 1e-9:
        return None
    return vec / norm


def unit_directions() -> list[tuple[float, float]]:
    raw = [
        (1.0, 0.0),
        (-1.0, 0.0),
        (0.0, 1.0),
        (0.0, -1.0),
        (1.0, 1.0),
        (1.0, -1.0),
        (-1.0, 1.0),
        (-1.0, -1.0),
    ]
    out: list[tuple[float, float]] = []
    for x, y in raw:
        n = math.hypot(x, y)
        out.append((x / n, y / n))
    return out


def parse_steps(text: str) -> list[float]:
    return [float(part) for part in text.split(",") if part.strip()]


def rect_port(
    center: np.ndarray,
    width: float,
    height: float,
    toward: np.ndarray,
) -> np.ndarray:
    dx = float(toward[0] - center[0])
    dy = float(toward[1] - center[1])
    if abs(dx) < 1e-9 and abs(dy) < 1e-9:
        return center.copy()
    scale = float("inf")
    if abs(dx) >= 1e-9:
        sx = (width / 2.0) / abs(dx)
        if sx > 0.0:
            scale = min(scale, sx)
    if abs(dy) >= 1e-9:
        sy = (height / 2.0) / abs(dy)
        if sy > 0.0:
            scale = min(scale, sy)
    if not math.isfinite(scale):
        scale = 0.0
    return center + np.array([dx * scale, dy * scale], dtype=np.float64)


def infer_port_spec(
    point: np.ndarray,
    center: np.ndarray,
    width: float,
    height: float,
) -> PortSpec:
    left = center[0] - width / 2.0
    right = center[0] + width / 2.0
    top = center[1] - height / 2.0
    bottom = center[1] + height / 2.0
    distances = [
        (abs(float(point[0] - left)), "left"),
        (abs(float(point[0] - right)), "right"),
        (abs(float(point[1] - top)), "top"),
        (abs(float(point[1] - bottom)), "bottom"),
    ]
    _dist, side = min(distances, key=lambda item: item[0])
    if side in ("left", "right"):
        fraction = 0.5 if height <= 1e-9 else float((point[1] - top) / height)
    else:
        fraction = 0.5 if width <= 1e-9 else float((point[0] - left) / width)
    return PortSpec(side, max(0.0, min(1.0, fraction)))


def port_from_spec(
    center: np.ndarray,
    width: float,
    height: float,
    spec: PortSpec,
) -> np.ndarray:
    if spec.side == "left":
        return np.array(
            [center[0] - width / 2.0, center[1] + (spec.fraction - 0.5) * height],
            dtype=np.float64,
        )
    if spec.side == "right":
        return np.array(
            [center[0] + width / 2.0, center[1] + (spec.fraction - 0.5) * height],
            dtype=np.float64,
        )
    if spec.side == "top":
        return np.array(
            [center[0] + (spec.fraction - 0.5) * width, center[1] - height / 2.0],
            dtype=np.float64,
        )
    return np.array(
        [center[0] + (spec.fraction - 0.5) * width, center[1] + height / 2.0],
        dtype=np.float64,
    )


def pair_cross_flags(
    segments: np.ndarray,
    pair_i: np.ndarray,
    pair_j: np.ndarray,
) -> np.ndarray:
    if pair_i.size == 0:
        return np.zeros(0, dtype=bool)
    a = segments[pair_i, 0]
    b = segments[pair_i, 1]
    c = segments[pair_j, 0]
    d = segments[pair_j, 1]

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


def intersection_points(
    segments: np.ndarray,
    pair_i: np.ndarray,
    pair_j: np.ndarray,
) -> np.ndarray:
    if pair_i.size == 0:
        return np.zeros((0, 2), dtype=np.float64)
    a = segments[pair_i, 0]
    b = segments[pair_i, 1]
    c = segments[pair_j, 0]
    d = segments[pair_j, 1]
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


def edge_rect_flags(
    segments: np.ndarray,
    rect_positions: np.ndarray,
    rect_widths: np.ndarray,
    rect_heights: np.ndarray,
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
    a = segments[ee, 0]
    b = segments[ee, 1]
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


class ExactRelationEvaluator:
    def __init__(
        self,
        layout: dict,
        *,
        overlap_margin: float,
        edge_rect_margin: float,
        overlap_weight: float,
        bbox_weight: float,
        bbox_target_b: float,
    ) -> None:
        self.layout = layout
        self.nodes = layout["nodes"]
        self.n_nodes = len(self.nodes)
        self.model_ids = [str(nd.get("modelId") or "") for nd in self.nodes]
        self.id_to_idx = {model_id: idx for idx, model_id in enumerate(self.model_ids)}
        self.raw_edges = v34.graph_edges(layout)
        self.geometry = v34.build_render_collision_geometry(layout, self.raw_edges)
        self.node_to_rect = {
            int(node_idx): int(rect_idx)
            for rect_idx, node_idx in enumerate(self.geometry.rect_node)
            if int(node_idx) >= 0
        }
        self.overlap_margin = float(overlap_margin)
        self.edge_rect_margin = float(edge_rect_margin)
        self.overlap_weight = float(overlap_weight)
        self.bbox_weight = float(bbox_weight)
        self.bbox_target_b = float(bbox_target_b)
        self.bundle_rect_by_index: dict[int, int] = {}
        self.bundle_leaves_by_index: dict[int, np.ndarray] = {}
        self._build_bundle_maps()
        self.visual_edges = self._build_visual_edges()
        self.edge_count = len(self.visual_edges)
        self.edge_nodes = tuple(edge.moved_nodes for edge in self.visual_edges)
        self.edge_source_rect = np.array(
            [edge.source_rect for edge in self.visual_edges],
            dtype=np.int32,
        )
        self.edge_target_rect = np.array(
            [edge.target_rect for edge in self.visual_edges],
            dtype=np.int32,
        )
        self.edge_pair_i, self.edge_pair_j = self._build_edge_pairs()
        self.edge_rect_pair_edge, self.edge_rect_pair_rect = self._build_edge_rect_pairs()
        self.overlap_pair_i, self.overlap_pair_j = self.geometry.overlap_pairs
        self.node_to_edges = self._build_node_to_edges()

    def _build_bundle_maps(self) -> None:
        base_rect = int(np.sum(self.geometry.rect_node >= 0))
        rect_offset = 0
        for bundle_idx, bundle in enumerate(
            (self.layout.get("engineMetadata") or {}).get("leafBundles") or []
        ):
            leaves = [
                self.id_to_idx[str(mid)]
                for mid in (bundle.get("leafModelIds") or [])
                if str(mid) in self.id_to_idx
            ]
            if not leaves:
                continue
            rect_idx = base_rect + rect_offset
            rect_offset += 1
            if rect_idx >= self.geometry.rect_node.shape[0]:
                continue
            arr = np.array(leaves, dtype=np.int32)
            self.bundle_rect_by_index[bundle_idx] = rect_idx
            self.bundle_leaves_by_index[bundle_idx] = arr

    def _build_visual_edges(self) -> list[VisualEdge]:
        leaf_to_bundle: dict[str, int] = {}
        bundle_roots: list[set[str]] = []
        leaf_bundles = (self.layout.get("engineMetadata") or {}).get("leafBundles") or []
        for bundle_idx, bundle in enumerate(leaf_bundles):
            roots = {str(root) for root in (bundle.get("sharedRootModelIds") or [])}
            parent = str(bundle.get("parentModelId") or "")
            if parent:
                roots.add(parent)
            bundle_roots.append(roots)
            for leaf in bundle.get("leafModelIds") or []:
                leaf_to_bundle[str(leaf)] = bundle_idx

        raw: list[VisualEdge] = []
        bundle_groups: dict[tuple[int, int], dict[str, object]] = {}
        for raw_index, edge in enumerate(self.layout.get("routedEdges") or []):
            source = str(edge.get("sourceModelId") or "")
            target = str(edge.get("targetModelId") or "")
            source_idx = self.id_to_idx.get(source)
            target_idx = self.id_to_idx.get(target)
            if source_idx is None or target_idx is None or source_idx == target_idx:
                continue

            source_bundle = leaf_to_bundle.get(source)
            if source_bundle is not None and target in bundle_roots[source_bundle]:
                root_idx = int(target_idx)
                key = (source_bundle, root_idx)
                group = bundle_groups.setdefault(
                    key,
                    {
                        "count": 0,
                        "sample_source": source,
                        "sample_target": target,
                        "raw_index": raw_index,
                    },
                )
                group["count"] = int(group["count"]) + 1
                continue

            target_bundle = leaf_to_bundle.get(target)
            if target_bundle is not None and source in bundle_roots[target_bundle]:
                root_idx = int(source_idx)
                key = (target_bundle, root_idx)
                group = bundle_groups.setdefault(
                    key,
                    {
                        "count": 0,
                        "sample_source": source,
                        "sample_target": target,
                        "raw_index": raw_index,
                    },
                )
                group["count"] = int(group["count"]) + 1
                continue

            if source_bundle is not None or target_bundle is not None:
                continue
            source_rect = self.node_to_rect.get(int(source_idx))
            target_rect = self.node_to_rect.get(int(target_idx))
            if source_rect is None or target_rect is None:
                continue
            raw.append(
                VisualEdge(
                    edge_id=str(edge.get("edgeId") or f"raw:{raw_index}"),
                    source_rect=int(source_rect),
                    target_rect=int(target_rect),
                    source_nodes=np.array([int(source_idx)], dtype=np.int32),
                    target_nodes=np.array([int(target_idx)], dtype=np.int32),
                    raw_index=raw_index,
                )
            )

        bundled: list[VisualEdge] = []
        for (bundle_idx, root_idx), group in bundle_groups.items():
            bundle_rect = self.bundle_rect_by_index.get(bundle_idx)
            root_rect = self.node_to_rect.get(root_idx)
            leaves = self.bundle_leaves_by_index.get(bundle_idx)
            if bundle_rect is None or root_rect is None or leaves is None or leaves.size == 0:
                continue
            bundled.append(
                VisualEdge(
                    edge_id=f"B{bundle_idx}|{self.model_ids[root_idx]}",
                    source_rect=int(bundle_rect),
                    target_rect=int(root_rect),
                    source_nodes=leaves,
                    target_nodes=np.array([int(root_idx)], dtype=np.int32),
                    count=int(group["count"]),
                    raw_index=int(group["raw_index"]),
                )
            )
        return bundled + raw

    def _build_edge_pairs(self) -> tuple[np.ndarray, np.ndarray]:
        if self.edge_count < 2:
            return np.zeros(0, dtype=np.int32), np.zeros(0, dtype=np.int32)
        i, j = np.triu_indices(self.edge_count, k=1)
        sr = self.edge_source_rect
        tr = self.edge_target_rect
        share = (
            (sr[i] == sr[j])
            | (sr[i] == tr[j])
            | (tr[i] == sr[j])
            | (tr[i] == tr[j])
        )
        return i[~share].astype(np.int32), j[~share].astype(np.int32)

    def _build_edge_rect_pairs(self) -> tuple[np.ndarray, np.ndarray]:
        rect_count = int(self.geometry.rect_node.shape[0])
        if self.edge_count == 0 or rect_count == 0:
            return np.zeros(0, dtype=np.int32), np.zeros(0, dtype=np.int32)
        pair_edge: list[int] = []
        pair_rect: list[int] = []
        for edge_idx, edge in enumerate(self.visual_edges):
            for rect_idx in range(rect_count):
                if rect_idx == edge.source_rect or rect_idx == edge.target_rect:
                    continue
                pair_edge.append(edge_idx)
                pair_rect.append(rect_idx)
        return np.array(pair_edge, dtype=np.int32), np.array(pair_rect, dtype=np.int32)

    def _build_node_to_edges(self) -> tuple[np.ndarray, ...]:
        out: list[list[int]] = [[] for _ in range(self.n_nodes)]
        for edge_idx, edge in enumerate(self.visual_edges):
            for node in edge.moved_nodes.tolist():
                if 0 <= int(node) < self.n_nodes:
                    out[int(node)].append(edge_idx)
        return tuple(np.array(sorted(set(xs)), dtype=np.int32) for xs in out)

    def rect_positions(self, positions: np.ndarray) -> np.ndarray:
        return v34.collision_positions(positions, self.geometry)

    def segments(
        self,
        rect_positions: np.ndarray,
        edge_indices: np.ndarray | None = None,
    ) -> np.ndarray:
        if edge_indices is None:
            edge_indices = np.arange(self.edge_count, dtype=np.int32)
        segs = np.zeros((edge_indices.size, 2, 2), dtype=np.float64)
        widths = self.geometry.rect_widths
        heights = self.geometry.rect_heights
        for out_idx, edge_idx_raw in enumerate(edge_indices):
            edge_idx = int(edge_idx_raw)
            s_rect = int(self.edge_source_rect[edge_idx])
            t_rect = int(self.edge_target_rect[edge_idx])
            s_center = rect_positions[s_rect]
            t_center = rect_positions[t_rect]
            segs[out_idx, 0] = rect_port(
                s_center,
                float(widths[s_rect]),
                float(heights[s_rect]),
                t_center,
            )
            segs[out_idx, 1] = rect_port(
                t_center,
                float(widths[t_rect]),
                float(heights[t_rect]),
                s_center,
            )
        return segs

    def score(self, edge_cross: int, edge_rect: int, overlaps: int, bbox_b: float) -> float:
        bbox_penalty = max(0.0, float(bbox_b) - self.bbox_target_b)
        return (
            float(edge_cross)
            + float(edge_rect)
            + self.overlap_weight * float(overlaps)
            + self.bbox_weight * bbox_penalty
        )

    def measure(self, positions: np.ndarray) -> tuple[Metrics, dict[str, np.ndarray]]:
        rect_positions = self.rect_positions(positions)
        segments = self.segments(rect_positions)
        cross_flags = pair_cross_flags(segments, self.edge_pair_i, self.edge_pair_j)
        edge_rect = edge_rect_flags(
            segments,
            rect_positions,
            self.geometry.rect_widths,
            self.geometry.rect_heights,
            self.edge_rect_pair_edge,
            self.edge_rect_pair_rect,
            self.edge_rect_margin,
        )
        overlap_flags = v34.overlap_flags_for_pairs(
            rect_positions,
            self.geometry.rect_widths,
            self.geometry.rect_heights,
            self.overlap_margin,
            self.overlap_pair_i,
            self.overlap_pair_j,
        )
        bbox_b = v34.bbox_rect_b(
            rect_positions,
            self.geometry.rect_widths,
            self.geometry.rect_heights,
        )
        metrics = Metrics(
            edge_cross=int(cross_flags.sum()),
            edge_rect=int(edge_rect.sum()),
            overlaps=int(overlap_flags.sum()),
            bbox_b=float(bbox_b),
            score=self.score(
                int(cross_flags.sum()),
                int(edge_rect.sum()),
                int(overlap_flags.sum()),
                float(bbox_b),
            ),
        )
        state = {
            "rect_positions": rect_positions,
            "segments": segments,
            "cross_flags": cross_flags,
            "edge_rect_flags": edge_rect,
            "overlap_flags": overlap_flags,
        }
        return metrics, state

    def impacted_masks(self, moved_nodes: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        impacted_rects = v34.impacted_collision_rect_mask(moved_nodes, self.geometry)
        impacted_edges = np.zeros(self.edge_count, dtype=bool)
        for node_raw in np.unique(moved_nodes.astype(np.int32, copy=False)):
            node = int(node_raw)
            if 0 <= node < len(self.node_to_edges):
                impacted_edges[self.node_to_edges[node]] = True
        return impacted_edges, impacted_rects

    def measure_candidate(
        self,
        base_positions: np.ndarray,
        moved_positions: np.ndarray,
        moved_nodes: np.ndarray,
        current: Metrics,
        state: dict[str, np.ndarray],
    ) -> Metrics:
        impacted_edges, impacted_rects = self.impacted_masks(moved_nodes)
        base_rect_positions = state["rect_positions"]
        moved_rect_positions = v34.moved_collision_positions(
            base_rect_positions,
            moved_positions,
            self.geometry,
            impacted_rects,
        )
        moved_segments = state["segments"].copy()
        edge_indices = np.flatnonzero(impacted_edges).astype(np.int32)
        if edge_indices.size:
            moved_segments[edge_indices] = self.segments(moved_rect_positions, edge_indices)

        cross_pair_mask = impacted_edges[self.edge_pair_i] | impacted_edges[self.edge_pair_j]
        old_cross_local = int(state["cross_flags"][cross_pair_mask].sum())
        new_cross_local = int(
            pair_cross_flags(
                moved_segments,
                self.edge_pair_i[cross_pair_mask],
                self.edge_pair_j[cross_pair_mask],
            ).sum()
        )
        edge_cross = current.edge_cross - old_cross_local + new_cross_local

        edge_rect_pair_mask = (
            impacted_edges[self.edge_rect_pair_edge]
            | impacted_rects[self.edge_rect_pair_rect]
        )
        old_edge_rect_local = int(state["edge_rect_flags"][edge_rect_pair_mask].sum())
        new_edge_rect_local = int(
            edge_rect_flags(
                moved_segments,
                moved_rect_positions,
                self.geometry.rect_widths,
                self.geometry.rect_heights,
                self.edge_rect_pair_edge,
                self.edge_rect_pair_rect,
                self.edge_rect_margin,
                edge_rect_pair_mask,
            ).sum()
        )
        edge_rect = current.edge_rect - old_edge_rect_local + new_edge_rect_local

        overlap_mask = impacted_rects[self.overlap_pair_i] | impacted_rects[self.overlap_pair_j]
        old_overlap_local = int(state["overlap_flags"][overlap_mask].sum())
        new_overlap_local = int(
            v34.overlap_flags_for_pairs(
                moved_rect_positions,
                self.geometry.rect_widths,
                self.geometry.rect_heights,
                self.overlap_margin,
                self.overlap_pair_i,
                self.overlap_pair_j,
                overlap_mask,
            ).sum()
        )
        overlaps = current.overlaps - old_overlap_local + new_overlap_local
        bbox_b = v34.bbox_rect_b(
            moved_rect_positions,
            self.geometry.rect_widths,
            self.geometry.rect_heights,
        )
        return Metrics(
            edge_cross=int(edge_cross),
            edge_rect=int(edge_rect),
            overlaps=int(overlaps),
            bbox_b=float(bbox_b),
            score=self.score(edge_cross, edge_rect, overlaps, bbox_b),
        )

    def crossing_incidents(
        self,
        state: dict[str, np.ndarray],
    ) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
        cross_flags = state["cross_flags"]
        pair_i = self.edge_pair_i[cross_flags]
        pair_j = self.edge_pair_j[cross_flags]
        edge_counts = np.zeros(self.edge_count, dtype=np.int32)
        if pair_i.size:
            np.add.at(edge_counts, pair_i, 1)
            np.add.at(edge_counts, pair_j, 1)
        node_counts = np.zeros(self.n_nodes, dtype=np.int32)
        for edge_idx in np.flatnonzero(edge_counts):
            moved = self.edge_nodes[int(edge_idx)]
            if moved.size:
                np.add.at(node_counts, moved, int(edge_counts[int(edge_idx)]))
        return edge_counts, node_counts, pair_i, pair_j


class FixedPortRelationEvaluator(ExactRelationEvaluator):
    """Evaluate layout moves using port side/fraction from a rendered port JSON."""

    def __init__(
        self,
        layout: dict,
        initial_positions: np.ndarray,
        port_data: dict,
        *,
        overlap_margin: float,
        edge_rect_margin: float,
        overlap_weight: float,
        bbox_weight: float,
        bbox_target_b: float,
    ) -> None:
        super().__init__(
            layout,
            overlap_margin=overlap_margin,
            edge_rect_margin=edge_rect_margin,
            overlap_weight=overlap_weight,
            bbox_weight=bbox_weight,
            bbox_target_b=bbox_target_b,
        )
        rendered_edges = {
            str(edge.get("id") or ""): edge
            for edge in list(port_data.get("edges") or [])
        }
        rect_positions = self.rect_positions(initial_positions)
        self.source_port_specs: list[PortSpec] = []
        self.target_port_specs: list[PortSpec] = []
        matched = 0
        for edge_idx, edge in enumerate(self.visual_edges):
            s_rect = int(self.edge_source_rect[edge_idx])
            t_rect = int(self.edge_target_rect[edge_idx])
            s_center = rect_positions[s_rect]
            t_center = rect_positions[t_rect]
            s_width = float(self.geometry.rect_widths[s_rect])
            s_height = float(self.geometry.rect_heights[s_rect])
            t_width = float(self.geometry.rect_widths[t_rect])
            t_height = float(self.geometry.rect_heights[t_rect])
            row = rendered_edges.get(edge.edge_id)
            if row is not None and len(row.get("points") or []) >= 2:
                points = row["points"]
                s_point = np.array(
                    [float(points[0].get("x", 0.0)), float(points[0].get("y", 0.0))],
                    dtype=np.float64,
                )
                t_point = np.array(
                    [float(points[1].get("x", 0.0)), float(points[1].get("y", 0.0))],
                    dtype=np.float64,
                )
                matched += 1
            else:
                s_point = rect_port(s_center, s_width, s_height, t_center)
                t_point = rect_port(t_center, t_width, t_height, s_center)
            self.source_port_specs.append(infer_port_spec(s_point, s_center, s_width, s_height))
            self.target_port_specs.append(infer_port_spec(t_point, t_center, t_width, t_height))
        print(f"fixed-port evaluator initialized matched={matched}", flush=True)

    def segments(
        self,
        rect_positions: np.ndarray,
        edge_indices: np.ndarray | None = None,
    ) -> np.ndarray:
        if edge_indices is None:
            edge_indices = np.arange(self.edge_count, dtype=np.int32)
        segs = np.zeros((edge_indices.size, 2, 2), dtype=np.float64)
        widths = self.geometry.rect_widths
        heights = self.geometry.rect_heights
        for out_idx, edge_idx_raw in enumerate(edge_indices):
            edge_idx = int(edge_idx_raw)
            s_rect = int(self.edge_source_rect[edge_idx])
            t_rect = int(self.edge_target_rect[edge_idx])
            segs[out_idx, 0] = port_from_spec(
                rect_positions[s_rect],
                float(widths[s_rect]),
                float(heights[s_rect]),
                self.source_port_specs[edge_idx],
            )
            segs[out_idx, 1] = port_from_spec(
                rect_positions[t_rect],
                float(widths[t_rect]),
                float(heights[t_rect]),
                self.target_port_specs[edge_idx],
            )
        return segs


def move_positions(positions: np.ndarray, cand: Candidate) -> np.ndarray:
    out = positions.copy()
    if cand.targets is not None:
        out[cand.nodes] = cand.targets
    else:
        out[cand.nodes, 0] += cand.dx
        out[cand.nodes, 1] += cand.dy
    return out


def add_unique_direction(dirs: list[np.ndarray], direction: np.ndarray | None) -> None:
    if direction is None:
        return
    for existing in dirs:
        if abs(float(np.dot(existing, direction))) > 0.985:
            return
    dirs.append(direction)


def candidate_key(cand: Candidate) -> tuple:
    nodes = tuple(int(x) for x in cand.nodes.tolist())
    if cand.targets is not None:
        rounded = tuple(tuple(float(round(v, 1)) for v in row) for row in cand.targets.tolist())
        return (cand.kind, nodes, rounded)
    return (cand.kind, nodes, round(cand.dx, 1), round(cand.dy, 1))


def dedupe_candidates(candidates: list[Candidate]) -> list[Candidate]:
    best: dict[tuple, Candidate] = {}
    for cand in candidates:
        key = candidate_key(cand)
        old = best.get(key)
        if old is None or cand.priority > old.priority:
            best[key] = cand
    return list(best.values())


def group_for_endpoint(
    evaluator: ExactRelationEvaluator,
    edge: VisualEdge,
    endpoint: str,
) -> np.ndarray:
    return edge.source_nodes if endpoint == "source" else edge.target_nodes


def visual_crossing_candidates(
    evaluator: ExactRelationEvaluator,
    positions: np.ndarray,
    state: dict[str, np.ndarray],
    edge_counts: np.ndarray,
    max_pairs: int,
    max_hot_edges: int,
    steps: list[float],
    orbit_radii: list[float],
) -> list[Candidate]:
    candidates: list[Candidate] = []
    segments = state["segments"]
    cross_flags = state["cross_flags"]
    pair_i = evaluator.edge_pair_i[cross_flags]
    pair_j = evaluator.edge_pair_j[cross_flags]
    if pair_i.size == 0:
        return candidates

    priority = edge_counts[pair_i] + edge_counts[pair_j]
    pair_order = np.argsort(-priority)[:max_pairs]
    for rank, pair_pos in enumerate(pair_order):
        e1 = int(pair_i[int(pair_pos)])
        e2 = int(pair_j[int(pair_pos)])
        seg1 = segments[e1]
        seg2 = segments[e2]
        v1 = seg1[1] - seg1[0]
        v2 = seg2[1] - seg2[0]
        u1 = unit(v1)
        u2 = unit(v2)
        if u1 is None or u2 is None:
            continue
        n1 = np.array([-u1[1], u1[0]], dtype=np.float64)
        n2 = np.array([-u2[1], u2[0]], dtype=np.float64)
        pair_priority = float(priority[int(pair_pos)])
        edges = [evaluator.visual_edges[e1], evaluator.visual_edges[e2]]
        normals = [n2, n1]
        for edge_idx, edge, normal in ((e1, edges[0], n2), (e2, edges[1], n1)):
            whole = edge.moved_nodes
            if whole.size == 0:
                continue
            for step in steps:
                for direction in (normal, -normal):
                    candidates.append(
                        Candidate(
                            "visual_edge_translate",
                            whole,
                            dx=float(direction[0] * step),
                            dy=float(direction[1] * step),
                            priority=pair_priority + float(edge_counts[edge_idx]),
                            label=f"pair:{rank}:edge:{edge_idx}",
                        )
                    )
                    for endpoint in ("source", "target"):
                        nodes = group_for_endpoint(evaluator, edge, endpoint)
                        if nodes.size == 0:
                            continue
                        candidates.append(
                            Candidate(
                                "visual_endpoint_translate",
                                nodes,
                                dx=float(direction[0] * step),
                                dy=float(direction[1] * step),
                                priority=pair_priority + float(edge_counts[edge_idx]) * 1.5,
                                label=f"pair:{rank}:endpoint:{edge_idx}:{endpoint}",
                            )
                        )

        spread_dirs: list[np.ndarray] = []
        add_unique_direction(spread_dirs, unit(n1 + n2))
        add_unique_direction(spread_dirs, unit(n1 - n2))
        add_unique_direction(spread_dirs, n1)
        add_unique_direction(spread_dirs, n2)
        e1_nodes = evaluator.visual_edges[e1].moved_nodes
        e2_nodes = evaluator.visual_edges[e2].moved_nodes
        if e1_nodes.size and e2_nodes.size:
            overlap = np.intersect1d(e1_nodes, e2_nodes, assume_unique=False)
            if overlap.size == 0:
                joined = np.concatenate([e1_nodes, e2_nodes]).astype(np.int32)
                split = int(e1_nodes.size)
                for step in steps:
                    for direction in spread_dirs:
                        targets = positions[joined].copy()
                        targets[:split] += direction.reshape(1, 2) * step
                        targets[split:] -= direction.reshape(1, 2) * step
                        candidates.append(
                            Candidate(
                                "visual_pair_spread",
                                joined,
                                targets=targets,
                                priority=pair_priority * 1.2,
                                label=f"pair:{rank}:{e1}:{e2}",
                            )
                        )

    ranked_edges = np.argsort(-edge_counts)[:max_hot_edges]
    for edge_rank, edge_idx_raw in enumerate(ranked_edges):
        edge_idx = int(edge_idx_raw)
        if edge_counts[edge_idx] <= 0:
            continue
        edge = evaluator.visual_edges[edge_idx]
        seg = segments[edge_idx]
        vec = seg[1] - seg[0]
        along = unit(vec)
        if along is None:
            continue
        normal = np.array([-along[1], along[0]], dtype=np.float64)
        dirs: list[np.ndarray] = []
        add_unique_direction(dirs, normal)
        add_unique_direction(dirs, -normal)

        touched = (pair_i == edge_idx) | (pair_j == edge_idx)
        partner_edges = np.where(pair_i[touched] == edge_idx, pair_j[touched], pair_i[touched])
        if partner_edges.size:
            edge_mid = seg.mean(axis=0)
            partner_mid = segments[partner_edges].mean(axis=1)
            add_unique_direction(dirs, unit(edge_mid - partner_mid.mean(axis=0)))
            partner_vec = segments[partner_edges, 1] - segments[partner_edges, 0]
            norms = np.linalg.norm(partner_vec, axis=1)
            good = norms > 1e-9
            if np.any(good):
                partner_unit = partner_vec[good] / norms[good].reshape(-1, 1)
                partner_normal = np.stack([-partner_unit[:, 1], partner_unit[:, 0]], axis=1)
                rel = edge_mid.reshape(1, 2) - partner_mid[good]
                sign = np.sign(np.sum(rel * partner_normal, axis=1))
                sign[sign == 0] = 1.0
                weights = np.maximum(1.0, edge_counts[partner_edges[good]].astype(np.float64))
                add_unique_direction(
                    dirs,
                    unit((partner_normal * sign.reshape(-1, 1) * weights.reshape(-1, 1)).sum(axis=0)),
                )

        for step in steps:
            for direction in dirs[:5]:
                whole = edge.moved_nodes
                if whole.size:
                    candidates.append(
                        Candidate(
                            "visual_fan_edge_translate",
                            whole,
                            dx=float(direction[0] * step),
                            dy=float(direction[1] * step),
                            priority=float(edge_counts[edge_idx]) * 4.0,
                            label=f"fan:{edge_rank}:edge:{edge_idx}",
                        )
                    )
                for endpoint in ("source", "target"):
                    nodes = group_for_endpoint(evaluator, edge, endpoint)
                    if nodes.size == 0:
                        continue
                    candidates.append(
                        Candidate(
                            "visual_fan_endpoint_translate",
                            nodes,
                            dx=float(direction[0] * step),
                            dy=float(direction[1] * step),
                            priority=float(edge_counts[edge_idx]) * 5.0,
                            label=f"fan:{edge_rank}:endpoint:{edge_idx}:{endpoint}",
                        )
                    )

        # Bundle endpoints are rendered as one free degree-1 box.  Orbiting the
        # bundle around its root tests that freedom without introducing
        # polyline detours.
        for endpoint, anchor_endpoint in (("source", "target"), ("target", "source")):
            nodes = group_for_endpoint(evaluator, edge, endpoint)
            anchor_nodes = group_for_endpoint(evaluator, edge, anchor_endpoint)
            if nodes.size == 0 or anchor_nodes.size == 0 or nodes.size < 2:
                continue
            anchor = positions[anchor_nodes].mean(axis=0)
            centroid = positions[nodes].mean(axis=0)
            current_radius = max(200.0, float(np.linalg.norm(centroid - anchor)))
            for scale in orbit_radii:
                radius = current_radius * scale
                for dx, dy in unit_directions():
                    target_centroid = anchor + np.array([dx * radius, dy * radius])
                    targets = positions[nodes] + (target_centroid - centroid).reshape(1, 2)
                    candidates.append(
                        Candidate(
                            "visual_bundle_orbit",
                            nodes,
                            targets=targets,
                            priority=float(edge_counts[edge_idx]) * 8.0 + float(nodes.size),
                            label=f"bundle-orbit:{edge_rank}:edge:{edge_idx}:{endpoint}",
                        )
                    )
    return candidates


def edge_rect_relief_candidates(
    evaluator: ExactRelationEvaluator,
    positions: np.ndarray,
    state: dict[str, np.ndarray],
    edge_counts: np.ndarray,
    node_counts: np.ndarray,
    max_hits: int,
    steps: list[float],
    max_neighborhood_nodes: int,
) -> list[Candidate]:
    flags = state["edge_rect_flags"]
    hit_idx = np.flatnonzero(flags)
    if hit_idx.size == 0 or max_hits <= 0:
        return []
    segments = state["segments"]
    rect_positions = state["rect_positions"]
    pair_edge = evaluator.edge_rect_pair_edge
    pair_rect = evaluator.edge_rect_pair_rect
    candidates: list[Candidate] = []

    ranked: list[tuple[float, int, int, int]] = []
    for hit in hit_idx:
        edge_idx = int(pair_edge[int(hit)])
        rect_idx = int(pair_rect[int(hit)])
        rect_effect = evaluator.geometry.rect_effect_nodes_by_rect[rect_idx]
        rect_score = 0.0
        if rect_effect:
            arr = np.array(rect_effect, dtype=np.int32)
            rect_score = float(node_counts[arr].sum()) / math.sqrt(max(1, arr.size))
        priority = float(edge_counts[edge_idx]) * 5.0 + rect_score
        ranked.append((priority, int(hit), edge_idx, rect_idx))
    ranked.sort(reverse=True, key=lambda item: item[0])

    for rank, (priority, _hit, edge_idx, rect_idx) in enumerate(ranked[:max_hits]):
        seg = segments[edge_idx]
        a = seg[0]
        b = seg[1]
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
        add_unique_direction(dirs, away)
        add_unique_direction(dirs, normal)
        add_unique_direction(dirs, -normal)

        rect_effect = tuple(int(x) for x in evaluator.geometry.rect_effect_nodes_by_rect[rect_idx])
        if rect_effect:
            blocker_nodes = np.array(rect_effect, dtype=np.int32)
            neighborhood_nodes = blocker_nodes
            if max_neighborhood_nodes > int(blocker_nodes.size):
                used = {int(x) for x in blocker_nodes.tolist()}
                incident_rows: list[tuple[float, np.ndarray]] = []
                for node in list(used):
                    if 0 <= node < len(evaluator.node_to_edges):
                        for incident_edge_raw in evaluator.node_to_edges[node]:
                            incident_edge = int(incident_edge_raw)
                            moved = evaluator.edge_nodes[incident_edge]
                            if moved.size == 0 or moved.size > max_neighborhood_nodes:
                                continue
                            score = (
                                float(edge_counts[incident_edge]) * 10.0
                                + float(node_counts[moved].sum()) / math.sqrt(max(1, int(moved.size)))
                            )
                            incident_rows.append((score, moved))
                incident_rows.sort(reverse=True, key=lambda item: item[0])
                expanded: list[int] = sorted(used)
                for _score, moved in incident_rows:
                    additions = [int(x) for x in moved.tolist() if int(x) not in used]
                    if not additions:
                        continue
                    if len(used) + len(additions) > max_neighborhood_nodes:
                        continue
                    used.update(additions)
                    expanded.extend(additions)
                neighborhood_nodes = np.array(sorted(set(expanded)), dtype=np.int32)
            rect_w = float(evaluator.geometry.rect_widths[rect_idx])
            rect_h = float(evaluator.geometry.rect_heights[rect_idx])
            precise_dirs: list[np.ndarray] = []
            add_unique_direction(precise_dirs, away)
            signed_normal = float(np.dot(center - closest, normal))
            add_unique_direction(precise_dirs, normal if signed_normal >= 0.0 else -normal)
            add_unique_direction(precise_dirs, -normal if signed_normal >= 0.0 else normal)
            for direction in precise_dirs[:3]:
                half_extent = (
                    abs(float(direction[0])) * rect_w
                    + abs(float(direction[1])) * rect_h
                ) / 2.0
                current_clearance = float(np.dot(center - closest, direction))
                needed = max(0.0, half_extent + evaluator.edge_rect_margin + 28.0 - current_clearance)
                if needed <= 1e-6:
                    continue
                for pad in (0.0, 60.0, 140.0, 260.0):
                    delta = direction * float(needed + pad)
                    candidates.append(
                        Candidate(
                            "visual_edge_rect_blocker_precise_clear",
                            blocker_nodes,
                            dx=float(delta[0]),
                            dy=float(delta[1]),
                            priority=priority * 2.2 + float(needed),
                            label=f"edge-rect-clear:{rank}:blocker:{edge_idx}:{rect_idx}",
                        )
                    )
                    if neighborhood_nodes.size > blocker_nodes.size:
                        targets = positions[neighborhood_nodes] + delta.reshape(1, 2)
                        candidates.append(
                            Candidate(
                                "visual_edge_rect_neighborhood_precise_clear",
                                neighborhood_nodes,
                                targets=targets,
                                priority=priority * 1.9 + float(neighborhood_nodes.size) * 0.4,
                                label=f"edge-rect-neighborhood-clear:{rank}:blocker:{edge_idx}:{rect_idx}:n={neighborhood_nodes.size}",
                            )
                        )
            for step in steps:
                for direction in dirs[:3]:
                    candidates.append(
                        Candidate(
                            "visual_edge_rect_blocker_translate",
                            blocker_nodes,
                            dx=float(direction[0] * step),
                            dy=float(direction[1] * step),
                            priority=priority * 1.4,
                            label=f"edge-rect:{rank}:blocker:{edge_idx}:{rect_idx}",
                        )
                    )

        edge = evaluator.visual_edges[edge_idx]
        for step in steps:
            for direction in dirs[:3]:
                for endpoint in ("source", "target"):
                    nodes = group_for_endpoint(evaluator, edge, endpoint)
                    if nodes.size == 0:
                        continue
                    candidates.append(
                        Candidate(
                            "visual_edge_rect_endpoint_translate",
                            nodes,
                            dx=float(-direction[0] * step),
                            dy=float(-direction[1] * step),
                            priority=priority,
                            label=f"edge-rect:{rank}:endpoint:{edge_idx}:{endpoint}",
                        )
                    )
                whole = edge.moved_nodes
                if whole.size:
                    candidates.append(
                        Candidate(
                            "visual_edge_rect_edge_translate",
                            whole,
                            dx=float(-direction[0] * step),
                            dy=float(-direction[1] * step),
                            priority=priority * 0.8,
                            label=f"edge-rect:{rank}:edge:{edge_idx}",
                        )
                    )
    return candidates


def edge_rect_corridor_clear_candidates(
    evaluator: ExactRelationEvaluator,
    positions: np.ndarray,
    state: dict[str, np.ndarray],
    edge_counts: np.ndarray,
    node_counts: np.ndarray,
    *,
    max_edges: int,
    max_blockers_per_edge: int,
    pads: list[float],
) -> list[Candidate]:
    """Move multiple blocker boxes out of the same edge corridor at once."""
    if max_edges <= 0 or max_blockers_per_edge <= 0 or not pads:
        return []
    flags = state["edge_rect_flags"]
    hit_idx = np.flatnonzero(flags)
    if hit_idx.size == 0:
        return []

    pair_edge = evaluator.edge_rect_pair_edge
    pair_rect = evaluator.edge_rect_pair_rect
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

    segments = state["segments"]
    rect_positions = state["rect_positions"]
    candidates: list[Candidate] = []
    for edge_rank, (edge_priority, edge_idx, rects) in enumerate(ranked_edges):
        seg = segments[edge_idx]
        a = seg[0]
        b = seg[1]
        edge_vec = b - a
        edge_unit = unit(edge_vec)
        if edge_unit is None:
            continue
        normal = np.array([-edge_unit[1], edge_unit[0]], dtype=np.float64)
        denom = float(np.dot(edge_vec, edge_vec))
        blocker_rows: list[tuple[float, int, np.ndarray, np.ndarray, float, float]] = []
        for rect_idx in rects:
            rect_effect = tuple(int(x) for x in evaluator.geometry.rect_effect_nodes_by_rect[rect_idx])
            if not rect_effect:
                continue
            nodes = np.array(rect_effect, dtype=np.int32)
            center = rect_positions[rect_idx]
            t = 0.0 if denom < 1e-9 else float(np.dot(center - a, edge_vec) / denom)
            t = max(0.0, min(1.0, t))
            closest = a + edge_vec * t
            rect_w = float(evaluator.geometry.rect_widths[rect_idx])
            rect_h = float(evaluator.geometry.rect_heights[rect_idx])
            half_extent = (
                abs(float(normal[0])) * rect_w
                + abs(float(normal[1])) * rect_h
            ) / 2.0
            signed = float(np.dot(center - closest, normal))
            blocker_score = (
                float(node_counts[nodes].sum()) / math.sqrt(max(1, int(nodes.size)))
                + edge_priority
                + half_extent * 0.01
            )
            blocker_rows.append((blocker_score, rect_idx, nodes, center, signed, half_extent))
        if not blocker_rows:
            continue
        blocker_rows.sort(reverse=True, key=lambda item: item[0])

        wanted_sizes = (2, 3, 4, 6, 8, 12, 16, max_blockers_per_edge)
        batch_sizes: list[int] = []
        for size in wanted_sizes:
            got = min(size, len(blocker_rows), max_blockers_per_edge)
            if got > 0 and got not in batch_sizes:
                batch_sizes.append(got)

        for batch_size in batch_sizes:
            batch = blocker_rows[:batch_size]
            for mode in ("split", "positive", "negative"):
                for pad in pads:
                    target_by_node: dict[int, tuple[float, np.ndarray]] = {}
                    moved_count = 0
                    for _score, rect_idx, nodes, center, signed, half_extent in batch:
                        if mode == "split":
                            side = 1.0 if signed >= 0.0 else -1.0
                        else:
                            side = 1.0 if mode == "positive" else -1.0
                        direction = normal * side
                        current_clearance = float(np.dot(center - a, direction))
                        denom_dir = float(np.dot(edge_vec, edge_vec))
                        if denom_dir >= 1e-9:
                            t = float(np.dot(center - a, edge_vec) / denom_dir)
                            t = max(0.0, min(1.0, t))
                            closest = a + edge_vec * t
                            current_clearance = float(np.dot(center - closest, direction))
                        needed = max(
                            0.0,
                            half_extent + evaluator.edge_rect_margin + 32.0 - current_clearance,
                        )
                        delta = direction * float(needed + pad)
                        if float(np.linalg.norm(delta)) <= 1e-6:
                            continue
                        moved_count += 1
                        delta_norm = float(np.linalg.norm(delta))
                        for node_raw in nodes:
                            node = int(node_raw)
                            target = positions[node] + delta
                            old = target_by_node.get(node)
                            if old is None or delta_norm > old[0]:
                                target_by_node[node] = (delta_norm, target)
                    if not target_by_node or moved_count == 0:
                        continue
                    node_items = sorted(target_by_node.items())
                    cand_nodes = np.array([node for node, _row in node_items], dtype=np.int32)
                    targets = np.vstack([row[1] for _node, row in node_items]).astype(np.float64)
                    candidates.append(
                        Candidate(
                            "visual_edge_rect_corridor_clear",
                            cand_nodes,
                            targets=targets,
                            priority=edge_priority * 2.0 + float(batch_size) * 25.0 - float(cand_nodes.size) * 0.1,
                            label=f"edge-corridor:{edge_rank}:edge={edge_idx}:n={batch_size}:{mode}:pad={pad:.0f}",
                        )
                    )
    return candidates


def edge_rect_force_relax_candidates(
    evaluator: ExactRelationEvaluator,
    positions: np.ndarray,
    state: dict[str, np.ndarray],
    edge_counts: np.ndarray,
    node_counts: np.ndarray,
    *,
    max_hits: int,
    max_nodes: int,
    scales: list[float],
    padding: float,
) -> list[Candidate]:
    """Accumulate edge-node collision forces and try smooth blocker moves."""
    if max_hits <= 0 or max_nodes <= 0 or not scales:
        return []
    flags = state["edge_rect_flags"]
    hit_idx = np.flatnonzero(flags)
    if hit_idx.size == 0:
        return []

    pair_edge = evaluator.edge_rect_pair_edge
    pair_rect = evaluator.edge_rect_pair_rect
    ranked: list[tuple[float, int, int]] = []
    for hit_raw in hit_idx:
        hit = int(hit_raw)
        edge_idx = int(pair_edge[hit])
        rect_idx = int(pair_rect[hit])
        rect_effect = evaluator.geometry.rect_effect_nodes_by_rect[rect_idx]
        if not rect_effect:
            continue
        nodes = np.array(rect_effect, dtype=np.int32)
        priority = (
            float(edge_counts[edge_idx]) * 8.0
            + float(node_counts[nodes].sum()) / math.sqrt(max(1, int(nodes.size)))
            + float(len(rect_effect)) * 0.1
        )
        ranked.append((priority, edge_idx, rect_idx))
    if not ranked:
        return []
    ranked.sort(reverse=True, key=lambda item: item[0])

    forces = np.zeros_like(positions, dtype=np.float64)
    weights = np.zeros(positions.shape[0], dtype=np.float64)
    segments = state["segments"]
    rect_positions = state["rect_positions"]
    for priority, edge_idx, rect_idx in ranked[:max_hits]:
        seg = segments[edge_idx]
        a = seg[0]
        b = seg[1]
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
        signed = float(np.dot(center - closest, normal))
        direction = normal if signed >= 0.0 else -normal
        rect_w = float(evaluator.geometry.rect_widths[rect_idx])
        rect_h = float(evaluator.geometry.rect_heights[rect_idx])
        half_extent = (
            abs(float(direction[0])) * rect_w
            + abs(float(direction[1])) * rect_h
        ) / 2.0
        current_clearance = float(np.dot(center - closest, direction))
        penetration = max(
            0.0,
            half_extent + evaluator.edge_rect_margin + float(padding) - current_clearance,
        )
        if penetration <= 1e-6:
            continue
        rect_effect = evaluator.geometry.rect_effect_nodes_by_rect[rect_idx]
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
    order = active[np.argsort(-norms)]
    order = order[:max_nodes]
    if order.size == 0:
        return []

    candidates: list[Candidate] = []
    wanted_sizes = (8, 16, 32, 48, 64, 96, 128, max_nodes)
    emitted: set[int] = set()
    for wanted in wanted_sizes:
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
            targets = positions[nodes] + clipped
            candidates.append(
                Candidate(
                    "visual_edge_rect_force_relax",
                    nodes,
                    targets=targets,
                    priority=float(norms[:size].sum()) + float(size) * 8.0,
                    label=f"edge-rect-force:n={size}:scale={scale:.2f}",
                )
            )
    return candidates


def visual_group_fan_candidates(
    evaluator: ExactRelationEvaluator,
    positions: np.ndarray,
    state: dict[str, np.ndarray],
    edge_counts: np.ndarray,
    node_counts: np.ndarray,
    groups: dict[str, np.ndarray],
    max_groups: int,
    max_group_size: int,
    steps: list[float],
) -> list[Candidate]:
    """Move crossing-heavy node groups away from their external edge fan."""
    if max_groups <= 0 or max_group_size <= 0 or not steps:
        return []
    segments = state["segments"]
    cross_flags = state["cross_flags"]
    pair_i = evaluator.edge_pair_i[cross_flags]
    pair_j = evaluator.edge_pair_j[cross_flags]
    if pair_i.size == 0:
        return []

    ranked: list[tuple[float, str, np.ndarray, np.ndarray]] = []
    for key, raw_members in groups.items():
        members = np.asarray(raw_members, dtype=np.int32)
        if members.size < 2 or members.size > max_group_size:
            continue
        touched_edges = np.zeros(evaluator.edge_count, dtype=bool)
        for node_raw in members:
            node = int(node_raw)
            if 0 <= node < len(evaluator.node_to_edges):
                touched_edges[evaluator.node_to_edges[node]] = True
        if not np.any(touched_edges):
            continue
        pair_touches = touched_edges[pair_i] | touched_edges[pair_j]
        pair_count = int(pair_touches.sum())
        if pair_count <= 0:
            continue
        score = (
            float(edge_counts[touched_edges].sum())
            + float(node_counts[members].sum()) / math.sqrt(max(1, members.size))
            + float(pair_count) * 2.0
        )
        if score <= 0.0:
            continue
        ranked.append((score, key, members, touched_edges))
    ranked.sort(reverse=True, key=lambda item: item[0])

    candidates: list[Candidate] = []
    for rank, (score, key, members, touched_edges) in enumerate(ranked[:max_groups]):
        in_i = touched_edges[pair_i]
        in_j = touched_edges[pair_j]
        external = np.concatenate([pair_j[in_i & ~in_j], pair_i[in_j & ~in_i]])
        group_edges = np.concatenate([pair_i[in_i], pair_j[in_j]])
        if external.size == 0 or group_edges.size == 0:
            continue
        external = np.unique(external).astype(np.int32)
        group_edges = np.unique(group_edges).astype(np.int32)

        centroid = positions[members].mean(axis=0)
        partner_mid = segments[external].mean(axis=1)
        partner_weights = np.maximum(1.0, edge_counts[external].astype(np.float64))
        partner_center = np.average(partner_mid, axis=0, weights=partner_weights)
        group_mid = segments[group_edges].mean(axis=1)
        group_weights = np.maximum(1.0, edge_counts[group_edges].astype(np.float64))
        group_center = np.average(group_mid, axis=0, weights=group_weights)

        dirs: list[np.ndarray] = []
        base = unit(centroid - partner_center)
        add_unique_direction(dirs, base)
        add_unique_direction(dirs, -base if base is not None else None)
        base2 = unit(group_center - partner_center)
        add_unique_direction(dirs, base2)
        add_unique_direction(dirs, -base2 if base2 is not None else None)

        partner_vec = segments[external, 1] - segments[external, 0]
        norms = np.linalg.norm(partner_vec, axis=1)
        good = norms > 1e-9
        if np.any(good):
            partner_unit = partner_vec[good] / norms[good].reshape(-1, 1)
            partner_normal = np.stack([-partner_unit[:, 1], partner_unit[:, 0]], axis=1)
            rel = centroid.reshape(1, 2) - partner_mid[good]
            sign = np.sign(np.sum(rel * partner_normal, axis=1))
            sign[sign == 0] = 1.0
            weights = partner_weights[good].reshape(-1, 1)
            aggregate = (partner_normal * sign.reshape(-1, 1) * weights).sum(axis=0)
            side = unit(aggregate)
            add_unique_direction(dirs, side)
            add_unique_direction(dirs, -side if side is not None else None)

        if not dirs:
            continue
        for step in steps:
            for direction in dirs[:6]:
                candidates.append(
                    Candidate(
                        "visual_group_fan_translate",
                        members,
                        dx=float(direction[0] * step),
                        dy=float(direction[1] * step),
                        priority=float(score),
                        label=f"group-fan:{rank}:{key}",
                    )
                )
    return candidates


def visual_hub_spoke_pack_candidates(
    evaluator: ExactRelationEvaluator,
    positions: np.ndarray,
    state: dict[str, np.ndarray],
    edge_counts: np.ndarray,
    node_counts: np.ndarray,
    *,
    max_hubs: int,
    max_spokes_per_hub: int,
    max_spoke_nodes: int,
    radii: list[float],
) -> list[Candidate]:
    """Move crossing-heavy endpoints closer to the hot hub they connect to."""
    if max_hubs <= 0 or max_spokes_per_hub <= 0 or max_spoke_nodes <= 0 or not radii:
        return []
    rect_positions = state["rect_positions"]
    hub_rows: dict[int, list[tuple[float, int, np.ndarray, int]]] = {}
    for edge_idx_raw in np.flatnonzero(edge_counts > 0):
        edge_idx = int(edge_idx_raw)
        edge = evaluator.visual_edges[edge_idx]
        for hub_rect, spoke_rect, spoke_nodes in (
            (int(edge.source_rect), int(edge.target_rect), edge.target_nodes),
            (int(edge.target_rect), int(edge.source_rect), edge.source_nodes),
        ):
            if spoke_nodes.size == 0 or spoke_nodes.size > max_spoke_nodes:
                continue
            score = (
                float(edge_counts[edge_idx]) * 10.0
                + float(node_counts[spoke_nodes].sum()) / math.sqrt(max(1, int(spoke_nodes.size)))
                + float(spoke_nodes.size)
            )
            hub_rows.setdefault(hub_rect, []).append((score, edge_idx, spoke_nodes, spoke_rect))
    if not hub_rows:
        return []

    ranked_hubs = sorted(
        (
            (sum(row[0] for row in rows), hub_rect, rows)
            for hub_rect, rows in hub_rows.items()
        ),
        reverse=True,
        key=lambda item: item[0],
    )[:max_hubs]

    candidates: list[Candidate] = []
    directions8 = [np.array([dx, dy], dtype=np.float64) for dx, dy in unit_directions()]
    for hub_rank, (hub_score, hub_rect, rows) in enumerate(ranked_hubs):
        hub_center = rect_positions[int(hub_rect)]
        rows.sort(reverse=True, key=lambda item: item[0])
        trimmed = rows[:max_spokes_per_hub]

        for spoke_rank, (score, edge_idx, spoke_nodes, _spoke_rect) in enumerate(trimmed):
            centroid = positions[spoke_nodes].mean(axis=0)
            dirs: list[np.ndarray] = []
            current_dir = unit(centroid - hub_center)
            add_unique_direction(dirs, current_dir)
            if current_dir is not None:
                add_unique_direction(dirs, np.array([-current_dir[1], current_dir[0]], dtype=np.float64))
                add_unique_direction(dirs, np.array([current_dir[1], -current_dir[0]], dtype=np.float64))
            for direction in directions8:
                add_unique_direction(dirs, direction)
            for radius in radii:
                if radius <= 0.0:
                    continue
                for direction in dirs[:5]:
                    target_centroid = hub_center + direction * float(radius)
                    targets = positions[spoke_nodes] + (target_centroid - centroid).reshape(1, 2)
                    candidates.append(
                        Candidate(
                            "visual_hub_spoke_orbit",
                            spoke_nodes,
                            targets=targets,
                            priority=hub_score + score * 4.0,
                            label=f"hub-spoke:{hub_rank}:{spoke_rank}:edge={edge_idx}:r={radius:.0f}",
                        )
                    )

        for batch_size in (3, 5, 8, 12, max_spokes_per_hub):
            batch = trimmed[: min(batch_size, len(trimmed))]
            if len(batch) < 2:
                continue
            used: set[int] = set()
            pieces: list[np.ndarray] = []
            source_centroids: list[np.ndarray] = []
            weights: list[float] = []
            for score, _edge_idx, spoke_nodes, _spoke_rect in batch:
                nodes = [int(idx) for idx in spoke_nodes.tolist() if int(idx) not in used]
                if not nodes:
                    continue
                for node in nodes:
                    used.add(node)
                arr = np.array(nodes, dtype=np.int32)
                pieces.append(arr)
                source_centroids.append(positions[arr].mean(axis=0))
                weights.append(float(score))
            if len(pieces) < 2:
                continue
            angles = [math.atan2(float(c[1] - hub_center[1]), float(c[0] - hub_center[0])) for c in source_centroids]
            order = sorted(range(len(pieces)), key=lambda idx: angles[idx])
            center_angle = math.atan2(
                float(sum(math.sin(angles[idx]) * weights[idx] for idx in range(len(pieces)))),
                float(sum(math.cos(angles[idx]) * weights[idx] for idx in range(len(pieces)))),
            )
            spread = min(math.pi * 1.35, max(math.pi / 4.0, 0.26 * len(order)))
            for radius in radii:
                if radius <= 0.0:
                    continue
                all_nodes: list[np.ndarray] = []
                all_targets: list[np.ndarray] = []
                for pos_idx, item_idx in enumerate(order):
                    if len(order) == 1:
                        angle = center_angle
                    else:
                        angle = center_angle + (pos_idx / (len(order) - 1) - 0.5) * spread
                    target_centroid = hub_center + np.array(
                        [math.cos(angle) * float(radius), math.sin(angle) * float(radius)],
                        dtype=np.float64,
                    )
                    arr = pieces[item_idx]
                    centroid = positions[arr].mean(axis=0)
                    all_nodes.append(arr)
                    all_targets.append(positions[arr] + (target_centroid - centroid).reshape(1, 2))
                joined = np.concatenate(all_nodes).astype(np.int32)
                if np.unique(joined).size != joined.size:
                    continue
                targets = np.concatenate(all_targets).astype(np.float64)
                candidates.append(
                    Candidate(
                        "visual_hub_spoke_batch_pack",
                        joined,
                        targets=targets,
                        priority=hub_score + float(joined.size) * 40.0,
                        label=f"hub-spoke-batch:{hub_rank}:n={joined.size}:r={radius:.0f}",
                    )
                )
    return candidates


def build_groups_by_node(
    groups: dict[str, np.ndarray],
    n_nodes: int,
    max_group_size: int,
) -> tuple[list[tuple[str, np.ndarray]], ...]:
    by_node: list[list[tuple[str, np.ndarray]]] = [[] for _ in range(n_nodes)]
    for key, raw_members in groups.items():
        members = np.asarray(raw_members, dtype=np.int32)
        if members.size < 2 or members.size > max_group_size:
            continue
        for node_raw in members:
            node = int(node_raw)
            if 0 <= node < n_nodes:
                by_node[node].append((key, members))
    return tuple(by_node)


def endpoint_move_options(
    evaluator: ExactRelationEvaluator,
    edge: VisualEdge,
    endpoint: str,
    groups_by_node: tuple[list[tuple[str, np.ndarray]], ...],
    node_counts: np.ndarray,
    max_group_options: int,
) -> list[tuple[str, np.ndarray, float]]:
    base = group_for_endpoint(evaluator, edge, endpoint)
    if base.size == 0:
        return []
    options: list[tuple[str, np.ndarray, float]] = [
        ("endpoint", base, float(node_counts[base].sum()) if base.size else 0.0)
    ]
    seen = {tuple(int(x) for x in base.tolist())}
    if base.size == 1:
        node = int(base[0])
        group_options = groups_by_node[node] if 0 <= node < len(groups_by_node) else []
        ranked: list[tuple[float, str, np.ndarray]] = []
        for key, members in group_options:
            priority = float(node_counts[members].sum()) / math.sqrt(max(1, members.size))
            ranked.append((priority, key, members))
        ranked.sort(reverse=True, key=lambda item: item[0])
        for priority, key, members in ranked[:max_group_options]:
            member_key = tuple(int(x) for x in members.tolist())
            if member_key in seen:
                continue
            seen.add(member_key)
            options.append((key, members, priority))
    return options


def visual_endpoint_orbit_candidates(
    evaluator: ExactRelationEvaluator,
    positions: np.ndarray,
    edge_counts: np.ndarray,
    node_counts: np.ndarray,
    groups_by_node: tuple[list[tuple[str, np.ndarray]], ...],
    max_edges: int,
    max_group_options: int,
    radii: list[float],
) -> list[Candidate]:
    if max_edges <= 0 or not radii:
        return []
    ranked_edges = np.argsort(-edge_counts)[:max_edges]
    candidates: list[Candidate] = []
    dirs = unit_directions()
    for rank, edge_idx_raw in enumerate(ranked_edges):
        edge_idx = int(edge_idx_raw)
        if edge_counts[edge_idx] <= 0:
            continue
        edge = evaluator.visual_edges[edge_idx]
        priority = float(edge_counts[edge_idx])
        for endpoint, partner_endpoint in (("source", "target"), ("target", "source")):
            partner_nodes = group_for_endpoint(evaluator, edge, partner_endpoint)
            if partner_nodes.size == 0:
                continue
            anchor = positions[partner_nodes].mean(axis=0)
            for option_key, nodes, option_priority in endpoint_move_options(
                evaluator,
                edge,
                endpoint,
                groups_by_node,
                node_counts,
                max_group_options,
            ):
                if np.intersect1d(nodes, partner_nodes, assume_unique=False).size > 0:
                    continue
                centroid = positions[nodes].mean(axis=0)
                for radius in radii:
                    if radius <= 0.0:
                        targets = positions[nodes] + (anchor - centroid).reshape(1, 2)
                        candidates.append(
                            Candidate(
                                "visual_endpoint_partner_orbit",
                                nodes,
                                targets=targets,
                                priority=priority * 14.0 + option_priority,
                                label=f"endpoint-orbit:{rank}:{edge_idx}:{endpoint}:{option_key}",
                            )
                        )
                        continue
                    for dx, dy in dirs:
                        target_centroid = anchor + np.array([dx * radius, dy * radius])
                        targets = positions[nodes] + (target_centroid - centroid).reshape(1, 2)
                        candidates.append(
                            Candidate(
                                "visual_endpoint_partner_orbit",
                                nodes,
                                targets=targets,
                                priority=priority * 14.0 + option_priority,
                                label=f"endpoint-orbit:{rank}:{edge_idx}:{endpoint}:{option_key}",
                            )
                        )
    return candidates


def visual_endpoint_swap_candidates(
    evaluator: ExactRelationEvaluator,
    positions: np.ndarray,
    state: dict[str, np.ndarray],
    edge_counts: np.ndarray,
    max_pairs: int,
) -> list[Candidate]:
    if max_pairs <= 0:
        return []
    cross_flags = state["cross_flags"]
    pair_i = evaluator.edge_pair_i[cross_flags]
    pair_j = evaluator.edge_pair_j[cross_flags]
    if pair_i.size == 0:
        return []
    priority = edge_counts[pair_i] + edge_counts[pair_j]
    order = np.argsort(-priority)[:max_pairs]
    candidates: list[Candidate] = []
    for rank, pair_pos in enumerate(order):
        e1 = int(pair_i[int(pair_pos)])
        e2 = int(pair_j[int(pair_pos)])
        edge1 = evaluator.visual_edges[e1]
        edge2 = evaluator.visual_edges[e2]
        for endpoint1 in ("source", "target"):
            nodes1 = group_for_endpoint(evaluator, edge1, endpoint1)
            if nodes1.size == 0:
                continue
            for endpoint2 in ("source", "target"):
                nodes2 = group_for_endpoint(evaluator, edge2, endpoint2)
                if nodes2.size == 0:
                    continue
                if np.intersect1d(nodes1, nodes2, assume_unique=False).size > 0:
                    continue
                c1 = positions[nodes1].mean(axis=0)
                c2 = positions[nodes2].mean(axis=0)
                joined = np.concatenate([nodes1, nodes2]).astype(np.int32)
                split = int(nodes1.size)
                targets = positions[joined].copy()
                targets[:split] += (c2 - c1).reshape(1, 2)
                targets[split:] += (c1 - c2).reshape(1, 2)
                candidates.append(
                    Candidate(
                        "visual_endpoint_swap",
                        joined,
                        targets=targets,
                        priority=float(priority[int(pair_pos)]) * 10.0,
                        label=f"endpoint-swap:{rank}:{e1}:{endpoint1}:{e2}:{endpoint2}",
                    )
                )
    return candidates


def visual_group_swap_candidates(
    evaluator: ExactRelationEvaluator,
    positions: np.ndarray,
    state: dict[str, np.ndarray],
    edge_counts: np.ndarray,
    node_counts: np.ndarray,
    groups_by_node: tuple[list[tuple[str, np.ndarray]], ...],
    *,
    max_pairs: int,
    max_group_options: int,
    max_total_nodes: int,
) -> list[Candidate]:
    """Swap whole endpoint groups for crossing pairs.

    This is a pure untangling candidate: every original node and edge remains
    intact.  It only changes the relative order of the real endpoint groups.
    """
    if max_pairs <= 0 or max_group_options <= 0 or max_total_nodes <= 0:
        return []
    cross_flags = state["cross_flags"]
    pair_i = evaluator.edge_pair_i[cross_flags]
    pair_j = evaluator.edge_pair_j[cross_flags]
    if pair_i.size == 0:
        return []
    priority = edge_counts[pair_i] + edge_counts[pair_j]
    order = np.argsort(-priority)[:max_pairs]
    candidates: list[Candidate] = []
    seen: set[tuple[tuple[int, ...], tuple[int, ...]]] = set()
    for rank, pair_pos in enumerate(order):
        e1 = int(pair_i[int(pair_pos)])
        e2 = int(pair_j[int(pair_pos)])
        edge1 = evaluator.visual_edges[e1]
        edge2 = evaluator.visual_edges[e2]
        pair_priority = float(priority[int(pair_pos)])
        endpoint_options_1: list[tuple[str, np.ndarray, float]] = []
        endpoint_options_2: list[tuple[str, np.ndarray, float]] = []
        for endpoint in ("source", "target"):
            endpoint_options_1.extend(
                endpoint_move_options(
                    evaluator,
                    edge1,
                    endpoint,
                    groups_by_node,
                    node_counts,
                    max_group_options,
                )
            )
            endpoint_options_2.extend(
                endpoint_move_options(
                    evaluator,
                    edge2,
                    endpoint,
                    groups_by_node,
                    node_counts,
                    max_group_options,
                )
            )
        for key1, nodes1_raw, score1 in endpoint_options_1:
            nodes1 = np.unique(nodes1_raw).astype(np.int32)
            if nodes1.size == 0:
                continue
            for key2, nodes2_raw, score2 in endpoint_options_2:
                nodes2 = np.unique(nodes2_raw).astype(np.int32)
                if nodes2.size == 0:
                    continue
                if int(nodes1.size + nodes2.size) > max_total_nodes:
                    continue
                if np.intersect1d(nodes1, nodes2, assume_unique=False).size > 0:
                    continue
                key = (tuple(nodes1.tolist()), tuple(nodes2.tolist()))
                rev_key = (key[1], key[0])
                if key in seen or rev_key in seen:
                    continue
                seen.add(key)
                c1 = positions[nodes1].mean(axis=0)
                c2 = positions[nodes2].mean(axis=0)
                joined = np.concatenate([nodes1, nodes2]).astype(np.int32)
                split = int(nodes1.size)
                targets = positions[joined].copy()
                targets[:split] += (c2 - c1).reshape(1, 2)
                targets[split:] += (c1 - c2).reshape(1, 2)
                candidates.append(
                    Candidate(
                        "visual_group_swap",
                        joined,
                        targets=targets,
                        priority=pair_priority * 12.0 + score1 + score2,
                        label=f"group-swap:{rank}:{e1}:{key1}:{e2}:{key2}",
                    )
                )

                midpoint = (c1 + c2) / 2.0
                targets_half = positions[joined].copy()
                targets_half[:split] += (midpoint - c1).reshape(1, 2)
                targets_half[split:] += (midpoint - c2).reshape(1, 2)
                candidates.append(
                    Candidate(
                        "visual_group_collapse_between_crossing",
                        joined,
                        targets=targets_half,
                        priority=pair_priority * 8.0 + score1 + score2,
                        label=f"group-collapse:{rank}:{e1}:{key1}:{e2}:{key2}",
                    )
                )
    return candidates


def visual_hot_edge_group_anchor_candidates(
    evaluator: ExactRelationEvaluator,
    positions: np.ndarray,
    state: dict[str, np.ndarray],
    edge_counts: np.ndarray,
    node_counts: np.ndarray,
    groups_by_node: tuple[list[tuple[str, np.ndarray]], ...],
    max_edges: int,
    max_group_options: int,
    max_total_nodes: int,
    radii: list[float],
) -> list[Candidate]:
    """Anchor hot edge endpoint groups near each other."""
    if max_edges <= 0 or max_total_nodes <= 0 or not radii:
        return []
    segments = state["segments"]
    ranked_edges = np.argsort(-edge_counts)[:max_edges]
    candidates: list[Candidate] = []
    for rank, edge_idx_raw in enumerate(ranked_edges):
        edge_idx = int(edge_idx_raw)
        if edge_counts[edge_idx] <= 0:
            continue
        edge = evaluator.visual_edges[edge_idx]
        source_options = endpoint_move_options(
            evaluator,
            edge,
            "source",
            groups_by_node,
            node_counts,
            max_group_options,
        )
        target_options = endpoint_move_options(
            evaluator,
            edge,
            "target",
            groups_by_node,
            node_counts,
            max_group_options,
        )
        if not source_options or not target_options:
            continue
        seg = segments[edge_idx]
        along = unit(seg[1] - seg[0])
        if along is None:
            continue
        normal = np.array([-along[1], along[0]], dtype=np.float64)
        dirs = [along, -along, normal, -normal]
        priority_base = float(edge_counts[edge_idx]) * 18.0
        for source_key, source_nodes, source_priority in source_options:
            source_centroid = positions[source_nodes].mean(axis=0)
            for target_key, target_nodes, target_priority in target_options:
                if np.intersect1d(source_nodes, target_nodes, assume_unique=False).size > 0:
                    continue
                total_nodes = int(source_nodes.size + target_nodes.size)
                if total_nodes > max_total_nodes:
                    continue
                target_centroid = positions[target_nodes].mean(axis=0)
                midpoint = (source_centroid + target_centroid) / 2.0
                priority = priority_base + source_priority + target_priority
                for radius in radii:
                    if radius <= 0.0:
                        source_targets = positions[source_nodes] + (
                            target_centroid - source_centroid
                        ).reshape(1, 2)
                        candidates.append(
                            Candidate(
                                "visual_hot_edge_group_anchor",
                                source_nodes,
                                targets=source_targets,
                                priority=priority,
                                label=f"hot-anchor:{rank}:{edge_idx}:s->{target_key}",
                            )
                        )
                        target_targets = positions[target_nodes] + (
                            source_centroid - target_centroid
                        ).reshape(1, 2)
                        candidates.append(
                            Candidate(
                                "visual_hot_edge_group_anchor",
                                target_nodes,
                                targets=target_targets,
                                priority=priority,
                                label=f"hot-anchor:{rank}:{edge_idx}:t->{source_key}",
                            )
                        )
                        continue
                    for direction in dirs:
                        source_target_centroid = target_centroid + direction * radius
                        source_targets = positions[source_nodes] + (
                            source_target_centroid - source_centroid
                        ).reshape(1, 2)
                        candidates.append(
                            Candidate(
                                "visual_hot_edge_group_anchor",
                                source_nodes,
                                targets=source_targets,
                                priority=priority,
                                label=f"hot-anchor:{rank}:{edge_idx}:source:{source_key}",
                            )
                        )
                        target_target_centroid = source_centroid - direction * radius
                        target_targets = positions[target_nodes] + (
                            target_target_centroid - target_centroid
                        ).reshape(1, 2)
                        candidates.append(
                            Candidate(
                                "visual_hot_edge_group_anchor",
                                target_nodes,
                                targets=target_targets,
                                priority=priority,
                                label=f"hot-anchor:{rank}:{edge_idx}:target:{target_key}",
                            )
                        )

                        joined = np.concatenate([source_nodes, target_nodes]).astype(np.int32)
                        split = int(source_nodes.size)
                        coordinated = positions[joined].copy()
                        coordinated[:split] += (
                            midpoint + direction * radius - source_centroid
                        ).reshape(1, 2)
                        coordinated[split:] += (
                            midpoint - direction * radius - target_centroid
                        ).reshape(1, 2)
                        candidates.append(
                            Candidate(
                                "visual_hot_edge_pair_anchor",
                                joined,
                                targets=coordinated,
                                priority=priority * 1.1,
                                label=f"hot-pair-anchor:{rank}:{edge_idx}:{source_key}:{target_key}",
                            )
                        )
    return candidates


def parse_scale_pairs(text: str) -> list[tuple[float, float]]:
    pairs: list[tuple[float, float]] = []
    for raw in text.split(","):
        part = raw.strip()
        if not part:
            continue
        if ":" in part:
            left, right = part.split(":", 1)
            pairs.append((float(left), float(right)))
        else:
            value = float(part)
            pairs.append((value, value))
    return pairs


def global_compression_candidates(
    positions: np.ndarray,
    scales: list[tuple[float, float]],
) -> list[Candidate]:
    if not scales:
        return []
    center = positions.mean(axis=0)
    all_nodes = np.arange(positions.shape[0], dtype=np.int32)
    candidates: list[Candidate] = []
    for sx, sy in scales:
        if sx <= 0.0 or sy <= 0.0 or (abs(sx - 1.0) < 1e-9 and abs(sy - 1.0) < 1e-9):
            continue
        targets = positions.copy()
        targets[:, 0] = center[0] + (positions[:, 0] - center[0]) * sx
        targets[:, 1] = center[1] + (positions[:, 1] - center[1]) * sy
        candidates.append(
            Candidate(
                "global_space_compress",
                all_nodes,
                targets=targets,
                priority=50_000.0 * max(0.0, (1.0 - sx) + (1.0 - sy)),
                label=f"compress:{sx:.3f}:{sy:.3f}",
            )
        )
    return candidates


def semantic_cluster_participation_candidates(
    layout: dict,
    positions: np.ndarray,
    node_counts: np.ndarray,
    raw_edges: np.ndarray,
    cluster_groups: dict[str, np.ndarray],
    *,
    max_nodes: int,
    top_clusters_per_node: int,
    singleton_max_size: int,
    max_cluster_size: int,
    min_score: float,
    radii: list[float],
) -> list[Candidate]:
    """Move standalone nodes near a semantically plausible cluster.

    Signals are intentionally generic:
    * model id app/name tokens,
    * app distribution of each cluster,
    * token distribution of each cluster, and
    * current graph-neighbour cluster vote.
    """
    if max_nodes <= 0 or top_clusters_per_node <= 0 or not radii:
        return []
    nodes = layout["nodes"]
    n_nodes = len(nodes)
    model_ids = [str(nd.get("modelId") or "") for nd in nodes]
    node_parts = [v34.semantic_name_parts(model_id) for model_id in model_ids]
    cluster_by_node = [str(nd.get("clusterId") or "") for nd in nodes]
    cluster_size_by_key = {
        key: int(np.asarray(members).size)
        for key, members in cluster_groups.items()
    }
    leaf_ids, _bundles = v34.bundle_leaf_index_sets(layout)

    adjacency = [[] for _ in range(n_nodes)]
    for s_raw, t_raw in raw_edges:
        s = int(s_raw)
        t = int(t_raw)
        adjacency[s].append(t)
        adjacency[t].append(s)

    profiles: list[dict] = []
    for key, raw_members in cluster_groups.items():
        members = np.asarray(raw_members, dtype=np.int32)
        if members.size < 2 or members.size > max_cluster_size:
            continue
        if v34.group_is_pseudo(key):
            continue
        token_counts: Counter[str] = Counter()
        app_counts: Counter[str] = Counter()
        for node_raw in members:
            app, tokens = node_parts[int(node_raw)]
            app_counts[app] += 1
            token_counts.update(tokens)
        pts = positions[members]
        spread_x = float(pts[:, 0].max() - pts[:, 0].min())
        spread_y = float(pts[:, 1].max() - pts[:, 1].min())
        radius = max(180.0, 0.5 * max(spread_x, spread_y))
        profiles.append(
            {
                "key": key,
                "members": members,
                "tokens": set(token_counts.keys()),
                "apps": app_counts,
                "size": int(members.size),
                "centroid": pts.mean(axis=0),
                "radius": radius,
            }
        )
    if not profiles:
        return []

    ranked_nodes: list[tuple[float, int]] = []
    for idx, model_id in enumerate(model_ids):
        if model_id in leaf_ids:
            continue
        cluster = cluster_by_node[idx]
        cluster_size = cluster_size_by_key.get(cluster, 0) if cluster else 0
        if cluster and cluster_size > singleton_max_size:
            continue
        degree = len(adjacency[idx])
        # Prefer isolated/small-cluster nodes near the bbox edge or with many
        # crossings; those are most likely to shrink space or untie long edges.
        center = positions.mean(axis=0)
        dist = float(np.linalg.norm(positions[idx] - center))
        priority = dist / 1000.0 + float(node_counts[idx]) * 8.0 + max(0, 4 - degree)
        ranked_nodes.append((priority, idx))
    ranked_nodes.sort(reverse=True, key=lambda item: item[0])

    directions = unit_directions()
    candidates: list[Candidate] = []
    for _priority, idx in ranked_nodes[:max_nodes]:
        app, tokens = node_parts[idx]
        current_cluster = cluster_by_node[idx]
        neighbor_votes: Counter[str] = Counter(
            cluster_by_node[nbr]
            for nbr in adjacency[idx]
            if cluster_by_node[nbr]
        )
        degree = max(1, len(adjacency[idx]))
        scored: list[tuple[float, dict, float, int, float]] = []
        for profile in profiles:
            if profile["key"] == current_cluster:
                continue
            shared = tokens & profile["tokens"]
            union = tokens | profile["tokens"]
            jaccard = float(len(shared) / len(union)) if union else 0.0
            app_match = float(profile["apps"].get(app, 0) / profile["size"])
            neighbor_vote = float(neighbor_votes.get(profile["key"], 0) / degree)
            score = (
                1.45 * jaccard
                + 0.80 * app_match
                + 0.08 * len(shared)
                + 0.90 * neighbor_vote
            )
            if score < min_score:
                continue
            scored.append((score, profile, app_match, len(shared), neighbor_vote))
        scored.sort(reverse=True, key=lambda item: item[0])
        node_arr = np.array([idx], dtype=np.int32)
        for score, profile, app_match, shared_count, neighbor_vote in scored[:top_clusters_per_node]:
            centroid = np.asarray(profile["centroid"], dtype=np.float64)
            for radius in radii:
                if radius <= 0.0:
                    target = centroid
                    candidates.append(
                        Candidate(
                            "semantic_cluster_participation",
                            node_arr,
                            targets=target.reshape(1, 2),
                            priority=score * 6000.0 + float(node_counts[idx]) * 20.0,
                            label=(
                                f"semantic:{model_ids[idx]}->{profile['key']}:"
                                f"score={score:.2f}:app={app_match:.2f}:"
                                f"shared={shared_count}:neighbor={neighbor_vote:.2f}"
                            ),
                        )
                    )
                    continue
                for dx, dy in directions:
                    direction = np.array([dx, dy], dtype=np.float64)
                    target = centroid + direction * (float(profile["radius"]) + radius)
                    candidates.append(
                        Candidate(
                            "semantic_cluster_participation",
                            node_arr,
                            targets=target.reshape(1, 2),
                            priority=score * 6000.0 + float(node_counts[idx]) * 20.0,
                            label=(
                                f"semantic:{model_ids[idx]}->{profile['key']}:"
                                f"score={score:.2f}:app={app_match:.2f}:"
                                f"shared={shared_count}:neighbor={neighbor_vote:.2f}"
                            ),
                        )
                    )
    return candidates


def pack_points_grid(
    anchor: np.ndarray,
    count: int,
    *,
    cell_w: float = 280.0,
    cell_h: float = 170.0,
) -> np.ndarray:
    if count <= 0:
        return np.zeros((0, 2), dtype=np.float64)
    cols = max(1, int(math.ceil(math.sqrt(count))))
    rows = max(1, int(math.ceil(count / cols)))
    out = np.zeros((count, 2), dtype=np.float64)
    for idx in range(count):
        row = idx // cols
        col = idx % cols
        out[idx, 0] = anchor[0] + (col - (cols - 1) / 2.0) * cell_w
        out[idx, 1] = anchor[1] + (row - (rows - 1) / 2.0) * cell_h
    return out


def rendered_grid_cell(
    evaluator: ExactRelationEvaluator,
    indices: np.ndarray,
    *,
    extra_w: float = 110.0,
    extra_h: float = 70.0,
) -> tuple[float, float]:
    widths: list[float] = []
    heights: list[float] = []
    for idx_raw in indices:
        rect_idx = evaluator.node_to_rect.get(int(idx_raw))
        if rect_idx is None:
            continue
        widths.append(float(evaluator.geometry.rect_widths[int(rect_idx)]))
        heights.append(float(evaluator.geometry.rect_heights[int(rect_idx)]))
    cell_w = max(280.0, (max(widths) if widths else 170.0) + extra_w)
    cell_h = max(175.0, (max(heights) if heights else 105.0) + extra_h)
    return cell_w, cell_h


def pack_points_grid_rendered(
    evaluator: ExactRelationEvaluator,
    anchor: np.ndarray,
    indices: np.ndarray,
    *,
    extra_w: float = 110.0,
    extra_h: float = 70.0,
) -> np.ndarray:
    cell_w, cell_h = rendered_grid_cell(
        evaluator,
        indices,
        extra_w=extra_w,
        extra_h=extra_h,
    )
    return pack_points_grid(anchor, int(indices.size), cell_w=cell_w, cell_h=cell_h)


def semantic_cluster_tighten_candidates(
    layout: dict,
    positions: np.ndarray,
    evaluator: ExactRelationEvaluator,
    node_counts: np.ndarray,
    cluster_groups: dict[str, np.ndarray],
    *,
    max_groups: int,
    min_group_size: int,
    max_group_size: int,
    max_cluster_size: int,
    pull_factors: list[float],
) -> list[Candidate]:
    """Tighten name/app-related nodes that already live in a broad cluster."""
    if max_groups <= 0 or max_group_size <= 1 or not pull_factors:
        return []
    nodes = layout["nodes"]
    model_ids = [str(nd.get("modelId") or "") for nd in nodes]
    node_parts = [v34.semantic_name_parts(model_id) for model_id in model_ids]
    leaf_ids, _bundles = v34.bundle_leaf_index_sets(layout)

    ranked: list[tuple[float, str, np.ndarray, np.ndarray]] = []
    seen_groups: set[tuple[str, tuple[int, ...]]] = set()
    for cluster_key, raw_members in cluster_groups.items():
        members = np.asarray(raw_members, dtype=np.int32)
        if members.size < min_group_size or members.size > max_cluster_size:
            continue
        if v34.group_is_pseudo(cluster_key):
            continue
        usable = [
            int(idx)
            for idx in members
            if model_ids[int(idx)] not in leaf_ids and int(idx) in evaluator.node_to_rect
        ]
        if len(usable) < min_group_size:
            continue
        buckets: dict[tuple[str, str], list[int]] = {}
        for idx in usable:
            app, tokens = node_parts[idx]
            buckets.setdefault(("app", app), []).append(idx)
            for token in tokens:
                buckets.setdefault(("token", token), []).append(idx)
        cluster_centroid = positions[np.array(usable, dtype=np.int32)].mean(axis=0)
        for semantic_key, bucket in buckets.items():
            unique = sorted(set(bucket))
            if len(unique) < min_group_size or len(unique) > max_group_size:
                continue
            indices = np.array(unique, dtype=np.int32)
            dedupe_key = (cluster_key, tuple(indices.tolist()))
            if dedupe_key in seen_groups:
                continue
            seen_groups.add(dedupe_key)
            pts = positions[indices]
            spread_x = float(pts[:, 0].max() - pts[:, 0].min())
            spread_y = float(pts[:, 1].max() - pts[:, 1].min())
            cell_w, cell_h = rendered_grid_cell(evaluator, indices)
            cols = max(1, int(math.ceil(math.sqrt(indices.size))))
            rows = max(1, int(math.ceil(indices.size / cols)))
            packed_w = max(1.0, (cols - 1) * cell_w)
            packed_h = max(1.0, (rows - 1) * cell_h)
            slack = max(0.0, spread_x - packed_w) + max(0.0, spread_y - packed_h)
            if slack <= 120.0 and int(node_counts[indices].sum()) <= 0:
                continue
            score = (
                slack / 20.0
                + float(indices.size) * 80.0
                + float(node_counts[indices].sum()) * 35.0
            )
            label_key = f"{semantic_key[0]}={semantic_key[1]}"
            ranked.append((score, f"{cluster_key}:{label_key}", indices, cluster_centroid))
    ranked.sort(reverse=True, key=lambda item: item[0])

    candidates: list[Candidate] = []
    for score, label_key, indices, cluster_centroid in ranked[:max_groups]:
        centroid = positions[indices].mean(axis=0)
        ordered = sorted(
            indices.tolist(),
            key=lambda idx: (
                v34.semantic_name_parts(model_ids[int(idx)])[0],
                model_ids[int(idx)],
            ),
        )
        ordered_indices = np.array(ordered, dtype=np.int32)
        for factor in pull_factors:
            anchor = cluster_centroid + (centroid - cluster_centroid) * float(factor)
            targets = pack_points_grid_rendered(
                evaluator,
                anchor,
                ordered_indices,
                extra_w=90.0,
                extra_h=55.0,
            )
            candidates.append(
                Candidate(
                    "semantic_cluster_tighten",
                    ordered_indices,
                    targets=targets,
                    priority=score,
                    label=f"semantic-tighten:{label_key}:n={ordered_indices.size}:f={factor:.2f}",
                )
            )
    return candidates


def global_semantic_family_candidates(
    layout: dict,
    positions: np.ndarray,
    evaluator: ExactRelationEvaluator,
    node_counts: np.ndarray,
    *,
    max_groups: int,
    min_group_size: int,
    max_group_size: int,
    min_boundary_pressure: float,
    pull_factors: list[float],
    scale_factors: list[float],
) -> list[Candidate]:
    """Compress app/name-token families that stretch the rendered bbox."""
    if max_groups <= 0 or max_group_size <= 1:
        return []
    nodes = layout["nodes"]
    model_ids = [str(nd.get("modelId") or "") for nd in nodes]
    node_parts = [v34.semantic_name_parts(model_id) for model_id in model_ids]
    leaf_ids, _bundles = v34.bundle_leaf_index_sets(layout)
    buckets: dict[tuple[str, str, str], set[int]] = {}
    for idx, model_id in enumerate(model_ids):
        if model_id in leaf_ids or idx not in evaluator.node_to_rect:
            continue
        app, tokens = node_parts[idx]
        buckets.setdefault(("app", app, app), set()).add(idx)
        for token in tokens:
            buckets.setdefault(("token", app, token), set()).add(idx)
        if len(tokens) >= 2:
            ordered_tokens = sorted(tokens)
            for left_idx in range(min(3, len(ordered_tokens))):
                for right_idx in range(left_idx + 1, min(4, len(ordered_tokens))):
                    pair = f"{ordered_tokens[left_idx]}+{ordered_tokens[right_idx]}"
                    buckets.setdefault(("pair", app, pair), set()).add(idx)

    min_x = float(positions[:, 0].min())
    max_x = float(positions[:, 0].max())
    min_y = float(positions[:, 1].min())
    max_y = float(positions[:, 1].max())
    width = max(1.0, max_x - min_x)
    height = max(1.0, max_y - min_y)
    graph_center = np.array([(min_x + max_x) / 2.0, (min_y + max_y) / 2.0], dtype=np.float64)

    ranked: list[tuple[float, str, np.ndarray, float]] = []
    seen: set[tuple[int, ...]] = set()
    for semantic_key, members in buckets.items():
        if len(members) < min_group_size or len(members) > max_group_size:
            continue
        indices = np.array(sorted(members), dtype=np.int32)
        dedupe_key = tuple(indices.tolist())
        if dedupe_key in seen:
            continue
        seen.add(dedupe_key)
        pts = positions[indices]
        centroid = pts.mean(axis=0)
        spread_x = float(pts[:, 0].max() - pts[:, 0].min())
        spread_y = float(pts[:, 1].max() - pts[:, 1].min())
        edge_pressure = max(
            (centroid[0] - graph_center[0]) / (width / 2.0),
            (graph_center[0] - centroid[0]) / (width / 2.0),
            (centroid[1] - graph_center[1]) / (height / 2.0),
            (graph_center[1] - centroid[1]) / (height / 2.0),
        )
        crossing_pressure = float(node_counts[indices].sum())
        if edge_pressure < min_boundary_pressure and crossing_pressure <= 0.0:
            continue
        cell_w, cell_h = rendered_grid_cell(evaluator, indices)
        cols = max(1, int(math.ceil(math.sqrt(indices.size))))
        rows = max(1, int(math.ceil(indices.size / cols)))
        slack = max(0.0, spread_x - (cols - 1) * cell_w) + max(
            0.0,
            spread_y - (rows - 1) * cell_h,
        )
        score = (
            max(0.0, edge_pressure) * 9000.0
            + slack / 12.0
            + crossing_pressure * 32.0
            + float(indices.size) * 120.0
        )
        label = f"{semantic_key[0]}:{semantic_key[1]}:{semantic_key[2]}"
        ranked.append((score, label, indices, edge_pressure))
    ranked.sort(reverse=True, key=lambda item: item[0])

    candidates: list[Candidate] = []
    for score, label, indices, edge_pressure in ranked[:max_groups]:
        pts = positions[indices]
        centroid = pts.mean(axis=0)
        for factor in pull_factors:
            target_centroid = graph_center + (centroid - graph_center) * float(factor)
            delta = target_centroid - centroid
            candidates.append(
                Candidate(
                    "global_semantic_family_pull",
                    indices,
                    targets=positions[indices] + delta.reshape(1, 2),
                    priority=score,
                    label=(
                        f"semantic-family-pull:{label}:n={indices.size}:"
                        f"pressure={edge_pressure:.2f}:f={factor:.2f}"
                    ),
                )
            )
        for scale in scale_factors:
            if scale <= 0.0 or scale >= 1.0:
                continue
            candidates.append(
                Candidate(
                    "global_semantic_family_scale",
                    indices,
                    targets=centroid.reshape(1, 2)
                    + (positions[indices] - centroid.reshape(1, 2)) * float(scale),
                    priority=score * (1.0 - float(scale) + 0.5),
                    label=f"semantic-family-scale:{label}:n={indices.size}:s={scale:.2f}",
                )
            )
        if indices.size <= 42:
            ordered = sorted(
                indices.tolist(),
                key=lambda idx: (
                    v34.semantic_name_parts(model_ids[int(idx)])[0],
                    model_ids[int(idx)],
                ),
            )
            ordered_indices = np.array(ordered, dtype=np.int32)
            for factor in pull_factors[:2]:
                target_centroid = graph_center + (centroid - graph_center) * float(factor)
                targets = pack_points_grid_rendered(
                    evaluator,
                    target_centroid,
                    ordered_indices,
                    extra_w=95.0,
                    extra_h=60.0,
                )
                candidates.append(
                    Candidate(
                        "global_semantic_family_pack",
                        ordered_indices,
                        targets=targets,
                        priority=score * 0.9,
                        label=(
                            f"semantic-family-pack:{label}:n={ordered_indices.size}:"
                            f"f={factor:.2f}"
                        ),
                    )
            )
    return candidates


def boundary_cluster_pull_candidates(
    layout: dict,
    positions: np.ndarray,
    evaluator: ExactRelationEvaluator,
    node_counts: np.ndarray,
    groups: dict[str, np.ndarray],
    *,
    max_groups: int,
    min_group_size: int,
    max_group_size: int,
    min_boundary_pressure: float,
    pull_factors: list[float],
    scale_factors: list[float],
) -> list[Candidate]:
    """Pull small boundary Louvain groups inward without changing internals much."""
    if max_groups <= 0:
        return []
    model_ids = [str(nd.get("modelId") or "") for nd in layout["nodes"]]
    leaf_ids, _bundles = v34.bundle_leaf_index_sets(layout)
    min_x = float(positions[:, 0].min())
    max_x = float(positions[:, 0].max())
    min_y = float(positions[:, 1].min())
    max_y = float(positions[:, 1].max())
    width = max(1.0, max_x - min_x)
    height = max(1.0, max_y - min_y)
    graph_center = np.array([(min_x + max_x) / 2.0, (min_y + max_y) / 2.0], dtype=np.float64)

    ranked: list[tuple[float, str, np.ndarray, float]] = []
    for group_key, raw_members in groups.items():
        if v34.group_is_pseudo(group_key):
            continue
        raw = np.asarray(raw_members, dtype=np.int32)
        indices = np.array(
            [
                int(idx)
                for idx in raw
                if model_ids[int(idx)] not in leaf_ids and int(idx) in evaluator.node_to_rect
            ],
            dtype=np.int32,
        )
        if indices.size < min_group_size or indices.size > max_group_size:
            continue
        pts = positions[indices]
        centroid = pts.mean(axis=0)
        edge_pressure = max(
            (centroid[0] - graph_center[0]) / (width / 2.0),
            (graph_center[0] - centroid[0]) / (width / 2.0),
            (centroid[1] - graph_center[1]) / (height / 2.0),
            (graph_center[1] - centroid[1]) / (height / 2.0),
        )
        if edge_pressure < min_boundary_pressure and int(node_counts[indices].sum()) <= 0:
            continue
        spread = max(
            float(pts[:, 0].max() - pts[:, 0].min()),
            float(pts[:, 1].max() - pts[:, 1].min()),
        )
        score = (
            max(0.0, edge_pressure) * 11000.0
            + spread / 8.0
            + float(node_counts[indices].sum()) * 36.0
            + float(indices.size) * 130.0
        )
        ranked.append((score, group_key, indices, edge_pressure))
    ranked.sort(reverse=True, key=lambda item: item[0])

    candidates: list[Candidate] = []
    for score, group_key, indices, edge_pressure in ranked[:max_groups]:
        pts = positions[indices]
        centroid = pts.mean(axis=0)
        for factor in pull_factors:
            target_centroid = graph_center + (centroid - graph_center) * float(factor)
            delta = target_centroid - centroid
            candidates.append(
                Candidate(
                    "boundary_cluster_pull",
                    indices,
                    targets=positions[indices] + delta.reshape(1, 2),
                    priority=score,
                    label=(
                        f"boundary-cluster-pull:{group_key}:n={indices.size}:"
                        f"pressure={edge_pressure:.2f}:f={factor:.2f}"
                    ),
                )
            )
        for scale in scale_factors:
            if scale <= 0.0 or scale >= 1.0:
                continue
            candidates.append(
                Candidate(
                    "boundary_cluster_scale",
                    indices,
                    targets=centroid.reshape(1, 2)
                    + (positions[indices] - centroid.reshape(1, 2)) * float(scale),
                    priority=score * (1.0 - float(scale) + 0.5),
                    label=f"boundary-cluster-scale:{group_key}:n={indices.size}:s={scale:.2f}",
                )
            )
        if indices.size <= 38:
            for factor in pull_factors[:2]:
                target_centroid = graph_center + (centroid - graph_center) * float(factor)
                targets = pack_points_grid_rendered(
                    evaluator,
                    target_centroid,
                    indices,
                    extra_w=100.0,
                    extra_h=65.0,
                )
                candidates.append(
                    Candidate(
                        "boundary_cluster_pack",
                        indices,
                        targets=targets,
                        priority=score * 0.8,
                        label=f"boundary-cluster-pack:{group_key}:n={indices.size}:f={factor:.2f}",
                    )
                )
    return candidates


def component_pack_targets(
    positions: np.ndarray,
    evaluator: ExactRelationEvaluator,
    components: list[np.ndarray],
    anchor: np.ndarray,
) -> tuple[np.ndarray, np.ndarray]:
    if not components:
        return (
            np.zeros(0, dtype=np.int32),
            np.zeros((0, 2), dtype=np.float64),
        )
    max_w = 0.0
    max_h = 0.0
    for component in components:
        pts = positions[component]
        spread_w = float(pts[:, 0].max() - pts[:, 0].min())
        spread_h = float(pts[:, 1].max() - pts[:, 1].min())
        cell_w, cell_h = rendered_grid_cell(evaluator, component)
        max_w = max(max_w, spread_w + cell_w)
        max_h = max(max_h, spread_h + cell_h)
    slot_centers = pack_points_grid(
        anchor,
        len(components),
        cell_w=max(360.0, max_w + 140.0),
        cell_h=max(230.0, max_h + 90.0),
    )
    index_parts: list[np.ndarray] = []
    target_parts: list[np.ndarray] = []
    for component, slot in zip(components, slot_centers):
        centroid = positions[component].mean(axis=0)
        index_parts.append(component.astype(np.int32, copy=False))
        target_parts.append(positions[component] + (slot - centroid).reshape(1, 2))
    return (
        np.concatenate(index_parts).astype(np.int32),
        np.concatenate(target_parts).astype(np.float64),
    )


def small_component_semantic_cluster_pack_candidates(
    layout: dict,
    positions: np.ndarray,
    evaluator: ExactRelationEvaluator,
    node_counts: np.ndarray,
    raw_edges: np.ndarray,
    cluster_groups: dict[str, np.ndarray],
    *,
    max_nodes: int,
    max_component_size: int,
    max_cluster_size: int,
    min_score: float,
    anchor_scales: list[float],
) -> list[Candidate]:
    """Move small components toward name/app-matching clusters as rigid units."""
    if max_nodes <= 0 or max_component_size <= 0 or not anchor_scales:
        return []
    nodes = layout["nodes"]
    n_nodes = len(nodes)
    model_ids = [str(nd.get("modelId") or "") for nd in nodes]
    node_parts = [v34.semantic_name_parts(model_id) for model_id in model_ids]
    leaf_ids, _bundles = v34.bundle_leaf_index_sets(layout)

    adjacency: list[list[int]] = [[] for _ in range(n_nodes)]
    for s_raw, t_raw in raw_edges:
        s = int(s_raw)
        t = int(t_raw)
        adjacency[s].append(t)
        adjacency[t].append(s)

    seen = np.zeros(n_nodes, dtype=bool)
    components: list[np.ndarray] = []
    for start in range(n_nodes):
        if seen[start]:
            continue
        stack = [start]
        seen[start] = True
        members: list[int] = []
        while stack:
            node = stack.pop()
            members.append(node)
            for nbr in adjacency[node]:
                if not seen[nbr]:
                    seen[nbr] = True
                    stack.append(nbr)
        arr = np.array(sorted(members), dtype=np.int32)
        if arr.size > max_component_size:
            continue
        if any(model_ids[int(idx)] in leaf_ids for idx in arr):
            continue
        components.append(arr)
    if not components:
        return []

    profiles: list[dict] = []
    for key, raw_members in cluster_groups.items():
        members = np.asarray(raw_members, dtype=np.int32)
        if members.size < 2 or members.size > max_cluster_size:
            continue
        if v34.group_is_pseudo(key):
            continue
        token_counts: Counter[str] = Counter()
        app_counts: Counter[str] = Counter()
        for node_raw in members:
            app, tokens = node_parts[int(node_raw)]
            app_counts[app] += 1
            token_counts.update(tokens)
        pts = positions[members]
        spread = max(
            480.0,
            float(pts[:, 0].max() - pts[:, 0].min()),
            float(pts[:, 1].max() - pts[:, 1].min()),
        )
        profiles.append(
            {
                "key": key,
                "members": members,
                "tokens": set(token_counts.keys()),
                "apps": app_counts,
                "size": int(members.size),
                "centroid": pts.mean(axis=0),
                "spread": spread,
            }
        )
    if not profiles:
        return []

    min_x = float(positions[:, 0].min())
    max_x = float(positions[:, 0].max())
    min_y = float(positions[:, 1].min())
    max_y = float(positions[:, 1].max())
    width = max(1.0, max_x - min_x)
    height = max(1.0, max_y - min_y)
    center = np.array([(min_x + max_x) / 2.0, (min_y + max_y) / 2.0], dtype=np.float64)

    rows_by_profile: dict[str, list[tuple[float, float, np.ndarray, dict]]] = {}
    for component in components:
        comp_apps: Counter[str] = Counter()
        comp_tokens: set[str] = set()
        for idx_raw in component:
            app, tokens = node_parts[int(idx_raw)]
            comp_apps[app] += 1
            comp_tokens.update(tokens)
        comp_centroid = positions[component].mean(axis=0)
        edge_pressure = max(
            (comp_centroid[0] - center[0]) / (width / 2.0),
            (center[0] - comp_centroid[0]) / (width / 2.0),
            (comp_centroid[1] - center[1]) / (height / 2.0),
            (center[1] - comp_centroid[1]) / (height / 2.0),
        )
        best: tuple[float, dict] | None = None
        for profile in profiles:
            shared = comp_tokens & profile["tokens"]
            union = comp_tokens | profile["tokens"]
            jaccard = float(len(shared) / len(union)) if union else 0.0
            app_match = 0.0
            for app, count in comp_apps.items():
                app_match += float(count) * float(profile["apps"].get(app, 0)) / float(profile["size"])
            app_match /= float(max(1, int(component.size)))
            score = 1.45 * jaccard + 0.85 * app_match + 0.08 * len(shared)
            if score < min_score:
                continue
            if best is None or score > best[0]:
                best = (score, profile)
        if best is None:
            continue
        pressure_bonus = max(0.0, edge_pressure) * 0.8
        cross_bonus = float(node_counts[component].sum()) / 40.0
        rank_score = best[0] + pressure_bonus + cross_bonus
        rows_by_profile.setdefault(str(best[1]["key"]), []).append(
            (rank_score, best[0], component, best[1])
        )
    if not rows_by_profile:
        return []

    candidates: list[Candidate] = []
    wanted_totals = (4, 8, 16, 32, 64, 96, 128, max_nodes)
    for profile_key, rows in rows_by_profile.items():
        rows.sort(reverse=True, key=lambda item: item[0])
        selected: list[tuple[float, float, np.ndarray, dict]] = []
        total = 0
        emitted: set[int] = set()
        for row in rows:
            component = row[2]
            if total + int(component.size) > max_nodes:
                continue
            selected.append(row)
            total += int(component.size)
            if total <= 0:
                continue
            if not any(total >= want and want not in emitted for want in wanted_totals):
                continue
            emitted.add(total)
            batch = selected.copy()
            components_batch = [item[2] for item in batch]
            profile = batch[0][3]
            cluster_centroid = np.asarray(profile["centroid"], dtype=np.float64)
            source_centroid = np.concatenate(components_batch).astype(np.int32)
            source_center = positions[source_centroid].mean(axis=0)
            direction = unit(source_center - cluster_centroid)
            if direction is None:
                direction = np.array([1.0, 0.0], dtype=np.float64)
            avg_score = float(sum(item[1] for item in batch) / len(batch))
            for scale in anchor_scales:
                anchor = cluster_centroid + direction * (float(profile["spread"]) * float(scale) + 360.0)
                indices, targets = component_pack_targets(
                    positions,
                    evaluator,
                    components_batch,
                    anchor,
                )
                if indices.size == 0:
                    continue
                priority = (
                    avg_score * 14000.0
                    + float(node_counts[indices].sum()) * 45.0
                    + float(indices.size) * 180.0
                )
                candidates.append(
                    Candidate(
                        "small_component_semantic_pack",
                        indices,
                        targets=targets,
                        priority=priority,
                        label=(
                            f"small-component-semantic:{profile_key}:"
                            f"n={indices.size}:score={avg_score:.2f}:scale={scale:.2f}"
                        ),
                    )
                )
            if total >= max_nodes:
                break
    return candidates


def isolated_semantic_pack_candidates(
    layout: dict,
    positions: np.ndarray,
    raw_edges: np.ndarray,
    cluster_groups: dict[str, np.ndarray],
    *,
    max_nodes: int,
    top_clusters: int,
    min_score: float,
    margin: float,
) -> list[Candidate]:
    """Batch-place degree-0 standalone nodes near name-matching clusters.

    A single isolated node move often cannot reduce bbox because many other
    isolated nodes remain on the same outer line.  This candidate moves a
    whole semantic batch, allowing bbox compression to become visible to the
    exact gate in one step.
    """
    if max_nodes <= 0:
        return []
    nodes = layout["nodes"]
    n_nodes = len(nodes)
    model_ids = [str(nd.get("modelId") or "") for nd in nodes]
    node_parts = [v34.semantic_name_parts(model_id) for model_id in model_ids]
    cluster_by_node = [str(nd.get("clusterId") or "") for nd in nodes]
    leaf_ids, _bundles = v34.bundle_leaf_index_sets(layout)
    degree = np.zeros(n_nodes, dtype=np.int32)
    if raw_edges.size:
        np.add.at(degree, raw_edges[:, 0], 1)
        np.add.at(degree, raw_edges[:, 1], 1)

    profiles: list[dict] = []
    for key, raw_members in cluster_groups.items():
        members = np.asarray(raw_members, dtype=np.int32)
        if members.size < 2:
            continue
        token_counts: Counter[str] = Counter()
        app_counts: Counter[str] = Counter()
        for node_raw in members:
            app, tokens = node_parts[int(node_raw)]
            app_counts[app] += 1
            token_counts.update(tokens)
        profiles.append(
            {
                "key": key,
                "members": members,
                "tokens": set(token_counts.keys()),
                "apps": app_counts,
                "size": int(members.size),
                "centroid": positions[members].mean(axis=0),
            }
        )
    if not profiles:
        return []

    isolated: list[int] = []
    center = positions.mean(axis=0)
    for idx, model_id in enumerate(model_ids):
        if model_id in leaf_ids:
            continue
        if degree[idx] != 0:
            continue
        # Only standalone/no-cluster nodes, or singleton cluster nodes that are
        # graph-isolated and therefore visually free to join a better cluster.
        if cluster_by_node[idx]:
            cluster_size = int(np.sum(np.array(cluster_by_node) == cluster_by_node[idx]))
            if cluster_size > 1:
                continue
        isolated.append(idx)
    if not isolated:
        return []

    scored_nodes: list[tuple[float, int, dict, float]] = []
    for idx in isolated:
        app, tokens = node_parts[idx]
        best: tuple[float, dict] | None = None
        for profile in profiles:
            shared = tokens & profile["tokens"]
            union = tokens | profile["tokens"]
            jaccard = float(len(shared) / len(union)) if union else 0.0
            app_match = float(profile["apps"].get(app, 0) / profile["size"])
            score = 1.55 * jaccard + 0.85 * app_match + 0.08 * len(shared)
            if score < min_score:
                continue
            if best is None or score > best[0]:
                best = (score, profile)
        if best is None:
            continue
        dist = float(np.linalg.norm(positions[idx] - center))
        scored_nodes.append((dist, idx, best[1], best[0]))
    scored_nodes.sort(reverse=True, key=lambda item: item[0])

    by_cluster: dict[str, list[tuple[int, float, dict]]] = {}
    for _dist, idx, profile, score in scored_nodes[:max_nodes]:
        by_cluster.setdefault(str(profile["key"]), []).append((idx, score, profile))

    candidates: list[Candidate] = []
    for cluster_key, rows in by_cluster.items():
        if not rows:
            continue
        profile = rows[0][2]
        rows.sort(reverse=True, key=lambda item: item[1])
        # Build cumulative batches so the gate can choose how aggressive the
        # isolated-node participation should be.
        batch_sizes = sorted(
            {
                min(len(rows), value)
                for value in (4, 8, 16, 32, 64, 96, len(rows))
                if value > 0
            }
        )
        for batch_size in batch_sizes:
            batch = rows[:batch_size]
            indices = np.array([idx for idx, _score, _profile in batch], dtype=np.int32)
            if indices.size == 0:
                continue
            # Put isolated nodes just outside the matched cluster, on the side
            # facing their current average position. This preserves the visual
            # clue that they are peripheral while removing the huge bbox strip.
            source_centroid = positions[indices].mean(axis=0)
            cluster_centroid = np.asarray(profile["centroid"], dtype=np.float64)
            direction = unit(source_centroid - cluster_centroid)
            if direction is None:
                direction = np.array([1.0, 0.0], dtype=np.float64)
            members = np.asarray(profile["members"], dtype=np.int32)
            cluster_pts = positions[members]
            spread = max(
                600.0,
                float(cluster_pts[:, 0].max() - cluster_pts[:, 0].min()),
                float(cluster_pts[:, 1].max() - cluster_pts[:, 1].min()),
            )
            anchor = cluster_centroid + direction * (spread * 0.65 + margin)
            targets = pack_points_grid(anchor, int(indices.size))
            avg_score = float(sum(score for _idx, score, _profile in batch) / len(batch))
            candidates.append(
                Candidate(
                    "isolated_semantic_cluster_pack",
                    indices,
                    targets=targets,
                    priority=avg_score * 12000.0 + float(indices.size) * 150.0,
                    label=f"isolated-pack:{cluster_key}:n={indices.size}:score={avg_score:.2f}",
                )
            )
    # One fallback app-level pack keeps fully unmatched no-edge models from
    # stretching the canvas forever while still grouping by model namespace.
    unmatched = [
        idx
        for idx in isolated
        if not any(idx == row[1] for row in scored_nodes[:max_nodes])
    ]
    if unmatched:
        app_groups: dict[str, list[int]] = {}
        for idx in unmatched[:max_nodes]:
            app, _tokens = node_parts[idx]
            app_groups.setdefault(app, []).append(idx)
        graph_center = positions.mean(axis=0)
        app_order = sorted(app_groups.items(), key=lambda item: len(item[1]), reverse=True)
        for app_rank, (app, members) in enumerate(app_order[:top_clusters]):
            indices = np.array(members[:96], dtype=np.int32)
            angle = (app_rank / max(1, min(top_clusters, len(app_order)))) * math.tau
            anchor = graph_center + np.array([math.cos(angle), math.sin(angle)]) * 9000.0
            targets = pack_points_grid(anchor, int(indices.size))
            candidates.append(
                Candidate(
                    "isolated_app_pack",
                    indices,
                    targets=targets,
                    priority=float(indices.size) * 100.0,
                    label=f"isolated-app-pack:{app}:n={indices.size}",
                )
            )
    return candidates


def isolated_boundary_catalog_pack_candidates(
    layout: dict,
    positions: np.ndarray,
    raw_edges: np.ndarray,
    *,
    max_nodes: int,
    min_boundary_pressure: float,
) -> list[Candidate]:
    """Compact graph-isolated outer nodes into an app/name grouped catalog area."""
    if max_nodes <= 0:
        return []
    nodes = layout["nodes"]
    n_nodes = len(nodes)
    model_ids = [str(nd.get("modelId") or "") for nd in nodes]
    node_parts = [v34.semantic_name_parts(model_id) for model_id in model_ids]
    leaf_ids, _bundles = v34.bundle_leaf_index_sets(layout)
    degree = np.zeros(n_nodes, dtype=np.int32)
    if raw_edges.size:
        np.add.at(degree, raw_edges[:, 0], 1)
        np.add.at(degree, raw_edges[:, 1], 1)

    min_x = float(positions[:, 0].min())
    max_x = float(positions[:, 0].max())
    min_y = float(positions[:, 1].min())
    max_y = float(positions[:, 1].max())
    width = max(1.0, max_x - min_x)
    height = max(1.0, max_y - min_y)
    center = np.array([(min_x + max_x) / 2.0, (min_y + max_y) / 2.0], dtype=np.float64)

    ranked: list[tuple[float, int]] = []
    for idx, model_id in enumerate(model_ids):
        if model_id in leaf_ids or degree[idx] != 0:
            continue
        x = float(positions[idx, 0])
        y = float(positions[idx, 1])
        edge_pressure = max(
            (x - center[0]) / (width / 2.0),
            (center[0] - x) / (width / 2.0),
            (y - center[1]) / (height / 2.0),
            (center[1] - y) / (height / 2.0),
        )
        if edge_pressure < min_boundary_pressure:
            continue
        dist = float(np.linalg.norm(positions[idx] - center))
        ranked.append((edge_pressure * 100000.0 + dist, idx))
    if not ranked:
        return []
    ranked.sort(reverse=True, key=lambda item: item[0])

    selected = [idx for _score, idx in ranked[:max_nodes]]

    def semantic_sort_key(idx: int) -> tuple[str, str, str]:
        app, tokens = node_parts[idx]
        token = sorted(tokens)[0] if tokens else ""
        return (app, token, model_ids[idx])

    selected.sort(key=semantic_sort_key)
    anchors = [
        np.array([min_x + width * 0.72, min_y + height * 0.78], dtype=np.float64),
        np.array([min_x + width * 0.28, min_y + height * 0.78], dtype=np.float64),
        np.array([min_x + width * 0.72, min_y + height * 0.24], dtype=np.float64),
        np.array([min_x + width * 0.28, min_y + height * 0.24], dtype=np.float64),
        center,
    ]
    batch_sizes = sorted(
        {
            min(len(selected), value)
            for value in (32, 64, 128, 192, 256, len(selected))
            if value > 0
        }
    )
    candidates: list[Candidate] = []
    for batch_size in batch_sizes:
        indices = np.array(selected[:batch_size], dtype=np.int32)
        if indices.size == 0:
            continue
        for anchor_idx, anchor in enumerate(anchors):
            targets = pack_points_grid(anchor, int(indices.size), cell_w=430.0, cell_h=245.0)
            candidates.append(
                Candidate(
                    "isolated_boundary_catalog_pack",
                    indices,
                    targets=targets,
                    priority=float(indices.size) * 260.0 + float(batch_size),
                    label=f"isolated-boundary-pack:n={indices.size}:anchor={anchor_idx}",
                )
            )
    return candidates


def small_component_boundary_pack_candidates(
    layout: dict,
    positions: np.ndarray,
    raw_edges: np.ndarray,
    *,
    max_nodes: int,
    max_component_size: int,
    min_boundary_pressure: float,
) -> list[Candidate]:
    """Pack small disconnected peripheral components into a compact catalog."""
    if max_nodes <= 0 or max_component_size <= 0:
        return []
    nodes = layout["nodes"]
    n_nodes = len(nodes)
    model_ids = [str(nd.get("modelId") or "") for nd in nodes]
    node_parts = [v34.semantic_name_parts(model_id) for model_id in model_ids]
    leaf_ids, _bundles = v34.bundle_leaf_index_sets(layout)
    adjacency: list[list[int]] = [[] for _ in range(n_nodes)]
    for s_raw, t_raw in raw_edges:
        s = int(s_raw)
        t = int(t_raw)
        adjacency[s].append(t)
        adjacency[t].append(s)

    seen = np.zeros(n_nodes, dtype=bool)
    components: list[np.ndarray] = []
    for start in range(n_nodes):
        if seen[start]:
            continue
        stack = [start]
        seen[start] = True
        members: list[int] = []
        while stack:
            node = stack.pop()
            members.append(node)
            for nbr in adjacency[node]:
                if not seen[nbr]:
                    seen[nbr] = True
                    stack.append(nbr)
        arr = np.array(sorted(members), dtype=np.int32)
        if arr.size <= 1 or arr.size > max_component_size:
            continue
        if any(model_ids[int(idx)] in leaf_ids for idx in arr):
            continue
        components.append(arr)
    if not components:
        return []

    min_x = float(positions[:, 0].min())
    max_x = float(positions[:, 0].max())
    min_y = float(positions[:, 1].min())
    max_y = float(positions[:, 1].max())
    width = max(1.0, max_x - min_x)
    height = max(1.0, max_y - min_y)
    center = np.array([(min_x + max_x) / 2.0, (min_y + max_y) / 2.0], dtype=np.float64)

    ranked_components: list[tuple[float, np.ndarray]] = []
    for component in components:
        centroid = positions[component].mean(axis=0)
        edge_pressure = max(
            (centroid[0] - center[0]) / (width / 2.0),
            (center[0] - centroid[0]) / (width / 2.0),
            (centroid[1] - center[1]) / (height / 2.0),
            (center[1] - centroid[1]) / (height / 2.0),
        )
        if edge_pressure < min_boundary_pressure:
            continue
        dist = float(np.linalg.norm(centroid - center))
        ranked_components.append((edge_pressure * 100000.0 + dist, component))
    if not ranked_components:
        return []

    def component_key(component: np.ndarray) -> tuple[str, str, str]:
        apps: Counter[str] = Counter()
        tokens: Counter[str] = Counter()
        names: list[str] = []
        for idx_raw in component:
            idx = int(idx_raw)
            app, got_tokens = node_parts[idx]
            apps[app] += 1
            tokens.update(got_tokens)
            names.append(model_ids[idx])
        app = apps.most_common(1)[0][0] if apps else ""
        token = tokens.most_common(1)[0][0] if tokens else ""
        return (app, token, "|".join(sorted(names)))

    ranked_components.sort(reverse=True, key=lambda item: item[0])
    ordered_components = [component for _score, component in ranked_components]
    ordered_components.sort(key=component_key)

    anchors = [
        np.array([min_x + width * 0.70, min_y + height * 0.72], dtype=np.float64),
        np.array([min_x + width * 0.30, min_y + height * 0.72], dtype=np.float64),
        np.array([min_x + width * 0.70, min_y + height * 0.30], dtype=np.float64),
        np.array([min_x + width * 0.30, min_y + height * 0.30], dtype=np.float64),
        center,
    ]
    candidates: list[Candidate] = []
    selected_components: list[np.ndarray] = []
    total = 0
    for component in ordered_components:
        if total + int(component.size) > max_nodes:
            continue
        selected_components.append(component)
        total += int(component.size)
        if total >= max_nodes:
            break
    if not selected_components:
        return []

    prefix_components: list[np.ndarray] = []
    prefix_total = 0
    wanted_totals = {16, 32, 64, 96, 128, 192, max_nodes}
    emitted_totals: set[int] = set()
    for component in selected_components:
        prefix_components.append(component)
        prefix_total += int(component.size)
        if not any(prefix_total >= want and want not in emitted_totals for want in wanted_totals):
            continue
        emit_total = prefix_total
        emitted_totals.add(emit_total)
        indices = np.concatenate(prefix_components).astype(np.int32)
        for anchor_idx, anchor in enumerate(anchors):
            targets = pack_points_grid(anchor, int(indices.size), cell_w=440.0, cell_h=260.0)
            candidates.append(
                Candidate(
                    "small_component_boundary_pack",
                    indices,
                    targets=targets,
                    priority=float(indices.size) * 230.0,
                    label=f"small-component-pack:n={indices.size}:anchor={anchor_idx}",
                )
            )
    return candidates


def truncate_candidates(
    candidates: list[Candidate],
    max_candidates: int,
    per_kind: int,
) -> list[Candidate]:
    candidates = dedupe_candidates(candidates)
    candidates.sort(key=lambda cand: cand.priority, reverse=True)
    if per_kind > 0:
        counts: dict[str, int] = {}
        kept: list[Candidate] = []
        for cand in candidates:
            got = counts.get(cand.kind, 0)
            if got >= per_kind:
                continue
            counts[cand.kind] = got + 1
            kept.append(cand)
        candidates = kept
    if max_candidates > 0:
        candidates = candidates[:max_candidates]
    return candidates


def run_search(args: argparse.Namespace) -> tuple[Metrics, np.ndarray]:
    layout = json.loads(args.layout.read_text())
    positions = (
        v34.read_positions_tsv(args.positions, layout)
        if args.positions is not None
        else v34.layout_positions(layout)
    )
    if args.port_json is not None:
        evaluator = FixedPortRelationEvaluator(
            layout,
            positions,
            json.loads(args.port_json.read_text()),
            overlap_margin=args.overlap_margin,
            edge_rect_margin=args.edge_rect_margin,
            overlap_weight=args.overlap_weight,
            bbox_weight=args.bbox_weight,
            bbox_target_b=args.bbox_target_b,
        )
    else:
        evaluator = ExactRelationEvaluator(
            layout,
            overlap_margin=args.overlap_margin,
            edge_rect_margin=args.edge_rect_margin,
            overlap_weight=args.overlap_weight,
            bbox_weight=args.bbox_weight,
            bbox_target_b=args.bbox_target_b,
        )
    metrics, state = evaluator.measure(positions)
    cluster_groups = v34.cluster_members(layout)
    groups = dict(cluster_groups)
    groups.update(v34.no_cluster_pseudo_groups(layout, evaluator.raw_edges))
    groups_by_node = build_groups_by_node(
        groups,
        positions.shape[0],
        args.endpoint_orbit_max_group_size,
    )
    print(
        f"initial exactEdgeCross={metrics.edge_cross} "
        f"edgeRect={metrics.edge_rect} overlaps={metrics.overlaps} "
        f"visualCross={metrics.visual_cross} bbox={metrics.bbox_b:.2f}B "
        f"score={metrics.score:.1f} visibleEdges={evaluator.edge_count}",
        flush=True,
    )
    best_metrics = metrics
    best_positions = positions.copy()
    started = time.time()
    accepted = 0
    steps = parse_steps(args.steps)
    edge_rect_steps = (
        parse_steps(args.edge_rect_steps)
        if args.edge_rect_steps.strip()
        else steps
    )
    orbit_radii = parse_steps(args.bundle_orbit_scales)
    hub_spoke_radii = parse_steps(args.hub_spoke_radii)
    semantic_radii = parse_steps(args.semantic_cluster_radii)
    semantic_tighten_factors = parse_steps(args.semantic_tighten_factors)
    semantic_family_pull_factors = parse_steps(args.semantic_family_pull_factors)
    semantic_family_scale_factors = parse_steps(args.semantic_family_scale_factors)
    boundary_cluster_pull_factors = parse_steps(args.boundary_cluster_pull_factors)
    boundary_cluster_scale_factors = parse_steps(args.boundary_cluster_scale_factors)
    small_component_semantic_scales = parse_steps(args.small_component_semantic_scales)
    compress_scales = parse_scale_pairs(args.compress_scales)
    anneal_rng = random.Random(args.anneal_seed)

    for round_idx in range(args.rounds):
        edge_counts, node_counts, _pair_i, _pair_j = evaluator.crossing_incidents(state)
        candidates = []
        candidates.extend(
            global_compression_candidates(
                positions,
                compress_scales,
            )
        )
        candidates.extend(
            boundary_cluster_pull_candidates(
                layout,
                positions,
                evaluator,
                node_counts,
                cluster_groups,
                max_groups=args.boundary_cluster_groups,
                min_group_size=args.boundary_cluster_min_size,
                max_group_size=args.boundary_cluster_max_size,
                min_boundary_pressure=args.boundary_cluster_min_pressure,
                pull_factors=boundary_cluster_pull_factors,
                scale_factors=boundary_cluster_scale_factors,
            )
        )
        candidates.extend(
            global_semantic_family_candidates(
                layout,
                positions,
                evaluator,
                node_counts,
                max_groups=args.semantic_family_groups,
                min_group_size=args.semantic_family_min_size,
                max_group_size=args.semantic_family_max_size,
                min_boundary_pressure=args.semantic_family_min_pressure,
                pull_factors=semantic_family_pull_factors,
                scale_factors=semantic_family_scale_factors,
            )
        )
        candidates.extend(
            semantic_cluster_tighten_candidates(
                layout,
                positions,
                evaluator,
                node_counts,
                cluster_groups,
                max_groups=args.semantic_tighten_groups,
                min_group_size=args.semantic_tighten_min_size,
                max_group_size=args.semantic_tighten_max_size,
                max_cluster_size=args.semantic_tighten_max_cluster_size,
                pull_factors=semantic_tighten_factors,
            )
        )
        candidates.extend(
            small_component_semantic_cluster_pack_candidates(
                layout,
                positions,
                evaluator,
                node_counts,
                evaluator.raw_edges,
                cluster_groups,
                max_nodes=args.small_component_semantic_nodes,
                max_component_size=args.small_component_semantic_max_size,
                max_cluster_size=args.small_component_semantic_max_cluster_size,
                min_score=args.small_component_semantic_min_score,
                anchor_scales=small_component_semantic_scales,
            )
        )
        candidates.extend(
            semantic_cluster_participation_candidates(
                layout,
                positions,
                node_counts,
                evaluator.raw_edges,
                cluster_groups,
                max_nodes=args.semantic_cluster_nodes,
                top_clusters_per_node=args.semantic_cluster_top_clusters,
                singleton_max_size=args.semantic_cluster_singleton_max_size,
                max_cluster_size=args.semantic_cluster_max_cluster_size,
                min_score=args.semantic_cluster_min_score,
                radii=semantic_radii,
            )
        )
        candidates.extend(
            isolated_semantic_pack_candidates(
                layout,
                positions,
                evaluator.raw_edges,
                cluster_groups,
                max_nodes=args.isolated_pack_nodes,
                top_clusters=args.isolated_pack_top_clusters,
                min_score=args.isolated_pack_min_score,
                margin=args.isolated_pack_margin,
            )
        )
        candidates.extend(
            isolated_boundary_catalog_pack_candidates(
                layout,
                positions,
                evaluator.raw_edges,
                max_nodes=args.isolated_boundary_pack_nodes,
                min_boundary_pressure=args.isolated_boundary_min_pressure,
            )
        )
        candidates.extend(
            small_component_boundary_pack_candidates(
                layout,
                positions,
                evaluator.raw_edges,
                max_nodes=args.small_component_pack_nodes,
                max_component_size=args.small_component_pack_max_size,
                min_boundary_pressure=args.small_component_min_pressure,
            )
        )
        candidates.extend(
            visual_crossing_candidates(
                evaluator,
                positions,
                state,
                edge_counts,
                args.cross_pairs,
                args.hot_edges,
                steps,
                orbit_radii,
            )
        )
        candidates.extend(
            visual_group_fan_candidates(
                evaluator,
                positions,
                state,
                edge_counts,
                node_counts,
                groups,
                args.group_fan_groups,
                args.group_fan_max_size,
                steps,
            )
        )
        candidates.extend(
            visual_hub_spoke_pack_candidates(
                evaluator,
                positions,
                state,
                edge_counts,
                node_counts,
                max_hubs=args.hub_spoke_hubs,
                max_spokes_per_hub=args.hub_spoke_spokes,
                max_spoke_nodes=args.hub_spoke_max_spoke_nodes,
                radii=hub_spoke_radii,
            )
        )
        candidates.extend(
            visual_hot_edge_group_anchor_candidates(
                evaluator,
                positions,
                state,
                edge_counts,
                node_counts,
                groups_by_node,
                args.hot_edge_anchor_edges,
                args.hot_edge_anchor_group_options,
                args.hot_edge_anchor_max_total_nodes,
                parse_steps(args.hot_edge_anchor_radii),
            )
        )
        candidates.extend(
            visual_endpoint_orbit_candidates(
                evaluator,
                positions,
                edge_counts,
                node_counts,
                groups_by_node,
                args.endpoint_orbit_edges,
                args.endpoint_orbit_group_options,
                parse_steps(args.endpoint_orbit_radii),
            )
        )
        candidates.extend(
            visual_endpoint_swap_candidates(
                evaluator,
                positions,
                state,
                edge_counts,
                args.endpoint_swap_pairs,
            )
        )
        candidates.extend(
            visual_group_swap_candidates(
                evaluator,
                positions,
                state,
                edge_counts,
                node_counts,
                groups_by_node,
                max_pairs=args.group_swap_pairs,
                max_group_options=args.group_swap_options,
                max_total_nodes=args.group_swap_max_total_nodes,
            )
        )
        candidates.extend(
            edge_rect_corridor_clear_candidates(
                evaluator,
                positions,
                state,
                edge_counts,
                node_counts,
                max_edges=args.edge_rect_corridor_edges,
                max_blockers_per_edge=args.edge_rect_corridor_blockers,
                pads=parse_steps(args.edge_rect_corridor_pads),
            )
        )
        candidates.extend(
            edge_rect_force_relax_candidates(
                evaluator,
                positions,
                state,
                edge_counts,
                node_counts,
                max_hits=args.edge_rect_force_hits,
                max_nodes=args.edge_rect_force_nodes,
                scales=parse_steps(args.edge_rect_force_scales),
                padding=args.edge_rect_force_padding,
            )
        )
        candidates.extend(
            edge_rect_relief_candidates(
                evaluator,
                positions,
                state,
                edge_counts,
                node_counts,
                args.edge_rect_hits,
                edge_rect_steps,
                args.edge_rect_neighborhood_max_nodes,
            )
        )
        candidates = truncate_candidates(candidates, args.max_candidates, args.per_kind_candidates)
        if not candidates:
            print(f"round {round_idx + 1}: no candidates", flush=True)
            break

        best_cand: Candidate | None = None
        best_cand_metrics: Metrics | None = None
        anneal_pool: list[tuple[float, Candidate, Metrics]] = []
        measured = 0
        for cand in candidates:
            moved = move_positions(positions, cand)
            cand_metrics = evaluator.measure_candidate(
                positions,
                moved,
                cand.nodes,
                metrics,
                state,
            )
            measured += 1
            if cand_metrics.visual_cross > metrics.visual_cross + args.max_visual_worsen:
                continue
            gain_here = metrics.score - cand_metrics.score
            if (
                args.anneal_temp > 0.0
                and gain_here >= -args.anneal_worsen_score
                and cand_metrics.overlaps <= metrics.overlaps + args.anneal_max_overlap_worsen
            ):
                anneal_pool.append((gain_here, cand, cand_metrics))
            if best_cand_metrics is None or cand_metrics.score < best_cand_metrics.score:
                best_cand = cand
                best_cand_metrics = cand_metrics

        if best_cand is None or best_cand_metrics is None:
            print(
                f"round {round_idx + 1}: stop no guarded candidate "
                f"measured={measured} visualCross={metrics.visual_cross}",
                flush=True,
            )
            break
        gain = metrics.score - best_cand_metrics.score
        if gain < args.min_gain:
            temp = max(
                args.anneal_min_temp,
                args.anneal_temp
                * (args.anneal_decay ** max(0, accepted)),
            )
            chosen: tuple[float, Candidate, Metrics] | None = None
            if temp > 0.0 and anneal_pool:
                weights = [math.exp(max(-60.0, min(60.0, gain_here / temp))) for gain_here, _cand, _metric in anneal_pool]
                total = sum(weights)
                if total > 0.0:
                    pick = anneal_rng.random() * total
                    cursor = 0.0
                    for row, weight in zip(anneal_pool, weights):
                        cursor += weight
                        if cursor >= pick:
                            chosen = row
                            break
            if chosen is None:
                print(
                    f"round {round_idx + 1}: stop gain={gain:.2f} "
                    f"bestKind={best_cand.kind} measured={measured} "
                    f"visualCross={metrics.visual_cross}",
                    flush=True,
                )
                break
            gain, best_cand, best_cand_metrics = chosen
            print(
                f"round {round_idx + 1}: anneal {best_cand.kind} "
                f"nodes={best_cand.nodes.size} gain={gain:.1f} "
                f"temp={temp:.2f} edgeCross={best_cand_metrics.edge_cross} "
                f"edgeRect={best_cand_metrics.edge_rect} overlaps={best_cand_metrics.overlaps} "
                f"visualCross={best_cand_metrics.visual_cross} measured={measured}",
                flush=True,
            )

        positions = move_positions(positions, best_cand)
        metrics, state = evaluator.measure(positions)
        accepted += 1
        if metrics.score < best_metrics.score:
            best_metrics = metrics
            best_positions = positions.copy()
            v34.write_positions_tsv(args.out_tsv, layout, best_positions)
        print(
            f"round {round_idx + 1}: accept {best_cand.kind} "
            f"nodes={best_cand.nodes.size} gain={gain:.1f} "
            f"edgeCross={metrics.edge_cross} edgeRect={metrics.edge_rect} "
            f"overlaps={metrics.overlaps} visualCross={metrics.visual_cross} "
            f"bbox={metrics.bbox_b:.2f}B measured={measured}",
            flush=True,
        )
        if metrics.visual_cross <= args.target_visual_cross:
            break

    v34.write_positions_tsv(args.out_tsv, layout, best_positions)
    elapsed = time.time() - started
    print(
        f"done accepted={accepted} elapsed={elapsed:.1f}s "
        f"best edgeCross={best_metrics.edge_cross} edgeRect={best_metrics.edge_rect} "
        f"overlaps={best_metrics.overlaps} visualCross={best_metrics.visual_cross} "
        f"bbox={best_metrics.bbox_b:.2f}B out={args.out_tsv}",
        flush=True,
    )
    return best_metrics, best_positions


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--positions", type=Path, default=None)
    parser.add_argument("--port-json", type=Path, default=None)
    parser.add_argument("--out-tsv", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=20)
    parser.add_argument("--target-visual-cross", type=int, default=500)
    parser.add_argument("--cross-pairs", type=int, default=320)
    parser.add_argument("--hot-edges", type=int, default=120)
    parser.add_argument("--edge-rect-hits", type=int, default=120)
    parser.add_argument("--edge-rect-neighborhood-max-nodes", type=int, default=0)
    parser.add_argument("--edge-rect-corridor-edges", type=int, default=0)
    parser.add_argument("--edge-rect-corridor-blockers", type=int, default=12)
    parser.add_argument("--edge-rect-corridor-pads", default="0,60,140,260")
    parser.add_argument("--edge-rect-force-hits", type=int, default=0)
    parser.add_argument("--edge-rect-force-nodes", type=int, default=96)
    parser.add_argument("--edge-rect-force-scales", default="0.35,0.65,1.0,1.35")
    parser.add_argument("--edge-rect-force-padding", type=float, default=42.0)
    parser.add_argument("--group-fan-groups", type=int, default=120)
    parser.add_argument("--group-fan-max-size", type=int, default=140)
    parser.add_argument("--hub-spoke-hubs", type=int, default=0)
    parser.add_argument("--hub-spoke-spokes", type=int, default=14)
    parser.add_argument("--hub-spoke-max-spoke-nodes", type=int, default=24)
    parser.add_argument("--hub-spoke-radii", default="480,800,1250,1900,2800")
    parser.add_argument("--endpoint-orbit-edges", type=int, default=80)
    parser.add_argument("--endpoint-orbit-group-options", type=int, default=2)
    parser.add_argument("--endpoint-orbit-max-group-size", type=int, default=120)
    parser.add_argument("--endpoint-orbit-radii", default="0,180,360,700,1200,2000,3200,5000")
    parser.add_argument("--endpoint-swap-pairs", type=int, default=80)
    parser.add_argument("--group-swap-pairs", type=int, default=0)
    parser.add_argument("--group-swap-options", type=int, default=3)
    parser.add_argument("--group-swap-max-total-nodes", type=int, default=220)
    parser.add_argument("--hot-edge-anchor-edges", type=int, default=70)
    parser.add_argument("--hot-edge-anchor-group-options", type=int, default=2)
    parser.add_argument("--hot-edge-anchor-max-total-nodes", type=int, default=180)
    parser.add_argument("--hot-edge-anchor-radii", default="0,250,600,1100,1900,3200")
    parser.add_argument("--semantic-cluster-nodes", type=int, default=0)
    parser.add_argument("--semantic-cluster-top-clusters", type=int, default=3)
    parser.add_argument("--semantic-cluster-singleton-max-size", type=int, default=1)
    parser.add_argument("--semantic-cluster-max-cluster-size", type=int, default=180)
    parser.add_argument("--semantic-cluster-min-score", type=float, default=0.32)
    parser.add_argument("--semantic-cluster-radii", default="0,160,360,700,1200")
    parser.add_argument("--semantic-tighten-groups", type=int, default=0)
    parser.add_argument("--semantic-tighten-min-size", type=int, default=3)
    parser.add_argument("--semantic-tighten-max-size", type=int, default=36)
    parser.add_argument("--semantic-tighten-max-cluster-size", type=int, default=220)
    parser.add_argument("--semantic-tighten-factors", default="0.15,0.35,0.55")
    parser.add_argument("--semantic-family-groups", type=int, default=0)
    parser.add_argument("--semantic-family-min-size", type=int, default=3)
    parser.add_argument("--semantic-family-max-size", type=int, default=70)
    parser.add_argument("--semantic-family-min-pressure", type=float, default=0.34)
    parser.add_argument("--semantic-family-pull-factors", default="0.78,0.64,0.50")
    parser.add_argument("--semantic-family-scale-factors", default="0.88,0.76")
    parser.add_argument("--boundary-cluster-groups", type=int, default=0)
    parser.add_argument("--boundary-cluster-min-size", type=int, default=2)
    parser.add_argument("--boundary-cluster-max-size", type=int, default=90)
    parser.add_argument("--boundary-cluster-min-pressure", type=float, default=0.30)
    parser.add_argument("--boundary-cluster-pull-factors", default="0.82,0.68,0.54")
    parser.add_argument("--boundary-cluster-scale-factors", default="0.90,0.80")
    parser.add_argument("--isolated-pack-nodes", type=int, default=0)
    parser.add_argument("--isolated-pack-top-clusters", type=int, default=8)
    parser.add_argument("--isolated-pack-min-score", type=float, default=0.24)
    parser.add_argument("--isolated-pack-margin", type=float, default=900.0)
    parser.add_argument("--isolated-boundary-pack-nodes", type=int, default=0)
    parser.add_argument("--isolated-boundary-min-pressure", type=float, default=0.74)
    parser.add_argument("--small-component-semantic-nodes", type=int, default=0)
    parser.add_argument("--small-component-semantic-max-size", type=int, default=4)
    parser.add_argument("--small-component-semantic-max-cluster-size", type=int, default=220)
    parser.add_argument("--small-component-semantic-min-score", type=float, default=0.26)
    parser.add_argument("--small-component-semantic-scales", default="0.15,0.32,0.52")
    parser.add_argument("--small-component-pack-nodes", type=int, default=0)
    parser.add_argument("--small-component-pack-max-size", type=int, default=4)
    parser.add_argument("--small-component-min-pressure", type=float, default=0.68)
    parser.add_argument(
        "--compress-scales",
        default="",
        help="comma-separated sx:sy or uniform scale values for global space compression",
    )
    parser.add_argument("--steps", default="120,240,420,700,1100,1700,2600")
    parser.add_argument("--edge-rect-steps", default="")
    parser.add_argument("--bundle-orbit-scales", default="0.55,0.75,1.0,1.3,1.7")
    parser.add_argument("--max-candidates", type=int, default=2600)
    parser.add_argument("--per-kind-candidates", type=int, default=800)
    parser.add_argument("--min-gain", type=float, default=1.0)
    parser.add_argument("--max-visual-worsen", type=int, default=1_000_000)
    parser.add_argument("--anneal-temp", type=float, default=0.0)
    parser.add_argument("--anneal-min-temp", type=float, default=0.0)
    parser.add_argument("--anneal-decay", type=float, default=0.92)
    parser.add_argument("--anneal-worsen-score", type=float, default=8.0)
    parser.add_argument("--anneal-max-overlap-worsen", type=int, default=0)
    parser.add_argument("--anneal-seed", type=int, default=35)
    parser.add_argument("--overlap-margin", type=float, default=0.0)
    parser.add_argument("--edge-rect-margin", type=float, default=0.0)
    parser.add_argument("--overlap-weight", type=float, default=80.0)
    parser.add_argument("--bbox-weight", type=float, default=45.0)
    parser.add_argument("--bbox-target-b", type=float, default=3.75)
    args = parser.parse_args()
    run_search(args)


if __name__ == "__main__":
    main()
