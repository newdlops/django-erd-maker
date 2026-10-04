import fs from "node:fs";

import {
  createDiagramRenderModel,
  measureRenderedEdgeNodeIntersections,
  measureRenderedVisualConflicts,
} from "../../out/webview/state/createDiagramRenderModel.js";

const layoutPath = process.argv[2] ?? "/private/tmp/djerd-e9-wide-rings.json";
const edgePath = process.argv[3]
  ?? "/var/folders/pc/jdz8pf2x2hl_wf6wpxl1zjzm0000gn/T/django-erd-ogdf-fmmm-kCVZQg/edges.tsv";
const summaryOnly = process.argv.includes("--summary");

const layout = JSON.parse(fs.readFileSync(layoutPath, "utf8"));
const canonicalStructuralEdges = fs.readFileSync(edgePath, "utf8").trim().split(/\n/).map((line) => {
  const [id, sourceModelId, targetModelId, kind, provenance] = line.split("\t");
  return { id, kind, provenance, sourceModelId, targetModelId };
});
const reverseKindByKind: Record<string, string> = {
  foreign_key: "reverse_foreign_key",
  many_to_many: "reverse_many_to_many",
  one_to_one: "reverse_one_to_one",
};
// Production payloads contain analyzer-derived reverse relationships in
// addition to the canonical routed edge set. Recreate them for catalog degree
// sizing without adding them to layout.routedEdges or the visible line set.
const structuralEdges = canonicalStructuralEdges.flatMap((edge) => {
  const reverseKind = reverseKindByKind[edge.kind];
  return reverseKind
    ? [
        edge,
        {
          ...edge,
          id: `${edge.id}:eval-derived-reverse`,
          kind: reverseKind,
          provenance: "derived_reverse",
          sourceModelId: edge.targetModelId,
          targetModelId: edge.sourceModelId,
        },
      ]
    : [edge];
});
const nodeIds = layout.nodes.map((node: { modelId: string }) => node.modelId);
const realModelIds = new Set(nodeIds);
const payload = {
  analyzer: {
    diagnostics: [],
    models: nodeIds.map((modelId: string) => {
      const [appLabel, ...nameParts] = modelId.split(".");
      return {
        declaredBaseClasses: [],
        fields: [],
        identity: {
          appLabel,
          id: modelId,
          modelName: nameParts.join("."),
        },
        methods: [],
        properties: [],
      };
    }),
    summary: {},
  },
  contractVersion: "semantic-carrier-eval",
  graph: {
    diagnostics: [],
    methodAssociations: [],
    nodes: nodeIds.map((modelId: string) => {
      const [appLabel, ...nameParts] = modelId.split(".");
      return { appLabel, modelId, modelName: nameParts.join(".") };
    }),
    structuralEdges,
  },
  layout,
  layoutExecution: {
    appliedMode: layout.mode,
    engine: "ogdf",
    requestedMode: layout.mode,
    status: "applied",
  },
  view: {
    layoutMode: layout.mode,
    tableOptions: [],
  },
};

