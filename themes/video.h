#ifndef VIDEO_H
#define VIDEO_H

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

#ifdef __cplusplus
extern "C" {
#endif

extern int cursor_x;
extern int cursor_y;
extern int seconds;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void clear_screen();
extern uint8_t console_color;
void kprint(const char* str);
void kprint_color(const char* str, uint8_t color);
void update_cursor(int x, int y);
void kprint_at(const char* str, int x, int y, uint8_t color);
void kprint_int_at(int num, int x, int y, uint8_t color);
void kprint_int(uint32_t n);   
void kprint_hex(uint16_t n);   
void kprint_hex8(uint8_t n);
void kprint_hex16(uint16_t n);
void kprint_hex32(uint32_t n);
extern uint32_t total_ram;
extern uint32_t used_ram;

#ifdef __cplusplus
}
#endif

#endif