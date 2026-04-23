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

test("phase11 local layouts keep the simple DAG crossing-free while hierarchical expands layer spacing", () => {
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
  const runtime = createBrowserLayoutRuntime(edges, tables);
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

  assert.equal(hierarchicalCrossings, 0);
  assert.equal(neuralCrossings, 0);
  assert.equal(flowCrossings, 0);
  assert.ok(
    layoutVariants.hierarchical["mesh.Gamma"].x - layoutVariants.hierarchical["mesh.Alpha"].x > 340,
    "hierarchical layout should expand layer spacing beyond the compact analyzer seed positions",
  );
});

test("phase11 runtime routing fans out shared-source edges across distinct side ports", () => {
  const tables = [
    createTable("mesh.Hub", "Hub", { x: 0, y: 120 }),
    createTable("mesh.Top", "Top", { x: 420, y: 0 }),
    createTable("mesh.Middle", "Middle", { x: 420, y: 120 }),
    createTable("mesh.Bottom", "Bottom", { x: 420, y: 240 }),
  ];
  const edges = [
    { edgeId: "edge-hub-top", sourceModelId: "mesh.Hub", targetModelId: "mesh.Top" },
    { edgeId: "edge-hub-middle", sourceModelId: "mesh.Hub", targetModelId: "mesh.Middle" },
    { edgeId: "edge-hub-bottom", sourceModelId: "mesh.Hub", targetModelId: "mesh.Bottom" },
  ];
  const runtime = createBrowserLayoutRuntime(edges, tables);
  const routedEdges = runtime.routeVisibleEdgesWithPorts(
    createEdgeEntries(edges, tables),
  );
  const sourceAnchorYs = routedEdges
    .map((route) => route.points[0].y)
    .slice()
    .sort((left, right) => left - right);
  const hubCenterY = tables[0].basePosition.y + tables[0].height / 2;

  assert.equal(new Set(sourceAnchorYs).size, 3);
  assert.ok(
    sourceAnchorYs[0] < hubCenterY && sourceAnchorYs[sourceAnchorYs.length - 1] > hubCenterY,
    "shared-source edges should leave the table from separate right-side ports instead of one merged center point",
  );
});

test("phase11 obstacle-aware routing detours around blocking tables", () => {
  const tables = [
    createTable("mesh.Source", "Source", { x: 0, y: 120 }),
    createTable("mesh.Blocker", "Blocker", { x: 280, y: 120 }),
    createTable("mesh.Target", "Target", { x: 560, y: 120 }),
  ];
  const edges = [
    { edgeId: "edge-source-target", sourceModelId: "mesh.Source", targetModelId: "mesh.Target" },
  ];
  const runtime = createBrowserLayoutRuntime(edges, tables);
  const routed = runtime.routeVisibleEdgesWithPorts(createEdgeEntries(edges, tables))[0];
  const blockerRect = toRect(tables[1]);

  assert.ok(
    findPathSegments(routed.points).every((segment) => !segmentCrossesRect(segment, blockerRect)),
    "edge should route around the blocker instead of cutting through its box",
  );
  assert.ok(
    routed.points.some((point) => point.y !== routed.points[0].y),
    "obstacle-aware path should introduce a visible detour when a direct trunk is blocked",
  );
});

test("phase11 grid routing keeps edges out of multiple unrelated table boxes", () => {
  const tables = [
    createTable("mesh.Source", "Source", { x: 0, y: 240 }),
    createTable("mesh.BlockerA", "BlockerA", { x: 280, y: 120 }),
    createTable("mesh.BlockerB", "BlockerB", { x: 560, y: 240 }),
    createTable("mesh.BlockerC", "BlockerC", { x: 840, y: 120 }),
    createTable("mesh.Target", "Target", { x: 1120, y: 240 }),
  ];
  const edges = [
    { edgeId: "edge-source-target", sourceModelId: "mesh.Source", targetModelId: "mesh.Target" },
  ];
  const runtime = createBrowserLayoutRuntime(edges, tables);
  const routed = runtime.routeVisibleEdgesWithPorts(createEdgeEntries(edges, tables))[0];
  const blockerRects = tables.slice(1, 4).map(toRect);

  assert.ok(
    blockerRects.every((rect) =>
      findPathSegments(routed.points).every((segment) => !segmentCrossesRect(segment, rect)),
    ),
    "grid route should never draw through unrelated table boxes even when several blockers sit in the corridor",
  );
});

