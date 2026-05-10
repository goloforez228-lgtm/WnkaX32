#ifndef GRAPH_H
#define GRAPH_H

#include "mouse.h"
#include "video.h"
#include <stdint.h>

extern int SCREEN_WIDTH;
extern int SCREEN_HEIGHT;
#define BLACK  0x0
#define BLUE   0x1
#define GREEN  0x2
#define CYAN   0x3
#define RED    0x4
#define PURPLE 0x5
#define BROWN  0x6
#define GRAY   0x7
#define DARK_GRAY 0x8
#define LIGHT_BLUE 0x9
#define LIGHT_GREEN 0xA
#define LIGHT_CYAN 0xB
#define LIGHT_RED 0xC
#define LIGHT_PURPLE 0xD
#define YELLOW 0xE
#define WHITE  0xF

#define VIDEO_MEM_ADDR 0xB8000
#define MAX_X 80
#define MAX_Y 25

#define TXT_BLACK    0x00
#define TXT_BLUE     0x01
#define TXT_GREEN    0x02
#define TXT_CYAN     0x03
#define TXT_RED      0x04
#define TXT_MAGENTA  0x05
#define TXT_BROWN    0x06
#define TXT_LGRAY    0x07
#define TXT_DGRAY    0x08
#define TXT_LBLUE    0x09
#define TXT_LGREEN   0x0A
#define TXT_LCYAN    0x0B
#define TXT_LRED     0x0C
#define TXT_LMAGENTA 0x0D
#define TXT_YELLOW   0x0E
#define TXT_WHITE    0x0F

#define S_HLINE     196  // ─
#define S_VLINE     179  // │
#define S_TL        218  // ┌
#define S_TR        191  // ┐
#define S_BL        192  // └
#define S_BR        217  // ┘
#define D_HLINE     205  // ═
#define D_VLINE     186  // ║
#define D_TL        201  // ╔
#define D_TR        187  // ╗
#define D_BL        200  // ╚
#define D_BR        188  // ╝
#define BLOCK       219  // █
#define BLOCK_LIGHT 176  // ▓
#define BULLET      7    // •

static inline void put_pixel(int x, int y, uint8_t bg, uint8_t fg, uint8_t ch) {
    if (x < 0 || x >= MAX_X || y < 0 || y >= MAX_Y) return;
    volatile uint16_t* where = (volatile uint16_t*)VIDEO_MEM_ADDR + (y * MAX_X + x);
    uint16_t attr = (uint16_t)((bg << 4) | fg);
    *where = (uint16_t)(ch | (attr << 8));
}

static inline void put_block(int x, int y, uint8_t bg, uint8_t ch) {
    put_pixel(x, y, bg, TXT_WHITE, ch);
}

static inline void draw_pixel(int x, int y, int color) {
    if(x >= 0 && x < 80 && y >= 0 && y < 25) {
        uint16_t* video = (uint16_t*)0xB8000;
        video[y * 80 + x] = (color << 12) | (0x0F << 8) | ' ';
    }
}

static inline void draw_square(int x, int y, int size, int color) {
    for(int i = 0; i < size; i++) {
        for(int j = 0; j < size; j++) {
            draw_pixel(x + j, y + i, color);
        }
    }
}

static inline void draw_text_box(int x, int y, const char* text, int bg_color, int text_color) {
    int len = 0;
    while(text[len]) len++;
    
    for(int i = -1; i <= len; i++) {
        draw_pixel(x + i, y - 1, bg_color);
        draw_pixel(x + i, y + 1, bg_color);
    }
    draw_pixel(x - 2, y, bg_color);
    draw_pixel(x + len + 1, y, bg_color);
    
    kprint_at(text, x, y, (bg_color << 4) | text_color);
}

static inline void clear_area(int x, int y, int w, int h) {
    for(int i = 0; i < h; i++) {
        for(int j = 0; j < w; j++) {
            draw_pixel(x + j, y + i, BLACK);
        }
    }
}

static inline void draw_smiley(int x, int y) {
    draw_pixel(x, y, YELLOW);
    draw_pixel(x-1, y-1, YELLOW);
    draw_pixel(x+1, y-1, YELLOW);
    draw_pixel(x, y+1, YELLOW);
}

