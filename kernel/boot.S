/* Allocate a safe 16KB stack in RAM */
.section .bss
.balign 16
stack_bottom:
.skip 16384  
stack_top:

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

    /* 3. Enable FP/SIMD to prevent compiler zeroing loops from trapping */
    mrs     x0, cpacr_el1
    orr     x0, x0, #(3 << 20)
    msr     cpacr_el1, x0

    /* 4. Zero the BSS section to initialize C globals safely */
    ldr     x1, =_bss_start
    ldr     x2, =_bss_end
1:  cmp     x1, x2
    b.ge    2f
    str     xzr, [x1], #8
    b       1b
2:

    /* 5. Load the Exception Vector Table */
    ldr     x0, =vectors
    msr     vbar_el1, x0

    /* 6. Drop into the C kernel */
    bl      kernel_main

halt:
    wfi
    b       halt