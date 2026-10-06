#include "sourceInputLayoutPipeline.h"
#include "sourceInputLayout.h"
#include "layoutPipeline.h"

#include <chrono>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace djerd {

SourceInputLayoutAttempt tryWriteSourceInputLayout(
    const CliArguments& arguments, const std::vector<NodeRecord>& nodes,
    const std::vector<EdgeRecord>& edges, ogdf::GraphAttributes& attributes,
    const LayoutRunMetadata& metadata, const CanonicalCrossingMetadata& canonical,
    const std::string& modelPath, double budgetMs, std::ostream& output) {
  using Clock = std::chrono::steady_clock;
  const auto began = Clock::now();
  const auto elapsed = [&] {
    return std::chrono::duration<double, std::milli>(Clock::now() - began).count();
  };
  std::vector<std::pair<double, double>> rollback;
  const auto restore = [&] {
    for (std::size_t n = 0; n < rollback.size(); ++n) {
      attributes.x(nodes[n].handle) = rollback[n].first;
      attributes.y(nodes[n].handle) = rollback[n].second;
    }
  };
  try {
    if (!std::isfinite(budgetMs) || budgetMs <= 0 || budgetMs > 40000)
      throw std::invalid_argument("source model position budget invalid");
    std::vector<StraightVisualNode> original;
    std::vector<std::string> ids;
    std::unordered_map<std::string, std::size_t> index;
    for (std::size_t n = 0; n < nodes.size(); ++n) {
      const auto& node = nodes[n];
      ids.push_back(node.modelId);
      if (!index.emplace(node.modelId, n).second)
        throw std::invalid_argument("source model original identity duplicated");
      original.push_back({sanitizeNodeWidth(node, attributes),
        sanitizeNodeHeight(node, attributes), 0, 0});
    }
    std::vector<StraightVisualEdge> independent;
    std::vector<std::size_t> routeIndex;
    for (std::size_t e = 0; e < edges.size(); ++e) {
      if (edges[e].sourceModelId == edges[e].targetModelId) continue;
      independent.push_back({index.at(edges[e].sourceModelId), index.at(edges[e].targetModelId)});
      routeIndex.push_back(e);
    }
    const auto seed = source_input::propose(original, independent, ids, modelPath,
      began + std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double, std::milli>(std::min(5000.0, budgetMs * .25))));
    if (!seed.complete) throw std::runtime_error(seed.reason);
    const double generatedMs = elapsed();
    StraightVisualPlacementOptions options;
    // The refiner also rebuilds and scores its final scene after its search
    // deadline. Leave room for that cleanup plus the independent native
    // audits and complete serialization, including on a busy host. This
    // reservation stays inside the original 40-second position allowance.
    const double finalAuditReserveMs = std::min(8000.0, budgetMs * .2);
    options.budgetMs = std::max(0.0, budgetMs - generatedMs - finalAuditReserveMs);
    options.escapeBudgetMs = std::min(10000.0, options.budgetMs * .25);
    options.cardGapX = source_input::kCardGapX;
    options.cardGapY = source_input::kCardGapY;
    const auto placement = optimizeStraightVisualPlacement(seed.nodes, independent, ids, {}, options);
    if (placement.nodes.size() != nodes.size() || placement.routes.size() != routeIndex.size()
        || placement.after.nodeOverlaps != 0 || placement.after.invalidRoutes != 0)
      throw std::runtime_error("source model incomplete original geometry");
    for (std::size_t n = 0; n < nodes.size(); ++n) {
      if (placement.nodes[n].width != original[n].width
          || placement.nodes[n].height != original[n].height)
        throw std::runtime_error("source model changed original dimensions");
    }
    // Rollback coordinates are captured only after source inference and
    // refinement finish. They are never inputs to the learned prediction.
    for (const auto& node : nodes)
      rollback.emplace_back(attributes.x(node.handle), attributes.y(node.handle));
    for (std::size_t n = 0; n < nodes.size(); ++n) {
      attributes.x(nodes[n].handle) = placement.nodes[n].x;
      attributes.y(nodes[n].handle) = placement.nodes[n].y;
    }
    // Self declarations keep their original IDs and existing empty-route
    // semantics; every original nonself declaration receives its own line.
    std::vector<std::vector<RoutePoint>> routes(edges.size());
    for (std::size_t e = 0; e < routeIndex.size(); ++e) {
      const auto r = placement.routes[e];
      routes[routeIndex[e]] = {{r.sourceX, r.sourceY}, {r.targetX, r.targetY}};
    }
    auto resultMetadata = metadata;
    resultMetadata.actualAlgorithm = "SourceInputLayoutModel+StraightVisualPlacement";
    if (placement.escapeEvaluations > 0) resultMetadata.actualAlgorithm += "+StraightVisualEscape";
    resultMetadata.strategy = "source_input_layout_model";
    resultMetadata.strategyReason = "Fresh original source names, card dimensions and relations; generic shared weights; no prior coordinates or scene buffers.";
    resultMetadata.canonicalCrossing = canonical;
    std::vector<std::vector<std::string>> crossingIds(edges.size());
    std::size_t crossingPairs = 0;
    const auto crossings = detectRouteCrossings(edges, routes, crossingIds, crossingPairs);
    resultMetadata.rawRouteCrossings = crossingPairs;
    auto quality = measureLayoutQuality(nodes, edges, routes, attributes,
      &resultMetadata.leafBundles, &resultMetadata.clusterByModelId);
    quality.edgeCrossings = crossingPairs;
    applyRenderedCarrierMetricsIfRequested(nodes, edges, routes, attributes,
      resultMetadata.clusterByModelId, resultMetadata, quality, crossingPairs, false, false);
    quality.visualCrossings = quality.edgeCrossings + quality.edgeNodeIntersections
      + quality.nodeOverlaps + quality.bundleEdgeIntersections + quality.bundleNodeOverlaps;
    measureCanonicalCrossingDrawing(resultMetadata.canonicalCrossing, nodes, edges, routes, attributes);
    const auto& proof = resultMetadata.canonicalCrossing;
    if (!proof.completeRoutes || proof.invariantViolations || proof.degenerateSegments
        || proof.collinearOverlaps || proof.pointContacts || proof.selfIntersections
        || proof.adjacentEdgeIntersections || quality.nodeOverlaps
        || quality.nodeSpacingOverlaps || quality.bundleNodeOverlaps
        || !resultMetadata.leafBundles.empty() || !resultMetadata.clusterByModelId.empty())
      throw std::runtime_error("source model final original geometry audit failed");
    const auto bounds = measureBounds(nodes, routes, attributes);
    std::ostringstream serialized;
    writeLayoutJson(serialized, arguments.mode, resultMetadata, nodes, edges,
      attributes, routes, crossings, crossingIds, quality, bounds);
    const double completedMs = elapsed();
    if (completedMs > budgetMs) {
      std::fprintf(stderr,
        "[source-layout-model] deadline elapsed=%.3fms allowance=%.3fms reserve=%.3fms.\n",
        completedMs, budgetMs, finalAuditReserveMs);
      throw std::runtime_error("source model complete position deadline exceeded");
    }
    output << serialized.str();
    std::fprintf(stderr,
      "[source-layout-model] complete=1 originalNodes=%zu originalRoutes=%zu visual=%lld->%lld init=%.3fms position=%.3fms allowance=%.3fms reserve=%.3fms.\n",
      nodes.size(), independent.size(), static_cast<long long>(placement.before.visual()),
      static_cast<long long>(placement.after.visual()), generatedMs, completedMs, budgetMs, finalAuditReserveMs);
    return {true, completedMs};
  } catch (const std::exception& error) {
    restore();
    std::fprintf(stderr, "[source-layout-model] complete=0 fallback=legacy reason=%s.\n", error.what());
    return {false, elapsed()};
  }
}

}  // namespace djerd