static inline void draw_heart(int x, int y) {
    draw_pixel(x-1, y-1, RED);
    draw_pixel(x+1, y-1, RED);
    draw_pixel(x-2, y, RED);
    draw_pixel(x, y, RED);
    draw_pixel(x+2, y, RED);
    draw_pixel(x-1, y+1, RED);
    draw_pixel(x+1, y+1, RED);
    draw_pixel(x, y+2, RED);
}

static inline void draw_blink_text(const char* text, int x, int y, int color) {
    static int blink = 0;
    blink = !blink;
    if(blink) {
        kprint_at(text, x, y, (BLACK << 4) | color);
    } else {
        kprint_at(text, x, y, (color << 4) | BLACK);
    }
}

static inline void draw_loading_bar(int x, int y, int percent) {
    int width = 30;
    int fill = (percent * width) / 100;
    
    for(int i = 0; i < width + 2; i++) {
        draw_pixel(x + i, y - 1, GRAY);
        draw_pixel(x + i, y + 1, GRAY);
    }
    draw_pixel(x - 1, y, GRAY);
    draw_pixel(x + width + 1, y, GRAY);
    
    for(int i = 0; i < fill; i++) {
        draw_pixel(x + i, y, GREEN);
    }
}

static inline void move_cursor(int x, int y) {
    uint16_t pos = y * 80 + x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static inline void hide_cursor() {
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

static inline void show_cursor() {
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x0E);
}

static inline void clear_screen_bg(uint8_t bg) {
    for (int y = 0; y < MAX_Y; y++) {
        for (int x = 0; x < MAX_X; x++) {
            put_pixel(x, y, bg, TXT_WHITE, ' ');
        }
    }
}

static inline void draw_hline(int x, int y, int len, uint8_t bg, uint8_t fg, uint8_t ch) {
    for(int i = 0; i < len; i++) {
        put_pixel(x + i, y, bg, fg, ch);
    }
}

static inline void draw_vline(int x, int y, int len, uint8_t bg, uint8_t fg, uint8_t ch) {
    for(int i = 0; i < len; i++) {
        put_pixel(x, y + i, bg, fg, ch);
    }
}

static inline void draw_frame(int x, int y, int w, int h, uint8_t bg, uint8_t fg) {
    if (w < 2 || h < 2) return;
    
    draw_hline(x + 1, y, w - 2, bg, fg, S_HLINE);
    draw_hline(x + 1, y + h - 1, w - 2, bg, fg, S_HLINE);
    draw_vline(x, y + 1, h - 2, bg, fg, S_VLINE);
    draw_vline(x + w - 1, y + 1, h - 2, bg, fg, S_VLINE);
    
    put_pixel(x, y, bg, fg, S_TL);
    put_pixel(x + w - 1, y, bg, fg, S_TR);
    put_pixel(x, y + h - 1, bg, fg, S_BL);
    put_pixel(x + w - 1, y + h - 1, bg, fg, S_BR);
}

static inline void draw_dframe(int x, int y, int w, int h, uint8_t bg, uint8_t fg) {
    if (w < 2 || h < 2) return;
    
    draw_hline(x + 1, y, w - 2, bg, fg, D_HLINE);
    draw_hline(x + 1, y + h - 1, w - 2, bg, fg, D_HLINE);
    draw_vline(x, y + 1, h - 2, bg, fg, D_VLINE);
    draw_vline(x + w - 1, y + 1, h - 2, bg, fg, D_VLINE);
    
    put_pixel(x, y, bg, fg, D_TL);
    put_pixel(x + w - 1, y, bg, fg, D_TR);
    put_pixel(x, y + h - 1, bg, fg, D_BL);
    put_pixel(x + w - 1, y + h - 1, bg, fg, D_BR);
}

static inline void draw_title_frame(int x, int y, int w, int h, const char* title, uint8_t bg, uint8_t fg) {
    draw_dframe(x, y, w, h, bg, fg);
    
    int len = 0;
    while(title[len]) len++;
    int tx = x + (w - len) / 2;
    kprint_at(title, tx, y, (bg << 4) | fg);
}

static inline void draw_shadow_window(int x, int y, int w, int h, uint8_t bg, uint8_t fg, const char* title) {
    for(int i = 1; i < h; i++) {
        put_pixel(x + w, y + i + 1, BLACK, TXT_BLACK, ' ');
    }
    for(int i = 1; i <= w; i++) {
        put_pixel(x + i, y + h, BLACK, TXT_BLACK, ' ');
    }
    
    draw_title_frame(x, y, w, h, title, bg, fg);
    
    for(int i = y + 1; i < y + h - 1; i++) {
        for(int j = x + 1; j < x + w - 1; j++) {
            put_pixel(j, i, bg, fg, ' ');
        }
    }
}

static inline void draw_progress(int x, int y, int w, int percent, uint8_t bg, uint8_t fg) {
    draw_frame(x, y, w + 2, 3, bg, fg);
    
    int fill = (percent * w) / 100;
    for(int i = 0; i < fill; i++) {
        put_pixel(x + 1 + i, y + 1, bg, fg, BLOCK);
    }
    for(int i = fill; i < w; i++) {
        put_pixel(x + 1 + i, y + 1, bg, fg, BLOCK_LIGHT);
    }
}

static inline void draw_button(int x, int y, int w, const char* text, uint8_t bg, uint8_t fg, int pressed) {
    if(pressed) {
        draw_hline(x, y, w, bg, fg, S_HLINE);
        draw_hline(x, y + 2, w, bg, fg, S_HLINE);
        put_pixel(x, y, bg, fg, S_TL);
        put_pixel(x + w - 1, y, bg, fg, S_TR);
        put_pixel(x, y + 2, bg, fg, S_BL);
        put_pixel(x + w - 1, y + 2, bg, fg, S_BR);
        
        for(int i = 1; i < w - 1; i++) {
            put_pixel(x + i, y + 1, bg, fg, ' ');
        }
    } else {
        draw_hline(x, y, w, bg, fg, S_HLINE);
        draw_hline(x, y + 2, w, bg, fg, S_HLINE);
        put_pixel(x, y, bg, fg, S_TL);
        put_pixel(x + w - 1, y, bg, fg, S_TR);
        put_pixel(x, y + 2, bg, fg, S_BL);
        put_pixel(x + w - 1, y + 2, bg, fg, S_BR);
        
        draw_vline(x + 1, y + 1, 1, bg, fg, S_VLINE);
        draw_vline(x + w - 2, y + 1, 1, bg, fg, S_VLINE);
        
        for(int i = 2; i < w - 2; i++) {
            put_pixel(x + i, y + 1, bg, fg, ' ');
        }
    }
    
    int len = 0;
    while(text[len]) len++;
    int tx = x + (w - len) / 2;
    kprint_at(text, tx, y + 1, (bg << 4) | fg);
}

typedef struct {
    const char* text;
    void (*callback)();
    uint8_t enabled;
} MenuItem;

static inline void draw_menu(int x, int y, MenuItem* items, int count, int selected, uint8_t bg, uint8_t fg) {
    draw_dframe(x - 2, y - 1, 12, count + 1, bg, fg);
    
    for(int i = 0; i < count; i++) {
        uint8_t color = (i == selected) ? ((fg << 4) | (bg & 0x0F)) : ((bg << 4) | fg);
        kprint_at(items[i].text, x, y + i, color);
        
        if(!items[i].enabled) {
            kprint_at(" [X]", x + 8, y + i, (bg << 4) | TXT_RED);
        }
    }
}

static inline void draw_listbox(int x, int y, int w, int h, const char** items, int count, int selected, int scroll, uint8_t bg, uint8_t fg) {
    draw_dframe(x, y, w, h + 2, bg, fg);
    
    for(int i = 0; i < h && i + scroll < count; i++) {
        uint8_t color = (i + scroll == selected) ? ((WHITE << 4) | TXT_BLACK) : ((bg << 4) | fg);
        kprint_at(items[i + scroll], x + 1, y + 1 + i, color);
    }
}

static inline void draw_table_cell(int x, int y, int w, const char* text, uint8_t bg, uint8_t fg) {
    put_pixel(x, y, bg, fg, S_VLINE);
    kprint_at(text, x + 1, y, (bg << 4) | fg);
    put_pixel(x + w, y, bg, fg, S_VLINE);
}

static inline void draw_icon_folder(int x, int y, uint8_t bg, uint8_t fg) {
    put_pixel(x, y, bg, fg, '[');
    put_pixel(x + 1, y, bg, fg, ']');
    put_pixel(x + 2, y, bg, fg, 192);
    put_pixel(x + 3, y, bg, fg, 196);
    put_pixel(x + 4, y, bg, fg, 196);
    put_pixel(x + 5, y, bg, fg, 217);
}

static inline void draw_icon_file(int x, int y, uint8_t bg, uint8_t fg) {
    put_pixel(x, y, bg, fg, 201);
    put_pixel(x + 1, y, bg, fg, 205);
    put_pixel(x + 2, y, bg, fg, 187);
    put_pixel(x, y + 1, bg, fg, 186);
    put_pixel(x + 2, y + 1, bg, fg, 186);
    put_pixel(x, y + 2, bg, fg, 200);
    put_pixel(x + 1, y + 2, bg, fg, 205);
    put_pixel(x + 2, y + 2, bg, fg, 188);
}

static inline void draw_statusbar(const char* left, const char* right, uint8_t bg, uint8_t fg) {
    for(int i = 0; i < MAX_X; i++) {
        put_pixel(i, MAX_Y - 1, bg, fg, ' ');
    }
    
    kprint_at(left, 1, MAX_Y - 1, (bg << 4) | fg);
    
    int len = 0;
    while(right[len]) len++;
    kprint_at(right, MAX_X - len - 1, MAX_Y - 1, (bg << 4) | fg);
}

static inline void draw_taskbar(int y, uint8_t bg, uint8_t fg) {
    for(int i = 0; i < MAX_X; i++) {
        put_pixel(i, y, bg, fg, ' ');
    }
    draw_button(1, y, 6, "Start", bg, fg, 0);
}

static inline int dialog_yesno(const char* title, const char* msg, uint8_t bg) {
    int x = 20, y = 8, w = 40, h = 8;
    
    draw_shadow_window(x, y, w, h, bg, TXT_WHITE, title);
    kprint_at(msg, x + 2, y + 3, (bg << 4) | TXT_WHITE);
    
    draw_button(x + 10, y + 5, 8, "Yes", bg, TXT_GREEN, 0);
    draw_button(x + 22, y + 5, 8, "No", bg, TXT_RED, 0);
    
    while(1) {
        uint8_t key = inb(0x60);
        if(key == 0x4B) return 0;
        if(key == 0x4D) return 1;
        if(key == 0x1C) return 0;
        if(key == 0x01) return 1;
        
        for(volatile int i = 0; i < 10000; i++);
    }
}

static inline void draw_window(int x, int y, int w, int h, uint8_t color) {
    draw_frame(x, y, w, h, color, TXT_WHITE);
}

static inline void draw_window_with_shadow(int x, int y, int w, int h, uint8_t color) {
    draw_shadow_window(x, y, w, h, color, TXT_WHITE, "");
}

static inline void draw_box(int x, int y, int w, int h, uint8_t color) {
    draw_frame(x, y, w, h, color, TXT_WHITE);
}

static inline void clear_text_graph(uint8_t color) {
    clear_screen_bg(color);
}

static inline void draw_house(int x, int y) {
    for(int i = 0; i < 5; i++) {
        for(int j = 0; j < 5; j++) {
            draw_pixel(x + j, y + i, BROWN);
        }
    }
    draw_pixel(x + 2, y - 2, RED);
    draw_pixel(x + 1, y - 1, RED);
    draw_pixel(x + 3, y - 1, RED);
    draw_pixel(x + 2, y + 2, DARK_GRAY);
    draw_pixel(x + 2, y + 3, DARK_GRAY);
}

static inline void draw_tree(int x, int y) {
    for(int i = 0; i < 4; i++) {
        draw_pixel(x, y + i, BROWN);
    }
    for(int i = -2; i <= 2; i++) {
        for(int j = -2; j <= 0; j++) {
            if(i*i + j*j <= 5) {
                draw_pixel(x + i, y - 4 + j, GREEN);
            }
        }
    }
}

static inline void draw_car(int x, int y) {
    for(int i = 0; i < 6; i++) {
        draw_pixel(x + i, y, BLUE);
        draw_pixel(x + i, y - 1, BLUE);
    }
    draw_pixel(x + 1, y + 1, BLACK);
    draw_pixel(x + 4, y + 1, BLACK);
}

static inline void draw_help() {
    clear_screen_bg(BLACK);
    kprint_at("=== GRAPH.H FUNCTIONS ===", 25, 2, (BLACK << 4) | YELLOW);
    kprint_at("draw_pixel(x,y,color)", 5, 4, (BLACK << 4) | WHITE);
    kprint_at("draw_square(x,y,size,color)", 5, 5, (BLACK << 4) | WHITE);
    kprint_at("draw_text_box(x,y,text,bg,fg)", 5, 6, (BLACK << 4) | WHITE);
    kprint_at("draw_smiley(x,y)", 5, 7, (BLACK << 4) | WHITE);
    kprint_at("draw_heart(x,y)", 5, 8, (BLACK << 4) | WHITE);
    kprint_at("draw_house(x,y)", 5, 9, (BLACK << 4) | WHITE);
    kprint_at("draw_tree(x,y)", 5, 10, (BLACK << 4) | WHITE);
    kprint_at("draw_car(x,y)", 5, 11, (BLACK << 4) | WHITE);
    kprint_at("draw_loading_bar(x,y,percent)", 5, 12, (BLACK << 4) | WHITE);
    kprint_at("clear_area(x,y,w,h)", 5, 13, (BLACK << 4) | WHITE);
    kprint_at("=== COLORS ===", 30, 15, (BLACK << 4) | CYAN);
    kprint_at("BLACK, BLUE, GREEN, CYAN, RED, PURPLE", 20, 16, (BLACK << 4) | WHITE);
    kprint_at("BROWN, GRAY, YELLOW, WHITE, etc.", 22, 17, (BLACK << 4) | WHITE);
}

static inline int button_click(int x, int y, int w, const char* text, uint8_t bg, uint8_t fg, int* last_click) {
    int hover = mouse_over(x, y, w, 2);
    
    if(hover) {
        draw_button(x, y, w, text, bg, fg, 1);
    } else {
        draw_button(x, y, w, text, bg, fg, 0);
    }
    
    if(hover && (mouse_btn & 1) && !*last_click) {
        *last_click = 1;
        return 1;
    }
    if(!(mouse_btn & 1)) *last_click = 0;
    return 0;
}

static inline int checkbox(int x, int y, int checked, const char* text, int* last_click) {
    if(checked) {
        put_pixel(x, y, WHITE, BLACK, '[');
        put_pixel(x+1, y, WHITE, BLACK, 'X');
        put_pixel(x+2, y, WHITE, BLACK, ']');
    } else {
        put_pixel(x, y, WHITE, BLACK, '[');
        put_pixel(x+1, y, WHITE, BLACK, ' ');
        put_pixel(x+2, y, WHITE, BLACK, ']');
    }
    
    kprint_at(text, x+4, y, (WHITE << 4) | BLACK);
    
    if(mouse_over(x, y, 3, 1) && (mouse_btn & 1) && !*last_click) {
        *last_click = 1;
        return !checked;
    }
    if(!(mouse_btn & 1)) *last_click = 0;
    return checked;
}

static inline int radio(int x, int y, int selected, const char* text, int* last_click) {
    if(selected) {
        put_pixel(x, y, WHITE, BLACK, '(');
        put_pixel(x+1, y, WHITE, BLACK, '*');
        put_pixel(x+2, y, WHITE, BLACK, ')');
    } else {
        put_pixel(x, y, WHITE, BLACK, '(');
        put_pixel(x+1, y, WHITE, BLACK, ' ');
        put_pixel(x+2, y, WHITE, BLACK, ')');
    }
    
    kprint_at(text, x+4, y, (WHITE << 4) | BLACK);
    
    if(mouse_over(x, y, 3, 1) && (mouse_btn & 1) && !*last_click) {
        *last_click = 1;
        return 1;
    }
    if(!(mouse_btn & 1)) *last_click = 0;
    return selected;
}

static inline int slider(int x, int y, int w, int value, int min, int max, int* last_click) {
    draw_hline(x, y, w, GRAY, TXT_WHITE, S_HLINE);
    draw_hline(x, y+2, w, GRAY, TXT_WHITE, S_HLINE);
    draw_vline(x-1, y, 3, GRAY, TXT_WHITE, S_VLINE);
    draw_vline(x+w, y, 3, GRAY, TXT_WHITE, S_VLINE);
    
    int pos = x + (value * w) / (max - min);
    draw_vline(pos, y-1, 3, WHITE, TXT_BLACK, BLOCK);
    
    if(mouse_over(pos-1, y-1, 3, 3) && (mouse_btn & 1) && !*last_click) {
        *last_click = 1;
        return value;
    }
    if((mouse_btn & 1) && *last_click) {
        int new_pos = mouse_x - x;
        if(new_pos < 0) new_pos = 0;
        if(new_pos > w) new_pos = w;
        return min + (new_pos * (max - min)) / w;
    }
    if(!(mouse_btn & 1)) *last_click = 0;
    return value;
}

static inline int dialog_3way(const char* title, const char* msg) {
    int x = 15, y = 7, w = 50, h = 10;
    
    clear_screen_bg(BLACK);
    draw_shadow_window(x, y, w, h, BLUE, TXT_WHITE, title);
    kprint_at(msg, x+2, y+3, (BLUE << 4) | TXT_WHITE);
    
    int result = -1;
    int last_click = 0;
    
    while(result == -1) {
        poll_mouse();
        
        if(button_click(x+5, y+6, 10, "Yes", BLUE, TXT_GREEN, &last_click)) result = 0;
        if(button_click(x+20, y+6, 8, "No", BLUE, TXT_RED, &last_click)) result = 1;
        if(button_click(x+33, y+6, 12, "Cancel", BLUE, TXT_WHITE, &last_click)) result = 2;
        
        if(inb(0x64) & 1) {
            uint8_t key = inb(0x60);
            if(key == 0x1C) result = 0;
            if(key == 0x01) result = 2;
        }
        
        draw_mouse();
        move_cursor(79, 24);
        for(volatile int i = 0; i < 2000; i++);
    }
    
    return result;
}

static inline void dialog_input(const char* title, char* buffer, int max_len) {
    int x = 15, y = 7, w = 50, h = 8;
    int pos = 0;
    int last_click = 0;
    int running = 1;
    
    clear_screen_bg(BLACK);
    draw_shadow_window(x, y, w, h, BLUE, TXT_WHITE, title);
    kprint_at("Enter text:", x+2, y+3, (BLUE << 4) | TXT_WHITE);
    
    while(running) {
        poll_mouse();
        
        draw_frame(x+2, y+4, 30, 1, GRAY, TXT_WHITE);
        kprint_at(buffer, x+3, y+4, (GRAY << 4) | TXT_BLACK);
        
        if(button_click(x+35, y+4, 8, "OK", BLUE, TXT_GREEN, &last_click)) {
            running = 0;
        }
        
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc < 0x80) {
                char ch = 0;
                if(sc == 0x0E && pos > 0) {
                    pos--;
                    buffer[pos] = '\0';
                }
                else if(sc >= 0x02 && sc <= 0x0D) {
                    ch = "1234567890-="[sc - 0x02];
                }
                else if(sc >= 0x10 && sc <= 0x19) {
                    ch = "qwertyuiop"[sc - 0x10];
                }
                else if(sc >= 0x1E && sc <= 0x26) {
                    ch = "asdfghjkl"[sc - 0x1E];
                }
                else if(sc >= 0x2C && sc <= 0x32) {
                    ch = "zxcvbnm"[sc - 0x2C];
                }
                else if(sc == 0x39) ch = ' ';
                
                if(ch && pos < max_len-1) {
                    buffer[pos++] = ch;
                    buffer[pos] = '\0';
                }
            }
        }
        
        draw_mouse();
        move_cursor(79, 24);
        for(volatile int i = 0; i < 2000; i++);
    }
}

