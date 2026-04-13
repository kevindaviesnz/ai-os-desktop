#include "os_types.h"
#include "os_virtio.h"
#include "os_watcher.h"

extern void fs_fat32_init(void);
extern void mmu_init_tables(void);
extern void virtio_probe_and_init(void);
extern void virtio_net_init(void); 
extern void uart_print(const char *str);
extern void uart_print_hex(uint64_t val);
extern void autarky_init(void);

extern char _el0_text_start[];
extern char _el0_stack_top[];

void fatal_exception_handler(uint64_t id, uint64_t esr, uint64_t elr, uint64_t far) {
    uart_print("\n========================================\n");
    uart_print("[ KERNEL PANIC ] HARDWARE EXCEPTION\n");
    uart_print("========================================\n");
    uart_print("Vector ID : "); uart_print_hex(id); uart_print("\n");
    uart_print("ESR_EL1   : "); uart_print_hex(esr); uart_print(" (Syndrome / Cause)\n");
    uart_print("FAR_EL1   : "); uart_print_hex(far); uart_print(" (Fault Address)\n");
    uart_print("ELR_EL1   : "); uart_print_hex(elr); uart_print(" (Instruction PC)\n");
    uart_print("========================================\n");
    uart_print("System Halted.\n");
    
    while (1) {
        __asm__ volatile("wfi");
    }
}

void kernel_main(void) {
    uart_print("\n[BOOT] C Kernel Entry Point Reached.\n");

    uart_print("[BOOT] Initializing MMU Tables...\n");
    mmu_init_tables();
    uart_print("[BOOT] MMU Online. Virtual Memory Active.\n");

    uart_print("[BOOT] Probing VirtIO Devices...\n");
    virtio_probe_and_init();
    
    /* Phase 13: Activate Network Subsystem */
    virtio_net_init(); 
    
    uart_print("[BOOT] VirtIO Initialization Complete.\n");

    fs_fat32_init();
    autarky_init();
    watcher_init();  
    
    uart_print("[BOOT] Configuring EL0 User Space Drop...\n");
    uart_print("       -> EL0 Entry : "); uart_print_hex((uint64_t)_el0_text_start); uart_print("\n");
    uart_print("       -> EL0 Stack : "); uart_print_hex((uint64_t)_el0_stack_top); uart_print("\n");
    uart_print("[BOOT] Executing ERET to user space...\n");

    __asm__ volatile(
        "msr spsr_el1, xzr\n\t"
        "ldr x0, =_el0_text_start\n\t"
        "msr elr_el1, x0\n\t"
        "ldr x0, =_el0_stack_top\n\t"
        "msr sp_el0, x0\n\t"
        "eret\n\t"
        :
        :
        : "x0"
    );

    while (1) {
        __asm__ volatile("wfi");
    }
}

void *memcpy(void *dest, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < n; i++) {
        d[ i ] = s[ i ];
    }
    return dest;
}