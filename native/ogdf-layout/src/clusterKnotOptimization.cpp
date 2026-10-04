#include "layoutPipeline.h"

namespace djerd {

void minimizeClusterKnots(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::unordered_map<std::string, std::string>& clusterByModelIdFull,
  ogdf::GraphAttributes& attributes, LayoutRunMetadata& metadata) {
      const char* skipKnotEnv = std::getenv("DJERD_NO_KNOT_MIN");
      const bool skipKnot =
        skipKnotEnv && std::strcmp(skipKnotEnv, "0") != 0;
      const bool runKnotRelocate =
        readBoolEnv("DJERD_KNOT_RELOCATE", false);
      if (!skipKnot || runKnotRelocate) {
        const auto swapBudgetStart = std::chrono::steady_clock::now();
        const double swapBudgetMs = readDoubleEnv(
          "DJERD_KNOT_SWAP_BUDGET_MS", 5000.0, 0.0, 60000.0);
        const bool unlimitedSwaps = readBoolEnv(
          "DJERD_DISABLE_WALL_CLOCK_BUDGETS", false);
        bool swapBudgetHit = false;
        auto swapExpired = [&]() {
          swapBudgetHit = !unlimitedSwaps
            && std::chrono::duration<double, std::milli>(
                 std::chrono::steady_clock::now() - swapBudgetStart).count()
                 >= swapBudgetMs;
          return swapBudgetHit;
        };
        const bool directScene = readBoolEnv("DJERD_NO_CARRIER_CROSS", false);
        std::unordered_set<std::string> bundleAbsorbedKM;
        if (!directScene) {
          for (const LeafBundleRecord& bundle : metadata.leafBundles) {
            bundleAbsorbedKM.insert(bundle.parentModelId);
            for (const std::string& leaf : bundle.leafModelIds) {
              bundleAbsorbedKM.insert(leaf);
            }
          }
        }
        // Group nodes by cluster (cluster id → node indices, excluding
        // bundle-absorbed).
        std::unordered_map<std::string, std::vector<std::size_t>>
          membersByCluster;
        std::unordered_map<std::string, std::size_t> idToIdxKM;
        idToIdxKM.reserve(nodes.size());
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          idToIdxKM[nodes[i].modelId] = i;
        }
        for (const auto& kv : clusterByModelIdFull) {
          if (bundleAbsorbedKM.count(kv.first)) continue;
          auto it = idToIdxKM.find(kv.first);
          if (it == idToIdxKM.end()) continue;
          membersByCluster[kv.second].push_back(it->second);
        }
        // Build edge index by node — for each node, the edges incident.
        std::vector<std::vector<std::size_t>> edgesByNode(nodes.size());
        for (std::size_t e = 0; e < edges.size(); ++e) {
          auto sIt = idToIdxKM.find(edges[e].sourceModelId);
          auto tIt = idToIdxKM.find(edges[e].targetModelId);
          if (sIt == idToIdxKM.end() || tIt == idToIdxKM.end()) continue;
          edgesByNode[sIt->second].push_back(e);
          edgesByNode[tIt->second].push_back(e);
        }
        // Build edge endpoint indices for crossing tests.
        std::vector<std::pair<std::size_t, std::size_t>> edgePairs(edges.size());
        for (std::size_t e = 0; e < edges.size(); ++e) {
          auto sIt = idToIdxKM.find(edges[e].sourceModelId);
          auto tIt = idToIdxKM.find(edges[e].targetModelId);
          if (sIt == idToIdxKM.end() || tIt == idToIdxKM.end()) {
            edgePairs[e] = {0, 0};
            continue;
          }
          edgePairs[e] = {sIt->second, tIt->second};
        }
        auto sgn = [](double x) { return (x > 0) - (x < 0); };
        auto segmentsCross = [&](std::size_t e1, std::size_t e2) {
          const auto& p1 = edgePairs[e1];
          const auto& p2 = edgePairs[e2];
          if (p1.first == p2.first || p1.first == p2.second
              || p1.second == p2.first || p1.second == p2.second) return false;
          const double ax = attributes.x(nodes[p1.first].handle);
          const double ay = attributes.y(nodes[p1.first].handle);
          const double bx = attributes.x(nodes[p1.second].handle);
          const double by = attributes.y(nodes[p1.second].handle);
          const double cx = attributes.x(nodes[p2.first].handle);
          const double cy = attributes.y(nodes[p2.first].handle);
          const double dx = attributes.x(nodes[p2.second].handle);
          const double dy = attributes.y(nodes[p2.second].handle);
          const int o1 = sgn((bx - ax) * (cy - ay) - (by - ay) * (cx - ax));
          const int o2 = sgn((bx - ax) * (dy - ay) - (by - ay) * (dx - ax));
          const int o3 = sgn((dx - cx) * (ay - cy) - (dy - cy) * (ax - cx));
          const int o4 = sgn((dx - cx) * (by - cy) - (dy - cy) * (bx - cx));
          return (o1 != o2) && (o3 != o4) && (o1 != 0) && (o3 != 0);
        };
        // Count crossings involving any edge incident to m1 or m2.
        auto localCrossCount = [&](std::size_t m1, std::size_t m2) {
          std::unordered_set<std::size_t> incident;
          for (std::size_t e : edgesByNode[m1]) incident.insert(e);
          for (std::size_t e : edgesByNode[m2]) incident.insert(e);
          std::size_t total = 0;
          for (std::size_t e1 : incident) {
            for (std::size_t e2 = 0; e2 < edges.size(); ++e2) {
              if (incident.count(e2) && e2 <= e1) continue;
              if (segmentsCross(e1, e2)) ++total;
            }
          }
          return total;
        };
        // Pre-build bundle bboxes (with margin) so knot-min's overlap
        // check can also detect when a swap pushes a node into a
        // leafBundle's territory.
        constexpr double kKnotOverlapMargin = 8.0;
        std::vector<Rect> bundleBoxesKM;
        std::vector<std::unordered_set<std::size_t>> bundleExemptIdx;
        bundleBoxesKM.reserve(metadata.leafBundles.size());
        bundleExemptIdx.reserve(metadata.leafBundles.size());
        if (!directScene) {
          for (const LeafBundleRecord& bundle : metadata.leafBundles) {
            Rect br;
            br.left = bundle.bboxX - kKnotOverlapMargin;
            br.right = bundle.bboxX + bundle.bboxWidth + kKnotOverlapMargin;
            br.top = bundle.bboxY - kKnotOverlapMargin;
            br.bottom = bundle.bboxY + bundle.bboxHeight + kKnotOverlapMargin;
            bundleBoxesKM.push_back(br);
            std::unordered_set<std::size_t> exempt;
            auto pIt = idToIdxKM.find(bundle.parentModelId);
            if (pIt != idToIdxKM.end()) exempt.insert(pIt->second);
            for (const std::string& leaf : bundle.leafModelIds) {
              auto lIt = idToIdxKM.find(leaf);
              if (lIt != idToIdxKM.end()) exempt.insert(lIt->second);
            }
            bundleExemptIdx.push_back(std::move(exempt));
          }
        }
        // Overlap count: node-rect overlaps + bundle-bbox overlaps for
        // m1 / m2. Bundle bboxes are obstacles too (per user spec).
        auto localOverlapCount = [&](std::size_t m1, std::size_t m2) {
          auto rectOf = [&](std::size_t i) {
            const NodeRecord& nd = nodes[i];
            Rect r;
            const double cx = attributes.x(nd.handle);
            const double cy = attributes.y(nd.handle);
            r.left = cx - nd.width / 2.0 - kKnotOverlapMargin;
            r.right = cx + nd.width / 2.0 + kKnotOverlapMargin;
            r.top = cy - nd.height / 2.0 - kKnotOverlapMargin;
            r.bottom = cy + nd.height / 2.0 + kKnotOverlapMargin;
            return r;
          };
          std::size_t total = 0;
          if (m1 != m2 && rectsOverlap(rectOf(m1), rectOf(m2))) {
            ++total;
          }
          for (std::size_t target : {m1, m2}) {
            const Rect tr = rectOf(target);
            for (std::size_t k = 0; k < nodes.size(); ++k) {
              if (k == m1 || k == m2) continue;
              if (rectsOverlap(tr, rectOf(k))) ++total;
            }
            // Bundle penalties: m1/m2 should not move INTO any bundle
            // bbox unless they're already absorbed by it.
            for (std::size_t bi = 0; bi < bundleBoxesKM.size(); ++bi) {
              if (bundleExemptIdx[bi].count(target)) continue;
              if (rectsOverlap(tr, bundleBoxesKM[bi])) ++total;
            }
            // Incident-edge bundle penalty: count edges from `target`
            // whose straight-line segment passes through a bundle bbox
            // (excluding bundles target is exempt from). This catches
            // the case where a swap moves the node such that its
            // incident edge now crosses a bundle area.
            for (std::size_t e : edgesByNode[target]) {
              const auto& p = edgePairs[e];
              const std::size_t other = (p.first == target) ? p.second : p.first;
              const RoutePoint pa{
                attributes.x(nodes[target].handle),
                attributes.y(nodes[target].handle)};
              const RoutePoint pb{
                attributes.x(nodes[other].handle),
                attributes.y(nodes[other].handle)};
              for (std::size_t bi = 0; bi < bundleBoxesKM.size(); ++bi) {
                if (bundleExemptIdx[bi].count(target)) continue;
                if (bundleExemptIdx[bi].count(other)) continue;
                if (segmentIntersectsRect(pa, pb, bundleBoxesKM[bi])) ++total;
              }
            }
          }
          return total;
        };
        // Build candidate set of swappable nodes: any non-bundle node
        // that has at least one inter-cluster edge (i.e., participates
        // in the central tangle). Cluster roots are pinned (backbone
        // structure) and bundle members are placed by matrix.
        std::vector<std::size_t> swappable;
        std::unordered_set<std::size_t> rootSetKM;
        for (const auto& kv : clusterByModelIdFull) {
          // We don't have a direct rootSet map; mark nodes that are
          // listed as rootIdx in any cluster. Simpler: mark nodes with
          // very high incidence as roots and skip. But the cleanest
          // marker: a "root" is the cluster's representative — for our
          // purposes, treat anything in clusterByModelIdFull as a
          // member candidate. Backbone roots will still tend to settle
          // because their cluster centroid pulls back.
          (void)kv;
        }
        // Include ALL non-bundle nodes (cluster members AND non-cluster).
        // Earlier filter (only inter-cluster edge owners) limited the
        // pool to 604 candidates; expanding to ~1.1k lets intra-cluster
        // re-arrangements untangle local knots too.
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          if (bundleAbsorbedKM.count(nodes[i].modelId)) continue;
          swappable.push_back(i);
        }
        // Spatial bin for nearby-pair lookup. Only swap nodes within
        // 2× average cluster radius — avoids absurd long-distance moves
        // that would scramble the layout.
        double sumR = 0.0;
        std::size_t cntR = 0;
        for (std::size_t i : swappable) {
          sumR += std::max(nodes[i].width, nodes[i].height);
          ++cntR;
        }
        const double avgNodeDim = cntR > 0 ? sumR / cntR : 100.0;
        // 9 cells. Tested 6→9: -3.8% visual, +43% time. 9→12 marginal,
        // not worth the further slowdown. Sweet spot for this graph.
        const double swapRadius = std::max(800.0, avgNodeDim * 9.0);
        auto pairHashKM = [](const std::pair<long long, long long>& p) {
          return std::hash<long long>()(p.first)
            ^ (std::hash<long long>()(p.second) << 1);
        };
        std::unordered_map<std::pair<long long, long long>,
                            std::vector<std::size_t>, decltype(pairHashKM)>
          binsKM(0, pairHashKM);
        const double cellKM = swapRadius;
        auto binKeyKM = [&](double x, double y) {
          return std::make_pair(
            static_cast<long long>(std::floor(x / cellKM)),
            static_cast<long long>(std::floor(y / cellKM)));
        };
        for (std::size_t i : swappable) {
          binsKM[binKeyKM(attributes.x(nodes[i].handle),
                          attributes.y(nodes[i].handle))].push_back(i);
        }
        // Iter cap raised 6→12 — early termination kicks in once a pass
        // accepts zero swaps, so doubling the cap costs nothing on
        // converged graphs but lets harder convergence keep going.
        const int kKnotMaxIters = skipKnot
          ? 0
          : static_cast<int>(std::round(readDoubleEnv(
              "DJERD_KNOT_GREEDY_ROUNDS", 12.0, 0.0, 100.0)));
        std::size_t totalAccepted = 0;
        for (int iter = 0; iter < kKnotMaxIters && !swapExpired(); ++iter) {
          std::size_t accepted = 0;
          // Rebuild bins each iter (positions changed).
          if (iter > 0) {
            binsKM.clear();
            for (std::size_t i : swappable) {
              binsKM[binKeyKM(attributes.x(nodes[i].handle),
                              attributes.y(nodes[i].handle))].push_back(i);
            }
          }
          for (std::size_t m1 : swappable) {
            if (swapExpired()) break;
            const auto k = binKeyKM(attributes.x(nodes[m1].handle),
                                     attributes.y(nodes[m1].handle));
            for (long long dx = -1; dx <= 1 && !swapBudgetHit; ++dx) {
              for (long long dy = -1; dy <= 1 && !swapBudgetHit; ++dy) {
                auto bIt = binsKM.find({k.first + dx, k.second + dy});
                if (bIt == binsKM.end()) continue;
                for (std::size_t m2 : bIt->second) {
                  if (m2 <= m1) continue;
                  // Stop between complete candidates so tentative swaps have
                  // already been accepted or fully reverted.
                  if (swapExpired()) break;
                  const std::size_t beforeC = localCrossCount(m1, m2);
                  const std::size_t beforeO = localOverlapCount(m1, m2);
                  const double x1 = attributes.x(nodes[m1].handle);
                  const double y1 = attributes.y(nodes[m1].handle);
                  attributes.x(nodes[m1].handle) = attributes.x(nodes[m2].handle);
                  attributes.y(nodes[m1].handle) = attributes.y(nodes[m2].handle);
                  attributes.x(nodes[m2].handle) = x1;
                  attributes.y(nodes[m2].handle) = y1;
                  const std::size_t afterC = localCrossCount(m1, m2);
                  const std::size_t afterO = localOverlapCount(m1, m2);
                  if (afterC + afterO < beforeC + beforeO
                      && afterO <= beforeO) {
                    ++accepted;
                  } else {
                    attributes.x(nodes[m2].handle) = attributes.x(nodes[m1].handle);
                    attributes.y(nodes[m2].handle) = attributes.y(nodes[m1].handle);
                    attributes.x(nodes[m1].handle) = x1;
                    attributes.y(nodes[m1].handle) = y1;
                  }
                }
              }
            }
          }
          totalAccepted += accepted;
          if (accepted == 0) break;
        }
        if (totalAccepted > 0) {
          std::fprintf(stderr,
            "[knot-min] Accepted %zu spatial swaps among %zu candidates.\n",
            totalAccepted, swappable.size());
        }

