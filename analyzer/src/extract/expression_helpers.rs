use crate::protocol::analysis::ChoiceValueKind;
use rustpython_parser::ast::{self, Constant};

pub fn attribute_path(expression: &ast::Expr) -> Option<String> {
    match expression {
        ast::Expr::Attribute(attribute) => {
            let prefix = attribute_path(&attribute.value)?;
            Some(format!("{prefix}.{}", attribute.attr))
        }
        ast::Expr::Name(name) => Some(name.id.to_string()),
        _ => None,
    }
}

pub fn constant_string(expression: &ast::Expr) -> Option<String> {
    match expression {
        ast::Expr::Constant(constant) => match &constant.value {
            Constant::Str(value) => Some(value.clone()),
            _ => None,
        },
        _ => None,
    }
}

pub fn constant_value_and_kind(expression: &ast::Expr) -> Option<(String, ChoiceValueKind)> {
    match expression {
        ast::Expr::Constant(constant) => match &constant.value {
            Constant::Bool(value) => Some((value.to_string(), ChoiceValueKind::Boolean)),
            Constant::Float(value) => Some((value.to_string(), ChoiceValueKind::Number)),
            Constant::Int(value) => Some((value.to_string(), ChoiceValueKind::Number)),
            Constant::None => Some(("null".to_string(), ChoiceValueKind::Null)),
            Constant::Str(value) => Some((value.clone(), ChoiceValueKind::String)),
            _ => None,
        },
        _ => None,
    }
}

pub fn expr_to_string(expression: &ast::Expr) -> String {
    if let Some(path) = attribute_path(expression) {
        return path;
    }

    match expression {
        ast::Expr::Call(call) => format!("{}(...)", expr_to_string(&call.func)),
        ast::Expr::Constant(constant) => match &constant.value {
            Constant::Bool(value) => value.to_string(),
            Constant::Float(value) => value.to_string(),
            Constant::Int(value) => value.to_string(),
            Constant::None => "None".to_string(),
            Constant::Str(value) => value.clone(),
            _ => "<constant>".to_string(),
        },
        ast::Expr::List(list) => format!(
            "[{}]",
            list.elts
                .iter()
                .map(expr_to_string)
                .collect::<Vec<_>>()
                .join(", ")
        ),
        ast::Expr::Subscript(subscript) => {
            format!(
                "{}[{}]",
                expr_to_string(&subscript.value),
                expr_to_string(&subscript.slice)
            )
        }
        ast::Expr::Tuple(tuple) => format!(
            "({})",
            tuple
                .elts
                .iter()
                .map(expr_to_string)
                .collect::<Vec<_>>()
                .join(", ")
        ),
        _ => "<expression>".to_string(),
    }
}

pub fn humanize_enum_label(symbol_name: &str) -> String {
    symbol_name
        .split('_')
        .filter(|segment| !segment.is_empty())
        .map(|segment| {
            let mut chars = segment.chars();
            match chars.next() {
                Some(first) => {
                    let first = first.to_uppercase().to_string();
                    let rest = chars.as_str().to_lowercase();
                    format!("{first}{rest}")
                }
                None => String::new(),
            }
        })
        .collect::<Vec<_>>()
        .join(" ")
}

pub fn is_property_decorator(expression: &ast::Expr) -> bool {
    matches!(attribute_path(expression).as_deref(), Some("property"))
}

pub fn keyword_value<'a>(keywords: &'a [ast::Keyword], name: &str) -> Option<&'a ast::Expr> {
    keywords
        .iter()
        .find(|keyword| keyword.arg.as_ref().map(|arg| arg.as_str()) == Some(name))
        .map(|keyword| &keyword.value)
}

pub fn name_target(expression: &ast::Expr) -> Option<&str> {
    match expression {
        ast::Expr::Name(name) => Some(name.id.as_str()),
        _ => None,
    }
}

pub fn terminal_path_segment(expression: &ast::Expr) -> Option<String> {
    attribute_path(expression).and_then(|path| path.rsplit('.').next().map(str::to_string))
}

