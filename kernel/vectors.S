.macro kernel_entry
    sub sp, sp, #288
    stp x0, x1, [ sp, #16 * 0 ]
    stp x2, x3, [ sp, #16 * 1 ]
    stp x4, x5, [ sp, #16 * 2 ]
    stp x6, x7, [ sp, #16 * 3 ]
    stp x8, x9, [ sp, #16 * 4 ]
    stp x10, x11, [ sp, #16 * 5 ]
    stp x12, x13, [ sp, #16 * 6 ]
    stp x14, x15, [ sp, #16 * 7 ]
    stp x16, x17, [ sp, #16 * 8 ]
    stp x18, x19, [ sp, #16 * 9 ]
    stp x20, x21, [ sp, #16 * 10 ]
    stp x22, x23, [ sp, #16 * 11 ]
    stp x24, x25, [ sp, #16 * 12 ]
    stp x26, x27, [ sp, #16 * 13 ]
    stp x28, x29, [ sp, #16 * 14 ]
    str x30, [ sp, #16 * 15 ]
    
    mrs x21, elr_el1
    mrs x22, spsr_el1
    stp x21, x22, [ sp, #16 * 16 ]
    
    mrs x21, sp_el0
    str x21, [ sp, #16 * 17 ]
.endm

.macro kernel_exit
    ldr x21, [ sp, #16 * 17 ]
    msr sp_el0, x21
    
    ldp x21, x22, [ sp, #16 * 16 ]
    msr elr_el1, x21
    msr spsr_el1, x22
    
    ldp x28, x29, [ sp, #16 * 14 ]
    ldr x30, [ sp, #16 * 15 ]
    ldp x26, x27, [ sp, #16 * 13 ]
    ldp x24, x25, [ sp, #16 * 12 ]
    ldp x22, x23, [ sp, #16 * 11 ]
    ldp x20, x21, [ sp, #16 * 10 ]
    ldp x18, x19, [ sp, #16 * 9 ]
    ldp x16, x17, [ sp, #16 * 8 ]
    ldp x14, x15, [ sp, #16 * 7 ]
    ldp x12, x13, [ sp, #16 * 6 ]
    ldp x10, x11, [ sp, #16 * 5 ]
    ldp x8, x9, [ sp, #16 * 4 ]
    ldp x6, x7, [ sp, #16 * 3 ]
    ldp x4, x5, [ sp, #16 * 2 ]
    ldp x2, x3, [ sp, #16 * 1 ]
    ldp x0, x1, [ sp, #16 * 0 ]
    add sp, sp, #288
    eret
.endm

.section .text.vectors
.global vectors
.balign 2048
vectors:
    .align 7; b default_handler 
    .align 7; b default_handler 
    .align 7; b default_handler 
    .align 7; b default_handler 

    .align 7; b kernel_sync     
    .align 7; b kernel_irq      
    .align 7; b default_handler 
    .align 7; b default_handler 

    .align 7; b el0_sync        
    .align 7; b el0_irq         
    .align 7; b default_handler 
    .align 7; b default_handler 

    .align 7; b default_handler 
    .align 7; b default_handler 
    .align 7; b default_handler 
    .align 7; b default_handler 

.section .text

kernel_sync:
    kernel_entry
    mov x0, sp
    bl syscall_handler
    kernel_exit

kernel_irq:
    kernel_entry
    mov x0, sp
    bl irq_handler
    kernel_exit

el0_sync:
    kernel_entry
    mov x0, sp
    bl syscall_handler
    kernel_exit

el0_irq:
    kernel_entry
    mov x0, sp
    bl irq_handler
    kernel_exit

default_handler:
    1:  b 1b

.global init_vectors
init_vectors:
    ldr x0, =vectors
    msr vbar_el1, x0
    isb
    ret