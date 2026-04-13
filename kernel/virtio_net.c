#include "os_types.h"

/* The VirtIO MMIO Bus */
#define VIRTIO_MMIO_BASE  0x0a000000
#define VIRTIO_MMIO_SIZE  0x200
#define VIRTIO_MMIO_MAX   32

#define VIRTIO_REG_MAGIC        0x000
#define VIRTIO_REG_VERSION      0x004  /* ADDED: Version checking */
#define VIRTIO_REG_DEVICE       0x008
#define VIRTIO_REG_STATUS       0x070
#define VIRTIO_REG_QUEUE_SEL    0x030
#define VIRTIO_REG_QUEUE_NUM    0x038
#define VIRTIO_REG_QUEUE_ALIGN  0x03c
#define VIRTIO_REG_QUEUE_PFN    0x040
#define VIRTIO_REG_QUEUE_NOTIFY 0x050

extern void uart_print(const char *str);
extern void uart_print_hex(uint64_t val);

static uint32_t net_base = 0;

/* --- VIRTQUEUE MEMORY STRUCTURES --- */
#define QUEUE_SIZE 8 
#define BUFFER_SIZE 256
#define PAGE_SIZE 4096

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[QUEUE_SIZE];
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[QUEUE_SIZE];
} __attribute__((packed));

