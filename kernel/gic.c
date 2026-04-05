#include "os_types.h"

#define GICD_BASE 0x08000000ULL
#define GICC_BASE 0x08010000ULL

void gic_init(void) {
    volatile uint32_t *gicd_ctlr = (volatile uint32_t *)GICD_BASE;
    volatile uint32_t *gicc_ctlr = (volatile uint32_t *)GICC_BASE;
    volatile uint32_t *gicc_pmr  = (volatile uint32_t *)(GICC_BASE + 0x0004);

    *gicd_ctlr = 1;       
    *gicc_pmr = 0xFFFF;   
    *gicc_ctlr = 1;       
}

void timer_init(void) {}

void irq_handler(uint64_t *regs) {
    (void)regs;
}