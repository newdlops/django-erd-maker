import type {
  CanonicalCrossingMetadata,
  LayoutEngineMetadata,
  LayoutSnapshot,
} from "../../../shared/graph/layoutContract";

export interface CanonicalCrossingNonRegression {
  adjacentBase: number | undefined;
  adjacentCandidate: number | undefined;
  adjacentOk: boolean;
  nodeHitsBase: number | undefined;
  nodeHitsCandidate: number | undefined;
  nodeHitsOk: boolean;
  ok: boolean;
}

/**
 * Non-regression gate for the complete, unbundled route set.
 *
 * `visualCrossings` and `edgeCrossings` describe reduced carrier geometry in
 * optimized layouts. `rawRouteCrossings` is the metric that still represents
 * every logical edge, which is also what the webview must keep visible at
 * every zoom level.
 */
export interface AllEdgeCrossingNonRegression {
  gain: number;
  ok: boolean;
  rawRouteBase: number | undefined;
  rawRouteCandidate: number | undefined;
}

export interface CanonicalObstacleRelief extends CanonicalCrossingNonRegression {
  adjacentGain: number;
  canonicalGain: number;
  nodeHitsGain: number;
  visualDebt: number;
  visualDebtPerCanonicalGain: number;
}

export interface CanonicalRouteRepairEvaluation {
  nodeHitsBase: number | undefined;
  nodeHitsCandidate: number | undefined;
  nodeHitsGain: number;
  ok: boolean;
  reasons: string[];
  routeBboxGrowth: number;
}

export interface CanonicalRouteRepairEvaluationOptions {
  maxRouteBboxGrowth?: number;
}

export const DEFAULT_CANONICAL_OBSTACLE_RELIEF_MAX_VISUAL_DEBT_PER_GAIN = 0.05;
export const DEFAULT_CANONICAL_ROUTE_REPAIR_MAX_ROUTE_BBOX_GROWTH = 1.01;
export const DEFAULT_EDGE_NODE_POLISH_HOLISTIC_RESERVE_MS = 30_000;

export interface EdgeNodePolishBudgetPlan {
  budgetLimited: boolean;
  holisticPending: boolean;
  reservedMs: number;
  timeoutMs: number;
}

export function planEdgeNodePolishCandidateBudget(
  remainingMs: number,
  configuredTimeoutMs: number,
  currentVariant: string,
  laterVariants: readonly string[],
  canonicalObstacleReliefEnabled: boolean,
  configuredHolisticReserveMs =
    DEFAULT_EDGE_NODE_POLISH_HOLISTIC_RESERVE_MS,
): EdgeNodePolishBudgetPlan {
  const safeRemainingMs = Number.isFinite(remainingMs)
    ? Math.max(0, Math.floor(remainingMs))
    : 0;
  const safeConfiguredTimeoutMs = Number.isFinite(configuredTimeoutMs)
    ? Math.max(1, Math.floor(configuredTimeoutMs))
    : 1;
  const safeConfiguredReserveMs = Number.isFinite(configuredHolisticReserveMs)
    ? Math.max(0, Math.floor(configuredHolisticReserveMs))
    : 0;
  const holisticPending = canonicalObstacleReliefEnabled
    && currentVariant !== "holistic"
    && laterVariants.includes("holistic");
  const reservedMs = holisticPending
    ? Math.min(safeRemainingMs, safeConfiguredReserveMs)
    : 0;
  const availableMs = Math.max(0, safeRemainingMs - reservedMs);
  const timeoutMs = Math.min(safeConfiguredTimeoutMs, availableMs);
  return {
    budgetLimited: timeoutMs < safeConfiguredTimeoutMs,
    holisticPending,
    reservedMs,
    timeoutMs,
  };
}

function finiteNonNegativeCount(value: unknown): number | undefined {
  return typeof value === "number"
    && Number.isFinite(value)
    && value >= 0
    ? value
    : undefined;
}

