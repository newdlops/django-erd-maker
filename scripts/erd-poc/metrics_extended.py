"""Extended layout quality metrics — Python parity with the C++ binary's
LayoutQualityMetrics extension (stress, edge-length CV, crossing angle,
hub clearance, cluster compactness).

All metrics are dataset-agnostic — no per-graph thresholds. Intended for
use alongside v34.Metrics in the ML pipeline; this module is self-
contained and depends only on numpy.

Reference: Gansner et al. 2005 "Graph Drawing by Stress Majorization"
for the stress formulation. Other metrics are standard force-directed
layout quality diagnostics.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field

import numpy as np


@dataclass
class ExtendedMetrics:
    """Continuous-valued quality signals complementing v34.Metrics.

    stress_score: Σ w_ij (||p_i - p_j|| - L · d_ij)² / pairs / L²
        with w_ij = 1/d_ij² and L = mean direct-edge length. Lower =
        layout positions match graph-theoretic distances.
    edge_length_cv: stddev / mean of direct edge lengths. 0 = uniform.
    crossing_angle_mean: mean acute angle (radians) of edge-edge
        segment crossings. π/2 = best visual clarity.
    crossing_angle_cv: cv of crossing angles. 0 = uniform geometry.
    hub_clearance_p10: 10th percentile of "distance to nearest non-
        incident node" across top-decile-degree hubs. Low = crowded.
    cluster_compactness_mean: mean of (cluster bbox area / Σ member
        node area) across clusters with ≥2 members. 1.0 = perfectly
        packed; higher = sparse.
    """

    stress_score: float = 0.0
    edge_length_cv: float = 0.0
    crossing_angle_mean: float = 0.0
    crossing_angle_cv: float = 0.0
    edge_bend_total: float = 0.0
    hub_clearance_p10: float = 0.0
    cluster_compactness_mean: float = 0.0
    # Cross/bbox subdivisions — see C++ types.h for definitions.
    crossings_per_edge_p50: int = 0
    crossings_per_edge_p90: int = 0
    clean_edge_ratio: float = 0.0
    edge_crossings_between_clusters: int = 0
    node_area_coverage: float = 0.0
    empty_space_cv: float = 0.0
    # Top-N edges by raw geometric crossing count, sorted descending.
    # Each entry: (source_model_id, target_model_id, crossings).
    # Length capped at DJERD_TOP_CROSS_EDGES_N (default 20).
    top_cross_edges: list[tuple[str, str, int]] = field(default_factory=list)
    # Composite quality score ∈ [0, 1] with 1 = best. Weighted sum of
    # 7 sub-scores; weights from Bennett et al. 2007 aesthetic study.
    composite_quality: float = 0.0
    sub_clean_quality: float = 0.0
    sub_severity_quality: float = 0.0
    sub_angle_quality: float = 0.0
    sub_stress_quality: float = 0.0
    sub_compact_quality: float = 0.0
    sub_uniform_quality: float = 0.0
    sub_spread_quality: float = 0.0


def _edge_lengths(positions: np.ndarray, edges: np.ndarray) -> np.ndarray:
    """Euclidean length of each direct (center-to-center) edge."""
    src = positions[edges[:, 0]]
    dst = positions[edges[:, 1]]
    return np.linalg.norm(dst - src, axis=1)


def _boundary_ports(
    positions: np.ndarray,
    widths: np.ndarray,
    heights: np.ndarray,
    edges: np.ndarray,
) -> tuple[np.ndarray, np.ndarray]:
    """For each edge return (port_at_source, port_at_target) — center-line
    direction clipped to each endpoint's axis-aligned bbox. Matches the
    C++ binary's straight-routing port computation so length/angle/cross
    metrics align with engineMetadata output."""
    src_idx = edges[:, 0]
    dst_idx = edges[:, 1]
    src_pos = positions[src_idx]
    dst_pos = positions[dst_idx]
    dirs = dst_pos - src_pos
    src_w = widths[src_idx] / 2.0
    src_h = heights[src_idx] / 2.0
    dst_w = widths[dst_idx] / 2.0
    dst_h = heights[dst_idx] / 2.0

    def _clip(center, half_w, half_h, dx, dy):
        # Return point on bbox boundary along (dx, dy) direction.
        # When direction is zero, the center is the port.
        out = center.copy()
        ax = np.abs(dx)
        ay = np.abs(dy)
        nonzero = (ax > 1e-9) | (ay > 1e-9)
        if not nonzero.any():
            return out
        scale_x = np.where(ax > 1e-9, half_w / np.maximum(ax, 1e-9), np.inf)
        scale_y = np.where(ay > 1e-9, half_h / np.maximum(ay, 1e-9), np.inf)
        scale = np.minimum(scale_x, scale_y)
        out[:, 0] = np.where(nonzero, center[:, 0] + scale * dx, center[:, 0])
        out[:, 1] = np.where(nonzero, center[:, 1] + scale * dy, center[:, 1])
        return out

    src_port = _clip(src_pos, src_w, src_h, dirs[:, 0], dirs[:, 1])
    dst_port = _clip(dst_pos, dst_w, dst_h, -dirs[:, 0], -dirs[:, 1])
    return src_port, dst_port


def edge_length_cv(
    positions: np.ndarray,
    edges: np.ndarray,
    widths: np.ndarray | None = None,
    heights: np.ndarray | None = None,
) -> float:
    """Coefficient of variation of edge lengths.
    With widths/heights provided, lengths are boundary-port distances
    (matching C++ engineMetadata). Without them, center-to-center.
    """
    if edges.shape[0] == 0:
        return 0.0
    if widths is not None and heights is not None:
        src_port, dst_port = _boundary_ports(positions, widths, heights, edges)
        lengths = np.linalg.norm(dst_port - src_port, axis=1)
    else:
        lengths = _edge_lengths(positions, edges)
    mean = float(lengths.mean())
    if mean < 1e-6:
        return 0.0
    return float(lengths.std() / mean)


def _bfs_distances(adjacency: list[list[int]], source: int, n: int) -> np.ndarray:
    """BFS hop distance from source to every node; unreached = 0xFFFF."""
    UNREACHED = np.uint16(0xFFFF)
    dist = np.full(n, UNREACHED, dtype=np.uint16)
    dist[source] = 0
    q = deque([source])
    while q:
        u = q.popleft()
        du = dist[u]
        for v in adjacency[u]:
            if dist[v] != UNREACHED:
                continue
            dist[v] = du + 1
            q.append(v)
    return dist


def stress_score(
    positions: np.ndarray,
    edges: np.ndarray,
    widths: np.ndarray | None = None,
    heights: np.ndarray | None = None,
) -> float:
    """Gansner stress, normalized by pair count and ideal-length². Scale-
    invariant: comparable across graphs of different sizes/scales.

    Ideal length L is the mean direct-edge length. When widths/heights
    are provided, L is computed from boundary-port endpoints to match
    the C++ engineMetadata's meanEdgeLength."""
    n = positions.shape[0]
    if n == 0 or edges.shape[0] == 0:
        return 0.0
    if widths is not None and heights is not None:
        src_port, dst_port = _boundary_ports(positions, widths, heights, edges)
        L = float(np.linalg.norm(dst_port - src_port, axis=1).mean())
    else:
        L = float(_edge_lengths(positions, edges).mean())
    if L < 1e-6:
        return 0.0
    adjacency: list[list[int]] = [[] for _ in range(n)]
    for a, b in edges:
        if a == b:
            continue
        adjacency[a].append(int(b))
        adjacency[b].append(int(a))
    stress_accum = 0.0
    pair_count = 0
    UNREACHED = np.uint16(0xFFFF)
    for src in range(n):
        dist = _bfs_distances(adjacency, src, n)
        # Only count unordered pairs once (j > src) with finite, nonzero distance.
        reachable = (dist != UNREACHED) & (np.arange(n) > src) & (dist != 0)
        if not reachable.any():
            continue
        d_ij = dist[reachable].astype(np.float64)
        euclid = np.linalg.norm(positions[reachable] - positions[src], axis=1)
        ideal = L * d_ij
        diff = euclid - ideal
        w = 1.0 / (d_ij * d_ij)
        stress_accum += float((w * diff * diff).sum())
        pair_count += int(reachable.sum())
    if pair_count == 0:
        return 0.0
    return stress_accum / (pair_count * L * L)


