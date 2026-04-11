#include "os_ipc.h"
#include "os_types.h"
#include "os_virtio.h"

#define REG_X0 0
#define REG_X1 1
#define REG_X8 8

/* --- Global IPC Mailbox --- */
#define IPC_QUEUE_SIZE 256
static os_message_t ipc_queue[ IPC_QUEUE_SIZE ];
static volatile uint32_t ipc_head = 0;
static volatile uint32_t ipc_tail = 0;

void ipc_kernel_send(os_message_t *msg) {
    uint32_t next_tail = (ipc_tail + 1) % IPC_QUEUE_SIZE;
    if (next_tail != ipc_head) { 
        ipc_queue[ ipc_tail ] = *msg;
        ipc_tail = next_tail;
    }
}

const uint8_t capability_matrix[ 9 ][ 9 ] = {
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

extern void virtio_gpu_flush(void);
extern char virtio_input_poll(void);

static void drain_hardware_queues(void) {
    char c;
    while ((c = virtio_input_poll()) != 0) {
        os_message_t hw_msg;
        hw_msg.sender_id = SYS_MOD_KERNEL;
        hw_msg.target_id = SYS_MOD_GUI_SHELL;
        hw_msg.type = IPC_MSG_KEY_PRESS;
        hw_msg.length = 1;
        hw_msg.payload[ 0 ] = (uint8_t)c;
        ipc_kernel_send(&hw_msg);
    }
}

void syscall_handler(uint64_t *sp) {
    uint64_t syscall_num = sp[ 8 ];
    uint64_t arg0        = sp[ 0 ];

    /* [QA REVERT]: Hardware must be drained on EVERY syscall to prevent input deadlocks */
    drain_hardware_queues();

    if (syscall_num == SYS_GPU_FLUSH) {
        virtio_gpu_flush();
    }
    else if (syscall_num == SYS_IPC_SEND) {
        os_message_t *in_msg = (os_message_t *)arg0;

        if (in_msg->target_id == SYS_MOD_KERNEL) {

            if (in_msg->type == IPC_MSG_FS_LIST_REQ) {
                char dir_buf[ 256 ];
                extern void fs_get_dir_list(char *buffer, uint32_t max_len);
                fs_get_dir_list(dir_buf, 256);

                os_message_t out_msg;
                out_msg.sender_id = SYS_MOD_KERNEL;
                out_msg.target_id = in_msg->sender_id;
                out_msg.type      = IPC_MSG_FS_LIST_RESP;

                uint32_t len = 0;
                while (dir_buf[ len ] != '\0' && len < 255) {
                    out_msg.payload[ len ] = dir_buf[ len ];
                    len++;
                }
                out_msg.payload[ len ] = '\0';
                out_msg.length = len + 1;
                ipc_kernel_send(&out_msg);
            }
            else if (in_msg->type == IPC_MSG_FS_READ_REQ) {
                char file_buf[ 256 ];
                extern void fs_read_file_content(const char *filename, char *buffer, uint32_t max_len);
                fs_read_file_content((const char *)in_msg->payload, file_buf, 256);

                os_message_t out_msg;
                out_msg.sender_id = SYS_MOD_KERNEL;
                out_msg.target_id = in_msg->sender_id;
                out_msg.type      = IPC_MSG_FS_READ_RESP;

                uint32_t len = 0;
                while (file_buf[ len ] != '\0' && len < 255) {
                    out_msg.payload[ len ] = file_buf[ len ];
                    len++;
                }
                out_msg.payload[ len ] = '\0';
                out_msg.length = len + 1;
                ipc_kernel_send(&out_msg);
            }
            else if (in_msg->type == IPC_MSG_FS_WRITE_REQ) {
                char *filename = (char *)in_msg->payload;
                char *data     = filename;
                while (*data != '\0') data++;
                data++;

                uint32_t data_len = in_msg->length - (uint32_t)(data - (char *)in_msg->payload);

                extern void fs_write_file_content(const char *filename, const char *data, uint32_t length);
                fs_write_file_content(filename, data, data_len);

                os_message_t out_msg;
                out_msg.sender_id = SYS_MOD_KERNEL;
                out_msg.target_id = in_msg->sender_id;
                out_msg.type      = IPC_MSG_FS_WRITE_RESP;

                const char *success = "Write committed to disk.\n";
                uint32_t len = 0;
                while (success[ len ] != '\0') {
                    out_msg.payload[ len ] = success[ len ];
                    len++;
                }
                out_msg.payload[ len ] = '\0';
                out_msg.length = len + 1;
                ipc_kernel_send(&out_msg);
            }
        } else {
            ipc_kernel_send(in_msg);
        }
    }
    else if (syscall_num == SYS_IPC_RECV) {
        os_message_t *msg = (os_message_t *)arg0;
        if (ipc_head != ipc_tail) {
            *msg = ipc_queue[ ipc_head ];
            ipc_head = (ipc_head + 1) % IPC_QUEUE_SIZE;
        } else {
            msg->type   = IPC_MSG_NONE;
            msg->length = 0;
        }
    }
}