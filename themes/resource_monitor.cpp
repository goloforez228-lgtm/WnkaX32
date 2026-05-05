#include "video.h"
#include "graph.h"
#include "resource_monitor.h"
#include <stdint.h>

extern int seconds;
extern uint64_t total_sectors;
extern uint64_t free_sectors;
extern uint32_t total_ram; 
extern uint32_t used_ram;   

uint32_t last_second = 0;
int cpu_history[60] = {0};
int history_index = 0;

uint64_t last_idle = 0;
uint64_t last_total = 0;

void draw_progress_bar(int x, int y, int width, int percent, uint8_t color) {
    if(percent < 0) percent = 0;
    if(percent > 100) percent = 100;
    
    int fill = (percent * width) / 100;
    
    put_pixel(x - 1, y, GRAY, TXT_WHITE, '[');
    put_pixel(x + width, y, GRAY, TXT_WHITE, ']');
    
    for(int i = 0; i < fill; i++) {
        put_pixel(x + i, y, color, TXT_WHITE, BLOCK);
    }
    for(int i = fill; i < width; i++) {
        put_pixel(x + i, y, GRAY, TXT_WHITE, BLOCK_LIGHT);
    }
    
    char percent_str[4];
    percent_str[0] = (percent / 10) + '0';
    percent_str[1] = (percent % 10) + '0';
    percent_str[2] = '%';
    percent_str[3] = '\0';
    kprint_at(percent_str, x + width + 2, y, (BLACK << 4) | TXT_YELLOW);
}

void draw_cpu_graph(int x, int y, int width, int height) {
    draw_frame(x - 1, y - 1, width + 2, height + 2, GRAY, TXT_WHITE);
    
    for(int i = 0; i < width && i < 60; i++) {
        int idx = (history_index - width + i + 60) % 60;
        int value = cpu_history[idx];
        int bar_height = (value * height) / 100;
        if(bar_height > height) bar_height = height;
        
        for(int h = 0; h < bar_height; h++) {
            int py = y + height - 1 - h;
            if(py >= y && py < y + height) {
                put_pixel(x + i, py, CYAN, TXT_WHITE, BLOCK);
            }
        }
    }
}

void update_stats() {
    static uint32_t frame_counter = 0;
    frame_counter++;
    
    if(frame_counter % 10 != 0) return;
    
    int cpu_load = 30 + (seconds * 3) % 50;
    
    if(seconds % 17 == 0) cpu_load = 85 + (seconds % 10);
    if(seconds % 23 == 0) cpu_load = 15 + (seconds % 20);
    
    cpu_history[history_index] = cpu_load;
    history_index = (history_index + 1) % 60;

    static uint32_t last_ram_update = 0;
    if(seconds != last_ram_update) {
        last_ram_update = seconds;
        used_ram = (used_ram + 50) % (total_ram * 8 / 10);
        if(used_ram < 100) used_ram = 1000;
    }
}

void show_resource_monitor() {
    clear_screen_bg(BLACK);
    
    draw_shadow_window(15, 1, 50, 3, CYAN, TXT_WHITE, "SYSTEM RESOURCES");
    kprint_at("Press ESC to exit", 30, 3, (BLACK << 4) | TXT_WHITE);
    
    int running = 1;
    int frame = 0;
    
    while(running) {
        update_stats();
        
        kprint_at("CPU Usage:", 5, 5, (BLACK << 4) | TXT_YELLOW);
        int cpu = cpu_history[(history_index - 1 + 60) % 60];
        draw_progress_bar(20, 5, 40, cpu, GREEN);
        
        if(frame % 5 == 0) {
            clear_area(20, 7, 60, 15);
            draw_cpu_graph(20, 7, 40, 8);
        }
        
        kprint_at("RAM Usage:", 5, 16, (BLACK << 4) | TXT_YELLOW);
        int ram_percent = (used_ram * 100) / total_ram;
        draw_progress_bar(20, 16, 40, ram_percent, BLUE);
        
        kprint_at("Used: ", 20, 17, (BLACK << 4) | TXT_WHITE);
        kprint_int_at(used_ram / 1024, 26, 17, (BLACK << 4) | TXT_GREEN);
        kprint_at(" KB  Total: ", 33, 17, (BLACK << 4) | TXT_WHITE);
        kprint_int_at(total_ram / 1024, 46, 17, (BLACK << 4) | TXT_GREEN);
        kprint_at(" KB", 52, 17, (BLACK << 4) | TXT_WHITE);
        
        kprint_at("Disk Usage:", 5, 19, (BLACK << 4) | TXT_YELLOW);
        
        int disk_percent = 0;
        uint32_t total_mb = 0;
        uint32_t used_mb = 0;
        
        if(total_sectors > 0) {
            uint64_t used_sectors = total_sectors - free_sectors;
            
            total_mb = (uint32_t)(total_sectors / 2048);
            used_mb = (uint32_t)(used_sectors / 2048);
            
            if(total_mb > 0) {
                disk_percent = (used_mb * 100) / total_mb;
            }
            
            draw_progress_bar(20, 19, 40, disk_percent, RED);
            
            char used_str[20];
            char total_str[20];
            
            kprint_at("Used: ", 20, 20, (BLACK << 4) | TXT_WHITE);
            
            for(int i = 0; i < 10; i++) {
                put_pixel(26 + i, 20, BLACK, TXT_WHITE, ' ');
            }
            kprint_int_at(used_mb, 26, 20, (BLACK << 4) | TXT_GREEN);
            
            kprint_at(" MB  Total: ", 33, 20, (BLACK << 4) | TXT_WHITE);
            
            for(int i = 0; i < 10; i++) {
                put_pixel(46 + i, 20, BLACK, TXT_WHITE, ' ');
            }
            kprint_int_at(total_mb, 46, 20, (BLACK << 4) | TXT_GREEN);
            kprint_at(" MB", 52, 20, (BLACK << 4) | TXT_WHITE);
        } else {
            draw_progress_bar(20, 19, 40, 0, RED);
            kprint_at("No disk detected", 22, 20, (BLACK << 4) | TXT_RED);
        }
        
        kprint_at("Uptime: ", 5, 22, (BLACK << 4) | TXT_YELLOW);
        int hours = seconds / 3600;
        int mins = (seconds % 3600) / 60;
        int secs = seconds % 60;
        
        for(int i = 0; i < 20; i++) {
            put_pixel(14 + i, 22, BLACK, TXT_WHITE, ' ');
        }
        
        kprint_int_at(hours, 14, 22, (BLACK << 4) | TXT_CYAN);
        kprint_at("h ", 16, 22, (BLACK << 4) | TXT_WHITE);
        kprint_int_at(mins, 19, 22, (BLACK << 4) | TXT_CYAN);
        kprint_at("m ", 21, 22, (BLACK << 4) | TXT_WHITE);
        kprint_int_at(secs, 24, 22, (BLACK << 4) | TXT_CYAN);
        kprint_at("s", 26, 22, (BLACK << 4) | TXT_WHITE);
        
        if(inb(0x64) & 1) {
            uint8_t key = inb(0x60);
            if(key == 0x01) running = 0;
        }
        
        move_cursor(79, 24);
        
        for(volatile int i = 0; i < 2000; i++);
        frame++;
    }
    
    clear_screen();
}