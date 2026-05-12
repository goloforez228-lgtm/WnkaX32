#include "floppy.h"
#include "fdc.h"
#include "video.h"
#include "kernel_stubs.h"
#include "graph.h"
#include "string_utils.h"

floppy_t floppy;

void floppy_init(void) {
    kprint("[FLOPPY] Initializing...\n");
    
    fdc_read_sector(0, 0, 0, 1, floppy.boot_sector);
    
    if(floppy.boot_sector[510] == 0x55 && floppy.boot_sector[511] == 0xAA) {
        kprint("[FLOPPY] FAT12 filesystem detected\n");
        floppy.mounted = 1;
        
        for(int i = 0; i < 9; i++) {
            int sector = 1 + i;
            int track = sector / 18;
            int head = (sector / 18) & 1;
            int sec = (sector % 18) + 1;
            fdc_read_sector(0, head, track, sec, &floppy.fat[i * 512]);
        }
        
        for(int i = 0; i < 14; i++) {
            int sector = 19 + i;
            int track = sector / 18;
            int head = (sector / 18) & 1;
            int sec = (sector % 18) + 1;
            fdc_read_sector(0, head, track, sec, &floppy.root_dir[i * 512]);
        }
        
        kprint("[FLOPPY] Ready\n");
    } else {
        kprint("[FLOPPY] Not a FAT12 floppy\n");
        floppy.mounted = 0;
    }
}

void floppy_save(void) {
    if(!floppy.mounted) return;
    
    for(int i = 0; i < 9; i++) {
        int sector = 1 + i;
        int track = sector / 18;
        int head = (sector / 18) & 1;
        int sec = (sector % 18) + 1;
        fdc_write_sector(0, head, track, sec, &floppy.fat[i * 512]);
    }
    
    for(int i = 0; i < 14; i++) {
        int sector = 19 + i;
        int track = sector / 18;
        int head = (sector / 18) & 1;
        int sec = (sector % 18) + 1;
        fdc_write_sector(0, head, track, sec, &floppy.root_dir[i * 512]);
    }
    
    kprint("[FLOPPY] Changes saved\n");
}

int floppy_find_free_slot(void) {
    for(int i = 0; i < 224; i++) {
        uint8_t* entry = &floppy.root_dir[i * 32];
        if(entry[0] == 0x00 || entry[0] == 0xE5) {
            return i;
        }
    }
    return -1;
}

int floppy_find_file(const char* filename) {
    for(int i = 0; i < 224; i++) {
        uint8_t* entry = &floppy.root_dir[i * 32];
        if(entry[0] == 0x00) break;
        if(entry[0] == 0xE5) continue;
        
        char name[12];
        for(int j = 0; j < 8; j++) {
            name[j] = (entry[j] != ' ') ? entry[j] : '\0';
        }
        if(entry[8] != ' ') {
            int len = 0;
            while(name[len]) len++;
            name[len] = '.';
            for(int j = 0; j < 3; j++) {
                name[len + 1 + j] = (entry[8 + j] != ' ') ? entry[8 + j] : '\0';
            }
            name[len + 4] = '\0';
        } else {
            name[8] = '\0';
        }
        
        if(my_strcmp(filename, name) == 0) {
            return i;
        }
    }
    return -1;
}

int floppy_create_file(const char* filename) {
    if(!floppy.mounted) return -1;
    if(fdc_is_write_protected()) return -2;
    
    if(floppy_find_file(filename) >= 0) {
        kprint("File already exists\n");
        return -3;
    }
    
    int slot = floppy_find_free_slot();
    if(slot == -1) {
        kprint("Directory full\n");
        return -4;
    }
    
    char name_part[9] = {0};
    char ext_part[4] = {0};
    int dot = -1;
    
    for(int i = 0; filename[i]; i++) {
        if(filename[i] == '.') dot = i;
    }
    
    if(dot == -1) {
        for(int i = 0; i < 8 && filename[i]; i++) name_part[i] = filename[i];
    } else {
        for(int i = 0; i < dot && i < 8; i++) name_part[i] = filename[i];
        for(int i = dot + 1; filename[i] && (i - dot - 1) < 3; i++) ext_part[i - dot - 1] = filename[i];
    }
    
    uint8_t* entry = &floppy.root_dir[slot * 32];
    for(int i = 0; i < 32; i++) entry[i] = 0;
    
    for(int i = 0; i < 8; i++) {
        entry[i] = (name_part[i] != '\0') ? name_part[i] : ' ';
    }
    
    for(int i = 0; i < 3; i++) {
        entry[8 + i] = (ext_part[i] != '\0') ? ext_part[i] : ' ';
    }
    
    entry[11] = 0x20;
    entry[26] = 0;
    entry[27] = 0;
    entry[28] = 0;
    entry[29] = 0;
    entry[30] = 0;
    entry[31] = 0;
    
    floppy_save();
    kprint("Created: ");
    kprint(filename);
    kprint("\n");
    return 0;
}