def _segments_intersect(
    p1: np.ndarray, p2: np.ndarray, p3: np.ndarray, p4: np.ndarray
) -> bool:
    """Proper segment intersection (excludes endpoint-touch)."""
    r = p2 - p1
    s = p4 - p3
    denom = r[0] * s[1] - r[1] * s[0]
    if abs(denom) < 0.01:
        return False
    qp = p3 - p1
    t = (qp[0] * s[1] - qp[1] * s[0]) / denom
    u = (qp[0] * r[1] - qp[1] * r[0]) / denom
    eps = 0.001
    return eps < t < 1 - eps and eps < u < 1 - eps


def crossing_angle_dist(
    positions: np.ndarray,
    edges: np.ndarray,
    widths: np.ndarray | None = None,
    heights: np.ndarray | None = None,
) -> tuple[float, float]:
    """(mean, cv) of acute angles (radians) at edge-edge crossings.
    Brute-force segment-pair enumeration with bbox prefilter.
    With widths/heights provided, edge endpoints are clipped to node
    bbox boundary ports (matches C++ engineMetadata).
    Returns (0.0, 0.0) when no crossings."""
    e_count = edges.shape[0]
    if e_count < 2:
        return 0.0, 0.0
    if widths is not None and heights is not None:
        src, dst = _boundary_ports(positions, widths, heights, edges)
    else:
        src = positions[edges[:, 0]]
        dst = positions[edges[:, 1]]
    min_x = np.minimum(src[:, 0], dst[:, 0])
    max_x = np.maximum(src[:, 0], dst[:, 0])
    min_y = np.minimum(src[:, 1], dst[:, 1])
    max_y = np.maximum(src[:, 1], dst[:, 1])
    dirs = dst - src
    lengths = np.linalg.norm(dirs, axis=1)
    valid = lengths > 1e-6
    angles: list[float] = []
    for i in range(e_count):
        if not valid[i]:
            continue
        # Vectorized bbox prefilter against ALL later edges.
        j_idx = np.arange(i + 1, e_count)
        if j_idx.size == 0:
            break
        bbox_ok = (
            (max_x[i] >= min_x[j_idx])
            & (max_x[j_idx] >= min_x[i])
            & (max_y[i] >= min_y[j_idx])
            & (max_y[j_idx] >= min_y[i])
            & valid[j_idx]
        )
        # NOTE: shared-endpoint edges are NOT skipped here — match C++
        # measureLayoutQuality which only skips same-edge segments. The
        # proper-intersection epsilon in _segments_intersect already
        # rejects exact endpoint-touch coincidences.
        candidates = j_idx[bbox_ok]
        for j in candidates:
            if not _segments_intersect(src[i], dst[i], src[j], dst[j]):
                continue
            cos_a = abs(float(dirs[i] @ dirs[j]) / (lengths[i] * lengths[j]))
            if cos_a > 1.0:
                cos_a = 1.0
            angles.append(float(np.arccos(cos_a)))
    if not angles:
        return 0.0, 0.0
    arr = np.array(angles)
    mean = float(arr.mean())
    if mean < 1e-6:
        return 0.0, 0.0
    cv = float(arr.std() / mean)
    return mean, cv


