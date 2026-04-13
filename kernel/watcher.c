#include "os_watcher.h"
#include "os_fat32.h"

/* MANUAL OVERRIDE: Tell the compiler this exists regardless of header state */
extern void fs_append_file_content(const char *filename, const char *data, uint32_t length);

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

/* --- BARE METAL STRING FORMATTERS --- */
/* Converts a 64-bit hash into a hex string */
static void u64_to_hex_str(uint64_t val, char* buf) {
    const char hex_chars[] = "0123456789ABCDEF";
    for (int i = 15; i >= 0; i--) {
        buf[ i ] = hex_chars[ val & 0xF ];
        val >>= 4;
    }
    buf[ 16 ] = '\0';
}

/* Converts a 32-bit signed volume into a decimal string */
static void i32_to_dec_str(int32_t val, char* buf) {
    if (val == 0) { buf[ 0 ] = '0'; buf[ 1 ] = '\0'; return; }
    int i = 0, is_neg = 0, ti = 0;
    char temp[ 16 ];
    
    if (val < 0) { is_neg = 1; val = -val; }
    while(val > 0) {
        temp[ ti++ ] = (val % 10) + '0';
        val /= 10;
    }
    if (is_neg) temp[ ti++ ] = '-';
    while(ti > 0) {
        buf[ i++ ] = temp[ --ti ];
    }
    buf[ i ] = '\0';
}

/* --- THE LEDGER VAULT API --- */
/* This is called directly across the FFI boundary from Rust */
void watcher_commit_ledger(uint64_t tx_hash, int32_t volume) {
    char context_buf[ 64 ];
    char hash_str[ 17 ];
    char vol_str[ 16 ];

    u64_to_hex_str(tx_hash, hash_str);
    i32_to_dec_str(volume, vol_str);

    /* Manually concatenate: "TX: 0x[HASH] | VOL: [VOLUME]" */
    int idx = 0;
    const char* p1 = "TX: 0x";
    while(*p1) context_buf[ idx++ ] = *p1++;
    
    char* h = hash_str;
    while(*h) context_buf[ idx++ ] = *h++;
    
    const char* p2 = " | VOL: ";
    while(*p2) context_buf[ idx++ ] = *p2++;
    
    char* v = vol_str;
    while(*v) context_buf[ idx++ ] = *v++;
    context_buf[ idx ] = '\0';

    /* Commit to the Sovereign Memory Slot (PID 0 = Kernel/VM) */
    watcher_log_event(EVENT_TYPE_LEDGER_COMMIT, 0, context_buf);
    
    /* Print the cryptographic receipt to the serial console */
    uart_print("[LEDGER] Receipt Committed: ");
    uart_print(context_buf);
    uart_print("\n");
}

/* PHASE 15: Persistence Bridge */
/* Flushes the current volatile ledger entries to LEDGER.LOG on disk */
void watcher_sync_to_disk(void) {
    uart_print("[WATCHER] Flushing Ledger to LEDGER.LOG...\n");
    
    if (watcher.count == 0) {
        uart_print("[WATCHER] No entries to sync.\n");
        return;
    }

    uint32_t current = watcher.tail;
    uint32_t synced_count = 0;

    for (uint32_t i = 0; i < watcher.count; i++) {
        /* Only sync high-integrity LEDGER_COMMIT types */
        if (watcher.events[ current ].type == EVENT_TYPE_LEDGER_COMMIT) {
            /* Append the context string (the formatted hash and volume) */
            int len = 0;
            while(watcher.events[ current ].context[ len ] != '\0') len++;
            
            fs_append_file_content("LEDGER.LOG", watcher.events[ current ].context, len);
            fs_append_file_content("LEDGER.LOG", "\n", 1);
            synced_count++;
        }
        current = (current + 1) % WATCHER_HISTORY_MAX;
    }
    
    uart_print("[WATCHER] Sync Complete. ");
    uart_print_hex(synced_count);
    uart_print(" entries persisted.\n");
}