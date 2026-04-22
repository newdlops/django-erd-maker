use crate::extract::ModuleInput;
use crate::extract::expression_helpers::attribute_path;
use crate::parser::python_module_parser::ParsedPythonModule;
use crate::protocol::model_identity::CanonicalModelId;
use rustpython_parser::ast;
use std::collections::{BTreeMap, BTreeSet};
use std::path::Path;

pub fn discover_project_model_ids(
    workspace_root: &Path,
    modules: &[(&ModuleInput, &ParsedPythonModule)],
) -> BTreeSet<String> {
    let catalogs = modules
        .iter()
        .map(|(module, parsed)| ModuleClassCatalog::new(workspace_root, module, parsed))
        .collect::<Vec<_>>();
    let resolver = ProjectImportResolver::new(&catalogs);

    let mut graph = ModelInheritanceGraph::default();
    for catalog in &catalogs {
        graph.add_catalog(catalog, &resolver);
    }

    graph.discover_model_ids()
}

#[derive(Debug, Default)]
struct ModelInheritanceGraph {
    descendant_model_ids_by_base_id: BTreeMap<String, BTreeSet<String>>,
    nodes: BTreeMap<String, ClassInheritanceNode>,
}

impl ModelInheritanceGraph {
    fn add_catalog(&mut self, catalog: &ModuleClassCatalog, resolver: &ProjectImportResolver) {
        let imports = collect_imports(catalog, resolver);

        for class_def in &catalog.class_defs {
            let canonical_model_id =
                CanonicalModelId::new(&catalog.app_label, class_def.name.as_str());
            let class_id = canonical_model_id.as_str().to_string();

            self.nodes.entry(class_id.clone()).or_default();

            for base in &class_def.bases {
                match catalog.resolve_base(base, resolver, &imports) {
                    Some(BaseResolution::DjangoModel) => {
                        self.nodes
                            .entry(class_id.clone())
                            .or_default()
                            .directly_extends_django_model = true;
                    }
                    Some(BaseResolution::ProjectClass(model_id)) => {
                        let base_id = model_id.as_str().to_string();
                        self.nodes
                            .entry(class_id.clone())
                            .or_default()
                            .base_model_ids
                            .insert(base_id.clone());
                        self.descendant_model_ids_by_base_id
                            .entry(base_id)
                            .or_default()
                            .insert(class_id.clone());
                    }
                    None => {}
                }
            }
        }
    }

    fn discover_model_ids(&self) -> BTreeSet<String> {
        let mut model_ids = BTreeSet::new();
        let mut pending = self
            .nodes
            .iter()
            .filter_map(|(class_id, node)| {
                node.directly_extends_django_model
                    .then_some(class_id.to_string())
            })
            .collect::<Vec<_>>();

        while let Some(model_id) = pending.pop() {
            if !model_ids.insert(model_id.clone()) {
                continue;
            }

            if let Some(descendants) = self.descendant_model_ids_by_base_id.get(&model_id) {
                pending.extend(descendants.iter().cloned());
            }
        }

        model_ids
    }
}

#[derive(Debug, Default)]
struct ClassInheritanceNode {
    directly_extends_django_model: bool,
    base_model_ids: BTreeSet<String>,
}

#[derive(Debug, Clone)]
enum BaseResolution {
    DjangoModel,
    ProjectClass(CanonicalModelId),
}

#[derive(Debug)]
struct ModuleClassCatalog {
    app_label: String,
    class_defs: Vec<ast::StmtClassDef>,
    is_package_module: bool,
    local_class_ids: BTreeMap<String, CanonicalModelId>,
    module_path: String,
    statements: Vec<ast::Stmt>,
}

