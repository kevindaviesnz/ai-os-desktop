#include "os_virtio.h"

extern void uart_print(const char *str);
extern void mmu_map_framebuffer(uint64_t phys_addr, uint64_t size);
extern void kpanic(const char *msg);
extern void uart_print_hex(uint32_t val);

/* --- Global GPU Pointers --- */
static volatile struct virtq_desc *gpu_desc;
static volatile struct virtq_avail *gpu_avail;
static volatile struct virtq_used *gpu_used;
static uint16_t gpu_last_used_idx = 0;
static uint64_t gpu_mmio_base = 0;
static uint64_t dma_heap_curr = 0x50000000ULL;
static uint16_t global_desc_idx = 0;

/* --- Active Framebuffer Tracking --- */
static uint64_t active_fb_phys = 0;
static uint32_t active_fb_width = 1024;
static uint32_t active_fb_height = 768;
static uint32_t active_fb_size = 1024 * 768 * 4;

/* --- Global Input Pointers --- */
static uint64_t input_mmio_base = 0;
static volatile struct virtq_desc *in_desc;
static volatile struct virtq_avail *in_avail;
static volatile struct virtq_used *in_used;
static uint16_t in_last_used_idx = 0;
static struct virtio_input_event *in_events;

/* --- Keyboard State --- */
static uint8_t shift_active = 0;

/* Linux EV_KEY to ASCII Translation Maps */
static const char keymap_us_lower[ 128 ] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0
};

static const char keymap_us_upper[ 128 ] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0
};

/* --- DMA Allocator --- */
uint64_t bump_allocate(uint32_t size, uint32_t align) {
    uint64_t addr = dma_heap_curr;
    if (addr % align != 0) {
        addr = (addr + align - 1) & ~((uint64_t)(align - 1));
    }
    dma_heap_curr = addr + size;
    
    for (uint32_t i = 0; i < size; i++) {
        ((volatile uint8_t *)addr)[ i ] = 0;
    }
    return addr;
}

/* --- Cache Coherency Helpers --- */
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

/* --- GPU Command Protocol --- */
uint32_t send_gpu_command(uint64_t request_phys,  uint32_t request_size,
                          uint64_t response_phys, uint32_t response_size) {

    uint16_t d0 = global_desc_idx++ % VIRTQ_SIZE;
    uint16_t d1 = global_desc_idx++ % VIRTQ_SIZE;

    gpu_desc[ d0 ].addr  = request_phys;
    gpu_desc[ d0 ].len   = request_size;
    gpu_desc[ d0 ].flags = VIRTQ_DESC_F_NEXT;
    gpu_desc[ d0 ].next  = d1;

    gpu_desc[ d1 ].addr  = response_phys;
    gpu_desc[ d1 ].len   = response_size;
    gpu_desc[ d1 ].flags = VIRTQ_DESC_F_WRITE;
    gpu_desc[ d1 ].next  = 0;

    uint16_t avail_idx = gpu_avail->idx;
    gpu_avail->ring[ avail_idx % VIRTQ_SIZE ] = d0;

    cache_flush(request_phys, request_size);
    cache_flush((uint64_t)&gpu_desc[ d0 ], sizeof(struct virtq_desc) * 2);
    cache_flush((uint64_t)&gpu_avail->ring[ avail_idx % VIRTQ_SIZE ], 2);

    gpu_avail->idx = avail_idx + 1;
    cache_flush((uint64_t)&gpu_avail->idx, 2);

    volatile uint32_t *notify_reg = (volatile uint32_t *)(gpu_mmio_base + VIRTIO_REG_QUEUE_NOTIFY);
    *notify_reg = 0;

    while (gpu_used->idx == gpu_last_used_idx) {
        cache_invalidate((uint64_t)&gpu_used->idx, 2);
    }
    gpu_last_used_idx++;

    cache_invalidate(response_phys, response_size);
    return ((struct virtio_gpu_ctrl_hdr *)response_phys)->type;
}

