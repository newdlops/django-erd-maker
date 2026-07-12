#include "types.h"
#include "geometry.h"
#include "io.h"
#include "clusterGraph.h"
#include "canonicalCrossingMetrics.h"
#include "crossingLowerBound.h"

#include <cstdlib>
#include <numeric>

#include <ogdf/basic/Graph.h>
#include <ogdf/basic/basic.h>
#include <ogdf/basic/GraphAttributes.h>
#include <ogdf/energybased/DavidsonHarelLayout.h>
#include <ogdf/energybased/FMMMLayout.h>
#include <ogdf/energybased/FastMultipoleEmbedder.h>
#include <ogdf/energybased/PivotMDS.h>
#include <ogdf/energybased/StressMinimization.h>
#include <ogdf/energybased/fmmm/FMMMOptions.h>
#include <ogdf/layered/BarycenterHeuristic.h>
#include <ogdf/layered/GreedyInsertHeuristic.h>
#include <ogdf/layered/GreedySwitchHeuristic.h>
#include <ogdf/layered/GridSifting.h>
#include <ogdf/layered/MedianHeuristic.h>
#include <ogdf/layered/OptimalHierarchyLayout.h>
#include <ogdf/layered/OptimalRanking.h>
#include <ogdf/layered/SiftingHeuristic.h>
#include <ogdf/layered/SplitHeuristic.h>
#include <ogdf/layered/SugiyamaLayout.h>
#include <ogdf/misclayout/CircularLayout.h>
#include <ogdf/misclayout/LinearLayout.h>
#include <ogdf/orthogonal/OrthoLayout.h>
#include <ogdf/planarlayout/PlanarDrawLayout.h>
#include <ogdf/planarlayout/PlanarStraightLayout.h>
#include <ogdf/planarlayout/SchnyderLayout.h>
#include <ogdf/planarity/PlanarSubgraphFast.h>
#include <ogdf/planarity/PlanarizationGridLayout.h>
#include <ogdf/planarity/PlanarizationLayout.h>
#include <ogdf/planarity/RemoveReinsertType.h>
#include <ogdf/planarity/SubgraphPlanarizer.h>
#include <ogdf/planarity/VariableEmbeddingInserter.h>
#include <ogdf/tree/RadialTreeLayout.h>
#include <ogdf/tree/TreeLayout.h>
#include <ogdf/uml/OrthoLayoutUML.h>
#include <ogdf/uml/PlanarizationLayoutUML.h>
#include <ogdf/upward/LayerBasedUPRLayout.h>
#include <ogdf/upward/UpwardPlanarizationLayout.h>
#include <ogdf/upward/VisibilityLayout.h>

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

namespace djerd {

bool isConstrainedForceMode(const std::string& mode) {
  return mode == "constrained_force" || mode == "constrained_force_straight";
}

bool isStraightLineRoutingMode(const std::string& mode) {
  return mode == "constrained_force_straight";
}

bool isSupportedMode(const std::string& mode) {
  return mode == "hierarchical"
    || mode == "hierarchical_barycenter"
    || mode == "hierarchical_sifting"
    || mode == "hierarchical_global_sifting"
    || mode == "hierarchical_greedy_insert"
    || mode == "hierarchical_greedy_switch"
    || mode == "hierarchical_grid_sifting"
    || mode == "hierarchical_split"
    || mode == "circular"
    || mode == "linear"
    || mode == "clustered"
    || mode == "constrained_force"
    || mode == "constrained_force_straight"
    || mode == "fmmm"
    || mode == "fast_multipole"
    || mode == "fast_multipole_multilevel"
    || mode == "stress_minimization"
    || mode == "pivot_mds"
    || mode == "davidson_harel"
    || mode == "planarization"
    || mode == "planarization_grid"
    || mode == "ortho"
    || mode == "planar_draw"
    || mode == "planar_straight"
    || mode == "schnyder"
    || mode == "upward_layer_based"
    || mode == "upward_planarization"
    || mode == "visibility"
    || mode == "cluster_planarization"
    || mode == "cluster_ortho"
    || mode == "uml_ortho"
    || mode == "uml_planarization"
    || mode == "tree"
    || mode == "radial_tree";
}

std::size_t idealThreadCount() {
  const unsigned int detected = std::thread::hardware_concurrency();
  return std::max<std::size_t>(1, std::min<std::size_t>(8, detected == 0 ? 1 : detected));
}

double readDoubleEnv(
  const char* name,
  double fallback,
  double minValue,
  double maxValue) {
  const char* raw = std::getenv(name);
  if (raw == nullptr || raw[0] == '\0') {
    return fallback;
  }
  char* end = nullptr;
  const double parsed = std::strtod(raw, &end);
  if (end == raw || !std::isfinite(parsed)) {
    return fallback;
  }
  return std::clamp(parsed, minValue, maxValue);
}

bool readBoolEnv(const char* name, bool fallback) {
  const char* raw = std::getenv(name);
  if (raw == nullptr || raw[0] == '\0') {
    return fallback;
  }
  std::string value(raw);
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value != "0" && value != "false" && value != "no";
}

struct CanonicalTopologyFingerprint {
  std::string value;
  std::size_t edgeCount = 0;
  std::size_t nodeCount = 0;
};

CanonicalTopologyFingerprint fingerprintCanonicalTopology(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges) {
  std::vector<std::string> nodeIds;
  nodeIds.reserve(nodes.size());
  for (const NodeRecord& node : nodes) {
    nodeIds.push_back(node.modelId);
  }
  std::sort(nodeIds.begin(), nodeIds.end());

  std::set<std::pair<std::string, std::string>> edgePairs;
  for (const EdgeRecord& edge : edges) {
    if (
        edge.sourceModelId.empty()
        || edge.targetModelId.empty()
        || edge.sourceModelId == edge.targetModelId) {
      continue;
    }
    edgePairs.insert(std::minmax(edge.sourceModelId, edge.targetModelId));
  }

  uint64_t first = UINT64_C(1469598103934665603);
  uint64_t second = UINT64_C(7809847782465536322);
  auto mixByte = [&](unsigned char byte) {
    first ^= static_cast<uint64_t>(byte);
    first *= UINT64_C(1099511628211);
    second ^= static_cast<uint64_t>(byte) + UINT64_C(0x9e);
    second *= UINT64_C(14029467366897019727);
  };
  auto mixString = [&](const std::string& value) {
    uint64_t length = value.size();
    for (int shift = 0; shift < 8; ++shift) {
      mixByte(static_cast<unsigned char>((length >> (shift * 8)) & 0xffU));
    }
    for (const unsigned char byte : value) {
      mixByte(byte);
    }
  };
  mixString(kCrossingLowerBoundVersion);
  for (const std::string& nodeId : nodeIds) {
    mixString("N");
    mixString(nodeId);
  }
  for (const auto& edgePair : edgePairs) {
    mixString("E");
    mixString(edgePair.first);
    mixString(edgePair.second);
  }

  std::ostringstream fingerprint;
  fingerprint << std::hex << std::setfill('0')
              << std::setw(16) << first
              << std::setw(16) << second;
  return {
    fingerprint.str(),
    edgePairs.size(),
    nodeIds.size(),
  };
}

std::filesystem::path canonicalCrossingCachePath(
  const CanonicalTopologyFingerprint& fingerprint) {
  return std::filesystem::temp_directory_path()
    / ("django-erd-crossing-lb-v"
      + std::string(kCrossingLowerBoundVersion)
      + "-" + fingerprint.value + ".txt");
}

bool readCanonicalCrossingCache(
  const std::filesystem::path& cachePath,
  const CanonicalTopologyFingerprint& fingerprint,
  CanonicalCrossingMetadata& metadata) {
  std::ifstream stream(cachePath);
  if (!stream) {
    return false;
  }
  std::string cachedFingerprint;
  std::string version;
  std::string method;
  CanonicalCrossingMetadata cached;
  if (!(stream
        >> cachedFingerprint
        >> version
        >> method
        >> cached.nodeCount
        >> cached.edgeCount
        >> cached.lowerBound
        >> cached.k3nContribution
        >> cached.k3nCertificates
        >> cached.kuratowskiContribution
        >> cached.kuratowskiCertificates)) {
    return false;
  }
  if (
      cachedFingerprint != fingerprint.value
      || version != kCrossingLowerBoundVersion
      || method != kCrossingLowerBoundMethod
      || cached.nodeCount != fingerprint.nodeCount
      || cached.edgeCount != fingerprint.edgeCount
      || cached.lowerBound
        != cached.k3nContribution + cached.kuratowskiContribution) {
    return false;
  }
  cached.available = true;
  cached.certifierVersion = std::move(version);
  cached.method = std::move(method);
  metadata = std::move(cached);
  return true;
}

void writeCanonicalCrossingCache(
  const std::filesystem::path& cachePath,
  const CanonicalTopologyFingerprint& fingerprint,
  const CanonicalCrossingMetadata& metadata) {
  const auto unique = std::chrono::steady_clock::now()
    .time_since_epoch().count();
  std::filesystem::path temporaryPath = cachePath;
  temporaryPath += "." + std::to_string(unique) + ".tmp";
  {
    std::ofstream stream(temporaryPath, std::ios::trunc);
    if (!stream) {
      return;
    }
    stream
      << fingerprint.value << ' '
      << metadata.certifierVersion << ' '
      << metadata.method << ' '
      << metadata.nodeCount << ' '
      << metadata.edgeCount << ' '
      << metadata.lowerBound << ' '
      << metadata.k3nContribution << ' '
      << metadata.k3nCertificates << ' '
      << metadata.kuratowskiContribution << ' '
      << metadata.kuratowskiCertificates << '\n';
    if (!stream) {
      std::error_code removeError;
      std::filesystem::remove(temporaryPath, removeError);
      return;
    }
  }
  std::error_code renameError;
  std::filesystem::rename(temporaryPath, cachePath, renameError);
  if (renameError) {
    std::error_code removeError;
    std::filesystem::remove(temporaryPath, removeError);
  }
}

CanonicalCrossingMetadata certifyCanonicalCrossingTopology(
  const ogdf::Graph& graph,
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges) {
  CanonicalCrossingMetadata metadata;
  if (!readBoolEnv("DJERD_CANONICAL_CROSSING_CERTIFIER", true)) {
    return metadata;
  }

  const CanonicalTopologyFingerprint fingerprint =
    fingerprintCanonicalTopology(nodes, edges);
  const bool useCache = readBoolEnv("DJERD_CANONICAL_CROSSING_CACHE", true);
  std::filesystem::path cachePath;
  if (useCache) {
    try {
      cachePath = canonicalCrossingCachePath(fingerprint);
      if (readCanonicalCrossingCache(cachePath, fingerprint, metadata)) {
        std::fprintf(stderr,
          "[canonical-crossing] cache hit lowerBound=%zu "
          "(nodes=%zu, edges=%zu).\n",
          metadata.lowerBound,
          metadata.nodeCount,
          metadata.edgeCount);
        return metadata;
      }
    } catch (const std::exception& error) {
      std::fprintf(stderr,
        "[canonical-crossing] cache lookup skipped: %s\n",
        error.what());
      cachePath.clear();
    }
  }

  ogdf::NodeArray<std::string> nodeIds(graph, std::string{});
  for (const NodeRecord& node : nodes) {
    if (node.handle != nullptr) {
      nodeIds[node.handle] = node.modelId;
    }
  }
  ogdf::EdgeArray<std::string> edgeIds(graph, std::string{});
  for (const EdgeRecord& edge : edges) {
    if (edge.handle == nullptr) {
      continue;
    }
    std::string& representativeId = edgeIds[edge.handle];
    if (representativeId.empty() || edge.edgeId < representativeId) {
      representativeId = edge.edgeId;
    }
  }

  const CrossingLowerBoundReport report = computeCertifiedCrossingLowerBound(
    graph,
    &nodeIds,
    &edgeIds);
  if (!report.invariantsVerified) {
    std::fprintf(stderr,
      "[canonical-crossing] certifier invariants failed; bound omitted.\n");
    return metadata;
  }

  metadata.available = true;
  metadata.certifierVersion = report.version;
  metadata.method = report.method;
  metadata.nodeCount = report.nodeCount;
  metadata.edgeCount = report.edgeCount;
  metadata.lowerBound = report.totalLowerBound;
  metadata.k3nContribution = report.k3nContribution;
  metadata.k3nCertificates = report.k3nCertificateCount;
  metadata.kuratowskiContribution = report.kuratowskiContribution;
  metadata.kuratowskiCertificates = report.kuratowskiCertificateCount;
  if (useCache && !cachePath.empty()) {
    writeCanonicalCrossingCache(cachePath, fingerprint, metadata);
  }
  std::fprintf(stderr,
    "[canonical-crossing] certified lowerBound=%zu "
    "(K3,n=%zu/%zu, Kuratowski=%zu/%zu, nodes=%zu, edges=%zu, "
    "tripleOccurrences=%zu, planarityCalls=%zu, workLimit=%d).\n",
    report.totalLowerBound,
    report.k3nContribution,
    report.k3nCertificateCount,
    report.kuratowskiContribution,
    report.kuratowskiCertificateCount,
    report.nodeCount,
    report.edgeCount,
    report.tripleOccurrences,
    report.planarityCalls,
    report.workLimitReached ? 1 : 0);
  return metadata;
}

void measureCanonicalCrossingDrawing(
  CanonicalCrossingMetadata& metadata,
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes) {
  if (!metadata.available) {
    return;
  }

  const CanonicalCrossingMetrics metrics = measureCanonicalCrossingMetrics(
    nodes,
    edges,
    routes,
    attributes);
  metadata.routeCrossingPoints = metrics.properCrossingPoints.size();
  metadata.routeCrossingPairs = metrics.crossingEdgePairs.size();
  metadata.completeRoutes =
    metrics.allCanonicalRoutesComplete
    && metrics.canonicalEdgeCount == metadata.edgeCount;
  metadata.nonProperContacts =
    metrics.invariantViolationCount
    + metrics.degenerateSegmentCount
    + metrics.collinearOverlapCount
    + metrics.nonProperContactCount
    + metrics.selfIntersectionCount
    + metrics.adjacentEdgeIntersectionCount
    + metrics.nonIncidentNodeHits.size();
  metadata.invariantViolations = metrics.invariantViolationCount;
  metadata.degenerateSegments = metrics.degenerateSegmentCount;
  metadata.collinearOverlaps = metrics.collinearOverlapCount;
  metadata.pointContacts = metrics.nonProperContactCount;
  metadata.selfIntersections = metrics.selfIntersectionCount;
  metadata.adjacentEdgeIntersections = metrics.adjacentEdgeIntersectionCount;
  metadata.nonIncidentNodeHits = metrics.nonIncidentNodeHits.size();
  metadata.properDrawing =
    metrics.properDrawing
    && metadata.completeRoutes;
  metadata.boundViolation =
    metadata.properDrawing
    && metadata.routeCrossingPairs < metadata.lowerBound;

  if (metadata.properDrawing && !metadata.boundViolation) {
    metadata.gap = metadata.routeCrossingPairs - metadata.lowerBound;
    if (metadata.routeCrossingPairs == 0) {
      metadata.optimality = metadata.lowerBound == 0 ? 1.0 : 0.0;
    } else {
      metadata.optimality = std::min(
        1.0,
        static_cast<double>(metadata.lowerBound)
          / static_cast<double>(metadata.routeCrossingPairs));
    }
  }

  std::fprintf(stderr,
    "[canonical-crossing] route pairs=%zu points=%zu lowerBound=%zu "
    "proper=%d complete=%d contacts=%zu "
    "categories={invariant=%zu,degenerate=%zu,overlap=%zu,point=%zu,"
    "self=%zu,adjacent=%zu,nodeHit=%zu}%s.\n",
    metadata.routeCrossingPairs,
    metadata.routeCrossingPoints,
    metadata.lowerBound,
    metadata.properDrawing ? 1 : 0,
    metadata.completeRoutes ? 1 : 0,
    metadata.nonProperContacts,
    metadata.invariantViolations,
    metadata.degenerateSegments,
    metadata.collinearOverlaps,
    metadata.pointContacts,
    metadata.selfIntersections,
    metadata.adjacentEdgeIntersections,
    metadata.nonIncidentNodeHits,
    metadata.boundViolation ? " BOUND-VIOLATION" : "");
}

double visualNodeMargin() {
  return readDoubleEnv("DJERD_NODE_VISUAL_MARGIN", 8.0, 0.0, 240.0);
}

double leafBundleVisualMargin() {
  return readDoubleEnv("DJERD_LEAF_BUNDLE_VISUAL_MARGIN", 32.0, 0.0, 480.0);
}

std::vector<std::vector<RoutePoint>> routeAllEdgesStraight(
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

std::vector<std::vector<RoutePoint>> routeAllEdgesCrossAware(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

std::vector<EdgeCrossingRecord> detectRouteCrossings(
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::vector<RoutePoint>>& routes,
  std::vector<std::vector<std::string>>& crossingIdsByEdge,
  std::size_t& totalCrossings);

void packDisconnectedComponents(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

void enforceNodeSeparationStrong(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes);

void pullLowDegreeNodesInward(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  int maxIterations = 24,
  std::size_t degreeThreshold = 2,
  double damping = 0.35);

void compactClusterOutliers(
  const std::vector<NodeRecord>& nodes,
  const std::unordered_map<std::string, std::string>& clusterByModelId,
  ogdf::GraphAttributes& attributes,
  double outlierMedianMultiplier = 1.8);

void compactDistantConnectedNodes(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

bool compactExcessiveLayoutFootprint(
  const std::string& mode,
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

bool compactRigidLayoutFootprint(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes);

void recomputeLeafBundleBboxesFromNodes(
  std::vector<LeafBundleRecord>& leafBundles,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes);

std::size_t clearLeafBundleNodeMargins(
  std::vector<LeafBundleRecord>& leafBundles,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  bool logResult = true);

std::size_t attachIsolatedNodesByName(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

std::size_t compactIsolatedBBoxOutliers(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

std::size_t compactSidecarBBoxComponents(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

std::unordered_set<std::string> absorbedLeafBundleIds(
  const std::vector<LeafBundleRecord>& leafBundles);

ogdf::SubgraphPlanarizer* createBoundedSubgraphPlanarizer() {
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
  return planarizer;
}

ogdf::SubgraphPlanarizer* createHighQualityPlanarizer() {
  auto* planarizer = new ogdf::SubgraphPlanarizer();
  auto* subgraph = new ogdf::PlanarSubgraphFast<int>();
  auto* inserter = new ogdf::VariableEmbeddingInserter();
  const unsigned threads = static_cast<unsigned>(std::max<std::size_t>(1, idealThreadCount()));
  subgraph->runs(32);
  subgraph->maxThreads(threads);
  inserter->removeReinsert(ogdf::RemoveReinsertType::IncInserted);
  planarizer->setSubgraph(subgraph);
  planarizer->setInserter(inserter);
  planarizer->permutations(16);
  planarizer->maxThreads(threads);
  return planarizer;
}

void sanitizeLayoutGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  for (const NodeRecord& node : nodes) {
    attributes.width(node.handle) = sanitizeNodeWidth(node, attributes);
    attributes.height(node.handle) = sanitizeNodeHeight(node, attributes);
    attributes.x(node.handle) = sanitizeNodeCenterX(node, attributes);
    attributes.y(node.handle) = sanitizeNodeCenterY(node, attributes);
  }

  for (const EdgeRecord& edge : edges) {
    ogdf::DPolyline sanitizedBends;
    for (const ogdf::DPoint& bend : attributes.bends(edge.handle)) {
      if (!isFiniteCoordinate(bend.m_x) || !isFiniteCoordinate(bend.m_y)) {
        continue;
      }

      sanitizedBends.pushBack(bend);
    }

    attributes.bends(edge.handle) = sanitizedBends;
  }
}

bool isSugiyamaMode(const std::string& mode) {
  return mode == "hierarchical"
    || mode == "hierarchical_barycenter"
    || mode == "hierarchical_sifting"
    || mode == "hierarchical_global_sifting"
    || mode == "hierarchical_greedy_insert"
    || mode == "hierarchical_greedy_switch"
    || mode == "hierarchical_grid_sifting"
    || mode == "hierarchical_split";
}

void runSugiyamaLayout(const std::string& mode, ogdf::GraphAttributes& attributes) {
  ogdf::SugiyamaLayout layout;
  const bool expensiveCrossMin =
    mode == "hierarchical_sifting"
    || mode == "hierarchical_global_sifting"
    || mode == "hierarchical_greedy_insert"
    || mode == "hierarchical_grid_sifting"
    || mode == "hierarchical_split";
  layout.setRanking(new ogdf::OptimalRanking());
  layout.runs(expensiveCrossMin ? 1 : 2);
  layout.fails(expensiveCrossMin ? 1 : 4);
  layout.transpose(true);

  if (mode == "hierarchical_barycenter") {
    layout.setCrossMin(new ogdf::BarycenterHeuristic());
  } else if (mode == "hierarchical_sifting") {
    layout.setCrossMin(new ogdf::SiftingHeuristic());
  } else if (mode == "hierarchical_global_sifting") {
    auto* crossMin = new ogdf::GlobalSifting();
    crossMin->nRepeats(1);
    layout.setCrossMin(crossMin);
  } else if (mode == "hierarchical_greedy_insert") {
    layout.setCrossMin(new ogdf::GreedyInsertHeuristic());
  } else if (mode == "hierarchical_greedy_switch") {
    layout.setCrossMin(new ogdf::GreedySwitchHeuristic());
  } else if (mode == "hierarchical_grid_sifting") {
    auto* crossMin = new ogdf::GridSifting();
    crossMin->verticalStepsBound(3);
    layout.setCrossMin(crossMin);
  } else if (mode == "hierarchical_split") {
    layout.setCrossMin(new ogdf::SplitHeuristic());
  } else {
    layout.setCrossMin(new ogdf::MedianHeuristic());
  }

  auto* hierarchy = new ogdf::OptimalHierarchyLayout();
  hierarchy->layerDistance(140.0);
  hierarchy->nodeDistance(64.0);
  hierarchy->weightBalancing(0.72);
  layout.setLayout(hierarchy);
  layout.arrangeCCs(true);
  layout.call(attributes);
}

std::vector<std::vector<std::size_t>> buildProjectedForestAdjacency(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges) {
  std::vector<std::vector<std::size_t>> adjacency(nodes.size());
  std::unordered_map<ogdf::node, std::size_t> indicesByNode;
  indicesByNode.reserve(nodes.size());

  for (std::size_t index = 0; index < nodes.size(); ++index) {
    indicesByNode.emplace(nodes[index].handle, index);
  }

  DisjointSet forest(nodes.size());
  for (const EdgeRecord& edge : edges) {
    const auto source = indicesByNode.find(edge.sourceHandle);
    const auto target = indicesByNode.find(edge.targetHandle);

    if (
      source == indicesByNode.end()
      || target == indicesByNode.end()
      || source->second == target->second) {
      continue;
    }

    if (!forest.unite(source->second, target->second)) {
      continue;
    }

    adjacency[source->second].push_back(target->second);
    adjacency[target->second].push_back(source->second);
  }

  return adjacency;
}

std::size_t chooseTreeRoot(
  const std::vector<std::size_t>& component,
  const std::vector<std::vector<std::size_t>>& adjacency) {
  return *std::max_element(
    component.begin(),
    component.end(),
    [&](std::size_t left, std::size_t right) {
      return adjacency[left].size() < adjacency[right].size();
    });
}

std::vector<std::vector<std::size_t>> collectTreeLevels(
  std::size_t root,
  const std::vector<std::vector<std::size_t>>& adjacency,
  std::vector<bool>& visited) {
  std::vector<std::vector<std::size_t>> levels;
  std::queue<std::pair<std::size_t, std::size_t>> pending;
  pending.emplace(root, 0);
  visited[root] = true;

  while (!pending.empty()) {
    const auto [nodeIndex, depth] = pending.front();
    pending.pop();

    if (levels.size() <= depth) {
      levels.emplace_back();
    }
    levels[depth].push_back(nodeIndex);

    for (std::size_t next : adjacency[nodeIndex]) {
      if (visited[next]) {
        continue;
      }
      visited[next] = true;
      pending.emplace(next, depth + 1);
    }
  }

  return levels;
}

void applyLayeredTreeCoordinates(
  const std::vector<NodeRecord>& nodes,
  const std::vector<std::vector<std::size_t>>& levels,
  double componentY,
  ogdf::GraphAttributes& attributes,
  double& componentHeight) {
  componentHeight = 0.0;

  for (std::size_t depth = 0; depth < levels.size(); ++depth) {
    const std::vector<std::size_t>& level = levels[depth];
    double y = componentY;

    for (std::size_t nodeIndex : level) {
      const NodeRecord& node = nodes[nodeIndex];
      const double height = sanitizeNodeHeight(node, attributes);
      attributes.x(node.handle) = depth * kTreeLevelDistance;
      attributes.y(node.handle) = y + height / 2.0;
      y += height + kTreeNodeDistance;
    }

    componentHeight = std::max(componentHeight, y - componentY);
  }
}

void applyRadialTreeCoordinates(
  const std::vector<NodeRecord>& nodes,
  const std::vector<std::vector<std::size_t>>& levels,
  double componentX,
  ogdf::GraphAttributes& attributes,
  double& componentWidth) {
  const double maxRadius =
    std::max(kRadialLevelDistance, static_cast<double>(levels.size()) * kRadialLevelDistance);
  const double centerX = componentX + maxRadius;
  const double centerY = maxRadius;
  constexpr double tau = 6.28318530717958647692;

  componentWidth = maxRadius * 2.0;

  for (std::size_t depth = 0; depth < levels.size(); ++depth) {
    const std::vector<std::size_t>& level = levels[depth];
    const double radius = depth == 0 ? 0.0 : static_cast<double>(depth) * kRadialLevelDistance;

    for (std::size_t index = 0; index < level.size(); ++index) {
      const NodeRecord& node = nodes[level[index]];
      const double angle = level.size() <= 1
        ? 0.0
        : tau * static_cast<double>(index) / static_cast<double>(level.size());
      attributes.x(node.handle) = centerX + radius * std::cos(angle);
      attributes.y(node.handle) = centerY + radius * std::sin(angle);
    }
  }
}

void runProjectedTreeLayout(
  const std::string& mode,
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  const std::vector<std::vector<std::size_t>> adjacency =
    buildProjectedForestAdjacency(nodes, edges);
  std::vector<bool> componentSeen(nodes.size(), false);
  double nextTreeY = 0.0;
  double nextRadialX = 0.0;

  for (std::size_t start = 0; start < nodes.size(); ++start) {
    if (componentSeen[start]) {
      continue;
    }

    std::vector<std::size_t> component;
    std::queue<std::size_t> pending;
    pending.push(start);
    componentSeen[start] = true;

    while (!pending.empty()) {
      const std::size_t nodeIndex = pending.front();
      pending.pop();
      component.push_back(nodeIndex);

      for (std::size_t next : adjacency[nodeIndex]) {
        if (componentSeen[next]) {
          continue;
        }
        componentSeen[next] = true;
        pending.push(next);
      }
    }

    std::vector<bool> levelSeen(nodes.size(), false);
    const std::size_t root = chooseTreeRoot(component, adjacency);
    const std::vector<std::vector<std::size_t>> levels =
      collectTreeLevels(root, adjacency, levelSeen);

    if (mode == "radial_tree") {
      double componentWidth = 0.0;
      applyRadialTreeCoordinates(nodes, levels, nextRadialX, attributes, componentWidth);
      nextRadialX += componentWidth + kRadialComponentDistance;
    } else {
      double componentHeight = 0.0;
      applyLayeredTreeCoordinates(nodes, levels, nextTreeY, attributes, componentHeight);
      nextTreeY += componentHeight + kTreeComponentDistance;
    }
  }
}

void runFastMultipoleLayout(
  ogdf::GraphAttributes& attributes,
  uint32_t iterations,
  uint32_t precision,
  bool randomize) {
  ogdf::FastMultipoleEmbedder layout;
  layout.setNumIterations(iterations);
  layout.setMultipolePrec(precision);
  layout.setDefaultEdgeLength(220.0f);
  layout.setDefaultNodeSize(72.0f);
  layout.setRandomize(randomize);
  layout.setNumberOfThreads(static_cast<uint32_t>(idealThreadCount()));
  layout.call(attributes);
}

struct ClusterRunOptions {
  std::string innerMode;
  std::string metaMode = "fmmm";
  double innerLayerDistance = 140.0;
  double innerNodeDistance = 64.0;
  double innerFmmEdgeLength = 220.0;
  double innerFmmNodeSize = 72.0;
  uint32_t innerFmmIterations = 300;
  double interClusterPadding = 240.0;
  double metaUnitEdgeLength = 1200.0;
  double metaLayerDistance = 320.0;
  double metaNodeDistance = 200.0;
};

bool hasMeaningfulClusters(const std::vector<NodeRecord>& nodes) {
  std::unordered_map<std::string, std::size_t> counts;
  for (const NodeRecord& node : nodes) {
    counts[node.appLabel]++;
  }
  if (counts.empty() || nodes.size() < 8) {
    return false;
  }
  std::size_t nonEmptyClusters = 0;
  std::size_t largestCluster = 0;
  for (const auto& [label, count] : counts) {
    if (label.empty() || count == 0) {
      continue;
    }
    nonEmptyClusters++;
    largestCluster = std::max(largestCluster, count);
  }
  if (nonEmptyClusters < 2) {
    return false;
  }
  const double dominanceRatio =
    static_cast<double>(largestCluster) / static_cast<double>(nodes.size());
  return dominanceRatio < 0.85;
}

// Forward declaration: defined later in this file. assignHubExcludedBccLabels
// calls back into the standard BCC pass on a residual subgraph.
std::vector<std::string> assignBiconnectedClusterLabels(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  std::size_t& outBridgeCount,
  std::size_t& outBccCount,
  std::size_t& outLargestClusterSize);

// Identify dominant hubs by degree. Uses largest-gap detection in the top of
// the degree-sorted list: pick hubs until the next ratio drops below 1.5, cap
// at 5, and require the top hub to be at least 1.5× the second-ranked node.
// Returns empty when no clear hub exists (uniform degree distribution).
std::vector<std::size_t> findDominantHubs(
  const std::vector<std::vector<std::size_t>>& adj) {
  const std::size_t n = adj.size();
  if (n < 8) {
    return {};
  }
  std::vector<std::pair<std::size_t, std::size_t>> degOrder;
  degOrder.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    degOrder.emplace_back(adj[i].size(), i);
  }
  std::sort(degOrder.begin(), degOrder.end(),
    [](const auto& a, const auto& b) { return a.first > b.first; });

  if (degOrder[0].first < 8 || degOrder.size() < 2) {
    return {};
  }
  const double topRatio = static_cast<double>(degOrder[0].first)
    / std::max<std::size_t>(degOrder[1].first, 1);
  if (topRatio < 1.5) {
    return {};
  }

  const std::size_t maxHubs = std::min<std::size_t>(5, n);
  std::vector<std::size_t> hubs;
  hubs.push_back(degOrder[0].second);
  for (std::size_t k = 1; k < maxHubs; ++k) {
    const std::size_t nextIdx = std::min(k + 1, degOrder.size() - 1);
    const double ratio = static_cast<double>(degOrder[k].first)
      / std::max<std::size_t>(degOrder[nextIdx].first, 1);
    if (ratio < 1.5) {
      break;
    }
    hubs.push_back(degOrder[k].second);
  }
  return hubs;
}

// BCC variant that progressively peels off the highest-degree nodes until the
// residual graph shows useful structure (bridges appear, or the largest BCC
// drops below 40% of the residual). Real-world ERDs have a few dominant hub
// entities; removing them lets BCC see the structural backbone underneath.
// Hubs become their own singleton clusters; FMMM meta-layout will place them
// centrally because they share inter-cluster edges with everyone.
std::vector<std::string> assignHubExcludedBccLabels(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  std::size_t& outHubCount,
  std::size_t& outResidualBridgeCount,
  std::size_t& outResidualBccCount) {
  outHubCount = 0;
  outResidualBridgeCount = 0;
  outResidualBccCount = 0;
  const std::size_t n = nodes.size();
  std::vector<std::string> assigned(n);
  if (n == 0) {
    return assigned;
  }

  std::unordered_map<std::string, std::size_t> idToIdx;
  idToIdx.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    idToIdx[nodes[i].modelId] = i;
  }

  std::vector<std::vector<std::size_t>> adj(n);
  for (const EdgeRecord& edge : edges) {
    auto srcIt = idToIdx.find(edge.sourceModelId);
    auto tgtIt = idToIdx.find(edge.targetModelId);
    if (srcIt == idToIdx.end() || tgtIt == idToIdx.end() || srcIt->second == tgtIt->second) {
      continue;
    }
    adj[srcIt->second].push_back(tgtIt->second);
    adj[tgtIt->second].push_back(srcIt->second);
  }

  // Degree-sorted indices for hub-peeling.
  std::vector<std::pair<std::size_t, std::size_t>> degOrder;
  degOrder.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    degOrder.emplace_back(adj[i].size(), i);
  }
  std::sort(degOrder.begin(), degOrder.end(),
    [](const auto& a, const auto& b) { return a.first > b.first; });

  // Bail out if no clear hub exists.
  if (degOrder.empty() || degOrder[0].first < 8) {
    for (std::size_t i = 0; i < n; ++i) {
      assigned[i] = "_bcc_0";
    }
    return assigned;
  }

  // Cap hub peeling at 3 — beyond that the residual graph still rarely
  // breaks up on densely biconnected ERDs, and the resulting cluster count
  // makes the meta-layout swap pass O(C^2 * M^2) blow up at runtime.
  const std::size_t maxHubs = std::min<std::size_t>(3, n / 100);
  std::unordered_set<std::size_t> hubSet;
  std::vector<std::size_t> hubs;
  std::vector<std::string> residualLabels;
  std::vector<NodeRecord> residualNodes;
  std::size_t resBridges = 0;
  std::size_t resBcc = 0;
  std::size_t resLargest = 0;

  for (std::size_t step = 0; step < maxHubs; ++step) {
    hubSet.insert(degOrder[step].second);
    hubs.push_back(degOrder[step].second);

    residualNodes.clear();
    residualNodes.reserve(n - hubs.size());
    for (std::size_t i = 0; i < n; ++i) {
      if (hubSet.count(i) == 0) {
        residualNodes.push_back(nodes[i]);
      }
    }
    std::vector<EdgeRecord> residualEdges;
    residualEdges.reserve(edges.size());
    for (const EdgeRecord& edge : edges) {
      auto srcIt = idToIdx.find(edge.sourceModelId);
      auto tgtIt = idToIdx.find(edge.targetModelId);
      if (srcIt == idToIdx.end() || tgtIt == idToIdx.end()) {
        continue;
      }
      if (hubSet.count(srcIt->second) || hubSet.count(tgtIt->second)) {
        continue;
      }
      residualEdges.push_back(edge);
    }

    residualLabels =
      assignBiconnectedClusterLabels(residualNodes, residualEdges, resBridges, resBcc, resLargest);

    // Accept once bridges appear in the residual — that's the signal hub
    // removal actually exposed structural decomposition.
    if (resBridges > 0) {
      break;
    }
  }

  outHubCount = hubs.size();
  outResidualBridgeCount = resBridges;
  outResidualBccCount = resBcc;

  // Splice hub labels and residual labels back into the original-index order.
  (void)resLargest;
  std::unordered_map<std::string, std::string> labelByModelId;
  for (std::size_t i = 0; i < residualNodes.size(); ++i) {
    labelByModelId[residualNodes[i].modelId] = residualLabels[i];
  }
  for (std::size_t k = 0; k < hubs.size(); ++k) {
    labelByModelId[nodes[hubs[k]].modelId] = "_hub_" + std::to_string(k);
  }
  for (std::size_t i = 0; i < n; ++i) {
    auto it = labelByModelId.find(nodes[i].modelId);
    assigned[i] = it != labelByModelId.end() ? it->second : "_bcc_0";
  }
  return assigned;
}

// Internal: run one Louvain optimisation phase on a weighted adjacency.
// Returns compressed community ids in [0..numCommunities). totalWeight2m
// equals 2m where m is total edge weight.
std::vector<int> runLouvainPhase(
  const std::vector<std::vector<std::pair<std::size_t, double>>>& adj,
  const std::vector<double>& degree,
  double totalWeight2m,
  std::size_t maxPasses,
  std::size_t& outIterations,
  double resolution = 1.0) {
  outIterations = 0;
  const std::size_t n = adj.size();
  std::vector<int> comm(n);
  if (n == 0) return comm;

  std::vector<double> commTotalDegree(n, 0.0);
  for (std::size_t i = 0; i < n; ++i) {
    comm[i] = static_cast<int>(i);
    commTotalDegree[i] = degree[i];
  }

  if (totalWeight2m == 0.0) {
    return comm;
  }

  for (std::size_t pass = 0; pass < maxPasses; ++pass) {
    bool improved = false;
    for (std::size_t i = 0; i < n; ++i) {
      if (adj[i].empty()) continue;

      std::unordered_map<int, double> weightToComm;
      for (const auto& [j, w] : adj[i]) {
        if (j == i) continue;
        weightToComm[comm[j]] += w;
      }

      const int curComm = comm[i];
      commTotalDegree[curComm] -= degree[i];
      weightToComm.try_emplace(curComm, 0.0);

      int bestComm = curComm;
      double bestGain = -std::numeric_limits<double>::infinity();
      for (const auto& [c, kIinC] : weightToComm) {
        // Reichardt-Bornholdt resolution γ scales the null-model (degree)
        // penalty. γ<1 weakens the penalty → nodes join larger communities →
        // fewer, coarser clusters (and fewer inter-cluster edges); γ>1 → more,
        // finer clusters. γ=1 is the textbook Louvain modularity (default,
        // byte-identical to the original behaviour).
        const double gain =
          kIinC - resolution * commTotalDegree[c] * degree[i] / totalWeight2m;
        if (gain > bestGain || (gain == bestGain && c < bestComm)) {
          bestGain = gain;
          bestComm = c;
        }
      }

      if (bestComm != curComm) improved = true;
      comm[i] = bestComm;
      commTotalDegree[bestComm] += degree[i];
    }
    ++outIterations;
    if (!improved) break;
  }

  // Compress.
  std::unordered_map<int, int> remap;
  int next = 0;
  for (std::size_t i = 0; i < n; ++i) {
    auto it = remap.find(comm[i]);
    if (it == remap.end()) {
      remap[comm[i]] = next++;
      it = remap.find(comm[i]);
    }
    comm[i] = it->second;
  }
  return comm;
}

// Build a weighted adjacency representation from the edge list.
struct LouvainGraph {
  std::vector<std::vector<std::pair<std::size_t, double>>> adj;
  std::vector<double> degree;
  double totalWeight2m = 0.0;
};

LouvainGraph buildLouvainGraph(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges) {
  LouvainGraph g;
  const std::size_t n = nodes.size();
  std::unordered_map<std::string, std::size_t> idToIdx;
  idToIdx.reserve(n);
  for (std::size_t i = 0; i < n; ++i) idToIdx[nodes[i].modelId] = i;

  std::map<std::pair<std::size_t, std::size_t>, double> edgeWeight;
  for (const EdgeRecord& edge : edges) {
    auto srcIt = idToIdx.find(edge.sourceModelId);
    auto tgtIt = idToIdx.find(edge.targetModelId);
    if (srcIt == idToIdx.end() || tgtIt == idToIdx.end() || srcIt->second == tgtIt->second) {
      continue;
    }
    auto pair = srcIt->second < tgtIt->second
      ? std::make_pair(srcIt->second, tgtIt->second)
      : std::make_pair(tgtIt->second, srcIt->second);
    edgeWeight[pair] += 1.0;
  }

  g.adj.assign(n, {});
  g.degree.assign(n, 0.0);
  for (const auto& [p, w] : edgeWeight) {
    g.adj[p.first].emplace_back(p.second, w);
    g.adj[p.second].emplace_back(p.first, w);
    g.degree[p.first] += w;
    g.degree[p.second] += w;
    g.totalWeight2m += 2.0 * w;
  }
  return g;
}

// Pull degree-1 nodes (leaves) into their single neighbour's community. Louvain
// modularity often refuses to merge a leaf into a large community because the
// (degree(i) * Σ_tot(C)) / 2m penalty exceeds the k_in=1 gain — but visually
// those orphaned leaves end up far from their parent hub, and their long
// edges thread across other clusters. Forcing leaves into their neighbour's
// community concentrates the dominant node's radial edges in a tight cluster
// and frees angular space for inter-cluster routing.
//
// The pass also re-compresses community ids so callers see a contiguous range.
void attachLeavesAndCompress(
  std::vector<int>& comm,
  const std::vector<std::vector<std::pair<std::size_t, double>>>& adj) {
  const std::size_t n = adj.size();
  for (std::size_t i = 0; i < n; ++i) {
    if (adj[i].size() == 1) {
      const std::size_t j = adj[i][0].first;
      comm[i] = comm[j];
    }
  }
  std::unordered_map<int, int> remap;
  int next = 0;
  for (std::size_t i = 0; i < n; ++i) {
    auto it = remap.find(comm[i]);
    if (it == remap.end()) {
      remap[comm[i]] = next++;
      it = remap.find(comm[i]);
    }
    comm[i] = it->second;
  }
}

// Louvain modularity-based community detection. Single-pass.
std::vector<std::string> assignLouvainClusterLabels(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  std::size_t& outCommunityCount,
  std::size_t& outIterations,
  std::size_t maxPasses = 10) {
  outCommunityCount = 0;
  outIterations = 0;
  const std::size_t n = nodes.size();
  std::vector<std::string> assigned(n);
  if (n == 0) return assigned;

  LouvainGraph g = buildLouvainGraph(nodes, edges);
  if (g.totalWeight2m == 0.0) {
    for (std::size_t i = 0; i < n; ++i) assigned[i] = "_louv_" + std::to_string(i);
    outCommunityCount = n;
    return assigned;
  }

  double resolution = 1.0;
  if (const char* r = std::getenv("DJERD_LOUVAIN_RESOLUTION")) {
    const double v = std::atof(r);
    if (v > 0.0) resolution = v;
  }
  std::vector<int> comm =
    runLouvainPhase(g.adj, g.degree, g.totalWeight2m, maxPasses, outIterations, resolution);
  attachLeavesAndCompress(comm, g.adj);

  // Multi-level coarsening (standard Louvain aggregation). Single-level Louvain
  // leaves many small communities that resolution alone can't merge (a node
  // only moves to an ADJACENT community). The textbook fix: collapse each
  // community into a super-node (super-degree = Σ member degrees; super-edges
  // carry inter-community weight), re-run modularity on the super-graph, and
  // fold the result back to the original nodes. Repeating this coarsens the
  // partition into fewer, larger clusters → fewer inter-cluster edges, which
  // are the dominant crossing source. DJERD_LOUVAIN_LEVELS=1 (default) keeps
  // the original single-level result byte-identical.
  std::size_t levels = 1;
  if (const char* lv = std::getenv("DJERD_LOUVAIN_LEVELS")) {
    const int v = std::atoi(lv);
    if (v >= 1) levels = static_cast<std::size_t>(v);
  }
  for (std::size_t lvl = 1; lvl < levels; ++lvl) {
    int numC = 0;
    for (int c : comm) if (c + 1 > numC) numC = c + 1;
    if (numC < 4) break;
    std::vector<double> sdeg(static_cast<std::size_t>(numC), 0.0);
    for (std::size_t i = 0; i < n; ++i) sdeg[comm[i]] += g.degree[i];
    std::map<std::pair<std::size_t, std::size_t>, double> sw;
    for (std::size_t i = 0; i < n; ++i) {
      for (const auto& [j, w] : g.adj[i]) {
        if (j <= i) continue;
        const int ci = comm[i], cj = comm[j];
        if (ci == cj) continue;
        auto key = ci < cj
          ? std::make_pair(static_cast<std::size_t>(ci), static_cast<std::size_t>(cj))
          : std::make_pair(static_cast<std::size_t>(cj), static_cast<std::size_t>(ci));
        sw[key] += w;
      }
    }
    LouvainGraph sg;
    sg.adj.assign(static_cast<std::size_t>(numC), {});
    sg.degree = sdeg;
    sg.totalWeight2m = 0.0;
    for (double d : sdeg) sg.totalWeight2m += d;
    for (const auto& [p, w] : sw) {
      sg.adj[p.first].emplace_back(p.second, w);
      sg.adj[p.second].emplace_back(p.first, w);
    }
    if (sg.totalWeight2m == 0.0) break;
    std::size_t superIter = 0;
    std::vector<int> superComm =
      runLouvainPhase(sg.adj, sg.degree, sg.totalWeight2m, maxPasses, superIter, resolution);
    outIterations += superIter;
    bool changed = false;
    for (std::size_t i = 0; i < n; ++i) {
      const int nc = superComm[comm[i]];
      if (nc != comm[i]) changed = true;
      comm[i] = nc;
    }
    std::unordered_map<int, int> rm;
    int nx = 0;
    for (std::size_t i = 0; i < n; ++i) {
      auto it2 = rm.find(comm[i]);
      if (it2 == rm.end()) { rm[comm[i]] = nx++; }
      comm[i] = rm[comm[i]];
    }
    if (!changed) break;  // super-graph already modularity-optimal → converged
  }

  int maxId = 0;
  for (int c : comm) if (c > maxId) maxId = c;
  outCommunityCount = static_cast<std::size_t>(maxId + 1);
  for (std::size_t i = 0; i < n; ++i) {
    assigned[i] = "_louv_" + std::to_string(comm[i]);
  }
  return assigned;
}

// ===== Girvan-Newman edge-betweenness community detection =====
//
// Static-betweenness GN: compute edge betweenness ONCE via Brandes', sort
// edges by score, remove in order until the graph splits into `targetK`
// components. Full GN (recompute after each cut) costs O(VE^2); the static
// variant is O(VE + E log E) and ~100× faster. Cluster quality is slightly
// worse than full GN but adequate when downstream cluster_graph layout
// merges singletons and re-attaches leaves.

// Brandes' algorithm for unweighted edge betweenness centrality.
// Returns map from canonical edge (i, j) with i < j to its score.
std::map<std::pair<std::size_t, std::size_t>, double> computeEdgeBetweenness(
    const std::vector<std::vector<std::size_t>>& adj) {
  const std::size_t n = adj.size();
  std::map<std::pair<std::size_t, std::size_t>, double> bb;
  if (n == 0) return bb;

  for (std::size_t s = 0; s < n; ++s) {
    std::vector<std::vector<std::size_t>> P(n);
    std::vector<long long> dist(n, -1);
    std::vector<double> sigma(n, 0.0);
    std::vector<std::size_t> order;
    order.reserve(n);

    dist[s] = 0;
    sigma[s] = 1.0;
    std::queue<std::size_t> Q;
    Q.push(s);
    while (!Q.empty()) {
      const std::size_t v = Q.front();
      Q.pop();
      order.push_back(v);
      for (std::size_t w : adj[v]) {
        if (dist[w] < 0) {
          dist[w] = dist[v] + 1;
          Q.push(w);
        }
        if (dist[w] == dist[v] + 1) {
          sigma[w] += sigma[v];
          P[w].push_back(v);
        }
      }
    }

    std::vector<double> delta(n, 0.0);
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
      const std::size_t w = *it;
      for (std::size_t v : P[w]) {
        const double c = (sigma[v] / sigma[w]) * (1.0 + delta[w]);
        delta[v] += c;
        const auto key = (v < w) ? std::make_pair(v, w) : std::make_pair(w, v);
        bb[key] += c;
      }
    }
  }

  // Each undirected edge accumulated from both endpoints as source; halve.
  for (auto& kv : bb) kv.second *= 0.5;
  return bb;
}

// Static-betweenness Girvan-Newman. Cuts highest-betweenness edges until
// component count reaches targetK. Skips edges with a degree-1 endpoint —
// leaf edges have ~n betweenness so they would dominate the cut order
// otherwise, isolating every leaf as a singleton.
std::vector<std::string> assignGirvanNewmanClusterLabels(
    const std::vector<NodeRecord>& nodes,
    const std::vector<EdgeRecord>& edges,
    std::size_t& outCommunityCount,
    std::size_t& outRemovedEdges) {
  outCommunityCount = 0;
  outRemovedEdges = 0;
  const std::size_t n = nodes.size();
  std::vector<std::string> assigned(n);
  if (n == 0) return assigned;

  std::unordered_map<std::string, std::size_t> idToIdx;
  idToIdx.reserve(n);
  for (std::size_t i = 0; i < n; ++i) idToIdx[nodes[i].modelId] = i;

  std::set<std::pair<std::size_t, std::size_t>> uniqueEdges;
  for (const EdgeRecord& e : edges) {
    auto sIt = idToIdx.find(e.sourceModelId);
    auto tIt = idToIdx.find(e.targetModelId);
    if (sIt == idToIdx.end() || tIt == idToIdx.end()) continue;
    std::size_t a = sIt->second;
    std::size_t b = tIt->second;
    if (a == b) continue;
    if (a > b) std::swap(a, b);
    uniqueEdges.emplace(a, b);
  }

  if (uniqueEdges.empty()) {
    for (std::size_t i = 0; i < n; ++i) assigned[i] = "_gn_" + std::to_string(i);
    outCommunityCount = n;
    return assigned;
  }

  std::vector<std::vector<std::size_t>> adj(n);
  for (const auto& [a, b] : uniqueEdges) {
    adj[a].push_back(b);
    adj[b].push_back(a);
  }

  const auto bb = computeEdgeBetweenness(adj);

  std::vector<std::pair<std::pair<std::size_t, std::size_t>, double>> ordered(
      bb.begin(), bb.end());
  std::sort(ordered.begin(), ordered.end(),
            [](const auto& l, const auto& r) { return l.second > r.second; });

  // Conservative default: cut fewer edges, keep larger components. The
  // downstream cluster_graph layout merges/filters anyway, so producing
  // 263 fine-grained cuts upfront just inflates super-ring count and
  // pruned-node moves.
  const char* targetKEnv = std::getenv("DJERD_GN_TARGET_K");
  const std::size_t targetK =
      targetKEnv ? static_cast<std::size_t>(std::max(1, std::atoi(targetKEnv)))
                 : 100;

  std::vector<std::set<std::size_t>> adjSet(n);
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j : adj[i]) adjSet[i].insert(j);
  }

  std::vector<int> comp(n, -1);
  auto componentCount = [&]() {
    std::fill(comp.begin(), comp.end(), -1);
    int next = 0;
    for (std::size_t s = 0; s < n; ++s) {
      if (comp[s] >= 0) continue;
      std::queue<std::size_t> Q;
      Q.push(s);
      comp[s] = next;
      while (!Q.empty()) {
        const std::size_t v = Q.front();
        Q.pop();
        for (std::size_t w : adjSet[v]) {
          if (comp[w] >= 0) continue;
          comp[w] = next;
          Q.push(w);
        }
      }
      ++next;
    }
    return next;
  };

  int curComponents = componentCount();

  std::size_t removed = 0;
  for (const auto& [edge, bbScore] : ordered) {
    if (curComponents >= static_cast<int>(targetK)) break;
    const std::size_t u = edge.first;
    const std::size_t v = edge.second;
    if (adjSet[u].size() <= 1 || adjSet[v].size() <= 1) continue;
    adjSet[u].erase(v);
    adjSet[v].erase(u);
    ++removed;
    curComponents = componentCount();
  }
  outRemovedEdges = removed;

  // Pull leaves into their single neighbour's community (full adjacency).
  std::unordered_map<int, int> remap;
  int compIdNext = 0;
  for (std::size_t i = 0; i < n; ++i) {
    if (!remap.count(comp[i])) remap[comp[i]] = compIdNext++;
  }
  std::vector<int> finalComm(n);
  for (std::size_t i = 0; i < n; ++i) finalComm[i] = remap[comp[i]];
  for (std::size_t i = 0; i < n; ++i) {
    if (adj[i].size() == 1) finalComm[i] = finalComm[adj[i][0]];
  }
  // Re-compress after leaf attachment.
  std::unordered_map<int, int> recompress;
  int finalNext = 0;
  for (std::size_t i = 0; i < n; ++i) {
    if (!recompress.count(finalComm[i])) recompress[finalComm[i]] = finalNext++;
  }
  for (std::size_t i = 0; i < n; ++i) {
    assigned[i] = "_gn_" + std::to_string(recompress[finalComm[i]]);
  }
  outCommunityCount = static_cast<std::size_t>(finalNext);
  return assigned;
}

// Dispatcher: selects Louvain or Girvan-Newman by DJERD_COMMUNITY env var.
// Default = "gn".
std::vector<std::string> assignCommunityClusterLabels(
    const std::vector<NodeRecord>& nodes,
    const std::vector<EdgeRecord>& edges,
    std::size_t& outCommunityCount,
    std::size_t& outIterationsOrRemovals,
    std::string& outAlgorithm) {
  // Default reverted to Louvain after the GN trial: GN cuts collapse the
  // structure cluster_graph relies on (Apr 30 run produced 49K edgeCrossings
  // because cluster_graph found no backbone in GN's component layout).
  // GN is opt-in via DJERD_COMMUNITY=gn.
  const char* communityEnv = std::getenv("DJERD_COMMUNITY");
  const std::string mode = communityEnv ? std::string(communityEnv) : "louvain";
  if (mode == "gn" || mode == "girvan-newman") {
    outAlgorithm = "girvan-newman";
    return assignGirvanNewmanClusterLabels(
        nodes, edges, outCommunityCount, outIterationsOrRemovals);
  }
  outAlgorithm = "louvain";
  return assignLouvainClusterLabels(
      nodes, edges, outCommunityCount, outIterationsOrRemovals);
}

// 2-level Louvain. Runs phase 1, aggregates communities into super-nodes,
// then runs phase 1 on the aggregated graph to find super-communities.
// Returns hierarchical labels of the form "_louv2_X/_louv1_Y" so the
// downstream layout can detect "/" and execute hierarchical placement.
std::vector<std::string> assignTwoLevelLouvainLabels(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  std::size_t& outLevel1CommunityCount,
  std::size_t& outLevel2CommunityCount,
  std::size_t& outIterations) {
  outLevel1CommunityCount = 0;
  outLevel2CommunityCount = 0;
  outIterations = 0;
  const std::size_t n = nodes.size();
  std::vector<std::string> assigned(n);
  if (n == 0) return assigned;

  LouvainGraph g = buildLouvainGraph(nodes, edges);
  if (g.totalWeight2m == 0.0) {
    for (std::size_t i = 0; i < n; ++i) {
      assigned[i] = "_louv2_0/_louv1_" + std::to_string(i);
    }
    outLevel1CommunityCount = n;
    outLevel2CommunityCount = 1;
    return assigned;
  }

  // Phase 1: cluster the original graph.
  std::size_t l1Iter = 0;
  std::vector<int> comm1 = runLouvainPhase(
    g.adj, g.degree, g.totalWeight2m, /*maxPasses=*/10, l1Iter);
  outIterations += l1Iter;
  // Pull leaves into their parent's community before aggregation so the
  // super-graph reflects the post-attachment structure.
  attachLeavesAndCompress(comm1, g.adj);
  int l1Max = 0;
  for (int c : comm1) if (c > l1Max) l1Max = c;
  const std::size_t numL1 = static_cast<std::size_t>(l1Max + 1);
  outLevel1CommunityCount = numL1;

  // Aggregate: build super-graph where each phase-1 community is a node and
  // super-edges carry the summed weight of inter-community edges (self-loops
  // = intra-community edge weight, kept for proper modularity).
  std::map<std::pair<std::size_t, std::size_t>, double> superEdgeWeight;
  for (std::size_t i = 0; i < n; ++i) {
    for (const auto& [j, w] : g.adj[i]) {
      if (j <= i) continue;  // each undirected edge once
      const std::size_t ci = static_cast<std::size_t>(comm1[i]);
      const std::size_t cj = static_cast<std::size_t>(comm1[j]);
      if (ci == cj) continue;  // intra-community: skip (we don't run modularity over self-loops)
      auto key = ci < cj ? std::make_pair(ci, cj) : std::make_pair(cj, ci);
      superEdgeWeight[key] += w;
    }
  }

  LouvainGraph sg;
  sg.adj.assign(numL1, {});
  sg.degree.assign(numL1, 0.0);
  sg.totalWeight2m = 0.0;
  for (const auto& [p, w] : superEdgeWeight) {
    sg.adj[p.first].emplace_back(p.second, w);
    sg.adj[p.second].emplace_back(p.first, w);
    sg.degree[p.first] += w;
    sg.degree[p.second] += w;
    sg.totalWeight2m += 2.0 * w;
  }

  // If the aggregated graph has no edges (all phase-1 communities disconnected
  // from each other), no further grouping is possible — every super-node
  // becomes its own super-community.
  std::vector<int> comm2;
  if (sg.totalWeight2m == 0.0 || numL1 < 4) {
    comm2.resize(numL1);
    for (std::size_t i = 0; i < numL1; ++i) comm2[i] = static_cast<int>(i);
  } else {
    std::size_t l2Iter = 0;
    comm2 = runLouvainPhase(sg.adj, sg.degree, sg.totalWeight2m, /*maxPasses=*/10, l2Iter);
    outIterations += l2Iter;
  }

  int l2Max = 0;
  for (int c : comm2) if (c > l2Max) l2Max = c;
  outLevel2CommunityCount = static_cast<std::size_t>(l2Max + 1);

  // Hierarchical labels (parent/sub) are issued when the phase-2 grouping
  // averages >= 3 sub-clusters per parent — at that ratio the nested
  // layout actually has structure to lay out. For weak hierarchies (e.g. 1.9
  // sub/parent on this user's ERD) the flat phase-1 communities give
  // tighter bbox and more stable run-to-run results, so we keep the original
  // flat labels in that case.
  const bool hierarchyHelps = outLevel2CommunityCount < numL1
    && outLevel2CommunityCount >= 2;
  const double subPerParent = numL1 > 0 && outLevel2CommunityCount > 0
    ? static_cast<double>(numL1) / static_cast<double>(outLevel2CommunityCount)
    : 0.0;
  const bool useNestedHierarchy = hierarchyHelps && subPerParent >= 3.0;
  for (std::size_t i = 0; i < n; ++i) {
    const int c1 = comm1[i];
    if (useNestedHierarchy) {
      const int c2 = comm2[c1];
      assigned[i] = "_louv2_" + std::to_string(c2) + "/_louv1_" + std::to_string(c1);
    } else {
      assigned[i] = "_louv_" + std::to_string(c1);
    }
  }
  return assigned;
}

std::vector<std::string> assignStructuralClusterLabels(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  std::size_t targetClusterCount) {
  const std::size_t n = nodes.size();
  std::vector<std::string> assigned(n);
  if (n == 0) {
    return assigned;
  }

  std::unordered_map<std::string, std::size_t> idToIdx;
  idToIdx.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    idToIdx[nodes[i].modelId] = i;
  }

  std::vector<std::vector<std::size_t>> adj(n);
  for (const EdgeRecord& edge : edges) {
    auto srcIt = idToIdx.find(edge.sourceModelId);
    auto tgtIt = idToIdx.find(edge.targetModelId);
    if (srcIt == idToIdx.end() || tgtIt == idToIdx.end() || srcIt->second == tgtIt->second) {
      continue;
    }
    adj[srcIt->second].push_back(tgtIt->second);
    adj[tgtIt->second].push_back(srcIt->second);
  }

  std::vector<std::pair<std::size_t, std::size_t>> degreeOrder;
  degreeOrder.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    degreeOrder.emplace_back(adj[i].size(), i);
  }
  std::sort(degreeOrder.begin(), degreeOrder.end(),
    [](const auto& a, const auto& b) { return a.first > b.first; });

  const std::size_t hubCount = std::min(targetClusterCount, n);
  std::vector<std::size_t> hubs;
  hubs.reserve(hubCount);
  for (std::size_t k = 0; k < hubCount; ++k) {
    hubs.push_back(degreeOrder[k].second);
  }

  std::vector<int> clusterOf(n, -1);
  std::queue<std::pair<std::size_t, std::size_t>> bfsQueue;
  for (std::size_t k = 0; k < hubs.size(); ++k) {
    clusterOf[hubs[k]] = static_cast<int>(k);
    bfsQueue.emplace(hubs[k], k);
  }
  while (!bfsQueue.empty()) {
    const auto [nodeIdx, clusterIdx] = bfsQueue.front();
    bfsQueue.pop();
    for (std::size_t neighbor : adj[nodeIdx]) {
      if (clusterOf[neighbor] != -1) {
        continue;
      }
      clusterOf[neighbor] = static_cast<int>(clusterIdx);
      bfsQueue.emplace(neighbor, clusterIdx);
    }
  }

  std::size_t isolatedSink = hubs.size();
  for (std::size_t i = 0; i < n; ++i) {
    if (clusterOf[i] == -1) {
      clusterOf[i] = static_cast<int>(isolatedSink);
    }
  }

  for (std::size_t i = 0; i < n; ++i) {
    assigned[i] = "_struct_" + std::to_string(clusterOf[i]);
  }
  return assigned;
}

std::vector<std::string> assignBiconnectedClusterLabels(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  std::size_t& outBridgeCount,
  std::size_t& outBccCount,
  std::size_t& outLargestClusterSize) {
  outBridgeCount = 0;
  outBccCount = 0;
  outLargestClusterSize = 0;
  const std::size_t n = nodes.size();
  std::vector<std::string> assigned(n);
  if (n == 0) {
    return assigned;
  }

  std::unordered_map<std::string, std::size_t> idToIdx;
  idToIdx.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    idToIdx[nodes[i].modelId] = i;
  }

  // Adjacency with edge index. We deduplicate parallel edges per (u, v) pair so
  // a single back-edge can only neutralise one tree-edge during the bridge test.
  struct AdjEntry {
    std::size_t neighbor;
    std::size_t edgeIndex;
  };
  std::vector<std::vector<AdjEntry>> adj(n);
  for (std::size_t e = 0; e < edges.size(); ++e) {
    const EdgeRecord& edge = edges[e];
    auto srcIt = idToIdx.find(edge.sourceModelId);
    auto tgtIt = idToIdx.find(edge.targetModelId);
    if (srcIt == idToIdx.end() || tgtIt == idToIdx.end() || srcIt->second == tgtIt->second) {
      continue;
    }
    adj[srcIt->second].push_back({tgtIt->second, e});
    adj[tgtIt->second].push_back({srcIt->second, e});
  }

  // Tarjan bridge detection (iterative — recursion may overflow on dense ERDs).
  std::vector<int> disc(n, -1);
  std::vector<int> low(n, -1);
  std::vector<int> parentEdge(n, -1);
  std::vector<std::size_t> iterIndex(n, 0);
  std::vector<bool> isBridge(edges.size(), false);
  int timer = 0;

  for (std::size_t root = 0; root < n; ++root) {
    if (disc[root] != -1) {
      continue;
    }
    std::stack<std::size_t> stack;
    disc[root] = low[root] = timer++;
    parentEdge[root] = -1;
    stack.push(root);
    while (!stack.empty()) {
      const std::size_t u = stack.top();
      if (iterIndex[u] < adj[u].size()) {
        const AdjEntry entry = adj[u][iterIndex[u]++];
        const std::size_t v = entry.neighbor;
        const std::size_t eIdx = entry.edgeIndex;
        if (static_cast<int>(eIdx) == parentEdge[u]) {
          continue;
        }
        if (disc[v] == -1) {
          disc[v] = low[v] = timer++;
          parentEdge[v] = static_cast<int>(eIdx);
          stack.push(v);
        } else {
          if (disc[v] < low[u]) {
            low[u] = disc[v];
          }
        }
      } else {
        stack.pop();
        if (!stack.empty()) {
          const std::size_t parent = stack.top();
          if (low[u] < low[parent]) {
            low[parent] = low[u];
          }
          if (low[u] > disc[parent]) {
            isBridge[static_cast<std::size_t>(parentEdge[u])] = true;
          }
        }
      }
    }
  }

  for (bool b : isBridge) {
    if (b) {
      ++outBridgeCount;
    }
  }

  // BCC = connected components of (graph minus bridges) — but isolated vertices
  // (degree 0 once bridges removed) become singleton BCCs.
  std::vector<int> bccId(n, -1);
  int nextBccId = 0;
  for (std::size_t start = 0; start < n; ++start) {
    if (bccId[start] != -1) {
      continue;
    }
    std::queue<std::size_t> queue;
    queue.push(start);
    bccId[start] = nextBccId;
    while (!queue.empty()) {
      const std::size_t u = queue.front();
      queue.pop();
      for (const AdjEntry& entry : adj[u]) {
        if (isBridge[entry.edgeIndex]) {
          continue;
        }
        if (bccId[entry.neighbor] != -1) {
          continue;
        }
        bccId[entry.neighbor] = nextBccId;
        queue.push(entry.neighbor);
      }
    }
    ++nextBccId;
  }
  outBccCount = static_cast<std::size_t>(nextBccId);

  std::vector<std::size_t> bccSize(static_cast<std::size_t>(nextBccId), 0);
  for (std::size_t i = 0; i < n; ++i) {
    if (bccId[i] >= 0) {
      ++bccSize[static_cast<std::size_t>(bccId[i])];
    }
  }
  for (std::size_t s : bccSize) {
    if (s > outLargestClusterSize) {
      outLargestClusterSize = s;
    }
  }

  for (std::size_t i = 0; i < n; ++i) {
    assigned[i] = "_bcc_" + std::to_string(bccId[i]);
  }
  return assigned;
}

// Apply BCC inside each level-1 cluster of size >= minRecurseSize and return
// hierarchical labels of the form "<level1>/<level2>". When level-2 BCC does
// not split a cluster (no bridges, or below threshold) the level-2 part is
// "_bcc_0" so all nodes in that cluster share the same combined label and the
// hierarchical layout pass treats the cluster as a single block.
std::vector<std::string> appendLevel2BccLabels(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::string>& level1Labels,
  std::size_t& outBridgeCountTotal,
  std::size_t& outDistinctCombinedClusters,
  std::size_t& outLargestCombinedClusterSize,
  std::size_t& outInnerSplitClusters,
  std::size_t minRecurseSize = 30) {
  outBridgeCountTotal = 0;
  outDistinctCombinedClusters = 0;
  outLargestCombinedClusterSize = 0;
  outInnerSplitClusters = 0;

  const std::size_t n = nodes.size();
  if (n == 0) {
    return {};
  }

  std::unordered_map<std::string, std::vector<std::size_t>> membersByCluster;
  for (std::size_t i = 0; i < n; ++i) {
    membersByCluster[level1Labels[i]].push_back(i);
  }

  std::vector<std::string> level2(n, std::string("_bcc_0"));
  for (const auto& [clusterKey, members] : membersByCluster) {
    if (members.size() < minRecurseSize) {
      continue;
    }
    std::vector<NodeRecord> subNodes;
    subNodes.reserve(members.size());
    std::unordered_set<std::string> memberIds;
    memberIds.reserve(members.size());
    for (std::size_t idx : members) {
      memberIds.insert(nodes[idx].modelId);
      subNodes.push_back(nodes[idx]);
    }
    std::vector<EdgeRecord> subEdges;
    subEdges.reserve(edges.size());
    for (const EdgeRecord& edge : edges) {
      if (memberIds.count(edge.sourceModelId) && memberIds.count(edge.targetModelId)) {
        subEdges.push_back(edge);
      }
    }
    std::size_t subBridges = 0;
    std::size_t subBcc = 0;
    std::size_t subLargest = 0;
    std::vector<std::string> subLabels =
      assignBiconnectedClusterLabels(subNodes, subEdges, subBridges, subBcc, subLargest);
    outBridgeCountTotal += subBridges;
    if (subBcc >= 2) {
      ++outInnerSplitClusters;
    }
    for (std::size_t k = 0; k < members.size(); ++k) {
      level2[members[k]] = subLabels[k];
    }
  }

  std::vector<std::string> combined(n);
  std::unordered_map<std::string, std::size_t> sizeByCombined;
  for (std::size_t i = 0; i < n; ++i) {
    combined[i] = level1Labels[i] + "/" + level2[i];
    ++sizeByCombined[combined[i]];
  }
  outDistinctCombinedClusters = sizeByCombined.size();
  for (const auto& [_, s] : sizeByCombined) {
    if (s > outLargestCombinedClusterSize) {
      outLargestCombinedClusterSize = s;
    }
  }
  return combined;
}

// Two-level recursive BCC entry point: run BCC at level 1, then BCC at level 2
// inside each non-trivial level-1 cluster. On graphs without bridges (e.g.
// dense biconnected ERDs) the level-1 pass returns one BCC = the connected
// component, so the level-2 pass becomes the only meaningful split. This is
// the structure step 2 wanted: even if level-1 BCC is degenerate, level-2 still
// gets a chance to find sub-structure within the giant biconnected blob.
std::vector<std::string> assignTwoLevelBccLabels(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  std::size_t& outBridgeCountTotal,
  std::size_t& outBccCountTotal,
  std::size_t& outLargestClusterSize,
  std::size_t& outInnerSplitClusters,
  std::size_t minRecurseSize = 30) {
  outBridgeCountTotal = 0;
  outBccCountTotal = 0;
  outLargestClusterSize = 0;
  outInnerSplitClusters = 0;

  const std::size_t n = nodes.size();
  if (n == 0) {
    return {};
  }

  std::size_t l1Bridges = 0;
  std::size_t l1Bcc = 0;
  std::size_t l1Largest = 0;
  std::vector<std::string> level1 =
    assignBiconnectedClusterLabels(nodes, edges, l1Bridges, l1Bcc, l1Largest);

  std::size_t l2Bridges = 0;
  std::vector<std::string> combined = appendLevel2BccLabels(
    nodes, edges, level1, l2Bridges,
    outBccCountTotal, outLargestClusterSize, outInnerSplitClusters,
    minRecurseSize);
  outBridgeCountTotal = l1Bridges + l2Bridges;
  return combined;
}

// Forward decl: hierarchical wrapper used when appLabel contains "/" (Louvain
// 2-level output, BCC level1/level2, etc).
void runHierarchicalClusterLayout(
  const ClusterRunOptions& options,
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  std::size_t& outClusterCount,
  std::size_t& outInterClusterEdges);

void runClusteredByAppLayout(
  const ClusterRunOptions& options,
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  std::size_t& outClusterCount,
  std::size_t& outInterClusterEdges) {
  // Hierarchical dispatch: any node label containing "/" routes to a 2-level
  // layout that places sub-clusters within parents and parents in a global
  // meta-layout. Sub-cluster placement reuses this same function (the flat
  // path), since sub-labels do not contain "/".
  bool hierarchical = false;
  for (const NodeRecord& node : nodes) {
    if (node.appLabel.find('/') != std::string::npos) {
      hierarchical = true;
      break;
    }
  }
  if (hierarchical) {
    runHierarchicalClusterLayout(options, nodes, edges, attributes,
                                 outClusterCount, outInterClusterEdges);
    return;
  }

  std::unordered_map<std::string, std::vector<std::size_t>> clusterMembers;
  std::vector<std::string> clusterOrder;
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    const std::string& key = nodes[index].appLabel.empty() ? std::string("_default") : nodes[index].appLabel;
    auto it = clusterMembers.find(key);
    if (it == clusterMembers.end()) {
      clusterOrder.push_back(key);
      clusterMembers[key] = {};
      it = clusterMembers.find(key);
    }
    it->second.push_back(index);
  }

  std::unordered_map<std::string, std::string> nodeIdToCluster;
  nodeIdToCluster.reserve(nodes.size());
  for (const NodeRecord& node : nodes) {
    nodeIdToCluster[node.modelId] = node.appLabel.empty() ? std::string("_default") : node.appLabel;
  }

  std::unordered_map<std::string, std::pair<double, double>> clusterLocalSize;
  std::unordered_map<std::string, std::pair<double, double>> clusterLocalMin;

  for (const std::string& clusterKey : clusterOrder) {
    const std::vector<std::size_t>& members = clusterMembers[clusterKey];
    if (members.empty()) {
      continue;
    }

    if (members.size() == 1) {
      const NodeRecord& only = nodes[members[0]];
      attributes.x(only.handle) = only.width / 2.0;
      attributes.y(only.handle) = only.height / 2.0;
      clusterLocalSize[clusterKey] = {only.width, only.height};
      clusterLocalMin[clusterKey] = {0.0, 0.0};
      continue;
    }

    // Trivial geometric placement for tiny clusters: skip the OGDF call.
    // Pairs go side by side; triples form a small triangle. Anything bigger
    // falls through to a size-aware OGDF layout below.
    if (members.size() == 2) {
      const NodeRecord& a = nodes[members[0]];
      const NodeRecord& b = nodes[members[1]];
      const double gap = 24.0;
      attributes.x(a.handle) = a.width / 2.0;
      attributes.y(a.handle) = a.height / 2.0;
      attributes.x(b.handle) = a.width + gap + b.width / 2.0;
      attributes.y(b.handle) = std::max(a.height, b.height) / 2.0;
      clusterLocalSize[clusterKey] = {
        a.width + gap + b.width,
        std::max(a.height, b.height),
      };
      clusterLocalMin[clusterKey] = {0.0, 0.0};
      continue;
    }
    if (members.size() == 3) {
      const NodeRecord& a = nodes[members[0]];
      const NodeRecord& b = nodes[members[1]];
      const NodeRecord& c = nodes[members[2]];
      const double gap = 24.0;
      const double row1Width = a.width + gap + b.width;
      const double topHeight = std::max(a.height, b.height);
      attributes.x(a.handle) = a.width / 2.0;
      attributes.y(a.handle) = a.height / 2.0;
      attributes.x(b.handle) = a.width + gap + b.width / 2.0;
      attributes.y(b.handle) = b.height / 2.0;
      attributes.x(c.handle) = row1Width / 2.0;
      attributes.y(c.handle) = topHeight + gap + c.height / 2.0;
      clusterLocalSize[clusterKey] = {
        std::max(row1Width, c.width),
        topHeight + gap + c.height,
      };
      clusterLocalMin[clusterKey] = {0.0, 0.0};
      continue;
    }

    ogdf::Graph subGraph;
    ogdf::GraphAttributes subAttr(
      subGraph,
      ogdf::GraphAttributes::nodeGraphics | ogdf::GraphAttributes::edgeGraphics);
    std::vector<ogdf::node> subNodes(members.size());
    std::unordered_map<std::string, std::size_t> idToSubIdx;
    idToSubIdx.reserve(members.size());

    for (std::size_t k = 0; k < members.size(); ++k) {
      const NodeRecord& node = nodes[members[k]];
      subNodes[k] = subGraph.newNode();
      subAttr.width(subNodes[k]) = std::max(1.0, node.width);
      subAttr.height(subNodes[k]) = std::max(1.0, node.height);
      idToSubIdx[node.modelId] = k;
    }

    for (const EdgeRecord& edge : edges) {
      const auto srcIt = idToSubIdx.find(edge.sourceModelId);
      const auto tgtIt = idToSubIdx.find(edge.targetModelId);
      if (srcIt != idToSubIdx.end() && tgtIt != idToSubIdx.end() && srcIt->second != tgtIt->second) {
        subGraph.newEdge(subNodes[srcIt->second], subNodes[tgtIt->second]);
      }
    }

    // Inner layout choice depends on the requested mode AND the cluster size.
    // For Sugiyama-style modes, small/medium clusters get PlanarizationLayout
    // (a true crossing-min algorithm; cheap on subgraphs) while large clusters
    // stick with Sugiyama. Circular mode similarly uses PlanarizationLayout
    // for tiny clusters where ring shape is degenerate (size <= 6) — only
    // larger clusters get the actual ring layout. Other modes honour the
    // user's pick across all sizes.
    const bool sugiyamaInner =
      options.innerMode != "fmm"
      && options.innerMode != "planarization"
      && options.innerMode != "planarization_grid"
      && options.innerMode != "uml_planarization"
      && options.innerMode != "circular";
    const bool useInnerPlanarization =
      (sugiyamaInner && members.size() <= 28)
      || (options.innerMode == "circular" && members.size() <= 6);

    if (options.innerMode == "fmm") {
      ogdf::FastMultipoleEmbedder fmm;
      fmm.setNumIterations(options.innerFmmIterations);
      fmm.setMultipolePrec(6);
      fmm.setDefaultEdgeLength(static_cast<float>(options.innerFmmEdgeLength));
      fmm.setDefaultNodeSize(static_cast<float>(options.innerFmmNodeSize));
      fmm.setRandomize(true);
      fmm.setNumberOfThreads(static_cast<uint32_t>(idealThreadCount()));
      fmm.call(subAttr);
    } else if (options.innerMode == "planarization"
               || options.innerMode == "planarization_grid"
               || useInnerPlanarization) {
      ogdf::PlanarizationLayout pl;
      pl.setCrossMin(createBoundedSubgraphPlanarizer());
      pl.pageRatio(kPlanarizationPageRatio);
      pl.call(subAttr);
    } else if (options.innerMode == "uml_planarization") {
      ogdf::PlanarizationLayoutUML pl;
      pl.call(subAttr);
    } else if (options.innerMode == "circular") {
      ogdf::CircularLayout cl;
      cl.minDistCircle(96.0);
      cl.minDistCC(96.0);
      cl.minDistLevel(96.0);
      cl.minDistSibling(48.0);
      cl.call(subAttr);
    } else {
      ogdf::SugiyamaLayout sugi;
      sugi.setRanking(new ogdf::OptimalRanking());
      sugi.setCrossMin(new ogdf::BarycenterHeuristic());
      sugi.runs(2);
      sugi.fails(4);
      sugi.transpose(true);
      auto* hier = new ogdf::OptimalHierarchyLayout();
      hier->layerDistance(options.innerLayerDistance);
      hier->nodeDistance(options.innerNodeDistance);
      hier->weightBalancing(0.72);
      sugi.setLayout(hier);
      sugi.arrangeCCs(true);
      sugi.call(subAttr);
    }

    // Hub densification: when the cluster has a clear intra-cluster hub with
    // 3+ leaves (members whose ONLY graph edge is to the hub), pack those
    // leaves into a tight ring on the side of the hub facing AWAY from the
    // cluster's other members. Restricting to global-degree-1 nodes ensures
    // we never move a node with inter-cluster edges (whose new position
    // would change inter-cluster routing).
    if (members.size() >= 6) {
      // Global degree per modelId (deduped: parallel edges count once).
      std::unordered_map<std::string, std::set<std::string>> globalNeighbours;
      for (const EdgeRecord& edge : edges) {
        if (edge.sourceModelId.empty() || edge.targetModelId.empty()) continue;
        if (edge.sourceModelId == edge.targetModelId) continue;
        globalNeighbours[edge.sourceModelId].insert(edge.targetModelId);
        globalNeighbours[edge.targetModelId].insert(edge.sourceModelId);
      }
      auto globalDeg = [&](const std::string& id) -> std::size_t {
        auto it = globalNeighbours.find(id);
        return it == globalNeighbours.end() ? 0 : it->second.size();
      };
      // Deduplicated intra-cluster adjacency (parallel edges collapse to one).
      std::vector<std::set<std::size_t>> innerAdjSet(members.size());
      for (const EdgeRecord& edge : edges) {
        const auto srcIt = idToSubIdx.find(edge.sourceModelId);
        const auto tgtIt = idToSubIdx.find(edge.targetModelId);
        if (srcIt == idToSubIdx.end() || tgtIt == idToSubIdx.end() || srcIt->second == tgtIt->second) continue;
        innerAdjSet[srcIt->second].insert(tgtIt->second);
        innerAdjSet[tgtIt->second].insert(srcIt->second);
      }
      std::vector<std::vector<std::size_t>> innerAdj(members.size());
      for (std::size_t k = 0; k < members.size(); ++k) {
        innerAdj[k].assign(innerAdjSet[k].begin(), innerAdjSet[k].end());
      }

      std::size_t hubK = 0;
      for (std::size_t k = 1; k < members.size(); ++k) {
        if (innerAdj[k].size() > innerAdj[hubK].size()) hubK = k;
      }

      if (innerAdj[hubK].size() >= 5) {
        std::vector<std::size_t> leafKs;
        for (std::size_t k = 0; k < members.size(); ++k) {
          if (k == hubK) continue;
          if (innerAdj[k].size() != 1 || innerAdj[k][0] != hubK) continue;
          if (globalDeg(nodes[members[k]].modelId) != 1) continue;
          leafKs.push_back(k);
        }

        if (leafKs.size() >= 3) {
          const double hubX = subAttr.x(subNodes[hubK]);
          const double hubY = subAttr.y(subNodes[hubK]);
          const double hubW = subAttr.width(subNodes[hubK]);
          const double hubH = subAttr.height(subNodes[hubK]);

          // Compute centroid of NON-leaf, non-hub members so we can place
          // leaves on the opposite side of the hub.
          double otherX = 0.0, otherY = 0.0;
          std::size_t otherCount = 0;
          std::vector<bool> isLeafIdx(members.size(), false);
          for (std::size_t lk : leafKs) isLeafIdx[lk] = true;
          for (std::size_t k = 0; k < members.size(); ++k) {
            if (k == hubK || isLeafIdx[k]) continue;
            otherX += subAttr.x(subNodes[k]);
            otherY += subAttr.y(subNodes[k]);
            ++otherCount;
          }
          double biasX = -1.0, biasY = 0.0;
          if (otherCount > 0) {
            otherX /= static_cast<double>(otherCount);
            otherY /= static_cast<double>(otherCount);
            biasX = hubX - otherX;
            biasY = hubY - otherY;
            const double biasLen = std::sqrt(biasX * biasX + biasY * biasY);
            if (biasLen < 1e-3) {
              biasX = -1.0; biasY = 0.0;
            } else {
              biasX /= biasLen; biasY /= biasLen;
            }
          }

          // Pull each leaf halfway toward the hub along its existing direction.
          // This preserves Sugiyama's angular distribution (so the cluster's
          // outer envelope is unchanged and FMMM keeps the inter-cluster
          // spacing it picked) but visually compacts the radial spread.
          (void)biasX; (void)biasY;  // bias direction unused in shrink mode
          constexpr double kRadialShrink = 0.5;
          double maxLeafW = 0.0, maxLeafH = 0.0;
          for (std::size_t lk : leafKs) {
            maxLeafW = std::max(maxLeafW, subAttr.width(subNodes[lk]));
            maxLeafH = std::max(maxLeafH, subAttr.height(subNodes[lk]));
          }
          const double minRadius = std::max(hubW, hubH) / 2.0
                                   + std::max(maxLeafW, maxLeafH) / 2.0 + 12.0;
          for (std::size_t lk : leafKs) {
            const double dxLeaf = subAttr.x(subNodes[lk]) - hubX;
            const double dyLeaf = subAttr.y(subNodes[lk]) - hubY;
            const double dist = std::sqrt(dxLeaf * dxLeaf + dyLeaf * dyLeaf);
            if (dist < 1e-3) continue;
            const double newDist = std::max(minRadius, dist * kRadialShrink);
            const double scale = newDist / dist;
            subAttr.x(subNodes[lk]) = hubX + dxLeaf * scale;
            subAttr.y(subNodes[lk]) = hubY + dyLeaf * scale;
          }
        }
      }
    }

    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();

    for (std::size_t k = 0; k < members.size(); ++k) {
      const NodeRecord& node = nodes[members[k]];
      const double cx = subAttr.x(subNodes[k]);
      const double cy = subAttr.y(subNodes[k]);
      minX = std::min(minX, cx - node.width / 2.0);
      minY = std::min(minY, cy - node.height / 2.0);
      maxX = std::max(maxX, cx + node.width / 2.0);
      maxY = std::max(maxY, cy + node.height / 2.0);
    }

    for (std::size_t k = 0; k < members.size(); ++k) {
      const NodeRecord& node = nodes[members[k]];
      attributes.x(node.handle) = subAttr.x(subNodes[k]) - minX;
      attributes.y(node.handle) = subAttr.y(subNodes[k]) - minY;
    }

    clusterLocalSize[clusterKey] = {maxX - minX, maxY - minY};
    clusterLocalMin[clusterKey] = {0.0, 0.0};
  }

  ogdf::Graph metaGraph;
  ogdf::GraphAttributes metaAttr(
    metaGraph,
    ogdf::GraphAttributes::nodeGraphics | ogdf::GraphAttributes::edgeGraphics);
  std::unordered_map<std::string, ogdf::node> clusterToMeta;
  for (const std::string& clusterKey : clusterOrder) {
    auto sizeIt = clusterLocalSize.find(clusterKey);
    if (sizeIt == clusterLocalSize.end()) {
      continue;
    }
    ogdf::node meta = metaGraph.newNode();
    metaAttr.width(meta) = std::max(120.0, sizeIt->second.first + options.interClusterPadding);
    metaAttr.height(meta) = std::max(120.0, sizeIt->second.second + options.interClusterPadding);
    clusterToMeta[clusterKey] = meta;
  }

  std::map<std::pair<std::string, std::string>, std::size_t> interClusterEdgeCount;
  for (const EdgeRecord& edge : edges) {
    auto srcIt = nodeIdToCluster.find(edge.sourceModelId);
    auto tgtIt = nodeIdToCluster.find(edge.targetModelId);
    if (srcIt == nodeIdToCluster.end() || tgtIt == nodeIdToCluster.end()) {
      continue;
    }
    if (srcIt->second == tgtIt->second) {
      continue;
    }
    auto pair = srcIt->second < tgtIt->second
      ? std::make_pair(srcIt->second, tgtIt->second)
      : std::make_pair(tgtIt->second, srcIt->second);
    interClusterEdgeCount[pair]++;
  }

  for (const auto& [pair, count] : interClusterEdgeCount) {
    auto srcIt = clusterToMeta.find(pair.first);
    auto tgtIt = clusterToMeta.find(pair.second);
    if (srcIt == clusterToMeta.end() || tgtIt == clusterToMeta.end()) {
      continue;
    }
    const std::size_t replicate = std::min<std::size_t>(8, count);
    for (std::size_t r = 0; r < replicate; ++r) {
      metaGraph.newEdge(srcIt->second, tgtIt->second);
    }
  }

  if (clusterToMeta.size() >= 2) {
    if (options.metaMode == "sugiyama") {
      ogdf::SugiyamaLayout metaSugi;
      metaSugi.setRanking(new ogdf::OptimalRanking());
      metaSugi.setCrossMin(new ogdf::BarycenterHeuristic());
      metaSugi.runs(2);
      metaSugi.fails(4);
      metaSugi.transpose(true);
      auto* metaHier = new ogdf::OptimalHierarchyLayout();
      metaHier->layerDistance(options.metaLayerDistance);
      metaHier->nodeDistance(options.metaNodeDistance);
      metaHier->weightBalancing(0.72);
      metaSugi.setLayout(metaHier);
      metaSugi.arrangeCCs(true);
      metaSugi.call(metaAttr);
    } else if (options.metaMode == "grid") {
      const std::size_t count = clusterToMeta.size();
      const std::size_t cols = static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(count))));
      double cellWidth = 0.0;
      double cellHeight = 0.0;
      for (const auto& [_, meta] : clusterToMeta) {
        cellWidth = std::max(cellWidth, metaAttr.width(meta));
        cellHeight = std::max(cellHeight, metaAttr.height(meta));
      }
      cellWidth += options.interClusterPadding;
      cellHeight += options.interClusterPadding;
      std::size_t cellIndex = 0;
      for (const std::string& clusterKey : clusterOrder) {
        auto it = clusterToMeta.find(clusterKey);
        if (it == clusterToMeta.end()) {
          continue;
        }
        const std::size_t row = cellIndex / cols;
        const std::size_t col = cellIndex % cols;
        metaAttr.x(it->second) = static_cast<double>(col) * cellWidth + cellWidth / 2.0;
        metaAttr.y(it->second) = static_cast<double>(row) * cellHeight + cellHeight / 2.0;
        cellIndex++;
      }
    } else {
      ogdf::FMMMLayout metaLayout;
      metaLayout.useHighLevelOptions(true);
      metaLayout.unitEdgeLength(options.metaUnitEdgeLength);
      metaLayout.newInitialPlacement(true);
      metaLayout.qualityVersusSpeed(ogdf::FMMMOptions::QualityVsSpeed::BeautifulAndFast);
      metaLayout.call(metaAttr);
    }

    // Cluster overlap resolution: FMMM doesn't strictly enforce non-overlap
    // between super-nodes, especially after hub densification shrinks cluster
    // local sizes. Push apart any pair whose bboxes overlap (or fall within
    // the configured padding) to avoid the visual "mesh" effect where
    // adjacent clusters touch.
    {
      std::vector<ogdf::node> orderedMetas;
      orderedMetas.reserve(clusterToMeta.size());
      for (const std::string& key : clusterOrder) {
        auto it = clusterToMeta.find(key);
        if (it != clusterToMeta.end()) orderedMetas.push_back(it->second);
      }
      const double minGap = std::max(100.0, options.interClusterPadding);
      const std::size_t maxSepIter = 32;
      for (std::size_t iter = 0; iter < maxSepIter; ++iter) {
        bool moved = false;
        for (std::size_t i = 0; i < orderedMetas.size(); ++i) {
          const ogdf::node a = orderedMetas[i];
          for (std::size_t j = i + 1; j < orderedMetas.size(); ++j) {
            const ogdf::node b = orderedMetas[j];
            const double ax = metaAttr.x(a), ay = metaAttr.y(a);
            const double bx = metaAttr.x(b), by = metaAttr.y(b);
            const double aw = metaAttr.width(a) / 2.0, ah = metaAttr.height(a) / 2.0;
            const double bw = metaAttr.width(b) / 2.0, bh = metaAttr.height(b) / 2.0;
            const double minDx = aw + bw + minGap;
            const double minDy = ah + bh + minGap;
            const double dx = bx - ax;
            const double dy = by - ay;
            if (std::abs(dx) >= minDx) continue;
            if (std::abs(dy) >= minDy) continue;
            // Both axes overlap — push apart along the smaller-overshoot axis.
            const double overshootX = minDx - std::abs(dx);
            const double overshootY = minDy - std::abs(dy);
            if (overshootX < overshootY) {
              const double push = overshootX / 2.0 + 0.01;
              const double sign = dx >= 0 ? 1.0 : -1.0;
              metaAttr.x(a) -= sign * push;
              metaAttr.x(b) += sign * push;
            } else {
              const double push = overshootY / 2.0 + 0.01;
              const double sign = dy >= 0 ? 1.0 : -1.0;
              metaAttr.y(a) -= sign * push;
              metaAttr.y(b) += sign * push;
            }
            moved = true;
          }
        }
        if (!moved) break;
      }
    }

    // Skip the swap pass entirely above 80 clusters — its inner loop is
    // O(C^2 * M^2) per iteration and chokes on large cluster counts. The
    // FMMM meta-layout already produced reasonable positions; the swap pass
    // is a refinement, not a requirement.
    if (clusterToMeta.size() >= 4 && clusterToMeta.size() <= 80) {
      std::vector<std::string> swapKeys;
      swapKeys.reserve(clusterToMeta.size());
      for (const std::string& key : clusterOrder) {
        if (clusterToMeta.find(key) != clusterToMeta.end()) {
          swapKeys.push_back(key);
        }
      }

      std::vector<std::pair<std::string, std::string>> interPairs;
      interPairs.reserve(interClusterEdgeCount.size());
      std::vector<std::size_t> interWeights;
      interWeights.reserve(interClusterEdgeCount.size());
      for (const auto& [pair, count] : interClusterEdgeCount) {
        if (clusterToMeta.find(pair.first) == clusterToMeta.end() ||
            clusterToMeta.find(pair.second) == clusterToMeta.end()) {
          continue;
        }
        interPairs.push_back(pair);
        interWeights.push_back(count);
      }


      auto segmentsCross = [](double ax, double ay, double bx, double by,
                              double cx, double cy, double dx, double dy) -> bool {
        const double d1x = bx - ax;
        const double d1y = by - ay;
        const double d2x = dx - cx;
        const double d2y = dy - cy;
        const double denom = d1x * d2y - d1y * d2x;
        if (std::abs(denom) < 1e-9) {
          return false;
        }
        const double t = ((cx - ax) * d2y - (cy - ay) * d2x) / denom;
        const double s = ((cx - ax) * d1y - (cy - ay) * d1x) / denom;
        return t > 1e-6 && t < 1.0 - 1e-6 && s > 1e-6 && s < 1.0 - 1e-6;
      };

      auto countWeightedCrossings = [&]() -> std::size_t {
        std::size_t total = 0;
        for (std::size_t i = 0; i + 1 < interPairs.size(); ++i) {
          const ogdf::node ai = clusterToMeta[interPairs[i].first];
          const ogdf::node bi = clusterToMeta[interPairs[i].second];
          const double aix = metaAttr.x(ai), aiy = metaAttr.y(ai);
          const double bix = metaAttr.x(bi), biy = metaAttr.y(bi);
          for (std::size_t j = i + 1; j < interPairs.size(); ++j) {
            if (interPairs[i].first == interPairs[j].first ||
                interPairs[i].first == interPairs[j].second ||
                interPairs[i].second == interPairs[j].first ||
                interPairs[i].second == interPairs[j].second) {
              continue;
            }
            const ogdf::node aj = clusterToMeta[interPairs[j].first];
            const ogdf::node bj = clusterToMeta[interPairs[j].second];
            if (segmentsCross(
                  aix, aiy, bix, biy,
                  metaAttr.x(aj), metaAttr.y(aj), metaAttr.x(bj), metaAttr.y(bj))) {
              total += interWeights[i] * interWeights[j];
            }
          }
        }
        return total;
      };

      std::size_t bestCrossings = countWeightedCrossings();
      bool improved = true;
      // Scale iterations inversely with cluster count squared. At C=25 we
      // get ~16 iterations (the historical default); at C=80 we get ~1.
      const std::size_t maxIterations =
        std::max<std::size_t>(1, (16 * 25 * 25) / (swapKeys.size() * swapKeys.size()));
      for (std::size_t iter = 0; iter < maxIterations && improved; ++iter) {
        improved = false;
        for (std::size_t i = 0; i + 1 < swapKeys.size(); ++i) {
          for (std::size_t j = i + 1; j < swapKeys.size(); ++j) {
            const ogdf::node a = clusterToMeta[swapKeys[i]];
            const ogdf::node b = clusterToMeta[swapKeys[j]];
            const double ax = metaAttr.x(a), ay = metaAttr.y(a);
            const double bx = metaAttr.x(b), by = metaAttr.y(b);
            metaAttr.x(a) = bx; metaAttr.y(a) = by;
            metaAttr.x(b) = ax; metaAttr.y(b) = ay;
            const std::size_t newCrossings = countWeightedCrossings();
            if (newCrossings < bestCrossings) {
              bestCrossings = newCrossings;
              improved = true;
            } else {
              metaAttr.x(a) = ax; metaAttr.y(a) = ay;
              metaAttr.x(b) = bx; metaAttr.y(b) = by;
            }
          }
        }
      }
    }
  }

  for (const std::string& clusterKey : clusterOrder) {
    auto metaIt = clusterToMeta.find(clusterKey);
    auto sizeIt = clusterLocalSize.find(clusterKey);
    if (metaIt == clusterToMeta.end() || sizeIt == clusterLocalSize.end()) {
      continue;
    }
    const double cx = metaAttr.x(metaIt->second);
    const double cy = metaAttr.y(metaIt->second);
    const double offsetX = cx - sizeIt->second.first / 2.0;
    const double offsetY = cy - sizeIt->second.second / 2.0;

    for (std::size_t memberIdx : clusterMembers[clusterKey]) {
      const NodeRecord& node = nodes[memberIdx];
      attributes.x(node.handle) += offsetX;
      attributes.y(node.handle) += offsetY;
    }
  }

  outClusterCount = clusterToMeta.size();
  outInterClusterEdges = 0;
  for (const auto& [_, count] : interClusterEdgeCount) {
    outInterClusterEdges += count;
  }
}

// Hierarchical layout: each node's appLabel is "<parent>/<sub>". Lays out each
// parent's sub-cluster graph independently (calls back into the flat path),
// then places parents in a global meta-layout. Decouples the meta swap pass
// from O(C²·M²) explosion that flat 459-cluster layout caused.
void runHierarchicalClusterLayout(
  const ClusterRunOptions& options,
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  std::size_t& outClusterCount,
  std::size_t& outInterClusterEdges) {
  outClusterCount = 0;
  outInterClusterEdges = 0;

  // 1. Group node indices by parent prefix (chars before "/"). A label without
  // "/" is treated as its own parent (degenerate hierarchy).
  std::unordered_map<std::string, std::vector<std::size_t>> parentMembers;
  std::vector<std::string> parentOrder;
  std::unordered_map<std::string, std::string> idToParent;
  idToParent.reserve(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    const std::string& label = nodes[i].appLabel;
    auto slash = label.find('/');
    std::string parent = (slash == std::string::npos)
      ? (label.empty() ? std::string("_default") : label)
      : label.substr(0, slash);
    auto it = parentMembers.find(parent);
    if (it == parentMembers.end()) {
      parentOrder.push_back(parent);
      parentMembers[parent] = {};
      it = parentMembers.find(parent);
    }
    it->second.push_back(i);
    idToParent[nodes[i].modelId] = parent;
  }

  // 2. For each parent, lay out its sub-graph using the existing flat code.
  // After the call, each member's global attributes hold positions in
  // sub-meta coordinates (relative to a small parent-local origin).
  std::unordered_map<std::string, std::pair<double, double>> parentSize;
  for (const std::string& parent : parentOrder) {
    const std::vector<std::size_t>& members = parentMembers[parent];
    if (members.empty()) continue;

    std::vector<NodeRecord> subNodes;
    subNodes.reserve(members.size());
    std::unordered_set<std::string> memberIds;
    memberIds.reserve(members.size());
    for (std::size_t idx : members) {
      NodeRecord copy = nodes[idx];
      auto slash = copy.appLabel.find('/');
      copy.appLabel = (slash == std::string::npos)
        ? std::string("_sub_0")
        : copy.appLabel.substr(slash + 1);
      memberIds.insert(copy.modelId);
      subNodes.push_back(copy);
    }
    std::vector<EdgeRecord> subEdges;
    subEdges.reserve(edges.size());
    for (const EdgeRecord& edge : edges) {
      if (memberIds.count(edge.sourceModelId) && memberIds.count(edge.targetModelId)) {
        subEdges.push_back(edge);
      }
    }

    ClusterRunOptions subOpts = options;
    subOpts.metaUnitEdgeLength = 50.0;  // sub-cluster placements packed tighter
    subOpts.interClusterPadding = 14.0;

    std::size_t subClusterCount = 0;
    std::size_t subInterEdges = 0;
    runClusteredByAppLayout(subOpts, subNodes, subEdges, attributes,
                            subClusterCount, subInterEdges);

    // Compute parent bbox from member positions, then translate to (0,0) origin.
    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    for (std::size_t idx : members) {
      const NodeRecord& node = nodes[idx];
      const double cx = attributes.x(node.handle);
      const double cy = attributes.y(node.handle);
      minX = std::min(minX, cx - node.width / 2.0);
      minY = std::min(minY, cy - node.height / 2.0);
      maxX = std::max(maxX, cx + node.width / 2.0);
      maxY = std::max(maxY, cy + node.height / 2.0);
    }
    if (!std::isfinite(minX)) {
      minX = 0.0;
      minY = 0.0;
      maxX = 1.0;
      maxY = 1.0;
    }
    for (std::size_t idx : members) {
      const NodeRecord& node = nodes[idx];
      attributes.x(node.handle) -= minX;
      attributes.y(node.handle) -= minY;
    }
    parentSize[parent] = {maxX - minX, maxY - minY};
  }

  // 3. Build parent meta-graph and run FMMM + swap pass at parent level.
  ogdf::Graph metaGraph;
  ogdf::GraphAttributes metaAttr(metaGraph,
    ogdf::GraphAttributes::nodeGraphics | ogdf::GraphAttributes::edgeGraphics);
  std::unordered_map<std::string, ogdf::node> parentToMeta;
  for (const std::string& parent : parentOrder) {
    auto sizeIt = parentSize.find(parent);
    if (sizeIt == parentSize.end()) continue;
    ogdf::node m = metaGraph.newNode();
    metaAttr.width(m) = std::max(120.0, sizeIt->second.first + options.interClusterPadding);
    metaAttr.height(m) = std::max(120.0, sizeIt->second.second + options.interClusterPadding);
    parentToMeta[parent] = m;
  }

  std::map<std::pair<std::string, std::string>, std::size_t> interParentCount;
  for (const EdgeRecord& edge : edges) {
    auto a = idToParent.find(edge.sourceModelId);
    auto b = idToParent.find(edge.targetModelId);
    if (a == idToParent.end() || b == idToParent.end()) continue;
    if (a->second == b->second) continue;
    auto key = a->second < b->second
      ? std::make_pair(a->second, b->second)
      : std::make_pair(b->second, a->second);
    interParentCount[key]++;
  }
  std::vector<std::pair<std::string, std::string>> interPairs;
  std::vector<std::size_t> interWeights;
  interPairs.reserve(interParentCount.size());
  interWeights.reserve(interParentCount.size());
  for (const auto& [pair, count] : interParentCount) {
    auto si = parentToMeta.find(pair.first);
    auto ti = parentToMeta.find(pair.second);
    if (si == parentToMeta.end() || ti == parentToMeta.end()) continue;
    const std::size_t replicate = std::min<std::size_t>(8, count);
    for (std::size_t r = 0; r < replicate; ++r) {
      metaGraph.newEdge(si->second, ti->second);
    }
    interPairs.push_back(pair);
    interWeights.push_back(count);
  }

  if (parentToMeta.size() >= 2) {
    ogdf::FMMMLayout metaLayout;
    metaLayout.useHighLevelOptions(true);
    metaLayout.unitEdgeLength(options.metaUnitEdgeLength);
    metaLayout.newInitialPlacement(true);
    metaLayout.qualityVersusSpeed(ogdf::FMMMOptions::QualityVsSpeed::BeautifulAndFast);
    metaLayout.call(metaAttr);

    // Swap pass on parent positions (parent count typically small).
    if (parentToMeta.size() >= 4 && parentToMeta.size() <= 80
        && !interPairs.empty()) {
      std::vector<std::string> swapKeys;
      for (const std::string& key : parentOrder) {
        if (parentToMeta.count(key)) swapKeys.push_back(key);
      }
      auto segmentsCross = [](double ax, double ay, double bx, double by,
                              double cx, double cy, double dx, double dy) -> bool {
        const double d1x = bx - ax;
        const double d1y = by - ay;
        const double d2x = dx - cx;
        const double d2y = dy - cy;
        const double denom = d1x * d2y - d1y * d2x;
        if (std::abs(denom) < 1e-9) return false;
        const double t = ((cx - ax) * d2y - (cy - ay) * d2x) / denom;
        const double s = ((cx - ax) * d1y - (cy - ay) * d1x) / denom;
        return t > 1e-6 && t < 1.0 - 1e-6 && s > 1e-6 && s < 1.0 - 1e-6;
      };
      auto countWeighted = [&]() -> std::size_t {
        std::size_t total = 0;
        for (std::size_t i = 0; i + 1 < interPairs.size(); ++i) {
          const auto ai = parentToMeta[interPairs[i].first];
          const auto bi = parentToMeta[interPairs[i].second];
          const double aix = metaAttr.x(ai), aiy = metaAttr.y(ai);
          const double bix = metaAttr.x(bi), biy = metaAttr.y(bi);
          for (std::size_t j = i + 1; j < interPairs.size(); ++j) {
            if (interPairs[i].first == interPairs[j].first
                || interPairs[i].first == interPairs[j].second
                || interPairs[i].second == interPairs[j].first
                || interPairs[i].second == interPairs[j].second) continue;
            const auto aj = parentToMeta[interPairs[j].first];
            const auto bj = parentToMeta[interPairs[j].second];
            if (segmentsCross(aix, aiy, bix, biy,
                              metaAttr.x(aj), metaAttr.y(aj),
                              metaAttr.x(bj), metaAttr.y(bj))) {
              total += interWeights[i] * interWeights[j];
            }
          }
        }
        return total;
      };
      std::size_t bestCrossings = countWeighted();
      bool improved = true;
      const std::size_t maxIterations =
        std::max<std::size_t>(1, (16 * 25 * 25) / (swapKeys.size() * swapKeys.size()));
      for (std::size_t iter = 0; iter < maxIterations && improved; ++iter) {
        improved = false;
        for (std::size_t i = 0; i + 1 < swapKeys.size(); ++i) {
          for (std::size_t j = i + 1; j < swapKeys.size(); ++j) {
            const auto a = parentToMeta[swapKeys[i]];
            const auto b = parentToMeta[swapKeys[j]];
            const double ax = metaAttr.x(a), ay = metaAttr.y(a);
            const double bx = metaAttr.x(b), by = metaAttr.y(b);
            metaAttr.x(a) = bx; metaAttr.y(a) = by;
            metaAttr.x(b) = ax; metaAttr.y(b) = ay;
            const std::size_t c = countWeighted();
            if (c < bestCrossings) {
              bestCrossings = c;
              improved = true;
            } else {
              metaAttr.x(a) = ax; metaAttr.y(a) = ay;
              metaAttr.x(b) = bx; metaAttr.y(b) = by;
            }
          }
        }
      }
    }
  }

  // 4. Apply parent meta-position offset to each node (composes with the
  // sub-meta positions written during step 2).
  for (const std::string& parent : parentOrder) {
    auto metaIt = parentToMeta.find(parent);
    auto sizeIt = parentSize.find(parent);
    if (metaIt == parentToMeta.end() || sizeIt == parentSize.end()) continue;
    const double cx = metaAttr.x(metaIt->second);
    const double cy = metaAttr.y(metaIt->second);
    const double offsetX = cx - sizeIt->second.first / 2.0;
    const double offsetY = cy - sizeIt->second.second / 2.0;
    for (std::size_t idx : parentMembers[parent]) {
      const NodeRecord& node = nodes[idx];
      attributes.x(node.handle) += offsetX;
      attributes.y(node.handle) += offsetY;
    }
  }

  outClusterCount = parentToMeta.size();
  outInterClusterEdges = 0;
  for (const auto& [_, count] : interParentCount) {
    outInterClusterEdges += count;
  }
}

void compactWhitespaceAxis(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  bool horizontal,
  double gap) {
  const std::size_t n = nodes.size();
  if (n == 0) {
    return;
  }
  std::vector<std::size_t> order(n);
  for (std::size_t i = 0; i < n; ++i) {
    order[i] = i;
  }
  std::sort(order.begin(), order.end(),
    [&](std::size_t a, std::size_t b) {
      const double aPos = horizontal ? attributes.x(nodes[a].handle) : attributes.y(nodes[a].handle);
      const double bPos = horizontal ? attributes.x(nodes[b].handle) : attributes.y(nodes[b].handle);
      return aPos < bPos;
    });

  std::vector<double> newPos(n, 0.0);
  std::vector<bool> placed(n, false);

  for (std::size_t k = 0; k < order.size(); ++k) {
    const std::size_t idx = order[k];
    const NodeRecord& node = nodes[idx];
    const double w = std::max(1.0, node.width);
    const double h = std::max(1.0, node.height);
    const double halfMain = horizontal ? w / 2.0 : h / 2.0;
    const double crossPos = horizontal ? attributes.y(node.handle) : attributes.x(node.handle);
    const double crossHalf = horizontal ? h / 2.0 : w / 2.0;
    const double thisLow = crossPos - crossHalf;
    const double thisHigh = crossPos + crossHalf;

    double earliestEdge = halfMain;
    for (std::size_t j = 0; j < k; ++j) {
      const std::size_t other = order[j];
      const NodeRecord& otherNode = nodes[other];
      const double otherW = std::max(1.0, otherNode.width);
      const double otherH = std::max(1.0, otherNode.height);
      const double otherCrossPos = horizontal ? attributes.y(otherNode.handle) : attributes.x(otherNode.handle);
      const double otherCrossHalf = horizontal ? otherH / 2.0 : otherW / 2.0;
      const double otherLow = otherCrossPos - otherCrossHalf;
      const double otherHigh = otherCrossPos + otherCrossHalf;
      if (otherHigh < thisLow || otherLow > thisHigh) {
        continue;
      }
      const double otherMainHalf = horizontal ? otherW / 2.0 : otherH / 2.0;
      const double otherEdge = newPos[other] + otherMainHalf + gap + halfMain;
      earliestEdge = std::max(earliestEdge, otherEdge);
    }
    newPos[idx] = earliestEdge;
    placed[idx] = true;
  }

  for (std::size_t i = 0; i < n; ++i) {
    if (horizontal) {
      attributes.x(nodes[i].handle) = newPos[i];
    } else {
      attributes.y(nodes[i].handle) = newPos[i];
    }
  }
}

void compactGlobalLayout(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  double gap) {
  compactWhitespaceAxis(nodes, attributes, true, gap);
  compactWhitespaceAxis(nodes, attributes, false, gap);
  compactWhitespaceAxis(nodes, attributes, true, gap);
}

template <typename Transform>
void transformLayoutGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  Transform transform) {
  for (const NodeRecord& node : nodes) {
    const auto next = transform(
      sanitizeNodeCenterX(node, attributes),
      sanitizeNodeCenterY(node, attributes));
    if (isFiniteCoordinate(next.first) && isFiniteCoordinate(next.second)) {
      attributes.x(node.handle) = next.first;
      attributes.y(node.handle) = next.second;
    }
  }

  for (const EdgeRecord& edge : edges) {
    ogdf::DPolyline transformedBends;
    for (const ogdf::DPoint& bend : attributes.bends(edge.handle)) {
      if (!isFiniteCoordinate(bend.m_x) || !isFiniteCoordinate(bend.m_y)) {
        continue;
      }

      const auto next = transform(bend.m_x, bend.m_y);
      if (isFiniteCoordinate(next.first) && isFiniteCoordinate(next.second)) {
        transformedBends.pushBack(ogdf::DPoint(next.first, next.second));
      }
    }

    attributes.bends(edge.handle) = transformedBends;
  }
}

void applySiftingSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  transformLayoutGeometry(nodes, edges, attributes, [](double x, double y) {
    const auto band = static_cast<long long>(std::floor(x / 520.0));
    const double direction = band % 2 == 0 ? -1.0 : 1.0;
    const double wave = std::sin(x * 0.004) * 18.0;
    return std::make_pair(x + direction * 28.0, y + direction * 84.0 + wave);
  });
}

void clearEdgeBends(
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  for (const EdgeRecord& edge : edges) {
    attributes.bends(edge.handle) = ogdf::DPolyline();
  }
}

void applyGlobalSiftingSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  transformLayoutGeometry(nodes, edges, attributes, [](double x, double y) {
    const auto band = static_cast<long long>(std::floor(y / 360.0));
    const double drift = std::cos(y * 0.003) * 42.0;
    return std::make_pair(x + band * 18.0 + drift, y * 1.015);
  });
}

void applyGreedyInsertSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  transformLayoutGeometry(nodes, edges, attributes, [](double x, double y) {
    const auto layer = static_cast<long long>(std::floor(x / 420.0));
    const double compact = layer % 3 == 0 ? -36.0 : 18.0;
    return std::make_pair(x * 0.965 + compact, y + std::sin(y * 0.006) * 24.0);
  });
}

void applyGreedySwitchSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  transformLayoutGeometry(nodes, edges, attributes, [](double x, double y) {
    const auto lane = static_cast<long long>(std::floor(y / 220.0));
    const double direction = lane % 2 == 0 ? 1.0 : -1.0;
    return std::make_pair(x + direction * 52.0, y + direction * 16.0);
  });
}

void applyGridSiftingSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  constexpr double gridX = 180.0;
  constexpr double gridY = 120.0;
  transformLayoutGeometry(nodes, edges, attributes, [=](double x, double y) {
    const double snappedX = std::round((x + 45.0) / gridX) * gridX;
    const double snappedY = std::round(y / gridY) * gridY;
    const auto row = static_cast<long long>(std::round(snappedY / gridY));
    return std::make_pair(snappedX + (row % 2 == 0 ? 0.0 : 42.0), snappedY);
  });
}

void applySplitSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  transformLayoutGeometry(nodes, edges, attributes, [](double x, double y) {
    const auto lane = static_cast<long long>(std::floor(x / 520.0));
    const double side = lane % 2 == 0 ? -1.0 : 1.0;
    return std::make_pair(x + side * 88.0, y * 0.985 + side * 34.0);
  });
}

void applyPlanarSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  transformLayoutGeometry(nodes, edges, attributes, [](double x, double y) {
    return std::make_pair(x + y * 0.055, y + x * 0.018);
  });
}

void applyOrthogonalSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  constexpr double gridX = 220.0;
  constexpr double gridY = 150.0;
  transformLayoutGeometry(nodes, edges, attributes, [=](double x, double y) {
    return std::make_pair(std::round(x / gridX) * gridX, std::round(y / gridY) * gridY);
  });

  for (const EdgeRecord& edge : edges) {
    const double sourceX = attributes.x(edge.sourceHandle);
    const double sourceY = attributes.y(edge.sourceHandle);
    const double targetX = attributes.x(edge.targetHandle);
    const double targetY = attributes.y(edge.targetHandle);
    ogdf::DPolyline bends;
    if (std::abs(sourceX - targetX) > 1.0 && std::abs(sourceY - targetY) > 1.0) {
      bends.pushBack(ogdf::DPoint(sourceX, targetY));
    }
    attributes.bends(edge.handle) = bends;
  }
}

void applyPlanarGridSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  constexpr double gridX = 240.0;
  constexpr double gridY = 160.0;
  transformLayoutGeometry(nodes, edges, attributes, [=](double x, double y) {
    const double snappedX = std::round(x / gridX) * gridX;
    const double snappedY = std::round(y / gridY) * gridY;
    const auto column = static_cast<long long>(std::round(snappedX / gridX));
    const double stagger = column % 2 == 0 ? 0.0 : gridY * 0.35;
    return std::make_pair(snappedX, snappedY + stagger);
  });
}

void applyStraightLineSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  clearEdgeBends(edges, attributes);
  transformLayoutGeometry(nodes, edges, attributes, [](double x, double y) {
    return std::make_pair(x * 1.03 + y * 0.025, y * 0.97);
  });
}

void applySchnyderSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  clearEdgeBends(edges, attributes);
  transformLayoutGeometry(nodes, edges, attributes, [](double x, double y) {
    const double skewX = x + y * 0.33;
    const double skewY = y * 0.82;
    return std::make_pair(skewX, skewY);
  });
}

void applyUpwardSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  bool layerBased) {
  clearEdgeBends(edges, attributes);
  transformLayoutGeometry(nodes, edges, attributes, [=](double x, double y) {
    const auto rank = static_cast<long long>(std::floor(x / 480.0));
    if (layerBased) {
      return std::make_pair(x + (rank % 2 == 0 ? 0.0 : 60.0), y + rank * 22.0);
    }

    const double diagonalLift = static_cast<double>(rank) * 34.0;
    return std::make_pair(x * 1.018 + y * 0.018, y * 0.965 + diagonalLift);
  });
}

void applyVisibilitySurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  constexpr double gridX = 240.0;
  constexpr double gridY = 110.0;
  transformLayoutGeometry(nodes, edges, attributes, [=](double x, double y) {
    return std::make_pair(std::round(x / gridX) * gridX, std::round(y / gridY) * gridY);
  });
  applyOrthogonalSurrogateGeometry(nodes, edges, attributes);
}

void applyPivotMdsGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  const double angle = 0.045;
  const double cosine = std::cos(angle);
  const double sine = std::sin(angle);
  transformLayoutGeometry(nodes, edges, attributes, [=](double x, double y) {
    return std::make_pair(
      x * cosine - y * sine,
      x * sine + y * cosine * 0.94);
  });
}

void applyUmlPlanarSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  transformLayoutGeometry(nodes, edges, attributes, [](double x, double y) {
    const double lane = std::floor(x / 620.0);
    return std::make_pair(x + y * 0.035 + lane * 12.0, y * 0.972 + x * 0.024);
  });
}

std::string clusterKeyForModelId(const std::string& modelId) {
  const std::size_t delimiter = modelId.find('.');
  if (delimiter == std::string::npos || delimiter == 0) {
    return "(default)";
  }

  return modelId.substr(0, delimiter);
}

struct ClusterGroupLayout {
  std::vector<std::size_t> nodeIndices;
  double height = 0.0;
  double width = 0.0;
};

void applyClusterSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  bool orthogonal) {
  std::unordered_map<std::string, std::size_t> groupIndexByKey;
  std::vector<ClusterGroupLayout> groups;

  for (std::size_t index = 0; index < nodes.size(); ++index) {
    const std::string key = clusterKeyForModelId(nodes[index].modelId);
    auto inserted = groupIndexByKey.emplace(key, groups.size());
    if (inserted.second) {
      groups.emplace_back();
    }
    groups[inserted.first->second].nodeIndices.push_back(index);
  }

  if (groups.empty()) {
    return;
  }

  const std::size_t groupColumns = std::max<std::size_t>(
    1,
    static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(groups.size())))));
  const std::size_t groupRows = (groups.size() + groupColumns - 1) / groupColumns;
  std::vector<double> columnWidths(groupColumns, 0.0);
  std::vector<double> rowHeights(groupRows, 0.0);

  for (std::size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
    ClusterGroupLayout& group = groups[groupIndex];
    double maxWidth = 120.0;
    double maxHeight = 80.0;

    for (std::size_t nodeIndex : group.nodeIndices) {
      maxWidth = std::max(maxWidth, sanitizeNodeWidth(nodes[nodeIndex], attributes));
      maxHeight = std::max(maxHeight, sanitizeNodeHeight(nodes[nodeIndex], attributes));
    }

    const std::size_t columns = std::max<std::size_t>(
      1,
      static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(group.nodeIndices.size())))));
    const std::size_t rows = (group.nodeIndices.size() + columns - 1) / columns;
    const double cellWidth = maxWidth + (orthogonal ? 130.0 : 160.0);
    const double cellHeight = maxHeight + (orthogonal ? 100.0 : 130.0);
    group.width = static_cast<double>(columns) * cellWidth + 220.0;
    group.height = static_cast<double>(rows) * cellHeight + 220.0;
    columnWidths[groupIndex % groupColumns] =
      std::max(columnWidths[groupIndex % groupColumns], group.width);
    rowHeights[groupIndex / groupColumns] =
      std::max(rowHeights[groupIndex / groupColumns], group.height);
  }

  std::vector<double> columnOrigins(groupColumns, 0.0);
  std::vector<double> rowOrigins(groupRows, 0.0);
  for (std::size_t index = 1; index < groupColumns; ++index) {
    columnOrigins[index] = columnOrigins[index - 1] + columnWidths[index - 1] + 420.0;
  }
  for (std::size_t index = 1; index < groupRows; ++index) {
    rowOrigins[index] = rowOrigins[index - 1] + rowHeights[index - 1] + 360.0;
  }

  for (std::size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
    const ClusterGroupLayout& group = groups[groupIndex];
    double maxWidth = 120.0;
    double maxHeight = 80.0;

    for (std::size_t nodeIndex : group.nodeIndices) {
      maxWidth = std::max(maxWidth, sanitizeNodeWidth(nodes[nodeIndex], attributes));
      maxHeight = std::max(maxHeight, sanitizeNodeHeight(nodes[nodeIndex], attributes));
    }

    const std::size_t columns = std::max<std::size_t>(
      1,
      static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(group.nodeIndices.size())))));
    const double cellWidth = maxWidth + (orthogonal ? 130.0 : 160.0);
    const double cellHeight = maxHeight + (orthogonal ? 100.0 : 130.0);
    const double originX = columnOrigins[groupIndex % groupColumns] + 110.0;
    const double originY = rowOrigins[groupIndex / groupColumns] + 110.0;

    for (std::size_t localIndex = 0; localIndex < group.nodeIndices.size(); ++localIndex) {
      const NodeRecord& node = nodes[group.nodeIndices[localIndex]];
      const std::size_t column = localIndex % columns;
      const std::size_t row = localIndex / columns;
      const double stagger = orthogonal || row % 2 == 0 ? 0.0 : cellWidth * 0.18;
      attributes.x(node.handle) = originX + static_cast<double>(column) * cellWidth + stagger;
      attributes.y(node.handle) = originY + static_cast<double>(row) * cellHeight;
    }
  }

  if (orthogonal) {
    for (const EdgeRecord& edge : edges) {
      const double sourceX = attributes.x(edge.sourceHandle);
      const double sourceY = attributes.y(edge.sourceHandle);
      const double targetX = attributes.x(edge.targetHandle);
      const double targetY = attributes.y(edge.targetHandle);
      ogdf::DPolyline bends;
      if (std::abs(sourceX - targetX) > 1.0 && std::abs(sourceY - targetY) > 1.0) {
        bends.pushBack(ogdf::DPoint(sourceX, targetY));
      }
      attributes.bends(edge.handle) = bends;
    }
  } else {
    clearEdgeBends(edges, attributes);
  }
}

std::vector<std::vector<std::size_t>> collectConnectedComponents(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges) {
  std::unordered_map<ogdf::node, std::size_t> indicesByNode;
  indicesByNode.reserve(nodes.size());
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    indicesByNode.emplace(nodes[index].handle, index);
  }

  std::vector<std::vector<std::size_t>> adjacency(nodes.size());
  for (const EdgeRecord& edge : edges) {
    const auto source = indicesByNode.find(edge.sourceHandle);
    const auto target = indicesByNode.find(edge.targetHandle);
    if (
      source == indicesByNode.end()
      || target == indicesByNode.end()
      || source->second == target->second) {
      continue;
    }
    adjacency[source->second].push_back(target->second);
    adjacency[target->second].push_back(source->second);
  }

  std::vector<std::vector<std::size_t>> components;
  std::vector<bool> seen(nodes.size(), false);
  for (std::size_t start = 0; start < nodes.size(); ++start) {
    if (seen[start]) {
      continue;
    }

    std::vector<std::size_t> component;
    std::queue<std::size_t> pending;
    pending.push(start);
    seen[start] = true;

    while (!pending.empty()) {
      const std::size_t current = pending.front();
      pending.pop();
      component.push_back(current);

      for (std::size_t next : adjacency[current]) {
        if (seen[next]) {
          continue;
        }
        seen[next] = true;
        pending.push(next);
      }
    }

    components.push_back(component);
  }

  return components;
}

std::vector<std::vector<std::size_t>> buildUndirectedAdjacency(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges) {
  std::unordered_map<ogdf::node, std::size_t> indicesByNode;
  indicesByNode.reserve(nodes.size());
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    indicesByNode.emplace(nodes[index].handle, index);
  }

  std::vector<std::vector<std::size_t>> adjacency(nodes.size());
  for (const EdgeRecord& edge : edges) {
    const auto source = indicesByNode.find(edge.sourceHandle);
    const auto target = indicesByNode.find(edge.targetHandle);
    if (
      source == indicesByNode.end()
      || target == indicesByNode.end()
      || source->second == target->second) {
      continue;
    }

    auto& sourceNeighbors = adjacency[source->second];
    if (
      std::find(sourceNeighbors.begin(), sourceNeighbors.end(), target->second)
      == sourceNeighbors.end()) {
      sourceNeighbors.push_back(target->second);
    }

    auto& targetNeighbors = adjacency[target->second];
    if (
      std::find(targetNeighbors.begin(), targetNeighbors.end(), source->second)
      == targetNeighbors.end()) {
      targetNeighbors.push_back(source->second);
    }
  }

  return adjacency;
}

Rect componentRect(
  const std::vector<NodeRecord>& nodes,
  const std::vector<std::size_t>& component,
  ogdf::GraphAttributes& attributes) {
  Rect rect;
  bool initialized = false;

  for (std::size_t nodeIndex : component) {
    const Rect node = nodeRect(nodes[nodeIndex], attributes);
    if (!initialized) {
      rect = node;
      initialized = true;
      continue;
    }

    rect.left = std::min(rect.left, node.left);
    rect.right = std::max(rect.right, node.right);
    rect.top = std::min(rect.top, node.top);
    rect.bottom = std::max(rect.bottom, node.bottom);
  }

  return rect;
}

void translateComponent(
  const std::vector<NodeRecord>& nodes,
  const std::vector<std::size_t>& component,
  ogdf::GraphAttributes& attributes,
  double dx,
  double dy) {
  for (std::size_t nodeIndex : component) {
    const NodeRecord& node = nodes[nodeIndex];
    attributes.x(node.handle) = sanitizeNodeCenterX(node, attributes) + dx;
    attributes.y(node.handle) = sanitizeNodeCenterY(node, attributes) + dy;
  }
}

void pullLowDegreeNodesInward(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  int maxIterations,
  std::size_t degreeThreshold,
  double damping) {
  if (nodes.empty()) {
    return;
  }
  std::unordered_map<std::string, std::size_t> indexById;
  indexById.reserve(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    indexById[nodes[i].modelId] = i;
  }
  std::vector<std::vector<std::size_t>> neighbors(nodes.size());
  for (const EdgeRecord& edge : edges) {
    const auto srcIt = indexById.find(edge.sourceModelId);
    const auto tgtIt = indexById.find(edge.targetModelId);
    if (srcIt == indexById.end() || tgtIt == indexById.end() || srcIt->second == tgtIt->second) {
      continue;
    }
    neighbors[srcIt->second].push_back(tgtIt->second);
    neighbors[tgtIt->second].push_back(srcIt->second);
  }
  for (auto& list : neighbors) {
    std::sort(list.begin(), list.end());
    list.erase(std::unique(list.begin(), list.end()), list.end());
  }

  // Compute mean inter-node spacing across the graph as a target distance budget.
  double sumX = 0.0;
  double sumY = 0.0;
  double sumW = 0.0;
  double sumH = 0.0;
  for (const NodeRecord& node : nodes) {
    sumW += std::max(1.0, node.width);
    sumH += std::max(1.0, node.height);
    sumX += attributes.x(node.handle);
    sumY += attributes.y(node.handle);
  }
  const double avgW = sumW / static_cast<double>(nodes.size());
  const double avgH = sumH / static_cast<double>(nodes.size());
  const double targetMinDistance = std::max(avgW, avgH) * 1.2;

  auto findOverlap = [&](std::size_t i, double newX, double newY) -> bool {
    const double width = std::max(1.0, nodes[i].width);
    const double height = std::max(1.0, nodes[i].height);
    for (std::size_t k = 0; k < nodes.size(); ++k) {
      if (k == i) continue;
      const double otherX = attributes.x(nodes[k].handle);
      const double otherY = attributes.y(nodes[k].handle);
      const double otherW = std::max(1.0, nodes[k].width);
      const double otherH = std::max(1.0, nodes[k].height);
      const double dx = std::abs(newX - otherX);
      const double dy = std::abs(newY - otherY);
      if (dx < (width + otherW) / 2.0 + 4.0 && dy < (height + otherH) / 2.0 + 4.0) {
        return true;
      }
    }
    return false;
  };

  for (int iter = 0; iter < maxIterations; ++iter) {
    bool moved = false;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      const std::vector<std::size_t>& adj = neighbors[i];
      if (adj.empty() || adj.size() > degreeThreshold) {
        continue;
      }
      double neighSumX = 0.0;
      double neighSumY = 0.0;
      for (std::size_t j : adj) {
        neighSumX += attributes.x(nodes[j].handle);
        neighSumY += attributes.y(nodes[j].handle);
      }
      const double targetX = neighSumX / static_cast<double>(adj.size());
      const double targetY = neighSumY / static_cast<double>(adj.size());
      const double currentX = attributes.x(nodes[i].handle);
      const double currentY = attributes.y(nodes[i].handle);
      const double distFromTarget = std::hypot(currentX - targetX, currentY - targetY);
      if (distFromTarget <= targetMinDistance * 4.0) {
        continue;
      }

      const double newX = currentX * (1.0 - damping) + targetX * damping;
      const double newY = currentY * (1.0 - damping) + targetY * damping;
      if (findOverlap(i, newX, newY)) {
        continue;
      }
      attributes.x(nodes[i].handle) = newX;
      attributes.y(nodes[i].handle) = newY;
      moved = true;
    }
    if (!moved) {
      break;
    }
  }
}

void compactClusterOutliers(
  const std::vector<NodeRecord>& nodes,
  const std::unordered_map<std::string, std::string>& clusterByModelId,
  ogdf::GraphAttributes& attributes,
  double outlierMedianMultiplier) {
  if (clusterByModelId.empty() || nodes.empty()) {
    return;
  }
  std::unordered_map<std::string, std::vector<std::size_t>> membersByCluster;
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    auto it = clusterByModelId.find(nodes[i].modelId);
    if (it == clusterByModelId.end()) {
      continue;
    }
    membersByCluster[it->second].push_back(i);
  }
  for (int iteration = 0; iteration < 2; ++iteration) {
    bool moved = false;
    for (const auto& [clusterKey, members] : membersByCluster) {
      if (members.size() < 4) {
        continue;
      }
      double sumX = 0.0;
      double sumY = 0.0;
      for (std::size_t idx : members) {
        sumX += attributes.x(nodes[idx].handle);
        sumY += attributes.y(nodes[idx].handle);
      }
      const double cx = sumX / static_cast<double>(members.size());
      const double cy = sumY / static_cast<double>(members.size());
      std::vector<double> distances;
      distances.reserve(members.size());
      for (std::size_t idx : members) {
        distances.push_back(std::hypot(
          attributes.x(nodes[idx].handle) - cx,
          attributes.y(nodes[idx].handle) - cy));
      }
      std::vector<double> sortedDist = distances;
      std::sort(sortedDist.begin(), sortedDist.end());
      const double median = sortedDist[sortedDist.size() / 2];
      const double percentile75 = sortedDist[(sortedDist.size() * 3) / 4];
      const double outlierCutoff = std::max(
        std::min(median * outlierMedianMultiplier, percentile75 * 1.2),
        1.0);
      for (std::size_t k = 0; k < members.size(); ++k) {
        if (distances[k] <= outlierCutoff) {
          continue;
        }
        const std::size_t idx = members[k];
        const double currentX = attributes.x(nodes[idx].handle);
        const double currentY = attributes.y(nodes[idx].handle);
        const double scale = outlierCutoff / distances[k];
        attributes.x(nodes[idx].handle) = cx + (currentX - cx) * scale;
        attributes.y(nodes[idx].handle) = cy + (currentY - cy) * scale;
        moved = true;
      }
    }
    if (!moved) {
      break;
    }
  }
}

void packDisconnectedComponents(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  std::vector<std::vector<std::size_t>> components = collectConnectedComponents(nodes, edges);
  if (components.size() <= 1) {
    return;
  }

  std::sort(
    components.begin(),
    components.end(),
    [&](const auto& left, const auto& right) {
      const Rect leftRect = componentRect(nodes, left, attributes);
      const Rect rightRect = componentRect(nodes, right, attributes);
      const double leftArea = rectWidth(leftRect) * rectHeight(leftRect);
      const double rightArea = rectWidth(rightRect) * rectHeight(rightRect);
      if (std::abs(leftArea - rightArea) > 0.01) {
        return leftArea > rightArea;
      }
      return left.size() > right.size();
    });

  constexpr double componentGapX = 220.0;
  constexpr double componentGapY = 180.0;
  double totalPackedArea = 0.0;
  double widest = 0.0;
  for (const auto& component : components) {
    const Rect rect = componentRect(nodes, component, attributes);
    totalPackedArea += (rectWidth(rect) + componentGapX) * (rectHeight(rect) + componentGapY);
    widest = std::max(widest, rectWidth(rect));
  }

  const double targetRowWidth = std::max(widest, std::sqrt(totalPackedArea) * 1.28);
  double cursorX = 0.0;
  double cursorY = 0.0;
  double rowHeight = 0.0;

  for (const auto& component : components) {
    const Rect rect = componentRect(nodes, component, attributes);
    const double width = rectWidth(rect);
    const double height = rectHeight(rect);

    if (cursorX > 0.0 && cursorX + width > targetRowWidth) {
      cursorX = 0.0;
      cursorY += rowHeight + componentGapY;
      rowHeight = 0.0;
    }

    translateComponent(nodes, component, attributes, cursorX - rect.left, cursorY - rect.top);
    cursorX += width + componentGapX;
    rowHeight = std::max(rowHeight, height);
  }

  clearEdgeBends(edges, attributes);
}

double centerDistance(
  const NodeRecord& left,
  const NodeRecord& right,
  ogdf::GraphAttributes& attributes) {
  const double dx = sanitizeNodeCenterX(left, attributes) - sanitizeNodeCenterX(right, attributes);
  const double dy = sanitizeNodeCenterY(left, attributes) - sanitizeNodeCenterY(right, attributes);
  return std::hypot(dx, dy);
}

double medianValue(std::vector<double> values) {
  if (values.empty()) {
    return 0.0;
  }

  std::sort(values.begin(), values.end());
  const std::size_t middle = values.size() / 2;
  if (values.size() % 2 == 1) {
    return values[middle];
  }

  return (values[middle - 1] + values[middle]) / 2.0;
}

void compactDistantConnectedNodes(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  if (nodes.size() <= 2 || edges.empty()) {
    return;
  }

  std::unordered_map<ogdf::node, std::size_t> indicesByNode;
  indicesByNode.reserve(nodes.size());
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    indicesByNode.emplace(nodes[index].handle, index);
  }

  std::vector<std::vector<std::size_t>> neighbors(nodes.size());
  std::vector<double> edgeLengths;
  edgeLengths.reserve(edges.size());

  for (const EdgeRecord& edge : edges) {
    const auto source = indicesByNode.find(edge.sourceHandle);
    const auto target = indicesByNode.find(edge.targetHandle);
    if (
      source == indicesByNode.end()
      || target == indicesByNode.end()
      || source->second == target->second) {
      continue;
    }

    neighbors[source->second].push_back(target->second);
    neighbors[target->second].push_back(source->second);
    const double length = centerDistance(nodes[source->second], nodes[target->second], attributes);
    if (length > 1.0 && isFiniteCoordinate(length)) {
      edgeLengths.push_back(length);
    }
  }

  const double medianEdgeLength = medianValue(std::move(edgeLengths));
  if (medianEdgeLength <= 1.0) {
    return;
  }

  const double threshold = std::max(
    kDistantEdgeMinThreshold,
    medianEdgeLength * kDistantEdgeLengthFactor);
  const double targetDistance = std::max(
    kDistantEdgeMinTarget,
    std::min(kDistantEdgeMaxTarget, medianEdgeLength * kDistantEdgeTargetFactor));

  for (std::size_t index = 0; index < nodes.size(); ++index) {
    if (neighbors[index].empty()) {
      continue;
    }

    double neighborX = 0.0;
    double neighborY = 0.0;
    for (std::size_t neighbor : neighbors[index]) {
      neighborX += sanitizeNodeCenterX(nodes[neighbor], attributes);
      neighborY += sanitizeNodeCenterY(nodes[neighbor], attributes);
    }
    neighborX /= static_cast<double>(neighbors[index].size());
    neighborY /= static_cast<double>(neighbors[index].size());

    const double centerX = sanitizeNodeCenterX(nodes[index], attributes);
    const double centerY = sanitizeNodeCenterY(nodes[index], attributes);
    const double dx = centerX - neighborX;
    const double dy = centerY - neighborY;
    const double distance = std::hypot(dx, dy);
    if (distance <= threshold || distance <= 1.0) {
      continue;
    }

    const double directionX = dx / distance;
    const double directionY = dy / distance;
    const double tangentOffset = (static_cast<double>(index % 7) - 3.0) * 18.0;
    attributes.x(nodes[index].handle) =
      neighborX + directionX * targetDistance - directionY * tangentOffset;
    attributes.y(nodes[index].handle) =
      neighborY + directionY * targetDistance + directionX * tangentOffset;
  }
}

void resolveNodeOverlaps(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  if (nodes.size() <= 1) {
    return;
  }

  std::vector<std::size_t> order;
  order.reserve(nodes.size());
  double maxWidth = 1.0;
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    order.push_back(index);
    maxWidth = std::max(maxWidth, sanitizeNodeWidth(nodes[index], attributes));
  }

  for (int iteration = 0; iteration < kOverlapRelaxationIterations; ++iteration) {
    std::sort(
      order.begin(),
      order.end(),
      [&](std::size_t left, std::size_t right) {
        return sanitizeNodeCenterX(nodes[left], attributes)
          < sanitizeNodeCenterX(nodes[right], attributes);
      });

    double maxShift = 0.0;
    bool moved = false;

    for (std::size_t leftOrder = 0; leftOrder < order.size(); ++leftOrder) {
      const std::size_t leftIndex = order[leftOrder];
      const NodeRecord& left = nodes[leftIndex];
      const double leftX = sanitizeNodeCenterX(left, attributes);
      const double leftY = sanitizeNodeCenterY(left, attributes);
      const double leftWidth = sanitizeNodeWidth(left, attributes);
      const double leftHeight = sanitizeNodeHeight(left, attributes);

      for (std::size_t rightOrder = leftOrder + 1; rightOrder < order.size(); ++rightOrder) {
        const std::size_t rightIndex = order[rightOrder];
        const NodeRecord& right = nodes[rightIndex];
        const double rightX = sanitizeNodeCenterX(right, attributes);
        const double dx = rightX - leftX;
        if (dx > maxWidth + kPostLayoutNodeGapX) {
          break;
        }

        const double rightY = sanitizeNodeCenterY(right, attributes);
        const double rightWidth = sanitizeNodeWidth(right, attributes);
        const double rightHeight = sanitizeNodeHeight(right, attributes);
        const double overlapX =
          (leftWidth + rightWidth) / 2.0 + kPostLayoutNodeGapX - std::abs(dx);
        if (overlapX <= 0.0) {
          continue;
        }

        const double dy = rightY - leftY;
        const double overlapY =
          (leftHeight + rightHeight) / 2.0 + kPostLayoutNodeGapY - std::abs(dy);
        if (overlapY <= 0.0) {
          continue;
        }

        if (overlapX <= overlapY) {
          const double direction = std::abs(dx) < 0.01
            ? (leftIndex % 2 == 0 ? 1.0 : -1.0)
            : (dx >= 0.0 ? 1.0 : -1.0);
          const double shift = overlapX / 2.0 + 2.0;
          attributes.x(left.handle) -= direction * shift;
          attributes.x(right.handle) += direction * shift;
          maxShift = std::max(maxShift, shift);
        } else {
          const double direction = std::abs(dy) < 0.01
            ? (leftIndex % 2 == 0 ? 1.0 : -1.0)
            : (dy >= 0.0 ? 1.0 : -1.0);
          const double shift = overlapY / 2.0 + 2.0;
          attributes.y(left.handle) -= direction * shift;
          attributes.y(right.handle) += direction * shift;
          maxShift = std::max(maxShift, shift);
        }
        moved = true;
      }
    }

    if (!moved || maxShift < 0.1) {
      break;
    }
  }
}

Rect expandedNodeRectAt(
  const NodeRecord& node,
  ogdf::GraphAttributes& attributes,
  double centerX,
  double centerY) {
  const double width = sanitizeNodeWidth(node, attributes);
  const double height = sanitizeNodeHeight(node, attributes);
  return {
    centerY + height / 2.0 + kPostLayoutNodeGapY / 2.0,
    centerX - width / 2.0 - kPostLayoutNodeGapX / 2.0,
    centerX + width / 2.0 + kPostLayoutNodeGapX / 2.0,
    centerY - height / 2.0 - kPostLayoutNodeGapY / 2.0,
  };
}

bool rectsOverlap(const Rect& left, const Rect& right) {
  return left.left < right.right
    && left.right > right.left
    && left.top < right.bottom
    && left.bottom > right.top;
}

bool hasNodeSpacingConflicts(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  for (std::size_t leftIndex = 0; leftIndex < nodes.size(); ++leftIndex) {
    const Rect left = expandedNodeRectAt(
      nodes[leftIndex],
      attributes,
      sanitizeNodeCenterX(nodes[leftIndex], attributes),
      sanitizeNodeCenterY(nodes[leftIndex], attributes));
    for (std::size_t rightIndex = leftIndex + 1; rightIndex < nodes.size(); ++rightIndex) {
      const Rect right = expandedNodeRectAt(
        nodes[rightIndex],
        attributes,
        sanitizeNodeCenterX(nodes[rightIndex], attributes),
        sanitizeNodeCenterY(nodes[rightIndex], attributes));
      if (rectsOverlap(left, right)) {
        return true;
      }
    }
  }

  return false;
}

bool overlapsPlacedRects(const Rect& rect, const std::vector<Rect>& placedRects) {
  return std::any_of(
    placedRects.begin(),
    placedRects.end(),
    [&](const Rect& placed) {
      return rectsOverlap(rect, placed);
    });
}

void placeNodesWithoutOverlaps(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  if (nodes.size() <= 1) {
    return;
  }

  std::vector<std::size_t> order;
  order.reserve(nodes.size());
  double totalWidth = 0.0;
  double totalHeight = 0.0;
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    order.push_back(index);
    totalWidth += sanitizeNodeWidth(nodes[index], attributes);
    totalHeight += sanitizeNodeHeight(nodes[index], attributes);
  }

  std::sort(
    order.begin(),
    order.end(),
    [&](std::size_t left, std::size_t right) {
      const double leftY = sanitizeNodeCenterY(nodes[left], attributes);
      const double rightY = sanitizeNodeCenterY(nodes[right], attributes);
      if (std::abs(leftY - rightY) > 0.01) {
        return leftY < rightY;
      }
      return sanitizeNodeCenterX(nodes[left], attributes)
        < sanitizeNodeCenterX(nodes[right], attributes);
    });

  const double averageWidth = totalWidth / static_cast<double>(nodes.size());
  const double averageHeight = totalHeight / static_cast<double>(nodes.size());
  const double stepX = std::max(averageWidth + kPostLayoutNodeGapX, 160.0);
  const double stepY = std::max(averageHeight + kPostLayoutNodeGapY, 120.0);
  const int maxRing = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(nodes.size())))) + 12;
  std::vector<Rect> placedRects;
  placedRects.reserve(nodes.size());

  for (std::size_t nodeIndex : order) {
    const NodeRecord& node = nodes[nodeIndex];
    const double desiredX = sanitizeNodeCenterX(node, attributes);
    const double desiredY = sanitizeNodeCenterY(node, attributes);
    double bestX = desiredX;
    double bestY = desiredY;
    bool placed = false;

    const Rect desiredRect = expandedNodeRectAt(node, attributes, desiredX, desiredY);
    if (!overlapsPlacedRects(desiredRect, placedRects)) {
      placed = true;
    }

    for (int ring = 1; !placed && ring <= maxRing; ++ring) {
      double bestDistance = std::numeric_limits<double>::infinity();
      for (int offsetY = -ring; offsetY <= ring; ++offsetY) {
        for (int offsetX = -ring; offsetX <= ring; ++offsetX) {
          if (std::abs(offsetX) != ring && std::abs(offsetY) != ring) {
            continue;
          }

          const double candidateX = desiredX + static_cast<double>(offsetX) * stepX;
          const double candidateY = desiredY + static_cast<double>(offsetY) * stepY;
          const Rect candidateRect = expandedNodeRectAt(node, attributes, candidateX, candidateY);
          if (overlapsPlacedRects(candidateRect, placedRects)) {
            continue;
          }

          const double distance =
            std::pow(candidateX - desiredX, 2.0) + std::pow(candidateY - desiredY, 2.0);
          if (distance < bestDistance) {
            bestDistance = distance;
            bestX = candidateX;
            bestY = candidateY;
            placed = true;
          }
        }
      }
    }

    if (!placed) {
      const std::size_t fallbackIndex = placedRects.size();
      const std::size_t columns = std::max<std::size_t>(
        1,
        static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(nodes.size())))));
      bestX = static_cast<double>(fallbackIndex % columns) * stepX;
      bestY = static_cast<double>(fallbackIndex / columns) * stepY;
    }

    attributes.x(node.handle) = bestX;
    attributes.y(node.handle) = bestY;
    placedRects.push_back(expandedNodeRectAt(node, attributes, bestX, bestY));
  }
}

void enforceNodeSeparation(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  resolveNodeOverlaps(nodes, attributes);
  if (hasNodeSpacingConflicts(nodes, attributes)) {
    placeNodesWithoutOverlaps(nodes, attributes);
  }
}

Rect graphNodeBounds(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  Rect bounds;
  bool initialized = false;
  for (const NodeRecord& node : nodes) {
    const Rect rect = nodeRect(node, attributes);
    if (!initialized) {
      bounds = rect;
      initialized = true;
      continue;
    }
    bounds.left = std::min(bounds.left, rect.left);
    bounds.right = std::max(bounds.right, rect.right);
    bounds.top = std::min(bounds.top, rect.top);
    bounds.bottom = std::max(bounds.bottom, rect.bottom);
  }
  return bounds;
}

bool compactExcessiveLayoutFootprint(
  const std::string& mode,
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  if (nodes.size() <= 2) {
    return false;
  }

  const Rect bounds = graphNodeBounds(nodes, attributes);
  const double width = rectWidth(bounds);
  const double height = rectHeight(bounds);
  if (width <= 1.0 || height <= 1.0) {
    return false;
  }

  const double centerX = rectCenterX(bounds);
  const double centerY = rectCenterY(bounds);
  const double nodeFactor = std::sqrt(static_cast<double>(nodes.size()));

  if (
    mode == "fast_multipole"
    || mode == "fast_multipole_multilevel"
    || isConstrainedForceMode(mode)) {
    const double targetMaxDimension = std::max(24000.0, nodeFactor * 680.0);
    const double maxDimension = std::max(width, height);
    if (maxDimension <= targetMaxDimension) {
      return false;
    }

    const double scale = std::max(0.22, targetMaxDimension / maxDimension);
    transformLayoutGeometry(nodes, edges, attributes, [=](double x, double y) {
      return std::make_pair(
        centerX + (x - centerX) * scale,
        centerY + (y - centerY) * scale);
    });
    return true;
  }

  if (isSugiyamaMode(mode)) {
    const double targetWidth = std::max(28000.0, std::max(height * 2.2, nodeFactor * 760.0));
    if (width <= targetWidth) {
      return false;
    }

    const double scaleX = std::max(0.24, targetWidth / width);
    transformLayoutGeometry(nodes, edges, attributes, [=](double x, double y) {
      return std::make_pair(centerX + (x - centerX) * scaleX, y);
    });
    return true;
  }

  return false;
}

double clampToSpan(double value, double minValue, double maxValue) {
  if (minValue > maxValue) {
    return (minValue + maxValue) / 2.0;
  }
  return std::max(minValue, std::min(maxValue, value));
}

bool almostSamePoint(const RoutePoint& left, const RoutePoint& right) {
  return std::abs(left.x - right.x) < 0.01 && std::abs(left.y - right.y) < 0.01;
}

bool isCollinear(const RoutePoint& left, const RoutePoint& middle, const RoutePoint& right) {
  return (
      std::abs(left.x - middle.x) < 0.01
      && std::abs(middle.x - right.x) < 0.01)
    || (
      std::abs(left.y - middle.y) < 0.01
      && std::abs(middle.y - right.y) < 0.01);
}

std::vector<RoutePoint> compressRoutePoints(std::vector<RoutePoint> points) {
  std::vector<RoutePoint> deduped;
  for (RoutePoint point : points) {
    if (!isFiniteCoordinate(point.x) || !isFiniteCoordinate(point.y)) {
      continue;
    }
    point.x = std::round(point.x * 100.0) / 100.0;
    point.y = std::round(point.y * 100.0) / 100.0;
    if (!deduped.empty() && almostSamePoint(deduped.back(), point)) {
      continue;
    }
    deduped.push_back(point);
  }

  std::vector<RoutePoint> compressed;
  for (const RoutePoint& point : deduped) {
    if (compressed.size() >= 2) {
      const RoutePoint& prev = compressed[compressed.size() - 1];
      const RoutePoint& prevPrev = compressed[compressed.size() - 2];
      if (isCollinear(prevPrev, prev, point)) {
        compressed.pop_back();
      }
    }
    compressed.push_back(point);
  }

  return compressed;
}

std::size_t applyPositionsTsvOverride(
  const std::string& positionsTsv,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  if (positionsTsv.empty()) {
    return 0;
  }
  std::ifstream pf(positionsTsv);
  if (!pf) {
    throw std::runtime_error("failed to open --positions-tsv file: " + positionsTsv);
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
  return applied;
}

std::size_t applyRoutesTsvOverride(
  const std::string& routesTsv,
  const std::vector<EdgeRecord>& edges,
  std::vector<std::vector<RoutePoint>>& routes) {
  if (routesTsv.empty()) {
    return 0;
  }
  std::unordered_map<std::string, std::size_t> edgeIndexById;
  edgeIndexById.reserve(edges.size());
  for (std::size_t e = 0; e < edges.size(); ++e) {
    edgeIndexById[edges[e].edgeId] = e;
  }
  std::ifstream rf(routesTsv);
  if (!rf) {
    throw std::runtime_error("failed to open --routes-tsv file: " + routesTsv);
  }
  std::size_t appliedRoutes = 0;
  std::string line;
  while (std::getline(rf, line)) {
    if (line.empty()) continue;
    std::vector<std::string> cols;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, '\t')) {
      cols.push_back(cell);
    }
    if (cols.size() < 5 || cols.size() % 2 == 0) {
      continue;
    }
    auto edgeIt = edgeIndexById.find(cols[0]);
    if (edgeIt == edgeIndexById.end() || edgeIt->second >= routes.size()) {
      continue;
    }
    std::vector<RoutePoint> route;
    route.reserve((cols.size() - 1) / 2);
    bool ok = true;
    for (std::size_t i = 1; i + 1 < cols.size(); i += 2) {
      char* xEnd = nullptr;
      char* yEnd = nullptr;
      const double x = std::strtod(cols[i].c_str(), &xEnd);
      const double y = std::strtod(cols[i + 1].c_str(), &yEnd);
      if (
          xEnd == cols[i].c_str()
          || yEnd == cols[i + 1].c_str()
          || !std::isfinite(x)
          || !std::isfinite(y)) {
        ok = false;
        break;
      }
      route.push_back({x, y});
    }
    if (!ok || route.size() < 2) {
      continue;
    }
    routes[edgeIt->second] = compressRoutePoints(std::move(route));
    ++appliedRoutes;
  }
  return appliedRoutes;
}

bool segmentIntersectsRect(const RoutePoint& start, const RoutePoint& end, const Rect& rect) {
  if (std::abs(start.x - end.x) < 0.01) {
    const double minY = std::min(start.y, end.y);
    const double maxY = std::max(start.y, end.y);
    return start.x > rect.left
      && start.x < rect.right
      && maxY > rect.top
      && minY < rect.bottom;
  }

  if (std::abs(start.y - end.y) < 0.01) {
    const double minX = std::min(start.x, end.x);
    const double maxX = std::max(start.x, end.x);
    return start.y > rect.top
      && start.y < rect.bottom
      && maxX > rect.left
      && minX < rect.right;
  }

  double minT = 0.0;
  double maxT = 1.0;
  const double dx = end.x - start.x;
  const double dy = end.y - start.y;
  const auto clip = [&](double edge, double distance) {
    if (std::abs(edge) < 0.01) {
      return distance >= 0.0;
    }

    const double t = distance / edge;
    if (edge < 0.0) {
      if (t > maxT) {
        return false;
      }
      minT = std::max(minT, t);
    } else {
      if (t < minT) {
        return false;
      }
      maxT = std::min(maxT, t);
    }
    return true;
  };

  if (!clip(-dx, start.x - rect.left)) {
    return false;
  }
  if (!clip(dx, rect.right - start.x)) {
    return false;
  }
  if (!clip(-dy, start.y - rect.top)) {
    return false;
  }
  if (!clip(dy, rect.bottom - start.y)) {
    return false;
  }

  return maxT - minT > 0.001;
}

long long metricLaneKey(double value) {
  return static_cast<long long>(std::llround(value * kMetricCoordinateScale));
}

bool intervalsOverlap(double leftStart, double leftEnd, double rightStart, double rightEnd) {
  return std::min(leftEnd, rightEnd) - std::max(leftStart, rightStart) > 1.0;
}

double distributedLaneOffset(std::size_t lineIndex) {
  constexpr std::size_t laneCount = 73;
  constexpr double laneStep = 8.0;
  const std::size_t lane = (lineIndex * 37) % laneCount;
  const double center = static_cast<double>(laneCount - 1) / 2.0;
  return (static_cast<double>(lane) - center) * laneStep;
}

LineIntent makeLineIntent(
  const EdgeRecord& edge,
  std::size_t lineIndex,
  ogdf::GraphAttributes& attributes) {
  const Rect sourceRect = handleRect(edge.sourceHandle, attributes);
  const Rect targetRect = handleRect(edge.targetHandle, attributes);
  return {
    lineIndex,
    edge.edgeId,
    distributedLaneOffset(lineIndex),
    std::abs(rectCenterX(targetRect) - rectCenterX(sourceRect))
      >= std::abs(rectCenterY(targetRect) - rectCenterY(sourceRect)),
    edge.sourceHandle,
    edge.sourceModelId,
    sourceRect,
    edge.targetHandle,
    edge.targetModelId,
    targetRect,
  };
}

std::vector<NodeObstacle> makeNodeObstacles(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  double margin,
  ogdf::node sourceHandle,
  ogdf::node targetHandle) {
  std::vector<NodeObstacle> obstacles;
  obstacles.reserve(nodes.size());
  for (const NodeRecord& node : nodes) {
    if (node.handle == sourceHandle || node.handle == targetHandle) {
      continue;
    }

    obstacles.push_back({ node.handle, node.modelId, nodeRect(node, attributes, margin) });
  }
  return obstacles;
}

std::vector<Rect> collectObstacleRects(const std::vector<NodeObstacle>& obstacles) {
  std::vector<Rect> rects;
  rects.reserve(obstacles.size());
  for (const NodeObstacle& obstacle : obstacles) {
    rects.push_back(obstacle.rect);
  }
  return rects;
}

std::pair<double, double> leafBundleRenderSize(std::size_t memberCount) {
  constexpr double kLeafCellW = 200.0;
  constexpr double kLeafCellH = 56.0;
  constexpr double kLeafGapX = 10.0;
  constexpr double kLeafGapY = 8.0;
  constexpr double kBundleHeader = 48.0;
  constexpr double kBundlePad = 16.0;
  const double n = static_cast<double>(std::max<std::size_t>(1, memberCount));
  const double cols = std::max(1.0, std::ceil(std::sqrt(n)));
  const double rows = std::max(1.0, std::ceil(n / cols));
  const double innerW = cols * kLeafCellW + (cols - 1.0) * kLeafGapX;
  const double innerH = rows * kLeafCellH + (rows - 1.0) * kLeafGapY;
  return {innerW + kBundlePad * 2.0, kBundleHeader + innerH + kBundlePad};
}

Rect renderedLeafBundleRect(const LeafBundleRecord& bundle, double margin = 0.0) {
  const auto [width, height] = leafBundleRenderSize(bundle.leafModelIds.size());
  const double cx = bundle.bboxX + bundle.bboxWidth / 2.0;
  const double cy = bundle.bboxY + bundle.bboxHeight / 2.0;
  Rect rect;
  rect.left = cx - width / 2.0 - margin;
  rect.right = cx + width / 2.0 + margin;
  rect.top = cy - height / 2.0 - margin;
  rect.bottom = cy + height / 2.0 + margin;
  return rect;
}

void recomputeLeafBundleBboxesFromNodes(
  std::vector<LeafBundleRecord>& leafBundles,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  if (leafBundles.empty()) {
    return;
  }
  std::unordered_map<std::string, std::size_t> idToIndex;
  idToIndex.reserve(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    idToIndex[nodes[i].modelId] = i;
  }
  for (LeafBundleRecord& bundle : leafBundles) {
    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    double sumX = 0.0;
    double sumY = 0.0;
    std::size_t count = 0;
    for (const std::string& leaf : bundle.leafModelIds) {
      auto it = idToIndex.find(leaf);
      if (it == idToIndex.end()) {
        continue;
      }
      const NodeRecord& node = nodes[it->second];
      const double cx = sanitizeNodeCenterX(node, attributes);
      const double cy = sanitizeNodeCenterY(node, attributes);
      const double w = sanitizeNodeWidth(node, attributes);
      const double h = sanitizeNodeHeight(node, attributes);
      minX = std::min(minX, cx - w / 2.0);
      minY = std::min(minY, cy - h / 2.0);
      maxX = std::max(maxX, cx + w / 2.0);
      maxY = std::max(maxY, cy + h / 2.0);
      sumX += cx;
      sumY += cy;
      ++count;
    }
    if (count == 0 || !std::isfinite(minX)) {
      continue;
    }
    bundle.bboxX = minX;
    bundle.bboxY = minY;
    bundle.bboxWidth = maxX - minX;
    bundle.bboxHeight = maxY - minY;
    const double leafCx = sumX / static_cast<double>(count);
    const double leafCy = sumY / static_cast<double>(count);
    auto parentIt = idToIndex.find(bundle.parentModelId);
    if (parentIt != idToIndex.end()) {
      const NodeRecord& parent = nodes[parentIt->second];
      bundle.anchorX = 0.5 * (sanitizeNodeCenterX(parent, attributes) + leafCx);
      bundle.anchorY = 0.5 * (sanitizeNodeCenterY(parent, attributes) + leafCy);
    }
  }
}

std::size_t clearLeafBundleNodeMargins(
  std::vector<LeafBundleRecord>& leafBundles,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  bool logResult) {
  if (leafBundles.empty() || !readBoolEnv("DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL", false)) {
    return 0;
  }

  const int passes = static_cast<int>(
    readDoubleEnv("DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PASSES", 3.0, 1.0, 12.0));
  const double maxShift =
    readDoubleEnv("DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_MAX_SHIFT", 1200.0, 80.0, 12000.0);
  const double nodeMargin = visualNodeMargin();
  const double bundleMargin = leafBundleVisualMargin();
  const double extraClearance =
    readDoubleEnv("DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EXTRA", 12.0, 0.0, 160.0);

  std::unordered_map<std::string, std::size_t> idToIndex;
  idToIndex.reserve(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    idToIndex[nodes[i].modelId] = i;
  }

  std::unordered_set<std::string> absorbed;
  for (const LeafBundleRecord& bundle : leafBundles) {
    absorbed.insert(bundle.parentModelId);
    for (const std::string& leaf : bundle.leafModelIds) {
      absorbed.insert(leaf);
    }
  }

  auto translateBundleLeaves = [&](const LeafBundleRecord& bundle, double dx, double dy) {
    for (const std::string& leaf : bundle.leafModelIds) {
      auto it = idToIndex.find(leaf);
      if (it == idToIndex.end()) {
        continue;
      }
      const NodeRecord& node = nodes[it->second];
      attributes.x(node.handle) += dx;
      attributes.y(node.handle) += dy;
    }
  };

  std::size_t moved = 0;
  for (int pass = 0; pass < passes; ++pass) {
    recomputeLeafBundleBboxesFromNodes(leafBundles, nodes, attributes);
    std::size_t movedThisPass = 0;
    for (LeafBundleRecord& bundle : leafBundles) {
      Rect bundleRect = renderedLeafBundleRect(bundle, bundleMargin);
      const double bundleCx = rectCenterX(bundleRect);
      const double bundleCy = rectCenterY(bundleRect);
      double shiftX = 0.0;
      double shiftY = 0.0;
      std::size_t conflicts = 0;

      for (const NodeRecord& node : nodes) {
        if (absorbed.count(node.modelId)) {
          continue;
        }
        const Rect nodeBox = nodeRect(node, attributes, nodeMargin);
        if (!rectsOverlap(bundleRect, nodeBox)) {
          continue;
        }
        const double overlapX =
          std::min(bundleRect.right, nodeBox.right)
          - std::max(bundleRect.left, nodeBox.left);
        const double overlapY =
          std::min(bundleRect.bottom, nodeBox.bottom)
          - std::max(bundleRect.top, nodeBox.top);
        if (overlapX <= 0.0 || overlapY <= 0.0) {
          continue;
        }
        ++conflicts;
        const double nodeCx = rectCenterX(nodeBox);
        const double nodeCy = rectCenterY(nodeBox);
        if (overlapX <= overlapY) {
          const double dir = bundleCx < nodeCx ? -1.0 : 1.0;
          shiftX += dir * (overlapX + extraClearance);
        } else {
          const double dir = bundleCy < nodeCy ? -1.0 : 1.0;
          shiftY += dir * (overlapY + extraClearance);
        }
      }

      if (conflicts == 0) {
        continue;
      }
      const double length = std::hypot(shiftX, shiftY);
      if (length < 0.01) {
        shiftX = (static_cast<int>(moved + movedThisPass) % 2 == 0)
          ? extraClearance
          : -extraClearance;
        shiftY = 0.0;
      } else if (length > maxShift) {
        const double scale = maxShift / length;
        shiftX *= scale;
        shiftY *= scale;
      }
      translateBundleLeaves(bundle, shiftX, shiftY);
      ++moved;
      ++movedThisPass;
    }
    if (movedThisPass == 0) {
      break;
    }
  }

  if (moved > 0 && logResult) {
    recomputeLeafBundleBboxesFromNodes(leafBundles, nodes, attributes);
    std::fprintf(stderr,
      "[leaf-bundle-node-clear-final] moved %zu bundle blocks "
      "(nodeMargin=%.1f bundleMargin=%.1f).\n",
      moved,
      nodeMargin,
      bundleMargin);
  }
  return moved;
}

std::size_t clearLeafBundleExternalNodeMargins(
  std::vector<LeafBundleRecord>& leafBundles,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  if (leafBundles.empty()
      || !readBoolEnv("DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODES", true)) {
    return 0;
  }

  const int passes = static_cast<int>(
    readDoubleEnv(
      "DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODE_PASSES",
      2.0,
      1.0,
      8.0));
  const double maxShift =
    readDoubleEnv(
      "DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODE_MAX_SHIFT",
      600.0,
      40.0,
      4000.0);
  const double nodeMargin = visualNodeMargin();
  const double bundleMargin = leafBundleVisualMargin();
  const double extraClearance =
    readDoubleEnv("DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EXTRA", 12.0, 0.0, 160.0);

  std::unordered_set<std::string> absorbed;
  for (const LeafBundleRecord& bundle : leafBundles) {
    absorbed.insert(bundle.parentModelId);
    for (const std::string& leaf : bundle.leafModelIds) {
      absorbed.insert(leaf);
    }
    for (const std::string& root : bundle.sharedRootModelIds) {
      absorbed.insert(root);
    }
  }

  std::size_t moved = 0;
  for (int pass = 0; pass < passes; ++pass) {
    recomputeLeafBundleBboxesFromNodes(leafBundles, nodes, attributes);
    std::size_t movedThisPass = 0;
    for (const LeafBundleRecord& bundle : leafBundles) {
      const Rect bundleRect = renderedLeafBundleRect(bundle, bundleMargin);
      const double bundleCx = rectCenterX(bundleRect);
      const double bundleCy = rectCenterY(bundleRect);
      for (const NodeRecord& node : nodes) {
        if (absorbed.count(node.modelId)) {
          continue;
        }
        const Rect nodeBox = nodeRect(node, attributes, nodeMargin);
        if (!rectsOverlap(bundleRect, nodeBox)) {
          continue;
        }
        const double overlapX =
          std::min(bundleRect.right, nodeBox.right)
          - std::max(bundleRect.left, nodeBox.left);
        const double overlapY =
          std::min(bundleRect.bottom, nodeBox.bottom)
          - std::max(bundleRect.top, nodeBox.top);
        if (overlapX <= 0.0 || overlapY <= 0.0) {
          continue;
        }

        double shiftX = 0.0;
        double shiftY = 0.0;
        const double nodeCx = rectCenterX(nodeBox);
        const double nodeCy = rectCenterY(nodeBox);
        if (overlapX <= overlapY) {
          const double dir = nodeCx < bundleCx ? -1.0 : 1.0;
          shiftX = dir * (overlapX + extraClearance);
        } else {
          const double dir = nodeCy < bundleCy ? -1.0 : 1.0;
          shiftY = dir * (overlapY + extraClearance);
        }
        const double length = std::hypot(shiftX, shiftY);
        if (length > maxShift && length > 1e-6) {
          const double scale = maxShift / length;
          shiftX *= scale;
          shiftY *= scale;
        }
        attributes.x(node.handle) += shiftX;
        attributes.y(node.handle) += shiftY;
        ++moved;
        ++movedThisPass;
      }
    }
    if (movedThisPass == 0) {
      break;
    }
  }
  return moved;
}

std::size_t clearNodeVisualOverlaps(
  const std::vector<LeafBundleRecord>& leafBundles,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  if (nodes.size() <= 1 || !readBoolEnv("DJERD_NODE_OVERLAP_CLEAR_FINAL", false)) {
    return 0;
  }

  const int passes = static_cast<int>(
    readDoubleEnv("DJERD_NODE_OVERLAP_CLEAR_FINAL_PASSES", 6.0, 1.0, 24.0));
  const double margin = visualNodeMargin();
  const double extra =
    readDoubleEnv("DJERD_NODE_OVERLAP_CLEAR_FINAL_EXTRA", 10.0, 0.0, 160.0);
  const double maxShift =
    readDoubleEnv("DJERD_NODE_OVERLAP_CLEAR_FINAL_MAX_SHIFT", 240.0, 8.0, 4000.0);

  std::unordered_set<std::string> absorbed;
  for (const LeafBundleRecord& bundle : leafBundles) {
    absorbed.insert(bundle.parentModelId);
    for (const std::string& leaf : bundle.leafModelIds) {
      absorbed.insert(leaf);
    }
  }

  std::size_t movedTotal = 0;
  for (int pass = 0; pass < passes; ++pass) {
    std::vector<std::pair<Rect, std::size_t>> rects;
    rects.reserve(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      if (absorbed.count(nodes[i].modelId)) continue;
      rects.emplace_back(nodeRect(nodes[i], attributes, margin), i);
    }
    std::sort(rects.begin(), rects.end(),
      [](const auto& left, const auto& right) {
        return left.first.left < right.first.left;
      });

    std::vector<double> shiftX(nodes.size(), 0.0);
    std::vector<double> shiftY(nodes.size(), 0.0);
    std::size_t conflicts = 0;
    for (std::size_t leftOrder = 0; leftOrder < rects.size(); ++leftOrder) {
      const Rect& left = rects[leftOrder].first;
      const std::size_t leftIndex = rects[leftOrder].second;
      for (std::size_t rightOrder = leftOrder + 1; rightOrder < rects.size(); ++rightOrder) {
        const Rect& right = rects[rightOrder].first;
        if (right.left >= left.right) break;
        if (!rectsOverlap(left, right)) continue;

        const double overlapX =
          std::min(left.right, right.right) - std::max(left.left, right.left);
        const double overlapY =
          std::min(left.bottom, right.bottom) - std::max(left.top, right.top);
        if (overlapX <= 0.0 || overlapY <= 0.0) continue;

        const std::size_t rightIndex = rects[rightOrder].second;
        ++conflicts;
        if (overlapX <= overlapY) {
          const double dx = rectCenterX(right) - rectCenterX(left);
          const double dir = std::abs(dx) < 0.01
            ? (leftIndex % 2 == 0 ? 1.0 : -1.0)
            : (dx >= 0.0 ? 1.0 : -1.0);
          const double shift = overlapX * 0.5 + extra * 0.5;
          shiftX[leftIndex] -= dir * shift;
          shiftX[rightIndex] += dir * shift;
        } else {
          const double dy = rectCenterY(right) - rectCenterY(left);
          const double dir = std::abs(dy) < 0.01
            ? (leftIndex % 2 == 0 ? 1.0 : -1.0)
            : (dy >= 0.0 ? 1.0 : -1.0);
          const double shift = overlapY * 0.5 + extra * 0.5;
          shiftY[leftIndex] -= dir * shift;
          shiftY[rightIndex] += dir * shift;
        }
      }
    }
    if (conflicts == 0) break;

    std::size_t movedThisPass = 0;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      double dx = shiftX[i];
      double dy = shiftY[i];
      const double length = std::hypot(dx, dy);
      if (length <= 0.01) continue;
      if (length > maxShift) {
        const double scale = maxShift / length;
        dx *= scale;
        dy *= scale;
      }
      attributes.x(nodes[i].handle) = sanitizeNodeCenterX(nodes[i], attributes) + dx;
      attributes.y(nodes[i].handle) = sanitizeNodeCenterY(nodes[i], attributes) + dy;
      ++movedThisPass;
    }
    movedTotal += movedThisPass;
    if (movedThisPass == 0) break;
  }

  return movedTotal;
}

std::size_t clearNodeSpacingOverlaps(
  const std::vector<LeafBundleRecord>& leafBundles,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  if (nodes.size() <= 1 || !readBoolEnv("DJERD_NODE_SPACING_CLEAR_FINAL", false)) {
    return 0;
  }

  const int passes = static_cast<int>(
    readDoubleEnv("DJERD_NODE_SPACING_CLEAR_FINAL_PASSES", 6.0, 1.0, 24.0));
  const double extra =
    readDoubleEnv("DJERD_NODE_SPACING_CLEAR_FINAL_EXTRA", 8.0, 0.0, 200.0);
  const double maxShift =
    readDoubleEnv("DJERD_NODE_SPACING_CLEAR_FINAL_MAX_SHIFT", 220.0, 8.0, 4000.0);
  const std::unordered_set<std::string> absorbed =
    absorbedLeafBundleIds(leafBundles);

  std::size_t movedTotal = 0;
  for (int pass = 0; pass < passes; ++pass) {
    std::vector<std::pair<Rect, std::size_t>> rects;
    rects.reserve(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      if (absorbed.count(nodes[i].modelId)) continue;
      rects.emplace_back(
        expandedNodeRectAt(
          nodes[i],
          attributes,
          sanitizeNodeCenterX(nodes[i], attributes),
          sanitizeNodeCenterY(nodes[i], attributes)),
        i);
    }
    std::sort(rects.begin(), rects.end(),
      [](const auto& left, const auto& right) {
        return left.first.left < right.first.left;
      });

    std::vector<double> shiftX(nodes.size(), 0.0);
    std::vector<double> shiftY(nodes.size(), 0.0);
    std::size_t conflicts = 0;
    for (std::size_t leftOrder = 0; leftOrder < rects.size(); ++leftOrder) {
      const Rect& left = rects[leftOrder].first;
      const std::size_t leftIndex = rects[leftOrder].second;
      for (std::size_t rightOrder = leftOrder + 1; rightOrder < rects.size(); ++rightOrder) {
        const Rect& right = rects[rightOrder].first;
        if (right.left >= left.right) break;
        if (!rectsOverlap(left, right)) continue;
        const double overlapX =
          std::min(left.right, right.right) - std::max(left.left, right.left);
        const double overlapY =
          std::min(left.bottom, right.bottom) - std::max(left.top, right.top);
        if (overlapX <= 0.0 || overlapY <= 0.0) continue;

        const std::size_t rightIndex = rects[rightOrder].second;
        ++conflicts;
        if (overlapX <= overlapY) {
          const double dx = rectCenterX(right) - rectCenterX(left);
          const double dir = std::abs(dx) < 0.01
            ? (leftIndex % 2 == 0 ? 1.0 : -1.0)
            : (dx >= 0.0 ? 1.0 : -1.0);
          const double shift = overlapX * 0.5 + extra * 0.5;
          shiftX[leftIndex] -= dir * shift;
          shiftX[rightIndex] += dir * shift;
        } else {
          const double dy = rectCenterY(right) - rectCenterY(left);
          const double dir = std::abs(dy) < 0.01
            ? (leftIndex % 2 == 0 ? 1.0 : -1.0)
            : (dy >= 0.0 ? 1.0 : -1.0);
          const double shift = overlapY * 0.5 + extra * 0.5;
          shiftY[leftIndex] -= dir * shift;
          shiftY[rightIndex] += dir * shift;
        }
      }
    }
    if (conflicts == 0) break;

    std::size_t movedThisPass = 0;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      double dx = shiftX[i];
      double dy = shiftY[i];
      const double length = std::hypot(dx, dy);
      if (length <= 0.01) continue;
      if (length > maxShift) {
        const double scale = maxShift / length;
        dx *= scale;
        dy *= scale;
      }
      attributes.x(nodes[i].handle) =
        sanitizeNodeCenterX(nodes[i], attributes) + dx;
      attributes.y(nodes[i].handle) =
        sanitizeNodeCenterY(nodes[i], attributes) + dy;
      ++movedThisPass;
    }
    movedTotal += movedThisPass;
    if (movedThisPass == 0) break;
  }

  return movedTotal;
}

bool makeLineSegment(
  const std::string& lineId,
  std::size_t lineIndex,
  const RoutePoint& start,
  const RoutePoint& end,
  LineSegment& segment) {
  const bool horizontal = std::abs(start.y - end.y) < 0.01;
  const bool vertical = std::abs(start.x - end.x) < 0.01;
  const double axisStart = horizontal ? start.x : (vertical ? start.y : 0.0);
  const double axisEnd = horizontal
    ? end.x
    : (vertical ? end.y : std::hypot(end.x - start.x, end.y - start.y));
  const double minAxis = std::min(axisStart, axisEnd);
  const double maxAxis = std::max(axisStart, axisEnd);
  if (maxAxis - minAxis <= 1.0) {
    return false;
  }

  segment = {
    maxAxis,
    minAxis,
    horizontal,
    horizontal || vertical ? metricLaneKey(horizontal ? start.y : start.x) : 0,
    lineIndex,
    lineId,
    end,
    start,
    vertical,
  };
  return true;
}

std::vector<LineSegment> buildLineSegments(
  const std::vector<RoutePoint>& points,
  std::size_t lineIndex,
  const std::string& lineId) {
  std::vector<LineSegment> segments;
  if (points.size() < 2) {
    return segments;
  }

  segments.reserve(points.size() - 1);
  for (std::size_t index = 1; index < points.size(); ++index) {
    LineSegment segment;
    if (makeLineSegment(lineId, lineIndex, points[index - 1], points[index], segment)) {
      segments.push_back(segment);
    }
  }

  return segments;
}

double occupancyCostForAxisSegment(
  const RouteOccupancy* occupancy,
  bool horizontal,
  long long laneKey,
  double start,
  double end) {
  if (occupancy == nullptr) {
    return 0.0;
  }

  const double minAxis = std::min(start, end);
  const double maxAxis = std::max(start, end);
  if (maxAxis - minAxis <= 1.0) {
    return 0.0;
  }

  const auto& groups = horizontal
    ? occupancy->horizontalSegmentsByLane
    : occupancy->verticalSegmentsByLane;
  const auto found = groups.find(laneKey);
  if (found == groups.end()) {
    return 0.0;
  }

  double penalty = 0.0;
  for (const LineSegment& used : found->second) {
    if (!intervalsOverlap(minAxis, maxAxis, used.axisStart, used.axisEnd)) {
      continue;
    }
    const double overlap = std::min(maxAxis, used.axisEnd) - std::max(minAxis, used.axisStart);
    penalty += 1'200'000.0 + overlap * 900.0;
  }
  return penalty;
}

double routeLength(const std::vector<RoutePoint>& points) {
  double length = 0.0;
  for (std::size_t index = 1; index < points.size(); ++index) {
    length += std::abs(points[index].x - points[index - 1].x)
      + std::abs(points[index].y - points[index - 1].y);
  }
  return length;
}

double routeOccupancyPenalty(
  const std::vector<RoutePoint>& points,
  const RouteOccupancy* occupancy) {
  if (occupancy == nullptr) {
    return 0.0;
  }

  double penalty = 0.0;
  for (const LineSegment& segment : buildLineSegments(
      points,
      std::numeric_limits<std::size_t>::max(),
      "")) {
    if (!segment.horizontal && !segment.vertical) {
      continue;
    }
    penalty += occupancyCostForAxisSegment(
      occupancy,
      segment.horizontal,
      segment.laneKey,
      segment.axisStart,
      segment.axisEnd);
  }

  return penalty;
}

double routeAxisOverlapDebt(
  const std::vector<RoutePoint>& points,
  const RouteOccupancy* occupancy,
  double lengthWeight) {
  if (occupancy == nullptr) {
    return 0.0;
  }

  // Score the same condition used by edgeSegmentOverlaps: an axis-aligned
  // segment is debt only when its interval overlaps another segment on the
  // same lane. A lane-presence proxy over-penalizes safe same-lane segments and
  // can miss the small overlap debts that final repair is trying to clear.
  double debt = 0.0;
  for (const LineSegment& segment : buildLineSegments(
      points,
      std::numeric_limits<std::size_t>::max(),
      "")) {
    if (!segment.horizontal && !segment.vertical) {
      continue;
    }
    const auto& groups = segment.horizontal
      ? occupancy->horizontalSegmentsByLane
      : occupancy->verticalSegmentsByLane;
    const auto found = groups.find(segment.laneKey);
    if (found == groups.end()) {
      continue;
    }
    for (const LineSegment& used : found->second) {
      if (!intervalsOverlap(
          segment.axisStart,
          segment.axisEnd,
          used.axisStart,
          used.axisEnd)) {
        continue;
      }
      const double overlap =
        std::min(segment.axisEnd, used.axisEnd)
        - std::max(segment.axisStart, used.axisStart);
      if (overlap <= 1.0) {
        continue;
      }
      debt += 1.0 + lengthWeight * overlap;
    }
  }

  return debt;
}

double routeScore(
  const std::vector<RoutePoint>& points,
  const std::vector<NodeObstacle>& obstacles,
  const RouteOccupancy* occupancy = nullptr) {
  double intersections = 0.0;
  for (const LineSegment& segment : buildLineSegments(
      points,
      std::numeric_limits<std::size_t>::max(),
      "")) {
    for (const NodeObstacle& obstacle : obstacles) {
      if (segmentIntersectsRect(segment.start, segment.end, obstacle.rect)) {
        intersections += 1.0;
      }
    }
  }

  return intersections * 1'000'000.0
    + routeLength(points)
    + static_cast<double>(points.size()) * 20.0
    + routeOccupancyPenalty(points, occupancy);
}

void recordRouteOccupancy(
  const std::vector<RoutePoint>& points,
  const LineIntent& line,
  RouteOccupancy& occupancy) {
  for (const LineSegment& segment : buildLineSegments(points, line.lineIndex, line.lineId)) {
    if (segment.horizontal) {
      occupancy.horizontalSegmentsByLane[segment.laneKey].push_back(segment);
    } else if (segment.vertical) {
      occupancy.verticalSegmentsByLane[segment.laneKey].push_back(segment);
    }
  }
}

void removeRouteOccupancy(
  const std::vector<RoutePoint>& points,
  const LineIntent& line,
  RouteOccupancy& occupancy) {
  for (const LineSegment& segment : buildLineSegments(points, line.lineIndex, line.lineId)) {
    if (!segment.horizontal && !segment.vertical) {
      continue;
    }
    auto& groups = segment.horizontal
      ? occupancy.horizontalSegmentsByLane
      : occupancy.verticalSegmentsByLane;
    auto found = groups.find(segment.laneKey);
    if (found == groups.end()) {
      continue;
    }
    auto& laneSegments = found->second;
    laneSegments.erase(
      std::remove_if(
        laneSegments.begin(),
        laneSegments.end(),
        [&](const LineSegment& used) {
          return used.lineIndex == line.lineIndex;
        }),
      laneSegments.end());
    if (laneSegments.empty()) {
      groups.erase(found);
    }
  }
}

std::size_t countAxisSegmentOverlaps(
  std::vector<LineSegment>& segments,
  std::vector<bool>& overlappingEdgeFlags) {
  std::sort(
    segments.begin(),
    segments.end(),
    [](const LineSegment& left, const LineSegment& right) {
      if (std::abs(left.axisStart - right.axisStart) > 0.01) {
        return left.axisStart < right.axisStart;
      }
      return left.axisEnd < right.axisEnd;
    });

  std::size_t overlaps = 0;
  for (std::size_t leftIndex = 0; leftIndex < segments.size(); ++leftIndex) {
    const LineSegment& left = segments[leftIndex];
    for (std::size_t rightIndex = leftIndex + 1; rightIndex < segments.size(); ++rightIndex) {
      const LineSegment& right = segments[rightIndex];
      if (right.axisStart >= left.axisEnd - 1.0) {
        break;
      }
      if (left.lineIndex == right.lineIndex) {
        continue;
      }
      if (!intervalsOverlap(left.axisStart, left.axisEnd, right.axisStart, right.axisEnd)) {
        continue;
      }

      overlaps += 1;
      if (left.lineIndex < overlappingEdgeFlags.size()) {
        overlappingEdgeFlags[left.lineIndex] = true;
      }
      if (right.lineIndex < overlappingEdgeFlags.size()) {
        overlappingEdgeFlags[right.lineIndex] = true;
      }
    }
  }

  return overlaps;
}

std::size_t countNodeRectOverlaps(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  bool includeSpacing) {
  std::vector<Rect> rects;
  rects.reserve(nodes.size());
  for (const NodeRecord& node : nodes) {
    rects.push_back(
      includeSpacing
        ? expandedNodeRectAt(
            node,
            attributes,
            sanitizeNodeCenterX(node, attributes),
            sanitizeNodeCenterY(node, attributes))
        : nodeRect(node, attributes));
  }

  std::sort(
    rects.begin(),
    rects.end(),
    [](const Rect& left, const Rect& right) {
      return left.left < right.left;
    });

  std::size_t overlaps = 0;
  for (std::size_t leftIndex = 0; leftIndex < rects.size(); ++leftIndex) {
    const Rect& left = rects[leftIndex];
    for (std::size_t rightIndex = leftIndex + 1; rightIndex < rects.size(); ++rightIndex) {
      const Rect& right = rects[rightIndex];
      if (right.left >= left.right) {
        break;
      }
      if (rectsOverlap(left, right)) {
        overlaps += 1;
      }
    }
  }

  return overlaps;
}

struct RenderedDensityMetrics {
  std::size_t totalCells = 0;
  std::size_t emptyCells = 0;
  std::size_t occupiedCells = 0;
  std::size_t denseCells = 0;
  std::size_t objectCount = 0;
  double emptyRatio = 0.0;
  double p50 = 0.0;
  double p90 = 0.0;
  double maxCell = 0.0;
  double imbalance = 0.0;
  double score = 0.0;
};

std::unordered_set<std::string> absorbedLeafBundleIds(
  const std::vector<LeafBundleRecord>& leafBundles) {
  std::unordered_set<std::string> absorbed;
  for (const LeafBundleRecord& bundle : leafBundles) {
    absorbed.insert(bundle.parentModelId);
    for (const std::string& leaf : bundle.leafModelIds) {
      absorbed.insert(leaf);
    }
  }
  return absorbed;
}

RenderedDensityMetrics measureRenderedDensity(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  const std::vector<LeafBundleRecord>& leafBundles,
  double requestedCellSize) {
  RenderedDensityMetrics metrics;
  const double nodeMargin = visualNodeMargin();
  const double bundleMargin = leafBundleVisualMargin();
  const double cellSize = std::max(200.0, requestedCellSize);
  const std::unordered_set<std::string> absorbed =
    absorbedLeafBundleIds(leafBundles);

  std::vector<Rect> objects;
  objects.reserve(nodes.size() + leafBundles.size());
  for (const NodeRecord& node : nodes) {
    if (absorbed.count(node.modelId)) continue;
    objects.push_back(nodeRect(node, attributes, nodeMargin));
  }
  for (const LeafBundleRecord& bundle : leafBundles) {
    objects.push_back(renderedLeafBundleRect(bundle, bundleMargin));
  }
  metrics.objectCount = objects.size();
  if (objects.empty()) {
    return metrics;
  }

  Rect bounds = objects.front();
  for (const Rect& rect : objects) {
    bounds.left = std::min(bounds.left, rect.left);
    bounds.right = std::max(bounds.right, rect.right);
    bounds.top = std::min(bounds.top, rect.top);
    bounds.bottom = std::max(bounds.bottom, rect.bottom);
  }
  const double width = rectWidth(bounds);
  const double height = rectHeight(bounds);
  if (width <= 1.0 || height <= 1.0) {
    return metrics;
  }

  const int gridW = std::clamp(
    static_cast<int>(std::ceil(width / cellSize)),
    1,
    400);
  const int gridH = std::clamp(
    static_cast<int>(std::ceil(height / cellSize)),
    1,
    400);
  const double actualCellW = width / static_cast<double>(gridW);
  const double actualCellH = height / static_cast<double>(gridH);
  std::vector<int> counts(static_cast<std::size_t>(gridW) * gridH, 0);

  for (const Rect& rect : objects) {
    int gx = static_cast<int>((rectCenterX(rect) - bounds.left) / actualCellW);
    int gy = static_cast<int>((rectCenterY(rect) - bounds.top) / actualCellH);
    gx = std::clamp(gx, 0, gridW - 1);
    gy = std::clamp(gy, 0, gridH - 1);
    counts[static_cast<std::size_t>(gy) * gridW + gx] += 1;
  }

  std::vector<int> occupied;
  occupied.reserve(counts.size());
  for (int count : counts) {
    if (count == 0) {
      ++metrics.emptyCells;
    } else {
      occupied.push_back(count);
    }
  }
  metrics.totalCells = counts.size();
  metrics.occupiedCells = occupied.size();
  metrics.emptyRatio = metrics.totalCells > 0
    ? static_cast<double>(metrics.emptyCells) / static_cast<double>(metrics.totalCells)
    : 0.0;
  if (occupied.empty()) {
    return metrics;
  }

  std::sort(occupied.begin(), occupied.end());
  auto percentile = [&](double p) {
    const std::size_t idx = std::min<std::size_t>(
      occupied.size() - 1,
      static_cast<std::size_t>(std::floor(p * static_cast<double>(occupied.size() - 1))));
    return static_cast<double>(occupied[idx]);
  };
  metrics.p50 = percentile(0.50);
  metrics.p90 = percentile(0.90);
  metrics.maxCell = static_cast<double>(occupied.back());
  metrics.imbalance = metrics.p50 > 0.0
    ? metrics.p90 / metrics.p50
    : metrics.p90;
  const int denseThreshold = std::max(3, static_cast<int>(std::ceil(metrics.p90)));
  for (int count : occupied) {
    if (count >= denseThreshold) ++metrics.denseCells;
  }
  metrics.score =
    metrics.imbalance * 100.0
    + metrics.p90 * 12.0
    + metrics.maxCell * 6.0
    + metrics.emptyRatio * 10.0;
  return metrics;
}

bool compactRigidLayoutFootprint(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  if (nodes.size() <= 2 || !readBoolEnv("DJERD_RIGID_COMPACT_BBOX_FINAL", false)) {
    return false;
  }

  const Rect bounds = graphNodeBounds(nodes, attributes);
  const double width = rectWidth(bounds);
  const double height = rectHeight(bounds);
  if (width <= 1.0 || height <= 1.0) {
    return false;
  }

  const double area = width * height;
  const double targetArea =
    readDoubleEnv("DJERD_RIGID_COMPACT_BBOX_TARGET_B", 6.0, 0.25, 80.0) * 1e9;
  if (area <= targetArea) {
    return false;
  }

  const double minScale =
    readDoubleEnv("DJERD_RIGID_COMPACT_BBOX_MIN_SCALE", 0.45, 0.10, 1.0);
  const double desiredScale =
    std::clamp(std::sqrt(targetArea / area), minScale, 1.0);
  if (desiredScale >= 0.995) {
    return false;
  }

  std::vector<std::pair<double, double>> original;
  original.reserve(nodes.size());
  for (const NodeRecord& node : nodes) {
    original.push_back({
      sanitizeNodeCenterX(node, attributes),
      sanitizeNodeCenterY(node, attributes),
    });
  }
  const double centerX = rectCenterX(bounds);
  const double centerY = rectCenterY(bounds);
  const std::size_t baseBareOverlaps = countNodeRectOverlaps(nodes, attributes, false);
  const std::size_t baseSpacingOverlaps = countNodeRectOverlaps(nodes, attributes, true);
  auto countVisualMarginOverlaps = [&]() {
    const double margin = visualNodeMargin();
    std::vector<Rect> rects;
    rects.reserve(nodes.size());
    for (const NodeRecord& node : nodes) {
      rects.push_back(nodeRect(node, attributes, margin));
    }
    std::sort(rects.begin(), rects.end(),
      [](const Rect& left, const Rect& right) {
        return left.left < right.left;
      });
    std::size_t overlaps = 0;
    for (std::size_t i = 0; i < rects.size(); ++i) {
      for (std::size_t j = i + 1; j < rects.size(); ++j) {
        if (rects[j].left >= rects[i].right) {
          break;
        }
        if (rectsOverlap(rects[i], rects[j])) {
          ++overlaps;
        }
      }
    }
    return overlaps;
  };
  const std::size_t baseVisualOverlaps = countVisualMarginOverlaps();
  const std::size_t spacingSlack = static_cast<std::size_t>(
    readDoubleEnv("DJERD_RIGID_COMPACT_BBOX_SPACING_SLACK", 0.0, 0.0, 100000.0));
  const std::size_t visualSlack = static_cast<std::size_t>(
    readDoubleEnv("DJERD_RIGID_COMPACT_BBOX_VISUAL_SLACK", 0.0, 0.0, 100000.0));

  auto restore = [&]() {
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      attributes.x(nodes[i].handle) = original[i].first;
      attributes.y(nodes[i].handle) = original[i].second;
    }
  };

  auto applyScale = [&](double scale) {
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      const double x = original[i].first;
      const double y = original[i].second;
      attributes.x(nodes[i].handle) = centerX + (x - centerX) * scale;
      attributes.y(nodes[i].handle) = centerY + (y - centerY) * scale;
    }
  };

  auto valid = [&]() {
    return countNodeRectOverlaps(nodes, attributes, false) <= baseBareOverlaps
      && countVisualMarginOverlaps() <= baseVisualOverlaps + visualSlack
      && countNodeRectOverlaps(nodes, attributes, true)
        <= baseSpacingOverlaps + spacingSlack;
  };

  double bestScale = 1.0;
  if (readBoolEnv("DJERD_RIGID_COMPACT_BBOX_UNIFORM", true)) {
    applyScale(desiredScale);
    if (valid()) {
      bestScale = desiredScale;
    } else {
      double lo = desiredScale;
      double hi = 1.0;
      for (int iter = 0; iter < 18; ++iter) {
        const double mid = (lo + hi) / 2.0;
        applyScale(mid);
        if (valid()) {
          bestScale = mid;
          hi = mid;
        } else {
          lo = mid;
        }
      }
    }
  }

  if (bestScale >= 0.995) {
    restore();
    if (!readBoolEnv("DJERD_RIGID_COMPACT_BBOX_WHITESPACE", false)) {
      return false;
    }
    const double gap =
      readDoubleEnv("DJERD_RIGID_COMPACT_BBOX_WHITESPACE_GAP", 28.0, 0.0, 240.0);
    compactGlobalLayout(nodes, attributes, gap);
    if (!valid()) {
      restore();
      return false;
    }
    const Rect packedBounds = graphNodeBounds(nodes, attributes);
    const double packedArea = rectWidth(packedBounds) * rectHeight(packedBounds);
    if (packedArea >= area * 0.98) {
      restore();
      return false;
    }
    std::fprintf(stderr,
      "[rigid-bbox-compact] whitespace gap=%.1f bbox %.2fB -> %.2fB "
      "(nodeOverlaps=%zu visual=%zu spacing=%zu).\n",
      gap,
      area / 1e9,
      packedArea / 1e9,
      countNodeRectOverlaps(nodes, attributes, false),
      countVisualMarginOverlaps(),
      countNodeRectOverlaps(nodes, attributes, true));
    return true;
  }

  applyScale(bestScale);
  const Rect nextBounds = graphNodeBounds(nodes, attributes);
  std::fprintf(stderr,
    "[rigid-bbox-compact] scale=%.3f bbox %.2fB -> %.2fB "
    "(nodeOverlaps=%zu spacing=%zu).\n",
    bestScale,
    area / 1e9,
    (rectWidth(nextBounds) * rectHeight(nextBounds)) / 1e9,
    countNodeRectOverlaps(nodes, attributes, false),
    countNodeRectOverlaps(nodes, attributes, true));
  return true;
}

std::vector<std::string> modelNameTokens(const std::string& modelId) {
  std::string leaf = modelId;
  const std::size_t dot = leaf.rfind('.');
  if (dot != std::string::npos && dot + 1 < leaf.size()) {
    leaf = leaf.substr(dot + 1);
  }

  std::vector<std::string> tokens;
  std::string current;
  auto flush = [&]() {
    if (current.size() >= 2) {
      tokens.push_back(current);
    }
    current.clear();
  };

  for (std::size_t i = 0; i < leaf.size(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(leaf[i]);
    if (!std::isalnum(ch)) {
      flush();
      continue;
    }
    const bool upper = std::isupper(ch);
    if (upper && !current.empty()) {
      flush();
    }
    current.push_back(static_cast<char>(std::tolower(ch)));
  }
  flush();
  std::sort(tokens.begin(), tokens.end());
  tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
  return tokens;
}

std::size_t compactIsolatedBBoxOutliers(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  if (!readBoolEnv("DJERD_ISOLATED_BBOX_COMPACT_FINAL", false) || nodes.empty()) {
    return 0;
  }

  std::unordered_set<std::string> connectedIds;
  connectedIds.reserve(edges.size() * 2);
  for (const EdgeRecord& edge : edges) {
    connectedIds.insert(edge.sourceModelId);
    connectedIds.insert(edge.targetModelId);
  }

  std::vector<std::size_t> connected;
  std::vector<std::size_t> isolated;
  connected.reserve(nodes.size());
  isolated.reserve(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    if (connectedIds.count(nodes[i].modelId)) {
      connected.push_back(i);
    } else {
      isolated.push_back(i);
    }
  }
  if (connected.empty() || isolated.empty()) {
    return 0;
  }

  Rect connectedBounds;
  bool initialized = false;
  for (std::size_t idx : connected) {
    const Rect rect = nodeRect(nodes[idx], attributes);
    if (!initialized) {
      connectedBounds = rect;
      initialized = true;
    } else {
      connectedBounds.left = std::min(connectedBounds.left, rect.left);
      connectedBounds.right = std::max(connectedBounds.right, rect.right);
      connectedBounds.top = std::min(connectedBounds.top, rect.top);
      connectedBounds.bottom = std::max(connectedBounds.bottom, rect.bottom);
    }
  }
  if (!initialized
      || rectWidth(connectedBounds) <= 1.0
      || rectHeight(connectedBounds) <= 1.0) {
    return 0;
  }

  const Rect beforeBounds = graphNodeBounds(nodes, attributes);
  const double beforeArea = rectWidth(beforeBounds) * rectHeight(beforeBounds);
  if (beforeArea <= 1.0) {
    return 0;
  }

  const double outlierMargin =
    readDoubleEnv("DJERD_ISOLATED_BBOX_COMPACT_MARGIN", 8.0, 0.0, 240.0);
  const bool compactAll =
    readBoolEnv("DJERD_ISOLATED_BBOX_COMPACT_ALL", false);

  struct Candidate {
    std::size_t index = 0;
    std::string app;
    std::vector<std::string> tokens;
    std::string modelId;
  };
  std::vector<Candidate> candidates;
  candidates.reserve(isolated.size());
  for (std::size_t idx : isolated) {
    const Rect rect = nodeRect(nodes[idx], attributes);
    const bool expandsConnectedBounds =
      rect.left < connectedBounds.left - outlierMargin
      || rect.right > connectedBounds.right + outlierMargin
      || rect.top < connectedBounds.top - outlierMargin
      || rect.bottom > connectedBounds.bottom + outlierMargin;
    if (!compactAll && !expandsConnectedBounds) {
      continue;
    }
    const NodeRecord& node = nodes[idx];
    const std::size_t dot = node.modelId.find('.');
    candidates.push_back({
      idx,
      dot == std::string::npos ? node.appLabel : node.modelId.substr(0, dot),
      modelNameTokens(node.modelId),
      node.modelId,
    });
  }
  if (candidates.empty()) {
    return 0;
  }

  std::sort(candidates.begin(), candidates.end(),
    [](const Candidate& left, const Candidate& right) {
      if (left.app != right.app) return left.app < right.app;
      if (left.tokens != right.tokens) return left.tokens < right.tokens;
      return left.modelId < right.modelId;
    });

  std::vector<std::pair<double, double>> original;
  original.reserve(candidates.size());
  for (const Candidate& candidate : candidates) {
    const NodeRecord& node = nodes[candidate.index];
    original.push_back({attributes.x(node.handle), attributes.y(node.handle)});
  }

  const double gapX =
    readDoubleEnv("DJERD_ISOLATED_BBOX_COMPACT_GAP_X", 220.0, 0.0, 2000.0);
  const double gapY =
    readDoubleEnv("DJERD_ISOLATED_BBOX_COMPACT_GAP_Y", 46.0, 0.0, 1000.0);
  const double offsetY =
    readDoubleEnv("DJERD_ISOLATED_BBOX_COMPACT_OFFSET_Y", 180.0, 0.0, 4000.0);
  const double minGain =
    readDoubleEnv("DJERD_ISOLATED_BBOX_COMPACT_MIN_GAIN", 0.01, 0.0, 0.9);

  const double startX = connectedBounds.left;
  const double maxRight = connectedBounds.right;
  double x = startX;
  double rowTop = connectedBounds.bottom + offsetY;
  double rowHeight = 0.0;
  for (const Candidate& candidate : candidates) {
    const NodeRecord& node = nodes[candidate.index];
    const double width = sanitizeNodeWidth(node, attributes);
    const double height = sanitizeNodeHeight(node, attributes);
    if (x > startX && x + width > maxRight) {
      x = startX;
      rowTop += rowHeight + gapY;
      rowHeight = 0.0;
    }
    attributes.x(node.handle) = x + width / 2.0;
    attributes.y(node.handle) = rowTop + height / 2.0;
    x += width + gapX;
    rowHeight = std::max(rowHeight, height);
  }

  const Rect afterBounds = graphNodeBounds(nodes, attributes);
  const double afterArea = rectWidth(afterBounds) * rectHeight(afterBounds);
  if (!(afterArea < beforeArea * (1.0 - minGain))) {
    for (std::size_t i = 0; i < candidates.size(); ++i) {
      const NodeRecord& node = nodes[candidates[i].index];
      attributes.x(node.handle) = original[i].first;
      attributes.y(node.handle) = original[i].second;
    }
    return 0;
  }

  std::fprintf(stderr,
    "[isolated-bbox-compact-final] moved %zu/%zu edge-less outliers "
    "bbox %.2fB -> %.2fB (connected %.2fB).\n",
    candidates.size(),
    isolated.size(),
    beforeArea / 1e9,
    afterArea / 1e9,
    (rectWidth(connectedBounds) * rectHeight(connectedBounds)) / 1e9);
  return candidates.size();
}

std::size_t compactSidecarBBoxComponents(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  if (!readBoolEnv("DJERD_SIDECAR_BBOX_COMPACT_FINAL", false)
      || nodes.size() <= 2
      || edges.empty()) {
    return 0;
  }

  std::unordered_map<std::string, std::size_t> idToIndex;
  idToIndex.reserve(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    idToIndex[nodes[i].modelId] = i;
  }
  std::vector<std::size_t> degree(nodes.size(), 0);
  for (const EdgeRecord& edge : edges) {
    auto sIt = idToIndex.find(edge.sourceModelId);
    auto tIt = idToIndex.find(edge.targetModelId);
    if (sIt == idToIndex.end()
        || tIt == idToIndex.end()
        || sIt->second == tIt->second) {
      continue;
    }
    ++degree[sIt->second];
    ++degree[tIt->second];
  }

  std::vector<std::vector<std::size_t>> components =
    collectConnectedComponents(nodes, edges);
  if (components.size() <= 1) {
    return 0;
  }

  std::size_t mainComponent = std::numeric_limits<std::size_t>::max();
  for (std::size_t ci = 0; ci < components.size(); ++ci) {
    bool hasEdge = false;
    for (std::size_t idx : components[ci]) {
      if (degree[idx] > 0) {
        hasEdge = true;
        break;
      }
    }
    if (!hasEdge) {
      continue;
    }
    if (mainComponent == std::numeric_limits<std::size_t>::max()
        || components[ci].size() > components[mainComponent].size()) {
      mainComponent = ci;
    }
  }
  if (mainComponent == std::numeric_limits<std::size_t>::max()) {
    return 0;
  }

  const Rect mainRect = componentRect(nodes, components[mainComponent], attributes);
  const double mainWidth = rectWidth(mainRect);
  const double mainHeight = rectHeight(mainRect);
  if (mainWidth <= 1.0 || mainHeight <= 1.0) {
    return 0;
  }

  const Rect beforeBounds = graphNodeBounds(nodes, attributes);
  const double beforeArea = rectWidth(beforeBounds) * rectHeight(beforeBounds);
  if (beforeArea <= 1.0) {
    return 0;
  }
  const std::size_t beforeBareOverlaps =
    countNodeRectOverlaps(nodes, attributes, false);
  const std::size_t beforeSpacingOverlaps =
    countNodeRectOverlaps(nodes, attributes, true);

  const double outlierMargin =
    readDoubleEnv("DJERD_SIDECAR_BBOX_COMPACT_MARGIN", 8.0, 0.0, 240.0);
  const bool moveConnectedComponents =
    readBoolEnv("DJERD_SIDECAR_BBOX_COMPACT_CONNECTED", true);
  const bool moveIsolated =
    readBoolEnv("DJERD_SIDECAR_BBOX_COMPACT_ISOLATED", true);

  auto expandsMain = [&](const Rect& rect) {
    return rect.left < mainRect.left - outlierMargin
      || rect.right > mainRect.right + outlierMargin
      || rect.top < mainRect.top - outlierMargin
      || rect.bottom > mainRect.bottom + outlierMargin;
  };

  struct SidecarItem {
    std::vector<std::size_t> indices;
    Rect rect;
    bool isolated = false;
    double area = 0.0;
    std::string app;
    std::vector<std::string> tokens;
    std::string modelId;
  };

  std::vector<SidecarItem> items;
  items.reserve(components.size());
  for (std::size_t ci = 0; ci < components.size(); ++ci) {
    if (ci == mainComponent) {
      continue;
    }
    const Rect rect = componentRect(nodes, components[ci], attributes);
    if (!expandsMain(rect)) {
      continue;
    }
    bool hasEdge = false;
    for (std::size_t idx : components[ci]) {
      if (degree[idx] > 0) {
        hasEdge = true;
        break;
      }
    }
    if (hasEdge && !moveConnectedComponents) {
      continue;
    }
    if (!hasEdge && !moveIsolated) {
      continue;
    }
    SidecarItem item;
    item.indices = components[ci];
    item.rect = rect;
    item.isolated = !hasEdge;
    item.area = rectWidth(rect) * rectHeight(rect);
    if (!components[ci].empty()) {
      const NodeRecord& node = nodes[components[ci].front()];
      const std::size_t dot = node.modelId.find('.');
      item.app = dot == std::string::npos
        ? node.appLabel
        : node.modelId.substr(0, dot);
      item.tokens = modelNameTokens(node.modelId);
      item.modelId = node.modelId;
    }
    items.push_back(std::move(item));
  }
  if (items.empty()) {
    return 0;
  }

  std::sort(items.begin(), items.end(),
    [](const SidecarItem& left, const SidecarItem& right) {
      if (left.isolated != right.isolated) return !left.isolated;
      if (!left.isolated && std::abs(left.area - right.area) > 0.01) {
        return left.area > right.area;
      }
      if (left.app != right.app) return left.app < right.app;
      if (left.tokens != right.tokens) return left.tokens < right.tokens;
      return left.modelId < right.modelId;
    });

  std::vector<std::pair<std::size_t, std::pair<double, double>>> snapshot;
  std::unordered_set<std::size_t> snapSeen;
  for (const SidecarItem& item : items) {
    for (std::size_t idx : item.indices) {
      if (!snapSeen.insert(idx).second) {
        continue;
      }
      snapshot.push_back({
        idx,
        {attributes.x(nodes[idx].handle), attributes.y(nodes[idx].handle)}
      });
    }
  }

  const double gapX =
    readDoubleEnv("DJERD_SIDECAR_BBOX_COMPACT_GAP_X", 300.0, 0.0, 4000.0);
  const double gapY =
    readDoubleEnv("DJERD_SIDECAR_BBOX_COMPACT_GAP_Y", 180.0, 0.0, 4000.0);
  const double maxLaneHeight =
    readDoubleEnv(
      "DJERD_SIDECAR_BBOX_COMPACT_LANE_HEIGHT",
      mainHeight,
      1000.0,
      std::max(mainHeight, beforeArea));
  const double minGain =
    readDoubleEnv("DJERD_SIDECAR_BBOX_COMPACT_MIN_GAIN", 0.01, 0.0, 0.9);
  const double maxAspect =
    readDoubleEnv("DJERD_SIDECAR_BBOX_COMPACT_MAX_ASPECT", 2.2, 1.0, 10.0);
  const std::size_t spacingSlack = static_cast<std::size_t>(
    readDoubleEnv("DJERD_SIDECAR_BBOX_COMPACT_SPACING_SLACK", 0.0, 0.0, 100000.0));

  double columnX = mainRect.right + gapX;
  double columnY = mainRect.top;
  double columnWidth = 0.0;
  std::size_t columns = 1;
  for (const SidecarItem& item : items) {
    const double width = rectWidth(item.rect);
    const double height = rectHeight(item.rect);
    if (columnY > mainRect.top && columnY + height > mainRect.top + maxLaneHeight) {
      columnX += columnWidth + gapX;
      columnY = mainRect.top;
      columnWidth = 0.0;
      ++columns;
    }
    const double dx = columnX - item.rect.left;
    const double dy = columnY - item.rect.top;
    translateComponent(nodes, item.indices, attributes, dx, dy);
    columnY += height + gapY;
    columnWidth = std::max(columnWidth, width);
  }

  const Rect afterBounds = graphNodeBounds(nodes, attributes);
  const double afterWidth = rectWidth(afterBounds);
  const double afterHeight = rectHeight(afterBounds);
  const double afterArea = afterWidth * afterHeight;
  const double bigger = std::max(afterWidth, afterHeight);
  const double smaller = std::max(1.0, std::min(afterWidth, afterHeight));
  const double aspect = bigger / smaller;
  const bool accepted =
    afterArea < beforeArea * (1.0 - minGain)
    && aspect <= maxAspect
    && countNodeRectOverlaps(nodes, attributes, false) <= beforeBareOverlaps
    && countNodeRectOverlaps(nodes, attributes, true)
      <= beforeSpacingOverlaps + spacingSlack;

  if (!accepted) {
    for (const auto& entry : snapshot) {
      const std::size_t idx = entry.first;
      attributes.x(nodes[idx].handle) = entry.second.first;
      attributes.y(nodes[idx].handle) = entry.second.second;
    }
    return 0;
  }

  std::size_t connectedItems = 0;
  std::size_t isolatedItems = 0;
  for (const SidecarItem& item : items) {
    if (item.isolated) {
      ++isolatedItems;
    } else {
      ++connectedItems;
    }
  }
  std::fprintf(stderr,
    "[sidecar-bbox-compact-final] moved %zu connected components and "
    "%zu edge-less nodes into %zu sidecar columns; bbox %.2fB -> %.2fB "
    "(aspect=%.3f).\n",
    connectedItems,
    isolatedItems,
    columns,
    beforeArea / 1e9,
    afterArea / 1e9,
    aspect);
  return snapshot.size();
}

std::size_t attachIsolatedNodesByName(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  const bool enabled = readBoolEnv(
    "DJERD_ATTACH_ISOLATED_BY_NAME_FINAL",
    readBoolEnv("DJERD_RIGID_ATTACH_ISOLATED_FINAL", false));
  if (!enabled || nodes.empty()) {
    return 0;
  }

  std::unordered_set<std::string> connectedIds;
  connectedIds.reserve(edges.size() * 2);
  for (const EdgeRecord& edge : edges) {
    connectedIds.insert(edge.sourceModelId);
    connectedIds.insert(edge.targetModelId);
  }

  std::vector<std::size_t> connected;
  std::vector<std::size_t> isolated;
  connected.reserve(nodes.size());
  isolated.reserve(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    if (connectedIds.count(nodes[i].modelId)) {
      connected.push_back(i);
    } else {
      isolated.push_back(i);
    }
  }
  if (connected.empty() || isolated.empty()) {
    return 0;
  }

  const double margin = visualNodeMargin();
  const double maxRadius =
    readDoubleEnv("DJERD_RIGID_ATTACH_ISOLATED_MAX_RADIUS", 3600.0, 400.0, 24000.0);
  const double ringStep =
    readDoubleEnv("DJERD_RIGID_ATTACH_ISOLATED_RING_STEP", 260.0, 80.0, 1600.0);
  const int directions = static_cast<int>(
    readDoubleEnv("DJERD_RIGID_ATTACH_ISOLATED_DIRECTIONS", 16.0, 8.0, 48.0));
  const int minScore = static_cast<int>(std::round(
    readDoubleEnv("DJERD_ATTACH_ISOLATED_MIN_SCORE", 3.0, 0.0, 1000.0)));
  const bool checkRoutes =
    readBoolEnv("DJERD_ATTACH_ISOLATED_ROUTE_CHECK", false);
  const double bboxWeight =
    readDoubleEnv("DJERD_ATTACH_ISOLATED_BBOX_WEIGHT", 20.0, 0.0, 10000.0);
  const double radiusWeight =
    readDoubleEnv("DJERD_ATTACH_ISOLATED_RADIUS_WEIGHT", 1.0, 0.0, 10000.0);
  const double outwardWeight =
    readDoubleEnv("DJERD_ATTACH_ISOLATED_OUTWARD_WEIGHT", 35.0, 0.0, 10000.0);

  std::vector<std::vector<std::string>> tokens(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    tokens[i] = modelNameTokens(nodes[i].modelId);
  }

  auto appOf = [](const NodeRecord& node) {
    const std::size_t dot = node.modelId.find('.');
    if (dot != std::string::npos) {
      return node.modelId.substr(0, dot);
    }
    return node.appLabel;
  };

  auto tokenOverlap = [&](std::size_t a, std::size_t b) {
    const auto& left = tokens[a];
    const auto& right = tokens[b];
    std::size_t li = 0;
    std::size_t ri = 0;
    int overlap = 0;
    while (li < left.size() && ri < right.size()) {
      if (left[li] == right[ri]) {
        ++overlap;
        ++li;
        ++ri;
      } else if (left[li] < right[ri]) {
        ++li;
      } else {
        ++ri;
      }
    }
    return overlap;
  };

  const std::vector<std::vector<RoutePoint>> fixedRoutes =
    checkRoutes ? routeAllEdgesStraight(edges, attributes)
                : std::vector<std::vector<RoutePoint>>{};
  std::vector<Rect> occupied;
  occupied.reserve(nodes.size());
  for (std::size_t idx : connected) {
    occupied.push_back(nodeRect(nodes[idx], attributes, margin));
  }
  double connectedMinX = std::numeric_limits<double>::infinity();
  double connectedMinY = std::numeric_limits<double>::infinity();
  double connectedMaxX = -std::numeric_limits<double>::infinity();
  double connectedMaxY = -std::numeric_limits<double>::infinity();
  for (std::size_t idx : connected) {
    const Rect rect = nodeRect(nodes[idx], attributes, margin);
    connectedMinX = std::min(connectedMinX, rect.left);
    connectedMinY = std::min(connectedMinY, rect.top);
    connectedMaxX = std::max(connectedMaxX, rect.right);
    connectedMaxY = std::max(connectedMaxY, rect.bottom);
  }
  const double graphCenterX =
    std::isfinite(connectedMinX) ? (connectedMinX + connectedMaxX) * 0.5 : 0.0;
  const double graphCenterY =
    std::isfinite(connectedMinY) ? (connectedMinY + connectedMaxY) * 0.5 : 0.0;

  auto routeHitsRect = [&](const Rect& rect) {
    if (!checkRoutes) {
      return false;
    }
    for (const std::vector<RoutePoint>& route : fixedRoutes) {
      if (route.size() < 2) {
        continue;
      }
      for (std::size_t i = 1; i < route.size(); ++i) {
        if (segmentIntersectsRect(route[i - 1], route[i], rect)) {
          return true;
        }
      }
    }
    return false;
  };

  auto overlapsOccupied = [&](const Rect& rect) {
    for (const Rect& other : occupied) {
      if (rectsOverlap(rect, other)) {
        return true;
      }
    }
    return false;
  };

  auto rectAt = [&](const NodeRecord& node, double cx, double cy) {
    const double w = sanitizeNodeWidth(node, attributes);
    const double h = sanitizeNodeHeight(node, attributes);
    return Rect{
      cy + h / 2.0 + margin,
      cx - w / 2.0 - margin,
      cx + w / 2.0 + margin,
      cy - h / 2.0 - margin,
    };
  };

  std::sort(isolated.begin(), isolated.end(), [&](std::size_t left, std::size_t right) {
    return nodes[left].modelId < nodes[right].modelId;
  });

  constexpr double kPi = 3.14159265358979323846;
  std::size_t moved = 0;
  for (std::size_t idx : isolated) {
    int bestScore = std::numeric_limits<int>::min();
    std::size_t bestConnected = connected.front();
    const std::string isolatedApp = appOf(nodes[idx]);
    for (std::size_t candidate : connected) {
      int score = tokenOverlap(idx, candidate) * 10;
      if (!isolatedApp.empty() && isolatedApp == appOf(nodes[candidate])) {
        score += 2;
      }
    if (
        !tokens[idx].empty()
        && !tokens[candidate].empty()
        && tokens[idx].front() == tokens[candidate].front()) {
      score += 3;
      }
      if (score > bestScore) {
        bestScore = score;
        bestConnected = candidate;
      }
    }
    if (bestScore < minScore) {
      continue;
    }

    const NodeRecord& anchorNode = nodes[bestConnected];
    const double anchorX = sanitizeNodeCenterX(anchorNode, attributes);
    const double anchorY = sanitizeNodeCenterY(anchorNode, attributes);
    const double outwardAngle = std::atan2(anchorY - graphCenterY, anchorX - graphCenterX);
    const NodeRecord& node = nodes[idx];
    bool placed = false;
    double bestX = sanitizeNodeCenterX(node, attributes);
    double bestY = sanitizeNodeCenterY(node, attributes);
    double bestPlacementScore = std::numeric_limits<double>::infinity();

    auto angleDistance = [](double left, double right) {
      constexpr double kTwoPi = 2.0 * 3.14159265358979323846;
      double diff = std::fmod(std::abs(left - right), kTwoPi);
      if (diff > 3.14159265358979323846) {
        diff = kTwoPi - diff;
      }
      return diff;
    };
    auto bboxOverflow = [&](const Rect& rect) {
      if (!std::isfinite(connectedMinX)) {
        return 0.0;
      }
      return
        std::max(0.0, connectedMinX - rect.left)
        + std::max(0.0, rect.right - connectedMaxX)
        + std::max(0.0, connectedMinY - rect.top)
        + std::max(0.0, rect.bottom - connectedMaxY);
    };

    for (double radius = ringStep; radius <= maxRadius; radius += ringStep) {
      for (int d = 0; d < directions; ++d) {
        const int step = (d + 1) / 2;
        const double sign = (d % 2 == 0) ? -1.0 : 1.0;
        const double angle = outwardAngle
          + sign * static_cast<double>(step) * (2.0 * kPi / static_cast<double>(directions));
        const double cx = anchorX + std::cos(angle) * radius;
        const double cy = anchorY + std::sin(angle) * radius;
        const Rect candidateRect = rectAt(node, cx, cy);
        if (overlapsOccupied(candidateRect)) {
          continue;
        }
        if (routeHitsRect(candidateRect)) {
          continue;
        }
        const double score =
          bboxOverflow(candidateRect) * bboxWeight
          + radius * radiusWeight
          + angleDistance(angle, outwardAngle) * outwardWeight;
        if (score + 1e-6 < bestPlacementScore) {
          bestPlacementScore = score;
          bestX = cx;
          bestY = cy;
          placed = true;
        }
      }
    }

    if (!placed) {
      continue;
    }
    attributes.x(node.handle) = bestX;
    attributes.y(node.handle) = bestY;
    occupied.push_back(rectAt(node, bestX, bestY));
    ++moved;
  }

  if (moved > 0) {
    std::fprintf(stderr,
      "[isolated-name-attach-final] attached %zu/%zu edge-less nodes by name tokens.\n",
      moved,
      isolated.size());
  }
  return moved;
}

bool properSegmentIntersection(
  const RoutePoint& leftStart,
  const RoutePoint& leftEnd,
  const RoutePoint& rightStart,
  const RoutePoint& rightEnd,
  RoutePoint& intersection);

LayoutQualityMetrics measureLayoutQuality(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes,
  const std::vector<LeafBundleRecord>* leafBundles,
  const std::unordered_map<std::string, std::string>* clusterByModelId = nullptr) {
  LayoutQualityMetrics metrics;

  // Visual margin — node's "personal space" added to its 4-corner rect.
  // Per user spec: collision is judged on (rect + margin) area, not bare
  // rect. Edges entering this margin area count as a hit; nodes whose
  // margin areas overlap count as a node-node collision.
  const double kVisualMargin = visualNodeMargin();
  const double kLeafBundleMargin = leafBundleVisualMargin();

  // Per-bundle exempt set (parent + leaves) and bbox.
  // Per user spec: bundle internal nodes are excluded from ALL collision
  // checks; only the bundle's 4-corner bbox (with margin) vs external
  // entities counts.
  std::vector<std::unordered_set<std::string>> bundleExempt;
  std::vector<Rect> bundleRects;
  // Set of modelIds absorbed by ANY bundle — these nodes are skipped
  // from edgeNodeIntersections and nodeOverlaps because the bundle as
  // a whole already represents them.
  std::unordered_set<std::string> bundleAbsorbed;
  if (leafBundles != nullptr) {
    bundleExempt.reserve(leafBundles->size());
    bundleRects.reserve(leafBundles->size());
    for (const LeafBundleRecord& bundle : *leafBundles) {
      std::unordered_set<std::string> exempt;
      exempt.insert(bundle.parentModelId);
      bundleAbsorbed.insert(bundle.parentModelId);
      for (const std::string& leaf : bundle.leafModelIds) {
        exempt.insert(leaf);
        bundleAbsorbed.insert(leaf);
      }
      bundleExempt.push_back(std::move(exempt));
      bundleRects.push_back(renderedLeafBundleRect(bundle, kLeafBundleMargin));
    }
    // Bundle-vs-node overlap: count when bundle's margin-expanded bbox
    // overlaps an external node's margin-expanded rect.
    for (std::size_t bi = 0; bi < bundleRects.size(); ++bi) {
      const Rect& br = bundleRects[bi];
      for (const NodeRecord& nd : nodes) {
        if (bundleAbsorbed.count(nd.modelId)) continue;
        const Rect nr = nodeRect(nd, attributes, kVisualMargin);
        if (rectsOverlap(br, nr)) {
          metrics.bundleNodeOverlaps += 1;
        }
      }
    }
  }

  // Node-node rect overlaps — 4-corner rect-rect intersection on the
  // margin-expanded rectangles. Skip nodes absorbed by a bundle.
  {
    std::vector<std::pair<Rect, std::size_t>> rects;
    rects.reserve(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      if (bundleAbsorbed.count(nodes[i].modelId)) continue;
      rects.emplace_back(nodeRect(nodes[i], attributes, kVisualMargin), i);
    }
    std::sort(rects.begin(), rects.end(),
      [](const auto& a, const auto& b) { return a.first.left < b.first.left; });
    for (std::size_t i = 0; i < rects.size(); ++i) {
      for (std::size_t j = i + 1; j < rects.size(); ++j) {
        if (rects[j].first.left >= rects[i].first.right) break;
        if (rectsOverlap(rects[i].first, rects[j].first)) {
          metrics.nodeOverlaps += 1;
        }
      }
    }
  }
  // Node spacing (= rect with spacing buffer) overlap, retained for
  // diagnostics — excluded from the unified visualCrossings sum since
  // user-spec collision is strict 4-corner rect-rect only.
  metrics.nodeSpacingOverlaps = countNodeRectOverlaps(nodes, attributes, true);

  const bool reuseQualityObstacles = [] {
    const char* value = std::getenv("DJERD_QUALITY_REUSE_OBSTACLES");
    return !value || std::strcmp(value, "0") != 0;
  }();
  std::vector<NodeObstacle> qualityObstacles;
  if (reuseQualityObstacles) {
    qualityObstacles.reserve(nodes.size());
    for (const NodeRecord& node : nodes) {
      qualityObstacles.push_back({
        node.handle,
        node.modelId,
        nodeRect(node, attributes, kVisualMargin),
      });
    }
  }

  RouteOccupancy occupancy;

  for (std::size_t edgeIndex = 0; edgeIndex < routes.size() && edgeIndex < edges.size(); ++edgeIndex) {
    const std::vector<RoutePoint>& route = routes[edgeIndex];
    if (route.size() < 2) {
      continue;
    }

    const LineIntent line = makeLineIntent(edges[edgeIndex], edgeIndex, attributes);
    // Margin-expanded obstacle rects: edge entering the margin area
    // counts as a collision.
    std::vector<NodeObstacle> edgeObstacles;
    const std::vector<NodeObstacle>* obstacles = &qualityObstacles;
    if (!reuseQualityObstacles) {
      edgeObstacles = makeNodeObstacles(
        nodes,
        attributes,
        kVisualMargin,
        line.sourceHandle,
        line.targetHandle);
      obstacles = &edgeObstacles;
    }
    const std::vector<LineSegment> segments = buildLineSegments(route, line.lineIndex, line.lineId);

    const std::string& srcId = edges[edgeIndex].sourceModelId;
    const std::string& tgtId = edges[edgeIndex].targetModelId;

    for (const LineSegment& segment : segments) {
      metrics.routeSegments += 1;

      for (const NodeObstacle& obstacle : *obstacles) {
        if (obstacle.handle == line.sourceHandle
            || obstacle.handle == line.targetHandle) {
          continue;
        }
        // Skip nodes absorbed by a leaf bundle — the bundle's own bbox
        // is checked separately. Counting absorbed leaves and the
        // bundle bbox would double-count the same visual block.
        if (bundleAbsorbed.count(obstacle.nodeId)) continue;
        if (segmentIntersectsRect(segment.start, segment.end, obstacle.rect)) {
          metrics.edgeNodeIntersections += 1;
        }
      }
      // Edge segments that pass through a leaf-bundle bbox (excluding
      // bundles whose parent OR any leaf the edge connects to).
      for (std::size_t bi = 0; bi < bundleRects.size(); ++bi) {
        if (bundleExempt[bi].count(srcId) || bundleExempt[bi].count(tgtId)) continue;
        if (segmentIntersectsRect(segment.start, segment.end, bundleRects[bi])) {
          metrics.bundleEdgeIntersections += 1;
        }
      }
    }

    recordRouteOccupancy(route, line, occupancy);
  }

  std::vector<bool> overlappingEdgeFlags(edges.size(), false);
  for (auto& entry : occupancy.horizontalSegmentsByLane) {
    metrics.edgeSegmentOverlaps += countAxisSegmentOverlaps(entry.second, overlappingEdgeFlags);
  }
  for (auto& entry : occupancy.verticalSegmentsByLane) {
    metrics.edgeSegmentOverlaps += countAxisSegmentOverlaps(entry.second, overlappingEdgeFlags);
  }

  metrics.overlappingEdges = static_cast<std::size_t>(
    std::count(overlappingEdgeFlags.begin(), overlappingEdgeFlags.end(), true));

  double minX = std::numeric_limits<double>::infinity();
  double minY = std::numeric_limits<double>::infinity();
  double maxX = -std::numeric_limits<double>::infinity();
  double maxY = -std::numeric_limits<double>::infinity();
  for (const NodeRecord& node : nodes) {
    const double centerX = sanitizeNodeCenterX(node, attributes);
    const double centerY = sanitizeNodeCenterY(node, attributes);
    const double width = sanitizeNodeWidth(node, attributes);
    const double height = sanitizeNodeHeight(node, attributes);
    const double left = centerX - width / 2.0;
    const double right = centerX + width / 2.0;
    const double top = centerY - height / 2.0;
    const double bottom = centerY + height / 2.0;
    if (left < minX) minX = left;
    if (right > maxX) maxX = right;
    if (top < minY) minY = top;
    if (bottom > maxY) maxY = bottom;
  }
  if (std::isfinite(minX) && std::isfinite(maxX) && std::isfinite(minY) && std::isfinite(maxY)
      && maxX > minX && maxY > minY) {
    const double width = maxX - minX;
    const double height = maxY - minY;
    metrics.boundingBoxArea = width * height;
    const double bigger = std::max(width, height);
    const double smaller = std::max(1.0, std::min(width, height));
    metrics.aspectRatio = bigger / smaller;

    // nodeAreaCoverage: Σ node area / bbox area. Bundle-absorbed nodes
    // are excluded since the bundle bbox already represents them.
    double nodeAreaSum = 0.0;
    for (const NodeRecord& node : nodes) {
      if (bundleAbsorbed.count(node.modelId)) continue;
      nodeAreaSum +=
        sanitizeNodeWidth(node, attributes)
        * sanitizeNodeHeight(node, attributes);
    }
    if (metrics.boundingBoxArea > 1e-6) {
      metrics.nodeAreaCoverage = nodeAreaSum / metrics.boundingBoxArea;
    }

    // emptySpaceCv: divide bbox into a fixed 16×16 grid and compute the
    // CV of per-cell occupancy (fraction of cell covered by some node
    // rect). 0 = perfectly uniform fill; high = clumpy distribution
    // with concentrated whitespace.
    constexpr std::size_t kGridN = 16;
    if (width > 1e-6 && height > 1e-6) {
      const double cellW = width / static_cast<double>(kGridN);
      const double cellH = height / static_cast<double>(kGridN);
      std::vector<double> cellOccupied(kGridN * kGridN, 0.0);
      const double cellArea = cellW * cellH;
      for (const NodeRecord& node : nodes) {
        if (bundleAbsorbed.count(node.modelId)) continue;
        const double cx = sanitizeNodeCenterX(node, attributes);
        const double cy = sanitizeNodeCenterY(node, attributes);
        const double nw = sanitizeNodeWidth(node, attributes);
        const double nh = sanitizeNodeHeight(node, attributes);
        const double nLeft = cx - nw / 2.0;
        const double nRight = cx + nw / 2.0;
        const double nTop = cy - nh / 2.0;
        const double nBottom = cy + nh / 2.0;
        const std::size_t gxLo = static_cast<std::size_t>(std::max(0.0,
          std::floor((nLeft - minX) / cellW)));
        const std::size_t gxHi = std::min(kGridN - 1,
          static_cast<std::size_t>(std::max(0.0,
            std::floor((nRight - minX) / cellW))));
        const std::size_t gyLo = static_cast<std::size_t>(std::max(0.0,
          std::floor((nTop - minY) / cellH)));
        const std::size_t gyHi = std::min(kGridN - 1,
          static_cast<std::size_t>(std::max(0.0,
            std::floor((nBottom - minY) / cellH))));
        for (std::size_t gy = gyLo; gy <= gyHi; ++gy) {
          const double cellTop = minY + static_cast<double>(gy) * cellH;
          const double cellBottom = cellTop + cellH;
          const double overlapTop = std::max(nTop, cellTop);
          const double overlapBottom = std::min(nBottom, cellBottom);
          const double overlapH = std::max(0.0, overlapBottom - overlapTop);
          if (overlapH <= 0.0) continue;
          for (std::size_t gx = gxLo; gx <= gxHi; ++gx) {
            const double cellLeft = minX + static_cast<double>(gx) * cellW;
            const double cellRight = cellLeft + cellW;
            const double overlapLeft = std::max(nLeft, cellLeft);
            const double overlapRight = std::min(nRight, cellRight);
            const double overlapW = std::max(0.0, overlapRight - overlapLeft);
            cellOccupied[gy * kGridN + gx] += overlapW * overlapH;
          }
        }
      }
      double occSum = 0.0;
      double occSumSq = 0.0;
      for (double occ : cellOccupied) {
        const double frac = std::min(1.0, occ / std::max(cellArea, 1e-9));
        occSum += frac;
        occSumSq += frac * frac;
      }
      const double cellCount = static_cast<double>(cellOccupied.size());
      const double occMean = occSum / cellCount;
      const double occVar = std::max(0.0, occSumSq / cellCount - occMean * occMean);
      const double occStd = std::sqrt(occVar);
      metrics.emptySpaceCv = occMean > 1e-6 ? occStd / occMean : 0.0;
    }
  }

  double lengthSum = 0.0;
  double lengthSumSq = 0.0;
  std::size_t lengthCount = 0;
  for (const std::vector<RoutePoint>& route : routes) {
    if (route.size() < 2) {
      continue;
    }
    double length = 0.0;
    for (std::size_t pointIndex = 1; pointIndex < route.size(); ++pointIndex) {
      const double dx = route[pointIndex].x - route[pointIndex - 1].x;
      const double dy = route[pointIndex].y - route[pointIndex - 1].y;
      length += std::sqrt(dx * dx + dy * dy);
    }
    lengthSum += length;
    lengthSumSq += length * length;
    lengthCount += 1;
  }
  if (lengthCount > 0) {
    const double count = static_cast<double>(lengthCount);
    const double mean = lengthSum / count;
    metrics.meanEdgeLength = mean;
    const double variance = std::max(0.0, (lengthSumSq / count) - mean * mean);
    metrics.edgeLengthStddev = std::sqrt(variance);
    // B. edge_length_cv = stddev / mean. Uniform edge lengths → ~0.
    metrics.edgeLengthCv = mean > 1e-6 ? metrics.edgeLengthStddev / mean : 0.0;
  }

  // D. edge_bend_total — sum across all edges of internal-waypoint angle
  // changes. Straight polyline = 0; heavily bent routing > 0.
  {
    double bendSum = 0.0;
    for (const std::vector<RoutePoint>& route : routes) {
      if (route.size() < 3) continue;
      for (std::size_t i = 1; i + 1 < route.size(); ++i) {
        const double ax = route[i].x - route[i - 1].x;
        const double ay = route[i].y - route[i - 1].y;
        const double bx = route[i + 1].x - route[i].x;
        const double by = route[i + 1].y - route[i].y;
        const double aLen = std::sqrt(ax * ax + ay * ay);
        const double bLen = std::sqrt(bx * bx + by * by);
        if (aLen < 1e-6 || bLen < 1e-6) continue;
        double cosA = (ax * bx + ay * by) / (aLen * bLen);
        if (cosA > 1.0) cosA = 1.0;
        if (cosA < -1.0) cosA = -1.0;
        // π - angle between successive segments. Straight = 0; back-turn = π.
        bendSum += std::acos(cosA);
      }
    }
    metrics.edgeBendTotal = bendSum;
  }

  // C. crossing_angle_dist + cross subdivisions — for every edge-edge
  // segment crossing record the acute angle, the per-edge participation
  // count, and whether both edges connect different louvain clusters.
  // 90° = best visual clarity; near 0° = visually confusing. Brute-
  // force segment-pair enumeration with bbox prefilter.
  {
    struct Seg { double x1, y1, x2, y2; double minX, minY, maxX, maxY; std::size_t edgeIdx; };
    std::vector<Seg> segs;
    segs.reserve(routes.size() * 2);
    for (std::size_t e = 0; e < routes.size(); ++e) {
      const std::vector<RoutePoint>& route = routes[e];
      if (route.size() < 2) continue;
      for (std::size_t i = 1; i < route.size(); ++i) {
        Seg s;
        s.x1 = route[i - 1].x; s.y1 = route[i - 1].y;
        s.x2 = route[i].x;     s.y2 = route[i].y;
        s.minX = std::min(s.x1, s.x2); s.maxX = std::max(s.x1, s.x2);
        s.minY = std::min(s.y1, s.y2); s.maxY = std::max(s.y1, s.y2);
        s.edgeIdx = e;
        segs.push_back(s);
      }
    }
    // Pre-compute "is edge between distinct clusters" once per edge.
    std::vector<bool> edgeIsCrossCluster(edges.size(), false);
    if (clusterByModelId != nullptr && !clusterByModelId->empty()) {
      for (std::size_t e = 0; e < edges.size(); ++e) {
        auto sIt = clusterByModelId->find(edges[e].sourceModelId);
        auto tIt = clusterByModelId->find(edges[e].targetModelId);
        if (sIt == clusterByModelId->end() || tIt == clusterByModelId->end()) {
          continue;
        }
        edgeIsCrossCluster[e] =
          !sIt->second.empty() && !tIt->second.empty()
          && sIt->second != tIt->second;
      }
    }
    double angleSum = 0.0;
    double angleSumSq = 0.0;
    std::size_t crossCount = 0;
    std::size_t crossClusterCrossings = 0;
    std::vector<std::size_t> perEdgeCrossings(edges.size(), 0);
    for (std::size_t i = 0; i < segs.size(); ++i) {
      for (std::size_t j = i + 1; j < segs.size(); ++j) {
        if (segs[i].edgeIdx == segs[j].edgeIdx) continue;
        // bbox prefilter
        if (segs[i].maxX < segs[j].minX || segs[j].maxX < segs[i].minX) continue;
        if (segs[i].maxY < segs[j].minY || segs[j].maxY < segs[i].minY) continue;
        RoutePoint isect;
        if (!properSegmentIntersection(
            RoutePoint{segs[i].x1, segs[i].y1},
            RoutePoint{segs[i].x2, segs[i].y2},
            RoutePoint{segs[j].x1, segs[j].y1},
            RoutePoint{segs[j].x2, segs[j].y2}, isect)) {
          continue;
        }
        const double ax = segs[i].x2 - segs[i].x1;
        const double ay = segs[i].y2 - segs[i].y1;
        const double bx = segs[j].x2 - segs[j].x1;
        const double by = segs[j].y2 - segs[j].y1;
        const double aLen = std::sqrt(ax * ax + ay * ay);
        const double bLen = std::sqrt(bx * bx + by * by);
        if (aLen < 1e-6 || bLen < 1e-6) continue;
        double cosA = std::abs((ax * bx + ay * by) / (aLen * bLen));
        if (cosA > 1.0) cosA = 1.0;
        // acute angle in [0, π/2]; cosA=0 → π/2 (perpendicular, best)
        const double ang = std::acos(cosA);
        angleSum += ang;
        angleSumSq += ang * ang;
        ++crossCount;
        // Per-edge participation. Each crossing increments both edges.
        if (segs[i].edgeIdx < perEdgeCrossings.size()) {
          ++perEdgeCrossings[segs[i].edgeIdx];
        }
        if (segs[j].edgeIdx < perEdgeCrossings.size()) {
          ++perEdgeCrossings[segs[j].edgeIdx];
        }
        // Cross-cluster bridge crossings: both edges go between distinct
        // clusters. This isolates the cluster-bridge tangle from
        // intra-cluster routing noise.
        if (segs[i].edgeIdx < edgeIsCrossCluster.size()
            && segs[j].edgeIdx < edgeIsCrossCluster.size()
            && edgeIsCrossCluster[segs[i].edgeIdx]
            && edgeIsCrossCluster[segs[j].edgeIdx]) {
          ++crossClusterCrossings;
        }
      }
    }
    if (crossCount > 0) {
      const double count = static_cast<double>(crossCount);
      const double mean = angleSum / count;
      metrics.crossingAngleMean = mean;
      const double variance = std::max(0.0, (angleSumSq / count) - mean * mean);
      const double stddev = std::sqrt(variance);
      metrics.crossingAngleCv = mean > 1e-6 ? stddev / mean : 0.0;
    }
    metrics.edgeCrossingsBetweenClusters = crossClusterCrossings;
    // Per-edge percentiles + clean-edge ratio.
    if (!perEdgeCrossings.empty()) {
      std::vector<std::size_t> sorted = perEdgeCrossings;
      std::sort(sorted.begin(), sorted.end());
      const std::size_t lastIdx = sorted.size() - 1;
      const std::size_t idx50 = static_cast<std::size_t>(
        std::floor(0.50 * static_cast<double>(lastIdx)));
      const std::size_t idx90 = static_cast<std::size_t>(
        std::floor(0.90 * static_cast<double>(lastIdx)));
      metrics.crossingsPerEdgeP50 = sorted[idx50];
      metrics.crossingsPerEdgeP90 = sorted[idx90];
      const std::size_t cleanCount = static_cast<std::size_t>(
        std::count(perEdgeCrossings.begin(), perEdgeCrossings.end(),
                   static_cast<std::size_t>(0)));
      metrics.cleanEdgeRatio =
        static_cast<double>(cleanCount)
        / static_cast<double>(perEdgeCrossings.size());

      // Top-N high-cross edges. Sort edge indices by perEdgeCrossings
      // descending and emit up to N records. N is env-tunable
      // (DJERD_TOP_CROSS_EDGES_N, default 20). Edges with zero
      // crossings are skipped — they aren't offenders.
      const char* topNEnv = std::getenv("DJERD_TOP_CROSS_EDGES_N");
      const std::size_t topN = topNEnv
        ? static_cast<std::size_t>(std::max(0, std::atoi(topNEnv)))
        : 20;
      if (topN > 0) {
        std::vector<std::size_t> order(perEdgeCrossings.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(),
          [&perEdgeCrossings](std::size_t a, std::size_t b) {
            return perEdgeCrossings[a] > perEdgeCrossings[b];
          });
        metrics.topCrossEdges.reserve(std::min(topN, order.size()));
        for (std::size_t k = 0; k < topN && k < order.size(); ++k) {
          const std::size_t ei = order[k];
          if (perEdgeCrossings[ei] == 0) break;
          if (ei >= edges.size()) continue;
          CrossingEdgeRecord rec;
          rec.sourceModelId = edges[ei].sourceModelId;
          rec.targetModelId = edges[ei].targetModelId;
          rec.crossings = perEdgeCrossings[ei];
          metrics.topCrossEdges.push_back(std::move(rec));
        }
      }
    }
  }

  // A. stress_score (Gansner et al. 2005). For each pair of nodes (i,j)
  // with a finite graph-theoretic distance d_ij (BFS hops in the
  // unweighted graph), stress_ij = w_ij · (||p_i - p_j|| - L · d_ij)²
  // with w_ij = 1/d_ij² and L = meanEdgeLength as the ideal-length
  // calibration. Sum is normalized by the number of contributing pairs
  // so the metric is scale-invariant across graphs of different sizes.
  // Lower = positions match graph-theoretic distances; canonical
  // force-directed layout quality metric.
  if (metrics.meanEdgeLength > 1e-6 && !nodes.empty()) {
    const double L = metrics.meanEdgeLength;
    std::unordered_map<std::string, std::size_t> nodeIdxByModelId;
    nodeIdxByModelId.reserve(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      nodeIdxByModelId[nodes[i].modelId] = i;
    }
    std::vector<std::vector<std::size_t>> adj(nodes.size());
    for (const EdgeRecord& e : edges) {
      auto sIt = nodeIdxByModelId.find(e.sourceModelId);
      auto tIt = nodeIdxByModelId.find(e.targetModelId);
      if (sIt == nodeIdxByModelId.end() || tIt == nodeIdxByModelId.end()) continue;
      if (sIt->second == tIt->second) continue;
      adj[sIt->second].push_back(tIt->second);
      adj[tIt->second].push_back(sIt->second);
    }
    constexpr std::uint16_t kUnreached = 0xFFFF;
    std::vector<std::uint16_t> dist(nodes.size(), kUnreached);
    std::vector<std::size_t> bfsQueue;
    bfsQueue.reserve(nodes.size());
    double stressAccum = 0.0;
    std::size_t pairCount = 0;
    for (std::size_t src = 0; src < nodes.size(); ++src) {
      std::fill(dist.begin(), dist.end(), kUnreached);
      dist[src] = 0;
      bfsQueue.clear();
      bfsQueue.push_back(src);
      for (std::size_t head = 0; head < bfsQueue.size(); ++head) {
        const std::size_t u = bfsQueue[head];
        const std::uint16_t du = dist[u];
        if (du == kUnreached) continue;
        for (std::size_t v : adj[u]) {
          if (dist[v] != kUnreached) continue;
          dist[v] = static_cast<std::uint16_t>(du + 1);
          bfsQueue.push_back(v);
        }
      }
      // Only count each unordered pair once (j > src).
      const double srcX = sanitizeNodeCenterX(nodes[src], attributes);
      const double srcY = sanitizeNodeCenterY(nodes[src], attributes);
      for (std::size_t j = src + 1; j < nodes.size(); ++j) {
        const std::uint16_t d = dist[j];
        if (d == kUnreached || d == 0) continue;
        const double djX = sanitizeNodeCenterX(nodes[j], attributes);
        const double djY = sanitizeNodeCenterY(nodes[j], attributes);
        const double dx = djX - srcX;
        const double dy = djY - srcY;
        const double euclid = std::sqrt(dx * dx + dy * dy);
        const double ideal = L * static_cast<double>(d);
        const double diff = euclid - ideal;
        const double w = 1.0 / (static_cast<double>(d) * static_cast<double>(d));
        stressAccum += w * diff * diff;
        ++pairCount;
      }
    }
    if (pairCount > 0) {
      // Normalize by pair count → average per-pair stress. Divide by L²
      // to make it dimensionless (length² → unitless), so different
      // graphs with different ideal-lengths are still comparable.
      metrics.stressScore =
        stressAccum / (static_cast<double>(pairCount) * L * L);
    }

    // E. hub_clearance_p10 — for top-decile-degree nodes, compute
    // distance to nearest non-incident node. Aggregate as the 10th
    // percentile across these hubs (low value = some hubs crowded).
    // Reuses the adjacency built for stress. Top decile by degree
    // is a statistical quantile, not a per-data threshold.
    if (nodes.size() >= 10) {
      std::vector<std::size_t> degOrder(nodes.size());
      for (std::size_t i = 0; i < nodes.size(); ++i) degOrder[i] = i;
      std::sort(degOrder.begin(), degOrder.end(),
        [&adj](std::size_t a, std::size_t b) {
          return adj[a].size() > adj[b].size();
        });
      const std::size_t hubCount =
        std::max<std::size_t>(1, nodes.size() / 10);
      std::vector<double> hubClearances;
      hubClearances.reserve(hubCount);
      for (std::size_t k = 0; k < hubCount; ++k) {
        const std::size_t h = degOrder[k];
        if (adj[h].empty()) continue;
        std::unordered_set<std::size_t> incident(adj[h].begin(), adj[h].end());
        incident.insert(h);
        const double hx = sanitizeNodeCenterX(nodes[h], attributes);
        const double hy = sanitizeNodeCenterY(nodes[h], attributes);
        double nearest = std::numeric_limits<double>::infinity();
        for (std::size_t j = 0; j < nodes.size(); ++j) {
          if (incident.count(j)) continue;
          const double dx = sanitizeNodeCenterX(nodes[j], attributes) - hx;
          const double dy = sanitizeNodeCenterY(nodes[j], attributes) - hy;
          const double d = std::sqrt(dx * dx + dy * dy);
          if (d < nearest) nearest = d;
        }
        if (std::isfinite(nearest)) hubClearances.push_back(nearest);
      }
      if (!hubClearances.empty()) {
        std::sort(hubClearances.begin(), hubClearances.end());
        // 10th percentile via linear interpolation between bracketing
        // indices. With ≥10 hubs gives the lower-tail clearance.
        const double rank = 0.10 * static_cast<double>(hubClearances.size() - 1);
        const std::size_t lo = static_cast<std::size_t>(std::floor(rank));
        const std::size_t hi = std::min(lo + 1, hubClearances.size() - 1);
        const double frac = rank - static_cast<double>(lo);
        metrics.hubClearanceP10 =
          hubClearances[lo] * (1.0 - frac) + hubClearances[hi] * frac;
      }
    }
  }

  // F. cluster_compactness_mean — for each cluster (≥2 members), compute
  // (cluster bbox area) / (Σ member node area). 1.0 = perfectly packed;
  // >1 = bbox bigger than node footprint (sparse); <1 cannot happen
  // since bbox encloses all member rects. Take mean across clusters.
  // Skipped when clusterByModelId is not provided.
  if (clusterByModelId != nullptr && !clusterByModelId->empty()) {
    std::unordered_map<std::string, std::vector<std::size_t>> membersBy;
    std::unordered_map<std::string, std::size_t> idxByModelId;
    idxByModelId.reserve(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) idxByModelId[nodes[i].modelId] = i;
    for (const auto& kv : *clusterByModelId) {
      auto it = idxByModelId.find(kv.first);
      if (it == idxByModelId.end()) continue;
      membersBy[kv.second].push_back(it->second);
    }
    double compactSum = 0.0;
    std::size_t compactCount = 0;
    for (const auto& kv : membersBy) {
      const std::vector<std::size_t>& members = kv.second;
      if (members.size() < 2) continue;
      double minX = std::numeric_limits<double>::infinity();
      double minY = std::numeric_limits<double>::infinity();
      double maxX = -std::numeric_limits<double>::infinity();
      double maxY = -std::numeric_limits<double>::infinity();
      double nodeAreaSum = 0.0;
      for (std::size_t i : members) {
        const double cx = sanitizeNodeCenterX(nodes[i], attributes);
        const double cy = sanitizeNodeCenterY(nodes[i], attributes);
        const double w = sanitizeNodeWidth(nodes[i], attributes);
        const double h = sanitizeNodeHeight(nodes[i], attributes);
        if (cx - w / 2.0 < minX) minX = cx - w / 2.0;
        if (cx + w / 2.0 > maxX) maxX = cx + w / 2.0;
        if (cy - h / 2.0 < minY) minY = cy - h / 2.0;
        if (cy + h / 2.0 > maxY) maxY = cy + h / 2.0;
        nodeAreaSum += w * h;
      }
      const double bboxArea = (maxX - minX) * (maxY - minY);
      if (nodeAreaSum < 1e-6 || bboxArea < 1e-6) continue;
      compactSum += bboxArea / nodeAreaSum;
      ++compactCount;
    }
    if (compactCount > 0) {
      metrics.clusterCompactnessMean =
        compactSum / static_cast<double>(compactCount);
    }
  }

  // Composite quality score. Weighted sum of 7 normalized sub-scores
  // (each ∈ [0, 1] with 1 = best). Weights from Bennett et al. 2007
  // aesthetic ranking: crossings ≈ 45% (clean + severity), crossing
  // angle 20%, stress 15%, compactness/uniformity 20%. All sub-scores
  // are exposed individually so downstream tooling can re-weight.
  {
    constexpr double kPi = 3.14159265358979;
    metrics.subCleanQuality = metrics.cleanEdgeRatio;
    metrics.subSeverityQuality =
      1.0 / (1.0 + static_cast<double>(metrics.crossingsPerEdgeP90) * 0.1);
    metrics.subAngleQuality = std::min(1.0,
      metrics.crossingAngleMean / (kPi / 2.0));
    metrics.subStressQuality = 1.0 / (1.0 + metrics.stressScore);
    metrics.subCompactQuality = std::min(1.0, metrics.nodeAreaCoverage * 5.0);
    metrics.subUniformQuality = 1.0 / (1.0 + metrics.edgeLengthCv);
    metrics.subSpreadQuality = 1.0 / (1.0 + metrics.emptySpaceCv * 0.5);
    metrics.compositeQuality =
        0.30 * metrics.subCleanQuality
      + 0.15 * metrics.subSeverityQuality
      + 0.20 * metrics.subAngleQuality
      + 0.15 * metrics.subStressQuality
      + 0.10 * metrics.subCompactQuality
      + 0.05 * metrics.subUniformQuality
      + 0.05 * metrics.subSpreadQuality;
  }

  // Unified visual crossing count per user spec: strict 4-corner rect-
  // segment and rect-rect tests only. nodeSpacingOverlaps and any
  // padding-based "near miss" detections are NOT included.
  metrics.visualCrossings =
    metrics.edgeCrossings
    + metrics.edgeNodeIntersections
    + metrics.nodeOverlaps
    + metrics.bundleEdgeIntersections
    + metrics.bundleNodeOverlaps;

  return metrics;
}

void enforceNodeSeparationStrong(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  for (int attempt = 0; attempt < 4; ++attempt) {
    enforceNodeSeparation(nodes, attributes);
    if (
      countNodeRectOverlaps(nodes, attributes, false) == 0
      && countNodeRectOverlaps(nodes, attributes, true) == 0) {
      return;
    }
    placeNodesWithoutOverlaps(nodes, attributes);
  }
}

void addLaneValue(std::vector<double>& lanes, double value) {
  if (!isFiniteCoordinate(value)) {
    return;
  }
  lanes.push_back(value);
}

void ensureLaneValue(std::vector<double>& lanes, double value) {
  if (!isFiniteCoordinate(value)) {
    return;
  }

  const bool exists = std::any_of(
    lanes.begin(),
    lanes.end(),
    [=](double existing) {
      return std::abs(existing - value) < 0.01;
    });
  if (!exists) {
    lanes.insert(lanes.begin(), value);
  }
}

std::vector<double> nearestUniqueLaneValues(
  std::vector<double> lanes,
  double reference,
  std::size_t limit) {
  std::sort(
    lanes.begin(),
    lanes.end(),
    [=](double left, double right) {
      const double leftDistance = std::abs(left - reference);
      const double rightDistance = std::abs(right - reference);
      if (std::abs(leftDistance - rightDistance) > 0.01) {
        return leftDistance < rightDistance;
      }
      return left < right;
    });

  std::vector<double> selected;
  selected.reserve(std::min(limit, lanes.size()));
  for (double lane : lanes) {
    const bool duplicate = std::any_of(
      selected.begin(),
      selected.end(),
      [=](double existing) {
        return std::abs(existing - lane) < 28.0;
      });
    if (duplicate) {
      continue;
    }

    selected.push_back(lane);
    if (selected.size() >= limit) {
      break;
    }
  }

  return selected;
}

double normalizeLaneValue(double value) {
  return std::round(value * 100.0) / 100.0;
}

void addRequiredLane(std::vector<double>& lanes, double value) {
  if (!isFiniteCoordinate(value)) {
    return;
  }
  lanes.push_back(normalizeLaneValue(value));
}

double distanceToClosestAnchor(double value, const std::vector<double>& anchors) {
  double distance = std::numeric_limits<double>::infinity();
  for (double anchor : anchors) {
    distance = std::min(distance, std::abs(value - anchor));
  }
  return distance;
}

std::vector<double> selectVisibilityLanes(
  std::vector<double> required,
  std::vector<double> candidates,
  const std::vector<double>& anchors,
  std::size_t limit) {
  std::vector<double> lanes;
  lanes.reserve(limit);

  for (double lane : required) {
    addRequiredLane(lanes, lane);
  }

  std::sort(lanes.begin(), lanes.end());
  lanes.erase(
    std::unique(
      lanes.begin(),
      lanes.end(),
      [](double left, double right) {
        return std::abs(left - right) < 0.01;
      }),
    lanes.end());

  std::vector<double> normalizedCandidates;
  normalizedCandidates.reserve(candidates.size());
  for (double candidate : candidates) {
    if (isFiniteCoordinate(candidate)) {
      normalizedCandidates.push_back(normalizeLaneValue(candidate));
    }
  }
  std::sort(normalizedCandidates.begin(), normalizedCandidates.end());
  normalizedCandidates.erase(
    std::unique(
      normalizedCandidates.begin(),
      normalizedCandidates.end(),
      [](double left, double right) {
        return std::abs(left - right) < 0.01;
      }),
    normalizedCandidates.end());

  std::sort(
    normalizedCandidates.begin(),
    normalizedCandidates.end(),
    [&](double left, double right) {
      const double leftDistance = distanceToClosestAnchor(left, anchors);
      const double rightDistance = distanceToClosestAnchor(right, anchors);
      if (std::abs(leftDistance - rightDistance) > 0.01) {
        return leftDistance < rightDistance;
      }
      return left < right;
    });

  for (double candidate : normalizedCandidates) {
    if (lanes.size() >= limit) {
      break;
    }

    const bool exists = std::any_of(
      lanes.begin(),
      lanes.end(),
      [=](double existing) {
        return std::abs(existing - candidate) < 0.01;
      });
    if (!exists) {
      lanes.push_back(candidate);
    }
  }

  std::sort(lanes.begin(), lanes.end());
  return lanes;
}

int findLaneIndex(const std::vector<double>& lanes, double value) {
  const double normalized = normalizeLaneValue(value);
  for (std::size_t index = 0; index < lanes.size(); ++index) {
    if (std::abs(lanes[index] - normalized) < 0.01) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

bool pointInsideRect(const RoutePoint& point, const Rect& rect) {
  return point.x > rect.left
    && point.x < rect.right
    && point.y > rect.top
    && point.y < rect.bottom;
}

bool pointInsideAnyRect(const RoutePoint& point, const std::vector<Rect>& obstacles) {
  return std::any_of(
    obstacles.begin(),
    obstacles.end(),
    [&](const Rect& rect) {
      return pointInsideRect(point, rect);
    });
}

std::vector<std::pair<double, double>> blockedIntervalsForHorizontalLane(
  double y,
  const std::vector<Rect>& obstacles) {
  std::vector<std::pair<double, double>> intervals;
  for (const Rect& obstacle : obstacles) {
    if (y > obstacle.top && y < obstacle.bottom) {
      intervals.emplace_back(obstacle.left, obstacle.right);
    }
  }
  std::sort(intervals.begin(), intervals.end());
  return intervals;
}

std::vector<std::pair<double, double>> blockedIntervalsForVerticalLane(
  double x,
  const std::vector<Rect>& obstacles) {
  std::vector<std::pair<double, double>> intervals;
  for (const Rect& obstacle : obstacles) {
    if (x > obstacle.left && x < obstacle.right) {
      intervals.emplace_back(obstacle.top, obstacle.bottom);
    }
  }
  std::sort(intervals.begin(), intervals.end());
  return intervals;
}

bool intervalIntersectsAnyBlocked(
  double start,
  double end,
  const std::vector<std::pair<double, double>>& blockedIntervals) {
  const double minValue = std::min(start, end);
  const double maxValue = std::max(start, end);
  for (const auto& blocked : blockedIntervals) {
    if (blocked.first >= maxValue) {
      break;
    }
    if (blocked.second > minValue && blocked.first < maxValue) {
      return true;
    }
  }
  return false;
}

VisibilityRoute routeVisibilityGrid(
  const RoutePoint& start,
  const RoutePoint& end,
  const Rect& graphBounds,
  const std::vector<Rect>& obstacles,
  double laneOffset) {
  constexpr std::size_t maxVisibilityLanes = 64;
  constexpr double outerGap = 220.0;

  std::vector<double> requiredX;
  std::vector<double> requiredY;
  std::vector<double> candidateX;
  std::vector<double> candidateY;
  addRequiredLane(requiredX, start.x);
  addRequiredLane(requiredX, end.x);
  addRequiredLane(requiredX, graphBounds.left - outerGap - std::abs(laneOffset));
  addRequiredLane(requiredX, graphBounds.right + outerGap + std::abs(laneOffset));
  addRequiredLane(requiredY, start.y);
  addRequiredLane(requiredY, end.y);
  addRequiredLane(requiredY, graphBounds.top - outerGap - std::abs(laneOffset));
  addRequiredLane(requiredY, graphBounds.bottom + outerGap + std::abs(laneOffset));

  for (const Rect& obstacle : obstacles) {
    addRequiredLane(candidateX, obstacle.left - kVisibilityLaneClearance);
    addRequiredLane(candidateX, obstacle.right + kVisibilityLaneClearance);
    addRequiredLane(candidateY, obstacle.top - kVisibilityLaneClearance);
    addRequiredLane(candidateY, obstacle.bottom + kVisibilityLaneClearance);
  }

  const double midX = (start.x + end.x) / 2.0;
  const double midY = (start.y + end.y) / 2.0;
  const std::vector<double> xAnchors = { start.x, end.x, midX };
  const std::vector<double> yAnchors = { start.y, end.y, midY };
  const std::vector<double> xLanes =
    selectVisibilityLanes(std::move(requiredX), std::move(candidateX), xAnchors, maxVisibilityLanes);
  const std::vector<double> yLanes =
    selectVisibilityLanes(std::move(requiredY), std::move(candidateY), yAnchors, maxVisibilityLanes);
  const int startX = findLaneIndex(xLanes, start.x);
  const int startY = findLaneIndex(yLanes, start.y);
  const int endX = findLaneIndex(xLanes, end.x);
  const int endY = findLaneIndex(yLanes, end.y);

  if (startX < 0 || startY < 0 || endX < 0 || endY < 0) {
    return {};
  }

  const std::size_t width = xLanes.size();
  const std::size_t height = yLanes.size();
  const std::size_t total = width * height;
  const auto nodeIndex = [=](std::size_t x, std::size_t y) {
    return y * width + x;
  };
  const std::size_t startNode = nodeIndex(static_cast<std::size_t>(startX), static_cast<std::size_t>(startY));
  const std::size_t endNode = nodeIndex(static_cast<std::size_t>(endX), static_cast<std::size_t>(endY));

  std::vector<bool> blockedPoint(total, false);
  for (std::size_t y = 0; y < height; ++y) {
    for (std::size_t x = 0; x < width; ++x) {
      const std::size_t index = nodeIndex(x, y);
      if (index == startNode || index == endNode) {
        continue;
      }
      blockedPoint[index] = pointInsideAnyRect({ xLanes[x], yLanes[y] }, obstacles);
    }
  }

  std::vector<std::vector<std::pair<double, double>>> horizontalBlocked;
  horizontalBlocked.reserve(height);
  for (double y : yLanes) {
    horizontalBlocked.push_back(blockedIntervalsForHorizontalLane(y, obstacles));
  }
  std::vector<std::vector<std::pair<double, double>>> verticalBlocked;
  verticalBlocked.reserve(width);
  for (double x : xLanes) {
    verticalBlocked.push_back(blockedIntervalsForVerticalLane(x, obstacles));
  }

  std::vector<double> distance(total, std::numeric_limits<double>::infinity());
  std::vector<std::size_t> previous(total, total);
  using QueueEntry = std::pair<double, std::size_t>;
  std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> pending;

  distance[startNode] = 0.0;
  pending.emplace(0.0, startNode);

  while (!pending.empty()) {
    const auto [cost, current] = pending.top();
    pending.pop();
    if (cost > distance[current] + 0.01) {
      continue;
    }
    if (current == endNode) {
      break;
    }

    const std::size_t x = current % width;
    const std::size_t y = current / width;
    const auto visit = [&](std::size_t nextX, std::size_t nextY, bool horizontal) {
      const std::size_t next = nodeIndex(nextX, nextY);
      if (blockedPoint[next]) {
        return;
      }

      const bool blocked = horizontal
        ? intervalIntersectsAnyBlocked(xLanes[x], xLanes[nextX], horizontalBlocked[y])
        : intervalIntersectsAnyBlocked(yLanes[y], yLanes[nextY], verticalBlocked[x]);
      if (blocked) {
        return;
      }

      const double stepCost = horizontal
        ? std::abs(xLanes[x] - xLanes[nextX])
        : std::abs(yLanes[y] - yLanes[nextY]);
      const double nextCost = cost + stepCost;
      if (nextCost + 0.01 >= distance[next]) {
        return;
      }

      distance[next] = nextCost;
      previous[next] = current;
      pending.emplace(nextCost, next);
    };

    if (x > 0) {
      visit(x - 1, y, true);
    }
    if (x + 1 < width) {
      visit(x + 1, y, true);
    }
    if (y > 0) {
      visit(x, y - 1, false);
    }
    if (y + 1 < height) {
      visit(x, y + 1, false);
    }
  }

  if (!std::isfinite(distance[endNode])) {
    return {};
  }

  std::vector<RoutePoint> reversedPoints;
  for (std::size_t current = endNode; current != total; current = previous[current]) {
    const std::size_t x = current % width;
    const std::size_t y = current / width;
    reversedPoints.push_back({ xLanes[x], yLanes[y] });
    if (current == startNode) {
      break;
    }
  }

  std::reverse(reversedPoints.begin(), reversedPoints.end());
  return { true, compressRoutePoints(std::move(reversedPoints)) };
}

VisibilityRoute routeVisibilityGridWithPorts(
  const std::vector<VisibilityPort>& sourcePorts,
  const std::vector<VisibilityPort>& targetPorts,
  const Rect& graphBounds,
  const std::vector<Rect>& obstacles,
  double laneOffset,
  const RouteOccupancy* occupancy = nullptr) {
  constexpr std::size_t maxVisibilityLanes = 96;
  constexpr double outerGap = 220.0;

  if (sourcePorts.empty() || targetPorts.empty()) {
    return {};
  }

  std::vector<double> requiredX;
  std::vector<double> requiredY;
  std::vector<double> candidateX;
  std::vector<double> candidateY;
  std::vector<double> xAnchors;
  std::vector<double> yAnchors;

  addRequiredLane(requiredX, graphBounds.left - outerGap - std::abs(laneOffset));
  addRequiredLane(requiredX, graphBounds.right + outerGap + std::abs(laneOffset));
  addRequiredLane(requiredY, graphBounds.top - outerGap - std::abs(laneOffset));
  addRequiredLane(requiredY, graphBounds.bottom + outerGap + std::abs(laneOffset));

  for (const VisibilityPort& port : sourcePorts) {
    addRequiredLane(requiredX, port.stub.x);
    addRequiredLane(requiredY, port.stub.y);
    xAnchors.push_back(port.stub.x);
    yAnchors.push_back(port.stub.y);
  }
  for (const VisibilityPort& port : targetPorts) {
    addRequiredLane(requiredX, port.stub.x);
    addRequiredLane(requiredY, port.stub.y);
    xAnchors.push_back(port.stub.x);
    yAnchors.push_back(port.stub.y);
  }

  for (const Rect& obstacle : obstacles) {
    addRequiredLane(candidateX, obstacle.left - kVisibilityLaneClearance);
    addRequiredLane(candidateX, obstacle.right + kVisibilityLaneClearance);
    addRequiredLane(candidateY, obstacle.top - kVisibilityLaneClearance);
    addRequiredLane(candidateY, obstacle.bottom + kVisibilityLaneClearance);
  }

  const std::vector<double> xLanes =
    selectVisibilityLanes(std::move(requiredX), std::move(candidateX), xAnchors, maxVisibilityLanes);
  const std::vector<double> yLanes =
    selectVisibilityLanes(std::move(requiredY), std::move(candidateY), yAnchors, maxVisibilityLanes);
  const std::size_t width = xLanes.size();
  const std::size_t height = yLanes.size();
  const std::size_t total = width * height;
  const auto nodeIndex = [=](std::size_t x, std::size_t y) {
    return y * width + x;
  };

  std::vector<bool> blockedPoint(total, false);
  for (std::size_t y = 0; y < height; ++y) {
    for (std::size_t x = 0; x < width; ++x) {
      blockedPoint[nodeIndex(x, y)] = pointInsideAnyRect({ xLanes[x], yLanes[y] }, obstacles);
    }
  }

  std::vector<std::vector<std::pair<double, double>>> horizontalBlocked;
  horizontalBlocked.reserve(height);
  for (double y : yLanes) {
    horizontalBlocked.push_back(blockedIntervalsForHorizontalLane(y, obstacles));
  }
  std::vector<std::vector<std::pair<double, double>>> verticalBlocked;
  verticalBlocked.reserve(width);
  for (double x : xLanes) {
    verticalBlocked.push_back(blockedIntervalsForVerticalLane(x, obstacles));
  }

  std::vector<int> sourcePortByNode(total, -1);
  std::vector<int> targetPortByNode(total, -1);
  std::vector<double> distance(total, std::numeric_limits<double>::infinity());
  std::vector<std::size_t> previous(total, total);
  using QueueEntry = std::pair<double, std::size_t>;
  std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> pending;

  for (std::size_t portIndex = 0; portIndex < sourcePorts.size(); ++portIndex) {
    const int x = findLaneIndex(xLanes, sourcePorts[portIndex].stub.x);
    const int y = findLaneIndex(yLanes, sourcePorts[portIndex].stub.y);
    if (x < 0 || y < 0) {
      continue;
    }
    const std::size_t index = nodeIndex(static_cast<std::size_t>(x), static_cast<std::size_t>(y));
    blockedPoint[index] = false;
    if (distance[index] <= 0.0) {
      continue;
    }
    distance[index] = 0.0;
    sourcePortByNode[index] = static_cast<int>(portIndex);
    pending.emplace(0.0, index);
  }

  for (std::size_t portIndex = 0; portIndex < targetPorts.size(); ++portIndex) {
    const int x = findLaneIndex(xLanes, targetPorts[portIndex].stub.x);
    const int y = findLaneIndex(yLanes, targetPorts[portIndex].stub.y);
    if (x < 0 || y < 0) {
      continue;
    }
    const std::size_t index = nodeIndex(static_cast<std::size_t>(x), static_cast<std::size_t>(y));
    blockedPoint[index] = false;
    targetPortByNode[index] = static_cast<int>(portIndex);
  }

  std::size_t bestEndNode = total;
  while (!pending.empty()) {
    const auto [cost, current] = pending.top();
    pending.pop();
    if (cost > distance[current] + 0.01) {
      continue;
    }
    if (targetPortByNode[current] >= 0) {
      bestEndNode = current;
      break;
    }

    const std::size_t x = current % width;
    const std::size_t y = current / width;
    const auto visit = [&](std::size_t nextX, std::size_t nextY, bool horizontal) {
      const std::size_t next = nodeIndex(nextX, nextY);
      if (blockedPoint[next]) {
        return;
      }

      const bool blocked = horizontal
        ? intervalIntersectsAnyBlocked(xLanes[x], xLanes[nextX], horizontalBlocked[y])
        : intervalIntersectsAnyBlocked(yLanes[y], yLanes[nextY], verticalBlocked[x]);
      if (blocked) {
        return;
      }

      const double stepCost = horizontal
        ? std::abs(xLanes[x] - xLanes[nextX])
        : std::abs(yLanes[y] - yLanes[nextY]);
      const double occupancyCost = horizontal
        ? occupancyCostForAxisSegment(
            occupancy,
            true,
            metricLaneKey(yLanes[y]),
            xLanes[x],
            xLanes[nextX])
        : occupancyCostForAxisSegment(
            occupancy,
            false,
            metricLaneKey(xLanes[x]),
            yLanes[y],
            yLanes[nextY]);
      const double nextCost = cost + stepCost + occupancyCost;
      if (nextCost + 0.01 >= distance[next]) {
        return;
      }

      distance[next] = nextCost;
      previous[next] = current;
      sourcePortByNode[next] = sourcePortByNode[current];
      pending.emplace(nextCost, next);
    };

    if (x > 0) {
      visit(x - 1, y, true);
    }
    if (x + 1 < width) {
      visit(x + 1, y, true);
    }
    if (y > 0) {
      visit(x, y - 1, false);
    }
    if (y + 1 < height) {
      visit(x, y + 1, false);
    }
  }

  if (bestEndNode == total || sourcePortByNode[bestEndNode] < 0 || targetPortByNode[bestEndNode] < 0) {
    return {};
  }

  std::vector<RoutePoint> reversedPoints;
  for (std::size_t current = bestEndNode; current != total; current = previous[current]) {
    const std::size_t x = current % width;
    const std::size_t y = current / width;
    reversedPoints.push_back({ xLanes[x], yLanes[y] });
    if (previous[current] == total) {
      break;
    }
  }

  std::reverse(reversedPoints.begin(), reversedPoints.end());
  return {
    true,
    compressRoutePoints(std::move(reversedPoints)),
    static_cast<std::size_t>(sourcePortByNode[bestEndNode]),
    static_cast<std::size_t>(targetPortByNode[bestEndNode]),
  };
}

VisibilityPort makeVisibilityPort(
  const Rect& rect,
  const std::string& side,
  double offset,
  double inset,
  double stub) {
  if (side == "left") {
    const double y = clampToSpan(rectCenterY(rect) + offset, rect.top + inset, rect.bottom - inset);
    return { { rect.left, y }, { rect.left - stub, y } };
  }
  if (side == "right") {
    const double y = clampToSpan(rectCenterY(rect) + offset, rect.top + inset, rect.bottom - inset);
    return { { rect.right, y }, { rect.right + stub, y } };
  }
  if (side == "top") {
    const double x = clampToSpan(rectCenterX(rect) + offset, rect.left + inset, rect.right - inset);
    return { { x, rect.top }, { x, rect.top - stub } };
  }

  const double x = clampToSpan(rectCenterX(rect) + offset, rect.left + inset, rect.right - inset);
  return { { x, rect.bottom }, { x, rect.bottom + stub } };
}

std::vector<VisibilityPort> makeVisibilityPorts(
  const Rect& rect,
  double offset,
  double inset,
  double stub) {
  return {
    makeVisibilityPort(rect, "left", offset, inset, stub),
    makeVisibilityPort(rect, "right", offset, inset, stub),
    makeVisibilityPort(rect, "top", offset, inset, stub),
    makeVisibilityPort(rect, "bottom", offset, inset, stub),
  };
}

std::vector<RoutePoint> routeObstacleAwareLine(
  const LineIntent& line,
  const Rect& graphBounds,
  const std::vector<NodeObstacle>& obstacles,
  const RouteOccupancy* occupancy = nullptr) {
  const Rect source = line.sourceRect;
  const Rect target = line.targetRect;
  const bool horizontal = line.prefersHorizontal;
  const double laneOffset = line.laneOffset;
  constexpr double portInset = 18.0;
  constexpr double stub = 52.0;
  constexpr double outerGap = 170.0;
  constexpr double laneGap = kVisibilityLaneClearance;

  RoutePoint start;
  RoutePoint end;
  RoutePoint startStub;
  RoutePoint endStub;
  std::vector<std::vector<RoutePoint>> candidates;
  const std::vector<Rect> obstacleRects = collectObstacleRects(obstacles);

  if (horizontal) {
    const bool leftToRight = rectCenterX(target) >= rectCenterX(source);
    start = {
      leftToRight ? source.right : source.left,
      clampToSpan(rectCenterY(source) + laneOffset, source.top + portInset, source.bottom - portInset),
    };
    end = {
      leftToRight ? target.left : target.right,
      clampToSpan(rectCenterY(target) - laneOffset, target.top + portInset, target.bottom - portInset),
    };
    startStub = { start.x + (leftToRight ? stub : -stub), start.y };
    endStub = { end.x + (leftToRight ? -stub : stub), end.y };
    const double midX = (startStub.x + endStub.x) / 2.0;
    const double topLane = graphBounds.top - outerGap - std::abs(laneOffset);
    const double bottomLane = graphBounds.bottom + outerGap + std::abs(laneOffset);
    candidates.push_back({ start, startStub, { midX, startStub.y }, { midX, endStub.y }, endStub, end });

    std::vector<double> verticalLanes;
    std::vector<double> horizontalLanes;
    const double minX = std::min(startStub.x, endStub.x);
    const double maxX = std::max(startStub.x, endStub.x);
    const double minY = std::min(startStub.y, endStub.y);
    const double maxY = std::max(startStub.y, endStub.y);
    for (const NodeObstacle& obstacle : obstacles) {
      if (obstacle.rect.bottom >= minY - laneGap && obstacle.rect.top <= maxY + laneGap) {
        addLaneValue(verticalLanes, obstacle.rect.left - laneGap);
        addLaneValue(verticalLanes, obstacle.rect.right + laneGap);
      }
      if (obstacle.rect.right >= minX - laneGap && obstacle.rect.left <= maxX + laneGap) {
        addLaneValue(horizontalLanes, obstacle.rect.top - laneGap);
        addLaneValue(horizontalLanes, obstacle.rect.bottom + laneGap);
      }
    }

    for (double lane : nearestUniqueLaneValues(verticalLanes, midX, 8)) {
      candidates.push_back({ start, startStub, { lane, startStub.y }, { lane, endStub.y }, endStub, end });
    }
    for (double lane : nearestUniqueLaneValues(horizontalLanes, (startStub.y + endStub.y) / 2.0, 12)) {
      candidates.push_back({ start, startStub, { startStub.x, lane }, { endStub.x, lane }, endStub, end });
    }

    std::vector<double> sourceLanes = nearestUniqueLaneValues(verticalLanes, startStub.x, 4);
    std::vector<double> targetLanes = nearestUniqueLaneValues(verticalLanes, endStub.x, 4);
    std::vector<double> bridgeLanes = nearestUniqueLaneValues(
      horizontalLanes,
      (startStub.y + endStub.y) / 2.0,
      8);
    ensureLaneValue(sourceLanes, startStub.x);
    ensureLaneValue(targetLanes, endStub.x);
    ensureLaneValue(bridgeLanes, topLane);
    ensureLaneValue(bridgeLanes, bottomLane);
    for (double sourceLane : sourceLanes) {
      for (double targetLane : targetLanes) {
        for (double bridgeLane : bridgeLanes) {
          candidates.push_back({
            start,
            startStub,
            { sourceLane, startStub.y },
            { sourceLane, bridgeLane },
            { targetLane, bridgeLane },
            { targetLane, endStub.y },
            endStub,
            end,
          });
        }
      }
    }

    candidates.push_back({ start, startStub, { startStub.x, topLane }, { endStub.x, topLane }, endStub, end });
    candidates.push_back({ start, startStub, { startStub.x, bottomLane }, { endStub.x, bottomLane }, endStub, end });
  } else {
    const bool topToBottom = rectCenterY(target) >= rectCenterY(source);
    start = {
      clampToSpan(rectCenterX(source) + laneOffset, source.left + portInset, source.right - portInset),
      topToBottom ? source.bottom : source.top,
    };
    end = {
      clampToSpan(rectCenterX(target) - laneOffset, target.left + portInset, target.right - portInset),
      topToBottom ? target.top : target.bottom,
    };
    startStub = { start.x, start.y + (topToBottom ? stub : -stub) };
    endStub = { end.x, end.y + (topToBottom ? -stub : stub) };
    const double midY = (startStub.y + endStub.y) / 2.0;
    const double leftLane = graphBounds.left - outerGap - std::abs(laneOffset);
    const double rightLane = graphBounds.right + outerGap + std::abs(laneOffset);
    candidates.push_back({ start, startStub, { startStub.x, midY }, { endStub.x, midY }, endStub, end });

    std::vector<double> verticalLanes;
    std::vector<double> horizontalLanes;
    const double minX = std::min(startStub.x, endStub.x);
    const double maxX = std::max(startStub.x, endStub.x);
    const double minY = std::min(startStub.y, endStub.y);
    const double maxY = std::max(startStub.y, endStub.y);
    for (const NodeObstacle& obstacle : obstacles) {
      if (obstacle.rect.bottom >= minY - laneGap && obstacle.rect.top <= maxY + laneGap) {
        addLaneValue(verticalLanes, obstacle.rect.left - laneGap);
        addLaneValue(verticalLanes, obstacle.rect.right + laneGap);
      }
      if (obstacle.rect.right >= minX - laneGap && obstacle.rect.left <= maxX + laneGap) {
        addLaneValue(horizontalLanes, obstacle.rect.top - laneGap);
        addLaneValue(horizontalLanes, obstacle.rect.bottom + laneGap);
      }
    }

    for (double lane : nearestUniqueLaneValues(horizontalLanes, midY, 8)) {
      candidates.push_back({ start, startStub, { startStub.x, lane }, { endStub.x, lane }, endStub, end });
    }
    for (double lane : nearestUniqueLaneValues(verticalLanes, (startStub.x + endStub.x) / 2.0, 12)) {
      candidates.push_back({ start, startStub, { lane, startStub.y }, { lane, endStub.y }, endStub, end });
    }

    std::vector<double> sourceLanes = nearestUniqueLaneValues(horizontalLanes, startStub.y, 4);
    std::vector<double> targetLanes = nearestUniqueLaneValues(horizontalLanes, endStub.y, 4);
    std::vector<double> bridgeLanes = nearestUniqueLaneValues(
      verticalLanes,
      (startStub.x + endStub.x) / 2.0,
      8);
    ensureLaneValue(sourceLanes, startStub.y);
    ensureLaneValue(targetLanes, endStub.y);
    ensureLaneValue(bridgeLanes, leftLane);
    ensureLaneValue(bridgeLanes, rightLane);
    for (double sourceLane : sourceLanes) {
      for (double targetLane : targetLanes) {
        for (double bridgeLane : bridgeLanes) {
          candidates.push_back({
            start,
            startStub,
            { startStub.x, sourceLane },
            { bridgeLane, sourceLane },
            { bridgeLane, targetLane },
            { endStub.x, targetLane },
            endStub,
            end,
          });
        }
      }
    }

    candidates.push_back({ start, startStub, { leftLane, startStub.y }, { leftLane, endStub.y }, endStub, end });
    candidates.push_back({ start, startStub, { rightLane, startStub.y }, { rightLane, endStub.y }, endStub, end });
  }

  const std::vector<VisibilityPort> sourcePorts =
    makeVisibilityPorts(source, laneOffset, portInset, stub);
  const std::vector<VisibilityPort> targetPorts =
    makeVisibilityPorts(target, -laneOffset, portInset, stub);
  const VisibilityRoute visibilityRoute =
    routeVisibilityGridWithPorts(sourcePorts, targetPorts, graphBounds, obstacleRects, laneOffset, occupancy);
  if (visibilityRoute.found && visibilityRoute.points.size() >= 2) {
    std::vector<RoutePoint> candidate;
    candidate.reserve(visibilityRoute.points.size() + 2);
    candidate.push_back(sourcePorts[visibilityRoute.sourcePortIndex].point);
    candidate.insert(candidate.end(), visibilityRoute.points.begin(), visibilityRoute.points.end());
    candidate.push_back(targetPorts[visibilityRoute.targetPortIndex].point);
    candidate = compressRoutePoints(std::move(candidate));
    if (
      routeScore(candidate, obstacles) < 1'000'000.0
      && routeOccupancyPenalty(candidate, occupancy) < 0.01) {
      return candidate;
    }
    candidates.push_back(std::move(candidate));
  }

  const double outerLaneSpread = static_cast<double>((line.lineIndex * 17) % 29) * 18.0;
  const double outerTop = graphBounds.top - outerGap - std::abs(laneOffset) - outerLaneSpread;
  const double outerBottom = graphBounds.bottom + outerGap + std::abs(laneOffset) + outerLaneSpread;
  const double outerLeft = graphBounds.left - outerGap - std::abs(laneOffset) - outerLaneSpread;
  const double outerRight = graphBounds.right + outerGap + std::abs(laneOffset) + outerLaneSpread;
  for (const VisibilityPort& sourcePort : sourcePorts) {
    for (const VisibilityPort& targetPort : targetPorts) {
      std::vector<std::vector<RoutePoint>> outerCandidates = {
        {
          sourcePort.point,
          sourcePort.stub,
          { sourcePort.stub.x, outerTop },
          { targetPort.stub.x, outerTop },
          targetPort.stub,
          targetPort.point,
        },
        {
          sourcePort.point,
          sourcePort.stub,
          { sourcePort.stub.x, outerBottom },
          { targetPort.stub.x, outerBottom },
          targetPort.stub,
          targetPort.point,
        },
        {
          sourcePort.point,
          sourcePort.stub,
          { outerLeft, sourcePort.stub.y },
          { outerLeft, targetPort.stub.y },
          targetPort.stub,
          targetPort.point,
        },
        {
          sourcePort.point,
          sourcePort.stub,
          { outerRight, sourcePort.stub.y },
          { outerRight, targetPort.stub.y },
          targetPort.stub,
          targetPort.point,
        },
      };

      for (std::vector<RoutePoint>& candidate : outerCandidates) {
        candidate = compressRoutePoints(std::move(candidate));
        if (
          routeScore(candidate, obstacles) < 1'000'000.0
          && routeOccupancyPenalty(candidate, occupancy) < 0.01) {
          return candidate;
        }
        candidates.push_back(std::move(candidate));
      }
    }
  }

  std::vector<RoutePoint> best;
  double bestScore = std::numeric_limits<double>::infinity();
  for (std::vector<RoutePoint> candidate : candidates) {
    candidate = compressRoutePoints(std::move(candidate));
    const double score = routeScore(candidate, obstacles, occupancy);
    if (score < bestScore) {
      bestScore = score;
      best = std::move(candidate);
    }
  }

  return best;
}

std::string describeLayoutAlgorithm(const std::string& mode) {
  if (mode == "hierarchical") {
    return "SugiyamaLayout + MedianHeuristic";
  }
  if (mode == "hierarchical_barycenter") {
    return "SugiyamaLayout + BarycenterHeuristic";
  }
  if (mode == "hierarchical_sifting") {
    return "SugiyamaLayout + SiftingHeuristic";
  }
  if (mode == "hierarchical_global_sifting") {
    return "SugiyamaLayout + GlobalSifting";
  }
  if (mode == "hierarchical_greedy_insert") {
    return "SugiyamaLayout + GreedyInsertHeuristic";
  }
  if (mode == "hierarchical_greedy_switch") {
    return "SugiyamaLayout + GreedySwitchHeuristic";
  }
  if (mode == "hierarchical_grid_sifting") {
    return "SugiyamaLayout + GridSifting";
  }
  if (mode == "hierarchical_split") {
    return "SugiyamaLayout + SplitHeuristic";
  }
  if (mode == "circular") {
    return "CircularLayout";
  }
  if (mode == "linear") {
    return "LinearLayout";
  }
  if (mode == "clustered" || mode == "fmmm") {
    return "FMMMLayout";
  }
  if (mode == "constrained_force") {
    return "ConstrainedForceDirectedLayout";
  }
  if (mode == "constrained_force_straight") {
    return "ConstrainedForceDirectedLayout + StraightLineRouter";
  }
  if (mode == "fast_multipole") {
    return "FastMultipoleEmbedder";
  }
  if (mode == "fast_multipole_multilevel") {
    return "FastMultipoleMultilevelEmbedder";
  }
  if (mode == "stress_minimization") {
    return "StressMinimization";
  }
  if (mode == "pivot_mds") {
    return "PivotMDS";
  }
  if (mode == "davidson_harel") {
    return "DavidsonHarelLayout";
  }
  if (mode == "planarization") {
    return "PlanarizationLayout";
  }
  if (mode == "planarization_grid") {
    return "PlanarizationGridLayout";
  }
  if (mode == "ortho") {
    return "PlanarizationLayout + OrthoLayout";
  }
  if (mode == "planar_draw") {
    return "PlanarDrawLayout";
  }
  if (mode == "planar_straight") {
    return "PlanarStraightLayout";
  }
  if (mode == "schnyder") {
    return "SchnyderLayout";
  }
  if (mode == "upward_layer_based") {
    return "UpwardPlanarizationLayout + LayerBasedUPRLayout";
  }
  if (mode == "upward_planarization") {
    return "UpwardPlanarizationLayout";
  }
  if (mode == "visibility") {
    return "VisibilityLayout";
  }
  if (mode == "cluster_planarization") {
    return "ClusterPlanarizationLayout";
  }
  if (mode == "cluster_ortho") {
    return "ClusterPlanarizationLayout + ClusterOrthoLayout";
  }
  if (mode == "uml_ortho") {
    return "PlanarizationLayoutUML + OrthoLayoutUML";
  }
  if (mode == "uml_planarization") {
    return "PlanarizationLayoutUML";
  }
  if (mode == "tree") {
    return "TreeLayout";
  }
  if (mode == "radial_tree") {
    return "RadialTreeLayout";
  }

  return mode;
}

LayoutRunMetadata makeLayoutRunMetadata(const std::string& mode) {
  const std::string algorithm = describeLayoutAlgorithm(mode);
  return {
    mode,
    mode == "clustered" ? "fmmm" : mode,
    algorithm,
    algorithm,
    "exact",
    "",
  };
}

LayoutRunMetadata runLayout(
  const std::string& mode,
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  LayoutRunMetadata metadata = makeLayoutRunMetadata(mode);

  if (isSugiyamaMode(mode)) {
    const bool requiresSurrogate =
      mode == "hierarchical_global_sifting"
      || mode == "hierarchical_grid_sifting";
    const bool useSurrogate =
      requiresSurrogate
      || (
        nodes.size() >= kSugiyamaSurrogateNodeThreshold
        && mode != "hierarchical"
        && mode != "hierarchical_barycenter");
    std::string actualRunMode = mode;

    if (useSurrogate) {
      actualRunMode =
        mode == "hierarchical_grid_sifting" || mode == "hierarchical_greedy_switch"
          ? "hierarchical"
          : "hierarchical_barycenter";
      metadata.actualMode = mode;
      metadata.strategy = requiresSurrogate ? "surrogate" : "large_graph_surrogate";

      if (mode == "hierarchical_sifting") {
        metadata.actualAlgorithm =
          "DjangoErdSiftingSurrogate(SugiyamaLayout + BarycenterHeuristic, layerStagger)";
        metadata.strategyReason =
          nodeThresholdReason(
            kSugiyamaSurrogateNodeThreshold,
            "sifting cross minimization uses a bounded barycenter base plus layer staggering");
      } else if (mode == "hierarchical_global_sifting") {
        metadata.actualAlgorithm =
          "DjangoErdGlobalSiftingSurrogate(SugiyamaLayout + BarycenterHeuristic, globalLayerDrift)";
        metadata.strategyReason =
          "GlobalSifting is unstable on ERD-scale cyclic graphs, so ERD mode uses a bounded barycenter base plus global layer drift";
      } else if (mode == "hierarchical_greedy_insert") {
        metadata.actualAlgorithm =
          "DjangoErdGreedyInsertSurrogate(SugiyamaLayout + BarycenterHeuristic, compactInsert)";
        metadata.strategyReason =
          nodeThresholdReason(
            kSugiyamaSurrogateNodeThreshold,
            "greedy insert uses a bounded barycenter base plus compact insertion offsets");
      } else if (mode == "hierarchical_greedy_switch") {
        metadata.actualAlgorithm =
          "DjangoErdGreedySwitchSurrogate(SugiyamaLayout + MedianHeuristic, alternatingSwitch)";
        metadata.strategyReason =
          nodeThresholdReason(
            kSugiyamaSurrogateNodeThreshold,
            "greedy switch uses a bounded median base plus alternating layer switches");
      } else if (mode == "hierarchical_grid_sifting") {
        metadata.actualAlgorithm =
          "DjangoErdGridSiftingSurrogate(SugiyamaLayout + MedianHeuristic, layerGridSnap)";
        metadata.strategyReason =
          "GridSifting is unstable on ERD-scale cyclic graphs, so ERD mode uses a bounded median base plus layer grid snapping";
      } else {
        metadata.actualAlgorithm =
          "DjangoErdSplitHeuristicSurrogate(SugiyamaLayout + BarycenterHeuristic, splitBands)";
        metadata.strategyReason =
          nodeThresholdReason(
            kSugiyamaSurrogateNodeThreshold,
            "split heuristic uses a bounded barycenter base plus split bands");
      }
    } else {
      metadata.actualAlgorithm += "(runs=1)";
      metadata.strategy = "bounded";
      metadata.strategyReason = "Sugiyama runs/fails are capped for interactive layout";
    }

    // DJERD_DISABLE_CLUSTER_FALLBACK=1 — keep pure SugiyamaLayout for
    // hierarchical_barycenter on large graphs. The default ClusteredLayout
    // (FMMM meta-layout) gives nicer-looking placement but takes ~5 min on
    // 1000-node ERDs; the pure Sugiyama path drops that to ~30 s at the
    // cost of more crossings.
    const char* skipClusterFallbackEnv =
      std::getenv("DJERD_DISABLE_CLUSTER_FALLBACK");
    const bool skipClusterFallback =
      skipClusterFallbackEnv != nullptr
      && std::strcmp(skipClusterFallbackEnv, "0") != 0;
    if (!useSurrogate && mode == "hierarchical_barycenter" && !skipClusterFallback) {
      const bool useAppClusters = hasMeaningfulClusters(nodes);
      const bool useStructuralFallback = !useAppClusters && nodes.size() >= 80;
      if (useAppClusters || useStructuralFallback) {
        std::vector<NodeRecord> clusteredNodes = nodes;
        if (useStructuralFallback) {
          std::size_t bccBridgeCount = 0;
          std::size_t bccComponentCount = 0;
          std::size_t bccLargestClusterSize = 0;
          std::vector<std::string> labels =
            assignBiconnectedClusterLabels(nodes, edges, bccBridgeCount, bccComponentCount, bccLargestClusterSize);
          const bool bccDegenerate =
            bccComponentCount < 2
            || bccComponentCount > nodes.size() / 2
            || bccBridgeCount == 0
            || bccLargestClusterSize * 2 > nodes.size();
          if (bccDegenerate) {
            // Hub-excluded BCC: pull dominant hubs out and run BCC on the
            // residual subgraph. Use this when standard BCC is degenerate
            // (typically because a giant biconnected blob exists).
            std::size_t hubCount = 0;
            std::size_t residualBridges = 0;
            std::size_t residualBcc = 0;
            std::vector<std::string> hubLabels = assignHubExcludedBccLabels(
              nodes, edges, hubCount, residualBridges, residualBcc);
            const bool hubMethodWorked =
              hubCount > 0 && residualBridges > 0 && residualBcc >= 4;
            if (hubMethodWorked) {
              labels = std::move(hubLabels);
            } else {
              std::size_t louvL1Count = 0;
              std::size_t louvL2Count = 0;
              std::size_t louvIters = 0;
              std::vector<std::string> louvLabels =
                assignTwoLevelLouvainLabels(nodes, edges, louvL1Count, louvL2Count, louvIters);
              const std::size_t louvCommCount = louvL1Count;
              const bool louvWorked =
                louvCommCount >= 4
                && louvCommCount * 2 < nodes.size();
              if (louvWorked) {
                labels = std::move(louvLabels);
              } else {
                const std::size_t targetClusters =
                  std::max<std::size_t>(8, std::min<std::size_t>(32, nodes.size() / 50));
                labels = assignStructuralClusterLabels(nodes, edges, targetClusters);
              }
            }
          }
          for (std::size_t i = 0; i < clusteredNodes.size(); ++i) {
            clusteredNodes[i].appLabel = labels[i];
          }
        }
        ClusterRunOptions opts;
        opts.innerMode = "sugiyama";
        opts.innerLayerDistance = 60.0;
        opts.innerNodeDistance = 28.0;
        opts.metaMode = "fmmm";
        opts.metaUnitEdgeLength = 80.0;
        opts.interClusterPadding = 20.0;
        std::size_t clusterCount = 0;
        std::size_t interEdges = 0;
        runClusteredByAppLayout(opts, clusteredNodes, edges, attributes, clusterCount, interEdges); for (const auto& cn : clusteredNodes) metadata.clusterByModelId[cn.modelId] = cn.appLabel;
        const std::string clusterSource = useAppClusters ? "appLabel" : "graphStructure";
        metadata.actualAlgorithm =
          "ClusteredLayout(source=" + clusterSource
          + ", inner=SugiyamaLayout + BarycenterHeuristic, meta=FMMM(weighted), clusters="
          + std::to_string(clusterCount) + ", interClusterEdges=" + std::to_string(interEdges) + ")";
        metadata.strategy = "clustered";
        metadata.strategyReason = useAppClusters
          ? "nodes grouped by appLabel; per-app Sugiyama with FMMM weighted meta-layout"
          : "BCC → hub-excluded BCC → Louvain modularity → high-degree-hub BFS fallback chain; FMMM weighted meta-layout";
        return metadata;
      }
    }

    runSugiyamaLayout(actualRunMode, attributes);
    if (useSurrogate) {
      if (mode == "hierarchical_sifting") {
        applySiftingSurrogateGeometry(nodes, edges, attributes);
      } else if (mode == "hierarchical_global_sifting") {
        applyGlobalSiftingSurrogateGeometry(nodes, edges, attributes);
      } else if (mode == "hierarchical_greedy_insert") {
        applyGreedyInsertSurrogateGeometry(nodes, edges, attributes);
      } else if (mode == "hierarchical_greedy_switch") {
        applyGreedySwitchSurrogateGeometry(nodes, edges, attributes);
      } else if (mode == "hierarchical_grid_sifting") {
        applyGridSiftingSurrogateGeometry(nodes, edges, attributes);
      } else {
        applySplitSurrogateGeometry(nodes, edges, attributes);
      }
    }
    return metadata;
  }

  if (mode == "circular") {
    {
      const bool useAppClusters = hasMeaningfulClusters(nodes);
      const bool useStructuralFallback = !useAppClusters && nodes.size() >= 80;
      if (useAppClusters || useStructuralFallback) {
        std::vector<NodeRecord> clusteredNodes = nodes;
        if (useStructuralFallback) {
          std::size_t bccBridgeCount = 0;
          std::size_t bccComponentCount = 0;
          std::size_t bccLargestClusterSize = 0;
          std::vector<std::string> labels =
            assignBiconnectedClusterLabels(nodes, edges, bccBridgeCount, bccComponentCount, bccLargestClusterSize);
          const bool bccDegenerate =
            bccComponentCount < 2
            || bccComponentCount > nodes.size() / 2
            || bccBridgeCount == 0
            || bccLargestClusterSize * 2 > nodes.size();
          if (bccDegenerate) {
            // Hub-excluded BCC: pull dominant hubs out and run BCC on the
            // residual subgraph. Use this when standard BCC is degenerate
            // (typically because a giant biconnected blob exists).
            std::size_t hubCount = 0;
            std::size_t residualBridges = 0;
            std::size_t residualBcc = 0;
            std::vector<std::string> hubLabels = assignHubExcludedBccLabels(
              nodes, edges, hubCount, residualBridges, residualBcc);
            const bool hubMethodWorked =
              hubCount > 0 && residualBridges > 0 && residualBcc >= 4;
            if (hubMethodWorked) {
              labels = std::move(hubLabels);
            } else {
              std::size_t louvL1Count = 0;
              std::size_t louvL2Count = 0;
              std::size_t louvIters = 0;
              std::vector<std::string> louvLabels =
                assignTwoLevelLouvainLabels(nodes, edges, louvL1Count, louvL2Count, louvIters);
              const std::size_t louvCommCount = louvL1Count;
              const bool louvWorked =
                louvCommCount >= 4
                && louvCommCount * 2 < nodes.size();
              if (louvWorked) {
                labels = std::move(louvLabels);
              } else {
                const std::size_t targetClusters =
                  std::max<std::size_t>(8, std::min<std::size_t>(32, nodes.size() / 50));
                labels = assignStructuralClusterLabels(nodes, edges, targetClusters);
              }
            }
          }
          for (std::size_t i = 0; i < clusteredNodes.size(); ++i) {
            clusteredNodes[i].appLabel = labels[i];
          }
        }
        ClusterRunOptions opts;
        opts.innerMode = "circular";
        opts.metaMode = "fmmm";
        opts.metaUnitEdgeLength = 80.0;
        opts.interClusterPadding = 20.0;
        std::size_t clusterCount = 0;
        std::size_t interEdges = 0;
        runClusteredByAppLayout(opts, clusteredNodes, edges, attributes, clusterCount, interEdges); for (const auto& cn : clusteredNodes) metadata.clusterByModelId[cn.modelId] = cn.appLabel;
        const std::string clusterSource = useAppClusters ? "appLabel" : "graphStructure";
        metadata.actualAlgorithm =
          "ClusteredLayout(source=" + clusterSource
          + ", inner=CircularLayout, meta=FMMM, clusters="
          + std::to_string(clusterCount) + ", interClusterEdges=" + std::to_string(interEdges) + ")";
        metadata.strategy = "clustered";
        metadata.strategyReason =
          "per-cluster CircularLayout produces ring-shaped clusters; FMMM meta-layout separates them by structural attraction";
        return metadata;
      }
    }

    ogdf::CircularLayout layout;
    layout.minDistCircle(96.0);
    layout.minDistCC(96.0);
    layout.minDistLevel(96.0);
    layout.minDistSibling(48.0);
    layout.call(attributes);
    return metadata;
  }

  if (mode == "linear") {
    ogdf::LinearLayout layout;
    layout.call(attributes);
    return metadata;
  }

  if (mode == "clustered" || mode == "fmmm") {
    ogdf::FMMMLayout layout;
    layout.useHighLevelOptions(true);
    layout.unitEdgeLength(140.0);
    layout.newInitialPlacement(true);
    layout.qualityVersusSpeed(ogdf::FMMMOptions::QualityVsSpeed::BeautifulAndFast);
    layout.call(attributes);
    metadata.actualMode = "fmmm";
    metadata.actualAlgorithm = "FMMMLayout(BeautifulAndFast, unitEdgeLength=140)";
    return metadata;
  }

  if (isConstrainedForceMode(mode)) {
    ogdf::FMMMLayout seedLayout;
    seedLayout.useHighLevelOptions(true);
    seedLayout.unitEdgeLength(170.0);
    seedLayout.newInitialPlacement(true);
    seedLayout.qualityVersusSpeed(ogdf::FMMMOptions::QualityVsSpeed::BeautifulAndFast);
    seedLayout.call(attributes);

    ogdf::StressMinimization stressLayout;
    stressLayout.hasInitialLayout(true);
    stressLayout.setIterations(90);
    stressLayout.setEdgeCosts(170.0);
    stressLayout.layoutComponentsSeparately(true);
    stressLayout.call(attributes);

    metadata.actualMode = mode;
    metadata.actualAlgorithm = isStraightLineRoutingMode(mode)
      ? "ConstrainedForceDirectedLayout(FMMM seed + StressMinimization, degree-hub axis refinement, straight-line routing)"
      : "ConstrainedForceDirectedLayout(FMMM seed + StressMinimization, constrained post-process)";
    metadata.strategy = "constrained";
    metadata.strategyReason = isStraightLineRoutingMode(mode)
      ? "force-directed layout is refined around high-degree hubs, separated, and rendered with direct straight-line edges"
      : "force-directed layout is refined with node separation, edge-node clearance, and occupancy-aware visibility routing";
    return metadata;
  }

  if (mode == "fast_multipole") {
    const bool useAppClusters = hasMeaningfulClusters(nodes);
    const bool useStructuralFallback = false;
    if (useAppClusters || useStructuralFallback) {
      std::vector<NodeRecord> clusteredNodes = nodes;
      if (useStructuralFallback) {
        std::size_t bccBridgeCount = 0;
        std::size_t bccComponentCount = 0;
        std::size_t bccLargestClusterSize = 0;
        std::vector<std::string> labels =
          assignBiconnectedClusterLabels(nodes, edges, bccBridgeCount, bccComponentCount, bccLargestClusterSize);
        const bool bccDegenerate =
          bccComponentCount < 2
          || bccComponentCount > nodes.size() / 2
          || bccBridgeCount == 0
          || bccLargestClusterSize * 2 > nodes.size();
        if (bccDegenerate) {
          std::size_t hubCount = 0;
          std::size_t residualBridges = 0;
          std::size_t residualBcc = 0;
          std::vector<std::string> hubLabels = assignHubExcludedBccLabels(
            nodes, edges, hubCount, residualBridges, residualBcc);
          const bool hubMethodWorked =
            hubCount > 0 && residualBridges > 0 && residualBcc >= 4;
          if (hubMethodWorked) {
            labels = std::move(hubLabels);
          } else {
            std::size_t louvL1Count = 0;
            std::size_t louvL2Count = 0;
            std::size_t louvIters = 0;
            std::vector<std::string> louvLabels =
              assignTwoLevelLouvainLabels(nodes, edges, louvL1Count, louvL2Count, louvIters);
            const std::size_t louvCommCount = louvL1Count;
            const bool louvWorked =
              louvCommCount >= 4
              && louvCommCount * 2 < nodes.size();
            if (louvWorked) {
              labels = std::move(louvLabels);
            } else {
              const std::size_t targetClusters =
                std::max<std::size_t>(8, std::min<std::size_t>(32, nodes.size() / 50));
              labels = assignStructuralClusterLabels(nodes, edges, targetClusters);
            }
          }
        }
        for (std::size_t i = 0; i < clusteredNodes.size(); ++i) {
          clusteredNodes[i].appLabel = labels[i];
        }
      }
      ClusterRunOptions opts;
      opts.innerMode = "fmm";
      opts.innerFmmEdgeLength = 220.0;
      opts.innerFmmNodeSize = 72.0;
      opts.innerFmmIterations = 300;
      opts.metaUnitEdgeLength = 1500.0;
      opts.interClusterPadding = 320.0;
      std::size_t clusterCount = 0;
      std::size_t interEdges = 0;
      runClusteredByAppLayout(opts, clusteredNodes, edges, attributes, clusterCount, interEdges); for (const auto& cn : clusteredNodes) metadata.clusterByModelId[cn.modelId] = cn.appLabel;
      const std::string clusterSource = useAppClusters ? "appLabel" : "graphStructure";
      metadata.actualAlgorithm =
        "ClusteredLayout(source=" + clusterSource
        + ", inner=FastMultipoleEmbedder, meta=FMMM, clusters="
        + std::to_string(clusterCount) + ", interClusterEdges=" + std::to_string(interEdges) + ")";
      metadata.strategy = "clustered";
      metadata.strategyReason = useAppClusters
        ? "nodes grouped by appLabel; per-app FMM keeps intra-app edges short while FMMM meta-layout separates app clusters"
        : "no useful appLabel split; BCC → hub-excluded BCC → Louvain modularity → high-degree-hub BFS fallback chain; FMMM meta-layout";
      return metadata;
    }
    runFastMultipoleLayout(attributes, 300, 6, true);
    metadata.actualAlgorithm = "FastMultipoleEmbedder(iterations=300, multipolePrecision=6)";
    metadata.strategy = "bounded";
    metadata.strategyReason = "iteration count is capped for interactive layout";
    return metadata;
  }

  if (mode == "fast_multipole_multilevel") {
    if (nodes.size() >= kEnergySurrogateNodeThreshold) {
      runFastMultipoleLayout(attributes, 180, 4, true);
      metadata.actualMode = "fast_multipole_multilevel";
      metadata.actualAlgorithm =
        "DjangoErdFastMultipoleMultilevelSurrogate(FastMultipoleEmbedder, iterations=180, multipolePrecision=4)";
      metadata.strategy = "large_graph_surrogate";
      metadata.strategyReason =
        nodeThresholdReason(
          kEnergySurrogateNodeThreshold,
          "multilevel embedder is replaced with bounded fast multipole");
      return metadata;
    }

    ogdf::FastMultipoleMultilevelEmbedder layout;
    layout.multilevelUntilNumNodesAreLess(kFastMultipoleMultilevelCoarseNodeBound);
    layout.maxNumThreads(static_cast<int>(std::min<std::size_t>(4, idealThreadCount())));
    layout.call(attributes);
    metadata.actualAlgorithm =
      "FastMultipoleMultilevelEmbedder(minCoarseNodes=1024,maxThreads<=4)";
    metadata.strategy = "bounded";
    metadata.strategyReason =
      "coarsening stops earlier because OGDF multilevel iterations grow quadratically by level";
    return metadata;
  }

  if (mode == "stress_minimization") {
    ogdf::StressMinimization layout;
    layout.hasInitialLayout(true);
    layout.setIterations(150);
    layout.setEdgeCosts(140.0);
    layout.layoutComponentsSeparately(true);
    layout.call(attributes);
    metadata.actualAlgorithm = "StressMinimization(initialLayout=true, iterations=150)";
    metadata.strategy = "bounded";
    metadata.strategyReason = "iteration count is capped and analyzer positions seed the layout";
    return metadata;
  }

  if (mode == "pivot_mds") {
    ogdf::PivotMDS layout;
    layout.setNumberOfPivots(std::max(16, std::min(256, static_cast<int>(nodes.size()))));
    layout.setEdgeCosts(140.0);
    layout.setForcing2DLayout(true);
    layout.call(attributes);
    applyPivotMdsGeometry(nodes, edges, attributes);
    metadata.actualAlgorithm = "PivotMDS(pivots<=256, edgeCosts=140, rotatedScale=true)";
    metadata.strategy = "bounded";
    metadata.strategyReason =
      "pivot count is capped and output is normalized apart from stress minimization";
    return metadata;
  }

  if (mode == "davidson_harel") {
    ogdf::DavidsonHarelLayout layout;
    const bool largeGraph = nodes.size() >= kDavidsonHarelReducedNodeThreshold;
    layout.fixSettings(
      largeGraph
        ? ogdf::DavidsonHarelLayout::SettingsParameter::Standard
        : ogdf::DavidsonHarelLayout::SettingsParameter::Planar);
    layout.setNumberOfIterations(largeGraph ? 18 : 120);
    layout.setStartTemperature(largeGraph ? 80 : 240);
    layout.setPreferredEdgeLength(140.0);
    layout.call(attributes);
    metadata.actualAlgorithm = largeGraph
      ? "DavidsonHarelLayout(Standard, iterations=18, startTemperature=80)"
      : "DavidsonHarelLayout(Planar, iterations=120, startTemperature=240)";
    metadata.strategy = largeGraph ? "large_graph_bounded" : "bounded";
    metadata.strategyReason = largeGraph
      ? nodeThresholdReason(
          kDavidsonHarelReducedNodeThreshold,
          "Davidson-Harel iterations and temperature are reduced")
      : "Davidson-Harel iterations are capped";
    return metadata;
  }

  if (mode == "planarization") {
    if (nodes.size() >= kTopologySurrogateNodeThreshold) {
      runSugiyamaLayout("hierarchical_barycenter", attributes);
      applyPlanarSurrogateGeometry(nodes, edges, attributes);
      metadata.actualMode = "planarization";
      metadata.actualAlgorithm =
        "DjangoErdPlanarizationSurrogate(SugiyamaLayout + BarycenterHeuristic, planarSkew)";
      metadata.strategy = "large_graph_surrogate";
      metadata.strategyReason =
        nodeThresholdReason(
          kTopologySurrogateNodeThreshold,
          "planarization uses a bounded Sugiyama base plus planar skewing");
      return metadata;
    }

    ogdf::PlanarizationLayout layout;
    layout.setCrossMin(createBoundedSubgraphPlanarizer());
    layout.pageRatio(kPlanarizationPageRatio);
    layout.call(attributes);
    metadata.actualAlgorithm =
      "PlanarizationLayout(boundedCrossMin=PlanarSubgraphFast(runs=1)+VariableEmbeddingInserter(removeReinsert=None),pageRatio=1.0)";
    metadata.strategy = "bounded";
    metadata.strategyReason =
      "crossing minimization uses one planar subgraph run and fixed embedding insertion for 60s layout";
    return metadata;
  }

  if (mode == "planarization_grid") {
    if (nodes.size() >= kPlanarizationGridSurrogateNodeThreshold) {
      runSugiyamaLayout("hierarchical", attributes);
      applyPlanarGridSurrogateGeometry(nodes, edges, attributes);
      metadata.actualMode = "planarization_grid";
      metadata.actualAlgorithm =
        "DjangoErdPlanarizationGridSurrogate(SugiyamaLayout + MedianHeuristic, gridSnap)";
      metadata.strategy = "large_graph_surrogate";
      metadata.strategyReason =
        nodeThresholdReason(
          kPlanarizationGridSurrogateNodeThreshold,
          "PlanarizationGridLayout is too slow for interactive ERD layout, so ERD mode uses a bounded Sugiyama base snapped to a grid");
      return metadata;
    }

    if (nodes.size() >= kPlanarizationGridProjectionNodeThreshold) {
      ogdf::PlanarizationLayout layout;
      layout.setCrossMin(createBoundedSubgraphPlanarizer());
      layout.pageRatio(kPlanarizationPageRatio);
      layout.call(attributes);
      applyPlanarGridSurrogateGeometry(nodes, edges, attributes);
      metadata.actualMode = "planarization_grid";
      metadata.actualAlgorithm =
        "DjangoErdPlanarizationGridProjection(PlanarizationLayout boundedCrossMin, gridSnap)";
      metadata.strategy = "bounded_projection";
      metadata.strategyReason =
        nodeThresholdReason(
          kPlanarizationGridProjectionNodeThreshold,
          "PlanarizationGridLayout's MixedModel grid layouter exceeded 60s, so ERD mode keeps bounded planarization and projects it onto a grid");
      return metadata;
    }

    ogdf::PlanarizationGridLayout layout;
    layout.setCrossMin(createBoundedSubgraphPlanarizer());
    layout.pageRatio(kPlanarizationPageRatio);
    layout.separation(kPlanarizationGridSeparation);
    layout.call(attributes);
    metadata.actualAlgorithm =
      "PlanarizationGridLayout(boundedCrossMin=PlanarSubgraphFast(runs=1)+VariableEmbeddingInserter(removeReinsert=None),pageRatio=1.25,separation=96)";
    metadata.strategy = "bounded";
    metadata.strategyReason =
      "grid planarization uses bounded crossing minimization and a fixed grid separation for 60s layout";
    return metadata;
  }

  if (mode == "ortho") {
    if (nodes.size() >= kTopologySurrogateNodeThreshold) {
      runSugiyamaLayout("hierarchical", attributes);
      applyOrthogonalSurrogateGeometry(nodes, edges, attributes);
      metadata.actualMode = "ortho";
      metadata.actualAlgorithm =
        "DjangoErdOrthogonalSurrogate(SugiyamaLayout + MedianHeuristic, orthogonalGridRouting)";
      metadata.strategy = "large_graph_surrogate";
      metadata.strategyReason =
        nodeThresholdReason(
          kTopologySurrogateNodeThreshold,
          "orthogonal layout uses a bounded Sugiyama base snapped to orthogonal routing");
      return metadata;
    }

    ogdf::PlanarizationLayout layout;
    layout.setCrossMin(createBoundedSubgraphPlanarizer());
    layout.setPlanarLayouter(new ogdf::OrthoLayout());
    layout.pageRatio(kPlanarizationPageRatio);
    layout.call(attributes);
    metadata.actualAlgorithm =
      "PlanarizationLayout + OrthoLayout(boundedCrossMin=PlanarSubgraphFast(runs=1)+VariableEmbeddingInserter(removeReinsert=None),pageRatio=1.0)";
    metadata.strategy = "bounded";
    metadata.strategyReason =
      "orthogonal planarization uses bounded crossing minimization for 60s layout";
    return metadata;
  }

  if (mode == "planar_draw") {
    runSugiyamaLayout("hierarchical_barycenter", attributes);
    applyStraightLineSurrogateGeometry(nodes, edges, attributes);
    metadata.actualMode = "planar_draw";
    metadata.actualAlgorithm =
      "DjangoErdPlanarDrawSurrogate(SugiyamaLayout + BarycenterHeuristic, straightLinePlanarStyle)";
    metadata.strategy = "surrogate";
    metadata.strategyReason =
      "PlanarDrawLayout requires a planar simple graph; ERD input is normalized through a safe straight-line surrogate";
    return metadata;
  }

  if (mode == "planar_straight") {
    runFastMultipoleLayout(attributes, 160, 4, true);
    applyStraightLineSurrogateGeometry(nodes, edges, attributes);
    metadata.actualMode = "planar_straight";
    metadata.actualAlgorithm =
      "DjangoErdPlanarStraightSurrogate(FastMultipoleEmbedder, straightLineNormalize)";
    metadata.strategy = "surrogate";
    metadata.strategyReason =
      "PlanarStraightLayout requires a planar simple graph; ERD input is normalized through a safe straight-line surrogate";
    return metadata;
  }

  if (mode == "schnyder") {
    ogdf::CircularLayout layout;
    layout.minDistCircle(140.0);
    layout.minDistCC(160.0);
    layout.minDistLevel(120.0);
    layout.minDistSibling(60.0);
    layout.call(attributes);
    applySchnyderSurrogateGeometry(nodes, edges, attributes);
    metadata.actualMode = "schnyder";
    metadata.actualAlgorithm =
      "DjangoErdSchnyderSurrogate(CircularLayout, triangularStraightLineProjection)";
    metadata.strategy = "surrogate";
    metadata.strategyReason =
      "SchnyderLayout requires a planar simple graph; ERD input is projected into a safe triangular straight-line style";
    return metadata;
  }

  if (mode == "upward_layer_based" || mode == "upward_planarization") {
    {
      const bool useAppClusters = hasMeaningfulClusters(nodes);
      const bool useStructuralFallback = !useAppClusters && nodes.size() >= 80;
      if (useAppClusters || useStructuralFallback) {
        std::vector<NodeRecord> clusteredNodes = nodes;
        if (useStructuralFallback) {
          std::size_t bccBridgeCount = 0;
          std::size_t bccComponentCount = 0;
          std::size_t bccLargestClusterSize = 0;
          std::vector<std::string> labels =
            assignBiconnectedClusterLabels(nodes, edges, bccBridgeCount, bccComponentCount, bccLargestClusterSize);
          const bool bccDegenerate =
            bccComponentCount < 2
            || bccComponentCount > nodes.size() / 2
            || bccBridgeCount == 0
            || bccLargestClusterSize * 2 > nodes.size();
          if (bccDegenerate) {
            // Hub-excluded BCC: pull dominant hubs out and run BCC on the
            // residual subgraph. Use this when standard BCC is degenerate
            // (typically because a giant biconnected blob exists).
            std::size_t hubCount = 0;
            std::size_t residualBridges = 0;
            std::size_t residualBcc = 0;
            std::vector<std::string> hubLabels = assignHubExcludedBccLabels(
              nodes, edges, hubCount, residualBridges, residualBcc);
            const bool hubMethodWorked =
              hubCount > 0 && residualBridges > 0 && residualBcc >= 4;
            if (hubMethodWorked) {
              labels = std::move(hubLabels);
            } else {
              std::size_t louvL1Count = 0;
              std::size_t louvL2Count = 0;
              std::size_t louvIters = 0;
              std::vector<std::string> louvLabels =
                assignTwoLevelLouvainLabels(nodes, edges, louvL1Count, louvL2Count, louvIters);
              const std::size_t louvCommCount = louvL1Count;
              const bool louvWorked =
                louvCommCount >= 4
                && louvCommCount * 2 < nodes.size();
              if (louvWorked) {
                labels = std::move(louvLabels);
              } else {
                const std::size_t targetClusters =
                  std::max<std::size_t>(8, std::min<std::size_t>(32, nodes.size() / 50));
                labels = assignStructuralClusterLabels(nodes, edges, targetClusters);
              }
            }
          }
          for (std::size_t i = 0; i < clusteredNodes.size(); ++i) {
            clusteredNodes[i].appLabel = labels[i];
          }
        }
        ClusterRunOptions opts;
        opts.innerMode = "sugiyama";
        opts.innerLayerDistance = 60.0;
        opts.innerNodeDistance = 28.0;
        opts.metaMode = "fmmm";
        opts.metaUnitEdgeLength = 80.0;
        opts.interClusterPadding = 20.0;
        std::size_t clusterCount = 0;
        std::size_t interEdges = 0;
        runClusteredByAppLayout(opts, clusteredNodes, edges, attributes, clusterCount, interEdges); for (const auto& cn : clusteredNodes) metadata.clusterByModelId[cn.modelId] = cn.appLabel;
        applyUpwardSurrogateGeometry(nodes, edges, attributes, mode == "upward_layer_based");
        const std::string clusterSource = useAppClusters ? "appLabel" : "graphStructure";
        metadata.actualMode = mode;
        metadata.actualAlgorithm =
          "ClusteredLayout(source=" + clusterSource
          + ", inner=SugiyamaLayout + BarycenterHeuristic, meta=FMMM(weighted), upwardProjection, clusters="
          + std::to_string(clusterCount) + ", interClusterEdges=" + std::to_string(interEdges) + ")";
        metadata.strategy = "clustered";
        metadata.strategyReason =
          "per-cluster Sugiyama with upward projection; FMMM weighted meta-layout pulls coupled clusters tight";
        return metadata;
      }
    }

    runSugiyamaLayout("hierarchical_barycenter", attributes);
    applyUpwardSurrogateGeometry(nodes, edges, attributes, mode == "upward_layer_based");
    metadata.actualMode = mode;
    metadata.actualAlgorithm = mode == "upward_layer_based"
      ? "DjangoErdLayerBasedUPRSurrogate(SugiyamaLayout + BarycenterHeuristic, upwardProjection)"
      : "DjangoErdUpwardPlanarizationSurrogate(SugiyamaLayout + BarycenterHeuristic, upwardProjection)";
    metadata.strategy = "surrogate";
    metadata.strategyReason =
      "UpwardPlanarizationLayout is unstable on cyclic or disconnected ERD graphs, so ERD mode uses a bounded Sugiyama base with upward projection";
    return metadata;
  }

  if (mode == "visibility") {
    runSugiyamaLayout("hierarchical", attributes);
    applyVisibilitySurrogateGeometry(nodes, edges, attributes);
    metadata.actualMode = "visibility";
    metadata.actualAlgorithm =
      "DjangoErdVisibilitySurrogate(SugiyamaLayout + MedianHeuristic, visibilityGridRouting)";
    metadata.strategy = "surrogate";
    metadata.strategyReason =
      "VisibilityLayout is unstable on cyclic or disconnected ERD graphs, so ERD mode uses a bounded Sugiyama base with grid visibility routing";
    return metadata;
  }

  if (mode == "cluster_planarization" || mode == "cluster_ortho") {
    applyClusterSurrogateGeometry(nodes, edges, attributes, mode == "cluster_ortho");
    metadata.actualMode = mode;
    metadata.actualAlgorithm = mode == "cluster_ortho"
      ? "DjangoErdClusterOrthoSurrogate(app-prefix clusters, orthogonalGridRouting)"
      : "DjangoErdClusterPlanarizationSurrogate(app-prefix clusters, packedClusterLayout)";
    metadata.strategy = "cluster_surrogate";
    metadata.strategyReason =
      "the extension provides ERD app-prefix clusters, while OGDF cluster layouts require an explicit ClusterGraph";
    return metadata;
  }

  if (mode == "uml_ortho") {
    if (nodes.size() >= kTopologySurrogateNodeThreshold) {
      runSugiyamaLayout("hierarchical_barycenter", attributes);
      applyOrthogonalSurrogateGeometry(nodes, edges, attributes);
      metadata.actualMode = "uml_ortho";
      metadata.actualAlgorithm =
        "DjangoErdUmlOrthoSurrogate(SugiyamaLayout + BarycenterHeuristic, umlOrthogonalProjection)";
      metadata.strategy = "large_graph_surrogate";
      metadata.strategyReason =
        nodeThresholdReason(
          kTopologySurrogateNodeThreshold,
          "UML orthogonal layout uses a bounded Sugiyama base snapped to orthogonal routing");
      return metadata;
    }

    ogdf::PlanarizationLayoutUML layout;
    layout.setPlanarLayouter(new ogdf::OrthoLayoutUML());
    layout.call(attributes);
    metadata.actualAlgorithm = "PlanarizationLayoutUML + OrthoLayoutUML";
    return metadata;
  }

  if (mode == "uml_planarization") {
    if (nodes.size() >= kTopologySurrogateNodeThreshold) {
      runSugiyamaLayout("hierarchical_barycenter", attributes);
      applyUmlPlanarSurrogateGeometry(nodes, edges, attributes);
      metadata.actualMode = "uml_planarization";
      metadata.actualAlgorithm =
        "DjangoErdUmlPlanarizationSurrogate(SugiyamaLayout + BarycenterHeuristic, umlPlanarProjection)";
      metadata.strategy = "large_graph_surrogate";
      metadata.strategyReason =
        nodeThresholdReason(
          kTopologySurrogateNodeThreshold,
          "UML planarization uses a bounded Sugiyama base plus planar skewing");
      return metadata;
    }

    ogdf::PlanarizationLayoutUML layout;
    layout.call(attributes);
    return metadata;
  }

  if (mode == "tree" || mode == "radial_tree") {
    runProjectedTreeLayout(mode, nodes, edges, attributes);
    metadata.actualAlgorithm = mode == "radial_tree"
      ? "DjangoErdProjectedRadialForestLayout"
      : "DjangoErdProjectedLayeredForestLayout";
    metadata.strategy = "projected_forest";
    metadata.strategyReason =
      "input graph can be cyclic or disconnected, so a spanning forest is laid out";
    return metadata;
  }

  throw std::runtime_error("unsupported mode: " + mode);
}

void updateBounds(Bounds& bounds, double x, double y, bool& hasPoint) {
  if (!hasPoint) {
    bounds.minX = x;
    bounds.minY = y;
    hasPoint = true;
    return;
  }

  bounds.minX = std::min(bounds.minX, x);
  bounds.minY = std::min(bounds.minY, y);
}

Bounds measureBounds(
  const std::vector<NodeRecord>& nodes,
  const std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes) {
  Bounds bounds;
  bool hasPoint = false;

  for (const NodeRecord& node : nodes) {
    const double width = sanitizeNodeWidth(node, attributes);
    const double height = sanitizeNodeHeight(node, attributes);
    const double centerX = sanitizeNodeCenterX(node, attributes);
    const double centerY = sanitizeNodeCenterY(node, attributes);
    updateBounds(
      bounds,
      centerX - width / 2.0,
      centerY - height / 2.0,
      hasPoint);
  }

  for (const std::vector<RoutePoint>& route : routes) {
    for (const RoutePoint& point : route) {
      updateBounds(bounds, point.x, point.y, hasPoint);
    }
  }

  if (!hasPoint) {
    bounds.minX = 0.0;
    bounds.minY = 0.0;
  }

  return bounds;
}

std::vector<std::vector<RoutePoint>> routeAllEdges(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  bool avoidLaneOverlaps = false) {
  std::vector<std::vector<RoutePoint>> routes;
  routes.reserve(edges.size());
  RouteOccupancy occupancy;
  RouteOccupancy* occupancyPtr = avoidLaneOverlaps ? &occupancy : nullptr;
  const Rect graphBounds = graphNodeBounds(nodes, attributes);
  constexpr double obstacleMargin = kRoutingObstacleMargin;

  for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
    const LineIntent line = makeLineIntent(edges[edgeIndex], edgeIndex, attributes);
    const std::vector<NodeObstacle> obstacles =
      makeNodeObstacles(nodes, attributes, obstacleMargin, line.sourceHandle, line.targetHandle);
    routes.push_back(routeObstacleAwareLine(line, graphBounds, obstacles, occupancyPtr));
    if (occupancyPtr != nullptr) {
      recordRouteOccupancy(routes.back(), line, *occupancyPtr);
    }
  }

  return routes;
}

RoutePoint straightPortOnRect(const Rect& rect, const Rect& target) {
  const double centerX = rectCenterX(rect);
  const double centerY = rectCenterY(rect);
  double dx = rectCenterX(target) - centerX;
  double dy = rectCenterY(target) - centerY;
  if (std::abs(dx) < 0.01 && std::abs(dy) < 0.01) {
    dx = 1.0;
    dy = 0.0;
  }

  const double halfWidth = std::max(1.0, rectWidth(rect) / 2.0);
  const double halfHeight = std::max(1.0, rectHeight(rect) / 2.0);
  const double scaleX = std::abs(dx) < 0.01
    ? std::numeric_limits<double>::infinity()
    : halfWidth / std::abs(dx);
  const double scaleY = std::abs(dy) < 0.01
    ? std::numeric_limits<double>::infinity()
    : halfHeight / std::abs(dy);
  const double scale = std::min(scaleX, scaleY);
  return {
    std::round((centerX + dx * scale) * 100.0) / 100.0,
    std::round((centerY + dy * scale) * 100.0) / 100.0,
  };
}

int sideOfPortOnRect(const RoutePoint& point, const Rect& rect) {
  const double tol =
    std::max(1.0, 0.01 * std::max(rectWidth(rect), rectHeight(rect)));
  const double dTop = std::abs(point.y - rect.top);
  const double dRight = std::abs(point.x - rect.right);
  const double dBottom = std::abs(point.y - rect.bottom);
  const double dLeft = std::abs(point.x - rect.left);
  const double minD = std::min({dTop, dRight, dBottom, dLeft});
  if (minD > tol) {
    return -1;
  }
  if (dTop == minD) return 0;
  if (dRight == minD) return 1;
  if (dBottom == minD) return 2;
  return 3;
}

RoutePoint slidePortOnRectSide(
  const RoutePoint& point,
  const Rect& rect,
  int side) {
  switch (side) {
    case 0:
      return {clampToSpan(point.x, rect.left, rect.right), rect.top};
    case 1:
      return {rect.right, clampToSpan(point.y, rect.top, rect.bottom)};
    case 2:
      return {clampToSpan(point.x, rect.left, rect.right), rect.bottom};
    case 3:
      return {rect.left, clampToSpan(point.y, rect.top, rect.bottom)};
    default: {
      const double cx = rectCenterX(rect);
      const double cy = rectCenterY(rect);
      const double dx = point.x - cx;
      const double dy = point.y - cy;
      if (std::abs(dx) >= std::abs(dy)) {
        return {
          dx < 0.0 ? rect.left : rect.right,
          clampToSpan(point.y, rect.top, rect.bottom),
        };
      }
      return {
        clampToSpan(point.x, rect.left, rect.right),
        dy < 0.0 ? rect.top : rect.bottom,
      };
    }
  }
}

std::vector<RoutePoint> routeStraightLine(const LineIntent& line) {
  return compressRoutePoints({
    straightPortOnRect(line.sourceRect, line.targetRect),
    straightPortOnRect(line.targetRect, line.sourceRect),
  });
}

std::vector<std::vector<RoutePoint>> routeAllEdgesStraight(
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  std::vector<std::vector<RoutePoint>> routes;
  routes.reserve(edges.size());

  for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
    routes.push_back(routeStraightLine(makeLineIntent(edges[edgeIndex], edgeIndex, attributes)));
  }

  // Apply lane offsets to parallel edges so visually overlapping straight
  // segments fan out. Edges sharing the same {source, target} pair (regardless
  // of direction) get evenly spaced perpendicular offsets.
  std::map<std::pair<std::string, std::string>, std::vector<std::size_t>> byPair;
  for (std::size_t i = 0; i < edges.size(); ++i) {
    const std::string& s = edges[i].sourceModelId;
    const std::string& t = edges[i].targetModelId;
    if (s.empty() || t.empty() || s == t) continue;
    auto key = s < t ? std::make_pair(s, t) : std::make_pair(t, s);
    byPair[key].push_back(i);
  }

  constexpr double kLaneSpacing = 12.0;
  for (const auto& [_, indices] : byPair) {
    if (indices.size() < 2) continue;
    const double centerOffset = (static_cast<double>(indices.size()) - 1.0) / 2.0;
    for (std::size_t k = 0; k < indices.size(); ++k) {
      const std::size_t edgeIdx = indices[k];
      auto& route = routes[edgeIdx];
      if (route.size() != 2) continue;
      const double dx = route[1].x - route[0].x;
      const double dy = route[1].y - route[0].y;
      const double len = std::sqrt(dx * dx + dy * dy);
      if (len < 1e-3) continue;
      // Perpendicular unit vector (right-hand rule).
      const double nx = -dy / len;
      const double ny = dx / len;
      const double offset = (static_cast<double>(k) - centerOffset) * kLaneSpacing;
      const EdgeRecord& edge = edges[edgeIdx];
      const Rect sourceRect = handleRect(edge.sourceHandle, attributes);
      const Rect targetRect = handleRect(edge.targetHandle, attributes);
      const int sourceSide = sideOfPortOnRect(route[0], sourceRect);
      const int targetSide = sideOfPortOnRect(route[1], targetRect);
      const RoutePoint sourcePort = slidePortOnRectSide(
        {route[0].x + nx * offset, route[0].y + ny * offset},
        sourceRect,
        sourceSide);
      const RoutePoint targetPort = slidePortOnRectSide(
        {route[1].x + nx * offset, route[1].y + ny * offset},
        targetRect,
        targetSide);
      route[0].x = std::round(sourcePort.x * 100.0) / 100.0;
      route[0].y = std::round(sourcePort.y * 100.0) / 100.0;
      route[1].x = std::round(targetPort.x * 100.0) / 100.0;
      route[1].y = std::round(targetPort.y * 100.0) / 100.0;
    }
  }

  // Obstacle-aware lateral nudge: for any edge whose straight line passes
  // through a non-endpoint node bbox, try a small perpendicular shift to
  // clear the obstacle. Keeps edges 2-point straight while sliding ports
  // along the node boundary, so endpoints stay visually attached.
  struct ObstacleBox {
    double minX, minY, maxX, maxY;
    ogdf::node handle;
  };
  std::vector<ObstacleBox> obstacles;
  const ogdf::Graph& G = attributes.constGraph();
  for (ogdf::node v : G.nodes) {
    const double cx = attributes.x(v);
    const double cy = attributes.y(v);
    const double hw = attributes.width(v) / 2.0;
    const double hh = attributes.height(v) / 2.0;
    obstacles.push_back({cx - hw, cy - hh, cx + hw, cy + hh, v});
  }

  auto segmentsCross = [](double ax, double ay, double bx, double by,
                          double cx, double cy, double dx2, double dy2) -> bool {
    const double d1x = bx - ax, d1y = by - ay;
    const double d2x = dx2 - cx, d2y = dy2 - cy;
    const double denom = d1x * d2y - d1y * d2x;
    if (std::abs(denom) < 1e-9) return false;
    const double t = ((cx - ax) * d2y - (cy - ay) * d2x) / denom;
    const double s = ((cx - ax) * d1y - (cy - ay) * d1x) / denom;
    return t > 1e-9 && t < 1.0 - 1e-9 && s > 1e-9 && s < 1.0 - 1e-9;
  };

  auto lineHitsBox = [&](double sx, double sy, double tx, double ty,
                          const ObstacleBox& b, double margin) -> bool {
    const double bx0 = b.minX - margin;
    const double bx1 = b.maxX + margin;
    const double by0 = b.minY - margin;
    const double by1 = b.maxY + margin;
    if (std::max(sx, tx) < bx0 || std::min(sx, tx) > bx1) return false;
    if (std::max(sy, ty) < by0 || std::min(sy, ty) > by1) return false;
    if (sx > bx0 && sx < bx1 && sy > by0 && sy < by1) return true;
    if (tx > bx0 && tx < bx1 && ty > by0 && ty < by1) return true;
    return segmentsCross(sx, sy, tx, ty, bx0, by0, bx1, by0)
        || segmentsCross(sx, sy, tx, ty, bx1, by0, bx1, by1)
        || segmentsCross(sx, sy, tx, ty, bx1, by1, bx0, by1)
        || segmentsCross(sx, sy, tx, ty, bx0, by1, bx0, by0);
  };

  for (std::size_t i = 0; i < edges.size(); ++i) {
    auto& route = routes[i];
    if (route.size() != 2) continue;
    const ogdf::node srcH = edges[i].sourceHandle;
    const ogdf::node tgtH = edges[i].targetHandle;

    auto countHits = [&](double sx, double sy, double tx, double ty) -> int {
      int hits = 0;
      for (const auto& b : obstacles) {
        if (b.handle == srcH || b.handle == tgtH) continue;
        if (lineHitsBox(sx, sy, tx, ty, b, /*margin=*/0.0)) ++hits;
      }
      return hits;
    };

    const int baseHits = countHits(route[0].x, route[0].y, route[1].x, route[1].y);
    if (baseHits == 0) continue;

    const double dx = route[1].x - route[0].x;
    const double dy = route[1].y - route[0].y;
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-3) continue;
    const double nx = -dy / len;
    const double ny = dx / len;

    const Rect srcRect = handleRect(srcH, attributes);
    const Rect tgtRect = handleRect(tgtH, attributes);
    const int srcSide = sideOfPortOnRect(route[0], srcRect);
    const int tgtSide = sideOfPortOnRect(route[1], tgtRect);

    const double tries[] = {-12, 12, -24, 24, -36, 36};
    int bestHits = baseHits;
    double bestOff = 0.0;
    for (double off : tries) {
      RoutePoint a = slidePortOnRectSide(
        {route[0].x + nx * off, route[0].y + ny * off},
        srcRect,
        srcSide);
      RoutePoint b = slidePortOnRectSide(
        {route[1].x + nx * off, route[1].y + ny * off},
        tgtRect,
        tgtSide);
      const int hits = countHits(a.x, a.y, b.x, b.y);
      if (hits < bestHits) {
        bestHits = hits;
        bestOff = off;
        if (hits == 0) break;
      }
    }
    if (bestOff != 0.0) {
      RoutePoint a = slidePortOnRectSide(
        {route[0].x + nx * bestOff, route[0].y + ny * bestOff},
        srcRect,
        srcSide);
      RoutePoint b = slidePortOnRectSide(
        {route[1].x + nx * bestOff, route[1].y + ny * bestOff},
        tgtRect,
        tgtSide);
      route[0].x = std::round(a.x * 100.0) / 100.0;
      route[0].y = std::round(a.y * 100.0) / 100.0;
      route[1].x = std::round(b.x * 100.0) / 100.0;
      route[1].y = std::round(b.y * 100.0) / 100.0;
    }
  }

  return routes;
}

std::vector<RoutePoint> routeStraightWithDetour(
  const LineIntent& line,
  const std::vector<NodeObstacle>& obstacles,
  int maxDetours) {
  constexpr double kDetourMargin = 24.0;
  const RoutePoint sourcePort = straightPortOnRect(line.sourceRect, line.targetRect);
  const RoutePoint targetPort = straightPortOnRect(line.targetRect, line.sourceRect);
  std::vector<RoutePoint> path = {sourcePort, targetPort};

  for (int iter = 0; iter < maxDetours; ++iter) {
    std::size_t blockedIndex = path.size();
    const NodeObstacle* blocker = nullptr;

    for (std::size_t segIndex = 0; segIndex + 1 < path.size(); ++segIndex) {
      for (const NodeObstacle& obstacle : obstacles) {
        if (segmentIntersectsRect(path[segIndex], path[segIndex + 1], obstacle.rect)) {
          blockedIndex = segIndex;
          blocker = &obstacle;
          break;
        }
      }
      if (blocker != nullptr) {
        break;
      }
    }

    if (blocker == nullptr) {
      break;
    }

    const RoutePoint segStart = path[blockedIndex];
    const RoutePoint segEnd = path[blockedIndex + 1];
    const Rect& rect = blocker->rect;
    const double dx = segEnd.x - segStart.x;
    const double dy = segEnd.y - segStart.y;
    const bool horizontal = std::abs(dx) >= std::abs(dy);

    RoutePoint optA;
    RoutePoint optB;
    if (horizontal) {
      const double midX = std::clamp(
        rectCenterX(rect),
        std::min(segStart.x, segEnd.x),
        std::max(segStart.x, segEnd.x));
      optA = {midX, rect.top - kDetourMargin};
      optB = {midX, rect.bottom + kDetourMargin};
    } else {
      const double midY = std::clamp(
        rectCenterY(rect),
        std::min(segStart.y, segEnd.y),
        std::max(segStart.y, segEnd.y));
      optA = {rect.left - kDetourMargin, midY};
      optB = {rect.right + kDetourMargin, midY};
    }

    const double costA = std::hypot(optA.x - segStart.x, optA.y - segStart.y)
      + std::hypot(segEnd.x - optA.x, segEnd.y - optA.y);
    const double costB = std::hypot(optB.x - segStart.x, optB.y - segStart.y)
      + std::hypot(segEnd.x - optB.x, segEnd.y - optB.y);
    const RoutePoint chosen = (costA <= costB) ? optA : optB;
    path.insert(path.begin() + static_cast<long>(blockedIndex + 1), chosen);
  }

  return compressRoutePoints(std::move(path));
}

std::vector<std::vector<RoutePoint>> routeAllEdgesStraightSmart(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  std::vector<std::vector<RoutePoint>> routes;
  routes.reserve(edges.size());

  constexpr int kMaxDetoursPerEdge = 24;

  for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
    const EdgeRecord& edge = edges[edgeIndex];
    const LineIntent line = makeLineIntent(edge, edgeIndex, attributes);
    const std::vector<NodeObstacle> obstacles = makeNodeObstacles(
      nodes, attributes, 0.0, edge.sourceHandle, edge.targetHandle);
    routes.push_back(routeStraightWithDetour(line, obstacles, kMaxDetoursPerEdge));
  }

  return routes;
}

// Cross-aware routing via per-edge A* on a 2D cell grid.
//
// For each edge, find a path from source-cell to target-cell that minimises
// (distance + density × penalty), where density counts how many already-
// routed edges pass through each cell. Edges are routed in length-desc
// order — long edges (most likely to cross many things) get first pick of
// uncongested corridors. Node-occupied cells are forbidden except the
// edge's own source/target node. Returned polyline is simplified by
// dropping near-collinear waypoints.
std::vector<std::vector<RoutePoint>> routeAllEdgesCrossAware(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  std::vector<std::vector<RoutePoint>> routes(edges.size());

  std::unordered_map<std::string, std::size_t> idToIdx;
  idToIdx.reserve(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    idToIdx[nodes[i].modelId] = i;
  }

  // Layout bounds with padding.
  double minX = std::numeric_limits<double>::infinity();
  double maxX = -std::numeric_limits<double>::infinity();
  double minY = minX;
  double maxY = maxX;
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    const double cx = attributes.x(nodes[i].handle);
    const double cy = attributes.y(nodes[i].handle);
    const double hw = nodes[i].width / 2.0;
    const double hh = nodes[i].height / 2.0;
    if (cx - hw < minX) minX = cx - hw;
    if (cx + hw > maxX) maxX = cx + hw;
    if (cy - hh < minY) minY = cy - hh;
    if (cy + hh > maxY) maxY = cy + hh;
  }
  const double padX = (maxX - minX) * 0.05 + 100.0;
  const double padY = (maxY - minY) * 0.05 + 100.0;
  minX -= padX; maxX += padX;
  minY -= padY; maxY += padY;

  constexpr int GW = 250;
  constexpr int GH = 200;
  const double rangeX = maxX - minX;
  const double rangeY = maxY - minY;
  const double cellW = rangeX > 0 ? rangeX / GW : 1.0;
  const double cellH = rangeY > 0 ? rangeY / GH : 1.0;

  auto worldToCell = [&](double x, double y) {
    int gx = static_cast<int>((x - minX) / cellW);
    int gy = static_cast<int>((y - minY) / cellH);
    if (gx < 0) gx = 0; else if (gx >= GW) gx = GW - 1;
    if (gy < 0) gy = 0; else if (gy >= GH) gy = GH - 1;
    return std::make_pair(gx, gy);
  };
  auto cellToWorld = [&](int gx, int gy) {
    return std::make_pair(minX + (gx + 0.5) * cellW,
                          minY + (gy + 0.5) * cellH);
  };

  // Mark cells inside any node's bbox. Routing into these cells is
  // forbidden unless the edge endpoint is THIS node (allowed near src/tgt).
  std::vector<std::vector<bool>> nodeOccupied(GW, std::vector<bool>(GH, false));
  std::vector<std::vector<int>> ownerNode(GW, std::vector<int>(GH, -1));
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    const double cx = attributes.x(nodes[i].handle);
    const double cy = attributes.y(nodes[i].handle);
    const double hw = nodes[i].width / 2.0;
    const double hh = nodes[i].height / 2.0;
    auto [gx0, gy0] = worldToCell(cx - hw, cy - hh);
    auto [gx1, gy1] = worldToCell(cx + hw, cy + hh);
    for (int x = gx0; x <= gx1; ++x) {
      for (int y = gy0; y <= gy1; ++y) {
        nodeOccupied[x][y] = true;
        ownerNode[x][y] = static_cast<int>(i);
      }
    }
  }

  std::vector<std::vector<int>> density(GW, std::vector<int>(GH, 0));

  // Order edges by length desc.
  std::vector<std::pair<double, std::size_t>> edgeByLen;
  edgeByLen.reserve(edges.size());
  for (std::size_t e = 0; e < edges.size(); ++e) {
    auto sIt = idToIdx.find(edges[e].sourceModelId);
    auto tIt = idToIdx.find(edges[e].targetModelId);
    if (sIt == idToIdx.end() || tIt == idToIdx.end()) {
      edgeByLen.emplace_back(0.0, e);
      continue;
    }
    const std::size_t a = sIt->second;
    const std::size_t b = tIt->second;
    const double dx = attributes.x(nodes[b].handle) - attributes.x(nodes[a].handle);
    const double dy = attributes.y(nodes[b].handle) - attributes.y(nodes[a].handle);
    edgeByLen.emplace_back(std::hypot(dx, dy), e);
  }
  std::sort(edgeByLen.begin(), edgeByLen.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });

  struct OpenEntry {
    double f;
    int gx, gy;
    bool operator<(const OpenEntry& o) const { return f > o.f; }  // min-heap
  };

  auto cellKey = [](int x, int y) { return y * GW + x; };

  std::size_t fallbacks = 0;
  for (const auto& kv : edgeByLen) {
    const std::size_t e = kv.second;
    auto sIt = idToIdx.find(edges[e].sourceModelId);
    auto tIt = idToIdx.find(edges[e].targetModelId);
    if (sIt == idToIdx.end() || tIt == idToIdx.end()
        || sIt->second == tIt->second) {
      routes[e] = {};
      continue;
    }
    const std::size_t srcN = sIt->second;
    const std::size_t tgtN = tIt->second;
    const double sx = attributes.x(nodes[srcN].handle);
    const double sy = attributes.y(nodes[srcN].handle);
    const double tx = attributes.x(nodes[tgtN].handle);
    const double ty = attributes.y(nodes[tgtN].handle);
    auto [sgx, sgy] = worldToCell(sx, sy);
    auto [tgx, tgy] = worldToCell(tx, ty);

    std::priority_queue<OpenEntry> open;
    std::unordered_map<int, double> gScore;
    std::unordered_map<int, int> parent;

    gScore[cellKey(sgx, sgy)] = 0.0;
    open.push({std::hypot(static_cast<double>(tgx - sgx),
                          static_cast<double>(tgy - sgy)), sgx, sgy});

    bool found = false;
    while (!open.empty()) {
      const auto cur = open.top();
      open.pop();
      const int gx = cur.gx;
      const int gy = cur.gy;
      if (gx == tgx && gy == tgy) { found = true; break; }
      const int curKey = cellKey(gx, gy);
      const double curG = gScore[curKey];
      // Check if entry is stale (better path found earlier).
      const double h = std::hypot(static_cast<double>(tgx - gx),
                                   static_cast<double>(tgy - gy));
      if (cur.f > curG + h + 0.001) continue;

      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          if (dx == 0 && dy == 0) continue;
          const int nx = gx + dx;
          const int ny = gy + dy;
          if (nx < 0 || nx >= GW || ny < 0 || ny >= GH) continue;

          // Forbid cells inside non-endpoint nodes.
          if (nodeOccupied[nx][ny]) {
            const int owner = ownerNode[nx][ny];
            if (owner != static_cast<int>(srcN)
                && owner != static_cast<int>(tgtN)) continue;
          }

          const double stepCost = (dx != 0 && dy != 0) ? 1.41421356 : 1.0;
          const double densityCost = static_cast<double>(density[nx][ny]) * 0.5;
          const double tentativeG = curG + stepCost + densityCost;

          const int nKey = cellKey(nx, ny);
          auto gIt = gScore.find(nKey);
          if (gIt == gScore.end() || tentativeG < gIt->second) {
            gScore[nKey] = tentativeG;
            parent[nKey] = curKey;
            const double hN = std::hypot(static_cast<double>(tgx - nx),
                                          static_cast<double>(tgy - ny));
            open.push({tentativeG + hN, nx, ny});
          }
        }
      }
    }

    std::vector<std::pair<int, int>> cellPath;
    if (found) {
      int curKey = cellKey(tgx, tgy);
      const int srcKey = cellKey(sgx, sgy);
      while (curKey != srcKey) {
        const int gx = curKey % GW;
        const int gy = curKey / GW;
        cellPath.emplace_back(gx, gy);
        auto pIt = parent.find(curKey);
        if (pIt == parent.end()) break;
        curKey = pIt->second;
      }
      cellPath.emplace_back(sgx, sgy);
      std::reverse(cellPath.begin(), cellPath.end());
    } else {
      ++fallbacks;
      cellPath = {{sgx, sgy}, {tgx, tgy}};
    }

    // Build polyline: exact src endpoint, intermediate cell centres, exact tgt endpoint.
    std::vector<RoutePoint> route;
    route.push_back({sx, sy});
    for (std::size_t i = 1; i + 1 < cellPath.size(); ++i) {
      const auto [gx, gy] = cellPath[i];
      const auto [wx, wy] = cellToWorld(gx, gy);
      route.push_back({wx, wy});
    }
    route.push_back({tx, ty});

    // Simplify near-collinear waypoints. Remove waypoint b if its
    // perpendicular distance from line a→c is < cellW × 0.4.
    if (route.size() >= 3) {
      std::vector<RoutePoint> simplified;
      simplified.push_back(route.front());
      for (std::size_t i = 1; i + 1 < route.size(); ++i) {
        const auto& a = simplified.back();
        const auto& b = route[i];
        const auto& c = route[i + 1];
        const double crossV = (b.x - a.x) * (c.y - a.y)
                             - (b.y - a.y) * (c.x - a.x);
        const double segLen = std::hypot(c.x - a.x, c.y - a.y);
        const double dist = (segLen > 1e-3)
            ? std::abs(crossV) / segLen : 0.0;
        if (dist > cellW * 0.4) {
          simplified.push_back(b);
        }
      }
      simplified.push_back(route.back());
      route = std::move(simplified);
    }

    routes[e] = route;

    // Update density along path so subsequent edges avoid this corridor.
    for (std::size_t i = 1; i < route.size(); ++i) {
      auto [g0x, g0y] = worldToCell(route[i - 1].x, route[i - 1].y);
      auto [g1x, g1y] = worldToCell(route[i].x, route[i].y);
      int dx = std::abs(g1x - g0x);
      int dy = std::abs(g1y - g0y);
      int sx2 = g0x < g1x ? 1 : -1;
      int sy2 = g0y < g1y ? 1 : -1;
      int err = dx - dy;
      int x = g0x, y = g0y;
      while (true) {
        density[x][y] += 1;
        if (x == g1x && y == g1y) break;
        const int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x += sx2; }
        if (e2 < dx) { err += dx; y += sy2; }
      }
    }
  }

  std::fprintf(stderr,
    "[cross-aware-routing] Routed %zu edges via A* on %dx%d grid (%zu fallbacks).\n",
    edges.size(), GW, GH, fallbacks);

  return routes;
}

int axisForNeighbor(double dx, double dy) {
  if (std::abs(dx) >= std::abs(dy)) {
    return dx >= 0.0 ? 0 : 1;
  }
  return dy >= 0.0 ? 3 : 2;
}

void placeAxisGroup(
  const std::vector<NodeRecord>& nodes,
  const std::vector<std::size_t>& group,
  int axis,
  double hubX,
  double hubY,
  double axisDistance,
  double slotGapX,
  double slotGapY,
  ogdf::GraphAttributes& attributes) {
  if (group.empty()) {
    return;
  }

  const double middle = (static_cast<double>(group.size()) - 1.0) / 2.0;
  for (std::size_t index = 0; index < group.size(); ++index) {
    const NodeRecord& node = nodes[group[index]];
    const double offset = static_cast<double>(index) - middle;

    if (axis == 0) {
      attributes.x(node.handle) = hubX + axisDistance;
      attributes.y(node.handle) = hubY + offset * slotGapY;
    } else if (axis == 1) {
      attributes.x(node.handle) = hubX - axisDistance;
      attributes.y(node.handle) = hubY + offset * slotGapY;
    } else if (axis == 2) {
      attributes.x(node.handle) = hubX + offset * slotGapX;
      attributes.y(node.handle) = hubY - axisDistance;
    } else {
      attributes.x(node.handle) = hubX + offset * slotGapX;
      attributes.y(node.handle) = hubY + axisDistance;
    }
  }
}

void refineStraightHubAxisLayout(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  if (nodes.size() <= 3 || edges.empty()) {
    return;
  }

  const std::vector<std::vector<std::size_t>> adjacency = buildUndirectedAdjacency(nodes, edges);
  std::vector<std::vector<std::size_t>> components = collectConnectedComponents(nodes, edges);

  for (const std::vector<std::size_t>& component : components) {
    if (component.size() <= 3) {
      continue;
    }

    const std::size_t hubIndex = *std::max_element(
      component.begin(),
      component.end(),
      [&](std::size_t left, std::size_t right) {
        if (adjacency[left].size() != adjacency[right].size()) {
          return adjacency[left].size() < adjacency[right].size();
        }
        return nodes[left].modelId > nodes[right].modelId;
      });
    const std::size_t hubDegree = adjacency[hubIndex].size();
    const std::size_t degreeThreshold = std::max<std::size_t>(
      4,
      static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(component.size())))));
    if (hubDegree < degreeThreshold) {
      continue;
    }

    double centerX = 0.0;
    double centerY = 0.0;
    double averageWidth = 0.0;
    double averageHeight = 0.0;
    for (std::size_t nodeIndex : component) {
      centerX += sanitizeNodeCenterX(nodes[nodeIndex], attributes);
      centerY += sanitizeNodeCenterY(nodes[nodeIndex], attributes);
      averageWidth += sanitizeNodeWidth(nodes[nodeIndex], attributes);
      averageHeight += sanitizeNodeHeight(nodes[nodeIndex], attributes);
    }
    centerX /= static_cast<double>(component.size());
    centerY /= static_cast<double>(component.size());
    averageWidth /= static_cast<double>(component.size());
    averageHeight /= static_cast<double>(component.size());

    const NodeRecord& hub = nodes[hubIndex];
    const double hubX = centerX;
    const double hubY = centerY;
    attributes.x(hub.handle) = hubX;
    attributes.y(hub.handle) = hubY;

    std::vector<bool> inComponent(nodes.size(), false);
    for (std::size_t nodeIndex : component) {
      inComponent[nodeIndex] = true;
    }

    std::vector<std::size_t> axisGroups[4];
    for (std::size_t neighbor : adjacency[hubIndex]) {
      if (!inComponent[neighbor]) {
        continue;
      }
      const double dx = sanitizeNodeCenterX(nodes[neighbor], attributes)
        - sanitizeNodeCenterX(hub, attributes);
      const double dy = sanitizeNodeCenterY(nodes[neighbor], attributes)
        - sanitizeNodeCenterY(hub, attributes);
      axisGroups[axisForNeighbor(dx, dy)].push_back(neighbor);
    }

    for (int axis = 0; axis < 4; ++axis) {
      std::sort(
        axisGroups[axis].begin(),
        axisGroups[axis].end(),
        [&](std::size_t left, std::size_t right) {
          const double leftPrimary = axis < 2
            ? sanitizeNodeCenterY(nodes[left], attributes)
            : sanitizeNodeCenterX(nodes[left], attributes);
          const double rightPrimary = axis < 2
            ? sanitizeNodeCenterY(nodes[right], attributes)
            : sanitizeNodeCenterX(nodes[right], attributes);
          if (std::abs(leftPrimary - rightPrimary) > 0.01) {
            return leftPrimary < rightPrimary;
          }
          return adjacency[left].size() > adjacency[right].size();
        });
    }

    const double slotGapX = std::max(averageWidth + 96.0, 180.0);
    const double slotGapY = std::max(averageHeight + 78.0, 150.0);
    const double axisDistance = std::max(
      360.0,
      std::max(sanitizeNodeWidth(hub, attributes), sanitizeNodeHeight(hub, attributes)) + 260.0);
    for (int axis = 0; axis < 4; ++axis) {
      placeAxisGroup(
        nodes,
        axisGroups[axis],
        axis,
        hubX,
        hubY,
        axisDistance,
        slotGapX,
        slotGapY,
        attributes);
    }
  }

  enforceNodeSeparationStrong(nodes, attributes);
}

double capShiftVector(double& dx, double& dy, double limit) {
  const double length = std::hypot(dx, dy);
  if (length <= limit || length <= 0.01) {
    return length;
  }

  const double scale = limit / length;
  dx *= scale;
  dy *= scale;
  return limit;
}

std::size_t applyNodeShifts(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  std::vector<double>& shiftX,
  std::vector<double>& shiftY,
  double limit) {
  std::size_t moved = 0;
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    double dx = shiftX[index];
    double dy = shiftY[index];
    const double length = capShiftVector(dx, dy, limit);
    if (length <= 0.05) {
      continue;
    }
    attributes.x(nodes[index].handle) = sanitizeNodeCenterX(nodes[index], attributes) + dx;
    attributes.y(nodes[index].handle) = sanitizeNodeCenterY(nodes[index], attributes) + dy;
    moved += 1;
  }
  return moved;
}

std::size_t repelNodesFromStraightEdgeCorridors(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  if (nodes.empty() || edges.empty()) {
    return 0;
  }

  std::vector<double> shiftX(nodes.size(), 0.0);
  std::vector<double> shiftY(nodes.size(), 0.0);
  constexpr double corridorClearance = 72.0;

  for (const EdgeRecord& edge : edges) {
    const double sourceX = attributes.x(edge.sourceHandle);
    const double sourceY = attributes.y(edge.sourceHandle);
    const double targetX = attributes.x(edge.targetHandle);
    const double targetY = attributes.y(edge.targetHandle);
    const double edgeX = targetX - sourceX;
    const double edgeY = targetY - sourceY;
    const double lengthSquared = edgeX * edgeX + edgeY * edgeY;
    if (lengthSquared <= 1.0) {
      continue;
    }
    const double length = std::sqrt(lengthSquared);

    for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
      const NodeRecord& node = nodes[nodeIndex];
      if (node.handle == edge.sourceHandle || node.handle == edge.targetHandle) {
        continue;
      }

      const double centerX = sanitizeNodeCenterX(node, attributes);
      const double centerY = sanitizeNodeCenterY(node, attributes);
      const double projection = (
        (centerX - sourceX) * edgeX + (centerY - sourceY) * edgeY) / lengthSquared;
      if (projection <= 0.02 || projection >= 0.98) {
        continue;
      }

      const double closestX = sourceX + edgeX * projection;
      const double closestY = sourceY + edgeY * projection;
      double awayX = centerX - closestX;
      double awayY = centerY - closestY;
      double distance = std::hypot(awayX, awayY);
      if (distance <= 0.01) {
        awayX = -edgeY / length;
        awayY = edgeX / length;
        distance = 1.0;
      } else {
        awayX /= distance;
        awayY /= distance;
      }

      const double nodeRadius =
        std::hypot(sanitizeNodeWidth(node, attributes), sanitizeNodeHeight(node, attributes)) / 2.0;
      const double clearance = nodeRadius + corridorClearance;
      if (distance >= clearance) {
        continue;
      }

      const double strength = std::min(84.0, (clearance - distance) * 0.18);
      shiftX[nodeIndex] += awayX * strength;
      shiftY[nodeIndex] += awayY * strength;
    }
  }

  return applyNodeShifts(nodes, attributes, shiftX, shiftY, 120.0);
}

std::size_t nudgeNodesFromRouteIntersections(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes) {
  if (nodes.empty() || edges.empty()) {
    return 0;
  }

  std::vector<double> shiftX(nodes.size(), 0.0);
  std::vector<double> shiftY(nodes.size(), 0.0);
  constexpr double clearance = 34.0;

  for (std::size_t edgeIndex = 0; edgeIndex < routes.size() && edgeIndex < edges.size(); ++edgeIndex) {
    const EdgeRecord& edge = edges[edgeIndex];
    const std::vector<RoutePoint>& route = routes[edgeIndex];
    for (std::size_t pointIndex = 1; pointIndex < route.size(); ++pointIndex) {
      const RoutePoint& start = route[pointIndex - 1];
      const RoutePoint& end = route[pointIndex];
      const bool vertical = std::abs(start.x - end.x) < 0.01;
      const bool horizontal = std::abs(start.y - end.y) < 0.01;
      if (!vertical && !horizontal) {
        continue;
      }

      for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
        const NodeRecord& node = nodes[nodeIndex];
        if (node.handle == edge.sourceHandle || node.handle == edge.targetHandle) {
          continue;
        }

        if (!segmentIntersectsRect(start, end, nodeRect(node, attributes, clearance))) {
          continue;
        }

        if (vertical) {
          const double centerX = sanitizeNodeCenterX(node, attributes);
          const double halfWidth = sanitizeNodeWidth(node, attributes) / 2.0;
          const double direction = centerX >= start.x ? 1.0 : -1.0;
          const double needed = halfWidth + clearance - std::abs(centerX - start.x);
          shiftX[nodeIndex] += direction * std::min(96.0, std::max(18.0, needed * 0.65));
        } else {
          const double centerY = sanitizeNodeCenterY(node, attributes);
          const double halfHeight = sanitizeNodeHeight(node, attributes) / 2.0;
          const double direction = centerY >= start.y ? 1.0 : -1.0;
          const double needed = halfHeight + clearance - std::abs(centerY - start.y);
          shiftY[nodeIndex] += direction * std::min(96.0, std::max(18.0, needed * 0.65));
        }
      }
    }
  }

  return applyNodeShifts(nodes, attributes, shiftX, shiftY, 140.0);
}

void refineConstrainedForceLayout(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes) {
  for (int pass = 0; pass < 2; ++pass) {
    for (int iteration = 0; iteration < 3; ++iteration) {
      const std::size_t moved = repelNodesFromStraightEdgeCorridors(nodes, edges, attributes);
      enforceNodeSeparationStrong(nodes, attributes);
      if (moved == 0) {
        break;
      }
    }

    const std::vector<std::vector<RoutePoint>> routes =
      routeAllEdges(nodes, edges, attributes, true);
    const LayoutQualityMetrics quality =
      measureLayoutQuality(nodes, edges, routes, attributes, nullptr);
    if (quality.edgeNodeIntersections == 0) {
      break;
    }

    const std::size_t nudged = nudgeNodesFromRouteIntersections(nodes, edges, routes, attributes);
    enforceNodeSeparationStrong(nodes, attributes);
    compactDistantConnectedNodes(nodes, edges, attributes);
    if (nudged == 0) {
      break;
    }
  }
}

double crossProduct(double ax, double ay, double bx, double by) {
  return ax * by - ay * bx;
}

bool sharesEndpoint(const EdgeRecord& left, const EdgeRecord& right) {
  return left.sourceHandle == right.sourceHandle
    || left.sourceHandle == right.targetHandle
    || left.targetHandle == right.sourceHandle
    || left.targetHandle == right.targetHandle;
}

bool properSegmentIntersection(
  const RoutePoint& leftStart,
  const RoutePoint& leftEnd,
  const RoutePoint& rightStart,
  const RoutePoint& rightEnd,
  RoutePoint& intersection) {
  const double rx = leftEnd.x - leftStart.x;
  const double ry = leftEnd.y - leftStart.y;
  const double sx = rightEnd.x - rightStart.x;
  const double sy = rightEnd.y - rightStart.y;
  const double denominator = crossProduct(rx, ry, sx, sy);
  if (std::abs(denominator) < 0.01) {
    return false;
  }

  const double qpx = rightStart.x - leftStart.x;
  const double qpy = rightStart.y - leftStart.y;
  const double t = crossProduct(qpx, qpy, sx, sy) / denominator;
  const double u = crossProduct(qpx, qpy, rx, ry) / denominator;
  constexpr double endpointEpsilon = 0.001;
  if (
    t <= endpointEpsilon
    || t >= 1.0 - endpointEpsilon
    || u <= endpointEpsilon
    || u >= 1.0 - endpointEpsilon) {
    return false;
  }

  intersection = {
    leftStart.x + t * rx,
    leftStart.y + t * ry,
  };
  return true;
}

std::vector<EdgeCrossingRecord> detectRouteCrossings(
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::vector<RoutePoint>>& routes,
  std::vector<std::vector<std::string>>& crossingIdsByEdge,
  std::size_t& totalCrossings) {
  crossingIdsByEdge.assign(edges.size(), {});
  totalCrossings = 0;
  std::vector<EdgeCrossingRecord> crossings;

  for (std::size_t leftIndex = 0; leftIndex < edges.size(); ++leftIndex) {
    if (leftIndex >= routes.size() || routes[leftIndex].size() < 2) {
      continue;
    }

    for (std::size_t rightIndex = leftIndex + 1; rightIndex < edges.size(); ++rightIndex) {
      if (
        rightIndex >= routes.size()
        || routes[rightIndex].size() < 2
        || sharesEndpoint(edges[leftIndex], edges[rightIndex])) {
        continue;
      }

      std::size_t pairCrossingIndex = 0;
      for (std::size_t leftPoint = 1; leftPoint < routes[leftIndex].size(); ++leftPoint) {
        for (std::size_t rightPoint = 1; rightPoint < routes[rightIndex].size(); ++rightPoint) {
          RoutePoint intersection;
          if (!properSegmentIntersection(
              routes[leftIndex][leftPoint - 1],
              routes[leftIndex][leftPoint],
              routes[rightIndex][rightPoint - 1],
              routes[rightIndex][rightPoint],
              intersection)) {
            continue;
          }

          const std::size_t crossingIndex = pairCrossingIndex++;
          totalCrossings += 1;
          if (crossings.size() >= kMaxReportedCrossings) {
            continue;
          }

          const std::string crossingId =
            "cross:" + edges[leftIndex].edgeId + ":" + edges[rightIndex].edgeId + ":"
            + std::to_string(crossingIndex);
          crossingIdsByEdge[leftIndex].push_back(crossingId);
          crossingIdsByEdge[rightIndex].push_back(crossingId);
          crossings.push_back({
            crossingId,
            edges[leftIndex].edgeId,
            intersection,
            edges[rightIndex].edgeId,
          });
        }
      }
    }
  }

  for (std::vector<std::string>& edgeCrossingIds : crossingIdsByEdge) {
    std::sort(edgeCrossingIds.begin(), edgeCrossingIds.end());
  }
  std::sort(
    crossings.begin(),
    crossings.end(),
    [](const EdgeCrossingRecord& left, const EdgeCrossingRecord& right) {
      return left.id < right.id;
    });

  return crossings;
}

bool applyRenderedCarrierMetricsIfRequested(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes,
  const std::unordered_map<std::string, std::string>& clusterByModelIdFull,
  LayoutRunMetadata& metadata,
  LayoutQualityMetrics& quality,
  std::size_t totalRouteCrossings,
  bool quiet = false) {
  const char* renderedCarrierMetricsEnv =
    std::getenv("DJERD_RENDERED_CARRIER_METRICS_FINAL");
  const bool renderedCarrierMetrics =
    renderedCarrierMetricsEnv && std::strcmp(renderedCarrierMetricsEnv, "0") != 0;
  if (!renderedCarrierMetrics || metadata.leafBundles.empty()) {
    return false;
  }

  const char* skipCarrierEnv = std::getenv("DJERD_NO_CARRIER_CROSS");
  const bool skipCarrier =
    skipCarrierEnv && std::strcmp(skipCarrierEnv, "0") != 0;
  if (skipCarrier) {
    return false;
  }

  std::unordered_map<std::string, std::size_t> leafToBundleIdx;
  for (std::size_t bi = 0; bi < metadata.leafBundles.size(); ++bi) {
    for (const std::string& leaf : metadata.leafBundles[bi].leafModelIds) {
      leafToBundleIdx[leaf] = bi;
    }
  }

  std::unordered_map<std::string, const NodeRecord*> nodeByModelId;
  nodeByModelId.reserve(nodes.size());
  for (const NodeRecord& node : nodes) {
    nodeByModelId[node.modelId] = &node;
  }

  std::unordered_map<std::string, std::pair<double, double>> sumByCluster;
  std::unordered_map<std::string, std::size_t> cntByCluster;
  for (const auto& kv : clusterByModelIdFull) {
    auto nodeIt = nodeByModelId.find(kv.first);
    if (nodeIt == nodeByModelId.end()) continue;
    sumByCluster[kv.second].first += attributes.x(nodeIt->second->handle);
    sumByCluster[kv.second].second += attributes.y(nodeIt->second->handle);
    cntByCluster[kv.second] += 1;
  }
  std::unordered_map<std::string, std::pair<double, double>> clusterCentroids;
  for (const auto& kv : sumByCluster) {
    const std::size_t count = cntByCluster[kv.first];
    if (count == 0) continue;
    clusterCentroids[kv.first] = {
      kv.second.first / static_cast<double>(count),
      kv.second.second / static_cast<double>(count),
    };
  }
  auto nearestCluster = [&](const std::string& modelId) {
    auto nodeIt = nodeByModelId.find(modelId);
    if (nodeIt == nodeByModelId.end()) return std::string{};
    const double mx = attributes.x(nodeIt->second->handle);
    const double my = attributes.y(nodeIt->second->handle);
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

  std::vector<std::string> renderedCarrierIdByEdge(edges.size());
  std::vector<bool> renderedEdgeVisible(edges.size(), true);
  std::vector<int> renderedBundleIndexByEdge(edges.size(), -1);
  std::vector<std::string> renderedBundleRootByEdge(edges.size());
  std::vector<std::pair<std::string, std::string>> carrierClustersByEdge(edges.size());

  for (std::size_t e = 0; e < edges.size(); ++e) {
    const std::string& source = edges[e].sourceModelId;
    const std::string& target = edges[e].targetModelId;
    const bool inheritance = edges[e].kind == "inheritance";
    auto sourceBundleIt = leafToBundleIdx.find(source);
    auto targetBundleIt = leafToBundleIdx.find(target);
    if (sourceBundleIt != leafToBundleIdx.end()) {
      const auto& bundle = metadata.leafBundles[sourceBundleIt->second];
      const auto& roots = bundle.sharedRootModelIds.empty()
        ? std::vector<std::string>{bundle.parentModelId}
        : bundle.sharedRootModelIds;
      if (std::find(roots.begin(), roots.end(), target) != roots.end()) {
        renderedCarrierIdByEdge[e] =
          "B" + std::to_string(sourceBundleIt->second) + "|" + target;
        renderedBundleIndexByEdge[e] = static_cast<int>(sourceBundleIt->second);
        renderedBundleRootByEdge[e] = target;
        continue;
      }
    }
    if (targetBundleIt != leafToBundleIdx.end()) {
      const auto& bundle = metadata.leafBundles[targetBundleIt->second];
      const auto& roots = bundle.sharedRootModelIds.empty()
        ? std::vector<std::string>{bundle.parentModelId}
        : bundle.sharedRootModelIds;
      if (std::find(roots.begin(), roots.end(), source) != roots.end()) {
        renderedCarrierIdByEdge[e] =
          "B" + std::to_string(targetBundleIt->second) + "|" + source;
        renderedBundleIndexByEdge[e] = static_cast<int>(targetBundleIt->second);
        renderedBundleRootByEdge[e] = source;
        continue;
      }
    }
    if (sourceBundleIt != leafToBundleIdx.end() || targetBundleIt != leafToBundleIdx.end()) {
      // The webview keeps a bundled leaf's non-carrier inheritance edge as an
      // individual structural line. Other incidental bundled-leaf relations
      // remain hidden. Neither kind participates in hub-carrier incidence.
      if (inheritance) {
        renderedCarrierIdByEdge[e] = edges[e].edgeId;
      } else {
        renderedEdgeVisible[e] = false;
      }
      continue;
    }

    // Inheritance is structurally significant in the webview and is never
    // folded into a cluster hub carrier.
    if (inheritance) {
      renderedCarrierIdByEdge[e] = edges[e].edgeId;
      continue;
    }

    auto sourceClusterIt = clusterByModelIdFull.find(source);
    auto targetClusterIt = clusterByModelIdFull.find(target);
    std::string sourceCluster = sourceClusterIt != clusterByModelIdFull.end()
      ? sourceClusterIt->second
      : std::string{};
    std::string targetCluster = targetClusterIt != clusterByModelIdFull.end()
      ? targetClusterIt->second
      : std::string{};
    if (sourceCluster.empty()) sourceCluster = nearestCluster(source);
    if (targetCluster.empty()) targetCluster = nearestCluster(target);
    if (!sourceCluster.empty() && !targetCluster.empty()) {
      carrierClustersByEdge[e] = {sourceCluster, targetCluster};
    }
    renderedCarrierIdByEdge[e] = edges[e].edgeId;
  }

  const char* hubCarrierEnv = std::getenv("DJERD_HUB_CARRIER_CROSS_FINAL");
  const bool hubCarrier =
    hubCarrierEnv && std::strcmp(hubCarrierEnv, "0") != 0;
  if (hubCarrier) {
    const char* thresholdEnv =
      std::getenv("DJERD_HUB_CARRIER_CROSS_FINAL_THRESHOLD");
    const int threshold = thresholdEnv
      ? std::max(2, std::atoi(thresholdEnv))
      : 16;
    std::unordered_map<std::string, int> incidentCarrierCount;
    for (const auto& [leftCluster, rightCluster] : carrierClustersByEdge) {
      if (leftCluster.empty() || rightCluster.empty() || leftCluster == rightCluster) {
        continue;
      }
      incidentCarrierCount[leftCluster] += 1;
      incidentCarrierCount[rightCluster] += 1;
    }
    std::size_t hubEdges = 0;
    std::unordered_set<std::string> hubClusters;
    for (std::size_t e = 0; e < carrierClustersByEdge.size(); ++e) {
      const auto& [leftCluster, rightCluster] = carrierClustersByEdge[e];
      if (leftCluster.empty() || rightCluster.empty() || leftCluster == rightCluster) {
        continue;
      }
      const int leftCount = incidentCarrierCount[leftCluster];
      const int rightCount = incidentCarrierCount[rightCluster];
      if (leftCount < threshold && rightCount < threshold) {
        continue;
      }
      const std::string& hub =
        (leftCount > rightCount || (leftCount == rightCount && leftCluster < rightCluster))
          ? leftCluster
          : rightCluster;
      renderedCarrierIdByEdge[e] = "H|" + hub;
      hubClusters.insert(hub);
      ++hubEdges;
    }
    metadata.hubCarrierThreshold = threshold;
    metadata.hubCarrierEdgesGrouped = hubEdges;
    metadata.hubCarrierClusters = hubClusters.size();
  }

  const char* occMarginEnv = std::getenv("DJERD_CARRIER_CROSS_OCCLUSION_MARGIN");
  const double occMargin = occMarginEnv
    ? std::max(0.0, std::atof(occMarginEnv))
    : 0.0;
  std::unordered_set<std::string> bundleAbsorbed;
  for (const LeafBundleRecord& bundle : metadata.leafBundles) {
    bundleAbsorbed.insert(bundle.parentModelId);
    for (const std::string& leaf : bundle.leafModelIds) {
      bundleAbsorbed.insert(leaf);
    }
  }
  std::vector<Rect> occlusionRects;
  occlusionRects.reserve(nodes.size() + metadata.leafBundles.size());
  for (const NodeRecord& node : nodes) {
    if (bundleAbsorbed.count(node.modelId)) continue;
    occlusionRects.push_back(nodeRect(node, attributes, occMargin));
  }
  for (const LeafBundleRecord& bundle : metadata.leafBundles) {
    occlusionRects.push_back(renderedLeafBundleRect(bundle, occMargin));
  }
  auto pointInOcclusion = [&](const RoutePoint& point) {
    if (occMargin <= 0.0) return false;
    for (const Rect& rect : occlusionRects) {
      if (
          point.x >= rect.left && point.x <= rect.right
          && point.y >= rect.top && point.y <= rect.bottom) {
        return true;
      }
    }
    return false;
  };

  struct RenderedCarrierMetricPath {
    std::string id;
    std::vector<RoutePoint> points;
    std::unordered_set<std::string> endpointModelIds;
    int bundleIndex = -1;
  };

  auto startsWith = [](const std::string& value, const char* prefix) {
    return value.rfind(prefix, 0) == 0;
  };
  auto rectCenterPoint = [](const Rect& rect) {
    return RoutePoint{
      (rect.left + rect.right) / 2.0,
      (rect.top + rect.bottom) / 2.0,
    };
  };
  auto boundaryPort = [&](const Rect& rect, const RoutePoint& toward) {
    const RoutePoint center = rectCenterPoint(rect);
    const double dx = toward.x - center.x;
    const double dy = toward.y - center.y;
    if (std::abs(dx) < 0.01 && std::abs(dy) < 0.01) {
      return center;
    }
    double scale = std::numeric_limits<double>::infinity();
    if (std::abs(dx) >= 0.01) {
      const double sx = dx > 0.0
        ? (rect.right - center.x) / dx
        : (rect.left - center.x) / dx;
      if (sx > 0.0) scale = std::min(scale, sx);
    }
    if (std::abs(dy) >= 0.01) {
      const double sy = dy > 0.0
        ? (rect.bottom - center.y) / dy
        : (rect.top - center.y) / dy;
      if (sy > 0.0) scale = std::min(scale, sy);
    }
    if (!std::isfinite(scale)) {
      scale = 0.0;
    }
    return RoutePoint{
      std::round((center.x + dx * scale) * 100.0) / 100.0,
      std::round((center.y + dy * scale) * 100.0) / 100.0,
    };
  };

  std::unordered_map<std::string, std::vector<std::size_t>> membersByCarrier;
  membersByCarrier.reserve(edges.size());
  for (std::size_t e = 0; e < edges.size(); ++e) {
    if (!renderedEdgeVisible[e]) continue;
    if (e >= routes.size() || routes[e].size() < 2) continue;
    if (renderedCarrierIdByEdge[e].empty()) {
      renderedCarrierIdByEdge[e] = edges[e].edgeId;
    }
    membersByCarrier[renderedCarrierIdByEdge[e]].push_back(e);
  }

  std::vector<RenderedCarrierMetricPath> renderedPaths;
  renderedPaths.reserve(membersByCarrier.size());
  auto addRawPath = [&](std::size_t edgeIndex) {
    if (edgeIndex >= routes.size() || routes[edgeIndex].size() < 2) return;
    RenderedCarrierMetricPath path;
    path.id = edges[edgeIndex].edgeId;
    path.points = routes[edgeIndex];
    path.endpointModelIds.insert(edges[edgeIndex].sourceModelId);
    path.endpointModelIds.insert(edges[edgeIndex].targetModelId);
    renderedPaths.push_back(std::move(path));
  };

  for (const auto& kv : membersByCarrier) {
    const std::string& carrierId = kv.first;
    const std::vector<std::size_t>& members = kv.second;
    if (members.empty()) continue;

    if (startsWith(carrierId, "B")) {
      const std::size_t firstEdge = members.front();
      const int bundleIndex = renderedBundleIndexByEdge[firstEdge];
      const std::string& rootModelId = renderedBundleRootByEdge[firstEdge];
      auto rootIt = nodeByModelId.find(rootModelId);
      if (
          bundleIndex < 0
          || static_cast<std::size_t>(bundleIndex) >= metadata.leafBundles.size()
          || rootIt == nodeByModelId.end()) {
        for (const std::size_t edgeIndex : members) {
          addRawPath(edgeIndex);
        }
        continue;
      }
      const Rect bundleRect =
        renderedLeafBundleRect(metadata.leafBundles[bundleIndex], 0.0);
      const Rect rootRect = nodeRect(*rootIt->second, attributes, 0.0);
      const RoutePoint bundleCenter = rectCenterPoint(bundleRect);
      const RoutePoint rootCenter = rectCenterPoint(rootRect);

      RenderedCarrierMetricPath path;
      path.id = carrierId;
      path.bundleIndex = bundleIndex;
      path.points = {
        boundaryPort(bundleRect, rootCenter),
        boundaryPort(rootRect, bundleCenter),
      };
      path.endpointModelIds.insert(rootModelId);
      path.endpointModelIds.insert(metadata.leafBundles[bundleIndex].parentModelId);
      for (const std::string& leaf : metadata.leafBundles[bundleIndex].leafModelIds) {
        path.endpointModelIds.insert(leaf);
      }
      renderedPaths.push_back(std::move(path));
      continue;
    }

    if (startsWith(carrierId, "H|") && members.size() >= 2) {
      double startX = 0.0;
      double startY = 0.0;
      double endX = 0.0;
      double endY = 0.0;
      for (const std::size_t edgeIndex : members) {
        const auto& route = routes[edgeIndex];
        startX += route.front().x;
        startY += route.front().y;
        endX += route.back().x;
        endY += route.back().y;
      }
      const double count = static_cast<double>(members.size());
      RenderedCarrierMetricPath path;
      path.id = carrierId;
      path.points = {
        RoutePoint{
          std::round((startX / count) * 100.0) / 100.0,
          std::round((startY / count) * 100.0) / 100.0,
        },
        RoutePoint{
          std::round((endX / count) * 100.0) / 100.0,
          std::round((endY / count) * 100.0) / 100.0,
        },
      };
      for (const std::size_t edgeIndex : members) {
        path.endpointModelIds.insert(edges[edgeIndex].sourceModelId);
        path.endpointModelIds.insert(edges[edgeIndex].targetModelId);
      }
      renderedPaths.push_back(std::move(path));
      continue;
    }

    for (const std::size_t edgeIndex : members) {
      addRawPath(edgeIndex);
    }
  }

  auto pathsShareEndpoint = [](
      const RenderedCarrierMetricPath& left,
      const RenderedCarrierMetricPath& right) {
    if (left.bundleIndex >= 0 && left.bundleIndex == right.bundleIndex) {
      return true;
    }
    const auto& smaller =
      left.endpointModelIds.size() < right.endpointModelIds.size()
        ? left.endpointModelIds
        : right.endpointModelIds;
    const auto& larger =
      left.endpointModelIds.size() < right.endpointModelIds.size()
        ? right.endpointModelIds
        : left.endpointModelIds;
    for (const std::string& id : smaller) {
      if (larger.count(id)) return true;
    }
    return false;
  };

  std::size_t renderedEdgeCrossings = 0;
  std::size_t renderedOccludedCrossings = 0;
  for (std::size_t i = 0; i < renderedPaths.size(); ++i) {
    if (renderedPaths[i].points.size() < 2) continue;
    for (std::size_t j = i + 1; j < renderedPaths.size(); ++j) {
      if (renderedPaths[j].points.size() < 2) continue;
      if (pathsShareEndpoint(renderedPaths[i], renderedPaths[j])) continue;
      bool anyCross = false;
      for (std::size_t li = 1; li < renderedPaths[i].points.size() && !anyCross; ++li) {
        for (std::size_t rj = 1; rj < renderedPaths[j].points.size() && !anyCross; ++rj) {
          RoutePoint isect;
          if (properSegmentIntersection(
              renderedPaths[i].points[li - 1], renderedPaths[i].points[li],
              renderedPaths[j].points[rj - 1], renderedPaths[j].points[rj], isect)) {
            if (pointInOcclusion(isect)) {
              ++renderedOccludedCrossings;
            } else {
              anyCross = true;
            }
          }
        }
      }
      if (anyCross) {
        ++renderedEdgeCrossings;
      }
    }
  }

  constexpr double kRenderedCarrierVisualMargin = 8.0;
  std::vector<Rect> bundleRects;
  bundleRects.reserve(metadata.leafBundles.size());
  for (const LeafBundleRecord& bundle : metadata.leafBundles) {
    bundleRects.push_back(renderedLeafBundleRect(bundle, kRenderedCarrierVisualMargin));
  }

  std::size_t renderedEdgeNodeIntersections = 0;
  std::size_t renderedBundleEdgeIntersections = 0;
  std::size_t renderedRouteSegments = 0;
  for (const RenderedCarrierMetricPath& path : renderedPaths) {
    if (path.points.size() < 2) continue;
    for (std::size_t pointIndex = 1; pointIndex < path.points.size(); ++pointIndex) {
      const RoutePoint& start = path.points[pointIndex - 1];
      const RoutePoint& end = path.points[pointIndex];
      ++renderedRouteSegments;

      for (const NodeRecord& node : nodes) {
        if (bundleAbsorbed.count(node.modelId)) continue;
        if (path.endpointModelIds.count(node.modelId)) continue;
        if (segmentIntersectsRect(
            start, end, nodeRect(node, attributes, kRenderedCarrierVisualMargin))) {
          ++renderedEdgeNodeIntersections;
        }
      }

      for (std::size_t bi = 0; bi < metadata.leafBundles.size(); ++bi) {
        if (path.bundleIndex == static_cast<int>(bi)) continue;
        const LeafBundleRecord& bundle = metadata.leafBundles[bi];
        if (path.endpointModelIds.count(bundle.parentModelId)) continue;
        bool touchesBundleMember = false;
        for (const std::string& leaf : bundle.leafModelIds) {
          if (path.endpointModelIds.count(leaf)) {
            touchesBundleMember = true;
            break;
          }
        }
        if (touchesBundleMember) continue;
        if (segmentIntersectsRect(start, end, bundleRects[bi])) {
          ++renderedBundleEdgeIntersections;
        }
      }
    }
  }

  if (!quiet) {
    std::fprintf(stderr,
      "[rendered-carrier-metrics-final] rawCross=%zu visibleEdges=%zu "
      "edgeCross=%zu edgeNode=%zu bundleEdge=%zu routeSegments=%zu "
      "(occluded=%zu).\n",
      totalRouteCrossings,
      renderedPaths.size(),
      renderedEdgeCrossings,
      renderedEdgeNodeIntersections,
      renderedBundleEdgeIntersections,
      renderedRouteSegments,
      renderedOccludedCrossings);
  }

  quality.edgeCrossings = renderedEdgeCrossings;
  quality.edgeNodeIntersections = renderedEdgeNodeIntersections;
  quality.bundleEdgeIntersections = renderedBundleEdgeIntersections;
  quality.routeSegments = renderedRouteSegments;
  quality.visualCrossings =
    quality.edgeCrossings
    + quality.edgeNodeIntersections
    + quality.nodeOverlaps
    + quality.bundleEdgeIntersections
    + quality.bundleNodeOverlaps;
  return true;
}

bool applyFinalCarrierMetricsIfRequested(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes,
  const std::unordered_map<std::string, std::string>& clusterByModelIdFull,
  LayoutRunMetadata& metadata,
  LayoutQualityMetrics& quality,
  std::size_t totalRouteCrossings,
  bool quiet = false) {
  const char* skipCarrierEnv = std::getenv("DJERD_NO_CARRIER_CROSS");
  const bool skipCarrier =
    skipCarrierEnv && std::strcmp(skipCarrierEnv, "0") != 0;
  if (skipCarrier || metadata.leafBundles.empty()) {
    return false;
  }

  std::unordered_map<std::string, std::size_t> leafToBundleIdx;
  for (std::size_t bi = 0; bi < metadata.leafBundles.size(); ++bi) {
    for (const std::string& leaf : metadata.leafBundles[bi].leafModelIds) {
      leafToBundleIdx[leaf] = bi;
    }
  }

  std::unordered_map<std::string, std::size_t> nodeIndexByModelId;
  nodeIndexByModelId.reserve(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    nodeIndexByModelId[nodes[i].modelId] = i;
  }

  std::unordered_map<std::string, std::pair<double, double>> sumByCluster;
  std::unordered_map<std::string, std::size_t> cntByCluster;
  for (const auto& kv : clusterByModelIdFull) {
    auto idIt = nodeIndexByModelId.find(kv.first);
    if (idIt == nodeIndexByModelId.end()) continue;
    const NodeRecord& node = nodes[idIt->second];
    sumByCluster[kv.second].first += attributes.x(node.handle);
    sumByCluster[kv.second].second += attributes.y(node.handle);
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
  auto nearestClusterFinal = [&](const std::string& mid) {
    auto idIt = nodeIndexByModelId.find(mid);
    if (idIt == nodeIndexByModelId.end()) return std::string{};
    const NodeRecord& node = nodes[idIt->second];
    const double mx = attributes.x(node.handle);
    const double my = attributes.y(node.handle);
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

  std::vector<std::string> carrierIdByEdge(edges.size());
  std::vector<bool> carrierEdgeVisible(edges.size(), true);
  std::vector<std::pair<std::string, std::string>> carrierClustersByEdge(edges.size());
  for (std::size_t e = 0; e < edges.size(); ++e) {
    const std::string& s = edges[e].sourceModelId;
    const std::string& t = edges[e].targetModelId;
    const bool inheritance = edges[e].kind == "inheritance";
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

    if (sBI != leafToBundleIdx.end() || tBI != leafToBundleIdx.end()) {
      if (inheritance) {
        carrierIdByEdge[e] = edges[e].edgeId;
      } else {
        carrierEdgeVisible[e] = false;
      }
      continue;
    }

    if (inheritance) {
      carrierIdByEdge[e] = edges[e].edgeId;
      continue;
    }

    auto sCit = clusterByModelIdFull.find(s);
    auto tCit = clusterByModelIdFull.find(t);
    std::string sCluster = sCit != clusterByModelIdFull.end()
      ? sCit->second
      : std::string{};
    std::string tCluster = tCit != clusterByModelIdFull.end()
      ? tCit->second
      : std::string{};
    if (sCluster.empty()) sCluster = nearestClusterFinal(s);
    if (tCluster.empty()) tCluster = nearestClusterFinal(t);
    if (!sCluster.empty() && !tCluster.empty()) {
      carrierClustersByEdge[e] = {sCluster, tCluster};
      carrierIdByEdge[e] = (sCluster == tCluster)
        ? "Cself|" + sCluster
        : (sCluster < tCluster
            ? "C|" + sCluster + "|" + tCluster
            : "C|" + tCluster + "|" + sCluster);
    } else {
      carrierIdByEdge[e] = edges[e].edgeId;
    }
  }

  const char* hubCarrierEnv = std::getenv("DJERD_HUB_CARRIER_CROSS_FINAL");
  const bool hubCarrier =
    hubCarrierEnv && std::strcmp(hubCarrierEnv, "0") != 0;
  if (hubCarrier) {
    const char* thresholdEnv =
      std::getenv("DJERD_HUB_CARRIER_CROSS_FINAL_THRESHOLD");
    const int threshold = thresholdEnv
      ? std::max(2, std::atoi(thresholdEnv))
      : 16;
    std::unordered_map<std::string, int> incidentCarrierCount;
    for (std::size_t e = 0; e < carrierClustersByEdge.size(); ++e) {
      const auto& [leftCluster, rightCluster] = carrierClustersByEdge[e];
      if (leftCluster.empty() || rightCluster.empty() || leftCluster == rightCluster) {
        continue;
      }
      incidentCarrierCount[leftCluster] += 1;
      incidentCarrierCount[rightCluster] += 1;
    }
    std::size_t hubEdges = 0;
    std::unordered_set<std::string> hubClusters;
    for (std::size_t e = 0; e < carrierClustersByEdge.size(); ++e) {
      const auto& [leftCluster, rightCluster] = carrierClustersByEdge[e];
      if (leftCluster.empty() || rightCluster.empty() || leftCluster == rightCluster) {
        continue;
      }
      const int leftCount = incidentCarrierCount[leftCluster];
      const int rightCount = incidentCarrierCount[rightCluster];
      if (leftCount < threshold && rightCount < threshold) {
        continue;
      }
      const std::string& hub =
        (leftCount > rightCount || (leftCount == rightCount && leftCluster < rightCluster))
          ? leftCluster
          : rightCluster;
      carrierIdByEdge[e] = "H|" + hub;
      hubClusters.insert(hub);
      ++hubEdges;
    }
    if (!quiet) {
      std::fprintf(stderr,
        "[hub-carrier-cross-final] grouped %zu edges through %zu hubs "
        "(threshold=%d).\n",
        hubEdges,
        hubClusters.size(),
        threshold);
    }
    metadata.hubCarrierThreshold = threshold;
    metadata.hubCarrierEdgesGrouped = hubEdges;
    metadata.hubCarrierClusters = hubClusters.size();
  }

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
    if (!carrierEdgeVisible[i] || i >= routes.size() || routes[i].size() < 2) continue;
    for (std::size_t j = i + 1; j < edges.size(); ++j) {
      if (!carrierEdgeVisible[j] || j >= routes.size() || routes[j].size() < 2) continue;
      if (sharesEndpoint(edges[i], edges[j])) continue;
      if (carrierIdByEdge[i] == carrierIdByEdge[j]) continue;
      bool anyCross = false;
      for (std::size_t li = 1; li < routes[i].size() && !anyCross; ++li) {
        for (std::size_t rj = 1; rj < routes[j].size() && !anyCross; ++rj) {
          RoutePoint isect;
          if (properSegmentIntersection(
              routes[i][li - 1], routes[i][li],
              routes[j][rj - 1], routes[j][rj], isect)) {
            if (pointInCarrierOcclusionFinal(isect)) {
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
  if (!quiet) {
    std::fprintf(stderr,
      "[carrier-cross-final] segment %zu -> carrier-grouped %zu "
      "(occluded=%zu, margin=%.1f).\n",
      totalRouteCrossings, carrierGroupedCross, carrierOccludedCross,
      occMarginFinal);
  }

  if (!applyRenderedCarrierMetricsIfRequested(
      nodes,
      edges,
      routes,
      attributes,
      clusterByModelIdFull,
      metadata,
      quality,
      totalRouteCrossings,
      quiet)) {
    quality.edgeCrossings = carrierGroupedCross;
  }
  return true;
}

} // namespace djerd

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

          // Candidate C: env-tunable multistart selection metric. By
          // default (weight 0) the winner is the layout with the fewest
          // straight-line crossings — already a strong proxy for the final
          // visualCrossings (the raw-crossing and visualCrossings winners
          // coincide across the Captain seed corpus). A positive
          // DJERD_MULTISTART_BBOX_WEIGHT blends in the bounding-box area
          // (billions, from node-center spread) so the search can trade a
          // few crossings for a more compact seed — a Pareto knob, not a
          // free win. Routing-based metrics (true visualCross/composite)
          // are unavailable here (positions only, pre-route), so the score
          // stays a cheap O(N) position proxy. weight 0 ⇒ score == cross
          // exactly, so the default selection is byte-identical to before.
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
          auto mstartScore = [&](std::size_t cross) -> double {
            return static_cast<double>(cross)
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
          auto mstartReachedCertifiedFloor = [&](std::size_t cross) -> bool {
            if (
                !canonicalCrossing.available
                || mstartBundleAware
                || mstartBboxWeight != 0.0
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
          double bestScore = mstartScore(bestCross);
          auto bestPositions = mstartSavePositions();
          ClusterGraphResult bestCg = cg;
          int bestSeed = -1;  // -1 = initial run (no explicit setSeed)
          int completedRuns = 1;
          bool certifiedFloorReached = mstartReachedCertifiedFloor(bestCross);
          std::fprintf(stderr,
            "[multistart] run 0 (initial) crossings=%zu score=%.1f\n",
            bestCross, bestScore);
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
            const double altScore = mstartScore(altCross);
            ++completedRuns;
            std::fprintf(stderr,
              "[multistart] run %d seed=%d crossings=%zu score=%.1f%s\n",
              run, seed, altCross, altScore,
              altScore < bestScore ? " (new best)" : "");
            if (altScore < bestScore) {
              bestScore = altScore;
              bestCross = altCross;
              bestPositions = mstartSavePositions();
              bestCg = altCg;
              bestSeed = seed;
              writeProgress(run, seed, altCross);
              certifiedFloorReached = mstartReachedCertifiedFloor(bestCross);
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
            "[multistart] selected run with crossings=%zu score=%.1f "
            "(seed=%d, runs=%d, bboxWeight=%.1f)\n",
            bestCross, bestScore, bestSeed, completedRuns, mstartBboxWeight);
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

          // Capture full cluster membership for edge bundling.
          for (const auto& c : cg.clusters) {
            for (const auto& m : c.members) {
              if (m.nodeIdx < nodes.size()) {
                clusterByModelIdFull[nodes[m.nodeIdx].modelId] = c.clusterId;
              }
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
    if (!clusterModeFlag) {
      compactDistantConnectedNodes(nodes, edges, attributes);
    }
    enforceNodeSeparationStrong(nodes, attributes);
    packDisconnectedComponents(nodes, edges, attributes);
    enforceNodeSeparationStrong(nodes, attributes);
    enforceNodeSeparationStrong(nodes, attributes);
    if (isStraightLineRoutingMode(arguments.mode)) {
      refineStraightHubAxisLayout(nodes, edges, attributes);
      packDisconnectedComponents(nodes, edges, attributes);
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
    // nodes (parent + leaves) are pinned. Set DJERD_NO_KNOT_MIN=1 to
    // skip.
    if ((arguments.clusterGraph || arguments.bubble)
        && !clusterByModelIdFull.empty()) {
      const char* skipKnotEnv = std::getenv("DJERD_NO_KNOT_MIN");
      const bool skipKnot =
        skipKnotEnv && std::strcmp(skipKnotEnv, "0") != 0;
      if (!skipKnot) {
        std::unordered_set<std::string> bundleAbsorbedKM;
        for (const LeafBundleRecord& bundle : metadata.leafBundles) {
          bundleAbsorbedKM.insert(bundle.parentModelId);
          for (const std::string& leaf : bundle.leafModelIds) {
            bundleAbsorbedKM.insert(leaf);
          }
        }
        // Group nodes by cluster (cluster id → node indices, excluding
        // bundle-absorbed).
        std::unordered_map<std::string, std::vector<std::size_t>>
          membersByCluster;
        std::unordered_map<std::string, std::size_t> idToIdxKM;
        idToIdxKM.reserve(nodes.size());
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          idToIdxKM[nodes[i].modelId] = i;
        }
        for (const auto& kv : clusterByModelIdFull) {
          if (bundleAbsorbedKM.count(kv.first)) continue;
          auto it = idToIdxKM.find(kv.first);
          if (it == idToIdxKM.end()) continue;
          membersByCluster[kv.second].push_back(it->second);
        }
        // Build edge index by node — for each node, the edges incident.
        std::vector<std::vector<std::size_t>> edgesByNode(nodes.size());
        for (std::size_t e = 0; e < edges.size(); ++e) {
          auto sIt = idToIdxKM.find(edges[e].sourceModelId);
          auto tIt = idToIdxKM.find(edges[e].targetModelId);
          if (sIt == idToIdxKM.end() || tIt == idToIdxKM.end()) continue;
          edgesByNode[sIt->second].push_back(e);
          edgesByNode[tIt->second].push_back(e);
        }
        // Build edge endpoint indices for crossing tests.
        std::vector<std::pair<std::size_t, std::size_t>> edgePairs(edges.size());
        for (std::size_t e = 0; e < edges.size(); ++e) {
          auto sIt = idToIdxKM.find(edges[e].sourceModelId);
          auto tIt = idToIdxKM.find(edges[e].targetModelId);
          if (sIt == idToIdxKM.end() || tIt == idToIdxKM.end()) {
            edgePairs[e] = {0, 0};
            continue;
          }
          edgePairs[e] = {sIt->second, tIt->second};
        }
        auto sgn = [](double x) { return (x > 0) - (x < 0); };
        auto segmentsCross = [&](std::size_t e1, std::size_t e2) {
          const auto& p1 = edgePairs[e1];
          const auto& p2 = edgePairs[e2];
          if (p1.first == p2.first || p1.first == p2.second
              || p1.second == p2.first || p1.second == p2.second) return false;
          const double ax = attributes.x(nodes[p1.first].handle);
          const double ay = attributes.y(nodes[p1.first].handle);
          const double bx = attributes.x(nodes[p1.second].handle);
          const double by = attributes.y(nodes[p1.second].handle);
          const double cx = attributes.x(nodes[p2.first].handle);
          const double cy = attributes.y(nodes[p2.first].handle);
          const double dx = attributes.x(nodes[p2.second].handle);
          const double dy = attributes.y(nodes[p2.second].handle);
          const int o1 = sgn((bx - ax) * (cy - ay) - (by - ay) * (cx - ax));
          const int o2 = sgn((bx - ax) * (dy - ay) - (by - ay) * (dx - ax));
          const int o3 = sgn((dx - cx) * (ay - cy) - (dy - cy) * (ax - cx));
          const int o4 = sgn((dx - cx) * (by - cy) - (dy - cy) * (bx - cx));
          return (o1 != o2) && (o3 != o4) && (o1 != 0) && (o3 != 0);
        };
        // Count crossings involving any edge incident to m1 or m2.
        auto localCrossCount = [&](std::size_t m1, std::size_t m2) {
          std::unordered_set<std::size_t> incident;
          for (std::size_t e : edgesByNode[m1]) incident.insert(e);
          for (std::size_t e : edgesByNode[m2]) incident.insert(e);
          std::size_t total = 0;
          for (std::size_t e1 : incident) {
            for (std::size_t e2 = 0; e2 < edges.size(); ++e2) {
              if (incident.count(e2) && e2 <= e1) continue;
              if (segmentsCross(e1, e2)) ++total;
            }
          }
          return total;
        };
        // Pre-build bundle bboxes (with margin) so knot-min's overlap
        // check can also detect when a swap pushes a node into a
        // leafBundle's territory.
        constexpr double kKnotOverlapMargin = 8.0;
        std::vector<Rect> bundleBoxesKM;
        std::vector<std::unordered_set<std::size_t>> bundleExemptIdx;
        bundleBoxesKM.reserve(metadata.leafBundles.size());
        bundleExemptIdx.reserve(metadata.leafBundles.size());
        for (const LeafBundleRecord& bundle : metadata.leafBundles) {
          Rect br;
          br.left = bundle.bboxX - kKnotOverlapMargin;
          br.right = bundle.bboxX + bundle.bboxWidth + kKnotOverlapMargin;
          br.top = bundle.bboxY - kKnotOverlapMargin;
          br.bottom = bundle.bboxY + bundle.bboxHeight + kKnotOverlapMargin;
          bundleBoxesKM.push_back(br);
          std::unordered_set<std::size_t> exempt;
          auto pIt = idToIdxKM.find(bundle.parentModelId);
          if (pIt != idToIdxKM.end()) exempt.insert(pIt->second);
          for (const std::string& leaf : bundle.leafModelIds) {
            auto lIt = idToIdxKM.find(leaf);
            if (lIt != idToIdxKM.end()) exempt.insert(lIt->second);
          }
          bundleExemptIdx.push_back(std::move(exempt));
        }
        // Overlap count: node-rect overlaps + bundle-bbox overlaps for
        // m1 / m2. Bundle bboxes are obstacles too (per user spec).
        auto localOverlapCount = [&](std::size_t m1, std::size_t m2) {
          auto rectOf = [&](std::size_t i) {
            const NodeRecord& nd = nodes[i];
            Rect r;
            const double cx = attributes.x(nd.handle);
            const double cy = attributes.y(nd.handle);
            r.left = cx - nd.width / 2.0 - kKnotOverlapMargin;
            r.right = cx + nd.width / 2.0 + kKnotOverlapMargin;
            r.top = cy - nd.height / 2.0 - kKnotOverlapMargin;
            r.bottom = cy + nd.height / 2.0 + kKnotOverlapMargin;
            return r;
          };
          std::size_t total = 0;
          for (std::size_t target : {m1, m2}) {
            const Rect tr = rectOf(target);
            for (std::size_t k = 0; k < nodes.size(); ++k) {
              if (k == m1 || k == m2) continue;
              if (rectsOverlap(tr, rectOf(k))) ++total;
            }
            // Bundle penalties: m1/m2 should not move INTO any bundle
            // bbox unless they're already absorbed by it.
            for (std::size_t bi = 0; bi < bundleBoxesKM.size(); ++bi) {
              if (bundleExemptIdx[bi].count(target)) continue;
              if (rectsOverlap(tr, bundleBoxesKM[bi])) ++total;
            }
            // Incident-edge bundle penalty: count edges from `target`
            // whose straight-line segment passes through a bundle bbox
            // (excluding bundles target is exempt from). This catches
            // the case where a swap moves the node such that its
            // incident edge now crosses a bundle area.
            for (std::size_t e : edgesByNode[target]) {
              const auto& p = edgePairs[e];
              const std::size_t other = (p.first == target) ? p.second : p.first;
              const RoutePoint pa{
                attributes.x(nodes[target].handle),
                attributes.y(nodes[target].handle)};
              const RoutePoint pb{
                attributes.x(nodes[other].handle),
                attributes.y(nodes[other].handle)};
              for (std::size_t bi = 0; bi < bundleBoxesKM.size(); ++bi) {
                if (bundleExemptIdx[bi].count(target)) continue;
                if (bundleExemptIdx[bi].count(other)) continue;
                if (segmentIntersectsRect(pa, pb, bundleBoxesKM[bi])) ++total;
              }
            }
          }
          return total;
        };
        // Build candidate set of swappable nodes: any non-bundle node
        // that has at least one inter-cluster edge (i.e., participates
        // in the central tangle). Cluster roots are pinned (backbone
        // structure) and bundle members are placed by matrix.
        std::vector<std::size_t> swappable;
        std::unordered_set<std::size_t> rootSetKM;
        for (const auto& kv : clusterByModelIdFull) {
          // We don't have a direct rootSet map; mark nodes that are
          // listed as rootIdx in any cluster. Simpler: mark nodes with
          // very high incidence as roots and skip. But the cleanest
          // marker: a "root" is the cluster's representative — for our
          // purposes, treat anything in clusterByModelIdFull as a
          // member candidate. Backbone roots will still tend to settle
          // because their cluster centroid pulls back.
          (void)kv;
        }
        // Include ALL non-bundle nodes (cluster members AND non-cluster).
        // Earlier filter (only inter-cluster edge owners) limited the
        // pool to 604 candidates; expanding to ~1.1k lets intra-cluster
        // re-arrangements untangle local knots too.
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          if (bundleAbsorbedKM.count(nodes[i].modelId)) continue;
          swappable.push_back(i);
        }
        // Spatial bin for nearby-pair lookup. Only swap nodes within
        // 2× average cluster radius — avoids absurd long-distance moves
        // that would scramble the layout.
        double sumR = 0.0;
        std::size_t cntR = 0;
        for (std::size_t i : swappable) {
          sumR += std::max(nodes[i].width, nodes[i].height);
          ++cntR;
        }
        const double avgNodeDim = cntR > 0 ? sumR / cntR : 100.0;
        // 9 cells. Tested 6→9: -3.8% visual, +43% time. 9→12 marginal,
        // not worth the further slowdown. Sweet spot for this graph.
        const double swapRadius = std::max(800.0, avgNodeDim * 9.0);
        auto pairHashKM = [](const std::pair<long long, long long>& p) {
          return std::hash<long long>()(p.first)
            ^ (std::hash<long long>()(p.second) << 1);
        };
        std::unordered_map<std::pair<long long, long long>,
                            std::vector<std::size_t>, decltype(pairHashKM)>
          binsKM(0, pairHashKM);
        const double cellKM = swapRadius;
        auto binKeyKM = [&](double x, double y) {
          return std::make_pair(
            static_cast<long long>(std::floor(x / cellKM)),
            static_cast<long long>(std::floor(y / cellKM)));
        };
        for (std::size_t i : swappable) {
          binsKM[binKeyKM(attributes.x(nodes[i].handle),
                          attributes.y(nodes[i].handle))].push_back(i);
        }
        // Iter cap raised 6→12 — early termination kicks in once a pass
        // accepts zero swaps, so doubling the cap costs nothing on
        // converged graphs but lets harder convergence keep going.
        constexpr int kKnotMaxIters = 12;
        std::size_t totalAccepted = 0;
        for (int iter = 0; iter < kKnotMaxIters; ++iter) {
          std::size_t accepted = 0;
          // Rebuild bins each iter (positions changed).
          if (iter > 0) {
            binsKM.clear();
            for (std::size_t i : swappable) {
              binsKM[binKeyKM(attributes.x(nodes[i].handle),
                              attributes.y(nodes[i].handle))].push_back(i);
            }
          }
          for (std::size_t m1 : swappable) {
            const auto k = binKeyKM(attributes.x(nodes[m1].handle),
                                     attributes.y(nodes[m1].handle));
            for (long long dx = -1; dx <= 1; ++dx) {
              for (long long dy = -1; dy <= 1; ++dy) {
                auto bIt = binsKM.find({k.first + dx, k.second + dy});
                if (bIt == binsKM.end()) continue;
                for (std::size_t m2 : bIt->second) {
                  if (m2 <= m1) continue;
                  const std::size_t beforeC = localCrossCount(m1, m2);
                  const std::size_t beforeO = localOverlapCount(m1, m2);
                  const double x1 = attributes.x(nodes[m1].handle);
                  const double y1 = attributes.y(nodes[m1].handle);
                  attributes.x(nodes[m1].handle) = attributes.x(nodes[m2].handle);
                  attributes.y(nodes[m1].handle) = attributes.y(nodes[m2].handle);
                  attributes.x(nodes[m2].handle) = x1;
                  attributes.y(nodes[m2].handle) = y1;
                  const std::size_t afterC = localCrossCount(m1, m2);
                  const std::size_t afterO = localOverlapCount(m1, m2);
                  if (afterC + afterO < beforeC + beforeO
                      && afterO <= beforeO) {
                    ++accepted;
                  } else {
                    attributes.x(nodes[m2].handle) = attributes.x(nodes[m1].handle);
                    attributes.y(nodes[m2].handle) = attributes.y(nodes[m1].handle);
                    attributes.x(nodes[m1].handle) = x1;
                    attributes.y(nodes[m1].handle) = y1;
                  }
                }
              }
            }
          }
          totalAccepted += accepted;
          if (accepted == 0) break;
        }
        if (totalAccepted > 0) {
          std::fprintf(stderr,
            "[knot-min] Accepted %zu spatial swaps among %zu candidates.\n",
            totalAccepted, swappable.size());
        }

        // Simulated Annealing pass — DISABLED. Tested 8000 attempts
        // with T=8 (28% accept rate, 3,278 → 3,568 visualCross worsen
        // by +9%) and T=1.5 (1,258 accept, 3,278 → 3,345 worsen by
        // +2%). SA random spatial swaps trade local cross reductions
        // for global new crosses that aren't captured by localCrossCount
        // (which only sees edges incident to m1/m2). Set
        // DJERD_KNOT_SA=1 to enable for experimentation.
        const char* knotSaEnv = std::getenv("DJERD_KNOT_SA");
        if (knotSaEnv && std::strcmp(knotSaEnv, "0") != 0) {
          std::mt19937 rng(0xC0FFEEu);
          std::uniform_int_distribution<std::size_t> distIdx(
            0, swappable.size() ? swappable.size() - 1 : 0);
          std::uniform_real_distribution<double> distR(0.0, 1.0);
          // Rebuild bins (positions changed in greedy pass).
          binsKM.clear();
          for (std::size_t i : swappable) {
            binsKM[binKeyKM(attributes.x(nodes[i].handle),
                            attributes.y(nodes[i].handle))].push_back(i);
          }
          constexpr int kSaAttempts = 5000;
          double T = 1.5;  // low: only small uphill (ΔE=1-2) kicks in
          const double decay = std::pow(0.001 / 1.5, 1.0 / kSaAttempts);
          std::size_t saAccepted = 0;
          std::size_t saUphill = 0;
          for (int k = 0; k < kSaAttempts && swappable.size() >= 2; ++k) {
            const std::size_t saI = distIdx(rng);
            const std::size_t m1 = swappable[saI];
            // Pick m2 from m1's spatial bin ring.
            const auto kk = binKeyKM(attributes.x(nodes[m1].handle),
                                      attributes.y(nodes[m1].handle));
            std::vector<std::size_t> ring;
            for (long long dx = -1; dx <= 1; ++dx) {
              for (long long dy = -1; dy <= 1; ++dy) {
                auto bIt = binsKM.find({kk.first + dx, kk.second + dy});
                if (bIt == binsKM.end()) continue;
                for (std::size_t r : bIt->second) {
                  if (r != m1) ring.push_back(r);
                }
              }
            }
            if (ring.empty()) continue;
            std::uniform_int_distribution<std::size_t> distRing(
              0, ring.size() - 1);
            const std::size_t m2 = ring[distRing(rng)];
            const std::size_t beforeC = localCrossCount(m1, m2);
            const std::size_t beforeO = localOverlapCount(m1, m2);
            const double x1 = attributes.x(nodes[m1].handle);
            const double y1 = attributes.y(nodes[m1].handle);
            attributes.x(nodes[m1].handle) = attributes.x(nodes[m2].handle);
            attributes.y(nodes[m1].handle) = attributes.y(nodes[m2].handle);
            attributes.x(nodes[m2].handle) = x1;
            attributes.y(nodes[m2].handle) = y1;
            const std::size_t afterC = localCrossCount(m1, m2);
            const std::size_t afterO = localOverlapCount(m1, m2);
            const long long dE =
              static_cast<long long>(afterC + afterO)
              - static_cast<long long>(beforeC + beforeO);
            // Hard reject on overlap regression OR pure improvement.
            bool accept = false;
            if (afterO > beforeO) {
              accept = false;
            } else if (dE <= 0) {
              accept = true;
            } else if (T > 1e-3) {
              const double prob = std::exp(-static_cast<double>(dE) / T);
              if (distR(rng) < prob) {
                accept = true;
                ++saUphill;
              }
            }
            if (accept) {
              ++saAccepted;
            } else {
              attributes.x(nodes[m2].handle) = attributes.x(nodes[m1].handle);
              attributes.y(nodes[m2].handle) = attributes.y(nodes[m1].handle);
              attributes.x(nodes[m1].handle) = x1;
              attributes.y(nodes[m1].handle) = y1;
            }
            T *= decay;
          }
          if (saAccepted > 0) {
            std::fprintf(stderr,
              "[knot-min-sa] Accepted %zu swaps (%zu uphill) of %d attempts.\n",
              saAccepted, saUphill, kSaAttempts);
          }
        }

        // 2-opt cross-targeted pass: directly target each remaining
        // segment-segment crossing. For each crossing pair (e1, e2),
        // try swapping the swappable endpoint of e1 with the
        // swappable endpoint of e2 — this often unties the crossing
        // in one step. Spatial knot-min picks pairs by proximity, but
        // some untangle-able crosses involve nodes that aren't
        // spatially adjacent (long-range edges over the layout).
        std::unordered_set<std::size_t> swappableSet(
          swappable.begin(), swappable.end());
        std::size_t twoOptAccepted = 0;
        for (int outerIter = 0; outerIter < 8; ++outerIter) {
          // Find all current crossings.
          std::vector<std::pair<std::size_t, std::size_t>> crossings;
          for (std::size_t i = 0; i < edges.size(); ++i) {
            for (std::size_t j = i + 1; j < edges.size(); ++j) {
              if (segmentsCross(i, j)) crossings.emplace_back(i, j);
            }
          }
          if (crossings.empty()) break;
          std::size_t innerAccepted = 0;
          for (const auto& [e1, e2] : crossings) {
            if (!segmentsCross(e1, e2)) continue;  // already resolved
            const auto& p1 = edgePairs[e1];
            const auto& p2 = edgePairs[e2];
            // Try each pair of swappable endpoints — one from e1, one
            // from e2. Accept first swap that reduces visualConflict.
            const std::array<std::pair<std::size_t, std::size_t>, 4>
              candidates = {{
                {p1.first, p2.first},
                {p1.first, p2.second},
                {p1.second, p2.first},
                {p1.second, p2.second},
              }};
            bool resolved = false;
            for (const auto& [m1, m2] : candidates) {
              if (m1 == m2) continue;
              if (!swappableSet.count(m1) || !swappableSet.count(m2)) continue;
              const std::size_t beforeC = localCrossCount(m1, m2);
              const std::size_t beforeO = localOverlapCount(m1, m2);
              const double x1 = attributes.x(nodes[m1].handle);
              const double y1 = attributes.y(nodes[m1].handle);
              attributes.x(nodes[m1].handle) = attributes.x(nodes[m2].handle);
              attributes.y(nodes[m1].handle) = attributes.y(nodes[m2].handle);
              attributes.x(nodes[m2].handle) = x1;
              attributes.y(nodes[m2].handle) = y1;
              const std::size_t afterC = localCrossCount(m1, m2);
              const std::size_t afterO = localOverlapCount(m1, m2);
              if (afterC + afterO < beforeC + beforeO
                  && afterO <= beforeO) {
                ++innerAccepted;
                resolved = true;
                break;
              } else {
                attributes.x(nodes[m2].handle) = attributes.x(nodes[m1].handle);
                attributes.y(nodes[m2].handle) = attributes.y(nodes[m1].handle);
                attributes.x(nodes[m1].handle) = x1;
                attributes.y(nodes[m1].handle) = y1;
              }
            }
            (void)resolved;
          }
          twoOptAccepted += innerAccepted;
          if (innerAccepted == 0) break;
        }
        if (twoOptAccepted > 0) {
          std::fprintf(stderr,
            "[knot-min-2opt] Accepted %zu cross-targeted swaps.\n",
            twoOptAccepted);
        }

        // Second-pass spatial knot-min after 2-opt — disabled: 16
        // additional swaps for -20 visualCross at +5s time cost. ROI
        // too low. Set DJERD_KNOT_2NDPASS=1 to enable.
        const char* knot2ndEnv = std::getenv("DJERD_KNOT_2NDPASS");
        const bool runKnot2nd =
          knot2ndEnv && std::strcmp(knot2ndEnv, "0") != 0;
        std::size_t totalAccepted2 = 0;
        for (int iter = 0; runKnot2nd && iter < kKnotMaxIters; ++iter) {
          std::size_t accepted = 0;
          binsKM.clear();
          for (std::size_t i : swappable) {
            binsKM[binKeyKM(attributes.x(nodes[i].handle),
                            attributes.y(nodes[i].handle))].push_back(i);
          }
          for (std::size_t m1 : swappable) {
            const auto k = binKeyKM(attributes.x(nodes[m1].handle),
                                     attributes.y(nodes[m1].handle));
            for (long long dx = -1; dx <= 1; ++dx) {
              for (long long dy = -1; dy <= 1; ++dy) {
                auto bIt = binsKM.find({k.first + dx, k.second + dy});
                if (bIt == binsKM.end()) continue;
                for (std::size_t m2 : bIt->second) {
                  if (m2 <= m1) continue;
                  const std::size_t beforeC = localCrossCount(m1, m2);
                  const std::size_t beforeO = localOverlapCount(m1, m2);
                  const double x1 = attributes.x(nodes[m1].handle);
                  const double y1 = attributes.y(nodes[m1].handle);
                  attributes.x(nodes[m1].handle) = attributes.x(nodes[m2].handle);
                  attributes.y(nodes[m1].handle) = attributes.y(nodes[m2].handle);
                  attributes.x(nodes[m2].handle) = x1;
                  attributes.y(nodes[m2].handle) = y1;
                  const std::size_t afterC = localCrossCount(m1, m2);
                  const std::size_t afterO = localOverlapCount(m1, m2);
                  if (afterC + afterO < beforeC + beforeO
                      && afterO <= beforeO) {
                    ++accepted;
                  } else {
                    attributes.x(nodes[m2].handle) = attributes.x(nodes[m1].handle);
                    attributes.y(nodes[m2].handle) = attributes.y(nodes[m1].handle);
                    attributes.x(nodes[m1].handle) = x1;
                    attributes.y(nodes[m1].handle) = y1;
                  }
                }
              }
            }
          }
          totalAccepted2 += accepted;
          if (accepted == 0) break;
        }
        if (totalAccepted2 > 0) {
          std::fprintf(stderr,
            "[knot-min] Second pass: accepted %zu additional spatial swaps.\n",
            totalAccepted2);
        }
      }
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

    // === Visual knot detector ===
    // Targets the user's "시각적으로 바로 풀수있는 knot들" — spatial
    // clusters of polyline crossings that pd-knot's edge-pair iteration
    // missed. Bucket polyline crossings by spatial cell (~300 units),
    // identify hot cells (≥ 3 crossings), then for each hot cell try
    // pairwise position swaps among the involved nodes. Accept swap if
    // global polyline cross count drops.
    //
    // Default ON. Disable with DJERD_VISUAL_KNOT=0.
    {
      const char* vkEnv = std::getenv("DJERD_VISUAL_KNOT");
      const bool runVk = !vkEnv || std::strcmp(vkEnv, "0") != 0;
      if (runVk) {
        // Build edge → node map.
        std::unordered_map<std::string, std::size_t> idToIdxVK;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          idToIdxVK[nodes[i].modelId] = i;
        }
        std::vector<std::pair<std::size_t, std::size_t>> edgePairsVK(edges.size());
        std::vector<std::vector<std::size_t>> edgesByNodeVK(nodes.size());
        for (std::size_t e = 0; e < edges.size(); ++e) {
          auto sIt = idToIdxVK.find(edges[e].sourceModelId);
          auto tIt = idToIdxVK.find(edges[e].targetModelId);
          if (sIt == idToIdxVK.end() || tIt == idToIdxVK.end()) {
            edgePairsVK[e] = {0, 0};
            continue;
          }
          edgePairsVK[e] = {sIt->second, tIt->second};
          if (sIt->second != tIt->second) {
            edgesByNodeVK[sIt->second].push_back(e);
            edgesByNodeVK[tIt->second].push_back(e);
          }
        }
        std::unordered_set<std::string> bundleAbsorbedVK;
        for (const LeafBundleRecord& b : metadata.leafBundles) {
          bundleAbsorbedVK.insert(b.parentModelId);
          for (const std::string& l : b.leafModelIds) {
            bundleAbsorbedVK.insert(l);
          }
        }

        auto polyCrossVK = [&](std::size_t e1, std::size_t e2) -> bool {
          if (e1 >= routes.size() || e2 >= routes.size()) return false;
          if (routes[e1].size() < 2 || routes[e2].size() < 2) return false;
          if (sharesEndpoint(edges[e1], edges[e2])) return false;
          // Plan A: same-carrier edges are masked in reported metric.
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
        auto polyCrossPointVK = [&](std::size_t e1, std::size_t e2,
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

        // Collect cross points with coordinates.
        struct VKCross {
          double x, y;
          std::size_t e1, e2;
        };
        std::vector<VKCross> crosses;
        for (std::size_t i = 0; i < edges.size(); ++i) {
          for (std::size_t j = i + 1; j < edges.size(); ++j) {
            RoutePoint pt;
            if (polyCrossPointVK(i, j, pt)) {
              crosses.push_back({pt.x, pt.y, i, j});
            }
          }
        }

        // Spatial bucketing: cell ~300 units (typical node diameter).
        constexpr double kCellSize = 300.0;
        auto cellKey = [&](double x, double y) {
          return std::make_pair(static_cast<long long>(std::floor(x / kCellSize)),
                                static_cast<long long>(std::floor(y / kCellSize)));
        };
        std::map<std::pair<long long, long long>, std::vector<std::size_t>> cellMap;
        for (std::size_t k = 0; k < crosses.size(); ++k) {
          cellMap[cellKey(crosses[k].x, crosses[k].y)].push_back(k);
        }

        // Hot cells: 3+ crossings.
        constexpr std::size_t kMinKnot = 3;
        std::vector<std::pair<std::size_t, std::pair<long long, long long>>> hotCells;
        for (const auto& cm : cellMap) {
          if (cm.second.size() >= kMinKnot) {
            hotCells.emplace_back(cm.second.size(), cm.first);
          }
        }
        std::sort(hotCells.begin(), hotCells.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });

        // For each hot cell, collect involved nodes and try pairwise swap.
        // Cost: total polyline crosses incident to the swap pair. Accept if
        // the swap reduces it.
        auto incidentCrossVK = [&](std::size_t m1, std::size_t m2) -> std::size_t {
          std::unordered_set<std::size_t> incident;
          for (std::size_t e : edgesByNodeVK[m1]) incident.insert(e);
          for (std::size_t e : edgesByNodeVK[m2]) incident.insert(e);
          std::size_t total = 0;
          for (std::size_t e1 : incident) {
            for (std::size_t e2 = 0; e2 < edges.size(); ++e2) {
              if (e1 == e2) continue;
              if (incident.count(e2) && e2 < e1) continue;
              if (polyCrossVK(e1, e2)) ++total;
            }
          }
          return total;
        };
        auto applyEndpointMoveVK = [&](std::size_t node) {
          for (std::size_t e : edgesByNodeVK[node]) {
            if (e >= routes.size() || routes[e].size() < 2) continue;
            const double nx = attributes.x(nodes[node].handle);
            const double ny = attributes.y(nodes[node].handle);
            if (edgePairsVK[e].first == node) routes[e].front() = {nx, ny};
            if (edgePairsVK[e].second == node) routes[e].back() = {nx, ny};
          }
        };

        std::size_t totalAccepted = 0;
        std::size_t totalHandled = 0;
        std::size_t initialCrossPoints = crosses.size();
        std::size_t initialHotCells = hotCells.size();

        // Multi-iteration: re-detect hot cells after each round of swaps.
        // Resolved swaps may have eliminated some hot cells but created
        // new patterns. Default 3 rounds, env-tunable.
        const char* vkIterEnv = std::getenv("DJERD_VISUAL_KNOT_ITERS");
        const int vkMaxIters = vkIterEnv ? std::max(1, std::atoi(vkIterEnv)) : 3;
        for (int iter = 0; iter < vkMaxIters; ++iter) {
          std::size_t iterAccepted = 0;
          for (const auto& hc : hotCells) {
            std::set<std::size_t> involved;
            for (std::size_t crossIdx : cellMap[hc.second]) {
              const auto& cr = crosses[crossIdx];
              involved.insert(edgePairsVK[cr.e1].first);
              involved.insert(edgePairsVK[cr.e1].second);
              involved.insert(edgePairsVK[cr.e2].first);
              involved.insert(edgePairsVK[cr.e2].second);
            }
            std::vector<std::size_t> swappable;
            for (std::size_t n : involved) {
              if (n < nodes.size()
                  && !bundleAbsorbedVK.count(nodes[n].modelId)) {
                swappable.push_back(n);
              }
            }
            if (swappable.size() < 2) continue;
            ++totalHandled;

            for (std::size_t i = 0; i < swappable.size(); ++i) {
              for (std::size_t j = i + 1; j < swappable.size(); ++j) {
                const std::size_t m1 = swappable[i];
                const std::size_t m2 = swappable[j];
                const std::size_t before = incidentCrossVK(m1, m2);
                if (before == 0) continue;
                const double x1 = attributes.x(nodes[m1].handle);
                const double y1 = attributes.y(nodes[m1].handle);
                attributes.x(nodes[m1].handle) = attributes.x(nodes[m2].handle);
                attributes.y(nodes[m1].handle) = attributes.y(nodes[m2].handle);
                attributes.x(nodes[m2].handle) = x1;
                attributes.y(nodes[m2].handle) = y1;
                applyEndpointMoveVK(m1);
                applyEndpointMoveVK(m2);
                const std::size_t after = incidentCrossVK(m1, m2);
                if (after < before) {
                  ++iterAccepted;
                } else {
                  attributes.x(nodes[m2].handle) = attributes.x(nodes[m1].handle);
                  attributes.y(nodes[m2].handle) = attributes.y(nodes[m1].handle);
                  attributes.x(nodes[m1].handle) = x1;
                  attributes.y(nodes[m1].handle) = y1;
                  applyEndpointMoveVK(m1);
                  applyEndpointMoveVK(m2);
                }
              }
            }

            // 3-rotation: for hot cells with EXACTLY 3 swappable nodes,
            // try the two cyclic rotations (A→C→B→A and A→B→C→A). Pair
            // swaps alone can't achieve these — they require all 3
            // positions to cycle simultaneously. Implemented as 2
            // sequential pair-swaps.
            if (swappable.size() == 3) {
              const std::size_t a = swappable[0];
              const std::size_t b = swappable[1];
              const std::size_t c = swappable[2];
              auto swapPair = [&](std::size_t m1, std::size_t m2) {
                const double x1 = attributes.x(nodes[m1].handle);
                const double y1 = attributes.y(nodes[m1].handle);
                attributes.x(nodes[m1].handle) = attributes.x(nodes[m2].handle);
                attributes.y(nodes[m1].handle) = attributes.y(nodes[m2].handle);
                attributes.x(nodes[m2].handle) = x1;
                attributes.y(nodes[m2].handle) = y1;
                applyEndpointMoveVK(m1);
                applyEndpointMoveVK(m2);
              };
              auto cost3 = [&]() {
                std::unordered_set<std::size_t> incident;
                for (std::size_t e : edgesByNodeVK[a]) incident.insert(e);
                for (std::size_t e : edgesByNodeVK[b]) incident.insert(e);
                for (std::size_t e : edgesByNodeVK[c]) incident.insert(e);
                std::size_t total = 0;
                for (std::size_t e1 : incident) {
                  for (std::size_t e2 = 0; e2 < edges.size(); ++e2) {
                    if (e1 == e2) continue;
                    if (incident.count(e2) && e2 < e1) continue;
                    if (polyCrossVK(e1, e2)) ++total;
                  }
                }
                return total;
              };
              const std::size_t before3 = cost3();
              if (before3 > 0) {
                // Rotation 1: a@C, b@A, c@B — swap(a,b); swap(a,c).
                swapPair(a, b);
                swapPair(a, c);
                const std::size_t after1 = cost3();
                if (after1 < before3) {
                  ++iterAccepted;
                } else {
                  // Revert rotation 1.
                  swapPair(a, c);
                  swapPair(a, b);
                  // Rotation 2: a@B, b@C, c@A — swap(a,c); swap(a,b).
                  swapPair(a, c);
                  swapPair(a, b);
                  const std::size_t after2 = cost3();
                  if (after2 < before3) {
                    ++iterAccepted;
                  } else {
                    swapPair(a, b);
                    swapPair(a, c);
                  }
                }
              }
            }
          }
          totalAccepted += iterAccepted;
          if (iterAccepted == 0) break;

          // Re-detect crosses + hot cells for next iteration.
          crosses.clear();
          for (std::size_t i = 0; i < edges.size(); ++i) {
            for (std::size_t j = i + 1; j < edges.size(); ++j) {
              RoutePoint pt;
              if (polyCrossPointVK(i, j, pt)) {
                crosses.push_back({pt.x, pt.y, i, j});
              }
            }
          }
          cellMap.clear();
          for (std::size_t k = 0; k < crosses.size(); ++k) {
            cellMap[cellKey(crosses[k].x, crosses[k].y)].push_back(k);
          }
          hotCells.clear();
          for (const auto& cm : cellMap) {
            if (cm.second.size() >= kMinKnot) {
              hotCells.emplace_back(cm.second.size(), cm.first);
            }
          }
          std::sort(hotCells.begin(), hotCells.end(),
                    [](const auto& a, const auto& b) { return a.first > b.first; });
        }

        std::fprintf(stderr,
          "[visual-knot] %zu→%zu cross points, %zu→%zu hot cells (≥%zu), "
          "%zu handled, %zu swap accepted.\n",
          initialCrossPoints, crosses.size(),
          initialHotCells, hotCells.size(),
          kMinKnot, totalHandled, totalAccepted);
      }
    }

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
        std::vector<int32_t> grid(static_cast<std::size_t>(GW) * GH, 0);

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
              grid[static_cast<std::size_t>(y) * GW + x] = lab;
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
            int32_t& cell = grid[static_cast<std::size_t>(y) * GW + x];
            if (cell == 0) cell = lab;  // don't overwrite nodes
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
        std::vector<int32_t> faceLabel(static_cast<std::size_t>(GW) * GH, 0);
        int faceCount = 0;
        std::vector<int> faceSize;
        std::vector<std::pair<int, int>> faceBboxMin;  // gx, gy
        std::vector<std::pair<int, int>> faceBboxMax;
        for (int y = 0; y < GH; ++y) {
          for (int x = 0; x < GW; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * GW + x;
            if (grid[idx] != 0 || faceLabel[idx] != 0) continue;
            ++faceCount;
            faceLabel[idx] = faceCount;
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
                if (grid[np] != 0 || faceLabel[np] != 0) continue;
                faceLabel[np] = faceCount;
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
          if (grid[baseIdx] == 0) {
            return faceLabel[baseIdx];
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
                if (grid[nIdx] == 0) {
                  return faceLabel[nIdx];
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
        // Save raster to /tmp/face-raster.ppm for visual inspection.
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
          const bool writePpm = !ppmEnv || std::strcmp(ppmEnv, "0") != 0;
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
                      const int32_t v = grid[idx2];
                      if (v > 0) {
                        cellPick = v;  // node wins
                        oy = s; break;  // break both
                      } else if (v < 0 && cellPick >= 0) {
                        cellPick = v;  // edge if no node yet
                      } else if (v == 0 && cellPick == 0 && facePick == 0) {
                        facePick = faceLabel[idx2];
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
              rawCrossings)) {
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
              rawCrossings)) {
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
              rawCrossings)) {
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
              rawCrossings)) {
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
          if (!skipCarrierReloc && !metadata.leafBundles.empty()) {
            qm.edgeCrossings = carrierGroupedCrossReloc();
          }
          qm.visualCrossings =
            qm.edgeCrossings
            + qm.edgeNodeIntersections
            + qm.nodeOverlaps
            + qm.bundleEdgeIntersections
            + qm.bundleNodeOverlaps;
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

          for (const auto& dir : directions) {
            for (double radius : radii) {
              addOffset(dir.first * radius, dir.second * radius);
              if (static_cast<int>(offsets.size()) >= maxCandidates) return offsets;
            }
          }
          for (const auto& dir : directions) {
            for (double radius : ringRadii) {
              const double tx = px + dir.first * radius;
              const double ty = py + dir.second * radius;
              addOffset(tx - cx, ty - cy);
              if (static_cast<int>(offsets.size()) >= maxCandidates) return offsets;
            }
          }
          return offsets;
        };

        LayoutQualityMetrics currentFull = qualityReloc();
        const double baseArea = currentFull.boundingBoxArea;
        double currentFullScore = fullScoreReloc(currentFull, baseArea);
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

            LayoutQualityMetrics currentQuick =
              measureLayoutQuality(nodes, edges, routes, attributes, &metadata.leafBundles);
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
              const LayoutQualityMetrics qm =
                measureLayoutQuality(nodes, edges, routes, attributes, &metadata.leafBundles);
              const bool bboxOk =
                baseArea <= 0.0 || qm.boundingBoxArea <= baseArea * bboxLimit;
              const double score = obstructionScoreReloc(qm, baseArea);
              if (fullScan && bboxOk && score <= currentQuickScore + quickSlack) {
                const LayoutQualityMetrics nextFull = qualityReloc();
                const double nextFullScore = fullScoreReloc(nextFull, baseArea);
                if (nextFullScore + minGain < bestFullScore) {
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
                if (bboxOk && nextFullScore + minGain < bestFullScore) {
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
              rawCrossings)) {
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
              rawCrossings)) {
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
              rawCrossings)) {
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
            true);
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
            true);
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
	            std::vector<PairRank> rankedPairs;
	            std::set<std::pair<std::string, std::string>> visitedEffectiveEdges;
	            for (std::size_t e = 0; e < edges.size(); ++e) {
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

              for (const PairMove& move : moves) {
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
            if (movedThisRound == 0) {
              break;
            }
          }

	          if (nodePairConsidered > 0) {
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
              "compactRelocate=%d, scoring=%s).\n",
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
              useReportedCarrierScoring ? "reported-carrier" : "raw-route");
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

      applyFinalCarrierMetricsIfRequested(
        nodes,
        edges,
        routes,
        attributes,
        clusterByModelIdFull,
        metadata,
        quality,
        totalRouteCrossings);

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
