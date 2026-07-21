import assert from "node:assert/strict";
import fs from "node:fs/promises";
import path from "node:path";
import test from "node:test";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const __dirname = path.dirname(fileURLToPath(import.meta.url));
const sourceModulePath = path.resolve(
  __dirname,
  "../../out/webview/interaction/runtime/browserEventSource.js",
);

test("enabling optimized layout does not revive the retired bend preference", () => {
  const { getBrowserEventSource } = require(sourceModulePath);
  const source = getBrowserEventSource();

  const optimizedHandlerStart = source.indexOf(
    'for (const button of document.querySelectorAll("[data-optimized-toggle]"))',
  );
  const optimizedHandler = source.slice(
    optimizedHandlerStart,
    source.indexOf('root.addEventListener("click"', optimizedHandlerStart),
  );
  assert.doesNotMatch(
    optimizedHandler,
    /state\.useEdgeBends\s*=/,
  );
  assert.match(
    optimizedHandler,
    /viewState: createRefreshViewStateSnapshot\(state\)/,
  );
});

test("optimized refresh locks duplicate controls and always has an error settlement path", async () => {
  const { getBrowserEventSource } = require(sourceModulePath);
  const browserSource = getBrowserEventSource();
  const [panelSource, commandSource] = await Promise.all([
    fs.readFile(
      path.resolve(__dirname, "../../src/extension/panels/erdPanel.ts"),
      "utf8",
    ),
    fs.readFile(
      path.resolve(__dirname, "../../src/extension/commands/openDiagram.ts"),
      "utf8",
    ),
  ]);

  assert.match(browserSource, /let layoutRefreshPending = false/);
  assert.match(browserSource, /button\.disabled = layoutRefreshPending/);
  assert.match(browserSource, /ML Analyzing/);
  assert.match(browserSource, /msg\.type === "diagram\.refresh\.settled"/);
  assert.match(panelSource, /type: "diagram\.refresh\.settled"/);
  assert.match(panelSource, /Diagram refresh failed and UI state was released/);
  assert.match(commandSource, /Optimizing Django ERD \([^`]+s budget\)/);
});

test("every renderer path is straight-only and bend-producing controls are absent", async () => {
  const [
    canvasScene,
    svgScene,
    canvasRuntime,
    layoutRuntime,
    renderModelSource,
    nativeRuntime,
  ] = await Promise.all([
    fs.readFile(
      path.resolve(__dirname, "../../src/webview/render/renderCanvasScene.ts"),
      "utf8",
    ),
    fs.readFile(
      path.resolve(__dirname, "../../src/webview/render/renderSvgScene.ts"),
      "utf8",
    ),
    fs.readFile(
      path.resolve(
        __dirname,
        "../../src/webview/interaction/runtime/browserCanvasDrawSource.ts",
      ),
      "utf8",
    ),
    fs.readFile(
      path.resolve(
        __dirname,
        "../../src/webview/interaction/runtime/browserLayoutSource.ts",
      ),
      "utf8",
    ),
    fs.readFile(
      path.resolve(
        __dirname,
        "../../src/webview/state/createDiagramRenderModel.ts",
      ),
      "utf8",
    ),
    fs.readFile(
      path.resolve(
        __dirname,
        "../../src/extension/services/layout/runOgdfLayout.ts",
      ),
      "utf8",
    ),
  ]);

  assert.doesNotMatch(canvasScene, /data-edge-bends-toggle/);
  assert.doesNotMatch(svgScene, /data-edge-bends-toggle/);
  assert.doesNotMatch(canvasScene, /data-edge-bundle-toggle/);
  assert.doesNotMatch(svgScene, /data-edge-bundle-toggle/);
  assert.match(canvasRuntime, /function createStraightEdgePath\(/);
  assert.match(canvasRuntime, /normalized\.length > 2/);
  assert.doesNotMatch(canvasRuntime, /routeEdgePathAroundTables/);
  assert.doesNotMatch(canvasRuntime, /createTableDetourCandidates/);
  assert.doesNotMatch(layoutRuntime, /function buildBundledPath\(/);
  assert.doesNotMatch(layoutRuntime, /function buildOrthogonalPath/);
  assert.match(layoutRuntime, /points: normalizePoints\(\[start, end\]\)/);
  assert.match(renderModelSource, /\.map\(enforceStraightRenderedEdge\)/);
  assert.match(nativeRuntime, /Straight-only is a hard output contract/);
  assert.match(
    nativeRuntime,
    /scorer accepted no moves; skipping redundant reroute/,
  );
  assert.match(
    nativeRuntime,
    /DJERD_RENDERED_NODE_CLEARANCE_FINAL_BATCHES/,
  );
  assert.match(
    nativeRuntime,
    /DJERD_SEMANTIC_CARRIER_TARGET_SHORT_CIRCUIT/,
  );
  assert.match(nativeRuntime, /&& !semanticCarrierTargetSatisfied/);
  for (const key of [
    "DJERD_CANONICAL_ROUTE_REPAIR",
    "DJERD_EDGE_DETOUR",
    "DJERD_EDGE_DETOUR_FINAL",
    "DJERD_L_BEND_REROUTE",
    "DJERD_PERIPHERY_REROUTE",
    "DJERD_XINGS_DETOUR",
    "DJERD_XINGS_DETOUR_FINAL",
  ]) {
    assert.match(nativeRuntime, new RegExp(`${key}: "0"`));
  }
});
