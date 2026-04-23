#include <ogdf/basic/Graph.h>
#include <ogdf/basic/GraphAttributes.h>
#include <ogdf/energybased/FMMMLayout.h>
#include <ogdf/energybased/fmmm/FMMMOptions.h>
#include <ogdf/layered/MedianHeuristic.h>
#include <ogdf/layered/OptimalHierarchyLayout.h>
#include <ogdf/layered/OptimalRanking.h>
#include <ogdf/layered/SugiyamaLayout.h>
#include <ogdf/misclayout/CircularLayout.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
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

  if (arguments.mode != "hierarchical" && arguments.mode != "circular" && arguments.mode != "clustered") {
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

void runLayout(const std::string& mode, ogdf::GraphAttributes& attributes) {
  if (mode == "hierarchical") {
    ogdf::SugiyamaLayout layout;
    layout.setRanking(new ogdf::OptimalRanking());
    layout.setCrossMin(new ogdf::MedianHeuristic());
    auto* hierarchy = new ogdf::OptimalHierarchyLayout();
    hierarchy->layerDistance(140.0);
    hierarchy->nodeDistance(80.0);
    hierarchy->weightBalancing(0.8);
    layout.setLayout(hierarchy);
    layout.arrangeCCs(true);
    layout.call(attributes);
    return;
  }

  if (mode == "circular") {
    ogdf::CircularLayout layout;
    layout.minDistCircle(80.0);
    layout.minDistCC(80.0);
    layout.minDistLevel(80.0);
    layout.minDistSibling(40.0);
    layout.call(attributes);
    return;
  }

  ogdf::FMMMLayout layout;
  layout.useHighLevelOptions(true);
  layout.unitEdgeLength(120.0);
  layout.newInitialPlacement(true);
  layout.qualityVersusSpeed(ogdf::FMMMOptions::QualityVsSpeed::BeautifulAndFast);
  layout.call(attributes);
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
    updateBounds(
      bounds,
      attributes.x(node.handle) - attributes.width(node.handle) / 2.0,
      attributes.y(node.handle) - attributes.height(node.handle) / 2.0,
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

void writeLayoutJson(
  std::ostream& stream,
  const std::string& mode,
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  const Bounds& bounds) {
  stream << std::fixed << std::setprecision(3);
  stream << "{\"crossings\":[],\"mode\":\"" << escapeJson(mode) << "\",\"nodes\":[";

  for (std::size_t index = 0; index < nodes.size(); ++index) {
    const NodeRecord& node = nodes[index];
    if (index > 0) {
      stream << ",";
    }

    stream << "{\"modelId\":\"" << escapeJson(node.modelId) << "\",\"position\":";
    writePoint(
      stream,
      attributes.x(node.handle) - attributes.width(node.handle) / 2.0,
      attributes.y(node.handle) - attributes.height(node.handle) / 2.0,
      bounds);
    stream << ",\"size\":{\"width\":" << attributes.width(node.handle)
           << ",\"height\":" << attributes.height(node.handle) << "}}";
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

    if (graph.numberOfNodes() > 0) {
      runLayout(arguments.mode, attributes);
    }

    const Bounds bounds = measureBounds(nodes, edges, attributes);
    writeLayoutJson(std::cout, arguments.mode, nodes, edges, attributes, bounds);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << std::endl;
    return 1;
  }
}
