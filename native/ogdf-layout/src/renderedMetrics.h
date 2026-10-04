#pragma once

#include "types.h"

#include <ogdf/basic/GraphAttributes.h>

namespace djerd {

bool applyRenderedCarrierMetricsIfRequested(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes,
  const std::unordered_map<std::string, std::string>& clusterByModelIdFull,
  LayoutRunMetadata& metadata,
  LayoutQualityMetrics& quality,
  std::size_t totalRouteCrossings,
  bool quiet = false,
  bool optimizeGeometry = true);

bool clearRenderedCarrierNodeIntersectionsIfRequested(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes,
  const std::unordered_map<std::string, std::string>& clusterByModelIdFull,
  LayoutRunMetadata& metadata);

bool applyFinalCarrierMetricsIfRequested(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes,
  const std::unordered_map<std::string, std::string>& clusterByModelIdFull,
  LayoutRunMetadata& metadata,
  LayoutQualityMetrics& quality,
  std::size_t totalRouteCrossings,
  bool quiet = false,
  bool optimizeGeometry = true);

bool repairCanonicalRouteObstaclesIfRequested(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes,
  const std::unordered_map<std::string, std::string>& clusterByModelIdFull,
  const LayoutRunMetadata& metadata);

}  // namespace djerd
