// Low-memory scorer/optimizer for the direct straight-edge ERD scene.
//
// The geometry in this file deliberately mirrors the production canvas
// contract in native/ogdf-layout/src/main.cpp:
//   * every real relationship remains one independent two-point line;
//   * ports sit on the real model-card boundary and are rounded to 0.01;
//   * the small obstacle-aware port slide used by routeAllEdgesStraight is
//     reproduced;
//   * crossings between incident edges are counted when they meet again away
//     from the endpoint;
//   * an edge entering a non-endpoint model card expanded by 10px is a hit;
//   * model overlap is measured with the production 8px visual margin.
//
// State is O(nodes + edges).  In particular, crossing pairs and edge/node hit
// pairs are never retained, which keeps the research runner safe on large
// Django schemas.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <sys/resource.h>

namespace {

constexpr double kEdgeNodeMargin = 10.0;
constexpr double kNodeVisualMargin = 8.0;

struct Point {
  double x = 0.0;
  double y = 0.0;
};

struct Rect {
  double left = 0.0;
  double top = 0.0;
  double right = 0.0;
  double bottom = 0.0;
};

struct Node {
  std::string id;
  double width = 1.0;
  double height = 1.0;
};

struct Edge {
  std::string id;
  std::size_t source = 0;
  std::size_t target = 0;
  std::string kind;
};

struct Segment {
  Point source;
  Point target;
};

struct Score {
  std::size_t edgeCrossings = 0;
  std::size_t edgeNodeIntersections = 0;
  std::size_t nodeOverlaps = 0;

  std::size_t visual() const {
    return edgeCrossings + edgeNodeIntersections + nodeOverlaps;
  }
};

struct NodeBundle {
  std::size_t anchor = 0;
  std::vector<std::size_t> members;
  std::string reason;
};

struct Bounds {
  double left = 0.0;
  double top = 0.0;
  double right = 0.0;
  double bottom = 0.0;

  double width() const {
    return std::max(1.0, right - left);
  }

  double height() const {
    return std::max(1.0, bottom - top);
  }

  double area() const {
    return width() * height();
  }
};

std::vector<std::string> splitTabs(const std::string& line) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (true) {
    const std::size_t next = line.find('\t', start);
    if (next == std::string::npos) {
      fields.push_back(line.substr(start));
      return fields;
    }
    fields.push_back(line.substr(start, next - start));
    start = next + 1;
  }
}

bool hasArg(char** argv, int argc, const std::string& name) {
  for (int index = 1; index < argc; ++index) {
    if (argv[index] == name) return true;
  }
  return false;
}

int intArg(char** argv, int argc, const std::string& name, int fallback) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (argv[index] == name) return std::stoi(argv[index + 1]);
  }
  return fallback;
}

double doubleArg(
    char** argv,
    int argc,
    const std::string& name,
    double fallback) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (argv[index] == name) return std::stod(argv[index + 1]);
  }
  return fallback;
}

std::string stringArg(
    char** argv,
    int argc,
    const std::string& name,
    const std::string& fallback = "") {
  for (int index = 1; index + 1 < argc; ++index) {
    if (argv[index] == name) return argv[index + 1];
  }
  if (!fallback.empty()) return fallback;
  throw std::runtime_error("missing argument: " + name);
}

double roundedHundredth(double value) {
  return std::round(value * 100.0) / 100.0;
}

double rectCenterX(const Rect& rect) {
  return (rect.left + rect.right) * 0.5;
}

double rectCenterY(const Rect& rect) {
  return (rect.top + rect.bottom) * 0.5;
}

double rectWidth(const Rect& rect) {
  return rect.right - rect.left;
}

double rectHeight(const Rect& rect) {
  return rect.bottom - rect.top;
}

Rect nodeRect(
    const Node& node,
    const Point& position,
    double margin = 0.0) {
  return {
    position.x - node.width * 0.5 - margin,
    position.y - node.height * 0.5 - margin,
    position.x + node.width * 0.5 + margin,
    position.y + node.height * 0.5 + margin,
  };
}

bool rectsOverlap(const Rect& left, const Rect& right) {
  return left.left < right.right
    && left.right > right.left
    && left.top < right.bottom
    && left.bottom > right.top;
}

Point straightPortOnRect(const Rect& rect, const Rect& target) {
  const double centerX = rectCenterX(rect);
  const double centerY = rectCenterY(rect);
  double dx = rectCenterX(target) - centerX;
  double dy = rectCenterY(target) - centerY;
  if (std::abs(dx) < 0.01 && std::abs(dy) < 0.01) {
    dx = 1.0;
    dy = 0.0;
  }
  const double halfWidth = std::max(1.0, rectWidth(rect) * 0.5);
  const double halfHeight = std::max(1.0, rectHeight(rect) * 0.5);
  const double scaleX = std::abs(dx) < 0.01
    ? std::numeric_limits<double>::infinity()
    : halfWidth / std::abs(dx);
  const double scaleY = std::abs(dy) < 0.01
    ? std::numeric_limits<double>::infinity()
    : halfHeight / std::abs(dy);
  const double scale = std::min(scaleX, scaleY);
  return {
    roundedHundredth(centerX + dx * scale),
    roundedHundredth(centerY + dy * scale),
  };
}

double clampToSpan(double value, double low, double high) {
  return std::max(low, std::min(high, value));
}

int sideOfPortOnRect(const Point& point, const Rect& rect) {
  const double tolerance =
    std::max(1.0, 0.01 * std::max(rectWidth(rect), rectHeight(rect)));
  const double dTop = std::abs(point.y - rect.top);
  const double dRight = std::abs(point.x - rect.right);
  const double dBottom = std::abs(point.y - rect.bottom);
  const double dLeft = std::abs(point.x - rect.left);
  const double minimum = std::min({dTop, dRight, dBottom, dLeft});
  if (minimum > tolerance) return -1;
  if (dTop == minimum) return 0;
  if (dRight == minimum) return 1;
  if (dBottom == minimum) return 2;
  return 3;
}

Point slidePortOnRectSide(const Point& point, const Rect& rect, int side) {
  switch (side) {
    case 0:
      return {clampToSpan(point.x, rect.left, rect.right), rect.top};
    case 1:
      return {rect.right, clampToSpan(point.y, rect.top, rect.bottom)};
    case 2:
      return {clampToSpan(point.x, rect.left, rect.right), rect.bottom};
    case 3:
      return {rect.left, clampToSpan(point.y, rect.top, rect.bottom)};
    default: {
      const double dx = point.x - rectCenterX(rect);
      const double dy = point.y - rectCenterY(rect);
      if (std::abs(dx) >= std::abs(dy)) {
        return {
          dx < 0.0 ? rect.left : rect.right,
          clampToSpan(point.y, rect.top, rect.bottom),
        };
      }
      return {
        clampToSpan(point.x, rect.left, rect.right),
        dy < 0.0 ? rect.top : rect.bottom,
      };
    }
  }
}

double crossProduct(double ax, double ay, double bx, double by) {
  return ax * by - ay * bx;
}

bool properSegmentIntersection(
    const Point& leftStart,
    const Point& leftEnd,
    const Point& rightStart,
    const Point& rightEnd) {
  const double rx = leftEnd.x - leftStart.x;
  const double ry = leftEnd.y - leftStart.y;
  const double sx = rightEnd.x - rightStart.x;
  const double sy = rightEnd.y - rightStart.y;
  const double denominator = crossProduct(rx, ry, sx, sy);
  if (std::abs(denominator) < 1e-9) return false;
  const double qpx = rightStart.x - leftStart.x;
  const double qpy = rightStart.y - leftStart.y;
  const double t = crossProduct(qpx, qpy, sx, sy) / denominator;
  const double u = crossProduct(qpx, qpy, rx, ry) / denominator;
  constexpr double epsilon = 1e-9;
  return t > epsilon && t < 1.0 - epsilon
    && u > epsilon && u < 1.0 - epsilon;
}

bool segmentIntersectsRect(
    const Point& start,
    const Point& end,
    const Rect& rect) {
  if (std::abs(start.x - end.x) < 1e-9) {
    const double minY = std::min(start.y, end.y);
    const double maxY = std::max(start.y, end.y);
    return start.x > rect.left
      && start.x < rect.right
      && maxY > rect.top
      && minY < rect.bottom;
  }
  if (std::abs(start.y - end.y) < 1e-9) {
    const double minX = std::min(start.x, end.x);
    const double maxX = std::max(start.x, end.x);
    return start.y > rect.top
      && start.y < rect.bottom
      && maxX > rect.left
      && minX < rect.right;
  }
  double minT = 0.0;
  double maxT = 1.0;
  const double dx = end.x - start.x;
  const double dy = end.y - start.y;
  const auto clip = [&](double edge, double distance) {
    if (std::abs(edge) < 1e-9) return distance >= 0.0;
    const double t = distance / edge;
    if (edge < 0.0) {
      if (t > maxT) return false;
      minT = std::max(minT, t);
    } else {
      if (t < minT) return false;
      maxT = std::min(maxT, t);
    }
    return true;
  };
  if (!clip(-dx, start.x - rect.left)) return false;
  if (!clip(dx, rect.right - start.x)) return false;
  if (!clip(-dy, start.y - rect.top)) return false;
  if (!clip(dy, rect.bottom - start.y)) return false;
  return maxT - minT > 1e-9;
}

bool segmentsCrossForNudge(
    double ax,
    double ay,
    double bx,
    double by,
    double cx,
    double cy,
    double dx,
    double dy) {
  const double d1x = bx - ax;
  const double d1y = by - ay;
  const double d2x = dx - cx;
  const double d2y = dy - cy;
  const double denominator = d1x * d2y - d1y * d2x;
  if (std::abs(denominator) < 1e-9) return false;
  const double t = ((cx - ax) * d2y - (cy - ay) * d2x) / denominator;
  const double s = ((cx - ax) * d1y - (cy - ay) * d1x) / denominator;
  return t > 1e-9 && t < 1.0 - 1e-9
    && s > 1e-9 && s < 1.0 - 1e-9;
}

