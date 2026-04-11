#include "os_types.h"

/* QEMU Virt PL011 UART Base Address */
#define UART0_BASE 0x09000000
#define UART_DR    ((volatile uint32_t *)(UART0_BASE + 0x000))
#define UART_FR    ((volatile uint32_t *)(UART0_BASE + 0x018))

#define UART_FR_TXFF (1 << 5)  /* Transmit FIFO Full */
#define UART_FR_RXFE (1 << 4)  /* Receive FIFO Empty */

void uart_print(const char *str) {
    while (*str) {
        /* Wait if the transmit FIFO is full */
        while (*UART_FR & UART_FR_TXFF);
        *UART_DR = *str++;
    }
}

void uart_print_hex(uint64_t val) {
    const char *hex_chars = "0123456789ABCDEF";
    char buffer[ 19 ];
    buffer[ 0 ] = '0';
    buffer[ 1 ] = 'x';
    buffer[ 18 ] = '\0';
    
    for (int i = 15; i >= 0; i--) {
        buffer[ 2 + i ] = hex_chars[ val & 0xF ];
        val >>= 4;
    }
    uart_print(buffer);
}

/* --- NEW: The Agent Receiver --- */
char uart_poll_rx(void) {
    /* If the Receive FIFO Empty flag is NOT set, we have a byte! */
    if ((*UART_FR & UART_FR_RXFE) == 0) {
        return (char)(*UART_DR & 0xFF);
    }
    return 0;
}