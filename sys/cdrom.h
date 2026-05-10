#ifndef CDROM_H
#define CDROM_H

#include <stdint.h>

#define ATAPI_SECTOR_SIZE 2048

typedef struct {
    uint8_t status;
    uint8_t chs_first[3];
    uint8_t type;
    uint8_t chs_last[3];
    uint32_t lba_start;
    uint32_t sector_count;
} __attribute__((packed)) mbr_part_t;

typedef struct {
    uint8_t jump[3];
    char oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t media_descriptor;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint8_t drive_number;
    uint8_t reserved;
    uint8_t boot_signature;
    uint32_t volume_id;
    char volume_label[11];
    char fs_type[8];
} __attribute__((packed)) fat_boot_sector_t;

typedef struct {
    char name[8];
    char ext[3];
    uint8_t attributes;
    uint8_t reserved;
    uint8_t create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t first_cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed)) fat_dir_entry_t;

typedef struct {
    uint8_t type;
    char    id[5];
    uint8_t version;
    uint8_t unused[8];
    char    system_id[32];
    char    volume_id[32];
    uint8_t unused2[8];
    uint32_t vol_space_size;
    char    escape_seq[8];
    uint16_t set_size;
    uint16_t seq_num;
    uint16_t log_block_size;
    uint32_t path_table_size;
    uint32_t path_table_l;
    uint32_t path_table_l_opt;
    uint32_t path_table_m;
    uint32_t path_table_m_opt;
    uint32_t root_dir_lba;
    uint32_t root_dir_size;
    uint8_t root_dir[64];
} __attribute__((packed)) iso_pvd_t;

#ifdef __cplusplus
extern "C" {
#endif

int atapi_init(void);
int atapi_read_sector(uint32_t lba, uint8_t* buffer);
int atapi_mount_iso(void);
void fat_mount(void);
void fat_list_root(void);
int fat_read_file(const char* filename, uint8_t* buffer, uint32_t max_size);
void read_cdrom_root(uint32_t root_lba);
void cdrom_list_root(void);
int cdrom_copy_file(const char* filename, const char* dest_path);

#ifdef __cplusplus
}
#endif

#endif