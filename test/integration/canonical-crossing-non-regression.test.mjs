import assert from "node:assert/strict";
import fs from "node:fs/promises";
import path from "node:path";
import test from "node:test";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const gateModulePath = path.resolve(
  __dirname,
  "../../out/extension/services/layout/canonicalCrossingNonRegression.js",
);
const {
  DEFAULT_CANONICAL_OBSTACLE_RELIEF_MAX_VISUAL_DEBT_PER_GAIN,
  DEFAULT_CANONICAL_ROUTE_REPAIR_MAX_ROUTE_BBOX_GROWTH,
  DEFAULT_EDGE_NODE_POLISH_HOLISTIC_RESERVE_MS,
  evaluateCanonicalCrossingNonRegression,
  evaluateCanonicalObstacleRelief,
  evaluateCanonicalRouteRepairCandidate,
  planEdgeNodePolishCandidateBudget,
  shouldSkipEdgeNodePolishVariantAfterVisualBlowup,
} = require(gateModulePath);

function metadata(visualCrossings, adjacentEdgeIntersections, nonIncidentNodeHits) {
  return {
    canonicalCrossing: {
      adjacentEdgeIntersections,
      nonIncidentNodeHits,
    },
    visualCrossings,
  };
}

function routeRepairSnapshot({
  adjacent = 8,
  edgeCross = 20,
  edgeIds = ["e1", "e2"],
  nodeHits = 10,
  nodeX = 0,
  pairs = 30,
  visual = 25,
} = {}) {
  return {
    crossings: [],
    engineMetadata: {
      bundleEdgeIntersections: 1,
      bundleNodeOverlaps: 0,
      canonicalCrossing: {
        adjacentEdgeIntersections: adjacent,
        boundViolation: false,
        certifierVersion: "test",
        collinearOverlaps: 0,
        completeRoutes: true,
        degenerateSegments: 0,
        domain: "canonical-simple-v1",
        edgeCount: 2,
        invariantViolations: 0,
        k3nCertificates: 0,
        k3nContribution: 0,
        kuratowskiCertificates: 0,
        kuratowskiContribution: 0,
        lowerBound: 1,
        method: "test",
        nodeCount: 3,
        nonIncidentNodeHits: nodeHits,
        nonProperContacts: adjacent + nodeHits,
        pointContacts: 0,
        properDrawing: false,
        routeCrossingPairs: pairs,
        routeCrossingPoints: pairs,
        selfIntersections: 0,
      },
      edgeCrossings: edgeCross,
      edgeNodeIntersections: 4,
      edgeSegmentOverlaps: 0,
      nodeOverlaps: 0,
      nodeSpacingOverlaps: 0,
      overlappingEdges: 0,
      rawRouteCrossings: pairs,
      visualCrossings: visual,
    },
    mode: "fmmm",
    nodes: [
      {
        modelId: "a",
        position: { x: nodeX, y: 0 },
        size: { height: 20, width: 20 },
      },
      {
        modelId: "b",
        position: { x: 100, y: 0 },
        size: { height: 20, width: 20 },
      },
      {
        modelId: "blocker",
        position: { x: 50, y: 0 },
        size: { height: 20, width: 20 },
      },
    ],
    routedEdges: edgeIds.map((edgeId, index) => ({
      crossingIds: [],
      edgeId,
      points: [
        { x: 10, y: index * 20 + 10 },
        { x: 90, y: index * 20 + 10 },
      ],
    })),
  };
}

test("better visual is rejected when canonical adjacent and node hits regress", () => {
  const base = metadata(1_258, 858, 1_050);
  const betterVisualButDirty = metadata(1_131, 3_356, 1_120);
  const result = evaluateCanonicalCrossingNonRegression(
    base,
    betterVisualButDirty,
  );

  assert.equal(result.adjacentOk, false);
  assert.equal(result.nodeHitsOk, false);
  assert.equal(result.ok, false);
});

test("each canonical diagnostic is an independent hard non-regression gate", () => {
  const base = metadata(1_258, 858, 1_050);

  assert.equal(
    evaluateCanonicalCrossingNonRegression(
      base,
      metadata(1_100, 859, 1_000),
    ).ok,
    false,
  );
  assert.equal(
    evaluateCanonicalCrossingNonRegression(
      base,
      metadata(1_100, 800, 1_051),
    ).ok,
    false,
  );
  assert.equal(
    evaluateCanonicalCrossingNonRegression(
      base,
      metadata(1_100, 858, 1_050),
    ).ok,
    true,
  );
  assert.equal(
    evaluateCanonicalCrossingNonRegression(
      base,
      metadata(1_100, 800, 1_000),
    ).ok,
    true,
  );
});

