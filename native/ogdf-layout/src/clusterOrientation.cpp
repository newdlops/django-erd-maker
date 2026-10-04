#include "clusterCrossingOptimization.h"
#include "clusterGraph.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <unordered_map>

namespace djerd {
void orientClusterGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<std::vector<std::size_t>>& adj,
  const ClusterGraphResult& result, ogdf::GraphAttributes& attributes,
  bool skipCgPositioning) {
    const char* orientEnv = std::getenv("DJERD_CLUSTER_ORIENT");
    // Skip when positions are externally supplied (--positions-tsv: ML reroute /
    // bbox-target / polish round-trips). Rotating a cluster there would discard
    // the very positions those stages computed. Orientation only runs on the
    // baseline / multistart layouts that keep their result.
    if (orientEnv && std::strcmp(orientEnv, "0") != 0 && !skipCgPositioning) {
      // Node → owning cluster root (members + transitively-attached pruned
      // subtree), mirroring §13's clusterOwned construction.
      std::unordered_map<std::size_t, std::size_t> nodeToCRoot;
      for (const ClusterRecord& c : result.clusters)
        for (const ClusterMemberInfo& m : c.members)
          nodeToCRoot[m.nodeIdx] = c.rootIdx;
      std::unordered_map<std::size_t, std::size_t> immParentO;
      for (const PrunedNodeRecord& p : result.prunedNodes)
        immParentO[p.nodeIdx] = p.parentIdx;
      auto coreAnchorO = [&](std::size_t v) {
        int hops = 0;
        while (hops++ < 200) {
          auto it = immParentO.find(v);
          if (it == immParentO.end() || it->second == v) break;
          v = it->second;
        }
        return v;
      };
      for (const PrunedNodeRecord& p : result.prunedNodes) {
        if (p.isAloneRoot || nodeToCRoot.count(p.nodeIdx)) continue;
        auto it = nodeToCRoot.find(coreAnchorO(p.nodeIdx));
        if (it != nodeToCRoot.end()) nodeToCRoot[p.nodeIdx] = it->second;
      }
      std::unordered_map<std::size_t, std::vector<std::size_t>> clusterOwnedO;
      for (const auto& kv : nodeToCRoot) clusterOwnedO[kv.second].push_back(kv.first);

      std::vector<std::pair<std::size_t, std::size_t>> dedupEdgesO;
      for (std::size_t i = 0; i < adj.size(); ++i)
        for (std::size_t j : adj[i]) if (j > i) dedupEdgesO.emplace_back(i, j);

      auto segCrossO = [](double ax, double ay, double bx, double by,
                          double cx, double cy, double dx, double dy) -> bool {
        auto ccw = [](double Ax, double Ay, double Bx, double By, double Cx, double Cy) {
          return (Cy - Ay) * (Bx - Ax) - (By - Ay) * (Cx - Ax);
        };
        const double d1 = ccw(cx, cy, dx, dy, ax, ay), d2 = ccw(cx, cy, dx, dy, bx, by);
        const double d3 = ccw(ax, ay, bx, by, cx, cy), d4 = ccw(ax, ay, bx, by, dx, dy);
        return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
               ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
      };
      auto globalCross = [&]() -> std::size_t {
        std::size_t t = 0;
        for (std::size_t i = 0; i < dedupEdgesO.size(); ++i) {
          const std::size_t a = dedupEdgesO[i].first, b = dedupEdgesO[i].second;
          const double ax = attributes.x(nodes[a].handle), ay = attributes.y(nodes[a].handle);
          const double bx = attributes.x(nodes[b].handle), by = attributes.y(nodes[b].handle);
          for (std::size_t j = i + 1; j < dedupEdgesO.size(); ++j) {
            const std::size_t c = dedupEdgesO[j].first, d = dedupEdgesO[j].second;
            if (a == c || a == d || b == c || b == d) continue;
            if (segCrossO(ax, ay, bx, by,
                          attributes.x(nodes[c].handle), attributes.y(nodes[c].handle),
                          attributes.x(nodes[d].handle), attributes.y(nodes[d].handle))) ++t;
          }
        }
        return t;
      };

      const std::size_t beforeOrient = globalCross();
      std::vector<std::pair<double, double>> snapO(nodes.size());
      for (std::size_t i = 0; i < nodes.size(); ++i)
        snapO[i] = {attributes.x(nodes[i].handle), attributes.y(nodes[i].handle)};

      const double kPi = 3.14159265358979323846;
      int orientedClusters = 0;
      // Deterministic cluster order (root index ascending).
      std::vector<std::size_t> roots;
      roots.reserve(clusterOwnedO.size());
      for (const auto& kv : clusterOwnedO) roots.push_back(kv.first);
      std::sort(roots.begin(), roots.end());

      // Multiple greedy rounds: a cluster oriented in round N sees the updated
      // positions of its neighbours in round N+1, so re-orienting converges to
      // a better mutual arrangement. Stop when a round changes nothing or stops
      // improving the global straight-line crossing count.
      int orientRounds = 4;
      if (const char* rr = std::getenv("DJERD_CLUSTER_ORIENT_ROUNDS")) {
        const int v = std::atoi(rr);
        if (v >= 1) orientRounds = v;
      }
      // Candidate angle granularity. 90° multiples (kStep=3 over 30° units)
      // rotate a cluster onto an axis-aligned box of the SAME area (width↔
      // height swap), so the layout's bounding box barely grows — vs 30° steps
      // which tilt clusters and inflate the bbox. QUARTER=1 trades some crossing
      // freedom for bbox preservation.
      const int kStep = [] {
        const char* q = std::getenv("DJERD_CLUSTER_ORIENT_QUARTER");
        return (q && std::strcmp(q, "0") != 0) ? 3 : 1;
      }();
      // Outer loop: alternate rotate↔swap. After cluster-swap moves cluster
      // POSITIONS, re-running rotation finds better ORIENTATIONS at the new
      // positions (and vice versa) — mutual refinement. DJERD_ORIENT_OUTER.
      const int orientOuter = [] {
        const char* o = std::getenv("DJERD_ORIENT_OUTER");
        return o ? std::max(1, std::atoi(o)) : 1;
      }();
      for (int outer = 0; outer < orientOuter; ++outer) {
      std::size_t prevRoundCross = globalCross();
      for (int rd = 0; rd < orientRounds; ++rd) {
      int roundOriented = 0;
      for (std::size_t root : roots) {
        const std::vector<std::size_t>& owned = clusterOwnedO[root];
        if (owned.size() < 3) continue;
        std::unordered_set<std::size_t> ownedSet(owned.begin(), owned.end());
        // Inter-cluster edges incident to this cluster: (m internal, x external).
        std::vector<std::pair<std::size_t, std::size_t>> inter;
        for (const auto& e : dedupEdgesO) {
          const bool ai = ownedSet.count(e.first), bi = ownedSet.count(e.second);
          if (ai && !bi) inter.emplace_back(e.first, e.second);
          else if (bi && !ai) inter.emplace_back(e.second, e.first);
        }
        if (inter.size() < 2) continue;
        double cx = 0, cy = 0;
        for (std::size_t n : owned) { cx += attributes.x(nodes[n].handle); cy += attributes.y(nodes[n].handle); }
        cx /= owned.size(); cy /= owned.size();
        // Score candidates by the ACTUAL straight-line crossings involving this
        // cluster's incident edges (length proved a poor proxy — it shortened
        // inter-edges but left crossings flat). For each rotation+reflection,
        // temporarily transform the owned nodes from their backup and count
        // crossings of the cluster's incident edges against all edges; keep the
        // transform with the fewest. Greedy per cluster; global revert below.
        std::vector<std::size_t> inv;
        for (std::size_t ii = 0; ii < dedupEdgesO.size(); ++ii)
          if (ownedSet.count(dedupEdgesO[ii].first) || ownedSet.count(dedupEdgesO[ii].second))
            inv.push_back(ii);
        std::vector<std::pair<double, double>> ownedBak(owned.size());
        for (std::size_t qi = 0; qi < owned.size(); ++qi)
          ownedBak[qi] = {attributes.x(nodes[owned[qi]].handle), attributes.y(nodes[owned[qi]].handle)};
        auto applyFromBak = [&](double cs, double sn, double s) {
          for (std::size_t qi = 0; qi < owned.size(); ++qi) {
            const double rx = (ownedBak[qi].first - cx) * s, ry = (ownedBak[qi].second - cy);
            attributes.x(nodes[owned[qi]].handle) = cx + cs * rx - sn * ry;
            attributes.y(nodes[owned[qi]].handle) = cy + sn * rx + cs * ry;
          }
        };
        auto involvedCross = [&]() -> std::size_t {
          std::size_t t = 0;
          for (std::size_t ii : inv) {
            const std::size_t a = dedupEdgesO[ii].first, b = dedupEdgesO[ii].second;
            const double ax = attributes.x(nodes[a].handle), ay = attributes.y(nodes[a].handle);
            const double bx = attributes.x(nodes[b].handle), by = attributes.y(nodes[b].handle);
            for (std::size_t j = 0; j < dedupEdgesO.size(); ++j) {
              if (j == ii) continue;
              const std::size_t c = dedupEdgesO[j].first, d = dedupEdgesO[j].second;
              if (a == c || a == d || b == c || b == d) continue;
              if (segCrossO(ax, ay, bx, by,
                            attributes.x(nodes[c].handle), attributes.y(nodes[c].handle),
                            attributes.x(nodes[d].handle), attributes.y(nodes[d].handle))) ++t;
            }
          }
          return t;
        };
        std::size_t bestCr = involvedCross();  // current positions = backup = identity
        double bestCs = 1.0, bestSn = 0.0, bestS = 1.0;
        for (int s = 0; s < 2; ++s) {
          const double sgn = s ? -1.0 : 1.0;
          for (int k = 0; k < 12; k += kStep) {
            if (s == 0 && k == 0) continue;  // identity already scored
            const double th = k * kPi / 6.0, cs = std::cos(th), sn = std::sin(th);
            applyFromBak(cs, sn, sgn);
            const std::size_t cr = involvedCross();
            if (cr < bestCr) { bestCr = cr; bestCs = cs; bestSn = sn; bestS = sgn; }
          }
        }
        applyFromBak(bestCs, bestSn, bestS);  // restore winner (identity if none better)
        if (!(bestCs == 1.0 && bestSn == 0.0 && bestS == 1.0)) { ++orientedClusters; ++roundOriented; }
      }
      const std::size_t roundCross = globalCross();
      std::fprintf(stderr,
        "[cluster-orient] round %d: %d clusters, straight-line %zu -> %zu.\n",
        rd, roundOriented, prevRoundCross, roundCross);
      if (roundOriented == 0 || roundCross >= prevRoundCross) { prevRoundCross = roundCross; break; }
      prevRoundCross = roundCross;
      }  // orient rounds

      // === Cluster-swap pass (DJERD_CLUSTER_SWAP=1, default off) ===
      // After rotation, swap the centroids of crossing-heavy cluster PAIRS
      // (placement lever; §13 cluster-swap 원리). Cheap: detect straight-line
      // crossing pairs once per round, dedup to the cluster pairs they span,
      // and evaluate each by that pair's incident-edge crossings. Node-overlap
      // guard (swap of unequal clusters would otherwise collide nodes — the
      // edge-crossing revert can't see that): only swap clusters of similar box
      // area, and only if neither swapped box ends up overlapping a THIRD
      // cluster's box. Global straight-line revert at the bottom as backstop.
      if (const char* sw = std::getenv("DJERD_CLUSTER_SWAP"); sw && std::strcmp(sw, "0") != 0) {
        auto cbox = [&](std::size_t r) -> std::array<double, 4> {
          double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
          auto it = clusterOwnedO.find(r);
          if (it != clusterOwnedO.end())
            for (std::size_t n : it->second) {
              const double x = attributes.x(nodes[n].handle), y = attributes.y(nodes[n].handle);
              x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y);
            }
          return {x0, y0, x1, y1};
        };
        auto ovl = [](const std::array<double, 4>& a, const std::array<double, 4>& b) {
          return a[0] < b[2] && b[0] < a[2] && a[1] < b[3] && b[1] < a[3];
        };
        auto translateC = [&](std::size_t r, double dx, double dy) {
          auto it = clusterOwnedO.find(r);
          if (it == clusterOwnedO.end()) return;
          for (std::size_t n : it->second) {
            attributes.x(nodes[n].handle) += dx; attributes.y(nodes[n].handle) += dy;
          }
        };
        auto cOf = [&](std::size_t v) -> std::size_t {
          auto it = nodeToCRoot.find(v); return it == nodeToCRoot.end() ? SIZE_MAX : it->second;
        };
        auto pairCross = [&](std::size_t rA, std::size_t rB) -> std::size_t {
          std::size_t t = 0;
          for (std::size_t i = 0; i < dedupEdgesO.size(); ++i) {
            const std::size_t a = dedupEdgesO[i].first, b = dedupEdgesO[i].second;
            const std::size_t ca = cOf(a), cb = cOf(b);
            if (!(ca == rA || ca == rB || cb == rA || cb == rB)) continue;
            const double ax = attributes.x(nodes[a].handle), ay = attributes.y(nodes[a].handle);
            const double bx = attributes.x(nodes[b].handle), by = attributes.y(nodes[b].handle);
            for (std::size_t j = 0; j < dedupEdgesO.size(); ++j) {
              if (j == i) continue;
              const std::size_t c = dedupEdgesO[j].first, d = dedupEdgesO[j].second;
              if (a == c || a == d || b == c || b == d) continue;
              if (segCrossO(ax, ay, bx, by,
                            attributes.x(nodes[c].handle), attributes.y(nodes[c].handle),
                            attributes.x(nodes[d].handle), attributes.y(nodes[d].handle))) ++t;
            }
          }
          return t;
        };
        std::vector<std::size_t> swapRoots;
        for (const auto& kv : clusterOwnedO) if (kv.second.size() >= 2) swapRoots.push_back(kv.first);
        const std::size_t beforeSwap = globalCross();
        int clusterSwaps = 0;
        for (int srd = 0; srd < 8; ++srd) {
          std::set<std::pair<std::size_t, std::size_t>> cand;
          for (std::size_t i = 0; i < dedupEdgesO.size(); ++i) {
            const std::size_t a = dedupEdgesO[i].first, b = dedupEdgesO[i].second;
            const double ax = attributes.x(nodes[a].handle), ay = attributes.y(nodes[a].handle);
            const double bx = attributes.x(nodes[b].handle), by = attributes.y(nodes[b].handle);
            for (std::size_t j = i + 1; j < dedupEdgesO.size(); ++j) {
              const std::size_t c = dedupEdgesO[j].first, d = dedupEdgesO[j].second;
              if (a == c || a == d || b == c || b == d) continue;
              if (!segCrossO(ax, ay, bx, by,
                             attributes.x(nodes[c].handle), attributes.y(nodes[c].handle),
                             attributes.x(nodes[d].handle), attributes.y(nodes[d].handle))) continue;
              const std::size_t rA = cOf(a), rB = cOf(c);
              if (rA == SIZE_MAX || rB == SIZE_MAX || rA == rB) continue;
              cand.insert(rA < rB ? std::make_pair(rA, rB) : std::make_pair(rB, rA));
            }
          }
          bool improved = false;
          std::unordered_set<std::size_t> used;
          for (const auto& pr : cand) {
            const std::size_t rA = pr.first, rB = pr.second;
            if (used.count(rA) || used.count(rB)) continue;
            const auto ba = cbox(rA), bb = cbox(rB);
            const double areaA = (ba[2] - ba[0]) * (ba[3] - ba[1]);
            const double areaB = (bb[2] - bb[0]) * (bb[3] - bb[1]);
            if (areaA <= 0 || areaB <= 0) continue;
            if ((areaA > areaB ? areaA / areaB : areaB / areaA) > 5.0) continue;  // size guard
            const double dx = ((bb[0] + bb[2]) - (ba[0] + ba[2])) / 2.0;
            const double dy = ((bb[1] + bb[3]) - (ba[1] + ba[3])) / 2.0;
            const std::size_t before = pairCross(rA, rB);
            translateC(rA, dx, dy); translateC(rB, -dx, -dy);
            const auto na = cbox(rA), nb = cbox(rB);
            bool bad = false;
            for (std::size_t o : swapRoots) {
              if (o == rA || o == rB) continue;
              const auto bo = cbox(o);
              if (ovl(na, bo) || ovl(nb, bo)) { bad = true; break; }
            }
            if (!bad && pairCross(rA, rB) < before) {
              improved = true; used.insert(rA); used.insert(rB); ++clusterSwaps;
            } else {
              translateC(rA, -dx, -dy); translateC(rB, dx, dy);  // revert
            }
          }
          if (!improved) break;
        }
        const std::size_t afterSwap = globalCross();
        std::fprintf(stderr,
          "[cluster-swap] %d swaps, straight-line %zu -> %zu.\n",
          clusterSwaps, beforeSwap, afterSwap);
      }
      }  // outer rotate↔swap loop

      // === Outlier node-pull (DJERD_NODE_PULL=1, default off) ===
      // Pull abnormally-far LOW-degree nodes toward their neighbour centroid,
      // but only when it strictly reduces that node's incident-edge crossings
      // AND the target spot isn't occupied (cheap node-overlap guard). The user
      // flagged "abnormally far clusters/nodes"; sim shows low-degree outliers
      // (deg<=4, >4% diag from neighbour centroid) net-improve when pulled in,
      // while hubs regress — so this skips high-degree nodes. Global revert
      // below if the whole orient+swap+pull pass regresses straight-line cross.
      if (const char* npe = std::getenv("DJERD_NODE_PULL"); npe && std::strcmp(npe, "0") != 0) {
        std::unordered_map<std::size_t, std::vector<std::size_t>> incid;
        for (const auto& pr : dedupEdgesO) {
          incid[pr.first].push_back(pr.second);
          incid[pr.second].push_back(pr.first);
        }
        double X0 = 1e18, Y0 = 1e18, X1 = -1e18, Y1 = -1e18;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          const double x = attributes.x(nodes[i].handle), y = attributes.y(nodes[i].handle);
          X0 = std::min(X0, x); Y0 = std::min(Y0, y); X1 = std::max(X1, x); Y1 = std::max(Y1, y);
        }
        const double thr = 0.04 * std::hypot(X1 - X0, Y1 - Y0);
        auto ncross = [&](std::size_t n, double nx, double ny) -> std::size_t {
          std::size_t t = 0; auto it = incid.find(n);
          if (it == incid.end()) return 0;
          for (std::size_t x : it->second) {
            const double xx = attributes.x(nodes[x].handle), xy = attributes.y(nodes[x].handle);
            for (const auto& pr : dedupEdgesO) {
              const std::size_t a = pr.first, b = pr.second;
              if (a == n || b == n || a == x || b == x) continue;
              if (segCrossO(nx, ny, xx, xy,
                            attributes.x(nodes[a].handle), attributes.y(nodes[a].handle),
                            attributes.x(nodes[b].handle), attributes.y(nodes[b].handle))) ++t;
            }
          }
          return t;
        };
        auto occupied = [&](double x, double y, std::size_t self) -> bool {
          for (std::size_t m = 0; m < nodes.size(); ++m) {
            if (m == self) continue;
            const double dx = attributes.x(nodes[m].handle) - x, dy = attributes.y(nodes[m].handle) - y;
            if (dx * dx + dy * dy < 620.0 * 620.0) return true;
          }
          return false;
        };
        const std::size_t beforeP = globalCross();
        int pulled = 0;
        for (int rd = 0; rd < 3; ++rd) {
          bool imp = false;
          for (std::size_t n = 0; n < nodes.size(); ++n) {
            auto it = incid.find(n);
            if (it == incid.end() || it->second.empty() || it->second.size() > 4) continue;
            double cx = 0, cy = 0;
            for (std::size_t x : it->second) { cx += attributes.x(nodes[x].handle); cy += attributes.y(nodes[x].handle); }
            cx /= it->second.size(); cy /= it->second.size();
            const double px = attributes.x(nodes[n].handle), py = attributes.y(nodes[n].handle);
            if (std::hypot(px - cx, py - cy) < thr) continue;  // outlier only
            if (occupied(cx, cy, n)) continue;                 // overlap guard
            if (ncross(n, cx, cy) < ncross(n, px, py)) {
              attributes.x(nodes[n].handle) = cx; attributes.y(nodes[n].handle) = cy;
              ++pulled; imp = true;
            }
          }
          if (!imp) break;
        }
        const std::size_t afterP = globalCross();
        std::fprintf(stderr,
          "[node-pull] %d nodes pulled, straight-line %zu -> %zu.\n",
          pulled, beforeP, afterP);
      }

      auto nodeBBoxO = [&](bool useSnap) -> double {
        double a = 1e18, b = 1e18, c = -1e18, d = -1e18;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          const double xx = useSnap ? snapO[i].first : attributes.x(nodes[i].handle);
          const double yy = useSnap ? snapO[i].second : attributes.y(nodes[i].handle);
          a = std::min(a, xx); c = std::max(c, xx); b = std::min(b, yy); d = std::max(d, yy);
        }
        return (c - a) * (d - b) / 1e9;
      };
      const double obBefore = nodeBBoxO(true), obAfter = nodeBBoxO(false);
      const std::size_t afterOrient = globalCross();
      if (afterOrient > beforeOrient) {
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          attributes.x(nodes[i].handle) = snapO[i].first;
          attributes.y(nodes[i].handle) = snapO[i].second;
        }
        std::fprintf(stderr,
          "[cluster-orient] reverted: %d clusters, straight-line crossings %zu -> %zu (worse).\n",
          orientedClusters, beforeOrient, afterOrient);
      } else {
        std::fprintf(stderr,
          "[cluster-orient] %d clusters oriented, straight-line crossings %zu -> %zu, nodeBBox %.2fB -> %.2fB.\n",
          orientedClusters, beforeOrient, afterOrient, obBefore, obAfter);
      }
      // Component cohesion (AFTER revert → permanent). Louvain splits a
      // hub-independent group — e.g. AuditLog is ONE 17-node connected component
      // once hubs(deg>50) are removed, but Louvain shatters it into 3 communities
      // and placement scatters them across 39%x26% of the canvas, so its internal
      // edges cross the giant Address blob (user-flagged: 241 crossings). Per the
      // user's union-find insight, two edges in DIFFERENT hub-excluded components
      // only cross because the components overlap in space — gathering each small
      // scattered component to its own centroid resolves them deterministically.
      // (Louvain-cluster cohesion failed: it re-densified already-tight clusters.
      // This instead re-joins INDEPENDENT components Louvain wrongly split.)
      if (const char* coh = std::getenv("DJERD_CLUSTER_COHESION"); coh && std::strcmp(coh, "0") != 0) {
        const double cohThr = 0.05 * std::hypot(
          [&]{ double a=1e18,b=-1e18; for(std::size_t i=0;i<nodes.size();++i){double x=attributes.x(nodes[i].handle);a=std::min(a,x);b=std::max(b,x);} return b-a; }(),
          [&]{ double a=1e18,b=-1e18; for(std::size_t i=0;i<nodes.size();++i){double y=attributes.y(nodes[i].handle);a=std::min(a,y);b=std::max(b,y);} return b-a; }());
        const double minGap = 360.0;
        auto occ = [&](double x, double y, std::size_t self) -> bool {
          for (std::size_t m = 0; m < nodes.size(); ++m) {
            if (m == self) continue;
            const double dx = attributes.x(nodes[m].handle) - x, dy = attributes.y(nodes[m].handle) - y;
            if (dx * dx + dy * dy < minGap * minGap) return true;
          }
          return false;
        };
        // hub-excluded connected components via union-find over non-hub edges
        std::unordered_map<std::size_t, int> dg;
        for (const auto& pr : dedupEdgesO) { dg[pr.first]++; dg[pr.second]++; }
        const int hubThr = [] {
          const char* h = std::getenv("DJERD_COHESION_HUB_DEG");
          return h ? std::max(1, std::atoi(h)) : 50;
        }();
        std::vector<std::size_t> uf(nodes.size());
        for (std::size_t i = 0; i < nodes.size(); ++i) uf[i] = i;
        auto ff = [&uf](std::size_t x) {
          while (uf[x] != x) { uf[x] = uf[uf[x]]; x = uf[x]; }
          return x;
        };
        for (const auto& pr : dedupEdgesO) {
          if (dg[pr.first] > hubThr || dg[pr.second] > hubThr) continue;
          uf[ff(pr.first)] = ff(pr.second);
        }
        std::unordered_map<std::size_t, std::vector<std::size_t>> comp;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          auto it = dg.find(i);
          if (it != dg.end() && it->second <= hubThr) comp[ff(i)].push_back(i);
        }
        int cohesed = 0;
        for (int rd = 0; rd < 4; ++rd) {
          bool imp = false;
          for (std::size_t n = 0; n < nodes.size(); ++n) {
            auto it = dg.find(n);
            if (it == dg.end() || it->second > hubThr) continue;  // hubs stay put
            const std::vector<std::size_t>& members = comp[ff(n)];
            // only small scattered independent groups; leave the giant component
            if (members.size() < 4 || members.size() > 120) continue;
            double cx = 0, cy = 0; std::size_t cnt = 0;
            for (std::size_t m : members) {
              if (m == n) continue;
              cx += attributes.x(nodes[m].handle); cy += attributes.y(nodes[m].handle); ++cnt;
            }
            if (cnt == 0) continue;
            cx /= cnt; cy /= cnt;
            const double px = attributes.x(nodes[n].handle), py = attributes.y(nodes[n].handle);
            if (std::hypot(px - cx, py - cy) < cohThr) continue;
            double tx = cx, ty = cy;
            if (occ(tx, ty, n)) {
              bool placed = false;
              for (double f = 0.7; f >= 0.2; f -= 0.1) {
                const double qx = px + (cx - px) * f, qy = py + (cy - py) * f;
                if (!occ(qx, qy, n)) { tx = qx; ty = qy; placed = true; break; }
              }
              if (!placed) continue;
            }
            attributes.x(nodes[n].handle) = tx; attributes.y(nodes[n].handle) = ty;
            ++cohesed; imp = true;
          }
          if (!imp) break;
        }
        std::fprintf(stderr, "[component-cohesion] %d nodes gathered to hub-excluded component centroid (hubThr=%d).\n", cohesed, hubThr);
      }
    }
}
}  // namespace djerd
