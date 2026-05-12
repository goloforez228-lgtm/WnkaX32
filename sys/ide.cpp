#include "graph.h"
#include "tcc.h"
#include "kernel_stubs.h"
#include <stdint.h>

#define NULL 0
#define MAX_LINES 100
#define MAX_LINE_LEN 80
#define EDITOR_X 2
#define EDITOR_Y 3
#define EDITOR_W 76
#define EDITOR_H 18

static char lines[MAX_LINES][MAX_LINE_LEN];
static int line_count = 3;
static int cursor_line = 0;
static int cursor_col = 0;
static int scroll = 0;
static int running = 1;
static int last_cursor_line = -1;
static int last_cursor_col = -1;
static int shift_pressed = 0;

static int my_strlen(const char* s) {
    int len = 0;
    while(s[len]) len++;
    return len;
}

static int my_strcmp(const char* s1, const char* s2) {
    while(*s1 && *s2 && *s1 == *s2) { s1++; s2++; }
    return *s1 - *s2;
}

static int my_strncmp(const char* s1, const char* s2, int n) {
    for(int i = 0; i < n; i++) {
        if(s1[i] != s2[i]) return s1[i] - s2[i];
        if(s1[i] == '\0') return 0;
    }
    return 0;
}

static void my_strcpy(char* dest, const char* src) {
    while(*src) *dest++ = *src++;
    *dest = '\0';
}

static void clear_line(int line) {
    for(int i = 0; i < MAX_LINE_LEN; i++) lines[line][i] = '\0';
}

static void draw_char(int x, int y, char ch, int color) {
    char s[2] = {ch, '\0'};
    kprint_at(s, x, y, color);
}
static int is_keyword(const char* word, int len) {
    if(len == 3 && my_strncmp(word, "int", 3) == 0) return 1;
    if(len == 4 && my_strncmp(word, "char", 4) == 0) return 1;
    if(len == 5 && my_strncmp(word, "while", 5) == 0) return 1;
    if(len == 3 && my_strncmp(word, "for", 3) == 0) return 1;
    if(len == 6 && my_strncmp(word, "return", 6) == 0) return 1;
    return 0;
}

static int is_operator(char c) {
    return c == '{' || c == '}' || c == '(' || c == ')' || 
           c == '[' || c == ']' || c == ';' || c == '=' ||
           c == '+' || c == '-' || c == '*' || c == '/' ||
           c == ':' || c == '<' || c == '>' || c == '|' ||
           c == '&' || c == '!' || c == '%' || c == '^' ||
           c == '~' || c == '?' || c == '@' || c == '#';
}

static void draw_colored_line(const char* line, int x, int y) {
    int i = 0;
    while(line[i] && i < EDITOR_W) {
        if(line[i] == ' ') {
            draw_char(x + i, y, ' ', 0x07);
            i++;
            continue;
        }
        
        if((line[i] >= 'a' && line[i] <= 'z') || (line[i] >= 'A' && line[i] <= 'Z') || line[i] == '_') {
            int start = i;
            while((line[i] >= 'a' && line[i] <= 'z') || (line[i] >= 'A' && line[i] <= 'Z') || 
                  (line[i] >= '0' && line[i] <= '9') || line[i] == '_') i++;
            int len = i - start;
            char word[32];
            for(int j = 0; j < len; j++) word[j] = line[start + j];
            word[len] = '\0';
            
            int color = 0x0F;
            if(is_keyword(word, len)) color = 0x0A;
            else if(my_strcmp(word, "main") == 0) color = 0x0E;
            
            for(int j = 0; j < len; j++) {
                draw_char(x + start + j, y, line[start + j], color);
            }
            continue;
        }
        
        if(line[i] >= '0' && line[i] <= '9') {
            int start = i;
            while(line[i] >= '0' && line[i] <= '9') i++;
            for(int j = start; j < i; j++) {
                draw_char(x + j, y, line[j], 0x0B);
            }
            continue;
        }
        
        if(is_operator(line[i])) {
            int color = (line[i] == '{' || line[i] == '}') ? 0x0E : 0x0C;
            draw_char(x + i, y, line[i], color);
            i++;
            continue;
        }
        
        draw_char(x + i, y, line[i], 0x07);
        i++;
    }
}

