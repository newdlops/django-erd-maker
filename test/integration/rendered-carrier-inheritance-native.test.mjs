import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { createRequire } from "node:module";
import { promisify } from "node:util";
import { fileURLToPath } from "node:url";

const execFileAsync = promisify(execFile);
const require = createRequire(import.meta.url);
const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(__dirname, "../..");
const binaryPath = process.env.DJANGO_ERD_OGDF_LAYOUT_BIN ?? path.join(
  repoRoot,
  "bin",
  "ogdf",
  `${process.platform}-${process.arch}`,
  process.platform === "win32"
    ? "django-erd-ogdf-layout.exe"
    : "django-erd-ogdf-layout",
);
const renderModelModulePath = path.resolve(
  repoRoot,
  "out/webview/state/createDiagramRenderModel.js",
);
const binaryAvailable = await pathExists(binaryPath);
const renderModelAvailable = await pathExists(renderModelModulePath);

test("native and webview share one complete canonical direct scene", {
  skip: !binaryAvailable || !renderModelAvailable,
}, async () => {
  const fixture = createCarrierFixture();
  const directory = await fs.mkdtemp(
    path.join(os.tmpdir(), "django-erd-carrier-inheritance-"),
  );
  const nodesPath = path.join(directory, "nodes.tsv");
  const edgesPath = path.join(directory, "edges.tsv");
  await fs.writeFile(
    nodesPath,
    `${fixture.nodes.map((modelId, index) =>
      `${modelId}\t120\t80\t${(index % 12) * 180}\t${Math.floor(index / 12) * 140}\ttest`
    ).join("\n")}\n`,
    "utf8",
  );
  await fs.writeFile(
    edgesPath,
    `${fixture.edges.map((edge) => [
      edge.id,
      edge.sourceModelId,
      edge.targetModelId,
      edge.kind,
      edge.provenance,
    ].join("\t")).join("\n")}\n`,
    "utf8",
  );

  try {
    const { stdout } = await execFileAsync(binaryPath, [
      "layout",
      "--mode", "fmmm",
      "--nodes-file", nodesPath,
      "--edges-file", edgesPath,
      "--edge-routing", "straight",
      "--cluster-graph", "1",
    ], {
      cwd: repoRoot,
      env: {
        ...process.env,
        DJERD_CANONICAL_CROSSING_CACHE: "0",
        DJERD_HUB_CARRIER_CROSS_FINAL: "0",
        DJERD_HUB_CARRIER_CROSS_FINAL_THRESHOLD: "2",
        DJERD_INHERITANCE_CARRIER_FINAL: "0",
        DJERD_INTRA_CLUSTER_CARRIER_FINAL: "0",
        DJERD_MULTISTART_RUNS: "1",
        DJERD_NO_CARRIER_CROSS: "1",
        DJERD_RENDERED_CARRIER_GEOMETRY_OPT_FINAL: "1",
        DJERD_RENDERED_CARRIER_BUNDLE_EDGE_TARGET: "0",
        DJERD_RENDERED_CARRIER_EDGE_NODE_TARGET: "0",
        DJERD_RENDERED_CARRIER_METRICS_FINAL: "1",
        DJERD_RENDERED_CARRIER_NODE_CLEAR_DIRECTIONS: "24",
        DJERD_RENDERED_CARRIER_NODE_CLEAR_FINAL: "1",
        DJERD_RENDERED_CARRIER_NODE_CLEAR_MAX_SHIFT: "1200",
        DJERD_RENDERED_CARRIER_NODE_CLEAR_ROUNDS: "8",
        DJERD_RENDERED_CARRIER_NODE_TARGET_FINAL: "1",
        DJERD_RENDERED_CARRIER_NODE_TARGET_PORT_SAMPLES: "12",
        DJERD_RENDERED_CARRIER_NODE_TARGET_ROUNDS: "8",
        DJERD_RENDERED_CARRIER_VISUAL_TARGET: "1000000",
        DJERD_RENDERED_NODE_CLEARANCE_FINAL: "1",
        DJERD_RENDERED_STRAIGHT_PORT_OPT_FINAL: "1",
      },
      maxBuffer: 32 * 1024 * 1024,
      timeout: 30_000,
    });
    const layout = JSON.parse(stdout);
    assert.ok(
      layout.engineMetadata?.leafBundles?.length > 0,
      "fixture should exercise bundled-leaf inheritance",
    );
    const canonical = layout.engineMetadata?.canonicalCrossing;
    assert.ok(canonical, "native layout should emit canonical crossing metadata");
    const contactCategories = [
      canonical.invariantViolations,
      canonical.degenerateSegments,
      canonical.collinearOverlaps,
      canonical.pointContacts,
      canonical.selfIntersections,
      canonical.adjacentEdgeIntersections,
      canonical.nonIncidentNodeHits,
    ];
    assert.ok(
      contactCategories.every(Number.isFinite),
      "native layout should emit every non-proper contact category",
    );
    assert.equal(
      contactCategories.reduce((sum, value) => sum + value, 0),
      canonical.nonProperContacts,
      "aggregate non-proper diagnostics should equal the documented category sum",
    );
    assert.notEqual(layout.engineMetadata?.inheritanceCarrierGrouping, true);
    assert.notEqual(layout.engineMetadata?.intraClusterCarrierGrouping, true);
    assert.equal(
      layout.engineMetadata?.nodeOverlaps,
      0,
      "the emitted direct table set must not contain actual card overlaps",
    );
    assert.equal(layout.routedEdges.length, fixture.edges.length);
    assert.equal(
      layout.engineMetadata?.renderedCarrierRoutes?.length ?? 0,
      fixture.edges.length,
    );
    assert.ok(
      layout.engineMetadata.renderedCarrierRoutes.every((route) =>
        route.memberEdgeIds.length === 1
        && route.memberEdgeIds[0] === route.carrierId),
      "every serialized route must remain its own relationship, never a proxy carrier",
    );

    assert.ok(Number.isFinite(layout.engineMetadata?.edgeNodeIntersections));

    const {
      createDiagramRenderModel,
      measureRenderedVisualConflicts,
    } = require(renderModelModulePath);
    const renderModel = createDiagramRenderModel(
      createBootstrapPayload(fixture, layout),
    );
    const renderedMetrics = measureRenderedVisualConflicts(renderModel);
    assert.equal(
      layout.engineMetadata?.bundleEdgeIntersections,
      renderedMetrics.bundleEdgeIntersections,
      "native and webview must audit the same visible bundle/edge geometry",
    );
    assert.equal(
      layout.engineMetadata?.edgeNodeIntersections,
      renderedMetrics.edgeNodeIntersections,
      "native and webview must audit the same visible table/edge geometry",
    );
    const renderedEdgeIds = new Set(renderModel.edges.map((edge) => edge.edgeId));
    const inheritanceEdges = fixture.edges.filter(
      (edge) => edge.kind === "inheritance",
    );
    const sharedInheritanceEdges = inheritanceEdges.filter(
      (edge) => edge.targetModelId === "test.sharedBase",
    );
    const individualInheritanceEdges = inheritanceEdges.filter(
      (edge) => edge.targetModelId !== "test.sharedBase",
    );

    assert.equal(inheritanceEdges.length, 46);
    assert.equal(sharedInheritanceEdges.length, 6);
    for (const edge of individualInheritanceEdges) {
      assert.ok(
        renderedEdgeIds.has(edge.id),
        `single inheritance edge ${edge.id} should remain individually rendered`,
      );
    }
    for (const edge of sharedInheritanceEdges) {
      assert.ok(
        renderedEdgeIds.has(edge.id),
        `shared inheritance edge ${edge.id} must remain until branch connectors exist`,
      );
    }
    assert.ok(!renderedEdgeIds.has("inheritance-carrier:test.sharedBase"));
    assert.ok(renderModel.leafBundles.length > 0);
    assert.equal(Object.hasOwn(renderModel, "detailEdges"), false);
    assert.equal(
      renderModel.edges.length,
      fixture.edges.length,
      "native metrics and the canvas must expose the same complete edge set",
    );
    assert.ok(
      layout.routedEdges.every((edge) => edge.points.length === 2),
      "every optimized relationship must remain one straight segment",
    );
    const renderedEdgeById = new Map(
      renderModel.edges.map((edge) => [edge.edgeId, edge]),
    );
    const scoredRouteByEdgeId = new Map(
      layout.engineMetadata.renderedCarrierRoutes.map((route) => [route.carrierId, route]),
    );
    for (const routedEdge of layout.routedEdges) {
      const renderedEdge = renderedEdgeById.get(routedEdge.edgeId);
      assert.ok(renderedEdge, `missing direct route ${routedEdge.edgeId}`);
      assert.equal(routedEdge.points.length, 2);
      const scoredRoute = scoredRouteByEdgeId.get(routedEdge.edgeId);
      assert.ok(scoredRoute, `missing scored direct route ${routedEdge.edgeId}`);
      assert.equal(scoredRoute.points.length, 2);
      const nativePoints = scoredRoute.points
        .map((point) => `${point.x},${point.y}`)
        .join(" ");
      const reversedNativePoints = [...scoredRoute.points]
        .reverse()
        .map((point) => `${point.x},${point.y}`)
        .join(" ");
      assert.ok(
        renderedEdge.points === nativePoints
        || renderedEdge.points === reversedNativePoints,
        `direct route ${routedEdge.edgeId} should use native-scored geometry`,
      );
    }
  } finally {
    await fs.rm(directory, { force: true, recursive: true });
  }
});

