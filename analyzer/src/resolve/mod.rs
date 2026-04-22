mod graph_builder;
mod model_registry;

pub use graph_builder::build_diagram_graph;

#[cfg(test)]
mod tests {
    use super::build_diagram_graph;
    use crate::extract::{AnalysisRequest, ModuleInput, analyze_request};
    use crate::protocol::diagnostics::DiagnosticCode;
    use std::fs;
    use std::path::PathBuf;

    #[test]
    fn builds_feature_rich_graph_snapshot() {
        let analyzer = analyze_request(&AnalysisRequest {
            modules: vec![
                fixture_module("accounts", "feature_rich_project/accounts/models.py"),
                fixture_module("blog", "feature_rich_project/blog/models.py"),
                fixture_module("taxonomy", "feature_rich_project/taxonomy/models.py"),
            ],
            workspace_root: fixture_root("feature_rich_project"),
        });

        let graph = build_diagram_graph(&analyzer);

        assert_eq!(
            graph.to_json().trim_end(),
            read_snapshot("phase5-feature-rich-graph.json").trim_end()
        );
    }

    #[test]
    fn builds_disconnected_graph_snapshot() {
        let analyzer = analyze_request(&AnalysisRequest {
            modules: vec![
                fixture_module("audit", "disconnected_project/audit/models.py"),
                fixture_module("crm", "disconnected_project/crm/models.py"),
                fixture_module("sales", "disconnected_project/sales/models.py"),
            ],
            workspace_root: fixture_root("disconnected_project"),
        });

        let graph = build_diagram_graph(&analyzer);

        assert_eq!(
            graph.to_json().trim_end(),
            read_snapshot("phase5-disconnected-graph.json").trim_end()
        );
    }

    #[test]
    fn adds_resolution_diagnostics_for_missing_relation_targets() {
        let analyzer = analyze_request(&AnalysisRequest {
            modules: vec![fixture_module(
                "orphan",
                "partial_reference_project/orphan/models.py",
            )],
            workspace_root: fixture_root("partial_reference_project"),
        });

        let graph = build_diagram_graph(&analyzer);

        assert_eq!(graph.nodes.len(), 1);
        assert!(graph.structural_edges.is_empty());
        assert!(
            graph
                .diagnostics
                .iter()
                .any(|diagnostic| diagnostic.code == DiagnosticCode::UnresolvedReference)
        );
    }

    #[test]
    fn keeps_method_associations_out_of_structural_edges() {
        let analyzer = analyze_request(&AnalysisRequest {
            modules: vec![
                fixture_module("accounts", "feature_rich_project/accounts/models.py"),
                fixture_module("blog", "feature_rich_project/blog/models.py"),
                fixture_module("taxonomy", "feature_rich_project/taxonomy/models.py"),
            ],
            workspace_root: fixture_root("feature_rich_project"),
        });

        let graph = build_diagram_graph(&analyzer);

        assert_eq!(graph.method_associations.len(), 3);
        assert_eq!(graph.structural_edges.len(), 4);
        assert!(graph.structural_edges.iter().all(|edge| edge.provenance
            != crate::protocol::graph::StructuralEdgeProvenance::Declared
            || edge.kind != crate::protocol::analysis::RelationKind::ReverseForeignKey));
    }

    fn fixture_module(app_label: &str, relative_path: &str) -> ModuleInput {
        ModuleInput {
            app_label: app_label.to_string(),
            file_path: PathBuf::from(env!("CARGO_MANIFEST_DIR"))
                .join("../test/fixtures/django")
                .join(relative_path),
        }
    }

    fn fixture_root(project_name: &str) -> PathBuf {
        PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../test/fixtures/django")
            .join(project_name)
    }

    fn read_snapshot(file_name: &str) -> String {
        fs::read_to_string(
            PathBuf::from(env!("CARGO_MANIFEST_DIR"))
                .join("../test/fixtures/analyzer-json")
                .join(file_name),
        )
        .expect("expected snapshot fixture")
    }
}
