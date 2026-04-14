#![no_std]

extern crate alloc;

use core::panic::PanicInfo;
use core::ffi::{c_char, c_void};
use alloc::collections::BTreeMap;
use alloc::string::String;
use alloc::format;

pub mod abi;
pub mod ast;
pub mod ir;
pub mod vm;
pub mod parser;

/* --- FFI BINDINGS TO C KERNEL --- */
extern "C" {
    fn uart_print(str: *const c_char);
    fn bump_allocate(size: u32, align: u32) -> *mut c_void;
    
    // PHASE 14: Cryptographic Ledger API
    fn watcher_commit_ledger(tx_hash: u64, volume: i32); 
}

/* --- THE RUST GLOBAL ALLOCATOR --- */
struct BareMetalAllocator;

unsafe impl core::alloc::GlobalAlloc for BareMetalAllocator {
    unsafe fn alloc(&self, layout: core::alloc::Layout) -> *mut u8 {
        bump_allocate(layout.size() as u32, layout.align() as u32) as *mut u8
    }
    unsafe fn dealloc(&self, _ptr: *mut u8, _layout: core::alloc::Layout) { }
}

#[global_allocator]
static ALLOCATOR: BareMetalAllocator = BareMetalAllocator;

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    // 100% stack-allocated. Safe even if the bump allocator has catastrophically failed.
    // No 'format!' allowed here to prevent recursive OOM double-faults.
    unsafe { 
        uart_print(b"[RUST PANIC] Autarky VM Fault. Core halted to preserve C kernel.\n\0".as_ptr() as *const c_char) 
    };
    loop {}
}

#[no_mangle]
pub extern "C" fn autarky_init() {
    let msg = b"[ATK-VM] Sovereign Rust Engine Initialized.\n\0";
    unsafe { uart_print(msg.as_ptr() as *const c_char) };
}

/* --- BARE METAL CRYPTOGRAPHY (FNV-1a 64-bit) --- */
/* Updated for Phase 14 Audit: Salts hash with volume and nonce to ensure unique TX IDs */
fn generate_tx_id(source: &str, volume: i32, nonce: u64) -> u64 {
    let mut hash: u64 = 0xcbf29ce484222325; // FNV offset basis
    
    // 1. Hash the bytecode source
    for byte in source.bytes() {
        hash ^= byte as u64;
        hash = hash.wrapping_mul(0x100000001b3); // FNV prime
    }

    // 2. Hash the resulting volume
    hash ^= volume as u64;
    hash = hash.wrapping_mul(0x100000001b3);

    // 3. Hash the system timer nonce
    hash ^= nonce;
    hash = hash.wrapping_mul(0x100000001b3);

    hash
}

/* --- THE EXECUTION ENTRY POINT --- */
#[no_mangle]
pub extern "C" fn autarky_execute(
    bytecode_ptr: *const c_char, 
    out_buf: *mut c_char, 
    max_len: u32
) -> u32 {
    unsafe { uart_print(b"[ATK-VM] Command received from C kernel.\n\0".as_ptr() as *const c_char) };
    
    // 1. Convert C string pointer to Rust &str with safety bounds
    let source = unsafe {
        if bytecode_ptr.is_null() { "" } else {
            let mut len = 0;
            // Dynamically defer to the max_len passed by the C kernel
            while *bytecode_ptr.add(len) != 0 && len < (max_len as usize) { len += 1; }
            let slice = core::slice::from_raw_parts(bytecode_ptr as *const u8, len);
            
            // Validate UTF-8 and trim whitespace
            match core::str::from_utf8(slice) {
                Ok(s) => s.trim(),
                Err(_) => {
                    uart_print(b"[ATK-VM] Error: Source contains invalid UTF-8 bytes.\n\0".as_ptr() as *const c_char);
                    ""
                }
            }
        }
    };

    if source.is_empty() {
        unsafe { uart_print(b"[ATK-VM] Error: Source script is empty or invalid.\n\0".as_ptr() as *const c_char) };
        return 0;
    }

    // --- QA FIX 3: Defensive Source Logging ---
    // Safely copy up to 32 bytes into a null-terminated buffer for UART printing
    unsafe {
        uart_print(b"[ATK-VM] Source prefix: \0".as_ptr() as *const c_char);
        let mut log_buf = [0u8; 34];
        let copy_len = core::cmp::min(source.len(), 32);
        log_buf[..copy_len].copy_from_slice(&source.as_bytes()[..copy_len]);
        log_buf[copy_len] = b'\n';
        log_buf[copy_len + 1] = 0; // Null terminate
        uart_print(log_buf.as_ptr() as *const c_char);
    }

    // 2. The Autarky Pipeline
    let mut parser = parser::Parser::new(source);
    
    // --- QA FIX 2: Better Fallback Parsing ---
    let ast = match parser.parse() {
        Ok(a) => {
            if !parser.is_eof() {
                let err_msg = b"[ATK-VM] Parser Error: Trailing characters after expression.\n\0";
                unsafe { uart_print(err_msg.as_ptr() as *const c_char) };
                return 0;
            }
            unsafe { uart_print(b"[ATK-VM] Source parsed successfully.\n\0".as_ptr() as *const c_char) };
            a
        },
        Err(e) => {
            // Check specifically if the error was just an empty EOF
            if e == "EOF" {
                unsafe { uart_print(b"[ATK-VM] Error: Empty contract.\n\0".as_ptr() as *const c_char) };
            } else {
                let err_header = format!("[ATK-VM] Parser Error: {}\n[ATK-VM] Failing Source: >>{}<<\n\0", e, source);
                unsafe { uart_print(err_header.as_ptr() as *const c_char) };
            }
            return 0;
        },
    };

    let ir_root = ir::erase_proofs(&ast);
    let mut machine = vm::VirtualMachine::new();
    
    // Seed standard execution environment
    let mut env = BTreeMap::new();
    env.insert(String::from("SYS_COMPUTE"), vm::Value::Int(1000));
    env.insert(String::from("SYS_CREDITS"), vm::Value::Int(500));

    unsafe { uart_print(b"[ATK-VM] Starting VM Evaluation...\n\0".as_ptr() as *const c_char) };

    // 3. Evaluate!
    let result = machine.evaluate(&env, &ir_root);

    // 4. Format Result for C output and Commit to Ledger
    let res_str = match result {
        Ok(val) => {
            // Extract the integer volume
            let volume = match val {
                vm::Value::Int(v) => v as i32,
                _ => 0, 
            };

            // Grab a high-precision nonce from the AArch64 System Timer (cntpct_el0)
            let mut cntpct: u64;
            unsafe { core::arch::asm!("mrs {}, cntpct_el0", out(reg) cntpct); }

            // Generate a truly unique ID for THIS specific execution
            let tx_hash = generate_tx_id(source, volume, cntpct);

            // Push the immutable receipt across the FFI boundary!
            unsafe { watcher_commit_ledger(tx_hash, volume); }

            format!("ATK MATCHED: {:?}\n\0", val)
        },
        Err(e) => format!("ATK FAULT: {}\n\0", e),
    };

    // 5. Safely copy back to C buffer
    unsafe {
        let bytes = res_str.as_bytes();
        let copy_len = if bytes.len() < max_len as usize { bytes.len() } else { (max_len - 1) as usize };
        core::ptr::copy_nonoverlapping(bytes.as_ptr(), out_buf as *mut u8, copy_len);
        *out_buf.add(copy_len) = 0; // Null terminate
    }

    unsafe { uart_print(b"[ATK-VM] Execution complete. Returning to C.\n\0".as_ptr() as *const c_char) };

    1500 // Return gas metered
}