static void redraw_line(int line_num) {
    int y = EDITOR_Y + (line_num - scroll);
    if(y >= EDITOR_Y && y < EDITOR_Y + EDITOR_H) {
        for(int x = 0; x < EDITOR_W; x++) {
            draw_char(EDITOR_X + x, y, ' ', 0x07);
        }
        draw_colored_line(lines[line_num], EDITOR_X, y);
    }
}

static void insert_char(char c) {
    int len = my_strlen(lines[cursor_line]);
    if(len >= MAX_LINE_LEN - 1) return;
    for(int i = len; i >= cursor_col; i--) {
        lines[cursor_line][i + 1] = lines[cursor_line][i];
    }
    lines[cursor_line][cursor_col] = c;
    cursor_col++;
}

static void delete_char(void) {
    if(cursor_col > 0) {
        int len = my_strlen(lines[cursor_line]);
        for(int i = cursor_col - 1; i < len; i++) {
            lines[cursor_line][i] = lines[cursor_line][i + 1];
        }
        cursor_col--;
    } else if(cursor_line > 0) {
        int above_len = my_strlen(lines[cursor_line - 1]);
        int current_len = my_strlen(lines[cursor_line]);
        if(above_len + current_len < MAX_LINE_LEN) {
            for(int i = 0; i <= current_len; i++) {
                lines[cursor_line - 1][above_len + i] = lines[cursor_line][i];
            }
            for(int i = cursor_line; i < line_count - 1; i++) {
                my_strcpy(lines[i], lines[i + 1]);
            }
            clear_line(line_count - 1);
            line_count--;
            cursor_line--;
            cursor_col = above_len;
        }
    }
}

static void newline(void) {
    if(line_count >= MAX_LINES) return;
    for(int i = line_count; i > cursor_line + 1; i--) {
        my_strcpy(lines[i], lines[i - 1]);
    }
    int len = my_strlen(lines[cursor_line]);
    char temp[MAX_LINE_LEN];
    my_strcpy(temp, lines[cursor_line] + cursor_col);
    lines[cursor_line][cursor_col] = '\0';
    clear_line(cursor_line + 1);
    my_strcpy(lines[cursor_line + 1], temp);
    line_count++;
    cursor_line++;
    cursor_col = 0;
}

static void clear_all(void) {
    for(int i = 0; i < MAX_LINES; i++) clear_line(i);
    line_count = 0;
    cursor_line = 0;
    cursor_col = 0;
    scroll = 0;
}

static void help_screen(void) {
    clear_screen();
    kprint_at("=== WNKA IDE HELP ===", 30, 2, 0x0F);
    kprint_at("ESC - Exit", 30, 5, 0x0A);
    kprint_at("F1 - Run program", 30, 6, 0x0A);
    kprint_at("F2 - Help", 30, 7, 0x0A);
    kprint_at("F3 - Clear all", 30, 8, 0x0A);
    kprint_at("Arrows - Move cursor", 30, 10, 0x0A);
    kprint_at("Shift + Arrows - Move 5 steps", 30, 11, 0x0A);
    kprint_at("Backspace - Delete", 30, 12, 0x0A);
    kprint_at("Enter - New line", 30, 13, 0x0A);
    kprint_at("Shift + numbers/symbols - Special chars", 30, 14, 0x0A);
    kprint_at("", 30, 16, 0x0F);
    kprint_at("=== TCC COMPILER ===", 30, 17, 0x0F);
    kprint_at("Supports: int, char, while, for, return", 30, 19, 0x0A);
    kprint_at("Press any key to continue...", 30, 23, 0x0E);
    
    while(!(inb(0x64) & 1));
    while(inb(0x64) & 1) inb(0x60);
}
static void run_code(void) {
    char buffer[4096] = {0};
    int pos = 0;
    for(int i = 0; i < line_count && pos < 4000; i++) {
        for(int j = 0; lines[i][j] && pos < 4000; j++) {
            buffer[pos++] = lines[i][j];
        }
        if(pos < 4000) buffer[pos++] = '\n';
    }
    
    kprint_at("Compiling...", 60, 24, 0x0E);
    
    TCCState* s = tcc_new();
    if(s) {
        if(tcc_compile_string(s, buffer) == 0) {
            if(tcc_relocate(s, NULL) == 0) {
                kprint_at("Running...    ", 60, 24, 0x0A);
                int result = tcc_run(s, 0, NULL);
                kprint_at("Result: ", 60, 24, 0x0F);
                kprint_int_at(result, 68, 24, 0x0E);
                kprint_at("     ", 75, 24, 0x07);
            } else {
                kprint_at("Relocate error", 60, 24, 0x0C);
            }
        } else {
            kprint_at("Compile error ", 60, 24, 0x0C);
        }
        tcc_delete(s);
    } else {
        kprint_at("TCC init error", 60, 24, 0x0C);
    }
}