test("bounded canonical obstacle relief selects only the minimal holistic candidate", () => {
  const base = metadata(1_258, 972, 1_250);
  const cheap = evaluateCanonicalObstacleRelief(
    base,
    metadata(1_311, 941, 1_176),
    1_258,
    1_311,
  );
  const local = evaluateCanonicalObstacleRelief(
    base,
    metadata(1_289, 869, 956),
    1_258,
    1_289,
  );
  const holistic = evaluateCanonicalObstacleRelief(
    base,
    metadata(1_265, 858, 1_050),
    1_258,
    1_265,
  );

  assert.equal(
    DEFAULT_CANONICAL_OBSTACLE_RELIEF_MAX_VISUAL_DEBT_PER_GAIN,
    0.05,
  );
  assert.equal(cheap.ok, false);
  assert.ok(cheap.visualDebtPerCanonicalGain > 0.5);
  assert.equal(local.ok, false);
  assert.ok(local.visualDebtPerCanonicalGain > 0.07);
  assert.equal(holistic.ok, true);
  assert.equal(holistic.adjacentGain, 114);
  assert.equal(holistic.nodeHitsGain, 200);
  assert.equal(holistic.canonicalGain, 314);
  assert.ok(holistic.visualDebtPerCanonicalGain < 0.023);
});

test("edge-node visual improvement cannot bypass canonical non-regression", () => {
  const base = metadata(1_258, 972, 1_250);
  const betterVisualButWorseAdjacent = metadata(1_100, 973, 1_000);
  const gate = evaluateCanonicalCrossingNonRegression(
    base,
    betterVisualButWorseAdjacent,
  );
  const relief = evaluateCanonicalObstacleRelief(
    base,
    betterVisualButWorseAdjacent,
    1_258,
    1_100,
  );

  assert.equal(gate.adjacentOk, false);
  assert.equal(gate.nodeHitsOk, true);
  assert.equal(gate.ok, false);
  assert.equal(relief.ok, false);
});

test("visual blow-up still allows only holistic canonical obstacle relief", () => {
  assert.equal(
    shouldSkipEdgeNodePolishVariantAfterVisualBlowup(true, "cheap", true),
    true,
  );
  assert.equal(
    shouldSkipEdgeNodePolishVariantAfterVisualBlowup(true, "local", true),
    true,
  );
  assert.equal(
    shouldSkipEdgeNodePolishVariantAfterVisualBlowup(true, "holistic", true),
    false,
  );
  assert.equal(
    shouldSkipEdgeNodePolishVariantAfterVisualBlowup(true, "holistic", false),
    true,
  );
  assert.equal(
    shouldSkipEdgeNodePolishVariantAfterVisualBlowup(false, "local", false),
    false,
  );
});

test("non-holistic polish reserves shared deadline time for pending holistic", () => {
  assert.equal(DEFAULT_EDGE_NODE_POLISH_HOLISTIC_RESERVE_MS, 30_000);

  const fresh = planEdgeNodePolishCandidateBudget(
    90_000,
    60_000,
    "cheap",
    ["local", "holistic"],
    true,
  );
  assert.deepEqual(fresh, {
    budgetLimited: false,
    holisticPending: true,
    reservedMs: 30_000,
    timeoutMs: 60_000,
  });

  const constrained = planEdgeNodePolishCandidateBudget(
    50_000,
    60_000,
    "local",
    ["holistic"],
    true,
  );
  assert.deepEqual(constrained, {
    budgetLimited: true,
    holisticPending: true,
    reservedMs: 30_000,
    timeoutMs: 20_000,
  });

  const reservedBoundary = planEdgeNodePolishCandidateBudget(
    30_000,
    60_000,
    "local",
    ["holistic"],
    true,
  );
  assert.equal(reservedBoundary.timeoutMs, 0);
  assert.equal(reservedBoundary.holisticPending, true);

  const holistic = planEdgeNodePolishCandidateBudget(
    30_000,
    60_000,
    "holistic",
    [],
    true,
  );
  assert.deepEqual(holistic, {
    budgetLimited: true,
    holisticPending: false,
    reservedMs: 0,
    timeoutMs: 30_000,
  });
});

test("candidate diagnostics fail closed once the base has canonical counts", () => {
  const base = metadata(1_258, 858, 1_050);

  assert.equal(
    evaluateCanonicalCrossingNonRegression(base, { visualCrossings: 1_100 }).ok,
    false,
  );
  assert.equal(
    evaluateCanonicalCrossingNonRegression(
      { visualCrossings: 1_258 },
      { visualCrossings: 1_100 },
    ).ok,
    true,
  );
});

test("route-repair-only gate accepts strict node-hit relief with no debt", () => {
  const base = routeRepairSnapshot();
  const candidate = routeRepairSnapshot({
    adjacent: 7,
    edgeCross: 19,
    nodeHits: 8,
    pairs: 29,
    visual: 23,
  });

  const result = evaluateCanonicalRouteRepairCandidate(base, candidate);
  assert.equal(result.ok, true, result.reasons.join(","));
  assert.equal(result.nodeHitsGain, 2);
  assert.deepEqual(result.reasons, []);
});

test("route-repair-only gate rejects visual debt, placement drift, and route loss", () => {
  const base = routeRepairSnapshot();
  const candidate = routeRepairSnapshot({
    edgeIds: ["e1"],
    nodeHits: 8,
    nodeX: 0.01,
    visual: 26,
  });

  const result = evaluateCanonicalRouteRepairCandidate(base, candidate);
  assert.equal(result.ok, false);
  assert.ok(result.reasons.includes("visual-visualCrossings-regressed-or-missing"));
  assert.ok(result.reasons.includes("node-placement-changed"));
  assert.ok(result.reasons.includes("routed-edge-set-changed-or-incomplete"));
});

