#include "os_types.h"
#include "os_virtio.h"

extern void kpanic(const char *msg);

#define PAGE_SIZE   4096
#define DESC_VALID  (1 << 0)
#define DESC_TABLE  (1 << 1)
#define DESC_PAGE   (1 << 1)
#define DESC_BLOCK  (0 << 1)
#define DESC_AF     (1 << 10)
#define AP_RW_EL1   (0 << 6)
#define AP_RW_ANY   (1 << 6)
#define ATTR_DEVICE (0 << 2)
#define ATTR_NORMAL (1 << 2)

/* Linker symbols for the complete EL0 executable and stack region */
extern char _el0_region_start[];
extern char _el0_region_end[];

static uint64_t l1_table[ 512 ]        __attribute__((aligned(PAGE_SIZE)));
static uint64_t l2_table_user[ 512 ]   __attribute__((aligned(PAGE_SIZE)));
static uint64_t l2_table_kernel[ 512 ] __attribute__((aligned(PAGE_SIZE)));
static uint64_t l3_table_mmio[ 512 ]   __attribute__((aligned(PAGE_SIZE)));

/* FIX: Two L3 tables to support up to 4MB of framebuffer */
static uint64_t l3_table_fb_0[ 512 ]   __attribute__((aligned(PAGE_SIZE)));
static uint64_t l3_table_fb_1[ 512 ]   __attribute__((aligned(PAGE_SIZE)));

void mmu_init_tables(void) {
    /* Step 0: Zero all tables */
    for (int i = 0; i < 512; i++) {
        l1_table[ i ]        = 0;
        l2_table_kernel[ i ] = 0;
        l2_table_user[ i ]   = 0;
        l3_table_mmio[ i ]   = 0;
        l3_table_fb_0[ i ]   = 0;
        l3_table_fb_1[ i ]   = 0;
    }

    /* Step 1: L1 — index 1 covers 0x40000000 (kernel), index 0 covers 0x00000000 (devices) */
    l1_table[ 1 ] = (uint64_t)l2_table_kernel | DESC_TABLE | DESC_VALID;
    l1_table[ 0 ] = (uint64_t)l2_table_user   | DESC_TABLE | DESC_VALID;

    /* Step 2: Kernel RAM — 2MB blocks, EL1-only by default */
    for (int i = 0; i < 64; i++) {
        l2_table_kernel[ i ] = (0x40000000ULL + ((uint64_t)i * 0x200000))
                             | ATTR_NORMAL | AP_RW_EL1 | DESC_AF | DESC_BLOCK | DESC_VALID;
    }

    /* Step 3: Map the dynamically aligned EL0 region with AP_RW_ANY */
    {
        uint64_t el0_start   = (uint64_t)_el0_region_start;
        uint64_t el0_end     = (uint64_t)_el0_region_end;
        uint64_t block       = 0x200000ULL;
        uint64_t kbase       = 0x40000000ULL;
        uint32_t idx_start   = (uint32_t)((el0_start - kbase) / block);
        uint32_t idx_end     = (uint32_t)((el0_end   - kbase + block - 1) / block);

        for (uint32_t i = idx_start; i < idx_end && i < 64; i++) {
            l2_table_kernel[ i ] = (kbase + ((uint64_t)i * block))
                                 | ATTR_NORMAL | AP_RW_ANY | DESC_AF | DESC_BLOCK | DESC_VALID;
        }
    }

    /* Step 4: GIC (0x08000000) */
    l2_table_user[ 64 ] = 0x08000000ULL | ATTR_DEVICE | AP_RW_EL1 | DESC_AF | DESC_BLOCK | DESC_VALID;

    /* Step 5: UART (0x09000000) */
    l2_table_user[ 72 ] = 0x09000000ULL | ATTR_DEVICE | AP_RW_EL1 | DESC_AF | DESC_BLOCK | DESC_VALID;

    /* Step 6: DMA heap (0x50000000 - 0x503FFFFF) */
    l2_table_kernel[ 128 ] = 0x50000000ULL | ATTR_DEVICE | AP_RW_EL1 | DESC_AF | DESC_BLOCK | DESC_VALID;
    l2_table_kernel[ 129 ] = 0x50200000ULL | ATTR_DEVICE | AP_RW_EL1 | DESC_AF | DESC_BLOCK | DESC_VALID;

    /* Step 7: VirtIO MMIO (0x0A000000) — 4KB pages via L3 */
    l2_table_user[ 80 ] = (uint64_t)l3_table_mmio | DESC_TABLE | DESC_VALID;
    for (int i = 0; i < 32; i++) {
        l3_table_mmio[ i ] = (VIRTIO_MMIO_BASE + ((uint64_t)i * 4096))
                           | ATTR_DEVICE | AP_RW_EL1 | DESC_AF | DESC_PAGE | DESC_VALID;
    }

    /* Step 8: Configure system registers and enable MMU */
    uint64_t mair = (0xFFULL << 8) | (0x00ULL << 0);
    uint64_t tcr  = (25ULL << 0)  | (1ULL << 8)  | (1ULL << 10) |
                    (3ULL << 12)  | (0ULL << 14) | (1ULL << 23) | (2ULL << 32);

    __asm__ volatile (
        "msr mair_el1,  %0\n\t"
        "msr tcr_el1,   %1\n\t"
        "msr ttbr0_el1, %2\n\t"
        "dsb sy\n\t"
        "isb\n\t"
        "tlbi vmalle1\n\t"
        "dsb sy\n\t"
        "isb\n\t"
        "mrs x0, sctlr_el1\n\t"
        "orr x0, x0, #(1 << 0)\n\t"
        "orr x0, x0, #(1 << 2)\n\t"
        "orr x0, x0, #(1 << 12)\n\t"
        "msr sctlr_el1, x0\n\t"
        "isb\n\t"
        : : "r"(mair), "r"(tcr), "r"((uint64_t)l1_table) : "x0", "memory"
    );
}

void mmu_map_framebuffer(uint64_t phys_addr, uint64_t size) {
    if (!phys_addr) return;

    /* 256 covers 0x20000000 - 0x201FFFFF (First 2MB)
       257 covers 0x20200000 - 0x203FFFFF (Next 2MB) */
    l2_table_user[ 256 ] = (uint64_t)l3_table_fb_0 | DESC_TABLE | DESC_VALID;
    l2_table_user[ 257 ] = (uint64_t)l3_table_fb_1 | DESC_TABLE | DESC_VALID;

    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
        uint32_t page_idx = (uint32_t)(offset / PAGE_SIZE);
        
        if (page_idx < 512) {
            l3_table_fb_0[ page_idx ] = (phys_addr + offset)
                                      | ATTR_NORMAL | AP_RW_ANY | DESC_AF | DESC_PAGE | DESC_VALID;
        } 
        else if (page_idx < 1024) {
            l3_table_fb_1[ page_idx - 512 ] = (phys_addr + offset)
                                            | ATTR_NORMAL | AP_RW_ANY | DESC_AF | DESC_PAGE | DESC_VALID;
        }
    }

    __asm__ volatile(
        "dsb sy\n\t"
        "tlbi vmalle1\n\t"
        "dsb sy\n\t"
        "isb\n\t"
        ::: "memory"
    );
}