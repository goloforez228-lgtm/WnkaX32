#include "cdrom.h"
#include "video.h"
#include "kernel_stubs.h"
#include "string_utils.h"
#include "ata.h"

static uint16_t atapi_port = 0;
static int atapi_device = 0;
static int atapi_present = 0;
static uint32_t iso_root_lba = 0;

static fat_boot_sector_t fat_boot;
static uint32_t fat_root_dir_lba = 0;
static uint32_t fat_data_lba = 0;
static uint32_t fat_root_dir_sectors = 0;
static uint16_t fat_bytes_per_sector = 512;
static uint8_t fat_sectors_per_cluster = 1;
static uint32_t fat_fat_start_lba = 0;
static uint16_t fat_fat_size = 0;
static int fat_mounted = 0;


static void ata_wait_bsy_cd(void) {
    int timeout = 1000000;
    while(timeout-- > 0) {
        uint8_t status = inb(atapi_port + 7);
        if((status & 0x80) == 0) return;
    }
}

static int atapi_wait_drq(void) {
    int timeout = 1000000;
    while(timeout-- > 0) {
        uint8_t status = inb(atapi_port + 7);
        if(status & 0x08) return 1;
        if(status & 0x01) return -1;
    }
    return 0;
}

static int atapi_packet_cmd(uint8_t* cmd, int cmd_len, uint16_t* buffer, uint32_t buffer_words) {
    outb(atapi_port + 6, atapi_device ? 0xB0 : 0xA0);
    ata_wait_bsy_cd();
    
    inb(atapi_port + 7);
    
    outb(atapi_port + 1, 0);
    outb(atapi_port + 2, 0);
    outb(atapi_port + 3, 0);
    outb(atapi_port + 4, 0);
    outb(atapi_port + 5, 0);
    outb(atapi_port + 7, 0xA0);
    
    if(atapi_wait_drq() <= 0) return -1;
    
    for(int i = 0; i < cmd_len; i += 2) {
        uint16_t word = (i+1 < cmd_len) ? (cmd[i] | (cmd[i+1] << 8)) : (cmd[i] | (0 << 8));
        outw(atapi_port, word);
    }
    
    uint32_t words_read = 0;
    while(words_read < buffer_words) {
        uint8_t status = inb(atapi_port + 7);
        if(status & 0x01) return -1;
        
        if(status & 0x08) {
            uint32_t to_read = buffer_words - words_read;
            if(to_read > 256) to_read = 256;
            for(uint32_t i = 0; i < to_read; i++) {
                buffer[words_read + i] = inw(atapi_port);
            }
            words_read += to_read;
        }
        
        if(status & 0x10) break;
    }
    
    return words_read * 2;
}

int atapi_read_sector(uint32_t lba, uint8_t* buffer) {
    if(!atapi_present) return -1;
    
    uint8_t cmd[12] = {
        0x28,          
        0x00,
        (uint8_t)(lba >> 24),
        (uint8_t)(lba >> 16),
        (uint8_t)(lba >> 8),
        (uint8_t)(lba),
        0x00, 0x00, 0x01,
        0x00, 0x00, 0x00
    };
    
    uint16_t temp_buf[1024];
    int result = atapi_packet_cmd(cmd, 12, temp_buf, 1024);
    
    if(result > 0) {
        for(int i = 0; i < ATAPI_SECTOR_SIZE / 2; i++) {
            buffer[i*2] = temp_buf[i] & 0xFF;
            buffer[i*2+1] = (temp_buf[i] >> 8) & 0xFF;
        }
        return ATAPI_SECTOR_SIZE;
    }
    
    return -1;
}


static int fat_read_sector(uint32_t lba, uint8_t* buffer) {
    return atapi_read_sector(lba, buffer);
}

