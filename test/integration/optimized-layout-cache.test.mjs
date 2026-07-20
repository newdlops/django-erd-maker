import assert from "node:assert/strict";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const cacheModulePath = path.resolve(
  __dirname,
  "../../out/extension/services/layout/optimizedLayoutCache.js",
);
const {
  acquireOptimizedLayoutFlight,
  compareOptimizedLayoutQuality,
  preserveBestOptimizedLayoutCache,
  selectPreferredOptimizedLayoutJson,
} = require(cacheModulePath);

test("optimized layout flights serialize identical keys but not distinct keys", async () => {
  const key = `same-${process.pid}-${Date.now()}`;
  const first = await acquireOptimizedLayoutFlight(key);
  let followerAcquired = false;
  const followerPromise = acquireOptimizedLayoutFlight(key).then((lease) => {
    followerAcquired = true;
    return lease;
  });

  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(followerAcquired, false);

  const independent = await acquireOptimizedLayoutFlight(`${key}-other`);
  assert.equal(independent.waited, false);
  independent.release();

  first.release();
  const follower = await followerPromise;
  assert.equal(follower.waited, true);
  follower.release();

  const afterRelease = await acquireOptimizedLayoutFlight(key);
  assert.equal(afterRelease.waited, false);
  afterRelease.release();
});

test("optimized layout flight wait times out without poisoning later requests", async () => {
  const key = `timeout-${process.pid}-${Date.now()}`;
  const producer = await acquireOptimizedLayoutFlight(key);

  await assert.rejects(
    acquireOptimizedLayoutFlight(key, undefined, 25),
    /in-flight wait timed out after 25ms/,
  );

  producer.release();
  const retry = await acquireOptimizedLayoutFlight(key, undefined, 25);
  assert.equal(retry.waited, false);
  retry.release();
});

test("optimized cache quality keeps the lower visual-crossing result", () => {
  const better = snapshot(446, { edgeNodeIntersections: 150 });
  const worse = snapshot(535, { edgeNodeIntersections: 99 });
  const selection = selectPreferredOptimizedLayoutJson(
    JSON.stringify(better),
    JSON.stringify(worse),
  );

  assert.equal(selection.source, "existing");
  assert.equal(selection.existingVisualCrossings, 446);
  assert.equal(selection.candidateVisualCrossings, 535);
  assert.equal(JSON.parse(selection.json).engineMetadata.visualCrossings, 446);
  assert.ok(compareOptimizedLayoutQuality(
    better.engineMetadata,
    worse.engineMetadata,
  ) < 0);
});

test("optimized cache prioritizes the complete edge set over carrier crossings", () => {
  const existing = snapshot(133, { rawRouteCrossings: 5_430 });
  const cleanerCarrierButWorseAllEdges = snapshot(64, {
    rawRouteCrossings: 6_738,
  });
  const rejected = selectPreferredOptimizedLayoutJson(
    JSON.stringify(existing),
    JSON.stringify(cleanerCarrierButWorseAllEdges),
  );
  assert.equal(rejected.source, "existing");
  assert.equal(rejected.preservationReason, "all-edge-non-regression");

  const betterAllEdges = snapshot(180, { rawRouteCrossings: 5_200 });
  const accepted = selectPreferredOptimizedLayoutJson(
    JSON.stringify(existing),
    JSON.stringify(betterAllEdges),
  );
  assert.equal(accepted.source, "candidate");
  assert.ok(compareOptimizedLayoutQuality(
    betterAllEdges.engineMetadata,
    existing.engineMetadata,
  ) < 0);
});

test("optimized cache quality uses conflict tie-breakers and keeps exact ties", () => {
  const cleanerTie = snapshot(446, { edgeNodeIntersections: 100 });
  const dirtierTie = snapshot(446, { edgeNodeIntersections: 120 });
  assert.ok(compareOptimizedLayoutQuality(
    cleanerTie.engineMetadata,
    dirtierTie.engineMetadata,
  ) < 0);

  const exactTie = selectPreferredOptimizedLayoutJson(
    JSON.stringify(cleanerTie),
    JSON.stringify(cleanerTie),
  );
  assert.equal(exactTie.source, "existing");
});

test("optimized cache never rewards legacy edge aggregation metadata", () => {
  const lowerHonestScore = snapshot(50, {
    // Old cache entries may still contain these unknown fields. They must not
    // override the complete-edge visual score.
    adaptiveCarrierGrid: 8,
    adaptiveCarrierTarget: 100,
    adaptiveCarrierVisibleEdges: 139,
  });
  const higherLegacyDetail = snapshot(90, {
    adaptiveCarrierGrid: 12,
    adaptiveCarrierTarget: 100,
    adaptiveCarrierVisibleEdges: 235,
  });
  assert.ok(compareOptimizedLayoutQuality(
    lowerHonestScore.engineMetadata,
    higherLegacyDetail.engineMetadata,
  ) < 0);
  assert.equal(
    selectPreferredOptimizedLayoutJson(
      JSON.stringify(higherLegacyDetail),
      JSON.stringify(lowerHonestScore),
    ).source,
    "candidate",
  );
});

