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
} = require(path.resolve(
  __dirname,
  "../../out/webview/state/createDiagramRenderModel.js",
));

test("large diagrams keep one direct edge per relationship while leafBundles group nodes", () => {
  const payload = createLargeCarrierPayload();
  const renderModel = createDiagramRenderModel(payload);
  const expectedEdgeIds = new Set(
    payload.graph.structuralEdges.map((edge) => edge.id),
  );
  const expectedNonSelfEdgeIds = new Set(
    payload.graph.structuralEdges
      .filter((edge) => edge.sourceModelId !== edge.targetModelId)
      .map((edge) => edge.id),
  );
  const representedEdgeIds = new Set(
    renderModel.edges.flatMap((edge) => edge.memberEdgeIds ?? [edge.edgeId]),
  );

  assert.equal(renderModel.semanticCarriers?.active, false);
  assert.equal(renderModel.semanticCarriers?.relationships, expectedEdgeIds.size);
  assert.deepEqual(renderModel.semanticCarriers?.missingRelationships, []);
  assert.deepEqual(renderModel.semanticCarriers?.disconnectedRelationships, []);
  assert.equal(renderModel.semanticCarriers?.bundleGroups, 0);
  assert.equal(renderModel.semanticCarriers?.bundledRelationships, 0);
  assert.equal(renderModel.semanticCarriers?.selfRelationships, 1);
  assert.deepEqual(representedEdgeIds, expectedNonSelfEdgeIds);
  assert.ok(renderModel.tables.every((table) =>
    !String(table.modelId).startsWith("__relationbundle.")));
  assert.ok(renderModel.edges.every((edge) => edge.carrierRole === "direct"));
  assert.ok(renderModel.edges.every((edge) =>
    edge.points.trim().split(/\s+/).length === 2));
  assert.equal(renderModel.edges.length, expectedNonSelfEdgeIds.size);
  assert.equal(
    renderModel.tables.find((table) => table.modelId === "carrier.Model050")
      ?.selfRelationshipCount,
    1,
  );
  const selfCarrier = renderModel.edges.find((edge) =>
    (edge.memberEdgeIds ?? []).includes("edge-self-0")
  );
  assert.equal(selfCarrier, undefined, "self relations belong to their real model card");
  const structuralEdgeById = new Map(
    payload.graph.structuralEdges.map((edge) => [edge.id, edge]),
  );
  const realModelIds = new Set(payload.graph.nodes.map((node) => node.modelId));
  for (const directEdge of renderModel.edges) {
    assert.equal(directEdge.memberEdgeIds.length, 1);
    assert.equal(directEdge.edgeId, directEdge.memberEdgeIds[0]);
    const relationship = structuralEdgeById.get(directEdge.edgeId);
    assert.ok(realModelIds.has(directEdge.sourceModelId));
    assert.ok(realModelIds.has(directEdge.targetModelId));
    assert.equal(directEdge.sourceModelId, relationship.sourceModelId);
    assert.equal(directEdge.targetModelId, relationship.targetModelId);
    assert.deepEqual(
      new Set(directEdge.physicalEndpointModelIds),
      new Set([relationship.sourceModelId, relationship.targetModelId]),
    );
  }
  assertRelationshipPathConnectsRealTables(
    renderModel,
    structuralEdgeById.get("edge-fk-hub-0"),
  );
});

test("relationships between models inside one leaf bundle keep their real endpoints", () => {
  const payload = createLargeCarrierPayload();
  payload.layout.routedEdges.find((route) => route.edgeId === "edge-chain-0").points = [
    { x: 236, y: 37 },
    { x: 360, y: 37 },
  ];
  payload.layout.engineMetadata.leafBundles = [{
    anchor: { x: 360, y: 220 },
    bbox: { height: 180, width: 440, x: 140, y: 130 },
    leafModelIds: ["carrier.Model000", "carrier.Model001"],
    parentModelId: "carrier.Model500",
    sharedRootModelIds: ["carrier.Model500"],
  }];

  const renderModel = createDiagramRenderModel(payload);
  const relationshipCarriers = renderModel.edges.filter((edge) =>
    (edge.memberEdgeIds ?? [edge.edgeId]).includes("edge-chain-0")
  );

  assert.equal(relationshipCarriers.length, 1, "the internal FK stays one direct relationship");
  assert.equal(relationshipCarriers[0].sourceModelId, "carrier.Model000");
  assert.equal(relationshipCarriers[0].targetModelId, "carrier.Model001");
  assert.deepEqual(
    new Set(relationshipCarriers[0].physicalEndpointModelIds),
    new Set(["carrier.Model000", "carrier.Model001"]),
  );
  assert.ok(relationshipCarriers.every((carrier) =>
    carrier.points.trim().split(/\s+/).length === 2));
  assertRelationshipPathConnectsRealTables(
    renderModel,
    payload.graph.structuralEdges.find((edge) => edge.id === "edge-chain-0"),
  );
  assert.deepEqual(renderModel.semanticCarriers?.missingRelationships, []);
  assert.deepEqual(renderModel.semanticCarriers?.disconnectedRelationships, []);
});

