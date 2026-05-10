#include "video.h"
#include "kernel_com.h"

void run_com(int idx) {
    ComEntry* prog = &com_programs[idx];
    
    kprint("\n╔════════════════════════════════╗\n");
    kprint("║     16-BIT COMPATIBILITY      ║\n");
    kprint("╠════════════════════════════════╣\n");
    kprint("║ Program: ");
    kprint(prog->name);
    kprint("\n");
    kprint("║ Size:    ");
    kprint_int(prog->size);
    kprint(" bytes\n");
    kprint("║ Mode:    Real Mode (16-bit)\n");
    kprint("╚════════════════════════════════╝\n\n");
    
    kprint_color("[COM] Program output:\n", 0x0A);
    
    kprint("Header: ");
    for(int i = 0; i < 16 && i < prog->size; i++) {
        char c = prog->data[i];
        if(c >= 32 && c <= 126) {
            char s[2] = {c, 0};
            kprint(s);
        } else {
            kprint(".");
        }
    }
    kprint("\n\n");
    
    kprint_color("[COM] Program finished (emulated)\n", 0x0C);
}