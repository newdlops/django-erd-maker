#include "layoutPipeline.h"
#include "straightRouteCandidates.h"

namespace djerd {

bool applyRenderedCarrierMetricsIfRequested(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes,
  const std::unordered_map<std::string, std::string>& clusterByModelIdFull,
  LayoutRunMetadata& metadata,
  LayoutQualityMetrics& quality,
  std::size_t totalRouteCrossings,
  bool quiet,
  bool optimizeGeometry) {
  const char* renderedCarrierMetricsEnv =
    std::getenv("DJERD_RENDERED_CARRIER_METRICS_FINAL");
  const bool renderedCarrierMetrics =
    renderedCarrierMetricsEnv && std::strcmp(renderedCarrierMetricsEnv, "0") != 0;
  // Plain layouts without bundle/cluster structure still render their
  // individual edges. They need the same exact edge/node penetration audit;
  // limiting this metric to cluster layouts would let a visible raw edge pass
  // through a table without entering the final hard target.
  if (!renderedCarrierMetrics) {
    return false;
  }

  const char* skipCarrierEnv = std::getenv("DJERD_NO_CARRIER_CROSS");
  const bool skipCarrier =
    skipCarrierEnv && std::strcmp(skipCarrierEnv, "0") != 0;
  // Direct mode disables semantic/bundle carrier substitution, not the final
  // metric itself. Continue below with one path per routed relationship and
  // audit every real model rectangle used by the canvas.

  std::unordered_map<std::string, std::size_t> leafToBundleIdx;
  if (!skipCarrier) {
    for (std::size_t bi = 0; bi < metadata.leafBundles.size(); ++bi) {
      for (const std::string& leaf : metadata.leafBundles[bi].leafModelIds) {
        leafToBundleIdx[leaf] = bi;
      }
    }
  }

  std::unordered_map<std::string, const NodeRecord*> nodeByModelId;
  nodeByModelId.reserve(nodes.size());
  for (const NodeRecord& node : nodes) {
    nodeByModelId[node.modelId] = &node;
  }

  std::unordered_map<std::string, std::pair<double, double>> sumByCluster;
  std::unordered_map<std::string, std::size_t> cntByCluster;
  for (const auto& kv : clusterByModelIdFull) {
    auto nodeIt = nodeByModelId.find(kv.first);
    if (nodeIt == nodeByModelId.end()) continue;
    sumByCluster[kv.second].first += attributes.x(nodeIt->second->handle);
    sumByCluster[kv.second].second += attributes.y(nodeIt->second->handle);
    cntByCluster[kv.second] += 1;
  }
  std::unordered_map<std::string, std::pair<double, double>> clusterCentroids;
  for (const auto& kv : sumByCluster) {
    const std::size_t count = cntByCluster[kv.first];
    if (count == 0) continue;
    clusterCentroids[kv.first] = {
      kv.second.first / static_cast<double>(count),
      kv.second.second / static_cast<double>(count),
    };
  }
  auto nearestCluster = [&](const std::string& modelId) {
    auto nodeIt = nodeByModelId.find(modelId);
    if (nodeIt == nodeByModelId.end()) return std::string{};
    const double mx = attributes.x(nodeIt->second->handle);
    const double my = attributes.y(nodeIt->second->handle);
    std::string best;
    double bestD2 = std::numeric_limits<double>::infinity();
    for (const auto& kv : clusterCentroids) {
      const double dx = mx - kv.second.first;
      const double dy = my - kv.second.second;
      const double d2 = dx * dx + dy * dy;
      if (d2 < bestD2) {
        bestD2 = d2;
        best = kv.first;
      }
    }
    return best;
  };

  std::vector<std::string> renderedCarrierIdByEdge(edges.size());
  std::vector<bool> renderedEdgeVisible(edges.size(), true);
  std::vector<int> renderedBundleIndexByEdge(edges.size(), -1);
  std::vector<std::string> renderedBundleRootByEdge(edges.size());
  std::vector<std::pair<std::string, std::string>> carrierClustersByEdge(edges.size());
  const bool inheritanceCarrier =
    readBoolEnv("DJERD_INHERITANCE_CARRIER_FINAL", false);
  const bool intraClusterCarrier =
    readBoolEnv("DJERD_INTRA_CLUSTER_CARRIER_FINAL", false);
  metadata.inheritanceCarrierGrouping = inheritanceCarrier;
  metadata.intraClusterCarrierGrouping = intraClusterCarrier;

  for (std::size_t e = 0; e < edges.size(); ++e) {
    const std::string& source = edges[e].sourceModelId;
    const std::string& target = edges[e].targetModelId;
    const bool inheritance = edges[e].kind == "inheritance";
    auto sourceBundleIt = leafToBundleIdx.find(source);
    auto targetBundleIt = leafToBundleIdx.find(target);
    if (sourceBundleIt != leafToBundleIdx.end()) {
      const auto& bundle = metadata.leafBundles[sourceBundleIt->second];
      const auto& roots = bundle.sharedRootModelIds.empty()
        ? std::vector<std::string>{bundle.parentModelId}
        : bundle.sharedRootModelIds;
      if (std::find(roots.begin(), roots.end(), target) != roots.end()) {
        renderedCarrierIdByEdge[e] =
          "B" + std::to_string(sourceBundleIt->second) + "|" + target;
        renderedBundleIndexByEdge[e] = static_cast<int>(sourceBundleIt->second);
        renderedBundleRootByEdge[e] = target;
        continue;
      }
    }
    if (targetBundleIt != leafToBundleIdx.end()) {
      const auto& bundle = metadata.leafBundles[targetBundleIt->second];
      const auto& roots = bundle.sharedRootModelIds.empty()
        ? std::vector<std::string>{bundle.parentModelId}
        : bundle.sharedRootModelIds;
      if (std::find(roots.begin(), roots.end(), source) != roots.end()) {
        renderedCarrierIdByEdge[e] =
          "B" + std::to_string(targetBundleIt->second) + "|" + source;
        renderedBundleIndexByEdge[e] = static_cast<int>(targetBundleIt->second);
        renderedBundleRootByEdge[e] = source;
        continue;
      }
    }
    if (sourceBundleIt != leafToBundleIdx.end() || targetBundleIt != leafToBundleIdx.end()) {
      // The webview keeps a bundled leaf's non-carrier inheritance edge as an
      // individual structural line. Other incidental bundled-leaf relations
      // remain hidden. Neither kind participates in hub-carrier incidence.
      if (inheritance) {
        renderedCarrierIdByEdge[e] = inheritanceCarrier
          ? "I|" + target
          : edges[e].edgeId;
      } else {
        renderedEdgeVisible[e] = false;
      }
      continue;
    }

    // Inheritance never folds into a cluster hub carrier. When explicitly
    // enabled, siblings sharing one parent use their own inheritance carrier.
    if (inheritance) {
      renderedCarrierIdByEdge[e] = inheritanceCarrier
        ? "I|" + target
        : edges[e].edgeId;
      continue;
    }

    auto sourceClusterIt = clusterByModelIdFull.find(source);
    auto targetClusterIt = clusterByModelIdFull.find(target);
    std::string sourceCluster = sourceClusterIt != clusterByModelIdFull.end()
      ? sourceClusterIt->second
      : std::string{};
    std::string targetCluster = targetClusterIt != clusterByModelIdFull.end()
      ? targetClusterIt->second
      : std::string{};
    if (sourceCluster.empty()) sourceCluster = nearestCluster(source);
    if (targetCluster.empty()) targetCluster = nearestCluster(target);
    if (!sourceCluster.empty() && !targetCluster.empty()) {
      carrierClustersByEdge[e] = {sourceCluster, targetCluster};
      if (intraClusterCarrier && sourceCluster == targetCluster) {
        renderedCarrierIdByEdge[e] = "Cself|" + sourceCluster;
        continue;
      }
    }
    renderedCarrierIdByEdge[e] = edges[e].edgeId;
  }

  const char* hubCarrierEnv = std::getenv("DJERD_HUB_CARRIER_CROSS_FINAL");
  const bool hubCarrier =
    hubCarrierEnv && std::strcmp(hubCarrierEnv, "0") != 0;
  if (hubCarrier) {
    const char* thresholdEnv =
      std::getenv("DJERD_HUB_CARRIER_CROSS_FINAL_THRESHOLD");
    const int threshold = thresholdEnv
      ? std::max(2, std::atoi(thresholdEnv))
      : 16;
    std::unordered_map<std::string, int> incidentCarrierCount;
    for (const auto& [leftCluster, rightCluster] : carrierClustersByEdge) {
      if (leftCluster.empty() || rightCluster.empty() || leftCluster == rightCluster) {
        continue;
      }
      incidentCarrierCount[leftCluster] += 1;
      incidentCarrierCount[rightCluster] += 1;
    }
    std::size_t hubEdges = 0;
    std::unordered_set<std::string> hubClusters;
    for (std::size_t e = 0; e < carrierClustersByEdge.size(); ++e) {
      const auto& [leftCluster, rightCluster] = carrierClustersByEdge[e];
      if (leftCluster.empty() || rightCluster.empty() || leftCluster == rightCluster) {
        continue;
      }
      const int leftCount = incidentCarrierCount[leftCluster];
      const int rightCount = incidentCarrierCount[rightCluster];
      if (leftCount < threshold && rightCount < threshold) {
        continue;
      }
      const std::string& hub =
        (leftCount > rightCount || (leftCount == rightCount && leftCluster < rightCluster))
          ? leftCluster
          : rightCluster;
      renderedCarrierIdByEdge[e] = "H|" + hub;
      hubClusters.insert(hub);
      ++hubEdges;
    }
    metadata.hubCarrierThreshold = threshold;
    metadata.hubCarrierEdgesGrouped = hubEdges;
    metadata.hubCarrierClusters = hubClusters.size();
  }

  const char* occMarginEnv = std::getenv("DJERD_CARRIER_CROSS_OCCLUSION_MARGIN");
  const double occMargin = occMarginEnv
    ? std::max(0.0, std::atof(occMarginEnv))
    : 0.0;
  std::unordered_set<std::string> bundleAbsorbed;
  if (!skipCarrier) {
    for (const LeafBundleRecord& bundle : metadata.leafBundles) {
      // Legacy carrier scenes replace raw leaves with fixed-size bundle tiles.
      // Direct scenes retain every real model table as an ordinary obstacle.
      for (const std::string& leaf : bundle.leafModelIds) {
        bundleAbsorbed.insert(leaf);
      }
    }
  }
  std::vector<Rect> occlusionRects;
  occlusionRects.reserve(nodes.size() + metadata.leafBundles.size());
  for (const NodeRecord& node : nodes) {
    if (bundleAbsorbed.count(node.modelId)) continue;
    occlusionRects.push_back(nodeRect(node, attributes, occMargin));
  }
  if (!skipCarrier) {
    for (const LeafBundleRecord& bundle : metadata.leafBundles) {
      occlusionRects.push_back(renderedLeafBundleRect(bundle, occMargin));
      const std::vector<Rect> tileRects = renderedLeafTileRects(bundle, occMargin);
      occlusionRects.insert(
        occlusionRects.end(),
        tileRects.begin(),
        tileRects.end());
    }
  }
  auto pointInOcclusion = [&](const RoutePoint& point) {
    if (occMargin <= 0.0) return false;
    for (const Rect& rect : occlusionRects) {
      if (
          point.x >= rect.left && point.x <= rect.right
          && point.y >= rect.top && point.y <= rect.bottom) {
        return true;
      }
    }
    return false;
  };

  struct RenderedCarrierMetricPath {
    std::string id;
    std::vector<RoutePoint> points;
    StraightRouteCandidates<RoutePoint> straightCandidates;
    std::vector<std::string> memberEdgeIds;
    std::unordered_set<std::string> endpointModelIds;
    std::unordered_set<std::size_t> endpointBundleIndices;
    bool hasStraightEndpointRects = false;
    Rect straightStartRect{};
    Rect straightEndRect{};
  };

  auto startsWith = [](const std::string& value, const char* prefix) {
    return value.rfind(prefix, 0) == 0;
  };
  auto rectCenterPoint = [](const Rect& rect) {
    return RoutePoint{
      (rect.left + rect.right) / 2.0,
      (rect.top + rect.bottom) / 2.0,
    };
  };
  auto boundaryPort = [&](const Rect& rect, const RoutePoint& toward) {
    const RoutePoint center = rectCenterPoint(rect);
    const double dx = toward.x - center.x;
    const double dy = toward.y - center.y;
    if (std::abs(dx) < 0.01 && std::abs(dy) < 0.01) {
      return center;
    }
    double scale = std::numeric_limits<double>::infinity();
    if (std::abs(dx) >= 0.01) {
      const double sx = dx > 0.0
        ? (rect.right - center.x) / dx
        : (rect.left - center.x) / dx;
      if (sx > 0.0) scale = std::min(scale, sx);
    }
    if (std::abs(dy) >= 0.01) {
      const double sy = dy > 0.0
        ? (rect.bottom - center.y) / dy
        : (rect.top - center.y) / dy;
      if (sy > 0.0) scale = std::min(scale, sy);
    }
    if (!std::isfinite(scale)) {
      scale = 0.0;
    }
    return RoutePoint{
      std::round((center.x + dx * scale) * 100.0) / 100.0,
      std::round((center.y + dy * scale) * 100.0) / 100.0,
    };
  };
  const bool optimizeStraightPorts = readBoolEnv(
    "DJERD_RENDERED_STRAIGHT_PORT_OPT_FINAL", false);
  auto roundedRoutePoint = [](double x, double y) {
    return RoutePoint{
      std::round(x * 100.0) / 100.0,
      std::round(y * 100.0) / 100.0,
    };
  };
  auto rectBoundaryCandidates = [&](const Rect& rect) {
    const double cx = rectCenterX(rect);
    const double cy = rectCenterY(rect);
    return std::vector<RoutePoint>{
      roundedRoutePoint(rect.left, rect.top),
      roundedRoutePoint(cx, rect.top),
      roundedRoutePoint(rect.right, rect.top),
      roundedRoutePoint(rect.right, cy),
      roundedRoutePoint(rect.right, rect.bottom),
      roundedRoutePoint(cx, rect.bottom),
      roundedRoutePoint(rect.left, rect.bottom),
      roundedRoutePoint(rect.left, cy),
    };
  };
  const int straightPortSamplesPerSide = static_cast<int>(readDoubleEnv(
    "DJERD_RENDERED_STRAIGHT_PORT_SAMPLES_PER_SIDE", 0.0, 0.0, 24.0));
  auto candidatePorts = [&](const Rect& rect, const RoutePoint& toward) {
    if (straightPortSamplesPerSide < 2) {
      return rectBoundaryCandidates(rect);
    }
    std::vector<RoutePoint> candidates;
    candidates.reserve(
      static_cast<std::size_t>(straightPortSamplesPerSide + 1) * 2 + 1);
    auto addCandidate = [&](const RoutePoint& candidate) {
      for (const RoutePoint& existing : candidates) {
        if (
            std::abs(existing.x - candidate.x) < 0.005
            && std::abs(existing.y - candidate.y) < 0.005) {
          return;
        }
      }
      candidates.push_back(candidate);
    };
    const RoutePoint center = rectCenterPoint(rect);
    addCandidate(boundaryPort(rect, toward));
    for (int sample = 0; sample <= straightPortSamplesPerSide; ++sample) {
      const double t = static_cast<double>(sample)
        / static_cast<double>(straightPortSamplesPerSide);
      if (std::abs(toward.x - center.x) >= 0.01) {
        const double x = toward.x >= center.x ? rect.right : rect.left;
        addCandidate(roundedRoutePoint(
          x, rect.top + rectHeight(rect) * t));
      }
      if (std::abs(toward.y - center.y) >= 0.01) {
        const double y = toward.y >= center.y ? rect.bottom : rect.top;
        addCandidate(roundedRoutePoint(
          rect.left + rectWidth(rect) * t, y));
      }
    }
    return candidates;
  };
  auto sourceTargetRouteEndpoints = [&](std::size_t edgeIndex) {
    const std::vector<RoutePoint>& route = routes[edgeIndex];
    RoutePoint front = route.front();
    RoutePoint back = route.back();
    auto sourceIt = nodeByModelId.find(edges[edgeIndex].sourceModelId);
    auto targetIt = nodeByModelId.find(edges[edgeIndex].targetModelId);
    if (sourceIt == nodeByModelId.end() || targetIt == nodeByModelId.end()) {
      return std::make_pair(front, back);
    }
    const RoutePoint sourceCenter = rectCenterPoint(
      nodeRect(*sourceIt->second, attributes, 0.0));
    const RoutePoint targetCenter = rectCenterPoint(
      nodeRect(*targetIt->second, attributes, 0.0));
    const auto distanceSquared = [](const RoutePoint& left, const RoutePoint& right) {
      const double dx = left.x - right.x;
      const double dy = left.y - right.y;
      return dx * dx + dy * dy;
    };
    const double forward =
      distanceSquared(front, sourceCenter) + distanceSquared(back, targetCenter);
    const double reversed =
      distanceSquared(front, targetCenter) + distanceSquared(back, sourceCenter);
    return forward <= reversed
      ? std::make_pair(front, back)
      : std::make_pair(back, front);
  };

  std::unordered_map<std::string, std::vector<std::size_t>> membersByCarrier;
  membersByCarrier.reserve(edges.size());
  for (std::size_t e = 0; e < edges.size(); ++e) {
    if (!renderedEdgeVisible[e]) continue;
    if (e >= routes.size() || routes[e].size() < 2) continue;
    if (renderedCarrierIdByEdge[e].empty()) {
      renderedCarrierIdByEdge[e] = edges[e].edgeId;
    }
    membersByCarrier[renderedCarrierIdByEdge[e]].push_back(e);
  }

  std::vector<RenderedCarrierMetricPath> renderedPaths;
  renderedPaths.reserve(membersByCarrier.size());
  auto addRawPath = [&](std::size_t edgeIndex) {
    if (edgeIndex >= routes.size() || routes[edgeIndex].size() < 2) return;
    RenderedCarrierMetricPath path;
    path.id = edges[edgeIndex].edgeId;
    path.points = routes[edgeIndex];
    path.memberEdgeIds.push_back(edges[edgeIndex].edgeId);
    path.endpointModelIds.insert(edges[edgeIndex].sourceModelId);
    path.endpointModelIds.insert(edges[edgeIndex].targetModelId);
    if (optimizeStraightPorts && routes[edgeIndex].size() == 2) {
      auto sourceIt = nodeByModelId.find(edges[edgeIndex].sourceModelId);
      auto targetIt = nodeByModelId.find(edges[edgeIndex].targetModelId);
      if (sourceIt != nodeByModelId.end() && targetIt != nodeByModelId.end()) {
        const auto sourceBundleIt =
          leafToBundleIdx.find(edges[edgeIndex].sourceModelId);
        const auto targetBundleIt =
          leafToBundleIdx.find(edges[edgeIndex].targetModelId);
        const bool bundleAnchoredInheritance =
          edges[edgeIndex].kind == "inheritance"
          && (sourceBundleIt != leafToBundleIdx.end()
              || targetBundleIt != leafToBundleIdx.end());
        const Rect sourceRect = sourceBundleIt != leafToBundleIdx.end()
          ? renderedLeafBundleRect(
              metadata.leafBundles[sourceBundleIt->second], 0.0)
          : nodeRect(*sourceIt->second, attributes, 0.0);
        const Rect targetRect = targetBundleIt != leafToBundleIdx.end()
          ? renderedLeafBundleRect(
              metadata.leafBundles[targetBundleIt->second], 0.0)
          : nodeRect(*targetIt->second, attributes, 0.0);
        if (bundleAnchoredInheritance) {
          const RoutePoint sourceCenter = rectCenterPoint(sourceRect);
          const RoutePoint targetCenter = rectCenterPoint(targetRect);
          path.points = {
            boundaryPort(sourceRect, targetCenter),
            boundaryPort(targetRect, sourceCenter),
          };
          // This is an individual inheritance line from one compact leaf to
          // an external base. Exempt its outer bundle frame, but keep every
          // sibling leaf tile as a visible obstacle. The actual endpoint leaf
          // is already present in endpointModelIds.
          if (sourceBundleIt != leafToBundleIdx.end()) {
            path.endpointBundleIndices.insert(sourceBundleIt->second);
          }
          if (targetBundleIt != leafToBundleIdx.end()) {
            path.endpointBundleIndices.insert(targetBundleIt->second);
          }
        } else {
          const auto [sourcePoint, targetPoint] =
            sourceTargetRouteEndpoints(edgeIndex);
          path.points = {sourcePoint, targetPoint};
        }
        path.hasStraightEndpointRects = true;
        path.straightStartRect = sourceRect;
        path.straightEndRect = targetRect;
        path.straightCandidates.push_back(path.points);
        const RoutePoint sourceCenter = rectCenterPoint(sourceRect);
        const RoutePoint targetCenter = rectCenterPoint(targetRect);
        const std::vector<RoutePoint> sourcePorts = candidatePorts(
          sourceRect, targetCenter);
        const std::vector<RoutePoint> targetPorts = candidatePorts(
          targetRect, sourceCenter);
        path.straightCandidates.setPortProduct(sourcePorts, targetPorts);
      }
    }
    renderedPaths.push_back(std::move(path));
  };

  for (const auto& kv : membersByCarrier) {
    const std::string& carrierId = kv.first;
    const std::vector<std::size_t>& members = kv.second;
    if (members.empty()) continue;

    if (startsWith(carrierId, "B")) {
      const std::size_t firstEdge = members.front();
      const int bundleIndex = renderedBundleIndexByEdge[firstEdge];
      const std::string& rootModelId = renderedBundleRootByEdge[firstEdge];
      auto rootIt = nodeByModelId.find(rootModelId);
      if (
          bundleIndex < 0
          || static_cast<std::size_t>(bundleIndex) >= metadata.leafBundles.size()
          || rootIt == nodeByModelId.end()) {
        for (const std::size_t edgeIndex : members) {
          addRawPath(edgeIndex);
        }
        continue;
      }
      const Rect bundleRect =
        renderedLeafBundleRect(metadata.leafBundles[bundleIndex], 0.0);
      const Rect rootRect = nodeRect(*rootIt->second, attributes, 0.0);
      const RoutePoint bundleCenter = rectCenterPoint(bundleRect);
      const RoutePoint rootCenter = rectCenterPoint(rootRect);

      RenderedCarrierMetricPath path;
      path.id = carrierId;
      path.endpointBundleIndices.insert(
        static_cast<std::size_t>(bundleIndex));
      path.points = {
        boundaryPort(bundleRect, rootCenter),
        boundaryPort(rootRect, bundleCenter),
      };
      for (const std::size_t edgeIndex : members) {
        path.memberEdgeIds.push_back(edges[edgeIndex].edgeId);
      }
      path.endpointModelIds.insert(rootModelId);
      path.endpointModelIds.insert(metadata.leafBundles[bundleIndex].parentModelId);
      for (const std::string& leaf : metadata.leafBundles[bundleIndex].leafModelIds) {
        path.endpointModelIds.insert(leaf);
      }
      if (optimizeStraightPorts) {
        path.hasStraightEndpointRects = true;
        path.straightStartRect = bundleRect;
        path.straightEndRect = rootRect;
        path.straightCandidates.push_back(path.points);
        const std::vector<RoutePoint> bundlePorts = candidatePorts(
          bundleRect, rootCenter);
        const std::vector<RoutePoint> rootPorts = candidatePorts(
          rootRect, bundleCenter);
        path.straightCandidates.setPortProduct(bundlePorts, rootPorts);
      }
      renderedPaths.push_back(std::move(path));
      continue;
    }

    if (
        (startsWith(carrierId, "H|")
         || startsWith(carrierId, "I|")
         || startsWith(carrierId, "Cself|"))
        && members.size() >= 2) {
      // The canvas can draw one straight carrier only when every represented
      // relationship has the same two logical endpoint tables. A hub or
      // intra-cluster bucket that fans out to three or more tables cannot be
      // represented by an averaged two-point line: the webview correctly
      // rejects that disconnected trunk and renders its members separately.
      // Score those same individual lines here so native candidate selection
      // and the final canvas never use different geometry domains.
      std::unordered_set<std::string> logicalEndpointModelIds;
      for (const std::size_t edgeIndex : members) {
        logicalEndpointModelIds.insert(edges[edgeIndex].sourceModelId);
        logicalEndpointModelIds.insert(edges[edgeIndex].targetModelId);
      }
      if (logicalEndpointModelIds.size() != 2) {
        for (const std::size_t edgeIndex : members) {
          addRawPath(edgeIndex);
        }
        continue;
      }

      double rawStartX = 0.0;
      double rawStartY = 0.0;
      double rawEndX = 0.0;
      double rawEndY = 0.0;
      for (const std::size_t edgeIndex : members) {
        const auto& route = routes[edgeIndex];
        rawStartX += route.front().x;
        rawStartY += route.front().y;
        rawEndX += route.back().x;
        rawEndY += route.back().y;
      }

      const std::size_t firstEdge = members.front();
      const bool firstHubAtSource = startsWith(carrierId, "H|")
        && carrierClustersByEdge[firstEdge].first == carrierId.substr(2);
      const bool firstAscending =
        edges[firstEdge].sourceModelId < edges[firstEdge].targetModelId;
      std::vector<std::pair<RoutePoint, RoutePoint>> alignedEndpoints;
      alignedEndpoints.reserve(members.size());
      double alignedStartX = 0.0;
      double alignedStartY = 0.0;
      double alignedEndX = 0.0;
      double alignedEndY = 0.0;
      for (const std::size_t edgeIndex : members) {
        auto [sourcePoint, targetPoint] = sourceTargetRouteEndpoints(edgeIndex);
        RoutePoint start = sourcePoint;
        RoutePoint end = targetPoint;
        if (startsWith(carrierId, "H|")) {
          const bool memberHubAtSource =
            carrierClustersByEdge[edgeIndex].first == carrierId.substr(2);
          if (memberHubAtSource != firstHubAtSource) {
            std::swap(start, end);
          }
        } else if (startsWith(carrierId, "Cself|")) {
          const bool memberAscending =
            edges[edgeIndex].sourceModelId < edges[edgeIndex].targetModelId;
          if (memberAscending != firstAscending) {
            std::swap(start, end);
          }
        }
        alignedEndpoints.emplace_back(start, end);
        alignedStartX += start.x;
        alignedStartY += start.y;
        alignedEndX += end.x;
        alignedEndY += end.y;
      }
      const double count = static_cast<double>(members.size());
      const auto roundedPoint = [](double x, double y) {
        return RoutePoint{
          std::round(x * 100.0) / 100.0,
          std::round(y * 100.0) / 100.0,
        };
      };
      const std::vector<RoutePoint> rawAverage = {
        roundedPoint(rawStartX / count, rawStartY / count),
        roundedPoint(rawEndX / count, rawEndY / count),
      };
      const std::vector<RoutePoint> alignedAverage = {
        roundedPoint(alignedStartX / count, alignedStartY / count),
        roundedPoint(alignedEndX / count, alignedEndY / count),
      };
      auto medianCoordinate = [](std::vector<double> values) {
        std::sort(values.begin(), values.end());
        const std::size_t middle = values.size() / 2;
        return values.size() % 2 == 1
          ? values[middle]
          : (values[middle - 1] + values[middle]) * 0.5;
      };
      std::vector<double> startXs;
      std::vector<double> startYs;
      std::vector<double> endXs;
      std::vector<double> endYs;
      startXs.reserve(alignedEndpoints.size());
      startYs.reserve(alignedEndpoints.size());
      endXs.reserve(alignedEndpoints.size());
      endYs.reserve(alignedEndpoints.size());
      for (const auto& [start, end] : alignedEndpoints) {
        startXs.push_back(start.x);
        startYs.push_back(start.y);
        endXs.push_back(end.x);
        endYs.push_back(end.y);
      }
      const std::vector<RoutePoint> alignedMedian = {
        roundedPoint(medianCoordinate(startXs), medianCoordinate(startYs)),
        roundedPoint(medianCoordinate(endXs), medianCoordinate(endYs)),
      };

      RenderedCarrierMetricPath path;
      path.id = carrierId;
      // Aligned endpoints keep reverse/parallel relationships attached to
      // the same two tables. rawAverage can average opposite orientations
      // into a floating segment that touches neither endpoint.
      path.points = alignedAverage;
      auto addStraightCandidate = [&](const std::vector<RoutePoint>& candidate) {
        if (candidate.size() != 2) return;
        bool duplicate = false;
        path.straightCandidates.forEach([&](const std::vector<RoutePoint>& existing) {
          if (
              std::abs(existing[0].x - candidate[0].x) < 0.005
              && std::abs(existing[0].y - candidate[0].y) < 0.005
              && std::abs(existing[1].x - candidate[1].x) < 0.005
              && std::abs(existing[1].y - candidate[1].y) < 0.005) {
            duplicate = true;
          }
        });
        if (duplicate) return;
        path.straightCandidates.push_back(candidate);
      };
      addStraightCandidate(rawAverage);
      addStraightCandidate(alignedAverage);
      addStraightCandidate(alignedMedian);
      for (const auto& [start, end] : alignedEndpoints) {
        const RoutePoint roundedStart = roundedPoint(start.x, start.y);
        const RoutePoint roundedEnd = roundedPoint(end.x, end.y);
        addStraightCandidate({roundedStart, roundedEnd});
        addStraightCandidate({roundedStart, alignedAverage[1]});
        addStraightCandidate({alignedAverage[0], roundedEnd});
      }
      for (const std::size_t edgeIndex : members) {
        path.memberEdgeIds.push_back(edges[edgeIndex].edgeId);
        path.endpointModelIds.insert(edges[edgeIndex].sourceModelId);
        path.endpointModelIds.insert(edges[edgeIndex].targetModelId);
      }
      renderedPaths.push_back(std::move(path));
      continue;
    }

    for (const std::size_t edgeIndex : members) {
      addRawPath(edgeIndex);
    }
  }

  // Match the canvas collision audit exactly. Both paths consume coordinates
  // rounded to two decimals, so a shared ten-unit margin preserves parity.
  constexpr double kRenderedCarrierVisualMargin = 10.0;
  std::vector<Rect> bundleRects;
  std::vector<std::vector<Rect>> bundleTileRects;
  bundleRects.reserve(metadata.leafBundles.size());
  bundleTileRects.reserve(metadata.leafBundles.size());
  if (!skipCarrier) {
    for (const LeafBundleRecord& bundle : metadata.leafBundles) {
      bundleRects.push_back(renderedLeafBundleRect(bundle, kRenderedCarrierVisualMargin));
      bundleTileRects.push_back(
        renderedLeafTileRects(bundle, kRenderedCarrierVisualMargin));
    }
  }

  struct RenderedCarrierCounts {
    std::size_t edgeCrossings = 0;
    std::size_t edgeNodeIntersections = 0;
    std::size_t bundleEdgeIntersections = 0;
    std::size_t routeSegments = 0;
    std::size_t occludedCrossings = 0;
  };

  auto measureRenderedPaths = [&](
      const std::vector<RenderedCarrierMetricPath>& paths) {
    RenderedCarrierCounts counts;
    for (std::size_t i = 0; i < paths.size(); ++i) {
      if (paths[i].points.size() < 2) continue;
      for (std::size_t j = i + 1; j < paths.size(); ++j) {
        if (paths[j].points.size() < 2) continue;
        // Proper intersection already excludes touches at the shared table
        // endpoint. Two incident straight edges can still cross again away
        // from that endpoint, and the canvas visibly counts that crossing.
        // Skipping the whole pair under-reported the exact same 1,357 paths by
        // 1,694 crossings on the preserved production scene.
        bool anyCross = false;
        for (std::size_t li = 1; li < paths[i].points.size() && !anyCross; ++li) {
          for (std::size_t rj = 1; rj < paths[j].points.size() && !anyCross; ++rj) {
            RoutePoint isect;
            if (properSegmentIntersection(
                paths[i].points[li - 1], paths[i].points[li],
                paths[j].points[rj - 1], paths[j].points[rj], isect)) {
              if (pointInOcclusion(isect)) {
                ++counts.occludedCrossings;
              } else {
                anyCross = true;
              }
            }
          }
        }
        if (anyCross) {
          ++counts.edgeCrossings;
        }
      }
    }

    for (const RenderedCarrierMetricPath& path : paths) {
      if (path.points.size() < 2) continue;
      for (std::size_t pointIndex = 1; pointIndex < path.points.size(); ++pointIndex) {
        const RoutePoint& start = path.points[pointIndex - 1];
        const RoutePoint& end = path.points[pointIndex];
        ++counts.routeSegments;

        for (const NodeRecord& node : nodes) {
          if (bundleAbsorbed.count(node.modelId)) continue;
          if (path.endpointModelIds.count(node.modelId)) continue;
          if (segmentIntersectsRect(
              start, end, nodeRect(node, attributes, kRenderedCarrierVisualMargin))) {
            ++counts.edgeNodeIntersections;
          }
        }

        for (std::size_t bi = 0; bi < bundleTileRects.size(); ++bi) {
          const LeafBundleRecord& bundle = metadata.leafBundles[bi];
          const std::vector<Rect>& tileRects = bundleTileRects[bi];
          for (std::size_t tileIndex = 0;
               tileIndex < tileRects.size()
                 && tileIndex < bundle.leafModelIds.size();
               ++tileIndex) {
            if (path.endpointModelIds.count(bundle.leafModelIds[tileIndex])) {
              continue;
            }
            if (segmentIntersectsRect(start, end, tileRects[tileIndex])) {
              ++counts.edgeNodeIntersections;
            }
          }
        }

        for (std::size_t bi = 0; bi < bundleRects.size(); ++bi) {
          if (path.endpointBundleIndices.count(bi)) continue;
          if (segmentIntersectsRect(start, end, bundleRects[bi])) {
            ++counts.bundleEdgeIntersections;
          }
        }
      }
    }
    return counts;
  };

  struct RenderedPathVisualCost {
    std::size_t edgeCrossings = 0;
    std::size_t edgeNodeIntersections = 0;
    std::size_t bundleEdgeIntersections = 0;

    std::size_t visual() const {
      return edgeCrossings
        + edgeNodeIntersections
        + bundleEdgeIntersections;
    }
  };

  auto renderedPathCost = [&]
      (const std::vector<RenderedCarrierMetricPath>& paths,
       std::size_t pathIndex,
       const std::vector<RoutePoint>& candidatePoints) {
    if (pathIndex >= paths.size() || candidatePoints.size() < 2) {
      return RenderedPathVisualCost{
        std::numeric_limits<std::size_t>::max() / 4,
        std::numeric_limits<std::size_t>::max() / 4,
        std::numeric_limits<std::size_t>::max() / 4,
      };
    }
    RenderedCarrierMetricPath candidatePath = paths[pathIndex];
    candidatePath.points = candidatePoints;
    auto entersEndpointInterior = [](
        const RoutePoint& endpoint,
        const RoutePoint& toward,
        const Rect& rect) {
      const double dx = toward.x - endpoint.x;
      const double dy = toward.y - endpoint.y;
      const double length = std::hypot(dx, dy);
      if (length < 0.01) return false;
      // Boundary ports are rounded to two decimals while rectangles retain
      // sub-pixel coordinates. Sampling one pixel along the outgoing segment
      // distinguishes a harmless boundary touch from choosing the far side
      // of a table and travelling through its body. The small inset tolerance
      // absorbs only that rounding difference; it cannot hide a real
      // traversal through the rectangle.
      const double step = std::min(1.0, length * 0.25) / length;
      const RoutePoint sample{
        endpoint.x + dx * step,
        endpoint.y + dy * step,
      };
      constexpr double kBoundaryRoundingTolerance = 0.02;
      return
        sample.x > rect.left + kBoundaryRoundingTolerance
        && sample.x < rect.right - kBoundaryRoundingTolerance
        && sample.y > rect.top + kBoundaryRoundingTolerance
        && sample.y < rect.bottom - kBoundaryRoundingTolerance;
    };
    if (
        candidatePath.hasStraightEndpointRects
        && (entersEndpointInterior(
              candidatePoints.front(), candidatePoints.back(),
              candidatePath.straightStartRect)
            || entersEndpointInterior(
              candidatePoints.back(), candidatePoints.front(),
              candidatePath.straightEndRect))) {
      // A port on the far side of an endpoint can look attractive when that
      // endpoint is excluded from obstacle scoring, but the resulting line
      // travels through its own table (or through sibling tiles inside its
      // leaf bundle) before exiting. Such a route is never renderable as a
      // collision-free straight carrier.
      return RenderedPathVisualCost{
        std::numeric_limits<std::size_t>::max() / 4,
        std::numeric_limits<std::size_t>::max() / 4,
        std::numeric_limits<std::size_t>::max() / 4,
      };
    }
    RenderedPathVisualCost cost;
    for (std::size_t otherIndex = 0; otherIndex < paths.size(); ++otherIndex) {
      if (
          otherIndex == pathIndex
          || paths[otherIndex].points.size() < 2) {
        continue;
      }
      bool anyCross = false;
      for (std::size_t left = 1;
           left < candidatePoints.size() && !anyCross; ++left) {
        for (std::size_t right = 1;
             right < paths[otherIndex].points.size() && !anyCross; ++right) {
          RoutePoint intersection;
          if (properSegmentIntersection(
              candidatePoints[left - 1], candidatePoints[left],
              paths[otherIndex].points[right - 1],
              paths[otherIndex].points[right], intersection)
              && !pointInOcclusion(intersection)) {
            anyCross = true;
          }
        }
      }
      if (anyCross) {
        ++cost.edgeCrossings;
      }
    }

    for (std::size_t pointIndex = 1;
         pointIndex < candidatePoints.size(); ++pointIndex) {
      const RoutePoint& start = candidatePoints[pointIndex - 1];
      const RoutePoint& end = candidatePoints[pointIndex];
      for (const NodeRecord& node : nodes) {
        if (
            bundleAbsorbed.count(node.modelId)
            || candidatePath.endpointModelIds.count(node.modelId)) {
          continue;
        }
        if (segmentIntersectsRect(
            start, end,
            nodeRect(node, attributes, kRenderedCarrierVisualMargin))) {
          ++cost.edgeNodeIntersections;
        }
      }
      for (std::size_t bundleIndex = 0;
           bundleIndex < bundleTileRects.size(); ++bundleIndex) {
        const LeafBundleRecord& bundle = metadata.leafBundles[bundleIndex];
        const std::vector<Rect>& tileRects = bundleTileRects[bundleIndex];
        for (std::size_t tileIndex = 0;
             tileIndex < tileRects.size()
               && tileIndex < bundle.leafModelIds.size();
             ++tileIndex) {
          if (candidatePath.endpointModelIds.count(
              bundle.leafModelIds[tileIndex])) {
            continue;
          }
          if (segmentIntersectsRect(start, end, tileRects[tileIndex])) {
            ++cost.edgeNodeIntersections;
          }
        }
      }
      for (std::size_t bundleIndex = 0;
           bundleIndex < bundleRects.size(); ++bundleIndex) {
        if (candidatePath.endpointBundleIndices.count(bundleIndex)) {
          continue;
        }
        if (segmentIntersectsRect(start, end, bundleRects[bundleIndex])) {
          ++cost.bundleEdgeIntersections;
        }
      }
    }
    return cost;
  };

  auto renderedPathVisualCost = [&]
      (const std::vector<RenderedCarrierMetricPath>& paths,
       std::size_t pathIndex,
       const std::vector<RoutePoint>& candidatePoints) {
    return renderedPathCost(paths, pathIndex, candidatePoints).visual();
  };

  const RenderedCarrierCounts preGeometryCounts =
    measureRenderedPaths(renderedPaths);
  std::size_t geometryMoves = 0;
  const bool disableWallClockBudgets = readBoolEnv(
    "DJERD_DISABLE_WALL_CLOCK_BUDGETS", false);
  const double geometryBudgetMs = readDoubleEnv(
    "DJERD_RENDERED_CARRIER_GEOMETRY_OPT_BUDGET_MS",
    5000.0,
    100.0,
    60000.0);
  const double effectiveGeometryBudgetMs = quiet
    ? std::min(
        geometryBudgetMs,
        readDoubleEnv(
          "DJERD_RENDERED_CARRIER_GEOMETRY_OPT_QUIET_BUDGET_MS",
          500.0,
          50.0,
          5000.0))
    : geometryBudgetMs;
  const auto geometryStarted = std::chrono::steady_clock::now();
  auto geometryBudgetExceeded = [&]() {
    return !disableWallClockBudgets
      && std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - geometryStarted).count()
        >= effectiveGeometryBudgetMs;
  };
  bool geometryBudgetHit = false;
  if (
      optimizeGeometry
      && readBoolEnv("DJERD_RENDERED_CARRIER_GEOMETRY_OPT_FINAL", false)) {
    const int geometryRounds = static_cast<int>(readDoubleEnv(
      "DJERD_RENDERED_CARRIER_GEOMETRY_OPT_ROUNDS", 4.0, 1.0, 12.0));
    for (int round = 0; round < geometryRounds; ++round) {
      if (geometryBudgetExceeded()) {
        geometryBudgetHit = true;
        break;
      }
      std::vector<std::pair<std::size_t, std::size_t>> order;
      for (std::size_t pathIndex = 0;
           pathIndex < renderedPaths.size(); ++pathIndex) {
        if (renderedPaths[pathIndex].straightCandidates.size() < 2) {
          continue;
        }
        order.push_back({
          renderedPathVisualCost(
            renderedPaths,
            pathIndex,
            renderedPaths[pathIndex].points),
          pathIndex,
        });
      }
      std::sort(
        order.begin(),
        order.end(),
        [&](const auto& left, const auto& right) {
          if (left.first != right.first) return left.first > right.first;
          return renderedPaths[left.second].id < renderedPaths[right.second].id;
        });
      std::size_t movedThisRound = 0;
      for (const auto& ranked : order) {
        if (geometryBudgetExceeded()) {
          geometryBudgetHit = true;
          break;
        }
        const std::size_t pathIndex = ranked.second;
        RenderedCarrierMetricPath& path = renderedPaths[pathIndex];
        std::size_t bestCost = renderedPathVisualCost(
          renderedPaths, pathIndex, path.points);
        std::vector<RoutePoint> bestPoints = path.points;
        bool improved = false;
        path.straightCandidates.forEach([&](const std::vector<RoutePoint>& candidate) {
          const std::size_t candidateCost = renderedPathVisualCost(
            renderedPaths, pathIndex, candidate);
          if (candidateCost < bestCost) {
            bestCost = candidateCost;
            bestPoints = candidate;
            improved = true;
          }
        });
        if (improved) {
          path.points = std::move(bestPoints);
          ++movedThisRound;
          ++geometryMoves;
        }
      }
      if (movedThisRound == 0) {
        break;
      }
      if (geometryBudgetHit) break;
    }
  }

