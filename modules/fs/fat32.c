#include "os_fat32.h"
#include "os_virtio.h"

extern void uart_print(const char *str);
extern void uart_print_hex(uint32_t val);
extern int virtio_blk_write_sector(uint64_t sector, const void *buffer);

/* Global FAT32 State Variables */
static uint32_t bytes_per_cluster;
static uint32_t fat_start_sector;
static uint32_t data_start_sector;
static uint32_t root_dir_cluster;

/* Buffers for disk I/O (Allocated in global BSS to prevent kernel stack smashes) */
static uint8_t disk_buffer[ 512 ] __attribute__((aligned(4096)));
static uint8_t write_buffer[ 512 ] __attribute__((aligned(4096)));

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

/* Helper: Convert "TEST.TXT" to FAT32 "TEST    TXT" */
static void format_fat_name(const char *input, char *output) {
    for (int k = 0; k < 11; k++) output[ k ] = ' '; /* Fill with spaces */
    
    int i = 0, j = 0;
    
    /* Copy name up to the dot or 8 chars */
    while (input[ i ] != '\0' && input[ i ] != '.' && j < 8) {
        /* Convert lowercase to uppercase for basic FAT32 matching */
        char c = input[ i ];
        if (c >= 'a' && c <= 'z') c -= 32; 
        output[ j++ ] = c;
        i++;
    }
    
    /* Find the dot for the extension */
    while (input[ i ] != '\0' && input[ i ] != '.') i++;
    
    /* Copy extension (up to 3 chars) */
    if (input[ i ] == '.') {
        i++;
        j = 8;
        while (input[ i ] != '\0' && j < 11) {
            char c = input[ i ];
            if (c >= 'a' && c <= 'z') c -= 32;
            output[ j++ ] = c;
            i++;
        }
    }
}

/* Find file by name and dump contents into buffer */
void fs_read_file_content(const char *filename, char *buffer, uint32_t max_len) {
    char target_name[ 11 ];
    format_fat_name(filename, target_name);
    
    uint32_t root_sector = cluster_to_sector(root_dir_cluster);
    if (virtio_blk_read_sector(root_sector, disk_buffer) != 0) {
        buffer[ 0 ] = '\0';
        return;
    }

    struct fat32_dir_entry *dir = (struct fat32_dir_entry *)disk_buffer;
    uint32_t target_cluster = 0;
    uint32_t target_size = 0;

    for (int i = 0; i < 16; i++) {
        if (dir[ i ].name[ 0 ] == 0x00) break;
        
        int match = 1;
        for (int j = 0; j < 11; j++) {
            if (dir[ i ].name[ j ] != target_name[ j ]) match = 0;
        }

        if (match) {
            target_cluster = ((uint32_t)dir[ i ].fst_clus_hi << 16) | dir[ i ].fst_clus_lo;
            target_size = dir[ i ].file_size;
            break;
        }
    }

    if (target_cluster == 0) {
        /* File not found */
        const char *err = "File not found.\n";
        for (uint32_t i = 0; err[ i ] != '\0' && i < max_len - 1; i++) buffer[ i ] = err[ i ];
        buffer[ 16 ] = '\0'; /* Length of error string */
        return;
    }

    /* Read the data cluster */
    uint32_t data_sector = cluster_to_sector(target_cluster);
    if (virtio_blk_read_sector(data_sector, disk_buffer) == 0) {
        uint32_t bytes_to_copy = target_size < (max_len - 1) ? target_size : (max_len - 1);
        for (uint32_t i = 0; i < bytes_to_copy; i++) {
            buffer[ i ] = (char)disk_buffer[ i ];
        }
        buffer[ bytes_to_copy ] = '\0';
    }
}

void fs_write_file_content(const char *filename, const char *data, uint32_t length) {
    char target_name[ 11 ];
    format_fat_name(filename, target_name);

    /* 1. Find a free cluster in the FAT table */
    uint32_t fat_sector = fat_start_sector;
    if (virtio_blk_read_sector(fat_sector, disk_buffer) != 0) return;

    uint32_t *fat_table = (uint32_t *)disk_buffer;
    uint32_t free_cluster = 0;

    /* Start looking from cluster 2 (0 and 1 are reserved) */
    for (uint32_t i = 2; i < 128; i++) {
        if ((fat_table[ i ] & 0x0FFFFFFF) == 0) {
            free_cluster = i;
            fat_table[ i ] = 0x0FFFFFFF; /* Mark as End of File */
            break;
        }
    }

    if (free_cluster == 0) return; /* Disk full or FAT read error */

    /* Save the updated FAT table back to disk */
    virtio_blk_write_sector(fat_sector, disk_buffer);

    /* 2. Write the actual data to the new data cluster */
    uint32_t data_sector = cluster_to_sector(free_cluster);
    
    /* Clear the global write buffer safely */
    for(int i = 0; i < 512; i++) write_buffer[ i ] = 0;
    
    uint32_t bytes_to_copy = length < 512 ? length : 512;
    for(uint32_t i = 0; i < bytes_to_copy; i++) write_buffer[ i ] = data[ i ];

    virtio_blk_write_sector(data_sector, write_buffer);

    /* 3. Find a free directory entry in the root directory */
    uint32_t root_sector = cluster_to_sector(root_dir_cluster);
    if (virtio_blk_read_sector(root_sector, disk_buffer) != 0) return;

    struct fat32_dir_entry *dir = (struct fat32_dir_entry *)disk_buffer;
    int free_idx = -1;

    for (int i = 0; i < 16; i++) {
        /* 0x00 = empty, 0xE5 = deleted file */
        if (dir[ i ].name[ 0 ] == 0x00 || dir[ i ].name[ 0 ] == (char)0xE5) {
            free_idx = i;
            break;
        }
    }

    if (free_idx == -1) return; /* Root directory is full */

    /* Populate the directory entry */
    for (int j = 0; j < 11; j++) dir[ free_idx ].name[ j ] = target_name[ j ];
    dir[ free_idx ].attr = 0x20; /* 0x20 = Archive flag (standard file) */
    dir[ free_idx ].nt_res = 0;
    dir[ free_idx ].crt_time_tenth = 0;
    dir[ free_idx ].crt_time = 0;
    dir[ free_idx ].crt_date = 0;
    dir[ free_idx ].lst_acc_date = 0;
    dir[ free_idx ].fst_clus_hi = (uint16_t)(free_cluster >> 16);
    dir[ free_idx ].fst_clus_lo = (uint16_t)(free_cluster & 0xFFFF);
    dir[ free_idx ].wrt_time = 0;
    dir[ free_idx ].wrt_date = 0;
    dir[ free_idx ].file_size = length;

    /* Write the updated directory sector back to disk */
    virtio_blk_write_sector(root_sector, disk_buffer);
}