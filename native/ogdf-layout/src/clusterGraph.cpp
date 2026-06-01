#include "clusterGraph.h"

#include <ogdf/basic/Graph.h>
#include <ogdf/basic/GraphAttributes.h>
#include <ogdf/energybased/FMMMLayout.h>
#include <ogdf/energybased/fmmm/FMMMOptions.h>
#include <ogdf/layered/BarycenterHeuristic.h>
#include <ogdf/layered/OptimalHierarchyLayout.h>
#include <ogdf/layered/OptimalRanking.h>
#include <ogdf/layered/SugiyamaLayout.h>
#include <ogdf/planarity/PlanarSubgraphFast.h>
#include <ogdf/planarity/PlanarizationLayout.h>
#include <ogdf/planarity/SubgraphPlanarizer.h>
#include <ogdf/planarity/VariableEmbeddingInserter.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <queue>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace djerd {

namespace {

constexpr double kInnerLeafRingPad = 16.0;
constexpr double kInnerInternalRingPad = 32.0;
constexpr double kInnerBridgeRingPad = 48.0;
constexpr double kSuperNodePadding = 24.0;

// FMMM randSeed is independent from ogdf::setSeed (the global RNG).
// To make multistart actually vary the layout, each FMMM instance must
// be re-seeded explicitly. main.cpp sets DJERD_FMMM_SEED per run; we
// read it here so every FMMM call inside cluster_graph picks it up.
// When unset, OGDF's built-in default (100) stays in force, preserving
// pre-multistart determinism.
inline int fmmmEnvSeedOr(int fallback) {
  const char* env = std::getenv("DJERD_FMMM_SEED");
  if (!env || env[0] == '\0') return fallback;
  const int v = std::atoi(env);
  return v >= 0 ? v : fallback;
}
inline void applyFmmmEnvSeed(ogdf::FMMMLayout& fmmm) {
  fmmm.randSeed(fmmmEnvSeedOr(100));
}

// Build dedup adjacency (parallel edges count as 1). Returns adj[i] = sorted
// list of distinct neighbour indices. Self-loops excluded.
std::vector<std::vector<std::size_t>> buildDedupAdjacency(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  std::size_t& outEdgeCount) {
  const std::size_t n = nodes.size();
  std::unordered_map<std::string, std::size_t> idToIdx;
  idToIdx.reserve(n);
  for (std::size_t i = 0; i < n; ++i) idToIdx[nodes[i].modelId] = i;

  std::vector<std::set<std::size_t>> sets(n);
  for (const EdgeRecord& edge : edges) {
    auto sIt = idToIdx.find(edge.sourceModelId);
    auto tIt = idToIdx.find(edge.targetModelId);
    if (sIt == idToIdx.end() || tIt == idToIdx.end()) continue;
    if (sIt->second == tIt->second) continue;
    sets[sIt->second].insert(tIt->second);
    sets[tIt->second].insert(sIt->second);
  }

  std::vector<std::vector<std::size_t>> adj(n);
  std::size_t edgeCount = 0;
  for (std::size_t i = 0; i < n; ++i) {
    adj[i].assign(sets[i].begin(), sets[i].end());
    edgeCount += sets[i].size();
  }
  outEdgeCount = edgeCount / 2;
  return adj;
}

// Pick the root of each cluster: the member with the highest dedup-degree
// (ties broken by lowest modelId for determinism).
std::unordered_map<std::string, std::size_t> pickRoots(
  const std::vector<NodeRecord>& nodes,
  const std::vector<std::string>& clusterLabels,
  const std::vector<std::vector<std::size_t>>& adj) {
  std::unordered_map<std::string, std::size_t> rootByCluster;
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    const std::string& cid = clusterLabels[i];
    if (cid.empty()) continue;
    auto it = rootByCluster.find(cid);
    if (it == rootByCluster.end()) {
      rootByCluster[cid] = i;
    } else {
      const std::size_t cur = it->second;
      const std::size_t curDeg = adj[cur].size();
      const std::size_t newDeg = adj[i].size();
      if (newDeg > curDeg
          || (newDeg == curDeg && nodes[i].modelId < nodes[cur].modelId)) {
        it->second = i;
      }
    }
  }
  return rootByCluster;
}

// Iterative leaf-pruning (graph-terminology.md §2.5). At each level, all
// nodes with deg ≤ 1 are removed simultaneously. Continues until no more
// deg ≤ 1 nodes remain (= 2-core).
//
// Returns: vector of PrunedNodeRecord (in pruning order, level 1 first),
// boolean array `inCore` (true = node remains in 2-core), and max level.
struct PruneResult {
  std::vector<PrunedNodeRecord> records;
  std::vector<bool> inCore;
  std::size_t maxLevel = 0;
  std::size_t aloneRootCount = 0;
};

PruneResult computePruning(
  const std::vector<std::vector<std::size_t>>& adj) {
  const std::size_t n = adj.size();
  std::vector<std::set<std::size_t>> mut(n);
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j : adj[i]) mut[i].insert(j);
  }
  PruneResult result;
  result.inCore.assign(n, true);

  std::size_t level = 0;
  while (true) {
    ++level;

    // Step A: deg ≤ 1 pruning (leaves + alone roots)
    std::vector<std::size_t> toPrune;
    for (std::size_t i = 0; i < n; ++i) {
      if (!result.inCore[i]) continue;
      if (mut[i].size() <= 1) toPrune.push_back(i);
    }
    if (!toPrune.empty()) {
      for (std::size_t i : toPrune) {
        PrunedNodeRecord rec;
        rec.nodeIdx = i;
        rec.level = level;
        if (mut[i].empty()) {
          rec.parentIdx = i;
          rec.isAloneRoot = true;
          ++result.aloneRootCount;
        } else {
          rec.parentIdx = *mut[i].begin();
          rec.isAloneRoot = false;
        }
        result.records.push_back(rec);
        result.inCore[i] = false;
        for (std::size_t j : mut[i]) mut[j].erase(i);
        mut[i].clear();
      }
      result.maxLevel = level;
      continue;
    }

    // Step B: Wing pruning (graph-terminology.md §1.1.2).
    // No deg ≤ 1 nodes remain. Look for "wings" = deg-2 chains in the
    // current 2-core that close back to a SINGLE anchor (deg ≥ 3 node).
    // Such a closed deg-2 cycle attached to one anchor is semantically a
    // leaf-bundle of that anchor, not part of the structural backbone.
    // Prune the chain intermediates as level-N pruned leaves of the anchor.
    bool wingFound = false;
    for (std::size_t anchor = 0; anchor < n; ++anchor) {
      if (!result.inCore[anchor]) continue;
      if (mut[anchor].size() < 3) continue;  // anchor must have deg ≥ 3
      std::vector<std::size_t> neighbours(mut[anchor].begin(), mut[anchor].end());
      for (std::size_t y : neighbours) {
        if (mut[y].size() != 2) continue;
        // Walk from anchor via y: follow deg-2 chain looking for closure back to anchor.
        std::vector<std::size_t> wingChain = {y};
        std::unordered_set<std::size_t> wingSet = {y};
        std::size_t prev = anchor;
        std::size_t curr = y;
        bool closed = false;
        while (true) {
          std::size_t next = std::numeric_limits<std::size_t>::max();
          for (std::size_t nb : mut[curr]) {
            if (nb != prev) { next = nb; break; }
          }
          if (next == std::numeric_limits<std::size_t>::max()) break;
          if (next == anchor) { closed = true; break; }
          if (mut[next].size() != 2) break;       // chain hits high-deg → not single-anchor wing
          if (wingSet.count(next)) break;          // chain self-loops weirdly
          wingChain.push_back(next);
          wingSet.insert(next);
          prev = curr;
          curr = next;
        }
        if (closed) {
          for (std::size_t w : wingChain) {
            PrunedNodeRecord rec;
            rec.nodeIdx = w;
            rec.level = level;
            rec.parentIdx = anchor;
            rec.isAloneRoot = false;
            result.records.push_back(rec);
            result.inCore[w] = false;
            for (std::size_t nb : mut[w]) mut[nb].erase(w);
            mut[w].clear();
          }
          result.maxLevel = level;
          wingFound = true;
          break;  // exit y-loop; restart anchor scan in next iter
        }
      }
      if (wingFound) break;
    }
    if (wingFound) continue;

    // No deg ≤ 1, no wings → done. Last increment didn't apply.
    --level;
    break;
  }
  return result;
}

// Identify external-root reach for each non-root node. Returns map nodeIdx →
// set of cluster ids whose roots this node is directly connected to.
// Caller decides connector vs router based on size of the set:
//   exactly 2 → Connector
//   ≥ 3       → Router
// Singleton-cluster "roots" (cluster size = 1, effectively leaves disguised
// as roots) are EXCLUDED from the reach count — leaves don't affect
// connector/router classification (graph-terminology.md user note).
std::unordered_map<std::size_t, std::set<std::string>> reachableRoots(
  const std::vector<std::vector<std::size_t>>& adj,
  const std::unordered_map<std::string, std::size_t>& rootByCluster,
  const std::vector<std::string>& clusterLabels,
  const std::unordered_set<std::size_t>& singletonRoots) {
  std::unordered_set<std::size_t> rootSet;
  for (const auto& [_, idx] : rootByCluster) rootSet.insert(idx);

  std::unordered_map<std::size_t, std::set<std::string>> reach;
  for (std::size_t i = 0; i < adj.size(); ++i) {
    if (rootSet.count(i)) continue;  // roots themselves are not connectors/routers
    std::set<std::string> roots;
    for (std::size_t j : adj[i]) {
      if (!rootSet.count(j)) continue;
      if (singletonRoots.count(j)) continue;  // ignore leaf-equivalent roots
      roots.insert(clusterLabels[j]);
    }
    if (roots.size() >= 2) {
      reach[i] = std::move(roots);
    }
  }
  return reach;
}

// Classify a non-root, non-connector cluster member.
MemberCategory classifyMember(
  std::size_t i,
  const std::vector<std::size_t>& neighbours,
  std::size_t myRoot,
  const std::unordered_set<std::size_t>& memberSet,
  const std::unordered_map<std::size_t, std::string>& rootToCluster) {
  bool connectsToOwnRoot = false;
  bool connectsToOtherIntra = false;
  bool connectsToOtherRoot = false;
  for (std::size_t j : neighbours) {
    if (j == myRoot) {
      connectsToOwnRoot = true;
      continue;
    }
    if (rootToCluster.count(j)) {
      connectsToOtherRoot = true;
      continue;
    }
    if (memberSet.count(j)) {
      connectsToOtherIntra = true;
    }
  }
  if (connectsToOwnRoot && connectsToOtherRoot && !connectsToOtherIntra) {
    return MemberCategory::Bridge;
  }
  if (connectsToOwnRoot && connectsToOtherIntra) {
    // Self-loop leaf demotion (graph-terminology.md §1.1.2): a deg-2
    // member with no external root connection is on a self-cycle of its
    // own root (e.g., A-B-C-A). Topologically a ring but semantically a
    // leaf attached to A. Place on the inward leaf arc, not the side
    // internal arc.
    if (neighbours.size() == 2 && !connectsToOtherRoot) {
      return MemberCategory::Leaf;
    }
    return MemberCategory::Internal;
  }
  // Bridge can also have intra-cluster ties; we still call it Bridge if it
  // reaches another root.
  if (connectsToOtherRoot) {
    return MemberCategory::Bridge;
  }
  return MemberCategory::Leaf;
  (void)i;
}

// Estimate cluster diameter from member counts. Used to size super-nodes
// BEFORE the actual inner radial runs (since the radial pass needs the
// outward angle from the super-graph layout, which needs sizes first).
double estimateClusterDiameter(
  const ClusterRecord& cluster,
  const std::vector<NodeRecord>& nodes) {
  const NodeRecord& root = nodes[cluster.rootIdx];
  double maxW = root.width;
  double maxH = root.height;
  std::size_t nonRoot = 0;
  for (const ClusterMemberInfo& m : cluster.members) {
    if (m.category == MemberCategory::Root) continue;
    maxW = std::max(maxW, nodes[m.nodeIdx].width);
    maxH = std::max(maxH, nodes[m.nodeIdx].height);
    ++nonRoot;
  }
  // Heuristic: enough radius to fit sqrt(N) nodes per circumference span.
  const double scale = 1.4;
  const double effW = std::max(maxW, maxH);
  const double diameter = std::max(120.0,
    scale * (effW + 16.0) * std::sqrt(static_cast<double>(nonRoot + 1)));
  return diameter;
}

// Place a list of members on an arc centred at thetaCenter, spanning arcRad,
// at the specified radius. Caller is responsible for picking a radius that
// fits all members without overlap.
void placeArcAt(
  const std::vector<std::size_t>& mems,
  double thetaCenter,
  double arcRad,
  double radius,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  const std::size_t L = mems.size();
  if (L == 0) return;
  if (L == 1) {
    attributes.x(nodes[mems[0]].handle) = radius * std::cos(thetaCenter);
    attributes.y(nodes[mems[0]].handle) = radius * std::sin(thetaCenter);
    return;
  }
  const double step = arcRad / static_cast<double>(L - 1);
  for (std::size_t i = 0; i < L; ++i) {
    const double theta = thetaCenter - arcRad / 2.0 + step * static_cast<double>(i);
    const NodeRecord& nd = nodes[mems[i]];
    attributes.x(nd.handle) = radius * std::cos(theta);
    attributes.y(nd.handle) = radius * std::sin(theta);
  }
}

// Place L members on a multi-ring fan (concentric arc stacking).
// Used when L is large enough that a single arc would need a huge radius.
// Returns the outermost radius (= radius of last ring) so callers can size
// the cluster bbox.
//
// Algorithm: starting at minR, fit as many members as possible on each ring
// (chord-fit on the arc), then step outward by radialStep for the next ring.
// Smaller arcRad → tighter wedge → more rings, less radial spread.
double placeArcMultiRing(
  const std::vector<std::size_t>& mems,
  double thetaCenter,
  double arcRad,
  double minR,
  double maxNodeW,
  double maxNodeH,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  const std::size_t L = mems.size();
  if (L == 0) return minR;
  const double slotChord = maxNodeW + 6.0;
  const double radialStep = std::max(maxNodeH, maxNodeW * 0.4) + 6.0;

  std::size_t placed = 0;
  double R = minR;
  double lastR = minR;
  while (placed < L) {
    const double arcLen = arcRad * R;
    std::size_t cap = std::max<std::size_t>(1,
      static_cast<std::size_t>(std::floor(arcLen / slotChord)));
    cap = std::min(cap, L - placed);
    std::vector<std::size_t> ringMems(mems.begin() + placed,
                                       mems.begin() + placed + cap);
    placeArcAt(ringMems, thetaCenter, arcRad, R, nodes, attributes);
    placed += cap;
    lastR = R;
    R += radialStep;
  }
  return lastR;
}

// Bubble inner placement (§11): root at centre, ALL members packed in
// concentric rings around it (full 360° per ring). Each cluster is a
// circular bubble. No outward awareness — used for the Bubble layout mode.
std::pair<double, double> innerBubbleFill(
  const ClusterRecord& cluster,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  const NodeRecord& root = nodes[cluster.rootIdx];
  attributes.x(root.handle) = 0.0;
  attributes.y(root.handle) = 0.0;

  std::vector<std::size_t> members;
  double maxW = root.width;
  double maxH = root.height;
  for (const ClusterMemberInfo& m : cluster.members) {
    if (m.category == MemberCategory::Root) continue;
    members.push_back(m.nodeIdx);
    maxW = std::max(maxW, nodes[m.nodeIdx].width);
    maxH = std::max(maxH, nodes[m.nodeIdx].height);
  }
  if (members.empty()) {
    return {root.width, root.height};
  }
  // Sort for determinism.
  std::sort(members.begin(), members.end());

  const double pi = 3.14159265358979;
  const double slotChord = maxW + 6.0;       // chord per ring slot
  const double radialStep = std::max(maxH, maxW * 0.4) + 6.0;

  std::size_t placed = 0;
  double R = std::max(root.width, root.height) / 2.0 + maxW / 2.0 + 12.0;
  while (placed < members.size()) {
    const double arcLen = 2.0 * pi * R;
    std::size_t cap = std::max<std::size_t>(1,
      static_cast<std::size_t>(std::floor(arcLen / slotChord)));
    cap = std::min(cap, members.size() - placed);

    if (cap == 1) {
      attributes.x(nodes[members[placed]].handle) = R;
      attributes.y(nodes[members[placed]].handle) = 0.0;
    } else {
      const double step = 2.0 * pi / static_cast<double>(cap);
      for (std::size_t k = 0; k < cap; ++k) {
        const double theta = step * static_cast<double>(k);
        attributes.x(nodes[members[placed + k]].handle) = R * std::cos(theta);
        attributes.y(nodes[members[placed + k]].handle) = R * std::sin(theta);
      }
    }
    placed += cap;
    R += radialStep;
  }
  const double diameter = 2.0 * R + maxW + 8.0;
  return {diameter, diameter};
}

// Compute the minimum radius needed to fit L nodes (each width w, padding pad)
// on an arc of arcRad radians.
double radiusForArc(std::size_t L, double arcRad, double w, double pad,
                    double minR) {
  if (L <= 1) return minR;
  const double step = arcRad / static_cast<double>(L - 1);
  const double need = (w + pad) / (2.0 * std::max(1e-3, std::sin(step / 2.0)));
  return std::max(minR, need);
}

// Outward-aware inner radial. theta_outward = angle (radians) toward the
// centroid of this cluster's super-graph neighbours. Bridges go on the
// outward 180° arc, leaves on the opposite inward 180° arc, internals split
// across two 30° side arcs.
//
// PER-CATEGORY RADIUS: leaves get their OWN tight radius based on leaf
// width + leaf count (NOT inflated by bridge count). Bridges spread further
// if needed. Internals on side arcs use their own. This keeps leaves close
// to the root visually.
std::pair<double, double> innerRadialWithOutward(
  const ClusterRecord& cluster,
  const std::vector<NodeRecord>& nodes,
  double thetaOutward,
  ogdf::GraphAttributes& attributes) {
  const NodeRecord& root = nodes[cluster.rootIdx];
  attributes.x(root.handle) = 0.0;
  attributes.y(root.handle) = 0.0;

  std::vector<std::size_t> leaves, internals, bridges;
  double maxLeafW = 0.0, maxInternalW = 0.0, maxBridgeW = 0.0;
  for (const ClusterMemberInfo& m : cluster.members) {
    if (m.category == MemberCategory::Root) continue;
    const double w = nodes[m.nodeIdx].width;
    switch (m.category) {
      case MemberCategory::Leaf:
        leaves.push_back(m.nodeIdx);
        maxLeafW = std::max(maxLeafW, w);
        break;
      case MemberCategory::Internal:
        internals.push_back(m.nodeIdx);
        maxInternalW = std::max(maxInternalW, w);
        break;
      case MemberCategory::Bridge:
        bridges.push_back(m.nodeIdx);
        maxBridgeW = std::max(maxBridgeW, w);
        break;
      case MemberCategory::Root: break;
    }
  }
  const double pi = 3.14159265358979;

  // Per-category min radius = half-root + half-category-node + small pad.
  const double rootHalf = std::max(root.width, root.height) / 2.0;
  auto minRadiusFor = [&](double w) {
    return rootHalf + w / 2.0 + 8.0;  // tight: 8 px gap between root edge and node edge
  };
  const double minRLeaf = minRadiusFor(maxLeafW);
  const double minRInternal = minRadiusFor(maxInternalW);
  const double minRBridge = minRadiusFor(maxBridgeW);

  // §3.2 leaf vs bridge angular separation: leaves on the inward hemisphere,
  // bridges on the outward hemisphere, internals on side gaps.
  //
  // Leaves use a NARROW arc (90° / pi/2) and stack into multiple concentric
  // rings. Without ring stacking, a 1st-degree hub with many leaves blew up
  // its single 180° arc to a huge radius (e.g., 43 leaves → R≈2700), which
  // crowds the whole layout, leaves no angular room for the bridge/connector
  // edges that form the backbone, and pulls the cluster bbox far across the
  // canvas (user feedback 2026-04-28). With multi-ring 90° leaves the same
  // 43 leaves fit within R≈700 and the remaining 270° is free for
  // backbone-direction edges.
  const double leafArc = leaves.empty() ? 0.0 : pi / 2.0;          // 90°
  const double arcBridge = bridges.empty() ? 0.0 : pi;             // 180° outward
  std::vector<std::size_t> internalsA, internalsB;
  for (std::size_t k = 0; k < internals.size(); ++k) {
    (k % 2 == 0 ? internalsA : internalsB).push_back(internals[k]);
  }
  const double arcInternal = internals.empty() ? 0.0 : pi / 6.0;   // 30° each

  const double thetaBridge = thetaOutward;
  const double thetaInternalA = thetaOutward + pi / 2.0;
  const double thetaInternalB = thetaOutward - pi / 2.0;
  const double thetaLeaf = thetaOutward + pi;                       // opposite

  // Bridges still single-ring (chord-fit) — they aim at external destinations,
  // so spreading them apart on a wider arc reduces edge bend.
  const double bridgePad = 14.0;
  const double rBridge = bridges.empty() ? 0.0
    : radiusForArc(bridges.size(), arcBridge, maxBridgeW, bridgePad, minRBridge);
  const double internalPad = 10.0;
  const double rIntA = internalsA.empty() ? 0.0
    : radiusForArc(internalsA.size(), arcInternal, maxInternalW, internalPad, minRInternal);
  const double rIntB = internalsB.empty() ? 0.0
    : radiusForArc(internalsB.size(), arcInternal, maxInternalW, internalPad, minRInternal);

  placeArcAt(bridges, thetaBridge, arcBridge, rBridge, nodes, attributes);
  placeArcAt(internalsA, thetaInternalA, arcInternal, rIntA, nodes, attributes);
  placeArcAt(internalsB, thetaInternalB, arcInternal, rIntB, nodes, attributes);

  // Multi-ring leaf placement on the narrow inward arc.
  const double rLeafOuter = leaves.empty() ? 0.0
    : placeArcMultiRing(leaves, thetaLeaf, leafArc, minRLeaf,
                        maxLeafW, nodes[cluster.rootIdx].height,
                        nodes, attributes);

  // Cluster bbox = furthest node distance × 2 + max-width-of-furthest-cat.
  double maxR = 0.0;
  double maxWAtMaxR = 0.0;
  if (rLeafOuter > maxR) { maxR = rLeafOuter; maxWAtMaxR = maxLeafW; }
  if (rBridge > maxR) { maxR = rBridge; maxWAtMaxR = maxBridgeW; }
  if (rIntA > maxR) { maxR = rIntA; maxWAtMaxR = maxInternalW; }
  if (rIntB > maxR) { maxR = rIntB; maxWAtMaxR = maxInternalW; }
  if (maxR == 0.0) {
    // Cluster is just the root.
    return {root.width, root.height};
  }
  const double diameter = 2.0 * maxR + maxWAtMaxR + 8.0;
  return {diameter, diameter};
}

}  // namespace