  // Once the ordinary visual-cost descent has produced a low-crossing
  // drawing, spend only the remaining visual budget on removing carrier
  // segments that pass through rendered tables. This is deliberately a
  // second, lexicographic stage: treating an edge/node hit as interchangeable
  // with an edge/edge crossing allowed a visually acceptable total to retain
  // lines through table bodies. Every candidate remains a two-point segment
  // attached to the boundary of the same endpoint tables.
  std::size_t carrierNodeTargetMoves = 0;
  if (
      optimizeGeometry
      && optimizeStraightPorts
      && readBoolEnv("DJERD_RENDERED_CARRIER_NODE_TARGET_FINAL", false)) {
    const std::size_t edgeNodeTarget = static_cast<std::size_t>(readDoubleEnv(
      "DJERD_RENDERED_CARRIER_EDGE_NODE_TARGET", 0.0, 0.0, 1'000'000.0));
    const std::size_t bundleEdgeTarget = static_cast<std::size_t>(readDoubleEnv(
      "DJERD_RENDERED_CARRIER_BUNDLE_EDGE_TARGET", 0.0, 0.0, 1'000'000.0));
    const std::size_t visualTarget = static_cast<std::size_t>(readDoubleEnv(
      "DJERD_RENDERED_CARRIER_VISUAL_TARGET", 100.0, 0.0, 1'000'000.0));
    const int targetRounds = static_cast<int>(readDoubleEnv(
      "DJERD_RENDERED_CARRIER_NODE_TARGET_ROUNDS", 4.0, 1.0, 12.0));
    const int samplesPerSide = static_cast<int>(readDoubleEnv(
      "DJERD_RENDERED_CARRIER_NODE_TARGET_PORT_SAMPLES", 8.0, 2.0, 24.0));
    const bool exhaustiveTargetPorts = readBoolEnv(
      "DJERD_RENDERED_CARRIER_NODE_TARGET_EXHAUSTIVE_PORTS", true);
    const double configuredTargetBudgetMs = readDoubleEnv(
      "DJERD_RENDERED_CARRIER_NODE_TARGET_BUDGET_MS",
      5000.0,
      100.0,
      60000.0);
    const double targetBudgetMs = quiet
      ? std::min(
          configuredTargetBudgetMs,
          readDoubleEnv(
            "DJERD_RENDERED_CARRIER_NODE_TARGET_QUIET_BUDGET_MS",
            500.0,
            50.0,
            5000.0))
      : configuredTargetBudgetMs;
    const auto targetStarted = std::chrono::steady_clock::now();
    auto targetBudgetExceeded = [&]() {
      return !disableWallClockBudgets
        && std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - targetStarted).count()
          >= targetBudgetMs;
    };
    bool targetBudgetHit = false;

