#!/usr/bin/env python3
"""Optimize exact relation endpoint ports and render a zoomable HTML view.

This keeps edges as straight segments connected to rendered node/bundle boxes.
Only the boundary port on each box is changed, so it is not a polyline detour.
"""

from __future__ import annotations

import argparse
import html
import importlib.util
import json
import math
import random
import sys
import time
from pathlib import Path

import numpy as np

SPEC = importlib.util.spec_from_file_location(
    "v35_exact_relation_search",
    Path(__file__).parent / "v35_exact_relation_search.py",
)
exact = importlib.util.module_from_spec(SPEC)
sys.modules["v35_exact_relation_search"] = exact
assert SPEC.loader is not None
SPEC.loader.exec_module(exact)


def esc(value: object) -> str:
    return html.escape(str(value), quote=True)


def rect_id_for(evaluator: exact.ExactRelationEvaluator, rect_idx: int) -> str:
    node_idx = int(evaluator.geometry.rect_node[rect_idx])
    if node_idx >= 0:
        return evaluator.model_ids[node_idx]
    for bundle_idx, got_rect in evaluator.bundle_rect_by_index.items():
        if int(got_rect) == int(rect_idx):
            return f"leaf-bundle-{bundle_idx + 1}"
    return f"leaf-bundle-{rect_idx}"


def rect_label(model_id: str) -> str:
    if model_id.startswith("leaf-bundle-"):
        return model_id
    return model_id.rsplit(".", 1)[-1]


def boundary_options(
    center: np.ndarray,
    width: float,
    height: float,
    toward: np.ndarray,
    slots: int,
) -> np.ndarray:
    """Return candidate ports on facing sides of a rectangle."""
    dx = float(toward[0] - center[0])
    dy = float(toward[1] - center[1])
    sides: list[str] = []
    if abs(dx) >= 1e-9:
        sides.append("right" if dx > 0 else "left")
    if abs(dy) >= 1e-9:
        sides.append("bottom" if dy > 0 else "top")
    if not sides:
        sides.append("right")

    fractions = np.unique(
        np.concatenate(
            [
                np.array([0.0, 0.02, 0.98, 1.0], dtype=np.float64),
                np.linspace(0.08, 0.92, max(2, slots), dtype=np.float64),
            ]
        )
    )
    points: list[np.ndarray] = []
    baseline = exact.rect_port(center, width, height, toward)
    points.append(baseline)
    for side in sides:
        for frac in fractions:
            if side == "left":
                points.append(np.array([center[0] - width / 2.0, center[1] + (frac - 0.5) * height]))
            elif side == "right":
                points.append(np.array([center[0] + width / 2.0, center[1] + (frac - 0.5) * height]))
            elif side == "top":
                points.append(np.array([center[0] + (frac - 0.5) * width, center[1] - height / 2.0]))
            else:
                points.append(np.array([center[0] + (frac - 0.5) * width, center[1] + height / 2.0]))

    dedup: list[np.ndarray] = []
    for point in points:
        if not any(float(np.linalg.norm(point - old)) < 0.5 for old in dedup):
            dedup.append(point.astype(np.float64))
    return np.vstack(dedup)


def cross_flags_for_edge(
    edge_idx: int,
    segment: np.ndarray,
    segments: np.ndarray,
    pair_i: np.ndarray,
    pair_j: np.ndarray,
    pair_mask: np.ndarray,
) -> np.ndarray:
    ii = pair_i[pair_mask]
    jj = pair_j[pair_mask]
    if ii.size == 0:
        return np.zeros(0, dtype=bool)
    left = segments[ii].copy()
    right = segments[jj].copy()
    left[ii == edge_idx] = segment
    right[jj == edge_idx] = segment
    temp = np.concatenate([left, right], axis=0)
    left_idx = np.arange(left.shape[0], dtype=np.int32)
    right_idx = left_idx + left.shape[0]
    return exact.pair_cross_flags(temp, left_idx, right_idx)


def edge_rect_flags_for_edge(
    edge_idx: int,
    segment: np.ndarray,
    rect_positions: np.ndarray,
    rect_widths: np.ndarray,
    rect_heights: np.ndarray,
    pair_edge: np.ndarray,
    pair_rect: np.ndarray,
    pair_mask: np.ndarray,
    margin: float,
) -> np.ndarray:
    rects = pair_rect[pair_mask]
    if rects.size == 0:
        return np.zeros(0, dtype=bool)
    segs = np.repeat(segment.reshape(1, 2, 2), rects.size, axis=0)
    local_edges = np.arange(rects.size, dtype=np.int32)
    return exact.edge_rect_flags(
        segs,
        rect_positions,
        rect_widths,
        rect_heights,
        local_edges,
        rects,
        margin,
    )


