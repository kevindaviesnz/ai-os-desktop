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

/* --- GPU Command Protocol (QA CACHE-COHERENT FIX) --- */
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

    /* Flush request payload to physical RAM */
    for (uint64_t a = request_phys & ~63ULL; a < request_phys + request_size; a += 64)
        __asm__ volatile("dc cvac, %0" :: "r"(a) : "memory");

    /* Flush descriptor entries to physical RAM */
    for (uint64_t a = (uint64_t)&gpu_desc[ d0 ] & ~63ULL;
         a < (uint64_t)&gpu_desc[ d1 ] + sizeof(struct virtq_desc); a += 64)
        __asm__ volatile("dc cvac, %0" :: "r"(a) : "memory");

    /* Flush avail ring and idx to physical RAM */
    for (uint64_t a = (uint64_t)&gpu_avail->ring[ avail_idx % VIRTQ_SIZE ] & ~63ULL;
         a < (uint64_t)&gpu_avail->idx + 2; a += 64)
        __asm__ volatile("dc cvac, %0" :: "r"(a) : "memory");

    __asm__ volatile("dsb sy" ::: "memory");

    gpu_avail->idx = avail_idx + 1;

    /* Flush the idx update */
    __asm__ volatile("dc cvac, %0" :: "r"((uint64_t)&gpu_avail->idx & ~63ULL) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");

    /* Notify device */
    volatile uint32_t *notify_reg =
        (volatile uint32_t *)(gpu_mmio_base + VIRTIO_REG_QUEUE_NOTIFY);
    *notify_reg = 0;

    /* Poll used->idx with cache invalidation so CPU sees GPU's write */
    uint64_t used_line = (uint64_t)&gpu_used->idx & ~63ULL;
    while (gpu_used->idx == gpu_last_used_idx) {
        __asm__ volatile("dc ivac, %0" :: "r"(used_line) : "memory");
        __asm__ volatile("dsb sy" ::: "memory");
    }
    gpu_last_used_idx++;

    /* Invalidate response buffer before reading so CPU sees the GPU response */
    for (uint64_t a = response_phys & ~63ULL; a < response_phys + response_size; a += 64)
        __asm__ volatile("dc ivac, %0" :: "r"(a) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");

    return ((struct virtio_gpu_ctrl_hdr *)response_phys)->type;
}

void gpu_init(void) {
    uint32_t resp_type;
    
    uart_print("[GPU] Commencing VirtIO Handshake...\n");

    /* Step 1: GET_DISPLAY_INFO */
    struct virtio_gpu_ctrl_hdr *req_info = 
        (struct virtio_gpu_ctrl_hdr *)bump_allocate(sizeof(*req_info), 8);
    struct virtio_gpu_resp_display_info *resp_info = 
        (struct virtio_gpu_resp_display_info *)bump_allocate(sizeof(*resp_info), 8);
    
    req_info->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
    resp_type = send_gpu_command((uint64_t)req_info, sizeof(*req_info), 
                                 (uint64_t)resp_info, sizeof(*resp_info));
    if (resp_type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) {
        uart_print("GPU: GET_DISPLAY_INFO failed: ");
        uart_print_hex(resp_type);
        kpanic("Handshake aborted");
    }

    uint32_t fb_width  = *(volatile uint32_t *)((uint8_t *)resp_info + 32);
    uint32_t fb_height = *(volatile uint32_t *)((uint8_t *)resp_info + 36);
    
    if (fb_width == 0 || fb_height == 0) { fb_width = 1280; fb_height = 800; }
    
    if (fb_width == 0 || fb_height == 0) {
        fb_width = 1024;
        fb_height = 768;
    }
    
    uint32_t fb_size = fb_width * fb_height * 4; 
    uart_print("[GPU] Display Info OK. Binding Resolution...\n");

    /* Step 2: RESOURCE_CREATE_2D (40 bytes = 10 words) */
    uint32_t *req_create = (uint32_t *)bump_allocate(40, 8);
    struct virtio_gpu_ctrl_hdr *resp_create = (struct virtio_gpu_ctrl_hdr *)bump_allocate(24, 8);
    
    req_create[ 0 ] = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    req_create[ 6 ] = 1;                                  /* resource_id */
    req_create[ 7 ] = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;   /* format */
    req_create[ 8 ] = fb_width;                           /* width */
    req_create[ 9 ] = fb_height;                          /* height */
    
    resp_type = send_gpu_command((uint64_t)req_create, 40, (uint64_t)resp_create, 24);
    if (resp_type != VIRTIO_GPU_RESP_OK_NODATA) {
        uart_print("GPU: CREATE failed: ");
        uart_print_hex(resp_type);
        kpanic("Handshake aborted");
    }

    /* Step 3: RESOURCE_ATTACH_BACKING (Flattened) */
    uint64_t fb_phys = bump_allocate(fb_size, 4096); 
    
    uint32_t *req_attach = (uint32_t *)bump_allocate(48, 8);
    struct virtio_gpu_ctrl_hdr *resp_attach = (struct virtio_gpu_ctrl_hdr *)bump_allocate(24, 8);
    
    req_attach[ 0 ] = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    req_attach[ 6 ] = 1; /* resource_id */
    req_attach[ 7 ] = 1; /* nr_entries */
    req_attach[ 8 ] = (uint32_t)(fb_phys & 0xFFFFFFFF); /* addr low */
    req_attach[ 9 ] = (uint32_t)(fb_phys >> 32);        /* addr high */
    req_attach[ 10 ] = fb_size;                         /* length */
    req_attach[ 11 ] = 0;                               /* padding */
    
    resp_type = send_gpu_command((uint64_t)req_attach, 48, (uint64_t)resp_attach, 24);
    if (resp_type != VIRTIO_GPU_RESP_OK_NODATA) {
        uart_print("GPU: ATTACH failed: ");
        uart_print_hex(resp_type);
        kpanic("Handshake aborted");
    }

    mmu_map_framebuffer(fb_phys, fb_size);

    /* Step 4: SET_SCANOUT (48 bytes = 12 words) */
    uint32_t *req_scanout = (uint32_t *)bump_allocate(48, 8);
    struct virtio_gpu_ctrl_hdr *resp_scanout = (struct virtio_gpu_ctrl_hdr *)bump_allocate(24, 8);
    
    req_scanout[ 0 ] = VIRTIO_GPU_CMD_SET_SCANOUT;
    req_scanout[ 6 ] = 0;         /* r.x */
    req_scanout[ 7 ] = 0;         /* r.y */
    req_scanout[ 8 ] = fb_width;  /* r.width */
    req_scanout[ 9 ] = fb_height; /* r.height */
    req_scanout[ 10 ] = 0;        /* scanout_id */
    req_scanout[ 11 ] = 1;        /* resource_id */
    
    resp_type = send_gpu_command((uint64_t)req_scanout, 48, (uint64_t)resp_scanout, 24);
    if (resp_type != VIRTIO_GPU_RESP_OK_NODATA) {
        uart_print("GPU: SCANOUT failed: ");
        uart_print_hex(resp_type);
        kpanic("Handshake aborted");
    }

    /* Step 5: Fill framebuffer with Solid Blue (BGRA: 0xFF0000FF) */
    uint32_t *fb_pixels = (uint32_t *)fb_phys;
    for (uint32_t i = 0; i < (fb_size / 4); i++) {
        fb_pixels[ i ] = 0xFF0000FF;
    }

    /* Flush Framebuffer to RAM before transfer */
    {
        uint64_t addr = fb_phys & ~63ULL;
        uint64_t end  = fb_phys + fb_size;
        while (addr < end) {
            __asm__ volatile("dc cvac, %0" :: "r"(addr) : "memory");
            addr += 64;
        }
        __asm__ volatile("dsb sy" ::: "memory");
    }

    /* Step 6: TRANSFER_TO_HOST_2D (56 bytes = 14 words) */
    uint32_t *req_tx = (uint32_t *)bump_allocate(56, 8);
    struct virtio_gpu_ctrl_hdr *resp_tx = (struct virtio_gpu_ctrl_hdr *)bump_allocate(24, 8);
    
    req_tx[ 0 ] = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    req_tx[ 6 ] = 0;         /* r.x */
    req_tx[ 7 ] = 0;         /* r.y */
    req_tx[ 8 ] = fb_width;  /* r.width */
    req_tx[ 9 ] = fb_height; /* r.height */
    req_tx[ 10 ] = 0;        /* offset low */
    req_tx[ 11 ] = 0;        /* offset high */
    req_tx[ 12 ] = 1;        /* resource_id */
    req_tx[ 13 ] = 0;        /* padding */
    
    resp_type = send_gpu_command((uint64_t)req_tx, 56, (uint64_t)resp_tx, 24);
    if (resp_type != VIRTIO_GPU_RESP_OK_NODATA) {
        uart_print("GPU: TRANSFER failed: ");
        uart_print_hex(resp_type);
        kpanic("Handshake aborted");
    }

    /* Step 7: RESOURCE_FLUSH (48 bytes = 12 words) */
    uint32_t *req_flush = (uint32_t *)bump_allocate(48, 8);
    struct virtio_gpu_ctrl_hdr *resp_flush = (struct virtio_gpu_ctrl_hdr *)bump_allocate(24, 8);
    
    req_flush[ 0 ] = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    req_flush[ 6 ] = 0;         /* r.x */
    req_flush[ 7 ] = 0;         /* r.y */
    req_flush[ 8 ] = fb_width;  /* r.width */
    req_flush[ 9 ] = fb_height; /* r.height */
    req_flush[ 10 ] = 1;        /* resource_id */
    req_flush[ 11 ] = 0;        /* padding */
    
    resp_type = send_gpu_command((uint64_t)req_flush, 48, (uint64_t)resp_flush, 24);
    if (resp_type != VIRTIO_GPU_RESP_OK_NODATA) {
        uart_print("GPU: FLUSH failed: ");
        uart_print_hex(resp_type);
        kpanic("Handshake aborted");
    }

    uart_print("[GPU] Handshake Complete! Screen should be active.\n");
}

/* --- Hardware Probing & Virtqueue Setup --- */
void virtio_probe_and_init(void) {
    uart_print("[KERNEL] VirtIO probe: scanning 32 slots...\n");
    
    for (int i = 0; i < VIRTIO_MMIO_SLOT_COUNT; i++) {
        uint64_t base = VIRTIO_MMIO_BASE + (i * VIRTIO_MMIO_SLOT_SIZE);
        volatile uint32_t *magic   = (volatile uint32_t *)(base + 0x000);
        volatile uint32_t *version = (volatile uint32_t *)(base + 0x004);
        volatile uint32_t *devid   = (volatile uint32_t *)(base + 0x008);
        
        if (*magic == VIRTIO_MAGIC && *devid != 0) {
            if (*devid == VIRTIO_DEV_INPUT) {
                uart_print("[KERNEL] VirtIO Input found\n");
            } else if (*devid == VIRTIO_DEV_GPU) {
                uart_print("[KERNEL] VirtIO GPU found\n");
                gpu_mmio_base = base;
                
                volatile uint32_t *status = (volatile uint32_t *)(base + VIRTIO_REG_STATUS);
                volatile uint32_t *q_sel  = (volatile uint32_t *)(base + VIRTIO_REG_QUEUE_SEL);
                volatile uint32_t *q_num  = (volatile uint32_t *)(base + VIRTIO_REG_QUEUE_NUM);
                
                *status = 0; /* Reset */
                *status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;

                uint32_t desc_size  = 16 * VIRTQ_SIZE;
                uint32_t avail_size = 6 + 2 * VIRTQ_SIZE;
                uint32_t pad        = 4096 - ((desc_size + avail_size) % 4096);
                uint32_t used_size  = 6 + 8 * VIRTQ_SIZE;
                uint32_t total      = desc_size + avail_size + pad + used_size;

                uint64_t queue_base = bump_allocate(total, 4096);
                gpu_desc  = (volatile struct virtq_desc *)queue_base;
                gpu_avail = (volatile struct virtq_avail *)(queue_base + desc_size);
                gpu_used  = (volatile struct virtq_used *)(queue_base + desc_size + avail_size + pad);

                if (*version == 1) {
                    uart_print("[GPU] Hardware reported: Legacy Version 1\n");
                    
                    volatile uint32_t *guest_page_size = (volatile uint32_t *)(base + 0x028);
                    volatile uint32_t *q_align         = (volatile uint32_t *)(base + 0x03C);
                    volatile uint32_t *q_pfn           = (volatile uint32_t *)(base + 0x040);
                    volatile uint32_t *q_num_max       = (volatile uint32_t *)(base + 0x034);
                    
                    *guest_page_size = 4096;
                    *q_sel = 0;
                    
                    /* WARNING-07 FIX: Validate MAX queue size */
                    if (*q_num_max < VIRTQ_SIZE) {
                        kpanic("GPU: queue too small for VIRTQ_SIZE\n");
                    }
                    *q_num = VIRTQ_SIZE;
                    
                    *q_align = 4096;
                    *q_pfn = (uint32_t)(queue_base / 4096);
                    
                    *status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK;
                } else {
                    uart_print("[GPU] Hardware reported: Modern Version 2\n");
                    
                    volatile uint32_t *drv_feat_sel = (volatile uint32_t *)(base + 0x024);
                    volatile uint32_t *drv_feat     = (volatile uint32_t *)(base + 0x020);
                    volatile uint32_t *q_desc_low   = (volatile uint32_t *)(base + VIRTIO_REG_QUEUE_DESC_LOW);
                    volatile uint32_t *q_desc_high  = (volatile uint32_t *)(base + VIRTIO_REG_QUEUE_DESC_HIGH);
                    volatile uint32_t *q_avail_low  = (volatile uint32_t *)(base + VIRTIO_REG_QUEUE_AVAIL_LOW);
                    volatile uint32_t *q_avail_high = (volatile uint32_t *)(base + VIRTIO_REG_QUEUE_AVAIL_HIGH);
                    volatile uint32_t *q_used_low   = (volatile uint32_t *)(base + VIRTIO_REG_QUEUE_USED_LOW);
                    volatile uint32_t *q_used_high  = (volatile uint32_t *)(base + VIRTIO_REG_QUEUE_USED_HIGH);
                    volatile uint32_t *q_ready      = (volatile uint32_t *)(base + VIRTIO_REG_QUEUE_READY);
                    volatile uint32_t *q_num_max    = (volatile uint32_t *)(base + 0x034);

                    *drv_feat_sel = 0; *drv_feat = 0;
                    *drv_feat_sel = 1; *drv_feat = 1; 
                    *status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;

                    /* WARNING-08 FIX: Read-back FEATURES_OK to ensure device acceptance */
                    if (!(*status & VIRTIO_STATUS_FEATURES_OK)) {
                        kpanic("GPU: device rejected feature negotiation\n");
                    }

                    *q_sel = 0;
                    
                    /* WARNING-07 FIX: Validate MAX queue size */
                    if (*q_num_max < VIRTQ_SIZE) {
                        kpanic("GPU: queue too small for VIRTQ_SIZE\n");
                    }
                    *q_num = VIRTQ_SIZE;

                    /* BLOCKER-33 FIX: Identity-mapped VA == PA. If non-identity mapping 
                       is ever introduced, recalculate physical addresses here. */
                    uint64_t desc_phys  = (uint64_t)gpu_desc;   
                    uint64_t avail_phys = (uint64_t)gpu_avail;
                    uint64_t used_phys  = (uint64_t)gpu_used;

                    *q_desc_low   = (uint32_t)(desc_phys & 0xFFFFFFFF);
                    *q_desc_high  = (uint32_t)(desc_phys >> 32);
                    *q_avail_low  = (uint32_t)(avail_phys & 0xFFFFFFFF);
                    *q_avail_high = (uint32_t)(avail_phys >> 32);
                    *q_used_low   = (uint32_t)(used_phys & 0xFFFFFFFF);
                    *q_used_high  = (uint32_t)(used_phys >> 32);
                    
                    *q_ready = 1;

                    *status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK;
                }

                gpu_init();
            }
        }
    }
    
    uart_print("[KERNEL] VirtIO probe complete\n");
}

* ========================================================================
 * VIRTIO BLOCK DRIVER (RESTORED STUB)
 * ======================================================================== */

/* Restored to satisfy linker dependencies in syscall.c */
void virtio_blk_read_sector(uint64_t sector, void *buffer) {
    (void)sector;
    (void)buffer;
    uart_print("[ VIRTIO ] Warning: Block read called (currently stubbed).\n");
}