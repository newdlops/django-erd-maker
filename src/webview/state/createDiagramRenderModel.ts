import { makeModelId, type ModelId } from "../../shared/domain/modelIdentity";
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

const MODEL_CATALOG_MODE_THRESHOLD = 500;
const SEMANTIC_CARRIER_NEIGHBOR_LIMIT = 64;
const SEMANTIC_CARRIER_OBSTACLE_PADDING = 10;
const SEMANTIC_CARRIER_SPATIAL_CELL = 1024;
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
  selectedMethodName?: string;
  selectedModelId?: string;
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
  selected: boolean;
  showMethodHighlights: boolean;
  showMethods: boolean;
  showProperties: boolean;
  size: { height: number; width: number };
}

export interface EdgeRenderModel {
  carrierFamily?: "association" | "inheritance" | "mixed";
  carrierRole?: "direct" | "semantic-tree";
  crossingIds: string[];
  cssKind: string;
  edgeId: string;
  logicalEndpointModelIds?: ModelId[];
  markerEndId: string;
  markerStartId: string;
  memberEdgeIds?: string[];
  physicalEndpointModelIds?: ModelId[];
  points: string;
  preserveRouteEndpoints?: boolean;
  provenance: string;
  sourceModelId: ModelId;
  targetModelId: ModelId;
}

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
  modelCatalogMode: boolean;
  overlays: MethodOverlayRenderModel[];
  timings: DiagramBootstrapPayload["timings"];
  tables: TableRenderModel[];
  semanticCarriers?: SemanticCarrierDiagnostics;
  visualCrossings?: number;
}

export interface SemanticCarrierDiagnostics {
  active: boolean;
  bundledRelationships: number;
  carrierSegments: number;
  disconnectedRelationships: string[];
  fallbackRelationships: number;
  missingRelationships: string[];
  obstacleIntersections: number;
  relationships: number;
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
  nodeModelId: ModelId;
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

/**
 * Measures the table rectangles that the canvas actually receives.
 *
 * Bundled leaf tables are intentionally nested inside their synthetic bundle
 * table, so they are represented by that outer table for pairwise clearance.
 * Bundle parents remain ordinary visible tables and are therefore included.
 * The X/Y gaps mirror the native post-layout spacing contract.
 */
export function measureRenderedTableClearance(
  renderModel: DiagramRenderModel,
  gapX = 56,
  gapY = 42,
): RenderedTableClearanceMetrics {
  const serializationTolerance = 0.01;
  const syntheticBundleIds = new Set(
    Object.keys(renderModel.bundleLeavesByFakeId ?? {}),
  );
  const bundledLeafIds = new Set(
    Object.values(renderModel.bundleLeavesByFakeId ?? {}).flat(),
  );
  const objects = renderModel.tables
    .filter((table) =>
      !table.hidden && !bundledLeafIds.has(table.modelId)
    )
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
    Object.keys(renderModel.bundleLeavesByFakeId ?? {}),
  );
  const bundleIdByLeafModelId = new Map<ModelId, string>();
  for (const [bundleId, leafModelIds] of Object.entries(
    renderModel.bundleLeavesByFakeId ?? {},
  )) {
    for (const leafModelId of leafModelIds) {
      bundleIdByLeafModelId.set(leafModelId, bundleId);
    }
  }
  // Bundle leaf cards are nested inside their synthetic outer table, but they
  // are still painted and still obstruct unrelated edges. Count them here just
  // as the browser collision audit does. The endpoint checks below exempt the
  // logical endpoint leaf and its outer bundle, without exempting its visible
  // sibling cards.
  const tables = renderModel.tables.filter((table) => !table.hidden);
  const hits: RenderedEdgeNodeIntersection[] = [];
  const seen = new Set<string>();