static uint8_t vq_memory[2 * PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
static struct virtq_desc   *rx_desc;
static struct virtq_avail  *rx_avail;
static struct virtq_used   *rx_used;
static uint8_t rx_buffers[QUEUE_SIZE][BUFFER_SIZE];
static uint16_t last_used_idx = 0;

/* --- ARM64 CACHE COHERENCY HELPERS --- */
static void cache_flush(uint64_t addr, uint32_t size) {
    uint64_t start = addr & ~63ULL;
    uint64_t end = addr + size;
    for (uint64_t p = start; p < end; p += 64) {
        __asm__ volatile("dc cvac, %0" :: "r"(p) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

static void cache_invalidate(uint64_t addr, uint32_t size) {
    uint64_t start = addr & ~63ULL;
    uint64_t end = addr + size;
    for (uint64_t p = start; p < end; p += 64) {
        __asm__ volatile("dc ivac, %0" :: "r"(p) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

void virtio_net_init(void) {
    uart_print("[NET] Scanning MMIO bus for Network Card...\n");
    
    uint32_t version = 0;

    for (int i = 0; i < VIRTIO_MMIO_MAX; i++) {
        uint32_t current_base = VIRTIO_MMIO_BASE + (i * VIRTIO_MMIO_SIZE);
        volatile uint32_t *magic = (uint32_t *)(current_base + VIRTIO_REG_MAGIC);
        volatile uint32_t *dev_id = (uint32_t *)(current_base + VIRTIO_REG_DEVICE);
        
        if (*magic == 0x74726976 && *dev_id == 1) { 
            net_base = current_base;
            version = *(volatile uint32_t *)(current_base + VIRTIO_REG_VERSION);
            uart_print("[NET] Network Card found at slot: ");
            uart_print_hex(current_base);
            uart_print("\n");
            break;
        }
    }

    if (net_base == 0) return;

    volatile uint32_t *status = (uint32_t *)(net_base + VIRTIO_REG_STATUS);
    *status = 0; *status |= 1; *status |= 2; 

    /* THE CRITICAL FIX: Modern VirtIO Feature Negotiation */
    if (version != 1) {
        volatile uint32_t *drv_feat_sel = (volatile uint32_t *)(net_base + 0x024);
        volatile uint32_t *drv_feat     = (volatile uint32_t *)(net_base + 0x020);
        *drv_feat_sel = 0; *drv_feat = 0; *drv_feat_sel = 1; *drv_feat = 1;
        *status |= 8; // VIRTIO_STATUS_FEATURES_OK
    }

    rx_desc = (struct virtq_desc *)vq_memory;
    rx_avail = (struct virtq_avail *)(vq_memory + (QUEUE_SIZE * 16));
    rx_used = (struct virtq_used *)(vq_memory + PAGE_SIZE); 

    volatile uint32_t *q_sel = (uint32_t *)(net_base + VIRTIO_REG_QUEUE_SEL);
    *q_sel = 0; 

    /* THE CRITICAL FIX: Modern 64-bit Memory Mapping */
    if (version == 1) {
        volatile uint32_t *q_num   = (uint32_t *)(net_base + VIRTIO_REG_QUEUE_NUM);
        volatile uint32_t *q_align = (uint32_t *)(net_base + VIRTIO_REG_QUEUE_ALIGN);
        volatile uint32_t *q_pfn   = (uint32_t *)(net_base + VIRTIO_REG_QUEUE_PFN);
        volatile uint32_t *guest_page_size = (uint32_t *)(net_base + 0x028);

        *q_num = QUEUE_SIZE;
        *guest_page_size = PAGE_SIZE;
        *q_align = PAGE_SIZE;
        *q_pfn = ((uint64_t)vq_memory) / PAGE_SIZE;
    } else {
        volatile uint32_t *q_num        = (uint32_t *)(net_base + VIRTIO_REG_QUEUE_NUM);
        volatile uint32_t *q_desc_low   = (uint32_t *)(net_base + 0x080);
        volatile uint32_t *q_desc_high  = (uint32_t *)(net_base + 0x084);
        volatile uint32_t *q_avail_low  = (uint32_t *)(net_base + 0x090);
        volatile uint32_t *q_avail_high = (uint32_t *)(net_base + 0x094);
        volatile uint32_t *q_used_low   = (uint32_t *)(net_base + 0x0a0);
        volatile uint32_t *q_used_high  = (uint32_t *)(net_base + 0x0a4);
        volatile uint32_t *q_ready      = (uint32_t *)(net_base + 0x044);

        *q_num = QUEUE_SIZE;
        *q_desc_low   = (uint32_t)(((uint64_t)rx_desc) & 0xFFFFFFFF);
        *q_desc_high  = (uint32_t)(((uint64_t)rx_desc) >> 32);
        *q_avail_low  = (uint32_t)(((uint64_t)rx_avail) & 0xFFFFFFFF);
        *q_avail_high = (uint32_t)(((uint64_t)rx_avail) >> 32);
        *q_used_low   = (uint32_t)(((uint64_t)rx_used) & 0xFFFFFFFF);
        *q_used_high  = (uint32_t)(((uint64_t)rx_used) >> 32);
        *q_ready = 1;
    }

    for (int i = 0; i < QUEUE_SIZE; i++) {
        rx_desc[i].addr = (uint64_t)rx_buffers[i];
        rx_desc[i].len = BUFFER_SIZE;
        rx_desc[i].flags = 2; // VRING_DESC_F_WRITE
        rx_desc[i].next = 0;
        rx_avail->ring[i] = i; 
    }
    rx_avail->idx = QUEUE_SIZE; 

    cache_flush((uint64_t)rx_desc, sizeof(struct virtq_desc) * QUEUE_SIZE);
    cache_flush((uint64_t)rx_avail, sizeof(struct virtq_avail));

    volatile uint32_t *q_notify = (uint32_t *)(net_base + VIRTIO_REG_QUEUE_NOTIFY);
    *q_notify = 0; 

    *status |= 4; // DRIVER_OK
    uart_print("[NET] RX VirtQueue established. Listening for FIX packets...\n");
}

int virtio_net_poll_rx(void) {
    if (net_base == 0) return 0;

    cache_invalidate((uint64_t)&rx_used->idx, 2);

    if (last_used_idx != rx_used->idx) {
        
        cache_invalidate((uint64_t)&rx_used->ring[last_used_idx % QUEUE_SIZE], sizeof(struct virtq_used_elem));
        
        uint16_t desc_id = rx_used->ring[last_used_idx % QUEUE_SIZE].id;
        uint32_t pkt_len = rx_used->ring[last_used_idx % QUEUE_SIZE].len;
        
        uint8_t *payload = rx_buffers[desc_id];
        cache_invalidate((uint64_t)payload, BUFFER_SIZE);

        char *fix_data = (char *)(payload + 26); 
        
        uart_print("[NET] Packet received! Payload: ");
        uart_print(fix_data);
        uart_print("\n");

        int volume = 0;
        if (pkt_len > 28) {
            for (int i = 0; i < (int)pkt_len - 28; i++) {
                if (fix_data[i] == '3' && fix_data[i+1] == '8' && fix_data[i+2] == '=') {
                    int j = i + 3;
                    while (fix_data[j] >= '0' && fix_data[j] <= '9') {
                        volume = volume * 10 + (fix_data[j] - '0');
                        j++;
                    }
                    break;
                }
            }
        }

        last_used_idx++; 

        rx_avail->ring[rx_avail->idx % QUEUE_SIZE] = desc_id;
        cache_flush((uint64_t)&rx_avail->ring[rx_avail->idx % QUEUE_SIZE], 2);
        
        rx_avail->idx++;
        cache_flush((uint64_t)&rx_avail->idx, 2);
        
        volatile uint32_t *q_notify = (uint32_t *)(net_base + VIRTIO_REG_QUEUE_NOTIFY);
        *q_notify = 0; 

        return volume;
    }
    return 0; 
}