impl ModuleClassCatalog {
    fn new(workspace_root: &Path, module: &ModuleInput, parsed: &ParsedPythonModule) -> Self {
        let class_defs = parsed
            .statements()
            .iter()
            .filter_map(|statement| match statement {
                ast::Stmt::ClassDef(class_def) => Some(class_def.clone()),
                _ => None,
            })
            .collect::<Vec<_>>();
        let local_class_ids = class_defs
            .iter()
            .map(|class_def| {
                (
                    class_def.name.to_string(),
                    CanonicalModelId::new(&module.app_label, class_def.name.as_str()),
                )
            })
            .collect();

        Self {
            app_label: module.app_label.clone(),
            class_defs,
            is_package_module: module
                .file_path
                .file_name()
                .is_some_and(|file_name| file_name == "__init__.py"),
            local_class_ids,
            module_path: derive_module_path(workspace_root, module),
            statements: parsed.statements().to_vec(),
        }
    }

    fn resolve_base(
        &self,
        base_expression: &ast::Expr,
        resolver: &ProjectImportResolver,
        imports: &ModuleImportCatalog,
    ) -> Option<BaseResolution> {
        let base_expression = unwrap_subscript_base(base_expression);
        let path = attribute_path(base_expression)?;

        if !path.contains('.') {
            return self
                .resolve_base_symbol(&path, imports)
                .or_else(|| (path == "Model").then_some(BaseResolution::DjangoModel));
        }

        if is_django_model_base_path(&path, imports) {
            return Some(BaseResolution::DjangoModel);
        }

        resolver
            .resolve_qualified_class_path(&path, imports)
            .map(BaseResolution::ProjectClass)
    }

    fn resolve_base_symbol(
        &self,
        symbol_name: &str,
        imports: &ModuleImportCatalog,
    ) -> Option<BaseResolution> {
        if let Some(local_model_id) = self.local_class_ids.get(symbol_name) {
            return Some(BaseResolution::ProjectClass(local_model_id.clone()));
        }

        imports.imported_base_symbols.get(symbol_name).cloned()
    }

    fn current_package_path(&self) -> String {
        if self.is_package_module {
            return self.module_path.clone();
        }

        self.module_path
            .rsplit_once('.')
            .map(|(package_path, _)| package_path.to_string())
            .unwrap_or_default()
    }
}

#[derive(Debug, Default)]
struct ProjectImportResolver {
    app_label_by_app_module_path: BTreeMap<String, String>,
    app_label_by_module_path: BTreeMap<String, String>,
    class_id_by_app_and_name: BTreeMap<String, CanonicalModelId>,
    class_id_by_module_and_name: BTreeMap<String, CanonicalModelId>,
    module_paths: BTreeSet<String>,
}

impl ProjectImportResolver {
    fn new(catalogs: &[ModuleClassCatalog]) -> Self {
        let mut resolver = Self::default();

        for catalog in catalogs {
            resolver
                .app_label_by_module_path
                .insert(catalog.module_path.clone(), catalog.app_label.clone());
            resolver.module_paths.insert(catalog.module_path.clone());

            if let Some(app_module_path) =
                infer_app_module_path(&catalog.module_path, &catalog.app_label)
            {
                resolver
                    .app_label_by_app_module_path
                    .insert(app_module_path, catalog.app_label.clone());
            }

            for (class_name, model_id) in &catalog.local_class_ids {
                resolver.class_id_by_app_and_name.insert(
                    app_class_key(&catalog.app_label, class_name),
                    model_id.clone(),
                );
                resolver.class_id_by_module_and_name.insert(
                    module_class_key(&catalog.module_path, class_name),
                    model_id.clone(),
                );
            }
        }

        resolver
    }

    fn resolve_imported_class(
        &self,
        module_path: &str,
        class_name: &str,
    ) -> Option<CanonicalModelId> {
        if let Some(model_id) = self
            .class_id_by_module_and_name
            .get(&module_class_key(module_path, class_name))
        {
            return Some(model_id.clone());
        }

        let app_label = self.resolve_app_label_for_module_path(module_path)?;
        self.class_id_by_app_and_name
            .get(&app_class_key(&app_label, class_name))
            .cloned()
            .or_else(|| Some(CanonicalModelId::new(&app_label, class_name)))
    }

