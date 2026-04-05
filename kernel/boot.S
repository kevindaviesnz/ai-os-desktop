/* Allocate a safe 16KB stack in RAM */
.section .bss
.balign 16          /* Resolves BLOCKER-23: Aligns to exactly 16 bytes */
stack_bottom:
.skip 16384  
stack_top:          /* Resolves ADVISORY-11: Establishes local source of truth */

.section .text.boot
.global _start

_start:
    /* 1. Halt secondary CPU cores */
    mrs     x1, mpidr_el1
    and     x1, x1, #0xFF
    cbz     x1, master_core
hang:
    wfe
    b       hang

master_core:
    /* 2. Setup EL1 stack pointer safely inside mapped RAM */
    ldr     x0, =stack_top
    mov     sp, x0

    /* 3. Load the Exception Vector Table */
    ldr     x0, =vectors
    msr     vbar_el1, x0

    /* PHASE 1 PATCH: MMU ENABLE SEQUENCE BYPASSED */
    /*
    bl      mmu_init_tables
    mrs     x0, sctlr_el1
    ldr     x1, =(1 << 0) | (1 << 2) | (1 << 12)  
    orr     x0, x0, x1
    msr     sctlr_el1, x0
    isb
    */

    /* 4. Drop into the C kernel */
    bl      kernel_main

halt:
    wfi
    b       halt