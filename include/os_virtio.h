#ifndef OS_VIRTIO_H
#define OS_VIRTIO_H

#include "os_types.h"

#define VIRTIO_MMIO_BASE       0x0a000000
#define VIRTIO_MMIO_SLOT_SIZE  0x200
#define VIRTIO_MMIO_SLOT_COUNT 32

#define VIRTIO_MAGIC 0x74726976

#define VIRTIO_DEV_BLK   2
#define VIRTIO_DEV_INPUT 18
#define VIRTIO_DEV_GPU   16

/* Block Request Types */
#define VIRTIO_BLK_T_IN          0
#define VIRTIO_BLK_T_OUT         1

/* Block Status */
#define VIRTIO_BLK_S_OK          0

#define VIRTQ_SIZE 256

#define VIRTIO_REG_MAGICVALUE     0x000
#define VIRTIO_REG_VERSION        0x004
#define VIRTIO_REG_DEVICEID       0x008
#define VIRTIO_REG_VENDORID       0x00c
#define VIRTIO_REG_STATUS         0x070
#define VIRTIO_REG_QUEUE_SEL      0x030
#define VIRTIO_REG_QUEUE_NUM      0x038
#define VIRTIO_REG_QUEUE_DESC_LOW 0x080
#define VIRTIO_REG_QUEUE_DESC_HIGH 0x084
#define VIRTIO_REG_QUEUE_AVAIL_LOW 0x090
#define VIRTIO_REG_QUEUE_AVAIL_HIGH 0x094
#define VIRTIO_REG_QUEUE_USED_LOW 0x0a0
#define VIRTIO_REG_QUEUE_USED_HIGH 0x0a4
#define VIRTIO_REG_QUEUE_READY    0x044
#define VIRTIO_REG_QUEUE_NOTIFY   0x050

#define VIRTIO_STATUS_ACKNOWLEDGE 0x01
#define VIRTIO_STATUS_DRIVER      0x02
#define VIRTIO_STATUS_DRIVER_OK   0x04
#define VIRTIO_STATUS_FEATURES_OK 0x08

#define VIRTQ_DESC_F_NEXT  1
#define VIRTQ_DESC_F_WRITE 2

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

/* FIX: Added volatile to ensure GCC doesn't optimize away memory polling */
struct virtq_avail {
    volatile uint16_t flags;
    volatile uint16_t idx;
    volatile uint16_t ring[VIRTQ_SIZE];
} __attribute__((packed));

struct virtq_used_elem {
    volatile uint32_t id;
    volatile uint32_t len;
} __attribute__((packed));

/* FIX: Added volatile to ensure GCC doesn't optimize away memory polling */
struct virtq_used {
    volatile uint16_t flags;
    volatile uint16_t idx;
    struct virtq_used_elem ring[VIRTQ_SIZE];
} __attribute__((packed));

/* --- VIRTIO BLOCK --- */
#define VIRTIO_BLK_T_IN  0
#define VIRTIO_BLK_T_OUT 1

struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));

/* --- VIRTIO INPUT --- */
#define VIRTIO_INPUT_EV_KEY 1

struct virtio_input_event {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} __attribute__((packed));

/* --- VIRTIO GPU --- */
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO    0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D  0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF      0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT         0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH      0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106

#define VIRTIO_GPU_RESP_OK_NODATA          0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO    0x1101

#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM   1
#define VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM   3
#define VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM   4

#define VIRTIO_GPU_MAX_SCANOUTS 16

struct virtio_gpu_ctrl_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct virtio_gpu_resp_display_info {
    struct virtio_gpu_ctrl_hdr hdr;
    struct {
        struct virtio_gpu_rect r;
        uint32_t enabled;
        uint32_t flags;
    } pmodes[VIRTIO_GPU_MAX_SCANOUTS];
} __attribute__((packed));

struct virtio_gpu_resource_create_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct virtio_gpu_set_scanout {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed));

struct virtio_gpu_transfer_to_host_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_flush {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_mem_entry {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_attach_backing {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
} __attribute__((packed));

void virtio_probe_and_init(void);
char virtio_input_poll(void);
void virtio_gpu_flush(void);

int virtio_blk_read_sector(uint64_t sector, void *buffer);
int virtio_blk_write_sector(uint64_t sector, const void *buffer);

#endif