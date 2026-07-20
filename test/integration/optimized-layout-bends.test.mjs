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

test("enabling optimized layout preserves the user's edge-bend preference", () => {
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

test("bend controls cannot flatten verified routed polylines", async () => {
  const [canvasScene, svgScene, canvasRuntime] = await Promise.all([
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
  ]);

  assert.doesNotMatch(canvasScene, /data-edge-bends-toggle/);
  assert.doesNotMatch(svgScene, /data-edge-bends-toggle/);
  assert.doesNotMatch(canvasRuntime, /if \(!state\.useEdgeBends\)/);
});