void gpu_init(void) {
    uint32_t resp_type;
    uart_print("[GPU] Commencing VirtIO Handshake...\n");

    /* Step 1: GET_DISPLAY_INFO */
    struct virtio_gpu_ctrl_hdr *req_info = (struct virtio_gpu_ctrl_hdr *)bump_allocate(sizeof(*req_info), 8);
    struct virtio_gpu_resp_display_info *resp_info = (struct virtio_gpu_resp_display_info *)bump_allocate(sizeof(*resp_info), 8);
    
    req_info->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
    resp_type = send_gpu_command((uint64_t)req_info, sizeof(*req_info), (uint64_t)resp_info, sizeof(*resp_info));
    if (resp_type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) kpanic("GPU: GET_DISPLAY_INFO failed");

    uint32_t fb_width  = *(volatile uint32_t *)((uint8_t *)resp_info + 32);
    uint32_t fb_height = *(volatile uint32_t *)((uint8_t *)resp_info + 36);
    
    if (fb_width == 0 || fb_height == 0) { fb_width = 1280; fb_height = 800; }
    
    uint32_t fb_size = fb_width * fb_height * 4;
    active_fb_width = fb_width; active_fb_height = fb_height; active_fb_size = fb_size;
    
    uart_print("[GPU] Display Info OK. Binding Resolution...\n");

    /* Step 2: RESOURCE_CREATE_2D */
    uint32_t *req_create = (uint32_t *)bump_allocate(40, 8);
    struct virtio_gpu_ctrl_hdr *resp_create = (struct virtio_gpu_ctrl_hdr *)bump_allocate(24, 8);
    
    req_create[ 0 ] = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    req_create[ 6 ] = 1;                                  
    req_create[ 7 ] = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;   
    req_create[ 8 ] = fb_width; req_create[ 9 ] = fb_height;                          
    
    if (send_gpu_command((uint64_t)req_create, 40, (uint64_t)resp_create, 24) != VIRTIO_GPU_RESP_OK_NODATA) kpanic("GPU: CREATE failed");

    /* Step 3: RESOURCE_ATTACH_BACKING */
    uint64_t fb_phys = bump_allocate(fb_size, 4096); 
    active_fb_phys = fb_phys;
    
    uint32_t *req_attach = (uint32_t *)bump_allocate(48, 8);
    struct virtio_gpu_ctrl_hdr *resp_attach = (struct virtio_gpu_ctrl_hdr *)bump_allocate(24, 8);
    
    req_attach[ 0 ] = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    req_attach[ 6 ] = 1; req_attach[ 7 ] = 1; 
    req_attach[ 8 ] = (uint32_t)(fb_phys & 0xFFFFFFFF); req_attach[ 9 ] = (uint32_t)(fb_phys >> 32);        
    req_attach[ 10 ] = fb_size; req_attach[ 11 ] = 0;                               
    
    if (send_gpu_command((uint64_t)req_attach, 48, (uint64_t)resp_attach, 24) != VIRTIO_GPU_RESP_OK_NODATA) kpanic("GPU: ATTACH failed");

    mmu_map_framebuffer(fb_phys, fb_size);

    /* Step 4: SET_SCANOUT */
    uint32_t *req_scanout = (uint32_t *)bump_allocate(48, 8);
    struct virtio_gpu_ctrl_hdr *resp_scanout = (struct virtio_gpu_ctrl_hdr *)bump_allocate(24, 8);
    
    req_scanout[ 0 ] = VIRTIO_GPU_CMD_SET_SCANOUT;
    req_scanout[ 6 ] = 0; req_scanout[ 7 ] = 0;         
    req_scanout[ 8 ] = fb_width; req_scanout[ 9 ] = fb_height; 
    req_scanout[ 10 ] = 0; req_scanout[ 11 ] = 1;        
    
    if (send_gpu_command((uint64_t)req_scanout, 48, (uint64_t)resp_scanout, 24) != VIRTIO_GPU_RESP_OK_NODATA) kpanic("GPU: SCANOUT failed");

    /* Step 5: Fill framebuffer with Solid Blue (BGRA: 0xFF0000FF) */
    uint32_t *fb_pixels = (uint32_t *)fb_phys;
    for (uint32_t i = 0; i < (fb_size / 4); i++) fb_pixels[ i ] = 0xFF0000FF;
    cache_flush(fb_phys, fb_size);

    /* Step 6: TRANSFER_TO_HOST_2D */
    uint32_t *req_tx = (uint32_t *)bump_allocate(56, 8);
    struct virtio_gpu_ctrl_hdr *resp_tx = (struct virtio_gpu_ctrl_hdr *)bump_allocate(24, 8);
    
    req_tx[ 0 ] = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    req_tx[ 6 ] = 0; req_tx[ 7 ] = 0; req_tx[ 8 ] = fb_width; req_tx[ 9 ] = fb_height; 
    req_tx[ 10 ] = 0; req_tx[ 11 ] = 0; req_tx[ 12 ] = 1; req_tx[ 13 ] = 0;        
    
    if (send_gpu_command((uint64_t)req_tx, 56, (uint64_t)resp_tx, 24) != VIRTIO_GPU_RESP_OK_NODATA) kpanic("GPU: TRANSFER failed");

    /* Step 7: RESOURCE_FLUSH */
    uint32_t *req_flush = (uint32_t *)bump_allocate(48, 8);
    struct virtio_gpu_ctrl_hdr *resp_flush = (struct virtio_gpu_ctrl_hdr *)bump_allocate(24, 8);
    
    req_flush[ 0 ] = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    req_flush[ 6 ] = 0; req_flush[ 7 ] = 0; req_flush[ 8 ] = fb_width; req_flush[ 9 ] = fb_height; 
    req_flush[ 10 ] = 1; req_flush[ 11 ] = 0;        
    
    if (send_gpu_command((uint64_t)req_flush, 48, (uint64_t)resp_flush, 24) != VIRTIO_GPU_RESP_OK_NODATA) kpanic("GPU: FLUSH failed");

    uart_print("[GPU] Handshake Complete! Screen should be active.\n");
}

