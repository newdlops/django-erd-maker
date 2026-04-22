use crate::extract::expression_helpers::walk_statements;
use crate::extract::module_context::{ModuleContext, canonical_model_id_from_reference};
use crate::protocol::analysis::{
    MethodAssociationConfidence, MethodRelatedModelReference, MethodVisibility,
    RelationTargetReference, UserMethod,
};
use crate::protocol::model_identity::CanonicalModelId;
use rustpython_parser::ast;
use std::collections::BTreeMap;
use std::path::Path;

pub fn extract_method(
    context: &ModuleContext,
    file_path: &Path,
    model_id: &CanonicalModelId,
    current_model_name: &str,
    function_def: &ast::StmtFunctionDef,
    relation_fields: &BTreeMap<String, RelationTargetReference>,
) -> UserMethod {
    UserMethod {
        name: function_def.name.to_string(),
        related_models: infer_method_related_models(
            context,
            file_path,
            model_id,
            current_model_name,
            &function_def.body,
            relation_fields,
        ),
        visibility: visibility_from_name(function_def.name.as_str()),
    }
}

pub fn extract_async_method(
    context: &ModuleContext,
    file_path: &Path,
    model_id: &CanonicalModelId,
    current_model_name: &str,
    function_def: &ast::StmtAsyncFunctionDef,
    relation_fields: &BTreeMap<String, RelationTargetReference>,
) -> UserMethod {
    UserMethod {
        name: function_def.name.to_string(),
        related_models: infer_method_related_models(
            context,
            file_path,
            model_id,
            current_model_name,
            &function_def.body,
            relation_fields,
        ),
        visibility: visibility_from_name(function_def.name.as_str()),
    }
}

fn infer_method_related_models(
    context: &ModuleContext,
    _file_path: &Path,
    _model_id: &CanonicalModelId,
    current_model_name: &str,
    statements: &[ast::Stmt],
    relation_fields: &BTreeMap<String, RelationTargetReference>,
) -> Vec<MethodRelatedModelReference> {
    let mut references = BTreeMap::new();

    walk_statements(statements, &mut |expression| {
        if let ast::Expr::Attribute(attribute) = expression {
            if matches!(attribute.value.as_ref(), ast::Expr::Name(name) if name.id.as_str() == "self")
            {
                let field_name = attribute.attr.as_str();
                if let Some(target) = relation_fields.get(field_name) {
                    let canonical_id = canonical_model_id_from_reference(
                        &context.app_label,
                        target.raw_reference.as_str(),
                    );
                    references
                        .entry(target.raw_reference.clone())
                        .or_insert_with(|| MethodRelatedModelReference {
                            confidence: MethodAssociationConfidence::High,
                            evidence: Some(format!("self.{field_name}")),
                            raw_reference: Some(target.raw_reference.clone()),
                            target_model_id: canonical_id,
                        });
                }
            }
        }

        if let ast::Expr::Name(name) = expression {
            if let Some(target_model_id) =
                context.resolve_model_symbol(name.id.as_str(), current_model_name)
            {
                references
                    .entry(target_model_id.as_str().to_string())
                    .or_insert_with(|| MethodRelatedModelReference {
                        confidence: MethodAssociationConfidence::Medium,
                        evidence: Some(name.id.to_string()),
                        raw_reference: Some(target_model_id.as_str().to_string()),
                        target_model_id: Some(target_model_id),
                    });
            }
        }
    });

    references.into_values().collect()
}

fn visibility_from_name(name: &str) -> MethodVisibility {
    if name.starts_with("__") {
        MethodVisibility::Private
    } else if name.starts_with('_') {
        MethodVisibility::Protected
    } else {
        MethodVisibility::Public
    }
}
