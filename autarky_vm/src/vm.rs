use crate::ir::IrNode;
use alloc::collections::BTreeMap;
use alloc::string::String;
use alloc::boxed::Box;
use alloc::format;

/* Import the C kernel's network polling function! */
extern "C" {
    fn virtio_net_poll_rx() -> core::ffi::c_int;
}

#[derive(Debug, Clone)]
pub enum Value {
    Int(i64),
    Float(f64),
    Pair(Box<Value>, Box<Value>),
    Left(Box<Value>),
    Right(Box<Value>),
    // rec_name allows the closure to re-inject itself for infinite recursion
    Closure { param: String, body: Box<IrNode>, env: BTreeMap<String, Value>, rec_name: Option<String> },
}

pub struct VirtualMachine {}

impl VirtualMachine {
    pub fn new() -> Self { VirtualMachine {} }

    pub fn evaluate(&mut self, env: &BTreeMap<String, Value>, node: &IrNode) -> Result<Value, String> {
        match node {
            IrNode::IntLiteral(n) => Ok(Value::Int(*n)),
            IrNode::FloatLiteral(f) => Ok(Value::Float(*f)),
            IrNode::Var(name) => {
                /* HARDWARE HOOK: If the contract asks for network data, poll the VirtQueue! */
                if name == "POLL" { // <--- Changed this from "POLL_NETWORK"
                    let volume = unsafe { virtio_net_poll_rx() };
                    return Ok(Value::Int(volume as i64));
                }
                env.get(name).cloned().ok_or_else(|| format!("Unbound variable: {}", name))
            },
            
            IrNode::Add(l, r) => {
                if let (Value::Int(a), Value::Int(b)) = (self.evaluate(env, l)?, self.evaluate(env, r)?) {
                    Ok(Value::Int(a + b))
                } else { Err("Math requires Int".into()) }
            }
            IrNode::Sub(l, r) => {
                if let (Value::Int(a), Value::Int(b)) = (self.evaluate(env, l)?, self.evaluate(env, r)?) {
                    Ok(Value::Int(a - b))
                } else { Err("Math requires Int".into()) }
            }
            IrNode::Mul(l, r) => {
                if let (Value::Int(a), Value::Int(b)) = (self.evaluate(env, l)?, self.evaluate(env, r)?) {
                    Ok(Value::Int(a * b))
                } else { Err("Math requires Int".into()) }
            }
            IrNode::Div(l, r) => {
                if let (Value::Int(a), Value::Int(b)) = (self.evaluate(env, l)?, self.evaluate(env, r)?) {
                    if b == 0 { Err("Div by zero".into()) } else { Ok(Value::Int(a / b)) }
                } else { Err("Math requires Int".into()) }
            }
            
            // LINEAR LOGIC RELATIONALS: Must return Left(0) or Right(0) to allow linear matching
            IrNode::Ge(l, r) => {
                if let (Value::Int(a), Value::Int(b)) = (self.evaluate(env, l)?, self.evaluate(env, r)?) {
                    if a >= b { Ok(Value::Left(Box::new(Value::Int(0)))) } else { Ok(Value::Right(Box::new(Value::Int(0)))) }
                } else { Err("Ge requires Int".into()) }
            }
            IrNode::Gt(l, r) => {
                if let (Value::Int(a), Value::Int(b)) = (self.evaluate(env, l)?, self.evaluate(env, r)?) {
                    if a > b { Ok(Value::Left(Box::new(Value::Int(0)))) } else { Ok(Value::Right(Box::new(Value::Int(0)))) }
                } else { Err("Gt requires Int".into()) }
            }
            IrNode::Lt(l, r) => {
                if let (Value::Int(a), Value::Int(b)) = (self.evaluate(env, l)?, self.evaluate(env, r)?) {
                    if a < b { Ok(Value::Left(Box::new(Value::Int(0)))) } else { Ok(Value::Right(Box::new(Value::Int(0)))) }
                } else { Err("Lt requires Int".into()) }
            }
            IrNode::Le(l, r) => {
                if let (Value::Int(a), Value::Int(b)) = (self.evaluate(env, l)?, self.evaluate(env, r)?) {
                    if a <= b { Ok(Value::Left(Box::new(Value::Int(0)))) } else { Ok(Value::Right(Box::new(Value::Int(0)))) }
                } else { Err("Le requires Int".into()) }
            }
            IrNode::Eq(l, r) => {
                if let (Value::Int(a), Value::Int(b)) = (self.evaluate(env, l)?, self.evaluate(env, r)?) {
                    if a == b { Ok(Value::Left(Box::new(Value::Int(0)))) } else { Ok(Value::Right(Box::new(Value::Int(0)))) }
                } else { Err("Eq requires Int".into()) }
            }

            IrNode::Pair(l, r) => Ok(Value::Pair(Box::new(self.evaluate(env, l)?), Box::new(self.evaluate(env, r)?))),
            IrNode::Left(v) => Ok(Value::Left(Box::new(self.evaluate(env, v)?))),
            IrNode::Right(v) => Ok(Value::Right(Box::new(self.evaluate(env, v)?))),
            
            IrNode::Lambda { param, body } => {
                Ok(Value::Closure { param: param.clone(), body: body.clone(), env: env.clone(), rec_name: None })
            }
            
            IrNode::App { func, arg } => {
                let f_val = self.evaluate(env, func)?;
                let a_val = self.evaluate(env, arg)?;
                if let Value::Closure { param, body, mut env, rec_name } = f_val.clone() {
                    env.insert(param, a_val);
                    // Re-inject self for infinite recursion without stack overflow
                    if let Some(name) = rec_name {
                        env.insert(name, f_val);
                    }
                    self.evaluate(&env, &body)
                } else { Err("App on non-closure".into()) }
            }
            
            IrNode::Match { expr, l_var, l_body, r_var, r_body } => {
                match self.evaluate(env, expr)? {
                    Value::Left(v) => {
                        let mut new_env = env.clone();
                        new_env.insert(l_var.clone(), *v);
                        self.evaluate(&new_env, l_body)
                    }
                    Value::Right(v) => {
                        let mut new_env = env.clone();
                        new_env.insert(r_var.clone(), *v);
                        self.evaluate(&new_env, r_body)
                    }
                    _ => Err("Match scrutinee must be Either".into()),
                }
            }
            
            IrNode::Let { var, val, body } => {
                let v = self.evaluate(env, val)?;
                let mut new_env = env.clone();
                new_env.insert(var.clone(), v);
                self.evaluate(&new_env, body)
            }
            
            IrNode::LetRec { var, val, body } => {
                let mut v = self.evaluate(env, val)?;
                // Tag the closure with its own name so `App` can dynamically bind it
                if let Value::Closure { ref mut rec_name, .. } = v {
                    *rec_name = Some(var.clone());
                }
                let mut new_env = env.clone();
                new_env.insert(var.clone(), v);
                self.evaluate(&new_env, body)
            }
            
            IrNode::Unpack { a, b, pair, body } => {
                if let Value::Pair(lv, rv) = self.evaluate(env, pair)? {
                    let mut new_env = env.clone();
                    new_env.insert(a.clone(), *lv);
                    new_env.insert(b.clone(), *rv);
                    self.evaluate(&new_env, body)
                } else { Err("Unpack requires Pair".into()) }
            }
        }
    }
}