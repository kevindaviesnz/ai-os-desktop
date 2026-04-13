use alloc::string::String;
use alloc::boxed::Box;

#[derive(Debug, Clone, PartialEq)]
pub enum Type {
    Int,
    Float,
    Any,
    Func(Box<Type>, Box<Type>),
    Pair(Box<Type>, Box<Type>),
    Either(Box<Type>, Box<Type>),
}

#[derive(Debug, Clone)]
pub enum Expr {
    IntLiteral(i64),
    FloatLiteral(f64),
    Var(String),
    Add(Box<Expr>, Box<Expr>),
    Sub(Box<Expr>, Box<Expr>),
    Mul(Box<Expr>, Box<Expr>),
    Div(Box<Expr>, Box<Expr>),
    Eq(Box<Expr>, Box<Expr>),
    Gt(Box<Expr>, Box<Expr>),
    Lt(Box<Expr>, Box<Expr>), 
    Ge(Box<Expr>, Box<Expr>), 
    Le(Box<Expr>, Box<Expr>), 
    Lambda { param: String, param_type: Type, body: Box<Expr> },
    // UPDATED: Added tail_position flag
    App { func: Box<Expr>, arg: Box<Expr>, tail_position: bool },
    Pair(Box<Expr>, Box<Expr>),
    Unpack { a: String, b: String, pair: Box<Expr>, body: Box<Expr> },
    Left(Box<Expr>, ()),
    Right(Box<Expr>, ()),
    Match { expr: Box<Expr>, l_var: String, l_body: Box<Expr>, r_var: String, r_body: Box<Expr> },
    Let { var: String, val: Box<Expr>, body: Box<Expr> },
    LetRec { var: String, val: Box<Expr>, body: Box<Expr> }, 
}