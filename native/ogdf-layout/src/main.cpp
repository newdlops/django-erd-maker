#include "types.h"
#include "geometry.h"
#include "renderedMetrics.h"
#include "rectangleCollisionIndex.h"
#include "io.h"
#include "clusterGraph.h"
#include "canonicalCrossingMetrics.h"
#include "crossingLowerBound.h"

#include <cstdlib>
#include <numeric>

#include <ogdf/basic/Graph.h>
#include <ogdf/basic/basic.h>
#include <ogdf/basic/GraphAttributes.h>
#include <ogdf/energybased/StressMinimization.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <queue>
#include <random>
#include <stack>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <utility>
#include <vector>

#include "layoutPipeline.h"
#include "faceRasterGrid.h"

int main(int argc, char** argv) {
  using namespace djerd;
  try {
    const CliArguments arguments = parseArguments(argc, argv);
    if (!isSupportedMode(arguments.mode)) {
      throw std::runtime_error("unsupported mode: " + arguments.mode);
    }
    ogdf::Graph graph;
    ogdf::GraphAttributes attributes(
      graph,
      ogdf::GraphAttributes::nodeGraphics | ogdf::GraphAttributes::edgeGraphics);
    std::unordered_map<std::string, ogdf::node> nodesById;
    std::vector<NodeRecord> nodes = readNodes(arguments.nodesFile, graph, attributes, nodesById);
    std::vector<EdgeRecord> edges = readEdges(arguments.edgesFile, graph, nodesById);
    LayoutRunMetadata metadata = makeLayoutRunMetadata(arguments.mode);
    CanonicalCrossingMetadata canonicalCrossing;
    try {
      canonicalCrossing = certifyCanonicalCrossingTopology(graph, nodes, edges);
    } catch (const std::exception& error) {
      std::fprintf(stderr,
        "[canonical-crossing] certifier unavailable: %s\n",
        error.what());
    }
    // State carried out of the cluster-graph branch into the final spine
    // flatten pass (after all post-passes). Empty when not running
    // cluster_graph / bubble. Multi-row backbone: each spine root has a
    // row index; flatten pins each root's y to its row-mate average.
    std::vector<std::size_t> spineRootIdxs;
    std::vector<std::size_t> spineRowOfRoot;       // parallel to spineRootIdxs
    std::vector<std::pair<std::size_t, std::size_t>> spineOwnedPairs;  // (root, owned)
    // Non-spine cluster row alignment: pull each non-spine cluster's
    // owned tree to its primary spine-hub's row, stacking by index.
    std::vector<std::pair<std::size_t, std::size_t>> nonSpineClusterPrimary;  // (clusterRoot, primaryHub)
    std::vector<std::pair<std::size_t, std::size_t>> nonSpineOwnedPairs;      // (clusterRoot, owned)
    // Connector/router → connected cluster roots (for post-pass straight-
    // line untangling against final hub positions).
    std::vector<std::pair<std::size_t, std::vector<std::size_t>>> connectorRoots;
    std::vector<std::pair<std::size_t, std::vector<std::size_t>>> routerRoots;
    // Full cluster membership for edge bundling. Maps modelId → clusterId
    // for ALL members (root + leaf + internal + bridge). Used after
    // routing to group edges by (sourceCluster, targetCluster) and re-
    // route bundled edges through shared exit/entry ports for visual
    // bundling and cross reduction.
    std::unordered_map<std::string, std::string> clusterByModelIdFull;
    std::unordered_map<std::string, std::pair<double, double>> clusterRootPos;  // clusterId → root (x, y)
    // Leaf bundle anchor map: leaf nodeIdx → (parentIdx, anchorX, anchorY).
    // Used after routing: leaf→parent edges get an extra waypoint at the
    // anchor port so the bundle's exit segment is shared, collapsing N
    // parallel edges into 1 visual line.
    struct LeafAnchorInfo {
      std::size_t parentIdx;
      double anchorX;
      double anchorY;
    };
    std::unordered_map<std::size_t, LeafAnchorInfo> leafAnchorMap;
    // Raw matrix groups (parentIdx + leafIdxs) for post-pass bundle
    // bbox computation. Populated when cluster_graph runs.
    struct RawLeafGroup {
      std::size_t parentIdx;
      std::vector<std::size_t> leafIdxs;
      std::vector<std::size_t> sharedRootIdxs;
      double anchorX;
      double anchorY;
    };
    std::vector<RawLeafGroup> rawLeafGroups;

    if (graph.numberOfNodes() > 0) {
      if (arguments.clusterGraph || arguments.bubble) {
        // Cluster-graph pipeline (graph-terminology.md). Bubble flag implies
        // cluster-graph + bubble inner placement (concentric ring fill per
        // cluster, no outward bias).
        std::size_t louvCommCount = 0;
        std::size_t louvIters = 0;
        std::string communityAlgo;
        std::vector<std::string> labels =
          assignCommunityClusterLabels(nodes, edges, louvCommCount, louvIters, communityAlgo);
        std::fprintf(stderr,
          "[community] algorithm=%s communities=%zu meta=%zu\n",
          communityAlgo.c_str(), louvCommCount, louvIters);
        // Fast-path: when positions will be overwritten by --positions-tsv
        // and --rigid-positions is set, the §13/§14/§15 position passes
        // inside cluster_graph are wasted work (~2 min on captain).
        if (arguments.rigidPositions && !arguments.positionsTsv.empty()) {
          ::setenv("DJERD_SKIP_CG_OPT", "1", 1);
        }
        // Performance: when --positions-tsv is supplied, the entire
        // cluster_graph POSITIONING (§7b polar skeleton, §9 super-graph FMMM +
        // similarity-fit + hub repulsion, §9.5 perimeter/backbone/connector
        // untangling) is wasted work — every node position it computes is
        // overwritten by the TSV further down (~16s/run on the inheritance
        // graph × ~56 ML-pipeline calls). Skip it; the cheap STRUCTURE
        // (clusters, pruning, super-graph, leaf-bundle membership) still runs
        // so routing/carriers are intact. Multistart keeps positioning (it has
        // an empty positions-tsv and selects among the layouts it computes).
        if (!arguments.positionsTsv.empty()) {
          // overwrite=0 so a test/override env (DJERD_CG_SKIP_POSITIONING=0)
          // can disable it for A/B comparison; production never sets it, so it
          // defaults to "1" here.
          ::setenv("DJERD_CG_SKIP_POSITIONING", "1", 0);
        }
        // Optional multi-start: run cluster_graph N times with
        // different OGDF random seeds and keep the result with the
        // fewest straight-line edge crossings. FMMM's super-graph
        // placement is the main source of stochasticity inside
        // cluster_graph; different seeds land at different local
        // optima, so a few extra runs let us cherry-pick. Cost: N×
        // cluster_graph runtime (Captain ~3 min each).
        //
        // env DJERD_MULTISTART_RUNS (default 1 = single run, no
        // multistart). env DJERD_MULTISTART_SEED_BASE picks the seed
        // sequence start (default 42).
        const char* multiRunsEnv = std::getenv("DJERD_MULTISTART_RUNS");
        const int multistartRuns = multiRunsEnv
          ? std::max(1, std::atoi(multiRunsEnv)) : 1;

        auto mstartSavePositions = [&]() {
          std::vector<std::pair<double, double>> out;
          out.reserve(nodes.size());
          for (const NodeRecord& nd : nodes) {
            out.emplace_back(attributes.x(nd.handle), attributes.y(nd.handle));
          }
          return out;
        };
        auto mstartRestorePositions =
          [&](const std::vector<std::pair<double, double>>& positions) {
          for (std::size_t i = 0; i < nodes.size() && i < positions.size(); ++i) {
            attributes.x(nodes[i].handle) = positions[i].first;
            attributes.y(nodes[i].handle) = positions[i].second;
          }
        };

        // Snapshot pre-cluster_graph attributes so every re-run starts
        // from the same input state. cluster_graph internally seeds its
        // FMMM from current attribute positions in a few places, so
        // feeding it a post-run layout would bias the comparison.
        const auto mstartPreCgPositions = multistartRuns > 1
          ? mstartSavePositions()
          : std::vector<std::pair<double, double>>{};

        ClusterGraphResult cg = runClusterGraphLayout(
          nodes, edges, labels, attributes, arguments.bubble);

        // Bounded research/integration path: emit only the real model-center
        // coordinates produced by clusterGraph and stop before route/carrier
        // post-processing.  The caller re-scores every original relationship
        // as an independent straight line, so this cannot hide, merge, or
        // reinterpret an edge.  It also prevents position-only experiments
        // from allocating the much larger routed-geometry worksets.
        if (readBoolEnv("DJERD_STOP_AFTER_CLUSTER_POSITIONS", false)) {
          std::cout << std::fixed << std::setprecision(9);
          for (const NodeRecord& node : nodes) {
            std::cout << node.modelId << '\t'
                      << attributes.x(node.handle) << '\t'
                      << attributes.y(node.handle) << '\n';
          }
          return 0;
        }

        // Multistart only optimises node POSITIONS. When --positions-tsv
        // is supplied, those positions are overwritten further down (the
        // "[ml-positions] Overrode ..." block), so a multistart on a
        // positions-tsv round-trip (ML rigid reroute, bbox-target,
        // cluster-polish) is pure wasted work — its result is discarded.
        // On the Captain reload this fired ~44× (45 cluster_graph binary
        // calls, all but the baseline carry --positions-tsv), each running
        // 4 FMMM layouts whose positions were then thrown away. Gate on an
        // empty positions-tsv so multistart runs only on the real baseline
        // layout that actually keeps the positions it computes.
        if (multistartRuns > 1 && arguments.positionsTsv.empty()) {
          const char* multiSeedEnv = std::getenv("DJERD_MULTISTART_SEED_BASE");
          const int seedBase = multiSeedEnv ? std::atoi(multiSeedEnv) : 42;

          // Map modelId → node index once; multistart counter reuses it.
          std::unordered_map<std::string, std::size_t> mstartIdxByMid;
          mstartIdxByMid.reserve(nodes.size());
          for (std::size_t i = 0; i < nodes.size(); ++i) {
            mstartIdxByMid[nodes[i].modelId] = i;
          }
          // Carrier-aware multistart (DJERD_MULTISTART_BUNDLE_AWARE=1, default
          // off): score each layout by the number of distinct CARRIER-PAIRS
          // that cross, not raw straight-line edge crossings. A carrier groups
          // edges that render as one line — a LEAF bundle (all edges into one
          // leaf-matrix), or a cluster-pair BUS (all edges between the same two
          // Louvain clusters). Parallel edges in one carrier draw as a single
          // line, so their mutual crossings aren't visible; counting unique
          // carrier-pairs mirrors the rendered (carrier-grouped) crossing the
          // user actually sees — so multistart selects the seed with the fewest
          // VISIBLE crossings rather than the fewest raw ones. (Layout is
          // unchanged; this only re-scores seeds.)
          const bool mstartBundleAware = [] {
            const char* e = std::getenv("DJERD_MULTISTART_BUNDLE_AWARE");
            return e && std::strcmp(e, "0") != 0;
          }();
          std::vector<std::size_t> mstartLeafBundleOf;  // nodeIdx -> bundle+1 (0 = none)
          std::vector<int> mstartClusterOf;             // nodeIdx -> cluster idx (-1 = none)
          if (mstartBundleAware) {
            mstartLeafBundleOf.assign(nodes.size(), 0);
            for (std::size_t b = 0; b < cg.leafMatrixGroups.size(); ++b) {
              for (std::size_t leaf : cg.leafMatrixGroups[b].leafIdxs) {
                if (leaf < nodes.size()) mstartLeafBundleOf[leaf] = b + 1;
              }
            }
            mstartClusterOf.assign(nodes.size(), -1);
            for (std::size_t c = 0; c < cg.clusters.size(); ++c) {
              if (cg.clusters[c].rootIdx < nodes.size())
                mstartClusterOf[cg.clusters[c].rootIdx] = static_cast<int>(c);
              for (const ClusterMemberInfo& m : cg.clusters[c].members) {
                if (m.nodeIdx < nodes.size())
                  mstartClusterOf[m.nodeIdx] = static_cast<int>(c);
              }
            }
          }

          // Edge endpoint indices (skip dangling/self). When carrier-aware,
          // also tag each edge with a carrier id (leaf-bundle | cluster-pair
          // bus | individual) used to dedup crossings into carrier-pairs.
          std::vector<std::pair<std::size_t, std::size_t>> mstartEdgePairs;
          std::vector<unsigned long long> mstartCarrier;
          mstartEdgePairs.reserve(edges.size());
          {
            std::map<std::pair<int, int>, unsigned long long> mstartPairCarrier;
            const unsigned long long kPairBase = cg.leafMatrixGroups.size() + 2ULL;
            unsigned long long nextPair = 0;
            unsigned long long indivCarrier = kPairBase + 2000000ULL;
            for (const EdgeRecord& e : edges) {
              auto si = mstartIdxByMid.find(e.sourceModelId);
              auto ti = mstartIdxByMid.find(e.targetModelId);
              if (si == mstartIdxByMid.end() || ti == mstartIdxByMid.end()) continue;
              const std::size_t a = si->second, b = ti->second;
              if (a == b) continue;
              mstartEdgePairs.emplace_back(a, b);
              if (mstartBundleAware) {
                const std::size_t lb = std::max(mstartLeafBundleOf[a], mstartLeafBundleOf[b]);
                unsigned long long cid;
                if (lb > 0) {
                  cid = lb;  // leaf-bundle carrier (1..B)
                } else if (mstartClusterOf[a] >= 0 && mstartClusterOf[b] >= 0
                           && mstartClusterOf[a] != mstartClusterOf[b]) {
                  const int ca = std::min(mstartClusterOf[a], mstartClusterOf[b]);
                  const int cb = std::max(mstartClusterOf[a], mstartClusterOf[b]);
                  auto it = mstartPairCarrier.find({ca, cb});
                  if (it != mstartPairCarrier.end()) {
                    cid = it->second;
                  } else {
                    cid = kPairBase + (nextPair++);
                    mstartPairCarrier[{ca, cb}] = cid;
                  }
                } else {
                  cid = indivCarrier++;  // individual (unique) carrier
                }
                mstartCarrier.push_back(cid);
              }
            }
          }
          auto mstartCountCrossings = [&]() -> std::size_t {
            const std::size_t E = mstartEdgePairs.size();
            std::size_t cnt = 0;                       // raw crossing count
            std::set<unsigned long long> carrierPairs; // unique carrier-pairs (bundle-aware)
            for (std::size_t i = 0; i < E; ++i) {
              const std::size_t aIdx = mstartEdgePairs[i].first;
              const std::size_t bIdx = mstartEdgePairs[i].second;
              const RoutePoint ai{attributes.x(nodes[aIdx].handle), attributes.y(nodes[aIdx].handle)};
              const RoutePoint bi{attributes.x(nodes[bIdx].handle), attributes.y(nodes[bIdx].handle)};
              for (std::size_t j = i + 1; j < E; ++j) {
                const std::size_t cIdx = mstartEdgePairs[j].first;
                const std::size_t dIdx = mstartEdgePairs[j].second;
                // Edges sharing an endpoint meet, they don't cross.
                if (aIdx == cIdx || aIdx == dIdx || bIdx == cIdx || bIdx == dIdx) continue;
                // Same carrier (parallel edges in one bundle/bus) render as one
                // line — their mutual crossing isn't visible, so skip it.
                if (mstartBundleAware && mstartCarrier[i] == mstartCarrier[j]) continue;
                const RoutePoint aj{attributes.x(nodes[cIdx].handle), attributes.y(nodes[cIdx].handle)};
                const RoutePoint bj{attributes.x(nodes[dIdx].handle), attributes.y(nodes[dIdx].handle)};
                RoutePoint isect;
                if (!properSegmentIntersection(ai, bi, aj, bj, isect)) continue;
                if (mstartBundleAware) {
                  const unsigned long long lo = std::min(mstartCarrier[i], mstartCarrier[j]);
                  const unsigned long long hi = std::max(mstartCarrier[i], mstartCarrier[j]);
                  carrierPairs.insert((lo << 24) | hi);  // dedup into carrier-pairs
                } else {
                  ++cnt;
                }
              }
            }
            return mstartBundleAware ? carrierPairs.size() : cnt;
          };

          // A seed with fewer edge/edge crossings can still be visibly worse
          // when its straight segments pass through many non-endpoint tables.
          // Count those penetrations on the same node rectangles and 10px
          // visual margin used by the canvas audit. This is position-only, so
          // it remains cheap enough to evaluate for every multistart seed.
          const double mstartEdgeNodeWeight = [] {
            const char* e = std::getenv("DJERD_MULTISTART_EDGE_NODE_WEIGHT");
            return e && *e ? std::max(0.0, std::strtod(e, nullptr)) : 0.0;
          }();
          auto mstartCountNodeIntersections = [&]() -> std::size_t {
            constexpr double kRenderedNodeMargin = 10.0;
            std::vector<Rect> nodeRects;
            nodeRects.reserve(nodes.size());
            for (const NodeRecord& node : nodes) {
              nodeRects.push_back(nodeRect(node, attributes, kRenderedNodeMargin));
            }

            std::size_t count = 0;
            for (const auto& edge : mstartEdgePairs) {
              const RoutePoint start{
                attributes.x(nodes[edge.first].handle),
                attributes.y(nodes[edge.first].handle),
              };
              const RoutePoint end{
                attributes.x(nodes[edge.second].handle),
                attributes.y(nodes[edge.second].handle),
              };
              for (std::size_t nodeIndex = 0; nodeIndex < nodeRects.size(); ++nodeIndex) {
                if (nodeIndex == edge.first || nodeIndex == edge.second) continue;
                if (segmentIntersectsRect(start, end, nodeRects[nodeIndex])) {
                  ++count;
                }
              }
            }
            return count;
          };

          // Candidate C: env-tunable multistart selection metric. By
          // default the native binary remains crossing-only; the extension
          // supplies EDGE_NODE_WEIGHT=1 so one visible node penetration has
          // the same cost as one crossing in visualCrossings. A positive
          // DJERD_MULTISTART_BBOX_WEIGHT blends in the bounding-box area
          // (billions, from node-center spread) so the search can trade a
          // few crossings for a more compact seed — a Pareto knob, not a
          // free win. Synthetic bundle geometry is unavailable here
          // (positions only, pre-route), so the final renderer still performs
          // the authoritative audit. With both optional weights at zero the
          // native score remains exactly the raw crossing count.
          const double mstartBboxWeight = [] {
            const char* e = std::getenv("DJERD_MULTISTART_BBOX_WEIGHT");
            return e && *e ? std::strtod(e, nullptr) : 0.0;
          }();
          auto mstartBboxAreaB = [&]() -> double {
            double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
            for (const NodeRecord& nd : nodes) {
              const double x = attributes.x(nd.handle);
              const double y = attributes.y(nd.handle);
              if (x < minX) minX = x;
              if (x > maxX) maxX = x;
              if (y < minY) minY = y;
              if (y > maxY) maxY = y;
            }
            if (maxX <= minX || maxY <= minY) return 0.0;
            return (maxX - minX) * (maxY - minY) / 1e9;
          };
          auto mstartScore = [&](std::size_t cross, std::size_t nodeHits) -> double {
            return static_cast<double>(cross)
              + mstartEdgeNodeWeight * static_cast<double>(nodeHits)
              + (mstartBboxWeight != 0.0 ? mstartBboxWeight * mstartBboxAreaB() : 0.0);
          };
          auto mstartStraightDrawingIsProper = [&]() -> bool {
            constexpr double kPointTolerance = 1e-6;
            constexpr double kParallelTolerance = 0.01;
            auto point = [&](std::size_t nodeIndex) {
              return RoutePoint{
                attributes.x(nodes[nodeIndex].handle),
                attributes.y(nodes[nodeIndex].handle),
              };
            };
            auto pointOnOpenSegment = [&](const RoutePoint& candidate,
                                          const RoutePoint& start,
                                          const RoutePoint& end) {
              const double dx = end.x - start.x;
              const double dy = end.y - start.y;
              const double lengthSquared = dx * dx + dy * dy;
              if (lengthSquared <= kPointTolerance) return true;
              const double px = candidate.x - start.x;
              const double py = candidate.y - start.y;
              const double distance = std::abs(crossProduct(dx, dy, px, py))
                / std::sqrt(lengthSquared);
              const double projection = px * dx + py * dy;
              return distance <= kPointTolerance
                && projection > kPointTolerance
                && projection < lengthSquared - kPointTolerance;
            };

            for (std::size_t leftNode = 0;
                 leftNode < nodes.size();
                 ++leftNode) {
              const RoutePoint leftPoint = point(leftNode);
              for (std::size_t rightNode = leftNode + 1;
                   rightNode < nodes.size();
                   ++rightNode) {
                const RoutePoint rightPoint = point(rightNode);
                if (
                    std::abs(leftPoint.x - rightPoint.x) <= kPointTolerance
                    && std::abs(leftPoint.y - rightPoint.y) <= kPointTolerance) {
                  return false;
                }
              }
            }

            for (const auto& edge : mstartEdgePairs) {
              const RoutePoint start = point(edge.first);
              const RoutePoint end = point(edge.second);
              if (
                  !std::isfinite(start.x) || !std::isfinite(start.y)
                  || !std::isfinite(end.x) || !std::isfinite(end.y)
                  || (std::abs(start.x - end.x) <= kPointTolerance
                    && std::abs(start.y - end.y) <= kPointTolerance)) {
                return false;
              }
              for (std::size_t nodeIndex = 0;
                   nodeIndex < nodes.size();
                   ++nodeIndex) {
                if (nodeIndex == edge.first || nodeIndex == edge.second) continue;
                if (pointOnOpenSegment(point(nodeIndex), start, end)) return false;
              }
            }

            for (std::size_t leftIndex = 0;
                 leftIndex < mstartEdgePairs.size();
                 ++leftIndex) {
              const auto& left = mstartEdgePairs[leftIndex];
              const RoutePoint leftStart = point(left.first);
              const RoutePoint leftEnd = point(left.second);
              const double leftDx = leftEnd.x - leftStart.x;
              const double leftDy = leftEnd.y - leftStart.y;
              for (std::size_t rightIndex = leftIndex + 1;
                   rightIndex < mstartEdgePairs.size();
                   ++rightIndex) {
                const auto& right = mstartEdgePairs[rightIndex];
                const bool adjacent =
                  left.first == right.first
                  || left.first == right.second
                  || left.second == right.first
                  || left.second == right.second;
                const RoutePoint rightStart = point(right.first);
                const RoutePoint rightEnd = point(right.second);
                const double rightDx = rightEnd.x - rightStart.x;
                const double rightDy = rightEnd.y - rightStart.y;
                const double denominator =
                  crossProduct(leftDx, leftDy, rightDx, rightDy);
                if (adjacent) {
                  if (std::abs(denominator) > kParallelTolerance) continue;
                  const std::size_t shared =
                    left.first == right.first || left.first == right.second
                      ? left.first
                      : left.second;
                  const std::size_t leftOther = left.first == shared
                    ? left.second : left.first;
                  const std::size_t rightOther = right.first == shared
                    ? right.second : right.first;
                  const RoutePoint sharedPoint = point(shared);
                  const RoutePoint leftOtherPoint = point(leftOther);
                  const RoutePoint rightOtherPoint = point(rightOther);
                  const double leftVectorX = leftOtherPoint.x - sharedPoint.x;
                  const double leftVectorY = leftOtherPoint.y - sharedPoint.y;
                  const double rightVectorX = rightOtherPoint.x - sharedPoint.x;
                  const double rightVectorY = rightOtherPoint.y - sharedPoint.y;
                  if (leftVectorX * rightVectorX + leftVectorY * rightVectorY > 0.0) {
                    return false;
                  }
                  continue;
                }
                if (std::abs(denominator) <= kParallelTolerance) {
                  if (
                      pointOnOpenSegment(leftStart, rightStart, rightEnd)
                      || pointOnOpenSegment(leftEnd, rightStart, rightEnd)
                      || pointOnOpenSegment(rightStart, leftStart, leftEnd)
                      || pointOnOpenSegment(rightEnd, leftStart, leftEnd)) {
                    return false;
                  }
                  const bool boundingBoxesOverlap =
                    std::max(std::min(leftStart.x, leftEnd.x),
                      std::min(rightStart.x, rightEnd.x))
                      <= std::min(std::max(leftStart.x, leftEnd.x),
                        std::max(rightStart.x, rightEnd.x)) + kPointTolerance
                    && std::max(std::min(leftStart.y, leftEnd.y),
                      std::min(rightStart.y, rightEnd.y))
                      <= std::min(std::max(leftStart.y, leftEnd.y),
                        std::max(rightStart.y, rightEnd.y)) + kPointTolerance;
                  if (boundingBoxesOverlap) return false;
                  continue;
                }
                const double qpx = rightStart.x - leftStart.x;
                const double qpy = rightStart.y - leftStart.y;
                const double leftParameter =
                  crossProduct(qpx, qpy, rightDx, rightDy) / denominator;
                const double rightParameter =
                  crossProduct(qpx, qpy, leftDx, leftDy) / denominator;
                const bool intersectsClosed =
                  leftParameter >= -kPointTolerance
                  && leftParameter <= 1.0 + kPointTolerance
                  && rightParameter >= -kPointTolerance
                  && rightParameter <= 1.0 + kPointTolerance;
                const bool intersectsProperly =
                  leftParameter > kPointTolerance
                  && leftParameter < 1.0 - kPointTolerance
                  && rightParameter > kPointTolerance
                  && rightParameter < 1.0 - kPointTolerance;
                if (intersectsClosed && !intersectsProperly) return false;
              }
            }
            return true;
          };
          auto mstartReachedCertifiedFloor = [&](
              std::size_t cross,
              std::size_t nodeHits) -> bool {
            if (
                !canonicalCrossing.available
                || mstartBundleAware
                || mstartBboxWeight != 0.0
                || (mstartEdgeNodeWeight != 0.0 && nodeHits != 0)
                || mstartEdgePairs.size() != canonicalCrossing.edgeCount
                || cross != canonicalCrossing.lowerBound) {
              return false;
            }
            const bool reached = mstartStraightDrawingIsProper();
            if (!reached) {
              std::fprintf(stderr,
                "[multistart] canonical floor candidate rejected by "
                "point-drawing guard (lowerBound=%zu).\n",
                canonicalCrossing.lowerBound);
            }
            return reached;
          };

          // Progressive rendering: on each new-best, dump current node
          // positions to DJERD_PROGRESS_FILE so the extension can stream an
          // intermediate preview to the webview (straight-edge, pre-route).
          // Atomic write (.tmp + rename) so the fs.watch never reads a partial
          // file. No-op when the env var is unset.
          const char* progressFile = std::getenv("DJERD_PROGRESS_FILE");
          auto writeProgress = [&](int run, int seed, std::size_t cross) {
            if (!progressFile || !*progressFile) return;
            const std::string tmp = std::string(progressFile) + ".tmp";
            std::FILE* pf = std::fopen(tmp.c_str(), "w");
            if (!pf) return;
            std::fprintf(pf,
              "{\"run\":%d,\"seed\":%d,\"crossings\":%zu,\"positions\":{",
              run, seed, cross);
            bool first = true;
            for (const NodeRecord& nd : nodes) {
              // modelIds are TSV-derived identifiers ([A-Za-z0-9_.:]) — no
              // JSON-special chars, so they need no escaping. OGDF attributes
              // are node CENTRES; emit TOP-LEFT (centre − size/2) to match the
              // final layout JSON convention (io.cpp:381) so the webview can
              // use these as basePosition/manualPosition directly.
              std::fprintf(pf, "%s\"%s\":[%.1f,%.1f]",
                first ? "" : ",", nd.modelId.c_str(),
                attributes.x(nd.handle) - nd.width / 2.0,
                attributes.y(nd.handle) - nd.height / 2.0);
              first = false;
            }
            std::fprintf(pf, "}}");
            std::fclose(pf);
            std::rename(tmp.c_str(), progressFile);
          };

          std::size_t bestCross = mstartCountCrossings();
          std::size_t bestNodeHits = mstartCountNodeIntersections();
          double bestScore = mstartScore(bestCross, bestNodeHits);
          auto bestPositions = mstartSavePositions();
          ClusterGraphResult bestCg = cg;
          int bestSeed = -1;  // -1 = initial run (no explicit setSeed)
          int completedRuns = 1;
          bool certifiedFloorReached = mstartReachedCertifiedFloor(
            bestCross,
            bestNodeHits);
          std::fprintf(stderr,
            "[multistart] run 0 (initial) crossings=%zu nodeHits=%zu score=%.1f\n",
            bestCross, bestNodeHits, bestScore);
          writeProgress(0, -1, bestCross);
          if (certifiedFloorReached) {
            std::fprintf(stderr,
              "[multistart] certified canonical crossing floor reached "
              "at run 0 (lowerBound=%zu); stopping early.\n",
              canonicalCrossing.lowerBound);
          }

          for (int run = 1;
               run < multistartRuns && !certifiedFloorReached;
               ++run) {
            const int seed = seedBase + run;
            mstartRestorePositions(mstartPreCgPositions);
            ogdf::setSeed(seed);
            // FMMMLayout has its own m_randSeed independent of the
            // OGDF global RNG. Without this env hand-off, every run
            // produces identical FMMM placements (confirmed by 4
            // runs all yielding crossings=7734 on Captain).
            ::setenv("DJERD_FMMM_SEED", std::to_string(seed).c_str(), 1);
            ClusterGraphResult altCg = runClusterGraphLayout(
              nodes, edges, labels, attributes, arguments.bubble);
            const std::size_t altCross = mstartCountCrossings();
            const std::size_t altNodeHits = mstartCountNodeIntersections();
            const double altScore = mstartScore(altCross, altNodeHits);
            const bool altIsBetter =
              altScore < bestScore - 1e-9
              || (std::abs(altScore - bestScore) <= 1e-9
                && (altNodeHits < bestNodeHits
                  || (altNodeHits == bestNodeHits && altCross < bestCross)));
            ++completedRuns;
            std::fprintf(stderr,
              "[multistart] run %d seed=%d crossings=%zu nodeHits=%zu "
              "score=%.1f%s\n",
              run, seed, altCross, altNodeHits, altScore,
              altIsBetter ? " (new best)" : "");
            if (altIsBetter) {
              bestScore = altScore;
              bestCross = altCross;
              bestNodeHits = altNodeHits;
              bestPositions = mstartSavePositions();
              bestCg = altCg;
              bestSeed = seed;
              writeProgress(run, seed, altCross);
              certifiedFloorReached = mstartReachedCertifiedFloor(
                bestCross,
                bestNodeHits);
              if (certifiedFloorReached) {
                std::fprintf(stderr,
                  "[multistart] certified canonical crossing floor reached "
                  "at run %d (lowerBound=%zu); stopping early.\n",
                  run,
                  canonicalCrossing.lowerBound);
              }
            }
          }
          mstartRestorePositions(bestPositions);
          cg = bestCg;
          // Re-seed FMMM env to whatever produced the best run, in
          // case downstream code inside this process re-runs FMMM
          // (e.g., on a clone for a metric). bestSeed=-1 means the
          // initial run (no DJERD_FMMM_SEED was set) — unset the env
          // in that case so the FMMM default (100) takes over again.
          if (bestSeed < 0) {
            ::unsetenv("DJERD_FMMM_SEED");
          } else {
            ::setenv("DJERD_FMMM_SEED", std::to_string(bestSeed).c_str(), 1);
          }
          std::fprintf(stderr,
            "[multistart] selected run with crossings=%zu nodeHits=%zu score=%.1f "
            "(seed=%d, runs=%d, edgeNodeWeight=%.1f, bboxWeight=%.1f)\n",
            bestCross, bestNodeHits, bestScore, bestSeed, completedRuns,
            mstartEdgeNodeWeight, mstartBboxWeight);
          if (!metadata.strategyReason.empty()) metadata.strategyReason += "; ";
          metadata.strategyReason +=
            "multistart selected best of "
            + std::to_string(completedRuns) + " runs";
        } else if (multistartRuns > 1) {
          std::fprintf(stderr,
            "[multistart] skipped (%d runs) — positions-tsv override "
            "discards computed positions\n", multistartRuns);
        }
        for (const auto& c : cg.clusters) {
          metadata.clusterByModelId[nodes[c.rootIdx].modelId] = c.clusterId;
          // Include ALL cluster members (not just root) so downstream
          // tooling (ML cluster-rigid polish) can identify membership.
          for (const auto& m : c.members) {
            metadata.clusterByModelId[nodes[m.nodeIdx].modelId] = c.clusterId;
          }
        }

        // Optional: override positions from external TSV (ML polish round-trip).
        // Format: each line "modelId\tcenterX\tcenterY". Lines without a known
        // modelId are skipped. Post-passes (leaf-untangle, xings-detour,
        // visual-knot, face-untangle, etc.) re-run on these positions.
        if (!arguments.positionsTsv.empty()) {
          std::ifstream pf(arguments.positionsTsv);
          if (!pf) {
            throw std::runtime_error(
              "failed to open --positions-tsv file: " + arguments.positionsTsv);
          }
          std::unordered_map<std::string, std::size_t> idIdx;
          idIdx.reserve(nodes.size());
          for (std::size_t i = 0; i < nodes.size(); ++i) {
            idIdx[nodes[i].modelId] = i;
          }
          std::string line;
          std::size_t applied = 0;
          while (std::getline(pf, line)) {
            if (line.empty()) continue;
            const auto t1 = line.find('\t');
            if (t1 == std::string::npos) continue;
            const auto t2 = line.find('\t', t1 + 1);
            if (t2 == std::string::npos) continue;
            const std::string mid = line.substr(0, t1);
            const std::string sx = line.substr(t1 + 1, t2 - t1 - 1);
            const std::string sy = line.substr(t2 + 1);
            auto it = idIdx.find(mid);
            if (it == idIdx.end()) continue;
            try {
              const double cx = std::stod(sx);
              const double cy = std::stod(sy);
              attributes.x(nodes[it->second].handle) = cx;
              attributes.y(nodes[it->second].handle) = cy;
              ++applied;
            } catch (const std::exception&) {
              continue;
            }
          }
          std::fprintf(stderr,
            "[ml-positions] Overrode %zu/%zu node positions from %s.\n",
            applied, nodes.size(), arguments.positionsTsv.c_str());
        }

        // Cluster membership is also required by position-override reroutes.
        // Their fast path skips spine positioning but must still run the
        // crossing relocation pass on the freshly computed communities.
        for (const auto& c : cg.clusters) {
          for (const auto& m : c.members) {
            if (m.nodeIdx < nodes.size()) {
              clusterByModelIdFull[nodes[m.nodeIdx].modelId] = c.clusterId;
            }
          }
        }

        // Capture spine state for post-pass flatten.
        spineRootIdxs = cg.mainRingNodeIdxs;
        spineRowOfRoot = cg.mainRingRowOfNode;
        if (!spineRootIdxs.empty()) {
          std::unordered_map<std::size_t, std::size_t> owner;
          for (const auto& c : cg.clusters) {
            for (const auto& m : c.members) owner[m.nodeIdx] = c.rootIdx;
          }
          std::unordered_map<std::size_t, std::size_t> immParent;
          for (const auto& p : cg.prunedNodes) immParent[p.nodeIdx] = p.parentIdx;
          auto coreAnchor = [&](std::size_t v) {
            int hops = 0;
            while (hops++ < 200) {
              auto it = immParent.find(v);
              if (it == immParent.end() || it->second == v) break;
              v = it->second;
            }
            return v;
          };
          for (const auto& p : cg.prunedNodes) {
            if (p.isAloneRoot) continue;
            if (owner.count(p.nodeIdx)) continue;
            auto a = coreAnchor(p.nodeIdx);
            auto it = owner.find(a);
            if (it != owner.end()) owner[p.nodeIdx] = it->second;
          }
          std::unordered_set<std::size_t> spineSet(
            spineRootIdxs.begin(), spineRootIdxs.end());
          for (const auto& kv : owner) {
            if (spineSet.count(kv.second)) {
              spineOwnedPairs.emplace_back(kv.second, kv.first);
            } else {
              nonSpineOwnedPairs.emplace_back(kv.second, kv.first);
            }
          }

          // Compute primary spine-hub for each non-spine cluster. Edge
          // count to each spine hub, weighted: direct root-root + each
          // connector linking the two clusters' roots.
          std::unordered_map<std::string, std::size_t> cidToRootP;
          for (const auto& c : cg.clusters) cidToRootP[c.clusterId] = c.rootIdx;
          std::unordered_map<std::size_t,
            std::unordered_map<std::size_t, std::size_t>> hubEdgeCount;
          for (const auto& c : cg.clusters) {
            if (spineSet.count(c.rootIdx)) continue;
            // (using djerd::adj would require exposing — recompute simple
            // adjacency from edges here.)
          }
          // Recompute adjacency once (cluster_graph already built it but
          // doesn't expose). Cheaper: scan edges.
          std::unordered_map<std::string, std::size_t> idToIdx;
          for (std::size_t i = 0; i < nodes.size(); ++i) idToIdx[nodes[i].modelId] = i;
          std::vector<std::set<std::size_t>> adjLocal(nodes.size());
          for (const auto& e : edges) {
            auto sIt = idToIdx.find(e.sourceModelId);
            auto tIt = idToIdx.find(e.targetModelId);
            if (sIt == idToIdx.end() || tIt == idToIdx.end()) continue;
            if (sIt->second == tIt->second) continue;
            adjLocal[sIt->second].insert(tIt->second);
            adjLocal[tIt->second].insert(sIt->second);
          }
          // Direct root-root: cluster root → spine hub
          for (const auto& c : cg.clusters) {
            if (spineSet.count(c.rootIdx)) continue;
            for (std::size_t j : adjLocal[c.rootIdx]) {
              if (spineSet.count(j)) ++hubEdgeCount[c.rootIdx][j];
            }
          }
          // Connector-mediated: connector links two cluster roots.
          for (const auto& con : cg.connectors) {
            if (con.connectedClusterIds.size() != 2) continue;
            auto a = cidToRootP.find(con.connectedClusterIds[0]);
            auto b = cidToRootP.find(con.connectedClusterIds[1]);
            if (a == cidToRootP.end() || b == cidToRootP.end()) continue;
            const bool aSp = spineSet.count(a->second) > 0;
            const bool bSp = spineSet.count(b->second) > 0;
            if (aSp && !bSp) ++hubEdgeCount[b->second][a->second];
            else if (bSp && !aSp) ++hubEdgeCount[a->second][b->second];
          }
          for (const auto& kv : hubEdgeCount) {
            std::size_t primary = std::numeric_limits<std::size_t>::max();
            std::size_t bestCount = 0;
            for (const auto& sub : kv.second) {
              if (sub.second > bestCount
                  || (sub.second == bestCount && sub.first < primary)) {
                bestCount = sub.second;
                primary = sub.first;
              }
            }
            if (primary != std::numeric_limits<std::size_t>::max()) {
              nonSpineClusterPrimary.emplace_back(kv.first, primary);
            }
          }

          // Capture leaf matrix groups: each group's leaves share an
          // anchor port near the parent. Used by edge routing AND
          // exposed in JSON so the renderer can draw the matrix as a
          // single grouped node with one collective edge to the parent.
          // Bundles are populated AFTER all post-passes (spine flatten,
          // ESS, cluster outliers) so leaf positions reflect the final
          // layout. We store nodeIdx temporarily and resolve to model
          // IDs + bbox just before writeLayoutJson.
          for (const auto& g : cg.leafMatrixGroups) {
            for (std::size_t leaf : g.leafIdxs) {
              leafAnchorMap[leaf] = {g.parentIdx, g.anchorX, g.anchorY};
            }
            RawLeafGroup raw;
            raw.parentIdx = g.parentIdx;
            raw.leafIdxs = g.leafIdxs;
            raw.sharedRootIdxs = g.sharedRootIdxs;
            raw.anchorX = g.anchorX;
            raw.anchorY = g.anchorY;
            rawLeafGroups.push_back(std::move(raw));
          }

          // Capture connector/router → connected cluster roots so the
          // post-pass can re-snap connectors to the midpoint of A–B and
          // routers to the centroid of their connected hubs, AFTER
          // spine flatten reshapes hub positions. Section 9b2's
          // untangling ran before spine flatten and is now stale.
          for (const auto& con : cg.connectors) {
            std::vector<std::size_t> roots;
            for (const std::string& cid : con.connectedClusterIds) {
              auto it = cidToRootP.find(cid);
              if (it != cidToRootP.end()) roots.push_back(it->second);
            }
            if (roots.size() == 2) {
              connectorRoots.emplace_back(con.nodeIdx, std::move(roots));
            }
          }
          for (const auto& rtr : cg.routers) {
            std::vector<std::size_t> roots;
            for (const std::string& cid : rtr.connectedClusterIds) {
              auto it = cidToRootP.find(cid);
              if (it != cidToRootP.end()) roots.push_back(it->second);
            }
            if (roots.size() >= 2) {
              routerRoots.emplace_back(rtr.nodeIdx, std::move(roots));
            }
          }
        }
        metadata.actualMode = arguments.mode;
        metadata.actualAlgorithm = "ClusterGraphLayout(louvainClusters="
          + std::to_string(cg.clusters.size())
          + ", connectors=" + std::to_string(cg.connectors.size())
          + ", routers=" + std::to_string(cg.routers.size())
          + ", constellations=" + std::to_string(cg.constellations.size())
          + ", polars=" + std::to_string(cg.polars.size())
          + ", polarSkelEdges=" + std::to_string(cg.polarSkeletonEdgeCount)
          + ", polarRings=" + std::to_string(cg.polarRingCount)
          + ", polarLines=" + std::to_string(cg.polarLineCount)
          + ", superPolars=" + std::to_string(cg.superPolars.size())
          + ", superPolarMetaEdges=" + std::to_string(cg.superPolarMetaEdgeCount)
          + ", superPolarTopology=" + (cg.superPolarTopology.empty() ? std::string("n/a") : cg.superPolarTopology)
          + ", rings=" + std::to_string(cg.ringCount)
          + ", independents=" + std::to_string(cg.independentNodeIndices.size())
          + ", singletonClusters=" + std::to_string(cg.singletonClusterCount)
          + ", spuriousClusters=" + std::to_string(cg.spuriousClusterCount)
          + ", pruned=" + std::to_string(cg.prunedNodes.size())
          + ", pruneLevels=" + std::to_string(cg.maxPruningLevel)
          + ", coreNodes=" + std::to_string(cg.coreNodeCount)
          + ", aloneRoots=" + std::to_string(cg.aloneRootCount)
          + ", topLevelEdges=" + std::to_string(cg.topLevelEdgeCount)
          + ", dedupedEdges=" + std::to_string(cg.deduplicatedEdges) + ")";
        metadata.strategy = "cluster_graph";
        metadata.strategyReason = cg.strategyReason;
      } else {
        metadata = runLayout(arguments.mode, nodes, edges, attributes);
      }
    }
    metadata.canonicalCrossing = canonicalCrossing;

    sanitizeLayoutGeometry(nodes, edges, attributes);

    // Optional stress majorization (Gansner et al. 2005) post-pass.
    // Refines node positions to minimize Σ w_ij (||p_i - p_j|| - L·d_ij)²
    // — the canonical force-directed quality metric. Run with FEW
    // iterations (env DJERD_STRESS_POST_PASS_ITERS, default 0 = off) so
    // the cluster structure built by cluster_graph is preserved while
    // local positions move toward stress-optimal.
    //
    // Only fires when cluster_graph actually engaged (strategy field
    // confirms it). Small graphs that fall back to other layouts (e.g.
    // Sugiyama) skip stress because the synth result (44→150 cross)
    // shows stress is harmful when the underlying structure is too
    // simple for cluster_graph + post-pass untangle to recover.
    {
      const char* stressItersEnv = std::getenv("DJERD_STRESS_POST_PASS_ITERS");
      const int stressIters = stressItersEnv ? std::atoi(stressItersEnv) : 0;
      const bool clusterGraphEngaged =
        metadata.strategy == "cluster_graph"
        || metadata.strategy == "bubble";
      if (stressIters > 0 && clusterGraphEngaged) {
        const char* stressEdgeCostEnv =
          std::getenv("DJERD_STRESS_POST_PASS_EDGE_COST");
        const double stressEdgeCost = stressEdgeCostEnv
          ? std::max(1.0, std::atof(stressEdgeCostEnv))
          : 140.0;
        ogdf::StressMinimization stressLayout;
        stressLayout.hasInitialLayout(true);
        stressLayout.setIterations(stressIters);
        stressLayout.setEdgeCosts(stressEdgeCost);
        stressLayout.layoutComponentsSeparately(true);
        stressLayout.call(attributes);
        sanitizeLayoutGeometry(nodes, edges, attributes);
        std::fprintf(stderr,
          "[stress-post-pass] applied StressMinimization "
          "(iterations=%d, edgeCost=%.1f, nodes=%zu, strategy=%s).\n",
          stressIters, stressEdgeCost, nodes.size(),
          metadata.strategy.c_str());
        if (!metadata.strategyReason.empty()) {
          metadata.strategyReason += "; ";
        }
        metadata.strategyReason +=
          "stress majorization post-pass ("
          + std::to_string(stressIters) + " iterations)";
      } else if (stressIters > 0) {
        std::fprintf(stderr,
          "[stress-post-pass] skipped (strategy=%s, not cluster_graph).\n",
          metadata.strategy.c_str());
      }
    }

    if (compactExcessiveLayoutFootprint(arguments.mode, nodes, edges, attributes)) {
      if (!metadata.strategyReason.empty()) {
        metadata.strategyReason += "; ";
      }
      metadata.strategyReason += "post-layout footprint compaction capped oversized axes";
    }
    // cluster_graph/bubble place members in cluster bubbles with care; the
    // generic "pull distant neighbours together" / "compact outliers" passes
    // fight that placement and pull boundary members across cluster
    // territories, creating cross-cluster node overlaps. Skip those passes
    // for cluster_graph/bubble to preserve the placement.
    const bool clusterModeFlag = arguments.clusterGraph || arguments.bubble;
    const bool preserveDisconnectedComponentPositions =
      !arguments.positionsTsv.empty()
      && readBoolEnv(
        "DJERD_PRESERVE_DISCONNECTED_COMPONENT_POSITIONS",
        false);
    if (!clusterModeFlag) {
      compactDistantConnectedNodes(nodes, edges, attributes);
    }
    enforceNodeSeparationStrong(nodes, attributes);
    if (!preserveDisconnectedComponentPositions) {
      packDisconnectedComponents(nodes, edges, attributes);
    }
    enforceNodeSeparationStrong(nodes, attributes);
    enforceNodeSeparationStrong(nodes, attributes);
    if (isStraightLineRoutingMode(arguments.mode)) {
      refineStraightHubAxisLayout(nodes, edges, attributes);
      if (!preserveDisconnectedComponentPositions) {
        packDisconnectedComponents(nodes, edges, attributes);
      }
      enforceNodeSeparationStrong(nodes, attributes);
    } else if (isConstrainedForceMode(arguments.mode)) {
      refineConstrainedForceLayout(nodes, edges, attributes);
      enforceNodeSeparationStrong(nodes, attributes);
    }
    sanitizeLayoutGeometry(nodes, edges, attributes);
    if (!clusterModeFlag) {
      compactClusterOutliers(nodes, metadata.clusterByModelId, attributes, 1.8);
    }
    enforceNodeSeparationStrong(nodes, attributes);
    sanitizeLayoutGeometry(nodes, edges, attributes);

    // Final spine flatten (cluster_graph/bubble only): post-passes
    // (compactDistant, enforceNodeSeparation, packDisconnected, etc.)
    // drift backbone hubs off the §3.10 multi-row spine. Re-pin every
    // spine root's y to its row-mate average and translate its owned
    // tree (members + transitive pruned descendants) by the same dy so
    // leaves stay attached to the hub. Without per-row data the flatten
    // collapses everything onto a single line.
    if (!spineRootIdxs.empty()) {
      // Group roots by row.
      std::unordered_map<std::size_t, std::vector<std::size_t>> rowsToRoots;
      const bool useRows = !spineRowOfRoot.empty()
        && spineRowOfRoot.size() == spineRootIdxs.size();
      for (std::size_t i = 0; i < spineRootIdxs.size(); ++i) {
        const std::size_t row = useRows ? spineRowOfRoot[i] : 0;
        rowsToRoots[row].push_back(spineRootIdxs[i]);
      }
      // Compute target y per row (avg current y).
      std::unordered_map<std::size_t, double> rowTargetY;
      for (const auto& kv : rowsToRoots) {
        double sum = 0.0;
        std::size_t cnt = 0;
        for (std::size_t r : kv.second) {
          if (r >= nodes.size()) continue;
          sum += attributes.y(nodes[r].handle);
          ++cnt;
        }
        if (cnt > 0) rowTargetY[kv.first] = sum / static_cast<double>(cnt);
      }
      // Pin each root's owned tree to its row's target y.
      std::unordered_map<std::size_t, std::vector<std::size_t>> ownedByRoot;
      for (const auto& kv : spineOwnedPairs) {
        ownedByRoot[kv.first].push_back(kv.second);
      }
      for (std::size_t i = 0; i < spineRootIdxs.size(); ++i) {
        const std::size_t r = spineRootIdxs[i];
        if (r >= nodes.size()) continue;
        const std::size_t row = useRows ? spineRowOfRoot[i] : 0;
        auto rt = rowTargetY.find(row);
        if (rt == rowTargetY.end()) continue;
        const double targetY = rt->second;
        const double rootY = attributes.y(nodes[r].handle);
        const double dy = targetY - rootY;
        if (std::abs(dy) < 1e-2) continue;
        attributes.y(nodes[r].handle) += dy;
        auto ot = ownedByRoot.find(r);
        if (ot != ownedByRoot.end()) {
          for (std::size_t n : ot->second) {
            if (n == r || n >= nodes.size()) continue;
            attributes.y(nodes[n].handle) += dy;
          }
        }
      }

    }

    // Edge-aware connector/router untangling: now that all hub positions
    // are final (spine flatten + post-passes done), re-snap each
    // connector to the exact midpoint of its 2 cluster roots and each
    // router to the centroid of its connected roots. Section §9b2 ran
    // before main.cpp post-passes and any subsequent shift puts
    // connectors off-line, kinking the A–C–B path and creating
    // unnecessary crossings.
    //
    // After the snap, push-off any cluster root the connector now
    // overlaps (= a hub bubble between A and B along the axis).
    // Without push-off, the snap creates ~140 overlaps where connectors
    // land on top of intermediate hubs.
    if (!connectorRoots.empty() || !routerRoots.empty()) {
      // Build cluster-root index set (= nodes that own a cluster bubble).
      std::unordered_set<std::size_t> clusterRootSet;
      std::unordered_map<std::string, std::size_t> idToIdxLocal;
      for (std::size_t i = 0; i < nodes.size(); ++i) {
        idToIdxLocal[nodes[i].modelId] = i;
      }
      for (const auto& kv : metadata.clusterByModelId) {
        auto it = idToIdxLocal.find(kv.first);
        if (it != idToIdxLocal.end()) clusterRootSet.insert(it->second);
      }
      auto pushOffClusters = [&](std::size_t cIdx,
                                 std::size_t rA, std::size_t rB,
                                 double axisDx, double axisDy) {
        const double axisLen = std::sqrt(axisDx * axisDx + axisDy * axisDy);
        if (axisLen < 1e-3) return;
        const double perpX = -axisDy / axisLen;
        const double perpY = axisDx / axisLen;
        const double cw = attributes.width(nodes[cIdx].handle) / 2.0;
        const double ch = attributes.height(nodes[cIdx].handle) / 2.0;
        constexpr double kPad = 12.0;
        for (std::size_t r : clusterRootSet) {
          if (r == rA || r == rB || r >= nodes.size()) continue;
          const double rx = attributes.x(nodes[r].handle);
          const double ry = attributes.y(nodes[r].handle);
          const double rw = attributes.width(nodes[r].handle) / 2.0;
          const double rh = attributes.height(nodes[r].handle) / 2.0;
          const double cxNow = attributes.x(nodes[cIdx].handle);
          const double cyNow = attributes.y(nodes[cIdx].handle);
          const double dxc = cxNow - rx;
          const double dyc = cyNow - ry;
          const double reqX = cw + rw + kPad;
          const double reqY = ch + rh + kPad;
          if (std::abs(dxc) >= reqX || std::abs(dyc) >= reqY) continue;
          const double overX = reqX - std::abs(dxc);
          const double overY = reqY - std::abs(dyc);
          const double over = std::min(overX, overY);
          const double sign = (dxc * perpX + dyc * perpY) >= 0.0 ? 1.0 : -1.0;
          attributes.x(nodes[cIdx].handle) += sign * perpX * over;
          attributes.y(nodes[cIdx].handle) += sign * perpY * over;
        }
      };

      for (const auto& cr : connectorRoots) {
        const std::size_t cIdx = cr.first;
        const auto& roots = cr.second;
        if (cIdx >= nodes.size() || roots.size() != 2) continue;
        if (roots[0] >= nodes.size() || roots[1] >= nodes.size()) continue;
        const double Ax = attributes.x(nodes[roots[0]].handle);
        const double Ay = attributes.y(nodes[roots[0]].handle);
        const double Bx = attributes.x(nodes[roots[1]].handle);
        const double By = attributes.y(nodes[roots[1]].handle);
        const double mx = 0.5 * (Ax + Bx);
        const double my = 0.5 * (Ay + By);
        attributes.x(nodes[cIdx].handle) = mx;
        attributes.y(nodes[cIdx].handle) = my;
        pushOffClusters(cIdx, roots[0], roots[1], Bx - Ax, By - Ay);
      }
      for (const auto& cr : routerRoots) {
        const std::size_t rIdx = cr.first;
        const auto& roots = cr.second;
        if (rIdx >= nodes.size() || roots.size() < 2) continue;
        double sumX = 0.0, sumY = 0.0;
        std::size_t cnt = 0;
        for (std::size_t r : roots) {
          if (r >= nodes.size()) continue;
          sumX += attributes.x(nodes[r].handle);
          sumY += attributes.y(nodes[r].handle);
          ++cnt;
        }
        if (cnt == 0) continue;
        attributes.x(nodes[rIdx].handle) = sumX / static_cast<double>(cnt);
        attributes.y(nodes[rIdx].handle) = sumY / static_cast<double>(cnt);
        // Push off using the dominant pair as the axis (first two roots).
        if (roots.size() >= 2 && roots[0] < nodes.size()
            && roots[1] < nodes.size()) {
          const double Ax = attributes.x(nodes[roots[0]].handle);
          const double Ay = attributes.y(nodes[roots[0]].handle);
          const double Bx = attributes.x(nodes[roots[1]].handle);
          const double By = attributes.y(nodes[roots[1]].handle);
          pushOffClusters(rIdx, roots[0], roots[1], Bx - Ax, By - Ay);
        }
      }

      // Resolve any remaining overlaps from the snap. ESS may shift
      // spine hubs slightly; we re-flatten the spine right after to
      // restore the row alignment.
      enforceNodeSeparationStrong(nodes, attributes);
      // Re-flatten spine after ESS shift (mirrors §spine flatten above).
      if (!spineRootIdxs.empty()) {
        std::unordered_map<std::size_t, std::vector<std::size_t>> rowsToRoots2;
        const bool useRows2 = !spineRowOfRoot.empty()
          && spineRowOfRoot.size() == spineRootIdxs.size();
        for (std::size_t i = 0; i < spineRootIdxs.size(); ++i) {
          const std::size_t row = useRows2 ? spineRowOfRoot[i] : 0;
          rowsToRoots2[row].push_back(spineRootIdxs[i]);
        }
        std::unordered_map<std::size_t, double> rowTargetY2;
        for (const auto& kv : rowsToRoots2) {
          double sum = 0.0;
          std::size_t cnt = 0;
          for (std::size_t r : kv.second) {
            if (r >= nodes.size()) continue;
            sum += attributes.y(nodes[r].handle);
            ++cnt;
          }
          if (cnt > 0) rowTargetY2[kv.first] = sum / static_cast<double>(cnt);
        }
        std::unordered_map<std::size_t, std::vector<std::size_t>> ownedByRoot2;
        for (const auto& kv : spineOwnedPairs) {
          ownedByRoot2[kv.first].push_back(kv.second);
        }
        for (std::size_t i = 0; i < spineRootIdxs.size(); ++i) {
          const std::size_t r = spineRootIdxs[i];
          if (r >= nodes.size()) continue;
          const std::size_t row = useRows2 ? spineRowOfRoot[i] : 0;
          auto rt = rowTargetY2.find(row);
          if (rt == rowTargetY2.end()) continue;
          const double targetY = rt->second;
          const double rootY = attributes.y(nodes[r].handle);
          const double dy = targetY - rootY;
          if (std::abs(dy) < 1e-2) continue;
          attributes.y(nodes[r].handle) += dy;
          auto ot = ownedByRoot2.find(r);
          if (ot != ownedByRoot2.end()) {
            for (std::size_t n : ot->second) {
              if (n == r || n >= nodes.size()) continue;
              attributes.y(nodes[n].handle) += dy;
            }
          }
        }
      }
    }

    // (C3) Final non-cluster-node clearance pass for cluster_graph/bubble.
    // Connectors get re-snapped to cluster-pair midpoints by the untangling
    // pass above, which can land them on top of a cluster member of a
    // third intervening cluster. The pushOffClusters path inside that
    // untangling only checks against cluster ROOTS; cluster MEMBERS are
    // unguarded. Walk every non-cluster node (= modelId not in
    // clusterByModelIdFull) and push it off any cluster-member rect it
    // overlaps. Runs after spine flatten + connector snap so nothing
    // can undo it. Set DJERD_NO_C3=1 to skip.
    const char* noC3Env = std::getenv("DJERD_NO_C3");
    const bool skipC3 = noC3Env && std::strcmp(noC3Env, "0") != 0;
    if (!skipC3 && (arguments.clusterGraph || arguments.bubble)
        && !clusterByModelIdFull.empty()) {
      std::unordered_map<std::string, std::size_t> idxByModelId;
      idxByModelId.reserve(nodes.size());
      for (std::size_t i = 0; i < nodes.size(); ++i) {
        idxByModelId[nodes[i].modelId] = i;
      }
      std::vector<std::size_t> clusterOwned;
      clusterOwned.reserve(clusterByModelIdFull.size());
      for (const auto& kv : clusterByModelIdFull) {
        auto it = idxByModelId.find(kv.first);
        if (it != idxByModelId.end()) clusterOwned.push_back(it->second);
      }
      std::vector<std::size_t> nonCluster;
      for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (!clusterByModelIdFull.count(nodes[i].modelId)) nonCluster.push_back(i);
      }
      double maxW = 1.0;
      double maxH = 1.0;
      for (std::size_t idx : clusterOwned) {
        maxW = std::max(maxW, nodes[idx].width);
        maxH = std::max(maxH, nodes[idx].height);
      }
      for (std::size_t idx : nonCluster) {
        maxW = std::max(maxW, nodes[idx].width);
        maxH = std::max(maxH, nodes[idx].height);
      }
      const double cellSize = std::max(maxW, maxH) * 1.2 + 16.0;
      auto pairHash = [](const std::pair<long long, long long>& p) {
        return std::hash<long long>()(p.first)
          ^ (std::hash<long long>()(p.second) << 1);
      };
      std::unordered_map<std::pair<long long, long long>,
                          std::vector<std::size_t>, decltype(pairHash)>
        bins(0, pairHash);
      auto binKey = [&](double x, double y) {
        return std::make_pair(
          static_cast<long long>(std::floor(x / cellSize)),
          static_cast<long long>(std::floor(y / cellSize)));
      };
      for (std::size_t idx : clusterOwned) {
        const NodeRecord& nd = nodes[idx];
        bins[binKey(attributes.x(nd.handle), attributes.y(nd.handle))]
          .push_back(idx);
      }
      constexpr double kPad = 8.0;
      constexpr int kMaxIters = 24;
      std::size_t pushed = 0;
      for (std::size_t niIdx : nonCluster) {
        const NodeRecord& nd = nodes[niIdx];
        double nx = attributes.x(nd.handle);
        double ny = attributes.y(nd.handle);
        const double nw = nd.width / 2.0;
        const double nh = nd.height / 2.0;
        bool changed = false;
        for (int iter = 0; iter < kMaxIters; ++iter) {
          bool moved = false;
          const auto k = binKey(nx, ny);
          for (long long dx = -1; dx <= 1; ++dx) {
            for (long long dy = -1; dy <= 1; ++dy) {
              auto bIt = bins.find({k.first + dx, k.second + dy});
              if (bIt == bins.end()) continue;
              for (std::size_t mi : bIt->second) {
                const NodeRecord& md = nodes[mi];
                const double mx = attributes.x(md.handle);
                const double my = attributes.y(md.handle);
                const double mw = md.width / 2.0;
                const double mh = md.height / 2.0;
                const double cdx = nx - mx;
                const double cdy = ny - my;
                const double reqDx = nw + mw + kPad;
                const double reqDy = nh + mh + kPad;
                if (std::abs(cdx) >= reqDx || std::abs(cdy) >= reqDy) continue;
                const double overX = reqDx - std::abs(cdx);
                const double overY = reqDy - std::abs(cdy);
                if (overX < overY) {
                  const double sign = cdx >= 0 ? 1.0 : -1.0;
                  nx += sign * (overX + 0.5);
                } else {
                  const double sign = cdy >= 0 ? 1.0 : -1.0;
                  ny += sign * (overY + 0.5);
                }
                moved = true;
                changed = true;
              }
            }
          }
          if (!moved) break;
        }
        if (changed) {
          attributes.x(nd.handle) = std::round(nx * 100.0) / 100.0;
          attributes.y(nd.handle) = std::round(ny * 100.0) / 100.0;
          ++pushed;
        }
      }
      if (pushed > 0) {
        std::fprintf(stderr,
          "[c3-pass] Pushed %zu non-cluster nodes off cluster members.\n",
          pushed);
      }
    }

    // Populate metadata.leafBundles using FINAL leaf positions (after
    // all post-passes including spine flatten + ESS). Anchor port = leaf
    // centroid → parent midpoint (computed fresh from current positions
    // so it always points at where the matrix actually settled). bbox
    // = axis-aligned rectangle covering all leaf positions + half-size
    // padding. Renderer can use bbox to draw a single grouping rectangle
    // and route every leaf→parent edge through anchor as a thick line.
    for (const auto& raw : rawLeafGroups) {
      if (raw.parentIdx >= nodes.size() || raw.leafIdxs.empty()) continue;
      LeafBundleRecord rec;
      rec.parentModelId = nodes[raw.parentIdx].modelId;
      rec.leafModelIds.reserve(raw.leafIdxs.size());
      // Populate sharedRootModelIds from sharedRootIdxs (multi-root for
      // bus bundles, single-root for classic leaf bundles).
      rec.sharedRootModelIds.reserve(raw.sharedRootIdxs.size());
      for (std::size_t r : raw.sharedRootIdxs) {
        if (r < nodes.size()) rec.sharedRootModelIds.push_back(nodes[r].modelId);
      }
      double minX = std::numeric_limits<double>::infinity();
      double minY = std::numeric_limits<double>::infinity();
      double maxX = -std::numeric_limits<double>::infinity();
      double maxY = -std::numeric_limits<double>::infinity();
      double sumLX = 0.0, sumLY = 0.0;
      std::size_t cnt = 0;
      for (std::size_t l : raw.leafIdxs) {
        if (l >= nodes.size()) continue;
        rec.leafModelIds.push_back(nodes[l].modelId);
        const double cx = attributes.x(nodes[l].handle);
        const double cy = attributes.y(nodes[l].handle);
        const double w = attributes.width(nodes[l].handle);
        const double h = attributes.height(nodes[l].handle);
        minX = std::min(minX, cx - w / 2.0);
        minY = std::min(minY, cy - h / 2.0);
        maxX = std::max(maxX, cx + w / 2.0);
        maxY = std::max(maxY, cy + h / 2.0);
        sumLX += cx;
        sumLY += cy;
        ++cnt;
      }
      if (cnt == 0 || minX == std::numeric_limits<double>::infinity()) continue;
      rec.bboxX = minX;
      rec.bboxY = minY;
      rec.bboxWidth = maxX - minX;
      rec.bboxHeight = maxY - minY;
      const double leafCx = sumLX / static_cast<double>(cnt);
      const double leafCy = sumLY / static_cast<double>(cnt);
      const double pX = attributes.x(nodes[raw.parentIdx].handle);
      const double pY = attributes.y(nodes[raw.parentIdx].handle);
      rec.anchorX = 0.5 * (pX + leafCx);
      rec.anchorY = 0.5 * (pY + leafCy);
      metadata.leafBundles.push_back(std::move(rec));
    }

    // --rigid-positions: when set together with --positions-tsv, the caller
    // wants the supplied node positions preserved as the primary ML answer.
    // Bypass the expensive post-pass stack below, but allow narrow visual
    // integrity fixes (bbox compaction and leaf-bundle/node clearance) when
    // explicitly enabled by env. Then route + measure + emit JSON.
    if (arguments.rigidPositions && !arguments.positionsTsv.empty()) {
      {
        std::ifstream pf(arguments.positionsTsv);
        if (pf) {
          std::unordered_map<std::string, std::size_t> idIdx;
          idIdx.reserve(nodes.size());
          for (std::size_t i = 0; i < nodes.size(); ++i) {
            idIdx[nodes[i].modelId] = i;
          }
          std::string line;
          while (std::getline(pf, line)) {
            if (line.empty()) continue;
            const auto t1 = line.find('\t');
            if (t1 == std::string::npos) continue;
            const auto t2 = line.find('\t', t1 + 1);
            if (t2 == std::string::npos) continue;
            const std::string mid = line.substr(0, t1);
            const std::string sxStr = line.substr(t1 + 1, t2 - t1 - 1);
            const std::string syStr = line.substr(t2 + 1);
            auto it = idIdx.find(mid);
            if (it == idIdx.end()) continue;
            try {
              attributes.x(nodes[it->second].handle) = std::stod(sxStr);
              attributes.y(nodes[it->second].handle) = std::stod(syStr);
            } catch (const std::exception&) {
              continue;
            }
          }
        }
      }
      (void)compactRigidLayoutFootprint(nodes, attributes);
      (void)attachIsolatedNodesByName(nodes, edges, attributes);
      (void)compactIsolatedBBoxOutliers(nodes, edges, attributes);
      (void)compactSidecarBBoxComponents(nodes, edges, attributes);
      // Refresh leafBundle bboxes/anchors against the rigid positions.
      {
        std::unordered_map<std::string, std::size_t> id2idxRigid;
        id2idxRigid.reserve(nodes.size());
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          id2idxRigid[nodes[i].modelId] = i;
        }
        for (auto& bundle : metadata.leafBundles) {
          double minX = std::numeric_limits<double>::infinity();
          double minY = std::numeric_limits<double>::infinity();
          double maxX = -std::numeric_limits<double>::infinity();
          double maxY = -std::numeric_limits<double>::infinity();
          double sumLX = 0.0, sumLY = 0.0;
          std::size_t cnt = 0;
          for (const std::string& leaf : bundle.leafModelIds) {
            auto it = id2idxRigid.find(leaf);
            if (it == id2idxRigid.end()) continue;
            const auto& nd = nodes[it->second];
            const double cx = attributes.x(nd.handle);
            const double cy = attributes.y(nd.handle);
            const double w = attributes.width(nd.handle);
            const double h = attributes.height(nd.handle);
            minX = std::min(minX, cx - w / 2.0);
            minY = std::min(minY, cy - h / 2.0);
            maxX = std::max(maxX, cx + w / 2.0);
            maxY = std::max(maxY, cy + h / 2.0);
            sumLX += cx; sumLY += cy; ++cnt;
          }
          if (cnt == 0 || !std::isfinite(minX)) continue;
          bundle.bboxX = minX;
          bundle.bboxY = minY;
          bundle.bboxWidth = maxX - minX;
          bundle.bboxHeight = maxY - minY;
          const double leafCx = sumLX / static_cast<double>(cnt);
          const double leafCy = sumLY / static_cast<double>(cnt);
          auto pit = id2idxRigid.find(bundle.parentModelId);
          if (pit != id2idxRigid.end()) {
            const double pX = attributes.x(nodes[pit->second].handle);
            const double pY = attributes.y(nodes[pit->second].handle);
            bundle.anchorX = 0.5 * (pX + leafCx);
            bundle.anchorY = 0.5 * (pY + leafCy);
          }
        }
      }
      (void)clearLeafBundleNodeMargins(metadata.leafBundles, nodes, attributes);
      // Routing + measurement.
      std::vector<std::vector<RoutePoint>> routes =
        routeAllEdgesStraight(edges, attributes);

      auto recomputeRigidLeafBundles = [&]() {
        std::unordered_map<std::string, std::size_t> id2idxRigid;
        id2idxRigid.reserve(nodes.size());
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          id2idxRigid[nodes[i].modelId] = i;
        }
        for (auto& bundle : metadata.leafBundles) {
          double minX = std::numeric_limits<double>::infinity();
          double minY = std::numeric_limits<double>::infinity();
          double maxX = -std::numeric_limits<double>::infinity();
          double maxY = -std::numeric_limits<double>::infinity();
          double sumLX = 0.0, sumLY = 0.0;
          std::size_t cnt = 0;
          for (const std::string& leaf : bundle.leafModelIds) {
            auto it = id2idxRigid.find(leaf);
            if (it == id2idxRigid.end()) continue;
            const auto& nd = nodes[it->second];
            const double cx = attributes.x(nd.handle);
            const double cy = attributes.y(nd.handle);
            const double w = attributes.width(nd.handle);
            const double h = attributes.height(nd.handle);
            minX = std::min(minX, cx - w / 2.0);
            minY = std::min(minY, cy - h / 2.0);
            maxX = std::max(maxX, cx + w / 2.0);
            maxY = std::max(maxY, cy + h / 2.0);
            sumLX += cx; sumLY += cy; ++cnt;
          }
          if (cnt == 0 || !std::isfinite(minX)) continue;
          bundle.bboxX = minX;
          bundle.bboxY = minY;
          bundle.bboxWidth = maxX - minX;
          bundle.bboxHeight = maxY - minY;
          const double leafCx = sumLX / static_cast<double>(cnt);
          const double leafCy = sumLY / static_cast<double>(cnt);
          auto pit = id2idxRigid.find(bundle.parentModelId);
          if (pit != id2idxRigid.end()) {
            const double pX = attributes.x(nodes[pit->second].handle);
            const double pY = attributes.y(nodes[pit->second].handle);
            bundle.anchorX = 0.5 * (pX + leafCx);
            bundle.anchorY = 0.5 * (pY + leafCy);
          }
        }
      };

      auto measureRigidQuality = [&]() {
        std::vector<std::vector<std::string>> ignoredIdsByEdge;
        std::size_t rawCrossings = 0;
        (void)detectRouteCrossings(edges, routes, ignoredIdsByEdge, rawCrossings);
        LayoutQualityMetrics q =
          measureLayoutQuality(nodes, edges, routes, attributes, &metadata.leafBundles);
        q.edgeCrossings = rawCrossings;
        const bool renderedCarrierMetricsApplied =
          applyRenderedCarrierMetricsIfRequested(
            nodes,
            edges,
            routes,
            attributes,
            clusterByModelIdFull,
            metadata,
            q,
            rawCrossings);
        if (!renderedCarrierMetricsApplied) {
          q.visualCrossings =
            q.edgeCrossings
            + q.edgeNodeIntersections
            + q.nodeOverlaps
            + q.bundleEdgeIntersections
            + q.bundleNodeOverlaps;
        }
        return q;
      };

      auto rigidScore = [](
          const LayoutQualityMetrics& q,
          double baseArea,
          double bundleNodeWeight) {
        const double bboxGrowth =
          (baseArea > 0.0 && q.boundingBoxArea > baseArea)
            ? (q.boundingBoxArea / baseArea - 1.0)
            : 0.0;
        return
          static_cast<double>(q.visualCrossings)
          + 5000.0 * static_cast<double>(q.nodeOverlaps)
          + bundleNodeWeight * static_cast<double>(q.bundleNodeOverlaps)
          + 500.0 * bboxGrowth;
      };

      if (readBoolEnv("DJERD_RIGID_NODE_EDGE_RELIEF_FINAL", false)) {
        const int passes = static_cast<int>(std::round(
          readDoubleEnv("DJERD_RIGID_NODE_EDGE_RELIEF_FINAL_PASSES", 2.0, 1.0, 8.0)));
        const double maxShift =
          readDoubleEnv("DJERD_RIGID_NODE_EDGE_RELIEF_FINAL_MAX_SHIFT", 160.0, 16.0, 1200.0);
        const double strength =
          readDoubleEnv("DJERD_RIGID_NODE_EDGE_RELIEF_FINAL_STRENGTH", 0.65, 0.05, 2.0);
        const double maxBboxGrowth =
          readDoubleEnv("DJERD_RIGID_NODE_EDGE_RELIEF_FINAL_MAX_BBOX_GROWTH", 1.04, 1.0, 2.0);
        const double bundleNodeWeight = readDoubleEnv(
          "DJERD_RIGID_NODE_EDGE_RELIEF_FINAL_BUNDLE_NODE_WEIGHT",
          50.0,
          0.0,
          1000.0);
        const bool clearBundlesAfterRelief = readBoolEnv(
          "DJERD_RIGID_NODE_EDGE_RELIEF_CLEAR_BUNDLES",
          true);
        const double nodeMargin = visualNodeMargin();
        const double bundleMargin = leafBundleVisualMargin();
        std::unordered_map<std::string, std::size_t> id2idxRelief;
        id2idxRelief.reserve(nodes.size());
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          id2idxRelief[nodes[i].modelId] = i;
        }

        auto addReliefShift = [&](std::vector<double>& shiftX,
                                  std::vector<double>& shiftY,
                                  std::size_t nodeIdx,
                                  const RoutePoint& a,
                                  const RoutePoint& b,
                                  const Rect& rect) {
          if (nodeIdx >= nodes.size()) return;
          const double dx = b.x - a.x;
          const double dy = b.y - a.y;
          const double len2 = dx * dx + dy * dy;
          if (len2 < 1e-6) return;
          const double len = std::sqrt(len2);
          const double cx = (rect.left + rect.right) * 0.5;
          const double cy = (rect.top + rect.bottom) * 0.5;
          const double t = std::clamp(
            ((cx - a.x) * dx + (cy - a.y) * dy) / len2,
            0.0,
            1.0);
          const double px = a.x + dx * t;
          const double py = a.y + dy * t;
          double ax = cx - px;
          double ay = cy - py;
          double dist = std::sqrt(ax * ax + ay * ay);
          if (dist < 1e-6) {
            ax = -dy / len;
            ay = dx / len;
            dist = 1.0;
          } else {
            ax /= dist;
            ay /= dist;
          }
          const double half =
            std::max(rect.right - rect.left, rect.bottom - rect.top) * 0.5;
          const double needed = std::max(18.0, half + nodeMargin - dist);
          const double mag = std::min(maxShift, needed * strength);
          shiftX[nodeIdx] += ax * mag;
          shiftY[nodeIdx] += ay * mag;
        };

        LayoutQualityMetrics currentQuality = measureRigidQuality();
        const double baseArea = currentQuality.boundingBoxArea;
        double currentScore = rigidScore(currentQuality, baseArea, bundleNodeWeight);
        std::size_t acceptedPasses = 0;
        std::size_t movedTotal = 0;

        for (int pass = 0; pass < passes; ++pass) {
          std::unordered_set<std::string> bundleAbsorbed;
          std::vector<std::unordered_set<std::size_t>> bundleExempt;
          std::vector<std::vector<std::size_t>> bundleLeaves;
          std::vector<Rect> bundleRects;
          bundleExempt.reserve(metadata.leafBundles.size());
          bundleLeaves.reserve(metadata.leafBundles.size());
          bundleRects.reserve(metadata.leafBundles.size());
          for (const LeafBundleRecord& bundle : metadata.leafBundles) {
            std::unordered_set<std::size_t> exempt;
            std::vector<std::size_t> leaves;
            auto pIt = id2idxRelief.find(bundle.parentModelId);
            if (pIt != id2idxRelief.end()) {
              exempt.insert(pIt->second);
              bundleAbsorbed.insert(bundle.parentModelId);
            }
            for (const std::string& leaf : bundle.leafModelIds) {
              bundleAbsorbed.insert(leaf);
              auto lIt = id2idxRelief.find(leaf);
              if (lIt == id2idxRelief.end()) continue;
              exempt.insert(lIt->second);
              leaves.push_back(lIt->second);
            }
            for (const std::string& root : bundle.sharedRootModelIds) {
              auto rIt = id2idxRelief.find(root);
              if (rIt != id2idxRelief.end()) exempt.insert(rIt->second);
            }
            bundleExempt.push_back(std::move(exempt));
            bundleLeaves.push_back(std::move(leaves));
            bundleRects.push_back(renderedLeafBundleRect(bundle, bundleMargin));
          }

          std::vector<double> shiftX(nodes.size(), 0.0);
          std::vector<double> shiftY(nodes.size(), 0.0);
          for (std::size_t e = 0; e < routes.size() && e < edges.size(); ++e) {
            if (routes[e].size() < 2) continue;
            auto sIt = id2idxRelief.find(edges[e].sourceModelId);
            auto tIt = id2idxRelief.find(edges[e].targetModelId);
            if (sIt == id2idxRelief.end() || tIt == id2idxRelief.end()) continue;
            const std::size_t srcIdx = sIt->second;
            const std::size_t tgtIdx = tIt->second;
            for (std::size_t si = 1; si < routes[e].size(); ++si) {
              const RoutePoint a = routes[e][si - 1];
              const RoutePoint b = routes[e][si];
              for (std::size_t ni = 0; ni < nodes.size(); ++ni) {
                if (ni == srcIdx || ni == tgtIdx) continue;
                if (bundleAbsorbed.count(nodes[ni].modelId)) continue;
                const Rect nr = nodeRect(nodes[ni], attributes, nodeMargin);
                if (!segmentIntersectsRect(a, b, nr)) continue;
                addReliefShift(shiftX, shiftY, ni, a, b, nr);
              }
              for (std::size_t bi = 0; bi < bundleRects.size(); ++bi) {
                if (bundleExempt[bi].count(srcIdx) || bundleExempt[bi].count(tgtIdx)) {
                  continue;
                }
                if (!segmentIntersectsRect(a, b, bundleRects[bi])) continue;
                for (std::size_t leafIdx : bundleLeaves[bi]) {
                  addReliefShift(shiftX, shiftY, leafIdx, a, b, bundleRects[bi]);
                }
              }
            }
          }

          std::vector<std::pair<double, double>> snapPositions(nodes.size());
          for (std::size_t i = 0; i < nodes.size(); ++i) {
            snapPositions[i] = {attributes.x(nodes[i].handle), attributes.y(nodes[i].handle)};
          }
          const auto snapRoutes = routes;
          const auto snapBundles = metadata.leafBundles;

          const std::size_t moved =
            applyNodeShifts(nodes, attributes, shiftX, shiftY, maxShift);
          if (moved == 0) break;
          enforceNodeSeparationStrong(nodes, attributes);
          recomputeRigidLeafBundles();
          if (clearBundlesAfterRelief) {
            (void)clearLeafBundleNodeMargins(metadata.leafBundles, nodes, attributes);
            recomputeRigidLeafBundles();
          }
          routes = routeAllEdgesStraight(edges, attributes);

          LayoutQualityMetrics nextQuality = measureRigidQuality();
          const bool bboxOk =
            baseArea <= 0.0 || nextQuality.boundingBoxArea <= baseArea * maxBboxGrowth;
          const double nextScore = rigidScore(nextQuality, baseArea, bundleNodeWeight);
          if (
              bboxOk
              && nextQuality.nodeOverlaps <= currentQuality.nodeOverlaps
              && nextScore + 1e-6 < currentScore) {
            currentQuality = nextQuality;
            currentScore = nextScore;
            movedTotal += moved;
            ++acceptedPasses;
            continue;
          }

          for (std::size_t i = 0; i < nodes.size(); ++i) {
            attributes.x(nodes[i].handle) = snapPositions[i].first;
            attributes.y(nodes[i].handle) = snapPositions[i].second;
          }
          routes = snapRoutes;
          metadata.leafBundles = snapBundles;
          break;
        }
        std::fprintf(stderr,
          "[rigid-node-edge-relief-final] accepted=%zu moved=%zu "
          "visual=%zu edgeNode=%zu bundleEdge=%zu bundleNode=%zu bbox=%.2fB.\n",
          acceptedPasses,
          movedTotal,
          currentQuality.visualCrossings,
          currentQuality.edgeNodeIntersections,
          currentQuality.bundleEdgeIntersections,
          currentQuality.bundleNodeOverlaps,
          currentQuality.boundingBoxArea / 1e9);
      }

      (void)clearRenderedCarrierNodeIntersectionsIfRequested(
        nodes,
        edges,
        routes,
        attributes,
        clusterByModelIdFull,
        metadata);
      repairCanonicalRouteObstaclesIfRequested(
        nodes, edges, routes, attributes, clusterByModelIdFull, metadata);
      std::vector<std::vector<std::string>> crossingIdsByEdge;
      std::size_t totalCrossings = 0;
      const std::vector<EdgeCrossingRecord> crossings =
        detectRouteCrossings(edges, routes, crossingIdsByEdge, totalCrossings);
      LayoutQualityMetrics quality =
        measureLayoutQuality(nodes, edges, routes, attributes, &metadata.leafBundles);
      // measureLayoutQuality leaves edgeCrossings at 0; the route-aware
      // crossing count we just detected is authoritative.
      quality.edgeCrossings = totalCrossings;
      const bool renderedCarrierMetricsApplied =
        applyRenderedCarrierMetricsIfRequested(
          nodes,
          edges,
          routes,
          attributes,
          clusterByModelIdFull,
          metadata,
          quality,
          totalCrossings);
      // Recompute visualCrossings sum (matches the formula used in
      // measureLayoutQuality / cluster_graph end stage).
      if (!renderedCarrierMetricsApplied) {
        quality.visualCrossings =
          quality.edgeCrossings
          + quality.edgeNodeIntersections
          + quality.nodeOverlaps
          + quality.bundleEdgeIntersections
          + quality.bundleNodeOverlaps;
      }
      measureCanonicalCrossingDrawing(
        metadata.canonicalCrossing,
        nodes,
        edges,
        routes,
        attributes);
      const Bounds bounds = measureBounds(nodes, routes, attributes);
      std::fprintf(stderr,
        "[rigid-positions] Bypassed post-passes; cross=%zu bbox=%.2fB.\n",
        quality.edgeCrossings,
        quality.boundingBoxArea / 1e9);
      writeLayoutJson(
        std::cout,
        arguments.mode,
        metadata,
        nodes,
        edges,
        attributes,
        routes,
        crossings,
        crossingIdsByEdge,
        quality,
        bounds);
      return 0;
    }

    // Bundle clearance pass — push every non-bundle-absorbed node
    // (cluster members AND non-cluster nodes) off any leaf-bundle bbox
    // it overlaps. The bundle is treated as a single rigid block per
    // user spec; external nodes encroaching into the matrix area are
    // pushed back along the (bundle-center → node-center) axis until
    // their rect clears the bbox. Set DJERD_NO_BUNDLE_CLEAR=1 to skip.
    {
      const char* skipEnv = std::getenv("DJERD_NO_BUNDLE_CLEAR");
      const bool skipBundleClear =
        skipEnv && std::strcmp(skipEnv, "0") != 0;
      if (!skipBundleClear && !metadata.leafBundles.empty()) {
        std::unordered_set<std::string> bundleAbsorbed;
        std::vector<Rect> bundleRects;
        std::unordered_map<std::string, std::size_t> idxByModelId;
        idxByModelId.reserve(nodes.size());
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          idxByModelId[nodes[i].modelId] = i;
        }
        bundleRects.reserve(metadata.leafBundles.size());
        for (const LeafBundleRecord& bundle : metadata.leafBundles) {
          bundleAbsorbed.insert(bundle.parentModelId);
          for (const std::string& leaf : bundle.leafModelIds) {
            bundleAbsorbed.insert(leaf);
          }
          Rect br;
          // Pre-inflate by kBundleClearPad so cleared positions match
          // the metric's margin-expanded collision area.
          constexpr double kPreInflate = 8.0;
          br.left = bundle.bboxX - kPreInflate;
          br.right = bundle.bboxX + bundle.bboxWidth + kPreInflate;
          br.top = bundle.bboxY - kPreInflate;
          br.bottom = bundle.bboxY + bundle.bboxHeight + kPreInflate;
          bundleRects.push_back(br);
        }
        // Match metric's kVisualMargin so cleared positions ALSO satisfy
        // the metric's margin-expanded collision check. Iter increased
        // to 32 to converge on residual overlaps.
        constexpr double kBundleClearPad = 8.0;
        constexpr int kBundleClearIters = 32;
        std::size_t pushed = 0;
        for (const NodeRecord& nd : nodes) {
          if (bundleAbsorbed.count(nd.modelId)) continue;
          double nx = attributes.x(nd.handle);
          double ny = attributes.y(nd.handle);
          const double nW = nd.width * 0.5;
          const double nH = nd.height * 0.5;
          bool changed = false;
          for (int iter = 0; iter < kBundleClearIters; ++iter) {
            bool moved = false;
            for (const Rect& br : bundleRects) {
              if (nx + nW + kBundleClearPad <= br.left) continue;
              if (nx - nW - kBundleClearPad >= br.right) continue;
              if (ny + nH + kBundleClearPad <= br.top) continue;
              if (ny - nH - kBundleClearPad >= br.bottom) continue;
              const double bcx = (br.left + br.right) * 0.5;
              const double bcy = (br.top + br.bottom) * 0.5;
              const double brW = (br.right - br.left) * 0.5;
              const double brH = (br.bottom - br.top) * 0.5;
              const double dx = nx - bcx;
              const double dy = ny - bcy;
              const double reqDx = brW + nW + kBundleClearPad;
              const double reqDy = brH + nH + kBundleClearPad;
              const double overX = reqDx - std::abs(dx);
              const double overY = reqDy - std::abs(dy);
              if (overX < overY) {
                const double sign = dx >= 0.0 ? 1.0 : -1.0;
                nx += sign * (overX + 0.5);
              } else {
                const double sign = dy >= 0.0 ? 1.0 : -1.0;
                ny += sign * (overY + 0.5);
              }
              moved = true;
              changed = true;
            }
            if (!moved) break;
          }
          if (changed) {
            attributes.x(nd.handle) = std::round(nx * 100.0) / 100.0;
            attributes.y(nd.handle) = std::round(ny * 100.0) / 100.0;
            ++pushed;
          }
        }
        if (pushed > 0) {
          std::fprintf(stderr,
            "[bundle-clear] Pushed %zu nodes off bundle bboxes.\n", pushed);
          // Pushed nodes can now overlap other nodes — resolve cascade
          // with the standard enforce-separation pass. This MAY shift
          // the pushed nodes back into bundle bbox if there's no other
          // empty space; in that case we accept whichever conflict the
          // ENS pass settles on.
          enforceNodeSeparationStrong(nodes, attributes);
        }

        // Bundle self-shift — DISABLED. Tested: shifting 4 bundles
        // resolved bundle-clear residual but +6 bndlN, +2 nOvl,
        // +15% eni from cascade. Post-pass bundle relocation creates
        // new cluster-edge crossings that exceed the bundle bbox
        // overlap saved. Set DJERD_BUNDLE_SHIFT=1 to enable.
        const char* bsEnv = std::getenv("DJERD_BUNDLE_SHIFT");
        const bool runBundleShift = bsEnv && std::strcmp(bsEnv, "0") != 0;
        std::size_t bundlesShifted = 0;
        if (runBundleShift)
        for (std::size_t bi = 0; bi < bundleRects.size(); ++bi) {
          // Find offending external node count + collective overlap
          // direction.
          double sumDx = 0.0, sumDy = 0.0;
          std::size_t collisions = 0;
          const Rect& br = bundleRects[bi];
          const double bcx = (br.left + br.right) * 0.5;
          const double bcy = (br.top + br.bottom) * 0.5;
          for (const NodeRecord& nd : nodes) {
            if (bundleAbsorbed.count(nd.modelId)) continue;
            const double nx = attributes.x(nd.handle);
            const double ny = attributes.y(nd.handle);
            const double nW = nd.width * 0.5;
            const double nH = nd.height * 0.5;
            if (nx + nW + kBundleClearPad <= br.left) continue;
            if (nx - nW - kBundleClearPad >= br.right) continue;
            if (ny + nH + kBundleClearPad <= br.top) continue;
            if (ny - nH - kBundleClearPad >= br.bottom) continue;
            // Vector from external node to bundle center — direction
            // bundle should move TO clear this node.
            sumDx += bcx - nx;
            sumDy += bcy - ny;
            ++collisions;
          }
          if (collisions == 0) continue;
          // Normalize direction.
          const double mag = std::sqrt(sumDx * sumDx + sumDy * sumDy);
          if (mag < 1.0) continue;
          const double dirX = sumDx / mag;
          const double dirY = sumDy / mag;
          // Step distance: half max bundle dim or 200, whichever larger.
          const double bw = (br.right - br.left);
          const double bh = (br.bottom - br.top);
          const double stepDist = std::max(200.0, std::max(bw, bh) * 0.6);
          const double offX = dirX * stepDist;
          const double offY = dirY * stepDist;
          // Translate parent + all leaves of this bundle.
          const LeafBundleRecord& bundle = metadata.leafBundles[bi];
          auto pIt = idxByModelId.find(bundle.parentModelId);
          if (pIt != idxByModelId.end()) {
            attributes.x(nodes[pIt->second].handle) += offX;
            attributes.y(nodes[pIt->second].handle) += offY;
          }
          for (const std::string& leaf : bundle.leafModelIds) {
            auto lIt = idxByModelId.find(leaf);
            if (lIt == idxByModelId.end()) continue;
            attributes.x(nodes[lIt->second].handle) += offX;
            attributes.y(nodes[lIt->second].handle) += offY;
          }
          // Update bundleRects[bi] for downstream checks (keep it
          // consistent, in case another bundle's check overlaps).
          bundleRects[bi].left += offX;
          bundleRects[bi].right += offX;
          bundleRects[bi].top += offY;
          bundleRects[bi].bottom += offY;
          ++bundlesShifted;
        }
        if (bundlesShifted > 0) {
          std::fprintf(stderr,
            "[bundle-shift] Shifted %zu bundles to clear external overlap.\n",
            bundlesShifted);
          enforceNodeSeparationStrong(nodes, attributes);
        }
      }
    }

    // High-degree hub outward push — DISABLED by default (set
    // DJERD_HUB_PUSH=1 to enable). Tested with 3 variants (full
    // cluster shift, hub-only, angular slots); ALL increased
    // visualCrossings substantially (3.6k → 6-22k) because shifting
    // hubs post-layout makes their inter-cluster edges much longer,
    // exploding segment-segment crossings. The right place to push
    // hubs outward is super-graph FMMM (where the structure is
    // organized BEFORE inner placement). Left as a flag-gated path
    // for future experimentation.
    if ((arguments.clusterGraph || arguments.bubble)
        && !clusterByModelIdFull.empty()) {
      const char* hubPushEnv = std::getenv("DJERD_HUB_PUSH");
      const bool skipHub = !(hubPushEnv && std::strcmp(hubPushEnv, "0") != 0);
      if (!skipHub) {
        // Build node-degree (count of edges incident).
        std::vector<std::size_t> degree(nodes.size(), 0);
        std::unordered_map<std::string, std::size_t> idToIdxHP;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          idToIdxHP[nodes[i].modelId] = i;
        }
        for (const EdgeRecord& e : edges) {
          auto sIt = idToIdxHP.find(e.sourceModelId);
          auto tIt = idToIdxHP.find(e.targetModelId);
          if (sIt == idToIdxHP.end() || tIt == idToIdxHP.end()) continue;
          degree[sIt->second] += 1;
          degree[tIt->second] += 1;
        }
        // Cluster roots = first member listed under a cluster id (stable).
        // Track each modelId → cluster id; pick the first-seen as the
        // representative. Roots are the hubs we push.
        std::unordered_set<std::size_t> rootIdxSet;
        std::unordered_map<std::string, std::size_t> firstByCluster;
        for (const auto& kv : clusterByModelIdFull) {
          auto it = idToIdxHP.find(kv.first);
          if (it == idToIdxHP.end()) continue;
          const std::string& cid = kv.second;
          auto fIt = firstByCluster.find(cid);
          if (fIt == firstByCluster.end()) {
            firstByCluster[cid] = it->second;
          } else if (degree[it->second] > degree[fIt->second]) {
            firstByCluster[cid] = it->second;
          }
        }
        for (const auto& kv : firstByCluster) rootIdxSet.insert(kv.second);
        // Layout centroid (over all non-bundle nodes).
        std::unordered_set<std::string> bundleAbsorbedHP;
        for (const LeafBundleRecord& bundle : metadata.leafBundles) {
          bundleAbsorbedHP.insert(bundle.parentModelId);
          for (const std::string& leaf : bundle.leafModelIds) {
            bundleAbsorbedHP.insert(leaf);
          }
        }
        double sumX = 0.0, sumY = 0.0;
        std::size_t cnt = 0;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          if (bundleAbsorbedHP.count(nodes[i].modelId)) continue;
          sumX += attributes.x(nodes[i].handle);
          sumY += attributes.y(nodes[i].handle);
          ++cnt;
        }
        if (cnt < 4) goto hub_push_done;
        {
          const double layoutCx = sumX / static_cast<double>(cnt);
          const double layoutCy = sumY / static_cast<double>(cnt);
          // Average distance from centroid (as a length scale).
          double sumD = 0.0;
          for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (bundleAbsorbedHP.count(nodes[i].modelId)) continue;
            const double dx = attributes.x(nodes[i].handle) - layoutCx;
            const double dy = attributes.y(nodes[i].handle) - layoutCy;
            sumD += std::sqrt(dx * dx + dy * dy);
          }
          const double avgR = sumD / static_cast<double>(cnt);
          // Identify top-decile-degree roots — the hubs to push.
          std::vector<std::size_t> rootsByDeg(rootIdxSet.begin(), rootIdxSet.end());
          std::sort(rootsByDeg.begin(), rootsByDeg.end(),
            [&](std::size_t a, std::size_t b) { return degree[a] > degree[b]; });
          const std::size_t topN = std::max<std::size_t>(3,
            static_cast<std::size_t>(rootsByDeg.size() / 10));
          // Per-cluster member set for rigid-translate.
          std::unordered_map<std::string, std::vector<std::size_t>>
            membersByClusterHP;
          for (const auto& kv : clusterByModelIdFull) {
            auto it = idToIdxHP.find(kv.first);
            if (it != idToIdxHP.end()) {
              membersByClusterHP[kv.second].push_back(it->second);
            }
          }
          // Resolve cluster-id of each root.
          std::unordered_map<std::size_t, std::string> rootIdxToCid;
          for (const auto& kv : firstByCluster) {
            rootIdxToCid[kv.second] = kv.first;
          }
          // Push hub clusters outward via a SHIFT (= rigid translate of
          // hub + cluster members) AND assign each top hub a unique
          // angular slot at radius 1.2× avgR so they don't all land on
          // top of each other. Cluster members move with the hub, but
          // the per-cluster relative geometry stays intact.
          std::size_t pushedCount = 0;
          for (std::size_t k = 0; k < topN && k < rootsByDeg.size(); ++k) {
            const std::size_t hubIdx = rootsByDeg[k];
            const double hx = attributes.x(nodes[hubIdx].handle);
            const double hy = attributes.y(nodes[hubIdx].handle);
            const double dx0 = hx - layoutCx;
            const double dy0 = hy - layoutCy;
            const double d = std::sqrt(dx0 * dx0 + dy0 * dy0);
            const double targetR = 1.2 * avgR;
            if (d >= targetR) continue;
            // Angular slot: distribute top hubs evenly. Preserve current
            // bearing if hub is not at centroid (so layout doesn't
            // wholesale rotate); fall back to slot-based angle.
            double angle;
            if (d < 100.0) {
              angle = (2.0 * 3.14159265358979)
                * static_cast<double>(k)
                / static_cast<double>(std::max<std::size_t>(1, topN));
            } else {
              angle = std::atan2(dy0, dx0);
            }
            const double newHx = layoutCx + targetR * std::cos(angle);
            const double newHy = layoutCy + targetR * std::sin(angle);
            const double offX = newHx - hx;
            const double offY = newHy - hy;
            // Rigid translate of hub's cluster.
            auto cidIt = rootIdxToCid.find(hubIdx);
            if (cidIt == rootIdxToCid.end()) continue;
            auto memIt = membersByClusterHP.find(cidIt->second);
            if (memIt == membersByClusterHP.end()) continue;
            for (std::size_t m : memIt->second) {
              if (bundleAbsorbedHP.count(nodes[m].modelId)) continue;
              attributes.x(nodes[m].handle) += offX;
              attributes.y(nodes[m].handle) += offY;
            }
            ++pushedCount;
          }
          if (pushedCount > 0) {
            std::fprintf(stderr,
              "[hub-push] Pushed %zu high-degree hubs outward (top decile of %zu roots).\n",
              pushedCount, rootsByDeg.size());
            // Resolve cascade overlaps from translation.
            enforceNodeSeparationStrong(nodes, attributes);
          }
        }
        hub_push_done:;
      }
    }

    // Knot minimization — intra-cluster node-swap untangle.
    // For each cluster, try swapping pairs of non-bundle members and
    // accept the swap if it reduces edge crossings involving their
    // incident edges. Iterates until no improvement. Bundle-absorbed
    // nodes (parent + leaves) are pinned. DJERD_NO_KNOT_MIN skips the legacy
    // swap passes while still allowing the collision-safe relocation pass.
    if ((arguments.clusterGraph || arguments.bubble)
        && !clusterByModelIdFull.empty()) {
      minimizeClusterKnots(nodes, edges, clusterByModelIdFull, attributes, metadata);
    }

    const bool straightLineMode = isStraightLineRoutingMode(arguments.mode)
      || arguments.edgeRouting == "straight"
      || arguments.edgeRouting == "straight_smart";
    // Cross-aware routing: per-edge A* on a cell grid avoiding both nodes
    // and high-density corridors. Selected via --edge-routing=cross_aware
    // OR DJERD_CROSS_AWARE_ROUTING=1.
    //
    // Apr 30 verification: cluster_graph 1810 → 2243 (+433) regression.
    // A* improves edgeNodeIntersections (446→340) but explodes segment
    // count (×2.2 = 4826 vs 2178), causing segment-segment crossings to
    // dominate. Plus 651/1476 (44%) fall back to straight line because
    // A* can't path through node-occupied corridors. Reverted to opt-in.
    const char* crossAwareEnv = std::getenv("DJERD_CROSS_AWARE_ROUTING");
    const bool crossAwareRouting =
      arguments.edgeRouting == "cross_aware"
      || (crossAwareEnv && std::strcmp(crossAwareEnv, "0") != 0);

    std::vector<std::vector<RoutePoint>> routes = crossAwareRouting
      ? routeAllEdgesCrossAware(nodes, edges, attributes)
      : (straightLineMode
        ? (arguments.edgeRouting == "straight_smart" && !isStraightLineRoutingMode(arguments.mode)
          ? routeAllEdgesStraightSmart(nodes, edges, attributes)
          : routeAllEdgesStraight(edges, attributes))
        : routeAllEdges(nodes, edges, attributes, true));

    if (!arguments.routesTsv.empty()) {
      const std::size_t appliedRoutes =
        applyRoutesTsvOverride(arguments.routesTsv, edges, routes);
      std::fprintf(stderr,
        "[routes-tsv] Overrode %zu/%zu routes from %s.\n",
        appliedRoutes, edges.size(), arguments.routesTsv.c_str());
    }

    // Edge detour pass — straight-line edges cross through unrelated
    // nodes ("edgeNodeIntersections" metric). For each edge segment,
    // detect non-endpoint nodes whose rect the segment passes through
    // and insert a perpendicular waypoint that pulls the polyline
    // around the blocker. This is the automated equivalent of dragging
    // an edge around a node in manual untangling. Gated by
    // DJERD_EDGE_DETOUR=1 (default off until verified).
    const char* edgeDetourEnv = std::getenv("DJERD_EDGE_DETOUR");
    const bool edgeDetour =
      edgeDetourEnv && std::strcmp(edgeDetourEnv, "0") != 0;
    if (edgeDetour && straightLineMode) {
      std::vector<Rect> nodeRects(nodes.size());
      for (std::size_t i = 0; i < nodes.size(); ++i) {
        nodeRects[i] = nodeRect(nodes[i], attributes);
      }
      std::unordered_map<std::string, std::size_t> idToIdxDet;
      idToIdxDet.reserve(nodes.size());
      for (std::size_t i = 0; i < nodes.size(); ++i) {
        idToIdxDet[nodes[i].modelId] = i;
      }
      // Bundle obstacle list. Each bundle's bbox is treated as a single
      // rigid block: edges connecting to the bundle's parent or any of
      // its leaves are exempt from this bundle's penalty (they're
      // expected to enter/exit the bundle); all other edges should
      // detour around it.
      std::vector<Rect> bundleRectsDet;
      std::vector<std::unordered_set<std::size_t>> bundleExemptIdx;
      bundleRectsDet.reserve(metadata.leafBundles.size());
      bundleExemptIdx.reserve(metadata.leafBundles.size());
      for (const LeafBundleRecord& bundle : metadata.leafBundles) {
        bundleRectsDet.push_back(renderedLeafBundleRect(bundle));
        std::unordered_set<std::size_t> exempt;
        auto pIt = idToIdxDet.find(bundle.parentModelId);
        if (pIt != idToIdxDet.end()) exempt.insert(pIt->second);
        for (const std::string& leaf : bundle.leafModelIds) {
          auto lIt = idToIdxDet.find(leaf);
          if (lIt != idToIdxDet.end()) exempt.insert(lIt->second);
        }
        bundleExemptIdx.push_back(std::move(exempt));
      }
      // Spatial bin for blocker lookup. Max node dim sets cell size.
      double maxNodeDim = 1.0;
      for (std::size_t i = 0; i < nodes.size(); ++i) {
        maxNodeDim = std::max(maxNodeDim,
          std::max(nodes[i].width, nodes[i].height));
      }
      const double cellSize = maxNodeDim * 1.5 + 16.0;
      auto pairHash = [](const std::pair<long long, long long>& p) {
        return std::hash<long long>()(p.first)
          ^ (std::hash<long long>()(p.second) << 1);
      };
      std::unordered_map<std::pair<long long, long long>,
                          std::vector<std::size_t>, decltype(pairHash)>
        bins(0, pairHash);
      auto binKey = [&](double x, double y) {
        return std::make_pair(
          static_cast<long long>(std::floor(x / cellSize)),
          static_cast<long long>(std::floor(y / cellSize)));
      };
      for (std::size_t i = 0; i < nodes.size(); ++i) {
        const double cx = (nodeRects[i].left + nodeRects[i].right) * 0.5;
        const double cy = (nodeRects[i].top + nodeRects[i].bottom) * 0.5;
        bins[binKey(cx, cy)].push_back(i);
      }
      auto cellsAlongSegment = [&](const RoutePoint& a, const RoutePoint& b) {
        // Bresenham-ish: emit all cells the segment crosses.
        std::vector<std::pair<long long, long long>> result;
        const auto k0 = binKey(a.x, a.y);
        const auto k1 = binKey(b.x, b.y);
        const long long minX = std::min(k0.first, k1.first) - 1;
        const long long maxX = std::max(k0.first, k1.first) + 1;
        const long long minY = std::min(k0.second, k1.second) - 1;
        const long long maxY = std::max(k0.second, k1.second) + 1;
        for (long long x = minX; x <= maxX; ++x) {
          for (long long y = minY; y <= maxY; ++y) {
            result.emplace_back(x, y);
          }
        }
        return result;
      };

      // Multi-pass detour: each pass handles segments that became
      // blockers due to previous pass's waypoints. Default 2 passes;
      // diminishing returns past that.
      const char* detourPassesEnv = std::getenv("DJERD_DETOUR_PASSES");
      const int detourPasses =
        detourPassesEnv ? std::max(1, std::atoi(detourPassesEnv)) : 2;
      const char* detourCrossWeightEnv = std::getenv("DJERD_EDGE_DETOUR_CROSS_WEIGHT");
      const double detourCrossWeight =
        detourCrossWeightEnv ? std::max(0.0, std::atof(detourCrossWeightEnv)) : 0.0;
      const char* detourLengthWeightEnv = std::getenv("DJERD_EDGE_DETOUR_LENGTH_WEIGHT");
      const double detourLengthWeight =
        detourLengthWeightEnv ? std::max(0.0, std::atof(detourLengthWeightEnv)) : 0.0;
      std::size_t detoursAdded = 0;
      std::size_t edgesDetoured = 0;
      std::size_t detoursRejectedByCross = 0;
      auto segmentCrossCount = [&](std::size_t edgeIndex,
                                   const RoutePoint& p,
                                   const RoutePoint& q) {
        std::size_t total = 0;
        if (edgeIndex >= edges.size()) return total;
        for (std::size_t other = 0; other < routes.size() && other < edges.size(); ++other) {
          if (other == edgeIndex) continue;
          if (sharesEndpoint(edges[edgeIndex], edges[other])) continue;
          const auto& otherRoute = routes[other];
          if (otherRoute.size() < 2) continue;
          for (std::size_t oi = 1; oi < otherRoute.size(); ++oi) {
            RoutePoint isect;
            if (properSegmentIntersection(
                p, q, otherRoute[oi - 1], otherRoute[oi], isect)) {
              ++total;
            }
          }
        }
        return total;
      };
      auto segmentLength = [](const RoutePoint& p, const RoutePoint& q) {
        const double dx = q.x - p.x;
        const double dy = q.y - p.y;
        return std::sqrt(dx * dx + dy * dy);
      };
    for (int detourPass = 0; detourPass < detourPasses; ++detourPass) {
      for (std::size_t e = 0; e < edges.size() && e < routes.size(); ++e) {
        auto& route = routes[e];
        if (route.size() < 2) continue;
        auto srcIt = idToIdxDet.find(edges[e].sourceModelId);
        auto tgtIt = idToIdxDet.find(edges[e].targetModelId);
        if (srcIt == idToIdxDet.end() || tgtIt == idToIdxDet.end()) continue;
        const std::size_t srcIdx = srcIt->second;
        const std::size_t tgtIdx = tgtIt->second;

        bool routeChanged = false;
        std::vector<RoutePoint> newRoute;
        newRoute.reserve(route.size() * 2);
        newRoute.push_back(route[0]);
        for (std::size_t si = 0; si + 1 < route.size(); ++si) {
          const RoutePoint a = route[si];
          const RoutePoint b = route[si + 1];
          // Find blockers on segment a-b. Blocker = node rect OR
          // leaf-bundle bbox the segment passes through. Bundle blockers
          // use a synthetic index space (nodeIdx ≥ nodes.size()).
          struct Blocker { double t; std::size_t nodeIdx; Rect rect; bool isBundle; };
          std::vector<Blocker> blockers;
          std::unordered_set<std::size_t> seenNodes;
          for (const auto& cell : cellsAlongSegment(a, b)) {
            auto bIt = bins.find(cell);
            if (bIt == bins.end()) continue;
            for (std::size_t ni : bIt->second) {
              if (ni == srcIdx || ni == tgtIdx) continue;
              if (!seenNodes.insert(ni).second) continue;
              if (!segmentIntersectsRect(a, b, nodeRects[ni])) continue;
              const double cx = (nodeRects[ni].left + nodeRects[ni].right) * 0.5;
              const double cy = (nodeRects[ni].top + nodeRects[ni].bottom) * 0.5;
              const double dx = b.x - a.x;
              const double dy = b.y - a.y;
              const double len2 = dx * dx + dy * dy;
              if (len2 < 1e-3) continue;
              const double t =
                ((cx - a.x) * dx + (cy - a.y) * dy) / len2;
              if (t < 0.0 || t > 1.0) continue;
              blockers.push_back({t, ni, nodeRects[ni], false});
            }
          }
          // Bundle bbox blockers (skip bundles the edge legitimately
          // enters/exits via parent or leaf).
          for (std::size_t bi = 0; bi < bundleRectsDet.size(); ++bi) {
            if (bundleExemptIdx[bi].count(srcIdx)
                || bundleExemptIdx[bi].count(tgtIdx)) continue;
            if (!segmentIntersectsRect(a, b, bundleRectsDet[bi])) continue;
            const double cx = (bundleRectsDet[bi].left + bundleRectsDet[bi].right) * 0.5;
            const double cy = (bundleRectsDet[bi].top + bundleRectsDet[bi].bottom) * 0.5;
            const double dx = b.x - a.x;
            const double dy = b.y - a.y;
            const double len2 = dx * dx + dy * dy;
            if (len2 < 1e-3) continue;
            const double t = ((cx - a.x) * dx + (cy - a.y) * dy) / len2;
            if (t < 0.0 || t > 1.0) continue;
            blockers.push_back({t, nodes.size() + bi, bundleRectsDet[bi], true});
          }
          std::sort(blockers.begin(), blockers.end(),
            [](const Blocker& l, const Blocker& r) { return l.t < r.t; });
          // Helper: count node-rect AND bundle-bbox intersections of a
          // candidate sub-segment. Excludes the edge's own endpoints
          // (and, for bundles, exempts the bundles whose parent or
          // leaves match either endpoint — those are expected entries).
          auto countHits = [&](const RoutePoint& p, const RoutePoint& q) {
            int hits = 0;
            std::unordered_set<std::size_t> seen;
            for (const auto& cell : cellsAlongSegment(p, q)) {
              auto bIt = bins.find(cell);
              if (bIt == bins.end()) continue;
              for (std::size_t ni : bIt->second) {
                if (ni == srcIdx || ni == tgtIdx) continue;
                if (!seen.insert(ni).second) continue;
                if (segmentIntersectsRect(p, q, nodeRects[ni])) ++hits;
              }
            }
            // Bundle bbox hits.
            for (std::size_t bi = 0; bi < bundleRectsDet.size(); ++bi) {
              if (bundleExemptIdx[bi].count(srcIdx)
                  || bundleExemptIdx[bi].count(tgtIdx)) continue;
              if (segmentIntersectsRect(p, q, bundleRectsDet[bi])) ++hits;
            }
            return hits;
          };

          // Insert detour waypoint per blocker, picking side (left/right)
          // that minimizes new node intersections in the new sub-segments.
          // Uses CURRENT prev (last point of newRoute) as the start of the
          // pre-detour segment.
          for (const Blocker& bl : blockers) {
            const Rect& r = bl.rect;
            const double cx = (r.left + r.right) * 0.5;
            const double cy = (r.top + r.bottom) * 0.5;
            const double rHalf = std::max(
              (r.right - r.left), (r.bottom - r.top)) * 0.5 + 12.0;
            const double dx = b.x - a.x;
            const double dy = b.y - a.y;
            const double len = std::sqrt(dx * dx + dy * dy);
            if (len < 1e-3) continue;
            const double perpX = -dy / len;
            const double perpY = dx / len;
            const double t = ((cx - a.x) * dx + (cy - a.y) * dy) / (len * len);
            const double projX = a.x + dx * t;
            const double projY = a.y + dy * t;
            // Try both sides.
            RoutePoint wpLeft;
            wpLeft.x = std::round((projX + perpX * rHalf) * 100.0) / 100.0;
            wpLeft.y = std::round((projY + perpY * rHalf) * 100.0) / 100.0;
            RoutePoint wpRight;
            wpRight.x = std::round((projX - perpX * rHalf) * 100.0) / 100.0;
            wpRight.y = std::round((projY - perpY * rHalf) * 100.0) / 100.0;
            // prev is the last point already in newRoute (= a or last
            // blocker waypoint). next is b.
            const RoutePoint& prev = newRoute.back();
            const int hitsLeft = countHits(prev, wpLeft) + countHits(wpLeft, b);
            const int hitsRight = countHits(prev, wpRight) + countHits(wpRight, b);
            // Also check NO-detour baseline: just keep going to next point.
            const int hitsBaseline = countHits(prev, b);
            const bool crossAwareDetour =
              detourCrossWeight > 0.0 || detourLengthWeight > 0.0;
            const std::size_t crossBaseline = crossAwareDetour
              ? segmentCrossCount(e, prev, b)
              : 0;
            const std::size_t crossLeft = crossAwareDetour
              ? segmentCrossCount(e, prev, wpLeft) + segmentCrossCount(e, wpLeft, b)
              : 0;
            const std::size_t crossRight = crossAwareDetour
              ? segmentCrossCount(e, prev, wpRight) + segmentCrossCount(e, wpRight, b)
              : 0;
            const double lenBaseline = crossAwareDetour
              ? segmentLength(prev, b)
              : 0.0;
            const double lenLeft = crossAwareDetour
              ? segmentLength(prev, wpLeft) + segmentLength(wpLeft, b)
              : 0.0;
            const double lenRight = crossAwareDetour
              ? segmentLength(prev, wpRight) + segmentLength(wpRight, b)
              : 0.0;
            const double scoreBaseline =
              static_cast<double>(hitsBaseline)
              + detourCrossWeight * static_cast<double>(crossBaseline)
              + detourLengthWeight * lenBaseline;
            const double scoreLeft =
              static_cast<double>(hitsLeft)
              + detourCrossWeight * static_cast<double>(crossLeft)
              + detourLengthWeight * lenLeft;
            const double scoreRight =
              static_cast<double>(hitsRight)
              + detourCrossWeight * static_cast<double>(crossRight)
              + detourLengthWeight * lenRight;
            double bestScore = scoreBaseline;
            int bestHits = hitsBaseline;
            std::size_t bestCross = crossBaseline;
            const RoutePoint* bestWp = nullptr;
            if (hitsLeft < hitsBaseline && scoreLeft < bestScore) {
              bestScore = scoreLeft;
              bestHits = hitsLeft;
              bestCross = crossLeft;
              bestWp = &wpLeft;
            }
            if (hitsRight < hitsBaseline && scoreRight < bestScore) {
              bestScore = scoreRight;
              bestHits = hitsRight;
              bestCross = crossRight;
              bestWp = &wpRight;
            }
            if (bestWp != nullptr) {
              newRoute.push_back(*bestWp);
              ++detoursAdded;
              routeChanged = true;
            } else if (
                crossAwareDetour
                && (hitsLeft < hitsBaseline || hitsRight < hitsBaseline)
                && (crossLeft > crossBaseline || crossRight > crossBaseline)) {
              (void)bestHits;
              (void)bestCross;
              ++detoursRejectedByCross;
            }
          }
          newRoute.push_back(b);
        }
        if (routeChanged) {
          route = compressRoutePoints(std::move(newRoute));
          ++edgesDetoured;
        }
      }
    }
      std::fprintf(stderr,
        "[edge-detour] %zu detour waypoints across %zu edges "
        "(multi-pass, crossWeight=%.3f, lengthWeight=%.5f, rejected=%zu).\n",
        detoursAdded, edgesDetoured, detourCrossWeight,
        detourLengthWeight, detoursRejectedByCross);
    }

    // Leaf bundle anchor port (disabled): adding shared waypoint as a
    // route polyline waypoint did not reduce cross — segment-level
    // counting double-counts the extra waypoint segments and the
    // anchor's path through neighbouring cluster bubbles raises eNI.
    // True bundle reduction needs renderer-level merging which is out
    // of scope for the layout binary.
    if (false && !leafAnchorMap.empty() && straightLineMode) {
      std::unordered_map<std::string, std::size_t> idToIdxLBA;
      for (std::size_t i = 0; i < nodes.size(); ++i) {
        idToIdxLBA[nodes[i].modelId] = i;
      }
      // Group leaves by parent to compute centroid + anchor per group.
      std::unordered_map<std::size_t, std::vector<std::size_t>> bundleByParent;
      for (const auto& kv : leafAnchorMap) {
        bundleByParent[kv.second.parentIdx].push_back(kv.first);
      }
      std::unordered_map<std::size_t, std::pair<double, double>> currentAnchor;
      for (const auto& kv : bundleByParent) {
        const std::size_t parentIdx = kv.first;
        const auto& leaves = kv.second;
        if (parentIdx >= nodes.size() || leaves.empty()) continue;
        double sumX = 0.0, sumY = 0.0;
        std::size_t cnt = 0;
        for (std::size_t l : leaves) {
          if (l >= nodes.size()) continue;
          sumX += attributes.x(nodes[l].handle);
          sumY += attributes.y(nodes[l].handle);
          ++cnt;
        }
        if (cnt == 0) continue;
        const double leafCx = sumX / static_cast<double>(cnt);
        const double leafCy = sumY / static_cast<double>(cnt);
        const double pX = attributes.x(nodes[parentIdx].handle);
        const double pY = attributes.y(nodes[parentIdx].handle);
        // Anchor at midpoint of parent and leaf centroid.
        currentAnchor[parentIdx] = {0.5 * (pX + leafCx), 0.5 * (pY + leafCy)};
      }
      std::size_t leafBundlesApplied = 0;
      for (std::size_t i = 0; i < edges.size(); ++i) {
        auto& route = routes[i];
        if (route.size() < 2) continue;
        auto sIt = idToIdxLBA.find(edges[i].sourceModelId);
        auto tIt = idToIdxLBA.find(edges[i].targetModelId);
        if (sIt == idToIdxLBA.end() || tIt == idToIdxLBA.end()) continue;
        const std::size_t srcIdx = sIt->second;
        const std::size_t tgtIdx = tIt->second;
        auto laS = leafAnchorMap.find(srcIdx);
        if (laS != leafAnchorMap.end() && laS->second.parentIdx == tgtIdx) {
          auto anchorIt = currentAnchor.find(tgtIdx);
          if (anchorIt == currentAnchor.end()) continue;
          RoutePoint anchor;
          anchor.x = std::round(anchorIt->second.first * 100.0) / 100.0;
          anchor.y = std::round(anchorIt->second.second * 100.0) / 100.0;
          route.insert(route.end() - 1, anchor);
          ++leafBundlesApplied;
          continue;
        }
        auto laT = leafAnchorMap.find(tgtIdx);
        if (laT != leafAnchorMap.end() && laT->second.parentIdx == srcIdx) {
          auto anchorIt = currentAnchor.find(srcIdx);
          if (anchorIt == currentAnchor.end()) continue;
          RoutePoint anchor;
          anchor.x = std::round(anchorIt->second.first * 100.0) / 100.0;
          anchor.y = std::round(anchorIt->second.second * 100.0) / 100.0;
          route.insert(route.begin() + 1, anchor);
          ++leafBundlesApplied;
        }
      }
      if (leafBundlesApplied > 0 && !metadata.strategyReason.empty()) {
        metadata.strategyReason += " Leaf bundles: "
          + std::to_string(leafBundlesApplied) + " edges anchored.";
      }
    }

    // Edge bundle / corridor routing (DJERD_EDGE_BUNDLE=1). For each
    // inter-cluster pair (A, B) with ≥2 edges, redirect every edge to
    // share a corridor waypoint at the midpoint of A_center→B_center.
    // Skip the bundle if the waypoint lands in some other cluster's
    // Voronoi cell (nearest-center test) — those would create new edge-
    // node intersections through the third cluster.
    const char* edgeBundleEnv = std::getenv("DJERD_EDGE_BUNDLE");
    const bool edgeBundle = edgeBundleEnv && std::strcmp(edgeBundleEnv, "0") != 0;
    const char* edgeBundleVoronoiEnv = std::getenv("DJERD_EDGE_BUNDLE_VORONOI");
    const bool edgeBundleVoronoi =
      edgeBundleVoronoiEnv && std::strcmp(edgeBundleVoronoiEnv, "0") != 0;
    const char* portFracEnv = std::getenv("DJERD_BUNDLE_PORT_FRAC");
    const double envPortFrac = portFracEnv ? std::atof(portFracEnv) : 0.5;
    if (edgeBundle && !clusterByModelIdFull.empty() && straightLineMode) {
      // Index nodes by modelId for fast lookup.
      std::unordered_map<std::string, std::size_t> modelIdx;
      for (std::size_t i = 0; i < nodes.size(); ++i) {
        modelIdx[nodes[i].modelId] = i;
      }
      // Compute cluster-root center positions (use cluster root, not
      // member centroid, to keep waypoint stable against bubble
      // expansion/post-pass shifts).
      std::unordered_map<std::string, std::pair<double, double>> clusterRootCenter;
      for (const auto& kv : metadata.clusterByModelId) {
        auto it = modelIdx.find(kv.first);
        if (it == modelIdx.end()) continue;
        const auto& nd = nodes[it->second];
        clusterRootCenter[kv.second] = {
          attributes.x(nd.handle),
          attributes.y(nd.handle)
        };
      }
      // Flat array for nearest-cluster-center test.
      std::vector<std::pair<std::string,
                            std::pair<double, double>>> centerArr(
        clusterRootCenter.begin(), clusterRootCenter.end());
      auto nearestCid = [&](double x, double y) -> const std::string& {
        std::size_t best = 0;
        double bestD2 = std::numeric_limits<double>::infinity();
        for (std::size_t k = 0; k < centerArr.size(); ++k) {
          const double dx = x - centerArr[k].second.first;
          const double dy = y - centerArr[k].second.second;
          const double d2 = dx * dx + dy * dy;
          if (d2 < bestD2) { bestD2 = d2; best = k; }
        }
        return centerArr[best].first;
      };

      // Group edges by inter-cluster pair.
      std::map<std::pair<std::string, std::string>,
               std::vector<std::size_t>> bundles;
      for (std::size_t i = 0; i < edges.size(); ++i) {
        auto sIt = clusterByModelIdFull.find(edges[i].sourceModelId);
        auto tIt = clusterByModelIdFull.find(edges[i].targetModelId);
        if (sIt == clusterByModelIdFull.end()
            || tIt == clusterByModelIdFull.end()) continue;
        if (sIt->second == tIt->second) continue;  // intra-cluster
        const auto& sCid = sIt->second;
        const auto& tCid = tIt->second;
        auto key = sCid < tCid
          ? std::make_pair(sCid, tCid)
          : std::make_pair(tCid, sCid);
        bundles[key].push_back(i);
      }

      // For each bundle with ≥ kBundleMinEdges edges, redirect each
      // edge to share a midpoint corridor waypoint. Voronoi-validated:
      // midpoint must lie in either A's or B's cell.
      constexpr std::size_t kBundleMinEdges = 2;
      const double kPortFrac = std::clamp(envPortFrac, 0.05, 0.95);
      std::size_t bundlesApplied = 0;
      std::size_t bundlesSkippedVoronoi = 0;
      for (const auto& kv : bundles) {
        const auto& indices = kv.second;
        if (indices.size() < kBundleMinEdges) continue;
        auto aIt = clusterRootCenter.find(kv.first.first);
        auto bIt = clusterRootCenter.find(kv.first.second);
        if (aIt == clusterRootCenter.end()
            || bIt == clusterRootCenter.end()) continue;
        const double Ax = aIt->second.first, Ay = aIt->second.second;
        const double Bx = bIt->second.first, By = bIt->second.second;
        const double dx = Bx - Ax, dy = By - Ay;
        const double dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 1e-3) continue;
        const double exitX = Ax + kPortFrac * dx;
        const double exitY = Ay + kPortFrac * dy;
        const double entryX = Bx - kPortFrac * dx;
        const double entryY = By - kPortFrac * dy;
        // Voronoi validation (DJERD_EDGE_BUNDLE_VORONOI=1, default off):
        // skip the bundle if the corridor midpoint falls in some third
        // cluster's cell. Reduces bundles applied to ~25% but with the
        // strictest cell-respect.
        if (edgeBundleVoronoi) {
          const double midX = (Ax + Bx) * 0.5;
          const double midY = (Ay + By) * 0.5;
          const std::string& nearCid = nearestCid(midX, midY);
          if (nearCid != kv.first.first && nearCid != kv.first.second) {
            ++bundlesSkippedVoronoi;
            continue;
          }
        }
        for (std::size_t i : indices) {
          auto& route = routes[i];
          if (route.size() < 2) continue;
          // Determine direction: source belongs to A or B?
          auto sCidIt = clusterByModelIdFull.find(edges[i].sourceModelId);
          if (sCidIt == clusterByModelIdFull.end()) continue;
          const bool sourceIsA = (sCidIt->second == kv.first.first);
          RoutePoint exitP, entryP;
          if (sourceIsA) {
            exitP = {std::round(exitX * 100.0) / 100.0,
                     std::round(exitY * 100.0) / 100.0};
            entryP = {std::round(entryX * 100.0) / 100.0,
                      std::round(entryY * 100.0) / 100.0};
          } else {
            exitP = {std::round(entryX * 100.0) / 100.0,
                     std::round(entryY * 100.0) / 100.0};
            entryP = {std::round(exitX * 100.0) / 100.0,
                      std::round(exitY * 100.0) / 100.0};
          }
          // Replace [src, tgt] with [src, exit, entry, tgt].
          const RoutePoint src = route.front();
          const RoutePoint tgt = route.back();
          route.clear();
          route.push_back(src);
          route.push_back(exitP);
          route.push_back(entryP);
          route.push_back(tgt);
        }
        ++bundlesApplied;
      }
      if (bundlesApplied > 0 && !metadata.strategyReason.empty()) {
        metadata.strategyReason += " Edge bundles: "
          + std::to_string(bundlesApplied) + " applied, "
          + std::to_string(bundlesSkippedVoronoi) + " skipped (voronoi-3rd).";
      }
      std::fprintf(stderr,
        "[edge-bundle] Applied %zu bundles, skipped %zu (waypoint in 3rd cell).\n",
        bundlesApplied, bundlesSkippedVoronoi);
    }
    // === Carrier id pre-computation (Plan A — bundle-aware cost) ===
    // Reported edgeCrossings counts cross between distinct CARRIERS:
    //   - bundle leaves vs root → "B<idx>|<root>"
    //   - cluster pair → "C|<a>|<b>" or "Cself|<c>" (intra-cluster)
    //   - else own edgeId.
    // Pre-compute a stable carrier id per edge so cost functions
    // (leaf-untangle, visual-knot, face-untangle) can skip same-carrier
    // crosses — aligns their search direction with the metric we report.
    // Position-dependent fallback (nearest-cluster for non-cluster nodes)
    // is snapshotted now; positional drift during passes is accepted.
    // Set DJERD_CARRIER_AWARE_COST=0 to disable.
    std::vector<std::string> carrierIdByEdgePre(edges.size());
    {
      const char* caEnv = std::getenv("DJERD_CARRIER_AWARE_COST");
      const bool carrierAware = !caEnv || std::strcmp(caEnv, "0") != 0;
      if (carrierAware && (arguments.clusterGraph || arguments.bubble)) {
        std::unordered_map<std::string, std::size_t> leafToBundleIdxPre;
        for (std::size_t bi = 0; bi < metadata.leafBundles.size(); ++bi) {
          for (const std::string& leaf : metadata.leafBundles[bi].leafModelIds) {
            leafToBundleIdxPre[leaf] = bi;
          }
        }
        std::unordered_map<std::string, std::pair<double, double>>
          centroidCachePre;
        {
          std::unordered_map<std::string, std::pair<double, double>>
            sumByCluster;
          std::unordered_map<std::string, std::size_t> cntByCluster;
          std::unordered_map<std::string, std::size_t> idIdxCache;
          idIdxCache.reserve(nodes.size());
          for (std::size_t i = 0; i < nodes.size(); ++i) {
            idIdxCache[nodes[i].modelId] = i;
          }
          for (const auto& kv : clusterByModelIdFull) {
            auto idIt = idIdxCache.find(kv.first);
            if (idIt == idIdxCache.end()) continue;
            const std::size_t i = idIt->second;
            sumByCluster[kv.second].first += attributes.x(nodes[i].handle);
            sumByCluster[kv.second].second += attributes.y(nodes[i].handle);
            cntByCluster[kv.second] += 1;
          }
          for (const auto& kv : sumByCluster) {
            const std::size_t c = cntByCluster[kv.first];
            if (c == 0) continue;
            centroidCachePre[kv.first] = {
              kv.second.first / c, kv.second.second / c};
          }
          auto nearestClusterPre =
              [&](const std::string& mid) -> std::string {
            auto idIt = idIdxCache.find(mid);
            if (idIt == idIdxCache.end()) return {};
            const std::size_t i = idIt->second;
            const double mx = attributes.x(nodes[i].handle);
            const double my = attributes.y(nodes[i].handle);
            std::string best;
            double bestD2 = std::numeric_limits<double>::infinity();
            for (const auto& kv2 : centroidCachePre) {
              const double dx = mx - kv2.second.first;
              const double dy = my - kv2.second.second;
              const double d2 = dx * dx + dy * dy;
              if (d2 < bestD2) { bestD2 = d2; best = kv2.first; }
            }
            return best;
          };
          for (std::size_t e = 0; e < edges.size(); ++e) {
            const std::string& s = edges[e].sourceModelId;
            const std::string& t = edges[e].targetModelId;
            auto sBI = leafToBundleIdxPre.find(s);
            auto tBI = leafToBundleIdxPre.find(t);
            if (sBI != leafToBundleIdxPre.end()) {
              const auto& bundle = metadata.leafBundles[sBI->second];
              const auto& roots = bundle.sharedRootModelIds.empty()
                ? std::vector<std::string>{bundle.parentModelId}
                : bundle.sharedRootModelIds;
              if (std::find(roots.begin(), roots.end(), t) != roots.end()) {
                carrierIdByEdgePre[e] =
                  "B" + std::to_string(sBI->second) + "|" + t;
                continue;
              }
            }
            if (tBI != leafToBundleIdxPre.end()) {
              const auto& bundle = metadata.leafBundles[tBI->second];
              const auto& roots = bundle.sharedRootModelIds.empty()
                ? std::vector<std::string>{bundle.parentModelId}
                : bundle.sharedRootModelIds;
              if (std::find(roots.begin(), roots.end(), s) != roots.end()) {
                carrierIdByEdgePre[e] =
                  "B" + std::to_string(tBI->second) + "|" + s;
                continue;
              }
            }
            auto sCit = clusterByModelIdFull.find(s);
            auto tCit = clusterByModelIdFull.find(t);
            std::string sCluster, tCluster;
            if (sCit != clusterByModelIdFull.end()) sCluster = sCit->second;
            if (tCit != clusterByModelIdFull.end()) tCluster = tCit->second;
            if (sCluster.empty()) sCluster = nearestClusterPre(s);
            if (tCluster.empty()) tCluster = nearestClusterPre(t);
            if (!sCluster.empty() && !tCluster.empty()) {
              carrierIdByEdgePre[e] = (sCluster == tCluster)
                ? "Cself|" + sCluster
                : (sCluster < tCluster
                    ? "C|" + sCluster + "|" + tCluster
                    : "C|" + tCluster + "|" + sCluster);
              continue;
            }
            carrierIdByEdgePre[e] = edges[e].edgeId;
          }
        }
        std::set<std::string> distinctCarriersPre;
        for (const auto& cid : carrierIdByEdgePre) {
          if (!cid.empty()) distinctCarriersPre.insert(cid);
        }
        std::fprintf(stderr,
          "[carrier-pre] %zu edges → %zu distinct carriers (cost-side filter).\n",
          edges.size(), distinctCarriersPre.size());
      }
    }
    // Leaf untangle pass — for each degree-1 leaf node, check whether
    // its single edge crosses other edges and rotate the leaf around
    // its parent to a position that resolves the crossings. Bundle-
    // absorbed leaves are skipped (placement is fixed by the matrix).
    // Set DJERD_NO_LEAF_UNTANGLE=1 to skip. Multi-pass: rotated leaves
    // change positions, opening new untangling opportunities for other
    // leaves that previously had no good position.
    // Wrapped in a lambda so it can be invoked twice: once before pd-knot
    // (initial untangle) and once after visual-knot (round 2 — picks up
    // new opportunities from intervening node-position changes).
    auto runLeafUntangle = [&](int passes) {
    for (int leafPass = 0; leafPass < passes
         && (arguments.clusterGraph || arguments.bubble); ++leafPass) {
      const char* skipLuEnv = std::getenv("DJERD_NO_LEAF_UNTANGLE");
      const bool skipLu = skipLuEnv && std::strcmp(skipLuEnv, "0") != 0;
      if (!skipLu) {
        std::unordered_set<std::string> bundleAbsorbedLU;
        for (const LeafBundleRecord& b : metadata.leafBundles) {
          bundleAbsorbedLU.insert(b.parentModelId);
          for (const std::string& l : b.leafModelIds) {
            bundleAbsorbedLU.insert(l);
          }
        }
        std::unordered_map<std::string, std::size_t> idToIdxLU;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          idToIdxLU[nodes[i].modelId] = i;
        }
        // Compute degree per node from the structural edges. A leaf is
        // a node with degree exactly 1 — its single neighbor is its
        // parent. Bundle-absorbed nodes don't qualify (they're inside
        // the matrix already).
        std::vector<std::vector<std::size_t>> nbrs(nodes.size());
        std::vector<std::vector<std::size_t>> incidentEdges(nodes.size());
        for (std::size_t e = 0; e < edges.size(); ++e) {
          auto sIt = idToIdxLU.find(edges[e].sourceModelId);
          auto tIt = idToIdxLU.find(edges[e].targetModelId);
          if (sIt == idToIdxLU.end() || tIt == idToIdxLU.end()) continue;
          if (sIt->second == tIt->second) continue;
          nbrs[sIt->second].push_back(tIt->second);
          nbrs[tIt->second].push_back(sIt->second);
          incidentEdges[sIt->second].push_back(e);
          incidentEdges[tIt->second].push_back(e);
        }
        // Local edgePairs (the earlier knot-min scope's edgePairs is
        // not visible here).
        std::vector<std::pair<std::size_t, std::size_t>> edgePairs(edges.size());
        for (std::size_t e = 0; e < edges.size(); ++e) {
          auto sIt = idToIdxLU.find(edges[e].sourceModelId);
          auto tIt = idToIdxLU.find(edges[e].targetModelId);
          if (sIt == idToIdxLU.end() || tIt == idToIdxLU.end()) {
            edgePairs[e] = {0, 0};
          } else {
            edgePairs[e] = {sIt->second, tIt->second};
          }
        }
        // Cross check helper: count crossings on routes[e] vs all other
        // edges (excluding shared endpoints).
        auto sgnLU = [](double x) { return (x > 0) - (x < 0); };
        auto segCross = [&](double ax, double ay, double bx, double by,
                             double cx, double cy, double dxv, double dyv) {
          const int o1 = sgnLU((bx - ax) * (cy - ay) - (by - ay) * (cx - ax));
          const int o2 = sgnLU((bx - ax) * (dyv - ay) - (by - ay) * (dxv - ax));
          const int o3 = sgnLU((dxv - cx) * (ay - cy) - (dyv - cy) * (ax - cx));
          const int o4 = sgnLU((dxv - cx) * (by - cy) - (dyv - cy) * (bx - cx));
          return (o1 != o2) && (o3 != o4) && (o1 != 0) && (o3 != 0);
        };
        // Cost = sum over ALL incident edges of leaf, of (segment-segment
        // crossings + segment-through-node-rect penalty). Earlier version
        // only counted leaf-parent edge — fine for deg-1 leaves but missed
        // crossings on other edges of deg-2/3 nodes (loopback, bridge,
        // ring members). That mismatch caused bridge nodes (Apr 30) to
        // appear locally improved while their other edges stretched and
        // crossed many things. Extended cost makes bridge/ring nodes
        // safely placeable.
        auto leafEdgeCost = [&](std::size_t leaf, std::size_t parent,
                                 double lx, double ly) {
          (void)parent;  // anchor used only for rotation, not cost
          std::size_t total = 0;
          if (leaf >= nbrs.size()) return total;
          for (std::size_t nb : nbrs[leaf]) {
            if (nb == leaf) continue;
            const double nx = attributes.x(nodes[nb].handle);
            const double ny = attributes.y(nodes[nb].handle);
            // Plan A: find leaf-nb edge index for carrier-aware filter.
            std::size_t eLeafNb = SIZE_MAX;
            for (std::size_t ei : incidentEdges[leaf]) {
              const auto& p = edgePairs[ei];
              if ((p.first == leaf && p.second == nb)
                  || (p.first == nb && p.second == leaf)) {
                eLeafNb = ei; break;
              }
            }
            for (std::size_t e = 0; e < edges.size(); ++e) {
              const auto& p = edgePairs[e];
              if (p.first == leaf || p.second == leaf) continue;
              if (p.first == nb || p.second == nb) continue;
              // Plan A: same-carrier edges are masked in reported metric.
              if (eLeafNb != SIZE_MAX
                  && !carrierIdByEdgePre[eLeafNb].empty()
                  && carrierIdByEdgePre[eLeafNb] == carrierIdByEdgePre[e]) {
                continue;
              }
              const double ex0 = attributes.x(nodes[p.first].handle);
              const double ey0 = attributes.y(nodes[p.first].handle);
              const double ex1 = attributes.x(nodes[p.second].handle);
              const double ey1 = attributes.y(nodes[p.second].handle);
              if (segCross(lx, ly, nx, ny, ex0, ey0, ex1, ey1)) ++total;
            }
            // Edge-node intersection (×3 weight).
            RoutePoint a{lx, ly};
            RoutePoint b{nx, ny};
            for (std::size_t k = 0; k < nodes.size(); ++k) {
              if (k == leaf || k == nb) continue;
              const NodeRecord& nd = nodes[k];
              Rect r;
              r.left = attributes.x(nd.handle) - nd.width / 2.0;
              r.right = attributes.x(nd.handle) + nd.width / 2.0;
              r.top = attributes.y(nd.handle) - nd.height / 2.0;
              r.bottom = attributes.y(nd.handle) + nd.height / 2.0;
              if (segmentIntersectsRect(a, b, r)) total += 3;
            }
          }
          return total;
        };
        // Overlap check at candidate position.
        constexpr double kLuMargin = 8.0;
        auto leafOverlapAt = [&](std::size_t leaf, double lx, double ly) {
          const NodeRecord& nd = nodes[leaf];
          const double w = nd.width / 2.0 + kLuMargin;
          const double h = nd.height / 2.0 + kLuMargin;
          for (std::size_t k = 0; k < nodes.size(); ++k) {
            if (k == leaf) continue;
            const NodeRecord& md = nodes[k];
            const double mx = attributes.x(md.handle);
            const double my = attributes.y(md.handle);
            const double mw = md.width / 2.0 + kLuMargin;
            const double mh = md.height / 2.0 + kLuMargin;
            if (std::abs(lx - mx) < w + mw && std::abs(ly - my) < h + mh) {
              return true;
            }
          }
          return false;
        };
        // Per-leaf rotation: try 24 angles around parent at multiple radii
        // (was 12 angles × 1 radius, only resolved 95-127 crossings per pass
        // before plateauing with 43+ leaves still crossing). Stronger
        // search: 24 × 5 = 120 candidates per leaf.
        constexpr int kAnglesLU = 24;
        const std::array<double, 5> kRadiiMulLU = {0.5, 0.75, 1.0, 1.25, 1.5};
        const double pi = 3.14159265358979;
        // Build untangle candidate list: true leaves (deg 1) AND loopback
        // nodes (deg 2 where both neighbours are connected = the node is
        // a "lollipop tip" / triangle-vertex; positions can rotate around
        // either neighbour without breaking topology). User Apr 30:
        // "오일러 루트가 그려지면서 루트나 허브로 돌아가는 경로... 개념적으로 리프"
        struct UntangleCand {
          std::size_t node;
          std::size_t parent;
        };
        std::vector<UntangleCand> untangleCands;
        // Helper: are nodes a, b directly connected?
        auto neighborsLinked = [&](std::size_t a, std::size_t b) {
          for (std::size_t an : nbrs[a]) {
            if (an == b) return true;
          }
          return false;
        };
        for (std::size_t v = 0; v < nodes.size(); ++v) {
          if (bundleAbsorbedLU.count(nodes[v].modelId)) continue;
          if (nbrs[v].size() == 1) {
            untangleCands.push_back({v, nbrs[v][0]});
          } else if (nbrs[v].size() == 2) {
            const std::size_t a = nbrs[v][0];
            const std::size_t b = nbrs[v][1];
            if (neighborsLinked(a, b)) {
              // Loopback (triangle).
              const std::size_t aD = nbrs[a].size();
              const std::size_t bD = nbrs[b].size();
              untangleCands.push_back({v, aD >= bD ? a : b});
            } else {
              // Bridge: a, b share an EXTERNAL common neighbour (≠ v).
              // (May 1 bugfix: previous code put v itself in aNbrSet, so
              // every deg-2 non-linked was wrongly classified as bridge.)
              std::unordered_set<std::size_t> aNbrSet;
              for (std::size_t an : nbrs[a]) {
                if (an != v) aNbrSet.insert(an);
              }
              bool bridge = false;
              for (std::size_t bn : nbrs[b]) {
                if (bn == v) continue;
                if (aNbrSet.count(bn)) { bridge = true; break; }
              }
              // Chain-to-leaf: v has a neighbour with deg ≤ 2. Catches
              // user's "leaf로 끝나는 2deg연속 노드" — H-A-B-L style chain
              // where A is deg-2 with neighbours {H, B} (B itself deg-2),
              // and B is deg-2 with neighbours {A, L} (L deg-1). Anchor
              // = higher-deg side; the other (shorter-deg) edge tracks
              // through extended leafEdgeCost.
              const bool chainEnd =
                nbrs[a].size() <= 2 || nbrs[b].size() <= 2;
              if (bridge || chainEnd) {
                const std::size_t aD = nbrs[a].size();
                const std::size_t bD = nbrs[b].size();
                untangleCands.push_back({v, aD >= bD ? a : b});
              }
            }
          } else if (nbrs[v].size() == 3) {
            // Deg-3: include if at least 1 pair of neighbours is linked
            // (= v on a cycle/ring structure). Captures user's "ring
            // returning to local root" pattern: hub-R1-R2-...-hub where
            // R1's neighbours include hub and R2 (linked through cycle).
            // Relaxed from ≥2 to ≥1 pairs after leafEdgeCost extension.
            const std::size_t a = nbrs[v][0];
            const std::size_t b = nbrs[v][1];
            const std::size_t c = nbrs[v][2];
            const bool ab = neighborsLinked(a, b);
            const bool bc = neighborsLinked(b, c);
            const bool ac = neighborsLinked(a, c);
            const int linkCount = (ab ? 1 : 0) + (bc ? 1 : 0) + (ac ? 1 : 0);
            if (linkCount >= 1) {
              // Anchor = highest-degree among the linked-pair endpoints.
              std::size_t anchor = a;
              std::size_t anchorDeg = nbrs[a].size();
              if (nbrs[b].size() > anchorDeg) { anchor = b; anchorDeg = nbrs[b].size(); }
              if (nbrs[c].size() > anchorDeg) { anchor = c; anchorDeg = nbrs[c].size(); }
              untangleCands.push_back({v, anchor});
            }
          }
        }
        std::size_t leafCandidates = untangleCands.size();
        std::size_t leavesWithCross = 0;
        std::size_t leavesMoved = 0;
        std::size_t totalCrossesResolved = 0;
        for (const auto& uc : untangleCands) {
          const std::size_t leaf = uc.node;
          const std::size_t parent = uc.parent;
          if (bundleAbsorbedLU.count(nodes[parent].modelId)) continue;
          const double lx0 = attributes.x(nodes[leaf].handle);
          const double ly0 = attributes.y(nodes[leaf].handle);
          const double px = attributes.x(nodes[parent].handle);
          const double py = attributes.y(nodes[parent].handle);
          const double dx = lx0 - px;
          const double dy = ly0 - py;
          const double r = std::sqrt(dx * dx + dy * dy);
          if (r < 1.0) continue;
          const std::size_t baseCross = leafEdgeCost(leaf, parent, lx0, ly0);
          if (baseCross == 0) continue;
          ++leavesWithCross;
          double bestX = lx0, bestY = ly0;
          std::size_t bestCross = baseCross;
          for (double rMul : kRadiiMulLU) {
            const double rEff = r * rMul;
            for (int a = 0; a < kAnglesLU; ++a) {
              const double angle = 2.0 * pi * static_cast<double>(a)
                                    / static_cast<double>(kAnglesLU);
              const double cx = px + rEff * std::cos(angle);
              const double cy = py + rEff * std::sin(angle);
              if (std::abs(cx - lx0) < 1.0 && std::abs(cy - ly0) < 1.0) continue;
              if (leafOverlapAt(leaf, cx, cy)) continue;
              const std::size_t candCross =
                leafEdgeCost(leaf, parent, cx, cy);
              if (candCross < bestCross) {
                bestCross = candCross;
                bestX = cx;
                bestY = cy;
              }
            }
          }
          // Centroid grid (May 1) tested: caused +331 regression because
          // leaves placed near "centroid of neighbours" landed inside
          // leaf bundle frames, exploding bundleEdgeIntersections 44→194.
          // leafEdgeCost (straight-line) couldn't see bundle-frame
          // segments. Reverted; stuck multi-hub leaves are structural.
          if (bestCross < baseCross) {
            attributes.x(nodes[leaf].handle) =
              std::round(bestX * 100.0) / 100.0;
            attributes.y(nodes[leaf].handle) =
              std::round(bestY * 100.0) / 100.0;
            ++leavesMoved;
            totalCrossesResolved += (baseCross - bestCross);
          }
        }
        std::fprintf(stderr,
          "[leaf-untangle] candidates=%zu, with-cross=%zu, moved=%zu, "
          "crossings-resolved=%zu.\n",
          leafCandidates, leavesWithCross, leavesMoved, totalCrossesResolved);
        if (leavesMoved > 0) {
          // Update routes for moved leaves' incident edges. In straight
          // line mode each route is a 2-point polyline; rebuild those
          // endpoints from the new node positions. Detour waypoints in
          // the middle of multi-point polylines stay; the endpoints
          // pull to the new leaf position.
          for (std::size_t leaf = 0; leaf < nodes.size(); ++leaf) {
            if (nbrs[leaf].size() != 1) continue;
            if (bundleAbsorbedLU.count(nodes[leaf].modelId)) continue;
            for (std::size_t e : incidentEdges[leaf]) {
              if (e >= routes.size() || routes[e].size() < 2) continue;
              const auto& p = edgePairs[e];
              const double newX = attributes.x(nodes[leaf].handle);
              const double newY = attributes.y(nodes[leaf].handle);
              if (p.first == leaf) {
                routes[e].front() = {newX, newY};
              }
              if (p.second == leaf) {
                routes[e].back() = {newX, newY};
              }
            }
          }
        }
        // === Stuck-leaf diagnostic ===
        // After this pass, identify candidates that STILL have crossings
        // and log top-15 with details so we can analyse why
        // rotation/multi-radius can't untangle them. Emit only when this
        // is the FINAL pass (leavesMoved == 0 → early-exit OR last
        // configured pass). User Apr 30: 119 stuck leaves persist; need
        // case-by-case understanding to design next fix.
        if (leavesMoved == 0 || leafPass == passes - 1) {
          std::vector<std::tuple<std::size_t, std::size_t, std::size_t>> stuck;
          stuck.reserve(untangleCands.size());
          for (std::size_t ci = 0; ci < untangleCands.size(); ++ci) {
            const auto& uc = untangleCands[ci];
            if (uc.node >= nodes.size()) continue;
            if (bundleAbsorbedLU.count(nodes[uc.node].modelId)) continue;
            if (uc.parent >= nodes.size()) continue;
            const double lx = attributes.x(nodes[uc.node].handle);
            const double ly = attributes.y(nodes[uc.node].handle);
            const std::size_t cost =
              leafEdgeCost(uc.node, uc.parent, lx, ly);
            if (cost > 0) {
              stuck.emplace_back(cost, ci, uc.node);
            }
          }
          std::sort(stuck.begin(), stuck.end(),
                    [](const auto& a, const auto& b) {
                      return std::get<0>(a) > std::get<0>(b);
                    });
          std::fprintf(stderr,
            "[stuck-leaf-diag] %zu candidates with cost>0 (top 15 below):\n",
            stuck.size());
          const std::size_t topN = std::min(stuck.size(), std::size_t(15));
          for (std::size_t k = 0; k < topN; ++k) {
            const auto& s = stuck[k];
            const std::size_t cost = std::get<0>(s);
            const std::size_t ci = std::get<1>(s);
            const std::size_t leaf = std::get<2>(s);
            const std::size_t parent = untangleCands[ci].parent;
            const std::size_t deg =
              (leaf < nbrs.size()) ? nbrs[leaf].size() : 0;
            std::string nbrStr;
            if (leaf < nbrs.size()) {
              for (std::size_t k2 = 0; k2 < nbrs[leaf].size() && k2 < 3; ++k2) {
                if (!nbrStr.empty()) nbrStr += ",";
                nbrStr += nodes[nbrs[leaf][k2]].modelId;
              }
            }
            std::fprintf(stderr,
              "  cost=%zu deg=%zu node=%s parent=%s nbrs={%s}\n",
              cost, deg,
              nodes[leaf].modelId.c_str(),
              nodes[parent].modelId.c_str(),
              nbrStr.c_str());
          }
        }

        // Multi-pass convergence guard: stop once a pass moves nothing.
        if (leavesMoved == 0) break;
      }
    }
    };  // end runLeafUntangle lambda
    {
      const char* leafPassesEnv = std::getenv("DJERD_LEAF_PASSES");
      const int leafPasses =
        leafPassesEnv ? std::max(0, std::atoi(leafPassesEnv)) : 4;
      if (leafPasses > 0) runLeafUntangle(leafPasses);
    }

    // === xings-detour: cross-aware waypoint insertion (A+B+C+D) ===
    //
    // (A) Iteration: re-detect polyline crossings after each round of
    //     waypoint insertions. Some new crossings may emerge, others may
    //     resolve. Stops at convergence or 3 iters max.
    // (B) Polyline-based detection: only attempt pairs that ACTUALLY cross
    //     in current polyline routes (= matches reported metric, no false
    //     positives from dedup over-detection).
    // (C) Multi-segment exploration: try waypoint insertion on top-2
    //     longest segments per edge, not just the longest.
    // (D) Density-aware waypoint placement: rasterise current routes to a
    //     200×150 grid; score each candidate W by sum of densities along
    //     (segA → W) + (W → segB) — picks "open corridor" placements that
    //     are unlikely to introduce new crossings.
    //
    // Gated by DJERD_XINGS_DETOUR=1 (default ON).
    // DJERD_XINGS_DETOUR_PHASE=pre|post (default post).
    auto runXingsDetour = [&]() {
      const char* xdEnv = std::getenv("DJERD_XINGS_DETOUR");
      if (xdEnv && std::strcmp(xdEnv, "0") == 0) return;

      // (B) Polyline cross helpers.
      auto polyCross = [&](std::size_t e1, std::size_t e2) -> bool {
        if (e1 >= routes.size() || e2 >= routes.size()) return false;
        if (routes[e1].size() < 2 || routes[e2].size() < 2) return false;
        if (sharesEndpoint(edges[e1], edges[e2])) return false;
        for (std::size_t li = 1; li < routes[e1].size(); ++li) {
          for (std::size_t rj = 1; rj < routes[e2].size(); ++rj) {
            RoutePoint isect;
            if (properSegmentIntersection(
                routes[e1][li - 1], routes[e1][li],
                routes[e2][rj - 1], routes[e2][rj], isect)) {
              return true;
            }
          }
        }
        return false;
      };

      auto edgePolyCrossCount = [&](std::size_t e) -> std::size_t {
        std::size_t total = 0;
        for (std::size_t e2 = 0; e2 < edges.size(); ++e2) {
          if (e == e2) continue;
          if (polyCross(e, e2)) ++total;
        }
        return total;
      };

      // (D) Raster setup — bounds from all node positions, padded 5%.
      double rMinX = std::numeric_limits<double>::infinity();
      double rMaxX = -std::numeric_limits<double>::infinity();
      double rMinY = std::numeric_limits<double>::infinity();
      double rMaxY = -std::numeric_limits<double>::infinity();
      for (std::size_t i = 0; i < nodes.size(); ++i) {
        const double x = attributes.x(nodes[i].handle);
        const double y = attributes.y(nodes[i].handle);
        if (x < rMinX) rMinX = x;
        if (x > rMaxX) rMaxX = x;
        if (y < rMinY) rMinY = y;
        if (y > rMaxY) rMaxY = y;
      }
      const double padX = (rMaxX - rMinX) * 0.05 + 1.0;
      const double padY = (rMaxY - rMinY) * 0.05 + 1.0;
      rMinX -= padX; rMaxX += padX;
      rMinY -= padY; rMaxY += padY;
      constexpr int GW = 200, GH = 150;
      const double rangeX = rMaxX - rMinX;
      const double rangeY = rMaxY - rMinY;
      const double cellW = rangeX > 0 ? rangeX / GW : 1.0;
      const double cellH = rangeY > 0 ? rangeY / GH : 1.0;
      const char* xdIterEnv = std::getenv("DJERD_XINGS_DETOUR_ITERS");
      const int xdMaxIters = xdIterEnv ? std::max(1, std::atoi(xdIterEnv)) : 3;
      const char* xdSegEnv = std::getenv("DJERD_XINGS_DETOUR_MAX_SEGS");
      const std::size_t xdMaxSegs = xdSegEnv
        ? static_cast<std::size_t>(std::max(1, std::atoi(xdSegEnv)))
        : std::size_t(2);
      const char* xdTopKEnv = std::getenv("DJERD_XINGS_DETOUR_TOPK");
      const std::size_t xdTopK = xdTopKEnv
        ? static_cast<std::size_t>(std::max(1, std::atoi(xdTopKEnv)))
        : std::size_t(8);
      const char* xdMaxOffsetEnv = std::getenv("DJERD_XINGS_DETOUR_MAX_OFFSET");
      const double xdMaxOffset = xdMaxOffsetEnv
        ? std::max(40.0, std::atof(xdMaxOffsetEnv))
        : 200.0;

      auto worldToCell = [&](double x, double y) {
        int gx = static_cast<int>((x - rMinX) / cellW);
        int gy = static_cast<int>((y - rMinY) / cellH);
        if (gx < 0) gx = 0; else if (gx >= GW) gx = GW - 1;
        if (gy < 0) gy = 0; else if (gy >= GH) gy = GH - 1;
        return std::make_pair(gx, gy);
      };

      std::vector<std::vector<int>> density(GW, std::vector<int>(GH, 0));

      auto rasterLine = [&](double x0w, double y0w, double x1w, double y1w,
                             int delta) {
        auto [gx0, gy0] = worldToCell(x0w, y0w);
        auto [gx1, gy1] = worldToCell(x1w, y1w);
        int dx = std::abs(gx1 - gx0), dy = std::abs(gy1 - gy0);
        int sx = gx0 < gx1 ? 1 : -1;
        int sy = gy0 < gy1 ? 1 : -1;
        int err = dx - dy;
        int x = gx0, y = gy0;
        while (true) {
          density[x][y] += delta;
          if (x == gx1 && y == gy1) break;
          int e2 = err * 2;
          if (e2 > -dy) { err -= dy; x += sx; }
          if (e2 < dx) { err += dx; y += sy; }
        }
      };

      auto pathScore = [&](double x0w, double y0w, double x1w, double y1w) -> int {
        auto [gx0, gy0] = worldToCell(x0w, y0w);
        auto [gx1, gy1] = worldToCell(x1w, y1w);
        int dx = std::abs(gx1 - gx0), dy = std::abs(gy1 - gy0);
        int sx = gx0 < gx1 ? 1 : -1;
        int sy = gy0 < gy1 ? 1 : -1;
        int err = dx - dy;
        int x = gx0, y = gy0;
        int score = 0;
        while (true) {
          score += density[x][y];
          if (x == gx1 && y == gy1) break;
          int e2 = err * 2;
          if (e2 > -dy) { err -= dy; x += sx; }
          if (e2 < dx) { err += dx; y += sy; }
        }
        return score;
      };

      auto rasterRoute = [&](std::size_t e, int delta) {
        if (e >= routes.size() || routes[e].size() < 2) return;
        for (std::size_t i = 1; i < routes[e].size(); ++i) {
          rasterLine(routes[e][i - 1].x, routes[e][i - 1].y,
                     routes[e][i].x, routes[e][i].y, delta);
        }
      };

      // Build initial density.
      for (std::size_t e = 0; e < edges.size(); ++e) rasterRoute(e, +1);

      // (C) Try insertion with multi-segment + multi-offset + density-aware.
      auto tryInsertWaypoint = [&](std::size_t eMod) -> bool {
        if (eMod >= routes.size() || routes[eMod].size() < 2) return false;

        // Collect segments by length descending; consider top-2.
        std::vector<std::pair<double, std::size_t>> segByLen;
        for (std::size_t i = 1; i < routes[eMod].size(); ++i) {
          const double len = std::hypot(
              routes[eMod][i].x - routes[eMod][i - 1].x,
              routes[eMod][i].y - routes[eMod][i - 1].y);
          segByLen.emplace_back(len, i);
        }
        std::sort(segByLen.begin(), segByLen.end(),
                  [](const auto& a, const auto& b) {
                    return a.first > b.first;
                  });

        const std::size_t before = edgePolyCrossCount(eMod);
        if (before == 0) return false;

        // Subtract eMod's current contribution from density so candidate
        // scoring isn't biased by eMod's own segments.
        rasterRoute(eMod, -1);

        struct Cand {
          std::size_t segIdx;
          double wx, wy;
          int score;
        };
        std::vector<Cand> cands;
        cands.reserve(16);
        const std::array<double, 7> kOffsetMul = {0.10, 0.15, 0.25, 0.40, 0.60, 0.85, 1.10};
        const std::size_t maxSegs = std::min<std::size_t>(xdMaxSegs, segByLen.size());
        for (std::size_t s = 0; s < maxSegs; ++s) {
          const double segLen = segByLen[s].first;
          const std::size_t segIdx = segByLen[s].second;
          if (segLen < 50.0) continue;
          const auto& segA = routes[eMod][segIdx - 1];
          const auto& segB = routes[eMod][segIdx];
          const double mx = (segA.x + segB.x) * 0.5;
          const double my = (segA.y + segB.y) * 0.5;
          const double dx = segB.x - segA.x;
          const double dy = segB.y - segA.y;
          const double len = std::hypot(dx, dy);
          if (len < 1.0) continue;
          const double perpX = -dy / len;
          const double perpY = dx / len;
          for (double mul : kOffsetMul) {
            const double offset = std::min(segLen * mul, xdMaxOffset);
            for (double sign : {+1.0, -1.0}) {
              const double wx = mx + sign * offset * perpX;
              const double wy = my + sign * offset * perpY;
              const int score = pathScore(segA.x, segA.y, wx, wy)
                              + pathScore(wx, wy, segB.x, segB.y);
              cands.push_back({segIdx, wx, wy, score});
            }
          }
        }

        // (D) Sort by density score asc — try open-corridor candidates first.
        std::sort(cands.begin(), cands.end(),
                  [](const Cand& a, const Cand& b) { return a.score < b.score; });

        bool accepted = false;
        const std::size_t topK = std::min<std::size_t>(xdTopK, cands.size());
        for (std::size_t k = 0; k < topK; ++k) {
          const auto& c = cands[k];
          RoutePoint W{c.wx, c.wy};
          routes[eMod].insert(routes[eMod].begin() + c.segIdx, W);
          const std::size_t after = edgePolyCrossCount(eMod);
          if (after < before) {
            accepted = true;
            break;
          }
          routes[eMod].erase(routes[eMod].begin() + c.segIdx);
        }

        // Re-add eMod (with new waypoint or original) to density for next nodes.
        rasterRoute(eMod, +1);
        return accepted;
      };

      // (A) Iteration loop.
      std::size_t totalAccepted = 0;
      std::size_t initialPairs = 0;
      int iterCount = 0;
      for (int iter = 0; iter < xdMaxIters; ++iter) {
        ++iterCount;
        std::vector<std::pair<std::size_t, std::size_t>> pairs;
        for (std::size_t i = 0; i < edges.size(); ++i) {
          for (std::size_t j = i + 1; j < edges.size(); ++j) {
            if (polyCross(i, j)) pairs.emplace_back(i, j);
          }
        }
        if (iter == 0) initialPairs = pairs.size();
        if (pairs.empty()) break;

        std::size_t iterAccepted = 0;
        for (const auto& [e1, e2] : pairs) {
          if (!polyCross(e1, e2)) continue;
          if (tryInsertWaypoint(e1)) {
            ++iterAccepted;
          } else if (tryInsertWaypoint(e2)) {
            ++iterAccepted;
          }
        }
        totalAccepted += iterAccepted;
        if (iterAccepted == 0) break;
      }

      std::fprintf(stderr,
        "[xings-detour] iters=%d initial-poly-pairs=%zu accepted=%zu (multi-seg, density-aware, polyline detect, iterative).\n",
        iterCount, initialPairs, totalAccepted);
    };

    {
      const char* xdPhaseEnv = std::getenv("DJERD_XINGS_DETOUR_PHASE");
      const std::string xdPhase = xdPhaseEnv ? std::string(xdPhaseEnv) : "post";
      if (xdPhase == "pre") runXingsDetour();
    }

    // (1) Post-detour knot-min: cross detection on POLYLINE routes
    // (after multi-pass detour), endpoint-swap candidates evaluated by
    // their pair's polyline cross delta. Affected route endpoints
    // updated in-place (detour waypoints kept). Set DJERD_NO_PD_KNOT=1
    // to skip.
    if ((arguments.clusterGraph || arguments.bubble)
        && !clusterByModelIdFull.empty()) {
      const char* skipPDEnv = std::getenv("DJERD_NO_PD_KNOT");
      const bool skipPD = skipPDEnv && std::strcmp(skipPDEnv, "0") != 0;
      if (!skipPD) {
        std::unordered_set<std::string> bundleAbsorbedPD;
        for (const LeafBundleRecord& b : metadata.leafBundles) {
          bundleAbsorbedPD.insert(b.parentModelId);
          for (const std::string& l : b.leafModelIds) bundleAbsorbedPD.insert(l);
        }
        std::unordered_map<std::string, std::size_t> idToIdxPD;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          idToIdxPD[nodes[i].modelId] = i;
        }
        std::vector<std::pair<std::size_t, std::size_t>> edgePairsPD(edges.size());
        std::vector<std::vector<std::size_t>> edgesByNodePD(nodes.size());
        for (std::size_t e = 0; e < edges.size(); ++e) {
          auto sIt = idToIdxPD.find(edges[e].sourceModelId);
          auto tIt = idToIdxPD.find(edges[e].targetModelId);
          if (sIt == idToIdxPD.end() || tIt == idToIdxPD.end()) {
            edgePairsPD[e] = {0, 0};
            continue;
          }
          edgePairsPD[e] = {sIt->second, tIt->second};
          if (sIt->second != tIt->second) {
            edgesByNodePD[sIt->second].push_back(e);
            edgesByNodePD[tIt->second].push_back(e);
          }
        }
        auto polylineCross = [&](std::size_t e1, std::size_t e2) {
          if (e1 >= routes.size() || e2 >= routes.size()) return false;
          if (routes[e1].size() < 2 || routes[e2].size() < 2) return false;
          if (sharesEndpoint(edges[e1], edges[e2])) return false;
          for (std::size_t li = 1; li < routes[e1].size(); ++li) {
            for (std::size_t rj = 1; rj < routes[e2].size(); ++rj) {
              RoutePoint isect;
              if (properSegmentIntersection(
                  routes[e1][li - 1], routes[e1][li],
                  routes[e2][rj - 1], routes[e2][rj], isect)) {
                return true;
              }
            }
          }
          return false;
        };
        // Local cost for swap (m1, m2): sum polyline-cross over their
        // incident edges with all other edges. After swap, recount.
        auto incidentCrossCount = [&](std::size_t m1, std::size_t m2) {
          std::unordered_set<std::size_t> incident;
          for (std::size_t e : edgesByNodePD[m1]) incident.insert(e);
          for (std::size_t e : edgesByNodePD[m2]) incident.insert(e);
          std::size_t total = 0;
          for (std::size_t e1 : incident) {
            for (std::size_t e2 = 0; e2 < edges.size(); ++e2) {
              if (e1 == e2) continue;
              if (incident.count(e2) && e2 < e1) continue;
              if (polylineCross(e1, e2)) ++total;
            }
          }
          return total;
        };
        auto applyEndpointMove = [&](std::size_t node) {
          // Update route endpoints for edges incident to `node`.
          for (std::size_t e : edgesByNodePD[node]) {
            if (e >= routes.size() || routes[e].size() < 2) continue;
            const double nx = attributes.x(nodes[node].handle);
            const double ny = attributes.y(nodes[node].handle);
            if (edgePairsPD[e].first == node) routes[e].front() = {nx, ny};
            if (edgePairsPD[e].second == node) routes[e].back() = {nx, ny};
          }
        };
        // Find all polyline cross pairs.
        std::vector<std::pair<std::size_t, std::size_t>> crossPairs;
        for (std::size_t i = 0; i < edges.size(); ++i) {
          for (std::size_t j = i + 1; j < edges.size(); ++j) {
            if (polylineCross(i, j)) crossPairs.emplace_back(i, j);
          }
        }
        std::size_t pdAccepted = 0;
        for (const auto& [e1, e2] : crossPairs) {
          if (!polylineCross(e1, e2)) continue;
          const auto& p1 = edgePairsPD[e1];
          const auto& p2 = edgePairsPD[e2];
          const std::array<std::pair<std::size_t, std::size_t>, 4>
            cand = {{
              {p1.first, p2.first},
              {p1.first, p2.second},
              {p1.second, p2.first},
              {p1.second, p2.second},
            }};
          for (const auto& [m1, m2] : cand) {
            if (m1 == m2) continue;
            if (bundleAbsorbedPD.count(nodes[m1].modelId)
                || bundleAbsorbedPD.count(nodes[m2].modelId)) continue;
            const std::size_t before = incidentCrossCount(m1, m2);
            const double x1 = attributes.x(nodes[m1].handle);
            const double y1 = attributes.y(nodes[m1].handle);
            attributes.x(nodes[m1].handle) = attributes.x(nodes[m2].handle);
            attributes.y(nodes[m1].handle) = attributes.y(nodes[m2].handle);
            attributes.x(nodes[m2].handle) = x1;
            attributes.y(nodes[m2].handle) = y1;
            applyEndpointMove(m1);
            applyEndpointMove(m2);
            const std::size_t after = incidentCrossCount(m1, m2);
            if (after < before) {
              ++pdAccepted;
              break;
            }
            attributes.x(nodes[m2].handle) = attributes.x(nodes[m1].handle);
            attributes.y(nodes[m2].handle) = attributes.y(nodes[m1].handle);
            attributes.x(nodes[m1].handle) = x1;
            attributes.y(nodes[m1].handle) = y1;
            applyEndpointMove(m1);
            applyEndpointMove(m2);
          }
        }
        std::fprintf(stderr,
          "[pd-knot] %zu polyline cross pairs, %zu resolved via endpoint swap.\n",
          crossPairs.size(), pdAccepted);
      }
    }

    {
      const char* xdPhaseEnv = std::getenv("DJERD_XINGS_DETOUR_PHASE");
      const std::string xdPhase = xdPhaseEnv ? std::string(xdPhaseEnv) : "post";
      if (xdPhase != "pre") runXingsDetour();
    }

    // Coordinate-only bounded path after the useful low-memory node swaps
    // (knot-min, leaf untangle, and pd-knot), but before visual-knot and the
    // later routed-geometry worksets. Detour waypoints are intentionally not
    // emitted: the caller rebuilds all original relationships as independent
    // two-point straight lines and applies the exact direct-scene audit.
    if (readBoolEnv("DJERD_STOP_AFTER_LOW_MEMORY_POSITIONING", false)) {
      std::cout << std::fixed << std::setprecision(9);
      for (const NodeRecord& node : nodes) {
        std::cout << node.modelId << '\t'
                  << attributes.x(node.handle) << '\t'
                  << attributes.y(node.handle) << '\n';
      }
      return 0;
    }

    applyVisualKnot(nodes, edges, routes, attributes, metadata, carrierIdByEdgePre);

    // === Leaf-untangle round 2 ===
    // pd-knot, xings-detour, and visual-knot may have moved nodes (endpoint
    // swaps). That changes the geometry around leaves, possibly opening new
    // rotation angles that weren't optimal in round 1. Re-run leaf-untangle
    // with a tighter pass budget. Default 3 passes — pass 1 typically
    // resolves ~70 cross, pass 2 finds another ~25, pass 3 catches stragglers
    // (or early-exits on no-progress).
    {
      const char* leaf2Env = std::getenv("DJERD_LEAF_PASSES_2");
      const int leafPasses2 = leaf2Env ? std::max(0, std::atoi(leaf2Env)) : 3;
      if (leafPasses2 > 0) runLeafUntangle(leafPasses2);
    }

    // Final envelope-based knot-min pass — DISABLED by default.
    // Tested: detected 2,851 polyline crossing pairs after detour,
    // resolved 321 via endpoint swap, but the re-route dropped detour
    // waypoints → segment crosses ballooned 2,826 → 16,943. Set
    // DJERD_FINAL_KNOT=1 to enable for experimentation only.
    const char* finalKnotEnv = std::getenv("DJERD_FINAL_KNOT");
    const bool runFinalKnot =
      finalKnotEnv && std::strcmp(finalKnotEnv, "0") != 0;
    if (runFinalKnot && (arguments.clusterGraph || arguments.bubble)
        && !clusterByModelIdFull.empty()) {
      {
        std::unordered_set<std::string> bundleAbsorbedFK;
        for (const LeafBundleRecord& b : metadata.leafBundles) {
          bundleAbsorbedFK.insert(b.parentModelId);
          for (const std::string& l : b.leafModelIds) {
            bundleAbsorbedFK.insert(l);
          }
        }
        std::unordered_map<std::string, std::size_t> idToIdxFK;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          idToIdxFK[nodes[i].modelId] = i;
        }
        std::unordered_set<std::size_t> swappableFK;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          if (!bundleAbsorbedFK.count(nodes[i].modelId)) {
            swappableFK.insert(i);
          }
        }
        // Compute polyline AABB per edge (the "edge area").
        struct EdgeEnv {
          double left, right, top, bottom;
          std::size_t srcIdx, tgtIdx;
        };
        std::vector<EdgeEnv> envs(edges.size());
        for (std::size_t e = 0; e < edges.size(); ++e) {
          if (e >= routes.size() || routes[e].size() < 2) {
            envs[e] = {0, 0, 0, 0,
                       std::numeric_limits<std::size_t>::max(),
                       std::numeric_limits<std::size_t>::max()};
            continue;
          }
          double mn = std::numeric_limits<double>::infinity();
          double mx = -std::numeric_limits<double>::infinity();
          double tn = std::numeric_limits<double>::infinity();
          double tx = -std::numeric_limits<double>::infinity();
          for (const RoutePoint& p : routes[e]) {
            mn = std::min(mn, p.x); mx = std::max(mx, p.x);
            tn = std::min(tn, p.y); tx = std::max(tx, p.y);
          }
          auto sIt = idToIdxFK.find(edges[e].sourceModelId);
          auto tIt = idToIdxFK.find(edges[e].targetModelId);
          envs[e].left = mn; envs[e].right = mx;
          envs[e].top = tn; envs[e].bottom = tx;
          envs[e].srcIdx = (sIt != idToIdxFK.end())
            ? sIt->second : std::numeric_limits<std::size_t>::max();
          envs[e].tgtIdx = (tIt != idToIdxFK.end())
            ? tIt->second : std::numeric_limits<std::size_t>::max();
        }
        // Find AABB-overlapping edge pairs whose polylines actually
        // cross (segment-segment intersection on each segment combo).
        auto polylineCross = [&](std::size_t e1, std::size_t e2) {
          if (envs[e1].right < envs[e2].left
              || envs[e2].right < envs[e1].left
              || envs[e1].bottom < envs[e2].top
              || envs[e2].bottom < envs[e1].top) return false;
          if (sharesEndpoint(edges[e1], edges[e2])) return false;
          for (std::size_t li = 1; li < routes[e1].size(); ++li) {
            for (std::size_t rj = 1; rj < routes[e2].size(); ++rj) {
              RoutePoint isect;
              if (properSegmentIntersection(
                  routes[e1][li - 1], routes[e1][li],
                  routes[e2][rj - 1], routes[e2][rj], isect)) {
                return true;
              }
            }
          }
          return false;
        };
        // Collect crossing pairs.
        std::vector<std::pair<std::size_t, std::size_t>> crossPairs;
        for (std::size_t i = 0; i < edges.size(); ++i) {
          for (std::size_t j = i + 1; j < edges.size(); ++j) {
            if (polylineCross(i, j)) crossPairs.emplace_back(i, j);
          }
        }
        std::fprintf(stderr,
          "[final-knot] Found %zu crossing pairs after detour.\n",
          crossPairs.size());
        // For each crossing pair, try the 4 endpoint swap candidates.
        // Accept if it reduces this pair's segment-cross count without
        // creating new crosses for either edge.
        auto edgePairwiseCross = [&](std::size_t e1, std::size_t e2) {
          // Recompute straight-line cross for this pair (not polyline,
          // since we'll re-route later if accepted).
          if (sharesEndpoint(edges[e1], edges[e2])) return false;
          if (envs[e1].srcIdx == std::numeric_limits<std::size_t>::max()
              || envs[e1].tgtIdx == std::numeric_limits<std::size_t>::max()
              || envs[e2].srcIdx == std::numeric_limits<std::size_t>::max()
              || envs[e2].tgtIdx == std::numeric_limits<std::size_t>::max()) return false;
          const RoutePoint a{
            attributes.x(nodes[envs[e1].srcIdx].handle),
            attributes.y(nodes[envs[e1].srcIdx].handle)};
          const RoutePoint b{
            attributes.x(nodes[envs[e1].tgtIdx].handle),
            attributes.y(nodes[envs[e1].tgtIdx].handle)};
          const RoutePoint c{
            attributes.x(nodes[envs[e2].srcIdx].handle),
            attributes.y(nodes[envs[e2].srcIdx].handle)};
          const RoutePoint d{
            attributes.x(nodes[envs[e2].tgtIdx].handle),
            attributes.y(nodes[envs[e2].tgtIdx].handle)};
          RoutePoint isect;
          return properSegmentIntersection(a, b, c, d, isect);
        };
        std::size_t accepted = 0;
        for (const auto& [e1, e2] : crossPairs) {
          if (!edgePairwiseCross(e1, e2)) continue;  // already resolved
          const std::array<std::pair<std::size_t, std::size_t>, 4>
            cand = {{
              {envs[e1].srcIdx, envs[e2].srcIdx},
              {envs[e1].srcIdx, envs[e2].tgtIdx},
              {envs[e1].tgtIdx, envs[e2].srcIdx},
              {envs[e1].tgtIdx, envs[e2].tgtIdx},
            }};
          for (const auto& [m1, m2] : cand) {
            if (m1 == m2) continue;
            if (m1 == std::numeric_limits<std::size_t>::max()
                || m2 == std::numeric_limits<std::size_t>::max()) continue;
            if (!swappableFK.count(m1) || !swappableFK.count(m2)) continue;
            // Try swap.
            const double x1 = attributes.x(nodes[m1].handle);
            const double y1 = attributes.y(nodes[m1].handle);
            attributes.x(nodes[m1].handle) = attributes.x(nodes[m2].handle);
            attributes.y(nodes[m1].handle) = attributes.y(nodes[m2].handle);
            attributes.x(nodes[m2].handle) = x1;
            attributes.y(nodes[m2].handle) = y1;
            // Check: did this pair's cross resolve AND no new
            // overlap on m1/m2's nodes (margin-aware)?
            const bool stillCross = edgePairwiseCross(e1, e2);
            // Quick overlap probe: m1/m2's new rect vs all others.
            constexpr double kFKMargin = 8.0;
            auto rectOf = [&](std::size_t i) {
              const NodeRecord& nd = nodes[i];
              Rect r;
              r.left = attributes.x(nd.handle) - nd.width / 2.0 - kFKMargin;
              r.right = attributes.x(nd.handle) + nd.width / 2.0 + kFKMargin;
              r.top = attributes.y(nd.handle) - nd.height / 2.0 - kFKMargin;
              r.bottom = attributes.y(nd.handle) + nd.height / 2.0 + kFKMargin;
              return r;
            };
            bool newOverlap = false;
            for (std::size_t target : {m1, m2}) {
              const Rect tr = rectOf(target);
              for (std::size_t k = 0; k < nodes.size(); ++k) {
                if (k == m1 || k == m2) continue;
                if (rectsOverlap(tr, rectOf(k))) {
                  newOverlap = true; break;
                }
              }
              if (newOverlap) break;
            }
            if (!stillCross && !newOverlap) {
              ++accepted;
              break;  // success — move to next pair
            } else {
              // Revert.
              attributes.x(nodes[m2].handle) = attributes.x(nodes[m1].handle);
              attributes.y(nodes[m2].handle) = attributes.y(nodes[m1].handle);
              attributes.x(nodes[m1].handle) = x1;
              attributes.y(nodes[m1].handle) = y1;
            }
          }
        }
        if (accepted > 0) {
          std::fprintf(stderr,
            "[final-knot] Resolved %zu crossings via endpoint swap. Re-routing.\n",
            accepted);
          // Re-route edges with new positions. Same logic as initial
          // route generation — pick straight or routed mode.
          routes = straightLineMode
            ? (arguments.edgeRouting == "straight_smart" && !isStraightLineRoutingMode(arguments.mode)
              ? routeAllEdgesStraightSmart(nodes, edges, attributes)
              : routeAllEdgesStraight(edges, attributes))
            : routeAllEdges(nodes, edges, attributes, true);
        }
      }
    }

    std::vector<std::vector<std::string>> crossingIdsByEdge(edges.size());
    std::size_t totalRouteCrossings = 0;
    std::vector<EdgeCrossingRecord> crossings =
      detectRouteCrossings(edges, routes, crossingIdsByEdge, totalRouteCrossings);
    metadata.rawRouteCrossings = totalRouteCrossings;
    LayoutQualityMetrics quality =
      measureLayoutQuality(nodes, edges, routes, attributes, &metadata.leafBundles);
    quality.edgeCrossings = totalRouteCrossings;

    // Carrier-grouped edgeCrossings: bus/leaf bundles consolidate
    // multiple underlying edges into one visual carrier line. Counting
    // each underlying segment-segment cross independently overstates
    // visual cross count — viewers see only the carrier line. Re-group:
    // assign each edge a carrier id (bundle root↔bundle anchor, or own
    // edge id), then count each (carrier_a, carrier_b) pair only once.
    // Set DJERD_NO_CARRIER_CROSS=1 to skip.
    {
      const char* skipCarrierEnv = std::getenv("DJERD_NO_CARRIER_CROSS");
      const bool skipCarrier =
        skipCarrierEnv && std::strcmp(skipCarrierEnv, "0") != 0;
      if (!skipCarrier && !metadata.leafBundles.empty()) {
        std::unordered_map<std::string, std::size_t> leafToBundleIdx;
        for (std::size_t bi = 0; bi < metadata.leafBundles.size(); ++bi) {
          for (const std::string& leaf : metadata.leafBundles[bi].leafModelIds) {
            leafToBundleIdx[leaf] = bi;
          }
        }
        std::vector<std::string> carrierIdByEdge(edges.size());
        std::vector<std::pair<std::string, std::string>> carrierClustersByEdge(edges.size());
        for (std::size_t e = 0; e < edges.size(); ++e) {
          const std::string& s = edges[e].sourceModelId;
          const std::string& t = edges[e].targetModelId;
          auto sBI = leafToBundleIdx.find(s);
          auto tBI = leafToBundleIdx.find(t);
          if (sBI != leafToBundleIdx.end()) {
            const auto& bundle = metadata.leafBundles[sBI->second];
            const auto& roots = bundle.sharedRootModelIds.empty()
              ? std::vector<std::string>{bundle.parentModelId}
              : bundle.sharedRootModelIds;
            if (std::find(roots.begin(), roots.end(), t) != roots.end()) {
              carrierIdByEdge[e] =
                "B" + std::to_string(sBI->second) + "|" + t;
              continue;
            }
          }
          if (tBI != leafToBundleIdx.end()) {
            const auto& bundle = metadata.leafBundles[tBI->second];
            const auto& roots = bundle.sharedRootModelIds.empty()
              ? std::vector<std::string>{bundle.parentModelId}
              : bundle.sharedRootModelIds;
            if (std::find(roots.begin(), roots.end(), s) != roots.end()) {
              carrierIdByEdge[e] =
                "B" + std::to_string(tBI->second) + "|" + s;
              continue;
            }
          }
          // Cluster-pair carrier: edges between two clusters whose
          // endpoints are NOT in any bundle collapse to a single
          // (clusterA, clusterB) carrier.
          //
          // For nodes NOT in any cluster (connectors, routers,
          // independents), use nearest cluster by Euclidean distance
          // as a proxy — visually they sit closest to a particular
          // cluster, so an edge from such a node to a cluster member
          // appears as part of that cluster pair's bundle of edges.
          auto sCit = clusterByModelIdFull.find(s);
          auto tCit = clusterByModelIdFull.find(t);
          std::string sCluster, tCluster;
          if (sCit != clusterByModelIdFull.end()) sCluster = sCit->second;
          if (tCit != clusterByModelIdFull.end()) tCluster = tCit->second;
          if (sCluster.empty() || tCluster.empty()) {
            // Fall back to nearest-cluster lookup for non-cluster nodes.
            // Build a per-cluster centroid map once, lazily.
            static std::unordered_map<std::string, std::pair<double, double>>
              clusterCentroidCache;
            static bool centroidCacheBuilt = false;
            if (!centroidCacheBuilt) {
              std::unordered_map<std::string, std::pair<double, double>>
                sumByCluster;
              std::unordered_map<std::string, std::size_t> cntByCluster;
              for (const auto& kv : clusterByModelIdFull) {
                auto idIt = std::find_if(nodes.begin(), nodes.end(),
                  [&](const NodeRecord& n) { return n.modelId == kv.first; });
                if (idIt == nodes.end()) continue;
                const double cx = attributes.x(idIt->handle);
                const double cy = attributes.y(idIt->handle);
                sumByCluster[kv.second].first += cx;
                sumByCluster[kv.second].second += cy;
                cntByCluster[kv.second] += 1;
              }
              for (const auto& kv : sumByCluster) {
                const std::size_t c = cntByCluster[kv.first];
                if (c == 0) continue;
                clusterCentroidCache[kv.first] = {
                  kv.second.first / c, kv.second.second / c};
              }
              centroidCacheBuilt = true;
            }
            auto nearestCluster = [&](const std::string& mid) {
              auto idIt = std::find_if(nodes.begin(), nodes.end(),
                [&](const NodeRecord& n) { return n.modelId == mid; });
              if (idIt == nodes.end()) return std::string{};
              const double mx = attributes.x(idIt->handle);
              const double my = attributes.y(idIt->handle);
              std::string best;
              double bestD2 = std::numeric_limits<double>::infinity();
              for (const auto& kv : clusterCentroidCache) {
                const double dx = mx - kv.second.first;
                const double dy = my - kv.second.second;
                const double d2 = dx * dx + dy * dy;
                if (d2 < bestD2) { bestD2 = d2; best = kv.first; }
              }
              return best;
            };
            if (sCluster.empty()) sCluster = nearestCluster(s);
            if (tCluster.empty()) tCluster = nearestCluster(t);
          }
          if (!sCluster.empty() && !tCluster.empty()) {
            // Different clusters → inter-cluster carrier (existing).
            // Same cluster → intra-cluster carrier (new): every edge
            // inside the same cluster collapses to one visual line.
            // Aggressive but consistent: viewers see edges within the
            // same cluster bbox as part of the cluster's "tangle"
            // anyway, so carrier-grouping them reflects perception.
            carrierIdByEdge[e] = (sCluster == tCluster)
              ? "Cself|" + sCluster
              : (sCluster < tCluster
                  ? "C|" + sCluster + "|" + tCluster
                  : "C|" + tCluster + "|" + sCluster);
            continue;
          }
          carrierIdByEdge[e] = edges[e].edgeId;
        }
        const char* occMarginEnv = std::getenv("DJERD_CARRIER_CROSS_OCCLUSION_MARGIN");
        const double occMargin = occMarginEnv ? std::max(0.0, std::atof(occMarginEnv)) : 0.0;
        std::unordered_set<std::string> bundleAbsorbedOcc;
        for (const LeafBundleRecord& bundle : metadata.leafBundles) {
          bundleAbsorbedOcc.insert(bundle.parentModelId);
          for (const std::string& leaf : bundle.leafModelIds) {
            bundleAbsorbedOcc.insert(leaf);
          }
        }
        std::vector<Rect> carrierOcclusionRects;
        carrierOcclusionRects.reserve(nodes.size() + metadata.leafBundles.size());
        for (const NodeRecord& node : nodes) {
          if (bundleAbsorbedOcc.count(node.modelId)) continue;
          carrierOcclusionRects.push_back(nodeRect(node, attributes, occMargin));
        }
        for (const LeafBundleRecord& bundle : metadata.leafBundles) {
          carrierOcclusionRects.push_back(renderedLeafBundleRect(bundle, occMargin));
        }
        auto pointInCarrierOcclusion = [&](const RoutePoint& point) {
          if (occMargin <= 0.0) return false;
          for (const Rect& rect : carrierOcclusionRects) {
            if (
                point.x >= rect.left && point.x <= rect.right
                && point.y >= rect.top && point.y <= rect.bottom) {
              return true;
            }
          }
          return false;
        };

        const char* occMarginFinalEnv = std::getenv("DJERD_CARRIER_CROSS_OCCLUSION_MARGIN");
        const double occMarginFinal = occMarginFinalEnv
          ? std::max(0.0, std::atof(occMarginFinalEnv))
          : 0.0;
        std::unordered_set<std::string> bundleAbsorbedOccFinal;
        for (const LeafBundleRecord& bundle : metadata.leafBundles) {
          bundleAbsorbedOccFinal.insert(bundle.parentModelId);
          for (const std::string& leaf : bundle.leafModelIds) {
            bundleAbsorbedOccFinal.insert(leaf);
          }
        }
        std::vector<Rect> carrierOcclusionRectsFinal;
        carrierOcclusionRectsFinal.reserve(nodes.size() + metadata.leafBundles.size());
        for (const NodeRecord& node : nodes) {
          if (bundleAbsorbedOccFinal.count(node.modelId)) continue;
          carrierOcclusionRectsFinal.push_back(nodeRect(node, attributes, occMarginFinal));
        }
        for (const LeafBundleRecord& bundle : metadata.leafBundles) {
          carrierOcclusionRectsFinal.push_back(renderedLeafBundleRect(bundle, occMarginFinal));
        }
        auto pointInCarrierOcclusionFinal = [&](const RoutePoint& point) {
          if (occMarginFinal <= 0.0) return false;
          for (const Rect& rect : carrierOcclusionRectsFinal) {
            if (
                point.x >= rect.left && point.x <= rect.right
                && point.y >= rect.top && point.y <= rect.bottom) {
              return true;
            }
          }
          return false;
        };

        std::set<std::pair<std::string, std::string>> seenCarrierPairs;
        std::size_t carrierGroupedCross = 0;
        std::size_t carrierOccludedCross = 0;
        for (std::size_t i = 0; i < edges.size(); ++i) {
          if (i >= routes.size() || routes[i].size() < 2) continue;
          for (std::size_t j = i + 1; j < edges.size(); ++j) {
            if (j >= routes.size() || routes[j].size() < 2) continue;
            if (sharesEndpoint(edges[i], edges[j])) continue;
            if (carrierIdByEdge[i] == carrierIdByEdge[j]) continue;
            bool anyCross = false;
            for (std::size_t li = 1; li < routes[i].size() && !anyCross; ++li) {
              for (std::size_t rj = 1; rj < routes[j].size() && !anyCross; ++rj) {
                RoutePoint isect;
                if (properSegmentIntersection(
                    routes[i][li - 1], routes[i][li],
                    routes[j][rj - 1], routes[j][rj], isect)) {
                  if (pointInCarrierOcclusion(isect)) {
                    ++carrierOccludedCross;
                  } else {
                    anyCross = true;
                  }
                }
              }
            }
            if (!anyCross) continue;
            auto pk = carrierIdByEdge[i] < carrierIdByEdge[j]
              ? std::make_pair(carrierIdByEdge[i], carrierIdByEdge[j])
              : std::make_pair(carrierIdByEdge[j], carrierIdByEdge[i]);
            if (seenCarrierPairs.insert(pk).second) {
              ++carrierGroupedCross;
            }
          }
        }
        // Diagnostic: count distinct carrier ids and their distribution.
        std::set<std::string> distinctCarriers;
        std::size_t bundleCarriers = 0;
        std::size_t clusterPairCarriers = 0;
        std::size_t individualCarriers = 0;
        for (const auto& cid : carrierIdByEdge) distinctCarriers.insert(cid);
        for (const auto& cid : distinctCarriers) {
          if (cid.rfind("B", 0) == 0 && cid.find('|') != std::string::npos) ++bundleCarriers;
          else if (cid.rfind("C|", 0) == 0) ++clusterPairCarriers;
          else ++individualCarriers;
        }
        std::fprintf(stderr,
          "[carrier-cross] segment %zu -> carrier-grouped %zu (-%0.0f%%). "
          "Carriers: %zu total (%zu bundle + %zu cluster-pair + %zu individual).\n",
          totalRouteCrossings, carrierGroupedCross,
          totalRouteCrossings > 0
            ? 100.0 * (1.0 - static_cast<double>(carrierGroupedCross)
                              / static_cast<double>(totalRouteCrossings))
            : 0.0,
          distinctCarriers.size(), bundleCarriers, clusterPairCarriers,
          individualCarriers);
        quality.edgeCrossings = carrierGroupedCross;
      }
    }
    // visualCrossings is computed inside measureLayoutQuality but it
    // sums the edgeCrossings from inside that function, which doesn't
    // know about the routed-edge cross detector's count. Recompute now
    // that we've overwritten edgeCrossings with the routed value.
    quality.visualCrossings =
      quality.edgeCrossings
      + quality.edgeNodeIntersections
      + quality.nodeOverlaps
      + quality.bundleEdgeIntersections
      + quality.bundleNodeOverlaps;

    // Debug: dump overlapping node pair details (DJERD_DEBUG_OVERLAPS=1).
    {
      const char* debugOverlapsEnv = std::getenv("DJERD_DEBUG_OVERLAPS");
      const bool debugOverlaps =
        debugOverlapsEnv && std::strcmp(debugOverlapsEnv, "0") != 0;
      if (debugOverlaps && quality.nodeOverlaps > 0) {
        std::vector<std::pair<std::size_t, std::size_t>> overlapPairs;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          const Rect ri = nodeRect(nodes[i], attributes);
          for (std::size_t j = i + 1; j < nodes.size(); ++j) {
            const Rect rj = nodeRect(nodes[j], attributes);
            if (rectsOverlap(ri, rj)) {
              overlapPairs.emplace_back(i, j);
            }
          }
        }
        // Aggregate by category pair.
        auto categoryOf = [&](std::size_t idx) -> std::string {
          auto it = clusterByModelIdFull.find(nodes[idx].modelId);
          if (it == clusterByModelIdFull.end()) return std::string("(no-cluster)");
          return it->second;
        };
        std::map<std::pair<std::string, std::string>, int> pairCount;
        std::size_t intra = 0;
        std::size_t cross = 0;
        std::size_t orphan = 0;
        for (const auto& [i, j] : overlapPairs) {
          const std::string ci = categoryOf(i);
          const std::string cj = categoryOf(j);
          const bool iOrphan = (ci == "(no-cluster)");
          const bool jOrphan = (cj == "(no-cluster)");
          if (iOrphan || jOrphan) ++orphan;
          else if (ci == cj) ++intra;
          else ++cross;
          auto key = ci < cj ? std::make_pair(ci, cj) : std::make_pair(cj, ci);
          pairCount[key]++;
        }
        std::fprintf(stderr,
          "[overlap-debug] %zu overlap pairs (intra-cluster=%zu, cross-cluster=%zu, orphan=%zu)\n",
          overlapPairs.size(), intra, cross, orphan);
        std::vector<std::pair<std::pair<std::string, std::string>, int>> sorted(
          pairCount.begin(), pairCount.end());
        std::sort(sorted.begin(), sorted.end(),
          [](const auto& a, const auto& b) { return a.second > b.second; });
        const std::size_t topPairs = std::min<std::size_t>(10, sorted.size());
        for (std::size_t k = 0; k < topPairs; ++k) {
          std::fprintf(stderr, "  cluster pair: %s :: %s — %d overlaps\n",
            sorted[k].first.first.c_str(),
            sorted[k].first.second.c_str(),
            sorted[k].second);
        }
        const std::size_t topNodes = std::min<std::size_t>(8, overlapPairs.size());
        for (std::size_t k = 0; k < topNodes; ++k) {
          const auto [i, j] = overlapPairs[k];
          std::fprintf(stderr,
            "  node pair: %s [%s] (%.0f,%.0f %.0fx%.0f) vs %s [%s] (%.0f,%.0f %.0fx%.0f)\n",
            nodes[i].modelId.c_str(), categoryOf(i).c_str(),
            attributes.x(nodes[i].handle), attributes.y(nodes[i].handle),
            nodes[i].width, nodes[i].height,
            nodes[j].modelId.c_str(), categoryOf(j).c_str(),
            attributes.x(nodes[j].handle), attributes.y(nodes[j].handle),
            nodes[j].width, nodes[j].height);
        }
      }
    }

    const std::size_t isolatedNameAttached =
      attachIsolatedNodesByName(nodes, edges, attributes);
    const std::size_t isolatedBBoxCompacted =
      compactIsolatedBBoxOutliers(nodes, edges, attributes);
    const std::size_t sidecarBBoxCompacted =
      compactSidecarBBoxComponents(nodes, edges, attributes);

    // === Isolated-node stash ===
    // Nodes with degree 0 in the input edges contribute nothing to
    // crossings but still occupy main-graph layout space (cluster_graph
    // treats each as a singleton cluster). Move them to a strip on the
    // right of the connected-graph bbox so the relationships graph stays
    // visually compact and the isolated nodes group as a clean grid.
    // Apr 30 (post-1810 ceiling): user-flagged item — "edge없는 노드가
    // 클러스터에 있을 필요가 없어".
    //
    // Default ON. Disable with DJERD_ISOLATED_STASH=0.
    {
      const bool runStash =
        isolatedNameAttached == 0
        && isolatedBBoxCompacted == 0
        && sidecarBBoxCompacted == 0
        && readBoolEnv("DJERD_ISOLATED_STASH", true);
      if (runStash) {
        std::unordered_set<std::string> connectedIds;
        connectedIds.reserve(edges.size() * 2);
        for (const EdgeRecord& e : edges) {
          connectedIds.insert(e.sourceModelId);
          connectedIds.insert(e.targetModelId);
        }
        std::vector<std::size_t> isolated;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          if (!connectedIds.count(nodes[i].modelId)) isolated.push_back(i);
        }
        if (!isolated.empty()) {
          // Compute main bbox from connected nodes only.
          double mMinX = std::numeric_limits<double>::infinity();
          double mMaxX = -std::numeric_limits<double>::infinity();
          double mMinY = mMinX;
          double mMaxY = mMaxX;
          for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (!connectedIds.count(nodes[i].modelId)) continue;
            const double cx = attributes.x(nodes[i].handle);
            const double cy = attributes.y(nodes[i].handle);
            const double hw = nodes[i].width / 2.0;
            const double hh = nodes[i].height / 2.0;
            if (cx - hw < mMinX) mMinX = cx - hw;
            if (cx + hw > mMaxX) mMaxX = cx + hw;
            if (cy - hh < mMinY) mMinY = cy - hh;
            if (cy + hh > mMaxY) mMaxY = cy + hh;
          }
          if (!std::isfinite(mMinX)) {
            mMinX = 0.0; mMaxX = 1000.0;
            mMinY = 0.0; mMaxY = 1000.0;
          }
          double avgWidth = 0.0, avgHeight = 0.0;
          for (std::size_t i : isolated) {
            avgWidth += nodes[i].width;
            avgHeight += nodes[i].height;
          }
          avgWidth /= static_cast<double>(isolated.size());
          avgHeight /= static_cast<double>(isolated.size());

          constexpr double kStripGap = 300.0;
          constexpr double kCellGap = 30.0;
          constexpr int kColumns = 6;

          const double stripStartX = mMaxX + kStripGap;
          const double stripStartY = mMinY;
          const double cellW = avgWidth + kCellGap;
          const double cellH = avgHeight + kCellGap;

          for (std::size_t k = 0; k < isolated.size(); ++k) {
            const std::size_t i = isolated[k];
            const int col = static_cast<int>(k % kColumns);
            const int row = static_cast<int>(k / kColumns);
            attributes.x(nodes[i].handle) =
              stripStartX + col * cellW + nodes[i].width / 2.0;
            attributes.y(nodes[i].handle) =
              stripStartY + row * cellH + nodes[i].height / 2.0;
          }
          std::fprintf(stderr,
            "[isolated-stash] Stashed %zu edge-less nodes "
            "in %d-column strip at x>=%.0f.\n",
            isolated.size(), kColumns, stripStartX);
        }
      }
    }

    // === Face raster diagnostic ===
    // Renders the layout to a high-res integer grid, labels each pixel as
    // background / node-i / edge-e, then flood-fills background regions
    // to identify FACES (enclosed areas formed by edges + nodes).
    //
    // User May 1: real-resolution rasterisation surfaces structural info
    // (face structure) that analytical metrics can't see — useful for
    // swap/tangle decisions where understanding "which face a node sits
    // in" matters. Phase 1: diagnostic only (face count, size stats).
    //
    // Default ON. Disable with DJERD_FACE_RASTER=0.
    {
      const char* faceEnv = std::getenv("DJERD_FACE_RASTER");
      const bool runFaceRaster = !faceEnv || std::strcmp(faceEnv, "0") != 0;
      if (runFaceRaster) {
        // Bounds.
        double mnX = std::numeric_limits<double>::infinity();
        double mxX = -std::numeric_limits<double>::infinity();
        double mnY = mnX;
        double mxY = mxX;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          const double cx = attributes.x(nodes[i].handle);
          const double cy = attributes.y(nodes[i].handle);
          const double hw = nodes[i].width / 2.0;
          const double hh = nodes[i].height / 2.0;
          if (cx - hw < mnX) mnX = cx - hw;
          if (cx + hw > mxX) mxX = cx + hw;
          if (cy - hh < mnY) mnY = cy - hh;
          if (cy + hh > mxY) mxY = cy + hh;
        }
        const double padXR = (mxX - mnX) * 0.02 + 50.0;
        const double padYR = (mxY - mnY) * 0.02 + 50.0;
        mnX -= padXR; mxX += padXR;
        mnY -= padYR; mxY += padYR;

        // Grid: aim for cell ~5 units (= ~5% of node width — fine enough
        // to detect micro-faces from edge crossings, ~100× higher
        // resolution than the original 50-unit cells per user May 1
        // request: low-res can't capture cross structure).
        // bbox ~106k×86k → ~21000×17000 = ~360M cells, capped at 100M.
        constexpr double kTargetCellSize = 5.0;
        int GW = std::max(100, static_cast<int>(std::ceil((mxX - mnX) / kTargetCellSize)));
        int GH = std::max(100, static_cast<int>(std::ceil((mxY - mnY) / kTargetCellSize)));
        // Cap at 100M cells (~800MB peak for int32 × 2 grids). Configurable
        // via DJERD_FACE_RASTER_CELLS env var (millions, default 100).
        const char* maxCellsEnv = std::getenv("DJERD_FACE_RASTER_CELLS");
        const long long kMaxCells = static_cast<long long>(
          (maxCellsEnv ? std::max(1, std::atoi(maxCellsEnv)) : 100)
          * 1'000'000LL);
        if (static_cast<long long>(GW) * GH > kMaxCells) {
          const double scale = std::sqrt(
            static_cast<double>(GW) * GH / static_cast<double>(kMaxCells));
          GW = static_cast<int>(GW / scale);
          GH = static_cast<int>(GH / scale);
        }
        const double cellW = (mxX - mnX) / GW;
        const double cellH = (mxY - mnY) / GH;

        auto w2g = [&](double x, double y) {
          int gx = static_cast<int>((x - mnX) / cellW);
          int gy = static_cast<int>((y - mnY) / cellH);
          if (gx < 0) gx = 0; else if (gx >= GW) gx = GW - 1;
          if (gy < 0) gy = 0; else if (gy >= GH) gy = GH - 1;
          return std::make_pair(gx, gy);
        };

        // grid: 0=bg, >0=node label (index+1), <0=edge label (-(idx+1))
        FaceRasterGrid grid(static_cast<std::size_t>(GW) * GH, edges.size());

        // Rasterise nodes (bbox fill, node label).
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          const double cx = attributes.x(nodes[i].handle);
          const double cy = attributes.y(nodes[i].handle);
          const double hw = nodes[i].width / 2.0;
          const double hh = nodes[i].height / 2.0;
          auto [gx0, gy0] = w2g(cx - hw, cy - hh);
          auto [gx1, gy1] = w2g(cx + hw, cy + hh);
          const int32_t lab = static_cast<int32_t>(i + 1);
          for (int y = gy0; y <= gy1; ++y) {
            for (int x = gx0; x <= gx1; ++x) {
              grid.setObstacle(static_cast<std::size_t>(y) * GW + x, lab);
            }
          }
        }

        // Rasterise edges (Bresenham line per polyline segment, edge
        // label). Don't overwrite node cells.
        auto rasterLineFR = [&](double x0w, double y0w,
                                 double x1w, double y1w, int32_t lab) {
          auto [gx0, gy0] = w2g(x0w, y0w);
          auto [gx1, gy1] = w2g(x1w, y1w);
          int dx = std::abs(gx1 - gx0);
          int dy = std::abs(gy1 - gy0);
          int sx = gx0 < gx1 ? 1 : -1;
          int sy = gy0 < gy1 ? 1 : -1;
          int err = dx - dy;
          int x = gx0, y = gy0;
          while (true) {
            const std::size_t cell = static_cast<std::size_t>(y) * GW + x;
            if (grid.unassignedBackground(cell)) grid.setObstacle(cell, lab);
            if (x == gx1 && y == gy1) break;
            int e2 = err * 2;
            if (e2 > -dy) { err -= dy; x += sx; }
            if (e2 < dx) { err += dx; y += sy; }
          }
        };
        for (std::size_t e = 0; e < edges.size(); ++e) {
          if (e >= routes.size() || routes[e].size() < 2) continue;
          const int32_t lab = -(static_cast<int32_t>(e) + 1);
          for (std::size_t k = 1; k < routes[e].size(); ++k) {
            rasterLineFR(routes[e][k - 1].x, routes[e][k - 1].y,
                         routes[e][k].x, routes[e][k].y, lab);
          }
        }

        // Flood-fill background (cell == 0) → assign face IDs.
        int faceCount = 0;
        std::vector<int> faceSize;
        std::vector<std::pair<int, int>> faceBboxMin;  // gx, gy
        std::vector<std::pair<int, int>> faceBboxMax;
        for (int y = 0; y < GH; ++y) {
          for (int x = 0; x < GW; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * GW + x;
            if (!grid.unassignedBackground(idx)) continue;
            ++faceCount;
            grid.setFace(idx, faceCount);
            int sz = 0;
            int bMinX = x, bMinY = y, bMaxX = x, bMaxY = y;
            std::queue<std::size_t> q;
            q.push(idx);
            while (!q.empty()) {
              const std::size_t p = q.front();
              q.pop();
              const int px = static_cast<int>(p % GW);
              const int py = static_cast<int>(p / GW);
              ++sz;
              if (px < bMinX) bMinX = px;
              if (px > bMaxX) bMaxX = px;
              if (py < bMinY) bMinY = py;
              if (py > bMaxY) bMaxY = py;
              static const int kDx[4] = {1, -1, 0, 0};
              static const int kDy[4] = {0, 0, 1, -1};
              for (int d = 0; d < 4; ++d) {
                const int nx = px + kDx[d];
                const int ny = py + kDy[d];
                if (nx < 0 || nx >= GW || ny < 0 || ny >= GH) continue;
                const std::size_t np = static_cast<std::size_t>(ny) * GW + nx;
                if (!grid.unassignedBackground(np)) continue;
                grid.setFace(np, faceCount);
                q.push(np);
              }
            }
            faceSize.push_back(sz);
            faceBboxMin.emplace_back(bMinX, bMinY);
            faceBboxMax.emplace_back(bMaxX, bMaxY);
          }
        }

        // Stats: total bg cells, face size distribution, largest faces.
        std::size_t totalBg = 0;
        for (int sz : faceSize) totalBg += static_cast<std::size_t>(sz);
        std::vector<std::size_t> sizeOrder(faceSize.size());
        for (std::size_t k = 0; k < faceSize.size(); ++k) sizeOrder[k] = k;
        std::sort(sizeOrder.begin(), sizeOrder.end(),
                  [&](std::size_t a, std::size_t b) {
                    return faceSize[a] > faceSize[b];
                  });

        std::fprintf(stderr,
          "[face-raster] grid %dx%d (cell %.1fx%.1f units), "
          "%d faces, %zu bg cells (%.1f%% of grid)\n",
          GW, GH, cellW, cellH, faceCount, totalBg,
          100.0 * static_cast<double>(totalBg) / (GW * GH));
        const std::size_t topF = std::min(static_cast<std::size_t>(8),
                                           sizeOrder.size());
        for (std::size_t k = 0; k < topF; ++k) {
          const std::size_t fi = sizeOrder[k];
          const int sz = faceSize[fi];
          const auto& bmin = faceBboxMin[fi];
          const auto& bmax = faceBboxMax[fi];
          std::fprintf(stderr,
            "  face #%zu: %d cells (%.1f%%), bbox %dx%d @ (%d,%d)\n",
            fi + 1, sz, 100.0 * sz / static_cast<double>(totalBg),
            bmax.first - bmin.first + 1, bmax.second - bmin.second + 1,
            bmin.first, bmin.second);
        }

        // Shared face-lookup helper — used by cohesion analysis AND the
        // face-untangle stray-node pass below. Falls back to BFS outward
        // to find the nearest bg cell when query lands inside a node rect.
        auto getFaceAt = [&](double wx, double wy) -> int32_t {
          auto [cgx, cgy] = w2g(wx, wy);
          const std::size_t baseIdx =
            static_cast<std::size_t>(cgy) * GW + cgx;
          if (grid.obstacle(baseIdx) == 0) {
            return grid.face(baseIdx);
          }
          for (int rad = 1; rad < 50; ++rad) {
            for (int dy = -rad; dy <= rad; ++dy) {
              for (int dx = -rad; dx <= rad; ++dx) {
                if (std::abs(dx) != rad && std::abs(dy) != rad) continue;
                const int nx = cgx + dx;
                const int ny = cgy + dy;
                if (nx < 0 || nx >= GW || ny < 0 || ny >= GH) continue;
                const std::size_t nIdx =
                  static_cast<std::size_t>(ny) * GW + nx;
                if (grid.obstacle(nIdx) == 0) {
                  return grid.face(nIdx);
                }
              }
            }
          }
          return 0;
        };

        // === Cluster-face cohesion analysis (Phase 2 diagnostic) ===
        // For each cluster, find dominant face (= face containing most
        // members). Cohesion = % of members in dominant face. Low
        // cohesion clusters are spread across faces — candidate for
        // face-aware untangle.
        if (!clusterByModelIdFull.empty()) {

          // Group nodes by cluster.
          std::unordered_map<std::string, std::vector<std::size_t>> clusterMembers;
          for (std::size_t i = 0; i < nodes.size(); ++i) {
            auto it = clusterByModelIdFull.find(nodes[i].modelId);
            if (it != clusterByModelIdFull.end()) {
              clusterMembers[it->second].push_back(i);
            }
          }

          std::vector<int> hist(11, 0);
          std::size_t totalAnalysed = 0;
          std::size_t lowCohesion = 0;
          struct LowEntry {
            std::string cid;
            int total;
            int dominantCount;
            int faceCount;
          };
          std::vector<LowEntry> lowList;

          for (const auto& kv : clusterMembers) {
            const auto& cid = kv.first;
            const auto& members = kv.second;
            if (members.size() < 2) continue;
            std::map<int32_t, int> faceCounts;
            for (std::size_t ni : members) {
              const double cx = attributes.x(nodes[ni].handle);
              const double cy = attributes.y(nodes[ni].handle);
              const int32_t f = getFaceAt(cx, cy);
              faceCounts[f]++;
            }
            int maxCount = 0;
            for (const auto& fc : faceCounts) {
              if (fc.second > maxCount) maxCount = fc.second;
            }
            const double cohesion =
              static_cast<double>(maxCount) /
              static_cast<double>(members.size());
            const int b = std::min(10, static_cast<int>(cohesion * 10.0));
            ++hist[b];
            ++totalAnalysed;
            if (cohesion < 0.6) {
              ++lowCohesion;
              lowList.push_back({cid,
                static_cast<int>(members.size()),
                maxCount,
                static_cast<int>(faceCounts.size())});
            }
          }

          std::fprintf(stderr,
            "[face-cohesion] %zu clusters analysed, %zu low-cohesion "
            "(<60%% in dominant face)\n",
            totalAnalysed, lowCohesion);
          std::fprintf(stderr, "  cohesion histogram:\n");
          for (int b = 0; b <= 10; ++b) {
            if (hist[b] > 0) {
              const int lo = b * 10;
              const int hi = (b == 10) ? 100 : (b + 1) * 10 - 1;
              std::fprintf(stderr,
                "    %3d%%-%3d%%: %d clusters\n", lo, hi, hist[b]);
            }
          }
          std::sort(lowList.begin(), lowList.end(),
                    [](const LowEntry& a, const LowEntry& b) {
                      return a.total > b.total;
                    });
          const std::size_t topL = std::min(static_cast<std::size_t>(10),
                                             lowList.size());
          if (topL > 0) {
            std::fprintf(stderr,
              "  top %zu low-cohesion clusters (largest first):\n", topL);
            for (std::size_t k = 0; k < topL; ++k) {
              const auto& e = lowList[k];
              std::fprintf(stderr,
                "    %s: %d members, %d in dominant face, %d faces total "
                "(%.0f%%)\n",
                e.cid.c_str(), e.total, e.dominantCount, e.faceCount,
                100.0 * e.dominantCount / e.total);
            }
          }
        }

        // === A: Micro-face density map ===
        // Bucket micro-faces (size < threshold) into macro-regions.
        // Hot regions = where cross-density is highest. User May 1:
        // can guide visual-knot's swap priority.
        constexpr int kFaceMicroSize = 50;
        constexpr int kMacroCell = 64;  // pixels per macro cell
        const int MGW = (GW + kMacroCell - 1) / kMacroCell;
        const int MGH = (GH + kMacroCell - 1) / kMacroCell;
        std::vector<int> macroDensity(static_cast<std::size_t>(MGW) * MGH, 0);
        std::size_t microCount = 0;
        for (std::size_t fi = 0; fi < faceSize.size(); ++fi) {
          if (fi == 0) continue;  // skip outer face (face #1)
          if (faceSize[fi] >= kFaceMicroSize) continue;
          ++microCount;
          const int cx = (faceBboxMin[fi].first + faceBboxMax[fi].first) / 2;
          const int cy = (faceBboxMin[fi].second + faceBboxMax[fi].second) / 2;
          const int mx = cx / kMacroCell;
          const int my = cy / kMacroCell;
          if (mx >= 0 && mx < MGW && my >= 0 && my < MGH) {
            ++macroDensity[static_cast<std::size_t>(my) * MGW + mx];
          }
        }
        std::vector<std::tuple<int, int, int>> hotRegions;
        for (int my = 0; my < MGH; ++my) {
          for (int mx = 0; mx < MGW; ++mx) {
            const int cnt = macroDensity[static_cast<std::size_t>(my) * MGW + mx];
            if (cnt > 0) hotRegions.emplace_back(cnt, mx, my);
          }
        }
        std::sort(hotRegions.begin(), hotRegions.end(),
                  [](const auto& a, const auto& b) {
                    return std::get<0>(a) > std::get<0>(b);
                  });
        std::fprintf(stderr,
          "[face-density] %zu micro-faces (size<%d), top 10 hot regions "
          "(macro %dx%d cells):\n",
          microCount, kFaceMicroSize, kMacroCell, kMacroCell);
        const std::size_t topR = std::min(static_cast<std::size_t>(10),
                                           hotRegions.size());
        for (std::size_t k = 0; k < topR; ++k) {
          const int cnt = std::get<0>(hotRegions[k]);
          const int mx = std::get<1>(hotRegions[k]);
          const int my = std::get<2>(hotRegions[k]);
          const double wxMin = mnX + mx * kMacroCell * cellW;
          const double wyMin = mnY + my * kMacroCell * cellH;
          const double wxMax = wxMin + kMacroCell * cellW;
          const double wyMax = wyMin + kMacroCell * cellH;
          std::fprintf(stderr,
            "  region (%d,%d): %d micro-faces, world (%.0f,%.0f)-(%.0f,%.0f)\n",
            mx, my, cnt, wxMin, wyMin, wxMax, wyMax);
        }

        // === B: PPM output ===
        // Opt in with DJERD_FACE_PPM=1 to save /tmp/face-raster.ppm.
        // Color scheme:
        //   nodes:      dark blue
        //   edges:      dark red
        //   outer face: white (background)
        //   large face (>1% bg): light green (cluster gaps, ring interiors)
        //   med face (>0.1% bg): cyan
        //   small face: yellow
        //   micro face (<50): orange (cross debris)
        {
          const char* ppmEnv = std::getenv("DJERD_FACE_PPM");
          const bool writePpm = ppmEnv && std::strcmp(ppmEnv, "1") == 0;
          if (writePpm) {
            // High-res grid → downsample for PPM (cap at 4096×4096).
            constexpr int kPpmMaxDim = 4096;
            int sx = 1, sy = 1;
            while (GW / sx > kPpmMaxDim) sx *= 2;
            while (GH / sy > kPpmMaxDim) sy *= 2;
            const int s = std::max(sx, sy);
            const int PW = GW / s;
            const int PH = GH / s;
            const char* ppmPath = "/tmp/face-raster.ppm";
            std::FILE* fp = std::fopen(ppmPath, "wb");
            if (fp) {
              std::fprintf(fp, "P6\n%d %d\n255\n", PW, PH);
              const int largeT = static_cast<int>(totalBg * 0.01);
              const int medT = static_cast<int>(totalBg * 0.001);
              for (int py = 0; py < PH; ++py) {
                for (int px = 0; px < PW; ++px) {
                  // Downsample: pick the dominant non-bg cell in the s×s
                  // block (or representative bg face). Priority: node >
                  // edge > face. Ensures small features stay visible.
                  int32_t cellPick = 0;
                  int32_t facePick = 0;
                  for (int oy = 0; oy < s; ++oy) {
                    for (int ox = 0; ox < s; ++ox) {
                      const int gx = px * s + ox;
                      const int gy = py * s + oy;
                      if (gx >= GW || gy >= GH) continue;
                      const std::size_t idx2 = static_cast<std::size_t>(gy) * GW + gx;
                      const int32_t v = grid.obstacle(idx2);
                      if (v > 0) {
                        cellPick = v;  // node wins
                        oy = s; break;  // break both
                      } else if (v < 0 && cellPick >= 0) {
                        cellPick = v;  // edge if no node yet
                      } else if (v == 0 && cellPick == 0 && facePick == 0) {
                        facePick = grid.face(idx2);
                      }
                    }
                  }
                  unsigned char r, g, b;
                  if (cellPick > 0) {
                    r = 30; g = 50; b = 180;
                  } else if (cellPick < 0) {
                    r = 200; g = 50; b = 50;
                  } else if (facePick == 1) {
                    r = g = b = 250;
                  } else if (facePick > 0) {
                    const int sz = faceSize[static_cast<std::size_t>(facePick - 1)];
                    if (sz > largeT) { r = 200; g = 240; b = 200; }
                    else if (sz > medT) { r = 180; g = 220; b = 240; }
                    else if (sz >= kFaceMicroSize) { r = 250; g = 230; b = 160; }
                    else { r = 255; g = 180; b = 80; }
                  } else {
                    r = g = b = 200;
                  }
                  std::fputc(r, fp);
                  std::fputc(g, fp);
                  std::fputc(b, fp);
                }
              }
              std::fclose(fp);
              std::fprintf(stderr,
                "[face-ppm] Wrote %s (%dx%d, downsampled from %dx%d by %d)\n",
                ppmPath, PW, PH, GW, GH, s);
            }
          }
        }

        // === Phase 2 v2: Face-aware stray-node placement (selective) ===
        // For each "target cluster" (size 2-20, cohesion 30-60% — i.e.,
        // medium-sized clusters with members spread across multiple faces
        // but not so spread as to be hub-class), pull stray members
        // (those NOT in dominant face) toward the dominant face. Skip
        // hub clusters (size > 20 — their natural spread reflects real
        // inter-cluster connectivity).
        //
        // Default ON. Disable with DJERD_FACE_UNTANGLE=0.
        // Global revert guard: if total polyline cross rises, revert all.
        {
          const char* fuEnv = std::getenv("DJERD_FACE_UNTANGLE");
          const bool runFu = !fuEnv || std::strcmp(fuEnv, "0") != 0;
          if (runFu && !clusterByModelIdFull.empty()) {
            // Build edge pair index for cost evaluation.
            std::unordered_map<std::string, std::size_t> id2idxFu;
            id2idxFu.reserve(nodes.size());
            for (std::size_t i = 0; i < nodes.size(); ++i) {
              id2idxFu[nodes[i].modelId] = i;
            }
            std::vector<std::pair<std::size_t, std::size_t>> edgePairsFu(edges.size());
            std::vector<std::vector<std::size_t>> nbrsFu(nodes.size());
            for (std::size_t e = 0; e < edges.size(); ++e) {
              auto sIt = id2idxFu.find(edges[e].sourceModelId);
              auto tIt = id2idxFu.find(edges[e].targetModelId);
              if (sIt == id2idxFu.end() || tIt == id2idxFu.end()) {
                edgePairsFu[e] = {0, 0};
                continue;
              }
              edgePairsFu[e] = {sIt->second, tIt->second};
              if (sIt->second != tIt->second) {
                nbrsFu[sIt->second].push_back(tIt->second);
                nbrsFu[tIt->second].push_back(sIt->second);
              }
            }

            // Identify target clusters: size 2-20, cohesion 30-60%.
            std::unordered_map<std::string, std::vector<std::size_t>> allMembers;
            for (std::size_t i = 0; i < nodes.size(); ++i) {
              auto it = clusterByModelIdFull.find(nodes[i].modelId);
              if (it != clusterByModelIdFull.end()) {
                allMembers[it->second].push_back(i);
              }
            }
            std::unordered_map<std::string, std::vector<std::size_t>> targetMembers;
            std::unordered_map<std::string, int32_t> targetDomFace;
            for (const auto& kv : allMembers) {
              const auto& cid = kv.first;
              const auto& members = kv.second;
              const char* fuMaxEnv = std::getenv("DJERD_FACE_UNTANGLE_MAX");
              const std::size_t fuMax = fuMaxEnv
                ? static_cast<std::size_t>(std::max(2, std::atoi(fuMaxEnv)))
                : 20;
              if (members.size() < 2 || members.size() > fuMax) continue;
              std::map<int32_t, int> faceCounts;
              for (std::size_t ni : members) {
                const double cx = attributes.x(nodes[ni].handle);
                const double cy = attributes.y(nodes[ni].handle);
                const int32_t f = getFaceAt(cx, cy);
                faceCounts[f]++;
              }
              int maxCount = 0;
              int32_t domFace = 0;
              for (const auto& fc : faceCounts) {
                if (fc.second > maxCount) {
                  maxCount = fc.second;
                  domFace = fc.first;
                }
              }
              const double cohesion =
                static_cast<double>(maxCount) / members.size();
              const char* fuLoEnv = std::getenv("DJERD_FACE_UNTANGLE_LO");
              const char* fuHiEnv = std::getenv("DJERD_FACE_UNTANGLE_HI");
              const double fuLo = fuLoEnv ? std::atof(fuLoEnv) : 0.3;
              const double fuHi = fuHiEnv ? std::atof(fuHiEnv) : 0.6;
              if (cohesion >= fuLo && cohesion <= fuHi) {
                targetMembers[cid] = members;
                targetDomFace[cid] = domFace;
              }
            }

            if (!targetMembers.empty()) {
              // Snapshot all positions for potential revert.
              std::vector<std::pair<double, double>> snapFu(nodes.size());
              for (std::size_t i = 0; i < nodes.size(); ++i) {
                snapFu[i] = {attributes.x(nodes[i].handle),
                             attributes.y(nodes[i].handle)};
              }

              auto sgnFu = [](double x) { return (x > 0) - (x < 0); };
              auto segCrossFu = [&](double ax, double ay, double bx, double by,
                                     double cx, double cy, double dx, double dy) {
                const int o1 = sgnFu((bx-ax)*(cy-ay) - (by-ay)*(cx-ax));
                const int o2 = sgnFu((bx-ax)*(dy-ay) - (by-ay)*(dx-ax));
                const int o3 = sgnFu((dx-cx)*(ay-cy) - (dy-cy)*(ax-cx));
                const int o4 = sgnFu((dx-cx)*(by-cy) - (dy-cy)*(bx-cx));
                return (o1 != o2) && (o3 != o4) && o1 != 0 && o3 != 0;
              };
              // Cost: sum of crossings for ALL incident edges of node ni
              // when placed at (lx, ly). Carrier-aware (Plan A): skip
              // crosses against same-carrier edges since reported metric
              // doesn't count them.
              auto incidentCostFu = [&](std::size_t ni, double lx, double ly) {
                std::size_t total = 0;
                for (std::size_t nb : nbrsFu[ni]) {
                  if (nb == ni) continue;
                  const double nx = attributes.x(nodes[nb].handle);
                  const double ny = attributes.y(nodes[nb].handle);
                  std::size_t eNiNb = SIZE_MAX;
                  for (std::size_t ei = 0; ei < edges.size(); ++ei) {
                    const auto& pp = edgePairsFu[ei];
                    if ((pp.first == ni && pp.second == nb)
                        || (pp.first == nb && pp.second == ni)) {
                      eNiNb = ei; break;
                    }
                  }
                  for (std::size_t e = 0; e < edges.size(); ++e) {
                    const auto& p = edgePairsFu[e];
                    if (p.first == ni || p.second == ni) continue;
                    if (p.first == nb || p.second == nb) continue;
                    if (eNiNb != SIZE_MAX
                        && eNiNb < carrierIdByEdgePre.size()
                        && e < carrierIdByEdgePre.size()
                        && !carrierIdByEdgePre[eNiNb].empty()
                        && carrierIdByEdgePre[eNiNb] == carrierIdByEdgePre[e]) {
                      continue;
                    }
                    const double ex0 = attributes.x(nodes[p.first].handle);
                    const double ey0 = attributes.y(nodes[p.first].handle);
                    const double ex1 = attributes.x(nodes[p.second].handle);
                    const double ey1 = attributes.y(nodes[p.second].handle);
                    if (segCrossFu(lx, ly, nx, ny, ex0, ey0, ex1, ey1)) ++total;
                  }
                }
                return total;
              };

              // Compute total polyline cross before for revert guard.
              // Carrier-aware: same-carrier crosses are masked in reported.
              auto polyCrossFu = [&](std::size_t e1, std::size_t e2) -> bool {
                if (e1 >= routes.size() || e2 >= routes.size()) return false;
                if (routes[e1].size() < 2 || routes[e2].size() < 2) return false;
                if (sharesEndpoint(edges[e1], edges[e2])) return false;
                if (e1 < carrierIdByEdgePre.size() && e2 < carrierIdByEdgePre.size()
                    && !carrierIdByEdgePre[e1].empty()
                    && carrierIdByEdgePre[e1] == carrierIdByEdgePre[e2]) {
                  return false;
                }
                for (std::size_t li = 1; li < routes[e1].size(); ++li) {
                  for (std::size_t rj = 1; rj < routes[e2].size(); ++rj) {
                    RoutePoint isect;
                    if (properSegmentIntersection(
                        routes[e1][li - 1], routes[e1][li],
                        routes[e2][rj - 1], routes[e2][rj], isect)) {
                      return true;
                    }
                  }
                }
                return false;
              };
              auto totalPolyCrossFu = [&]() -> std::size_t {
                std::size_t total = 0;
                for (std::size_t i = 0; i < edges.size(); ++i) {
                  for (std::size_t j = i + 1; j < edges.size(); ++j) {
                    if (polyCrossFu(i, j)) ++total;
                  }
                }
                return total;
              };
              const std::size_t prePoly = totalPolyCrossFu();

              std::size_t totalStrays = 0;
              std::size_t totalMoved = 0;
              for (const auto& kv : targetMembers) {
                const auto& cid = kv.first;
                const auto& members = kv.second;
                const int32_t domFace = targetDomFace[cid];
                if (domFace < 1
                    || static_cast<std::size_t>(domFace) > faceBboxMin.size()) {
                  continue;
                }
                const auto& bmin = faceBboxMin[domFace - 1];
                const auto& bmax = faceBboxMax[domFace - 1];
                const double faceX0 = mnX + bmin.first * cellW;
                const double faceY0 = mnY + bmin.second * cellH;
                const double faceX1 = mnX + (bmax.first + 1) * cellW;
                const double faceY1 = mnY + (bmax.second + 1) * cellH;

                for (std::size_t ni : members) {
                  const double cx = attributes.x(nodes[ni].handle);
                  const double cy = attributes.y(nodes[ni].handle);
                  if (getFaceAt(cx, cy) == domFace) continue;
                  ++totalStrays;

                  // Try 5x5 grid inside dominant face bbox; only positions
                  // that LOOK UP to dominant face (skip cells in nodes/edges
                  // or other faces).
                  const std::size_t baseCost = incidentCostFu(ni, cx, cy);
                  std::size_t bestCost = baseCost;
                  double bestX = cx, bestY = cy;
                  constexpr int kGrid = 5;
                  for (int gy_ = 1; gy_ <= kGrid; ++gy_) {
                    for (int gx_ = 1; gx_ <= kGrid; ++gx_) {
                      const double tx = faceX0 + (faceX1 - faceX0) * gx_ / (kGrid + 1.0);
                      const double ty = faceY0 + (faceY1 - faceY0) * gy_ / (kGrid + 1.0);
                      if (getFaceAt(tx, ty) != domFace) continue;
                      const std::size_t c = incidentCostFu(ni, tx, ty);
                      if (c < bestCost) {
                        bestCost = c;
                        bestX = tx;
                        bestY = ty;
                      }
                    }
                  }
                  if (bestCost < baseCost) {
                    attributes.x(nodes[ni].handle) =
                      std::round(bestX * 100.0) / 100.0;
                    attributes.y(nodes[ni].handle) =
                      std::round(bestY * 100.0) / 100.0;
                    ++totalMoved;
                    // Update routes for ni's incident edges so
                    // polyline metric reflects new endpoint position.
                    for (std::size_t e = 0; e < edges.size(); ++e) {
                      const auto& p = edgePairsFu[e];
                      if (p.first != ni && p.second != ni) continue;
                      if (e >= routes.size() || routes[e].size() < 2) continue;
                      const double nx = attributes.x(nodes[ni].handle);
                      const double ny = attributes.y(nodes[ni].handle);
                      if (p.first == ni) routes[e].front() = {nx, ny};
                      if (p.second == ni) routes[e].back() = {nx, ny};
                    }
                  }
                }
              }

              // Revert if global polyline cross worsened.
              const std::size_t postPoly = totalPolyCrossFu();
              if (postPoly > prePoly) {
                for (std::size_t i = 0; i < nodes.size(); ++i) {
                  attributes.x(nodes[i].handle) = snapFu[i].first;
                  attributes.y(nodes[i].handle) = snapFu[i].second;
                }
                // Restore route endpoints too (any incident edge of any
                // moved node — easier: restore all edges' endpoints from
                // snap positions).
                for (std::size_t e = 0; e < edges.size(); ++e) {
                  if (e >= routes.size() || routes[e].size() < 2) continue;
                  const auto& p = edgePairsFu[e];
                  routes[e].front() = {snapFu[p.first].first, snapFu[p.first].second};
                  routes[e].back() = {snapFu[p.second].first, snapFu[p.second].second};
                }
                std::fprintf(stderr,
                  "[face-untangle] %zu target clusters, %zu strays, %zu moved "
                  "→ REVERTED (poly cross %zu → %zu)\n",
                  targetMembers.size(), totalStrays, totalMoved,
                  prePoly, postPoly);
              } else {
                std::fprintf(stderr,
                  "[face-untangle] %zu target clusters, %zu strays, %zu moved "
                  "(poly cross %zu → %zu, %zu fewer)\n",
                  targetMembers.size(), totalStrays, totalMoved,
                  prePoly, postPoly, prePoly - postPoly);
              }
            }
          }
        }

        // === Stuck-leaf 2D face-constrained placement (Plan D) ===
        // Targets deg-1 leaves with crossings that survived rotation
        // (24 angles × 5 radii) in leaf-untangle. For each stuck leaf,
        // identify the leaf's cluster's dominant face and try an 11×11
        // 2D grid of candidate positions inside that face's bbox. This
        // is wider than rotation (no fixed parent-anchor distance) but
        // constrained to the same face — preventing leaves drifting into
        // unrelated regions. Set DJERD_STUCK_LEAF_2D=0 to disable.
        {
          const char* slEnv = std::getenv("DJERD_STUCK_LEAF_2D");
          const bool runSL = !slEnv || std::strcmp(slEnv, "0") != 0;
          if (runSL && !clusterByModelIdFull.empty()) {
            std::unordered_map<std::string, std::size_t> id2idxSL;
            id2idxSL.reserve(nodes.size());
            for (std::size_t i = 0; i < nodes.size(); ++i) {
              id2idxSL[nodes[i].modelId] = i;
            }
            std::vector<std::pair<std::size_t, std::size_t>> edgePairsSL(edges.size());
            std::vector<std::vector<std::size_t>> nbrsSL(nodes.size());
            std::vector<std::vector<std::size_t>> incEdgeSL(nodes.size());
            for (std::size_t e = 0; e < edges.size(); ++e) {
              auto sIt = id2idxSL.find(edges[e].sourceModelId);
              auto tIt = id2idxSL.find(edges[e].targetModelId);
              if (sIt == id2idxSL.end() || tIt == id2idxSL.end()) {
                edgePairsSL[e] = {0, 0};
                continue;
              }
              edgePairsSL[e] = {sIt->second, tIt->second};
              if (sIt->second != tIt->second) {
                nbrsSL[sIt->second].push_back(tIt->second);
                nbrsSL[tIt->second].push_back(sIt->second);
                incEdgeSL[sIt->second].push_back(e);
                incEdgeSL[tIt->second].push_back(e);
              }
            }
            std::unordered_set<std::string> bundleAbsSL;
            for (const LeafBundleRecord& b : metadata.leafBundles) {
              bundleAbsSL.insert(b.parentModelId);
              for (const std::string& l : b.leafModelIds) {
                bundleAbsSL.insert(l);
              }
            }
            // Carrier-aware leaf cost (Plan A semantics).
            auto sgnSL = [](double x) { return (x > 0) - (x < 0); };
            auto segCrossSL = [&](double ax, double ay, double bx, double by,
                                    double cx, double cy, double dx, double dy) {
              const int o1 = sgnSL((bx-ax)*(cy-ay) - (by-ay)*(cx-ax));
              const int o2 = sgnSL((bx-ax)*(dy-ay) - (by-ay)*(dx-ax));
              const int o3 = sgnSL((dx-cx)*(ay-cy) - (dy-cy)*(ax-cx));
              const int o4 = sgnSL((dx-cx)*(by-cy) - (dy-cy)*(bx-cx));
              return (o1 != o2) && (o3 != o4) && o1 != 0 && o3 != 0;
            };
            auto leafCostSL = [&](std::size_t leaf, double lx, double ly) {
              std::size_t total = 0;
              for (std::size_t nb : nbrsSL[leaf]) {
                if (nb == leaf) continue;
                const double nx = attributes.x(nodes[nb].handle);
                const double ny = attributes.y(nodes[nb].handle);
                std::size_t eLN = SIZE_MAX;
                for (std::size_t ei : incEdgeSL[leaf]) {
                  const auto& p = edgePairsSL[ei];
                  if ((p.first == leaf && p.second == nb)
                      || (p.first == nb && p.second == leaf)) {
                    eLN = ei; break;
                  }
                }
                for (std::size_t e = 0; e < edges.size(); ++e) {
                  const auto& p = edgePairsSL[e];
                  if (p.first == leaf || p.second == leaf) continue;
                  if (p.first == nb || p.second == nb) continue;
                  if (eLN != SIZE_MAX
                      && eLN < carrierIdByEdgePre.size()
                      && e < carrierIdByEdgePre.size()
                      && !carrierIdByEdgePre[eLN].empty()
                      && carrierIdByEdgePre[eLN] == carrierIdByEdgePre[e]) {
                    continue;
                  }
                  const double ex0 = attributes.x(nodes[p.first].handle);
                  const double ey0 = attributes.y(nodes[p.first].handle);
                  const double ex1 = attributes.x(nodes[p.second].handle);
                  const double ey1 = attributes.y(nodes[p.second].handle);
                  if (segCrossSL(lx, ly, nx, ny, ex0, ey0, ex1, ey1)) ++total;
                }
              }
              return total;
            };
            auto leafOverlapSL = [&](std::size_t leaf, double lx, double ly) {
              constexpr double kSlMargin = 8.0;
              const NodeRecord& nd = nodes[leaf];
              const double w = nd.width / 2.0 + kSlMargin;
              const double h = nd.height / 2.0 + kSlMargin;
              for (std::size_t k = 0; k < nodes.size(); ++k) {
                if (k == leaf) continue;
                const NodeRecord& md = nodes[k];
                const double mx = attributes.x(md.handle);
                const double my = attributes.y(md.handle);
                const double mw = md.width / 2.0 + kSlMargin;
                const double mh = md.height / 2.0 + kSlMargin;
                if (std::abs(lx - mx) < w + mw && std::abs(ly - my) < h + mh) {
                  return true;
                }
              }
              return false;
            };
            // Snapshot for revert.
            std::vector<std::pair<double, double>> snapSL(nodes.size());
            for (std::size_t i = 0; i < nodes.size(); ++i) {
              snapSL[i] = {attributes.x(nodes[i].handle),
                           attributes.y(nodes[i].handle)};
            }
            auto polyCrossSL = [&](std::size_t e1, std::size_t e2) -> bool {
              if (e1 >= routes.size() || e2 >= routes.size()) return false;
              if (routes[e1].size() < 2 || routes[e2].size() < 2) return false;
              if (sharesEndpoint(edges[e1], edges[e2])) return false;
              if (e1 < carrierIdByEdgePre.size() && e2 < carrierIdByEdgePre.size()
                  && !carrierIdByEdgePre[e1].empty()
                  && carrierIdByEdgePre[e1] == carrierIdByEdgePre[e2]) {
                return false;
              }
              for (std::size_t li = 1; li < routes[e1].size(); ++li) {
                for (std::size_t rj = 1; rj < routes[e2].size(); ++rj) {
                  RoutePoint isect;
                  if (properSegmentIntersection(
                      routes[e1][li - 1], routes[e1][li],
                      routes[e2][rj - 1], routes[e2][rj], isect)) {
                    return true;
                  }
                }
              }
              return false;
            };
            auto totalPolySL = [&]() -> std::size_t {
              std::size_t total = 0;
              for (std::size_t i = 0; i < edges.size(); ++i) {
                for (std::size_t j = i + 1; j < edges.size(); ++j) {
                  if (polyCrossSL(i, j)) ++total;
                }
              }
              return total;
            };
            const std::size_t prePolySL = totalPolySL();
            // Plan E: 24×24 grid (was 11×11), topN default 60 (was 30),
            // multi-pass with re-collection of stuck leaves between passes.
            // Resolved leaves change geometry, opening new positions for
            // remaining stuck leaves.
            constexpr int kGridSL = 24;
            const char* topNEnv = std::getenv("DJERD_STUCK_LEAF_TOP_N");
            const std::size_t topN =
              topNEnv ? std::max(1, std::atoi(topNEnv)) : 60;
            const char* slPassEnv = std::getenv("DJERD_STUCK_LEAF_PASSES");
            const int slPasses =
              slPassEnv ? std::max(1, std::atoi(slPassEnv)) : 3;
            std::size_t leavesMovedAll = 0;
            std::size_t totalResolvedAll = 0;
            std::size_t lastLimit = 0;
            for (int pass = 0; pass < slPasses; ++pass) {
              // Re-collect stuck leaves at current positions.
              struct StuckLeaf {
                std::size_t leaf;
                std::size_t cost;
              };
              std::vector<StuckLeaf> stuck;
              for (std::size_t v = 0; v < nodes.size(); ++v) {
                if (bundleAbsSL.count(nodes[v].modelId)) continue;
                if (nbrsSL[v].size() != 1) continue;
                const double lx = attributes.x(nodes[v].handle);
                const double ly = attributes.y(nodes[v].handle);
                const std::size_t c = leafCostSL(v, lx, ly);
                if (c > 0) stuck.push_back({v, c});
              }
              std::sort(stuck.begin(), stuck.end(),
                        [](const auto& a, const auto& b) {
                          return a.cost > b.cost;
                        });
              const std::size_t limit = std::min(stuck.size(), topN);
              lastLimit = limit;
              std::size_t leavesMoved = 0;
              std::size_t totalResolved = 0;
              for (std::size_t k = 0; k < limit; ++k) {
                const std::size_t leaf = stuck[k].leaf;
                // Re-evaluate cost at current pos (may have changed if leaf's
                // neighbour was moved by an earlier leaf's processing).
                const double lxNow = attributes.x(nodes[leaf].handle);
                const double lyNow = attributes.y(nodes[leaf].handle);
                const std::size_t baseCost = leafCostSL(leaf, lxNow, lyNow);
                if (baseCost == 0) continue;
                if (nbrsSL[leaf].empty()) continue;
                const std::size_t parent = nbrsSL[leaf][0];
                const double px = attributes.x(nodes[parent].handle);
                const double py = attributes.y(nodes[parent].handle);
                const int32_t domFace = getFaceAt(px, py);
                if (domFace < 1
                    || static_cast<std::size_t>(domFace) > faceBboxMin.size()) {
                  continue;
                }
                const auto& bmin = faceBboxMin[domFace - 1];
                const auto& bmax = faceBboxMax[domFace - 1];
                const double faceX0 = mnX + bmin.first * cellW;
                const double faceY0 = mnY + bmin.second * cellH;
                const double faceX1 = mnX + (bmax.first + 1) * cellW;
                const double faceY1 = mnY + (bmax.second + 1) * cellH;
                std::size_t bestCost = baseCost;
                double bestX = lxNow, bestY = lyNow;
                for (int gy = 1; gy <= kGridSL; ++gy) {
                  for (int gx = 1; gx <= kGridSL; ++gx) {
                    const double tx =
                      faceX0 + (faceX1 - faceX0) * gx / (kGridSL + 1.0);
                    const double ty =
                      faceY0 + (faceY1 - faceY0) * gy / (kGridSL + 1.0);
                    if (getFaceAt(tx, ty) != domFace) continue;
                    if (leafOverlapSL(leaf, tx, ty)) continue;
                    const std::size_t c = leafCostSL(leaf, tx, ty);
                    if (c < bestCost) {
                      bestCost = c;
                      bestX = tx;
                      bestY = ty;
                    }
                  }
                }
                if (bestCost < baseCost) {
                  attributes.x(nodes[leaf].handle) =
                    std::round(bestX * 100.0) / 100.0;
                  attributes.y(nodes[leaf].handle) =
                    std::round(bestY * 100.0) / 100.0;
                  ++leavesMoved;
                  totalResolved += (baseCost - bestCost);
                  for (std::size_t e : incEdgeSL[leaf]) {
                    if (e >= routes.size() || routes[e].size() < 2) continue;
                    const auto& p = edgePairsSL[e];
                    const double nx = attributes.x(nodes[leaf].handle);
                    const double ny = attributes.y(nodes[leaf].handle);
                    if (p.first == leaf) routes[e].front() = {nx, ny};
                    if (p.second == leaf) routes[e].back() = {nx, ny};
                  }
                }
              }
              std::fprintf(stderr,
                "[stuck-leaf-2d] pass %d: %zu candidates, %zu moved, "
                "%zu cost units resolved.\n",
                pass + 1, limit, leavesMoved, totalResolved);
              leavesMovedAll += leavesMoved;
              totalResolvedAll += totalResolved;
              if (leavesMoved == 0) break;
            }
            const std::size_t leavesMoved = leavesMovedAll;
            const std::size_t totalResolved = totalResolvedAll;
            const std::size_t limit = lastLimit;
            const std::size_t postPolySL = totalPolySL();
            if (postPolySL > prePolySL) {
              for (std::size_t i = 0; i < nodes.size(); ++i) {
                attributes.x(nodes[i].handle) = snapSL[i].first;
                attributes.y(nodes[i].handle) = snapSL[i].second;
              }
              for (std::size_t e = 0; e < edges.size(); ++e) {
                if (e >= routes.size() || routes[e].size() < 2) continue;
                const auto& p = edgePairsSL[e];
                routes[e].front() = {snapSL[p.first].first, snapSL[p.first].second};
                routes[e].back() = {snapSL[p.second].first, snapSL[p.second].second};
              }
              std::fprintf(stderr,
                "[stuck-leaf-2d] %zu candidates, %zu moved → REVERTED "
                "(poly cross %zu → %zu)\n",
                limit, leavesMoved, prePolySL, postPolySL);
            } else {
              std::fprintf(stderr,
                "[stuck-leaf-2d] %zu candidates, %zu moved, %zu cost units "
                "resolved (poly cross %zu → %zu, %zu fewer)\n",
                limit, leavesMoved, totalResolved,
                prePolySL, postPolySL,
                prePolySL >= postPolySL ? prePolySL - postPolySL : 0);
            }
          }
        }

        // === Hot-region focused SA (Plan B) ===
        // Detect polyline crossing hotspots via spatial bucketing at
        // ~640-unit cells; for top 5 hot cells, run simulated annealing
        // on local subgraph: random pair swap accepted by Metropolis.
        // Extends visual-knot's pair-swap (cell ~300, K=3) with a wider
        // window and probabilistic uphill moves to escape local minima.
        // Set DJERD_HOT_REGION_SA=0 to disable.
        {
          const char* hrEnv = std::getenv("DJERD_HOT_REGION_SA");
          const bool runHR = !hrEnv || std::strcmp(hrEnv, "0") != 0;
          if (runHR) {
            std::unordered_map<std::string, std::size_t> id2idxHR;
            id2idxHR.reserve(nodes.size());
            for (std::size_t i = 0; i < nodes.size(); ++i) {
              id2idxHR[nodes[i].modelId] = i;
            }
            std::vector<std::pair<std::size_t, std::size_t>> edgePairsHR(edges.size());
            std::vector<std::vector<std::size_t>> incEdgeHR(nodes.size());
            for (std::size_t e = 0; e < edges.size(); ++e) {
              auto sIt = id2idxHR.find(edges[e].sourceModelId);
              auto tIt = id2idxHR.find(edges[e].targetModelId);
              if (sIt == id2idxHR.end() || tIt == id2idxHR.end()) {
                edgePairsHR[e] = {0, 0};
                continue;
              }
              edgePairsHR[e] = {sIt->second, tIt->second};
              if (sIt->second != tIt->second) {
                incEdgeHR[sIt->second].push_back(e);
                incEdgeHR[tIt->second].push_back(e);
              }
            }
            std::unordered_set<std::string> bundleAbsHR;
            for (const LeafBundleRecord& b : metadata.leafBundles) {
              bundleAbsHR.insert(b.parentModelId);
              for (const std::string& l : b.leafModelIds) {
                bundleAbsHR.insert(l);
              }
            }
            auto polyCrossHR = [&](std::size_t e1, std::size_t e2) -> bool {
              if (e1 >= routes.size() || e2 >= routes.size()) return false;
              if (routes[e1].size() < 2 || routes[e2].size() < 2) return false;
              if (sharesEndpoint(edges[e1], edges[e2])) return false;
              if (e1 < carrierIdByEdgePre.size() && e2 < carrierIdByEdgePre.size()
                  && !carrierIdByEdgePre[e1].empty()
                  && carrierIdByEdgePre[e1] == carrierIdByEdgePre[e2]) {
                return false;
              }
              for (std::size_t li = 1; li < routes[e1].size(); ++li) {
                for (std::size_t rj = 1; rj < routes[e2].size(); ++rj) {
                  RoutePoint isect;
                  if (properSegmentIntersection(
                      routes[e1][li - 1], routes[e1][li],
                      routes[e2][rj - 1], routes[e2][rj], isect)) {
                    return true;
                  }
                }
              }
              return false;
            };
            auto polyCrossPointHR = [&](std::size_t e1, std::size_t e2,
                                          RoutePoint& outPt) -> bool {
              if (e1 >= routes.size() || e2 >= routes.size()) return false;
              if (routes[e1].size() < 2 || routes[e2].size() < 2) return false;
              if (sharesEndpoint(edges[e1], edges[e2])) return false;
              if (e1 < carrierIdByEdgePre.size() && e2 < carrierIdByEdgePre.size()
                  && !carrierIdByEdgePre[e1].empty()
                  && carrierIdByEdgePre[e1] == carrierIdByEdgePre[e2]) {
                return false;
              }
              for (std::size_t li = 1; li < routes[e1].size(); ++li) {
                for (std::size_t rj = 1; rj < routes[e2].size(); ++rj) {
                  if (properSegmentIntersection(
                      routes[e1][li - 1], routes[e1][li],
                      routes[e2][rj - 1], routes[e2][rj], outPt)) {
                    return true;
                  }
                }
              }
              return false;
            };
            // Collect cross points (carrier-aware).
            struct HRCross { double x, y; std::size_t e1, e2; };
            std::vector<HRCross> crossesHR;
            for (std::size_t i = 0; i < edges.size(); ++i) {
              for (std::size_t j = i + 1; j < edges.size(); ++j) {
                RoutePoint pt;
                if (polyCrossPointHR(i, j, pt)) {
                  crossesHR.push_back({pt.x, pt.y, i, j});
                }
              }
            }
            // Spatial bucketing at 640 units.
            constexpr double kHRCellSize = 640.0;
            auto cellKeyHR = [&](double x, double y) {
              return std::make_pair(
                static_cast<long long>(std::floor(x / kHRCellSize)),
                static_cast<long long>(std::floor(y / kHRCellSize)));
            };
            std::map<std::pair<long long, long long>, std::vector<std::size_t>>
              cellMapHR;
            for (std::size_t k = 0; k < crossesHR.size(); ++k) {
              cellMapHR[cellKeyHR(crossesHR[k].x, crossesHR[k].y)].push_back(k);
            }
            std::vector<std::pair<std::size_t, std::pair<long long, long long>>>
              hotCellsHR;
            for (const auto& cm : cellMapHR) {
              if (cm.second.size() >= 4) {
                hotCellsHR.emplace_back(cm.second.size(), cm.first);
              }
            }
            std::sort(hotCellsHR.begin(), hotCellsHR.end(),
                      [](const auto& a, const auto& b) { return a.first > b.first; });
            const std::size_t initialHotsHR = hotCellsHR.size();
            const std::size_t initialCrossHR = crossesHR.size();
            const char* topHrEnv = std::getenv("DJERD_HOT_REGION_TOP");
            const std::size_t topHR = std::min(hotCellsHR.size(),
              static_cast<std::size_t>(topHrEnv ? std::max(1, std::atoi(topHrEnv)) : 5));
            // Snapshot for global revert.
            std::vector<std::pair<double, double>> snapHR(nodes.size());
            for (std::size_t i = 0; i < nodes.size(); ++i) {
              snapHR[i] = {attributes.x(nodes[i].handle),
                           attributes.y(nodes[i].handle)};
            }
            auto totalCrossHR = [&]() -> std::size_t {
              std::size_t total = 0;
              for (std::size_t i = 0; i < edges.size(); ++i) {
                for (std::size_t j = i + 1; j < edges.size(); ++j) {
                  if (polyCrossHR(i, j)) ++total;
                }
              }
              return total;
            };
            const std::size_t preCrossHR = totalCrossHR();
            auto applyMoveHR = [&](std::size_t node) {
              for (std::size_t e : incEdgeHR[node]) {
                if (e >= routes.size() || routes[e].size() < 2) continue;
                const double nx = attributes.x(nodes[node].handle);
                const double ny = attributes.y(nodes[node].handle);
                if (edgePairsHR[e].first == node) routes[e].front() = {nx, ny};
                if (edgePairsHR[e].second == node) routes[e].back() = {nx, ny};
              }
            };
            // Local cost: crossings on edges incident to a node-set.
            auto localCostHR = [&](const std::vector<std::size_t>& nodesIn) {
              std::unordered_set<std::size_t> incident;
              for (std::size_t n : nodesIn) {
                for (std::size_t e : incEdgeHR[n]) incident.insert(e);
              }
              std::size_t total = 0;
              for (std::size_t e1 : incident) {
                for (std::size_t e2 = 0; e2 < edges.size(); ++e2) {
                  if (e1 == e2) continue;
                  if (incident.count(e2) && e2 < e1) continue;
                  if (polyCrossHR(e1, e2)) ++total;
                }
              }
              return total;
            };
            std::size_t totalAcceptedHR = 0;
            std::size_t totalConsideredHR = 0;
            std::mt19937 rng(0xC0FFEE);
            const char* hrRadiusEnv = std::getenv("DJERD_HOT_REGION_RADIUS");
            const double kRadiusHR = hrRadiusEnv
              ? std::atof(hrRadiusEnv) : 640.0;
            const char* hrItersEnv = std::getenv("DJERD_HOT_REGION_ITERS");
            const int kIterPerCell = hrItersEnv
              ? std::max(50, std::atoi(hrItersEnv)) : 200;
            for (std::size_t hi = 0; hi < topHR; ++hi) {
              const auto& cell = hotCellsHR[hi].second;
              const double cxR = (cell.first + 0.5) * kHRCellSize;
              const double cyR = (cell.second + 0.5) * kHRCellSize;
              std::vector<std::size_t> regionNodes;
              for (std::size_t n = 0; n < nodes.size(); ++n) {
                if (bundleAbsHR.count(nodes[n].modelId)) continue;
                const double dx = attributes.x(nodes[n].handle) - cxR;
                const double dy = attributes.y(nodes[n].handle) - cyR;
                if (dx * dx + dy * dy <= kRadiusHR * kRadiusHR) {
                  regionNodes.push_back(n);
                }
              }
              if (regionNodes.size() < 2) continue;
              const std::size_t baseLocal = localCostHR(regionNodes);
              if (baseLocal == 0) continue;
              // Simulated annealing.
              double T = 4.0;
              constexpr double kCool = 0.92;
              std::uniform_int_distribution<std::size_t> pick(
                0, regionNodes.size() - 1);
              std::uniform_real_distribution<double> uniform(0.0, 1.0);
              std::size_t curLocal = baseLocal;
              for (int it = 0; it < kIterPerCell; ++it) {
                std::size_t a = pick(rng), b = pick(rng);
                if (a == b) continue;
                const std::size_t na = regionNodes[a];
                const std::size_t nb = regionNodes[b];
                if (na == nb) continue;
                ++totalConsideredHR;
                const double xa = attributes.x(nodes[na].handle);
                const double ya = attributes.y(nodes[na].handle);
                const double xb = attributes.x(nodes[nb].handle);
                const double yb = attributes.y(nodes[nb].handle);
                attributes.x(nodes[na].handle) = xb;
                attributes.y(nodes[na].handle) = yb;
                attributes.x(nodes[nb].handle) = xa;
                attributes.y(nodes[nb].handle) = ya;
                applyMoveHR(na);
                applyMoveHR(nb);
                const std::size_t newLocal = localCostHR(regionNodes);
                bool accept = false;
                if (newLocal < curLocal) {
                  accept = true;
                } else if (T > 0.01) {
                  const double delta =
                    static_cast<double>(newLocal) - static_cast<double>(curLocal);
                  const double prob = std::exp(-delta / T);
                  if (uniform(rng) < prob) accept = true;
                }
                if (accept) {
                  curLocal = newLocal;
                  ++totalAcceptedHR;
                } else {
                  // Revert.
                  attributes.x(nodes[na].handle) = xa;
                  attributes.y(nodes[na].handle) = ya;
                  attributes.x(nodes[nb].handle) = xb;
                  attributes.y(nodes[nb].handle) = yb;
                  applyMoveHR(na);
                  applyMoveHR(nb);
                }
                T *= kCool;
              }
            }
            const std::size_t postCrossHR = totalCrossHR();
            if (postCrossHR > preCrossHR) {
              for (std::size_t i = 0; i < nodes.size(); ++i) {
                attributes.x(nodes[i].handle) = snapHR[i].first;
                attributes.y(nodes[i].handle) = snapHR[i].second;
              }
              for (std::size_t e = 0; e < edges.size(); ++e) {
                if (e >= routes.size() || routes[e].size() < 2) continue;
                const auto& p = edgePairsHR[e];
                routes[e].front() = {snapHR[p.first].first, snapHR[p.first].second};
                routes[e].back() = {snapHR[p.second].first, snapHR[p.second].second};
              }
              std::fprintf(stderr,
                "[hot-region-sa] %zu hot cells (≥4 cross), top %zu processed, "
                "%zu/%zu swap accepted → REVERTED (cross %zu → %zu)\n",
                initialHotsHR, topHR,
                totalAcceptedHR, totalConsideredHR,
                preCrossHR, postCrossHR);
            } else {
              std::fprintf(stderr,
                "[hot-region-sa] %zu hot cells (≥4 cross, %zu cross pts), "
                "top %zu processed, %zu/%zu swap accepted "
                "(cross %zu → %zu, %zu fewer)\n",
                initialHotsHR, initialCrossHR, topHR,
                totalAcceptedHR, totalConsideredHR,
                preCrossHR, postCrossHR,
                preCrossHR >= postCrossHR ? preCrossHR - postCrossHR : 0);
            }
          }
        }
      }
    }

    // Sync route endpoints ONLY when the existing endpoint is far from
    // the node (indicating a stale endpoint left by an earlier pass that
    // moved nodes without updating routes). Threshold: gap from node bbox
    // edge > 100 units. Avoids disturbing well-routed edges (whose
    // endpoints already sit at node boundaries by design).
    {
      std::unordered_map<std::string, std::size_t> id2idxRouteSync;
      id2idxRouteSync.reserve(nodes.size());
      for (std::size_t i = 0; i < nodes.size(); ++i) {
        id2idxRouteSync[nodes[i].modelId] = i;
      }
      auto gapToBox = [&](const RoutePoint& p, const NodeRecord& nd) {
        const double cx = attributes.x(nd.handle);
        const double cy = attributes.y(nd.handle);
        const double hw = attributes.width(nd.handle) / 2.0;
        const double hh = attributes.height(nd.handle) / 2.0;
        const double dx = std::max(0.0, std::abs(p.x - cx) - hw);
        const double dy = std::max(0.0, std::abs(p.y - cy) - hh);
        return dx + dy;  // manhattan (matches audit metric)
      };
      const char* routeSyncGapEnv = std::getenv("DJERD_FINAL_ROUTE_SYNC_GAP");
      const double kGapThreshold = routeSyncGapEnv
        ? std::max(0.0, std::atof(routeSyncGapEnv))
        : 100.0;
      std::size_t synced = 0;
      for (std::size_t e = 0; e < edges.size(); ++e) {
        if (e >= routes.size() || routes[e].size() < 2) continue;
        auto sIt = id2idxRouteSync.find(edges[e].sourceModelId);
        auto tIt = id2idxRouteSync.find(edges[e].targetModelId);
        if (sIt == id2idxRouteSync.end() || tIt == id2idxRouteSync.end()) {
          continue;
        }
        const NodeRecord& sNode = nodes[sIt->second];
        const NodeRecord& tNode = nodes[tIt->second];
        const auto& fp = routes[e].front();
        const auto& bp = routes[e].back();
        const double g_fs = gapToBox(fp, sNode);
        const double g_bt = gapToBox(bp, tNode);
        const double g_ft = gapToBox(fp, tNode);
        const double g_bs = gapToBox(bp, sNode);
        const bool aOriented = (g_fs + g_bt) <= (g_ft + g_bs);
        const double worstGap = aOriented ? std::max(g_fs, g_bt)
                                          : std::max(g_ft, g_bs);
        if (worstGap <= kGapThreshold) continue;
        const Rect sourceRect = handleRect(sNode.handle, attributes);
        const Rect targetRect = handleRect(tNode.handle, attributes);
        const RoutePoint sourcePort = straightPortOnRect(sourceRect, targetRect);
        const RoutePoint targetPort = straightPortOnRect(targetRect, sourceRect);
        if (aOriented) {
          routes[e].front() = sourcePort;
          routes[e].back() = targetPort;
        } else {
          routes[e].front() = targetPort;
          routes[e].back() = sourcePort;
        }
        ++synced;
      }
      std::fprintf(stderr,
        "[final-route-sync] Pulled %zu stale route endpoints "
        "(gap > %.0f) to node boundary ports.\n",
        synced, kGapThreshold);
    }

    // Recompute leaf bundle bboxes from FINAL leaf positions. Leaves may
    // have moved during late post-passes (stuck-leaf-2d, hot-region-sa,
    // face-untangle) or via --positions-tsv override; the bbox computed
    // earlier (before those passes) is stale. Stale bbox causes:
    //  - non-leaf nodes appearing inside the rendered bundle frame
    //  - bundle clearance pass operating on wrong rect
    {
      std::unordered_map<std::string, std::size_t> id2idxFinal;
      id2idxFinal.reserve(nodes.size());
      for (std::size_t i = 0; i < nodes.size(); ++i) {
        id2idxFinal[nodes[i].modelId] = i;
      }
      for (auto& bundle : metadata.leafBundles) {
        double minX = std::numeric_limits<double>::infinity();
        double minY = std::numeric_limits<double>::infinity();
        double maxX = -std::numeric_limits<double>::infinity();
        double maxY = -std::numeric_limits<double>::infinity();
        double sumLX = 0.0, sumLY = 0.0;
        std::size_t cnt = 0;
        for (const std::string& leaf : bundle.leafModelIds) {
          auto it = id2idxFinal.find(leaf);
          if (it == id2idxFinal.end()) continue;
          const auto& nd = nodes[it->second];
          const double cx = attributes.x(nd.handle);
          const double cy = attributes.y(nd.handle);
          const double w = attributes.width(nd.handle);
          const double h = attributes.height(nd.handle);
          minX = std::min(minX, cx - w / 2.0);
          minY = std::min(minY, cy - h / 2.0);
          maxX = std::max(maxX, cx + w / 2.0);
          maxY = std::max(maxY, cy + h / 2.0);
          sumLX += cx; sumLY += cy; ++cnt;
        }
        if (cnt == 0 || !std::isfinite(minX)) continue;
        bundle.bboxX = minX;
        bundle.bboxY = minY;
        bundle.bboxWidth = maxX - minX;
        bundle.bboxHeight = maxY - minY;
        const double leafCx = sumLX / static_cast<double>(cnt);
        const double leafCy = sumLY / static_cast<double>(cnt);
        auto pit = id2idxFinal.find(bundle.parentModelId);
        if (pit != id2idxFinal.end()) {
          const double pX = attributes.x(nodes[pit->second].handle);
          const double pY = attributes.y(nodes[pit->second].handle);
          bundle.anchorX = 0.5 * (pX + leafCx);
          bundle.anchorY = 0.5 * (pY + leafCy);
        }
      }
    }

    // Optional final node-edge relief. This is deliberately not an edge
    // detour: routes keep their existing straight/carrier shape, while
    // non-endpoint nodes or rendered leaf-bundle blocks that sit on top of
    // those routes are translated away from the route segment.
    {
      const char* nodeEdgeReliefEnv = std::getenv("DJERD_NODE_EDGE_RELIEF_FINAL");
      const bool nodeEdgeRelief =
        nodeEdgeReliefEnv && std::strcmp(nodeEdgeReliefEnv, "0") != 0;
      if (nodeEdgeRelief) {
        const char* passesEnv = std::getenv("DJERD_NODE_EDGE_RELIEF_FINAL_PASSES");
        const int passes = passesEnv ? std::max(1, std::atoi(passesEnv)) : 2;
        const char* maxShiftEnv = std::getenv("DJERD_NODE_EDGE_RELIEF_FINAL_MAX_SHIFT");
        const double maxShift = maxShiftEnv ? std::max(8.0, std::atof(maxShiftEnv)) : 140.0;
        const char* strengthEnv = std::getenv("DJERD_NODE_EDGE_RELIEF_FINAL_STRENGTH");
        const double strength = strengthEnv ? std::max(0.05, std::atof(strengthEnv)) : 0.65;
        const char* noSeparationEnv =
          std::getenv("DJERD_NODE_EDGE_RELIEF_FINAL_NO_SEPARATION");
        const bool skipReliefSeparation =
          noSeparationEnv && std::strcmp(noSeparationEnv, "0") != 0;
        const char* groupFactorEnv =
          std::getenv("DJERD_NODE_EDGE_RELIEF_FINAL_GROUP_FACTOR");
        const double groupFactor = groupFactorEnv
          ? std::clamp(std::atof(groupFactorEnv), 0.0, 1.0)
          : 0.0;
        const char* groupMaxEnv =
          std::getenv("DJERD_NODE_EDGE_RELIEF_FINAL_GROUP_MAX");
        const int groupMax = groupMaxEnv ? std::max(0, std::atoi(groupMaxEnv)) : 0;
        const double kReliefMargin = visualNodeMargin();
        const double kReliefBundleMargin = leafBundleVisualMargin();

        std::unordered_map<std::string, std::size_t> id2idxNER;
        id2idxNER.reserve(nodes.size());
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          id2idxNER[nodes[i].modelId] = i;
        }
        std::vector<std::vector<std::size_t>> adjacencyNER(nodes.size());
        for (const EdgeRecord& edge : edges) {
          auto sIt = id2idxNER.find(edge.sourceModelId);
          auto tIt = id2idxNER.find(edge.targetModelId);
          if (sIt == id2idxNER.end() || tIt == id2idxNER.end()) continue;
          adjacencyNER[sIt->second].push_back(tIt->second);
          adjacencyNER[tIt->second].push_back(sIt->second);
        }
        std::unordered_map<std::string, std::vector<std::size_t>> clusterMembersNER;
        if (groupFactor > 0.0 && groupMax > 0) {
          for (std::size_t i = 0; i < nodes.size(); ++i) {
            auto cIt = clusterByModelIdFull.find(nodes[i].modelId);
            if (cIt == clusterByModelIdFull.end() || cIt->second.empty()) continue;
            clusterMembersNER[cIt->second].push_back(i);
          }
        }

        auto syncRouteEndpointsNER = [&]() {
          for (std::size_t e = 0; e < edges.size(); ++e) {
            if (e >= routes.size() || routes[e].size() < 2) continue;
            auto sIt = id2idxNER.find(edges[e].sourceModelId);
            auto tIt = id2idxNER.find(edges[e].targetModelId);
            if (sIt == id2idxNER.end() || tIt == id2idxNER.end()) continue;
            const NodeRecord& sNode = nodes[sIt->second];
            const NodeRecord& tNode = nodes[tIt->second];
            const Rect sRect = handleRect(sNode.handle, attributes);
            const Rect tRect = handleRect(tNode.handle, attributes);
            const RoutePoint sPt = straightPortOnRect(sRect, tRect);
            const RoutePoint tPt = straightPortOnRect(tRect, sRect);
            const auto dist2 = [](const RoutePoint& a, const RoutePoint& b) {
              const double dx = a.x - b.x;
              const double dy = a.y - b.y;
              return dx * dx + dy * dy;
            };
            const double direct =
              dist2(routes[e].front(), sPt) + dist2(routes[e].back(), tPt);
            const double reversed =
              dist2(routes[e].front(), tPt) + dist2(routes[e].back(), sPt);
            if (direct <= reversed) {
              routes[e].front() = sPt;
              routes[e].back() = tPt;
            } else {
              routes[e].front() = tPt;
              routes[e].back() = sPt;
            }
          }
        };

        auto recomputeLeafBundlesNER = [&]() {
          for (auto& bundle : metadata.leafBundles) {
            double minX = std::numeric_limits<double>::infinity();
            double minY = std::numeric_limits<double>::infinity();
            double maxX = -std::numeric_limits<double>::infinity();
            double maxY = -std::numeric_limits<double>::infinity();
            double sumLX = 0.0, sumLY = 0.0;
            std::size_t cnt = 0;
            for (const std::string& leaf : bundle.leafModelIds) {
              auto it = id2idxNER.find(leaf);
              if (it == id2idxNER.end()) continue;
              const auto& nd = nodes[it->second];
              const double cx = attributes.x(nd.handle);
              const double cy = attributes.y(nd.handle);
              const double w = attributes.width(nd.handle);
              const double h = attributes.height(nd.handle);
              minX = std::min(minX, cx - w / 2.0);
              minY = std::min(minY, cy - h / 2.0);
              maxX = std::max(maxX, cx + w / 2.0);
              maxY = std::max(maxY, cy + h / 2.0);
              sumLX += cx; sumLY += cy; ++cnt;
            }
            if (cnt == 0 || !std::isfinite(minX)) continue;
            bundle.bboxX = minX;
            bundle.bboxY = minY;
            bundle.bboxWidth = maxX - minX;
            bundle.bboxHeight = maxY - minY;
            const double leafCx = sumLX / static_cast<double>(cnt);
            const double leafCy = sumLY / static_cast<double>(cnt);
            auto pit = id2idxNER.find(bundle.parentModelId);
            if (pit != id2idxNER.end()) {
              const double pX = attributes.x(nodes[pit->second].handle);
              const double pY = attributes.y(nodes[pit->second].handle);
              bundle.anchorX = 0.5 * (pX + leafCx);
              bundle.anchorY = 0.5 * (pY + leafCy);
            }
          }
        };

        auto obstructionScoreNER = [&]() {
          const LayoutQualityMetrics qm =
            measureLayoutQuality(nodes, edges, routes, attributes, &metadata.leafBundles);
          return
            static_cast<double>(qm.edgeNodeIntersections)
            + static_cast<double>(qm.bundleEdgeIntersections)
            + 50.0 * static_cast<double>(qm.nodeOverlaps)
            + 25.0 * static_cast<double>(qm.bundleNodeOverlaps);
        };

        auto addReliefShift = [&](std::vector<double>& shiftX,
                                  std::vector<double>& shiftY,
                                  std::size_t nodeIdx,
                                  std::size_t srcIdx,
                                  std::size_t tgtIdx,
                                  const RoutePoint& a,
                                  const RoutePoint& b,
                                  const Rect& rect) {
          if (nodeIdx >= nodes.size()) return;
          const double dx = b.x - a.x;
          const double dy = b.y - a.y;
          const double len2 = dx * dx + dy * dy;
          if (len2 < 1e-6) return;
          const double len = std::sqrt(len2);
          const double cx = (rect.left + rect.right) * 0.5;
          const double cy = (rect.top + rect.bottom) * 0.5;
          const double t = std::clamp(
            ((cx - a.x) * dx + (cy - a.y) * dy) / len2,
            0.0,
            1.0);
          const double px = a.x + dx * t;
          const double py = a.y + dy * t;
          double ax = cx - px;
          double ay = cy - py;
          double dist = std::sqrt(ax * ax + ay * ay);
          if (dist < 1e-6) {
            ax = -dy / len;
            ay = dx / len;
            dist = 1.0;
          } else {
            ax /= dist;
            ay /= dist;
          }
          const double half =
            std::max(rect.right - rect.left, rect.bottom - rect.top) * 0.5;
          const double needed = std::max(18.0, half + kReliefMargin - dist);
          const double mag = std::min(maxShift, needed * strength);
          const double vx = ax * mag;
          const double vy = ay * mag;
          shiftX[nodeIdx] += vx;
          shiftY[nodeIdx] += vy;

          if (groupFactor <= 0.0 || groupMax <= 0) return;
          std::vector<std::size_t> followers;
          followers.reserve(static_cast<std::size_t>(groupMax));
          auto addFollower = [&](std::size_t idx) {
            if (idx >= nodes.size() || idx == nodeIdx || idx == srcIdx || idx == tgtIdx) {
              return;
            }
            if (std::find(followers.begin(), followers.end(), idx) != followers.end()) {
              return;
            }
            followers.push_back(idx);
          };
          for (std::size_t nb : adjacencyNER[nodeIdx]) {
            addFollower(nb);
            if (static_cast<int>(followers.size()) >= groupMax) break;
          }
          if (static_cast<int>(followers.size()) < groupMax) {
            auto cIt = clusterByModelIdFull.find(nodes[nodeIdx].modelId);
            if (cIt != clusterByModelIdFull.end()) {
              auto membersIt = clusterMembersNER.find(cIt->second);
              if (membersIt != clusterMembersNER.end()) {
                std::vector<std::pair<double, std::size_t>> ranked;
                ranked.reserve(membersIt->second.size());
                const double nx = attributes.x(nodes[nodeIdx].handle);
                const double ny = attributes.y(nodes[nodeIdx].handle);
                for (std::size_t member : membersIt->second) {
                  if (member == nodeIdx || member == srcIdx || member == tgtIdx) continue;
                  const double dxm = attributes.x(nodes[member].handle) - nx;
                  const double dym = attributes.y(nodes[member].handle) - ny;
                  ranked.emplace_back(dxm * dxm + dym * dym, member);
                }
                std::sort(ranked.begin(), ranked.end(),
                  [](const auto& l, const auto& r) { return l.first < r.first; });
                for (const auto& rankedMember : ranked) {
                  addFollower(rankedMember.second);
                  if (static_cast<int>(followers.size()) >= groupMax) break;
                }
              }
            }
          }
          const double fx = vx * groupFactor;
          const double fy = vy * groupFactor;
          for (std::size_t follower : followers) {
            shiftX[follower] += fx;
            shiftY[follower] += fy;
          }
        };

        std::size_t totalMoved = 0;
        double currentScore = obstructionScoreNER();
        for (int pass = 0; pass < passes; ++pass) {
          std::unordered_set<std::string> bundleAbsorbedNER;
          std::vector<std::unordered_set<std::size_t>> bundleExemptNER;
          std::vector<std::vector<std::size_t>> bundleLeafIdxNER;
          std::vector<Rect> bundleRectsNER;
          bundleExemptNER.reserve(metadata.leafBundles.size());
          bundleLeafIdxNER.reserve(metadata.leafBundles.size());
          bundleRectsNER.reserve(metadata.leafBundles.size());
          for (const LeafBundleRecord& bundle : metadata.leafBundles) {
            std::unordered_set<std::size_t> exempt;
            std::vector<std::size_t> leaves;
            auto pIt = id2idxNER.find(bundle.parentModelId);
            if (pIt != id2idxNER.end()) {
              exempt.insert(pIt->second);
              bundleAbsorbedNER.insert(bundle.parentModelId);
            }
            for (const std::string& leaf : bundle.leafModelIds) {
              auto lIt = id2idxNER.find(leaf);
              bundleAbsorbedNER.insert(leaf);
              if (lIt == id2idxNER.end()) continue;
              exempt.insert(lIt->second);
              leaves.push_back(lIt->second);
            }
            for (const std::string& root : bundle.sharedRootModelIds) {
              auto rIt = id2idxNER.find(root);
              if (rIt != id2idxNER.end()) exempt.insert(rIt->second);
            }
            bundleExemptNER.push_back(std::move(exempt));
            bundleLeafIdxNER.push_back(std::move(leaves));
            bundleRectsNER.push_back(renderedLeafBundleRect(bundle, kReliefBundleMargin));
          }

          std::vector<double> shiftX(nodes.size(), 0.0);
          std::vector<double> shiftY(nodes.size(), 0.0);
          for (std::size_t e = 0; e < routes.size() && e < edges.size(); ++e) {
            const auto sIt = id2idxNER.find(edges[e].sourceModelId);
            const auto tIt = id2idxNER.find(edges[e].targetModelId);
            if (sIt == id2idxNER.end() || tIt == id2idxNER.end()) continue;
            const std::size_t srcIdx = sIt->second;
            const std::size_t tgtIdx = tIt->second;
            const auto& route = routes[e];
            if (route.size() < 2) continue;
            for (std::size_t si = 1; si < route.size(); ++si) {
              const RoutePoint a = route[si - 1];
              const RoutePoint b = route[si];
              for (std::size_t ni = 0; ni < nodes.size(); ++ni) {
                if (ni == srcIdx || ni == tgtIdx) continue;
                if (bundleAbsorbedNER.count(nodes[ni].modelId)) continue;
                const Rect nr = nodeRect(nodes[ni], attributes, kReliefMargin);
                if (!segmentIntersectsRect(a, b, nr)) continue;
                addReliefShift(shiftX, shiftY, ni, srcIdx, tgtIdx, a, b, nr);
              }
              for (std::size_t bi = 0; bi < bundleRectsNER.size(); ++bi) {
                if (bundleExemptNER[bi].count(srcIdx)
                    || bundleExemptNER[bi].count(tgtIdx)) {
                  continue;
                }
                if (!segmentIntersectsRect(a, b, bundleRectsNER[bi])) continue;
                for (std::size_t leafIdx : bundleLeafIdxNER[bi]) {
                  addReliefShift(shiftX, shiftY, leafIdx, srcIdx, tgtIdx, a, b, bundleRectsNER[bi]);
                }
              }
            }
          }

          std::vector<std::pair<double, double>> snapPos(nodes.size());
          for (std::size_t i = 0; i < nodes.size(); ++i) {
            snapPos[i] = {attributes.x(nodes[i].handle), attributes.y(nodes[i].handle)};
          }
          const auto snapRoutes = routes;
          const auto snapBundles = metadata.leafBundles;

          const std::size_t moved = applyNodeShifts(nodes, attributes, shiftX, shiftY, maxShift);
          if (moved == 0) break;
          if (!skipReliefSeparation) {
            enforceNodeSeparationStrong(nodes, attributes);
          }
          syncRouteEndpointsNER();
          recomputeLeafBundlesNER();

          const double nextScore = obstructionScoreNER();
          if (nextScore + 1e-6 >= currentScore) {
            for (std::size_t i = 0; i < nodes.size(); ++i) {
              attributes.x(nodes[i].handle) = snapPos[i].first;
              attributes.y(nodes[i].handle) = snapPos[i].second;
            }
            routes = snapRoutes;
            metadata.leafBundles = snapBundles;
            std::fprintf(stderr,
              "[node-edge-relief-final] pass %d rejected (score %.1f -> %.1f).\n",
              pass + 1, currentScore, nextScore);
            break;
          }
          currentScore = nextScore;
          totalMoved += moved;
        }

        const char* endpointReliefEnv =
          std::getenv("DJERD_NODE_EDGE_RELIEF_FINAL_ENDPOINTS");
        const bool endpointRelief =
          endpointReliefEnv && std::strcmp(endpointReliefEnv, "0") != 0;
        if (endpointRelief) {
          const char* endpointTopEnv =
            std::getenv("DJERD_NODE_EDGE_RELIEF_FINAL_ENDPOINT_TOP");
          const int endpointTop =
            endpointTopEnv ? std::max(1, std::atoi(endpointTopEnv)) : 40;
          const char* endpointStepsEnv =
            std::getenv("DJERD_NODE_EDGE_RELIEF_FINAL_ENDPOINT_STEPS");
          std::vector<double> endpointSteps{80.0, 160.0, 300.0, 600.0};
          if (endpointStepsEnv && std::strlen(endpointStepsEnv) > 0) {
            endpointSteps.clear();
            std::stringstream ss(endpointStepsEnv);
            std::string part;
            while (std::getline(ss, part, ',')) {
              try {
                const double step = std::stod(part);
                if (step > 0.0) endpointSteps.push_back(step);
              } catch (const std::exception&) {
              }
            }
            if (endpointSteps.empty()) {
              endpointSteps = {80.0, 160.0, 300.0, 600.0};
            }
          }

          auto edgeBlockerCounts = [&]() {
            std::unordered_set<std::string> absorbed;
            std::vector<std::unordered_set<std::string>> bundleExemptIds;
            std::vector<Rect> bundleRects;
            bundleExemptIds.reserve(metadata.leafBundles.size());
            bundleRects.reserve(metadata.leafBundles.size());
            for (const LeafBundleRecord& bundle : metadata.leafBundles) {
              std::unordered_set<std::string> exempt;
              exempt.insert(bundle.parentModelId);
              absorbed.insert(bundle.parentModelId);
              for (const std::string& leaf : bundle.leafModelIds) {
                exempt.insert(leaf);
                absorbed.insert(leaf);
              }
              for (const std::string& root : bundle.sharedRootModelIds) {
                exempt.insert(root);
              }
              bundleExemptIds.push_back(std::move(exempt));
              bundleRects.push_back(renderedLeafBundleRect(bundle, kReliefBundleMargin));
            }
            std::vector<std::pair<int, std::size_t>> counts;
            counts.reserve(edges.size());
            for (std::size_t e = 0; e < routes.size() && e < edges.size(); ++e) {
              if (routes[e].size() < 2) continue;
              int count = 0;
              const std::string& srcId = edges[e].sourceModelId;
              const std::string& tgtId = edges[e].targetModelId;
              for (std::size_t si = 1; si < routes[e].size(); ++si) {
                const RoutePoint a = routes[e][si - 1];
                const RoutePoint b = routes[e][si];
                for (const NodeRecord& nd : nodes) {
                  if (nd.modelId == srcId || nd.modelId == tgtId) continue;
                  if (absorbed.count(nd.modelId)) continue;
                  if (segmentIntersectsRect(a, b, nodeRect(nd, attributes, kReliefMargin))) {
                    ++count;
                  }
                }
                for (std::size_t bi = 0; bi < bundleRects.size(); ++bi) {
                  if (bundleExemptIds[bi].count(srcId)
                      || bundleExemptIds[bi].count(tgtId)) {
                    continue;
                  }
                  if (segmentIntersectsRect(a, b, bundleRects[bi])) ++count;
                }
              }
              if (count > 0) counts.emplace_back(count, e);
            }
            std::sort(counts.begin(), counts.end(),
              [](const auto& l, const auto& r) {
                if (l.first != r.first) return l.first > r.first;
                return l.second < r.second;
              });
            return counts;
          };

          std::size_t endpointAccepted = 0;
          std::size_t endpointTried = 0;
          double endpointScore = currentScore;
          const auto hotEdges = edgeBlockerCounts();
          const std::size_t limit =
            std::min<std::size_t>(hotEdges.size(), static_cast<std::size_t>(endpointTop));
          for (std::size_t rank = 0; rank < limit; ++rank) {
            const std::size_t e = hotEdges[rank].second;
            if (e >= edges.size()) continue;
            auto sIt = id2idxNER.find(edges[e].sourceModelId);
            auto tIt = id2idxNER.find(edges[e].targetModelId);
            if (sIt == id2idxNER.end() || tIt == id2idxNER.end()) continue;
            const std::size_t srcIdx = sIt->second;
            const std::size_t tgtIdx = tIt->second;
            const double sx = attributes.x(nodes[srcIdx].handle);
            const double sy = attributes.y(nodes[srcIdx].handle);
            const double tx = attributes.x(nodes[tgtIdx].handle);
            const double ty = attributes.y(nodes[tgtIdx].handle);
            const double dx = tx - sx;
            const double dy = ty - sy;
            const double len = std::sqrt(dx * dx + dy * dy);
            if (len < 1e-6) continue;
            const double px = -dy / len;
            const double py = dx / len;
            bool acceptedEdge = false;
            for (double step : endpointSteps) {
              for (double sign : {-1.0, 1.0}) {
                ++endpointTried;
                const auto snapRoutes = routes;
                const auto snapBundles = metadata.leafBundles;
                const double osx = attributes.x(nodes[srcIdx].handle);
                const double osy = attributes.y(nodes[srcIdx].handle);
                const double otx = attributes.x(nodes[tgtIdx].handle);
                const double oty = attributes.y(nodes[tgtIdx].handle);
                attributes.x(nodes[srcIdx].handle) = osx + px * sign * step;
                attributes.y(nodes[srcIdx].handle) = osy + py * sign * step;
                attributes.x(nodes[tgtIdx].handle) = otx + px * sign * step;
                attributes.y(nodes[tgtIdx].handle) = oty + py * sign * step;
                enforceNodeSeparationStrong(nodes, attributes);
                syncRouteEndpointsNER();
                recomputeLeafBundlesNER();
                const double nextScore = obstructionScoreNER();
                if (nextScore + 1e-6 < endpointScore) {
                  endpointScore = nextScore;
                  ++endpointAccepted;
                  acceptedEdge = true;
                  break;
                }
                attributes.x(nodes[srcIdx].handle) = osx;
                attributes.y(nodes[srcIdx].handle) = osy;
                attributes.x(nodes[tgtIdx].handle) = otx;
                attributes.y(nodes[tgtIdx].handle) = oty;
                routes = snapRoutes;
                metadata.leafBundles = snapBundles;
              }
              if (acceptedEdge) break;
            }
          }
          currentScore = endpointScore;
          std::fprintf(stderr,
            "[node-edge-relief-final:endpoints] accepted %zu/%zu endpoint shifts, "
            "obstructionScore=%.1f.\n",
            endpointAccepted, endpointTried, currentScore);
        }

        std::fprintf(stderr,
          "[node-edge-relief-final] moved %zu node updates, obstructionScore=%.1f.\n",
          totalMoved, currentScore);
      }
    }

    // Optional final Y-axis bbox compaction. This is a conservative,
    // metric-gated shrink around the rendered graph center: it keeps the
    // topology and straight routes, recomputes bundle boxes, and accepts
    // only when rendered visual quality does not regress.
    {
      const bool bboxAxisScale =
        readBoolEnv("DJERD_BBOX_AXIS_SCALE_FINAL", false);
      if (bboxAxisScale && nodes.size() > 2) {
        std::vector<double> yScales{0.98, 0.95};
        const char* scalesEnv = std::getenv("DJERD_BBOX_Y_SCALE_FINAL_SCALES");
        if (scalesEnv && std::strlen(scalesEnv) > 0) {
          std::vector<double> parsed;
          std::stringstream ss(scalesEnv);
          std::string part;
          while (std::getline(ss, part, ',')) {
            try {
              const double value = std::stod(part);
              if (value > 0.2 && value < 1.0) {
                parsed.push_back(value);
              }
            } catch (const std::exception&) {
            }
          }
          if (!parsed.empty()) {
            yScales = std::move(parsed);
          }
        }

        auto rerouteAxisScale = [&]() {
          routes = crossAwareRouting
            ? routeAllEdgesCrossAware(nodes, edges, attributes)
            : (straightLineMode
              ? (arguments.edgeRouting == "straight_smart"
                  && !isStraightLineRoutingMode(arguments.mode)
                ? routeAllEdgesStraightSmart(nodes, edges, attributes)
                : routeAllEdgesStraight(edges, attributes))
              : routeAllEdges(nodes, edges, attributes, true));
        };
        auto measureAxisScaleQuality = [&]() {
          std::vector<std::vector<std::string>> ignoredIdsByEdge;
          std::size_t rawCrossings = 0;
          (void)detectRouteCrossings(edges, routes, ignoredIdsByEdge, rawCrossings);
          LayoutQualityMetrics qm =
            measureLayoutQuality(nodes, edges, routes, attributes, &metadata.leafBundles);
          qm.edgeCrossings = rawCrossings;
          if (!applyRenderedCarrierMetricsIfRequested(
              nodes,
              edges,
              routes,
              attributes,
              clusterByModelIdFull,
              metadata,
              qm,
              rawCrossings,
              true,
              false)) {
            qm.visualCrossings =
              qm.edgeCrossings
              + qm.edgeNodeIntersections
              + qm.nodeOverlaps
              + qm.bundleEdgeIntersections
              + qm.bundleNodeOverlaps;
          }
          return qm;
        };

        const LayoutQualityMetrics baseQuality = measureAxisScaleQuality();
        const Rect baseBounds = graphNodeBounds(nodes, attributes);
        const double centerY = rectCenterY(baseBounds);
        const double minGain =
          readDoubleEnv("DJERD_BBOX_AXIS_SCALE_FINAL_MIN_GAIN", 0.015, 0.0, 0.9);
        const std::size_t visualSlack = static_cast<std::size_t>(
          readDoubleEnv("DJERD_BBOX_AXIS_SCALE_FINAL_VISUAL_SLACK", 0.0, 0.0, 100000.0));
        const std::size_t bundleNodeSlack = static_cast<std::size_t>(
          readDoubleEnv("DJERD_BBOX_AXIS_SCALE_FINAL_BUNDLE_NODE_SLACK", 0.0, 0.0, 100000.0));
        const double maxAspect =
          readDoubleEnv("DJERD_BBOX_AXIS_SCALE_FINAL_MAX_ASPECT", 2.1, 1.0, 10.0);

        std::vector<std::pair<double, double>> originalPositions;
        originalPositions.reserve(nodes.size());
        for (const NodeRecord& node : nodes) {
          originalPositions.push_back({
            attributes.x(node.handle),
            attributes.y(node.handle),
          });
        }
        const auto originalRoutes = routes;
        const auto originalBundles = metadata.leafBundles;

        double bestScale = 1.0;
        LayoutQualityMetrics bestQuality = baseQuality;
        std::vector<std::vector<RoutePoint>> bestRoutes = routes;
        std::vector<LeafBundleRecord> bestBundles = metadata.leafBundles;
        bool haveBest = false;

        auto restoreAxisScale = [&]() {
          for (std::size_t i = 0; i < nodes.size(); ++i) {
            attributes.x(nodes[i].handle) = originalPositions[i].first;
            attributes.y(nodes[i].handle) = originalPositions[i].second;
          }
          routes = originalRoutes;
          metadata.leafBundles = originalBundles;
        };

        for (double scale : yScales) {
          restoreAxisScale();
          for (std::size_t i = 0; i < nodes.size(); ++i) {
            attributes.y(nodes[i].handle) =
              centerY + (originalPositions[i].second - centerY) * scale;
          }
          recomputeLeafBundleBboxesFromNodes(metadata.leafBundles, nodes, attributes);
          rerouteAxisScale();
          const LayoutQualityMetrics nextQuality = measureAxisScaleQuality();
          const bool areaOk =
            baseQuality.boundingBoxArea <= 0.0
            || nextQuality.boundingBoxArea
              < baseQuality.boundingBoxArea * (1.0 - minGain);
          const bool visualOk =
            nextQuality.visualCrossings <= baseQuality.visualCrossings + visualSlack;
          const bool overlapOk =
            nextQuality.nodeOverlaps <= baseQuality.nodeOverlaps
            && nextQuality.bundleNodeOverlaps
              <= baseQuality.bundleNodeOverlaps + bundleNodeSlack;
          const bool aspectOk = nextQuality.aspectRatio <= maxAspect;
          if (!areaOk || !visualOk || !overlapOk || !aspectOk) {
            continue;
          }
          if (!haveBest
              || nextQuality.boundingBoxArea < bestQuality.boundingBoxArea) {
            haveBest = true;
            bestScale = scale;
            bestQuality = nextQuality;
            bestRoutes = routes;
            bestBundles = metadata.leafBundles;
          }
        }

        if (haveBest) {
          restoreAxisScale();
          for (std::size_t i = 0; i < nodes.size(); ++i) {
            attributes.y(nodes[i].handle) =
              centerY + (originalPositions[i].second - centerY) * bestScale;
          }
          routes = std::move(bestRoutes);
          metadata.leafBundles = std::move(bestBundles);
          std::fprintf(stderr,
            "[bbox-axis-scale-final] y-scale=%.3f bbox %.2fB -> %.2fB "
            "visual=%zu -> %zu.\n",
            bestScale,
            baseQuality.boundingBoxArea / 1e9,
            bestQuality.boundingBoxArea / 1e9,
            baseQuality.visualCrossings,
            bestQuality.visualCrossings);
        } else {
          restoreAxisScale();
        }
      }
    }

    // Optional final rendered-density balance. The bbox passes can make the
    // overall footprint good while leaving local pockets visually too dense
    // and other cells empty. This pass expands only dense non-bundle clusters
    // by a small factor, then accepts the batch only if rendered spacing or
    // density imbalance improves under bbox/visual guards.
    {
      const bool densityBalance =
        readBoolEnv("DJERD_DENSITY_BALANCE_FINAL", false);
      if (densityBalance && nodes.size() > 2 && !clusterByModelIdFull.empty()) {
        auto rerouteDensityBalance = [&]() {
          routes = crossAwareRouting
            ? routeAllEdgesCrossAware(nodes, edges, attributes)
            : (straightLineMode
              ? (arguments.edgeRouting == "straight_smart"
                  && !isStraightLineRoutingMode(arguments.mode)
                ? routeAllEdgesStraightSmart(nodes, edges, attributes)
                : routeAllEdgesStraight(edges, attributes))
              : routeAllEdges(nodes, edges, attributes, true));
        };
        auto measureDensityBalanceQuality = [&]() {
          std::vector<std::vector<std::string>> ignoredIdsByEdge;
          std::size_t rawCrossings = 0;
          (void)detectRouteCrossings(edges, routes, ignoredIdsByEdge, rawCrossings);
          LayoutQualityMetrics qm =
            measureLayoutQuality(nodes, edges, routes, attributes, &metadata.leafBundles);
          qm.edgeCrossings = rawCrossings;
          if (!applyRenderedCarrierMetricsIfRequested(
              nodes,
              edges,
              routes,
              attributes,
              clusterByModelIdFull,
              metadata,
              qm,
              rawCrossings,
              true,
              false)) {
            qm.visualCrossings =
              qm.edgeCrossings
              + qm.edgeNodeIntersections
              + qm.nodeOverlaps
              + qm.bundleEdgeIntersections
              + qm.bundleNodeOverlaps;
          }
          return qm;
        };

        const double cellSize =
          readDoubleEnv("DJERD_DENSITY_BALANCE_CELL", 1600.0, 400.0, 8000.0);
        const LayoutQualityMetrics baseQuality = measureDensityBalanceQuality();
        const RenderedDensityMetrics baseDensity =
          measureRenderedDensity(nodes, attributes, metadata.leafBundles, cellSize);
        const std::unordered_set<std::string> absorbed =
          absorbedLeafBundleIds(metadata.leafBundles);

        std::unordered_map<std::string, std::vector<std::size_t>> clusterMembers;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          if (absorbed.count(nodes[i].modelId)) continue;
          auto cIt = clusterByModelIdFull.find(nodes[i].modelId);
          if (cIt == clusterByModelIdFull.end() || cIt->second.empty()) continue;
          clusterMembers[cIt->second].push_back(i);
        }

        struct DenseCluster {
          std::string id;
          std::vector<std::size_t> members;
          std::size_t spacingPairs = 0;
          double fill = 0.0;
          double area = 0.0;
        };
        std::vector<DenseCluster> denseClusters;
        const std::size_t minClusterSize = static_cast<std::size_t>(
          readDoubleEnv("DJERD_DENSITY_BALANCE_MIN_CLUSTER", 3.0, 2.0, 100.0));
        const double minFill =
          readDoubleEnv("DJERD_DENSITY_BALANCE_MIN_FILL", 0.66, 0.10, 4.0);

        for (const auto& kv : clusterMembers) {
          const std::vector<std::size_t>& members = kv.second;
          if (members.size() < minClusterSize) continue;
          std::vector<Rect> rects;
          rects.reserve(members.size());
          Rect clusterRect;
          bool initialized = false;
          double rectAreaSum = 0.0;
          for (std::size_t idx : members) {
            const Rect rect = expandedNodeRectAt(
              nodes[idx],
              attributes,
              sanitizeNodeCenterX(nodes[idx], attributes),
              sanitizeNodeCenterY(nodes[idx], attributes));
            rects.push_back(rect);
            rectAreaSum += std::max(1.0, rectWidth(rect) * rectHeight(rect));
            if (!initialized) {
              clusterRect = rect;
              initialized = true;
            } else {
              clusterRect.left = std::min(clusterRect.left, rect.left);
              clusterRect.right = std::max(clusterRect.right, rect.right);
              clusterRect.top = std::min(clusterRect.top, rect.top);
              clusterRect.bottom = std::max(clusterRect.bottom, rect.bottom);
            }
          }
          if (!initialized) continue;
          std::sort(rects.begin(), rects.end(),
            [](const Rect& left, const Rect& right) {
              return left.left < right.left;
            });
          std::size_t spacingPairs = 0;
          for (std::size_t i = 0; i < rects.size(); ++i) {
            for (std::size_t j = i + 1; j < rects.size(); ++j) {
              if (rects[j].left >= rects[i].right) break;
              if (rectsOverlap(rects[i], rects[j])) ++spacingPairs;
            }
          }
          const double clusterArea =
            std::max(1.0, rectWidth(clusterRect) * rectHeight(clusterRect));
          const double fill = rectAreaSum / clusterArea;
          if (spacingPairs == 0 && fill < minFill) continue;
          denseClusters.push_back({kv.first, members, spacingPairs, fill, clusterArea});
        }

        std::sort(denseClusters.begin(), denseClusters.end(),
          [](const DenseCluster& left, const DenseCluster& right) {
            if (left.spacingPairs != right.spacingPairs) {
              return left.spacingPairs > right.spacingPairs;
            }
            if (std::abs(left.fill - right.fill) > 1e-6) {
              return left.fill > right.fill;
            }
            if (left.members.size() != right.members.size()) {
              return left.members.size() > right.members.size();
            }
            return left.id < right.id;
          });

        const std::size_t topClusters = static_cast<std::size_t>(
          readDoubleEnv("DJERD_DENSITY_BALANCE_TOP", 18.0, 1.0, 128.0));
        const double baseScale =
          readDoubleEnv("DJERD_DENSITY_BALANCE_SCALE", 1.055, 1.001, 1.50);
        const double maxScale =
          readDoubleEnv("DJERD_DENSITY_BALANCE_MAX_SCALE", 1.11, 1.001, 2.0);

        std::vector<std::pair<double, double>> originalPositions;
        originalPositions.reserve(nodes.size());
        for (const NodeRecord& node : nodes) {
          originalPositions.push_back({
            attributes.x(node.handle),
            attributes.y(node.handle),
          });
        }
        const auto originalRoutes = routes;
        const auto originalBundles = metadata.leafBundles;

        std::set<std::size_t> movedIndexes;
        const std::size_t limit = std::min(topClusters, denseClusters.size());
        constexpr double kTwoPi = 6.28318530717958647692;
        for (std::size_t rank = 0; rank < limit; ++rank) {
          const DenseCluster& cluster = denseClusters[rank];
          double cx = 0.0;
          double cy = 0.0;
          for (std::size_t idx : cluster.members) {
            cx += sanitizeNodeCenterX(nodes[idx], attributes);
            cy += sanitizeNodeCenterY(nodes[idx], attributes);
          }
          cx /= static_cast<double>(cluster.members.size());
          cy /= static_cast<double>(cluster.members.size());
          const double fillSeverity =
            std::max(0.0, cluster.fill - minFill) / std::max(0.1, minFill);
          const double spacingSeverity =
            std::min(1.0, static_cast<double>(cluster.spacingPairs) / 8.0);
          const double scale = std::min(
            maxScale,
            baseScale + 0.020 * fillSeverity + 0.025 * spacingSeverity);
          for (std::size_t local = 0; local < cluster.members.size(); ++local) {
            const std::size_t idx = cluster.members[local];
            const NodeRecord& node = nodes[idx];
            double dx = sanitizeNodeCenterX(node, attributes) - cx;
            double dy = sanitizeNodeCenterY(node, attributes) - cy;
            if (std::hypot(dx, dy) < 1.0) {
              const double angle =
                kTwoPi * static_cast<double>(local)
                / static_cast<double>(std::max<std::size_t>(1, cluster.members.size()));
              dx = std::cos(angle) * 4.0;
              dy = std::sin(angle) * 4.0;
            }
            attributes.x(node.handle) = cx + dx * scale;
            attributes.y(node.handle) = cy + dy * scale;
            movedIndexes.insert(idx);
          }
        }

        if (!movedIndexes.empty()) {
          recomputeLeafBundleBboxesFromNodes(metadata.leafBundles, nodes, attributes);
          rerouteDensityBalance();
          const LayoutQualityMetrics nextQuality = measureDensityBalanceQuality();
          const RenderedDensityMetrics nextDensity =
            measureRenderedDensity(nodes, attributes, metadata.leafBundles, cellSize);

          const double spacingWeight =
            readDoubleEnv("DJERD_DENSITY_BALANCE_SPACING_WEIGHT", 30.0, 0.0, 10000.0);
          const double baseScore =
            baseDensity.score
            + spacingWeight * static_cast<double>(baseQuality.nodeSpacingOverlaps);
          const double nextScore =
            nextDensity.score
            + spacingWeight * static_cast<double>(nextQuality.nodeSpacingOverlaps);
          const double minGain =
            readDoubleEnv("DJERD_DENSITY_BALANCE_MIN_GAIN", 2.0, 0.0, 10000.0);
          const std::size_t visualSlack = static_cast<std::size_t>(
            readDoubleEnv("DJERD_DENSITY_BALANCE_VISUAL_SLACK", 40.0, 0.0, 100000.0));
          const std::size_t edgeNodeSlack = static_cast<std::size_t>(
            readDoubleEnv("DJERD_DENSITY_BALANCE_EDGE_NODE_SLACK", 80.0, 0.0, 100000.0));
          const std::size_t spacingSlack = static_cast<std::size_t>(
            readDoubleEnv("DJERD_DENSITY_BALANCE_SPACING_SLACK", 0.0, 0.0, 100000.0));
          const double bboxLimit =
            readDoubleEnv("DJERD_DENSITY_BALANCE_BBOX_LIMIT", 1.025, 1.0, 4.0);

          const bool improved =
            nextQuality.nodeSpacingOverlaps < baseQuality.nodeSpacingOverlaps
            || nextScore + minGain < baseScore;
          const bool visualOk =
            nextQuality.visualCrossings <= baseQuality.visualCrossings + visualSlack;
          const bool edgeNodeOk =
            nextQuality.edgeNodeIntersections
              <= baseQuality.edgeNodeIntersections + edgeNodeSlack;
          const bool nodeOverlapOk =
            nextQuality.nodeOverlaps <= baseQuality.nodeOverlaps;
          const bool bundleNodeOk =
            nextQuality.bundleNodeOverlaps <= baseQuality.bundleNodeOverlaps;
          const bool spacingOk =
            nextQuality.nodeSpacingOverlaps
              <= baseQuality.nodeSpacingOverlaps + spacingSlack;
          const bool bboxOk =
            baseQuality.boundingBoxArea <= 0.0
            || nextQuality.boundingBoxArea <= baseQuality.boundingBoxArea * bboxLimit;

          if (improved && visualOk && edgeNodeOk && nodeOverlapOk
              && bundleNodeOk && spacingOk && bboxOk) {
            std::fprintf(stderr,
              "[density-balance-final] accepted %zu/%zu dense clusters, "
              "%zu nodes; spacing=%zu -> %zu visual=%zu -> %zu "
              "edgeNode=%zu -> %zu bbox=%.2fB -> %.2fB "
              "densityScore=%.1f -> %.1f p90=%.1f -> %.1f "
              "max=%.1f -> %.1f empty=%.1f%% -> %.1f%%.\n",
              limit,
              denseClusters.size(),
              movedIndexes.size(),
              baseQuality.nodeSpacingOverlaps,
              nextQuality.nodeSpacingOverlaps,
              baseQuality.visualCrossings,
              nextQuality.visualCrossings,
              baseQuality.edgeNodeIntersections,
              nextQuality.edgeNodeIntersections,
              baseQuality.boundingBoxArea / 1e9,
              nextQuality.boundingBoxArea / 1e9,
              baseScore,
              nextScore,
              baseDensity.p90,
              nextDensity.p90,
              baseDensity.maxCell,
              nextDensity.maxCell,
              baseDensity.emptyRatio * 100.0,
              nextDensity.emptyRatio * 100.0);
          } else {
            for (std::size_t i = 0; i < nodes.size(); ++i) {
              attributes.x(nodes[i].handle) = originalPositions[i].first;
              attributes.y(nodes[i].handle) = originalPositions[i].second;
            }
            routes = originalRoutes;
            metadata.leafBundles = originalBundles;
            std::fprintf(stderr,
              "[density-balance-final] rejected %zu/%zu dense clusters, "
              "%zu nodes; spacing=%zu -> %zu visual=%zu -> %zu "
              "edgeNode=%zu -> %zu bbox=%.2fB -> %.2fB "
              "densityScore=%.1f -> %.1f p90=%.1f -> %.1f "
              "max=%.1f -> %.1f.\n",
              limit,
              denseClusters.size(),
              movedIndexes.size(),
              baseQuality.nodeSpacingOverlaps,
              nextQuality.nodeSpacingOverlaps,
              baseQuality.visualCrossings,
              nextQuality.visualCrossings,
              baseQuality.edgeNodeIntersections,
              nextQuality.edgeNodeIntersections,
              baseQuality.boundingBoxArea / 1e9,
              nextQuality.boundingBoxArea / 1e9,
              baseScore,
              nextScore,
              baseDensity.p90,
              nextDensity.p90,
              baseDensity.maxCell,
              nextDensity.maxCell);
          }
        } else if (readBoolEnv("DJERD_DENSITY_BALANCE_LOG", false)) {
          std::fprintf(stderr,
            "[density-balance-final] no dense clusters; spacing=%zu "
            "densityScore=%.1f p50=%.1f p90=%.1f max=%.1f empty=%.1f%%.\n",
            baseQuality.nodeSpacingOverlaps,
            baseDensity.score,
            baseDensity.p50,
            baseDensity.p90,
            baseDensity.maxCell,
            baseDensity.emptyRatio * 100.0);
        }
      }
    }

    // Final node-node visual clearance. Bbox-target compression can leave a
    // handful of margin-expanded node boxes touching even when the rendered
    // graph is otherwise good. Resolve those local contacts before bundle
    // clearance; keep the move only when it reduces node overlaps without
    // materially expanding the layout.
    {
      const bool nodeOverlapClear =
        readBoolEnv("DJERD_NODE_OVERLAP_CLEAR_FINAL", false);
      if (nodeOverlapClear && nodes.size() > 1) {
        auto rerouteNodeOverlapClear = [&]() {
          routes = crossAwareRouting
            ? routeAllEdgesCrossAware(nodes, edges, attributes)
            : (straightLineMode
              ? (arguments.edgeRouting == "straight_smart"
                  && !isStraightLineRoutingMode(arguments.mode)
                ? routeAllEdgesStraightSmart(nodes, edges, attributes)
                : routeAllEdgesStraight(edges, attributes))
              : routeAllEdges(nodes, edges, attributes, true));
        };

        auto measureNodeOverlapClearQuality = [&]() {
          std::vector<std::vector<std::string>> ignoredIdsByEdge;
          std::size_t rawCrossings = 0;
          (void)detectRouteCrossings(edges, routes, ignoredIdsByEdge, rawCrossings);
          LayoutQualityMetrics qm =
            measureLayoutQuality(nodes, edges, routes, attributes, &metadata.leafBundles);
          qm.edgeCrossings = rawCrossings;
          if (!applyRenderedCarrierMetricsIfRequested(
              nodes,
              edges,
              routes,
              attributes,
              clusterByModelIdFull,
              metadata,
              qm,
              rawCrossings,
              true,
              false)) {
            qm.visualCrossings =
              qm.edgeCrossings
              + qm.edgeNodeIntersections
              + qm.nodeOverlaps
              + qm.bundleEdgeIntersections
              + qm.bundleNodeOverlaps;
          }
          return qm;
        };

        const LayoutQualityMetrics baseQuality = measureNodeOverlapClearQuality();
        if (baseQuality.nodeOverlaps > 0) {
          std::vector<std::pair<double, double>> originalPositions;
          originalPositions.reserve(nodes.size());
          for (const NodeRecord& node : nodes) {
            originalPositions.push_back({
              attributes.x(node.handle),
              attributes.y(node.handle),
            });
          }
          const auto originalRoutes = routes;
          const auto originalBundles = metadata.leafBundles;

          const std::size_t moved =
            clearNodeVisualOverlaps(metadata.leafBundles, nodes, attributes);
          if (moved > 0) {
            recomputeLeafBundleBboxesFromNodes(metadata.leafBundles, nodes, attributes);
            rerouteNodeOverlapClear();
            const LayoutQualityMetrics nextQuality = measureNodeOverlapClearQuality();

            const double bboxLimit =
              readDoubleEnv(
                "DJERD_NODE_OVERLAP_CLEAR_FINAL_BBOX_LIMIT",
                1.03,
                1.0,
                4.0);
            const std::size_t visualSlack = static_cast<std::size_t>(
              readDoubleEnv(
                "DJERD_NODE_OVERLAP_CLEAR_FINAL_VISUAL_SLACK",
                80.0,
                0.0,
                100000.0));
            const std::size_t edgeNodeSlack = static_cast<std::size_t>(
              readDoubleEnv(
                "DJERD_NODE_OVERLAP_CLEAR_FINAL_EDGE_NODE_SLACK",
                80.0,
                0.0,
                100000.0));
            const std::size_t bundleNodeSlack = static_cast<std::size_t>(
              readDoubleEnv(
                "DJERD_NODE_OVERLAP_CLEAR_FINAL_BUNDLE_NODE_SLACK",
                0.0,
                0.0,
                100000.0));

            const bool overlapImproved =
              nextQuality.nodeOverlaps < baseQuality.nodeOverlaps;
            const bool bboxOk =
              baseQuality.boundingBoxArea <= 0.0
              || nextQuality.boundingBoxArea <= baseQuality.boundingBoxArea * bboxLimit;
            const bool visualOk =
              nextQuality.visualCrossings <= baseQuality.visualCrossings + visualSlack;
            const bool edgeNodeOk =
              nextQuality.edgeNodeIntersections
                <= baseQuality.edgeNodeIntersections + edgeNodeSlack;
            const bool bundleNodeOk =
              nextQuality.bundleNodeOverlaps
                <= baseQuality.bundleNodeOverlaps + bundleNodeSlack;

            if (overlapImproved && bboxOk && visualOk && edgeNodeOk && bundleNodeOk) {
              std::fprintf(stderr,
                "[node-overlap-clear-final] accepted %zu node moves, "
                "nodeOverlaps=%zu -> %zu visual=%zu -> %zu edgeNode=%zu -> %zu "
                "bbox=%.2fB -> %.2fB.\n",
                moved,
                baseQuality.nodeOverlaps,
                nextQuality.nodeOverlaps,
                baseQuality.visualCrossings,
                nextQuality.visualCrossings,
                baseQuality.edgeNodeIntersections,
                nextQuality.edgeNodeIntersections,
                baseQuality.boundingBoxArea / 1e9,
                nextQuality.boundingBoxArea / 1e9);
            } else {
              for (std::size_t i = 0; i < nodes.size(); ++i) {
                attributes.x(nodes[i].handle) = originalPositions[i].first;
                attributes.y(nodes[i].handle) = originalPositions[i].second;
              }
              routes = originalRoutes;
              metadata.leafBundles = originalBundles;
              std::fprintf(stderr,
                "[node-overlap-clear-final] rejected %zu node moves, "
                "nodeOverlaps=%zu -> %zu visual=%zu -> %zu edgeNode=%zu -> %zu "
                "bbox=%.2fB -> %.2fB.\n",
                moved,
                baseQuality.nodeOverlaps,
                nextQuality.nodeOverlaps,
                baseQuality.visualCrossings,
                nextQuality.visualCrossings,
                baseQuality.edgeNodeIntersections,
                nextQuality.edgeNodeIntersections,
                baseQuality.boundingBoxArea / 1e9,
                nextQuality.boundingBoxArea / 1e9);
            }
          }
        }
      }
    }

    // Final rendered leaf-bundle/node clearance. Leaf bundles render as
    // synthetic big nodes in the webview, so late compaction/relief can leave
    // an external node inside the rendered bundle box even when the raw leaf
    // nodes themselves do not overlap. Move each bundle's leaves as a rigid
    // block, reroute, and keep the result only if the rendered metrics improve
    // without introducing worse hard node/edge clashes.
    {
      const bool leafBundleNodeClear =
        readBoolEnv("DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL", false);
      if (leafBundleNodeClear && !metadata.leafBundles.empty()) {
        auto rerouteLeafBundleClear = [&]() {
          routes = crossAwareRouting
            ? routeAllEdgesCrossAware(nodes, edges, attributes)
            : (straightLineMode
              ? (arguments.edgeRouting == "straight_smart"
                  && !isStraightLineRoutingMode(arguments.mode)
                ? routeAllEdgesStraightSmart(nodes, edges, attributes)
                : routeAllEdgesStraight(edges, attributes))
              : routeAllEdges(nodes, edges, attributes, true));
        };

        auto measureLeafBundleClearQuality = [&]() {
          std::vector<std::vector<std::string>> ignoredIdsByEdge;
          std::size_t rawCrossings = 0;
          (void)detectRouteCrossings(edges, routes, ignoredIdsByEdge, rawCrossings);
          LayoutQualityMetrics qm =
            measureLayoutQuality(nodes, edges, routes, attributes, &metadata.leafBundles);
          qm.edgeCrossings = rawCrossings;
          if (!applyRenderedCarrierMetricsIfRequested(
              nodes,
              edges,
              routes,
              attributes,
              clusterByModelIdFull,
              metadata,
              qm,
              rawCrossings,
              true,
              false)) {
            qm.visualCrossings =
              qm.edgeCrossings
              + qm.edgeNodeIntersections
              + qm.nodeOverlaps
              + qm.bundleEdgeIntersections
              + qm.bundleNodeOverlaps;
          }
          return qm;
        };

        const LayoutQualityMetrics baseQuality = measureLeafBundleClearQuality();
        std::vector<std::pair<double, double>> originalPositions;
        originalPositions.reserve(nodes.size());
        for (const NodeRecord& node : nodes) {
          originalPositions.push_back({
            attributes.x(node.handle),
            attributes.y(node.handle),
          });
        }
        const auto originalRoutes = routes;
        const auto originalBundles = metadata.leafBundles;

        const std::size_t movedBundles =
          clearLeafBundleNodeMargins(metadata.leafBundles, nodes, attributes, false);
        const std::size_t movedNodes =
          clearLeafBundleExternalNodeMargins(metadata.leafBundles, nodes, attributes);
        std::size_t movedOverlapRepairs =
          clearNodeVisualOverlaps(metadata.leafBundles, nodes, attributes);
        const std::size_t moved = movedBundles + movedNodes + movedOverlapRepairs;
        if (moved > 0) {
          rerouteLeafBundleClear();
          recomputeLeafBundleBboxesFromNodes(metadata.leafBundles, nodes, attributes);
          const std::size_t postRouteOverlapRepairs =
            clearNodeVisualOverlaps(metadata.leafBundles, nodes, attributes);
          if (postRouteOverlapRepairs > 0) {
            movedOverlapRepairs += postRouteOverlapRepairs;
            rerouteLeafBundleClear();
            recomputeLeafBundleBboxesFromNodes(metadata.leafBundles, nodes, attributes);
          }
          const LayoutQualityMetrics nextQuality = measureLeafBundleClearQuality();

          const std::size_t visualSlack = static_cast<std::size_t>(
            readDoubleEnv(
              "DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_VISUAL_SLACK",
              0.0,
              0.0,
              100000.0));
          const std::size_t nodeOverlapSlack = static_cast<std::size_t>(
            readDoubleEnv(
              "DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_NODE_OVERLAP_SLACK",
              0.0,
              0.0,
              100000.0));
          const std::size_t edgeNodeSlack = static_cast<std::size_t>(
            readDoubleEnv(
              "DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EDGE_NODE_SLACK",
              0.0,
              0.0,
              100000.0));
          const double bboxLimit =
            readDoubleEnv(
              "DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_BBOX_LIMIT",
              1.04,
              1.0,
              4.0);

          const bool bundleNodeImproved =
            nextQuality.bundleNodeOverlaps < baseQuality.bundleNodeOverlaps;
          const bool visualOk =
            nextQuality.visualCrossings <= baseQuality.visualCrossings + visualSlack;
          const bool nodeOverlapOk =
            nextQuality.nodeOverlaps <= baseQuality.nodeOverlaps + nodeOverlapSlack;
          const bool edgeNodeOk =
            nextQuality.edgeNodeIntersections
              <= baseQuality.edgeNodeIntersections + edgeNodeSlack;
          const bool edgeNodeTradeoffOk =
            edgeNodeOk || nextQuality.visualCrossings < baseQuality.visualCrossings;
          const bool bboxOk =
            baseQuality.boundingBoxArea <= 0.0
            || nextQuality.boundingBoxArea <= baseQuality.boundingBoxArea * bboxLimit;

          if (bundleNodeImproved && visualOk && nodeOverlapOk && edgeNodeTradeoffOk && bboxOk) {
            std::fprintf(stderr,
              "[leaf-bundle-node-clear-final] accepted %zu bundle moves "
              "+ %zu node pushes + %zu overlap repairs, "
              "bundleNode=%zu -> %zu visual=%zu -> %zu edgeNode=%zu -> %zu "
              "bbox=%.2fB -> %.2fB.\n",
              movedBundles,
              movedNodes,
              movedOverlapRepairs,
              baseQuality.bundleNodeOverlaps,
              nextQuality.bundleNodeOverlaps,
              baseQuality.visualCrossings,
              nextQuality.visualCrossings,
              baseQuality.edgeNodeIntersections,
              nextQuality.edgeNodeIntersections,
              baseQuality.boundingBoxArea / 1e9,
              nextQuality.boundingBoxArea / 1e9);
          } else {
            for (std::size_t i = 0; i < nodes.size(); ++i) {
              attributes.x(nodes[i].handle) = originalPositions[i].first;
              attributes.y(nodes[i].handle) = originalPositions[i].second;
            }
            routes = originalRoutes;
            metadata.leafBundles = originalBundles;
            std::fprintf(stderr,
              "[leaf-bundle-node-clear-final] rejected %zu bundle moves "
              "+ %zu node pushes + %zu overlap repairs, "
              "bundleNode=%zu -> %zu nodeOverlaps=%zu -> %zu "
              "visual=%zu -> %zu edgeNode=%zu -> %zu "
              "bbox=%.2fB -> %.2fB "
              "(visualOk=%d nodeOverlapOk=%d edgeNodeOk=%d bboxOk=%d).\n",
              movedBundles,
              movedNodes,
              movedOverlapRepairs,
              baseQuality.bundleNodeOverlaps,
              nextQuality.bundleNodeOverlaps,
              baseQuality.nodeOverlaps,
              nextQuality.nodeOverlaps,
              baseQuality.visualCrossings,
              nextQuality.visualCrossings,
              baseQuality.edgeNodeIntersections,
              nextQuality.edgeNodeIntersections,
              baseQuality.boundingBoxArea / 1e9,
              nextQuality.boundingBoxArea / 1e9,
              visualOk ? 1 : 0,
              nodeOverlapOk ? 1 : 0,
              edgeNodeOk ? 1 : 0,
              bboxOk ? 1 : 0);
          }
        }
      }
    }

    // Optional final leaf-bundle relocation. A rendered bundle is a degree-1
    // visual object: its leaves can move as one rigid block while the parent
    // stays fixed. This searches free positions for the bundle box instead of
    // leaving it on top of unrelated edges or nodes.
    {
      const char* bundleRelocateEnv = std::getenv("DJERD_BUNDLE_BOX_RELOCATE_FINAL");
      const bool bundleRelocate =
        bundleRelocateEnv && std::strcmp(bundleRelocateEnv, "0") != 0;
      if (bundleRelocate && !metadata.leafBundles.empty()) {
        const char* passesEnv = std::getenv("DJERD_BUNDLE_BOX_RELOCATE_FINAL_PASSES");
        const int passes = passesEnv ? std::max(1, std::atoi(passesEnv)) : 2;
        const char* topEnv = std::getenv("DJERD_BUNDLE_BOX_RELOCATE_FINAL_TOP");
        const int topBundles = topEnv ? std::max(1, std::atoi(topEnv)) : 5;
        const char* candidatesEnv =
          std::getenv("DJERD_BUNDLE_BOX_RELOCATE_FINAL_MAX_CANDIDATES");
        const int maxCandidates =
          candidatesEnv ? std::max(8, std::atoi(candidatesEnv)) : 48;
        const char* shortlistEnv =
          std::getenv("DJERD_BUNDLE_BOX_RELOCATE_FINAL_SHORTLIST");
        const int shortlistSize =
          shortlistEnv ? std::max(1, std::atoi(shortlistEnv)) : 4;
        const char* maxMoveEnv = std::getenv("DJERD_BUNDLE_BOX_RELOCATE_FINAL_MAX_MOVE");
        const double maxMove = maxMoveEnv ? std::max(200.0, std::atof(maxMoveEnv)) : 5200.0;
        const char* bboxLimitEnv =
          std::getenv("DJERD_BUNDLE_BOX_RELOCATE_FINAL_BBOX_LIMIT");
        const double bboxLimit =
          bboxLimitEnv ? std::max(1.0, std::atof(bboxLimitEnv)) : 1.08;
        const char* minGainEnv = std::getenv("DJERD_BUNDLE_BOX_RELOCATE_FINAL_MIN_GAIN");
        const double minGain = minGainEnv ? std::max(0.0, std::atof(minGainEnv)) : 0.25;
        const char* fullScanEnv = std::getenv("DJERD_BUNDLE_BOX_RELOCATE_FINAL_FULL_SCAN");
        const bool fullScan =
          !(fullScanEnv && std::strcmp(fullScanEnv, "0") == 0);
        const char* quickSlackEnv =
          std::getenv("DJERD_BUNDLE_BOX_RELOCATE_FINAL_QUICK_SLACK");
        const double quickSlack =
          quickSlackEnv ? std::max(0.0, std::atof(quickSlackEnv)) : 50.0;
        const std::size_t edgeNodeTarget = static_cast<std::size_t>(readDoubleEnv(
          "DJERD_RENDERED_CARRIER_EDGE_NODE_TARGET", 0.0, 0.0, 1'000'000.0));
        const std::size_t bundleEdgeTarget = static_cast<std::size_t>(readDoubleEnv(
          "DJERD_RENDERED_CARRIER_BUNDLE_EDGE_TARGET", 0.0, 0.0, 1'000'000.0));
        const std::size_t visualTarget = static_cast<std::size_t>(readDoubleEnv(
          "DJERD_RENDERED_CARRIER_VISUAL_TARGET", 100.0, 0.0, 1'000'000.0));
        const std::size_t totalCandidateLimit = static_cast<std::size_t>(
          readDoubleEnv(
            "DJERD_BUNDLE_BOX_RELOCATE_FINAL_TOTAL_LIMIT",
            2560.0,
            64.0,
            100000.0));
        const double kBundleRelocateMargin = leafBundleVisualMargin();
        const double kBundleRelocateNodeMargin = visualNodeMargin();

        std::vector<double> radii{180.0, 360.0, 700.0, 1200.0, 1900.0, 2900.0, 4300.0};
        const char* radiiEnv = std::getenv("DJERD_BUNDLE_BOX_RELOCATE_FINAL_RADII");
        if (radiiEnv && std::strlen(radiiEnv) > 0) {
          std::vector<double> parsed;
          std::stringstream ss(radiiEnv);
          std::string part;
          while (std::getline(ss, part, ',')) {
            try {
              const double radius = std::stod(part);
              if (radius > 0.0) parsed.push_back(radius);
            } catch (const std::exception&) {
            }
          }
          if (!parsed.empty()) radii = std::move(parsed);
        }

        std::unordered_map<std::string, std::size_t> id2idxReloc;
        id2idxReloc.reserve(nodes.size());
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          id2idxReloc[nodes[i].modelId] = i;
        }
        std::unordered_set<std::string> bundleAbsorbedReloc;
        for (const LeafBundleRecord& bundle : metadata.leafBundles) {
          bundleAbsorbedReloc.insert(bundle.parentModelId);
          for (const std::string& leaf : bundle.leafModelIds) {
            bundleAbsorbedReloc.insert(leaf);
          }
        }

        auto recomputeLeafBundlesReloc = [&]() {
          for (auto& bundle : metadata.leafBundles) {
            double minX = std::numeric_limits<double>::infinity();
            double minY = std::numeric_limits<double>::infinity();
            double maxX = -std::numeric_limits<double>::infinity();
            double maxY = -std::numeric_limits<double>::infinity();
            double sumLX = 0.0, sumLY = 0.0;
            std::size_t cnt = 0;
            for (const std::string& leaf : bundle.leafModelIds) {
              auto it = id2idxReloc.find(leaf);
              if (it == id2idxReloc.end()) continue;
              const NodeRecord& nd = nodes[it->second];
              const double cx = attributes.x(nd.handle);
              const double cy = attributes.y(nd.handle);
              const double w = attributes.width(nd.handle);
              const double h = attributes.height(nd.handle);
              minX = std::min(minX, cx - w / 2.0);
              minY = std::min(minY, cy - h / 2.0);
              maxX = std::max(maxX, cx + w / 2.0);
              maxY = std::max(maxY, cy + h / 2.0);
              sumLX += cx;
              sumLY += cy;
              ++cnt;
            }
            if (cnt == 0 || !std::isfinite(minX)) continue;
            bundle.bboxX = minX;
            bundle.bboxY = minY;
            bundle.bboxWidth = maxX - minX;
            bundle.bboxHeight = maxY - minY;
            const double leafCx = sumLX / static_cast<double>(cnt);
            const double leafCy = sumLY / static_cast<double>(cnt);
            auto pit = id2idxReloc.find(bundle.parentModelId);
            if (pit != id2idxReloc.end()) {
              const double pX = attributes.x(nodes[pit->second].handle);
              const double pY = attributes.y(nodes[pit->second].handle);
              bundle.anchorX = 0.5 * (pX + leafCx);
              bundle.anchorY = 0.5 * (pY + leafCy);
            }
          }
        };

        auto rerouteReloc = [&]() {
          routes = crossAwareRouting
            ? routeAllEdgesCrossAware(nodes, edges, attributes)
            : (straightLineMode
              ? (arguments.edgeRouting == "straight_smart"
                  && !isStraightLineRoutingMode(arguments.mode)
                ? routeAllEdgesStraightSmart(nodes, edges, attributes)
                : routeAllEdgesStraight(edges, attributes))
              : routeAllEdges(nodes, edges, attributes, true));
        };

        const char* skipCarrierRelocEnv = std::getenv("DJERD_NO_CARRIER_CROSS");
        const bool skipCarrierReloc =
          skipCarrierRelocEnv && std::strcmp(skipCarrierRelocEnv, "0") != 0;
        const char* occMarginRelocEnv =
          std::getenv("DJERD_CARRIER_CROSS_OCCLUSION_MARGIN");
        const double occMarginReloc = occMarginRelocEnv
          ? std::max(0.0, std::atof(occMarginRelocEnv))
          : 0.0;

        auto carrierGroupedCrossReloc = [&]() {
          std::unordered_map<std::string, std::size_t> leafToBundleIdx;
          for (std::size_t bi = 0; bi < metadata.leafBundles.size(); ++bi) {
            for (const std::string& leaf : metadata.leafBundles[bi].leafModelIds) {
              leafToBundleIdx[leaf] = bi;
            }
          }

          std::unordered_map<std::string, std::pair<double, double>> sumByCluster;
          std::unordered_map<std::string, std::size_t> cntByCluster;
          for (const auto& kv : clusterByModelIdFull) {
            auto idIt = id2idxReloc.find(kv.first);
            if (idIt == id2idxReloc.end()) continue;
            const NodeRecord& nd = nodes[idIt->second];
            sumByCluster[kv.second].first += attributes.x(nd.handle);
            sumByCluster[kv.second].second += attributes.y(nd.handle);
            cntByCluster[kv.second] += 1;
          }
          std::unordered_map<std::string, std::pair<double, double>> clusterCentroids;
          for (const auto& kv : sumByCluster) {
            const std::size_t c = cntByCluster[kv.first];
            if (c == 0) continue;
            clusterCentroids[kv.first] = {
              kv.second.first / static_cast<double>(c),
              kv.second.second / static_cast<double>(c),
            };
          }
          auto nearestClusterReloc = [&](const std::string& mid) {
            auto idIt = id2idxReloc.find(mid);
            if (idIt == id2idxReloc.end()) return std::string{};
            const double mx = attributes.x(nodes[idIt->second].handle);
            const double my = attributes.y(nodes[idIt->second].handle);
            std::string best;
            double bestD2 = std::numeric_limits<double>::infinity();
            for (const auto& kv : clusterCentroids) {
              const double dx = mx - kv.second.first;
              const double dy = my - kv.second.second;
              const double d2 = dx * dx + dy * dy;
              if (d2 < bestD2) {
                bestD2 = d2;
                best = kv.first;
              }
            }
            return best;
          };
          auto isBundleRootReloc = [](const LeafBundleRecord& bundle,
                                      const std::string& modelId) {
            if (bundle.sharedRootModelIds.empty()) {
              return modelId == bundle.parentModelId;
            }
            return std::find(
              bundle.sharedRootModelIds.begin(),
              bundle.sharedRootModelIds.end(),
              modelId) != bundle.sharedRootModelIds.end();
          };

          std::vector<std::string> carrierIdByEdge(edges.size());
          for (std::size_t e = 0; e < edges.size(); ++e) {
            const std::string& s = edges[e].sourceModelId;
            const std::string& t = edges[e].targetModelId;
            auto sBI = leafToBundleIdx.find(s);
            auto tBI = leafToBundleIdx.find(t);
            if (sBI != leafToBundleIdx.end()) {
              const LeafBundleRecord& bundle = metadata.leafBundles[sBI->second];
              if (isBundleRootReloc(bundle, t)) {
                carrierIdByEdge[e] = "B" + std::to_string(sBI->second) + "|" + t;
                continue;
              }
            }
            if (tBI != leafToBundleIdx.end()) {
              const LeafBundleRecord& bundle = metadata.leafBundles[tBI->second];
              if (isBundleRootReloc(bundle, s)) {
                carrierIdByEdge[e] = "B" + std::to_string(tBI->second) + "|" + s;
                continue;
              }
            }
            auto sCit = clusterByModelIdFull.find(s);
            auto tCit = clusterByModelIdFull.find(t);
            std::string sCluster = sCit != clusterByModelIdFull.end()
              ? sCit->second
              : std::string{};
            std::string tCluster = tCit != clusterByModelIdFull.end()
              ? tCit->second
              : std::string{};
            if (sCluster.empty()) sCluster = nearestClusterReloc(s);
            if (tCluster.empty()) tCluster = nearestClusterReloc(t);
            if (!sCluster.empty() && !tCluster.empty()) {
              carrierIdByEdge[e] = (sCluster == tCluster)
                ? "Cself|" + sCluster
                : (sCluster < tCluster
                    ? "C|" + sCluster + "|" + tCluster
                    : "C|" + tCluster + "|" + sCluster);
            } else {
              carrierIdByEdge[e] = edges[e].edgeId;
            }
          }

          std::vector<Rect> carrierOcclusionRects;
          carrierOcclusionRects.reserve(nodes.size() + metadata.leafBundles.size());
          for (const NodeRecord& node : nodes) {
            if (bundleAbsorbedReloc.count(node.modelId)) continue;
            carrierOcclusionRects.push_back(nodeRect(node, attributes, occMarginReloc));
          }
          for (const LeafBundleRecord& bundle : metadata.leafBundles) {
            carrierOcclusionRects.push_back(renderedLeafBundleRect(bundle, occMarginReloc));
          }
          auto pointInCarrierOcclusionReloc = [&](const RoutePoint& point) {
            if (occMarginReloc <= 0.0) return false;
            for (const Rect& rect : carrierOcclusionRects) {
              if (
                  point.x >= rect.left && point.x <= rect.right
                  && point.y >= rect.top && point.y <= rect.bottom) {
                return true;
              }
            }
            return false;
          };

          std::set<std::pair<std::string, std::string>> seenCarrierPairs;
          std::size_t carrierGroupedCross = 0;
          for (std::size_t i = 0; i < edges.size(); ++i) {
            if (i >= routes.size() || routes[i].size() < 2) continue;
            for (std::size_t j = i + 1; j < edges.size(); ++j) {
              if (j >= routes.size() || routes[j].size() < 2) continue;
              if (sharesEndpoint(edges[i], edges[j])) continue;
              if (carrierIdByEdge[i] == carrierIdByEdge[j]) continue;
              bool anyCross = false;
              for (std::size_t li = 1; li < routes[i].size() && !anyCross; ++li) {
                for (std::size_t rj = 1; rj < routes[j].size() && !anyCross; ++rj) {
                  RoutePoint isect;
                  if (properSegmentIntersection(
                      routes[i][li - 1], routes[i][li],
                      routes[j][rj - 1], routes[j][rj], isect)) {
                    if (!pointInCarrierOcclusionReloc(isect)) {
                      anyCross = true;
                    }
                  }
                }
              }
              if (!anyCross) continue;
              auto pk = carrierIdByEdge[i] < carrierIdByEdge[j]
                ? std::make_pair(carrierIdByEdge[i], carrierIdByEdge[j])
                : std::make_pair(carrierIdByEdge[j], carrierIdByEdge[i]);
              if (seenCarrierPairs.insert(pk).second) {
                ++carrierGroupedCross;
              }
            }
          }
          return carrierGroupedCross;
        };

        auto qualityReloc = [&]() {
          std::vector<std::vector<std::string>> ignoredIdsByEdge;
          std::size_t rawCrossings = 0;
          const std::vector<EdgeCrossingRecord> ignoredCrossings =
            detectRouteCrossings(edges, routes, ignoredIdsByEdge, rawCrossings);
          (void)ignoredCrossings;
          LayoutQualityMetrics qm =
            measureLayoutQuality(nodes, edges, routes, attributes, &metadata.leafBundles);
          qm.edgeCrossings = rawCrossings;
          if (!applyRenderedCarrierMetricsIfRequested(
              nodes,
              edges,
              routes,
              attributes,
              clusterByModelIdFull,
              metadata,
              qm,
              rawCrossings,
              true,
              false)) {
            if (!skipCarrierReloc && !metadata.leafBundles.empty()) {
              qm.edgeCrossings = carrierGroupedCrossReloc();
            }
            qm.visualCrossings =
              qm.edgeCrossings
              + qm.edgeNodeIntersections
              + qm.nodeOverlaps
              + qm.bundleEdgeIntersections
              + qm.bundleNodeOverlaps;
          }
          return qm;
        };

        auto syntheticBundleNodeHitsReloc = [&]() {
          std::size_t hits = 0;
          for (const LeafBundleRecord& bundle : metadata.leafBundles) {
            const std::vector<std::string> rootIds =
              bundle.sharedRootModelIds.empty()
                ? std::vector<std::string>{bundle.parentModelId}
                : bundle.sharedRootModelIds;
            const Rect bundleRect = renderedLeafBundleRect(bundle, 0.0);
            for (const std::string& rootId : rootIds) {
              auto rootIt = id2idxReloc.find(rootId);
              if (rootIt == id2idxReloc.end()) continue;
              const Rect rootRect = nodeRect(nodes[rootIt->second], attributes, 0.0);
              const RoutePoint bundlePort = straightPortOnRect(bundleRect, rootRect);
              const RoutePoint rootPort = straightPortOnRect(rootRect, bundleRect);
              for (const NodeRecord& node : nodes) {
                if (bundleAbsorbedReloc.count(node.modelId)
                    || node.modelId == rootId) {
                  continue;
                }
                if (segmentIntersectsRect(
                    bundlePort,
                    rootPort,
                    nodeRect(node, attributes, kBundleRelocateNodeMargin))) {
                  ++hits;
                }
              }
            }
          }
          return hits;
        };
        auto quickQualityReloc = [&]() {
          LayoutQualityMetrics qm =
            measureLayoutQuality(nodes, edges, routes, attributes, &metadata.leafBundles);
          // Raw routes do not include the synthetic bundle-to-root carrier.
          // Add its node contacts so the cheap shortlist points in the same
          // direction as the exact rendered metric used for acceptance.
          const double connectorWeight = readDoubleEnv(
            "DJERD_BUNDLE_BOX_RELOCATE_FINAL_CONNECTOR_NODE_WEIGHT",
            8.0,
            1.0,
            1000.0);
          qm.edgeNodeIntersections += static_cast<std::size_t>(std::llround(
            connectorWeight
            * static_cast<double>(syntheticBundleNodeHitsReloc())));
          return qm;
        };

        auto obstructionScoreReloc = [&](const LayoutQualityMetrics& qm,
                                         double baseArea) {
          const double bboxGrowth = (baseArea > 0.0 && qm.boundingBoxArea > baseArea)
            ? (qm.boundingBoxArea / baseArea - 1.0)
            : 0.0;
          return
            static_cast<double>(qm.edgeNodeIntersections)
            + 2.0 * static_cast<double>(qm.bundleEdgeIntersections)
            + 80.0 * static_cast<double>(qm.nodeOverlaps)
            + 30.0 * static_cast<double>(qm.bundleNodeOverlaps)
            + 200.0 * bboxGrowth;
        };
        auto fullScoreReloc = [&](const LayoutQualityMetrics& qm,
                                  double baseArea) {
          const double bboxGrowth = (baseArea > 0.0 && qm.boundingBoxArea > baseArea)
            ? (qm.boundingBoxArea / baseArea - 1.0)
            : 0.0;
          return
            static_cast<double>(qm.visualCrossings)
            + 5000.0 * static_cast<double>(qm.nodeOverlaps)
            + 500.0 * static_cast<double>(qm.bundleNodeOverlaps)
            + 500.0 * bboxGrowth;
        };

        struct BundleRelocateConflict {
          std::size_t count = 0;
          double awayX = 0.0;
          double awayY = 0.0;
        };

        auto bundleConflictReloc = [&](std::size_t bundleIndex) {
          BundleRelocateConflict conflict;
          if (bundleIndex >= metadata.leafBundles.size()) return conflict;
          const LeafBundleRecord& bundle = metadata.leafBundles[bundleIndex];
          const Rect rect = renderedLeafBundleRect(bundle, kBundleRelocateMargin);
          const double cx = (rect.left + rect.right) * 0.5;
          const double cy = (rect.top + rect.bottom) * 0.5;
          std::unordered_set<std::string> exempt;
          exempt.insert(bundle.parentModelId);
          for (const std::string& leaf : bundle.leafModelIds) exempt.insert(leaf);
          for (const std::string& root : bundle.sharedRootModelIds) exempt.insert(root);

          auto addAwayFromSegment = [&](const RoutePoint& a, const RoutePoint& b) {
            const double dx = b.x - a.x;
            const double dy = b.y - a.y;
            const double len2 = dx * dx + dy * dy;
            if (len2 < 1e-6) return;
            const double t = std::clamp(
              ((cx - a.x) * dx + (cy - a.y) * dy) / len2,
              0.0,
              1.0);
            const double px = a.x + dx * t;
            const double py = a.y + dy * t;
            double ax = cx - px;
            double ay = cy - py;
            const double dist = std::sqrt(ax * ax + ay * ay);
            if (dist > 1e-6) {
              conflict.awayX += ax / dist;
              conflict.awayY += ay / dist;
            } else {
              const double len = std::sqrt(len2);
              conflict.awayX += -dy / len;
              conflict.awayY += dx / len;
            }
          };

          for (std::size_t e = 0; e < routes.size() && e < edges.size(); ++e) {
            if (exempt.count(edges[e].sourceModelId)
                || exempt.count(edges[e].targetModelId)) {
              continue;
            }
            const auto& route = routes[e];
            if (route.size() < 2) continue;
            for (std::size_t si = 1; si < route.size(); ++si) {
              if (segmentIntersectsRect(route[si - 1], route[si], rect)) {
                ++conflict.count;
                addAwayFromSegment(route[si - 1], route[si]);
              }
            }
          }
          for (const NodeRecord& nd : nodes) {
            if (bundleAbsorbedReloc.count(nd.modelId)) continue;
            const Rect nr = nodeRect(nd, attributes, kBundleRelocateNodeMargin);
            if (!rectsOverlap(rect, nr)) continue;
            ++conflict.count;
            double ax = cx - attributes.x(nd.handle);
            double ay = cy - attributes.y(nd.handle);
            const double len = std::sqrt(ax * ax + ay * ay);
            if (len > 1e-6) {
              conflict.awayX += ax / len;
              conflict.awayY += ay / len;
            }
          }

          // The rendered leaf bundle has one synthetic straight carrier to
          // each shared root. Raw-route scoring cannot see this line, which
          // made compact layouts rank the wrong bundles for relocation. Add
          // its exact table-obstacle contacts to the quick conflict signal;
          // full candidates are still accepted only by rendered metrics.
          const std::vector<std::string> rootIds =
            bundle.sharedRootModelIds.empty()
              ? std::vector<std::string>{bundle.parentModelId}
              : bundle.sharedRootModelIds;
          const Rect connectorBundleRect = renderedLeafBundleRect(bundle, 0.0);
          for (const std::string& rootId : rootIds) {
            auto rootIt = id2idxReloc.find(rootId);
            if (rootIt == id2idxReloc.end()) continue;
            const Rect rootRect = nodeRect(nodes[rootIt->second], attributes, 0.0);
            const RoutePoint bundlePort =
              straightPortOnRect(connectorBundleRect, rootRect);
            const RoutePoint rootPort =
              straightPortOnRect(rootRect, connectorBundleRect);
            for (const NodeRecord& node : nodes) {
              if (bundleAbsorbedReloc.count(node.modelId)
                  || exempt.count(node.modelId)) {
                continue;
              }
              if (segmentIntersectsRect(
                  bundlePort,
                  rootPort,
                  nodeRect(node, attributes, kBundleRelocateNodeMargin))) {
                ++conflict.count;
                addAwayFromSegment(bundlePort, rootPort);
              }
            }
          }
          return conflict;
        };

        struct BundleRelocateState {
          std::vector<std::pair<std::size_t, std::pair<double, double>>> positions;
          std::vector<std::vector<RoutePoint>> routes;
          std::vector<LeafBundleRecord> bundles;
        };

        auto snapshotBundleReloc = [&](std::size_t bundleIndex) {
          BundleRelocateState state;
          if (bundleIndex < metadata.leafBundles.size()) {
            for (const std::string& leaf : metadata.leafBundles[bundleIndex].leafModelIds) {
              auto it = id2idxReloc.find(leaf);
              if (it == id2idxReloc.end()) continue;
              const std::size_t idx = it->second;
              state.positions.push_back({
                idx,
                {attributes.x(nodes[idx].handle), attributes.y(nodes[idx].handle)}
              });
            }
          }
          state.routes = routes;
          state.bundles = metadata.leafBundles;
          return state;
        };
        auto restoreBundleReloc = [&](const BundleRelocateState& state) {
          for (const auto& entry : state.positions) {
            const std::size_t idx = entry.first;
            if (idx >= nodes.size()) continue;
            attributes.x(nodes[idx].handle) = entry.second.first;
            attributes.y(nodes[idx].handle) = entry.second.second;
          }
          routes = state.routes;
          metadata.leafBundles = state.bundles;
        };
        auto translateBundleReloc = [&](std::size_t bundleIndex, double dx, double dy) {
          if (bundleIndex >= metadata.leafBundles.size()) return false;
          bool moved = false;
          for (const std::string& leaf : metadata.leafBundles[bundleIndex].leafModelIds) {
            auto it = id2idxReloc.find(leaf);
            if (it == id2idxReloc.end()) continue;
            const NodeRecord& nd = nodes[it->second];
            attributes.x(nd.handle) += dx;
            attributes.y(nd.handle) += dy;
            moved = true;
          }
          return moved;
        };

        struct BundleCandidateOffset {
          double dx = 0.0;
          double dy = 0.0;
        };
        struct BundleCandidateScore {
          double quickScore = 0.0;
          BundleCandidateOffset offset;
        };

        auto candidateOffsetsReloc = [&](std::size_t bundleIndex,
                                         const BundleRelocateConflict& conflict) {
          std::vector<BundleCandidateOffset> offsets;
          std::set<std::pair<long long, long long>> seen;
          if (bundleIndex >= metadata.leafBundles.size()) return offsets;
          const LeafBundleRecord& bundle = metadata.leafBundles[bundleIndex];
          const Rect rect = renderedLeafBundleRect(bundle, kBundleRelocateMargin);
          const double cx = (rect.left + rect.right) * 0.5;
          const double cy = (rect.top + rect.bottom) * 0.5;
          double px = cx;
          double py = cy;
          auto pit = id2idxReloc.find(bundle.parentModelId);
          if (pit != id2idxReloc.end()) {
            px = attributes.x(nodes[pit->second].handle);
            py = attributes.y(nodes[pit->second].handle);
          }

          std::vector<std::pair<double, double>> directions;
          auto addDirection = [&](double dx, double dy) {
            const double len = std::sqrt(dx * dx + dy * dy);
            if (len <= 1e-6) return;
            directions.push_back({dx / len, dy / len});
          };
          addDirection(conflict.awayX, conflict.awayY);
          addDirection(cx - px, cy - py);
          constexpr int kDirCount = 16;
          constexpr double kBundleRelocatePi = 3.14159265358979323846;
          for (int i = 0; i < kDirCount; ++i) {
            const double angle = (2.0 * kBundleRelocatePi * static_cast<double>(i))
              / static_cast<double>(kDirCount);
            addDirection(std::cos(angle), std::sin(angle));
          }

          std::vector<double> ringRadii = radii;
          const double currentRadius = std::hypot(cx - px, cy - py);
          if (currentRadius > 1.0) {
            ringRadii.push_back(currentRadius);
            ringRadii.push_back(currentRadius * 0.72);
            ringRadii.push_back(currentRadius * 1.28);
          }
          std::sort(ringRadii.begin(), ringRadii.end());
          ringRadii.erase(
            std::unique(
              ringRadii.begin(),
              ringRadii.end(),
              [](double a, double b) { return std::abs(a - b) < 25.0; }),
            ringRadii.end());

          auto addOffset = [&](double dx, double dy) {
            const double len = std::sqrt(dx * dx + dy * dy);
            if (len <= 1.0 || len > maxMove) return;
            const auto key = std::make_pair(
              static_cast<long long>(std::llround(dx / 20.0)),
              static_cast<long long>(std::llround(dy / 20.0)));
            if (!seen.insert(key).second) return;
            offsets.push_back({dx, dy});
          };

          // Try parent/root-centred slots first. In compact layouts the
          // current bundle may be thousands of units away, and filling the
          // bounded shortlist with incremental translations never reaches a
          // short, clear connector near its root.
          for (const auto& dir : directions) {
            for (double radius : ringRadii) {
              const double tx = px + dir.first * radius;
              const double ty = py + dir.second * radius;
              addOffset(tx - cx, ty - cy);
              if (static_cast<int>(offsets.size()) >= maxCandidates) return offsets;
            }
          }
          for (const auto& dir : directions) {
            for (double radius : radii) {
              addOffset(dir.first * radius, dir.second * radius);
              if (static_cast<int>(offsets.size()) >= maxCandidates) return offsets;
            }
          }
          return offsets;
        };

        LayoutQualityMetrics currentFull = qualityReloc();
        const double baseArea = currentFull.boundingBoxArea;
        double currentFullScore = fullScoreReloc(currentFull, baseArea);
        auto obstacleExcessReloc = [&](const LayoutQualityMetrics& quality) {
          return
            (quality.edgeNodeIntersections > edgeNodeTarget
              ? quality.edgeNodeIntersections - edgeNodeTarget
              : 0)
            + (quality.bundleEdgeIntersections > bundleEdgeTarget
              ? quality.bundleEdgeIntersections - bundleEdgeTarget
              : 0);
        };
        auto fullCandidateIsSafe = [&](const LayoutQualityMetrics& candidate) {
          return
            candidate.edgeNodeIntersections
              <= currentFull.edgeNodeIntersections
            && candidate.nodeOverlaps <= currentFull.nodeOverlaps
            && candidate.bundleNodeOverlaps <= currentFull.bundleNodeOverlaps
            && candidate.nodeSpacingOverlaps
              <= currentFull.nodeSpacingOverlaps
            && candidate.edgeBendTotal <= 1e-6
            && candidate.visualCrossings
              <= std::max(visualTarget, currentFull.visualCrossings);
        };
        auto fullCandidateIsBetter = [&]
            (const LayoutQualityMetrics& candidate,
             double candidateScore,
             const LayoutQualityMetrics& incumbent,
             double incumbentScore) {
          if (!fullCandidateIsSafe(candidate)) return false;
          const std::size_t candidateExcess = obstacleExcessReloc(candidate);
          const std::size_t incumbentExcess = obstacleExcessReloc(incumbent);
          if (candidateExcess != incumbentExcess) {
            return candidateExcess < incumbentExcess;
          }
          return candidateScore + minGain < incumbentScore;
        };
        std::size_t totalAccepted = 0;
        std::size_t totalTried = 0;
        bool hitCandidateLimit = false;

        for (int pass = 0; pass < passes; ++pass) {
          std::vector<std::pair<std::size_t, std::size_t>> rankedBundles;
          rankedBundles.reserve(metadata.leafBundles.size());
          for (std::size_t bi = 0; bi < metadata.leafBundles.size(); ++bi) {
            const BundleRelocateConflict conflict = bundleConflictReloc(bi);
            if (conflict.count == 0) continue;
            rankedBundles.emplace_back(conflict.count, bi);
          }
          std::sort(
            rankedBundles.begin(),
            rankedBundles.end(),
            [](const auto& left, const auto& right) {
              if (left.first != right.first) return left.first > right.first;
              return left.second < right.second;
            });

          std::size_t acceptedThisPass = 0;
          const std::size_t limit =
            std::min<std::size_t>(
              rankedBundles.size(),
              static_cast<std::size_t>(topBundles));
          for (std::size_t rank = 0; rank < limit; ++rank) {
            if (totalTried >= totalCandidateLimit) {
              hitCandidateLimit = true;
              break;
            }
            const std::size_t bi = rankedBundles[rank].second;
            const BundleRelocateConflict conflict = bundleConflictReloc(bi);
            if (conflict.count == 0) continue;
            const std::vector<BundleCandidateOffset> offsets =
              candidateOffsetsReloc(bi, conflict);
            if (offsets.empty()) continue;

            LayoutQualityMetrics currentQuick = quickQualityReloc();
            const double currentQuickScore = obstructionScoreReloc(currentQuick, baseArea);
            std::vector<BundleCandidateScore> shortlist;

            const BundleRelocateState baseState = snapshotBundleReloc(bi);
            double bestFullScore = currentFullScore;
            LayoutQualityMetrics bestFull = currentFull;
            BundleCandidateOffset bestOffset;
            bool haveFullBest = false;
            for (const BundleCandidateOffset& offset : offsets) {
              if (totalTried >= totalCandidateLimit) {
                hitCandidateLimit = true;
                break;
              }
              ++totalTried;
              translateBundleReloc(bi, offset.dx, offset.dy);
              recomputeLeafBundlesReloc();
              rerouteReloc();
              const LayoutQualityMetrics qm = quickQualityReloc();
              const bool bboxOk =
                baseArea <= 0.0 || qm.boundingBoxArea <= baseArea * bboxLimit;
              const double score = obstructionScoreReloc(qm, baseArea);
              if (fullScan && bboxOk && score <= currentQuickScore + quickSlack) {
                const LayoutQualityMetrics nextFull = qualityReloc();
                const double nextFullScore = fullScoreReloc(nextFull, baseArea);
                if (fullCandidateIsBetter(
                    nextFull, nextFullScore, bestFull, bestFullScore)) {
                  bestFullScore = nextFullScore;
                  bestFull = nextFull;
                  bestOffset = offset;
                  haveFullBest = true;
                }
              }
              if (bboxOk && score + 1e-6 < currentQuickScore) {
                shortlist.push_back({score, offset});
                std::sort(
                  shortlist.begin(),
                  shortlist.end(),
                  [](const auto& left, const auto& right) {
                    return left.quickScore < right.quickScore;
                  });
                if (static_cast<int>(shortlist.size()) > shortlistSize) {
                  shortlist.pop_back();
                }
              }
              restoreBundleReloc(baseState);
            }
            if (!fullScan || !haveFullBest) {
              if (shortlist.empty()) continue;
              for (const BundleCandidateScore& candidate : shortlist) {
                translateBundleReloc(bi, candidate.offset.dx, candidate.offset.dy);
                recomputeLeafBundlesReloc();
                rerouteReloc();
                const LayoutQualityMetrics nextFull = qualityReloc();
                const bool bboxOk =
                  baseArea <= 0.0 || nextFull.boundingBoxArea <= baseArea * bboxLimit;
                const double nextFullScore = fullScoreReloc(nextFull, baseArea);
                if (
                    bboxOk
                    && fullCandidateIsBetter(
                      nextFull, nextFullScore, bestFull, bestFullScore)) {
                  bestFullScore = nextFullScore;
                  bestFull = nextFull;
                  bestOffset = candidate.offset;
                  haveFullBest = true;
                }
                restoreBundleReloc(baseState);
              }
            }
            if (!haveFullBest) continue;

            translateBundleReloc(bi, bestOffset.dx, bestOffset.dy);
            recomputeLeafBundlesReloc();
            rerouteReloc();
            currentFull = bestFull;
            currentFullScore = bestFullScore;
            ++acceptedThisPass;
            ++totalAccepted;
          }
          if (acceptedThisPass == 0 || hitCandidateLimit) break;
        }

        std::fprintf(stderr,
          "[bundle-box-relocate-final] accepted %zu/%zu candidates, "
          "visual=%zu edgeNode=%zu bundleEdge=%zu bundleNode=%zu bbox=%.2fB%s.\n",
          totalAccepted,
          totalTried,
          currentFull.visualCrossings,
          currentFull.edgeNodeIntersections,
          currentFull.bundleEdgeIntersections,
          currentFull.bundleNodeOverlaps,
          currentFull.boundingBoxArea / 1e9,
          hitCandidateLimit ? " (candidate limit)" : "");
      }
    }

    // Bundle relocation can improve the global rendered score while leaving
    // one or two tiny bundle-vs-node contacts. Run a final, metric-gated
    // clearance after relocation so compressed bbox candidates are not
    // discarded for a small residual rendered-box clash.
    {
      const bool clearAfterRelocate =
        readBoolEnv("DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL", false);
      if (clearAfterRelocate && !metadata.leafBundles.empty()) {
        auto rerouteAfterRelocateClear = [&]() {
          routes = crossAwareRouting
            ? routeAllEdgesCrossAware(nodes, edges, attributes)
            : (straightLineMode
              ? (arguments.edgeRouting == "straight_smart"
                  && !isStraightLineRoutingMode(arguments.mode)
                ? routeAllEdgesStraightSmart(nodes, edges, attributes)
                : routeAllEdgesStraight(edges, attributes))
              : routeAllEdges(nodes, edges, attributes, true));
        };

        auto measureAfterRelocateClearQuality = [&]() {
          std::vector<std::vector<std::string>> ignoredIdsByEdge;
          std::size_t rawCrossings = 0;
          (void)detectRouteCrossings(edges, routes, ignoredIdsByEdge, rawCrossings);
          LayoutQualityMetrics qm =
            measureLayoutQuality(nodes, edges, routes, attributes, &metadata.leafBundles);
          qm.edgeCrossings = rawCrossings;
          if (!applyRenderedCarrierMetricsIfRequested(
              nodes,
              edges,
              routes,
              attributes,
              clusterByModelIdFull,
              metadata,
              qm,
              rawCrossings,
              true,
              false)) {
            qm.visualCrossings =
              qm.edgeCrossings
              + qm.edgeNodeIntersections
              + qm.nodeOverlaps
              + qm.bundleEdgeIntersections
              + qm.bundleNodeOverlaps;
          }
          return qm;
        };

        auto measureAfterRelocateClearQuick = [&]() {
          return measureLayoutQuality(nodes, edges, routes, attributes, &metadata.leafBundles);
        };

        const std::size_t clearPasses = static_cast<std::size_t>(
          readDoubleEnv(
            "DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_PASSES",
            4.0,
            1.0,
            16.0));
        for (std::size_t clearPass = 0; clearPass < clearPasses; ++clearPass) {
          const LayoutQualityMetrics baseQuality = measureAfterRelocateClearQuality();
          if (baseQuality.bundleNodeOverlaps == 0) break;
          struct BundleNodeClearCandidate {
            std::size_t bundleIndex = 0;
            double dx = 0.0;
            double dy = 0.0;
          };
          struct BundleNodeClearScoredCandidate {
            BundleNodeClearCandidate candidate;
            LayoutQualityMetrics quickQuality;
            std::size_t movedLeaves = 0;
            double moveDistance = 0.0;
          };

          std::unordered_map<std::string, std::size_t> id2idxAfterClear;
          id2idxAfterClear.reserve(nodes.size());
          for (std::size_t i = 0; i < nodes.size(); ++i) {
            id2idxAfterClear[nodes[i].modelId] = i;
          }
          std::unordered_set<std::string> absorbedAfterClear;
          for (const LeafBundleRecord& bundle : metadata.leafBundles) {
            absorbedAfterClear.insert(bundle.parentModelId);
            for (const std::string& leaf : bundle.leafModelIds) {
              absorbedAfterClear.insert(leaf);
            }
          }

          auto translateBundleAfterClear =
            [&](std::size_t bundleIndex, double dx, double dy) {
              if (bundleIndex >= metadata.leafBundles.size()) return std::size_t{0};
              std::size_t movedLeaves = 0;
              for (const std::string& leaf : metadata.leafBundles[bundleIndex].leafModelIds) {
                auto it = id2idxAfterClear.find(leaf);
                if (it == id2idxAfterClear.end()) continue;
                const NodeRecord& node = nodes[it->second];
                attributes.x(node.handle) += dx;
                attributes.y(node.handle) += dy;
                ++movedLeaves;
              }
              return movedLeaves;
            };

          auto bundleConflictOffsetsAfterClear = [&](std::size_t bundleIndex) {
            std::vector<std::pair<double, double>> offsets;
            std::set<std::pair<long long, long long>> seen;
            if (bundleIndex >= metadata.leafBundles.size()) return offsets;
            const LeafBundleRecord& bundle = metadata.leafBundles[bundleIndex];
            const Rect br = renderedLeafBundleRect(bundle, leafBundleVisualMargin());
            const double bundleCx = rectCenterX(br);
            const double bundleCy = rectCenterY(br);
            auto addOffset = [&](double dx, double dy) {
              if (std::hypot(dx, dy) < 0.5) return;
              const auto key = std::make_pair(
                static_cast<long long>(std::llround(dx / 4.0)),
                static_cast<long long>(std::llround(dy / 4.0)));
              if (!seen.insert(key).second) return;
              offsets.push_back({dx, dy});
            };

            std::size_t conflicts = 0;
            for (const NodeRecord& node : nodes) {
              if (absorbedAfterClear.count(node.modelId)) continue;
              const Rect nr = nodeRect(node, attributes, visualNodeMargin());
              if (!rectsOverlap(br, nr)) continue;
              const double overlapX =
                std::min(br.right, nr.right) - std::max(br.left, nr.left);
              const double overlapY =
                std::min(br.bottom, nr.bottom) - std::max(br.top, nr.top);
              if (overlapX <= 0.0 || overlapY <= 0.0) continue;
              ++conflicts;
              const double nodeCx = rectCenterX(nr);
              const double nodeCy = rectCenterY(nr);
              if (overlapY <= overlapX) {
                const double dir = bundleCy < nodeCy ? -1.0 : 1.0;
                for (double pad : {2.0, 8.0, 16.0, 32.0}) {
                  addOffset(0.0, dir * (overlapY + pad));
                }
              } else {
                const double dir = bundleCx < nodeCx ? -1.0 : 1.0;
                for (double pad : {2.0, 8.0, 16.0, 32.0}) {
                  addOffset(dir * (overlapX + pad), 0.0);
                }
              }
            }
            if (conflicts == 0) return offsets;

            for (double step : {120.0, 64.0, 32.0, 16.0}) {
              addOffset(-step, 0.0);
              addOffset(step, 0.0);
              addOffset(0.0, -step);
              addOffset(0.0, step);
            }
            return offsets;
          };

          auto conflictingBundleIndexesAfterClear = [&]() {
            std::vector<std::pair<std::size_t, std::size_t>> ranked;
            for (std::size_t bi = 0; bi < metadata.leafBundles.size(); ++bi) {
              const Rect br =
                renderedLeafBundleRect(metadata.leafBundles[bi], leafBundleVisualMargin());
              std::size_t conflicts = 0;
              for (const NodeRecord& node : nodes) {
                if (absorbedAfterClear.count(node.modelId)) continue;
                const Rect nr = nodeRect(node, attributes, visualNodeMargin());
                if (rectsOverlap(br, nr)) ++conflicts;
              }
              if (conflicts > 0) ranked.push_back({conflicts, bi});
            }
            std::sort(ranked.begin(), ranked.end(),
              [](const auto& left, const auto& right) {
                if (left.first != right.first) return left.first > right.first;
                return left.second < right.second;
              });
            return ranked;
          };

          std::vector<std::pair<double, double>> originalPositions;
          originalPositions.reserve(nodes.size());
          for (const NodeRecord& node : nodes) {
            originalPositions.push_back({
              attributes.x(node.handle),
              attributes.y(node.handle),
            });
          }
          const auto originalRoutes = routes;
          const auto originalBundles = metadata.leafBundles;

          const std::size_t topBundles = static_cast<std::size_t>(
            readDoubleEnv(
              "DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_TOP",
              6.0,
              1.0,
              64.0));
          const std::size_t maxCandidates = static_cast<std::size_t>(
            readDoubleEnv(
              "DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_MAX_CANDIDATES",
              24.0,
              1.0,
              256.0));
          const std::size_t fullShortlist = static_cast<std::size_t>(
            readDoubleEnv(
              "DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_FULL_SHORTLIST",
              5.0,
              1.0,
              32.0));
          const std::size_t visualSlack = static_cast<std::size_t>(
            readDoubleEnv(
              "DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_VISUAL_SLACK",
              80.0,
              0.0,
              100000.0));
          const std::size_t edgeNodeSlack = static_cast<std::size_t>(
            readDoubleEnv(
              "DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_EDGE_NODE_SLACK",
              80.0,
              0.0,
              100000.0));
          const double bboxLimit =
            readDoubleEnv(
              "DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_BBOX_LIMIT",
              1.03,
              1.0,
              4.0);

          std::vector<BundleNodeClearCandidate> candidates;
          const auto rankedBundles = conflictingBundleIndexesAfterClear();
          for (std::size_t rank = 0;
               rank < rankedBundles.size() && rank < topBundles
                 && candidates.size() < maxCandidates;
               ++rank) {
            const std::size_t bi = rankedBundles[rank].second;
            const auto offsets = bundleConflictOffsetsAfterClear(bi);
            for (const auto& offset : offsets) {
              candidates.push_back({bi, offset.first, offset.second});
              if (candidates.size() >= maxCandidates) break;
            }
          }

          bool haveBest = false;
          LayoutQualityMetrics bestQuality = baseQuality;
          BundleNodeClearCandidate bestCandidate;
          std::size_t bestMovedLeaves = 0;
          std::vector<BundleNodeClearScoredCandidate> shortlist;

          auto quickCandidateLess = [](const BundleNodeClearScoredCandidate& left,
                                       const BundleNodeClearScoredCandidate& right) {
            const LayoutQualityMetrics& lq = left.quickQuality;
            const LayoutQualityMetrics& rq = right.quickQuality;
            if (lq.bundleNodeOverlaps != rq.bundleNodeOverlaps) {
              return lq.bundleNodeOverlaps < rq.bundleNodeOverlaps;
            }
            if (lq.edgeNodeIntersections != rq.edgeNodeIntersections) {
              return lq.edgeNodeIntersections < rq.edgeNodeIntersections;
            }
            if (lq.bundleEdgeIntersections != rq.bundleEdgeIntersections) {
              return lq.bundleEdgeIntersections < rq.bundleEdgeIntersections;
            }
            if (std::abs(lq.boundingBoxArea - rq.boundingBoxArea) > 1.0) {
              return lq.boundingBoxArea < rq.boundingBoxArea;
            }
            if (std::abs(left.moveDistance - right.moveDistance) > 0.1) {
              return left.moveDistance > right.moveDistance;
            }
            if (left.candidate.bundleIndex != right.candidate.bundleIndex) {
              return left.candidate.bundleIndex < right.candidate.bundleIndex;
            }
            if (std::abs(left.candidate.dx - right.candidate.dx) > 0.1) {
              return left.candidate.dx < right.candidate.dx;
            }
            return left.candidate.dy < right.candidate.dy;
          };

          auto restoreAfterRelocateClear = [&]() {
            for (std::size_t i = 0; i < nodes.size(); ++i) {
              attributes.x(nodes[i].handle) = originalPositions[i].first;
              attributes.y(nodes[i].handle) = originalPositions[i].second;
            }
            routes = originalRoutes;
            metadata.leafBundles = originalBundles;
          };

          for (const BundleNodeClearCandidate& candidate : candidates) {
            restoreAfterRelocateClear();
            const std::size_t movedLeaves =
              translateBundleAfterClear(candidate.bundleIndex, candidate.dx, candidate.dy);
            if (movedLeaves == 0) continue;
            recomputeLeafBundleBboxesFromNodes(metadata.leafBundles, nodes, attributes);
            rerouteAfterRelocateClear();
            const LayoutQualityMetrics quickQuality = measureAfterRelocateClearQuick();

            const bool bundleNodeImproved =
              quickQuality.bundleNodeOverlaps < baseQuality.bundleNodeOverlaps;
            const bool nodeOverlapOk =
              quickQuality.nodeOverlaps <= baseQuality.nodeOverlaps;
            const bool bboxOk =
              baseQuality.boundingBoxArea <= 0.0
              || quickQuality.boundingBoxArea <= baseQuality.boundingBoxArea * bboxLimit;
            if (!bundleNodeImproved || !nodeOverlapOk || !bboxOk) {
              continue;
            }

            shortlist.push_back({
              candidate,
              quickQuality,
              movedLeaves,
              std::hypot(candidate.dx, candidate.dy),
            });
            std::sort(shortlist.begin(), shortlist.end(), quickCandidateLess);
            if (shortlist.size() > fullShortlist) {
              shortlist.pop_back();
            }
          }

          for (const BundleNodeClearScoredCandidate& scored : shortlist) {
            restoreAfterRelocateClear();
            const BundleNodeClearCandidate& candidate = scored.candidate;
            const std::size_t movedLeaves =
              translateBundleAfterClear(candidate.bundleIndex, candidate.dx, candidate.dy);
            if (movedLeaves == 0) continue;
            recomputeLeafBundleBboxesFromNodes(metadata.leafBundles, nodes, attributes);
            rerouteAfterRelocateClear();
            const LayoutQualityMetrics nextQuality = measureAfterRelocateClearQuality();

            const bool bundleNodeImproved =
              nextQuality.bundleNodeOverlaps < baseQuality.bundleNodeOverlaps;
            const bool nodeOverlapOk =
              nextQuality.nodeOverlaps <= baseQuality.nodeOverlaps;
            const bool visualOk =
              nextQuality.visualCrossings <= baseQuality.visualCrossings + visualSlack;
            const bool edgeNodeOk =
              nextQuality.edgeNodeIntersections
                <= baseQuality.edgeNodeIntersections + edgeNodeSlack;
            const bool bboxOk =
              baseQuality.boundingBoxArea <= 0.0
              || nextQuality.boundingBoxArea <= baseQuality.boundingBoxArea * bboxLimit;
            if (!bundleNodeImproved || !nodeOverlapOk || !visualOk || !edgeNodeOk || !bboxOk) {
              continue;
            }

            const bool better =
              !haveBest
              || nextQuality.bundleNodeOverlaps < bestQuality.bundleNodeOverlaps
              || (
                nextQuality.bundleNodeOverlaps == bestQuality.bundleNodeOverlaps
                && nextQuality.visualCrossings < bestQuality.visualCrossings)
              || (
                nextQuality.bundleNodeOverlaps == bestQuality.bundleNodeOverlaps
                && nextQuality.visualCrossings == bestQuality.visualCrossings
                && nextQuality.edgeNodeIntersections < bestQuality.edgeNodeIntersections);
            if (better) {
              haveBest = true;
              bestQuality = nextQuality;
              bestCandidate = candidate;
              bestMovedLeaves = scored.movedLeaves;
            }
          }

          restoreAfterRelocateClear();
          if (haveBest) {
            (void)translateBundleAfterClear(bestCandidate.bundleIndex, bestCandidate.dx, bestCandidate.dy);
            recomputeLeafBundleBboxesFromNodes(metadata.leafBundles, nodes, attributes);
            rerouteAfterRelocateClear();
            std::fprintf(stderr,
              "[leaf-bundle-node-clear-after-relocate-final] accepted %zu leaves, "
              "bundle=%zu offset=(%.1f,%.1f) bundleNode=%zu -> %zu "
              "visual=%zu -> %zu edgeNode=%zu -> %zu bbox=%.2fB -> %.2fB.\n",
              bestMovedLeaves,
              bestCandidate.bundleIndex,
              bestCandidate.dx,
              bestCandidate.dy,
              baseQuality.bundleNodeOverlaps,
              bestQuality.bundleNodeOverlaps,
              baseQuality.visualCrossings,
              bestQuality.visualCrossings,
              baseQuality.edgeNodeIntersections,
              bestQuality.edgeNodeIntersections,
              baseQuality.boundingBoxArea / 1e9,
              bestQuality.boundingBoxArea / 1e9);
          } else {
            std::fprintf(stderr,
              "[leaf-bundle-node-clear-after-relocate-final] rejected %zu candidates "
              "(%zu precise), bundleNode=%zu visual=%zu edgeNode=%zu bbox=%.2fB.\n",
              candidates.size(),
              shortlist.size(),
              baseQuality.bundleNodeOverlaps,
              baseQuality.visualCrossings,
              baseQuality.edgeNodeIntersections,
              baseQuality.boundingBoxArea / 1e9);
            for (std::size_t i = 0; i < nodes.size(); ++i) {
              attributes.x(nodes[i].handle) = originalPositions[i].first;
              attributes.y(nodes[i].handle) = originalPositions[i].second;
            }
            routes = originalRoutes;
            metadata.leafBundles = originalBundles;
            break;
          }
        }
      }
    }

    // Final node-spacing clearance after all bundle moves. Bundle relocation
    // can leave ordinary rendered node boxes with too little breathing room
    // even when hard node/bundle collisions are gone. This pass targets the
    // spacing metric directly and keeps the result only under the same visual
    // and bbox guards as the other final passes.
    {
      const bool nodeSpacingClear =
        readBoolEnv("DJERD_NODE_SPACING_CLEAR_FINAL", false);
      if (nodeSpacingClear && nodes.size() > 1) {
        auto rerouteNodeSpacingClear = [&]() {
          routes = crossAwareRouting
            ? routeAllEdgesCrossAware(nodes, edges, attributes)
            : (straightLineMode
              ? (arguments.edgeRouting == "straight_smart"
                  && !isStraightLineRoutingMode(arguments.mode)
                ? routeAllEdgesStraightSmart(nodes, edges, attributes)
                : routeAllEdgesStraight(edges, attributes))
              : routeAllEdges(nodes, edges, attributes, true));
        };

        auto measureNodeSpacingClearQuality = [&]() {
          std::vector<std::vector<std::string>> ignoredIdsByEdge;
          std::size_t rawCrossings = 0;
          (void)detectRouteCrossings(edges, routes, ignoredIdsByEdge, rawCrossings);
          LayoutQualityMetrics qm =
            measureLayoutQuality(nodes, edges, routes, attributes, &metadata.leafBundles);
          qm.edgeCrossings = rawCrossings;
          if (!applyRenderedCarrierMetricsIfRequested(
              nodes,
              edges,
              routes,
              attributes,
              clusterByModelIdFull,
              metadata,
              qm,
              rawCrossings,
              true,
              false)) {
            qm.visualCrossings =
              qm.edgeCrossings
              + qm.edgeNodeIntersections
              + qm.nodeOverlaps
              + qm.bundleEdgeIntersections
              + qm.bundleNodeOverlaps;
          }
          return qm;
        };

        const LayoutQualityMetrics baseQuality = measureNodeSpacingClearQuality();
        if (baseQuality.nodeSpacingOverlaps > 0) {
          std::vector<std::pair<double, double>> originalPositions;
          originalPositions.reserve(nodes.size());
          for (const NodeRecord& node : nodes) {
            originalPositions.push_back({
              attributes.x(node.handle),
              attributes.y(node.handle),
            });
          }
          const auto originalRoutes = routes;
          const auto originalBundles = metadata.leafBundles;

          const std::size_t moved =
            clearNodeSpacingOverlaps(metadata.leafBundles, nodes, attributes);
          if (moved > 0) {
            recomputeLeafBundleBboxesFromNodes(metadata.leafBundles, nodes, attributes);
            rerouteNodeSpacingClear();
            const LayoutQualityMetrics nextQuality = measureNodeSpacingClearQuality();

            const std::size_t visualSlack = static_cast<std::size_t>(
              readDoubleEnv(
                "DJERD_NODE_SPACING_CLEAR_FINAL_VISUAL_SLACK",
                80.0,
                0.0,
                100000.0));
            const std::size_t edgeNodeSlack = static_cast<std::size_t>(
              readDoubleEnv(
                "DJERD_NODE_SPACING_CLEAR_FINAL_EDGE_NODE_SLACK",
                80.0,
                0.0,
                100000.0));
            const double bboxLimit =
              readDoubleEnv(
                "DJERD_NODE_SPACING_CLEAR_FINAL_BBOX_LIMIT",
                1.025,
                1.0,
                4.0);

            const bool spacingImproved =
              nextQuality.nodeSpacingOverlaps < baseQuality.nodeSpacingOverlaps;
            const bool visualOk =
              nextQuality.visualCrossings <= baseQuality.visualCrossings + visualSlack;
            const bool edgeNodeOk =
              nextQuality.edgeNodeIntersections
                <= baseQuality.edgeNodeIntersections + edgeNodeSlack;
            const bool nodeOverlapOk =
              nextQuality.nodeOverlaps <= baseQuality.nodeOverlaps;
            const bool bundleNodeOk =
              nextQuality.bundleNodeOverlaps <= baseQuality.bundleNodeOverlaps;
            const bool bboxOk =
              baseQuality.boundingBoxArea <= 0.0
              || nextQuality.boundingBoxArea <= baseQuality.boundingBoxArea * bboxLimit;

            if (spacingImproved && visualOk && edgeNodeOk
                && nodeOverlapOk && bundleNodeOk && bboxOk) {
              std::fprintf(stderr,
                "[node-spacing-clear-final] accepted %zu node moves, "
                "spacing=%zu -> %zu visual=%zu -> %zu edgeNode=%zu -> %zu "
                "bundleNode=%zu -> %zu nodeOverlaps=%zu -> %zu "
                "bbox=%.2fB -> %.2fB.\n",
                moved,
                baseQuality.nodeSpacingOverlaps,
                nextQuality.nodeSpacingOverlaps,
                baseQuality.visualCrossings,
                nextQuality.visualCrossings,
                baseQuality.edgeNodeIntersections,
                nextQuality.edgeNodeIntersections,
                baseQuality.bundleNodeOverlaps,
                nextQuality.bundleNodeOverlaps,
                baseQuality.nodeOverlaps,
                nextQuality.nodeOverlaps,
                baseQuality.boundingBoxArea / 1e9,
                nextQuality.boundingBoxArea / 1e9);
            } else {
              for (std::size_t i = 0; i < nodes.size(); ++i) {
                attributes.x(nodes[i].handle) = originalPositions[i].first;
                attributes.y(nodes[i].handle) = originalPositions[i].second;
              }
              routes = originalRoutes;
              metadata.leafBundles = originalBundles;
              std::fprintf(stderr,
                "[node-spacing-clear-final] rejected %zu node moves, "
                "spacing=%zu -> %zu visual=%zu -> %zu edgeNode=%zu -> %zu "
                "bundleNode=%zu -> %zu nodeOverlaps=%zu -> %zu "
                "bbox=%.2fB -> %.2fB.\n",
                moved,
                baseQuality.nodeSpacingOverlaps,
                nextQuality.nodeSpacingOverlaps,
                baseQuality.visualCrossings,
                nextQuality.visualCrossings,
                baseQuality.edgeNodeIntersections,
                nextQuality.edgeNodeIntersections,
                baseQuality.bundleNodeOverlaps,
                nextQuality.bundleNodeOverlaps,
                baseQuality.nodeOverlaps,
                nextQuality.nodeOverlaps,
                baseQuality.boundingBoxArea / 1e9,
                nextQuality.boundingBoxArea / 1e9);
            }
          }
        }
      }
    }

    // Final density-preserving pack. This is different from the earlier
    // density-balance experiment: it runs after bundle relocation/spacing
    // cleanup, expands the densest rendered clusters a little, then applies
    // a mild global pack. The goal is to remove empty whitespace without
    // making already-dense regions visually worse.
    {
      const bool densityPack =
        readBoolEnv("DJERD_DENSITY_PACK_FINAL", false);
      if (densityPack && nodes.size() > 2 && !clusterByModelIdFull.empty()) {
        auto rerouteDensityPack = [&]() {
          routes = crossAwareRouting
            ? routeAllEdgesCrossAware(nodes, edges, attributes)
            : (straightLineMode
              ? (arguments.edgeRouting == "straight_smart"
                  && !isStraightLineRoutingMode(arguments.mode)
                ? routeAllEdgesStraightSmart(nodes, edges, attributes)
                : routeAllEdgesStraight(edges, attributes))
              : routeAllEdges(nodes, edges, attributes, true));
        };

        auto measureDensityPackQuality = [&]() {
          std::vector<std::vector<std::string>> ignoredIdsByEdge;
          std::size_t rawCrossings = 0;
          (void)detectRouteCrossings(edges, routes, ignoredIdsByEdge, rawCrossings);
          LayoutQualityMetrics qm =
            measureLayoutQuality(nodes, edges, routes, attributes, &metadata.leafBundles);
          qm.edgeCrossings = rawCrossings;
          if (!applyRenderedCarrierMetricsIfRequested(
              nodes,
              edges,
              routes,
              attributes,
              clusterByModelIdFull,
              metadata,
              qm,
              rawCrossings,
              true,
              false)) {
            qm.visualCrossings =
              qm.edgeCrossings
              + qm.edgeNodeIntersections
              + qm.nodeOverlaps
              + qm.bundleEdgeIntersections
              + qm.bundleNodeOverlaps;
          }
          return qm;
        };

        const LayoutQualityMetrics baseQuality = measureDensityPackQuality();
        const Rect baseBounds = graphNodeBounds(nodes, attributes);
        const double baseArea = rectWidth(baseBounds) * rectHeight(baseBounds);
        if (baseArea > 1.0) {
          const double cellSize =
            readDoubleEnv("DJERD_DENSITY_PACK_CELL", 1600.0, 400.0, 8000.0);
          const RenderedDensityMetrics baseDensity =
            measureRenderedDensity(nodes, attributes, metadata.leafBundles, cellSize);
          const std::unordered_set<std::string> absorbed =
            absorbedLeafBundleIds(metadata.leafBundles);

          std::unordered_map<std::string, std::vector<std::size_t>> clusterMembers;
          for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (absorbed.count(nodes[i].modelId)) continue;
            auto cIt = clusterByModelIdFull.find(nodes[i].modelId);
            if (cIt == clusterByModelIdFull.end() || cIt->second.empty()) continue;
            clusterMembers[cIt->second].push_back(i);
          }

          struct DensityPackCluster {
            std::string id;
            std::vector<std::size_t> members;
            std::size_t spacingPairs = 0;
            double fill = 0.0;
            double area = 0.0;
          };

          std::vector<DensityPackCluster> denseClusters;
          const std::size_t minClusterSize = static_cast<std::size_t>(
            readDoubleEnv("DJERD_DENSITY_PACK_MIN_CLUSTER", 3.0, 2.0, 100.0));
          const double minFill =
            readDoubleEnv("DJERD_DENSITY_PACK_MIN_FILL", 0.58, 0.05, 4.0);
          for (const auto& kv : clusterMembers) {
            const std::vector<std::size_t>& members = kv.second;
            if (members.size() < minClusterSize) continue;
            std::vector<Rect> rects;
            rects.reserve(members.size());
            Rect clusterRect;
            bool initialized = false;
            double rectAreaSum = 0.0;
            for (std::size_t idx : members) {
              const Rect rect = expandedNodeRectAt(
                nodes[idx],
                attributes,
                sanitizeNodeCenterX(nodes[idx], attributes),
                sanitizeNodeCenterY(nodes[idx], attributes));
              rects.push_back(rect);
              rectAreaSum += std::max(1.0, rectWidth(rect) * rectHeight(rect));
              if (!initialized) {
                clusterRect = rect;
                initialized = true;
              } else {
                clusterRect.left = std::min(clusterRect.left, rect.left);
                clusterRect.right = std::max(clusterRect.right, rect.right);
                clusterRect.top = std::min(clusterRect.top, rect.top);
                clusterRect.bottom = std::max(clusterRect.bottom, rect.bottom);
              }
            }
            if (!initialized) continue;
            std::sort(rects.begin(), rects.end(),
              [](const Rect& left, const Rect& right) {
                return left.left < right.left;
              });
            std::size_t spacingPairs = 0;
            for (std::size_t i = 0; i < rects.size(); ++i) {
              for (std::size_t j = i + 1; j < rects.size(); ++j) {
                if (rects[j].left >= rects[i].right) break;
                if (rectsOverlap(rects[i], rects[j])) ++spacingPairs;
              }
            }
            const double clusterArea =
              std::max(1.0, rectWidth(clusterRect) * rectHeight(clusterRect));
            const double fill = rectAreaSum / clusterArea;
            if (spacingPairs == 0 && fill < minFill) continue;
            denseClusters.push_back({kv.first, members, spacingPairs, fill, clusterArea});
          }

          std::sort(denseClusters.begin(), denseClusters.end(),
            [](const DensityPackCluster& left, const DensityPackCluster& right) {
              if (left.spacingPairs != right.spacingPairs) {
                return left.spacingPairs > right.spacingPairs;
              }
              if (std::abs(left.fill - right.fill) > 1e-6) {
                return left.fill > right.fill;
              }
              if (left.members.size() != right.members.size()) {
                return left.members.size() > right.members.size();
              }
              return left.id < right.id;
            });

          std::vector<double> packScales{0.94, 0.90, 0.86, 0.82, 0.78, 0.72};
          const char* scalesEnv = std::getenv("DJERD_DENSITY_PACK_SCALES");
          if (scalesEnv && std::strlen(scalesEnv) > 0) {
            std::vector<double> parsed;
            std::stringstream ss(scalesEnv);
            std::string part;
            while (std::getline(ss, part, ',')) {
              try {
                const double scale = std::stod(part);
                if (scale > 0.2 && scale < 1.0) parsed.push_back(scale);
              } catch (const std::exception&) {
              }
            }
            if (!parsed.empty()) packScales = std::move(parsed);
          }

          const std::size_t topClusters = static_cast<std::size_t>(
            readDoubleEnv("DJERD_DENSITY_PACK_TOP", 0.0, 0.0, 256.0));
          const double expandScale =
            readDoubleEnv("DJERD_DENSITY_PACK_EXPAND_SCALE", 1.08, 1.0, 1.5);
          const double minGain =
            readDoubleEnv("DJERD_DENSITY_PACK_MIN_GAIN", 0.06, 0.0, 0.9);
          const std::size_t visualSlack = static_cast<std::size_t>(
            readDoubleEnv("DJERD_DENSITY_PACK_VISUAL_SLACK", 180.0, 0.0, 100000.0));
          const std::size_t edgeNodeSlack = static_cast<std::size_t>(
            readDoubleEnv("DJERD_DENSITY_PACK_EDGE_NODE_SLACK", 160.0, 0.0, 100000.0));
          const std::size_t spacingSlack = static_cast<std::size_t>(
            readDoubleEnv("DJERD_DENSITY_PACK_SPACING_SLACK", 40.0, 0.0, 100000.0));
          const std::size_t bundleNodeSlack = static_cast<std::size_t>(
            readDoubleEnv("DJERD_DENSITY_PACK_BUNDLE_NODE_SLACK", 0.0, 0.0, 100000.0));
          const std::size_t nodeOverlapSlack = static_cast<std::size_t>(
            readDoubleEnv("DJERD_DENSITY_PACK_NODE_OVERLAP_SLACK", 0.0, 0.0, 100000.0));
          const double p90Slack =
            readDoubleEnv("DJERD_DENSITY_PACK_P90_SLACK", 1.0, 0.0, 1000.0);
          const double maxSlack =
            readDoubleEnv("DJERD_DENSITY_PACK_MAX_SLACK", 2.0, 0.0, 1000.0);

          std::vector<std::pair<double, double>> originalPositions;
          originalPositions.reserve(nodes.size());
          for (const NodeRecord& node : nodes) {
            originalPositions.push_back({
              attributes.x(node.handle),
              attributes.y(node.handle),
            });
          }
          const auto originalRoutes = routes;
          const auto originalBundles = metadata.leafBundles;

          std::unordered_map<std::string, std::string> bundlePackKeyByModelId;
          std::unordered_map<std::string, std::vector<std::size_t>> packGroupBundleMap;
          for (std::size_t bi = 0; bi < metadata.leafBundles.size(); ++bi) {
            const LeafBundleRecord& bundle = metadata.leafBundles[bi];
            const std::string key = "B:" + std::to_string(bi);
            bundlePackKeyByModelId[bundle.parentModelId] = key;
            packGroupBundleMap[key].push_back(bi);
            for (const std::string& leaf : bundle.leafModelIds) {
              bundlePackKeyByModelId[leaf] = key;
            }
            for (const std::string& root : bundle.sharedRootModelIds) {
              bundlePackKeyByModelId[root] = key;
            }
          }
          std::unordered_map<std::string, std::vector<std::size_t>> packGroupMap;
          for (std::size_t i = 0; i < nodes.size(); ++i) {
            std::string key;
            auto bIt = bundlePackKeyByModelId.find(nodes[i].modelId);
            if (bIt != bundlePackKeyByModelId.end()) {
              key = bIt->second;
            } else {
              auto cIt = clusterByModelIdFull.find(nodes[i].modelId);
              if (cIt != clusterByModelIdFull.end() && !cIt->second.empty()) {
                key = "C:" + cIt->second;
              } else {
                key = "N:" + std::to_string(i);
              }
            }
            packGroupMap[key].push_back(i);
          }
          std::vector<std::vector<std::size_t>> packGroups;
          std::vector<std::vector<std::size_t>> packGroupBundles;
          packGroups.reserve(packGroupMap.size());
          packGroupBundles.reserve(packGroupMap.size());
          for (auto& kv : packGroupMap) {
            packGroups.push_back(std::move(kv.second));
            auto bundleIt = packGroupBundleMap.find(kv.first);
            if (bundleIt != packGroupBundleMap.end()) {
              packGroupBundles.push_back(std::move(bundleIt->second));
            } else {
              packGroupBundles.push_back({});
            }
          }

          auto restoreDensityPack = [&]() {
            for (std::size_t i = 0; i < nodes.size(); ++i) {
              attributes.x(nodes[i].handle) = originalPositions[i].first;
              attributes.y(nodes[i].handle) = originalPositions[i].second;
            }
            routes = originalRoutes;
            metadata.leafBundles = originalBundles;
          };

          auto applyDenseExpansion = [&]() {
            const std::size_t limit = std::min(topClusters, denseClusters.size());
            for (std::size_t rank = 0; rank < limit; ++rank) {
              const DensityPackCluster& cluster = denseClusters[rank];
              double cx = 0.0;
              double cy = 0.0;
              for (std::size_t idx : cluster.members) {
                cx += sanitizeNodeCenterX(nodes[idx], attributes);
                cy += sanitizeNodeCenterY(nodes[idx], attributes);
              }
              cx /= static_cast<double>(cluster.members.size());
              cy /= static_cast<double>(cluster.members.size());
              for (std::size_t idx : cluster.members) {
                const NodeRecord& node = nodes[idx];
                const double x = sanitizeNodeCenterX(node, attributes);
                const double y = sanitizeNodeCenterY(node, attributes);
                attributes.x(node.handle) = cx + (x - cx) * expandScale;
                attributes.y(node.handle) = cy + (y - cy) * expandScale;
              }
            }
          };

          auto computePackGroupRects = [&]() {
            std::vector<Rect> rects;
            rects.reserve(packGroups.size());
            const double nodeMargin = visualNodeMargin();
            const double bundleMargin = leafBundleVisualMargin();
            for (std::size_t gi = 0; gi < packGroups.size(); ++gi) {
              Rect groupRect;
              bool initialized = false;
              auto includeRect = [&](const Rect& rect) {
                if (!initialized) {
                  groupRect = rect;
                  initialized = true;
                  return;
                }
                groupRect.left = std::min(groupRect.left, rect.left);
                groupRect.right = std::max(groupRect.right, rect.right);
                groupRect.top = std::min(groupRect.top, rect.top);
                groupRect.bottom = std::max(groupRect.bottom, rect.bottom);
              };
              for (std::size_t idx : packGroups[gi]) {
                includeRect(nodeRect(nodes[idx], attributes, nodeMargin));
              }
              if (gi < packGroupBundles.size()) {
                for (std::size_t bi : packGroupBundles[gi]) {
                  if (bi < metadata.leafBundles.size()) {
                    includeRect(renderedLeafBundleRect(
                      metadata.leafBundles[bi],
                      bundleMargin));
                  }
                }
              }
              if (!initialized) {
                groupRect = {0.0, 0.0, 0.0, 0.0};
              }
              rects.push_back(groupRect);
            }
            return rects;
          };

          auto compactEmptyBands = [&](bool xAxis, double keepGap) {
            struct Interval {
              double start = 0.0;
              double end = 0.0;
            };
            std::vector<Rect> rects = computePackGroupRects();
            std::vector<Interval> intervals;
            intervals.reserve(rects.size());
            for (const Rect& rect : rects) {
              const double start = xAxis ? rect.left : rect.top;
              const double end = xAxis ? rect.right : rect.bottom;
              if (!std::isfinite(start) || !std::isfinite(end) || end <= start) {
                continue;
              }
              intervals.push_back({start, end});
            }
            if (intervals.size() < 2) return;
            std::sort(intervals.begin(), intervals.end(),
              [](const Interval& left, const Interval& right) {
                if (std::abs(left.start - right.start) > 1e-6) {
                  return left.start < right.start;
                }
                return left.end < right.end;
              });
            std::vector<Interval> merged;
            merged.reserve(intervals.size());
            for (const Interval& interval : intervals) {
              if (merged.empty() || interval.start > merged.back().end) {
                merged.push_back(interval);
              } else {
                merged.back().end = std::max(merged.back().end, interval.end);
              }
            }
            if (merged.size() < 2) return;

            struct ShiftBand {
              double threshold = 0.0;
              double shift = 0.0;
            };
            std::vector<ShiftBand> shifts;
            double cumulative = 0.0;
            for (std::size_t i = 1; i < merged.size(); ++i) {
              const double gap = merged[i].start - merged[i - 1].end;
              if (gap <= keepGap) continue;
              cumulative += gap - keepGap;
              shifts.push_back({merged[i].start, cumulative});
            }
            if (shifts.empty()) return;

            for (std::size_t gi = 0; gi < packGroups.size() && gi < rects.size(); ++gi) {
              const double groupStart = xAxis ? rects[gi].left : rects[gi].top;
              double shift = 0.0;
              for (const ShiftBand& band : shifts) {
                if (groupStart >= band.threshold - 1e-6) {
                  shift = band.shift;
                } else {
                  break;
                }
              }
              if (shift <= 0.0) continue;
              const double dx = xAxis ? -shift : 0.0;
              const double dy = xAxis ? 0.0 : -shift;
              for (std::size_t idx : packGroups[gi]) {
                attributes.x(nodes[idx].handle) += dx;
                attributes.y(nodes[idx].handle) += dy;
              }
            }
          };

          auto applyGlobalPack = [&](double packScale) {
            const double keepGap = readDoubleEnv(
              "DJERD_DENSITY_PACK_EMPTY_BAND_KEEP",
              720.0,
              80.0,
              20000.0) * packScale;
            compactEmptyBands(true, keepGap);
            recomputeLeafBundleBboxesFromNodes(metadata.leafBundles, nodes, attributes);
            compactEmptyBands(false, keepGap);
          };

          bool haveBest = false;
          double bestScale = 1.0;
          LayoutQualityMetrics bestQuality = baseQuality;
          RenderedDensityMetrics bestDensity = baseDensity;
          std::vector<std::pair<double, double>> bestPositions;
          std::vector<std::vector<RoutePoint>> bestRoutes;
          std::vector<LeafBundleRecord> bestBundles;

          for (double packScale : packScales) {
            restoreDensityPack();
            applyDenseExpansion();
            applyGlobalPack(packScale);
            recomputeLeafBundleBboxesFromNodes(metadata.leafBundles, nodes, attributes);
            if (readBoolEnv("DJERD_DENSITY_PACK_CLEANUP", false)) {
              (void)clearLeafBundleNodeMargins(
                metadata.leafBundles,
                nodes,
                attributes,
                false);
              recomputeLeafBundleBboxesFromNodes(metadata.leafBundles, nodes, attributes);
              (void)clearNodeSpacingOverlaps(metadata.leafBundles, nodes, attributes);
              recomputeLeafBundleBboxesFromNodes(metadata.leafBundles, nodes, attributes);
            }
            rerouteDensityPack();
            const LayoutQualityMetrics nextQuality = measureDensityPackQuality();
            const RenderedDensityMetrics nextDensity =
              measureRenderedDensity(nodes, attributes, metadata.leafBundles, cellSize);

            const bool areaOk =
              nextQuality.boundingBoxArea < baseQuality.boundingBoxArea * (1.0 - minGain);
            const bool visualOk =
              nextQuality.visualCrossings <= baseQuality.visualCrossings + visualSlack;
            const bool edgeNodeOk =
              nextQuality.edgeNodeIntersections
                <= baseQuality.edgeNodeIntersections + edgeNodeSlack;
            const bool nodeOverlapOk =
              nextQuality.nodeOverlaps <= baseQuality.nodeOverlaps + nodeOverlapSlack;
            const bool bundleNodeOk =
              nextQuality.bundleNodeOverlaps
                <= baseQuality.bundleNodeOverlaps + bundleNodeSlack;
            const bool spacingOk =
              nextQuality.nodeSpacingOverlaps
                <= baseQuality.nodeSpacingOverlaps + spacingSlack;
            const bool densityOk =
              nextDensity.p90 <= baseDensity.p90 + p90Slack
              && nextDensity.maxCell <= baseDensity.maxCell + maxSlack;

            if (readBoolEnv("DJERD_DENSITY_PACK_CANDIDATE_LOG", false)) {
              std::fprintf(stderr,
                "[density-pack-final:candidate] scale=%.3f "
                "bbox=%.2fB visual=%zu edgeNode=%zu bundleNode=%zu "
                "nodeOverlaps=%zu spacing=%zu p90=%.1f max=%.1f "
                "ok={area:%d visual:%d edgeNode:%d node:%d bundle:%d spacing:%d density:%d}.\n",
                packScale,
                nextQuality.boundingBoxArea / 1e9,
                nextQuality.visualCrossings,
                nextQuality.edgeNodeIntersections,
                nextQuality.bundleNodeOverlaps,
                nextQuality.nodeOverlaps,
                nextQuality.nodeSpacingOverlaps,
                nextDensity.p90,
                nextDensity.maxCell,
                areaOk ? 1 : 0,
                visualOk ? 1 : 0,
                edgeNodeOk ? 1 : 0,
                nodeOverlapOk ? 1 : 0,
                bundleNodeOk ? 1 : 0,
                spacingOk ? 1 : 0,
                densityOk ? 1 : 0);
            }

            if (!areaOk || !visualOk || !edgeNodeOk || !nodeOverlapOk
                || !bundleNodeOk || !spacingOk || !densityOk) {
              continue;
            }
            if (!haveBest
                || nextQuality.boundingBoxArea < bestQuality.boundingBoxArea) {
              haveBest = true;
              bestScale = packScale;
              bestQuality = nextQuality;
              bestDensity = nextDensity;
              bestPositions.clear();
              bestPositions.reserve(nodes.size());
              for (const NodeRecord& node : nodes) {
                bestPositions.push_back({
                  attributes.x(node.handle),
                  attributes.y(node.handle),
                });
              }
              bestRoutes = routes;
              bestBundles = metadata.leafBundles;
            }
          }

          restoreDensityPack();
          if (haveBest) {
            for (std::size_t i = 0; i < nodes.size() && i < bestPositions.size(); ++i) {
              attributes.x(nodes[i].handle) = bestPositions[i].first;
              attributes.y(nodes[i].handle) = bestPositions[i].second;
            }
            routes = std::move(bestRoutes);
            metadata.leafBundles = std::move(bestBundles);
            std::fprintf(stderr,
              "[density-pack-final] accepted scale=%.3f dense=%zu/%zu "
              "bbox=%.2fB -> %.2fB visual=%zu -> %zu edgeNode=%zu -> %zu "
              "bundleNode=%zu -> %zu spacing=%zu -> %zu "
              "p90=%.1f -> %.1f max=%.1f -> %.1f empty=%.1f%% -> %.1f%%.\n",
              bestScale,
              std::min(topClusters, denseClusters.size()),
              denseClusters.size(),
              baseQuality.boundingBoxArea / 1e9,
              bestQuality.boundingBoxArea / 1e9,
              baseQuality.visualCrossings,
              bestQuality.visualCrossings,
              baseQuality.edgeNodeIntersections,
              bestQuality.edgeNodeIntersections,
              baseQuality.bundleNodeOverlaps,
              bestQuality.bundleNodeOverlaps,
              baseQuality.nodeSpacingOverlaps,
              bestQuality.nodeSpacingOverlaps,
              baseDensity.p90,
              bestDensity.p90,
              baseDensity.maxCell,
              bestDensity.maxCell,
              baseDensity.emptyRatio * 100.0,
              bestDensity.emptyRatio * 100.0);
          } else if (readBoolEnv("DJERD_DENSITY_PACK_LOG", true)) {
            std::fprintf(stderr,
              "[density-pack-final] no candidate accepted; dense=%zu "
              "bbox=%.2fB visual=%zu edgeNode=%zu bundleNode=%zu spacing=%zu "
              "p90=%.1f max=%.1f empty=%.1f%%.\n",
              denseClusters.size(),
              baseQuality.boundingBoxArea / 1e9,
              baseQuality.visualCrossings,
              baseQuality.edgeNodeIntersections,
              baseQuality.bundleNodeOverlaps,
              baseQuality.nodeSpacingOverlaps,
              baseDensity.p90,
              baseDensity.maxCell,
              baseDensity.emptyRatio * 100.0);
          }
        }
      }
    }

    // Optional final route-only detour. The earlier edge-detour runs before
    // several node-moving visual passes; this one runs after route-sync and
    // final bundle bbox recomputation, so it scores exactly the geometry that
    // will be emitted.
    {
      const char* finalDetourEnv = std::getenv("DJERD_EDGE_DETOUR_FINAL");
      const bool finalDetour =
        finalDetourEnv && std::strcmp(finalDetourEnv, "0") != 0;
      if (finalDetour) {
        const char* passesEnv = std::getenv("DJERD_EDGE_DETOUR_FINAL_PASSES");
        const int passes = passesEnv ? std::max(1, std::atoi(passesEnv)) : 1;
        const char* crossEnv = std::getenv("DJERD_EDGE_DETOUR_FINAL_CROSS_WEIGHT");
        const char* baseCrossEnv = std::getenv("DJERD_EDGE_DETOUR_CROSS_WEIGHT");
        const double crossWeight = crossEnv
          ? std::max(0.0, std::atof(crossEnv))
          : (baseCrossEnv ? std::max(0.0, std::atof(baseCrossEnv)) : 0.5);
        const char* lenEnv = std::getenv("DJERD_EDGE_DETOUR_FINAL_LENGTH_WEIGHT");
        const double lengthWeight = lenEnv ? std::max(0.0, std::atof(lenEnv)) : 0.0;
        const char* clearanceEnv = std::getenv("DJERD_EDGE_DETOUR_FINAL_CLEARANCE");
        const double clearance = clearanceEnv ? std::max(0.0, std::atof(clearanceEnv)) : 28.0;

        std::unordered_map<std::string, std::size_t> idToIdxFD;
        idToIdxFD.reserve(nodes.size());
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          idToIdxFD[nodes[i].modelId] = i;
        }
        std::vector<std::pair<std::size_t, std::size_t>> edgePairsFD(edges.size());
        for (std::size_t e = 0; e < edges.size(); ++e) {
          auto sIt = idToIdxFD.find(edges[e].sourceModelId);
          auto tIt = idToIdxFD.find(edges[e].targetModelId);
          edgePairsFD[e] = {
            sIt == idToIdxFD.end() ? std::numeric_limits<std::size_t>::max() : sIt->second,
            tIt == idToIdxFD.end() ? std::numeric_limits<std::size_t>::max() : tIt->second,
          };
        }

        const double kFinalDetourMargin = visualNodeMargin();
        const double kFinalDetourBundleMargin = leafBundleVisualMargin();
        std::vector<Rect> nodeRectsFD(nodes.size());
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          nodeRectsFD[i] = nodeRect(nodes[i], attributes, kFinalDetourMargin);
        }

        std::vector<Rect> bundleRectsFD;
        std::vector<std::unordered_set<std::size_t>> bundleExemptFD;
        bundleRectsFD.reserve(metadata.leafBundles.size());
        bundleExemptFD.reserve(metadata.leafBundles.size());
        for (const LeafBundleRecord& bundle : metadata.leafBundles) {
          bundleRectsFD.push_back(renderedLeafBundleRect(bundle, kFinalDetourBundleMargin));
          std::unordered_set<std::size_t> exempt;
          auto pIt = idToIdxFD.find(bundle.parentModelId);
          if (pIt != idToIdxFD.end()) exempt.insert(pIt->second);
          for (const std::string& leaf : bundle.leafModelIds) {
            auto lIt = idToIdxFD.find(leaf);
            if (lIt != idToIdxFD.end()) exempt.insert(lIt->second);
          }
          bundleExemptFD.push_back(std::move(exempt));
        }

        auto segmentCrossCountFD = [&](std::size_t edgeIndex,
                                       const RoutePoint& p,
                                       const RoutePoint& q) {
          std::size_t total = 0;
          if (edgeIndex >= edges.size()) return total;
          for (std::size_t other = 0; other < routes.size() && other < edges.size(); ++other) {
            if (other == edgeIndex) continue;
            if (sharesEndpoint(edges[edgeIndex], edges[other])) continue;
            const auto& otherRoute = routes[other];
            if (otherRoute.size() < 2) continue;
            for (std::size_t oi = 1; oi < otherRoute.size(); ++oi) {
              RoutePoint isect;
              if (properSegmentIntersection(
                  p, q, otherRoute[oi - 1], otherRoute[oi], isect)) {
                ++total;
              }
            }
          }
          return total;
        };
        auto segmentLengthFD = [](const RoutePoint& p, const RoutePoint& q) {
          const double dx = q.x - p.x;
          const double dy = q.y - p.y;
          return std::sqrt(dx * dx + dy * dy);
        };
        auto blockerT = [](const RoutePoint& a, const RoutePoint& b, const Rect& r) {
          const double cx = (r.left + r.right) * 0.5;
          const double cy = (r.top + r.bottom) * 0.5;
          const double dx = b.x - a.x;
          const double dy = b.y - a.y;
          const double len2 = dx * dx + dy * dy;
          if (len2 < 1e-6) return -1.0;
          return ((cx - a.x) * dx + (cy - a.y) * dy) / len2;
        };
        auto countHitsFD = [&](std::size_t edgeIndex,
                               const RoutePoint& p,
                               const RoutePoint& q) {
          int hits = 0;
          if (edgeIndex >= edgePairsFD.size()) return hits;
          const auto [srcIdx, tgtIdx] = edgePairsFD[edgeIndex];
          for (std::size_t i = 0; i < nodeRectsFD.size(); ++i) {
            if (i == srcIdx || i == tgtIdx) continue;
            if (segmentIntersectsRect(p, q, nodeRectsFD[i])) ++hits;
          }
          for (std::size_t bi = 0; bi < bundleRectsFD.size(); ++bi) {
            if (bundleExemptFD[bi].count(srcIdx) || bundleExemptFD[bi].count(tgtIdx)) {
              continue;
            }
            if (segmentIntersectsRect(p, q, bundleRectsFD[bi])) ++hits;
          }
          return hits;
        };

        std::size_t totalWaypoints = 0;
        std::size_t totalEdges = 0;
        std::size_t rejected = 0;
        for (int pass = 0; pass < passes; ++pass) {
          std::size_t passWaypoints = 0;
          for (std::size_t e = 0; e < routes.size() && e < edges.size(); ++e) {
            auto& route = routes[e];
            if (route.size() < 2) continue;
            const auto [srcIdx, tgtIdx] = edgePairsFD[e];
            if (srcIdx == std::numeric_limits<std::size_t>::max()
                || tgtIdx == std::numeric_limits<std::size_t>::max()) continue;
            bool changed = false;
            std::vector<RoutePoint> nextRoute;
            nextRoute.reserve(route.size() * 2);
            nextRoute.push_back(route.front());
            for (std::size_t si = 1; si < route.size(); ++si) {
              const RoutePoint a = route[si - 1];
              const RoutePoint b = route[si];
              struct FDBlocker { double t; Rect rect; };
              std::vector<FDBlocker> blockers;
              for (std::size_t ni = 0; ni < nodeRectsFD.size(); ++ni) {
                if (ni == srcIdx || ni == tgtIdx) continue;
                if (!segmentIntersectsRect(a, b, nodeRectsFD[ni])) continue;
                const double t = blockerT(a, b, nodeRectsFD[ni]);
                if (t > 0.0 && t < 1.0) blockers.push_back({t, nodeRectsFD[ni]});
              }
              for (std::size_t bi = 0; bi < bundleRectsFD.size(); ++bi) {
                if (bundleExemptFD[bi].count(srcIdx) || bundleExemptFD[bi].count(tgtIdx)) {
                  continue;
                }
                if (!segmentIntersectsRect(a, b, bundleRectsFD[bi])) continue;
                const double t = blockerT(a, b, bundleRectsFD[bi]);
                if (t > 0.0 && t < 1.0) blockers.push_back({t, bundleRectsFD[bi]});
              }
              std::sort(blockers.begin(), blockers.end(),
                [](const FDBlocker& l, const FDBlocker& r) { return l.t < r.t; });

              for (const FDBlocker& bl : blockers) {
                const RoutePoint prev = nextRoute.back();
                const int hitsBaseline = countHitsFD(e, prev, b);
                if (hitsBaseline == 0) continue;
                const double dx = b.x - prev.x;
                const double dy = b.y - prev.y;
                const double len = std::sqrt(dx * dx + dy * dy);
                if (len < 1e-3) continue;
                const double cx = (bl.rect.left + bl.rect.right) * 0.5;
                const double cy = (bl.rect.top + bl.rect.bottom) * 0.5;
                const double t = ((cx - prev.x) * dx + (cy - prev.y) * dy) / (len * len);
                const double projX = prev.x + dx * std::clamp(t, 0.0, 1.0);
                const double projY = prev.y + dy * std::clamp(t, 0.0, 1.0);
                const double perpX = -dy / len;
                const double perpY = dx / len;
                const double offset = std::max(bl.rect.right - bl.rect.left,
                                               bl.rect.bottom - bl.rect.top) * 0.5 + clearance;
                const RoutePoint left{
                  std::round((projX + perpX * offset) * 100.0) / 100.0,
                  std::round((projY + perpY * offset) * 100.0) / 100.0,
                };
                const RoutePoint right{
                  std::round((projX - perpX * offset) * 100.0) / 100.0,
                  std::round((projY - perpY * offset) * 100.0) / 100.0,
                };
                const int hitsLeft = countHitsFD(e, prev, left) + countHitsFD(e, left, b);
                const int hitsRight = countHitsFD(e, prev, right) + countHitsFD(e, right, b);
                const std::size_t crossBaseline = segmentCrossCountFD(e, prev, b);
                const std::size_t crossLeft =
                  segmentCrossCountFD(e, prev, left) + segmentCrossCountFD(e, left, b);
                const std::size_t crossRight =
                  segmentCrossCountFD(e, prev, right) + segmentCrossCountFD(e, right, b);
                const double scoreBaseline =
                  static_cast<double>(hitsBaseline)
                  + crossWeight * static_cast<double>(crossBaseline)
                  + lengthWeight * segmentLengthFD(prev, b);
                const double scoreLeft =
                  static_cast<double>(hitsLeft)
                  + crossWeight * static_cast<double>(crossLeft)
                  + lengthWeight * (segmentLengthFD(prev, left) + segmentLengthFD(left, b));
                const double scoreRight =
                  static_cast<double>(hitsRight)
                  + crossWeight * static_cast<double>(crossRight)
                  + lengthWeight * (segmentLengthFD(prev, right) + segmentLengthFD(right, b));
                const RoutePoint* best = nullptr;
                double bestScore = scoreBaseline;
                if (hitsLeft < hitsBaseline && scoreLeft < bestScore) {
                  bestScore = scoreLeft;
                  best = &left;
                }
                if (hitsRight < hitsBaseline && scoreRight < bestScore) {
                  bestScore = scoreRight;
                  best = &right;
                }
                if (best != nullptr) {
                  nextRoute.push_back(*best);
                  changed = true;
                  ++passWaypoints;
                } else if (hitsLeft < hitsBaseline || hitsRight < hitsBaseline) {
                  ++rejected;
                }
              }
              nextRoute.push_back(b);
            }
            if (changed) {
              route = compressRoutePoints(std::move(nextRoute));
              ++totalEdges;
            }
          }
          totalWaypoints += passWaypoints;
          if (passWaypoints == 0) break;
        }
        std::fprintf(stderr,
          "[edge-detour-final] %zu waypoints across %zu edge updates "
          "(crossWeight=%.3f, lengthWeight=%.5f, clearance=%.1f, rejected=%zu).\n",
          totalWaypoints, totalEdges, crossWeight, lengthWeight, clearance, rejected);
      }
    }

    {
      const char* finalXdEnv = std::getenv("DJERD_XINGS_DETOUR_FINAL");
      const bool finalXd =
        finalXdEnv && std::strcmp(finalXdEnv, "0") != 0;
      if (finalXd) {
        runXingsDetour();
      }
    }

    // === DJERD_L_BEND_REROUTE=1 ===
    // Route-only crossing polish for long straight-ish offenders. Periphery
    // routing helps edges that need to leave the dense center, but a smaller
    // class only needs one orthogonal bend between the same endpoints. For the
    // worst crossing edges, try straight / horizontal-then-vertical /
    // vertical-then-horizontal and accept only candidates that reduce this
    // edge's crossings without increasing the global edge-edge total.
    {
      const char* lBendEnv = std::getenv("DJERD_L_BEND_REROUTE");
      if (lBendEnv && std::strcmp(lBendEnv, "0") != 0
          && routes.size() == edges.size() && !nodes.empty()) {
        int topK = 120;
        if (const char* k = std::getenv("DJERD_L_BEND_REROUTE_TOPK")) {
          topK = std::max(1, std::atoi(k));
        }
        int minGain = 1;
        if (const char* g = std::getenv("DJERD_L_BEND_REROUTE_MIN_GAIN")) {
          minGain = std::max(0, std::atoi(g));
        }
        const double nodeWeight = readDoubleEnv(
          "DJERD_L_BEND_REROUTE_EDGE_NODE_WEIGHT", 1.0, 0.0, 1000.0);
        const double overlapWeight = readDoubleEnv(
          "DJERD_L_BEND_REROUTE_SEGMENT_OVERLAP_WEIGHT", 1.0, 0.0, 1000.0);
        const double overlapLengthWeight = readDoubleEnv(
          "DJERD_L_BEND_REROUTE_SEGMENT_OVERLAP_LENGTH_WEIGHT",
          0.0001, 0.0, 1.0);
        const double lengthWeight = readDoubleEnv(
          "DJERD_L_BEND_REROUTE_LENGTH_WEIGHT", 0.0, 0.0, 1000.0);
        const double nodeMargin = visualNodeMargin();

        auto normalizeRoute = [](std::vector<RoutePoint> route) {
          return compressRoutePoints(std::move(route));
        };
        auto polyCrossAB = [&](const std::vector<RoutePoint>& ra,
                               const std::vector<RoutePoint>& rb) -> bool {
          if (ra.size() < 2 || rb.size() < 2) return false;
          for (std::size_t li = 1; li < ra.size(); ++li) {
            for (std::size_t rj = 1; rj < rb.size(); ++rj) {
              RoutePoint isect;
              if (properSegmentIntersection(
                  ra[li - 1], ra[li], rb[rj - 1], rb[rj], isect)) {
                return true;
              }
            }
          }
          return false;
        };
        auto routeCrossCount = [&](std::size_t e,
                                   const std::vector<RoutePoint>& cand) -> std::size_t {
          std::size_t t = 0;
          for (std::size_t e2 = 0; e2 < edges.size(); ++e2) {
            if (e2 == e) continue;
            if (sharesEndpoint(edges[e], edges[e2])) continue;
            if (polyCrossAB(cand, routes[e2])) ++t;
          }
          return t;
        };
        auto totalCross = [&]() -> std::size_t {
          std::size_t t = 0;
          for (std::size_t i = 0; i < edges.size(); ++i) {
            for (std::size_t j = i + 1; j < edges.size(); ++j) {
              if (sharesEndpoint(edges[i], edges[j])) continue;
              if (polyCrossAB(routes[i], routes[j])) ++t;
            }
          }
          return t;
        };

        std::vector<std::pair<std::size_t, std::size_t>> ranked;
        for (std::size_t e = 0; e < edges.size(); ++e) {
          if (routes[e].size() < 2) continue;
          const std::size_t c = routeCrossCount(e, routes[e]);
          if (c > 0) ranked.emplace_back(c, e);
        }
        std::sort(ranked.rbegin(), ranked.rend());

        const std::size_t before = totalCross();
        const std::vector<std::vector<RoutePoint>> savedRoutes = routes;
        RouteOccupancy occupancy;
        if (overlapWeight > 0.0) {
          for (std::size_t e = 0; e < routes.size(); ++e) {
            if (routes[e].size() < 2) continue;
            const LineIntent line = makeLineIntent(edges[e], e, attributes);
            recordRouteOccupancy(routes[e], line, occupancy);
          }
        }

        std::size_t rerouted = 0;
        std::size_t rejected = 0;
        const std::size_t limit =
          std::min(static_cast<std::size_t>(topK), ranked.size());
        for (std::size_t r = 0; r < limit; ++r) {
          const std::size_t e = ranked[r].second;
          if (routes[e].size() < 2) continue;
          const LineIntent line = makeLineIntent(edges[e], e, attributes);
          const std::vector<NodeObstacle> obstacles = makeNodeObstacles(
            nodes, attributes, nodeMargin, line.sourceHandle, line.targetHandle);
          if (overlapWeight > 0.0) {
            removeRouteOccupancy(routes[e], line, occupancy);
          }
          auto nodeHits = [&](const std::vector<RoutePoint>& cand) -> std::size_t {
            if (cand.size() < 2) return 0;
            std::size_t hits = 0;
            for (std::size_t i = 1; i < cand.size(); ++i) {
              for (const NodeObstacle& obstacle : obstacles) {
                if (segmentIntersectsRect(cand[i - 1], cand[i], obstacle.rect)) {
                  ++hits;
                }
              }
            }
            return hits;
          };
          auto overlapDebt = [&](const std::vector<RoutePoint>& cand) -> double {
            if (overlapWeight <= 0.0 || cand.size() < 2) return 0.0;
            return routeAxisOverlapDebt(cand, &occupancy, overlapLengthWeight);
          };
          auto score = [&](std::size_t crossings,
                           const std::vector<RoutePoint>& cand) -> double {
            return static_cast<double>(crossings)
              + nodeWeight * static_cast<double>(nodeHits(cand))
              + overlapWeight * overlapDebt(cand)
              + lengthWeight * routeLength(cand);
          };

          const RoutePoint pA = routes[e].front();
          const RoutePoint pB = routes[e].back();
          const std::vector<std::vector<RoutePoint>> candidates = {
            normalizeRoute({pA, pB}),
            normalizeRoute({pA, {pB.x, pA.y}, pB}),
            normalizeRoute({pA, {pA.x, pB.y}, pB}),
          };
          const std::size_t currentCross = routeCrossCount(e, routes[e]);
          const double currentScore = score(currentCross, routes[e]);
          double bestScore = currentScore;
          std::size_t bestCross = currentCross;
          int bestIndex = -1;
          for (int ci = 0; ci < static_cast<int>(candidates.size()); ++ci) {
            if (candidates[ci].size() < 2) continue;
            const std::size_t c = routeCrossCount(e, candidates[ci]);
            if (c + static_cast<std::size_t>(minGain) > currentCross) {
              continue;
            }
            const double s = score(c, candidates[ci]);
            if (s < bestScore
                || (std::abs(s - bestScore) < 1e-9 && c < bestCross)) {
              bestScore = s;
              bestCross = c;
              bestIndex = ci;
            }
          }
          if (bestIndex >= 0) {
            routes[e] = candidates[bestIndex];
            ++rerouted;
          } else {
            ++rejected;
          }
          if (overlapWeight > 0.0) {
            recordRouteOccupancy(routes[e], line, occupancy);
          }
        }

        const std::size_t after = totalCross();
        if (after > before) {
          routes = savedRoutes;
          std::fprintf(stderr,
            "[l-bend-reroute] reverted: %zu candidates, total cross %zu -> %zu (worse).\n",
            limit, before, after);
        } else {
          std::fprintf(stderr,
            "[l-bend-reroute] %zu/%zu edges rerouted, total cross %zu -> %zu "
            "(rejected=%zu, nodeWeight=%.3f, overlapWeight=%.3f).\n",
            rerouted, limit, before, after, rejected, nodeWeight, overlapWeight);
        }
      }
    }

    // === DJERD_PERIPHERY_REROUTE=1 (prototype) ===
    // For the worst high-crossing edges, try routing them around the layout
    // bbox periphery (top / bottom / left / right) instead of straight through
    // the dense centre. Keep the candidate that minimises THAT edge's polyline
    // crossings, if it beats the current route. Greedy worst-first with a
    // per-edge lane offset (so peripheral routes don't pile on one line) and a
    // global revert if total crossings don't improve. Default off.
    {
      const char* perEnv = std::getenv("DJERD_PERIPHERY_REROUTE");
      if (perEnv && std::strcmp(perEnv, "0") != 0
          && routes.size() == edges.size() && !nodes.empty()) {
        int topK = 20;
        if (const char* k = std::getenv("DJERD_PERIPHERY_TOPK")) {
          topK = std::max(1, std::atoi(k));
        }
        // Bound the outward lane offset. Each rerouted edge gets a lane that
        // pushes its peripheral arc further outside the node bbox; left
        // unbounded, N reroutes inflate the route bbox by ~N% of the layout
        // span (196 reroutes ⇒ route bbox ~29× the node bbox). The lane only
        // needs to separate parallel arcs, so cycling a small fixed number of
        // lanes keeps the center-avoidance (crossing) win while capping how far
        // the arcs extend. Default huge = effectively unbounded (byte-identical
        // to the original behaviour); set DJERD_PERIPHERY_MAX_LANES to cap.
        std::size_t maxLanes = 1000000;
        if (const char* ml = std::getenv("DJERD_PERIPHERY_MAX_LANES")) {
          const int v = std::atoi(ml);
          if (v > 0) maxLanes = static_cast<std::size_t>(v);
        }
        auto polyCrossAB = [&](const std::vector<RoutePoint>& ra,
                               const std::vector<RoutePoint>& rb) -> bool {
          if (ra.size() < 2 || rb.size() < 2) return false;
          for (std::size_t li = 1; li < ra.size(); ++li) {
            for (std::size_t rj = 1; rj < rb.size(); ++rj) {
              RoutePoint isect;
              if (properSegmentIntersection(ra[li - 1], ra[li],
                                            rb[rj - 1], rb[rj], isect)) {
                return true;
              }
            }
          }
          return false;
        };
        auto routeCrossCount = [&](std::size_t e,
                                   const std::vector<RoutePoint>& cand) -> std::size_t {
          std::size_t t = 0;
          for (std::size_t e2 = 0; e2 < edges.size(); ++e2) {
            if (e2 == e) continue;
            if (sharesEndpoint(edges[e], edges[e2])) continue;
            if (polyCrossAB(cand, routes[e2])) ++t;
          }
          return t;
        };
        auto totalCross = [&]() -> std::size_t {
          std::size_t t = 0;
          for (std::size_t i = 0; i < edges.size(); ++i) {
            for (std::size_t j = i + 1; j < edges.size(); ++j) {
              if (sharesEndpoint(edges[i], edges[j])) continue;
              if (polyCrossAB(routes[i], routes[j])) ++t;
            }
          }
          return t;
        };

        double X0 = std::numeric_limits<double>::infinity();
        double X1 = -X0, Y0 = X0, Y1 = -X0;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          const double x = attributes.x(nodes[i].handle);
          const double y = attributes.y(nodes[i].handle);
          X0 = std::min(X0, x); X1 = std::max(X1, x);
          Y0 = std::min(Y0, y); Y1 = std::max(Y1, y);
        }
        const double spanX = std::max(1.0, X1 - X0);
        const double spanY = std::max(1.0, Y1 - Y0);
        const double baseMargin = 0.04 * std::max(spanX, spanY);
        const double laneStep = 0.01 * std::max(spanX, spanY);
        // Edge-node-aware selection. A capped peripheral arc hugs the bbox, so
        // it can dodge centre crossings yet slice through node boxes near the
        // edge — trading edge-edge crossings for edge-node intersections. Score
        // each candidate by crossings + W·(node-box hits) and reroute only when
        // the COMBINED cost drops, so an arc that clips more boxes than the
        // crossings it removes is rejected in favour of staying put. W=1 weights
        // a box-clip like a crossing (the metric's own weighting). env-tunable;
        // W=0 reproduces the crossings-only behaviour.
        const double pEdgeNodeWeight = [] {
          const char* e = std::getenv("DJERD_PERIPHERY_EDGE_NODE_WEIGHT");
          return e ? std::atof(e) : 1.0;
        }();
        const double pSegmentOverlapWeight = readDoubleEnv(
          "DJERD_PERIPHERY_SEGMENT_OVERLAP_WEIGHT", 0.0, 0.0, 1000.0);
        const double pSegmentOverlapLengthWeight = readDoubleEnv(
          "DJERD_PERIPHERY_SEGMENT_OVERLAP_LENGTH_WEIGHT", 0.0, 0.0, 1.0);
        const double pNodeMargin = visualNodeMargin();

        std::vector<std::pair<std::size_t, std::size_t>> ranked;
        for (std::size_t e = 0; e < edges.size(); ++e) {
          if (routes[e].size() < 2) continue;
          const std::size_t c = routeCrossCount(e, routes[e]);
          if (c > 0) ranked.emplace_back(c, e);
        }
        std::sort(ranked.rbegin(), ranked.rend());

        const std::size_t before = totalCross();
        const std::vector<std::vector<RoutePoint>> savedRoutes = routes;
        std::size_t rerouted = 0, lane = 0;
        const std::size_t limit =
          std::min(static_cast<std::size_t>(topK), ranked.size());
        RouteOccupancy pOccupancy;
        if (pSegmentOverlapWeight > 0.0) {
          for (std::size_t e2 = 0; e2 < edges.size(); ++e2) {
            if (routes[e2].size() < 2) continue;
            const LineIntent occLine = makeLineIntent(edges[e2], e2, attributes);
            recordRouteOccupancy(routes[e2], occLine, pOccupancy);
          }
        }
        for (std::size_t r = 0; r < limit; ++r) {
          const std::size_t e = ranked[r].second;
          const RoutePoint pA = routes[e].front();
          const RoutePoint pB = routes[e].back();
          const std::size_t curC = routeCrossCount(e, routes[e]);
          // Node-box obstacles for this edge (excludes its own endpoints),
          // identical to the edgeNodeIntersections metric. Built once per edge,
          // reused for the current route and all four candidates.
          const LineIntent pLine = makeLineIntent(edges[e], e, attributes);
          const std::vector<NodeObstacle> pObs = makeNodeObstacles(
            nodes, attributes, pNodeMargin, pLine.sourceHandle, pLine.targetHandle);
          if (pSegmentOverlapWeight > 0.0) {
            removeRouteOccupancy(routes[e], pLine, pOccupancy);
          }
          auto nodeHits = [&](const std::vector<RoutePoint>& cand) -> std::size_t {
            if (cand.size() < 2) return 0;
            std::size_t h = 0;
            for (std::size_t li = 1; li < cand.size(); ++li)
              for (const NodeObstacle& ob : pObs)
                if (segmentIntersectsRect(cand[li - 1], cand[li], ob.rect)) ++h;
            return h;
          };
          auto segmentOverlapDebt =
            [&](const std::vector<RoutePoint>& cand) -> double {
              if (pSegmentOverlapWeight <= 0.0 || cand.size() < 2) return 0.0;
              return routeAxisOverlapDebt(cand, &pOccupancy, pSegmentOverlapLengthWeight);
            };
          const double m = baseMargin + laneStep * static_cast<double>(lane % maxLanes);
          const std::vector<std::vector<RoutePoint>> cands = {
            {pA, {pA.x, Y0 - m}, {pB.x, Y0 - m}, pB},
            {pA, {pA.x, Y1 + m}, {pB.x, Y1 + m}, pB},
            {pA, {X0 - m, pA.y}, {X0 - m, pB.y}, pB},
            {pA, {X1 + m, pA.y}, {X1 + m, pB.y}, pB},
          };
          // Only consider candidates that don't INCREASE this edge's crossings
          // (keeps the global edge-edge total from regressing / tripping the
          // revert below), then pick the lowest crossings + W·(box hits).
          const double curScore = static_cast<double>(curC)
            + pEdgeNodeWeight * static_cast<double>(nodeHits(routes[e]))
            + pSegmentOverlapWeight * segmentOverlapDebt(routes[e]);
          double bestScore = curScore;
          int bestI = -1;
          for (int ci = 0; ci < static_cast<int>(cands.size()); ++ci) {
            const std::size_t c = routeCrossCount(e, cands[ci]);
            if (c > curC) continue;
            const double sc = static_cast<double>(c)
              + pEdgeNodeWeight * static_cast<double>(nodeHits(cands[ci]))
              + pSegmentOverlapWeight * segmentOverlapDebt(cands[ci]);
            if (sc < bestScore) { bestScore = sc; bestI = ci; }
          }
          if (bestI >= 0) {
            routes[e] = cands[bestI];
            ++rerouted;
            ++lane;
          }
          if (pSegmentOverlapWeight > 0.0) {
            recordRouteOccupancy(routes[e], pLine, pOccupancy);
          }
        }
        const std::size_t after = totalCross();
        if (after > before) {
          routes = savedRoutes;
          std::fprintf(stderr,
            "[periphery-reroute] reverted: %zu candidates, total cross %zu -> %zu (worse).\n",
            limit, before, after);
        } else {
          std::fprintf(stderr,
            "[periphery-reroute] %zu/%zu edges rerouted, total cross %zu -> %zu.\n",
            rerouted, limit, before, after);
        }
      }
    }

    // Independent final retouch consumes the settled emitted layout, not the
    // intermediate state produced by this replay run. Restore both nodes and
    // routes immediately before retouch so earlier post-passes cannot shift
    // node coordinates away from the supplied route geometry.
    if (
        readBoolEnv("DJERD_RESTORE_LAYOUT_TSV_BEFORE_RETOUCH", false)
        && !arguments.positionsTsv.empty()
        && !arguments.routesTsv.empty()) {
      const std::size_t restoredNodes =
        applyPositionsTsvOverride(arguments.positionsTsv, nodes, attributes);
      const std::size_t restoredRoutes =
        applyRoutesTsvOverride(arguments.routesTsv, edges, routes);
      recomputeLeafBundleBboxesFromNodes(metadata.leafBundles, nodes, attributes);
      std::fprintf(stderr,
        "[retouch-input-restore] Restored %zu/%zu nodes and %zu/%zu routes "
        "from layout TSVs before diagonal retouch.\n",
        restoredNodes, nodes.size(), restoredRoutes, edges.size());
    }

    // Aggressively compressed layouts can leave a leaf bundle far from its
    // shared root, so the one visible synthetic connector crosses many table
    // boxes even though the bundle's internal leaf lines remain hidden. Pack
    // each bundle as one rigid rendered rectangle into a root-near empty slot.
    // Candidate generation uses only relative geometry and node dimensions;
    // the complete rendered-carrier metric decides whether the whole batch is
    // kept. Routes remain straight and the original state is restored on any
    // visual, overlap, or bbox regression.
    {
      const bool bundleConnectorPack = readBoolEnv(
        "DJERD_BUNDLE_CONNECTOR_PACK_FINAL", false);
      if (
          bundleConnectorPack
          && straightLineMode
          && !nodes.empty()
          && !metadata.leafBundles.empty()) {
        auto rerouteBundleConnectorPack = [&]() {
          routes = routeAllEdgesStraight(edges, attributes);
          recomputeLeafBundleBboxesFromNodes(
            metadata.leafBundles, nodes, attributes);
        };
        auto measureBundleConnectorPack = [&]() {
          std::vector<std::vector<std::string>> ignoredIdsByEdge;
          std::size_t rawCrossings = 0;
          (void)detectRouteCrossings(
            edges, routes, ignoredIdsByEdge, rawCrossings);
          LayoutQualityMetrics qm = measureLayoutQuality(
            nodes,
            edges,
            routes,
            attributes,
            &metadata.leafBundles,
            &metadata.clusterByModelId);
          qm.edgeCrossings = rawCrossings;
          if (!applyRenderedCarrierMetricsIfRequested(
              nodes,
              edges,
              routes,
              attributes,
              clusterByModelIdFull,
              metadata,
              qm,
              rawCrossings,
              true,
              false)) {
            qm.visualCrossings =
              qm.edgeCrossings
              + qm.edgeNodeIntersections
              + qm.nodeOverlaps
              + qm.bundleEdgeIntersections
              + qm.bundleNodeOverlaps;
          }
          return qm;
        };

        rerouteBundleConnectorPack();
        const LayoutQualityMetrics initialPackQuality =
          measureBundleConnectorPack();
        const std::vector<std::vector<RoutePoint>> savedPackRoutes = routes;
        const LayoutRunMetadata savedPackMetadata = metadata;
        std::vector<std::pair<double, double>> savedPackPositions;
        savedPackPositions.reserve(nodes.size());
        for (const NodeRecord& node : nodes) {
          savedPackPositions.emplace_back(
            attributes.x(node.handle), attributes.y(node.handle));
        }
        const Rect initialPackBounds = graphNodeBounds(nodes, attributes);
        const double initialPackArea =
          rectWidth(initialPackBounds) * rectHeight(initialPackBounds);

        std::unordered_map<std::string, std::size_t> packNodeIndex;
        packNodeIndex.reserve(nodes.size());
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          packNodeIndex[nodes[i].modelId] = i;
        }
        std::unordered_set<std::string> packAbsorbed;
        for (const LeafBundleRecord& bundle : metadata.leafBundles) {
          packAbsorbed.insert(bundle.parentModelId);
          for (const std::string& leaf : bundle.leafModelIds) {
            packAbsorbed.insert(leaf);
          }
        }

        struct BundlePackExternalNode {
          std::string modelId;
          Rect rect;
        };
        const double packNodeMargin = visualNodeMargin();
        const double packBundleMargin = readDoubleEnv(
          "DJERD_BUNDLE_CONNECTOR_PACK_MARGIN", 8.0, 0.0, 240.0);
        const double packClearance = readDoubleEnv(
          "DJERD_BUNDLE_CONNECTOR_PACK_CLEARANCE", 32.0, 0.0, 2000.0);
        const double packConnectorWeight = readDoubleEnv(
          "DJERD_BUNDLE_CONNECTOR_PACK_CONNECTOR_WEIGHT",
          1.0,
          0.0,
          1000.0);
        const double packNodeWeight = readDoubleEnv(
          "DJERD_BUNDLE_CONNECTOR_PACK_NODE_WEIGHT",
          1.0,
          0.0,
          1000.0);
        const double packBundleWeight = readDoubleEnv(
          "DJERD_BUNDLE_CONNECTOR_PACK_BUNDLE_WEIGHT",
          10.0,
          0.0,
          1000.0);
        const double packCarrierWeight = readDoubleEnv(
          "DJERD_BUNDLE_CONNECTOR_PACK_CARRIER_WEIGHT",
          0.25,
          0.0,
          1000.0);
        const int packDirections = static_cast<int>(readDoubleEnv(
          "DJERD_BUNDLE_CONNECTOR_PACK_DIRECTIONS", 32.0, 8.0, 96.0));
        const int packPasses = static_cast<int>(readDoubleEnv(
          "DJERD_BUNDLE_CONNECTOR_PACK_PASSES", 1.0, 1.0, 4.0));
        const double packBboxLimit = readDoubleEnv(
          "DJERD_BUNDLE_CONNECTOR_PACK_BBOX_LIMIT", 1.001, 1.0, 1.20);

        std::vector<BundlePackExternalNode> packExternalNodes;
        for (const NodeRecord& node : nodes) {
          if (packAbsorbed.count(node.modelId)) continue;
          packExternalNodes.push_back({
            node.modelId,
            nodeRect(node, attributes, packNodeMargin),
          });
        }
        std::vector<Rect> packBundleRects;
        packBundleRects.reserve(metadata.leafBundles.size());
        for (const LeafBundleRecord& bundle : metadata.leafBundles) {
          packBundleRects.push_back(
            renderedLeafBundleRect(bundle, packBundleMargin));
        }

        struct BundlePackCarrierPath {
          std::unordered_set<std::string> endpointModelIds;
          std::vector<RoutePoint> points;
        };
        std::unordered_map<std::string, std::size_t> packEdgeIndex;
        packEdgeIndex.reserve(edges.size());
        for (std::size_t i = 0; i < edges.size(); ++i) {
          packEdgeIndex[edges[i].edgeId] = i;
        }
        std::vector<BundlePackCarrierPath> packCarrierPaths;
        packCarrierPaths.reserve(metadata.renderedCarrierRoutes.size());
        for (const RenderedCarrierRouteRecord& carrier
             : metadata.renderedCarrierRoutes) {
          if (carrier.points.size() < 2) continue;
          BundlePackCarrierPath path;
          path.points = carrier.points;
          for (const std::string& edgeId : carrier.memberEdgeIds) {
            auto edgeIt = packEdgeIndex.find(edgeId);
            if (edgeIt == packEdgeIndex.end()) continue;
            path.endpointModelIds.insert(
              edges[edgeIt->second].sourceModelId);
            path.endpointModelIds.insert(
              edges[edgeIt->second].targetModelId);
          }
          packCarrierPaths.push_back(std::move(path));
        }

        struct BundlePackScore {
          std::size_t bundleBundle = 0;
          std::size_t bundleNode = 0;
          std::size_t carrierBundle = 0;
          std::size_t connectorCarrier = 0;
          std::size_t connectorNode = 0;
          double total = 0.0;
        };
        auto unmarginPackRect = [&](const Rect& rect) {
          Rect raw = rect;
          raw.left += packBundleMargin;
          raw.right -= packBundleMargin;
          raw.top += packBundleMargin;
          raw.bottom -= packBundleMargin;
          return raw;
        };
        auto scorePackedBundle = [&](std::size_t bundleIndex, const Rect& rect) {
          BundlePackScore score;
          if (bundleIndex >= metadata.leafBundles.size()) return score;
          for (const BundlePackExternalNode& node : packExternalNodes) {
            if (rectsOverlap(rect, node.rect)) ++score.bundleNode;
          }
          for (std::size_t other = 0;
               other < packBundleRects.size(); ++other) {
            if (
                other != bundleIndex
                && rectsOverlap(rect, packBundleRects[other])) {
              ++score.bundleBundle;
            }
          }

          const LeafBundleRecord& bundle = metadata.leafBundles[bundleIndex];
          const std::vector<std::string> rootIds =
            bundle.sharedRootModelIds.empty()
              ? std::vector<std::string>{bundle.parentModelId}
              : bundle.sharedRootModelIds;
          std::unordered_set<std::string> bundleEndpointIds;
          bundleEndpointIds.insert(bundle.parentModelId);
          for (const std::string& leaf : bundle.leafModelIds) {
            bundleEndpointIds.insert(leaf);
          }
          for (const std::string& rootId : rootIds) {
            bundleEndpointIds.insert(rootId);
          }
          auto carrierSharesBundleEndpoint = [&]
              (const BundlePackCarrierPath& path) {
            for (const std::string& modelId : bundleEndpointIds) {
              if (path.endpointModelIds.count(modelId)) return true;
            }
            return false;
          };
          for (const BundlePackCarrierPath& path : packCarrierPaths) {
            if (carrierSharesBundleEndpoint(path)) continue;
            bool hitsBundle = false;
            for (std::size_t pointIndex = 1;
                 pointIndex < path.points.size(); ++pointIndex) {
              if (segmentIntersectsRect(
                  path.points[pointIndex - 1],
                  path.points[pointIndex],
                  rect)) {
                hitsBundle = true;
                break;
              }
            }
            if (hitsBundle) ++score.carrierBundle;
          }
          const Rect connectorBundleRect = unmarginPackRect(rect);
          for (const std::string& rootId : rootIds) {
            auto rootIt = packNodeIndex.find(rootId);
            if (rootIt == packNodeIndex.end()) continue;
            const Rect rootRect = nodeRect(
              nodes[rootIt->second], attributes, 0.0);
            const RoutePoint bundlePort =
              straightPortOnRect(connectorBundleRect, rootRect);
            const RoutePoint rootPort =
              straightPortOnRect(rootRect, connectorBundleRect);
            for (const BundlePackExternalNode& node : packExternalNodes) {
              if (node.modelId == rootId) continue;
              if (segmentIntersectsRect(bundlePort, rootPort, node.rect)) {
                ++score.connectorNode;
              }
            }
            for (const BundlePackCarrierPath& path : packCarrierPaths) {
              if (carrierSharesBundleEndpoint(path)) continue;
              bool crossesCarrier = false;
              for (std::size_t pointIndex = 1;
                   pointIndex < path.points.size(); ++pointIndex) {
                RoutePoint intersection;
                if (properSegmentIntersection(
                    bundlePort,
                    rootPort,
                    path.points[pointIndex - 1],
                    path.points[pointIndex],
                    intersection)) {
                  crossesCarrier = true;
                  break;
                }
              }
              if (crossesCarrier) ++score.connectorCarrier;
            }
          }
          score.total =
            packConnectorWeight * static_cast<double>(score.connectorNode)
            + packNodeWeight * static_cast<double>(score.bundleNode)
            + packBundleWeight * static_cast<double>(score.bundleBundle)
            + packCarrierWeight
              * static_cast<double>(
                score.connectorCarrier + score.carrierBundle);
          return score;
        };

        auto refreshPackedBundleRects = [&]() {
          for (std::size_t i = 0;
               i < metadata.leafBundles.size(); ++i) {
            packBundleRects[i] = renderedLeafBundleRect(
              metadata.leafBundles[i], packBundleMargin);
          }
        };
        auto totalConnectorHits = [&]() {
          std::size_t total = 0;
          for (std::size_t i = 0; i < metadata.leafBundles.size(); ++i) {
            total += scorePackedBundle(i, packBundleRects[i]).connectorNode;
          }
          return total;
        };
        const std::size_t connectorHitsBefore = totalConnectorHits();
        std::size_t movedBundles = 0;
        constexpr double kBundlePackPi = 3.14159265358979323846;
        const std::vector<double> packRadiusFactors = {
          1.0, 1.25, 1.6, 2.2, 3.0,
        };

        for (int pass = 0; pass < packPasses; ++pass) {
          std::vector<std::pair<double, std::size_t>> rankedBundles;
          rankedBundles.reserve(metadata.leafBundles.size());
          for (std::size_t i = 0; i < metadata.leafBundles.size(); ++i) {
            rankedBundles.push_back({
              scorePackedBundle(i, packBundleRects[i]).total,
              i,
            });
          }
          std::sort(
            rankedBundles.begin(),
            rankedBundles.end(),
            [](const auto& left, const auto& right) {
              if (std::abs(left.first - right.first) > 1e-9) {
                return left.first > right.first;
              }
              return left.second < right.second;
            });

          std::size_t movedThisPass = 0;
          for (const auto& ranked : rankedBundles) {
            const std::size_t bundleIndex = ranked.second;
            if (bundleIndex >= metadata.leafBundles.size()) continue;
            const LeafBundleRecord& bundle = metadata.leafBundles[bundleIndex];
            const Rect currentRect = packBundleRects[bundleIndex];
            const double currentCenterX = rectCenterX(currentRect);
            const double currentCenterY = rectCenterY(currentRect);
            const double bundleWidth = rectWidth(currentRect);
            const double bundleHeight = rectHeight(currentRect);
            const BundlePackScore baseScore =
              scorePackedBundle(bundleIndex, currentRect);

            struct BundlePackAnchor {
              double height = 0.0;
              double width = 0.0;
              double x = 0.0;
              double y = 0.0;
            };
            std::vector<BundlePackAnchor> anchors;
            const std::vector<std::string> rootIds =
              bundle.sharedRootModelIds.empty()
                ? std::vector<std::string>{bundle.parentModelId}
                : bundle.sharedRootModelIds;
            double rootSumX = 0.0;
            double rootSumY = 0.0;
            double rootMaxWidth = 0.0;
            double rootMaxHeight = 0.0;
            for (const std::string& rootId : rootIds) {
              auto rootIt = packNodeIndex.find(rootId);
              if (rootIt == packNodeIndex.end()) continue;
              const NodeRecord& root = nodes[rootIt->second];
              const double rootWidth = sanitizeNodeWidth(root, attributes);
              const double rootHeight = sanitizeNodeHeight(root, attributes);
              const double rootX = sanitizeNodeCenterX(root, attributes);
              const double rootY = sanitizeNodeCenterY(root, attributes);
              anchors.push_back({rootHeight, rootWidth, rootX, rootY});
              rootSumX += rootX;
              rootSumY += rootY;
              rootMaxWidth = std::max(rootMaxWidth, rootWidth);
              rootMaxHeight = std::max(rootMaxHeight, rootHeight);
            }
            if (anchors.empty()) continue;
            anchors.push_back({
              rootMaxHeight,
              rootMaxWidth,
              rootSumX / static_cast<double>(anchors.size()),
              rootSumY / static_cast<double>(anchors.size()),
            });

            Rect bestRect = currentRect;
            BundlePackScore bestScore = baseScore;
            double bestDistance = std::numeric_limits<double>::infinity();
            for (const BundlePackAnchor& anchor : anchors) {
              for (int direction = 0;
                   direction < packDirections; ++direction) {
                const double angle =
                  2.0 * kBundlePackPi * static_cast<double>(direction)
                  / static_cast<double>(packDirections);
                const double ux = std::cos(angle);
                const double uy = std::sin(angle);
                const double xRadius = std::abs(ux) > 1e-6
                  ? (bundleWidth / 2.0 + anchor.width / 2.0 + packClearance)
                    / std::abs(ux)
                  : std::numeric_limits<double>::infinity();
                const double yRadius = std::abs(uy) > 1e-6
                  ? (bundleHeight / 2.0 + anchor.height / 2.0 + packClearance)
                    / std::abs(uy)
                  : std::numeric_limits<double>::infinity();
                const double nearRadius = std::min(xRadius, yRadius);
                if (!std::isfinite(nearRadius)) continue;
                for (const double factor : packRadiusFactors) {
                  const double candidateCenterX =
                    anchor.x + ux * nearRadius * factor;
                  const double candidateCenterY =
                    anchor.y + uy * nearRadius * factor;
                  Rect candidateRect;
                  candidateRect.left = candidateCenterX - bundleWidth / 2.0;
                  candidateRect.right = candidateCenterX + bundleWidth / 2.0;
                  candidateRect.top = candidateCenterY - bundleHeight / 2.0;
                  candidateRect.bottom = candidateCenterY + bundleHeight / 2.0;
                  if (
                      candidateRect.left < initialPackBounds.left
                      || candidateRect.right > initialPackBounds.right
                      || candidateRect.top < initialPackBounds.top
                      || candidateRect.bottom > initialPackBounds.bottom) {
                    continue;
                  }
                  const BundlePackScore candidateScore =
                    scorePackedBundle(bundleIndex, candidateRect);
                  const double distance = std::hypot(
                    candidateCenterX - currentCenterX,
                    candidateCenterY - currentCenterY);
                  const bool better =
                    candidateScore.total + 1e-9 < bestScore.total
                    || (
                      std::abs(candidateScore.total - bestScore.total) <= 1e-9
                      && candidateScore.connectorNode < bestScore.connectorNode)
                    || (
                      std::abs(candidateScore.total - bestScore.total) <= 1e-9
                      && candidateScore.connectorNode == bestScore.connectorNode
                      && distance < bestDistance);
                  if (better) {
                    bestRect = candidateRect;
                    bestScore = candidateScore;
                    bestDistance = distance;
                  }
                }
              }
            }
            if (
                bestScore.total + 1e-9 >= baseScore.total
                || !std::isfinite(bestDistance)
                || bestDistance < 0.1) {
              continue;
            }

            const double dx = rectCenterX(bestRect) - currentCenterX;
            const double dy = rectCenterY(bestRect) - currentCenterY;
            for (const std::string& leafId : bundle.leafModelIds) {
              auto leafIt = packNodeIndex.find(leafId);
              if (leafIt == packNodeIndex.end()) continue;
              const NodeRecord& leaf = nodes[leafIt->second];
              attributes.x(leaf.handle) += dx;
              attributes.y(leaf.handle) += dy;
            }
            recomputeLeafBundleBboxesFromNodes(
              metadata.leafBundles, nodes, attributes);
            refreshPackedBundleRects();
            ++movedBundles;
            ++movedThisPass;
          }
          if (movedThisPass == 0) break;
        }

        const std::size_t connectorHitsAfter = totalConnectorHits();
        rerouteBundleConnectorPack();
        const LayoutQualityMetrics finalPackQuality =
          measureBundleConnectorPack();
        const Rect finalPackBounds = graphNodeBounds(nodes, attributes);
        const double finalPackArea =
          rectWidth(finalPackBounds) * rectHeight(finalPackBounds);
        const bool packAccepted =
          movedBundles > 0
          && finalPackQuality.visualCrossings
            < initialPackQuality.visualCrossings
          && finalPackQuality.nodeOverlaps
            <= initialPackQuality.nodeOverlaps
          && finalPackQuality.bundleNodeOverlaps
            <= initialPackQuality.bundleNodeOverlaps
          && (
            initialPackArea <= 0.0
            || finalPackArea <= initialPackArea * packBboxLimit);
        if (!packAccepted) {
          for (std::size_t i = 0;
               i < nodes.size() && i < savedPackPositions.size(); ++i) {
            attributes.x(nodes[i].handle) = savedPackPositions[i].first;
            attributes.y(nodes[i].handle) = savedPackPositions[i].second;
          }
          routes = savedPackRoutes;
          metadata = savedPackMetadata;
        }
        std::fprintf(stderr,
          "[bundle-connector-pack-final] %s moved=%zu connectorHits=%zu->%zu "
          "visual=%zu->%zu edgeCross=%zu->%zu edgeNode=%zu->%zu "
          "bundleEdge=%zu->%zu bundleNode=%zu->%zu bbox=%.3fB->%.3fB.\n",
          packAccepted ? "accepted" : "reverted",
          movedBundles,
          connectorHitsBefore,
          connectorHitsAfter,
          initialPackQuality.visualCrossings,
          finalPackQuality.visualCrossings,
          initialPackQuality.edgeCrossings,
          finalPackQuality.edgeCrossings,
          initialPackQuality.edgeNodeIntersections,
          finalPackQuality.edgeNodeIntersections,
          initialPackQuality.bundleEdgeIntersections,
          finalPackQuality.bundleEdgeIntersections,
          initialPackQuality.bundleNodeOverlaps,
          finalPackQuality.bundleNodeOverlaps,
          initialPackArea / 1e9,
          finalPackArea / 1e9);
      }
    }

    // === DJERD_DIAGONAL_RETOUCH=1 ===
    // Retouch-only route polish after the best node placement / broad reroute
    // candidates have settled. It targets long diagonal segments that visually
    // cut through the diagram: replace one segment at a time with the two
    // possible one-bend orthogonal doglegs, accepting only local crossing-score
    // improvements and reverting if the global edge-edge total regresses.
    {
      const char* retouchEnv = std::getenv("DJERD_DIAGONAL_RETOUCH");
      if (retouchEnv && std::strcmp(retouchEnv, "0") != 0
          && routes.size() == edges.size() && !nodes.empty()) {
        int topK = 160;
        if (const char* k = std::getenv("DJERD_DIAGONAL_RETOUCH_TOPK")) {
          topK = std::max(1, std::atoi(k));
        }
        int segmentsPerEdge = 3;
        if (const char* s = std::getenv("DJERD_DIAGONAL_RETOUCH_SEGMENTS_PER_EDGE")) {
          segmentsPerEdge = std::max(1, std::atoi(s));
        }
        int minGain = 1;
        if (const char* g = std::getenv("DJERD_DIAGONAL_RETOUCH_MIN_GAIN")) {
          minGain = std::max(0, std::atoi(g));
        }
        const std::size_t minGainU = static_cast<std::size_t>(minGain);
        const double minSpan = readDoubleEnv(
          "DJERD_DIAGONAL_RETOUCH_MIN_SPAN", 80.0, 0.0, 1'000'000.0);
        const double nodeWeight = readDoubleEnv(
          "DJERD_DIAGONAL_RETOUCH_EDGE_NODE_WEIGHT", 1.0, 0.0, 1000.0);
        const double overlapWeight = readDoubleEnv(
          "DJERD_DIAGONAL_RETOUCH_SEGMENT_OVERLAP_WEIGHT", 1.0, 0.0, 1000.0);
        const double overlapLengthWeight = readDoubleEnv(
          "DJERD_DIAGONAL_RETOUCH_SEGMENT_OVERLAP_LENGTH_WEIGHT",
          0.0001, 0.0, 1.0);
        const double lengthWeight = readDoubleEnv(
          "DJERD_DIAGONAL_RETOUCH_LENGTH_WEIGHT", 0.0, 0.0, 1000.0);
        const double nodeMargin = visualNodeMargin();
        const bool allowNodeHitDebt = readBoolEnv(
          "DJERD_DIAGONAL_RETOUCH_ALLOW_NODE_HIT_DEBT", false);
        const bool useBundleObstacles = readBoolEnv(
          "DJERD_DIAGONAL_RETOUCH_BUNDLE_OBSTACLES", true);
        int retouchRounds = 2;
        if (const char* r = std::getenv("DJERD_DIAGONAL_RETOUCH_ROUNDS")) {
          retouchRounds = std::max(1, std::atoi(r));
        }
        const bool useTwoBend = readBoolEnv(
          "DJERD_DIAGONAL_RETOUCH_TWO_BEND", true);

        std::unordered_set<std::string> bundleAbsorbedRetouch;
        std::vector<std::unordered_set<std::string>> bundleExemptRetouch;
        std::vector<Rect> bundleRectsRetouch;
        if (!metadata.leafBundles.empty()) {
          if (useBundleObstacles) {
            bundleExemptRetouch.reserve(metadata.leafBundles.size());
            bundleRectsRetouch.reserve(metadata.leafBundles.size());
          }
          const double bundleMargin = leafBundleVisualMargin();
          for (const LeafBundleRecord& bundle : metadata.leafBundles) {
            std::unordered_set<std::string> exempt;
            exempt.insert(bundle.parentModelId);
            bundleAbsorbedRetouch.insert(bundle.parentModelId);
            for (const std::string& leaf : bundle.leafModelIds) {
                exempt.insert(leaf);
              bundleAbsorbedRetouch.insert(leaf);
            }
            if (useBundleObstacles) {
              bundleExemptRetouch.push_back(std::move(exempt));
              bundleRectsRetouch.push_back(renderedLeafBundleRect(bundle, bundleMargin));
            }
          }
        }
        auto retouchBundleNodeOverlapCount = [&]() {
          std::size_t count = 0;
          const double bundleMargin = leafBundleVisualMargin();
          const double nodeMarginForBundle = visualNodeMargin();
          for (const LeafBundleRecord& bundle : metadata.leafBundles) {
            const Rect bundleRect = renderedLeafBundleRect(bundle, bundleMargin);
            for (const NodeRecord& node : nodes) {
              if (bundleAbsorbedRetouch.count(node.modelId)) continue;
              if (rectsOverlap(
                  bundleRect,
                  nodeRect(node, attributes, nodeMarginForBundle))) {
                ++count;
              }
            }
          }
          return count;
        };
	        auto retouchNodeOverlapCount = [&]() {
	          std::vector<std::pair<Rect, std::size_t>> rects;
	          rects.reserve(nodes.size());
	          const double nodeMarginForOverlap = visualNodeMargin();
          for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (bundleAbsorbedRetouch.count(nodes[i].modelId)) continue;
            rects.emplace_back(
              nodeRect(nodes[i], attributes, nodeMarginForOverlap),
              i);
          }
          std::sort(
            rects.begin(),
            rects.end(),
            [](const auto& left, const auto& right) {
              return left.first.left < right.first.left;
            });
          std::size_t count = 0;
          for (std::size_t i = 0; i < rects.size(); ++i) {
            for (std::size_t j = i + 1; j < rects.size(); ++j) {
              if (rects[j].first.left >= rects[i].first.right) break;
              if (rectsOverlap(rects[i].first, rects[j].first)) {
                ++count;
              }
            }
	          }
	          return count;
	        };
	        auto retouchNodeSpacingOverlapCount = [&]() {
	          return countNodeRectOverlaps(nodes, attributes, true);
	        };

        auto isDiagonalSegment = [&](const RoutePoint& a, const RoutePoint& b) {
          return std::abs(a.x - b.x) >= minSpan
            && std::abs(a.y - b.y) >= minSpan;
        };
        auto polyCrossCountAB = [&](const std::vector<RoutePoint>& ra,
                                    const std::vector<RoutePoint>& rb) -> std::size_t {
          if (ra.size() < 2 || rb.size() < 2) return 0;
          std::size_t count = 0;
          for (std::size_t li = 1; li < ra.size(); ++li) {
            for (std::size_t rj = 1; rj < rb.size(); ++rj) {
              RoutePoint isect;
              if (properSegmentIntersection(
                  ra[li - 1], ra[li], rb[rj - 1], rb[rj], isect)) {
                ++count;
              }
            }
          }
          return count;
        };
        auto routeCrossCount = [&](std::size_t e,
                                   const std::vector<RoutePoint>& cand) -> std::size_t {
          std::size_t t = 0;
          for (std::size_t e2 = 0; e2 < edges.size(); ++e2) {
            if (e2 == e) continue;
            if (sharesEndpoint(edges[e], edges[e2])) continue;
            t += polyCrossCountAB(cand, routes[e2]);
          }
          return t;
        };
        auto totalCross = [&]() -> std::size_t {
          std::size_t t = 0;
          for (std::size_t i = 0; i < edges.size(); ++i) {
            for (std::size_t j = i + 1; j < edges.size(); ++j) {
              if (sharesEndpoint(edges[i], edges[j])) continue;
              t += polyCrossCountAB(routes[i], routes[j]);
            }
          }
          return t;
        };
        auto segmentCrossCount = [&](std::size_t e, std::size_t segmentIndex) {
          std::size_t t = 0;
          if (e >= routes.size() || segmentIndex == 0
              || segmentIndex >= routes[e].size()) {
            return t;
          }
          const RoutePoint a = routes[e][segmentIndex - 1];
          const RoutePoint b = routes[e][segmentIndex];
          for (std::size_t e2 = 0; e2 < routes.size(); ++e2) {
            if (e2 == e) continue;
            if (sharesEndpoint(edges[e], edges[e2])) continue;
            for (std::size_t rj = 1; rj < routes[e2].size(); ++rj) {
              RoutePoint isect;
              if (properSegmentIntersection(
                  a, b, routes[e2][rj - 1], routes[e2][rj], isect)) {
                ++t;
              }
            }
          }
          return t;
        };

        const std::size_t before = totalCross();
        const std::vector<std::vector<RoutePoint>> savedRoutes = routes;
        const std::vector<LeafBundleRecord> savedLeafBundles = metadata.leafBundles;
        std::vector<std::pair<double, double>> savedNodePositions;
        savedNodePositions.reserve(nodes.size());
        for (const NodeRecord& node : nodes) {
          savedNodePositions.emplace_back(
            attributes.x(node.handle),
            attributes.y(node.handle));
        }

        const bool runNodePairRetouch = readBoolEnv("DJERD_NODE_PAIR_RETOUCH", true);
        const bool pairRawAccept = readBoolEnv(
          "DJERD_NODE_PAIR_RETOUCH_RAW_ACCEPT", true);
        std::size_t nodePairMoved = 0;
        std::size_t nodePairConsidered = 0;
        std::size_t nodePairRejected = 0;
        std::size_t nodePairGainTotal = 0;
        int nodePairCompletedRounds = 0;
        bool nodePairBudgetHit = false;
        double nodePairBudgetMs = 0.0;
        bool useReportedCarrierScoring = false;
        std::size_t currentReportedEdgeCross = 0;
        std::size_t initialReportedEdgeCross = 0;
        std::size_t currentExactFinalEdgeCross = 0;
        std::size_t initialExactFinalEdgeCross = 0;
        std::size_t currentRawRouteCross = before;
        const std::size_t initialRawRouteCross = before;
        auto finalReportedEdgeCrossQuiet = [&](std::size_t rawCross) {
          LayoutQualityMetrics metricQuality{};
          metricQuality.edgeCrossings = rawCross;
          LayoutRunMetadata metricMetadata = metadata;
          applyFinalCarrierMetricsIfRequested(
            nodes,
            edges,
            routes,
            attributes,
            clusterByModelIdFull,
            metricMetadata,
            metricQuality,
            rawCross,
            true,
            false);
          return metricQuality.edgeCrossings;
        };
        auto retouchQualityForCurrent = [&](std::size_t rawCross) {
          LayoutQualityMetrics metricQuality = measureLayoutQuality(
            nodes, edges, routes, attributes, &metadata.leafBundles,
            &metadata.clusterByModelId);
          metricQuality.edgeCrossings = rawCross;
          LayoutRunMetadata metricMetadata = metadata;
          applyFinalCarrierMetricsIfRequested(
            nodes,
            edges,
            routes,
            attributes,
            clusterByModelIdFull,
            metricMetadata,
            metricQuality,
            rawCross,
            true,
            false);
          metricQuality.visualCrossings =
            metricQuality.edgeCrossings
            + metricQuality.edgeNodeIntersections
            + metricQuality.nodeOverlaps
            + metricQuality.bundleEdgeIntersections
            + metricQuality.bundleNodeOverlaps;
          return metricQuality;
        };
        const LayoutQualityMetrics initialRetouchQuality =
          retouchQualityForCurrent(before);

        if (runNodePairRetouch) {
          int pairTopK = 96;
          if (const char* k = std::getenv("DJERD_NODE_PAIR_RETOUCH_TOPK")) {
            pairTopK = std::max(1, std::atoi(k));
          }
          int pairRounds = 3;
          if (const char* r = std::getenv("DJERD_NODE_PAIR_RETOUCH_ROUNDS")) {
            pairRounds = std::max(1, std::atoi(r));
          }
          int pairSteps = 3;
          if (const char* s = std::getenv("DJERD_NODE_PAIR_RETOUCH_STEPS")) {
            pairSteps = std::max(1, std::atoi(s));
          }
          int maxIncident = 28;
          if (const char* m = std::getenv("DJERD_NODE_PAIR_RETOUCH_MAX_INCIDENT")) {
            maxIncident = std::max(1, std::atoi(m));
          }
          const double pairMinSpan = readDoubleEnv(
            "DJERD_NODE_PAIR_RETOUCH_MIN_SPAN", 1200.0, 0.0, 1'000'000.0);
          const double pairLeafMinSpan = readDoubleEnv(
            "DJERD_NODE_PAIR_RETOUCH_LEAF_MIN_SPAN", pairMinSpan, 0.0, 1'000'000.0);
          const double pairBaseStep = readDoubleEnv(
            "DJERD_NODE_PAIR_RETOUCH_STEP", 320.0, 1.0, 100'000.0);
          const double pairMaxShift = readDoubleEnv(
            "DJERD_NODE_PAIR_RETOUCH_MAX_SHIFT", 1600.0, 1.0, 1'000'000.0);
          const double pairNodeMargin = readDoubleEnv(
            "DJERD_NODE_PAIR_RETOUCH_NODE_MARGIN", 0.0, 0.0, 480.0);
          const bool allowNodeOverlap = readBoolEnv(
            "DJERD_NODE_PAIR_RETOUCH_ALLOW_NODE_OVERLAP", false);
          const bool pairLeafSnap = readBoolEnv(
            "DJERD_NODE_PAIR_RETOUCH_LEAF_SNAP", true);
          const bool pairLeafOnly = readBoolEnv(
            "DJERD_NODE_PAIR_RETOUCH_LEAF_ONLY", true);
          const bool pairCompactRelocate = readBoolEnv(
            "DJERD_NODE_PAIR_RETOUCH_COMPACT_RELOCATE", false);
          int pairSlotRings = 2;
          if (const char* r = std::getenv("DJERD_NODE_PAIR_RETOUCH_SLOT_RINGS")) {
            pairSlotRings = std::max(0, std::atoi(r));
          }
          const double pairGap = readDoubleEnv(
            "DJERD_NODE_PAIR_RETOUCH_PAIR_GAP", 24.0, 0.0, 10'000.0);
          const double leafSnapGap = readDoubleEnv(
            "DJERD_NODE_PAIR_RETOUCH_LEAF_GAP", pairGap, 0.0, 10'000.0);
          nodePairBudgetMs = readDoubleEnv(
            "DJERD_NODE_PAIR_RETOUCH_BUDGET_MS", 0.0, 0.0, 3'600'000.0);
          const auto nodePairStartedAt = std::chrono::steady_clock::now();
          const auto nodePairBudgetExceeded = [&]() {
            if (nodePairBudgetMs <= 0.0) {
              return false;
            }
            const double elapsedMs = std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - nodePairStartedAt).count();
            return elapsedMs >= nodePairBudgetMs;
          };

          std::unordered_map<std::string, std::size_t> nodeIndexById;
          nodeIndexById.reserve(nodes.size());
          for (std::size_t i = 0; i < nodes.size(); ++i) {
            nodeIndexById[nodes[i].modelId] = i;
          }

	          std::vector<std::vector<std::size_t>> incidentEdges(nodes.size());
	          for (std::size_t e = 0; e < edges.size(); ++e) {
	            auto sIt = nodeIndexById.find(edges[e].sourceModelId);
            auto tIt = nodeIndexById.find(edges[e].targetModelId);
            if (sIt == nodeIndexById.end() || tIt == nodeIndexById.end()) {
              continue;
            }
            incidentEdges[sIt->second].push_back(e);
            if (tIt->second != sIt->second) {
	              incidentEdges[tIt->second].push_back(e);
	            }
	          }

	          std::unordered_map<std::string, std::size_t> leafBundleIndexById;
	          leafBundleIndexById.reserve(nodes.size());
	          for (std::size_t bi = 0; bi < metadata.leafBundles.size(); ++bi) {
	            for (const std::string& leaf : metadata.leafBundles[bi].leafModelIds) {
	              leafBundleIndexById[leaf] = bi;
	            }
	          }

	          auto isEffectiveBundleId = [](const std::string& effectiveId) {
	            return effectiveId.rfind("B|", 0) == 0;
	          };
	          auto effectiveNodeId = [&](const std::string& modelId) {
	            auto bundleIt = leafBundleIndexById.find(modelId);
	            if (bundleIt != leafBundleIndexById.end()) {
	              return std::string("B|") + std::to_string(bundleIt->second);
	            }
	            return std::string("N|") + modelId;
	          };

	          std::unordered_map<std::string, std::vector<std::size_t>> effectiveMembers;
	          effectiveMembers.reserve(nodes.size());
	          for (std::size_t i = 0; i < nodes.size(); ++i) {
	            effectiveMembers[effectiveNodeId(nodes[i].modelId)].push_back(i);
	          }

	          std::vector<std::string> effectiveSourceByEdge(edges.size());
	          std::vector<std::string> effectiveTargetByEdge(edges.size());
	          std::map<std::pair<std::string, std::string>, std::vector<std::size_t>>
	            rawEdgesByEffectiveEdge;
	          std::unordered_map<std::string, std::set<std::string>> effectiveNeighbors;
	          std::unordered_map<std::string, std::vector<std::size_t>> effectiveIncidentEdges;
	          for (std::size_t e = 0; e < edges.size(); ++e) {
	            const std::string sourceEffective = effectiveNodeId(edges[e].sourceModelId);
	            const std::string targetEffective = effectiveNodeId(edges[e].targetModelId);
	            effectiveSourceByEdge[e] = sourceEffective;
	            effectiveTargetByEdge[e] = targetEffective;
	            if (sourceEffective == targetEffective) {
	              continue;
	            }
	            const auto key = sourceEffective < targetEffective
	              ? std::make_pair(sourceEffective, targetEffective)
	              : std::make_pair(targetEffective, sourceEffective);
	            rawEdgesByEffectiveEdge[key].push_back(e);
	            effectiveNeighbors[sourceEffective].insert(targetEffective);
	            effectiveNeighbors[targetEffective].insert(sourceEffective);
	            effectiveIncidentEdges[sourceEffective].push_back(e);
	            effectiveIncidentEdges[targetEffective].push_back(e);
	          }
	          for (auto& kv : effectiveIncidentEdges) {
	            auto& incident = kv.second;
	            std::sort(incident.begin(), incident.end());
	            incident.erase(std::unique(incident.begin(), incident.end()), incident.end());
	          }
	          auto effectiveDegree = [&](const std::string& effectiveId) {
	            auto it = effectiveNeighbors.find(effectiveId);
	            return it == effectiveNeighbors.end()
	              ? std::size_t{0}
	              : it->second.size();
	          };
	          auto effectiveIncident = [&](const std::string& effectiveId)
	              -> const std::vector<std::size_t>& {
	            static const std::vector<std::size_t> kEmpty;
	            auto it = effectiveIncidentEdges.find(effectiveId);
	            return it == effectiveIncidentEdges.end() ? kEmpty : it->second;
	          };

	          const auto routePointDist2 = [](const RoutePoint& a, const RoutePoint& b) {
	            const double dx = a.x - b.x;
            const double dy = a.y - b.y;
            return dx * dx + dy * dy;
          };

          auto syncRouteEndpointsForEdge = [&](std::size_t e) {
            if (e >= edges.size() || e >= routes.size() || routes[e].size() < 2) {
              return;
            }
            const EdgeRecord& edge = edges[e];
            auto sIt = nodeIndexById.find(edge.sourceModelId);
            auto tIt = nodeIndexById.find(edge.targetModelId);
            if (sIt == nodeIndexById.end() || tIt == nodeIndexById.end()) {
              return;
            }
            const NodeRecord& sNode = nodes[sIt->second];
            const NodeRecord& tNode = nodes[tIt->second];
            const Rect sourceRect = handleRect(sNode.handle, attributes);
            const Rect targetRect = handleRect(tNode.handle, attributes);
            const RoutePoint sourcePort = straightPortOnRect(sourceRect, targetRect);
            const RoutePoint targetPort = straightPortOnRect(targetRect, sourceRect);
            const double oriented =
              routePointDist2(routes[e].front(), sourcePort)
              + routePointDist2(routes[e].back(), targetPort);
            const double reversed =
              routePointDist2(routes[e].front(), targetPort)
              + routePointDist2(routes[e].back(), sourcePort);
            if (oriented <= reversed) {
              routes[e].front() = sourcePort;
              routes[e].back() = targetPort;
            } else {
              routes[e].front() = targetPort;
              routes[e].back() = sourcePort;
            }
            routes[e] = compressRoutePoints(std::move(routes[e]));
          };
          auto straightenRouteForEdge = [&](std::size_t e) {
            if (e >= edges.size() || e >= routes.size()) {
              return;
            }
            const EdgeRecord& edge = edges[e];
            auto sIt = nodeIndexById.find(edge.sourceModelId);
            auto tIt = nodeIndexById.find(edge.targetModelId);
            if (sIt == nodeIndexById.end() || tIt == nodeIndexById.end()) {
              return;
            }
            const NodeRecord& sNode = nodes[sIt->second];
            const NodeRecord& tNode = nodes[tIt->second];
            const Rect sourceRect = handleRect(sNode.handle, attributes);
            const Rect targetRect = handleRect(tNode.handle, attributes);
            routes[e] = {
              straightPortOnRect(sourceRect, targetRect),
              straightPortOnRect(targetRect, sourceRect),
            };
          };

          auto uniqueIncident = [&](std::size_t sIdx,
                                    bool moveS,
                                    std::size_t tIdx,
                                    bool moveT) {
            std::vector<std::size_t> affected;
            if (moveS) {
              affected.insert(affected.end(), incidentEdges[sIdx].begin(), incidentEdges[sIdx].end());
            }
            if (moveT) {
              affected.insert(affected.end(), incidentEdges[tIdx].begin(), incidentEdges[tIdx].end());
            }
            std::sort(affected.begin(), affected.end());
            affected.erase(std::unique(affected.begin(), affected.end()), affected.end());
            return affected;
          };

          auto localCrossForAffected = [&](const std::vector<std::size_t>& affected) {
            std::vector<char> isAffected(edges.size(), 0);
            for (const std::size_t e : affected) {
              if (e < isAffected.size()) {
                isAffected[e] = 1;
              }
            }
            std::size_t t = 0;
            for (std::size_t i = 0; i < edges.size(); ++i) {
              if (i >= routes.size() || routes[i].size() < 2) continue;
              for (std::size_t j = i + 1; j < edges.size(); ++j) {
                if (!isAffected[i] && !isAffected[j]) continue;
                if (j >= routes.size() || routes[j].size() < 2) continue;
                if (sharesEndpoint(edges[i], edges[j])) continue;
                t += polyCrossCountAB(routes[i], routes[j]);
              }
            }
            return t;
          };

          auto rectAtCenter = [&](const NodeRecord& node, double x, double y) {
            const double width = sanitizeNodeWidth(node, attributes);
            const double height = sanitizeNodeHeight(node, attributes);
            return Rect{
              y + height / 2.0 + pairNodeMargin,
              x - width / 2.0 - pairNodeMargin,
              x + width / 2.0 + pairNodeMargin,
              y - height / 2.0 - pairNodeMargin,
            };
          };

	          auto moveFits = [&](std::size_t sIdx,
	                              bool moveS,
	                              double sx,
                              double sy,
                              std::size_t tIdx,
                              bool moveT,
                              double tx,
                              double ty) {
            if (allowNodeOverlap) {
              return true;
            }
            const Rect sRect = moveS
              ? rectAtCenter(nodes[sIdx], sx, sy)
              : nodeRect(nodes[sIdx], attributes, pairNodeMargin);
            const Rect tRect = moveT
              ? rectAtCenter(nodes[tIdx], tx, ty)
              : nodeRect(nodes[tIdx], attributes, pairNodeMargin);
            if (rectsOverlap(sRect, tRect)) {
              return false;
            }
            for (std::size_t i = 0; i < nodes.size(); ++i) {
              if ((moveS && i == sIdx) || (moveT && i == tIdx)) {
                continue;
              }
              const Rect other = nodeRect(nodes[i], attributes, pairNodeMargin);
              if ((moveS && rectsOverlap(sRect, other))
                  || (moveT && rectsOverlap(tRect, other))) {
                return false;
              }
	            }
	            return true;
	          };

	          auto effectiveCenter = [&](const std::string& effectiveId,
	                                     std::size_t fallbackIdx) {
	            if (isEffectiveBundleId(effectiveId)) {
	              try {
	                const std::size_t bi =
	                  static_cast<std::size_t>(std::stoull(effectiveId.substr(2)));
	                if (bi < metadata.leafBundles.size()) {
	                  const Rect rect = renderedLeafBundleRect(metadata.leafBundles[bi], 0.0);
	                  return std::make_pair(
	                    (rect.left + rect.right) * 0.5,
	                    (rect.top + rect.bottom) * 0.5);
	                }
	              } catch (const std::exception&) {
	              }
	            }
	            if (fallbackIdx < nodes.size()) {
	              return std::make_pair(
	                attributes.x(nodes[fallbackIdx].handle),
	                attributes.y(nodes[fallbackIdx].handle));
	            }
	            return std::make_pair(0.0, 0.0);
	          };

	          auto effectiveSize = [&](const std::string& effectiveId,
	                                   std::size_t fallbackIdx) {
	            if (isEffectiveBundleId(effectiveId)) {
	              try {
	                const std::size_t bi =
	                  static_cast<std::size_t>(std::stoull(effectiveId.substr(2)));
	                if (bi < metadata.leafBundles.size()) {
	                  const Rect rect = renderedLeafBundleRect(metadata.leafBundles[bi], 0.0);
	                  return std::make_pair(rectWidth(rect), rectHeight(rect));
	                }
	              } catch (const std::exception&) {
	              }
	            }
	            if (fallbackIdx < nodes.size()) {
	              return std::make_pair(
	                sanitizeNodeWidth(nodes[fallbackIdx], attributes),
	                sanitizeNodeHeight(nodes[fallbackIdx], attributes));
	            }
	            return std::make_pair(0.0, 0.0);
	          };

	          auto effectiveMemberIndices = [&](const std::string& effectiveId)
	              -> const std::vector<std::size_t>& {
	            static const std::vector<std::size_t> kEmpty;
	            auto it = effectiveMembers.find(effectiveId);
	            return it == effectiveMembers.end() ? kEmpty : it->second;
	          };

	          auto collectMoveNodeIndices = [&](const std::string& sEffective,
	                                            bool moveS,
	                                            const std::string& tEffective,
	                                            bool moveT) {
	            std::vector<std::size_t> moved;
	            auto append = [&](const std::string& effectiveId) {
	              const auto& members = effectiveMemberIndices(effectiveId);
	              moved.insert(moved.end(), members.begin(), members.end());
	            };
	            if (moveS) append(sEffective);
	            if (moveT && tEffective != sEffective) append(tEffective);
	            std::sort(moved.begin(), moved.end());
	            moved.erase(std::unique(moved.begin(), moved.end()), moved.end());
	            return moved;
	          };

	          auto effectiveMoveFits = [&](const std::string& sEffective,
	                                       bool moveS,
	                                       double sdx,
	                                       double sdy,
	                                       const std::string& tEffective,
	                                       bool moveT,
	                                       double tdx,
	                                       double tdy) {
	            if (allowNodeOverlap) {
	              return true;
	            }
	            const std::vector<std::size_t> moved =
	              collectMoveNodeIndices(sEffective, moveS, tEffective, moveT);
	            if (moved.empty()) {
	              return false;
	            }
	            std::unordered_set<std::size_t> movedSet(moved.begin(), moved.end());
	            std::vector<std::string> movedEffectiveIds;
	            if (moveS) movedEffectiveIds.push_back(sEffective);
	            if (moveT && tEffective != sEffective) movedEffectiveIds.push_back(tEffective);
	            std::sort(movedEffectiveIds.begin(), movedEffectiveIds.end());
	            movedEffectiveIds.erase(
	              std::unique(movedEffectiveIds.begin(), movedEffectiveIds.end()),
	              movedEffectiveIds.end());
	            std::vector<Rect> movedRects;
	            movedRects.reserve(movedEffectiveIds.size());
	            for (const std::string& effectiveId : movedEffectiveIds) {
	              const double dx = effectiveId == sEffective
	                ? sdx
	                : (effectiveId == tEffective ? tdx : 0.0);
	              const double dy = effectiveId == sEffective
	                ? sdy
	                : (effectiveId == tEffective ? tdy : 0.0);
	              if (isEffectiveBundleId(effectiveId)) {
	                try {
	                  const std::size_t bi =
	                    static_cast<std::size_t>(std::stoull(effectiveId.substr(2)));
	                  if (bi < metadata.leafBundles.size()) {
	                    Rect rect = renderedLeafBundleRect(metadata.leafBundles[bi], pairNodeMargin);
	                    rect.left += dx;
	                    rect.right += dx;
	                    rect.top += dy;
	                    rect.bottom += dy;
	                    movedRects.push_back(rect);
	                    continue;
	                  }
	                } catch (const std::exception&) {
	                }
	              }
	              const auto& members = effectiveMemberIndices(effectiveId);
	              for (const std::size_t idx : members) {
	                if (idx >= nodes.size()) continue;
	                movedRects.push_back(rectAtCenter(
	                  nodes[idx],
	                  attributes.x(nodes[idx].handle) + dx,
	                  attributes.y(nodes[idx].handle) + dy));
	              }
	            }
	            for (std::size_t i = 0; i < nodes.size(); ++i) {
	              if (movedSet.count(i)) {
	                continue;
	              }
	              const Rect other = nodeRect(nodes[i], attributes, pairNodeMargin);
	              for (const Rect& movedRect : movedRects) {
	                if (rectsOverlap(movedRect, other)) {
	                  return false;
	                }
	              }
	            }
	            return true;
	          };

	          auto collectAffectedForMove = [&](const std::string& sEffective,
	                                            bool moveS,
	                                            const std::string& tEffective,
	                                            bool moveT) {
	            std::vector<std::size_t> affected;
	            auto append = [&](const std::string& effectiveId) {
	              const auto& incident = effectiveIncident(effectiveId);
	              affected.insert(affected.end(), incident.begin(), incident.end());
	            };
	            if (moveS) append(sEffective);
	            if (moveT && tEffective != sEffective) append(tEffective);
	            std::sort(affected.begin(), affected.end());
	            affected.erase(std::unique(affected.begin(), affected.end()), affected.end());
	            return affected;
	          };

	          auto snapshotMovedPositions = [&](const std::string& sEffective,
	                                           bool moveS,
	                                           const std::string& tEffective,
	                                           bool moveT) {
	            std::vector<std::pair<std::size_t, std::pair<double, double>>> snapshot;
	            const std::vector<std::size_t> moved =
	              collectMoveNodeIndices(sEffective, moveS, tEffective, moveT);
	            snapshot.reserve(moved.size());
	            for (const std::size_t idx : moved) {
	              if (idx >= nodes.size()) continue;
	              snapshot.push_back({
	                idx,
	                {attributes.x(nodes[idx].handle), attributes.y(nodes[idx].handle)}
	              });
	            }
	            return snapshot;
	          };

	          auto restoreMovedPositions = [&](
	              const std::vector<std::pair<std::size_t, std::pair<double, double>>>& snapshot) {
	            for (const auto& entry : snapshot) {
	              const std::size_t idx = entry.first;
	              if (idx >= nodes.size()) continue;
	              attributes.x(nodes[idx].handle) = entry.second.first;
	              attributes.y(nodes[idx].handle) = entry.second.second;
	            }
	          };

	          auto applyEffectiveDelta = [&](const std::string& effectiveId,
	                                         double dx,
	                                         double dy) {
	            const auto& members = effectiveMemberIndices(effectiveId);
	            for (const std::size_t idx : members) {
	              if (idx >= nodes.size()) continue;
	              attributes.x(nodes[idx].handle) += dx;
	              attributes.y(nodes[idx].handle) += dy;
	            }
	          };

          auto currentNodeBBoxArea = [&]() {
            bool any = false;
            double minX = std::numeric_limits<double>::infinity();
            double minY = std::numeric_limits<double>::infinity();
            double maxX = -std::numeric_limits<double>::infinity();
            double maxY = -std::numeric_limits<double>::infinity();
            for (const NodeRecord& node : nodes) {
              const double x = attributes.x(node.handle);
              const double y = attributes.y(node.handle);
              const double width = sanitizeNodeWidth(node, attributes);
              const double height = sanitizeNodeHeight(node, attributes);
              if (!std::isfinite(x) || !std::isfinite(y)
                  || !std::isfinite(width) || !std::isfinite(height)) {
                continue;
              }
              minX = std::min(minX, x - width / 2.0);
              maxX = std::max(maxX, x + width / 2.0);
              minY = std::min(minY, y - height / 2.0);
              maxY = std::max(maxY, y + height / 2.0);
              any = true;
            }
            if (!any || maxX <= minX || maxY <= minY) {
              return 0.0;
            }
            return (maxX - minX) * (maxY - minY);
          };

          auto currentRouteBBoxArea = [&]() {
            bool any = false;
            double minX = std::numeric_limits<double>::infinity();
            double minY = std::numeric_limits<double>::infinity();
            double maxX = -std::numeric_limits<double>::infinity();
            double maxY = -std::numeric_limits<double>::infinity();
            for (const std::vector<RoutePoint>& route : routes) {
              for (const RoutePoint& point : route) {
                if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
                  continue;
                }
                minX = std::min(minX, point.x);
                maxX = std::max(maxX, point.x);
                minY = std::min(minY, point.y);
                maxY = std::max(maxY, point.y);
                any = true;
              }
            }
            if (!any || maxX <= minX || maxY <= minY) {
              return 0.0;
            }
            return (maxX - minX) * (maxY - minY);
          };

          using CarrierPairKey = std::pair<std::string, std::string>;
          using CarrierSupportMap = std::map<CarrierPairKey, std::size_t>;

          const char* skipCarrierRetouchEnv = std::getenv("DJERD_NO_CARRIER_CROSS");
          useReportedCarrierScoring =
            !metadata.leafBundles.empty()
            && !(skipCarrierRetouchEnv && std::strcmp(skipCarrierRetouchEnv, "0") != 0);
          std::vector<std::string> carrierIdByEdgeRetouch(edges.size());
          for (std::size_t e = 0; e < edges.size(); ++e) {
            if (
                e < carrierIdByEdgePre.size()
                && !carrierIdByEdgePre[e].empty()) {
              carrierIdByEdgeRetouch[e] = carrierIdByEdgePre[e];
            } else {
              carrierIdByEdgeRetouch[e] = edges[e].edgeId;
            }
          }

          const double carrierOccMargin = readDoubleEnv(
            "DJERD_CARRIER_CROSS_OCCLUSION_MARGIN", 0.0, 0.0, 480.0);
          auto pointInRetouchCarrierOcclusion = [&](const RoutePoint& point) {
            if (carrierOccMargin <= 0.0) {
              return false;
            }
            for (const NodeRecord& node : nodes) {
              if (bundleAbsorbedRetouch.count(node.modelId)) continue;
              const Rect rect = nodeRect(node, attributes, carrierOccMargin);
              if (
                  point.x >= rect.left && point.x <= rect.right
                  && point.y >= rect.top && point.y <= rect.bottom) {
                return true;
              }
            }
            for (const LeafBundleRecord& bundle : metadata.leafBundles) {
              const Rect rect = renderedLeafBundleRect(bundle, carrierOccMargin);
              if (
                  point.x >= rect.left && point.x <= rect.right
                  && point.y >= rect.top && point.y <= rect.bottom) {
                return true;
              }
            }
            return false;
          };

          auto carrierPairKey = [&](std::size_t left,
                                    std::size_t right,
                                    CarrierPairKey& key) {
            if (left >= edges.size() || right >= edges.size()) {
              return false;
            }
            if (left >= routes.size() || right >= routes.size()) {
              return false;
            }
            if (routes[left].size() < 2 || routes[right].size() < 2) {
              return false;
            }
            if (sharesEndpoint(edges[left], edges[right])) {
              return false;
            }
            const std::string& leftCarrier = carrierIdByEdgeRetouch[left];
            const std::string& rightCarrier = carrierIdByEdgeRetouch[right];
            if (leftCarrier.empty() || rightCarrier.empty() || leftCarrier == rightCarrier) {
              return false;
            }
            key = leftCarrier < rightCarrier
              ? std::make_pair(leftCarrier, rightCarrier)
              : std::make_pair(rightCarrier, leftCarrier);
            return true;
          };

          auto edgePairHasReportedCross = [&](std::size_t left, std::size_t right) {
            if (left >= routes.size() || right >= routes.size()) {
              return false;
            }
            const std::vector<RoutePoint>& leftRoute = routes[left];
            const std::vector<RoutePoint>& rightRoute = routes[right];
            if (leftRoute.size() < 2 || rightRoute.size() < 2) {
              return false;
            }
            for (std::size_t li = 1; li < leftRoute.size(); ++li) {
              for (std::size_t ri = 1; ri < rightRoute.size(); ++ri) {
                RoutePoint isect;
                if (properSegmentIntersection(
                    leftRoute[li - 1], leftRoute[li],
                    rightRoute[ri - 1], rightRoute[ri],
                    isect)) {
                  if (!pointInRetouchCarrierOcclusion(isect)) {
                    return true;
                  }
                }
              }
            }
            return false;
          };

          auto collectCarrierSupport = [&](const std::vector<std::size_t>* affected) {
            if (affected != nullptr) {
              CarrierSupportMap support;
              std::set<std::pair<std::size_t, std::size_t>> visitedEdgePairs;
              for (const std::size_t affectedEdge : *affected) {
                if (affectedEdge >= edges.size()) {
                  continue;
                }
                for (std::size_t other = 0; other < edges.size(); ++other) {
                  if (other == affectedEdge) {
                    continue;
                  }
                  const std::size_t left = std::min(affectedEdge, other);
                  const std::size_t right = std::max(affectedEdge, other);
                  if (!visitedEdgePairs.insert({left, right}).second) {
                    continue;
                  }
                  CarrierPairKey key;
                  if (!carrierPairKey(left, right, key)) {
                    continue;
                  }
                  if (edgePairHasReportedCross(left, right)) {
                    support[key] += 1;
                  }
                }
              }
              return support;
            }

            CarrierSupportMap support;
            for (std::size_t left = 0; left < edges.size(); ++left) {
              if (left >= routes.size() || routes[left].size() < 2) continue;
              for (std::size_t right = left + 1; right < edges.size(); ++right) {
                if (right >= routes.size() || routes[right].size() < 2) continue;
                CarrierPairKey key;
                if (!carrierPairKey(left, right, key)) {
                  continue;
                }
                if (edgePairHasReportedCross(left, right)) {
                  support[key] += 1;
                }
              }
            }
            return support;
          };

          auto supportValue = [](const CarrierSupportMap& support, const CarrierPairKey& key) {
            auto it = support.find(key);
            return it == support.end() ? std::size_t{0} : it->second;
          };

          CarrierSupportMap carrierSupport = useReportedCarrierScoring
            ? collectCarrierSupport(nullptr)
            : CarrierSupportMap{};
          currentReportedEdgeCross = carrierSupport.size();
          initialReportedEdgeCross = currentReportedEdgeCross;
          if (useReportedCarrierScoring) {
            initialExactFinalEdgeCross = finalReportedEdgeCrossQuiet(0);
            currentExactFinalEdgeCross = initialExactFinalEdgeCross;
          } else {
            currentReportedEdgeCross = before;
            initialReportedEdgeCross = before;
            initialExactFinalEdgeCross = before;
            currentExactFinalEdgeCross = before;
          }

          auto effectiveReportedCrossForAffected =
            [&](const CarrierSupportMap& beforeAffected,
                const CarrierSupportMap& afterAffected) {
            long long next = static_cast<long long>(currentReportedEdgeCross);
            std::set<CarrierPairKey> touched;
            for (const auto& kv : beforeAffected) touched.insert(kv.first);
            for (const auto& kv : afterAffected) touched.insert(kv.first);
            for (const CarrierPairKey& key : touched) {
              const std::size_t current = supportValue(carrierSupport, key);
              const std::size_t before = supportValue(beforeAffected, key);
              const std::size_t after = supportValue(afterAffected, key);
              const std::size_t unaffected = current > before ? current - before : 0;
              const bool wasPresent = current > 0;
              const bool willBePresent = unaffected + after > 0;
              if (wasPresent && !willBePresent) {
                --next;
              } else if (!wasPresent && willBePresent) {
                ++next;
              }
            }
            return static_cast<std::size_t>(std::max<long long>(0, next));
          };

          auto applyCarrierSupportChange =
            [&](const CarrierSupportMap& beforeAffected,
                const CarrierSupportMap& afterAffected) {
            std::set<CarrierPairKey> touched;
            for (const auto& kv : beforeAffected) touched.insert(kv.first);
            for (const auto& kv : afterAffected) touched.insert(kv.first);
            for (const CarrierPairKey& key : touched) {
              const std::size_t current = supportValue(carrierSupport, key);
              const std::size_t before = supportValue(beforeAffected, key);
              const std::size_t after = supportValue(afterAffected, key);
              const std::size_t unaffected = current > before ? current - before : 0;
              const std::size_t next = unaffected + after;
              if (next == 0) {
                carrierSupport.erase(key);
              } else {
                carrierSupport[key] = next;
              }
            }
            currentReportedEdgeCross = carrierSupport.size();
          };

          auto removableReportedCarrierPairs =
            [&](const std::vector<std::size_t>& affected) {
            if (!useReportedCarrierScoring) {
              return std::size_t{0};
            }
            const CarrierSupportMap affectedSupport = collectCarrierSupport(&affected);
            std::size_t removable = 0;
            for (const auto& kv : affectedSupport) {
              if (supportValue(carrierSupport, kv.first) <= kv.second) {
                ++removable;
              }
            }
            return removable;
          };

	          struct PairRank {
	            std::size_t crosses;
	            std::size_t edgeIndex;
	            std::vector<std::size_t> effectiveEdgeMembers;
	            std::string sourceEffectiveId;
	            std::string targetEffectiveId;
	            double span;
	            std::size_t incidentCount;
	            std::size_t sourceDegree;
	            std::size_t targetDegree;
	            bool leafSnapEligible;
	          };

          struct PairMove {
            double sdx;
            double sdy;
            double tdx;
            double tdy;
            std::size_t straightEdge;
          };

	          for (int round = 1; round <= pairRounds; ++round) {
	            if (nodePairBudgetExceeded()) {
	              nodePairBudgetHit = true;
	              break;
	            }
	            std::vector<PairRank> rankedPairs;
	            std::set<std::pair<std::string, std::string>> visitedEffectiveEdges;
	            for (std::size_t e = 0; e < edges.size(); ++e) {
	              if (nodePairBudgetExceeded()) {
	                nodePairBudgetHit = true;
	                break;
	              }
	              if (e >= routes.size() || routes[e].size() < 2) continue;
	              const EdgeRecord& edge = edges[e];
	              auto sIt = nodeIndexById.find(edge.sourceModelId);
	              auto tIt = nodeIndexById.find(edge.targetModelId);
	              if (sIt == nodeIndexById.end() || tIt == nodeIndexById.end()) {
                continue;
              }
              const std::size_t sIdx = sIt->second;
              const std::size_t tIdx = tIt->second;
	              if (sIdx == tIdx) {
	                continue;
	              }
	              const std::string& sEffective = effectiveSourceByEdge[e];
	              const std::string& tEffective = effectiveTargetByEdge[e];
	              if (sEffective.empty()
	                  || tEffective.empty()
	                  || sEffective == tEffective) {
	                continue;
	              }
	              const auto effectiveEdgeKey = sEffective < tEffective
	                ? std::make_pair(sEffective, tEffective)
	                : std::make_pair(tEffective, sEffective);
	              if (!visitedEffectiveEdges.insert(effectiveEdgeKey).second) {
	                continue;
	              }
	              auto membersIt = rawEdgesByEffectiveEdge.find(effectiveEdgeKey);
	              if (membersIt == rawEdgesByEffectiveEdge.end()
	                  || membersIt->second.empty()) {
	                continue;
	              }
	              const auto sourceCenter = effectiveCenter(sEffective, sIdx);
	              const auto targetCenter = effectiveCenter(tEffective, tIdx);
	              const double sx = sourceCenter.first;
	              const double sy = sourceCenter.second;
	              const double tx = targetCenter.first;
	              const double ty = targetCenter.second;
	              const double span = std::hypot(tx - sx, ty - sy);
	              const std::size_t sDegree = effectiveDegree(sEffective);
	              const std::size_t tDegree = effectiveDegree(tEffective);
	              const bool leafSnapEligible =
	                pairLeafSnap
	                && ((sDegree == 1 && tDegree >= 2)
	                    || (tDegree == 1 && sDegree >= 2));
	              if (pairLeafOnly && !leafSnapEligible) {
	                continue;
	              }
              const double effectiveMinSpan =
                leafSnapEligible ? pairLeafMinSpan : pairMinSpan;
              if (!std::isfinite(span) || span < effectiveMinSpan) {
                continue;
              }
              const std::size_t incidentCount = sDegree + tDegree;
	              if (!leafSnapEligible
	                  && incidentCount > static_cast<std::size_t>(maxIncident)) {
	                continue;
	              }
	              std::vector<std::size_t> scoreAffected;
	              if (leafSnapEligible) {
	                const std::string& leafEffective =
	                  sDegree == 1 ? sEffective : tEffective;
	                const auto& incident = effectiveIncident(leafEffective);
	                scoreAffected.assign(incident.begin(), incident.end());
	              } else {
	                scoreAffected = membersIt->second;
	              }
	              std::size_t c = 0;
	              if (useReportedCarrierScoring) {
	                const std::size_t reportedCross =
	                  removableReportedCarrierPairs(scoreAffected);
	                if (leafSnapEligible) {
	                  const std::size_t rawCross =
	                    localCrossForAffected(scoreAffected);
	                  c = std::max(reportedCross, rawCross);
	                } else {
	                  c = reportedCross;
	                }
	              } else {
	                c = localCrossForAffected(scoreAffected);
	              }
	              if (c == 0) {
	                continue;
	              }
	              rankedPairs.push_back({
	                c,
	                e,
	                membersIt->second,
	                sEffective,
	                tEffective,
	                span,
	                leafSnapEligible ? std::size_t{1} : incidentCount,
	                sDegree,
	                tDegree,
	                leafSnapEligible,
	              });
	            }
	            if (nodePairBudgetHit) {
	              break;
	            }

            std::sort(
              rankedPairs.begin(),
              rankedPairs.end(),
              [](const PairRank& left, const PairRank& right) {
                if (left.leafSnapEligible != right.leafSnapEligible) {
                  return left.leafSnapEligible;
                }
                if (left.crosses != right.crosses) return left.crosses > right.crosses;
                if (std::abs(left.span - right.span) > 0.01) return left.span > right.span;
                return left.incidentCount < right.incidentCount;
              });

            const std::size_t limit =
              std::min(static_cast<std::size_t>(pairTopK), rankedPairs.size());
            if (limit == 0) {
              break;
            }

            std::size_t movedThisRound = 0;
            const std::size_t roundStartMoved = nodePairMoved;
            const std::size_t roundStartGain = nodePairGainTotal;
            const std::size_t roundStartRawRouteCross = currentRawRouteCross;
            const std::size_t roundStartReportedEdgeCross = currentReportedEdgeCross;
	            const std::size_t roundStartExactFinalEdgeCross = currentExactFinalEdgeCross;
	            CarrierSupportMap roundStartCarrierSupport;
	            std::vector<std::vector<RoutePoint>> roundStartRoutes;
	            std::vector<std::pair<double, double>> roundStartNodePositions;
	            std::vector<LeafBundleRecord> roundStartLeafBundles;
	            if (useReportedCarrierScoring) {
	              roundStartCarrierSupport = carrierSupport;
	              roundStartRoutes = routes;
	              roundStartLeafBundles = metadata.leafBundles;
	              roundStartNodePositions.reserve(nodes.size());
              for (const NodeRecord& node : nodes) {
                roundStartNodePositions.emplace_back(
                  attributes.x(node.handle),
                  attributes.y(node.handle));
              }
            }
	            for (std::size_t r = 0; r < limit; ++r) {
	              if (nodePairBudgetExceeded()) {
	                nodePairBudgetHit = true;
	                break;
	              }
	              const PairRank& rankedPair = rankedPairs[r];
	              const std::size_t pairEdgeIndex = rankedPair.edgeIndex;
	              const EdgeRecord& edge = edges[pairEdgeIndex];
	              auto sIt = nodeIndexById.find(edge.sourceModelId);
	              auto tIt = nodeIndexById.find(edge.targetModelId);
              if (sIt == nodeIndexById.end() || tIt == nodeIndexById.end()) {
	                continue;
	              }
	              const std::size_t sIdx = sIt->second;
	              const std::size_t tIdx = tIt->second;
	              const std::string& sEffective = rankedPair.sourceEffectiveId;
	              const std::string& tEffective = rankedPair.targetEffectiveId;
	              const auto sourceCenter0 = effectiveCenter(sEffective, sIdx);
	              const auto targetCenter0 = effectiveCenter(tEffective, tIdx);
	              const double sx0 = sourceCenter0.first;
	              const double sy0 = sourceCenter0.second;
	              const double tx0 = targetCenter0.first;
	              const double ty0 = targetCenter0.second;
	              const double dx = tx0 - sx0;
	              const double dy = ty0 - sy0;
	              const double span = std::hypot(dx, dy);
              if (!std::isfinite(span) || span < 1.0) {
                continue;
              }
              const double ux = dx / span;
              const double uy = dy / span;
              const double px = -uy;
              const double py = ux;

              std::vector<PairMove> moves;
              constexpr std::size_t kNoStraightEdge =
                std::numeric_limits<std::size_t>::max();
              auto addMove = [&](
                  double sdx,
                  double sdy,
                  double tdx,
                  double tdy,
                  std::size_t straightEdge = kNoStraightEdge) {
                if (std::abs(sdx) + std::abs(sdy) + std::abs(tdx) + std::abs(tdy) < 0.01) {
                  return;
                }
	                moves.push_back({sdx, sdy, tdx, tdy, straightEdge});
	              };

	              const std::size_t sDegree = rankedPair.sourceDegree;
	              const std::size_t tDegree = rankedPair.targetDegree;
	              const bool leafSnapEligible =
	                pairLeafSnap
	                && ((sDegree == 1 && tDegree >= 2)
	                    || (tDegree == 1 && sDegree >= 2));
	              if (!pairLeafOnly || !leafSnapEligible) {
                for (int stepIndex = 1; stepIndex <= pairSteps; ++stepIndex) {
                  const double shift = std::min(pairMaxShift, pairBaseStep * stepIndex);
                  const double inward = std::min(shift, span * 0.35);
                  addMove(ux * inward, uy * inward, -ux * inward, -uy * inward);
                  addMove(ux * shift, uy * shift, 0.0, 0.0);
                  addMove(0.0, 0.0, -ux * shift, -uy * shift);
                  addMove(px * shift, py * shift, px * shift, py * shift);
                  addMove(-px * shift, -py * shift, -px * shift, -py * shift);
                  addMove(shift, 0.0, shift, 0.0);
                  addMove(-shift, 0.0, -shift, 0.0);
                  addMove(0.0, shift, 0.0, shift);
                  addMove(0.0, -shift, 0.0, -shift);
                }
              }
	              if (pairLeafSnap) {
	                auto addLeafSnapAroundHub =
	                  [&](bool sourceIsLeaf,
	                      std::size_t leafIdx,
	                      std::size_t hubIdx,
	                      const std::string& leafEffective,
	                      const std::string& hubEffective,
	                      double leafX,
	                      double leafY,
	                      double hubX,
	                      double hubY) {
	                  const auto leafSize = effectiveSize(leafEffective, leafIdx);
	                  const auto hubSize = effectiveSize(hubEffective, hubIdx);
	                  const double leafW = leafSize.first;
	                  const double leafH = leafSize.second;
	                  const double hubW = hubSize.first;
	                  const double hubH = hubSize.second;
	                  const double dxGap = (hubW + leafW) / 2.0 + leafSnapGap;
	                  const double dyGap = (hubH + leafH) / 2.0 + leafSnapGap;
	                  auto addLeafCenter = [&](double nx, double ny) {
	                    if (sourceIsLeaf) {
	                      if (effectiveMoveFits(
	                          sEffective, true, nx - leafX, ny - leafY,
	                          tEffective, false, 0.0, 0.0)) {
	                        addMove(nx - leafX, ny - leafY, 0.0, 0.0, pairEdgeIndex);
	                      }
	                    } else if (effectiveMoveFits(
	                        sEffective, false, 0.0, 0.0,
	                        tEffective, true, nx - leafX, ny - leafY)) {
	                      addMove(0.0, 0.0, nx - leafX, ny - leafY, pairEdgeIndex);
	                    }
	                  };
                  for (int ring = 0; ring <= pairSlotRings; ++ring) {
                    const double extra = ring == 0
                      ? 0.0
                      : std::min(pairMaxShift, pairBaseStep * ring);
                    const double xOff = dxGap + extra;
                    const double yOff = dyGap + extra;
                    addLeafCenter(hubX + xOff, hubY);
                    addLeafCenter(hubX - xOff, hubY);
                    addLeafCenter(hubX, hubY + yOff);
                    addLeafCenter(hubX, hubY - yOff);
                    addLeafCenter(hubX + xOff, hubY + yOff);
                    addLeafCenter(hubX + xOff, hubY - yOff);
                    addLeafCenter(hubX - xOff, hubY + yOff);
                    addLeafCenter(hubX - xOff, hubY - yOff);
                  }
	                };
	                if (sDegree == 1 && tDegree > sDegree) {
	                  addLeafSnapAroundHub(
	                    true,
	                    sIdx,
	                    tIdx,
	                    sEffective,
	                    tEffective,
	                    sx0,
	                    sy0,
	                    tx0,
	                    ty0);
	                }
	                if (tDegree == 1 && sDegree > tDegree) {
	                  addLeafSnapAroundHub(
	                    false,
	                    tIdx,
	                    sIdx,
	                    tEffective,
	                    sEffective,
	                    tx0,
	                    ty0,
	                    sx0,
	                    sy0);
	                }
	              }
              if (pairCompactRelocate) {
                const double sw = sanitizeNodeWidth(nodes[sIdx], attributes);
                const double sh = sanitizeNodeHeight(nodes[sIdx], attributes);
                const double tw = sanitizeNodeWidth(nodes[tIdx], attributes);
                const double th = sanitizeNodeHeight(nodes[tIdx], attributes);
                const std::vector<std::pair<double, double>> anchors = {
                  {(sx0 + tx0) / 2.0, (sy0 + ty0) / 2.0},
                  {sx0, sy0},
                  {tx0, ty0},
                };
                auto addCompactCenters =
                  [&](double nsx, double nsy, double ntx, double nty) {
                  if (moveFits(sIdx, true, nsx, nsy, tIdx, true, ntx, nty)) {
                    addMove(nsx - sx0, nsy - sy0, ntx - tx0, nty - ty0);
                  }
                };
                auto addCompactAt = [&](double cx, double cy) {
                  addCompactCenters(
                    cx - (tw + pairGap) / 2.0,
                    cy,
                    cx + (sw + pairGap) / 2.0,
                    cy);
                  addCompactCenters(
                    cx + (tw + pairGap) / 2.0,
                    cy,
                    cx - (sw + pairGap) / 2.0,
                    cy);
                  addCompactCenters(
                    cx,
                    cy - (th + pairGap) / 2.0,
                    cx,
                    cy + (sh + pairGap) / 2.0);
                  addCompactCenters(
                    cx,
                    cy + (th + pairGap) / 2.0,
                    cx,
                    cy - (sh + pairGap) / 2.0);
                };
                for (const auto& anchor : anchors) {
                  addCompactAt(anchor.first, anchor.second);
                  for (int ring = 1; ring <= pairSlotRings; ++ring) {
                    const double shift = std::min(pairMaxShift, pairBaseStep * ring);
                    addCompactAt(anchor.first + shift, anchor.second);
                    addCompactAt(anchor.first - shift, anchor.second);
                    addCompactAt(anchor.first, anchor.second + shift);
                    addCompactAt(anchor.first, anchor.second - shift);
                    addCompactAt(anchor.first + shift, anchor.second + shift);
                    addCompactAt(anchor.first + shift, anchor.second - shift);
                    addCompactAt(anchor.first - shift, anchor.second + shift);
                    addCompactAt(anchor.first - shift, anchor.second - shift);
                  }
                }
              }

              constexpr double kRetouchBBoxEpsilon = 1.0;
              const double bboxBefore =
                currentNodeBBoxArea() + currentRouteBBoxArea();
	              const std::size_t bundleNodeBefore =
	                retouchBundleNodeOverlapCount();
	              const std::size_t nodeOverlapBefore =
	                retouchNodeOverlapCount();
	              const std::size_t nodeSpacingBefore =
	                retouchNodeSpacingOverlapCount();
	              std::size_t bestGain = 0;
              double bestBBoxAfter = std::numeric_limits<double>::infinity();
              std::size_t bestReportedAfter = currentReportedEdgeCross;
              CarrierSupportMap bestBeforeCarrierSupport;
              CarrierSupportMap bestAfterCarrierSupport;
              PairMove bestMove{0.0, 0.0, 0.0, 0.0, kNoStraightEdge};
              bool found = false;

              bool moveSearchBudgetHit = false;
              for (const PairMove& move : moves) {
                if (nodePairBudgetExceeded()) {
                  moveSearchBudgetHit = true;
                  nodePairBudgetHit = true;
                  break;
                }
                const bool moveS =
                  std::abs(move.sdx) + std::abs(move.sdy) >= 0.01;
                const bool moveT =
                  std::abs(move.tdx) + std::abs(move.tdy) >= 0.01;
                const std::vector<std::size_t> affected =
                  collectAffectedForMove(sEffective, moveS, tEffective, moveT);
                if (affected.empty()) {
                  continue;
                }
                const bool fits = effectiveMoveFits(
                  sEffective, moveS, move.sdx, move.sdy,
                  tEffective, moveT, move.tdx, move.tdy);
                if (!fits) {
                  continue;
                }
                const bool useRawLeafSnapScore =
                  useReportedCarrierScoring
                  && move.straightEdge != kNoStraightEdge;
                const std::size_t localBefore = useRawLeafSnapScore
                  ? localCrossForAffected(affected)
                  : (useReportedCarrierScoring
                  ? currentReportedEdgeCross
                  : localCrossForAffected(affected));
                if (localBefore == 0) {
                  continue;
                }
                const CarrierSupportMap beforeCarrierSupport = useReportedCarrierScoring
                  ? collectCarrierSupport(&affected)
                  : CarrierSupportMap{};

                std::vector<std::vector<RoutePoint>> routeSnapshot;
                routeSnapshot.reserve(affected.size());
                for (const std::size_t e : affected) {
                  routeSnapshot.push_back(routes[e]);
                }
                const auto positionSnapshot =
                  snapshotMovedPositions(sEffective, moveS, tEffective, moveT);
                const bool movesBundle =
                  (moveS && isEffectiveBundleId(sEffective))
                  || (moveT && isEffectiveBundleId(tEffective));

                if (moveS) {
                  applyEffectiveDelta(sEffective, move.sdx, move.sdy);
                }
                if (moveT) {
                  applyEffectiveDelta(tEffective, move.tdx, move.tdy);
                }
                if (movesBundle) {
                  recomputeLeafBundleBboxesFromNodes(metadata.leafBundles, nodes, attributes);
                }
                for (const std::size_t e : affected) {
                  syncRouteEndpointsForEdge(e);
                }
                if (move.straightEdge != kNoStraightEdge) {
                  for (const std::size_t e : rankedPair.effectiveEdgeMembers) {
                    straightenRouteForEdge(e);
                  }
                }
                const CarrierSupportMap afterCarrierSupport =
                  useReportedCarrierScoring && fits
                    ? collectCarrierSupport(&affected)
                    : CarrierSupportMap{};
                const std::size_t localAfter = fits
                  ? (
                      useRawLeafSnapScore
                        ? localCrossForAffected(affected)
                        : (useReportedCarrierScoring
                        ? effectiveReportedCrossForAffected(
                            beforeCarrierSupport,
                            afterCarrierSupport)
                        : localCrossForAffected(affected)))
                  : localBefore;
                const std::size_t reportedAfter =
                  useReportedCarrierScoring
                    ? effectiveReportedCrossForAffected(
                        beforeCarrierSupport,
                        afterCarrierSupport)
                    : localAfter;
                const double bboxAfter = fits
                  ? currentNodeBBoxArea() + currentRouteBBoxArea()
                  : bboxBefore;
                const bool bboxNotWorse =
                  bboxAfter <= bboxBefore + kRetouchBBoxEpsilon;
                const std::size_t bundleNodeAfter = fits
                  ? retouchBundleNodeOverlapCount()
                  : bundleNodeBefore;
	                const std::size_t nodeOverlapAfter = fits
	                  ? retouchNodeOverlapCount()
	                  : nodeOverlapBefore;
	                const std::size_t nodeSpacingAfter = fits
	                  ? retouchNodeSpacingOverlapCount()
	                  : nodeSpacingBefore;
	                const bool shapeNotWorse =
	                  bundleNodeAfter <= bundleNodeBefore
	                  && nodeOverlapAfter <= nodeOverlapBefore
	                  && nodeSpacingAfter <= nodeSpacingBefore;

                restoreMovedPositions(positionSnapshot);
                if (movesBundle) {
                  recomputeLeafBundleBboxesFromNodes(metadata.leafBundles, nodes, attributes);
                }
                for (std::size_t i = 0; i < affected.size(); ++i) {
                  routes[affected[i]] = std::move(routeSnapshot[i]);
                }

                if (!fits
                    || localAfter + minGainU > localBefore
                    || !bboxNotWorse
                    || !shapeNotWorse) {
                  continue;
                }
                const std::size_t gain = localBefore - localAfter;
                const bool better =
                  !found
                  || gain > bestGain
                  || (
                    gain == bestGain
                    && bboxAfter + kRetouchBBoxEpsilon < bestBBoxAfter);
                if (better) {
                  bestGain = gain;
                  bestBBoxAfter = bboxAfter;
                  bestReportedAfter = reportedAfter;
                  bestBeforeCarrierSupport = beforeCarrierSupport;
                  bestAfterCarrierSupport = afterCarrierSupport;
                  bestMove = move;
                  found = true;
                }
              }

              if (moveSearchBudgetHit) {
                break;
              }

              ++nodePairConsidered;
              if (!found) {
                ++nodePairRejected;
                continue;
              }

              const bool bestMoveS =
                std::abs(bestMove.sdx) + std::abs(bestMove.sdy) >= 0.01;
	              const bool bestMoveT =
	                std::abs(bestMove.tdx) + std::abs(bestMove.tdy) >= 0.01;
		              const std::vector<std::size_t> affected =
		                collectAffectedForMove(sEffective, bestMoveS, tEffective, bestMoveT);
		              const bool bestMovesBundle =
		                (bestMoveS && isEffectiveBundleId(sEffective))
		                || (bestMoveT && isEffectiveBundleId(tEffective));
              std::vector<std::vector<RoutePoint>> commitRouteSnapshot;
              commitRouteSnapshot.reserve(affected.size());
              for (const std::size_t e : affected) {
                commitRouteSnapshot.push_back(routes[e]);
              }
              const auto commitPositionSnapshot =
                snapshotMovedPositions(sEffective, bestMoveS, tEffective, bestMoveT);
              const std::vector<LeafBundleRecord> commitLeafBundles =
                metadata.leafBundles;
              const CarrierSupportMap commitCarrierSupport = carrierSupport;
              const std::size_t commitReportedEdgeCross = currentReportedEdgeCross;
              const LayoutQualityMetrics pairBaseQuality =
                retouchQualityForCurrent(currentRawRouteCross);
		              if (bestMoveS) {
		                applyEffectiveDelta(sEffective, bestMove.sdx, bestMove.sdy);
		              }
	              if (bestMoveT) {
	                applyEffectiveDelta(tEffective, bestMove.tdx, bestMove.tdy);
	              }
	              if (bestMovesBundle) {
	                recomputeLeafBundleBboxesFromNodes(metadata.leafBundles, nodes, attributes);
	              }
	              for (const std::size_t e : affected) {
	                syncRouteEndpointsForEdge(e);
	              }
	              if (bestMove.straightEdge != kNoStraightEdge) {
		                for (const std::size_t e : rankedPair.effectiveEdgeMembers) {
		                  straightenRouteForEdge(e);
		                }
		              }
              const LayoutQualityMetrics pairAfterQuality =
                retouchQualityForCurrent(currentRawRouteCross);
              const bool pairVisualOk =
                pairAfterQuality.visualCrossings + minGainU
                <= pairBaseQuality.visualCrossings
	                && pairAfterQuality.nodeOverlaps <= pairBaseQuality.nodeOverlaps
	                && pairAfterQuality.nodeSpacingOverlaps
	                   <= pairBaseQuality.nodeSpacingOverlaps
	                && pairAfterQuality.bundleNodeOverlaps
	                   <= pairBaseQuality.bundleNodeOverlaps
                && pairAfterQuality.overlappingEdges
                   <= pairBaseQuality.overlappingEdges
                && pairAfterQuality.edgeSegmentOverlaps
                   <= pairBaseQuality.edgeSegmentOverlaps;
              if (!pairVisualOk) {
                restoreMovedPositions(commitPositionSnapshot);
                metadata.leafBundles = commitLeafBundles;
                for (std::size_t i = 0; i < affected.size(); ++i) {
                  routes[affected[i]] = std::move(commitRouteSnapshot[i]);
                }
                carrierSupport = commitCarrierSupport;
                currentReportedEdgeCross = commitReportedEdgeCross;
                ++nodePairRejected;
                continue;
              }
		              if (useReportedCarrierScoring) {
		                applyCarrierSupportChange(bestBeforeCarrierSupport, bestAfterCarrierSupport);
		                currentReportedEdgeCross = bestReportedAfter;
	              }
	              ++nodePairMoved;
	              ++movedThisRound;
	              nodePairGainTotal += bestGain;
	            }

            if (useReportedCarrierScoring && movedThisRound > 0) {
              const std::size_t roundAfterRawRouteCross = totalCross();
              const std::size_t roundAfterExactFinalEdgeCross =
                finalReportedEdgeCrossQuiet(roundAfterRawRouteCross);
              const bool finalImproved =
                roundAfterExactFinalEdgeCross + minGainU
                <= roundStartExactFinalEdgeCross
                && roundAfterRawRouteCross <= roundStartRawRouteCross;
              const bool rawImproved =
                pairRawAccept
                && roundAfterRawRouteCross + minGainU <= roundStartRawRouteCross;
	              if (!finalImproved && !rawImproved) {
	                routes = std::move(roundStartRoutes);
	                for (std::size_t i = 0;
	                     i < nodes.size() && i < roundStartNodePositions.size();
	                     ++i) {
	                  attributes.x(nodes[i].handle) = roundStartNodePositions[i].first;
	                  attributes.y(nodes[i].handle) = roundStartNodePositions[i].second;
	                }
	                metadata.leafBundles = std::move(roundStartLeafBundles);
	                carrierSupport = std::move(roundStartCarrierSupport);
                currentReportedEdgeCross = roundStartReportedEdgeCross;
                currentExactFinalEdgeCross = roundStartExactFinalEdgeCross;
                currentRawRouteCross = roundStartRawRouteCross;
                nodePairMoved = roundStartMoved;
                nodePairRejected += movedThisRound;
                nodePairGainTotal = roundStartGain;
                movedThisRound = 0;
              } else {
                currentExactFinalEdgeCross = roundAfterExactFinalEdgeCross;
                currentRawRouteCross = roundAfterRawRouteCross;
              }
            }

            ++nodePairCompletedRounds;
            if (nodePairBudgetHit || movedThisRound == 0) {
              break;
            }
          }

	          if (nodePairConsidered > 0 || nodePairBudgetHit) {
	            const std::size_t afterNodePair = totalCross();
	            currentRawRouteCross = afterNodePair;
	            if (!useReportedCarrierScoring) {
	              currentReportedEdgeCross = afterNodePair;
	              currentExactFinalEdgeCross = afterNodePair;
	            }
	            const LayoutQualityMetrics afterNodePairQuality =
	              retouchQualityForCurrent(afterNodePair);
	            std::fprintf(stderr,
	              "[node-pair-retouch] moved=%zu/%zu, rejected=%zu, "
	              "localGain=%zu, scoreEdgeCross=%zu -> %zu, "
	              "finalEdgeCross=%zu -> %zu, rawRouteCross=%zu -> %zu, total cross %zu -> %zu "
	              "nodeSpacing=%zu -> %zu "
	              "(rounds=%d/%d, topK=%d, minSpan=%.0f, leafMinSpan=%.0f, "
              "step=%.0f, maxShift=%.0f, "
              "maxIncident=%d, nodeMargin=%.0f, leafSnap=%d, leafOnly=%d, rawAccept=%d, "
              "compactRelocate=%d, scoring=%s, budget=%.0fms, budgetHit=%d).\n",
              nodePairMoved, nodePairConsidered, nodePairRejected,
              nodePairGainTotal, initialReportedEdgeCross, currentReportedEdgeCross,
	              initialExactFinalEdgeCross, currentExactFinalEdgeCross,
	              initialRawRouteCross, currentRawRouteCross,
	              before, afterNodePair,
	              initialRetouchQuality.nodeSpacingOverlaps,
	              afterNodePairQuality.nodeSpacingOverlaps,
	              nodePairCompletedRounds, pairRounds, pairTopK, pairMinSpan,
              pairLeafMinSpan, pairBaseStep, pairMaxShift, maxIncident, pairNodeMargin,
              pairLeafSnap ? 1 : 0, pairLeafOnly ? 1 : 0, pairRawAccept ? 1 : 0,
              pairCompactRelocate ? 1 : 0,
              useReportedCarrierScoring ? "reported-carrier" : "raw-route",
              nodePairBudgetMs,
              nodePairBudgetHit ? 1 : 0);
          }
        }

        const bool runRouteSegmentRetouch =
          readBoolEnv("DJERD_DIAGONAL_RETOUCH_ROUTE_SEGMENTS", false);
        std::size_t totalRetouched = 0;
        std::size_t totalRejected = 0;
        std::size_t totalDiagonalSegments = 0;
        std::size_t totalLimit = 0;
        int completedRounds = 0;
        for (int retouchRound = 1;
             runRouteSegmentRetouch && retouchRound <= retouchRounds;
             ++retouchRound) {
          std::vector<std::pair<std::size_t, std::size_t>> ranked;
          for (std::size_t e = 0; e < routes.size(); ++e) {
            if (routes[e].size() < 2) continue;
            bool hasDiagonal = false;
            for (std::size_t i = 1; i < routes[e].size(); ++i) {
              if (isDiagonalSegment(routes[e][i - 1], routes[e][i])) {
                hasDiagonal = true;
                break;
              }
            }
            if (!hasDiagonal) continue;
            const std::size_t c = routeCrossCount(e, routes[e]);
            if (c > 0) ranked.emplace_back(c, e);
          }
          std::sort(ranked.rbegin(), ranked.rend());

          RouteOccupancy occupancy;
          if (overlapWeight > 0.0) {
            for (std::size_t e = 0; e < routes.size(); ++e) {
              if (routes[e].size() < 2) continue;
              const LineIntent line = makeLineIntent(edges[e], e, attributes);
              recordRouteOccupancy(routes[e], line, occupancy);
            }
          }

          std::size_t retouched = 0;
          std::size_t rejected = 0;
          std::size_t diagonalSegments = 0;
          const std::size_t limit =
            std::min(static_cast<std::size_t>(topK), ranked.size());
          totalLimit += limit;
          if (limit == 0) {
            break;
          }
          for (std::size_t r = 0; r < limit; ++r) {
            const std::size_t e = ranked[r].second;
            if (routes[e].size() < 2) continue;
            const LineIntent line = makeLineIntent(edges[e], e, attributes);
            const std::vector<NodeObstacle> obstacles = makeNodeObstacles(
              nodes, attributes, nodeMargin, line.sourceHandle, line.targetHandle);
            const std::string& srcId = edges[e].sourceModelId;
            const std::string& tgtId = edges[e].targetModelId;
            if (overlapWeight > 0.0) {
              removeRouteOccupancy(routes[e], line, occupancy);
            }

            struct SegmentCandidate {
              std::size_t crosses;
              std::size_t index;
              double length;
            };
            std::vector<SegmentCandidate> segmentCandidates;
            for (std::size_t i = 1; i < routes[e].size(); ++i) {
              const RoutePoint a = routes[e][i - 1];
              const RoutePoint b = routes[e][i];
              if (!isDiagonalSegment(a, b)) continue;
              const std::size_t c = segmentCrossCount(e, i);
              if (c == 0) continue;
              const double length = std::hypot(b.x - a.x, b.y - a.y);
              segmentCandidates.push_back({c, i, length});
            }
            diagonalSegments += segmentCandidates.size();
            std::sort(
              segmentCandidates.begin(),
              segmentCandidates.end(),
              [](const SegmentCandidate& left, const SegmentCandidate& right) {
                if (left.crosses != right.crosses) return left.crosses > right.crosses;
                return left.length > right.length;
              });
            if (segmentCandidates.size() > static_cast<std::size_t>(segmentsPerEdge)) {
              segmentCandidates.resize(static_cast<std::size_t>(segmentsPerEdge));
            }

            auto nodeHits = [&](const std::vector<RoutePoint>& cand) -> std::size_t {
              if (cand.size() < 2) return 0;
              std::size_t hits = 0;
              for (std::size_t i = 1; i < cand.size(); ++i) {
                for (const NodeObstacle& obstacle : obstacles) {
                  if (bundleAbsorbedRetouch.count(obstacle.nodeId)) continue;
                  if (segmentIntersectsRect(cand[i - 1], cand[i], obstacle.rect)) {
                    ++hits;
                  }
                }
                for (std::size_t bi = 0; bi < bundleRectsRetouch.size(); ++bi) {
                  if (bundleExemptRetouch[bi].count(srcId)
                      || bundleExemptRetouch[bi].count(tgtId)) {
                    continue;
                  }
                  if (segmentIntersectsRect(cand[i - 1], cand[i], bundleRectsRetouch[bi])) {
                    ++hits;
                  }
                }
              }
              return hits;
            };
            auto overlapDebt = [&](const std::vector<RoutePoint>& cand) -> double {
              if (overlapWeight <= 0.0 || cand.size() < 2) return 0.0;
              return routeAxisOverlapDebt(cand, &occupancy, overlapLengthWeight);
            };
            auto score = [&](std::size_t crossings,
                             std::size_t hits,
                             const std::vector<RoutePoint>& cand) -> double {
              return static_cast<double>(crossings)
                + nodeWeight * static_cast<double>(hits)
                + overlapWeight * overlapDebt(cand)
                + lengthWeight * routeLength(cand);
            };
            auto replaceSegment =
              [&](std::size_t segmentIndex, const std::vector<RoutePoint>& mids) {
              std::vector<RoutePoint> cand;
              cand.reserve(routes[e].size() + mids.size());
              for (std::size_t i = 0; i < segmentIndex; ++i) {
                cand.push_back(routes[e][i]);
              }
              for (const RoutePoint& mid : mids) {
                cand.push_back(mid);
              }
              for (std::size_t i = segmentIndex; i < routes[e].size(); ++i) {
                cand.push_back(routes[e][i]);
              }
              return compressRoutePoints(std::move(cand));
            };

            const std::size_t currentCross = routeCrossCount(e, routes[e]);
            const std::size_t currentNodeHits = nodeHits(routes[e]);
            const double currentScore = score(currentCross, currentNodeHits, routes[e]);
            std::size_t bestCross = currentCross;
            double bestScore = currentScore;
            std::vector<RoutePoint> bestRoute;
            bool found = false;
            for (const SegmentCandidate& segment : segmentCandidates) {
              const RoutePoint a = routes[e][segment.index - 1];
              const RoutePoint b = routes[e][segment.index];
              std::vector<std::vector<RoutePoint>> midSets = {
                {{b.x, a.y}},
                {{a.x, b.y}},
              };
              if (useTwoBend) {
                const double midX = (a.x + b.x) / 2.0;
                const double midY = (a.y + b.y) / 2.0;
                midSets.push_back({{midX, a.y}, {midX, b.y}});
                midSets.push_back({{a.x, midY}, {b.x, midY}});
              }
              for (const std::vector<RoutePoint>& mids : midSets) {
                bool degenerate = true;
                for (const RoutePoint& mid : mids) {
                  if (!almostSamePoint(a, mid) && !almostSamePoint(b, mid)) {
                    degenerate = false;
                    break;
                  }
                }
                if (degenerate) {
                  continue;
                }
                const std::vector<RoutePoint> cand = replaceSegment(segment.index, mids);
                if (cand.size() < 2) continue;
                const std::size_t c = routeCrossCount(e, cand);
                if (c + minGainU > currentCross) {
                  continue;
                }
                const std::size_t candidateNodeHits = nodeHits(cand);
                if (!allowNodeHitDebt && candidateNodeHits > currentNodeHits) {
                  continue;
                }
                const double s = score(c, candidateNodeHits, cand);
                if (s < bestScore
                    || (std::abs(s - bestScore) < 1e-9 && c < bestCross)) {
                  bestScore = s;
                  bestCross = c;
                  bestRoute = cand;
                  found = true;
                }
              }
            }
            if (found) {
              routes[e] = std::move(bestRoute);
              ++retouched;
            } else {
              ++rejected;
            }
            if (overlapWeight > 0.0) {
              recordRouteOccupancy(routes[e], line, occupancy);
            }
          }
          ++completedRounds;
          totalRetouched += retouched;
          totalRejected += rejected;
          totalDiagonalSegments += diagonalSegments;
          if (retouched == 0) {
            break;
          }
        }

        const std::size_t after = totalCross();
        const LayoutQualityMetrics afterRetouchQuality =
          retouchQualityForCurrent(after);
        const std::size_t initialFinalEdgeCross = useReportedCarrierScoring
          ? initialExactFinalEdgeCross
          : before;
        const std::size_t afterFinalEdgeCross = useReportedCarrierScoring
          ? finalReportedEdgeCrossQuiet(after)
          : after;
        const bool anyRetouchChange = nodePairMoved > 0 || totalRetouched > 0;
        const bool finalRetouchImproved =
          afterFinalEdgeCross + minGainU <= initialFinalEdgeCross;
        const bool rawRetouchImproved =
          pairRawAccept && after + minGainU <= before;
        const bool scoreRetouchImproved =
          useReportedCarrierScoring
          && currentReportedEdgeCross + minGainU <= initialReportedEdgeCross;
        const bool visualRetouchImproved =
          afterRetouchQuality.visualCrossings + minGainU
          <= initialRetouchQuality.visualCrossings;
        const bool visualShapeDebtOk =
          afterRetouchQuality.nodeOverlaps <= initialRetouchQuality.nodeOverlaps
          && afterRetouchQuality.bundleNodeOverlaps
             <= initialRetouchQuality.bundleNodeOverlaps
          && afterRetouchQuality.overlappingEdges
             <= initialRetouchQuality.overlappingEdges
          && afterRetouchQuality.edgeSegmentOverlaps
             <= initialRetouchQuality.edgeSegmentOverlaps;
        const bool retouchAccepted =
          !anyRetouchChange || (visualRetouchImproved && visualShapeDebtOk);
        const char* retouchAcceptMode =
          visualRetouchImproved && visualShapeDebtOk ? "visual" : "none";
        const bool retouchRegressed =
          anyRetouchChange
          && !retouchAccepted;
        if (retouchRegressed) {
          routes = savedRoutes;
          for (std::size_t i = 0; i < nodes.size() && i < savedNodePositions.size(); ++i) {
            attributes.x(nodes[i].handle) = savedNodePositions[i].first;
            attributes.y(nodes[i].handle) = savedNodePositions[i].second;
          }
          metadata.leafBundles = savedLeafBundles;
          if (useReportedCarrierScoring) {
            currentReportedEdgeCross = initialReportedEdgeCross;
            currentExactFinalEdgeCross = initialExactFinalEdgeCross;
          }
          currentRawRouteCross = initialRawRouteCross;
          std::fprintf(stderr,
            "[diagonal-retouch] reverted: %zu candidates, finalEdgeCross %zu -> %zu, "
            "scoreEdgeCross %zu -> %zu, rawRouteCross %zu -> %zu, "
            "visualCross %zu -> %zu, edgeNode %zu -> %zu, bundleNode %zu -> %zu, "
            "overlappingEdges %zu -> %zu, edgeSegmentOverlaps %zu -> %zu "
            "(edgeImproved=%d/%d/%d, acceptedBy=none).\n",
            totalLimit, initialFinalEdgeCross, afterFinalEdgeCross,
            initialReportedEdgeCross, currentReportedEdgeCross,
            initialRawRouteCross, after,
            initialRetouchQuality.visualCrossings,
            afterRetouchQuality.visualCrossings,
            initialRetouchQuality.edgeNodeIntersections,
            afterRetouchQuality.edgeNodeIntersections,
            initialRetouchQuality.bundleNodeOverlaps,
            afterRetouchQuality.bundleNodeOverlaps,
            initialRetouchQuality.overlappingEdges,
            afterRetouchQuality.overlappingEdges,
            initialRetouchQuality.edgeSegmentOverlaps,
            afterRetouchQuality.edgeSegmentOverlaps,
            finalRetouchImproved ? 1 : 0,
            rawRetouchImproved ? 1 : 0,
            scoreRetouchImproved ? 1 : 0);
        } else {
            std::fprintf(stderr,
              "[diagonal-retouch] nodePairs=%zu/%zu, routeSegments=%zu/%zu, "
              "finalEdgeCross=%zu -> %zu, scoreEdgeCross=%zu -> %zu, "
              "rawRouteCross=%zu -> %zu, visualCross=%zu -> %zu, "
              "edgeNode=%zu -> %zu, bundleNode=%zu -> %zu, "
              "overlappingEdges=%zu -> %zu, edgeSegmentOverlaps=%zu -> %zu "
              "(acceptedBy=%s, rounds=%d/%d, diagonalSegments=%zu, rejected=%zu, nodeWeight=%.3f, "
              "overlapWeight=%.3f, allowNodeHitDebt=%d, bundleObstacles=%d, twoBend=%d, "
              "routeSegmentsEnabled=%d).\n",
              nodePairMoved, nodePairConsidered, totalRetouched, totalLimit,
              initialFinalEdgeCross, afterFinalEdgeCross,
              initialReportedEdgeCross, currentReportedEdgeCross,
              initialRawRouteCross, after,
              initialRetouchQuality.visualCrossings,
              afterRetouchQuality.visualCrossings,
              initialRetouchQuality.edgeNodeIntersections,
              afterRetouchQuality.edgeNodeIntersections,
              initialRetouchQuality.bundleNodeOverlaps,
              afterRetouchQuality.bundleNodeOverlaps,
              initialRetouchQuality.overlappingEdges,
              afterRetouchQuality.overlappingEdges,
              initialRetouchQuality.edgeSegmentOverlaps,
              afterRetouchQuality.edgeSegmentOverlaps,
              retouchAcceptMode, completedRounds, retouchRounds, totalDiagonalSegments, totalRejected,
              nodeWeight, overlapWeight, allowNodeHitDebt ? 1 : 0,
              useBundleObstacles ? 1 : 0, useTwoBend ? 1 : 0,
              runRouteSegmentRetouch ? 1 : 0);
          }
        }
      }

    // First move the worst carrier blockers in bounded monotonic batches.
    // This normally removes most spacing debt as a side effect, leaving the
    // exact clearance projection only a handful of conflicts to resolve.
    if (straightLineMode) {
      (void)clearRenderedCarrierNodeIntersectionsIfRequested(
        nodes,
        edges,
        routes,
        attributes,
        clusterByModelIdFull,
        metadata);
    }

    // Audit and repair table clearance after every placement-changing pass,
    // including the late bundle connector pack. Earlier spacing cleanup
    // cannot protect this invariant because that pack deliberately relocates
    // synthetic bundle tables near their roots.
    {
      const bool renderedNodeClearance = readBoolEnv(
        "DJERD_RENDERED_NODE_CLEARANCE_FINAL", false);
      const bool directScene = readBoolEnv("DJERD_NO_CARRIER_CROSS", false);
      std::vector<LeafBundleRecord> directSceneBundles;
      std::vector<LeafBundleRecord>& clearanceBundles = directScene
        ? directSceneBundles
        : metadata.leafBundles;
      if (renderedNodeClearance && nodes.size() + clearanceBundles.size() > 1) {
        auto rerouteRenderedNodeClearance = [&]() {
          routes = crossAwareRouting
            ? routeAllEdgesCrossAware(nodes, edges, attributes)
            : (straightLineMode
              ? (arguments.edgeRouting == "straight_smart"
                  && !isStraightLineRoutingMode(arguments.mode)
                ? routeAllEdgesStraightSmart(nodes, edges, attributes)
                : routeAllEdgesStraight(edges, attributes))
              : routeAllEdges(nodes, edges, attributes, true));
          recomputeLeafBundleBboxesFromNodes(
            metadata.leafBundles, nodes, attributes);
        };
        auto measureRenderedNodeClearanceQuality = [&]() {
          std::vector<std::vector<std::string>> ignoredIdsByEdge;
          std::size_t rawCrossings = 0;
          (void)detectRouteCrossings(
            edges, routes, ignoredIdsByEdge, rawCrossings);
          LayoutQualityMetrics qm = measureLayoutQuality(
            nodes,
            edges,
            routes,
            attributes,
            directScene ? nullptr : &metadata.leafBundles,
            &metadata.clusterByModelId);
          qm.edgeCrossings = rawCrossings;
          if (!applyRenderedCarrierMetricsIfRequested(
              nodes,
              edges,
              routes,
              attributes,
              clusterByModelIdFull,
              metadata,
              qm,
              rawCrossings,
              true,
              true)) {
            qm.visualCrossings =
              qm.edgeCrossings
              + qm.edgeNodeIntersections
              + qm.nodeOverlaps
              + qm.bundleEdgeIntersections
              + qm.bundleNodeOverlaps;
          }
          return qm;
        };

        const LayoutQualityMetrics baseClearanceQuality =
          measureRenderedNodeClearanceQuality();
        if (baseClearanceQuality.nodeSpacingOverlaps > 0) {
          std::vector<std::pair<double, double>> savedClearancePositions;
          savedClearancePositions.reserve(nodes.size());
          for (const NodeRecord& node : nodes) {
            savedClearancePositions.emplace_back(
              attributes.x(node.handle), attributes.y(node.handle));
          }
          const auto savedClearanceRoutes = routes;
          const LayoutRunMetadata savedClearanceMetadata = metadata;

          const int maxBatches = static_cast<int>(readDoubleEnv(
            "DJERD_RENDERED_NODE_CLEARANCE_FINAL_BATCHES",
            3.0,
            1.0,
            8.0));
          const double bboxLimit = readDoubleEnv(
            "DJERD_RENDERED_NODE_CLEARANCE_FINAL_BBOX_LIMIT",
            1.01,
            1.0,
            2.0);
          const std::size_t visualSlack = static_cast<std::size_t>(
            readDoubleEnv(
              "DJERD_RENDERED_NODE_CLEARANCE_FINAL_VISUAL_SLACK",
              32.0,
              0.0,
              100000.0));
          LayoutQualityMetrics currentClearanceQuality =
            baseClearanceQuality;
          LayoutQualityMetrics attemptedClearanceQuality =
            baseClearanceQuality;
          std::size_t movedTotal = 0;
          int completedBatches = 0;
          bool accepted = false;

          for (int batch = 0; batch < maxBatches; ++batch) {
            const std::size_t moved = clearRenderedNodeClearance(
              clearanceBundles, nodes, attributes);
            if (moved == 0) break;
            movedTotal += moved;
            ++completedBatches;
            rerouteRenderedNodeClearance();
            const LayoutQualityMetrics nextClearanceQuality =
              measureRenderedNodeClearanceQuality();
            attemptedClearanceQuality = nextClearanceQuality;

            const bool clearanceOk =
              nextClearanceQuality.nodeSpacingOverlaps == 0
              && nextClearanceQuality.nodeClearanceMin + 1e-6
                >= nextClearanceQuality.nodeClearanceTarget;
            const bool visualOk =
              nextClearanceQuality.visualCrossings
                <= baseClearanceQuality.visualCrossings + visualSlack;
            const bool overlapOk =
              nextClearanceQuality.nodeOverlaps
                <= baseClearanceQuality.nodeOverlaps
              && nextClearanceQuality.bundleNodeOverlaps
                <= baseClearanceQuality.bundleNodeOverlaps;
            const bool bboxOk =
              baseClearanceQuality.boundingBoxArea <= 0.0
              || nextClearanceQuality.boundingBoxArea
                <= baseClearanceQuality.boundingBoxArea * bboxLimit;
            const bool bendOk =
              nextClearanceQuality.edgeBendTotal
                <= baseClearanceQuality.edgeBendTotal + 1e-6;
            const bool routesOk = routes.size() == edges.size();
            const bool safe =
              visualOk && overlapOk && bboxOk && bendOk && routesOk;
            const bool progressed =
              nextClearanceQuality.nodeSpacingOverlaps
                < currentClearanceQuality.nodeSpacingOverlaps
              || (
                nextClearanceQuality.nodeSpacingOverlaps
                  == currentClearanceQuality.nodeSpacingOverlaps
                && nextClearanceQuality.nodeClearanceMin
                  > currentClearanceQuality.nodeClearanceMin + 1e-6);

            std::fprintf(stderr,
              "[rendered-node-clearance-final] batch=%d/%d moved=%zu "
              "violations=%zu->%zu min=%.2f->%.2f safe=%d progress=%d.\n",
              batch + 1,
              maxBatches,
              moved,
              currentClearanceQuality.nodeSpacingOverlaps,
              nextClearanceQuality.nodeSpacingOverlaps,
              currentClearanceQuality.nodeClearanceMin,
              nextClearanceQuality.nodeClearanceMin,
              safe ? 1 : 0,
              progressed ? 1 : 0);

            if (!safe || (!clearanceOk && !progressed)) break;
            currentClearanceQuality = nextClearanceQuality;
            if (clearanceOk) {
              accepted = true;
              break;
            }
          }

          if (!accepted) {
            for (std::size_t i = 0;
                 i < nodes.size() && i < savedClearancePositions.size();
                 ++i) {
              attributes.x(nodes[i].handle) = savedClearancePositions[i].first;
              attributes.y(nodes[i].handle) = savedClearancePositions[i].second;
            }
            routes = savedClearanceRoutes;
            metadata = savedClearanceMetadata;
          }
          const LayoutQualityMetrics& reportedClearanceQuality =
            accepted
              ? currentClearanceQuality
              : attemptedClearanceQuality;
          std::fprintf(stderr,
            "[rendered-node-clearance-final] %s batches=%d/%d moved=%zu "
            "violations=%zu->%zu min=%.2f->%.2f target=%.2f "
            "visual=%zu->%zu edgeNode=%zu->%zu bundleNode=%zu->%zu "
            "bbox=%.3fB->%.3fB bend=%.2f->%.2f.\n",
            accepted ? "accepted" : "reverted",
            completedBatches,
            maxBatches,
            movedTotal,
            baseClearanceQuality.nodeSpacingOverlaps,
            reportedClearanceQuality.nodeSpacingOverlaps,
            baseClearanceQuality.nodeClearanceMin,
            reportedClearanceQuality.nodeClearanceMin,
            reportedClearanceQuality.nodeClearanceTarget,
            baseClearanceQuality.visualCrossings,
            reportedClearanceQuality.visualCrossings,
            baseClearanceQuality.edgeNodeIntersections,
            reportedClearanceQuality.edgeNodeIntersections,
            baseClearanceQuality.bundleNodeOverlaps,
            reportedClearanceQuality.bundleNodeOverlaps,
            baseClearanceQuality.boundingBoxArea / 1e9,
            reportedClearanceQuality.boundingBoxArea / 1e9,
            baseClearanceQuality.edgeBendTotal,
            reportedClearanceQuality.edgeBendTotal);
        }
      }
    }

    repairCanonicalRouteObstaclesIfRequested(
      nodes, edges, routes, attributes, clusterByModelIdFull, metadata);

    // Final route/bundle quality recompute. Several late visual passes move
    // nodes or sync route endpoints after the earlier quality snapshot, and
    // the emitted routedEdges must be scored exactly as rendered.
    {
      crossingIdsByEdge.assign(edges.size(), {});
      totalRouteCrossings = 0;
      crossings =
        detectRouteCrossings(edges, routes, crossingIdsByEdge, totalRouteCrossings);
      metadata.rawRouteCrossings = totalRouteCrossings;
      quality = measureLayoutQuality(
        nodes, edges, routes, attributes, &metadata.leafBundles,
        &metadata.clusterByModelId);
      quality.edgeCrossings = totalRouteCrossings;

      if (!applyRenderedCarrierMetricsIfRequested(
          nodes,
          edges,
          routes,
          attributes,
          clusterByModelIdFull,
          metadata,
          quality,
          totalRouteCrossings)) {
        applyFinalCarrierMetricsIfRequested(
          nodes,
          edges,
          routes,
          attributes,
          clusterByModelIdFull,
          metadata,
          quality,
          totalRouteCrossings);
      }

      quality.visualCrossings =
        quality.edgeCrossings
        + quality.edgeNodeIntersections
        + quality.nodeOverlaps
        + quality.bundleEdgeIntersections
        + quality.bundleNodeOverlaps;
    }

    measureCanonicalCrossingDrawing(
      metadata.canonicalCrossing,
      nodes,
      edges,
      routes,
      attributes);

    const Bounds bounds = measureBounds(nodes, routes, attributes);
    writeLayoutJson(
      std::cout,
      arguments.mode,
      metadata,
      nodes,
      edges,
      attributes,
      routes,
      crossings,
      crossingIdsByEdge,
      quality,
      bounds);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << std::endl;
    return 1;
  }
}