        // Simulated Annealing pass — DISABLED. Tested 8000 attempts
        // with T=8 (28% accept rate, 3,278 → 3,568 visualCross worsen
        // by +9%) and T=1.5 (1,258 accept, 3,278 → 3,345 worsen by
        // +2%). SA random spatial swaps trade local cross reductions
        // for global new crosses that aren't captured by localCrossCount
        // (which only sees edges incident to m1/m2). Set
        // DJERD_KNOT_SA=1 to enable for experimentation.
        const char* knotSaEnv = std::getenv("DJERD_KNOT_SA");
        if (!skipKnot && knotSaEnv && std::strcmp(knotSaEnv, "0") != 0
            && !swapExpired()) {
          std::mt19937 rng(0xC0FFEEu);
          std::uniform_int_distribution<std::size_t> distIdx(
            0, swappable.size() ? swappable.size() - 1 : 0);
          std::uniform_real_distribution<double> distR(0.0, 1.0);
          // Rebuild bins (positions changed in greedy pass).
          binsKM.clear();
          for (std::size_t i : swappable) {
            binsKM[binKeyKM(attributes.x(nodes[i].handle),
                            attributes.y(nodes[i].handle))].push_back(i);
          }
          constexpr int kSaAttempts = 5000;
          double T = 1.5;  // low: only small uphill (ΔE=1-2) kicks in
          const double decay = std::pow(0.001 / 1.5, 1.0 / kSaAttempts);
          std::size_t saAccepted = 0;
          std::size_t saUphill = 0;
          for (int k = 0; k < kSaAttempts && swappable.size() >= 2
               && !swapExpired(); ++k) {
            const std::size_t saI = distIdx(rng);
            const std::size_t m1 = swappable[saI];
            // Pick m2 from m1's spatial bin ring.
            const auto kk = binKeyKM(attributes.x(nodes[m1].handle),
                                      attributes.y(nodes[m1].handle));
            std::vector<std::size_t> ring;
            for (long long dx = -1; dx <= 1; ++dx) {
              for (long long dy = -1; dy <= 1; ++dy) {
                auto bIt = binsKM.find({kk.first + dx, kk.second + dy});
                if (bIt == binsKM.end()) continue;
                for (std::size_t r : bIt->second) {
                  if (r != m1) ring.push_back(r);
                }
              }
            }
            if (ring.empty()) continue;
            std::uniform_int_distribution<std::size_t> distRing(
              0, ring.size() - 1);
            const std::size_t m2 = ring[distRing(rng)];
            const std::size_t beforeC = localCrossCount(m1, m2);
            const std::size_t beforeO = localOverlapCount(m1, m2);
            const double x1 = attributes.x(nodes[m1].handle);
            const double y1 = attributes.y(nodes[m1].handle);
            attributes.x(nodes[m1].handle) = attributes.x(nodes[m2].handle);
            attributes.y(nodes[m1].handle) = attributes.y(nodes[m2].handle);
            attributes.x(nodes[m2].handle) = x1;
            attributes.y(nodes[m2].handle) = y1;
            const std::size_t afterC = localCrossCount(m1, m2);
            const std::size_t afterO = localOverlapCount(m1, m2);
            const long long dE =
              static_cast<long long>(afterC + afterO)
              - static_cast<long long>(beforeC + beforeO);
            // Hard reject on overlap regression OR pure improvement.
            bool accept = false;
            if (afterO > beforeO) {
              accept = false;
            } else if (dE <= 0) {
              accept = true;
            } else if (T > 1e-3) {
              const double prob = std::exp(-static_cast<double>(dE) / T);
              if (distR(rng) < prob) {
                accept = true;
                ++saUphill;
              }
            }
            if (accept) {
              ++saAccepted;
            } else {
              attributes.x(nodes[m2].handle) = attributes.x(nodes[m1].handle);
              attributes.y(nodes[m2].handle) = attributes.y(nodes[m1].handle);
              attributes.x(nodes[m1].handle) = x1;
              attributes.y(nodes[m1].handle) = y1;
            }
            T *= decay;
          }
          if (saAccepted > 0) {
            std::fprintf(stderr,
              "[knot-min-sa] Accepted %zu swaps (%zu uphill) of %d attempts.\n",
              saAccepted, saUphill, kSaAttempts);
          }
        }

