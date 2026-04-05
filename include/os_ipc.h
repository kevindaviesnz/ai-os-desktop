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

#ifndef STATIC_ASSERT
#define STATIC_ASSERT(COND, MSG) typedef char static_assertion_##MSG[(COND)?1:-1]
#endif
STATIC_ASSERT(SYS_MOD_GUI_SHELL < MODULE_COUNT, gui_shell_id_bounds);

#define IPC_OK            0
#define IPC_ERR_INVALID   1
#define IPC_ERR_DENIED    2
#define IPC_ERR_FULL      3
#define IPC_ERR_EMPTY     4

#define IPC_PAYLOAD_MAX_SIZE 32

#define SYS_IPC_SEND         1
#define SYS_IPC_RECEIVE      2
#define SYS_MODULE_REGISTER  3
#define SYS_INIT_DONE        4
#define SYS_HANDLER_DONE     5
#define SYS_CACHE_CLEAN      6

/* PHASE 2: Graphics IPC Commands */
#define IPC_TYPE_DRAW_RECT   10
#define IPC_TYPE_DRAW_BITMAP 11

typedef struct {
    uint32_t sender_id;
    uint32_t target_id;
    uint32_t type;
    uint32_t length;
    uint8_t  payload[ IPC_PAYLOAD_MAX_SIZE ];
} os_message_t;

/* Phase 2: Payloads mapped into os_message_t.payload */
typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t argb_color;
} ipc_draw_rect_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t argb_color;
    uint8_t  glyph_data[ 8 ]; /* 8x8 monochrome bitmap */
} ipc_draw_bitmap_t;

#endif