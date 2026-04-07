#ifndef OS_IPC_H
#define OS_IPC_H

#include "os_types.h"

#define SYS_MOD_KERNEL      0
#define SYS_MOD_UART        1
#define SYS_MOD_SHELL       2
#define SYS_MOD_FS          3
#define SYS_MOD_ISO7816     4
#define SYS_MOD_CARDSIM     5
#define SYS_MOD_COMPOSITOR  6
#define SYS_MOD_INPUT       7
#define SYS_MOD_GUI_SHELL   8

#define MODULE_COUNT        9

/* IPC Return Codes */
#define IPC_SUCCESS         0
#define IPC_ERROR          -1

#ifndef STATIC_ASSERT
#define STATIC_ASSERT(COND, MSG) typedef char static_assertion_##MSG[(COND)?1:-1]
#endif
STATIC_ASSERT(SYS_MOD_GUI_SHELL < MODULE_COUNT, gui_shell_id_bounds);

#define IPC_PAYLOAD_MAX_SIZE 32

/* Syscalls */
#define SYS_IPC_SEND       1
#define SYS_IPC_RECV       2
#define SYS_CACHE_CLEAN    6
#define SYS_BLK_READ       7
#define SYS_GPU_FLUSH      8

/* IPC Message Types */
#define IPC_MSG_KEY_PRESS  1
#define IPC_MSG_FILE_REQ   2
#define IPC_MSG_FILE_RESP  3

typedef struct {
    uint32_t sender_id;
    uint32_t target_id;
    uint32_t type;
    uint32_t length;
    uint8_t  payload[IPC_PAYLOAD_MAX_SIZE];
} os_message_t;

#endif