/* --- Setup Helper for VirtQueues --- */
static uint64_t setup_virtqueue(uint64_t base, uint32_t version, volatile struct virtq_desc **desc, volatile struct virtq_avail **avail, volatile struct virtq_used **used) {
    uint32_t desc_size  = 16 * VIRTQ_SIZE;
    uint32_t avail_size = 6 + 2 * VIRTQ_SIZE;
    uint32_t pad        = 4096 - ((desc_size + avail_size) % 4096);
    uint32_t used_size  = 6 + 8 * VIRTQ_SIZE;
    uint32_t total      = desc_size + avail_size + pad + used_size;

    uint64_t queue_base = bump_allocate(total, 4096);
    *desc  = (volatile struct virtq_desc *)queue_base;
    *avail = (volatile struct virtq_avail *)(queue_base + desc_size);
    *used  = (volatile struct virtq_used *)(queue_base + desc_size + avail_size + pad);

    volatile uint32_t *q_sel = (volatile uint32_t *)(base + VIRTIO_REG_QUEUE_SEL);
    volatile uint32_t *q_num = (volatile uint32_t *)(base + VIRTIO_REG_QUEUE_NUM);
    volatile uint32_t *q_num_max = (volatile uint32_t *)(base + 0x034);

    *q_sel = 0;
    if (*q_num_max < VIRTQ_SIZE) kpanic("VirtIO: queue too small for VIRTQ_SIZE");
    *q_num = VIRTQ_SIZE;

    if (version == 1) {
        volatile uint32_t *guest_page_size = (volatile uint32_t *)(base + 0x028);
        volatile uint32_t *q_align         = (volatile uint32_t *)(base + 0x03C);
        volatile uint32_t *q_pfn           = (volatile uint32_t *)(base + 0x040);
        *guest_page_size = 4096;
        *q_align = 4096;
        *q_pfn = (uint32_t)(queue_base / 4096);
    } else {
        volatile uint32_t *q_desc_low   = (volatile uint32_t *)(base + VIRTIO_REG_QUEUE_DESC_LOW);
        volatile uint32_t *q_desc_high  = (volatile uint32_t *)(base + VIRTIO_REG_QUEUE_DESC_HIGH);
        volatile uint32_t *q_avail_low  = (volatile uint32_t *)(base + VIRTIO_REG_QUEUE_AVAIL_LOW);
        volatile uint32_t *q_avail_high = (volatile uint32_t *)(base + VIRTIO_REG_QUEUE_AVAIL_HIGH);
        volatile uint32_t *q_used_low   = (volatile uint32_t *)(base + VIRTIO_REG_QUEUE_USED_LOW);
        volatile uint32_t *q_used_high  = (volatile uint32_t *)(base + VIRTIO_REG_QUEUE_USED_HIGH);
        volatile uint32_t *q_ready      = (volatile uint32_t *)(base + VIRTIO_REG_QUEUE_READY);

        *q_desc_low   = (uint32_t)(queue_base & 0xFFFFFFFF);
        *q_desc_high  = (uint32_t)(queue_base >> 32);
        *q_avail_low  = (uint32_t)((queue_base + desc_size) & 0xFFFFFFFF);
        *q_avail_high = (uint32_t)((queue_base + desc_size) >> 32);
        *q_used_low   = (uint32_t)((queue_base + desc_size + avail_size + pad) & 0xFFFFFFFF);
        *q_used_high  = (uint32_t)((queue_base + desc_size + avail_size + pad) >> 32);
        *q_ready = 1;
    }
    return queue_base;
}

