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
const browserCanvasSourceModulePath = path.resolve(
  __dirname,
  "../../out/webview/interaction/runtime/browserCanvasDrawSource.js",
);
const runOgdfLayoutModulePath = path.resolve(
  __dirname,
  "../../out/extension/services/layout/runOgdfLayout.js",
);
const sampleModulePath = path.resolve(
  __dirname,
  "../../out/extension/services/loadPhaseOneSample.js",
);
const packageManifest = require(path.resolve(__dirname, "../../package.json"));
const { OGDF_LAYOUT_TOOLBAR_DEFINITIONS } = require(layoutContractModulePath);
const { decodeLayoutSnapshot } = require(protocolDecoderModulePath);
const {
  createDiagramRenderModel,
  measureRenderedEdgeNodeIntersections,
  measureRenderedTableClearance,
} = require(renderModelModulePath);
const {
  evaluateOptimizedLayoutHardTargets,
  measureLayoutRenderedTableClearance,
  synchronizeLayoutNodeSizesWithRenderedTables,
} = require(runOgdfLayoutModulePath);
const { renderDiagramDocument } = require(renderModulePath);
const { loadPhaseOneSample } = require(sampleModulePath);
const { getBrowserCanvasDrawSource } = require(browserCanvasSourceModulePath);

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
  assert.match(html, /Applied Fast Multipole Embedder/);
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

test("phase8 document embeds every semantic carrier in one zoom-independent set", () => {
  const payload = structuredClone(loadPhaseOneSample());
  const expected = createDiagramRenderModel(payload);
  const html = renderDiagramDocument(payload);
  const embedded = readRenderModel(html);

  assert.ok(expected.edges.length > 0);
  assert.deepEqual(embedded.edges, expected.edges);
  assert.equal(Object.hasOwn(embedded, "detailEdges"), false);
  assert.doesNotMatch(html, /applyEdgeSegmentLod/);
  assert.doesNotMatch(html, /GPU_EDGE_LOD/);
  assert.doesNotMatch(html, /GPU_MAX_SEGMENTS_PER_FRAME/);
});

test("phase8 decoder preserves carrier metadata but rejects disconnected multi-endpoint trunks", () => {
  const payload = structuredClone(loadPhaseOneSample());
  const clusterByModelId = {
    "accounts.Author": "cluster-b",
    "blog.Post": "cluster-a",
    "taxonomy.Tag": "cluster-c",
  };
  payload.layout.nodes.forEach((node) => {
    node.clusterId = clusterByModelId[node.modelId] ?? "cluster-other";
  });
  payload.layout.engineMetadata = {
    ...(payload.layout.engineMetadata ?? {}),
    hubCarrierThreshold: 1,
    renderedCarrierRoutes: [{
      carrierId: "H|cluster-a",
      memberEdgeIds: ["edge-post-author", "edge-post-tags"],
      points: [{ x: 120.5, y: 240.25 }, { x: 900.75, y: 480.5 }],
    }],
  };

  const decoded = decodeLayoutSnapshot(payload.layout, "phase8CarrierGeometry");
  assert.deepEqual(
    decoded.engineMetadata?.renderedCarrierRoutes,
    payload.layout.engineMetadata.renderedCarrierRoutes,
  );

  const singleton = structuredClone(payload.layout);
  singleton.engineMetadata.renderedCarrierRoutes[0].memberEdgeIds = ["edge-post-author"];
  assert.doesNotThrow(
    () => decodeLayoutSnapshot(singleton, "phase8SingletonCarrierGeometry"),
  );

  const malformed = structuredClone(payload.layout);
  malformed.engineMetadata.renderedCarrierRoutes[0].memberEdgeIds = [];
  assert.throws(
    () => decodeLayoutSnapshot(malformed, "phase8MalformedCarrierGeometry"),
    /at least one member/,
  );

  const renderModel = createDiagramRenderModel(payload);
  const carrier = renderModel.edges.find(
    (edge) => edge.edgeId === "hub-carrier:cluster-a",
  );
  assert.equal(carrier, undefined);
  assert.ok(renderModel.edges.some((edge) => edge.edgeId === "edge-post-author"));
  assert.ok(renderModel.edges.some((edge) => edge.edgeId === "edge-post-tags"));
});

