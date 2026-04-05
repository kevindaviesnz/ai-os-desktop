import os

# 1. Define the directory structure
directories = [
    "kernel",
    "include",
    "build",
    "tools",
    "modules/compositor",
    "modules/input",
    "modules/shell_gui",
    "modules/fs",
    "modules/iso7816"
]

# 2. Define the cleared Phase 1 file contents
files = {
    "include/os_ipc.h": """#ifndef OS_IPC_H
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
""",

    "include/os_virtio.h": """#ifndef OS_VIRTIO_H
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
""",

    "kernel/mmu_snippet.c": """/* INTEGRATION NOTE: Paste this into your existing mmu.c inside mmu_init_tables() */
    /* Map VirtIO MMIO range (0x0A000000 - 0x0A003FFF) as ATTR_DEVICE | AP_RW_EL1 | XN_ALL */
    uint64_t virtio_start = 0x0A000000;
    uint64_t virtio_end   = 0x0A000000 + (32 * 0x200);
    
    for (uint64_t addr = virtio_start; addr < virtio_end; addr += 4096) {
        map_page(addr, addr, ATTR_DEVICE | AP_RW_EL1 | XN_ALL);
    }
""",

    "kernel/syscall_matrix.c": """/* INTEGRATION NOTE: Replace the matrix in your existing syscall.c with this */
#include "os_ipc.h"

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
"""
}

def main():
    print("[*] Initializing ai-os-desktop Phase 1 Workspace...")
    
    for d in directories:
        os.makedirs(d, exist_ok=True)
        print(f"  [+] Created directory: {d}")

    for filepath, content in files.items():
        with open(filepath, 'w') as f:
            f.write(content)
        print(f"  [+] Wrote cleared code to: {filepath}")
        
    print("\n[*] Workspace ready. Waiting for QA to return at 12 PM.")

if __name__ == "__main__":
    main()