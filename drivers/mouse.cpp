#include "mouse.h"
#include "video.h"

int mouse_x = 512;
int mouse_y = 384;
uint8_t mouse_btn = 0;
int mouse_present = 0;

int vesa_mouse_x = 512;
int vesa_mouse_y = 384;

float vesa_smooth_x = 512.0f;
float vesa_smooth_y = 384.0f;
int mouse_speed = 1;

static void mouse_delay(void) { for(volatile int i = 0; i < 1000; i++); }
static int mouse_wait_read(void) { for(int i = 0; i < 100000; i++) { if(inb(0x64) & 0x01) return 1; } return 0; }
static int mouse_wait_write(void) { for(int i = 0; i < 100000; i++) { if(!(inb(0x64) & 0x02)) return 1; } return 0; }
static void mouse_write(uint8_t data) { mouse_wait_write(); outb(0x64, 0xD4); mouse_wait_write(); outb(0x60, data); }
static uint8_t mouse_read(void) { mouse_wait_read(); return inb(0x60); }

void init_mouse(void) {
    kprint("[MOUSE] Init...\n");
    while(inb(0x64) & 0x01) inb(0x60);
    mouse_wait_write(); outb(0x64, 0xA8);
    mouse_wait_write(); outb(0x64, 0x20);
    uint8_t cfg = mouse_read(); cfg |= 0x02; cfg &= ~0x20;
    mouse_wait_write(); outb(0x64, 0x60); mouse_wait_write(); outb(0x60, cfg);
    mouse_write(0xFF);
    if(mouse_read() != 0xFA) { kprint("[MOUSE] Reset failed\n"); return; }
    mouse_read(); mouse_read();
    mouse_write(0xF4);
    if(mouse_read() != 0xFA) { kprint("[MOUSE] Enable failed\n"); return; }
    mouse_present = 1;
    mouse_x = 512; mouse_y = 384; mouse_btn = 0;
    vesa_mouse_x = 512; vesa_mouse_y = 384;
    vesa_smooth_x = 512.0f; vesa_smooth_y = 384.0f;
    kprint("[MOUSE] OK\n");
}

void poll_mouse(void) {
    if(!mouse_present) return;
    
    while(inb(0x64) & 0x01) {
        uint8_t status = inb(0x64);
        if(!(status & 0x20)) { inb(0x60); continue; }
        
        uint8_t data = inb(0x60);
        static uint8_t packet[3];
        static int idx = 0;
        
        if(idx == 0 && !(data & 0x08)) continue;
        packet[idx++] = data;
        
        if(idx == 3) {
            idx = 0;
            mouse_btn = packet[0] & 0x07;
            
            int dx = (int8_t)packet[1];
            int dy = (int8_t)packet[2];
            
            dx *= 3;
            dy *= 3;
            
            if(dx > 15) dx = 15;
            if(dx < -15) dx = -15;
            if(dy > 15) dy = 15;
            if(dy < -15) dy = -15;
            
            mouse_x += dx;
            mouse_y -= dy; 
            
            if(mouse_x < 0) mouse_x = 0;
            if(mouse_x > 1023) mouse_x = 1023;
            if(mouse_y < 0) mouse_y = 0;
            if(mouse_y > 767) mouse_y = 767;
            
            vesa_mouse_x = mouse_x;
            vesa_mouse_y = mouse_y;
            vesa_smooth_x = (float)mouse_x;
            vesa_smooth_y = (float)mouse_y;
        }
    }
}

int get_mouse_x(void) { return mouse_x; }
int get_mouse_y(void) { return mouse_y; }
uint8_t get_mouse_btn(void) { return mouse_btn; }

int get_vesa_mouse_x(void) { return mouse_x; }
int get_vesa_mouse_y(void) { return mouse_y; }

void set_mouse_speed(int speed) { if(speed>=1&&speed<=10)mouse_speed=speed; }
int get_mouse_speed(void) { return mouse_speed; }

void draw_mouse(void) { if(!mouse_present) return; }

int mouse_over(int x, int y, int w, int h) {
    if(!mouse_present) return 0;
    return (mouse_x >= x && mouse_x < x + w && mouse_y >= y && mouse_y < y + h);
}

void disable_mouse(void) {
    if(!mouse_present) return;
    mouse_write(0xF5);
    mouse_present = 0;
}

void refresh_mouse(void) {}
void mouse_test(void) { kprint("OK\n"); }