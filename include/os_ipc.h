#ifndef OS_IPC_H
#define OS_IPC_H

#include "os_types.h"

#define SYS_MOD_KERNEL    0
#define SYS_MOD_UART      1
#define SYS_MOD_GUI_SHELL 2
#define SYS_MOD_FS        3
#define SYS_MOD_ISO7816   4

/* Syscall Numbers */
#define SYS_IPC_SEND  1
#define SYS_IPC_RECV  2
#define SYS_GPU_FLUSH 3

/* IPC Return Codes */
#define IPC_SUCCESS          0
#define IPC_ERR_QUEUE_FULL  -1

/* IPC Message Types */
#define IPC_MSG_NONE          0
#define IPC_MSG_KEY_PRESS     1
#define IPC_MSG_PROC_SPAWN    2
#define IPC_MSG_PROC_KILL     3
#define IPC_MSG_FS_LIST_REQ   4
#define IPC_MSG_FS_LIST_RESP  5
#define IPC_MSG_FS_READ_REQ   6
#define IPC_MSG_FS_READ_RESP  7
#define IPC_MSG_FS_WRITE_REQ  8
#define IPC_MSG_FS_WRITE_RESP 9
#define IPC_MSG_FS_WRITE_REQ  8
#define IPC_MSG_FS_WRITE_RESP 9
#define IPC_MSG_WATCHER_DUMP_REQ  10  /* Add this */
#define IPC_MSG_WATCHER_DUMP_RESP 11  /* Add this */

typedef struct {
    uint32_t sender_id;
    uint32_t target_id;
    uint32_t type;
    uint32_t length;
    uint8_t  payload[ 256 ];
} os_message_t;

void sys_ipc_send(os_message_t *msg);
void ipc_receive(os_message_t *msg);

#endif