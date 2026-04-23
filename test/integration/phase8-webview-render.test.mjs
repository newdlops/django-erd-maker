import assert from "node:assert/strict";
import path from "node:path";
import test from "node:test";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const renderModulePath = path.resolve(
  __dirname,
  "../../out/webview/app/renderDiagramDocument.js",
);
const sampleModulePath = path.resolve(
  __dirname,
  "../../out/extension/services/loadPhaseOneSample.js",
);
const { renderDiagramDocument } = require(renderModulePath);
const { loadPhaseOneSample } = require(sampleModulePath);

test("phase8 document renders canvas scene metadata, routed edges, crossings, choices, properties, and methods", () => {
  const html = render();

  assert.match(html, /<canvas[\s\S]*data-erd-drawing-canvas/);
  assert.match(html, /data-erd-minimap/);
  assert.match(html, /data-erd-minimap-canvas/);
  assert.match(html, /data-erd-minimap-viewport/);
  assert.match(html, /aria-label="Django ERD diagram"/);
  assert.match(html, /id="erd-render-model"/);
  assert.match(html, /"baseLayoutMode":"hierarchical"/);
  assert.match(html, /data-table-name="blog_post"/);
  assert.match(html, /data-edge-id="edge-post-tags"/);
  assert.match(html, /data-crossing-id="crossing-1"/);
  assert.match(html, /Draft = draft/);
  assert.match(html, /@ display_title -&gt; str/);
  assert.match(html, /fn publish/);
  assert.match(html, /erd-relation-chip--high[\s\S]*accounts\.Author/);
  assert.match(html, /data-layout-mode="hierarchical"/);
  assert.match(html, /data-layout-mode="flow"/);
  assert.match(html, /data-layout-mode="graph"/);
  assert.match(html, /data-layout-mode="neural"/);
  assert.match(html, /data-layout-mode="radial"/);
  assert.match(html, /id="erd-initial-state"/);
});

test("phase8 document respects method and property visibility state in the inspector", () => {
  const taxonomyHtml = render((payload) => {
    payload.view.selectedModelId = "taxonomy.Tag";
    payload.view.selectedMethodContext = undefined;
  });
  const auditHtml = render((payload) => {
    payload.view.selectedModelId = "audit.AuditLog";
    payload.view.selectedMethodContext = undefined;
  });

  assert.match(taxonomyHtml, /Methods are hidden by the current table view state\./);
  assert.match(auditHtml, /Properties are hidden by the current table view state\./);
});

test("phase8 setup explains that layout settings apply after refresh", () => {
  const html = render();

  assert.match(html, /data-setup-control="nodeSpacing"/);
  assert.match(html, /data-setup-control="edgeDetour"/);
  assert.match(html, /Layout and routing settings are applied on Refresh/);
  assert.match(html, /Refresh To Apply/);
});

test("phase8 browser controller declares layout settings before initial viewport calculation", () => {
  const html = render();
  const wasmRuntimeIndex = html.indexOf("const layoutWasmRuntime = createLayoutWasmRuntime");
  const stateDeclarationIndex = html.indexOf("let state = null");
  const appliedSettingsIndex = html.indexOf("let appliedLayoutSettings = pickLayoutRoutingSettings");
  const viewportCalculationIndex = html.indexOf("computeInitialViewport(initialStateValue)");

  assert.ok(wasmRuntimeIndex >= 0, "controller should initialize layout WASM before viewport calculation");
  assert.ok(stateDeclarationIndex >= 0, "controller should declare state before initialization");
  assert.ok(appliedSettingsIndex >= 0, "controller should declare applied layout settings before initialization");
  assert.ok(viewportCalculationIndex >= 0, "controller should compute the initial viewport");
  assert.ok(
    wasmRuntimeIndex < viewportCalculationIndex &&
      stateDeclarationIndex < viewportCalculationIndex &&
      appliedSettingsIndex < viewportCalculationIndex,
    "initial viewport calculation should not run while layout setting state is in the temporal dead zone",
  );
});

