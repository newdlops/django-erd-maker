import { makeModelId, type ModelIdentity } from "../domain/modelIdentity";
import type { AnalyzerDiagnostic, SourceLocation, SourceRange } from "../diagnostics/analyzerDiagnostic";
import type { DiagramGraph, GraphNode, MethodAssociation, StructuralGraphEdge } from "../graph/diagramGraph";
import type { EdgeCrossing, LayoutSnapshot, NodeLayout, Point, RoutedEdgePath, Size } from "../graph/layoutContract";
import type {
  AnalysisSummary,
  AnalyzerOutput,
  ChoiceFieldMetadata,
  ChoiceOption,
  ExtractedModel,
  FieldRelation,
  MethodRelatedModelReference,
  ModelField,
  PropertyAttribute,
  RelationTargetReference,
  UserMethod,
} from "./analyzerContract";
import { CONTRACT_VERSION, type ContractVersion } from "./contractVersion";
import {
  readArray,
  readBoolean,
  readLiteral,
  readModelId,
  readNumber,
  readOptionalBoolean,
  readOptionalNumber,
  readOptionalObject,
  readOptionalModelId,
  readOptionalString,
  readRecord,
  readString,
  readStringArray,
  type JsonRecord,
} from "./runtimeGuards";
import type { PipelineTimings } from "./pipelineTimingContract";
import type {
  DiagramBootstrapPayload,
  InitialViewState,
  SelectedMethodContext,
  TableViewOptions,
} from "./webviewContract";

export function decodeDiagramBootstrapPayload(
  value: unknown,
): DiagramBootstrapPayload {
  const root = readRecord(value, "diagramBootstrapPayload");

  return {
    analyzer: decodeAnalyzerOutput(readRecord(root.analyzer, "diagramBootstrapPayload.analyzer")),
    contractVersion: decodeContractVersion(root, "contractVersion", "diagramBootstrapPayload"),
    graph: decodeGraph(readRecord(root.graph, "diagramBootstrapPayload.graph")),
    layout: decodeLayout(readRecord(root.layout, "diagramBootstrapPayload.layout"), "diagramBootstrapPayload.layout"),
    timings: decodePipelineTimings(root),
    view: decodeViewState(readRecord(root.view, "diagramBootstrapPayload.view")),
  };
}

export function decodeLayoutSnapshot(
  value: unknown,
  context = "layoutSnapshot",
): LayoutSnapshot {
  return decodeLayout(readRecord(value, context), context);
}

function decodeAnalyzerOutput(record: JsonRecord): AnalyzerOutput {
  return {
    contractVersion: decodeContractVersion(record, "contractVersion", "diagramBootstrapPayload.analyzer"),
    diagnostics: readArray(record, "diagnostics", "diagramBootstrapPayload.analyzer").map((item, index) =>
      decodeDiagnostic(item, `diagramBootstrapPayload.analyzer.diagnostics[${index}]`),
    ),
    models: readArray(record, "models", "diagramBootstrapPayload.analyzer").map((item, index) =>
      decodeExtractedModel(item, `diagramBootstrapPayload.analyzer.models[${index}]`),
    ),
    summary: decodeAnalysisSummary(
      readRecord(record.summary, "diagramBootstrapPayload.analyzer.summary"),
    ),
  };
}

function decodeAnalysisSummary(record: JsonRecord): AnalysisSummary {
  return {
    diagnosticCount: readNumber(record, "diagnosticCount", "diagramBootstrapPayload.analyzer.summary"),
    discoveredAppCount: readNumber(record, "discoveredAppCount", "diagramBootstrapPayload.analyzer.summary"),
    discoveredModelCount: readNumber(record, "discoveredModelCount", "diagramBootstrapPayload.analyzer.summary"),
    workspaceRoot: readString(record, "workspaceRoot", "diagramBootstrapPayload.analyzer.summary"),
  };
}

function decodeChoiceFieldMetadata(record: JsonRecord, context: string): ChoiceFieldMetadata {
  return {
    isChoiceField: readBoolean(record, "isChoiceField", context) as true,
    isFullyResolved: readBoolean(record, "isFullyResolved", context),
    options: readArray(record, "options", context).map((item, index) =>
      decodeChoiceOption(item, `${context}.options[${index}]`),
    ),
  };
}

function decodeChoiceOption(value: unknown, context: string): ChoiceOption {
  const record = readRecord(value, context);

  return {
    label: readString(record, "label", context),
    value: readString(record, "value", context),
    valueKind: readLiteral(record, "valueKind", ["boolean", "null", "number", "string"], context),
  };
}

