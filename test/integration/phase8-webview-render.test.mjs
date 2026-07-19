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
const layoutContractModulePath = path.resolve(
  __dirname,
  "../../out/shared/graph/layoutContract.js",
);
const protocolDecoderModulePath = path.resolve(
  __dirname,
  "../../out/shared/protocol/decodeDiagramBootstrap.js",
);
const renderModelModulePath = path.resolve(
  __dirname,
  "../../out/webview/state/createDiagramRenderModel.js",
);
const sampleModulePath = path.resolve(
  __dirname,
  "../../out/extension/services/loadPhaseOneSample.js",
);
const packageManifest = require(path.resolve(__dirname, "../../package.json"));
const { OGDF_LAYOUT_TOOLBAR_DEFINITIONS } = require(layoutContractModulePath);
const { decodeLayoutSnapshot } = require(protocolDecoderModulePath);
const { createDiagramRenderModel } = require(renderModelModulePath);
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
  assert.match(html, /<template id="erd-render-model">/);
  assert.equal(renderModel.appVersion, packageManifest.version);
  assert.equal(renderModel.tables.find((table) => table.modelId === "blog.Post")?.databaseTableName, "blog_post");
  assert.ok(renderModel.edges.some((edge) => edge.edgeId === "edge-post-tags"));
  assert.ok(renderModel.crossings.some((crossing) => crossing.id === "crossing-1"));
  assert.match(html, /Draft = draft/);
  assert.match(html, /@ display_title -&gt; str/);
  assert.match(html, /fn publish/);
  assert.match(html, /erd-relation-chip--high[\s\S]*accounts\.Author/);
  for (const layout of OGDF_LAYOUT_TOOLBAR_DEFINITIONS) {
    assert.match(html, new RegExp(`data-layout-mode="${layout.id}"`));
  }
  assert.match(html, /<template id="erd-initial-state">/);
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

test("phase8 document surfaces layout fallback state and disables failed layout buttons", () => {
  const html = render((payload) => {
    payload.layout.mode = "clustered";
    payload.view.layoutMode = "fmmm";
    payload.layoutExecution = {
      appliedMode: "clustered",
      engine: "analyzer",
      reason: "native layout timed out after 20000ms",
      requestedMode: "planarization",
      status: "fallback",
    };
    payload.layoutFailures = {
      planarization: "native layout timed out after 20000ms",
    };
  });

  assert.match(html, /Requested Planarization Layout/);
  assert.match(html, /Applied FMMM Layout \(FM3\)/);
  assert.match(html, /Fallback active/);
  assert.match(html, /Layout Failures/);
  assert.match(html, /Planarization Layout: native layout timed out after 20000ms/);
  assert.match(html, /data-layout-mode="planarization"[\s\S]*disabled/);
});

test("phase8 document surfaces an applied layout that missed its quality target", () => {
  const html = render((payload) => {
    payload.layoutExecution = {
      appliedMode: "fast_multipole",
      engine: "ogdf",
      reason: "Optimized visual crossings 2174 exceed target 500 by 1674.",
      requestedMode: "fast_multipole",
      status: "quality-degraded",
    };
  });

  assert.match(html, /Optimized layout applied/);
  assert.match(html, /Quality target missed/);
  assert.match(html, /Optimized visual crossings 2174 exceed target 500 by 1674/);
  assert.match(html, /layout_quality_degraded/);
  assert.doesNotMatch(html, /Fallback active/);
});

