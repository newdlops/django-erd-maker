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
  measureRenderedEdgeCrossings,
  measureRenderedEdgeNodeIntersections,
  measureRenderedTableClearance,
  measureRenderedVisualConflicts,
} = require(renderModelModulePath);
const {
  evaluateOptimizedLayoutHardTargets,
  measureLayoutRenderedTableClearance,
  measureLayoutRenderedVisualConflicts,
  parseV36AcceptedMoves,
  synchronizeLayoutRouteEndpoints,
  synchronizeLayoutRenderedVisualMetrics,
  synchronizeLayoutNodeSizesWithRenderedTables,
} = require(runOgdfLayoutModulePath);
const { renderDiagramDocument } = require(renderModulePath);
const { loadPhaseOneSample } = require(sampleModulePath);
const { getBrowserCanvasDrawSource } = require(browserCanvasSourceModulePath);

test("v36 completion marker exposes whether a reroute can change geometry", () => {
  assert.equal(parseV36AcceptedMoves("done accepted=0 elapsed=14.9s"), 0);
  assert.equal(
    parseV36AcceptedMoves(
      "round 01 accepted=0\ndone accepted=2 elapsed=18.1s\n",
    ),
    2,
  );
  assert.equal(parseV36AcceptedMoves("no completion marker"), undefined);
});

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

test("phase8 sidebar prioritizes the selected model and isolates diagram details", () => {
  const html = render();
  const modelTabIndex = html.indexOf('data-sidebar-tab="model"');
  const diagramTabIndex = html.indexOf('data-sidebar-tab="diagram"');
  const modelSheetIndex = html.indexOf('data-sidebar-sheet="model"');
  const diagramSheetIndex = html.indexOf('data-sidebar-sheet="diagram"');
  const stageIndex = html.indexOf('<section class="erd-stage">');
  const modelSheet = html.slice(modelSheetIndex, diagramSheetIndex);
  const diagramSheet = html.slice(diagramSheetIndex, stageIndex);

  assert.ok(modelTabIndex >= 0 && modelTabIndex < diagramTabIndex);
  assert.ok(modelSheetIndex >= 0 && modelSheetIndex < diagramSheetIndex);
  assert.match(html, /data-sidebar-tab="model">Model<\/button>/);
  assert.match(html, /data-sidebar-tab="diagram">Diagram<\/button>/);
  assert.match(modelSheet, /data-model-id="blog\.Post"/);
  assert.doesNotMatch(modelSheet, /<h2>Setup<\/h2>|<h2>Diagnostics<\/h2>/);
  assert.match(diagramSheet, /<h2>Setup<\/h2>/);
  assert.match(diagramSheet, /<h2>Diagnostics<\/h2>/);
  assert.match(html, /function setSidebarSheet\(sheetId, focusTab\)/);
  assert.match(html, /setSidebarSheet\("model", false\)/);
});

