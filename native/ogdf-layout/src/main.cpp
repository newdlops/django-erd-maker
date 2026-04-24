#include <ogdf/basic/Graph.h>
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
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
struct CliArguments {
  std::string edgesFile;
  std::string mode;
  std::string nodesFile;
};

struct NodeRecord {
  std::string modelId;
  double height = 0.0;
  ogdf::node handle = nullptr;
  double width = 0.0;
  double x = 0.0;
  double y = 0.0;
};

struct EdgeRecord {
  std::string edgeId;
  ogdf::edge handle = nullptr;
  std::string kind;
  std::string provenance;
  ogdf::node sourceHandle = nullptr;
  std::string sourceModelId;
  ogdf::node targetHandle = nullptr;
  std::string targetModelId;
};

struct Bounds {
  double minX = 0.0;
  double minY = 0.0;
};

struct Rect {
  double bottom = 0.0;
  double left = 0.0;
  double right = 0.0;
  double top = 0.0;
};

struct RoutePoint {
  double x = 0.0;
  double y = 0.0;
};

struct VisibilityPort {
  RoutePoint point;
  RoutePoint stub;
};

struct VisibilityRoute {
  bool found = false;
  std::vector<RoutePoint> points;
  std::size_t sourcePortIndex = 0;
  std::size_t targetPortIndex = 0;
};

struct LayoutRunMetadata {
  std::string requestedMode;
  std::string actualMode;
  std::string requestedAlgorithm;
  std::string actualAlgorithm;
  std::string strategy;
  std::string strategyReason;
};

struct LayoutQualityMetrics {
  std::size_t edgeNodeIntersections = 0;
  std::size_t edgeSegmentOverlaps = 0;
  std::size_t nodeOverlaps = 0;
  std::size_t nodeSpacingOverlaps = 0;
  std::size_t overlappingEdges = 0;
  std::size_t routeSegments = 0;
};

struct AxisRouteSegment {
  std::size_t edgeIndex = 0;
  double end = 0.0;
  double start = 0.0;
};

constexpr std::size_t kSugiyamaSurrogateNodeThreshold = 10000;
constexpr std::size_t kEnergySurrogateNodeThreshold = 10000;
constexpr std::size_t kTopologySurrogateNodeThreshold = 10000;
constexpr std::size_t kPlanarizationGridSurrogateNodeThreshold = 10000;
constexpr std::size_t kPlanarizationGridProjectionNodeThreshold = 1000;
constexpr std::size_t kDavidsonHarelReducedNodeThreshold = 10000;
constexpr int kFastMultipoleMultilevelCoarseNodeBound = 1024;
constexpr double kPlanarizationPageRatio = 1.25;
constexpr double kPlanarizationGridSeparation = 96.0;
constexpr double kTreeLevelDistance = 320.0;
constexpr double kTreeNodeDistance = 96.0;
constexpr double kTreeComponentDistance = 260.0;
constexpr double kRadialLevelDistance = 220.0;
constexpr double kRadialComponentDistance = 360.0;
constexpr double kPostLayoutNodeGapX = 56.0;
constexpr double kPostLayoutNodeGapY = 42.0;
constexpr int kOverlapRelaxationIterations = 64;
constexpr double kDistantEdgeMinThreshold = 1800.0;
constexpr double kDistantEdgeLengthFactor = 5.0;
constexpr double kDistantEdgeMinTarget = 420.0;
constexpr double kDistantEdgeMaxTarget = 1120.0;
constexpr double kDistantEdgeTargetFactor = 2.25;
constexpr double kVisibilityLaneClearance = 14.0;
constexpr double kMetricCoordinateScale = 100.0;

struct DisjointSet {
  explicit DisjointSet(std::size_t size)
    : parent(size),
      rank(size, 0) {
    for (std::size_t index = 0; index < size; ++index) {
      parent[index] = index;
    }
  }

  std::size_t find(std::size_t value) {
    if (parent[value] == value) {
      return value;
    }

    parent[value] = find(parent[value]);
    return parent[value];
  }