ClusterGraphResult runClusterGraphLayout(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::string>& clusterLabels,
  ogdf::GraphAttributes& attributes,
  bool bubbleMode) {
  ClusterGraphResult result;
  if (nodes.empty()) return result;

  // Timing checkpoints — used to localise stalls when downstream passes
  // scale poorly with the upstream community structure.
  const auto cgTStart = std::chrono::steady_clock::now();
  auto cgCheckpoint = [&cgTStart](const char* tag) {
    const auto now = std::chrono::steady_clock::now();
    const long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - cgTStart).count();
    std::fprintf(stderr, "[cg-time] %ldms %s\n", ms, tag);
  };

  // Performance: when the caller will overwrite every node position from a
  // --positions-tsv (the ML reroute / bbox-target / polish round-trips), the
  // cluster_graph POSITIONING is wasted — only the STRUCTURE (clusters,
  // pruning, super-graph, leaf-bundle membership) it produces is used (for
  // routing/carriers). Skip the expensive positioning passes (super-graph
  // FMMM, connector/router untangling); cheap structure code still runs.
  const bool skipCgPositioning = [] {
    const char* e = std::getenv("DJERD_CG_SKIP_POSITIONING");
    return e && std::strcmp(e, "0") != 0;
  }();
  if (skipCgPositioning) {
    std::fprintf(stderr,
      "[cg-fast] DJERD_CG_SKIP_POSITIONING=1 — positions overwritten downstream; "
      "skipping super-graph FMMM + connector/router untangling\n");
  }

  // 1. dedup adjacency.
  std::size_t edgeCount = 0;
  std::vector<std::vector<std::size_t>> adj = buildDedupAdjacency(nodes, edges, edgeCount);
  result.deduplicatedEdges = edgeCount;

  // 1a. Per-node "has parallel edges" flag (user rule: nodes whose adjacency
  // includes any neighbour reached by ≥2 raw edges are excluded from leaf
  // classification). Uses unordered pair counts on the raw `edges` list, so
  // that FK + auto-derived reverse-FK pairs (Django default) count as 2.
  std::unordered_map<std::string, std::size_t> idToIdxParallel;
  idToIdxParallel.reserve(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    idToIdxParallel[nodes[i].modelId] = i;
  }
  std::unordered_map<long long, std::size_t> pairEdgeCount;
  for (const EdgeRecord& edge : edges) {
    auto sIt = idToIdxParallel.find(edge.sourceModelId);
    auto tIt = idToIdxParallel.find(edge.targetModelId);
    if (sIt == idToIdxParallel.end() || tIt == idToIdxParallel.end()) continue;
    if (sIt->second == tIt->second) continue;
    const std::size_t lo = std::min(sIt->second, tIt->second);
    const std::size_t hi = std::max(sIt->second, tIt->second);
    const long long key = (static_cast<long long>(lo) << 32) | static_cast<long long>(hi);
    pairEdgeCount[key] += 1;
  }
  std::vector<bool> nodeHasParallelEdges(nodes.size(), false);
  for (const auto& kv : pairEdgeCount) {
    if (kv.second < 2) continue;
    const std::size_t lo = static_cast<std::size_t>(kv.first >> 32);
    const std::size_t hi = static_cast<std::size_t>(kv.first & 0xffffffffLL);
    if (lo < nodeHasParallelEdges.size()) nodeHasParallelEdges[lo] = true;
    if (hi < nodeHasParallelEdges.size()) nodeHasParallelEdges[hi] = true;
  }

  // 1b. Graph pruning (graph-terminology.md §2.5). Iteratively remove leaves
  // and alone-roots until only the 2-core remains. Records (level, parent)
  // for each pruned node, used in §11 for reverse-order re-attachment.
  PruneResult pruneRes = computePruning(adj);
  result.prunedNodes = pruneRes.records;
  result.maxPruningLevel = pruneRes.maxLevel;
  result.aloneRootCount = pruneRes.aloneRootCount;
  for (bool b : pruneRes.inCore) if (b) ++result.coreNodeCount;
  cgCheckpoint("post-§2.5-pruning");

  // 2. roots.
  auto rootByCluster = pickRoots(nodes, clusterLabels, adj);

  // 2b. Singleton-cluster filter (graph-terminology.md user note: leaves
  // shouldn't affect connector/router/polar classification).
  // A cluster of size 1 is just a leaf disguised as a cluster — its "root"
  // shouldn't elevate any neighbour to connector/router. We:
  //   - track singleton roots so reach/polar logic ignores them
  //   - skip singleton clusters in ClusterRecord building
  //   - emit their nodes as independents instead
  std::unordered_map<std::string, std::size_t> clusterSize;
  for (const std::string& cid : clusterLabels) {
    if (!cid.empty()) ++clusterSize[cid];
  }
  std::unordered_set<std::size_t> singletonRoots;
  std::unordered_set<std::string> singletonClusters;
  for (const auto& [cid, idx] : rootByCluster) {
    auto sIt = clusterSize.find(cid);
    if (sIt != clusterSize.end() && sIt->second <= 1) {
      singletonRoots.insert(idx);
      singletonClusters.insert(cid);
    }
  }
  result.singletonClusterCount = singletonClusters.size();

  // 2c. Spurious cluster demotion (graph-terminology.md §3.1 stricter root rule).
  //
  // A cluster of size ≤ 2 whose root has ≥ 2 OTHER cluster roots as direct
  // neighbours is essentially a "connector + leaf" pattern, not a real
  // domain cluster. Louvain micro-clusters of this shape pollute the cluster
  // super-graph with spaghetti when treated as roots — they get a cluster
  // super-node, root-root edges to their externals, etc.
  //
  // Demotion: remove the cluster from rootByCluster (root becomes non-root,
  // gets reach computed → becomes connector/router by §3 rules); add cluster
  // to singletonClusters set so its members fall through to independents
  // (and the leaf is re-attached as a leaf of the connector via §12 pruning).
  {
    std::unordered_map<std::size_t, std::string> idxToCid;
    for (const auto& kv : rootByCluster) {
      if (singletonRoots.count(kv.second)) continue;
      idxToCid[kv.second] = kv.first;
    }

    std::vector<std::string> demoteList;
    for (const auto& kv : rootByCluster) {
      const std::string& cid = kv.first;
      const std::size_t rootIdx = kv.second;
      if (singletonClusters.count(cid)) continue;  // already demoted
      auto szIt = clusterSize.find(cid);
      if (szIt == clusterSize.end() || szIt->second > 2) continue;  // size ≤ 2

      std::set<std::string> ext;
      for (std::size_t j : adj[rootIdx]) {
        auto cIt = idxToCid.find(j);
        if (cIt != idxToCid.end() && cIt->second != cid) {
          ext.insert(cIt->second);
          if (ext.size() >= 2) break;
        }
      }
      if (ext.size() >= 2) demoteList.push_back(cid);
    }

    for (const std::string& cid : demoteList) {
      auto rIt = rootByCluster.find(cid);
      if (rIt != rootByCluster.end()) {
        singletonRoots.insert(rIt->second);
        rootByCluster.erase(rIt);
      }
      singletonClusters.insert(cid);
    }
    result.spuriousClusterCount = demoteList.size();
  }

  // rootToCluster excludes singleton roots so member classification and
  // polar detection naturally treat them as non-roots.
  std::unordered_map<std::size_t, std::string> rootToCluster;
  for (const auto& [cid, idx] : rootByCluster) {
    if (singletonRoots.count(idx)) continue;
    rootToCluster[idx] = cid;
  }

  // 3. Reach map: for each non-root node, which cluster roots it touches.
  auto reach = reachableRoots(adj, rootByCluster, clusterLabels, singletonRoots);

  // 4. Split into connectors (=2 roots) and routers (≥3 roots). Both are
  // pulled OUT of any cluster.
  std::unordered_set<std::size_t> connectorSet;
  std::unordered_set<std::size_t> routerSet;
  for (const auto& [idx, roots] : reach) {
    if (roots.size() == 2) connectorSet.insert(idx);
    else if (roots.size() >= 3) routerSet.insert(idx);
  }

  std::unordered_map<std::size_t, std::string> nodeCluster;
  std::unordered_map<std::string, std::vector<std::size_t>> clusterMembers;
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    if (connectorSet.count(i) || routerSet.count(i)) continue;
    const std::string& cid = clusterLabels[i];
    if (cid.empty()) continue;
    if (singletonClusters.count(cid)) continue;  // singleton → independent
    nodeCluster[i] = cid;
    clusterMembers[cid].push_back(i);
  }

  // 5. Build ClusterRecords + classify members.
  for (auto& [cid, members] : clusterMembers) {
    auto rIt = rootByCluster.find(cid);
    if (rIt == rootByCluster.end()) continue;
    const std::size_t rootIdx = rIt->second;
    if (connectorSet.count(rootIdx) || routerSet.count(rootIdx)) continue;
    if (singletonRoots.count(rootIdx)) continue;

    ClusterRecord rec;
    rec.clusterId = cid;
    rec.rootIdx = rootIdx;
    std::unordered_set<std::size_t> memberSet(members.begin(), members.end());

    for (std::size_t i : members) {
      ClusterMemberInfo info;
      info.nodeIdx = i;
      if (i == rootIdx) {
        info.category = MemberCategory::Root;
      } else {
        info.category = classifyMember(i, adj[i], rootIdx, memberSet, rootToCluster);
      }
      rec.members.push_back(info);
    }
    result.clusters.push_back(std::move(rec));
  }

  // 6. Connector records (exactly 2 roots).
  for (std::size_t i : connectorSet) {
    ConnectorRecord rec;
    rec.nodeIdx = i;
    auto it = reach.find(i);
    if (it != reach.end()) {
      rec.connectedClusterIds.assign(it->second.begin(), it->second.end());
    }
    result.connectors.push_back(std::move(rec));
  }

  // 7. Router records (3+ roots) and constellation construction.
  // Each cluster joins the constellation of its strongest router (the one
  // with the most distinct cluster-root connections including this cluster).
  // Tie-break: lowest-index router for determinism.
  for (std::size_t i : routerSet) {
    RouterRecord rec;
    rec.nodeIdx = i;
    auto it = reach.find(i);
    if (it != reach.end()) {
      rec.connectedClusterIds.assign(it->second.begin(), it->second.end());
    }
    rec.constellationId = "_const_" + std::to_string(i);
    result.routers.push_back(std::move(rec));
  }
  // Build cluster → router(s) reverse map.
  std::unordered_map<std::string, std::vector<std::size_t>> clusterToRouters;
  for (const RouterRecord& r : result.routers) {
    for (const std::string& cid : r.connectedClusterIds) {
      clusterToRouters[cid].push_back(r.nodeIdx);
    }
  }
  // Assign each cluster to one constellation (router with most reach; tie →
  // lowest index).
  std::unordered_map<std::string, std::size_t> clusterToConstellationRouter;
  for (auto& [cid, routerCandidates] : clusterToRouters) {
    std::sort(routerCandidates.begin(), routerCandidates.end(),
      [&](std::size_t a, std::size_t b) {
        const std::size_t ra = reach[a].size();
        const std::size_t rb = reach[b].size();
        if (ra != rb) return ra > rb;
        return a < b;
      });
    if (!routerCandidates.empty()) {
      clusterToConstellationRouter[cid] = routerCandidates[0];
    }
  }
  // Populate ConstellationRecords with member cluster ids.
  std::unordered_map<std::size_t, ConstellationRecord*> routerIdxToConst;
  for (RouterRecord& r : result.routers) {
    ConstellationRecord c;
    c.constellationId = r.constellationId;
    c.routerIdx = r.nodeIdx;
    result.constellations.push_back(std::move(c));
  }
  for (ConstellationRecord& c : result.constellations) {
    routerIdxToConst[c.routerIdx] = &c;
  }
  for (const auto& [cid, routerIdx] : clusterToConstellationRouter) {
    auto it = routerIdxToConst.find(routerIdx);
    if (it == routerIdxToConst.end()) continue;
    it->second->clusterIds.push_back(cid);
  }
  // Stamp constellationId onto member ClusterRecords for downstream use.
  std::unordered_map<std::string, std::string> clusterToConstId;
  for (const ConstellationRecord& c : result.constellations) {
    for (const std::string& cid : c.clusterIds) {
      clusterToConstId[cid] = c.constellationId;
    }
  }
  for (ClusterRecord& cluster : result.clusters) {
    auto it = clusterToConstId.find(cluster.clusterId);
    if (it != clusterToConstId.end()) cluster.constellationId = it->second;
  }

  // 8. Independent nodes (not in any cluster, not connector, not router).
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    if (connectorSet.count(i) || routerSet.count(i)) continue;
    if (nodeCluster.count(i)) continue;
    result.independentNodeIndices.push_back(i);
  }

  // 8c. Filter alone-roots (deg-0 isolated nodes) OUT of the super-graph
  // pipeline. With FMMM `arrangeCCs` they each become their own component
  // and the layout bbox blows up. Instead we place them in a tight grid
  // post-layout outside the main bbox (graph-terminology.md §2.5.1).
  std::unordered_set<std::size_t> aloneRootSet;
  for (const PrunedNodeRecord& p : result.prunedNodes) {
    if (p.isAloneRoot) aloneRootSet.insert(p.nodeIdx);
  }
  std::vector<std::size_t> aloneRootIndices;
  {
    std::vector<std::size_t> filtered;
    for (std::size_t idx : result.independentNodeIndices) {
      if (aloneRootSet.count(idx)) aloneRootIndices.push_back(idx);
      else filtered.push_back(idx);
    }
    result.independentNodeIndices = std::move(filtered);
  }

  // 8b. Polar detection (graph-terminology.md §3.5):
  // A cluster root is a Polar if it is directly adjacent to 3+ DISTINCT
  // OTHER cluster roots. Polars form the topmost structural skeleton —
  // their relative positions get cross-min priority before other layout.
  // Iterate filtered rootToCluster so singleton roots are excluded both as
  // candidate polars and as counted-other-roots.
  for (const auto& [rootIdx, cid] : rootToCluster) {
    std::set<std::string> otherRoots;
    for (std::size_t j : adj[rootIdx]) {
      auto rIt = rootToCluster.find(j);
      if (rIt == rootToCluster.end()) continue;
      if (rIt->second == cid) continue;
      otherRoots.insert(rIt->second);
    }
    if (otherRoots.size() >= 3) {
      PolarRecord pr;
      pr.nodeIdx = rootIdx;
      pr.clusterId = cid;
      pr.connectedClusterIds.assign(otherRoots.begin(), otherRoots.end());
      result.polars.push_back(std::move(pr));
    }
  }
  // Sort polars by nodeIdx for determinism.
  std::sort(result.polars.begin(), result.polars.end(),
    [](const PolarRecord& a, const PolarRecord& b) {
      return a.nodeIdx < b.nodeIdx;
    });

  // 7. Estimate cluster bbox so super-graph layout has reasonable sizes
  // BEFORE inner radial runs. The actual inner radial pass below uses the
  // outward direction from the super-graph layout.
  for (ClusterRecord& cluster : result.clusters) {
    const double d = estimateClusterDiameter(cluster, nodes);
    cluster.localSize = {d, d};
  }

  // 7a. Pre-layout leaf-bundle aggregation (graph-terminology.md §12).
  // Currently, the leafBundle matrix placement happens AFTER super-graph
  // layout (at inner-radial pass). Super-graph FMMM uses the diameter
  // estimate above which doesn't know about matrix bbox — when matrix is
  // large, super-graph FMMM under-reserves space and the matrix collides
  // with neighbours after inner placement. Detect bundle parents now,
  // pre-compute the matrix bbox, and inflate cluster localSize so super-
  // graph FMMM gives the cluster enough room for both its bubble and its
  // bundle matrix.
  {
    constexpr std::size_t kMatrixThresholdPre = 5;
    // Build parent → level-1 leaf children map from pruned records.
    std::unordered_map<std::size_t, std::vector<std::size_t>> parentToLeaves;
    for (const PrunedNodeRecord& p : result.prunedNodes) {
      if (p.isAloneRoot) continue;
      parentToLeaves[p.parentIdx].push_back(p.nodeIdx);
    }
    for (ClusterRecord& cluster : result.clusters) {
      // Check cluster root and members as potential bundle parents.
      double bundleAlong = 0.0;
      double bundlePerp = 0.0;
      auto considerParent = [&](std::size_t pIdx) {
        auto it = parentToLeaves.find(pIdx);
        if (it == parentToLeaves.end()) return;
        const std::size_t M = it->second.size();
        if (M < kMatrixThresholdPre) return;
        double maxChildW = 0.0, maxChildH = 0.0;
        for (std::size_t c : it->second) {
          maxChildW = std::max(maxChildW, nodes[c].width);
          maxChildH = std::max(maxChildH, nodes[c].height);
        }
        const double cellW = maxChildW + 4.0;
        const double cellH = maxChildH + 4.0;
        const std::size_t cols = std::max<std::size_t>(1,
          static_cast<std::size_t>(std::ceil(
            std::sqrt(static_cast<double>(M)
                       * std::max(1.0, cellH / std::max(1.0, cellW))))));
        const std::size_t rows = (M + cols - 1) / cols;
        const double matrixW = static_cast<double>(cols) * cellW;
        const double matrixH = static_cast<double>(rows) * cellH;
        const double pw = nodes[pIdx].width;
        const double ph = nodes[pIdx].height;
        const double pHalf = std::max(pw, ph) / 2.0;
        // Matrix sits offset from parent in centerAngle direction:
        //   parent_center → matrix_near_edge: pHalf + 16
        //   matrix_near_edge → matrix_far_edge: matrixH
        // Total extent from cluster center (parent ≈ cluster center) along
        // bundle axis: 2*pHalf + matrixH + 16. Perpendicular: max(pw, matrixW).
        const double along = 2.0 * pHalf + matrixH + 16.0;
        const double perp = std::max(pw, matrixW);
        if (along > bundleAlong) bundleAlong = along;
        if (perp > bundlePerp) bundlePerp = perp;
      };
      considerParent(cluster.rootIdx);
      for (const ClusterMemberInfo& m : cluster.members) {
        if (m.nodeIdx != cluster.rootIdx) considerParent(m.nodeIdx);
      }
      if (bundleAlong > 0.0 || bundlePerp > 0.0) {
        // Use max(along, perp): fits the cluster's axis-aligned bound.
        // Diagonal would over-pad (15-30%); square-with-max gives just
        // enough room for the matrix to sit alongside the parent.
        const double dim = std::max(bundleAlong, bundlePerp);
        if (dim > cluster.localSize.first) {
          cluster.localSize.first = dim;
          cluster.localSize.second = dim;
        }
      }
    }
  }

  // 7b. Polar skeleton layout (graph-terminology.md §3.6).
  // Polars are the most structurally important nodes — they need cross-min
  // priority on the polar↔polar edges before anything else. We build a
  // polar-only sub-graph (only polar nodes + dedup edges between polars),
  // run a strong cross-min layout on this small graph, classify each
  // connected component as ring/line/complex/single, and store the
  // resulting positions as anchors for the main super-graph layout.
  std::unordered_map<std::size_t, std::pair<double, double>> polarAnchors;
  if (result.polars.size() >= 2) {
    // Polar adjacency map (polar node idx → polar neighbours).
    std::unordered_set<std::size_t> polarSet;
    for (const PolarRecord& p : result.polars) polarSet.insert(p.nodeIdx);
    std::unordered_map<std::size_t, std::vector<std::size_t>> polarAdj;
    for (std::size_t pIdx : polarSet) {
      std::vector<std::size_t>& nbrs = polarAdj[pIdx];
      for (std::size_t j : adj[pIdx]) {
        if (polarSet.count(j)) nbrs.push_back(j);
      }
      std::sort(nbrs.begin(), nbrs.end());
    }

    // Count edges in skeleton (sum of degrees / 2).
    std::size_t skeletonEdges = 0;
    for (const auto& [_, nbrs] : polarAdj) skeletonEdges += nbrs.size();
    skeletonEdges /= 2;
    result.polarSkeletonEdgeCount = skeletonEdges;

    // Classify connected components as ring / line / complex / single.
    std::unordered_set<std::size_t> visited;
    for (std::size_t start : polarSet) {
      if (visited.count(start)) continue;
      std::vector<std::size_t> compNodes;
      std::vector<std::size_t> stack{start};
      visited.insert(start);
      while (!stack.empty()) {
        std::size_t v = stack.back();
        stack.pop_back();
        compNodes.push_back(v);
        for (std::size_t u : polarAdj[v]) {
          if (!visited.count(u)) {
            visited.insert(u);
            stack.push_back(u);
          }
        }
      }
      const std::size_t N = compNodes.size();
      if (N <= 1) continue;
      std::size_t E = 0;
      std::size_t deg1 = 0, deg2 = 0;
      bool nonStandardDeg = false;
      for (std::size_t v : compNodes) {
        const std::size_t d = polarAdj[v].size();
        E += d;
        if (d == 1) ++deg1;
        else if (d == 2) ++deg2;
        else nonStandardDeg = true;
      }
      E /= 2;
      if (!nonStandardDeg && E == N && deg2 == N) {
        ++result.polarRingCount;
      } else if (!nonStandardDeg && E == N - 1 && deg1 == 2 && deg2 == N - 2) {
        ++result.polarLineCount;
      }
    }

    // ---- BOTTOM-UP SIZING (graph-terminology.md §3.6) ----
    // Each polar's slot diameter is governed by its OWN cluster bbox plus
    // padding. Polar slot → super-polar bbox → super-polar separation.
    // Padding accommodates non-polar clusters / connectors that route between
    // polars within and between super-polars.
    constexpr double kPolarSlotPad = 40.0;      // ring spacing between adj polars
    constexpr double kSuperPolarPad = 120.0;    // padding around super-polar bbox
    std::unordered_map<std::size_t, double> polarSlotDiam;
    for (const ClusterRecord& c : result.clusters) {
      polarSlotDiam[c.rootIdx] = c.localSize.first + kSuperNodePadding;
    }
    auto polarSlotOf = [&](std::size_t pIdx) -> double {
      auto it = polarSlotDiam.find(pIdx);
      return it != polarSlotDiam.end() ? it->second : 280.0;
    };

    // Branch on polar count:
    //   - ≥ 4 polars: super-polar tier (group polars, layout meta-graph
    //     kink-free, arrange polars around super-polar centres).
    //   - 2-3 polars: direct skeleton layout (Sugiyama, no super tier).
    if (result.polars.size() >= 4) {
      // Super-polar tier (graph-terminology.md §3.6 extension).
      // K = round(sqrt(N)). Pick top-K skeleton-degree polars as seeds, then
      // BFS-assign the rest to nearest seed by hop distance in the polar
      // skeleton. Polars not reached (skeleton-isolated) become own seeds.
      const std::size_t N = result.polars.size();
      const std::size_t K = std::max<std::size_t>(2,
        static_cast<std::size_t>(
          std::round(std::sqrt(static_cast<double>(N)))));

      std::vector<std::size_t> polarsByDeg;
      polarsByDeg.reserve(N);
      for (const PolarRecord& p : result.polars) polarsByDeg.push_back(p.nodeIdx);
      std::sort(polarsByDeg.begin(), polarsByDeg.end(),
        [&](std::size_t a, std::size_t b) {
          const std::size_t da = polarAdj[a].size();
          const std::size_t db = polarAdj[b].size();
          if (da != db) return da > db;
          return a < b;
        });
      std::unordered_set<std::size_t> seedSet;
      for (std::size_t i = 0; i < std::min(K, polarsByDeg.size()); ++i) {
        seedSet.insert(polarsByDeg[i]);
      }
      std::unordered_map<std::size_t, std::size_t> polarToSeed;
      std::queue<std::pair<std::size_t, std::size_t>> bfsq;
      for (std::size_t s : seedSet) {
        polarToSeed[s] = s;
        bfsq.emplace(s, s);
      }
      while (!bfsq.empty()) {
        auto front = bfsq.front();
        bfsq.pop();
        const std::size_t v = front.first;
        const std::size_t seed = front.second;
        for (std::size_t u : polarAdj[v]) {
          if (!polarToSeed.count(u)) {
            polarToSeed[u] = seed;
            bfsq.emplace(u, seed);
          }
        }
      }
      // Skeleton-isolated polars: own super-polar.
      for (std::size_t p : polarSet) {
        if (!polarToSeed.count(p)) {
          polarToSeed[p] = p;
          seedSet.insert(p);
        }
      }
      // Build SuperPolarRecords.
      std::unordered_map<std::size_t, std::size_t> seedToSuperIdx;
      for (std::size_t s : seedSet) {
        SuperPolarRecord sp;
        sp.centerNodeIdx = s;
        seedToSuperIdx[s] = result.superPolars.size();
        result.superPolars.push_back(std::move(sp));
      }
      for (auto& kv : polarToSeed) {
        const std::size_t spIdx = seedToSuperIdx[kv.second];
        result.superPolars[spIdx].memberPolarIdxs.push_back(kv.first);
      }
      for (SuperPolarRecord& sp : result.superPolars) {
        std::sort(sp.memberPolarIdxs.begin(), sp.memberPolarIdxs.end());
      }
      // Stamp super-polar idx onto polars.
      for (PolarRecord& p : result.polars) {
        p.superPolarIdx = seedToSuperIdx[polarToSeed[p.nodeIdx]];
      }

      // ---- BOTTOM-UP super-polar bbox (sized from member polar slots) ----
      const double pi = 3.14159265358979;
      for (SuperPolarRecord& sp : result.superPolars) {
        double maxPolarSlot = 0.0;
        for (std::size_t pIdx : sp.memberPolarIdxs) {
          maxPolarSlot = std::max(maxPolarSlot, polarSlotOf(pIdx));
        }
        const std::size_t M = sp.memberPolarIdxs.size();
        if (M <= 1) {
          sp.polarRingRadius = 0.0;
          sp.bboxDiam = maxPolarSlot;
        } else {
          // Polars sit on a ring whose chord (= polar↔polar distance) is
          // at least the largest polar slot + padding; circumference fits all.
          const double minChord = maxPolarSlot + kPolarSlotPad;
          const double sinHalf = std::sin(pi / static_cast<double>(M));
          const double r = std::max(maxPolarSlot,
                                    minChord / (2.0 * std::max(1e-3, sinHalf)));
          sp.polarRingRadius = r;
          sp.bboxDiam = 2.0 * r + maxPolarSlot;
        }
      }

      // Super-polar meta-graph: edge between SP_a and SP_b iff any polar in
      // SP_a is adjacent (in skeleton) to any polar in SP_b.
      std::set<std::pair<std::size_t, std::size_t>> spEdges;
      std::unordered_map<std::size_t, std::vector<std::size_t>> spMetaAdj;
      for (std::size_t i = 0; i < result.superPolars.size(); ++i) spMetaAdj[i] = {};
      for (const auto& kv : polarAdj) {
        const std::size_t spA = seedToSuperIdx[polarToSeed[kv.first]];
        for (std::size_t u : kv.second) {
          const std::size_t spB = seedToSuperIdx[polarToSeed[u]];
          if (spA == spB) continue;
          auto key = spA < spB ? std::make_pair(spA, spB)
                                : std::make_pair(spB, spA);
          if (spEdges.insert(key).second) {
            spMetaAdj[spA].push_back(spB);
            spMetaAdj[spB].push_back(spA);
          }
        }
      }
      result.superPolarMetaEdgeCount = spEdges.size();

      // ---- TOPOLOGY DETECTION (graph-terminology.md §3.6) ----
      // Find the largest connected component in the meta-graph. Classify it:
      //   "ring"   — single cycle (all deg=2, |E|=|V|)
      //   "path"   — line (2 endpoints with deg=1, rest deg=2, |E|=|V|-1)
      //   "complex"— anything else with cycles
      //   "trivial"— single node / disconnected isolates only
      std::vector<std::vector<std::size_t>> metaComponents;
      {
        std::unordered_set<std::size_t> visitedMeta;
        for (std::size_t s = 0; s < result.superPolars.size(); ++s) {
          if (visitedMeta.count(s)) continue;
          std::vector<std::size_t> comp;
          std::queue<std::size_t> q;
          q.push(s);
          visitedMeta.insert(s);
          while (!q.empty()) {
            std::size_t v = q.front(); q.pop();
            comp.push_back(v);
            for (std::size_t u : spMetaAdj[v]) {
              if (!visitedMeta.count(u)) {
                visitedMeta.insert(u);
                q.push(u);
              }
            }
          }
          metaComponents.push_back(std::move(comp));
        }
      }
      // Largest component
      std::size_t largestIdx = 0;
      for (std::size_t i = 1; i < metaComponents.size(); ++i) {
        if (metaComponents[i].size() > metaComponents[largestIdx].size()) largestIdx = i;
      }
      // Classify largest
      std::string topoKind = "trivial";
      std::vector<std::size_t> ringOrPathOrder;
      if (!metaComponents.empty()) {
        const auto& largest = metaComponents[largestIdx];
        if (largest.size() == 1) {
          topoKind = "trivial";
        } else {
          std::size_t deg1 = 0, deg2 = 0;
          bool nonStd = false;
          std::size_t edgeCount = 0;
          for (std::size_t v : largest) {
            const std::size_t d = spMetaAdj[v].size();
            edgeCount += d;
            if (d == 1) ++deg1;
            else if (d == 2) ++deg2;
            else nonStd = true;
          }
          edgeCount /= 2;
          if (!nonStd && edgeCount == largest.size() && deg2 == largest.size()) {
            topoKind = "ring";
            // Walk the ring
            std::size_t cur = largest[0];
            std::size_t prev = std::numeric_limits<std::size_t>::max();
            ringOrPathOrder.reserve(largest.size());
            while (ringOrPathOrder.size() < largest.size()) {
              ringOrPathOrder.push_back(cur);
              std::size_t next = std::numeric_limits<std::size_t>::max();
              for (std::size_t u : spMetaAdj[cur]) {
                if (u != prev) { next = u; break; }
              }
              if (next == std::numeric_limits<std::size_t>::max()) break;
              prev = cur;
              cur = next;
            }
          } else if (!nonStd && edgeCount == largest.size() - 1
                     && deg1 == 2 && deg2 == largest.size() - 2) {
            topoKind = "path";
            // Walk path from a deg-1 endpoint
            std::size_t start = largest[0];
            for (std::size_t v : largest) if (spMetaAdj[v].size() == 1) { start = v; break; }
            std::size_t cur = start;
            std::size_t prev = std::numeric_limits<std::size_t>::max();
            ringOrPathOrder.reserve(largest.size());
            while (ringOrPathOrder.size() < largest.size()) {
              ringOrPathOrder.push_back(cur);
              std::size_t next = std::numeric_limits<std::size_t>::max();
              for (std::size_t u : spMetaAdj[cur]) {
                if (u != prev) { next = u; break; }
              }
              if (next == std::numeric_limits<std::size_t>::max()) break;
              prev = cur;
              cur = next;
            }
          } else {
            topoKind = "complex";
          }
        }
      }
      result.superPolarTopology = topoKind;

      // Super-polar separation = max super-polar bbox + padding (so adjacent
      // super-polars don't overlap).
      double maxSpBbox = 0.0;
      for (const SuperPolarRecord& sp : result.superPolars) {
        maxSpBbox = std::max(maxSpBbox, sp.bboxDiam);
      }
      const double spSeparation = maxSpBbox + kSuperPolarPad;

      // ---- TOPOLOGY-AWARE SUPER-POLAR LAYOUT ----
      // ring   → place ring members on a regular polygon in cycle order
      // path   → place path members on a horizontal line
      // else   → Sugiyama with nodeDistance from super-polar bbox
      // Other (smaller) components → off to the side, simple grid.
      bool topologyHandled = false;
      if (topoKind == "ring" && ringOrPathOrder.size() == metaComponents[largestIdx].size()) {
        const std::size_t Nr = ringOrPathOrder.size();
        const double sinHalf = std::sin(pi / static_cast<double>(Nr));
        const double R = std::max(spSeparation,
                                  spSeparation / (2.0 * std::max(1e-3, sinHalf)));
        for (std::size_t k = 0; k < Nr; ++k) {
          const double theta = 2.0 * pi * static_cast<double>(k)
                             / static_cast<double>(Nr);
          const std::size_t spIdx = ringOrPathOrder[k];
          result.superPolars[spIdx].centerX = R * std::cos(theta);
          result.superPolars[spIdx].centerY = R * std::sin(theta);
        }
        topologyHandled = true;
      } else if (topoKind == "path"
                 && ringOrPathOrder.size() == metaComponents[largestIdx].size()) {
        for (std::size_t k = 0; k < ringOrPathOrder.size(); ++k) {
          const std::size_t spIdx = ringOrPathOrder[k];
          result.superPolars[spIdx].centerX = spSeparation * static_cast<double>(k);
          result.superPolars[spIdx].centerY = 0.0;
        }
        topologyHandled = true;
      }

      if (!topologyHandled) {
        // Complex / mixed largest: Sugiyama on the meta-graph. nodeDistance
        // is governed by the max super-polar bbox so adjacent super-polars
        // don't overlap.
        ogdf::Graph spGraph;
        ogdf::GraphAttributes spAttr(spGraph,
          ogdf::GraphAttributes::nodeGraphics | ogdf::GraphAttributes::edgeGraphics);
        std::vector<ogdf::node> spNodes(result.superPolars.size());
        for (std::size_t i = 0; i < result.superPolars.size(); ++i) {
          spNodes[i] = spGraph.newNode();
          spAttr.width(spNodes[i]) = result.superPolars[i].bboxDiam;
          spAttr.height(spNodes[i]) = result.superPolars[i].bboxDiam;
        }
        for (auto& e : spEdges) spGraph.newEdge(spNodes[e.first], spNodes[e.second]);
        if (spGraph.numberOfNodes() >= 2) {
          try {
            ogdf::SugiyamaLayout sugi;
            sugi.setRanking(new ogdf::OptimalRanking());
            sugi.setCrossMin(new ogdf::BarycenterHeuristic());
            sugi.runs(16);
            sugi.fails(16);
            sugi.transpose(true);
            auto* hier = new ogdf::OptimalHierarchyLayout();
            hier->layerDistance(spSeparation);
            hier->nodeDistance(spSeparation);
            hier->weightBalancing(0.7);
            sugi.setLayout(hier);
            sugi.arrangeCCs(true);
            sugi.call(spAttr);
          } catch (...) {
            ogdf::FMMMLayout fmmm;
            applyFmmmEnvSeed(fmmm);
            fmmm.useHighLevelOptions(true);
            fmmm.unitEdgeLength(spSeparation);
            fmmm.newInitialPlacement(true);
            fmmm.qualityVersusSpeed(
              ogdf::FMMMOptions::QualityVsSpeed::GorgeousAndEfficient);
            fmmm.call(spAttr);
          }
        }
        for (std::size_t i = 0; i < result.superPolars.size(); ++i) {
          result.superPolars[i].centerX = spAttr.x(spNodes[i]);
          result.superPolars[i].centerY = spAttr.y(spNodes[i]);
        }
      }

      // ---- INTRA-SUPER-POLAR: place polars on bottom-up-sized ring ----
      for (SuperPolarRecord& sp : result.superPolars) {
        const std::size_t M = sp.memberPolarIdxs.size();
        if (M == 1) {
          polarAnchors[sp.memberPolarIdxs[0]] = {sp.centerX, sp.centerY};
          continue;
        }
        for (std::size_t k = 0; k < M; ++k) {
          const double theta = 2.0 * pi * static_cast<double>(k)
                             / static_cast<double>(M);
          const double x = sp.centerX + sp.polarRingRadius * std::cos(theta);
          const double y = sp.centerY + sp.polarRingRadius * std::sin(theta);
          polarAnchors[sp.memberPolarIdxs[k]] = {x, y};
        }
      }
    } else if (skeletonEdges >= 1) {
      // 2-3 polars: direct line layout (skeleton has at most 1-2 edges).
      // Slot-driven separation = max polar slot + padding.
      double maxSlot = 0.0;
      for (const PolarRecord& p : result.polars) {
        maxSlot = std::max(maxSlot, polarSlotOf(p.nodeIdx));
      }
      const double sep = maxSlot + kSuperPolarPad;
      // Place polars on a horizontal line (deterministic order by nodeIdx).
      std::vector<std::size_t> ordered;
      for (const PolarRecord& p : result.polars) ordered.push_back(p.nodeIdx);
      std::sort(ordered.begin(), ordered.end());
      for (std::size_t i = 0; i < ordered.size(); ++i) {
        polarAnchors[ordered[i]] = {sep * static_cast<double>(i), 0.0};
      }
      result.superPolarTopology = ordered.size() == 2 ? "path" : "trivial";
    }

    // Final: copy anchors back to PolarRecord for telemetry.
    for (PolarRecord& p : result.polars) {
      auto it = polarAnchors.find(p.nodeIdx);
      if (it != polarAnchors.end()) {
        p.anchorX = it->second.first;
        p.anchorY = it->second.second;
      }
    }
  }

  cgCheckpoint("post-§7b-polar-skeleton");
  // 8. Super-graph: cluster super-nodes + connectors + independents.
  // Build OGDF super-graph for layout.
  ogdf::Graph super;
  ogdf::GraphAttributes superAttr(super,
    ogdf::GraphAttributes::nodeGraphics | ogdf::GraphAttributes::edgeGraphics);

  std::unordered_map<std::string, ogdf::node> clusterToSuper;
  std::unordered_map<std::size_t, ogdf::node> connectorToSuper;
  std::unordered_map<std::size_t, ogdf::node> routerToSuper;
  std::unordered_map<std::size_t, ogdf::node> indepToSuper;

  // Cluster super-nodes (size = local diameter + padding).
  for (const ClusterRecord& c : result.clusters) {
    ogdf::node sn = super.newNode();
    superAttr.width(sn) = c.localSize.first + kSuperNodePadding;
    superAttr.height(sn) = c.localSize.second + kSuperNodePadding;
    clusterToSuper[c.clusterId] = sn;
  }
  for (const ConnectorRecord& con : result.connectors) {
    ogdf::node sn = super.newNode();
    const NodeRecord& nd = nodes[con.nodeIdx];
    superAttr.width(sn) = std::max(60.0, nd.width);
    superAttr.height(sn) = std::max(40.0, nd.height);
    connectorToSuper[con.nodeIdx] = sn;
  }
  for (const RouterRecord& rtr : result.routers) {
    ogdf::node sn = super.newNode();
    const NodeRecord& nd = nodes[rtr.nodeIdx];
    superAttr.width(sn) = std::max(80.0, nd.width * 1.2);
    superAttr.height(sn) = std::max(56.0, nd.height * 1.2);
    routerToSuper[rtr.nodeIdx] = sn;
  }
  // Independent nodes.
  for (std::size_t idx : result.independentNodeIndices) {
    ogdf::node sn = super.newNode();
    const NodeRecord& nd = nodes[idx];
    superAttr.width(sn) = std::max(60.0, nd.width);
    superAttr.height(sn) = std::max(40.0, nd.height);
    indepToSuper[idx] = sn;
  }

  std::set<std::pair<ogdf::node, ogdf::node>> seenEdges;
  auto addEdge = [&](ogdf::node a, ogdf::node b) {
    if (a == nullptr || b == nullptr || a == b) return;
    auto key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
    if (seenEdges.insert(key).second) {
      super.newEdge(a, b);
    }
  };

  // root↔root edges
  for (const auto& [cidA, rootA] : rootByCluster) {
    for (std::size_t j : adj[rootA]) {
      auto it = rootToCluster.find(j);
      if (it == rootToCluster.end()) continue;
      const std::string& cidB = it->second;
      if (cidA >= cidB) continue;
      auto sa = clusterToSuper.find(cidA);
      auto sb = clusterToSuper.find(cidB);
      if (sa == clusterToSuper.end() || sb == clusterToSuper.end()) continue;
      addEdge(sa->second, sb->second);
    }
  }
  // connector↔root edges
  for (const ConnectorRecord& con : result.connectors) {
    auto cnIt = connectorToSuper.find(con.nodeIdx);
    if (cnIt == connectorToSuper.end()) continue;
    for (const std::string& cid : con.connectedClusterIds) {
      auto sa = clusterToSuper.find(cid);
      if (sa == clusterToSuper.end()) continue;
      addEdge(cnIt->second, sa->second);
    }
  }
  // router↔root edges — these are the constellation backbone. Multiple of
  // them per router pull the constellation members together at top level.
  for (const RouterRecord& rtr : result.routers) {
    auto rsn = routerToSuper.find(rtr.nodeIdx);
    if (rsn == routerToSuper.end()) continue;
    for (const std::string& cid : rtr.connectedClusterIds) {
      auto sa = clusterToSuper.find(cid);
      if (sa == clusterToSuper.end()) continue;
      addEdge(rsn->second, sa->second);
    }
  }
  // independent↔* edges. Independents include nodes that have empty cluster
  // labels OR are demoted singleton-cluster roots (= leaves attached to a
  // connector/router). Either way we need to wire each independent to ALL
  // its adjacent super-nodes (root, connector, router, or other indep) so
  // FMMM keeps it close to its actual neighbour rather than floating.
  for (std::size_t idx : result.independentNodeIndices) {
    auto isn = indepToSuper.find(idx);
    if (isn == indepToSuper.end()) continue;
    for (std::size_t j : adj[idx]) {
      // root?
      auto rIt = rootToCluster.find(j);
      if (rIt != rootToCluster.end()) {
        auto sa = clusterToSuper.find(rIt->second);
        if (sa != clusterToSuper.end()) addEdge(isn->second, sa->second);
        continue;
      }
      // connector?
      auto cIt = connectorToSuper.find(j);
      if (cIt != connectorToSuper.end()) {
        addEdge(isn->second, cIt->second);
        continue;
      }
      // router?
      auto rtIt = routerToSuper.find(j);
      if (rtIt != routerToSuper.end()) {
        addEdge(isn->second, rtIt->second);
        continue;
      }
      // other independent?
      auto oIt = indepToSuper.find(j);
      if (oIt != indepToSuper.end() && oIt->second != isn->second) {
        addEdge(isn->second, oIt->second);
      }
    }
  }

  // chain-based root↔root super-edges (graph-terminology.md §1.1.1).
  // Any path A → ... → B through non-root, non-router intermediaries
  // (connectors AND cluster members) topologically equals a direct A-B
  // edge. BFS from each root; on encountering another root, dedup-add a
  // super-edge between their cluster super-nodes. Routers are NOT
  // expanded through (they're separate super-nodes).
  std::size_t chainEdgesAdded = 0;
  {
    const std::size_t edgesBefore = seenEdges.size();
    for (const auto& kv : rootToCluster) {
      const std::size_t rootA = kv.first;
      const std::string& cidA = kv.second;
      auto saIt = clusterToSuper.find(cidA);
      if (saIt == clusterToSuper.end()) continue;
      ogdf::node sA = saIt->second;

      std::unordered_set<std::size_t> visited;
      visited.insert(rootA);
      std::queue<std::size_t> q;
      for (std::size_t j : adj[rootA]) {
        if (rootToCluster.count(j)) continue;          // direct: handled above
        if (routerSet.count(j)) continue;              // routers: own super-node
        if (visited.insert(j).second) q.push(j);
      }
      while (!q.empty()) {
        const std::size_t cur = q.front();
        q.pop();
        for (std::size_t nb : adj[cur]) {
          if (visited.count(nb)) continue;
          auto rIt = rootToCluster.find(nb);
          if (rIt != rootToCluster.end()) {
            const std::string& cidB = rIt->second;
            if (cidA < cidB) {
              auto sbIt = clusterToSuper.find(cidB);
              if (sbIt != clusterToSuper.end()) {
                addEdge(sA, sbIt->second);
              }
            }
            visited.insert(nb);
            continue;  // don't BFS through this root
          }
          if (routerSet.count(nb)) continue;
          visited.insert(nb);
          q.push(nb);
        }
      }
    }
    chainEdgesAdded = seenEdges.size() - edgesBefore;
  }

  result.topLevelEdgeCount = seenEdges.size();

  // 8b. Skeleton probe (DJERD_SKELETON_PROBE=1): instrumentation only.
  // Builds a stripped-down "cluster-only" top-level graph (one node per
  // cluster, one edge per distinct inter-cluster connection) and reports
  // crossings under several candidate layouts. Output goes to stderr;
  // the actual super-graph layout below is unaffected.
  {
    const char* skelEnv = std::getenv("DJERD_SKELETON_PROBE");
    if (skelEnv && std::strcmp(skelEnv, "0") != 0) {
      ogdf::Graph skel;
      ogdf::GraphAttributes skelAttr(skel,
        ogdf::GraphAttributes::nodeGraphics | ogdf::GraphAttributes::edgeGraphics);
      std::unordered_map<std::string, ogdf::node> cidToSkel;
      for (const ClusterRecord& c : result.clusters) {
        ogdf::node n = skel.newNode();
        skelAttr.width(n) = 80.0;
        skelAttr.height(n) = 80.0;
        cidToSkel[c.clusterId] = n;
      }
      std::set<std::pair<std::string, std::string>> skelEdgePairs;
      for (std::size_t i = 0; i < adj.size(); ++i) {
        auto cIt = nodeCluster.find(i);
        if (cIt == nodeCluster.end()) continue;
        for (std::size_t j : adj[i]) {
          if (j <= i) continue;
          auto cJt = nodeCluster.find(j);
          if (cJt == nodeCluster.end()) continue;
          if (cIt->second == cJt->second) continue;
          auto p = (cIt->second < cJt->second)
            ? std::make_pair(cIt->second, cJt->second)
            : std::make_pair(cJt->second, cIt->second);
          skelEdgePairs.insert(p);
        }
      }
      for (const auto& p : skelEdgePairs) {
        auto sIt = cidToSkel.find(p.first);
        auto tIt = cidToSkel.find(p.second);
        if (sIt == cidToSkel.end() || tIt == cidToSkel.end()) continue;
        skel.newEdge(sIt->second, tIt->second);
      }

      auto countSkelCross = [&]() -> std::size_t {
        std::vector<std::pair<ogdf::node, ogdf::node>> es;
        es.reserve(skel.numberOfEdges());
        for (ogdf::edge e : skel.edges) es.emplace_back(e->source(), e->target());
        auto sgn = [](double x) { return (x > 0) - (x < 0); };
        std::size_t total = 0;
        for (std::size_t i = 0; i < es.size(); ++i) {
          for (std::size_t j = i + 1; j < es.size(); ++j) {
            const auto& a = es[i];
            const auto& b = es[j];
            if (a.first == b.first || a.first == b.second
                || a.second == b.first || a.second == b.second) continue;
            const double ax = skelAttr.x(a.first), ay = skelAttr.y(a.first);
            const double bx = skelAttr.x(a.second), by = skelAttr.y(a.second);
            const double cx = skelAttr.x(b.first), cy = skelAttr.y(b.first);
            const double dx = skelAttr.x(b.second), dy = skelAttr.y(b.second);
            const int o1 = sgn((bx - ax) * (cy - ay) - (by - ay) * (cx - ax));
            const int o2 = sgn((bx - ax) * (dy - ay) - (by - ay) * (dx - ax));
            const int o3 = sgn((dx - cx) * (ay - cy) - (dy - cy) * (ax - cx));
            const int o4 = sgn((dx - cx) * (by - cy) - (dy - cy) * (bx - cx));
            if ((o1 != o2) && (o3 != o4) && (o1 != 0) && (o3 != 0)) ++total;
          }
        }
        return total;
      };

      auto tryLayout = [&](const char* label, std::function<void()> doLayout) {
        const auto t0 = std::chrono::steady_clock::now();
        try {
          doLayout();
          const auto t1 = std::chrono::steady_clock::now();
          const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
          const std::size_t c = countSkelCross();
          std::fprintf(stderr,
            "[skel-probe] %s: %d nodes, %d edges, %zu crossings (%lldms).\n",
            label, skel.numberOfNodes(), skel.numberOfEdges(), c,
            static_cast<long long>(ms));
        } catch (const std::exception& ex) {
          std::fprintf(stderr, "[skel-probe] %s: failed (%s)\n", label, ex.what());
        }
      };

      tryLayout("FMMM", [&]() {
        ogdf::FMMMLayout fmmm;
        applyFmmmEnvSeed(fmmm);
        fmmm.useHighLevelOptions(true);
        fmmm.unitEdgeLength(120.0);
        fmmm.qualityVersusSpeed(ogdf::FMMMOptions::QualityVsSpeed::GorgeousAndEfficient);
        fmmm.call(skelAttr);
      });

      tryLayout("Sugiyama", [&]() {
        ogdf::SugiyamaLayout sugi;
        sugi.setRanking(new ogdf::OptimalRanking());
        sugi.setCrossMin(new ogdf::BarycenterHeuristic());
        sugi.runs(2);
        sugi.fails(4);
        sugi.transpose(true);
        auto* hier = new ogdf::OptimalHierarchyLayout();
        hier->layerDistance(120.0);
        hier->nodeDistance(80.0);
        sugi.setLayout(hier);
        sugi.arrangeCCs(true);
        sugi.call(skelAttr);
      });

      tryLayout("Planarization", [&]() {
        ogdf::PlanarizationLayout pl;
        auto* planarizer = new ogdf::SubgraphPlanarizer();
        auto* subgraph = new ogdf::PlanarSubgraphFast<int>();
        auto* inserter = new ogdf::VariableEmbeddingInserter();
        subgraph->runs(1);
        subgraph->maxThreads(1);
        inserter->removeReinsert(ogdf::RemoveReinsertType::None);
        planarizer->setSubgraph(subgraph);
        planarizer->setInserter(inserter);
        planarizer->permutations(1);
        planarizer->maxThreads(1);
        pl.setCrossMin(planarizer);
        pl.call(skelAttr);
      });

      // === Phase 2γ: top-N degree pick + multi-source BFS assignment ===
      // Pick N highest-degree clusters as super-centers. Multi-source BFS
      // (unweighted) along the cluster-only graph assigns every other
      // cluster to its closest super-center. Disconnected clusters get
      // their own singleton super-cluster. Repeat for several N values.
      const int K = static_cast<int>(result.clusters.size());
      if (K >= 4) {
        // Build weighted adjacency for the cluster-only graph.
        std::unordered_map<std::string, int> cidToIdx;
        for (int i = 0; i < K; ++i) cidToIdx[result.clusters[i].clusterId] = i;
        std::map<std::pair<int, int>, double> wsum;
        for (std::size_t i = 0; i < adj.size(); ++i) {
          auto cIt = nodeCluster.find(i);
          if (cIt == nodeCluster.end()) continue;
          auto aIt = cidToIdx.find(cIt->second);
          if (aIt == cidToIdx.end()) continue;
          for (std::size_t j : adj[i]) {
            if (j <= i) continue;
            auto cJt = nodeCluster.find(j);
            if (cJt == nodeCluster.end()) continue;
            auto bIt = cidToIdx.find(cJt->second);
            if (bIt == cidToIdx.end()) continue;
            if (aIt->second == bIt->second) continue;
            const int a = aIt->second, b = bIt->second;
            wsum[std::minmax(a, b)] += 1.0;
          }
        }
        std::vector<std::vector<int>> uwAdj(K);  // unweighted neighbours
        std::vector<double> deg(K, 0.0);
        for (const auto& [p, w] : wsum) {
          uwAdj[p.first].push_back(p.second);
          uwAdj[p.second].push_back(p.first);
          deg[p.first] += w;
          deg[p.second] += w;
        }

        // Run γ for a list of target N values.
        const std::vector<int> Ns = {8, 16, 24, 32, 48};

        // Precompute degree-sorted cluster index list (high → low). Tie
        // break by cluster idx (stable).
        std::vector<int> degRank(K);
        for (int i = 0; i < K; ++i) degRank[i] = i;
        std::sort(degRank.begin(), degRank.end(), [&](int a, int b) {
          if (deg[a] != deg[b]) return deg[a] > deg[b];
          return a < b;
        });

        for (int N : Ns) {
          if (N >= K) break;
          // Multi-source BFS from top-N degree centers.
          std::vector<int> centerOf(K, -1);
          std::queue<int> bfsQ;
          for (int s = 0; s < N; ++s) {
            const int c = degRank[s];
            centerOf[c] = s;
            bfsQ.push(c);
          }
          while (!bfsQ.empty()) {
            const int v = bfsQ.front();
            bfsQ.pop();
            for (int u : uwAdj[v]) {
              if (centerOf[u] < 0) {
                centerOf[u] = centerOf[v];
                bfsQ.push(u);
              }
            }
          }
          // Disconnected clusters → each gets its own super-cluster.
          int nextS = N;
          for (int i = 0; i < K; ++i) {
            if (centerOf[i] < 0) centerOf[i] = nextS++;
          }
          const int S = nextS;
          std::vector<int> superSize(S, 0);
          for (int i = 0; i < K; ++i) superSize[centerOf[i]] += 1;

          // Build super-cluster graph.
          ogdf::Graph superSkel;
          ogdf::GraphAttributes superSkelAttr(superSkel,
            ogdf::GraphAttributes::nodeGraphics | ogdf::GraphAttributes::edgeGraphics);
          std::vector<ogdf::node> sIdxToNode(S);
          for (int s = 0; s < S; ++s) {
            ogdf::node n = superSkel.newNode();
            superSkelAttr.width(n) = 80.0;
            superSkelAttr.height(n) = 80.0;
            sIdxToNode[s] = n;
          }
          std::set<std::pair<int, int>> superEdgeSet;
          for (const auto& [p, w] : wsum) {
            const int a = centerOf[p.first];
            const int b = centerOf[p.second];
            if (a == b) continue;
            superEdgeSet.insert(std::minmax(a, b));
          }
          for (const auto& [a, b] : superEdgeSet) {
            superSkel.newEdge(sIdxToNode[a], sIdxToNode[b]);
          }

          // Layout super-skel via FMMM.
          {
            ogdf::FMMMLayout fmmm;
            applyFmmmEnvSeed(fmmm);
            fmmm.useHighLevelOptions(true);
            fmmm.unitEdgeLength(120.0);
            fmmm.qualityVersusSpeed(ogdf::FMMMOptions::QualityVsSpeed::GorgeousAndEfficient);
            fmmm.call(superSkelAttr);
          }
          // Count super-skel crossings.
          std::size_t superCross = 0;
          {
            std::vector<std::pair<ogdf::node, ogdf::node>> es;
            es.reserve(superSkel.numberOfEdges());
            for (ogdf::edge e : superSkel.edges) es.emplace_back(e->source(), e->target());
            auto sgn = [](double x) { return (x > 0) - (x < 0); };
            for (std::size_t i = 0; i < es.size(); ++i) {
              for (std::size_t j = i + 1; j < es.size(); ++j) {
                const auto& a = es[i];
                const auto& b = es[j];
                if (a.first == b.first || a.first == b.second
                    || a.second == b.first || a.second == b.second) continue;
                const double ax = superSkelAttr.x(a.first), ay = superSkelAttr.y(a.first);
                const double bx = superSkelAttr.x(a.second), by = superSkelAttr.y(a.second);
                const double cx = superSkelAttr.x(b.first), cy = superSkelAttr.y(b.first);
                const double dx = superSkelAttr.x(b.second), dy = superSkelAttr.y(b.second);
                const int o1 = sgn((bx - ax) * (cy - ay) - (by - ay) * (cx - ax));
                const int o2 = sgn((bx - ax) * (dy - ay) - (by - ay) * (dx - ax));
                const int o3 = sgn((dx - cx) * (ay - cy) - (dy - cy) * (ax - cx));
                const int o4 = sgn((dx - cx) * (by - cy) - (dy - cy) * (bx - cx));
                if ((o1 != o2) && (o3 != o4) && (o1 != 0) && (o3 != 0)) ++superCross;
              }
            }
          }

          // Compute super-skel bbox + super-pitch.
          double sxMin = std::numeric_limits<double>::infinity();
          double syMin = sxMin;
          double sxMax = -sxMin;
          double syMax = -sxMin;
          for (ogdf::node n : superSkel.nodes) {
            sxMin = std::min(sxMin, superSkelAttr.x(n));
            syMin = std::min(syMin, superSkelAttr.y(n));
            sxMax = std::max(sxMax, superSkelAttr.x(n));
            syMax = std::max(syMax, superSkelAttr.y(n));
          }
          const double superPitchX = std::max(1.0, sxMax - sxMin) / std::max(1.0, std::sqrt((double)S));
          const double superPitchY = std::max(1.0, syMax - syMin) / std::max(1.0, std::sqrt((double)S));
          const double superPitch = std::max(superPitchX, superPitchY);

          // Per-super-cluster local layout + 2-level placement.
          std::vector<std::pair<double, double>> clusterFinalPos(K);
          for (int s = 0; s < S; ++s) {
            std::vector<int> members;
            for (int i = 0; i < K; ++i) if (centerOf[i] == s) members.push_back(i);
            if (members.empty()) continue;
            const ogdf::node sNode = sIdxToNode[s];
            const double tx = superSkelAttr.x(sNode);
            const double ty = superSkelAttr.y(sNode);
            if (members.size() == 1) {
              clusterFinalPos[members[0]] = {tx, ty};
              continue;
            }
            ogdf::Graph local;
            ogdf::GraphAttributes localAttr(local,
              ogdf::GraphAttributes::nodeGraphics | ogdf::GraphAttributes::edgeGraphics);
            std::unordered_map<int, ogdf::node> idxToLocal;
            for (int i : members) {
              ogdf::node n = local.newNode();
              localAttr.width(n) = 60.0;
              localAttr.height(n) = 60.0;
              idxToLocal[i] = n;
            }
            for (const auto& [p, w] : wsum) {
              if (centerOf[p.first] != s || centerOf[p.second] != s) continue;
              local.newEdge(idxToLocal[p.first], idxToLocal[p.second]);
            }
            ogdf::FMMMLayout fmmm;
            applyFmmmEnvSeed(fmmm);
            fmmm.useHighLevelOptions(true);
            fmmm.unitEdgeLength(40.0);
            fmmm.qualityVersusSpeed(ogdf::FMMMOptions::QualityVsSpeed::GorgeousAndEfficient);
            try { fmmm.call(localAttr); } catch (...) {}
            double cx = 0.0, cy = 0.0;
            for (int i : members) {
              cx += localAttr.x(idxToLocal[i]);
              cy += localAttr.y(idxToLocal[i]);
            }
            cx /= members.size();
            cy /= members.size();
            double localRadius = 0.0;
            for (int i : members) {
              const double ddx = localAttr.x(idxToLocal[i]) - cx;
              const double ddy = localAttr.y(idxToLocal[i]) - cy;
              localRadius = std::max(localRadius, std::sqrt(ddx * ddx + ddy * ddy));
            }
            if (localRadius <= 1e-3) localRadius = 1.0;
            const double targetRadius = 0.4 * superPitch;
            const double localScale = targetRadius / localRadius;
            for (int i : members) {
              const double lx = (localAttr.x(idxToLocal[i]) - cx) * localScale + tx;
              const double ly = (localAttr.y(idxToLocal[i]) - cy) * localScale + ty;
              clusterFinalPos[i] = {lx, ly};
            }
          }

          // Count cluster-edge crossings under 2-level placement.
          std::size_t twoLevelCross = 0;
          {
            std::vector<std::tuple<int, int>> edgesInt;
            edgesInt.reserve(wsum.size());
            for (const auto& [p, w] : wsum) edgesInt.emplace_back(p.first, p.second);
            auto sgn = [](double x) { return (x > 0) - (x < 0); };
            for (std::size_t i = 0; i < edgesInt.size(); ++i) {
              for (std::size_t j = i + 1; j < edgesInt.size(); ++j) {
                const auto [a1, b1] = edgesInt[i];
                const auto [a2, b2] = edgesInt[j];
                if (a1 == a2 || a1 == b2 || b1 == a2 || b1 == b2) continue;
                const double ax = clusterFinalPos[a1].first, ay = clusterFinalPos[a1].second;
                const double bx = clusterFinalPos[b1].first, by = clusterFinalPos[b1].second;
                const double cx = clusterFinalPos[a2].first, cy = clusterFinalPos[a2].second;
                const double dx = clusterFinalPos[b2].first, dy = clusterFinalPos[b2].second;
                const int o1 = sgn((bx - ax) * (cy - ay) - (by - ay) * (cx - ax));
                const int o2 = sgn((bx - ax) * (dy - ay) - (by - ay) * (dx - ax));
                const int o3 = sgn((dx - cx) * (ay - cy) - (dy - cy) * (ax - cx));
                const int o4 = sgn((dx - cx) * (by - cy) - (dy - cy) * (bx - cx));
                if ((o1 != o2) && (o3 != o4) && (o1 != 0) && (o3 != 0)) ++twoLevelCross;
              }
            }
          }

          std::vector<int> sizeSorted = superSize;
          std::sort(sizeSorted.rbegin(), sizeSorted.rend());
          int singletons = 0;
          for (int sz : superSize) if (sz == 1) ++singletons;
          std::fprintf(stderr,
            "[skel-probe] γ N=%d → S=%d (singletons=%d), super-skel: %d edges, %zu cross; cluster-edge cross under 2-level: %zu. Top sizes:",
            N, S, singletons,
            superSkel.numberOfEdges(), superCross, twoLevelCross);
          const int top = std::min<int>(8, static_cast<int>(sizeSorted.size()));
          for (int i = 0; i < top; ++i) std::fprintf(stderr, " %d", sizeSorted[i]);
          std::fprintf(stderr, "\n");
        }
      }
    }
  }

  cgCheckpoint("post-§8-supergraph-build");
  // 9. Super-graph layout. Strategy:
  //   - With polar anchoring active (≥2 polars + skeleton edges): FMMM with
  //     newInitialPlacement(false). Pre-seed polar cluster super-nodes from
  //     skeleton positions; pre-seed non-polar super-nodes from the centroid
  //     of their polar neighbours (or origin) so FMMM has a sensible start.
  //     This keeps the structural skeleton stable through main layout.
  //   - Otherwise: existing Sugiyama (≤600) / FMMM (>600) flow.
  //
  // Env-var experiments:
  //   DJERD_SUPERLAYOUT=planarization → option A: replace FMMM call with
  //     PlanarizationLayout (crossing-min focused).
  //   DJERD_POSTFMMM_SWAP=1 → option B: after polar+FMMM (and similarity
  //     fit), run a post-pass that greedily swaps super-node positions to
  //     reduce super-edge crossings.
  const char* superLayoutEnv = std::getenv("DJERD_SUPERLAYOUT");
  // Cluster-level planarization (variant B) tested Apr 30: edgeCrossings
  // 1810 → 3976 (+120%). Super-graph crossings do not map to actual
  // inter-cluster edge crossings because real edges route between member
  // nodes inside clusters, not between cluster centres. Plus polar anchors
  // are bypassed in planarization mode, destroying structural skeleton.
  // Default reverted to "default" (polar-anchored FMMM). Opt-in via
  // DJERD_SUPERLAYOUT=planarization for further experimentation.
  const std::string superLayoutChoice =
    superLayoutEnv ? std::string(superLayoutEnv) : std::string("default");
  const char* postFmmmSwapEnv = std::getenv("DJERD_POSTFMMM_SWAP");
  const bool postFmmmSwap =
    postFmmmSwapEnv && std::strcmp(postFmmmSwapEnv, "0") != 0;

  // === DJERD_CLUSTER_ANCHOR=1 ===
  // Run FMMM on the cluster-only graph (256 clusters + 230 inter-cluster
  // edges) and inject those positions as polar anchors for every cluster
  // not already structurally polar-anchored. Existing pipeline then sees
  // an extended polar-anchor set covering all 256 clusters; the super-graph
  // FMMM seeds + similarity-fit operate on that combined set.
  const char* clusterAnchorEnv = std::getenv("DJERD_CLUSTER_ANCHOR");
  const bool clusterAnchorMode =
    clusterAnchorEnv && std::strcmp(clusterAnchorEnv, "0") != 0;
  if (clusterAnchorMode && !result.clusters.empty()) {
    // Build cluster-only graph.
    ogdf::Graph coGraph;
    ogdf::GraphAttributes coAttr(coGraph,
      ogdf::GraphAttributes::nodeGraphics | ogdf::GraphAttributes::edgeGraphics);
    std::unordered_map<std::string, ogdf::node> cidToCo;
    for (const ClusterRecord& c : result.clusters) {
      ogdf::node n = coGraph.newNode();
      coAttr.width(n) = 80.0;
      coAttr.height(n) = 80.0;
      cidToCo[c.clusterId] = n;
    }
    std::set<std::pair<std::string, std::string>> coPairs;
    for (std::size_t i = 0; i < adj.size(); ++i) {
      auto cIt = nodeCluster.find(i);
      if (cIt == nodeCluster.end()) continue;
      for (std::size_t j : adj[i]) {
        if (j <= i) continue;
        auto cJt = nodeCluster.find(j);
        if (cJt == nodeCluster.end()) continue;
        if (cIt->second == cJt->second) continue;
        auto p = (cIt->second < cJt->second)
          ? std::make_pair(cIt->second, cJt->second)
          : std::make_pair(cJt->second, cIt->second);
        coPairs.insert(p);
      }
    }
    for (const auto& [a, b] : coPairs) {
      auto sIt = cidToCo.find(a);
      auto tIt = cidToCo.find(b);
      if (sIt == cidToCo.end() || tIt == cidToCo.end()) continue;
      coGraph.newEdge(sIt->second, tIt->second);
    }
    if (coGraph.numberOfNodes() >= 2) {
      ogdf::FMMMLayout fmmm;
      applyFmmmEnvSeed(fmmm);
      fmmm.useHighLevelOptions(true);
      fmmm.unitEdgeLength(120.0);
      fmmm.qualityVersusSpeed(ogdf::FMMMOptions::QualityVsSpeed::GorgeousAndEfficient);
      fmmm.call(coAttr);
      // Determine cluster-only spread; scale to match polar-anchor spread
      // (~3000-5000 unit centroid distance) so anchors don't fight each
      // other in the super-graph FMMM.
      double cxC = 0.0, cyC = 0.0;
      int nC = 0;
      for (ogdf::node n : coGraph.nodes) {
        cxC += coAttr.x(n);
        cyC += coAttr.y(n);
        ++nC;
      }
      cxC /= std::max(1, nC);
      cyC /= std::max(1, nC);
      double spreadC = 0.0;
      for (ogdf::node n : coGraph.nodes) {
        const double dx = coAttr.x(n) - cxC;
        const double dy = coAttr.y(n) - cyC;
        spreadC += std::sqrt(dx * dx + dy * dy);
      }
      spreadC /= std::max(1, nC);
      double spreadP = 0.0;
      if (!polarAnchors.empty()) {
        double cxP = 0.0, cyP = 0.0;
        for (const auto& kv : polarAnchors) {
          cxP += kv.second.first;
          cyP += kv.second.second;
        }
        cxP /= polarAnchors.size();
        cyP /= polarAnchors.size();
        for (const auto& kv : polarAnchors) {
          const double dx = kv.second.first - cxP;
          const double dy = kv.second.second - cyP;
          spreadP += std::sqrt(dx * dx + dy * dy);
        }
        spreadP /= polarAnchors.size();
      }
      const double targetSpread = (spreadP > 100.0) ? spreadP : 3500.0;
      const double scale = (spreadC > 1e-3) ? (targetSpread / spreadC) : 1.0;
      // Inject scaled cluster-only positions as polar anchors for every
      // cluster not already polar-anchored. Keep structural polar anchors
      // intact (they encode super-polar topology).
      std::size_t injected = 0;
      for (const ClusterRecord& c : result.clusters) {
        if (polarAnchors.count(c.rootIdx)) continue;
        auto coIt = cidToCo.find(c.clusterId);
        if (coIt == cidToCo.end()) continue;
        const double x = (coAttr.x(coIt->second) - cxC) * scale;
        const double y = (coAttr.y(coIt->second) - cyC) * scale;
        polarAnchors[c.rootIdx] = {x, y};
        ++injected;
      }
      std::fprintf(stderr,
        "[cluster-anchor] Injected %zu cluster anchors (scale=%.3f, target=%.1f, src=%.1f).\n",
        injected, scale, targetSpread, spreadC);
    }
  }

  // === DJERD_TWO_LEVEL_COMMUNITY=1 (prototype) ===
  // Promote the skel-probe γ 2-level placement into real cluster anchors.
  // Group the K clusters into S super-communities (top-N degree centers +
  // multi-source BFS on the cluster-only graph), lay out the super-communities
  // via FMMM, then lay out each super-community's members locally and place
  // them around the super-community centre. Inject the resulting positions as
  // polar anchors (same hand-off as DJERD_CLUSTER_ANCHOR). N is set by
  // DJERD_TWO_LEVEL_N (default 48 — the skel-probe sweep minimum). By default
  // ALL clusters (incl. structural polars) are overridden to faithfully match
  // the probe that produced the -46% cluster-edge-crossing signal; set
  // DJERD_TWO_LEVEL_KEEP_POLARS=1 to preserve polar-skeleton anchors instead.
  const char* twoLevelEnv = std::getenv("DJERD_TWO_LEVEL_COMMUNITY");
  const bool twoLevelMode = twoLevelEnv && std::strcmp(twoLevelEnv, "0") != 0;
  if (twoLevelMode && result.clusters.size() >= 4) {
    const int K = static_cast<int>(result.clusters.size());
    int targetN = 48;
    if (const char* nEnv = std::getenv("DJERD_TWO_LEVEL_N")) {
      targetN = std::max(2, std::atoi(nEnv));
    }
    const char* keepPolarsEnv = std::getenv("DJERD_TWO_LEVEL_KEEP_POLARS");
    const bool keepPolars = keepPolarsEnv && std::strcmp(keepPolarsEnv, "0") != 0;

    // Cluster index map + weighted cluster-only adjacency (wsum).
    std::unordered_map<std::string, int> cidToIdx;
    for (int i = 0; i < K; ++i) cidToIdx[result.clusters[i].clusterId] = i;
    std::map<std::pair<int, int>, double> wsum;
    for (std::size_t i = 0; i < adj.size(); ++i) {
      auto cIt = nodeCluster.find(i);
      if (cIt == nodeCluster.end()) continue;
      auto aIt = cidToIdx.find(cIt->second);
      if (aIt == cidToIdx.end()) continue;
      for (std::size_t j : adj[i]) {
        if (j <= i) continue;
        auto cJt = nodeCluster.find(j);
        if (cJt == nodeCluster.end()) continue;
        auto bIt = cidToIdx.find(cJt->second);
        if (bIt == cidToIdx.end()) continue;
        if (aIt->second == bIt->second) continue;
        wsum[std::minmax(aIt->second, bIt->second)] += 1.0;
      }
    }
    std::vector<std::vector<int>> uwAdj(K);
    std::vector<double> deg(K, 0.0);
    for (const auto& [p, w] : wsum) {
      uwAdj[p.first].push_back(p.second);
      uwAdj[p.second].push_back(p.first);
      deg[p.first] += w;
      deg[p.second] += w;
    }
    std::vector<int> degRank(K);
    for (int i = 0; i < K; ++i) degRank[i] = i;
    std::sort(degRank.begin(), degRank.end(), [&](int a, int b) {
      if (deg[a] != deg[b]) return deg[a] > deg[b];
      return a < b;
    });

    // Multi-source BFS: top-N degree centers seed super-communities.
    const int N = std::min(targetN, K - 1);
    std::vector<int> centerOf(K, -1);
    std::queue<int> bfsQ;
    for (int s = 0; s < N; ++s) {
      centerOf[degRank[s]] = s;
      bfsQ.push(degRank[s]);
    }
    while (!bfsQ.empty()) {
      const int v = bfsQ.front();
      bfsQ.pop();
      for (int u : uwAdj[v]) {
        if (centerOf[u] < 0) {
          centerOf[u] = centerOf[v];
          bfsQ.push(u);
        }
      }
    }
    int nextS = N;
    for (int i = 0; i < K; ++i) if (centerOf[i] < 0) centerOf[i] = nextS++;
    const int S = nextS;

    // Super-community graph + FMMM.
    ogdf::Graph superSkel;
    ogdf::GraphAttributes superSkelAttr(superSkel,
      ogdf::GraphAttributes::nodeGraphics | ogdf::GraphAttributes::edgeGraphics);
    std::vector<ogdf::node> sIdxToNode(S);
    for (int s = 0; s < S; ++s) {
      ogdf::node n = superSkel.newNode();
      superSkelAttr.width(n) = 80.0;
      superSkelAttr.height(n) = 80.0;
      sIdxToNode[s] = n;
    }
    std::set<std::pair<int, int>> superEdgeSet;
    for (const auto& [p, w] : wsum) {
      const int a = centerOf[p.first], b = centerOf[p.second];
      if (a != b) superEdgeSet.insert(std::minmax(a, b));
    }
    for (const auto& [a, b] : superEdgeSet) {
      superSkel.newEdge(sIdxToNode[a], sIdxToNode[b]);
    }
    if (superSkel.numberOfNodes() >= 2) {
      ogdf::FMMMLayout fmmm;
      applyFmmmEnvSeed(fmmm);
      fmmm.useHighLevelOptions(true);
      fmmm.unitEdgeLength(120.0);
      fmmm.qualityVersusSpeed(ogdf::FMMMOptions::QualityVsSpeed::GorgeousAndEfficient);
      fmmm.call(superSkelAttr);
    }

    double sxMin = std::numeric_limits<double>::infinity();
    double syMin = sxMin, sxMax = -sxMin, syMax = -sxMin;
    for (ogdf::node n : superSkel.nodes) {
      sxMin = std::min(sxMin, superSkelAttr.x(n));
      syMin = std::min(syMin, superSkelAttr.y(n));
      sxMax = std::max(sxMax, superSkelAttr.x(n));
      syMax = std::max(syMax, superSkelAttr.y(n));
    }
    const double superPitchX = std::max(1.0, sxMax - sxMin) / std::max(1.0, std::sqrt((double)S));
    const double superPitchY = std::max(1.0, syMax - syMin) / std::max(1.0, std::sqrt((double)S));
    const double superPitch = std::max(superPitchX, superPitchY);

    // Per-super-community local layout, placed around the super-centre.
    std::vector<std::pair<double, double>> clusterFinalPos(K, {0.0, 0.0});
    for (int s = 0; s < S; ++s) {
      std::vector<int> members;
      for (int i = 0; i < K; ++i) if (centerOf[i] == s) members.push_back(i);
      if (members.empty()) continue;
      const double tx = superSkelAttr.x(sIdxToNode[s]);
      const double ty = superSkelAttr.y(sIdxToNode[s]);
      if (members.size() == 1) {
        clusterFinalPos[members[0]] = {tx, ty};
        continue;
      }
      ogdf::Graph local;
      ogdf::GraphAttributes localAttr(local,
        ogdf::GraphAttributes::nodeGraphics | ogdf::GraphAttributes::edgeGraphics);
      std::unordered_map<int, ogdf::node> idxToLocal;
      for (int i : members) {
        ogdf::node n = local.newNode();
        localAttr.width(n) = 60.0;
        localAttr.height(n) = 60.0;
        idxToLocal[i] = n;
      }
      for (const auto& [p, w] : wsum) {
        if (centerOf[p.first] != s || centerOf[p.second] != s) continue;
        local.newEdge(idxToLocal[p.first], idxToLocal[p.second]);
      }
      ogdf::FMMMLayout fmmm;
      applyFmmmEnvSeed(fmmm);
      fmmm.useHighLevelOptions(true);
      fmmm.unitEdgeLength(40.0);
      fmmm.qualityVersusSpeed(ogdf::FMMMOptions::QualityVsSpeed::GorgeousAndEfficient);
      try { fmmm.call(localAttr); } catch (...) {}
      double cx = 0.0, cy = 0.0;
      for (int i : members) {
        cx += localAttr.x(idxToLocal[i]);
        cy += localAttr.y(idxToLocal[i]);
      }
      cx /= members.size();
      cy /= members.size();
      double localRadius = 0.0;
      for (int i : members) {
        const double ddx = localAttr.x(idxToLocal[i]) - cx;
        const double ddy = localAttr.y(idxToLocal[i]) - cy;
        localRadius = std::max(localRadius, std::sqrt(ddx * ddx + ddy * ddy));
      }
      if (localRadius <= 1e-3) localRadius = 1.0;
      const double localScale = (0.4 * superPitch) / localRadius;
      for (int i : members) {
        clusterFinalPos[i] = {
          (localAttr.x(idxToLocal[i]) - cx) * localScale + tx,
          (localAttr.y(idxToLocal[i]) - cy) * localScale + ty};
      }
    }

    // Scale to polar-anchor spread, then inject as anchors.
    double cxC = 0.0, cyC = 0.0;
    for (int i = 0; i < K; ++i) { cxC += clusterFinalPos[i].first; cyC += clusterFinalPos[i].second; }
    cxC /= std::max(1, K);
    cyC /= std::max(1, K);
    double spreadC = 0.0;
    for (int i = 0; i < K; ++i) {
      const double dx = clusterFinalPos[i].first - cxC;
      const double dy = clusterFinalPos[i].second - cyC;
      spreadC += std::sqrt(dx * dx + dy * dy);
    }
    spreadC /= std::max(1, K);
    double spreadP = 0.0;
    if (!polarAnchors.empty()) {
      double cxP = 0.0, cyP = 0.0;
      for (const auto& kv : polarAnchors) { cxP += kv.second.first; cyP += kv.second.second; }
      cxP /= polarAnchors.size();
      cyP /= polarAnchors.size();
      for (const auto& kv : polarAnchors) {
        const double dx = kv.second.first - cxP;
        const double dy = kv.second.second - cyP;
        spreadP += std::sqrt(dx * dx + dy * dy);
      }
      spreadP /= polarAnchors.size();
    }
    const double targetSpread = (spreadP > 100.0) ? spreadP : 3500.0;
    const double scale = (spreadC > 1e-3) ? (targetSpread / spreadC) : 1.0;
    std::size_t injected = 0;
    for (const ClusterRecord& c : result.clusters) {
      if (keepPolars && polarAnchors.count(c.rootIdx)) continue;
      auto it = cidToIdx.find(c.clusterId);
      if (it == cidToIdx.end()) continue;
      const double x = (clusterFinalPos[it->second].first - cxC) * scale;
      const double y = (clusterFinalPos[it->second].second - cyC) * scale;
      polarAnchors[c.rootIdx] = {x, y};
      ++injected;
    }
    std::fprintf(stderr,
      "[two-level] N=%d S=%d injected=%zu anchors (keepPolars=%d, scale=%.3f, target=%.1f).\n",
      N, S, injected, keepPolars ? 1 : 0, scale, targetSpread);
  }

  const bool polarAnchorActive =
    (polarAnchors.size() >= 2) && (superLayoutChoice != "planarization");
  if (super.numberOfNodes() >= 2) {
    if (polarAnchorActive) {
      // Pre-seed positions.
      for (const ClusterRecord& c : result.clusters) {
        auto snIt = clusterToSuper.find(c.clusterId);
        if (snIt == clusterToSuper.end()) continue;
        auto pIt = polarAnchors.find(c.rootIdx);
        if (pIt != polarAnchors.end()) {
          superAttr.x(snIt->second) = pIt->second.first;
          superAttr.y(snIt->second) = pIt->second.second;
        }
      }
      // Non-polar cluster / connector / router / independent super-nodes:
      // start at the centroid of their adjacent polar anchors.
      auto seedFromNeighbours = [&](ogdf::node sn) {
        if (sn == nullptr) return;
        if (superAttr.x(sn) != 0.0 || superAttr.y(sn) != 0.0) return;
        double sx = 0.0, sy = 0.0;
        int n = 0;
        for (ogdf::adjEntry ae : sn->adjEntries) {
          ogdf::node other = ae->twinNode();
          const double ox = superAttr.x(other);
          const double oy = superAttr.y(other);
          if (ox == 0.0 && oy == 0.0) continue;
          sx += ox;
          sy += oy;
          ++n;
        }
        if (n > 0) {
          superAttr.x(sn) = sx / static_cast<double>(n);
          superAttr.y(sn) = sy / static_cast<double>(n);
        }
      };
      for (const ClusterRecord& c : result.clusters) {
        auto it = clusterToSuper.find(c.clusterId);
        if (it != clusterToSuper.end() && !polarAnchors.count(c.rootIdx)) {
          seedFromNeighbours(it->second);
        }
      }
      for (const auto& [_, sn] : connectorToSuper) seedFromNeighbours(sn);
      for (const auto& [_, sn] : routerToSuper) seedFromNeighbours(sn);
      for (const auto& [_, sn] : indepToSuper) seedFromNeighbours(sn);

      // Polar anchors are at a much wider scale (super-polar separation
      // ~3000-5000 units). With unitEdgeLength=120 FMMM would compress the
      // whole layout. Scale FMMM's target edge length so it complements the
      // anchor scale instead of fighting it. Tunable.
      double anchorSpread = 0.0;
      {
        double cx = 0.0, cy = 0.0;
        for (const auto& kv : polarAnchors) { cx += kv.second.first; cy += kv.second.second; }
        const double inv = 1.0 / static_cast<double>(polarAnchors.size());
        cx *= inv; cy *= inv;
        for (const auto& kv : polarAnchors) {
          const double dx = kv.second.first - cx;
          const double dy = kv.second.second - cy;
          anchorSpread += std::sqrt(dx * dx + dy * dy);
        }
        anchorSpread *= inv;
      }
      // D: hard cap unitEdgeLength so anchorSpread doesn't blow it up.
      const double anchorEdgeLen = std::min(300.0,
        std::max(120.0, anchorSpread / 12.0));

      ogdf::FMMMLayout fmmm;

      applyFmmmEnvSeed(fmmm);
      fmmm.useHighLevelOptions(true);
      fmmm.unitEdgeLength(anchorEdgeLen);
      fmmm.newInitialPlacement(false);
      fmmm.qualityVersusSpeed(ogdf::FMMMOptions::QualityVsSpeed::GorgeousAndEfficient);
      // F: tight component packing so disconnected components don't spread
      // the layout via FMMM's default arrangeCCs (default minDistCC ~100).
      fmmm.minDistCC(50.0);
      // Skip the super-graph FMMM positioning when positions are overwritten
      // downstream — super-nodes keep their polar-anchor/centroid pre-seed,
      // which the similarity-fit below leaves ~unchanged and --positions-tsv
      // overrides entirely.
      if (!skipCgPositioning) fmmm.call(superAttr);

      // Post-FMMM: similarity-fit (translate + uniform scale) so polar
      // super-nodes land back at their anchor positions. Applies the same
      // transform to ALL super-nodes so non-polar relative structure from
      // FMMM is preserved. Without this pass FMMM tends to compress the
      // skeleton even with a generous unitEdgeLength.
      if (polarAnchors.size() >= 2) {
        // Build matched pairs: (polar fmmm pos) ↔ (polar anchor pos).
        std::vector<std::pair<double, double>> pAfter, pAnchor;
        pAfter.reserve(polarAnchors.size());
        pAnchor.reserve(polarAnchors.size());
        for (const ClusterRecord& c : result.clusters) {
          auto pIt = polarAnchors.find(c.rootIdx);
          if (pIt == polarAnchors.end()) continue;
          auto snIt = clusterToSuper.find(c.clusterId);
          if (snIt == clusterToSuper.end()) continue;
          pAfter.emplace_back(superAttr.x(snIt->second), superAttr.y(snIt->second));
          pAnchor.emplace_back(pIt->second.first, pIt->second.second);
        }
        if (pAfter.size() >= 2) {
          double cax = 0.0, cay = 0.0, ccx = 0.0, ccy = 0.0;
          for (std::size_t i = 0; i < pAfter.size(); ++i) {
            cax += pAfter[i].first; cay += pAfter[i].second;
            ccx += pAnchor[i].first; ccy += pAnchor[i].second;
          }
          const double inv = 1.0 / static_cast<double>(pAfter.size());
          cax *= inv; cay *= inv; ccx *= inv; ccy *= inv;
          double sa = 0.0, sc = 0.0;
          for (std::size_t i = 0; i < pAfter.size(); ++i) {
            const double dax = pAfter[i].first - cax;
            const double day = pAfter[i].second - cay;
            const double dcx = pAnchor[i].first - ccx;
            const double dcy = pAnchor[i].second - ccy;
            sa += std::sqrt(dax * dax + day * day);
            sc += std::sqrt(dcx * dcx + dcy * dcy);
          }
          const double scale = (sa > 1e-3) ? (sc / sa) : 1.0;
          // Apply scale clamp to avoid pathological blow-up. Tightened
          // further (was [0.5, 2.5]) — the stretch upper bound was a major
          // bbox driver since FMMM compresses then fit stretches 2.5x.
          const double s = std::min(1.8, std::max(0.5, scale));
          for (ogdf::node v : super.nodes) {
            const double nx = (superAttr.x(v) - cax) * s + ccx;
            const double ny = (superAttr.y(v) - cay) * s + ccy;
            superAttr.x(v) = nx;
            superAttr.y(v) = ny;
          }
        }
      }

      // Option B: post-FMMM swap pass. Greedy super-node position swaps
      // that reduce super-edge crossings. Polar-anchored super-nodes are
      // pinned (their positions encode the structural skeleton).
      if (postFmmmSwap) {
        std::unordered_set<ogdf::node> pinned;
        for (const ClusterRecord& c : result.clusters) {
          if (!polarAnchors.count(c.rootIdx)) continue;
          auto it = clusterToSuper.find(c.clusterId);
          if (it != clusterToSuper.end()) pinned.insert(it->second);
        }
        std::vector<ogdf::node> moveable;
        moveable.reserve(super.numberOfNodes());
        for (ogdf::node v : super.nodes) {
          if (!pinned.count(v)) moveable.push_back(v);
        }
        std::vector<std::pair<ogdf::node, ogdf::node>> superEdgePairs;
        superEdgePairs.reserve(super.numberOfEdges());
        for (ogdf::edge e : super.edges) {
          superEdgePairs.emplace_back(e->source(), e->target());
        }
        // Per-node incident edge index.
        std::unordered_map<ogdf::node, std::vector<std::size_t>> incident;
        for (std::size_t i = 0; i < superEdgePairs.size(); ++i) {
          incident[superEdgePairs[i].first].push_back(i);
          incident[superEdgePairs[i].second].push_back(i);
        }
        auto sgn = [](double x) {
          return (x > 0) - (x < 0);
        };
        auto cross2 = [&](double ax, double ay, double bx, double by,
                          double cx, double cy, double dx, double dy) -> bool {
          const int o1 = sgn((bx - ax) * (cy - ay) - (by - ay) * (cx - ax));
          const int o2 = sgn((bx - ax) * (dy - ay) - (by - ay) * (dx - ax));
          const int o3 = sgn((dx - cx) * (ay - cy) - (dy - cy) * (ax - cx));
          const int o4 = sgn((dx - cx) * (by - cy) - (dy - cy) * (bx - cx));
          return (o1 != o2) && (o3 != o4) && (o1 != 0) && (o3 != 0);
        };
        auto edgeXing = [&](std::size_t i, std::size_t j) -> bool {
          const auto& a = superEdgePairs[i];
          const auto& b = superEdgePairs[j];
          if (a.first == b.first || a.first == b.second
              || a.second == b.first || a.second == b.second) return false;
          return cross2(
            superAttr.x(a.first), superAttr.y(a.first),
            superAttr.x(a.second), superAttr.y(a.second),
            superAttr.x(b.first), superAttr.y(b.first),
            superAttr.x(b.second), superAttr.y(b.second));
        };
        auto countLocal = [&](ogdf::node u, ogdf::node v) -> std::size_t {
          std::unordered_set<std::size_t> uniqueE;
          for (std::size_t i : incident[u]) uniqueE.insert(i);
          for (std::size_t i : incident[v]) uniqueE.insert(i);
          std::size_t total = 0;
          for (std::size_t i : uniqueE) {
            for (std::size_t j = 0; j < superEdgePairs.size(); ++j) {
              if (uniqueE.count(j) && j <= i) continue;
              if (edgeXing(i, j)) ++total;
            }
          }
          return total;
        };
        std::mt19937 rng(0xC0FFEEu);
        std::uniform_int_distribution<std::size_t> dist(0, moveable.size() ? moveable.size() - 1 : 0);
        const std::size_t kAttempts = 5000;
        std::size_t accepted = 0;
        if (moveable.size() >= 2) {
          for (std::size_t k = 0; k < kAttempts; ++k) {
            const std::size_t i = dist(rng);
            std::size_t j = dist(rng);
            if (i == j) continue;
            ogdf::node u = moveable[i];
            ogdf::node w = moveable[j];
            const std::size_t before = countLocal(u, w);
            const double ux = superAttr.x(u), uy = superAttr.y(u);
            superAttr.x(u) = superAttr.x(w);
            superAttr.y(u) = superAttr.y(w);
            superAttr.x(w) = ux;
            superAttr.y(w) = uy;
            const std::size_t after = countLocal(u, w);
            if (after < before) {
              ++accepted;
            } else {
              superAttr.x(w) = superAttr.x(u);
              superAttr.y(w) = superAttr.y(u);
              superAttr.x(u) = ux;
              superAttr.y(u) = uy;
            }
          }
        }
        std::fprintf(stderr,
          "[cluster_graph] Post-FMMM swap pass: %zu accepted of %zu attempts.\n",
          accepted, kAttempts);
      }
    } else if (superLayoutChoice == "planarization") {
      // Option A: PlanarizationLayout (crossing-min focused). Polar
      // anchors are deliberately not honored here — the experiment is to
      // see whether crossing-min beats polar+FMMM on this dataset.
      try {
        ogdf::PlanarizationLayout pl;
        auto* planarizer = new ogdf::SubgraphPlanarizer();
        auto* subgraph = new ogdf::PlanarSubgraphFast<int>();
        auto* inserter = new ogdf::VariableEmbeddingInserter();
        subgraph->runs(1);
        subgraph->maxThreads(1);
        inserter->removeReinsert(ogdf::RemoveReinsertType::None);
        planarizer->setSubgraph(subgraph);
        planarizer->setInserter(inserter);
        planarizer->permutations(1);
        planarizer->maxThreads(1);
        pl.setCrossMin(planarizer);
        pl.pageRatio(1.0);
        pl.call(superAttr);
        // Rescale to match cluster_graph spacing expectations. Planarization
        // outputs at compact orthogonal scale; later passes assume polar
        // anchor scale ~3000-5000 unit centroid spread.
        double minX = std::numeric_limits<double>::infinity();
        double minY = minX;
        double maxX = -minX, maxY = -minX;
        for (ogdf::node v : super.nodes) {
          minX = std::min(minX, superAttr.x(v));
          minY = std::min(minY, superAttr.y(v));
          maxX = std::max(maxX, superAttr.x(v));
          maxY = std::max(maxY, superAttr.y(v));
        }
        const double w = maxX - minX;
        const double h = maxY - minY;
        const double bigger = std::max(w, h);
        if (bigger > 1e-3) {
          const double targetSize = 6000.0;
          const double s = targetSize / bigger;
          const double cx = (minX + maxX) * 0.5;
          const double cy = (minY + maxY) * 0.5;
          for (ogdf::node v : super.nodes) {
            superAttr.x(v) = (superAttr.x(v) - cx) * s;
            superAttr.y(v) = (superAttr.y(v) - cy) * s;
          }
        }
      } catch (const std::exception& ex) {
        // Fall back to FMMM if planarization throws.
        ogdf::FMMMLayout fmmm;
        applyFmmmEnvSeed(fmmm);
        fmmm.useHighLevelOptions(true);
        fmmm.unitEdgeLength(120.0);
        fmmm.newInitialPlacement(true);
        fmmm.qualityVersusSpeed(ogdf::FMMMOptions::QualityVsSpeed::BeautifulAndFast);
        fmmm.call(superAttr);
        std::fprintf(stderr,
          "[cluster_graph] Planarization super-layout failed (%s), fell back to FMMM.\n",
          ex.what());
      }
    } else if (super.numberOfNodes() <= 600) {
      ogdf::SugiyamaLayout sugi;
      sugi.setRanking(new ogdf::OptimalRanking());
      sugi.setCrossMin(new ogdf::BarycenterHeuristic());
      sugi.runs(2);
      sugi.fails(4);
      sugi.transpose(true);
      auto* hier = new ogdf::OptimalHierarchyLayout();
      hier->layerDistance(120.0);
      hier->nodeDistance(80.0);
      hier->weightBalancing(0.72);
      sugi.setLayout(hier);
      sugi.arrangeCCs(true);
      try {
        sugi.call(superAttr);
      } catch (...) {
        ogdf::FMMMLayout fmmm;
        applyFmmmEnvSeed(fmmm);
        fmmm.useHighLevelOptions(true);
        fmmm.unitEdgeLength(120.0);
        fmmm.newInitialPlacement(true);
        fmmm.qualityVersusSpeed(ogdf::FMMMOptions::QualityVsSpeed::BeautifulAndFast);
        fmmm.call(superAttr);
      }
    } else {
      ogdf::FMMMLayout fmmm;
      applyFmmmEnvSeed(fmmm);
      fmmm.useHighLevelOptions(true);
      fmmm.unitEdgeLength(120.0);
      fmmm.newInitialPlacement(true);
      fmmm.qualityVersusSpeed(ogdf::FMMMOptions::QualityVsSpeed::BeautifulAndFast);
      fmmm.call(superAttr);
    }
  }

  // 9b. Degree-weighted repulsion pass — only between cluster roots and
  // routers (the high-leverage hubs). FMMM treats all super-nodes equally
  // by size, so high-degree hubs don't push each other harder than low-
  // degree pairs. This pass enforces a minimum separation that grows with
  // the dedup-degree of the participating root/router. Runs as a few
  // iterations of a soft-constraint solver; only super-node centres move.
  {
    struct Hub {
      ogdf::node sn;
      std::size_t deg;
    };
    std::vector<Hub> hubs;
    hubs.reserve(result.clusters.size() + result.routers.size());
    for (const ClusterRecord& c : result.clusters) {
      auto it = clusterToSuper.find(c.clusterId);
      if (it == clusterToSuper.end()) continue;
      hubs.push_back({it->second, adj[c.rootIdx].size()});
    }
    for (const RouterRecord& r : result.routers) {
      auto it = routerToSuper.find(r.nodeIdx);
      if (it == routerToSuper.end()) continue;
      hubs.push_back({it->second, adj[r.nodeIdx].size()});
    }
    // Degree-weighted hub separation. Flagged as "too strong" — the gap
    // grows 8 units per unit of summed hub degree, which over-spreads
    // dense high-degree components (memory: high-degree-repulsion).
    // Env-overridable so the compactness/crossings trade-off can be swept
    // without a rebuild: DJERD_HUB_REPULSION_BASE_GAP (default 300),
    // DJERD_HUB_REPULSION_DEG_GAP (default 8). Set DEG_GAP=0 to make hub
    // separation degree-independent (pack dense components tighter).
    const double kBaseGap = [] {
      const char* e = std::getenv("DJERD_HUB_REPULSION_BASE_GAP");
      return e && *e ? std::strtod(e, nullptr) : 300.0;
    }();
    const double kDegGap = [] {
      const char* e = std::getenv("DJERD_HUB_REPULSION_DEG_GAP");
      return e && *e ? std::strtod(e, nullptr) : 8.0;
    }();
    constexpr std::size_t kIters = 16;
    for (std::size_t iter = 0; iter < kIters; ++iter) {
      bool moved = false;
      for (std::size_t i = 0; i < hubs.size(); ++i) {
        for (std::size_t j = i + 1; j < hubs.size(); ++j) {
          const ogdf::node a = hubs[i].sn;
          const ogdf::node b = hubs[j].sn;
          const double ax = superAttr.x(a), ay = superAttr.y(a);
          const double bx = superAttr.x(b), by = superAttr.y(b);
          const double aw = superAttr.width(a) / 2.0;
          const double ah = superAttr.height(a) / 2.0;
          const double bw = superAttr.width(b) / 2.0;
          const double bh = superAttr.height(b) / 2.0;
          // Required centre-to-centre distance: bbox half-widths plus gap.
          const double degSum = static_cast<double>(hubs[i].deg + hubs[j].deg);
          const double gap = kBaseGap + kDegGap * degSum;
          // Use Manhattan-ish: requireDx = aw + bw + gap, similarly y.
          const double reqDx = aw + bw + gap;
          const double reqDy = ah + bh + gap;
          const double dx = bx - ax;
          const double dy = by - ay;
          if (std::abs(dx) >= reqDx || std::abs(dy) >= reqDy) continue;
          // Both axes too close — push apart on the smaller-overshoot axis.
          const double overX = reqDx - std::abs(dx);
          const double overY = reqDy - std::abs(dy);
          if (overX < overY) {
            const double sign = dx >= 0 ? 1.0 : -1.0;
            const double half = overX / 2.0 + 0.05;
            superAttr.x(a) -= sign * half;
            superAttr.x(b) += sign * half;
          } else {
            const double sign = dy >= 0 ? 1.0 : -1.0;
            const double half = overY / 2.0 + 0.05;
            superAttr.y(a) -= sign * half;
            superAttr.y(b) += sign * half;
          }
          moved = true;
        }
      }
      if (!moved) break;
    }
  }

  // 9b0. High-degree super-node outward bias.
  // After hub-pair repulsion settles, push the top-decile-degree
  // cluster roots radially OUTWARD from the layout centroid. Reason:
  // a high-deg root sitting near the centroid becomes a sink for
  // radial edges from every direction → dense central tangle. Pushing
  // them to ~1.2× current avg radius spreads incident-edge angles
  // along an arc, reducing the "everything pointing inward" pattern.
  // Done at super-graph level so inner placement (which translates
  // each cluster as a rigid block to its super-node position) carries
  // the cluster bbox along — no post-pass cluster-shifting (which
  // already proved to balloon crossings).
  {
    if (result.clusters.size() >= 8) {
      // Collect cluster super-nodes with their (rootIdx, deg).
      struct Hub2 { ogdf::node sn; std::size_t deg; std::size_t rootIdx; };
      std::vector<Hub2> hubs2;
      hubs2.reserve(result.clusters.size());
      for (const ClusterRecord& c : result.clusters) {
        auto it = clusterToSuper.find(c.clusterId);
        if (it == clusterToSuper.end()) continue;
        hubs2.push_back({it->second, adj[c.rootIdx].size(), c.rootIdx});
      }
      // Layout centroid over all super-nodes.
      double sCx = 0.0, sCy = 0.0;
      std::size_t sCnt = 0;
      for (ogdf::node v : super.nodes) {
        sCx += superAttr.x(v); sCy += superAttr.y(v); ++sCnt;
      }
      if (sCnt > 0) {
        sCx /= static_cast<double>(sCnt);
        sCy /= static_cast<double>(sCnt);
        double sumR = 0.0;
        for (ogdf::node v : super.nodes) {
          const double dx = superAttr.x(v) - sCx;
          const double dy = superAttr.y(v) - sCy;
          sumR += std::sqrt(dx * dx + dy * dy);
        }
        const double avgR2 = sumR / static_cast<double>(sCnt);
        // Sort hubs by deg desc, push top decile.
        std::sort(hubs2.begin(), hubs2.end(),
          [](const Hub2& a, const Hub2& b) { return a.deg > b.deg; });
        // Top decile (10%) tested at 22 hubs; 15% widens to ~33 hubs
        // for additional outward spread.
        const std::size_t topN = std::max<std::size_t>(3,
          (hubs2.size() * 15 + 50) / 100);
        // 1.25 → 1.4 — push hubs further out so layout center is even
        // less hub-dense. Tested 1.25=baseline, 1.4=measured below.
        const double targetR = 1.4 * avgR2;
        std::size_t pushed = 0;
        for (std::size_t k = 0; k < topN && k < hubs2.size(); ++k) {
          const ogdf::node sn = hubs2[k].sn;
          const double hx = superAttr.x(sn);
          const double hy = superAttr.y(sn);
          const double dx = hx - sCx;
          const double dy = hy - sCy;
          const double d = std::sqrt(dx * dx + dy * dy);
          if (d >= targetR) continue;
          double angle;
          if (d < 100.0) {
            angle = (2.0 * 3.14159265358979)
              * static_cast<double>(k)
              / static_cast<double>(std::max<std::size_t>(1, topN));
          } else {
            angle = std::atan2(dy, dx);
          }
          superAttr.x(sn) = sCx + targetR * std::cos(angle);
          superAttr.y(sn) = sCy + targetR * std::sin(angle);
          ++pushed;
        }
        if (pushed > 0) {
          std::fprintf(stderr,
            "[hub-bias] Super-graph: pushed %zu high-deg cluster roots to 1.4x avgR.\n",
            pushed);
        }
      }
    }
  }
  cgCheckpoint("post-hub-bias");


  // 9b1. Rank-based perimeter tier shaping (graph-terminology.md §3.7/§3.8/§3.9).
  //
  // 2-root vs 3+root SEPARATION (§3.9): Polars are 3+root participants
  // (their cluster connects to 3+ external roots) → kept INTERIOR at their
  // polar-anchor positions. Only NON-POLAR cluster roots (= 2-root chain
  // participants) are eligible for the outer perimeter ring.
  //
  // Sort non-polar cluster roots by weight (leaf-tree size + dedup degree).
  // Push top √N tier to perimeterR (outer ring beyond polar bbox), linear
  // decay for the rest. This forms an outline of 2-root structures around
  // the polar/3+root interior.
  {
    // Walk pruning parent chain to find each pruned node's ultimate
    // 2-core anchor; aggregate counts per anchor = leaf weight.
    std::unordered_map<std::size_t, std::size_t> immediateParent;
    for (const PrunedNodeRecord& p : result.prunedNodes) {
      immediateParent[p.nodeIdx] = p.parentIdx;
    }
    auto coreAnchorOf = [&](std::size_t v) {
      int hops = 0;
      while (hops++ < 200) {
        auto it = immediateParent.find(v);
        if (it == immediateParent.end() || it->second == v) break;
        v = it->second;
      }
      return v;
    };
    std::unordered_map<std::size_t, std::size_t> leafW;
    for (const PrunedNodeRecord& p : result.prunedNodes) {
      if (p.isAloneRoot) continue;
      ++leafW[coreAnchorOf(p.parentIdx)];
    }

    // Polar set (3+root participants — interior, NOT in perimeter rank list)
    std::unordered_set<std::size_t> polarSet;
    for (const PolarRecord& p : result.polars) polarSet.insert(p.nodeIdx);

    struct RootRank {
      std::string clusterId;
      std::size_t rootIdx;
      double weight;     // leaf size + degree (combined size proxy)
    };
    std::vector<RootRank> rrs;
    rrs.reserve(result.clusters.size());
    for (const ClusterRecord& c : result.clusters) {
      if (polarSet.count(c.rootIdx)) continue;  // §3.9: polars stay interior
      const std::size_t lf = leafW.count(c.rootIdx) ? leafW[c.rootIdx] : 0;
      const std::size_t dg = adj[c.rootIdx].size();
      rrs.push_back({c.clusterId, c.rootIdx,
                      static_cast<double>(lf) + static_cast<double>(dg)});
    }
    if (rrs.size() >= 2) {
    std::sort(rrs.begin(), rrs.end(),
      [](const RootRank& a, const RootRank& b) {
        if (a.weight != b.weight) return a.weight > b.weight;
        return a.rootIdx < b.rootIdx;
      });

    // Centroid of ALL cluster super-nodes (polars + non-polars)
    double cx = 0.0, cy = 0.0;
    std::size_t cnt = 0;
    for (const ClusterRecord& c : result.clusters) {
      auto it = clusterToSuper.find(c.clusterId);
      if (it == clusterToSuper.end()) continue;
      cx += superAttr.x(it->second);
      cy += superAttr.y(it->second);
      ++cnt;
    }
    if (cnt >= 2) {
    cx /= static_cast<double>(cnt);
    cy /= static_cast<double>(cnt);

    // maxR considers ALL cluster super-nodes including polars — so the
    // perimeter ring for non-polars is placed BEYOND polar bbox (§3.9).
    double maxR = 0.0;
    for (const ClusterRecord& c : result.clusters) {
      auto it = clusterToSuper.find(c.clusterId);
      if (it == clusterToSuper.end()) continue;
      const double dx = superAttr.x(it->second) - cx;
      const double dy = superAttr.y(it->second) - cy;
      maxR = std::max(maxR, std::sqrt(dx * dx + dy * dy));
    }

    const std::size_t N = rrs.size();
    const std::size_t tierTop = std::max<std::size_t>(2,
      static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(N)))));
    const double perimeterR = std::max(1000.0, maxR * 1.35);
    // Backbone (main ring/path) is placed on its OWN outermost ring so that
    // it stands out visually against the inner non-backbone clusters. With
    // `backboneR > perimeterR`, the backbone forms a distinct outline and
    // there is empty space between it and the next inner tier (= visual
    // prominence requirement, graph-terminology.md §3.10).
    const double backboneR = perimeterR * 1.7;
    const double kPi = 3.14159265358979;

    // §3.10 Main backbone detection — find the highest-scoring structure
    // among non-polar cluster roots: either a closed ring (cycle) OR an
    // open한붓그리기 path (linear). Both are scored as Σ deg of nodes on the
    // structure. Greedy DFS from top-deg starts; closed if last node has
    // edge back to start, else treated as open path.
    std::vector<std::size_t> mainRing;
    double bestRingScore = -1.0;
    bool mainRingClosed = false;
    // Multi-backbone detection: extract iterative longest-paths from the
    // root-adjacency graph. Each path = one backbone (= one row in the
    // final layout). 2-root paths (just two cluster roots connected) are
    // accepted — user 2026-04-28 feedback: "2-root의 backbone도 허용".
    //
    // Algorithm:
    //   1. Build root-to-root adjacency (direct + connector-mediated)
    //      among all cluster roots. Polars are included so the highest-
    //      deg hubs are eligible to be on a backbone.
    //   2. Repeatedly find the highest-scoring path (greedy DFS from the
    //      highest-deg remaining root), accept if size ≥ 2, then remove
    //      its nodes from the candidate pool. Repeat until no path of
    //      size ≥ 2 remains.
    //   3. Each extracted path is a separate backbone. The first one
    //      (highest score) is the main backbone; smaller ones are
    //      secondary backbones placed in additional rows.
    //
    // Score = sum of dedup-degree across the path. mainRing stores the
    // CONCATENATED backbone members (all rows back-to-back) and
    // mainRingRowOfNode tags each member with its row index so
    // downstream placement / flatten can address them per-row.
    std::vector<std::vector<std::size_t>> backbones;
    {
      std::unordered_map<std::size_t, std::set<std::size_t>> bbAdj;
      std::unordered_map<std::string, std::size_t> cidToRoot;
      for (const ClusterRecord& c : result.clusters) {
        cidToRoot[c.clusterId] = c.rootIdx;
      }
      // Direct root-root edges
      for (const ClusterRecord& c : result.clusters) {
        for (std::size_t j : adj[c.rootIdx]) {
          if (j == c.rootIdx) continue;
          auto rIt = rootToCluster.find(j);
          if (rIt == rootToCluster.end()) continue;
          bbAdj[c.rootIdx].insert(j);
          bbAdj[j].insert(c.rootIdx);
        }
      }
      // Connector-mediated
      for (const ConnectorRecord& con : result.connectors) {
        if (con.connectedClusterIds.size() != 2) continue;
        auto a = cidToRoot.find(con.connectedClusterIds[0]);
        auto b = cidToRoot.find(con.connectedClusterIds[1]);
        if (a == cidToRoot.end() || b == cidToRoot.end()) continue;
        if (a->second == b->second) continue;
        bbAdj[a->second].insert(b->second);
        bbAdj[b->second].insert(a->second);
      }

      // All cluster roots, sorted by deg desc.
      std::vector<std::size_t> rootsByDegDesc;
      rootsByDegDesc.reserve(result.clusters.size());
      for (const ClusterRecord& c : result.clusters) {
        rootsByDegDesc.push_back(c.rootIdx);
      }
      std::sort(rootsByDegDesc.begin(), rootsByDegDesc.end(),
        [&](std::size_t a, std::size_t b) {
          const std::size_t da = adj[a].size();
          const std::size_t db = adj[b].size();
          if (da != db) return da > db;
          return a < b;
        });

      // Available pool of backbone candidates.
      std::unordered_set<std::size_t> available(
        rootsByDegDesc.begin(), rootsByDegDesc.end());

      // Limit total backbone hubs to ceil(sqrt(N_clusters)) so visual
      // density stays manageable. Beyond that, lower-deg hubs aren't
      // structurally significant enough to form a backbone row.
      const std::size_t maxBackboneNodes = std::max<std::size_t>(6,
        static_cast<std::size_t>(std::ceil(std::sqrt(
          static_cast<double>(result.clusters.size())))));
      std::size_t totalBackboneNodes = 0;

      // Iteratively extract longest path from highest-deg start.
      while (totalBackboneNodes < maxBackboneNodes && !available.empty()) {
        // Find best start: highest-deg root in `available`.
        std::size_t start = std::numeric_limits<std::size_t>::max();
        for (std::size_t r : rootsByDegDesc) {
          if (available.count(r)) { start = r; break; }
        }
        if (start == std::numeric_limits<std::size_t>::max()) break;

        // Greedy DFS: walk highest-deg unvisited neighbour each step.
        std::vector<std::size_t> path = {start};
        std::unordered_set<std::size_t> vis = {start};
        while (path.size() < 200
                && totalBackboneNodes + path.size() < maxBackboneNodes + 1) {
          const std::size_t last = path.back();
          auto adjIt = bbAdj.find(last);
          if (adjIt == bbAdj.end()) break;
          std::size_t best = std::numeric_limits<std::size_t>::max();
          std::size_t bestDeg = 0;
          for (std::size_t n : adjIt->second) {
            if (vis.count(n) || !available.count(n)) continue;
            const std::size_t d = adj[n].size();
            if (d > bestDeg) { bestDeg = d; best = n; }
          }
          if (best == std::numeric_limits<std::size_t>::max()) break;
          path.push_back(best);
          vis.insert(best);
        }

        // Accept path of size ≥ 2. Size-1 paths (= isolated hub) are
        // skipped — a backbone is by definition a chain.
        if (path.size() < 2) {
          available.erase(start);
          continue;
        }
        for (std::size_t r : path) available.erase(r);
        totalBackboneNodes += path.size();
        backbones.push_back(std::move(path));
      }

      // Concatenate all backbone paths into mainRing for backwards
      // compatibility; sum of degrees as overall score.
      std::vector<std::size_t> ordered;
      double score = 0.0;
      for (const auto& bb : backbones) {
        for (std::size_t r : bb) {
          ordered.push_back(r);
          score += static_cast<double>(adj[r].size());
        }
      }
      mainRing = ordered;
      bestRingScore = score;
      mainRingClosed = false;
    }

    std::unordered_set<std::size_t> ringSet;
    if (mainRing.size() >= 2) {
      result.mainRingSize = mainRing.size();
      result.mainRingScore = bestRingScore;
      result.mainRingClosed = mainRingClosed;
      result.mainRingNodeIdxs = mainRing;
      ringSet.insert(mainRing.begin(), mainRing.end());
      std::unordered_map<std::size_t, std::string> rootIdxToCid;
      for (const ClusterRecord& c : result.clusters) {
        rootIdxToCid[c.rootIdx] = c.clusterId;
      }

      // Largest backbone super-node bbox sets row/col spacing.
      double maxBackboneBbox = 0.0;
      for (std::size_t bi : mainRing) {
        auto cidIt = rootIdxToCid.find(bi);
        if (cidIt == rootIdxToCid.end()) continue;
        auto snIt = clusterToSuper.find(cidIt->second);
        if (snIt == clusterToSuper.end()) continue;
        maxBackboneBbox = std::max(maxBackboneBbox,
          std::max(superAttr.width(snIt->second),
                    superAttr.height(snIt->second)));
      }
      const double colSpacing = std::max(800.0, maxBackboneBbox * 1.2);
      const double rowSpacing = std::max(800.0, maxBackboneBbox * 1.4);

      result.mainRingRowOfNode.clear();
      result.mainRingRowOfNode.reserve(mainRing.size());

      if (mainRingClosed) {
        // Closed ring (cycle) → full circle on backboneR (legacy path).
        const std::size_t M = mainRing.size();
        for (std::size_t i = 0; i < M; ++i) {
          auto cidIt = rootIdxToCid.find(mainRing[i]);
          if (cidIt == rootIdxToCid.end()) {
            result.mainRingRowOfNode.push_back(0);
            continue;
          }
          auto snIt = clusterToSuper.find(cidIt->second);
          if (snIt == clusterToSuper.end()) {
            result.mainRingRowOfNode.push_back(0);
            continue;
          }
          const double angle = 2.0 * kPi * static_cast<double>(i)
                              / static_cast<double>(M);
          superAttr.x(snIt->second) = cx + backboneR * std::cos(angle);
          superAttr.y(snIt->second) = cy + backboneR * std::sin(angle);
          result.mainRingRowOfNode.push_back(0);
        }
      } else {
        // Open path backbones placed on a convex-hull (circle) at the
        // average natural FMMM radius, with each member kept at its
        // FMMM-natural angle. Beats both the row layout and the
        // path-order-angle hull on every metric (visualCrossings,
        // edgeCrossings) because:
        //   - radius normalization gives the backbone a clean outline
        //   - keeping each member's FMMM angle preserves the connectivity
        //     patterns FMMM organically discovered, so edges to non-
        //     backbone neighbours remain short.
        // Set DJERD_BACKBONE_ROW=1 to fall back to the original
        // horizontal row layout for comparison.
        const char* rowEnv = std::getenv("DJERD_BACKBONE_ROW");
        const bool useHull = !(rowEnv && std::strcmp(rowEnv, "0") != 0);
        if (useHull) {
          // Use the AVERAGE current FMMM radius of backbone clusters,
          // not perimeterR. perimeterR (~1.35×maxR) pushes backbone far
          // outward from natural FMMM positions, blowing up edge
          // lengths to non-backbone neighbours and exploding segment-
          // segment crossings. Average natural radius keeps backbone
          // close to where FMMM organically placed them, reducing edge
          // displacement.
          double bbSumR = 0.0;
          std::size_t bbCnt = 0;
          for (const auto& bbPath : backbones) {
            for (std::size_t idx : bbPath) {
              auto cidIt = rootIdxToCid.find(idx);
              if (cidIt == rootIdxToCid.end()) continue;
              auto snIt = clusterToSuper.find(cidIt->second);
              if (snIt == clusterToSuper.end()) continue;
              const double dx = superAttr.x(snIt->second) - cx;
              const double dy = superAttr.y(snIt->second) - cy;
              bbSumR += std::sqrt(dx * dx + dy * dy);
              ++bbCnt;
            }
          }
          const double hullR = (bbCnt > 0)
            ? std::max(800.0, bbSumR / static_cast<double>(bbCnt))
            : perimeterR;
          // Use each backbone member's FMMM-natural angle (atan2 of
          // current position) instead of forcing path-order angles.
          // This preserves the angular distribution FMMM organically
          // produced (clusters with many connections to one side stay
          // on that side) and only normalizes the RADIUS — much smaller
          // edge displacement than forcing sequential angles.
          std::size_t globalRowIdx = 0;
          for (std::size_t bi = 0; bi < backbones.size(); ++bi) {
            const auto& path = backbones[bi];
            const std::size_t M = path.size();
            const double ringR = hullR * std::pow(0.7, static_cast<double>(bi));
            for (std::size_t i = 0; i < M; ++i) {
              auto cidIt = rootIdxToCid.find(path[i]);
              if (cidIt == rootIdxToCid.end()) {
                result.mainRingRowOfNode.push_back(globalRowIdx++);
                continue;
              }
              auto snIt = clusterToSuper.find(cidIt->second);
              if (snIt == clusterToSuper.end()) {
                result.mainRingRowOfNode.push_back(globalRowIdx++);
                continue;
              }
              // Natural angle from FMMM position. Fall back to path-
              // order angle only if FMMM placed it at the centroid.
              const double dx = superAttr.x(snIt->second) - cx;
              const double dy = superAttr.y(snIt->second) - cy;
              double angle = (dx * dx + dy * dy > 1e-3)
                ? std::atan2(dy, dx)
                : (2.0 * kPi * static_cast<double>(i) / static_cast<double>(M));
              superAttr.x(snIt->second) = cx + ringR * std::cos(angle);
              superAttr.y(snIt->second) = cy + ringR * std::sin(angle);
              result.mainRingRowOfNode.push_back(globalRowIdx++);
            }
          }
          (void)rowSpacing;
          (void)colSpacing;
        } else {
          // Default: multi-backbone open paths on horizontal rows.
          // Backbone 0 (highest score) is centred at cy; subsequent
          // backbones stack alternating above / below.
          auto rowOffset = [&](std::size_t backboneIdx) -> double {
            if (backboneIdx == 0) return 0.0;
            const std::size_t pair = (backboneIdx + 1) / 2;
            const double sign = (backboneIdx % 2 == 1) ? -1.0 : 1.0;
            return sign * static_cast<double>(pair);
          };

          for (std::size_t bi = 0; bi < backbones.size(); ++bi) {
            const auto& path = backbones[bi];
            const std::size_t M = path.size();
            const double yRel = rowOffset(bi);
            for (std::size_t i = 0; i < M; ++i) {
              auto cidIt = rootIdxToCid.find(path[i]);
              if (cidIt == rootIdxToCid.end()) {
                result.mainRingRowOfNode.push_back(bi);
                continue;
              }
              auto snIt = clusterToSuper.find(cidIt->second);
              if (snIt == clusterToSuper.end()) {
                result.mainRingRowOfNode.push_back(bi);
                continue;
              }
              const double xRel = static_cast<double>(i)
                                  - 0.5 * static_cast<double>(M - 1);
              superAttr.x(snIt->second) = cx + xRel * colSpacing;
              superAttr.y(snIt->second) = cy + yRel * rowSpacing;
              result.mainRingRowOfNode.push_back(bi);
            }
          }
        }
      }
    }

    // Step 1: Compute current angle for each top-tier root.
    // Roots at centroid (FMMM force-balance pulled high-deg ones to center)
    // get a deterministic angle via gap-filling.
    struct TierEntry {
      std::size_t rrIndex;
      double angle;
      bool angleKnown;
    };
    std::vector<TierEntry> top;
    top.reserve(tierTop);
    // Pick the first `tierTop` rrs entries that are NOT main-ring members.
    // Main ring already occupies its perimeter slots, so the remaining top
    // tier picks from non-ring members in rank order.
    for (std::size_t i = 0; i < N && top.size() < tierTop; ++i) {
      if (ringSet.count(rrs[i].rootIdx)) continue;  // already on main ring
      auto it = clusterToSuper.find(rrs[i].clusterId);
      if (it == clusterToSuper.end()) continue;
      const double dx = superAttr.x(it->second) - cx;
      const double dy = superAttr.y(it->second) - cy;
      const double r = std::sqrt(dx * dx + dy * dy);
      TierEntry te;
      te.rrIndex = i;
      if (r > 1e-3) {
        te.angle = std::atan2(dy, dx);
        te.angleKnown = true;
      } else {
        te.angle = 0.0;
        te.angleKnown = false;
      }
      top.push_back(te);
    }

    // Collect known angles, sort.
    std::vector<double> known;
    for (const TierEntry& te : top) if (te.angleKnown) known.push_back(te.angle);
    std::sort(known.begin(), known.end());

    // For each unknown-angle entry, place in largest current gap.
    std::size_t evenIdx = 0;
    for (TierEntry& te : top) {
      if (te.angleKnown) continue;
      if (known.empty()) {
        // No reference — evenly spaced
        te.angle = 2.0 * kPi * static_cast<double>(evenIdx++)
                 / static_cast<double>(top.size());
      } else {
        // Find largest gap in `known` (circular)
        double bestGap = 0.0;
        double bestStart = known[0];
        for (std::size_t k = 0; k < known.size(); ++k) {
          const double next = (k + 1 < known.size())
            ? known[k + 1]
            : known[0] + 2.0 * kPi;
          const double gap = next - known[k];
          if (gap > bestGap) {
            bestGap = gap;
            bestStart = known[k];
          }
        }
        te.angle = bestStart + bestGap * 0.5;
      }
      te.angleKnown = true;
      // Insert sorted to keep `known` ordered
      auto pos = std::lower_bound(known.begin(), known.end(), te.angle);
      known.insert(pos, te.angle);
    }

    // Step 2: Place top tier on perimeter circle at perimeterR.
    for (const TierEntry& te : top) {
      auto it = clusterToSuper.find(rrs[te.rrIndex].clusterId);
      if (it == clusterToSuper.end()) continue;
      ogdf::node sn = it->second;
      superAttr.x(sn) = cx + perimeterR * std::cos(te.angle);
      superAttr.y(sn) = cy + perimeterR * std::sin(te.angle);
    }

    // Step 3: Lower tier — radial scaling factor decreasing from kMidFactor
    // to 1.0 by rank for non-ring non-top entries. Reduced from 1.4 → 1.05
    // so mid-tier stays compact inward, leaving empty space between mid
    // tier and the outermost backbone ring (= backbone prominence).
    constexpr double kMidFactor = 1.05;
    // Build set of rrs indices already placed (main ring members + top tier)
    std::unordered_set<std::size_t> placedIdx;
    for (const TierEntry& te : top) placedIdx.insert(te.rrIndex);
    // Iterate rrs, skip ring members and top tier; apply factor to the rest.
    std::size_t midRank = 0;
    std::size_t midTotal = 0;
    for (std::size_t i = 0; i < N; ++i) {
      if (ringSet.count(rrs[i].rootIdx)) continue;
      if (placedIdx.count(i)) continue;
      ++midTotal;
    }
    for (std::size_t i = 0; i < N; ++i) {
      if (ringSet.count(rrs[i].rootIdx)) continue;
      if (placedIdx.count(i)) continue;
      auto it = clusterToSuper.find(rrs[i].clusterId);
      if (it == clusterToSuper.end()) { ++midRank; continue; }
      ogdf::node sn = it->second;
      const double t = static_cast<double>(midRank)
                     / std::max<double>(1.0, static_cast<double>(midTotal));
      const double factor = kMidFactor - (kMidFactor - 1.0) * t;
      const double dx = superAttr.x(sn) - cx;
      const double dy = superAttr.y(sn) - cy;
      superAttr.x(sn) = cx + dx * factor;
      superAttr.y(sn) = cy + dy * factor;
      ++midRank;
    }
    }  // if cnt >= 2
    }  // if rrs.size() >= 2
  }

  // 9b2. Connector / router untangling pass.
  //
  // A connector C connects exactly 2 cluster roots A,B; ideally A-C-B is a
  // straight line so the polyline doesn't form an unnecessary V-kink. A
  // router R connects 3+ cluster roots; ideally R sits at their centroid
  // so its outgoing edges fan out evenly without crossing each other.
  //
  // Iterative midpoint/centroid pull with damping. After hub repulsion has
  // settled super-node positions, this lightly nudges connectors/routers
  // toward their structurally-correct rest position. Multiple iterations
  // converge under conflicting forces (e.g., two connectors sharing a path).
  //
  // Cluster roots are NOT moved — they're anchored by polar/super-polar
  // structure. Only intermediate hubs (connectors, routers) move.
  {
    // Skip the positioning iterations when positions are overwritten
    // downstream (kIters=0); the ringCount structure below still computes.
    // Iteration count is env-tunable (DJERD_CG_UNTANGLE_ITERS, default 10) so
    // we can trade connector/router untangling quality for multistart speed —
    // 9b2 is the dominant per-run cost (~18s × N seeds on the inheritance
    // super-graph).
    const std::size_t kIters = skipCgPositioning ? 0 : [] {
      const char* e = std::getenv("DJERD_CG_UNTANGLE_ITERS");
      return e ? static_cast<std::size_t>(std::max(0, std::atoi(e))) : std::size_t(10);
    }();
    constexpr double kPullStrength = 0.5;
    for (std::size_t iter = 0; iter < kIters; ++iter) {
      // Connectors → midpoint of their 2 connected cluster roots.
      for (const ConnectorRecord& con : result.connectors) {
        auto cIt = connectorToSuper.find(con.nodeIdx);
        if (cIt == connectorToSuper.end()) continue;
        ogdf::node cn = cIt->second;
        std::vector<std::pair<double, double>> rp;
        rp.reserve(con.connectedClusterIds.size());
        for (const std::string& cid : con.connectedClusterIds) {
          auto sa = clusterToSuper.find(cid);
          if (sa == clusterToSuper.end()) continue;
          rp.emplace_back(superAttr.x(sa->second), superAttr.y(sa->second));
        }
        if (rp.size() != 2) continue;
        const double tx = (rp[0].first + rp[1].first) * 0.5;
        const double ty = (rp[0].second + rp[1].second) * 0.5;
        superAttr.x(cn) += (tx - superAttr.x(cn)) * kPullStrength;
        superAttr.y(cn) += (ty - superAttr.y(cn)) * kPullStrength;
      }
      // Routers → centroid of their connected cluster roots.
      for (const RouterRecord& rtr : result.routers) {
        auto rIt = routerToSuper.find(rtr.nodeIdx);
        if (rIt == routerToSuper.end()) continue;
        ogdf::node rn = rIt->second;
        double sx = 0.0, sy = 0.0;
        std::size_t n = 0;
        for (const std::string& cid : rtr.connectedClusterIds) {
          auto sa = clusterToSuper.find(cid);
          if (sa == clusterToSuper.end()) continue;
          sx += superAttr.x(sa->second);
          sy += superAttr.y(sa->second);
          ++n;
        }
        if (n == 0) continue;
        const double tx = sx / static_cast<double>(n);
        const double ty = sy / static_cast<double>(n);
        superAttr.x(rn) += (tx - superAttr.x(rn)) * kPullStrength;
        superAttr.y(rn) += (ty - superAttr.y(rn)) * kPullStrength;
      }
    }

    // Push connectors/routers off any cluster super-node they ended up
    // overlapping (the midpoint may pass through a third cluster). Single
    // pass: for each connector/router, check overlap with cluster super-
    // nodes and push perpendicular to the connector's edge axis.
    auto pushOffClusters = [&](ogdf::node cn,
                                double axisX, double axisY) {
      if (cn == nullptr) return;
      const double cw = superAttr.width(cn) / 2.0;
      const double ch = superAttr.height(cn) / 2.0;
      const double axisLen = std::sqrt(axisX * axisX + axisY * axisY);
      if (axisLen < 1e-3) return;
      const double perpX = -axisY / axisLen;
      const double perpY = axisX / axisLen;
      for (const ClusterRecord& c : result.clusters) {
        auto sa = clusterToSuper.find(c.clusterId);
        if (sa == clusterToSuper.end()) continue;
        ogdf::node clusN = sa->second;
        const double dx = superAttr.x(cn) - superAttr.x(clusN);
        const double dy = superAttr.y(cn) - superAttr.y(clusN);
        const double rw = superAttr.width(clusN) / 2.0;
        const double rh = superAttr.height(clusN) / 2.0;
        const double reqX = cw + rw + 12.0;
        const double reqY = ch + rh + 12.0;
        if (std::abs(dx) >= reqX || std::abs(dy) >= reqY) continue;
        // Inside cluster bbox — push along perpendicular to edge axis.
        const double overX = reqX - std::abs(dx);
        const double overY = reqY - std::abs(dy);
        const double over = std::min(overX, overY);
        const double sign = (dx * perpX + dy * perpY) >= 0.0 ? 1.0 : -1.0;
        superAttr.x(cn) += sign * perpX * over;
        superAttr.y(cn) += sign * perpY * over;
      }
    };
    for (const ConnectorRecord& con : result.connectors) {
      auto cIt = connectorToSuper.find(con.nodeIdx);
      if (cIt == connectorToSuper.end()) continue;
      std::vector<std::pair<double, double>> rp;
      for (const std::string& cid : con.connectedClusterIds) {
        auto sa = clusterToSuper.find(cid);
        if (sa == clusterToSuper.end()) continue;
        rp.emplace_back(superAttr.x(sa->second), superAttr.y(sa->second));
      }
      if (rp.size() != 2) continue;
      pushOffClusters(cIt->second, rp[1].first - rp[0].first,
                                    rp[1].second - rp[0].second);
    }
  }

  // 9b3. Local cross-min swap pass (graph-terminology.md §3.8).
  //
  // Iteratively detect crossing super-edge pairs and swap an endpoint of
  // one with an endpoint of the other if it reduces local crossings.
  // Resolves cases like K4-minus-edge + chain dedup adds back the missing
  // edge such that it crosses the central deg3↔deg3 edge — the planar
  // embedding exists, but FMMM/perimeter-shaping didn't pick it.
  //
  // Constrained to CLUSTER super-nodes only (not connectors/routers/indep)
  // to preserve hub alignment from connector untangling.
  {
    // Backbone members are pinned by §3.10 spine placement; do NOT swap
    // them in cross-min — that would break the visible spine.
    std::unordered_set<std::size_t> backboneSet9b3(
      result.mainRingNodeIdxs.begin(), result.mainRingNodeIdxs.end());
    std::unordered_set<ogdf::node> clusterSnSet;
    for (const ClusterRecord& c : result.clusters) {
      if (backboneSet9b3.count(c.rootIdx)) continue;
      auto it = clusterToSuper.find(c.clusterId);
      if (it != clusterToSuper.end()) clusterSnSet.insert(it->second);
    }

    std::vector<std::pair<ogdf::node, ogdf::node>> sEdges;
    for (ogdf::edge e : super.edges) sEdges.emplace_back(e->source(), e->target());
    std::unordered_map<ogdf::node, std::vector<std::size_t>> incidentE;
    for (std::size_t i = 0; i < sEdges.size(); ++i) {
      incidentE[sEdges[i].first].push_back(i);
      incidentE[sEdges[i].second].push_back(i);
    }

    auto segCross = [](double ax, double ay, double bx, double by,
                       double cx, double cy, double dx, double dy) -> bool {
      auto ccw = [](double Ax, double Ay, double Bx, double By,
                    double Cx, double Cy) -> double {
        return (Cy - Ay) * (Bx - Ax) - (By - Ay) * (Cx - Ax);
      };
      const double d1 = ccw(cx, cy, dx, dy, ax, ay);
      const double d2 = ccw(cx, cy, dx, dy, bx, by);
      const double d3 = ccw(ax, ay, bx, by, cx, cy);
      const double d4 = ccw(ax, ay, bx, by, dx, dy);
      return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
             ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
    };

    auto incidentCross = [&](ogdf::node n) -> int {
      int total = 0;
      auto it = incidentE.find(n);
      if (it == incidentE.end()) return 0;
      for (std::size_t i : it->second) {
        const ogdf::node a = sEdges[i].first;
        const ogdf::node b = sEdges[i].second;
        const double ax = superAttr.x(a), ay = superAttr.y(a);
        const double bx = superAttr.x(b), by = superAttr.y(b);
        for (std::size_t j = 0; j < sEdges.size(); ++j) {
          if (i == j) continue;
          const ogdf::node u = sEdges[j].first;
          const ogdf::node v = sEdges[j].second;
          if (a == u || a == v || b == u || b == v) continue;
          const double ux = superAttr.x(u), uy = superAttr.y(u);
          const double vx = superAttr.x(v), vy = superAttr.y(v);
          if (segCross(ax, ay, bx, by, ux, uy, vx, vy)) ++total;
        }
      }
      return total;
    };

    auto swapPos = [&](ogdf::node n1, ogdf::node n2) {
      const double tx = superAttr.x(n1);
      const double ty = superAttr.y(n1);
      superAttr.x(n1) = superAttr.x(n2);
      superAttr.y(n1) = superAttr.y(n2);
      superAttr.x(n2) = tx;
      superAttr.y(n2) = ty;
    };

    // env-tunable (DJERD_CG_SWAP_ITERS, default 4); the swap pass's
    // incidentCross is O(super-edges), so on the dense inheritance super-graph
    // this loop scales ~O(E²) and is a prime multistart-cost suspect.
    const int kMaxIter = skipCgPositioning ? 0 : [] {
      const char* e = std::getenv("DJERD_CG_SWAP_ITERS");
      return e ? std::max(0, std::atoi(e)) : 4;
    }();
    constexpr int kMaxSwapsPerIter = 80;
    for (int iter = 0; iter < kMaxIter; ++iter) {
      int swaps = 0;
      bool anyImproved = false;
      for (std::size_t i = 0; i < sEdges.size() && swaps < kMaxSwapsPerIter; ++i) {
        const ogdf::node a = sEdges[i].first;
        const ogdf::node b = sEdges[i].second;
        const double ax = superAttr.x(a), ay = superAttr.y(a);
        const double bx = superAttr.x(b), by = superAttr.y(b);
        for (std::size_t j = i + 1; j < sEdges.size() && swaps < kMaxSwapsPerIter; ++j) {
          const ogdf::node u = sEdges[j].first;
          const ogdf::node v = sEdges[j].second;
          if (a == u || a == v || b == u || b == v) continue;
          const double ux = superAttr.x(u), uy = superAttr.y(u);
          const double vx = superAttr.x(v), vy = superAttr.y(v);
          if (!segCross(ax, ay, bx, by, ux, uy, vx, vy)) continue;

          // Try the 4 endpoint-swap candidates; pick the one with the
          // largest reduction in local crossings.
          ogdf::node candA[4] = {a, a, b, b};
          ogdf::node candB[4] = {u, v, u, v};
          int bestSave = 0;
          int bestK = -1;
          for (int k = 0; k < 4; ++k) {
            if (!clusterSnSet.count(candA[k]) || !clusterSnSet.count(candB[k])) continue;
            const int oldCnt = incidentCross(candA[k]) + incidentCross(candB[k]);
            swapPos(candA[k], candB[k]);
            const int newCnt = incidentCross(candA[k]) + incidentCross(candB[k]);
            swapPos(candA[k], candB[k]);  // revert (involutive)
            const int save = oldCnt - newCnt;
            if (save > bestSave) { bestSave = save; bestK = k; }
          }
          if (bestK >= 0) {
            swapPos(candA[bestK], candB[bestK]);
            ++swaps;
            anyImproved = true;
            break;  // restart i scan (positions changed)
          }
        }
      }
      if (!anyImproved) break;
    }
  }

  cgCheckpoint("post-9b2-untangle-swap");
  // 9c. Ring detection (length-3 triangles in the top-level super-graph).
  // Pure topological count — no position modifications. Reported via
  // metadata for downstream visualisation (e.g., highlight ring nodes).
  // Active interior-emptying push-out was tried and rolled back: on this
  // dataset it spiked crossings 30%+ because pushing non-ring nodes out of
  // triangle interiors broke FMMM-laid edge bundles. A stable variant
  // would need a force-balanced re-layout pass, not a one-shot push.
  // result.ringCount is unused (no reader in the C++/TS/webview — only a log
  // label), and this O(E·d) triangle count is a large chunk of every
  // multistart run on the dense inheritance super-graph. Skip it entirely.
  // (Flip to true to restore the metadata.)
  constexpr bool kComputeRingCount = false;
  if (kComputeRingCount) {
    std::unordered_map<ogdf::node, std::vector<ogdf::node>> sAdj;
    for (ogdf::node v : super.nodes) sAdj[v] = {};
    for (ogdf::edge e : super.edges) {
      sAdj[e->source()].push_back(e->target());
      sAdj[e->target()].push_back(e->source());
    }
    for (auto& [_, vec] : sAdj) std::sort(vec.begin(), vec.end());

    std::size_t triangleCount = 0;
    for (ogdf::edge e : super.edges) {
      ogdf::node a = e->source();
      ogdf::node b = e->target();
      const auto& na = sAdj[a];
      const auto& nb = sAdj[b];
      std::size_t i = 0, j = 0;
      while (i < na.size() && j < nb.size()) {
        if (na[i] == nb[j]) {
          ogdf::node c = na[i];
          if (c != a && c != b && a < b && b < c) {
            ++triangleCount;
          }
          ++i; ++j;
        } else if (na[i] < nb[j]) ++i;
        else ++j;
      }
    }
    result.ringCount = triangleCount;
  }

  cgCheckpoint("pre-§10-outward-radial");
  // 10. Compute outward angle per cluster from super-graph layout, then run
  // outward-aware inner radial. Each cluster's bridges face the outward
  // direction; leaves face the opposite (inward) side.
  //
  // §3.10 Backbone OUTWARD override: for cluster roots on the main backbone
  // (= main ring/path on perimeter), set thetaOutward = direction toward
  // LAYOUT CENTER. Inner radial places bridges at thetaOutward and leaves
  // at thetaOutward + π — so backbone leaves face RADIALLY OUTWARD from
  // layout center (away from interior).
  std::unordered_set<std::size_t> backboneSet(result.mainRingNodeIdxs.begin(),
                                               result.mainRingNodeIdxs.end());
  double layoutCx = 0.0, layoutCy = 0.0;
  std::size_t layoutCn = 0;
  for (const auto& kv : clusterToSuper) {
    layoutCx += superAttr.x(kv.second);
    layoutCy += superAttr.y(kv.second);
    ++layoutCn;
  }
  if (layoutCn > 0) { layoutCx /= layoutCn; layoutCy /= layoutCn; }

  // Map clusterId -> rootIdx for backbone lookup
  std::unordered_map<std::string, std::size_t> cidToRootIdx;
  for (const ClusterRecord& cr : result.clusters) cidToRootIdx[cr.clusterId] = cr.rootIdx;

  std::unordered_map<std::string, double> outwardByCluster;
  for (const auto& [cid, sn] : clusterToSuper) {
    const double cx = superAttr.x(sn);
    const double cy = superAttr.y(sn);
    double theta = 0.0;
    auto rIt = cidToRootIdx.find(cid);
    const bool isBackbone = rIt != cidToRootIdx.end() && backboneSet.count(rIt->second);

    const double rdx = cx - layoutCx;
    const double rdy = cy - layoutCy;
    const bool radiallyValid = (std::abs(rdx) > 1e-3 || std::abs(rdy) > 1e-3);

    if (isBackbone && radiallyValid) {
      theta = std::atan2(-rdy, -rdx);
    } else if (radiallyValid) {
      theta = std::atan2(rdy, rdx);
    } else {
      double sumX = 0.0, sumY = 0.0;
      int neighbourCount = 0;
      for (ogdf::adjEntry adj : sn->adjEntries) {
        ogdf::node other = adj->twinNode();
        sumX += superAttr.x(other);
        sumY += superAttr.y(other);
        ++neighbourCount;
      }
      if (neighbourCount > 0) {
        const double avgX = sumX / static_cast<double>(neighbourCount);
        const double avgY = sumY / static_cast<double>(neighbourCount);
        const double dx = avgX - cx;
        const double dy = avgY - cy;
        if (std::abs(dx) > 1e-3 || std::abs(dy) > 1e-3) {
          theta = std::atan2(dy, dx);
        }
      }
    }
    outwardByCluster[cid] = theta;
  }

  for (ClusterRecord& cluster : result.clusters) {
    if (bubbleMode) {
      // Bubble mode: full 360° concentric ring fill around root.
      cluster.localSize = innerBubbleFill(cluster, nodes, attributes);
    } else {
      const double theta = outwardByCluster[cluster.clusterId];
      cluster.localSize = innerRadialWithOutward(cluster, nodes, theta, attributes);
    }
  }

  cgCheckpoint("pre-§11-compose");
  // 11. Compose: cluster centre + member local; connector / independent direct.
  for (const ClusterRecord& c : result.clusters) {
    auto it = clusterToSuper.find(c.clusterId);
    if (it == clusterToSuper.end()) continue;
    const double cx = superAttr.x(it->second);
    const double cy = superAttr.y(it->second);
    for (const ClusterMemberInfo& m : c.members) {
      const NodeRecord& nd = nodes[m.nodeIdx];
      attributes.x(nd.handle) = std::round((attributes.x(nd.handle) + cx) * 100.0) / 100.0;
      attributes.y(nd.handle) = std::round((attributes.y(nd.handle) + cy) * 100.0) / 100.0;
    }
  }
  for (const ConnectorRecord& con : result.connectors) {
    auto it = connectorToSuper.find(con.nodeIdx);
    if (it == connectorToSuper.end()) continue;
    const NodeRecord& nd = nodes[con.nodeIdx];
    attributes.x(nd.handle) = std::round(superAttr.x(it->second) * 100.0) / 100.0;
    attributes.y(nd.handle) = std::round(superAttr.y(it->second) * 100.0) / 100.0;
  }
  for (const RouterRecord& rtr : result.routers) {
    auto it = routerToSuper.find(rtr.nodeIdx);
    if (it == routerToSuper.end()) continue;
    const NodeRecord& nd = nodes[rtr.nodeIdx];
    attributes.x(nd.handle) = std::round(superAttr.x(it->second) * 100.0) / 100.0;
    attributes.y(nd.handle) = std::round(superAttr.y(it->second) * 100.0) / 100.0;
  }
  for (std::size_t idx : result.independentNodeIndices) {
    auto it = indepToSuper.find(idx);
    if (it == indepToSuper.end()) continue;
    const NodeRecord& nd = nodes[idx];
    attributes.x(nd.handle) = std::round(superAttr.x(it->second) * 100.0) / 100.0;
    attributes.y(nd.handle) = std::round(superAttr.y(it->second) * 100.0) / 100.0;
  }

  // 11b. Alone-root grid (graph-terminology.md §2.5.1).
  //
  // Place all alone-roots (deg-0 isolated nodes) in a compact grid OUTSIDE
  // the main layout bbox. Keeps them visible and grouped without bloating
  // the main layout's bbox via FMMM's component-arrangement spread.
  if (!aloneRootIndices.empty()) {
    double minX = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    bool any = false;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      if (aloneRootSet.count(i)) continue;
      const double x = attributes.x(nodes[i].handle);
      const double y = attributes.y(nodes[i].handle);
      if (x < minX) minX = x;
      if (x > maxX) maxX = x;
      if (y < minY) minY = y;
      if (y > maxY) maxY = y;
      any = true;
    }
    if (!any) { minX = maxX = minY = maxY = 0.0; }

    double maxW = 0.0, maxH = 0.0;
    for (std::size_t idx : aloneRootIndices) {
      maxW = std::max(maxW, nodes[idx].width);
      maxH = std::max(maxH, nodes[idx].height);
    }
    const double cellW = maxW + 12.0;
    const double cellH = maxH + 12.0;
    const std::size_t N = aloneRootIndices.size();
    // Square-aspect grid in real units: cols × cellW ≈ rows × cellH.
    const std::size_t cols = std::max<std::size_t>(1,
      static_cast<std::size_t>(std::ceil(
        std::sqrt(static_cast<double>(N) * cellH / cellW))));
    const std::size_t rows = (N + cols - 1) / cols;
    const double gridW = static_cast<double>(cols) * cellW;
    const double gridH = static_cast<double>(rows) * cellH;
    // Place alone-root grid at the LAYOUT CENTROID (inside the main bbox)
    // instead of beside it. Perimeter shaping pushes high-deg cluster roots
    // outward, so the centroid is the most empty region. This means
    // alone-roots add ZERO extension to the bbox. Alone-roots have no edges
    // so visual overlap with the few low-rank clusters that may be near
    // the centroid is acceptable (no edge clutter).
    const double cx = (minX + maxX) * 0.5;
    const double cy = (minY + maxY) * 0.5;
    const double startX = cx - gridW * 0.5;
    const double startY = cy - gridH * 0.5;
    // Sort by nodeIdx for determinism.
    std::sort(aloneRootIndices.begin(), aloneRootIndices.end());
    for (std::size_t k = 0; k < N; ++k) {
      const std::size_t row = k / cols;
      const std::size_t col = k % cols;
      const NodeRecord& nd = nodes[aloneRootIndices[k]];
      attributes.x(nd.handle) = std::round((startX + static_cast<double>(col) * cellW) * 100.0) / 100.0;
      attributes.y(nd.handle) = std::round((startY + static_cast<double>(row) * cellH) * 100.0) / 100.0;
    }
  }

  // 11c. Bus alignment for bridges (graph-terminology.md §3.7.5).
  // Skipped in bubble mode — bubble has no directionalised arcs; bridges
  // are scattered around root in concentric rings like other members.
  if (!bubbleMode) {
  //
  // Inner radial places all bridges of a cluster on the same outward 180°
  // arc averaged across destinations. But each bridge has a SPECIFIC
  // external destination — bridges with the same destination form a "bus"
  // that should bundle toward that direction.
  //
  // Group bridges by external root, place each group on a small arc
  // pointing at that destination's actual position. Reduces edge bend
  // and cross since bridges align with their target direction.
  //
  // Run AFTER compose (positions are absolute) but BEFORE pruning
  // re-attach (so re-attach uses bus-aligned bridge positions as parents).
  {
    const double busPi = 3.14159265358979;
    for (const ClusterRecord& c : result.clusters) {
      // Group bridges by their external root
      std::unordered_map<std::size_t, std::vector<std::size_t>> groups;
      for (const ClusterMemberInfo& m : c.members) {
        if (m.category != MemberCategory::Bridge) continue;
        for (std::size_t j : adj[m.nodeIdx]) {
          auto rIt = rootToCluster.find(j);
          if (rIt == rootToCluster.end()) continue;
          if (j == c.rootIdx) continue;
          groups[j].push_back(m.nodeIdx);
          break;  // bridge has exactly one external root by classification
        }
      }
      if (groups.empty()) continue;

      const double cxC = attributes.x(nodes[c.rootIdx].handle);
      const double cyC = attributes.y(nodes[c.rootIdx].handle);

      for (auto& kv : groups) {
        const std::size_t extRoot = kv.first;
        std::vector<std::size_t>& bridgeIdxs = kv.second;
        std::sort(bridgeIdxs.begin(), bridgeIdxs.end());

        const double exC = attributes.x(nodes[extRoot].handle);
        const double eyC = attributes.y(nodes[extRoot].handle);
        const double ddx = exC - cxC;
        const double ddy = eyC - cyC;
        if (ddx * ddx + ddy * ddy < 1e-3) continue;
        const double busAngle = std::atan2(ddy, ddx);

        // Preserve max radial distance among these bridges (= keep them
        // on the same shell as inner radial put them).
        double maxR = 0.0;
        for (std::size_t idx : bridgeIdxs) {
          const double bdx = attributes.x(nodes[idx].handle) - cxC;
          const double bdy = attributes.y(nodes[idx].handle) - cyC;
          maxR = std::max(maxR, std::sqrt(bdx * bdx + bdy * bdy));
        }
        if (maxR < 1e-3) continue;

        const std::size_t M = bridgeIdxs.size();
        if (M == 1) {
          attributes.x(nodes[bridgeIdxs[0]].handle) =
            std::round((cxC + maxR * std::cos(busAngle)) * 100.0) / 100.0;
          attributes.y(nodes[bridgeIdxs[0]].handle) =
            std::round((cyC + maxR * std::sin(busAngle)) * 100.0) / 100.0;
        } else {
          // Spread bridges in narrow arc (max 60°) at the bus direction.
          const double arcSize = std::min(busPi / 3.0,
            (busPi / 12.0) * static_cast<double>(M - 1));
          const double step = arcSize / static_cast<double>(M - 1);
          for (std::size_t k = 0; k < M; ++k) {
            const double theta = busAngle - arcSize / 2.0
                               + step * static_cast<double>(k);
            attributes.x(nodes[bridgeIdxs[k]].handle) =
              std::round((cxC + maxR * std::cos(theta)) * 100.0) / 100.0;
            attributes.y(nodes[bridgeIdxs[k]].handle) =
              std::round((cyC + maxR * std::sin(theta)) * 100.0) / 100.0;
          }
        }
      }
    }
  }
  }  // if (!bubbleMode) — bus alignment

  cgCheckpoint("pre-§12-pruning-reattach");
  // 12. Pruning re-attach — face-aware tree growth (§2.5.4).
  //
  // Pruning leaves a 2-core "mesh" of rings/polyhedra; trees that were
  // pruned attach to mesh nodes. We place each tree INTO THE FACES of the
  // mesh (= the empty regions adjacent to a mesh node).
  //
  // Algorithm: process pruned nodes in REVERSE level order (deepest first).
  // For each parent, find the LARGEST EMPTY ANGULAR ARC among its currently-
  // placed neighbours (excluding the children we're about to place). That
  // arc points into a face — that's where the tree grows. Tightly chord-fit
  // children into the arc.
  //
  // This is uniform across all pruned levels (1, 2, 3, ...) — trees stack
  // outward from the mesh, packing tightly because tree structure stays
  // recognizable even at small spacing.
  if (!result.prunedNodes.empty()) {
    std::map<std::size_t, std::vector<const PrunedNodeRecord*>> byLevel;
    for (const PrunedNodeRecord& p : result.prunedNodes) byLevel[p.level].push_back(&p);

    // §3.10 Backbone outward override (re-attach): if parent is on main
    // backbone, place wings/leaves RADIALLY OUTWARD from the layout center.
    // Compute layout centroid from cluster super-node final positions.
    std::unordered_set<std::size_t> reattachBackboneSet(
      result.mainRingNodeIdxs.begin(), result.mainRingNodeIdxs.end());
    double reLayoutCx = 0.0, reLayoutCy = 0.0;
    {
      std::size_t cn = 0;
      for (const ClusterRecord& c : result.clusters) {
        reLayoutCx += attributes.x(nodes[c.rootIdx].handle);
        reLayoutCy += attributes.y(nodes[c.rootIdx].handle);
        ++cn;
      }
      if (cn > 0) { reLayoutCx /= cn; reLayoutCy /= cn; }
    }

    // Bubble-mode pruned-skip set: every node listed as a cluster member
    // (any level, any category) is already placed by innerBubbleFill on the
    // 360° concentric rings inside its bubble. Re-attaching it via the
    // narrow face arc would scrape it back out of the bubble and waste the
    // area we just allocated (graph-terminology.md §11: bubble must be
    // visually prominent). Cross-cluster pruned nodes (e.g., demoted
    // singletons attached to a foreign root) still reattach normally.
    std::unordered_set<std::size_t> bubbleClusterMemberSet;
    if (bubbleMode) {
      for (const ClusterRecord& c : result.clusters) {
        for (const ClusterMemberInfo& m : c.members) {
          bubbleClusterMemberSet.insert(m.nodeIdx);
        }
      }
    }

    // Per-cluster-root outward angle, mirroring the §10 outward-aware
    // inner-radial direction. Used by the hub-leaf override below to
    // place leaves on a 180° INWARD arc (= opposite of bridges/connectors)
    // so the outward hemisphere stays clear for backbone-direction edges
    // (graph-terminology.md §3.2 & §7 edge repulsion principle).
    std::unordered_map<std::size_t, double> rootOutward;
    {
      std::unordered_map<std::string, std::size_t> cidToRootR;
      for (const ClusterRecord& c : result.clusters) cidToRootR[c.clusterId] = c.rootIdx;
      for (const auto& kv : outwardByCluster) {
        auto it = cidToRootR.find(kv.first);
        if (it != cidToRootR.end()) rootOutward[it->second] = kv.second;
      }
    }

    const double pi = 3.14159265358979;
    for (auto rIt = byLevel.rbegin(); rIt != byLevel.rend(); ++rIt) {
      // Group by parent for this level.
      std::unordered_map<std::size_t, std::vector<std::size_t>> byParent;
      for (const PrunedNodeRecord* p : rIt->second) {
        if (p->isAloneRoot) continue;  // alone-roots → grid (11b)
        // NOTE: do NOT filter on nodeHasParallelEdges here. The parallel-edge
        // rule applies to classifyMember (cluster-internal Leaf/Internal/
        // Bridge category for inner radial placement), not to topological
        // leaves (= pruned children). User wants topological leaves merged
        // into matrix bundles regardless of parallel-edge classification.
        // NOTE: bubble mode also goes through matrix override — user wants
        // leaf bundling applied in bubble mode too (matrix overrides bubble's
        // 360° placement for pruned leaves).
        byParent[p->parentIdx].push_back(p->nodeIdx);
      }

      for (auto& kv : byParent) {
        const std::size_t parentIdx = kv.first;
        std::vector<std::size_t>& children = kv.second;
        std::sort(children.begin(), children.end());  // determinism
        const std::size_t M = children.size();

        const NodeRecord& parentNode = nodes[parentIdx];
        const double px = attributes.x(parentNode.handle);
        const double py = attributes.y(parentNode.handle);
        const double pHalf = std::max(parentNode.width, parentNode.height) / 2.0;

        double maxChildW = 0.0, maxChildH = 0.0;
        for (std::size_t c : children) {
          maxChildW = std::max(maxChildW, nodes[c].width);
          maxChildH = std::max(maxChildH, nodes[c].height);
        }
        const double minR = pHalf + std::max(maxChildW, maxChildH) / 2.0 + 8.0;

        double centerAngle = 0.0;
        double arcWidth = 2.0 * pi;
        bool useBackboneOutward = false;
        if (reattachBackboneSet.count(parentIdx)) {
          const double bdx = px - reLayoutCx;
          const double bdy = py - reLayoutCy;
          if (bdx * bdx + bdy * bdy > 1e-3) {
            centerAngle = std::atan2(bdy, bdx);   // radial outward
            arcWidth = pi;                         // 180° outward semicircle
            useBackboneOutward = true;
          }
        }

        if (!useBackboneOutward) {
          // Largest empty arc among parent's other (non-child) neighbours.
          std::unordered_set<std::size_t> childSet(children.begin(), children.end());
          std::vector<double> nbrAngles;
          for (std::size_t j : adj[parentIdx]) {
            if (childSet.count(j)) continue;
            const double jx = attributes.x(nodes[j].handle);
            const double jy = attributes.y(nodes[j].handle);
            const double dx = jx - px;
            const double dy = jy - py;
            if (std::abs(dx) < 1e-3 && std::abs(dy) < 1e-3) continue;
            nbrAngles.push_back(std::atan2(dy, dx));
          }
          std::sort(nbrAngles.begin(), nbrAngles.end());

          if (nbrAngles.size() == 1) {
            centerAngle = nbrAngles[0] + pi;
            arcWidth = 2.0 * pi - pi / 12.0;
          } else if (nbrAngles.size() >= 2) {
            std::vector<double> wrapped = nbrAngles;
            wrapped.push_back(nbrAngles[0] + 2.0 * pi);
            double bestGap = 0.0;
            double bestStart = 0.0;
            for (std::size_t i = 0; i + 1 < wrapped.size(); ++i) {
              const double gap = wrapped[i + 1] - wrapped[i];
              if (gap > bestGap) {
                bestGap = gap;
                bestStart = wrapped[i];
              }
            }
            centerAngle = bestStart + bestGap / 2.0;
            arcWidth = bestGap;
          }
        }
        // Cap arc so trees stay tight even when face is huge (max 120°).
        // BUT for hub parents (high-deg root with many pruned children) the
        // largest-empty-arc collapses to a sliver (root surrounded by edges
        // in every direction), forcing leaves to stack radially far from
        // root. Override: hub leaves go on a 180° INWARD arc (opposite of
        // bridges/connectors per §3.2 & §7), packed in concentric rings.
        // The outward hemisphere stays clear so backbone-direction edges
        // (root→external roots) don't tangle with leaf bundle, and the
        // outward 180° arc occupied by bridges/connectors stays uncluttered
        // (user feedback 2026-04-28: backbone needs angular room).
        const std::size_t parentDegHC = (parentIdx < adj.size()) ? adj[parentIdx].size() : 0;
        const bool isHubParent = (parentDegHC >= 20 && M >= 5);
        double useArc;
        if (isHubParent && !useBackboneOutward) {
          auto outwardIt = rootOutward.find(parentIdx);
          if (outwardIt != rootOutward.end()) {
            centerAngle = outwardIt->second + pi;     // inward = away from outward
          } else {
            // Fallback: use radial-inward (toward layout center)
            const double bdx = px - reLayoutCx;
            const double bdy = py - reLayoutCy;
            if (bdx * bdx + bdy * bdy > 1e-3) {
              centerAngle = std::atan2(-bdy, -bdx);
            }
          }
          useArc = pi;                                  // 180° hemisphere
        } else {
          useArc = std::min(arcWidth * 0.9, 2.0 * pi / 3.0);
        }

        // Matrix placement for high-leaf-count parents: when a cluster
        // root has many children (M ≥ kMatrixThreshold), tiling them as
        // a rectangular grid is more efficient than concentric rings.
        // The matrix sits at offsetDist from the root in centerAngle
        // direction; cells are leaf-sized + small pad. All leaves share
        // the rectangular footprint, providing a tight visual block
        // suitable for downstream edge bundling (single anchor port at
        // the matrix face nearest the root).
        // Matrix bundle for ≥5 leaves under one parent. Smaller groups (2-4
        // leaves) are placed by the radial fallback to avoid layout side
        // effects (small bundles overlapping with neighbours, super-graph
        // swap pass behaving worse). User accepted threshold=5 trade-off.
        constexpr std::size_t kMatrixThreshold = 5;
        const bool useMatrix = (M >= kMatrixThreshold);

        // Override centerAngle so the matrix faces the LEAVES' COLLECTIVE
        // EXTERNAL CONNECTION direction. Default OFF: tested -2.4%
        // edgeCross (carrier) but +28% eni and +5 bndlN cascade.
        // Set DJERD_MATRIX_FACING=1 to enable.
        if (useMatrix) {
          const char* mfEnv = std::getenv("DJERD_MATRIX_FACING");
          const bool skipMf = !(mfEnv && std::strcmp(mfEnv, "0") != 0);
          if (!skipMf) {
            // Compute average external-target position of all leaves.
            double extX = 0.0, extY = 0.0;
            std::size_t extCnt = 0;
            for (std::size_t c : children) {
              for (std::size_t j : adj[c]) {
                if (j == parentIdx) continue;
                // Take any non-parent neighbor's position as a proxy
                // for the leaf's external connection direction.
                extX += attributes.x(nodes[j].handle);
                extY += attributes.y(nodes[j].handle);
                ++extCnt;
              }
            }
            if (extCnt > 0) {
              const double tx = extX / static_cast<double>(extCnt) - px;
              const double ty = extY / static_cast<double>(extCnt) - py;
              if (tx * tx + ty * ty > 1.0) {
                centerAngle = std::atan2(ty, tx);
              }
            }
          }
        }

        if (M == 1) {
          const NodeRecord& child = nodes[children[0]];
          attributes.x(child.handle) = std::round((px + minR * std::cos(centerAngle)) * 100.0) / 100.0;
          attributes.y(child.handle) = std::round((py + minR * std::sin(centerAngle)) * 100.0) / 100.0;
        } else if (useMatrix) {
          // Cell size = max child bbox + 4 px pad
          const double cellW = maxChildW + 4.0;
          const double cellH = maxChildH + 4.0;
          // Aspect ratio: prefer rows×cols ≈ √M square, but bias slightly
          // toward more cols (= matrix wider than tall) when leaves are
          // landscape-shaped (cellW > cellH).
          const std::size_t cols = std::max<std::size_t>(1,
            static_cast<std::size_t>(std::ceil(
              std::sqrt(static_cast<double>(M)
                         * std::max(1.0, cellH / std::max(1.0, cellW))))));
          const std::size_t rows = (M + cols - 1) / cols;
          const double matrixW = static_cast<double>(cols) * cellW;
          const double matrixH = static_cast<double>(rows) * cellH;
          // Distance from root center to matrix center: enough so root's
          // bbox edge and matrix's near edge don't touch.
          const double offsetDist = pHalf + matrixH / 2.0 + 16.0;
          const double matrixCx = px + offsetDist * std::cos(centerAngle);
          const double matrixCy = py + offsetDist * std::sin(centerAngle);
          // Tile children left-to-right, top-to-bottom (sorted by nodeIdx
          // for determinism).
          for (std::size_t i = 0; i < M; ++i) {
            const std::size_t col = i % cols;
            const std::size_t row = i / cols;
            const double cellX = matrixCx
              + (static_cast<double>(col) - 0.5 * static_cast<double>(cols - 1))
                * cellW;
            const double cellY = matrixCy
              + (static_cast<double>(row) - 0.5 * static_cast<double>(rows - 1))
                * cellH;
            const NodeRecord& child = nodes[children[i]];
            attributes.x(child.handle) = std::round(cellX * 100.0) / 100.0;
            attributes.y(child.handle) = std::round(cellY * 100.0) / 100.0;
          }
          // Anchor port: midpoint of matrix's near edge (= face toward
          // root). Used downstream for edge bundle routing.
          ClusterGraphResult::LeafMatrixGroup group;
          group.parentIdx = parentIdx;
          group.leafIdxs.assign(children.begin(), children.end());
          group.sharedRootIdxs.push_back(parentIdx);  // single-root leaf bundle
          group.anchorX = matrixCx - (matrixH / 2.0) * std::cos(centerAngle);
          group.anchorY = matrixCy - (matrixH / 2.0) * std::sin(centerAngle);
          result.leafMatrixGroups.push_back(std::move(group));
          (void)matrixW;
        } else {
          // Multi-ring concentric arc fallback for small M.
          const double slotChord = maxChildW + 4.0;      // tight: 4-px chord pad
          const double radialStep = maxChildH + 8.0;      // ring-to-ring radial step
          std::size_t placed = 0;
          double R = minR;
          while (placed < M) {
            // Capacity at this radius: arc length / chord per child.
            const double arcLen = useArc * R;
            std::size_t cap = std::max<std::size_t>(1,
              static_cast<std::size_t>(std::floor(arcLen / slotChord)));
            cap = std::min(cap, M - placed);
            if (cap == 1) {
              const NodeRecord& child = nodes[children[placed]];
              attributes.x(child.handle) = std::round((px + R * std::cos(centerAngle)) * 100.0) / 100.0;
              attributes.y(child.handle) = std::round((py + R * std::sin(centerAngle)) * 100.0) / 100.0;
            } else {
              const double step = useArc / static_cast<double>(cap - 1);
              for (std::size_t k = 0; k < cap; ++k) {
                const double theta = centerAngle - useArc / 2.0
                                   + step * static_cast<double>(k);
                const NodeRecord& child = nodes[children[placed + k]];
                attributes.x(child.handle) = std::round((px + R * std::cos(theta)) * 100.0) / 100.0;
                attributes.y(child.handle) = std::round((py + R * std::sin(theta)) * 100.0) / 100.0;
              }
            }
            placed += cap;
            R += radialStep;
          }
        }
      }
    }
  }

  cgCheckpoint("pre-§13-cluster-swap");
  // Fast-path: when the caller is going to overwrite all positions
  // (rigid-positions with --positions-tsv), the §13/§14/§15 position-
  // optimizing passes are wasted work — they only adjust attributes.x/y
  // which gets replaced. Skip them when DJERD_SKIP_CG_OPT=1 is set.
  // §13 alone takes ~2 min on captain (1248 nodes); skipping it brings
  // the rigid-positions reroute from ~126s back to ~30s.
  const char* skipCgOptEnv = std::getenv("DJERD_SKIP_CG_OPT");
  const bool skipCgOpt = skipCgOptEnv && std::strcmp(skipCgOptEnv, "0") != 0;
  if (skipCgOpt) {
    std::fprintf(stderr,
      "[cg-fast] DJERD_SKIP_CG_OPT=1 — skipping §13/§14/§15 position passes\n");
  }
  // Counters referenced in end-of-function summary log; declared outside
  // the skipCgOpt block so the summary still compiles when §13/§14/§15
  // are skipped.
  std::size_t finalSwaps = 0;
  std::size_t spineCorridorPushed = 0;
  std::size_t prunedMoves = 0;
  std::size_t chainStraightened = 0;
  std::size_t busPushed = 0;
  std::size_t nodeNudges = 0;
  std::size_t subtreePushCount = 0;
  if (!skipCgOpt) {
  // 13. Final cluster-swap crossing-reduction pass.
  //
  // After ALL placement (compose, alone-root grid, pruning re-attach), use
  // the full dedup edge set to detect actual edge crossings. For each
  // persistent crossing whose endpoints span 2 different clusters, try
  // swapping those cluster super-node positions — translate every owned
  // node (root + members + transitively-attached pruned tree) by the
  // delta. Accept the swap if it reduces local crossings.
  //
  // This is the LAST pass and operates on the final layout, catching
  // crossings that the super-graph-level passes (9b3) couldn't see.
  {
    // Build owned-nodes per cluster root: members + transitive pruned subtree
    std::unordered_map<std::size_t, std::size_t> nodeToCRoot;
    for (const ClusterRecord& c : result.clusters) {
      for (const ClusterMemberInfo& m : c.members) {
        nodeToCRoot[m.nodeIdx] = c.rootIdx;
      }
    }
    std::unordered_map<std::size_t, std::size_t> immParent;
    for (const PrunedNodeRecord& p : result.prunedNodes) {
      immParent[p.nodeIdx] = p.parentIdx;
    }
    auto coreAnchor = [&](std::size_t v) {
      int hops = 0;
      while (hops++ < 200) {
        auto it = immParent.find(v);
        if (it == immParent.end() || it->second == v) break;
        v = it->second;
      }
      return v;
    };
    for (const PrunedNodeRecord& p : result.prunedNodes) {
      if (p.isAloneRoot) continue;
      if (nodeToCRoot.count(p.nodeIdx)) continue;
      const std::size_t anchor = coreAnchor(p.nodeIdx);
      auto it = nodeToCRoot.find(anchor);
      if (it != nodeToCRoot.end()) {
        nodeToCRoot[p.nodeIdx] = it->second;
      }
    }
    std::unordered_map<std::size_t, std::vector<std::size_t>> clusterOwned;
    for (const auto& kv : nodeToCRoot) {
      clusterOwned[kv.second].push_back(kv.first);
    }

    // Dedup edge list as (i, j) node indices, i < j
    std::vector<std::pair<std::size_t, std::size_t>> dedupEdges;
    for (std::size_t i = 0; i < adj.size(); ++i) {
      for (std::size_t j : adj[i]) {
        if (j > i) dedupEdges.emplace_back(i, j);
      }
    }

    auto segCross = [](double ax, double ay, double bx, double by,
                       double cx, double cy, double dx, double dy) -> bool {
      auto ccw = [](double Ax, double Ay, double Bx, double By,
                    double Cx, double Cy) -> double {
        return (Cy - Ay) * (Bx - Ax) - (By - Ay) * (Cx - Ax);
      };
      const double d1 = ccw(cx, cy, dx, dy, ax, ay);
      const double d2 = ccw(cx, cy, dx, dy, bx, by);
      const double d3 = ccw(ax, ay, bx, by, cx, cy);
      const double d4 = ccw(ax, ay, bx, by, dx, dy);
      return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
             ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
    };

    auto detectCrossings = [&]() {
      std::vector<std::pair<std::size_t, std::size_t>> out;
      for (std::size_t i = 0; i < dedupEdges.size(); ++i) {
        const std::size_t a = dedupEdges[i].first;
        const std::size_t b = dedupEdges[i].second;
        const double ax = attributes.x(nodes[a].handle);
        const double ay = attributes.y(nodes[a].handle);
        const double bx = attributes.x(nodes[b].handle);
        const double by = attributes.y(nodes[b].handle);
        for (std::size_t j = i + 1; j < dedupEdges.size(); ++j) {
          const std::size_t c = dedupEdges[j].first;
          const std::size_t d = dedupEdges[j].second;
          if (a == c || a == d || b == c || b == d) continue;
          const double cx = attributes.x(nodes[c].handle);
          const double cy = attributes.y(nodes[c].handle);
          const double dx = attributes.x(nodes[d].handle);
          const double dy = attributes.y(nodes[d].handle);
          if (segCross(ax, ay, bx, by, cx, cy, dx, dy)) {
            out.emplace_back(i, j);
          }
        }
      }
      return out;
    };

    // Local crossing count: edges incident to nodes in cluster A or B,
    // counting their crossings with all other edges. Used as before/after
    // swap evaluation metric.
    auto localCount = [&](std::size_t rootA, std::size_t rootB) -> int {
      std::unordered_set<std::size_t> involved;
      auto itA = clusterOwned.find(rootA);
      auto itB = clusterOwned.find(rootB);
      if (itA != clusterOwned.end()) for (auto n : itA->second) involved.insert(n);
      if (itB != clusterOwned.end()) for (auto n : itB->second) involved.insert(n);
      int total = 0;
      for (std::size_t i = 0; i < dedupEdges.size(); ++i) {
        const std::size_t a = dedupEdges[i].first;
        const std::size_t b = dedupEdges[i].second;
        const bool iInvolved = involved.count(a) || involved.count(b);
        if (!iInvolved) continue;
        const double ax = attributes.x(nodes[a].handle);
        const double ay = attributes.y(nodes[a].handle);
        const double bx = attributes.x(nodes[b].handle);
        const double by = attributes.y(nodes[b].handle);
        for (std::size_t j = 0; j < dedupEdges.size(); ++j) {
          if (i == j) continue;
          const std::size_t c = dedupEdges[j].first;
          const std::size_t d = dedupEdges[j].second;
          if (a == c || a == d || b == c || b == d) continue;
          const double cx = attributes.x(nodes[c].handle);
          const double cy = attributes.y(nodes[c].handle);
          const double dx = attributes.x(nodes[d].handle);
          const double dy = attributes.y(nodes[d].handle);
          if (segCross(ax, ay, bx, by, cx, cy, dx, dy)) ++total;
        }
      }
      return total;
    };

    auto translate = [&](std::size_t rootIdx, double dx, double dy) {
      auto it = clusterOwned.find(rootIdx);
      if (it == clusterOwned.end()) return;
      for (std::size_t n : it->second) {
        attributes.x(nodes[n].handle) += dx;
        attributes.y(nodes[n].handle) += dy;
      }
    };
    auto swapClusters = [&](std::size_t rootA, std::size_t rootB) {
      const double ax = attributes.x(nodes[rootA].handle);
      const double ay = attributes.y(nodes[rootA].handle);
      const double bx = attributes.x(nodes[rootB].handle);
      const double by = attributes.y(nodes[rootB].handle);
      const double dxv = bx - ax;
      const double dyv = by - ay;
      translate(rootA, dxv, dyv);
      translate(rootB, -dxv, -dyv);
    };

    // §13 cluster-swap pass with simulated annealing + multi-seed restart
    // and global-cost selection.
    //
    // Pipeline:
    //   1. Snapshot positions (= initial state for every restart).
    //   2. For each seed, restore initial positions and run SA from scratch.
    //   3. After each seed, evaluate the FULL crossing count globally
    //      (= same metric layout-quality reports later).
    //   4. Track the seed with the lowest global cost. Restore to that
    //      seed's positions at the end.
    //
    // This combines:
    //   - E (multi-seed): 2-3 different SA trajectories so a single
    //     unlucky run doesn't dictate the result.
    //   - B (global cost): selection between trajectories uses the actual
    //     global crossing count, not the local heuristic.
    //
    // Safety: best ≤ initial guaranteed (initial counted as a candidate).
    std::vector<std::pair<double, double>> initialPositions(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      initialPositions[i] = {attributes.x(nodes[i].handle), attributes.y(nodes[i].handle)};
    }
    const std::size_t initialCost = detectCrossings().size();

    std::vector<std::pair<double, double>> bestPositions = initialPositions;
    std::size_t bestCost = initialCost;
    std::size_t bestSwaps = 0;

    std::unordered_set<std::size_t> backboneSet13(
      result.mainRingNodeIdxs.begin(), result.mainRingNodeIdxs.end());
    // §13 SA budget (Apr 30 round 1, round 2 reverted):
    //   round 2 (kMaxIter=35) regressed to 2016 crossings vs round 1's
    //   1926. Slow cooling with extended iters made SA accept too many
    //   uphill moves and lose the best path. Reverted to round 1.
    constexpr int kMaxIter = 20;
    constexpr int kMaxAttemptsPerIter = 3000;
    constexpr double sa_decay = 0.92;
    constexpr double sa_min = 0.05;
    // Multi-seed restart was added speculatively. Empirically (Apr 30 GN
    // run): SA found 0 improvements, so additional seeds just retrace the
    // same null exploration. Keeping array shape for future re-enable via
    // env var if needed; default = 1 seed.
    const std::array<uint32_t, 1> kSeeds = {0xC0FFEEu};

    for (uint32_t seed : kSeeds) {
      // Restore initial state for each seed.
      for (std::size_t i = 0; i < nodes.size(); ++i) {
        attributes.x(nodes[i].handle) = initialPositions[i].first;
        attributes.y(nodes[i].handle) = initialPositions[i].second;
      }
      std::mt19937 rng(seed);
      std::uniform_real_distribution<double> uniform(0.0, 1.0);
      double sa_T = 10.0;
      std::size_t seedSwaps = 0;
      int consecutiveNoImprove = 0;

      for (int iter = 0; iter < kMaxIter; ++iter) {
      auto crossings = detectCrossings();
      if (crossings.empty()) break;
      bool anyImproved = false;
      int attempts = 0;
      for (auto& cp : crossings) {
        if (attempts++ >= kMaxAttemptsPerIter) break;
        const std::size_t ei = cp.first;
        const std::size_t ej = cp.second;
        const std::size_t a = dedupEdges[ei].first;
        const std::size_t b = dedupEdges[ei].second;
        const std::size_t c = dedupEdges[ej].first;
        const std::size_t d = dedupEdges[ej].second;
        auto rA = nodeToCRoot.find(a);
        auto rB = nodeToCRoot.find(b);
        auto rC = nodeToCRoot.find(c);
        auto rD = nodeToCRoot.find(d);
        std::set<std::pair<std::size_t, std::size_t>> tryPairs;
        auto add = [&](decltype(rA) it1, decltype(rA) it2) {
          if (it1 == nodeToCRoot.end() || it2 == nodeToCRoot.end()) return;
          if (it1->second == it2->second) return;
          if (backboneSet13.count(it1->second)
              && backboneSet13.count(it2->second)) return;
          auto p = it1->second < it2->second
            ? std::make_pair(it1->second, it2->second)
            : std::make_pair(it2->second, it1->second);
          tryPairs.insert(p);
        };
        add(rA, rC); add(rA, rD); add(rB, rC); add(rB, rD);

        bool acceptedThis = false;
        for (const auto& pr : tryPairs) {
          const int before = localCount(pr.first, pr.second);
          swapClusters(pr.first, pr.second);
          const int after = localCount(pr.first, pr.second);
          const int delta = after - before;
          bool accept = false;
          if (delta < 0) {
            accept = true;
          } else if (sa_T > sa_min && delta > 0) {
            const double prob = std::exp(-static_cast<double>(delta) / sa_T);
            if (uniform(rng) < prob) accept = true;
          }
          if (accept) {
            anyImproved = true;
            acceptedThis = true;
            ++seedSwaps;
            break;
          } else {
            swapClusters(pr.first, pr.second);  // revert
          }
        }

        // 3-cluster rotation fallback: if no 2-swap among {rA,rB,rC,rD} pairs
        // helped, the crossing may be a 3-opt local minimum where any single
        // swap makes things locally worse but a cyclic rotation reduces the
        // count. Tight gate (sa_T > 3.0) keeps it to the first ~3 SA
        // iterations (T0=6.0, decay=0.85 → iter 3 is T≈3.7). Beyond that
        // it's pure overhead — empirically rotation finds no improvements
        // once 2-swap also stops finding them.
        if (!acceptedThis && sa_T > 3.0) {
          std::vector<std::size_t> rRoots;
          auto pushR = [&](decltype(rA) it) {
            if (it == nodeToCRoot.end()) return;
            const std::size_t r = it->second;
            for (std::size_t prev : rRoots) if (prev == r) return;
            rRoots.push_back(r);
          };
          pushR(rA); pushR(rB); pushR(rC); pushR(rD);
          if (rRoots.size() >= 3) {
            // Combined local cost across the 3 involved clusters.
            auto localCount3 = [&](std::size_t r0, std::size_t r1, std::size_t r2) {
              std::unordered_set<std::size_t> involved;
              for (std::size_t r : {r0, r1, r2}) {
                auto it = clusterOwned.find(r);
                if (it != clusterOwned.end()) {
                  for (std::size_t n : it->second) involved.insert(n);
                }
              }
              int total = 0;
              for (std::size_t i = 0; i < dedupEdges.size(); ++i) {
                const std::size_t ai = dedupEdges[i].first;
                const std::size_t bi = dedupEdges[i].second;
                if (!involved.count(ai) && !involved.count(bi)) continue;
                const double ax = attributes.x(nodes[ai].handle);
                const double ay = attributes.y(nodes[ai].handle);
                const double bx = attributes.x(nodes[bi].handle);
                const double by = attributes.y(nodes[bi].handle);
                for (std::size_t j = 0; j < dedupEdges.size(); ++j) {
                  if (i == j) continue;
                  const std::size_t cj = dedupEdges[j].first;
                  const std::size_t dj = dedupEdges[j].second;
                  if (ai == cj || ai == dj || bi == cj || bi == dj) continue;
                  const double cx = attributes.x(nodes[cj].handle);
                  const double cy = attributes.y(nodes[cj].handle);
                  const double dx = attributes.x(nodes[dj].handle);
                  const double dy = attributes.y(nodes[dj].handle);
                  if (segCross(ax, ay, bx, by, cx, cy, dx, dy)) ++total;
                }
              }
              return total;
            };
            for (std::size_t i0 = 0; i0 + 2 < rRoots.size() && !acceptedThis; ++i0) {
              for (std::size_t i1 = i0 + 1; i1 < rRoots.size() && !acceptedThis; ++i1) {
                for (std::size_t i2 = i1 + 1; i2 < rRoots.size() && !acceptedThis; ++i2) {
                  const std::size_t X = rRoots[i0];
                  const std::size_t Y = rRoots[i1];
                  const std::size_t Z = rRoots[i2];
                  if (backboneSet13.count(X) && backboneSet13.count(Y)
                      && backboneSet13.count(Z)) continue;
                  const int before3 = localCount3(X, Y, Z);
                  // Direction A: swap(X,Y); swap(X,Z) → X@Z, Y@X, Z@Y.
                  swapClusters(X, Y);
                  swapClusters(X, Z);
                  const int afterA = localCount3(X, Y, Z);
                  if (afterA < before3) {
                    anyImproved = true;
                    acceptedThis = true;
                    ++seedSwaps;
                    break;
                  }
                  // Revert direction A.
                  swapClusters(X, Z);
                  swapClusters(X, Y);
                  // Direction B: swap(X,Z); swap(X,Y) → X@Y, Y@Z, Z@X.
                  swapClusters(X, Z);
                  swapClusters(X, Y);
                  const int afterB = localCount3(X, Y, Z);
                  if (afterB < before3) {
                    anyImproved = true;
                    acceptedThis = true;
                    ++seedSwaps;
                    break;
                  }
                  // Revert direction B.
                  swapClusters(X, Y);
                  swapClusters(X, Z);
                }
              }
            }
          }
        }
      }
      sa_T *= sa_decay;
      if (sa_T < sa_min) sa_T = sa_min;
      if (anyImproved) {
        consecutiveNoImprove = 0;
      } else {
        ++consecutiveNoImprove;
      }
      // Stop early when fully converged: 3 consecutive no-improvement iters
      // at minimum temperature. Without this, the loop wastes the extended
      // budget retracing dead ends.
      if (consecutiveNoImprove >= 3 && sa_T <= sa_min) break;
      }  // end for iter

      // Evaluate this seed's full cost. Keep best globally.
      const std::size_t seedCost = detectCrossings().size();
      if (seedCost < bestCost) {
        bestCost = seedCost;
        bestSwaps = seedSwaps;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          bestPositions[i] = {attributes.x(nodes[i].handle), attributes.y(nodes[i].handle)};
        }
      }
    }  // end for seed

    // Restore the best-cost positions.
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      attributes.x(nodes[i].handle) = bestPositions[i].first;
      attributes.y(nodes[i].handle) = bestPositions[i].second;
    }
    finalSwaps = bestSwaps;

    // §13.4 Node-level crossing resolution — disabled. The pass reduces
    // the dedup-line crossing count but the actually-reported metric uses
    // routed edges (which include parallel-edge expansions), and small
    // node nudges produced cost mismatches that made the output worse.
    if (false) {
      std::unordered_set<std::size_t> backboneSetND(
        result.mainRingNodeIdxs.begin(), result.mainRingNodeIdxs.end());

      auto localCountByNode = [&](std::size_t n) -> int {
        int total = 0;
        for (std::size_t i = 0; i < dedupEdges.size(); ++i) {
          const std::size_t a = dedupEdges[i].first;
          const std::size_t b = dedupEdges[i].second;
          if (a != n && b != n) continue;
          const double ax = attributes.x(nodes[a].handle);
          const double ay = attributes.y(nodes[a].handle);
          const double bx = attributes.x(nodes[b].handle);
          const double by = attributes.y(nodes[b].handle);
          for (std::size_t j = 0; j < dedupEdges.size(); ++j) {
            if (j == i) continue;
            const std::size_t c = dedupEdges[j].first;
            const std::size_t d = dedupEdges[j].second;
            if (a == c || a == d || b == c || b == d) continue;
            const double cx = attributes.x(nodes[c].handle);
            const double cy = attributes.y(nodes[c].handle);
            const double dx = attributes.x(nodes[d].handle);
            const double dy = attributes.y(nodes[d].handle);
            if (segCross(ax, ay, bx, by, cx, cy, dx, dy)) ++total;
          }
        }
        return total;
      };

      const std::array<std::pair<double, double>, 4> kDirs = {{
        {1.0, 0.0}, {-1.0, 0.0}, {0.0, 1.0}, {0.0, -1.0},
      }};
      constexpr double kStep = 30.0;
      constexpr int kNudgeIter = 4;
      constexpr int kAttemptsPerIter = 200;

      // Snapshot before the nudge pass so we can revert if global cost
      // ends up worse (balloon effect: a node move resolves one crossing
      // but introduces more elsewhere).
      std::vector<std::pair<double, double>> nudgeSnapshot(nodes.size());
      for (std::size_t i = 0; i < nodes.size(); ++i) {
        nudgeSnapshot[i] = {attributes.x(nodes[i].handle), attributes.y(nodes[i].handle)};
      }
      const std::size_t nudgeInitialCost = detectCrossings().size();

      for (int iter = 0; iter < kNudgeIter; ++iter) {
        auto crossings = detectCrossings();
        if (crossings.empty()) break;
        bool improved = false;
        int attempts = 0;
        std::unordered_set<std::size_t> nudgedThisIter;

        for (auto& cp : crossings) {
          if (attempts++ >= kAttemptsPerIter) break;
          const std::array<std::size_t, 4> endpoints = {
            dedupEdges[cp.first].first,
            dedupEdges[cp.first].second,
            dedupEdges[cp.second].first,
            dedupEdges[cp.second].second,
          };

          for (std::size_t n : endpoints) {
            if (backboneSetND.count(n)) continue;
            if (nudgedThisIter.count(n)) continue;
            const double origX = attributes.x(nodes[n].handle);
            const double origY = attributes.y(nodes[n].handle);
            const int before = localCountByNode(n);
            if (before == 0) continue;

            int bestAfter = before;
            double bestDx = 0.0, bestDy = 0.0;
            for (const auto& d : kDirs) {
              attributes.x(nodes[n].handle) = origX + d.first * kStep;
              attributes.y(nodes[n].handle) = origY + d.second * kStep;
              const int after = localCountByNode(n);
              if (after < bestAfter) {
                bestAfter = after;
                bestDx = d.first * kStep;
                bestDy = d.second * kStep;
              }
            }
            if (bestAfter < before) {
              attributes.x(nodes[n].handle) = origX + bestDx;
              attributes.y(nodes[n].handle) = origY + bestDy;
              nudgedThisIter.insert(n);
              ++nodeNudges;
              improved = true;
              break;
            } else {
              attributes.x(nodes[n].handle) = origX;
              attributes.y(nodes[n].handle) = origY;
            }
          }
        }
        if (!improved) break;
      }

      // Global validation: revert all nudges if total crossings rose.
      const std::size_t nudgeFinalCost = detectCrossings().size();
      if (nudgeFinalCost > nudgeInitialCost) {
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          attributes.x(nodes[i].handle) = nudgeSnapshot[i].first;
          attributes.y(nodes[i].handle) = nudgeSnapshot[i].second;
        }
        nodeNudges = 0;
      }
    }
  }  // end §13 scope (cluster-swap + node-nudge)

  // §13.5 Backbone-bound subtree outward push (user 2026-04-29 feedback).
  //
  // After all placement is done, for each backbone cluster X, find the
  // clusters that are *only* reachable through X (= reachable in the cluster-
  // adjacency super-graph without crossing any other backbone cluster) — X's
  // "exclusive subtree". Reassign each exclusive subtree cluster's angular
  // position around X to lie on the outward arc relative to X, so unrelated
  // edges from other backbones don't have to cross them.
  //
  // Skipped in bubble mode (placement is fixed concentric).
  // (subtreePushCount hoisted earlier so it survives skipCgOpt branch)
  if (!bubbleMode && !backboneSet.empty() && result.clusters.size() >= 2) {
    // 1. Build cluster-id adjacency from inter-cluster edges (dedup pairs).
    std::unordered_map<std::string, std::size_t> cidToRoot;
    for (const ClusterRecord& c : result.clusters) cidToRoot[c.clusterId] = c.rootIdx;
    std::unordered_map<std::size_t, std::string> rootToCid;
    for (const auto& kv : cidToRoot) rootToCid[kv.second] = kv.first;

    std::unordered_map<std::string, std::unordered_set<std::string>> clusterAdj;
    {
      // For every dedup edge, look up cluster of each endpoint (a node belongs
      // to its cluster's root). If both endpoints are in different clusters,
      // record adjacency.
      std::unordered_map<std::size_t, std::string> nodeToClusterId;
      for (const ClusterRecord& c : result.clusters) {
        for (const ClusterMemberInfo& m : c.members) {
          nodeToClusterId[m.nodeIdx] = c.clusterId;
        }
      }
      // Also include pruned nodes via their parent chain (transitive).
      std::unordered_map<std::size_t, std::size_t> immParent2;
      for (const PrunedNodeRecord& p : result.prunedNodes) {
        immParent2[p.nodeIdx] = p.parentIdx;
      }
      auto resolveCid = [&](std::size_t v) -> const std::string* {
        for (int hops = 0; hops < 200; ++hops) {
          auto it = nodeToClusterId.find(v);
          if (it != nodeToClusterId.end()) return &it->second;
          auto pIt = immParent2.find(v);
          if (pIt == immParent2.end() || pIt->second == v) break;
          v = pIt->second;
        }
        return nullptr;
      };
      for (std::size_t i = 0; i < adj.size(); ++i) {
        for (std::size_t j : adj[i]) {
          if (j <= i) continue;
          const std::string* cidA = resolveCid(i);
          const std::string* cidB = resolveCid(j);
          if (!cidA || !cidB || *cidA == *cidB) continue;
          clusterAdj[*cidA].insert(*cidB);
          clusterAdj[*cidB].insert(*cidA);
        }
      }
    }

    // 2. Backbone cluster-id set.
    std::unordered_set<std::string> backboneCids;
    for (std::size_t bn : backboneSet) {
      auto it = rootToCid.find(bn);
      if (it != rootToCid.end()) backboneCids.insert(it->second);
    }
    if (backboneCids.size() >= 2) {

    // 3. For each non-backbone cluster, compute the set of backbone clusters
    // it can reach in the super-graph (BFS without crossing through other
    // backbones). Categorize:
    //   - reachable size 1: exclusive to one backbone → push outward (later)
    //   - reachable size 2: bridge between two backbones → place between them
    //   - reachable size 3+: multi-backbone — keep as is (no good single
    //     placement)
    std::unordered_map<std::string, std::string> exclusiveOwner;
    std::unordered_map<std::string, std::pair<std::string, std::string>> bridgeOwner;
    for (const auto& [cid, _] : clusterAdj) {
      if (backboneCids.count(cid)) continue;  // backbone itself
      std::unordered_set<std::string> reachedBackbones;
      std::unordered_set<std::string> visited;
      std::vector<std::string> queue;
      queue.push_back(cid);
      visited.insert(cid);
      while (!queue.empty()) {
        const std::string cur = std::move(queue.back());
        queue.pop_back();
        auto adjIt = clusterAdj.find(cur);
        if (adjIt == clusterAdj.end()) continue;
        for (const std::string& nb : adjIt->second) {
          if (visited.count(nb)) continue;
          if (backboneCids.count(nb)) {
            reachedBackbones.insert(nb);
          } else {
            visited.insert(nb);
            queue.push_back(nb);
          }
        }
      }
      if (reachedBackbones.size() == 1) {
        exclusiveOwner[cid] = *reachedBackbones.begin();
      } else if (reachedBackbones.size() == 2) {
        auto it = reachedBackbones.begin();
        const std::string& a = *it;
        const std::string& b = *(++it);
        bridgeOwner[cid] = (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
      }
    }

    // 4. Group exclusive subtrees by owning backbone, then for each backbone
    // angularly collapse exclusive clusters toward the backbone's outward
    // direction.
    std::unordered_map<std::string, std::vector<std::string>> exclusiveByBackbone;
    for (const auto& [cid, owner] : exclusiveOwner) {
      exclusiveByBackbone[owner].push_back(cid);
    }

    // Owned-nodes per cluster: members + transitive pruned tree.
    // Reuse the structure from §13.
    std::unordered_map<std::size_t, std::vector<std::size_t>> clusterOwnedNodes;
    {
      std::unordered_map<std::size_t, std::size_t> nodeToCRoot2;
      for (const ClusterRecord& c : result.clusters) {
        for (const ClusterMemberInfo& m : c.members) {
          nodeToCRoot2[m.nodeIdx] = c.rootIdx;
        }
      }
      std::unordered_map<std::size_t, std::size_t> immParent2;
      for (const PrunedNodeRecord& p : result.prunedNodes) {
        immParent2[p.nodeIdx] = p.parentIdx;
      }
      auto coreAnchor2 = [&](std::size_t v) {
        for (int hops = 0; hops < 200; ++hops) {
          auto it = immParent2.find(v);
          if (it == immParent2.end() || it->second == v) break;
          v = it->second;
        }
        return v;
      };
      for (const PrunedNodeRecord& p : result.prunedNodes) {
        if (p.isAloneRoot) continue;
        if (nodeToCRoot2.count(p.nodeIdx)) continue;
        std::size_t a = coreAnchor2(p.nodeIdx);
        auto it = nodeToCRoot2.find(a);
        if (it != nodeToCRoot2.end()) nodeToCRoot2[p.nodeIdx] = it->second;
      }
      for (const auto& kv : nodeToCRoot2) {
        clusterOwnedNodes[kv.second].push_back(kv.first);
      }
    }

    // For each backbone, angular reposition.
    for (const auto& [bbCid, exClusters] : exclusiveByBackbone) {
      if (exClusters.empty()) continue;
      auto rIt = cidToRoot.find(bbCid);
      if (rIt == cidToRoot.end()) continue;
      const std::size_t bbRoot = rIt->second;
      const double bx = attributes.x(nodes[bbRoot].handle);
      const double by = attributes.y(nodes[bbRoot].handle);

      // Outward direction at this backbone (from outwardByCluster).
      auto oIt = outwardByCluster.find(bbCid);
      if (oIt == outwardByCluster.end()) continue;
      const double outwardTheta = oIt->second;

      // Compute current angles (relative to backbone root) and original
      // distances. Sort by current angle so the relative ordering is
      // preserved when we collapse.
      struct ClusterPolar {
        std::string cid;
        double theta;
        double dist;
      };
      std::vector<ClusterPolar> polars;
      polars.reserve(exClusters.size());
      for (const std::string& exCid : exClusters) {
        auto exIt = cidToRoot.find(exCid);
        if (exIt == cidToRoot.end()) continue;
        const std::size_t exRoot = exIt->second;
        const double ex = attributes.x(nodes[exRoot].handle);
        const double ey = attributes.y(nodes[exRoot].handle);
        const double dx = ex - bx;
        const double dy = ey - by;
        const double dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 1e-3) continue;
        polars.push_back({exCid, std::atan2(dy, dx), dist});
      }
      if (polars.empty()) continue;

      // Identification only — count exclusive subtree clusters per backbone.
      // Actual angular reposition is disabled because naive push collides
      // with other backbones' subtrees.
      subtreePushCount += polars.size();
      (void)outwardTheta;
    }

    // §13.5b Bridge cluster placement: clusters that reach exactly TWO
    // backbones go between those two backbones. Group bridges by backbone
    // pair, then for each pair distribute clusters along the midline so
    // unrelated edges don't have to cross over the backbone (user 2026-04-29
    // feedback: backbone 사이에 배치하면 backbone 관통 줄어듦).
    std::unordered_map<std::string, std::vector<std::string>> bridgesByPair;
    for (const auto& [cid, pair] : bridgeOwner) {
      const std::string key = pair.first + "|" + pair.second;
      bridgesByPair[key].push_back(cid);
    }
    // Backbone-spacing push and bridge placement disabled — naive post-pass
    // distortion always made other regions worse. The data structures are
    // kept (bridgesByPair, clusterOwnedNodes) for the SA pass below to use
    // for evaluation.
    (void)clusterOwnedNodes;
    std::size_t bridgePushCount = 0;
    if (false) for (auto& [pairKey, bridgeCids] : bridgesByPair) {
      // Decode key.
      const std::size_t sep = pairKey.find('|');
      if (sep == std::string::npos) continue;
      const std::string b1Cid = pairKey.substr(0, sep);
      const std::string b2Cid = pairKey.substr(sep + 1);
      auto r1It = cidToRoot.find(b1Cid);
      auto r2It = cidToRoot.find(b2Cid);
      if (r1It == cidToRoot.end() || r2It == cidToRoot.end()) continue;
      const double b1x = attributes.x(nodes[r1It->second].handle);
      const double b1y = attributes.y(nodes[r1It->second].handle);
      const double b2x = attributes.x(nodes[r2It->second].handle);
      const double b2y = attributes.y(nodes[r2It->second].handle);
      const double dx = b2x - b1x;
      const double dy = b2y - b1y;
      const double dist = std::sqrt(dx * dx + dy * dy);
      if (dist < 1e-3) continue;

      // Distribute cluster slots evenly between the backbones (skip the very
      // ends — that would land them on top of the backbone itself).
      const std::size_t N = bridgeCids.size();
      // Perpendicular unit vector for splitting bridges across both sides
      // when there are many of them, avoiding stacking on the midline.
      const double perpX = -dy / dist;
      const double perpY = dx / dist;
      // Maximum perpendicular offset = small fraction of inter-backbone dist
      // so bridges stay close to the line between backbones.
      const double perpMax = std::min(dist * 0.25, 4000.0);
      for (std::size_t i = 0; i < N; ++i) {
        const std::string& cid = bridgeCids[i];
        auto cIt = cidToRoot.find(cid);
        if (cIt == cidToRoot.end()) continue;
        const std::size_t cRoot = cIt->second;
        // Slot position: t in [0.2, 0.8] along the line.
        const double t = (N == 1) ? 0.5
          : 0.2 + (0.6 * static_cast<double>(i) / static_cast<double>(N - 1));
        // Alternate sides: even index → +perp, odd → -perp.
        const double side = (i % 2 == 0) ? 1.0 : -1.0;
        const double perpAmount = perpMax * side
          * (static_cast<double>((i / 2) + 1) / static_cast<double>(N));
        const double targetX = b1x + dx * t + perpX * perpAmount;
        const double targetY = b1y + dy * t + perpY * perpAmount;
        // Blend 30/70 toward target so layout is nudged, not displaced.
        const double curX = attributes.x(nodes[cRoot].handle);
        const double curY = attributes.y(nodes[cRoot].handle);
        const double newX = curX * 0.7 + targetX * 0.3;
        const double newY = curY * 0.7 + targetY * 0.3;
        const double tdx = newX - curX;
        const double tdy = newY - curY;
        auto ownIt = clusterOwnedNodes.find(cRoot);
        if (ownIt == clusterOwnedNodes.end()) continue;
        for (std::size_t n : ownIt->second) {
          attributes.x(nodes[n].handle) += tdx;
          attributes.y(nodes[n].handle) += tdy;
        }
        ++bridgePushCount;
      }
    }
    subtreePushCount += bridgePushCount;

    }  // backboneCids.size() >= 2
  }

  cgCheckpoint("pre-§14-chain-straightening");
  // 14. Chain straightening — small-bus linearisation.
  //
  // When a connector C has reach 2 (= roots A, D) but additionally has a
  // deg-2 cluster member B as neighbour (= would-be wing of A or D, broken
  // by B's external connection through C), the path A — B — C — D forms
  // a small bus that should be drawn as a STRAIGHT LINE. Without this pass,
  // B sits in cluster A's leaf area while C sits at the A-D midpoint, and
  // the B-C edge bends sharply.
  //
  // Detection: for each connector C with reach=2, walk deg-2 chains from
  // each of C's non-root neighbours toward each connected root. Place the
  // entire chain (cluster member intermediates + connector) linearly on
  // the line from root_A to root_D.
  //
  // Skipped in bubble mode (graph-terminology.md §11.2): bubble has no
  // directionalised bus concept and equal-distribution per-cluster fill
  // would be undone by linearising chains anyway.
  // (chainStraightened hoisted earlier so it survives skipCgOpt branch)
  if (!bubbleMode) {
    auto walkChain = [&](std::size_t start, std::size_t prev,
                          std::size_t targetRoot) -> std::vector<std::size_t> {
      std::vector<std::size_t> chain;
      if (start >= adj.size() || adj[start].size() != 2) return chain;
      chain.push_back(start);
      std::size_t curr = start;
      std::size_t p = prev;
      while (true) {
        std::size_t next = std::numeric_limits<std::size_t>::max();
        for (std::size_t nb : adj[curr]) {
          if (nb == p) continue;
          next = nb;
          break;
        }
        if (next == std::numeric_limits<std::size_t>::max()) break;
        if (next == targetRoot) return chain;
        if (adj[next].size() != 2) { chain.clear(); return chain; }
        chain.push_back(next);
        p = curr;
        curr = next;
      }
      chain.clear();
      return chain;
    };

    std::unordered_map<std::string, std::size_t> cidToRoot14;
    for (const ClusterRecord& c : result.clusters) cidToRoot14[c.clusterId] = c.rootIdx;

    std::unordered_set<std::size_t> rootSet14;
    for (const ClusterRecord& c : result.clusters) rootSet14.insert(c.rootIdx);

    // §14a Connector-mediated chains, with multi-bus grouping.
    //
    // Multiple connectors between the SAME root pair (A, D) form parallel
    // buses. Group connectors by sorted (rA, rD); for each group, place
    // chains at PERPENDICULAR offsets from the A-D line so they don't
    // overlap. With N parallel chains and spacing s, chain k at offset
    // (k - (N-1)/2) × s (centered around line).
    struct DetectedChain {
      std::vector<std::size_t> chain;  // ordered A side → D side
    };
    std::map<std::pair<std::size_t, std::size_t>,
             std::vector<DetectedChain>> chainsByPair;
    for (const ConnectorRecord& con : result.connectors) {
      if (con.connectedClusterIds.size() != 2) continue;
      auto aIt = cidToRoot14.find(con.connectedClusterIds[0]);
      auto dIt = cidToRoot14.find(con.connectedClusterIds[1]);
      if (aIt == cidToRoot14.end() || dIt == cidToRoot14.end()) continue;
      const std::size_t rA = aIt->second;
      const std::size_t rD = dIt->second;
      if (rA == rD) continue;

      std::vector<std::size_t> chainA, chainD;
      for (std::size_t nb : adj[con.nodeIdx]) {
        if (nb == rA || nb == rD) continue;
        if (adj[nb].size() != 2) continue;
        auto pathA = walkChain(nb, con.nodeIdx, rA);
        if (!pathA.empty()) { chainA = pathA; continue; }
        auto pathD = walkChain(nb, con.nodeIdx, rD);
        if (!pathD.empty()) { chainD = pathD; }
      }

      // Build full chain (from A side to D side, excluding the roots).
      // Note: even for plain reach-2 connectors with no chain extensions,
      // the connector itself goes on the A-D line (existing behavior).
      DetectedChain dc;
      for (auto it = chainA.rbegin(); it != chainA.rend(); ++it) {
        dc.chain.push_back(*it);
      }
      dc.chain.push_back(con.nodeIdx);
      for (std::size_t x : chainD) dc.chain.push_back(x);

      const auto pairKey = (rA < rD) ? std::make_pair(rA, rD)
                                      : std::make_pair(rD, rA);
      // Always store as (low, high). If we stored under (rA, rD), swapping
      // means walking in reverse — track orientation explicitly.
      // Simplification: always orient chain A-side → D-side based on which
      // root is "low" in pairKey.
      if (pairKey.first != rA) {
        std::reverse(dc.chain.begin(), dc.chain.end());
      }
      chainsByPair[pairKey].push_back(std::move(dc));
    }

    // §14a-bis: Router-on-bus attachment.
    //
    // A router R with 3+ roots may have a SUBSET of its roots forming a
    // multi-connector bus pair. Example: R connects {A, D, E} where A and
    // D have 2+ connectors between them = an A-D bus. Then R logically
    // belongs to the A-D bus (with edge to E branching off) — place R as
    // an additional slot on the A-D bus line.
    //
    // For each router, find the LARGEST bus pair (= sorted root pair with
    // ≥ 2 connectors already in chainsByPair) among its connected roots
    // and attach R to that bus.
    for (const RouterRecord& rtr : result.routers) {
      std::vector<std::size_t> rRoots;
      for (const auto& cid : rtr.connectedClusterIds) {
        auto it = cidToRoot14.find(cid);
        if (it != cidToRoot14.end()) rRoots.push_back(it->second);
      }
      if (rRoots.size() < 3) continue;

      std::pair<std::size_t, std::size_t> bestPair{0, 0};
      std::size_t bestSize = 0;
      for (std::size_t i = 0; i < rRoots.size(); ++i) {
        for (std::size_t j = i + 1; j < rRoots.size(); ++j) {
          const auto pair = (rRoots[i] < rRoots[j])
            ? std::make_pair(rRoots[i], rRoots[j])
            : std::make_pair(rRoots[j], rRoots[i]);
          auto cIt = chainsByPair.find(pair);
          if (cIt == chainsByPair.end()) continue;
          if (cIt->second.size() >= 2 && cIt->second.size() > bestSize) {
            bestSize = cIt->second.size();
            bestPair = pair;
          }
        }
      }
      if (bestSize >= 2) {
        DetectedChain dc;
        dc.chain.push_back(rtr.nodeIdx);
        chainsByPair[bestPair].push_back(std::move(dc));
      }
    }

    // Place each group with parallel offsets
    for (auto& kv : chainsByPair) {
      const std::size_t rLow = kv.first.first;
      const std::size_t rHi = kv.first.second;
      auto& chains = kv.second;
      const double pAx = attributes.x(nodes[rLow].handle);
      const double pAy = attributes.y(nodes[rLow].handle);
      const double pDx = attributes.x(nodes[rHi].handle);
      const double pDy = attributes.y(nodes[rHi].handle);
      const double dx = pDx - pAx;
      const double dy = pDy - pAy;
      const double dist = std::sqrt(dx * dx + dy * dy);
      if (dist < 1e-3) continue;
      // Perpendicular unit vector (rotate 90° CCW)
      const double perpX = -dy / dist;
      const double perpY = dx / dist;
      constexpr double kBusSpacing = 90.0;

      const std::size_t N = chains.size();
      for (std::size_t k = 0; k < N; ++k) {
        const double offsetMag = (static_cast<double>(k)
                                   - 0.5 * static_cast<double>(N - 1))
                                * kBusSpacing;
        const auto& chain = chains[k].chain;
        const std::size_t Nseg = chain.size();
        if (Nseg == 0) continue;
        for (std::size_t i = 0; i < Nseg; ++i) {
          const double t = static_cast<double>(i + 1)
                         / static_cast<double>(Nseg + 1);
          const double bx = pAx + t * dx;
          const double by = pAy + t * dy;
          const double x = bx + offsetMag * perpX;
          const double y = by + offsetMag * perpY;
          attributes.x(nodes[chain[i]].handle) = std::round(x * 100.0) / 100.0;
          attributes.y(nodes[chain[i]].handle) = std::round(y * 100.0) / 100.0;
        }
        ++chainStraightened;
      }
    }

    // §14b Pure-member chains: A → m1 → m2 → ... → mn → D where all mi are
    // deg-2 cluster members (no connector involved). Straighten on line A-D.
    auto walkPureChain = [&](std::size_t start, std::size_t prev,
                              const std::unordered_set<std::size_t>& rsRef)
        -> std::pair<std::vector<std::size_t>, std::size_t> {
      std::vector<std::size_t> chain;
      if (start >= adj.size() || adj[start].size() != 2) {
        return {chain, std::numeric_limits<std::size_t>::max()};
      }
      chain.push_back(start);
      std::size_t curr = start;
      std::size_t p = prev;
      while (true) {
        std::size_t next = std::numeric_limits<std::size_t>::max();
        for (std::size_t nb : adj[curr]) {
          if (nb == p) continue;
          next = nb;
          break;
        }
        if (next == std::numeric_limits<std::size_t>::max()) break;
        if (rsRef.count(next)) {
          return {chain, next};
        }
        if (adj[next].size() != 2) {
          chain.clear();
          break;
        }
        chain.push_back(next);
        p = curr;
        curr = next;
      }
      chain.clear();
      return {chain, std::numeric_limits<std::size_t>::max()};
    };

    std::set<std::pair<std::size_t, std::size_t>> processedPairs;
    for (const ClusterRecord& cRec : result.clusters) {
      const std::size_t rA = cRec.rootIdx;
      for (std::size_t nb : adj[rA]) {
        if (rootSet14.count(nb)) continue;        // direct root-root, no chain
        if (adj[nb].size() != 2) continue;         // chain entry must be deg-2
        auto walkRes = walkPureChain(nb, rA, rootSet14);
        const auto& chain = walkRes.first;
        const std::size_t rD = walkRes.second;
        if (chain.empty()) continue;
        if (rD == std::numeric_limits<std::size_t>::max()) continue;
        if (rD == rA) continue;  // wing — handled by wing pruning
        const auto pairKey = (rA < rD) ? std::make_pair(rA, rD)
                                        : std::make_pair(rD, rA);
        if (processedPairs.count(pairKey)) continue;
        processedPairs.insert(pairKey);

        const double pAx = attributes.x(nodes[rA].handle);
        const double pAy = attributes.y(nodes[rA].handle);
        const double pDx = attributes.x(nodes[rD].handle);
        const double pDy = attributes.y(nodes[rD].handle);
        const std::size_t Nseg = chain.size();
        for (std::size_t k = 0; k < Nseg; ++k) {
          const double t = static_cast<double>(k + 1)
                         / static_cast<double>(Nseg + 1);
          const double x = pAx + t * (pDx - pAx);
          const double y = pAy + t * (pDy - pAy);
          attributes.x(nodes[chain[k]].handle) = std::round(x * 100.0) / 100.0;
          attributes.y(nodes[chain[k]].handle) = std::round(y * 100.0) / 100.0;
        }
        ++chainStraightened;
      }
    }
  }

  // 14c. Bus corridor enforcement.
  //
  // Rule: bus and cross are NEVER allowed to mix. Once a bus is straightened
  // between root pair (rA, rD), no other cluster may occupy the corridor
  // between them — otherwise the other cluster's edges would cross the bus.
  //
  // For each bus pair (= 1+ connectors between the same root pair), define
  // a corridor rectangle (axis from rA → rD, half-width = N×spacing/2 + pad)
  // and push out any cluster whose root currently sits inside the corridor
  // (excluding the bus endpoint clusters themselves). Push direction =
  // perpendicular to bus axis, magnitude = enough to clear the corridor.
  //
  // Skipped in bubble mode (graph-terminology.md §11.2): bubble's full-360°
  // per-cluster fill spreads members in every direction, so any neighbouring
  // bus corridor gets invaded by some member. With strict push-out the whole
  // canvas inflates 2× (observed: 123×95k → 208×265k). Bubble keeps its
  // isotropic shape; bus-vs-bubble overlaps are accepted.
  // (busPushed hoisted earlier so it survives skipCgOpt branch)
  if (!bubbleMode) {
    // Build cluster ownership: root → all owned nodes (members + attached
    // pruned subtree). Translating the cluster moves all of these together.
    std::unordered_map<std::size_t, std::size_t> ownerOfNode;
    for (const ClusterRecord& c : result.clusters) {
      for (const auto& m : c.members) ownerOfNode[m.nodeIdx] = c.rootIdx;
    }
    std::unordered_map<std::size_t, std::size_t> immParent;
    for (const PrunedNodeRecord& p : result.prunedNodes) {
      immParent[p.nodeIdx] = p.parentIdx;
    }
    auto coreAnchor = [&](std::size_t v) {
      int hops = 0;
      while (hops++ < 200) {
        auto it = immParent.find(v);
        if (it == immParent.end() || it->second == v) break;
        v = it->second;
      }
      return v;
    };
    for (const PrunedNodeRecord& p : result.prunedNodes) {
      if (p.isAloneRoot) continue;
      if (ownerOfNode.count(p.nodeIdx)) continue;
      auto anchor = coreAnchor(p.nodeIdx);
      auto it = ownerOfNode.find(anchor);
      if (it != ownerOfNode.end()) ownerOfNode[p.nodeIdx] = it->second;
    }
    std::unordered_map<std::size_t, std::vector<std::size_t>> clusterOwned;
    for (const auto& kv : ownerOfNode) {
      clusterOwned[kv.second].push_back(kv.first);
    }

    // Backbone members must NOT be pushed by corridor enforcement — the
    // §3.10 spine placement is the layout's primary visual structure and
    // any post-pass that scrambles backbone clusters undoes that work.
    std::unordered_set<std::size_t> backboneSetBus(
      result.mainRingNodeIdxs.begin(), result.mainRingNodeIdxs.end());

    // Compute bus pairs (= connector count per sorted root pair)
    std::unordered_map<std::string, std::size_t> cidToRootC;
    for (const auto& c : result.clusters) cidToRootC[c.clusterId] = c.rootIdx;
    std::map<std::pair<std::size_t, std::size_t>, std::size_t> busSize;
    for (const auto& con : result.connectors) {
      if (con.connectedClusterIds.size() != 2) continue;
      auto aIt = cidToRootC.find(con.connectedClusterIds[0]);
      auto dIt = cidToRootC.find(con.connectedClusterIds[1]);
      if (aIt == cidToRootC.end() || dIt == cidToRootC.end()) continue;
      const auto pair = (aIt->second < dIt->second)
        ? std::make_pair(aIt->second, dIt->second)
        : std::make_pair(dIt->second, aIt->second);
      ++busSize[pair];
    }

    constexpr double kBusSpacing = 90.0;       // matches §14a multi-bus spacing
    constexpr double kCorridorPad = 200.0;
    constexpr double kPushExtra = 60.0;
    constexpr int kCorridorIters = 4;

    // Iterative push: any cluster with ANY owned node inside any bus
    // corridor gets translated until no cluster invades any corridor.
    // Cluster (not just root) checked node-by-node: leaf or member
    // extending into corridor → whole cluster pushed.
    for (int iter = 0; iter < kCorridorIters; ++iter) {
      std::size_t pushedThisIter = 0;
      for (const auto& kv : busSize) {
        const std::size_t rA = kv.first.first;
        const std::size_t rD = kv.first.second;
        const std::size_t Nbus = kv.second;
        const double Ax = attributes.x(nodes[rA].handle);
        const double Ay = attributes.y(nodes[rA].handle);
        const double Dx = attributes.x(nodes[rD].handle);
        const double Dy = attributes.y(nodes[rD].handle);
        const double axisDx = Dx - Ax;
        const double axisDy = Dy - Ay;
        const double axisLen = std::sqrt(axisDx * axisDx + axisDy * axisDy);
        if (axisLen < 1e-3) continue;
        const double ax = axisDx / axisLen;
        const double ay = axisDy / axisLen;
        const double perpUx = -ay;
        const double perpUy = ax;
        const double corridorHalfW = static_cast<double>(Nbus) * kBusSpacing / 2.0
                                    + kCorridorPad;

        for (const ClusterRecord& c : result.clusters) {
          if (c.rootIdx == rA || c.rootIdx == rD) continue;
          if (backboneSetBus.count(c.rootIdx)) continue;  // spine pinned
          // Scan ALL owned nodes (root + members + attached pruned tree).
          // Cluster invades corridor if ANY owned node falls inside.
          // Track signed max-perpendicular invasion to push the whole
          // cluster fully clear in one shot.
          auto ownedIt = clusterOwned.find(c.rootIdx);
          if (ownedIt == clusterOwned.end()) continue;
          bool invades = false;
          double signedMaxPerp = 0.0;
          for (std::size_t n : ownedIt->second) {
            const double nx = attributes.x(nodes[n].handle);
            const double ny = attributes.y(nodes[n].handle);
            const double relX = nx - Ax;
            const double relY = ny - Ay;
            const double along = relX * ax + relY * ay;
            const double perpDist = relX * perpUx + relY * perpUy;
            if (along < 0.0 || along > axisLen) continue;
            if (std::abs(perpDist) > corridorHalfW) continue;
            invades = true;
            // Track perpDist whose magnitude is closest to 0 (= farthest
            // into corridor, needs largest push).
            if (!invades || std::abs(perpDist) < std::abs(signedMaxPerp)
                || signedMaxPerp == 0.0) {
              signedMaxPerp = perpDist;
            }
          }
          if (!invades) continue;
          const double pushAmount = corridorHalfW - std::abs(signedMaxPerp)
                                    + kPushExtra;
          const double sign = signedMaxPerp >= 0.0 ? 1.0 : -1.0;
          const double dxv = sign * perpUx * pushAmount;
          const double dyv = sign * perpUy * pushAmount;
          for (std::size_t n : ownedIt->second) {
            attributes.x(nodes[n].handle) += dxv;
            attributes.y(nodes[n].handle) += dyv;
          }
          ++pushedThisIter;
        }
      }
      busPushed += pushedThisIter;
      if (pushedThisIter == 0) break;
    }
  }

  // 14d. Spine corridor enforcement — clear a horizontal channel along
  // the spine so the chain of backbone hubs is visually unobstructed.
  //
  // Push non-spine clusters whose owned nodes fall within
  // [spineXMin..spineXMax] × [spineY ± corridorHalfH] above or below the
  // corridor (current y sign decides which hemisphere). Width is chosen
  // moderate (≈300 px) so we only displace clusters genuinely on the
  // spine line; clusters already in the upper/lower hemisphere keep
  // their FMMM-balanced positions.
  // (spineCorridorPushed hoisted earlier so it survives skipCgOpt branch)
  if (result.mainRingNodeIdxs.size() >= 4 && !result.mainRingClosed) {
    // 1. Build cluster-owned-nodes map (members + transitive pruned tree).
    std::unordered_map<std::size_t, std::size_t> ownerOfNode2;
    for (const ClusterRecord& c : result.clusters) {
      for (const auto& m : c.members) ownerOfNode2[m.nodeIdx] = c.rootIdx;
    }
    std::unordered_map<std::size_t, std::size_t> immParent2;
    for (const PrunedNodeRecord& p : result.prunedNodes) {
      immParent2[p.nodeIdx] = p.parentIdx;
    }
    auto coreAnchor2 = [&](std::size_t v) {
      int hops = 0;
      while (hops++ < 200) {
        auto it = immParent2.find(v);
        if (it == immParent2.end() || it->second == v) break;
        v = it->second;
      }
      return v;
    };
    for (const PrunedNodeRecord& p : result.prunedNodes) {
      if (p.isAloneRoot) continue;
      if (ownerOfNode2.count(p.nodeIdx)) continue;
      auto anchor = coreAnchor2(p.nodeIdx);
      auto it = ownerOfNode2.find(anchor);
      if (it != ownerOfNode2.end()) ownerOfNode2[p.nodeIdx] = it->second;
    }
    std::unordered_map<std::size_t, std::vector<std::size_t>> clusterOwned2;
    for (const auto& kv : ownerOfNode2) {
      clusterOwned2[kv.second].push_back(kv.first);
    }

    // 2. Spine extent.
    std::unordered_set<std::size_t> spineSet(
      result.mainRingNodeIdxs.begin(), result.mainRingNodeIdxs.end());
    double spineY = 0.0;
    double spineXMin = std::numeric_limits<double>::infinity();
    double spineXMax = -std::numeric_limits<double>::infinity();
    std::size_t spineCount = 0;
    for (std::size_t r : result.mainRingNodeIdxs) {
      const double rx = attributes.x(nodes[r].handle);
      const double ry = attributes.y(nodes[r].handle);
      spineY += ry;
      spineXMin = std::min(spineXMin, rx);
      spineXMax = std::max(spineXMax, rx);
      ++spineCount;
    }
    if (spineCount > 0) {
      spineY /= static_cast<double>(spineCount);
      const double corridorHalfH = 300.0;       // moderate, just clears spine
      constexpr int kSpineCorridorIters = 3;
      constexpr double kSpinePushExtra = 60.0;
      for (int iter = 0; iter < kSpineCorridorIters; ++iter) {
        std::size_t pushedThisIter = 0;
        for (const ClusterRecord& c : result.clusters) {
          if (spineSet.count(c.rootIdx)) continue;
          auto ownedIt = clusterOwned2.find(c.rootIdx);
          if (ownedIt == clusterOwned2.end()) continue;
          bool invades = false;
          double signedY = 0.0;
          for (std::size_t n : ownedIt->second) {
            const double nx = attributes.x(nodes[n].handle);
            const double ny = attributes.y(nodes[n].handle);
            if (nx < spineXMin || nx > spineXMax) continue;
            const double dy = ny - spineY;
            if (std::abs(dy) > corridorHalfH) continue;
            invades = true;
            if (std::abs(dy) < std::abs(signedY) || signedY == 0.0) {
              signedY = dy;
            }
          }
          if (!invades) continue;
          const double rootY = attributes.y(nodes[c.rootIdx].handle);
          const double sign = (rootY >= spineY) ? 1.0 : -1.0;
          const double targetEdge = spineY + sign * (corridorHalfH + kSpinePushExtra);
          const double dyShift = targetEdge - (spineY + signedY);
          for (std::size_t n : ownedIt->second) {
            attributes.y(nodes[n].handle) += dyShift;
          }
          ++pushedThisIter;
        }
        spineCorridorPushed += pushedThisIter;
        if (pushedThisIter == 0) break;
      }

      // §14d.5 (single-spineY flatten) and §14e (single-spineY leaf nudge)
      // were single-line-spine specific. With multi-row backbone they would
      // collapse all rows into a single y. Spine flatten now happens in
      // main.cpp at the very end, row-aware.
    }
  }

  cgCheckpoint("pre-§15-pruned-cross-min");
  // 15. Pruned-node cross-min iteration.
  //
  // After backbone, roots, buses, and clusters are positioned, the only
  // remaining flexibility is in the leaves/wings/level-2+ pruned nodes —
  // each has a single edge (to its parent) and can rotate freely around
  // parent without affecting graph structure. Iterate: for each pruned
  // node, try alternative angles around parent; pick the angle that
  // minimizes crossings of its edge against other edges.
  // (prunedMoves hoisted earlier so it survives skipCgOpt branch)
  {
    // Build dedup edge list once
    std::vector<std::pair<std::size_t, std::size_t>> dedupEdges;
    for (std::size_t i = 0; i < adj.size(); ++i) {
      for (std::size_t j : adj[i]) {
        if (j > i) dedupEdges.emplace_back(i, j);
      }
    }

    auto segCrossPruned = [](double ax, double ay, double bx, double by,
                              double cx, double cy, double dx, double dy) -> bool {
      auto ccw = [](double Ax, double Ay, double Bx, double By,
                    double Cx, double Cy) -> double {
        return (Cy - Ay) * (Bx - Ax) - (By - Ay) * (Cx - Ax);
      };
      const double d1 = ccw(cx, cy, dx, dy, ax, ay);
      const double d2 = ccw(cx, cy, dx, dy, bx, by);
      const double d3 = ccw(ax, ay, bx, by, cx, cy);
      const double d4 = ccw(ax, ay, bx, by, dx, dy);
      return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
             ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
    };

    auto countEdgeCrossings = [&](std::size_t Nidx, std::size_t parentIdx) -> int {
      int total = 0;
      const double Nx = attributes.x(nodes[Nidx].handle);
      const double Ny = attributes.y(nodes[Nidx].handle);
      const double Px = attributes.x(nodes[parentIdx].handle);
      const double Py = attributes.y(nodes[parentIdx].handle);
      for (const auto& e : dedupEdges) {
        const std::size_t a = e.first;
        const std::size_t b = e.second;
        if (a == Nidx || b == Nidx) continue;  // share endpoint with N's edge
        const double Ax = attributes.x(nodes[a].handle);
        const double Ay = attributes.y(nodes[a].handle);
        const double Bx = attributes.x(nodes[b].handle);
        const double By = attributes.y(nodes[b].handle);
        if (segCrossPruned(Nx, Ny, Px, Py, Ax, Ay, Bx, By)) ++total;
      }
      return total;
    };

    // Count crossings of ALL edges incident to N (vs all other dedup edges).
    // Used by extended §15 + §15.5 for multi-edge pruned nodes, alone-roots
    // and independents, where N may have several original-graph neighbours
    // beyond a single anchor parent. Cost: O(deg(N) × E).
    auto countNodeCrossings = [&](std::size_t Nidx) -> int {
      int total = 0;
      if (Nidx >= adj.size()) return 0;
      const double Nx = attributes.x(nodes[Nidx].handle);
      const double Ny = attributes.y(nodes[Nidx].handle);
      for (std::size_t nb : adj[Nidx]) {
        if (nb == Nidx) continue;
        const double Px = attributes.x(nodes[nb].handle);
        const double Py = attributes.y(nodes[nb].handle);
        for (const auto& e : dedupEdges) {
          const std::size_t a = e.first;
          const std::size_t b = e.second;
          if (a == Nidx || b == Nidx) continue;
          if (a == nb || b == nb) continue;  // shares endpoint
          const double Ax = attributes.x(nodes[a].handle);
          const double Ay = attributes.y(nodes[a].handle);
          const double Bx = attributes.x(nodes[b].handle);
          const double By = attributes.y(nodes[b].handle);
          if (segCrossPruned(Nx, Ny, Px, Py, Ax, Ay, Bx, By)) ++total;
        }
      }
      return total;
    };

    // Sort pruned by level descending (deepest first — their parents
    // settle in earlier iterations).
    //
    // Hub-leaf preservation: §12 already placed hub-root children on a
    // tight inward 180° arc with concentric multi-rings (see hub override
    // there). §15's free 360° rotation would scatter those leaves across
    // the canvas, undoing the one-side distribution that keeps the
    // outward hemisphere clear for backbone edges. Skip hub-root children
    // here — preserves the §12 placement.
    std::unordered_set<std::size_t> clusterRootSet15;
    for (const ClusterRecord& c : result.clusters) clusterRootSet15.insert(c.rootIdx);
    std::vector<const PrunedNodeRecord*> prunedSorted;
    for (const PrunedNodeRecord& p : result.prunedNodes) {
      if (p.isAloneRoot) continue;
      const bool parentIsClusterRoot = clusterRootSet15.count(p.parentIdx) > 0;
      const std::size_t parentDeg15 = (p.parentIdx < adj.size())
        ? adj[p.parentIdx].size() : 0;
      if (parentIsClusterRoot && parentDeg15 >= 20) continue;  // hub child
      prunedSorted.push_back(&p);
    }
    // Higher-degree pruned nodes participate in more edges, so place them
    // first within a level — subsequent low-degree placements can avoid the
    // angles those wings claim.
    std::sort(prunedSorted.begin(), prunedSorted.end(),
      [&adj](const PrunedNodeRecord* a, const PrunedNodeRecord* b) {
        if (a->level != b->level) return a->level > b->level;
        const std::size_t da = (a->nodeIdx < adj.size()) ? adj[a->nodeIdx].size() : 0;
        const std::size_t db = (b->nodeIdx < adj.size()) ? adj[b->nodeIdx].size() : 0;
        if (da != db) return da > db;
        return a->nodeIdx < b->nodeIdx;
      });

    constexpr double kPi = 3.14159265358979;
    const double kAngleDeltas[] = {
      -3 * kPi / 4, -2 * kPi / 3, -kPi / 2, -kPi / 3,
      -kPi / 4, -kPi / 6, -kPi / 12,
      kPi / 12, kPi / 6, kPi / 4, kPi / 3, kPi / 2,
      2 * kPi / 3, 3 * kPi / 4, kPi
    };
    constexpr int kPrunedCrossMinIters = 10;
    for (int iter = 0; iter < kPrunedCrossMinIters; ++iter) {
      std::size_t movedThisIter = 0;
      for (const PrunedNodeRecord* p : prunedSorted) {
        const std::size_t Nidx = p->nodeIdx;
        const std::size_t parent = p->parentIdx;
        if (Nidx == parent) continue;
        const double Nx = attributes.x(nodes[Nidx].handle);
        const double Ny = attributes.y(nodes[Nidx].handle);
        const double Px = attributes.x(nodes[parent].handle);
        const double Py = attributes.y(nodes[parent].handle);
        const double dx = Nx - Px;
        const double dy = Ny - Py;
        const double R = std::sqrt(dx * dx + dy * dy);
        if (R < 1e-3) continue;
        const double currentAngle = std::atan2(dy, dx);
        // Single-edge cost (N-parent only). Tested multi-edge variant Apr 30:
        // it rejected partial-improvement rotations and net regressed
        // crossings (488 moves → 111 moves; edgeCrossings 1926 → 1952).
        // Pruned nodes are mostly degree-1 anyway so single-edge cost is
        // equivalent for them, and for multi-degree pruned the partial
        // improvements were valuable.
        const int currentCount = countEdgeCrossings(Nidx, parent);
        if (currentCount == 0) continue;

        int bestCount = currentCount;
        double bestAngle = currentAngle;
        for (double delta : kAngleDeltas) {
          const double angle = currentAngle + delta;
          const double tx = Px + R * std::cos(angle);
          const double ty = Py + R * std::sin(angle);
          attributes.x(nodes[Nidx].handle) = tx;
          attributes.y(nodes[Nidx].handle) = ty;
          const int count = countEdgeCrossings(Nidx, parent);
          if (count < bestCount) {
            bestCount = count;
            bestAngle = angle;
          }
        }
        const double finalX = Px + R * std::cos(bestAngle);
        const double finalY = Py + R * std::sin(bestAngle);
        attributes.x(nodes[Nidx].handle) = std::round(finalX * 100.0) / 100.0;
        attributes.y(nodes[Nidx].handle) = std::round(finalY * 100.0) / 100.0;
        if (bestCount < currentCount) ++movedThisIter;
      }
      prunedMoves += movedThisIter;
      if (movedThisIter == 0) break;
    }

    // §15.5 Position-optimize alone-roots and independents (rotation).
    //
    // Tried 3 variants on Apr 30:
    //   a) Rotation around highest-deg neighbour: 13 moves, -62 cross. ← winner
    //   b) Raster low-density cell pick: 3 moves, +107 cross (lone cells far
    //      from neighbours → long edges through dense areas).
    //   c) Raster corridor (path-density to neighbours): 26 moves, +77 cross
    //      (improved cluster_graph hotspot 3386→3270 but routing+leaf-untangle
    //      regressed final metric — moves crossed more node bboxes, raised
    //      edgeNodeIntersections 447→489 and forced more detour waypoints).
    // Rotation kept as proven best. Anchor = highest-deg neighbour, fixed
    // radius = current distance, 24 angles tried.
    std::vector<std::size_t> extraNodes;
    extraNodes.reserve(result.prunedNodes.size() + result.independentNodeIndices.size());
    for (const PrunedNodeRecord& p : result.prunedNodes) {
      if (!p.isAloneRoot) continue;
      if (p.nodeIdx >= adj.size() || adj[p.nodeIdx].empty()) continue;
      extraNodes.push_back(p.nodeIdx);
    }
    for (std::size_t idx : result.independentNodeIndices) {
      if (idx >= adj.size() || adj[idx].empty()) continue;
      extraNodes.push_back(idx);
    }
    std::sort(extraNodes.begin(), extraNodes.end(),
      [&adj](std::size_t a, std::size_t b) {
        const std::size_t da = (a < adj.size()) ? adj[a].size() : 0;
        const std::size_t db = (b < adj.size()) ? adj[b].size() : 0;
        if (da != db) return da > db;
        return a < b;
      });

    std::size_t extraMoves = 0;
    for (int iter = 0; iter < kPrunedCrossMinIters; ++iter) {
      std::size_t movedThisIter = 0;
      for (std::size_t Nidx : extraNodes) {
        if (Nidx >= adj.size() || adj[Nidx].empty()) continue;
        // Pick highest-degree neighbour as anchor (most stable centre).
        std::size_t parent = adj[Nidx][0];
        std::size_t bestParentDeg = (parent < adj.size()) ? adj[parent].size() : 0;
        for (std::size_t cand : adj[Nidx]) {
          const std::size_t deg = (cand < adj.size()) ? adj[cand].size() : 0;
          if (deg > bestParentDeg) { parent = cand; bestParentDeg = deg; }
        }
        if (Nidx == parent) continue;
        const double Nx = attributes.x(nodes[Nidx].handle);
        const double Ny = attributes.y(nodes[Nidx].handle);
        const double Px = attributes.x(nodes[parent].handle);
        const double Py = attributes.y(nodes[parent].handle);
        const double dx = Nx - Px;
        const double dy = Ny - Py;
        const double R = std::sqrt(dx * dx + dy * dy);
        if (R < 1e-3) continue;
        const double currentAngle = std::atan2(dy, dx);
        const int currentCount = countNodeCrossings(Nidx);
        if (currentCount == 0) continue;

        int bestCount = currentCount;
        double bestAngle = currentAngle;
        for (double delta : kAngleDeltas) {
          const double angle = currentAngle + delta;
          const double tx = Px + R * std::cos(angle);
          const double ty = Py + R * std::sin(angle);
          attributes.x(nodes[Nidx].handle) = tx;
          attributes.y(nodes[Nidx].handle) = ty;
          const int count = countNodeCrossings(Nidx);
          if (count < bestCount) {
            bestCount = count;
            bestAngle = angle;
          }
        }
        const double finalX = Px + R * std::cos(bestAngle);
        const double finalY = Py + R * std::sin(bestAngle);
        attributes.x(nodes[Nidx].handle) = std::round(finalX * 100.0) / 100.0;
        attributes.y(nodes[Nidx].handle) = std::round(finalY * 100.0) / 100.0;
        if (bestCount < currentCount) ++movedThisIter;
      }
      extraMoves += movedThisIter;
      if (movedThisIter == 0) break;
    }
    if (!extraNodes.empty()) {
      std::fprintf(stderr,
        "[§15.5] Optimized %zu alone-root+independent nodes (%zu moves).\n",
        extraNodes.size(), extraMoves);
    }

    // §15.7 Intra-cluster member angular reorder. DEFAULT OFF.
    //
    // Apr 30 result: 4 clusters / 8 nodes reordered, dedup-cross 3464 → 3462
    // (-2), but FINAL routed crossings 1864 → 1907 (+43). Same dedup vs
    // routed metric mismatch as §15.5-corridor: cluster_graph internal
    // straight-line metric improved, main.cpp routing pass produced more
    // crossings. Disabled; opt in with DJERD_INTRA_REORDER=1.
    //
    // §10/§11 places cluster members in a radial pattern around the cluster
    // root. For clusters connecting to several other clusters in different
    // directions, the static radial doesn't align members with the
    // direction of their inter-cluster edge — so the edge has to traverse
    // the cluster body to exit on the side facing the connected cluster.
    //
    // Approach: for each cluster member m without descendants, compute m's
    // "preferred angle" = circular mean of directions to connected non-self
    // cluster centres. Permute members within the cluster so the i-th
    // (by preferred angle) gets the i-th slot (by current angle). Preserves
    // each member's radius — just shuffles which member sits at which
    // angular slot, keeping the cluster shape intact.
    //
    // Skipped: members that anchor pruned subtrees or leaf bundles
    // (moving them would require translating the entire subtree, which
    // can disrupt subsequent passes). Those members keep their original
    // angle slot, leaving room for "free" members to fill better positions.
    //
    // Global revert guard: if total dedup-edge crossings rises after this
    // pass, revert all moves.
    {
      const char* intraEnv = std::getenv("DJERD_INTRA_REORDER");
      const bool runIntra = intraEnv && std::strcmp(intraEnv, "0") != 0;
      if (runIntra) {
      auto countAllDedupCrossings = [&]() -> std::size_t {
        std::size_t total = 0;
        for (std::size_t i = 0; i < dedupEdges.size(); ++i) {
          const auto a = dedupEdges[i].first;
          const auto b = dedupEdges[i].second;
          const double ax = attributes.x(nodes[a].handle);
          const double ay = attributes.y(nodes[a].handle);
          const double bx = attributes.x(nodes[b].handle);
          const double by = attributes.y(nodes[b].handle);
          for (std::size_t j = i + 1; j < dedupEdges.size(); ++j) {
            const auto c = dedupEdges[j].first;
            const auto dn = dedupEdges[j].second;
            if (a == c || a == dn || b == c || b == dn) continue;
            const double cx = attributes.x(nodes[c].handle);
            const double cy = attributes.y(nodes[c].handle);
            const double dx2 = attributes.x(nodes[dn].handle);
            const double dy2 = attributes.y(nodes[dn].handle);
            if (segCrossPruned(ax, ay, bx, by, cx, cy, dx2, dy2)) ++total;
          }
        }
        return total;
      };

      std::unordered_set<std::size_t> hasDescendants;
      for (const auto& p : result.prunedNodes) {
        if (!p.isAloneRoot && p.parentIdx != p.nodeIdx) {
          hasDescendants.insert(p.parentIdx);
        }
      }
      for (const auto& g : result.leafMatrixGroups) {
        hasDescendants.insert(g.parentIdx);
      }

      std::vector<std::pair<double, double>> snap157(nodes.size());
      for (std::size_t i = 0; i < nodes.size(); ++i) {
        snap157[i] = {attributes.x(nodes[i].handle),
                      attributes.y(nodes[i].handle)};
      }
      const std::size_t pre157 = countAllDedupCrossings();

      std::unordered_map<std::size_t, std::string> nodeToCid157;
      std::unordered_map<std::string, std::pair<double, double>> clusterCenters157;
      for (const auto& c : result.clusters) {
        for (const auto& m : c.members) nodeToCid157[m.nodeIdx] = c.clusterId;
        clusterCenters157[c.clusterId] = {
          attributes.x(nodes[c.rootIdx].handle),
          attributes.y(nodes[c.rootIdx].handle)};
      }

      std::size_t reorderedClusters = 0;
      std::size_t totalReorderedNodes = 0;

      for (const auto& c : result.clusters) {
        if (c.members.size() < 3) continue;
        const std::size_t rootIdx = c.rootIdx;
        const auto centerIt = clusterCenters157.find(c.clusterId);
        if (centerIt == clusterCenters157.end()) continue;
        const auto cCenter = centerIt->second;

        struct MInfo {
          std::size_t nodeIdx;
          double currentAngle;
          double preferredAngle;
          double radius;
        };
        std::vector<MInfo> mInfos;
        mInfos.reserve(c.members.size());

        for (const auto& m : c.members) {
          if (m.nodeIdx == rootIdx) continue;
          if (m.nodeIdx >= nodes.size()) continue;
          if (hasDescendants.count(m.nodeIdx)) continue;
          const double mx = attributes.x(nodes[m.nodeIdx].handle);
          const double my = attributes.y(nodes[m.nodeIdx].handle);
          const double dx = mx - cCenter.first;
          const double dy = my - cCenter.second;
          const double r = std::sqrt(dx * dx + dy * dy);
          if (r < 1e-3) continue;
          const double curA = std::atan2(dy, dx);

          double sumX = 0.0, sumY = 0.0;
          int dirCount = 0;
          if (m.nodeIdx < adj.size()) {
            for (std::size_t adjN : adj[m.nodeIdx]) {
              if (adjN == m.nodeIdx) continue;
              auto cidIt = nodeToCid157.find(adjN);
              if (cidIt == nodeToCid157.end()) continue;
              if (cidIt->second == c.clusterId) continue;
              auto otherIt = clusterCenters157.find(cidIt->second);
              if (otherIt == clusterCenters157.end()) continue;
              const double dirX = otherIt->second.first - cCenter.first;
              const double dirY = otherIt->second.second - cCenter.second;
              const double mag = std::sqrt(dirX * dirX + dirY * dirY);
              if (mag < 1e-3) continue;
              sumX += dirX / mag;
              sumY += dirY / mag;
              ++dirCount;
            }
          }
          const double prefA = (dirCount > 0) ? std::atan2(sumY, sumX) : curA;
          mInfos.push_back({m.nodeIdx, curA, prefA, r});
        }

        if (mInfos.size() < 2) continue;

        std::vector<std::pair<double, double>> slots;
        slots.reserve(mInfos.size());
        for (const auto& mi : mInfos) {
          slots.emplace_back(mi.currentAngle, mi.radius);
        }

        std::sort(mInfos.begin(), mInfos.end(),
          [](const MInfo& a, const MInfo& b) {
            return a.preferredAngle < b.preferredAngle;
          });
        std::sort(slots.begin(), slots.end(),
          [](const auto& a, const auto& b) { return a.first < b.first; });

        bool anyChange = false;
        for (std::size_t i = 0; i < mInfos.size(); ++i) {
          const double newAngle = slots[i].first;
          const double newR = slots[i].second;
          const double newX = cCenter.first + newR * std::cos(newAngle);
          const double newY = cCenter.second + newR * std::sin(newAngle);
          const std::size_t Nidx = mInfos[i].nodeIdx;
          const double oldX = attributes.x(nodes[Nidx].handle);
          const double oldY = attributes.y(nodes[Nidx].handle);
          const double dxv = newX - oldX;
          const double dyv = newY - oldY;
          if (dxv * dxv + dyv * dyv < 1.0) continue;
          attributes.x(nodes[Nidx].handle) = std::round(newX * 100.0) / 100.0;
          attributes.y(nodes[Nidx].handle) = std::round(newY * 100.0) / 100.0;
          anyChange = true;
          ++totalReorderedNodes;
        }
        if (anyChange) ++reorderedClusters;
      }

      const std::size_t post157 = countAllDedupCrossings();
      if (post157 > pre157) {
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          attributes.x(nodes[i].handle) = snap157[i].first;
          attributes.y(nodes[i].handle) = snap157[i].second;
        }
        std::fprintf(stderr,
          "[§15.7] Intra-cluster reorder REVERTED (cross %zu → %zu, +%zu).\n",
          pre157, post157, post157 - pre157);
      } else {
        std::fprintf(stderr,
          "[§15.7] Reordered %zu clusters / %zu nodes (cross %zu → %zu, %zu fewer).\n",
          reorderedClusters, totalReorderedNodes, pre157, post157,
          pre157 - post157);
      }
      }  // end if (runIntra)
    }

    // §16. Crossing-aware cluster-center SA (free moves, not just swaps).
    //
    // Each move translates one cluster (root + members + pruned subtree)
    // by a Gaussian step. Cost = sum of incident inter-cluster edge
    // crossings against ALL dedup edges (= straight-line cross at member
    // level — the metric we actually care about, not super-graph cross).
    // Backbone clusters are pinned. Metropolis acceptance.
    //
    // Default OFF — opt in with DJERD_META_SA=1. SA wandering risks
    // visual regression; needs validation.
    {
      // Apr 30 verification: 4240/5000 moves accepted (84.8% local cost
      // reduction) but final routed crossings 1810 → 1940 (+130). Same
      // dedup→routed metric mismatch as §15.5-corridor/§15.7/variant-B.
      // Reverted to opt-in via DJERD_META_SA=1.
      const char* metaSaEnv = std::getenv("DJERD_META_SA");
      const bool runMetaSa = metaSaEnv && std::strcmp(metaSaEnv, "0") != 0;
      if (runMetaSa) {
        // Build cluster ownership: rootIdx → owned nodes (members + pruned
        // subtree). Members come from result.clusters. Pruned descend from
        // their parent until hitting a cluster member.
        std::unordered_map<std::size_t, std::vector<std::size_t>> ownedByRoot16;
        std::unordered_map<std::size_t, std::size_t> nodeToRoot16;
        for (const ClusterRecord& c : result.clusters) {
          for (const auto& m : c.members) {
            ownedByRoot16[c.rootIdx].push_back(m.nodeIdx);
            nodeToRoot16[m.nodeIdx] = c.rootIdx;
          }
        }
        std::unordered_map<std::size_t, std::size_t> immParent16;
        for (const PrunedNodeRecord& p : result.prunedNodes) {
          immParent16[p.nodeIdx] = p.parentIdx;
        }
        auto findRoot16 = [&](std::size_t v) -> std::size_t {
          int hops = 0;
          while (hops++ < 200) {
            auto rIt = nodeToRoot16.find(v);
            if (rIt != nodeToRoot16.end()) return rIt->second;
            auto pIt = immParent16.find(v);
            if (pIt == immParent16.end() || pIt->second == v) {
              return std::numeric_limits<std::size_t>::max();
            }
            v = pIt->second;
          }
          return std::numeric_limits<std::size_t>::max();
        };
        for (const PrunedNodeRecord& p : result.prunedNodes) {
          if (p.isAloneRoot) continue;
          if (nodeToRoot16.count(p.nodeIdx)) continue;
          const std::size_t root = findRoot16(p.parentIdx);
          if (root == std::numeric_limits<std::size_t>::max()) continue;
          ownedByRoot16[root].push_back(p.nodeIdx);
          nodeToRoot16[p.nodeIdx] = root;
        }

        // Per-cluster inter-cluster incident edges.
        std::unordered_map<std::size_t, std::vector<std::size_t>> incidentByRoot;
        for (std::size_t e = 0; e < dedupEdges.size(); ++e) {
          const std::size_t a = dedupEdges[e].first;
          const std::size_t b = dedupEdges[e].second;
          auto raIt = nodeToRoot16.find(a);
          auto rbIt = nodeToRoot16.find(b);
          if (raIt == nodeToRoot16.end() || rbIt == nodeToRoot16.end()) continue;
          if (raIt->second == rbIt->second) continue;
          incidentByRoot[raIt->second].push_back(e);
          incidentByRoot[rbIt->second].push_back(e);
        }

        // Pin backbone clusters (visual structure).
        std::unordered_set<std::size_t> backbonePinned16(
          result.mainRingNodeIdxs.begin(), result.mainRingNodeIdxs.end());

        std::vector<std::size_t> movable;
        for (const auto& kv : ownedByRoot16) {
          if (backbonePinned16.count(kv.first)) continue;
          if (incidentByRoot.find(kv.first) == incidentByRoot.end()) continue;
          movable.push_back(kv.first);
        }

        if (!movable.empty()) {
          auto incidentCross = [&](std::size_t root) {
            int total = 0;
            auto inc = incidentByRoot.find(root);
            if (inc == incidentByRoot.end()) return total;
            for (std::size_t ei : inc->second) {
              const std::size_t a = dedupEdges[ei].first;
              const std::size_t b = dedupEdges[ei].second;
              const double ax = attributes.x(nodes[a].handle);
              const double ay = attributes.y(nodes[a].handle);
              const double bx = attributes.x(nodes[b].handle);
              const double by = attributes.y(nodes[b].handle);
              for (std::size_t j = 0; j < dedupEdges.size(); ++j) {
                if (j == ei) continue;
                const std::size_t c = dedupEdges[j].first;
                const std::size_t d = dedupEdges[j].second;
                if (a == c || a == d || b == c || b == d) continue;
                const double cx = attributes.x(nodes[c].handle);
                const double cy = attributes.y(nodes[c].handle);
                const double dx = attributes.x(nodes[d].handle);
                const double dy = attributes.y(nodes[d].handle);
                if (segCrossPruned(ax, ay, bx, by, cx, cy, dx, dy)) ++total;
              }
            }
            return total;
          };
          auto translateCluster = [&](std::size_t root, double dx, double dy) {
            auto it = ownedByRoot16.find(root);
            if (it == ownedByRoot16.end()) return;
            for (std::size_t n : it->second) {
              attributes.x(nodes[n].handle) += dx;
              attributes.y(nodes[n].handle) += dy;
            }
          };

          std::mt19937 rng16(0xCAFEFACEu);
          std::uniform_int_distribution<std::size_t> distNode(
            0, movable.size() - 1);
          std::uniform_real_distribution<double> distR(0.0, 1.0);
          std::normal_distribution<double> distMove(0.0, 80.0);

          constexpr int kAttempts = 5000;
          double T = 5.0;
          const double decay = std::pow(0.001 / 5.0, 1.0 / kAttempts);

          std::size_t accepted = 0;
          std::size_t uphill = 0;
          for (int k = 0; k < kAttempts; ++k) {
            const std::size_t root = movable[distNode(rng16)];
            const int before = incidentCross(root);
            const double dx = distMove(rng16);
            const double dy = distMove(rng16);
            translateCluster(root, dx, dy);
            const int after = incidentCross(root);
            const int dE = after - before;
            bool accept = (dE <= 0);
            if (!accept && T > 1e-3) {
              const double prob = std::exp(-static_cast<double>(dE) / T);
              if (distR(rng16) < prob) {
                accept = true;
                ++uphill;
              }
            }
            if (accept) {
              ++accepted;
            } else {
              translateCluster(root, -dx, -dy);
            }
            T *= decay;
          }
          std::fprintf(stderr,
            "[§16-meta-sa] %zu/%d moves accepted (%zu uphill), "
            "%zu movable clusters.\n",
            accepted, kAttempts, uphill, movable.size());
        }
      }
    }
  }

  // === DJERD_VORONOI_CLAMP=1 ===
  // After all placement, pull each cluster member into the Voronoi cell
  // of its own cluster. Cluster center = root position. Cell radius =
  // half the distance to the nearest other cluster center, with a 10%
  // inset for safety. Members straying outside this radius are projected
  // inward along the (member, center) line. This prevents members from
  // spilling into adjacent cluster territories — a source of many
  // inter-cluster routed-edge crossings.
  std::size_t voronoiClamped = 0;
  std::size_t voronoiPrunedClamped = 0;
  {
    const char* voronoiEnv = std::getenv("DJERD_VORONOI_CLAMP");
    const bool voronoiClamp = voronoiEnv && std::strcmp(voronoiEnv, "0") != 0;
    if (voronoiClamp && !result.clusters.empty()) {
      const std::size_t K = result.clusters.size();
      std::vector<std::pair<double, double>> clusterCenter(K);
      for (std::size_t k = 0; k < K; ++k) {
        const std::size_t rootIdx = result.clusters[k].rootIdx;
        clusterCenter[k] = {
          attributes.x(nodes[rootIdx].handle),
          attributes.y(nodes[rootIdx].handle)};
      }
      std::vector<double> safeRadius(K, std::numeric_limits<double>::infinity());
      for (std::size_t i = 0; i < K; ++i) {
        for (std::size_t j = i + 1; j < K; ++j) {
          const double dx = clusterCenter[i].first - clusterCenter[j].first;
          const double dy = clusterCenter[i].second - clusterCenter[j].second;
          const double d = std::sqrt(dx * dx + dy * dy);
          const double half = d * 0.5;
          if (half < safeRadius[i]) safeRadius[i] = half;
          if (half < safeRadius[j]) safeRadius[j] = half;
        }
      }
      // Clamp cluster members.
      std::unordered_map<std::size_t, std::size_t> nodeToClusterIdx;
      for (std::size_t k = 0; k < K; ++k) {
        for (const auto& m : result.clusters[k].members) {
          nodeToClusterIdx[m.nodeIdx] = k;
        }
      }
      for (std::size_t k = 0; k < K; ++k) {
        const auto& cc = clusterCenter[k];
        const double maxR = safeRadius[k] * 0.9;
        if (!std::isfinite(maxR)) continue;
        for (const auto& m : result.clusters[k].members) {
          if (m.nodeIdx == result.clusters[k].rootIdx) continue;
          const double mx = attributes.x(nodes[m.nodeIdx].handle);
          const double my = attributes.y(nodes[m.nodeIdx].handle);
          const double dx = mx - cc.first;
          const double dy = my - cc.second;
          const double d = std::sqrt(dx * dx + dy * dy);
          if (d > maxR && d > 1e-3) {
            const double s = maxR / d;
            attributes.x(nodes[m.nodeIdx].handle) = cc.first + dx * s;
            attributes.y(nodes[m.nodeIdx].handle) = cc.second + dy * s;
            ++voronoiClamped;
          }
        }
      }
      // Clamp pruned nodes (transitive attachment to a cluster). Walk the
      // parent chain until a clustered ancestor is found.
      std::unordered_map<std::size_t, std::size_t> immParent;
      for (const auto& p : result.prunedNodes) immParent[p.nodeIdx] = p.parentIdx;
      auto resolveCluster = [&](std::size_t v) -> std::size_t {
        for (int hops = 0; hops < 200; ++hops) {
          auto it = nodeToClusterIdx.find(v);
          if (it != nodeToClusterIdx.end()) return it->second;
          auto ip = immParent.find(v);
          if (ip == immParent.end() || ip->second == v) return std::numeric_limits<std::size_t>::max();
          v = ip->second;
        }
        return std::numeric_limits<std::size_t>::max();
      };
      for (const auto& p : result.prunedNodes) {
        if (p.isAloneRoot) continue;
        const std::size_t k = resolveCluster(p.nodeIdx);
        if (k == std::numeric_limits<std::size_t>::max()) continue;
        const auto& cc = clusterCenter[k];
        const double maxR = safeRadius[k] * 0.9;
        if (!std::isfinite(maxR)) continue;
        const double mx = attributes.x(nodes[p.nodeIdx].handle);
        const double my = attributes.y(nodes[p.nodeIdx].handle);
        const double dx = mx - cc.first;
        const double dy = my - cc.second;
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d > maxR && d > 1e-3) {
          const double s = maxR / d;
          attributes.x(nodes[p.nodeIdx].handle) = cc.first + dx * s;
          attributes.y(nodes[p.nodeIdx].handle) = cc.second + dy * s;
          ++voronoiPrunedClamped;
        }
      }
      std::fprintf(stderr,
        "[voronoi-clamp] Clamped %zu members + %zu pruned to cell-inscribed radius.\n",
        voronoiClamped, voronoiPrunedClamped);
    }
  }
  }  // end if (!skipCgOpt) — §13/§14/§15 block

  cgCheckpoint("pre-bus-bundle");
  // === Bus bundle detection (graph-terminology.md §3.7.5 extension) ===
  // Standard leaf bundles group degree-1 children of one parent. A "bus
  // bundle" extends this to nodes that share the SAME multi-root
  // signature: e.g., 5+ tables that all FK to {User, Company} are
  // structurally a single bus and should render as a matrix block.
  // Detection: compute per-node sorted set of connected cluster roots;
  // group nodes with identical signatures. Place each group as a matrix
  // at the signature centroid and emit as leafMatrixGroup so the
  // existing pipeline (bbox metadata, webview frame, bundle-clear)
  // handles them.
  std::size_t busBundleCount = 0;
  std::size_t busMemberCount = 0;
  {
    // Per user request: bundle ANY group of nodes (size ≥ 2) sharing
    // the same multi-root signature — including connector pairs and
    // router triples. Threshold 2 captures the "multiple connectors
    // bridging the same A↔B" case as a single visual bus.
    // Threshold 2: tested 1 (5x more bundles) but +26% carrier cross
    // because 1-member matrix adds frame bbox + empty grouping with
    // no visual aggregation benefit. 2 is the sweet spot.
    constexpr std::size_t kBusThreshold = 2;
    // Build root index set + rootIdx → clusterId.
    std::unordered_set<std::size_t> rootSet;
    std::unordered_map<std::size_t, std::string> rootIdxToCidBus;
    for (const ClusterRecord& c : result.clusters) {
      rootSet.insert(c.rootIdx);
      rootIdxToCidBus[c.rootIdx] = c.clusterId;
    }
    // Track which nodes are already in a leaf bundle (skip them).
    std::unordered_set<std::size_t> leafBundleAbsorbed;
    for (const auto& g : result.leafMatrixGroups) {
      leafBundleAbsorbed.insert(g.parentIdx);
      for (std::size_t l : g.leafIdxs) leafBundleAbsorbed.insert(l);
    }
    // Group non-root, non-leaf-bundle nodes by signature.
    std::map<std::string, std::vector<std::size_t>> busGroups;
    for (std::size_t v = 0; v < nodes.size(); ++v) {
      if (rootSet.count(v)) continue;
      if (leafBundleAbsorbed.count(v)) continue;
      // Build signature: sorted set of cluster IDs of connected roots.
      std::set<std::string> sig;
      for (std::size_t u : adj[v]) {
        auto rIt = rootIdxToCidBus.find(u);
        if (rIt != rootIdxToCidBus.end()) sig.insert(rIt->second);
      }
      if (sig.size() < 2) continue;  // need multi-root
      std::string key;
      for (const auto& c : sig) {
        key += c;
        key += '|';
      }
      busGroups[key].push_back(v);
    }
    // For each group with size >= threshold, place as matrix.
    for (auto& kv : busGroups) {
      auto& members = kv.second;
      if (members.size() < kBusThreshold) continue;
      std::sort(members.begin(), members.end());  // determinism
      // Pick "parent" root: highest-deg cluster root in the signature.
      // Re-extract signature roots from any member's adj.
      std::set<std::string> sigCids;
      for (std::size_t u : adj[members[0]]) {
        auto rIt = rootIdxToCidBus.find(u);
        if (rIt != rootIdxToCidBus.end()) sigCids.insert(rIt->second);
      }
      std::size_t parentRootIdx = std::numeric_limits<std::size_t>::max();
      std::size_t parentDeg = 0;
      double sumX = 0.0, sumY = 0.0;
      std::size_t cnt = 0;
      for (const ClusterRecord& c : result.clusters) {
        if (!sigCids.count(c.clusterId)) continue;
        sumX += attributes.x(nodes[c.rootIdx].handle);
        sumY += attributes.y(nodes[c.rootIdx].handle);
        ++cnt;
        if (adj[c.rootIdx].size() > parentDeg) {
          parentDeg = adj[c.rootIdx].size();
          parentRootIdx = c.rootIdx;
        }
      }
      if (parentRootIdx == std::numeric_limits<std::size_t>::max()
          || cnt == 0) continue;
      const double sigCx = sumX / static_cast<double>(cnt);
      const double sigCy = sumY / static_cast<double>(cnt);
      // Matrix dimensions.
      const std::size_t M = members.size();
      double maxChildW = 0.0, maxChildH = 0.0;
      for (std::size_t m : members) {
        maxChildW = std::max(maxChildW, nodes[m].width);
        maxChildH = std::max(maxChildH, nodes[m].height);
      }
      const double cellW = maxChildW + 4.0;
      const double cellH = maxChildH + 4.0;
      const std::size_t cols = std::max<std::size_t>(1,
        static_cast<std::size_t>(std::ceil(
          std::sqrt(static_cast<double>(M)
                     * std::max(1.0, cellH / std::max(1.0, cellW))))));
      const std::size_t rows = (M + cols - 1) / cols;
      // Place matrix CENTRED on signature centroid (sigCx, sigCy).
      // Members tile left-to-right, top-to-bottom.
      for (std::size_t i = 0; i < M; ++i) {
        const std::size_t col = i % cols;
        const std::size_t row = i / cols;
        const double cellX = sigCx
          + (static_cast<double>(col) - 0.5 * static_cast<double>(cols - 1))
            * cellW;
        const double cellY = sigCy
          + (static_cast<double>(row) - 0.5 * static_cast<double>(rows - 1))
            * cellH;
        attributes.x(nodes[members[i]].handle) =
          std::round(cellX * 100.0) / 100.0;
        attributes.y(nodes[members[i]].handle) =
          std::round(cellY * 100.0) / 100.0;
      }
      // Emit as a LeafMatrixGroup so downstream (bbox metadata, frame
      // rendering, bundle-clear) treats this as a unified block.
      // sharedRootIdxs lists ALL cluster roots in the signature so the
      // webview can consolidate carrier edges from each shared root to
      // the bundle anchor (per user request: "버스나 router의 경우
      // 같은 root를 공유하는 경우 leafbundle처럼 하나의 노드와
      // 엣지로 묶자").
      ClusterGraphResult::LeafMatrixGroup group;
      group.parentIdx = parentRootIdx;
      group.leafIdxs.assign(members.begin(), members.end());
      for (const ClusterRecord& c : result.clusters) {
        if (sigCids.count(c.clusterId)) {
          group.sharedRootIdxs.push_back(c.rootIdx);
        }
      }
      group.anchorX = sigCx;
      group.anchorY = sigCy;
      result.leafMatrixGroups.push_back(std::move(group));
      ++busBundleCount;
      busMemberCount += M;
    }
  }
  if (busBundleCount > 0) {
    std::fprintf(stderr,
      "[bus-bundle] Created %zu bus bundles (%zu members, sig ≥ 2 roots).\n",
      busBundleCount, busMemberCount);
  }

  // === Crossings hotspot diagnostic ===
  // Identifies the worst-offender edges and cluster-pair signatures so
  // future optimizations can target the actual hotspots rather than
  // burning SA budget churning low-impact crossings.
  {
    std::unordered_map<std::string, std::size_t> idToIdxXH;
    idToIdxXH.reserve(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) idToIdxXH[nodes[i].modelId] = i;
    std::set<std::pair<std::size_t, std::size_t>> uniqXH;
    for (const auto& e : edges) {
      auto sIt = idToIdxXH.find(e.sourceModelId);
      auto tIt = idToIdxXH.find(e.targetModelId);
      if (sIt == idToIdxXH.end() || tIt == idToIdxXH.end()) continue;
      std::size_t a = sIt->second, b = tIt->second;
      if (a == b) continue;
      if (a > b) std::swap(a, b);
      uniqXH.emplace(a, b);
    }
    std::vector<std::pair<std::size_t, std::size_t>> dedupXH(uniqXH.begin(), uniqXH.end());

    std::unordered_map<std::size_t, std::string> nodeToCidXH;
    for (const auto& c : result.clusters) {
      for (const auto& m : c.members) nodeToCidXH[m.nodeIdx] = c.clusterId;
    }
    auto cidOf = [&](std::size_t n) {
      auto it = nodeToCidXH.find(n);
      return it != nodeToCidXH.end() ? it->second : std::string("?");
    };

    auto sgnXH = [](double x) { return (x > 0) - (x < 0); };
    auto segCrossXH = [&](double ax, double ay, double bx, double by,
                           double cx, double cy, double dvx, double dvy) {
      const int o1 = sgnXH((bx - ax) * (cy - ay) - (by - ay) * (cx - ax));
      const int o2 = sgnXH((bx - ax) * (dvy - ay) - (by - ay) * (dvx - ax));
      const int o3 = sgnXH((dvx - cx) * (ay - cy) - (dvy - cy) * (ax - cx));
      const int o4 = sgnXH((dvx - cx) * (by - cy) - (dvy - cy) * (bx - cx));
      return (o1 != o2) && (o3 != o4) && o1 != 0 && o3 != 0;
    };

    std::vector<int> edgeCrossesXH(dedupXH.size(), 0);
    std::map<std::string, int> pairCrossXH;
    std::size_t totalXH = 0;
    for (std::size_t i = 0; i < dedupXH.size(); ++i) {
      const auto a = dedupXH[i].first;
      const auto b = dedupXH[i].second;
      const double ax = attributes.x(nodes[a].handle);
      const double ay = attributes.y(nodes[a].handle);
      const double bx = attributes.x(nodes[b].handle);
      const double by = attributes.y(nodes[b].handle);
      for (std::size_t j = i + 1; j < dedupXH.size(); ++j) {
        const auto c = dedupXH[j].first;
        const auto dn = dedupXH[j].second;
        if (a == c || a == dn || b == c || b == dn) continue;
        const double cx = attributes.x(nodes[c].handle);
        const double cy = attributes.y(nodes[c].handle);
        const double dx = attributes.x(nodes[dn].handle);
        const double dy = attributes.y(nodes[dn].handle);
        if (!segCrossXH(ax, ay, bx, by, cx, cy, dx, dy)) continue;
        ++totalXH;
        ++edgeCrossesXH[i];
        ++edgeCrossesXH[j];
        std::set<std::string> s = {cidOf(a), cidOf(b), cidOf(c), cidOf(dn)};
        std::string key;
        for (const auto& x : s) {
          if (!key.empty()) key += "|";
          key += x;
        }
        ++pairCrossXH[key];
      }
    }

    std::vector<std::pair<std::size_t, int>> edgeRanked;
    edgeRanked.reserve(dedupXH.size());
    for (std::size_t i = 0; i < dedupXH.size(); ++i) {
      if (edgeCrossesXH[i] > 0) edgeRanked.emplace_back(i, edgeCrossesXH[i]);
    }
    std::sort(edgeRanked.begin(), edgeRanked.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    std::fprintf(stderr,
      "[xings-hotspot] total=%zu xings across %zu offending edges (of %zu)\n",
      totalXH, edgeRanked.size(), dedupXH.size());
    const std::size_t topN = std::min(edgeRanked.size(), std::size_t(8));
    for (std::size_t k = 0; k < topN; ++k) {
      const auto eidx = edgeRanked[k].first;
      const auto cnt = edgeRanked[k].second;
      const auto a = dedupXH[eidx].first;
      const auto b = dedupXH[eidx].second;
      std::fprintf(stderr, "  edge %s↔%s xings=%d cluster=(%s,%s)\n",
                   nodes[a].modelId.c_str(), nodes[b].modelId.c_str(), cnt,
                   cidOf(a).c_str(), cidOf(b).c_str());
    }

    std::vector<std::pair<std::string, int>> sigRanked(pairCrossXH.begin(), pairCrossXH.end());
    std::sort(sigRanked.begin(), sigRanked.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    std::fprintf(stderr, "[xings-hotspot] top cluster signatures:\n");
    const std::size_t topS = std::min(sigRanked.size(), std::size_t(8));
    for (std::size_t k = 0; k < topS; ++k) {
      std::fprintf(stderr, "  {%s} xings=%d\n",
                   sigRanked[k].first.c_str(), sigRanked[k].second);
    }
  }

  result.strategyReason =
    "Cluster-graph: " + std::to_string(result.clusters.size()) + " clusters, "
    + std::to_string(result.connectors.size()) + " connectors, "
    + std::to_string(result.routers.size()) + " routers, "
    + std::to_string(result.constellations.size()) + " constellations, "
    + std::to_string(result.polars.size()) + " polars ("
    + std::to_string(result.polarSkeletonEdgeCount) + " skel edges, "
    + std::to_string(result.polarRingCount) + " rings, "
    + std::to_string(result.polarLineCount) + " lines), "
    + std::to_string(result.superPolars.size()) + " super-polars ("
    + std::to_string(result.superPolarMetaEdgeCount) + " meta edges, topology="
    + (result.superPolarTopology.empty() ? std::string("n/a") : result.superPolarTopology) + "), "
    + std::to_string(result.ringCount) + " super-rings (length-3), "
    + std::to_string(result.independentNodeIndices.size()) + " independents, "
    + std::to_string(result.topLevelEdgeCount) + " top-level edges ("
    + std::to_string(chainEdgesAdded) + " from chain dedup). "
    "Pruning: " + std::to_string(result.prunedNodes.size()) + " pruned across "
    + std::to_string(result.maxPruningLevel) + " levels (alone-roots="
    + std::to_string(result.aloneRootCount) + "), 2-core="
    + std::to_string(result.coreNodeCount) + ". "
    "Main backbone: " + std::to_string(result.mainRingSize) + " nodes (score="
    + std::to_string(static_cast<int>(result.mainRingScore)) + ", "
    + (result.mainRingClosed ? "closed ring" : "open path") + "). "
    "Final cluster-swap pass: " + std::to_string(finalSwaps) + " swaps. "
    "Node nudges: " + std::to_string(nodeNudges) + ". "
    "Backbone subtree: " + std::to_string(subtreePushCount) + " clusters analyzed. "
    "Chain straightening: " + std::to_string(chainStraightened) + " chains linearized. "
    "Bus corridor enforcement: " + std::to_string(busPushed) + " clusters pushed out. "
    "Pruned cross-min: " + std::to_string(prunedMoves) + " pruned-node moves. "
    "Spine corridor pushes: " + std::to_string(spineCorridorPushed) + ". "
    "Outward-aware inner radial; degree-weighted hub repulsion; top-level "
    "layout via "
    + (polarAnchorActive
        ? std::string("FMMM+polar-anchor+similarity-fit")
        : (super.numberOfNodes() <= 600
            ? std::string("Sugiyama")
            : std::string("FMMM")))
    + ".";
  return result;
}

}  // namespace djerd