void fat_mount(void) {
    uint8_t sector[ATAPI_SECTOR_SIZE];
    
    kprint("[FAT] Searching for FAT12/16 filesystem...\n");
    
    uint32_t try_lba[] = {0, 16, 32, 64, 80, 96, 128};
    
    for(int i = 0; i < 7; i++) {
        uint32_t lba = try_lba[i];
        if(atapi_read_sector(lba, sector) <= 0) continue;
        
        if(sector[510] == 0x55 && sector[511] == 0xAA) {
            if((sector[0] == 0xEB && sector[2] == 0x90) || sector[0] == 0xE9) {
                
                fat_boot = *(fat_boot_sector_t*)sector;
                fat_bytes_per_sector = fat_boot.bytes_per_sector;
                fat_sectors_per_cluster = fat_boot.sectors_per_cluster;
                uint16_t reserved = fat_boot.reserved_sectors;
                uint8_t fat_count = fat_boot.fat_count;
                uint16_t root_entries = fat_boot.root_entries;
                fat_fat_size = fat_boot.fat_size_16;
                
                if(fat_fat_size == 0) {
                    kprint("[FAT] FAT32 not supported yet\n");
                    continue;
                }
                
                fat_root_dir_sectors = (root_entries * 32 + fat_bytes_per_sector - 1) / fat_bytes_per_sector;
                fat_fat_start_lba = reserved;
                fat_root_dir_lba = reserved + fat_count * fat_fat_size;
                fat_data_lba = fat_root_dir_lba + fat_root_dir_sectors;
                
                kprint("[FAT] Found FAT12/16 at LBA ");
                kprint_int(lba);
                kprint("\n");
                kprint("[FAT] Bytes per sector: ");
                kprint_int(fat_bytes_per_sector);
                kprint("\n");
                kprint("[FAT] Sectors per cluster: ");
                kprint_int(fat_sectors_per_cluster);
                kprint("\n");
                kprint("[FAT] Root directory LBA: ");
                kprint_int(fat_root_dir_lba);
                kprint("\n");
                kprint("[FAT] Data area LBA: ");
                kprint_int(fat_data_lba);
                kprint("\n");
                
                fat_mounted = 1;
                return;
            }
        }
    }
    
    kprint("[FAT] No FAT filesystem found\n");
}

void fat_list_root(void) {
    if(!fat_mounted) {
        kprint("[FAT] FAT not mounted\n");
        return;
    }
    
    uint8_t sector[512];
    if(fat_read_sector(fat_root_dir_lba, sector) <= 0) {
        kprint("[FAT] Cannot read root directory\n");
        return;
    }
    
    kprint("\n[FAT] Root directory contents:\n");
    
    for(int i = 0; i < fat_boot.root_entries; i++) {
        fat_dir_entry_t* entry = (fat_dir_entry_t*)(sector + i * 32);
        
        if(entry->name[0] == 0x00) break;
        if(entry->name[0] == 0xE5) continue;
        
        char filename[13];
        int pos = 0;
        
        for(int j = 0; j < 8 && entry->name[j] != ' '; j++) {
            filename[pos++] = entry->name[j];
        }
        if(entry->ext[0] != ' ') {
            filename[pos++] = '.';
            for(int j = 0; j < 3 && entry->ext[j] != ' '; j++) {
                filename[pos++] = entry->ext[j];
            }
        }
        filename[pos] = '\0';
        
        uint16_t first_cluster = entry->first_cluster_low;
        uint32_t file_size = entry->file_size;
        
        if(entry->attributes & 0x10) {
            kprint("  [DIR]  ");
        } else {
            kprint("  [FILE] ");
        }
        kprint(filename);
        
        if(!(entry->attributes & 0x10) && file_size > 0) {
            kprint(" (");
            if(file_size > 1024*1024) {
                kprint_int(file_size / (1024*1024));
                kprint(" MB");
            } else if(file_size > 1024) {
                kprint_int(file_size / 1024);
                kprint(" KB");
            } else {
                kprint_int(file_size);
                kprint(" B");
            }
            kprint(", cluster ");
            kprint_int(first_cluster);
            kprint(")");
        }
        kprint("\n");
    }
}

static uint16_t fat_get_next_cluster(uint16_t cluster) {
    uint8_t fat_sector[512];
    uint32_t fat_offset = cluster * 3 / 2;
    uint32_t fat_sector_lba = fat_fat_start_lba + (fat_offset / fat_bytes_per_sector);
    uint32_t fat_offset_in_sector = fat_offset % fat_bytes_per_sector;
    
    if(fat_read_sector(fat_sector_lba, fat_sector) <= 0) return 0xFFF;
    
    uint16_t next;
    if(cluster & 1) {
        next = (*(uint16_t*)(fat_sector + fat_offset_in_sector) >> 4) & 0x0FFF;
    } else {
        next = *(uint16_t*)(fat_sector + fat_offset_in_sector) & 0x0FFF;
    }
    
    return next;
}