test("route-repair-only gate caps route bounding-box growth", () => {
  const base = routeRepairSnapshot();
  const candidate = routeRepairSnapshot({ nodeHits: 8 });
  candidate.routedEdges[1].points[1].y = 200;

  const result = evaluateCanonicalRouteRepairCandidate(base, candidate);
  assert.equal(DEFAULT_CANONICAL_ROUTE_REPAIR_MAX_ROUTE_BBOX_GROWTH, 1.01);
  assert.equal(result.ok, false);
  assert.ok(result.routeBboxGrowth > 1.01);
  assert.ok(result.reasons.includes("route-bbox-growth-exceeded"));
});

test("route-repair-only gate requires strict node-hit gain and every canonical category", () => {
  const base = routeRepairSnapshot();
  const noGain = routeRepairSnapshot({ nodeHits: 10 });
  const adjacentDebt = routeRepairSnapshot({ adjacent: 9, nodeHits: 8 });

  assert.ok(
    evaluateCanonicalRouteRepairCandidate(base, noGain).reasons.includes(
      "canonical-node-hit-not-improved",
    ),
  );
  assert.ok(
    evaluateCanonicalRouteRepairCandidate(base, adjacentDebt).reasons.includes(
      "canonical-adjacentEdgeIntersections-regressed-or-missing",
    ),
  );
});

test("v16 orchestration targets at most 100 rendered crossings adaptively", async () => {
  const source = await fs.readFile(
    path.resolve(
      __dirname,
      "../../src/extension/services/layout/runOgdfLayout.ts",
    ),
    "utf8",
  );

  assert.match(source, /"optimized-layout-cache-v16"/);
  assert.doesNotMatch(source, /"optimized-layout-cache-v14"/);
  assert.doesNotMatch(source, /"optimized-layout-cache-v15"/);
  assert.match(source, /const DEFAULT_VISUAL_CROSS_TARGET = 100;/);
  assert.match(source, /DJERD_ADAPTIVE_CARRIER_TARGET_FINAL/);
  assert.match(source, /adaptiveCarrierTarget=/);
  assert.match(source, /const DEFAULT_RENDERED_CARRIER_THRESHOLD = "2";/);
  assert.match(
    source,
    /DJERD_INHERITANCE_CARRIER_FINAL:[\s\S]*\?\? "1"/,
  );
  assert.match(
    source,
    /DJERD_INTRA_CLUSTER_CARRIER_FINAL:[\s\S]*\?\? "1"/,
  );
  assert.match(
    source,
    /ogdfOptimizedCanonicalRouteRepairEnv\(\)[\s\S]*DJERD_CANONICAL_ROUTE_REPAIR/,
  );
  assert.match(
    source,
    /DJERD_RIGID_NODE_EDGE_RELIEF_FINAL:\s*"0"/,
  );
  assert.match(
    source,
    /ogdfOptimizedCanonicalRouteRepairEnv\(\)[\s\S]*DJERD_RESTORE_LAYOUT_TSV_BEFORE_RETOUCH:\s*"1"/,
  );
  assert.match(
    source,
    /writeRoutesTsvFromLayoutJson\([\s\S]*"--routes-tsv", repairRoutesPath/,
  );
  assert.match(
    source,
    /DJERD_OPTIMIZED_EDGE_NODE_POLISH_VARIANTS",\s*"cheap,local"/,
  );
  assert.match(
    source,
    /optimizedCanonicalRouteRepairNative=.*DJERD_OPTIMIZED_CANONICAL_ROUTE_REPAIR_NATIVE/,
  );
  assert.match(
    source,
    /optimizedCanonicalRouteRepairMaxRouteBboxGrowth=.*DJERD_OPTIMIZED_CANONICAL_ROUTE_REPAIR_MAX_ROUTE_BBOX_GROWTH/,
  );
  assert.match(
    source,
    /DJERD_OPTIMIZED_EDGE_NODE_POLISH_CANONICAL_ROUTE_REPAIR[\s\S]*\?\? "1"/,
  );
  assert.match(
    source,
    /optimizedEdgeNodePolishHolisticReserveMs=.*DJERD_OPTIMIZED_EDGE_NODE_POLISH_HOLISTIC_RESERVE_MS/,
  );
  assert.match(
    source,
    /bboxVisualTargetReached[\s\S]*configuredBboxTargetVariants\.filter\(\(variant\) => variant === "local"\)/,
  );
  assert.match(
    source,
    /DJERD_OPTIMIZED_BBOX_TARGET_HOLISTIC_AFTER_VISUAL_TARGET/,
  );
  assert.match(
    source,
    /bboxCanonicalNonRegression[\s\S]*evaluateCanonicalCrossingNonRegression[\s\S]*bboxCanonicalNonRegression\.ok/,
  );
});
