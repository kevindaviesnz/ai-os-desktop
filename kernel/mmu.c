#include "os_types.h"
#include "os_virtio.h"

extern void kpanic(const char *msg);

#define PAGE_SIZE       4096
#define DESC_VALID      (1 << 0)
#define DESC_TABLE      (1 << 1)
#define DESC_PAGE       (1 << 1)
#define DESC_BLOCK      (0 << 1)
#define DESC_AF         (1 << 10)
#define AP_RW_EL1       (0 << 6)
#define AP_RW_ANY       (1 << 6)
#define ATTR_DEVICE     (0 << 2) /* Maps to MAIR index 0 */
#define ATTR_NORMAL     (1 << 2) /* Maps to MAIR index 1 */

/* 512-element arrays */
static uint64_t l1_table[ 512 ]          __attribute__((aligned(PAGE_SIZE)));
static uint64_t l2_table_user[ 512 ]     __attribute__((aligned(PAGE_SIZE)));
static uint64_t l2_table_kernel[ 512 ]   __attribute__((aligned(PAGE_SIZE)));
static uint64_t l3_table_mmio[ 512 ]     __attribute__((aligned(PAGE_SIZE)));
static uint64_t l3_table_fb[ 512 ]       __attribute__((aligned(PAGE_SIZE)));

void mmu_init_tables(void) {
    /* Step 0: Zero all tables explicitly */
    for (int i = 0; i < 512; i++) {
        l1_table[ i ] = 0;
        l2_table_kernel[ i ] = 0;
        l2_table_user[ i ] = 0;
        l3_table_mmio[ i ] = 0;
        l3_table_fb[ i ] = 0;
    }

    /* Step 1: L1 setup */
    l1_table[ 1 ] = (uint64_t)l2_table_kernel | DESC_TABLE | DESC_VALID;
    l1_table[ 0 ] = (uint64_t)l2_table_user   | DESC_TABLE | DESC_VALID;

    /* Step 2: Kernel RAM — 2MB blocks via L2 */
    for (int i = 0; i < 64; i++) { 
        l2_table_kernel[ i ] = (0x40000000ULL + ((uint64_t)i * 0x200000))
                              | ATTR_NORMAL | AP_RW_EL1 | DESC_AF | DESC_BLOCK | DESC_VALID;
    }

    /* Step 3: GIC device pages (0x08000000) */
    l2_table_user[ 64 ] = 0x08000000ULL
                        | ATTR_DEVICE | AP_RW_EL1 | DESC_AF | DESC_BLOCK | DESC_VALID;

    /* Step 4: UART device page (0x09000000) */
    l2_table_user[ 72 ] = 0x09000000ULL
                        | ATTR_DEVICE | AP_RW_EL1 | DESC_AF | DESC_BLOCK | DESC_VALID;

    /* Step 5: DMA heap (0x50000000 to 0x503FFFFF) - 4MB Total */
    /* THE FIX: Changed to ATTR_DEVICE so Virtqueues bypass CPU Cache! */
    l2_table_kernel[ 128 ] = 0x50000000ULL 
                           | ATTR_DEVICE | AP_RW_EL1 | DESC_AF | DESC_BLOCK | DESC_VALID;
    
    l2_table_kernel[ 129 ] = 0x50200000ULL 
                           | ATTR_DEVICE | AP_RW_EL1 | DESC_AF | DESC_BLOCK | DESC_VALID;

    /* Step 6: VirtIO MMIO (0x0A000000) — 4KB pages via L3 */
    l2_table_user[ 80 ] = (uint64_t)l3_table_mmio | DESC_TABLE | DESC_VALID;
    for (int i = 0; i < 32; i++) { 
        l3_table_mmio[ i ] = (VIRTIO_MMIO_BASE + ((uint64_t)i * 4096))
                           | ATTR_DEVICE | AP_RW_EL1 | DESC_AF | DESC_PAGE | DESC_VALID;
    }

    /* Step 7: Configure system registers */
    uint64_t mair = (0xFFULL << 8) | (0x00ULL << 0);
    uint64_t tcr  = (25ULL << 0) | (1ULL << 8) | (1ULL << 10) |
                    (3ULL << 12) | (0ULL << 14) | (1ULL << 23) | (2ULL << 32);

    __asm__ volatile (
        "msr mair_el1, %0\n\t"
        "msr tcr_el1,  %1\n\t"
        "msr ttbr0_el1, %2\n\t"
        "dsb sy\n\t"            
        "isb\n\t"               
        "tlbi vmalle1\n\t"      
        "dsb sy\n\t"
        "isb\n\t"
        "mrs x0, sctlr_el1\n\t"
        "orr x0, x0, #(1 << 0)\n\t"   /* M: MMU enable */
        "orr x0, x0, #(1 << 2)\n\t"   /* C: D-cache enable */
        "orr x0, x0, #(1 << 12)\n\t"  /* I: I-cache enable */
        "msr sctlr_el1, x0\n\t"
        "isb\n\t"               
        : : "r"(mair), "r"(tcr), "r"((uint64_t)l1_table) : "x0", "memory"
    );
}

void mmu_map_framebuffer(uint64_t phys_addr, uint64_t size) {
    if (!phys_addr) return;
    
    l2_table_user[ 256 ] = (uint64_t)l3_table_fb | DESC_TABLE | DESC_VALID;

    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
        uint32_t idx = offset / PAGE_SIZE;
        if (idx < 512) {
            l3_table_fb[ idx ] = (phys_addr + offset) | ATTR_NORMAL | AP_RW_ANY | DESC_AF | DESC_PAGE | DESC_VALID;
        }
    }
}