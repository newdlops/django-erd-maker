import assert from "node:assert/strict";
import { createRequire } from "node:module";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const {
  createDiagramRenderModel,
  measureRenderedEdgeNodeIntersections,
} = require(path.resolve(
  __dirname,
  "../../out/webview/state/createDiagramRenderModel.js",
));

test("large diagrams never collapse distinct FK relationships into fabricated tree paths", () => {
  const payload = createLargeCarrierPayload();
  const renderModel = createDiagramRenderModel(payload);
  const expectedEdgeIds = new Set(
    payload.layout.routedEdges.map((edge) => edge.edgeId),
  );
  const representedEdgeIds = new Set(
    renderModel.edges.flatMap((edge) => edge.memberEdgeIds ?? [edge.edgeId]),
  );

  assert.equal(renderModel.semanticCarriers?.active, true);
  assert.equal(renderModel.semanticCarriers?.relationships, expectedEdgeIds.size);
  assert.deepEqual(renderModel.semanticCarriers?.missingRelationships, []);
  assert.deepEqual(renderModel.semanticCarriers?.disconnectedRelationships, []);
  assert.equal(renderModel.semanticCarriers?.bundledRelationships, 0);
  assert.deepEqual(representedEdgeIds, expectedEdgeIds);
  assert.equal(renderModel.edges.length, payload.layout.routedEdges.length);
  assert.ok(renderModel.edges.every((edge) =>
    edge.points.trim().split(/\s+/).length === 2));
  assert.ok(
    measureRenderedEdgeNodeIntersections(renderModel).count > 0,
    "the audit must expose real straight-line node penetrations instead of hiding them behind an unrelated-node tree",
  );
});

test("relationships between models inside one leaf bundle keep their real endpoints", () => {
  const payload = createLargeCarrierPayload();
  payload.layout.engineMetadata.leafBundles = [{
    anchor: { x: 360, y: 220 },
    bbox: { height: 180, width: 440, x: 140, y: 130 },
    leafModelIds: ["carrier.Model000", "carrier.Model001"],
    parentModelId: "carrier.Model500",
    sharedRootModelIds: ["carrier.Model500"],
  }];

  const renderModel = createDiagramRenderModel(payload);
  const relationship = renderModel.edges.find((edge) =>
    (edge.memberEdgeIds ?? [edge.edgeId]).includes("edge-chain-0")
  );

  assert.ok(relationship, "the internal FK must remain represented");
  assert.equal(relationship.sourceModelId, "carrier.Model000");
  assert.equal(relationship.targetModelId, "carrier.Model001");
  assert.equal(relationship.points.trim().split(/\s+/).length, 2);
  assert.deepEqual(renderModel.semanticCarriers?.missingRelationships, []);
  assert.deepEqual(renderModel.semanticCarriers?.disconnectedRelationships, []);
});

function createLargeCarrierPayload() {
  const modelCount = 501;
  const modelIds = Array.from(
    { length: modelCount },
    (_, index) => `carrier.Model${String(index).padStart(3, "0")}`,
  );
  const structuralEdges = [];
  for (let index = 0; index + 1 < modelCount; index += 1) {
    structuralEdges.push(edge(`edge-chain-${index}`, modelIds[index], modelIds[index + 1]));
  }
  for (let index = 0; index + 2 < modelCount; index += 1) {
    structuralEdges.push(edge(`edge-chord-${index}`, modelIds[index], modelIds[index + 2]));
  }

  return {
    analyzer: {
      diagnostics: [],
      models: modelIds.map((modelId) => ({
        declaredBaseClasses: [],
        fields: [],
        identity: {
          appLabel: "carrier",
          id: modelId,
          modelName: modelId.slice("carrier.".length),
        },
        methods: [],
        properties: [],
      })),
      summary: {},
    },
    contractVersion: "semantic-carrier-test",
    graph: {
      diagnostics: [],
      methodAssociations: [],
      nodes: modelIds.map((modelId) => ({
        appLabel: "carrier",
        modelId,
        modelName: modelId.slice("carrier.".length),
      })),
      structuralEdges,
    },
    layout: {
      crossings: [],
      engineMetadata: {},
      mode: "fmmm",
      nodes: modelIds.map((modelId, index) => ({
        clusterId: "carrier-cluster",
        modelId,
        position: {
          x: (index % 25) * 360,
          y: Math.floor(index / 25) * 220,
        },
        size: { height: 74, width: 236 },
      })),
      routedEdges: structuralEdges.map((structuralEdge) => ({
        crossingIds: [],
        edgeId: structuralEdge.id,
        points: [],
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
