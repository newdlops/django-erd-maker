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
import type { EdgeCrossing, LeafBundle, Point, RoutedEdgePath } from "../../shared/graph/layoutContract";
import type { StructuralGraphEdge, MethodAssociation } from "../../shared/graph/diagramGraph";

const MODEL_CATALOG_MODE_THRESHOLD = 500;
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
  status: "applied" | "empty" | "fallback";
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
  crossingIds: string[];
  cssKind: string;
  edgeId: string;
  markerEndId: string;
  markerStartId: string;
  points: string;
  provenance: string;
  sourceModelId: ModelId;
  targetModelId: ModelId;
}

interface HubCarrierRenderGroup {
  id: string;
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
  memberCount: number;
}

export interface DiagramRenderModel {
  bundleLeafTiles: BundleLeafTile[];
  bundleLeavesByFakeId: Record<string, ModelId[]>;
  canvas: { height: number; width: number };
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
  const leafBundles = packLeafBundles(rawLeafBundles, tableByModelId).leafBundles;
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
  const renderedTables = modelCatalogMode
    ? tables.map((table) =>
        String(table.modelId).startsWith("__leafbundle.")
        || bundleIndexByLeafModelId.has(table.modelId)
          ? table
          : toCatalogTable(table, catalogDegreeByModel.get(table.modelId) ?? 0))
    : tables;
  const structuralEdgeById = new Map(
    payload.graph.structuralEdges.map((edge) => [edge.id, edge] as const),
  );
  const hubCarrierByEdgeId = createHubCarrierRenderGroups(
    payload.layout.routedEdges,
    structuralEdgeById,
    layoutNodesById,
    rawLeafBundles,
    bundleIndexByLeafModelId,
    payload.layout.engineMetadata?.hubCarrierThreshold,
  );
  // Set of `${bundleIndex}|${rootModelId}` keys: ensures ONE carrier edge
  // per (bundle, shared-root) pair, so a bus bundle with N shared roots
  // produces N carrier edges (one per root) instead of one global.
  const carrierAssignedByBundleRoot = new Set<string>();
  const routedEdges = payload.layout.routedEdges
    .map((route) =>
      createEdgeRenderModel(
        route,
        structuralEdgeById,
        leafBundles,
        bundleIndexByLeafModelId,
        carrierAssignedByBundleRoot,
        bundleFakeIdByIndex,
        hubCarrierByEdgeId,
      ),
    )
    .filter((edge): edge is EdgeRenderModel => Boolean(edge));
  const catalogEdges = modelCatalogMode
    ? payload.graph.structuralEdges
        .map((edge) => createCatalogEdgeRenderModel(edge, layoutNodesById))
        .filter((edge): edge is EdgeRenderModel => Boolean(edge))
    : [];
  const renderedEdges = modelCatalogMode && routedEdges.length === 0
    ? catalogEdges
    : routedEdges;

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
  const clusterOutlines = computeClusterOutlines(renderedTables);

  return {
    bundleLeafTiles,
    bundleLeavesByFakeId,
    canvas: canvasSize(payload, renderedTables, modelCatalogMode && routedEdges.length === 0),
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
  };
}

function computeClusterOutlines(
  tables: TableRenderModel[],
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
    outlines.push({
      bbox: {
        height: yMax - yMin + 2 * PAD,
        width: xMax - xMin + 2 * PAD,
        x: xMin - PAD,
        y: yMin - PAD,
      },
      clusterId,
      memberCount: members.length,
    });
  }
  return outlines;
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
    points: "",
    provenance: edge.provenance,
    sourceModelId: edge.sourceModelId,
    targetModelId: edge.targetModelId,
  };
}