export function evaluateAllEdgeCrossingNonRegression(
  baseMetadata: LayoutEngineMetadata | undefined,
  candidateMetadata: LayoutEngineMetadata | undefined,
): AllEdgeCrossingNonRegression {
  const rawRouteBase = finiteNonNegativeCount(
    baseMetadata?.rawRouteCrossings,
  );
  const rawRouteCandidate = finiteNonNegativeCount(
    candidateMetadata?.rawRouteCrossings,
  );
  const ok = rawRouteBase === undefined
    ? true
    : rawRouteCandidate !== undefined && rawRouteCandidate <= rawRouteBase;
  return {
    gain:
      rawRouteBase !== undefined && rawRouteCandidate !== undefined
        ? Math.max(0, rawRouteBase - rawRouteCandidate)
        : 0,
    ok,
    rawRouteBase,
    rawRouteCandidate,
  };
}

const CANONICAL_ROUTE_REPAIR_COUNT_KEYS: ReadonlyArray<
  keyof CanonicalCrossingMetadata
> = [
  "adjacentEdgeIntersections",
  "routeCrossingPairs",
  "routeCrossingPoints",
  "nonProperContacts",
  "invariantViolations",
  "degenerateSegments",
  "collinearOverlaps",
  "pointContacts",
  "selfIntersections",
];

const CANONICAL_ROUTE_REPAIR_VISUAL_KEYS: ReadonlyArray<
  keyof LayoutEngineMetadata
> = [
  "visualCrossings",
  "edgeCrossings",
  "edgeNodeIntersections",
  "nodeOverlaps",
  "bundleEdgeIntersections",
  "bundleNodeOverlaps",
  "overlappingEdges",
  "edgeSegmentOverlaps",
  "rawRouteCrossings",
  "nodeSpacingOverlaps",
];

const ROUTE_REPAIR_POSITION_EPSILON = 0.002;

/**
 * Fail-closed gate for the route-repair-only round trip.
 *
 * The native repair already rolls its route snapshot back on canonical or
 * rendered-metric debt. This second gate verifies that the orchestration also
 * preserved the accepted topology and node placement, received a complete
 * route set, and achieved a strict non-incident-node-hit reduction without
 * spending any visual debt.
 */
export function evaluateCanonicalRouteRepairCandidate(
  base: LayoutSnapshot,
  candidate: LayoutSnapshot,
  options: CanonicalRouteRepairEvaluationOptions = {},
): CanonicalRouteRepairEvaluation {
  const reasons: string[] = [];
  const baseCanonical = base.engineMetadata?.canonicalCrossing;
  const candidateCanonical = candidate.engineMetadata?.canonicalCrossing;
  const nodeHitsBase = finiteNonNegativeCount(
    baseCanonical?.nonIncidentNodeHits,
  );
  const nodeHitsCandidate = finiteNonNegativeCount(
    candidateCanonical?.nonIncidentNodeHits,
  );

  if (!baseCanonical || !candidateCanonical) {
    reasons.push("canonical-metadata-missing");
  } else {
    if (
      candidateCanonical.nodeCount !== baseCanonical.nodeCount
      || candidateCanonical.edgeCount !== baseCanonical.edgeCount
      || candidateCanonical.domain !== baseCanonical.domain
      || candidateCanonical.lowerBound !== baseCanonical.lowerBound
    ) {
      reasons.push("canonical-topology-changed");
    }
    if (!candidateCanonical.completeRoutes) {
      reasons.push("canonical-routes-incomplete");
    }
    if (candidateCanonical.boundViolation) {
      reasons.push("canonical-bound-violation");
    }
    if (baseCanonical.properDrawing && !candidateCanonical.properDrawing) {
      reasons.push("canonical-proper-drawing-regressed");
    }
    for (const key of CANONICAL_ROUTE_REPAIR_COUNT_KEYS) {
      pushNonRegressionReason(
        reasons,
        `canonical-${String(key)}`,
        baseCanonical[key],
        candidateCanonical[key],
      );
    }
  }

  if (
    nodeHitsBase === undefined
    || nodeHitsCandidate === undefined
    || nodeHitsCandidate >= nodeHitsBase
  ) {
    reasons.push("canonical-node-hit-not-improved");
  }

  for (const key of CANONICAL_ROUTE_REPAIR_VISUAL_KEYS) {
    pushNonRegressionReason(
      reasons,
      `visual-${String(key)}`,
      base.engineMetadata?.[key],
      candidate.engineMetadata?.[key],
    );
  }

  if (candidate.mode !== base.mode) {
    reasons.push("layout-mode-changed");
  }
  if (!sameNodePlacement(base, candidate)) {
    reasons.push("node-placement-changed");
  }
  if (!sameCompleteRoutedEdgeSet(base, candidate)) {
    reasons.push("routed-edge-set-changed-or-incomplete");
  }
  const baseRouteBboxArea = routeBoundingBoxArea(base);
  const candidateRouteBboxArea = routeBoundingBoxArea(candidate);
  const routeBboxGrowth = baseRouteBboxArea > 0
    ? candidateRouteBboxArea / baseRouteBboxArea
    : candidateRouteBboxArea <= ROUTE_REPAIR_POSITION_EPSILON
      ? 1
      : Number.POSITIVE_INFINITY;
  const maxRouteBboxGrowth =
    typeof options.maxRouteBboxGrowth === "number"
      && Number.isFinite(options.maxRouteBboxGrowth)
      && options.maxRouteBboxGrowth >= 1
      ? options.maxRouteBboxGrowth
      : DEFAULT_CANONICAL_ROUTE_REPAIR_MAX_ROUTE_BBOX_GROWTH;
  if (
    !Number.isFinite(routeBboxGrowth)
    || routeBboxGrowth > maxRouteBboxGrowth
  ) {
    reasons.push("route-bbox-growth-exceeded");
  }

  return {
    nodeHitsBase,
    nodeHitsCandidate,
    nodeHitsGain:
      nodeHitsBase !== undefined && nodeHitsCandidate !== undefined
        ? Math.max(0, nodeHitsBase - nodeHitsCandidate)
        : 0,
    ok: reasons.length === 0,
    reasons,
    routeBboxGrowth,
  };
}

