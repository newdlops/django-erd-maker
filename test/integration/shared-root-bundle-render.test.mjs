import assert from "node:assert/strict";
import { createRequire } from "node:module";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const __dirname = path.dirname(fileURLToPath(import.meta.url));
const { createDiagramRenderModel } = require(path.resolve(
  __dirname,
  "../../out/webview/state/createDiagramRenderModel.js",
));

test("packed leaf bundles retain carriers for every shared root", () => {
  const payload = createSharedRootPayload();
  const renderModel = createDiagramRenderModel(payload);

  assert.deepEqual(renderModel.leafBundles[0]?.sharedRootModelIds, [
    "test.Parent",
    "test.Extra",
  ]);
  assert.equal(renderModel.edges.length, 2);
  assert.deepEqual(
    renderModel.edges.map((edge) => edge.targetModelId).sort(),
    ["test.Extra", "test.Parent"],
  );
  assert.ok(
    renderModel.edges.every((edge) =>
      String(edge.sourceModelId).startsWith("__leafbundle.")
    ),
  );
});

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
