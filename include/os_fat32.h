#ifndef OS_FAT32_H
#define OS_FAT32_H

#include "os_types.h"

/* --- Master Boot Record (MBR) --- */
struct mbr_partition_entry {
    uint8_t  status;
    uint8_t  chs_first[ 3 ];
    uint8_t  type;
    uint8_t  chs_last[ 3 ];
    uint32_t lba_start;
    uint32_t sector_count;
} __attribute__((packed));

struct mbr_sector {
    uint8_t  bootstrap[ 446 ];
    struct mbr_partition_entry partitions[ 4 ];
    uint16_t signature; /* 0xAA55 */
} __attribute__((packed));

/* --- FAT32 Boot Sector (BPB) --- */
struct fat32_bpb {
    uint8_t  jmp[ 3 ];
    char     oem_name[ 8 ];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_dir_entries; /* Always 0 for FAT32 */
    uint16_t total_sectors_16;
    uint8_t  media_descriptor;
    uint16_t sectors_per_fat_16; /* Always 0 for FAT32 */
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    
    /* FAT32 Extended fields */
    uint32_t sectors_per_fat_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[ 12 ];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[ 11 ];
    char     fs_type[ 8 ];
} __attribute__((packed));

/* --- FAT32 Directory Entry (32 Bytes) --- */
struct fat32_dir_entry {
    char     name[ 11 ];      /* 8 char name + 3 char extension */
    uint8_t  attr;            /* File attributes (Hidden, Directory, etc) */
    uint8_t  nt_res;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t fst_clus_hi;     /* High 16 bits of starting cluster */
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t fst_clus_lo;     /* Low 16 bits of starting cluster */
    uint32_t file_size;       /* Size in bytes */
} __attribute__((packed));

void fs_fat32_init(void);
void fs_list_root(void);
void fs_read_test_file(void);
void fs_get_dir_list(char *buffer, uint32_t max_len);
void fs_read_file_content(const char *filename, char *buffer, uint32_t max_len);

#endif