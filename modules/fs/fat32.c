#include "os_fat32.h"
#include "os_virtio.h"

extern void uart_print(const char *str);
extern void uart_print_hex(uint32_t val);
extern int virtio_blk_read_sector(uint64_t sector, void *buffer);
extern int virtio_blk_write_sector(uint64_t sector, const void *buffer);

/* Global FAT32 State Variables */
static uint32_t bytes_per_cluster;
static uint32_t fat_start_sector;
static uint32_t data_start_sector;
static uint32_t root_dir_cluster;
static uint32_t max_valid_clusters; /* QA FIX: Cycle guard limit */

/* Buffers for disk I/O (Allocated in global BSS to prevent kernel stack smashes) */
static uint8_t disk_buffer[ 512 ] __attribute__((aligned(4096)));
static uint8_t write_buffer[ 512 ] __attribute__((aligned(4096)));

/* Helper: Converts a FAT32 Cluster Number to a Physical LBA Sector */
static uint32_t cluster_to_sector(uint32_t cluster) {
    uint32_t offset = cluster - 2;
    uint32_t sectors_per_cluster = bytes_per_cluster / 512;
    return data_start_sector + (offset * sectors_per_cluster);
}

void fs_fat32_init(void) {
    uart_print("[FS] Initializing FAT32 File System (Direct Mode)...\n");

    if (virtio_blk_read_sector(0, disk_buffer) != 0) {
        uart_print("[FS] ERROR: Failed to read Boot Sector.\n");
        return;
    }

    struct fat32_bpb *bpb = (struct fat32_bpb *)disk_buffer;
    
    if (disk_buffer[ 510 ] != 0x55 || disk_buffer[ 511 ] != 0xAA) {
        uart_print("[FS] ERROR: Invalid Boot Sector signature.\n");
        return;
    }

    bytes_per_cluster = bpb->bytes_per_sector * bpb->sectors_per_cluster;
    fat_start_sector  = bpb->reserved_sectors;
    
    uint32_t fat_size = bpb->sectors_per_fat_32;
    data_start_sector = fat_start_sector + (bpb->fat_count * fat_size);
    root_dir_cluster = bpb->root_cluster;

    /* QA FIX: Calculate maximum valid clusters to prevent infinite loops during traversal */
    uint32_t total_sectors = bpb->total_sectors_32 != 0 ? bpb->total_sectors_32 : bpb->total_sectors_16;
    max_valid_clusters = total_sectors / bpb->sectors_per_cluster;
    if (max_valid_clusters == 0) max_valid_clusters = 65536; /* Safety fallback */

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

    for (int i = 0; i < 16; i++) {
        if (dir[ i ].name[ 0 ] == 0x00) break; 
        if (dir[ i ].name[ 0 ] == (char)0xE5) continue; 
        if (dir[ i ].attr == 0x0F) continue; 

        char name_buf[ 12 ];
        for (int j = 0; j < 11; j++) {
            name_buf[ j ] = dir[ i ].name[ j ];
        }
        name_buf[ 11 ] = '\0';

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
    
    uint32_t root_sector = cluster_to_sector(root_dir_cluster);
    if (virtio_blk_read_sector(root_sector, disk_buffer) != 0) return;

    struct fat32_dir_entry *dir = (struct fat32_dir_entry *)disk_buffer;
    uint32_t target_cluster = 0;
    uint32_t target_size = 0;

    for (int i = 0; i < 16; i++) {
        if (dir[ i ].name[ 0 ] == 0x00) break;
        
        const char *target = "TEST    TXT";
        int match = 1;
        for (int j = 0; j < 11; j++) {
            if (dir[ i ].name[ j ] != target[ j ]) match = 0;
        }

        if (match) {
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

    uint32_t data_sector = cluster_to_sector(target_cluster);
    if (virtio_blk_read_sector(data_sector, disk_buffer) == 0) {
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
        if (dir[ i ].name[ 0 ] == 0x00) break;
        if (dir[ i ].name[ 0 ] == (char)0xE5) continue;
        if (dir[ i ].attr == 0x0F) continue;

        for (int j = 0; j < 8; j++) {
            if (dir[ i ].name[ j ] != ' ' && pos < max_len - 1) {
                buffer[ pos++ ] = dir[ i ].name[ j ];
            }
        }
        
        if (dir[ i ].name[ 8 ] != ' ' && pos < max_len - 1) {
            buffer[ pos++ ] = '.';
            for (int j = 8; j < 11; j++) {
                if (dir[ i ].name[ j ] != ' ' && pos < max_len - 1) {
                    buffer[ pos++ ] = dir[ i ].name[ j ];
                }
            }
        }
        
        if (pos < max_len - 1) buffer[ pos++ ] = '\n';
    }
    buffer[ pos ] = '\0';
}

static void format_fat_name(const char *input, char *output) {
    for (int k = 0; k < 11; k++) output[ k ] = ' ';
    int i = 0, j = 0;
    while (input[ i ] != '\0' && input[ i ] != '.' && j < 8) {
        char c = input[ i ];
        if (c >= 'a' && c <= 'z') c -= 32; 
        output[ j++ ] = c;
        i++;
    }
    while (input[ i ] != '\0' && input[ i ] != '.') i++;
    if (input[ i ] == '.') {
        i++; j = 8;
        while (input[ i ] != '\0' && j < 11) {
            char c = input[ i ];
            if (c >= 'a' && c <= 'z') c -= 32;
            output[ j++ ] = c;
            i++;
        }
    }
}

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
        const char *err = "File not found.\n";
        for (uint32_t i = 0; err[ i ] != '\0' && i < max_len - 1; i++) buffer[ i ] = err[ i ];
        buffer[ 16 ] = '\0';
        return;
    }

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

    uint32_t fat_sector = fat_start_sector;
    if (virtio_blk_read_sector(fat_sector, disk_buffer) != 0) return;

    uint32_t *fat_table = (uint32_t *)disk_buffer;
    uint32_t free_cluster = 0;

    for (uint32_t i = 2; i < 128; i++) {
        if ((fat_table[ i ] & 0x0FFFFFFF) == 0) {
            free_cluster = i;
            fat_table[ i ] = 0x0FFFFFFF; 
            break;
        }
    }

    if (free_cluster == 0) return; 

    virtio_blk_write_sector(fat_sector, disk_buffer);

    uint32_t data_sector = cluster_to_sector(free_cluster);
    
    for(int i = 0; i < 512; i++) write_buffer[ i ] = 0;
    
    uint32_t bytes_to_copy = length < 512 ? length : 512;
    for(uint32_t i = 0; i < bytes_to_copy; i++) write_buffer[ i ] = data[ i ];

    virtio_blk_write_sector(data_sector, write_buffer);

    uint32_t root_sector = cluster_to_sector(root_dir_cluster);
    if (virtio_blk_read_sector(root_sector, disk_buffer) != 0) return;

    struct fat32_dir_entry *dir = (struct fat32_dir_entry *)disk_buffer;
    int free_idx = -1;

    for (int i = 0; i < 16; i++) {
        if (dir[ i ].name[ 0 ] == 0x00 || dir[ i ].name[ 0 ] == (char)0xE5) {
            free_idx = i;
            break;
        }
    }

    if (free_idx == -1) return;

    for (int j = 0; j < 11; j++) dir[ free_idx ].name[ j ] = target_name[ j ];
    dir[ free_idx ].attr = 0x20; 
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

    virtio_blk_write_sector(root_sector, disk_buffer);
}

/* PHASE 15: Append logic for the Ledger (with Phase 15.1 Cycle Guard AND Safety Fix) */
void fs_append_file_content(const char *filename, const char *data, uint32_t length) {
    char target_name[ 11 ];
    format_fat_name(filename, target_name);

    /* 1. Find the file in the Root Directory */
    uint32_t root_sector = cluster_to_sector(root_dir_cluster);
    if (virtio_blk_read_sector(root_sector, disk_buffer) != 0) return;

    struct fat32_dir_entry *dir = (struct fat32_dir_entry *)disk_buffer;
    int file_idx = -1;
    for (int i = 0; i < 16; i++) {
        int match = 1;
        for (int j = 0; j < 11; j++) {
            if (dir[ i ].name[ j ] != target_name[ j ]) match = 0;
        }
        if (match) { file_idx = i; break; }
    }

    /* If file doesn't exist, fall back to standard write */
    if (file_idx == -1) {
        fs_write_file_content(filename, data, length);
        return;
    }

    uint32_t current_cluster = ((uint32_t)dir[ file_idx ].fst_clus_hi << 16) | dir[ file_idx ].fst_clus_lo;
    uint32_t current_size = dir[ file_idx ].file_size;

    /* QA FIX: Cycle Guard for FAT Traversal */
    uint32_t fat_sector = fat_start_sector;
    
    /* DANGER ZONE: This read overwrites disk_buffer, obliterating the root directory data! */
    if (virtio_blk_read_sector(fat_sector, disk_buffer) != 0) return;
    
    uint32_t *fat_table = (uint32_t *)disk_buffer;
    uint32_t cycle_guard = 0;

    /* 0x0FFFFFF8 and above represents End of File (EOF) in FAT32 */
    while ((fat_table[ current_cluster ] & 0x0FFFFFFF) >= 2 && 
           (fat_table[ current_cluster ] & 0x0FFFFFFF) < 0x0FFFFFF8) {
        
        current_cluster = fat_table[ current_cluster ] & 0x0FFFFFFF;
        cycle_guard++;
        
        if (cycle_guard > max_valid_clusters) {
            uart_print("[FS] FATAL: Circular FAT chain detected! Aborting append.\n");
            return;
        }
    }

    /* 3. Load existing last sector, append data, and write back */
    uint32_t target_sector = cluster_to_sector(current_cluster);
    
    if (virtio_blk_read_sector(target_sector, write_buffer) != 0) return;
    
    uint32_t offset = current_size % 512;
    uint32_t space_left = 512 - offset;
    
    /* Simplified append: assumes payload fits within the remaining space of the last sector */
    uint32_t to_copy = length < space_left ? length : space_left;

    for (uint32_t i = 0; i < to_copy; i++) {
        write_buffer[ offset + i ] = data[ i ];
    }

    virtio_blk_write_sector(target_sector, write_buffer);

    /* --- THE CRITICAL SAFETY FIX --- */
    /* We MUST re-read the root directory from disk before updating it,
       because the cycle guard loop loaded the FAT table into disk_buffer! */
    if (virtio_blk_read_sector(root_sector, disk_buffer) != 0) return;
    
    /* Re-cast the pointer to the newly loaded directory buffer */
    dir = (struct fat32_dir_entry *)disk_buffer;

    /* 4. Update Directory Entry Size and save back to disk */
    dir[ file_idx ].file_size += to_copy;
    virtio_blk_write_sector(root_sector, disk_buffer);
}