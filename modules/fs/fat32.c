#include "os_fat32.h"
#include "os_virtio.h"

extern void uart_print(const char *str);
extern void uart_print_hex(uint32_t val);

/* Global FAT32 State Variables */
static uint32_t bytes_per_cluster;
static uint32_t fat_start_sector;
static uint32_t data_start_sector;
static uint32_t root_dir_cluster;

/* Buffer for disk reads */
static uint8_t disk_buffer[ 512 ] __attribute__((aligned(4096)));

/* Helper: Converts a FAT32 Cluster Number to a Physical LBA Sector */
static uint32_t cluster_to_sector(uint32_t cluster) {
    /* FAT32 data clusters start at 2. So Cluster 2 is an offset of 0. */
    uint32_t offset = cluster - 2;
    uint32_t sectors_per_cluster = bytes_per_cluster / 512;
    return data_start_sector + (offset * sectors_per_cluster);
}

void fs_fat32_init(void) {
    uart_print("[FS] Initializing FAT32 File System (Direct Mode)...\n");

    /* Read the Boot Sector directly from Sector 0 */
    if (virtio_blk_read_sector(0, disk_buffer) != 0) {
        uart_print("[FS] ERROR: Failed to read Boot Sector.\n");
        return;
    }

    struct fat32_bpb *bpb = (struct fat32_bpb *)disk_buffer;
    
    /* Ensure it's actually FAT32 (Signature at offset 510) */
    if (disk_buffer[ 510 ] != 0x55 || disk_buffer[ 511 ] != 0xAA) {
        uart_print("[FS] ERROR: Invalid Boot Sector signature.\n");
        return;
    }

    /* Calculate the "Treasure Map" Offsets */
    bytes_per_cluster = bpb->bytes_per_sector * bpb->sectors_per_cluster;
    
    /* In Direct Mode, the partition starts at 0 */
    fat_start_sector  = bpb->reserved_sectors;
    
    /* Data region starts after the Reserved Sectors and the FAT tables */
    uint32_t fat_size = bpb->sectors_per_fat_32;
    data_start_sector = fat_start_sector + (bpb->fat_count * fat_size);
    
    root_dir_cluster = bpb->root_cluster;

    uart_print("[FS] FAT32 Mount Successful!\n");
    uart_print("     -> Bytes/Cluster : "); uart_print_hex(bytes_per_cluster); uart_print("\n");
    uart_print("     -> FAT Start LBA : "); uart_print_hex(fat_start_sector); uart_print("\n");
    uart_print("     -> Data Start LBA: "); uart_print_hex(data_start_sector); uart_print("\n");
    uart_print("     -> Root Cluster  : "); uart_print_hex(root_dir_cluster); uart_print("\n");
}

void fs_list_root(void) {
    uart_print("[FS] Scanning Root Directory...\n");
    
    uint32_t root_sector = cluster_to_sector(root_dir_cluster);
    
    if (virtio_blk_read_sector(root_sector, disk_buffer) != 0) {
        uart_print("[FS] ERROR: Failed to read Root Directory sector.\n");
        return;
    }

    struct fat32_dir_entry *dir = (struct fat32_dir_entry *)disk_buffer;
    int files_found = 0;

    /* A 512 byte sector holds exactly sixteen 32-byte directory entries */
    for (int i = 0; i < 16; i++) {
        /* 0x00 means this entry and all following entries are empty */
        if (dir[ i ].name[ 0 ] == 0x00) break; 
        
        /* 0xE5 means the file was deleted, skip it */
        if (dir[ i ].name[ 0 ] == (char)0xE5) continue; 
        
        /* 0x0F is a special attribute for Long File Names, skip for now */
        if (dir[ i ].attr == 0x0F) continue; 

        /* We found a valid file! Extract the 11-character name */
        char name_buf[ 12 ];
        for (int j = 0; j < 11; j++) {
            name_buf[ j ] = dir[ i ].name[ j ];
        }
        name_buf[ 11 ] = '\0'; /* Null terminate for uart_print */

        uart_print("     -> FILE FOUND: ");
        uart_print(name_buf);
        uart_print(" | Size: ");
        uart_print_hex(dir[ i ].file_size);
        uart_print(" bytes\n");
        
        files_found++;
    }

    if (files_found == 0) {
        uart_print("     -> (Directory is empty)\n");
    }
}

