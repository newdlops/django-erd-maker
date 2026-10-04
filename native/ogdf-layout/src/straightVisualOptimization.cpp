#include "straightVisualOptimization.h"
#include "routeBoundsIndex.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace djerd {
namespace {
constexpr double epsilon = 1e-9;
Rect rect(const StraightVisualNode& node, double margin = 0) {
  return {node.y + node.height / 2 + margin, node.x - node.width / 2 - margin,
          node.x + node.width / 2 + margin, node.y - node.height / 2 - margin};
}
Rect bounds(const StraightVisualRoute& route) {
  return {std::max(route.sourceY, route.targetY), std::min(route.sourceX, route.targetX),
          std::max(route.sourceX, route.targetX), std::min(route.sourceY, route.targetY)};
}
Rect expand(Rect value, double margin) {
  value.left -= margin; value.right += margin;
  value.top -= margin; value.bottom += margin;
  return value;
}
double rounded(double value) { return std::round(value * 100) / 100; }
std::pair<double, double> port(const StraightVisualNode& node,
                             const StraightVisualNode& target) {
  double dx = target.x - node.x, dy = target.y - node.y;
  if (dx == 0 && dy == 0) dx = 1;
  const double scale = std::min(dx == 0 ? std::numeric_limits<double>::infinity() : node.width / (2 * std::abs(dx)),
                               dy == 0 ? std::numeric_limits<double>::infinity() : node.height / (2 * std::abs(dy)));
  return {node.x + dx * scale, node.y + dy * scale};
}
struct Lane { double fraction = 0; bool parallel = false; };
std::pair<double, double> slotted(std::pair<double, double> point,
  const StraightVisualNode& node, double fraction, double dx, double dy) {
  const auto box = rect(node);
  const double a = std::abs(point.first - box.left), b = std::abs(point.first - box.right);
  const double c = std::abs(point.second - box.top), d = std::abs(point.second - box.bottom);
  const double nearest = std::min({a, b, c, d});
  if (a == nearest || b == nearest) {
    return {a == nearest ? box.left : box.right,
            node.y + fraction * node.height * (dy < 0 ? -1 : 1)};
  }
  return {node.x + fraction * node.width * (dx < 0 ? -1 : 1),
          c == nearest ? box.top : box.bottom};
}
StraightVisualRoute route(const StraightVisualNode& source,
                          const StraightVisualNode& target, Lane lane) {
  auto start = port(source, target), end = port(target, source);
  const double length = std::hypot(end.first - start.first, end.second - start.second);
  if (lane.parallel && length > 0) {
    const double dx = -(target.y - source.y), dy = target.x - source.x;
    start = slotted(start, source, lane.fraction, dx, dy);
    end = slotted(end, target, lane.fraction, dx, dy);
  }
  return {rounded(start.first), rounded(start.second), rounded(end.first), rounded(end.second)};
}
bool invalid(const StraightVisualRoute& line, const StraightVisualNode& source,
             const StraightVisualNode& target) {
  const double dx = line.targetX - line.sourceX, dy = line.targetY - line.sourceY;
  const double length = std::hypot(dx, dy);
  if (length < 0.01) return true;
  const double step = std::min(1.0, length * .25) / length;
  const auto inside = [](double x, double y, const StraightVisualNode& node) {
    const auto box = rect(node);
    return x > box.left + .02 && x < box.right - .02
        && y > box.top + .02 && y < box.bottom - .02;
  };
  return inside(line.sourceX + step * dx, line.sourceY + step * dy, source)
      || inside(line.targetX - step * dx, line.targetY - step * dy, target);
}
double orientation(double ax, double ay, double bx, double by, double x, double y) {
  return (bx - ax) * (y - ay) - (by - ay) * (x - ax);
}
bool cross(const StraightVisualRoute& a, const StraightVisualRoute& b) {
  return orientation(a.sourceX, a.sourceY, a.targetX, a.targetY, b.sourceX, b.sourceY)
      * orientation(a.sourceX, a.sourceY, a.targetX, a.targetY, b.targetX, b.targetY) < -epsilon
    && orientation(b.sourceX, b.sourceY, b.targetX, b.targetY, a.sourceX, a.sourceY)
      * orientation(b.sourceX, b.sourceY, b.targetX, b.targetY, a.targetX, a.targetY) < -epsilon;
}
bool overlaps(const StraightVisualRoute& a, const StraightVisualRoute& b) {
  if ((a.sourceX == b.sourceX && a.sourceY == b.sourceY && a.targetX == b.targetX && a.targetY == b.targetY)
      || (a.sourceX == b.targetX && a.sourceY == b.targetY && a.targetX == b.sourceX && a.targetY == b.sourceY)) return true;
  const auto first = bounds(a), second = bounds(b);
  if (first.right < second.left - 1e-7 || second.right < first.left - 1e-7
      || first.bottom < second.top - 1e-7 || second.bottom < first.top - 1e-7) return false;
  // Use the independent canonical audit's angular and parameter tolerances.
  // A shared portion of two independent routes is invalid even when their
  // complete endpoint pairs differ and strict proper crossings count zero.
  const double rx = a.targetX - a.sourceX, ry = a.targetY - a.sourceY;
  const double sx = b.targetX - b.sourceX, sy = b.targetY - b.sourceY;
  const double qx = b.sourceX - a.sourceX, qy = b.sourceY - a.sourceY;
  const double rLength = std::hypot(rx, ry), sLength = std::hypot(sx, sy);
  if (std::abs(rx * sy - ry * sx) > 1e-7 * std::max(1.0, rLength * sLength)) return false;
  const double displacement = std::hypot(qx, qy);
  const auto projectedOverlap = [&](double dx, double dy, double ux, double uy,
                                    double offsetX, double offsetY, double length) {
    if (std::abs(offsetX * dy - offsetY * dx) > 1e-7 * std::max(1.0, length * displacement)) return false;
    const double squared = dx * dx + dy * dy;
    if (squared == 0) return false;
    const double t0 = (offsetX * dx + offsetY * dy) / squared;
    const double t1 = t0 + (ux * dx + uy * dy) / squared;
    return std::min(1.0, std::max(t0, t1)) > std::max(0.0, std::min(t0, t1)) + 1e-9;
  };
  // Near the tolerance boundary the canonical predicate depends on which
  // line defines the projection. Conservatively reject either orientation;
  // full and changed-edge evaluations must use exactly one symmetric score.
  return projectedOverlap(rx, ry, sx, sy, qx, qy, rLength)
      || projectedOverlap(sx, sy, rx, ry, -qx, -qy, sLength);
}
bool hits(const StraightVisualRoute& line, const Rect& box) {
  double enter = 0, exit = 1;
  const double origins[] = {line.sourceX, line.sourceY};
  const double deltas[] = {line.targetX - line.sourceX, line.targetY - line.sourceY};
  const double low[] = {box.left, box.top}, high[] = {box.right, box.bottom};
  for (int axis = 0; axis < 2; ++axis) {
    if (std::abs(deltas[axis]) < epsilon) {
      if (origins[axis] <= low[axis] || origins[axis] >= high[axis]) return false;
      continue;
    }
    const double a = (low[axis] - origins[axis]) / deltas[axis];
    const double b = (high[axis] - origins[axis]) / deltas[axis];
    enter = std::max(enter, std::min(a, b)); exit = std::min(exit, std::max(a, b));
    if (exit - enter <= epsilon) return false;
  }
  return exit > epsilon && enter < 1 - epsilon;
}
void validate(const std::vector<StraightVisualNode>& nodes,
              const std::vector<StraightVisualEdge>& edges) {
  for (const auto& node : nodes) {
    if (!std::isfinite(node.x) || !std::isfinite(node.y)
        || !std::isfinite(node.width) || !std::isfinite(node.height)
        || node.width <= 0 || node.height <= 0) throw std::invalid_argument("invalid rendered card");
  }
  for (const auto& edge : edges) {
    if (edge.source >= nodes.size() || edge.target >= nodes.size() || edge.source == edge.target)
      throw std::invalid_argument("invalid nonself relationship");
  }
}
std::vector<Lane> lanes(const std::vector<StraightVisualEdge>& edges) {
  std::map<std::pair<std::size_t, std::size_t>, std::vector<std::size_t>> pairs;
  for (std::size_t i = 0; i < edges.size(); ++i) pairs[std::minmax(edges[i].source, edges[i].target)].push_back(i);
  std::vector<Lane> result(edges.size());
  for (const auto& item : pairs) {
    for (std::size_t i = 0; i < item.second.size(); ++i) {
      const auto index = item.second[i];
      const auto& edge = edges[index];
      result[index] = {(static_cast<double>(i) - (item.second.size() - 1) * .5)
                    / (item.second.size() + 1) * (edge.source < edge.target ? 1 : -1),
                      item.second.size() > 1};
    }
  }
  return result;
}
StraightVisualScore addDelta(StraightVisualScore total, const StraightVisualScore& after,
                              const StraightVisualScore& before) {
  total.edgeCrossings += after.edgeCrossings - before.edgeCrossings;
  total.edgeNodeIntersections += after.edgeNodeIntersections - before.edgeNodeIntersections;
  total.nodeOverlaps += after.nodeOverlaps - before.nodeOverlaps;
  total.invalidRoutes += after.invalidRoutes - before.invalidRoutes;
  return total;
}
}  // namespace