    auto denseBoundaryCandidates = [&](const Rect& rect) {
      std::vector<RoutePoint> candidates;
      candidates.reserve(static_cast<std::size_t>(samplesPerSide) * 4);
      auto addCandidate = [&](const RoutePoint& candidate) {
        for (const RoutePoint& existing : candidates) {
          if (
              std::abs(existing.x - candidate.x) < 0.005
              && std::abs(existing.y - candidate.y) < 0.005) {
            return;
          }
        }
        candidates.push_back(candidate);
      };
      for (int sample = 0; sample <= samplesPerSide; ++sample) {
        const double t = static_cast<double>(sample)
          / static_cast<double>(samplesPerSide);
        addCandidate(roundedRoutePoint(
          rect.left + rectWidth(rect) * t, rect.top));
        addCandidate(roundedRoutePoint(
          rect.right, rect.top + rectHeight(rect) * t));
        addCandidate(roundedRoutePoint(
          rect.right - rectWidth(rect) * t, rect.bottom));
        addCandidate(roundedRoutePoint(
          rect.left, rect.bottom - rectHeight(rect) * t));
      }
      return candidates;
    };

    RenderedCarrierCounts targetCounts = measureRenderedPaths(renderedPaths);
    const RenderedCarrierCounts beforeTargetCounts = targetCounts;
    auto obstacleExcess = [&](std::size_t edgeNode, std::size_t bundleEdge) {
      return (edgeNode > edgeNodeTarget ? edgeNode - edgeNodeTarget : 0)
        + (bundleEdge > bundleEdgeTarget
            ? bundleEdge - bundleEdgeTarget
            : 0);
    };
    for (
        int round = 0;
        round < targetRounds
          && obstacleExcess(
            targetCounts.edgeNodeIntersections,
            targetCounts.bundleEdgeIntersections) > 0;
        ++round) {
      if (targetBudgetExceeded()) {
        targetBudgetHit = true;
        break;
      }
      std::vector<std::pair<std::size_t, std::size_t>> order;
      order.reserve(renderedPaths.size());
      for (std::size_t pathIndex = 0;
           pathIndex < renderedPaths.size(); ++pathIndex) {
        const RenderedCarrierMetricPath& path = renderedPaths[pathIndex];
        if (path.points.size() != 2 || path.straightCandidates.empty()) continue;
        const RenderedPathVisualCost currentCost =
          renderedPathCost(renderedPaths, pathIndex, path.points);
        const std::size_t currentObstacleHits =
          currentCost.edgeNodeIntersections
          + currentCost.bundleEdgeIntersections;
        if (currentObstacleHits == 0) continue;
        order.push_back({currentObstacleHits, pathIndex});
      }
      std::sort(
        order.begin(),
        order.end(),
        [&](const auto& left, const auto& right) {
          if (left.first != right.first) return left.first > right.first;
          return renderedPaths[left.second].id < renderedPaths[right.second].id;
        });

      std::size_t movedThisRound = 0;
      for (const auto& ranked : order) {
        if (targetBudgetExceeded()) {
          targetBudgetHit = true;
          break;
        }
        const std::size_t pathIndex = ranked.second;
        RenderedCarrierMetricPath& path = renderedPaths[pathIndex];
        const RenderedPathVisualCost currentCost =
          renderedPathCost(renderedPaths, pathIndex, path.points);
        const std::size_t currentObstacleHits =
          currentCost.edgeNodeIntersections
          + currentCost.bundleEdgeIntersections;
        if (currentObstacleHits == 0) continue;

        std::vector<RoutePoint> bestPoints = path.points;
        RenderedPathVisualCost bestCost = currentCost;
        std::size_t bestGlobalObstacleExcess = obstacleExcess(
          targetCounts.edgeNodeIntersections,
          targetCounts.bundleEdgeIntersections);
        std::size_t bestGlobalEdgeNode = targetCounts.edgeNodeIntersections;
        std::size_t bestGlobalBundleEdge =
          targetCounts.bundleEdgeIntersections;
        std::size_t bestGlobalVisual =
          targetCounts.edgeCrossings
          + targetCounts.edgeNodeIntersections
          + targetCounts.bundleEdgeIntersections;

        auto considerCandidate = [&](const std::vector<RoutePoint>& candidate) {
          if (candidate.size() != 2) return;
          const RenderedPathVisualCost candidateCost =
            renderedPathCost(renderedPaths, pathIndex, candidate);
          const std::size_t candidateObstacleHits =
            candidateCost.edgeNodeIntersections
            + candidateCost.bundleEdgeIntersections;
          if (candidateObstacleHits >= currentObstacleHits) {
            return;
          }
          const std::size_t nextEdgeCrossings =
            targetCounts.edgeCrossings
            - currentCost.edgeCrossings
            + candidateCost.edgeCrossings;
          const std::size_t nextEdgeNode =
            targetCounts.edgeNodeIntersections
            - currentCost.edgeNodeIntersections
            + candidateCost.edgeNodeIntersections;
          const std::size_t nextBundleEdge =
            targetCounts.bundleEdgeIntersections
            - currentCost.bundleEdgeIntersections
            + candidateCost.bundleEdgeIntersections;
          const std::size_t nextVisual =
            nextEdgeCrossings + nextEdgeNode + nextBundleEdge;
          const std::size_t currentVisual =
            targetCounts.edgeCrossings
            + targetCounts.edgeNodeIntersections
            + targetCounts.bundleEdgeIntersections;
          // The hard target is a destination. While the current scene is over
          // target, accept only monotonically improving exact rendered moves;
          // do not demand that one boundary-port change solve the whole graph.
          if (nextVisual > currentVisual) return;
          const std::size_t nextObstacleExcess = obstacleExcess(
            nextEdgeNode, nextBundleEdge);
          if (
              nextObstacleExcess < bestGlobalObstacleExcess
              || (nextObstacleExcess == bestGlobalObstacleExcess
                  && nextVisual < bestGlobalVisual)) {
            bestPoints = candidate;
            bestCost = candidateCost;
            bestGlobalObstacleExcess = nextObstacleExcess;
            bestGlobalEdgeNode = nextEdgeNode;
            bestGlobalBundleEdge = nextBundleEdge;
            bestGlobalVisual = nextVisual;
          }
        };

        path.straightCandidates.forEach(considerCandidate);
        if (path.hasStraightEndpointRects) {
          const std::vector<RoutePoint> startCandidates =
            denseBoundaryCandidates(path.straightStartRect);
          const std::vector<RoutePoint> endCandidates =
            denseBoundaryCandidates(path.straightEndRect);
          if (exhaustiveTargetPorts) {
            for (const RoutePoint& start : startCandidates) {
              for (const RoutePoint& end : endCandidates) {
                considerCandidate({start, end});
              }
            }
          } else {
            // Coordinate descent retains exact candidate scoring while
            // avoiding the O(S²) boundary Cartesian product. Four ordered
            // sweeps also allow both endpoints to change in one carrier
            // update; the following round can refine it again.
            for (const RoutePoint& start : startCandidates) {
              considerCandidate({start, path.points[1]});
            }
            for (const RoutePoint& end : endCandidates) {
              considerCandidate({path.points[0], end});
            }
            RoutePoint fixedStart = bestPoints[0];
            for (const RoutePoint& end : endCandidates) {
              considerCandidate({fixedStart, end});
            }
            RoutePoint fixedEnd = bestPoints[1];
            for (const RoutePoint& start : startCandidates) {
              considerCandidate({start, fixedEnd});
            }
          }
        }

        if (
            bestGlobalObstacleExcess < obstacleExcess(
              targetCounts.edgeNodeIntersections,
              targetCounts.bundleEdgeIntersections)
            && (std::abs(bestPoints[0].x - path.points[0].x) >= 0.005
                || std::abs(bestPoints[0].y - path.points[0].y) >= 0.005
                || std::abs(bestPoints[1].x - path.points[1].x) >= 0.005
                || std::abs(bestPoints[1].y - path.points[1].y) >= 0.005)) {
          targetCounts.edgeCrossings =
            targetCounts.edgeCrossings
            - currentCost.edgeCrossings
            + bestCost.edgeCrossings;
          targetCounts.edgeNodeIntersections = bestGlobalEdgeNode;
          targetCounts.bundleEdgeIntersections = bestGlobalBundleEdge;
          path.points = std::move(bestPoints);
          ++movedThisRound;
          ++carrierNodeTargetMoves;
          ++geometryMoves;
        }
      }
      if (movedThisRound == 0) break;
      // Re-measure rather than trusting accumulated deltas; this also keeps
      // the hard-target stage honest if a future carrier metric changes.
      targetCounts = measureRenderedPaths(renderedPaths);
    }

