#include "mouse.h"
#include "video.h"

int mouse_x = 40;
int mouse_y = 12;
int old_x = 40;
int old_y = 12;
uint8_t mouse_btn = 0;
uint16_t saved_char = 0x0720;
int mouse_present = 0;

static void mouse_delay(void) {
    for(volatile int i = 0; i < 1000; i++);
}

static int mouse_wait_read(void) {
    for(int i = 0; i < 100000; i++) {
        if(inb(0x64) & 0x01) return 1;
    }
    return 0;
}

static int mouse_wait_write(void) {
    for(int i = 0; i < 100000; i++) {
        if(!(inb(0x64) & 0x02)) return 1;
    }
    return 0;
}

static void mouse_write(uint8_t data) {
    mouse_wait_write();
    outb(0x64, 0xD4);
    mouse_wait_write();
    outb(0x60, data);
}

static uint8_t mouse_read(void) {
    mouse_wait_read();
    return inb(0x60);
}

void init_mouse(void) {
    kprint("[MOUSE] Init...\n");
    
    while(inb(0x64) & 0x01) inb(0x60);
    
    mouse_wait_write();
    outb(0x64, 0xA8);
    
    mouse_wait_write();
    outb(0x64, 0x20);
    uint8_t cfg = mouse_read();
    cfg |= 0x02;  
    cfg &= ~0x20; 
    mouse_wait_write();
    outb(0x64, 0x60);
    mouse_wait_write();
    outb(0x60, cfg);
    
    mouse_write(0xFF);
    uint8_t ack = mouse_read();
    if(ack != 0xFA) {
        kprint("[MOUSE] Reset failed\n");
        return;
    }
    mouse_read(); 
    mouse_read(); 
    
    mouse_write(0xF4);
    ack = mouse_read();
    if(ack != 0xFA) {
        kprint("[MOUSE] Enable failed\n");
        return;
    }
    
    mouse_present = 1;
    mouse_x = 40;
    mouse_y = 12;
    mouse_btn = 0;
    
    kprint("[MOUSE] OK\n");
}

void poll_mouse(void) {
    if(!mouse_present) return;
    
    while(inb(0x64) & 0x01) {
        uint8_t status = inb(0x64);
        if(!(status & 0x20)) {
            inb(0x60); 
            continue;
        }
        
        uint8_t data = inb(0x60);
        
        static uint8_t packet[3];
        static int idx = 0;
        
        if(idx == 0) {
            if(!(data & 0x08)) continue;
        }
        
        packet[idx++] = data;
        
        if(idx == 3) {
            idx = 0;
            
            mouse_btn = packet[0] & 0x07;
            
            int dx = (int8_t)packet[1];
            int dy = (int8_t)packet[2];
            
            if(dx > 2) dx = 2;
            if(dx < -2) dx = -2;
            if(dy > 2) dy = 2;
            if(dy < -2) dy = -2;
            
            mouse_x += dx;
            mouse_y -= dy;
            
            if(mouse_x < 0) mouse_x = 0;
            if(mouse_x > 79) mouse_x = 79;
            if(mouse_y < 0) mouse_y = 0;
            if(mouse_y > 24) mouse_y = 24;
            
            old_x = mouse_x;
            old_y = mouse_y;
        }
    }
}

void draw_mouse(void) {
    if(!mouse_present) return;
    
    static int last_x = -1, last_y = -1;
    static uint16_t saved = 0x0720;
    
    uint16_t* vga = (uint16_t*)0xB8000;
    
    if(last_x >= 0 && last_x < 80 && last_y >= 0 && last_y < 25) {
        vga[last_y * 80 + last_x] = saved;
    }
    
    if(mouse_x >= 0 && mouse_x < 80 && mouse_y >= 0 && mouse_y < 25) {
        saved = vga[mouse_y * 80 + mouse_x];
        last_x = mouse_x;
        last_y = mouse_y;
        
        if(mouse_btn & 1) {
            vga[mouse_y * 80 + mouse_x] = 0x2F00 | '*';
        } else {
            vga[mouse_y * 80 + mouse_x] = 0x7000 | 0xB1;
        }
    }
}

int mouse_over(int x, int y, int w, int h) {
    if(!mouse_present) return 0;
    return (mouse_x >= x && mouse_x < x + w && 
            mouse_y >= y && mouse_y < y + h);
}

void disable_mouse(void) {
    if(!mouse_present) return;
    
    uint16_t* vga = (uint16_t*)0xB8000;
    if(mouse_x >= 0 && mouse_x < 80 && mouse_y >= 0 && mouse_y < 25) {
        vga[mouse_y * 80 + mouse_x] = 0x0720;
    }
    
    mouse_write(0xF5);
    mouse_present = 0;
}

void refresh_mouse(void) {
}

void mouse_test(void) {
    kprint("Mouse: ");
    kprint_int(mouse_x);
    kprint(",");
    kprint_int(mouse_y);
    kprint(" BTN:");
    kprint_int(mouse_btn);
    kprint("\n");
}