import { writeFile } from "node:fs/promises";
import path from "node:path";

import type {
  DiagramGraph,
  GraphNode,
} from "../../../shared/graph/diagramGraph";
import type {
  LayoutEngineMetadata,
  LayoutSnapshot,
  NodeLayout,
  Point,
  RoutedEdgePath,
} from "../../../shared/graph/layoutContract";
import type { DiagramBootstrapPayload } from "../../../shared/protocol/webviewContract";
import type { Logger } from "../logging/logger";

type WeaknessSeverity = "critical" | "info" | "warning";

export interface DiagramAnalysisExportContext {
  acceptedBy?: string;
  bubbleLayout?: boolean;
  clusterGraphLayout?: boolean;
  optimizedLayout?: boolean;
  refreshKind?: "full" | "layout";
  requestId?: number;
}

interface ResolvedDiagramAnalysisExportContext extends DiagramAnalysisExportContext {
  actualAlgorithm?: string;
  actualLayout?: string;
  appliedLayout?: string;
  exportedAt: string;
  layoutEngine?: string;
  layoutStatus?: string;
  requestedLayout?: string;
  source: "extension-accepted-layout" | "manual-or-direct-call";
  strategy?: string;
}

interface Rect {
  bottom: number;
  left: number;
  right: number;
  top: number;
}

interface NodeDegreeRecord {
  appLabel: string;
  clusterId?: string;
  inDegree: number;
  methodInDegree: number;
  methodOutDegree: number;
  methodTotalDegree: number;
  modelId: string;
  modelName: string;
  outDegree: number;
  totalDegree: number;
}

interface TopologyWeakness {
  code: string;
  evidence: Record<string, unknown>;
  message: string;
  severity: WeaknessSeverity;
}

interface TopologyDiagnostics {
  bundleTopology: {
    bundleCount: number;
    leafCount: number;
    maxLeafCount: number;
    topBundles: Array<{
      leafCount: number;
      parentModelId: string;
      sharedRootCount: number;
      sharedRootModelIds: string[];
    }>;
  };
  clusterTopology: {
    clusterCount: number;
    interClusterEdgeRatio: number;
    interClusterEdges: number;
    nodesWithoutCluster: number;
    singletonClusterCount: number;
    singletonClusterRatio: number;
    topClusters: Array<{
      externalEdgeRatio: number;
      externalEdges: number;
      id: string;
      incomingEdges: number;
      internalEdges: number;
      nodeCount: number;
      outgoingEdges: number;
      topCrossEdgeWeight: number;
    }>;
  };
  exportContext: ResolvedDiagramAnalysisExportContext;
  generatedAt: string;
  geometry: {
    estimatedEdgeNodeIntersections: number;
    nodeArea: number;
    nodeAreaCoverage: number;
    nodeBBox: Rect & { area: number; height: number; width: number };
    routeBBox: Rect & { area: number; height: number; width: number };
    topEdgeNodeHitEdges: Array<{
      edgeId: string;
      hitCount: number;
      sourceModelId?: string;
      targetModelId?: string;
    }>;
    topEdgeNodeHitNodes: Array<{
      appLabel?: string;
      clusterId?: string;
      hitCount: number;
      modelId: string;
      modelName?: string;
    }>;
  };
  graphTopology: {
    appCounts: Record<string, number>;
    connectedComponents: {
      count: number;
      isolatedNodeCount: number;
      largest: Array<{ nodeCount: number; sampleModelIds: string[] }>;
      largestComponentRatio: number;
    };
    degree: {
      average: number;
      max: number;
      p50: number;
      p90: number;
      topNodes: NodeDegreeRecord[];
    };
    edgeKindCounts: Record<string, number>;
    multiEdgePairs: Array<{
      count: number;
      sourceModelId: string;
      targetModelId: string;
    }>;
  };
  layoutMetrics: {
    actualAlgorithm?: string;
    edgeCrossings: number;
    edgeNodeIntersections: number;
    edgeSegmentOverlaps: number;
    mode: string;
    nodeOverlaps: number;
    nodeSpacingOverlaps: number;
    overlappingEdges: number;
    rawRouteCrossings?: number;
    routeSegments: number;
    strategy?: string;
    visualCrossings: number;
  };
  routeTopology: {
    averageRouteLength: number;
    bentEdgeCount: number;
    maxRouteLength: number;
    p90RouteLength: number;
    routedEdgeCount: number;
    routeSegmentCount: number;
  };
  schemaVersion: "django-erd.topology-diagnostics.v1";
  summary: {
    methodAssociationCount: number;
    nodeCount: number;
    routedEdgeCount: number;
    structuralEdgeCount: number;
  };
  topCrossEdges: Array<{
    crossings: number;
    sourceClusterId?: string;
    sourceDegree?: number;
    sourceModelId: string;
    targetClusterId?: string;
    targetDegree?: number;
    targetModelId: string;
  }>;
  weaknesses: TopologyWeakness[];
}

interface EdgeNodeHitRecord {
  edgeId: string;
  nodeModelId: string;
  point: Point;
  segmentIndex: number;
}

interface ErdMakerTopologyResult {
  clusters: Array<{
    bbox: Rect & { area: number; center: Point; height: number; width: number };
    id: string;
    internalEdgeCount: number;
    interClusterEdgeCount: number;
    nodeCount: number;
    nodeModelIds: string[];
  }>;
  coordinateSystem: {
    bounds: Rect & { area: number; height: number; width: number };
    origin: "top-left";
    units: "layout-px";
  };
  crossings: Array<{
    edgeIds: [string, string];
    edges: Array<{
      edgeId: string;
      segmentIndex?: number;
      sourceModelId?: string;
      targetModelId?: string;
    }>;
    id: string;
    markerStyle: string;
    position: Point;
  }>;
  diagnostics: TopologyDiagnostics;
  edgeNodeHits: EdgeNodeHitRecord[];
  edges: Array<{
    crossingCount: number;
    crossings: Array<{
      id: string;
      otherEdgeId?: string;
      position: Point;
      segmentIndex?: number;
    }>;
    edgeId: string;
    endPoint?: Point;
    kind?: string;
    length: number;
    pointCount: number;
    points: Point[];
    provenance?: string;
    routeBBox: Rect & { area: number; height: number; width: number };
    segments: Array<{
      bbox: Rect & { area: number; height: number; width: number };
      crossingIds: string[];
      end: Point;
      index: number;
      length: number;
      orientation: "diagonal" | "horizontal" | "point" | "vertical";
      start: Point;
    }>;
    source: {
      clusterId?: string;
      modelId?: string;
      portSide?: "bottom" | "inside" | "left" | "right" | "top";
      rect?: Rect & { area: number; center: Point; height: number; width: number };
    };
    startPoint?: Point;
    target: {
      clusterId?: string;
      modelId?: string;
      portSide?: "bottom" | "inside" | "left" | "right" | "top";
      rect?: Rect & { area: number; center: Point; height: number; width: number };
    };
  }>;
  exportContext: ResolvedDiagramAnalysisExportContext;
  generatedAt: string;
  layout: {
    actualAlgorithm?: string;
    crossingPointCount: number;
    mode: string;
    reportedMetrics: TopologyDiagnostics["layoutMetrics"];
    strategy?: string;
  };
  leafBundles: NonNullable<LayoutEngineMetadata["leafBundles"]>;
  nodes: Array<{
    appLabel?: string;
    center: Point;
    clusterId?: string;
    degree: {
      in: number;
      methodIn: number;
      methodOut: number;
      methodTotal: number;
      out: number;
      total: number;
    };
    incidentEdgeIds: string[];
    modelId: string;
    modelName?: string;
    position: Point;
    rect: Rect & { area: number; center: Point; height: number; width: number };
    size: { height: number; width: number };
  }>;
  schemaVersion: "django-erd-maker.topology-result.v1";
  spatialHotspots: {
    columns: number;
    rows: number;
    topCells: Array<{
      bounds: Rect & { area: number; height: number; width: number };
      col: number;
      crossingCount: number;
      crossingEdges: Array<{ count: number; edgeId: string }>;
      edgeNodeHitCount: number;
      edgeNodeHitEdges: Array<{ count: number; edgeId: string }>;
      edgeNodeHitNodes: Array<{ count: number; modelId: string }>;
      id: string;
      nodeCount: number;
      nodes: Array<{
        appLabel?: string;
        center: Point;
        clusterId?: string;
        degree: number;
        modelId: string;
        modelName?: string;
      }>;
      routeSegmentCount: number;
      routeSegmentEdges: Array<{ count: number; edgeId: string }>;
      row: number;
      sampleCrossings: Array<{
        edgeIds: [string, string];
        id: string;
        position: Point;
      }>;
      sampleEdgeNodeHits: EdgeNodeHitRecord[];
      sampleRouteSegments: Array<{
        edgeId: string;
        end: Point;
        segmentIndex: number;
        start: Point;
      }>;
      score: number;
    }>;
  };
  summary: {
    crossingPointCount: number;
    edgeNodeHitCount: number;
    nodeCount: number;
    routedEdgeCount: number;
    structuralEdgeCount: number;
  };
}

