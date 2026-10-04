import assert from "node:assert/strict";
import { createRequire } from "node:module";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const __dirname = path.dirname(fileURLToPath(import.meta.url));
const { createDiagramRenderModel, getRenderedConnectionTables } = require(path.resolve(
  __dirname,
  "../../out/webview/state/createDiagramRenderModel.js",
));

test("bundle members retain native tables and share one boundary connection per external root", () => {
  const payload = createSharedRootPayload();
  const renderModel = createDiagramRenderModel(payload);

  assert.deepEqual(renderModel.leafBundles[0]?.sharedRootModelIds, [
    "test.Parent",
    "test.Extra",
  ]);
  assert.equal(renderModel.edges.length, 2);
  assert.ok(renderModel.edges.every(edge => edge.memberEdgeIds.length === 2));
  assert.deepEqual(
    renderModel.leafCardOverview.individualEdges.map((edge) => [edge.sourceModelId, edge.targetModelId]).sort(),
    [
      ["test.LeafA", "test.Extra"],
      ["test.LeafA", "test.Parent"],
      ["test.LeafB", "test.Extra"],
      ["test.LeafB", "test.Parent"],
    ],
  );
  assert.ok(
    renderModel.tables.every((table) =>
      !String(table.modelId).startsWith("__leafbundle.")),
  );
  assert.ok(renderModel.edges.every((edge) =>
    !String(edge.sourceModelId).startsWith("__leafbundle.")
    && !String(edge.targetModelId).startsWith("__leafbundle.")));
  assert.deepEqual(
    renderModel.tables
      .filter((table) => table.modelId === "test.LeafA" || table.modelId === "test.LeafB")
      .map((table) => [table.modelId, table.position, table.size]),
    [
      ["test.LeafA", { x: 480, y: 0 }, { height: 120, width: 180 }],
      ["test.LeafB", { x: 720, y: 320 }, { height: 120, width: 180 }],
    ],
  );
  const tableById = new Map(getRenderedConnectionTables(renderModel).map((table) => [table.modelId, table]));
  for (const edge of renderModel.edges) {
    const [start, end] = edge.points.split(/\s+/).map((value) => {
      const [x, y] = value.split(",").map(Number);
      return { x, y };
    });
    assert.ok(pointTouchesTable(start, tableById.get(edge.leafCardEndpointIds?.[0] || edge.sourceModelId)));
    assert.ok(pointTouchesTable(end, tableById.get(edge.leafCardEndpointIds?.[1] || edge.targetModelId)));
  }
});

function pointTouchesTable(point, table) {
  return point.x >= table.position.x - 0.01
    && point.x <= table.position.x + table.size.width + 0.01
    && point.y >= table.position.y - 0.01
    && point.y <= table.position.y + table.size.height + 0.01;
}

function createSharedRootPayload() {
  const modelIds = [
    "test.Parent",
    "test.Extra",
    "test.LeafA",
    "test.LeafB",
  ];
  const structuralEdges = [
    edge("edge-leaf-a-parent", "test.LeafA", "test.Parent"),
    edge("edge-leaf-b-parent", "test.LeafB", "test.Parent"),
    edge("edge-leaf-a-extra", "test.LeafA", "test.Extra"),
    edge("edge-leaf-b-extra", "test.LeafB", "test.Extra"),
  ];

  return {
    analyzer: {
      diagnostics: [],
      models: modelIds.map((modelId) => ({
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
      nodes: modelIds.map((modelId) => ({
        appLabel: "test",
        modelId,
        modelName: modelId.slice("test.".length),
      })),
      structuralEdges,
    },
    layout: {
      crossings: [],
      engineMetadata: {
        leafBundles: [{
          anchor: { x: 360, y: 220 },
          bbox: { height: 180, width: 440, x: 140, y: 130 },
          leafModelIds: ["test.LeafA", "test.LeafB"],
          parentModelId: "test.Parent",
          sharedRootModelIds: ["test.Parent", "test.Extra"],
        }],
      },
      mode: "fmmm",
      nodes: modelIds.map((modelId, index) => ({
        modelId,
        position: { x: index * 240, y: index % 2 === 0 ? 0 : 320 },
        size: { height: 120, width: 180 },
      })),
      routedEdges: structuralEdges.map((structuralEdge, index) => ({
        crossingIds: [],
        edgeId: structuralEdge.id,
        points: [
          { x: 40 + index * 10, y: 40 },
          { x: 400 + index * 10, y: 300 },
        ],
      })),
    },
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

function edge(id, sourceModelId, targetModelId) {
  return {
    id,
    kind: "foreign_key",
    provenance: "declared",
    sourceModelId,
    targetModelId,
  };
}
