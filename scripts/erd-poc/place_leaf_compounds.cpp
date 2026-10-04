// Bounded coordinate placement of real-size leaf compound rectangles.
// Core cards can be fixed for the ablation. All quotient relationships
// enter the objective, including roots hidden by the previous overview groups.
#include "constrained_scene.h"
#include <sstream>
#include <unordered_set>

int main(int argc, char** argv) {
  try {
    const std::string directory = arg(argc, argv, "--directory");
    const std::string output = arg(argc, argv, "--out");
    const double seconds = number(argc, argv, "--seconds", 60);
    const double overviewWeight = number(argc, argv, "--overview-weight", 4);
    const bool moveCore = number(argc, argv, "--move-core", 0) != 0;
    const double compoundHitWeight = number(argc, argv, "--compound-hit-weight", 1);
    const long maxEvaluations = long(number(argc, argv, "--max-evaluations", 1000000000));
    State all, overview;
    all.width = overview.width = 38720;
    all.height = overview.height = 38720; // 1.4992384B, including compounds.
    std::unordered_map<std::string, int> nodeIndex, edgeIndex;
    std::vector<int> movable;
    std::vector<char> active;
    std::string line;
    std::ifstream nodes(directory + "/nodes.tsv");
    while (std::getline(nodes, line)) {
      const auto fields = split(line);
      const int index = int(all.nodes.size());
      nodeIndex[fields.at(0)] = index;
      all.nodes.push_back({fields[0], std::stod(fields.at(1)), std::stod(fields.at(2))});
      const bool compound = fields.at(3) == "1";
      if (compound) movable.push_back(index);
      active.push_back(!compound);
    }
    if (all.nodes.empty() || (movable.empty() && !moveCore)) throw std::runtime_error("empty compound graph");
    all.pos.resize(all.nodes.size());
    std::ifstream positions(directory + "/positions.tsv");
    while (std::getline(positions, line)) {
      const auto fields = split(line);
      all.pos.at(nodeIndex.at(fields.at(0))) = {std::stod(fields.at(1)), std::stod(fields.at(2))};
    }
    std::vector<char> visible;
    std::ifstream edges(directory + "/edges.tsv");
    while (std::getline(edges, line)) {
      const auto fields = split(line);
      edgeIndex[fields.at(0)] = int(all.edges.size());
      all.edges.push_back({fields[0], nodeIndex.at(fields.at(1)), nodeIndex.at(fields.at(2))});
      visible.push_back(fields.at(3) == "1");
    }
    all.routes.resize(all.edges.size());
    std::ifstream routes(directory + "/routes.tsv");
    while (std::getline(routes, line)) {
      const auto fields = split(line);
      auto coordinates = fields.at(1);
      std::replace(coordinates.begin(), coordinates.end(), ',', ' ');
      Point a, b;
      std::istringstream input(coordinates);
      if (!(input >> a.x >> a.y >> b.x >> b.y)) throw std::runtime_error("invalid route");
      all.routes.at(edgeIndex.at(fields.at(0))) = segment(a, b);
    }
    overview.nodes = all.nodes;
    overview.pos = all.pos;
    for (int index = 0; index < int(all.edges.size()); ++index) if (visible[index]) {
      overview.edges.push_back(all.edges[index]);
      overview.routes.push_back(all.routes[index]);
    }
    for (State* state : {&all, &overview}) {
      state->incident.resize(state->nodes.size());
      state->moved.resize(state->nodes.size());
      state->changedEdge.resize(state->edges.size());
      for (int index = 0; index < int(state->edges.size()); ++index) {
        state->incident[state->edges[index].s].push_back(index);
        state->incident[state->edges[index].t].push_back(index);
      }
    }
    const auto started = std::chrono::steady_clock::now();
    const auto elapsed = [&]() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(); };
    const auto free = [&](int n, Point p) {
      if (!all.inside(n, p)) return false;
      for (int other = 0; other < int(all.nodes.size()); ++other) if (other != n && active[other]) {
        const auto& a = all.nodes[n]; const auto& b = all.nodes[other];
        if (std::abs(p.x - all.pos[other].x) < (a.w + b.w) / 2 + 55.99 &&
            std::abs(p.y - all.pos[other].y) < (a.h + b.h) / 2 + 41.99) return false;
      }
      return true;
    };
    const auto anchor = [&](int n) {
      Point p;
      for (int edge : all.incident[n]) {
        const auto& e = all.edges[edge]; const auto q = all.pos[e.s == n ? e.t : e.s];
        p.x += q.x; p.y += q.y;
      }
      const double count = std::max(size_t(1), all.incident[n].size());
      return Point{p.x / count, p.y / count};
    };
    std::stable_sort(movable.begin(), movable.end(), [&](int a, int b) {
      return all.nodes[a].w * all.nodes[a].h > all.nodes[b].w * all.nodes[b].h;
    });
    // Place only compounds; previously placed core rectangles are hard obstacles.
    for (int n : movable) {
      const Point wanted = all.pos[n];
      Point best = wanted;
      bool found = free(n, best);
      for (int radius = 1; !found && radius < 500; ++radius) {
        const int count = std::max(24, radius * 6);
        for (int index = 0; index < count && !found; ++index) {
          const double angle = index * 6.283185307179586 / count;
          const Point p{wanted.x + radius * 60 * std::cos(angle), wanted.y + radius * 60 * std::sin(angle)};
          if (free(n, p)) { best = p; found = true; }
        }
      }
      if (!found) throw std::runtime_error("cannot place compound " + all.nodes[n].id);
      all.pos[n] = overview.pos[n] = best;
      active[n] = true;
    }
    for (int n : movable) { all.update(all.incident[n]); overview.update(overview.incident[n]); }
    Score fullScore = all.full(), overviewScore = overview.full();
    if (fullScore.overlap || fullScore.spacing) throw std::runtime_error("infeasible initial packing");
    all.save(output + ".initial.tsv");
    report("compound-initial-all", 0, fullScore, elapsed());
    report("compound-initial-overview", 0, overviewScore, elapsed());
    long evaluated = 0, accepted = 0;
    const auto compoundHits = [&](const State& state, int n) {
      long count = 0;
      for (int e : state.incident[n]) for (int c : movable) {
        if (c != state.edges[e].s && c != state.edges[e].t) count += hits(state.routes[e], state.pos[c], state.nodes[c]);
      }
      if (all.nodes[n].id.rfind("@leaf:", 0) == 0) {
        for (int e = 0; e < int(state.edges.size()); ++e) if (!state.changedEdge[e]) count += hits(state.routes[e], state.pos[n], state.nodes[n]);
      }
      return count;
    };
    const auto length = [&](int n) {
      double total = 0;
      for (int e : all.incident[n]) total += std::hypot(all.routes[e].a.x - all.routes[e].b.x, all.routes[e].a.y - all.routes[e].b.y);
      return total;
    };
    for (int round = 0; round < 5 && elapsed() < seconds && evaluated < maxEvaluations; ++round) {
      bool improved = false;
      std::vector<int> order = movable;
      if (moveCore && (round > 0 || movable.empty())) {
        std::vector<long> pressure;
        overview.full(&pressure);
        std::vector<int> core;
        for (int n = 0; n < int(all.nodes.size()); ++n) if (all.nodes[n].id.rfind("@leaf:", 0) != 0 && pressure[n] > 0) core.push_back(n);
        std::stable_sort(core.begin(), core.end(), [&](int a, int b) { return pressure[a] > pressure[b]; });
        core.insert(core.end(), order.begin(), order.end());
        order = std::move(core);
      }
      for (int n : order) {
        if (elapsed() >= seconds || evaluated >= maxEvaluations) break;
        const Point previous = all.pos[n], target = anchor(n);
        const std::vector<int> moved{n};
        for (State* state : {&all, &overview}) {
          state->moved[n] = 1;
          for (int edge : state->incident[n]) state->changedEdge[edge] = 1;
        }
        const Score oldAll = all.local(moved, all.incident[n]), oldOverview = overview.local(moved, overview.incident[n]);
        Score bestAll = oldAll, bestOverview = oldOverview;
        double bestBarrier = compoundHits(all, n) + overviewWeight * compoundHits(overview, n);
        Point best = previous;
        double bestLength = length(n);
        std::vector<Segment> bestAllRoutes, bestOverviewRoutes;
        const auto saveBestRoutes = [&]() {
          bestAllRoutes.clear(); bestOverviewRoutes.clear();
          for (int e : all.incident[n]) bestAllRoutes.push_back(all.routes[e]);
          for (int e : overview.incident[n]) bestOverviewRoutes.push_back(overview.routes[e]);
        };
        saveBestRoutes();
        const auto tryPosition = [&](Point p) {
          if (evaluated >= maxEvaluations) return;
          ++evaluated;
          if (!free(n, p)) return;
          all.pos[n] = overview.pos[n] = p;
          all.update(all.incident[n]); overview.update(overview.incident[n]);
          const Score candidateAll = all.local(moved, all.incident[n]);
          const Score candidateOverview = overview.local(moved, overview.incident[n]);
          const double barrier = compoundHits(all, n) + overviewWeight * compoundHits(overview, n);
          const double cost = candidateAll.visual() + overviewWeight * candidateOverview.visual() + (compoundHitWeight - 1) * barrier;
          const double bestCost = bestAll.visual() + overviewWeight * bestOverview.visual() + (compoundHitWeight - 1) * bestBarrier;
          const double candidateLength = length(n);
          if (cost < bestCost || (cost == bestCost && candidateLength < bestLength - .01)) {
            best = p; bestAll = candidateAll; bestOverview = candidateOverview; bestLength = candidateLength;
            bestBarrier = barrier;
            saveBestRoutes();
          }
        };
        for (double radius : {56., 128., 256., 512., 1024., 2048., 4096.}) {
          for (int angle = 0; angle < 32; ++angle) {
            const double theta = angle * 6.283185307179586 / 32;
            tryPosition({previous.x + radius * std::cos(theta), previous.y + radius * std::sin(theta)});
            tryPosition({target.x + radius * std::cos(theta), target.y + radius * std::sin(theta)});
          }
        }
        if (round == 0 || (moveCore && round == 1)) {
          for (double x = all.nodes[n].w / 2 + 100; x < all.width - all.nodes[n].w / 2; x += 640) {
            if (elapsed() >= seconds) break;
            for (double y = all.nodes[n].h / 2 + 100; y < all.height - all.nodes[n].h / 2; y += 640) tryPosition({x, y});
          }
        }
        all.pos[n] = overview.pos[n] = best;
        for (size_t i = 0; i < all.incident[n].size(); ++i) all.routes[all.incident[n][i]] = bestAllRoutes[i];
        for (size_t i = 0; i < overview.incident[n].size(); ++i) overview.routes[overview.incident[n][i]] = bestOverviewRoutes[i];
        fullScore = fullScore - oldAll + bestAll;
        overviewScore = overviewScore - oldOverview + bestOverview;
        if (std::hypot(best.x - previous.x, best.y - previous.y) > .01) { improved = true; ++accepted; }
        for (State* state : {&all, &overview}) {
          state->moved[n] = 0;
          for (int edge : state->incident[n]) state->changedEdge[edge] = 0;
        }
      }
      if (!equal(fullScore, all.full()) || !equal(overviewScore, overview.full())) throw std::runtime_error("compound score delta mismatch");
      report("compound-round-all", round, fullScore, elapsed());
      report("compound-round-overview", round, overviewScore, elapsed());
      if (!improved) break;
    }
    all.save(output);
    std::cout << "{\"actors\":" << all.nodes.size() << ",\"compounds\":" << movable.size()
      << ",\"allVisual\":" << fullScore.visual() << ",\"overviewVisual\":" << overviewScore.visual()
      << ",\"overlap\":" << fullScore.overlap << ",\"spacing\":" << fullScore.spacing
      << ",\"evaluated\":" << evaluated << ",\"accepted\":" << accepted << ",\"seconds\":" << elapsed() << "}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