export async function exportDiagramAnalysisArtifacts(
  extensionRootPath: string,
  payload: DiagramBootstrapPayload,
  logger?: Logger,
  context: DiagramAnalysisExportContext = {},
): Promise<void> {
  const exportContext = resolveExportContext(payload, context);

  await writeArtifact(
    path.join(extensionRootPath, "erd-layout-final.json"),
    { exportContext, layout: payload.layout, graph: payload.graph },
    "[export] final layout dumped -> erd-layout-final.json",
    logger,
  );

  const diagnostics = createTopologyDiagnostics(payload, exportContext);

  await writeArtifact(
    path.join(extensionRootPath, "erd-topology-diagnostics.json"),
    diagnostics,
    "[export] topology diagnostics dumped -> erd-topology-diagnostics.json",
    logger,
  );

  await writeArtifact(
    path.join(extensionRootPath, "erdmaker-topology-result.json"),
    createErdMakerTopologyResult(payload, diagnostics, exportContext),
    "[export] topology result dumped -> erdmaker-topology-result.json",
    logger,
  );
}

function resolveExportContext(
  payload: DiagramBootstrapPayload,
  context: DiagramAnalysisExportContext,
): ResolvedDiagramAnalysisExportContext {
  const execution = payload.layoutExecution;
  const metadata = execution?.engineMetadata ?? payload.layout.engineMetadata;
  return {
    ...context,
    actualAlgorithm: metadata?.actualAlgorithm,
    actualLayout: metadata?.actualMode ?? payload.layout.mode,
    appliedLayout: execution?.appliedMode ?? payload.layout.mode,
    exportedAt: new Date().toISOString(),
    layoutEngine: execution?.engine,
    layoutStatus: execution?.status ?? "applied",
    requestedLayout: execution?.requestedMode ?? payload.view.layoutMode,
    source: context.acceptedBy === "extension.refreshLoader"
      ? "extension-accepted-layout"
      : "manual-or-direct-call",
    strategy: metadata?.strategy,
  };
}

function createTopologyDiagnostics(
  payload: DiagramBootstrapPayload,
  exportContext: ResolvedDiagramAnalysisExportContext,
): TopologyDiagnostics {
  const graph = payload.graph;
  const layout = payload.layout;
  const metadata = layout.engineMetadata;
  const graphNodeById = new Map(graph.nodes.map((node) => [node.modelId, node]));
  const clusterByModelId = buildClusterByModelId(layout);
  const degreeRecords = buildDegreeRecords(graph, layout, clusterByModelId);
  const graphTopology = buildGraphTopology(graph, degreeRecords);
  const clusterTopology = buildClusterTopology(
    graph,
    metadata,
    clusterByModelId,
  );
  const bundleTopology = buildBundleTopology(metadata);
  const geometry = buildGeometryDiagnostics(
    layout,
    graph,
    graphNodeById,
    clusterByModelId,
  );
  const routeTopology = buildRouteTopology(layout.routedEdges);
  const layoutMetrics = buildLayoutMetrics(layout, metadata, geometry);
  const topCrossEdges = buildTopCrossEdges(metadata, degreeRecords, clusterByModelId);
  const weaknesses = buildWeaknesses({
    bundleTopology,
    clusterTopology,
    geometry,
    graphTopology,
    layoutMetrics,
    routeTopology,
  });

  return {
    bundleTopology,
    clusterTopology,
    exportContext,
    generatedAt: exportContext.exportedAt,
    geometry,
    graphTopology,
    layoutMetrics,
    routeTopology,
    schemaVersion: "django-erd.topology-diagnostics.v1",
    summary: {
      methodAssociationCount: graph.methodAssociations.length,
      nodeCount: graph.nodes.length,
      routedEdgeCount: layout.routedEdges.length,
      structuralEdgeCount: graph.structuralEdges.length,
    },
    topCrossEdges,
    weaknesses,
  };
}

function createErdMakerTopologyResult(
  payload: DiagramBootstrapPayload,
  diagnostics: TopologyDiagnostics,
  exportContext: ResolvedDiagramAnalysisExportContext,
): ErdMakerTopologyResult {
  const graph = payload.graph;
  const layout = payload.layout;
  const metadata = layout.engineMetadata;
  const graphNodeById = new Map(graph.nodes.map((node) => [node.modelId, node]));
  const layoutNodeById = new Map(layout.nodes.map((node) => [node.modelId, node]));
  const edgeById = new Map(graph.structuralEdges.map((edge) => [edge.id, edge]));
  const routeByEdgeId = new Map(layout.routedEdges.map((route) => [route.edgeId, route]));
  const crossingById = new Map(layout.crossings.map((crossing) => [crossing.id, crossing]));
  const clusterByModelId = buildClusterByModelId(layout);
  const degreeRecords = buildDegreeRecords(graph, layout, clusterByModelId);
  const edgeNodeHits = collectEdgeNodeHitRecords(layout, graph);
  const coordinateBounds = mergeRects(
    diagnostics.geometry.nodeBBox,
    diagnostics.geometry.routeBBox,
  );

  return {
    clusters: buildTopologyResultClusters(
      layout,
      clusterByModelId,
      edgeById,
    ),
    coordinateSystem: {
      bounds: coordinateBounds,
      origin: "top-left",
      units: "layout-px",
    },
    crossings: buildTopologyResultCrossings(
      layout,
      edgeById,
      routeByEdgeId,
    ),
    diagnostics,
    edgeNodeHits,
    edges: buildTopologyResultEdges({
      crossingById,
      edgeById,
      layout,
      layoutNodeById,
    }),
    exportContext,
    generatedAt: exportContext.exportedAt,
    layout: {
      actualAlgorithm: metadata?.actualAlgorithm,
      crossingPointCount: layout.crossings.length,
      mode: layout.mode,
      reportedMetrics: diagnostics.layoutMetrics,
      strategy: metadata?.strategy,
    },
    leafBundles: metadata?.leafBundles ?? [],
    nodes: buildTopologyResultNodes({
      clusterByModelId,
      degreeRecords,
      graph,
      graphNodeById,
      layout,
    }),
    schemaVersion: "django-erd-maker.topology-result.v1",
    spatialHotspots: buildSpatialHotspots(
      layout.nodes,
      layout.routedEdges,
      layout.crossings,
      edgeNodeHits,
      degreeRecords,
      graphNodeById,
      coordinateBounds,
    ),
    summary: {
      crossingPointCount: layout.crossings.length,
      edgeNodeHitCount: edgeNodeHits.length,
      nodeCount: layout.nodes.length,
      routedEdgeCount: layout.routedEdges.length,
      structuralEdgeCount: graph.structuralEdges.length,
    },
  };
}

