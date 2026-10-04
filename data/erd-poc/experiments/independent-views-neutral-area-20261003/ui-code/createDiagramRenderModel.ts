import type { ModelId } from "../../shared/domain/modelIdentity";
import {
  getOgdfLayoutDefinition,
  normalizeLayoutMode,
  type LayoutMode,
} from "../../shared/graph/layoutContract";
import type {
  ExtractedModel,
  MethodAssociationConfidence,
  UserMethod,
} from "../../shared/protocol/analyzerContract";
import type { DjangoWorkspaceDiscoveryResult } from "../../shared/protocol/discoveryContract";
import type {
  DiagramBootstrapPayload,
  TableViewOptions,
} from "../../shared/protocol/webviewContract";
import type {
  CanonicalCrossingMetadata,
  EdgeCrossing,
  LeafBundle,
  Point,
  RenderedCarrierRoute,
  RoutedEdgePath,
} from "../../shared/graph/layoutContract";
import type { StructuralGraphEdge, MethodAssociation } from "../../shared/graph/diagramGraph";
import { createJuneRelationshipOverview } from "./createJuneRelationshipOverview";
import { createLeafCardConnectionTools } from "./leafCardConnections";
import { createLeafCards, type LeafCardRenderModel } from "./createLeafCards";

const MODEL_CATALOG_MODE_THRESHOLD = 500;
const SEMANTIC_CARRIER_NEIGHBOR_LIMIT = 64;
const SEMANTIC_CARRIER_OBSTACLE_PADDING = 10;
const SEMANTIC_CARRIER_SPATIAL_CELL = 1024;
const VISIBLE_SEMANTIC_BUNDLE_MIN_RELATIONSHIPS = 2;
const VISIBLE_SEMANTIC_BUNDLE_TARGET_GROUP_SIZE = 2;
const VISIBLE_SEMANTIC_BUNDLE_MAX_GROUP_SIZE = 4;
const CATALOG_BASE_TABLE_HEIGHT = 74;
const CATALOG_BASE_TABLE_WIDTH = 236;
const CATALOG_MAX_TABLE_HEIGHT = 434;
const CATALOG_MAX_TABLE_WIDTH = 396;

export interface DiscoveryRenderModel {
  appCount: number;
  apps: Array<{ appLabel: string; flags: string[] }>;
  diagnostics: Array<{ code: string; message: string; severity: string }>;
  selectedRoot: string;
  strategy: string;
}

export interface InspectorRenderModel {
  diagnostics: Array<{ code: string; message: string; severity: string }>;
  discovery?: DiscoveryRenderModel;
  models: InspectorModelRenderModel[];
  selectedMethodName?: string;
  selectedModelId?: string;
}

export interface InspectorModelRenderModel {
  activeMethodName?: string;
  appLabel: string;
  databaseTableName: string;
  fieldRows: Array<{ key: string; text: string; tone: "enum-option" | "field" }>;
  hidden: boolean;
  methods: UserMethod[];
  modelId: ModelId;
  modelName: string;
  properties: string[];
  relationships: InspectorRelationshipRenderModel[];
  selected: boolean;
  showMethodHighlights: boolean;
  showMethods: boolean;
  showProperties: boolean;
}

export interface InspectorRelationshipRenderModel {
  direction: "incoming" | "outgoing" | "self";
  edgeId: string;
  fieldName: string;
  kind: StructuralGraphEdge["kind"];
  otherModelId: ModelId;
  sourceModelId: ModelId;
  targetModelId: ModelId;
}

export interface LayoutExecutionRenderModel {
  appliedLabel: string;
  appliedMode: LayoutMode;
  engine: "analyzer" | "empty" | "ogdf";
  reason?: string;
  requestedLabel: string;
  requestedMode: LayoutMode;
  status: "applied" | "empty" | "fallback" | "quality-degraded";
}

export interface LayoutFailureRenderModel {
  label: string;
  mode: LayoutMode;
  reason: string;
}

export interface MethodOverlayRenderModel {
  confidence: MethodAssociationConfidence;
  id: string;
  methodName: string;
  sourceModelId: ModelId;
  targetModelId: ModelId;
  x1: number;
  x2: number;
  y1: number;
  y2: number;
}

export interface TableRenderModel {
  activeMethodName?: string;
  appLabel: string;
  clusterId?: string;
  databaseTableName: string;
  fieldRows: Array<{ key: string; text: string; tone: "enum-option" | "field" }>;
  hasExplicitDatabaseTableName: boolean;
  hidden: boolean;
  methodAssociations: MethodAssociation[];
  methods: UserMethod[];
  modelId: ModelId;
  modelName: string;
  position: { x: number; y: number };
  properties: string[];
  selfRelationshipCount?: number;
  selected: boolean;
  showMethodHighlights: boolean;
  showMethods: boolean;
  showProperties: boolean;
  size: { height: number; width: number };
}

export interface EdgeRenderModel {
  carrierFamily?: "association" | "inheritance" | "mixed";
  carrierRole?: "direct" | "overview" | "semantic-branch" | "semantic-tree" | "semantic-trunk";
  crossingIds: string[];
  cssKind: string;
  edgeId: string;
  logicalEndpointModelIds?: ModelId[];
  markerEndId: string;
  markerStartId: string;
  memberEdgeIds?: string[];
  physicalEndpointModelIds?: ModelId[];
  leafCardEndpointIds?: [string | null, string | null];
  points: string;
  preserveRouteEndpoints?: boolean;
  provenance: string;
  sourceModelId: ModelId;
  targetModelId: ModelId;
}

/**
 * Legacy metadata parser retained only while old layout snapshots can still be
 * decoded. The render pipeline deliberately never calls it: a node group must
 * not replace any of its member relationships with one representative edge.
 */
interface HubCarrierRenderGroup {
  id: string;
  logicalEndpointModelIds: ModelId[];
  memberEdgeIds: string[];
  points: Point[];
  representativeEdgeId: string;
  sourceModelId: ModelId;
  targetModelId: ModelId;
}

export interface BundleLeafTile {
  appLabel: string;
  bundleIndex: number;
  modelId: ModelId;
  modelName: string;
  position: { x: number; y: number };
  size: { height: number; width: number };
}

export interface ClusterOutline {
  bbox: { x: number; y: number; width: number; height: number };
  clusterId: string;
  colorKey: string;
  label: string;
  memberCount: number;
}

export interface CanonicalCrossingRenderModel {
  adjacentEdgeIntersections?: number;
  boundViolation: boolean;
  collinearOverlaps?: number;
  completeRoutes: boolean;
  degenerateSegments?: number;
  gap?: number;
  invariantViolations?: number;
  lowerBound: number;
  nonIncidentNodeHits?: number;
  nonProperContacts: number;
  optimality?: number;
  pointContacts?: number;
  properDrawing: boolean;
  routeCrossingPairs: number;
  selfIntersections?: number;
}

export interface DiagramRenderModel {
  bundleLeafTiles: BundleLeafTile[];
  bundleLeavesByFakeId: Record<string, ModelId[]>;
  canvas: { height: number; width: number };
  canonicalCrossing?: CanonicalCrossingRenderModel;
  clusterOutlines: ClusterOutline[];
  crossings: EdgeCrossing[];
  edges: EdgeRenderModel[];
  inspector: InspectorRenderModel;
  layoutExecution: LayoutExecutionRenderModel;
  layoutFailures: LayoutFailureRenderModel[];
  layoutMode: DiagramBootstrapPayload["view"]["layoutMode"];
  leafBundles: LeafBundle[];
  leafCards: LeafCardRenderModel[];
  leafCardOverview?: {
    baseEdges: EdgeRenderModel[];
    individualEdges: EdgeRenderModel[];
    internalEdgeIds: string[];
    connectionCount: number;
    baseVisualCrossings: number;
    visualCrossings: number;
    expandedVisualCrossings: number;
    expandedEdgeCount: number;
  };
  modelCatalogMode: boolean;
  overlays: MethodOverlayRenderModel[];
  timings: DiagramBootstrapPayload["timings"];
  tables: TableRenderModel[];
  semanticCarriers?: SemanticCarrierDiagnostics;
  relationshipOverview?: {
    groupCount: number;
    individualEdges: EdgeRenderModel[];
    individualVisualCrossings: number;
    visualCrossings: number;
  };
  visualCrossings?: number;
  individualView?: {
    positions: Record<string, Point>;
    edges: EdgeRenderModel[];
    clusterOutlines: ClusterOutline[];
    visualCrossings: number;
  };
}

export interface SemanticCarrierDiagnostics {
  active: boolean;
  bundleGroups?: number;
  bundledRelationships: number;
  carrierSegments: number;
  coincidentRelationships?: number;
  disconnectedRelationships: string[];
  eligibleRelationships?: number;
  fallbackRelationships: number;
  missingRelationships: string[];
  obstacleIntersections: number;
  relationships: number;
  semanticCrossingsAvoided?: number;
  semanticObstacleIntersectionsAvoided?: number;
  selfRelationships?: number;
}

export interface RenderedTableClearanceMetrics {
  bboxArea: number;
  bboxHeight: number;
  bboxWidth: number;
  bundleBundleOverlaps: number;
  bundleNodeOverlaps: number;
  minimum: number;
  nodeOverlaps: number;
  objectCount: number;
  spacingViolations: number;
}

export interface RenderedEdgeNodeIntersection {
  edgeId: string;
  nodeModelId: string;
}

export interface RenderedEdgeNodeIntersectionMetrics {
  bundleCount: number;
  count: number;
  hits: RenderedEdgeNodeIntersection[];
  nodeCount: number;
}

export interface RenderedVisualConflictMetrics {
  bundleBundleOverlaps: number;
  bundleEdgeIntersections: number;
  bundleNodeOverlaps: number;
  edgeCount: number;
  edgeCrossings: number;
  edgeNodeIntersections: number;
  nodeOverlaps: number;
  routeSegments: number;
  visualCrossings: number;
}

interface ResolvedRenderedEdgeGeometry {
  edge: EdgeRenderModel;
  points: Point[];
}

function isRenderedSyntheticBundleModelId(modelId: string): boolean {
  return String(modelId).startsWith("__leafbundle.") || String(modelId).startsWith("leaf-card:");
}

/** Physical obstacles for the active presentation, while real models stay selectable. */
export function getRenderedConnectionTables(renderModel: DiagramRenderModel): Array<Pick<TableRenderModel, "position" | "size" | "hidden"> & {modelId: string}> {
  const tables = renderModel.tables.filter(table => !table.hidden);
  if (!renderModel.leafCardOverview) return tables;
  const cards = createLeafCardConnectionTools().bounds(renderModel.leafCards, new Map(tables.map(table => [table.modelId, {
    modelId: table.modelId, x: table.position.x, y: table.position.y, ...table.size,
  }])));
  const members = new Set(cards.flatMap(card => card.memberModelIds));
  return [...tables.filter(table => !members.has(table.modelId)), ...cards.map(card => ({
    modelId: card.id, hidden: false,
    position: {x: card.x, y: card.y}, size: {width: card.width, height: card.height},
  }))];
}

/**
 * Measures the visible connection obstacles: whole leaf cards when enabled,
 * otherwise individual model cards. Members retain their own geometry and
 * can be checked separately with leafCardOverview disabled. The X/Y gaps
 * mirror the native post-layout spacing contract.
 */
export function measureRenderedTableClearance(
  renderModel: DiagramRenderModel,
  gapX = 56,
  gapY = 42,
): RenderedTableClearanceMetrics {
  const serializationTolerance = 0.01;
  const syntheticBundleIds = new Set(
    [
      ...Object.keys(renderModel.bundleLeavesByFakeId ?? {}),
      ...getRenderedConnectionTables(renderModel)
        .filter((table) => isRenderedSyntheticBundleModelId(table.modelId))
        .map((table) => table.modelId),
    ],
  );
  const objects = getRenderedConnectionTables(renderModel)
    .filter((table) => !table.hidden)
    .map((table) => ({
      bottom: table.position.y + table.size.height,
      bundle: syntheticBundleIds.has(table.modelId),
      left: table.position.x,
      right: table.position.x + table.size.width,
      top: table.position.y,
    }));

  const metrics: RenderedTableClearanceMetrics = {
    bboxArea: 0,
    bboxHeight: 0,
    bboxWidth: 0,
    bundleBundleOverlaps: 0,
    bundleNodeOverlaps: 0,
    minimum: 0,
    nodeOverlaps: 0,
    objectCount: objects.length,
    spacingViolations: 0,
  };
  if (objects.length < 2) {
    if (objects.length === 1) {
      metrics.bboxWidth = objects[0].right - objects[0].left;
      metrics.bboxHeight = objects[0].bottom - objects[0].top;
      metrics.bboxArea = metrics.bboxWidth * metrics.bboxHeight;
    }
    return metrics;
  }

  const minX = Math.min(...objects.map((object) => object.left));
  const minY = Math.min(...objects.map((object) => object.top));
  const maxX = Math.max(...objects.map((object) => object.right));
  const maxY = Math.max(...objects.map((object) => object.bottom));
  metrics.bboxWidth = maxX - minX;
  metrics.bboxHeight = maxY - minY;
  metrics.bboxArea = metrics.bboxWidth * metrics.bboxHeight;

  metrics.minimum = Number.POSITIVE_INFINITY;
  for (let leftIndex = 0; leftIndex < objects.length; leftIndex += 1) {
    const left = objects[leftIndex];
    for (let rightIndex = leftIndex + 1; rightIndex < objects.length; rightIndex += 1) {
      const right = objects[rightIndex];
      const separationX = Math.max(
        0,
        left.left - right.right,
        right.left - left.right,
      );
      const separationY = Math.max(
        0,
        left.top - right.bottom,
        right.top - left.bottom,
      );
      metrics.minimum = Math.min(
        metrics.minimum,
        Math.hypot(separationX, separationY),
      );

      const overlapX = Math.min(left.right, right.right)
        - Math.max(left.left, right.left);
      const overlapY = Math.min(left.bottom, right.bottom)
        - Math.max(left.top, right.top);
      if (overlapX > 0 && overlapY > 0) {
        if (left.bundle && right.bundle) {
          metrics.bundleBundleOverlaps += 1;
        } else if (left.bundle || right.bundle) {
          metrics.bundleNodeOverlaps += 1;
        } else {
          metrics.nodeOverlaps += 1;
        }
      }

      // Equivalent to expanding both rectangles by half of the native X/Y
      // gap and checking strict rectangle overlap.
      if (
        separationX + serializationTolerance < gapX
        && separationY + serializationTolerance < gapY
      ) {
        metrics.spacingViolations += 1;
      }
    }
  }
  if (!Number.isFinite(metrics.minimum)) {
    metrics.minimum = 0;
  }
  return metrics;
}

/**
 * Independently audits the exact edge/table geometry sent to the canvas.
 * Native metadata is deliberately not consulted: a stale or carrier-filtered
 * metric must never certify a line that still penetrates a rendered table.
 */
export function measureRenderedEdgeNodeIntersections(
  renderModel: DiagramRenderModel,
  padding = 10,
): RenderedEdgeNodeIntersectionMetrics {
  const syntheticBundleIds = new Set(
    [
      ...Object.keys(renderModel.bundleLeavesByFakeId ?? {}),
      ...getRenderedConnectionTables(renderModel)
        .filter((table) => isRenderedSyntheticBundleModelId(table.modelId))
        .map((table) => table.modelId),
    ],
  );
  const bundleIdByLeafModelId = new Map<string, string>();
  for (const [bundleId, leafModelIds] of Object.entries(
    renderModel.bundleLeavesByFakeId ?? {},
  )) {
    for (const leafModelId of leafModelIds) {
      bundleIdByLeafModelId.set(leafModelId, bundleId);
    }
  }
  // A displayed leaf card obstructs an unrelated line across its whole area,
  // including gaps between members. Exempt only the physical endpoint cards.
  const tables = getRenderedConnectionTables(renderModel);
  const hits: RenderedEdgeNodeIntersection[] = [];
  const seen = new Set<string>();

  for (const { edge, points } of resolveRenderedEdgeGeometries(renderModel)) {
    const physicalEndpointModelIds = new Set<string>([
      ...(edge.physicalEndpointModelIds ?? [edge.sourceModelId, edge.targetModelId]),
      ...(edge.leafCardEndpointIds?.filter((id): id is string => id !== null) ?? []),
    ]);
    const logicalEndpointBundleIds = new Set(
      [...physicalEndpointModelIds]
        .map((modelId) => bundleIdByLeafModelId.get(modelId))
        .filter((bundleId): bundleId is string => bundleId !== undefined),
    );

    for (const table of tables) {
      if (
        physicalEndpointModelIds.has(table.modelId)
        || (syntheticBundleIds.has(table.modelId)
          && logicalEndpointBundleIds.has(table.modelId))
      ) {
        continue;
      }
      const penetrates = points.slice(1).some((end, index) =>
        segmentPenetratesRenderedTable(points[index], end, table, padding)
      );
      if (!penetrates) {
        continue;
      }

      const key = `${edge.edgeId}\u0000${table.modelId}`;
      if (seen.has(key)) {
        continue;
      }
      seen.add(key);
      hits.push({ edgeId: edge.edgeId, nodeModelId: table.modelId });
    }
  }

  const bundleCount = hits.filter((hit) =>
    syntheticBundleIds.has(hit.nodeModelId)
  ).length;
  return {
    bundleCount,
    count: hits.length,
    hits,
    nodeCount: hits.length - bundleCount,
  };
}

/**
 * Counts proper segment/segment crossings in the exact edge geometry supplied
 * to the canvas. Endpoint touches and collinear contacts are deliberately not
 * counted here; they remain separate overlap/contact diagnostics.
 */
export function measureRenderedEdgeCrossings(
  renderModel: DiagramRenderModel,
): { edgeCount: number; edgeCrossings: number; routeSegments: number } {
  const geometries = resolveRenderedEdgeGeometries(renderModel);
  const segments = geometries.flatMap(({ edge, points }) =>
    points.slice(1).flatMap((end, index) => {
      const start = points[index];
      return sameRenderedPoint(start, end)
        ? []
        : [{ edgeId: edge.edgeId, end, start }];
    })
  );
  let edgeCrossings = 0;
  for (let leftIndex = 0; leftIndex < segments.length; leftIndex += 1) {
    const left = segments[leftIndex];
    for (let rightIndex = leftIndex + 1; rightIndex < segments.length; rightIndex += 1) {
      const right = segments[rightIndex];
      if (left.edgeId === right.edgeId) {
        continue;
      }
      if (segmentsProperlyCross(left.start, left.end, right.start, right.end)) {
        edgeCrossings += 1;
      }
    }
  }
  return {
    edgeCount: geometries.length,
    edgeCrossings,
    routeSegments: segments.length,
  };
}

/**
 * Authoritative visual-conflict audit for the initial canvas scene.
 *
 * Every component is measured from one DiagramRenderModel, so a reduced
 * native carrier score can never be exposed as though it described the
 * expanded edge/table geometry that the user actually sees.
 */
export function measureRenderedVisualConflicts(
  renderModel: DiagramRenderModel,
  edgeTablePadding = 10,
): RenderedVisualConflictMetrics {
  const clearance = measureRenderedTableClearance(renderModel);
  const edgeTable = measureRenderedEdgeNodeIntersections(
    renderModel,
    edgeTablePadding,
  );
  const edgeGeometry = measureRenderedEdgeCrossings(renderModel);
  const visualCrossings =
    edgeGeometry.edgeCrossings
    + edgeTable.nodeCount
    + edgeTable.bundleCount
    + clearance.nodeOverlaps
    + clearance.bundleNodeOverlaps
    + clearance.bundleBundleOverlaps;
  return {
    bundleBundleOverlaps: clearance.bundleBundleOverlaps,
    bundleEdgeIntersections: edgeTable.bundleCount,
    bundleNodeOverlaps: clearance.bundleNodeOverlaps,
    edgeCount: edgeGeometry.edgeCount,
    edgeCrossings: edgeGeometry.edgeCrossings,
    edgeNodeIntersections: edgeTable.nodeCount,
    nodeOverlaps: clearance.nodeOverlaps,
    routeSegments: edgeGeometry.routeSegments,
    visualCrossings,
  };
}

function resolveRenderedEdgeGeometries(
  renderModel: DiagramRenderModel,
): ResolvedRenderedEdgeGeometry[] {
  const visibleTableByModelId = new Map(
    renderModel.tables
      .filter((table) => !table.hidden)
      .map((table) => [table.modelId, table] as const),
  );
  const geometries: ResolvedRenderedEdgeGeometry[] = [];
  for (const edge of renderModel.edges) {
    const sourceTable = visibleTableByModelId.get(edge.sourceModelId);
    const targetTable = visibleTableByModelId.get(edge.targetModelId);
    if (!sourceTable || !targetTable) {
      continue;
    }
    const parsedPoints = parseRenderedEdgePoints(edge.points);
    const points = parsedPoints.length >= 2
      ? edge.preserveRouteEndpoints
        ? parsedPoints
        : attachRenderedEdgeEndpoints(parsedPoints, sourceTable, targetTable)
      : buildStraightRenderedEdgePoints(sourceTable, targetTable);
    if (points.length >= 2) {
      geometries.push({ edge, points });
    }
  }
  return geometries;
}

function sameRenderedPoint(left: Point, right: Point): boolean {
  return Math.abs(left.x - right.x) <= 1e-9
    && Math.abs(left.y - right.y) <= 1e-9;
}

function segmentsProperlyCross(
  firstStart: Point,
  firstEnd: Point,
  secondStart: Point,
  secondEnd: Point,
): boolean {
  const epsilon = 1e-9;
  const firstA = renderedOrientation(firstStart, firstEnd, secondStart);
  const firstB = renderedOrientation(firstStart, firstEnd, secondEnd);
  const secondA = renderedOrientation(secondStart, secondEnd, firstStart);
  const secondB = renderedOrientation(secondStart, secondEnd, firstEnd);
  return (
    firstA * firstB < -epsilon
    && secondA * secondB < -epsilon
  );
}