StraightVisualScore measureStraightVisualFull(const std::vector<StraightVisualNode>& nodes,
                                             const std::vector<StraightVisualEdge>& edges) {
  validate(nodes, edges);
  const auto lane = lanes(edges);
  std::vector<StraightVisualRoute> routes;
  for (std::size_t i = 0; i < edges.size(); ++i) routes.push_back(route(nodes[edges[i].source], nodes[edges[i].target], lane[i]));
  StraightVisualScore score;
  for (std::size_t i = 0; i < routes.size(); ++i) {
    score.invalidRoutes += invalid(routes[i], nodes[edges[i].source], nodes[edges[i].target]);
    for (std::size_t j = i + 1; j < routes.size(); ++j) {
      score.edgeCrossings += cross(routes[i], routes[j]);
      score.invalidRoutes += overlaps(routes[i], routes[j]);
    }
    for (std::size_t j = 0; j < nodes.size(); ++j) {
      if (j != edges[i].source && j != edges[i].target) score.edgeNodeIntersections += hits(routes[i], rect(nodes[j], 10));
    }
  }
  for (std::size_t i = 0; i < nodes.size(); ++i)
    for (std::size_t j = i + 1; j < nodes.size(); ++j) score.nodeOverlaps += rectsOverlap(rect(nodes[i]), rect(nodes[j]));
  return score;
}

