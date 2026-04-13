#ifndef OS_WATCHER_H
#define OS_WATCHER_H

#include "os_types.h"

/* The maximum number of recent events the OS will remember. 
   Keeping this at 64 keeps our kernel memory footprint tiny. */
#define WATCHER_HISTORY_MAX 64

void watcher_sync_to_disk(void);

/* We categorize events so the AI knows *what* kind of action it was */
typedef enum {
    EVENT_TYPE_NONE = 0,
    EVENT_TYPE_KEYSTROKE,
    EVENT_TYPE_FS_READ,
    EVENT_TYPE_FS_WRITE,
    EVENT_TYPE_PROC_SPAWN,
    EVENT_TYPE_LEDGER_COMMIT  /* <--- NEW: Cryptographic VM Execution */
} watcher_event_type_t;

/* A single "Memory" of an event */
typedef struct {
    uint64_t timestamp;             /* When did this happen? (CPU Ticks) */
    watcher_event_type_t type;      /* What happened? */
    uint32_t process_id;            /* Who did it? (Usually the Shell) */
    char context[ 64 ];             /* Details (e.g., "TEST.TXT") */
} watcher_event_t;

/* The Circular Ring Buffer Data Structure */
typedef struct {
    watcher_event_t events[ WATCHER_HISTORY_MAX ];
    uint32_t head;                  /* Where we write the next event */
    uint32_t tail;                  /* The oldest event in memory */
    uint32_t count;                 /* How many events are currently stored */
} watcher_state_t;

/* Function Prototypes for the Kernel */
void watcher_init(void);
void watcher_log_event(watcher_event_type_t type, uint32_t pid, const char *context_data);
void watcher_dump_history(void); /* For debugging what the AI sees */

/* NEW: The Ledger API for the Rust VM */
void watcher_commit_ledger(uint64_t tx_hash, int32_t volume);

#endif