function renderedOrientation(start: Point, end: Point, point: Point): number {
  return (end.x - start.x) * (point.y - start.y)
    - (end.y - start.y) * (point.x - start.x);
}

function parseRenderedEdgePoints(value: string): Point[] {
  if (!value.trim()) {
    return [];
  }
  return value.trim().split(/\s+/).flatMap((pair) => {
    const [rawX, rawY] = pair.split(",");
    const x = Number(rawX);
    const y = Number(rawY);
    return Number.isFinite(x) && Number.isFinite(y) ? [{ x, y }] : [];
  });
}

function enforceStraightRenderedEdge(edge: EdgeRenderModel): EdgeRenderModel {
  const points = parseRenderedEdgePoints(edge.points);
  if (points.length <= 2) {
    return edge;
  }
  const straightPoints = [points[0], points[points.length - 1]];
  return {
    ...edge,
    points: straightPoints.map((point) => `${point.x},${point.y}`).join(" "),
  };
}

function attachRenderedEdgeEndpoints(
  points: Point[],
  sourceTable: TableRenderModel,
  targetTable: TableRenderModel,
): Point[] {
  const attached = points.map((point) => ({ ...point }));
  const lastIndex = attached.length - 1;
  attached[0] = computeRenderedEndpointPort(sourceTable, attached[1]);
  attached[lastIndex] = computeRenderedEndpointPort(
    targetTable,
    attached[lastIndex - 1],
  );
  return attached;
}

function buildStraightRenderedEdgePoints(
  sourceTable: TableRenderModel,
  targetTable: TableRenderModel,
): Point[] {
  const sourceCenter = renderedTableCenter(sourceTable);
  const targetCenter = renderedTableCenter(targetTable);
  return [
    computeRenderedBoundaryPort(sourceTable, targetCenter),
    computeRenderedBoundaryPort(targetTable, sourceCenter),
  ];
}

function renderedTableCenter(table: TableRenderModel): Point {
  return {
    x: table.position.x + table.size.width / 2,
    y: table.position.y + table.size.height / 2,
  };
}

function computeRenderedEndpointPort(
  table: TableRenderModel,
  peerPoint: Point,
): Point {
  const left = table.position.x;
  const right = left + table.size.width;
  const top = table.position.y;
  const bottom = top + table.size.height;
  const center = renderedTableCenter(table);
  let dx = peerPoint.x - center.x;
  let dy = peerPoint.y - center.y;
  if (Math.abs(dx) < 0.01 && Math.abs(dy) < 0.01) {
    dx = 1;
    dy = 0;
  }
  if (Math.abs(dx) >= Math.abs(dy)) {
    return {
      x: dx >= 0 ? right : left,
      y: Math.max(top, Math.min(bottom, peerPoint.y)),
    };
  }
  return {
    x: Math.max(left, Math.min(right, peerPoint.x)),
    y: dy >= 0 ? bottom : top,
  };
}

function computeRenderedBoundaryPort(
  table: TableRenderModel,
  towardCenter: Point,
): Point {
  const center = renderedTableCenter(table);
  let dx = towardCenter.x - center.x;
  let dy = towardCenter.y - center.y;
  if (Math.abs(dx) < 0.01 && Math.abs(dy) < 0.01) {
    dx = 1;
    dy = 0;
  }
  const scaleX = Math.abs(dx) < 0.01
    ? Number.POSITIVE_INFINITY
    : Math.max(1, table.size.width / 2) / Math.abs(dx);
  const scaleY = Math.abs(dy) < 0.01
    ? Number.POSITIVE_INFINITY
    : Math.max(1, table.size.height / 2) / Math.abs(dy);
  const scale = Math.min(scaleX, scaleY);
  return {
    x: center.x + dx * scale,
    y: center.y + dy * scale,
  };
}

function segmentPenetratesRenderedTable(
  start: Point,
  end: Point,
  table: Pick<TableRenderModel, "position" | "size">,
  padding: number,
): boolean {
  const left = table.position.x - padding;
  const right = table.position.x + table.size.width + padding;
  const top = table.position.y - padding;
  const bottom = table.position.y + table.size.height + padding;
  const dx = end.x - start.x;
  const dy = end.y - start.y;
  let enter = 0;
  let exit = 1;

  for (const [origin, delta, minimum, maximum] of [
    [start.x, dx, left, right],
    [start.y, dy, top, bottom],
  ] as const) {
    if (Math.abs(delta) < 1e-9) {
      if (origin <= minimum || origin >= maximum) {
        return false;
      }
      continue;
    }
    const first = (minimum - origin) / delta;
    const second = (maximum - origin) / delta;
    enter = Math.max(enter, Math.min(first, second));
    exit = Math.min(exit, Math.max(first, second));
    if (exit - enter <= 1e-9) {
      return false;
    }
  }

  return exit > 1e-9 && enter < 1 - 1e-9;
}

export function createDiagramRenderModel(
  payload: DiagramBootstrapPayload,
  discovery?: DjangoWorkspaceDiscoveryResult,
): DiagramRenderModel {
  const modelsById = new Map(
    payload.analyzer.models.map((model) => [model.identity.id, model] as const),
  );
  const layoutNodesById = new Map(
    payload.layout.nodes.map((node) => [node.modelId, node] as const),
  );
  const tableOptionsById = new Map(
    payload.view.tableOptions.map((options) => [options.modelId, options] as const),
  );
  const allTables = payload.layout.nodes
    .map((layoutNode) => createTableRenderModel(layoutNode, payload, modelsById, tableOptionsById))
    .filter(isDefined);
  const inspectorRelationshipsByModelId = createInspectorRelationshipsByModelId(
    payload.graph.structuralEdges,
    modelsById,
  );
  const modelCatalogMode = allTables.length > MODEL_CATALOG_MODE_THRESHOLD;
  const rawLeafBundles = payload.layout.engineMetadata?.leafBundles ?? [];
  // Keep the saved member positions. Valid packed groups become connection
  // endpoints after the individual geometry and relationship audit are built.
  const leafBundles = rawLeafBundles;
  const bundleLeafTiles: BundleLeafTile[] = [];
  // Group endpoints have separate render metadata and never introduce a fake
  // selectable model into the real table catalog.
  const bundleLeavesByFakeId: Record<string, ModelId[]> = {};
  const tables = allTables;
  const catalogDegreeByModel = modelCatalogMode
    ? createCatalogRelationDegreeByModel(payload.graph.structuralEdges, layoutNodesById)
    : new Map<ModelId, number>();
  let renderedTables = modelCatalogMode
    ? tables.map((table) =>
        toCatalogTable(table, catalogDegreeByModel.get(table.modelId) ?? 0))
    : tables;
  const structuralEdgeById = new Map(
    payload.graph.structuralEdges.map((edge) => [edge.id, edge] as const),
  );
  const renderedDirectCarrierPointsByEdgeId = new Map<string, Point[]>();
  for (const route of payload.layout.engineMetadata?.renderedCarrierRoutes ?? []) {
    if (
      route.points.length >= 2
      && route.memberEdgeIds.length === 1
      && route.carrierId === route.memberEdgeIds[0]
    ) {
      renderedDirectCarrierPointsByEdgeId.set(route.carrierId, route.points);
    }
  }
  const baseRoutedEdges = payload.layout.routedEdges
    .map((route) =>
      createEdgeRenderModel(
        route,
        structuralEdgeById,
        renderedDirectCarrierPointsByEdgeId,
      ),
    )
    .filter((edge): edge is EdgeRenderModel => Boolean(edge));
  const routedEdges = baseRoutedEdges;
  const catalogEdges = modelCatalogMode
    ? payload.graph.structuralEdges
        .map((edge) => createCatalogEdgeRenderModel(edge, layoutNodesById))
        .filter((edge): edge is EdgeRenderModel => Boolean(edge))
    : [];
  const renderedTableByModelId = new Map(
    renderedTables.map((table) => [table.modelId, table] as const),
  );
  const straightRenderedEdges = (
    modelCatalogMode && routedEdges.length === 0
      ? catalogEdges
      : routedEdges
  )
    .map(enforceStraightRenderedEdge)
    .map((edge) => reconnectStraightEdgeToRenderedTables(
      edge,
      renderedTableByModelId,
    ));
  const selfRelationshipCountByModelId = new Map<ModelId, number>();
  for (const edge of payload.graph.structuralEdges) {
    if (edge.sourceModelId !== edge.targetModelId || edge.provenance !== "declared") {
      continue;
    }
    selfRelationshipCountByModelId.set(
      edge.sourceModelId,
      (selfRelationshipCountByModelId.get(edge.sourceModelId) ?? 0) + 1,
    );
  }
  renderedTables = renderedTables.map((table) => {
    const selfRelationshipCount = selfRelationshipCountByModelId.get(table.modelId) ?? 0;
    return selfRelationshipCount > 0 ? { ...table, selfRelationshipCount } : table;
  });
  const directRelationshipAudit = modelCatalogMode
    ? createRelationshipFaithfulCarrierEdges(
        payload.layout.routedEdges.length > 0
          ? payload.layout.routedEdges.map((route) => route.edgeId)
          : payload.graph.structuralEdges
              .filter((edge) => edge.provenance === "declared")
              .map((edge) => edge.id),
        renderedTables,
        structuralEdgeById,
        straightRenderedEdges,
      )
    : undefined;
  // Only the explicit June overview consumes complete group memberships.
  // Ordinary and historical snapshots retain their individual routed lines.
  const overview = payload.layout.engineMetadata?.relationshipPresentation === "june-bundled"
    ? createJuneRelationshipOverview(
        straightRenderedEdges,
        payload.layout.engineMetadata.renderedCarrierRoutes ?? [],
      )
    : undefined;
  const renderedEdges = overview?.edges ?? straightRenderedEdges;

  const overlays = modelCatalogMode
    ? []
    : payload.graph.methodAssociations
        .map((association) => {
          const source = layoutNodesById.get(association.sourceModelId);
          const target = layoutNodesById.get(association.targetModelId);
          if (!source || !target) {
            return undefined;
          }

          return {
            confidence: association.confidence,
            id: association.id,
            methodName: association.methodName,
            sourceModelId: association.sourceModelId,
            targetModelId: association.targetModelId,
            x1: centerX(source),
            x2: centerX(target),
            y1: centerY(source),
            y2: centerY(target),
          } satisfies MethodOverlayRenderModel;
        })
        .filter(isDefined);

  // Cluster outlines: bbox per Louvain cluster (>=2 members) so the
  // renderer can draw a faint rectangle around each cluster, restoring
  // visual cluster grouping after cross-reduction passes (CPT, scaling,
  // etc.) move clusters as units but visually mix them with neighbors.
  const clusterOutlines = computeClusterOutlines(
    renderedTables,
    payload.graph.structuralEdges,
  );

  const result: DiagramRenderModel = {
    bundleLeafTiles,
    bundleLeavesByFakeId,
    canvas: canvasSize(payload, renderedTables, modelCatalogMode && routedEdges.length === 0),
    canonicalCrossing: createCanonicalCrossingRenderModel(
      payload.layout.engineMetadata?.canonicalCrossing,
    ),
    clusterOutlines,
    crossings: modelCatalogMode ? [] : payload.layout.crossings,
    edges: renderedEdges,
    inspector: {
      diagnostics: createDiagnostics(payload),
      discovery: discovery ? createDiscoveryRenderModel(discovery) : undefined,
      // The canvas may use catalog cards with their rows removed, but the
      // inspector must retain the analyzer's complete model data so users can
      // audit every relationship represented by the rendered carrier graph.
      models: allTables.map((table) => createInspectorModelRenderModel(
        table,
        inspectorRelationshipsByModelId.get(table.modelId) ?? [],
      )),
      selectedMethodName: payload.view.selectedMethodContext?.methodName,
      selectedModelId: payload.view.selectedModelId,
    },
    layoutExecution: createLayoutExecution(payload),
    layoutFailures: createLayoutFailures(payload),
    layoutMode: payload.view.layoutMode,
    leafBundles,
    leafCards: createLeafCards(leafBundles, renderedTables),
    modelCatalogMode,
    overlays,
    timings: payload.timings,
    tables: renderedTables,
    semanticCarriers: directRelationshipAudit?.diagnostics,
    visualCrossings: payload.layout.engineMetadata?.visualCrossings,
  };
  if (payload.layout.engineMetadata?.relationshipPresentation === "june-bundled") {
    result.visualCrossings = measureRenderedVisualConflicts(result).visualCrossings;
  }
  if (overview) {
    const visualCrossings = result.visualCrossings!;
    result.relationshipOverview = {
      groupCount: overview.groupCount,
      individualEdges: straightRenderedEdges,
      individualVisualCrossings: measureRenderedVisualConflicts({
        ...result,
        edges: straightRenderedEdges,
      }).visualCrossings,
      visualCrossings,
    };
    result.visualCrossings = visualCrossings;
  }
  if (result.leafCards.length) {
    const tools = createLeafCardConnectionTools();
    const visible = new Set(renderedTables.filter(table => !table.hidden).map(table => table.modelId));
    const individualEdges = straightRenderedEdges.filter(edge => visible.has(edge.sourceModelId) && visible.has(edge.targetModelId));
    const cards = tools.bounds(result.leafCards, new Map(renderedTables.filter(table => !table.hidden).map(table => [table.modelId, {
      modelId: table.modelId, x: table.position.x, y: table.position.y, ...table.size,
    }])));
    const projection = tools.project(renderedEdges, individualEdges, cards);
    const baseVisualCrossings = measureRenderedVisualConflicts(result).visualCrossings;
    result.leafCardOverview = {
      baseEdges: renderedEdges, individualEdges: straightRenderedEdges,
      internalEdgeIds: projection.internalEdgeIds, connectionCount: projection.connectionCount,
      baseVisualCrossings, visualCrossings: 0, expandedVisualCrossings: 0, expandedEdgeCount: 0,
    };
    result.edges = projection.edges;
    result.visualCrossings = measureRenderedVisualConflicts(result).visualCrossings;
    result.leafCardOverview.visualCrossings = result.visualCrossings;
    const expanded = tools.project(individualEdges, individualEdges, cards);
    result.leafCardOverview.expandedVisualCrossings = measureRenderedVisualConflicts({...result, edges: expanded.edges}).visualCrossings;
    result.leafCardOverview.expandedEdgeCount = expanded.edges.length;
  }
  if (payload.layout.individualView && result.relationshipOverview) {
    const alternate = payload.layout.individualView;
    const nodes = new Map(alternate.nodes.map(node => [node.modelId, node]));
    const routes = new Map(alternate.routedEdges.map(edge => [edge.edgeId, edge]));
    const individualTables = renderedTables.map(table => {
      const node = nodes.get(table.modelId);
      if (!node || node.size.width !== table.size.width || node.size.height !== table.size.height) {
        throw new Error("Individual view must preserve all rendered cards and dimensions.");
      }
      return { ...table, position: { ...node.position } };
    });
    const individualById = new Map(individualTables.map(table => [table.modelId, table]));
    const individualEdges = straightRenderedEdges.map(edge => {
      const route = routes.get(edge.edgeId);
      if (!route || route.points.length !== 2) throw new Error("Individual view is missing a straight relationship.");
      const tables = [individualById.get(edge.sourceModelId), individualById.get(edge.targetModelId)];
      for (let end = 0; end < 2; end++) {
        const table = tables[end], point = route.points[end], peer = route.points[1 - end];
        if (!table) throw new Error("Individual view has a missing endpoint.");
        const { x: left, y: top } = table.position;
        const right = left + table.size.width, bottom = top + table.size.height, epsilon = .011;
        const outward = (Math.abs(point.x - left) <= epsilon && peer.x <= point.x + epsilon)
          || (Math.abs(point.x - right) <= epsilon && peer.x >= point.x - epsilon)
          || (Math.abs(point.y - top) <= epsilon && peer.y <= point.y + epsilon)
          || (Math.abs(point.y - bottom) <= epsilon && peer.y >= point.y - epsilon);
        if (!Number.isFinite(point.x) || !Number.isFinite(point.y) || point.x < left - epsilon || point.x > right + epsilon
          || point.y < top - epsilon || point.y > bottom + epsilon || !outward
          || Math.hypot(point.x - peer.x, point.y - peer.y) <= epsilon) {
          throw new Error("Individual view route must connect outward-facing card boundaries.");
        }
      }
      return { ...edge, points: serializeRenderedEdgePoints(route.points), preserveRouteEndpoints: true };
    });
    const individualScene = { ...result, tables: individualTables, edges: individualEdges,
      leafCards: [], leafBundles: [], bundleLeafTiles: [], leafCardOverview: undefined, relationshipOverview: undefined };
    result.individualView = {
      positions: Object.fromEntries(individualTables.map(table => [table.modelId, table.position])),
      edges: individualEdges,
      clusterOutlines: computeClusterOutlines(individualTables, payload.graph.structuralEdges),
      visualCrossings: measureRenderedVisualConflicts(individualScene).visualCrossings,
    };
  }
  return result;
}

function createCanonicalCrossingRenderModel(
  metadata: CanonicalCrossingMetadata | undefined,
): CanonicalCrossingRenderModel | undefined {
  if (!metadata) {
    return undefined;
  }

  return {
    adjacentEdgeIntersections: metadata.adjacentEdgeIntersections,
    boundViolation: metadata.boundViolation,
    collinearOverlaps: metadata.collinearOverlaps,
    completeRoutes: metadata.completeRoutes,
    degenerateSegments: metadata.degenerateSegments,
    gap: metadata.gap,
    invariantViolations: metadata.invariantViolations,
    lowerBound: metadata.lowerBound,
    nonIncidentNodeHits: metadata.nonIncidentNodeHits,
    nonProperContacts: metadata.nonProperContacts,
    optimality: metadata.optimality,
    pointContacts: metadata.pointContacts,
    properDrawing: metadata.properDrawing,
    routeCrossingPairs: metadata.routeCrossingPairs,
    selfIntersections: metadata.selfIntersections,
  };
}

function computeClusterOutlines(
  tables: TableRenderModel[],
  edges: StructuralGraphEdge[],
): ClusterOutline[] {
  // Skip synthetic bundle tables (their clusterId mirrors parent's, but
  // including the bundle frame would double-cover bundle leaves).
  const membersByCluster = new Map<string, TableRenderModel[]>();
  for (const t of tables) {
    if (String(t.modelId).startsWith("__leafbundle.")) continue;
    if (t.hidden) continue;
    if (!t.clusterId) continue;
    const list = membersByCluster.get(t.clusterId) ?? [];
    list.push(t);
    membersByCluster.set(t.clusterId, list);
  }
  const degreeByModelId = new Map<ModelId, number>();
  for (const edge of edges) {
    degreeByModelId.set(
      edge.sourceModelId,
      (degreeByModelId.get(edge.sourceModelId) ?? 0) + 1,
    );
    degreeByModelId.set(
      edge.targetModelId,
      (degreeByModelId.get(edge.targetModelId) ?? 0) + 1,
    );
  }
  const outlines: ClusterOutline[] = [];
  const PAD = 24;
  for (const [clusterId, members] of membersByCluster) {
    if (members.length < 2) continue;  // singleton clusters: no outline
    let xMin = Infinity, yMin = Infinity, xMax = -Infinity, yMax = -Infinity;
    for (const m of members) {
      xMin = Math.min(xMin, m.position.x);
      yMin = Math.min(yMin, m.position.y);
      xMax = Math.max(xMax, m.position.x + m.size.width);
      yMax = Math.max(yMax, m.position.y + m.size.height);
    }
    if (!Number.isFinite(xMin)) continue;
    const anchor = [...members].sort((left, right) =>
      (degreeByModelId.get(right.modelId) ?? 0)
        - (degreeByModelId.get(left.modelId) ?? 0)
      || left.modelName.localeCompare(right.modelName)
    )[0];
    outlines.push({
      bbox: {
        height: yMax - yMin + 2 * PAD,
        width: xMax - xMin + 2 * PAD,
        x: xMin - PAD,
        y: yMin - PAD,
      },
      clusterId,
      colorKey: clusterId,
      label: `${anchor.modelName} cluster`,
      memberCount: members.length,
    });
  }
  return outlines.sort((left, right) =>
    right.bbox.width * right.bbox.height
      - left.bbox.width * left.bbox.height
    || left.clusterId.localeCompare(right.clusterId)
  );
}

function createCatalogRelationDegreeByModel(
  edges: StructuralGraphEdge[],
  layoutNodesById: Map<ModelId, DiagramBootstrapPayload["layout"]["nodes"][number]>,
): Map<ModelId, number> {
  const degreeByModel = new Map<ModelId, number>();

  for (const edge of edges) {
    if (
      edge.sourceModelId === edge.targetModelId ||
      !layoutNodesById.has(edge.sourceModelId) ||
      !layoutNodesById.has(edge.targetModelId)
    ) {
      continue;
    }

    degreeByModel.set(edge.sourceModelId, (degreeByModel.get(edge.sourceModelId) ?? 0) + 1);
    degreeByModel.set(edge.targetModelId, (degreeByModel.get(edge.targetModelId) ?? 0) + 1);
  }

  return degreeByModel;
}

function createCatalogEdgeRenderModel(
  edge: StructuralGraphEdge,
  layoutNodesById: Map<ModelId, DiagramBootstrapPayload["layout"]["nodes"][number]>,
): EdgeRenderModel | undefined {
  if (
    edge.sourceModelId === edge.targetModelId ||
    !layoutNodesById.has(edge.sourceModelId) ||
    !layoutNodesById.has(edge.targetModelId)
  ) {
    return undefined;
  }

  const [markerStartId, markerEndId] = markerIds(edge.kind);

  return {
    carrierFamily: edge.kind === "inheritance" ? "inheritance" : "association",
    carrierRole: "direct",
    crossingIds: [],
    cssKind: edge.kind.replaceAll("_", "-"),
    edgeId: edge.id,
    markerEndId,
    markerStartId,
    memberEdgeIds: [edge.id],
    logicalEndpointModelIds: [edge.sourceModelId, edge.targetModelId],
    physicalEndpointModelIds: [edge.sourceModelId, edge.targetModelId],
    points: "",
    provenance: edge.provenance,
    sourceModelId: edge.sourceModelId,
    targetModelId: edge.targetModelId,
  };
}