bool lineHitsBox(
    const Point& source,
    const Point& target,
    const Rect& box,
    double margin = 0.0) {
  const double minX = box.left - margin;
  const double maxX = box.right + margin;
  const double minY = box.top - margin;
  const double maxY = box.bottom + margin;
  if (std::max(source.x, target.x) < minX
      || std::min(source.x, target.x) > maxX) return false;
  if (std::max(source.y, target.y) < minY
      || std::min(source.y, target.y) > maxY) return false;
  if (source.x > minX && source.x < maxX
      && source.y > minY && source.y < maxY) return true;
  if (target.x > minX && target.x < maxX
      && target.y > minY && target.y < maxY) return true;
  return segmentsCrossForNudge(
      source.x, source.y, target.x, target.y, minX, minY, maxX, minY)
    || segmentsCrossForNudge(
      source.x, source.y, target.x, target.y, maxX, minY, maxX, maxY)
    || segmentsCrossForNudge(
      source.x, source.y, target.x, target.y, maxX, maxY, minX, maxY)
    || segmentsCrossForNudge(
      source.x, source.y, target.x, target.y, minX, maxY, minX, minY);
}

std::vector<Segment> routeAllEdgesStraight(
    const std::vector<Node>& nodes,
    const std::vector<Edge>& edges,
    const std::vector<Point>& positions) {
  std::vector<Segment> routes;
  routes.reserve(edges.size());
  for (const Edge& edge : edges) {
    const Rect sourceRect = nodeRect(nodes[edge.source], positions[edge.source]);
    const Rect targetRect = nodeRect(nodes[edge.target], positions[edge.target]);
    routes.push_back({
      straightPortOnRect(sourceRect, targetRect),
      straightPortOnRect(targetRect, sourceRect),
    });
  }

  std::map<std::pair<std::size_t, std::size_t>, std::vector<std::size_t>> byPair;
  for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
    const Edge& edge = edges[edgeIndex];
    if (edge.source == edge.target) continue;
    byPair[std::minmax(edge.source, edge.target)].push_back(edgeIndex);
  }
  constexpr double laneSpacing = 12.0;
  for (const auto& item : byPair) {
    const std::vector<std::size_t>& indices = item.second;
    if (indices.size() < 2) continue;
    const double centerOffset = (static_cast<double>(indices.size()) - 1.0) * 0.5;
    for (std::size_t lane = 0; lane < indices.size(); ++lane) {
      const std::size_t edgeIndex = indices[lane];
      Segment& route = routes[edgeIndex];
      const double dx = route.target.x - route.source.x;
      const double dy = route.target.y - route.source.y;
      const double length = std::hypot(dx, dy);
      if (length < 1e-3) continue;
      const double nx = -dy / length;
      const double ny = dx / length;
      const double offset = (static_cast<double>(lane) - centerOffset) * laneSpacing;
      const Edge& edge = edges[edgeIndex];
      const Rect sourceRect = nodeRect(nodes[edge.source], positions[edge.source]);
      const Rect targetRect = nodeRect(nodes[edge.target], positions[edge.target]);
      const int sourceSide = sideOfPortOnRect(route.source, sourceRect);
      const int targetSide = sideOfPortOnRect(route.target, targetRect);
      const Point sourcePort = slidePortOnRectSide(
        {route.source.x + nx * offset, route.source.y + ny * offset},
        sourceRect,
        sourceSide);
      const Point targetPort = slidePortOnRectSide(
        {route.target.x + nx * offset, route.target.y + ny * offset},
        targetRect,
        targetSide);
      route.source = {
        roundedHundredth(sourcePort.x), roundedHundredth(sourcePort.y)};
      route.target = {
        roundedHundredth(targetPort.x), roundedHundredth(targetPort.y)};
    }
  }

  std::vector<Rect> obstacles;
  obstacles.reserve(nodes.size());
  for (std::size_t node = 0; node < nodes.size(); ++node) {
    obstacles.push_back(nodeRect(nodes[node], positions[node]));
  }
  for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
    Segment& route = routes[edgeIndex];
    const Edge& edge = edges[edgeIndex];
    const auto countHits = [&](const Point& source, const Point& target) {
      int hits = 0;
      for (std::size_t node = 0; node < obstacles.size(); ++node) {
        if (node == edge.source || node == edge.target) continue;
        if (lineHitsBox(source, target, obstacles[node])) ++hits;
      }
      return hits;
    };
    const int baseHits = countHits(route.source, route.target);
    if (baseHits == 0) continue;
    const double dx = route.target.x - route.source.x;
    const double dy = route.target.y - route.source.y;
    const double length = std::hypot(dx, dy);
    if (length < 1e-3) continue;
    const double nx = -dy / length;
    const double ny = dx / length;
    const Rect& sourceRect = obstacles[edge.source];
    const Rect& targetRect = obstacles[edge.target];
    const int sourceSide = sideOfPortOnRect(route.source, sourceRect);
    const int targetSide = sideOfPortOnRect(route.target, targetRect);
    constexpr double attempts[] = {-12.0, 12.0, -24.0, 24.0, -36.0, 36.0};
    int bestHits = baseHits;
    double bestOffset = 0.0;
    for (const double offset : attempts) {
      const Point source = slidePortOnRectSide(
        {route.source.x + nx * offset, route.source.y + ny * offset},
        sourceRect,
        sourceSide);
      const Point target = slidePortOnRectSide(
        {route.target.x + nx * offset, route.target.y + ny * offset},
        targetRect,
        targetSide);
      const int hits = countHits(source, target);
      if (hits < bestHits) {
        bestHits = hits;
        bestOffset = offset;
        if (hits == 0) break;
      }
    }
    if (bestOffset == 0.0) continue;
    const Point source = slidePortOnRectSide(
      {route.source.x + nx * bestOffset, route.source.y + ny * bestOffset},
      sourceRect,
      sourceSide);
    const Point target = slidePortOnRectSide(
      {route.target.x + nx * bestOffset, route.target.y + ny * bestOffset},
      targetRect,
      targetSide);
    route.source = {roundedHundredth(source.x), roundedHundredth(source.y)};
    route.target = {roundedHundredth(target.x), roundedHundredth(target.y)};
  }
  return routes;
}

Score measureScore(
    const std::vector<Node>& nodes,
    const std::vector<Edge>& edges,
    const std::vector<Point>& positions,
    const std::vector<Segment>& routes) {
  Score score;
  for (std::size_t left = 0; left < routes.size(); ++left) {
    for (std::size_t right = left + 1; right < routes.size(); ++right) {
      if (properSegmentIntersection(
            routes[left].source,
            routes[left].target,
            routes[right].source,
            routes[right].target)) {
        ++score.edgeCrossings;
      }
    }
  }
  std::vector<Rect> edgeNodeRects;
  edgeNodeRects.reserve(nodes.size());
  for (std::size_t node = 0; node < nodes.size(); ++node) {
    edgeNodeRects.push_back(
      nodeRect(nodes[node], positions[node], kEdgeNodeMargin));
  }
  for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
    const Edge& edge = edges[edgeIndex];
    const Segment& route = routes[edgeIndex];
    for (std::size_t node = 0; node < nodes.size(); ++node) {
      if (node == edge.source || node == edge.target) continue;
      if (segmentIntersectsRect(
            route.source, route.target, edgeNodeRects[node])) {
        ++score.edgeNodeIntersections;
      }
    }
  }
  std::vector<Rect> overlapRects;
  overlapRects.reserve(nodes.size());
  for (std::size_t node = 0; node < nodes.size(); ++node) {
    overlapRects.push_back(
      nodeRect(nodes[node], positions[node], kNodeVisualMargin));
  }
  std::vector<std::size_t> order(nodes.size());
  for (std::size_t node = 0; node < nodes.size(); ++node) order[node] = node;
  std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
    return overlapRects[left].left < overlapRects[right].left;
  });
  for (std::size_t leftOrder = 0; leftOrder < order.size(); ++leftOrder) {
    const Rect& leftRect = overlapRects[order[leftOrder]];
    for (std::size_t rightOrder = leftOrder + 1;
         rightOrder < order.size(); ++rightOrder) {
      const Rect& rightRect = overlapRects[order[rightOrder]];
      if (rightRect.left >= leftRect.right) break;
      if (rectsOverlap(leftRect, rightRect)) ++score.nodeOverlaps;
    }
  }
  return score;
}

struct PartialPlacementCost {
  std::size_t crossings = 0;
  std::size_t edgeNodeIntersections = 0;
  std::size_t nodeOverlaps = 0;
  std::size_t segmentOverlaps = 0;
  double edgeLength = 0.0;
  double boundsArea = 0.0;
};

struct ActiveRoute {
  std::size_t edgeIndex = 0;
  Segment segment;
};

double stablePhase(const std::string& value) {
  std::uint32_t state = 2166136261u;
  for (const unsigned char byte : value) {
    state ^= byte;
    state *= 16777619u;
  }
  constexpr double tau = 6.283185307179586476925286766559;
  return (static_cast<double>(state) / 4294967296.0) * tau;
}

bool collinearSegmentOverlap(const Segment& left, const Segment& right) {
  const double ldx = left.target.x - left.source.x;
  const double ldy = left.target.y - left.source.y;
  const double rdx = right.target.x - right.source.x;
  const double rdy = right.target.y - right.source.y;
  const double leftLength = std::hypot(ldx, ldy);
  const double rightLength = std::hypot(rdx, rdy);
  if (leftLength < 1e-6 || rightLength < 1e-6) return false;
  const double parallel = std::abs(crossProduct(ldx, ldy, rdx, rdy));
  if (parallel > leftLength * rightLength * 1e-7) return false;
  const double offsetX = right.source.x - left.source.x;
  const double offsetY = right.source.y - left.source.y;
  if (std::abs(crossProduct(ldx, ldy, offsetX, offsetY))
      > leftLength * 0.05) {
    return false;
  }
  const bool useX = std::abs(ldx) >= std::abs(ldy);
  const double leftStart = useX ? left.source.x : left.source.y;
  const double leftEnd = useX ? left.target.x : left.target.y;
  const double rightStart = useX ? right.source.x : right.source.y;
  const double rightEnd = useX ? right.target.x : right.target.y;
  return std::min(std::max(leftStart, leftEnd), std::max(rightStart, rightEnd))
      - std::max(std::min(leftStart, leftEnd), std::min(rightStart, rightEnd))
    > 1.0;
}