function pushNonRegressionReason(
  reasons: string[],
  label: string,
  base: unknown,
  candidate: unknown,
): void {
  const baseCount = finiteNonNegativeCount(base);
  const candidateCount = finiteNonNegativeCount(candidate);
  if (
    baseCount === undefined
    || candidateCount === undefined
    || candidateCount > baseCount
  ) {
    reasons.push(`${label}-regressed-or-missing`);
  }
}

function sameNodePlacement(
  base: LayoutSnapshot,
  candidate: LayoutSnapshot,
): boolean {
  if (candidate.nodes.length !== base.nodes.length) {
    return false;
  }
  const candidateById = new Map(
    candidate.nodes.map((node) => [node.modelId, node] as const),
  );
  if (candidateById.size !== candidate.nodes.length) {
    return false;
  }
  return base.nodes.every((baseNode) => {
    const candidateNode = candidateById.get(baseNode.modelId);
    return candidateNode !== undefined
      && nearlyEqual(candidateNode.position.x, baseNode.position.x)
      && nearlyEqual(candidateNode.position.y, baseNode.position.y)
      && nearlyEqual(candidateNode.size.width, baseNode.size.width)
      && nearlyEqual(candidateNode.size.height, baseNode.size.height);
  });
}

function sameCompleteRoutedEdgeSet(
  base: LayoutSnapshot,
  candidate: LayoutSnapshot,
): boolean {
  if (candidate.routedEdges.length !== base.routedEdges.length) {
    return false;
  }
  const baseIds = new Set(base.routedEdges.map((edge) => edge.edgeId));
  const candidateIds = new Set(candidate.routedEdges.map((edge) => edge.edgeId));
  if (
    baseIds.size !== base.routedEdges.length
    || candidateIds.size !== candidate.routedEdges.length
    || candidateIds.size !== baseIds.size
  ) {
    return false;
  }
  return candidate.routedEdges.every(
    (edge) => baseIds.has(edge.edgeId) && edge.points.length >= 2,
  );
}

function nearlyEqual(left: number, right: number): boolean {
  return Number.isFinite(left)
    && Number.isFinite(right)
    && Math.abs(left - right) <= ROUTE_REPAIR_POSITION_EPSILON;
}

function routeBoundingBoxArea(layout: LayoutSnapshot): number {
  let minX = Number.POSITIVE_INFINITY;
  let minY = Number.POSITIVE_INFINITY;
  let maxX = Number.NEGATIVE_INFINITY;
  let maxY = Number.NEGATIVE_INFINITY;
  for (const edge of layout.routedEdges) {
    for (const point of edge.points) {
      if (!Number.isFinite(point.x) || !Number.isFinite(point.y)) {
        return Number.POSITIVE_INFINITY;
      }
      minX = Math.min(minX, point.x);
      minY = Math.min(minY, point.y);
      maxX = Math.max(maxX, point.x);
      maxY = Math.max(maxY, point.y);
    }
  }
  if (
    !Number.isFinite(minX)
    || !Number.isFinite(minY)
    || !Number.isFinite(maxX)
    || !Number.isFinite(maxY)
  ) {
    return Number.POSITIVE_INFINITY;
  }
  return Math.max(0, maxX - minX) * Math.max(0, maxY - minY);
}