type SemanticCarrierFamily = "association" | "inheritance" | "mixed";

interface VisibleSemanticRelationshipUnit {
  directEdge: EdgeRenderModel;
  memberEdgeIds: string[];
  relationship: StructuralGraphEdge;
  sourceTable: TableRenderModel;
  targetTable: TableRenderModel;
}

interface VisibleSemanticBundleGroup {
  bundleId: string;
  kind: StructuralGraphEdge["kind"];
  targetTable: TableRenderModel;
  units: VisibleSemanticRelationshipUnit[];
}

interface VisibleSemanticSegment {
  end: Point;
  physicalEndpointModelIds: ReadonlySet<ModelId>;
  start: Point;
}

interface VisibleSemanticReplacementCost {
  crossings: number;
  length: number;
  obstacleIntersections: number;
}

/**
 * Preserves the declared ERD topology without fabricating relationship nodes
 * or merging relationships into shared carrier geometry.
 *
 * A node bundle is layout/selection metadata for a group of real model cards;
 * it is not an edge bundle. Every non-self relationship therefore produces one
 * independent straight segment whose two physical endpoints are exactly its
 * declared source and target models. A self relation remains represented by
 * the badge on its real table and by its inspector row because a straight
 * segment cannot leave and re-enter the same rectangle without a bend.
 */
function createRelationshipFaithfulCarrierEdges(
  canonicalRelationshipIds: readonly string[],
  tables: TableRenderModel[],
  structuralEdgeById: Map<string, StructuralGraphEdge>,
  canonicalRenderedEdges: readonly EdgeRenderModel[],
): SemanticCarrierResult {
  const visibleTableByModelId = new Map(
    tables
      .filter((table) => !table.hidden)
      .map((table) => [table.modelId, table] as const),
  );
  // The native routed-edge set is already the canonical consolidation of
  // declared and derived-reverse graph records. Carrier member lists may come
  // from the unconsolidated structural graph, so using them here can silently
  // re-expand 1,684 routed relationships into almost 2,000 visible lines.
  const expectedRelationshipIds = new Set(canonicalRelationshipIds);
  for (const relationship of structuralEdgeById.values()) {
    if (
      relationship.sourceModelId === relationship.targetModelId
      && relationship.provenance === "declared"
      && visibleTableByModelId.has(relationship.sourceModelId)
    ) {
      expectedRelationshipIds.add(relationship.id);
    }
  }
  const relationships = [...expectedRelationshipIds]
    .map((edgeId) => structuralEdgeById.get(edgeId))
    .filter((edge): edge is StructuralGraphEdge => edge !== undefined)
    .sort((left, right) =>
      String(left.sourceModelId).localeCompare(String(right.sourceModelId))
      || String(left.targetModelId).localeCompare(String(right.targetModelId))
      || left.kind.localeCompare(right.kind)
      || left.id.localeCompare(right.id)
    );
  const canonicalRenderedEdgeById = new Map<string, EdgeRenderModel>();
  for (const edge of canonicalRenderedEdges) {
    canonicalRenderedEdgeById.set(edge.edgeId, edge);
    for (const memberEdgeId of edge.memberEdgeIds ?? []) {
      canonicalRenderedEdgeById.set(memberEdgeId, edge);
    }
  }
  const selfRelationships = relationships.filter((relationship) =>
    relationship.sourceModelId === relationship.targetModelId
  );
  const obstacleIndex = createRenderedObstacleIndex([...visibleTableByModelId.values()]);
  const relationshipUnits: VisibleSemanticRelationshipUnit[] = [];
  let fallbackRelationships = 0;
  for (const relationship of relationships) {
    if (relationship.sourceModelId === relationship.targetModelId) {
      continue;
    }
    const sourceTable = visibleTableByModelId.get(relationship.sourceModelId);
    const targetTable = visibleTableByModelId.get(relationship.targetModelId);
    if (!sourceTable || !targetTable) {
      continue;
    }
    const suppliedEdge = canonicalRenderedEdgeById.get(relationship.id);
    const suppliedPoints = suppliedEdge
      ? parseRenderedEdgePoints(suppliedEdge.points)
      : [];
    // Native layout already optimized these exact straight boundary endpoints.
    // Preserve them only while they still touch their declared tables; stale
    // route points from a position-only cache are otherwise visibly detached.
    const suppliedRouteIsConnected = suppliedPoints.length >= 2
      && suppliedEdge?.sourceModelId === sourceTable.modelId
      && suppliedEdge.targetModelId === targetTable.modelId
      && renderedPointTouchesTable(suppliedPoints[0], sourceTable)
      && renderedPointTouchesTable(
        suppliedPoints[suppliedPoints.length - 1],
        targetTable,
      );
    const points = suppliedRouteIsConnected
      ? [suppliedPoints[0], suppliedPoints[suppliedPoints.length - 1]]
      : buildStraightRenderedEdgePoints(sourceTable, targetTable);
    if (!suppliedRouteIsConnected) {
      fallbackRelationships += 1;
    }
    const edge = createDirectRelationshipEdge(
      relationship,
      sourceTable,
      targetTable,
      points,
    );
    edge.memberEdgeIds = [relationship.id];
    edge.crossingIds = suppliedEdge?.crossingIds ?? [];
    edge.logicalEndpointModelIds = [relationship.sourceModelId, relationship.targetModelId];
    relationshipUnits.push({
      directEdge: edge,
      memberEdgeIds: edge.memberEdgeIds,
      relationship,
      sourceTable,
      targetTable,
    });
  }

  const edges = relationshipUnits.map((unit) => unit.directEdge);
  const bundleGroups = 0;
  const bundledRelationships = 0;
  const semanticCrossingsAvoided = 0;
  const semanticObstacleIntersectionsAvoided = 0;
  const eligibleRelationshipIds = new Set(
    relationshipUnits.flatMap((unit) => unit.memberEdgeIds),
  );
  edges.sort((left, right) => left.edgeId.localeCompare(right.edgeId));

  const representedRelationshipIds = new Set(
    [
      ...edges.flatMap((edge) => edge.memberEdgeIds ?? [edge.edgeId]),
      ...selfRelationships
        .filter((relationship) => visibleTableByModelId.has(relationship.sourceModelId))
        .map((relationship) => relationship.id),
    ],
  );
  const missingRelationships = [...expectedRelationshipIds]
    .filter((edgeId) => !representedRelationshipIds.has(edgeId))
    .sort();
  const disconnectedRelationships = relationships.flatMap((relationship) =>
    relationship.sourceModelId === relationship.targetModelId
      ? []
      : visibleRelationshipPathConnectsEndpoints(
          relationship,
          edges,
          visibleTableByModelId,
        )
        ? []
        : [relationship.id]
  ).sort();
  const obstacleIntersections = countVisibleSemanticEdgeObstacles(
    edges,
    obstacleIndex,
  );

  return {
    diagnostics: {
      active: false,
      bundleGroups,
      bundledRelationships,
      carrierSegments: edges.length,
      coincidentRelationships: 0,
      disconnectedRelationships,
      eligibleRelationships: eligibleRelationshipIds.size,
      fallbackRelationships,
      missingRelationships,
      obstacleIntersections,
      relationships: expectedRelationshipIds.size,
      semanticCrossingsAvoided,
      semanticObstacleIntersectionsAvoided,
      selfRelationships: selfRelationships.length,
    },
    edges,
  };
}

interface VisibleSemanticKindNetworkResult {
  bundleGroups: number;
  bundledRelationships: number;
  edges: EdgeRenderModel[];
}

interface VisibleSemanticKindTreeLink {
  kind: StructuralGraphEdge["kind"];
  memberEdgeIds: Set<string>;
  sourceModelId: ModelId;
  targetModelId: ModelId;
}

interface VisibleSemanticKindVertexCandidate {
  leafPoint: Point;
  point: Point;
  preference: number;
}

interface VisibleSemanticKindVertex {
  candidateIndex: number;
  candidates: VisibleSemanticKindVertexCandidate[];
  incidentLinkIndices: number[];
  kind: StructuralGraphEdge["kind"];
  modelId: ModelId;
  originalPorts: Point[];
  sourceMemberEdgeIds: Set<string>;
  table: TableRenderModel;
  targetMemberEdgeIds: Set<string>;
}

/**
 * Builds one visible coordinate-only carrier network per exact relationship
 * kind. A model card is always a leaf of that network: shared trunks terminate
 * at unlabeled junction coordinates outside cards, never at an unrelated real
 * model or a synthetic proxy card. Every segment retains the IDs of the real
 * relationships that use it, which keeps selection and endpoint connectivity
 * exact even though repeated visual strokes are drawn once.
 */
function createVisibleSemanticKindCarrierNetwork(
  units: VisibleSemanticRelationshipUnit[],
  tableByModelId: ReadonlyMap<ModelId, TableRenderModel>,
  obstacleIndex: RenderedObstacleIndex,
): VisibleSemanticKindNetworkResult {
  const unitsByKind = new Map<
    StructuralGraphEdge["kind"],
    VisibleSemanticRelationshipUnit[]
  >();
  const unitByMemberEdgeId = new Map<string, VisibleSemanticRelationshipUnit>();
  for (const unit of units) {
    const kindUnits = unitsByKind.get(unit.relationship.kind) ?? [];
    kindUnits.push(unit);
    unitsByKind.set(unit.relationship.kind, kindUnits);
    for (const memberEdgeId of unit.memberEdgeIds) {
      unitByMemberEdgeId.set(memberEdgeId, unit);
    }
  }

  const treeLinks: VisibleSemanticKindTreeLink[] = [];
  for (const [kind, kindUnits] of [...unitsByKind].sort(([left], [right]) =>
    left.localeCompare(right)
  )) {
    const modelIds = [...new Set<ModelId>(
      kindUnits.flatMap((unit) => [
        unit.relationship.sourceModelId,
        unit.relationship.targetModelId,
      ]),
    )].sort();
    if (modelIds.length < 2) {
      continue;
    }
    const family: Exclude<SemanticCarrierFamily, "mixed"> =
      kind === "inheritance" ? "inheritance" : "association";
    const sourceEdges: SemanticCarrierSourceEdge[] = kindUnits.map((unit) => ({
      family,
      memberEdgeIds: unit.memberEdgeIds,
      renderEdge: unit.directEdge,
      sourceModelId: unit.relationship.sourceModelId,
      targetModelId: unit.relationship.targetModelId,
    }));
    // The sorted bridges only guarantee that disconnected components of the
    // same exact kind participate in one carrier infrastructure. They are not
    // rendered and have no relationship IDs; real paths alone decide which
    // final tree links remain visible.
    const representative = sourceEdges[0];
    const bridgeEdges = modelIds.slice(1).map((targetModelId, index) => ({
      ...representative,
      memberEdgeIds: [],
      sourceModelId: modelIds[index],
      targetModelId,
    }));
    const links = buildCollisionAwareSemanticSpanningTree(
      modelIds,
      [...sourceEdges, ...bridgeEdges],
      new Map(tableByModelId),
      obstacleIndex,
    );
    if (links.length + 1 !== modelIds.length) {
      continue;
    }
    const adjacency = new Map<
      ModelId,
      Array<{ linkIndex: number; modelId: ModelId }>
    >(modelIds.map((modelId) => [modelId, []]));
    const memberEdgeIdsByLink = links.map(() => new Set<string>());
    links.forEach((link, linkIndex) => {
      adjacency.get(link.sourceModelId)?.push({
        linkIndex,
        modelId: link.targetModelId,
      });
      adjacency.get(link.targetModelId)?.push({
        linkIndex,
        modelId: link.sourceModelId,
      });
    });
    for (const unit of kindUnits) {
      const pathLinkIndices = findSemanticCarrierTreePath(
        unit.relationship.sourceModelId,
        unit.relationship.targetModelId,
        adjacency,
      );
      for (const linkIndex of pathLinkIndices) {
        for (const memberEdgeId of unit.memberEdgeIds) {
          memberEdgeIdsByLink[linkIndex].add(memberEdgeId);
        }
      }
    }
    links.forEach((link, linkIndex) => {
      if (memberEdgeIdsByLink[linkIndex].size === 0) {
        return;
      }
      treeLinks.push({
        kind,
        memberEdgeIds: memberEdgeIdsByLink[linkIndex],
        sourceModelId: link.sourceModelId,
        targetModelId: link.targetModelId,
      });
    });
  }

  const vertexKey = (kind: StructuralGraphEdge["kind"], modelId: ModelId): string =>
    `${kind}\u0000${modelId}`;
  const vertices = new Map<string, VisibleSemanticKindVertex>();
  const ensureVertex = (
    kind: StructuralGraphEdge["kind"],
    modelId: ModelId,
  ): VisibleSemanticKindVertex | undefined => {
    const key = vertexKey(kind, modelId);
    const existing = vertices.get(key);
    if (existing) {
      return existing;
    }
    const table = tableByModelId.get(modelId);
    if (!table) {
      return undefined;
    }
    const vertex: VisibleSemanticKindVertex = {
      candidateIndex: 0,
      candidates: [],
      incidentLinkIndices: [],
      kind,
      modelId,
      originalPorts: [],
      sourceMemberEdgeIds: new Set(),
      table,
      targetMemberEdgeIds: new Set(),
    };
    vertices.set(key, vertex);
    return vertex;
  };
  treeLinks.forEach((link, linkIndex) => {
    const source = ensureVertex(link.kind, link.sourceModelId);
    const target = ensureVertex(link.kind, link.targetModelId);
    if (!source || !target) {
      return;
    }
    source.incidentLinkIndices.push(linkIndex);
    target.incidentLinkIndices.push(linkIndex);
    source.originalPorts.push(
      computeRenderedBoundaryPort(source.table, renderedTableCenter(target.table)),
    );
    target.originalPorts.push(
      computeRenderedBoundaryPort(target.table, renderedTableCenter(source.table)),
    );
  });
  for (const unit of units) {
    const source = ensureVertex(
      unit.relationship.kind,
      unit.relationship.sourceModelId,
    );
    const target = ensureVertex(
      unit.relationship.kind,
      unit.relationship.targetModelId,
    );
    for (const memberEdgeId of unit.memberEdgeIds) {
      source?.sourceMemberEdgeIds.add(memberEdgeId);
      target?.targetMemberEdgeIds.add(memberEdgeId);
    }
  }

  for (const vertex of vertices.values()) {
    vertex.candidates = createVisibleSemanticKindVertexCandidates(vertex);
    vertex.candidates.sort((left, right) =>
      countRenderedSegmentTableIntersections(
        left.leafPoint,
        left.point,
        obstacleIndex,
        new Set([vertex.modelId]),
      ) - countRenderedSegmentTableIntersections(
        right.leafPoint,
        right.point,
        obstacleIndex,
        new Set([vertex.modelId]),
      )
      || left.preference - right.preference
    );
    vertex.candidateIndex = 0;
  }

  const localCost = (
    key: string,
    candidateIndex: number,
  ): {
    leafObstacles: number;
    length: number;
    obstacles: number;
    preference: number;
  } => {
    const vertex = vertices.get(key)!;
    const candidate = vertex.candidates[candidateIndex];
    const leafObstacles = countRenderedSegmentTableIntersections(
      candidate.leafPoint,
      candidate.point,
      obstacleIndex,
      new Set([vertex.modelId]),
    );
    let trunkObstacles = 0;
    let length = Math.hypot(
      candidate.point.x - candidate.leafPoint.x,
      candidate.point.y - candidate.leafPoint.y,
    );
    for (const linkIndex of vertex.incidentLinkIndices) {
      const link = treeLinks[linkIndex];
      const peerModelId = link.sourceModelId === vertex.modelId
        ? link.targetModelId
        : link.sourceModelId;
      const peer = vertices.get(vertexKey(vertex.kind, peerModelId));
      if (!peer) {
        continue;
      }
      const peerPoint = peer.candidates[peer.candidateIndex].point;
      trunkObstacles += countRenderedSegmentTableIntersections(
        candidate.point,
        peerPoint,
        obstacleIndex,
        new Set(),
      );
      length += Math.hypot(
        candidate.point.x - peerPoint.x,
        candidate.point.y - peerPoint.y,
      );
    }
    return {
      leafObstacles,
      length,
      obstacles: leafObstacles + trunkObstacles,
      preference: candidate.preference,
    };
  };
  const orderedVertexKeys = [...vertices]
    .sort((left, right) =>
      right[1].incidentLinkIndices.length - left[1].incidentLinkIndices.length
      || left[0].localeCompare(right[0])
    )
    .map(([key]) => key);
  for (let sweep = 0; sweep < 8; sweep += 1) {
    let changed = 0;
    for (const key of orderedVertexKeys) {
      const vertex = vertices.get(key)!;
      let bestIndex = vertex.candidateIndex;
      let best = localCost(key, bestIndex);
      for (
        let candidateIndex = 0;
        candidateIndex < vertex.candidates.length;
        candidateIndex += 1
      ) {
        const candidate = localCost(key, candidateIndex);
        if (
          candidate.leafObstacles < best.leafObstacles
          || (candidate.leafObstacles === best.leafObstacles
            && candidate.obstacles < best.obstacles)
          || (candidate.leafObstacles === best.leafObstacles
            && candidate.obstacles === best.obstacles
            && candidate.length < best.length - 1e-6)
          || (candidate.leafObstacles === best.leafObstacles
            && candidate.obstacles === best.obstacles
            && Math.abs(candidate.length - best.length) <= 1e-6
            && candidate.preference < best.preference)
        ) {
          best = candidate;
          bestIndex = candidateIndex;
        }
      }
      if (bestIndex !== vertex.candidateIndex) {
        vertex.candidateIndex = bestIndex;
        changed += 1;
      }
    }
    if (changed === 0) {
      break;
    }
    orderedVertexKeys.reverse();
  }

  const unresolvedEdges: EdgeRenderModel[] = [];
  const sharedMemberEdgeIds = new Set<string>();
  let bundleGroups = 0;
  treeLinks.forEach((link, linkIndex) => {
    const source = vertices.get(vertexKey(link.kind, link.sourceModelId));
    const target = vertices.get(vertexKey(link.kind, link.targetModelId));
    const memberEdgeIds = [...link.memberEdgeIds].sort();
    const representative = memberEdgeIds
      .map((memberEdgeId) => unitByMemberEdgeId.get(memberEdgeId))
      .find((unit) => unit !== undefined);
    if (!source || !target || !representative || memberEdgeIds.length === 0) {
      return;
    }
    if (memberEdgeIds.length > 1) {
      bundleGroups += 1;
      memberEdgeIds.forEach((memberEdgeId) => sharedMemberEdgeIds.add(memberEdgeId));
    }
    unresolvedEdges.push({
      carrierFamily: link.kind === "inheritance" ? "inheritance" : "association",
      carrierRole: "semantic-trunk",
      crossingIds: [],
      cssKind: link.kind.replaceAll("_", "-"),
      edgeId: `semantic-kind:${link.kind}:trunk:${linkIndex}`,
      logicalEndpointModelIds: visibleSemanticLogicalEndpointModelIds(
        memberEdgeIds,
        unitByMemberEdgeId,
      ),
      markerEndId: "",
      markerStartId: "",
      memberEdgeIds,
      physicalEndpointModelIds: [],
      points: serializeRenderedEdgePoints([
        source.candidates[source.candidateIndex].point,
        target.candidates[target.candidateIndex].point,
      ]),
      preserveRouteEndpoints: true,
      provenance: "semantic_bundle",
      sourceModelId: representative.relationship.sourceModelId,
      targetModelId: representative.relationship.targetModelId,
    });
  });
  for (const [key, vertex] of [...vertices].sort(([left], [right]) =>
    left.localeCompare(right)
  )) {
    const sourceMemberEdgeIds = [...vertex.sourceMemberEdgeIds].sort();
    const targetMemberEdgeIds = [...vertex.targetMemberEdgeIds].sort();
    const memberEdgeIds = [...new Set([
      ...sourceMemberEdgeIds,
      ...targetMemberEdgeIds,
    ])].sort();
    if (memberEdgeIds.length === 0) {
      continue;
    }
    if (memberEdgeIds.length > 1) {
      bundleGroups += 1;
      memberEdgeIds.forEach((memberEdgeId) => sharedMemberEdgeIds.add(memberEdgeId));
    }
    const representative = (
      sourceMemberEdgeIds.length > 0 ? sourceMemberEdgeIds : targetMemberEdgeIds
    )
      .map((memberEdgeId) => unitByMemberEdgeId.get(memberEdgeId))
      .find((unit) => unit !== undefined);
    if (!representative) {
      continue;
    }
    const [kindMarkerStartId, kindMarkerEndId] = markerIds(vertex.kind);
    const candidate = vertex.candidates[vertex.candidateIndex];
    const sourceOrMixed = sourceMemberEdgeIds.length > 0;
    const directionIsUnambiguous = sourceMemberEdgeIds.length === 0
      || targetMemberEdgeIds.length === 0;
    unresolvedEdges.push({
      carrierFamily: vertex.kind === "inheritance" ? "inheritance" : "association",
      carrierRole: "semantic-branch",
      crossingIds: [],
      cssKind: vertex.kind.replaceAll("_", "-"),
      edgeId: `semantic-kind:${encodeURIComponent(key)}:leaf`,
      logicalEndpointModelIds: visibleSemanticLogicalEndpointModelIds(
        memberEdgeIds,
        unitByMemberEdgeId,
      ),
      markerEndId: !sourceOrMixed && directionIsUnambiguous ? kindMarkerEndId : "",
      markerStartId: sourceOrMixed && directionIsUnambiguous ? kindMarkerStartId : "",
      memberEdgeIds,
      physicalEndpointModelIds: [vertex.modelId],
      points: serializeRenderedEdgePoints(
        sourceOrMixed
          ? [candidate.leafPoint, candidate.point]
          : [candidate.point, candidate.leafPoint],
      ),
      preserveRouteEndpoints: true,
      provenance: "semantic_bundle",
      sourceModelId: representative.relationship.sourceModelId,
      targetModelId: representative.relationship.targetModelId,
    });
  }
  const edges = repairVisibleSemanticCarrierObstacles(
    unresolvedEdges,
    tableByModelId,
    obstacleIndex,
  ).sort((left, right) => left.edgeId.localeCompare(right.edgeId));
  return {
    bundleGroups,
    bundledRelationships: sharedMemberEdgeIds.size,
    edges,
  };
}

