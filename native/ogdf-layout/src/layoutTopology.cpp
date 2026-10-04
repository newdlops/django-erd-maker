#include "layoutPipeline.h"
#include "layoutAlgorithms.h"

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
    || mode == "planar_backbone"
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
  const char* configured = std::getenv("DJERD_LAYOUT_THREADS");
  if (configured == nullptr) configured = std::getenv("OMP_NUM_THREADS");
  const unsigned long requested = configured == nullptr ? 1 : std::strtoul(configured, nullptr, 10);
  return std::max<std::size_t>(1, std::min<std::size_t>(
    std::min<std::size_t>(8, detected == 0 ? 1 : detected), requested));
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

std::vector<int> runLouvainPhase(
  const std::vector<std::vector<std::pair<std::size_t, double>>>& adj,
  const std::vector<double>& degree,
  double totalWeight2m,
  std::size_t maxPasses,
  std::size_t& outIterations,
  double resolution) {
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

std::vector<std::string> assignLouvainClusterLabels(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  std::size_t& outCommunityCount,
  std::size_t& outIterations,
  std::size_t maxPasses) {
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

std::vector<std::string> appendLevel2BccLabels(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::string>& level1Labels,
  std::size_t& outBridgeCountTotal,
  std::size_t& outDistinctCombinedClusters,
  std::size_t& outLargestCombinedClusterSize,
  std::size_t& outInnerSplitClusters,
  std::size_t minRecurseSize) {
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

std::vector<std::string> assignTwoLevelBccLabels(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  std::size_t& outBridgeCountTotal,
  std::size_t& outBccCountTotal,
  std::size_t& outLargestClusterSize,
  std::size_t& outInnerSplitClusters,
  std::size_t minRecurseSize) {
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

}  // namespace djerd