  bool unite(std::size_t left, std::size_t right) {
    left = find(left);
    right = find(right);

    if (left == right) {
      return false;
    }

    if (rank[left] < rank[right]) {
      std::swap(left, right);
    }

    parent[right] = left;

    if (rank[left] == rank[right]) {
      rank[left] += 1;
    }

    return true;
  }

  std::vector<std::size_t> parent;
  std::vector<std::size_t> rank;
};

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

CliArguments parseArguments(int argc, char** argv) {
  if (argc < 8) {
    throw std::runtime_error(
      "usage: django-erd-ogdf-layout layout --mode <mode> --nodes-file <path> --edges-file <path>");
  }

  CliArguments arguments;

  if (std::string(argv[1]) != "layout") {
    throw std::runtime_error("only the 'layout' subcommand is supported");
  }

  for (int index = 2; index + 1 < argc; index += 2) {
    const std::string flag = argv[index];
    const std::string value = argv[index + 1];

    if (flag == "--mode") {
      arguments.mode = value;
    } else if (flag == "--nodes-file") {
      arguments.nodesFile = value;
    } else if (flag == "--edges-file") {
      arguments.edgesFile = value;
    } else {
      throw std::runtime_error("unknown argument: " + flag);
    }
  }

  if (arguments.mode.empty() || arguments.nodesFile.empty() || arguments.edgesFile.empty()) {
    throw std::runtime_error("mode, nodes-file and edges-file are required");
  }

  if (!isSupportedMode(arguments.mode)) {
    throw std::runtime_error("unsupported mode: " + arguments.mode);
  }

  return arguments;
}

std::vector<std::string> splitTabs(const std::string& line) {
  std::vector<std::string> fields;
  std::size_t start = 0;

  while (true) {
    const std::size_t next = line.find('\t', start);
    if (next == std::string::npos) {
      fields.push_back(line.substr(start));
      break;
    }

    fields.push_back(line.substr(start, next - start));
    start = next + 1;
  }

  return fields;
}

double parseNumber(const std::string& value, const std::string& context) {
  std::size_t parsed = 0;
  const double number = std::stod(value, &parsed);

  if (parsed != value.size()) {
    throw std::runtime_error("invalid number in " + context + ": " + value);
  }

  return number;
}

std::vector<NodeRecord> readNodes(
  const std::string& filePath,
  ogdf::Graph& graph,
  ogdf::GraphAttributes& attributes,
  std::unordered_map<std::string, ogdf::node>& nodesById) {
  std::ifstream stream(filePath);

  if (!stream) {
    throw std::runtime_error("failed to open nodes file: " + filePath);
  }

  std::vector<NodeRecord> nodes;
  std::string line;

  while (std::getline(stream, line)) {
    if (line.empty()) {
      continue;
    }

    const std::vector<std::string> fields = splitTabs(line);
    if (fields.size() < 5) {
      throw std::runtime_error("invalid nodes.tsv row: " + line);
    }

    NodeRecord record;
    record.modelId = fields[0];
    record.width = parseNumber(fields[1], "nodes.tsv width");
    record.height = parseNumber(fields[2], "nodes.tsv height");
    record.x = parseNumber(fields[3], "nodes.tsv x");
    record.y = parseNumber(fields[4], "nodes.tsv y");
    record.handle = graph.newNode();

    attributes.width(record.handle) = std::max(1.0, record.width);
    attributes.height(record.handle) = std::max(1.0, record.height);
    attributes.x(record.handle) = record.x + record.width / 2.0;
    attributes.y(record.handle) = record.y + record.height / 2.0;

    nodesById.emplace(record.modelId, record.handle);
    nodes.push_back(record);
  }

  return nodes;
}

