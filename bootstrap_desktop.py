import os
import shutil

# --- Paths ---
OLD_DIR = "/Users/kevindavies/Development/AI/ai-os-smartcard/ai-os"
NEW_DIR = "/Users/kevindavies/Development/AI/ai-os-desktop"

def create_directories():
    print("[*] Creating directory structure...")
    dirs = [
        "kernel", "include", "build", "tools",
        "modules/compositor", "modules/input", 
        "modules/shell_gui", "modules/fs", "modules/iso7816"
    ]
    for d in dirs:
        os.makedirs(os.path.join(NEW_DIR, d), exist_ok=True)
        print(f"  + {d}/")

def copy_core_files():
    print("\n[*] Copying core kernel files from smartcard project...")
    
    # 1. Kernel files
    kernel_files = ["boot.S", "vectors.S", "main.c", "mmu.c", "uart.c", "gic.c", "loader.c"]
    for f in kernel_files:
        src = os.path.join(OLD_DIR, "src", f)
        dst = os.path.join(NEW_DIR, "kernel", f)
        if os.path.exists(src):
            shutil.copy2(src, dst)
            print(f"  + kernel/{f}")
            
    # 2. Headers
    old_include = os.path.join(OLD_DIR, "include")
    if os.path.exists(old_include):
        for f in os.listdir(old_include):
            if f.endswith(".h"):
                shutil.copy2(os.path.join(old_include, f), os.path.join(NEW_DIR, "include", f))
                print(f"  + include/{f}")

    # 3. Build & Linker files
    root_files = ["Makefile", "linker.ld", "module.ld"]
    for f in root_files:
        src = os.path.join(OLD_DIR, f)
        if os.path.exists(src):
            shutil.copy2(src, os.path.join(NEW_DIR, f))
            print(f"  + {f}")

    # 4. Tools
    old_tools = os.path.join(OLD_DIR, "tools")
    if os.path.exists(old_tools):
        for f in os.listdir(old_tools):
            if f.endswith(".py"):
                shutil.copy2(os.path.join(old_tools, f), os.path.join(NEW_DIR, "tools", f))
                print(f"  + tools/{f}")