static inline int simple_menu(const char* title, const char* items[], int count) {
    int x = 20, y = 5;
    int selected = 0;
    int last_click = 0;
    int running = 1;
    
    clear_screen_bg(BLACK);
    draw_shadow_window(x-2, y-2, 40, count+4, BLUE, TXT_WHITE, title);
    
    while(running) {
        poll_mouse();
        
        for(int i = 0; i < count; i++) {
            int hover = mouse_over(x, y+i, 30, 1);
            uint8_t color = (i == selected || hover) ? TXT_YELLOW : TXT_WHITE;
            kprint_at(items[i], x, y+i, (BLUE << 4) | color);
        }
        
        for(int i = 0; i < count; i++) {
            if(mouse_over(x, y+i, 30, 1) && (mouse_btn & 1) && !last_click) {
                last_click = 1;
                return i;
            }
        }
        if(!(mouse_btn & 1)) last_click = 0;
        
        if(inb(0x64) & 1) {
            uint8_t key = inb(0x60);
            if(key == 0x48) selected = (selected - 1 + count) % count;
            if(key == 0x50) selected = (selected + 1) % count;
            if(key == 0x1C) return selected;
            if(key == 0x01) return -1;
        }
        
        draw_mouse();
        move_cursor(79, 24);
        for(volatile int i = 0; i < 2000; i++);
    }
    return -1;
}

