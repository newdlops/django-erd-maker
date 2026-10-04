#include "layoutPipeline.h"
#include "routeBoundsIndex.h"

namespace djerd {

void applyVisualKnot(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes,
  const LayoutRunMetadata& metadata,
  const std::vector<std::string>& carrierIdByEdgePre)
    // === Visual knot detector ===
    // Targets the user's "시각적으로 바로 풀수있는 knot들" — spatial
    // clusters of polyline crossings that pd-knot's edge-pair iteration
    // missed. Bucket polyline crossings by spatial cell (~300 units),
    // identify hot cells (≥ 3 crossings), then for each hot cell try
    // pairwise position swaps among the involved nodes. Accept swap if
    // global polyline cross count drops.
    //
    // Default ON. Disable with DJERD_VISUAL_KNOT=0.
    {
      const char* vkEnv = std::getenv("DJERD_VISUAL_KNOT");
      const bool runVk = !vkEnv || std::strcmp(vkEnv, "0") != 0;
      if (runVk) {
        const auto budgetStart = std::chrono::steady_clock::now();
        const double budgetMs = readDoubleEnv(
          "DJERD_VISUAL_KNOT_BUDGET_MS", 10000.0, 0.0, 60000.0);
        const bool disableWallClockBudgets = readBoolEnv(
          "DJERD_DISABLE_WALL_CLOCK_BUDGETS", false);
        auto budgetExceeded = [&]() {
          return !disableWallClockBudgets
            && std::chrono::duration<double, std::milli>(
                 std::chrono::steady_clock::now() - budgetStart).count() >= budgetMs;
        };
        // Build edge → node map.
        std::unordered_map<std::string, std::size_t> idToIdxVK;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          idToIdxVK[nodes[i].modelId] = i;
        }
        std::vector<std::pair<std::size_t, std::size_t>> edgePairsVK(edges.size());
        std::vector<std::vector<std::size_t>> edgesByNodeVK(nodes.size());
        for (std::size_t e = 0; e < edges.size(); ++e) {
          auto sIt = idToIdxVK.find(edges[e].sourceModelId);
          auto tIt = idToIdxVK.find(edges[e].targetModelId);
          if (sIt == idToIdxVK.end() || tIt == idToIdxVK.end()) {
            edgePairsVK[e] = {0, 0};
            continue;
          }
          edgePairsVK[e] = {sIt->second, tIt->second};
          if (sIt->second != tIt->second) {
            edgesByNodeVK[sIt->second].push_back(e);
            edgesByNodeVK[tIt->second].push_back(e);
          }
        }
        std::unordered_set<std::string> bundleAbsorbedVK;
        for (const LeafBundleRecord& b : metadata.leafBundles) {
          bundleAbsorbedVK.insert(b.parentModelId);
          for (const std::string& l : b.leafModelIds) {
            bundleAbsorbedVK.insert(l);
          }
        }

        // Rebuild these transient bounds for every layout. Moving or reverting
        // an endpoint updates the index before the next crossing evaluation.
        std::vector<Rect> routeBoundsVK(edges.size());
        RouteBoundsIndex routeIndexVK(edges.size());
        auto updateRouteBoundsVK = [&](std::size_t e) {
          Rect bounds;
          if (e < routes.size() && !routes[e].empty()) {
            bounds = {routes[e].front().y, routes[e].front().x,
                      routes[e].front().x, routes[e].front().y};
            for (const auto& point : routes[e]) {
              if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
                bounds.left = std::numeric_limits<double>::quiet_NaN();
                break;
              }
              bounds.left = std::min(bounds.left, point.x);
              bounds.right = std::max(bounds.right, point.x);
              bounds.top = std::min(bounds.top, point.y);
              bounds.bottom = std::max(bounds.bottom, point.y);
            }
          }
          routeBoundsVK[e] = bounds;
          routeIndexVK.update(e, bounds);
        };
        for (std::size_t e = 0; e < edges.size(); ++e) updateRouteBoundsVK(e);
        auto boundsCanCrossVK = [&](std::size_t e1, std::size_t e2) {
          const auto& left = routeBoundsVK[e1];
          const auto& right = routeBoundsVK[e2];
          // Nonfinite routes retain the original geometric predicate.
          return !std::isfinite(left.left) || !std::isfinite(right.left)
            || rectsOverlap(left, right);
        };