        // 2-opt cross-targeted pass: directly target each remaining
        // segment-segment crossing. For each crossing pair (e1, e2),
        // try swapping the swappable endpoint of e1 with the
        // swappable endpoint of e2 — this often unties the crossing
        // in one step. Spatial knot-min picks pairs by proximity, but
        // some untangle-able crosses involve nodes that aren't
        // spatially adjacent (long-range edges over the layout).
        std::unordered_set<std::size_t> swappableSet(
          swappable.begin(), swappable.end());
        std::size_t twoOptAccepted = 0;
        const int knotTwoOptRounds = skipKnot
          ? 0
          : static_cast<int>(std::round(readDoubleEnv(
              "DJERD_KNOT_TWO_OPT_ROUNDS", 8.0, 0.0, 100.0)));
        for (int outerIter = 0; outerIter < knotTwoOptRounds
             && !swapExpired(); ++outerIter) {
          // Find all current crossings.
          std::vector<std::pair<std::size_t, std::size_t>> crossings;
          for (std::size_t i = 0; i < edges.size(); ++i) {
            for (std::size_t j = i + 1; j < edges.size(); ++j) {
              if (segmentsCross(i, j)) crossings.emplace_back(i, j);
            }
          }
          if (crossings.empty()) break;
          std::size_t innerAccepted = 0;
          for (const auto& [e1, e2] : crossings) {
            if (swapExpired()) break;
            if (!segmentsCross(e1, e2)) continue;  // already resolved
            const auto& p1 = edgePairs[e1];
            const auto& p2 = edgePairs[e2];
            // Try each pair of swappable endpoints — one from e1, one
            // from e2. Accept first swap that reduces visualConflict.
            const std::array<std::pair<std::size_t, std::size_t>, 4>
              candidates = {{
                {p1.first, p2.first},
                {p1.first, p2.second},
                {p1.second, p2.first},
                {p1.second, p2.second},
              }};
            bool resolved = false;
            for (const auto& [m1, m2] : candidates) {
              if (m1 == m2) continue;
              if (!swappableSet.count(m1) || !swappableSet.count(m2)) continue;
              if (swapExpired()) break;
              const std::size_t beforeC = localCrossCount(m1, m2);
              const std::size_t beforeO = localOverlapCount(m1, m2);
              const double x1 = attributes.x(nodes[m1].handle);
              const double y1 = attributes.y(nodes[m1].handle);
              attributes.x(nodes[m1].handle) = attributes.x(nodes[m2].handle);
              attributes.y(nodes[m1].handle) = attributes.y(nodes[m2].handle);
              attributes.x(nodes[m2].handle) = x1;
              attributes.y(nodes[m2].handle) = y1;
              const std::size_t afterC = localCrossCount(m1, m2);
              const std::size_t afterO = localOverlapCount(m1, m2);
              if (afterC + afterO < beforeC + beforeO
                  && afterO <= beforeO) {
                ++innerAccepted;
                resolved = true;
                break;
              } else {
                attributes.x(nodes[m2].handle) = attributes.x(nodes[m1].handle);
                attributes.y(nodes[m2].handle) = attributes.y(nodes[m1].handle);
                attributes.x(nodes[m1].handle) = x1;
                attributes.y(nodes[m1].handle) = y1;
              }
            }
            (void)resolved;
          }
          twoOptAccepted += innerAccepted;
          if (innerAccepted == 0) break;
        }
        if (twoOptAccepted > 0) {
          std::fprintf(stderr,
            "[knot-min-2opt] Accepted %zu cross-targeted swaps.\n",
            twoOptAccepted);
        }