static inline void demo_calculator() {
    clear_screen_bg(BLACK);
    draw_shadow_window(15, 3, 50, 18, BLUE, TXT_WHITE, "DEMO CALCULATOR");
    
    int last_click = 0;
    int running = 1;
    
    while(running) {
        poll_mouse();
        
        kprint_at("Use buttons above", 20, 8, (BLUE << 4) | TXT_WHITE);
        
        if(button_click(20, 12, 5, "7", BLUE, TXT_WHITE, &last_click)) {}
        if(button_click(26, 12, 5, "8", BLUE, TXT_WHITE, &last_click)) {}
        if(button_click(32, 12, 5, "9", BLUE, TXT_WHITE, &last_click)) {}
        if(button_click(38, 12, 5, "+", BLUE, TXT_GREEN, &last_click)) {}
        
        if(button_click(20, 14, 5, "4", BLUE, TXT_WHITE, &last_click)) {}
        if(button_click(26, 14, 5, "5", BLUE, TXT_WHITE, &last_click)) {}
        if(button_click(32, 14, 5, "6", BLUE, TXT_WHITE, &last_click)) {}
        if(button_click(38, 14, 5, "-", BLUE, TXT_GREEN, &last_click)) {}
        
        if(button_click(20, 16, 5, "1", BLUE, TXT_WHITE, &last_click)) {}
        if(button_click(26, 16, 5, "2", BLUE, TXT_WHITE, &last_click)) {}
        if(button_click(32, 16, 5, "3", BLUE, TXT_WHITE, &last_click)) {}
        if(button_click(38, 16, 5, "*", BLUE, TXT_GREEN, &last_click)) {}
        
        if(button_click(20, 18, 5, "0", BLUE, TXT_WHITE, &last_click)) {}
        if(button_click(26, 18, 5, "C", BLUE, TXT_RED, &last_click)) {}
        if(button_click(32, 18, 5, "=", BLUE, TXT_GREEN, &last_click)) {}
        if(button_click(38, 18, 5, "/", BLUE, TXT_GREEN, &last_click)) {}
        
        if(button_click(50, 20, 10, "Exit", BLUE, TXT_RED, &last_click)) {
            running = 0;
        }
        
        draw_mouse();
        move_cursor(79, 24);
        for(volatile int i = 0; i < 2000; i++);
    }
}