test("native final carrier pass clears table penetrations without bends or bbox growth", {
  skip: !binaryAvailable,
}, async () => {
  const directory = await fs.mkdtemp(
    path.join(os.tmpdir(), "django-erd-carrier-node-clear-"),
  );
  const nodesPath = path.join(directory, "nodes.tsv");
  const edgesPath = path.join(directory, "edges.tsv");
  const positionsPath = path.join(directory, "positions.tsv");
  const centers = new Map([
    ["test.source", [100, 100]],
    ["test.blocker", [500, 100]],
    ["test.target", [900, 100]],
    ["test.lowerLeft", [100, 600]],
    ["test.lowerMid", [500, 600]],
    ["test.lowerRight", [900, 600]],
    ["test.upperLeft", [100, -400]],
    ["test.upperRight", [900, -400]],
  ]);
  const nodes = [...centers.keys()];
  const edges = [
    ["edge:penetrating", "test.source", "test.target"],
    ["edge:blocker-lower", "test.blocker", "test.lowerMid"],
    ["edge:blocker-upper", "test.blocker", "test.upperRight"],
    ["edge:source-lower", "test.source", "test.lowerLeft"],
    ["edge:lower-left-mid", "test.lowerLeft", "test.lowerMid"],
    ["edge:lower-mid-right", "test.lowerMid", "test.lowerRight"],
    ["edge:target-lower", "test.target", "test.lowerRight"],
    ["edge:source-upper", "test.source", "test.upperLeft"],
    ["edge:upper", "test.upperLeft", "test.upperRight"],
    ["edge:target-upper", "test.target", "test.upperRight"],
  ];

  await fs.writeFile(
    nodesPath,
    `${nodes.map((modelId, index) =>
      `${modelId}\t120\t80\t0\t0\tgroup${index % 2}`
    ).join("\n")}\n`,
    "utf8",
  );
  await fs.writeFile(
    edgesPath,
    `${edges.map(([id, source, target]) =>
      [id, source, target, "association", "declared"].join("\t")
    ).join("\n")}\n`,
    "utf8",
  );
  await fs.writeFile(
    positionsPath,
    `${nodes.map((modelId) => {
      const [x, y] = centers.get(modelId);
      return `${modelId}\t${x}\t${y}`;
    }).join("\n")}\n`,
    "utf8",
  );

  const commonEnv = {
    ...process.env,
    DJERD_ATTACH_ISOLATED_BY_NAME_FINAL: "0",
    DJERD_BBOX_AXIS_SCALE_FINAL: "0",
    DJERD_BUNDLE_BOX_RELOCATE_FINAL: "0",
    DJERD_CANONICAL_CROSSING_CACHE: "0",
    DJERD_CG_SKIP_POSITIONING: "1",
    DJERD_DENSITY_BALANCE_FINAL: "0",
    DJERD_DENSITY_PACK_FINAL: "0",
    DJERD_DIAGONAL_RETOUCH: "0",
    DJERD_FACE_RASTER: "0",
    DJERD_FACE_UNTANGLE: "0",
    DJERD_HOT_REGION_SA: "0",
    DJERD_HUB_CARRIER_CROSS_FINAL: "0",
    DJERD_INHERITANCE_CARRIER_FINAL: "0",
    DJERD_INTRA_CLUSTER_CARRIER_FINAL: "0",
    DJERD_ISOLATED_BBOX_COMPACT_FINAL: "0",
    DJERD_LEAF_PASSES: "0",
    DJERD_LEAF_PASSES_2: "0",
    DJERD_MULTISTART_RUNS: "1",
    DJERD_NODE_EDGE_RELIEF_FINAL: "0",
    DJERD_NODE_OVERLAP_CLEAR_FINAL: "0",
    DJERD_NODE_PAIR_RETOUCH: "0",
    DJERD_NODE_SPACING_CLEAR_FINAL: "0",
    DJERD_NO_BUNDLE_CLEAR: "1",
    DJERD_NO_KNOT_MIN: "1",
    DJERD_NO_LEAF_UNTANGLE: "1",
    DJERD_NO_PD_KNOT: "1",
    DJERD_PERIPHERY_REROUTE: "0",
    DJERD_RENDERED_CARRIER_BUNDLE_EDGE_TARGET: "0",
    DJERD_RENDERED_CARRIER_EDGE_NODE_TARGET: "0",
    DJERD_RENDERED_CARRIER_GEOMETRY_OPT_FINAL: "1",
    DJERD_RENDERED_CARRIER_METRICS_FINAL: "1",
    DJERD_RENDERED_CARRIER_VISUAL_TARGET: "100",
    DJERD_RENDERED_NODE_CLEARANCE_FINAL: "1",
    DJERD_RENDERED_STRAIGHT_PORT_OPT_FINAL: "1",
    DJERD_RIGID_ATTACH_ISOLATED_FINAL: "0",
    DJERD_RIGID_COMPACT_BBOX_FINAL: "0",
    DJERD_RIGID_NODE_EDGE_RELIEF_FINAL: "0",
    DJERD_SIDECAR_BBOX_COMPACT_FINAL: "0",
    DJERD_SKIP_CG_OPT: "1",
    DJERD_STRESS_POST_PASS_ITERS: "0",
    DJERD_STUCK_LEAF_2D: "0",
    DJERD_VISUAL_KNOT: "0",
    DJERD_XINGS_DETOUR: "0",
    DJERD_XINGS_DETOUR_FINAL: "0",
  };
  const args = [
    "layout",
    "--mode", "fmmm",
    "--nodes-file", nodesPath,
    "--edges-file", edgesPath,
    "--edge-routing", "straight",
    "--cluster-graph", "0",
    "--positions-tsv", positionsPath,
    "--rigid-positions", "1",
  ];

  try {
    const baseRun = await execFileAsync(binaryPath, args, {
      cwd: repoRoot,
      env: {
        ...commonEnv,
        DJERD_RENDERED_CARRIER_NODE_CLEAR_FINAL: "0",
        DJERD_RENDERED_CARRIER_NODE_TARGET_FINAL: "0",
      },
      maxBuffer: 16 * 1024 * 1024,
      timeout: 30_000,
    });
    const base = JSON.parse(baseRun.stdout);
    assert.ok(
      base.engineMetadata.edgeNodeIntersections > 0,
      "fixture must begin with a visible carrier crossing a table body",
    );
    const baseArea = nodeBoundingBoxArea(base.nodes);

    const clearRun = await execFileAsync(binaryPath, args, {
      cwd: repoRoot,
      env: {
        ...commonEnv,
        DJERD_RENDERED_CARRIER_NODE_CLEAR_DIRECTIONS: "24",
        DJERD_RENDERED_CARRIER_NODE_CLEAR_FINAL: "1",
        DJERD_RENDERED_CARRIER_NODE_CLEAR_MAX_SHIFT: "1200",
        DJERD_RENDERED_CARRIER_NODE_CLEAR_ROUNDS: "6",
        DJERD_RENDERED_CARRIER_NODE_TARGET_FINAL: "1",
      },
      maxBuffer: 16 * 1024 * 1024,
      timeout: 30_000,
    });
    const cleared = JSON.parse(clearRun.stdout);
    assert.equal(cleared.engineMetadata.edgeNodeIntersections, 0);
    assert.ok(cleared.engineMetadata.visualCrossings <= 100);
    assert.equal(cleared.engineMetadata.nodeOverlaps, 0);
    assert.equal(cleared.engineMetadata.bundleNodeOverlaps, 0);
    assert.equal(cleared.engineMetadata.nodeSpacingOverlaps, 0);
    assert.ok(
      cleared.engineMetadata.nodeClearanceMin
        >= cleared.engineMetadata.nodeClearanceTarget,
    );
    assert.equal(cleared.engineMetadata.edgeBendTotal, 0);
    assert.ok(cleared.routedEdges.every((edge) => edge.points.length === 2));
    assert.ok(
      nodeBoundingBoxArea(cleared.nodes) <= baseArea + 1e-6,
      "local blocker relocation must stay inside the settled node bbox",
    );
    assert.match(clearRun.stderr, /\[rendered-carrier-node-clear-final\]/);
  } finally {
    await fs.rm(directory, { force: true, recursive: true });
  }
});