        // Collision-safe node relocation. Swapping nodes can only permute the
        // slots chosen by the original force layout; it cannot use the large
        // empty regions beside a node's actual neighbours. For each hot
        // non-bundle node, sample compact rings around its neighbours and
        // accept a move only when the exact changed part of the straight-line
        // scene improves while edge/node, bundle/edge, node-spacing, and
        // bundle/node collisions are individually non-regressing.
        if (runKnotRelocate && !swappable.empty()) {
          struct LocalSceneCost {
            std::size_t crossings = 0;
            std::size_t edgeNode = 0;
            std::size_t bundleEdge = 0;
            std::size_t nodeSpacing = 0;
            std::size_t bundleNode = 0;

            std::size_t total() const {
              return crossings + edgeNode + bundleEdge
                + nodeSpacing + bundleNode;
            }
          };

          const double relocateMargin = readDoubleEnv(
            "DJERD_KNOT_RELOCATE_MARGIN", 8.0, 0.0, 240.0);
          std::vector<std::size_t> relocatableNodes = swappable;
          std::unordered_set<std::size_t> relocatableSet(
            relocatableNodes.begin(), relocatableNodes.end());
          std::unordered_set<std::string> bundleLeafIds;
          for (const LeafBundleRecord& bundle : metadata.leafBundles) {
            for (const std::string& leaf : bundle.leafModelIds) {
              bundleLeafIds.insert(leaf);
            }
          }
          // Bundle parents are safe to move independently: the leaf matrix
          // bbox remains fixed and only its straight anchor-to-parent segment
          // changes. Do not add a parent that is itself a leaf of another
          // bundle, because that would require resizing that other matrix.
          for (const LeafBundleRecord& bundle : metadata.leafBundles) {
            if (bundleLeafIds.count(bundle.parentModelId)) continue;
            auto parentIt = idToIdxKM.find(bundle.parentModelId);
            if (parentIt == idToIdxKM.end()) continue;
            if (relocatableSet.insert(parentIt->second).second) {
              relocatableNodes.push_back(parentIt->second);
            }
          }
          const int relocateRounds = static_cast<int>(std::round(readDoubleEnv(
            "DJERD_KNOT_RELOCATE_ROUNDS", 2.0, 1.0, 16.0)));
          const std::size_t relocateTop = static_cast<std::size_t>(std::round(
            readDoubleEnv(
              "DJERD_KNOT_RELOCATE_TOP", 400.0, 1.0,
              static_cast<double>(relocatableNodes.size()))));
          const int angleSamples = static_cast<int>(std::round(readDoubleEnv(
            "DJERD_KNOT_RELOCATE_ANGLES", 16.0, 4.0, 64.0)));
          const double budgetMs = readDoubleEnv(
            "DJERD_KNOT_RELOCATE_BUDGET_MS", 12000.0, 100.0, 300000.0);
          const bool disableWallClockBudgets = readBoolEnv(
            "DJERD_DISABLE_WALL_CLOCK_BUDGETS", false);
          const auto relocateStarted = std::chrono::steady_clock::now();
          bool relocateBudgetHit = false;
          auto relocateExpired = [&]() {
            if (disableWallClockBudgets) return false;
            const double elapsed = std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - relocateStarted).count();
            relocateBudgetHit = elapsed >= budgetMs;
            return relocateBudgetHit;
          };
          const Rect settledRelocateBounds = graphNodeBounds(nodes, attributes);