function buildTopologyResultNodes(input: {
  clusterByModelId: Map<string, string>;
  degreeRecords: Map<string, NodeDegreeRecord>;
  graph: DiagramGraph;
  graphNodeById: Map<string, GraphNode>;
  layout: LayoutSnapshot;
}): ErdMakerTopologyResult["nodes"] {
  const incidentEdgeIds = new Map<string, string[]>();
  for (const edge of input.graph.structuralEdges) {
    pushMapArray(incidentEdgeIds, edge.sourceModelId, edge.id);
    pushMapArray(incidentEdgeIds, edge.targetModelId, edge.id);
  }

  return input.layout.nodes
    .map((node) => {
      const graphNode = input.graphNodeById.get(node.modelId);
      const degree = input.degreeRecords.get(node.modelId);
      const rect = enrichRect(nodeRect(node));
      return {
        appLabel: graphNode?.appLabel,
        center: rect.center,
        clusterId: input.clusterByModelId.get(node.modelId) ?? node.clusterId,
        degree: {
          in: degree?.inDegree ?? 0,
          methodIn: degree?.methodInDegree ?? 0,
          methodOut: degree?.methodOutDegree ?? 0,
          methodTotal: degree?.methodTotalDegree ?? 0,
          out: degree?.outDegree ?? 0,
          total: degree?.totalDegree ?? 0,
        },
        incidentEdgeIds: incidentEdgeIds.get(node.modelId) ?? [],
        modelId: node.modelId,
        modelName: graphNode?.modelName,
        position: node.position,
        rect,
        size: node.size,
      };
    })
    .sort((left, right) => left.modelId.localeCompare(right.modelId));
}

function buildTopologyResultEdges(input: {
  crossingById: Map<string, LayoutSnapshot["crossings"][number]>;
  edgeById: Map<string, DiagramGraph["structuralEdges"][number]>;
  layout: LayoutSnapshot;
  layoutNodeById: Map<string, NodeLayout>;
}): ErdMakerTopologyResult["edges"] {
  return input.layout.routedEdges
    .map((route) => {
      const graphEdge = input.edgeById.get(route.edgeId);
      const sourceNode = graphEdge
        ? input.layoutNodeById.get(graphEdge.sourceModelId)
        : undefined;
      const targetNode = graphEdge
        ? input.layoutNodeById.get(graphEdge.targetModelId)
        : undefined;
      const sourceRect = sourceNode ? enrichRect(nodeRect(sourceNode)) : undefined;
      const targetRect = targetNode ? enrichRect(nodeRect(targetNode)) : undefined;
      const crossingRecords = route.crossingIds
        .map((id) => input.crossingById.get(id))
        .filter((crossing): crossing is LayoutSnapshot["crossings"][number] =>
          crossing !== undefined,
        )
        .map((crossing) => ({
          id: crossing.id,
          otherEdgeId: crossing.edgeIds.find((edgeId) => edgeId !== route.edgeId),
          position: crossing.position,
          segmentIndex: findSegmentIndexForPoint(route.points, crossing.position),
        }));
      const segmentCrossingIds = new Map<number, string[]>();
      for (const crossing of crossingRecords) {
        if (crossing.segmentIndex !== undefined) {
          pushMapArray(segmentCrossingIds, crossing.segmentIndex, crossing.id);
        }
      }

      return {
        crossingCount: route.crossingIds.length,
        crossings: crossingRecords,
        edgeId: route.edgeId,
        endPoint: route.points.at(-1),
        kind: graphEdge?.kind,
        length: routeLength(route.points),
        pointCount: route.points.length,
        points: route.points,
        provenance: graphEdge?.provenance,
        routeBBox: bboxFromPoints(route.points),
        segments: buildRouteSegments(route.points, segmentCrossingIds),
        source: {
          clusterId: sourceNode?.clusterId,
          modelId: graphEdge?.sourceModelId,
          portSide: sourceRect && route.points[0]
            ? classifyPortSide(route.points[0], sourceRect)
            : undefined,
          rect: sourceRect,
        },
        startPoint: route.points[0],
        target: {
          clusterId: targetNode?.clusterId,
          modelId: graphEdge?.targetModelId,
          portSide: targetRect && route.points.at(-1)
            ? classifyPortSide(route.points.at(-1) as Point, targetRect)
            : undefined,
          rect: targetRect,
        },
      };
    })
    .sort((left, right) =>
      right.crossingCount - left.crossingCount
      || right.length - left.length
      || left.edgeId.localeCompare(right.edgeId),
    );
}

function buildTopologyResultCrossings(
  layout: LayoutSnapshot,
  edgeById: Map<string, DiagramGraph["structuralEdges"][number]>,
  routeByEdgeId: Map<string, RoutedEdgePath>,
): ErdMakerTopologyResult["crossings"] {
  return layout.crossings.map((crossing) => ({
    edgeIds: crossing.edgeIds,
    edges: crossing.edgeIds.map((edgeId) => {
      const edge = edgeById.get(edgeId);
      const route = routeByEdgeId.get(edgeId);
      return {
        edgeId,
        segmentIndex: route
          ? findSegmentIndexForPoint(route.points, crossing.position)
          : undefined,
        sourceModelId: edge?.sourceModelId,
        targetModelId: edge?.targetModelId,
      };
    }),
    id: crossing.id,
    markerStyle: crossing.markerStyle,
    position: crossing.position,
  }));
}

function collectEdgeNodeHitRecords(
  layout: LayoutSnapshot,
  graph: DiagramGraph,
): EdgeNodeHitRecord[] {
  const edgeById = new Map(graph.structuralEdges.map((edge) => [edge.id, edge]));
  const hits: EdgeNodeHitRecord[] = [];
  for (const route of layout.routedEdges) {
    const edge = edgeById.get(route.edgeId);
    for (let i = 1; i < route.points.length; i += 1) {
      const start = route.points[i - 1];
      const end = route.points[i];
      for (const node of layout.nodes) {
        if (
          edge
          && (node.modelId === edge.sourceModelId || node.modelId === edge.targetModelId)
        ) {
          continue;
        }
        const rect = nodeRect(node);
        if (segmentIntersectsRect(start, end, rect)) {
          hits.push({
            edgeId: route.edgeId,
            nodeModelId: node.modelId,
            point: closestPointOnSegment(enrichRect(rect).center, start, end),
            segmentIndex: i - 1,
          });
        }
      }
    }
  }
  return hits.sort((left, right) =>
    left.edgeId.localeCompare(right.edgeId)
    || left.segmentIndex - right.segmentIndex
    || left.nodeModelId.localeCompare(right.nodeModelId),
  );
}

function buildTopologyResultClusters(
  layout: LayoutSnapshot,
  clusterByModelId: Map<string, string>,
  edgeById: Map<string, DiagramGraph["structuralEdges"][number]>,
): ErdMakerTopologyResult["clusters"] {
  const nodesByCluster = new Map<string, NodeLayout[]>();
  for (const node of layout.nodes) {
    const clusterId = clusterByModelId.get(node.modelId) ?? "__unclustered";
    pushMapArray(nodesByCluster, clusterId, node);
  }

  const internalEdgeCount = new Map<string, number>();
  const interClusterEdgeCount = new Map<string, number>();
  for (const edge of edgeById.values()) {
    const sourceCluster = clusterByModelId.get(edge.sourceModelId) ?? "__unclustered";
    const targetCluster = clusterByModelId.get(edge.targetModelId) ?? "__unclustered";
    if (sourceCluster === targetCluster) {
      internalEdgeCount.set(sourceCluster, (internalEdgeCount.get(sourceCluster) ?? 0) + 1);
    } else {
      interClusterEdgeCount.set(sourceCluster, (interClusterEdgeCount.get(sourceCluster) ?? 0) + 1);
      interClusterEdgeCount.set(targetCluster, (interClusterEdgeCount.get(targetCluster) ?? 0) + 1);
    }
  }

  return [...nodesByCluster.entries()]
    .map(([id, nodes]) => ({
      bbox: bboxFromRects(nodes.map((node) => nodeRect(node))),
      id,
      internalEdgeCount: internalEdgeCount.get(id) ?? 0,
      interClusterEdgeCount: interClusterEdgeCount.get(id) ?? 0,
      nodeCount: nodes.length,
      nodeModelIds: nodes.map((node) => node.modelId).sort(),
    }))
    .sort((left, right) =>
      right.nodeCount - left.nodeCount
      || right.interClusterEdgeCount - left.interClusterEdgeCount
      || left.id.localeCompare(right.id),
    );
}

function buildRouteSegments(
  points: Point[],
  crossingIdsBySegment: Map<number, string[]>,
): ErdMakerTopologyResult["edges"][number]["segments"] {
  const segments: ErdMakerTopologyResult["edges"][number]["segments"] = [];
  for (let i = 1; i < points.length; i += 1) {
    const start = points[i - 1];
    const end = points[i];
    segments.push({
      bbox: bboxFromPoints([start, end]),
      crossingIds: crossingIdsBySegment.get(i - 1) ?? [],
      end,
      index: i - 1,
      length: Math.hypot(end.x - start.x, end.y - start.y),
      orientation: segmentOrientation(start, end),
      start,
    });
  }
  return segments;
}