/* --- Hardware Probing --- */
void virtio_probe_and_init(void) {
    uart_print("[KERNEL] VirtIO probe: scanning 32 slots...\n");
    
    for (int i = 0; i < VIRTIO_MMIO_SLOT_COUNT; i++) {
        uint64_t base = VIRTIO_MMIO_BASE + (i * VIRTIO_MMIO_SLOT_SIZE);
        volatile uint32_t *magic   = (volatile uint32_t *)(base + 0x000);
        volatile uint32_t *version = (volatile uint32_t *)(base + 0x004);
        volatile uint32_t *devid   = (volatile uint32_t *)(base + 0x008);
        
        if (*magic == VIRTIO_MAGIC && *devid != 0) {
            volatile uint32_t *status = (volatile uint32_t *)(base + VIRTIO_REG_STATUS);

            if (*devid == VIRTIO_DEV_INPUT) {
                uart_print("[KERNEL] VirtIO Input found. Initializing EventQ...\n");
                input_mmio_base = base;
                
                *status = 0; /* Reset */
                *status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;
                if (*version != 1) {
                    volatile uint32_t *drv_feat_sel = (volatile uint32_t *)(base + 0x024);
                    volatile uint32_t *drv_feat     = (volatile uint32_t *)(base + 0x020);
                    *drv_feat_sel = 0; *drv_feat = 0; *drv_feat_sel = 1; *drv_feat = 1; 
                    *status |= VIRTIO_STATUS_FEATURES_OK;
                }

                setup_virtqueue(base, *version, &in_desc, &in_avail, &in_used);
                
                /* Give QEMU empty Event buffers to write keystrokes into */
                in_events = (struct virtio_input_event *)bump_allocate(sizeof(struct virtio_input_event) * VIRTQ_SIZE, 8);
                for (int j = 0; j < VIRTQ_SIZE; j++) {
                    in_desc[ j ].addr  = (uint64_t)&in_events[ j ];
                    in_desc[ j ].len   = sizeof(struct virtio_input_event);
                    in_desc[ j ].flags = VIRTQ_DESC_F_WRITE;
                    in_desc[ j ].next  = 0;
                    in_avail->ring[ j ] = j;
                }
                
                cache_flush((uint64_t)in_desc, sizeof(struct virtq_desc) * VIRTQ_SIZE);
                cache_flush((uint64_t)in_avail->ring, 2 * VIRTQ_SIZE);
                
                in_avail->idx = VIRTQ_SIZE;
                cache_flush((uint64_t)&in_avail->idx, 2);

                *status |= VIRTIO_STATUS_DRIVER_OK;
                volatile uint32_t *notify_reg = (volatile uint32_t *)(base + VIRTIO_REG_QUEUE_NOTIFY);
                *notify_reg = 0; /* Ring doorbell for Queue 0 */
            } 
            else if (*devid == VIRTIO_DEV_GPU) {
                uart_print("[KERNEL] VirtIO GPU found\n");
                gpu_mmio_base = base;
                
                *status = 0; /* Reset */
                *status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;
                if (*version != 1) {
                    volatile uint32_t *drv_feat_sel = (volatile uint32_t *)(base + 0x024);
                    volatile uint32_t *drv_feat     = (volatile uint32_t *)(base + 0x020);
                    *drv_feat_sel = 0; *drv_feat = 0; *drv_feat_sel = 1; *drv_feat = 1; 
                    *status |= VIRTIO_STATUS_FEATURES_OK;
                }

                setup_virtqueue(base, *version, &gpu_desc, &gpu_avail, &gpu_used);
                *status |= VIRTIO_STATUS_DRIVER_OK;
                gpu_init();
            }
        }
    }
    uart_print("[KERNEL] VirtIO probe complete\n");
}