Segment routeSingleStraight(
    const Edge& edge,
    const std::vector<Node>& nodes,
    const std::vector<Point>& positions,
    const std::vector<bool>& placed) {
  const Rect sourceRect = nodeRect(nodes[edge.source], positions[edge.source]);
  const Rect targetRect = nodeRect(nodes[edge.target], positions[edge.target]);
  Segment route{
    straightPortOnRect(sourceRect, targetRect),
    straightPortOnRect(targetRect, sourceRect),
  };
  const auto countHits = [&](const Point& source, const Point& target) {
    int hits = 0;
    for (std::size_t node = 0; node < nodes.size(); ++node) {
      if (!placed[node] || node == edge.source || node == edge.target) continue;
      if (lineHitsBox(
            source,
            target,
            nodeRect(nodes[node], positions[node]))) {
        ++hits;
      }
    }
    return hits;
  };
  const int baseHits = countHits(route.source, route.target);
  if (baseHits == 0) return route;
  const double dx = route.target.x - route.source.x;
  const double dy = route.target.y - route.source.y;
  const double length = std::hypot(dx, dy);
  if (length < 1e-3) return route;
  const double nx = -dy / length;
  const double ny = dx / length;
  const int sourceSide = sideOfPortOnRect(route.source, sourceRect);
  const int targetSide = sideOfPortOnRect(route.target, targetRect);
  constexpr double attempts[] = {-12.0, 12.0, -24.0, 24.0, -36.0, 36.0};
  int bestHits = baseHits;
  double bestOffset = 0.0;
  for (const double offset : attempts) {
    const Point source = slidePortOnRectSide(
      {route.source.x + nx * offset, route.source.y + ny * offset},
      sourceRect,
      sourceSide);
    const Point target = slidePortOnRectSide(
      {route.target.x + nx * offset, route.target.y + ny * offset},
      targetRect,
      targetSide);
    const int hits = countHits(source, target);
    if (hits < bestHits) {
      bestHits = hits;
      bestOffset = offset;
      if (hits == 0) break;
    }
  }
  if (bestOffset == 0.0) return route;
  const Point source = slidePortOnRectSide(
    {route.source.x + nx * bestOffset, route.source.y + ny * bestOffset},
    sourceRect,
    sourceSide);
  const Point target = slidePortOnRectSide(
    {route.target.x + nx * bestOffset, route.target.y + ny * bestOffset},
    targetRect,
    targetSide);
  route.source = {roundedHundredth(source.x), roundedHundredth(source.y)};
  route.target = {roundedHundredth(target.x), roundedHundredth(target.y)};
  return route;
}

Bounds measurePlacedBounds(
    const std::vector<Node>& nodes,
    const std::vector<Point>& positions,
    const std::vector<bool>& placed) {
  Bounds bounds;
  bool initialized = false;
  for (std::size_t node = 0; node < nodes.size(); ++node) {
    if (!placed[node]) continue;
    const Rect rect = nodeRect(nodes[node], positions[node]);
    if (!initialized) {
      bounds = {rect.left, rect.top, rect.right, rect.bottom};
      initialized = true;
      continue;
    }
    bounds.left = std::min(bounds.left, rect.left);
    bounds.top = std::min(bounds.top, rect.top);
    bounds.right = std::max(bounds.right, rect.right);
    bounds.bottom = std::max(bounds.bottom, rect.bottom);
  }
  return bounds;
}

void appendRingCandidates(
    std::vector<Point>& candidates,
    const Point& center,
    double radius,
    int angularSamples,
    double phase) {
  constexpr double tau = 6.283185307179586476925286766559;
  for (int sample = 0; sample < angularSamples; ++sample) {
    const double angle = phase
      + tau * static_cast<double>(sample)
        / static_cast<double>(std::max(1, angularSamples));
    candidates.push_back({
      center.x + std::cos(angle) * radius,
      center.y + std::sin(angle) * radius,
    });
  }
}

std::vector<Point> shellCandidates(
    std::size_t node,
    const std::vector<std::size_t>& parents,
    const std::vector<Node>& nodes,
    const std::vector<Point>& positions,
    const std::vector<bool>& placed,
    int angularSamples,
    double maximumRadiusFactor) {
  const Bounds bounds = measurePlacedBounds(nodes, positions, placed);
  const Point graphCenter{
    (bounds.left + bounds.right) * 0.5,
    (bounds.top + bounds.bottom) * 0.5,
  };
  const double graphDiagonal = std::hypot(bounds.width(), bounds.height());
  const double maximumRadius = std::max(
    2400.0, graphDiagonal * maximumRadiusFactor);
  const double phase = stablePhase(nodes[node].id);
  std::vector<Point> candidates;
  candidates.reserve(static_cast<std::size_t>(angularSamples) * 20 + 64);

  if (parents.empty()) {
    const double gap = std::max(240.0, nodes[node].width + 80.0);
    candidates.push_back({bounds.right + gap, bounds.top + nodes[node].height * 0.5});
    candidates.push_back({bounds.left - gap, bounds.top + nodes[node].height * 0.5});
    candidates.push_back({bounds.left + nodes[node].width * 0.5, bounds.bottom + gap});
    candidates.push_back({bounds.left + nodes[node].width * 0.5, bounds.top - gap});
    return candidates;
  }

  Point parentCenter{0.0, 0.0};
  double parentDiameter = 0.0;
  for (const std::size_t parent : parents) {
    parentCenter.x += positions[parent].x;
    parentCenter.y += positions[parent].y;
    parentDiameter = std::max(
      parentDiameter,
      std::hypot(nodes[parent].width, nodes[parent].height));
  }
  parentCenter.x /= static_cast<double>(parents.size());
  parentCenter.y /= static_cast<double>(parents.size());
  const double nodeDiameter = std::hypot(nodes[node].width, nodes[node].height);
  const double baseRadius = (parentDiameter + nodeDiameter) * 0.5 + 48.0;
  double outward = std::atan2(
    parentCenter.y - graphCenter.y, parentCenter.x - graphCenter.x);
  if (std::abs(parentCenter.x - graphCenter.x) < 1e-6
      && std::abs(parentCenter.y - graphCenter.y) < 1e-6) {
    outward = phase;
  }

  const std::vector<double> radiusMultipliers{1.0, 1.6, 2.5, 4.0, 6.5, 10.0, 16.0};
  for (const double multiplier : radiusMultipliers) {
    const double radius = std::min(maximumRadius, baseRadius * multiplier);
    appendRingCandidates(
      candidates,
      parentCenter,
      radius,
      angularSamples,
      outward + phase * 0.125);
  }

  if (parents.size() >= 2) {
    const Point source = positions[parents[0]];
    const Point target = positions[parents[1]];
    const double dx = target.x - source.x;
    const double dy = target.y - source.y;
    const double length = std::hypot(dx, dy);
    const double nx = length < 1e-6 ? 0.0 : -dy / length;
    const double ny = length < 1e-6 ? 1.0 : dx / length;
    const std::vector<double> fractions{0.15, 0.28, 0.4, 0.5, 0.6, 0.72, 0.85};
    const std::vector<double> offsets{
      baseRadius, baseRadius * 1.8, baseRadius * 3.0,
      baseRadius * 5.0, baseRadius * 8.0,
    };
    for (const double fraction : fractions) {
      const Point onAxis{
        source.x + dx * fraction,
        source.y + dy * fraction,
      };
      for (const double offset : offsets) {
        if (offset > maximumRadius) continue;
        candidates.push_back({onAxis.x + nx * offset, onAxis.y + ny * offset});
        candidates.push_back({onAxis.x - nx * offset, onAxis.y - ny * offset});
      }
    }
    for (const std::size_t parent : parents) {
      for (const double multiplier : {1.0, 2.5, 6.0}) {
        appendRingCandidates(
          candidates,
          positions[parent],
          std::min(maximumRadius, baseRadius * multiplier),
          std::max(8, angularSamples / 2),
          phase);
      }
    }
  }

  // Perimeter candidates give the insertion search access to an empty face
  // without inflating the whole scene: only this real family branch moves.
  const double sideGap = baseRadius + 120.0;
  candidates.push_back({bounds.left - sideGap, parentCenter.y});
  candidates.push_back({bounds.right + sideGap, parentCenter.y});
  candidates.push_back({parentCenter.x, bounds.top - sideGap});
  candidates.push_back({parentCenter.x, bounds.bottom + sideGap});
  return candidates;
}

PartialPlacementCost evaluateShellCandidate(
    std::size_t node,
    const Point& candidate,
    const std::vector<std::size_t>& newEdgeIndices,
    const std::vector<Node>& nodes,
    const std::vector<Edge>& edges,
    std::vector<Point>& positions,
    std::vector<bool>& placed,
    const std::vector<ActiveRoute>& activeRoutes) {
  const Point previous = positions[node];
  positions[node] = candidate;
  placed[node] = true;
  PartialPlacementCost cost;
  const Rect candidateOverlapRect =
    nodeRect(nodes[node], candidate, kNodeVisualMargin);
  const Rect candidateEdgeRect =
    nodeRect(nodes[node], candidate, kEdgeNodeMargin);
  for (std::size_t other = 0; other < nodes.size(); ++other) {
    if (!placed[other] || other == node) continue;
    if (rectsOverlap(
          candidateOverlapRect,
          nodeRect(nodes[other], positions[other], kNodeVisualMargin))) {
      ++cost.nodeOverlaps;
    }
  }
  for (const ActiveRoute& active : activeRoutes) {
    if (segmentIntersectsRect(
          active.segment.source, active.segment.target, candidateEdgeRect)) {
      ++cost.edgeNodeIntersections;
    }
  }

  std::vector<Segment> newRoutes;
  newRoutes.reserve(newEdgeIndices.size());
  for (const std::size_t edgeIndex : newEdgeIndices) {
    const Edge& edge = edges[edgeIndex];
    const Segment route = routeSingleStraight(edge, nodes, positions, placed);
    newRoutes.push_back(route);
    cost.edgeLength += std::hypot(
      route.target.x - route.source.x, route.target.y - route.source.y);
    for (std::size_t obstacle = 0; obstacle < nodes.size(); ++obstacle) {
      if (!placed[obstacle]
          || obstacle == edge.source
          || obstacle == edge.target) {
        continue;
      }
      if (segmentIntersectsRect(
            route.source,
            route.target,
            nodeRect(nodes[obstacle], positions[obstacle], kEdgeNodeMargin))) {
        ++cost.edgeNodeIntersections;
      }
    }
    for (const ActiveRoute& active : activeRoutes) {
      if (properSegmentIntersection(
            route.source,
            route.target,
            active.segment.source,
            active.segment.target)) {
        ++cost.crossings;
      }
      if (collinearSegmentOverlap(route, active.segment)) {
        ++cost.segmentOverlaps;
      }
    }
  }
  for (std::size_t left = 0; left < newRoutes.size(); ++left) {
    for (std::size_t right = left + 1; right < newRoutes.size(); ++right) {
      if (properSegmentIntersection(
            newRoutes[left].source,
            newRoutes[left].target,
            newRoutes[right].source,
            newRoutes[right].target)) {
        ++cost.crossings;
      }
      if (collinearSegmentOverlap(newRoutes[left], newRoutes[right])) {
        ++cost.segmentOverlaps;
      }
    }
  }
  cost.boundsArea = measurePlacedBounds(nodes, positions, placed).area();
  placed[node] = false;
  positions[node] = previous;
  return cost;
}