        auto polyCrossVK = [&](std::size_t e1, std::size_t e2) -> bool {
          if (e1 >= routes.size() || e2 >= routes.size()) return false;
          if (routes[e1].size() < 2 || routes[e2].size() < 2) return false;
          if (!boundsCanCrossVK(e1, e2)) return false;
          if (sharesEndpoint(edges[e1], edges[e2])) return false;
          // Plan A: same-carrier edges are masked in reported metric.
          if (e1 < carrierIdByEdgePre.size() && e2 < carrierIdByEdgePre.size()
              && !carrierIdByEdgePre[e1].empty()
              && carrierIdByEdgePre[e1] == carrierIdByEdgePre[e2]) {
            return false;
          }
          for (std::size_t li = 1; li < routes[e1].size(); ++li) {
            for (std::size_t rj = 1; rj < routes[e2].size(); ++rj) {
              RoutePoint isect;
              if (properSegmentIntersection(
                  routes[e1][li - 1], routes[e1][li],
                  routes[e2][rj - 1], routes[e2][rj], isect)) {
                return true;
              }
            }
          }
          return false;
        };
        auto polyCrossPointVK = [&](std::size_t e1, std::size_t e2,
                                     RoutePoint& outPt) -> bool {
          if (e1 >= routes.size() || e2 >= routes.size()) return false;
          if (routes[e1].size() < 2 || routes[e2].size() < 2) return false;
          if (!boundsCanCrossVK(e1, e2)) return false;
          if (sharesEndpoint(edges[e1], edges[e2])) return false;
          if (e1 < carrierIdByEdgePre.size() && e2 < carrierIdByEdgePre.size()
              && !carrierIdByEdgePre[e1].empty()
              && carrierIdByEdgePre[e1] == carrierIdByEdgePre[e2]) {
            return false;
          }
          for (std::size_t li = 1; li < routes[e1].size(); ++li) {
            for (std::size_t rj = 1; rj < routes[e2].size(); ++rj) {
              if (properSegmentIntersection(
                  routes[e1][li - 1], routes[e1][li],
                  routes[e2][rj - 1], routes[e2][rj], outPt)) {
                return true;
              }
            }
          }
          return false;
        };

        // Collect cross points with coordinates.
        struct VKCross {
          double x, y;
          std::size_t e1, e2;
        };
        std::vector<VKCross> crosses;
        for (std::size_t i = 0; i < edges.size(); ++i) {
          for (std::size_t j = i + 1; j < edges.size(); ++j) {
            RoutePoint pt;
            if (polyCrossPointVK(i, j, pt)) {
              crosses.push_back({pt.x, pt.y, i, j});
            }
          }
        }

        // Spatial bucketing: cell ~300 units (typical node diameter).
        constexpr double kCellSize = 300.0;
        auto cellKey = [&](double x, double y) {
          return std::make_pair(static_cast<long long>(std::floor(x / kCellSize)),
                                static_cast<long long>(std::floor(y / kCellSize)));
        };
        std::map<std::pair<long long, long long>, std::vector<std::size_t>> cellMap;
        for (std::size_t k = 0; k < crosses.size(); ++k) {
          cellMap[cellKey(crosses[k].x, crosses[k].y)].push_back(k);
        }

