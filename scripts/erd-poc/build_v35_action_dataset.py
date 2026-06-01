#!/usr/bin/env python3
"""Build graph-aware action-scoring data from v34 candidates.

The output NPZ stores layout states once per round and candidate actions as
ragged affected-node lists plus per-node deltas. This avoids training on fixed
Captain coordinates only; the model can inspect what an action does to a node
set rather than memorizing a final layout.
"""

from __future__ import annotations

import argparse
import importlib.util
import math
import sys
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "v34_move_search", ROOT / "scripts/erd-poc/v34_move_search.py"
)
v34 = importlib.util.module_from_spec(SPEC)
sys.modules["v34_move_search"] = v34
SPEC.loader.exec_module(v34)

# Extended ML-signal metrics (optional, when --extended-metrics is set).
_EXT_SPEC = importlib.util.spec_from_file_location(
    "metrics_extended", ROOT / "scripts/erd-poc/metrics_extended.py"
)
metrics_extended = importlib.util.module_from_spec(_EXT_SPEC)
sys.modules["metrics_extended"] = metrics_extended
_EXT_SPEC.loader.exec_module(metrics_extended)


SCALAR_NAMES = [
    "graphNodes",
    "graphEdges",
    "baseCross",
    "baseVisualCross",
    "baseOverlaps",
    "baseEdgeNode",
    "baseBboxB",
    "baseScore",
    "candidateMoved",
    "candidateDx",
    "candidateDy",
    "candidatePriority",
    "candidateGroupIsLouvain",
    "candidateOtherGroupIsLouvain",
    "candidateSourceGroupIsLouvain",
    "candidateGroupIsPseudo",
    "candidateOtherGroupIsPseudo",
    "candidateGroupIsComponent",
    "candidateSemanticScore",
    "candidateSemanticTokenJaccard",
    "candidateSemanticSharedTokens",
    "candidateSemanticAppMatch",
    "candidateSemanticTargetSize",
    "candidateSemanticTargetCrossIncident",
    "candidateMeanCrossIncident",
    "candidateMaxCrossIncident",
    "candidateCentroidX",
    "candidateCentroidY",
    "candidateSpreadX",
    "candidateSpreadY",
    "candidateTargetDeltaX",
    "candidateTargetDeltaY",
    "candidateTargetMoveDistMean",
    "candidateTargetMoveDistMax",
    "candidateSwapDistance",
    "candidateOtherMeanCrossIncident",
    "contextIncidentEdgeCount",
    "contextFullyMovedEdgeCount",
    "contextPartiallyMovedEdgeCount",
    "contextIncidentCrossPairs",
    "contextIncidentCrossPairsBothIncident",
    "contextIncidentCrossOtherEdges",
    "contextIncidentCrossFrac",
    "contextIncidentEdgeCrossSum",
    "contextIncidentEdgeCrossMean",
    "contextIncidentEdgeCrossMax",
    "contextIncidentEdgeCrossDensity",
    "contextCurrentOverlapPairs",
    "contextCurrentOverlapFrac",
    "contextCurrentEdgeNodePairs",
    "contextCurrentEdgeNodeFrac",
    "contextEdgeLengthDeltaMean",
    "contextEdgeLengthDeltaMaxAbs",
    "contextEdgeLengthDeltaStd",
    "contextEdgeStretchMeanAbs",
    "contextEdgeStretchMaxAbs",
]


def parse_start(text: str) -> tuple[str, Path]:
    if "=" not in text:
        path = Path(text)
        return path.stem, path
    name, path = text.split("=", 1)
    return name, Path(path)


def finite(value: object, default: float = 0.0) -> float:
    try:
        got = float(value)
    except (TypeError, ValueError):
        return default
    return got if math.isfinite(got) else default


def node_static_features(layout: dict, edges: np.ndarray) -> tuple[np.ndarray, list[str]]:
    nodes = layout["nodes"]
    n = len(nodes)
    deg = np.zeros(n, dtype=np.float32)
    inter_deg = np.zeros(n, dtype=np.float32)
    cluster = [nd.get("clusterId") or "" for nd in nodes]
    for s_raw, t_raw in edges:
        s = int(s_raw)
        t = int(t_raw)
        deg[s] += 1.0
        deg[t] += 1.0
        if cluster[s] and cluster[t] and cluster[s] != cluster[t]:
            inter_deg[s] += 1.0
            inter_deg[t] += 1.0

    id_to_idx = {nd["modelId"]: idx for idx, nd in enumerate(nodes)}
    is_bundle_parent = np.zeros(n, dtype=np.float32)
    is_bundle_leaf = np.zeros(n, dtype=np.float32)
    bundle_size = np.zeros(n, dtype=np.float32)
    for bundle in (layout.get("engineMetadata") or {}).get("leafBundles") or []:
        size = float(len(bundle.get("leafModelIds") or []))
        parent = bundle.get("parentModelId")
        if parent in id_to_idx:
            idx = id_to_idx[parent]
            is_bundle_parent[idx] = 1.0
            bundle_size[idx] = size
        for leaf in bundle.get("leafModelIds") or []:
            if leaf in id_to_idx:
                idx = id_to_idx[leaf]
                is_bundle_leaf[idx] = 1.0
                bundle_size[idx] = size

    width_raw, height_raw = v34.render_node_sizes(layout, edges)
    width = width_raw.astype(np.float32)
    height = height_raw.astype(np.float32)
    feat = np.stack(
        [
            np.log1p(deg),
            (deg == 1).astype(np.float32),
            (deg >= 10).astype(np.float32),
            np.log1p(width) - 5.0,
            np.log1p(height) - 4.0,
            np.array([1.0 if c else 0.0 for c in cluster], dtype=np.float32),
            is_bundle_parent,
            is_bundle_leaf,
            np.log1p(bundle_size),
            (inter_deg > 0).astype(np.float32),
            np.log1p(inter_deg),
        ],
        axis=1,
    )
    names = [
        "logDegree",
        "isLeaf",
        "isHub",
        "logWidthCentered",
        "logHeightCentered",
        "isClustered",
        "isBundleParent",
        "isBundleLeaf",
        "logBundleSize",
        "isClusterBoundary",
        "logInterClusterDegree",
    ]
    return feat.astype(np.float32), names


