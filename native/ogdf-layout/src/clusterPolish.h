// Cluster-rigid polish — C++ port of scripts/ml-layout-polish-rigid.py.
//
// Optimizes cluster transformations (translation + rotation) and hub
// positions via Adam gradient descent on a soft-cross-count loss.
// Cluster intra-structure (member positions relative to centroid) is
// preserved exactly — only cluster_translation and cluster_rotation are
// learned for cluster members; hubs (non-cluster nodes) are fully free.

#pragma once

#include <string>
#include <vector>

#include <ogdf/basic/Graph.h>
#include <ogdf/basic/GraphAttributes.h>

#include "types.h"

namespace djerd {

struct ClusterPolishOptions {
  int iters = 1500;
  double lr = 100.0;
  double sharpness_start = 5.0;
  double sharpness_end = 20.0;
  double w_anchor_trans = 1e-7;
  double w_anchor_rot = 1e-5;
  double w_anchor_hub = 1e-7;
  double w_overlap = 0.001;
  double overlap_margin = 8.0;
  bool enable_rotation = true;
  // Multi-restart: run polish num_restarts times with different jittered
  // initial conditions. Pick best by hard cross count.
  int num_restarts = 1;
  double jitter_std = 200.0;  // applied to translations and hub positions
  unsigned int seed = 0xC0FFEE;
  // Bundle-bbox avoidance: penalize edges whose segment passes through
  // any leaf-bundle bbox. Each bundle = (x, y, w, h) rect.
  double w_bundle_avoid = 0.0;  // 0 = disabled
  double bundle_margin = 8.0;   // penalty kicks in within margin of bbox
  // Hub-delta: per-absorbed-hub small free offset on top of cluster
  // transform. Allows fine adjustment while preserving rigid base move.
  // Strong anchor keeps delta small.
  double w_anchor_hub_delta = 1e-4;
  // Print progress every N iters (0 = silent).
  int log_every = 100;
};

// Leaf-bundle bbox descriptor for avoidance loss.
struct BundleBox {
  double x_min, y_min, x_max, y_max;
  // Node indices (modelId-based) that ARE in this bundle — edges with
  // an endpoint here are exempt from the avoidance penalty.
  std::vector<std::size_t> exempt_node_indices;
};

struct ClusterPolishResult {
  // Best loss observed (final or earlier — we save best-so-far).
  double best_loss = 0;
  // Best straight-line hard cross count on filtered (carrier-aware) pairs.
  std::size_t best_hard_cross = 0;
  std::size_t initial_hard_cross = 0;
  int iters_run = 0;
};

// Run the polish in-place on `attributes`. `clusterMembers[c]` lists the
// node indices belonging to cluster c. `nodeClusterIdx[i]` is the cluster
// index for node i, or -1 if i is a hub. `carrierIdByEdge[e]` is used to
// skip same-carrier edge pairs (cost-side filter that mirrors the
// reported edgeCrossings semantics).
ClusterPolishResult runClusterPolish(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  const std::vector<std::vector<std::size_t>>& clusterMembers,
  const std::vector<int>& nodeClusterIdx,
  const std::vector<std::string>& carrierIdByEdge,
  const std::vector<BundleBox>& bundleBoxes,
  // True for nodes that were originally hubs but absorbed into a cluster.
  // These get a small free hub_delta on top of cluster transform.
  // Pass empty vector for no hybrid (pure rigid).
  const std::vector<bool>& isAbsorbedHub,
  const ClusterPolishOptions& opts);

}  // namespace djerd