test("phase8 browser controller caches layout variants lazily per active mode", () => {
  const html = render();

  assert.match(html, /let layoutVariantCache = new Map\(\)/);
  assert.match(html, /function getLayoutVariant\(layoutMode, settingsOverride\)/);
  assert.match(
    html,
    /return getLayoutVariant\(\s*layoutMode,\s*settingsOverride \|\| getAppliedLayoutSettings\(\),\s*\)/,
  );
});

test("phase8 browser runtime uses drag preview rendering instead of full reroute on pointer move", () => {
  const html = render();

  assert.match(html, /let dragPreviewFrame = 0/);
  assert.match(html, /let pendingDragPreviewRects = \[\]/);
  assert.match(html, /function scheduleDragPreviewRender\(\)/);
  assert.match(html, /function applyManualPositionState\(modelId\)/);
  assert.match(html, /function applySelectionState\(previousState, action\)/);
  assert.match(html, /function redrawCanvasRegions\(screenRects, options\)/);
  assert.match(html, /function queueDragPreviewRects\(previousState, modelId\)/);
  assert.match(
    html,
    /action\.type === "set-table-manual-position"[\s\S]*drag &&[\s\S]*drag\.kind === "table"[\s\S]*queueDragPreviewRects\(previousState, action\.modelId\)[\s\S]*scheduleDragPreviewRender\(\)/,
  );
  assert.match(html, /applyManualPositionState\(action\.modelId\)/);
  assert.match(html, /applyManualPositionState\(completedDrag\.modelId\)/);
  assert.match(html, /function rerouteModelEdges\(modelId\)/);
  assert.match(html, /function filterNearbyCrossings\(crossings\)/);
  assert.match(
    html,
    /pendingDragPreviewRects\.length > 0[\s\S]*redrawCanvasRegions\(pendingDragPreviewRects,\s*\{[\s\S]*skipCrossings: true,[\s\S]*skipMethodOverlays: true,[\s\S]*\}\)/,
  );
});