          auto rectForNode = [&](std::size_t index) {
            Rect rect;
            const double x = attributes.x(nodes[index].handle);
            const double y = attributes.y(nodes[index].handle);
            rect.left = x - nodes[index].width * 0.5 - relocateMargin;
            rect.right = x + nodes[index].width * 0.5 + relocateMargin;
            rect.top = y - nodes[index].height * 0.5 - relocateMargin;
            rect.bottom = y + nodes[index].height * 0.5 + relocateMargin;
            return rect;
          };
          auto rawRectForNode = [&](std::size_t index) {
            Rect rect;
            const double x = attributes.x(nodes[index].handle);
            const double y = attributes.y(nodes[index].handle);
            rect.left = x - nodes[index].width * 0.5;
            rect.right = x + nodes[index].width * 0.5;
            rect.top = y - nodes[index].height * 0.5;
            rect.bottom = y + nodes[index].height * 0.5;
            return rect;
          };
          auto candidatePreservesDirectScene = [&](std::size_t target) {
            const Rect candidateRect = rawRectForNode(target);
            if (
                candidateRect.left < settledRelocateBounds.left - 1e-6
                || candidateRect.right > settledRelocateBounds.right + 1e-6
                || candidateRect.top < settledRelocateBounds.top - 1e-6
                || candidateRect.bottom > settledRelocateBounds.bottom + 1e-6) {
              return false;
            }
            for (std::size_t other = 0; other < nodes.size(); ++other) {
              if (other == target) continue;
              if (rectsOverlap(candidateRect, rawRectForNode(other))) {
                return false;
              }
            }
            return true;
          };