bool betterPartialCost(
    const PartialPlacementCost& candidate,
    const PartialPlacementCost& best,
    double edgeNodeWeight) {
  if (candidate.nodeOverlaps != best.nodeOverlaps) {
    return candidate.nodeOverlaps < best.nodeOverlaps;
  }
  const long double candidateConflict =
    static_cast<long double>(candidate.crossings)
    + static_cast<long double>(candidate.edgeNodeIntersections) * edgeNodeWeight
    + static_cast<long double>(candidate.segmentOverlaps) * 4.0L;
  const long double bestConflict =
    static_cast<long double>(best.crossings)
    + static_cast<long double>(best.edgeNodeIntersections) * edgeNodeWeight
    + static_cast<long double>(best.segmentOverlaps) * 4.0L;
  if (candidateConflict != bestConflict) return candidateConflict < bestConflict;
  const std::size_t candidateVisual =
    candidate.crossings + candidate.edgeNodeIntersections;
  const std::size_t bestVisual = best.crossings + best.edgeNodeIntersections;
  if (candidateVisual != bestVisual) return candidateVisual < bestVisual;
  if (candidate.boundsArea != best.boundsArea) {
    return candidate.boundsArea < best.boundsArea;
  }
  return candidate.edgeLength < best.edgeLength;
}

void constructDegenerateShell(
    const std::vector<Node>& nodes,
    const std::vector<Edge>& edges,
    const std::vector<std::vector<std::size_t>>& adjacency,
    const std::vector<std::vector<std::size_t>>& incidentEdges,
    std::vector<Point>& positions,
    std::vector<bool>& positioned,
    int angularSamples,
    double maximumRadiusFactor,
    double edgeNodeWeight,
    double coreScale) {
  std::vector<int> degree(nodes.size(), 0);
  std::vector<bool> active(nodes.size(), true);
  std::set<std::pair<int, std::size_t>> peelable;
  for (std::size_t node = 0; node < nodes.size(); ++node) {
    degree[node] = static_cast<int>(adjacency[node].size());
    if (degree[node] < 3) peelable.emplace(degree[node], node);
  }
  std::vector<std::size_t> removalOrder;
  removalOrder.reserve(nodes.size());
  while (!peelable.empty()) {
    const std::size_t node = peelable.begin()->second;
    peelable.erase(peelable.begin());
    if (!active[node] || degree[node] >= 3) continue;
    active[node] = false;
    removalOrder.push_back(node);
    for (const std::size_t neighbor : adjacency[node]) {
      if (!active[neighbor]) continue;
      if (degree[neighbor] < 3) {
        peelable.erase({degree[neighbor], neighbor});
      }
      --degree[neighbor];
      if (degree[neighbor] < 3) {
        peelable.emplace(degree[neighbor], neighbor);
      }
    }
  }
  std::size_t coreCount = 0;
  Point coreCenter{0.0, 0.0};
  for (std::size_t node = 0; node < nodes.size(); ++node) {
    if (!active[node]) continue;
    ++coreCount;
    if (!positioned[node]) {
      throw std::runtime_error(
        "partial positions do not cover 3-core node " + nodes[node].id);
    }
    coreCenter.x += positions[node].x;
    coreCenter.y += positions[node].y;
  }
  if (coreCount > 0) {
    coreCenter.x /= static_cast<double>(coreCount);
    coreCenter.y /= static_cast<double>(coreCount);
  }
  for (std::size_t node = 0; node < nodes.size(); ++node) {
    if (active[node]) {
      positions[node] = {
        coreCenter.x + (positions[node].x - coreCenter.x) * coreScale,
        coreCenter.y + (positions[node].y - coreCenter.y) * coreScale,
      };
      positioned[node] = true;
    } else {
      positioned[node] = false;
      positions[node] = {};
    }
  }

  std::vector<ActiveRoute> activeRoutes;
  std::vector<bool> edgeActive(edges.size(), false);
  for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
    const Edge& edge = edges[edgeIndex];
    if (!positioned[edge.source] || !positioned[edge.target]) continue;
    edgeActive[edgeIndex] = true;
    activeRoutes.push_back({
      edgeIndex,
      routeSingleStraight(edge, nodes, positions, positioned),
    });
  }
  std::cerr << "shell-start core=" << coreCount
            << " removed=" << removalOrder.size()
            << " coreEdges=" << activeRoutes.size()
            << " coreScale=" << coreScale << '\n';

  std::size_t inserted = 0;
  std::size_t zeroConflictInsertions = 0;
  for (auto orderIt = removalOrder.rbegin();
       orderIt != removalOrder.rend(); ++orderIt) {
    const std::size_t node = *orderIt;
    std::vector<std::size_t> parents;
    for (const std::size_t neighbor : adjacency[node]) {
      if (positioned[neighbor]) parents.push_back(neighbor);
    }
    if (parents.size() > 2) {
      throw std::runtime_error(
        "3-core reverse insertion has more than two placed anchors");
    }
    std::vector<std::size_t> newEdges;
    for (const std::size_t edgeIndex : incidentEdges[node]) {
      const Edge& edge = edges[edgeIndex];
      const std::size_t other = edge.source == node ? edge.target : edge.source;
      if (positioned[other] && !edgeActive[edgeIndex]) {
        newEdges.push_back(edgeIndex);
      }
    }
    const std::vector<Point> candidates = shellCandidates(
      node,
      parents,
      nodes,
      positions,
      positioned,
      angularSamples,
      maximumRadiusFactor);
    if (candidates.empty()) {
      throw std::runtime_error("no shell candidate for " + nodes[node].id);
    }
    Point bestPoint = candidates.front();
    PartialPlacementCost bestCost = evaluateShellCandidate(
      node,
      bestPoint,
      newEdges,
      nodes,
      edges,
      positions,
      positioned,
      activeRoutes);
    for (std::size_t candidateIndex = 1;
         candidateIndex < candidates.size(); ++candidateIndex) {
      const PartialPlacementCost cost = evaluateShellCandidate(
        node,
        candidates[candidateIndex],
        newEdges,
        nodes,
        edges,
        positions,
        positioned,
        activeRoutes);
      if (betterPartialCost(cost, bestCost, edgeNodeWeight)) {
        bestCost = cost;
        bestPoint = candidates[candidateIndex];
      }
    }
    positions[node] = bestPoint;
    positioned[node] = true;
    if (bestCost.crossings == 0
        && bestCost.edgeNodeIntersections == 0
        && bestCost.nodeOverlaps == 0
        && bestCost.segmentOverlaps == 0) {
      ++zeroConflictInsertions;
    }
    for (const std::size_t edgeIndex : newEdges) {
      edgeActive[edgeIndex] = true;
      activeRoutes.push_back({
        edgeIndex,
        routeSingleStraight(edges[edgeIndex], nodes, positions, positioned),
      });
    }
    ++inserted;
    if (inserted % 100 == 0 || inserted == removalOrder.size()) {
      std::cerr << "shell-progress inserted=" << inserted
                << '/' << removalOrder.size()
                << " zeroConflict=" << zeroConflictInsertions
                << " activeEdges=" << activeRoutes.size() << '\n';
    }
  }
}

struct ComponentBundle {
  std::vector<std::size_t> members;
  std::vector<std::size_t> anchors;
  std::vector<std::size_t> edgeIndices;
};

PartialPlacementCost evaluateComponentCandidate(
    const ComponentBundle& component,
    const std::vector<Point>& candidatePoints,
    const std::vector<Node>& nodes,
    const std::vector<Edge>& edges,
    std::vector<Point>& positions,
    std::vector<bool>& placed,
    const std::vector<ActiveRoute>& activeRoutes) {
  std::vector<Point> previous;
  previous.reserve(component.members.size());
  for (std::size_t index = 0; index < component.members.size(); ++index) {
    const std::size_t node = component.members[index];
    previous.push_back(positions[node]);
    positions[node] = candidatePoints[index];
    placed[node] = true;
  }
  PartialPlacementCost cost;
  for (std::size_t leftIndex = 0;
       leftIndex < component.members.size(); ++leftIndex) {
    const std::size_t left = component.members[leftIndex];
    const Rect leftRect = nodeRect(
      nodes[left], positions[left], kNodeVisualMargin);
    for (std::size_t right = 0; right < nodes.size(); ++right) {
      if (!placed[right] || right == left) continue;
      if (std::find(
            component.members.begin(),
            component.members.begin() + static_cast<std::ptrdiff_t>(leftIndex),
            right) != component.members.begin()
              + static_cast<std::ptrdiff_t>(leftIndex)) {
        continue;
      }
      if (rectsOverlap(
            leftRect,
            nodeRect(nodes[right], positions[right], kNodeVisualMargin))) {
        ++cost.nodeOverlaps;
      }
    }
    const Rect edgeRect = nodeRect(
      nodes[left], positions[left], kEdgeNodeMargin);
    for (const ActiveRoute& active : activeRoutes) {
      const Edge& edge = edges[active.edgeIndex];
      if (edge.source == left || edge.target == left) continue;
      if (segmentIntersectsRect(
            active.segment.source, active.segment.target, edgeRect)) {
        ++cost.edgeNodeIntersections;
      }
    }
  }

  std::vector<Segment> newRoutes;
  newRoutes.reserve(component.edgeIndices.size());
  for (const std::size_t edgeIndex : component.edgeIndices) {
    const Edge& edge = edges[edgeIndex];
    const Segment route = routeSingleStraight(edge, nodes, positions, placed);
    newRoutes.push_back(route);
    cost.edgeLength += std::hypot(
      route.target.x - route.source.x, route.target.y - route.source.y);
    for (std::size_t obstacle = 0; obstacle < nodes.size(); ++obstacle) {
      if (!placed[obstacle]
          || obstacle == edge.source
          || obstacle == edge.target) {
        continue;
      }
      if (segmentIntersectsRect(
            route.source,
            route.target,
            nodeRect(nodes[obstacle], positions[obstacle], kEdgeNodeMargin))) {
        ++cost.edgeNodeIntersections;
      }
    }
    for (const ActiveRoute& active : activeRoutes) {
      if (properSegmentIntersection(
            route.source,
            route.target,
            active.segment.source,
            active.segment.target)) {
        ++cost.crossings;
      }
      if (collinearSegmentOverlap(route, active.segment)) {
        ++cost.segmentOverlaps;
      }
    }
  }
  for (std::size_t left = 0; left < newRoutes.size(); ++left) {
    for (std::size_t right = left + 1; right < newRoutes.size(); ++right) {
      if (properSegmentIntersection(
            newRoutes[left].source,
            newRoutes[left].target,
            newRoutes[right].source,
            newRoutes[right].target)) {
        ++cost.crossings;
      }
      if (collinearSegmentOverlap(newRoutes[left], newRoutes[right])) {
        ++cost.segmentOverlaps;
      }
    }
  }
  cost.boundsArea = measurePlacedBounds(nodes, positions, placed).area();
  for (std::size_t index = 0; index < component.members.size(); ++index) {
    const std::size_t node = component.members[index];
    positions[node] = previous[index];
    placed[node] = false;
  }
  return cost;
}