test("optimized cache preserves canonical adjacent and node-hit non-regression", () => {
  const existing = snapshot(1_258, {
    canonicalCrossing: canonicalCrossing(858, 1_050),
  });
  const lowerVisualButDirty = snapshot(1_131, {
    canonicalCrossing: canonicalCrossing(3_356, 1_120),
  });
  const dirtySelection = selectPreferredOptimizedLayoutJson(
    JSON.stringify(existing),
    JSON.stringify(lowerVisualButDirty),
  );
  assert.equal(dirtySelection.source, "existing");
  assert.equal(
    dirtySelection.preservationReason,
    "canonical-non-regression",
  );

  const lowerVisualButWorseNodeHits = snapshot(1_100, {
    canonicalCrossing: canonicalCrossing(800, 1_051),
  });
  assert.equal(
    selectPreferredOptimizedLayoutJson(
      JSON.stringify(existing),
      JSON.stringify(lowerVisualButWorseNodeHits),
    ).source,
    "existing",
  );

  const lowerVisualAndCanonicalClean = snapshot(1_100, {
    canonicalCrossing: canonicalCrossing(800, 1_000),
  });
  assert.equal(
    selectPreferredOptimizedLayoutJson(
      JSON.stringify(existing),
      JSON.stringify(lowerVisualAndCanonicalClean),
    ).source,
    "candidate",
  );
});

test("optimized cache accepts only bounded canonical obstacle relief", () => {
  const existing = snapshot(1_258, {
    canonicalCrossing: canonicalCrossing(972, 1_250),
  });
  const holistic = snapshot(1_265, {
    canonicalCrossing: canonicalCrossing(858, 1_050),
  });
  const holisticSelection = selectPreferredOptimizedLayoutJson(
    JSON.stringify(existing),
    JSON.stringify(holistic),
  );
  assert.equal(holisticSelection.source, "candidate");
  assert.equal(
    holisticSelection.candidateReason,
    "canonical-obstacle-relief",
  );

  const local = snapshot(1_289, {
    canonicalCrossing: canonicalCrossing(869, 956),
  });
  const localSelection = selectPreferredOptimizedLayoutJson(
    JSON.stringify(existing),
    JSON.stringify(local),
  );
  assert.equal(localSelection.source, "existing");
  assert.equal(localSelection.preservationReason, "quality");
});

test("optimized cache write preserves a better cache and replaces a worse one", async () => {
  const directory = await fs.mkdtemp(
    path.join(os.tmpdir(), "django-erd-optimized-cache-test-"),
  );
  const cachePath = path.join(directory, "layout.json");
  try {
    await fs.writeFile(cachePath, JSON.stringify(snapshot(446)), "utf8");
    const preserved = await preserveBestOptimizedLayoutCache(
      cachePath,
      JSON.stringify(snapshot(535)),
    );
    assert.equal(preserved.source, "existing");
    assert.equal(
      JSON.parse(await fs.readFile(cachePath, "utf8")).engineMetadata.visualCrossings,
      446,
    );

    const replaced = await preserveBestOptimizedLayoutCache(
      cachePath,
      JSON.stringify(snapshot(400)),
    );
    assert.equal(replaced.source, "candidate");
    assert.equal(
      JSON.parse(await fs.readFile(cachePath, "utf8")).engineMetadata.visualCrossings,
      400,
    );
  } finally {
    await fs.rm(directory, { force: true, recursive: true });
  }
});

function snapshot(visualCrossings, metadata = {}) {
  return {
    crossings: [],
    engineMetadata: {
      edgeCrossings: visualCrossings,
      visualCrossings,
      ...metadata,
    },
    mode: "fmmm",
    nodes: [],
    routedEdges: [],
  };
}

function canonicalCrossing(adjacentEdgeIntersections, nonIncidentNodeHits) {
  return {
    adjacentEdgeIntersections,
    boundViolation: false,
    certifierVersion: "1",
    collinearOverlaps: 0,
    completeRoutes: true,
    degenerateSegments: 0,
    domain: "canonical-simple-v1",
    edgeCount: 1_682,
    invariantViolations: 0,
    k3nCertificates: 4,
    k3nContribution: 30,
    kuratowskiCertificates: 23,
    kuratowskiContribution: 23,
    lowerBound: 53,
    method: "degree2-k3n+deterministic-boyer-myrvold-packing",
    nodeCount: 1_218,
    nonIncidentNodeHits,
    nonProperContacts: adjacentEdgeIntersections + nonIncidentNodeHits,
    pointContacts: 0,
    properDrawing: false,
    routeCrossingPairs: 5_201,
    routeCrossingPoints: 5_282,
    selfIntersections: 0,
  };
}
