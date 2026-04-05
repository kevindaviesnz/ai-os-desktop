#include "os_types.h"
#include "os_ipc.h"
#include "os_loader.h"

static module_region_t current_module = {
    .module_id  = SYS_MOD_KERNEL,
    .code_base  = 0x40000000,
    .code_size  = 0x80000,
    .stack_base = 0,
    .stack_size = 0
};

void loader_init(void) {}

module_region_t* get_region_for_current_module(void) {
    return &current_module;
}

/* DEFERRED-12 */
int is_valid_el0_pointer(uint64_t ptr, uint64_t size) {
    (void)ptr; (void)size;
    return 1; 
}

int ipc_send(uint32_t sender_id, const os_message_t *msg) {
    (void)sender_id; (void)msg;
    return IPC_OK; 
}

void try_dispatch_next(uint64_t *regs) {
    (void)regs;
}