function buildSpatialHotspots(
  nodes: NodeLayout[],
  routedEdges: RoutedEdgePath[],
  crossings: LayoutSnapshot["crossings"],
  edgeNodeHits: EdgeNodeHitRecord[],
  degreeRecords: Map<string, NodeDegreeRecord>,
  graphNodeById: Map<string, GraphNode>,
  bounds: Rect & { area: number; height: number; width: number },
): ErdMakerTopologyResult["spatialHotspots"] {
  type MutableHotspotCell = {
    bounds: Rect & { area: number; center: Point; height: number; width: number };
    col: number;
    crossingCount: number;
    crossingEdgeCounts: Map<string, number>;
    edgeNodeHitCount: number;
    edgeNodeHitEdgeCounts: Map<string, number>;
    edgeNodeHitNodeCounts: Map<string, number>;
    id: string;
    nodeCount: number;
    nodes: NodeLayout[];
    routeSegmentCount: number;
    routeSegmentEdgeCounts: Map<string, number>;
    row: number;
    sampleCrossings: Array<{
      edgeIds: [string, string];
      id: string;
      position: Point;
    }>;
    sampleEdgeNodeHits: EdgeNodeHitRecord[];
    sampleRouteSegments: Array<{
      edgeId: string;
      end: Point;
      segmentIndex: number;
      start: Point;
    }>;
    score: number;
  };

  const columns = 24;
  const rows = 18;
  const cellWidth = bounds.width > 0 ? bounds.width / columns : 1;
  const cellHeight = bounds.height > 0 ? bounds.height / rows : 1;
  const cells: MutableHotspotCell[] = Array.from({ length: columns * rows }, (_, index) => {
    const row = Math.floor(index / columns);
    const col = index % columns;
    const left = bounds.left + col * cellWidth;
    const top = bounds.top + row * cellHeight;
    return {
      bounds: enrichRect({
        bottom: top + cellHeight,
        left,
        right: left + cellWidth,
        top,
      }),
      col,
      crossingCount: 0,
      crossingEdgeCounts: new Map<string, number>(),
      edgeNodeHitCount: 0,
      edgeNodeHitEdgeCounts: new Map<string, number>(),
      edgeNodeHitNodeCounts: new Map<string, number>(),
      id: `${col},${row}`,
      nodeCount: 0,
      nodes: [],
      routeSegmentCount: 0,
      routeSegmentEdgeCounts: new Map<string, number>(),
      row,
      sampleCrossings: [],
      sampleEdgeNodeHits: [],
      sampleRouteSegments: [],
      score: 0,
    };
  });
  const getCell = (col: number, row: number) => cells[row * columns + col];
  const locatePoint = (point: Point): { col: number; row: number } => ({
    col: clampInt(Math.floor((point.x - bounds.left) / cellWidth), 0, columns - 1),
    row: clampInt(Math.floor((point.y - bounds.top) / cellHeight), 0, rows - 1),
  });

  for (const node of nodes) {
    const cell = locatePoint(enrichRect(nodeRect(node)).center);
    const hotspot = getCell(cell.col, cell.row);
    hotspot.nodeCount += 1;
    hotspot.nodes.push(node);
  }
  for (const crossing of crossings) {
    const cell = locatePoint(crossing.position);
    const hotspot = getCell(cell.col, cell.row);
    hotspot.crossingCount += 1;
    for (const edgeId of crossing.edgeIds) {
      incrementCount(hotspot.crossingEdgeCounts, edgeId);
    }
    if (hotspot.sampleCrossings.length < 20) {
      hotspot.sampleCrossings.push({
        edgeIds: crossing.edgeIds,
        id: crossing.id,
        position: crossing.position,
      });
    }
  }
  for (const hit of edgeNodeHits) {
    const cell = locatePoint(hit.point);
    const hotspot = getCell(cell.col, cell.row);
    hotspot.edgeNodeHitCount += 1;
    incrementCount(hotspot.edgeNodeHitEdgeCounts, hit.edgeId);
    incrementCount(hotspot.edgeNodeHitNodeCounts, hit.nodeModelId);
    if (hotspot.sampleEdgeNodeHits.length < 20) {
      hotspot.sampleEdgeNodeHits.push(hit);
    }
  }
  for (const route of routedEdges) {
    for (let i = 1; i < route.points.length; i += 1) {
      const start = route.points[i - 1];
      const end = route.points[i];
      const segmentBox = bboxFromPoints([start, end]);
      const min = locatePoint({ x: segmentBox.left, y: segmentBox.top });
      const max = locatePoint({ x: segmentBox.right, y: segmentBox.bottom });
      for (let row = min.row; row <= max.row; row += 1) {
        for (let col = min.col; col <= max.col; col += 1) {
          const cell = getCell(col, row);
          if (segmentIntersectsRect(start, end, cell.bounds)) {
            cell.routeSegmentCount += 1;
            incrementCount(cell.routeSegmentEdgeCounts, route.edgeId);
            if (cell.sampleRouteSegments.length < 20) {
              cell.sampleRouteSegments.push({
                edgeId: route.edgeId,
                end,
                segmentIndex: i - 1,
                start,
              });
            }
          }
        }
      }
    }
  }

  for (const cell of cells) {
    cell.score =
      cell.crossingCount * 10
      + cell.edgeNodeHitCount * 5
      + cell.nodeCount * 2
      + cell.routeSegmentCount;
  }

  return {
    columns,
    rows,
    topCells: cells
      .filter((cell) => cell.score > 0)
      .sort((left, right) =>
        right.score - left.score
        || right.crossingCount - left.crossingCount
        || right.edgeNodeHitCount - left.edgeNodeHitCount
        || right.routeSegmentCount - left.routeSegmentCount,
      )
      .slice(0, 60)
      .map((cell) => ({
        bounds: cell.bounds,
        col: cell.col,
        crossingCount: cell.crossingCount,
        crossingEdges: topCountEntries(cell.crossingEdgeCounts, 20)
          .map((entry) => ({ count: entry.count, edgeId: entry.id })),
        edgeNodeHitCount: cell.edgeNodeHitCount,
        edgeNodeHitEdges: topCountEntries(cell.edgeNodeHitEdgeCounts, 20)
          .map((entry) => ({ count: entry.count, edgeId: entry.id })),
        edgeNodeHitNodes: topCountEntries(cell.edgeNodeHitNodeCounts, 20)
          .map((entry) => ({ count: entry.count, modelId: entry.id })),
        id: cell.id,
        nodeCount: cell.nodeCount,
        nodes: cell.nodes
          .map((node) => {
            const graphNode = graphNodeById.get(node.modelId);
            const degree = degreeRecords.get(node.modelId);
            return {
              appLabel: graphNode?.appLabel,
              center: enrichRect(nodeRect(node)).center,
              clusterId: degree?.clusterId ?? node.clusterId,
              degree: degree?.totalDegree ?? 0,
              modelId: node.modelId,
              modelName: graphNode?.modelName,
            };
          })
          .sort((left, right) =>
            right.degree - left.degree
            || left.modelId.localeCompare(right.modelId),
          )
          .slice(0, 20),
        routeSegmentCount: cell.routeSegmentCount,
        routeSegmentEdges: topCountEntries(cell.routeSegmentEdgeCounts, 20)
          .map((entry) => ({ count: entry.count, edgeId: entry.id })),
        row: cell.row,
        sampleCrossings: cell.sampleCrossings,
        sampleEdgeNodeHits: cell.sampleEdgeNodeHits,
        sampleRouteSegments: cell.sampleRouteSegments,
        score: cell.score,
      })),
  };
}

function incrementCount(records: Map<string, number>, key: string): void {
  records.set(key, (records.get(key) ?? 0) + 1);
}

function topCountEntries(
  records: Map<string, number>,
  limit: number,
): Array<{ count: number; id: string }> {
  return [...records.entries()]
    .map(([id, count]) => ({ count, id }))
    .sort((left, right) => right.count - left.count || left.id.localeCompare(right.id))
    .slice(0, limit);
}