    fn resolve_qualified_class_path(
        &self,
        path: &str,
        imports: &ModuleImportCatalog,
    ) -> Option<CanonicalModelId> {
        let (module_path, class_name) = imported_module_path_and_class_name(path, imports)?;
        self.resolve_imported_class(&module_path, &class_name)
    }

    fn resolve_app_label_for_module_path(&self, module_path: &str) -> Option<String> {
        if let Some(app_label) = self.app_label_by_module_path.get(module_path) {
            return Some(app_label.clone());
        }

        self.app_label_by_app_module_path
            .iter()
            .filter(|(app_module_path, _)| {
                module_path == app_module_path.as_str()
                    || module_path.starts_with(&format!("{app_module_path}."))
            })
            .max_by_key(|(app_module_path, _)| app_module_path.len())
            .map(|(_, app_label)| app_label.clone())
    }
}

#[derive(Debug, Default)]
struct ModuleImportCatalog {
    django_model_module_aliases: BTreeSet<String>,
    imported_base_symbols: BTreeMap<String, BaseResolution>,
    imported_module_paths: BTreeMap<String, String>,
}

fn collect_imports(
    catalog: &ModuleClassCatalog,
    resolver: &ProjectImportResolver,
) -> ModuleImportCatalog {
    let mut imports = ModuleImportCatalog::default();

    for statement in &catalog.statements {
        match statement {
            ast::Stmt::Import(import) => collect_import(import, &mut imports),
            ast::Stmt::ImportFrom(import_from) => {
                collect_import_from(catalog, import_from, resolver, &mut imports);
            }
            _ => {}
        }
    }

    imports
}

fn collect_import(import: &ast::StmtImport, imports: &mut ModuleImportCatalog) {
    for alias in &import.names {
        let imported_path = alias.name.as_str();
        let symbol_name = import_bound_symbol(alias);
        let bound_module_path = import_bound_module_path(alias);

        if is_django_model_module_path(imported_path) || is_django_module_path(imported_path) {
            imports
                .django_model_module_aliases
                .insert(symbol_name.clone());
        }

        imports
            .imported_module_paths
            .insert(symbol_name, bound_module_path);
    }
}

fn collect_import_from(
    catalog: &ModuleClassCatalog,
    import_from: &ast::StmtImportFrom,
    resolver: &ProjectImportResolver,
    imports: &mut ModuleImportCatalog,
) {
    let module_path = import_from_module_path(catalog, import_from);

    for alias in &import_from.names {
        if alias.name.as_str() == "*" {
            continue;
        }

        let symbol_name = alias.asname.as_ref().unwrap_or(&alias.name).to_string();

        if is_django_model_from_import(module_path.as_deref(), alias.name.as_str()) {
            imports
                .imported_base_symbols
                .insert(symbol_name, BaseResolution::DjangoModel);
            continue;
        }

        if is_django_model_module_from_import(module_path.as_deref(), alias.name.as_str()) {
            imports.django_model_module_aliases.insert(symbol_name);
            continue;
        }

        let Some(module_path) = module_path.as_deref() else {
            continue;
        };

        if let Some(model_id) = resolver.resolve_imported_class(module_path, alias.name.as_str()) {
            imports
                .imported_base_symbols
                .insert(symbol_name.clone(), BaseResolution::ProjectClass(model_id));
        }

        imports.imported_module_paths.insert(
            symbol_name,
            join_module_path(module_path, alias.name.as_str()),
        );
    }
}

fn imported_module_path_and_class_name(
    path: &str,
    imports: &ModuleImportCatalog,
) -> Option<(String, String)> {
    let segments = path.split('.').collect::<Vec<_>>();
    let class_name = segments.last()?.to_string();
    let module_segments = &segments[..segments.len().saturating_sub(1)];
    if module_segments.is_empty() {
        return None;
    }

    let module_path = if let Some(imported_module_path) = imports.imported_module_paths.get(
        *module_segments
            .first()
            .expect("module_segments is known to be non-empty"),
    ) {
        let suffix = module_segments
            .iter()
            .skip(1)
            .copied()
            .collect::<Vec<_>>()
            .join(".");
        join_optional_module_path(imported_module_path, &suffix)
    } else {
        module_segments.join(".")
    };

    Some((module_path, class_name))
}

