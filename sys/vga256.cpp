#include "vga256.h"
#include "kernel_stubs.h"
#include "video.h"
#include <stdint.h>

static uint8_t* vga_memory = (uint8_t*)0xA0000;
static uint8_t back_buffer[320 * 200];
static int vga_active = 0;
static int vga_width = 320;
static int vga_height = 200;

void vga_wait_key(void) {
    kprint("[VGA] Press any key to exit...\n");
    while(!(inb(0x64) & 1));
    uint8_t key = inb(0x60);
    (void)key;
}

int vga_init(void) {
    kprint("[VGA] Initializing Mode 13h...\n");
    
    __asm__ volatile(
        "mov $0x13, %%ax\n"
        "int $0x10\n"
        : : : "ax"
    );
    
    vga_active = 1;
    vga_width = 320;
    vga_height = 200;
    
    vga_clear(VGA_BLACK);
    vga_flip();
    
    kprint("[VGA] Mode 13h active: 320x200x256\n");
    return 0;
}

void vga_exit(void) {
    if(vga_active) {
        kprint("[VGA] Switching to text mode...\n");
        __asm__ volatile (
            "mov $0x03, %%ax\n"
            "int $0x10\n"
            : : : "ax"
        );
        vga_active = 0;
    }
}

void vga_pixel(int x, int y, uint8_t color) {
    if(!vga_active) return;
    if(x < 0 || x >= vga_width || y < 0 || y >= vga_height) return;
    back_buffer[y * vga_width + x] = color;
}

uint8_t vga_get_pixel(int x, int y) {
    if(!vga_active) return 0;
    if(x < 0 || x >= vga_width || y < 0 || y >= vga_height) return 0;
    return back_buffer[y * vga_width + x];
}

void vga_clear(uint8_t color) {
    if(!vga_active) return;
    for(int i = 0; i < vga_width * vga_height; i++) {
        back_buffer[i] = color;
    }
}

void vga_rect(int x, int y, int w, int h, uint8_t color) {
    for(int j = 0; j < h; j++) {
        for(int i = 0; i < w; i++) {
            vga_pixel(x + i, y + j, color);
        }
    }
}

void vga_line(int x1, int y1, int x2, int y2, uint8_t color) {
    int dx = x2 - x1;
    int dy = y2 - y1;
    int steps = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    if(steps == 0) steps = 1;
    
    for(int i = 0; i <= steps; i++) {
        int x = x1 + dx * i / steps;
        int y = y1 + dy * i / steps;
        vga_pixel(x, y, color);
    }
}

void vga_circle(int cx, int cy, int r, uint8_t color) {
    int x = 0, y = r;
    int d = 3 - 2 * r;
    while(y >= x) {
        vga_pixel(cx + x, cy + y, color);
        vga_pixel(cx - x, cy + y, color);
        vga_pixel(cx + x, cy - y, color);
        vga_pixel(cx - x, cy - y, color);
        vga_pixel(cx + y, cy + x, color);
        vga_pixel(cx - y, cy + x, color);
        vga_pixel(cx + y, cy - x, color);
        vga_pixel(cx - y, cy - x, color);
        if(d < 0) {
            d += 4 * x + 6;
        } else {
            d += 4 * (x - y) + 10;
            y--;
        }
        x++;
    }
}

void vga_flip(void) {
    if(!vga_active) return;
    for(int i = 0; i < vga_width * vga_height; i++) {
        vga_memory[i] = back_buffer[i];
    }
}

void vga_set_palette(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    if(!vga_active) return;
    outb(0x3C8, index);
    outb(0x3C9, r);
    outb(0x3C9, g);
    outb(0x3C9, b);
}

void vga_set_palette_rgb(uint8_t index, uint32_t rgb) {
    vga_set_palette(index, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}
void vga_blue_screen(void) {
    vga_init();
    
    for(int i = 0; i < vga_width * vga_height; i++) {
        vga_memory[i] = VGA_BLUE;
    }
    
    kprint("[VGA] Blue screen - press any key to exit\n");
    vga_wait_key();
    vga_exit();
}

void vga_demo_color_bars(void) {
    vga_init();
    int bar_width = vga_width / 16;
    for(int i = 0; i < 16; i++) {
        for(int y = 0; y < vga_height; y++) {
            for(int x = i * bar_width; x < (i+1) * bar_width; x++) {
                vga_memory[y * vga_width + x] = i;
            }
        }
    }
    vga_wait_key();
    vga_exit();
}

void vga_demo_plasma(void) {
    vga_init();
    for(int t = 0; t < 100 && vga_active; t++) {
        for(int y = 0; y < vga_height; y++) {
            for(int x = 0; x < vga_width; x++) {
                int v = (x + t) ^ (y + t);
                vga_memory[y * vga_width + x] = v & 0xFF;
            }
        }
        for(volatile int d = 0; d < 500000; d++);
        
        if(inb(0x64) & 1) {
            uint8_t key = inb(0x60);
            if(key == 0x01) break;
        }
    }
    vga_exit();
}

void vga_demo_mandelbrot(void) {
    vga_init();
    for(int y = 0; y < vga_height; y++) {
        for(int x = 0; x < vga_width; x++) {
            float zx = 0, zy = 0;
            float cx = (x - 160) / 80.0f;
            float cy = (y - 100) / 80.0f;
            int iter = 0;
            while(iter < 256 && zx*zx + zy*zy < 4) {
                float nx = zx*zx - zy*zy + cx;
                zy = 2*zx*zy + cy;
                zx = nx;
                iter++;
            }
            vga_memory[y * vga_width + x] = iter & 0xFF;
        }
    }
    vga_wait_key();
    vga_exit();
}