int floppy_write_file(const char* filename, const char* data) {
    if(!floppy.mounted) return -1;
    if(fdc_is_write_protected()) return -2;
    
    int slot = floppy_find_file(filename);
    if(slot == -1) {
        kprint("File not found\n");
        return -3;
    }
    
    uint8_t* entry = &floppy.root_dir[slot * 32];
    uint32_t size = 0;
    while(data[size]) size++;
    
    if(size == 0) {
        kprint("No data to write\n");
        return -4;
    }
    
    int clusters_needed = (size + 511) / 512;
    int first_cluster = -1;
    int prev_cluster = -1;
    
    for(int c = 2; c < 2880 && clusters_needed > 0; c++) {
        uint16_t fat_entry;
        if(c % 2 == 0) {
            fat_entry = floppy.fat[(c * 3) / 2] & 0x0FFF;
        } else {
            fat_entry = (floppy.fat[(c * 3) / 2] >> 4) & 0x0FFF;
        }
        
        if(fat_entry == 0) {
            if(first_cluster == -1) first_cluster = c;
            if(prev_cluster != -1) {
                if(prev_cluster % 2 == 0) {
                    floppy.fat[(prev_cluster * 3) / 2] = (floppy.fat[(prev_cluster * 3) / 2] & 0xF000) | c;
                } else {
                    floppy.fat[(prev_cluster * 3) / 2] = (floppy.fat[(prev_cluster * 3) / 2] & 0x000F) | (c << 4);
                }
            }
            prev_cluster = c;
            clusters_needed--;
            
            if(clusters_needed == 0) {
                if(c % 2 == 0) {
                    floppy.fat[(c * 3) / 2] = (floppy.fat[(c * 3) / 2] & 0xF000) | 0xFFF;
                } else {
                    floppy.fat[(c * 3) / 2] = (floppy.fat[(c * 3) / 2] & 0x000F) | (0xFFF << 4);
                }
            }
        }
    }
    
    if(first_cluster == -1) {
        kprint("No free clusters\n");
        return -5;
    }
    
    entry[26] = first_cluster & 0xFF;
    entry[27] = (first_cluster >> 8) & 0xFF;
    entry[28] = size & 0xFF;
    entry[29] = (size >> 8) & 0xFF;
    entry[30] = (size >> 16) & 0xFF;
    entry[31] = (size >> 24) & 0xFF;
    
    int cluster = first_cluster;
    uint32_t offset = 0;
    
    while(cluster < 0xFF8 && offset < size) {
        int start_sector = 33 + (cluster - 2);
        
        for(int s = 0; s < 1 && offset < size; s++) {
            int sector = start_sector + s;
            int track = sector / 18;
            int head = (sector / 18) & 1;
            int sec = (sector % 18) + 1;
            
            uint8_t data_buf[512] = {0};
            for(int i = 0; i < 512 && offset + i < size; i++) {
                data_buf[i] = data[offset + i];
            }
            
            fdc_write_sector(0, head, track, sec, data_buf);
            offset += 512;
        }
        
        if(cluster % 2 == 0) {
            cluster = floppy.fat[(cluster * 3) / 2] & 0x0FFF;
        } else {
            cluster = (floppy.fat[(cluster * 3) / 2] >> 4) & 0x0FFF;
        }
    }
    
    floppy_save();
    kprint("Written ");
    kprint_int(size);
    kprint(" bytes to ");
    kprint(filename);
    kprint("\n");
    return size;
}