function createVisibleSemanticKindVertexCandidates(
  vertex: VisibleSemanticKindVertex,
): VisibleSemanticKindVertexCandidate[] {
  const table = vertex.table;
  const center = renderedTableCenter(table);
  const preferred = vertex.originalPorts.length > 0
    ? {
        x: vertex.originalPorts.reduce((sum, point) => sum + point.x, 0)
          / vertex.originalPorts.length,
        y: vertex.originalPorts.reduce((sum, point) => sum + point.y, 0)
          / vertex.originalPorts.length,
      }
    : center;
  const candidates: VisibleSemanticKindVertexCandidate[] = [];
  const seen = new Set<string>();
  for (const clearance of [10.5, 22, 46, 94, 160, 280]) {
    const left = table.position.x - clearance;
    const right = table.position.x + table.size.width + clearance;
    const top = table.position.y - clearance;
    const bottom = table.position.y + table.size.height + clearance;
    const rawCandidates: Array<{ leafPoint: Point; point: Point }> = [
      {
        leafPoint: { x: table.position.x, y: center.y },
        point: { x: left, y: center.y },
      },
      {
        leafPoint: { x: table.position.x + table.size.width, y: center.y },
        point: { x: right, y: center.y },
      },
      {
        leafPoint: { x: center.x, y: table.position.y },
        point: { x: center.x, y: top },
      },
      {
        leafPoint: { x: center.x, y: table.position.y + table.size.height },
        point: { x: center.x, y: bottom },
      },
      {
        leafPoint: { x: table.position.x, y: table.position.y },
        point: { x: left, y: top },
      },
      {
        leafPoint: {
          x: table.position.x + table.size.width,
          y: table.position.y,
        },
        point: { x: right, y: top },
      },
      {
        leafPoint: {
          x: table.position.x + table.size.width,
          y: table.position.y + table.size.height,
        },
        point: { x: right, y: bottom },
      },
      {
        leafPoint: {
          x: table.position.x,
          y: table.position.y + table.size.height,
        },
        point: { x: left, y: bottom },
      },
    ];
    for (const candidate of rawCandidates) {
      const key = `${candidate.point.x},${candidate.point.y}`;
      if (seen.has(key)) {
        continue;
      }
      seen.add(key);
      candidates.push({
        ...candidate,
        preference: Math.hypot(
          candidate.point.x - preferred.x,
          candidate.point.y - preferred.y,
        ),
      });
    }
  }
  return candidates;
}

function visibleSemanticLogicalEndpointModelIds(
  memberEdgeIds: readonly string[],
  unitByMemberEdgeId: ReadonlyMap<string, VisibleSemanticRelationshipUnit>,
): ModelId[] {
  return [...new Set<ModelId>(memberEdgeIds.flatMap((memberEdgeId) => {
    const unit = unitByMemberEdgeId.get(memberEdgeId);
    return unit
      ? [unit.relationship.sourceModelId, unit.relationship.targetModelId]
      : [];
  }))].sort();
}

function repairVisibleSemanticCarrierObstacles(
  edges: readonly EdgeRenderModel[],
  tableByModelId: ReadonlyMap<ModelId, TableRenderModel>,
  obstacleIndex: RenderedObstacleIndex,
): EdgeRenderModel[] {
  return edges.flatMap((edge) => {
    const points = parseRenderedEdgePoints(edge.points);
    if (points.length < 2) {
      return [];
    }
    const start = points[0];
    const end = points[points.length - 1];
    const ignoredModelIds = new Set<ModelId>(edge.physicalEndpointModelIds ?? []);
    const route = findVisibleSemanticObstacleFreeRoute(
      start,
      end,
      ignoredModelIds,
      obstacleIndex,
    );
    const physicalStartModelIds = [...ignoredModelIds].filter((modelId) => {
      const table = tableByModelId.get(modelId);
      return table ? renderedPointTouchesTable(start, table) : false;
    });
    const physicalEndModelIds = [...ignoredModelIds].filter((modelId) => {
      const table = tableByModelId.get(modelId);
      return table ? renderedPointTouchesTable(end, table) : false;
    });
    return route.slice(1).map((routeEnd, index) => {
      const segmentIndex = index;
      const lastSegmentIndex = route.length - 2;
      return {
        ...edge,
        edgeId: route.length === 2
          ? edge.edgeId
          : `${edge.edgeId}:clear:${segmentIndex}`,
        markerEndId: segmentIndex === lastSegmentIndex ? edge.markerEndId : "",
        markerStartId: segmentIndex === 0 ? edge.markerStartId : "",
        physicalEndpointModelIds: [...new Set<ModelId>([
          ...(segmentIndex === 0 ? physicalStartModelIds : []),
          ...(segmentIndex === lastSegmentIndex ? physicalEndModelIds : []),
        ])],
        points: serializeRenderedEdgePoints([route[index], routeEnd]),
      } satisfies EdgeRenderModel;
    });
  });
}

function findVisibleSemanticObstacleFreeRoute(
  start: Point,
  end: Point,
  ignoredModelIds: ReadonlySet<ModelId>,
  obstacleIndex: RenderedObstacleIndex,
): Point[] {
  const directHitTables = visibleSemanticSegmentHitTables(
    start,
    end,
    ignoredModelIds,
    obstacleIndex,
  );
  if (directHitTables.length === 0) {
    return [start, end];
  }
  const candidateTableById = new Map(
    directHitTables.map((table) => [table.modelId, table] as const),
  );
  let expanded = true;
  while (expanded) {
    expanded = false;
    const component = [...candidateTableById.values()];
    const left = Math.min(
      ...component.map((table) => table.position.x - 10.75),
    );
    const right = Math.max(
      ...component.map((table) => table.position.x + table.size.width + 10.75),
    );
    const top = Math.min(
      ...component.map((table) => table.position.y - 10.75),
    );
    const bottom = Math.max(
      ...component.map((table) => table.position.y + table.size.height + 10.75),
    );
    for (const table of obstacleIndex.visibleTables) {
      if (ignoredModelIds.has(table.modelId) || candidateTableById.has(table.modelId)) {
        continue;
      }
      const tableLeft = table.position.x - 10.75;
      const tableRight = table.position.x + table.size.width + 10.75;
      const tableTop = table.position.y - 10.75;
      const tableBottom = table.position.y + table.size.height + 10.75;
      if (
        tableRight >= left && tableLeft <= right
        && tableBottom >= top && tableTop <= bottom
      ) {
        candidateTableById.set(table.modelId, table);
        expanded = true;
      }
    }
  }
  const componentRoute = solveVisibleSemanticVisibilityGraph(
    start,
    end,
    [...candidateTableById.values()],
    ignoredModelIds,
    obstacleIndex,
  );
  if (componentRoute) {
    return componentRoute;
  }
  for (const margin of [600, 1_200, 2_400]) {
    const minX = Math.min(start.x, end.x) - margin;
    const maxX = Math.max(start.x, end.x) + margin;
    const minY = Math.min(start.y, end.y) - margin;
    const maxY = Math.max(start.y, end.y) + margin;
    const corridorTables = obstacleIndex.visibleTables.filter((table) =>
      !ignoredModelIds.has(table.modelId)
      && table.position.x + table.size.width >= minX
      && table.position.x <= maxX
      && table.position.y + table.size.height >= minY
      && table.position.y <= maxY
    );
    const route = solveVisibleSemanticVisibilityGraph(
      start,
      end,
      corridorTables,
      ignoredModelIds,
      obstacleIndex,
    );
    if (route) {
      return route;
    }
  }
  return [start, end];
}

function visibleSemanticSegmentHitTables(
  start: Point,
  end: Point,
  ignoredModelIds: ReadonlySet<ModelId>,
  obstacleIndex: RenderedObstacleIndex,
): TableRenderModel[] {
  return queryRenderedObstacles(obstacleIndex, start, end).filter((table) =>
    !ignoredModelIds.has(table.modelId)
    && segmentPenetratesRenderedTable(
      start,
      end,
      table,
      SEMANTIC_CARRIER_OBSTACLE_PADDING,
    )
  );
}

function solveVisibleSemanticVisibilityGraph(
  start: Point,
  end: Point,
  tables: readonly TableRenderModel[],
  ignoredModelIds: ReadonlySet<ModelId>,
  obstacleIndex: RenderedObstacleIndex,
): Point[] | undefined {
  if (tables.length === 0) {
    return undefined;
  }
  const candidates: Point[] = [start, end];
  const seen = new Set([`${start.x},${start.y}`, `${end.x},${end.y}`]);
  const addCandidate = (point: Point): void => {
    const rounded = { x: round2(point.x), y: round2(point.y) };
    const key = `${rounded.x},${rounded.y}`;
    if (!seen.has(key)) {
      seen.add(key);
      candidates.push(rounded);
    }
  };
  for (const table of tables) {
    const left = table.position.x - 10.75;
    const right = table.position.x + table.size.width + 10.75;
    const top = table.position.y - 10.75;
    const bottom = table.position.y + table.size.height + 10.75;
    addCandidate({ x: left, y: top });
    addCandidate({ x: right, y: top });
    addCandidate({ x: right, y: bottom });
    addCandidate({ x: left, y: bottom });
  }
  const distances = candidates.map(() => Number.POSITIVE_INFINITY);
  const previous = candidates.map(() => -1);
  const visited = candidates.map(() => false);
  distances[0] = 0;
  for (let iteration = 0; iteration < candidates.length; iteration += 1) {
    let current = -1;
    for (let index = 0; index < candidates.length; index += 1) {
      if (!visited[index] && (current < 0 || distances[index] < distances[current])) {
        current = index;
      }
    }
    if (current < 0 || !Number.isFinite(distances[current])) {
      break;
    }
    if (current === 1) {
      break;
    }
    visited[current] = true;
    for (let next = 0; next < candidates.length; next += 1) {
      if (
        next === current
        || visited[next]
        || countRenderedSegmentTableIntersections(
          candidates[current],
          candidates[next],
          obstacleIndex,
          ignoredModelIds,
        ) > 0
      ) {
        continue;
      }
      const distance = distances[current] + Math.hypot(
        candidates[next].x - candidates[current].x,
        candidates[next].y - candidates[current].y,
      );
      if (distance < distances[next] - 1e-9) {
        distances[next] = distance;
        previous[next] = current;
      }
    }
  }
  if (!Number.isFinite(distances[1])) {
    return undefined;
  }
  const reversed: Point[] = [];
  for (let cursor = 1; cursor >= 0; cursor = previous[cursor]) {
    reversed.push(candidates[cursor]);
    if (cursor === 0) {
      break;
    }
  }
  return reversed.reverse();
}

function createVisibleSemanticBundleGroups(
  units: VisibleSemanticRelationshipUnit[],
): VisibleSemanticBundleGroup[] {
  const buckets = new Map<string, VisibleSemanticRelationshipUnit[]>();
  for (const unit of units) {
    const key = `${unit.relationship.targetModelId}\u0000${unit.relationship.kind}`;
    const bucket = buckets.get(key) ?? [];
    bucket.push(unit);
    buckets.set(key, bucket);
  }

  const groups: VisibleSemanticBundleGroup[] = [];
  for (const [bucketKey, bucket] of [...buckets].sort(([left], [right]) =>
    left.localeCompare(right)
  )) {
    if (bucket.length < VISIBLE_SEMANTIC_BUNDLE_MIN_RELATIONSHIPS) {
      continue;
    }
    const targetCenter = renderedTableCenter(bucket[0].targetTable);
    const angularUnits = bucket.map((unit) => {
      const sourceCenter = renderedTableCenter(unit.sourceTable);
      let angle = Math.atan2(
        sourceCenter.y - targetCenter.y,
        sourceCenter.x - targetCenter.x,
      );
      if (angle < 0) angle += Math.PI * 2;
      return { angle, unit };
    }).sort((left, right) =>
      left.angle - right.angle
      || String(left.unit.relationship.sourceModelId).localeCompare(
        String(right.unit.relationship.sourceModelId),
      )
    );
    let cutAfter = angularUnits.length - 1;
    let largestGap = -1;
    for (let index = 0; index < angularUnits.length; index += 1) {
      const nextIndex = (index + 1) % angularUnits.length;
      const nextAngle = angularUnits[nextIndex].angle
        + (nextIndex === 0 ? Math.PI * 2 : 0);
      const gap = nextAngle - angularUnits[index].angle;
      if (gap > largestGap + 1e-9) {
        largestGap = gap;
        cutAfter = index;
      }
    }
    const ordered = [
      ...angularUnits.slice(cutAfter + 1),
      ...angularUnits.slice(0, cutAfter + 1),
    ].map(({ unit }) => unit);
    let groupCount = Math.max(
      1,
      Math.round(ordered.length / VISIBLE_SEMANTIC_BUNDLE_TARGET_GROUP_SIZE),
      Math.ceil(ordered.length / VISIBLE_SEMANTIC_BUNDLE_MAX_GROUP_SIZE),
    );
    while (
      groupCount > 1
      && Math.floor(ordered.length / groupCount)
        < VISIBLE_SEMANTIC_BUNDLE_MIN_RELATIONSHIPS
    ) {
      groupCount -= 1;
    }
    let offset = 0;
    for (let groupIndex = 0; groupIndex < groupCount; groupIndex += 1) {
      const remainingUnits = ordered.length - offset;
      const remainingGroups = groupCount - groupIndex;
      const size = Math.ceil(remainingUnits / remainingGroups);
      const groupedUnits = ordered.slice(offset, offset + size);
      offset += size;
      if (groupedUnits.length < VISIBLE_SEMANTIC_BUNDLE_MIN_RELATIONSHIPS) {
        continue;
      }
      groups.push({
        bundleId: `semantic-bundle:${encodeURIComponent(bucketKey)}:${groupIndex}`,
        kind: groupedUnits[0].relationship.kind,
        targetTable: groupedUnits[0].targetTable,
        units: groupedUnits,
      });
    }
  }
  return groups.sort((left, right) =>
    right.units.length - left.units.length
    || left.bundleId.localeCompare(right.bundleId)
  );
}

function createVisibleSemanticJunctionCandidates(
  group: VisibleSemanticBundleGroup,
): Point[] {
  const targetCenter = renderedTableCenter(group.targetTable);
  const sourceCenters = group.units.map((unit) => renderedTableCenter(unit.sourceTable));
  const centroid = {
    x: sourceCenters.reduce((sum, point) => sum + point.x, 0) / sourceCenters.length,
    y: sourceCenters.reduce((sum, point) => sum + point.y, 0) / sourceCenters.length,
  };
  let dx = centroid.x - targetCenter.x;
  let dy = centroid.y - targetCenter.y;
  let directionLength = Math.hypot(dx, dy);
  if (directionLength < 1) {
    const first = sourceCenters[0];
    dx = first.x - targetCenter.x;
    dy = first.y - targetCenter.y;
    directionLength = Math.max(1, Math.hypot(dx, dy));
  }
  const unitX = dx / directionLength;
  const unitY = dy / directionLength;
  const perpendicularX = -unitY;
  const perpendicularY = unitX;
  const sortedDistances = sourceCenters
    .map((point) => Math.hypot(point.x - targetCenter.x, point.y - targetCenter.y))
    .sort((left, right) => left - right);
  const medianDistance = sortedDistances[Math.floor(sortedDistances.length / 2)]
    ?? directionLength;
  const perpendicularScale = Math.max(160, Math.min(1_200, medianDistance * 0.18));
  const candidates: Point[] = [];
  const seen = new Set<string>();
  const addCandidate = (point: Point): void => {
    const rounded = { x: round2(point.x), y: round2(point.y) };
    const key = `${rounded.x},${rounded.y}`;
    if (!seen.has(key)) {
      seen.add(key);
      candidates.push(rounded);
    }
  };
  // Native port optimisation often found a useful target-side corridor that a
  // centre-based ray cannot reproduce. Extend each distinct audited target
  // port radially away from the real target card and consider that corridor as
  // a junction too.
  for (const unit of group.units) {
    const points = parseRenderedEdgePoints(unit.directEdge.points);
    const targetPoint = points[points.length - 1];
    if (!targetPoint) {
      continue;
    }
    let portDx = targetPoint.x - targetCenter.x;
    let portDy = targetPoint.y - targetCenter.y;
    let portLength = Math.hypot(portDx, portDy);
    if (portLength < 1) {
      const sourceCenter = renderedTableCenter(unit.sourceTable);
      portDx = sourceCenter.x - targetCenter.x;
      portDy = sourceCenter.y - targetCenter.y;
      portLength = Math.max(1, Math.hypot(portDx, portDy));
    }
    const portUnitX = portDx / portLength;
    const portUnitY = portDy / portLength;
    const portClearance = visibleSemanticPaddedExitDistance(
      targetPoint,
      { x: portUnitX, y: portUnitY },
      group.targetTable,
    );
    for (const extraDistance of [2, 48, 160, 416]) {
      const distance = portClearance + extraDistance;
      addCandidate({
        x: targetPoint.x + portUnitX * distance,
        y: targetPoint.y + portUnitY * distance,
      });
    }
  }
  const targetPort = computeRenderedBoundaryPort(group.targetTable, centroid);
  // Near-target candidates are the monotonic entry point for a bundle: they
  // differ only slightly from the original fan while still creating a visible
  // shared trunk. Distances are measured from the real card boundary (not its
  // centre), and the first one clears the padded target rectangle even on a
  // diagonal approach.
  const paddedClearance = 16 / Math.max(0.01, Math.max(Math.abs(unitX), Math.abs(unitY)));
  for (const radialDistance of [
    paddedClearance,
    paddedClearance + 24,
    paddedClearance + 56,
    paddedClearance + 120,
    paddedClearance + 248,
    paddedClearance + 504,
  ]) {
    for (const offsetFactor of [0, -0.3, 0.3]) {
      const localPerpendicular = Math.min(
        perpendicularScale,
        Math.max(24, radialDistance * 0.45),
      );
      addCandidate({
        x: round2(targetPort.x + unitX * radialDistance
          + perpendicularX * localPerpendicular * offsetFactor),
        y: round2(targetPort.y + unitY * radialDistance
          + perpendicularY * localPerpendicular * offsetFactor),
      });
    }
  }
  for (const fraction of [0.18, 0.3, 0.42, 0.56, 0.7, 0.84]) {
    for (const offsetFactor of [0, -0.35, 0.35, -0.7, 0.7]) {
      addCandidate({
        x: round2(targetCenter.x + dx * fraction
          + perpendicularX * perpendicularScale * offsetFactor),
        y: round2(targetCenter.y + dy * fraction
          + perpendicularY * perpendicularScale * offsetFactor),
      });
    }
  }
  return candidates;
}

function visibleSemanticPaddedExitDistance(
  point: Point,
  direction: Point,
  table: TableRenderModel,
  padding = SEMANTIC_CARRIER_OBSTACLE_PADDING + 2,
): number {
  const left = table.position.x - padding;
  const right = table.position.x + table.size.width + padding;
  const top = table.position.y - padding;
  const bottom = table.position.y + table.size.height + padding;
  if (
    point.x < left || point.x > right
    || point.y < top || point.y > bottom
  ) {
    return 0;
  }
  const exits: number[] = [];
  if (direction.x > 1e-9) exits.push((right - point.x) / direction.x);
  if (direction.x < -1e-9) exits.push((left - point.x) / direction.x);
  if (direction.y > 1e-9) exits.push((bottom - point.y) / direction.y);
  if (direction.y < -1e-9) exits.push((top - point.y) / direction.y);
  const positiveExits = exits.filter((value) => value >= 0);
  return positiveExits.length > 0 ? Math.min(...positiveExits) : 0;
}

function createVisibleSemanticBundleEdges(
  group: VisibleSemanticBundleGroup,
  junction: Point,
): EdgeRenderModel[] {
  const [markerStartId, markerEndId] = markerIds(group.kind);
  const carrierFamily = group.kind === "inheritance" ? "inheritance" : "association";
  const cssKind = group.kind.replaceAll("_", "-");
  const branches = group.units.flatMap((unit, index) => {
    const sourcePort = computeRenderedBoundaryPort(unit.sourceTable, junction);
    if (sameRenderedPoint(sourcePort, junction)) {
      return [];
    }
    return [{
      carrierFamily,
      carrierRole: "semantic-branch",
      crossingIds: [],
      cssKind,
      edgeId: `${group.bundleId}:branch:${index}`,
      logicalEndpointModelIds: [
        unit.relationship.sourceModelId,
        unit.relationship.targetModelId,
      ],
      markerEndId: "",
      markerStartId,
      memberEdgeIds: unit.memberEdgeIds,
      physicalEndpointModelIds: [unit.relationship.sourceModelId],
      points: serializeRenderedEdgePoints([sourcePort, junction]),
      preserveRouteEndpoints: true,
      provenance: unit.relationship.provenance,
      sourceModelId: unit.relationship.sourceModelId,
      targetModelId: unit.relationship.targetModelId,
    } satisfies EdgeRenderModel];
  });
  const targetPort = computeRenderedBoundaryPort(group.targetTable, junction);
  if (sameRenderedPoint(junction, targetPort)) {
    return [];
  }
  const memberEdgeIds = [...new Set(
    group.units.flatMap((unit) => unit.memberEdgeIds),
  )].sort();
  const logicalEndpointModelIds = [...new Set<ModelId>(
    group.units.flatMap((unit) => [
      unit.relationship.sourceModelId,
      unit.relationship.targetModelId,
    ]),
  )].sort();
  const representative = group.units[0].relationship;
  const trunk: EdgeRenderModel = {
    carrierFamily,
    carrierRole: "semantic-trunk",
    crossingIds: [],
    cssKind,
    edgeId: `${group.bundleId}:trunk`,
    logicalEndpointModelIds,
    markerEndId,
    markerStartId: "",
    memberEdgeIds,
    physicalEndpointModelIds: [representative.targetModelId],
    points: serializeRenderedEdgePoints([junction, targetPort]),
    preserveRouteEndpoints: true,
    provenance: "semantic_bundle",
    // Both IDs name real tables so the existing scene visibility contract can
    // keep this coordinate-only carrier; physical endpoints remain explicit.
    sourceModelId: representative.sourceModelId,
    targetModelId: representative.targetModelId,
  };
  return [...branches, trunk];
}