test("unrouted reverse members cannot expand the canonical direct edge set", () => {
  const payload = createLargeCarrierPayload();
  payload.layout.engineMetadata.leafBundles = [{
    anchor: { x: 360, y: 220 },
    bbox: { height: 180, width: 440, x: 140, y: 130 },
    leafModelIds: ["carrier.Model000"],
    parentModelId: "carrier.Model500",
    sharedRootModelIds: ["carrier.Model500"],
  }];
  payload.graph.structuralEdges.push({
    id: "edge-derived-reverse-not-routed",
    kind: "foreign_key",
    provenance: "derived_reverse",
    sourceModelId: "carrier.Model500",
    targetModelId: "carrier.Model000",
  });

  const renderModel = createDiagramRenderModel(payload);
  const representedEdgeIds = new Set(renderModel.edges.flatMap((edge) =>
    edge.memberEdgeIds ?? [edge.edgeId]));
  const canonicalNonSelfIds = new Set(payload.layout.routedEdges.map((edge) => edge.edgeId));

  assert.deepEqual(representedEdgeIds, canonicalNonSelfIds);
  assert.equal(renderModel.semanticCarriers?.relationships, canonicalNonSelfIds.size + 1);
  assert.equal(representedEdgeIds.has("edge-derived-reverse-not-routed"), false);
  assert.ok(renderModel.tables.every((table) =>
    !String(table.modelId).startsWith("__leafbundle.")));
});

test("node bundles cannot collapse relationships in a small diagram", () => {
  const payload = createSmallNodeBundlePayload();
  const renderModel = createDiagramRenderModel(payload);
  const edgeById = new Map(renderModel.edges.map((edge) => [edge.edgeId, edge]));

  assert.equal(renderModel.modelCatalogMode, false);
  assert.equal(renderModel.leafBundles.length, 1, "the node group remains available to layout/selection");
  assert.deepEqual(renderModel.bundleLeavesByFakeId, {}, "a node group is not a proxy model");
  assert.equal(renderModel.edges.length, 2);
  for (const relationship of payload.graph.structuralEdges) {
    const rendered = edgeById.get(relationship.id);
    assert.ok(rendered, `relationship ${relationship.id} must keep its own line`);
    assert.deepEqual(rendered.memberEdgeIds, [relationship.id]);
    assert.equal(rendered.sourceModelId, relationship.sourceModelId);
    assert.equal(rendered.targetModelId, relationship.targetModelId);
    assert.equal(rendered.points.trim().split(/\s+/).length, 2);
  }
  assert.equal(edgeById.has("H|legacy-node-group"), false);
});

