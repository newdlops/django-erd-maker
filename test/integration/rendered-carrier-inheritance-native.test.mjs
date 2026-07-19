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

test("native and webview share semantic bundling without zoom-only detail", {
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
    const { stdout, stderr } = await execFileAsync(binaryPath, [
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
        DJERD_HUB_CARRIER_CROSS_FINAL: "1",
        DJERD_HUB_CARRIER_CROSS_FINAL_THRESHOLD: "2",
        DJERD_INHERITANCE_CARRIER_FINAL: "1",
        DJERD_INTRA_CLUSTER_CARRIER_FINAL: "1",
        DJERD_MULTISTART_RUNS: "1",
        DJERD_RENDERED_CARRIER_GEOMETRY_OPT_FINAL: "1",
        DJERD_RENDERED_CARRIER_METRICS_FINAL: "1",
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
    assert.equal(layout.engineMetadata?.inheritanceCarrierGrouping, true);
    assert.equal(layout.engineMetadata?.intraClusterCarrierGrouping, true);
    assert.equal(
      layout.engineMetadata?.nodeSpacingOverlaps,
      0,
      "the emitted table set should satisfy the rendered clearance gate",
    );
    assert.ok(
      layout.engineMetadata?.nodeClearanceMin
        >= layout.engineMetadata?.nodeClearanceTarget,
      "the measured minimum table clearance should meet its declared target",
    );
    assert.equal(layout.routedEdges.length, fixture.edges.length);
    assert.ok(
      layout.engineMetadata?.renderedCarrierRoutes?.length > 0,
      "native layout should serialize optimized straight carrier geometry",
    );

    const metricMatches = [...stderr.matchAll(
      /\[rendered-carrier-metrics-final\].*visibleEdges=(\d+).*routeSegments=(\d+)/g,
    )];
    const finalMetric = metricMatches.at(-1);
    assert.ok(finalMetric, "native rendered-carrier metrics should be emitted");

    const { createDiagramRenderModel } = require(renderModelModulePath);
    const renderModel = createDiagramRenderModel(
      createBootstrapPayload(fixture, layout),
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
        !renderedEdgeIds.has(edge.id),
        `shared inheritance edge ${edge.id} should use its parent carrier`,
      );
    }
    assert.ok(renderedEdgeIds.has("inheritance-carrier:test.sharedBase"));
    assert.ok(renderModel.leafBundles.length > 0);
    assert.equal(Object.hasOwn(renderModel, "detailEdges"), false);
    assert.equal(
      Number(finalMetric[1]),
      renderModel.edges.length,
      "every scored semantic carrier should be visible without zoom",
    );
    assert.ok(
      Number(finalMetric[2]) >= renderModel.edges.length,
      "native route-segment count should cover every visible carrier",
    );
    const renderedEdgeById = new Map(
      renderModel.edges.map((edge) => [edge.edgeId, edge]),
    );
    for (const carrierRoute of layout.engineMetadata.renderedCarrierRoutes) {
      const webviewCarrierId = carrierRoute.carrierId.startsWith("H|")
        ? `hub-carrier:${carrierRoute.carrierId.slice(2)}`
        : carrierRoute.carrierId.startsWith("I|")
          ? `inheritance-carrier:${carrierRoute.carrierId.slice(2)}`
          : carrierRoute.carrierId.startsWith("Cself|")
            ? `intra-cluster-carrier:${carrierRoute.carrierId.slice("Cself|".length)}`
            : carrierRoute.carrierId.startsWith("B")
              ? carrierRoute.memberEdgeIds.find((edgeId) => renderedEdgeById.has(edgeId))
              : carrierRoute.carrierId;
      const renderedEdge = renderedEdgeById.get(webviewCarrierId);
      assert.ok(renderedEdge, `missing serialized carrier ${webviewCarrierId}`);
      const nativePoints = carrierRoute.points
        .map((point) => `${point.x},${point.y}`)
        .join(" ");
      const reversedNativePoints = [...carrierRoute.points]
        .reverse()
        .map((point) => `${point.x},${point.y}`)
        .join(" ");
      assert.ok(
        renderedEdge.points === nativePoints
        || renderedEdge.points === reversedNativePoints,
        `carrier ${webviewCarrierId} should use native-scored geometry`,
      );
      assert.ok(carrierRoute.memberEdgeIds.length >= 1);
      assert.equal(
        carrierRoute.points.length,
        2,
        `carrier ${webviewCarrierId} should remain a straight line`,
      );
    }
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

async function pathExists(filePath) {
  try {
    await fs.access(filePath);
    return true;
  } catch {
    return false;
  }
}
