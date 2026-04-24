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
#include <ogdf/planarity/PlanarizationGridLayout.h>
#include <ogdf/planarity/PlanarizationLayout.h>
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
  std::string sourceModelId;
  std::string targetModelId;
};

struct Bounds {
  double minX = 0.0;
  double minY = 0.0;
};

struct LayoutRunMetadata {
  std::string requestedMode;
  std::string actualMode;
  std::string requestedAlgorithm;
  std::string actualAlgorithm;
  std::string strategy;
  std::string strategyReason;
};

constexpr std::size_t kLargeGraphNodeThreshold = 500;
constexpr double kTreeLevelDistance = 320.0;
constexpr double kTreeNodeDistance = 96.0;
constexpr double kTreeComponentDistance = 260.0;
constexpr double kRadialLevelDistance = 220.0;
constexpr double kRadialComponentDistance = 360.0;

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
    record.handle = graph.newEdge(source->second, target->second);

    edges.push_back(record);
  }

  return edges;
}

std::size_t idealThreadCount() {
  const unsigned int detected = std::thread::hardware_concurrency();
  return std::max<std::size_t>(1, std::min<std::size_t>(8, detected == 0 ? 1 : detected));
}

bool isFiniteCoordinate(double value) {
  return std::isfinite(value);
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
  layout.setRanking(new ogdf::OptimalRanking());
  layout.runs(1);
  layout.fails(
    mode == "hierarchical_sifting"
      || mode == "hierarchical_global_sifting"
      || mode == "hierarchical_grid_sifting"
      ? 1
      : 2);
  layout.transpose(
    mode != "hierarchical_sifting"
    && mode != "hierarchical_global_sifting"
    && mode != "hierarchical_grid_sifting");

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
  } else {
    layout.setCrossMin(new ogdf::MedianHeuristic());
  }

  auto* hierarchy = new ogdf::OptimalHierarchyLayout();
  hierarchy->layerDistance(140.0);
  hierarchy->nodeDistance(96.0);
  hierarchy->weightBalancing(0.8);
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
    const auto source = indicesByNode.find(edge.handle->source());
    const auto target = indicesByNode.find(edge.handle->target());

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
    const double sourceX = attributes.x(edge.handle->source());
    const double sourceY = attributes.y(edge.handle->source());
    const double targetX = attributes.x(edge.handle->target());
    const double targetY = attributes.y(edge.handle->target());
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
      const double sourceX = attributes.x(edge.handle->source());
      const double sourceY = attributes.y(edge.handle->source());
      const double targetX = attributes.x(edge.handle->target());
      const double targetY = attributes.y(edge.handle->target());
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
    const bool largeGraph = nodes.size() >= kLargeGraphNodeThreshold;
    const bool alwaysSurrogate =
      mode == "hierarchical_global_sifting"
      || mode == "hierarchical_grid_sifting"
      || mode == "hierarchical_split";
    const bool largeGraphSurrogate =
      largeGraph
      && mode != "hierarchical"
      && mode != "hierarchical_barycenter";
    std::string actualRunMode = mode;

    if (alwaysSurrogate || largeGraphSurrogate) {
      actualRunMode =
        mode == "hierarchical_grid_sifting" || mode == "hierarchical_greedy_switch"
          ? "hierarchical"
          : "hierarchical_barycenter";
      metadata.actualMode = mode;
      metadata.strategy = largeGraphSurrogate ? "large_graph_surrogate" : "surrogate";

      if (mode == "hierarchical_sifting") {
        metadata.actualAlgorithm =
          "DjangoErdSiftingSurrogate(SugiyamaLayout + BarycenterHeuristic, layerStagger)";
        metadata.strategyReason =
          "nodes>=500; sifting cross minimization uses a bounded barycenter base plus layer staggering";
      } else if (mode == "hierarchical_global_sifting") {
        metadata.actualAlgorithm =
          "DjangoErdGlobalSiftingSurrogate(SugiyamaLayout + BarycenterHeuristic, globalLayerDrift)";
        metadata.strategyReason =
          largeGraph
            ? "nodes>=500; global sifting uses a bounded barycenter base plus global layer drift"
            : "global sifting is approximated with bounded barycenter ordering and global layer drift";
      } else if (mode == "hierarchical_greedy_insert") {
        metadata.actualAlgorithm =
          "DjangoErdGreedyInsertSurrogate(SugiyamaLayout + BarycenterHeuristic, compactInsert)";
        metadata.strategyReason =
          largeGraph
            ? "nodes>=500; greedy insert uses a bounded barycenter base plus compact insertion offsets"
            : "greedy insert is approximated with bounded barycenter ordering and compact insertion offsets";
      } else if (mode == "hierarchical_greedy_switch") {
        metadata.actualAlgorithm =
          "DjangoErdGreedySwitchSurrogate(SugiyamaLayout + MedianHeuristic, alternatingSwitch)";
        metadata.strategyReason =
          largeGraph
            ? "nodes>=500; greedy switch uses a bounded median base plus alternating layer switches"
            : "greedy switch is approximated with bounded median ordering and alternating layer switches";
      } else if (mode == "hierarchical_grid_sifting") {
        metadata.actualAlgorithm =
          "DjangoErdGridSiftingSurrogate(SugiyamaLayout + MedianHeuristic, layerGridSnap)";
        metadata.strategyReason =
          largeGraph
            ? "nodes>=500; grid sifting uses a bounded median base plus layer grid snapping"
            : "grid sifting is approximated with bounded median ordering and layer grid snapping";
      } else {
        metadata.actualAlgorithm =
          "DjangoErdSplitHeuristicSurrogate(SugiyamaLayout + BarycenterHeuristic, splitBands)";
        metadata.strategyReason =
          "SplitHeuristic is a simultaneous-drawing cross-minimizer, so ERD mode uses a bounded split-band surrogate";
      }
    } else {
      metadata.actualAlgorithm += "(runs=1)";
      metadata.strategy = "bounded";
      metadata.strategyReason = "Sugiyama runs/fails are capped for interactive layout";
    }

    runSugiyamaLayout(actualRunMode, attributes);
    if (alwaysSurrogate || largeGraphSurrogate) {
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
    if (nodes.size() >= kLargeGraphNodeThreshold) {
      runFastMultipoleLayout(attributes, 180, 4, true);
      metadata.actualMode = "fast_multipole_multilevel";
      metadata.actualAlgorithm =
        "DjangoErdFastMultipoleMultilevelSurrogate(FastMultipoleEmbedder, iterations=180, multipolePrecision=4)";
      metadata.strategy = "large_graph_surrogate";
      metadata.strategyReason =
        "nodes>=500; multilevel embedder is replaced with bounded fast multipole";
      return metadata;
    }

    ogdf::FastMultipoleMultilevelEmbedder layout;
    layout.multilevelUntilNumNodesAreLess(64);
    layout.maxNumThreads(static_cast<int>(idealThreadCount()));
    layout.call(attributes);
    metadata.actualAlgorithm = "FastMultipoleMultilevelEmbedder(minCoarseNodes=64)";
    metadata.strategy = "bounded";
    metadata.strategyReason = "coarsening threshold and thread count are capped";
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
    const bool largeGraph = nodes.size() >= kLargeGraphNodeThreshold;
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
      ? "nodes>=500; Davidson-Harel iterations and temperature are reduced"
      : "Davidson-Harel iterations are capped";
    return metadata;
  }

  if (mode == "planarization") {
    if (nodes.size() >= kLargeGraphNodeThreshold) {
      runSugiyamaLayout("hierarchical_barycenter", attributes);
      applyPlanarSurrogateGeometry(nodes, edges, attributes);
      metadata.actualMode = "planarization";
      metadata.actualAlgorithm =
        "DjangoErdPlanarizationSurrogate(SugiyamaLayout + BarycenterHeuristic, planarSkew)";
      metadata.strategy = "large_graph_surrogate";
      metadata.strategyReason =
        "nodes>=500; planarization uses a bounded Sugiyama base plus planar skewing";
      return metadata;
    }

    ogdf::PlanarizationLayout layout;
    layout.pageRatio(1.6);
    layout.call(attributes);
    return metadata;
  }

  if (mode == "planarization_grid") {
    if (nodes.size() >= kLargeGraphNodeThreshold) {
      runSugiyamaLayout("hierarchical", attributes);
      applyPlanarGridSurrogateGeometry(nodes, edges, attributes);
      metadata.actualMode = "planarization_grid";
      metadata.actualAlgorithm =
        "DjangoErdPlanarizationGridSurrogate(SugiyamaLayout + MedianHeuristic, gridSnap)";
      metadata.strategy = "large_graph_surrogate";
      metadata.strategyReason =
        "nodes>=500; planarization grid uses a bounded Sugiyama base snapped to a grid";
      return metadata;
    }

    ogdf::PlanarizationGridLayout layout;
    layout.pageRatio(1.6);
    layout.call(attributes);
    return metadata;
  }

  if (mode == "ortho") {
    if (nodes.size() >= kLargeGraphNodeThreshold) {
      runSugiyamaLayout("hierarchical", attributes);
      applyOrthogonalSurrogateGeometry(nodes, edges, attributes);
      metadata.actualMode = "ortho";
      metadata.actualAlgorithm =
        "DjangoErdOrthogonalSurrogate(SugiyamaLayout + MedianHeuristic, orthogonalGridRouting)";
      metadata.strategy = "large_graph_surrogate";
      metadata.strategyReason =
        "nodes>=500; orthogonal layout uses a bounded Sugiyama base snapped to orthogonal routing";
      return metadata;
    }

    ogdf::PlanarizationLayout layout;
    layout.setPlanarLayouter(new ogdf::OrthoLayout());
    layout.pageRatio(1.6);
    layout.call(attributes);
    metadata.actualAlgorithm = "PlanarizationLayout + OrthoLayout(pageRatio=1.6)";
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
    if (nodes.size() >= kLargeGraphNodeThreshold) {
      runSugiyamaLayout("hierarchical_barycenter", attributes);
      applyUpwardSurrogateGeometry(nodes, edges, attributes, mode == "upward_layer_based");
      metadata.actualMode = mode;
      metadata.actualAlgorithm = mode == "upward_layer_based"
        ? "DjangoErdLayerBasedUPRSurrogate(SugiyamaLayout + BarycenterHeuristic, upwardProjection)"
        : "DjangoErdUpwardPlanarizationSurrogate(SugiyamaLayout + BarycenterHeuristic, upwardProjection)";
      metadata.strategy = "large_graph_surrogate";
      metadata.strategyReason =
        "nodes>=500; upward layout uses a bounded Sugiyama base with upward projection";
      return metadata;
    }

    ogdf::UpwardPlanarizationLayout layout;
    if (mode == "upward_layer_based") {
      layout.setUPRLayout(new ogdf::LayerBasedUPRLayout());
    }
    layout.call(attributes);
    metadata.actualAlgorithm = mode == "upward_layer_based"
      ? "UpwardPlanarizationLayout + LayerBasedUPRLayout"
      : "UpwardPlanarizationLayout";
    return metadata;
  }

  if (mode == "visibility") {
    if (nodes.size() >= kLargeGraphNodeThreshold) {
      runSugiyamaLayout("hierarchical", attributes);
      applyVisibilitySurrogateGeometry(nodes, edges, attributes);
      metadata.actualMode = "visibility";
      metadata.actualAlgorithm =
        "DjangoErdVisibilitySurrogate(SugiyamaLayout + MedianHeuristic, visibilityGridRouting)";
      metadata.strategy = "large_graph_surrogate";
      metadata.strategyReason =
        "nodes>=500; visibility layout uses a bounded Sugiyama base with grid visibility routing";
      return metadata;
    }

    ogdf::VisibilityLayout layout;
    layout.setMinGridDistance(90);
    layout.call(attributes);
    metadata.actualAlgorithm = "VisibilityLayout(minGridDistance=90)";
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
    if (nodes.size() >= kLargeGraphNodeThreshold) {
      runSugiyamaLayout("hierarchical_barycenter", attributes);
      applyOrthogonalSurrogateGeometry(nodes, edges, attributes);
      metadata.actualMode = "uml_ortho";
      metadata.actualAlgorithm =
        "DjangoErdUmlOrthoSurrogate(SugiyamaLayout + BarycenterHeuristic, umlOrthogonalProjection)";
      metadata.strategy = "large_graph_surrogate";
      metadata.strategyReason =
        "nodes>=500; UML orthogonal layout uses a bounded Sugiyama base snapped to orthogonal routing";
      return metadata;
    }

    ogdf::PlanarizationLayoutUML layout;
    layout.setPlanarLayouter(new ogdf::OrthoLayoutUML());
    layout.call(attributes);
    metadata.actualAlgorithm = "PlanarizationLayoutUML + OrthoLayoutUML";
    return metadata;
  }

  if (mode == "uml_planarization") {
    if (nodes.size() >= kLargeGraphNodeThreshold) {
      runSugiyamaLayout("hierarchical_barycenter", attributes);
      applyUmlPlanarSurrogateGeometry(nodes, edges, attributes);
      metadata.actualMode = "uml_planarization";
      metadata.actualAlgorithm =
        "DjangoErdUmlPlanarizationSurrogate(SugiyamaLayout + BarycenterHeuristic, umlPlanarProjection)";
      metadata.strategy = "large_graph_surrogate";
      metadata.strategyReason =
        "nodes>=500; UML planarization uses a bounded Sugiyama base plus planar skewing";
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
  const std::vector<EdgeRecord>& edges,
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

  for (const EdgeRecord& edge : edges) {
    const double sourceX = attributes.x(edge.handle->source());
    const double sourceY = attributes.y(edge.handle->source());
    const double targetX = attributes.x(edge.handle->target());
    const double targetY = attributes.y(edge.handle->target());
    updateBounds(bounds, sourceX, sourceY, hasPoint);
    updateBounds(bounds, targetX, targetY, hasPoint);

    for (const ogdf::DPoint& bend : attributes.bends(edge.handle)) {
      updateBounds(bounds, bend.m_x, bend.m_y, hasPoint);
    }
  }

  if (!hasPoint) {
    bounds.minX = 0.0;
    bounds.minY = 0.0;
  }

  return bounds;
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

void writeLayoutEngineMetadata(std::ostream& stream, const LayoutRunMetadata& metadata) {
  stream << "{\"requestedMode\":\"" << escapeJson(metadata.requestedMode)
         << "\",\"actualMode\":\"" << escapeJson(metadata.actualMode)
         << "\",\"requestedAlgorithm\":\"" << escapeJson(metadata.requestedAlgorithm)
         << "\",\"actualAlgorithm\":\"" << escapeJson(metadata.actualAlgorithm)
         << "\",\"strategy\":\"" << escapeJson(metadata.strategy)
         << "\",\"strategyReason\":\"" << escapeJson(metadata.strategyReason)
         << "\"}";
}

void writeLayoutJson(
  std::ostream& stream,
  const std::string& mode,
  const LayoutRunMetadata& metadata,
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  const Bounds& bounds) {
  stream << std::fixed << std::setprecision(3);
  stream << "{\"crossings\":[],\"engineMetadata\":";
  writeLayoutEngineMetadata(stream, metadata);
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
    bool wrotePoint = false;

    writePoint(
      stream,
      attributes.x(edge.handle->source()),
      attributes.y(edge.handle->source()),
      bounds);
    wrotePoint = true;

    for (const ogdf::DPoint& bend : attributes.bends(edge.handle)) {
      stream << ",";
      writePoint(stream, bend.m_x, bend.m_y, bounds);
    }

    const double targetX = attributes.x(edge.handle->target());
    const double targetY = attributes.y(edge.handle->target());
    const bool hasTerminalBend = !attributes.bends(edge.handle).empty()
      && attributes.bends(edge.handle).back().m_x == targetX
      && attributes.bends(edge.handle).back().m_y == targetY;

    if (!hasTerminalBend) {
      if (wrotePoint) {
        stream << ",";
      }
      writePoint(stream, targetX, targetY, bounds);
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
    const Bounds bounds = measureBounds(nodes, edges, attributes);
    writeLayoutJson(std::cout, arguments.mode, metadata, nodes, edges, attributes, bounds);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << std::endl;
    return 1;
  }
}