async function writeArtifact(
  outputPath: string,
  value: unknown,
  successMessage: string,
  logger?: Logger,
): Promise<void> {
  try {
    await writeFile(outputPath, JSON.stringify(value, null, 2), "utf8");
    logger?.info(successMessage);
  } catch (error) {
    const msg = error instanceof Error ? error.message : String(error);
    logger?.warn(`[export] failed to write ${path.basename(outputPath)}: ${msg}`);
  }
}

function buildClusterByModelId(layout: LayoutSnapshot): Map<string, string> {
  const clusterByModelId = new Map<string, string>();
  const metadataClusters = layout.engineMetadata?.clusterByModelId ?? {};
  for (const [modelId, clusterId] of Object.entries(metadataClusters)) {
    if (typeof clusterId === "string" && clusterId.length > 0) {
      clusterByModelId.set(modelId, clusterId);
    }
  }
  for (const node of layout.nodes) {
    if (node.clusterId && !clusterByModelId.has(node.modelId)) {
      clusterByModelId.set(node.modelId, node.clusterId);
    }
  }
  return clusterByModelId;
}

function buildDegreeRecords(
  graph: DiagramGraph,
  layout: LayoutSnapshot,
  clusterByModelId: Map<string, string>,
): Map<string, NodeDegreeRecord> {
  const records = new Map<string, NodeDegreeRecord>();
  for (const node of graph.nodes) {
    records.set(node.modelId, createDegreeRecord(node, clusterByModelId));
  }
  for (const node of layout.nodes) {
    if (!records.has(node.modelId)) {
      records.set(node.modelId, createDegreeRecord({
        appLabel: node.modelId.split(".")[0] ?? "",
        modelId: node.modelId,
        modelName: node.modelId.split(".").slice(1).join(".") || node.modelId,
      }, clusterByModelId));
    }
  }
  for (const edge of graph.structuralEdges) {
    const source = ensureDegreeRecord(records, edge.sourceModelId, clusterByModelId);
    const target = ensureDegreeRecord(records, edge.targetModelId, clusterByModelId);
    source.outDegree += 1;
    source.totalDegree += 1;
    target.inDegree += 1;
    target.totalDegree += 1;
  }
  for (const association of graph.methodAssociations) {
    const source = ensureDegreeRecord(
      records,
      association.sourceModelId,
      clusterByModelId,
    );
    const target = ensureDegreeRecord(
      records,
      association.targetModelId,
      clusterByModelId,
    );
    source.methodOutDegree += 1;
    source.methodTotalDegree += 1;
    target.methodInDegree += 1;
    target.methodTotalDegree += 1;
  }
  return records;
}

function createDegreeRecord(
  node: { appLabel: string; modelId: string; modelName: string },
  clusterByModelId: Map<string, string>,
): NodeDegreeRecord {
  return {
    appLabel: node.appLabel,
    clusterId: clusterByModelId.get(node.modelId),
    inDegree: 0,
    methodInDegree: 0,
    methodOutDegree: 0,
    methodTotalDegree: 0,
    modelId: node.modelId,
    modelName: node.modelName,
    outDegree: 0,
    totalDegree: 0,
  };
}

function ensureDegreeRecord(
  records: Map<string, NodeDegreeRecord>,
  modelId: string,
  clusterByModelId: Map<string, string>,
): NodeDegreeRecord {
  const existing = records.get(modelId);
  if (existing) return existing;
  const created = createDegreeRecord({
    appLabel: modelId.split(".")[0] ?? "",
    modelId,
    modelName: modelId.split(".").slice(1).join(".") || modelId,
  }, clusterByModelId);
  records.set(modelId, created);
  return created;
}

function buildGraphTopology(
  graph: DiagramGraph,
  degreeRecords: Map<string, NodeDegreeRecord>,
): TopologyDiagnostics["graphTopology"] {
  const degreeValues = [...degreeRecords.values()]
    .map((record) => record.totalDegree)
    .sort(sortNumberAscending);
  const appCounts: Record<string, number> = {};
  for (const node of graph.nodes) {
    appCounts[node.appLabel] = (appCounts[node.appLabel] ?? 0) + 1;
  }

  const edgeKindCounts: Record<string, number> = {};
  const pairCounts = new Map<string, {
    count: number;
    sourceModelId: string;
    targetModelId: string;
  }>();
  for (const edge of graph.structuralEdges) {
    edgeKindCounts[edge.kind] = (edgeKindCounts[edge.kind] ?? 0) + 1;
    const key = `${edge.sourceModelId}->${edge.targetModelId}`;
    const existing = pairCounts.get(key);
    if (existing) {
      existing.count += 1;
    } else {
      pairCounts.set(key, {
        count: 1,
        sourceModelId: edge.sourceModelId,
        targetModelId: edge.targetModelId,
      });
    }
  }

  return {
    appCounts: sortRecordByValueDesc(appCounts),
    connectedComponents: buildConnectedComponents(graph),
    degree: {
      average: average(degreeValues),
      max: degreeValues.at(-1) ?? 0,
      p50: percentileSorted(degreeValues, 0.5),
      p90: percentileSorted(degreeValues, 0.9),
      topNodes: [...degreeRecords.values()]
        .sort((left, right) => right.totalDegree - left.totalDegree)
        .slice(0, 25),
    },
    edgeKindCounts: sortRecordByValueDesc(edgeKindCounts),
    multiEdgePairs: [...pairCounts.values()]
      .filter((pair) => pair.count > 1)
      .sort((left, right) => right.count - left.count)
      .slice(0, 25),
  };
}

function buildConnectedComponents(
  graph: DiagramGraph,
): TopologyDiagnostics["graphTopology"]["connectedComponents"] {
  const parent = new Map<string, string>();
  const size = new Map<string, number>();
  for (const node of graph.nodes) {
    parent.set(node.modelId, node.modelId);
    size.set(node.modelId, 1);
  }
  for (const edge of graph.structuralEdges) {
    union(parent, size, edge.sourceModelId, edge.targetModelId);
  }

  const groups = new Map<string, string[]>();
  for (const node of graph.nodes) {
    const root = find(parent, node.modelId);
    const group = groups.get(root);
    if (group) {
      group.push(node.modelId);
    } else {
      groups.set(root, [node.modelId]);
    }
  }
  const components = [...groups.values()]
    .sort((left, right) => right.length - left.length);
  const largestSize = components[0]?.length ?? 0;
  return {
    count: components.length,
    isolatedNodeCount: components.filter((component) => component.length === 1).length,
    largest: components.slice(0, 10).map((component) => ({
      nodeCount: component.length,
      sampleModelIds: component.slice(0, 12),
    })),
    largestComponentRatio: safeRatio(largestSize, graph.nodes.length),
  };
}

function find(parent: Map<string, string>, value: string): string {
  const current = parent.get(value);
  if (!current) {
    parent.set(value, value);
    return value;
  }
  if (current === value) return current;
  const root = find(parent, current);
  parent.set(value, root);
  return root;
}

function union(
  parent: Map<string, string>,
  size: Map<string, number>,
  left: string,
  right: string,
): void {
  const leftRoot = find(parent, left);
  const rightRoot = find(parent, right);
  if (leftRoot === rightRoot) return;
  const leftSize = size.get(leftRoot) ?? 1;
  const rightSize = size.get(rightRoot) ?? 1;
  if (leftSize < rightSize) {
    parent.set(leftRoot, rightRoot);
    size.set(rightRoot, leftSize + rightSize);
  } else {
    parent.set(rightRoot, leftRoot);
    size.set(leftRoot, leftSize + rightSize);
  }
}