static inline void demo_colors() {
    clear_screen_bg(BLACK);
    draw_shadow_window(10, 2, 60, 20, BLUE, TXT_WHITE, "COLOR PALETTE");
    
    kprint_at("Click on any color:", 25, 4, (BLUE << 4) | TXT_WHITE);
    
    const char* color_names[] = {
        "BLACK", "BLUE", "GREEN", "CYAN", "RED", "PURPLE",
        "BROWN", "GRAY", "DARK_GRAY", "LIGHT_BLUE", "LIGHT_GREEN",
        "LIGHT_CYAN", "LIGHT_RED", "LIGHT_PURPLE", "YELLOW", "WHITE"
    };
    
    int colors[] = {
        BLACK, BLUE, GREEN, CYAN, RED, PURPLE,
        BROWN, GRAY, DARK_GRAY, LIGHT_BLUE, LIGHT_GREEN,
        LIGHT_CYAN, LIGHT_RED, LIGHT_PURPLE, YELLOW, WHITE
    };
    
    int last_click = 0;
    int running = 1;
    int selected_color = BLACK;
    
    while(running) {
        poll_mouse();
        
        for(int i = 0; i < 16; i++) {
            int cx = 15 + (i % 4) * 12;
            int cy = 7 + (i / 4) * 2;
            
            for(int dx = 0; dx < 8; dx++) {
                put_pixel(cx + dx, cy, colors[i], TXT_WHITE, ' ');
            }
            
            kprint_at(color_names[i], cx, cy+1, (BLUE << 4) | TXT_WHITE);
            
            if(mouse_over(cx, cy, 8, 1) && (mouse_btn & 1) && !last_click) {
                selected_color = colors[i];
                last_click = 1;
            }
        }
        
        if(!(mouse_btn & 1)) last_click = 0;
        
        kprint_at("Selected: ", 25, 18, (BLUE << 4) | TXT_WHITE);
        for(int i = 0; i < 8; i++) {
            put_pixel(35 + i, 18, selected_color, TXT_WHITE, ' ');
        }
        
        if(button_click(35, 20, 10, "Exit", BLUE, TXT_RED, &last_click)) {
            running = 0;
        }
        
        draw_mouse();
        move_cursor(79, 24);
        for(volatile int i = 0; i < 2000; i++);
    }
}

