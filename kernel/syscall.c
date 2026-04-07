#include "os_ipc.h"
#include "os_types.h"
#include "os_virtio.h"

#define REG_X0 0
#define REG_X1 1
#define REG_X8 8

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

extern int get_region_for_current_module(void);
extern int is_valid_el0_pointer(uint64_t ptr, uint64_t size);
extern void kpanic(const char *msg);
extern int virtio_blk_read_sector(uint64_t sector, uint8_t *buffer);

void syscall_handler(uint64_t *regs) {
    uint64_t syscall_id = regs[REG_X8];
    uint64_t arg0       = regs[REG_X0];
    uint64_t arg1       = regs[REG_X1];

    int caller_id = get_region_for_current_module();
    if (caller_id < 0) return; 
    
    switch (syscall_id) {
        case SYS_IPC_SEND:
            if (arg1 >= MODULE_COUNT || capability_matrix[caller_id][arg1] == 0) {
                regs[REG_X0] = (uint64_t)-1;
                return; 
            }
            regs[REG_X0] = 0;
            break;

        case SYS_CACHE_CLEAN:
            if (!is_valid_el0_pointer(arg0, arg1)) {
                kpanic("Security Violation: Module attempted to flush memory outside its mapped domain.");
            }
            
            uint64_t start_aligned = arg0 & ~(63ULL);
            uint64_t end = arg0 + arg1;
            for (uint64_t addr = start_aligned; addr < end; addr += 64) {
                __asm__ volatile("dc cvac, %0" : : "r"(addr) : "memory");
            }
            __asm__ volatile("dsb sy\n\tisb\n\t" ::: "memory");
            regs[REG_X0] = 0;
            break;

        case SYS_BLK_READ:
            if (caller_id != SYS_MOD_FS) {
                kpanic("Security Violation: Non-FS module attempted to read raw disk blocks.");
            }
            if (!is_valid_el0_pointer(arg1, 512)) {
                regs[REG_X0] = (uint64_t)-1;
                return;
            }
            regs[REG_X0] = virtio_blk_read_sector(arg0, (uint8_t *)arg1);
            break;
            
        case SYS_GPU_FLUSH:
            virtio_gpu_flush();
            regs[REG_X0] = 0;
            break;

        default:
            regs[REG_X0] = (uint64_t)-1;
            break;
    }
}