function createLargeCarrierPayload() {
  const modelCount = 501;
  const modelIds = Array.from(
    { length: modelCount },
    (_, index) => `carrier.Model${String(index).padStart(3, "0")}`,
  );
  const structuralEdges = [];
  structuralEdges.push(edge("edge-chain-0", modelIds[0], modelIds[1]));
  structuralEdges.push(edge("edge-self-0", modelIds[50], modelIds[50], "one_to_one"));
  for (let index = 0; index < 300; index += 1) {
    structuralEdges.push(edge(`edge-fk-hub-${index}`, modelIds[index], modelIds[500]));
  }
  for (let index = 100; index < 400; index += 1) {
    structuralEdges.push(edge(
      `edge-o2o-hub-${index}`,
      modelIds[index],
      modelIds[500],
      "one_to_one",
    ));
  }
  for (let index = 200; index < 450; index += 1) {
    structuralEdges.push(edge(`edge-second-hub-${index}`, modelIds[index], modelIds[499]));
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
      routedEdges: structuralEdges
        .filter((structuralEdge) =>
          structuralEdge.sourceModelId !== structuralEdge.targetModelId)
        .map((structuralEdge) => ({
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

function edge(id, sourceModelId, targetModelId, kind = "foreign_key") {
  return {
    id,
    kind,
    provenance: "declared",
    sourceModelId,
    targetModelId,
  };
}

function createSmallNodeBundlePayload() {
  const modelIds = ["small.A", "small.B", "small.C"];
  const structuralEdges = [
    edge("edge-small-fk", "small.A", "small.B", "foreign_key"),
    edge("edge-small-m2m", "small.A", "small.B", "many_to_many"),
  ];
  return {
    analyzer: {
      diagnostics: [],
      models: modelIds.map((modelId) => ({
        declaredBaseClasses: [],
        fields: [],
        identity: {
          appLabel: "small",
          id: modelId,
          modelName: modelId.slice("small.".length),
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
        appLabel: "small",
        modelId,
        modelName: modelId.slice("small.".length),
      })),
      structuralEdges,
    },
    layout: {
      crossings: [],
      engineMetadata: {
        hubCarrierThreshold: 2,
        leafBundles: [{
          anchor: { x: 170, y: 220 },
          bbox: { height: 160, width: 300, x: 20, y: 140 },
          leafModelIds: ["small.A", "small.C"],
          parentModelId: "small.B",
          sharedRootModelIds: ["small.B"],
        }],
        renderedCarrierRoutes: [{
          carrierId: "H|legacy-node-group",
          memberEdgeIds: structuralEdges.map((relationship) => relationship.id),
          points: [{ x: 120, y: 50 }, { x: 440, y: 50 }],
        }],
      },
      mode: "fmmm",
      nodes: modelIds.map((modelId, index) => ({
        clusterId: index === 1 ? "cluster-b" : "cluster-a",
        modelId,
        position: { x: index * 320, y: index === 2 ? 240 : 0 },
        size: { height: 100, width: 200 },
      })),
      routedEdges: structuralEdges.map((relationship, index) => ({
        crossingIds: [],
        edgeId: relationship.id,
        points: [{ x: 200, y: 40 + index * 20 }, { x: 320, y: 40 + index * 20 }],
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

function assertRelationshipPathConnectsRealTables(renderModel, relationship) {
  const tableById = new Map(renderModel.tables.map((table) => [table.modelId, table]));
  const sourceTable = tableById.get(relationship.sourceModelId);
  const targetTable = tableById.get(relationship.targetModelId);
  const adjacency = new Map();
  const pointByKey = new Map();
  const carriers = renderModel.edges.filter((edge) =>
    (edge.memberEdgeIds ?? [edge.edgeId]).includes(relationship.id));
  assert.equal(carriers.length, 1, "each relationship must remain one direct edge");
  for (const carrier of carriers) {
    const [start, end] = carrier.points.split(/\s+/).map((value) => {
      const [x, y] = value.split(",").map(Number);
      return { x, y };
    });
    const startKey = `${start.x},${start.y}`;
    const endKey = `${end.x},${end.y}`;
    pointByKey.set(startKey, start);
    pointByKey.set(endKey, end);
    if (!adjacency.has(startKey)) adjacency.set(startKey, new Set());
    if (!adjacency.has(endKey)) adjacency.set(endKey, new Set());
    adjacency.get(startKey).add(endKey);
    adjacency.get(endKey).add(startKey);
  }
  const sourceKeys = [...pointByKey]
    .filter(([, point]) => pointTouchesTable(point, sourceTable))
    .map(([key]) => key);
  const targetKeys = new Set(
    [...pointByKey]
      .filter(([, point]) => pointTouchesTable(point, targetTable))
      .map(([key]) => key),
  );
  const seen = new Set(sourceKeys);
  const queue = [...sourceKeys];
  for (let index = 0; index < queue.length; index += 1) {
    for (const neighbor of adjacency.get(queue[index]) ?? []) {
      if (!seen.has(neighbor)) {
        seen.add(neighbor);
        queue.push(neighbor);
      }
    }
  }
  assert.ok([...targetKeys].some((key) => seen.has(key)));
}

function pointTouchesTable(point, table) {
  return point.x >= table.position.x - 0.01
    && point.x <= table.position.x + table.size.width + 0.01
    && point.y >= table.position.y - 0.01
    && point.y <= table.position.y + table.size.height + 0.01;
}