    if (!quiet) {
      const std::size_t beforeVisual =
        beforeTargetCounts.edgeCrossings
        + beforeTargetCounts.edgeNodeIntersections
        + beforeTargetCounts.bundleEdgeIntersections;
      const std::size_t afterVisual =
        targetCounts.edgeCrossings
        + targetCounts.edgeNodeIntersections
        + targetCounts.bundleEdgeIntersections;
      std::fprintf(stderr,
        "[rendered-carrier-node-target] moved=%zu straight carriers, "
        "visual=%zu -> %zu/%zu, edgeCross=%zu -> %zu, edgeNode=%zu -> %zu/%zu, "
        "bundleEdge=%zu -> %zu/%zu.\n",
        carrierNodeTargetMoves,
        beforeVisual, afterVisual, visualTarget,
        beforeTargetCounts.edgeCrossings, targetCounts.edgeCrossings,
        beforeTargetCounts.edgeNodeIntersections,
        targetCounts.edgeNodeIntersections, edgeNodeTarget,
        beforeTargetCounts.bundleEdgeIntersections,
        targetCounts.bundleEdgeIntersections, bundleEdgeTarget);
      if (targetBudgetHit) {
        std::fprintf(stderr,
          "[rendered-carrier-node-target] budgetHit=1 budgetMs=%.0f; "
          "kept monotonic partial result.\n",
          targetBudgetMs);
      }
    }
  }

  metadata.renderedCarrierRoutes.clear();
  for (const RenderedCarrierMetricPath& path : renderedPaths) {
    if (path.straightCandidates.empty() || path.points.size() < 2) {
      continue;
    }
    metadata.renderedCarrierRoutes.push_back({
      path.id,
      path.memberEdgeIds,
      path.points,
    });
  }
  std::sort(
    metadata.renderedCarrierRoutes.begin(),
    metadata.renderedCarrierRoutes.end(),
    [](const RenderedCarrierRouteRecord& left,
       const RenderedCarrierRouteRecord& right) {
      return left.carrierId < right.carrierId;
    });

  RenderedCarrierCounts renderedCounts = measureRenderedPaths(renderedPaths);
  if (
      !quiet
      && readBoolEnv("DJERD_RENDERED_CARRIER_DIAGNOSTICS", false)) {
    for (const RenderedCarrierMetricPath& path : renderedPaths) {
      if (path.points.size() < 2) continue;
      for (std::size_t pointIndex = 1;
           pointIndex < path.points.size(); ++pointIndex) {
        const RoutePoint& start = path.points[pointIndex - 1];
        const RoutePoint& end = path.points[pointIndex];
        for (std::size_t bundleIndex = 0;
             bundleIndex < bundleRects.size(); ++bundleIndex) {
          if (path.endpointBundleIndices.count(bundleIndex)) continue;
          if (!segmentIntersectsRect(
              start, end, bundleRects[bundleIndex])) {
            continue;
          }
          std::fprintf(stderr,
            "[rendered-carrier-bundle-edge] carrier=%s bundle=%zu parent=%s "
            "segment=(%.2f,%.2f)->(%.2f,%.2f).\n",
            path.id.c_str(),
            bundleIndex,
            metadata.leafBundles[bundleIndex].parentModelId.c_str(),
            start.x, start.y, end.x, end.y);
        }
      }
    }
  }
  if (!quiet && geometryMoves > 0) {
    const std::size_t beforeVisual =
      preGeometryCounts.edgeCrossings
      + preGeometryCounts.edgeNodeIntersections
      + preGeometryCounts.bundleEdgeIntersections;
    const std::size_t afterVisual =
      renderedCounts.edgeCrossings
      + renderedCounts.edgeNodeIntersections
      + renderedCounts.bundleEdgeIntersections;
    std::fprintf(stderr,
      "[rendered-carrier-geometry-opt] moved=%zu straight carriers, "
      "visual=%zu -> %zu, edgeCross=%zu -> %zu, edgeNode=%zu -> %zu, "
      "bundleEdge=%zu -> %zu.\n",
      geometryMoves,
      beforeVisual, afterVisual,
      preGeometryCounts.edgeCrossings, renderedCounts.edgeCrossings,
      preGeometryCounts.edgeNodeIntersections,
      renderedCounts.edgeNodeIntersections,
      preGeometryCounts.bundleEdgeIntersections,
      renderedCounts.bundleEdgeIntersections);
  }
  if (!quiet && geometryBudgetHit) {
    std::fprintf(stderr,
      "[rendered-carrier-geometry-opt] budgetHit=1 budgetMs=%.0f; "
      "kept monotonic partial result.\n",
      effectiveGeometryBudgetMs);
  }

  if (!quiet) {
    std::fprintf(stderr,
      "[rendered-carrier-metrics-final] rawCross=%zu visibleEdges=%zu "
      "edgeCross=%zu edgeNode=%zu bundleEdge=%zu routeSegments=%zu "
      "(occluded=%zu).\n",
      totalRouteCrossings,
      renderedPaths.size(),
      renderedCounts.edgeCrossings,
      renderedCounts.edgeNodeIntersections,
      renderedCounts.bundleEdgeIntersections,
      renderedCounts.routeSegments,
      renderedCounts.occludedCrossings);
  }

  quality.edgeCrossings = renderedCounts.edgeCrossings;
  quality.edgeNodeIntersections = renderedCounts.edgeNodeIntersections;
  quality.bundleEdgeIntersections = renderedCounts.bundleEdgeIntersections;
  if (skipCarrier) {
    // Direct canvas scenes expose bundle membership only as selection metadata;
    // no bundle frame participates in the initial visual-conflict total.
    quality.bundleNodeOverlaps = 0;
  }
  quality.routeSegments = renderedCounts.routeSegments;
  quality.visualCrossings =
    quality.edgeCrossings
    + quality.edgeNodeIntersections
    + quality.nodeOverlaps
    + quality.bundleEdgeIntersections
    + quality.bundleNodeOverlaps;
  return true;
}