  for (const { edge, points } of resolveRenderedEdgeGeometries(renderModel)) {
    const physicalEndpointModelIds = new Set<ModelId>(
      edge.physicalEndpointModelIds
      ?? [edge.sourceModelId, edge.targetModelId],
    );
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
  table: TableRenderModel,
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

function resolveRenderedBundleTableOverlaps(
  tables: TableRenderModel[],
  bundleLeavesByFakeId: Record<string, ModelId[]>,
): {
  offsetByBundleId: Map<ModelId, Point>;
  tables: TableRenderModel[];
} {
  const bundleIds = new Set<ModelId>(
    Object.keys(bundleLeavesByFakeId) as ModelId[],
  );
  if (bundleIds.size === 0) {
    return { offsetByBundleId: new Map(), tables };
  }
  const bundledLeafIds = new Set<ModelId>(
    Object.values(bundleLeavesByFakeId).flat(),
  );
  const tableByModelId = new Map(
    tables.map((table) => [table.modelId, table] as const),
  );
  const placedObstacles = tables
    .filter((table) =>
      !table.hidden
      && !bundleIds.has(table.modelId)
      && !bundledLeafIds.has(table.modelId)
    )
    .map((table) => ({
      bottom: table.position.y + table.size.height,
      left: table.position.x,
      right: table.position.x + table.size.width,
      top: table.position.y,
    }));
  const offsetByBundleId = new Map<ModelId, Point>();
  const overlapsObstacle = (
    table: TableRenderModel,
    position: Point,
    padding = 12,
  ): boolean => {
    const left = position.x - padding;
    const right = position.x + table.size.width + padding;
    const top = position.y - padding;
    const bottom = position.y + table.size.height + padding;
    return placedObstacles.some((obstacle) =>
      Math.min(right, obstacle.right) - Math.max(left, obstacle.left) > 0
      && Math.min(bottom, obstacle.bottom) - Math.max(top, obstacle.top) > 0
    );
  };

  const bundleTables = tables
    .filter((table) => bundleIds.has(table.modelId) && !table.hidden)
    .sort((left, right) =>
      right.size.width * right.size.height - left.size.width * left.size.height
      || String(left.modelId).localeCompare(String(right.modelId))
    );
  for (const bundleTable of bundleTables) {
    const original = bundleTable.position;
    let chosen = original;
    if (overlapsObstacle(bundleTable, original)) {
      const radialStep = Math.max(
        96,
        Math.min(280, Math.min(bundleTable.size.width, bundleTable.size.height) / 2),
      );
      let found = false;
      for (let ring = 1; ring <= 48 && !found; ring += 1) {
        const radius = radialStep * ring;
        const candidateCount = Math.max(16, ring * 8);
        for (let candidateIndex = 0; candidateIndex < candidateCount; candidateIndex += 1) {
          const angle = candidateIndex / candidateCount * Math.PI * 2;
          const candidate = {
            x: round2(Math.max(16, original.x + Math.cos(angle) * radius)),
            y: round2(Math.max(16, original.y + Math.sin(angle) * radius)),
          };
          if (overlapsObstacle(bundleTable, candidate)) {
            continue;
          }
          chosen = candidate;
          found = true;
          break;
        }
      }
    }
    const offset = {
      x: round2(chosen.x - original.x),
      y: round2(chosen.y - original.y),
    };
    offsetByBundleId.set(bundleTable.modelId, offset);
    bundleTable.position = chosen;
    for (const leafModelId of bundleLeavesByFakeId[bundleTable.modelId] ?? []) {
      const leafTable = tableByModelId.get(leafModelId);
      if (!leafTable) {
        continue;
      }
      leafTable.position = {
        x: round2(leafTable.position.x + offset.x),
        y: round2(leafTable.position.y + offset.y),
      };
    }
    placedObstacles.push({
      bottom: chosen.y + bundleTable.size.height,
      left: chosen.x,
      right: chosen.x + bundleTable.size.width,
      top: chosen.y,
    });
  }
  return { offsetByBundleId, tables };
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
  const modelCatalogMode = allTables.length > MODEL_CATALOG_MODE_THRESHOLD;
  const rawLeafBundles = payload.layout.engineMetadata?.leafBundles ?? [];
  const bundleIndexByLeafModelId = new Map<ModelId, number>();
  rawLeafBundles.forEach((bundle, index) => {
    for (const leaf of bundle.leafModelIds) {
      bundleIndexByLeafModelId.set(leaf, index);
    }
  });
  const tableByModelId = new Map(allTables.map((table) => [table.modelId, table] as const));
  let leafBundles = packLeafBundles(rawLeafBundles, tableByModelId).leafBundles;
  const bundleLeafTiles: BundleLeafTile[] = [];
  const bundleFakeIdByIndex = new Map<number, ModelId>();
  const bundleLeavesByFakeId: Record<string, ModelId[]> = {};
  const bundleTables: TableRenderModel[] = [];
  const LEAF_CELL_W = 200;
  const LEAF_CELL_H = 56;
  const LEAF_GAP_X = 10;
  const LEAF_GAP_Y = 8;
  const BUNDLE_HEADER = 48;
  const BUNDLE_PAD = 16;
  const modifiedLeafTables = new Map<ModelId, TableRenderModel>();
  leafBundles.forEach((bundle, bundleIndex) => {
    const parentTable = tableByModelId.get(bundle.parentModelId);
    if (!parentTable) {
      return;
    }
    const safeName = parentTable.modelName.replace(/[^A-Za-z0-9]/g, "_");
    const fakeId = makeModelId("__leafbundle", `${safeName}_${bundleIndex}`);
    bundleFakeIdByIndex.set(bundleIndex, fakeId);
    bundleLeavesByFakeId[fakeId] = [...bundle.leafModelIds];

    const memberLeaves = bundle.leafModelIds
      .map((id) => tableByModelId.get(id))
      .filter((table): table is TableRenderModel => Boolean(table));
    const N = memberLeaves.length;
    const cols = Math.max(1, Math.ceil(Math.sqrt(N)));
    const rows = Math.max(1, Math.ceil(N / cols));
    const innerW = cols * LEAF_CELL_W + (cols - 1) * LEAF_GAP_X;
    const innerH = rows * LEAF_CELL_H + (rows - 1) * LEAF_GAP_Y;
    const outerW = innerW + BUNDLE_PAD * 2;
    const outerH = BUNDLE_HEADER + innerH + BUNDLE_PAD;

    const cx = bundle.bbox.x + bundle.bbox.width / 2;
    const cy = bundle.bbox.y + bundle.bbox.height / 2;
    const outerX = round2(cx - outerW / 2);
    const outerY = round2(cy - outerH / 2);

    bundleTables.push({
      activeMethodName: undefined,
      appLabel: parentTable.appLabel,
      clusterId: parentTable.clusterId,
      databaseTableName: `${N} leaves`,
      fieldRows: [],
      hasExplicitDatabaseTableName: false,
      hidden: false,
      methodAssociations: [],
      methods: [],
      modelId: fakeId,
      modelName: `${parentTable.modelName} · leaves`,
      position: { x: outerX, y: outerY },
      properties: [],
      selected: false,
      showMethodHighlights: false,
      showMethods: false,
      showProperties: false,
      size: { height: outerH, width: outerW },
    });

    memberLeaves.forEach((leaf, idx) => {
      const col = idx % cols;
      const row = Math.floor(idx / cols);
      const leafX = round2(outerX + BUNDLE_PAD + col * (LEAF_CELL_W + LEAF_GAP_X));
      const leafY = round2(outerY + BUNDLE_HEADER + row * (LEAF_CELL_H + LEAF_GAP_Y));
      modifiedLeafTables.set(leaf.modelId, {
        ...leaf,
        fieldRows: [],
        methodAssociations: [],
        methods: [],
        position: { x: leafX, y: leafY },
        properties: [],
        showMethodHighlights: false,
        showMethods: false,
        showProperties: false,
        size: { height: LEAF_CELL_H, width: LEAF_CELL_W },
      });
    });
  });
  const tables = [
    ...allTables.map((t) => modifiedLeafTables.get(t.modelId) ?? t),
    ...bundleTables,
  ];
  const catalogDegreeByModel = modelCatalogMode
    ? createCatalogRelationDegreeByModel(payload.graph.structuralEdges, layoutNodesById)
    : new Map<ModelId, number>();
  let renderedTables = modelCatalogMode
    ? tables.map((table) =>
        String(table.modelId).startsWith("__leafbundle.")
        || bundleIndexByLeafModelId.has(table.modelId)
          ? table
          : toCatalogTable(table, catalogDegreeByModel.get(table.modelId) ?? 0))
    : tables;
  const bundlePlacement = modelCatalogMode
    ? resolveRenderedBundleTableOverlaps(
        renderedTables,
        bundleLeavesByFakeId,
      )
    : {
        offsetByBundleId: new Map<ModelId, Point>(),
        tables: renderedTables,
      };
  renderedTables = bundlePlacement.tables;
  leafBundles = leafBundles.map((bundle, bundleIndex) => {
    const fakeBundleId = bundleFakeIdByIndex.get(bundleIndex);
    const offset = fakeBundleId
      ? bundlePlacement.offsetByBundleId.get(fakeBundleId)
      : undefined;
    return offset
      ? {
          ...bundle,
          anchor: {
            x: bundle.anchor.x + offset.x,
            y: bundle.anchor.y + offset.y,
          },
          bbox: {
            ...bundle.bbox,
            x: bundle.bbox.x + offset.x,
            y: bundle.bbox.y + offset.y,
          },
        }
      : bundle;
  });
  const structuralEdgeById = new Map(
    payload.graph.structuralEdges.map((edge) => [edge.id, edge] as const),
  );
  const memberEdgeIdsByBundleRoot = new Map<string, string[]>();
  for (const edge of payload.graph.structuralEdges) {
    const match = bundleEdgeMatch(
      edge.sourceModelId,
      edge.targetModelId,
      leafBundles,
      bundleIndexByLeafModelId,
    );
    if (!match) {
      continue;
    }
    const carrierKey = `${match.bundleIndex}|${match.rootModelId}`;
    const memberEdgeIds = memberEdgeIdsByBundleRoot.get(carrierKey) ?? [];
    memberEdgeIds.push(edge.id);
    memberEdgeIdsByBundleRoot.set(carrierKey, memberEdgeIds);
  }
  const hubCarrierByEdgeId = createHubCarrierRenderGroups(
    payload.layout.routedEdges,
    structuralEdgeById,
    layoutNodesById,
    rawLeafBundles,
    bundleIndexByLeafModelId,
    payload.layout.engineMetadata?.hubCarrierThreshold,
    payload.layout.engineMetadata?.inheritanceCarrierGrouping,
    payload.layout.engineMetadata?.intraClusterCarrierGrouping,
    payload.layout.engineMetadata?.renderedCarrierRoutes,
  );
  const renderedBundleCarrierPointsByKey = new Map<string, Point[]>();
  const renderedDirectCarrierPointsByEdgeId = new Map<string, Point[]>();
  for (const route of payload.layout.engineMetadata?.renderedCarrierRoutes ?? []) {
    const match = /^B(\d+)\|(.+)$/.exec(route.carrierId);
    if (match && route.points.length >= 2) {
      renderedBundleCarrierPointsByKey.set(`${match[1]}|${match[2]}`, route.points);
    }
    if (
      route.points.length >= 2
      && route.memberEdgeIds.length === 1
      && route.carrierId === route.memberEdgeIds[0]
    ) {
      renderedDirectCarrierPointsByEdgeId.set(route.carrierId, route.points);
    }
  }
  // Set of `${bundleIndex}|${rootModelId}` keys: ensures ONE carrier edge
  // per (bundle, shared-root) pair, so a bus bundle with N shared roots
  // produces N carrier edges (one per root) instead of one global.
  const carrierAssignedByBundleRoot = new Set<string>();
  const baseRoutedEdges = payload.layout.routedEdges
    .map((route) =>
      createEdgeRenderModel(
        route,
        structuralEdgeById,
        leafBundles,
        bundleIndexByLeafModelId,
        carrierAssignedByBundleRoot,
        bundleFakeIdByIndex,
        hubCarrierByEdgeId,
        memberEdgeIdsByBundleRoot,
        modelCatalogMode,
        renderedBundleCarrierPointsByKey,
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
  const straightRenderedEdges = (
    modelCatalogMode && routedEdges.length === 0
      ? catalogEdges
      : routedEdges
  ).map(enforceStraightRenderedEdge);
  const semanticCarrierResult = modelCatalogMode
    ? createSemanticCarrierEdges(
        straightRenderedEdges,
        renderedTables,
        structuralEdgeById,
        new Map(
          [...bundleIndexByLeafModelId].flatMap(([leafModelId, bundleIndex]) => {
            const fakeBundleId = bundleFakeIdByIndex.get(bundleIndex);
            return fakeBundleId ? [[leafModelId, fakeBundleId] as const] : [];
          }),
        ),
      )
    : undefined;
  const renderedEdges = semanticCarrierResult?.edges ?? straightRenderedEdges;

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

  return {
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
      selectedMethodName: payload.view.selectedMethodContext?.methodName,
      selectedModelId: payload.view.selectedModelId,
    },
    layoutExecution: createLayoutExecution(payload),
    layoutFailures: createLayoutFailures(payload),
    layoutMode: payload.view.layoutMode,
    leafBundles,
    modelCatalogMode,
    overlays,
    timings: payload.timings,
    tables: renderedTables,
    semanticCarriers: semanticCarrierResult?.diagnostics,
    visualCrossings: payload.layout.engineMetadata?.visualCrossings,
  };
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
    crossingIds: [],
    cssKind: edge.kind.replaceAll("_", "-"),
    edgeId: edge.id,
    markerEndId,
    markerStartId,
    memberEdgeIds: [edge.id],
    points: "",
    provenance: edge.provenance,
    sourceModelId: edge.sourceModelId,
    targetModelId: edge.targetModelId,
  };
}

type SemanticCarrierFamily = "association" | "inheritance" | "mixed";

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
function createSemanticCarrierEdges(
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
  const byCell = new Map<string, TableRenderModel[]>();
  for (const table of visibleTables) {
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
        const members = byCell.get(key) ?? [];
        members.push(table);
        byCell.set(key, members);
      }
    }
  }
  return { byCell, visibleTables };
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
): number {
  const [start, end] = buildStraightRenderedEdgePoints(sourceTable, targetTable);
  return queryRenderedObstacles(obstacleIndex, start, end).filter((table) =>
    table.modelId !== sourceTable.modelId
    && table.modelId !== targetTable.modelId
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
  leafBundles: LeafBundle[],
  bundleIndexByLeafModelId: Map<ModelId, number>,
  carrierAssignedByBundleRoot: Set<string>,
  bundleFakeIdByIndex: Map<number, ModelId>,
  hubCarrierByEdgeId: Map<string, HubCarrierRenderGroup>,
  memberEdgeIdsByBundleRoot: Map<string, string[]>,
  retainBundledLeafRelations: boolean,
  renderedBundleCarrierPointsByKey: Map<string, Point[]>,
  renderedDirectCarrierPointsByEdgeId: Map<string, Point[]>,
): EdgeRenderModel | undefined {
  const edge = structuralEdgeById.get(route.edgeId);
  if (!edge) {
    return undefined;
  }

  const [markerStartId, markerEndId] = markerIds(edge.kind);

  const match = bundleEdgeMatch(
    edge.sourceModelId,
    edge.targetModelId,
    leafBundles,
    bundleIndexByLeafModelId,
  );
  if (match !== undefined) {
    // ONE carrier edge per (bundle, sharedRoot) pair. Multiple
    // underlying edges from the same shared root to bundle leaves all
    // collapse to a single visual line.
    const carrierKey = `${match.bundleIndex}|${match.rootModelId}`;
    if (carrierAssignedByBundleRoot.has(carrierKey)) {
      return undefined;
    }
    const fakeBundleId = bundleFakeIdByIndex.get(match.bundleIndex);
    if (!fakeBundleId) {
      return undefined;
    }
    carrierAssignedByBundleRoot.add(carrierKey);
    const optimizedPoints = renderedBundleCarrierPointsByKey.get(carrierKey);
    const orientedPoints = optimizedPoints
      ? (match.leafIsSource ? optimizedPoints : [...optimizedPoints].reverse())
      : undefined;
    return {
      crossingIds: [],
      cssKind: edge.kind.replaceAll("_", "-"),
      edgeId: edge.id,
      logicalEndpointModelIds: [
        fakeBundleId,
        match.rootModelId,
        ...leafBundles[match.bundleIndex].leafModelIds,
        leafBundles[match.bundleIndex].parentModelId,
        ...(leafBundles[match.bundleIndex].sharedRootModelIds ?? []),
      ].filter((modelId, index, values) => values.indexOf(modelId) === index),
      markerEndId,
      markerStartId,
      memberEdgeIds: memberEdgeIdsByBundleRoot.get(carrierKey) ?? [edge.id],
      points: orientedPoints
        ? orientedPoints.map((point) => `${point.x},${point.y}`).join(" ")
        : "",
      preserveRouteEndpoints: Boolean(orientedPoints),
      provenance: edge.provenance,
      sourceModelId: match.leafIsSource ? fakeBundleId : match.rootModelId,
      targetModelId: match.leafIsSource ? match.rootModelId : fakeBundleId,
    };
  }

  const hubCarrier = hubCarrierByEdgeId.get(edge.id);
  if (hubCarrier !== undefined) {
    if (hubCarrier.representativeEdgeId !== edge.id) {
      return undefined;
    }
    return {
      crossingIds: [],
      cssKind: edge.kind.replaceAll("_", "-"),
      edgeId: hubCarrier.id,
      logicalEndpointModelIds: hubCarrier.logicalEndpointModelIds,
      markerEndId,
      markerStartId,
      memberEdgeIds: hubCarrier.memberEdgeIds,
      points: hubCarrier.points.map((point) => `${point.x},${point.y}`).join(" "),
      preserveRouteEndpoints: true,
      provenance: edge.provenance,
      sourceModelId: hubCarrier.sourceModelId,
      targetModelId: hubCarrier.targetModelId,
    };
  }
  if (
    bundleIndexByLeafModelId.has(edge.sourceModelId) ||
    bundleIndexByLeafModelId.has(edge.targetModelId)
  ) {
    // An edge whose endpoint is a bundled leaf but which is NOT a carrier
    // match (the other endpoint is not the bundle's shared root/parent)
    // used to be dropped. Compact diagrams retain that legacy simplification,
    // except that inheritance ("is-a") must stay visible. Large semantic-
    // carrier diagrams keep every one of these relationships; their physical
    // leaf endpoint is normalized to the enclosing bundle boundary later.
    // The bundled leaf is still a rendered compact pill (modifiedLeafTables
    // keeps its modelId in `tables`), so emit a direct edge with empty
    // points: the renderer's getStaticOrLiveEdgePath falls back to
    // buildStraightPath between the two rendered tables at their current
    // (compact) positions — the same path mechanism the bundle carrier uses.
    if (edge.kind === "inheritance" || retainBundledLeafRelations) {
      const optimizedPoints = renderedDirectCarrierPointsByEdgeId.get(edge.id);
      return {
        crossingIds: [],
        cssKind: edge.kind.replaceAll("_", "-"),
        edgeId: edge.id,
        markerEndId,
        markerStartId,
        memberEdgeIds: [edge.id],
        points: optimizedPoints
          ? optimizedPoints.map((point) => `${point.x},${point.y}`).join(" ")
          : "",
        preserveRouteEndpoints: Boolean(optimizedPoints),
        provenance: edge.provenance,
        sourceModelId: edge.sourceModelId,
        targetModelId: edge.targetModelId,
      };
    }
    return undefined;
  }

  const optimizedPoints = renderedDirectCarrierPointsByEdgeId.get(edge.id);
  return {
    crossingIds: route.crossingIds,
    cssKind: edge.kind.replaceAll("_", "-"),
    edgeId: edge.id,
    markerEndId,
    markerStartId,
    memberEdgeIds: [edge.id],
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

const BUNDLE_NODE_WIDTH = 280;
const BUNDLE_NODE_HEIGHT = 88;

function packLeafBundles(
  rawBundles: LeafBundle[],
  tableByModelId: Map<ModelId, TableRenderModel>,
): { leafBundles: LeafBundle[]; bundleLeafTiles: BundleLeafTile[] } {
  const packed: LeafBundle[] = [];
  rawBundles.forEach((bundle) => {
    const memberCount = bundle.leafModelIds.filter((id) => tableByModelId.has(id)).length;
    if (memberCount === 0) {
      packed.push(bundle);
      return;
    }
    const cx = bundle.bbox.x + bundle.bbox.width / 2;
    const cy = bundle.bbox.y + bundle.bbox.height / 2;
    packed.push({
      anchor: bundle.anchor,
      bbox: {
        height: BUNDLE_NODE_HEIGHT,
        width: BUNDLE_NODE_WIDTH,
        x: round2(cx - BUNDLE_NODE_WIDTH / 2),
        y: round2(cy - BUNDLE_NODE_HEIGHT / 2),
      },
      leafModelIds: bundle.leafModelIds,
      parentModelId: bundle.parentModelId,
      sharedRootModelIds: bundle.sharedRootModelIds,
    });
  });
  return { bundleLeafTiles: [], leafBundles: packed };
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
