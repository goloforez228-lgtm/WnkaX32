#ifndef VESA_H
#define VESA_H

#include <stdint.h>
#include "video.h"
#include "graph.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VBE_DISPI_IOPORT_INDEX  0x01CE
#define VBE_DISPI_IOPORT_DATA   0x01CF
#define VBE_DISPI_INDEX_ID      0x00
#define VBE_DISPI_INDEX_XRES    0x01
#define VBE_DISPI_INDEX_YRES    0x02
#define VBE_DISPI_INDEX_BPP     0x03
#define VBE_DISPI_INDEX_ENABLE  0x04
#define VBE_DISPI_ENABLED       0x01
#define VBE_DISPI_LFB_ENABLED   0x40
#define VESA_LFB_ADDR1  0xE0000000 
#define VESA_LFB_ADDR2  0xFD000000 
#define VESA_LFB_ADDR3  0xFC000000  
#define VESA_LFB_ADDR4  0xF0000000 

static uint16_t* vesa_fb = 0;      
static uint16_t vesa_w = 1024;      
static uint16_t vesa_h = 768;      
static uint32_t vesa_pitch = 2048;  
static int vesa_ok = 0;

static inline void vesa_outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void vesa_outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t vesa_inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void vesa_iodelay(void) {
    for(volatile int i = 0; i < 100; i++) vesa_inb(0x80);
}
static inline void my_outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t my_inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    my_outl(0xCF8, address);
    for(volatile int i = 0; i < 500; i++);
    return my_inl(0xCFC);
}

static inline uint16_t vesa_inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static int vesa_detect_bga(void) {
    uint32_t device_id = pci_read_config(0, 2, 0, 0);
    
    if((device_id & 0xFFFF) == 0x1234) {
        uint32_t dev = device_id >> 16;
        if(dev == 0x1111) {
            kprint_color("[VESA] BGA detected (Bochs/QEMU/VM)\n", TXT_GREEN);
            return 1;
        }
    }
    
    if((device_id & 0xFFFF) == 0x80EE) {
        kprint_color("[VESA] VirtualBox VGA detected\n", TXT_GREEN);
        return 1;
    }
    
    if((device_id & 0xFFFF) == 0x15AD) {
        kprint_color("[VESA] VMware SVGA detected\n", TXT_GREEN);
        return 1;
    }
    
    return 0;
}

static int vesa_probe_lfb(void) {
    uint32_t lfb_addresses[] = {
        0xE0000000,
        0xFD000000,  
        0xFC000000,  
        0xF0000000, 
        0xF8000000, 
        0xF0000000,  
        0xD0000000,  
        0xC0000000,  
    };
    
    int num_addresses = sizeof(lfb_addresses) / sizeof(lfb_addresses[0]);
    
    for(int i = 0; i < num_addresses; i++) {
        uint32_t addr = lfb_addresses[i];
        
        volatile uint16_t* test = (volatile uint16_t*)addr;
        uint16_t old_val = *test;
        *test = 0xAA55;
        
        for(volatile int d = 0; d < 100; d++);
        
        if(*test == 0xAA55) {
            *test = old_val;
            
            *test = 0xFFFF;
            for(volatile int d = 0; d < 100; d++);
            if(*test == 0xFFFF) {
                *test = 0x0000;
                for(volatile int d = 0; d < 100; d++);
                if(*test == 0x0000) {
                    *test = old_val;
                    vesa_fb = (uint16_t*)addr;
                    
                    kprint_color("[VESA] LFB found at 0x", TXT_GREEN);
                    kprint_hex32(addr);
                    kprint("\n");
                    return 1;
                }
                *test = old_val;
            }
            *test = old_val;
        }
    }
    
    return 0;
}

static int vesa_use_banks = 0;
static uint32_t vesa_bank_size = 65536;
static int vesa_current_bank = -1;
static int vesa_bpp = 16;

