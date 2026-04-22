use crate::protocol::diagnostics::{
    AnalyzerDiagnostic, DiagnosticCode, DiagnosticSeverity, SourceLocation, SourceRange,
};
use rustpython_parser::{ParseError, source_code::LinearLocator};
use std::path::Path;

pub fn file_read_diagnostic(file_path: &Path, error: &std::io::Error) -> AnalyzerDiagnostic {
    AnalyzerDiagnostic {
        code: DiagnosticCode::UnsupportedConstruct,
        location: Some(SourceLocation {
            file_path: to_posix_path(file_path),
            range: None,
            symbol_name: None,
        }),
        message: format!("Failed to read Python source file: {error}"),
        related_model_id: None,
        severity: DiagnosticSeverity::Error,
    }
}

pub fn syntax_error_diagnostic(source: &str, error: ParseError) -> AnalyzerDiagnostic {
    let mut locator = LinearLocator::new(source);
    let location = locator.locate(error.offset);
    let start_line = location.row.get();
    let start_column = location.column.get();

    AnalyzerDiagnostic {
        code: DiagnosticCode::SyntaxError,
        location: Some(SourceLocation {
            file_path: to_posix_path(Path::new(&error.source_path)),
            range: Some(SourceRange {
                end_column: start_column,
                end_line: start_line,
                start_column,
                start_line,
            }),
            symbol_name: None,
        }),
        message: error.to_string(),
        related_model_id: None,
        severity: DiagnosticSeverity::Error,
    }
}

fn to_posix_path(file_path: &Path) -> String {
    file_path.to_string_lossy().replace('\\', "/")
}
