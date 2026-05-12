#include "wnx.h"
#include "video.h"
#include "graph.h"
#include "ata.h"
#include "string_utils.h"
#include <stdint.h>

#define NULL 0

static wnx_context_t* current_ctx = NULL;

static uint32_t calc_checksum(uint8_t* data, uint32_t size) {
    uint32_t crc = 0xFFFFFFFF;
    for(uint32_t i = 0; i < size; i++) {
        crc ^= data[i];
        for(int j = 0; j < 8; j++) {
            if(crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else crc >>= 1;
        }
    }
    return ~crc;
}

int wnx_load(const char* filename, wnx_context_t* ctx) {
    if(!filename || !ctx) return -1;
    
    kprint_color("[WNX] Loading: ", TXT_CYAN);
    kprint(filename);
    kprint("\n");
    
    uint16_t dir_buf[256];
    read_sector(100, dir_buf);
    
    int slot = -1;
    uint16_t file_sector = 0;
    uint32_t file_size = 0;
    
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
        
        int match = 1;
        for(int j = 0; filename[j] && j < 11; j++) {
            if(name[j] != filename[j] && name[j] != (filename[j] - 32)) {
                match = 0;
                break;
            }
        }
        if(match && filename[0] != 0) {
            slot = i;
            file_sector = dir_buf[i*8 + 6];
            file_size = dir_buf[i*8 + 7];
            break;
        }
    }
    
    if(slot == -1) {
        kprint_color("[WNX] File not found\n", TXT_RED);
        return -1;
    }
    
    uint16_t hdr_buf[256];
    read_sector(file_sector, hdr_buf);
    
    uint8_t* hdr_bytes = (uint8_t*)&ctx->header;
    for(int i = 0; i < sizeof(wnx_header_t) && i < 512; i++) {
        if(i % 2 == 0) hdr_bytes[i] = hdr_buf[i/2] & 0xFF;
        else hdr_bytes[i] = (hdr_buf[i/2] >> 8) & 0xFF;
    }
    
    if(ctx->header.magic != WNX_MAGIC) {
        kprint_color("[WNX] Invalid WNX format\n", TXT_RED);
        return -1;
    }
    
    uint32_t total_size = sizeof(wnx_header_t) + ctx->header.code_size + ctx->header.data_size;
    uint8_t* all_data = (uint8_t*)hdr_bytes;
    uint32_t calc_sum = calc_checksum(all_data, total_size);
    
    if(calc_sum != ctx->header.checksum && ctx->header.checksum != 0) {
        kprint_color("[WNX] Checksum mismatch! File may be corrupted.\n", TXT_YELLOW);
    }
    
    kprint("[WNX] Name: ");
    kprint(ctx->header.name);
    kprint("\n");
    kprint("[WNX] Author: ");
    kprint(ctx->header.author);
    kprint("\n");
    kprint("[WNX] Type: ");
    if(ctx->header.flags & WNX_FLAG_GUI) kprint_color("GUI APP\n", TXT_GREEN);
    else kprint_color("CONSOLE\n", TXT_YELLOW);
    
    ctx->code = (uint8_t*)0x400000;
    ctx->data = (uint8_t*)0x500000;
    ctx->stack = (uint8_t*)0x600000;
    ctx->stack_ptr = ctx->header.stack_size;
    
    int sectors_per_block = (ctx->header.code_size + 511) / 512;
    for(int s = 0; s < sectors_per_block; s++) {
        uint16_t buf[256];
        read_sector(file_sector + 1 + s, buf);
        for(int i = 0; i < 256 && (s * 512 + i * 2) < ctx->header.code_size; i++) {
            ctx->code[s * 512 + i * 2] = buf[i] & 0xFF;
            if(s * 512 + i * 2 + 1 < ctx->header.code_size) {
                ctx->code[s * 512 + i * 2 + 1] = (buf[i] >> 8) & 0xFF;
            }
        }
    }
    
    int data_start = 1 + sectors_per_block;
    for(int s = 0; s < (ctx->header.data_size + 511) / 512; s++) {
        uint16_t buf[256];
        read_sector(file_sector + data_start + s, buf);
        for(int i = 0; i < 256 && (s * 512 + i * 2) < ctx->header.data_size; i++) {
            ctx->data[s * 512 + i * 2] = buf[i] & 0xFF;
            if(s * 512 + i * 2 + 1 < ctx->header.data_size) {
                ctx->data[s * 512 + i * 2 + 1] = (buf[i] >> 8) & 0xFF;
            }
        }
    }
    
    ctx->entry = (uint32_t)ctx->code + ctx->header.entry_point;
    ctx->running = 0;
    
    kprint_color("[WNX] Loaded successfully!\n", TXT_GREEN);
    return 0;
}

int wnx_execute(wnx_context_t* ctx) {
    if(!ctx) return -1;
    
    ctx->running = 1;
    current_ctx = ctx;
    
    kprint_color("[WNX] Executing: ", TXT_CYAN);
    kprint(ctx->header.name);
    kprint("\n");
    
    void (*entry)(char* args) = (void(*)(char*))ctx->entry;
    
    if(ctx->header.flags & WNX_FLAG_GUI) {
        kprint_color("[WNX] GUI mode\n", TXT_GREEN);
        entry(ctx->args);
    } else {
        kprint_color("[WNX] Console mode\n", TXT_YELLOW);
        entry(ctx->args);
    }
    
    ctx->running = 0;
    current_ctx = NULL;
    
    kprint_color("[WNX] Finished\n", TXT_GREEN);
    return 0;
}

void wnx_stop(wnx_context_t* ctx) {
    if(ctx) ctx->running = 0;
    current_ctx = NULL;
}

int wnx_list_files(void) {
    uint16_t dir_buf[256];
    read_sector(100, dir_buf);
    
    kprint_color("\n=== WNX Programs ===\n", TXT_CYAN);
    int count = 0;
    
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
        
        if(name[0] != 0 && name[0] != 0xE5) {
            uint16_t hdr_buf[256];
            read_sector(dir_buf[i*8 + 6], hdr_buf);
            
            uint32_t magic = hdr_buf[0] | (hdr_buf[1] << 16);
            if(magic == WNX_MAGIC) {
                count++;
                kprint("  ");
                kprint_int(count);
                kprint(". ");
                kprint(name);
                
                uint8_t prog_name[33];
                for(int j = 0; j < 16 && j < 32; j++) {
                    if(j % 2 == 0) prog_name[j] = hdr_buf[16 + j/2] & 0xFF;
                    else prog_name[j] = (hdr_buf[16 + j/2] >> 8) & 0xFF;
                }
                prog_name[32] = 0;
                
                kprint(" - ");
                kprint((char*)prog_name);
                kprint("\n");
            }
        }
    }
    
    if(count == 0) {
        kprint("  No WNX files found\n");
    }
    
    kprint("==================\n");
    return count;
}