ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x40000000, LENGTH = 128M
}

SECTIONS
{
    . = 0x40000000;

    .text : {
        KEEP(*(.text.boot))

        . = ALIGN(2048);
        _vectors = .;
        KEEP(*(.text.vectors))

        *(.text*)
    } > RAM

    . = ALIGN(4096);
    _etext = .;

    .rodata : { *(.rodata*) } > RAM

    . = ALIGN(4096);
    _erodata = .;

    .data : { *(.data*) } > RAM

    .bss : {
        . = ALIGN(4096);
        . = ALIGN(16);
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4096);
        . = ALIGN(16);
        _bss_end = .;
    } > RAM

    /* KERNEL STACK */
    . = ALIGN(16);
    . += 0x4000;
    _stack_top = .;

    /* --- EL0 USER SPACE REGION --- */

    /* Force the entire EL0 region into its own 2MB block */
    . = ALIGN(0x200000); 
    _el0_region_start = .;

    .el0_text : ALIGN(4096) {
        _el0_text_start = .;
        KEEP(*(.el0_entry))
        *(.el0_user_text)
        *(.el0_user_rodata)
        . = ALIGN(4096);
        _el0_text_end = .;
    } > RAM

    /* EL0 STACK GUARD (Unmapped Page) */
    . = ALIGN(4096);
    _el0_stack_guard = .;
    . += 4096;

    /* EL0 STACK (16KB) */
    _el0_stack_bottom = .;
    . += 0x4000;
    _el0_stack_top = .;

    /* Mark the end of the region to guarantee the MMU grants stack permissions */
    . = ALIGN(0x200000);
    _el0_region_end = .;
}