test("straight boundary-port optimization never regresses the exact direct scene", {
  skip: !binaryAvailable,
}, async () => {
  const directory = await fs.mkdtemp(
    path.join(os.tmpdir(), "django-erd-straight-port-monotonic-"),
  );
  const nodesPath = path.join(directory, "nodes.tsv");
  const edgesPath = path.join(directory, "edges.tsv");
  const positionsPath = path.join(directory, "positions.tsv");
  const nodes = [
    ["test.source", 100, 100],
    ["test.blocker", 500, 149],
    ["test.target", 900, 100],
  ];

  await fs.writeFile(
    nodesPath,
    `${nodes.map(([modelId]) =>
      `${modelId}\t120\t80\t0\t0\ttest`
    ).join("\n")}\n`,
    "utf8",
  );
  await fs.writeFile(
    edgesPath,
    "edge:source-target\ttest.source\ttest.target\tforeign_key\tdeclared\n",
    "utf8",
  );
  await fs.writeFile(
    positionsPath,
    `${nodes.map(([modelId, x, y]) => `${modelId}\t${x}\t${y}`).join("\n")}\n`,
    "utf8",
  );

  const args = [
    "layout",
    "--mode", "fmmm",
    "--nodes-file", nodesPath,
    "--edges-file", edgesPath,
    "--edge-routing", "straight",
    "--cluster-graph", "0",
    "--positions-tsv", positionsPath,
    "--rigid-positions", "1",
  ];
  const commonEnv = {
    ...process.env,
    DJERD_CANONICAL_CROSSING_CACHE: "0",
    DJERD_CG_SKIP_POSITIONING: "1",
    DJERD_MULTISTART_RUNS: "1",
    DJERD_NO_CARRIER_CROSS: "1",
    DJERD_NO_KNOT_MIN: "1",
    DJERD_RENDERED_CARRIER_METRICS_FINAL: "1",
    DJERD_RENDERED_CARRIER_NODE_CLEAR_FINAL: "0",
    DJERD_RENDERED_CARRIER_NODE_TARGET_FINAL: "0",
    DJERD_RENDERED_NODE_CLEARANCE_FINAL: "0",
    DJERD_SKIP_CG_OPT: "1",
  };

  try {
    const baseRun = await execFileAsync(binaryPath, args, {
      cwd: repoRoot,
      env: {
        ...commonEnv,
        DJERD_RENDERED_CARRIER_GEOMETRY_OPT_FINAL: "0",
        DJERD_RENDERED_STRAIGHT_PORT_OPT_FINAL: "0",
      },
      maxBuffer: 8 * 1024 * 1024,
      timeout: 15_000,
    });
    const base = JSON.parse(baseRun.stdout);
    assert.equal(base.engineMetadata.edgeNodeIntersections, 1);

    const optimizedRun = await execFileAsync(binaryPath, args, {
      cwd: repoRoot,
      env: {
        ...commonEnv,
        DJERD_RENDERED_CARRIER_GEOMETRY_OPT_BUDGET_MS: "1000",
        DJERD_RENDERED_CARRIER_GEOMETRY_OPT_FINAL: "1",
        DJERD_RENDERED_STRAIGHT_PORT_OPT_FINAL: "1",
        DJERD_RENDERED_STRAIGHT_PORT_SAMPLES_PER_SIDE: "8",
      },
      maxBuffer: 8 * 1024 * 1024,
      timeout: 15_000,
    });
    const optimized = JSON.parse(optimizedRun.stdout);
    assert.equal(optimized.engineMetadata.edgeNodeIntersections, 0);
    assert.ok(
      optimized.engineMetadata.visualCrossings
        <= base.engineMetadata.visualCrossings,
    );
    assert.equal(optimized.engineMetadata.nodeOverlaps, 0);
    assert.equal(optimized.routedEdges.length, 1);
    assert.equal(optimized.routedEdges[0].points.length, 2);
    assert.equal(
      optimized.engineMetadata.renderedCarrierRoutes[0].points.length,
      2,
    );
  } finally {
    await fs.rm(directory, { force: true, recursive: true });
  }
});