def hub_clearance_p10(
    positions: np.ndarray, edges: np.ndarray
) -> float:
    """10th percentile of (distance to nearest non-incident node) at
    top-decile-degree hubs. Low value = some hubs crowded.
    Returns 0.0 when fewer than 10 nodes."""
    n = positions.shape[0]
    if n < 10:
        return 0.0
    degree = np.zeros(n, dtype=np.int32)
    incident_sets: list[set[int]] = [set() for _ in range(n)]
    for a, b in edges:
        if a == b:
            continue
        degree[a] += 1
        degree[b] += 1
        incident_sets[a].add(int(b))
        incident_sets[b].add(int(a))
    hub_count = max(1, n // 10)
    hub_idx = np.argsort(-degree)[:hub_count]
    clearances: list[float] = []
    all_idx = np.arange(n)
    for h in hub_idx:
        if degree[h] == 0:
            continue
        incident = incident_sets[h] | {int(h)}
        mask = np.ones(n, dtype=bool)
        for j in incident:
            mask[j] = False
        if not mask.any():
            continue
        diffs = positions[mask] - positions[h]
        dists = np.linalg.norm(diffs, axis=1)
        clearances.append(float(dists.min()))
    if not clearances:
        return 0.0
    return float(np.percentile(clearances, 10.0))


def cluster_compactness_mean(
    positions: np.ndarray,
    widths: np.ndarray,
    heights: np.ndarray,
    cluster_ids: list[str] | np.ndarray,
) -> float:
    """Mean (cluster bbox area / Σ member node area) across clusters
    with ≥2 members. 1.0 = perfectly packed; higher = sparse.
    cluster_ids: per-node cluster string; "" = no cluster."""
    if len(cluster_ids) != positions.shape[0]:
        return 0.0
    members_by: dict[str, list[int]] = {}
    for i, cid in enumerate(cluster_ids):
        if not cid:
            continue
        members_by.setdefault(str(cid), []).append(i)
    ratios: list[float] = []
    for cid, members in members_by.items():
        if len(members) < 2:
            continue
        mem = np.array(members)
        cx = positions[mem, 0]
        cy = positions[mem, 1]
        w = widths[mem]
        h = heights[mem]
        min_x = float((cx - w / 2.0).min())
        max_x = float((cx + w / 2.0).max())
        min_y = float((cy - h / 2.0).min())
        max_y = float((cy + h / 2.0).max())
        bbox_area = (max_x - min_x) * (max_y - min_y)
        node_area = float((w * h).sum())
        if node_area < 1e-6 or bbox_area < 1e-6:
            continue
        ratios.append(bbox_area / node_area)
    if not ratios:
        return 0.0
    return float(np.mean(ratios))


def _crossings_per_edge_and_clusters(
    route_segments: list[np.ndarray],
    edges: np.ndarray,
    cluster_ids: list[str] | np.ndarray | None,
) -> tuple[np.ndarray, int]:
    """Returns (per-edge crossing counts, cross-cluster crossing total)
    using the same segment-pair enumeration as crossing_angle_dist_from_routes.
    """
    n_edges = edges.shape[0]
    per_edge = np.zeros(n_edges, dtype=np.int64)
    if n_edges < 2:
        return per_edge, 0
    is_cross_cluster = np.zeros(n_edges, dtype=bool)
    if cluster_ids is not None and len(cluster_ids) > 0:
        for e in range(n_edges):
            s_idx = int(edges[e, 0])
            t_idx = int(edges[e, 1])
            if s_idx < len(cluster_ids) and t_idx < len(cluster_ids):
                sc = str(cluster_ids[s_idx])
                tc = str(cluster_ids[t_idx])
                if sc and tc and sc != tc:
                    is_cross_cluster[e] = True
    segs: list[tuple[int, float, float, float, float]] = []
    for e, route in enumerate(route_segments):
        if route.shape[0] < 2:
            continue
        for i in range(1, route.shape[0]):
            segs.append((
                e,
                float(route[i - 1, 0]), float(route[i - 1, 1]),
                float(route[i, 0]),     float(route[i, 1]),
            ))
    cross_cluster_total = 0
    for i in range(len(segs)):
        ei, ax1, ay1, ax2, ay2 = segs[i]
        for j in range(i + 1, len(segs)):
            ej, bx1, by1, bx2, by2 = segs[j]
            if ei == ej:
                continue
            if max(ax1, ax2) < min(bx1, bx2):
                continue
            if max(bx1, bx2) < min(ax1, ax2):
                continue
            if max(ay1, ay2) < min(by1, by2):
                continue
            if max(by1, by2) < min(ay1, ay2):
                continue
            if not _segments_intersect(
                np.array([ax1, ay1]), np.array([ax2, ay2]),
                np.array([bx1, by1]), np.array([bx2, by2]),
            ):
                continue
            per_edge[ei] += 1
            per_edge[ej] += 1
            if is_cross_cluster[ei] and is_cross_cluster[ej]:
                cross_cluster_total += 1
    return per_edge, cross_cluster_total


def node_area_coverage(
    positions: np.ndarray,
    widths: np.ndarray,
    heights: np.ndarray,
) -> float:
    """Σ node area / bbox area. Higher = tighter packing."""
    n = positions.shape[0]
    if n == 0:
        return 0.0
    half_w = widths / 2.0
    half_h = heights / 2.0
    left = (positions[:, 0] - half_w).min()
    right = (positions[:, 0] + half_w).max()
    top = (positions[:, 1] - half_h).min()
    bottom = (positions[:, 1] + half_h).max()
    bbox_area = float((right - left) * (bottom - top))
    if bbox_area < 1e-6:
        return 0.0
    return float((widths * heights).sum() / bbox_area)


def empty_space_cv(
    positions: np.ndarray,
    widths: np.ndarray,
    heights: np.ndarray,
    grid_n: int = 16,
) -> float:
    """CV of per-cell occupancy in a grid_n × grid_n grid covering the
    bbox. 0 = uniform fill; high = clumpy distribution."""
    n = positions.shape[0]
    if n == 0:
        return 0.0
    half_w = widths / 2.0
    half_h = heights / 2.0
    left = (positions[:, 0] - half_w).min()
    right = (positions[:, 0] + half_w).max()
    top = (positions[:, 1] - half_h).min()
    bottom = (positions[:, 1] + half_h).max()
    width = float(right - left)
    height = float(bottom - top)
    if width < 1e-6 or height < 1e-6:
        return 0.0
    cell_w = width / grid_n
    cell_h = height / grid_n
    cell_area = cell_w * cell_h
    occupied = np.zeros((grid_n, grid_n), dtype=np.float64)
    for i in range(n):
        cx = positions[i, 0]
        cy = positions[i, 1]
        nw = widths[i]
        nh = heights[i]
        n_left = cx - nw / 2.0
        n_right = cx + nw / 2.0
        n_top = cy - nh / 2.0
        n_bottom = cy + nh / 2.0
        gx_lo = max(0, int(np.floor((n_left - left) / cell_w)))
        gx_hi = min(grid_n - 1, int(np.floor((n_right - left) / cell_w)))
        gy_lo = max(0, int(np.floor((n_top - top) / cell_h)))
        gy_hi = min(grid_n - 1, int(np.floor((n_bottom - top) / cell_h)))
        for gy in range(gy_lo, gy_hi + 1):
            cell_top = top + gy * cell_h
            cell_bottom = cell_top + cell_h
            overlap_top = max(n_top, cell_top)
            overlap_bottom = min(n_bottom, cell_bottom)
            overlap_h = max(0.0, overlap_bottom - overlap_top)
            if overlap_h <= 0.0:
                continue
            for gx in range(gx_lo, gx_hi + 1):
                cell_left = left + gx * cell_w
                cell_right = cell_left + cell_w
                overlap_left = max(n_left, cell_left)
                overlap_right = min(n_right, cell_right)
                overlap_w = max(0.0, overlap_right - overlap_left)
                occupied[gy, gx] += overlap_w * overlap_h
    fracs = np.minimum(1.0, occupied / max(cell_area, 1e-9))
    flat = fracs.flatten()
    occ_mean = float(flat.mean())
    if occ_mean < 1e-6:
        return 0.0
    return float(flat.std() / occ_mean)


def _fill_composite_quality(result: ExtendedMetrics) -> None:
    """Compute 7 sub-scores and Bennett-weighted composite. Sub-scores
    are each in [0, 1] with 1 = best; composite in [0, 1] with
    weights 0.30 / 0.15 / 0.20 / 0.15 / 0.10 / 0.05 / 0.05."""
    pi_half = np.pi / 2.0
    result.sub_clean_quality = result.clean_edge_ratio
    result.sub_severity_quality = (
        1.0 / (1.0 + float(result.crossings_per_edge_p90) * 0.1)
    )
    result.sub_angle_quality = min(1.0,
        result.crossing_angle_mean / pi_half if pi_half > 0 else 0.0)
    result.sub_stress_quality = 1.0 / (1.0 + result.stress_score)
    result.sub_compact_quality = min(1.0, result.node_area_coverage * 5.0)
    result.sub_uniform_quality = 1.0 / (1.0 + result.edge_length_cv)
    result.sub_spread_quality = 1.0 / (1.0 + result.empty_space_cv * 0.5)
    result.composite_quality = (
        0.30 * result.sub_clean_quality
        + 0.15 * result.sub_severity_quality
        + 0.20 * result.sub_angle_quality
        + 0.15 * result.sub_stress_quality
        + 0.10 * result.sub_compact_quality
        + 0.05 * result.sub_uniform_quality
        + 0.05 * result.sub_spread_quality
    )


def _build_top_cross_edges(
    per_edge: np.ndarray,
    edges: np.ndarray,
    model_ids: list[str] | None,
    top_n: int = 20,
) -> list[tuple[str, str, int]]:
    """Sort edges by per_edge crossing count descending; return top N
    records as (source_model_id, target_model_id, crossings). Entries
    with zero crossings are dropped. When model_ids is None, falls back
    to "node<idx>" labels for parity with C++ behavior."""
    if per_edge.size == 0 or top_n <= 0:
        return []
    order = np.argsort(-per_edge)
    out: list[tuple[str, str, int]] = []
    for k in range(min(top_n, len(order))):
        ei = int(order[k])
        if per_edge[ei] == 0:
            break
        s_idx = int(edges[ei, 0])
        t_idx = int(edges[ei, 1])
        s_mid = model_ids[s_idx] if model_ids and s_idx < len(model_ids) else f"node{s_idx}"
        t_mid = model_ids[t_idx] if model_ids and t_idx < len(model_ids) else f"node{t_idx}"
        out.append((s_mid, t_mid, int(per_edge[ei])))
    return out


def measure_extended(
    positions: np.ndarray,
    edges: np.ndarray,
    widths: np.ndarray,
    heights: np.ndarray,
    cluster_ids: list[str] | np.ndarray | None = None,
    model_ids: list[str] | None = None,
    top_cross_edges_n: int = 20,
) -> ExtendedMetrics:
    """Compute all extended metrics in one call.

    positions: (N, 2) center coords
    edges: (E, 2) int indices into positions
    widths, heights: (N,) node bbox sizes
    cluster_ids: per-node cluster string (or None to skip compactness)
    model_ids: per-node model ID string (for top_cross_edges labels)
    top_cross_edges_n: how many top offenders to report (default 20)
    """
    result = ExtendedMetrics()
    result.stress_score = stress_score(positions, edges, widths, heights)
    result.edge_length_cv = edge_length_cv(positions, edges, widths, heights)
    result.crossing_angle_mean, result.crossing_angle_cv = crossing_angle_dist(
        positions, edges, widths, heights
    )
    result.hub_clearance_p10 = hub_clearance_p10(positions, edges)
    if cluster_ids is not None:
        result.cluster_compactness_mean = cluster_compactness_mean(
            positions, widths, heights, cluster_ids
        )
    result.node_area_coverage = node_area_coverage(positions, widths, heights)
    result.empty_space_cv = empty_space_cv(positions, widths, heights)
    # Cross subdivisions need route segments; without them we build
    # synthetic 2-point routes from boundary ports.
    if widths is not None and heights is not None:
        src_port, dst_port = _boundary_ports(positions, widths, heights, edges)
        route_segments = [
            np.array([src_port[e], dst_port[e]]) for e in range(edges.shape[0])
        ]
        per_edge, cross_cluster = _crossings_per_edge_and_clusters(
            route_segments, edges, cluster_ids
        )
        if per_edge.size > 0:
            sorted_pe = np.sort(per_edge)
            last = len(sorted_pe) - 1
            result.crossings_per_edge_p50 = int(sorted_pe[int(0.50 * last)])
            result.crossings_per_edge_p90 = int(sorted_pe[int(0.90 * last)])
            result.clean_edge_ratio = float((per_edge == 0).sum() / len(per_edge))
        result.edge_crossings_between_clusters = int(cross_cluster)
        result.top_cross_edges = _build_top_cross_edges(
            per_edge, edges, model_ids, top_cross_edges_n
        )
    _fill_composite_quality(result)
    return result


# ---------------------------------------------------------------------------
# Route-aware variants. Used when the caller has the C++ binary's routed
# polylines (engineMetadata + routedEdges) and wants metrics computed on
# the actual visual geometry instead of center-to-center approximations.
# ---------------------------------------------------------------------------


def _route_length(route: np.ndarray) -> float:
    """Total polyline length of a (P, 2) route array."""
    if route.shape[0] < 2:
        return 0.0
    diffs = route[1:] - route[:-1]
    return float(np.linalg.norm(diffs, axis=1).sum())


def edge_length_cv_from_routes(route_segments: list[np.ndarray]) -> float:
    lengths = np.array(
        [_route_length(r) for r in route_segments if r.shape[0] >= 2],
        dtype=np.float64,
    )
    if lengths.size == 0:
        return 0.0
    mean = float(lengths.mean())
    if mean < 1e-6:
        return 0.0
    return float(lengths.std() / mean)


def crossing_angle_dist_from_routes(
    route_segments: list[np.ndarray],
) -> tuple[float, float]:
    """Mean/CV of crossing angles using actual route polyline segments.
    Matches the C++ binary's segment-pair enumeration.
    """
    # Flatten into (edge_idx, x1, y1, x2, y2) segment records.
    segs: list[tuple[int, float, float, float, float]] = []
    for e, route in enumerate(route_segments):
        if route.shape[0] < 2:
            continue
        for i in range(1, route.shape[0]):
            segs.append((
                e,
                float(route[i - 1, 0]), float(route[i - 1, 1]),
                float(route[i, 0]),     float(route[i, 1]),
            ))
    if len(segs) < 2:
        return 0.0, 0.0
    angles: list[float] = []
    for i in range(len(segs)):
        ei, ax1, ay1, ax2, ay2 = segs[i]
        ax = ax2 - ax1
        ay = ay2 - ay1
        a_len = (ax * ax + ay * ay) ** 0.5
        if a_len < 1e-6:
            continue
        for j in range(i + 1, len(segs)):
            ej, bx1, by1, bx2, by2 = segs[j]
            if ei == ej:
                continue
            # bbox prefilter
            if max(ax1, ax2) < min(bx1, bx2):
                continue
            if max(bx1, bx2) < min(ax1, ax2):
                continue
            if max(ay1, ay2) < min(by1, by2):
                continue
            if max(by1, by2) < min(ay1, ay2):
                continue
            if not _segments_intersect(
                np.array([ax1, ay1]), np.array([ax2, ay2]),
                np.array([bx1, by1]), np.array([bx2, by2]),
            ):
                continue
            bx = bx2 - bx1
            by = by2 - by1
            b_len = (bx * bx + by * by) ** 0.5
            if b_len < 1e-6:
                continue
            cos_a = abs(ax * bx + ay * by) / (a_len * b_len)
            if cos_a > 1.0:
                cos_a = 1.0
            angles.append(float(np.arccos(cos_a)))
    if not angles:
        return 0.0, 0.0
    arr = np.array(angles)
    mean = float(arr.mean())
    if mean < 1e-6:
        return 0.0, 0.0
    cv = float(arr.std() / mean)
    return mean, cv


def edge_bend_total_from_routes(route_segments: list[np.ndarray]) -> float:
    """Sum across edges of internal-waypoint angle changes (radians).
    Straight 2-point routes contribute 0; multi-segment routes accumulate
    the turn at each interior waypoint.
    """
    bend_sum = 0.0
    for route in route_segments:
        if route.shape[0] < 3:
            continue
        for i in range(1, route.shape[0] - 1):
            a = route[i] - route[i - 1]
            b = route[i + 1] - route[i]
            a_len = float(np.linalg.norm(a))
            b_len = float(np.linalg.norm(b))
            if a_len < 1e-6 or b_len < 1e-6:
                continue
            cos_a = float(a @ b) / (a_len * b_len)
            if cos_a > 1.0:
                cos_a = 1.0
            if cos_a < -1.0:
                cos_a = -1.0
            bend_sum += float(np.arccos(cos_a))
    return bend_sum


def measure_extended_with_routes(
    positions: np.ndarray,
    edges: np.ndarray,
    widths: np.ndarray,
    heights: np.ndarray,
    route_segments: list[np.ndarray],
    cluster_ids: list[str] | np.ndarray | None = None,
    model_ids: list[str] | None = None,
    top_cross_edges_n: int = 20,
) -> ExtendedMetrics:
    """Same as measure_extended but uses route polyline segments for
    metrics that depend on edge geometry (length, crossing angle, bend).
    Stress / hub clearance / cluster compactness are still position-based
    since they don't benefit from routing detail.

    route_segments: list[ndarray(P_e, 2)], length == edges.shape[0]
    """
    result = ExtendedMetrics()
    result.stress_score = stress_score(positions, edges, widths, heights)
    result.edge_length_cv = edge_length_cv_from_routes(route_segments)
    result.crossing_angle_mean, result.crossing_angle_cv = (
        crossing_angle_dist_from_routes(route_segments)
    )
    result.edge_bend_total = edge_bend_total_from_routes(route_segments)
    result.hub_clearance_p10 = hub_clearance_p10(positions, edges)
    if cluster_ids is not None:
        result.cluster_compactness_mean = cluster_compactness_mean(
            positions, widths, heights, cluster_ids
        )
    result.node_area_coverage = node_area_coverage(positions, widths, heights)
    result.empty_space_cv = empty_space_cv(positions, widths, heights)
    per_edge, cross_cluster = _crossings_per_edge_and_clusters(
        route_segments, edges, cluster_ids
    )
    if per_edge.size > 0:
        sorted_pe = np.sort(per_edge)
        last = len(sorted_pe) - 1
        result.crossings_per_edge_p50 = int(sorted_pe[int(0.50 * last)])
        result.crossings_per_edge_p90 = int(sorted_pe[int(0.90 * last)])
        result.clean_edge_ratio = float((per_edge == 0).sum() / len(per_edge))
    result.edge_crossings_between_clusters = int(cross_cluster)
    result.top_cross_edges = _build_top_cross_edges(
        per_edge, edges, model_ids, top_cross_edges_n
    )
    _fill_composite_quality(result)
    return result