test("phase11 catalog routing also avoids visible table boxes", () => {
  const tables = [
    createTable("mesh.Source", "Source", { x: 0, y: 120 }),
    createTable("mesh.Blocker", "Blocker", { x: 280, y: 120 }),
    createTable("mesh.Target", "Target", { x: 560, y: 120 }),
  ];
  const edges = [
    { edgeId: "edge-source-target", sourceModelId: "mesh.Source", targetModelId: "mesh.Target" },
  ];
  const runtime = createBrowserLayoutRuntime(edges, tables, {
    layoutMode: "clustered",
  });
  const routed = runtime.routeCatalogEdgesWithPorts(createEdgeEntries(edges, tables))[0];
  const blockerRect = toRect(tables[1]);

  assert.ok(
    findPathSegments(routed.points).every((segment) => !segmentCrossesRect(segment, blockerRect)),
    "catalog route should use the same obstacle-aware router instead of crossing table boxes",
  );
});

test("phase11 all local layout variants keep dense seed positions separated", () => {
  const tables = Array.from({ length: 12 }, (_, index) =>
    createTable(`mesh.Node${index + 1}`, `Node${index + 1}`, {
      x: (index % 3) * 24,
      y: Math.floor(index / 3) * 18,
    }),
  );
  const edges = [];
  for (let index = 0; index < tables.length - 1; index += 1) {
    edges.push({
      edgeId: `edge-${index}`,
      sourceModelId: tables[index].modelId,
      targetModelId: tables[index + 1].modelId,
    });
  }
  const runtime = createBrowserLayoutRuntime(edges, tables);
  const layoutVariants = runtime.createLayoutVariants(tables);

  for (const [layoutName, layout] of Object.entries(layoutVariants)) {
    assert.equal(
      countStrictNodeOverlaps(layout, tables),
      0,
      `${layoutName} layout should not leave table boxes overlapping after final spacing`,
    );
  }
});

test("phase11 node spacing tuning widens the neural layout footprint", () => {
  const tables = [
    createTable("mesh.Alpha", "Alpha", { x: 0, y: 0 }),
    createTable("mesh.Beta", "Beta", { x: 0, y: 220 }),
    createTable("mesh.Gamma", "Gamma", { x: 340, y: 0 }),
    createTable("mesh.Delta", "Delta", { x: 340, y: 220 }),
    createTable("mesh.Epsilon", "Epsilon", { x: 680, y: 0 }),
    createTable("mesh.Zeta", "Zeta", { x: 680, y: 220 }),
  ];
  const edges = [
    { edgeId: "edge-alpha-gamma", sourceModelId: "mesh.Alpha", targetModelId: "mesh.Gamma" },
    { edgeId: "edge-beta-delta", sourceModelId: "mesh.Beta", targetModelId: "mesh.Delta" },
    { edgeId: "edge-gamma-epsilon", sourceModelId: "mesh.Gamma", targetModelId: "mesh.Epsilon" },
    { edgeId: "edge-delta-zeta", sourceModelId: "mesh.Delta", targetModelId: "mesh.Zeta" },
  ];
  const runtime = createBrowserLayoutRuntime(edges, tables);
  const compactLayout = runtime.createLayoutVariants(tables, {
    edgeDetour: 1.2,
    nodeSpacing: 0.8,
  }).neural;
  const expandedLayout = runtime.createLayoutVariants(tables, {
    edgeDetour: 1.2,
    nodeSpacing: 1.8,
  }).neural;

  assert.ok(
    computeLayoutWidth(expandedLayout, tables) > computeLayoutWidth(compactLayout, tables) + 140,
    "higher node spacing should noticeably widen the neural layout footprint",
  );
});

test("phase11 edge detour tuning pushes reverse edges onto wider outer lanes", () => {
  const tables = [
    createTable("mesh.Left", "Left", { x: 0, y: 120 }),
    createTable("mesh.Middle", "Middle", { x: 280, y: 120 }),
    createTable("mesh.Right", "Right", { x: 560, y: 120 }),
  ];
  const edges = [
    {
      edgeId: "edge-right-left",
      provenance: "derived_reverse",
      sourceModelId: "mesh.Right",
      targetModelId: "mesh.Left",
    },
  ];
  const compactRuntime = createBrowserLayoutRuntime(edges, tables, {
    settings: {
      edgeDetour: 0.8,
      nodeSpacing: 1.25,
    },
  });
  const detouredRuntime = createBrowserLayoutRuntime(edges, tables, {
    settings: {
      edgeDetour: 1.8,
      nodeSpacing: 1.25,
    },
  });
  const compactRoute = compactRuntime.routeVisibleEdgesWithPorts(createEdgeEntries(edges, tables))[0];
  const detouredRoute = detouredRuntime.routeVisibleEdgesWithPorts(createEdgeEntries(edges, tables))[0];
  const middleRect = toRect(tables[1]);

  assert.ok(
    findRouteClearanceFromRect(detouredRoute.points, middleRect) >
      findRouteClearanceFromRect(compactRoute.points, middleRect) + 40,
    "higher edge detour should move reverse-edge trunks farther away from the blocking middle table",
  );
});