function measureVisibleSemanticReplacementCost(
  replacementEdges: EdgeRenderModel[],
  outsideEdges: EdgeRenderModel[],
  obstacleIndex: RenderedObstacleIndex,
  preparedOutsideSegments?: readonly VisibleSemanticSegment[],
): VisibleSemanticReplacementCost {
  const replacementSegments = createVisibleSemanticSegments(replacementEdges);
  const outsideSegments = preparedOutsideSegments
    ?? createVisibleSemanticSegments(outsideEdges);
  let crossings = 0;
  for (let leftIndex = 0; leftIndex < replacementSegments.length; leftIndex += 1) {
    const left = replacementSegments[leftIndex];
    for (let rightIndex = leftIndex + 1; rightIndex < replacementSegments.length; rightIndex += 1) {
      const right = replacementSegments[rightIndex];
      if (segmentsProperlyCross(left.start, left.end, right.start, right.end)) {
        crossings += 1;
      }
    }
    for (const outside of outsideSegments) {
      if (segmentsProperlyCross(left.start, left.end, outside.start, outside.end)) {
        crossings += 1;
      }
    }
  }
  let obstacleIntersections = 0;
  let length = 0;
  for (const segment of replacementSegments) {
    obstacleIntersections += countRenderedSegmentTableIntersections(
      segment.start,
      segment.end,
      obstacleIndex,
      segment.physicalEndpointModelIds,
    );
    length += Math.hypot(
      segment.end.x - segment.start.x,
      segment.end.y - segment.start.y,
    );
  }
  return { crossings, length, obstacleIntersections };
}

function createVisibleSemanticSegments(
  edges: readonly EdgeRenderModel[],
): VisibleSemanticSegment[] {
  return edges.flatMap((edge) => {
    const points = parseRenderedEdgePoints(edge.points);
    const physicalEndpointModelIds = new Set<ModelId>(
      edge.physicalEndpointModelIds ?? [edge.sourceModelId, edge.targetModelId],
    );
    return points.slice(1).flatMap((end, index) => {
      const start = points[index];
      return sameRenderedPoint(start, end)
        ? []
        : [{ end, physicalEndpointModelIds, start }];
    });
  });
}

function compareVisibleSemanticReplacementCost(
  left: VisibleSemanticReplacementCost,
  right: VisibleSemanticReplacementCost,
): number {
  return (left.obstacleIntersections + left.crossings)
      - (right.obstacleIntersections + right.crossings)
    || left.obstacleIntersections - right.obstacleIntersections
    || left.crossings - right.crossings
    || left.length - right.length;
}

function visibleRelationshipPathConnectsEndpoints(
  relationship: StructuralGraphEdge,
  edges: readonly EdgeRenderModel[],
  tableByModelId: ReadonlyMap<ModelId, TableRenderModel>,
): boolean {
  const sourceTable = tableByModelId.get(relationship.sourceModelId);
  const targetTable = tableByModelId.get(relationship.targetModelId);
  if (!sourceTable || !targetTable) {
    return false;
  }
  const adjacency = new Map<string, Set<string>>();
  const pointByKey = new Map<string, Point>();
  for (const edge of edges) {
    if (!(edge.memberEdgeIds ?? [edge.edgeId]).includes(relationship.id)) {
      continue;
    }
    const points = parseRenderedEdgePoints(edge.points);
    for (let index = 1; index < points.length; index += 1) {
      const start = points[index - 1];
      const end = points[index];
      const startKey = `${round2(start.x)},${round2(start.y)}`;
      const endKey = `${round2(end.x)},${round2(end.y)}`;
      pointByKey.set(startKey, start);
      pointByKey.set(endKey, end);
      const startNeighbors = adjacency.get(startKey) ?? new Set<string>();
      startNeighbors.add(endKey);
      adjacency.set(startKey, startNeighbors);
      const endNeighbors = adjacency.get(endKey) ?? new Set<string>();
      endNeighbors.add(startKey);
      adjacency.set(endKey, endNeighbors);
    }
  }
  const sourceKeys = [...pointByKey]
    .filter(([, point]) => renderedPointTouchesTable(point, sourceTable))
    .map(([key]) => key);
  const targetKeys = new Set(
    [...pointByKey]
      .filter(([, point]) => renderedPointTouchesTable(point, targetTable))
      .map(([key]) => key),
  );
  const seen = new Set(sourceKeys);
  const queue = [...sourceKeys];
  for (let index = 0; index < queue.length; index += 1) {
    const key = queue[index];
    if (targetKeys.has(key)) {
      return true;
    }
    for (const neighbor of adjacency.get(key) ?? []) {
      if (!seen.has(neighbor)) {
        seen.add(neighbor);
        queue.push(neighbor);
      }
    }
  }
  return false;
}

function countVisibleSemanticEdgeObstacles(
  edges: readonly EdgeRenderModel[],
  obstacleIndex: RenderedObstacleIndex,
): number {
  let intersections = 0;
  for (const edge of edges) {
    const ignoredObstacleModelIds = new Set<ModelId>(
      edge.physicalEndpointModelIds ?? [edge.sourceModelId, edge.targetModelId],
    );
    const points = parseRenderedEdgePoints(edge.points);
    for (let index = 1; index < points.length; index += 1) {
      intersections += countRenderedSegmentTableIntersections(
        points[index - 1],
        points[index],
        obstacleIndex,
        ignoredObstacleModelIds,
      );
    }
  }
  return intersections;
}

interface VisibleSemanticHierarchyResult {
  bundleGroups: number;
  crossingsAvoided: number;
  edges: EdgeRenderModel[];
  obstacleIntersectionsAvoided: number;
}

interface VisibleSemanticOutgoingResult extends VisibleSemanticHierarchyResult {
  bundledRelationships: number;
  eligibleRelationshipIds: string[];
}

function createVisibleSemanticOutgoingBundles(
  initialEdges: EdgeRenderModel[],
  units: VisibleSemanticRelationshipUnit[],
  obstacleIndex: RenderedObstacleIndex,
): VisibleSemanticOutgoingResult {
  const unitsBySourceAndKind = new Map<string, VisibleSemanticRelationshipUnit[]>();
  for (const unit of units) {
    const key = `${unit.relationship.sourceModelId}\u0000${unit.relationship.kind}`;
    const bucket = unitsBySourceAndKind.get(key) ?? [];
    bucket.push(unit);
    unitsBySourceAndKind.set(key, bucket);
  }
  const groups: Array<{
    groupId: string;
    kind: StructuralGraphEdge["kind"];
    sourceTable: TableRenderModel;
    units: VisibleSemanticRelationshipUnit[];
  }> = [];
  for (const [key, bucket] of [...unitsBySourceAndKind].sort(([left], [right]) =>
    left.localeCompare(right)
  )) {
    if (bucket.length < VISIBLE_SEMANTIC_BUNDLE_MIN_RELATIONSHIPS) {
      continue;
    }
    const sourceCenter = renderedTableCenter(bucket[0].sourceTable);
    const angularUnits = bucket.map((unit) => {
      const targetCenter = renderedTableCenter(unit.targetTable);
      let angle = Math.atan2(
        targetCenter.y - sourceCenter.y,
        targetCenter.x - sourceCenter.x,
      );
      if (angle < 0) angle += Math.PI * 2;
      return { angle, unit };
    }).sort((left, right) =>
      left.angle - right.angle
      || String(left.unit.relationship.targetModelId).localeCompare(
        String(right.unit.relationship.targetModelId),
      )
    );
    let cutAfter = angularUnits.length - 1;
    let largestGap = -1;
    for (let index = 0; index < angularUnits.length; index += 1) {
      const nextIndex = (index + 1) % angularUnits.length;
      const nextAngle = angularUnits[nextIndex].angle
        + (nextIndex === 0 ? Math.PI * 2 : 0);
      const gap = nextAngle - angularUnits[index].angle;
      if (gap > largestGap + 1e-9) {
        largestGap = gap;
        cutAfter = index;
      }
    }
    const ordered = [
      ...angularUnits.slice(cutAfter + 1),
      ...angularUnits.slice(0, cutAfter + 1),
    ].map(({ unit }) => unit);
    let groupCount = Math.max(
      1,
      Math.round(ordered.length / VISIBLE_SEMANTIC_BUNDLE_TARGET_GROUP_SIZE),
      Math.ceil(ordered.length / VISIBLE_SEMANTIC_BUNDLE_MAX_GROUP_SIZE),
    );
    while (
      groupCount > 1
      && Math.floor(ordered.length / groupCount)
        < VISIBLE_SEMANTIC_BUNDLE_MIN_RELATIONSHIPS
    ) {
      groupCount -= 1;
    }
    let offset = 0;
    for (let groupIndex = 0; groupIndex < groupCount; groupIndex += 1) {
      const size = Math.ceil(
        (ordered.length - offset) / (groupCount - groupIndex),
      );
      const groupedUnits = ordered.slice(offset, offset + size);
      offset += size;
      if (groupedUnits.length >= VISIBLE_SEMANTIC_BUNDLE_MIN_RELATIONSHIPS) {
        groups.push({
          groupId: `semantic-outgoing:${encodeURIComponent(key)}:${groupIndex}`,
          kind: groupedUnits[0].relationship.kind,
          sourceTable: groupedUnits[0].sourceTable,
          units: groupedUnits,
        });
      }
    }
  }

  let edges = initialEdges;
  let bundleGroups = 0;
  let bundledRelationships = 0;
  let crossingsAvoided = 0;
  let obstacleIntersectionsAvoided = 0;
  for (const group of groups.sort((left, right) =>
    right.units.length - left.units.length || left.groupId.localeCompare(right.groupId)
  )) {
    const directEdgeIds = new Set(group.units.map((unit) => unit.directEdge.edgeId));
    if (!group.units.every((unit) => edges.some((edge) => edge.edgeId === unit.directEdge.edgeId))) {
      continue;
    }
    const outsideEdges = edges.filter((edge) => !directEdgeIds.has(edge.edgeId));
    const outsideSegments = createVisibleSemanticSegments(outsideEdges);
    const baselineEdges = group.units.map((unit) => unit.directEdge);
    const baselineCost = measureVisibleSemanticReplacementCost(
      baselineEdges,
      outsideEdges,
      obstacleIndex,
      outsideSegments,
    );
    let best:
      | { cost: VisibleSemanticReplacementCost; edges: EdgeRenderModel[] }
      | undefined;
    for (const junction of createVisibleSemanticOutgoingJunctionCandidates(group)) {
      const candidateEdges = createVisibleSemanticOutgoingEdges(group, junction);
      if (candidateEdges.length !== group.units.length + 1) {
        continue;
      }
      const candidateCost = measureVisibleSemanticReplacementCost(
        candidateEdges,
        outsideEdges,
        obstacleIndex,
        outsideSegments,
      );
      const baselineConflicts = baselineCost.crossings + baselineCost.obstacleIntersections;
      const candidateConflicts = candidateCost.crossings + candidateCost.obstacleIntersections;
      if (
        candidateCost.obstacleIntersections > baselineCost.obstacleIntersections
        || candidateConflicts > baselineConflicts
        || (candidateConflicts === baselineConflicts
          && candidateCost.length >= baselineCost.length * 0.999)
      ) {
        continue;
      }
      if (!best || compareVisibleSemanticReplacementCost(candidateCost, best.cost) < 0) {
        best = { cost: candidateCost, edges: candidateEdges };
      }
    }
    if (!best) {
      continue;
    }
    edges = [...outsideEdges, ...best.edges];
    bundleGroups += 1;
    bundledRelationships += group.units.reduce(
      (sum, unit) => sum + unit.memberEdgeIds.length,
      0,
    );
    crossingsAvoided += baselineCost.crossings - best.cost.crossings;
    obstacleIntersectionsAvoided +=
      baselineCost.obstacleIntersections - best.cost.obstacleIntersections;
  }
  return {
    bundleGroups,
    bundledRelationships,
    crossingsAvoided,
    edges,
    eligibleRelationshipIds: [...new Set(
      groups.flatMap((group) => group.units.flatMap((unit) => unit.memberEdgeIds)),
    )],
    obstacleIntersectionsAvoided,
  };
}

function createVisibleSemanticOutgoingJunctionCandidates(
  group: { sourceTable: TableRenderModel; units: VisibleSemanticRelationshipUnit[] },
): Point[] {
  const anchorCenter = renderedTableCenter(group.sourceTable);
  const peerCenters = group.units.map((unit) => renderedTableCenter(unit.targetTable));
  const centroid = {
    x: peerCenters.reduce((sum, point) => sum + point.x, 0) / peerCenters.length,
    y: peerCenters.reduce((sum, point) => sum + point.y, 0) / peerCenters.length,
  };
  let dx = centroid.x - anchorCenter.x;
  let dy = centroid.y - anchorCenter.y;
  let distance = Math.hypot(dx, dy);
  if (distance < 1) {
    dx = peerCenters[0].x - anchorCenter.x;
    dy = peerCenters[0].y - anchorCenter.y;
    distance = Math.max(1, Math.hypot(dx, dy));
  }
  const unitX = dx / distance;
  const unitY = dy / distance;
  const perpendicularX = -unitY;
  const perpendicularY = unitX;
  const candidates: Point[] = [];
  const seen = new Set<string>();
  const add = (point: Point): void => {
    const rounded = { x: round2(point.x), y: round2(point.y) };
    const key = `${rounded.x},${rounded.y}`;
    if (!seen.has(key)) {
      seen.add(key);
      candidates.push(rounded);
    }
  };
  for (const unit of group.units) {
    const sourcePoint = parseRenderedEdgePoints(unit.directEdge.points)[0];
    if (!sourcePoint) continue;
    let portDx = sourcePoint.x - anchorCenter.x;
    let portDy = sourcePoint.y - anchorCenter.y;
    let portLength = Math.hypot(portDx, portDy);
    if (portLength < 1) {
      portDx = dx;
      portDy = dy;
      portLength = distance;
    }
    const portDirection = { x: portDx / portLength, y: portDy / portLength };
    const clearance = visibleSemanticPaddedExitDistance(
      sourcePoint,
      portDirection,
      group.sourceTable,
    );
    for (const extra of [2, 48, 160, 416]) {
      add({
        x: sourcePoint.x + portDirection.x * (clearance + extra),
        y: sourcePoint.y + portDirection.y * (clearance + extra),
      });
    }
  }
  for (const fraction of [0.18, 0.3, 0.42, 0.56, 0.7, 0.84]) {
    for (const offsetFactor of [0, -0.35, 0.35, -0.7, 0.7]) {
      const offset = Math.min(1_200, Math.max(160, distance * 0.18)) * offsetFactor;
      add({
        x: anchorCenter.x + dx * fraction + perpendicularX * offset,
        y: anchorCenter.y + dy * fraction + perpendicularY * offset,
      });
    }
  }
  return candidates;
}

function createVisibleSemanticOutgoingEdges(
  group: {
    groupId: string;
    kind: StructuralGraphEdge["kind"];
    sourceTable: TableRenderModel;
    units: VisibleSemanticRelationshipUnit[];
  },
  junction: Point,
): EdgeRenderModel[] {
  const [markerStartId, markerEndId] = markerIds(group.kind);
  const carrierFamily = group.kind === "inheritance" ? "inheritance" : "association";
  const cssKind = group.kind.replaceAll("_", "-");
  const memberEdgeIds = [...new Set(
    group.units.flatMap((unit) => unit.memberEdgeIds),
  )].sort();
  const logicalEndpointModelIds = [...new Set<ModelId>(
    group.units.flatMap((unit) => [
      unit.relationship.sourceModelId,
      unit.relationship.targetModelId,
    ]),
  )].sort();
  const representative = group.units[0].relationship;
  const sourcePort = computeRenderedBoundaryPort(group.sourceTable, junction);
  if (sameRenderedPoint(sourcePort, junction)) {
    return [];
  }
  const trunk: EdgeRenderModel = {
    carrierFamily,
    carrierRole: "semantic-trunk",
    crossingIds: [],
    cssKind,
    edgeId: `${group.groupId}:trunk`,
    logicalEndpointModelIds,
    markerEndId: "",
    markerStartId,
    memberEdgeIds,
    physicalEndpointModelIds: [representative.sourceModelId],
    points: serializeRenderedEdgePoints([sourcePort, junction]),
    preserveRouteEndpoints: true,
    provenance: "semantic_bundle",
    sourceModelId: representative.sourceModelId,
    targetModelId: representative.targetModelId,
  };
  const branches = group.units.flatMap((unit, index) => {
    const targetPort = computeRenderedBoundaryPort(unit.targetTable, junction);
    if (sameRenderedPoint(junction, targetPort)) {
      return [];
    }
    return [{
      carrierFamily,
      carrierRole: "semantic-branch",
      crossingIds: [],
      cssKind,
      edgeId: `${group.groupId}:branch:${index}`,
      logicalEndpointModelIds: [
        unit.relationship.sourceModelId,
        unit.relationship.targetModelId,
      ],
      markerEndId,
      markerStartId: "",
      memberEdgeIds: unit.memberEdgeIds,
      physicalEndpointModelIds: [unit.relationship.targetModelId],
      points: serializeRenderedEdgePoints([junction, targetPort]),
      preserveRouteEndpoints: true,
      provenance: unit.relationship.provenance,
      sourceModelId: unit.relationship.sourceModelId,
      targetModelId: unit.relationship.targetModelId,
    } satisfies EdgeRenderModel];
  });
  return [trunk, ...branches];
}

function createVisibleSemanticTrunkHierarchy(
  initialEdges: EdgeRenderModel[],
  obstacleIndex: RenderedObstacleIndex,
  tableByModelId: ReadonlyMap<ModelId, TableRenderModel>,
): VisibleSemanticHierarchyResult {
  let edges = initialEdges;
  let bundleGroups = 0;
  let crossingsAvoided = 0;
  let obstacleIntersectionsAvoided = 0;
  for (let level = 0; level < 12; level += 1) {
    const trunksByTargetAndKind = new Map<string, EdgeRenderModel[]>();
    for (const edge of edges) {
      if (
        edge.carrierRole !== "semantic-trunk"
        || edge.markerStartId !== ""
        || edge.markerEndId === ""
        || edge.physicalEndpointModelIds?.[0] !== edge.targetModelId
      ) {
        continue;
      }
      const key = [
        edge.targetModelId,
        edge.cssKind,
        edge.carrierFamily ?? "association",
        edge.markerEndId,
      ].join("\u0000");
      const trunks = trunksByTargetAndKind.get(key) ?? [];
      trunks.push(edge);
      trunksByTargetAndKind.set(key, trunks);
    }
    const candidateGroups: Array<{
      groupId: string;
      targetTable: TableRenderModel;
      trunks: EdgeRenderModel[];
    }> = [];
    for (const [key, trunks] of [...trunksByTargetAndKind].sort(([left], [right]) =>
      left.localeCompare(right)
    )) {
      if (trunks.length < VISIBLE_SEMANTIC_BUNDLE_MIN_RELATIONSHIPS) {
        continue;
      }
      const targetTable = tableByModelId.get(trunks[0].targetModelId);
      if (!targetTable) {
        continue;
      }
      const targetCenter = renderedTableCenter(targetTable);
      const ordered = trunks.map((trunk) => {
        const start = parseRenderedEdgePoints(trunk.points)[0];
        const angle = start
          ? Math.atan2(start.y - targetCenter.y, start.x - targetCenter.x)
          : 0;
        return { angle: angle < 0 ? angle + Math.PI * 2 : angle, trunk };
      }).sort((left, right) =>
        left.angle - right.angle || left.trunk.edgeId.localeCompare(right.trunk.edgeId)
      );
      let cutAfter = ordered.length - 1;
      let largestGap = -1;
      for (let index = 0; index < ordered.length; index += 1) {
        const nextIndex = (index + 1) % ordered.length;
        const nextAngle = ordered[nextIndex].angle
          + (nextIndex === 0 ? Math.PI * 2 : 0);
        const gap = nextAngle - ordered[index].angle;
        if (gap > largestGap + 1e-9) {
          largestGap = gap;
          cutAfter = index;
        }
      }
      const circularOrder = [
        ...ordered.slice(cutAfter + 1),
        ...ordered.slice(0, cutAfter + 1),
      ].map(({ trunk }) => trunk);
      let groupCount = Math.max(
        1,
        Math.round(circularOrder.length / VISIBLE_SEMANTIC_BUNDLE_TARGET_GROUP_SIZE),
        Math.ceil(circularOrder.length / VISIBLE_SEMANTIC_BUNDLE_MAX_GROUP_SIZE),
      );
      while (
        groupCount > 1
        && Math.floor(circularOrder.length / groupCount)
          < VISIBLE_SEMANTIC_BUNDLE_MIN_RELATIONSHIPS
      ) {
        groupCount -= 1;
      }
      let offset = 0;
      for (let groupIndex = 0; groupIndex < groupCount; groupIndex += 1) {
        const size = Math.ceil(
          (circularOrder.length - offset) / (groupCount - groupIndex),
        );
        const groupedTrunks = circularOrder.slice(offset, offset + size);
        offset += size;
        if (groupedTrunks.length < VISIBLE_SEMANTIC_BUNDLE_MIN_RELATIONSHIPS) {
          continue;
        }
        candidateGroups.push({
          groupId: `semantic-hierarchy:${level}:${encodeURIComponent(key)}:${groupIndex}`,
          targetTable,
          trunks: groupedTrunks,
        });
      }
    }
    if (candidateGroups.length === 0) {
      break;
    }
    let acceptedThisLevel = 0;
    for (const group of candidateGroups.sort((left, right) =>
      right.trunks.length - left.trunks.length
      || left.groupId.localeCompare(right.groupId)
    )) {
      const replacedEdgeIds = new Set(group.trunks.map((trunk) => trunk.edgeId));
      if (!group.trunks.every((trunk) => edges.some((edge) => edge.edgeId === trunk.edgeId))) {
        continue;
      }
      const outsideEdges = edges.filter((edge) => !replacedEdgeIds.has(edge.edgeId));
      const outsideSegments = createVisibleSemanticSegments(outsideEdges);
      const baselineCost = measureVisibleSemanticReplacementCost(
        group.trunks,
        outsideEdges,
        obstacleIndex,
        outsideSegments,
      );
      let best:
        | { cost: VisibleSemanticReplacementCost; edges: EdgeRenderModel[] }
        | undefined;
      for (const junction of createVisibleSemanticHierarchyJunctionCandidates(group)) {
        const candidateEdges = createVisibleSemanticHierarchyEdges(group, junction);
        if (candidateEdges.length !== group.trunks.length + 1) {
          continue;
        }
        const candidateCost = measureVisibleSemanticReplacementCost(
          candidateEdges,
          outsideEdges,
          obstacleIndex,
          outsideSegments,
        );
        const baselineConflicts = baselineCost.crossings
          + baselineCost.obstacleIntersections;
        const candidateConflicts = candidateCost.crossings
          + candidateCost.obstacleIntersections;
        if (
          candidateCost.obstacleIntersections > baselineCost.obstacleIntersections
          || candidateConflicts > baselineConflicts
          || (candidateConflicts === baselineConflicts
            && candidateCost.length >= baselineCost.length * 0.999)
        ) {
          continue;
        }
        if (!best || compareVisibleSemanticReplacementCost(candidateCost, best.cost) < 0) {
          best = { cost: candidateCost, edges: candidateEdges };
        }
      }
      if (!best) {
        continue;
      }
      edges = [...outsideEdges, ...best.edges];
      bundleGroups += 1;
      acceptedThisLevel += 1;
      crossingsAvoided += baselineCost.crossings - best.cost.crossings;
      obstacleIntersectionsAvoided +=
        baselineCost.obstacleIntersections - best.cost.obstacleIntersections;
    }
    if (acceptedThisLevel === 0) {
      break;
    }
  }
  return { bundleGroups, crossingsAvoided, edges, obstacleIntersectionsAvoided };
}