std::vector<EdgeRecord> readEdges(
  const std::string& filePath,
  ogdf::Graph& graph,
  const std::unordered_map<std::string, ogdf::node>& nodesById) {
  std::ifstream stream(filePath);

  if (!stream) {
    throw std::runtime_error("failed to open edges file: " + filePath);
  }

  std::vector<EdgeRecord> edges;
  std::unordered_map<std::string, ogdf::edge> topologyEdgesByPair;
  std::string line;

  while (std::getline(stream, line)) {
    if (line.empty()) {
      continue;
    }

    const std::vector<std::string> fields = splitTabs(line);
    if (fields.size() < 5) {
      throw std::runtime_error("invalid edges.tsv row: " + line);
    }

    const auto source = nodesById.find(fields[1]);
    const auto target = nodesById.find(fields[2]);

    if (source == nodesById.end() || target == nodesById.end()) {
      throw std::runtime_error("edge references unknown node: " + line);
    }

    if (source->second == target->second) {
      continue;
    }

    EdgeRecord record;
    record.edgeId = fields[0];
    record.sourceModelId = fields[1];
    record.targetModelId = fields[2];
    record.kind = fields[3];
    record.provenance = fields[4];
    record.sourceHandle = source->second;
    record.targetHandle = target->second;

    const std::string topologyKey = fields[1] < fields[2]
      ? fields[1] + "\t" + fields[2]
      : fields[2] + "\t" + fields[1];
    const auto existingEdge = topologyEdgesByPair.find(topologyKey);
    if (existingEdge == topologyEdgesByPair.end()) {
      record.handle = graph.newEdge(source->second, target->second);
      topologyEdgesByPair.emplace(topologyKey, record.handle);
    } else {
      record.handle = existingEdge->second;
    }

    edges.push_back(record);
  }

  return edges;
}