def scalar_features(
    layout: dict,
    edges: np.ndarray,
    metrics: object,
    cand: object,
    positions: np.ndarray,
    node_counts: np.ndarray,
    edge_counts: np.ndarray,
    evaluator: object,
    current_cross_flags: np.ndarray,
    overlap_pairs: tuple[np.ndarray, np.ndarray],
    current_overlap_flags: np.ndarray,
    moved_positions: np.ndarray,
    edge_node_pairs: tuple[np.ndarray, np.ndarray] | None = None,
    current_edge_node_flags: np.ndarray | None = None,
    requested_names: set[str] | None = None,
    collision_geometry: object | None = None,
) -> np.ndarray:
    features = v34.candidate_features(cand, positions, node_counts)
    needs_edge_node_context = requested_names is None or bool(
        {
            "contextCurrentEdgeNodePairs",
            "contextCurrentEdgeNodeFrac",
        }
        & requested_names
    )
    context = action_context_features(
        cand,
        positions,
        moved_positions,
        edges,
        edge_counts,
        evaluator,
        current_cross_flags,
        overlap_pairs,
        current_overlap_flags,
        metrics.cross,
        metrics.overlaps,
        getattr(metrics, "edge_node", 0),
        edge_node_pairs if needs_edge_node_context else None,
        current_edge_node_flags if needs_edge_node_context else None,
        collision_geometry if needs_edge_node_context else None,
    )
    values = {
        "graphNodes": len(layout["nodes"]),
        "graphEdges": edges.shape[0],
        "baseCross": metrics.cross,
        "baseVisualCross": getattr(metrics, "visual_cross", metrics.cross + getattr(metrics, "edge_node", 0)),
        "baseOverlaps": metrics.overlaps,
        "baseEdgeNode": getattr(metrics, "edge_node", 0),
        "baseBboxB": metrics.bbox_b,
        "baseScore": metrics.score,
        "candidateMoved": features.get("moved"),
        "candidateDx": features.get("dx"),
        "candidateDy": features.get("dy"),
        "candidatePriority": features.get("priority"),
        "candidateGroupIsLouvain": features.get("groupIsLouvain"),
        "candidateOtherGroupIsLouvain": features.get("otherGroupIsLouvain"),
        "candidateSourceGroupIsLouvain": features.get("sourceGroupIsLouvain"),
        "candidateGroupIsPseudo": features.get("groupIsPseudo"),
        "candidateOtherGroupIsPseudo": features.get("otherGroupIsPseudo"),
        "candidateGroupIsComponent": features.get("groupIsComponent"),
        "candidateSemanticScore": features.get("semanticScore"),
        "candidateSemanticTokenJaccard": features.get("semanticTokenJaccard"),
        "candidateSemanticSharedTokens": features.get("semanticSharedTokens"),
        "candidateSemanticAppMatch": features.get("semanticAppMatch"),
        "candidateSemanticTargetSize": features.get("semanticTargetSize"),
        "candidateSemanticTargetCrossIncident": features.get(
            "semanticTargetCrossIncident"
        ),
        "candidateMeanCrossIncident": features.get("meanCrossIncident"),
        "candidateMaxCrossIncident": features.get("maxCrossIncident"),
        "candidateCentroidX": features.get("centroidX"),
        "candidateCentroidY": features.get("centroidY"),
        "candidateSpreadX": features.get("spreadX"),
        "candidateSpreadY": features.get("spreadY"),
        "candidateTargetDeltaX": features.get("targetDeltaX"),
        "candidateTargetDeltaY": features.get("targetDeltaY"),
        "candidateTargetMoveDistMean": features.get("targetMoveDistMean"),
        "candidateTargetMoveDistMax": features.get("targetMoveDistMax"),
        "candidateSwapDistance": features.get("swapDistance"),
        "candidateOtherMeanCrossIncident": features.get("otherMeanCrossIncident"),
        **context,
    }
    return np.array([finite(values.get(name)) for name in SCALAR_NAMES], dtype=np.float32)


