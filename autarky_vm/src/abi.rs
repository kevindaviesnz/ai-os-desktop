use alloc::string::{String, ToString};
use alloc::vec::Vec;
use alloc::boxed::Box;
use alloc::format;
use crate::ast::Expr;

pub fn deserialize_hex(hex_str: &str) -> Result<Expr, String> {
    let bytes = decode_hex(hex_str)?;
    let (expr, remaining) = parse_bytes(&bytes)?;
    
    if !remaining.is_empty() {
        return Err("ABI Error: Trailing bytes found after parsing payload".to_string());
    }
    
    Ok(expr)
}

fn decode_hex(s: &str) -> Result<Vec<u8>, String> {
    if s.len() % 2 != 0 { 
        return Err("Hex string length must be even".to_string()); 
    }
    (0..s.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&s[ i..i + 2 ], 16).map_err(|e| e.to_string()))
        .collect()
}

fn parse_bytes(bytes: &[ u8 ]) -> Result<(Expr, &[ u8 ]), String> {
    if bytes.is_empty() {
        return Err("ABI Error: Unexpected end of payload".to_string());
    }
    
    let tag: u8 = bytes[ 0 ]; 
    let rest = &bytes[ 1.. ];
    
    match tag {
        0x00 => { 
            if rest.len() < 8 {
                return Err("ABI Error: Not enough bytes for Int64".to_string());
            }
            let mut buf = [ 0u8; 8 ];
            buf.copy_from_slice(&rest[ ..8 ]);
            let val = i64::from_be_bytes(buf);
            Ok((Expr::IntLiteral(val), &rest[ 8.. ]))
        }
        0x01 => { 
            let (left, rest1) = parse_bytes(rest)?;
            let (right, rest2) = parse_bytes(rest1)?;
            Ok((Expr::Pair(Box::new(left), Box::new(right)), rest2))
        }
        0x02 => { 
            let (inner, rest1) = parse_bytes(rest)?;
            Ok((Expr::Left(Box::new(inner), ()), rest1))
        }
        0x03 => { 
            let (inner, rest1) = parse_bytes(rest)?;
            Ok((Expr::Right(Box::new(inner), ()), rest1))
        }
        _ => Err(format!("ABI Error: Unknown tag byte 0x{:02X}", tag)),
    }
}