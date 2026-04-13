use alloc::string::String;
use alloc::boxed::Box;
use crate::ast::Expr;

#[derive(Debug, Clone)]
pub enum IrNode {
    IntLiteral(i64),
    FloatLiteral(f64),
    Var(String),
    Add(Box<IrNode>, Box<IrNode>),
    Sub(Box<IrNode>, Box<IrNode>),
    Mul(Box<IrNode>, Box<IrNode>),
    Div(Box<IrNode>, Box<IrNode>),
    Eq(Box<IrNode>, Box<IrNode>),
    Gt(Box<IrNode>, Box<IrNode>),
    Lt(Box<IrNode>, Box<IrNode>),
    Ge(Box<IrNode>, Box<IrNode>),
    Le(Box<IrNode>, Box<IrNode>),
    Lambda { param: String, body: Box<IrNode> },
    App { func: Box<IrNode>, arg: Box<IrNode> },
    Pair(Box<IrNode>, Box<IrNode>),
    Unpack { a: String, b: String, pair: Box<IrNode>, body: Box<IrNode> },
    Left(Box<IrNode>),
    Right(Box<IrNode>),
    Match { expr: Box<IrNode>, l_var: String, l_body: Box<IrNode>, r_var: String, r_body: Box<IrNode> },
    Let { var: String, val: Box<IrNode>, body: Box<IrNode> },
    LetRec { var: String, val: Box<IrNode>, body: Box<IrNode> },
}

pub fn erase_proofs(expr: &Expr) -> IrNode {
    match expr {
        Expr::IntLiteral(n) => IrNode::IntLiteral(*n),
        Expr::FloatLiteral(f) => IrNode::FloatLiteral(*f),
        Expr::Var(name) => IrNode::Var(name.clone()),
        Expr::Add(l, r) => IrNode::Add(Box::new(erase_proofs(l)), Box::new(erase_proofs(r))),
        Expr::Sub(l, r) => IrNode::Sub(Box::new(erase_proofs(l)), Box::new(erase_proofs(r))),
        Expr::Mul(l, r) => IrNode::Mul(Box::new(erase_proofs(l)), Box::new(erase_proofs(r))),
        Expr::Div(l, r) => IrNode::Div(Box::new(erase_proofs(l)), Box::new(erase_proofs(r))),
        Expr::Eq(l, r) => IrNode::Eq(Box::new(erase_proofs(l)), Box::new(erase_proofs(r))),
        Expr::Gt(l, r) => IrNode::Gt(Box::new(erase_proofs(l)), Box::new(erase_proofs(r))),
        Expr::Lt(l, r) => IrNode::Lt(Box::new(erase_proofs(l)), Box::new(erase_proofs(r))),
        Expr::Ge(l, r) => IrNode::Ge(Box::new(erase_proofs(l)), Box::new(erase_proofs(r))),
        Expr::Le(l, r) => IrNode::Le(Box::new(erase_proofs(l)), Box::new(erase_proofs(r))),
        Expr::Lambda { param, body, .. } => IrNode::Lambda { param: param.clone(), body: Box::new(erase_proofs(body)) },
        Expr::App { func, arg, .. } => IrNode::App { func: Box::new(erase_proofs(func)), arg: Box::new(erase_proofs(arg)) },
        Expr::Pair(l, r) => IrNode::Pair(Box::new(erase_proofs(l)), Box::new(erase_proofs(r))),
        Expr::Unpack { a, b, pair, body } => IrNode::Unpack { a: a.clone(), b: b.clone(), pair: Box::new(erase_proofs(pair)), body: Box::new(erase_proofs(body)) },
        Expr::Left(val, _) => IrNode::Left(Box::new(erase_proofs(val))),
        Expr::Right(val, _) => IrNode::Right(Box::new(erase_proofs(val))),
        Expr::Match { expr, l_var, l_body, r_var, r_body } => IrNode::Match {
            expr: Box::new(erase_proofs(expr)),
            l_var: l_var.clone(),
            l_body: Box::new(erase_proofs(l_body)),
            r_var: r_var.clone(),
            r_body: Box::new(erase_proofs(r_body)),
        },
        Expr::Let { var, val, body } => IrNode::Let { var: var.clone(), val: Box::new(erase_proofs(val)), body: Box::new(erase_proofs(body)) },
        Expr::LetRec { var, val, body } => IrNode::LetRec { var: var.clone(), val: Box::new(erase_proofs(val)), body: Box::new(erase_proofs(body)) },
    }
}