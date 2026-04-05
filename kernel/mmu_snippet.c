/* INTEGRATION NOTE: Paste this into your existing mmu.c inside mmu_init_tables() */
    /* Map VirtIO MMIO range (0x0A000000 - 0x0A003FFF) as ATTR_DEVICE | AP_RW_EL1 | XN_ALL */
    uint64_t virtio_start = 0x0A000000;
    uint64_t virtio_end   = 0x0A000000 + (32 * 0x200);
    
    for (uint64_t addr = virtio_start; addr < virtio_end; addr += 4096) {
        map_page(addr, addr, ATTR_DEVICE | AP_RW_EL1 | XN_ALL);
    }