static void vesa_set_bank(int bank) {
    if(bank == vesa_current_bank) return;
    vesa_current_bank = bank;
    outw(0x1CE, 0);
    outw(0x1CF, bank);
}
static int vesa_set_mode(int width, int height, int bpp);
static void vesa_enable(void) {
    kprint("[VESA] Initializing VESA/VBE...\n");
    
    vesa_outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ID);
    vesa_iodelay();
    uint16_t vbe_id = vesa_inw(VBE_DISPI_IOPORT_DATA);
    vesa_iodelay();
    
    kprint("[VESA] VBE ID: 0x");
    kprint_hex16(vbe_id);
    kprint("\n");
    
    if(vbe_id < 0xB0C0 || vbe_id > 0xB0C5) {
        kprint_color("[VESA] No VBE/BGA device found!\n", TXT_YELLOW);
        
        if(!vesa_detect_bga()) {
            kprint_color("[VESA] No compatible video card detected\n", TXT_YELLOW);
            kprint("[VESA] Trying direct LFB probe...\n");
            if(vesa_probe_lfb()) {
                vesa_ok = 1;
                return;
            }
            
            kprint_color("\n========================================\n", TXT_RED);
            kprint_color("  THIS VIDEO CARD IS NOT SUPPORTED\n", TXT_RED);
            kprint_color("  VESA/VBE mode is not available\n", TXT_RED);
            kprint_color("\nPress any key to continue in text mode...\n", TXT_CYAN);
            
            vesa_ok = 0;
            return;
        }
    }
    
    vesa_ok = 1;
    
    vesa_set_mode(1024, 768, 16);
    
    if(vesa_w == 0 || vesa_h == 0) {
        vesa_set_mode(800, 600, 16);
    }
    if(vesa_w == 0 || vesa_h == 0) {
        vesa_set_mode(640, 480, 16);
    }
    if(vesa_w == 0 || vesa_h == 0) {
        vesa_set_mode(640, 480, 8);
    }
    
    if(vesa_w > 0 && vesa_h > 0) {
        kprint_color("[VESA] Initialization complete!\n", TXT_GREEN);
    }
}

static int vesa_set_mode(int width, int height, int bpp) {
    if(!vesa_ok && bpp != 0) {
        vesa_enable();
    }
    if(!vesa_ok && bpp == 0) return 0;
    
    vesa_outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ENABLE);
    vesa_outw(VBE_DISPI_IOPORT_DATA, 0x00);
    vesa_iodelay();
    
    vesa_outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_XRES);
    vesa_outw(VBE_DISPI_IOPORT_DATA, width);
    vesa_iodelay();
    
    vesa_outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_YRES);
    vesa_outw(VBE_DISPI_IOPORT_DATA, height);
    vesa_iodelay();
    
    vesa_outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_BPP);
    vesa_outw(VBE_DISPI_IOPORT_DATA, bpp);
    vesa_iodelay();
    
    vesa_use_banks = 0;
    
    if(bpp <= 8) {
        vesa_outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ENABLE);
        vesa_outw(VBE_DISPI_IOPORT_DATA, VBE_DISPI_ENABLED);
        vesa_iodelay();
        
        volatile uint8_t* bank_fb = (volatile uint8_t*)0xA0000;
        uint8_t old1 = bank_fb[0];
        uint8_t old2 = bank_fb[100];
        bank_fb[0] = 0x55;
        bank_fb[100] = 0xAA;
        
        if(bank_fb[0] == 0x55 && bank_fb[100] == 0xAA) {
            bank_fb[0] = old1;
            bank_fb[100] = old2;
            vesa_use_banks = 1;
            vesa_fb = (uint16_t*)0xA0000;
            vesa_w = width;
            vesa_h = height;
            vesa_pitch = width;
            vesa_bpp = bpp;
            vesa_current_bank = -1;
            kprint_color("[VESA] Banked: ", TXT_GREEN);
            kprint_int(width); kprint("x"); kprint_int(height); kprint("@"); kprint_int(bpp); kprint("bpp\n");
            return 1;
        }
        bank_fb[0] = old1;
        bank_fb[100] = old2;
    }
    
    vesa_outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ENABLE);
    vesa_outw(VBE_DISPI_IOPORT_DATA, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);
    vesa_iodelay();
    
    if(vesa_probe_lfb()) {
        vesa_w = width;
        vesa_h = height;
        vesa_pitch = width * (bpp / 8);
        vesa_bpp = bpp;
        kprint_color("[VESA] LFB: ", TXT_GREEN);
        kprint_int(width); kprint("x"); kprint_int(height); kprint("@"); kprint_int(bpp); kprint("bpp\n");
        return 1;
    }
    
    if(bpp > 8) {
        return vesa_set_mode(width, height, 8);
    }
    
    kprint_color("[VESA] Failed to set mode\n", TXT_RED);
    return 0;
}

static inline uint16_t vesa_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

static void vesa_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if(!vesa_ok || !vesa_fb) return;
    if(x < 0 || x >= (int)vesa_w || y < 0 || y >= (int)vesa_h) return;
    
    vesa_fb[y * (vesa_pitch / 2) + x] = vesa_rgb565(r, g, b);
}

static void vesa_pixel16(int x, int y, uint16_t color) {
    if(!vesa_ok || !vesa_fb) return;
    if(x < 0 || x >= (int)vesa_w || y < 0 || y >= (int)vesa_h) return;
    
    vesa_fb[y * (vesa_pitch / 2) + x] = color;
}

