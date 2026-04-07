#include "os_ipc.h"
#include "os_types.h"
#include "os_virtio.h"

#define REG_X0 0
#define REG_X1 1
#define REG_X8 8

const uint8_t capability_matrix[ MODULE_COUNT ][ MODULE_COUNT ] = {
    /* KRN  UAR  SHL   FS  ISO  CRD  CMP  INP  GUI */
    /* KERNEL  */ { 1,   1,   1,   1,   1,   1,   1,   1,   1 },
    /* UART    */ { 1,   0,   1,   0,   0,   0,   0,   0,   0 },
    /* SHELL   */ { 0,   1,   0,   1,   1,   0,   0,   0,   0 },
    /* FS      */ { 0,   0,   1,   0,   0,   0,   0,   0,   1 },
    /* ISO7816 */ { 1,   0,   1,   0,   0,   1,   0,   0,   0 },
    /* CARDSIM */ { 0,   0,   0,   0,   1,   0,   0,   0,   0 },
    /* COMP    */ { 1,   1,   0,   0,   0,   0,   0,   0,   1 },
    /* INPUT   */ { 1,   1,   0,   0,   0,   0,   1,   0,   0 },
    /* GUI_SHL */ { 0,   1,   0,   1,   0,   0,   1,   0,   0 },
};

extern int get_region_for_current_module(void);
extern int is_valid_el0_pointer(uint64_t ptr, uint64_t size);
extern void kpanic(const char *msg);
extern int virtio_blk_read_sector(uint64_t sector, uint8_t *buffer);
extern void virtio_gpu_flush(void);
extern char virtio_input_poll(void);

void syscall_handler(uint64_t *sp) {
    /* * sp[ 8 ] contains register x8 (Syscall Number)
     * sp[ 0 ] contains register x0 (Arg 0, usually the struct pointer)
     */
    uint64_t syscall_num = sp[ 8 ];
    uint64_t arg0 = sp[ 0 ];

    if (syscall_num == SYS_GPU_FLUSH) {
        virtio_gpu_flush();
    }
    else if (syscall_num == SYS_IPC_RECV) {
        /* Future Phase: Re-enable pointer security validation here */
        /* if (!is_valid_el0_pointer(arg0, sizeof(os_message_t))) kpanic("Invalid IPC ptr"); */

        os_message_t *msg = (os_message_t *)arg0;
        char c = virtio_input_poll();

        if (c != 0) {
            /* Keystroke detected! Package it into the user's message struct */
            msg->type = IPC_MSG_KEY_PRESS;
            msg->length = 1;
            msg->payload[ 0 ] = (uint8_t)c;
        } else {
            /* No input ready. Tell the shell to keep waiting. */
            msg->type = 0; /* Assuming 0 represents IPC_MSG_NONE */
            msg->length = 0;
        }
    }
}