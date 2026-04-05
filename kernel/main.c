#include "os_types.h"
#include "os_virtio.h"

extern void uart_print(const char *str);
extern void mmu_init_tables(void);
extern void gic_init(void);
extern void timer_init(void);
extern void loader_init(void);
extern void init_vectors(void);

void kpanic(const char *msg) {
    uart_print("\n[KERNEL PANIC] ");
    uart_print(msg);
    uart_print("\n");
    while(1) {
        __asm__ volatile("wfi");
    }
}

/* Bulletproof Stack-Free Hex Printer */
void uart_print_hex(uint64_t val) {
    static char buf[ 19 ]; 
    static char hex_chars[ 17 ] = "0123456789ABCDEF"; 
    
    buf[ 0 ] = '0'; 
    buf[ 1 ] = 'x';
    buf[ 18 ] = '\0';
    
    for (int i = 0; i < 16; i++) {
        buf[ 17 - i ] = hex_chars[ val & 0xF ];
        val >>= 4;
    }
    
    uart_print(buf);
}

void kernel_main(void) {
    /* CRITICAL: Exception vectors MUST be set before any operation that can fault */
    init_vectors();

    /* BARE METAL MAGIC: Enable the FPU / NEON Coprocessor!
     * GCC at -O2 auto-vectorizes large array loops using SIMD registers. 
     * If we don't wake up the FPU, the CPU throws an EC=0x07 trap.
     */
    uint64_t cpacr;
    __asm__ volatile("mrs %0, cpacr_el1" : "=r"(cpacr));
    cpacr |= (3 << 20); /* Set FPEN bits 20 and 21 to 1 */
    __asm__ volatile("msr cpacr_el1, %0\n\tisb" : : "r"(cpacr));

    uart_print("[KERNEL] ai-os-desktop booting...\n");

    mmu_init_tables();
    uart_print("[KERNEL] MMU online\n");

    gic_init();
    timer_init();
    
    virtio_probe_and_init();

    uart_print("[KERNEL] Dropping to EL0...\n");
    
    loader_init();
    
    while(1) {
        __asm__ volatile("wfi");
    }
}