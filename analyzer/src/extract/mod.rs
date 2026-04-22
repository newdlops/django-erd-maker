mod diagnostics;
mod expression_helpers;
mod field_extractor;
mod method_extractor;
mod model_catalog;
mod model_extractor;
mod module_context;

use crate::parser::python_module_parser::parse_python_module_file;
use crate::protocol::analysis::AnalyzerOutput;
use model_catalog::discover_project_model_ids;
use model_extractor::extract_models_from_module;
use std::path::PathBuf;
use std::time::Instant;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AnalysisRequest {
    pub modules: Vec<ModuleInput>,
    pub workspace_root: PathBuf,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ModuleInput {
    pub app_label: String,
    pub file_path: PathBuf,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct AnalysisMetrics {
    pub extract_ms: f64,
    pub parse_ms: f64,
}

pub fn analyze_request(request: &AnalysisRequest) -> AnalyzerOutput {
    analyze_request_with_metrics(request).0
}

pub fn analyze_request_with_metrics(
    request: &AnalysisRequest,
) -> (AnalyzerOutput, AnalysisMetrics) {
    let mut output = AnalyzerOutput::empty(&request.workspace_root.to_string_lossy());
    let mut parse_ms = 0.0;
    let mut extract_ms = 0.0;
    let mut parsed_modules = Vec::new();

    for module in &request.modules {
        let parse_started = Instant::now();
        match parse_python_module_file(&module.file_path) {
            Ok(parsed) => {
                parse_ms += elapsed_ms(parse_started);
                parsed_modules.push((module.clone(), parsed));
            }
            Err(diagnostics) => {
                parse_ms += elapsed_ms(parse_started);
                output.diagnostics.extend(diagnostics)
            }
        }
    }

    let known_model_ids = discover_project_model_ids(
        &request.workspace_root,
        &parsed_modules
            .iter()
            .map(|(module, parsed)| (module, parsed))
            .collect::<Vec<_>>(),
    );

    for (module, parsed) in &parsed_modules {
        let extract_started = Instant::now();
        let result =
            extract_models_from_module(&request.workspace_root, module, parsed, &known_model_ids);
        extract_ms += elapsed_ms(extract_started);
        output.models.extend(result.models);
        output.diagnostics.extend(result.diagnostics);
    }

    finalize_output(&mut output);
    (
        output,
        AnalysisMetrics {
            extract_ms,
            parse_ms,
        },
    )
}

fn finalize_output(output: &mut AnalyzerOutput) {
    output
        .models
        .sort_by(|left, right| left.identity.id.as_str().cmp(right.identity.id.as_str()));
    output.diagnostics.sort_by(|left, right| {
        let left_key = (
            left.location
                .as_ref()
                .map(|location| location.file_path.as_str())
                .unwrap_or_default(),
            left.message.as_str(),
        );
        let right_key = (
            right
                .location
                .as_ref()
                .map(|location| location.file_path.as_str())
                .unwrap_or_default(),
            right.message.as_str(),
        );

        left_key.cmp(&right_key)
    });
    output.summary.discovered_app_count = output
        .models
        .iter()
        .map(|model| model.identity.app_label.clone())
        .collect::<std::collections::BTreeSet<_>>()
        .len();
    output.summary.discovered_model_count = output.models.len();
    output.summary.diagnostic_count = output.diagnostics.len();
}

fn elapsed_ms(started: Instant) -> f64 {
    started.elapsed().as_secs_f64() * 1000.0
}

#[cfg(test)]
mod tests {
    use super::{AnalysisRequest, ModuleInput, analyze_request, finalize_output};
    use crate::parser::python_module_parser::parse_python_module_source;
    use crate::protocol::diagnostics::DiagnosticCode;
    use crate::protocol::model_identity::CanonicalModelId;
    use std::path::PathBuf;

    #[test]
    fn extracts_single_app_scalar_field_and_magic_method() {
        let workspace_root = fixture_root("single_app_project");
        let output = analyze_request(&AnalysisRequest {
            modules: vec![fixture_module("blog", "single_app_project/blog/models.py")],
            workspace_root,
        });

        assert!(output.diagnostics.is_empty());
        assert_eq!(output.summary.discovered_app_count, 1);
        assert_eq!(output.summary.discovered_model_count, 1);

        let model = &output.models[0];
        assert_eq!(model.identity.id.as_str(), "blog.Post");
        assert_eq!(model.identity.module_path.as_deref(), Some("blog.models"));
        assert_eq!(model.database_table_name, "blog_post");
        assert_eq!(model.declared_base_classes, vec!["models.Model"]);
        assert!(!model.has_explicit_database_table_name);
        assert_eq!(model.fields.len(), 1);
        assert_eq!(model.fields[0].name, "title");
        assert_eq!(model.fields[0].field_type, "CharField");
        assert_eq!(model.methods.len(), 1);
        assert_eq!(model.methods[0].name, "__str__");
    }

    #[test]
    fn extracts_relation_fields_choices_properties_and_methods() {
        let workspace_root = fixture_root("feature_rich_project");
        let output = analyze_request(&AnalysisRequest {
            modules: vec![
                fixture_module("accounts", "feature_rich_project/accounts/models.py"),
                fixture_module("blog", "feature_rich_project/blog/models.py"),
                fixture_module("taxonomy", "feature_rich_project/taxonomy/models.py"),
            ],
            workspace_root,
        });

        assert!(output.diagnostics.is_empty());
        assert_eq!(output.summary.discovered_model_count, 3);

        let author = output
            .models
            .iter()
            .find(|model| model.identity.id.as_str() == "accounts.Author")
            .expect("expected author model");
        assert_eq!(author.properties[0].name, "handle");
        assert_eq!(author.methods[0].name, "featured_posts");
        assert_eq!(
            author.methods[0].related_models[0]
                .target_model_id
                .as_ref()
                .map(CanonicalModelId::as_str),
            Some("blog.Post")
        );

        let post = output
            .models
            .iter()
            .find(|model| model.identity.id.as_str() == "blog.Post")
            .expect("expected post model");
        assert_eq!(post.fields.len(), 4);
        assert_eq!(post.properties[0].name, "display_title");
        assert_eq!(post.properties[0].return_type.as_deref(), Some("str"));

        let status = post
            .fields
            .iter()
            .find(|field| field.name == "status")
            .expect("expected status field");
        let choice_metadata = status
            .choice_metadata
            .as_ref()
            .expect("expected choice metadata");
        assert!(choice_metadata.is_choice_field);
        assert!(choice_metadata.is_fully_resolved);
        assert_eq!(choice_metadata.options.len(), 2);
        assert_eq!(choice_metadata.options[0].value, "draft");
        assert_eq!(choice_metadata.options[0].label, "Draft");

        let author_field = post
            .fields
            .iter()
            .find(|field| field.name == "author")
            .expect("expected author field");
        assert_eq!(
            author_field
                .relation
                .as_ref()
                .expect("expected author relation")
                .target
                .raw_reference,
            "accounts.Author"
        );

        let publish_method = post
            .methods
            .iter()
            .find(|method| method.name == "publish")
            .expect("expected publish method");
        assert_eq!(
            publish_method.related_models[0]
                .target_model_id
                .as_ref()
                .map(CanonicalModelId::as_str),
            Some("accounts.Author")
        );
    }

    #[test]
    fn extracts_one_to_one_and_preserves_deferred_string_targets() {
        let workspace_root = fixture_root("multi_app_project");
        let output = analyze_request(&AnalysisRequest {
            modules: vec![fixture_module(
                "accounts",
                "multi_app_project/accounts/models.py",
            )],
            workspace_root,
        });

        assert!(output.diagnostics.is_empty());
        assert_eq!(output.summary.discovered_model_count, 2);

        let profile = output
            .models
            .iter()
            .find(|model| model.identity.id.as_str() == "accounts.Profile")
            .expect("expected profile model");
        let author_field = profile
            .fields
            .iter()
            .find(|field| field.name == "author")
            .expect("expected author field");
        assert_eq!(
            author_field
                .relation
                .as_ref()
                .expect("expected relation")
                .kind,
            crate::protocol::analysis::RelationKind::OneToOne
        );

        let partial_root = fixture_root("partial_reference_project");
        let partial_output = analyze_request(&AnalysisRequest {
            modules: vec![fixture_module(
                "orphan",
                "partial_reference_project/orphan/models.py",
            )],
            workspace_root: partial_root,
        });
        let comment = &partial_output.models[0];
        assert_eq!(
            comment.fields[0]
                .relation
                .as_ref()
                .expect("expected relation")
                .target
                .raw_reference,
            "accounts.MissingAuthor"
        );
    }

    #[test]
    fn discovers_project_wide_model_modules_and_transitive_model_subclasses() {
        let workspace_root = fixture_root("project_wide_scan_project");
        let output = analyze_request(&AnalysisRequest {
            modules: vec![
                fixture_module("catalog", "project_wide_scan_project/catalog/apps.py"),
                fixture_module("catalog", "project_wide_scan_project/catalog/base.py"),
                fixture_module("catalog", "project_wide_scan_project/catalog/entities.py"),
                fixture_module("project", "project_wide_scan_project/project/settings.py"),
            ],
            workspace_root,
        });

        assert!(output.diagnostics.is_empty());
        assert_eq!(output.summary.discovered_app_count, 1);
        assert_eq!(output.summary.discovered_model_count, 2);
        assert!(
            output
                .models
                .iter()
                .any(|model| model.identity.id.as_str() == "catalog.BaseRecord")
        );
        let product = output
            .models
            .iter()
            .find(|model| model.identity.id.as_str() == "catalog.Product")
            .expect("expected product model");
        assert_eq!(product.database_table_name, "catalog_product_entity");
        assert!(product.has_explicit_database_table_name);
    }

    #[test]
    fn discovers_model_descendants_through_qualified_bases_and_model_aliases() {
        let workspace_root = PathBuf::from("/virtual/workspace");
        let output = analyze_sources(
            &workspace_root,
            vec![
                (
                    ModuleInput {
                        app_label: "inventory".to_string(),
                        file_path: workspace_root.join("inventory/base.py"),
                    },
                    r#"from django.db import models
from django.db.models import Model as DjangoModel

class Timestamped(DjangoModel):
    created_at = models.DateTimeField()

    class Meta:
        abstract = True

class SoftDeleteRecord(Timestamped):
    deleted_at = models.DateTimeField(null=True)

    class Meta:
        abstract = True
"#,
                ),
                (
                    ModuleInput {
                        app_label: "inventory".to_string(),
                        file_path: workspace_root.join("inventory/entities.py"),
                    },
                    r#"from django.db import models
from . import base

class StockItem(base.SoftDeleteRecord):
    sku = models.CharField(max_length=32)

class FeaturedStockItem(StockItem):
    badge = models.CharField(max_length=16)
"#,
                ),
            ],
        );

        assert!(output.diagnostics.is_empty());
        assert_eq!(output.summary.discovered_app_count, 1);
        assert_eq!(output.summary.discovered_model_count, 2);
        assert!(
            output
                .models
                .iter()
                .any(|model| model.identity.id.as_str() == "inventory.StockItem")
        );
        assert!(
            output
                .models
                .iter()
                .any(|model| model.identity.id.as_str() == "inventory.FeaturedStockItem")
        );
    }

    #[test]
    fn discovers_descendants_through_package_prefixed_absolute_imports() {
        let workspace_root = PathBuf::from("/virtual/workspace");
        let output = analyze_sources(
            &workspace_root,
            vec![
                (
                    ModuleInput {
                        app_label: "db".to_string(),
                        file_path: workspace_root.join("zuzu/db/model_base.py"),
                    },
                    r#"from django.db import models

class TimestampedModel(models.Model):
    created_at = models.DateTimeField()

    class Meta:
        abstract = True
"#,
                ),
                (
                    ModuleInput {
                        app_label: "db".to_string(),
                        file_path: workspace_root.join("zuzu/db/event_base.py"),
                    },
                    r#"from zuzu.db import model_base

class EventTraceable(model_base.TimestampedModel):
    trace_id = "not a model field"

    class Meta:
        abstract = True
"#,
                ),
                (
                    ModuleInput {
                        app_label: "db".to_string(),
                        file_path: workspace_root.join("zuzu/db/entities.py"),
                    },
                    r#"from django.db import models
from zuzu.db import event_base as events

class Safe(events.EventTraceable):
    amount = models.IntegerField()

class SecondarySafe(Safe):
    memo = models.CharField(max_length=32)
"#,
                ),
            ],
        );

        assert!(output.diagnostics.is_empty());
        assert_eq!(output.summary.discovered_app_count, 1);
        assert_eq!(output.summary.discovered_model_count, 2);
        assert!(
            output
                .models
                .iter()
                .any(|model| model.identity.id.as_str() == "db.Safe")
        );
        assert!(
            output
                .models
                .iter()
                .any(|model| model.identity.id.as_str() == "db.SecondarySafe")
        );
    }

    #[test]
    fn skips_non_database_backed_model_subclasses() {
        let workspace_root = PathBuf::from("/virtual/workspace");
        let output = analyze_source(
            &workspace_root,
            ModuleInput {
                app_label: "inventory".to_string(),
                file_path: workspace_root.join("inventory/models.py"),
            },
            r#"from django.db import models
class AbstractRecord(models.Model):
    class Meta:
        abstract = True
class StockItem(AbstractRecord):
    sku = models.CharField(max_length=32)
class StockItemProxy(StockItem):
    class Meta:
        proxy = True
"#,
        );

        assert_eq!(output.summary.discovered_model_count, 1);
        assert_eq!(output.models[0].identity.id.as_str(), "inventory.StockItem");
        assert_eq!(output.models[0].database_table_name, "inventory_stockitem");
    }

    #[test]
    fn emits_diagnostics_for_dynamic_relation_targets_and_choices() {
        let workspace_root = PathBuf::from("/virtual/workspace");
        let output = analyze_source(
            &workspace_root,
            ModuleInput {
                app_label: "dynamic".to_string(),
                file_path: workspace_root.join("dynamic/models.py"),
            },
            r#"
from django.conf import settings
from django.db import models

def build_choices():
    return [("draft", "Draft")]

class Comment(models.Model):
    owner = models.ForeignKey(settings.AUTH_USER_MODEL, on_delete=models.CASCADE)
    status = models.CharField(choices=build_choices(), max_length=20)
"#,
        );

        assert_eq!(output.summary.discovered_model_count, 1);
        assert_eq!(output.diagnostics.len(), 2);
        assert!(
            output
                .diagnostics
                .iter()
                .any(|diagnostic| diagnostic.code == DiagnosticCode::PartialInference)
        );
        assert!(
            output
                .diagnostics
                .iter()
                .any(|diagnostic| diagnostic.code == DiagnosticCode::UnresolvedReference)
        );

        let comment = &output.models[0];
        let owner = comment
            .fields
            .iter()
            .find(|field| field.name == "owner")
            .expect("expected owner field");
        assert_eq!(
            owner
                .relation
                .as_ref()
                .expect("expected relation")
                .target
                .resolution_state,
            crate::protocol::analysis::ResolutionState::Unresolved
        );

        let status = comment
            .fields
            .iter()
            .find(|field| field.name == "status")
            .expect("expected status field");
        assert!(
            !status
                .choice_metadata
                .as_ref()
                .expect("expected choice metadata")
                .is_fully_resolved
        );
    }

    #[test]
    fn emits_unsupported_construct_diagnostic_for_generic_foreign_key() {
        let workspace_root = PathBuf::from("/virtual/workspace");
        let output = analyze_source(
            &workspace_root,
            ModuleInput {
                app_label: "audit".to_string(),
                file_path: workspace_root.join("audit/models.py"),
            },
            r#"
from django.contrib.contenttypes.fields import GenericForeignKey
from django.db import models

class AuditEvent(models.Model):
    content_object = GenericForeignKey("content_type", "object_id")
"#,
        );

        assert_eq!(output.summary.discovered_model_count, 1);
        assert!(
            output
                .diagnostics
                .iter()
                .any(|diagnostic| diagnostic.code == DiagnosticCode::UnsupportedConstruct)
        );
    }

    #[test]
    fn serializes_analyzer_output_using_contract_shape() {
        let workspace_root = fixture_root("single_app_project");
        let output = analyze_request(&AnalysisRequest {
            modules: vec![fixture_module("blog", "single_app_project/blog/models.py")],
            workspace_root,
        });

        let json = output.to_json();

        assert!(json.contains("\"contractVersion\": \"1.0\""));
        assert!(json.contains("\"declaredBaseClasses\": ["));
        assert!(json.contains("\"databaseTableName\": \"blog_post\""));
        assert!(json.contains("\"discoveredModelCount\": 1"));
        assert!(json.contains("\"fieldType\": \"CharField\""));
    }

    fn analyze_source(
        workspace_root: &PathBuf,
        module: ModuleInput,
        source: &str,
    ) -> crate::protocol::analysis::AnalyzerOutput {
        analyze_sources(workspace_root, vec![(module, source)])
    }

    fn analyze_sources(
        workspace_root: &PathBuf,
        modules: Vec<(ModuleInput, &str)>,
    ) -> crate::protocol::analysis::AnalyzerOutput {
        let parsed_modules = modules
            .iter()
            .map(|(module, source)| {
                (
                    module.clone(),
                    parse_python_module_source(source, &module.file_path)
                        .expect("expected valid source"),
                )
            })
            .collect::<Vec<_>>();
        let mut output =
            crate::protocol::analysis::AnalyzerOutput::empty(&workspace_root.to_string_lossy());
        let known_model_ids = crate::extract::model_catalog::discover_project_model_ids(
            workspace_root,
            &parsed_modules
                .iter()
                .map(|(module, parsed)| (module, parsed))
                .collect::<Vec<_>>(),
        );

        for (module, parsed) in &parsed_modules {
            let result = crate::extract::model_extractor::extract_models_from_module(
                workspace_root,
                module,
                parsed,
                &known_model_ids,
            );
            output.models.extend(result.models);
            output.diagnostics.extend(result.diagnostics);
        }

        finalize_output(&mut output);
        output
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
}