int floppy_read_file(const char* filename, char* buffer, int max_size) {
    if(!floppy.mounted) return -1;
    
    int slot = floppy_find_file(filename);
    if(slot == -1) {
        return -2;
    }
    
    uint8_t* entry = &floppy.root_dir[slot * 32];
    uint32_t size = entry[28] | (entry[29] << 8) | (entry[30] << 16) | (entry[31] << 24);
    uint32_t first_cluster = entry[26] | (entry[27] << 8);
    
    if(size == 0) return 0;
    if(size > (uint32_t)max_size) size = max_size;
    
    int cluster = first_cluster;
    uint32_t offset = 0;
    
    while(cluster < 0xFF8 && offset < size) {
        int start_sector = 33 + (cluster - 2);
        
        for(int s = 0; s < 1 && offset < size; s++) {
            int sector = start_sector + s;
            int track = sector / 18;
            int head = (sector / 18) & 1;
            int sec = (sector % 18) + 1;
            
            uint8_t data_buf[512];
            fdc_read_sector(0, head, track, sec, data_buf);
            
            for(int i = 0; i < 512 && offset + i < size; i++) {
                buffer[offset + i] = data_buf[i];
            }
            offset += 512;
        }
        
        if(cluster % 2 == 0) {
            cluster = floppy.fat[(cluster * 3) / 2] & 0x0FFF;
        } else {
            cluster = (floppy.fat[(cluster * 3) / 2] >> 4) & 0x0FFF;
        }
    }
    
    buffer[offset] = '\0';
    return offset;
}

int floppy_delete_file(const char* filename) {
    if(!floppy.mounted) return -1;
    if(fdc_is_write_protected()) return -2;
    
    int slot = floppy_find_file(filename);
    if(slot == -1) {
        kprint("File not found\n");
        return -3;
    }
    
    uint8_t* entry = &floppy.root_dir[slot * 32];
    uint32_t first_cluster = entry[26] | (entry[27] << 8);
    
    int cluster = first_cluster;
    while(cluster < 0xFF8 && cluster >= 2) {
        int next_cluster;
        if(cluster % 2 == 0) {
            next_cluster = floppy.fat[(cluster * 3) / 2] & 0x0FFF;
            floppy.fat[(cluster * 3) / 2] = 0;
        } else {
            next_cluster = (floppy.fat[(cluster * 3) / 2] >> 4) & 0x0FFF;
            floppy.fat[(cluster * 3) / 2] = 0;
        }
        cluster = next_cluster;
    }
    
    entry[0] = 0xE5;
    floppy_save();
    kprint("Deleted: ");
    kprint(filename);
    kprint("\n");
    return 0;
}

void floppy_list_files(void) {
    kprint("\n=== FLOPPY DIRECTORY ===\n");
    
    int count = 0;
    for(int i = 0; i < 224; i++) {
        uint8_t* entry = &floppy.root_dir[i * 32];
        if(entry[0] == 0x00) break;
        if(entry[0] == 0xE5) continue;
        
        char name[12];
        for(int j = 0; j < 8; j++) {
            name[j] = (entry[j] != ' ') ? entry[j] : '\0';
        }
        if(entry[8] != ' ') {
            int len = 0;
            while(name[len]) len++;
            name[len] = '.';
            for(int j = 0; j < 3; j++) {
                name[len + 1 + j] = (entry[8 + j] != ' ') ? entry[8 + j] : '\0';
            }
            name[len + 4] = '\0';
        } else {
            name[8] = '\0';
        }
        
        uint32_t size = entry[28] | (entry[29] << 8) | (entry[30] << 16) | (entry[31] << 24);
        
        kprint("  ");
        kprint(name);
        for(int s = 0; s < 12 - my_strlen(name); s++) kprint(" ");
        kprint(" (");
        kprint_int(size);
        kprint(" bytes)\n");
        count++;
    }
    
    if(count == 0) {
        kprint("  (empty)\n");
    }
    kprint("========================\n");
}