static inline int dialog_custom(const char* title, const char* msg, 
                                const char* btn1, const char* btn2, const char* btn3) {
    int x = 15, y = 7, w = 50, h = 10;
    int result = -1;
    int last_click = 0;
    
    clear_screen_bg(BLACK);
    draw_shadow_window(x, y, w, h, BLUE, TXT_WHITE, title);
    kprint_at(msg, x+2, y+3, (BLUE << 4) | TXT_WHITE);
    
    while(result == -1) {
        poll_mouse();
        
        if(btn1[0] && button_click(x+5, y+6, 10, btn1, BLUE, TXT_GREEN, &last_click)) result = 0;
        if(btn2[0] && button_click(x+20, y+6, 10, btn2, BLUE, TXT_RED, &last_click)) result = 1;
        if(btn3[0] && button_click(x+35, y+6, 10, btn3, BLUE, TXT_WHITE, &last_click)) result = 2;
        
        draw_mouse();
        move_cursor(79, 24);
        for(volatile int i = 0; i < 2000; i++);
    }
    
    return result;
}

static inline int ui_dialog(const char* title, const char* msg, uint8_t bg) {
    int x = 20, y = 8, w = 40, h = 8;
    
    draw_shadow_window(x, y, w, h, bg, TXT_WHITE, title);
    kprint_at(msg, x + 2, y + 3, (bg << 4) | TXT_WHITE);
    
    int selection = 0;
    int last_btn = 0;
    
    while(1) {
        draw_button(x + 10, y + 5, 8, "Yes", bg, selection == 0 ? TXT_GREEN : TXT_WHITE, 0);
        draw_button(x + 22, y + 5, 8, "No", bg, selection == 1 ? TXT_RED : TXT_WHITE, 0);
        
        if(inb(0x64) & 1) {
            uint8_t key = inb(0x60);
            if(key == 0x4B) selection = 0;
            if(key == 0x4D) selection = 1;
            if(key == 0x1C) return selection;
            if(key == 0x01) return 1;
        }
        
        poll_mouse();
        if((mouse_btn & 1) && !last_btn) {
            if(mouse_over(x + 10, y + 5, 8, 2)) return 0;
            if(mouse_over(x + 22, y + 5, 8, 2)) return 1;
        }
        last_btn = mouse_btn & 1;
        
        draw_mouse();
        move_cursor(79, 24);
        
        for(volatile int i = 0; i < 5000; i++);
    }
}
static inline void set_statusbar_colors(uint8_t bg, uint8_t fg) {
    (void)bg; (void)fg;
}

static inline void set_window_colors(uint8_t bg, uint8_t accent) {
    (void)bg; (void)accent;
}

static inline void set_button_colors(uint8_t highlight, uint8_t warning) {
    (void)highlight; (void)warning;
}

#endif