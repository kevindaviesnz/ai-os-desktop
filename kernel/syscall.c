#include "os_ipc.h"
#include "os_types.h"

#define REG_X0 0
#define REG_X1 1
#define REG_X8 8

/* * ZERO-TRUST IPC CAPABILITY MATRIX */
const uint8_t capability_matrix[MODULE_COUNT][MODULE_COUNT] = {
    /* KRN  UAR  SHL   FS  ISO  CRD  CMP  INP  GUI */
    /* KERNEL  */ { 1,   1,   1,   1,   1,   1,   1,   1,   1 },
    /* UART    */ { 1,   0,   1,   0,   0,   0,   0,   0,   0 },
    /* SHELL   */ { 0,   1,   0,   1,   1,   0,   0,   0,   0 },
    /* FS      */ { 0,   0,   1,   0,   0,   0,   0,   0,   1 },
    /* ISO7816 */ { 1,   0,   1,   0,   0,   1,   0,   0,   0 },
    /* CARDSIM */ { 0,   0,   0,   0,   1,   0,   0,   0,   0 },
    /* COMP    */ { 1,   1,   0,   0,   0,   0,   0,   0,   1 },
    /* INPUT   */ { 1,   1,   0,   0,   0,   0,   1,   0,   0 },
    /* GUI_SHL */ { 0,   1,   0,   1,   0,   0,   1,   0,   0 },
};

typedef struct {
    uint32_t module_id;
    uint64_t code_base;
    uint64_t code_size;
    uint64_t stack_base;
    uint64_t stack_size;
} module_region_t;

extern module_region_t* get_region_for_current_module(void);
extern int is_valid_el0_pointer(uint64_t ptr, uint64_t size);
extern void kpanic(const char *msg);
extern int ipc_send(uint32_t sender_id, const os_message_t *msg);
extern void try_dispatch_next(uint64_t *regs);

void syscall_handler(uint64_t *regs) {
    uint64_t syscall_id = regs[REG_X8];

    module_region_t *region = get_region_for_current_module();
    if (!region) return;

    uint32_t caller_id = region->module_id;
    if (caller_id >= MODULE_COUNT) kpanic("FATAL: caller_id OOB\n");
    
    switch (syscall_id) {
        case SYS_IPC_SEND:
        {
            os_message_t *msg = (os_message_t*)regs[REG_X0];
            
            if (!is_valid_el0_pointer((uint64_t)msg, sizeof(os_message_t))) {
                regs[REG_X0] = IPC_ERR_INVALID;
                break;
            }
            if (msg->target_id >= MODULE_COUNT || capability_matrix[caller_id][msg->target_id] == 0) {
                regs[REG_X0] = IPC_ERR_DENIED;
                break;
            }
            
            regs[REG_X0] = ipc_send(caller_id, msg);
            try_dispatch_next(regs);
            break;
        }
        case SYS_CACHE_CLEAN:
        {
            uint64_t arg0 = regs[REG_X0];
            uint64_t arg1 = regs[REG_X1];
            
            if (!is_valid_el0_pointer(arg0, arg1)) kpanic("Security Violation");
            
            uint64_t start = arg0 & ~(63ULL);
            uint64_t end = arg0 + arg1;
            for (uint64_t addr = start; addr < end; addr += 64) {
                __asm__ volatile("dc cvac, %0" : : "r"(addr) : "memory");
            }
            __asm__ volatile("dsb sy\n\t" "isb\n\t" ::: "memory");
            break;
        }
    }
}