#include "video.h"
#include "kernel_stubs.h"
#include "graph.h"
#include <stdint.h>

#define NULL 0

#define TXT_WHITE    0x0F
#define TXT_BLACK    0x00
#define TXT_BLUE     0x01
#define TXT_GREEN    0x02
#define TXT_CYAN     0x03
#define TXT_RED      0x04
#define TXT_YELLOW   0x0E

#define BLOCK 219

extern "C" void play_sound(uint32_t nFrequence);
extern "C" void nosound(void);

static uint8_t current_color = 0x0F;
static int brush_size = 1;
static int running = 1;
static int paint_cursor_x = 40, paint_cursor_y = 12;

static const char* color_names[] = {
    "BLACK", "BLUE", "GREEN", "CYAN",
    "RED",   "PURPLE", "YELLOW", "WHITE",
    "GRAY",  "LBLUE", "LGREEN", "LCYAN",
    "LRED",  "LPURPLE", "LYELLOW", "LWHITE"
};

static void delay_ms(int ms) {
    for(volatile int i = 0; i < ms * 2000; i++);
}

static void draw_pixel(int x, int y, uint8_t color) {
    if(x >= 1 && x < 79 && y >= 2 && y < 22) {
        put_pixel(x, y, color, TXT_WHITE, BLOCK);
    }
}

static void draw_brush(int x, int y) {
    int half = brush_size / 2;
    for(int dy = -half; dy <= half; dy++) {
        for(int dx = -half; dx <= half; dx++) {
            draw_pixel(x + dx, y + dy, current_color);
        }
    }
    play_sound(800);
    delay_ms(15);
    nosound();
}

static void erase_pixel(int x, int y) {
    if(x >= 1 && x < 79 && y >= 2 && y < 22) {
        put_pixel(x, y, GRAY, TXT_WHITE, ' ');
    }
}

static void erase_brush(int x, int y) {
    int half = brush_size / 2;
    for(int dy = -half; dy <= half; dy++) {
        for(int dx = -half; dx <= half; dx++) {
            erase_pixel(x + dx, y + dy);
        }
    }
    play_sound(600);
    delay_ms(15);
    nosound();
}

static void clear_canvas(void) {
    for(int y = 2; y < 22; y++) {
        for(int x = 1; x < 79; x++) {
            put_pixel(x, y, GRAY, TXT_WHITE, ' ');
        }
    }
    play_sound(1000);
    delay_ms(100);
    nosound();
}

static void draw_ui(void) {
    clear_screen();
    
    kprint_at("=== PAINT v1.0 (WASD+Space) ===", 25, 0, (BLUE << 4) | TXT_WHITE);
    
    for(int x = 0; x < 80; x++) {
        put_pixel(x, 1, BLUE, TXT_WHITE, S_HLINE);
        put_pixel(x, 22, BLUE, TXT_WHITE, S_HLINE);
    }
    for(int y = 1; y < 23; y++) {
        put_pixel(0, y, BLUE, TXT_WHITE, S_VLINE);
        put_pixel(79, y, BLUE, TXT_WHITE, S_VLINE);
    }
    
    for(int i = 0; i < 16; i++) {
        int x = 2 + i * 5;
        for(int j = 0; j < 3; j++) {
            put_pixel(x + j, 23, i, TXT_WHITE, ' ');
        }
        kprint_at(color_names[i], x, 24, TXT_WHITE);
    }
    
    kprint_at("Color:", 2, 22, TXT_WHITE);
    put_pixel(9, 22, current_color, TXT_WHITE, ' ');
    put_pixel(10, 22, current_color, TXT_WHITE, ' ');
    put_pixel(11, 22, current_color, TXT_WHITE, ' ');
    
    kprint_at("Brush:", 14, 22, TXT_WHITE);
    kprint_int_at(brush_size, 21, 22, TXT_WHITE);
    kprint_at("(+/-)", 25, 22, TXT_YELLOW);
    
    kprint_at("WASD - move, Space - draw", 35, 22, TXT_CYAN);
    kprint_at("Shift+Space - erase", 55, 22, TXT_YELLOW);
    kprint_at("C - clear, ESC - exit", 2, 21, TXT_RED);
    kprint_at("1-9 - color shortcuts", 35, 21, TXT_GREEN);
    kprint_at("R - Red, G - Green, B - Blue, W - White", 2, 20, TXT_CYAN);
}