def action_context_features(
    cand: object,
    positions: np.ndarray,
    moved_positions: np.ndarray,
    edges: np.ndarray,
    edge_counts: np.ndarray,
    evaluator: object,
    current_cross_flags: np.ndarray,
    overlap_pairs: tuple[np.ndarray, np.ndarray],
    current_overlap_flags: np.ndarray,
    base_cross: int,
    base_overlaps: int,
    base_edge_node: int = 0,
    edge_node_pairs: tuple[np.ndarray, np.ndarray] | None = None,
    current_edge_node_flags: np.ndarray | None = None,
    collision_geometry: object | None = None,
) -> dict[str, float]:
    if cand.other is not None:
        affected_raw = np.concatenate([cand.indices, cand.other]).astype(np.int32)
    else:
        affected_raw = cand.indices.astype(np.int32)
    if affected_raw.size == 0:
        return {}
    affected = np.unique(affected_raw)
    affected_mask = np.zeros(positions.shape[0], dtype=bool)
    affected_mask[affected] = True

    incident_edge_mask = affected_mask[edges[:, 0]] | affected_mask[edges[:, 1]]
    fully_moved_edge_mask = affected_mask[edges[:, 0]] & affected_mask[edges[:, 1]]
    incident_edges = np.flatnonzero(incident_edge_mask)
    incident_edge_count = int(incident_edges.shape[0])
    fully_moved_edge_count = int(fully_moved_edge_mask.sum())
    partially_moved_edge_count = incident_edge_count - fully_moved_edge_count

    if incident_edge_count > 0:
        incident_counts = edge_counts[incident_edges].astype(np.float64)
        edge_cross_sum = float(incident_counts.sum())
        edge_cross_mean = float(incident_counts.mean())
        edge_cross_max = float(incident_counts.max())
        cur_vec = positions[edges[incident_edges, 0]] - positions[edges[incident_edges, 1]]
        nxt_vec = (
            moved_positions[edges[incident_edges, 0]]
            - moved_positions[edges[incident_edges, 1]]
        )
        cur_len = np.linalg.norm(cur_vec, axis=1)
        nxt_len = np.linalg.norm(nxt_vec, axis=1)
        length_delta = nxt_len - cur_len
        stretch = np.abs(length_delta) / np.maximum(cur_len, 1.0)
        length_delta_mean = float(length_delta.mean())
        length_delta_max_abs = float(np.abs(length_delta).max())
        length_delta_std = float(length_delta.std())
        stretch_mean_abs = float(stretch.mean())
        stretch_max_abs = float(stretch.max())
    else:
        edge_cross_sum = 0.0
        edge_cross_mean = 0.0
        edge_cross_max = 0.0
        length_delta_mean = 0.0
        length_delta_max_abs = 0.0
        length_delta_std = 0.0
        stretch_mean_abs = 0.0
        stretch_max_abs = 0.0

    pair_incident = incident_edge_mask[evaluator.i] | incident_edge_mask[evaluator.j]
    pair_both = incident_edge_mask[evaluator.i] & incident_edge_mask[evaluator.j]
    incident_cross = current_cross_flags & pair_incident
    incident_cross_pairs = int(incident_cross.sum())
    incident_cross_pairs_both = int((current_cross_flags & pair_both).sum())
    if incident_cross_pairs > 0:
        crossed_edges = np.concatenate(
            [evaluator.i[incident_cross], evaluator.j[incident_cross]]
        )
        other_edges = crossed_edges[~incident_edge_mask[crossed_edges]]
        incident_cross_other_edges = int(np.unique(other_edges).shape[0])
    else:
        incident_cross_other_edges = 0

    pair_i, pair_j = overlap_pairs
    if pair_i.size:
        if collision_geometry is not None:
            affected_rects = v34.impacted_collision_rect_mask(
                affected,
                collision_geometry,
            )
            overlap_incident = affected_rects[pair_i] | affected_rects[pair_j]
        else:
            overlap_incident = affected_mask[pair_i] | affected_mask[pair_j]
        current_overlap_pairs = int((current_overlap_flags & overlap_incident).sum())
    else:
        current_overlap_pairs = 0

    if (
        edge_node_pairs is not None
        and current_edge_node_flags is not None
        and edge_node_pairs[0].size
    ):
        en_edge, en_node = edge_node_pairs
        if collision_geometry is not None:
            affected_rects = v34.impacted_collision_rect_mask(
                affected,
                collision_geometry,
            )
            edge_node_incident = affected_rects[en_node] | incident_edge_mask[en_edge]
        else:
            edge_node_incident = affected_mask[en_node] | incident_edge_mask[en_edge]
        current_edge_node_pairs = int(
            (current_edge_node_flags & edge_node_incident).sum()
        )
    else:
        current_edge_node_pairs = 0

    return {
        "contextIncidentEdgeCount": float(incident_edge_count),
        "contextFullyMovedEdgeCount": float(fully_moved_edge_count),
        "contextPartiallyMovedEdgeCount": float(partially_moved_edge_count),
        "contextIncidentCrossPairs": float(incident_cross_pairs),
        "contextIncidentCrossPairsBothIncident": float(incident_cross_pairs_both),
        "contextIncidentCrossOtherEdges": float(incident_cross_other_edges),
        "contextIncidentCrossFrac": float(incident_cross_pairs / max(1, base_cross)),
        "contextIncidentEdgeCrossSum": edge_cross_sum,
        "contextIncidentEdgeCrossMean": edge_cross_mean,
        "contextIncidentEdgeCrossMax": edge_cross_max,
        "contextIncidentEdgeCrossDensity": float(
            incident_cross_pairs / max(1, incident_edge_count)
        ),
        "contextCurrentOverlapPairs": float(current_overlap_pairs),
        "contextCurrentOverlapFrac": float(current_overlap_pairs / max(1, base_overlaps)),
        "contextCurrentEdgeNodePairs": float(current_edge_node_pairs),
        "contextCurrentEdgeNodeFrac": float(
            current_edge_node_pairs / max(1, base_edge_node)
        ),
        "contextEdgeLengthDeltaMean": length_delta_mean,
        "contextEdgeLengthDeltaMaxAbs": length_delta_max_abs,
        "contextEdgeLengthDeltaStd": length_delta_std,
        "contextEdgeStretchMeanAbs": stretch_mean_abs,
        "contextEdgeStretchMaxAbs": stretch_max_abs,
    }