function decodeContractVersion(
  record: JsonRecord,
  key: string,
  context: string,
): ContractVersion {
  const value = readString(record, key, context);

  if (value !== CONTRACT_VERSION) {
    throw new Error(`${context}.${key} must equal contract version ${CONTRACT_VERSION}.`);
  }

  return value;
}

function decodeDiagnostic(value: unknown, context: string): AnalyzerDiagnostic {
  const record = readRecord(value, context);
  const locationRecord = readOptionalObject(record, "location", context);

  return {
    code: readLiteral(
      record,
      "code",
      ["partial_inference", "syntax_error", "unresolved_reference", "unsupported_construct"],
      context,
    ),
    location: locationRecord ? decodeSourceLocation(locationRecord, `${context}.location`) : undefined,
    message: readString(record, "message", context),
    relatedModelId: readOptionalModelId(record, "relatedModelId", context),
    severity: readLiteral(record, "severity", ["error", "info", "warning"], context),
  };
}

function decodeExtractedModel(value: unknown, context: string): ExtractedModel {
  const record = readRecord(value, context);

  return {
    databaseTableName: readOptionalString(record, "databaseTableName", context),
    declaredBaseClasses: readStringArray(record, "declaredBaseClasses", context),
    fields: readArray(record, "fields", context).map((item, index) =>
      decodeModelField(item, `${context}.fields[${index}]`),
    ),
    hasExplicitDatabaseTableName: readOptionalBoolean(
      record,
      "hasExplicitDatabaseTableName",
      context,
    ),
    identity: decodeModelIdentity(readRecord(record.identity, `${context}.identity`), `${context}.identity`),
    methods: readArray(record, "methods", context).map((item, index) =>
      decodeUserMethod(item, `${context}.methods[${index}]`),
    ),
    properties: readArray(record, "properties", context).map((item, index) =>
      decodePropertyAttribute(item, `${context}.properties[${index}]`),
    ),
  };
}

function decodeGraph(record: JsonRecord): DiagramGraph {
  return {
    diagnostics: readArray(record, "diagnostics", "diagramBootstrapPayload.graph").map((item, index) =>
      decodeDiagnostic(item, `diagramBootstrapPayload.graph.diagnostics[${index}]`),
    ),
    methodAssociations: readArray(record, "methodAssociations", "diagramBootstrapPayload.graph").map((item, index) =>
      decodeMethodAssociation(item, `diagramBootstrapPayload.graph.methodAssociations[${index}]`),
    ),
    nodes: readArray(record, "nodes", "diagramBootstrapPayload.graph").map((item, index) =>
      decodeGraphNode(item, `diagramBootstrapPayload.graph.nodes[${index}]`),
    ),
    structuralEdges: readArray(record, "structuralEdges", "diagramBootstrapPayload.graph").map((item, index) =>
      decodeStructuralGraphEdge(item, `diagramBootstrapPayload.graph.structuralEdges[${index}]`),
    ),
  };
}

function decodeGraphNode(value: unknown, context: string): GraphNode {
  const record = readRecord(value, context);

  return {
    appLabel: readString(record, "appLabel", context),
    modelId: readModelId(record, "modelId", context),
    modelName: readString(record, "modelName", context),
  };
}

function decodeFieldRelation(record: JsonRecord, context: string): FieldRelation {
  return {
    kind: readLiteral(
      record,
      "kind",
      [
        "foreign_key",
        "many_to_many",
        "one_to_one",
        "reverse_foreign_key",
        "reverse_many_to_many",
        "reverse_one_to_one",
      ],
      context,
    ),
    reverseAccessorName: readOptionalString(record, "reverseAccessorName", context),
    target: decodeRelationTargetReference(
      readRecord(record.target, `${context}.target`),
      `${context}.target`,
    ),
    throughModelId: readOptionalModelId(record, "throughModelId", context),
  };
}

function decodeLayout(record: JsonRecord, context: string): LayoutSnapshot {
  return {
    crossings: readArray(record, "crossings", context).map((item, index) =>
      decodeEdgeCrossing(item, `${context}.crossings[${index}]`),
    ),
    mode: readLiteral(record, "mode", ["circular", "clustered", "hierarchical"], context),
    nodes: readArray(record, "nodes", context).map((item, index) =>
      decodeNodeLayout(item, `${context}.nodes[${index}]`),
    ),
    routedEdges: readArray(record, "routedEdges", context).map((item, index) =>
      decodeRoutedEdgePath(item, `${context}.routedEdges[${index}]`),
    ),
  };
}

