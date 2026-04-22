use crate::protocol::diagnostics::{
    AnalyzerDiagnostic, DiagnosticCode, DiagnosticSeverity, SourceLocation,
};
use crate::protocol::model_identity::CanonicalModelId;
use std::path::Path;

pub fn partial_inference_diagnostic(
    file_path: &Path,
    symbol_name: &str,
    message: impl Into<String>,
    related_model_id: Option<&CanonicalModelId>,
) -> AnalyzerDiagnostic {
    AnalyzerDiagnostic {
        code: DiagnosticCode::PartialInference,
        location: Some(source_location(file_path, symbol_name)),
        message: message.into(),
        related_model_id: related_model_id.cloned(),
        severity: DiagnosticSeverity::Warning,
    }
}

pub fn unresolved_reference_diagnostic(
    file_path: &Path,
    symbol_name: &str,
    message: impl Into<String>,
    related_model_id: Option<&CanonicalModelId>,
) -> AnalyzerDiagnostic {
    AnalyzerDiagnostic {
        code: DiagnosticCode::UnresolvedReference,
        location: Some(source_location(file_path, symbol_name)),
        message: message.into(),
        related_model_id: related_model_id.cloned(),
        severity: DiagnosticSeverity::Warning,
    }
}

pub fn unsupported_construct_diagnostic(
    file_path: &Path,
    symbol_name: &str,
    message: impl Into<String>,
    related_model_id: Option<&CanonicalModelId>,
) -> AnalyzerDiagnostic {
    AnalyzerDiagnostic {
        code: DiagnosticCode::UnsupportedConstruct,
        location: Some(source_location(file_path, symbol_name)),
        message: message.into(),
        related_model_id: related_model_id.cloned(),
        severity: DiagnosticSeverity::Warning,
    }
}

fn source_location(file_path: &Path, symbol_name: &str) -> SourceLocation {
    SourceLocation {
        file_path: file_path.to_string_lossy().to_string(),
        range: None,
        symbol_name: Some(symbol_name.to_string()),
    }
}
