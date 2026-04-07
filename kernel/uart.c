#include "os_types.h"

#define UART0_BASE 0x09000000

void uart_putc(char c) {
    volatile uint32_t *uart_dr = (volatile uint32_t *)UART0_BASE;
    *uart_dr = c;
}

void uart_print(const char *str) {
    while (*str) uart_putc(*str++);
}

/* New: Hexadecimal formatter for dumping memory addresses and registers */
void uart_print_hex(uint64_t val) {
    uart_print("0x");
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (val >> i) & 0xF;
        if (nibble < 10) {
            uart_putc('0' + nibble);
        } else {
            uart_putc('A' + (nibble - 10));
        }
    }
}