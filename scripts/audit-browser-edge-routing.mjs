import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, "..");
const layoutPath = process.argv[2];
const edgesPath = process.argv[3]?.startsWith("--") ? undefined : process.argv[3];
const useDirectMemberRoutes = process.argv.includes("--direct-members");
const summaryOnly = process.argv.includes("--summary");

if (!layoutPath) {
  console.error(
    "Usage: node scripts/audit-browser-edge-routing.mjs <layout.json> [edges.tsv] [--direct-members]",
  );
  process.exitCode = 2;
} else {
  const { getBrowserCanvasDrawSource } = require(
    path.join(
      repositoryRoot,
      "out/webview/interaction/runtime/browserCanvasDrawSource.js",
    ),
  );
  const layout = JSON.parse(fs.readFileSync(layoutPath, "utf8"));
  const renderInput = edgesPath
    ? createRenderedAuditInput(layout, edgesPath, useDirectMemberRoutes)
    : {
        bundleLeavesByFakeId: {},
        nodes: layout.nodes ?? [],
        routes: layout.routedEdges ?? [],
      };
  const createStraightEdgePath = createBrowserStraightEdgePath(
    getBrowserCanvasDrawSource,
    renderInput.bundleLeavesByFakeId,
  );
  const scene = createRoutingScene(renderInput.nodes);
  const result = auditRoutes(
    renderInput.routes,
    createStraightEdgePath,
    scene,
  );
  if (renderInput.renderedVisual) {
    result.renderedVisual = renderInput.renderedVisual;
  }
  if (renderInput.routeParity) {
    result.routeParity = renderInput.routeParity;
  }

  console.log(JSON.stringify(
    summaryOnly ? { ...result, collisionSamples: undefined } : result,
    null,
    2,
  ));
  if (
    result.bentEdges > 0
    || result.invalidRoutes > 0
    || result.straightPenetrations > 0
  ) {
    process.exitCode = 1;
  }
}

function createBrowserStraightEdgePath(
  getBrowserCanvasDrawSource,
  bundleLeavesByFakeIdRaw = {},
) {
  const factory = new Function(
    "round2",
    "normalizePoints",
    "findSegments",
    "rectIntersectsBounds",
    "bundleLeavesByFakeIdRaw",
    `${getBrowserCanvasDrawSource()}\nreturn createStraightEdgePath;`,
  );
  return factory(
    (value) => Math.round(value * 100) / 100,
    normalizePoints,
    findSegments,
    rectIntersectsBounds,
    bundleLeavesByFakeIdRaw,
  );
}

function auditRoutes(routes, createStraightEdgePath, scene) {
  let bentEdges = 0;
  let collisionEdges = 0;
  let invalidRoutes = 0;
  let routedSegments = 0;
  let straightPenetrations = 0;
  const collisionSamples = [];
  const routedRoutes = [];

  for (const route of routes) {
    const inputPoints = resolveAuditRoutePoints(route, scene);
    if (inputPoints.length < 2) {
      invalidRoutes += 1;
      continue;
    }
    const sourceModelId = route.sourceModelId
      ?? inferEndpointTableId(inputPoints[0], scene);
    const targetModelId = route.targetModelId
      ?? inferEndpointTableId(inputPoints[inputPoints.length - 1], scene);
    const logicalEndpointModelIds = route.logicalEndpointModelIds
      ?? [sourceModelId, targetModelId].filter(Boolean);
    const straight = createStraightEdgePath(
      inputPoints,
      {
        edgeId: route.edgeId,
        logicalEndpointModelIds,
        sourceModelId,
        targetModelId,
      },
      scene,
    );
    assert.ok(Array.isArray(straight.points));
    straightPenetrations += straight.collisions.length;
    routedSegments += Math.max(0, straight.points.length - 1);
    if (straight.points.length > 2) {
      bentEdges += 1;
    }
    if (straight.collisions.length > 0) {
      collisionEdges += 1;
    }
    routedRoutes.push({
      edgeId: route.edgeId,
      logicalEndpointModelIds,
      points: straight.points,
    });
    if (straight.collisions.length > 0 && collisionSamples.length < 32) {
      collisionSamples.push({
        edgeId: route.edgeId,
        inputPoints,
        logicalEndpointModelIds,
        straightPoints: straight.points,
        sourceModelId,
        tableModelIds: [...new Set(straight.collisions.map(
          (collision) => collision.table.modelId,
        ))],
        targetModelId,
        collisions: straight.collisions.map((collision) => ({
          interval: collision.interval,
          segment: {
            end: straight.points[collision.segmentIndex + 1],
            start: straight.points[collision.segmentIndex],
          },
          table: collision.table,
        })),
      });
    }
  }

  const crossingAudit = measureProperEdgeCrossings(routedRoutes);
  return {
    bendPolicy: "straight-only",
    bentEdges,
    collisionEdges,
    collisionSamples,
    inputEdges: routes.length,
    invalidRoutes,
    nonAdjacentCrossings: crossingAudit.nonAdjacentCrossings,
    routedSegments,
    properEdgeCrossings: crossingAudit.count,
    sharedEndpointCrossings: crossingAudit.sharedEndpointCrossings,
    straightPenetrations,
    tables: scene.tables.length,
    topCrossingEdges: crossingAudit.topEdges,
  };
}

