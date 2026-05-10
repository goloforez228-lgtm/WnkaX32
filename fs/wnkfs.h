#ifndef WNKFS_H
#define WNKFS_H

#include <stdint.h>

#define WNKFS_MAGIC         0x574E4B46
#define WNKFS_VERSION       0x0100
#define WNKFS_BLOCK_SIZE    512
#define WNKFS_NAME_LEN      32
#define WNKFS_MAX_FILES     1024
#define WNKFS_MAX_DIRS      256

#define WNKFS_TYPE_FILE         0
#define WNKFS_TYPE_DIR          1
#define WNKFS_TYPE_SYMLINK      2
#define WNKFS_TYPE_DEVICE       3

#define WNKFS_OK                0
#define WNKFS_ERR_NOT_FOUND     -1
#define WNKFS_ERR_EXISTS        -2
#define WNKFS_ERR_FULL          -3
#define WNKFS_ERR_READONLY      -4
#define WNKFS_ERR_CORRUPT       -5
#define WNKFS_ERR_IO            -6

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint64_t total_blocks;
    uint64_t free_blocks;
    uint32_t root_dir;
    uint32_t file_table;
    uint32_t dir_table;
    uint32_t bitmap_start;
    uint32_t bitmap_blocks;
    uint32_t data_start;
    char     volume_name[32];
    uint8_t  reserved[408];
} __attribute__((packed)) wnkfs_super_t;

typedef struct {
    uint32_t inode;
    char     name[WNKFS_NAME_LEN];
    uint16_t name_len;
    uint8_t  type;
    uint8_t  attributes;
    uint64_t size;
    uint32_t blocks;
    uint32_t first_block;
    uint32_t created;
    uint32_t modified;
    uint32_t accessed;
    uint8_t  reserved[32];
} __attribute__((packed)) wnkfs_dirent_t;

typedef struct {
    uint32_t inode;
    uint8_t  used;
    uint8_t  type;
    uint16_t links;
    uint32_t parent;
    uint64_t size;
    uint32_t blocks;
    uint32_t direct[12];
    uint32_t indirect;
    uint32_t double_indirect;
    uint32_t triple_indirect;
    uint32_t created;
    uint32_t modified;
    uint32_t accessed;
    uint8_t  reserved[64];
} __attribute__((packed)) wnkfs_inode_t;

typedef struct {
    uint32_t dir_id;
    uint32_t parent_id;
    char     name[WNKFS_NAME_LEN];
    uint32_t entry_count;
    uint32_t first_entry;
    uint8_t  reserved[32];
} __attribute__((packed)) wnkfs_dir_t;

#ifdef __cplusplus
extern "C" {
#endif

extern int wnkfs_mounted;
extern uint32_t current_dir;

extern int current_partition;

int wnkfs_format(void);
int wnkfs_mount(void);
void wnkfs_umount(void);
void wnkfs_info(void);
int wnkfs_list_dir(void);
int wnkfs_change_dir(const char* path);
int wnkfs_create_dir(const char* name);
int wnkfs_remove_dir(const char* name);
int wnkfs_create_file(const char* name);
int wnkfs_delete_file(const char* name);
int wnkfs_read_file(const char* name, uint8_t* buffer, uint32_t size);
int wnkfs_write_file(const char* name, uint8_t* data, uint32_t size);
int wnkfs_file_size(const char* name);
int wnkfs_rename_file(const char* old_name, const char* new_name);
int wnkfs_copy_file(const char* src, const char* dst);

int read_mbr(void);
void show_partitions(void);
int select_partition(int index);
int ext2_read_superblock(uint32_t offset);
void ext2_list_root(uint32_t offset);

int read_block(uint32_t block, uint8_t* buffer);
int write_block(uint32_t block, uint8_t* buffer);

void wnkfs_mega_scan(void);
void wnkfs_scan_all_ports(void);
int wnkfs_select_disk(void);
void wnkfs_show_disks(void);
int wnkfs_find_ssd(const char* model_part);

void cmd_fdisk(void);
void cmd_mount_part(int index);
void cmd_lsext2(void);
void cmd_testext2(void);

void wnkfs_test(void);
void wnkfs_init(void);
void wnkfs_debug_dump(void);
void wnkfs_debug_disk(void);

extern int current_partition;
extern int select_partition(int index);

#ifdef __cplusplus
}
#endif

#endif