const renderStartedAt = performance.now();
const renderModel = createDiagramRenderModel(payload as never);
const renderMs = performance.now() - renderStartedAt;
const metrics = measureRenderedVisualConflicts(renderModel);
const edgeNode = measureRenderedEdgeNodeIntersections(renderModel);
const canonicalEdgeById = new Map(canonicalStructuralEdges.map((edge) => [edge.id, edge]));
const exactLayoutTables = renderModel.tables.map((table) => {
  const node = layout.nodes.find((candidate: { modelId: string }) =>
    candidate.modelId === table.modelId);
  return node
    ? { ...table, position: node.position, size: node.size }
    : table;
});
const exactLayoutEdges = layout.routedEdges.flatMap((route: {
  edgeId: string;
  points: Array<{ x: number; y: number }>;
  sourceModelId?: string;
  targetModelId?: string;
}) => {
  const relationship = canonicalEdgeById.get(route.edgeId);
  if (!relationship || route.points.length < 2) return [];
  const start = route.points[0];
  const end = route.points[route.points.length - 1];
  return [{
    crossingIds: [],
    cssKind: relationship.kind.replaceAll("_", "-"),
    edgeId: relationship.id,
    logicalEndpointModelIds: [relationship.sourceModelId, relationship.targetModelId],
    memberEdgeIds: [relationship.id],
    physicalEndpointModelIds: [relationship.sourceModelId, relationship.targetModelId],
    points: `${start.x},${start.y} ${end.x},${end.y}`,
    preserveRouteEndpoints: true,
    provenance: relationship.provenance,
    sourceModelId: route.sourceModelId ?? relationship.sourceModelId,
    targetModelId: route.targetModelId ?? relationship.targetModelId,
  }];
});
const exactLayoutMetrics = measureRenderedVisualConflicts({
  ...renderModel,
  edges: exactLayoutEdges,
  tables: exactLayoutTables,
});
const renderedEdgeById = new Map(renderModel.edges.map((edge) => [edge.edgeId, edge]));
const renderedTableById = new Map(renderModel.tables.map((table) => [table.modelId, table]));
const layoutNodeById = new Map(layout.nodes.map(
  (node: { modelId: string; size: { height: number; width: number } }) =>
    [node.modelId, node] as const,
));
const tableSizeMismatches = renderModel.tables.filter((table) => {
  const node = layoutNodeById.get(table.modelId);
  return !node
    || Math.abs(node.size.width - table.size.width) > 0.01
    || Math.abs(node.size.height - table.size.height) > 0.01;
}).length;
const relationshipProxyTables = renderModel.tables
  .map((table) => String(table.modelId))
  .filter((modelId) => modelId.startsWith("__relationbundle."));
const syntheticBundleTables = renderModel.tables
  .map((table) => String(table.modelId))
  .filter((modelId) =>
    modelId.startsWith("__relationbundle.")
    || modelId.startsWith("__leafbundle."));
const nonModelEndpointEdges = renderModel.edges
  .filter((edge) =>
    !realModelIds.has(edge.sourceModelId)
    || !realModelIds.has(edge.targetModelId)
  )
  .map((edge) => edge.edgeId);
const edgeNodeHitSummary = edgeNode.hits.reduce((summary, hit) => {
  const edge = renderedEdgeById.get(hit.edgeId);
  const edgeClass = edge?.carrierRole ?? "direct";
  const obstacleClass = String(hit.nodeModelId).startsWith("__leafbundle.")
    ? "bundleObstacle"
    : "modelObstacle";
  const key = `${edgeClass}/${obstacleClass}`;
  summary[key] = (summary[key] ?? 0) + 1;
  return summary;
}, {} as Record<string, number>);
const edgeCrossingPairs = metrics.edgeCrossings <= 20
  ? renderedCrossingPairs(renderModel.edges)
  : [];
const edgeCrossingDetails = edgeCrossingPairs.map(([leftId, rightId]) =>
  [leftId, rightId].map((edgeId) => {
    const edge = renderedEdgeById.get(edgeId);
    return {
      edgeId,
      source: edge ? renderedTableById.get(edge.sourceModelId) : undefined,
      target: edge ? renderedTableById.get(edge.targetModelId) : undefined,
    };
  })
);
const edgeNodeHitDetails = edgeNode.hits.length <= 20
  ? edgeNode.hits.map((hit) => {
      const edge = renderedEdgeById.get(hit.edgeId);
      return {
        ...hit,
        obstacle: renderedTableById.get(hit.nodeModelId),
        source: edge ? renderedTableById.get(edge.sourceModelId) : undefined,
        target: edge ? renderedTableById.get(edge.targetModelId) : undefined,
      };
    })
  : [];
const pointCounts = renderModel.edges.map((edge) =>
  edge.points.trim() ? edge.points.trim().split(/\s+/).length : 0);