class PortOptimizer:
    def __init__(
        self,
        evaluator: exact.ExactRelationEvaluator,
        positions: np.ndarray,
        *,
        slots: int,
    ) -> None:
        self.evaluator = evaluator
        self.positions = positions
        self.rect_positions = evaluator.rect_positions(positions)
        self.rect_widths = evaluator.geometry.rect_widths
        self.rect_heights = evaluator.geometry.rect_heights
        self.edge_pair_i = evaluator.edge_pair_i
        self.edge_pair_j = evaluator.edge_pair_j
        self.edge_rect_pair_edge = evaluator.edge_rect_pair_edge
        self.edge_rect_pair_rect = evaluator.edge_rect_pair_rect
        self.source_options: list[np.ndarray] = []
        self.target_options: list[np.ndarray] = []
        self.source_choice = np.zeros(evaluator.edge_count, dtype=np.int32)
        self.target_choice = np.zeros(evaluator.edge_count, dtype=np.int32)
        self._build_options(slots)
        self.segments = self._segments_from_choices()
        self.cross_flags = exact.pair_cross_flags(
            self.segments,
            self.edge_pair_i,
            self.edge_pair_j,
        )
        self.edge_rect_flags = exact.edge_rect_flags(
            self.segments,
            self.rect_positions,
            self.rect_widths,
            self.rect_heights,
            self.edge_rect_pair_edge,
            self.edge_rect_pair_rect,
            evaluator.edge_rect_margin,
        )

    @property
    def edge_cross(self) -> int:
        return int(self.cross_flags.sum())

    @property
    def edge_rect(self) -> int:
        return int(self.edge_rect_flags.sum())

    @property
    def score(self) -> int:
        return self.edge_cross + self.edge_rect

    def _build_options(self, slots: int) -> None:
        for edge in self.evaluator.visual_edges:
            s_rect = int(edge.source_rect)
            t_rect = int(edge.target_rect)
            s_center = self.rect_positions[s_rect]
            t_center = self.rect_positions[t_rect]
            self.source_options.append(
                boundary_options(
                    s_center,
                    float(self.rect_widths[s_rect]),
                    float(self.rect_heights[s_rect]),
                    t_center,
                    slots,
                )
            )
            self.target_options.append(
                boundary_options(
                    t_center,
                    float(self.rect_widths[t_rect]),
                    float(self.rect_heights[t_rect]),
                    s_center,
                    slots,
                )
            )

    def _segments_from_choices(self) -> np.ndarray:
        segments = np.zeros((self.evaluator.edge_count, 2, 2), dtype=np.float64)
        for edge_idx in range(self.evaluator.edge_count):
            segments[edge_idx, 0] = self.source_options[edge_idx][self.source_choice[edge_idx]]
            segments[edge_idx, 1] = self.target_options[edge_idx][self.target_choice[edge_idx]]
        return segments

    def rebuild_flags(self) -> None:
        self.segments = self._segments_from_choices()
        self.cross_flags = exact.pair_cross_flags(
            self.segments,
            self.edge_pair_i,
            self.edge_pair_j,
        )
        self.edge_rect_flags = exact.edge_rect_flags(
            self.segments,
            self.rect_positions,
            self.rect_widths,
            self.rect_heights,
            self.edge_rect_pair_edge,
            self.edge_rect_pair_rect,
            self.evaluator.edge_rect_margin,
        )

    def initialize_from_rendered_edges(self, rendered_edges: list[dict]) -> None:
        by_id = {str(edge.get("id") or ""): edge for edge in rendered_edges}
        matched = 0
        for edge_idx, edge in enumerate(self.evaluator.visual_edges):
            row = by_id.get(edge.edge_id)
            if row is None:
                continue
            points = row.get("points") or []
            if len(points) < 2:
                continue
            source_point = np.array(
                [float(points[0].get("x", 0.0)), float(points[0].get("y", 0.0))],
                dtype=np.float64,
            )
            target_point = np.array(
                [float(points[1].get("x", 0.0)), float(points[1].get("y", 0.0))],
                dtype=np.float64,
            )
            if np.linalg.norm(self.source_options[edge_idx] - source_point.reshape(1, 2), axis=1).min() > 0.25:
                self.source_options[edge_idx] = np.vstack([self.source_options[edge_idx], source_point])
            if np.linalg.norm(self.target_options[edge_idx] - target_point.reshape(1, 2), axis=1).min() > 0.25:
                self.target_options[edge_idx] = np.vstack([self.target_options[edge_idx], target_point])
            source_dist = np.linalg.norm(self.source_options[edge_idx] - source_point.reshape(1, 2), axis=1)
            target_dist = np.linalg.norm(self.target_options[edge_idx] - target_point.reshape(1, 2), axis=1)
            self.source_choice[edge_idx] = int(np.argmin(source_dist))
            self.target_choice[edge_idx] = int(np.argmin(target_dist))
            matched += 1
        self.rebuild_flags()
        print(f"initialized ports from json matched={matched}", flush=True)

    def edge_contrib(self) -> np.ndarray:
        contrib = np.zeros(self.evaluator.edge_count, dtype=np.int32)
        if self.cross_flags.size:
            np.add.at(contrib, self.edge_pair_i[self.cross_flags], 1)
            np.add.at(contrib, self.edge_pair_j[self.cross_flags], 1)
        if self.edge_rect_flags.size:
            np.add.at(contrib, self.edge_rect_pair_edge[self.edge_rect_flags], 1)
        return contrib

    def evaluate_segment(self, edge_idx: int, segment: np.ndarray) -> tuple[int, int, int]:
        pair_mask = (self.edge_pair_i == edge_idx) | (self.edge_pair_j == edge_idx)
        old_cross = int(self.cross_flags[pair_mask].sum())
        new_cross_flags = cross_flags_for_edge(
            edge_idx,
            segment,
            self.segments,
            self.edge_pair_i,
            self.edge_pair_j,
            pair_mask,
        )
        new_cross = int(new_cross_flags.sum())

        edge_rect_mask = self.edge_rect_pair_edge == edge_idx
        old_edge_rect = int(self.edge_rect_flags[edge_rect_mask].sum())
        new_edge_rect_flags = edge_rect_flags_for_edge(
            edge_idx,
            segment,
            self.rect_positions,
            self.rect_widths,
            self.rect_heights,
            self.edge_rect_pair_edge,
            self.edge_rect_pair_rect,
            edge_rect_mask,
            self.evaluator.edge_rect_margin,
        )
        new_edge_rect = int(new_edge_rect_flags.sum())
        gain = (old_cross + old_edge_rect) - (new_cross + new_edge_rect)
        return gain, new_cross, new_edge_rect

    def candidate_segments(self, edge_idx: int, limit: int) -> list[tuple[int, int, np.ndarray]]:
        current_s = int(self.source_choice[edge_idx])
        current_t = int(self.target_choice[edge_idx])
        current_segment = np.array(
            [
                self.source_options[edge_idx][current_s],
                self.target_options[edge_idx][current_t],
            ],
            dtype=np.float64,
        )
        if limit <= 1:
            return [(current_s, current_t, current_segment)]

        rows: list[tuple[int, int, int, int, np.ndarray]] = []
        for s_choice in range(len(self.source_options[edge_idx])):
            for t_choice in range(len(self.target_options[edge_idx])):
                segment = np.array(
                    [
                        self.source_options[edge_idx][s_choice],
                        self.target_options[edge_idx][t_choice],
                    ],
                    dtype=np.float64,
                )
                gain, new_cross, new_edge_rect = self.evaluate_segment(edge_idx, segment)
                rows.append((new_cross + new_edge_rect, -gain, s_choice, t_choice, segment))
        rows.sort(key=lambda row: (row[0], row[1]))

        out: list[tuple[int, int, np.ndarray]] = [(current_s, current_t, current_segment)]
        seen = {(current_s, current_t)}
        for _score, _neg_gain, s_choice, t_choice, segment in rows:
            key = (int(s_choice), int(t_choice))
            if key in seen:
                continue
            seen.add(key)
            out.append((int(s_choice), int(t_choice), segment))
            if len(out) >= limit:
                break
        return out

    def evaluate_two_segments(
        self,
        edge_a: int,
        segment_a: np.ndarray,
        edge_b: int,
        segment_b: np.ndarray,
    ) -> int:
        pair_mask = (
            (self.edge_pair_i == edge_a)
            | (self.edge_pair_j == edge_a)
            | (self.edge_pair_i == edge_b)
            | (self.edge_pair_j == edge_b)
        )
        old_cross = int(self.cross_flags[pair_mask].sum())
        ii = self.edge_pair_i[pair_mask]
        jj = self.edge_pair_j[pair_mask]
        if ii.size:
            left = self.segments[ii].copy()
            right = self.segments[jj].copy()
            left[ii == edge_a] = segment_a
            right[jj == edge_a] = segment_a
            left[ii == edge_b] = segment_b
            right[jj == edge_b] = segment_b
            temp = np.concatenate([left, right], axis=0)
            left_idx = np.arange(left.shape[0], dtype=np.int32)
            right_idx = left_idx + left.shape[0]
            new_cross = int(exact.pair_cross_flags(temp, left_idx, right_idx).sum())
        else:
            new_cross = 0

        edge_rect_mask = (
            (self.edge_rect_pair_edge == edge_a)
            | (self.edge_rect_pair_edge == edge_b)
        )
        old_edge_rect = int(self.edge_rect_flags[edge_rect_mask].sum())
        rects = self.edge_rect_pair_rect[edge_rect_mask]
        edges = self.edge_rect_pair_edge[edge_rect_mask]
        if rects.size:
            segs = self.segments[edges].copy()
            segs[edges == edge_a] = segment_a
            segs[edges == edge_b] = segment_b
            local_edges = np.arange(rects.size, dtype=np.int32)
            new_edge_rect = int(
                exact.edge_rect_flags(
                    segs,
                    self.rect_positions,
                    self.rect_widths,
                    self.rect_heights,
                    local_edges,
                    rects,
                    self.evaluator.edge_rect_margin,
                ).sum()
            )
        else:
            new_edge_rect = 0
        return (old_cross + old_edge_rect) - (new_cross + new_edge_rect)

    def apply_segment(
        self,
        edge_idx: int,
        source_choice: int,
        target_choice: int,
    ) -> None:
        self.source_choice[edge_idx] = int(source_choice)
        self.target_choice[edge_idx] = int(target_choice)
        segment = np.array(
            [
                self.source_options[edge_idx][source_choice],
                self.target_options[edge_idx][target_choice],
            ],
            dtype=np.float64,
        )
        self.segments[edge_idx] = segment
        pair_mask = (self.edge_pair_i == edge_idx) | (self.edge_pair_j == edge_idx)
        self.cross_flags[pair_mask] = cross_flags_for_edge(
            edge_idx,
            segment,
            self.segments,
            self.edge_pair_i,
            self.edge_pair_j,
            pair_mask,
        )
        edge_rect_mask = self.edge_rect_pair_edge == edge_idx
        self.edge_rect_flags[edge_rect_mask] = edge_rect_flags_for_edge(
            edge_idx,
            segment,
            self.rect_positions,
            self.rect_widths,
            self.rect_heights,
            self.edge_rect_pair_edge,
            self.edge_rect_pair_rect,
            edge_rect_mask,
            self.evaluator.edge_rect_margin,
        )

    def optimize(self, rounds: int, hot_edges: int, combo_hot_edges: int) -> None:
        for round_idx in range(rounds):
            before = self.score
            accepted = 0
            contrib = self.edge_contrib()
            order = np.argsort(-contrib)[:hot_edges]
            for rank, edge_idx_raw in enumerate(order):
                edge_idx = int(edge_idx_raw)
                if contrib[edge_idx] <= 0:
                    continue
                current_s = int(self.source_choice[edge_idx])
                current_t = int(self.target_choice[edge_idx])
                best_gain = 0
                best_s = current_s
                best_t = current_t

                source_range = range(len(self.source_options[edge_idx]))
                target_range = range(len(self.target_options[edge_idx]))
                if rank >= combo_hot_edges:
                    checks = (
                        [(s, current_t) for s in source_range]
                        + [(current_s, t) for t in target_range]
                    )
                else:
                    checks = [(s, t) for s in source_range for t in target_range]
                for s_choice, t_choice in checks:
                    if s_choice == current_s and t_choice == current_t:
                        continue
                    segment = np.array(
                        [
                            self.source_options[edge_idx][s_choice],
                            self.target_options[edge_idx][t_choice],
                        ],
                        dtype=np.float64,
                    )
                    gain, _new_cross, _new_edge_rect = self.evaluate_segment(edge_idx, segment)
                    if gain > best_gain:
                        best_gain = gain
                        best_s = int(s_choice)
                        best_t = int(t_choice)
                if best_gain > 0:
                    self.apply_segment(edge_idx, best_s, best_t)
                    accepted += 1
            print(
                f"round {round_idx + 1}: accepted={accepted} "
                f"edgeCross={self.edge_cross} edgeRect={self.edge_rect} "
                f"score={self.score} gain={before - self.score}",
                flush=True,
            )
            if accepted == 0 or self.score >= before:
                break

    def optimize_pairs(
        self,
        passes: int,
        crossing_pairs: int,
        options_per_edge: int,
    ) -> None:
        if passes <= 0 or crossing_pairs <= 0 or options_per_edge <= 1:
            return
        for pass_idx in range(passes):
            before = self.score
            accepted = 0
            contrib = self.edge_contrib()
            active_pair_rows = np.flatnonzero(self.cross_flags)
            if active_pair_rows.size == 0:
                break
            pair_order = active_pair_rows[
                np.argsort(
                    -(
                        contrib[self.edge_pair_i[active_pair_rows]]
                        + contrib[self.edge_pair_j[active_pair_rows]]
                    )
                )
            ][:crossing_pairs]
            option_cache: dict[int, list[tuple[int, int, np.ndarray]]] = {}
            for row_raw in pair_order:
                row = int(row_raw)
                edge_a = int(self.edge_pair_i[row])
                edge_b = int(self.edge_pair_j[row])
                if not self.cross_flags[row]:
                    continue
                if edge_a not in option_cache:
                    option_cache[edge_a] = self.candidate_segments(edge_a, options_per_edge)
                if edge_b not in option_cache:
                    option_cache[edge_b] = self.candidate_segments(edge_b, options_per_edge)

                current_a = (int(self.source_choice[edge_a]), int(self.target_choice[edge_a]))
                current_b = (int(self.source_choice[edge_b]), int(self.target_choice[edge_b]))
                best_gain = 0
                best_a = current_a
                best_b = current_b
                for s_a, t_a, segment_a in option_cache[edge_a]:
                    for s_b, t_b, segment_b in option_cache[edge_b]:
                        if (s_a, t_a) == current_a and (s_b, t_b) == current_b:
                            continue
                        gain = self.evaluate_two_segments(
                            edge_a,
                            segment_a,
                            edge_b,
                            segment_b,
                        )
                        if gain > best_gain:
                            best_gain = gain
                            best_a = (s_a, t_a)
                            best_b = (s_b, t_b)
                if best_gain > 0:
                    self.apply_segment(edge_a, best_a[0], best_a[1])
                    self.apply_segment(edge_b, best_b[0], best_b[1])
                    option_cache.pop(edge_a, None)
                    option_cache.pop(edge_b, None)
                    accepted += 1
            print(
                f"pair pass {pass_idx + 1}: accepted={accepted} "
                f"edgeCross={self.edge_cross} edgeRect={self.edge_rect} "
                f"score={self.score} gain={before - self.score}",
                flush=True,
            )
            if accepted == 0 or self.score >= before:
                break

    def restore_choices(self, source_choice: np.ndarray, target_choice: np.ndarray) -> None:
        self.source_choice = source_choice.copy()
        self.target_choice = target_choice.copy()
        self.rebuild_flags()

    def optimize_anneal(
        self,
        iterations: int,
        hot_edges: int,
        *,
        start_temp: float,
        end_temp: float,
        seed: int,
    ) -> None:
        if iterations <= 0 or hot_edges <= 0:
            return
        rng = random.Random(seed)
        best_score = self.score
        best_source = self.source_choice.copy()
        best_target = self.target_choice.copy()
        contrib = self.edge_contrib()
        accepted = 0
        improved = 0
        for iteration in range(1, iterations + 1):
            if iteration == 1 or iteration % 64 == 0:
                contrib = self.edge_contrib()
                order = [int(x) for x in np.argsort(-contrib)[:hot_edges] if contrib[int(x)] > 0]
                if not order:
                    break
                weights = [max(1, int(contrib[idx])) for idx in order]
            frac = iteration / max(1, iterations)
            temp = float(start_temp) * ((float(end_temp) / max(1e-9, float(start_temp))) ** frac)
            edge_idx = rng.choices(order, weights=weights, k=1)[0]
            current_s = int(self.source_choice[edge_idx])
            current_t = int(self.target_choice[edge_idx])
            s_choice = rng.randrange(len(self.source_options[edge_idx]))
            t_choice = rng.randrange(len(self.target_options[edge_idx]))
            if s_choice == current_s and t_choice == current_t:
                continue
            segment = np.array(
                [
                    self.source_options[edge_idx][s_choice],
                    self.target_options[edge_idx][t_choice],
                ],
                dtype=np.float64,
            )
            gain, _new_cross, _new_edge_rect = self.evaluate_segment(edge_idx, segment)
            accept = gain > 0
            if not accept and temp > 1e-9:
                accept = rng.random() < math.exp(float(gain) / temp)
            if not accept:
                continue
            self.apply_segment(edge_idx, s_choice, t_choice)
            accepted += 1
            if self.score < best_score:
                best_score = self.score
                best_source = self.source_choice.copy()
                best_target = self.target_choice.copy()
                improved += 1
            if iteration % 1000 == 0:
                print(
                    f"anneal iter={iteration} accepted={accepted} improved={improved} "
                    f"current={self.score} best={best_score} temp={temp:.3f}",
                    flush=True,
                )
        self.restore_choices(best_source, best_target)
        print(
            f"anneal done accepted={accepted} improved={improved} "
            f"edgeCross={self.edge_cross} edgeRect={self.edge_rect} score={self.score}",
            flush=True,
        )


