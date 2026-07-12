#include "canonicalCrossingMetrics.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <unordered_set>
#include <utility>

#include "geometry.h"

namespace djerd {
namespace {

constexpr double kCoordinateEpsilon = 1e-7;
constexpr double kParameterEpsilon = 1e-9;
constexpr double kAttachmentTolerance = 0.05;

struct EndpointKey {
  std::string first;
  std::string second;

  bool operator<(const EndpointKey& other) const {
    return std::tie(first, second) < std::tie(other.first, other.second);
  }
};

EndpointKey makeEndpointKey(const EdgeRecord& edge) {
  if (edge.sourceModelId < edge.targetModelId) {
    return {edge.sourceModelId, edge.targetModelId};
  }
  return {edge.targetModelId, edge.sourceModelId};
}

struct NodeGeometry {
  const NodeRecord* record = nullptr;
  Rect rect;
  bool usable = false;
};

struct CanonicalEdgeGeometry {
  std::size_t inputEdgeIndex = 0;
  EndpointKey endpoints;
};

struct Segment {
  std::size_t canonicalEdgeIndex = 0;
  std::size_t routeSegmentIndex = 0;
  RoutePoint start;
  RoutePoint end;
  double minX = 0.0;
  double minY = 0.0;
  double maxX = 0.0;
  double maxY = 0.0;
};

struct RouteGeometry {
  const std::vector<RoutePoint>* points = nullptr;
  bool frontAtSource = false;
  bool frontAtTarget = false;
  bool backAtSource = false;
  bool backAtTarget = false;
};

enum class SegmentIntersectionKind {
  none,
  proper,
  pointContact,
  collinearOverlap,
};

struct SegmentIntersection {
  SegmentIntersectionKind kind = SegmentIntersectionKind::none;
  RoutePoint position;
};

bool finitePoint(const RoutePoint& point) {
  return isFiniteCoordinate(point.x) && isFiniteCoordinate(point.y);
}

double coordinateScale(const RoutePoint& left, const RoutePoint& right) {
  return std::max({
    1.0,
    std::abs(left.x),
    std::abs(left.y),
    std::abs(right.x),
    std::abs(right.y),
  });
}

bool samePoint(const RoutePoint& left, const RoutePoint& right) {
  const double tolerance =
    kCoordinateEpsilon * coordinateScale(left, right);
  return std::abs(left.x - right.x) <= tolerance
    && std::abs(left.y - right.y) <= tolerance;
}

bool finiteRect(const Rect& rect) {
  return isFiniteCoordinate(rect.left)
    && isFiniteCoordinate(rect.right)
    && isFiniteCoordinate(rect.top)
    && isFiniteCoordinate(rect.bottom)
    && rect.left <= rect.right
    && rect.top <= rect.bottom;
}

bool pointInRect(
  const RoutePoint& point,
  const Rect& rect,
  double tolerance = 0.0) {
  return point.x >= rect.left - tolerance
    && point.x <= rect.right + tolerance
    && point.y >= rect.top - tolerance
    && point.y <= rect.bottom + tolerance;
}

double cross(double ax, double ay, double bx, double by) {
  return ax * by - ay * bx;
}

double dot(double ax, double ay, double bx, double by) {
  return ax * bx + ay * by;
}

bool boxesOverlap(const Segment& left, const Segment& right) {
  const double scale = std::max({
    1.0,
    std::abs(left.minX),
    std::abs(left.maxX),
    std::abs(left.minY),
    std::abs(left.maxY),
    std::abs(right.minX),
    std::abs(right.maxX),
    std::abs(right.minY),
    std::abs(right.maxY),
  });
  const double tolerance = kCoordinateEpsilon * scale;
  return left.maxX + tolerance >= right.minX
    && right.maxX + tolerance >= left.minX
    && left.maxY + tolerance >= right.minY
    && right.maxY + tolerance >= left.minY;
}

SegmentIntersection intersectSegments(
  const Segment& left,
  const Segment& right) {
  if (!boxesOverlap(left, right)) {
    return {};
  }

  const double rx = left.end.x - left.start.x;
  const double ry = left.end.y - left.start.y;
  const double sx = right.end.x - right.start.x;
  const double sy = right.end.y - right.start.y;
  const double qpx = right.start.x - left.start.x;
  const double qpy = right.start.y - left.start.y;
  const double rLength = std::hypot(rx, ry);
  const double sLength = std::hypot(sx, sy);
  const double denominator = cross(rx, ry, sx, sy);
  const double parallelTolerance =
    kCoordinateEpsilon * std::max(1.0, rLength * sLength);

  if (std::abs(denominator) > parallelTolerance) {
    const double t = cross(qpx, qpy, sx, sy) / denominator;
    const double u = cross(qpx, qpy, rx, ry) / denominator;
    if (t < -kParameterEpsilon || t > 1.0 + kParameterEpsilon
        || u < -kParameterEpsilon || u > 1.0 + kParameterEpsilon) {
      return {};
    }

    SegmentIntersection result;
    result.position = {
      left.start.x + std::max(0.0, std::min(1.0, t)) * rx,
      left.start.y + std::max(0.0, std::min(1.0, t)) * ry,
    };
    const bool leftInterior =
      t > kParameterEpsilon && t < 1.0 - kParameterEpsilon;
    const bool rightInterior =
      u > kParameterEpsilon && u < 1.0 - kParameterEpsilon;
    result.kind = leftInterior && rightInterior
      ? SegmentIntersectionKind::proper
      : SegmentIntersectionKind::pointContact;
    return result;
  }

  const double displacementLength = std::hypot(qpx, qpy);
  const double collinearTolerance =
    kCoordinateEpsilon * std::max(1.0, rLength * displacementLength);
  if (std::abs(cross(qpx, qpy, rx, ry)) > collinearTolerance) {
    return {};
  }

  const double rLengthSquared = dot(rx, ry, rx, ry);
  if (rLengthSquared <= 0.0) {
    return {};
  }
  const double t0 = dot(qpx, qpy, rx, ry) / rLengthSquared;
  const double t1 = t0 + dot(sx, sy, rx, ry) / rLengthSquared;
  const double overlapStart = std::max(0.0, std::min(t0, t1));
  const double overlapEnd = std::min(1.0, std::max(t0, t1));
  if (overlapEnd < overlapStart - kParameterEpsilon) {
    return {};
  }

  SegmentIntersection result;
  const double midpoint = std::max(
    0.0,
    std::min(1.0, (overlapStart + overlapEnd) / 2.0));
  result.position = {
    left.start.x + midpoint * rx,
    left.start.y + midpoint * ry,
  };
  result.kind = overlapEnd > overlapStart + kParameterEpsilon
    ? SegmentIntersectionKind::collinearOverlap
    : SegmentIntersectionKind::pointContact;
  return result;
}

bool segmentIntersectsRect(const Segment& segment, const Rect& rect) {
  const double scale = std::max({
    1.0,
    std::abs(segment.start.x),
    std::abs(segment.start.y),
    std::abs(segment.end.x),
    std::abs(segment.end.y),
    std::abs(rect.left),
    std::abs(rect.right),
    std::abs(rect.top),
    std::abs(rect.bottom),
  });
  const double tolerance = kCoordinateEpsilon * scale;
  const double left = rect.left - tolerance;
  const double right = rect.right + tolerance;
  const double top = rect.top - tolerance;
  const double bottom = rect.bottom + tolerance;
  if (pointInRect(segment.start, {bottom, left, right, top})
      || pointInRect(segment.end, {bottom, left, right, top})) {
    return true;
  }

  // Liang-Barsky clipping against the closed rectangle.
  const double dx = segment.end.x - segment.start.x;
  const double dy = segment.end.y - segment.start.y;
  double lower = 0.0;
  double upper = 1.0;
  const auto clip = [&lower, &upper](double p, double q) {
    if (std::abs(p) <= kCoordinateEpsilon) {
      return q >= 0.0;
    }
    const double ratio = q / p;
    if (p < 0.0) {
      if (ratio > upper) return false;
      lower = std::max(lower, ratio);
    } else {
      if (ratio < lower) return false;
      upper = std::min(upper, ratio);
    }
    return lower <= upper;
  };

  return clip(-dx, segment.start.x - left)
    && clip(dx, right - segment.start.x)
    && clip(-dy, segment.start.y - top)
    && clip(dy, bottom - segment.start.y);
}

bool sharesGraphEndpoint(
  const CanonicalEdgeGeometry& left,
  const CanonicalEdgeGeometry& right,
  std::string& sharedModelId) {
  if (left.endpoints.first == right.endpoints.first
      || left.endpoints.first == right.endpoints.second) {
    sharedModelId = left.endpoints.first;
    return true;
  }
  if (left.endpoints.second == right.endpoints.first
      || left.endpoints.second == right.endpoints.second) {
    sharedModelId = left.endpoints.second;
    return true;
  }
  return false;
}

bool routeTouchesSharedEndpoint(
  const RouteGeometry& route,
  const EdgeRecord& edge,
  const std::string& sharedModelId,
  const RoutePoint& contact) {
  if (route.points == nullptr || route.points->empty()) {
    return false;
  }
  const RoutePoint& front = route.points->front();
  const RoutePoint& back = route.points->back();
  if (edge.sourceModelId == sharedModelId) {
    return (route.frontAtSource && samePoint(front, contact))
      || (route.backAtSource && samePoint(back, contact));
  }
  if (edge.targetModelId == sharedModelId) {
    return (route.frontAtTarget && samePoint(front, contact))
      || (route.backAtTarget && samePoint(back, contact));
  }
  return false;
}

bool expectedAdjacentSegmentContact(
  const Segment& left,
  const Segment& right,
  const SegmentIntersection& intersection) {
  if (left.canonicalEdgeIndex != right.canonicalEdgeIndex
      || left.routeSegmentIndex + 1 != right.routeSegmentIndex
      || intersection.kind != SegmentIntersectionKind::pointContact) {
    return false;
  }
  return samePoint(left.end, right.start)
    && samePoint(intersection.position, left.end);
}

}  // namespace

CanonicalCrossingMetrics measureCanonicalCrossingMetrics(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes) {
  CanonicalCrossingMetrics metrics;
  metrics.inputEdgeCount = edges.size();
  if (routes.size() != edges.size()) {
    ++metrics.invariantViolationCount;
  }

  const ogdf::Graph& graph = attributes.constGraph();
  std::unordered_set<ogdf::node> graphNodes;
  std::unordered_set<ogdf::edge> graphEdges;
  graphNodes.reserve(static_cast<std::size_t>(graph.numberOfNodes()));
  graphEdges.reserve(static_cast<std::size_t>(graph.numberOfEdges()));
  for (ogdf::node handle : graph.nodes) {
    graphNodes.insert(handle);
  }
  for (ogdf::edge handle : graph.edges) {
    graphEdges.insert(handle);
  }

  std::map<std::string, NodeGeometry> nodesById;
  std::unordered_set<ogdf::node> representedNodeHandles;
  representedNodeHandles.reserve(nodes.size());
  for (const NodeRecord& node : nodes) {
    if (node.modelId.empty() || node.handle == nullptr
        || graphNodes.count(node.handle) == 0) {
      ++metrics.invariantViolationCount;
      continue;
    }
    if (!representedNodeHandles.insert(node.handle).second) {
      ++metrics.invariantViolationCount;
    }
    if (nodesById.count(node.modelId) != 0) {
      ++metrics.invariantViolationCount;
      continue;
    }
    NodeGeometry geometry;
    geometry.record = &node;
    geometry.rect = nodeRect(node, attributes);
    geometry.usable = finiteRect(geometry.rect);
    if (!geometry.usable) {
      ++metrics.invariantViolationCount;
    }
    nodesById.emplace(node.modelId, geometry);
  }
  for (ogdf::node handle : graph.nodes) {
    if (representedNodeHandles.count(handle) == 0) {
      ++metrics.invariantViolationCount;
    }
  }

  std::map<EndpointKey, std::vector<std::size_t>> edgeIndicesByEndpoints;
  std::set<std::string> seenEdgeIds;
  for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
    const EdgeRecord& edge = edges[edgeIndex];
    bool validRecord = true;
    if (edge.edgeId.empty() || !seenEdgeIds.insert(edge.edgeId).second) {
      ++metrics.invariantViolationCount;
      validRecord = false;
    }
    if (edge.sourceModelId.empty() || edge.targetModelId.empty()) {
      ++metrics.invariantViolationCount;
      ++metrics.invalidEdgeRecordCount;
      continue;
    }
    if (edge.sourceModelId == edge.targetModelId) {
      ++metrics.ignoredSelfLoopCount;
      continue;
    }

    const auto sourceNode = nodesById.find(edge.sourceModelId);
    const auto targetNode = nodesById.find(edge.targetModelId);
    if (sourceNode == nodesById.end() || targetNode == nodesById.end()
        || !sourceNode->second.usable || !targetNode->second.usable) {
      ++metrics.invariantViolationCount;
      validRecord = false;
    }
    if (edge.sourceHandle == nullptr || edge.targetHandle == nullptr
        || graphNodes.count(edge.sourceHandle) == 0
        || graphNodes.count(edge.targetHandle) == 0) {
      ++metrics.invariantViolationCount;
      validRecord = false;
    } else {
      if (sourceNode != nodesById.end()
          && sourceNode->second.record->handle != edge.sourceHandle) {
        ++metrics.invariantViolationCount;
        validRecord = false;
      }
      if (targetNode != nodesById.end()
          && targetNode->second.record->handle != edge.targetHandle) {
        ++metrics.invariantViolationCount;
        validRecord = false;
      }
    }
    if (edge.handle == nullptr || graphEdges.count(edge.handle) == 0) {
      ++metrics.invariantViolationCount;
      validRecord = false;
    } else {
      const bool handleEndpointsMatch =
        (edge.handle->source() == edge.sourceHandle
          && edge.handle->target() == edge.targetHandle)
        || (edge.handle->source() == edge.targetHandle
          && edge.handle->target() == edge.sourceHandle);
      if (!handleEndpointsMatch) {
        ++metrics.invariantViolationCount;
        validRecord = false;
      }
    }
    if (!validRecord) {
      ++metrics.invalidEdgeRecordCount;
    }
    // Retain malformed-but-keyable records for conservative route accounting.
    // invariantViolationCount prevents their measurements from being certified.
    edgeIndicesByEndpoints[makeEndpointKey(edge)].push_back(edgeIndex);
  }

