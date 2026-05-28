#!/usr/bin/env python3
"""Evaluate v35 scorer as a top-k filter before exact verification."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import numpy as np
import torch


ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


ds = load_module(
    "build_v35_action_dataset", ROOT / "scripts/erd-poc/build_v35_action_dataset.py"
)
trainer = load_module(
    "train_v35_graph_action_scorer",
    ROOT / "scripts/erd-poc/train_v35_graph_action_scorer.py",
)
family_trainer = load_module(
    "train_v37_action_family_prior",
    ROOT / "scripts/erd-poc/train_v37_action_family_prior.py",
)
v34 = ds.v34


def local_feature_names(node_static_names: list[str]) -> list[str]:
    names: list[str] = []
    names.extend([f"moved.mean.{name}" for name in node_static_names])
    names.extend([f"moved.max.{name}" for name in node_static_names])
    names.extend(
        [
            "moved.posMeanX",
            "moved.posMeanY",
            "moved.posStdX",
            "moved.posStdY",
            "delta.meanX",
            "delta.meanY",
            "delta.stdX",
            "delta.stdY",
            "moved.count",
            "moved.logCount",
            "delta.distMean",
            "delta.distMax",
            "delta.distStd",
            "delta.logDistMean",
            "delta.logDistMax",
            "delta.nonzeroFrac",
        ]
    )
    return names


def local_feature_values(
    node_static: np.ndarray,
    node_static_names: list[str],
    positions: np.ndarray,
    moved_positions: np.ndarray,
    cand: object,
) -> dict[str, float]:
    names = local_feature_names(node_static_names)
    values = {name: 0.0 for name in names}
    affected, delta = ds.affected_nodes_and_deltas(cand, positions, moved_positions)
    if affected.size == 0:
        return values
    state_mean = positions.mean(axis=0)
    state_std = float(positions.reshape(-1).std() + 1e-6)
    static = node_static[affected]
    pos_norm = (positions[affected] - state_mean) / state_std
    dist = np.linalg.norm(delta, axis=1)

    for name, val in zip(node_static_names, static.mean(axis=0)):
        values[f"moved.mean.{name}"] = float(val)
    for name, val in zip(node_static_names, static.max(axis=0)):
        values[f"moved.max.{name}"] = float(val)
    values["moved.posMeanX"] = float(pos_norm[:, 0].mean())
    values["moved.posMeanY"] = float(pos_norm[:, 1].mean())
    values["moved.posStdX"] = float(pos_norm[:, 0].std())
    values["moved.posStdY"] = float(pos_norm[:, 1].std())
    values["delta.meanX"] = float(delta[:, 0].mean())
    values["delta.meanY"] = float(delta[:, 1].mean())
    values["delta.stdX"] = float(delta[:, 0].std())
    values["delta.stdY"] = float(delta[:, 1].std())
    values["moved.count"] = float(affected.shape[0])
    values["moved.logCount"] = float(np.log1p(affected.shape[0]))
    values["delta.distMean"] = float(dist.mean()) if dist.size else 0.0
    values["delta.distMax"] = float(dist.max()) if dist.size else 0.0
    values["delta.distStd"] = float(dist.std()) if dist.size else 0.0
    values["delta.logDistMean"] = float(np.log1p(dist.mean())) if dist.size else 0.0
    values["delta.logDistMax"] = float(np.log1p(dist.max())) if dist.size else 0.0
    values["delta.nonzeroFrac"] = float((dist > 0).mean()) if dist.size else 0.0
    return values


class LiveScorer:
    def __init__(self, ckpt_path: Path):
        try:
            ckpt = torch.load(ckpt_path, map_location="cpu", weights_only=False)
        except TypeError:
            ckpt = torch.load(ckpt_path, map_location="cpu")
        self.feature_names = [str(name) for name in ckpt["feature_names"]]
        self.mean = np.asarray(ckpt["mean"], dtype=np.float32)
        self.std = np.asarray(ckpt["std"], dtype=np.float32)
        self.std[self.std < 1e-6] = 1.0
        self.requested_scalar_names = {
            name.removeprefix("scalar.")
            for name in self.feature_names
            if name.startswith("scalar.")
        }
        self.needs_edge_node_context = bool(
            {
                "contextCurrentEdgeNodePairs",
                "contextCurrentEdgeNodeFrac",
            }
            & self.requested_scalar_names
        )
        self.model = trainer.ActionScorer(
            len(self.feature_names),
            int(ckpt["hidden"]),
            int(ckpt["layers"]),
            float(ckpt["dropout"]),
        )
        self.model.load_state_dict(ckpt["model"])
        self.model.eval()

    def row_from_candidate(
        self,
        layout: dict,
        edges: np.ndarray,
        metrics: object,
        cand: object,
        positions: np.ndarray,
        moved_positions: np.ndarray,
        node_counts: np.ndarray,
        edge_counts: np.ndarray,
        evaluator: object,
        current_cross_flags: np.ndarray,
        overlap_pairs: tuple[np.ndarray, np.ndarray],
        current_overlap_flags: np.ndarray,
        node_static: np.ndarray,
        node_static_names: list[str],
        edge_node_pairs: tuple[np.ndarray, np.ndarray] | None = None,
        current_edge_node_flags: np.ndarray | None = None,
        collision_geometry: object | None = None,
    ) -> np.ndarray:
        values: dict[str, float] = {}
        scalars = ds.scalar_features(
            layout,
            edges,
            metrics,
            cand,
            positions,
            node_counts,
            edge_counts,
            evaluator,
            current_cross_flags,
            overlap_pairs,
            current_overlap_flags,
            moved_positions,
            edge_node_pairs,
            current_edge_node_flags,
            self.requested_scalar_names,
            collision_geometry,
        )
        for name, value in zip(ds.SCALAR_NAMES, scalars):
            values[f"scalar.{name}"] = float(value)
        values[f"action.{v34.candidate_action_type(cand)}"] = 1.0
        values.update(
            local_feature_values(
                node_static,
                node_static_names,
                positions,
                moved_positions,
                cand,
            )
        )
        return np.array(
            [values.get(name, 0.0) for name in self.feature_names],
            dtype=np.float32,
        )

    def score(
        self,
        rows: np.ndarray,
        batch_size: int = 2048,
    ) -> tuple[np.ndarray, np.ndarray]:
        x = ((rows - self.mean) / self.std).astype(np.float32)
        gains: list[np.ndarray] = []
        ranks: list[np.ndarray] = []
        with torch.no_grad():
            for start in range(0, x.shape[0], batch_size):
                batch = torch.from_numpy(x[start : start + batch_size])
                pred_gain, pred_rank = self.model(batch)
                gains.append(pred_gain.cpu().numpy())
                ranks.append(pred_rank.cpu().numpy())
        return np.concatenate(gains), np.concatenate(ranks)


# Mapping from composite sub-scores to the action families whose moves
# functionally address that quality dimension. Used by the dim-boost
# family-prior reweighting: each sub-score's gap from optimal (1.0)
# proportionally boosts the priors of its associated families. The
# mapping is engineering judgment, not data-tuned — each entry reflects
# "which actions can plausibly improve this dimension?".
SUB_TO_FAMILIES: dict[str, list[str]] = {
    "sub_clean": [
        "edge_translate",
        "node_position_swap",
        "node_translate",
    ],
    "sub_severity": [
        "edge_translate",
        "edge_endpoint_translate",
    ],
    "sub_angle": [
        "edge_endpoint_translate",
        "node_position_swap",
    ],
    "sub_stress": [
        "semantic_orphan_to_louvain_cluster",
        "louvain_cluster_anchor_to_neighbors",
        "node_anchor_to_neighbors",
    ],
    "sub_compact": [
        "node_anchor_to_neighbors",
        "louvain_cluster_anchor_to_neighbors",
        "louvain_cluster_centroid_swap",
        "louvain_cluster_translate",
        "louvain_component_anchor_to_neighbors",
        "overlap_component_line",
        "overlap_components_batch_line",
    ],
    "sub_uniform": [
        "edge_translate",
        "node_translate",
    ],
    "sub_spread": [
        "node_translate",
        "node_anchor_to_neighbors",
    ],
}


class LiveFamilyPrior:
    def __init__(
        self,
        ckpt_path: Path,
        widths: np.ndarray | None = None,
        heights: np.ndarray | None = None,
        cluster_ids: list[str] | None = None,
    ):
        try:
            ckpt = torch.load(ckpt_path, map_location="cpu", weights_only=False)
        except TypeError:
            ckpt = torch.load(ckpt_path, map_location="cpu")
        self.feature_names = [str(name) for name in ckpt["feature_names"]]
        self.action_names = [str(name) for name in ckpt["action_names"]]
        self.mean = np.asarray(ckpt["mean"], dtype=np.float32)
        self.std = np.asarray(ckpt["std"], dtype=np.float32)
        self.std[self.std < 1e-6] = 1.0
        self.gain_scale = float(ckpt.get("gain_scale", 50.0))
        # Detect whether ckpt was trained on extended / composite features.
        self.has_extended = any(
            n in {
                "metric.logStress", "metric.edgeLengthCv",
                "metric.crossingAngleMean", "metric.crossingAngleCv",
                "metric.logHubClearP10", "metric.logClusterCompact",
            }
            for n in self.feature_names
        )
        self.has_composite = any(
            n.startswith("metric.composite") or n.startswith("metric.sub")
            for n in self.feature_names
        )
        # Layout-static data needed for extended/composite computation.
        # Without these the new feature columns fall through to zero, so a
        # composite-trained ckpt sees an inconsistent state.
        self.widths = widths
        self.heights = heights
        self.cluster_ids = cluster_ids
        self._metrics_extended_mod = None
        if self.has_extended or self.has_composite:
            import importlib.util
            spec = importlib.util.spec_from_file_location(
                "metrics_extended",
                ROOT / "scripts/erd-poc/metrics_extended.py",
            )
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            self._metrics_extended_mod = mod
        self.model = family_trainer.FamilyPrior(
            len(self.feature_names),
            len(self.action_names),
            int(ckpt["hidden"]),
            int(ckpt["layers"]),
            float(ckpt["dropout"]),
        )
        self.model.load_state_dict(ckpt["model"])
        self.model.eval()

    def top_actions(
        self,
        node_static: np.ndarray,
        node_static_names: list[str],
        edges: np.ndarray,
        positions: np.ndarray,
        metrics: object,
        top_n: int,
        dim_boost_weight: float = 0.0,
    ) -> list[tuple[str, float]]:
        metric_row = [
            float(metrics.cross),
            float(metrics.overlaps),
            float(getattr(metrics, "edge_node", 0)),
            float(metrics.bbox_b),
            float(metrics.score),
        ]
        if (self.has_extended or self.has_composite) and self._metrics_extended_mod is not None:
            ext = self._metrics_extended_mod.measure_extended(
                positions.astype(np.float64),
                edges,
                self.widths,
                self.heights,
                self.cluster_ids,
            )
            if self.has_extended:
                metric_row.extend([
                    ext.stress_score,
                    ext.edge_length_cv,
                    ext.crossing_angle_mean,
                    ext.crossing_angle_cv,
                    ext.hub_clearance_p10,
                    ext.cluster_compactness_mean,
                ])
            if self.has_composite:
                metric_row.extend([
                    ext.composite_quality,
                    ext.sub_clean_quality,
                    ext.sub_severity_quality,
                    ext.sub_angle_quality,
                    ext.sub_stress_quality,
                    ext.sub_compact_quality,
                    ext.sub_uniform_quality,
                    ext.sub_spread_quality,
                ])
        metric_values = np.array(metric_row, dtype=np.float32)
        values = family_trainer.state_feature_values(
            node_static,
            node_static_names,
            edges,
            positions.astype(np.float32),
            metric_values,
            self.has_extended,
            self.has_composite,
        )
        row_values = {
            name: float(value)
            for name, value in zip(
                family_trainer.state_feature_names(
                    node_static_names, self.has_extended, self.has_composite
                ),
                values,
            )
        }
        row = np.array(
            [row_values.get(name, 0.0) for name in self.feature_names],
            dtype=np.float32,
        )
        x = ((row - self.mean) / self.std).astype(np.float32)
        with torch.no_grad():
            pred_gain, pred_accept = self.model(torch.from_numpy(x.reshape(1, -1)))
        gain = torch.sinh(pred_gain).cpu().numpy().reshape(-1) * self.gain_scale
        prob = torch.sigmoid(pred_accept).cpu().numpy().reshape(-1)
        score = gain + prob * self.gain_scale
        # Dim-boost: when enabled, each composite sub-score's gap from
        # 1.0 boosts the priors of its associated action families. This
        # makes the worst-quality dimension steer the search even when
        # v37's predicted gains converge on a single dominant family.
        if (
            dim_boost_weight > 0.0
            and (self.has_extended or self.has_composite)
            and self._metrics_extended_mod is not None
        ):
            ext = self._metrics_extended_mod.measure_extended(
                positions.astype(np.float64),
                edges,
                self.widths,
                self.heights,
                self.cluster_ids,
            )
            sub_gaps = {
                "sub_clean": 1.0 - float(ext.sub_clean_quality),
                "sub_severity": 1.0 - float(ext.sub_severity_quality),
                "sub_angle": 1.0 - float(ext.sub_angle_quality),
                "sub_stress": 1.0 - float(ext.sub_stress_quality),
                "sub_compact": 1.0 - float(ext.sub_compact_quality),
                "sub_uniform": 1.0 - float(ext.sub_uniform_quality),
                "sub_spread": 1.0 - float(ext.sub_spread_quality),
            }
            name_to_idx = {n: i for i, n in enumerate(self.action_names)}
            for sub_name, gap in sub_gaps.items():
                if gap <= 0.0:
                    continue
                for fam in SUB_TO_FAMILIES.get(sub_name, []):
                    idx = name_to_idx.get(fam)
                    if idx is not None:
                        score[idx] += dim_boost_weight * gap
        order = np.argsort(-score)[: max(1, min(top_n, len(self.action_names)))]
        return [(self.action_names[int(idx)], float(score[int(idx)])) for idx in order]


def add_candidate_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--top-nodes", type=int, default=60)
    parser.add_argument("--top-edges", type=int, default=60)
    parser.add_argument("--top-clusters", type=int, default=70)
    parser.add_argument("--steps", default="75,150,300,600,1000,1800,3000")
    parser.add_argument("--anchor-radii", default="75,150,300,600,1000,1800,3000,5000")
    parser.add_argument("--cross-pair-candidates", type=int, default=0)
    parser.add_argument("--cross-pair-steps", default="")
    parser.add_argument("--cross-fan-edges", type=int, default=0)
    parser.add_argument("--cross-fan-steps", default="")
    parser.add_argument("--cross-group-fan-groups", type=int, default=0)
    parser.add_argument("--cross-group-fan-max-size", type=int, default=120)
    parser.add_argument("--cross-group-fan-steps", default="")
    parser.add_argument("--cross-carrier-pair-candidates", type=int, default=0)
    parser.add_argument("--cross-carrier-pair-max-size", type=int, default=120)
    parser.add_argument("--cross-carrier-pair-steps", default="")
    parser.add_argument("--cross-long-edge-anchors", type=int, default=0)
    parser.add_argument("--cross-long-edge-max-group-size", type=int, default=120)
    parser.add_argument("--cross-long-edge-radii", default="")
    parser.add_argument("--cross-long-edge-min-crossings", type=int, default=8)
    parser.add_argument("--edge-node-relief-hits", type=int, default=0)
    parser.add_argument("--edge-node-relief-max-group-size", type=int, default=120)
    parser.add_argument("--edge-node-relief-steps", default="")
    parser.add_argument("--edge-node-precise-hits", type=int, default=0)
    parser.add_argument("--edge-node-precise-neighborhood-max-nodes", type=int, default=48)
    parser.add_argument("--edge-node-precise-pads", default="0,60,140,260")
    parser.add_argument("--edge-node-clear-padding", type=float, default=32.0)
    parser.add_argument("--edge-node-corridor-edges", type=int, default=0)
    parser.add_argument("--edge-node-corridor-blockers", type=int, default=12)
    parser.add_argument("--edge-node-corridor-pads", default="0,45,100,180,300")
    parser.add_argument("--edge-node-force-hits", type=int, default=0)
    parser.add_argument("--edge-node-force-nodes", type=int, default=128)
    parser.add_argument("--edge-node-force-scales", default="0.25,0.45,0.7,1.0,1.35")
    parser.add_argument("--edge-node-force-padding", type=float, default=55.0)
    parser.add_argument("--cross-hotspot-bypass-hotspots", type=int, default=0)
    parser.add_argument("--cross-hotspot-bypass-edges", type=int, default=8)
    parser.add_argument("--cross-hotspot-bypass-max-size", type=int, default=80)
    parser.add_argument("--cross-hotspot-bypass-cell-size", type=float, default=5000.0)
    parser.add_argument("--cross-hotspot-bypass-steps", default="")
    parser.add_argument("--cross-partner-orbit-edges", type=int, default=0)
    parser.add_argument("--cross-partner-radii", default="150,300,600,1000,1800,3000,5000,8000")
    parser.add_argument("--cross-endpoint-swap-pairs", type=int, default=0)
    parser.add_argument("--swap-pairs", type=int, default=80)
    parser.add_argument("--cluster-swap-pairs", type=int, default=300)
    parser.add_argument("--overlap-candidates", type=int, default=120)
    parser.add_argument("--overlap-spread-components", type=int, default=60)
    parser.add_argument("--overlap-spread-spacing-scale", type=float, default=1.4)
    parser.add_argument("--overlap-spread-rotations", type=int, default=4)
    parser.add_argument("--overlap-spread-batch-sizes", default="8,16,32")
    parser.add_argument("--loose-pack-groups", type=int, default=30)
    parser.add_argument("--loose-pack-spacing-scale", type=float, default=2.0)
    parser.add_argument("--loose-pack-rotations", type=int, default=4)
    parser.add_argument("--loose-anchor-nodes", type=int, default=120)
    parser.add_argument("--loose-anchor-radii", default="150,300,600,1000,1800,3000,5000")
    parser.add_argument("--semantic-anchor-nodes", type=int, default=30)
    parser.add_argument("--semantic-anchor-top-clusters", type=int, default=2)
    parser.add_argument("--semantic-anchor-max-degree", type=int, default=0)
    parser.add_argument("--semantic-anchor-max-group-size", type=int, default=160)
    parser.add_argument("--semantic-anchor-min-score", type=float, default=0.35)
    parser.add_argument("--semantic-anchor-radii", default="0,250,600")
    parser.add_argument("--group-anchor-groups", type=int, default=180)
    parser.add_argument("--group-anchor-max-size", type=int, default=100)
    parser.add_argument("--group-anchor-radii", default="0,150,300,600,1000,1800,3000,5000,8000")
    parser.add_argument("--component-anchor-components", type=int, default=70)
    parser.add_argument("--component-anchor-max-group-size", type=int, default=8)
    parser.add_argument("--component-anchor-max-total-nodes", type=int, default=80)
    parser.add_argument("--component-anchor-radii", default="0,300,600,1000,1800,3000,5000,8000")
    parser.add_argument("--bundle-orbit-groups", type=int, default=28)
    parser.add_argument("--bundle-orbit-rotations", type=int, default=8)
    parser.add_argument("--bundle-orbit-radius-scales", default="0.8,1.0,1.3")
    parser.add_argument("--final-max-candidates", type=int, default=4000)
    parser.add_argument("--final-per-action-candidates", type=int, default=0)


def measure_candidate(
    positions: np.ndarray,
    moved: np.ndarray,
    cand: object,
    metrics: object,
    current_cross_flags: np.ndarray,
    evaluator: object,
    widths: np.ndarray,
    heights: np.ndarray,
    active_mask: np.ndarray,
    overlap_pairs: tuple[np.ndarray, np.ndarray],
    current_overlap_flags: np.ndarray,
    edge_node_pairs: tuple[np.ndarray, np.ndarray],
    current_edge_node_flags: np.ndarray | None,
    edge_node_active_nodes: np.ndarray,
    args: argparse.Namespace,
    collision_geometry: object | None = None,
    current_rect_positions: np.ndarray | None = None,
    composite_context: object | None = None,
) -> object:
    return v34.measure_candidate_incremental(
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
        edge_node_pairs=edge_node_pairs,
        current_edge_node_flags=current_edge_node_flags,
        edge_node_active_nodes=edge_node_active_nodes,
        edge_node_margin=args.edge_node_margin,
        edge_node_weight=args.edge_node_weight,
        collision_geometry=collision_geometry,
        current_rect_positions=current_rect_positions,
        composite_context=composite_context,
    )


def is_admissible_move(
    current_metrics: object,
    candidate_metrics: object,
    args: argparse.Namespace,
) -> bool:
    """Behavior gate for generic layout actions.

    The learned scorer is a filter, not the authority. A move must be
    non-regressive on the observable layout behaviors we can measure cheaply:
    node overlaps, edge-node intersections, edge crossings, and footprint.
    This prevents scalar-weight artifacts such as accepting a large crossing
    increase just because one overlap disappears.
    """
    if candidate_metrics.overlaps > current_metrics.overlaps + args.max_overlap_regression:
        return False
    if (
        candidate_metrics.edge_node
        > current_metrics.edge_node + args.max_edge_node_regression
    ):
        return False
    if candidate_metrics.cross > current_metrics.cross + args.max_cross_regression:
        return False
    if args.max_bbox_growth > 0:
        max_bbox = current_metrics.bbox_b * args.max_bbox_growth
        if candidate_metrics.bbox_b > max_bbox:
            return False
    return candidate_metrics.score < current_metrics.score


def rigid_measure_positions(
    layout: dict,
    positions: np.ndarray,
    args: argparse.Namespace,
) -> dict[str, float]:
    with tempfile.TemporaryDirectory(prefix="v35-rigid-verify-") as tmp:
        tmp_path = Path(tmp)
        positions_tsv = tmp_path / "positions.tsv"
        out_json = tmp_path / "layout.json"
        v34.write_positions_tsv(positions_tsv, layout, positions)
        if args.renderer_verify_mode == "postpass":
            nodes_tsv, edges_tsv = v34.write_graph_tsvs(layout, tmp_path)
            env = os.environ.copy()
            env["DJERD_SKIP_CG_OPT"] = "1"
            env["DJERD_KNOT_2NDPASS"] = "1"
            proc = subprocess.run(
                [
                    str(v34.BINARY),
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
                ],
                capture_output=True,
                text=True,
                env=env,
            )
            if proc.returncode != 0:
                raise RuntimeError(proc.stderr[:800])
            out_json.write_text(proc.stdout)
            meta = json.loads(proc.stdout).get("engineMetadata", {})
        else:
            meta = v34.run_rigid_measure(positions_tsv, out_json, layout)
    edge_cross = int(meta.get("edgeCrossings") or 0)
    edge_node = int(meta.get("edgeNodeIntersections") or 0)
    bundle_edge = int(meta.get("bundleEdgeIntersections") or 0)
    bundle_node = int(meta.get("bundleNodeOverlaps") or 0)
    visual_cross = int(
        meta.get("visualCrossings")
        or (edge_cross + edge_node + bundle_edge + bundle_node)
    )
    edge_plus_node = edge_cross + edge_node
    node_overlaps = int(meta.get("nodeOverlaps") or 0)
    bbox_b = float(meta.get("boundingBoxArea") or 0.0) / 1e9
    if args.rigid_score_objective == "edge-cross":
        objective_cross = edge_cross
    elif args.rigid_score_objective == "edge-plus-node":
        objective_cross = edge_plus_node
    else:
        objective_cross = visual_cross
    score = v34.score_metrics(
        objective_cross,
        node_overlaps,
        bbox_b,
        args.overlap_weight,
        args.bbox_weight,
        args.bbox_target_b,
        edge_node=0,
        edge_node_weight=0.0,
    )
    return {
        "edgeCross": float(edge_cross),
        "edgeNode": float(edge_node),
        "bundleEdge": float(bundle_edge),
        "bundleNode": float(bundle_node),
        "visualCross": float(visual_cross),
        "edgePlusNode": float(edge_plus_node),
        "nodeOverlaps": float(node_overlaps),
        "bboxB": float(bbox_b),
        "score": float(score),
    }


def is_admissible_rigid_move(
    current: dict[str, float],
    candidate: dict[str, float],
    args: argparse.Namespace,
) -> bool:
    if candidate["nodeOverlaps"] > current["nodeOverlaps"] + args.max_overlap_regression:
        return False
    if candidate["edgeNode"] > current["edgeNode"] + args.max_edge_node_regression:
        return False
    if candidate["edgeCross"] > current["edgeCross"] + args.max_cross_regression:
        return False
    if args.max_bbox_growth > 0:
        max_bbox = current["bboxB"] * args.max_bbox_growth
        if candidate["bboxB"] > max_bbox:
            return False
    return candidate["score"] < current["score"]


def hotspot_pressure(
    positions: np.ndarray,
    evaluator: object,
    cell_size: float,
    top_cells: int,
    cross_flags: np.ndarray | None = None,
) -> int:
    if top_cells <= 0 or cell_size <= 0:
        return 0
    flags = (
        v34.crossing_flags_for_pairs(positions, evaluator)
        if cross_flags is None
        else cross_flags
    )
    if not np.any(flags):
        return 0
    points = v34.crossing_points_for_pairs(positions, evaluator, flags)
    if points.shape[0] == 0:
        return 0
    cells = np.floor(points / float(cell_size)).astype(np.int64)
    _uniq, counts = np.unique(cells, axis=0, return_counts=True)
    counts.sort()
    return int(counts[-top_cells:].sum())


def passes_hotspot_pressure_gate(
    cand: object,
    moved_positions: np.ndarray,
    evaluator: object,
    current_pressure: int,
    args: argparse.Namespace,
) -> bool:
    if getattr(cand, "kind", "") != "cross_hotspot_endpoint_bypass":
        return True
    if args.cross_hotspot_bypass_max_pressure_growth < 0:
        return True
    candidate_pressure = hotspot_pressure(
        moved_positions,
        evaluator,
        args.cross_hotspot_bypass_cell_size,
        args.cross_hotspot_bypass_pressure_top_cells,
    )
    return (
        candidate_pressure
        <= current_pressure + args.cross_hotspot_bypass_max_pressure_growth
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, default=ROOT / "data/erd-poc/layouts/real-main.json")
    parser.add_argument("--positions", type=Path, required=True)
    parser.add_argument("--ckpt", type=Path, default=ROOT / "data/erd-poc/checkpoints/v35-graph-action-scorer.pt")
    parser.add_argument("--family-prior-ckpt", type=Path, default=None)
    parser.add_argument("--family-prior-top-actions", type=int, default=0)
    parser.add_argument("--family-prior-min-candidates", type=int, default=100)
    parser.add_argument("--family-prior-always-actions", default="")
    parser.add_argument(
        "--composite-log",
        action="store_true",
        help=(
            "Compute Bennett-weighted composite quality + 7 sub-scores at "
            "the initial state and after every round. Lets the run log show "
            "which quality dimensions move even when v34's score gain is "
            "saturated at its overlap floor."
        ),
    )
    parser.add_argument(
        "--dim-boost-weight",
        type=float,
        default=0.0,
        help=(
            "When > 0, the family-prior top_actions step boosts each "
            "action family's score by dim_boost_weight * (1 - sub_score) "
            "for the sub-scores it functionally addresses (SUB_TO_FAMILIES "
            "map). Steers candidate selection toward families that fix "
            "the worst composite quality dimension."
        ),
    )
    parser.add_argument(
        "--composite-weight",
        type=float,
        default=0.0,
        help=(
            "When > 0, the v34 score formula gains a composite term: "
            "composite_weight * (1 - composite_quality). Rebalances the "
            "cost away from overlap_weight's discrete floor toward "
            "continuous visual quality. ~1000x slower per measure call "
            "(O(E²) crossing detection per candidate)."
        ),
    )
    parser.add_argument("--out-tsv", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=8)
    parser.add_argument("--ml-top-k", type=int, default=50)
    parser.add_argument("--per-action-k", type=int, default=0)
    parser.add_argument("--score-batch-size", type=int, default=2048)
    parser.add_argument("--min-gain", type=float, default=1.0)
    parser.add_argument("--count-bundle-nodes", action="store_true")
    parser.add_argument("--overlap-margin", type=float, default=16.0)
    parser.add_argument("--overlap-weight", type=float, default=5000.0)
    parser.add_argument("--edge-node-margin", type=float, default=0.0)
    parser.add_argument("--edge-node-weight", type=float, default=0.5)
    parser.add_argument("--bbox-weight", type=float, default=800.0)
    parser.add_argument("--bbox-target-b", type=float, default=4.0)
    parser.add_argument(
        "--max-cross-regression",
        type=int,
        default=0,
        help="reject accepted moves whose exact crossing count increases by more than this",
    )
    parser.add_argument(
        "--max-overlap-regression",
        type=int,
        default=0,
        help="reject accepted moves whose exact node-overlap count increases by more than this",
    )
    parser.add_argument(
        "--max-edge-node-regression",
        type=int,
        default=0,
        help="reject accepted moves whose exact edge-node intersection count increases by more than this",
    )
    parser.add_argument(
        "--max-bbox-growth",
        type=float,
        default=1.05,
        help="reject accepted moves whose bbox grows beyond this multiple; <=0 disables",
    )
    parser.add_argument(
        "--cross-hotspot-bypass-pressure-top-cells",
        type=int,
        default=8,
        help="top crossing-density cells used by the hotspot-bypass pressure gate",
    )
    parser.add_argument(
        "--cross-hotspot-bypass-max-pressure-growth",
        type=int,
        default=-1,
        help=(
            "reject hotspot-bypass moves that increase top-cell crossing "
            "pressure by more than this; <0 disables"
        ),
    )
    parser.add_argument(
        "--advance",
        choices=["ml", "full"],
        default="ml",
        help="which exact-verified move to apply after each comparison",
    )
    parser.add_argument(
        "--no-compare-full",
        action="store_true",
        help="skip full exact verification and verify only the ML shortlist",
    )
    parser.add_argument(
        "--rigid-verify-top-k",
        type=int,
        default=0,
        help=(
            "after cheap ML shortlist verification, run the actual rigid renderer "
            "on this many best shortlist moves and accept only renderer-improving moves"
        ),
    )
    parser.add_argument(
        "--rigid-verify-pool",
        choices=["admissible", "selected"],
        default="admissible",
        help=(
            "which ML shortlist candidates to send to rigid verification: "
            "cheap-metric admissible moves only, or the selected ML shortlist itself"
        ),
    )
    parser.add_argument(
        "--rigid-verify-selected-order",
        choices=["rank", "score", "cross"],
        default="rank",
        help="ordering used before rigid-checking the selected ML shortlist",
    )
    parser.add_argument(
        "--rigid-score-objective",
        choices=["visual-cross", "edge-cross", "edge-plus-node"],
        default="visual-cross",
        help="renderer metric used as the rigid verification score",
    )
    parser.add_argument(
        "--renderer-verify-mode",
        choices=["rigid", "postpass"],
        default="rigid",
        help="renderer path used for verification; postpass matches the under1000 replay path",
    )
    add_candidate_args(parser)
    args = parser.parse_args()
    if args.no_compare_full and args.advance == "full":
        parser.error("--advance full requires full comparison; remove --no-compare-full")

    layout = v34.load_layout(args.layout)
    args.layout_obj = layout
    positions = v34.read_positions_tsv(args.positions, layout)
    edges = v34.graph_edges(layout)
    widths, heights = v34.render_node_sizes(layout, edges)
    active_mask = v34.render_active_overlap_mask(layout, args.count_bundle_nodes)
    collision_geometry = v34.build_render_collision_geometry(
        layout,
        edges,
        args.count_bundle_nodes,
    )
    overlap_pairs = collision_geometry.overlap_pairs
    edge_node_active_nodes = np.zeros(0, dtype=np.int32)
    edge_node_pairs = collision_geometry.edge_node_pairs
    evaluator = v34.fce.FastCrossEval(edges, positions.shape[0])
    adjacency = v34.adjacency_from_edges(edges, positions.shape[0])
    base_clusters = v34.cluster_members(layout)
    pseudo_groups = v34.no_cluster_pseudo_groups(layout, edges)
    clusters = dict(base_clusters)
    clusters.update(pseudo_groups)
    cluster_mask = np.array(
        [bool(nd.get("clusterId") or "") for nd in layout["nodes"]],
        dtype=bool,
    )
    node_static, node_static_names = ds.node_static_features(layout, edges)
    scorer = LiveScorer(args.ckpt)
    family_prior_cluster_ids = [
        str(nd.get("clusterId") or "") for nd in layout["nodes"]
    ]
    composite_context = None
    if args.composite_weight > 0.0:
        composite_context = v34.CompositeContext(
            widths=widths,
            heights=heights,
            cluster_ids=family_prior_cluster_ids,
            weight=args.composite_weight,
        )
    family_prior = (
        LiveFamilyPrior(
            args.family_prior_ckpt,
            widths=widths,
            heights=heights,
            cluster_ids=family_prior_cluster_ids,
        )
        if args.family_prior_ckpt is not None and args.family_prior_top_actions > 0
        else None
    )
    family_prior_always = {
        item.strip()
        for item in args.family_prior_always_actions.split(",")
        if item.strip()
    }
    metrics = v34.measure(
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
        edge_node_pairs=edge_node_pairs,
        edge_node_margin=args.edge_node_margin,
        edge_node_weight=args.edge_node_weight,
        collision_geometry=collision_geometry,
        composite_context=composite_context,
    )
    print(
        f"initial cross={metrics.cross} overlaps={metrics.overlaps} "
        f"edgeNode={metrics.edge_node} visualCross={metrics.visual_cross} "
        f"bbox={metrics.bbox_b:.2f}B "
        f"score={metrics.score:.1f}",
        flush=True,
    )
    rigid_metrics = None
    if args.rigid_verify_top_k > 0:
        rigid_metrics = rigid_measure_positions(layout, positions, args)
        print(
            "initialRigid "
            f"edgeCross={int(rigid_metrics['edgeCross'])} "
            f"edgeNode={int(rigid_metrics['edgeNode'])} "
            f"bundleEdge={int(rigid_metrics['bundleEdge'])} "
            f"bundleNode={int(rigid_metrics['bundleNode'])} "
            f"visualCross={int(rigid_metrics['visualCross'])} "
            f"nodeOverlaps={int(rigid_metrics['nodeOverlaps'])} "
            f"bbox={rigid_metrics['bboxB']:.2f}B "
            f"score={rigid_metrics['score']:.1f}",
            flush=True,
        )
    # Optional per-round composite quality logging. When enabled, we
    # compute the Bennett-weighted composite (+ 7 sub-scores) at each
    # round boundary so the run log shows exactly which quality
    # dimensions move, even when v34's score gain is at its overlap
    # floor (where multiple actions tie).
    composite_log = args.composite_log
    metrics_extended_mod = None
    if composite_log:
        import importlib.util as _ilu
        _spec = _ilu.spec_from_file_location(
            "metrics_extended",
            ROOT / "scripts/erd-poc/metrics_extended.py",
        )
        metrics_extended_mod = _ilu.module_from_spec(_spec)
        _spec.loader.exec_module(metrics_extended_mod)
        ext = metrics_extended_mod.measure_extended(
            positions.astype(np.float64),
            edges,
            widths,
            heights,
            family_prior_cluster_ids,
        )
        print(
            f"initialComposite quality={ext.composite_quality:.4f} "
            f"clean={ext.sub_clean_quality:.3f} sev={ext.sub_severity_quality:.3f} "
            f"ang={ext.sub_angle_quality:.3f} str={ext.sub_stress_quality:.3f} "
            f"cmp={ext.sub_compact_quality:.3f} uni={ext.sub_uniform_quality:.3f} "
            f"spr={ext.sub_spread_quality:.3f}",
            flush=True,
        )
        _prev_composite = ext.composite_quality
    started = time.time()
    accepted = 0
    total_full_gain = 0.0
    total_ml_gain = 0.0
    for round_idx in range(args.rounds):
        _cross, node_counts, edge_counts = v34.crossing_incident_counts(
            positions,
            evaluator,
        )
        current_cross_flags = v34.crossing_flags_for_pairs(positions, evaluator)
        current_rect_positions = v34.collision_positions(
            positions,
            collision_geometry,
        )
        current_overlap_flags = v34.overlap_flags_for_pairs(
            current_rect_positions,
            collision_geometry.rect_widths,
            collision_geometry.rect_heights,
            args.overlap_margin,
            overlap_pairs[0],
            overlap_pairs[1],
        )
        current_edge_node_flags = (
            v34.edge_rect_intersection_flags(
                positions,
                current_rect_positions,
                collision_geometry.rect_widths,
                collision_geometry.rect_heights,
                edges,
                edge_node_pairs[0],
                edge_node_pairs[1],
                args.edge_node_margin,
            )
            if (
                scorer.needs_edge_node_context
                or args.edge_node_relief_hits > 0
                or args.edge_node_precise_hits > 0
                or args.edge_node_corridor_edges > 0
                or args.edge_node_force_hits > 0
            )
            else None
        )
        allowed_action_types = None
        family_prior_text = "off"
        if family_prior is not None:
            ranked_actions = family_prior.top_actions(
                node_static,
                node_static_names,
                edges,
                positions,
                metrics,
                args.family_prior_top_actions,
                dim_boost_weight=args.dim_boost_weight,
            )
            allowed_action_types = {name for name, _score in ranked_actions}
            allowed_action_types.update(family_prior_always)
            family_prior_text = ",".join(name for name, _score in ranked_actions)

        candidates = ds.action_candidates(
            args,
            positions,
            node_counts,
            edge_counts,
            edges,
            adjacency,
            clusters,
            pseudo_groups,
            cluster_mask,
            widths,
            heights,
            active_mask,
            collision_geometry,
            current_edge_node_flags,
            allowed_action_types=allowed_action_types,
        )
        if (
            allowed_action_types is not None
            and len(candidates) < args.family_prior_min_candidates
        ):
            candidates = ds.action_candidates(
                args,
                positions,
                node_counts,
                edge_counts,
                edges,
                adjacency,
                clusters,
                pseudo_groups,
                cluster_mask,
                widths,
                heights,
                active_mask,
                collision_geometry,
                current_edge_node_flags,
                allowed_action_types=None,
            )
            family_prior_text += ":fallback"
        if not candidates:
            print(f"round {round_idx+1}: no candidates", flush=True)
            break
        rows = np.zeros((len(candidates), len(scorer.feature_names)), dtype=np.float32)
        moved_cache: list[np.ndarray] = []
        for idx, cand in enumerate(candidates):
            moved = v34.move_positions(positions, cand)
            moved_cache.append(moved)
            rows[idx] = scorer.row_from_candidate(
                layout,
                edges,
                metrics,
                cand,
                positions,
                moved,
                node_counts,
                edge_counts,
                evaluator,
                current_cross_flags,
                overlap_pairs,
                current_overlap_flags,
                node_static,
                node_static_names,
                edge_node_pairs,
                current_edge_node_flags,
                collision_geometry,
            )
        _pred_gain, pred_rank = scorer.score(rows, args.score_batch_size)
        ml_order = np.argsort(-pred_rank)
        selected = set(int(i) for i in ml_order[: min(args.ml_top_k, len(candidates))])
        if args.per_action_k > 0:
            by_action: dict[str, list[int]] = {}
            for idx, cand in enumerate(candidates):
                by_action.setdefault(v34.candidate_action_type(cand), []).append(idx)
            for members in by_action.values():
                members.sort(key=lambda idx: pred_rank[idx], reverse=True)
                selected.update(members[: args.per_action_k])

        ml_best_idx = -1
        ml_best_metrics = None
        selected_metrics: list[tuple[int, object]] = []
        ml_admissible_metrics: list[tuple[int, object]] = []
        full_best_idx = -1
        full_best_metrics = None
        ml_admissible = 0
        full_admissible = 0
        hotspot_pressure_rejected = 0
        current_hotspot_pressure = (
            hotspot_pressure(
                positions,
                evaluator,
                args.cross_hotspot_bypass_cell_size,
                args.cross_hotspot_bypass_pressure_top_cells,
                current_cross_flags,
            )
            if args.cross_hotspot_bypass_max_pressure_growth >= 0
            else 0
        )
        measure_idxs = (
            sorted(selected)
            if args.no_compare_full
            else list(range(len(candidates)))
        )
        for idx in measure_idxs:
            cand = candidates[idx]
            cand_metrics = measure_candidate(
                positions,
                moved_cache[idx],
                cand,
                metrics,
                current_cross_flags,
                evaluator,
                widths,
                heights,
                active_mask,
                overlap_pairs,
                current_overlap_flags,
                edge_node_pairs,
                current_edge_node_flags,
                edge_node_active_nodes,
                args,
                collision_geometry,
                current_rect_positions,
                composite_context=composite_context,
            )
            passes_pressure = passes_hotspot_pressure_gate(
                cand,
                moved_cache[idx],
                evaluator,
                current_hotspot_pressure,
                args,
            )
            if idx in selected and passes_pressure:
                selected_metrics.append((idx, cand_metrics))
            if not is_admissible_move(metrics, cand_metrics, args):
                continue
            if not passes_pressure:
                hotspot_pressure_rejected += 1
                continue
            if idx in selected:
                ml_admissible += 1
                ml_admissible_metrics.append((idx, cand_metrics))
            if not args.no_compare_full:
                full_admissible += 1
            if not args.no_compare_full and (
                full_best_metrics is None or cand_metrics.score < full_best_metrics.score
            ):
                full_best_idx = idx
                full_best_metrics = cand_metrics
            if idx in selected and (
                ml_best_metrics is None or cand_metrics.score < ml_best_metrics.score
            ):
                ml_best_idx = idx
                ml_best_metrics = cand_metrics
        if ml_best_metrics is None and args.rigid_verify_top_k <= 0:
            print(
                f"round {round_idx+1:02d}: candidates={len(candidates)} "
                f"topK={args.ml_top_k} selected={len(selected)} "
                f"mlAdmissible=0 fullAdmissible={full_admissible} "
                "no admissible ML move",
                flush=True,
            )
            break
        inverse_rank = np.empty(len(candidates), dtype=np.int32)
        inverse_rank[ml_order] = np.arange(len(candidates), dtype=np.int32)
        if args.no_compare_full:
            full_best_rank = -1
            full_action_rank = -1
            full_gain = float("nan")
            regret = float("nan")
            full_action = "not_measured"
        else:
            assert full_best_metrics is not None
            full_best_rank = int(inverse_rank[full_best_idx]) + 1
            full_action = v34.candidate_action_type(candidates[full_best_idx])
            action_members = [
                idx
                for idx, cand in enumerate(candidates)
                if v34.candidate_action_type(cand) == full_action
            ]
            action_members.sort(key=lambda idx: pred_rank[idx], reverse=True)
            full_action_rank = action_members.index(full_best_idx) + 1
            full_gain = metrics.score - full_best_metrics.score
            regret = full_gain - (metrics.score - ml_best_metrics.score)
        ml_gain = (
            metrics.score - ml_best_metrics.score
            if ml_best_metrics is not None
            else float("nan")
        )
        rigid_gain = float("nan")
        rigid_checked = 0
        rigid_admissible = 0
        rigid_best_idx = -1
        rigid_best_metrics = None
        if args.rigid_verify_top_k > 0:
            assert rigid_metrics is not None
            rigid_pool = (
                selected_metrics
                if args.rigid_verify_pool == "selected"
                else ml_admissible_metrics
            )
            if args.rigid_verify_pool == "selected":
                if args.rigid_verify_selected_order == "score":
                    rigid_pool.sort(key=lambda item: item[1].score)
                elif args.rigid_verify_selected_order == "cross":
                    rigid_pool.sort(key=lambda item: item[1].cross)
                else:
                    rigid_pool.sort(key=lambda item: pred_rank[item[0]], reverse=True)
            else:
                rigid_pool.sort(key=lambda item: item[1].score)
            for idx, _cheap_metrics in rigid_pool[: args.rigid_verify_top_k]:
                candidate_rigid = rigid_measure_positions(
                    layout,
                    moved_cache[idx],
                    args,
                )
                rigid_checked += 1
                if not is_admissible_rigid_move(rigid_metrics, candidate_rigid, args):
                    continue
                rigid_admissible += 1
                if (
                    rigid_best_metrics is None
                    or candidate_rigid["score"] < rigid_best_metrics["score"]
                ):
                    rigid_best_idx = idx
                    rigid_best_metrics = candidate_rigid
            if rigid_best_metrics is None:
                print(
                    f"round {round_idx+1:02d}: candidates={len(candidates)} "
                    f"topK={args.ml_top_k} selected={len(selected)} "
                    f"mlAdmissible={ml_admissible} "
                    f"rigidChecked={rigid_checked} rigidAdmissible=0 "
                    "no renderer-admissible ML move",
                    flush=True,
                )
                break
            rigid_gain = rigid_metrics["score"] - rigid_best_metrics["score"]
        if not args.no_compare_full:
            total_full_gain += max(0.0, float(full_gain))
        total_ml_gain += max(
            0.0,
            float(rigid_gain if args.rigid_verify_top_k > 0 else ml_gain),
        )
        ml_action = (
            v34.candidate_action_type(candidates[ml_best_idx])
            if ml_best_idx >= 0
            else "none"
        )
        rigid_action = (
            v34.candidate_action_type(candidates[rigid_best_idx])
            if rigid_best_idx >= 0
            else "not_measured"
        )
        print(
            f"round {round_idx+1:02d}: candidates={len(candidates)} "
            f"topK={args.ml_top_k} fullGain={full_gain:.1f} "
            f"mlGain={ml_gain:.1f} regret={regret:.1f} "
            f"rigidGain={rigid_gain:.1f} "
            f"selected={len(selected)} fullRank={full_best_rank} "
            f"fullActionRank={full_action_rank} "
            f"mlAdmissible={ml_admissible} "
            f"fullAdmissible={full_admissible} "
            f"rigidChecked={rigid_checked} "
            f"rigidAdmissible={rigid_admissible} "
            f"rigidPool={args.rigid_verify_pool} "
            f"hotspotPressureRejected={hotspot_pressure_rejected} "
            f"familyPrior={family_prior_text} "
            f"full={full_action} ml={ml_action} rigid={rigid_action} "
            + (
                "mlCross=nan mlOverlaps=nan mlEdgeNode=nan mlVisualCross=nan"
                if ml_best_metrics is None
                else (
                    f"mlCross={ml_best_metrics.cross} "
                    f"mlOverlaps={ml_best_metrics.overlaps} "
                    f"mlEdgeNode={ml_best_metrics.edge_node} "
                    f"mlVisualCross={ml_best_metrics.visual_cross}"
                )
            )
            + (
                ""
                if rigid_best_metrics is None
                else (
                    f" rigidEdgeCross={int(rigid_best_metrics['edgeCross'])} "
                    f"rigidEdgeNode={int(rigid_best_metrics['edgeNode'])} "
                    f"rigidBundleEdge={int(rigid_best_metrics['bundleEdge'])} "
                    f"rigidBundleNode={int(rigid_best_metrics['bundleNode'])} "
                    f"rigidVisualCross={int(rigid_best_metrics['visualCross'])}"
                )
            ),
            flush=True,
        )
        if args.rigid_verify_top_k > 0 and rigid_gain < args.min_gain:
            break
        if args.rigid_verify_top_k <= 0:
            if (args.advance == "ml" and ml_gain < args.min_gain) or (
                args.advance == "full"
                and not args.no_compare_full
                and full_gain < args.min_gain
            ):
                break
        chosen_idx = (
            rigid_best_idx
            if args.rigid_verify_top_k > 0
            else (ml_best_idx if args.advance == "ml" else full_best_idx)
        )
        positions = moved_cache[chosen_idx]
        metrics = v34.measure(
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
            edge_node_pairs=edge_node_pairs,
            edge_node_margin=args.edge_node_margin,
            edge_node_weight=args.edge_node_weight,
            collision_geometry=collision_geometry,
        ) if args.rigid_verify_top_k > 0 else (
            ml_best_metrics if args.advance == "ml" else full_best_metrics
        )
        if args.rigid_verify_top_k > 0:
            rigid_metrics = rigid_best_metrics
        accepted += 1
        if composite_log and metrics_extended_mod is not None:
            ext_round = metrics_extended_mod.measure_extended(
                positions.astype(np.float64),
                edges,
                widths,
                heights,
                family_prior_cluster_ids,
            )
            delta = ext_round.composite_quality - _prev_composite
            print(
                f"  compositeRound r={round_idx+1} "
                f"quality={ext_round.composite_quality:.4f} "
                f"delta={delta:+.4f} "
                f"clean={ext_round.sub_clean_quality:.3f} "
                f"sev={ext_round.sub_severity_quality:.3f} "
                f"ang={ext_round.sub_angle_quality:.3f} "
                f"str={ext_round.sub_stress_quality:.3f} "
                f"cmp={ext_round.sub_compact_quality:.3f}",
                flush=True,
            )
            _prev_composite = ext_round.composite_quality

    args.out_tsv.parent.mkdir(parents=True, exist_ok=True)
    v34.write_positions_tsv(args.out_tsv, layout, positions)
    captured = (
        f"{total_ml_gain:.1f}/{total_full_gain:.1f}"
        if not args.no_compare_full
        else f"{total_ml_gain:.1f}/not_measured"
    )
    print(
        f"done accepted={accepted} elapsed={time.time() - started:.1f}s "
        f"final cross={metrics.cross} overlaps={metrics.overlaps} "
        f"edgeNode={metrics.edge_node} visualCross={metrics.visual_cross} "
        f"bbox={metrics.bbox_b:.2f}B score={metrics.score:.1f} "
        + (
            ""
            if rigid_metrics is None
            else (
                f"rigidEdgeCross={int(rigid_metrics['edgeCross'])} "
                f"rigidEdgeNode={int(rigid_metrics['edgeNode'])} "
                f"rigidBundleEdge={int(rigid_metrics['bundleEdge'])} "
                f"rigidBundleNode={int(rigid_metrics['bundleNode'])} "
                f"rigidVisualCross={int(rigid_metrics['visualCross'])} "
                f"rigidScore={rigid_metrics['score']:.1f} "
            )
        )
        + f"capturedGain={captured} "
        f"out={args.out_tsv}",
        flush=True,
    )


if __name__ == "__main__":
    main()
