#pragma once

#include "types.h"
#include "geometry.h"
#include "renderedMetrics.h"
#include "rectangleCollisionIndex.h"
#include "io.h"
#include "clusterGraph.h"
#include "canonicalCrossingMetrics.h"
#include "crossingLowerBound.h"

#include <cstdlib>
#include <numeric>

#include <ogdf/basic/Graph.h>
#include <ogdf/basic/basic.h>
#include <ogdf/basic/GraphAttributes.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <queue>
#include <random>
#include <stack>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ogdf { class SubgraphPlanarizer; }

namespace djerd {

struct CanonicalTopologyFingerprint {
  std::string value;
  std::size_t edgeCount = 0;
  std::size_t nodeCount = 0;
};

struct ClusterRunOptions {
  std::string innerMode;
  std::string metaMode = "fmmm";
  double innerLayerDistance = 140.0;
  double innerNodeDistance = 64.0;
  double innerFmmEdgeLength = 220.0;
  double innerFmmNodeSize = 72.0;
  uint32_t innerFmmIterations = 300;
  double interClusterPadding = 240.0;
  double metaUnitEdgeLength = 1200.0;
  double metaLayerDistance = 320.0;
  double metaNodeDistance = 200.0;
};

struct LouvainGraph {
  std::vector<std::vector<std::pair<std::size_t, double>>> adj;
  std::vector<double> degree;
  double totalWeight2m = 0.0;
};

struct ClusterGroupLayout {
  std::vector<std::size_t> nodeIndices;
  double height = 0.0;
  double width = 0.0;
};

struct RenderedDensityMetrics {
  std::size_t totalCells = 0;
  std::size_t emptyCells = 0;
  std::size_t occupiedCells = 0;
  std::size_t denseCells = 0;
  std::size_t objectCount = 0;
  double emptyRatio = 0.0;
  double p50 = 0.0;
  double p90 = 0.0;
  double maxCell = 0.0;
  double imbalance = 0.0;
  double score = 0.0;
};

struct RenderedNodeClearanceMetrics {
  std::size_t violations = 0;
  double minimum = 0.0;
};

struct PlanarBackboneLayoutResult {
  std::size_t uniqueEdges = 0;
  std::size_t initiallyDeleted = 0;
  std::size_t reinserted = 0;
  std::size_t remainingDeleted = 0;
  std::size_t components = 0;
};

constexpr double kRenderedLeafCellW = 200.0;

constexpr double kRenderedLeafCellH = 56.0;

constexpr double kRenderedLeafGapX = 10.0;

constexpr double kRenderedLeafGapY = 8.0;

constexpr double kRenderedBundleHeader = 48.0;

constexpr double kRenderedBundlePad = 16.0;

bool isConstrainedForceMode(const std::string& mode);

bool isStraightLineRoutingMode(const std::string& mode);

bool isSupportedMode(const std::string& mode);

std::size_t idealThreadCount();

double readDoubleEnv(
  const char* name,
  double fallback,
  double minValue,
  double maxValue);

bool readBoolEnv(const char* name, bool fallback);

CanonicalTopologyFingerprint fingerprintCanonicalTopology(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges);

std::filesystem::path canonicalCrossingCachePath(
  const CanonicalTopologyFingerprint& fingerprint);

bool readCanonicalCrossingCache(
  const std::filesystem::path& cachePath,
  const CanonicalTopologyFingerprint& fingerprint,
  CanonicalCrossingMetadata& metadata);

void writeCanonicalCrossingCache(
  const std::filesystem::path& cachePath,
  const CanonicalTopologyFingerprint& fingerprint,
  const CanonicalCrossingMetadata& metadata);

CanonicalCrossingMetadata certifyCanonicalCrossingTopology(
  const ogdf::Graph& graph,
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges);

void measureCanonicalCrossingDrawing(
  CanonicalCrossingMetadata& metadata,
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes);

double visualNodeMargin();

double leafBundleVisualMargin();

std::vector<std::vector<RoutePoint>> routeAllEdgesStraight(
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

std::vector<std::vector<RoutePoint>> routeAllEdgesCrossAware(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

std::vector<EdgeCrossingRecord> detectRouteCrossings(
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::vector<RoutePoint>>& routes,
  std::vector<std::vector<std::string>>& crossingIdsByEdge,
  std::size_t& totalCrossings);

void packDisconnectedComponents(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

void enforceNodeSeparationStrong(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes);

void pullLowDegreeNodesInward(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  int maxIterations = 24,
  std::size_t degreeThreshold = 2,
  double damping = 0.35);

void compactClusterOutliers(
  const std::vector<NodeRecord>& nodes,
  const std::unordered_map<std::string, std::string>& clusterByModelId,
  ogdf::GraphAttributes& attributes,
  double outlierMedianMultiplier = 1.8);

void compactDistantConnectedNodes(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

bool compactExcessiveLayoutFootprint(
  const std::string& mode,
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

bool compactRigidLayoutFootprint(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes);

void recomputeLeafBundleBboxesFromNodes(
  std::vector<LeafBundleRecord>& leafBundles,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes);

std::size_t clearLeafBundleNodeMargins(
  std::vector<LeafBundleRecord>& leafBundles,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  bool logResult = true);

std::size_t attachIsolatedNodesByName(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

std::size_t compactIsolatedBBoxOutliers(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

std::size_t compactSidecarBBoxComponents(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

std::unordered_set<std::string> absorbedLeafBundleIds(
  const std::vector<LeafBundleRecord>& leafBundles);

ogdf::SubgraphPlanarizer* createBoundedSubgraphPlanarizer();

ogdf::SubgraphPlanarizer* createHighQualityPlanarizer();

void sanitizeLayoutGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

bool isSugiyamaMode(const std::string& mode);

void runSugiyamaLayout(const std::string& mode, ogdf::GraphAttributes& attributes);

std::vector<std::vector<std::size_t>> buildProjectedForestAdjacency(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges);

std::size_t chooseTreeRoot(
  const std::vector<std::size_t>& component,
  const std::vector<std::vector<std::size_t>>& adjacency);

std::vector<std::vector<std::size_t>> collectTreeLevels(
  std::size_t root,
  const std::vector<std::vector<std::size_t>>& adjacency,
  std::vector<bool>& visited);

void applyLayeredTreeCoordinates(
  const std::vector<NodeRecord>& nodes,
  const std::vector<std::vector<std::size_t>>& levels,
  double componentY,
  ogdf::GraphAttributes& attributes,
  double& componentHeight);

void applyRadialTreeCoordinates(
  const std::vector<NodeRecord>& nodes,
  const std::vector<std::vector<std::size_t>>& levels,
  double componentX,
  ogdf::GraphAttributes& attributes,
  double& componentWidth);

void runProjectedTreeLayout(
  const std::string& mode,
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

void runFastMultipoleLayout(
  ogdf::GraphAttributes& attributes,
  uint32_t iterations,
  uint32_t precision,
  bool randomize);

bool hasMeaningfulClusters(const std::vector<NodeRecord>& nodes);

std::vector<std::string> assignBiconnectedClusterLabels(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  std::size_t& outBridgeCount,
  std::size_t& outBccCount,
  std::size_t& outLargestClusterSize);

std::vector<std::size_t> findDominantHubs(
  const std::vector<std::vector<std::size_t>>& adj);

std::vector<std::string> assignHubExcludedBccLabels(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  std::size_t& outHubCount,
  std::size_t& outResidualBridgeCount,
  std::size_t& outResidualBccCount);

std::vector<int> runLouvainPhase(
  const std::vector<std::vector<std::pair<std::size_t, double>>>& adj,
  const std::vector<double>& degree,
  double totalWeight2m,
  std::size_t maxPasses,
  std::size_t& outIterations,
  double resolution = 1.0);

LouvainGraph buildLouvainGraph(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges);

void attachLeavesAndCompress(
  std::vector<int>& comm,
  const std::vector<std::vector<std::pair<std::size_t, double>>>& adj);

std::vector<std::string> assignLouvainClusterLabels(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  std::size_t& outCommunityCount,
  std::size_t& outIterations,
  std::size_t maxPasses = 10);

std::map<std::pair<std::size_t, std::size_t>, double> computeEdgeBetweenness(
    const std::vector<std::vector<std::size_t>>& adj);

std::vector<std::string> assignGirvanNewmanClusterLabels(
    const std::vector<NodeRecord>& nodes,
    const std::vector<EdgeRecord>& edges,
    std::size_t& outCommunityCount,
    std::size_t& outRemovedEdges);

std::vector<std::string> assignCommunityClusterLabels(
    const std::vector<NodeRecord>& nodes,
    const std::vector<EdgeRecord>& edges,
    std::size_t& outCommunityCount,
    std::size_t& outIterationsOrRemovals,
    std::string& outAlgorithm);

std::vector<std::string> assignTwoLevelLouvainLabels(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  std::size_t& outLevel1CommunityCount,
  std::size_t& outLevel2CommunityCount,
  std::size_t& outIterations);

std::vector<std::string> assignStructuralClusterLabels(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  std::size_t targetClusterCount);

std::vector<std::string> appendLevel2BccLabels(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::string>& level1Labels,
  std::size_t& outBridgeCountTotal,
  std::size_t& outDistinctCombinedClusters,
  std::size_t& outLargestCombinedClusterSize,
  std::size_t& outInnerSplitClusters,
  std::size_t minRecurseSize = 30);

std::vector<std::string> assignTwoLevelBccLabels(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  std::size_t& outBridgeCountTotal,
  std::size_t& outBccCountTotal,
  std::size_t& outLargestClusterSize,
  std::size_t& outInnerSplitClusters,
  std::size_t minRecurseSize = 30);

void runHierarchicalClusterLayout(
  const ClusterRunOptions& options,
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  std::size_t& outClusterCount,
  std::size_t& outInterClusterEdges);

void runClusteredByAppLayout(
  const ClusterRunOptions& options,
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  std::size_t& outClusterCount,
  std::size_t& outInterClusterEdges);

void compactWhitespaceAxis(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  bool horizontal,
  double gap);

void compactGlobalLayout(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  double gap);

void applySiftingSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

void clearEdgeBends(
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

void applyGlobalSiftingSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

void applyGreedyInsertSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

void applyGreedySwitchSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

void applyGridSiftingSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

void applySplitSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

void applyPlanarSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

void applyOrthogonalSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

void applyPlanarGridSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

void applyStraightLineSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

void applySchnyderSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

void applyUpwardSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  bool layerBased);

void applyVisibilitySurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

void applyPivotMdsGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

void applyUmlPlanarSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

std::string clusterKeyForModelId(const std::string& modelId);

void applyClusterSurrogateGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  bool orthogonal);

std::vector<std::vector<std::size_t>> collectConnectedComponents(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges);

std::vector<std::vector<std::size_t>> buildUndirectedAdjacency(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges);

Rect componentRect(
  const std::vector<NodeRecord>& nodes,
  const std::vector<std::size_t>& component,
  ogdf::GraphAttributes& attributes);

void translateComponent(
  const std::vector<NodeRecord>& nodes,
  const std::vector<std::size_t>& component,
  ogdf::GraphAttributes& attributes,
  double dx,
  double dy);

double centerDistance(
  const NodeRecord& left,
  const NodeRecord& right,
  ogdf::GraphAttributes& attributes);

double medianValue(std::vector<double> values);

void resolveNodeOverlaps(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes);

Rect expandedNodeRectAt(
  const NodeRecord& node,
  ogdf::GraphAttributes& attributes,
  double centerX,
  double centerY);

bool hasNodeSpacingConflicts(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes);

void placeNodesWithoutOverlaps(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes);

void enforceNodeSeparation(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes);

Rect graphNodeBounds(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes);

double clampToSpan(double value, double minValue, double maxValue);

bool almostSamePoint(const RoutePoint& left, const RoutePoint& right);

bool isCollinear(const RoutePoint& left, const RoutePoint& middle, const RoutePoint& right);

std::vector<RoutePoint> compressRoutePoints(std::vector<RoutePoint> points);

std::size_t applyPositionsTsvOverride(
  const std::string& positionsTsv,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes);

std::size_t applyRoutesTsvOverride(
  const std::string& routesTsv,
  const std::vector<EdgeRecord>& edges,
  std::vector<std::vector<RoutePoint>>& routes);

bool segmentIntersectsRect(const RoutePoint& start, const RoutePoint& end, const Rect& rect);

long long metricLaneKey(double value);

bool intervalsOverlap(double leftStart, double leftEnd, double rightStart, double rightEnd);

double distributedLaneOffset(std::size_t lineIndex);

LineIntent makeLineIntent(
  const EdgeRecord& edge,
  std::size_t lineIndex,
  ogdf::GraphAttributes& attributes);

std::vector<NodeObstacle> makeNodeObstacles(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  double margin,
  ogdf::node sourceHandle,
  ogdf::node targetHandle);

std::vector<Rect> collectObstacleRects(const std::vector<NodeObstacle>& obstacles);

std::pair<double, double> leafBundleRenderSize(std::size_t memberCount);

Rect renderedLeafBundleRect(const LeafBundleRecord& bundle, double margin = 0.0);

std::vector<Rect> renderedLeafTileRects(
    const LeafBundleRecord& bundle,
    double margin = 0.0);

std::size_t clearLeafBundleExternalNodeMargins(
  std::vector<LeafBundleRecord>& leafBundles,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes);

std::size_t clearNodeVisualOverlaps(
  const std::vector<LeafBundleRecord>& leafBundles,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes);

std::size_t clearNodeSpacingOverlaps(
  const std::vector<LeafBundleRecord>& leafBundles,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes);

bool makeLineSegment(
  const std::string& lineId,
  std::size_t lineIndex,
  const RoutePoint& start,
  const RoutePoint& end,
  LineSegment& segment);

std::vector<LineSegment> buildLineSegments(
  const std::vector<RoutePoint>& points,
  std::size_t lineIndex,
  const std::string& lineId);

double occupancyCostForAxisSegment(
  const RouteOccupancy* occupancy,
  bool horizontal,
  long long laneKey,
  double start,
  double end);

double routeLength(const std::vector<RoutePoint>& points);

double routeOccupancyPenalty(
  const std::vector<RoutePoint>& points,
  const RouteOccupancy* occupancy);

double routeAxisOverlapDebt(
  const std::vector<RoutePoint>& points,
  const RouteOccupancy* occupancy,
  double lengthWeight);

double routeScore(
  const std::vector<RoutePoint>& points,
  const std::vector<NodeObstacle>& obstacles,
  const RouteOccupancy* occupancy = nullptr);

void recordRouteOccupancy(
  const std::vector<RoutePoint>& points,
  const LineIntent& line,
  RouteOccupancy& occupancy);

void removeRouteOccupancy(
  const std::vector<RoutePoint>& points,
  const LineIntent& line,
  RouteOccupancy& occupancy);

std::size_t countAxisSegmentOverlaps(
  std::vector<LineSegment>& segments,
  std::vector<bool>& overlappingEdgeFlags);

std::size_t countNodeRectOverlaps(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  bool includeSpacing,
  const std::unordered_set<std::string>* ignoredModelIds = nullptr);

std::unordered_set<std::string> renderedLeafTileIds(
  const std::vector<LeafBundleRecord>& leafBundles);

Rect expandForRenderedNodeClearance(const Rect& rect);

double rectangleClearance(const Rect& left, const Rect& right);

RenderedNodeClearanceMetrics measureRenderedNodeClearance(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  const std::vector<LeafBundleRecord>& leafBundles);

std::size_t clearRenderedNodeClearance(
  std::vector<LeafBundleRecord>& leafBundles,
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes);

RenderedDensityMetrics measureRenderedDensity(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  const std::vector<LeafBundleRecord>& leafBundles,
  double requestedCellSize);

std::vector<std::string> modelNameTokens(const std::string& modelId);

bool properSegmentIntersection(
  const RoutePoint& leftStart,
  const RoutePoint& leftEnd,
  const RoutePoint& rightStart,
  const RoutePoint& rightEnd,
  RoutePoint& intersection);

LayoutQualityMetrics measureLayoutQuality(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes,
  const std::vector<LeafBundleRecord>* leafBundles,
  const std::unordered_map<std::string, std::string>* clusterByModelId = nullptr);

void addLaneValue(std::vector<double>& lanes, double value);

void ensureLaneValue(std::vector<double>& lanes, double value);

std::vector<double> nearestUniqueLaneValues(
  std::vector<double> lanes,
  double reference,
  std::size_t limit);

double normalizeLaneValue(double value);

void addRequiredLane(std::vector<double>& lanes, double value);

double distanceToClosestAnchor(double value, const std::vector<double>& anchors);

std::vector<double> selectVisibilityLanes(
  std::vector<double> required,
  std::vector<double> candidates,
  const std::vector<double>& anchors,
  std::size_t limit);

int findLaneIndex(const std::vector<double>& lanes, double value);

bool pointInsideRect(const RoutePoint& point, const Rect& rect);

bool pointInsideAnyRect(const RoutePoint& point, const std::vector<Rect>& obstacles);

std::vector<std::pair<double, double>> blockedIntervalsForHorizontalLane(
  double y,
  const std::vector<Rect>& obstacles);

std::vector<std::pair<double, double>> blockedIntervalsForVerticalLane(
  double x,
  const std::vector<Rect>& obstacles);

bool intervalIntersectsAnyBlocked(
  double start,
  double end,
  const std::vector<std::pair<double, double>>& blockedIntervals);

VisibilityRoute routeVisibilityGrid(
  const RoutePoint& start,
  const RoutePoint& end,
  const Rect& graphBounds,
  const std::vector<Rect>& obstacles,
  double laneOffset);

VisibilityRoute routeVisibilityGridWithPorts(
  const std::vector<VisibilityPort>& sourcePorts,
  const std::vector<VisibilityPort>& targetPorts,
  const Rect& graphBounds,
  const std::vector<Rect>& obstacles,
  double laneOffset,
  const RouteOccupancy* occupancy = nullptr);

VisibilityPort makeVisibilityPort(
  const Rect& rect,
  const std::string& side,
  double offset,
  double inset,
  double stub);

std::vector<VisibilityPort> makeVisibilityPorts(
  const Rect& rect,
  double offset,
  double inset,
  double stub);

std::vector<RoutePoint> routeObstacleAwareLine(
  const LineIntent& line,
  const Rect& graphBounds,
  const std::vector<NodeObstacle>& obstacles,
  const RouteOccupancy* occupancy = nullptr);

PlanarBackboneLayoutResult runPlanarBackboneLayout(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

std::string describeLayoutAlgorithm(const std::string& mode);

LayoutRunMetadata makeLayoutRunMetadata(const std::string& mode);

LayoutRunMetadata runLayout(
  const std::string& mode,
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

void updateBounds(Bounds& bounds, double x, double y, bool& hasPoint);

Bounds measureBounds(
  const std::vector<NodeRecord>& nodes,
  const std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes);

std::vector<std::vector<RoutePoint>> routeAllEdges(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  bool avoidLaneOverlaps = false);

RoutePoint straightPortOnRect(const Rect& rect, const Rect& target);

int sideOfPortOnRect(const RoutePoint& point, const Rect& rect);

RoutePoint slidePortOnRectSide(
  const RoutePoint& point,
  const Rect& rect,
  int side);

std::vector<RoutePoint> routeStraightLine(const LineIntent& line);

std::vector<RoutePoint> routeStraightWithDetour(
  const LineIntent& line,
  const std::vector<NodeObstacle>& obstacles,
  int maxDetours);

std::vector<std::vector<RoutePoint>> routeAllEdgesStraightSmart(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

int axisForNeighbor(double dx, double dy);

void placeAxisGroup(
  const std::vector<NodeRecord>& nodes,
  const std::vector<std::size_t>& group,
  int axis,
  double hubX,
  double hubY,
  double axisDistance,
  double slotGapX,
  double slotGapY,
  ogdf::GraphAttributes& attributes);

void refineStraightHubAxisLayout(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

double capShiftVector(double& dx, double& dy, double limit);

std::size_t applyNodeShifts(
  const std::vector<NodeRecord>& nodes,
  ogdf::GraphAttributes& attributes,
  std::vector<double>& shiftX,
  std::vector<double>& shiftY,
  double limit);

std::size_t repelNodesFromStraightEdgeCorridors(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

std::size_t nudgeNodesFromRouteIntersections(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes);

void refineConstrainedForceLayout(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes);

double crossProduct(double ax, double ay, double bx, double by);

bool sharesEndpoint(const EdgeRecord& left, const EdgeRecord& right);

template <typename Transform>
void transformLayoutGeometry(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  ogdf::GraphAttributes& attributes,
  Transform transform) {
  for (const NodeRecord& node : nodes) {
    const auto next = transform(
      sanitizeNodeCenterX(node, attributes),
      sanitizeNodeCenterY(node, attributes));
    if (isFiniteCoordinate(next.first) && isFiniteCoordinate(next.second)) {
      attributes.x(node.handle) = next.first;
      attributes.y(node.handle) = next.second;
    }
  }

  for (const EdgeRecord& edge : edges) {
    ogdf::DPolyline transformedBends;
    for (const ogdf::DPoint& bend : attributes.bends(edge.handle)) {
      if (!isFiniteCoordinate(bend.m_x) || !isFiniteCoordinate(bend.m_y)) {
        continue;
      }

      const auto next = transform(bend.m_x, bend.m_y);
      if (isFiniteCoordinate(next.first) && isFiniteCoordinate(next.second)) {
        transformedBends.pushBack(ogdf::DPoint(next.first, next.second));
      }
    }

    attributes.bends(edge.handle) = transformedBends;
  }
}

void minimizeClusterKnots(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  const std::unordered_map<std::string, std::string>& clusterByModelIdFull,
  ogdf::GraphAttributes& attributes, LayoutRunMetadata& metadata);

void applyVisualKnot(
  const std::vector<NodeRecord>& nodes,
  const std::vector<EdgeRecord>& edges,
  std::vector<std::vector<RoutePoint>>& routes,
  ogdf::GraphAttributes& attributes,
  const LayoutRunMetadata& metadata,
  const std::vector<std::string>& carrierIdByEdgePre);

}  // namespace djerd