bool clearRenderedCarrierNodeIntersectionsIfRequested(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes,
  const std::unordered_map<std::string, std::string>& clusterByModelIdFull,
  LayoutRunMetadata& metadata) {
  if (
      !readBoolEnv("DJERD_RENDERED_CARRIER_NODE_CLEAR_FINAL", false)
      || nodes.empty()
      || edges.empty()) {
    return false;
  }

  const std::size_t edgeNodeTarget = static_cast<std::size_t>(readDoubleEnv(
    "DJERD_RENDERED_CARRIER_EDGE_NODE_TARGET", 0.0, 0.0, 1'000'000.0));
  const std::size_t visualTarget = static_cast<std::size_t>(readDoubleEnv(
    "DJERD_RENDERED_CARRIER_VISUAL_TARGET", 100.0, 0.0, 1'000'000.0));
  const int rounds = static_cast<int>(readDoubleEnv(
    "DJERD_RENDERED_CARRIER_NODE_CLEAR_ROUNDS", 16.0, 1.0, 16.0));
  const int directions = static_cast<int>(readDoubleEnv(
    "DJERD_RENDERED_CARRIER_NODE_CLEAR_DIRECTIONS", 24.0, 8.0, 64.0));
  const double maxShift = readDoubleEnv(
    "DJERD_RENDERED_CARRIER_NODE_CLEAR_MAX_SHIFT", 1200.0, 40.0, 12000.0);
  const std::size_t blockerBatchSize = static_cast<std::size_t>(readDoubleEnv(
    "DJERD_RENDERED_CARRIER_NODE_CLEAR_BATCH_SIZE", 8.0, 1.0, 4096.0));
  const std::size_t followupBlockerBatchSize =
    static_cast<std::size_t>(readDoubleEnv(
      "DJERD_RENDERED_CARRIER_NODE_CLEAR_FOLLOWUP_BATCH_SIZE",
      8.0,
      1.0,
      4096.0));
  const std::size_t minBlockerBatchSize =
    static_cast<std::size_t>(readDoubleEnv(
      "DJERD_RENDERED_CARRIER_NODE_CLEAR_MIN_BATCH_SIZE",
      1.0,
      1.0,
      4096.0));
  const double clearBudgetMs = readDoubleEnv(
    "DJERD_RENDERED_CARRIER_NODE_CLEAR_BUDGET_MS",
    12000.0,
    100.0,
    60000.0);
  const std::size_t maxEfficientVisualDebt =
    static_cast<std::size_t>(readDoubleEnv(
      "DJERD_RENDERED_CARRIER_NODE_CLEAR_VISUAL_SLACK",
      32.0,
      0.0,
      10000.0));
  const std::size_t minCollisionGainPerVisualDebt =
    static_cast<std::size_t>(readDoubleEnv(
      "DJERD_RENDERED_CARRIER_NODE_CLEAR_MIN_GAIN_PER_VISUAL_DEBT",
      8.0,
      1.0,
      1000.0));
  const double bboxTargetB = readDoubleEnv(
    "DJERD_RENDERED_CARRIER_NODE_CLEAR_BBOX_TARGET_B", 1.0, 0.0, 1000.0);
  const double bboxTolerance = readDoubleEnv(
    "DJERD_RENDERED_CARRIER_NODE_CLEAR_BBOX_TOLERANCE", 1.02, 1.0, 2.0);
  constexpr double kCarrierNodeMargin = 10.0;
  const bool directScene = readBoolEnv("DJERD_NO_CARRIER_CROSS", false);

  auto reroute = [&]() {
    routes = routeAllEdgesStraight(edges, attributes);
    recomputeLeafBundleBboxesFromNodes(metadata.leafBundles, nodes, attributes);
  };
  auto measure = [&](bool optimizeGeometry) {
    std::vector<std::vector<std::string>> ignoredIdsByEdge;
    std::size_t rawCrossings = 0;
    (void)detectRouteCrossings(
      edges, routes, ignoredIdsByEdge, rawCrossings);
    LayoutQualityMetrics quality = measureLayoutQuality(
      nodes,
      edges,
      routes,
      attributes,
      directScene ? nullptr : &metadata.leafBundles,
      &metadata.clusterByModelId);
    quality.edgeCrossings = rawCrossings;
    if (!applyRenderedCarrierMetricsIfRequested(
        nodes,
        edges,
        routes,
        attributes,
        clusterByModelIdFull,
        metadata,
        quality,
        rawCrossings,
        true,
        optimizeGeometry)) {
      quality.visualCrossings =
        quality.edgeCrossings
        + quality.edgeNodeIntersections
        + quality.nodeOverlaps
        + quality.bundleEdgeIntersections
        + quality.bundleNodeOverlaps;
    }
    return quality;
  };

  reroute();
  // Establish one optimized carrier scene. A moved-node trial must be scored
  // with the same port optimizer: applyRenderedCarrierMetricsIfRequested()
  // rebuilds every carrier from the changed endpoint rectangles, so an
  // audit-only rebuild would silently replace the optimized baseline with raw
  // default ports and make even a one-node move incomparable. Unchanged-scene
  // audits elsewhere remain pure.
  LayoutQualityMetrics currentQuality = measure(true);
  const LayoutQualityMetrics initialQuality = currentQuality;
  if (currentQuality.edgeNodeIntersections <= edgeNodeTarget) {
    return false;
  }
  const auto clearStarted = std::chrono::steady_clock::now();
  auto clearBudgetExceeded = [&]() {
    return std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - clearStarted).count()
      >= clearBudgetMs;
  };
  bool clearBudgetHit = false;

  std::unordered_map<std::string, std::size_t> edgeIndexById;
  edgeIndexById.reserve(edges.size());
  for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
    edgeIndexById[edges[edgeIndex].edgeId] = edgeIndex;
  }
  const std::unordered_set<std::string> leafTiles = directScene
    ? std::unordered_set<std::string>{}
    : renderedLeafTileIds(metadata.leafBundles);
  std::unordered_map<std::string, std::size_t> nodeIndexByModelId;
  nodeIndexByModelId.reserve(nodes.size());
  for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
    nodeIndexByModelId[nodes[nodeIndex].modelId] = nodeIndex;
  }
  std::vector<std::vector<std::size_t>> bundleLeafNodeIndices(
    directScene ? 0 : metadata.leafBundles.size());
  for (std::size_t bundleIndex = 0;
       bundleIndex < bundleLeafNodeIndices.size(); ++bundleIndex) {
    for (const std::string& leafModelId
         : metadata.leafBundles[bundleIndex].leafModelIds) {
      auto nodeIt = nodeIndexByModelId.find(leafModelId);
      if (nodeIt != nodeIndexByModelId.end()) {
        bundleLeafNodeIndices[bundleIndex].push_back(nodeIt->second);
      }
    }
  }

  struct CarrierNodeClearPath {
    std::vector<RoutePoint> points;
    std::unordered_set<std::string> endpointModelIds;
  };
  auto buildCarrierPaths = [&]() {
    std::vector<CarrierNodeClearPath> paths;
    paths.reserve(metadata.renderedCarrierRoutes.size());
    for (const RenderedCarrierRouteRecord& carrier
         : metadata.renderedCarrierRoutes) {
      if (carrier.points.size() < 2) continue;
      CarrierNodeClearPath path;
      path.points = carrier.points;
      for (const std::string& edgeId : carrier.memberEdgeIds) {
        auto edgeIt = edgeIndexById.find(edgeId);
        if (edgeIt == edgeIndexById.end()) continue;
        path.endpointModelIds.insert(
          edges[edgeIt->second].sourceModelId);
        path.endpointModelIds.insert(
          edges[edgeIt->second].targetModelId);
      }
      if (carrier.carrierId.size() > 2 && carrier.carrierId[0] == 'B') {
        const std::size_t separator = carrier.carrierId.find('|');
        if (separator != std::string::npos) {
          try {
            const std::size_t bundleIndex = static_cast<std::size_t>(
              std::stoull(carrier.carrierId.substr(1, separator - 1)));
            if (bundleIndex < metadata.leafBundles.size()) {
              const LeafBundleRecord& bundle =
                metadata.leafBundles[bundleIndex];
              path.endpointModelIds.insert(bundle.parentModelId);
              for (const std::string& leaf : bundle.leafModelIds) {
                path.endpointModelIds.insert(leaf);
              }
              for (const std::string& root : bundle.sharedRootModelIds) {
                path.endpointModelIds.insert(root);
              }
            }
          } catch (const std::exception&) {
          }
        }
      }
      paths.push_back(std::move(path));
    }
    return paths;
  };

  auto rectAt = [&](std::size_t nodeIndex, double centerX, double centerY) {
    const NodeRecord& node = nodes[nodeIndex];
    const double halfWidth = attributes.width(node.handle) / 2.0;
    const double halfHeight = attributes.height(node.handle) / 2.0;
    return Rect{
      centerY + halfHeight,
      centerX - halfWidth,
      centerX + halfWidth,
      centerY - halfHeight,
    };
  };
  auto expandedRect = [](const Rect& rect, double margin) {
    return Rect{
      rect.bottom + margin,
      rect.left - margin,
      rect.right + margin,
      rect.top - margin,
    };
  };

  std::size_t acceptedMoves = 0;
  int completedRounds = 0;
  std::size_t activeFollowupBatchSize = followupBlockerBatchSize;
  std::unordered_set<std::string> deferredNodeModelIds;
  std::unordered_set<std::size_t> deferredBundleIndices;
  for (
      int round = 0;
      round < rounds
        && currentQuality.edgeNodeIntersections > edgeNodeTarget;
      ++round) {
    if (clearBudgetExceeded()) {
      clearBudgetHit = true;
      break;
    }
    const std::vector<CarrierNodeClearPath> carrierPaths = buildCarrierPaths();
    if (carrierPaths.empty()) break;

    // Candidate scoring used to scan every rendered carrier for every trial
    // card position. On a 1,300-carrier scene that made one 128-card batch
    // consume the entire final budget. Index straight segments by every grid
    // cell they actually traverse; a card then tests only nearby segments and
    // still runs the exact segment/rectangle predicate on every hit.
    struct IndexedCarrierSegment {
      RoutePoint start;
      RoutePoint end;
      std::size_t pathIndex;
    };
    constexpr double kCarrierClearGridCell = 512.0;
    auto carrierClearCell = [&](double value) {
      return static_cast<std::int32_t>(
        std::floor(value / kCarrierClearGridCell));
    };
    auto carrierClearCellKey = [](std::int32_t x, std::int32_t y) {
      return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32)
        | static_cast<std::uint32_t>(y);
    };
    std::vector<IndexedCarrierSegment> indexedCarrierSegments;
    indexedCarrierSegments.reserve(carrierPaths.size());
    std::unordered_map<std::uint64_t, std::vector<std::size_t>>
      carrierSegmentsByCell;
    auto addCarrierSegmentCell = [&]
        (std::size_t segmentIndex, std::int32_t cellX, std::int32_t cellY) {
      carrierSegmentsByCell[carrierClearCellKey(cellX, cellY)]
        .push_back(segmentIndex);
    };
    for (std::size_t pathIndex = 0;
         pathIndex < carrierPaths.size(); ++pathIndex) {
      const CarrierNodeClearPath& path = carrierPaths[pathIndex];
      for (std::size_t pointIndex = 1;
           pointIndex < path.points.size(); ++pointIndex) {
        const RoutePoint start = path.points[pointIndex - 1];
        const RoutePoint end = path.points[pointIndex];
        const std::size_t segmentIndex = indexedCarrierSegments.size();
        indexedCarrierSegments.push_back({start, end, pathIndex});

        std::int32_t cellX = carrierClearCell(start.x);
        std::int32_t cellY = carrierClearCell(start.y);
        const std::int32_t endCellX = carrierClearCell(end.x);
        const std::int32_t endCellY = carrierClearCell(end.y);
        const int stepX = end.x > start.x ? 1 : (end.x < start.x ? -1 : 0);
        const int stepY = end.y > start.y ? 1 : (end.y < start.y ? -1 : 0);
        const double deltaX = end.x - start.x;
        const double deltaY = end.y - start.y;
        const double infinity = std::numeric_limits<double>::infinity();
        const double nextBoundaryX = stepX > 0
          ? (static_cast<double>(cellX) + 1.0) * kCarrierClearGridCell
          : static_cast<double>(cellX) * kCarrierClearGridCell;
        const double nextBoundaryY = stepY > 0
          ? (static_cast<double>(cellY) + 1.0) * kCarrierClearGridCell
          : static_cast<double>(cellY) * kCarrierClearGridCell;
        double tMaxX = stepX == 0
          ? infinity
          : (nextBoundaryX - start.x) / deltaX;
        double tMaxY = stepY == 0
          ? infinity
          : (nextBoundaryY - start.y) / deltaY;
        const double tDeltaX = stepX == 0
          ? infinity
          : kCarrierClearGridCell / std::abs(deltaX);
        const double tDeltaY = stepY == 0
          ? infinity
          : kCarrierClearGridCell / std::abs(deltaY);

        addCarrierSegmentCell(segmentIndex, cellX, cellY);
        std::size_t guard = 0;
        while (
            (cellX != endCellX || cellY != endCellY)
            && guard++ < 1'000'000) {
          if (std::abs(tMaxX - tMaxY) <= 1e-12) {
            // A line crossing a cell corner intersects the closed cells on
            // both sides. Add those side cells as well so inclusive browser
            // rectangle hits cannot be lost at an exact grid boundary.
            if (stepX != 0) {
              addCarrierSegmentCell(
                segmentIndex, cellX + stepX, cellY);
            }
            if (stepY != 0) {
              addCarrierSegmentCell(
                segmentIndex, cellX, cellY + stepY);
            }
            cellX += stepX;
            cellY += stepY;
            tMaxX += tDeltaX;
            tMaxY += tDeltaY;
          } else if (tMaxX < tMaxY) {
            cellX += stepX;
            tMaxX += tDeltaX;
          } else {
            cellY += stepY;
            tMaxY += tDeltaY;
          }
          addCarrierSegmentCell(segmentIndex, cellX, cellY);
        }
      }
    }
    std::vector<std::uint32_t> carrierSegmentVisit(
      indexedCarrierSegments.size(), 0);
    const bool useSpatialCandidateIndex = carrierPaths.size() > 256;
    std::uint32_t carrierSegmentVisitToken = 0;
    auto forEachCarrierSegmentNear = [&]
        (const Rect& rect, const auto& visitor) {
      if (++carrierSegmentVisitToken == 0) {
        std::fill(
          carrierSegmentVisit.begin(), carrierSegmentVisit.end(), 0);
        carrierSegmentVisitToken = 1;
      }
      const std::int32_t minCellX = carrierClearCell(
        std::min(rect.left, rect.right));
      const std::int32_t maxCellX = carrierClearCell(
        std::max(rect.left, rect.right));
      const std::int32_t minCellY = carrierClearCell(
        std::min(rect.top, rect.bottom));
      const std::int32_t maxCellY = carrierClearCell(
        std::max(rect.top, rect.bottom));
      for (std::int32_t cellY = minCellY;
           cellY <= maxCellY; ++cellY) {
        for (std::int32_t cellX = minCellX;
             cellX <= maxCellX; ++cellX) {
          auto cellIt = carrierSegmentsByCell.find(
            carrierClearCellKey(cellX, cellY));
          if (cellIt == carrierSegmentsByCell.end()) continue;
          for (const std::size_t segmentIndex : cellIt->second) {
            if (
                segmentIndex >= carrierSegmentVisit.size()
                || carrierSegmentVisit[segmentIndex]
                  == carrierSegmentVisitToken) {
              continue;
            }
            carrierSegmentVisit[segmentIndex] = carrierSegmentVisitToken;
            visitor(indexedCarrierSegments[segmentIndex]);
          }
        }
      }
    };

    struct HitSegment {
      RoutePoint start;
      RoutePoint end;
    };
    std::vector<std::vector<HitSegment>> hitsByNode(nodes.size());
    std::vector<std::vector<HitSegment>> hitsByBundle(
      directScene ? 0 : metadata.leafBundles.size());
    for (const CarrierNodeClearPath& path : carrierPaths) {
      for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
        const NodeRecord& node = nodes[nodeIndex];
        if (
            leafTiles.count(node.modelId)
            || path.endpointModelIds.count(node.modelId)) {
          continue;
        }
        const Rect obstacle = nodeRect(
          node, attributes, kCarrierNodeMargin);
        for (std::size_t pointIndex = 1;
             pointIndex < path.points.size(); ++pointIndex) {
          if (segmentIntersectsRect(
              path.points[pointIndex - 1],
              path.points[pointIndex],
              obstacle)) {
            hitsByNode[nodeIndex].push_back({
              path.points[pointIndex - 1],
              path.points[pointIndex],
            });
          }
        }
      }
      for (std::size_t bundleIndex = 0;
           bundleIndex < hitsByBundle.size(); ++bundleIndex) {
        const LeafBundleRecord& bundle = metadata.leafBundles[bundleIndex];
        const std::vector<Rect> tileRects = renderedLeafTileRects(
          bundle,
          kCarrierNodeMargin);
        for (std::size_t tileIndex = 0;
             tileIndex < tileRects.size()
               && tileIndex < bundle.leafModelIds.size();
             ++tileIndex) {
          if (path.endpointModelIds.count(bundle.leafModelIds[tileIndex])) {
            continue;
          }
          for (std::size_t pointIndex = 1;
               pointIndex < path.points.size(); ++pointIndex) {
            if (segmentIntersectsRect(
                path.points[pointIndex - 1],
                path.points[pointIndex],
                tileRects[tileIndex])) {
              hitsByBundle[bundleIndex].push_back({
                path.points[pointIndex - 1],
                path.points[pointIndex],
              });
            }
          }
        }
      }
    }

    std::vector<std::pair<std::size_t, std::size_t>> blockerOrder;
    for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
      if (!hitsByNode[nodeIndex].empty()) {
        blockerOrder.push_back({hitsByNode[nodeIndex].size(), nodeIndex});
      }
    }
    std::sort(
      blockerOrder.begin(),
      blockerOrder.end(),
      [&](const auto& left, const auto& right) {
        if (left.first != right.first) return left.first > right.first;
        return nodes[left.second].modelId < nodes[right.second].modelId;
      });
    std::vector<std::pair<std::size_t, std::size_t>> bundleBlockerOrder;
    for (std::size_t bundleIndex = 0;
         bundleIndex < hitsByBundle.size(); ++bundleIndex) {
      if (!hitsByBundle[bundleIndex].empty()) {
        bundleBlockerOrder.push_back({
          hitsByBundle[bundleIndex].size(),
          bundleIndex,
        });
      }
    }
    std::sort(
      bundleBlockerOrder.begin(),
      bundleBlockerOrder.end(),
      [&](const auto& left, const auto& right) {
        if (left.first != right.first) return left.first > right.first;
        return metadata.leafBundles[left.second].parentModelId
          < metadata.leafBundles[right.second].parentModelId;
      });
    if (blockerOrder.empty() && bundleBlockerOrder.empty()) break;

    std::vector<std::pair<double, double>> savedPositions;
    savedPositions.reserve(nodes.size());
    for (const NodeRecord& node : nodes) {
      savedPositions.emplace_back(
        attributes.x(node.handle), attributes.y(node.handle));
    }
    const std::vector<std::vector<RoutePoint>> savedRoutes = routes;
    const LayoutRunMetadata savedMetadata = metadata;
    const Rect settledBounds = graphNodeBounds(nodes, attributes);
    std::size_t processedBlockers = 0;
    // Small fixtures can safely solve every blocker in one exact batch and
    // should not inherit the production graph's conservative batch cap. The
    // latter is needed only once carrier scoring becomes quadratic enough for
    // a broad move to create long-edge crossing debt.
    const std::size_t batchLimit = carrierPaths.size() <= 256
      ? nodes.size() + metadata.leafBundles.size()
      : (round == 0
          ? blockerBatchSize
          : std::min(blockerBatchSize, activeFollowupBatchSize));

    auto candidateHasClearance = [&](std::size_t movingIndex, const Rect& candidate) {
      const Rect expandedCandidate = expandForRenderedNodeClearance(candidate);
      for (std::size_t otherIndex = 0;
           otherIndex < nodes.size(); ++otherIndex) {
        if (
            otherIndex == movingIndex
            || leafTiles.count(nodes[otherIndex].modelId)) {
          continue;
        }
        if (rectsOverlap(
            expandedCandidate,
            expandForRenderedNodeClearance(
              nodeRect(nodes[otherIndex], attributes)))) {
          return false;
        }
      }
      if (!directScene) {
        for (const LeafBundleRecord& bundle : metadata.leafBundles) {
          if (rectsOverlap(
              expandedCandidate,
              expandForRenderedNodeClearance(
                renderedLeafBundleRect(bundle)))) {
            return false;
          }
        }
      }
      return true;
    };
    auto candidateHitCount = [&]
        (std::size_t movingIndex, const Rect& rawCandidate) {
      const Rect candidate = expandedRect(
        rawCandidate, kCarrierNodeMargin);
      std::size_t hits = 0;
      if (!useSpatialCandidateIndex) {
        for (const CarrierNodeClearPath& path : carrierPaths) {
          if (path.endpointModelIds.count(nodes[movingIndex].modelId)) continue;
          for (std::size_t pointIndex = 1;
               pointIndex < path.points.size(); ++pointIndex) {
            if (segmentIntersectsRect(
                path.points[pointIndex - 1],
                path.points[pointIndex],
                candidate)) {
              ++hits;
            }
          }
        }
        return hits;
      }
      forEachCarrierSegmentNear(
        candidate,
        [&](const IndexedCarrierSegment& segment) {
          const CarrierNodeClearPath& path =
            carrierPaths[segment.pathIndex];
          if (path.endpointModelIds.count(nodes[movingIndex].modelId)) return;
          if (segmentIntersectsRect(
              segment.start, segment.end, candidate)) {
            ++hits;
          }
        });
      return hits;
    };
    auto candidateBundleHasClearance = [&]
        (std::size_t movingBundleIndex, const Rect& candidate) {
      const Rect expandedCandidate = expandForRenderedNodeClearance(candidate);
      for (const NodeRecord& node : nodes) {
        if (leafTiles.count(node.modelId)) continue;
        if (rectsOverlap(
            expandedCandidate,
            expandForRenderedNodeClearance(nodeRect(node, attributes)))) {
          return false;
        }
      }
      for (std::size_t otherBundleIndex = 0;
           otherBundleIndex < metadata.leafBundles.size(); ++otherBundleIndex) {
        if (otherBundleIndex == movingBundleIndex) continue;
        if (rectsOverlap(
            expandedCandidate,
            expandForRenderedNodeClearance(renderedLeafBundleRect(
              metadata.leafBundles[otherBundleIndex])))) {
          return false;
        }
      }
      return true;
    };
    auto candidateBundleHitCount = [&]
        (std::size_t bundleIndex, double dx, double dy) {
      if (bundleIndex >= metadata.leafBundles.size()) {
        return std::numeric_limits<std::size_t>::max();
      }
      const LeafBundleRecord& bundle = metadata.leafBundles[bundleIndex];
      std::vector<Rect> tileRects = renderedLeafTileRects(
        bundle,
        kCarrierNodeMargin);
      for (Rect& rect : tileRects) {
        rect.left += dx;
        rect.right += dx;
        rect.top += dy;
        rect.bottom += dy;
      }
      std::size_t hits = 0;
      if (!useSpatialCandidateIndex) {
        for (const CarrierNodeClearPath& path : carrierPaths) {
          for (std::size_t tileIndex = 0;
               tileIndex < tileRects.size()
                 && tileIndex < bundle.leafModelIds.size();
               ++tileIndex) {
            if (path.endpointModelIds.count(bundle.leafModelIds[tileIndex])) {
              continue;
            }
            for (std::size_t pointIndex = 1;
                 pointIndex < path.points.size(); ++pointIndex) {
              if (segmentIntersectsRect(
                  path.points[pointIndex - 1],
                  path.points[pointIndex],
                  tileRects[tileIndex])) {
                ++hits;
              }
            }
          }
        }
        return hits;
      }
      for (std::size_t tileIndex = 0;
           tileIndex < tileRects.size()
             && tileIndex < bundle.leafModelIds.size();
           ++tileIndex) {
        forEachCarrierSegmentNear(
          tileRects[tileIndex],
          [&](const IndexedCarrierSegment& segment) {
            const CarrierNodeClearPath& path =
              carrierPaths[segment.pathIndex];
            if (path.endpointModelIds.count(bundle.leafModelIds[tileIndex])) {
              return;
            }
            if (segmentIntersectsRect(
                segment.start, segment.end, tileRects[tileIndex])) {
              ++hits;
            }
          });
      }
      return hits;
    };

    std::size_t movedThisRound = 0;
    std::vector<std::string> movedNodeModelIdsThisRound;
    std::vector<std::size_t> movedBundleIndicesThisRound;
    for (const auto& ranked : blockerOrder) {
      const std::size_t nodeIndex = ranked.second;
      if (deferredNodeModelIds.count(nodes[nodeIndex].modelId)) {
        continue;
      }
      if (
          processedBlockers >= batchLimit
          || clearBudgetExceeded()) {
        clearBudgetHit = clearBudgetExceeded();
        break;
      }
      ++processedBlockers;
      const NodeRecord& node = nodes[nodeIndex];
      const double originX = attributes.x(node.handle);
      const double originY = attributes.y(node.handle);
      const Rect originRect = rectAt(nodeIndex, originX, originY);
      const std::size_t originHits =
        candidateHitCount(nodeIndex, originRect);
      if (originHits == 0) continue;

      struct PositionCandidate {
        double x;
        double y;
      };
      std::vector<PositionCandidate> candidates;
      candidates.reserve(
        hitsByNode[nodeIndex].size() * 4
        + static_cast<std::size_t>(directions) * 7);
      auto addCandidate = [&](double x, double y) {
        const double dx = x - originX;
        const double dy = y - originY;
        if (dx * dx + dy * dy > maxShift * maxShift + 1e-6) return;
        const Rect candidateRect = rectAt(nodeIndex, x, y);
        // Do not expand an already compliant BBOX just to clear a carrier.
        if (
            candidateRect.left < settledBounds.left - 1e-6
            || candidateRect.right > settledBounds.right + 1e-6
            || candidateRect.top < settledBounds.top - 1e-6
            || candidateRect.bottom > settledBounds.bottom + 1e-6) {
          return;
        }
        candidates.push_back({x, y});
      };

      const double halfWidth = attributes.width(node.handle) / 2.0;
      const double halfHeight = attributes.height(node.handle) / 2.0;
      for (const HitSegment& hit : hitsByNode[nodeIndex]) {
        const double dx = hit.end.x - hit.start.x;
        const double dy = hit.end.y - hit.start.y;
        const double length = std::hypot(dx, dy);
        if (length < 1e-6) continue;
        const double nx = -dy / length;
        const double ny = dx / length;
        const double signedDistance =
          (originX - hit.start.x) * nx
          + (originY - hit.start.y) * ny;
        const double projectedRadius =
          std::abs(nx) * (halfWidth + kCarrierNodeMargin)
          + std::abs(ny) * (halfHeight + kCarrierNodeMargin)
          + 2.0;
        for (const double sign : {-1.0, 1.0}) {
          const double shift = sign * projectedRadius - signedDistance;
          addCandidate(originX + nx * shift, originY + ny * shift);
          addCandidate(
            originX + nx * shift * 1.35,
            originY + ny * shift * 1.35);
        }
      }

      std::size_t bestHits = originHits;
      double bestDistanceSquared = std::numeric_limits<double>::infinity();
      PositionCandidate best{originX, originY};
      auto evaluateCandidates = [&](std::size_t begin, bool stopAtZero) {
        for (std::size_t candidateIndex = begin;
             candidateIndex < candidates.size(); ++candidateIndex) {
          const PositionCandidate& candidate = candidates[candidateIndex];
          const Rect candidateRect = rectAt(
            nodeIndex, candidate.x, candidate.y);
          if (!candidateHasClearance(nodeIndex, candidateRect)) continue;
          const std::size_t hits =
            candidateHitCount(nodeIndex, candidateRect);
          const double dx = candidate.x - originX;
          const double dy = candidate.y - originY;
          const double distanceSquared = dx * dx + dy * dy;
          if (
              hits < bestHits
              || (hits == bestHits
                  && distanceSquared < bestDistanceSquared)) {
            bestHits = hits;
            bestDistanceSquared = distanceSquared;
            best = candidate;
            if (stopAtZero && bestHits == 0) break;
          }
        }
      };

      // The exact normal projection is normally sufficient to put the card
      // just outside every line that currently pierces it. Evaluate those
      // few deterministic candidates first. The broad polar search is an
      // expensive fallback (168 candidates with the production defaults),
      // so only pay for it when the direct projection cannot reach zero.
      evaluateCandidates(0, false);
      if (bestHits > 0) {
        const std::size_t polarBegin = candidates.size();
        const double baseRadius = std::max({
          80.0,
          halfWidth + kPostLayoutNodeGapX + kCarrierNodeMargin,
          halfHeight + kPostLayoutNodeGapY + kCarrierNodeMargin,
        });
        const std::vector<double> radiusFactors = {
          1.0, 1.5, 2.25, 3.25, 4.5, 6.0, 8.0,
        };
        constexpr double kPi = 3.14159265358979323846;
        for (const double factor : radiusFactors) {
          const double radius = std::min(maxShift, baseRadius * factor);
          for (int direction = 0; direction < directions; ++direction) {
            const double angle = 2.0 * kPi
              * static_cast<double>(direction)
              / static_cast<double>(directions);
            addCandidate(
              originX + std::cos(angle) * radius,
              originY + std::sin(angle) * radius);
          }
        }
        evaluateCandidates(polarBegin, useSpatialCandidateIndex);
      }
      if (bestHits >= originHits) continue;
      attributes.x(node.handle) = best.x;
      attributes.y(node.handle) = best.y;
      movedNodeModelIdsThisRound.push_back(node.modelId);
      ++movedThisRound;
    }

    // A visible leaf tile is not an independent OGDF node at render time: the
    // whole tile matrix moves with its synthetic bundle table. Relocate that
    // rigid block when carrier-port selection alone cannot clear sibling-card
    // penetrations. Every accepted move is still re-routed and checked by the
    // full rendered metric below.
    for (const auto& ranked : bundleBlockerOrder) {
      const std::size_t bundleIndex = ranked.second;
      if (deferredBundleIndices.count(bundleIndex)) {
        continue;
      }
      if (
          processedBlockers >= batchLimit
          || clearBudgetExceeded()) {
        clearBudgetHit = clearBudgetExceeded();
        break;
      }
      ++processedBlockers;
      if (
          bundleIndex >= metadata.leafBundles.size()
          || bundleIndex >= bundleLeafNodeIndices.size()
          || bundleLeafNodeIndices[bundleIndex].empty()) {
        continue;
      }
      LeafBundleRecord& bundle = metadata.leafBundles[bundleIndex];
      const Rect originRect = renderedLeafBundleRect(bundle);
      const double originX = rectCenterX(originRect);
      const double originY = rectCenterY(originRect);
      const std::size_t originHits = candidateBundleHitCount(
        bundleIndex,
        0.0,
        0.0);
      if (originHits == 0) continue;

      struct BundlePositionCandidate {
        double dx;
        double dy;
      };
      std::vector<BundlePositionCandidate> candidates;
      candidates.reserve(
        hitsByBundle[bundleIndex].size() * 4
        + static_cast<std::size_t>(directions) * 7);
      auto addBundleCandidate = [&](double dx, double dy) {
        const double distanceSquared = dx * dx + dy * dy;
        if (distanceSquared > maxShift * maxShift + 1e-6) return;
        // Preserve the settled raw-node bbox as well as the rendered bundle
        // clearance. This prevents a collision fix from creating new empty
        // margins around the entire diagram.
        for (const std::size_t leafNodeIndex
             : bundleLeafNodeIndices[bundleIndex]) {
          const Rect movedLeaf = nodeRect(nodes[leafNodeIndex], attributes);
          if (
              movedLeaf.left + dx < settledBounds.left - 1e-6
              || movedLeaf.right + dx > settledBounds.right + 1e-6
              || movedLeaf.top + dy < settledBounds.top - 1e-6
              || movedLeaf.bottom + dy > settledBounds.bottom + 1e-6) {
            return;
          }
        }
        candidates.push_back({dx, dy});
      };

      const double halfWidth = rectWidth(originRect) / 2.0;
      const double halfHeight = rectHeight(originRect) / 2.0;
      for (const HitSegment& hit : hitsByBundle[bundleIndex]) {
        const double dx = hit.end.x - hit.start.x;
        const double dy = hit.end.y - hit.start.y;
        const double length = std::hypot(dx, dy);
        if (length < 1e-6) continue;
        const double nx = -dy / length;
        const double ny = dx / length;
        const double signedDistance =
          (originX - hit.start.x) * nx + (originY - hit.start.y) * ny;
        const double projectedRadius =
          std::abs(nx) * (halfWidth + kCarrierNodeMargin)
          + std::abs(ny) * (halfHeight + kCarrierNodeMargin)
          + 2.0;
        for (const double sign : {-1.0, 1.0}) {
          const double shift = sign * projectedRadius - signedDistance;
          addBundleCandidate(nx * shift, ny * shift);
          addBundleCandidate(nx * shift * 1.35, ny * shift * 1.35);
        }
      }

      std::size_t bestHits = originHits;
      double bestDistanceSquared = std::numeric_limits<double>::infinity();
      BundlePositionCandidate best{0.0, 0.0};
      auto evaluateBundleCandidates = [&](std::size_t begin, bool stopAtZero) {
        for (std::size_t candidateIndex = begin;
             candidateIndex < candidates.size(); ++candidateIndex) {
          const BundlePositionCandidate& candidate = candidates[candidateIndex];
          const Rect candidateRect{
            originRect.bottom + candidate.dy,
            originRect.left + candidate.dx,
            originRect.right + candidate.dx,
            originRect.top + candidate.dy,
          };
          if (!candidateBundleHasClearance(bundleIndex, candidateRect)) continue;
          const std::size_t hits = candidateBundleHitCount(
            bundleIndex,
            candidate.dx,
            candidate.dy);
          const double distanceSquared =
            candidate.dx * candidate.dx + candidate.dy * candidate.dy;
          if (
              hits < bestHits
              || (hits == bestHits
                  && distanceSquared < bestDistanceSquared)) {
            bestHits = hits;
            bestDistanceSquared = distanceSquared;
            best = candidate;
            if (stopAtZero && bestHits == 0) break;
          }
        }
      };

      evaluateBundleCandidates(0, false);
      if (bestHits > 0) {
        const std::size_t polarBegin = candidates.size();
        const double baseRadius = std::max({
          120.0,
          halfWidth + kPostLayoutNodeGapX + kCarrierNodeMargin,
          halfHeight + kPostLayoutNodeGapY + kCarrierNodeMargin,
        });
        const std::vector<double> radiusFactors = {
          1.0, 1.5, 2.25, 3.25, 4.5, 6.0, 8.0,
        };
        constexpr double kPi = 3.14159265358979323846;
        for (const double factor : radiusFactors) {
          const double radius = std::min(maxShift, baseRadius * factor);
          for (int direction = 0; direction < directions; ++direction) {
            const double angle = 2.0 * kPi
              * static_cast<double>(direction)
              / static_cast<double>(directions);
            addBundleCandidate(
              std::cos(angle) * radius,
              std::sin(angle) * radius);
          }
        }
        evaluateBundleCandidates(polarBegin, useSpatialCandidateIndex);
      }
      if (bestHits >= originHits) continue;
      for (const std::size_t leafNodeIndex
           : bundleLeafNodeIndices[bundleIndex]) {
        attributes.x(nodes[leafNodeIndex].handle) += best.dx;
        attributes.y(nodes[leafNodeIndex].handle) += best.dy;
      }
      bundle.bboxX += best.dx;
      bundle.bboxY += best.dy;
      bundle.anchorX += best.dx;
      bundle.anchorY += best.dy;
      movedBundleIndicesThisRound.push_back(bundleIndex);
      ++movedThisRound;
    }

    if (movedThisRound == 0) break;
    reroute();
    const LayoutQualityMetrics candidateQuality = measure(true);
    const bool edgeNodeImproved =
      candidateQuality.edgeNodeIntersections
        < currentQuality.edgeNodeIntersections;
    const bool bundleEdgeNonRegressed =
      candidateQuality.bundleEdgeIntersections
        <= currentQuality.bundleEdgeIntersections;
    const std::size_t currentCollisionDebt =
      currentQuality.edgeNodeIntersections
      + currentQuality.bundleEdgeIntersections;
    const std::size_t candidateCollisionDebt =
      candidateQuality.edgeNodeIntersections
      + candidateQuality.bundleEdgeIntersections;
    const std::size_t collisionGain =
      candidateCollisionDebt < currentCollisionDebt
        ? currentCollisionDebt - candidateCollisionDebt
        : 0;
    const bool collisionImproved = collisionGain > 0;
    const std::size_t visualDebt =
      candidateQuality.visualCrossings > currentQuality.visualCrossings
        ? candidateQuality.visualCrossings - currentQuality.visualCrossings
        : 0;
    // Hard targets are destinations, not admission prerequisites. Requiring a
    // 6k-conflict scene to reach 100 in one move made every progressive repair
    // impossible. Normally keep the exact rendered objective monotonic. A
    // tightly capped exception admits only highly efficient collision relief
    // (at least N real penetrations removed per one unit of visual debt), so a
    // large table-hit reduction is not discarded over a single-digit trade.
    const bool efficientCollisionTrade =
      currentQuality.visualCrossings > visualTarget
      && visualDebt > 0
      && visualDebt <= maxEfficientVisualDebt
      && collisionGain
        >= visualDebt * minCollisionGainPerVisualDebt;
    const bool visualOk =
      candidateQuality.visualCrossings <= currentQuality.visualCrossings
      || efficientCollisionTrade;
    const bool overlapOk =
      candidateQuality.nodeOverlaps <= currentQuality.nodeOverlaps
      && candidateQuality.bundleNodeOverlaps
        <= currentQuality.bundleNodeOverlaps
      && candidateQuality.nodeSpacingOverlaps
        <= currentQuality.nodeSpacingOverlaps;
    const double bboxTargetArea = bboxTargetB * 1e9 * bboxTolerance;
    const bool bboxTargetOk =
      bboxTargetB <= 0.0
      || (currentQuality.boundingBoxArea <= bboxTargetArea
        ? candidateQuality.boundingBoxArea <= bboxTargetArea
        : candidateQuality.boundingBoxArea
            <= currentQuality.boundingBoxArea + 1e-6);
    const bool bboxOk =
      bboxTargetOk
      && (currentQuality.boundingBoxArea <= 0.0
          || candidateQuality.boundingBoxArea
             <= currentQuality.boundingBoxArea * bboxTolerance);
    const bool bendOk = candidateQuality.edgeBendTotal <= 1e-6;
    if (!(
        edgeNodeImproved
        && collisionImproved
        && visualOk
        && overlapOk
        && bboxOk
        && bendOk)) {
      std::fprintf(stderr,
        "[rendered-carrier-node-clear-final] rejected batch moved=%zu "
        "visual=%zu->%zu edgeCross=%zu->%zu edgeNode=%zu->%zu "
        "bundleEdge=%zu->%zu bundleNode=%zu->%zu spacing=%zu->%zu "
        "bbox=%.3fB->%.3fB collisionGain=%zu visualDebt=%zu "
        "gates={edgeNode=%d,collision=%d,bundleEdgeNonRegression=%d,visual=%d,efficient=%d,overlap=%d,bbox=%d,bend=%d}.\n",
        movedThisRound,
        currentQuality.visualCrossings,
        candidateQuality.visualCrossings,
        currentQuality.edgeCrossings,
        candidateQuality.edgeCrossings,
        currentQuality.edgeNodeIntersections,
        candidateQuality.edgeNodeIntersections,
        currentQuality.bundleEdgeIntersections,
        candidateQuality.bundleEdgeIntersections,
        currentQuality.bundleNodeOverlaps,
        candidateQuality.bundleNodeOverlaps,
        currentQuality.nodeSpacingOverlaps,
        candidateQuality.nodeSpacingOverlaps,
        currentQuality.boundingBoxArea / 1e9,
        candidateQuality.boundingBoxArea / 1e9,
        collisionGain,
        visualDebt,
        edgeNodeImproved ? 1 : 0,
        collisionImproved ? 1 : 0,
        bundleEdgeNonRegressed ? 1 : 0,
        visualOk ? 1 : 0,
        efficientCollisionTrade ? 1 : 0,
        overlapOk ? 1 : 0,
        bboxOk ? 1 : 0,
        bendOk ? 1 : 0);
      for (std::size_t nodeIndex = 0;
           nodeIndex < nodes.size() && nodeIndex < savedPositions.size();
           ++nodeIndex) {
        attributes.x(nodes[nodeIndex].handle) = savedPositions[nodeIndex].first;
        attributes.y(nodes[nodeIndex].handle) = savedPositions[nodeIndex].second;
      }
      routes = savedRoutes;
      metadata = savedMetadata;
      const bool retrySmallerBatch =
        edgeNodeImproved
        && collisionImproved
        && (!visualOk || !overlapOk || !bboxOk)
        && activeFollowupBatchSize > minBlockerBatchSize;
      if (retrySmallerBatch) {
        activeFollowupBatchSize = std::max(
          minBlockerBatchSize,
          activeFollowupBatchSize / 2);
        std::fprintf(stderr,
          "[rendered-carrier-node-clear-final] retrying with batch=%zu.\n",
          activeFollowupBatchSize);
        continue;
      }
      // The highest-ranked blocker is not necessarily globally movable: a
      // locally clear card can force several long incident lines across the
      // rest of the drawing. Previously one such blocker terminated the
      // whole pass, leaving hundreds of independent, repairable blockers
      // unexamined. Defer only the rejected movers and continue down the exact
      // ranking. A later accepted scene clears this set because its changed
      // geometry can make those blockers admissible again.
      deferredNodeModelIds.insert(
        movedNodeModelIdsThisRound.begin(),
        movedNodeModelIdsThisRound.end());
      deferredBundleIndices.insert(
        movedBundleIndicesThisRound.begin(),
        movedBundleIndicesThisRound.end());
      if (
          !clearBudgetExceeded()
          && (!movedNodeModelIdsThisRound.empty()
              || !movedBundleIndicesThisRound.empty())) {
        std::fprintf(stderr,
          "[rendered-carrier-node-clear-final] deferred rejected blockers "
          "nodes=%zu bundles=%zu; continuing ranked search.\n",
          movedNodeModelIdsThisRound.size(),
          movedBundleIndicesThisRound.size());
        continue;
      }
      break;
    }
    currentQuality = candidateQuality;
    acceptedMoves += movedThisRound;
    ++completedRounds;
    activeFollowupBatchSize = followupBlockerBatchSize;
    deferredNodeModelIds.clear();
    deferredBundleIndices.clear();
    if (clearBudgetHit) break;
  }

  std::fprintf(stderr,
    "[rendered-carrier-node-clear-final] moved=%zu rounds=%d/%d, "
    "visual=%zu->%zu/%zu edgeCross=%zu->%zu edgeNode=%zu->%zu/%zu "
    "bundleEdge=%zu->%zu bundleNode=%zu->%zu spacing=%zu->%zu "
    "bbox=%.3fB->%.3fB bend=%.2f->%.2f.\n",
    acceptedMoves,
    completedRounds,
    rounds,
    initialQuality.visualCrossings,
    currentQuality.visualCrossings,
    visualTarget,
    initialQuality.edgeCrossings,
    currentQuality.edgeCrossings,
    initialQuality.edgeNodeIntersections,
    currentQuality.edgeNodeIntersections,
    edgeNodeTarget,
    initialQuality.bundleEdgeIntersections,
    currentQuality.bundleEdgeIntersections,
    initialQuality.bundleNodeOverlaps,
    currentQuality.bundleNodeOverlaps,
    initialQuality.nodeSpacingOverlaps,
    currentQuality.nodeSpacingOverlaps,
    initialQuality.boundingBoxArea / 1e9,
    currentQuality.boundingBoxArea / 1e9,
    initialQuality.edgeBendTotal,
    currentQuality.edgeBendTotal);
  if (clearBudgetHit) {
    std::fprintf(stderr,
      "[rendered-carrier-node-clear-final] budgetHit=1 budgetMs=%.0f; "
      "kept guarded partial result.\n",
      clearBudgetMs);
  }
  return acceptedMoves > 0;
}