const parsedSegments = renderModel.edges.flatMap((edge) => {
  const points = edge.points.trim().split(/\s+/).map((value) => {
    const [x, y] = value.split(",").map(Number);
    return { x, y };
  }).filter((point) => Number.isFinite(point.x) && Number.isFinite(point.y));
  return points.length >= 2
    ? [{
        edge,
        end: points[points.length - 1],
        start: points[0],
      }]
    : [];
});
const properCross = (
  left: (typeof parsedSegments)[number],
  right: (typeof parsedSegments)[number],
) => {
  const rx = left.end.x - left.start.x;
  const ry = left.end.y - left.start.y;
  const sx = right.end.x - right.start.x;
  const sy = right.end.y - right.start.y;
  const denominator = rx * sy - ry * sx;
  if (Math.abs(denominator) < 1e-9) return false;
  const qx = right.start.x - left.start.x;
  const qy = right.start.y - left.start.y;
  const t = (qx * sy - qy * sx) / denominator;
  const u = (qx * ry - qy * rx) / denominator;
  return t > 1e-9 && t < 1 - 1e-9 && u > 1e-9 && u < 1 - 1e-9;
};
const degreeByModelId = canonicalStructuralEdges.reduce((degrees, edge) => {
  degrees.set(edge.sourceModelId, (degrees.get(edge.sourceModelId) ?? 0) + 1);
  degrees.set(edge.targetModelId, (degrees.get(edge.targetModelId) ?? 0) + 1);
  return degrees;
}, new Map<string, number>());
const isDegreeOneEdge = (edge: (typeof parsedSegments)[number]["edge"]) => {
  const endpointIds = edge.logicalEndpointModelIds
    ?? [edge.sourceModelId, edge.targetModelId];
  return endpointIds.some((modelId) => degreeByModelId.get(modelId) === 1);
};
let adjacentEdgeCrossings = 0;
let nonAdjacentEdgeCrossings = 0;
let degreeOneToDegreeOneCrossings = 0;
let degreeOneToCoreCrossings = 0;
let coreToCoreCrossings = 0;
const crossingCountByRolePair = new Map<string, number>();
const crossingCountByEdgeId = new Map<string, number>();
const crossingCountByTargetKind = new Map<string, number>();
const crossingCountBySourceKind = new Map<string, number>();
let sameTargetKindCrossings = 0;
let sameSourceKindCrossings = 0;
const semanticKeysForEdge = (
  edge: (typeof parsedSegments)[number]["edge"],
  endpoint: "source" | "target",
) => new Set((edge.memberEdgeIds ?? [edge.edgeId]).flatMap((memberEdgeId) => {
  const relationship = canonicalEdgeById.get(memberEdgeId);
  if (!relationship) return [];
  const modelId = endpoint === "source"
    ? relationship.sourceModelId
    : relationship.targetModelId;
  return [`${modelId}\u0000${relationship.kind}`];
}));
for (let leftIndex = 0; leftIndex < parsedSegments.length; leftIndex += 1) {
  const left = parsedSegments[leftIndex];
  const leftEndpoints = new Set(
    left.edge.logicalEndpointModelIds
    ?? [left.edge.sourceModelId, left.edge.targetModelId],
  );
  for (let rightIndex = leftIndex + 1; rightIndex < parsedSegments.length; rightIndex += 1) {
    const right = parsedSegments[rightIndex];
    if (!properCross(left, right)) continue;
    const rolePair = [
      left.edge.carrierRole ?? "direct",
      right.edge.carrierRole ?? "direct",
    ].sort().join("/");
    crossingCountByRolePair.set(
      rolePair,
      (crossingCountByRolePair.get(rolePair) ?? 0) + 1,
    );
    crossingCountByEdgeId.set(
      left.edge.edgeId,
      (crossingCountByEdgeId.get(left.edge.edgeId) ?? 0) + 1,
    );
    crossingCountByEdgeId.set(
      right.edge.edgeId,
      (crossingCountByEdgeId.get(right.edge.edgeId) ?? 0) + 1,
    );
    const leftTargetKeys = semanticKeysForEdge(left.edge, "target");
    const rightTargetKeys = semanticKeysForEdge(right.edge, "target");
    const sharedTargetKeys = [...leftTargetKeys].filter((key) => rightTargetKeys.has(key));
    if (sharedTargetKeys.length > 0) {
      sameTargetKindCrossings += 1;
      for (const key of sharedTargetKeys) {
        crossingCountByTargetKind.set(
          key,
          (crossingCountByTargetKind.get(key) ?? 0) + 1,
        );
      }
    }
    const leftSourceKeys = semanticKeysForEdge(left.edge, "source");
    const rightSourceKeys = semanticKeysForEdge(right.edge, "source");
    const sharedSourceKeys = [...leftSourceKeys].filter((key) => rightSourceKeys.has(key));
    if (sharedSourceKeys.length > 0) {
      sameSourceKindCrossings += 1;
      for (const key of sharedSourceKeys) {
        crossingCountBySourceKind.set(
          key,
          (crossingCountBySourceKind.get(key) ?? 0) + 1,
        );
      }
    }
    const leftIsDegreeOne = isDegreeOneEdge(left.edge);
    const rightIsDegreeOne = isDegreeOneEdge(right.edge);
    if (leftIsDegreeOne && rightIsDegreeOne) {
      degreeOneToDegreeOneCrossings += 1;
    } else if (leftIsDegreeOne || rightIsDegreeOne) {
      degreeOneToCoreCrossings += 1;
    } else {
      coreToCoreCrossings += 1;
    }
    const rightEndpoints = right.edge.logicalEndpointModelIds
      ?? [right.edge.sourceModelId, right.edge.targetModelId];
    if (rightEndpoints.some((modelId) => leftEndpoints.has(modelId))) {
      adjacentEdgeCrossings += 1;
    } else {
      nonAdjacentEdgeCrossings += 1;
    }
  }
}
const representedEdgeIds = new Set(renderModel.edges.flatMap((edge) =>
  edge.memberEdgeIds ?? [edge.edgeId]));