/* --- Input Poller (Translates QEMU Keyboard events to ASCII) --- */
char virtio_input_poll(void) {
    if (!input_mmio_base) return 0;

    cache_invalidate((uint64_t)&in_used->idx, 2);

    if (in_used->idx != in_last_used_idx) {
        uint16_t used_ring_idx = in_last_used_idx % VIRTQ_SIZE;
        uint32_t desc_idx = in_used->ring[ used_ring_idx ].id;

        struct virtio_input_event *evt = &in_events[ desc_idx ];
        cache_invalidate((uint64_t)evt, sizeof(struct virtio_input_event));

        char ascii = 0;
        
        /* EV_KEY is 1. Value 1 is Press, Value 2 is Hold/Repeat, Value 0 is Release */
        if (evt->type == 1) {
            /* Check for Left Shift (42) or Right Shift (54) */
            if (evt->code == 42 || evt->code == 54) {
                if (evt->value == 1) {
                    shift_active = 1;
                } else if (evt->value == 0) {
                    shift_active = 0;
                }
            } 
            /* Process standard keys on Press or Repeat */
            else if (evt->value == 1 || evt->value == 2) {
                if (evt->code < 128) {
                    if (shift_active) {
                        ascii = keymap_us_upper[ evt->code ];
                    } else {
                        ascii = keymap_us_lower[ evt->code ];
                    }
                }
            }
        }

        /* Give the buffer back to QEMU so it can capture the next keystroke */
        uint16_t avail_idx = in_avail->idx;
        in_avail->ring[ avail_idx % VIRTQ_SIZE ] = desc_idx;
        cache_flush((uint64_t)&in_avail->ring[ avail_idx % VIRTQ_SIZE ], 2);
        
        in_avail->idx = avail_idx + 1;
        cache_flush((uint64_t)&in_avail->idx, 2);

        volatile uint32_t *notify_reg = (volatile uint32_t *)(input_mmio_base + VIRTIO_REG_QUEUE_NOTIFY);
        *notify_reg = 0;

        in_last_used_idx++;
        return ascii; /* Returns 0 if key was a release or modifier, otherwise returns the char */
    }

    return 0;
}

/* --- Syscall Handler Support --- */
void virtio_gpu_flush(void) {
    if (!gpu_mmio_base || !active_fb_phys) return;

    cache_flush(active_fb_phys, active_fb_size);

    uint32_t *req_tx = (uint32_t *)bump_allocate(56, 8);
    struct virtio_gpu_ctrl_hdr *resp_tx = (struct virtio_gpu_ctrl_hdr *)bump_allocate(24, 8);

    req_tx[ 0 ] = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D; req_tx[ 6 ] = 0; req_tx[ 7 ] = 0;
    req_tx[ 8 ] = active_fb_width; req_tx[ 9 ] = active_fb_height; req_tx[ 10 ] = 0;
    req_tx[ 11 ] = 0; req_tx[ 12 ] = 1; req_tx[ 13 ] = 0;
    send_gpu_command((uint64_t)req_tx, 56, (uint64_t)resp_tx, 24);

    uint32_t *req_flush = (uint32_t *)bump_allocate(48, 8);
    struct virtio_gpu_ctrl_hdr *resp_flush = (struct virtio_gpu_ctrl_hdr *)bump_allocate(24, 8);

    req_flush[ 0 ] = VIRTIO_GPU_CMD_RESOURCE_FLUSH; req_flush[ 6 ] = 0; req_flush[ 7 ] = 0;
    req_flush[ 8 ] = active_fb_width; req_flush[ 9 ] = active_fb_height; req_flush[ 10 ] = 1; req_flush[ 11 ] = 0;
    send_gpu_command((uint64_t)req_flush, 48, (uint64_t)resp_flush, 24);
}

void virtio_blk_read_sector(uint64_t sector, void *buffer) {
    (void)sector; (void)buffer;
    uart_print("[ VIRTIO ] Warning: Block read called (currently stubbed).\n");
}