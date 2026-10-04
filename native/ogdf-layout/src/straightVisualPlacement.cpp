#include "straightVisualOptimization.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <utility>

namespace djerd {
namespace {
struct Point { double x, y; };
std::int64_t cost(const StraightVisualScore& score) {
  return score.visual() + score.nodeOverlaps * 100 + score.invalidRoutes * 10000;
}
double orientation(double ax, double ay, double bx, double by, double x, double y) {
  return (bx - ax) * (y - ay) - (by - ay) * (x - ax);
}
bool crosses(const StraightVisualRoute& a, const StraightVisualRoute& b) {
  return orientation(a.sourceX, a.sourceY, a.targetX, a.targetY, b.sourceX, b.sourceY)
      * orientation(a.sourceX, a.sourceY, a.targetX, a.targetY, b.targetX, b.targetY) < -1e-9
    && orientation(b.sourceX, b.sourceY, b.targetX, b.targetY, a.sourceX, a.sourceY)
      * orientation(b.sourceX, b.sourceY, b.targetX, b.targetY, a.targetX, a.targetY) < -1e-9;
}

}  // namespace
StraightVisualPlacementResult optimizeStraightVisualPlacement(
  const std::vector<StraightVisualNode>& nodes,
  const std::vector<StraightVisualEdge>& edges,
  const std::vector<std::string>& ids,
  const std::vector<std::vector<std::size_t>>& groups,
  const StraightVisualPlacementOptions& options) {
    if (!std::isfinite(options.budgetMs) || options.budgetMs < 0
        || !std::isfinite(options.groupBudgetMs) || ids.size() != nodes.size())
      throw std::invalid_argument("invalid placement options or identity coverage");
    std::vector<unsigned char> grouped(nodes.size(), 0);
    for (const auto& group : groups) for (const auto node : group) {
      if (node >= nodes.size() || grouped[node]) throw std::invalid_argument("ambiguous placement group");
      grouped[node] = 1;
    }
    const double budgetMs = options.budgetMs;
    const int rounds = std::max(0, options.maxRounds);
    const int angular = std::clamp(options.angularSamples, 4, 32);
    const int limit = options.nodeLimit;
    const int swapLimit = std::max(0, options.swapLimit);
    std::mt19937_64 random(options.seed);
    std::uniform_real_distribution<double> unit(0, 1);
    std::normal_distribution<double> normal(0, 1);
    std::vector<std::vector<std::size_t>> adjacent(nodes.size()), incident(nodes.size());
    for (std::size_t i = 0; i < edges.size(); ++i) {
      const auto& edge = edges[i];
      if (edge.source >= nodes.size() || edge.target >= nodes.size()) throw std::invalid_argument("unknown relationship endpoint");
      adjacent[edge.source].push_back(edge.target); adjacent[edge.target].push_back(edge.source);
      incident[edge.source].push_back(i); incident[edge.target].push_back(i);
    }
    for (auto& neighbors : adjacent) {
      std::sort(neighbors.begin(), neighbors.end()); neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    }
    for (auto& neighbors : adjacent) std::sort(neighbors.begin(), neighbors.end(), [&](auto a, auto b) {
      return adjacent[a].size() != adjacent[b].size() ? adjacent[a].size() > adjacent[b].size() : ids[a] < ids[b];
    });
    const auto started = std::chrono::steady_clock::now();
    StraightVisualState state(nodes, edges);
    const auto before = state.score();
    const auto elapsed = [&]() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(); };
    double left = 0, right = 0, top = 0, bottom = 0;
    if (!nodes.empty()) {
      left = right = nodes[0].x; top = bottom = nodes[0].y;
      for (const auto& node : nodes) { left = std::min(left, node.x); right = std::max(right, node.x); top = std::min(top, node.y); bottom = std::max(bottom, node.y); }
    }
    const double spanX = std::max(100.0, right - left), spanY = std::max(100.0, bottom - top);
    const double diagonal = std::hypot(spanX, spanY);
    std::size_t evaluations = 0, moves = 0;
    int completed = 0;
    for (int round = 0; round < rounds && elapsed() < budgetMs; ++round) {
      const auto pressure = state.pressure();
      std::vector<std::size_t> order(nodes.size()); std::iota(order.begin(), order.end(), 0);
      std::sort(order.begin(), order.end(), [&](auto a, auto b) { return pressure[a] != pressure[b] ? pressure[a] > pressure[b] : ids[a] < ids[b]; });
      if (limit > 0 && order.size() > static_cast<std::size_t>(limit)) order.resize(limit);
      std::size_t accepted = 0;
      std::size_t groupMoves = 0;
      const double groupEnd = std::min(budgetMs, elapsed() + std::max(0.0, options.groupBudgetMs));
      if (!groups.empty()) {
        std::vector<std::size_t> groupOrder(groups.size()); std::iota(groupOrder.begin(), groupOrder.end(), 0);
        std::vector<std::int64_t> groupPressure(groups.size(), 0);
        for (std::size_t i = 0; i < groups.size(); ++i)
          for (const auto node : groups[i]) groupPressure[i] += pressure[node];
        std::sort(groupOrder.begin(), groupOrder.end(), [&](auto a, auto b) { return groupPressure[a] != groupPressure[b] ? groupPressure[a] > groupPressure[b] : a < b; });
        const auto centerOf = [&](const std::vector<std::size_t>& group) {
          Point center{0, 0};
          for (const auto node : group) { center.x += state.nodes()[node].x; center.y += state.nodes()[node].y; }
          center.x /= group.size(); center.y /= group.size(); return center;
        };
        for (std::size_t i = 0; i < std::min<std::size_t>(32, groupOrder.size()) && elapsed() < groupEnd; ++i) {
          const auto group = groupOrder[i];
          if (groupPressure[group] == 0) break;
          const auto center = centerOf(groups[group]);
          auto bestScore = state.score(); std::vector<StraightVisualMove> best;
          std::vector<unsigned char> member(nodes.size(), 0);
          for (const auto node : groups[group]) member[node] = 1;
          Point external{0, 0}; double mass = 0;
          for (const auto node : groups[group]) for (const auto neighbor : adjacent[node]) if (!member[neighbor]) {
            external.x += state.nodes()[neighbor].x; external.y += state.nodes()[neighbor].y; ++mass;
          }
          if (mass) { external.x /= mass; external.y /= mass; } else external = center;
          const auto consider = [&](std::vector<StraightVisualMove> trial) {
            if (elapsed() >= groupEnd) return;
            const auto score = state.evaluateMoves(trial); ++evaluations;
            if (cost(score) < cost(bestScore)) { bestScore = score; best = std::move(trial); }
          };
          for (const auto target : {center, external}) {
            for (int rotation = 0; rotation < 8 && elapsed() < groupEnd; ++rotation) {
              for (const double reflect : {1.0, -1.0}) {
                const double angle = rotation * .7853981633974483;
                std::vector<StraightVisualMove> trial;
                for (const auto node : groups[group]) {
                  const auto& p = state.nodes()[node]; const double x = (p.x - center.x) * reflect, y = p.y - center.y;
                  trial.push_back({node, target.x + x * std::cos(angle) - y * std::sin(angle), target.y + x * std::sin(angle) + y * std::cos(angle)});
                }
                consider(std::move(trial));
              }
            }
          }
          for (std::size_t j = i + 1; j < std::min<std::size_t>(20, groupOrder.size()) && elapsed() < groupEnd; ++j) {
            const auto partner = groupOrder[j]; const auto target = centerOf(groups[partner]);
            std::vector<StraightVisualMove> trial;
            for (const auto node : groups[group]) trial.push_back({node, state.nodes()[node].x + target.x - center.x, state.nodes()[node].y + target.y - center.y});
            for (const auto node : groups[partner]) trial.push_back({node, state.nodes()[node].x + center.x - target.x, state.nodes()[node].y + center.y - target.y});
            consider(std::move(trial));
          }
          if (!best.empty()) { state.moveMany(best); ++moves; ++groupMoves; }
        }
      }
      for (const auto node : order) {
        if (pressure[node] == 0 || elapsed() >= budgetMs) break;
        const auto& geometry = state.nodes();
        const auto original = geometry[node];
        Point center{original.x, original.y}, weighted = center;
        if (!adjacent[node].empty()) {
          center = weighted = {0, 0}; double mass = 0;
          for (const auto neighbor : adjacent[node]) {
            center.x += geometry[neighbor].x; center.y += geometry[neighbor].y;
            const double w = std::sqrt(static_cast<double>(adjacent[neighbor].size()));
            weighted.x += geometry[neighbor].x * w; weighted.y += geometry[neighbor].y * w; mass += w;
          }
          center.x /= adjacent[node].size(); center.y /= adjacent[node].size();
          weighted.x /= mass; weighted.y /= mass;
        }
        std::vector<Point> candidates{center, weighted}; candidates.reserve(800);
        const double phase = unit(random) * 6.283185307179586;
        const auto polar = [&](Point base, double radius, int samples) {
          for (int i = 0; i < samples; ++i) {
            const double angle = phase + i * 6.283185307179586 / samples;
            candidates.push_back({base.x + radius * std::cos(angle), base.y + radius * std::sin(angle)});
          }
        };
        const double clearance = std::hypot(original.width, original.height) + 60;
        for (double radius : {clearance * .55, clearance, clearance * 2, diagonal * .015, diagonal * .06, diagonal * .18}) {
          polar(center, radius, angular); polar(weighted, radius, angular);
        }
        for (double radius : {clearance * .3, clearance, clearance * 3}) polar({original.x, original.y}, radius, angular);
        for (std::size_t i = 0; i < std::min<std::size_t>(adjacent[node].size(), 8); ++i) {
          const auto& neighbor = geometry[adjacent[node][i]];
          const double gap = .5 * std::hypot(original.width + neighbor.width, original.height + neighbor.height) + 70;
          polar({neighbor.x, neighbor.y}, gap, angular);
          polar({neighbor.x, neighbor.y}, gap * 2.5, angular);
        }
        if (adjacent[node].size() >= 2) {
          const auto& a = geometry[adjacent[node][0]]; const auto& b = geometry[adjacent[node][1]];
          const double dx = b.x - a.x, dy = b.y - a.y, length = std::hypot(dx, dy);
          if (length > 1) for (double fraction : {.12, .3, .5, .7, .88}) {
            for (double offset : {-clearance * 3, -clearance, -clearance * .55, clearance * .55, clearance, clearance * 3})
              candidates.push_back({a.x + fraction * dx - dy * offset / length, a.y + fraction * dy + dx * offset / length});
          }
        }
        for (int i = 0; i < 16; ++i) {
          const double scale = i % 2 ? .025 : .15;
          candidates.push_back({weighted.x + normal(random) * spanX * scale, weighted.y + normal(random) * spanY * scale});
          candidates.push_back({left + unit(random) * spanX, top + unit(random) * spanY});
        }
        // A crossing changes when the moved endpoint passes a ray from its
        // fixed neighbour through either end of the other line. Try both
        // sides of those actual constraints, with room for the real card.
        int blockers = 0;
        const auto& routes = state.routes();
        for (std::size_t k = 0; k < incident[node].size() && blockers < 16; ++k) {
          const auto edge = incident[node][(k + round) % incident[node].size()];
          const auto neighbor = edges[edge].source == node ? edges[edge].target : edges[edge].source;
          const auto& pivot = geometry[neighbor];
          for (std::size_t j = 0; j < edges.size() && blockers < 16; ++j) {
            const auto other = (j + round * 71) % edges.size();
            if (other == edge || !crosses(routes[edge], routes[other])) continue;
            for (const auto endpoint : {Point{routes[other].sourceX, routes[other].sourceY}, Point{routes[other].targetX, routes[other].targetY}}) {
              const double dx = endpoint.x - pivot.x, dy = endpoint.y - pivot.y;
              const double length = std::hypot(dx, dy);
              if (length < 1) continue;
              const double t = ((original.x - pivot.x) * dx + (original.y - pivot.y) * dy) / (length * length);
              for (const double offset : {-clearance * .6, clearance * .6}) candidates.push_back({
                pivot.x + t * dx - offset * dy / length, pivot.y + t * dy + offset * dx / length});
            }
            ++blockers;
          }
        }
        Point best{original.x, original.y}; auto bestScore = state.score();
        double bestLength = std::numeric_limits<double>::infinity();
        for (const auto& candidate : candidates) {
          if (elapsed() >= budgetMs) break;
          if (candidate.x < left - spanX * .2 || candidate.x > right + spanX * .2
              || candidate.y < top - spanY * .2 || candidate.y > bottom + spanY * .2) continue;
          const auto score = state.evaluateMove(node, candidate.x, candidate.y); ++evaluations;
          if (cost(score) > cost(bestScore)) continue;
          double length = 0;
          for (const auto neighbor : adjacent[node]) length += std::hypot(candidate.x - geometry[neighbor].x, candidate.y - geometry[neighbor].y);
          if (cost(score) < cost(bestScore) || (cost(score) < cost(state.score()) && length < bestLength)) {
            best = candidate; bestScore = score; bestLength = length;
          }
        }
        if (cost(bestScore) < cost(state.score())) { state.move(node, best.x, best.y); ++accepted; ++moves; }
      }
      std::size_t swaps = 0;
      if (swapLimit > 0 && elapsed() < budgetMs) {
        const auto updated = state.pressure();
        std::sort(order.begin(), order.end(), [&](auto a, auto b) { return updated[a] != updated[b] ? updated[a] > updated[b] : ids[a] < ids[b]; });
        const auto count = std::min<std::size_t>(swapLimit, order.size());
        for (std::size_t i = 0; i < count && elapsed() < budgetMs; ++i) {
          const auto node = order[i];
          if (updated[node] == 0) break;
          auto bestScore = state.score(); auto best = node;
          std::vector<std::pair<double, std::size_t>> nearest;
          for (std::size_t other = 0; other < nodes.size(); ++other) {
            if (other == node || adjacent[other].empty()) continue;
            const auto& a = state.nodes()[node]; const auto& b = state.nodes()[other];
            nearest.push_back({std::hypot(a.x - b.x, a.y - b.y), other});
          }
          const auto nearCount = std::min<std::size_t>(24, nearest.size());
          std::partial_sort(nearest.begin(), nearest.begin() + nearCount, nearest.end());
          std::vector<std::size_t> partners;
          for (std::size_t j = 0; j < nearCount; ++j) partners.push_back(nearest[j].second);
          for (std::size_t j = i + 1; j < count; ++j) partners.push_back(order[j]);
          for (const auto other : partners) {
            if (elapsed() >= budgetMs) break;
            const auto score = state.evaluateSwap(node, other); ++evaluations;
            if (cost(score) < cost(bestScore)) { bestScore = score; best = other; }
          }
          if (best != node) { state.swap(node, best); ++swaps; ++moves; }
        }
      }
      ++completed;
      std::cerr << "round=" << completed << " groups=" << groupMoves << " accepted=" << accepted << " swaps=" << swaps << " visual=" << state.score().visual()
        << " crossings=" << state.score().edgeCrossings << " edgeNode=" << state.score().edgeNodeIntersections
        << " overlaps=" << state.score().nodeOverlaps << " invalid=" << state.score().invalidRoutes
        << " evaluations=" << evaluations << " elapsedMs=" << elapsed() << '\n';
      if (accepted == 0 && swaps == 0 && groupMoves == 0) break;
    }
    const auto audited = measureStraightVisualFull(state.nodes(), edges);
    if (audited.edgeCrossings != state.score().edgeCrossings || audited.edgeNodeIntersections != state.score().edgeNodeIntersections
        || audited.nodeOverlaps != state.score().nodeOverlaps || audited.invalidRoutes != state.score().invalidRoutes) throw std::runtime_error("final scene audit mismatch");

    StraightVisualPlacementResult result;
    result.nodes = state.nodes(); result.routes = state.routes();
    result.before = before; result.after = audited;
    result.moves = moves; result.evaluations = evaluations; result.rounds = completed;
    result.elapsedMs = elapsed(); result.budgetHit = result.elapsedMs >= budgetMs;
    return result;
}
}  // namespace djerd
