#include "wnx.h"
#include "video.h"
#include "ata.h"
#include "graph.h"
#include "string_utils.h"
#include <stdint.h>


static void str_cpy(char* d, const char* s) { while(*s){*d=*s;d++;s++;} *d=0; }
static int str_len(const char* s) { int l=0; while(*s++)l++; return l; }
static int str_cmp(const char* a, const char* b) { while(*a&&*b&&*a==*b){a++;b++;} return *a-*b; }

int wnx_create_from_code(const char* code, int code_size, const char* output_name) {
    kprint_color("[WNX] Creating WNX file: ", TXT_CYAN);
    kprint(output_name);
    kprint("\n");
    
    wnx_header_t header;
    header.magic = WNX_MAGIC;
    header.version = 1;
    header.flags = WNX_FLAG_GUI;
    header.entry_point = 0; 
    header.code_size = code_size;
    header.data_size = 0;
    header.stack_size = 65536;
    header.heap_size = 65536;
    str_cpy(header.name, output_name);
    str_cpy(header.author, "WNKA User");
    str_cpy(header.description, "Created from source");
    header.icon = 0;
    header.checksum = 0;
    
    uint8_t* code_buf = (uint8_t*)0x400000;
    for(int i = 0; i < code_size; i++) {
        code_buf[i] = code[i];
    }
    
    uint8_t* wnx_data = (uint8_t*)0x300000;
    for(int i = 0; i < sizeof(wnx_header_t); i++) {
        wnx_data[i] = ((uint8_t*)&header)[i];
    }
    for(int i = 0; i < code_size; i++) {
        wnx_data[sizeof(wnx_header_t) + i] = code_buf[i];
    }
    
    int total_size = sizeof(wnx_header_t) + code_size;
    int sectors = (total_size + 511) / 512;
    
    uint16_t dir_buf[256];
    read_sector(100, dir_buf);
    
    int slot = -1;
    for(int i = 0; i < 32; i++) {
        if(((char*)dir_buf)[i*16] == 0) {
            slot = i;
            break;
        }
    }
    
    if(slot == -1) {
        kprint_color("[WNX] Directory full!\n", TXT_RED);
        return -1;
    }
    
    static int file_counter = 2000;
    int wnx_sector = file_counter++;
    
    for(int s = 0; s < sectors; s++) {
        uint16_t sector_buf[256] = {0};
        for(int i = 0; i < 256 && (s * 512 + i * 2) < total_size; i++) {
            sector_buf[i] = wnx_data[s * 512 + i * 2] | (wnx_data[s * 512 + i * 2 + 1] << 8);
        }
        write_sector(wnx_sector + s, sector_buf);
    }
    
    for(int j = 0; j < 11 && output_name[j]; j++) {
        ((char*)dir_buf)[slot*16 + j] = output_name[j];
    }
    ((char*)dir_buf)[slot*16 + 11] = 0;
    dir_buf[slot*8 + 6] = wnx_sector;
    dir_buf[slot*8 + 7] = total_size;
    write_sector(100, dir_buf);
    
    kprint_color("[WNX] Created: ", TXT_GREEN);
    kprint(output_name);
    kprint(" (");
    kprint_int(total_size);
    kprint(" bytes)\n");
    
    return 0;
}
int wnx_create_from_source(const char* source_path, const char* output_name) {
    kprint_color("[WNX] Creating from source: ", TXT_CYAN);
    kprint(source_path);
    kprint("\n");
    
    uint16_t dir_buf[256];
    read_sector(100, dir_buf);
    
    int slot = -1;
    int file_sector = 0;
    int file_size = 0;
    
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
        if(my_strcmp(source_path, name) == 0) {
            slot = i;
            file_sector = dir_buf[i*8 + 6];
            file_size = dir_buf[i*8 + 7];
            break;
        }
    }
    
    if(slot == -1) {
        kprint_color("[WNX] Source file not found\n", TXT_RED);
        return -1;
    }
    
    uint16_t src_buf[256];
    read_sector(file_sector, src_buf);
    
    char source_code[4096];
    int src_len = 0;
    for(int i = 0; i < file_size && src_len < 4095; i++) {
        if(i % 2 == 0) source_code[src_len] = src_buf[i/2] & 0xFF;
        else source_code[src_len] = (src_buf[i/2] >> 8) & 0xFF;
        if(source_code[src_len] != 0) src_len++;
    }
    source_code[src_len] = '\0';
    
    return wnx_create_from_code(source_code, src_len, output_name);
}