function createVisibleSemanticHierarchyJunctionCandidates(
  group: { targetTable: TableRenderModel; trunks: EdgeRenderModel[] },
): Point[] {
  const starts = group.trunks.flatMap((trunk) => {
    const start = parseRenderedEdgePoints(trunk.points)[0];
    return start ? [start] : [];
  });
  if (starts.length !== group.trunks.length) {
    return [];
  }
  const targetCenter = renderedTableCenter(group.targetTable);
  const centroid = {
    x: starts.reduce((sum, point) => sum + point.x, 0) / starts.length,
    y: starts.reduce((sum, point) => sum + point.y, 0) / starts.length,
  };
  let dx = centroid.x - targetCenter.x;
  let dy = centroid.y - targetCenter.y;
  let distance = Math.hypot(dx, dy);
  if (distance < 1) {
    dx = starts[0].x - targetCenter.x;
    dy = starts[0].y - targetCenter.y;
    distance = Math.max(1, Math.hypot(dx, dy));
  }
  const perpendicularX = -dy / distance;
  const perpendicularY = dx / distance;
  const candidates: Point[] = [];
  const seen = new Set<string>();
  const add = (point: Point): void => {
    const rounded = { x: round2(point.x), y: round2(point.y) };
    const key = `${rounded.x},${rounded.y}`;
    if (!seen.has(key)) {
      seen.add(key);
      candidates.push(rounded);
    }
  };
  for (const fraction of [0.12, 0.22, 0.36, 0.52, 0.7, 0.86]) {
    for (const offsetFactor of [0, -0.12, 0.12, -0.28, 0.28]) {
      const offset = Math.min(700, distance * 0.12) * offsetFactor;
      add({
        x: targetCenter.x + dx * fraction + perpendicularX * offset,
        y: targetCenter.y + dy * fraction + perpendicularY * offset,
      });
    }
  }
  for (const start of starts) {
    for (const fraction of [0.2, 0.4, 0.65]) {
      add({
        x: targetCenter.x + (start.x - targetCenter.x) * fraction,
        y: targetCenter.y + (start.y - targetCenter.y) * fraction,
      });
    }
  }
  return candidates;
}

function createVisibleSemanticHierarchyEdges(
  group: {
    groupId: string;
    targetTable: TableRenderModel;
    trunks: EdgeRenderModel[];
  },
  junction: Point,
): EdgeRenderModel[] {
  const branches = group.trunks.flatMap((trunk, index) => {
    const start = parseRenderedEdgePoints(trunk.points)[0];
    if (!start || sameRenderedPoint(start, junction)) {
      return [];
    }
    return [{
      ...trunk,
      carrierRole: "semantic-branch",
      edgeId: `${group.groupId}:branch:${index}`,
      markerEndId: "",
      markerStartId: "",
      physicalEndpointModelIds: [],
      points: serializeRenderedEdgePoints([start, junction]),
    } satisfies EdgeRenderModel];
  });
  const representative = group.trunks[0];
  const targetPort = computeRenderedBoundaryPort(group.targetTable, junction);
  if (sameRenderedPoint(junction, targetPort)) {
    return [];
  }
  const memberEdgeIds = [...new Set(
    group.trunks.flatMap((trunk) => trunk.memberEdgeIds ?? [trunk.edgeId]),
  )].sort();
  const logicalEndpointModelIds = [...new Set<ModelId>(
    group.trunks.flatMap((trunk) =>
      trunk.logicalEndpointModelIds ?? [trunk.sourceModelId, trunk.targetModelId]
    ),
  )].sort();
  const trunk: EdgeRenderModel = {
    ...representative,
    carrierRole: "semantic-trunk",
    edgeId: `${group.groupId}:trunk`,
    logicalEndpointModelIds,
    markerStartId: "",
    memberEdgeIds,
    physicalEndpointModelIds: [group.targetTable.modelId],
    points: serializeRenderedEdgePoints([junction, targetPort]),
    targetModelId: group.targetTable.modelId,
  };
  return [...branches, trunk];
}

// Retained temporarily as deterministic rollback/reference implementations for
// older serialized carrier fixtures. Production renders direct relationships;
// leafBundles remain node-layout and selection metadata only.
void createVisibleSemanticKindCarrierNetwork;
void createVisibleSemanticBundleGroups;
void createVisibleSemanticJunctionCandidates;
void createVisibleSemanticBundleEdges;
void createVisibleSemanticOutgoingBundles;
void createVisibleSemanticTrunkHierarchy;
void createHubCarrierRenderGroups;

function createDirectRelationshipEdge(
  relationship: StructuralGraphEdge,
  sourceTable: TableRenderModel,
  targetTable: TableRenderModel,
  selectedPoints?: Point[],
): EdgeRenderModel {
  const [markerStartId, markerEndId] = markerIds(relationship.kind);
  return {
    carrierFamily: relationship.kind === "inheritance" ? "inheritance" : "association",
    carrierRole: "direct",
    crossingIds: [],
    cssKind: relationship.kind.replaceAll("_", "-"),
    edgeId: relationship.id,
    logicalEndpointModelIds: [relationship.sourceModelId, relationship.targetModelId],
    markerEndId,
    markerStartId,
    memberEdgeIds: [relationship.id],
    physicalEndpointModelIds: [relationship.sourceModelId, relationship.targetModelId],
    points: serializeRenderedEdgePoints(
      selectedPoints ?? buildStraightRenderedEdgePoints(sourceTable, targetTable),
    ),
    preserveRouteEndpoints: true,
    provenance: relationship.provenance,
    sourceModelId: relationship.sourceModelId,
    targetModelId: relationship.targetModelId,
  };
}

function renderedPointTouchesTable(
  point: Point,
  table: TableRenderModel,
  tolerance = 3,
): boolean {
  return point.x >= table.position.x - tolerance
    && point.x <= table.position.x + table.size.width + tolerance
    && point.y >= table.position.y - tolerance
    && point.y <= table.position.y + table.size.height + tolerance;
}

function reconnectStraightEdgeToRenderedTables(
  edge: EdgeRenderModel,
  tableByModelId: ReadonlyMap<ModelId, TableRenderModel>,
): EdgeRenderModel {
  const sourceTable = tableByModelId.get(edge.sourceModelId);
  const targetTable = tableByModelId.get(edge.targetModelId);
  if (!sourceTable || !targetTable || sourceTable.modelId === targetTable.modelId) {
    return edge;
  }
  const points = parseRenderedEdgePoints(edge.points);
  if (
    points.length >= 2
    && renderedPointTouchesTable(points[0], sourceTable)
    && renderedPointTouchesTable(points[points.length - 1], targetTable)
  ) {
    return edge;
  }
  return {
    ...edge,
    crossingIds: [],
    points: serializeRenderedEdgePoints(
      buildStraightRenderedEdgePoints(sourceTable, targetTable),
    ),
    preserveRouteEndpoints: true,
  };
}

function serializeRenderedEdgePoints(points: Point[]): string {
  return points.map((point) => `${round2(point.x)},${round2(point.y)}`).join(" ");
}

interface SemanticCarrierSourceEdge {
  family: Exclude<SemanticCarrierFamily, "mixed">;
  memberEdgeIds: string[];
  renderEdge: EdgeRenderModel;
  sourceModelId: ModelId;
  targetModelId: ModelId;
}

interface SemanticCarrierSegment {
  family: SemanticCarrierFamily;
  memberEdgeIds: Set<string>;
  role: "direct" | "semantic-tree";
  sourceModelId: ModelId;
  targetModelId: ModelId;
  templateEdge?: EdgeRenderModel;
}

interface SemanticCarrierTreeLink {
  sourceModelId: ModelId;
  targetModelId: ModelId;
}

interface SemanticCarrierForest {
  passthroughEdges: EdgeRenderModel[];
  segments: SemanticCarrierSegment[];
}

interface SemanticCarrierResult {
  diagnostics: SemanticCarrierDiagnostics;
  edges: EdgeRenderModel[];
}

interface RenderedObstacleIndex {
  byCell: Map<string, TableRenderModel[]>;
  visibleTables: TableRenderModel[];
}

/**
 * Builds a confluent carrier drawing for very large diagrams.
 *
 * Every original rendered relationship remains a member of a continuous path
 * between its two rendered endpoints. Association and inheritance components
 * receive independent physical trees, so repeated strokes are drawn once
 * without pretending that one disconnected average line reaches every table.
 * If a semantic-family tree has an unavoidable table penetration, only the
 * affected relationships are rerouted over a collision-aware component tree.
 */
/** @deprecated Research-only confluent tree; production uses the relationship-faithful carrier path. */
export function createSemanticCarrierEdges(
  sourceEdges: EdgeRenderModel[],
  tables: TableRenderModel[],
  structuralEdgeById: Map<string, StructuralGraphEdge>,
  renderedEndpointByModelId: Map<ModelId, ModelId>,
): SemanticCarrierResult {
  const visibleTableByModelId = new Map(
    tables
      .filter((table) => !table.hidden)
      .map((table) => [table.modelId, table] as const),
  );
  const sourceCarrierEdges: SemanticCarrierSourceEdge[] = [];
  const passthroughEdges: EdgeRenderModel[] = [];
  const endpointPairByMemberEdgeId = new Map<string, [ModelId, ModelId]>();

  for (const renderEdge of sourceEdges) {
    const sourceModelId = renderedEndpointByModelId.get(renderEdge.sourceModelId)
      ?? renderEdge.sourceModelId;
    const targetModelId = renderedEndpointByModelId.get(renderEdge.targetModelId)
      ?? renderEdge.targetModelId;
    const sourceTable = visibleTableByModelId.get(sourceModelId);
    const targetTable = visibleTableByModelId.get(targetModelId);
    const memberEdgeIds = [...new Set(
      renderEdge.memberEdgeIds && renderEdge.memberEdgeIds.length > 0
        ? renderEdge.memberEdgeIds
        : [renderEdge.edgeId],
    )].sort();
    for (const memberEdgeId of memberEdgeIds) {
      if (!endpointPairByMemberEdgeId.has(memberEdgeId)) {
        endpointPairByMemberEdgeId.set(
          memberEdgeId,
          sourceModelId === targetModelId
            ? [renderEdge.sourceModelId, renderEdge.targetModelId]
            : [sourceModelId, targetModelId],
        );
      }
    }
    if (
      !sourceTable
      || !targetTable
      || sourceModelId === targetModelId
    ) {
      passthroughEdges.push({ ...renderEdge, memberEdgeIds });
      continue;
    }

    const family = memberEdgeIds.every(
      (edgeId) => structuralEdgeById.get(edgeId)?.kind === "inheritance",
    )
      ? "inheritance"
      : "association";
    sourceCarrierEdges.push({
      family,
      memberEdgeIds,
      renderEdge,
      sourceModelId,
      targetModelId,
    });
  }

  const obstacleIndex = createRenderedObstacleIndex(
    [...visibleTableByModelId.values()],
  );
  const primary = buildSemanticCarrierForest(
    sourceCarrierEdges,
    visibleTableByModelId,
    obstacleIndex,
    true,
  );
  primary.passthroughEdges.push(...passthroughEdges);

  const fallbackMemberEdgeIds = new Set<string>();
  for (const segment of primary.segments) {
    if (
      countSemanticCarrierSegmentObstacles(
        segment,
        visibleTableByModelId,
        obstacleIndex,
      ) === 0
    ) {
      continue;
    }
    for (const memberEdgeId of segment.memberEdgeIds) {
      fallbackMemberEdgeIds.add(memberEdgeId);
    }
  }

  let carrierSegments = primary.segments;
  if (fallbackMemberEdgeIds.size > 0) {
    const globalFallback = buildSemanticCarrierForest(
      sourceCarrierEdges,
      visibleTableByModelId,
      obstacleIndex,
      false,
    );
    const primaryWithoutFallback = primary.segments.flatMap((segment) => {
      const memberEdgeIds = new Set(
        [...segment.memberEdgeIds].filter(
          (edgeId) => !fallbackMemberEdgeIds.has(edgeId),
        ),
      );
      return memberEdgeIds.size > 0 ? [{ ...segment, memberEdgeIds }] : [];
    });
    const fallbackSegments = globalFallback.segments.flatMap((segment) => {
      const memberEdgeIds = new Set(
        [...segment.memberEdgeIds].filter(
          (edgeId) => fallbackMemberEdgeIds.has(edgeId),
        ),
      );
      return memberEdgeIds.size > 0 ? [{ ...segment, memberEdgeIds }] : [];
    });
    carrierSegments = mergeCoincidentSemanticCarrierSegments([
      ...primaryWithoutFallback,
      ...fallbackSegments,
    ]);
  } else {
    carrierSegments = mergeCoincidentSemanticCarrierSegments(carrierSegments);
  }

  const renderedCarrierEdges = carrierSegments
    .sort(compareSemanticCarrierSegments)
    .map((segment, index) =>
      createSemanticCarrierRenderEdge(
        segment,
        index,
        visibleTableByModelId,
        structuralEdgeById,
      ))
    .filter((edge): edge is EdgeRenderModel => edge !== undefined);
  const finalSegmentsByMemberEdgeId = new Map<string, SemanticCarrierSegment[]>();
  for (const segment of carrierSegments) {
    for (const memberEdgeId of segment.memberEdgeIds) {
      const members = finalSegmentsByMemberEdgeId.get(memberEdgeId) ?? [];
      members.push(segment);
      finalSegmentsByMemberEdgeId.set(memberEdgeId, members);
    }
  }
  for (const passthroughEdge of primary.passthroughEdges) {
    const memberEdgeIds = passthroughEdge.memberEdgeIds ?? [passthroughEdge.edgeId];
    const segment: SemanticCarrierSegment = {
      family: passthroughEdge.carrierFamily ?? "mixed",
      memberEdgeIds: new Set(memberEdgeIds),
      role: "direct",
      sourceModelId: passthroughEdge.sourceModelId,
      targetModelId: passthroughEdge.targetModelId,
      templateEdge: passthroughEdge,
    };
    for (const memberEdgeId of memberEdgeIds) {
      const members = finalSegmentsByMemberEdgeId.get(memberEdgeId) ?? [];
      members.push(segment);
      finalSegmentsByMemberEdgeId.set(memberEdgeId, members);
    }
  }
  const missingRelationships: string[] = [];
  const disconnectedRelationships: string[] = [];
  let bundledRelationships = 0;
  for (const [memberEdgeId, endpointPair] of endpointPairByMemberEdgeId) {
    const segments = finalSegmentsByMemberEdgeId.get(memberEdgeId) ?? [];
    if (segments.length === 0) {
      missingRelationships.push(memberEdgeId);
      continue;
    }
    if (
      segments.length > 1
      || segments.some((segment) => segment.memberEdgeIds.size > 1)
    ) {
      bundledRelationships += 1;
    }
    if (!semanticCarrierSegmentsConnectEndpoints(segments, endpointPair)) {
      disconnectedRelationships.push(memberEdgeId);
    }
  }
  let obstacleIntersections = 0;
  for (const segment of carrierSegments) {
    obstacleIntersections += countSemanticCarrierSegmentObstacles(
      segment,
      visibleTableByModelId,
      obstacleIndex,
    );
  }

  return {
    diagnostics: {
      active: true,
      bundledRelationships,
      carrierSegments: renderedCarrierEdges.length + primary.passthroughEdges.length,
      disconnectedRelationships: disconnectedRelationships.sort(),
      fallbackRelationships: fallbackMemberEdgeIds.size,
      missingRelationships: missingRelationships.sort(),
      obstacleIntersections,
      relationships: endpointPairByMemberEdgeId.size,
    },
    edges: [...renderedCarrierEdges, ...primary.passthroughEdges],
  };
}

function buildSemanticCarrierForest(
  sourceEdges: SemanticCarrierSourceEdge[],
  tableByModelId: Map<ModelId, TableRenderModel>,
  obstacleIndex: RenderedObstacleIndex,
  separateSemanticFamilies: boolean,
): SemanticCarrierForest {
  const sourcesByFamily = new Map<SemanticCarrierFamily, SemanticCarrierSourceEdge[]>();
  for (const sourceEdge of sourceEdges) {
    const family = separateSemanticFamilies ? sourceEdge.family : "mixed";
    const members = sourcesByFamily.get(family) ?? [];
    members.push(sourceEdge);
    sourcesByFamily.set(family, members);
  }

  const segments: SemanticCarrierSegment[] = [];
  for (const [family, familyEdges] of sourcesByFamily) {
    for (const component of collectSemanticCarrierComponents(familyEdges)) {
      const modelIds = [...new Set(component.flatMap((edge) => [
        edge.sourceModelId,
        edge.targetModelId,
      ]))].sort();
      if (component.length < 2 || modelIds.length < 3) {
        for (const sourceEdge of component) {
          segments.push({
            family,
            memberEdgeIds: new Set(sourceEdge.memberEdgeIds),
            role: "direct",
            sourceModelId: sourceEdge.sourceModelId,
            targetModelId: sourceEdge.targetModelId,
            templateEdge: sourceEdge.renderEdge,
          });
        }
        continue;
      }

      const links = buildCollisionAwareSemanticSpanningTree(
        modelIds,
        component,
        tableByModelId,
        obstacleIndex,
      );
      const treeAdjacency = new Map<
        ModelId,
        Array<{ linkIndex: number; modelId: ModelId }>
      >(modelIds.map((modelId) => [modelId, []]));
      const memberEdgeIdsByLink = links.map(() => new Set<string>());
      links.forEach((link, linkIndex) => {
        treeAdjacency.get(link.sourceModelId)?.push({
          linkIndex,
          modelId: link.targetModelId,
        });
        treeAdjacency.get(link.targetModelId)?.push({
          linkIndex,
          modelId: link.sourceModelId,
        });
      });

      for (const sourceEdge of component) {
        const pathLinkIndices = findSemanticCarrierTreePath(
          sourceEdge.sourceModelId,
          sourceEdge.targetModelId,
          treeAdjacency,
        );
        for (const linkIndex of pathLinkIndices) {
          for (const memberEdgeId of sourceEdge.memberEdgeIds) {
            memberEdgeIdsByLink[linkIndex].add(memberEdgeId);
          }
        }
      }
      links.forEach((link, linkIndex) => {
        if (memberEdgeIdsByLink[linkIndex].size === 0) {
          return;
        }
        segments.push({
          family,
          memberEdgeIds: memberEdgeIdsByLink[linkIndex],
          role: "semantic-tree",
          sourceModelId: link.sourceModelId,
          targetModelId: link.targetModelId,
        });
      });
    }
  }

  return { passthroughEdges: [], segments };
}