test("phase8 cluster focus appears only after selecting a member", () => {
  const payload = structuredClone(loadPhaseOneSample());
  payload.layout.nodes.forEach((node) => {
    node.clusterId = node.modelId === "audit.AuditLog"
      ? "cluster-audit"
      : "cluster-content";
  });
  const renderModel = createDiagramRenderModel(payload);
  const content = renderModel.clusterOutlines.find(
    (outline) => outline.clusterId === "cluster-content",
  );

  assert.ok(content);
  assert.equal(content.colorKey, "cluster-content");
  assert.match(content.label, / cluster$/);
  assert.equal(content.memberCount, 3);

  const html = renderDiagramDocument(payload);
  assert.match(html, /function buildClusterOutlineRecords\(scene\)/);
  assert.match(html, /kind: "cluster-outline"/);
  assert.match(html, /scene\.tablesById\.get\(state\.selectedModelId\)/);
  assert.match(html, /table\.meta\.clusterId === selectedClusterId/);
  assert.match(html, /GPU_CLUSTER_OUTLINE_MAX_AREA_RATIO/);
  assert.match(html, /function edgeSelectedClusterRelation\(meta\)/);
  assert.match(html, /isModelInSelectedCluster\(record\.modelId\)/);
  assert.match(html, /selectedModelId: value\.selectedModelId \|\| undefined/);
  assert.match(html, /dispatch\(\{ type: "clear-selection" \}\)/);
  assert.doesNotMatch(html, /clusterId \|\| appLabel/);
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

test("catalog layout input and clearance audit use the exact rendered bundle geometry", () => {
  const payload = createCatalogPayload();
  payload.layout.nodes.forEach((node, index) => {
    node.position = { x: index * 1000, y: 0 };
    node.size = { height: 76, width: 180 };
  });
  payload.layout.engineMetadata = {
    leafBundles: [{
      anchor: { x: 190, y: 217 },
      bbox: { height: 10, width: 10, x: 185, y: 212 },
      leafModelIds: Array.from(
        { length: 39 },
        (_, index) => `catalog.Leaf${index + 1}`,
      ),
      parentModelId: "catalog.Hub",
      sharedRootModelIds: ["catalog.Hub"],
    }],
  };

  const beforeSyncRenderModel = createDiagramRenderModel(payload);
  const beforeSyncAudit = measureRenderedTableClearance(beforeSyncRenderModel);
  assert.equal(
    beforeSyncAudit.bundleNodeOverlaps,
    1,
    "the final canvas geometry should expose the hub/bundle collision",
  );

  const changed = synchronizeLayoutNodeSizesWithRenderedTables(payload);
  assert.ok(changed > 0, "catalog dimensions should replace analyzer dimensions");
  const synchronizedRenderModel = createDiagramRenderModel(payload);
  const synchronizedTableById = new Map(
    synchronizedRenderModel.tables.map((table) => [table.modelId, table]),
  );
  for (const node of payload.layout.nodes) {
    assert.deepEqual(
      node.size,
      synchronizedTableById.get(node.modelId)?.size,
      `native input size should match rendered size for ${node.modelId}`,
    );
  }
  assert.deepEqual(
    payload.layout.nodes.find((node) => node.modelId === "catalog.Hub")?.size,
    { height: 378, width: 356 },
  );
  assert.equal(
    synchronizeLayoutNodeSizesWithRenderedTables(payload),
    0,
    "render-size synchronization should be idempotent",
  );

  const layoutAudit = measureLayoutRenderedTableClearance(payload, payload.layout);
  assert.equal(layoutAudit.bundleNodeOverlaps, 1);
  assert.equal(layoutAudit.bundleBundleOverlaps, 0);
  assert.equal(layoutAudit.nodeOverlaps, 0);
  assert.equal(
    layoutAudit.objectCount,
    payload.layout.nodes.length - 39 + 1,
    "nested leaf tiles should be represented by one synthetic bundle table",
  );

  // This assertion block isolates table/bundle clearance. The catalog fixture's
  // star edges are deliberately collinear here and are covered by the separate
  // rendered edge/node audit below.
  payload.graph.structuralEdges = [];
  payload.layout.engineMetadata = {
    ...payload.layout.engineMetadata,
    bundleEdgeIntersections: 0,
    bundleNodeOverlaps: 0,
    edgeBendTotal: 0,
    edgeCrossings: 0,
    edgeNodeIntersections: 0,
    visualCrossings: 0,
  };
  const rejectedCacheCandidate = evaluateOptimizedLayoutHardTargets(
    payload,
    payload.layout,
    { bboxTargetB: 10, visualTarget: 100 },
  );
  assert.equal(rejectedCacheCandidate.pass, false);
  assert.equal(rejectedCacheCandidate.clearancePass, false);
  assert.match(
    rejectedCacheCandidate.failures.join(" "),
    /1 bundle overlaps/,
  );

  const clearLayout = structuredClone(payload.layout);
  clearLayout.engineMetadata.leafBundles[0].bbox = {
    height: 10,
    width: 10,
    x: 0,
    y: 10_000,
  };
  clearLayout.crossings = [];
  clearLayout.routedEdges = [];
  const acceptedCacheCandidate = evaluateOptimizedLayoutHardTargets(
    payload,
    clearLayout,
    { bboxTargetB: 10, visualTarget: 100 },
  );
  assert.equal(acceptedCacheCandidate.pass, true);
  assert.equal(acceptedCacheCandidate.clearancePass, true);
  assert.equal(acceptedCacheCandidate.bendPass, true);
  assert.equal(acceptedCacheCandidate.bundleEdgePass, true);
  assert.equal(acceptedCacheCandidate.edgeNodePass, true);

  const penetratingLayout = structuredClone(clearLayout);
  penetratingLayout.engineMetadata.edgeNodeIntersections = 1;
  penetratingLayout.engineMetadata.visualCrossings = 1;
  const penetratingCacheCandidate = evaluateOptimizedLayoutHardTargets(
    payload,
    penetratingLayout,
    { bboxTargetB: 10, edgeNodeTarget: 0, visualTarget: 100 },
  );
  assert.equal(
    penetratingCacheCandidate.visualPass,
    true,
    "aggregate visual target alone must not hide an edge through a table",
  );
  assert.equal(penetratingCacheCandidate.edgeNodePass, false);
  assert.equal(penetratingCacheCandidate.pass, false);
  assert.match(
    penetratingCacheCandidate.failures.join(" "),
    /edge\/node intersections 1 exceed target 0/,
  );

  const bundlePenetratingLayout = structuredClone(clearLayout);
  bundlePenetratingLayout.engineMetadata.bundleEdgeIntersections = 1;
  bundlePenetratingLayout.engineMetadata.visualCrossings = 1;
  const bundlePenetratingCandidate = evaluateOptimizedLayoutHardTargets(
    payload,
    bundlePenetratingLayout,
    { bboxTargetB: 10, bundleEdgeTarget: 0, visualTarget: 100 },
  );
  assert.equal(bundlePenetratingCandidate.bundleEdgePass, false);
  assert.equal(bundlePenetratingCandidate.pass, false);
  assert.match(
    bundlePenetratingCandidate.failures.join(" "),
    /bundle\/edge intersections 1 exceed target 0/,
  );
});

test("rendered geometry audit detects diagonal edge penetrations independently of metadata", () => {
  const source = renderedAuditTable("test.Source", 0, 0, 100, 100);
  const blocker = renderedAuditTable("test.Blocker", 220, 110, 80, 80);
  const target = renderedAuditTable("test.Target", 400, 200, 100, 100);
  const renderModel = {
    edges: [
      {
        edgeId: "edge-source-target",
        points: "100,50 400,250",
        sourceModelId: source.modelId,
        targetModelId: target.modelId,
      },
    ],
    tables: [source, blocker, target],
  };

  const penetrating = measureRenderedEdgeNodeIntersections(renderModel);
  assert.equal(penetrating.count, 1);
  assert.equal(penetrating.bundleCount, 0);
  assert.equal(penetrating.nodeCount, 1);
  assert.deepEqual(penetrating.hits, [
    { edgeId: "edge-source-target", nodeModelId: "test.Blocker" },
  ]);

  blocker.position.y = 300;
  const clear = measureRenderedEdgeNodeIntersections(renderModel);
  assert.equal(clear.count, 0);
});

test("rendered geometry audit separates bundle hits and preserves carrier endpoints", () => {
  const source = renderedAuditTable("test.Source", 0, 0, 100, 100);
  const bundle = renderedAuditTable("__leafbundle.Group_0", 220, 80, 80, 80);
  const target = renderedAuditTable("test.Target", 400, 0, 100, 100);
  const baseModel = {
    bundleLeavesByFakeId: { [bundle.modelId]: ["test.Leaf"] },
    edges: [{
      edgeId: "edge-source-target",
      points: "100,200 400,200",
      sourceModelId: source.modelId,
      targetModelId: target.modelId,
    }],
    tables: [source, bundle, target],
  };

  const reattached = measureRenderedEdgeNodeIntersections(baseModel);
  assert.equal(reattached.bundleCount, 1);
  assert.equal(reattached.nodeCount, 0);

  baseModel.edges[0].preserveRouteEndpoints = true;
  const preserved = measureRenderedEdgeNodeIntersections(baseModel);
  assert.equal(preserved.count, 0);
});

test("phase8 browser runtime renders a GPU scene with minimap and viewport-aware culling", () => {
  const html = render();

  assert.match(html, /function createErdLogTimestamp\(\)/);
  assert.match(html, /function logErd\(level, event, details\)/);
  assert.match(html, /type: "diagram\.log"/);
  assert.match(html, /webview\.bootstrap/);
  assert.match(html, /renderer\.selected/);
  assert.match(html, /scene\.graph\.built/);
  assert.match(html, /avoidedTablePenetrations/);
  assert.match(html, /avoidedBundlePenetrations/);
  assert.match(html, /avoidedNodePenetrations/);
  assert.match(html, /unresolvedRoutePenetrations/);
  assert.match(html, /visibilityFallbackEdges/);
  assert.match(html, /function segmentRectangleInteriorInterval\(/);
  assert.match(html, /function routeEdgePathAroundTables\(/);
  assert.match(html, /function routeCollisionFreeVisibilityPath\(/);
  assert.match(html, /function discoverVisibleDetourPoints\(/);
  assert.match(html, /function createTableDetourCandidates\(/);
  assert.doesNotMatch(html, /function createClippedLineSegments\(/);
  assert.match(html, /base \+ tangent \* cap \+ normal/);
  assert.doesNotMatch(html, /if \(!vertical && !horizontal\)/);
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
  assert.match(
    html,
    /function drawWebGpuScene\(renderer, segments, overlays, tables, labels, leafBundles, leafTiles\)/,
  );
  assert.match(html, /arrayStride: 48/);
  assert.match(html, /erdUniforms: ErdCommonUniforms/);
  assert.doesNotMatch(html, /var<uniform> common:/);
  assert.match(html, /renderer\.backend === "webgpu"/);
  assert.match(html, /function createLabelAtlas\(gl\)/);
  assert.match(html, /function collectVisibleTables\(scene, bounds\)/);
  assert.match(html, /function collectVisibleSegments\(scene, bounds\)/);
  assert.match(html, /function applyLiveDragTableRecord\(scene, records, bounds\)/);
  assert.match(html, /function applyLiveDragEdgeSegments\(records, bounds\)/);
  assert.match(
    html,
    /function collectLiveDragEdgeSegments\(\s*activeDrag,\s*movedModelIds,\s*bounds,\s*overrideById,/,
  );
  assert.match(html, /GPU_EDGE_TABLE_CLEARANCE = 10/);
  assert.match(html, /GPU_EDGE_DETOUR_MAX_STEPS = 96/);
  assert.match(html, /function routeEdgeRecordsAroundLiveDragTables\(/);
  assert.match(html, /function fitFontToWidth\(context, font, text, maxWidth\)/);
  assert.doesNotMatch(html, /function trimTextToWidth\(/);
  assert.doesNotMatch(html, /…/);
  assert.match(html, /overflow-wrap: anywhere/);
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
  assert.match(html, /function applyProgressSemanticRenderModel\(preview\)/);
  assert.match(html, /renderModel\.edges = preview\.edges\.slice\(\)/);
  assert.match(
    html,
    /applyProgressSemanticRenderModel\(msg\.semanticRenderModel\);\s*dispatch\(\{ type: "apply-progress-positions"/,
  );
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

test("phase8 browser router keeps obstacle detours continuous and collision-free", () => {
  const {
    collectEdgePathCollisions,
    routeCollisionFreeVisibilityPath,
    routeEdgePathAroundTables,
  } =
    createBrowserEdgeRouter({});
  const scene = createRoutingScene([
    routingTable("blocker-a", 360, -70, 120, 140),
    routingTable("blocker-b", 610, -20, 120, 140),
  ]);
  const meta = { sourceModelId: "source", targetModelId: "target" };
  const routed = routeEdgePathAroundTables(
    [{ x: 0, y: 0 }, { x: 1000, y: 0 }],
    meta,
    scene,
  );

  assert.equal(routed.initialCollisions.length, 2);
  assert.equal(routed.unresolvedCollisions.length, 0);
  assert.ok(routed.points.length > 2);
  assert.ok(routed.points.every((point) =>
    Number.isFinite(point.x) && Number.isFinite(point.y)));
  assert.equal(collectEdgePathCollisions(routed.points, meta, scene).length, 0);

  const denseScene = createRoutingScene(
    Array.from({ length: 20 }, (_, index) =>
      routingTable(`dense-${index}`, 180 + index * 145, -80, 100, 160)),
  );
  const denseRoute = routeEdgePathAroundTables(
    [{ x: 0, y: 0 }, { x: 3100, y: 0 }],
    meta,
    denseScene,
  );
  assert.equal(denseRoute.initialCollisions.length, 20);
  assert.equal(denseRoute.unresolvedCollisions.length, 0);
  assert.equal(
    collectEdgePathCollisions(denseRoute.points, meta, denseScene).length,
    0,
  );

  const alternatingMaze = createRoutingScene([
    routingTable("wall-a", 180, -420, 130, 470),
    routingTable("wall-b", 360, -50, 130, 470),
    routingTable("wall-c", 540, -420, 130, 470),
    routingTable("wall-d", 720, -50, 130, 470),
  ]);
  const visibilityRoute = routeCollisionFreeVisibilityPath(
    [{ x: 0, y: 0 }, { x: 1040, y: 0 }],
    meta,
    alternatingMaze,
  );
  assert.ok(visibilityRoute);
  assert.ok(visibilityRoute.length > 2);
  assert.equal(
    collectEdgePathCollisions(visibilityRoute, meta, alternatingMaze).length,
    0,
  );

  const bundleId = "__leafbundle.Parent_0";
  const bundleRouter = createBrowserEdgeRouter({ [bundleId]: ["test.Leaf"] });
  const bundleRoute = bundleRouter.routeEdgePathAroundTables(
    [{ x: 0, y: 0 }, { x: 800, y: 0 }],
    {
      logicalEndpointModelIds: ["test.Leaf", "test.Base"],
      sourceModelId: "test.Leaf",
      targetModelId: "test.Base",
    },
    createRoutingScene([routingTable(bundleId, -40, -120, 340, 240)]),
  );
  assert.equal(bundleRoute.initialCollisions.length, 0);

  const carrierRoute = routeEdgePathAroundTables(
    [{ x: 40, y: 40 }, { x: 800, y: 0 }],
    {
      logicalEndpointModelIds: ["test.Representative", "test.Sibling", "test.Base"],
      sourceModelId: "test.Representative",
      targetModelId: "test.Base",
    },
    createRoutingScene([routingTable("test.Sibling", 0, 0, 180, 120)]),
  );
  assert.equal(
    carrierRoute.initialCollisions.length,
    0,
    "every logical carrier endpoint must be allowed to emit its shared carrier",
  );

  const tightEndpointScene = createRoutingScene([
    routingTable("source", 0, -50, 100, 100),
    routingTable("target", 500, -50, 100, 100),
    routingTable("adjacent-blocker", 390, -30, 109, 60),
  ]);
  const tightEndpointRoute = routeEdgePathAroundTables(
    [{ x: 100, y: 0 }, { x: 500, y: 0 }],
    meta,
    tightEndpointScene,
  );
  assert.equal(tightEndpointRoute.initialCollisions.length, 1);
  assert.equal(tightEndpointRoute.unresolvedCollisions.length, 0);
  assert.equal(
    collectEdgePathCollisions(tightEndpointRoute.points, meta, tightEndpointScene).length,
    0,
  );
  assert.notDeepEqual(
    tightEndpointRoute.points[tightEndpointRoute.points.length - 1],
    { x: 500, y: 0 },
    "a blocked fixed endpoint should move to a free port on the same table",
  );
});

function render(mutatePayload) {
  const payload = structuredClone(loadPhaseOneSample());
  mutatePayload?.(payload);
  return renderDiagramDocument(payload);
}

function createBrowserEdgeRouter(bundleLeavesByFakeIdRaw) {
  const factory = new Function(
    "round2",
    "normalizePoints",
    "findSegments",
    "rectIntersectsBounds",
    "bundleLeavesByFakeIdRaw",
    `${getBrowserCanvasDrawSource()}\nreturn { collectEdgePathCollisions, routeCollisionFreeVisibilityPath, routeEdgePathAroundTables };`,
  );
  return factory(
    (value) => Math.round(value * 100) / 100,
    (points) => points.filter((point, index) =>
      index === 0
      || point.x !== points[index - 1].x
      || point.y !== points[index - 1].y),
    (points) => points.slice(1).map((end, index) => ({
      end,
      start: points[index],
    })),
    (x, y, width, height, bounds, padding = 0) =>
      x + width + padding >= bounds.left
      && x - padding <= bounds.right
      && y + height + padding >= bounds.top
      && y - padding <= bounds.bottom,
    bundleLeavesByFakeIdRaw,
  );
}

function createRoutingScene(tables) {
  const tableBuckets = new Map();
  const tablesById = new Map();
  for (const table of tables) {
    tablesById.set(table.modelId, table);
    const startColumn = Math.floor(table.x / 960);
    const endColumn = Math.floor((table.x + table.width) / 960);
    const startRow = Math.floor(table.y / 960);
    const endRow = Math.floor((table.y + table.height) / 960);
    for (let row = startRow; row <= endRow; row += 1) {
      for (let column = startColumn; column <= endColumn; column += 1) {
        const key = `${column}:${row}`;
        const bucket = tableBuckets.get(key) ?? [];
        bucket.push(table.modelId);
        tableBuckets.set(key, bucket);
      }
    }
  }
  return { tableBuckets, tables, tablesById };
}

function routingTable(modelId, x, y, width, height) {
  return { height, modelId, width, x, y };
}

function renderedAuditTable(modelId, x, y, width, height) {
  return {
    hidden: false,
    modelId,
    position: { x, y },
    size: { height, width },
  };
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
