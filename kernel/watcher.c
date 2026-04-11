#include "os_watcher.h"

extern void uart_print(const char *str);
extern void uart_print_hex(uint32_t val);

/* The global state of our Sovereign OS Memory */
static watcher_state_t watcher;

/* Hardware helper to read the AArch64 System Timer */
static uint64_t get_system_timer(void) {
    uint64_t cntpct;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(cntpct));
    return cntpct;
}

void watcher_init(void) {
    watcher.head = 0;
    watcher.tail = 0;
    watcher.count = 0;
    
    for(int i = 0; i < WATCHER_HISTORY_MAX; i++) {
        watcher.events[ i ].timestamp = 0;
        watcher.events[ i ].type = EVENT_TYPE_NONE;
        watcher.events[ i ].process_id = 0;
        watcher.events[ i ].context[ 0 ] = '\0';
    }
    uart_print("[WATCHER] Sovereign Memory Initialized. (64 Slots)\n");
}

void watcher_log_event(watcher_event_type_t type, uint32_t pid, const char *context_data) {
    uint32_t idx = watcher.head;
    
    /* 1. Record the Event */
    watcher.events[ idx ].timestamp = get_system_timer();
    watcher.events[ idx ].type = type;
    watcher.events[ idx ].process_id = pid;
    
    /* Safely copy the context string (e.g., "TEST.TXT") */
    int i = 0;
    if (context_data) {
        while (context_data[ i ] != '\0' && i < 63) {
            watcher.events[ idx ].context[ i ] = context_data[ i ];
            i++;
        }
    }
    watcher.events[ idx ].context[ i ] = '\0';

    /* 2. Advance the Head Pointer using Modulo */
    watcher.head = (watcher.head + 1) % WATCHER_HISTORY_MAX;
    
    /* 3. Handle a Full Buffer (Overwrite Oldest) */
    if (watcher.count < WATCHER_HISTORY_MAX) {
        watcher.count++;
    } else {
        watcher.tail = (watcher.tail + 1) % WATCHER_HISTORY_MAX; /* Push tail forward */
    }
}

void watcher_dump_history(void) {
    uart_print("\n=== WATCHER MEMORY DUMP ===\n");
    if (watcher.count == 0) {
        uart_print("No events logged.\n");
        return;
    }

    uint32_t current = watcher.tail;
    for (uint32_t i = 0; i < watcher.count; i++) {
        uart_print("Event [");
        uart_print_hex(i);
        uart_print("] Type: ");
        uart_print_hex(watcher.events[ current ].type);
        uart_print(" | Ctx: ");
        uart_print(watcher.events[ current ].context);
        uart_print("\n");
        
        current = (current + 1) % WATCHER_HISTORY_MAX;
    }
    uart_print("===========================\n");
}