static void draw_frame(void) {
    clear_screen();
    kprint_at("WNKA IDE - Text Editor", 30, 0, 0x0F);
    kprint_at("F1:Run  F2:Help  F3:Clear  ESC:Exit", 20, 1, 0x0E);
    draw_frame(EDITOR_X-1, EDITOR_Y-1, EDITOR_W+2, EDITOR_H+2, 0x07, 0x0F);
}

static void draw_status(void) {
    kprint_at("Line: ", 2, 23, 0x0A);
    kprint_int_at(cursor_line + 1, 8, 23, 0x0F);
    kprint_at(" Col: ", 15, 23, 0x0A);
    kprint_int_at(cursor_col + 1, 20, 23, 0x0F);
    kprint_at(" Lines: ", 30, 23, 0x0A);
    kprint_int_at(line_count, 37, 23, 0x0F);
    if(shift_pressed) {
        kprint_at(" SHIFT", 50, 23, 0x0E);
    }
}

static void draw_text(void) {
    int start = scroll;
    int end = start + EDITOR_H;
    if(end > line_count) end = line_count;
    for(int i = start; i < end; i++) {
        int y = EDITOR_Y + (i - start);
        for(int x = 0; x < EDITOR_W; x++) draw_char(EDITOR_X + x, y, ' ', 0x07);
        draw_colored_line(lines[i], EDITOR_X, y);
    }
    if(end < start + EDITOR_H) {
        for(int i = end; i < start + EDITOR_H; i++) {
            int y = EDITOR_Y + (i - start);
            for(int x = 0; x < EDITOR_W; x++) draw_char(EDITOR_X + x, y, ' ', 0x07);
        }
    }
}

static void update_cursor(void) {
    if(last_cursor_line >= 0 && last_cursor_line >= scroll && last_cursor_line < scroll + EDITOR_H) {
        int old_y = EDITOR_Y + (last_cursor_line - scroll);
        draw_colored_line(lines[last_cursor_line], EDITOR_X, old_y);
    }
    int cursor_y = EDITOR_Y + (cursor_line - scroll);
    char ch = lines[cursor_line][cursor_col];
    if(ch == '\0') ch = ' ';
    draw_char(EDITOR_X + cursor_col, cursor_y, ch, 0x70);
    last_cursor_line = cursor_line;
    last_cursor_col = cursor_col;
}