function buildClusterTopology(
  graph: DiagramGraph,
  metadata: LayoutEngineMetadata | undefined,
  clusterByModelId: Map<string, string>,
): TopologyDiagnostics["clusterTopology"] {
  const clusters = new Map<string, {
    id: string;
    incomingEdges: number;
    internalEdges: number;
    nodeCount: number;
    outgoingEdges: number;
    topCrossEdgeWeight: number;
  }>();
  for (const clusterId of clusterByModelId.values()) {
    const cluster = ensureCluster(clusters, clusterId);
    cluster.nodeCount += 1;
  }

  let interClusterEdges = 0;
  for (const edge of graph.structuralEdges) {
    const sourceCluster = clusterByModelId.get(edge.sourceModelId);
    const targetCluster = clusterByModelId.get(edge.targetModelId);
    if (!sourceCluster || !targetCluster) continue;
    const source = ensureCluster(clusters, sourceCluster);
    const target = ensureCluster(clusters, targetCluster);
    if (sourceCluster === targetCluster) {
      source.internalEdges += 1;
    } else {
      interClusterEdges += 1;
      source.outgoingEdges += 1;
      target.incomingEdges += 1;
    }
  }

  for (const topEdge of metadata?.topCrossEdges ?? []) {
    const sourceCluster = clusterByModelId.get(topEdge.sourceModelId);
    const targetCluster = clusterByModelId.get(topEdge.targetModelId);
    if (sourceCluster) {
      ensureCluster(clusters, sourceCluster).topCrossEdgeWeight += topEdge.crossings;
    }
    if (targetCluster && targetCluster !== sourceCluster) {
      ensureCluster(clusters, targetCluster).topCrossEdgeWeight += topEdge.crossings;
    }
  }

  const clusterRows = [...clusters.values()].map((cluster) => {
    const externalEdges = cluster.incomingEdges + cluster.outgoingEdges;
    return {
      externalEdgeRatio: safeRatio(
        externalEdges,
        externalEdges + cluster.internalEdges,
      ),
      externalEdges,
      id: cluster.id,
      incomingEdges: cluster.incomingEdges,
      internalEdges: cluster.internalEdges,
      nodeCount: cluster.nodeCount,
      outgoingEdges: cluster.outgoingEdges,
      topCrossEdgeWeight: cluster.topCrossEdgeWeight,
    };
  });
  const singletonClusterCount = clusterRows.filter((cluster) => cluster.nodeCount === 1).length;

  return {
    clusterCount: clusterRows.length,
    interClusterEdgeRatio: safeRatio(interClusterEdges, graph.structuralEdges.length),
    interClusterEdges,
    nodesWithoutCluster: graph.nodes.filter(
      (node) => !clusterByModelId.has(node.modelId),
    ).length,
    singletonClusterCount,
    singletonClusterRatio: safeRatio(singletonClusterCount, clusterRows.length),
    topClusters: clusterRows
      .sort((left, right) =>
        right.externalEdges - left.externalEdges
        || right.nodeCount - left.nodeCount
      )
      .slice(0, 25),
  };
}

function ensureCluster(
  clusters: Map<string, {
    id: string;
    incomingEdges: number;
    internalEdges: number;
    nodeCount: number;
    outgoingEdges: number;
    topCrossEdgeWeight: number;
  }>,
  id: string,
) {
  const existing = clusters.get(id);
  if (existing) return existing;
  const created = {
    id,
    incomingEdges: 0,
    internalEdges: 0,
    nodeCount: 0,
    outgoingEdges: 0,
    topCrossEdgeWeight: 0,
  };
  clusters.set(id, created);
  return created;
}

function buildBundleTopology(
  metadata: LayoutEngineMetadata | undefined,
): TopologyDiagnostics["bundleTopology"] {
  const bundles = metadata?.leafBundles ?? [];
  const topBundles = bundles
    .map((bundle) => ({
      leafCount: bundle.leafModelIds.length,
      parentModelId: bundle.parentModelId,
      sharedRootCount: bundle.sharedRootModelIds?.length ?? 0,
      sharedRootModelIds: bundle.sharedRootModelIds ?? [],
    }))
    .sort((left, right) => right.leafCount - left.leafCount)
    .slice(0, 25);
  return {
    bundleCount: bundles.length,
    leafCount: bundles.reduce(
      (sum, bundle) => sum + bundle.leafModelIds.length,
      0,
    ),
    maxLeafCount: topBundles[0]?.leafCount ?? 0,
    topBundles,
  };
}

function buildGeometryDiagnostics(
  layout: LayoutSnapshot,
  graph: DiagramGraph,
  graphNodeById: Map<string, GraphNode>,
  clusterByModelId: Map<string, string>,
): TopologyDiagnostics["geometry"] {
  const nodeBBox = computeNodeBBox(layout.nodes);
  const routeBBox = computeRouteBBox(layout.routedEdges, nodeBBox);
  const nodeArea = layout.nodes.reduce(
    (sum, node) => sum + node.size.width * node.size.height,
    0,
  );
  const edgeNodeHits = estimateEdgeNodeHits(layout, graph);

  return {
    estimatedEdgeNodeIntersections: edgeNodeHits.totalHits,
    nodeArea,
    nodeAreaCoverage: safeRatio(nodeArea, nodeBBox.area),
    nodeBBox,
    routeBBox,
    topEdgeNodeHitEdges: edgeNodeHits.topEdges,
    topEdgeNodeHitNodes: edgeNodeHits.topNodes.map((row) => {
      const graphNode = graphNodeById.get(row.modelId);
      return {
        appLabel: graphNode?.appLabel,
        clusterId: clusterByModelId.get(row.modelId),
        hitCount: row.hitCount,
        modelId: row.modelId,
        modelName: graphNode?.modelName,
      };
    }),
  };
}

function estimateEdgeNodeHits(
  layout: LayoutSnapshot,
  graph: DiagramGraph,
): {
  topEdges: Array<{
    edgeId: string;
    hitCount: number;
    sourceModelId?: string;
    targetModelId?: string;
  }>;
  topNodes: Array<{ hitCount: number; modelId: string }>;
  totalHits: number;
} {
  const edgeById = new Map(graph.structuralEdges.map((edge) => [edge.id, edge]));
  const nodeHits = new Map<string, number>();
  const edgeHits = new Map<string, number>();
  let totalHits = 0;

  for (const route of layout.routedEdges) {
    const edge = edgeById.get(route.edgeId);
    if (route.points.length < 2) continue;
    for (let i = 1; i < route.points.length; i += 1) {
      const start = route.points[i - 1];
      const end = route.points[i];
      for (const node of layout.nodes) {
        if (
          edge
          && (node.modelId === edge.sourceModelId || node.modelId === edge.targetModelId)
        ) {
          continue;
        }
        if (segmentIntersectsRect(start, end, nodeRect(node))) {
          totalHits += 1;
          nodeHits.set(node.modelId, (nodeHits.get(node.modelId) ?? 0) + 1);
          edgeHits.set(route.edgeId, (edgeHits.get(route.edgeId) ?? 0) + 1);
        }
      }
    }
  }

  return {
    topEdges: [...edgeHits.entries()]
      .map(([edgeId, hitCount]) => {
        const edge = edgeById.get(edgeId);
        return {
          edgeId,
          hitCount,
          sourceModelId: edge?.sourceModelId,
          targetModelId: edge?.targetModelId,
        };
      })
      .sort((left, right) => right.hitCount - left.hitCount)
      .slice(0, 25),
    topNodes: [...nodeHits.entries()]
      .map(([modelId, hitCount]) => ({ hitCount, modelId }))
      .sort((left, right) => right.hitCount - left.hitCount)
      .slice(0, 25),
    totalHits,
  };
}

function buildRouteTopology(
  routedEdges: RoutedEdgePath[],
): TopologyDiagnostics["routeTopology"] {
  const lengths: number[] = [];
  let routeSegmentCount = 0;
  let bentEdgeCount = 0;
  for (const edge of routedEdges) {
    routeSegmentCount += Math.max(0, edge.points.length - 1);
    if (edge.points.length > 2) bentEdgeCount += 1;
    lengths.push(routeLength(edge.points));
  }
  const sortedLengths = lengths.sort(sortNumberAscending);
  return {
    averageRouteLength: average(sortedLengths),
    bentEdgeCount,
    maxRouteLength: sortedLengths.at(-1) ?? 0,
    p90RouteLength: percentileSorted(sortedLengths, 0.9),
    routedEdgeCount: routedEdges.length,
    routeSegmentCount,
  };
}