test("removed adaptive settings cannot create a zoom-only carrier set", {
  skip: !binaryAvailable || !renderModelAvailable,
}, async () => {
  const fixture = createDenseBundleFreeFixture();
  const directory = await fs.mkdtemp(
    path.join(os.tmpdir(), "django-erd-carrier-adaptive-"),
  );
  const nodesPath = path.join(directory, "nodes.tsv");
  const edgesPath = path.join(directory, "edges.tsv");
  await fs.writeFile(
    nodesPath,
    `${fixture.nodes.map((modelId, index) => {
      const side = index < 8 ? 0 : 1;
      const row = index % 8;
      return `${modelId}\t120\t80\t${side * 1200}\t${row * 180}\ttest`;
    }).join("\n")}\n`,
    "utf8",
  );
  await fs.writeFile(
    edgesPath,
    `${fixture.edges.map((edge) => [
      edge.id,
      edge.sourceModelId,
      edge.targetModelId,
      edge.kind,
      edge.provenance,
    ].join("\t")).join("\n")}\n`,
    "utf8",
  );

  try {
    const { stdout } = await execFileAsync(binaryPath, [
      "layout",
      "--mode", "fmmm",
      "--nodes-file", nodesPath,
      "--edges-file", edgesPath,
      "--edge-routing", "straight",
      "--cluster-graph", "1",
    ], {
      cwd: repoRoot,
      env: {
        ...process.env,
        DJERD_ADAPTIVE_CARRIER_TARGET_FINAL: "1",
        DJERD_CANONICAL_CROSSING_CACHE: "0",
        DJERD_CG_SKIP_POSITIONING: "1",
        DJERD_HUB_CARRIER_CROSS_FINAL: "0",
        DJERD_INHERITANCE_CARRIER_FINAL: "0",
        DJERD_INTRA_CLUSTER_CARRIER_FINAL: "0",
        DJERD_MULTISTART_RUNS: "1",
        DJERD_RENDERED_CARRIER_METRICS_FINAL: "1",
        DJERD_SKIP_CG_OPT: "1",
      },
      maxBuffer: 32 * 1024 * 1024,
      timeout: 30_000,
    });
    const layout = JSON.parse(stdout);
    assert.equal(layout.engineMetadata?.leafBundles?.length ?? 0, 0);
    assert.equal(layout.engineMetadata?.adaptiveCarrierTarget, undefined);
    assert.equal(layout.engineMetadata?.adaptiveCarrierGrid, undefined);
    assert.equal(layout.routedEdges.length, fixture.edges.length);
    assert.ok(layout.engineMetadata?.visualCrossings > 1);

    const { createDiagramRenderModel } = require(renderModelModulePath);
    const renderModel = createDiagramRenderModel(
      createBootstrapPayload(fixture, layout),
    );
    assert.equal(
      renderModel.edges.length,
      fixture.edges.length,
      "every routed relationship should be present at every zoom",
    );
    assert.equal(Object.hasOwn(renderModel, "detailEdges"), false);
    assert.deepEqual(
      new Set(renderModel.edges.map((edge) => edge.edgeId)),
      new Set(fixture.edges.map((edge) => edge.id)),
    );
  } finally {
    await fs.rm(directory, { force: true, recursive: true });
  }
});

