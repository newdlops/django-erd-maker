use rustpython_parser::ast;

#[derive(Debug)]
pub struct PythonModuleAst {
    suite: ast::Suite,
}

impl PythonModuleAst {
    pub fn new(suite: ast::Suite) -> Self {
        Self { suite }
    }

    pub fn statements(&self) -> &[ast::Stmt] {
        &self.suite
    }

    pub fn class_defs(&self) -> Vec<PythonClassDef<'_>> {
        self.suite
            .iter()
            .filter_map(|statement| match statement {
                ast::Stmt::ClassDef(class_def) => Some(PythonClassDef { class_def }),
                _ => None,
            })
            .collect()
    }

    pub fn statement_count(&self) -> usize {
        self.suite.len()
    }

    pub fn top_level_class_names(&self) -> Vec<String> {
        self.class_defs()
            .into_iter()
            .map(|class_def| class_def.name().to_string())
            .collect()
    }

    pub fn top_level_function_names(&self) -> Vec<String> {
        self.suite
            .iter()
            .filter_map(|statement| match statement {
                ast::Stmt::FunctionDef(function_def) => Some(function_def.name.as_str()),
                ast::Stmt::AsyncFunctionDef(function_def) => Some(function_def.name.as_str()),
                _ => None,
            })
            .map(str::to_string)
            .collect()
    }
}

pub struct PythonClassDef<'a> {
    class_def: &'a ast::StmtClassDef,
}

impl<'a> PythonClassDef<'a> {
    pub fn body_statement_count(&self) -> usize {
        self.class_def.body.len()
    }

    pub fn bases(&self) -> &[ast::Expr] {
        &self.class_def.bases
    }

    pub fn body(&self) -> &[ast::Stmt] {
        &self.class_def.body
    }

    pub fn name(&self) -> &str {
        self.class_def.name.as_str()
    }
}

#[cfg(test)]
mod tests {
    use super::PythonModuleAst;
    use rustpython_parser::{Parse, ast};

    #[test]
    fn extracts_top_level_class_names() {
        let suite = ast::Suite::parse(
            "class Post:\n    pass\n\ndef helper():\n    return 1\n",
            "<test>",
        )
        .expect("expected valid python source");

        let module = PythonModuleAst::new(suite);

        assert_eq!(module.statement_count(), 2);
        assert_eq!(module.top_level_class_names(), vec!["Post"]);
        assert_eq!(module.top_level_function_names(), vec!["helper"]);
        assert_eq!(module.class_defs()[0].body_statement_count(), 1);
    }
}