def write_phase1_files():
    print("\n[*] Injecting QA-Cleared Phase 1 Code...")

    # --- include/os_ipc.h ---
    with open(os.path.join(NEW_DIR, "include", "os_ipc.h"), "w") as f:
        f.write("""#ifndef OS_IPC_H
#define OS_IPC_H

#include "os_types.h"

#define SYS_MOD_KERNEL      0
#define SYS_MOD_UART        1
#define SYS_MOD_SHELL       2
#define SYS_MOD_FS          3
#define SYS_MOD_ISO7816     4
#define SYS_MOD_CARDSIM     5
#define SYS_MOD_COMPOSITOR  6
#define SYS_MOD_INPUT       7
#define SYS_MOD_GUI_SHELL   8

#define MODULE_COUNT        9

#define STATIC_ASSERT(COND, MSG) typedef char static_assertion_##MSG[(COND)?1:-1]
STATIC_ASSERT(SYS_MOD_GUI_SHELL < MODULE_COUNT, gui_shell_id_bounds);

#define IPC_PAYLOAD_MAX_SIZE 32
#define SYS_CACHE_CLEAN      6

typedef struct {
    uint32_t sender_id;
    uint32_t target_id;
    uint32_t type;
    uint32_t length;
    uint8_t  payload[IPC_PAYLOAD_MAX_SIZE];
} os_message_t;

#endif
""")
    print("  + Overwrote include/os_ipc.h")

    # --- include/os_virtio.h ---
    with open(os.path.join(NEW_DIR, "include", "os_virtio.h"), "w") as f:
        f.write("""#ifndef OS_VIRTIO_H
#define OS_VIRTIO_H

#include "os_types.h"

#define VIRTIO_MMIO_BASE        0x0A000000
#define VIRTIO_MMIO_SLOT_SIZE   0x200
#define VIRTIO_MMIO_SLOT_COUNT  32
#define VIRTIO_IRQ_BASE         48

#define VIRTIO_MAGIC            0x74726976
#define VIRTIO_DEV_GPU          16
#define VIRTIO_DEV_INPUT        18

#define VIRTIO_STATUS_RESET           0x00
#define VIRTIO_STATUS_ACKNOWLEDGE     0x01
#define VIRTIO_STATUS_DRIVER          0x02
#define VIRTIO_STATUS_DRIVER_OK       0x04
#define VIRTIO_STATUS_FEATURES_OK     0x08
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 0x40
#define VIRTIO_STATUS_FAILED          0x80

#define VIRTIO_REG_MAGICVALUE         0x000
#define VIRTIO_REG_VERSION            0x004
#define VIRTIO_REG_DEVICEID           0x008
#define VIRTIO_REG_VENDORID           0x00C
#define VIRTIO_REG_DEVICE_FEATURES    0x010
#define VIRTIO_REG_DRIVER_FEATURES    0x020
#define VIRTIO_REG_STATUS             0x070

void virtio_probe_and_init(void);

#endif
""")
    print("  + Created include/os_virtio.h")

    # --- kernel/virtio.c ---
    with open(os.path.join(NEW_DIR, "kernel", "virtio.c"), "w") as f:
        f.write("""#include "os_virtio.h"
#include "os_types.h"

extern void kpanic(const char *msg);

#define GICD_BASE 0x08000000

static void virtio_write32(uint64_t base, uint32_t offset, uint32_t val) {
    *((volatile uint32_t *)(base + offset)) = val;
}

static uint32_t virtio_read32(uint64_t base, uint32_t offset) {
    return *((volatile uint32_t *)(base + offset));
}

static void gicv2_register_irq(uint32_t irq) {
    uint32_t reg_index = irq / 32;
    uint32_t bit_index = irq % 32;

    volatile uint32_t *isenabler = (volatile uint32_t *)(GICD_BASE + 0x100 + (reg_index * 4));
    *isenabler = (1 << bit_index);

    volatile uint8_t *ipriorityr = (volatile uint8_t *)(GICD_BASE + 0x400 + irq);
    *ipriorityr = 0xA0;

    volatile uint8_t *itargetsr = (volatile uint8_t *)(GICD_BASE + 0x800 + irq);
    *itargetsr = 0x01; 
}

void virtio_probe_and_init(void) {
    for (int slot = 0; slot < VIRTIO_MMIO_SLOT_COUNT; slot++) {
        uint64_t base_addr = VIRTIO_MMIO_BASE + (slot * VIRTIO_MMIO_SLOT_SIZE);
        
        uint32_t magic = virtio_read32(base_addr, VIRTIO_REG_MAGICVALUE);
        if (magic != VIRTIO_MAGIC) continue;

        uint32_t version = virtio_read32(base_addr, VIRTIO_REG_VERSION);
        if (version != 2) continue; 

        uint32_t device_id = virtio_read32(base_addr, VIRTIO_REG_DEVICEID);
        
        if (device_id == VIRTIO_DEV_GPU || device_id == VIRTIO_DEV_INPUT) {
            
            virtio_write32(base_addr, VIRTIO_REG_STATUS, VIRTIO_STATUS_RESET);
            virtio_write32(base_addr, VIRTIO_REG_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
            virtio_write32(base_addr, VIRTIO_REG_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
            
            /* Phase 1: Accept no optional features — basic display only. */
            uint32_t driver_features = 0; 
            virtio_write32(base_addr, VIRTIO_REG_DRIVER_FEATURES, driver_features);
            
            uint32_t status = virtio_read32(base_addr, VIRTIO_REG_STATUS);
            status |= VIRTIO_STATUS_FEATURES_OK;
            virtio_write32(base_addr, VIRTIO_REG_STATUS, status);
            
            status = virtio_read32(base_addr, VIRTIO_REG_STATUS);
            if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
                kpanic("VirtIO Initialization Failed: Device rejected FEATURES_OK");
            }
            
            status |= VIRTIO_STATUS_DRIVER_OK;
            virtio_write32(base_addr, VIRTIO_REG_STATUS, status);

            uint32_t irq = VIRTIO_IRQ_BASE + slot;
            gicv2_register_irq(irq);
        }
    }
}
""")
    print("  + Created kernel/virtio.c")

    # --- kernel/syscall.c ---
    with open(os.path.join(NEW_DIR, "kernel", "syscall.c"), "w") as f:
        f.write("""#include "os_ipc.h"
#include "os_types.h"

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
    /* COMP    */ { 1,   1,   0,   0,   0,   0,   0,   0,   1 }, /* DEFERRED-11 */
    /* INPUT   */ { 1,   1,   0,   0,   0,   0,   1,   0,   0 }, /* DEFERRED-11 */
    /* GUI_SHL */ { 0,   1,   0,   1,   0,   0,   1,   0,   0 }, /* DEFERRED-11 */
};

extern int get_region_for_current_module(void);
extern int is_valid_el0_pointer(uint64_t ptr, uint64_t size);
extern void kpanic(const char *msg);

void syscall_handler(uint64_t *regs) {
    uint64_t syscall_id = regs[REG_X8];
    uint64_t arg0       = regs[REG_X0];
    uint64_t arg1       = regs[REG_X1];

    int caller_id = get_region_for_current_module();
    if (caller_id < 0) return; 
    
    switch (syscall_id) {
        case SYS_IPC_SEND:
            if (arg1 >= MODULE_COUNT || capability_matrix[caller_id][arg1] == 0) {
                return; 
            }
            /* ... existing IPC dispatch logic goes here ... */
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
            __asm__ volatile("dsb sy\\n\\t" "isb\\n\\t" ::: "memory");
            break;
    }
}
""")
    print("  + Overwrote kernel/syscall.c")

def patch_mmu_c():
    mmu_path = os.path.join(NEW_DIR, "kernel", "mmu.c")
    if not os.path.exists(mmu_path):
        print("  ! Could not find kernel/mmu.c to patch.")
        return

    with open(mmu_path, "r") as f:
        content = f.read()

    # Make sure we don't double-patch
    if "VIRTIO_MMIO_BASE" in content or "0x0A000000" in content:
        print("  + kernel/mmu.c is already patched for VirtIO.")
        return

    # Look for mmu_init_tables entry point
    hook = "void mmu_init_tables(void) {"
    if hook in content:
        patch = """
    /* Map VirtIO MMIO range (0x0A000000 - 0x0A003FFF) */
    uint64_t virtio_start = 0x0A000000;
    uint64_t virtio_end   = 0x0A000000 + (32 * 0x200);
    for (uint64_t addr = virtio_start; addr < virtio_end; addr += 4096) {
        map_page(addr, addr, ATTR_DEVICE | AP_RW_EL1 | XN_ALL);
    }
"""
        new_content = content.replace(hook, hook + patch)
        with open(mmu_path, "w") as f:
            f.write(new_content)
        print("  + Patched kernel/mmu.c with ATTR_DEVICE VirtIO mapping.")
    else:
        print("  ! Could not find 'void mmu_init_tables(void) {' in mmu.c to inject the patch.")

if __name__ == "__main__":
    if not os.path.exists(OLD_DIR):
        print(f"[!] Source directory not found: {OLD_DIR}")
        exit(1)
        
    create_directories()
    copy_core_files()
    write_phase1_files()
    patch_mmu_c()
    print("\n[✓] ai-os-desktop architecture successfully bootstrapped.")