const missingRoutedEdgeIds = layout.routedEdges
  .map((edge: { edgeId: string }) => edge.edgeId)
  .filter((edgeId: string) => !representedEdgeIds.has(edgeId));
const bundleIds = new Set(Object.keys(renderModel.bundleLeavesByFakeId));
// Bundling is placement metadata only. Every real leaf remains independently
// visible and therefore participates in the same overlap audit as other tables.
const clearanceTables = renderModel.tables.filter((table) => !table.hidden);
const tableOverlapPairs = [];
for (let leftIndex = 0; leftIndex < clearanceTables.length; leftIndex += 1) {
  const left = clearanceTables[leftIndex];
  for (let rightIndex = leftIndex + 1; rightIndex < clearanceTables.length; rightIndex += 1) {
    const right = clearanceTables[rightIndex];
    if (
      Math.min(left.position.x + left.size.width, right.position.x + right.size.width)
        - Math.max(left.position.x, right.position.x) > 0
      && Math.min(left.position.y + left.size.height, right.position.y + right.size.height)
        - Math.max(left.position.y, right.position.y) > 0
    ) {
      tableOverlapPairs.push({
        left: left.modelId,
        leftBundle: bundleIds.has(left.modelId),
        right: right.modelId,
        rightBundle: bundleIds.has(right.modelId),
      });
    }
  }
}
if (process.argv.includes("--compact")) {
  const semanticTrunks = renderModel.edges
    .filter((edge) => edge.carrierRole === "semantic-trunk")
    .map((edge) => ({
      members: edge.memberEdgeIds?.length ?? 1,
      target: edge.targetModelId,
    }))
    .sort((left, right) => right.members - left.members || left.target.localeCompare(right.target));
  console.log(JSON.stringify({
    adjacentEdgeCrossings,
    coreToCoreCrossings,
    degreeOneToCoreCrossings,
    degreeOneToDegreeOneCrossings,
    directSceneEdgeCrossings: metrics.edgeCrossings
      + (renderModel.semanticCarriers?.semanticCrossingsAvoided ?? 0),
    directSceneEdgeNodeIntersections: metrics.edgeNodeIntersections
      + (renderModel.semanticCarriers?.semanticObstacleIntersectionsAvoided ?? 0),
    directSceneVisualCrossings: metrics.visualCrossings
      + (renderModel.semanticCarriers?.semanticCrossingsAvoided ?? 0)
      + (renderModel.semanticCarriers?.semanticObstacleIntersectionsAvoided ?? 0),
    edgeNodeIntersections: metrics.edgeNodeIntersections,
    exactLayoutEdgeCrossings: exactLayoutMetrics.edgeCrossings,
    exactLayoutEdgeNodeIntersections: exactLayoutMetrics.edgeNodeIntersections,
    exactLayoutNodeOverlaps: exactLayoutMetrics.nodeOverlaps,
    exactLayoutVisualCrossings: exactLayoutMetrics.visualCrossings,
    fallbackRelationships: renderModel.semanticCarriers?.fallbackRelationships ?? 0,
    semanticCarriers: renderModel.semanticCarriers,
    crossingCountByRolePair: Object.fromEntries(
      [...crossingCountByRolePair].sort((left, right) => right[1] - left[1]),
    ),
    sameSourceKindCrossings,
    sameTargetKindCrossings,
    topCrossingEdges: [...crossingCountByEdgeId]
      .map(([edgeId, crossings]) => {
        const edge = renderedEdgeById.get(edgeId);
        return {
          crossings,
          edgeId,
          members: edge?.memberEdgeIds?.length ?? 1,
          role: edge?.carrierRole ?? "direct",
          source: edge?.sourceModelId,
          target: edge?.targetModelId,
        };
      })
      .sort((left, right) => right.crossings - left.crossings || left.edgeId.localeCompare(right.edgeId))
      .slice(0, 30),
    topSourceKindCrossings: [...crossingCountBySourceKind]
      .sort((left, right) => right[1] - left[1])
      .slice(0, 20),
    topTargetKindCrossings: [...crossingCountByTargetKind]
      .sort((left, right) => right[1] - left[1])
      .slice(0, 20),
    semanticTrunkCount: semanticTrunks.length,
    semanticTrunks: semanticTrunks.slice(0, 24),
    missingRoutedEdges: missingRoutedEdgeIds.length,
    nodeOverlaps: metrics.nodeOverlaps,
    nonAdjacentEdgeCrossings,
    nonModelEndpointEdges: nonModelEndpointEdges.length,
    renderedEdges: renderModel.edges.length,
    renderMs,
    syntheticBundleTables: syntheticBundleTables.length,
    tableOverlapPairs: tableOverlapPairs.length,
    tableSizeMismatches,
    visualCrossings: metrics.visualCrossings,
  }));
  process.exit(0);
}
console.log(JSON.stringify({
  diagnostics: renderModel.semanticCarriers,
  adjacentEdgeCrossings,
  edgeMemberReferences: renderModel.edges.reduce(
    (sum, edge) => sum + (edge.memberEdgeIds?.length ?? 1),
    0,
  ),
  maxPointsPerCarrier: Math.max(0, ...pointCounts),
  missingRoutedEdgeIds,
  nonModelEndpointEdges,
  nonAdjacentEdgeCrossings,
  relationshipProxyTables,
  syntheticBundleTables,
  metrics,
  edgeNodeHitSummary,
  edgeNodeHitDetails,
  edgeCrossingPairs,
  edgeCrossingDetails,
  ...(summaryOnly ? {} : { edgeNodeHits: edgeNode.hits }),
  renderedEdges: renderModel.edges.length,
  renderMs,
  tables: renderModel.tables.length,
  tableOverlapPairs,
}, null, 2));

function renderedCrossingPairs(edges: Array<{ edgeId: string; points: string }>) {
  const segments = edges.flatMap((edge) => {
    const points = edge.points.trim().split(/\s+/).flatMap((pair) => {
      const [x, y] = pair.split(",").map(Number);
      return Number.isFinite(x) && Number.isFinite(y) ? [{ x, y }] : [];
    });
    return points.length >= 2 ? [{ edgeId: edge.edgeId, start: points[0], end: points[1] }] : [];
  });
  const pairs = [];
  for (let leftIndex = 0; leftIndex < segments.length; leftIndex += 1) {
    for (let rightIndex = leftIndex + 1; rightIndex < segments.length; rightIndex += 1) {
      const left = segments[leftIndex];
      const right = segments[rightIndex];
      const orientation = (a, b, c) => Math.sign(
        (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x),
      );
      if (
        orientation(left.start, left.end, right.start)
          * orientation(left.start, left.end, right.end) < 0
        && orientation(right.start, right.end, left.start)
          * orientation(right.start, right.end, left.end) < 0
      ) {
        pairs.push([left.edgeId, right.edgeId]);
      }
    }
  }
  return pairs;
}
