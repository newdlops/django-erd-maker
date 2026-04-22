import type { ModelId } from "../domain/modelIdentity";

export type DiagnosticCode =
  | "partial_inference"
  | "syntax_error"
  | "unresolved_reference"
  | "unsupported_construct";

export type DiagnosticSeverity = "error" | "info" | "warning";

export interface SourceRange {
  endColumn: number;
  endLine: number;
  startColumn: number;
  startLine: number;
}

export interface SourceLocation {
  filePath: string;
  range?: SourceRange;
  symbolName?: string;
}

export interface AnalyzerDiagnostic {
  code: DiagnosticCode;
  location?: SourceLocation;
  message: string;
  relatedModelId?: ModelId;
  severity: DiagnosticSeverity;
}
