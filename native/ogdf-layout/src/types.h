#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ogdf/basic/Graph.h>

namespace djerd {

struct CliArguments {
  std::string edgeRouting = "orthogonal";
  std::string edgesFile;
  std::string mode;
  std::string nodesFile;
};

struct NodeRecord {
  std::string modelId;
  std::string appLabel;
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

struct NodeObstacle {
  ogdf::node handle = nullptr;
  std::string nodeId;
  Rect rect;
};

struct LineIntent {
  std::size_t lineIndex = 0;
  std::string lineId;
  double laneOffset = 0.0;
  bool prefersHorizontal = true;
  ogdf::node sourceHandle = nullptr;
  std::string sourceNodeId;
  Rect sourceRect;
  ogdf::node targetHandle = nullptr;
  std::string targetNodeId;
  Rect targetRect;
};

struct LineSegment {
  double axisEnd = 0.0;
  double axisStart = 0.0;
  bool horizontal = false;
  long long laneKey = 0;
  std::size_t lineIndex = 0;
  std::string lineId;
  RoutePoint end;
  RoutePoint start;
  bool vertical = false;
};

struct EdgeCrossingRecord {
  std::string id;
  std::string leftEdgeId;
  RoutePoint position;
  std::string rightEdgeId;
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
  std::unordered_map<std::string, std::string> clusterByModelId;
};

struct LayoutQualityMetrics {
  std::size_t edgeCrossings = 0;
  std::size_t edgeNodeIntersections = 0;
  std::size_t edgeSegmentOverlaps = 0;
  std::size_t nodeOverlaps = 0;
  std::size_t nodeSpacingOverlaps = 0;
  std::size_t overlappingEdges = 0;
  std::size_t routeSegments = 0;
  double aspectRatio = 0.0;
  double boundingBoxArea = 0.0;
  double edgeLengthStddev = 0.0;
  double meanEdgeLength = 0.0;
};

struct RouteOccupancy {
  std::unordered_map<long long, std::vector<LineSegment>> horizontalSegmentsByLane;
  std::unordered_map<long long, std::vector<LineSegment>> verticalSegmentsByLane;
};

constexpr std::size_t kSugiyamaSurrogateNodeThreshold = 10000;
constexpr std::size_t kEnergySurrogateNodeThreshold = 10000;
constexpr std::size_t kTopologySurrogateNodeThreshold = 10000;
constexpr std::size_t kPlanarizationGridSurrogateNodeThreshold = 10000;
constexpr std::size_t kPlanarizationGridProjectionNodeThreshold = 1000;
constexpr std::size_t kDavidsonHarelReducedNodeThreshold = 10000;
constexpr int kFastMultipoleMultilevelCoarseNodeBound = 1024;
constexpr double kPlanarizationPageRatio = 1.0;
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
constexpr std::size_t kMaxReportedCrossings = 5000;
constexpr double kRoutingObstacleMargin = 14.0;

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

}  // namespace djerd