  std::vector<CanonicalEdgeGeometry> canonicalEdges;
  canonicalEdges.reserve(edgeIndicesByEndpoints.size());
  for (const auto& entry : edgeIndicesByEndpoints) {
    const std::vector<std::size_t>& indices = entry.second;
    if (indices.size() > 1) {
      metrics.duplicateEdgeCount += indices.size() - 1;
    }
    const auto representative = std::min_element(
      indices.begin(),
      indices.end(),
      [&edges](std::size_t left, std::size_t right) {
        if (edges[left].edgeId != edges[right].edgeId) {
          return edges[left].edgeId < edges[right].edgeId;
        }
        return left < right;
      });
    canonicalEdges.push_back({*representative, entry.first});
  }
  metrics.canonicalEdgeCount = canonicalEdges.size();
  if (canonicalEdges.size() != static_cast<std::size_t>(graph.numberOfEdges())) {
    ++metrics.invariantViolationCount;
  }

  std::vector<RouteGeometry> routeGeometry(canonicalEdges.size());
  std::vector<Segment> segments;
  std::vector<std::pair<std::size_t, std::size_t>> segmentRangeByCanonicalEdge(
    canonicalEdges.size(),
    {0, 0});
  for (std::size_t canonicalIndex = 0;
       canonicalIndex < canonicalEdges.size();
       ++canonicalIndex) {
    const CanonicalEdgeGeometry& canonical = canonicalEdges[canonicalIndex];
    const EdgeRecord& edge = edges[canonical.inputEdgeIndex];
    CanonicalRouteStatus status;
    status.canonicalEdgeIndex = canonicalIndex;
    status.inputEdgeIndex = canonical.inputEdgeIndex;
    status.edgeId = edge.edgeId;
    status.endpointA = canonical.endpoints.first;
    status.endpointB = canonical.endpoints.second;
    const std::size_t segmentBegin = segments.size();

    if (canonical.inputEdgeIndex < routes.size()) {
      const std::vector<RoutePoint>& route = routes[canonical.inputEdgeIndex];
      RouteGeometry& geometry = routeGeometry[canonicalIndex];
      geometry.points = &route;
      status.pointCount = route.size();
      status.finite = route.size() >= 2
        && std::all_of(route.begin(), route.end(), finitePoint);

      const auto sourceNode = nodesById.find(edge.sourceModelId);
      const auto targetNode = nodesById.find(edge.targetModelId);
      if (status.finite
          && sourceNode != nodesById.end() && sourceNode->second.usable
          && targetNode != nodesById.end() && targetNode->second.usable) {
        geometry.frontAtSource = pointInRect(
          route.front(), sourceNode->second.rect, kAttachmentTolerance);
        geometry.frontAtTarget = pointInRect(
          route.front(), targetNode->second.rect, kAttachmentTolerance);
        geometry.backAtSource = pointInRect(
          route.back(), sourceNode->second.rect, kAttachmentTolerance);
        geometry.backAtTarget = pointInRect(
          route.back(), targetNode->second.rect, kAttachmentTolerance);
        status.endpointsAttached =
          (geometry.frontAtSource && geometry.backAtTarget)
          || (geometry.frontAtTarget && geometry.backAtSource);
      }

      if (route.size() >= 2) {
        for (std::size_t pointIndex = 1;
             pointIndex < route.size();
             ++pointIndex) {
          const RoutePoint& start = route[pointIndex - 1];
          const RoutePoint& end = route[pointIndex];
          if (!finitePoint(start) || !finitePoint(end)) {
            continue;
          }
          if (samePoint(start, end)) {
            ++metrics.degenerateSegmentCount;
            continue;
          }
          segments.push_back({
            canonicalIndex,
            pointIndex - 1,
            start,
            end,
            std::min(start.x, end.x),
            std::min(start.y, end.y),
            std::max(start.x, end.x),
            std::max(start.y, end.y),
          });
          ++status.segmentCount;
        }
      }
      status.complete =
        status.finite && status.endpointsAttached && status.segmentCount > 0;
    }
    segmentRangeByCanonicalEdge[canonicalIndex] = {
      segmentBegin,
      segments.size(),
    };

    if (status.complete) {
      ++metrics.completeRouteCount;
    }
    metrics.canonicalRoutes.push_back(std::move(status));
  }
  metrics.canonicalSegmentCount = segments.size();
  metrics.allCanonicalRoutesComplete =
    metrics.completeRouteCount == metrics.canonicalEdgeCount;

