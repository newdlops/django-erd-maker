#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <ogdf/basic/GraphAttributes.h>

#include "types.h"

namespace djerd {

// The canonical graph contains one representative for each unordered pair of
// model ids. The representative is the input edge with the lexicographically
// smallest edge id (and the smallest input index if malformed input contains
// duplicate ids).
struct CanonicalRouteStatus {
  std::size_t canonicalEdgeIndex = 0;
  std::size_t inputEdgeIndex = 0;
  std::string edgeId;
  std::string endpointA;
  std::string endpointB;
  std::size_t pointCount = 0;
  std::size_t segmentCount = 0;
  bool finite = false;
  bool endpointsAttached = false;
  bool complete = false;
};

struct CanonicalProperCrossing {
  std::size_t leftCanonicalEdgeIndex = 0;
  std::size_t rightCanonicalEdgeIndex = 0;
  std::string leftEdgeId;
  std::string rightEdgeId;
  RoutePoint position;
};

// One record per unordered canonical edge pair. A pair can have more than one
// proper crossing point when the supplied polylines are not a simple drawing.
struct CanonicalCrossingEdgePair {
  std::size_t leftCanonicalEdgeIndex = 0;
  std::size_t rightCanonicalEdgeIndex = 0;
  std::string leftEdgeId;
  std::string rightEdgeId;
  std::size_t properCrossingPointCount = 0;
};

// Non-incident node hits are deduplicated per canonical edge/node pair even if
// several segments of that route enter the same node rectangle.
struct CanonicalEdgeNodeHit {
  std::size_t canonicalEdgeIndex = 0;
  std::string edgeId;
  std::string nodeId;
};

struct CanonicalCrossingMetrics {
  std::size_t inputEdgeCount = 0;
  std::size_t canonicalEdgeCount = 0;
  std::size_t duplicateEdgeCount = 0;
  std::size_t ignoredSelfLoopCount = 0;
  std::size_t invalidEdgeRecordCount = 0;
  std::size_t canonicalSegmentCount = 0;
  std::size_t completeRouteCount = 0;

  // A non-zero invariant count means the geometry is still inspected where
  // safe, but none of its crossing counts should be used as a certified upper
  // bound. Examples include route/edge cardinality mismatch, unknown handles,
  // duplicate node ids, non-finite node rectangles, and duplicate edge ids.
  std::size_t invariantViolationCount = 0;
  std::size_t degenerateSegmentCount = 0;

  std::vector<CanonicalRouteStatus> canonicalRoutes;
  std::vector<CanonicalProperCrossing> properCrossingPoints;
  std::vector<CanonicalCrossingEdgePair> crossingEdgePairs;
  std::vector<CanonicalEdgeNodeHit> nonIncidentNodeHits;

  // Counts below are segment-pair events. Categories can overlap: for
  // example, a collinear overlap within one route is both an overlap and a
  // self-intersection.
  std::size_t collinearOverlapCount = 0;
  std::size_t nonProperContactCount = 0;
  std::size_t selfIntersectionCount = 0;
  std::size_t adjacentEdgeIntersectionCount = 0;

  bool allCanonicalRoutesComplete = false;
  bool properDrawing = false;
};

// Runs in O(S^2 + S*N), where S is the number of finite, non-degenerate
// canonical route segments and N is the number of usable node rectangles.
// The function is read-only even though GraphAttributes' geometry accessors
// require a non-const reference in the OGDF version used by this project.
[[nodiscard]] CanonicalCrossingMetrics measureCanonicalCrossingMetrics(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes);

}  // namespace djerd