function createCarrierFixture() {
  const nodes = [];
  const edges = [];
  const addNode = (name) => {
    const modelId = `test.${name}`;
    if (!nodes.includes(modelId)) {
      nodes.push(modelId);
    }
    return modelId;
  };
  const addEdge = (source, target, kind = "foreign_key") => {
    edges.push({
      id: `edge-${edges.length}`,
      kind,
      provenance: "declared",
      sourceModelId: addNode(source),
      targetModelId: addNode(target),
    });
  };

  for (const hub of ["hubA", "hubB", "hubC"]) {
    addNode(hub);
  }
  addEdge("hubA", "hubB");
  addEdge("hubB", "hubC");
  addEdge("hubC", "hubA");

  for (const [hub, prefix] of [["hubA", "a"], ["hubB", "b"], ["hubC", "c"]]) {
    for (let index = 0; index < 10; index += 1) {
      addEdge(`${prefix}${index}`, hub);
      addEdge(`${prefix}${index}`, `base${prefix}${index}`, "inheritance");
    }
  }
  for (let index = 0; index < 10; index += 1) {
    addEdge(`coreA${index}`, "hubA");
    addEdge(`coreA${index}`, `coreB${index}`, "inheritance");
    addEdge(`coreB${index}`, "hubB");
  }
  for (let index = 0; index < 6; index += 1) {
    addEdge(`sharedChild${index}`, "sharedBase", "inheritance");
    // Keep these children out of the degree-1 leaf bundle so this fixture
    // specifically exercises the shared inheritance carrier.
    addEdge(`sharedChild${index}`, "hubC");
  }

  return { edges, nodes };
}

