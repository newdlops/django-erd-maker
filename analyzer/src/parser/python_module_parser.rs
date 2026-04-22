use crate::parser::ast_adapter::PythonModuleAst;
use crate::parser::diagnostics::{file_read_diagnostic, syntax_error_diagnostic};
use crate::protocol::diagnostics::AnalyzerDiagnostic;
use rustpython_parser::{Parse, ast};
use std::fs;
use std::path::{Path, PathBuf};

pub type ParserResult<T> = Result<T, Vec<AnalyzerDiagnostic>>;

#[derive(Debug)]
pub struct ParsedPythonModule {
    pub ast: PythonModuleAst,
    pub source: String,
    pub source_path: PathBuf,
}

impl ParsedPythonModule {
    pub fn statements(&self) -> &[ast::Stmt] {
        self.ast.statements()
    }

    pub fn top_level_class_names(&self) -> Vec<String> {
        self.ast.top_level_class_names()
    }

    pub fn top_level_function_names(&self) -> Vec<String> {
        self.ast.top_level_function_names()
    }
}

pub fn parse_python_module_file(file_path: &Path) -> ParserResult<ParsedPythonModule> {
    let source = fs::read_to_string(file_path)
        .map_err(|error| vec![file_read_diagnostic(file_path, &error)])?;

    parse_python_module_source(&source, file_path)
}

pub fn parse_python_module_source(
    source: &str,
    source_path: &Path,
) -> ParserResult<ParsedPythonModule> {
    let suite = ast::Suite::parse(source, &source_path.to_string_lossy())
        .map_err(|error| vec![syntax_error_diagnostic(source, error)])?;

    Ok(ParsedPythonModule {
        ast: PythonModuleAst::new(suite),
        source: source.to_string(),
        source_path: source_path.to_path_buf(),
    })
}

#[cfg(test)]
mod tests {
    use super::parse_python_module_file;
    use crate::protocol::diagnostics::DiagnosticCode;
    use std::path::PathBuf;

    #[test]
    fn parses_simple_model_fixture() {
        let fixture_path = fixture_path("single_app_project/blog/models.py");

        let parsed = parse_python_module_file(&fixture_path).expect("expected valid fixture");

        assert_eq!(parsed.top_level_class_names(), vec!["Post"]);
        assert!(parsed.top_level_function_names().is_empty());
    }

    #[test]
    fn parses_feature_rich_model_fixture() {
        let fixture_path = fixture_path("feature_rich_project/blog/models.py");

        let parsed = parse_python_module_file(&fixture_path).expect("expected valid fixture");

        assert_eq!(parsed.top_level_class_names(), vec!["Post"]);
        assert!(parsed.top_level_function_names().is_empty());
        assert!(parsed.source.contains("TextChoices"));
    }

    #[test]
    fn maps_missing_file_to_unsupported_diagnostic() {
        let fixture_path = fixture_path("missing_project/blog/models.py");
        let diagnostics =
            parse_python_module_file(&fixture_path).expect_err("expected missing file diagnostic");

        assert_eq!(diagnostics.len(), 1);
        assert_eq!(diagnostics[0].code, DiagnosticCode::UnsupportedConstruct);
    }

    #[test]
    fn maps_syntax_error_to_syntax_diagnostic() {
        let fixture_path = fixture_path("syntax_error_project/broken/models.py");
        let diagnostics =
            parse_python_module_file(&fixture_path).expect_err("expected syntax error diagnostic");

        assert_eq!(diagnostics.len(), 1);
        assert_eq!(diagnostics[0].code, DiagnosticCode::SyntaxError);
        assert_eq!(
            diagnostics[0].severity,
            crate::protocol::diagnostics::DiagnosticSeverity::Error
        );
        assert!(
            diagnostics[0]
                .location
                .as_ref()
                .expect("expected source location")
                .file_path
                .ends_with("syntax_error_project/broken/models.py")
        );
    }

    fn fixture_path(relative_path: &str) -> PathBuf {
        PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../test/fixtures/django")
            .join(relative_path)
    }
}