function decodeMethodAssociation(value: unknown, context: string): MethodAssociation {
  const record = readRecord(value, context);

  return {
    confidence: readLiteral(record, "confidence", ["high", "low", "medium"], context),
    id: readString(record, "id", context),
    methodName: readString(record, "methodName", context),
    provenance: readLiteral(record, "provenance", ["method_inference"], context),
    sourceModelId: readModelId(record, "sourceModelId", context),
    targetModelId: readModelId(record, "targetModelId", context),
  };
}

function decodeMethodRelatedModelReference(
  value: unknown,
  context: string,
): MethodRelatedModelReference {
  const record = readRecord(value, context);

  return {
    confidence: readLiteral(record, "confidence", ["high", "low", "medium"], context),
    evidence: readOptionalString(record, "evidence", context),
    rawReference: readOptionalString(record, "rawReference", context),
    targetModelId: readOptionalModelId(record, "targetModelId", context),
  };
}

function decodePipelineTimings(record: JsonRecord): PipelineTimings | undefined {
  const timings = readOptionalObject(record, "timings", "diagramBootstrapPayload");

  if (!timings) {
    return undefined;
  }

  return {
    analyzerBootstrapMs: readOptionalNumber(timings, "analyzerBootstrapMs", "diagramBootstrapPayload.timings"),
    discoveryMs: readOptionalNumber(timings, "discoveryMs", "diagramBootstrapPayload.timings"),
    extractMs: readOptionalNumber(timings, "extractMs", "diagramBootstrapPayload.timings"),
    graphMs: readOptionalNumber(timings, "graphMs", "diagramBootstrapPayload.timings"),
    layoutMs: readOptionalNumber(timings, "layoutMs", "diagramBootstrapPayload.timings"),
    ogdfLayoutMs: readOptionalNumber(timings, "ogdfLayoutMs", "diagramBootstrapPayload.timings"),
    parseMs: readOptionalNumber(timings, "parseMs", "diagramBootstrapPayload.timings"),
    renderDocumentMs: readOptionalNumber(timings, "renderDocumentMs", "diagramBootstrapPayload.timings"),
  };
}

function decodeModelField(value: unknown, context: string): ModelField {
  const record = readRecord(value, context);
  const choiceMetadata = readOptionalObject(record, "choiceMetadata", context);
  const relation = readOptionalObject(record, "relation", context);

  return {
    choiceMetadata: choiceMetadata
      ? decodeChoiceFieldMetadata(choiceMetadata, `${context}.choiceMetadata`)
      : undefined,
    fieldType: readString(record, "fieldType", context),
    name: readString(record, "name", context),
    nullable: readBoolean(record, "nullable", context),
    persistence: readLiteral(record, "persistence", ["computed", "stored"], context),
    primaryKey: readBoolean(record, "primaryKey", context),
    relation: relation ? decodeFieldRelation(relation, `${context}.relation`) : undefined,
  };
}

function decodeModelIdentity(record: JsonRecord, context: string): ModelIdentity {
  const appLabel = readString(record, "appLabel", context);
  const modelName = readString(record, "modelName", context);
  const id = readModelId(record, "id", context);
  const canonicalId = makeModelId(appLabel, modelName);

  if (id !== canonicalId) {
    throw new Error(`${context}.id must match ${canonicalId}.`);
  }

  return {
    appLabel,
    id,
    modelName,
    modulePath: readOptionalString(record, "modulePath", context),
  };
}

function decodeNodeLayout(value: unknown, context: string): NodeLayout {
  const record = readRecord(value, context);

  return {
    modelId: readModelId(record, "modelId", context),
    position: decodePoint(readRecord(record.position, `${context}.position`), `${context}.position`),
    size: decodeSize(readRecord(record.size, `${context}.size`), `${context}.size`),
  };
}

function decodePoint(record: JsonRecord, context: string): Point {
  return {
    x: readNumber(record, "x", context),
    y: readNumber(record, "y", context),
  };
}

function decodePropertyAttribute(value: unknown, context: string): PropertyAttribute {
  const record = readRecord(value, context);

  return {
    name: readString(record, "name", context),
    returnType: readOptionalString(record, "returnType", context),
  };
}

function decodeRelationTargetReference(
  record: JsonRecord,
  context: string,
): RelationTargetReference {
  return {
    appLabelHint: readOptionalString(record, "appLabelHint", context),
    rawReference: readString(record, "rawReference", context),
    resolutionState: readLiteral(record, "resolutionState", ["deferred", "resolved", "unresolved"], context),
    resolvedModelId: readOptionalModelId(record, "resolvedModelId", context),
  };
}