function createHubCarrierRenderGroups(
  routes: RoutedEdgePath[],
  structuralEdgeById: Map<string, StructuralGraphEdge>,
  layoutNodesById: Map<ModelId, DiagramBootstrapPayload["layout"]["nodes"][number]>,
  leafBundles: LeafBundle[],
  bundleIndexByLeafModelId: Map<ModelId, number>,
  threshold: number | undefined,
): Map<string, HubCarrierRenderGroup> {
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

  const clusterPairsByEdgeId = new Map<string, [string, string]>();
  const incidentCountByCluster = new Map<string, number>();
  for (const route of routes) {
    const edge = structuralEdgeById.get(route.edgeId);
    if (
      !edge ||
      edge.sourceModelId === edge.targetModelId ||
      // Inheritance ("is-a") edges are structurally significant and few;
      // never fold them into a cluster-to-cluster hub carrier or the direct
      // parent→child line disappears (e.g. Shareholder→Stakeholder). They
      // always render as their own edge.
      edge.kind === "inheritance" ||
      bundleEdgeMatch(edge.sourceModelId, edge.targetModelId, leafBundles, bundleIndexByLeafModelId) ||
      bundleIndexByLeafModelId.has(edge.sourceModelId) ||
      bundleIndexByLeafModelId.has(edge.targetModelId)
    ) {
      continue;
    }
    const sourceCluster = clusterByModelId.get(edge.sourceModelId) ?? nearestCluster(edge.sourceModelId);
    const targetCluster = clusterByModelId.get(edge.targetModelId) ?? nearestCluster(edge.targetModelId);
    if (!sourceCluster || !targetCluster || sourceCluster === targetCluster) {
      continue;
    }
    clusterPairsByEdgeId.set(edge.id, [sourceCluster, targetCluster]);
    incidentCountByCluster.set(sourceCluster, (incidentCountByCluster.get(sourceCluster) ?? 0) + 1);
    incidentCountByCluster.set(targetCluster, (incidentCountByCluster.get(targetCluster) ?? 0) + 1);
  }

  const routeGroups = new Map<string, Array<{ edge: StructuralGraphEdge; route: RoutedEdgePath }>>();
  for (const route of routes) {
    const edge = structuralEdgeById.get(route.edgeId);
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
  for (const [carrierId, members] of routeGroups) {
    if (members.length < 2) {
      continue;
    }
    const firstMember = members[0];
    const start = averageRouteEndpoint(members, "start");
    const end = averageRouteEndpoint(members, "end");
    const group: HubCarrierRenderGroup = {
      id: carrierId,
      points: [start, end],
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
    return {
      crossingIds: [],
      cssKind: edge.kind.replaceAll("_", "-"),
      edgeId: edge.id,
      markerEndId,
      markerStartId,
      points: "",
      provenance: edge.provenance,
      sourceModelId: match.leafIsSource ? fakeBundleId : match.rootModelId,
      targetModelId: match.leafIsSource ? match.rootModelId : fakeBundleId,
    };
  }
  if (
    bundleIndexByLeafModelId.has(edge.sourceModelId) ||
    bundleIndexByLeafModelId.has(edge.targetModelId)
  ) {
    // An edge whose endpoint is a bundled leaf but which is NOT a carrier
    // match (the other endpoint is not the bundle's shared root/parent)
    // would otherwise be dropped. For most edge kinds that is intentional —
    // a leaf's incidental relations collapse into the bundle. But an
    // inheritance ("is-a") edge to a parent OTHER than the bundle root must
    // stay visible (e.g. CmeBulkEmailRecipient → BulkEmailRecipient, where
    // the child is a topological leaf bundled under a different FK parent).
    // The bundled leaf is still a rendered compact pill (modifiedLeafTables
    // keeps its modelId in `tables`), so emit a direct edge with empty
    // points: the renderer's getStaticOrLiveEdgePath falls back to
    // buildStraightPath between the two rendered tables at their current
    // (compact) positions — the same path mechanism the bundle carrier uses.
    if (edge.kind === "inheritance") {
      return {
        crossingIds: [],
        cssKind: edge.kind.replaceAll("_", "-"),
        edgeId: edge.id,
        markerEndId,
        markerStartId,
        points: "",
        provenance: edge.provenance,
        sourceModelId: edge.sourceModelId,
        targetModelId: edge.targetModelId,
      };
    }
    return undefined;
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
      markerEndId,
      markerStartId,
      points: hubCarrier.points.map((point) => `${point.x},${point.y}`).join(" "),
      provenance: edge.provenance,
      sourceModelId: hubCarrier.sourceModelId,
      targetModelId: hubCarrier.targetModelId,
    };
  }

  return {
    crossingIds: route.crossingIds,
    cssKind: edge.kind.replaceAll("_", "-"),
    edgeId: edge.id,
    markerEndId,
    markerStartId,
    points: route.points.map((point) => `${point.x},${point.y}`).join(" "),
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
