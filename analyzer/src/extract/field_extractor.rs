use crate::extract::diagnostics::{partial_inference_diagnostic, unresolved_reference_diagnostic};
use crate::extract::expression_helpers::{
    attribute_path, constant_string, constant_value_and_kind, expr_to_string, humanize_enum_label,
    keyword_value, name_target, terminal_path_segment,
};
use crate::extract::module_context::{canonical_model_id_from_reference, relation_app_label_hint};
use crate::protocol::analysis::{
    ChoiceFieldMetadata, ChoiceOption, FieldRelation, ModelField, ModelFieldPersistence,
    RelationKind, RelationTargetReference, ResolutionState,
};
use crate::protocol::diagnostics::AnalyzerDiagnostic;
use crate::protocol::model_identity::CanonicalModelId;
use rustpython_parser::ast::{self, Constant};
use std::collections::BTreeMap;
use std::path::Path;

pub fn assignment_name_and_value(statement: &ast::Stmt) -> Option<(&str, &ast::Expr)> {
    match statement {
        ast::Stmt::AnnAssign(assign) => {
            let field_name = name_target(&assign.target)?;
            let value = assign.value.as_deref()?;
            Some((field_name, value))
        }
        ast::Stmt::Assign(assign) => {
            if assign.targets.len() != 1 {
                return None;
            }

            let field_name = name_target(&assign.targets[0])?;
            Some((field_name, &assign.value))
        }
        _ => None,
    }
}

pub fn collect_choice_definitions(
    statements: &[ast::Stmt],
) -> BTreeMap<String, ChoiceFieldMetadata> {
    let mut definitions = BTreeMap::new();

    for statement in statements {
        let ast::Stmt::ClassDef(class_def) = statement else {
            continue;
        };

        if !is_choice_enum(class_def) {
            continue;
        }

        let mut options = Vec::new();
        let mut is_fully_resolved = true;

        for member_statement in &class_def.body {
            let Some((member_name, value_expression)) = assignment_name_and_value(member_statement)
            else {
                continue;
            };

            match extract_choice_option(member_name, value_expression) {
                Some(option) => options.push(option),
                None => is_fully_resolved = false,
            }
        }

        definitions.insert(
            class_def.name.to_string(),
            ChoiceFieldMetadata {
                is_choice_field: true,
                is_fully_resolved,
                options,
            },
        );
    }

    definitions
}

pub fn extract_field(
    file_path: &Path,
    model_id: &CanonicalModelId,
    current_app_label: &str,
    field_name: &str,
    call: &ast::ExprCall,
    choice_definitions: &BTreeMap<String, ChoiceFieldMetadata>,
) -> Option<(ModelField, Vec<AnalyzerDiagnostic>)> {
    let field_type = terminal_path_segment(&call.func)?;
    let relation_kind = relation_kind_from_field_type(&field_type);
    let is_supported_field = relation_kind.is_some() || field_type.ends_with("Field");
    if !is_supported_field {
        return None;
    }

    let mut diagnostics = Vec::new();
    let choice_metadata = extract_choice_metadata(
        file_path,
        model_id,
        field_name,
        call,
        choice_definitions,
        &mut diagnostics,
    );
    let relation = relation_kind.map(|kind| {
        extract_relation(
            file_path,
            model_id,
            current_app_label,
            field_name,
            kind,
            call,
            &mut diagnostics,
        )
    });

    Some((
        ModelField {
            choice_metadata,
            field_type,
            name: field_name.to_string(),
            nullable: keyword_value(&call.keywords, "null")
                .and_then(boolean_literal)
                .unwrap_or(false),
            persistence: ModelFieldPersistence::Stored,
            primary_key: keyword_value(&call.keywords, "primary_key")
                .and_then(boolean_literal)
                .unwrap_or(false),
            relation,
        },
        diagnostics,
    ))
}

