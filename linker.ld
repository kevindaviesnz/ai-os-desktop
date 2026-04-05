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
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        
        /* CLEANED: Removed legacy Smartcard OS hardcoded table allocations 
           which were fighting with our mmu.c static arrays! */
        
        . = ALIGN(4096);
        _bss_end = .;
    } > RAM

    /* KERNEL STACK */
    . = ALIGN(16);
    . += 0x4000;
    _stack_top = .;

    /* --- EL0 USER SPACE REGION --- */
    
    /* EL0 MODULE CODE */
    .el0_text : ALIGN(4096) {
        _el0_text_start = .;
        KEEP(*(.el0_user_text))
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
}