        // Hot cells: 3+ crossings.
        constexpr std::size_t kMinKnot = 3;
        std::vector<std::pair<std::size_t, std::pair<long long, long long>>> hotCells;
        for (const auto& cm : cellMap) {
          if (cm.second.size() >= kMinKnot) {
            hotCells.emplace_back(cm.second.size(), cm.first);
          }
        }
        std::sort(hotCells.begin(), hotCells.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });

        // For each hot cell, collect involved nodes and try pairwise swap.
        // Cost: total polyline crosses incident to the swap pair. Accept if
        // the swap reduces it.
        auto incidentCrossVK = [&](std::size_t m1, std::size_t m2) -> std::size_t {
          std::unordered_set<std::size_t> incident;
          for (std::size_t e : edgesByNodeVK[m1]) incident.insert(e);
          for (std::size_t e : edgesByNodeVK[m2]) incident.insert(e);
          std::size_t total = 0;
          for (std::size_t e1 : incident) {
            for (std::size_t e2 : routeIndexVK.query(routeBoundsVK[e1])) {
              if (e1 == e2) continue;
              if (incident.count(e2) && e2 < e1) continue;
              if (polyCrossVK(e1, e2)) ++total;
            }
          }
          return total;
        };
        auto applyEndpointMoveVK = [&](std::size_t node) {
          for (std::size_t e : edgesByNodeVK[node]) {
            if (e >= routes.size() || routes[e].size() < 2) continue;
            const double nx = attributes.x(nodes[node].handle);
            const double ny = attributes.y(nodes[node].handle);
            if (edgePairsVK[e].first == node) routes[e].front() = {nx, ny};
            if (edgePairsVK[e].second == node) routes[e].back() = {nx, ny};
            updateRouteBoundsVK(e);
          }
        };

        std::size_t totalAccepted = 0;
        std::size_t totalHandled = 0;
        std::size_t initialCrossPoints = crosses.size();
        std::size_t initialHotCells = hotCells.size();

        // Multi-iteration: re-detect hot cells after each round of swaps.
        // Resolved swaps may have eliminated some hot cells but created
        // new patterns. Default 3 rounds, env-tunable.
        const char* vkIterEnv = std::getenv("DJERD_VISUAL_KNOT_ITERS");
        const int vkMaxIters = vkIterEnv ? std::max(1, std::atoi(vkIterEnv)) : 3;
        bool budgetHit = budgetExceeded();
        for (int iter = 0; iter < vkMaxIters; ++iter) {
          if (budgetHit) break;
          std::size_t iterAccepted = 0;
          for (const auto& hc : hotCells) {
            std::set<std::size_t> involved;
            for (std::size_t crossIdx : cellMap[hc.second]) {
              const auto& cr = crosses[crossIdx];
              involved.insert(edgePairsVK[cr.e1].first);
              involved.insert(edgePairsVK[cr.e1].second);
              involved.insert(edgePairsVK[cr.e2].first);
              involved.insert(edgePairsVK[cr.e2].second);
            }
            std::vector<std::size_t> swappable;
            for (std::size_t n : involved) {
              if (n < nodes.size()
                  && !bundleAbsorbedVK.count(nodes[n].modelId)) {
                swappable.push_back(n);
              }
            }
            if (swappable.size() < 2) continue;
            ++totalHandled;

            for (std::size_t i = 0; i < swappable.size(); ++i) {
              for (std::size_t j = i + 1; j < swappable.size(); ++j) {
                // Stop only between complete candidates. A tentative swap
                // has already been accepted or reverted before this check.
                if (budgetExceeded()) {
                  budgetHit = true;
                  break;
                }
                const std::size_t m1 = swappable[i];
                const std::size_t m2 = swappable[j];
                const std::size_t before = incidentCrossVK(m1, m2);
                if (before == 0) continue;
                const double x1 = attributes.x(nodes[m1].handle);
                const double y1 = attributes.y(nodes[m1].handle);
                attributes.x(nodes[m1].handle) = attributes.x(nodes[m2].handle);
                attributes.y(nodes[m1].handle) = attributes.y(nodes[m2].handle);
                attributes.x(nodes[m2].handle) = x1;
                attributes.y(nodes[m2].handle) = y1;
                applyEndpointMoveVK(m1);
                applyEndpointMoveVK(m2);
                const std::size_t after = incidentCrossVK(m1, m2);
                if (after < before) {
                  ++iterAccepted;
                } else {
                  attributes.x(nodes[m2].handle) = attributes.x(nodes[m1].handle);
                  attributes.y(nodes[m2].handle) = attributes.y(nodes[m1].handle);
                  attributes.x(nodes[m1].handle) = x1;
                  attributes.y(nodes[m1].handle) = y1;
                  applyEndpointMoveVK(m1);
                  applyEndpointMoveVK(m2);
                }
              }
              if (budgetHit) break;
            }
            if (budgetHit) break;

            // 3-rotation: for hot cells with EXACTLY 3 swappable nodes,
            // try the two cyclic rotations (A→C→B→A and A→B→C→A). Pair
            // swaps alone can't achieve these — they require all 3
            // positions to cycle simultaneously. Implemented as 2
            // sequential pair-swaps.
            if (swappable.size() == 3) {
              const std::size_t a = swappable[0];
              const std::size_t b = swappable[1];
              const std::size_t c = swappable[2];
              auto swapPair = [&](std::size_t m1, std::size_t m2) {
                const double x1 = attributes.x(nodes[m1].handle);
                const double y1 = attributes.y(nodes[m1].handle);
                attributes.x(nodes[m1].handle) = attributes.x(nodes[m2].handle);
                attributes.y(nodes[m1].handle) = attributes.y(nodes[m2].handle);
                attributes.x(nodes[m2].handle) = x1;
                attributes.y(nodes[m2].handle) = y1;
                applyEndpointMoveVK(m1);
                applyEndpointMoveVK(m2);
              };
              auto cost3 = [&]() {
                std::unordered_set<std::size_t> incident;
                for (std::size_t e : edgesByNodeVK[a]) incident.insert(e);
                for (std::size_t e : edgesByNodeVK[b]) incident.insert(e);
                for (std::size_t e : edgesByNodeVK[c]) incident.insert(e);
                std::size_t total = 0;
                for (std::size_t e1 : incident) {
                  for (std::size_t e2 : routeIndexVK.query(routeBoundsVK[e1])) {
                    if (e1 == e2) continue;
                    if (incident.count(e2) && e2 < e1) continue;
                    if (polyCrossVK(e1, e2)) ++total;
                  }
                }
                return total;
              };
              const std::size_t before3 = cost3();
              if (before3 > 0) {
                // Rotation 1: a@C, b@A, c@B — swap(a,b); swap(a,c).
                swapPair(a, b);
                swapPair(a, c);
                const std::size_t after1 = cost3();
                if (after1 < before3) {
                  ++iterAccepted;
                } else {
                  // Revert rotation 1.
                  swapPair(a, c);
                  swapPair(a, b);
                  // Rotation 2: a@B, b@C, c@A — swap(a,c); swap(a,b).
                  swapPair(a, c);
                  swapPair(a, b);
                  const std::size_t after2 = cost3();
                  if (after2 < before3) {
                    ++iterAccepted;
                  } else {
                    swapPair(a, b);
                    swapPair(a, c);
                  }
                }
              }
            }
          }
          totalAccepted += iterAccepted;
          if (iterAccepted == 0) break;

          // Re-detect crosses + hot cells for next iteration.
          crosses.clear();
          for (std::size_t i = 0; i < edges.size(); ++i) {
            for (std::size_t j = i + 1; j < edges.size(); ++j) {
              RoutePoint pt;
              if (polyCrossPointVK(i, j, pt)) {
                crosses.push_back({pt.x, pt.y, i, j});
              }
            }
          }
          cellMap.clear();
          for (std::size_t k = 0; k < crosses.size(); ++k) {
            cellMap[cellKey(crosses[k].x, crosses[k].y)].push_back(k);
          }
          hotCells.clear();
          for (const auto& cm : cellMap) {
            if (cm.second.size() >= kMinKnot) {
              hotCells.emplace_back(cm.second.size(), cm.first);
            }
          }
          std::sort(hotCells.begin(), hotCells.end(),
                    [](const auto& a, const auto& b) { return a.first > b.first; });
          if (budgetHit) break;
        }

        std::fprintf(stderr,
          "[visual-knot] %zu→%zu cross points, %zu→%zu hot cells (≥%zu), "
          "%zu handled, %zu swap accepted (budgetHit=%d, budgetMs=%.0f).\n",
          initialCrossPoints, crosses.size(),
          initialHotCells, hotCells.size(),
          kMinKnot, totalHandled, totalAccepted, budgetHit, budgetMs);
      }
    }


}  // namespace djerd