void constructComponentBundles(
    const std::vector<Node>& nodes,
    const std::vector<Edge>& edges,
    const std::vector<std::vector<std::size_t>>& adjacency,
    const std::vector<std::vector<std::size_t>>& incidentEdges,
    std::vector<Point>& positions,
    std::vector<bool>& positioned,
    int angularSamples,
    double edgeNodeWeight,
    const std::unordered_set<std::string>& fixedCoreIds) {
  const std::vector<Point> templates = positions;
  std::vector<int> degree(nodes.size(), 0);
  std::vector<bool> core(nodes.size(), false);
  if (!fixedCoreIds.empty()) {
    for (std::size_t node = 0; node < nodes.size(); ++node) {
      core[node] = fixedCoreIds.count(nodes[node].id) > 0;
    }
  } else {
    std::fill(core.begin(), core.end(), true);
    std::set<std::pair<int, std::size_t>> peelable;
    for (std::size_t node = 0; node < nodes.size(); ++node) {
      degree[node] = static_cast<int>(adjacency[node].size());
      if (degree[node] < 3) peelable.emplace(degree[node], node);
    }
    while (!peelable.empty()) {
      const std::size_t node = peelable.begin()->second;
      peelable.erase(peelable.begin());
      if (!core[node] || degree[node] >= 3) continue;
      core[node] = false;
      for (const std::size_t neighbor : adjacency[node]) {
        if (!core[neighbor]) continue;
        if (degree[neighbor] < 3) peelable.erase({degree[neighbor], neighbor});
        --degree[neighbor];
        if (degree[neighbor] < 3) peelable.emplace(degree[neighbor], neighbor);
      }
    }
  }

  std::vector<int> componentByNode(nodes.size(), -1);
  std::vector<ComponentBundle> components;
  for (std::size_t start = 0; start < nodes.size(); ++start) {
    if (core[start] || componentByNode[start] >= 0) continue;
    const int componentIndex = static_cast<int>(components.size());
    components.push_back({});
    std::vector<std::size_t> queue{start};
    componentByNode[start] = componentIndex;
    for (std::size_t cursor = 0; cursor < queue.size(); ++cursor) {
      const std::size_t node = queue[cursor];
      components.back().members.push_back(node);
      for (const std::size_t neighbor : adjacency[node]) {
        if (core[neighbor]) continue;
        if (componentByNode[neighbor] >= 0) continue;
        componentByNode[neighbor] = componentIndex;
        queue.push_back(neighbor);
      }
    }
  }
  for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
    const Edge& edge = edges[edgeIndex];
    const int sourceComponent = componentByNode[edge.source];
    const int targetComponent = componentByNode[edge.target];
    if (sourceComponent >= 0) {
      components[static_cast<std::size_t>(sourceComponent)].edgeIndices.push_back(edgeIndex);
      if (core[edge.target]) {
        components[static_cast<std::size_t>(sourceComponent)].anchors.push_back(edge.target);
      }
    } else if (targetComponent >= 0) {
      components[static_cast<std::size_t>(targetComponent)].edgeIndices.push_back(edgeIndex);
      if (core[edge.source]) {
        components[static_cast<std::size_t>(targetComponent)].anchors.push_back(edge.source);
      }
    }
  }
  for (ComponentBundle& component : components) {
    std::sort(component.anchors.begin(), component.anchors.end());
    component.anchors.erase(
      std::unique(component.anchors.begin(), component.anchors.end()),
      component.anchors.end());
  }
  std::vector<std::size_t> anchored;
  std::vector<std::size_t> detached;
  for (std::size_t index = 0; index < components.size(); ++index) {
    if (components[index].anchors.empty()) detached.push_back(index);
    else anchored.push_back(index);
  }
  std::sort(anchored.begin(), anchored.end(), [&](std::size_t left, std::size_t right) {
    const ComponentBundle& a = components[left];
    const ComponentBundle& b = components[right];
    if (a.anchors.size() != b.anchors.size()) return a.anchors.size() > b.anchors.size();
    if (a.edgeIndices.size() != b.edgeIndices.size()) {
      return a.edgeIndices.size() > b.edgeIndices.size();
    }
    return nodes[a.members.front()].id < nodes[b.members.front()].id;
  });

  for (std::size_t node = 0; node < nodes.size(); ++node) {
    positioned[node] = core[node];
  }
  std::vector<ActiveRoute> activeRoutes;
  auto rebuildActiveRoutes = [&]() {
    activeRoutes.clear();
    for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
      const Edge& edge = edges[edgeIndex];
      if (!positioned[edge.source] || !positioned[edge.target]) continue;
      activeRoutes.push_back({
        edgeIndex,
        routeSingleStraight(edge, nodes, positions, positioned),
      });
    }
  };
  rebuildActiveRoutes();
  std::cerr << "component-start core="
            << std::count(core.begin(), core.end(), true)
            << " anchored=" << anchored.size()
            << " detached=" << detached.size()
            << " coreEdges=" << activeRoutes.size() << '\n';
  constexpr double tau = 6.283185307179586476925286766559;
  std::size_t zeroConflict = 0;
  std::size_t inserted = 0;
  std::size_t approximateCrossings = 0;
  std::size_t approximateEdgeNode = 0;
  for (const std::size_t componentIndex : anchored) {
    const ComponentBundle& component = components[componentIndex];
    Point templateCenter{0.0, 0.0};
    for (const std::size_t member : component.members) {
      templateCenter.x += templates[member].x;
      templateCenter.y += templates[member].y;
    }
    templateCenter.x /= static_cast<double>(component.members.size());
    templateCenter.y /= static_cast<double>(component.members.size());
    std::vector<Point> local;
    local.reserve(component.members.size());
    double localRadius = 1.0;
    for (const std::size_t member : component.members) {
      const Point point{
        templates[member].x - templateCenter.x,
        templates[member].y - templateCenter.y,
      };
      local.push_back(point);
      localRadius = std::max(localRadius, std::hypot(point.x, point.y));
    }
    Point anchorCenter{0.0, 0.0};
    for (const std::size_t anchor : component.anchors) {
      anchorCenter.x += positions[anchor].x;
      anchorCenter.y += positions[anchor].y;
    }
    anchorCenter.x /= static_cast<double>(component.anchors.size());
    anchorCenter.y /= static_cast<double>(component.anchors.size());
    const Bounds activeBounds = measurePlacedBounds(nodes, positions, positioned);
    const double activeDiagonal = std::hypot(activeBounds.width(), activeBounds.height());
    const double baseRadius = localRadius
      + std::sqrt(static_cast<double>(component.members.size())) * 120.0 + 120.0;
    std::vector<Point> centers{anchorCenter, templateCenter};
    for (const double multiplier : {1.0, 2.0, 4.0, 7.0}) {
      appendRingCandidates(
        centers,
        anchorCenter,
        std::min(std::max(800.0, activeDiagonal * 0.35), baseRadius * multiplier),
        angularSamples,
        stablePhase(nodes[component.members.front()].id));
    }
    if (component.anchors.size() == 2) {
      const Point source = positions[component.anchors[0]];
      const Point target = positions[component.anchors[1]];
      const double dx = target.x - source.x;
      const double dy = target.y - source.y;
      const double length = std::hypot(dx, dy);
      const double nx = length < 1e-6 ? 0.0 : -dy / length;
      const double ny = length < 1e-6 ? 1.0 : dx / length;
      for (const double fraction : {0.2, 0.35, 0.5, 0.65, 0.8}) {
        for (const double side : {-1.0, 1.0}) {
          for (const double multiplier : {1.0, 2.5, 5.0}) {
            centers.push_back({
              source.x + dx * fraction + nx * baseRadius * multiplier * side,
              source.y + dy * fraction + ny * baseRadius * multiplier * side,
            });
          }
        }
      }
    }
    const double outsideGap = baseRadius + 240.0;
    centers.push_back({activeBounds.left - outsideGap, anchorCenter.y});
    centers.push_back({activeBounds.right + outsideGap, anchorCenter.y});
    centers.push_back({anchorCenter.x, activeBounds.top - outsideGap});
    centers.push_back({anchorCenter.x, activeBounds.bottom + outsideGap});

    PartialPlacementCost bestCost;
    bool hasBest = false;
    std::vector<Point> bestPoints;
    const int rotations = component.members.size() == 1 ? 1 : 4;
    const int reflections = component.members.size() == 1 ? 1 : 2;
    const std::vector<double> scales = component.members.size() == 1
      ? std::vector<double>{1.0}
      : std::vector<double>{1.0, 1.5, 2.25};
    for (const Point& center : centers) {
      for (int reflection = 0; reflection < reflections; ++reflection) {
        for (int rotation = 0; rotation < rotations; ++rotation) {
          const double angle = tau * static_cast<double>(rotation)
            / static_cast<double>(rotations);
          const double cosine = std::cos(angle);
          const double sine = std::sin(angle);
          for (const double scale : scales) {
            std::vector<Point> candidatePoints;
            candidatePoints.reserve(local.size());
            for (Point point : local) {
              if (reflection != 0) point.x = -point.x;
              candidatePoints.push_back({
                center.x + (point.x * cosine - point.y * sine) * scale,
                center.y + (point.x * sine + point.y * cosine) * scale,
              });
            }
            const PartialPlacementCost cost = evaluateComponentCandidate(
              component,
              candidatePoints,
              nodes,
              edges,
              positions,
              positioned,
              activeRoutes);
            if (!hasBest || betterPartialCost(cost, bestCost, edgeNodeWeight)) {
              hasBest = true;
              bestCost = cost;
              bestPoints = std::move(candidatePoints);
            }
          }
        }
      }
    }
    if (!hasBest) throw std::runtime_error("no component bundle candidate");
    for (std::size_t index = 0; index < component.members.size(); ++index) {
      positions[component.members[index]] = bestPoints[index];
      positioned[component.members[index]] = true;
    }
    approximateCrossings += bestCost.crossings;
    approximateEdgeNode += bestCost.edgeNodeIntersections;
    if (component.anchors.size() > 2 || bestCost.crossings > 40) {
      std::cerr << "component-detail members=" << component.members.size()
                << " anchors=" << component.anchors.size()
                << " edges=" << component.edgeIndices.size()
                << " cross=" << bestCost.crossings
                << " edgeNode=" << bestCost.edgeNodeIntersections
                << " overlaps=" << bestCost.nodeOverlaps
                << " first=" << nodes[component.members.front()].id << '\n';
    }
    if (bestCost.crossings == 0
        && bestCost.edgeNodeIntersections == 0
        && bestCost.nodeOverlaps == 0
        && bestCost.segmentOverlaps == 0) {
      ++zeroConflict;
    }
    ++inserted;
    rebuildActiveRoutes();
    if (inserted % 50 == 0 || inserted == anchored.size()) {
      std::cerr << "component-progress inserted=" << inserted
                << '/' << anchored.size()
                << " zeroConflict=" << zeroConflict
                << " approxCross=" << approximateCrossings
                << " approxEdgeNode=" << approximateEdgeNode
                << " activeEdges=" << activeRoutes.size() << '\n';
    }
  }

  Bounds placedBounds = measurePlacedBounds(nodes, positions, positioned);
  double cursorX = placedBounds.right + 1000.0;
  double cursorY = placedBounds.top;
  double columnWidth = 0.0;
  const double columnHeight = std::max(4000.0, placedBounds.height());
  for (const std::size_t componentIndex : detached) {
    const ComponentBundle& component = components[componentIndex];
    double left = std::numeric_limits<double>::infinity();
    double top = std::numeric_limits<double>::infinity();
    double right = -std::numeric_limits<double>::infinity();
    double bottom = -std::numeric_limits<double>::infinity();
    for (const std::size_t member : component.members) {
      const Rect rect = nodeRect(nodes[member], templates[member]);
      left = std::min(left, rect.left);
      top = std::min(top, rect.top);
      right = std::max(right, rect.right);
      bottom = std::max(bottom, rect.bottom);
    }
    const double width = right - left;
    const double height = bottom - top;
    if (cursorY > placedBounds.top && cursorY + height > placedBounds.top + columnHeight) {
      cursorX += columnWidth + 500.0;
      cursorY = placedBounds.top;
      columnWidth = 0.0;
    }
    for (const std::size_t member : component.members) {
      positions[member] = {
        cursorX + templates[member].x - left,
        cursorY + templates[member].y - top,
      };
      positioned[member] = true;
    }
    cursorY += height + 240.0;
    columnWidth = std::max(columnWidth, width);
  }
}

