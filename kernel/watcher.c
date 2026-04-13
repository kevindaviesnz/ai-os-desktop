#include "os_watcher.h"
#include "os_fat32.h"

extern void uart_print(const char *str);
extern void uart_print_hex(uint32_t val);

static watcher_state_t watcher;
static uint32_t sync_cursor = 0; /* QA FIX: High-water mark for idempotency */

static uint64_t get_system_timer(void) {
    uint64_t cntpct;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(cntpct));
    return cntpct;
}

void watcher_init(void) {
    watcher.head = 0;
    watcher.tail = 0;
    watcher.count = 0;
    sync_cursor = 0;
    
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
    
    watcher.events[ idx ].timestamp = get_system_timer();
    watcher.events[ idx ].type = type;
    watcher.events[ idx ].process_id = pid;
    
    int i = 0;
    if (context_data) {
        while (context_data[ i ] != '\0' && i < 63) {
            watcher.events[ idx ].context[ i ] = context_data[ i ];
            i++;
        }
    }
    watcher.events[ idx ].context[ i ] = '\0';

    watcher.head = (watcher.head + 1) % WATCHER_HISTORY_MAX;
    
    if (watcher.count < WATCHER_HISTORY_MAX) {
        watcher.count++;
    } else {
        uint32_t old_tail = watcher.tail;
        watcher.tail = (watcher.tail + 1) % WATCHER_HISTORY_MAX;
        
        /* QA FIX: If the tail eats the sync cursor, push the cursor forward 
           to prevent syncing overwritten/corrupted memory slots. */
        if (sync_cursor == old_tail) {
            sync_cursor = watcher.tail;
        }
    }
}

/* ... [watcher_dump_history, u64_to_hex_str, i32_to_dec_str, watcher_commit_ledger remain unchanged] ... */

/* PHASE 15.1: Idempotent Persistence Bridge */
void watcher_sync_to_disk(void) {
    uart_print("[WATCHER] Flushing Ledger to LEDGER.LOG...\n");
    
    if (sync_cursor == watcher.head) {
        uart_print("[WATCHER] Ledger is up to date. No new entries to sync.\n");
        return;
    }

    uint32_t current = sync_cursor;
    uint32_t synced_count = 0;

    while (current != watcher.head) {
        if (watcher.events[ current ].type == EVENT_TYPE_LEDGER_COMMIT) {
            int len = 0;
            while(watcher.events[ current ].context[ len ] != '\0') len++;
            
            fs_append_file_content("LEDGER.LOG", watcher.events[ current ].context, len);
            fs_append_file_content("LEDGER.LOG", "\n", 1);
            synced_count++;
        }
        current = (current + 1) % WATCHER_HISTORY_MAX;
    }
    
    /* QA FIX: Update high-water mark */
    sync_cursor = watcher.head;
    
    uart_print("[WATCHER] Sync Complete. ");
    uart_print_hex(synced_count);
    uart_print(" new entries persisted.\n");
}