function buildLayoutMetrics(
  layout: LayoutSnapshot,
  metadata: LayoutEngineMetadata | undefined,
  geometry: TopologyDiagnostics["geometry"],
): TopologyDiagnostics["layoutMetrics"] {
  return {
    actualAlgorithm: metadata?.actualAlgorithm,
    edgeCrossings: metadata?.edgeCrossings ?? layout.crossings.length,
    edgeNodeIntersections:
      metadata?.edgeNodeIntersections ?? geometry.estimatedEdgeNodeIntersections,
    edgeSegmentOverlaps: metadata?.edgeSegmentOverlaps ?? 0,
    mode: layout.mode,
    nodeOverlaps: metadata?.nodeOverlaps ?? 0,
    nodeSpacingOverlaps: metadata?.nodeSpacingOverlaps ?? 0,
    overlappingEdges: metadata?.overlappingEdges ?? 0,
    rawRouteCrossings: metadata?.rawRouteCrossings,
    routeSegments: metadata?.routeSegments
      ?? layout.routedEdges.reduce(
        (sum, edge) => sum + Math.max(0, edge.points.length - 1),
        0,
      ),
    strategy: metadata?.strategy,
    visualCrossings: metadata?.visualCrossings ?? layout.crossings.length,
  };
}

function buildTopCrossEdges(
  metadata: LayoutEngineMetadata | undefined,
  degreeRecords: Map<string, NodeDegreeRecord>,
  clusterByModelId: Map<string, string>,
): TopologyDiagnostics["topCrossEdges"] {
  return (metadata?.topCrossEdges ?? []).slice(0, 25).map((edge) => ({
    crossings: edge.crossings,
    sourceClusterId: clusterByModelId.get(edge.sourceModelId),
    sourceDegree: degreeRecords.get(edge.sourceModelId)?.totalDegree,
    sourceModelId: edge.sourceModelId,
    targetClusterId: clusterByModelId.get(edge.targetModelId),
    targetDegree: degreeRecords.get(edge.targetModelId)?.totalDegree,
    targetModelId: edge.targetModelId,
  }));
}

function buildWeaknesses(input: {
  bundleTopology: TopologyDiagnostics["bundleTopology"];
  clusterTopology: TopologyDiagnostics["clusterTopology"];
  geometry: TopologyDiagnostics["geometry"];
  graphTopology: TopologyDiagnostics["graphTopology"];
  layoutMetrics: TopologyDiagnostics["layoutMetrics"];
  routeTopology: TopologyDiagnostics["routeTopology"];
}): TopologyWeakness[] {
  const weaknesses: TopologyWeakness[] = [];
  const add = (
    severity: WeaknessSeverity,
    code: string,
    message: string,
    evidence: Record<string, unknown>,
  ) => weaknesses.push({ code, evidence, message, severity });

  if (input.clusterTopology.interClusterEdgeRatio > 0.65) {
    add(
      "warning",
      "inter_cluster_edges_dominate",
      "Most structural edges cross cluster boundaries; placement quality is dominated by cluster topology, not local table packing.",
      {
        interClusterEdgeRatio: input.clusterTopology.interClusterEdgeRatio,
        interClusterEdges: input.clusterTopology.interClusterEdges,
      },
    );
  }

  if (input.clusterTopology.singletonClusterRatio > 0.2) {
    add(
      "warning",
      "many_singleton_clusters",
      "The cluster partition has many singleton clusters, which usually creates connector and routing noise.",
      {
        singletonClusterCount: input.clusterTopology.singletonClusterCount,
        singletonClusterRatio: input.clusterTopology.singletonClusterRatio,
      },
    );
  }

  const topDegree = input.graphTopology.degree.topNodes[0]?.totalDegree ?? 0;
  if (topDegree > Math.max(24, input.graphTopology.degree.p90 * 2.5)) {
    add(
      "warning",
      "hub_degree_dominance",
      "A small number of high-degree models dominate routing pressure.",
      {
        p90Degree: input.graphTopology.degree.p90,
        topDegree,
        topModelId: input.graphTopology.degree.topNodes[0]?.modelId,
      },
    );
  }

  if (input.geometry.nodeAreaCoverage > 0 && input.geometry.nodeAreaCoverage < 0.04) {
    add(
      "warning",
      "sparse_node_bbox",
      "The node bounding box is sparse; the layout spends a lot of area on whitespace and long routes.",
      {
        nodeAreaCoverage: input.geometry.nodeAreaCoverage,
        nodeBBoxArea: input.geometry.nodeBBox.area,
      },
    );
  }

  if (input.layoutMetrics.edgeNodeIntersections > 0) {
    const severity: WeaknessSeverity =
      input.layoutMetrics.edgeNodeIntersections > 200 ? "critical" : "warning";
    add(
      severity,
      "edge_node_intersections_remaining",
      "Routed edges still pass through unrelated node boxes.",
      {
        edgeNodeIntersections: input.layoutMetrics.edgeNodeIntersections,
        estimatedEdgeNodeIntersections: input.geometry.estimatedEdgeNodeIntersections,
        topHitNodes: input.geometry.topEdgeNodeHitNodes.slice(0, 5),
      },
    );
  }

  if (input.layoutMetrics.edgeCrossings > 0) {
    add(
      input.layoutMetrics.edgeCrossings > 700 ? "critical" : "warning",
      "edge_crossings_remaining",
      "Visible edge crossings remain after carrier grouping and route polishing.",
      {
        edgeCrossings: input.layoutMetrics.edgeCrossings,
        rawRouteCrossings: input.layoutMetrics.rawRouteCrossings,
        visualCrossings: input.layoutMetrics.visualCrossings,
      },
    );
  }

  if (
    input.layoutMetrics.overlappingEdges > 0
    || input.layoutMetrics.edgeSegmentOverlaps > 0
  ) {
    add(
      "warning",
      "route_overlap_debt_remaining",
      "Some routed edges or route segments overlap, which can hide topology in the rendered diagram.",
      {
        edgeSegmentOverlaps: input.layoutMetrics.edgeSegmentOverlaps,
        overlappingEdges: input.layoutMetrics.overlappingEdges,
      },
    );
  }

  if (input.bundleTopology.maxLeafCount >= 12) {
    add(
      "info",
      "large_leaf_bundles",
      "Large leaf bundles are present; inspect whether bundle boxes are reducing or hiding meaningful topology.",
      {
        maxLeafCount: input.bundleTopology.maxLeafCount,
        topBundles: input.bundleTopology.topBundles.slice(0, 5),
      },
    );
  }

  if (input.routeTopology.bentEdgeCount > input.routeTopology.routedEdgeCount * 0.5) {
    add(
      "info",
      "many_bent_routes",
      "More than half of routed edges have bends; route complexity is high relative to topology size.",
      {
        bentEdgeCount: input.routeTopology.bentEdgeCount,
        routedEdgeCount: input.routeTopology.routedEdgeCount,
      },
    );
  }

  return weaknesses;
}

function computeNodeBBox(nodes: NodeLayout[]): Rect & {
  area: number;
  height: number;
  width: number;
} {
  if (nodes.length === 0) {
    return { area: 0, bottom: 0, height: 0, left: 0, right: 0, top: 0, width: 0 };
  }
  let left = Number.POSITIVE_INFINITY;
  let top = Number.POSITIVE_INFINITY;
  let right = Number.NEGATIVE_INFINITY;
  let bottom = Number.NEGATIVE_INFINITY;
  for (const node of nodes) {
    const rect = nodeRect(node);
    left = Math.min(left, rect.left);
    top = Math.min(top, rect.top);
    right = Math.max(right, rect.right);
    bottom = Math.max(bottom, rect.bottom);
  }
  const width = Math.max(0, right - left);
  const height = Math.max(0, bottom - top);
  return { area: width * height, bottom, height, left, right, top, width };
}

function computeRouteBBox(
  routedEdges: RoutedEdgePath[],
  fallback: Rect & { area: number; height: number; width: number },
): Rect & { area: number; height: number; width: number } {
  let sawPoint = false;
  let left = Number.POSITIVE_INFINITY;
  let top = Number.POSITIVE_INFINITY;
  let right = Number.NEGATIVE_INFINITY;
  let bottom = Number.NEGATIVE_INFINITY;
  for (const edge of routedEdges) {
    for (const point of edge.points) {
      sawPoint = true;
      left = Math.min(left, point.x);
      top = Math.min(top, point.y);
      right = Math.max(right, point.x);
      bottom = Math.max(bottom, point.y);
    }
  }
  if (!sawPoint) return fallback;
  const width = Math.max(0, right - left);
  const height = Math.max(0, bottom - top);
  return { area: width * height, bottom, height, left, right, top, width };
}