def affected_nodes_and_deltas(
    cand: object,
    positions: np.ndarray,
    moved_positions: np.ndarray,
) -> tuple[np.ndarray, np.ndarray]:
    if cand.other is not None:
        affected = np.concatenate([cand.indices, cand.other]).astype(np.int32)
    else:
        affected = cand.indices.astype(np.int32)
    if affected.size == 0:
        return affected, np.zeros((0, 2), dtype=np.float32)
    uniq, first = np.unique(affected, return_index=True)
    affected = uniq[np.argsort(first)].astype(np.int32)
    deltas = (moved_positions[affected] - positions[affected]).astype(np.float32)
    return affected, deltas


def training_gain(current_metrics: object, candidate_metrics: object, args: argparse.Namespace) -> float:
    gain = float(current_metrics.score - candidate_metrics.score)
    if candidate_metrics.overlaps > current_metrics.overlaps + args.max_overlap_regression:
        return min(gain, -1.0)
    if candidate_metrics.edge_node > current_metrics.edge_node + args.max_edge_node_regression:
        return min(gain, -1.0)
    if candidate_metrics.cross > current_metrics.cross + args.max_cross_regression:
        return min(gain, -1.0)
    if args.max_bbox_growth > 0:
        max_bbox = current_metrics.bbox_b * args.max_bbox_growth
        if candidate_metrics.bbox_b > max_bbox:
            return min(gain, -1.0)
    return gain