test("phase8 browser runtime caches relation state and buckets spacing checks", () => {
  const html = render();

  assert.match(html, /let relationLayoutStateCache = null/);
  assert.match(html, /let baseLayoutCache = null/);
  assert.match(html, /function getRelationLayoutState\(tableMetaList\)/);
  assert.match(html, /function createRelaxationPairIndexes\(/);
  assert.match(html, /function createAdaptiveFinalizeOptions\(tableMetaList, options\)/);
  assert.match(html, /function createHybridRelationGraphLayout\(tableMetaList, config\)/);
  assert.match(html, /function createHybridGraphComponentPlan\(componentIds, relationState, config, componentIndex\)/);
  assert.match(html, /function createAdaptiveSweepIterations\(iterations, tableCount\)/);
});

test("phase8 hierarchical layout uses analyzer seed first and keeps layered barycenter fallback", () => {
  const html = render();

  assert.match(html, /function shouldUseAnalyzerHierarchicalSeed\(tableMetaList\)/);
  assert.match(html, /function createAnalyzerSeedLayout\(tableMetaList\)/);
  assert.match(html, /renderModel\.baseLayoutMode === "hierarchical"/);
  assert.match(
    html,
    /case "hierarchical":[\s\S]*shouldUseAnalyzerHierarchicalSeed\(tableMetaList\)[\s\S]*createAnalyzerSeedLayout\(tableMetaList\)/,
  );
  assert.match(
    html,
    /function createHierarchicalLayout\(tableMetaList, tuning = createLayoutTuning\(\)\)[\s\S]*hasRelationEdgesInLayout\(relationState\)[\s\S]*createLayeredRelationLayout\(/,
  );
  assert.match(html, /function reduceLayerCrossings\(layers, layerById, relationState, sweepIterations\)/);
  assert.match(html, /function sweepLayerIdsByNeighborBarycenter\(/);
  assert.match(html, /function spaceVisibleEdgeBundleDescriptors\(routes\)/);
  assert.match(html, /function buildOrthogonalPathFromPorts\(/);
});

test("phase8 browser runtime prefers the server layout for the active refreshed mode", () => {
  const html = render();

  assert.match(html, /function shouldUseServerLayoutVariant\(layoutMode, tableMetaList\)/);
  assert.match(html, /function createServerLayoutVariant\(tableMetaList\)/);
  assert.match(html, /function getServerLayoutVariant\(layoutMode, tableMetaList\)/);
  assert.match(
    html,
    /const serverLayout = getServerLayoutVariant\(layoutMode, tableMetaList\);[\s\S]*if \(serverLayout\) \{\s*return serverLayout;\s*\}/,
  );
  assert.match(
    html,
    /const activeServerLayout = getServerLayoutVariant\(layoutMode, tableMetaList\);[\s\S]*const fallbackServerLayout = getServerLayoutVariant\(/,
  );
});

test("phase8 browser runtime embeds WASM layout optimizer with JS fallback", () => {
  const html = render();

  assert.match(html, /script-src 'nonce-[^']+' 'wasm-unsafe-eval'/);
  assert.match(html, /const layoutWasmBase64 = "[A-Za-z0-9+/=]+"/);
  assert.match(html, /function createLayoutWasmRuntime\(base64\)/);
  assert.match(html, /new WebAssembly\.Module\(bytes\)/);
  assert.match(html, /function tryOptimizeLayoutWithWasm\(tableMetaList, positions, settingsOverride\)/);
  assert.match(html, /function createLayoutWasmOptimizerSettings\(settingsOverride\)/);
  assert.match(html, /writer\.writeF64\(settings\.nodeSpacing\)/);
  assert.match(html, /writer\.writeF64\(settings\.edgeDetour\)/);
  assert.match(html, /function tryComputeLayoutBoundsWithWasm\(tables, optionsByModelId, layout\)/);
  assert.match(
    html,
    /function finalizeLayoutVariant\(tableMetaList, positions, options\)[\s\S]*tryOptimizeLayoutWithWasm\([\s\S]*tableMetaList,[\s\S]*positions,[\s\S]*finalizeOptions\.tuning/,
  );
  assert.match(
    html,
    /function computeLayoutBounds\(layoutMode, tableOptions, settingsOverride\)[\s\S]*tryComputeLayoutBoundsWithWasm\(tableMetaById\.values\(\), optionsByModelId, layout\)/,
  );
});

test("phase8 viewport fit and center use visible component bounds", () => {
  const html = render();

  assert.match(
    html,
    /function computeViewportForLayout\(layoutMode, tableOptions, options\)[\s\S]*const bounds = computeLayoutBounds\(layoutMode, tableOptions, options\?\.settings\)/,
  );
  assert.match(
    html,
    /function computeCenteredViewportForLayout\(layoutMode, tableOptions, zoom, settingsOverride\)[\s\S]*const bounds = computeLayoutBounds\(layoutMode, tableOptions, settingsOverride\)/,
  );
  assert.match(html, /function createViewportPanToWorldPointAction\(worldPoint\)/);
  assert.doesNotMatch(html, /function expandLayoutBoundsWithRoutedEdges\(/);
});

test("phase8 minimap renders component positions and pans by cursor drag", () => {
  const html = render();

  assert.match(html, /const minimap = document\.querySelector\("\[data-erd-minimap\]"\)/);
  assert.match(html, /scene: createSceneSnapshot\(\)/);
  assert.match(html, /canvasInkSample: sampleCanvasInk\(\)/);
  assert.match(html, /layoutButtons: layoutButtons\.map/);
  assert.match(html, /function renderMinimap\(renderMode\)/);
  assert.match(html, /renderMode === "viewport" && cachedMinimapMetrics/);
  assert.match(html, /let metrics = renderMode === "full" \? null : cachedMinimapMetrics/);
  assert.match(html, /function createMinimapMetrics\(bounds\)/);
  assert.match(html, /function updateMinimapViewportCursor\(metrics\)/);
  assert.match(html, /function getViewportWorldRect\(\)/);
  assert.match(html, /function fitMinimapCursorRect\(rect, metrics\)/);
  assert.match(html, /function getViewportScreenRect\(\)/);
  assert.match(html, /drawingRect && drawingRect\.width > 1 && drawingRect\.height > 1/);
  assert.match(html, /function getMinimapWorldPoint\(event\)/);
  assert.match(html, /minimap\.addEventListener\("pointerdown"/);
  assert.match(html, /dispatch\(createViewportPanToWorldPointAction\(worldPoint\)\)/);
  assert.match(html, /case "clickZoomAction":/);
  assert.match(html, /case "pointerPanBy":/);
  assert.match(html, /case "setSetupControl":/);
});

test("phase8 canvas scene keeps hidden table metadata in the DOM with hidden state", () => {
  const html = render((payload) => {
    const taxonomy = payload.view.tableOptions.find(
      (options) => options.modelId === "taxonomy.Tag",
    );

    if (!taxonomy) {
      throw new Error("taxonomy.Tag table options fixture is missing.");
    }

    taxonomy.hidden = true;
  });

  assert.match(
    html,
    /<div[\s\S]*class="erd-table[^"]*"[\s\S]*data-model-id="taxonomy\.Tag"[\s\S]*hidden/,
  );
  assert.match(html, /data-edge-id="edge-post-author"/);
});

test("phase8 catalog mode expands high-degree tables for relation ports", () => {
  const payload = createCatalogPayload();
  const html = renderDiagramDocument(payload);
  const hubSize = readTableSize(html, "catalog.Hub");
  const leafSize = readTableSize(html, "catalog.Leaf1");

  assert.match(html, /Model catalog mode: model and DB table names only\./);
  assert.ok(hubSize.height > leafSize.height, "hub table should be taller than leaf tables");
  assert.ok(hubSize.width > leafSize.width, "hub table should be wider than leaf tables");
});

function render(mutatePayload) {
  const payload = structuredClone(loadPhaseOneSample());
  mutatePayload?.(payload);
  return renderDiagramDocument(payload);
}

function createCatalogPayload() {
  const payload = structuredClone(loadPhaseOneSample());
  const modelCount = 501;
  const models = Array.from({ length: modelCount }, (_, index) => {
    const modelName = index === 0 ? "Hub" : `Leaf${index}`;

    return {
      declaredBaseClasses: ["models.Model"],
      databaseTableName: `catalog_${modelName.toLowerCase()}`,
      fields: [],
      hasExplicitDatabaseTableName: true,
      identity: {
        appLabel: "catalog",
        id: `catalog.${modelName}`,
        modelName,
        modulePath: "catalog/models.py",
      },
      methods: [],
      properties: [],
    };
  });
  const layoutNodes = models.map((model, index) => ({
    modelId: model.identity.id,
    position: {
      x: 24 + (index % 24) * 260,
      y: 24 + Math.floor(index / 24) * 104,
    },
    size: {
      height: 74,
      width: 236,
    },
  }));
  const structuralEdges = Array.from({ length: 80 }, (_, index) => ({
    id: `edge-leaf-${index + 1}-hub`,
    kind: "foreign_key",
    provenance: "declared",
    sourceModelId: `catalog.Leaf${index + 1}`,
    targetModelId: "catalog.Hub",
  }));

  payload.analyzer.models = models;
  payload.analyzer.summary.discoveredModelCount = models.length;
  payload.graph.methodAssociations = [];
  payload.graph.nodes = models.map((model) => ({
    appLabel: model.identity.appLabel,
    modelId: model.identity.id,
    modelName: model.identity.modelName,
  }));
  payload.graph.structuralEdges = structuralEdges;
  payload.layout.crossings = [];
  payload.layout.nodes = layoutNodes;
  payload.layout.routedEdges = [];
  payload.view.selectedMethodContext = undefined;
  payload.view.selectedModelId = "catalog.Hub";
  payload.view.tableOptions = models.map((model) => ({
    hidden: false,
    modelId: model.identity.id,
    showMethodHighlights: false,
    showMethods: false,
    showProperties: false,
  }));

  return payload;
}

function readTableSize(html, modelId) {
  const tableMarkup = Array.from(
    html.matchAll(/<div\s+class="erd-table[^"]*"[\s\S]*?>/g),
  )
    .map((match) => match[0])
    .find((markup) => markup.includes(`data-model-id="${modelId}"`));
  const match = tableMarkup?.match(
    /data-width="(?<width>\d+)"[\s\S]*?data-height="(?<height>\d+)"/,
  );

  assert.ok(match?.groups, `missing table metadata for ${modelId}`);
  return {
    height: Number(match.groups.height),
    width: Number(match.groups.width),
  };
}
