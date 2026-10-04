#include "clusterCrossingOptimization.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace djerd {
void minimizeClusterSupergraphCrossings(
  ogdf::Graph& super, ogdf::GraphAttributes& superAttr,
  const std::unordered_set<ogdf::node>& clusterSnSet, bool skipCgPositioning) {
    std::vector<std::pair<ogdf::node, ogdf::node>> sEdges;
    for (ogdf::edge e : super.edges) sEdges.emplace_back(e->source(), e->target());
    std::unordered_map<ogdf::node, std::vector<std::size_t>> incidentE;
    for (std::size_t i = 0; i < sEdges.size(); ++i) {
      incidentE[sEdges[i].first].push_back(i);
      incidentE[sEdges[i].second].push_back(i);
    }

    const bool useSwapAabb = [] {
      const char* value = std::getenv("DJERD_CG_SWAP_AABB");
      return !value || std::strcmp(value, "0") != 0;
    }();
    const bool useSwapCache = [] {
      const char* value = std::getenv("DJERD_CG_SWAP_CACHE");
      return !value || std::strcmp(value, "0") != 0;
    }();
    auto segCross = [useSwapAabb](
                       double ax, double ay, double bx, double by,
                       double cx, double cy, double dx, double dy) -> bool {
      // Exact broad phase: disjoint inclusive AABBs cannot cross. Keep `<`
      // (not `<=`) so touching bounds still reach the orientation test.
      if (useSwapAabb
          && (std::max(ax, bx) < std::min(cx, dx)
              || std::max(cx, dx) < std::min(ax, bx)
              || std::max(ay, by) < std::min(cy, dy)
              || std::max(cy, dy) < std::min(ay, by))) {
        return false;
      }
      auto ccw = [](double Ax, double Ay, double Bx, double By,
                    double Cx, double Cy) -> double {
        return (Cy - Ay) * (Bx - Ax) - (By - Ay) * (Cx - Ax);
      };
      const double d1 = ccw(cx, cy, dx, dy, ax, ay);
      const double d2 = ccw(cx, cy, dx, dy, bx, by);
      const double d3 = ccw(ax, ay, bx, by, cx, cy);
      const double d4 = ccw(ax, ay, bx, by, dx, dy);
      return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
             ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
    };

    auto incidentCrossRaw = [&](ogdf::node n) -> int {
      int total = 0;
      auto it = incidentE.find(n);
      if (it == incidentE.end()) return 0;
      for (std::size_t i : it->second) {
        const ogdf::node a = sEdges[i].first;
        const ogdf::node b = sEdges[i].second;
        const double ax = superAttr.x(a), ay = superAttr.y(a);
        const double bx = superAttr.x(b), by = superAttr.y(b);
        for (std::size_t j = 0; j < sEdges.size(); ++j) {
          if (i == j) continue;
          const ogdf::node u = sEdges[j].first;
          const ogdf::node v = sEdges[j].second;
          if (a == u || a == v || b == u || b == v) continue;
          const double ux = superAttr.x(u), uy = superAttr.y(u);
          const double vx = superAttr.x(v), vy = superAttr.y(v);
          if (segCross(ax, ay, bx, by, ux, uy, vx, vy)) ++total;
        }
      }
      return total;
    };
    std::unordered_map<ogdf::node, int> incidentCrossCache;
    incidentCrossCache.reserve(clusterSnSet.size());
    auto incidentCross = [&](ogdf::node n) -> int {
      if (!useSwapCache) return incidentCrossRaw(n);
      auto cached = incidentCrossCache.find(n);
      if (cached != incidentCrossCache.end()) return cached->second;
      const int total = incidentCrossRaw(n);
      incidentCrossCache.emplace(n, total);
      return total;
    };

    struct NodePairHash {
      std::size_t operator()(
          const std::pair<ogdf::node, ogdf::node>& value) const noexcept {
        const std::size_t left = std::hash<ogdf::node>{}(value.first);
        const std::size_t right = std::hash<ogdf::node>{}(value.second);
        return left ^ (right + 0x9e3779b9U + (left << 6U) + (left >> 2U));
      }
    };
    std::unordered_map<std::pair<ogdf::node, ogdf::node>, int, NodePairHash>
      swapGainCache;
    swapGainCache.reserve(clusterSnSet.size() * 4);
    auto orderedNodePair = [](ogdf::node left, ogdf::node right) {
      return left->index() <= right->index()
        ? std::make_pair(left, right)
        : std::make_pair(right, left);
    };

    auto swapPos = [&](ogdf::node n1, ogdf::node n2) {
      const double tx = superAttr.x(n1);
      const double ty = superAttr.y(n1);
      superAttr.x(n1) = superAttr.x(n2);
      superAttr.y(n1) = superAttr.y(n2);
      superAttr.x(n2) = tx;
      superAttr.y(n2) = ty;
    };

    // env-tunable (DJERD_CG_SWAP_ITERS, default 4); the swap pass's
    // incidentCross is O(super-edges), so on the dense inheritance super-graph
    // this loop scales ~O(E²) and is a prime multistart-cost suspect.
    const int kMaxIter = skipCgPositioning ? 0 : [] {
      const char* e = std::getenv("DJERD_CG_SWAP_ITERS");
      return e ? std::max(0, std::atoi(e)) : 4;
    }();
    constexpr int kMaxSwapsPerIter = 80;
    for (int iter = 0; iter < kMaxIter; ++iter) {
      int swaps = 0;
      bool anyImproved = false;
      for (std::size_t i = 0; i < sEdges.size() && swaps < kMaxSwapsPerIter; ++i) {
        const ogdf::node a = sEdges[i].first;
        const ogdf::node b = sEdges[i].second;
        const double ax = superAttr.x(a), ay = superAttr.y(a);
        const double bx = superAttr.x(b), by = superAttr.y(b);
        for (std::size_t j = i + 1; j < sEdges.size() && swaps < kMaxSwapsPerIter; ++j) {
          const ogdf::node u = sEdges[j].first;
          const ogdf::node v = sEdges[j].second;
          if (a == u || a == v || b == u || b == v) continue;
          const double ux = superAttr.x(u), uy = superAttr.y(u);
          const double vx = superAttr.x(v), vy = superAttr.y(v);
          if (!segCross(ax, ay, bx, by, ux, uy, vx, vy)) continue;

          // Try the 4 endpoint-swap candidates; pick the one with the
          // largest reduction in local crossings.
          ogdf::node candA[4] = {a, a, b, b};
          ogdf::node candB[4] = {u, v, u, v};
          int bestSave = 0;
          int bestK = -1;
          for (int k = 0; k < 4; ++k) {
            if (!clusterSnSet.count(candA[k]) || !clusterSnSet.count(candB[k])) continue;
            const auto pairKey = orderedNodePair(candA[k], candB[k]);
            auto cachedGain = useSwapCache
              ? swapGainCache.find(pairKey)
              : swapGainCache.end();
            int save = 0;
            if (cachedGain != swapGainCache.end()) {
              save = cachedGain->second;
            } else {
              const int oldCnt =
                incidentCross(candA[k]) + incidentCross(candB[k]);
              swapPos(candA[k], candB[k]);
              // Trial coordinates differ from the cached current layout.
              const int newCnt =
                incidentCrossRaw(candA[k]) + incidentCrossRaw(candB[k]);
              swapPos(candA[k], candB[k]);  // revert (involutive)
              save = oldCnt - newCnt;
              if (useSwapCache) swapGainCache.emplace(pairKey, save);
            }
            if (save > bestSave) { bestSave = save; bestK = k; }
          }
          if (bestK >= 0) {
            swapPos(candA[bestK], candB[bestK]);
            // Any accepted swap changes crossing counts globally; the next
            // greedy decision must be evaluated against the new layout.
            incidentCrossCache.clear();
            swapGainCache.clear();
            ++swaps;
            anyImproved = true;
            break;  // restart i scan (positions changed)
          }
        }
      }
      if (!anyImproved) break;
    }
}
}  // namespace djerd
