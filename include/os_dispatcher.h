#ifndef OS_DISPATCHER_H
#define OS_DISPATCHER_H

#include "os_ipc.h"

/* Canonical Error Codes */
#define IPC_OK            0
#define IPC_ERR_INVALID   1
#define IPC_ERR_DENIED    2
#define IPC_ERR_FULL      3
#define IPC_ERR_EMPTY     4

extern const uint8_t capability_matrix [ MODULE_COUNT ] [ MODULE_COUNT ];

int ipc_send(uint32_t sender_id, const os_message_t *msg);
void try_dispatch_next(uint64_t *frame);

#endif