function collectSemanticCarrierComponents(
  sourceEdges: SemanticCarrierSourceEdge[],
): SemanticCarrierSourceEdge[][] {
  const edgeIndicesByModelId = new Map<ModelId, number[]>();
  sourceEdges.forEach((edge, edgeIndex) => {
    const sourceIndices = edgeIndicesByModelId.get(edge.sourceModelId) ?? [];
    sourceIndices.push(edgeIndex);
    edgeIndicesByModelId.set(edge.sourceModelId, sourceIndices);
    const targetIndices = edgeIndicesByModelId.get(edge.targetModelId) ?? [];
    targetIndices.push(edgeIndex);
    edgeIndicesByModelId.set(edge.targetModelId, targetIndices);
  });

  const unseenEdgeIndices = new Set(sourceEdges.map((_, index) => index));
  const components: SemanticCarrierSourceEdge[][] = [];
  while (unseenEdgeIndices.size > 0) {
    const seedIndex = unseenEdgeIndices.values().next().value as number;
    const queuedModelIds: ModelId[] = [sourceEdges[seedIndex].sourceModelId];
    const seenModelIds = new Set<ModelId>();
    const componentEdgeIndices = new Set<number>();
    for (let queueIndex = 0; queueIndex < queuedModelIds.length; queueIndex += 1) {
      const modelId = queuedModelIds[queueIndex];
      if (seenModelIds.has(modelId)) {
        continue;
      }
      seenModelIds.add(modelId);
      for (const edgeIndex of edgeIndicesByModelId.get(modelId) ?? []) {
        if (componentEdgeIndices.has(edgeIndex)) {
          continue;
        }
        componentEdgeIndices.add(edgeIndex);
        unseenEdgeIndices.delete(edgeIndex);
        const edge = sourceEdges[edgeIndex];
        queuedModelIds.push(edge.sourceModelId, edge.targetModelId);
      }
    }
    components.push(
      [...componentEdgeIndices]
        .sort((left, right) => left - right)
        .map((edgeIndex) => sourceEdges[edgeIndex]),
    );
  }
  return components;
}

function buildCollisionAwareSemanticSpanningTree(
  modelIds: ModelId[],
  componentEdges: SemanticCarrierSourceEdge[],
  tableByModelId: Map<ModelId, TableRenderModel>,
  obstacleIndex: RenderedObstacleIndex,
): SemanticCarrierTreeLink[] {
  interface Candidate extends SemanticCarrierTreeLink {
    distanceSquared: number;
    obstacleIntersections: number;
  }

  const candidateByPair = new Map<string, Candidate>();
  const addCandidate = (leftModelId: ModelId, rightModelId: ModelId): void => {
    if (leftModelId === rightModelId) {
      return;
    }
    const [sourceModelId, targetModelId] = String(leftModelId) < String(rightModelId)
      ? [leftModelId, rightModelId]
      : [rightModelId, leftModelId];
    const key = `${sourceModelId}\u0000${targetModelId}`;
    if (candidateByPair.has(key)) {
      return;
    }
    const sourceTable = tableByModelId.get(sourceModelId);
    const targetTable = tableByModelId.get(targetModelId);
    if (!sourceTable || !targetTable) {
      return;
    }
    const sourceCenter = renderedTableCenter(sourceTable);
    const targetCenter = renderedTableCenter(targetTable);
    const dx = sourceCenter.x - targetCenter.x;
    const dy = sourceCenter.y - targetCenter.y;
    candidateByPair.set(key, {
      distanceSquared: dx * dx + dy * dy,
      obstacleIntersections: countStraightRenderedTableIntersections(
        sourceTable,
        targetTable,
        obstacleIndex,
      ),
      sourceModelId,
      targetModelId,
    });
  };

  const neighborLimit = Math.min(
    Math.max(0, modelIds.length - 1),
    SEMANTIC_CARRIER_NEIGHBOR_LIMIT,
  );
  for (const sourceModelId of modelIds) {
    const sourceTable = tableByModelId.get(sourceModelId);
    if (!sourceTable) {
      continue;
    }
    const sourceCenter = renderedTableCenter(sourceTable);
    const nearest = modelIds.flatMap((targetModelId) => {
      if (targetModelId === sourceModelId) {
        return [];
      }
      const targetTable = tableByModelId.get(targetModelId);
      if (!targetTable) {
        return [];
      }
      const targetCenter = renderedTableCenter(targetTable);
      const dx = sourceCenter.x - targetCenter.x;
      const dy = sourceCenter.y - targetCenter.y;
      return [{
        distanceSquared: dx * dx + dy * dy,
        targetModelId,
      }];
    });
    nearest.sort((left, right) =>
      left.distanceSquared - right.distanceSquared
      || String(left.targetModelId).localeCompare(String(right.targetModelId))
    );
    for (const neighbor of nearest.slice(0, neighborLimit)) {
      addCandidate(sourceModelId, neighbor.targetModelId);
    }
  }
  // The original semantic edges guarantee a connected candidate graph even
  // when a component contains two distant geometric islands.
  for (const edge of componentEdges) {
    addCandidate(edge.sourceModelId, edge.targetModelId);
  }

  const parentByModelId = new Map(modelIds.map((modelId) => [modelId, modelId]));
  const findRoot = (modelId: ModelId): ModelId => {
    let root = modelId;
    while (parentByModelId.get(root) !== root) {
      root = parentByModelId.get(root) ?? root;
    }
    let cursor = modelId;
    while (parentByModelId.get(cursor) !== cursor) {
      const next = parentByModelId.get(cursor) ?? root;
      parentByModelId.set(cursor, root);
      cursor = next;
    }
    return root;
  };
  const union = (leftModelId: ModelId, rightModelId: ModelId): boolean => {
    const leftRoot = findRoot(leftModelId);
    const rightRoot = findRoot(rightModelId);
    if (leftRoot === rightRoot) {
      return false;
    }
    parentByModelId.set(leftRoot, rightRoot);
    return true;
  };

  const candidates = [...candidateByPair.values()].sort((left, right) =>
    left.obstacleIntersections - right.obstacleIntersections
    || left.distanceSquared - right.distanceSquared
    || String(left.sourceModelId).localeCompare(String(right.sourceModelId))
    || String(left.targetModelId).localeCompare(String(right.targetModelId))
  );
  const links: SemanticCarrierTreeLink[] = [];
  for (const candidate of candidates) {
    if (!union(candidate.sourceModelId, candidate.targetModelId)) {
      continue;
    }
    links.push({
      sourceModelId: candidate.sourceModelId,
      targetModelId: candidate.targetModelId,
    });
    if (links.length + 1 === modelIds.length) {
      break;
    }
  }
  return links;
}

function findSemanticCarrierTreePath(
  sourceModelId: ModelId,
  targetModelId: ModelId,
  adjacency: Map<ModelId, Array<{ linkIndex: number; modelId: ModelId }>>,
): number[] {
  const previous = new Map<
    ModelId,
    { linkIndex: number; modelId: ModelId } | undefined
  >([[sourceModelId, undefined]]);
  const queue: ModelId[] = [sourceModelId];
  for (
    let queueIndex = 0;
    queueIndex < queue.length && !previous.has(targetModelId);
    queueIndex += 1
  ) {
    const modelId = queue[queueIndex];
    for (const step of adjacency.get(modelId) ?? []) {
      if (previous.has(step.modelId)) {
        continue;
      }
      previous.set(step.modelId, { linkIndex: step.linkIndex, modelId });
      queue.push(step.modelId);
    }
  }

  const linkIndices: number[] = [];
  let cursor = targetModelId;
  while (cursor !== sourceModelId) {
    const step = previous.get(cursor);
    if (!step) {
      return [];
    }
    linkIndices.push(step.linkIndex);
    cursor = step.modelId;
  }
  return linkIndices;
}

function createRenderedObstacleIndex(
  visibleTables: TableRenderModel[],
): RenderedObstacleIndex {
  const index: RenderedObstacleIndex = {
    byCell: new Map<string, TableRenderModel[]>(),
    visibleTables: [],
  };
  for (const table of visibleTables) {
    addRenderedObstacleToIndex(index, table);
  }
  return index;
}

function addRenderedObstacleToIndex(
  index: RenderedObstacleIndex,
  table: TableRenderModel,
): void {
  index.visibleTables.push(table);
  const startColumn = Math.floor(table.position.x / SEMANTIC_CARRIER_SPATIAL_CELL);
  const endColumn = Math.floor(
    (table.position.x + table.size.width) / SEMANTIC_CARRIER_SPATIAL_CELL,
  );
  const startRow = Math.floor(table.position.y / SEMANTIC_CARRIER_SPATIAL_CELL);
  const endRow = Math.floor(
    (table.position.y + table.size.height) / SEMANTIC_CARRIER_SPATIAL_CELL,
  );
  for (let row = startRow; row <= endRow; row += 1) {
    for (let column = startColumn; column <= endColumn; column += 1) {
      const key = `${column}:${row}`;
      const members = index.byCell.get(key) ?? [];
      members.push(table);
      index.byCell.set(key, members);
    }
  }
}

function queryRenderedObstacles(
  index: RenderedObstacleIndex,
  start: Point,
  end: Point,
): TableRenderModel[] {
  const padding = SEMANTIC_CARRIER_OBSTACLE_PADDING;
  const startColumn = Math.floor(
    (Math.min(start.x, end.x) - padding) / SEMANTIC_CARRIER_SPATIAL_CELL,
  );
  const endColumn = Math.floor(
    (Math.max(start.x, end.x) + padding) / SEMANTIC_CARRIER_SPATIAL_CELL,
  );
  const startRow = Math.floor(
    (Math.min(start.y, end.y) - padding) / SEMANTIC_CARRIER_SPATIAL_CELL,
  );
  const endRow = Math.floor(
    (Math.max(start.y, end.y) + padding) / SEMANTIC_CARRIER_SPATIAL_CELL,
  );
  const tableByModelId = new Map<ModelId, TableRenderModel>();
  for (let row = startRow; row <= endRow; row += 1) {
    for (let column = startColumn; column <= endColumn; column += 1) {
      for (const table of index.byCell.get(`${column}:${row}`) ?? []) {
        tableByModelId.set(table.modelId, table);
      }
    }
  }
  return [...tableByModelId.values()];
}

function countStraightRenderedTableIntersections(
  sourceTable: TableRenderModel,
  targetTable: TableRenderModel,
  obstacleIndex: RenderedObstacleIndex,
  ignoredObstacleModelIds?: ReadonlySet<ModelId>,
): number {
  const [start, end] = buildStraightRenderedEdgePoints(sourceTable, targetTable);
  return countRenderedSegmentTableIntersections(
    start,
    end,
    obstacleIndex,
    new Set<ModelId>([
      sourceTable.modelId,
      targetTable.modelId,
      ...(ignoredObstacleModelIds ?? []),
    ]),
  );
}

function countRenderedSegmentTableIntersections(
  start: Point,
  end: Point,
  obstacleIndex: RenderedObstacleIndex,
  ignoredObstacleModelIds: ReadonlySet<ModelId>,
): number {
  return queryRenderedObstacles(obstacleIndex, start, end).filter((table) =>
    !ignoredObstacleModelIds.has(table.modelId)
    && segmentPenetratesRenderedTable(
      start,
      end,
      table,
      SEMANTIC_CARRIER_OBSTACLE_PADDING,
    )
  ).length;
}

function countSemanticCarrierSegmentObstacles(
  segment: SemanticCarrierSegment,
  tableByModelId: Map<ModelId, TableRenderModel>,
  obstacleIndex: RenderedObstacleIndex,
): number {
  const sourceTable = tableByModelId.get(segment.sourceModelId);
  const targetTable = tableByModelId.get(segment.targetModelId);
  return sourceTable && targetTable
    ? countStraightRenderedTableIntersections(sourceTable, targetTable, obstacleIndex)
    : 0;
}

function mergeCoincidentSemanticCarrierSegments(
  segments: SemanticCarrierSegment[],
): SemanticCarrierSegment[] {
  const segmentByKey = new Map<string, SemanticCarrierSegment>();
  for (const segment of segments) {
    const [sourceModelId, targetModelId] =
      String(segment.sourceModelId) < String(segment.targetModelId)
        ? [segment.sourceModelId, segment.targetModelId]
        : [segment.targetModelId, segment.sourceModelId];
    const key = `${sourceModelId}\u0000${targetModelId}`;
    const existing = segmentByKey.get(key);
    if (!existing) {
      segmentByKey.set(key, {
        ...segment,
        sourceModelId,
        targetModelId,
        memberEdgeIds: new Set(segment.memberEdgeIds),
      });
      continue;
    }
    for (const memberEdgeId of segment.memberEdgeIds) {
      existing.memberEdgeIds.add(memberEdgeId);
    }
    if (existing.family !== segment.family) {
      existing.family = "mixed";
    }
    if (existing.role !== segment.role) {
      existing.role = "semantic-tree";
      existing.templateEdge = undefined;
    }
  }
  return [...segmentByKey.values()];
}

function compareSemanticCarrierSegments(
  left: SemanticCarrierSegment,
  right: SemanticCarrierSegment,
): number {
  return left.family.localeCompare(right.family)
    || String(left.sourceModelId).localeCompare(String(right.sourceModelId))
    || String(left.targetModelId).localeCompare(String(right.targetModelId));
}

function createSemanticCarrierRenderEdge(
  segment: SemanticCarrierSegment,
  index: number,
  tableByModelId: Map<ModelId, TableRenderModel>,
  structuralEdgeById: Map<string, StructuralGraphEdge>,
): EdgeRenderModel | undefined {
  const sourceTable = tableByModelId.get(segment.sourceModelId);
  const targetTable = tableByModelId.get(segment.targetModelId);
  if (!sourceTable || !targetTable) {
    return undefined;
  }
  const points = buildStraightRenderedEdgePoints(sourceTable, targetTable);
  const memberEdgeIds = [...segment.memberEdgeIds].sort();
  const logicalEndpointModelIds = [...new Set<ModelId>([
    segment.sourceModelId,
    segment.targetModelId,
    ...memberEdgeIds.flatMap((edgeId) => {
      const edge = structuralEdgeById.get(edgeId);
      return edge ? [edge.sourceModelId, edge.targetModelId] : [];
    }),
  ])].sort();
  if (segment.role === "direct" && segment.templateEdge) {
    return {
      ...segment.templateEdge,
      carrierFamily: segment.family,
      carrierRole: "direct",
      logicalEndpointModelIds,
      memberEdgeIds,
      physicalEndpointModelIds: [segment.sourceModelId, segment.targetModelId],
      points: points.map((point) => `${round2(point.x)},${round2(point.y)}`).join(" "),
      preserveRouteEndpoints: true,
      sourceModelId: segment.sourceModelId,
      targetModelId: segment.targetModelId,
    };
  }
  return {
    carrierFamily: segment.family,
    carrierRole: "semantic-tree",
    crossingIds: [],
    cssKind: `semantic-carrier-${segment.family}`,
    edgeId: `semantic-carrier:${index}`,
    logicalEndpointModelIds,
    markerEndId: "",
    markerStartId: "",
    memberEdgeIds,
    physicalEndpointModelIds: [segment.sourceModelId, segment.targetModelId],
    points: points.map((point) => `${round2(point.x)},${round2(point.y)}`).join(" "),
    preserveRouteEndpoints: true,
    provenance: "semantic_bundle",
    sourceModelId: segment.sourceModelId,
    targetModelId: segment.targetModelId,
  };
}

function semanticCarrierSegmentsConnectEndpoints(
  segments: SemanticCarrierSegment[],
  [sourceModelId, targetModelId]: [ModelId, ModelId],
): boolean {
  const adjacency = new Map<ModelId, ModelId[]>();
  for (const segment of segments) {
    const sourceNeighbors = adjacency.get(segment.sourceModelId) ?? [];
    sourceNeighbors.push(segment.targetModelId);
    adjacency.set(segment.sourceModelId, sourceNeighbors);
    const targetNeighbors = adjacency.get(segment.targetModelId) ?? [];
    targetNeighbors.push(segment.sourceModelId);
    adjacency.set(segment.targetModelId, targetNeighbors);
  }
  const seen = new Set<ModelId>([sourceModelId]);
  const queue: ModelId[] = [sourceModelId];
  for (let index = 0; index < queue.length; index += 1) {
    const modelId = queue[index];
    if (modelId === targetModelId) {
      return true;
    }
    for (const neighbor of adjacency.get(modelId) ?? []) {
      if (seen.has(neighbor)) {
        continue;
      }
      seen.add(neighbor);
      queue.push(neighbor);
    }
  }
  return seen.has(targetModelId);
}

function createHubCarrierRenderGroups(
  routes: RoutedEdgePath[],
  structuralEdgeById: Map<string, StructuralGraphEdge>,
  layoutNodesById: Map<ModelId, DiagramBootstrapPayload["layout"]["nodes"][number]>,
  leafBundles: LeafBundle[],
  bundleIndexByLeafModelId: Map<ModelId, number>,
  threshold: number | undefined,
  inheritanceCarrierGrouping: boolean | undefined,
  intraClusterCarrierGrouping: boolean | undefined,
  renderedCarrierRoutes: RenderedCarrierRoute[] | undefined,
): Map<string, HubCarrierRenderGroup> {
  if ((renderedCarrierRoutes?.length ?? 0) > 0) {
    const routeByEdgeId = new Map(routes.map((route) => [route.edgeId, route] as const));
    const serializedGroups = new Map<string, HubCarrierRenderGroup>();
    for (const carrierRoute of renderedCarrierRoutes ?? []) {
      if (carrierRoute.memberEdgeIds.length < 1 || carrierRoute.points.length < 2) {
        continue;
      }
      const representativeEdgeId = carrierRoute.memberEdgeIds[0];
      const representativeEdge = structuralEdgeById.get(representativeEdgeId);
      if (!representativeEdge || !routeByEdgeId.has(representativeEdgeId)) {
        continue;
      }
      const logicalEndpointModelIds = [...new Set(
        carrierRoute.memberEdgeIds.flatMap((edgeId) => {
          const memberEdge = structuralEdgeById.get(edgeId);
          return memberEdge
            ? [memberEdge.sourceModelId, memberEdge.targetModelId]
            : [];
        }),
      )].sort();
      // A single straight carrier cannot represent three or more table
      // endpoints without explicit branch connectors. Rendering only that
      // averaged trunk leaves the member tables visibly disconnected. Keep
      // the original routed member edges until connector geometry is present.
      if (logicalEndpointModelIds.length > 2) {
        continue;
      }
      if (!carrierPointsConnectLogicalEndpoints(
        carrierRoute.points,
        logicalEndpointModelIds,
        layoutNodesById,
      )) {
        continue;
      }
      const webviewCarrierId = carrierRoute.carrierId.startsWith("H|")
        ? `hub-carrier:${carrierRoute.carrierId.slice(2)}`
        : carrierRoute.carrierId.startsWith("I|")
          ? `inheritance-carrier:${carrierRoute.carrierId.slice(2)}`
          : carrierRoute.carrierId.startsWith("Cself|")
            ? `intra-cluster-carrier:${carrierRoute.carrierId.slice("Cself|".length)}`
            : carrierRoute.carrierId;
      const group: HubCarrierRenderGroup = {
        id: webviewCarrierId,
        logicalEndpointModelIds,
        memberEdgeIds: [...carrierRoute.memberEdgeIds],
        points: carrierRoute.points,
        representativeEdgeId,
        sourceModelId: representativeEdge.sourceModelId,
        targetModelId: representativeEdge.targetModelId,
      };
      for (const edgeId of carrierRoute.memberEdgeIds) {
        if (structuralEdgeById.has(edgeId) && routeByEdgeId.has(edgeId)) {
          serializedGroups.set(edgeId, group);
        }
      }
    }
    return serializedGroups;
  }

  if (threshold === undefined || threshold < 2) {
    return new Map();
  }

  const clusterByModelId = new Map<ModelId, string>();
  const centroidSumByCluster = new Map<string, { count: number; x: number; y: number }>();
  for (const node of layoutNodesById.values()) {
    if (!node.clusterId) {
      continue;
    }
    clusterByModelId.set(node.modelId, node.clusterId);
    const sum = centroidSumByCluster.get(node.clusterId) ?? { count: 0, x: 0, y: 0 };
    sum.count += 1;
    sum.x += centerX(node);
    sum.y += centerY(node);
    centroidSumByCluster.set(node.clusterId, sum);
  }
  const centroidByCluster = new Map<string, Point>();
  for (const [clusterId, sum] of centroidSumByCluster) {
    if (sum.count > 0) {
      centroidByCluster.set(clusterId, {
        x: sum.x / sum.count,
        y: sum.y / sum.count,
      });
    }
  }

  const nearestCluster = (modelId: ModelId): string | undefined => {
    const node = layoutNodesById.get(modelId);
    if (!node || centroidByCluster.size === 0) {
      return undefined;
    }
    const x = centerX(node);
    const y = centerY(node);
    let bestCluster: string | undefined;
    let bestDistance = Number.POSITIVE_INFINITY;
    for (const [clusterId, centroid] of centroidByCluster) {
      const dx = x - centroid.x;
      const dy = y - centroid.y;
      const distance = dx * dx + dy * dy;
      if (distance < bestDistance) {
        bestCluster = clusterId;
        bestDistance = distance;
      }
    }
    return bestCluster;
  };

  const directCarrierByEdgeId = new Map<string, string>();
  const clusterPairsByEdgeId = new Map<string, [string, string]>();
  const incidentCountByCluster = new Map<string, number>();
  for (const route of routes) {
    const edge = structuralEdgeById.get(route.edgeId);
    if (
      !edge ||
      edge.sourceModelId === edge.targetModelId ||
      bundleEdgeMatch(
        edge.sourceModelId,
        edge.targetModelId,
        leafBundles,
        bundleIndexByLeafModelId,
      )
    ) {
      continue;
    }
    if (edge.kind === "inheritance") {
      if (inheritanceCarrierGrouping) {
        directCarrierByEdgeId.set(
          edge.id,
          `inheritance-carrier:${edge.targetModelId}`,
        );
      }
      continue;
    }
    if (
      bundleIndexByLeafModelId.has(edge.sourceModelId) ||
      bundleIndexByLeafModelId.has(edge.targetModelId)
    ) {
      continue;
    }
    const sourceCluster = clusterByModelId.get(edge.sourceModelId) ?? nearestCluster(edge.sourceModelId);
    const targetCluster = clusterByModelId.get(edge.targetModelId) ?? nearestCluster(edge.targetModelId);
    if (!sourceCluster || !targetCluster) {
      continue;
    }
    if (sourceCluster === targetCluster) {
      if (intraClusterCarrierGrouping) {
        directCarrierByEdgeId.set(
          edge.id,
          `intra-cluster-carrier:${sourceCluster}`,
        );
      }
      continue;
    }
    clusterPairsByEdgeId.set(edge.id, [sourceCluster, targetCluster]);
    incidentCountByCluster.set(sourceCluster, (incidentCountByCluster.get(sourceCluster) ?? 0) + 1);
    incidentCountByCluster.set(targetCluster, (incidentCountByCluster.get(targetCluster) ?? 0) + 1);
  }

  const routeGroups = new Map<string, Array<{ edge: StructuralGraphEdge; route: RoutedEdgePath }>>();
  for (const route of routes) {
    const edge = structuralEdgeById.get(route.edgeId);
    const directCarrier = edge
      ? directCarrierByEdgeId.get(edge.id)
      : undefined;
    if (edge && directCarrier && route.points.length >= 2) {
      const members = routeGroups.get(directCarrier) ?? [];
      members.push({ edge, route });
      routeGroups.set(directCarrier, members);
      continue;
    }
    const pair = edge ? clusterPairsByEdgeId.get(edge.id) : undefined;
    if (!edge || !pair || route.points.length < 2) {
      continue;
    }
    const [sourceCluster, targetCluster] = pair;
    const sourceCount = incidentCountByCluster.get(sourceCluster) ?? 0;
    const targetCount = incidentCountByCluster.get(targetCluster) ?? 0;
    if (sourceCount < threshold && targetCount < threshold) {
      continue;
    }
    const hubCluster =
      sourceCount > targetCount || (sourceCount === targetCount && sourceCluster < targetCluster)
        ? sourceCluster
        : targetCluster;
    const carrierId = `hub-carrier:${hubCluster}`;
    const members = routeGroups.get(carrierId) ?? [];
    members.push({ edge, route });
    routeGroups.set(carrierId, members);
  }

  const groupByEdgeId = new Map<string, HubCarrierRenderGroup>();
  const renderedPointsByCarrierId = new Map(
    (renderedCarrierRoutes ?? []).map((route) => [route.carrierId, route.points] as const),
  );
  for (const [carrierId, members] of routeGroups) {
    if (members.length < 2) {
      continue;
    }
    const firstMember = members[0];
    const nativeCarrierId = carrierId.startsWith("hub-carrier:")
      ? `H|${carrierId.slice("hub-carrier:".length)}`
      : carrierId.startsWith("inheritance-carrier:")
        ? `I|${carrierId.slice("inheritance-carrier:".length)}`
        : carrierId.startsWith("intra-cluster-carrier:")
          ? `Cself|${carrierId.slice("intra-cluster-carrier:".length)}`
          : carrierId;
    const optimizedPoints = renderedPointsByCarrierId.get(nativeCarrierId);
    const points = optimizedPoints && optimizedPoints.length >= 2
      ? optimizedPoints
      : [
          averageRouteEndpoint(members, "start"),
          averageRouteEndpoint(members, "end"),
        ];
    const logicalEndpointModelIds = [...new Set(
      members.flatMap((member) => [
        member.edge.sourceModelId,
        member.edge.targetModelId,
      ]),
    )].sort();
    if (
      logicalEndpointModelIds.length > 2
      || !carrierPointsConnectLogicalEndpoints(
        points,
        logicalEndpointModelIds,
        layoutNodesById,
      )
    ) {
      continue;
    }
    const group: HubCarrierRenderGroup = {
      id: carrierId,
      logicalEndpointModelIds,
      memberEdgeIds: members.map((member) => member.edge.id),
      points,
      representativeEdgeId: firstMember.edge.id,
      sourceModelId: firstMember.edge.sourceModelId,
      targetModelId: firstMember.edge.targetModelId,
    };
    for (const member of members) {
      groupByEdgeId.set(member.edge.id, group);
    }
  }

  return groupByEdgeId;
}