fn extract_choice_metadata(
    file_path: &Path,
    model_id: &CanonicalModelId,
    field_name: &str,
    call: &ast::ExprCall,
    choice_definitions: &BTreeMap<String, ChoiceFieldMetadata>,
    diagnostics: &mut Vec<AnalyzerDiagnostic>,
) -> Option<ChoiceFieldMetadata> {
    let choices_expression = keyword_value(&call.keywords, "choices")?;

    if let Some(choice_metadata) = literal_choice_metadata(choices_expression) {
        return Some(choice_metadata);
    }

    if let Some(choice_metadata) =
        referenced_choice_metadata(choices_expression, choice_definitions)
    {
        return Some(choice_metadata);
    }

    diagnostics.push(partial_inference_diagnostic(
        file_path,
        field_name,
        format!("Could not fully infer choices for field '{field_name}'."),
        Some(model_id),
    ));

    Some(ChoiceFieldMetadata {
        is_choice_field: true,
        is_fully_resolved: false,
        options: Vec::new(),
    })
}

fn extract_relation(
    file_path: &Path,
    model_id: &CanonicalModelId,
    current_app_label: &str,
    field_name: &str,
    relation_kind: RelationKind,
    call: &ast::ExprCall,
    diagnostics: &mut Vec<AnalyzerDiagnostic>,
) -> FieldRelation {
    let target_expression = call
        .args
        .first()
        .or_else(|| keyword_value(&call.keywords, "to"));
    let (target, through_model_id) = if let Some(target_expression) = target_expression {
        (
            extract_relation_target_reference(
                file_path,
                model_id,
                current_app_label,
                field_name,
                target_expression,
                diagnostics,
            ),
            keyword_value(&call.keywords, "through")
                .and_then(relation_reference_from_expression)
                .and_then(|raw_reference| {
                    canonical_model_id_from_reference(current_app_label, &raw_reference)
                }),
        )
    } else {
        diagnostics.push(unresolved_reference_diagnostic(
            file_path,
            field_name,
            format!("Relation field '{field_name}' does not declare a target model."),
            Some(model_id),
        ));

        (
            RelationTargetReference {
                app_label_hint: None,
                raw_reference: String::new(),
                resolution_state: ResolutionState::Unresolved,
                resolved_model_id: None,
            },
            None,
        )
    };

    FieldRelation {
        kind: relation_kind,
        reverse_accessor_name: keyword_value(&call.keywords, "related_name")
            .and_then(constant_string),
        target,
        through_model_id,
    }
}

fn extract_relation_target_reference(
    file_path: &Path,
    model_id: &CanonicalModelId,
    current_app_label: &str,
    field_name: &str,
    expression: &ast::Expr,
    diagnostics: &mut Vec<AnalyzerDiagnostic>,
) -> RelationTargetReference {
    let Some(raw_reference) = relation_reference_from_expression(expression) else {
        diagnostics.push(unresolved_reference_diagnostic(
            file_path,
            field_name,
            format!("Could not statically resolve relation target for field '{field_name}'."),
            Some(model_id),
        ));

        return RelationTargetReference {
            app_label_hint: None,
            raw_reference: expr_to_string(expression),
            resolution_state: ResolutionState::Unresolved,
            resolved_model_id: None,
        };
    };

    let resolved_model_id = canonical_model_id_from_reference(current_app_label, &raw_reference);
    let resolution_state = if resolved_model_id.is_some() {
        ResolutionState::Deferred
    } else {
        diagnostics.push(unresolved_reference_diagnostic(
            file_path,
            field_name,
            format!(
                "Relation field '{field_name}' points to '{raw_reference}', which cannot be normalized yet."
            ),
            Some(model_id),
        ));
        ResolutionState::Unresolved
    };

    RelationTargetReference {
        app_label_hint: relation_app_label_hint(current_app_label, &raw_reference),
        raw_reference,
        resolution_state,
        resolved_model_id: None,
    }
}

