use crate::extract::ModuleInput;
use crate::parser::python_module_parser::ParsedPythonModule;
use crate::protocol::model_identity::{CanonicalModelId, ModelIdentity};
use rustpython_parser::ast;
use std::collections::{BTreeMap, BTreeSet};
use std::path::Path;

#[derive(Debug, Clone)]
pub struct ModuleContext {
    pub app_label: String,
    pub module_path: String,
    imported_models: BTreeMap<String, CanonicalModelId>,
    same_module_models: BTreeMap<String, CanonicalModelId>,
}

impl ModuleContext {
    pub fn new(
        workspace_root: &Path,
        module: &ModuleInput,
        parsed: &ParsedPythonModule,
        known_model_ids: &BTreeSet<String>,
    ) -> Self {
        let same_module_models = parsed
            .statements()
            .iter()
            .filter_map(|statement| match statement {
                ast::Stmt::ClassDef(class_def) => {
                    let model_id =
                        CanonicalModelId::new(&module.app_label, class_def.name.as_str());
                    known_model_ids
                        .contains(model_id.as_str())
                        .then_some((class_def.name.to_string(), model_id))
                }
                _ => None,
            })
            .collect();

        Self {
            app_label: module.app_label.clone(),
            module_path: derive_module_path(workspace_root, module),
            imported_models: collect_imported_models(&module.app_label, parsed.statements()),
            same_module_models,
        }
    }

    pub fn model_identity(&self, model_name: &str) -> ModelIdentity {
        ModelIdentity {
            app_label: self.app_label.clone(),
            id: CanonicalModelId::new(&self.app_label, model_name),
            model_name: model_name.to_string(),
            module_path: Some(self.module_path.clone()),
        }
    }

    pub fn resolve_model_symbol(
        &self,
        symbol_name: &str,
        current_model_name: &str,
    ) -> Option<CanonicalModelId> {
        if let Some(model_id) = self.imported_models.get(symbol_name) {
            return Some(model_id.clone());
        }

        self.same_module_models
            .get(symbol_name)
            .filter(|model_id| {
                model_id.as_str()
                    != CanonicalModelId::new(&self.app_label, current_model_name).as_str()
            })
            .cloned()
    }
}

pub fn canonical_model_id_from_reference(
    current_app_label: &str,
    raw_reference: &str,
) -> Option<CanonicalModelId> {
    if raw_reference.is_empty() || raw_reference == "self" {
        return None;
    }

    let segments = raw_reference.split('.').collect::<Vec<_>>();
    match segments.as_slice() {
        [model_name] => Some(CanonicalModelId::new(current_app_label, model_name)),
        [app_label, model_name] => Some(CanonicalModelId::new(app_label, model_name)),
        _ => None,
    }
}

pub fn relation_app_label_hint(current_app_label: &str, raw_reference: &str) -> Option<String> {
    if raw_reference.is_empty() {
        return None;
    }

    let segments = raw_reference.split('.').collect::<Vec<_>>();
    match segments.as_slice() {
        [_] => Some(current_app_label.to_string()),
        [app_label, _] => Some((*app_label).to_string()),
        _ => None,
    }
}

fn collect_imported_models(
    current_app_label: &str,
    statements: &[ast::Stmt],
) -> BTreeMap<String, CanonicalModelId> {
    let mut imported_models = BTreeMap::new();

    for statement in statements {
        let ast::Stmt::ImportFrom(import_from) = statement else {
            continue;
        };

        let Some(app_label) = import_app_label(current_app_label, import_from) else {
            continue;
        };

        for alias in &import_from.names {
            let symbol_name = alias.asname.as_ref().unwrap_or(&alias.name).to_string();
            imported_models.insert(
                symbol_name,
                CanonicalModelId::new(&app_label, alias.name.as_str()),
            );
        }
    }

    imported_models
}

fn derive_module_path(workspace_root: &Path, module: &ModuleInput) -> String {
    let Ok(relative_path) = module.file_path.strip_prefix(workspace_root) else {
        return format!("{}.models", module.app_label);
    };

    let mut segments = relative_path
        .iter()
        .map(|component| component.to_string_lossy().to_string())
        .collect::<Vec<_>>();

    if segments.is_empty() {
        return format!("{}.models", module.app_label);
    }

    if let Some(last_segment) = segments.last_mut() {
        if last_segment == "__init__.py" {
            segments.pop();
        } else if last_segment.ends_with(".py") {
            *last_segment = last_segment.trim_end_matches(".py").to_string();
        }
    }

    if segments.is_empty() {
        return format!("{}.models", module.app_label);
    }

    segments.join(".")
}

fn import_app_label(current_app_label: &str, import_from: &ast::StmtImportFrom) -> Option<String> {
    if import_from
        .level
        .as_ref()
        .map(|level| level.to_u32())
        .unwrap_or(0)
        > 0
    {
        return Some(current_app_label.to_string());
    }

    let module_name = import_from.module.as_ref()?.as_str();
    module_name.split('.').next().map(str::to_string)
}