function carrierPointsConnectLogicalEndpoints(
  points: Point[],
  logicalEndpointModelIds: ModelId[],
  layoutNodesById: Map<ModelId, DiagramBootstrapPayload["layout"]["nodes"][number]>,
): boolean {
  const tolerance = 1;
  const touchedModelIds = new Set<ModelId>();
  for (const point of points) {
    let touchesEndpoint = false;
    for (const modelId of logicalEndpointModelIds) {
      const node = layoutNodesById.get(modelId);
      if (!node) {
        continue;
      }
      if (
        point.x >= node.position.x - tolerance
        && point.x <= node.position.x + node.size.width + tolerance
        && point.y >= node.position.y - tolerance
        && point.y <= node.position.y + node.size.height + tolerance
      ) {
        touchesEndpoint = true;
        touchedModelIds.add(modelId);
      }
    }
    if (!touchesEndpoint) {
      return false;
    }
  }
  return logicalEndpointModelIds.length > 0
    && logicalEndpointModelIds.every((modelId) => touchedModelIds.has(modelId));
}

function averageRouteEndpoint(
  members: Array<{ route: RoutedEdgePath }>,
  endpoint: "end" | "start",
): Point {
  let x = 0;
  let y = 0;
  for (const member of members) {
    const points = member.route.points;
    const point = endpoint === "start" ? points[0] : points[points.length - 1];
    x += point.x;
    y += point.y;
  }
  return {
    x: round2(x / members.length),
    y: round2(y / members.length),
  };
}

function createTableRenderModel(
  layoutNode: DiagramBootstrapPayload["layout"]["nodes"][number],
  payload: DiagramBootstrapPayload,
  modelsById: Map<ModelId, ExtractedModel>,
  tableOptionsById: Map<ModelId, TableViewOptions>,
): TableRenderModel | undefined {
  const model = modelsById.get(layoutNode.modelId);
  if (!model) {
    return undefined;
  }

  const tableOptions =
    tableOptionsById.get(layoutNode.modelId) ?? defaultTableOptions(layoutNode.modelId);
  const methodAssociations = payload.graph.methodAssociations.filter(
    (association) => association.sourceModelId === layoutNode.modelId,
  );

  return {
    activeMethodName:
      payload.view.selectedMethodContext?.modelId === model.identity.id
        ? payload.view.selectedMethodContext.methodName
        : undefined,
    appLabel: model.identity.appLabel,
    clusterId: layoutNode.clusterId,
    databaseTableName: databaseTableName(model),
    fieldRows: createFieldRows(model),
    hasExplicitDatabaseTableName: Boolean(model.hasExplicitDatabaseTableName),
    hidden: tableOptions.hidden,
    methodAssociations,
    methods: model.methods,
    modelId: model.identity.id,
    modelName: model.identity.modelName,
    position: layoutNode.position,
    properties: model.properties.map((property) =>
      property.returnType ? `${property.name} -> ${property.returnType}` : property.name,
    ),
    selected: payload.view.selectedModelId === model.identity.id,
    showMethodHighlights: tableOptions.showMethodHighlights,
    showMethods: tableOptions.showMethods,
    showProperties: tableOptions.showProperties,
    size: layoutNode.size,
  };
}

function toCatalogTable(table: TableRenderModel, relationDegree: number): TableRenderModel {
  const pressure = Math.max(0, relationDegree - 4);
  const widthFromText = Math.max(
    CATALOG_BASE_TABLE_WIDTH,
    Math.ceil(Math.max(table.modelName.length, table.databaseTableName.length) * 7.4 + 32),
  );
  const width = Math.min(
    CATALOG_MAX_TABLE_WIDTH,
    widthFromText + Math.min(144, Math.ceil(pressure / 8) * 12),
  );
  const height = Math.min(
    CATALOG_MAX_TABLE_HEIGHT,
    CATALOG_BASE_TABLE_HEIGHT + Math.min(360, Math.ceil(pressure / 2) * 8),
  );

  return {
    ...table,
    activeMethodName: undefined,
    fieldRows: [],
    methodAssociations: [],
    methods: [],
    properties: [],
    showMethodHighlights: false,
    showMethods: false,
    showProperties: false,
    size: {
      height,
      width,
    },
  };
}

function createInspectorModelRenderModel(
  table: TableRenderModel,
  relationships: InspectorRelationshipRenderModel[],
): InspectorModelRenderModel {
  return {
    activeMethodName: table.activeMethodName,
    appLabel: table.appLabel,
    databaseTableName: table.databaseTableName,
    fieldRows: table.fieldRows,
    hidden: table.hidden,
    methods: table.methods,
    modelId: table.modelId,
    modelName: table.modelName,
    properties: table.properties,
    relationships,
    selected: table.selected,
    showMethodHighlights: table.showMethodHighlights,
    showMethods: table.showMethods,
    showProperties: table.showProperties,
  };
}

function createInspectorRelationshipsByModelId(
  structuralEdges: StructuralGraphEdge[],
  modelsById: Map<ModelId, ExtractedModel>,
): Map<ModelId, InspectorRelationshipRenderModel[]> {
  const relationshipsByModelId = new Map<ModelId, InspectorRelationshipRenderModel[]>();
  const append = (
    modelId: ModelId,
    relationship: InspectorRelationshipRenderModel,
  ): void => {
    const relationships = relationshipsByModelId.get(modelId) ?? [];
    relationships.push(relationship);
    relationshipsByModelId.set(modelId, relationships);
  };

  for (const edge of structuralEdges) {
    // A declared relation already appears as an incoming entry on its target;
    // including derived_reverse would show the same database relationship twice.
    if (edge.provenance !== "declared") {
      continue;
    }
    const fieldName = declaredRelationshipFieldName(edge, modelsById.get(edge.sourceModelId));
    if (edge.sourceModelId === edge.targetModelId) {
      append(edge.sourceModelId, {
        direction: "self",
        edgeId: edge.id,
        fieldName,
        kind: edge.kind,
        otherModelId: edge.targetModelId,
        sourceModelId: edge.sourceModelId,
        targetModelId: edge.targetModelId,
      });
      continue;
    }
    append(edge.sourceModelId, {
      direction: "outgoing",
      edgeId: edge.id,
      fieldName,
      kind: edge.kind,
      otherModelId: edge.targetModelId,
      sourceModelId: edge.sourceModelId,
      targetModelId: edge.targetModelId,
    });
    append(edge.targetModelId, {
      direction: "incoming",
      edgeId: edge.id,
      fieldName,
      kind: edge.kind,
      otherModelId: edge.sourceModelId,
      sourceModelId: edge.sourceModelId,
      targetModelId: edge.targetModelId,
    });
  }

  for (const relationships of relationshipsByModelId.values()) {
    relationships.sort((left, right) =>
      relationshipDirectionRank(left.direction) - relationshipDirectionRank(right.direction)
      || left.kind.localeCompare(right.kind)
      || String(left.otherModelId).localeCompare(String(right.otherModelId))
      || left.fieldName.localeCompare(right.fieldName)
    );
  }
  return relationshipsByModelId;
}

function declaredRelationshipFieldName(
  edge: StructuralGraphEdge,
  sourceModel: ExtractedModel | undefined,
): string {
  if (edge.kind === "inheritance") {
    return "extends";
  }
  const prefix = `edge:declared:${edge.sourceModelId}:`;
  const encodedFieldName = edge.id.startsWith(prefix)
    ? edge.id.slice(prefix.length)
    : undefined;
  const matchingFields = sourceModel?.fields.filter((field) =>
    field.relation?.kind === edge.kind
    && field.relation.target.resolvedModelId === edge.targetModelId
  ) ?? [];
  return matchingFields.find((field) => field.name === encodedFieldName)?.name
    ?? (matchingFields.length === 1 ? matchingFields[0].name : undefined)
    ?? encodedFieldName
    ?? edge.id;
}

function relationshipDirectionRank(
  direction: InspectorRelationshipRenderModel["direction"],
): number {
  return direction === "outgoing" ? 0 : direction === "self" ? 1 : 2;
}

function databaseTableName(model: ExtractedModel): string {
  return model.databaseTableName ?? `${model.identity.appLabel}_${model.identity.modelName.toLowerCase()}`;
}

function canvasSize(
  payload: DiagramBootstrapPayload,
  tables: TableRenderModel[],
  ignoreRoutes = false,
): { height: number; width: number } {
  const maxX = tables.reduce(
    (largest, table) => Math.max(largest, table.position.x + table.size.width),
    0,
  );
  const maxY = tables.reduce(
    (largest, table) => Math.max(largest, table.position.y + table.size.height),
    0,
  );
  const routeMaxX = ignoreRoutes
    ? maxX
    : payload.layout.routedEdges.reduce(
        (largest, route) =>
          Math.max(largest, ...route.points.map((point) => point.x)),
        maxX,
      );
  const routeMaxY = ignoreRoutes
    ? maxY
    : payload.layout.routedEdges.reduce(
        (largest, route) =>
          Math.max(largest, ...route.points.map((point) => point.y)),
        maxY,
      );

  return {
    height: Math.max(720, Math.ceil(routeMaxY + 220)),
    width: Math.max(1280, Math.ceil(routeMaxX + 260)),
  };
}

function centerX(node: DiagramBootstrapPayload["layout"]["nodes"][number]): number {
  return round2(node.position.x + node.size.width / 2);
}

function centerY(node: DiagramBootstrapPayload["layout"]["nodes"][number]): number {
  return round2(node.position.y + node.size.height / 2);
}

function createDiagnostics(payload: DiagramBootstrapPayload): InspectorRenderModel["diagnostics"] {
  const layoutExecution = createLayoutExecution(payload);
  const combined = [
    ...(layoutExecution.status === "fallback"
      ? [
          {
            code: "layout_fallback",
            message: [
              `Requested ${layoutExecution.requestedLabel} but applied ${layoutExecution.appliedLabel}.`,
              layoutExecution.reason,
            ].filter(Boolean).join(" "),
            severity: "warning",
          },
        ]
      : []),
    ...(layoutExecution.status === "quality-degraded"
      ? [
          {
            code: "layout_quality_degraded",
            message:
              layoutExecution.reason
              ?? "The optimized layout was applied but did not meet its quality target.",
            severity: "warning",
          },
        ]
      : []),
    ...payload.analyzer.diagnostics,
    ...payload.graph.diagnostics,
  ];

  return combined.map((diagnostic) => ({
    code: diagnostic.code,
    message: diagnostic.message,
    severity: diagnostic.severity,
  }));
}

function createLayoutExecution(payload: DiagramBootstrapPayload): LayoutExecutionRenderModel {
  const fallbackRequestedMode = normalizeLayoutMode(payload.view.layoutMode ?? payload.layout.mode);
  const execution = payload.layoutExecution ?? {
    appliedMode: payload.layout.mode,
    engine: payload.layout.nodes.length > 0 ? "analyzer" : "empty",
    requestedMode: fallbackRequestedMode,
    status: payload.layout.nodes.length > 0 ? ("applied" as const) : ("empty" as const),
  };

  return {
    appliedLabel: getLayoutLabel(execution.appliedMode),
    appliedMode: execution.appliedMode,
    engine: execution.engine,
    reason: execution.reason,
    requestedLabel: getLayoutLabel(execution.requestedMode),
    requestedMode: execution.requestedMode,
    status: execution.status,
  };
}

function createLayoutFailures(payload: DiagramBootstrapPayload): LayoutFailureRenderModel[] {
  return Object.entries(payload.layoutFailures ?? {}).map(([layoutMode, reason]) => ({
    label: getLayoutLabel(layoutMode as LayoutMode),
    mode: layoutMode as LayoutMode,
    reason,
  }));
}

function getLayoutLabel(layoutMode: LayoutMode): string {
  return getOgdfLayoutDefinition(normalizeLayoutMode(layoutMode)).label;
}

function createDiscoveryRenderModel(
  discovery: DjangoWorkspaceDiscoveryResult,
): DiscoveryRenderModel {
  return {
    appCount: discovery.apps.length,
    apps: discovery.apps.map((app) => ({
      appLabel: app.appLabel,
      flags: [
        app.hasAppConfig ? "apps.py" : "no apps.py",
        app.hasModelsPy ? "models.py" : "no models.py",
        app.hasModelsPackage ? "models package" : "no models package",
      ],
    })),
    diagnostics: discovery.diagnostics.map((diagnostic) => ({
      code: diagnostic.code,
      message: diagnostic.message,
      severity: diagnostic.severity,
    })),
    selectedRoot: discovery.selectedRoot,
    strategy: discovery.strategy,
  };
}

function createEdgeRenderModel(
  route: RoutedEdgePath,
  structuralEdgeById: Map<string, StructuralGraphEdge>,
  renderedDirectCarrierPointsByEdgeId: Map<string, Point[]>,
): EdgeRenderModel | undefined {
  const edge = structuralEdgeById.get(route.edgeId);
  if (!edge) {
    return undefined;
  }

  const [markerStartId, markerEndId] = markerIds(edge.kind);

  const optimizedPoints = renderedDirectCarrierPointsByEdgeId.get(edge.id);
  return {
    carrierFamily: edge.kind === "inheritance" ? "inheritance" : "association",
    carrierRole: "direct",
    crossingIds: route.crossingIds,
    cssKind: edge.kind.replaceAll("_", "-"),
    edgeId: edge.id,
    markerEndId,
    markerStartId,
    memberEdgeIds: [edge.id],
    logicalEndpointModelIds: [edge.sourceModelId, edge.targetModelId],
    physicalEndpointModelIds: [edge.sourceModelId, edge.targetModelId],
    points: (optimizedPoints ?? route.points)
      .map((point) => `${point.x},${point.y}`)
      .join(" "),
    // These endpoints were already clipped to the table boundary and audited
    // by the native carrier scorer. Reattaching them in the browser subtly
    // changes the first/last segment and can recreate node penetrations that
    // the accepted native route did not contain.
    preserveRouteEndpoints: (optimizedPoints ?? route.points).length >= 2,
    provenance: edge.provenance,
    sourceModelId: edge.sourceModelId,
    targetModelId: edge.targetModelId,
  };
}

interface BundleEdgeMatch {
  bundleIndex: number;
  rootModelId: ModelId;
  leafIsSource: boolean;
}

function bundleEdgeMatch(
  sourceModelId: ModelId,
  targetModelId: ModelId,
  leafBundles: LeafBundle[],
  bundleIndexByLeafModelId: Map<ModelId, number>,
): BundleEdgeMatch | undefined {
  // Edge from bundle leaf → any shared root: leafIsSource=true.
  const sourceBundle = bundleIndexByLeafModelId.get(sourceModelId);
  if (sourceBundle !== undefined) {
    const bundle = leafBundles[sourceBundle];
    const roots = bundle.sharedRootModelIds && bundle.sharedRootModelIds.length > 0
      ? bundle.sharedRootModelIds
      : [bundle.parentModelId];
    if (roots.includes(targetModelId)) {
      return { bundleIndex: sourceBundle, rootModelId: targetModelId, leafIsSource: true };
    }
  }
  // Edge from any shared root → bundle leaf: leafIsSource=false.
  const targetBundle = bundleIndexByLeafModelId.get(targetModelId);
  if (targetBundle !== undefined) {
    const bundle = leafBundles[targetBundle];
    const roots = bundle.sharedRootModelIds && bundle.sharedRootModelIds.length > 0
      ? bundle.sharedRootModelIds
      : [bundle.parentModelId];
    if (roots.includes(sourceModelId)) {
      return { bundleIndex: targetBundle, rootModelId: sourceModelId, leafIsSource: false };
    }
  }
  return undefined;
}

function createFieldRows(model: ExtractedModel): TableRenderModel["fieldRows"] {
  const rows: TableRenderModel["fieldRows"] = [];

  for (const field of model.fields) {
    const flags = [
      field.primaryKey ? "pk" : "",
      field.nullable ? "nullable" : "",
      field.relation?.reverseAccessorName ? `reverse:${field.relation.reverseAccessorName}` : "",
    ].filter(Boolean);
    const relationSuffix = field.relation
      ? ` -> ${field.relation.target.resolvedModelId ?? field.relation.target.rawReference}`
      : "";
    const flagSuffix = flags.length > 0 ? ` (${flags.join(", ")})` : "";

    rows.push({
      key: `${model.identity.id}:field:${field.name}`,
      text: `${field.name}: ${field.fieldType}${relationSuffix}${flagSuffix}`,
      tone: "field",
    });

    for (const option of field.choiceMetadata?.options ?? []) {
      rows.push({
        key: `${model.identity.id}:choice:${field.name}:${option.value}`,
        text: `${option.label} = ${option.value}`,
        tone: "enum-option",
      });
    }
  }

  return rows;
}

function defaultTableOptions(modelId: ModelId): TableViewOptions {
  return {
    hidden: false,
    modelId,
    showMethodHighlights: true,
    showMethods: true,
    showProperties: true,
  };
}

function markerIds(kind: StructuralGraphEdge["kind"]): [string, string] {
  switch (kind) {
    case "foreign_key":
      return ["erd-marker-many", "erd-marker-one"];
    case "many_to_many":
      return ["erd-marker-many", "erd-marker-many"];
    case "one_to_one":
      return ["erd-marker-one", "erd-marker-one"];
    case "reverse_foreign_key":
      return ["erd-marker-one", "erd-marker-many"];
    case "reverse_many_to_many":
      return ["erd-marker-many", "erd-marker-many"];
    case "reverse_one_to_one":
      return ["erd-marker-one", "erd-marker-one"];
    case "inheritance":
      return ["erd-marker-one", "erd-marker-one"];
  }
}

function round2(value: number): number {
  return Math.round(value * 100) / 100;
}

function isDefined<T>(value: T | undefined): value is T {
  return value !== undefined;
}
