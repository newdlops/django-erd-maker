use crate::protocol::analysis::ExtractedModel;
use crate::protocol::diagnostics::{
    AnalyzerDiagnostic, DiagnosticCode, DiagnosticSeverity, SourceLocation,
};
use crate::protocol::model_identity::CanonicalModelId;
use std::path::Path;

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

pub fn canonical_model_id_collision_diagnostic(
    overwritten: &ExtractedModel,
    retained: &ExtractedModel,
) -> AnalyzerDiagnostic {
    let canonical_id = retained.identity.id.as_str();
    let overwritten_provenance = model_provenance(overwritten);
    let retained_provenance = model_provenance(retained);

    AnalyzerDiagnostic {
        code: DiagnosticCode::PartialInference,
        location: retained
            .source_file_path
            .as_ref()
            .map(|file_path| SourceLocation {
                file_path: file_path.clone(),
                range: None,
                symbol_name: Some(retained.identity.model_name.clone()),
            }),
        message: format!(
            "Canonical model ID '{canonical_id}' is produced by multiple extracted models. \
The earlier definition ({overwritten_provenance}) is overwritten by the later definition \
({retained_provenance}) when graph nodes are built. Only the later definition is used for \
graph-node identity. Use unique Django app labels/model names or remove the duplicate module input."
        ),
        related_model_id: Some(retained.identity.id.clone()),
        severity: DiagnosticSeverity::Warning,
    }
}

fn model_provenance(model: &ExtractedModel) -> String {
    let module_path = model.identity.module_path.as_deref().unwrap_or("<unknown>");
    let source_file_path = model.source_file_path.as_deref().unwrap_or("<unknown>");

    format!(
        "app='{}', module='{module_path}', class='{}', source='{source_file_path}'",
        model.identity.app_label, model.identity.model_name
    )
}

fn source_location(file_path: &Path, symbol_name: &str) -> SourceLocation {
    SourceLocation {
        file_path: file_path.to_string_lossy().to_string(),
        range: None,
        symbol_name: Some(symbol_name.to_string()),
    }
}