static void vesa_clear(uint8_t r, uint8_t g, uint8_t b) {
    if(!vesa_ok || !vesa_fb) return;
    uint16_t color = vesa_rgb565(r, g, b);
    for(int y = 0; y < (int)vesa_h; y++) {
        uint16_t* line = vesa_fb + y * (vesa_pitch / 2);
        for(int x = 0; x < (int)vesa_w; x++) {
            line[x] = color;
        }
    }
}

static void vesa_hline(int x, int y, int len, uint8_t r, uint8_t g, uint8_t b) {
    if(!vesa_ok || !vesa_fb) return;
    if(y < 0 || y >= (int)vesa_h) return;
    if(x < 0) { len += x; x = 0; }
    if(x + len > (int)vesa_w) len = vesa_w - x;
    if(len <= 0) return;
    
    uint16_t color = vesa_rgb565(r, g, b);
    uint16_t* line = vesa_fb + y * (vesa_pitch / 2);
    for(int i = 0; i < len; i++) line[x + i] = color;
}

static void vesa_vline(int x, int y, int len, uint8_t r, uint8_t g, uint8_t b) {
    if(!vesa_ok || !vesa_fb) return;
    if(x < 0 || x >= (int)vesa_w) return;
    if(y < 0) { len += y; y = 0; }
    if(y + len > (int)vesa_h) len = vesa_h - y;
    if(len <= 0) return;
    
    uint16_t color = vesa_rgb565(r, g, b);
    for(int i = 0; i < len; i++) {
        vesa_fb[(y + i) * (vesa_pitch / 2) + x] = color;
    }
}

static void vesa_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    for(int dy = 0; dy < h; dy++) {
        vesa_hline(x, y + dy, w, r, g, b);
    }
}

static void vesa_line(int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b) {
    int dx = x2 - x1;
    int dy = y2 - y1;
    int steps = dx > dy ? dx : dy;
    if(steps < 0) steps = -steps;
    if(steps == 0) steps = 1;
    
    for(int i = 0; i <= steps; i++) {
        int x = x1 + (dx * i) / steps;
        int y = y1 + (dy * i) / steps;
        vesa_pixel(x, y, r, g, b);
    }
}

static void vesa_gradient_h(int x, int y, int w, int h,
                            uint8_t r1, uint8_t g1, uint8_t b1,
                            uint8_t r2, uint8_t g2, uint8_t b2) {
    for(int dx = 0; dx < w; dx++) {
        uint8_t r = r1 + ((r2 - r1) * dx) / w;
        uint8_t g = g1 + ((g2 - g1) * dx) / w;
        uint8_t b = b1 + ((b2 - b1) * dx) / w;
        for(int dy = 0; dy < h; dy++) {
            vesa_pixel(x + dx, y + dy, r, g, b);
        }
    }
}

static void vesa_gradient_v(int x, int y, int w, int h,
                            uint8_t r1, uint8_t g1, uint8_t b1,
                            uint8_t r2, uint8_t g2, uint8_t b2) {
    for(int dy = 0; dy < h; dy++) {
        uint8_t r = r1 + ((r2 - r1) * dy) / h;
        uint8_t g = g1 + ((g2 - g1) * dy) / h;
        uint8_t b = b1 + ((b2 - b1) * dy) / h;
        for(int dx = 0; dx < w; dx++) {
            vesa_pixel(x + dx, y + dy, r, g, b);
        }
    }
}

static void vesa_draw_char(int x, int y, char ch, uint8_t r, uint8_t g, uint8_t b) {
    if(!vesa_ok) return;
    uint16_t color = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    for(int dy = 0; dy < 8; dy++) {
        for(int dx = 0; dx < 8; dx++) {
            uint16_t* ptr = (uint16_t*)((uint8_t*)vesa_fb + (y + dy) * vesa_pitch + (x + dx) * 2);
            *ptr = color;
        }
    }
}

static void vesa_draw_text(int x, int y, const char* str, uint8_t r, uint8_t g, uint8_t b) {
    int cx = x;
    int cy = y;
    while(*str) {
        if(*str == '\n') {
            cx = x;
            cy += 10;
        } else {
            vesa_draw_char(cx, cy, *str, r, g, b);
            cx += 8;
        }
        str++;
    }
}

static inline void vesa_wait_vsync(void) {
    vesa_inb(0x3DA);
    while(!(vesa_inb(0x3DA) & 0x08));
    while(vesa_inb(0x3DA) & 0x08);
}

static inline void vesa_flip(void) {
    vesa_wait_vsync();
}

static int vesa_get_w(void)   { return vesa_w; }
static int vesa_get_h(void)   { return vesa_h; }
static int vesa_ready(void)   { return vesa_ok; }
static void vesa_exit(void)   { vesa_ok = 0; }

#ifdef __cplusplus
}
#endif

#endif