bool applyFinalCarrierMetricsIfRequested(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes,
  const std::unordered_map<std::string, std::string>& clusterByModelIdFull,
  LayoutRunMetadata& metadata,
  LayoutQualityMetrics& quality,
  std::size_t totalRouteCrossings,
  bool quiet,
  bool optimizeGeometry) {
  const char* skipCarrierEnv = std::getenv("DJERD_NO_CARRIER_CROSS");
  const bool skipCarrier =
    skipCarrierEnv && std::strcmp(skipCarrierEnv, "0") != 0;
  if (skipCarrier || metadata.leafBundles.empty()) {
    return false;
  }

  std::unordered_map<std::string, std::size_t> leafToBundleIdx;
  for (std::size_t bi = 0; bi < metadata.leafBundles.size(); ++bi) {
    for (const std::string& leaf : metadata.leafBundles[bi].leafModelIds) {
      leafToBundleIdx[leaf] = bi;
    }
  }

  std::unordered_map<std::string, std::size_t> nodeIndexByModelId;
  nodeIndexByModelId.reserve(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    nodeIndexByModelId[nodes[i].modelId] = i;
  }

  std::unordered_map<std::string, std::pair<double, double>> sumByCluster;
  std::unordered_map<std::string, std::size_t> cntByCluster;
  for (const auto& kv : clusterByModelIdFull) {
    auto idIt = nodeIndexByModelId.find(kv.first);
    if (idIt == nodeIndexByModelId.end()) continue;
    const NodeRecord& node = nodes[idIt->second];
    sumByCluster[kv.second].first += attributes.x(node.handle);
    sumByCluster[kv.second].second += attributes.y(node.handle);
    cntByCluster[kv.second] += 1;
  }
  std::unordered_map<std::string, std::pair<double, double>> clusterCentroids;
  for (const auto& kv : sumByCluster) {
    const std::size_t c = cntByCluster[kv.first];
    if (c == 0) continue;
    clusterCentroids[kv.first] = {
      kv.second.first / static_cast<double>(c),
      kv.second.second / static_cast<double>(c),
    };
  }
  auto nearestClusterFinal = [&](const std::string& mid) {
    auto idIt = nodeIndexByModelId.find(mid);
    if (idIt == nodeIndexByModelId.end()) return std::string{};
    const NodeRecord& node = nodes[idIt->second];
    const double mx = attributes.x(node.handle);
    const double my = attributes.y(node.handle);
    std::string best;
    double bestD2 = std::numeric_limits<double>::infinity();
    for (const auto& kv : clusterCentroids) {
      const double dx = mx - kv.second.first;
      const double dy = my - kv.second.second;
      const double d2 = dx * dx + dy * dy;
      if (d2 < bestD2) {
        bestD2 = d2;
        best = kv.first;
      }
    }
    return best;
  };

  std::vector<std::string> carrierIdByEdge(edges.size());
  std::vector<bool> carrierEdgeVisible(edges.size(), true);
  std::vector<std::pair<std::string, std::string>> carrierClustersByEdge(edges.size());
  for (std::size_t e = 0; e < edges.size(); ++e) {
    const std::string& s = edges[e].sourceModelId;
    const std::string& t = edges[e].targetModelId;
    const bool inheritance = edges[e].kind == "inheritance";
    auto sBI = leafToBundleIdx.find(s);
    auto tBI = leafToBundleIdx.find(t);
    if (sBI != leafToBundleIdx.end()) {
      const auto& bundle = metadata.leafBundles[sBI->second];
      const auto& roots = bundle.sharedRootModelIds.empty()
        ? std::vector<std::string>{bundle.parentModelId}
        : bundle.sharedRootModelIds;
      if (std::find(roots.begin(), roots.end(), t) != roots.end()) {
        carrierIdByEdge[e] =
          "B" + std::to_string(sBI->second) + "|" + t;
        continue;
      }
    }
    if (tBI != leafToBundleIdx.end()) {
      const auto& bundle = metadata.leafBundles[tBI->second];
      const auto& roots = bundle.sharedRootModelIds.empty()
        ? std::vector<std::string>{bundle.parentModelId}
        : bundle.sharedRootModelIds;
      if (std::find(roots.begin(), roots.end(), s) != roots.end()) {
        carrierIdByEdge[e] =
          "B" + std::to_string(tBI->second) + "|" + s;
        continue;
      }
    }

    if (sBI != leafToBundleIdx.end() || tBI != leafToBundleIdx.end()) {
      if (inheritance) {
        carrierIdByEdge[e] = edges[e].edgeId;
      } else {
        carrierEdgeVisible[e] = false;
      }
      continue;
    }

    if (inheritance) {
      carrierIdByEdge[e] = edges[e].edgeId;
      continue;
    }

    auto sCit = clusterByModelIdFull.find(s);
    auto tCit = clusterByModelIdFull.find(t);
    std::string sCluster = sCit != clusterByModelIdFull.end()
      ? sCit->second
      : std::string{};
    std::string tCluster = tCit != clusterByModelIdFull.end()
      ? tCit->second
      : std::string{};
    if (sCluster.empty()) sCluster = nearestClusterFinal(s);
    if (tCluster.empty()) tCluster = nearestClusterFinal(t);
    if (!sCluster.empty() && !tCluster.empty()) {
      carrierClustersByEdge[e] = {sCluster, tCluster};
      carrierIdByEdge[e] = (sCluster == tCluster)
        ? "Cself|" + sCluster
        : (sCluster < tCluster
            ? "C|" + sCluster + "|" + tCluster
            : "C|" + tCluster + "|" + sCluster);
    } else {
      carrierIdByEdge[e] = edges[e].edgeId;
    }
  }

  const char* hubCarrierEnv = std::getenv("DJERD_HUB_CARRIER_CROSS_FINAL");
  const bool hubCarrier =
    hubCarrierEnv && std::strcmp(hubCarrierEnv, "0") != 0;
  if (hubCarrier) {
    const char* thresholdEnv =
      std::getenv("DJERD_HUB_CARRIER_CROSS_FINAL_THRESHOLD");
    const int threshold = thresholdEnv
      ? std::max(2, std::atoi(thresholdEnv))
      : 16;
    std::unordered_map<std::string, int> incidentCarrierCount;
    for (std::size_t e = 0; e < carrierClustersByEdge.size(); ++e) {
      const auto& [leftCluster, rightCluster] = carrierClustersByEdge[e];
      if (leftCluster.empty() || rightCluster.empty() || leftCluster == rightCluster) {
        continue;
      }
      incidentCarrierCount[leftCluster] += 1;
      incidentCarrierCount[rightCluster] += 1;
    }
    std::size_t hubEdges = 0;
    std::unordered_set<std::string> hubClusters;
    for (std::size_t e = 0; e < carrierClustersByEdge.size(); ++e) {
      const auto& [leftCluster, rightCluster] = carrierClustersByEdge[e];
      if (leftCluster.empty() || rightCluster.empty() || leftCluster == rightCluster) {
        continue;
      }
      const int leftCount = incidentCarrierCount[leftCluster];
      const int rightCount = incidentCarrierCount[rightCluster];
      if (leftCount < threshold && rightCount < threshold) {
        continue;
      }
      const std::string& hub =
        (leftCount > rightCount || (leftCount == rightCount && leftCluster < rightCluster))
          ? leftCluster
          : rightCluster;
      carrierIdByEdge[e] = "H|" + hub;
      hubClusters.insert(hub);
      ++hubEdges;
    }
    if (!quiet) {
      std::fprintf(stderr,
        "[hub-carrier-cross-final] grouped %zu edges through %zu hubs "
        "(threshold=%d).\n",
        hubEdges,
        hubClusters.size(),
        threshold);
    }
    metadata.hubCarrierThreshold = threshold;
    metadata.hubCarrierEdgesGrouped = hubEdges;
    metadata.hubCarrierClusters = hubClusters.size();
  }

  const char* occMarginFinalEnv = std::getenv("DJERD_CARRIER_CROSS_OCCLUSION_MARGIN");
  const double occMarginFinal = occMarginFinalEnv
    ? std::max(0.0, std::atof(occMarginFinalEnv))
    : 0.0;
  std::unordered_set<std::string> bundleAbsorbedOccFinal;
  for (const LeafBundleRecord& bundle : metadata.leafBundles) {
    bundleAbsorbedOccFinal.insert(bundle.parentModelId);
    for (const std::string& leaf : bundle.leafModelIds) {
      bundleAbsorbedOccFinal.insert(leaf);
    }
  }
  std::vector<Rect> carrierOcclusionRectsFinal;
  carrierOcclusionRectsFinal.reserve(nodes.size() + metadata.leafBundles.size());
  for (const NodeRecord& node : nodes) {
    if (bundleAbsorbedOccFinal.count(node.modelId)) continue;
    carrierOcclusionRectsFinal.push_back(nodeRect(node, attributes, occMarginFinal));
  }
  for (const LeafBundleRecord& bundle : metadata.leafBundles) {
    carrierOcclusionRectsFinal.push_back(renderedLeafBundleRect(bundle, occMarginFinal));
  }
  auto pointInCarrierOcclusionFinal = [&](const RoutePoint& point) {
    if (occMarginFinal <= 0.0) return false;
    for (const Rect& rect : carrierOcclusionRectsFinal) {
      if (
          point.x >= rect.left && point.x <= rect.right
          && point.y >= rect.top && point.y <= rect.bottom) {
        return true;
      }
    }
    return false;
  };

  std::set<std::pair<std::string, std::string>> seenCarrierPairs;
  std::size_t carrierGroupedCross = 0;
  std::size_t carrierOccludedCross = 0;
  for (std::size_t i = 0; i < edges.size(); ++i) {
    if (!carrierEdgeVisible[i] || i >= routes.size() || routes[i].size() < 2) continue;
    for (std::size_t j = i + 1; j < edges.size(); ++j) {
      if (!carrierEdgeVisible[j] || j >= routes.size() || routes[j].size() < 2) continue;
      if (sharesEndpoint(edges[i], edges[j])) continue;
      if (carrierIdByEdge[i] == carrierIdByEdge[j]) continue;
      bool anyCross = false;
      for (std::size_t li = 1; li < routes[i].size() && !anyCross; ++li) {
        for (std::size_t rj = 1; rj < routes[j].size() && !anyCross; ++rj) {
          RoutePoint isect;
          if (properSegmentIntersection(
              routes[i][li - 1], routes[i][li],
              routes[j][rj - 1], routes[j][rj], isect)) {
            if (pointInCarrierOcclusionFinal(isect)) {
              ++carrierOccludedCross;
            } else {
              anyCross = true;
            }
          }
        }
      }
      if (!anyCross) continue;
      auto pk = carrierIdByEdge[i] < carrierIdByEdge[j]
        ? std::make_pair(carrierIdByEdge[i], carrierIdByEdge[j])
        : std::make_pair(carrierIdByEdge[j], carrierIdByEdge[i]);
      if (seenCarrierPairs.insert(pk).second) {
        ++carrierGroupedCross;
      }
    }
  }
  if (!quiet) {
    std::fprintf(stderr,
      "[carrier-cross-final] segment %zu -> carrier-grouped %zu "
      "(occluded=%zu, margin=%.1f).\n",
      totalRouteCrossings, carrierGroupedCross, carrierOccludedCross,
      occMarginFinal);
  }

  if (!applyRenderedCarrierMetricsIfRequested(
      nodes,
      edges,
      routes,
      attributes,
      clusterByModelIdFull,
      metadata,
      quality,
      totalRouteCrossings,
      quiet,
      optimizeGeometry)) {
    quality.edgeCrossings = carrierGroupedCross;
  }
  return true;
}

