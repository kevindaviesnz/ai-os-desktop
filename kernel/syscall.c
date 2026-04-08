#include "os_ipc.h"
#include "os_types.h"
#include "os_virtio.h"

#define REG_X0 0
#define REG_X1 1
#define REG_X8 8

/* --- Phase 5: Global IPC Mailbox --- */
#define IPC_QUEUE_SIZE 256
static os_message_t ipc_queue[ IPC_QUEUE_SIZE ];
static volatile uint32_t ipc_head = 0;
static volatile uint32_t ipc_tail = 0;

void ipc_kernel_send(os_message_t *msg) {
    uint32_t next_tail = (ipc_tail + 1) % IPC_QUEUE_SIZE;
    if (next_tail != ipc_head) { /* Drop message if queue is full */
        ipc_queue[ ipc_tail ] = *msg;
        ipc_tail = next_tail;
    }
}

/* ----------------------------------- */

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

/* Hardware Maintenance Poller: Drains VirtIO into the Mailbox */
static void drain_hardware_queues(void) {
    char c;
    while ((c = virtio_input_poll()) != 0) {
        os_message_t hw_msg;
        hw_msg.type = IPC_MSG_KEY_PRESS;
        hw_msg.length = 1;
        hw_msg.payload[ 0 ] = (uint8_t)c;
        ipc_kernel_send(&hw_msg);
    }
}

void syscall_handler(uint64_t *sp) {
    /* * sp[ 8 ] contains register x8 (Syscall Number)
     * sp[ 0 ] contains register x0 (Arg 0, usually the struct pointer)
     */
    uint64_t syscall_num = sp[ 8 ];
    uint64_t arg0 = sp[ 0 ];

    /* Phase 5: Always drain hardware queues on kernel entry */
    drain_hardware_queues();

    if (syscall_num == SYS_GPU_FLUSH) {
        virtio_gpu_flush();
    }
    else if (syscall_num == SYS_IPC_RECV) {
        /* DEFERRED-12: Bounds checking stubbed */
        /* if (!is_valid_el0_pointer(arg0, sizeof(os_message_t))) kpanic("Invalid IPC ptr"); */

        os_message_t *msg = (os_message_t *)arg0;

        /* Pop from mailbox if not empty */
        if (ipc_head != ipc_tail) {
            *msg = ipc_queue[ ipc_head ];
            ipc_head = (ipc_head + 1) % IPC_QUEUE_SIZE;
        } else {
            msg->type = 0; /* IPC_MSG_NONE */
            msg->length = 0;
        }
    }
}