struct StraightVisualState::Storage {
  std::vector<StraightVisualNode> nodes;
  std::vector<StraightVisualEdge> edges;
  std::vector<Lane> lane;
  std::vector<StraightVisualRoute> routes;
  std::vector<std::vector<std::size_t>> incident;
  RouteBoundsIndex routeIndex, nodeIndex;
  StraightVisualScore current;
  mutable std::size_t preparedNode = std::numeric_limits<std::size_t>::max();
  mutable StraightVisualScore preparedLocal;
  Storage(std::vector<StraightVisualNode> n, std::vector<StraightVisualEdge> e)
    : nodes(std::move(n)), edges(std::move(e)), lane(lanes(edges)), incident(nodes.size()),
      routeIndex(edges.size()), nodeIndex(nodes.size()) {
    validate(nodes, edges);
    current = measureStraightVisualFull(nodes, edges);
    for (std::size_t i = 0; i < nodes.size(); ++i) nodeIndex.update(i, rect(nodes[i]));
    for (std::size_t i = 0; i < edges.size(); ++i) {
      incident[edges[i].source].push_back(i); incident[edges[i].target].push_back(i);
      routes.push_back(route(nodes[edges[i].source], nodes[edges[i].target], lane[i]));
      routeIndex.update(i, bounds(routes[i]));
    }
  }
  bool affected(std::size_t edge, std::size_t node,
                std::size_t second = std::numeric_limits<std::size_t>::max()) const {
    return edges[edge].source == node || edges[edge].target == node
        || edges[edge].source == second || edges[edge].target == second;
  }
  StraightVisualScore local(std::size_t node, const StraightVisualNode& position,
    std::size_t second = std::numeric_limits<std::size_t>::max(),
    const StraightVisualNode* secondPosition = nullptr) const {
    StraightVisualScore result;
    const auto moved = [&](std::size_t id) { return id == node || id == second; };
    const auto get = [&](std::size_t id) -> const StraightVisualNode& {
      return id == node ? position : (id == second ? *secondPosition : nodes[id]);
    };
    auto affectedEdges = incident[node];
    if (secondPosition) for (const auto edge : incident[second]) {
      if (!affected(edge, node)) affectedEdges.push_back(edge);
    }
    std::vector<StraightVisualRoute> changed;
    changed.reserve(affectedEdges.size());
    for (const auto edge : affectedEdges) {
      const auto& pair = edges[edge];
      const auto& source = get(pair.source);
      const auto& target = get(pair.target);
      const auto candidate = route(source, target, lane[edge]);
      result.invalidRoutes += invalid(candidate, source, target);
      for (const auto other : routeIndex.query(expand(bounds(candidate), .000001))) {
        if (!affected(other, node, second)) {
          result.edgeCrossings += cross(candidate, routes[other]);
          result.invalidRoutes += overlaps(candidate, routes[other]);
        }
      }
      for (const auto& previous : changed) {
        result.edgeCrossings += cross(candidate, previous);
        result.invalidRoutes += overlaps(candidate, previous);
      }
      for (const auto other : nodeIndex.query(expand(bounds(candidate), 10)))
        if (!moved(other) && other != pair.source && other != pair.target) result.edgeNodeIntersections += hits(candidate, rect(nodes[other], 10));
      if (node != pair.source && node != pair.target) result.edgeNodeIntersections += hits(candidate, rect(position, 10));
      if (secondPosition && second != pair.source && second != pair.target) result.edgeNodeIntersections += hits(candidate, rect(*secondPosition, 10));
      changed.push_back(candidate);
    }
    // Moving a card can obstruct lines whose endpoint geometry never changes.
    const auto cardCost = [&](const StraightVisualNode& card) {
      for (const auto other : routeIndex.query(rect(card, 10)))
        if (!affected(other, node, second)) result.edgeNodeIntersections += hits(routes[other], rect(card, 10));
      for (const auto other : nodeIndex.query(rect(card)))
        if (!moved(other)) result.nodeOverlaps += rectsOverlap(rect(card), rect(nodes[other]));
    };
    cardCost(position);
    if (secondPosition) {
      cardCost(*secondPosition);
      result.nodeOverlaps += rectsOverlap(rect(position), rect(*secondPosition));
    }
    return result;
  }
  StraightVisualScore groupLocal(const std::vector<StraightVisualMove>& moves) const {
    std::vector<unsigned char> moved(nodes.size(), 0), affectedEdges(edges.size(), 0);
    auto positions = nodes;
    for (const auto& move : moves) {
      if (move.node >= nodes.size() || moved[move.node] || !std::isfinite(move.x) || !std::isfinite(move.y))
        throw std::invalid_argument("invalid or duplicate group move");
      moved[move.node] = 1;
      positions[move.node].x = move.x; positions[move.node].y = move.y;
      for (const auto edge : incident[move.node]) affectedEdges[edge] = 1;
    }
    StraightVisualScore result;
    std::vector<StraightVisualRoute> changed;
    for (std::size_t edge = 0; edge < edges.size(); ++edge) {
      if (!affectedEdges[edge]) continue;
      const auto& pair = edges[edge];
      const auto candidate = route(positions[pair.source], positions[pair.target], lane[edge]);
      result.invalidRoutes += invalid(candidate, positions[pair.source], positions[pair.target]);
      for (const auto other : routeIndex.query(expand(bounds(candidate), .000001))) {
        if (!affectedEdges[other]) {
          result.edgeCrossings += cross(candidate, routes[other]);
          result.invalidRoutes += overlaps(candidate, routes[other]);
        }
      }
      for (const auto& previous : changed) {
        result.edgeCrossings += cross(candidate, previous);
        result.invalidRoutes += overlaps(candidate, previous);
      }
      for (const auto other : nodeIndex.query(expand(bounds(candidate), 10))) {
        if (!moved[other] && other != pair.source && other != pair.target)
          result.edgeNodeIntersections += hits(candidate, rect(nodes[other], 10));
      }
      for (const auto& move : moves) {
        if (move.node != pair.source && move.node != pair.target)
          result.edgeNodeIntersections += hits(candidate, rect(positions[move.node], 10));
      }
      changed.push_back(candidate);
    }
    for (std::size_t i = 0; i < moves.size(); ++i) {
      const auto& position = positions[moves[i].node];
      for (const auto other : routeIndex.query(rect(position, 10))) {
        if (!affectedEdges[other]) result.edgeNodeIntersections += hits(routes[other], rect(position, 10));
      }
      for (const auto other : nodeIndex.query(rect(position))) {
        if (!moved[other]) result.nodeOverlaps += rectsOverlap(rect(position), rect(nodes[other]));
      }
      for (std::size_t j = 0; j < i; ++j)
        result.nodeOverlaps += rectsOverlap(rect(position), rect(positions[moves[j].node]));
    }
    return result;
  }
};