int fat_read_file(const char* filename, uint8_t* buffer, uint32_t max_size) {
    if(!fat_mounted) {
        kprint("[FAT] FAT not mounted\n");
        return -1;
    }
    
    uint8_t sector[512];
    if(fat_read_sector(fat_root_dir_lba, sector) <= 0) {
        kprint("[FAT] Cannot read root directory\n");
        return -1;
    }
    
    uint16_t first_cluster = 0;
    uint32_t file_size = 0;
    int found = 0;
    
    for(int i = 0; i < fat_boot.root_entries; i++) {
        fat_dir_entry_t* entry = (fat_dir_entry_t*)(sector + i * 32);
        
        if(entry->name[0] == 0x00) break;
        if(entry->name[0] == 0xE5) continue;
        if(entry->attributes & 0x10) continue;
        
        char entry_name[13];
        int pos = 0;
        for(int j = 0; j < 8 && entry->name[j] != ' '; j++) {
            entry_name[pos++] = entry->name[j];
        }
        if(entry->ext[0] != ' ') {
            entry_name[pos++] = '.';
            for(int j = 0; j < 3 && entry->ext[j] != ' '; j++) {
                entry_name[pos++] = entry->ext[j];
            }
        }
        entry_name[pos] = '\0';
        
        int match = 1;
        for(int j = 0; j < pos && filename[j]; j++) {
            char a = entry_name[j];
            char b = filename[j];
            if(a >= 'A' && a <= 'Z') a += 32;
            if(b >= 'A' && b <= 'Z') b += 32;
            if(a != b) { match = 0; break; }
        }
        
        if(match && filename[pos] == '\0') {
            first_cluster = entry->first_cluster_low;
            file_size = entry->file_size;
            found = 1;
            break;
        }
    }
    
    if(!found) {
        kprint("[FAT] File not found: ");
        kprint(filename);
        kprint("\n");
        return -1;
    }
    
    kprint("[FAT] Reading file: ");
    kprint(filename);
    kprint(" (");
    kprint_int(file_size);
    kprint(" bytes)\n");
    
    uint32_t bytes_read = 0;
    uint16_t cluster = first_cluster;
    
    while(bytes_read < file_size && bytes_read < max_size) {
        uint32_t cluster_lba = fat_data_lba + (cluster - 2) * fat_sectors_per_cluster;
        uint32_t to_read = file_size - bytes_read;
        uint32_t cluster_bytes = fat_sectors_per_cluster * fat_bytes_per_sector;
        if(to_read > cluster_bytes) to_read = cluster_bytes;
        if(to_read > max_size - bytes_read) to_read = max_size - bytes_read;
        
        for(uint32_t s = 0; s < fat_sectors_per_cluster; s++) {
            if(fat_read_sector(cluster_lba + s, buffer + bytes_read + s * fat_bytes_per_sector) <= 0) {
                kprint("[FAT] Read error at cluster ");
                kprint_int(cluster);
                kprint("\n");
                return -1;
            }
        }
        
        bytes_read += to_read;
        cluster = fat_get_next_cluster(cluster);
        if(cluster >= 0xFF8) break;
    }
    
    kprint("[FAT] Read ");
    kprint_int(bytes_read);
    kprint(" bytes\n");
    
    return bytes_read;
}


void read_cdrom_root(uint32_t root_lba) {
    uint8_t sector[ATAPI_SECTOR_SIZE];
    if(atapi_read_sector(root_lba, sector) <= 0) {
        kprint("[ISO] Cannot read root directory\n");
        return;
    }
    
    kprint("\n[ISO] Root directory contents:\n");
    
    uint32_t offset = 0;
    while(offset < ATAPI_SECTOR_SIZE) {
        uint8_t rec_len = sector[offset];
        if(rec_len == 0) {
            offset += 4;
            continue;
        }
        
        uint8_t name_len = sector[offset + 32];
        if(name_len > 0 && name_len < 255) {
            char filename[256];
            int fn_len = 0;
            for(int i = 0; i < name_len && i < 255; i++) {
                char c = sector[offset + 33 + i];
                if(c == ';') break;
                if(c >= 32 && c <= 126) filename[fn_len++] = c;
            }
            filename[fn_len] = '\0';
            
            if(fn_len == 0 || (fn_len == 1 && filename[0] == '.')) {
                offset += rec_len;
                continue;
            }
            
            uint8_t flags = sector[offset + 25];
            uint32_t file_lba = *(uint32_t*)(sector + offset + 2);
            uint32_t file_size = *(uint32_t*)(sector + offset + 10);
            
            if(flags & 0x02) {
                kprint("  [DIR]  ");
            } else {
                kprint("  [FILE] ");
            }
            kprint(filename);
            
            if(!(flags & 0x02) && file_size > 0) {
                kprint(" (");
                kprint_int(file_size);
                kprint(" bytes)");
            }
            kprint("\n");
        }
        
        offset += rec_len;
    }
}