test("phase8 decoder and inspector keep canonical crossing certification separate from visual conflicts", () => {
  const payload = structuredClone(loadPhaseOneSample());
  payload.layout.engineMetadata = {
    ...(payload.layout.engineMetadata ?? {}),
    canonicalCrossing: canonicalCrossingFixture(),
    visualCrossings: 446,
  };

  const decodedLayout = decodeLayoutSnapshot(payload.layout, "phase8CanonicalLayout");
  assert.deepEqual(
    decodedLayout.engineMetadata?.canonicalCrossing,
    canonicalCrossingFixture(),
  );
  payload.layout = decodedLayout;

  const viewModel = createDiagramRenderModel(payload);
  const html = renderDiagramDocument(payload);
  assert.equal(viewModel.visualCrossings, 446);
  assert.deepEqual(viewModel.canonicalCrossing, {
    adjacentEdgeIntersections: 0,
    boundViolation: false,
    collinearOverlaps: 0,
    completeRoutes: true,
    degenerateSegments: 0,
    gap: 38,
    invariantViolations: 0,
    lowerBound: 53,
    nonIncidentNodeHits: 0,
    nonProperContacts: 0,
    optimality: 53 / 91,
    pointContacts: 0,
    properDrawing: true,
    routeCrossingPairs: 91,
    selfIntersections: 0,
  });
  assert.match(html, /Visual conflicts: 446/);
  assert.match(
    html,
    /Canonical crossings: pairs 91 · certified lower bound ≥ 53 · gap 38/,
  );

  const improperPayload = structuredClone(payload);
  const improperCanonical = canonicalCrossingFixture({
    adjacentEdgeIntersections: 7,
    nonIncidentNodeHits: 3,
    nonProperContacts: 10,
    properDrawing: false,
  });
  delete improperCanonical.gap;
  delete improperCanonical.optimality;
  improperPayload.layout.engineMetadata.canonicalCrossing = improperCanonical;
  improperPayload.layout = decodeLayoutSnapshot(
    improperPayload.layout,
    "phase8ImproperCanonicalLayout",
  );
  const improperHtml = renderDiagramDocument(improperPayload);
  assert.match(
    improperHtml,
    /non-proper diagnostics 10 · node hits 3 · adjacent 7/,
  );

  const malformedLayout = structuredClone(payload.layout);
  malformedLayout.engineMetadata.canonicalCrossing.routeCrossingPairs = "91";
  assert.throws(
    () => decodeLayoutSnapshot(malformedLayout, "phase8MalformedLayout"),
    /canonicalCrossing\.routeCrossingPairs must be a number/,
  );
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

test("phase8 document embeds adaptive detail edges for zoom LOD", () => {
  const payload = structuredClone(loadPhaseOneSample());
  const minX = Math.min(...payload.layout.nodes.map((node) => node.position.x));
  const minY = Math.min(...payload.layout.nodes.map((node) => node.position.y));
  const maxX = Math.max(...payload.layout.nodes.map(
    (node) => node.position.x + node.size.width,
  ));
  const maxY = Math.max(...payload.layout.nodes.map(
    (node) => node.position.y + node.size.height,
  ));
  payload.layout.engineMetadata = {
    ...(payload.layout.engineMetadata ?? {}),
    adaptiveCarrierBaseEdges: payload.layout.routedEdges.length,
    adaptiveCarrierGrid: 1,
    adaptiveCarrierMaxX: maxX,
    adaptiveCarrierMaxY: maxY,
    adaptiveCarrierMinX: minX,
    adaptiveCarrierMinY: minY,
    adaptiveCarrierTarget: 100,
    adaptiveCarrierVisibleEdges: 1,
  };

  const expected = createDiagramRenderModel(payload);
  const embedded = readRenderModel(renderDiagramDocument(payload));

  assert.ok(expected.detailEdges.length > 0);
  assert.deepEqual(embedded.detailEdges, expected.detailEdges);
  assert.deepEqual(embedded.edges, expected.edges);
});

test("phase8 catalog mode expands high-degree tables for relation ports", () => {
  const payload = createCatalogPayload();
  payload.layout.engineMetadata = {
    ...(payload.layout.engineMetadata ?? {}),
    canonicalCrossing: canonicalCrossingFixture({
      completeRoutes: false,
      gap: undefined,
      properDrawing: false,
    }),
  };
  const html = renderDiagramDocument(payload);
  const renderModel = readRenderModel(html);
  const hubSize = readTableSize(renderModel, "catalog.Hub");
  const leafSize = readTableSize(renderModel, "catalog.Leaf1");

  assert.match(html, /Model catalog mode: model and DB table names only\./);
  assert.match(
    html,
    /Canonical crossings: pairs 91 · certified lower bound ≥ 53 · gap unavailable/,
  );
  assert.match(html, /diagnostic only/);
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
  assert.match(html, /function readEmbeddedJson\(element\)/);
  assert.match(html, /element instanceof HTMLTemplateElement/);
  assert.match(html, /element\.content\.textContent \|\| ""/);
});

test("phase8 browser runtime declares layoutModes before layout variants are created", () => {
  const html = render();
  const layoutModesIndex = html.indexOf("const layoutModes = ");
  const layoutVariantsIndex = html.indexOf("const layoutVariants = createLayoutVariants(tableMetaList);");

  assert.notEqual(layoutModesIndex, -1, "layoutModes declaration should exist");
  assert.notEqual(layoutVariantsIndex, -1, "layoutVariants initialization should exist");
  assert.ok(
    layoutModesIndex < layoutVariantsIndex,
    "layoutModes must be initialized before createLayoutVariants(tableMetaList) runs",
  );
});

function render(mutatePayload) {
  const payload = structuredClone(loadPhaseOneSample());
  mutatePayload?.(payload);
  return renderDiagramDocument(payload);
}

function canonicalCrossingFixture(overrides = {}) {
  return {
    boundViolation: false,
    certifierVersion: "lb53-v1",
    completeRoutes: true,
    domain: "canonical-simple-v1",
    edgeCount: 1682,
    gap: 38,
    k3nCertificates: 4,
    k3nContribution: 30,
    kuratowskiCertificates: 23,
    kuratowskiContribution: 23,
    lowerBound: 53,
    method: "k3n-mantel+edge-disjoint-kuratowski",
    nodeCount: 1218,
    adjacentEdgeIntersections: 0,
    collinearOverlaps: 0,
    degenerateSegments: 0,
    invariantViolations: 0,
    nonIncidentNodeHits: 0,
    nonProperContacts: 0,
    optimality: 53 / 91,
    pointContacts: 0,
    properDrawing: true,
    routeCrossingPairs: 91,
    routeCrossingPoints: 96,
    selfIntersections: 0,
    ...overrides,
  };
}

function readRenderModel(html) {
  const match = html.match(
    /<template id="erd-render-model">([\s\S]*?)<\/template>/,
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