std::size_t idealThreadCount() {
  const unsigned int detected = std::thread::hardware_concurrency();
  return std::max<std::size_t>(1, std::min<std::size_t>(8, detected == 0 ? 1 : detected));
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

bool isFiniteCoordinate(double value) {
  return std::isfinite(value);
}

std::string nodeThresholdReason(std::size_t threshold, const std::string& detail) {
  return "nodes>=" + std::to_string(threshold) + "; " + detail;
}

double sanitizeNodeWidth(const NodeRecord& node, ogdf::GraphAttributes& attributes) {
  const double width = attributes.width(node.handle);
  return std::max(1.0, isFiniteCoordinate(width) ? width : node.width);
}

double sanitizeNodeHeight(const NodeRecord& node, ogdf::GraphAttributes& attributes) {
  const double height = attributes.height(node.handle);
  return std::max(1.0, isFiniteCoordinate(height) ? height : node.height);
}

double sanitizeNodeCenterX(const NodeRecord& node, ogdf::GraphAttributes& attributes) {
  const double width = sanitizeNodeWidth(node, attributes);
  const double fallback = node.x + width / 2.0;
  const double center = attributes.x(node.handle);
  return isFiniteCoordinate(center) ? center : fallback;
}

double sanitizeNodeCenterY(const NodeRecord& node, ogdf::GraphAttributes& attributes) {
  const double height = sanitizeNodeHeight(node, attributes);
  const double fallback = node.y + height / 2.0;
  const double center = attributes.y(node.handle);
  return isFiniteCoordinate(center) ? center : fallback;
}

Rect nodeRect(
  const NodeRecord& node,
  ogdf::GraphAttributes& attributes,
  double margin = 0.0) {
  const double width = sanitizeNodeWidth(node, attributes);
  const double height = sanitizeNodeHeight(node, attributes);
  const double centerX = sanitizeNodeCenterX(node, attributes);
  const double centerY = sanitizeNodeCenterY(node, attributes);
  return {
    centerY + height / 2.0 + margin,
    centerX - width / 2.0 - margin,
    centerX + width / 2.0 + margin,
    centerY - height / 2.0 - margin,
  };
}

Rect handleRect(
  ogdf::node handle,
  ogdf::GraphAttributes& attributes,
  double margin = 0.0) {
  const double width = std::max(1.0, attributes.width(handle));
  const double height = std::max(1.0, attributes.height(handle));
  const double centerX = attributes.x(handle);
  const double centerY = attributes.y(handle);
  return {
    centerY + height / 2.0 + margin,
    centerX - width / 2.0 - margin,
    centerX + width / 2.0 + margin,
    centerY - height / 2.0 - margin,
  };
}

double rectWidth(const Rect& rect) {
  return rect.right - rect.left;
}

double rectHeight(const Rect& rect) {
  return rect.bottom - rect.top;
}

double rectCenterX(const Rect& rect) {
  return (rect.left + rect.right) / 2.0;
}

double rectCenterY(const Rect& rect) {
  return (rect.top + rect.bottom) / 2.0;
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
  layout.runs(1);
  layout.fails(expensiveCrossMin ? 1 : 2);
  layout.transpose(!expensiveCrossMin);

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
  hierarchy->layerDistance(96.0);
  hierarchy->nodeDistance(44.0);
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
  layout.setDefaultEdgeLength(140.0f);
  layout.setDefaultNodeSize(48.0f);
  layout.setRandomize(randomize);
  layout.setNumberOfThreads(static_cast<uint32_t>(idealThreadCount()));
  layout.call(attributes);
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

  if (mode == "fast_multipole" || mode == "fast_multipole_multilevel") {
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

  return false;
}

double routeLength(const std::vector<RoutePoint>& points) {
  double length = 0.0;
  for (std::size_t index = 1; index < points.size(); ++index) {
    length += std::abs(points[index].x - points[index - 1].x)
      + std::abs(points[index].y - points[index - 1].y);
  }
  return length;
}

double routeScore(
  const std::vector<RoutePoint>& points,
  const std::vector<Rect>& obstacles) {
  double intersections = 0.0;
  for (std::size_t index = 1; index < points.size(); ++index) {
    for (const Rect& obstacle : obstacles) {
      if (segmentIntersectsRect(points[index - 1], points[index], obstacle)) {
        intersections += 1.0;
      }
    }
  }

  return intersections * 1'000'000.0
    + routeLength(points)
    + static_cast<double>(points.size()) * 20.0;
}

long long metricLaneKey(double value) {
  return static_cast<long long>(std::llround(value * kMetricCoordinateScale));
}

bool intervalsOverlap(double leftStart, double leftEnd, double rightStart, double rightEnd) {
  return std::min(leftEnd, rightEnd) - std::max(leftStart, rightStart) > 1.0;
}

void addAxisRouteSegment(
  std::unordered_map<long long, std::vector<AxisRouteSegment>>& groups,
  long long laneKey,
  std::size_t edgeIndex,
  double start,
  double end) {
  const double minAxis = std::min(start, end);
  const double maxAxis = std::max(start, end);
  if (maxAxis - minAxis <= 1.0) {
    return;
  }

  groups[laneKey].push_back({ edgeIndex, maxAxis, minAxis });
}

std::size_t countAxisSegmentOverlaps(
  std::vector<AxisRouteSegment>& segments,
  std::vector<bool>& overlappingEdgeFlags) {
  std::sort(
    segments.begin(),
    segments.end(),
    [](const AxisRouteSegment& left, const AxisRouteSegment& right) {
      if (std::abs(left.start - right.start) > 0.01) {
        return left.start < right.start;
      }
      return left.end < right.end;
    });

  std::size_t overlaps = 0;
  for (std::size_t leftIndex = 0; leftIndex < segments.size(); ++leftIndex) {
    const AxisRouteSegment& left = segments[leftIndex];
    for (std::size_t rightIndex = leftIndex + 1; rightIndex < segments.size(); ++rightIndex) {
      const AxisRouteSegment& right = segments[rightIndex];
      if (right.start >= left.end - 1.0) {
        break;
      }
      if (left.edgeIndex == right.edgeIndex) {
        continue;
      }
      if (!intervalsOverlap(left.start, left.end, right.start, right.end)) {
        continue;
      }

      overlaps += 1;
      if (left.edgeIndex < overlappingEdgeFlags.size()) {
        overlappingEdgeFlags[left.edgeIndex] = true;
      }
      if (right.edgeIndex < overlappingEdgeFlags.size()) {
        overlappingEdgeFlags[right.edgeIndex] = true;
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

LayoutQualityMetrics measureLayoutQuality(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes) {
  LayoutQualityMetrics metrics;
  metrics.nodeOverlaps = countNodeRectOverlaps(nodes, attributes, false);
  metrics.nodeSpacingOverlaps = countNodeRectOverlaps(nodes, attributes, true);

  std::unordered_map<long long, std::vector<AxisRouteSegment>> horizontalSegmentsByLane;
  std::unordered_map<long long, std::vector<AxisRouteSegment>> verticalSegmentsByLane;

  for (std::size_t edgeIndex = 0; edgeIndex < routes.size() && edgeIndex < edges.size(); ++edgeIndex) {
    const std::vector<RoutePoint>& route = routes[edgeIndex];
    if (route.size() < 2) {
      continue;
    }

    const EdgeRecord& edge = edges[edgeIndex];
    for (std::size_t pointIndex = 1; pointIndex < route.size(); ++pointIndex) {
      const RoutePoint& start = route[pointIndex - 1];
      const RoutePoint& end = route[pointIndex];
      metrics.routeSegments += 1;

      for (const NodeRecord& node : nodes) {
        if (node.handle == edge.sourceHandle || node.handle == edge.targetHandle) {
          continue;
        }
        if (segmentIntersectsRect(start, end, nodeRect(node, attributes))) {
          metrics.edgeNodeIntersections += 1;
        }
      }

      if (std::abs(start.y - end.y) < 0.01) {
        addAxisRouteSegment(
          horizontalSegmentsByLane,
          metricLaneKey(start.y),
          edgeIndex,
          start.x,
          end.x);
      } else if (std::abs(start.x - end.x) < 0.01) {
        addAxisRouteSegment(
          verticalSegmentsByLane,
          metricLaneKey(start.x),
          edgeIndex,
          start.y,
          end.y);
      }
    }
  }

  std::vector<bool> overlappingEdgeFlags(edges.size(), false);
  for (auto& entry : horizontalSegmentsByLane) {
    metrics.edgeSegmentOverlaps += countAxisSegmentOverlaps(entry.second, overlappingEdgeFlags);
  }
  for (auto& entry : verticalSegmentsByLane) {
    metrics.edgeSegmentOverlaps += countAxisSegmentOverlaps(entry.second, overlappingEdgeFlags);
  }

  metrics.overlappingEdges = static_cast<std::size_t>(
    std::count(overlappingEdgeFlags.begin(), overlappingEdgeFlags.end(), true));
  return metrics;
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
  double laneOffset) {
  constexpr std::size_t maxVisibilityLanes = 72;
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
      const double nextCost = cost + stepCost;
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

std::vector<RoutePoint> routeObstacleAwareEdge(
  const EdgeRecord& edge,
  std::size_t edgeIndex,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes) {
  const Rect graphBounds = graphNodeBounds(nodes, attributes);
  const Rect source = handleRect(edge.sourceHandle, attributes);
  const Rect target = handleRect(edge.targetHandle, attributes);
  const bool horizontal = std::abs(rectCenterX(target) - rectCenterX(source))
    >= std::abs(rectCenterY(target) - rectCenterY(source));
  const double laneOffset = (static_cast<double>(edgeIndex % 9) - 4.0) * 26.0;
  constexpr double portInset = 18.0;
  constexpr double stub = 52.0;
  constexpr double outerGap = 170.0;
  constexpr double obstacleMargin = 6.0;
  constexpr double laneGap = kVisibilityLaneClearance;

  RoutePoint start;
  RoutePoint end;
  RoutePoint startStub;
  RoutePoint endStub;
  std::vector<std::vector<RoutePoint>> candidates;
  std::vector<Rect> obstacles;
  obstacles.reserve(nodes.size());
  for (const NodeRecord& node : nodes) {
    if (node.handle == edge.sourceHandle || node.handle == edge.targetHandle) {
      continue;
    }
    obstacles.push_back(nodeRect(node, attributes, obstacleMargin));
  }

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
    for (const Rect& obstacle : obstacles) {
      if (obstacle.bottom >= minY - laneGap && obstacle.top <= maxY + laneGap) {
        addLaneValue(verticalLanes, obstacle.left - laneGap);
        addLaneValue(verticalLanes, obstacle.right + laneGap);
      }
      if (obstacle.right >= minX - laneGap && obstacle.left <= maxX + laneGap) {
        addLaneValue(horizontalLanes, obstacle.top - laneGap);
        addLaneValue(horizontalLanes, obstacle.bottom + laneGap);
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
    for (const Rect& obstacle : obstacles) {
      if (obstacle.bottom >= minY - laneGap && obstacle.top <= maxY + laneGap) {
        addLaneValue(verticalLanes, obstacle.left - laneGap);
        addLaneValue(verticalLanes, obstacle.right + laneGap);
      }
      if (obstacle.right >= minX - laneGap && obstacle.left <= maxX + laneGap) {
        addLaneValue(horizontalLanes, obstacle.top - laneGap);
        addLaneValue(horizontalLanes, obstacle.bottom + laneGap);
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
    routeVisibilityGridWithPorts(sourcePorts, targetPorts, graphBounds, obstacles, laneOffset);
  if (visibilityRoute.found && visibilityRoute.points.size() >= 2) {
    std::vector<RoutePoint> candidate;
    candidate.reserve(visibilityRoute.points.size() + 2);
    candidate.push_back(sourcePorts[visibilityRoute.sourcePortIndex].point);
    candidate.insert(candidate.end(), visibilityRoute.points.begin(), visibilityRoute.points.end());
    candidate.push_back(targetPorts[visibilityRoute.targetPortIndex].point);
    candidate = compressRoutePoints(std::move(candidate));
    if (routeScore(candidate, obstacles) < 1'000'000.0) {
      return candidate;
    }
    candidates.push_back(std::move(candidate));
  }

  const double outerTop = graphBounds.top - outerGap - std::abs(laneOffset);
  const double outerBottom = graphBounds.bottom + outerGap + std::abs(laneOffset);
  const double outerLeft = graphBounds.left - outerGap - std::abs(laneOffset);
  const double outerRight = graphBounds.right + outerGap + std::abs(laneOffset);
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
        if (routeScore(candidate, obstacles) < 1'000'000.0) {
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
    const double score = routeScore(candidate, obstacles);
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

  if (mode == "fast_multipole") {
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
      "PlanarizationLayout(boundedCrossMin=PlanarSubgraphFast(runs=1)+VariableEmbeddingInserter(removeReinsert=None),pageRatio=1.25)";
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
      "PlanarizationLayout + OrthoLayout(boundedCrossMin=PlanarSubgraphFast(runs=1)+VariableEmbeddingInserter(removeReinsert=None),pageRatio=1.25)";
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
  ogdf::GraphAttributes& attributes) {
  std::vector<std::vector<RoutePoint>> routes;
  routes.reserve(edges.size());

  for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
    routes.push_back(routeObstacleAwareEdge(edges[edgeIndex], edgeIndex, nodes, attributes));
  }

  return routes;
}

std::string escapeJson(const std::string& value) {
  std::ostringstream stream;

  for (const char character : value) {
    switch (character) {
      case '\\':
        stream << "\\\\";
        break;
      case '"':
        stream << "\\\"";
        break;
      case '\n':
        stream << "\\n";
        break;
      case '\r':
        stream << "\\r";
        break;
      case '\t':
        stream << "\\t";
        break;
      default:
        stream << character;
        break;
    }
  }

  return stream.str();
}

void writePoint(std::ostream& stream, double x, double y, const Bounds& bounds) {
  stream << "{\"x\":" << (x - bounds.minX) << ",\"y\":" << (y - bounds.minY) << "}";
}

void writeLayoutEngineMetadata(
  std::ostream& stream,
  const LayoutRunMetadata& metadata,
  const LayoutQualityMetrics& quality) {
  stream << "{\"requestedMode\":\"" << escapeJson(metadata.requestedMode)
         << "\",\"actualMode\":\"" << escapeJson(metadata.actualMode)
         << "\",\"requestedAlgorithm\":\"" << escapeJson(metadata.requestedAlgorithm)
         << "\",\"actualAlgorithm\":\"" << escapeJson(metadata.actualAlgorithm)
         << "\",\"strategy\":\"" << escapeJson(metadata.strategy)
         << "\",\"strategyReason\":\"" << escapeJson(metadata.strategyReason)
         << "\",\"nodeOverlaps\":" << quality.nodeOverlaps
         << ",\"nodeSpacingOverlaps\":" << quality.nodeSpacingOverlaps
         << ",\"edgeNodeIntersections\":" << quality.edgeNodeIntersections
         << ",\"edgeSegmentOverlaps\":" << quality.edgeSegmentOverlaps
         << ",\"overlappingEdges\":" << quality.overlappingEdges
         << ",\"routeSegments\":" << quality.routeSegments
         << "}";
}

void writeLayoutJson(
  std::ostream& stream,
  const std::string& mode,
  const LayoutRunMetadata& metadata,
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  const std::vector<std::vector<RoutePoint>>& routes,
  const LayoutQualityMetrics& quality,
  const Bounds& bounds) {
  stream << std::fixed << std::setprecision(3);
  stream << "{\"crossings\":[],\"engineMetadata\":";
  writeLayoutEngineMetadata(stream, metadata, quality);
  stream << ",\"mode\":\"" << escapeJson(mode) << "\",\"nodes\":[";

  for (std::size_t index = 0; index < nodes.size(); ++index) {
    const NodeRecord& node = nodes[index];
    const double width = sanitizeNodeWidth(node, attributes);
    const double height = sanitizeNodeHeight(node, attributes);
    const double centerX = sanitizeNodeCenterX(node, attributes);
    const double centerY = sanitizeNodeCenterY(node, attributes);
    if (index > 0) {
      stream << ",";
    }

    stream << "{\"modelId\":\"" << escapeJson(node.modelId) << "\",\"position\":";
    writePoint(
      stream,
      centerX - width / 2.0,
      centerY - height / 2.0,
      bounds);
    stream << ",\"size\":{\"width\":" << width
           << ",\"height\":" << height << "}}";
  }

  stream << "],\"routedEdges\":[";

  for (std::size_t index = 0; index < edges.size(); ++index) {
    const EdgeRecord& edge = edges[index];
    if (index > 0) {
      stream << ",";
    }

    stream << "{\"crossingIds\":[],\"edgeId\":\"" << escapeJson(edge.edgeId) << "\",\"points\":[";
    const std::vector<RoutePoint>& route = routes[index];
    for (std::size_t pointIndex = 0; pointIndex < route.size(); ++pointIndex) {
      if (pointIndex > 0) {
        stream << ",";
      }
      writePoint(stream, route[pointIndex].x, route[pointIndex].y, bounds);
    }

    stream << "]}";
  }

  stream << "]}";
}
} // namespace

int main(int argc, char** argv) {
  try {
    const CliArguments arguments = parseArguments(argc, argv);
    ogdf::Graph graph;
    ogdf::GraphAttributes attributes(
      graph,
      ogdf::GraphAttributes::nodeGraphics | ogdf::GraphAttributes::edgeGraphics);
    std::unordered_map<std::string, ogdf::node> nodesById;
    std::vector<NodeRecord> nodes = readNodes(arguments.nodesFile, graph, attributes, nodesById);
    std::vector<EdgeRecord> edges = readEdges(arguments.edgesFile, graph, nodesById);
    LayoutRunMetadata metadata = makeLayoutRunMetadata(arguments.mode);

    if (graph.numberOfNodes() > 0) {
      metadata = runLayout(arguments.mode, nodes, edges, attributes);
    }

    sanitizeLayoutGeometry(nodes, edges, attributes);
    if (compactExcessiveLayoutFootprint(arguments.mode, nodes, edges, attributes)) {
      if (!metadata.strategyReason.empty()) {
        metadata.strategyReason += "; ";
      }
      metadata.strategyReason += "post-layout footprint compaction capped oversized axes";
    }
    compactDistantConnectedNodes(nodes, edges, attributes);
    enforceNodeSeparation(nodes, attributes);
    packDisconnectedComponents(nodes, edges, attributes);
    enforceNodeSeparation(nodes, attributes);
    sanitizeLayoutGeometry(nodes, edges, attributes);
    const std::vector<std::vector<RoutePoint>> routes = routeAllEdges(nodes, edges, attributes);
    const LayoutQualityMetrics quality = measureLayoutQuality(nodes, edges, routes, attributes);
    const Bounds bounds = measureBounds(nodes, routes, attributes);
    writeLayoutJson(std::cout, arguments.mode, metadata, nodes, edges, attributes, routes, quality, bounds);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << std::endl;
    return 1;
  }
}
