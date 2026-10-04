#include "straightVisualOptimization.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

using namespace djerd;

void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

bool same(const StraightVisualScore& a, const StraightVisualScore& b) {
  return a.edgeCrossings == b.edgeCrossings
    && a.edgeNodeIntersections == b.edgeNodeIntersections
    && a.nodeOverlaps == b.nodeOverlaps
    && a.invalidRoutes == b.invalidRoutes;
}

int main() {
  try {
    // Moving an unconnected card must account for the line it obstructs.
    StraightVisualState obstacle({{40, 40, 0, 0}, {40, 40, 400, 0},
      {40, 40, 200, 100}}, {{0, 1}});
    require(obstacle.score().visual() == 0, "clear initial scene");
    const auto blocked = obstacle.evaluateMove(2, 200, 0);
    require(blocked.edgeNodeIntersections == 1, "unrelated line hitting moved card omitted");
    require(obstacle.score().visual() == 0, "evaluating a move mutated scene");
    obstacle.move(2, 200, 0);
    require(same(obstacle.score(), blocked), "committed move differs from evaluated move");
    obstacle.move(2, 200, 100);
    require(obstacle.score().visual() == 0, "reverting obstruction leaves stale bounds");

    // Crossing removal cannot count each affected pair twice.
    StraightVisualState crossing({{20, 20, -200, -200}, {20, 20, 200, 200},
      {20, 20, -200, 200}, {20, 20, 200, -200}}, {{0, 1}, {2, 3}});
    require(crossing.score().edgeCrossings == 1, "initial crossing must be counted");
    require(crossing.evaluateMove(0, -200, 250).edgeCrossings == 0, "crossing removal not evaluated");

    StraightVisualState touch({{20, 20, -10, 0}, {20, 20, 10, 0}}, {});
    require(touch.score().nodeOverlaps == 0, "touching card boundary treated as overlap");
    require(touch.evaluateMove(0, -9.99, 0).nodeOverlaps == 1, "small actual overlap omitted");
    bool rejected = false;
    try { touch.evaluateMove(0, std::numeric_limits<double>::quiet_NaN(), 0); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "nonfinite coordinate accepted");

    StraightVisualState parallel({{20, 20, 0, 0}, {20, 20, 400, 0}},
      {{0, 1}, {1, 0}, {0, 1}, {1, 0}, {0, 1}});
    std::set<std::tuple<double, double, double, double>> distinct;
    for (auto route : parallel.routes()) {
      if (route.sourceX > route.targetX) {
        std::swap(route.sourceX, route.targetX); std::swap(route.sourceY, route.targetY);
      }
      require(route.sourceX == 10 && route.targetX == 390, "parallel line leaves real card boundary");
      distinct.insert({route.sourceX, route.sourceY, route.targetX, route.targetY});
    }
    require(distinct.size() == 5, "clamping merged distinct relationship lanes");
    require(parallel.score().invalidRoutes == 0, "distinct horizontal lanes are invalid");
    std::vector<StraightVisualEdge> tooMany(3000, {0, 1});
    StraightVisualState roundedLanes({{20, 20, 0, 0}, {20, 20, 400, 0}}, tooMany);
    require(roundedLanes.score().invalidRoutes > 0, "rounding merged lanes without invalidating the scene");

    std::mt19937_64 random(9031);
    std::uniform_real_distribution<double> coordinate(-2000, 2000);
    std::uniform_real_distribution<double> extent(10, 350);
    std::vector<StraightVisualNode> nodes;
    for (int i = 0; i < 35; ++i) nodes.push_back({extent(random), extent(random), coordinate(random), coordinate(random)});
    std::vector<StraightVisualEdge> edges;
    for (int i = 0; i < 60; ++i) {
      std::size_t a = random() % nodes.size(), b = random() % nodes.size();
      if (a == b) b = (b + 1) % nodes.size();
      edges.push_back({a, b});
    }
    StraightVisualState state(nodes, edges);
    for (int i = 0; i < 2000; ++i) {
      const auto node = random() % nodes.size();
      const double x = coordinate(random), y = coordinate(random);
      auto trial = state.nodes();
      trial[node].x = x; trial[node].y = y;
      const auto expected = measureStraightVisualFull(trial, edges);
      const auto evaluated = state.evaluateMove(node, x, y);
      require(same(evaluated, expected), "incremental move differs from complete scene audit");
      if (i % 3 == 0) {
        state.move(node, x, y);
        require(same(state.score(), expected), "stale index after accepted move");
      }
    }
    for (int i = 0; i < 1000; ++i) {
      std::size_t a = random() % nodes.size(), b = random() % nodes.size();
      if (a == b) b = (b + 1) % nodes.size();
      auto trial = state.nodes();
      std::swap(trial[a].x, trial[b].x); std::swap(trial[a].y, trial[b].y);
      const auto expected = measureStraightVisualFull(trial, edges);
      require(same(state.evaluateSwap(a, b), expected), "swap changes shared lines or both card obstacles incorrectly");
      if (i % 3 == 0) {
        state.swap(a, b);
        require(same(state.score(), expected), "committed swap leaves stale route or card index");
      }
    }
    for (int i = 0; i < 500; ++i) {
      auto trial = state.nodes();
      std::vector<StraightVisualMove> moves;
      const auto start = random() % nodes.size();
      const auto count = 2 + random() % 9;
      for (std::size_t j = 0; j < count; ++j) {
        const auto node = (start + j) % nodes.size();
        moves.push_back({node, coordinate(random), coordinate(random)});
        trial[node].x = moves.back().x; trial[node].y = moves.back().y;
      }
      const auto expected = measureStraightVisualFull(trial, edges);
      require(same(state.evaluateMoves(moves), expected), "group move double-counts changed edges or omits changed obstacles");
      if (i % 3 == 0) {
        state.moveMany(moves);
        require(same(state.score(), expected), "group commit leaves stale geometry");
      }
    }
    const auto saved = state.score();
    rejected = false;
    try { state.moveMany({{0, 100, 200}, {0, 300, 400}}); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && same(saved, state.score()), "duplicate move ids partially mutate the scene");
    const std::vector<StraightVisualNode> crossedNodes{{20, 20, -200, -200}, {20, 20, 200, 200},
      {20, 20, -200, 200}, {20, 20, 200, -200}};
    const std::vector<StraightVisualEdge> crossedEdges{{0, 1}, {2, 3}};
    StraightVisualPlacementOptions options;
    options.budgetMs = 0;
    const auto expired = optimizeStraightVisualPlacement(crossedNodes, crossedEdges,
      {"a", "b", "c", "d"}, {}, options);
    require(expired.moves == 0 && expired.nodes.size() == 4 && expired.routes.size() == 2,
      "zero budget lost original geometry or performed a search");
    for (std::size_t i = 0; i < crossedNodes.size(); ++i)
      require(expired.nodes[i].x == crossedNodes[i].x && expired.nodes[i].y == crossedNodes[i].y,
        "expired search changed positions");
    options.budgetMs = 100;
    const auto improved = optimizeStraightVisualPlacement(crossedNodes, crossedEdges,
      {"a", "b", "c", "d"}, {}, options);
    require(improved.after.visual() == 0 && improved.after.invalidRoutes == 0,
      "bounded placement failed to untangle the four-card crossing");
    require(improved.nodes.size() == 4 && improved.routes.size() == 2,
      "search lost a real card or relationship");
    std::cout << "2000 move evaluations match complete scene audits; unrelated obstacles, "
                 "crossings, reverts, touching boundaries and nonfinite input checked\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