  // Node hits are counted once per canonical edge/node pair.
  for (std::size_t canonicalIndex = 0;
       canonicalIndex < canonicalEdges.size();
       ++canonicalIndex) {
    const CanonicalEdgeGeometry& canonical = canonicalEdges[canonicalIndex];
    const EdgeRecord& edge = edges[canonical.inputEdgeIndex];
    for (const auto& nodeEntry : nodesById) {
      if (!nodeEntry.second.usable
          || nodeEntry.first == canonical.endpoints.first
          || nodeEntry.first == canonical.endpoints.second) {
        continue;
      }
      bool hit = false;
      const auto [segmentBegin, segmentEnd] =
        segmentRangeByCanonicalEdge[canonicalIndex];
      for (std::size_t segmentIndex = segmentBegin;
           segmentIndex < segmentEnd;
           ++segmentIndex) {
        const Segment& segment = segments[segmentIndex];
        if (segmentIntersectsRect(segment, nodeEntry.second.rect)) {
          hit = true;
          break;
        }
      }
      if (hit) {
        metrics.nonIncidentNodeHits.push_back({
          canonicalIndex,
          edge.edgeId,
          nodeEntry.first,
        });
      }
    }
  }

  std::map<std::pair<std::size_t, std::size_t>, std::size_t>
    properCrossingsByEdgePair;
  for (std::size_t leftIndex = 0; leftIndex < segments.size(); ++leftIndex) {
    const Segment& left = segments[leftIndex];
    for (std::size_t rightIndex = leftIndex + 1;
         rightIndex < segments.size();
         ++rightIndex) {
      const Segment& right = segments[rightIndex];
      const SegmentIntersection intersection = intersectSegments(left, right);
      if (intersection.kind == SegmentIntersectionKind::none) {
        continue;
      }

      if (left.canonicalEdgeIndex == right.canonicalEdgeIndex) {
        if (expectedAdjacentSegmentContact(left, right, intersection)) {
          continue;
        }
        ++metrics.selfIntersectionCount;
        if (intersection.kind == SegmentIntersectionKind::collinearOverlap) {
          ++metrics.collinearOverlapCount;
        } else if (intersection.kind == SegmentIntersectionKind::pointContact) {
          ++metrics.nonProperContactCount;
        }
        continue;
      }

      const CanonicalEdgeGeometry& leftCanonical =
        canonicalEdges[left.canonicalEdgeIndex];
      const CanonicalEdgeGeometry& rightCanonical =
        canonicalEdges[right.canonicalEdgeIndex];
      const EdgeRecord& leftEdge = edges[leftCanonical.inputEdgeIndex];
      const EdgeRecord& rightEdge = edges[rightCanonical.inputEdgeIndex];
      std::string sharedModelId;
      if (sharesGraphEndpoint(leftCanonical, rightCanonical, sharedModelId)) {
        const bool legalSharedEndpointContact =
          intersection.kind == SegmentIntersectionKind::pointContact
          && routeTouchesSharedEndpoint(
            routeGeometry[left.canonicalEdgeIndex],
            leftEdge,
            sharedModelId,
            intersection.position)
          && routeTouchesSharedEndpoint(
            routeGeometry[right.canonicalEdgeIndex],
            rightEdge,
            sharedModelId,
            intersection.position);
        if (!legalSharedEndpointContact) {
          ++metrics.adjacentEdgeIntersectionCount;
          if (intersection.kind == SegmentIntersectionKind::collinearOverlap) {
            ++metrics.collinearOverlapCount;
          } else if (intersection.kind == SegmentIntersectionKind::pointContact) {
            ++metrics.nonProperContactCount;
          }
        }
        // Intersections of graph-adjacent edges are never canonical crossings.
        continue;
      }

      if (intersection.kind == SegmentIntersectionKind::collinearOverlap) {
        ++metrics.collinearOverlapCount;
        continue;
      }
      if (intersection.kind == SegmentIntersectionKind::pointContact) {
        ++metrics.nonProperContactCount;
        continue;
      }

      const std::pair<std::size_t, std::size_t> edgePair = {
        left.canonicalEdgeIndex,
        right.canonicalEdgeIndex,
      };
      ++properCrossingsByEdgePair[edgePair];
      metrics.properCrossingPoints.push_back({
        left.canonicalEdgeIndex,
        right.canonicalEdgeIndex,
        leftEdge.edgeId,
        rightEdge.edgeId,
        intersection.position,
      });
    }
  }

  metrics.crossingEdgePairs.reserve(properCrossingsByEdgePair.size());
  for (const auto& entry : properCrossingsByEdgePair) {
    const std::size_t leftIndex = entry.first.first;
    const std::size_t rightIndex = entry.first.second;
    metrics.crossingEdgePairs.push_back({
      leftIndex,
      rightIndex,
      edges[canonicalEdges[leftIndex].inputEdgeIndex].edgeId,
      edges[canonicalEdges[rightIndex].inputEdgeIndex].edgeId,
      entry.second,
    });
  }

  metrics.properDrawing = metrics.allCanonicalRoutesComplete
    && metrics.invariantViolationCount == 0
    && metrics.degenerateSegmentCount == 0
    && metrics.nonIncidentNodeHits.empty()
    && metrics.collinearOverlapCount == 0
    && metrics.nonProperContactCount == 0
    && metrics.selfIntersectionCount == 0
    && metrics.adjacentEdgeIntersectionCount == 0;
  return metrics;
}

}  // namespace djerd
