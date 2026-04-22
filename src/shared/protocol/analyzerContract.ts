import type { ModelIdentity, ModelId } from "../domain/modelIdentity";
import type { AnalyzerDiagnostic } from "../diagnostics/analyzerDiagnostic";
import type { ContractVersion } from "./contractVersion";

export type ChoiceValueKind = "boolean" | "null" | "number" | "string";
export type MethodAssociationConfidence = "high" | "low" | "medium";
export type MethodVisibility = "private" | "protected" | "public";
export type ModelFieldPersistence = "computed" | "stored";
export type RelationKind =
  | "foreign_key"
  | "many_to_many"
  | "one_to_one"
  | "reverse_foreign_key"
  | "reverse_many_to_many"
  | "reverse_one_to_one";
export type ResolutionState = "deferred" | "resolved" | "unresolved";

export interface AnalysisSummary {
  diagnosticCount: number;
  discoveredAppCount: number;
  discoveredModelCount: number;
  workspaceRoot: string;
}

export interface ChoiceOption {
  label: string;
  value: string;
  valueKind: ChoiceValueKind;
}

export interface ChoiceFieldMetadata {
  isChoiceField: true;
  isFullyResolved: boolean;
  options: ChoiceOption[];
}

export interface RelationTargetReference {
  appLabelHint?: string;
  rawReference: string;
  resolutionState: ResolutionState;
  resolvedModelId?: ModelId;
}

export interface FieldRelation {
  kind: RelationKind;
  reverseAccessorName?: string;
  target: RelationTargetReference;
  throughModelId?: ModelId;
}

export interface ModelField {
  choiceMetadata?: ChoiceFieldMetadata;
  fieldType: string;
  name: string;
  nullable: boolean;
  persistence: ModelFieldPersistence;
  primaryKey: boolean;
  relation?: FieldRelation;
}

export interface PropertyAttribute {
  name: string;
  returnType?: string;
}

export interface MethodRelatedModelReference {
  confidence: MethodAssociationConfidence;
  evidence?: string;
  rawReference?: string;
  targetModelId?: ModelId;
}

export interface UserMethod {
  name: string;
  relatedModels: MethodRelatedModelReference[];
  visibility: MethodVisibility;
}

export interface ExtractedModel {
  databaseTableName?: string;
  declaredBaseClasses: string[];
  fields: ModelField[];
  hasExplicitDatabaseTableName?: boolean;
  identity: ModelIdentity;
  methods: UserMethod[];
  properties: PropertyAttribute[];
}

export interface AnalyzerOutput {
  contractVersion: ContractVersion;
  diagnostics: AnalyzerDiagnostic[];
  models: ExtractedModel[];
  summary: AnalysisSummary;
}
