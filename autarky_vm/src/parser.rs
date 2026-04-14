use alloc::string::{String, ToString};
use alloc::boxed::Box;
use alloc::format;
use crate::ast::{Expr, Type};

pub struct Parser<'a> {
    source: &'a str,
    pos: usize,
}

impl<'a> Parser<'a> {

    pub fn is_eof(&mut self) -> bool {
        self.skip_whitespace();
        self.pos >= self.source.len()
    }
    
    pub fn new(source: &'a str) -> Self {
        Parser { source, pos: 0 }
    }

    fn skip_whitespace(&mut self) {
        while self.pos < self.source.len() {
            let c = self.source[self.pos..].chars().next().unwrap();
            
            if c.is_whitespace() || c.is_control() || c == '\0' {
                self.pos += c.len_utf8();
            } else if c == '@' { // <--- CHANGED FROM '#' TO '@'
                // THE ABSOLUTE QUARANTINE WALL (Bypassing C Shell comments)
                self.pos = self.source.len();
                break;
            } else {
                break;
            }
        }
    }

    fn read_word(&mut self) -> String {
        self.skip_whitespace();
        if self.pos >= self.source.len() { return String::new(); }
        if self.source[self.pos..].starts_with("->") || self.source[self.pos..].starts_with("=>") {
            self.pos += 2;
            return self.source[self.pos - 2..self.pos].to_string();
        }
        let c = self.source[self.pos..].chars().next().unwrap();
        // ADDED '@' TO THIS LIST SO IT CAN DETACH FROM THE NUMBER
        if "():={},\\#@".contains(c) {
            self.pos += c.len_utf8();
            return c.to_string();
        }
        let mut word = String::new();
        while self.pos < self.source.len() {
            let c = self.source[self.pos..].chars().next().unwrap();
            // ADDED '@' TO THIS BOUNDARY CHECK LIST TOO
            if c.is_whitespace() || c.is_control() || c == '\0' || "():={},\\#@".contains(c) || self.source[self.pos..].starts_with("->") || self.source[self.pos..].starts_with("=>") {
                break;
            }
            word.push(c);
            self.pos += c.len_utf8();
        }
        word
    }

    fn parse_type(&mut self) -> Result<Type, String> {
        let t = self.read_word();
        match t.as_str() {
            "Int" => Ok(Type::Int),
            "Float" => Ok(Type::Float),
            "Any" => Ok(Type::Any),
            "Pair" => {
                self.read_word(); // (
                let t1 = self.parse_type()?;
                self.read_word(); // ,
                let t2 = self.parse_type()?;
                self.read_word(); // )
                Ok(Type::Pair(Box::new(t1), Box::new(t2)))
            }
            "Either" => {
                self.read_word(); // (
                let t1 = self.parse_type()?;
                self.read_word(); // ,
                let t2 = self.parse_type()?;
                self.read_word(); // )
                Ok(Type::Either(Box::new(t1), Box::new(t2)))
            }
            _ => Err(format!("Unknown type: {}", t)),
        }
    }

    pub fn parse(&mut self) -> Result<Expr, String> {
        self.skip_whitespace();
        if self.pos >= self.source.len() { return Err("EOF".into()); }
        let word = self.read_word();
        match word.as_str() {
            "add" => Ok(Expr::Add(Box::new(self.parse()?), Box::new(self.parse()?))),
            "sub" => Ok(Expr::Sub(Box::new(self.parse()?), Box::new(self.parse()?))),
            "mul" => Ok(Expr::Mul(Box::new(self.parse()?), Box::new(self.parse()?))),
            "div" => Ok(Expr::Div(Box::new(self.parse()?), Box::new(self.parse()?))),
            "eq" => Ok(Expr::Eq(Box::new(self.parse()?), Box::new(self.parse()?))),
            "gt" => Ok(Expr::Gt(Box::new(self.parse()?), Box::new(self.parse()?))),
            "lt" => Ok(Expr::Lt(Box::new(self.parse()?), Box::new(self.parse()?))), 
            "ge" => Ok(Expr::Ge(Box::new(self.parse()?), Box::new(self.parse()?))), 
            "le" => Ok(Expr::Le(Box::new(self.parse()?), Box::new(self.parse()?))), 
            "pair" => Ok(Expr::Pair(Box::new(self.parse()?), Box::new(self.parse()?))),
            "Left" => { self.read_word(); let v = self.parse()?; self.read_word(); Ok(Expr::Left(Box::new(v), ())) }
            "Right" => { self.read_word(); let v = self.parse()?; self.read_word(); Ok(Expr::Right(Box::new(v), ())) }
            "match" => {
                let expr = self.parse()?;
                self.read_word(); // {
                self.read_word(); // Left
                self.read_word(); // (
                let l_var = self.read_word();
                self.read_word(); // )
                self.read_word(); // =>
                let l_body = self.parse()?;
                let next = self.read_word();
                let _r_token = if next == "," { self.read_word() } else { next };
                self.read_word(); // (
                let r_var = self.read_word();
                self.read_word(); // )
                self.read_word(); // =>
                let r_body = self.parse()?;
                self.read_word(); // }
                Ok(Expr::Match { expr: Box::new(expr), l_var, l_body: Box::new(l_body), r_var, r_body: Box::new(r_body) })
            }
            "let" => {
                let next = self.read_word();
                if next == "rec" {
                    let var = self.read_word(); self.read_word(); // =
                    let val = self.parse()?; self.read_word(); // in
                    let body = self.parse()?;
                    Ok(Expr::LetRec { var, val: Box::new(val), body: Box::new(body) })
                } else {
                    let var = next; self.read_word(); // =
                    let val = self.parse()?; self.read_word(); // in
                    let body = self.parse()?;
                    Ok(Expr::Let { var, val: Box::new(val), body: Box::new(body) })
                }
            }
            "unpack" => {
                let a = self.read_word(); let b = self.read_word(); self.read_word();
                let p = self.parse()?; self.read_word();
                let body = self.parse()?;
                Ok(Expr::Unpack { a, b, pair: Box::new(p), body: Box::new(body) })
            }
            "\\" => {
                let p = self.read_word(); self.read_word();
                let pt = self.parse_type()?; self.read_word();
                let b = self.parse()?;
                Ok(Expr::Lambda { param: p, param_type: pt, body: Box::new(b) })
            }
            "(" => {
                let first = self.parse()?;
                self.skip_whitespace();
                if self.pos < self.source.len() && self.source[self.pos..].starts_with(')') {
                    self.read_word(); 
                    return Ok(first); 
                }
                let second = self.parse()?;
                self.read_word(); 
                Ok(Expr::App { func: Box::new(first), arg: Box::new(second), tail_position: false })
            }
            _ => {
                if let Ok(n) = word.parse::<i64>() { Ok(Expr::IntLiteral(n)) }
                else if let Ok(f) = word.parse::<f64>() { Ok(Expr::FloatLiteral(f)) }
                else { Ok(Expr::Var(word)) }
            }
        }
    }
}