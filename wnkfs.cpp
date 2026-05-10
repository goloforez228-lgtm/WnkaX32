#include "video.h"
#include "graph.h"
#include "wnkfs.h"
#include "ata.h"
#include <stdint.h>

#define MBR_SIGNATURE 0xAA55
#define EXT2_SUPER_MAGIC 0xEF53

typedef struct {
    uint8_t status;
    uint8_t chs_first[3];
    uint8_t type;
    uint8_t chs_last[3];
    uint32_t lba_first;
    uint32_t sector_count;
} __attribute__((packed)) mbr_entry_t;

typedef struct {
    uint8_t bootstrap[446];
    mbr_entry_t partitions[4];
    uint16_t signature;
} __attribute__((packed)) mbr_t;

typedef struct {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t s_uuid[16];
    char s_volume_name[16];
    char s_last_mounted[64];
    uint32_t s_algo_bitmap;
    uint8_t s_prealloc_blocks;
    uint8_t s_prealloc_dir_blocks;
    uint16_t s_padding1;
    uint8_t s_journal_uuid[16];
    uint32_t s_journal_inum;
    uint32_t s_journal_dev;
    uint32_t s_last_orphan;
    uint32_t s_hash_seed[4];
    uint8_t s_def_hash_version;
    uint8_t s_jnl_backup_type;
    uint16_t s_desc_size;
    uint32_t s_default_mount_opts;
    uint32_t s_first_meta_bg;
    uint32_t s_mkfs_time;
    uint32_t s_jnl_blocks[17];
} __attribute__((packed)) ext2_superblock_t;

typedef struct {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint8_t bg_reserved[12];
} __attribute__((packed)) ext2_block_group_desc_t;

typedef struct {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t i_osd2[12];
} __attribute__((packed)) ext2_inode_t;

static const char* get_part_type_name(uint8_t type) {
    switch(type) {
        case 0x00: return "Empty";
        case 0x01: return "FAT12";
        case 0x04: return "FAT16 (<32M)";
        case 0x05: return "Extended";
        case 0x06: return "FAT16 (>32M)";
        case 0x07: return "NTFS/exFAT";
        case 0x0B: return "FAT32";
        case 0x0C: return "FAT32 (LBA)";
        case 0x0E: return "FAT16 (LBA)";
        case 0x0F: return "Extended (LBA)";
        case 0x11: return "Hidden FAT12";
        case 0x14: return "Hidden FAT16";
        case 0x16: return "Hidden FAT16";
        case 0x1B: return "Hidden FAT32";
        case 0x1C: return "Hidden FAT32 (LBA)";
        case 0x1E: return "Hidden FAT16 (LBA)";
        case 0x42: return "Windows LDM";
        case 0x63: return "Unix SysV";
        case 0x82: return "Linux swap";
        case 0x83: return "Linux native (ext2/3/4)";
        case 0x85: return "Linux extended";
        case 0x8E: return "Linux LVM";
        case 0xDA: return "WNKFS";
        case 0xEE: return "GPT Protective";
        case 0xEF: return "EFI System";
        default: return "Unknown";
    }
}

static void* my_memset(void* ptr, int value, unsigned int num) {
    unsigned char* p = (unsigned char*)ptr;
    for(unsigned int i = 0; i < num; i++) {
        p[i] = (unsigned char)value;
    }
    return ptr;
}