function inferEndpointTableId(point, scene) {
  const epsilon = 0.01;
  const candidates = scene.tables.filter((table) =>
    point.x >= table.x - epsilon
    && point.x <= table.x + table.width + epsilon
    && point.y >= table.y - epsilon
    && point.y <= table.y + table.height + epsilon
  );
  candidates.sort((left, right) => {
    const leftCenterDistance = Math.hypot(
      point.x - (left.x + left.width / 2),
      point.y - (left.y + left.height / 2),
    );
    const rightCenterDistance = Math.hypot(
      point.x - (right.x + right.width / 2),
      point.y - (right.y + right.height / 2),
    );
    return leftCenterDistance - rightCenterDistance
      || String(left.modelId).localeCompare(String(right.modelId));
  });
  return candidates[0]?.modelId;
}

function createRoutingScene(nodes) {
  const tableBuckets = new Map();
  const tables = [];
  const tablesById = new Map();
  for (const node of nodes) {
    if (!node?.position || !node?.size || node.hidden) {
      continue;
    }
    const table = {
      height: Number(node.size.height),
      modelId: node.modelId,
      width: Number(node.size.width),
      x: Number(node.position.x),
      y: Number(node.position.y),
    };
    tables.push(table);
    tablesById.set(table.modelId, table);
    addToBuckets(tableBuckets, table, table.modelId);
  }
  return { tableBuckets, tables, tablesById };
}

function createRenderedAuditInput(layout, edgesFilePath, directMemberRoutes) {
  const structuralEdges = fs.readFileSync(edgesFilePath, "utf8")
    .trim()
    .split(/\r?\n/)
    .filter(Boolean)
    .map((line) => {
      const [id, sourceModelId, targetModelId, kind, provenance] = line.split("\t");
      return { id, kind, provenance, sourceModelId, targetModelId };
    });
  const models = layout.nodes.map((node) => {
    const [appLabel, ...nameParts] = String(node.modelId).split(".");
    return {
      declaredBaseClasses: [],
      fields: [],
      identity: {
        appLabel,
        id: node.modelId,
        modelName: nameParts.join("."),
      },
      methods: [],
      properties: [],
    };
  });
  const {
    createDiagramRenderModel,
    measureRenderedVisualConflicts,
  } = require(
    path.join(repositoryRoot, "out/webview/state/createDiagramRenderModel.js"),
  );
  const renderModel = createDiagramRenderModel({
    analyzer: { diagnostics: [], models, summary: {} },
    contractVersion: "browser-routing-audit",
    graph: {
      diagnostics: [],
      methodAssociations: [],
      nodes: models.map((model) => ({
        appLabel: model.identity.appLabel,
        modelId: model.identity.id,
        modelName: model.identity.modelName,
      })),
      structuralEdges,
    },
    layout,
    layoutExecution: {
      appliedMode: layout.mode ?? "fmmm",
      engine: "ogdf",
      requestedMode: layout.mode ?? "fmmm",
      status: "applied",
    },
    view: { layoutMode: layout.mode ?? "fmmm", tableOptions: [] },
  });
  const originalRouteById = new Map(
    (layout.routedEdges ?? []).map((route) => [route.edgeId, route]),
  );
  return {
    bundleLeavesByFakeId: renderModel.bundleLeavesByFakeId,
    nodes: renderModel.tables,
    renderedVisual: measureRenderedVisualConflicts(renderModel),
    routeParity: compareNativeAndBrowserGeometry(layout, renderModel.edges),
    routes: renderModel.edges.map((edge) => ({
      ...edge,
      points: directMemberRoutes && originalRouteById.has(edge.edgeId)
        ? originalRouteById.get(edge.edgeId).points
        : parseEdgePointString(edge.points),
    })),
  };
}

