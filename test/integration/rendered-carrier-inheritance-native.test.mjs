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

test("native rendered-carrier topology preserves inheritance like the webview", {
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
        DJERD_MULTISTART_RUNS: "1",
        DJERD_RENDERED_CARRIER_METRICS_FINAL: "1",
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

    assert.equal(inheritanceEdges.length, 40);
    for (const edge of inheritanceEdges) {
      assert.ok(
        renderedEdgeIds.has(edge.id),
        `inheritance edge ${edge.id} should remain individually rendered`,
      );
    }
    assert.equal(
      Number(finalMetric[1]),
      renderModel.edges.length,
      "native visible carrier paths should match webview structural edges",
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

  return { edges, nodes };
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