          auto localSceneCost = [&](std::size_t target) {
            LocalSceneCost cost;
            cost.crossings = localCrossCount(target, target);
            const Rect targetRect = rectForNode(target);
            for (std::size_t other = 0; other < nodes.size(); ++other) {
              if (other == target) continue;
              if (rectsOverlap(targetRect, rectForNode(other))) {
                ++cost.nodeSpacing;
              }
            }
            for (std::size_t bi = 0; bi < bundleBoxesKM.size(); ++bi) {
              if (bundleExemptIdx[bi].count(target)) continue;
              if (rectsOverlap(targetRect, bundleBoxesKM[bi])) {
                ++cost.bundleNode;
              }
            }

            for (std::size_t edgeIndex = 0;
                 edgeIndex < edgePairs.size(); ++edgeIndex) {
              const auto& endpoints = edgePairs[edgeIndex];
              const bool incident = endpoints.first == target
                || endpoints.second == target;
              const RoutePoint a{
                attributes.x(nodes[endpoints.first].handle),
                attributes.y(nodes[endpoints.first].handle),
              };
              const RoutePoint b{
                attributes.x(nodes[endpoints.second].handle),
                attributes.y(nodes[endpoints.second].handle),
              };
              if (!incident) {
                if (segmentIntersectsRect(a, b, targetRect)) {
                  ++cost.edgeNode;
                }
                continue;
              }

              for (std::size_t blocker = 0; blocker < nodes.size(); ++blocker) {
                if (blocker == endpoints.first || blocker == endpoints.second) {
                  continue;
                }
                if (segmentIntersectsRect(a, b, rectForNode(blocker))) {
                  ++cost.edgeNode;
                }
              }
              for (std::size_t bi = 0; bi < bundleBoxesKM.size(); ++bi) {
                if (bundleExemptIdx[bi].count(endpoints.first)
                    || bundleExemptIdx[bi].count(endpoints.second)) {
                  continue;
                }
                if (segmentIntersectsRect(a, b, bundleBoxesKM[bi])) {
                  ++cost.bundleEdge;
                }
              }
            }
            return cost;
          };

