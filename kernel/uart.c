#include "os_types.h"

#define UART0_BASE 0x09000000

void uart_putc(char c) {
    volatile uint32_t *uart_dr = (volatile uint32_t *)UART0_BASE;
    *uart_dr = c;
}

void uart_print(const char *str) {
    while (*str) uart_putc(*str++);
}