#pragma once

#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <ogdf/basic/Graph.h>
#include <ogdf/basic/GraphAttributes.h>

#include "types.h"

namespace djerd {

CliArguments parseArguments(int argc, char** argv);

std::vector<NodeRecord> readNodes(
  const std::string& filePath,
  ogdf::Graph& graph,
  ogdf::GraphAttributes& attributes,
  std::unordered_map<std::string, ogdf::node>& nodesById);

std::vector<EdgeRecord> readEdges(
  const std::string& filePath,
  ogdf::Graph& graph,
  const std::unordered_map<std::string, ogdf::node>& nodesById);

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
  const Bounds& bounds);

}  // namespace djerd