          std::size_t acceptedRelocations = 0;
          std::int64_t crossingGain = 0;
          std::size_t collisionGain = 0;
          for (int round = 0;
               round < relocateRounds && !relocateExpired(); ++round) {
            std::vector<std::pair<std::size_t, std::size_t>> ranked;
            ranked.reserve(relocatableNodes.size());
            for (std::size_t nodeIndex : relocatableNodes) {
              const LocalSceneCost cost = localSceneCost(nodeIndex);
              if (cost.total() == 0) continue;
              ranked.emplace_back(cost.total(), nodeIndex);
              if (relocateExpired()) break;
            }
            std::sort(ranked.begin(), ranked.end(),
              [](const auto& left, const auto& right) {
                return left.first > right.first;
              });
            if (ranked.size() > relocateTop) ranked.resize(relocateTop);

            std::size_t acceptedThisRound = 0;
            for (const auto& rankedNode : ranked) {
              if (relocateExpired()) break;
              const std::size_t target = rankedNode.second;
              if (edgesByNode[target].empty()) continue;
              const double originalX = attributes.x(nodes[target].handle);
              const double originalY = attributes.y(nodes[target].handle);
              const LocalSceneCost before = localSceneCost(target);
              LocalSceneCost best = before;
              double bestX = originalX;
              double bestY = originalY;

              std::vector<std::size_t> neighbours;
              neighbours.reserve(edgesByNode[target].size());
              for (std::size_t edgeIndex : edgesByNode[target]) {
                const auto& endpoints = edgePairs[edgeIndex];
                const std::size_t other = endpoints.first == target
                  ? endpoints.second : endpoints.first;
                if (other != target
                    && std::find(neighbours.begin(), neighbours.end(), other)
                      == neighbours.end()) {
                  neighbours.push_back(other);
                }
              }
              if (neighbours.empty()) continue;

              double centroidX = 0.0;
              double centroidY = 0.0;
              for (std::size_t neighbour : neighbours) {
                centroidX += attributes.x(nodes[neighbour].handle);
                centroidY += attributes.y(nodes[neighbour].handle);
              }
              centroidX /= static_cast<double>(neighbours.size());
              centroidY /= static_cast<double>(neighbours.size());

              std::vector<double> neighbourXs;
              std::vector<double> neighbourYs;
              neighbourXs.reserve(neighbours.size());
              neighbourYs.reserve(neighbours.size());
              double neighbourSpread2 = 0.0;
              for (std::size_t neighbour : neighbours) {
                const double x = attributes.x(nodes[neighbour].handle);
                const double y = attributes.y(nodes[neighbour].handle);
                neighbourXs.push_back(x);
                neighbourYs.push_back(y);
                const double dx = x - centroidX;
                const double dy = y - centroidY;
                neighbourSpread2 += dx * dx + dy * dy;
              }
              std::sort(neighbourXs.begin(), neighbourXs.end());
              std::sort(neighbourYs.begin(), neighbourYs.end());
              const double medianX = neighbourXs[neighbourXs.size() / 2];
              const double medianY = neighbourYs[neighbourYs.size() / 2];
              const double neighbourSpread = std::sqrt(
                neighbourSpread2 / static_cast<double>(neighbours.size()));

              std::vector<std::pair<double, double>> candidates;
              candidates.reserve(
                neighbours.size() * static_cast<std::size_t>(angleSamples) * 3 + 8);
              candidates.emplace_back(centroidX, centroidY);
              candidates.emplace_back(medianX, medianY);
              for (double blend : {0.25, 0.5, 0.75}) {
                candidates.emplace_back(
                  originalX + (centroidX - originalX) * blend,
                  originalY + (centroidY - originalY) * blend);
              }
              if (neighbours.size() >= 5 && neighbourSpread > 1.0) {
                for (double centerBlend : {0.15, 0.3, 0.5}) {
                  const double radius = neighbourSpread * centerBlend;
                  for (int angleIndex = 0;
                       angleIndex < angleSamples; ++angleIndex) {
                    const double angle = 2.0 * 3.14159265358979323846
                      * static_cast<double>(angleIndex)
                      / static_cast<double>(angleSamples);
                    candidates.emplace_back(
                      centroidX + std::cos(angle) * radius,
                      centroidY + std::sin(angle) * radius);
                    candidates.emplace_back(
                      medianX + std::cos(angle) * radius,
                      medianY + std::sin(angle) * radius);
                  }
                }
              }
              const std::size_t ringNeighbourCount =
                std::min<std::size_t>(12, neighbours.size());
              for (std::size_t neighbourOffset = 0;
                   neighbourOffset < ringNeighbourCount; ++neighbourOffset) {
                const std::size_t neighbour = neighbours[neighbourOffset];
                const double nx = attributes.x(nodes[neighbour].handle);
                const double ny = attributes.y(nodes[neighbour].handle);
                const double halfW = 0.5
                  * (nodes[target].width + nodes[neighbour].width)
                  + relocateMargin + 20.0;
                const double halfH = 0.5
                  * (nodes[target].height + nodes[neighbour].height)
                  + relocateMargin + 20.0;
                const double baseRadius = std::hypot(halfW, halfH);
                for (double ring : {1.0, 1.8, 3.2, 5.5, 9.0, 14.0}) {
                  const double radius = baseRadius * ring;
                  for (int angleIndex = 0;
                       angleIndex < angleSamples; ++angleIndex) {
                    const double angle = 2.0 * 3.14159265358979323846
                      * static_cast<double>(angleIndex)
                      / static_cast<double>(angleSamples);
                    candidates.emplace_back(
                      nx + std::cos(angle) * radius,
                      ny + std::sin(angle) * radius);
                  }
                }
              }

              // Crossing-driven candidates: if target--neighbour crosses an
              // unrelated segment C--D, move target just across the infinite
              // line C--D onto the same side as neighbour. This is the
              // minimum perpendicular displacement that removes that exact
              // crossing; several clearance samples let the collision audit
              // choose a genuinely empty landing point.
              const std::size_t maxUntangleCandidates =
                static_cast<std::size_t>(std::round(readDoubleEnv(
                  "DJERD_KNOT_RELOCATE_UNTANGLE_TOP", 96.0, 0.0, 1000.0)));
              std::size_t untangleCount = 0;
              for (std::size_t incidentEdge : edgesByNode[target]) {
                if (untangleCount >= maxUntangleCandidates) break;
                const auto& incidentEndpoints = edgePairs[incidentEdge];
                const std::size_t neighbour =
                  incidentEndpoints.first == target
                    ? incidentEndpoints.second : incidentEndpoints.first;
                const double neighbourX = attributes.x(nodes[neighbour].handle);
                const double neighbourY = attributes.y(nodes[neighbour].handle);
                for (std::size_t otherEdge = 0;
                     otherEdge < edgePairs.size(); ++otherEdge) {
                  if (untangleCount >= maxUntangleCandidates) break;
                  if (otherEdge == incidentEdge
                      || !segmentsCross(incidentEdge, otherEdge)) {
                    continue;
                  }
                  const auto& opposing = edgePairs[otherEdge];
                  const double cx = attributes.x(nodes[opposing.first].handle);
                  const double cy = attributes.y(nodes[opposing.first].handle);
                  const double dx = attributes.x(nodes[opposing.second].handle);
                  const double dy = attributes.y(nodes[opposing.second].handle);
                  const double lineX = dx - cx;
                  const double lineY = dy - cy;
                  const double length = std::hypot(lineX, lineY);
                  if (length < 1e-6) continue;
                  const double normalX = -lineY / length;
                  const double normalY = lineX / length;
                  const double neighbourSide =
                    (neighbourX - cx) * normalX
                    + (neighbourY - cy) * normalY;
                  const double side = neighbourSide >= 0.0 ? 1.0 : -1.0;
                  const double targetSide =
                    (originalX - cx) * normalX
                    + (originalY - cy) * normalY;
                  const double projectionX = originalX - targetSide * normalX;
                  const double projectionY = originalY - targetSide * normalY;
                  const double nodeClearance = std::max(
                    24.0,
                    0.25 * std::hypot(
                      nodes[target].width, nodes[target].height));
                  for (double gapScale : {1.0, 2.0, 4.0, 8.0}) {
                    const double gap = nodeClearance * gapScale;
                    candidates.emplace_back(
                      projectionX + normalX * side * gap,
                      projectionY + normalY * side * gap);
                  }
                  ++untangleCount;
                }
              }

              std::unordered_set<std::pair<long long, long long>,
                                 decltype(pairHashKM)>
                testedCandidates(0, pairHashKM);
              for (const auto& candidate : candidates) {
                if (relocateExpired()) break;
                if (std::hypot(candidate.first - originalX,
                               candidate.second - originalY) < 1.0) {
                  continue;
                }
                const auto candidateKey = std::make_pair(
                  static_cast<long long>(std::llround(candidate.first * 10.0)),
                  static_cast<long long>(std::llround(candidate.second * 10.0)));
                if (!testedCandidates.insert(candidateKey).second) continue;
                attributes.x(nodes[target].handle) = candidate.first;
                attributes.y(nodes[target].handle) = candidate.second;
                if (directScene && !candidatePreservesDirectScene(target)) {
                  continue;
                }
                const LocalSceneCost after = localSceneCost(target);
                const bool collisionSafe =
                  after.edgeNode <= before.edgeNode
                  && after.bundleEdge <= before.bundleEdge
                  && after.nodeSpacing <= before.nodeSpacing
                  && after.bundleNode <= before.bundleNode;
                const bool better = collisionSafe
                  && after.total() < best.total();
                if (better) {
                  best = after;
                  bestX = candidate.first;
                  bestY = candidate.second;
                }
              }

              attributes.x(nodes[target].handle) = bestX;
              attributes.y(nodes[target].handle) = bestY;
              if (bestX != originalX || bestY != originalY) {
                for (LeafBundleRecord& bundle : metadata.leafBundles) {
                  if (bundle.parentModelId != nodes[target].modelId) continue;
                  double leafX = 0.0;
                  double leafY = 0.0;
                  std::size_t leafCount = 0;
                  for (const std::string& leafId : bundle.leafModelIds) {
                    auto leafIt = idToIdxKM.find(leafId);
                    if (leafIt == idToIdxKM.end()) continue;
                    leafX += attributes.x(nodes[leafIt->second].handle);
                    leafY += attributes.y(nodes[leafIt->second].handle);
                    ++leafCount;
                  }
                  if (leafCount == 0) continue;
                  leafX /= static_cast<double>(leafCount);
                  leafY /= static_cast<double>(leafCount);
                  bundle.anchorX = 0.5 * (bestX + leafX);
                  bundle.anchorY = 0.5 * (bestY + leafY);
                }
                ++acceptedRelocations;
                ++acceptedThisRound;
                crossingGain += static_cast<std::int64_t>(before.crossings)
                  - static_cast<std::int64_t>(best.crossings);
                collisionGain +=
                  (before.edgeNode + before.bundleEdge
                    + before.nodeSpacing + before.bundleNode)
                  - (best.edgeNode + best.bundleEdge
                    + best.nodeSpacing + best.bundleNode);
              }
            }
            if (acceptedThisRound == 0) break;
          }

