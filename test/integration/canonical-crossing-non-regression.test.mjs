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
const { evaluateCanonicalCrossingNonRegression } = require(gateModulePath);

function metadata(visualCrossings, adjacentEdgeIntersections, nonIncidentNodeHits) {
  return {
    canonicalCrossing: {
      adjacentEdgeIntersections,
      nonIncidentNodeHits,
    },
    visualCrossings,
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

test("optimized cache schema is bumped so pre-gate layouts are not reused", async () => {
  const source = await fs.readFile(
    path.resolve(
      __dirname,
      "../../src/extension/services/layout/runOgdfLayout.ts",
    ),
    "utf8",
  );

  assert.match(source, /"optimized-layout-cache-v11"/);
  assert.doesNotMatch(source, /"optimized-layout-cache-v10"/);
});