def build_rects(evaluator: exact.ExactRelationEvaluator, rect_positions: np.ndarray) -> list[dict]:
    out: list[dict] = []
    for rect_idx in range(evaluator.geometry.rect_node.shape[0]):
        rid = rect_id_for(evaluator, rect_idx)
        node_idx = int(evaluator.geometry.rect_node[rect_idx])
        out.append(
            {
                "id": rid,
                "label": rect_label(rid),
                "cluster": str(evaluator.nodes[node_idx].get("clusterId") or "") if node_idx >= 0 else "",
                "kind": "node" if node_idx >= 0 else "bundle",
                "x": float(rect_positions[rect_idx, 0] - evaluator.geometry.rect_widths[rect_idx] / 2.0),
                "y": float(rect_positions[rect_idx, 1] - evaluator.geometry.rect_heights[rect_idx] / 2.0),
                "w": float(evaluator.geometry.rect_widths[rect_idx]),
                "h": float(evaluator.geometry.rect_heights[rect_idx]),
            }
        )
    return out


def build_edges(evaluator: exact.ExactRelationEvaluator, segments: np.ndarray) -> list[dict]:
    out: list[dict] = []
    for edge_idx, edge in enumerate(evaluator.visual_edges):
        carrier = edge.edge_id if edge.edge_id.startswith("B") else ""
        out.append(
            {
                "id": edge.edge_id,
                "source": rect_id_for(evaluator, int(edge.source_rect)),
                "target": rect_id_for(evaluator, int(edge.target_rect)),
                "carrier": carrier,
                "count": int(edge.count),
                "points": [
                    {"x": float(segments[edge_idx, 0, 0]), "y": float(segments[edge_idx, 0, 1])},
                    {"x": float(segments[edge_idx, 1, 0]), "y": float(segments[edge_idx, 1, 1])},
                ],
            }
        )
    return out