          std::size_t verifiedCrossings = 0;
          for (std::size_t left = 0; left < edges.size(); ++left) {
            for (std::size_t right = left + 1; right < edges.size(); ++right) {
              if (segmentsCross(left, right)) ++verifiedCrossings;
            }
          }
          if (disableWallClockBudgets) {
            std::fprintf(stderr,
              "[knot-relocate] accepted=%zu crossingGain=%lld "
              "collisionGain=%zu verifiedCrossings=%zu budget=unlimited.\n",
              acceptedRelocations,
              static_cast<long long>(crossingGain),
              collisionGain,
              verifiedCrossings);
          } else {
            std::fprintf(stderr,
              "[knot-relocate] accepted=%zu crossingGain=%lld "
              "collisionGain=%zu verifiedCrossings=%zu budgetMs=%.0f "
              "budgetHit=%d.\n",
              acceptedRelocations,
              static_cast<long long>(crossingGain),
              collisionGain,
              verifiedCrossings,
              budgetMs,
              relocateBudgetHit ? 1 : 0);
          }
        }

        // Second-pass spatial knot-min after 2-opt — disabled: 16
        // additional swaps for -20 visualCross at +5s time cost. ROI
        // too low. Set DJERD_KNOT_2NDPASS=1 to enable.
        const char* knot2ndEnv = std::getenv("DJERD_KNOT_2NDPASS");
        const bool runKnot2nd =
          !skipKnot && knot2ndEnv && std::strcmp(knot2ndEnv, "0") != 0;
        std::size_t totalAccepted2 = 0;
        for (int iter = 0; runKnot2nd && iter < kKnotMaxIters
             && !swapExpired(); ++iter) {
          std::size_t accepted = 0;
          binsKM.clear();
          for (std::size_t i : swappable) {
            binsKM[binKeyKM(attributes.x(nodes[i].handle),
                            attributes.y(nodes[i].handle))].push_back(i);
          }
          for (std::size_t m1 : swappable) {
            if (swapExpired()) break;
            const auto k = binKeyKM(attributes.x(nodes[m1].handle),
                                     attributes.y(nodes[m1].handle));
            for (long long dx = -1; dx <= 1 && !swapBudgetHit; ++dx) {
              for (long long dy = -1; dy <= 1 && !swapBudgetHit; ++dy) {
                auto bIt = binsKM.find({k.first + dx, k.second + dy});
                if (bIt == binsKM.end()) continue;
                for (std::size_t m2 : bIt->second) {
                  if (m2 <= m1) continue;
                  if (swapExpired()) break;
                  const std::size_t beforeC = localCrossCount(m1, m2);
                  const std::size_t beforeO = localOverlapCount(m1, m2);
                  const double x1 = attributes.x(nodes[m1].handle);
                  const double y1 = attributes.y(nodes[m1].handle);
                  attributes.x(nodes[m1].handle) = attributes.x(nodes[m2].handle);
                  attributes.y(nodes[m1].handle) = attributes.y(nodes[m2].handle);
                  attributes.x(nodes[m2].handle) = x1;
                  attributes.y(nodes[m2].handle) = y1;
                  const std::size_t afterC = localCrossCount(m1, m2);
                  const std::size_t afterO = localOverlapCount(m1, m2);
                  if (afterC + afterO < beforeC + beforeO
                      && afterO <= beforeO) {
                    ++accepted;
                  } else {
                    attributes.x(nodes[m2].handle) = attributes.x(nodes[m1].handle);
                    attributes.y(nodes[m2].handle) = attributes.y(nodes[m1].handle);
                    attributes.x(nodes[m1].handle) = x1;
                    attributes.y(nodes[m1].handle) = y1;
                  }
                }
              }
            }
          }
          totalAccepted2 += accepted;
          if (accepted == 0) break;
        }
        if (totalAccepted2 > 0) {
          std::fprintf(stderr,
            "[knot-min] Second pass: accepted %zu additional spatial swaps.\n",
            totalAccepted2);
        }
        if (!skipKnot) {
          std::fprintf(stderr, "[knot-min] budgetMs=%.0f budgetHit=%d.\n",
            swapBudgetMs, swapBudgetHit ? 1 : 0);
        }
      }
}

}  // namespace djerd
