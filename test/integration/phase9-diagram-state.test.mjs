import assert from "node:assert/strict";
import path from "node:path";
import test from "node:test";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const stateModulePath = path.resolve(
  __dirname,
  "../../out/webview/state/diagramInteractionState.js",
);
const sampleModulePath = path.resolve(
  __dirname,
  "../../out/extension/services/loadPhaseOneSample.js",
);
const {
  cloneDiagramInteractionState,
  createDiagramInteractionState,
  getTableViewOptions,
  reduceDiagramInteractionState,
} = require(stateModulePath);
const { loadPhaseOneSample } = require(sampleModulePath);

test("phase9 selection and method context transitions are explicit", () => {
  const payload = loadPhaseOneSample();
  const initialState = createDiagramInteractionState(payload.view);
  const selectedAuthor = reduceDiagramInteractionState(initialState, {
    modelId: "accounts.Author",
    type: "select-model",
  });
  const activatedMethod = reduceDiagramInteractionState(selectedAuthor, {
    methodName: "featured_posts",
    modelId: "accounts.Author",
    type: "toggle-method",
  });
  const clearedMethod = reduceDiagramInteractionState(activatedMethod, {
    methodName: "featured_posts",
    modelId: "accounts.Author",
    type: "toggle-method",
  });

  assert.equal(selectedAuthor.selectedModelId, "accounts.Author");
  assert.equal(selectedAuthor.selectedMethodContext, undefined);
  assert.deepEqual(activatedMethod.selectedMethodContext, {
    methodName: "featured_posts",
    modelId: "accounts.Author",
  });
  assert.equal(clearedMethod.selectedMethodContext, undefined);
});

test("phase9 table toggles and manual position overrides stay in table state", () => {
  const payload = loadPhaseOneSample();
  const initialState = createDiagramInteractionState(payload.view);
  const movedState = reduceDiagramInteractionState(initialState, {
    manualPosition: { x: 512, y: 288 },
    modelId: "blog.Post",
    type: "set-table-manual-position",
  });
  const hiddenState = reduceDiagramInteractionState(movedState, {
    hidden: true,
    modelId: "taxonomy.Tag",
    type: "set-table-hidden",
  });
  const propertyState = reduceDiagramInteractionState(hiddenState, {
    modelId: "blog.Post",
    showProperties: false,
    type: "set-table-show-properties",
  });

  assert.deepEqual(getTableViewOptions(movedState, "blog.Post").manualPosition, {
    x: 512,
    y: 288,
  });
  assert.equal(getTableViewOptions(hiddenState, "taxonomy.Tag").hidden, true);
  assert.equal(getTableViewOptions(propertyState, "blog.Post").showProperties, false);
});

test("phase9 disabling method visibility clears selected method context and reset restores initial state", () => {
  const payload = loadPhaseOneSample();
  const initialState = createDiagramInteractionState(payload.view);
  const hiddenMethods = reduceDiagramInteractionState(initialState, {
    modelId: "blog.Post",
    showMethods: false,
    type: "set-table-show-methods",
  });
  const changedLayout = reduceDiagramInteractionState(hiddenMethods, {
    layoutMode: "circular",
    type: "set-layout-mode",
  });
  const resetState = reduceDiagramInteractionState(changedLayout, {
    initialState: cloneDiagramInteractionState(initialState),
    type: "reset-view",
  });

  assert.equal(hiddenMethods.selectedMethodContext, undefined);
  assert.equal(changedLayout.layoutMode, "circular");
  assert.deepEqual(resetState, initialState);
});
