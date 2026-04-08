#include "os_types.h"
#include "os_ipc.h"
#include "os_loader.h"

extern void uart_print(const char *str);

/* Restored to standard BSS. Linker script handles alignment. */
module_region_t module_regions[ 8 ];
uint32_t loaded_module_count = 0;

static module_region_t current_module = {
    .module_id  = SYS_MOD_KERNEL,
    .code_base  = 0x40000000,
    .code_size  = 0x80000,
    .stack_base = 0,
    .stack_size = 0
};

void loader_init(void) {}

int get_region_for_current_module(void) {
    return current_module.module_id;
}

/* DEFERRED-12 */
int is_valid_el0_pointer(uint64_t ptr, uint64_t size) {
    (void)ptr; (void)size;
    return 1;
}

int ipc_send(uint32_t sender_id, const os_message_t *msg) {
    (void)sender_id; (void)msg;
    return IPC_SUCCESS;
}

/*
void ipc_kernel_send(uint32_t target_id, uint32_t type, uint8_t *payload, uint32_t length) {
    (void)target_id;
    (void)type;
    (void)payload;
    (void)length;
}
    */

void kpanic(const char *msg) {
    uart_print("\n[ KERNEL PANIC ] ");
    uart_print(msg);
    uart_print("\nSystem Halted.\n");
    while (1) {
        __asm__ volatile("wfi");
    }
}

void try_dispatch_next(uint64_t *regs) {
    (void)regs;
}