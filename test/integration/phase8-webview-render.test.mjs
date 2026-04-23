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
const packageManifest = require(path.resolve(__dirname, "../../package.json"));
const { renderDiagramDocument } = require(renderModulePath);
const { loadPhaseOneSample } = require(sampleModulePath);

test("phase8 document renders canvas scene metadata, routed edges, crossings, choices, properties, and methods", () => {
  const html = render();
  const renderModel = readRenderModel(html);

  assert.match(html, /<canvas[\s\S]*data-erd-drawing-canvas/);
  assert.match(html, /data-erd-gpu-warning/);
  assert.match(html, /data-erd-gpu-warning-message/);
  assert.match(html, /data-erd-minimap/);
  assert.match(html, /data-erd-minimap-canvas/);
  assert.match(html, /data-erd-minimap-viewport/);
  assert.match(html, /aria-label="Django ERD diagram"/);
  assert.match(html, /id="erd-render-model"/);
  assert.equal(renderModel.appVersion, packageManifest.version);
  assert.equal(renderModel.tables.find((table) => table.modelId === "blog.Post")?.databaseTableName, "blog_post");
  assert.ok(renderModel.edges.some((edge) => edge.edgeId === "edge-post-tags"));
  assert.ok(renderModel.crossings.some((crossing) => crossing.id === "crossing-1"));
  assert.match(html, /Draft = draft/);
  assert.match(html, /@ display_title -&gt; str/);
  assert.match(html, /fn publish/);
  assert.match(html, /erd-relation-chip--high[\s\S]*accounts\.Author/);
  assert.match(html, /data-layout-mode="hierarchical"/);
  assert.match(html, /id="erd-initial-state"/);
  assert.doesNotMatch(html, /class="erd-table/);
  assert.doesNotMatch(html, /class="erd-edge/);
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

test("phase8 canvas scene keeps hidden table state in the JSON scene graph without DOM table nodes", () => {
  const html = render((payload) => {
    const taxonomy = payload.view.tableOptions.find(
      (options) => options.modelId === "taxonomy.Tag",
    );

    if (!taxonomy) {
      throw new Error("taxonomy.Tag table options fixture is missing.");
    }

    taxonomy.hidden = true;
  });
  const renderModel = readRenderModel(html);

  assert.equal(renderModel.tables.find((table) => table.modelId === "taxonomy.Tag")?.hidden, true);
  assert.ok(renderModel.edges.some((edge) => edge.edgeId === "edge-post-author"));
  assert.doesNotMatch(html, /class="erd-table/);
});

test("phase8 catalog mode expands high-degree tables for relation ports", () => {
  const payload = createCatalogPayload();
  const html = renderDiagramDocument(payload);
  const renderModel = readRenderModel(html);
  const hubSize = readTableSize(renderModel, "catalog.Hub");
  const leafSize = readTableSize(renderModel, "catalog.Leaf1");

  assert.match(html, /Model catalog mode: model and DB table names only\./);
  assert.ok(hubSize.height > leafSize.height, "hub table should be taller than leaf tables");
  assert.ok(hubSize.width > leafSize.width, "hub table should be wider than leaf tables");
});

test("phase8 browser runtime renders a GPU scene with minimap and viewport-aware culling", () => {
  const html = render();

  assert.match(html, /function createErdLogTimestamp\(\)/);
  assert.match(html, /function logErd\(level, event, details\)/);
  assert.match(html, /type: "diagram\.log"/);
  assert.match(html, /webview\.bootstrap/);
  assert.match(html, /renderer\.selected/);
  assert.match(html, /scene\.graph\.built/);
  assert.match(html, /render\.frame/);
  assert.match(html, /render\.stats/);
  assert.match(html, /canvasWidth/);
  assert.match(html, /frameId/);
  assert.match(html, /sinceLastFrameMs/);
  assert.match(html, /avgFrameMs/);
  assert.match(html, /drawMs/);
  assert.match(html, /cullMs/);
  assert.match(html, /liveDragEdges/);
  assert.match(html, /liveDragSegments/);
  assert.match(html, /totalSegments/);
  assert.match(html, /renderer\.webgpu\.draw_validation_failed/);
  assert.match(html, /event\.drag\.start/);
  assert.match(html, /event\.refresh\.request/);
  assert.match(html, /const gpuWarning = document\.querySelector\("\[data-erd-gpu-warning\]"\)/);
  assert.match(html, /function detectGpuSupport\(\)/);
  assert.match(html, /window\.WebGL2RenderingContext/);
  assert.match(html, /navigator\.gpu/);
  assert.match(html, /\(async \(\) =>/);
  assert.match(html, /await createGpuRenderer\(gpuSupport\)/);
  assert.match(html, /async function createGpuRenderer\(gpuSupport\)/);
  assert.match(html, /async function createWebGpuRenderer\(\)/);
  assert.match(html, /function createWebGpuTablePipeline\(device, format, commonBindGroupLayout\)/);
  assert.match(html, /function createWebGpuSegmentPipeline\(device, format, commonBindGroupLayout\)/);
  assert.match(html, /function createWebGpuSpritePipeline\(/);
  assert.match(html, /function drawWebGpuScene\(renderer, segments, overlays, tables, labels\)/);
  assert.match(html, /arrayStride: 48/);
  assert.match(html, /erdUniforms: ErdCommonUniforms/);
  assert.doesNotMatch(html, /var<uniform> common:/);
  assert.match(html, /renderer\.backend === "webgpu"/);
  assert.match(html, /function createLabelAtlas\(gl\)/);
  assert.match(html, /function collectVisibleTables\(scene, bounds\)/);
  assert.match(html, /function collectVisibleSegments\(scene, bounds\)/);
  assert.match(html, /function applyLiveDragTableRecord\(scene, records, bounds\)/);
  assert.match(html, /function applyLiveDragEdgeSegments\(records, bounds\)/);
  assert.match(html, /function collectLiveDragEdgeSegments\(activeDrag, bounds\)/);
  assert.match(html, /drag\.currentPosition =/);
  assert.match(html, /scheduleViewportRender\(\)/);
  assert.match(html, /function queryTableMetaNearWorldPoint\(point\)/);
  assert.match(html, /GPU_TABLE_DETAIL_ZOOM/);
  assert.match(html, /const minimap = document\.querySelector\("\[data-erd-minimap\]"\)/);
  assert.match(html, /const minimapCanvas = document\.querySelector\("\[data-erd-minimap-canvas\]"\)/);
  assert.match(html, /const minimapViewport = document\.querySelector\("\[data-erd-minimap-viewport\]"\)/);
  assert.match(html, /function getViewportScreenRect\(\)/);
  assert.match(html, /function renderMinimap\(renderMode\)/);
  assert.match(html, /function createMinimapMetrics\(bounds\)/);
  assert.match(html, /function updateMinimapViewportCursor\(metrics\)/);
  assert.match(html, /function getMinimapWorldPoint\(event\)/);
  assert.match(html, /function createViewportPanToWorldPointAction\(worldPoint\)/);
  assert.match(html, /minimap\.addEventListener\("pointerdown"/);
  assert.match(html, /ResizeObserver/);
});

function render(mutatePayload) {
  const payload = structuredClone(loadPhaseOneSample());
  mutatePayload?.(payload);
  return renderDiagramDocument(payload);
}

function readRenderModel(html) {
  const match = html.match(
    /<script id="erd-render-model" type="application\/json">([\s\S]*?)<\/script>/,
  );

  assert.ok(match?.[1], "render model JSON should be embedded in the document");
  return JSON.parse(match[1]);
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

function readTableSize(renderModel, modelId) {
  const table = renderModel.tables.find((entry) => entry.modelId === modelId);

  assert.ok(table, `missing table metadata for ${modelId}`);
  return {
    height: Number(table.size.height),
    width: Number(table.size.width),
  };
}