function createBrowserLayoutRuntime(edgeMeta, tableMetaList = [], stateOverride = {}) {
  const source = getBrowserLayoutSource();
  const factory = new Function(
    "edgeMeta",
    "state",
    "tableMetaList",
    `${source}
    const tableMetaById = new Map(tableMetaList.map((table) => [table.modelId, table]));
    function isVisibleModel() {
      return true;
    }
    function getCurrentPosition(modelId) {
      const table = tableMetaById.get(modelId);
      return table ? table.basePosition : { x: 0, y: 0 };
    }
    return {
      createLayoutVariants,
      routeCatalogEdgesWithPorts,
      routeVisibleEdgesWithPorts,
    };`,
  );

  return factory(
    edgeMeta,
    {
      layoutMode: "neural",
      settings: {
        edgeDetour: 1.2,
        nodeSpacing: 1.25,
      },
      ...stateOverride,
    },
    tableMetaList,
  );
}

function createEdgeEntries(edgeMeta, tables) {
  const tableById = new Map(tables.map((table) => [table.modelId, table]));

  return edgeMeta.map((edge) => ({
    meta: {
      edgeId: edge.edgeId,
      provenance: edge.provenance || "declared",
      sourceModelId: edge.sourceModelId,
      targetModelId: edge.targetModelId,
    },
    sourcePosition: tableById.get(edge.sourceModelId).basePosition,
    sourceTable: tableById.get(edge.sourceModelId),
    targetPosition: tableById.get(edge.targetModelId).basePosition,
    targetTable: tableById.get(edge.targetModelId),
  }));
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

function findPathSegments(points) {
  const segments = [];

  for (let index = 1; index < points.length; index += 1) {
    segments.push({
      end: points[index],
      start: points[index - 1],
    });
  }

  return segments;
}

function segmentCrossesRect(segment, rect) {
  if (segment.start.x === segment.end.x) {
    const x = segment.start.x;
    const minY = Math.min(segment.start.y, segment.end.y);
    const maxY = Math.max(segment.start.y, segment.end.y);

    return x > rect.minX && x < rect.maxX && maxY > rect.minY && minY < rect.maxY;
  }

  const y = segment.start.y;
  const minX = Math.min(segment.start.x, segment.end.x);
  const maxX = Math.max(segment.start.x, segment.end.x);

  return y > rect.minY && y < rect.maxY && maxX > rect.minX && minX < rect.maxX;
}

function toRect(table) {
  return {
    maxX: table.basePosition.x + table.width,
    maxY: table.basePosition.y + table.height,
    minX: table.basePosition.x,
    minY: table.basePosition.y,
  };
}

function countStrictNodeOverlaps(layout, tables) {
  let overlaps = 0;

  for (let leftIndex = 0; leftIndex < tables.length; leftIndex += 1) {
    for (let rightIndex = leftIndex + 1; rightIndex < tables.length; rightIndex += 1) {
      const leftTable = tables[leftIndex];
      const rightTable = tables[rightIndex];
      const leftPosition = layout[leftTable.modelId];
      const rightPosition = layout[rightTable.modelId];

      if (
        leftPosition.x < rightPosition.x + rightTable.width &&
        leftPosition.x + leftTable.width > rightPosition.x &&
        leftPosition.y < rightPosition.y + rightTable.height &&
        leftPosition.y + leftTable.height > rightPosition.y
      ) {
        overlaps += 1;
      }
    }
  }

  return overlaps;
}

function computeLayoutWidth(layout, tables) {
  let minX = Number.POSITIVE_INFINITY;
  let maxX = Number.NEGATIVE_INFINITY;

  tables.forEach((table) => {
    const position = layout[table.modelId];
    minX = Math.min(minX, position.x);
    maxX = Math.max(maxX, position.x + table.width);
  });

  return maxX - minX;
}

function findRouteClearanceFromRect(points, rect) {
  return findPathSegments(points).reduce((largest, segment) => {
    if (segment.start.x === segment.end.x) {
      const x = segment.start.x;
      const segmentMinY = Math.min(segment.start.y, segment.end.y);
      const segmentMaxY = Math.max(segment.start.y, segment.end.y);
      const overlapsY = segmentMaxY >= rect.minY && segmentMinY <= rect.maxY;
      const distance = x < rect.minX
        ? rect.minX - x
        : x > rect.maxX
          ? x - rect.maxX
          : 0;

      return overlapsY ? Math.max(largest, distance) : largest;
    }

    const y = segment.start.y;
    const segmentMinX = Math.min(segment.start.x, segment.end.x);
    const segmentMaxX = Math.max(segment.start.x, segment.end.x);
    const overlapsX = segmentMaxX >= rect.minX && segmentMinX <= rect.maxX;
    const distance = y < rect.minY
      ? rect.minY - y
      : y > rect.maxY
        ? y - rect.maxY
        : 0;

    return overlapsX ? Math.max(largest, distance) : largest;
  }, 0);
}