function nodeRect(node: NodeLayout): Rect {
  return {
    bottom: node.position.y + node.size.height,
    left: node.position.x,
    right: node.position.x + node.size.width,
    top: node.position.y,
  };
}

function segmentIntersectsRect(start: Point, end: Point, rect: Rect): boolean {
  if (pointInRect(start, rect) || pointInRect(end, rect)) return true;
  const corners = [
    { x: rect.left, y: rect.top },
    { x: rect.right, y: rect.top },
    { x: rect.right, y: rect.bottom },
    { x: rect.left, y: rect.bottom },
  ];
  for (let i = 0; i < corners.length; i += 1) {
    const a = corners[i];
    const b = corners[(i + 1) % corners.length];
    if (segmentsIntersect(start, end, a, b)) return true;
  }
  return false;
}

function pointInRect(point: Point, rect: Rect): boolean {
  return (
    point.x >= rect.left
    && point.x <= rect.right
    && point.y >= rect.top
    && point.y <= rect.bottom
  );
}

function segmentsIntersect(a: Point, b: Point, c: Point, d: Point): boolean {
  const o1 = orientation(a, b, c);
  const o2 = orientation(a, b, d);
  const o3 = orientation(c, d, a);
  const o4 = orientation(c, d, b);
  if (o1 === 0 && pointOnSegment(c, a, b)) return true;
  if (o2 === 0 && pointOnSegment(d, a, b)) return true;
  if (o3 === 0 && pointOnSegment(a, c, d)) return true;
  if (o4 === 0 && pointOnSegment(b, c, d)) return true;
  return o1 !== o2 && o3 !== o4;
}

function orientation(a: Point, b: Point, c: Point): -1 | 0 | 1 {
  const value = (b.y - a.y) * (c.x - b.x) - (b.x - a.x) * (c.y - b.y);
  if (Math.abs(value) < 1e-9) return 0;
  return value > 0 ? 1 : -1;
}

function pointOnSegment(point: Point, start: Point, end: Point): boolean {
  return (
    point.x >= Math.min(start.x, end.x) - 1e-9
    && point.x <= Math.max(start.x, end.x) + 1e-9
    && point.y >= Math.min(start.y, end.y) - 1e-9
    && point.y <= Math.max(start.y, end.y) + 1e-9
  );
}

function pushMapArray<K, V>(map: Map<K, V[]>, key: K, value: V): void {
  const existing = map.get(key);
  if (existing) {
    existing.push(value);
  } else {
    map.set(key, [value]);
  }
}

function enrichRect(rect: Rect): Rect & {
  area: number;
  center: Point;
  height: number;
  width: number;
} {
  const width = Math.max(0, rect.right - rect.left);
  const height = Math.max(0, rect.bottom - rect.top);
  return {
    ...rect,
    area: width * height,
    center: {
      x: rect.left + width / 2,
      y: rect.top + height / 2,
    },
    height,
    width,
  };
}

function bboxFromPoints(points: Point[]): Rect & {
  area: number;
  height: number;
  width: number;
} {
  if (points.length === 0) {
    return { area: 0, bottom: 0, height: 0, left: 0, right: 0, top: 0, width: 0 };
  }
  let left = Number.POSITIVE_INFINITY;
  let top = Number.POSITIVE_INFINITY;
  let right = Number.NEGATIVE_INFINITY;
  let bottom = Number.NEGATIVE_INFINITY;
  for (const point of points) {
    left = Math.min(left, point.x);
    top = Math.min(top, point.y);
    right = Math.max(right, point.x);
    bottom = Math.max(bottom, point.y);
  }
  const enriched = enrichRect({ bottom, left, right, top });
  return {
    area: enriched.area,
    bottom: enriched.bottom,
    height: enriched.height,
    left: enriched.left,
    right: enriched.right,
    top: enriched.top,
    width: enriched.width,
  };
}

function bboxFromRects(rects: Rect[]): Rect & {
  area: number;
  center: Point;
  height: number;
  width: number;
} {
  if (rects.length === 0) {
    return enrichRect({ bottom: 0, left: 0, right: 0, top: 0 });
  }
  let left = Number.POSITIVE_INFINITY;
  let top = Number.POSITIVE_INFINITY;
  let right = Number.NEGATIVE_INFINITY;
  let bottom = Number.NEGATIVE_INFINITY;
  for (const rect of rects) {
    left = Math.min(left, rect.left);
    top = Math.min(top, rect.top);
    right = Math.max(right, rect.right);
    bottom = Math.max(bottom, rect.bottom);
  }
  return enrichRect({ bottom, left, right, top });
}

function mergeRects(
  leftRect: Rect,
  rightRect: Rect,
): Rect & { area: number; height: number; width: number } {
  const enriched = bboxFromRects([leftRect, rightRect]);
  return {
    area: enriched.area,
    bottom: enriched.bottom,
    height: enriched.height,
    left: enriched.left,
    right: enriched.right,
    top: enriched.top,
    width: enriched.width,
  };
}

function segmentOrientation(
  start: Point,
  end: Point,
): "diagonal" | "horizontal" | "point" | "vertical" {
  const dx = Math.abs(end.x - start.x);
  const dy = Math.abs(end.y - start.y);
  if (dx < 1e-6 && dy < 1e-6) return "point";
  if (dy < 1e-6) return "horizontal";
  if (dx < 1e-6) return "vertical";
  return "diagonal";
}

function classifyPortSide(
  point: Point,
  rect: Rect,
): "bottom" | "inside" | "left" | "right" | "top" {
  const distances = [
    { side: "left" as const, value: Math.abs(point.x - rect.left) },
    { side: "right" as const, value: Math.abs(point.x - rect.right) },
    { side: "top" as const, value: Math.abs(point.y - rect.top) },
    { side: "bottom" as const, value: Math.abs(point.y - rect.bottom) },
  ];
  distances.sort((left, right) => left.value - right.value);
  if (distances[0].value <= 3) return distances[0].side;
  if (pointInRect(point, rect)) return "inside";
  return distances[0].side;
}

function findSegmentIndexForPoint(
  points: Point[],
  point: Point,
): number | undefined {
  let bestIndex: number | undefined;
  let bestDistance = Number.POSITIVE_INFINITY;
  for (let i = 1; i < points.length; i += 1) {
    const distance = distancePointToSegment(point, points[i - 1], points[i]);
    if (distance < bestDistance) {
      bestDistance = distance;
      bestIndex = i - 1;
    }
  }
  return bestDistance <= 1.5 ? bestIndex : undefined;
}

function closestPointOnSegment(point: Point, start: Point, end: Point): Point {
  const dx = end.x - start.x;
  const dy = end.y - start.y;
  const lengthSquared = dx * dx + dy * dy;
  if (lengthSquared <= 1e-9) return start;
  const t = Math.max(
    0,
    Math.min(1, ((point.x - start.x) * dx + (point.y - start.y) * dy) / lengthSquared),
  );
  return {
    x: start.x + dx * t,
    y: start.y + dy * t,
  };
}

function distancePointToSegment(point: Point, start: Point, end: Point): number {
  const closest = closestPointOnSegment(point, start, end);
  return Math.hypot(point.x - closest.x, point.y - closest.y);
}

function clampInt(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value));
}

function routeLength(points: Point[]): number {
  let length = 0;
  for (let i = 1; i < points.length; i += 1) {
    const dx = points[i].x - points[i - 1].x;
    const dy = points[i].y - points[i - 1].y;
    length += Math.hypot(dx, dy);
  }
  return length;
}

function average(values: number[]): number {
  if (values.length === 0) return 0;
  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

function percentileSorted(values: number[], percentile: number): number {
  if (values.length === 0) return 0;
  const index = Math.min(
    values.length - 1,
    Math.max(0, Math.floor((values.length - 1) * percentile)),
  );
  return values[index];
}

function safeRatio(numerator: number, denominator: number): number {
  return denominator > 0 ? numerator / denominator : 0;
}

function sortNumberAscending(left: number, right: number): number {
  return left - right;
}

function sortRecordByValueDesc(record: Record<string, number>): Record<string, number> {
  return Object.fromEntries(
    Object.entries(record).sort((left, right) => right[1] - left[1]),
  );
}