StraightVisualState::StraightVisualState(std::vector<StraightVisualNode> nodes,
  std::vector<StraightVisualEdge> edges) : storage_(std::make_unique<Storage>(std::move(nodes), std::move(edges))) {}
StraightVisualState::~StraightVisualState() = default;
const std::vector<StraightVisualNode>& StraightVisualState::nodes() const { return storage_->nodes; }
const std::vector<StraightVisualRoute>& StraightVisualState::routes() const { return storage_->routes; }
StraightVisualScore StraightVisualState::score() const { return storage_->current; }
StraightVisualScore StraightVisualState::evaluateMove(std::size_t node, double x, double y) const {
  if (!std::isfinite(x) || !std::isfinite(y)) throw std::invalid_argument("nonfinite move");
  auto trial = storage_->nodes.at(node);
  trial.x = x; trial.y = y;
  if (storage_->preparedNode != node) {
    storage_->preparedLocal = storage_->local(node, storage_->nodes[node]);
    storage_->preparedNode = node;
  }
  return addDelta(storage_->current, storage_->local(node, trial), storage_->preparedLocal);
}
void StraightVisualState::move(std::size_t node, double x, double y) {
  const auto score = evaluateMove(node, x, y);
  auto& s = *storage_;
  s.nodes[node].x = x; s.nodes[node].y = y;
  s.nodeIndex.update(node, rect(s.nodes[node]));
  for (const auto edge : s.incident[node]) {
    s.routes[edge] = route(s.nodes[s.edges[edge].source], s.nodes[s.edges[edge].target], s.lane[edge]);
    s.routeIndex.update(edge, bounds(s.routes[edge]));
  }
  s.current = score;
  s.preparedNode = std::numeric_limits<std::size_t>::max();
}
StraightVisualScore StraightVisualState::evaluateSwap(std::size_t first, std::size_t second) const {
  auto a = storage_->nodes.at(first), b = storage_->nodes.at(second);
  if (first == second) return storage_->current;
  std::swap(a.x, b.x); std::swap(a.y, b.y);
  return addDelta(storage_->current, storage_->local(first, a, second, &b),
    storage_->local(first, storage_->nodes[first], second, &storage_->nodes[second]));
}
void StraightVisualState::swap(std::size_t first, std::size_t second) {
  const auto score = evaluateSwap(first, second);
  auto& s = *storage_;
  std::swap(s.nodes[first].x, s.nodes[second].x); std::swap(s.nodes[first].y, s.nodes[second].y);
  for (const auto node : {first, second}) {
    s.nodeIndex.update(node, rect(s.nodes[node]));
    for (const auto edge : s.incident[node]) {
      s.routes[edge] = route(s.nodes[s.edges[edge].source], s.nodes[s.edges[edge].target], s.lane[edge]);
      s.routeIndex.update(edge, bounds(s.routes[edge]));
    }
  }
  s.current = score;
  s.preparedNode = std::numeric_limits<std::size_t>::max();
}
StraightVisualScore StraightVisualState::evaluateMoves(const std::vector<StraightVisualMove>& moves) const {
  if (moves.empty()) return storage_->current;
  // Validate the complete transaction before measuring its old position or
  // mutating anything; duplicate IDs must never hide an incomplete move.
  const auto after = storage_->groupLocal(moves);
  auto original = moves;
  for (auto& move : original) { move.x = storage_->nodes[move.node].x; move.y = storage_->nodes[move.node].y; }
  return addDelta(storage_->current, after, storage_->groupLocal(original));
}
void StraightVisualState::moveMany(const std::vector<StraightVisualMove>& moves) {
  const auto score = evaluateMoves(moves);
  auto& s = *storage_;
  std::vector<unsigned char> affected(s.edges.size(), 0);
  for (const auto& move : moves) {
    s.nodes[move.node].x = move.x; s.nodes[move.node].y = move.y;
    s.nodeIndex.update(move.node, rect(s.nodes[move.node]));
    for (const auto edge : s.incident[move.node]) affected[edge] = 1;
  }
  for (std::size_t edge = 0; edge < s.edges.size(); ++edge) {
    if (!affected[edge]) continue;
    s.routes[edge] = route(s.nodes[s.edges[edge].source], s.nodes[s.edges[edge].target], s.lane[edge]);
    s.routeIndex.update(edge, bounds(s.routes[edge]));
  }
  s.current = score;
  s.preparedNode = std::numeric_limits<std::size_t>::max();
}
std::vector<std::int64_t> StraightVisualState::pressure() const {
  const auto& s = *storage_;
  std::vector<std::int64_t> result(s.nodes.size(), 0);
  const auto charge = [&](std::size_t edge, std::int64_t value) {
    result[s.edges[edge].source] += value; result[s.edges[edge].target] += value;
  };
  for (std::size_t edge = 0; edge < s.edges.size(); ++edge) {
    charge(edge, invalid(s.routes[edge], s.nodes[s.edges[edge].source], s.nodes[s.edges[edge].target]) * 1000);
    for (const auto other : s.routeIndex.query(expand(bounds(s.routes[edge]), .000001))) {
      if (other <= edge) continue;
      if (cross(s.routes[edge], s.routes[other])) { charge(edge, 1); charge(other, 1); }
      if (overlaps(s.routes[edge], s.routes[other])) { charge(edge, 1000); charge(other, 1000); }
    }
    for (const auto node : s.nodeIndex.query(expand(bounds(s.routes[edge]), 10))) {
      if (node != s.edges[edge].source && node != s.edges[edge].target
          && hits(s.routes[edge], rect(s.nodes[node], 10))) { charge(edge, 1); ++result[node]; }
    }
  }
  for (std::size_t node = 0; node < s.nodes.size(); ++node) {
    for (const auto other : s.nodeIndex.query(rect(s.nodes[node]))) {
      if (other > node) { result[node] += 100; result[other] += 100; }
    }
  }
  return result;
}
}  // namespace djerd