function createDenseBundleFreeFixture() {
  const left = Array.from({ length: 8 }, (_, index) => `test.left${index}`);
  const right = Array.from({ length: 8 }, (_, index) => `test.right${index}`);
  const edges = [];
  for (const sourceModelId of left) {
    for (const targetModelId of right) {
      edges.push({
        id: `edge-${edges.length}`,
        kind: "foreign_key",
        provenance: "declared",
        sourceModelId,
        targetModelId,
      });
    }
  }
  return { edges, nodes: [...left, ...right] };
}

function createBootstrapPayload(fixture, layout) {
  return {
    analyzer: {
      diagnostics: [],
      models: fixture.nodes.map((modelId) => ({
        declaredBaseClasses: [],
        fields: [],
        identity: {
          appLabel: "test",
          id: modelId,
          modelName: modelId.slice("test.".length),
        },
        methods: [],
        properties: [],
      })),
      summary: {},
    },
    contractVersion: "test",
    graph: {
      diagnostics: [],
      methodAssociations: [],
      nodes: fixture.nodes.map((modelId) => ({
        appLabel: "test",
        modelId,
        modelName: modelId.slice("test.".length),
      })),
      structuralEdges: fixture.edges,
    },
    layout,
    layoutExecution: {
      appliedMode: "fmmm",
      engine: "ogdf",
      requestedMode: "fmmm",
      status: "applied",
    },
    view: {
      layoutMode: "fmmm",
      tableOptions: [],
    },
  };
}

function nodeBoundingBoxArea(nodes) {
  const minX = Math.min(...nodes.map((node) => node.position.x));
  const minY = Math.min(...nodes.map((node) => node.position.y));
  const maxX = Math.max(
    ...nodes.map((node) => node.position.x + node.size.width),
  );
  const maxY = Math.max(
    ...nodes.map((node) => node.position.y + node.size.height),
  );
  return (maxX - minX) * (maxY - minY);
}

async function pathExists(filePath) {
  try {
    await fs.access(filePath);
    return true;
  } catch {
    return false;
  }
}