function decodeRoutedEdgePath(value: unknown, context: string): RoutedEdgePath {
  const record = readRecord(value, context);

  return {
    crossingIds: readStringArray(record, "crossingIds", context),
    edgeId: readString(record, "edgeId", context),
    points: readArray(record, "points", context).map((item, index) =>
      decodePoint(readRecord(item, `${context}.points[${index}]`), `${context}.points[${index}]`),
    ),
  };
}

function decodeSelectedMethodContext(
  record: JsonRecord,
  context: string,
): SelectedMethodContext {
  return {
    methodName: readString(record, "methodName", context),
    modelId: readModelId(record, "modelId", context),
  };
}

function decodeSize(record: JsonRecord, context: string): Size {
  return {
    height: readNumber(record, "height", context),
    width: readNumber(record, "width", context),
  };
}

function decodeSourceLocation(record: JsonRecord, context: string): SourceLocation {
  const range = readOptionalObject(record, "range", context);

  return {
    filePath: readString(record, "filePath", context),
    range: range ? decodeSourceRange(range, `${context}.range`) : undefined,
    symbolName: readOptionalString(record, "symbolName", context),
  };
}

function decodeSourceRange(record: JsonRecord, context: string): SourceRange {
  return {
    endColumn: readNumber(record, "endColumn", context),
    endLine: readNumber(record, "endLine", context),
    startColumn: readNumber(record, "startColumn", context),
    startLine: readNumber(record, "startLine", context),
  };
}

function decodeStructuralGraphEdge(value: unknown, context: string): StructuralGraphEdge {
  const record = readRecord(value, context);

  return {
    id: readString(record, "id", context),
    kind: readLiteral(
      record,
      "kind",
      [
        "foreign_key",
        "many_to_many",
        "one_to_one",
        "reverse_foreign_key",
        "reverse_many_to_many",
        "reverse_one_to_one",
      ],
      context,
    ),
    provenance: readLiteral(record, "provenance", ["declared", "derived_reverse"], context),
    sourceModelId: readModelId(record, "sourceModelId", context),
    targetModelId: readModelId(record, "targetModelId", context),
  };
}

function decodeTableViewOptions(value: unknown, context: string): TableViewOptions {
  const record = readRecord(value, context);
  const manualPosition = readOptionalObject(record, "manualPosition", context);

  return {
    hidden: readBoolean(record, "hidden", context),
    manualPosition: manualPosition
      ? decodePoint(manualPosition, `${context}.manualPosition`)
      : undefined,
    modelId: readModelId(record, "modelId", context),
    showMethodHighlights: readBoolean(record, "showMethodHighlights", context),
    showMethods: readBoolean(record, "showMethods", context),
    showProperties: readBoolean(record, "showProperties", context),
  };
}

function decodeUserMethod(value: unknown, context: string): UserMethod {
  const record = readRecord(value, context);

  return {
    name: readString(record, "name", context),
    relatedModels: readArray(record, "relatedModels", context).map((item, index) =>
      decodeMethodRelatedModelReference(item, `${context}.relatedModels[${index}]`),
    ),
    visibility: readLiteral(record, "visibility", ["private", "protected", "public"], context),
  };
}

function decodeViewState(record: JsonRecord): InitialViewState {
  const selectedMethodContext = readOptionalObject(
    record,
    "selectedMethodContext",
    "diagramBootstrapPayload.view",
  );

  return {
    layoutMode: readLiteral(record, "layoutMode", ["circular", "clustered", "hierarchical"], "diagramBootstrapPayload.view"),
    selectedMethodContext: selectedMethodContext
      ? decodeSelectedMethodContext(
          selectedMethodContext,
          "diagramBootstrapPayload.view.selectedMethodContext",
        )
      : undefined,
    selectedModelId: readOptionalModelId(record, "selectedModelId", "diagramBootstrapPayload.view"),
    tableOptions: readArray(record, "tableOptions", "diagramBootstrapPayload.view").map((item, index) =>
      decodeTableViewOptions(item, `diagramBootstrapPayload.view.tableOptions[${index}]`),
    ),
  };
}

function decodeEdgeCrossing(value: unknown, context: string): EdgeCrossing {
  const record = readRecord(value, context);
  const edgeIds = readStringArray(record, "edgeIds", context);

  if (edgeIds.length !== 2) {
    throw new Error(`${context}.edgeIds must contain exactly two edge IDs.`);
  }

  return {
    edgeIds: [edgeIds[0], edgeIds[1]],
    id: readString(record, "id", context),
    markerStyle: readLiteral(record, "markerStyle", ["bridge", "marker"], context),
    position: decodePoint(readRecord(record.position, `${context}.position`), `${context}.position`),
  };
}
