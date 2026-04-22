import assert from "node:assert/strict";
import path from "node:path";
import test from "node:test";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const layoutModulePath = path.resolve(
  __dirname,
  "../../out/webview/interaction/runtime/browserLayoutSource.js",
);
const { getBrowserLayoutSource } = require(layoutModulePath);

test("phase11 layered local layouts reduce abstract layer crossings for a simple DAG", () => {
  const tables = [
    createTable("mesh.Alpha", "Alpha", { x: 0, y: 0 }),
    createTable("mesh.Beta", "Beta", { x: 0, y: 220 }),
    createTable("mesh.Gamma", "Gamma", { x: 340, y: 0 }),
    createTable("mesh.Delta", "Delta", { x: 340, y: 220 }),
    createTable("mesh.Epsilon", "Epsilon", { x: 680, y: 0 }),
    createTable("mesh.Zeta", "Zeta", { x: 680, y: 220 }),
  ];
  const edges = [
    { edgeId: "edge-alpha-delta", sourceModelId: "mesh.Alpha", targetModelId: "mesh.Delta" },
    { edgeId: "edge-beta-gamma", sourceModelId: "mesh.Beta", targetModelId: "mesh.Gamma" },
    { edgeId: "edge-gamma-zeta", sourceModelId: "mesh.Gamma", targetModelId: "mesh.Zeta" },
    { edgeId: "edge-delta-epsilon", sourceModelId: "mesh.Delta", targetModelId: "mesh.Epsilon" },
  ];
  const runtime = createBrowserLayoutRuntime(edges);
  const layoutVariants = runtime.createLayoutVariants(tables);

  const hierarchicalCrossings = countAbstractLayerCrossings(
    extractOrderedLayers(layoutVariants.hierarchical, tables, "x", "y"),
    edges,
  );
  const neuralCrossings = countAbstractLayerCrossings(
    extractOrderedLayers(layoutVariants.neural, tables, "x", "y"),
    edges,
  );
  const flowCrossings = countAbstractLayerCrossings(
    extractOrderedLayers(layoutVariants.flow, tables, "y", "x"),
    edges,
  );

  assert.equal(hierarchicalCrossings, 2);
  assert.equal(neuralCrossings, 0);
  assert.equal(flowCrossings, 0);
  assert.ok(
    neuralCrossings < hierarchicalCrossings,
    "neural layout should reduce layer-order crossings for the synthetic DAG",
  );
  assert.ok(
    flowCrossings < hierarchicalCrossings,
    "flow layout should reduce layer-order crossings for the synthetic DAG",
  );
});

function createBrowserLayoutRuntime(edgeMeta) {
  const source = getBrowserLayoutSource();
  const factory = new Function(
    "edgeMeta",
    `${source}
    return {
      createLayoutVariants,
    };`,
  );

  return factory(edgeMeta);
}

function createTable(modelId, modelName, basePosition) {
  return {
    appLabel: "mesh",
    basePosition,
    height: 120,
    modelId,
    modelName,
    width: 220,
  };
}

function extractOrderedLayers(layout, tables, layerAxis, orderAxis) {
  const entries = tables.map((table) => ({
    layerValue: roundKey(layout[table.modelId][layerAxis]),
    modelId: table.modelId,
    orderValue: layout[table.modelId][orderAxis],
  }));
  const layerValues = Array.from(new Set(entries.map((entry) => entry.layerValue))).sort(
    compareNumericKeys,
  );

  return layerValues.map((layerValue) =>
    entries
      .filter((entry) => entry.layerValue === layerValue)
      .slice()
      .sort((left, right) => {
        if (left.orderValue !== right.orderValue) {
          return left.orderValue - right.orderValue;
        }

        return left.modelId.localeCompare(right.modelId);
      })
      .map((entry) => entry.modelId),
  );
}

function countAbstractLayerCrossings(layers, edges) {
  const indexById = new Map();

  layers.forEach((layerIds, layerIndex) => {
    layerIds.forEach((modelId, orderIndex) => {
      indexById.set(modelId, {
        layerIndex,
        orderIndex,
      });
    });
  });

  let crossings = 0;

  for (let left = 0; left < edges.length; left += 1) {
    for (let right = left + 1; right < edges.length; right += 1) {
      const leftSource = indexById.get(edges[left].sourceModelId);
      const leftTarget = indexById.get(edges[left].targetModelId);
      const rightSource = indexById.get(edges[right].sourceModelId);
      const rightTarget = indexById.get(edges[right].targetModelId);

      if (
        !leftSource ||
        !leftTarget ||
        !rightSource ||
        !rightTarget ||
        leftSource.layerIndex !== rightSource.layerIndex ||
        leftTarget.layerIndex !== rightTarget.layerIndex ||
        leftSource.layerIndex === leftTarget.layerIndex
      ) {
        continue;
      }

      const sourceDelta = leftSource.orderIndex - rightSource.orderIndex;
      const targetDelta = leftTarget.orderIndex - rightTarget.orderIndex;

      if (sourceDelta === 0 || targetDelta === 0) {
        continue;
      }

      if (
        (sourceDelta < 0 && targetDelta > 0) ||
        (sourceDelta > 0 && targetDelta < 0)
      ) {
        crossings += 1;
      }
    }
  }

  return crossings;
}

function roundKey(value) {
  return value.toFixed(2);
}

function compareNumericKeys(left, right) {
  return Number.parseFloat(left) - Number.parseFloat(right);
}
