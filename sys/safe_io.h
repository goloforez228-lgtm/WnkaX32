#ifndef SAFE_IO_H
#define SAFE_IO_H

#include <stdint.h>

static inline uint8_t safe_inb(uint16_t port) {
    if(port < 0x100) {
        return inb(port);
    }
    kprint_color("[IO] Blocked access to port 0x", TXT_RED);
    kprint_hex16(port);
    kprint("\n");
    return 0;
}

static inline void safe_outb(uint16_t port, uint8_t value) {
    if(port < 0x100 || (port >= 0x3B0 && port <= 0x3DF)) {
        outb(port, value);
        return;
    }
    kprint_color("[IO] Blocked write to port 0x", TXT_RED);
    kprint_hex16(port);
    kprint("\n");
}

int safe_memcpy(void* dest, const void* src, uint32_t size) {
    uint32_t d = (uint32_t)dest;
    uint32_t s = (uint32_t)src;
    
    if(d < 0x100000 || d + size > 0x200000) {
        if(d < 0x100000) {
            kprint_color("[MEM] Copy to protected area!\n", TXT_RED);
            return -1;
        }
    }
    
    for(uint32_t i = 0; i < size; i++) {
        ((char*)dest)[i] = ((char*)src)[i];
    }
    return 0;
}

#endif