def action_candidates(
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
    collision_geometry=None,
    current_edge_node_flags=None,
    allowed_action_types: set[str] | None = None,
):
    candidates = v34.generate_candidates(
        positions,
        node_counts,
        edge_counts,
        edges,
        adjacency,
        clusters,
        args.top_nodes,
        args.top_edges,
        args.top_clusters,
        v34.parse_steps(args.steps),
        v34.parse_steps(args.anchor_radii),
        args.swap_pairs,
        args.cluster_swap_pairs,
        0,
        None,
    )
    if getattr(args, "cross_pair_candidates", 0) > 0:
        cross_pair_steps_text = getattr(args, "cross_pair_steps", "")
        candidates.extend(
            v34.crossing_pair_candidates(
                positions,
                node_counts,
                edge_counts,
                edges,
                v34.fce.FastCrossEval(edges, positions.shape[0]),
                args.cross_pair_candidates,
                v34.parse_steps(cross_pair_steps_text)
                if cross_pair_steps_text.strip()
                else v34.parse_steps(args.steps),
            )
        )
    if getattr(args, "cross_fan_edges", 0) > 0:
        cross_fan_steps_text = getattr(args, "cross_fan_steps", "")
        candidates.extend(
            v34.crossing_fan_candidates(
                positions,
                edge_counts,
                edges,
                v34.fce.FastCrossEval(edges, positions.shape[0]),
                args.cross_fan_edges,
                v34.parse_steps(cross_fan_steps_text)
                if cross_fan_steps_text.strip()
                else v34.parse_steps(args.steps),
            )
        )
    if getattr(args, "cross_group_fan_groups", 0) > 0:
        cross_group_fan_steps_text = getattr(args, "cross_group_fan_steps", "")
        candidates.extend(
            v34.crossing_group_fan_candidates(
                positions,
                node_counts,
                edge_counts,
                edges,
                v34.fce.FastCrossEval(edges, positions.shape[0]),
                clusters,
                args.cross_group_fan_groups,
                args.cross_group_fan_max_size,
                v34.parse_steps(cross_group_fan_steps_text)
                if cross_group_fan_steps_text.strip()
                else v34.parse_steps(args.steps),
            )
        )
    if getattr(args, "cross_carrier_pair_candidates", 0) > 0:
        cross_carrier_pair_steps_text = getattr(args, "cross_carrier_pair_steps", "")
        candidates.extend(
            v34.crossing_carrier_pair_separation_candidates(
                positions,
                node_counts,
                edge_counts,
                edges,
                v34.fce.FastCrossEval(edges, positions.shape[0]),
                clusters,
                args.cross_carrier_pair_candidates,
                args.cross_carrier_pair_max_size,
                v34.parse_steps(cross_carrier_pair_steps_text)
                if cross_carrier_pair_steps_text.strip()
                else v34.parse_steps(args.steps),
            )
        )
    if getattr(args, "cross_long_edge_anchors", 0) > 0:
        cross_long_edge_radii_text = getattr(args, "cross_long_edge_radii", "")
        candidates.extend(
            v34.crossing_long_edge_anchor_candidates(
                positions,
                node_counts,
                edge_counts,
                edges,
                clusters,
                args.cross_long_edge_anchors,
                args.cross_long_edge_max_group_size,
                v34.parse_steps(cross_long_edge_radii_text)
                if cross_long_edge_radii_text.strip()
                else v34.parse_steps(args.anchor_radii),
                v34.unit_directions(),
                args.cross_long_edge_min_crossings,
            )
        )
    if (
        getattr(args, "edge_node_relief_hits", 0) > 0
        and collision_geometry is not None
        and current_edge_node_flags is not None
    ):
        edge_node_relief_steps_text = getattr(args, "edge_node_relief_steps", "")
        candidates.extend(
            v34.edge_node_relief_candidates(
                positions,
                node_counts,
                edge_counts,
                edges,
                collision_geometry,
                current_edge_node_flags,
                clusters,
                args.edge_node_relief_hits,
                args.edge_node_relief_max_group_size,
                v34.parse_steps(edge_node_relief_steps_text)
                if edge_node_relief_steps_text.strip()
                else v34.parse_steps(args.steps),
            )
        )
    if (
        getattr(args, "edge_node_precise_hits", 0) > 0
        and collision_geometry is not None
        and current_edge_node_flags is not None
    ):
        candidates.extend(
            v34.edge_node_precise_clear_candidates(
                positions,
                node_counts,
                edge_counts,
                edges,
                collision_geometry,
                current_edge_node_flags,
                args.edge_node_precise_hits,
                v34.parse_steps(args.edge_node_precise_pads),
                max_neighborhood_nodes=args.edge_node_precise_neighborhood_max_nodes,
                edge_node_margin=args.edge_node_margin,
                clear_padding=args.edge_node_clear_padding,
            )
        )
    if (
        getattr(args, "edge_node_corridor_edges", 0) > 0
        and collision_geometry is not None
        and current_edge_node_flags is not None
    ):
        candidates.extend(
            v34.edge_node_corridor_clear_candidates(
                positions,
                node_counts,
                edge_counts,
                edges,
                collision_geometry,
                current_edge_node_flags,
                max_edges=args.edge_node_corridor_edges,
                max_blockers_per_edge=args.edge_node_corridor_blockers,
                pads=v34.parse_steps(args.edge_node_corridor_pads),
                edge_node_margin=args.edge_node_margin,
                clear_padding=args.edge_node_clear_padding,
            )
        )
    if (
        getattr(args, "edge_node_force_hits", 0) > 0
        and collision_geometry is not None
        and current_edge_node_flags is not None
    ):
        candidates.extend(
            v34.edge_node_force_relax_candidates(
                positions,
                node_counts,
                edge_counts,
                edges,
                collision_geometry,
                current_edge_node_flags,
                max_hits=args.edge_node_force_hits,
                max_nodes=args.edge_node_force_nodes,
                scales=v34.parse_steps(args.edge_node_force_scales),
                edge_node_margin=args.edge_node_margin,
                clear_padding=args.edge_node_force_padding,
            )
        )
    if getattr(args, "cross_hotspot_bypass_hotspots", 0) > 0:
        cross_hotspot_bypass_steps_text = getattr(args, "cross_hotspot_bypass_steps", "")
        candidates.extend(
            v34.crossing_hotspot_endpoint_bypass_candidates(
                positions,
                node_counts,
                edge_counts,
                edges,
                v34.fce.FastCrossEval(edges, positions.shape[0]),
                clusters,
                args.cross_hotspot_bypass_hotspots,
                args.cross_hotspot_bypass_edges,
                args.cross_hotspot_bypass_max_size,
                args.cross_hotspot_bypass_cell_size,
                v34.parse_steps(cross_hotspot_bypass_steps_text)
                if cross_hotspot_bypass_steps_text.strip()
                else v34.parse_steps(args.steps),
            )
        )
    if getattr(args, "cross_partner_orbit_edges", 0) > 0:
        candidates.extend(
            v34.crossing_endpoint_partner_orbit_candidates(
                positions,
                edge_counts,
                edges,
                args.cross_partner_orbit_edges,
                v34.parse_steps(args.cross_partner_radii),
                v34.unit_directions(),
            )
        )
    if getattr(args, "cross_endpoint_swap_pairs", 0) > 0:
        candidates.extend(
            v34.crossing_pair_endpoint_swap_candidates(
                positions,
                edge_counts,
                edges,
                v34.fce.FastCrossEval(edges, positions.shape[0]),
                args.cross_endpoint_swap_pairs,
            )
        )
    if args.overlap_candidates > 0:
        candidates.extend(
            v34.generate_overlap_candidates(
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
            v34.overlap_component_spread_candidates(
                positions,
                widths,
                heights,
                node_counts,
                active_mask,
                args.overlap_margin,
                args.overlap_spread_components,
                args.overlap_spread_spacing_scale,
                args.overlap_spread_rotations,
                [int(x) for x in args.overlap_spread_batch_sizes.split(",") if x],
            )
        )
    if args.loose_pack_groups > 0:
        candidates.extend(
            v34.loose_pack_candidates(
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
            v34.loose_node_anchor_candidates(
                positions,
                node_counts,
                adjacency,
                cluster_mask,
                args.loose_anchor_nodes,
                v34.parse_steps(args.loose_anchor_radii),
                v34.unit_directions(),
            )
        )
    if args.semantic_anchor_nodes > 0:
        candidates.extend(
            v34.semantic_anchor_candidates(
                args.layout_obj,
                positions,
                node_counts,
                adjacency,
                clusters,
                args.semantic_anchor_nodes,
                args.semantic_anchor_top_clusters,
                args.semantic_anchor_max_degree,
                args.semantic_anchor_max_group_size,
                args.semantic_anchor_min_score,
                v34.parse_steps(args.semantic_anchor_radii),
                v34.unit_directions(),
            )
        )
    if args.group_anchor_groups > 0:
        candidates.extend(
            v34.group_anchor_candidates(
                positions,
                node_counts,
                adjacency,
                clusters,
                args.group_anchor_groups,
                args.group_anchor_max_size,
                v34.parse_steps(args.group_anchor_radii),
                v34.unit_directions(),
            )
        )
    if args.component_anchor_components > 0:
        candidates.extend(
            v34.component_anchor_candidates(
                positions,
                node_counts,
                edges,
                adjacency,
                clusters,
                args.component_anchor_components,
                args.component_anchor_max_group_size,
                args.component_anchor_max_total_nodes,
                v34.parse_steps(args.component_anchor_radii),
                v34.unit_directions(),
            )
        )
    if args.bundle_orbit_groups > 0:
        candidates.extend(
            v34.bundle_orbit_candidates(
                positions,
                widths,
                heights,
                node_counts,
                args.layout_obj,
                args.bundle_orbit_groups,
                args.bundle_orbit_rotations,
                v34.parse_steps(args.bundle_orbit_radius_scales),
            )
        )
    if allowed_action_types:
        filtered = [
            cand
            for cand in candidates
            if v34.candidate_action_type(cand) in allowed_action_types
        ]
        if filtered:
            candidates = filtered

    return v34.truncate_candidates(
        candidates,
        node_counts,
        args.final_max_candidates,
        getattr(args, "final_per_action_candidates", 0),
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, default=ROOT / "data/erd-poc/layouts/real-main.json")
    parser.add_argument("--start", action="append", default=[])
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=8)
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
    parser.add_argument("--count-bundle-nodes", action="store_true")
    parser.add_argument("--overlap-margin", type=float, default=16.0)
    parser.add_argument("--overlap-weight", type=float, default=5000.0)
    parser.add_argument("--edge-node-margin", type=float, default=0.0)
    # edge_node multiplier inside score_metrics (v34_move_search). 0 falls
    # back to the legacy 1x weight (edge_node folded into visual_cross). >0
    # amplifies the edge-node penalty so the learned policy avoids edge/node
    # collisions when bbox compression is aggressive — set to ~3 to roughly
    # triple the relative cost of an edge-node hit vs an edge-edge crossing.
    parser.add_argument("--edge-node-weight", type=float, default=0.0)
    parser.add_argument("--bbox-weight", type=float, default=800.0)
    parser.add_argument("--bbox-target-b", type=float, default=4.0)
    # composite_weight > 0 turns the Bennett-weighted quality (clean,
    # sev, ang, str, cmp, uni, spr) into a gain tiebreaker. The weight
    # is added as `w * (1 - quality)` to the score, so an improvement
    # of +0.10 quality is worth `w * 0.10` gain points. Default 0
    # keeps legacy semantics; v37-multistart-1301-bbox 후속 학습에서
    # subCompactQuality 0.01 약점이 학습 신호에 반영되도록 사용.
    parser.add_argument("--composite-weight", type=float, default=0.0)
    # When set, composite quality uses only the cheap sub-scores
    # (compact, uniform, spread) — O(N+E) per call vs O(E²) for the
    # full measure_extended. 1000x faster, subCompact still 50% of the
    # signal, missing scores (clean, severity, angle, stress) are
    # either redundant with visual_cross or low-priority for this run.
    # Recommended whenever composite-weight > 0 to keep build time
    # under ~60 min on Captain-scale.
    parser.add_argument(
        "--composite-light", action="store_true", default=False,
    )
    parser.add_argument("--max-cross-regression", type=int, default=0)
    parser.add_argument("--max-overlap-regression", type=int, default=0)
    parser.add_argument("--max-edge-node-regression", type=int, default=0)
    parser.add_argument("--max-bbox-growth", type=float, default=1.05)
    parser.add_argument("--final-max-candidates", type=int, default=4000)
    parser.add_argument("--final-per-action-candidates", type=int, default=0)
    parser.add_argument("--min-gain", type=float, default=1.0)
    parser.add_argument(
        "--extended-metrics",
        action="store_true",
        help=(
            "Append 6 extended ML-signal metrics (stress, edgeLenCv, "
            "xAngMean, xAngCv, hubClearP10, clusterCompact) to each "
            "state_metrics row. Increases state_metrics from 5 to 11 dims; "
            "downstream training code must opt into the wider feature set."
        ),
    )
    args = parser.parse_args()

    if not args.start:
        args.start = [
            "under1000=data/erd-poc/v34-under1000/under1000.tsv",
            "groupanchor2=data/erd-poc/v34-under1000/groupanchor2.tsv",
        ]
        tmp_zero = Path("/tmp/v34-under1000-raw-overlap-spread.tsv")
        if tmp_zero.exists():
            args.start.append(f"zero-overlap={tmp_zero}")

    layout = v34.load_layout(args.layout)
    args.layout_obj = layout
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
    evaluator = v34.fce.FastCrossEval(edges, len(layout["nodes"]))
    adjacency = v34.adjacency_from_edges(edges, len(layout["nodes"]))
    base_clusters = v34.cluster_members(layout)
    pseudo_groups = v34.no_cluster_pseudo_groups(layout, edges)
    clusters = dict(base_clusters)
    clusters.update(pseudo_groups)
    cluster_mask = np.array([bool(nd.get("clusterId") or "") for nd in layout["nodes"]], dtype=bool)
    node_static, node_static_names = node_static_features(layout, edges)

    # Optional Bennett-quality tiebreaker. Compiled once per run and
    # reused for every measure() / measure_candidate_incremental() call
    # so sub-score gradients (e.g. subCompactQuality 0.01 weakness) flow
    # into the gain label without an explicit per-call setup cost.
    composite_ctx = None
    if args.composite_weight > 0.0:
        composite_ctx = v34.CompositeContext(
            widths=widths,
            heights=heights,
            cluster_ids=[str(nd.get("clusterId") or "") for nd in layout["nodes"]],
            weight=float(args.composite_weight),
            light_only=bool(args.composite_light),
        )

    action_types: dict[str, int] = {}
    state_positions: list[np.ndarray] = []
    state_metrics: list[list[float]] = []
    state_start: list[int] = []
    state_round: list[int] = []
    sample_state: list[int] = []
    sample_action_type: list[int] = []
    sample_scalars: list[np.ndarray] = []
    sample_gain: list[float] = []
    sample_delta_cross: list[float] = []
    sample_delta_overlaps: list[float] = []
    sample_delta_edge_node: list[float] = []
    sample_delta_bbox: list[float] = []
    sample_delta_score: list[float] = []
    sample_is_best: list[int] = []
    sample_moved_start: list[int] = []
    sample_moved_len: list[int] = []
    moved_indices: list[np.ndarray] = []
    moved_deltas: list[np.ndarray] = []
    start_names: list[str] = []

    for start_idx, start_text in enumerate(args.start):
        name, path = parse_start(start_text)
        start_names.append(name)
        # Accept either a positions TSV (modelId\tx\ty per line) or a
        # layout JSON (e.g. captain-corpus-multistart/capt-msNN.json).
        # JSON inputs let us feed multistart cluster_graph layouts as
        # training starts without writing intermediate TSVs.
        if path.suffix.lower() == ".json":
            positions = v34.layout_positions(v34.load_layout(path))
        else:
            positions = v34.read_positions_tsv(path, layout)
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
            composite_context=composite_ctx,
        )
        print(
            f"start {name}: cross={metrics.cross} overlaps={metrics.overlaps} "
            f"edgeNode={metrics.edge_node} visualCross={metrics.visual_cross} "
            f"score={metrics.score:.1f}",
            flush=True,
        )
        for round_idx in range(args.rounds):
            state_id = len(state_positions)
            state_positions.append(positions.astype(np.float32).copy())
            state_metric_row = [
                metrics.cross,
                metrics.overlaps,
                metrics.edge_node,
                metrics.bbox_b,
                metrics.score,
            ]
            if args.extended_metrics:
                cluster_ids = [
                    str(nd.get("clusterId") or "") for nd in layout["nodes"]
                ]
                ext = metrics_extended.measure_extended(
                    positions, edges, widths, heights, cluster_ids
                )
                state_metric_row.extend(
                    [
                        ext.stress_score,
                        ext.edge_length_cv,
                        ext.crossing_angle_mean,
                        ext.crossing_angle_cv,
                        ext.hub_clearance_p10,
                        ext.cluster_compactness_mean,
                        # Bennett-weighted composite + 7 sub-scores.
                        # All ∈ [0, 1] with 1 = best. Direct ML reward
                        # signal without ad-hoc weight tuning.
                        ext.composite_quality,
                        ext.sub_clean_quality,
                        ext.sub_severity_quality,
                        ext.sub_angle_quality,
                        ext.sub_stress_quality,
                        ext.sub_compact_quality,
                        ext.sub_uniform_quality,
                        ext.sub_spread_quality,
                    ]
                )
            state_metrics.append(state_metric_row)
            state_start.append(start_idx)
            state_round.append(round_idx + 1)

            _cross, node_counts, edge_counts = v34.crossing_incident_counts(positions, evaluator)
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
            current_edge_node_flags = v34.edge_rect_intersection_flags(
                positions,
                current_rect_positions,
                collision_geometry.rect_widths,
                collision_geometry.rect_heights,
                edges,
                edge_node_pairs[0],
                edge_node_pairs[1],
                args.edge_node_margin,
            )
            candidates = action_candidates(
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
            )
            best_metrics = None
            best_cand = None
            best_gain = None
            first_sample = len(sample_gain)
            for cand in candidates:
                moved = v34.move_positions(positions, cand)
                cand_metrics = v34.measure_candidate_incremental(
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
                    composite_context=composite_ctx,
                )
                affected, deltas = affected_nodes_and_deltas(cand, positions, moved)
                action_type = v34.candidate_action_type(cand)
                if action_type not in action_types:
                    action_types[action_type] = len(action_types)
                sample_state.append(state_id)
                sample_action_type.append(action_types[action_type])
                sample_scalars.append(
                    scalar_features(
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
                        moved,
                        edge_node_pairs,
                        current_edge_node_flags,
                        collision_geometry=collision_geometry,
                    )
                )
                gain = training_gain(metrics, cand_metrics, args)
                sample_gain.append(float(gain))
                sample_delta_cross.append(float(cand_metrics.cross - metrics.cross))
                sample_delta_overlaps.append(float(cand_metrics.overlaps - metrics.overlaps))
                sample_delta_edge_node.append(float(cand_metrics.edge_node - metrics.edge_node))
                sample_delta_bbox.append(float(cand_metrics.bbox_b - metrics.bbox_b))
                sample_delta_score.append(float(cand_metrics.score - metrics.score))
                sample_is_best.append(0)
                sample_moved_start.append(sum(arr.shape[0] for arr in moved_indices))
                sample_moved_len.append(int(affected.shape[0]))
                moved_indices.append(affected.astype(np.int32))
                moved_deltas.append(deltas.astype(np.float32))
                if best_gain is None or gain > best_gain:
                    best_metrics = cand_metrics
                    best_cand = cand
                    best_gain = gain
            if best_metrics is None:
                break
            gains = [sample_gain[i] for i in range(first_sample, len(sample_gain))]
            best_offset = int(np.argmax(np.array(gains, dtype=np.float64)))
            sample_is_best[first_sample + best_offset] = 1
            print(
                f"  round {round_idx+1:02d}: candidates={len(candidates)} "
                f"bestGain={best_gain:.1f} cross={best_metrics.cross} "
                f"overlaps={best_metrics.overlaps} "
                f"edgeNode={best_metrics.edge_node} "
                f"visualCross={best_metrics.visual_cross}",
                flush=True,
            )
            if best_cand is None or best_gain is None or best_gain < args.min_gain:
                break
            positions = v34.move_positions(positions, best_cand)
            metrics = best_metrics

    flat_indices = np.concatenate(moved_indices).astype(np.int32) if moved_indices else np.zeros(0, dtype=np.int32)
    flat_deltas = np.concatenate(moved_deltas).astype(np.float32) if moved_deltas else np.zeros((0, 2), dtype=np.float32)
    action_type_names = np.array(
        [name for name, _idx in sorted(action_types.items(), key=lambda item: item[1])],
        dtype="U64",
    )
    # Column names for state_metrics so consumers can introspect the
    # feature schema without hardcoding the dimension order.
    state_metric_names = ["cross", "overlaps", "edge_node", "bbox_b", "score"]
    if args.extended_metrics:
        state_metric_names.extend([
            "stress",
            "edge_length_cv",
            "crossing_angle_mean",
            "crossing_angle_cv",
            "hub_clearance_p10",
            "cluster_compactness_mean",
            "composite_quality",
            "sub_clean_quality",
            "sub_severity_quality",
            "sub_angle_quality",
            "sub_stress_quality",
            "sub_compact_quality",
            "sub_uniform_quality",
            "sub_spread_quality",
        ])
    args.out.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        args.out,
        node_static=node_static,
        node_static_names=np.array(node_static_names, dtype="U64"),
        edges=edges.astype(np.int32),
        state_positions=np.stack(state_positions).astype(np.float32),
        state_metrics=np.array(state_metrics, dtype=np.float32),
        state_metric_names=np.array(state_metric_names, dtype="U64"),
        state_start=np.array(state_start, dtype=np.int32),
        state_round=np.array(state_round, dtype=np.int32),
        start_names=np.array(start_names, dtype="U128"),
        sample_state=np.array(sample_state, dtype=np.int32),
        sample_action_type=np.array(sample_action_type, dtype=np.int32),
        action_type_names=action_type_names,
        sample_scalars=np.stack(sample_scalars).astype(np.float32),
        scalar_names=np.array(SCALAR_NAMES, dtype="U64"),
        sample_gain=np.array(sample_gain, dtype=np.float32),
        sample_delta_cross=np.array(sample_delta_cross, dtype=np.float32),
        sample_delta_overlaps=np.array(sample_delta_overlaps, dtype=np.float32),
        sample_delta_edge_node=np.array(sample_delta_edge_node, dtype=np.float32),
        sample_delta_bbox=np.array(sample_delta_bbox, dtype=np.float32),
        sample_delta_score=np.array(sample_delta_score, dtype=np.float32),
        sample_is_best=np.array(sample_is_best, dtype=np.int8),
        sample_moved_start=np.array(sample_moved_start, dtype=np.int64),
        sample_moved_len=np.array(sample_moved_len, dtype=np.int32),
        moved_indices=flat_indices,
        moved_deltas=flat_deltas,
    )
    print(
        f"saved {args.out} states={len(state_positions)} "
        f"samples={len(sample_gain)} movedRefs={flat_indices.shape[0]} "
        f"actions={len(action_types)}",
        flush=True,
    )


if __name__ == "__main__":
    main()
