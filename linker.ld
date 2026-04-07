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

    .rodata : ALIGN(4096) {
        *(.rodata*)
    } > RAM

    .data : ALIGN(4096) {
        *(.data*)
    } > RAM

    .bss : ALIGN(4096) {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(16);
        _bss_end = .;
    } > RAM

    /* KERNEL STACK */
    . = ALIGN(16);
    . += 0x4000;
    _stack_top = .;

    /* --- EL0 USER SPACE REGION --- */

    /* Force the GNU Linker to push this specific section to the 2MB boundary */
    .el0_text ALIGN(0x200000) : {
        _el0_region_start = .;
        _el0_text_start = .;
        KEEP(*(.el0_entry))
        *(.el0_user_text)
        *(.el0_user_rodata)
    } > RAM

    .el0_stack ALIGN(4096) : {
        _el0_stack_guard = .;
        . += 4096; /* Unmapped Guard Page */
        _el0_stack_bottom = .;
        . += 0x4000; /* 16KB Stack */
        _el0_stack_top = .;
        . = ALIGN(0x200000);
        _el0_region_end = .;
    } > RAM
}