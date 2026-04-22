use crate::extract::ModuleInput;
use crate::extract::diagnostics::unsupported_construct_diagnostic;
use crate::extract::expression_helpers::{
    expr_to_string, is_property_decorator, terminal_path_segment,
};
use crate::extract::field_extractor::{
    assignment_name_and_value, collect_choice_definitions, extract_field,
};
use crate::extract::method_extractor::{extract_async_method, extract_method};
use crate::extract::module_context::ModuleContext;
use crate::parser::python_module_parser::ParsedPythonModule;
use crate::protocol::analysis::{ExtractedModel, PropertyAttribute};
use crate::protocol::diagnostics::AnalyzerDiagnostic;
use rustpython_parser::ast;
use std::collections::{BTreeMap, BTreeSet};
use std::path::Path;

#[derive(Debug)]
pub struct ModuleExtractionResult {
    pub diagnostics: Vec<AnalyzerDiagnostic>,
    pub models: Vec<ExtractedModel>,
}

pub fn extract_models_from_module(
    workspace_root: &Path,
    module: &ModuleInput,
    parsed: &ParsedPythonModule,
    known_model_ids: &BTreeSet<String>,
) -> ModuleExtractionResult {
    let context = ModuleContext::new(workspace_root, module, parsed, known_model_ids);
    let mut diagnostics = Vec::new();
    let mut models = Vec::new();

    for statement in parsed.statements() {
        let ast::Stmt::ClassDef(class_def) = statement else {
            continue;
        };

        let model_id = context.model_identity(class_def.name.as_str()).id;
        if !known_model_ids.contains(model_id.as_str()) {
            continue;
        }
        if !is_database_backed_model(class_def) {
            continue;
        }

        let (model, model_diagnostics) = extract_model_from_class(&context, module, class_def);
        diagnostics.extend(model_diagnostics);
        models.push(model);
    }

    ModuleExtractionResult {
        diagnostics,
        models,
    }
}

fn extract_model_from_class(
    context: &ModuleContext,
    module: &ModuleInput,
    class_def: &ast::StmtClassDef,
) -> (ExtractedModel, Vec<AnalyzerDiagnostic>) {
    let identity = context.model_identity(class_def.name.as_str());
    let choice_definitions = collect_choice_definitions(class_def.body.as_slice());
    let explicit_table_name = explicit_database_table_name(class_def);
    let mut diagnostics = Vec::new();
    let mut fields = Vec::new();
    let mut relation_fields = BTreeMap::new();

    for statement in &class_def.body {
        let Some((field_name, value_expression)) = assignment_name_and_value(statement) else {
            continue;
        };
        let ast::Expr::Call(call) = value_expression else {
            continue;
        };

        if let Some((field, field_diagnostics)) = extract_field(
            module.file_path.as_path(),
            &identity.id,
            &context.app_label,
            field_name,
            call,
            &choice_definitions,
        ) {
            if let Some(relation) = &field.relation {
                relation_fields.insert(field.name.clone(), relation.target.clone());
            }

            diagnostics.extend(field_diagnostics);
            fields.push(field);
        } else if let Some(unsupported_member_name) = unsupported_model_member_name(call) {
            diagnostics.push(unsupported_construct_diagnostic(
                module.file_path.as_path(),
                field_name,
                format!(
                    "Model member '{field_name}' uses unsupported Django construct '{unsupported_member_name}'."
                ),
                Some(&identity.id),
            ));
        }
    }

    let mut methods = Vec::new();
    let mut properties = Vec::new();
    for statement in &class_def.body {
        match statement {
            ast::Stmt::FunctionDef(function_def) => {
                if function_def
                    .decorator_list
                    .iter()
                    .any(is_property_decorator)
                {
                    properties.push(PropertyAttribute {
                        name: function_def.name.to_string(),
                        return_type: function_def
                            .returns
                            .as_ref()
                            .map(|returns| expr_to_string(returns)),
                    });
                } else {
                    methods.push(extract_method(
                        context,
                        module.file_path.as_path(),
                        &identity.id,
                        class_def.name.as_str(),
                        function_def,
                        &relation_fields,
                    ));
                }
            }
            ast::Stmt::AsyncFunctionDef(function_def) => {
                methods.push(extract_async_method(
                    context,
                    module.file_path.as_path(),
                    &identity.id,
                    class_def.name.as_str(),
                    function_def,
                    &relation_fields,
                ));
            }
            _ => {}
        }
    }

    (
        ExtractedModel {
            database_table_name: database_table_name(
                &context.app_label,
                class_def,
                explicit_table_name.as_deref(),
            ),
            declared_base_classes: class_def.bases.iter().map(expr_to_string).collect(),
            fields,
            has_explicit_database_table_name: explicit_table_name.is_some(),
            identity,
            methods,
            properties,
        },
        diagnostics,
    )
}

fn database_table_name(
    app_label: &str,
    class_def: &ast::StmtClassDef,
    explicit_table_name: Option<&str>,
) -> String {
    explicit_table_name
        .map(str::to_string)
        .unwrap_or_else(|| format!("{}_{}", app_label, class_def.name.as_str().to_lowercase()))
}

fn explicit_database_table_name(class_def: &ast::StmtClassDef) -> Option<String> {
    let meta_class = meta_class(class_def)?;

    for statement in &meta_class.body {
        let Some((field_name, value_expression)) = assignment_name_and_value(statement) else {
            continue;
        };

        if field_name != "db_table" {
            continue;
        }

        if let ast::Expr::Constant(constant) = value_expression {
            if let rustpython_parser::ast::Constant::Str(value) = &constant.value {
                return Some(value.clone());
            }
        }
    }

    None
}

fn is_database_backed_model(class_def: &ast::StmtClassDef) -> bool {
    !meta_bool(class_def, "abstract").unwrap_or(false)
        && !meta_bool(class_def, "proxy").unwrap_or(false)
}

fn meta_bool(class_def: &ast::StmtClassDef, option_name: &str) -> Option<bool> {
    let meta_class = meta_class(class_def)?;

    for statement in &meta_class.body {
        let Some((field_name, value_expression)) = assignment_name_and_value(statement) else {
            continue;
        };

        if field_name != option_name {
            continue;
        }

        if let ast::Expr::Constant(constant) = value_expression {
            if let rustpython_parser::ast::Constant::Bool(value) = &constant.value {
                return Some(*value);
            }
        }
    }

    None
}

fn meta_class(class_def: &ast::StmtClassDef) -> Option<&ast::StmtClassDef> {
    class_def.body.iter().find_map(|statement| match statement {
        ast::Stmt::ClassDef(meta_class) if meta_class.name.as_str() == "Meta" => Some(meta_class),
        _ => None,
    })
}

fn unsupported_model_member_name(call: &ast::ExprCall) -> Option<String> {
    let member_name = terminal_path_segment(&call.func)?;

    match member_name.as_str() {
        "GenericForeignKey" | "GenericRelation" => Some(member_name),
        _ => None,
    }
}