function compareNativeAndBrowserGeometry(layout, renderedEdges) {
  const nativeKeys = (layout.engineMetadata?.renderedCarrierRoutes ?? [])
    .map((route) => geometryKey(route.points));
  const browserKeys = renderedEdges
    .map((edge) => geometryKey(parseEdgePointString(edge.points)));
  const nativeCounts = frequencyMap(nativeKeys);
  const browserCounts = frequencyMap(browserKeys);
  let matched = 0;
  for (const [key, count] of nativeCounts) {
    matched += Math.min(count, browserCounts.get(key) ?? 0);
  }
  return {
    browserOnly: browserKeys.length - matched,
    browserRoutes: browserKeys.length,
    matched,
    nativeOnly: nativeKeys.length - matched,
    nativeRoutes: nativeKeys.length,
  };
}

function geometryKey(points) {
  if (!Array.isArray(points) || points.length < 2) {
    return "invalid";
  }
  const first = points[0];
  const last = points[points.length - 1];
  const cells = [
    `${Number(first.x).toFixed(2)},${Number(first.y).toFixed(2)}`,
    `${Number(last.x).toFixed(2)},${Number(last.y).toFixed(2)}`,
  ].sort();
  return cells.join("|");
}

function frequencyMap(values) {
  const counts = new Map();
  for (const value of values) {
    counts.set(value, (counts.get(value) ?? 0) + 1);
  }
  return counts;
}

function parseEdgePointString(value) {
  if (typeof value !== "string" || !value.trim()) {
    return [];
  }
  return value.trim().split(/\s+/).map((pair) => {
    const [x, y] = pair.split(",").map(Number);
    return { x, y };
  }).filter((point) => Number.isFinite(point.x) && Number.isFinite(point.y));
}

function resolveAuditRoutePoints(route, scene) {
  const points = Array.isArray(route.points)
    ? normalizePoints(route.points)
    : parseEdgePointString(route.points);
  if (points.length >= 2) {
    return points;
  }
  const source = scene.tablesById.get(route.sourceModelId);
  const target = scene.tablesById.get(route.targetModelId);
  if (!source || !target) {
    return [];
  }
  return [boundaryPort(source, target), boundaryPort(target, source)];
}

function boundaryPort(table, peerTable) {
  const center = {
    x: table.x + table.width / 2,
    y: table.y + table.height / 2,
  };
  const peer = {
    x: peerTable.x + peerTable.width / 2,
    y: peerTable.y + peerTable.height / 2,
  };
  const dx = peer.x - center.x;
  const dy = peer.y - center.y;
  const scaleX = Math.abs(dx) > 1e-9 ? table.width / 2 / Math.abs(dx) : Infinity;
  const scaleY = Math.abs(dy) > 1e-9 ? table.height / 2 / Math.abs(dy) : Infinity;
  const scale = Math.min(scaleX, scaleY);
  return {
    x: Math.round((center.x + dx * scale) * 100) / 100,
    y: Math.round((center.y + dy * scale) * 100) / 100,
  };
}

