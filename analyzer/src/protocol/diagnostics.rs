use crate::protocol::model_identity::CanonicalModelId;
use serde::Serialize;

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum DiagnosticCode {
    PartialInference,
    SyntaxError,
    UnresolvedReference,
    UnsupportedConstruct,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum DiagnosticSeverity {
    Error,
    Info,
    Warning,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SourceRange {
    pub end_column: u32,
    pub end_line: u32,
    pub start_column: u32,
    pub start_line: u32,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SourceLocation {
    pub file_path: String,
    pub range: Option<SourceRange>,
    pub symbol_name: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AnalyzerDiagnostic {
    pub code: DiagnosticCode,
    pub location: Option<SourceLocation>,
    pub message: String,
    pub related_model_id: Option<CanonicalModelId>,
    pub severity: DiagnosticSeverity,
}