Bounds measureNodeBounds(
    const std::vector<Node>& nodes,
    const std::vector<Point>& positions) {
  if (nodes.empty()) return {};
  Bounds bounds;
  const Rect first = nodeRect(nodes.front(), positions.front());
  bounds.left = first.left;
  bounds.top = first.top;
  bounds.right = first.right;
  bounds.bottom = first.bottom;
  for (std::size_t node = 1; node < nodes.size(); ++node) {
    const Rect rect = nodeRect(nodes[node], positions[node]);
    bounds.left = std::min(bounds.left, rect.left);
    bounds.top = std::min(bounds.top, rect.top);
    bounds.right = std::max(bounds.right, rect.right);
    bounds.bottom = std::max(bounds.bottom, rect.bottom);
  }
  return bounds;
}

std::vector<NodeBundle> buildNodeBundles(
    const std::vector<Node>& nodes,
    const std::vector<Edge>& edges,
    const std::vector<std::vector<std::size_t>>& adjacency,
    int anchorMinDegree,
    int memberMaxDegree) {
  std::vector<NodeBundle> bundles;
  std::vector<bool> assigned(nodes.size(), false);

  // Inheritance is directional in the canonical graph: concrete child ->
  // base model.  Children with the same base form a semantic node family even
  // when their fields and their additional FK/O2O relations differ.
  std::map<std::size_t, std::vector<std::size_t>> inheritanceByParent;
  for (const Edge& edge : edges) {
    if (edge.kind == "inheritance") {
      inheritanceByParent[edge.target].push_back(edge.source);
    }
  }
  for (auto& item : inheritanceByParent) {
    std::vector<std::size_t>& members = item.second;
    std::sort(members.begin(), members.end());
    members.erase(std::unique(members.begin(), members.end()), members.end());
    if (members.size() < 2) continue;
    bundles.push_back({item.first, members, "inheritance-anchor"});
    for (const std::size_t member : members) assigned[member] = true;
  }

  // Remaining peripheral models choose a dominant real neighbour.  This is a
  // deliberately tolerant assignment: no equal field set, equal degree, or
  // equal neighbour signature is required.  A model can carry arbitrary
  // extra relations; those relations later determine its order inside the
  // family rather than ejecting it from the family.
  std::map<std::size_t, std::vector<std::size_t>> membersByAnchor;
  for (std::size_t node = 0; node < nodes.size(); ++node) {
    if (assigned[node] || adjacency[node].empty()) continue;
    if (static_cast<int>(adjacency[node].size()) > memberMaxDegree) continue;
    std::size_t bestAnchor = nodes.size();
    std::size_t bestDegree = 0;
    for (const std::size_t neighbor : adjacency[node]) {
      const std::size_t degree = adjacency[neighbor].size();
      if (degree > bestDegree
          || (degree == bestDegree
              && bestAnchor < nodes.size()
              && nodes[neighbor].id < nodes[bestAnchor].id)) {
        bestDegree = degree;
        bestAnchor = neighbor;
      }
    }
    if (bestAnchor >= nodes.size()
        || static_cast<int>(bestDegree) < anchorMinDegree) {
      continue;
    }
    membersByAnchor[bestAnchor].push_back(node);
  }
  for (auto& item : membersByAnchor) {
    std::vector<std::size_t>& members = item.second;
    if (members.size() < 2) continue;
    std::sort(members.begin(), members.end(), [&](std::size_t left, std::size_t right) {
      return nodes[left].id < nodes[right].id;
    });
    bundles.push_back({item.first, members, "dominant-relation-anchor"});
  }
  return bundles;
}

std::vector<std::size_t> nodeConflictPressure(
    const std::vector<Node>& nodes,
    const std::vector<Edge>& edges,
    const std::vector<Point>& positions,
    const std::vector<Segment>& routes) {
  std::vector<std::size_t> pressure(nodes.size(), 0);
  for (std::size_t left = 0; left < routes.size(); ++left) {
    for (std::size_t right = left + 1; right < routes.size(); ++right) {
      if (!properSegmentIntersection(
            routes[left].source,
            routes[left].target,
            routes[right].source,
            routes[right].target)) {
        continue;
      }
      ++pressure[edges[left].source];
      ++pressure[edges[left].target];
      ++pressure[edges[right].source];
      ++pressure[edges[right].target];
    }
  }
  std::vector<Rect> rectangles;
  rectangles.reserve(nodes.size());
  for (std::size_t node = 0; node < nodes.size(); ++node) {
    rectangles.push_back(nodeRect(
      nodes[node], positions[node], kEdgeNodeMargin));
  }
  for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
    for (std::size_t node = 0; node < nodes.size(); ++node) {
      if (node == edges[edgeIndex].source || node == edges[edgeIndex].target) {
        continue;
      }
      if (!segmentIntersectsRect(
            routes[edgeIndex].source,
            routes[edgeIndex].target,
            rectangles[node])) {
        continue;
      }
      pressure[edges[edgeIndex].source] += 2;
      pressure[edges[edgeIndex].target] += 2;
      pressure[node] += 2;
    }
  }
  return pressure;
}

std::vector<std::size_t> orderBundleMembers(
    const NodeBundle& bundle,
    const std::vector<Node>& nodes,
    const std::vector<std::vector<std::size_t>>& adjacency,
    const std::vector<Point>& positions) {
  std::unordered_set<std::size_t> family(bundle.members.begin(), bundle.members.end());
  family.insert(bundle.anchor);
  const Point anchor = positions[bundle.anchor];
  std::vector<std::pair<double, std::size_t>> keyed;
  keyed.reserve(bundle.members.size());
  for (const std::size_t member : bundle.members) {
    Point toward{0.0, 0.0};
    std::size_t externalCount = 0;
    for (const std::size_t neighbor : adjacency[member]) {
      if (family.count(neighbor)) continue;
      toward.x += positions[neighbor].x;
      toward.y += positions[neighbor].y;
      ++externalCount;
    }
    if (externalCount > 0) {
      toward.x /= static_cast<double>(externalCount);
      toward.y /= static_cast<double>(externalCount);
    } else {
      toward = positions[member];
    }
    const double angle = std::atan2(toward.y - anchor.y, toward.x - anchor.x);
    keyed.emplace_back(angle, member);
  }
  std::sort(keyed.begin(), keyed.end(), [&](const auto& left, const auto& right) {
    if (left.first != right.first) return left.first < right.first;
    return nodes[left.second].id < nodes[right.second].id;
  });
  std::vector<std::size_t> order;
  order.reserve(keyed.size());
  for (const auto& item : keyed) order.push_back(item.second);
  return order;
}

std::vector<Point> arrangeBundleArc(
    const NodeBundle& bundle,
    const std::vector<std::size_t>& order,
    const std::vector<Node>& nodes,
    const std::vector<Point>& positions,
    double centerAngle,
    double span,
    double radiusScale) {
  std::vector<Point> candidate = positions;
  if (order.empty()) return candidate;
  const double tau = 6.283185307179586476925286766559;
  const bool fullCircle = span >= tau - 0.01;
  const double delta = order.size() <= 1
    ? 1.0
    : (fullCircle
       ? span / static_cast<double>(order.size())
       : span / static_cast<double>(order.size() - 1));
  double maximumDiameter = 1.0;
  for (const std::size_t member : order) {
    maximumDiameter = std::max(
      maximumDiameter,
      std::hypot(nodes[member].width + 16.0, nodes[member].height + 16.0));
  }
  const double anchorRadius = std::hypot(
    nodes[bundle.anchor].width + 16.0,
    nodes[bundle.anchor].height + 16.0) * 0.5;
  const double chord = std::max(0.02, 2.0 * std::sin(std::min(3.0, delta) * 0.5));
  const double radius = std::max(
    anchorRadius + maximumDiameter * 0.65 + 48.0,
    (maximumDiameter + 36.0) / chord) * radiusScale;
  const double start = fullCircle
    ? centerAngle - span * 0.5 + delta * 0.5
    : centerAngle - span * 0.5;
  const Point anchor = positions[bundle.anchor];
  for (std::size_t index = 0; index < order.size(); ++index) {
    const double angle = order.size() == 1
      ? centerAngle
      : start + delta * static_cast<double>(index);
    candidate[order[index]] = {
      anchor.x + std::cos(angle) * radius,
      anchor.y + std::sin(angle) * radius,
    };
  }
  return candidate;
}