void paint_main(void) {
    draw_ui();
    
    paint_cursor_x = 40;
    paint_cursor_y = 12;
    running = 1;
    int shift_pressed = 0;
    
    while(running) {
        static int old_x = -1, old_y = -1;
        static uint16_t old_char = 0x0720;
        uint16_t* vga = (uint16_t*)0xB8000;
        
        if(old_x >= 0 && old_y >= 0) {
            vga[old_y * 80 + old_x] = old_char;
        }
        
        if(paint_cursor_x >= 1 && paint_cursor_x < 79 && paint_cursor_y >= 2 && paint_cursor_y < 22) {
            old_char = vga[paint_cursor_y * 80 + paint_cursor_x];
            old_x = paint_cursor_x;
            old_y = paint_cursor_y;
            vga[paint_cursor_y * 80 + paint_cursor_x] = (BLUE << 4) | 0x1F;
        }
        
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            
            if(sc == 0x2A || sc == 0x36) {
                shift_pressed = 1;
            }
            if(sc == 0xAA || sc == 0xB6) {
                shift_pressed = 0;
            }
            
            if(sc < 0x80) {
                
                if(sc == 0x01) {
                    running = 0;
                    break;
                }
                
                if(sc == 0x11 || sc == 0x48) {
                    if(paint_cursor_y > 2) paint_cursor_y--;
                }
                if(sc == 0x1F || sc == 0x50) {
                    if(paint_cursor_y < 21) paint_cursor_y++;
                }
                if(sc == 0x1E || sc == 0x4B) {
                    if(paint_cursor_x > 1) paint_cursor_x--;
                }
                if(sc == 0x20 || sc == 0x4D) {
                    if(paint_cursor_x < 78) paint_cursor_x++;
                }
                
                if(sc == 0x39) {
                    if(paint_cursor_y >= 2 && paint_cursor_y < 22 && paint_cursor_x >= 1 && paint_cursor_x < 79) {
                        if(shift_pressed) {
                            erase_brush(paint_cursor_x, paint_cursor_y);
                        } else {
                            draw_brush(paint_cursor_x, paint_cursor_y);
                        }
                    }
                }
                
                if(sc == 0x2E) {
                    clear_canvas();
                }
                
                if(sc == 0x0D) {
                    if(brush_size < 9) brush_size += 2;
                    if(brush_size > 9) brush_size = 9;
                    kprint_int_at(brush_size, 21, 22, TXT_WHITE);
                    play_sound(1000);
                    delay_ms(50);
                    nosound();
                }
                
                if(sc == 0x0C) {
                    if(brush_size > 1) brush_size -= 2;
                    if(brush_size < 1) brush_size = 1;
                    kprint_int_at(brush_size, 21, 22, TXT_WHITE);
                    play_sound(800);
                    delay_ms(50);
                    nosound();
                }
                
                if(sc >= 0x02 && sc <= 0x0A) {
                    current_color = sc - 0x02;
                    put_pixel(9, 22, current_color, TXT_WHITE, ' ');
                    put_pixel(10, 22, current_color, TXT_WHITE, ' ');
                    put_pixel(11, 22, current_color, TXT_WHITE, ' ');
                    play_sound(1500);
                    delay_ms(50);
                    nosound();
                }
                
                if(sc == 0x0B) {
                    current_color = BLACK;
                    put_pixel(9, 22, current_color, TXT_WHITE, ' ');
                    put_pixel(10, 22, current_color, TXT_WHITE, ' ');
                    put_pixel(11, 22, current_color, TXT_WHITE, ' ');
                }
                
                if(sc == 0x13) {
                    current_color = RED;
                    put_pixel(9, 22, current_color, TXT_WHITE, ' ');
                    put_pixel(10, 22, current_color, TXT_WHITE, ' ');
                    put_pixel(11, 22, current_color, TXT_WHITE, ' ');
                }
                
                if(sc == 0x30) {
                    current_color = BLUE;
                    put_pixel(9, 22, current_color, TXT_WHITE, ' ');
                    put_pixel(10, 22, current_color, TXT_WHITE, ' ');
                    put_pixel(11, 22, current_color, TXT_WHITE, ' ');
                }
                
                if(sc == 0x22) {
                    current_color = GREEN;
                    put_pixel(9, 22, current_color, TXT_WHITE, ' ');
                    put_pixel(10, 22, current_color, TXT_WHITE, ' ');
                    put_pixel(11, 22, current_color, TXT_WHITE, ' ');
                }
                
                if(sc == 0x11) {
                    current_color = TXT_WHITE;
                    put_pixel(9, 22, current_color, TXT_WHITE, ' ');
                    put_pixel(10, 22, current_color, TXT_WHITE, ' ');
                    put_pixel(11, 22, current_color, TXT_WHITE, ' ');
                }
                
                if(sc == 0x15) {
                    current_color = 0x0E;
                    put_pixel(9, 22, current_color, TXT_WHITE, ' ');
                    put_pixel(10, 22, current_color, TXT_WHITE, ' ');
                    put_pixel(11, 22, current_color, TXT_WHITE, ' ');
                }
            }
        }
        
        for(volatile int i = 0; i < 500; i++);
    }
    
    clear_screen();
    kprint_color("Paint exited\n", TXT_GREEN);
}