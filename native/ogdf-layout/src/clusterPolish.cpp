// Cluster-rigid polish — C++ port of scripts/ml-layout-polish-rigid.py.
//
// Math (matches the Python reference 1:1 — cross-product normalised by
// segment lengths, sigmoid-of-cross combined into a 4-way straddle
// indicator). Manual analytical gradient. Adam optimizer.

#include "clusterPolish.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <unordered_map>

namespace djerd {

namespace {

inline double sigmoid(double x) {
  if (x >= 0) {
    const double e = std::exp(-x);
    return 1.0 / (1.0 + e);
  }
  const double e = std::exp(x);
  return e / (1.0 + e);
}

// Pair of edge indices (i, j) with i < j.
struct Pair {
  std::size_t i, j;
};

}  // namespace

ClusterPolishResult runClusterPolish(
    const std::vector<NodeRecord>& nodes,
    const std::vector<EdgeRecord>& edges,
    ogdf::GraphAttributes& attributes,
    const std::vector<std::vector<std::size_t>>& clusterMembers,
    const std::vector<int>& nodeClusterIdx,
    const std::vector<std::string>& carrierIdByEdge,
    const std::vector<BundleBox>& bundleBoxes,
    const std::vector<bool>& isAbsorbedHub,
    const ClusterPolishOptions& opts) {
  ClusterPolishResult result{};
  const std::size_t N = nodes.size();
  const std::size_t E = edges.size();

  // Build modelId → idx map for edges.
  std::unordered_map<std::string, std::size_t> idToIdx;
  idToIdx.reserve(N);
  for (std::size_t i = 0; i < N; ++i) {
    idToIdx[nodes[i].modelId] = i;
  }
  std::vector<std::pair<std::size_t, std::size_t>> edgePairs(E, {0, 0});
  for (std::size_t e = 0; e < E; ++e) {
    auto sIt = idToIdx.find(edges[e].sourceModelId);
    auto tIt = idToIdx.find(edges[e].targetModelId);
    if (sIt == idToIdx.end() || tIt == idToIdx.end()) continue;
    edgePairs[e] = {sIt->second, tIt->second};
  }

  // Precompute valid edge pairs: skip same-carrier or shared-endpoint.
  std::vector<Pair> pairs;
  pairs.reserve(E * (E - 1) / 2);
  for (std::size_t i = 0; i < E; ++i) {
    const auto& pi = edgePairs[i];
    for (std::size_t j = i + 1; j < E; ++j) {
      const auto& pj = edgePairs[j];
      if (pi.first == pj.first || pi.first == pj.second
          || pi.second == pj.first || pi.second == pj.second) continue;
      if (i < carrierIdByEdge.size() && j < carrierIdByEdge.size()
          && !carrierIdByEdge[i].empty()
          && carrierIdByEdge[i] == carrierIdByEdge[j]) continue;
      pairs.push_back({i, j});
    }
  }

  // Initial positions (baseline).
  std::vector<double> baseline_x(N), baseline_y(N);
  for (std::size_t i = 0; i < N; ++i) {
    baseline_x[i] = attributes.x(nodes[i].handle);
    baseline_y[i] = attributes.y(nodes[i].handle);
  }

  // Cluster centroid + per-member local offset.
  const std::size_t numClusters = clusterMembers.size();
  std::vector<double> centroid_x(numClusters, 0), centroid_y(numClusters, 0);
  for (std::size_t c = 0; c < numClusters; ++c) {
    if (clusterMembers[c].empty()) continue;
    double sx = 0, sy = 0;
    for (std::size_t n : clusterMembers[c]) {
      sx += baseline_x[n];
      sy += baseline_y[n];
    }
    centroid_x[c] = sx / clusterMembers[c].size();
    centroid_y[c] = sy / clusterMembers[c].size();
  }
  std::vector<double> offset_x(N, 0), offset_y(N, 0);
  for (std::size_t i = 0; i < N; ++i) {
    int ci = nodeClusterIdx[i];
    if (ci >= 0) {
      offset_x[i] = baseline_x[i] - centroid_x[ci];
      offset_y[i] = baseline_y[i] - centroid_y[ci];
    }
  }

  // Hub indices.
  std::vector<std::size_t> hubIdx;
  hubIdx.reserve(N);
  std::vector<int> hubOfNode(N, -1);
  for (std::size_t i = 0; i < N; ++i) {
    if (nodeClusterIdx[i] < 0) {
      hubOfNode[i] = static_cast<int>(hubIdx.size());
      hubIdx.push_back(i);
    }
  }
  const std::size_t numHubs = hubIdx.size();

  // Hub-delta: per-absorbed-hub small free offset on top of cluster
  // transform. Identifies absorbed hubs (cluster member + flag set).
  std::vector<int> hubDeltaIdx(N, -1);
  std::vector<std::size_t> hubDeltaNodeIdx;
  if (!isAbsorbedHub.empty()) {
    for (std::size_t i = 0; i < N; ++i) {
      if (i >= isAbsorbedHub.size()) break;
      if (!isAbsorbedHub[i]) continue;
      if (nodeClusterIdx[i] < 0) continue;  // not in a cluster: skip
      hubDeltaIdx[i] = static_cast<int>(hubDeltaNodeIdx.size());
      hubDeltaNodeIdx.push_back(i);
    }
  }
  const std::size_t numHubDelta = hubDeltaNodeIdx.size();

  // Optimizable parameters:
  std::vector<double> trans_x(numClusters, 0), trans_y(numClusters, 0);
  std::vector<double> rot(numClusters, 0);
  std::vector<double> hub_x(numHubs), hub_y(numHubs);
  for (std::size_t h = 0; h < numHubs; ++h) {
    hub_x[h] = baseline_x[hubIdx[h]];
    hub_y[h] = baseline_y[hubIdx[h]];
  }
  // Hub-delta: small free offset for absorbed hubs (init 0).
  std::vector<double> hub_delta_x(numHubDelta, 0);
  std::vector<double> hub_delta_y(numHubDelta, 0);

  // Adam state.
  const std::size_t totalParams =
    2 * numClusters + (opts.enable_rotation ? numClusters : 0)
    + 2 * numHubs;
  std::vector<double> adam_m_trans_x(numClusters, 0),
                      adam_m_trans_y(numClusters, 0);
  std::vector<double> adam_v_trans_x(numClusters, 0),
                      adam_v_trans_y(numClusters, 0);
  std::vector<double> adam_m_rot(numClusters, 0),
                      adam_v_rot(numClusters, 0);
  std::vector<double> adam_m_hx(numHubs, 0), adam_m_hy(numHubs, 0);
  std::vector<double> adam_v_hx(numHubs, 0), adam_v_hy(numHubs, 0);
  std::vector<double> adam_m_hd_x(numHubDelta, 0),
                      adam_m_hd_y(numHubDelta, 0);
  std::vector<double> adam_v_hd_x(numHubDelta, 0),
                      adam_v_hd_y(numHubDelta, 0);
  constexpr double beta1 = 0.9, beta2 = 0.999, adam_eps = 1e-8;

  // Helper: assemble positions from current params.
  std::vector<double> pos_x(N), pos_y(N);
  auto assemble = [&]() {
    for (std::size_t i = 0; i < N; ++i) {
      int ci = nodeClusterIdx[i];
      if (ci >= 0) {
        const double cs = std::cos(rot[ci]);
        const double sn = std::sin(rot[ci]);
        const double rx = offset_x[i] * cs - offset_y[i] * sn;
        const double ry = offset_x[i] * sn + offset_y[i] * cs;
        pos_x[i] = centroid_x[ci] + rx + trans_x[ci];
        pos_y[i] = centroid_y[ci] + ry + trans_y[ci];
        // Hub-delta: small per-absorbed-hub fine offset.
        const int hd = hubDeltaIdx[i];
        if (hd >= 0) {
          pos_x[i] += hub_delta_x[hd];
          pos_y[i] += hub_delta_y[hd];
        }
      } else {
        pos_x[i] = hub_x[hubOfNode[i]];
        pos_y[i] = hub_y[hubOfNode[i]];
      }
    }
  };

  // Hard cross count helper (for tracking only — not used in gradient).
  auto hardCrossCount = [&]() -> std::size_t {
    auto sgn = [](double x) { return (x > 0) - (x < 0); };
    std::size_t total = 0;
    for (const auto& pr : pairs) {
      const auto& pi = edgePairs[pr.i];
      const auto& pj = edgePairs[pr.j];
      const double ax = pos_x[pi.first], ay = pos_y[pi.first];
      const double bx = pos_x[pi.second], by = pos_y[pi.second];
      const double cx = pos_x[pj.first], cy = pos_y[pj.first];
      const double dx = pos_x[pj.second], dy = pos_y[pj.second];
      const int o1 = sgn((bx-ax)*(cy-ay) - (by-ay)*(cx-ax));
      const int o2 = sgn((bx-ax)*(dy-ay) - (by-ay)*(dx-ax));
      const int o3 = sgn((dx-cx)*(ay-cy) - (dy-cy)*(ax-cx));
      const int o4 = sgn((dx-cx)*(by-cy) - (dy-cy)*(bx-cx));
      if (o1 != o2 && o3 != o4 && o1 != 0 && o3 != 0) ++total;
    }
    return total;
  };

  // Initial hard cross.
  assemble();
  result.initial_hard_cross = hardCrossCount();
  result.best_hard_cross = result.initial_hard_cross;

  // Gradient buffers — w.r.t. node positions, then mapped to params.
  std::vector<double> gradPos_x(N, 0), gradPos_y(N, 0);

  // Snapshot params for multi-restart reset.
  const auto trans_x_init = trans_x;
  const auto trans_y_init = trans_y;
  const auto rot_init = rot;
  const auto hub_x_init = hub_x;
  const auto hub_y_init = hub_y;

  // Best-across-restart buffers.
  std::vector<double> best_pos_x = pos_x, best_pos_y = pos_y;
  std::size_t best_hc_overall = result.initial_hard_cross;

  std::fprintf(stderr,
    "[cluster-polish] N=%zu E=%zu pairs=%zu clusters=%zu hubs=%zu "
    "iters=%d lr=%.0f K=%.1f→%.1f rotation=%s restarts=%d jitter=%.0f\n",
    N, E, pairs.size(), numClusters, numHubs,
    opts.iters, opts.lr, opts.sharpness_start, opts.sharpness_end,
    opts.enable_rotation ? "on" : "off",
    opts.num_restarts, opts.jitter_std);
  std::fprintf(stderr,
    "[cluster-polish] initial hard_cross=%zu\n", result.initial_hard_cross);

  const double eps = 1.0;  // matches Python soft_cross_loss eps

  for (int restart = 0; restart < std::max(1, opts.num_restarts); ++restart) {
    // Restart reset: reload initial params, possibly with jitter.
    trans_x = trans_x_init;
    trans_y = trans_y_init;
    rot = rot_init;
    hub_x = hub_x_init;
    hub_y = hub_y_init;
    if (restart > 0 && opts.jitter_std > 0) {
      std::mt19937 rng(opts.seed + restart);
      std::normal_distribution<double> nd(0.0, opts.jitter_std);
      for (std::size_t c = 0; c < numClusters; ++c) {
        trans_x[c] += nd(rng);
        trans_y[c] += nd(rng);
      }
      for (std::size_t h = 0; h < numHubs; ++h) {
        hub_x[h] += nd(rng);
        hub_y[h] += nd(rng);
      }
    }
    // Reset Adam state.
    std::fill(adam_m_trans_x.begin(), adam_m_trans_x.end(), 0.0);
    std::fill(adam_m_trans_y.begin(), adam_m_trans_y.end(), 0.0);
    std::fill(adam_v_trans_x.begin(), adam_v_trans_x.end(), 0.0);
    std::fill(adam_v_trans_y.begin(), adam_v_trans_y.end(), 0.0);
    std::fill(adam_m_rot.begin(), adam_m_rot.end(), 0.0);
    std::fill(adam_v_rot.begin(), adam_v_rot.end(), 0.0);
    std::fill(adam_m_hx.begin(), adam_m_hx.end(), 0.0);
    std::fill(adam_m_hy.begin(), adam_m_hy.end(), 0.0);
    std::fill(adam_v_hx.begin(), adam_v_hx.end(), 0.0);
    std::fill(adam_v_hy.begin(), adam_v_hy.end(), 0.0);
    if (opts.num_restarts > 1) {
      std::fprintf(stderr,
        "[cluster-polish] === restart %d/%d  jitter=%.0f ===\n",
        restart + 1, opts.num_restarts,
        restart == 0 ? 0.0 : opts.jitter_std);
    }

    for (int it = 0; it < opts.iters; ++it) {
    // Anneal sharpness.
    const double K = opts.sharpness_start
      + (opts.sharpness_end - opts.sharpness_start)
        * (static_cast<double>(it) / std::max(1, opts.iters - 1));

    // Forward.
    assemble();

    // Reset position gradient.
    std::fill(gradPos_x.begin(), gradPos_x.end(), 0.0);
    std::fill(gradPos_y.begin(), gradPos_y.end(), 0.0);

    double L_cross = 0.0;
    double L_overlap = 0.0;

    // Overlap loss + gradient. For each pair (i, j), squared hinge:
    //   penalty = max(0, min_dist - dist)²
    // where dist = |pos_i - pos_j|, min_dist = r_i + r_j + margin.
    // Approximate node bbox by circle of radius max(w, h)/2.
    if (opts.w_overlap > 0) {
      const double margin = opts.overlap_margin;
      // Cache node radii.
      static thread_local std::vector<double> radii;
      if (radii.size() != N) {
        radii.assign(N, 0.0);
        for (std::size_t k = 0; k < N; ++k) {
          radii[k] = std::max(nodes[k].width, nodes[k].height) / 2.0;
        }
      }
      for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = i + 1; j < N; ++j) {
          const double dx = pos_x[j] - pos_x[i];
          const double dy = pos_y[j] - pos_y[i];
          const double dist2 = dx * dx + dy * dy;
          const double min_d = radii[i] + radii[j] + margin;
          if (dist2 >= min_d * min_d) continue;
          const double dist = std::sqrt(dist2 + 1e-6);
          const double slack = min_d - dist;
          L_overlap += slack * slack;
          // d(slack²)/d_dist = 2*slack*(-1) = -2*slack
          // d_dist/d_pos_j.x = dx/dist
          // d_dist/d_pos_i.x = -dx/dist
          const double inv_dist = 1.0 / dist;
          const double common = -2.0 * slack * inv_dist;
          // dL_overlap / d_pos_j.x = common * dx
          // dL_overlap / d_pos_i.x = -common * dx
          const double gx = common * dx * opts.w_overlap;
          const double gy = common * dy * opts.w_overlap;
          gradPos_x[j] += gx;
          gradPos_y[j] += gy;
          gradPos_x[i] -= gx;
          gradPos_y[i] -= gy;
        }
      }
    }

    // Bundle-bbox avoidance: penalize edge segments that pass close to
    // any leaf-bundle bbox. Approximate bbox as circle of radius
    // max(half-w, half-h) for analytic-friendly distance.
    double L_bundle = 0.0;
    if (opts.w_bundle_avoid > 0 && !bundleBoxes.empty()) {
      // Pre-compute bundle circles (cx, cy, r).
      static thread_local std::vector<std::array<double, 3>> bundleCircles;
      static thread_local std::vector<std::vector<int>> bundleExempt;
      if (bundleCircles.size() != bundleBoxes.size()) {
        bundleCircles.resize(bundleBoxes.size());
        bundleExempt.resize(bundleBoxes.size());
        for (std::size_t bi = 0; bi < bundleBoxes.size(); ++bi) {
          const auto& b = bundleBoxes[bi];
          const double cx = (b.x_min + b.x_max) * 0.5;
          const double cy = (b.y_min + b.y_max) * 0.5;
          const double half_w = (b.x_max - b.x_min) * 0.5;
          const double half_h = (b.y_max - b.y_min) * 0.5;
          const double r = std::max(half_w, half_h) + opts.bundle_margin;
          bundleCircles[bi] = {cx, cy, r};
          bundleExempt[bi].assign(N, 0);
          for (std::size_t ni : b.exempt_node_indices) {
            if (ni < N) bundleExempt[bi][ni] = 1;
          }
        }
      }
      // For each edge, for each bundle, segment-circle penalty.
      for (std::size_t e = 0; e < E; ++e) {
        const auto& p = edgePairs[e];
        const std::size_t a = p.first;
        const std::size_t b = p.second;
        const double ax = pos_x[a], ay = pos_y[a];
        const double bx = pos_x[b], by = pos_y[b];
        const double abx = bx - ax;
        const double aby = by - ay;
        const double ab_len2 = abx * abx + aby * aby + 1e-3;
        for (std::size_t bi = 0; bi < bundleBoxes.size(); ++bi) {
          if (bundleExempt[bi][a] || bundleExempt[bi][b]) continue;
          const double cx = bundleCircles[bi][0];
          const double cy = bundleCircles[bi][1];
          const double r = bundleCircles[bi][2];
          // t = clamp(((cx - ax) * abx + (cy - ay) * aby) / ab_len2, 0, 1)
          double t_raw = ((cx - ax) * abx + (cy - ay) * aby) / ab_len2;
          double t = t_raw;
          bool clamp_low = false, clamp_hi = false;
          if (t < 0) { t = 0; clamp_low = true; }
          else if (t > 1) { t = 1; clamp_hi = true; }
          const double px = ax + t * abx;
          const double py = ay + t * aby;
          const double ddx = px - cx;
          const double ddy = py - cy;
          const double d2 = ddx * ddx + ddy * ddy + 1e-3;
          const double d = std::sqrt(d2);
          if (d >= r) continue;
          const double slack = r - d;
          L_bundle += slack * slack;
          // Gradient: penalty = (r - d)², d = |closest - center|.
          // d_d/d_px = ddx/d, d_d/d_py = ddy/d.
          // d_penalty/d_px = 2*slack * (-d_d/d_px) = -2*slack*ddx/d
          // d_penalty/d_py = -2*slack*ddy/d
          // d_px/d_ax = 1 - t (when t not clamped, t depends on a/b)
          //            but if clamped, t is fixed → d_px/d_ax = 1 (clamp_low)
          //                                           or 0 (clamp_hi, t=1, px=bx)
          // For simplicity: use the dominant gradient; ignore t's dependence
          // on a/b (it's a small effect compared to direct).
          const double inv_d = 1.0 / d;
          const double dpen_dpx = -2.0 * slack * ddx * inv_d
                                  * opts.w_bundle_avoid;
          const double dpen_dpy = -2.0 * slack * ddy * inv_d
                                  * opts.w_bundle_avoid;
          // px = ax + t*(bx - ax), py = ay + t*(by - ay)
          // d_px/d_ax = 1 - t (treating t as constant)
          // d_px/d_bx = t
          // d_py/d_ay = 1 - t
          // d_py/d_by = t
          gradPos_x[a] += dpen_dpx * (1.0 - t);
          gradPos_x[b] += dpen_dpx * t;
          gradPos_y[a] += dpen_dpy * (1.0 - t);
          gradPos_y[b] += dpen_dpy * t;
          (void)clamp_low; (void)clamp_hi; (void)t_raw;
        }
      }
    }

    // Soft cross loss + analytical gradient w.r.t. positions.
    for (const auto& pr : pairs) {
      const auto& pi = edgePairs[pr.i];
      const auto& pj = edgePairs[pr.j];
      const std::size_t a = pi.first, b = pi.second;
      const std::size_t c = pj.first, d = pj.second;
      const double ax = pos_x[a], ay = pos_y[a];
      const double bx = pos_x[b], by = pos_y[b];
      const double cx = pos_x[c], cy = pos_y[c];
      const double dx = pos_x[d], dy = pos_y[d];

      const double abx = bx - ax, aby = by - ay;
      const double cdx = dx - cx, cdy = dy - cy;
      const double ac_x = cx - ax, ac_y = cy - ay;
      const double ad_x = dx - ax, ad_y = dy - ay;
      const double cb_x = bx - cx, cb_y = by - cy;
      // ca = -ac

      const double ab_len2 = abx * abx + aby * aby + eps;
      const double cd_len2 = cdx * cdx + cdy * cdy + eps;
      const double ac_len2 = ac_x * ac_x + ac_y * ac_y + eps;
      const double ad_len2 = ad_x * ad_x + ad_y * ad_y + eps;
      const double cb_len2 = cb_x * cb_x + cb_y * cb_y + eps;
      const double ab_len = std::sqrt(ab_len2);
      const double cd_len = std::sqrt(cd_len2);
      const double ac_len = std::sqrt(ac_len2);
      const double ad_len = std::sqrt(ad_len2);
      const double cb_len = std::sqrt(cb_len2);

      // Numerators (cross products).
      const double N1 = abx * ac_y - aby * ac_x;
      const double N2 = abx * ad_y - aby * ad_x;
      const double N3 = cdx * (-ac_y) - cdy * (-ac_x);  // ca = -ac
      const double N4 = cdx * cb_y - cdy * cb_x;
      // Denominators.
      const double D1 = ab_len * ac_len;
      const double D2 = ab_len * ad_len;
      const double D3 = cd_len * ac_len;  // ca_len == ac_len
      const double D4 = cd_len * cb_len;
      const double c1 = N1 / D1;
      const double c2 = N2 / D2;
      const double c3 = N3 / D3;
      const double c4 = N4 / D4;

      // Sigmoids.
      const double sp1 = sigmoid(K * c1);
      const double sn1 = 1.0 - sp1;
      const double sp2 = sigmoid(K * c2);
      const double sn2 = 1.0 - sp2;
      const double sp3 = sigmoid(K * c3);
      const double sn3 = 1.0 - sp3;
      const double sp4 = sigmoid(K * c4);
      const double sn4 = 1.0 - sp4;

      // Straddle products.
      const double s1 = sp1 * sn2 + sn1 * sp2;
      const double s2 = sp3 * sn4 + sn3 * sp4;
      const double loss_pair = s1 * s2;
      L_cross += loss_pair;

      // ∂s1/∂c1 = K * sp1*(1-sp1) * sn2 - K * sn1*(1-sn1) * sp2
      //        = K*[sp1*sn1]*sn2 - K*[sn1*sp1]*sp2
      //        = K*sp1*sn1*(sn2 - sp2)
      //        Wait — sp1*(1-sp1) = sp1*sn1, sn1*(1-sn1) = sn1*sp1, so
      //        ∂s1/∂c1 = K*sp1*sn1*sn2 - K*sn1*sp1*sp2 = K*sp1*sn1*(sn2-sp2)
      const double ds1_dc1 = K * sp1 * sn1 * (sn2 - sp2);
      // s1 = sp1*sn2 + sn1*sp2
      // ∂s1/∂c2 = sp1 * (-K*sp2*sn2) + sn1 * (K*sp2*sn2)
      //        = K*sp2*sn2*(sn1 - sp1)
      const double ds1_dc2 = K * sp2 * sn2 * (sn1 - sp1);
      const double ds2_dc3 = K * sp3 * sn3 * (sn4 - sp4);
      const double ds2_dc4 = K * sp4 * sn4 * (sn3 - sp3);

      // ∂loss/∂c1 = s2 * ds1_dc1, etc.
      const double dL_dc1 = s2 * ds1_dc1;
      const double dL_dc2 = s2 * ds1_dc2;
      const double dL_dc3 = s1 * ds2_dc3;
      const double dL_dc4 = s1 * ds2_dc4;

      // Now propagate dL_dc{1..4} to position partials. Each ck = Nk / Dk.
      // ∂ck/∂var = (∂Nk/∂var * Dk - Nk * ∂Dk/∂var) / Dk²
      //         = ∂Nk/∂var / Dk - ck * ∂Dk/∂var / Dk
      //         = (∂Nk/∂var - ck * ∂Dk/∂var) / Dk

      // Helper macros for clarity.
      // For each var (a.x, a.y, b.x, b.y, c.x, c.y, d.x, d.y), compute
      // partials of N1..N4 and D1..D4.

      // Partial derivatives of cross products (numerators).
      // N1 = abx * ac_y - aby * ac_x
      //    = (b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x)
      // ∂N1/∂a.x = -(c.y - a.y) - (-(b.y - a.y)) = -ac_y + aby = aby - ac_y
      // ∂N1/∂a.y = (b.x-a.x)*(-1) - (-1)*(c.x-a.x) = -abx + ac_x = ac_x - abx
      // ∂N1/∂b.x = (c.y - a.y) = ac_y
      // ∂N1/∂b.y = -(c.x - a.x) = -ac_x
      // ∂N1/∂c.x = -(b.y - a.y) = -aby
      // ∂N1/∂c.y = (b.x - a.x) = abx
      // ∂N1/∂d.x = 0, ∂N1/∂d.y = 0

      const double dN1_dax = aby - ac_y;
      const double dN1_day = ac_x - abx;
      const double dN1_dbx = ac_y;
      const double dN1_dby = -ac_x;
      const double dN1_dcx = -aby;
      const double dN1_dcy = abx;
      // dN1/dd.x = 0, dN1/dd.y = 0

      // N2 = abx * ad_y - aby * ad_x
      // ∂N2/∂a.x = aby - ad_y
      // ∂N2/∂a.y = ad_x - abx
      // ∂N2/∂b.x = ad_y, ∂N2/∂b.y = -ad_x
      // ∂N2/∂d.x = -aby, ∂N2/∂d.y = abx
      const double dN2_dax = aby - ad_y;
      const double dN2_day = ad_x - abx;
      const double dN2_dbx = ad_y;
      const double dN2_dby = -ad_x;
      const double dN2_ddx = -aby;
      const double dN2_ddy = abx;

      // N3 = cdx * (-ac_y) - cdy * (-ac_x) = -cdx*ac_y + cdy*ac_x
      //    = cdy*ac_x - cdx*ac_y
      //    = (d.x-c.x)*(c.y-a.y)*0 ... let me redo
      //    cdx = d.x - c.x, cdy = d.y - c.y
      //    -ac_y = -(c.y - a.y) = a.y - c.y
      //    -ac_x = -(c.x - a.x) = a.x - c.x
      //    N3 = cdx*(a.y-c.y) - cdy*(a.x-c.x)
      //       = (d.x-c.x)*(a.y-c.y) - (d.y-c.y)*(a.x-c.x)
      // ∂N3/∂a.x = -(d.y - c.y) = -cdy
      // ∂N3/∂a.y = (d.x - c.x) = cdx
      // ∂N3/∂c.x = -(a.y - c.y) - (d.y - c.y)*(-1)*... wait let me expand.
      //   N3 = (d.x-c.x)*(a.y-c.y) - (d.y-c.y)*(a.x-c.x)
      //   ∂N3/∂c.x = (-1)*(a.y-c.y) - (d.y-c.y)*(-1) = c.y-a.y + d.y-c.y = d.y - a.y
      //                                              = (d.y-c.y) + (c.y-a.y) = cdy + ac_y
      //   Wait: -(a.y-c.y) = c.y - a.y, and -(d.y-c.y)*(-1) = (d.y-c.y) = cdy
      //   So ∂N3/∂c.x = (c.y - a.y) + (d.y - c.y) = d.y - a.y
      //                                            = ad_y
      //   Hmm let me re-derive:
      //   d/dc.x of (d.x-c.x)*(a.y-c.y) = -1*(a.y-c.y) + (d.x-c.x)*0 = -(a.y-c.y) = c.y-a.y
      //   d/dc.x of -(d.y-c.y)*(a.x-c.x) = -(d.y-c.y) * (-1) = d.y - c.y = cdy
      //   Total ∂N3/∂c.x = (c.y - a.y) + cdy = -ac_y + cdy
      // OK let me just compute with the original formula using ca=-ac:
      // N3 = cdx*ca_y - cdy*ca_x where ca_x = -ac_x, ca_y = -ac_y.
      //     ∂(cdx*ca_y)/∂a.x: cdx = d.x-c.x (no a dep). ca_y = -ac_y = a.y-c.y. ∂ca_y/∂a.x = 0.
      //                       So 0.
      //     ∂(cdx*ca_y)/∂a.y: ∂ca_y/∂a.y = 1. So cdx*1 = cdx.
      //     ∂(cdy*ca_x)/∂a.x: ∂ca_x/∂a.x = 1. So cdy.
      //     ∂(cdy*ca_x)/∂a.y: 0.
      //   So ∂N3/∂a.x = -cdy, ∂N3/∂a.y = cdx.
      //   For c: cdx depends on c.x (-1). ca_x depends on c.x (-1). ca_y depends on c.y (-1).
      //   ∂(cdx*ca_y)/∂c.x = (-1)*ca_y = -ca_y = ac_y
      //   ∂(cdx*ca_y)/∂c.y = cdx*(-1) = -cdx
      //   ∂(cdy*ca_x)/∂c.x = cdy*(-1) = -cdy
      //   ∂(cdy*ca_x)/∂c.y = (-1)*ca_x = -ca_x = ac_x
      //   N3 = cdx*ca_y - cdy*ca_x.
      //   ∂N3/∂c.x = ac_y - (-cdy) = ac_y + cdy
      //   ∂N3/∂c.y = -cdx - ac_x
      //   For d: cdx depends on d.x (+1). cdy depends on d.y (+1).
      //   ∂N3/∂d.x = 1*ca_y - 0 = ca_y = -ac_y
      //   ∂N3/∂d.y = 0 - 1*ca_x = -ca_x = ac_x

      const double dN3_dax = -cdy;
      const double dN3_day = cdx;
      const double dN3_dcx = ac_y + cdy;
      const double dN3_dcy = -cdx - ac_x;
      const double dN3_ddx = -ac_y;
      const double dN3_ddy = ac_x;

      // N4 = cdx * cb_y - cdy * cb_x
      //   cb_x = b.x - c.x, cb_y = b.y - c.y
      // ∂N4/∂b.x = -cdy, ∂N4/∂b.y = cdx
      // ∂N4/∂c.x = (-1)*cb_y - 0 - (-1)*cb_x*... let me redo.
      //   N4 = (d.x-c.x)*(b.y-c.y) - (d.y-c.y)*(b.x-c.x)
      //   ∂N4/∂c.x = (-1)*(b.y-c.y) - (d.y-c.y)*(-1) = -(b.y-c.y) + (d.y-c.y)
      //            = c.y-b.y + d.y-c.y = d.y - b.y = (d.y-c.y) - (b.y-c.y) = cdy - cb_y
      //   ∂N4/∂c.y = (d.x-c.x)*(-1) - (-1)*(b.x-c.x) = -(d.x-c.x) + (b.x-c.x)
      //            = -cdx + cb_x = cb_x - cdx
      //   ∂N4/∂d.x = (1)*(b.y-c.y) = cb_y
      //   ∂N4/∂d.y = -(b.x-c.x) = -cb_x

      const double dN4_dbx = -cdy;
      const double dN4_dby = cdx;
      const double dN4_dcx = cdy - cb_y;
      const double dN4_dcy = cb_x - cdx;
      const double dN4_ddx = cb_y;
      const double dN4_ddy = -cb_x;

      // Denominators: D1 = ab_len * ac_len.
      // ∂ab_len/∂a.x = -abx/ab_len  (since ab_len² = (b-a)·(b-a))
      // ∂ab_len/∂a.y = -aby/ab_len
      // ∂ab_len/∂b.x = abx/ab_len, ∂ab_len/∂b.y = aby/ab_len
      // ∂ab_len/∂c.x = ∂ab_len/∂c.y = ∂ab_len/∂d.x = ∂ab_len/∂d.y = 0
      // Similarly for cd, ac, ad, cb.

      const double inv_ab = 1.0 / ab_len;
      const double inv_cd = 1.0 / cd_len;
      const double inv_ac = 1.0 / ac_len;
      const double inv_ad = 1.0 / ad_len;
      const double inv_cb = 1.0 / cb_len;

      // ∂D1/∂a.x = ∂ab_len/∂a.x * ac_len + ab_len * ∂ac_len/∂a.x
      //          = (-abx*inv_ab) * ac_len + ab_len * (-ac_x*inv_ac)
      //          = -(abx*ac_len/ab_len + ab_len*ac_x/ac_len)
      const double dab_dax = -abx * inv_ab;
      const double dab_day = -aby * inv_ab;
      const double dab_dbx = abx * inv_ab;
      const double dab_dby = aby * inv_ab;
      const double dcd_dcx = -cdx * inv_cd;
      const double dcd_dcy = -cdy * inv_cd;
      const double dcd_ddx = cdx * inv_cd;
      const double dcd_ddy = cdy * inv_cd;
      const double dac_dax = -ac_x * inv_ac;
      const double dac_day = -ac_y * inv_ac;
      const double dac_dcx = ac_x * inv_ac;
      const double dac_dcy = ac_y * inv_ac;
      const double dad_dax = -ad_x * inv_ad;
      const double dad_day = -ad_y * inv_ad;
      const double dad_ddx = ad_x * inv_ad;
      const double dad_ddy = ad_y * inv_ad;
      const double dcb_dbx = cb_x * inv_cb;
      const double dcb_dby = cb_y * inv_cb;
      const double dcb_dcx = -cb_x * inv_cb;
      const double dcb_dcy = -cb_y * inv_cb;

      // D1 = ab*ac, D2 = ab*ad, D3 = cd*ac, D4 = cd*cb
      const double dD1_dax = dab_dax * ac_len + ab_len * dac_dax;
      const double dD1_day = dab_day * ac_len + ab_len * dac_day;
      const double dD1_dbx = dab_dbx * ac_len;
      const double dD1_dby = dab_dby * ac_len;
      const double dD1_dcx = ab_len * dac_dcx;
      const double dD1_dcy = ab_len * dac_dcy;
      // dD1/dd = 0

      const double dD2_dax = dab_dax * ad_len + ab_len * dad_dax;
      const double dD2_day = dab_day * ad_len + ab_len * dad_day;
      const double dD2_dbx = dab_dbx * ad_len;
      const double dD2_dby = dab_dby * ad_len;
      const double dD2_ddx = ab_len * dad_ddx;
      const double dD2_ddy = ab_len * dad_ddy;

      const double dD3_dax = cd_len * dac_dax;
      const double dD3_day = cd_len * dac_day;
      const double dD3_dcx = dcd_dcx * ac_len + cd_len * dac_dcx;
      const double dD3_dcy = dcd_dcy * ac_len + cd_len * dac_dcy;
      const double dD3_ddx = dcd_ddx * ac_len;
      const double dD3_ddy = dcd_ddy * ac_len;

      const double dD4_dbx = cd_len * dcb_dbx;
      const double dD4_dby = cd_len * dcb_dby;
      const double dD4_dcx = dcd_dcx * cb_len + cd_len * dcb_dcx;
      const double dD4_dcy = dcd_dcy * cb_len + cd_len * dcb_dcy;
      const double dD4_ddx = dcd_ddx * cb_len;
      const double dD4_ddy = dcd_ddy * cb_len;

      // ∂ck/∂var = (∂Nk/∂var - ck * ∂Dk/∂var) / Dk
      const double inv_D1 = 1.0 / D1, inv_D2 = 1.0 / D2;
      const double inv_D3 = 1.0 / D3, inv_D4 = 1.0 / D4;

      // Aggregate position gradient: dL/dvar = Σk dL/dck * dck/dvar
      // Var: a.x
      double g_ax = 0;
      g_ax += dL_dc1 * (dN1_dax - c1 * dD1_dax) * inv_D1;
      g_ax += dL_dc2 * (dN2_dax - c2 * dD2_dax) * inv_D2;
      g_ax += dL_dc3 * (dN3_dax - c3 * dD3_dax) * inv_D3;
      // c4: no a dep in N4, no a dep in D4 → 0
      double g_ay = 0;
      g_ay += dL_dc1 * (dN1_day - c1 * dD1_day) * inv_D1;
      g_ay += dL_dc2 * (dN2_day - c2 * dD2_day) * inv_D2;
      g_ay += dL_dc3 * (dN3_day - c3 * dD3_day) * inv_D3;

      double g_bx = 0;
      g_bx += dL_dc1 * (dN1_dbx - c1 * dD1_dbx) * inv_D1;
      g_bx += dL_dc2 * (dN2_dbx - c2 * dD2_dbx) * inv_D2;
      // c3: no b dep
      g_bx += dL_dc4 * (dN4_dbx - c4 * dD4_dbx) * inv_D4;
      double g_by = 0;
      g_by += dL_dc1 * (dN1_dby - c1 * dD1_dby) * inv_D1;
      g_by += dL_dc2 * (dN2_dby - c2 * dD2_dby) * inv_D2;
      g_by += dL_dc4 * (dN4_dby - c4 * dD4_dby) * inv_D4;

      double g_cx = 0;
      g_cx += dL_dc1 * (dN1_dcx - c1 * dD1_dcx) * inv_D1;
      // c2: no c dep in N2 (uses ad_x, ad_y, abx, aby), but D2 has no c. → 0
      g_cx += dL_dc3 * (dN3_dcx - c3 * dD3_dcx) * inv_D3;
      g_cx += dL_dc4 * (dN4_dcx - c4 * dD4_dcx) * inv_D4;
      double g_cy = 0;
      g_cy += dL_dc1 * (dN1_dcy - c1 * dD1_dcy) * inv_D1;
      g_cy += dL_dc3 * (dN3_dcy - c3 * dD3_dcy) * inv_D3;
      g_cy += dL_dc4 * (dN4_dcy - c4 * dD4_dcy) * inv_D4;

      double g_dx = 0;
      g_dx += dL_dc2 * (dN2_ddx - c2 * dD2_ddx) * inv_D2;
      g_dx += dL_dc3 * (dN3_ddx - c3 * dD3_ddx) * inv_D3;
      g_dx += dL_dc4 * (dN4_ddx - c4 * dD4_ddx) * inv_D4;
      double g_dy = 0;
      g_dy += dL_dc2 * (dN2_ddy - c2 * dD2_ddy) * inv_D2;
      g_dy += dL_dc3 * (dN3_ddy - c3 * dD3_ddy) * inv_D3;
      g_dy += dL_dc4 * (dN4_ddy - c4 * dD4_ddy) * inv_D4;

      gradPos_x[a] += g_ax;
      gradPos_y[a] += g_ay;
      gradPos_x[b] += g_bx;
      gradPos_y[b] += g_by;
      gradPos_x[c] += g_cx;
      gradPos_y[c] += g_cy;
      gradPos_x[d] += g_dx;
      gradPos_y[d] += g_dy;
    }

    // Add anchor losses + gradients.
    // anchor_trans = sum(trans²): grad += 2*w_anchor_trans*trans
    // anchor_rot = sum(rot²): grad += 2*w_anchor_rot*rot
    // anchor_hub = sum((hub_pos - baseline_hub)²): grad on hub_pos
    // anchor adds to L for tracking; gradients go directly to params.
    // (Position gradient already accumulated above for cross loss.)

    // Project gradPos back to parameter gradients.
    std::vector<double> grad_trans_x(numClusters, 0),
                        grad_trans_y(numClusters, 0);
    std::vector<double> grad_rot(numClusters, 0);
    std::vector<double> grad_hx(numHubs, 0), grad_hy(numHubs, 0);
    std::vector<double> grad_hd_x(numHubDelta, 0), grad_hd_y(numHubDelta, 0);
    for (std::size_t i = 0; i < N; ++i) {
      int ci = nodeClusterIdx[i];
      if (ci >= 0) {
        // pos[i] = centroid[ci] + R(rot[ci]) * offset[i] + trans[ci]
        //         + (hub_delta if absorbed)
        grad_trans_x[ci] += gradPos_x[i];
        grad_trans_y[ci] += gradPos_y[i];
        if (opts.enable_rotation) {
          const double cs = std::cos(rot[ci]);
          const double sn = std::sin(rot[ci]);
          const double ox = offset_x[i], oy = offset_y[i];
          const double drx_dtheta = -ox * sn - oy * cs;
          const double dry_dtheta = ox * cs - oy * sn;
          grad_rot[ci] += gradPos_x[i] * drx_dtheta
                       + gradPos_y[i] * dry_dtheta;
        }
        const int hd = hubDeltaIdx[i];
        if (hd >= 0) {
          grad_hd_x[hd] += gradPos_x[i];
          grad_hd_y[hd] += gradPos_y[i];
        }
      } else {
        const int h = hubOfNode[i];
        grad_hx[h] += gradPos_x[i];
        grad_hy[h] += gradPos_y[i];
      }
    }

    // Add anchor terms to gradients (and track loss separately).
    double L_anchor_trans = 0, L_anchor_rot = 0, L_anchor_hub = 0;
    for (std::size_t c = 0; c < numClusters; ++c) {
      L_anchor_trans += trans_x[c] * trans_x[c] + trans_y[c] * trans_y[c];
      grad_trans_x[c] += 2.0 * opts.w_anchor_trans * trans_x[c];
      grad_trans_y[c] += 2.0 * opts.w_anchor_trans * trans_y[c];
      if (opts.enable_rotation) {
        L_anchor_rot += rot[c] * rot[c];
        grad_rot[c] += 2.0 * opts.w_anchor_rot * rot[c];
      }
    }
    for (std::size_t h = 0; h < numHubs; ++h) {
      const std::size_t i = hubIdx[h];
      const double dx = hub_x[h] - baseline_x[i];
      const double dy = hub_y[h] - baseline_y[i];
      L_anchor_hub += dx * dx + dy * dy;
      grad_hx[h] += 2.0 * opts.w_anchor_hub * dx;
      grad_hy[h] += 2.0 * opts.w_anchor_hub * dy;
    }
    // Hub-delta anchor: keep fine offset small.
    double L_anchor_hub_delta = 0;
    for (std::size_t hd = 0; hd < numHubDelta; ++hd) {
      L_anchor_hub_delta += hub_delta_x[hd] * hub_delta_x[hd]
                          + hub_delta_y[hd] * hub_delta_y[hd];
      grad_hd_x[hd] += 2.0 * opts.w_anchor_hub_delta * hub_delta_x[hd];
      grad_hd_y[hd] += 2.0 * opts.w_anchor_hub_delta * hub_delta_y[hd];
    }

    // Adam update.
    const int t = it + 1;
    const double bc1 = 1.0 - std::pow(beta1, t);
    const double bc2 = 1.0 - std::pow(beta2, t);
    auto adam_step = [&](double& m, double& v, double g, double& p) {
      m = beta1 * m + (1 - beta1) * g;
      v = beta2 * v + (1 - beta2) * g * g;
      const double m_hat = m / bc1;
      const double v_hat = v / bc2;
      p -= opts.lr * m_hat / (std::sqrt(v_hat) + adam_eps);
    };
    for (std::size_t c = 0; c < numClusters; ++c) {
      adam_step(adam_m_trans_x[c], adam_v_trans_x[c],
                grad_trans_x[c], trans_x[c]);
      adam_step(adam_m_trans_y[c], adam_v_trans_y[c],
                grad_trans_y[c], trans_y[c]);
      if (opts.enable_rotation) {
        adam_step(adam_m_rot[c], adam_v_rot[c], grad_rot[c], rot[c]);
      }
    }
    for (std::size_t h = 0; h < numHubs; ++h) {
      adam_step(adam_m_hx[h], adam_v_hx[h], grad_hx[h], hub_x[h]);
      adam_step(adam_m_hy[h], adam_v_hy[h], grad_hy[h], hub_y[h]);
    }
    for (std::size_t hd = 0; hd < numHubDelta; ++hd) {
      adam_step(adam_m_hd_x[hd], adam_v_hd_x[hd],
                grad_hd_x[hd], hub_delta_x[hd]);
      adam_step(adam_m_hd_y[hd], adam_v_hd_y[hd],
                grad_hd_y[hd], hub_delta_y[hd]);
    }

    const double total_L = L_cross
      + opts.w_anchor_trans * L_anchor_trans
      + opts.w_anchor_rot * L_anchor_rot
      + opts.w_anchor_hub * L_anchor_hub
      + opts.w_overlap * L_overlap
      + opts.w_bundle_avoid * L_bundle;

    // Track best.
    if (total_L < result.best_loss || it == 0) {
      result.best_loss = total_L;
    }

    // Periodic log.
    if (opts.log_every > 0 && (it == 0 || (it + 1) % opts.log_every == 0)) {
      assemble();
      const std::size_t hc = hardCrossCount();
      if (hc < result.best_hard_cross) result.best_hard_cross = hc;
      std::fprintf(stderr,
        "[cluster-polish] iter %4d/%d  K=%.1f  L=%.2f  L_cross=%.2f  "
        "hard_cross=%zu\n",
        it + 1, opts.iters, K, total_L, L_cross, hc);
    }
    result.iters_run = it + 1;
  }
    // End of one restart's iter loop. Track best.
    assemble();
    const std::size_t hc_restart = hardCrossCount();
    if (hc_restart < best_hc_overall) {
      best_hc_overall = hc_restart;
      best_pos_x = pos_x;
      best_pos_y = pos_y;
      result.best_hard_cross = hc_restart;
    }
    if (opts.num_restarts > 1) {
      std::fprintf(stderr,
        "[cluster-polish] restart %d done: hard_cross=%zu (best=%zu)\n",
        restart + 1, hc_restart, best_hc_overall);
    }
  }  // end multi-restart loop

  // Apply best.
  pos_x = best_pos_x;
  pos_y = best_pos_y;
  std::fprintf(stderr,
    "[cluster-polish] DONE restarts=%d  best hard_cross=%zu (initial %zu)\n",
    opts.num_restarts, best_hc_overall, result.initial_hard_cross);
  for (std::size_t i = 0; i < N; ++i) {
    attributes.x(nodes[i].handle) = pos_x[i];
    attributes.y(nodes[i].handle) = pos_y[i];
  }
  return result;
}

}  // namespace djerd