function measureProperEdgeCrossings(routes) {
  const segments = [];
  for (const route of routes) {
    for (let index = 0; index + 1 < route.points.length; index += 1) {
      const start = route.points[index];
      const end = route.points[index + 1];
      if (start.x === end.x && start.y === end.y) continue;
      segments.push({
        edgeId: route.edgeId,
        end,
        logicalEndpointModelIds: new Set(route.logicalEndpointModelIds ?? []),
        start,
      });
    }
  }
  const cellSize = 480;
  const buckets = new Map();
  segments.forEach((segment, segmentIndex) => {
    const minX = Math.floor(Math.min(segment.start.x, segment.end.x) / cellSize);
    const maxX = Math.floor(Math.max(segment.start.x, segment.end.x) / cellSize);
    const minY = Math.floor(Math.min(segment.start.y, segment.end.y) / cellSize);
    const maxY = Math.floor(Math.max(segment.start.y, segment.end.y) / cellSize);
    for (let y = minY; y <= maxY; y += 1) {
      for (let x = minX; x <= maxX; x += 1) {
        const key = `${x}:${y}`;
        const bucket = buckets.get(key) ?? [];
        bucket.push(segmentIndex);
        buckets.set(key, bucket);
      }
    }
  });
  const testedPairs = new Set();
  const crossingsByEdge = new Map();
  let count = 0;
  let sharedEndpointCrossings = 0;
  for (const bucket of buckets.values()) {
    for (let leftIndex = 0; leftIndex < bucket.length; leftIndex += 1) {
      for (let rightIndex = leftIndex + 1; rightIndex < bucket.length; rightIndex += 1) {
        const leftId = bucket[leftIndex];
        const rightId = bucket[rightIndex];
        if (leftId === rightId) continue;
        const low = Math.min(leftId, rightId);
        const high = Math.max(leftId, rightId);
        const pairKey = low * segments.length + high;
        if (testedPairs.has(pairKey)) continue;
        testedPairs.add(pairKey);
        const left = segments[low];
        const right = segments[high];
        if (left.edgeId === right.edgeId || !properSegmentsCross(left, right)) continue;
        count += 1;
        const [smallerEndpoints, largerEndpoints] =
          left.logicalEndpointModelIds.size <= right.logicalEndpointModelIds.size
            ? [left.logicalEndpointModelIds, right.logicalEndpointModelIds]
            : [right.logicalEndpointModelIds, left.logicalEndpointModelIds];
        if ([...smallerEndpoints].some((endpoint) => largerEndpoints.has(endpoint))) {
          sharedEndpointCrossings += 1;
        }
        crossingsByEdge.set(left.edgeId, (crossingsByEdge.get(left.edgeId) ?? 0) + 1);
        crossingsByEdge.set(right.edgeId, (crossingsByEdge.get(right.edgeId) ?? 0) + 1);
      }
    }
  }
  return {
    count,
    nonAdjacentCrossings: count - sharedEndpointCrossings,
    sharedEndpointCrossings,
    topEdges: [...crossingsByEdge.entries()]
      .sort((left, right) => right[1] - left[1] || left[0].localeCompare(right[0]))
      .slice(0, 12)
      .map(([edgeId, crossings]) => ({ crossings, edgeId })),
  };
}

function properSegmentsCross(left, right) {
  const a = orientation(left.start, left.end, right.start);
  const b = orientation(left.start, left.end, right.end);
  const c = orientation(right.start, right.end, left.start);
  const d = orientation(right.start, right.end, left.end);
  const epsilon = 1e-7;
  return ((a > epsilon && b < -epsilon) || (a < -epsilon && b > epsilon))
    && ((c > epsilon && d < -epsilon) || (c < -epsilon && d > epsilon));
}

function orientation(start, end, point) {
  return (end.x - start.x) * (point.y - start.y)
    - (end.y - start.y) * (point.x - start.x);
}

function addToBuckets(buckets, table, modelId) {
  const tileSize = 960;
  const startColumn = Math.floor(table.x / tileSize);
  const endColumn = Math.floor((table.x + table.width) / tileSize);
  const startRow = Math.floor(table.y / tileSize);
  const endRow = Math.floor((table.y + table.height) / tileSize);
  for (let row = startRow; row <= endRow; row += 1) {
    for (let column = startColumn; column <= endColumn; column += 1) {
      const key = `${column}:${row}`;
      const bucket = buckets.get(key) ?? [];
      bucket.push(modelId);
      buckets.set(key, bucket);
    }
  }
}

function normalizePoints(points) {
  return points
    .map((point) => ({ x: Number(point.x), y: Number(point.y) }))
    .filter((point) => Number.isFinite(point.x) && Number.isFinite(point.y))
    .filter((point, index, values) =>
      index === 0
      || point.x !== values[index - 1].x
      || point.y !== values[index - 1].y);
}

function findSegments(points) {
  return points.slice(1).map((end, index) => ({
    end,
    start: points[index],
  }));
}

function rectIntersectsBounds(x, y, width, height, bounds, padding = 0) {
  return x + width + padding >= bounds.left
    && x - padding <= bounds.right
    && y + height + padding >= bounds.top
    && y - padding <= bounds.bottom;
}
