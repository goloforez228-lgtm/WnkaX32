#include "video.h"
#include "wnkfs.h"
#include "string_utils.h"
#define NULL 0

static int abs(int x) {
    return x < 0 ? -x : x;
}


static uint8_t dos_memory[1024 * 1024];
static int dos_running = 0;

typedef struct {
    uint16_t signature;   
    uint16_t last_page_bytes;
    uint16_t pages;
    uint16_t relocation_items;
    uint16_t header_paragraphs;
    uint16_t min_paragraphs;
    uint16_t max_paragraphs;
    uint16_t initial_ss;
    uint16_t initial_sp;
    uint16_t checksum;
    uint16_t initial_ip;
    uint16_t initial_cs;
    uint16_t relocation_table;
    uint16_t overlay_number;
} __attribute__((packed)) MZHeader;

int dos_load_com(const char* filename) {
    kprint("[DOS] Loading .COM: ");
    kprint(filename);
    kprint("\n");
    
    uint8_t buffer[65536];
    int size = wnkfs_read_file(filename, buffer, 65536);
    if(size <= 0) {
        kprint("[DOS] File not found\n");
        return -1;
    }
    
    if(size > 0xFF00) {
        kprint("[DOS] .COM too large\n");
        return -1;
    }
    
    for(int i = 0; i < 0x10000; i++) {
        dos_memory[i] = 0;
    }
    
    for(int i = 0; i < size; i++) {
        dos_memory[0x100 + i] = buffer[i];
    }
    
    kprint("[DOS] Loaded ");
    kprint_int(size);
    kprint(" bytes at 0x100\n");
    
    return 0;
}

int dos_load_exe(const char* filename) {
    kprint("[DOS] Loading .EXE: ");
    kprint(filename);
    kprint("\n");
    
    uint8_t buffer[1024 * 1024];
    int size = wnkfs_read_file(filename, buffer, 1024 * 1024);
    if(size <= 0) {
        kprint("[DOS] File not found\n");
        return -1;
    }
    
    if(size < sizeof(MZHeader)) {
        kprint("[DOS] File too small\n");
        return -1;
    }
    
    MZHeader* mz = (MZHeader*)buffer;
    if(mz->signature != 0x5A4D) {
        kprint("[DOS] Not a valid .EXE file\n");
        return -1;
    }
    
    kprint("[DOS] .EXE format detected\n");
    kprint("[DOS] .EXE support is limited - only basic programs may work\n");
    
    return 0;
}

void dos_run(void) {
    kprint("\n[DOS] Running program...\n");
    kprint("[DOS] This is a stub - real emulation coming soon\n");
    kprint("[DOS] Press any key to return...\n");
    
    while(!(inb(0x64) & 1));
    while(inb(0x64) & 1) inb(0x60);
}

void dos_stop(void) {
    dos_running = 0;
    kprint("[DOS] Program stopped\n");
}

void dos_test(void) {
    kprint("\n=== DOS EMULATOR TEST ===\n");
    kprint("Memory allocated: 1MB\n");
    kprint("Status: Basic loader ready\n");
    kprint("Use 'run <file.com>' to load a program\n");
}