fn import_from_module_path(
    catalog: &ModuleClassCatalog,
    import_from: &ast::StmtImportFrom,
) -> Option<String> {
    let level = import_from
        .level
        .as_ref()
        .map(|level| level.to_u32())
        .unwrap_or(0);
    let module_name = import_from.module.as_ref().map(|module| module.as_str());

    if level == 0 {
        return module_name.map(str::to_string);
    }

    let mut package_segments = catalog
        .current_package_path()
        .split('.')
        .filter(|segment| !segment.is_empty())
        .map(str::to_string)
        .collect::<Vec<_>>();

    for _ in 1..level {
        package_segments.pop();
    }

    if let Some(module_name) = module_name {
        package_segments.extend(module_name.split('.').map(str::to_string));
    }

    Some(package_segments.join("."))
}

fn is_django_model_base_path(path: &str, imports: &ModuleImportCatalog) -> bool {
    let segments = path.split('.').collect::<Vec<_>>();
    if segments.last().copied() != Some("Model") {
        return false;
    }

    matches!(segments.first().copied(), Some("django") | Some("models"))
        || segments
            .first()
            .is_some_and(|module_alias| imports.django_model_module_aliases.contains(*module_alias))
}

fn import_bound_symbol(alias: &ast::Alias) -> String {
    alias
        .asname
        .as_ref()
        .map(|asname| asname.to_string())
        .unwrap_or_else(|| {
            alias
                .name
                .as_str()
                .split('.')
                .next()
                .unwrap_or(alias.name.as_str())
                .to_string()
        })
}

fn import_bound_module_path(alias: &ast::Alias) -> String {
    alias
        .asname
        .as_ref()
        .map(|_| alias.name.to_string())
        .unwrap_or_else(|| {
            alias
                .name
                .as_str()
                .split('.')
                .next()
                .unwrap_or(alias.name.as_str())
                .to_string()
        })
}

fn is_django_model_from_import(module_path: Option<&str>, symbol_name: &str) -> bool {
    symbol_name == "Model" && module_path.is_some_and(is_django_model_module_path)
}

fn is_django_model_module_from_import(module_path: Option<&str>, symbol_name: &str) -> bool {
    symbol_name == "models" && matches!(module_path, Some("django.db"))
}

fn is_django_model_module_path(module_path: &str) -> bool {
    module_path == "django.db.models" || module_path.starts_with("django.db.models.")
}

fn is_django_module_path(module_path: &str) -> bool {
    module_path == "django" || module_path.starts_with("django.")
}

fn unwrap_subscript_base(expression: &ast::Expr) -> &ast::Expr {
    match expression {
        ast::Expr::Subscript(subscript) => &subscript.value,
        _ => expression,
    }
}

fn app_class_key(app_label: &str, class_name: &str) -> String {
    format!("{app_label}.{class_name}")
}

fn module_class_key(module_path: &str, class_name: &str) -> String {
    format!("{module_path}.{class_name}")
}

fn infer_app_module_path(module_path: &str, app_label: &str) -> Option<String> {
    let segments = module_path.split('.').collect::<Vec<_>>();
    let app_segment_index = segments.iter().position(|segment| *segment == app_label)?;
    Some(segments[..=app_segment_index].join("."))
}

fn join_module_path(module_path: &str, child_name: &str) -> String {
    join_optional_module_path(module_path, child_name)
}

fn join_optional_module_path(module_path: &str, suffix: &str) -> String {
    if module_path.is_empty() {
        return suffix.to_string();
    }
    if suffix.is_empty() {
        return module_path.to_string();
    }

    format!("{module_path}.{suffix}")
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