test("phase8 model sheet asks for a node instead of choosing one implicitly", () => {
  const html = render((payload) => {
    payload.view.selectedModelId = undefined;
    payload.view.selectedMethodContext = undefined;
  });
  const modelSheetIndex = html.indexOf('data-sidebar-sheet="model"');
  const diagramSheetIndex = html.indexOf('data-sidebar-sheet="diagram"');
  const modelSheet = html.slice(modelSheetIndex, diagramSheetIndex);

  assert.match(modelSheet, /<h2>Select a model<\/h2>/);
  assert.match(modelSheet, /Click a node in the diagram to inspect its model details\./);
  assert.doesNotMatch(modelSheet, /data-model-id="blog\.Post"/);
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

test("phase8 route endpoints survive decoding and are restored for older caches", () => {
  const payload = structuredClone(loadPhaseOneSample());
  const structuralEdgeById = new Map(
    payload.graph.structuralEdges.map((edge) => [edge.id, edge]),
  );
  const firstRoute = payload.layout.routedEdges[0];
  const firstStructuralEdge = structuralEdgeById.get(firstRoute.edgeId);
  assert.ok(firstStructuralEdge);
  firstRoute.sourceModelId = firstStructuralEdge.sourceModelId;
  firstRoute.targetModelId = firstStructuralEdge.targetModelId;

  const decoded = decodeLayoutSnapshot(payload.layout, "phase8RouteEndpoints");
  assert.equal(decoded.routedEdges[0].sourceModelId, firstStructuralEdge.sourceModelId);
  assert.equal(decoded.routedEdges[0].targetModelId, firstStructuralEdge.targetModelId);

  for (const route of decoded.routedEdges) {
    delete route.sourceModelId;
    delete route.targetModelId;
  }
  payload.layout = decoded;
  assert.equal(
    synchronizeLayoutRouteEndpoints(payload, payload.layout),
    payload.layout.routedEdges.length,
  );
  for (const route of payload.layout.routedEdges) {
    const structuralEdge = structuralEdgeById.get(route.edgeId);
    assert.equal(route.sourceModelId, structuralEdge?.sourceModelId);
    assert.equal(route.targetModelId, structuralEdge?.targetModelId);
  }
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

test("phase8 render model flattens every supplied polyline to its two endpoints", () => {
  const payload = structuredClone(loadPhaseOneSample());
  const route = payload.layout.routedEdges.find((candidate) =>
    candidate.points.length >= 2
  );
  assert.ok(route);
  const start = route.points[0];
  const end = route.points[route.points.length - 1];
  route.points = [
    start,
    { x: (start.x + end.x) / 2, y: (start.y + end.y) / 2 + 500 },
    end,
  ];

  const renderModel = createDiagramRenderModel(payload);
  const pointSets = renderModel.edges
    .map((edge) => edge.points.trim())
    .filter(Boolean)
    .map((points) => points.split(/\s+/));
  assert.ok(pointSets.length > 0);
  assert.ok(pointSets.every((points) => points.length === 2));
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

test("phase8 render model applies native singleton boundary-port geometry", () => {
  const payload = structuredClone(loadPhaseOneSample());
  payload.layout.engineMetadata = {
    ...(payload.layout.engineMetadata ?? {}),
    renderedCarrierRoutes: [{
      carrierId: "edge-post-tags",
      memberEdgeIds: ["edge-post-tags"],
      points: [{ x: 660, y: 120 }, { x: 740, y: 120 }],
    }],
  };

  const renderModel = createDiagramRenderModel(payload);
  const edge = renderModel.edges.find(
    (candidate) => candidate.edgeId === "edge-post-tags",
  );

  assert.ok(edge);
  assert.equal(edge.points, "660,120 740,120");
  assert.equal(edge.preserveRouteEndpoints, true);
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

test("catalog layout input resolves and audits the exact rendered bundle geometry", () => {
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
    0,
    "the final canvas geometry should relocate the bundle away from the hub",
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
  assert.equal(layoutAudit.bundleNodeOverlaps, 0);
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
  const resolvedCacheCandidate = evaluateOptimizedLayoutHardTargets(
    payload,
    payload.layout,
    { bboxTargetB: 10, visualTarget: 100 },
  );
  assert.equal(resolvedCacheCandidate.pass, true);
  assert.equal(resolvedCacheCandidate.clearancePass, true);

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

  const staleEdgeNodeMetadataLayout = structuredClone(clearLayout);
  staleEdgeNodeMetadataLayout.engineMetadata.edgeNodeIntersections = 1;
  staleEdgeNodeMetadataLayout.engineMetadata.visualCrossings = 1;
  const staleEdgeNodeMetadataCandidate = evaluateOptimizedLayoutHardTargets(
    payload,
    staleEdgeNodeMetadataLayout,
    { bboxTargetB: 10, edgeNodeTarget: 0, visualTarget: 100 },
  );
  assert.equal(
    staleEdgeNodeMetadataCandidate.visualCrossings,
    0,
    "carrier metadata must not be reported as a final-scene conflict",
  );
  assert.equal(
    staleEdgeNodeMetadataCandidate.visualPass,
    true,
    "the rendered scene is clear even when stale native metadata says otherwise",
  );
  assert.equal(staleEdgeNodeMetadataCandidate.edgeNodePass, true);
  assert.equal(staleEdgeNodeMetadataCandidate.pass, true);

  const staleBundleMetadataLayout = structuredClone(clearLayout);
  staleBundleMetadataLayout.engineMetadata.bundleEdgeIntersections = 1;
  staleBundleMetadataLayout.engineMetadata.visualCrossings = 1;
  const staleBundleMetadataCandidate = evaluateOptimizedLayoutHardTargets(
    payload,
    staleBundleMetadataLayout,
    { bboxTargetB: 10, bundleEdgeTarget: 0, visualTarget: 100 },
  );
  assert.equal(staleBundleMetadataCandidate.bundleEdgeIntersections, 0);
  assert.equal(staleBundleMetadataCandidate.bundleEdgePass, true);
  assert.equal(staleBundleMetadataCandidate.pass, true);
});

test("optimized hard targets reject an edge that penetrates the actual final scene", () => {
  const payload = structuredClone(loadPhaseOneSample());
  const nodeById = new Map(
    payload.layout.nodes.map((node) => [node.modelId, node]),
  );
  Object.assign(nodeById.get("blog.Post"), {
    position: { x: 0, y: 0 },
    size: { height: 280, width: 280 },
  });
  Object.assign(nodeById.get("taxonomy.Tag"), {
    position: { x: 1000, y: 0 },
    size: { height: 160, width: 220 },
  });
  Object.assign(nodeById.get("audit.AuditLog"), {
    position: { x: 500, y: 40 },
    size: { height: 140, width: 260 },
  });
  Object.assign(nodeById.get("accounts.Author"), {
    position: { x: 0, y: 1000 },
    size: { height: 180, width: 240 },
  });
  payload.graph.structuralEdges = payload.graph.structuralEdges.filter(
    (edge) => edge.id === "edge-post-tags",
  );
  payload.layout.routedEdges = [{
    crossingIds: [],
    edgeId: "edge-post-tags",
    points: [{ x: 280, y: 140 }, { x: 1000, y: 140 }],
  }];
  payload.layout.crossings = [];
  payload.layout.engineMetadata = {
    ...(payload.layout.engineMetadata ?? {}),
    edgeBendTotal: 0,
    edgeNodeIntersections: 0,
    visualCrossings: 0,
  };

  const evaluation = evaluateOptimizedLayoutHardTargets(payload, payload.layout, {
    bboxTargetB: 10,
    edgeNodeTarget: 0,
    expectedRouteEdgeIds: ["edge-post-tags"],
    visualTarget: 100,
  });

  assert.equal(evaluation.renderedEdgeCount, 1);
  assert.equal(evaluation.renderedEdgeNodeIntersections, 1);
  assert.equal(evaluation.edgeNodePass, false);
  assert.equal(evaluation.pass, false);
  assert.match(
    evaluation.failures.join(" "),
    /edge\/node intersections 1 exceed target 0/,
  );
});

test("rendered visual metrics preserve carrier scores under an explicit scope", () => {
  const payload = structuredClone(loadPhaseOneSample());
  payload.layout.engineMetadata = {
    ...(payload.layout.engineMetadata ?? {}),
    bundleEdgeIntersections: 8,
    bundleNodeOverlaps: 2,
    edgeCrossings: 26,
    edgeNodeIntersections: 54,
    nodeOverlaps: 3,
    routeSegments: 468,
    visualCrossings: 88,
  };
  const expected = measureLayoutRenderedVisualConflicts(payload, payload.layout);
  const actual = synchronizeLayoutRenderedVisualMetrics(payload, payload.layout);

  assert.deepEqual(actual, expected);
  assert.equal(
    payload.layout.engineMetadata.visualCrossingsScope,
    "rendered-semantic-carrier-v2",
  );
  assert.equal(payload.layout.engineMetadata.carrierVisualCrossings, 88);
  assert.equal(payload.layout.engineMetadata.carrierEdgeCrossings, 26);
  assert.equal(payload.layout.engineMetadata.carrierEdgeNodeIntersections, 54);
  assert.equal(payload.layout.engineMetadata.carrierBundleEdgeIntersections, 8);
  assert.equal(payload.layout.engineMetadata.carrierNodeOverlaps, 3);
  assert.equal(payload.layout.engineMetadata.carrierBundleNodeOverlaps, 2);
  assert.equal(payload.layout.engineMetadata.carrierRouteSegments, 468);
  assert.equal(payload.layout.engineMetadata.visualCrossings, expected.visualCrossings);
  assert.equal(payload.layout.engineMetadata.edgeCrossings, expected.edgeCrossings);
  assert.equal(
    payload.layout.engineMetadata.edgeNodeIntersections,
    expected.edgeNodeIntersections,
  );
  assert.equal(payload.layout.engineMetadata.renderedEdgeCount, expected.edgeCount);

  synchronizeLayoutRenderedVisualMetrics(payload, payload.layout);
  assert.equal(
    payload.layout.engineMetadata.carrierVisualCrossings,
    88,
    "re-auditing must not overwrite the original carrier-domain diagnostic",
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

test("rendered geometry audit counts visible bundle leaf siblings as obstacles", () => {
  const bundleId = "__leafbundle.Group_0";
  const endpointLeaf = renderedAuditTable("test.EndpointLeaf", 0, 0, 80, 80);
  const siblingLeaf = renderedAuditTable("test.SiblingLeaf", 100, 0, 80, 80);
  const bundle = renderedAuditTable(bundleId, -10, -10, 210, 100);
  const target = renderedAuditTable("test.Target", 400, 0, 100, 100);
  const renderModel = {
    bundleLeavesByFakeId: {
      [bundleId]: [endpointLeaf.modelId, siblingLeaf.modelId],
    },
    edges: [{
      edgeId: "edge-leaf-target",
      logicalEndpointModelIds: [endpointLeaf.modelId, target.modelId],
      points: "80,40 400,40",
      sourceModelId: endpointLeaf.modelId,
      targetModelId: target.modelId,
    }],
    tables: [bundle, endpointLeaf, siblingLeaf, target],
  };

  const collisions = measureRenderedEdgeNodeIntersections(renderModel);
  assert.deepEqual(collisions.hits, [
    { edgeId: "edge-leaf-target", nodeModelId: siblingLeaf.modelId },
  ]);
  assert.equal(collisions.bundleCount, 0);
  assert.equal(collisions.nodeCount, 1);
});

test("rendered visual audit counts proper crossings from every visible edge", () => {
  const sourceA = renderedAuditTable("test.SourceA", 0, 0, 100, 100);
  const targetA = renderedAuditTable("test.TargetA", 400, 400, 100, 100);
  const sourceB = renderedAuditTable("test.SourceB", 0, 400, 100, 100);
  const targetB = renderedAuditTable("test.TargetB", 400, 0, 100, 100);
  const renderModel = {
    edges: [
      {
        edgeId: "edge-a",
        points: "100,100 400,400",
        preserveRouteEndpoints: true,
        sourceModelId: sourceA.modelId,
        targetModelId: targetA.modelId,
      },
      {
        edgeId: "edge-b",
        points: "100,400 400,100",
        preserveRouteEndpoints: true,
        sourceModelId: sourceB.modelId,
        targetModelId: targetB.modelId,
      },
    ],
    tables: [sourceA, targetA, sourceB, targetB],
  };

  assert.deepEqual(measureRenderedEdgeCrossings(renderModel), {
    edgeCount: 2,
    edgeCrossings: 1,
    routeSegments: 2,
  });
  const visual = measureRenderedVisualConflicts(renderModel);
  assert.equal(visual.edgeCrossings, 1);
  assert.equal(visual.edgeNodeIntersections, 0);
  assert.equal(visual.visualCrossings, 1);
});

test("phase8 browser runtime renders a GPU scene with minimap and viewport-aware culling", () => {
  const html = render();

  assert.match(html, /function createErdLogTimestamp\(\)/);
  assert.match(html, /function logErd\(level, event, details\)/);
  assert.match(html, /type: "diagram\.log"/);
  assert.match(html, /webview\.bootstrap/);
  assert.match(html, /renderer\.selected/);
  assert.match(html, /scene\.graph\.built/);
  assert.match(html, /bendPolicy: "straight-only"/);
  assert.match(html, /straightTablePenetrations/);
  assert.match(html, /straightBundlePenetrations/);
  assert.match(html, /straightNodePenetrations/);
  assert.match(html, /straightCollisionEdges/);
  assert.match(html, /function segmentRectangleInteriorInterval\(/);
  assert.match(html, /function createStraightEdgePath\(/);
  assert.doesNotMatch(html, /function routeEdgePathAroundTables\(/);
  assert.doesNotMatch(html, /function routeCollisionFreeVisibilityPath\(/);
  assert.doesNotMatch(html, /function discoverVisibleDetourPoints\(/);
  assert.doesNotMatch(html, /function createTableDetourCandidates\(/);
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
  assert.doesNotMatch(html, /GPU_EDGE_DETOUR/);
  assert.doesNotMatch(html, /GPU_EDGE_VISIBILITY/);
  assert.doesNotMatch(html, /function routeEdgeRecordsAroundLiveDragTables\(/);
  assert.doesNotMatch(html, /data-edge-bundle-toggle/);
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

test("phase8 browser keeps one straight segment and audits collisions without detours", () => {
  const {
    collectEdgePathCollisions,
    createStraightEdgePath,
  } =
    createBrowserEdgeRouter({});
  const scene = createRoutingScene([
    routingTable("blocker-a", 360, -70, 120, 140),
    routingTable("blocker-b", 610, -20, 120, 140),
  ]);
  const meta = { sourceModelId: "source", targetModelId: "target" };
  const straight = createStraightEdgePath(
    [{ x: 0, y: 0 }, { x: 500, y: 160 }, { x: 1000, y: 0 }],
    meta,
    scene,
  );

  assert.equal(straight.collisions.length, 2);
  assert.deepEqual(straight.points, [{ x: 0, y: 0 }, { x: 1000, y: 0 }]);
  assert.ok(straight.points.every((point) =>
    Number.isFinite(point.x) && Number.isFinite(point.y)));
  assert.equal(collectEdgePathCollisions(straight.points, meta, scene).length, 2);

  const denseScene = createRoutingScene(
    Array.from({ length: 20 }, (_, index) =>
      routingTable(`dense-${index}`, 180 + index * 145, -80, 100, 160)),
  );
  const denseRoute = createStraightEdgePath(
    [{ x: 0, y: 0 }, { x: 3100, y: 0 }],
    meta,
    denseScene,
  );
  assert.equal(denseRoute.collisions.length, 20);
  assert.equal(denseRoute.points.length, 2);
  assert.equal(
    collectEdgePathCollisions(denseRoute.points, meta, denseScene).length,
    20,
  );

  const bundleId = "__leafbundle.Parent_0";
  const bundleRouter = createBrowserEdgeRouter({ [bundleId]: ["test.Leaf"] });
  const bundleRoute = bundleRouter.createStraightEdgePath(
    [{ x: 0, y: 0 }, { x: 800, y: 0 }],
    {
      logicalEndpointModelIds: ["test.Leaf", "test.Base"],
      sourceModelId: "test.Leaf",
      targetModelId: "test.Base",
    },
    createRoutingScene([routingTable(bundleId, -40, -120, 340, 240)]),
  );
  assert.equal(bundleRoute.collisions.length, 0);

  const carrierRoute = createStraightEdgePath(
    [{ x: 40, y: 40 }, { x: 800, y: 0 }],
    {
      logicalEndpointModelIds: ["test.Representative", "test.Sibling", "test.Base"],
      sourceModelId: "test.Representative",
      targetModelId: "test.Base",
    },
    createRoutingScene([routingTable("test.Sibling", 0, 0, 180, 120)]),
  );
  assert.equal(
    carrierRoute.collisions.length,
    1,
    "logical carrier membership must not hide a physical table penetration",
  );

  const tightEndpointScene = createRoutingScene([
    routingTable("source", 0, -50, 100, 100),
    routingTable("target", 500, -50, 100, 100),
    routingTable("adjacent-blocker", 390, -30, 109, 60),
  ]);
  const tightEndpointRoute = createStraightEdgePath(
    [{ x: 100, y: 0 }, { x: 500, y: 0 }],
    meta,
    tightEndpointScene,
  );
  assert.equal(tightEndpointRoute.collisions.length, 1);
  assert.equal(
    collectEdgePathCollisions(tightEndpointRoute.points, meta, tightEndpointScene).length,
    1,
  );
  assert.deepEqual(
    tightEndpointRoute.points[tightEndpointRoute.points.length - 1],
    { x: 500, y: 0 },
    "straight-only rendering must not move an endpoint to create a detour",
  );

  const selfScene = createRoutingScene([
    routingTable("self", 100, 200, 180, 120),
  ]);
  const selfRoute = createStraightEdgePath(
    [{ x: 280, y: 260 }, { x: 280, y: 260 }],
    { sourceModelId: "self", targetModelId: "self" },
    selfScene,
  );
  assert.equal(selfRoute.points.length, 2);
  assert.notDeepEqual(selfRoute.points[0], selfRoute.points[1]);
  assert.equal(selfRoute.points[0].x, selfRoute.points[1].x);
  assert.equal(selfRoute.collisions.length, 0);
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
    `${getBrowserCanvasDrawSource()}\nreturn { collectEdgePathCollisions, createStraightEdgePath };`,
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