pub fn walk_expression(expression: &ast::Expr, visitor: &mut impl FnMut(&ast::Expr)) {
    visitor(expression);

    match expression {
        ast::Expr::Attribute(attribute) => walk_expression(&attribute.value, visitor),
        ast::Expr::Await(await_expr) => walk_expression(&await_expr.value, visitor),
        ast::Expr::BinOp(bin_op) => {
            walk_expression(&bin_op.left, visitor);
            walk_expression(&bin_op.right, visitor);
        }
        ast::Expr::BoolOp(bool_op) => {
            for value in &bool_op.values {
                walk_expression(value, visitor);
            }
        }
        ast::Expr::Call(call) => {
            walk_expression(&call.func, visitor);
            for argument in &call.args {
                walk_expression(argument, visitor);
            }
            for keyword in &call.keywords {
                walk_expression(&keyword.value, visitor);
            }
        }
        ast::Expr::Compare(compare) => {
            walk_expression(&compare.left, visitor);
            for comparator in &compare.comparators {
                walk_expression(comparator, visitor);
            }
        }
        ast::Expr::Dict(dict) => {
            for key in dict.keys.iter().flatten() {
                walk_expression(key, visitor);
            }
            for value in &dict.values {
                walk_expression(value, visitor);
            }
        }
        ast::Expr::FormattedValue(formatted) => {
            walk_expression(&formatted.value, visitor);
            if let Some(format_spec) = &formatted.format_spec {
                walk_expression(format_spec, visitor);
            }
        }
        ast::Expr::GeneratorExp(generator) => {
            walk_expression(&generator.elt, visitor);
            for comprehension in &generator.generators {
                walk_expression(&comprehension.target, visitor);
                walk_expression(&comprehension.iter, visitor);
                for guard in &comprehension.ifs {
                    walk_expression(guard, visitor);
                }
            }
        }
        ast::Expr::ListComp(generator) => {
            walk_expression(&generator.elt, visitor);
            for comprehension in &generator.generators {
                walk_expression(&comprehension.target, visitor);
                walk_expression(&comprehension.iter, visitor);
                for guard in &comprehension.ifs {
                    walk_expression(guard, visitor);
                }
            }
        }
        ast::Expr::SetComp(generator) => {
            walk_expression(&generator.elt, visitor);
            for comprehension in &generator.generators {
                walk_expression(&comprehension.target, visitor);
                walk_expression(&comprehension.iter, visitor);
                for guard in &comprehension.ifs {
                    walk_expression(guard, visitor);
                }
            }
        }
        ast::Expr::DictComp(dict_comp) => {
            walk_expression(&dict_comp.key, visitor);
            walk_expression(&dict_comp.value, visitor);
            for comprehension in &dict_comp.generators {
                walk_expression(&comprehension.target, visitor);
                walk_expression(&comprehension.iter, visitor);
                for guard in &comprehension.ifs {
                    walk_expression(guard, visitor);
                }
            }
        }
        ast::Expr::IfExp(if_expr) => {
            walk_expression(&if_expr.test, visitor);
            walk_expression(&if_expr.body, visitor);
            walk_expression(&if_expr.orelse, visitor);
        }
        ast::Expr::JoinedStr(joined) => {
            for value in &joined.values {
                walk_expression(value, visitor);
            }
        }
        ast::Expr::Lambda(lambda) => walk_expression(&lambda.body, visitor),
        ast::Expr::List(list) => {
            for element in &list.elts {
                walk_expression(element, visitor);
            }
        }
        ast::Expr::NamedExpr(named) => {
            walk_expression(&named.target, visitor);
            walk_expression(&named.value, visitor);
        }
        ast::Expr::Set(set) => {
            for element in &set.elts {
                walk_expression(element, visitor);
            }
        }
        ast::Expr::Slice(slice) => {
            if let Some(lower) = &slice.lower {
                walk_expression(lower, visitor);
            }
            if let Some(upper) = &slice.upper {
                walk_expression(upper, visitor);
            }
            if let Some(step) = &slice.step {
                walk_expression(step, visitor);
            }
        }
        ast::Expr::Starred(starred) => walk_expression(&starred.value, visitor),
        ast::Expr::Subscript(subscript) => {
            walk_expression(&subscript.value, visitor);
            walk_expression(&subscript.slice, visitor);
        }
        ast::Expr::Tuple(tuple) => {
            for element in &tuple.elts {
                walk_expression(element, visitor);
            }
        }
        ast::Expr::UnaryOp(unary_op) => walk_expression(&unary_op.operand, visitor),
        ast::Expr::Yield(yield_expr) => {
            if let Some(value) = &yield_expr.value {
                walk_expression(value, visitor);
            }
        }
        ast::Expr::YieldFrom(yield_from) => walk_expression(&yield_from.value, visitor),
        ast::Expr::Constant(_) | ast::Expr::Name(_) => {}
    }
}

pub fn walk_statements(statements: &[ast::Stmt], visitor: &mut impl FnMut(&ast::Expr)) {
    for statement in statements {
        walk_statement(statement, visitor);
    }
}