def bounds(rects: list[dict], edges: list[dict]) -> dict:
    xs: list[float] = []
    ys: list[float] = []
    for rect in rects:
        xs.extend([float(rect["x"]), float(rect["x"]) + float(rect["w"])])
        ys.extend([float(rect["y"]), float(rect["y"]) + float(rect["h"])])
    for edge in edges:
        for point in edge["points"]:
            xs.append(float(point["x"]))
            ys.append(float(point["y"]))
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)
    pad = max(max_x - min_x, max_y - min_y) * 0.04
    return {
        "x": min_x - pad,
        "y": min_y - pad,
        "w": (max_x - min_x) + pad * 2.0,
        "h": (max_y - min_y) + pad * 2.0,
    }


def write_html(path: Path, data: dict) -> None:
    payload = json.dumps(data, separators=(",", ":"))
    title = str(data.get("title") or "v35 port assignment")
    html_text = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{esc(title)}</title>
  <style>
    html, body {{ height: 100%; margin: 0; font-family: ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; background: #111827; color: #e5e7eb; }}
    body {{ overflow: hidden; }}
    .shell {{ display: grid; grid-template-rows: auto 1fr; height: 100%; }}
    .bar {{ display: flex; gap: 10px; align-items: center; padding: 10px 12px; background: #1f2937; border-bottom: 1px solid #374151; }}
    .title {{ font-weight: 700; white-space: nowrap; }}
    .metric {{ color: #cbd5e1; font-size: 13px; white-space: nowrap; }}
    .spacer {{ flex: 1; }}
    button, input {{ height: 30px; border-radius: 6px; border: 1px solid #4b5563; background: #111827; color: #e5e7eb; }}
    button {{ padding: 0 10px; cursor: pointer; }}
    input {{ width: 260px; padding: 0 8px; }}
    #canvas {{ width: 100%; height: 100%; display: block; background: #f8fafc; cursor: grab; }}
    #canvas.dragging {{ cursor: grabbing; }}
    .detail {{ position: fixed; right: 12px; top: 58px; width: min(420px, calc(100vw - 24px)); max-height: calc(100vh - 80px); overflow: auto; background: rgba(15, 23, 42, 0.94); color: #e5e7eb; border: 1px solid #475569; border-radius: 8px; padding: 12px; font-size: 12px; line-height: 1.4; box-shadow: 0 18px 40px rgba(15, 23, 42, 0.28); display: none; }}
    .detail.visible {{ display: block; }}
    .detail h2 {{ margin: 0 0 6px; font-size: 14px; }}
    .detail .sub {{ color: #cbd5e1; margin-bottom: 8px; }}
    .detail ul {{ margin: 8px 0 0; padding-left: 18px; }}
    .detail li {{ margin: 2px 0; overflow-wrap: anywhere; }}
  </style>
</head>
<body>
<div class="shell">
  <div class="bar">
    <div class="title">{esc(title)}</div>
    <div class="metric" id="metrics"></div>
    <div class="spacer"></div>
    <input id="search" placeholder="model name search">
    <button id="fit">Fit</button>
    <button id="zin">+</button>
    <button id="zout">-</button>
    <span class="metric" id="zoomLabel"></span>
  </div>
  <canvas id="canvas"></canvas>
  <div class="detail" id="detail"></div>
</div>
<script id="layout-data" type="application/json">{payload}</script>
<script>
const data = JSON.parse(document.getElementById('layout-data').textContent);
const canvas = document.getElementById('canvas');
const ctx = canvas.getContext('2d');
let dpr = window.devicePixelRatio || 1;
let panX = 0, panY = 0, zoom = 1, dragging = false, moved = false, lastX = 0, lastY = 0, downX = 0, downY = 0, selected = '', activeBoxId = '';
const carrierGroups = new Map((data.carrierGroups || []).map(g => [String(g.carrier), g]));
const aliasGroups = new Map((data.aliasGroups || []).map(g => [String(g.alias), g]));
const detail = document.getElementById('detail');
const rectById = new Map(data.rects.map(r => [String(r.id), r]));
document.getElementById('metrics').textContent = [
  `edgeCross=${{data.metrics.edgeCross}}`,
  `edgeRect=${{data.metrics.edgeRect}}`,
  `overlap=${{data.metrics.overlaps}}`,
  `visual=${{data.metrics.visualCross}}`,
  `edges=${{data.edges.length}}`
].join(' · ');
function resize() {{
  const r = canvas.getBoundingClientRect();
  dpr = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, Math.floor(r.width * dpr));
  canvas.height = Math.max(1, Math.floor(r.height * dpr));
  draw();
}}
function fit() {{
  const r = canvas.getBoundingClientRect(), b = data.bounds;
  zoom = Math.max(0.01, Math.min(r.width / b.w, r.height / b.h) * 0.92);
  panX = (r.width - b.w * zoom) / 2 - b.x * zoom;
  panY = (r.height - b.h * zoom) / 2 - b.y * zoom;
  draw();
}}
function screenToWorld(x, y) {{ return [(x - panX) / zoom, (y - panY) / zoom]; }}
function visibleWorld() {{
  const r = canvas.getBoundingClientRect(), a = screenToWorld(0,0), b = screenToWorld(r.width,r.height);
  const p = 600 / Math.max(zoom, 0.01);
  return {{left:a[0]-p, top:a[1]-p, right:b[0]+p, bottom:b[1]+p}};
}}
function rectVisible(r, v) {{ return r.x <= v.right && r.x + r.w >= v.left && r.y <= v.bottom && r.y + r.h >= v.top; }}
function edgeVisible(e, v) {{ return e.points.some(p => p.x >= v.left && p.x <= v.right && p.y >= v.top && p.y <= v.bottom); }}
function relationVisible(rel, v) {{
  const pts = rel.points || [];
  return pts.some(p => p.x >= v.left && p.x <= v.right && p.y >= v.top && p.y <= v.bottom);
}}
function escapeText(value) {{
  return String(value).replace(/[&<>"']/g, ch => ({{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}}[ch]));
}}
function hitBox(sx, sy) {{
  const [x, y] = screenToWorld(sx, sy);
  for (let i = data.rects.length - 1; i >= 0; i--) {{
    const b = data.rects[i];
    if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) return b;
  }}
  return null;
}}
function updateDetail() {{
  const box = data.rects.find(b => b.id === activeBoxId);
  if (!box) {{ detail.classList.remove('visible'); detail.textContent = ''; return; }}
  const group = carrierGroups.get(String(box.id)) || aliasGroups.get(String(box.id));
  const lines = [];
  lines.push(`<h2>${{escapeText(box.label || box.id)}}</h2>`);
  lines.push(`<div class="sub">${{escapeText(box.id)}} · ${{escapeText(box.kind || 'node')}}</div>`);
  if (group) {{
    const aliasOf = box.aliasOf ? ` · aliasOf=${{escapeText(box.aliasOf)}}` : '';
    lines.push(`<div>hub=${{escapeText(group.hub)}} · count=${{Number(group.count || 0)}}${{aliasOf}}</div>`);
    const relations = (group.relations || []).slice(0, 160);
    lines.push(`<ul>${{relations.map(rel => `<li>${{escapeText(rel.source)}} → ${{escapeText(rel.target)}} · ${{escapeText(rel.field || rel.id)}}</li>`).join('')}}</ul>`);
  }} else if (box.aliasOf) {{
    lines.push(`<div>aliasOf=${{escapeText(box.aliasOf)}} · key=${{escapeText(box.aliasKey || '')}}</div>`);
  }}
  detail.innerHTML = lines.join('');
  detail.classList.add('visible');
}}
function colorForCluster(c) {{
  if (!c) return '#64748b';
  let h = 2166136261;
  for (let i = 0; i < c.length; i++) h = Math.imul(h ^ c.charCodeAt(i), 16777619);
  return `hsl(${{Math.abs(h) % 360}} 58% 42%)`;
}}
function drawSelectedCarrierRelations(v) {{
  const group = carrierGroups.get(String(activeBoxId)) || aliasGroups.get(String(activeBoxId));
  if (!group) return;
  const relations = group.relations || [];
  ctx.save();
  ctx.setLineDash([10 / Math.max(zoom, 0.1), 7 / Math.max(zoom, 0.1)]);
  ctx.lineCap = 'round';
  ctx.lineJoin = 'round';
  for (const rel of relations) {{
    if (!relationVisible(rel, v)) continue;
    const pts = rel.points || [];
    if (pts.length < 2) continue;
    ctx.strokeStyle = 'rgba(220, 38, 38, 0.58)';
    ctx.lineWidth = Math.max(2.2 / zoom, 1.6);
    ctx.beginPath();
    ctx.moveTo(pts[0].x, pts[0].y);
    ctx.lineTo(pts[1].x, pts[1].y);
    ctx.stroke();
  }}
  ctx.restore();
}}
function draw() {{
  const r = canvas.getBoundingClientRect();
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0,0,r.width,r.height);
  ctx.fillStyle = '#f8fafc';
  ctx.fillRect(0,0,r.width,r.height);
  ctx.save();
  ctx.translate(panX, panY); ctx.scale(zoom, zoom);
  const v = visibleWorld();
  ctx.lineCap = 'round'; ctx.lineJoin = 'round';
  for (const e of data.edges) {{
    if (!edgeVisible(e, v)) continue;
    const isBundle = String(e.carrier || '').startsWith('B');
    const isCarrier = String(e.carrier || '').startsWith('hub-carrier:');
    const count = Number(e.count || 1);
    ctx.strokeStyle = isCarrier ? 'rgba(2, 132, 199, 0.44)' : (isBundle ? 'rgba(180, 83, 9, 0.32)' : 'rgba(51, 65, 85, 0.24)');
    ctx.lineWidth = Math.max((isBundle || isCarrier ? 1.8 : 1.0) / zoom, isBundle || isCarrier ? 1.4 : 0.75) + Math.min(8, Math.sqrt(count)) / Math.max(zoom, 0.08) * 0.12;
    ctx.beginPath();
    ctx.moveTo(e.points[0].x, e.points[0].y);
    ctx.lineTo(e.points[1].x, e.points[1].y);
    ctx.stroke();
  }}
  drawSelectedCarrierRelations(v);
  for (const box of data.rects) {{
    if (!rectVisible(box, v)) continue;
    const group = carrierGroups.get(String(activeBoxId)) || aliasGroups.get(String(activeBoxId));
    const related = group && (box.id === group.hub || (group.sources || []).includes(box.id));
    const hit = (selected && box.id.toLowerCase().includes(selected)) || box.id === activeBoxId || related;
    ctx.fillStyle = hit ? '#fef08a' : (box.kind === 'alias' ? '#ecfeff' : (box.kind === 'carrier' ? '#e0f2fe' : (box.kind === 'bundle' ? '#fff7ed' : '#ffffff')));
    ctx.strokeStyle = hit ? '#dc2626' : (box.kind === 'alias' ? '#0891b2' : (box.kind === 'carrier' ? '#0284c7' : (box.kind === 'bundle' ? '#ea580c' : colorForCluster(box.cluster))));
    ctx.lineWidth = hit ? Math.max(5 / zoom, 2) : Math.max(1.4 / zoom, 1);
    ctx.beginPath(); ctx.rect(box.x, box.y, box.w, box.h); ctx.fill(); ctx.stroke();
    if (zoom >= 0.22 || hit) {{
      ctx.fillStyle = '#0f172a';
      ctx.font = `${{Math.max(10 / zoom, 13)}}px ui-sans-serif`;
      ctx.fillText(box.label, box.x + 8 / zoom, box.y + 22 / zoom);
    }}
  }}
  ctx.restore();
  document.getElementById('zoomLabel').textContent = `${{Math.round(zoom * 100)}}%`;
}}
canvas.addEventListener('mousedown', e => {{ dragging = true; moved = false; downX = e.clientX; downY = e.clientY; lastX = e.clientX; lastY = e.clientY; canvas.classList.add('dragging'); }});
window.addEventListener('mouseup', e => {{
  if (dragging && !moved) {{
    const r = canvas.getBoundingClientRect();
    const box = hitBox(e.clientX - r.left, e.clientY - r.top);
    activeBoxId = box ? box.id : '';
    updateDetail(); draw();
  }}
  dragging = false; canvas.classList.remove('dragging');
}});
window.addEventListener('mousemove', e => {{
  if (!dragging) return;
  if (Math.hypot(e.clientX - downX, e.clientY - downY) > 3) moved = true;
  panX += e.clientX - lastX; panY += e.clientY - lastY; lastX = e.clientX; lastY = e.clientY; draw();
}});
canvas.addEventListener('wheel', e => {{
  e.preventDefault();
  const r = canvas.getBoundingClientRect(), mx = e.clientX - r.left, my = e.clientY - r.top;
  const before = screenToWorld(mx, my);
  zoom *= e.deltaY < 0 ? 1.15 : 1 / 1.15;
  zoom = Math.max(0.01, Math.min(zoom, 8));
  panX = mx - before[0] * zoom; panY = my - before[1] * zoom; draw();
}}, {{passive:false}});
document.getElementById('fit').onclick = fit;
document.getElementById('zin').onclick = () => {{ zoom *= 1.2; draw(); }};
document.getElementById('zout').onclick = () => {{ zoom /= 1.2; draw(); }};
document.getElementById('search').addEventListener('input', e => {{ selected = e.target.value.trim().toLowerCase(); activeBoxId = ''; updateDetail(); draw(); }});
window.addEventListener('resize', resize);
resize(); fit();
</script>
</body>
</html>
"""
    path.write_text(html_text)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--positions", type=Path, default=None)
    parser.add_argument("--out-html", type=Path, required=True)
    parser.add_argument("--out-json", type=Path, default=None)
    parser.add_argument("--initial-json", type=Path, default=None)
    parser.add_argument("--title", default="v35 exact relation port assignment")
    parser.add_argument("--rounds", type=int, default=8)
    parser.add_argument("--hot-edges", type=int, default=420)
    parser.add_argument("--combo-hot-edges", type=int, default=120)
    parser.add_argument("--slots", type=int, default=13)
    parser.add_argument("--pair-passes", type=int, default=0)
    parser.add_argument("--pair-crossings", type=int, default=120)
    parser.add_argument("--pair-options", type=int, default=8)
    parser.add_argument("--anneal-iters", type=int, default=0)
    parser.add_argument("--anneal-hot-edges", type=int, default=240)
    parser.add_argument("--anneal-start-temp", type=float, default=1.5)
    parser.add_argument("--anneal-end-temp", type=float, default=0.05)
    parser.add_argument("--anneal-seed", type=int, default=35)
    parser.add_argument("--edge-rect-margin", type=float, default=0.0)
    args = parser.parse_args()

    layout = json.loads(args.layout.read_text())
    positions = (
        exact.v34.read_positions_tsv(args.positions, layout)
        if args.positions is not None
        else exact.v34.layout_positions(layout)
    )
    evaluator = exact.ExactRelationEvaluator(
        layout,
        overlap_margin=0.0,
        edge_rect_margin=args.edge_rect_margin,
        overlap_weight=0.0,
        bbox_weight=0.0,
        bbox_target_b=0.0,
    )
    opt = PortOptimizer(evaluator, positions, slots=args.slots)
    if args.initial_json is not None:
        seed = json.loads(args.initial_json.read_text())
        opt.initialize_from_rendered_edges(list(seed.get("edges") or []))
    rect_positions = evaluator.rect_positions(positions)
    overlap_flags = exact.v34.overlap_flags_for_pairs(
        rect_positions,
        evaluator.geometry.rect_widths,
        evaluator.geometry.rect_heights,
        0.0,
        evaluator.overlap_pair_i,
        evaluator.overlap_pair_j,
    )
    overlaps = int(overlap_flags.sum())
    print(
        f"initial port edgeCross={opt.edge_cross} edgeRect={opt.edge_rect} "
        f"overlaps={overlaps} visual={opt.score + overlaps}",
        flush=True,
    )
    started = time.time()
    opt.optimize(args.rounds, args.hot_edges, args.combo_hot_edges)
    opt.optimize_pairs(args.pair_passes, args.pair_crossings, args.pair_options)
    opt.optimize_anneal(
        args.anneal_iters,
        args.anneal_hot_edges,
        start_temp=args.anneal_start_temp,
        end_temp=args.anneal_end_temp,
        seed=args.anneal_seed,
    )
    rects = build_rects(evaluator, opt.rect_positions)
    edges = build_edges(evaluator, opt.segments)
    metrics = {
        "edgeCross": opt.edge_cross,
        "edgeRect": opt.edge_rect,
        "overlaps": overlaps,
        "visualCross": opt.score + overlaps,
        "elapsedSec": round(time.time() - started, 2),
    }
    data = {
        "title": args.title,
        "metrics": metrics,
        "rects": rects,
        "edges": edges,
        "bounds": bounds(rects, edges),
    }
    write_html(args.out_html, data)
    if args.out_json is not None:
        args.out_json.write_text(json.dumps(data, indent=2))
    print(
        f"done edgeCross={opt.edge_cross} edgeRect={opt.edge_rect} "
        f"overlaps={overlaps} visual={opt.score + overlaps} html={args.out_html}",
        flush=True,
    )


if __name__ == "__main__":
    main()