void fs_read_test_file(void) {
    uart_print("[FS] Attempting to read TEST    TXT...\n");
    
    /* Step 1: Read the Root Directory */
    uint32_t root_sector = cluster_to_sector(root_dir_cluster);
    if (virtio_blk_read_sector(root_sector, disk_buffer) != 0) return;

    struct fat32_dir_entry *dir = (struct fat32_dir_entry *)disk_buffer;
    uint32_t target_cluster = 0;
    uint32_t target_size = 0;

    /* Step 2: Find the specific file */
    for (int i = 0; i < 16; i++) {
        if (dir[ i ].name[ 0 ] == 0x00) break;
        
        const char *target = "TEST    TXT";
        int match = 1;
        for (int j = 0; j < 11; j++) {
            if (dir[ i ].name[ j ] != target[ j ]) match = 0;
        }

        if (match) {
            /* Combine High and Low 16-bit cluster values */
            target_cluster = ((uint32_t)dir[ i ].fst_clus_hi << 16) | dir[ i ].fst_clus_lo;
            target_size = dir[ i ].file_size;
            break;
        }
    }

    if (target_cluster == 0) {
        uart_print("[FS] ERROR: TEST    TXT not found.\n");
        return;
    }

    uart_print("[FS] File found at Cluster: ");
    uart_print_hex(target_cluster);
    uart_print("\n[FS] Contents:\n");
    uart_print("----------------------------------------\n");

    /* Step 3: Read the Data Cluster */
    uint32_t data_sector = cluster_to_sector(target_cluster);
    if (virtio_blk_read_sector(data_sector, disk_buffer) == 0) {
        
        /* Safely null-terminate the buffer so uart_print knows where to stop */
        uint32_t end_idx = target_size < 512 ? target_size : 511;
        disk_buffer[ end_idx ] = '\0';
        
        uart_print((const char *)disk_buffer);
        
        if (disk_buffer[ end_idx - 1 ] != '\n') uart_print("\n");
        uart_print("----------------------------------------\n");
    }
}

void fs_get_dir_list(char *buffer, uint32_t max_len) {
    uint32_t root_sector = cluster_to_sector(root_dir_cluster);
    
    if (virtio_blk_read_sector(root_sector, disk_buffer) != 0) {
        buffer[ 0 ] = '\0';
        return;
    }

    struct fat32_dir_entry *dir = (struct fat32_dir_entry *)disk_buffer;
    uint32_t pos = 0;
    buffer[ 0 ] = '\0';

    for (int i = 0; i < 16; i++) {
        if (dir[ i ].name[ 0 ] == 0x00) break;          /* End of directory */
        if (dir[ i ].name[ 0 ] == (char)0xE5) continue; /* Deleted file */
        if (dir[ i ].attr == 0x0F) continue;            /* Long file name entry */

        /* 1. Extract the 8-character file name (skip padding spaces) */
        for (int j = 0; j < 8; j++) {
            if (dir[ i ].name[ j ] != ' ' && pos < max_len - 1) {
                buffer[ pos++ ] = dir[ i ].name[ j ];
            }
        }
        
        /* 2. Extract the 3-character extension (if it exists) */
        if (dir[ i ].name[ 8 ] != ' ' && pos < max_len - 1) {
            buffer[ pos++ ] = '.'; /* Add the dot separator */
            for (int j = 8; j < 11; j++) {
                if (dir[ i ].name[ j ] != ' ' && pos < max_len - 1) {
                    buffer[ pos++ ] = dir[ i ].name[ j ];
                }
            }
        }
        
        /* Add a newline after each file */
        if (pos < max_len - 1) buffer[ pos++ ] = '\n';
    }
    
    /* Null-terminate the final string */
    buffer[ pos ] = '\0';
}