#pragma once
#include <ogdf/basic/Graph.h>
#include <ogdf/basic/GraphAttributes.h>
#include <unordered_set>
#include <vector>

namespace djerd {
struct NodeRecord;
struct ClusterGraphResult;
void minimizeClusterSupergraphCrossings(
  ogdf::Graph& super, ogdf::GraphAttributes& superAttr,
  const std::unordered_set<ogdf::node>& clusterSnSet, bool skipCgPositioning);
void orientClusterGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<std::vector<std::size_t>>& adj,
  const ClusterGraphResult& result, ogdf::GraphAttributes& attributes,
  bool skipCgPositioning);
}  // namespace djerd