function nonRegression(
  base: number | undefined,
  candidate: number | undefined,
): boolean {
  if (base === undefined) {
    return true;
  }
  return candidate !== undefined && candidate <= base;
}

export function evaluateCanonicalCrossingNonRegression(
  baseMetadata: LayoutEngineMetadata | undefined,
  candidateMetadata: LayoutEngineMetadata | undefined,
): CanonicalCrossingNonRegression {
  const adjacentBase = finiteNonNegativeCount(
    baseMetadata?.canonicalCrossing?.adjacentEdgeIntersections,
  );
  const adjacentCandidate = finiteNonNegativeCount(
    candidateMetadata?.canonicalCrossing?.adjacentEdgeIntersections,
  );
  const nodeHitsBase = finiteNonNegativeCount(
    baseMetadata?.canonicalCrossing?.nonIncidentNodeHits,
  );
  const nodeHitsCandidate = finiteNonNegativeCount(
    candidateMetadata?.canonicalCrossing?.nonIncidentNodeHits,
  );
  const adjacentOk = nonRegression(adjacentBase, adjacentCandidate);
  const nodeHitsOk = nonRegression(nodeHitsBase, nodeHitsCandidate);
  return {
    adjacentBase,
    adjacentCandidate,
    adjacentOk,
    nodeHitsBase,
    nodeHitsCandidate,
    nodeHitsOk,
    ok: adjacentOk && nodeHitsOk,
  };
}

export function evaluateCanonicalObstacleRelief(
  baseMetadata: LayoutEngineMetadata | undefined,
  candidateMetadata: LayoutEngineMetadata | undefined,
  baseVisual: number,
  candidateVisual: number,
  maxVisualDebtPerCanonicalGain =
    DEFAULT_CANONICAL_OBSTACLE_RELIEF_MAX_VISUAL_DEBT_PER_GAIN,
): CanonicalObstacleRelief {
  const nonRegression = evaluateCanonicalCrossingNonRegression(
    baseMetadata,
    candidateMetadata,
  );
  const adjacentGain = nonRegression.adjacentBase !== undefined
      && nonRegression.adjacentCandidate !== undefined
    ? Math.max(0, nonRegression.adjacentBase - nonRegression.adjacentCandidate)
    : 0;
  const nodeHitsGain = nonRegression.nodeHitsBase !== undefined
      && nonRegression.nodeHitsCandidate !== undefined
    ? Math.max(0, nonRegression.nodeHitsBase - nonRegression.nodeHitsCandidate)
    : 0;
  const canonicalGain = adjacentGain + nodeHitsGain;
  const finiteVisuals = Number.isFinite(baseVisual)
    && Number.isFinite(candidateVisual)
    && baseVisual >= 0
    && candidateVisual >= 0;
  const visualDebt = finiteVisuals
    ? Math.max(0, candidateVisual - baseVisual)
    : Number.POSITIVE_INFINITY;
  const visualDebtPerCanonicalGain = canonicalGain > 0
    ? visualDebt / canonicalGain
    : Number.POSITIVE_INFINITY;
  const boundedDebt = Number.isFinite(maxVisualDebtPerCanonicalGain)
    && maxVisualDebtPerCanonicalGain >= 0
    && visualDebtPerCanonicalGain <= maxVisualDebtPerCanonicalGain;
  return {
    ...nonRegression,
    adjacentGain,
    canonicalGain,
    nodeHitsGain,
    ok: nonRegression.ok
      && nodeHitsGain > 0
      && finiteVisuals
      && boundedDebt,
    visualDebt,
    visualDebtPerCanonicalGain,
  };
}

export function shouldSkipEdgeNodePolishVariantAfterVisualBlowup(
  skipRemaining: boolean,
  variant: string,
  canonicalObstacleReliefEnabled: boolean,
): boolean {
  if (!skipRemaining) {
    return false;
  }
  return !(canonicalObstacleReliefEnabled && variant === "holistic");
}