static int my_strcmp(const char* s1, const char* s2) {
    while(*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static char* my_strcpy(char* dest, const char* src) {
    char* d = dest;
    while(*src) {
        *d++ = *src++;
    }
    *d = '\0';
    return dest;
}

static unsigned int my_strlen(const char* s) {
    unsigned int len = 0;
    while(s[len]) len++;
    return len;
}

static void my_memcpy(void* dest, const void* src, uint32_t n) {
    uint8_t* d = (uint8_t*)dest;
    uint8_t* s = (uint8_t*)src;
    for(uint32_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

int wnkfs_mounted = 0;
uint32_t current_dir = 0;
wnkfs_super_t super;
static uint8_t block_cache[512];
static uint32_t current_inode = 0;
static uint32_t partition_offset = 0;
static int mbr_valid = 0;
static mbr_entry_t mbr_partitions[4];
int current_partition = -1;
static int fs_type = 0;
static ext2_superblock_t ext2_sb;
static uint32_t ext2_block_size = 1024;
static uint32_t ext2_inodes_per_group;
static uint32_t ext2_blocks_per_group;
static uint32_t ext2_first_data_block;
static uint16_t ata_data_port = 0x1F0;
static uint16_t ata_error_port = 0x1F1;
static uint16_t ata_sector_count_port = 0x1F2;
static uint16_t ata_lba_low_port = 0x1F3;
static uint16_t ata_lba_mid_port = 0x1F4;
static uint16_t ata_lba_high_port = 0x1F5;
static uint16_t ata_device_port = 0x1F6;
static uint16_t ata_command_port = 0x1F7;
static uint16_t ata_status_port = 0x1F7;
static uint16_t ata_alt_status_port = 0x3F6;
static uint32_t ata_total_sectors = 0;



static int ata_drive_ready(void) {
    uint8_t status = inb(ata_status_port);
    return (status & 0x40) != 0;
}

static int ata_wait_ready(void) {
    int timeout = 10000000;
    while(timeout-- > 0) {
        uint8_t status = inb(ata_status_port);
        if((status & 0x80) == 0) return 1;
        if(timeout % 100000 == 0) {
            kprint(".");
        }
    }
    kprint_color("\n[ATA] Timeout!\n", TXT_RED);
    return 0;
}

static void ata_get_disk_size(void) {
    uint16_t identify[256];
    
    if(!ata_wait_ready()) {
        ata_total_sectors = 0;
        return;
    }
    
    outb(ata_device_port, 0xA0);
    outb(ata_sector_count_port, 0);
    outb(ata_lba_low_port, 0);
    outb(ata_lba_mid_port, 0);
    outb(ata_lba_high_port, 0);
    outb(ata_command_port, 0xEC);
    
    if(!ata_wait_ready()) {
        ata_total_sectors = 0;
        return;
    }
    
    for(int i = 0; i < 256; i++) {
        identify[i] = inw(ata_data_port);
    }
    
    ata_total_sectors = identify[60] | (identify[61] << 16);
    
    if(ata_total_sectors == 0) {
        uint16_t cylinders = identify[1];
        uint16_t heads = identify[3];
        uint16_t sectors = identify[6];
        ata_total_sectors = cylinders * heads * sectors;
    }
    
    kprint("[ATA] Disk size: ");
    if(ata_total_sectors >= 2097152) {
        kprint_int(ata_total_sectors / 2048 / 1024);
        kprint(" GB\n");
    } else {
        kprint_int(ata_total_sectors / 2048);
        kprint(" MB\n");
    }
}
static int ata_read_sector(uint32_t lba, uint16_t* buffer) {
    if(!ata_wait_ready()) return 0;
    
    outb(ata_device_port, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ata_sector_count_port, 1);
    outb(ata_lba_low_port, lba & 0xFF);
    outb(ata_lba_mid_port, (lba >> 8) & 0xFF);
    outb(ata_lba_high_port, (lba >> 16) & 0xFF);
    outb(ata_command_port, 0x20);
    
    if(!ata_wait_ready()) return 0;
    
    for(int i = 0; i < 256; i++) {
        buffer[i] = inw(ata_data_port);
    }
    
    return 1;
}

static int ata_write_sector(uint32_t lba, uint16_t* buffer) {
    if(!ata_wait_ready()) return 0;
    
    outb(ata_device_port, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ata_sector_count_port, 1);
    outb(ata_lba_low_port, lba & 0xFF);
    outb(ata_lba_mid_port, (lba >> 8) & 0xFF);
    outb(ata_lba_high_port, (lba >> 16) & 0xFF);
    outb(ata_command_port, 0x30);
    
    if(!ata_wait_ready()) return 0;
    
    for(int i = 0; i < 256; i++) {
        outw(ata_data_port, buffer[i]);
    }
    
    return 1;
}

static void ata_select_drive(uint8_t drive) {
    outb(ata_device_port, 0xA0 | (drive << 4));
    ata_wait_ready();
}


static int find_wnkfs_partition(void) {
    uint8_t buffer[512];
    mbr_t* mbr = (mbr_t*)buffer;
    uint16_t temp[256];
    
    kprint("[WNKFS] Checking for MBR...\n");
    
    if(!ata_read_sector(0, temp)) {
        kprint("[WNKFS] ERROR: Cannot read sector 0\n");
        return -1;
    }
    
    for(int i = 0; i < 256; i++) {
        buffer[i*2] = temp[i] & 0xFF;
        buffer[i*2 + 1] = (temp[i] >> 8) & 0xFF;
    }
    
    if(mbr->signature == MBR_SIGNATURE) {
        kprint("[WNKFS] Valid MBR found (0xAA55)\n");
        
        for(int i = 0; i < 4; i++) {
            if(mbr->partitions[i].type != 0) {
                kprint("  Partition "); kprint_int(i); kprint(": type 0x");
                kprint_hex8(mbr->partitions[i].type);
                kprint(" start="); kprint_int(mbr->partitions[i].lba_first);
                kprint(" size="); kprint_int(mbr->partitions[i].sector_count); kprint("\n");
                
                if(mbr->partitions[i].type == 0xDA || mbr->partitions[i].type == 0x83) {
                    kprint("[WNKFS] Found partition at sector ");
                    kprint_int(mbr->partitions[i].lba_first);
                    kprint("\n");
                    return mbr->partitions[i].lba_first;
                }
            }
        }
        
        kprint("[WNKFS] No WNKFS partition found in MBR\n");
        return -1;
    } else {
        kprint("[WNKFS] No valid MBR found (signature: 0x");
        kprint_hex16(mbr->signature);
        kprint(")\n");
        
        kprint("[WNKFS] Create new MBR with WNKFS partition? (y/n): ");
        
        int timeout = 10000000;
        char response = 0;
        while(timeout-- && !response) {
            if(inb(0x64) & 1) {
                uint8_t sc = inb(0x60);
                if(sc == 0x15) response = 'y';
                if(sc == 0x31) response = 'n';
                while(inb(0x64) & 1) inb(0x60);
            }
        }
        
        kprint("\n");
        
        if(response == 'y') {
            kprint("[WNKFS] Creating new MBR...\n");
            
            for(int i = 0; i < 512; i++) buffer[i] = 0;
            
            mbr->partitions[0].status = 0x80;
            mbr->partitions[0].type = 0xDA;
            mbr->partitions[0].lba_first = 2048;
            mbr->partitions[0].sector_count = (uint32_t)(ata_total_sectors - 2048);
            mbr->signature = MBR_SIGNATURE;
            
            for(int i = 0; i < 256; i++) {
                temp[i] = buffer[i*2] | (buffer[i*2+1] << 8);
            }
            
            if(ata_write_sector(0, temp)) {
                kprint("[WNKFS] MBR created successfully!\n");
                return 2048;
            } else {
                kprint("[WNKFS] ERROR: Failed to write MBR\n");
                return -1;
            }
        } else {
            kprint("[WNKFS] Using raw disk mode (no partition table)\n");
            return 0;
        }
    }
}


int read_mbr(void) {
    uint8_t buffer[512];
    mbr_t* mbr = (mbr_t*)buffer;
    uint16_t temp[256];
    
    if(!ata_read_sector(0, temp)) {
        kprint("[MBR] ERROR: Cannot read sector 0\n");
        return -1;
    }
    
    for(int i = 0; i < 256; i++) {
        buffer[i*2] = temp[i] & 0xFF;
        buffer[i*2 + 1] = (temp[i] >> 8) & 0xFF;
    }
    
    if(mbr->signature != MBR_SIGNATURE) {
        kprint("[MBR] No valid MBR found (signature: 0x");
        kprint_hex16(mbr->signature);
        kprint(")\n");
        mbr_valid = 0;
        return -1;
    }
    
    kprint("[MBR] Valid MBR found (0xAA55)\n");
    mbr_valid = 1;
    
    for(int i = 0; i < 4; i++) {
        mbr_partitions[i] = mbr->partitions[i];
    }
    
    return 0;
}

void show_partitions(void) {
    if(!mbr_valid) {
        if(read_mbr() != 0) {
            kprint("No MBR table available\n");
            return;
        }
    }
    
    kprint("\n=== MBR PARTITION TABLE ===\n");
    kprint("Part | Status | Type               | Start LBA | Size (sectors)\n");
    kprint("-----|--------|--------------------|-----------|---------------\n");
    
    for(int i = 0; i < 4; i++) {
        if(mbr_partitions[i].type == 0) continue;
        
        char status_str[4] = "   ";
        if(mbr_partitions[i].status & 0x80) {
            status_str[0] = 'B';
            status_str[1] = 'O';
            status_str[2] = 'O';
        }
        
        kprint("  ");
        kprint_int(i + 1);
        kprint("   |  ");
        kprint(status_str);
        kprint("  | ");
        
        const char* type_name = get_part_type_name(mbr_partitions[i].type);
        kprint(type_name);
        
        int len = 0;
        while(type_name[len]) len++;
        for(int j = len; j < 20; j++) kprint(" ");
        
        kprint(" | ");
        kprint_int(mbr_partitions[i].lba_first);
        kprint("     | ");
        kprint_int(mbr_partitions[i].sector_count);
        kprint("\n");
    }
    kprint("-------------------------------------------\n");
}

int select_partition(int index) {
    if(!mbr_valid) {
        if(read_mbr() != 0) {
            kprint("No MBR table available\n");
            return -1;
        }
    }
    
    if(index < 1 || index > 4) {
        kprint("Invalid partition index (1-4)\n");
        return -1;
    }
    
    if(mbr_partitions[index-1].type == 0) {
        kprint("Partition is empty\n");
        return -1;
    }
    
    current_partition = index - 1;
    partition_offset = mbr_partitions[current_partition].lba_first;
    
    kprint("Selected partition ");
    kprint_int(index);
    kprint(" at sector ");
    kprint_int(partition_offset);
    kprint("\n");
    
    if(mbr_partitions[current_partition].type == 0x83) {
        kprint("Detected Linux partition. Attempting to mount ext2...\n");
        fs_type = 1;
    } else if(mbr_partitions[current_partition].type == 0xDA) {
        kprint("Detected WNKFS partition\n");
        fs_type = 0;
    } else {
        kprint("Unknown filesystem type. Defaulting to raw sector access.\n");
        fs_type = -1;
    }
    
    return 0;
}


static int ext2_read_superblock(void) {
    uint8_t buffer[1024];
    uint16_t temp[512];
    
    uint32_t sb_sector = partition_offset + 2;
    
    for(int i = 0; i < 2; i++) {
        if(!ata_read_sector(sb_sector + i, temp + i*256)) {
            kprint("[EXT2] Failed to read superblock sector\n");
            return -1;
        }
    }
    
    for(int i = 0; i < 512; i++) {
        buffer[i*2] = temp[i] & 0xFF;
        buffer[i*2 + 1] = (temp[i] >> 8) & 0xFF;
    }
    
    my_memset(&ext2_sb, 0, sizeof(ext2_superblock_t));
    my_memcpy(&ext2_sb, buffer, sizeof(ext2_superblock_t));
    
    if(ext2_sb.s_magic != EXT2_SUPER_MAGIC) {
        kprint("[EXT2] Invalid superblock magic: 0x");
        kprint_hex16(ext2_sb.s_magic);
        kprint("\n");
        return -1;
    }
    
    ext2_block_size = 1024 << ext2_sb.s_log_block_size;
    ext2_inodes_per_group = ext2_sb.s_inodes_per_group;
    ext2_blocks_per_group = ext2_sb.s_blocks_per_group;
    ext2_first_data_block = ext2_sb.s_first_data_block;
    
    kprint("[EXT2] Valid ext2 superblock found!\n");
    kprint("  Volume name: ");
    for(int i = 0; i < 16 && ext2_sb.s_volume_name[i]; i++) {
        char c[2] = {ext2_sb.s_volume_name[i], '\0'};
        kprint(c);
    }
    kprint("\n");
    kprint("  Block size: "); kprint_int(ext2_block_size); kprint(" bytes\n");
    kprint("  Total blocks: "); kprint_int(ext2_sb.s_blocks_count); kprint("\n");
    kprint("  Inodes: "); kprint_int(ext2_sb.s_inodes_count); kprint("\n");
    
    return 0;
}

static int ext2_read_inode(uint32_t inode_num, ext2_inode_t* inode) {
    uint32_t group = (inode_num - 1) / ext2_inodes_per_group;
    uint32_t index = (inode_num - 1) % ext2_inodes_per_group;
    
    uint32_t bgdt_block = 2;
    uint32_t bgdt_offset = group * sizeof(ext2_block_group_desc_t);
    
    uint8_t buffer[512];
    uint16_t temp[256];
    
    uint32_t sector = partition_offset + bgdt_block * (ext2_block_size / 512) + (bgdt_offset / 512);
    uint32_t offset_in_sector = bgdt_offset % 512;
    
    if(!ata_read_sector(sector, temp)) return -1;
    
    for(int i = 0; i < 256; i++) {
        buffer[i*2] = temp[i] & 0xFF;
        buffer[i*2 + 1] = (temp[i] >> 8) & 0xFF;
    }
    
    ext2_block_group_desc_t* bgd = (ext2_block_group_desc_t*)(buffer + offset_in_sector);
    
    uint32_t inode_table_block = bgd->bg_inode_table;
    uint32_t inode_size = ext2_sb.s_inode_size;
    uint32_t inode_offset = index * inode_size;
    
    uint32_t inode_sector = partition_offset + inode_table_block * (ext2_block_size / 512) + (inode_offset / 512);
    uint32_t inode_offset_in_sector = inode_offset % 512;
    
    if(!ata_read_sector(inode_sector, temp)) return -1;
    
    for(int i = 0; i < 256; i++) {
        buffer[i*2] = temp[i] & 0xFF;
        buffer[i*2 + 1] = (temp[i] >> 8) & 0xFF;
    }
    
    my_memcpy(inode, buffer + inode_offset_in_sector, sizeof(ext2_inode_t));
    
    return 0;
}

static void ext2_list_root(void) {
    ext2_inode_t root_inode;
    if(ext2_read_inode(2, &root_inode) != 0) {
        kprint("[EXT2] Failed to read root inode\n");
        return;
    }
    
    kprint("\nEXT2 Root directory:\n");
    kprint("------------------\n");
    
    uint8_t block_buffer[4096];
    uint16_t temp[2048];
    
    for(int i = 0; i < 12 && root_inode.i_block[i]; i++) {
        uint32_t block = root_inode.i_block[i];
        uint32_t block_sectors = ext2_block_size / 512;
        
        for(uint32_t s = 0; s < block_sectors; s++) {
            if(!ata_read_sector(partition_offset + block * block_sectors + s, temp + s*256)) {
                kprint("[EXT2] Failed to read block\n");
                return;
            }
        }
        
        for(uint32_t s = 0; s < block_sectors; s++) {
            for(int j = 0; j < 256; j++) {
                uint32_t idx = s*512 + j*2;
                block_buffer[idx] = temp[s*256 + j] & 0xFF;
                block_buffer[idx + 1] = (temp[s*256 + j] >> 8) & 0xFF;
            }
        }
        
        uint32_t offset = 0;
        while(offset < ext2_block_size) {
            uint32_t inode = *(uint32_t*)(block_buffer + offset);
            uint16_t rec_len = *(uint16_t*)(block_buffer + offset + 4);
            uint8_t name_len = *(uint8_t*)(block_buffer + offset + 6);
            uint8_t file_type = *(uint8_t*)(block_buffer + offset + 7);
            char* name = (char*)(block_buffer + offset + 8);
            
            if(inode == 0) {
                offset += rec_len;
                continue;
            }
            
            kprint("  ");
            if(file_type == 1) kprint("[DIR]  ");
            else if(file_type == 2) kprint("[CHR]  ");
            else kprint("[FILE] ");
            
            for(int j = 0; j < name_len; j++) {
                char c[2] = {name[j], '\0'};
                kprint(c);
            }
            kprint("\n");
            
            offset += rec_len;
        }
    }
    
    kprint("------------------\n");
}


int read_block(uint32_t block, uint8_t* buffer) {
    if(partition_offset == 0) {
        partition_offset = find_wnkfs_partition();
        if(partition_offset < 0) {
            kprint("[WNKFS] No valid partition found!\n");
            return -1;
        }
    }
    
    uint32_t physical_sector = partition_offset + block;
    uint16_t temp[256];
    
    if(!ata_read_sector(physical_sector, temp)) {
        kprint("[WNKFS] Read error at block ");
        kprint_int(block);
        kprint("\n");
        return -1;
    }
    
    for(int i = 0; i < 256; i++) {
        buffer[i*2] = temp[i] & 0xFF;
        buffer[i*2 + 1] = (temp[i] >> 8) & 0xFF;
    }
    return 0;
}

int write_block(uint32_t block, uint8_t* buffer) {
    if(partition_offset == 0) {
        partition_offset = find_wnkfs_partition();
        if(partition_offset < 0) return -1;
    }
    
    uint32_t physical_sector = partition_offset + block;
    uint16_t temp[256];
    
    for(int i = 0; i < 256; i++) {
        temp[i] = buffer[i*2] | (buffer[i*2+1] << 8);
    }
    
    if(!ata_write_sector(physical_sector, temp)) {
        kprint("[WNKFS] Write error at block ");
        kprint_int(block);
        kprint("\n");
        return -1;
    }
    return 0;
}


static int bitmap_test(uint32_t block) {
    uint32_t bitmap_block = super.bitmap_start + (block / 4096);
    uint32_t bit_index = block % 4096;
    
    uint8_t buffer[512];
    if(read_block(bitmap_block, buffer) != 0) return 0;
    
    uint32_t byte_index = bit_index / 8;
    uint8_t bit_mask = 1 << (bit_index % 8);
    
    return (buffer[byte_index] & bit_mask) ? 1 : 0;
}

static void bitmap_set(uint32_t block, int used) {
    uint32_t bitmap_block = super.bitmap_start + (block / 4096);
    uint32_t bit_index = block % 4096;
    
    uint8_t buffer[512];
    if(read_block(bitmap_block, buffer) != 0) return;
    
    uint32_t byte_index = bit_index / 8;
    uint8_t bit_mask = 1 << (bit_index % 8);
    
    if(used) {
        buffer[byte_index] |= bit_mask;
        super.free_blocks--;
    } else {
        buffer[byte_index] &= ~bit_mask;
        super.free_blocks++;
    }
    
    write_block(bitmap_block, buffer);
    write_block(0, (uint8_t*)&super);
}

static uint32_t alloc_block(void) {
    if(super.free_blocks == 0) return 0;
    
    for(uint32_t block = super.data_start; block < super.total_blocks; block++) {
        if(!bitmap_test(block)) {
            bitmap_set(block, 1);
            return block;
        }
    }
    return 0;
}

static void free_block(uint32_t block) {
    bitmap_set(block, 0);
}


int wnkfs_format(void) {
    kprint("\n[WNKFS] Formatting partition...\n");
    if(ata_total_sectors == 0) {
    kprint("[WNKFS] ERROR: ata_total_sectors not initialized!\n");
    return -1;
    }
    
    if(partition_offset == 0) {
        partition_offset = find_wnkfs_partition();
        if(partition_offset < 0) {
            kprint("[WNKFS] Cannot format - no valid partition!\n");
            return -1;
        }
    }
    
    uint8_t buffer[512];
    my_memset(buffer, 0, 512);
    
    uint32_t available_sectors = (uint32_t)(ata_total_sectors - partition_offset);
    super.total_blocks = available_sectors;
    super.free_blocks = super.total_blocks - 100;
    
    super.magic = WNKFS_MAGIC;
    super.version = WNKFS_VERSION;
    super.root_dir = 1;
    super.file_table = 2;
    super.dir_table = 3;
    super.bitmap_start = 4;
    super.bitmap_blocks = (super.total_blocks + 4095) / 4096;
    super.data_start = super.bitmap_start + super.bitmap_blocks;
    
    const char* volname = "WNKA SSD";
    for(int i = 0; i < 32 && volname[i]; i++) {
        super.volume_name[i] = volname[i];
    }
    
    kprint("[WNKFS] Total blocks: "); kprint_int((uint32_t)super.total_blocks); kprint("\n");
    kprint("[WNKFS] Data starts at: "); kprint_int(super.data_start); kprint("\n");
    
    if(write_block(0, (uint8_t*)&super) != 0) {
        kprint("[WNKFS] Failed to write superblock!\n");
        return -1;
    }
    
    for(uint32_t i = 0; i < super.bitmap_blocks; i++) {
        my_memset(buffer, 0, 512);
        
        if(i == 0) {
            for(uint32_t b = 0; b < super.data_start; b++) {
                uint32_t bit_index = b % 4096;
                uint32_t byte_index = bit_index / 8;
                buffer[byte_index] |= (1 << (bit_index % 8));
            }
        }
        
        if(write_block(super.bitmap_start + i, buffer) != 0) {
            kprint("[WNKFS] Failed to write bitmap\n");
            return -1;
        }
    }
    
    wnkfs_inode_t root_inode;
    my_memset(&root_inode, 0, sizeof(wnkfs_inode_t));
    root_inode.inode = 1;
    root_inode.used = 1;
    root_inode.type = 1;
    root_inode.parent = 1;
    root_inode.direct[0] = alloc_block();
    root_inode.blocks = 1;
    
    if(write_block(super.file_table, (uint8_t*)&root_inode) != 0) {
        kprint("[WNKFS] Failed to write root inode\n");
        return -1;
    }
    
    wnkfs_dirent_t root_dirent[16];
    my_memset(root_dirent, 0, sizeof(root_dirent));
    
    root_dirent[0].inode = 1;
    my_strcpy((char*)root_dirent[0].name, ".");
    root_dirent[0].name_len = 1;
    root_dirent[0].type = 1;
    
    root_dirent[1].inode = 1;
    my_strcpy((char*)root_dirent[1].name, "..");
    root_dirent[1].name_len = 2;
    root_dirent[1].type = 1;
    
    if(write_block(root_inode.direct[0], (uint8_t*)root_dirent) != 0) {
        kprint("[WNKFS] Failed to write root directory\n");
        return -1;
    }
    
    wnkfs_dir_t root_dir_entry;
    my_memset(&root_dir_entry, 0, sizeof(wnkfs_dir_t));
    root_dir_entry.dir_id = 1;
    root_dir_entry.parent_id = 1;
    my_strcpy((char*)root_dir_entry.name, "/");
    root_dir_entry.entry_count = 2;
    root_dir_entry.first_entry = 0;
    
    if(write_block(super.dir_table, (uint8_t*)&root_dir_entry) != 0) {
        kprint("[WNKFS] Failed to write root dir table\n");
        return -1;
    }
    
    kprint("[WNKFS] Format complete!\n");
    return 0;
}

int wnkfs_mount(void) {
    kprint("\n[WNKFS] Mounting...\n");
    
    if(partition_offset == 0) {
        partition_offset = find_wnkfs_partition();
        if(partition_offset < 0) {
            kprint("[WNKFS] No valid partition found!\n");
            return -1;
        }
    }
    
    kprint("[WNKFS] Reading superblock at block 0 (sector ");
    kprint_int(partition_offset);
    kprint(")...\n");
    
    if(read_block(0, (uint8_t*)&super) != 0) {
        kprint("[WNKFS] Failed to read superblock\n");
        return -1;
    }
    
    if(super.magic != WNKFS_MAGIC) {
        kprint("[WNKFS] Invalid magic - need format\n");
        return -1;
    }
    
    wnkfs_mounted = 1;
    current_dir = super.root_dir;
    
    kprint("[WNKFS] Volume: ");
    for(int i = 0; i < 32 && super.volume_name[i]; i++) {
        char s[2] = {super.volume_name[i], '\0'};
        kprint(s);
    }
    kprint("\n");
    
    uint32_t size_mb = (uint32_t)(super.total_blocks * 512 / 1024 / 1024);
    uint32_t size_gb = size_mb / 1024;
    
    kprint("[WNKFS] Size: ");
    if(size_gb > 0) {
        kprint_int(size_gb); kprint(" GB");
        if(size_mb % 1024 > 0) {
            kprint(" ("); kprint_int(size_mb % 1024); kprint(" MB)");
        }
    } else {
        kprint_int(size_mb); kprint(" MB");
    }
    kprint("\n");
    
    kprint("[WNKFS] Mount successful!\n");
    return 0;
}

void wnkfs_umount(void) {
    wnkfs_mounted = 0;
    current_dir = 0;
    kprint("[WNKFS] Unmounted\n");
}

void wnkfs_info(void) {
    if(!wnkfs_mounted) {
        kprint("[WNKFS] Not mounted\n");
        return;
    }
    
    kprint("\n=== WNKFS INFORMATION ===\n");
    kprint("Volume: ");
    for(int i = 0; i < 32 && super.volume_name[i]; i++) {
        char s[2] = {super.volume_name[i], '\0'};
        kprint(s);
    }
    kprint("\n");
    
    uint32_t total_mb = (uint32_t)(super.total_blocks / 2);
    uint32_t free_mb = (uint32_t)(super.free_blocks / 2);
    uint32_t used_mb = total_mb - free_mb;
    
    kprint("Total: "); kprint_int(total_mb); kprint(" MB (");
    kprint_int(total_mb / 1024); kprint(" GB)\n");
    kprint("Free:  "); kprint_int(free_mb); kprint(" MB (");
    kprint_int(free_mb / 1024); kprint(" GB)\n");
    kprint("Used:  "); kprint_int(used_mb); kprint(" MB (");
    kprint_int(used_mb / 1024); kprint(" GB)\n");
    kprint("Free blocks: "); kprint_int((uint32_t)super.free_blocks); kprint("\n");
    kprint("Partition offset: sector "); kprint_int(partition_offset); kprint("\n");
}

int wnkfs_list_dir(void) {
    if(!wnkfs_mounted) return -1;
    
    wnkfs_inode_t dir;
    if(read_block(super.file_table + current_dir - 1, (uint8_t*)&dir) != 0) {
        return -1;
    }
    
    kprint("\nDirectory listing of ");
    
    wnkfs_dir_t dir_entry;
    read_block(super.dir_table + current_dir - 1, (uint8_t*)&dir_entry);
    kprint((char*)dir_entry.name);
    kprint(":\n");
    
    kprint("------------------\n");
    
    wnkfs_dirent_t entries[16];
    int total = 0;
    int files = 0;
    int dirs = 0;
    
    for(int b = 0; b < 12 && dir.direct[b]; b++) {
        if(read_block(dir.direct[b], (uint8_t*)entries) != 0) continue;
        
        for(int i = 0; i < 16; i++) {
            if(entries[i].inode) {
                total++;
                if(entries[i].type == 1) {
                    kprint("[DIR]  ");
                    dirs++;
                } else {
                    kprint("[FILE] ");
                    files++;
                }
                
                for(int j = 0; j < WNKFS_NAME_LEN && entries[i].name[j]; j++) {
                    char s[2] = {entries[i].name[j], '\0'};
                    kprint(s);
                }
                
                int len = 0;
                while(entries[i].name[len]) len++;
                for(int p = len; p < 20; p++) kprint(" ");
                
                kprint_int((uint32_t)entries[i].size);
                kprint(" bytes\n");
            }
        }
    }
    
    kprint("------------------\n");
    kprint_int(total); kprint(" entries (");
    kprint_int(dirs); kprint(" dirs, ");
    kprint_int(files); kprint(" files)\n");
    
    return 0;
}

int wnkfs_change_dir(const char* path) {
    if(!wnkfs_mounted) return -1;
    
    if(my_strcmp(path, "/") == 0) {
        current_dir = super.root_dir;
        return 0;
    }
    
    if(my_strcmp(path, "..") == 0) {
        wnkfs_inode_t dir;
        read_block(super.file_table + current_dir - 1, (uint8_t*)&dir);
        current_dir = dir.parent;
        return 0;
    }
    
    wnkfs_inode_t dir;
    read_block(super.file_table + current_dir - 1, (uint8_t*)&dir);
    
    wnkfs_dirent_t entries[16];
    for(int b = 0; b < 12 && dir.direct[b]; b++) {
        read_block(dir.direct[b], (uint8_t*)entries);
        for(int i = 0; i < 16; i++) {
            if(entries[i].inode && entries[i].type == 1 &&
               my_strcmp((char*)entries[i].name, path) == 0) {
                current_dir = entries[i].inode;
                return 0;
            }
        }
    }
    
    return -1;
}

int wnkfs_create_dir(const char* name) {
    if(!wnkfs_mounted) return -1;
    
    wnkfs_inode_t dir;
    read_block(super.file_table + current_dir - 1, (uint8_t*)&dir);
    
    wnkfs_dirent_t entries[16];
    for(int b = 0; b < 12 && dir.direct[b]; b++) {
        read_block(dir.direct[b], (uint8_t*)entries);
        for(int i = 0; i < 16; i++) {
            if(entries[i].inode && my_strcmp((char*)entries[i].name, name) == 0) {
                return -2;
            }
        }
    }
    
    wnkfs_inode_t new_inode;
    uint32_t inode_num = 0;
    for(uint32_t i = 1; i <= super.total_blocks; i++) {
        read_block(super.file_table + i - 1, (uint8_t*)&new_inode);
        if(!new_inode.used) {
            inode_num = i;
            break;
        }
    }
    
    if(inode_num == 0) return -3;
    
    my_memset(&new_inode, 0, sizeof(wnkfs_inode_t));
    new_inode.inode = inode_num;
    new_inode.used = 1;
    new_inode.type = 1;
    new_inode.parent = current_dir;
    new_inode.direct[0] = alloc_block();
    new_inode.blocks = 1;
    
    write_block(super.file_table + inode_num - 1, (uint8_t*)&new_inode);
    
    wnkfs_dirent_t new_entries[16];
    my_memset(new_entries, 0, sizeof(new_entries));
    
    new_entries[0].inode = inode_num;
    my_strcpy((char*)new_entries[0].name, ".");
    new_entries[0].type = 1;
    
    new_entries[1].inode = current_dir;
    my_strcpy((char*)new_entries[1].name, "..");
    new_entries[1].type = 1;
    
    write_block(new_inode.direct[0], (uint8_t*)new_entries);
    
    for(int b = 0; b < 12 && dir.direct[b]; b++) {
        read_block(dir.direct[b], (uint8_t*)entries);
        for(int i = 0; i < 16; i++) {
            if(entries[i].inode == 0) {
                entries[i].inode = inode_num;
                my_strcpy((char*)entries[i].name, name);
                entries[i].name_len = my_strlen(name);
                entries[i].type = 1;
                entries[i].size = 0;
                entries[i].first_block = new_inode.direct[0];
                write_block(dir.direct[b], (uint8_t*)entries);
                
                wnkfs_dir_t dir_entry;
                read_block(super.dir_table + inode_num - 1, (uint8_t*)&dir_entry);
                my_strcpy((char*)dir_entry.name, name);
                dir_entry.dir_id = inode_num;
                dir_entry.parent_id = current_dir;
                dir_entry.entry_count = 2;
                dir_entry.first_entry = 0;
                write_block(super.dir_table + inode_num - 1, (uint8_t*)&dir_entry);
                
                return 0;
            }
        }
    }
    
    return -3;
}

int wnkfs_remove_dir(const char* name) {
    if(!wnkfs_mounted) return -1;
    
    wnkfs_inode_t dir;
    read_block(super.file_table + current_dir - 1, (uint8_t*)&dir);
    
    wnkfs_dirent_t entries[16];
    for(int b = 0; b < 12 && dir.direct[b]; b++) {
        read_block(dir.direct[b], (uint8_t*)entries);
        for(int i = 0; i < 16; i++) {
            if(entries[i].inode && entries[i].type == 1 &&
               my_strcmp((char*)entries[i].name, name) == 0) {
                
                wnkfs_inode_t subdir;
                read_block(super.file_table + entries[i].inode - 1, (uint8_t*)&subdir);
                
                wnkfs_dirent_t subentries[16];
                int entry_count = 0;
                for(int sb = 0; sb < 12 && subdir.direct[sb]; sb++) {
                    read_block(subdir.direct[sb], (uint8_t*)subentries);
                    for(int si = 0; si < 16; si++) {
                        if(subentries[si].inode) entry_count++;
                    }
                }
                
                if(entry_count > 2) return -1;
                
                for(int d = 0; d < 12 && subdir.direct[d]; d++) {
                    free_block(subdir.direct[d]);
                }
                
                subdir.used = 0;
                write_block(super.file_table + entries[i].inode - 1, (uint8_t*)&subdir);
                
                entries[i].inode = 0;
                entries[i].name[0] = '\0';
                write_block(dir.direct[b], (uint8_t*)entries);
                
                return 0;
            }
        }
    }
    
    return -1;
}


int wnkfs_create_file(const char* name) {
    if(!wnkfs_mounted) return -1;
    
    wnkfs_inode_t dir;
    read_block(super.file_table + current_dir - 1, (uint8_t*)&dir);
    
    wnkfs_dirent_t entries[16];
    for(int b = 0; b < 12 && dir.direct[b]; b++) {
        read_block(dir.direct[b], (uint8_t*)entries);
        for(int i = 0; i < 16; i++) {
            if(entries[i].inode && my_strcmp((char*)entries[i].name, name) == 0) {
                return -2;
            }
        }
    }
    
    wnkfs_inode_t inode;
    uint32_t inode_num = 0;
    for(uint32_t i = 1; i <= super.total_blocks; i++) {
        read_block(super.file_table + i - 1, (uint8_t*)&inode);
        if(!inode.used) {
            inode_num = i;
            break;
        }
    }
    
    if(inode_num == 0) return -3;
    
    my_memset(&inode, 0, sizeof(wnkfs_inode_t));
    inode.inode = inode_num;
    inode.used = 1;
    inode.type = 0;
    inode.parent = current_dir;
    inode.direct[0] = alloc_block();
    inode.blocks = 1;
    
    write_block(super.file_table + inode_num - 1, (uint8_t*)&inode);
    
    for(int b = 0; b < 12 && dir.direct[b]; b++) {
        read_block(dir.direct[b], (uint8_t*)entries);
        for(int i = 0; i < 16; i++) {
            if(entries[i].inode == 0) {
                entries[i].inode = inode_num;
                my_strcpy((char*)entries[i].name, name);
                entries[i].name_len = my_strlen(name);
                entries[i].type = 0;
                entries[i].size = 0;
                entries[i].first_block = inode.direct[0];
                write_block(dir.direct[b], (uint8_t*)entries);
                return 0;
            }
        }
    }
    
    return -3;
}

int wnkfs_delete_file(const char* name) {
    if(!wnkfs_mounted) return -1;
    
    wnkfs_inode_t dir;
    read_block(super.file_table + current_dir - 1, (uint8_t*)&dir);
    
    wnkfs_dirent_t entries[16];
    for(int b = 0; b < 12 && dir.direct[b]; b++) {
        read_block(dir.direct[b], (uint8_t*)entries);
        for(int i = 0; i < 16; i++) {
            if(entries[i].inode && my_strcmp((char*)entries[i].name, name) == 0) {
                wnkfs_inode_t inode;
                read_block(super.file_table + entries[i].inode - 1, (uint8_t*)&inode);
                
                for(int d = 0; d < 12 && inode.direct[d]; d++) {
                    free_block(inode.direct[d]);
                }
                
                inode.used = 0;
                write_block(super.file_table + entries[i].inode - 1, (uint8_t*)&inode);
                
                entries[i].inode = 0;
                entries[i].name[0] = '\0';
                write_block(dir.direct[b], (uint8_t*)entries);
                
                return 0;
            }
        }
    }
    
    return -1;
}

int wnkfs_read_file(const char* name, uint8_t* buffer, uint32_t size) {
    if(!wnkfs_mounted) return -1;
    
    wnkfs_inode_t dir;
    read_block(super.file_table + current_dir - 1, (uint8_t*)&dir);
    
    wnkfs_dirent_t entries[16];
    for(int b = 0; b < 12 && dir.direct[b]; b++) {
        read_block(dir.direct[b], (uint8_t*)entries);
        for(int i = 0; i < 16; i++) {
            if(entries[i].inode && my_strcmp((char*)entries[i].name, name) == 0) {
                wnkfs_inode_t inode;
                read_block(super.file_table + entries[i].inode - 1, (uint8_t*)&inode);
                
                uint32_t to_read = (inode.size < size) ? (uint32_t)inode.size : size;
                uint32_t blocks = (to_read + 511) / 512;
                
                for(uint32_t b = 0; b < blocks && b < 12; b++) {
                    read_block(inode.direct[b], buffer + b * 512);
                }
                
                return to_read;
            }
        }
    }
    
    return -1;
}

int wnkfs_write_file(const char* name, uint8_t* data, uint32_t size) {
    if(!wnkfs_mounted) return -1;
    
    wnkfs_inode_t dir;
    read_block(super.file_table + current_dir - 1, (uint8_t*)&dir);
    
    wnkfs_dirent_t entries[16];
    for(int b = 0; b < 12 && dir.direct[b]; b++) {
        read_block(dir.direct[b], (uint8_t*)entries);
        for(int i = 0; i < 16; i++) {
            if(entries[i].inode && my_strcmp((char*)entries[i].name, name) == 0) {
                wnkfs_inode_t inode;
                read_block(super.file_table + entries[i].inode - 1, (uint8_t*)&inode);
                
                uint32_t blocks = (size + 511) / 512;
                
                for(uint32_t b = blocks; b < inode.blocks; b++) {
                    if(inode.direct[b]) free_block(inode.direct[b]);
                }
                
                for(uint32_t b = 0; b < blocks; b++) {
                    if(b >= 12) break;
                    if(inode.direct[b] == 0) {
                        inode.direct[b] = alloc_block();
                    }
                    write_block(inode.direct[b], data + b * 512);
                }
                
                inode.size = size;
                inode.blocks = blocks;
                write_block(super.file_table + entries[i].inode - 1, (uint8_t*)&inode);
                
                entries[i].size = size;
                write_block(dir.direct[b], (uint8_t*)entries);
                
                return size;
            }
        }
    }
    
    int result = wnkfs_create_file(name);
    if(result != 0) return result;
    
    return wnkfs_write_file(name, data, size);
}

int wnkfs_file_size(const char* name) {
    if(!wnkfs_mounted) return -1;
    
    wnkfs_inode_t dir;
    read_block(super.file_table + current_dir - 1, (uint8_t*)&dir);
    
    wnkfs_dirent_t entries[16];
    for(int b = 0; b < 12 && dir.direct[b]; b++) {
        read_block(dir.direct[b], (uint8_t*)entries);
        for(int i = 0; i < 16; i++) {
            if(entries[i].inode && my_strcmp((char*)entries[i].name, name) == 0) {
                return (int)entries[i].size;
            }
        }
    }
    
    return -1;
}

int wnkfs_rename_file(const char* old_name, const char* new_name) {
    if(!wnkfs_mounted) return -1;
    
    wnkfs_inode_t dir;
    read_block(super.file_table + current_dir - 1, (uint8_t*)&dir);
    
    wnkfs_dirent_t entries[16];
    for(int b = 0; b < 12 && dir.direct[b]; b++) {
        read_block(dir.direct[b], (uint8_t*)entries);
        for(int i = 0; i < 16; i++) {
            if(entries[i].inode && my_strcmp((char*)entries[i].name, old_name) == 0) {
                my_strcpy((char*)entries[i].name, new_name);
                entries[i].name_len = my_strlen(new_name);
                write_block(dir.direct[b], (uint8_t*)entries);
                return 0;
            }
        }
    }
    
    return -1;
}

int wnkfs_copy_file(const char* src, const char* dst) {
    if(!wnkfs_mounted) return -1;
    
    wnkfs_inode_t dir;
    read_block(super.file_table + current_dir - 1, (uint8_t*)&dir);
    
    wnkfs_dirent_t entries[16];
    wnkfs_inode_t src_inode;
    uint32_t src_size = 0;
    int found = 0;
    
    for(int b = 0; b < 12 && dir.direct[b]; b++) {
        read_block(dir.direct[b], (uint8_t*)entries);
        for(int i = 0; i < 16; i++) {
            if(entries[i].inode && my_strcmp((char*)entries[i].name, src) == 0) {
                read_block(super.file_table + entries[i].inode - 1, (uint8_t*)&src_inode);
                src_size = (uint32_t)src_inode.size;
                found = 1;
                break;
            }
        }
        if(found) break;
    }
    
    if(!found) return -1;
    
    int result = wnkfs_create_file(dst);
    if(result != 0) return result;
    
    uint8_t buffer[512];
    uint32_t remaining = src_size;
    uint32_t offset = 0;
    
    while(remaining > 0) {
        uint32_t to_read = (remaining > 512) ? 512 : remaining;
        
        for(int b = 0; b < 12 && src_inode.direct[b] && offset < src_size; b++) {
            read_block(src_inode.direct[b], buffer);
            wnkfs_write_file(dst, buffer, to_read);
            offset += to_read;
            remaining -= to_read;
        }
    }
    
    return 0;
}

void wnkfs_test(void) {
    kprint("\n=== WNKFS TEST ===\n");
    
    if(wnkfs_mount() != 0) {
        kprint("Formatting...\n");
        wnkfs_format();
        wnkfs_mount();
    }
    
    wnkfs_info();
    wnkfs_list_dir();
    
    kprint("\nCreating test file...\n");
    const char* test_data = "Hello from WNKFS!";
    if(wnkfs_write_file("test.txt", (uint8_t*)test_data, my_strlen(test_data)) > 0) {
        kprint("File created\n");
    } else {
        kprint("Failed to create file\n");
    }
    
    wnkfs_list_dir();
    
    kprint("\nReading test file...\n");
    uint8_t buffer[512];
    int read = wnkfs_read_file("test.txt", buffer, 512);
    if(read > 0) {
        buffer[read] = '\0';
        kprint("Content: ");
        kprint((char*)buffer);
        kprint("\n");
    }
    
    kprint("\nTest complete\n");
}

void wnkfs_debug_dump(void) {
    kprint("\n=== WNKFS DEBUG DUMP ===\n");
    kprint("Mounted: "); kprint_int(wnkfs_mounted); kprint("\n");
    kprint("Current dir: "); kprint_int(current_dir); kprint("\n");
    kprint("Partition offset: "); kprint_int(partition_offset); kprint("\n");
    
    if(wnkfs_mounted) {
        kprint("Superblock magic: 0x");
        kprint_hex32(super.magic);
        kprint("\n");
        kprint("Total blocks: "); kprint_int((uint32_t)super.total_blocks); kprint("\n");
        kprint("Free blocks: "); kprint_int((uint32_t)super.free_blocks); kprint("\n");
        kprint("Root dir: "); kprint_int(super.root_dir); kprint("\n");
        kprint("Data start: "); kprint_int(super.data_start); kprint("\n");
    }
}


extern "C" void cmd_fdisk(void) {
    show_partitions();
}

extern "C" void cmd_mount_part(int index) {
    if(select_partition(index) == 0) {
        if(fs_type == 0) {
            wnkfs_mount();
        } else if(fs_type == 1) {
            kprint("[EXT2] Mounted (read-only test)\n");
        }
    }
}

extern "C" void cmd_lsext2(void) {
    if(fs_type != 1) {
        kprint("Not an EXT2 partition\n");
        return;
    }
    ext2_list_root();
}

extern "C" void cmd_testext2(void) {
    if(fs_type != 1) {
        if(select_partition(1) != 0) return;
    }
    if(ext2_read_superblock() == 0) {
        ext2_list_root();
    }
}


extern "C" void wnkfs_init(void) {
    kprint("\n[WNKFS] Initializing multi-filesystem support (ATA mode)...\n");
    
    ata_select_drive(0);
    ata_get_disk_size();
    read_mbr();
    if(mbr_valid) {
        show_partitions();
    } else {
        kprint("[WNKFS] No MBR found. Using raw disk mode.\n");
    }
    
    kprint("[WNKFS] Ready. Commands: fdisk, mount [1-4], lsext2, testext2\n");
}