std::uint64_t weightedConflictCost(const Score& score, double edgeNodeWeight) {
  const long double value =
    static_cast<long double>(score.edgeCrossings)
    + static_cast<long double>(score.edgeNodeIntersections) * edgeNodeWeight
    + static_cast<long double>(score.nodeOverlaps) * 1000.0L;
  return static_cast<std::uint64_t>(std::min<long double>(
    value, static_cast<long double>(std::numeric_limits<std::uint64_t>::max())));
}

bool betterCandidate(
    const Score& candidate,
    const Score& best,
    double edgeNodeWeight,
    double candidateArea,
    double bestArea) {
  if (candidate.nodeOverlaps > best.nodeOverlaps) return false;
  if (candidate.visual() > best.visual()) return false;
  const std::uint64_t candidateCost =
    weightedConflictCost(candidate, edgeNodeWeight);
  const std::uint64_t bestCost = weightedConflictCost(best, edgeNodeWeight);
  if (candidateCost != bestCost) return candidateCost < bestCost;
  if (candidate.visual() != best.visual()) {
    return candidate.visual() < best.visual();
  }
  return candidateArea < bestArea;
}

void writePositions(
    const std::string& path,
    const std::vector<Node>& nodes,
    const std::vector<Point>& positions);

Score optimizeExactNodes(
    const std::vector<Node>& nodes,
    const std::vector<Edge>& edges,
    const std::vector<std::vector<std::size_t>>& adjacency,
    std::vector<Point>& positions,
    const std::string& outputPath,
    int rounds,
    int nodeLimit,
    int angularSamples,
    double maximumRadiusFactor,
    double edgeNodeWeight,
    double maximumBboxGrowth) {
  std::vector<bool> placed(nodes.size(), true);
  std::vector<Segment> routes = routeAllEdgesStraight(nodes, edges, positions);
  Score current = measureScore(nodes, edges, positions, routes);
  const Bounds initialBounds = measureNodeBounds(nodes, positions);
  const double maximumWidth = initialBounds.width() * maximumBboxGrowth;
  const double maximumHeight = initialBounds.height() * maximumBboxGrowth;
  std::cerr << "exact-node-start rounds=" << rounds
            << " limit=" << nodeLimit
            << " edgeCross=" << current.edgeCrossings
            << " edgeNode=" << current.edgeNodeIntersections
            << " nodeOverlap=" << current.nodeOverlaps
            << " visual=" << current.visual() << '\n';

  for (int round = 0; round < rounds; ++round) {
    routes = routeAllEdgesStraight(nodes, edges, positions);
    const std::vector<std::size_t> pressure =
      nodeConflictPressure(nodes, edges, positions, routes);
    std::vector<std::size_t> order(nodes.size());
    for (std::size_t node = 0; node < nodes.size(); ++node) order[node] = node;
    std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
      if (pressure[left] != pressure[right]) return pressure[left] > pressure[right];
      return nodes[left].id < nodes[right].id;
    });
    if (nodeLimit > 0 && order.size() > static_cast<std::size_t>(nodeLimit)) {
      order.resize(static_cast<std::size_t>(nodeLimit));
    }

    std::size_t accepted = 0;
    for (const std::size_t node : order) {
      if (pressure[node] == 0) break;
      const std::vector<Point> candidates = shellCandidates(
        node,
        adjacency[node],
        nodes,
        positions,
        placed,
        angularSamples,
        maximumRadiusFactor);
      const Point original = positions[node];
      std::vector<Point> bestPositions;
      Score best = current;
      double bestArea = measureNodeBounds(nodes, positions).area();
      for (const Point& candidate : candidates) {
        if (!std::isfinite(candidate.x) || !std::isfinite(candidate.y)) continue;
        positions[node] = candidate;
        const Bounds candidateBounds = measureNodeBounds(nodes, positions);
        if (candidateBounds.width() > maximumWidth
            || candidateBounds.height() > maximumHeight) {
          continue;
        }
        const std::vector<Segment> candidateRoutes =
          routeAllEdgesStraight(nodes, edges, positions);
        const Score candidateScore =
          measureScore(nodes, edges, positions, candidateRoutes);
        if (!betterCandidate(
              candidateScore,
              best,
              edgeNodeWeight,
              candidateBounds.area(),
              bestArea)) {
          continue;
        }
        best = candidateScore;
        bestArea = candidateBounds.area();
        bestPositions = positions;
      }
      positions[node] = original;
      if (bestPositions.empty()
          || !betterCandidate(
            best,
            current,
            edgeNodeWeight,
            bestArea,
            measureNodeBounds(nodes, positions).area())) {
        continue;
      }
      const Score before = current;
      positions = std::move(bestPositions);
      current = best;
      ++accepted;
      writePositions(outputPath, nodes, positions);
      std::cerr << "exact-node round=" << (round + 1)
                << " node=" << nodes[node].id
                << " visual=" << before.visual() << "->" << current.visual()
                << " edgeCross=" << before.edgeCrossings
                << "->" << current.edgeCrossings
                << " edgeNode=" << before.edgeNodeIntersections
                << "->" << current.edgeNodeIntersections << '\n';
    }
    std::cerr << "exact-node-round-done round=" << (round + 1)
              << " accepted=" << accepted
              << " visual=" << current.visual() << '\n';
    if (accepted == 0) break;
  }
  return current;
}

Score optimizeNodeBundles(
    const std::vector<Node>& nodes,
    const std::vector<Edge>& edges,
    const std::vector<std::vector<std::size_t>>& adjacency,
    const std::vector<NodeBundle>& bundles,
    std::vector<Point>& positions,
    const std::string& outputPath,
    int rounds,
    int groupLimit,
    int directions,
    double edgeNodeWeight,
    double maximumBboxGrowth) {
  std::vector<Segment> routes = routeAllEdgesStraight(nodes, edges, positions);
  Score current = measureScore(nodes, edges, positions, routes);
  const Bounds initialBounds = measureNodeBounds(nodes, positions);
  const double maximumWidth = initialBounds.width() * maximumBboxGrowth;
  const double maximumHeight = initialBounds.height() * maximumBboxGrowth;
  std::cerr << "bundle-start groups=" << bundles.size()
            << " edgeCross=" << current.edgeCrossings
            << " edgeNode=" << current.edgeNodeIntersections
            << " nodeOverlap=" << current.nodeOverlaps
            << " visual=" << current.visual()
            << " bbox=" << initialBounds.width() << 'x' << initialBounds.height()
            << '\n';
  const double tau = 6.283185307179586476925286766559;
  const std::vector<double> spans{tau * 0.25, tau * 0.5, tau * 0.75, tau};
  const std::vector<double> radiusScales{1.0, 1.35};

  for (int round = 0; round < rounds; ++round) {
    routes = routeAllEdgesStraight(nodes, edges, positions);
    const std::vector<std::size_t> nodePressure =
      nodeConflictPressure(nodes, edges, positions, routes);
    std::vector<std::pair<std::size_t, std::size_t>> ranked;
    ranked.reserve(bundles.size());
    for (std::size_t bundleIndex = 0;
         bundleIndex < bundles.size(); ++bundleIndex) {
      const NodeBundle& bundle = bundles[bundleIndex];
      std::size_t pressure = nodePressure[bundle.anchor];
      for (const std::size_t member : bundle.members) {
        pressure += nodePressure[member];
      }
      ranked.emplace_back(pressure, bundleIndex);
    }
    std::sort(ranked.begin(), ranked.end(), [&](const auto& left, const auto& right) {
      if (left.first != right.first) return left.first > right.first;
      return nodes[bundles[left.second].anchor].id
        < nodes[bundles[right.second].anchor].id;
    });
    if (groupLimit > 0
        && ranked.size() > static_cast<std::size_t>(groupLimit)) {
      ranked.resize(static_cast<std::size_t>(groupLimit));
    }

    std::size_t accepted = 0;
    for (std::size_t rank = 0; rank < ranked.size(); ++rank) {
      const NodeBundle& bundle = bundles[ranked[rank].second];
      std::vector<std::size_t> order = orderBundleMembers(
        bundle, nodes, adjacency, positions);
      std::vector<Point> bestPositions = positions;
      Score best = current;
      double bestArea = measureNodeBounds(nodes, positions).area();
      const Bounds currentBounds = measureNodeBounds(nodes, positions);
      const Point graphCenter{
        (currentBounds.left + currentBounds.right) * 0.5,
        (currentBounds.top + currentBounds.bottom) * 0.5,
      };
      const Point anchor = positions[bundle.anchor];
      const double outward = std::atan2(
        anchor.y - graphCenter.y, anchor.x - graphCenter.x);

      for (int reversed = 0; reversed < 2; ++reversed) {
        if (reversed != 0) std::reverse(order.begin(), order.end());
        for (int direction = 0; direction < directions; ++direction) {
          const double centerAngle =
            outward + tau * static_cast<double>(direction)
              / static_cast<double>(std::max(1, directions));
          for (const double span : spans) {
            for (const double radiusScale : radiusScales) {
              std::vector<Point> candidate = arrangeBundleArc(
                bundle,
                order,
                nodes,
                positions,
                centerAngle,
                span,
                radiusScale);
              const Bounds candidateBounds = measureNodeBounds(nodes, candidate);
              if (candidateBounds.width() > maximumWidth
                  || candidateBounds.height() > maximumHeight) {
                continue;
              }
              const std::vector<Segment> candidateRoutes =
                routeAllEdgesStraight(nodes, edges, candidate);
              const Score candidateScore =
                measureScore(nodes, edges, candidate, candidateRoutes);
              if (betterCandidate(
                    candidateScore,
                    best,
                    edgeNodeWeight,
                    candidateBounds.area(),
                    bestArea)) {
                best = candidateScore;
                bestArea = candidateBounds.area();
                bestPositions = std::move(candidate);
              }
            }
          }
        }
        if (reversed != 0) std::reverse(order.begin(), order.end());
      }
      if (!betterCandidate(
            best,
            current,
            edgeNodeWeight,
            bestArea,
            measureNodeBounds(nodes, positions).area())) {
        std::cerr << "bundle round=" << (round + 1)
                  << " rank=" << (rank + 1)
                  << " anchor=" << nodes[bundle.anchor].id
                  << " members=" << bundle.members.size()
                  << " reason=" << bundle.reason
                  << " accepted=0 visual=" << current.visual() << '\n';
        continue;
      }
      const Score before = current;
      positions = std::move(bestPositions);
      current = best;
      ++accepted;
      writePositions(outputPath, nodes, positions);
      std::cerr << "bundle round=" << (round + 1)
                << " rank=" << (rank + 1)
                << " anchor=" << nodes[bundle.anchor].id
                << " members=" << bundle.members.size()
                << " reason=" << bundle.reason
                << " accepted=1 visual=" << before.visual()
                << "->" << current.visual()
                << " edgeCross=" << before.edgeCrossings
                << "->" << current.edgeCrossings
                << " edgeNode=" << before.edgeNodeIntersections
                << "->" << current.edgeNodeIntersections
                << " nodeOverlap=" << before.nodeOverlaps
                << "->" << current.nodeOverlaps << '\n';
    }
    std::cerr << "bundle-round-done round=" << (round + 1)
              << " accepted=" << accepted
              << " visual=" << current.visual() << '\n';
    if (accepted == 0) break;
  }
  return current;
}