bool repairCanonicalRouteObstaclesIfRequested(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes,
  const std::unordered_map<std::string, std::string>& clusterByModelIdFull,
  const LayoutRunMetadata& metadata) {
  if (!readBoolEnv("DJERD_CANONICAL_ROUTE_REPAIR", false)
      || routes.size() != edges.size() || nodes.empty()) {
    return false;
  }

  const double clearance = readDoubleEnv(
    "DJERD_CANONICAL_ROUTE_REPAIR_CLEARANCE", 12.0, 1.0, 480.0);
  const int maxDetours = static_cast<int>(readDoubleEnv(
    "DJERD_CANONICAL_ROUTE_REPAIR_MAX_DETOURS", 12.0, 1.0, 64.0));
  const std::vector<std::vector<RoutePoint>> savedRoutes = routes;

  std::unordered_map<std::string, std::size_t> nodeIndexById;
  nodeIndexById.reserve(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    nodeIndexById[nodes[i].modelId] = i;
  }
  const std::size_t invalidIndex = std::numeric_limits<std::size_t>::max();
  std::vector<std::pair<std::size_t, std::size_t>> endpointIndices(edges.size());
  for (std::size_t e = 0; e < edges.size(); ++e) {
    const auto source = nodeIndexById.find(edges[e].sourceModelId);
    const auto target = nodeIndexById.find(edges[e].targetModelId);
    endpointIndices[e] = {
      source == nodeIndexById.end() ? invalidIndex : source->second,
      target == nodeIndexById.end() ? invalidIndex : target->second,
    };
  }

  std::vector<Rect> canonicalNodeRects(nodes.size());
  std::vector<Rect> visualNodeRects(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    canonicalNodeRects[i] = nodeRect(nodes[i], attributes);
    visualNodeRects[i] = nodeRect(nodes[i], attributes, visualNodeMargin());
  }

  std::vector<Rect> bundleRects;
  std::vector<std::unordered_set<std::size_t>> bundleMembers;
  bundleRects.reserve(metadata.leafBundles.size());
  bundleMembers.reserve(metadata.leafBundles.size());
  for (const LeafBundleRecord& bundle : metadata.leafBundles) {
    bundleRects.push_back(renderedLeafBundleRect(bundle, leafBundleVisualMargin()));
    std::unordered_set<std::size_t> members;
    const auto parent = nodeIndexById.find(bundle.parentModelId);
    if (parent != nodeIndexById.end()) members.insert(parent->second);
    for (const std::string& leafId : bundle.leafModelIds) {
      const auto leaf = nodeIndexById.find(leafId);
      if (leaf != nodeIndexById.end()) members.insert(leaf->second);
    }
    bundleMembers.push_back(std::move(members));
  }

  auto countNodeHits = [&](std::size_t edgeIndex,
                           const std::vector<RoutePoint>& route,
                           const std::vector<Rect>& rects) {
    std::size_t hits = 0;
    if (edgeIndex >= endpointIndices.size() || route.size() < 2) return hits;
    const auto [sourceIndex, targetIndex] = endpointIndices[edgeIndex];
    for (std::size_t nodeIndex = 0; nodeIndex < rects.size(); ++nodeIndex) {
      if (nodeIndex == sourceIndex || nodeIndex == targetIndex) continue;
      for (std::size_t segmentIndex = 1;
           segmentIndex < route.size(); ++segmentIndex) {
        if (segmentIntersectsRect(
            route[segmentIndex - 1], route[segmentIndex], rects[nodeIndex])) {
          ++hits;
          break;
        }
      }
    }
    return hits;
  };

  auto countBundleHits = [&](std::size_t edgeIndex,
                             const std::vector<RoutePoint>& route) {
    std::size_t hits = 0;
    if (edgeIndex >= endpointIndices.size() || route.size() < 2) return hits;
    const auto [sourceIndex, targetIndex] = endpointIndices[edgeIndex];
    for (std::size_t bundleIndex = 0;
         bundleIndex < bundleRects.size(); ++bundleIndex) {
      if (bundleMembers[bundleIndex].count(sourceIndex) != 0
          || bundleMembers[bundleIndex].count(targetIndex) != 0) {
        continue;
      }
      for (std::size_t segmentIndex = 1;
           segmentIndex < route.size(); ++segmentIndex) {
        if (segmentIntersectsRect(
            route[segmentIndex - 1], route[segmentIndex],
            bundleRects[bundleIndex])) {
          ++hits;
          break;
        }
      }
    }
    return hits;
  };

  auto segmentsTouch = [](const RoutePoint& leftStart,
                          const RoutePoint& leftEnd,
                          const RoutePoint& rightStart,
                          const RoutePoint& rightEnd) {
    constexpr double coordinateEpsilon = 1e-7;
    constexpr double parameterEpsilon = 1e-9;
    const double scale = std::max({
      1.0,
      std::abs(leftStart.x), std::abs(leftStart.y),
      std::abs(leftEnd.x), std::abs(leftEnd.y),
      std::abs(rightStart.x), std::abs(rightStart.y),
      std::abs(rightEnd.x), std::abs(rightEnd.y),
    });
    const double tolerance = coordinateEpsilon * scale;
    if (std::max(leftStart.x, leftEnd.x) + tolerance
          < std::min(rightStart.x, rightEnd.x)
        || std::max(rightStart.x, rightEnd.x) + tolerance
          < std::min(leftStart.x, leftEnd.x)
        || std::max(leftStart.y, leftEnd.y) + tolerance
          < std::min(rightStart.y, rightEnd.y)
        || std::max(rightStart.y, rightEnd.y) + tolerance
          < std::min(leftStart.y, leftEnd.y)) {
      return false;
    }
    const double rx = leftEnd.x - leftStart.x;
    const double ry = leftEnd.y - leftStart.y;
    const double sx = rightEnd.x - rightStart.x;
    const double sy = rightEnd.y - rightStart.y;
    const double qpx = rightStart.x - leftStart.x;
    const double qpy = rightStart.y - leftStart.y;
    const double denominator = crossProduct(rx, ry, sx, sy);
    const double parallelTolerance = coordinateEpsilon
      * std::max(1.0, std::hypot(rx, ry) * std::hypot(sx, sy));
    if (std::abs(denominator) > parallelTolerance) {
      const double t = crossProduct(qpx, qpy, sx, sy) / denominator;
      const double u = crossProduct(qpx, qpy, rx, ry) / denominator;
      return t >= -parameterEpsilon && t <= 1.0 + parameterEpsilon
        && u >= -parameterEpsilon && u <= 1.0 + parameterEpsilon;
    }
    const double collinearTolerance = coordinateEpsilon * std::max(
      1.0, std::hypot(rx, ry) * std::hypot(qpx, qpy));
    if (std::abs(crossProduct(qpx, qpy, rx, ry)) > collinearTolerance) {
      return false;
    }
    const double lengthSquared = rx * rx + ry * ry;
    if (lengthSquared <= 0.0) return false;
    const double start = (qpx * rx + qpy * ry) / lengthSquared;
    const double end = start + (sx * rx + sy * ry) / lengthSquared;
    return std::min(1.0, std::max(start, end))
      >= std::max(0.0, std::min(start, end)) - parameterEpsilon;
  };

  auto countAdjacentContacts = [&](std::size_t edgeIndex,
                                   const std::vector<RoutePoint>& candidate) {
    std::size_t contacts = 0;
    if (candidate.size() < 2) return contacts;
    for (std::size_t other = 0; other < edges.size(); ++other) {
      if (other == edgeIndex || !sharesEndpoint(edges[edgeIndex], edges[other])
          || routes[other].size() < 2) {
        continue;
      }
      for (std::size_t left = 1; left < candidate.size(); ++left) {
        for (std::size_t right = 1; right < routes[other].size(); ++right) {
          if (segmentsTouch(
              candidate[left - 1], candidate[left],
              routes[other][right - 1], routes[other][right])) {
            ++contacts;
          }
        }
      }
    }
    return contacts;
  };

  struct NonAdjacentInteractionCounts {
    std::size_t properCrossings = 0;
    std::size_t nonProperContacts = 0;
  };
  auto countNonAdjacentInteractions = [&](
      std::size_t edgeIndex,
      const std::vector<RoutePoint>& candidate) {
    NonAdjacentInteractionCounts counts;
    if (candidate.size() < 2) return counts;
    for (std::size_t other = 0; other < edges.size(); ++other) {
      if (other == edgeIndex || sharesEndpoint(edges[edgeIndex], edges[other])
          || routes[other].size() < 2) {
        continue;
      }
      for (std::size_t left = 1; left < candidate.size(); ++left) {
        for (std::size_t right = 1; right < routes[other].size(); ++right) {
          RoutePoint intersection;
          if (properSegmentIntersection(
              candidate[left - 1], candidate[left],
              routes[other][right - 1], routes[other][right], intersection)) {
            ++counts.properCrossings;
          } else if (segmentsTouch(
              candidate[left - 1], candidate[left],
              routes[other][right - 1], routes[other][right])) {
            ++counts.nonProperContacts;
          }
        }
      }
    }
    return counts;
  };

  auto countSelfContacts = [&](const std::vector<RoutePoint>& candidate) {
    std::size_t contacts = 0;
    if (candidate.size() < 4) return contacts;
    for (std::size_t left = 1; left < candidate.size(); ++left) {
      for (std::size_t right = left + 2; right < candidate.size(); ++right) {
        if (segmentsTouch(
            candidate[left - 1], candidate[left],
            candidate[right - 1], candidate[right])) {
          ++contacts;
        }
      }
    }
    return contacts;
  };

  auto routeLength = [](const std::vector<RoutePoint>& route) {
    double length = 0.0;
    for (std::size_t i = 1; i < route.size(); ++i) {
      length += std::hypot(
        route[i].x - route[i - 1].x,
        route[i].y - route[i - 1].y);
    }
    return length;
  };
  auto roundedPoint = [](double x, double y) {
    return RoutePoint{
      std::round(x * 100.0) / 100.0,
      std::round(y * 100.0) / 100.0,
    };
  };

  const CanonicalCrossingMetrics beforeCanonical =
    measureCanonicalCrossingMetrics(nodes, edges, routes, attributes);
  auto qualityForRoutes = [&]() {
    std::vector<std::vector<std::string>> crossingIdsByEdge;
    std::size_t rawCrossings = 0;
    detectRouteCrossings(edges, routes, crossingIdsByEdge, rawCrossings);
    LayoutQualityMetrics quality = measureLayoutQuality(
      nodes, edges, routes, attributes, &metadata.leafBundles,
      &metadata.clusterByModelId);
    quality.edgeCrossings = rawCrossings;
    LayoutRunMetadata metricMetadata = metadata;
    applyFinalCarrierMetricsIfRequested(
      nodes, edges, routes, attributes, clusterByModelIdFull,
      metricMetadata, quality, rawCrossings, true, false);
    quality.visualCrossings =
      quality.edgeCrossings
      + quality.edgeNodeIntersections
      + quality.nodeOverlaps
      + quality.bundleEdgeIntersections
      + quality.bundleNodeOverlaps;
    return quality;
  };
  const LayoutQualityMetrics beforeQuality = qualityForRoutes();

  std::vector<std::pair<std::size_t, std::size_t>> rankedEdges;
  rankedEdges.reserve(edges.size());
  for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
    const std::size_t hits = countNodeHits(
      edgeIndex, routes[edgeIndex], canonicalNodeRects);
    if (hits > 0) rankedEdges.push_back({hits, edgeIndex});
  }
  std::sort(rankedEdges.begin(), rankedEdges.end(),
    [](const auto& left, const auto& right) {
      if (left.first != right.first) return left.first > right.first;
      return left.second < right.second;
    });

  std::size_t changedEdges = 0;
  std::size_t insertedDetours = 0;
  std::size_t rejectedEdges = 0;
  for (const auto& ranked : rankedEdges) {
    const std::size_t edgeIndex = ranked.second;
    bool edgeChanged = false;
    for (int iteration = 0; iteration < maxDetours; ++iteration) {
      const std::vector<RoutePoint>& current = routes[edgeIndex];
      const std::size_t currentCanonicalHits = countNodeHits(
        edgeIndex, current, canonicalNodeRects);
      if (currentCanonicalHits == 0 || current.size() < 2) break;
      const std::size_t currentVisualHits = countNodeHits(
        edgeIndex, current, visualNodeRects);
      const std::size_t currentBundleHits = countBundleHits(edgeIndex, current);
      const std::size_t currentAdjacent = countAdjacentContacts(edgeIndex, current);
      const NonAdjacentInteractionCounts currentInteractions =
        countNonAdjacentInteractions(edgeIndex, current);
      const std::size_t currentSelfContacts = countSelfContacts(current);

      std::size_t blockedSegment = current.size();
      std::size_t blockerIndex = nodes.size();
      const auto [sourceIndex, targetIndex] = endpointIndices[edgeIndex];
      for (std::size_t segmentIndex = 1;
           segmentIndex < current.size() && blockerIndex == nodes.size();
           ++segmentIndex) {
        for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
          if (nodeIndex == sourceIndex || nodeIndex == targetIndex) continue;
          if (segmentIntersectsRect(
              current[segmentIndex - 1], current[segmentIndex],
              canonicalNodeRects[nodeIndex])) {
            blockedSegment = segmentIndex;
            blockerIndex = nodeIndex;
            break;
          }
        }
      }
      if (blockerIndex == nodes.size()) break;

      const RoutePoint start = current[blockedSegment - 1];
      const RoutePoint end = current[blockedSegment];
      const Rect& rect = canonicalNodeRects[blockerIndex];
      const double left = rect.left - clearance;
      const double right = rect.right + clearance;
      const double top = rect.top - clearance;
      const double bottom = rect.bottom + clearance;
      std::array<std::array<RoutePoint, 2>, 4> detours;
      if (end.x >= start.x) {
        detours[0] = {roundedPoint(left, top), roundedPoint(right, top)};
        detours[1] = {roundedPoint(left, bottom), roundedPoint(right, bottom)};
      } else {
        detours[0] = {roundedPoint(right, top), roundedPoint(left, top)};
        detours[1] = {roundedPoint(right, bottom), roundedPoint(left, bottom)};
      }
      if (end.y >= start.y) {
        detours[2] = {roundedPoint(left, top), roundedPoint(left, bottom)};
        detours[3] = {roundedPoint(right, top), roundedPoint(right, bottom)};
      } else {
        detours[2] = {roundedPoint(left, bottom), roundedPoint(left, top)};
        detours[3] = {roundedPoint(right, bottom), roundedPoint(right, top)};
      }

      std::vector<RoutePoint> bestRoute;
      std::size_t bestCanonicalHits = currentCanonicalHits;
      std::size_t bestAdjacent = currentAdjacent;
      std::size_t bestCrossings = currentInteractions.properCrossings;
      double bestLength = std::numeric_limits<double>::infinity();
      for (const auto& detour : detours) {
        std::vector<RoutePoint> candidate;
        candidate.reserve(current.size() + 2);
        candidate.insert(
          candidate.end(), current.begin(), current.begin() + blockedSegment);
        candidate.push_back(detour[0]);
        candidate.push_back(detour[1]);
        candidate.insert(
          candidate.end(), current.begin() + blockedSegment, current.end());
        candidate = compressRoutePoints(std::move(candidate));
        const std::size_t canonicalHits = countNodeHits(
          edgeIndex, candidate, canonicalNodeRects);
        if (canonicalHits >= currentCanonicalHits) continue;
        const std::size_t visualHits = countNodeHits(
          edgeIndex, candidate, visualNodeRects);
        if (visualHits > currentVisualHits
            || countBundleHits(edgeIndex, candidate) > currentBundleHits) {
          continue;
        }
        const std::size_t adjacent = countAdjacentContacts(edgeIndex, candidate);
        const NonAdjacentInteractionCounts interactions =
          countNonAdjacentInteractions(edgeIndex, candidate);
        if (adjacent > currentAdjacent
            || interactions.properCrossings
              > currentInteractions.properCrossings
            || interactions.nonProperContacts
              > currentInteractions.nonProperContacts
            || countSelfContacts(candidate) > currentSelfContacts) {
          continue;
        }
        const std::size_t crossings = interactions.properCrossings;
        const double length = routeLength(candidate);
        const bool better = bestRoute.empty()
          || canonicalHits < bestCanonicalHits
          || (canonicalHits == bestCanonicalHits && adjacent < bestAdjacent)
          || (canonicalHits == bestCanonicalHits && adjacent == bestAdjacent
              && crossings < bestCrossings)
          || (canonicalHits == bestCanonicalHits && adjacent == bestAdjacent
              && crossings == bestCrossings && length < bestLength);
        if (better) {
          bestCanonicalHits = canonicalHits;
          bestAdjacent = adjacent;
          bestCrossings = crossings;
          bestLength = length;
          bestRoute = std::move(candidate);
        }
      }
      if (bestRoute.empty()) {
        ++rejectedEdges;
        break;
      }
      routes[edgeIndex] = std::move(bestRoute);
      edgeChanged = true;
      ++insertedDetours;
    }
    if (edgeChanged) ++changedEdges;
  }

  if (insertedDetours == 0) {
    std::fprintf(stderr,
      "[canonical-route-repair] no safe obstacle detour candidate (%zu hit edges).\n",
      rankedEdges.size());
    return false;
  }

  const CanonicalCrossingMetrics afterCanonical =
    measureCanonicalCrossingMetrics(nodes, edges, routes, attributes);
  const LayoutQualityMetrics afterQuality = qualityForRoutes();
  const bool canonicalSafe =
    afterCanonical.nonIncidentNodeHits.size()
      < beforeCanonical.nonIncidentNodeHits.size()
    && afterCanonical.adjacentEdgeIntersectionCount
      <= beforeCanonical.adjacentEdgeIntersectionCount
    && afterCanonical.properCrossingPoints.size()
      <= beforeCanonical.properCrossingPoints.size()
    && afterCanonical.crossingEdgePairs.size()
      <= beforeCanonical.crossingEdgePairs.size()
    && afterCanonical.completeRouteCount >= beforeCanonical.completeRouteCount
    && (!beforeCanonical.allCanonicalRoutesComplete
        || afterCanonical.allCanonicalRoutesComplete)
    && afterCanonical.invariantViolationCount
      <= beforeCanonical.invariantViolationCount
    && afterCanonical.degenerateSegmentCount
      <= beforeCanonical.degenerateSegmentCount
    && afterCanonical.collinearOverlapCount
      <= beforeCanonical.collinearOverlapCount
    && afterCanonical.nonProperContactCount
      <= beforeCanonical.nonProperContactCount
    && afterCanonical.selfIntersectionCount
      <= beforeCanonical.selfIntersectionCount;
  const bool visualSafe =
    afterQuality.edgeCrossings <= beforeQuality.edgeCrossings
    && afterQuality.edgeNodeIntersections <= beforeQuality.edgeNodeIntersections
    && afterQuality.nodeOverlaps <= beforeQuality.nodeOverlaps
    && afterQuality.bundleEdgeIntersections
      <= beforeQuality.bundleEdgeIntersections
    && afterQuality.bundleNodeOverlaps <= beforeQuality.bundleNodeOverlaps
    && afterQuality.overlappingEdges <= beforeQuality.overlappingEdges
    && afterQuality.edgeSegmentOverlaps <= beforeQuality.edgeSegmentOverlaps
    && afterQuality.visualCrossings <= beforeQuality.visualCrossings;

  std::vector<std::string> failedSafetyFields;
  const auto recordSafetyFailure = [&](bool condition, const char* field) {
    if (!condition) failedSafetyFields.emplace_back(field);
  };
  recordSafetyFailure(
    afterCanonical.nonIncidentNodeHits.size()
      < beforeCanonical.nonIncidentNodeHits.size(),
    "canonical.nodeHitStrict");
  recordSafetyFailure(
    afterCanonical.adjacentEdgeIntersectionCount
      <= beforeCanonical.adjacentEdgeIntersectionCount,
    "canonical.adjacent");
  recordSafetyFailure(
    afterCanonical.properCrossingPoints.size()
      <= beforeCanonical.properCrossingPoints.size(),
    "canonical.properPoints");
  recordSafetyFailure(
    afterCanonical.crossingEdgePairs.size()
      <= beforeCanonical.crossingEdgePairs.size(),
    "canonical.properPairs");
  recordSafetyFailure(
    afterCanonical.completeRouteCount >= beforeCanonical.completeRouteCount,
    "canonical.completeRouteCount");
  recordSafetyFailure(
    !beforeCanonical.allCanonicalRoutesComplete
      || afterCanonical.allCanonicalRoutesComplete,
    "canonical.allComplete");
  recordSafetyFailure(
    afterCanonical.invariantViolationCount
      <= beforeCanonical.invariantViolationCount,
    "canonical.invariant");
  recordSafetyFailure(
    afterCanonical.degenerateSegmentCount
      <= beforeCanonical.degenerateSegmentCount,
    "canonical.degenerate");
  recordSafetyFailure(
    afterCanonical.collinearOverlapCount
      <= beforeCanonical.collinearOverlapCount,
    "canonical.collinearOverlap");
  recordSafetyFailure(
    afterCanonical.nonProperContactCount
      <= beforeCanonical.nonProperContactCount,
    "canonical.pointContact");
  recordSafetyFailure(
    afterCanonical.selfIntersectionCount
      <= beforeCanonical.selfIntersectionCount,
    "canonical.selfIntersection");
  recordSafetyFailure(
    afterQuality.edgeCrossings <= beforeQuality.edgeCrossings,
    "visual.edgeCross");
  recordSafetyFailure(
    afterQuality.edgeNodeIntersections <= beforeQuality.edgeNodeIntersections,
    "visual.edgeNode");
  recordSafetyFailure(
    afterQuality.nodeOverlaps <= beforeQuality.nodeOverlaps,
    "visual.nodeOverlap");
  recordSafetyFailure(
    afterQuality.bundleEdgeIntersections
      <= beforeQuality.bundleEdgeIntersections,
    "visual.bundleEdge");
  recordSafetyFailure(
    afterQuality.bundleNodeOverlaps <= beforeQuality.bundleNodeOverlaps,
    "visual.bundleNode");
  recordSafetyFailure(
    afterQuality.overlappingEdges <= beforeQuality.overlappingEdges,
    "visual.overlappingEdges");
  recordSafetyFailure(
    afterQuality.edgeSegmentOverlaps <= beforeQuality.edgeSegmentOverlaps,
    "visual.edgeSegmentOverlap");
  recordSafetyFailure(
    afterQuality.visualCrossings <= beforeQuality.visualCrossings,
    "visual.total");

  const auto boundViolationFor = [&](const CanonicalCrossingMetrics& metrics) {
    return metadata.canonicalCrossing.available
      && metrics.properDrawing
      && metrics.crossingEdgePairs.size()
        < metadata.canonicalCrossing.lowerBound;
  };
  const bool beforeBoundViolation = boundViolationFor(beforeCanonical);
  const bool afterBoundViolation = boundViolationFor(afterCanonical);
  std::ostringstream failedSafetySummary;
  if (failedSafetyFields.empty()) {
    failedSafetySummary << "none";
  } else {
    for (std::size_t i = 0; i < failedSafetyFields.size(); ++i) {
      if (i > 0) failedSafetySummary << ',';
      failedSafetySummary << failedSafetyFields[i];
    }
  }
  const auto logRepairSummary = [&](const char* outcome) {
    std::ostringstream summary;
    summary
      << "[canonical-route-repair] " << outcome << ' '
      << insertedDetours << " detours/" << changedEdges
      << " edges (rejected=" << rejectedEdges << "): "
      << "canonical={"
      << "invariant=" << beforeCanonical.invariantViolationCount << "->"
      << afterCanonical.invariantViolationCount
      << ",degenerate=" << beforeCanonical.degenerateSegmentCount << "->"
      << afterCanonical.degenerateSegmentCount
      << ",collinearOverlap=" << beforeCanonical.collinearOverlapCount << "->"
      << afterCanonical.collinearOverlapCount
      << ",pointContact=" << beforeCanonical.nonProperContactCount << "->"
      << afterCanonical.nonProperContactCount
      << ",selfIntersection=" << beforeCanonical.selfIntersectionCount << "->"
      << afterCanonical.selfIntersectionCount
      << ",adjacent=" << beforeCanonical.adjacentEdgeIntersectionCount << "->"
      << afterCanonical.adjacentEdgeIntersectionCount
      << ",nodeHit=" << beforeCanonical.nonIncidentNodeHits.size() << "->"
      << afterCanonical.nonIncidentNodeHits.size()
      << ",properPoints=" << beforeCanonical.properCrossingPoints.size() << "->"
      << afterCanonical.properCrossingPoints.size()
      << ",properPairs=" << beforeCanonical.crossingEdgePairs.size() << "->"
      << afterCanonical.crossingEdgePairs.size()
      << ",completeRoutes=" << beforeCanonical.completeRouteCount << "->"
      << afterCanonical.completeRouteCount
      << ",canonicalEdges=" << beforeCanonical.canonicalEdgeCount << "->"
      << afterCanonical.canonicalEdgeCount
      << ",allComplete=" << (beforeCanonical.allCanonicalRoutesComplete ? 1 : 0)
      << "->" << (afterCanonical.allCanonicalRoutesComplete ? 1 : 0)
      << ",properDrawing=" << (beforeCanonical.properDrawing ? 1 : 0)
      << "->" << (afterCanonical.properDrawing ? 1 : 0)
      << ",boundViolation=" << (beforeBoundViolation ? 1 : 0)
      << "->" << (afterBoundViolation ? 1 : 0)
      << "}; visual={"
      << "total=" << beforeQuality.visualCrossings << "->"
      << afterQuality.visualCrossings
      << ",edgeCross=" << beforeQuality.edgeCrossings << "->"
      << afterQuality.edgeCrossings
      << ",edgeNode=" << beforeQuality.edgeNodeIntersections << "->"
      << afterQuality.edgeNodeIntersections
      << ",nodeOverlap=" << beforeQuality.nodeOverlaps << "->"
      << afterQuality.nodeOverlaps
      << ",nodeSpacing=" << beforeQuality.nodeSpacingOverlaps << "->"
      << afterQuality.nodeSpacingOverlaps
      << ",bundleEdge=" << beforeQuality.bundleEdgeIntersections << "->"
      << afterQuality.bundleEdgeIntersections
      << ",bundleNode=" << beforeQuality.bundleNodeOverlaps << "->"
      << afterQuality.bundleNodeOverlaps
      << ",overlappingEdges=" << beforeQuality.overlappingEdges << "->"
      << afterQuality.overlappingEdges
      << ",edgeSegmentOverlap=" << beforeQuality.edgeSegmentOverlaps << "->"
      << afterQuality.edgeSegmentOverlaps
      << "}; failed={" << failedSafetySummary.str() << "}"
      << " (canonicalSafe=" << (canonicalSafe ? 1 : 0)
      << " visualSafe=" << (visualSafe ? 1 : 0) << ").";
    std::fprintf(stderr, "%s\n", summary.str().c_str());
  };
  if (!canonicalSafe || !visualSafe) {
    routes = savedRoutes;
    logRepairSummary("reverted");
    return false;
  }

  logRepairSummary("accepted");
  return true;
}

}  // namespace djerd