fn walk_statement(statement: &ast::Stmt, visitor: &mut impl FnMut(&ast::Expr)) {
    match statement {
        ast::Stmt::AnnAssign(assign) => {
            walk_expression(&assign.target, visitor);
            walk_expression(&assign.annotation, visitor);
            if let Some(value) = &assign.value {
                walk_expression(value, visitor);
            }
        }
        ast::Stmt::Assert(assert_stmt) => {
            walk_expression(&assert_stmt.test, visitor);
            if let Some(message) = &assert_stmt.msg {
                walk_expression(message, visitor);
            }
        }
        ast::Stmt::Assign(assign) => {
            for target in &assign.targets {
                walk_expression(target, visitor);
            }
            walk_expression(&assign.value, visitor);
        }
        ast::Stmt::AsyncFor(for_stmt) => {
            walk_expression(&for_stmt.target, visitor);
            walk_expression(&for_stmt.iter, visitor);
            walk_statements(&for_stmt.body, visitor);
            walk_statements(&for_stmt.orelse, visitor);
        }
        ast::Stmt::For(for_stmt) => {
            walk_expression(&for_stmt.target, visitor);
            walk_expression(&for_stmt.iter, visitor);
            walk_statements(&for_stmt.body, visitor);
            walk_statements(&for_stmt.orelse, visitor);
        }
        ast::Stmt::AsyncFunctionDef(function) => {
            for decorator in &function.decorator_list {
                walk_expression(decorator, visitor);
            }
            if let Some(returns) = &function.returns {
                walk_expression(returns, visitor);
            }
            walk_statements(&function.body, visitor);
        }
        ast::Stmt::FunctionDef(function) => {
            for decorator in &function.decorator_list {
                walk_expression(decorator, visitor);
            }
            if let Some(returns) = &function.returns {
                walk_expression(returns, visitor);
            }
            walk_statements(&function.body, visitor);
        }
        ast::Stmt::AsyncWith(with_stmt) => {
            for item in &with_stmt.items {
                walk_expression(&item.context_expr, visitor);
                if let Some(optional_vars) = &item.optional_vars {
                    walk_expression(optional_vars, visitor);
                }
            }
            walk_statements(&with_stmt.body, visitor);
        }
        ast::Stmt::With(with_stmt) => {
            for item in &with_stmt.items {
                walk_expression(&item.context_expr, visitor);
                if let Some(optional_vars) = &item.optional_vars {
                    walk_expression(optional_vars, visitor);
                }
            }
            walk_statements(&with_stmt.body, visitor);
        }
        ast::Stmt::ClassDef(class_def) => {
            for base in &class_def.bases {
                walk_expression(base, visitor);
            }
            for keyword in &class_def.keywords {
                walk_expression(&keyword.value, visitor);
            }
            for decorator in &class_def.decorator_list {
                walk_expression(decorator, visitor);
            }
            walk_statements(&class_def.body, visitor);
        }
        ast::Stmt::Delete(delete) => {
            for target in &delete.targets {
                walk_expression(target, visitor);
            }
        }
        ast::Stmt::Expr(expr_stmt) => walk_expression(&expr_stmt.value, visitor),
        ast::Stmt::If(if_stmt) => {
            walk_expression(&if_stmt.test, visitor);
            walk_statements(&if_stmt.body, visitor);
            walk_statements(&if_stmt.orelse, visitor);
        }
        ast::Stmt::Match(match_stmt) => {
            walk_expression(&match_stmt.subject, visitor);
            for case in &match_stmt.cases {
                if let Some(guard) = &case.guard {
                    walk_expression(guard, visitor);
                }
                walk_statements(&case.body, visitor);
            }
        }
        ast::Stmt::Raise(raise_stmt) => {
            if let Some(exception) = &raise_stmt.exc {
                walk_expression(exception, visitor);
            }
            if let Some(cause) = &raise_stmt.cause {
                walk_expression(cause, visitor);
            }
        }
        ast::Stmt::Return(return_stmt) => {
            if let Some(value) = &return_stmt.value {
                walk_expression(value, visitor);
            }
        }
        ast::Stmt::Try(try_stmt) => {
            walk_statements(&try_stmt.body, visitor);
            walk_statements(&try_stmt.orelse, visitor);
            walk_statements(&try_stmt.finalbody, visitor);
            for handler in &try_stmt.handlers {
                let ast::ExceptHandler::ExceptHandler(except_handler) = handler;
                if let Some(type_expr) = &except_handler.type_ {
                    walk_expression(type_expr, visitor);
                }
                walk_statements(&except_handler.body, visitor);
            }
        }
        ast::Stmt::TryStar(try_stmt) => {
            walk_statements(&try_stmt.body, visitor);
            walk_statements(&try_stmt.orelse, visitor);
            walk_statements(&try_stmt.finalbody, visitor);
            for handler in &try_stmt.handlers {
                let ast::ExceptHandler::ExceptHandler(except_handler) = handler;
                if let Some(type_expr) = &except_handler.type_ {
                    walk_expression(type_expr, visitor);
                }
                walk_statements(&except_handler.body, visitor);
            }
        }
        ast::Stmt::While(while_stmt) => {
            walk_expression(&while_stmt.test, visitor);
            walk_statements(&while_stmt.body, visitor);
            walk_statements(&while_stmt.orelse, visitor);
        }
        ast::Stmt::AugAssign(assign) => {
            walk_expression(&assign.target, visitor);
            walk_expression(&assign.value, visitor);
        }
        ast::Stmt::Import(_)
        | ast::Stmt::ImportFrom(_)
        | ast::Stmt::Break(_)
        | ast::Stmt::Continue(_)
        | ast::Stmt::Global(_)
        | ast::Stmt::Nonlocal(_)
        | ast::Stmt::Pass(_)
        | ast::Stmt::TypeAlias(_) => {}
    }
}
