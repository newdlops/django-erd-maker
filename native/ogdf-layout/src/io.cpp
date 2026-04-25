#include "io.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

#include "geometry.h"

namespace djerd {

namespace {

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
         << ",\"edgeCrossings\":" << quality.edgeCrossings
         << ",\"edgeNodeIntersections\":" << quality.edgeNodeIntersections
         << ",\"edgeSegmentOverlaps\":" << quality.edgeSegmentOverlaps
         << ",\"overlappingEdges\":" << quality.overlappingEdges
         << ",\"routeSegments\":" << quality.routeSegments
         << ",\"boundingBoxArea\":" << quality.boundingBoxArea
         << ",\"aspectRatio\":" << quality.aspectRatio
         << ",\"meanEdgeLength\":" << quality.meanEdgeLength
         << ",\"edgeLengthStddev\":" << quality.edgeLengthStddev
         << "}";
}

}  // namespace

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
    } else if (flag == "--edge-routing") {
      if (value != "straight" && value != "straight_smart" && value != "orthogonal") {
        throw std::runtime_error("--edge-routing must be 'straight', 'straight_smart', or 'orthogonal'");
      }
      arguments.edgeRouting = value;
    } else {
      throw std::runtime_error("unknown argument: " + flag);
    }
  }

  if (arguments.mode.empty() || arguments.nodesFile.empty() || arguments.edgesFile.empty()) {
    throw std::runtime_error("mode, nodes-file and edges-file are required");
  }

  return arguments;
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

void writeLayoutJson(
  std::ostream& stream,
  const std::string& mode,
  const LayoutRunMetadata& metadata,
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  const std::vector<std::vector<RoutePoint>>& routes,
  const std::vector<EdgeCrossingRecord>& crossings,
  const std::vector<std::vector<std::string>>& crossingIdsByEdge,
  const LayoutQualityMetrics& quality,
  const Bounds& bounds) {
  stream << std::fixed << std::setprecision(3);
  stream << "{\"crossings\":[";
  for (std::size_t index = 0; index < crossings.size(); ++index) {
    const EdgeCrossingRecord& crossing = crossings[index];
    if (index > 0) {
      stream << ",";
    }
    stream << "{\"edgeIds\":[\"" << escapeJson(crossing.leftEdgeId)
           << "\",\"" << escapeJson(crossing.rightEdgeId)
           << "\"],\"id\":\"" << escapeJson(crossing.id)
           << "\",\"markerStyle\":\"bridge\",\"position\":";
    writePoint(stream, crossing.position.x, crossing.position.y, bounds);
    stream << "}";
  }

  stream << "],\"engineMetadata\":";
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

    stream << "{\"crossingIds\":[";
    if (index < crossingIdsByEdge.size()) {
      const std::vector<std::string>& crossingIds = crossingIdsByEdge[index];
      for (std::size_t crossingIndex = 0; crossingIndex < crossingIds.size(); ++crossingIndex) {
        if (crossingIndex > 0) {
          stream << ",";
        }
        stream << "\"" << escapeJson(crossingIds[crossingIndex]) << "\"";
      }
    }
    stream << "],\"edgeId\":\"" << escapeJson(edge.edgeId) << "\",\"points\":[";
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

}  // namespace djerd
