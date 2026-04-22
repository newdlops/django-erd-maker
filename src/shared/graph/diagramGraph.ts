import type { ModelId } from "../domain/modelIdentity";
import type { AnalyzerDiagnostic } from "../diagnostics/analyzerDiagnostic";
import type { MethodAssociationConfidence, RelationKind } from "../protocol/analyzerContract";

export type StructuralEdgeProvenance = "declared" | "derived_reverse";

export interface GraphNode {
  appLabel: string;
  modelId: ModelId;
  modelName: string;
}

export interface StructuralGraphEdge {
  id: string;
  kind: RelationKind;
  provenance: StructuralEdgeProvenance;
  sourceModelId: ModelId;
  targetModelId: ModelId;
}

export interface MethodAssociation {
  confidence: MethodAssociationConfidence;
  id: string;
  methodName: string;
  provenance: "method_inference";
  sourceModelId: ModelId;
  targetModelId: ModelId;
}

export interface DiagramGraph {
  diagnostics: AnalyzerDiagnostic[];
  methodAssociations: MethodAssociation[];
  nodes: GraphNode[];
  structuralEdges: StructuralGraphEdge[];
}