void ide(void) {
    my_strcpy(lines[0], "int main() {");
    my_strcpy(lines[1], "    return 42;");
    my_strcpy(lines[2], "}");
    line_count = 3;
    cursor_line = 0;
    cursor_col = 0;
    scroll = 0;
    running = 1;
    last_cursor_line = -1;
    shift_pressed = 0;
    
    draw_frame();
    draw_text();
    draw_status();
    update_cursor();
    
    while(running) {
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            int old_line = cursor_line;
            int old_col = cursor_col;
            int old_scroll = scroll;
            
            if(sc == 0x2A || sc == 0x36) {
                shift_pressed = 1;
            }
            else if(sc == 0xAA || sc == 0xB6) {
                shift_pressed = 0;
            }
            else if(sc == 0x01) { running = 0; }
            else if(sc == 0x3B) { run_code(); }
            else if(sc == 0x3C) { help_screen(); draw_frame(); draw_text(); draw_status(); update_cursor(); }
            else if(sc == 0x3D) { clear_all(); draw_text(); draw_status(); update_cursor(); }
            else if(sc == 0x0E) { delete_char(); }
            else if(sc == 0x1C) { newline(); }
            else if(sc == 0x4B) { 
                int step = shift_pressed ? 5 : 1;
                for(int s = 0; s < step && cursor_col > 0; s++) cursor_col--;
            }
            else if(sc == 0x4D) { 
                int step = shift_pressed ? 5 : 1;
                int len = my_strlen(lines[cursor_line]);
                for(int s = 0; s < step && cursor_col < len; s++) cursor_col++;
            }
            else if(sc == 0x48) { 
                int step = shift_pressed ? 5 : 1;
                for(int s = 0; s < step && cursor_line > 0; s++) {
                    cursor_line--;
                    int len = my_strlen(lines[cursor_line]);
                    if(cursor_col > len) cursor_col = len;
                }
            }
            else if(sc == 0x50) { 
                int step = shift_pressed ? 5 : 1;
                for(int s = 0; s < step && cursor_line < line_count - 1; s++) {
                    cursor_line++;
                    int len = my_strlen(lines[cursor_line]);
                    if(cursor_col > len) cursor_col = len;
                }
            }
            else if(sc >= 0x02 && sc <= 0x0D) { 
                if(shift_pressed) {
                    const char* shift_chars = "!@#$%^&*()_+";
                    insert_char(shift_chars[sc - 0x02]);
                } else {
                    const char* chars = "1234567890-=";
                    insert_char(chars[sc - 0x02]);
                }
            }
            else if(sc >= 0x10 && sc <= 0x19) { 
                const char* chars = "qwertyuiop"; 
                if(shift_pressed) {
                    const char* shift_chars = "QWERTYUIOP";
                    insert_char(shift_chars[sc - 0x10]);
                } else {
                    insert_char(chars[sc - 0x10]);
                }
            }
            else if(sc >= 0x1E && sc <= 0x26) { 
                const char* chars = "asdfghjkl"; 
                if(shift_pressed) {
                    const char* shift_chars = "ASDFGHJKL";
                    insert_char(shift_chars[sc - 0x1E]);
                } else {
                    insert_char(chars[sc - 0x1E]);
                }
            }
            else if(sc >= 0x2C && sc <= 0x32) { 
                const char* chars = "zxcvbnm"; 
                if(shift_pressed) {
                    const char* shift_chars = "ZXCVBNM";
                    insert_char(shift_chars[sc - 0x2C]);
                } else {
                    insert_char(chars[sc - 0x2C]);
                }
            }
            else if(sc == 0x0C) {
                if(shift_pressed) insert_char('_');
                else insert_char('-');
            }
            else if(sc == 0x0D) {
                if(shift_pressed) insert_char('+');
                else insert_char('=');
            }
            else if(sc == 0x1A) {
                if(shift_pressed) insert_char('{');
                else insert_char('[');
            }
            else if(sc == 0x1B) {
                if(shift_pressed) insert_char('}');
                else insert_char(']');
            }
            else if(sc == 0x27) {
                if(shift_pressed) insert_char(':');
                else insert_char(';');
            }
            else if(sc == 0x28) {
                if(shift_pressed) insert_char('"');
                else insert_char('\'');
            }
            else if(sc == 0x29) {
                if(shift_pressed) insert_char('~');
                else insert_char('`');
            }
            else if(sc == 0x33) { 
                if(shift_pressed) insert_char('<');
                else insert_char(',');
            }
            else if(sc == 0x34) { 
                if(shift_pressed) insert_char('>');
                else insert_char('.');
            }
            else if(sc == 0x35) { 
                if(shift_pressed) insert_char('?');
                else insert_char('/');
            }
            else if(sc == 0x39) {
                insert_char(' ');
            }
            
            if(cursor_line < scroll) scroll = cursor_line;
            if(cursor_line >= scroll + EDITOR_H) scroll = cursor_line - EDITOR_H + 1;
            
            if(old_scroll != scroll) { draw_text(); }
            else {
                if(old_line != cursor_line) {
                    if(old_line >= scroll && old_line < scroll + EDITOR_H) redraw_line(old_line);
                    if(cursor_line >= scroll && cursor_line < scroll + EDITOR_H) redraw_line(cursor_line);
                } else if(old_col != cursor_col) {
                    if(cursor_line >= scroll && cursor_line < scroll + EDITOR_H) redraw_line(cursor_line);
                }
            }
            
            draw_status();
            update_cursor();
            while(inb(0x64) & 1) inb(0x60);
        }
        move_cursor(79, 24);
    }
    clear_screen();
}