fn extract_choice_option(member_name: &str, value_expression: &ast::Expr) -> Option<ChoiceOption> {
    if let Some((value, value_kind)) = constant_value_and_kind(value_expression) {
        return Some(ChoiceOption {
            label: humanize_enum_label(member_name),
            value,
            value_kind,
        });
    }

    let tuple_values = match value_expression {
        ast::Expr::Tuple(tuple) => &tuple.elts,
        ast::Expr::List(list) => &list.elts,
        _ => return None,
    };

    let value_expression = tuple_values.first()?;
    let (value, value_kind) = constant_value_and_kind(value_expression)?;
    let label = tuple_values
        .get(1)
        .and_then(constant_string)
        .unwrap_or_else(|| humanize_enum_label(member_name));

    Some(ChoiceOption {
        label,
        value,
        value_kind,
    })
}

fn is_choice_enum(class_def: &ast::StmtClassDef) -> bool {
    class_def.bases.iter().any(|base| {
        matches!(
            attribute_path(base).as_deref(),
            Some("Choices")
                | Some("models.Choices")
                | Some("TextChoices")
                | Some("models.TextChoices")
                | Some("IntegerChoices")
                | Some("models.IntegerChoices")
        )
    })
}

fn literal_choice_metadata(expression: &ast::Expr) -> Option<ChoiceFieldMetadata> {
    let options = match expression {
        ast::Expr::List(list) => list
            .elts
            .iter()
            .map(literal_choice_option)
            .collect::<Option<Vec<_>>>()?,
        ast::Expr::Tuple(tuple) => tuple
            .elts
            .iter()
            .map(literal_choice_option)
            .collect::<Option<Vec<_>>>()?,
        _ => return None,
    };

    Some(ChoiceFieldMetadata {
        is_choice_field: true,
        is_fully_resolved: true,
        options,
    })
}

fn literal_choice_option(expression: &ast::Expr) -> Option<ChoiceOption> {
    let pair = match expression {
        ast::Expr::List(list) => &list.elts,
        ast::Expr::Tuple(tuple) => &tuple.elts,
        _ => return None,
    };

    let value_expression = pair.first()?;
    let label_expression = pair.get(1)?;
    let (value, value_kind) = constant_value_and_kind(value_expression)?;
    let label = constant_string(label_expression)?;

    Some(ChoiceOption {
        label,
        value,
        value_kind,
    })
}

fn referenced_choice_metadata(
    expression: &ast::Expr,
    choice_definitions: &BTreeMap<String, ChoiceFieldMetadata>,
) -> Option<ChoiceFieldMetadata> {
    match expression {
        ast::Expr::Attribute(attribute) if attribute.attr.as_str() == "choices" => {
            let ast::Expr::Name(name) = attribute.value.as_ref() else {
                return None;
            };
            choice_definitions.get(name.id.as_str()).cloned()
        }
        ast::Expr::Name(name) => choice_definitions.get(name.id.as_str()).cloned(),
        _ => None,
    }
}

fn relation_kind_from_field_type(field_type: &str) -> Option<RelationKind> {
    match field_type {
        "ForeignKey" => Some(RelationKind::ForeignKey),
        "ManyToManyField" => Some(RelationKind::ManyToMany),
        "OneToOneField" => Some(RelationKind::OneToOne),
        _ => None,
    }
}

fn relation_reference_from_expression(expression: &ast::Expr) -> Option<String> {
    match expression {
        ast::Expr::Constant(constant) => match &constant.value {
            Constant::Str(value) => Some(value.clone()),
            _ => None,
        },
        ast::Expr::Name(name) => Some(name.id.to_string()),
        _ => None,
    }
}

fn boolean_literal(expression: &ast::Expr) -> Option<bool> {
    match expression {
        ast::Expr::Constant(constant) => match constant.value {
            Constant::Bool(value) => Some(value),
            _ => None,
        },
        _ => None,
    }
}
