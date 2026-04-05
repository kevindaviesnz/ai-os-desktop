ENTRY(_start)
SECTIONS {
    /* PIE modules are linked at address 0 */
    . = 0;

    .text : {
        *(.text._start)
        *(.text*)
    }

    .rodata : ALIGN(8) {
        *(.rodata*)
    }

    /* Force .data to start on a new 4KB hardware page */
    .data : ALIGN(4096) {
        *(.data*)

        /* Capture GOT and dynamic symbols — prevents objcopy stripping */
        *(.got*)
        *(.got.plt*)
        *(.data.rel.ro*)
        *(.dynamic)

        /* Guarantee non-empty to prevent boundary collapse */
        BYTE(0)

        /* Pad to 4KB boundary — CRITICAL.
           Forces objcopy to export the full page so loader.c and the
           ELF's internal .bss offset are always synchronised.
           Without this, code_size is not page-aligned and .bss lands
           at the wrong address in physical memory. */
        . = ALIGN(4096);
    }

    .bss : ALIGN(4096) {
        *(.bss*)
        *(COMMON)
    }
}