void writePositions(
    const std::string& path,
    const std::vector<Node>& nodes,
    const std::vector<Point>& positions) {
  std::ofstream stream(path);
  if (!stream) throw std::runtime_error("cannot write " + path);
  stream << std::fixed << std::setprecision(9);
  for (std::size_t node = 0; node < nodes.size(); ++node) {
    stream << nodes[node].id << '\t'
           << positions[node].x << '\t'
           << positions[node].y << '\n';
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const std::string nodesPath = stringArg(argv, argc, "--nodes");
    const std::string edgesPath = stringArg(argv, argc, "--edges");
    const std::string positionsPath = stringArg(argv, argc, "--positions");
    const std::string outputPath = stringArg(argv, argc, "--out", positionsPath);
    const int rounds = std::max(0, intArg(argv, argc, "--rounds", 0));
    const int groupLimit = std::max(0, intArg(argv, argc, "--group-limit", 24));
    const int directions = std::max(2, intArg(argv, argc, "--directions", 8));
    const int anchorMinDegree = std::max(
      2, intArg(argv, argc, "--anchor-min-degree", 4));
    const int memberMaxDegree = std::max(
      1, intArg(argv, argc, "--member-max-degree", 8));
    const double edgeNodeWeight = std::max(
      1.0, doubleArg(argv, argc, "--edge-node-weight", 4.0));
    const double maximumBboxGrowth = std::max(
      1.0, doubleArg(argv, argc, "--max-bbox-growth", 1.15));
    const bool constructShell = hasArg(argv, argc, "--construct-shell");
    const bool constructComponents = hasArg(
      argv, argc, "--construct-component-bundles");
    const std::string fixedCoreNodesPath = hasArg(argv, argc, "--fixed-core-nodes")
      ? stringArg(argv, argc, "--fixed-core-nodes")
      : std::string{};
    const int shellAngularSamples = std::max(
      8, intArg(argv, argc, "--shell-angular-samples", 16));
    const double shellMaximumRadiusFactor = std::max(
      0.05, doubleArg(argv, argc, "--shell-max-radius-factor", 0.35));
    const double coreScale = std::max(
      0.25, doubleArg(argv, argc, "--core-scale", 1.0));
    const int exactNodeRounds = std::max(
      0, intArg(argv, argc, "--exact-node-rounds", 0));
    const int exactNodeLimit = std::max(
      0, intArg(argv, argc, "--exact-node-limit", 32));
    const int exactNodeAngularSamples = std::max(
      4, intArg(argv, argc, "--exact-node-angular-samples", 8));
    const double exactNodeMaximumRadiusFactor = std::max(
      0.05, doubleArg(argv, argc, "--exact-node-max-radius-factor", 0.25));

    std::vector<Node> nodes;
    std::unordered_map<std::string, std::size_t> indexById;
    {
      std::ifstream stream(nodesPath);
      if (!stream) throw std::runtime_error("cannot read " + nodesPath);
      std::string line;
      while (std::getline(stream, line)) {
        if (line.empty()) continue;
        const std::vector<std::string> fields = splitTabs(line);
        if (fields.size() < 3) throw std::runtime_error("bad node row");
        Node node;
        node.id = fields[0];
        node.width = std::max(1.0, std::stod(fields[1]));
        node.height = std::max(1.0, std::stod(fields[2]));
        indexById[node.id] = nodes.size();
        nodes.push_back(std::move(node));
      }
    }

    std::vector<Edge> edges;
    {
      std::ifstream stream(edgesPath);
      if (!stream) throw std::runtime_error("cannot read " + edgesPath);
      std::string line;
      while (std::getline(stream, line)) {
        if (line.empty()) continue;
        const std::vector<std::string> fields = splitTabs(line);
        if (fields.size() < 4) throw std::runtime_error("bad edge row");
        const auto source = indexById.find(fields[1]);
        const auto target = indexById.find(fields[2]);
        if (source == indexById.end() || target == indexById.end()) {
          throw std::runtime_error("edge references unknown node");
        }
        if (source->second == target->second) continue;
        edges.push_back({
          fields[0], source->second, target->second, fields[3]});
      }
    }

    std::vector<Point> positions(nodes.size());
    std::vector<bool> positioned(nodes.size(), false);
    {
      std::ifstream stream(positionsPath);
      if (!stream) throw std::runtime_error("cannot read " + positionsPath);
      std::string line;
      while (std::getline(stream, line)) {
        if (line.empty()) continue;
        const std::vector<std::string> fields = splitTabs(line);
        if (fields.size() < 3) continue;
        const auto found = indexById.find(fields[0]);
        if (found == indexById.end()) continue;
        positions[found->second] = {
          std::stod(fields[1]), std::stod(fields[2])};
        positioned[found->second] = true;
      }
    }
    std::vector<std::vector<std::size_t>> adjacency(nodes.size());
    std::vector<std::vector<std::size_t>> incidentEdges(nodes.size());
    for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
      const Edge& edge = edges[edgeIndex];
      adjacency[edge.source].push_back(edge.target);
      adjacency[edge.target].push_back(edge.source);
      incidentEdges[edge.source].push_back(edgeIndex);
      incidentEdges[edge.target].push_back(edgeIndex);
    }
    for (std::vector<std::size_t>& neighbors : adjacency) {
      std::sort(neighbors.begin(), neighbors.end());
      neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    }
    if (constructComponents) {
      if (std::find(positioned.begin(), positioned.end(), false)
          != positioned.end()) {
        throw std::runtime_error(
          "component template positions must cover every node");
      }
      std::unordered_set<std::string> fixedCoreIds;
      if (!fixedCoreNodesPath.empty()) {
        std::ifstream fixedCoreStream(fixedCoreNodesPath);
        if (!fixedCoreStream) {
          throw std::runtime_error("cannot read " + fixedCoreNodesPath);
        }
        std::string fixedCoreLine;
        while (std::getline(fixedCoreStream, fixedCoreLine)) {
          if (fixedCoreLine.empty()) continue;
          const std::size_t separator = fixedCoreLine.find('\t');
          fixedCoreIds.insert(fixedCoreLine.substr(0, separator));
        }
      }
      constructComponentBundles(
        nodes,
        edges,
        adjacency,
        incidentEdges,
        positions,
        positioned,
        shellAngularSamples,
        edgeNodeWeight,
        fixedCoreIds);
    } else if (constructShell) {
      constructDegenerateShell(
        nodes,
        edges,
        adjacency,
        incidentEdges,
        positions,
        positioned,
        shellAngularSamples,
        shellMaximumRadiusFactor,
        edgeNodeWeight,
        coreScale);
    } else if (
        std::find(positioned.begin(), positioned.end(), false)
        != positioned.end()) {
      throw std::runtime_error("positions file does not cover every node");
    }
    const std::vector<NodeBundle> bundles = buildNodeBundles(
      nodes,
      edges,
      adjacency,
      anchorMinDegree,
      memberMaxDegree);
    std::size_t inheritanceBundles = 0;
    std::size_t dominantBundles = 0;
    std::size_t bundledMembers = 0;
    for (const NodeBundle& bundle : bundles) {
      bundledMembers += bundle.members.size();
      if (bundle.reason == "inheritance-anchor") {
        ++inheritanceBundles;
      } else {
        ++dominantBundles;
      }
      if (hasArg(argv, argc, "--dump-bundles")) {
        std::cerr << "bundle-def anchor=" << nodes[bundle.anchor].id
                  << " members=" << bundle.members.size()
                  << " reason=" << bundle.reason << '\n';
      }
    }
    std::cerr << "bundle-defs total=" << bundles.size()
              << " inheritance=" << inheritanceBundles
              << " dominant=" << dominantBundles
              << " memberships=" << bundledMembers << '\n';

    std::vector<Segment> routes =
      routeAllEdgesStraight(nodes, edges, positions);
    Score initialScore = measureScore(nodes, edges, positions, routes);
    writePositions(outputPath, nodes, positions);
    Score finalScore = initialScore;
    if (exactNodeRounds > 0) {
      finalScore = optimizeExactNodes(
        nodes,
        edges,
        adjacency,
        positions,
        outputPath,
        exactNodeRounds,
        exactNodeLimit,
        exactNodeAngularSamples,
        exactNodeMaximumRadiusFactor,
        edgeNodeWeight,
        maximumBboxGrowth);
      initialScore = finalScore;
      writePositions(outputPath, nodes, positions);
    }
    if (rounds > 0 && !bundles.empty()) {
      finalScore = optimizeNodeBundles(
        nodes,
        edges,
        adjacency,
        bundles,
        positions,
        outputPath,
        rounds,
        groupLimit,
        directions,
        edgeNodeWeight,
        maximumBboxGrowth);
      writePositions(outputPath, nodes, positions);
    }
    std::cerr << "score nodes=" << nodes.size()
              << " edges=" << edges.size()
              << " edgeCross=" << finalScore.edgeCrossings
              << " edgeNode=" << finalScore.edgeNodeIntersections
              << " nodeOverlap=" << finalScore.nodeOverlaps
              << " visual=" << finalScore.visual();
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
#if defined(__APPLE__)
      const double peakMib = static_cast<double>(usage.ru_maxrss)
        / (1024.0 * 1024.0);
#else
      const double peakMib = static_cast<double>(usage.ru_maxrss) / 1024.0;
#endif
      std::cerr << " peakMiB=" << peakMib;
    }
    std::cerr << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
