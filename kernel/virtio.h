#ifndef OS_VIRTIO_H
#define OS_VIRTIO_H

#include "os_types.h"

#define VIRTIO_MMIO_BASE        0x0A000000
#define VIRTIO_MMIO_SLOT_SIZE   0x200
#define VIRTIO_MMIO_SLOT_COUNT  32
#define VIRTIO_IRQ_BASE         48

#define VIRTIO_MAGIC            0x74726976
#define VIRTIO_DEV_GPU          16
#define VIRTIO_DEV_INPUT        18

#define VIRTIO_STATUS_RESET           0x00
#define VIRTIO_STATUS_ACKNOWLEDGE     0x01
#define VIRTIO_STATUS_DRIVER          0x02
#define VIRTIO_STATUS_DRIVER_OK       0x04
#define VIRTIO_STATUS_FEATURES_OK     0x08

#define VIRTIO_REG_MAGICVALUE         0x000
#define VIRTIO_REG_VERSION            0x004
#define VIRTIO_REG_DEVICEID           0x008
#define VIRTIO_REG_VENDORID           0x00C
#define VIRTIO_REG_DEVICE_FEATURES    0x010
#define VIRTIO_REG_DRIVER_FEATURES    0x020
#define VIRTIO_REG_QUEUE_SEL          0x030
#define VIRTIO_REG_QUEUE_NUM_MAX      0x034
#define VIRTIO_REG_QUEUE_NUM          0x038
#define VIRTIO_REG_QUEUE_READY        0x044
#define VIRTIO_REG_STATUS             0x070
#define VIRTIO_REG_QUEUE_DESC_LOW     0x080
#define VIRTIO_REG_QUEUE_DESC_HIGH    0x084
#define VIRTIO_REG_QUEUE_AVAIL_LOW    0x090
#define VIRTIO_REG_QUEUE_AVAIL_HIGH   0x094
#define VIRTIO_REG_QUEUE_USED_LOW     0x0A0
#define VIRTIO_REG_QUEUE_USED_HIGH    0x0A4

/* Phase 2: Virtqueue Ring Structures */
#define VIRTQ_SIZE 256

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTQ_SIZE];
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[VIRTQ_SIZE];
} __attribute__((packed));

void virtio_probe_and_init(void);

#endif