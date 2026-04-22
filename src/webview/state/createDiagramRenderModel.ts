import type { ModelId } from "../../shared/domain/modelIdentity";
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
import type { EdgeCrossing, RoutedEdgePath } from "../../shared/graph/layoutContract";
import type { StructuralGraphEdge, MethodAssociation } from "../../shared/graph/diagramGraph";

const MODEL_CATALOG_MODE_THRESHOLD = 500;

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

export interface DiagramRenderModel {
  canvas: { height: number; width: number };
  crossings: EdgeCrossing[];
  edges: EdgeRenderModel[];
  inspector: InspectorRenderModel;
  layoutMode: DiagramBootstrapPayload["view"]["layoutMode"];
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
  const tables = payload.layout.nodes
    .map((layoutNode) => createTableRenderModel(layoutNode, payload, modelsById, tableOptionsById))
    .filter(isDefined);
  const modelCatalogMode = tables.length > MODEL_CATALOG_MODE_THRESHOLD;

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

  return {
    canvas: canvasSize(payload, tables, modelCatalogMode),
    crossings: modelCatalogMode ? [] : payload.layout.crossings,
    edges: modelCatalogMode
      ? []
      : payload.layout.routedEdges
          .map((route) => createEdgeRenderModel(route, payload.graph.structuralEdges))
          .filter((edge): edge is EdgeRenderModel => Boolean(edge)),
    inspector: {
      diagnostics: createDiagnostics(payload),
      discovery: discovery ? createDiscoveryRenderModel(discovery) : undefined,
      selectedMethodName: payload.view.selectedMethodContext?.methodName,
      selectedModelId: payload.view.selectedModelId,
    },
    layoutMode: payload.view.layoutMode,
    modelCatalogMode,
    overlays,
    timings: payload.timings,
    tables: modelCatalogMode ? tables.map(toCatalogTable) : tables,
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

function toCatalogTable(table: TableRenderModel): TableRenderModel {
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
  const combined = [
    ...payload.analyzer.diagnostics,
    ...payload.graph.diagnostics,
  ];

  return combined.map((diagnostic) => ({
    code: diagnostic.code,
    message: diagnostic.message,
    severity: diagnostic.severity,
  }));
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
  structuralEdges: StructuralGraphEdge[],
): EdgeRenderModel | undefined {
  const edge = structuralEdges.find((candidate) => candidate.id === route.edgeId);
  if (!edge) {
    return undefined;
  }

  const [markerStartId, markerEndId] = markerIds(edge.kind);

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
  }
}

function round2(value: number): number {
  return Math.round(value * 100) / 100;
}

function isDefined<T>(value: T | undefined): value is T {
  return value !== undefined;
}