int atapi_mount_iso(void) {
    if(!atapi_present) return -1;
    
    kprint("[ISO] Searching for ISO 9660 filesystem...\n");
    
    uint8_t sector[ATAPI_SECTOR_SIZE];
    
    for(uint32_t lba = 16; lba < 50; lba++) {
        if(atapi_read_sector(lba, sector) <= 0) continue;
        
        for(int offset = 0; offset < ATAPI_SECTOR_SIZE - 5; offset++) {
            if(sector[offset] == 'C' && sector[offset+1] == 'D' &&
               sector[offset+2] == '0' && sector[offset+3] == '0' &&
               sector[offset+4] == '1') {
                
                uint8_t type = sector[offset - 1];
                if(type == 1) {
                    iso_pvd_t* pvd = (iso_pvd_t*)(sector + offset - 1);
                    iso_root_lba = pvd->root_dir_lba;
                    kprint("[ISO] Found ISO 9660 at LBA ");
                    kprint_int(lba);
                    kprint("\n");
                    kprint("[ISO] Root directory LBA: ");
                    kprint_int(iso_root_lba);
                    kprint("\n");
                    read_cdrom_root(iso_root_lba);
                    return 0;
                }
            }
        }
    }
    
    kprint("[ISO] No ISO 9660 found\n");
    return -1;
}


int atapi_init(void) {
    uint16_t ports[] = {0x1F0, 0x170, 0x1E8, 0x168};
    const char* port_names[] = {"Primary", "Secondary", "Third", "Fourth"};
    
    kprint("[ATAPI] Scanning for CD-ROM...\n");
    
    for(int p = 0; p < 4; p++) {
        atapi_port = ports[p];
        
        for(int device = 0; device < 2; device++) {
            outb(atapi_port + 6, device ? 0xB0 : 0xA0);
            ata_wait_bsy_cd();
            
            outb(atapi_port + 7, 0xA1);
            
            for(volatile int i = 0; i < 100000; i++);
            
            uint8_t status = inb(atapi_port + 7);
            uint8_t lob = inb(atapi_port + 4);
            
            if((status & 0x01) == 0 && status != 0xFF && status != 0x00) {
                uint16_t identify[256];
                for(int i = 0; i < 256; i++) {
                    identify[i] = inw(atapi_port);
                }
                
                if(identify[0] & 0x8000) {
                    char model[41] = {0};
                    for(int i = 0; i < 20; i++) {
                        uint16_t w = identify[27 + i];
                        model[i*2] = (char)(w >> 8);
                        model[i*2+1] = (char)(w & 0xFF);
                    }
                    
                    for(int i = 39; i >= 0; i--) {
                        if(model[i] != ' ') break;
                        model[i] = '\0';
                    }
                    
                    kprint("[ATAPI] Found CD-ROM on ");
                    kprint(port_names[p]);
                    kprint(" (device ");
                    kprint_int(device);
                    kprint("): ");
                    kprint(model);
                    kprint("\n");
                    
                    atapi_device = device;
                    atapi_present = 1;
                    return 1;
                }
            }
        }
    }
    
    kprint("[ATAPI] No CD-ROM found\n");
    return 0;
}

void cdrom_list_root(void) {
    if(iso_root_lba != 0) {
        read_cdrom_root(iso_root_lba);
    } else if(fat_mounted) {
        fat_list_root();
    } else {
        kprint("[CDROM] No filesystem mounted. Run 'cdmount' first.\n");
    }
}

int cdrom_copy_file(const char* filename, const char* dest_path) {
    (void)dest_path;
    
    if(iso_root_lba != 0) {
        kprint("[CDROM] Copy from ISO not implemented yet\n");
        kprint("[CDROM] Use 'fatread' for FAT files\n");
        return -1;
    }
    
    if(fat_mounted) {
        uint8_t buffer[65536];
        int size = fat_read_file(filename, buffer, sizeof(buffer));
        if(size > 0) {
            kprint("[CDROM] File read successfully, ");
            kprint_int(size);
            kprint(" bytes\n");
            return 0;
        }
        return -1;
    }
    
    kprint("[CDROM] No filesystem mounted\n");
    return -1;
}