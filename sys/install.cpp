#include "video.h"
#include "graph.h"
#include "ata.h"
#include "vga.h"
#include "string_utils.h"
#include <stdint.h>
#include <stdarg.h>

static int mx = 160; 
static int my = 100; 

#define NULL 0

#define COLOR_BLACK      0x00
#define COLOR_BLUE       0x01
#define COLOR_GREEN      0x02
#define COLOR_CYAN       0x03
#define COLOR_RED        0x04
#define COLOR_YELLOW     0x0E
#define COLOR_GRAY       0x07
#define COLOR_DARK_GRAY  0x08
#define COLOR_WHITE      0x0F

static int selected = 0;
static int install_progress = 0;
static char username[32] = {0};
static char user_password[64] = {0};
static int user_ram_mb = 0;
static int user_mouse = 0;
static int user_monitor = 0;
static int user_sound = 1;
static int user_network = 0;
static int cpu_mhz_detected = 0;
static int ram_mb_detected = 0;
extern void update_time_display(void);

static int my_atoi(const char* s) {
    int res = 0;
    while(*s >= '0' && *s <= '9') {
        res = res * 10 + (*s - '0');
        s++;
    }
    return res;
}

static char* my_strstr(const char* haystack, const char* needle) {
    if(!haystack || !needle) return NULL;
    int needle_len = 0;
    while(needle[needle_len]) needle_len++;
    for(int i = 0; haystack[i]; i++) {
        int match = 1;
        for(int j = 0; j < needle_len; j++) {
            if(haystack[i+j] != needle[j]) {
                match = 0;
                break;
            }
        }
        if(match) return (char*)&haystack[i];
    }
    return NULL;
}

static void my_sprintf(char* buf, const char* fmt, ...) {
    char* b = buf;
    const char* f = fmt;
    va_list args;
    va_start(args, fmt);
    
    while(*f) {
        if(*f == '%') {
            f++;
            if(*f == 's') {
                const char* s = va_arg(args, const char*);
                while(*s) *b++ = *s++;
                f++;
            } else if(*f == 'd') {
                int n = va_arg(args, int);
                if(n == 0) {
                    *b++ = '0';
                } else {
                    char temp[16];
                    int i = 0;
                    while(n > 0) {
                        temp[i++] = '0' + (n % 10);
                        n /= 10;
                    }
                    while(i--) *b++ = temp[i];
                }
                f++;
            } else if(*f == '%') {
                *b++ = '%';
                f++;
            }
        } else if(*f == '\\') {
            f++;
            if(*f == 'n') {
                *b++ = '\n';
                f++;
            } else if(*f == 'r') {
                *b++ = '\r';
                f++;
            } else if(*f == 't') {
                *b++ = '\t';
                f++;
            } else {
                *b++ = '\\';
                *b++ = *f++;
            }
        } else {
            *b++ = *f++;
        }
    }
    *b = '\0';
    va_end(args);
}

static void my_strcat(char* dest, const char* src) {
    while(*dest) dest++;
    while(*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

static void draw_cursor(void) {
    static int cursor_blink = 0;
    cursor_blink = !cursor_blink;
    
    if(cursor_blink) {
        uint16_t* video = (uint16_t*)0xB8000;
        int pos = 40 * 80 + 40;
        video[pos] = 0x7020;
    }
}

static void draw_cursor_at(int x, int y) {
    if(x >= 0 && x < 80 && y >= 0 && y < 25) {
        uint16_t* video = (uint16_t*)0xB8000;
        video[y * 80 + x] = 0x7020;
    }
}

static void int_to_str(int num, char* str) {
    if(num == 0) { str[0] = '0'; str[1] = '\0'; return; }
    char temp[16];
    int i = 0;
    while(num > 0) {
        temp[i++] = (num % 10) + '0';
        num /= 10;
    }
    for(int j = 0; j < i; j++) str[j] = temp[i - j - 1];
    str[i] = '\0';
}

static void inst_delay(int ms) {
    for(volatile int i = 0; i < ms * 10000; i++);
}

static void kprint_char(char c) {
    char s[2] = {c, 0};
    kprint(s);
}

static void write_install_config(int stage, const char* data) {
    uint16_t buf[256] = {0};
    int len = my_strlen(data);
    for(int i = 0; i < len && i < 510; i++) {
        if(i % 2 == 0) buf[i/2] = data[i];
        else buf[i/2] |= (data[i] << 8);
    }
    write_sector(101 + stage, buf);
}

static void read_install_config(int stage, char* buffer, int max_len) {
    uint16_t buf[256];
    int sector = 101 + stage;
    read_sector(sector, buf);
    for(int i = 0; i < max_len && i < 510; i++) {
        if(i % 2 == 0) buffer[i] = buf[i/2] & 0xFF;
        else buffer[i] = (buf[i/2] >> 8) & 0xFF;
        if(buffer[i] == 0) break;
    }
    buffer[max_len-1] = '\0';
}

static int dir_counter = 300;
static int file_counter = 500;

static void create_dir(uint16_t parent_sector, const char* name, uint16_t* new_sector) {
    if(dir_counter > 2000) {
        kprint("[ERROR] Too many directories!\n");
        return;
    }
    
    uint16_t dir_buf[256];
    read_sector(parent_sector, dir_buf);
    
    int slot = -1;
    for(int i = 0; i < 32; i++) {
        if(((char*)dir_buf)[i*16] == 0) {
            slot = i;
            break;
        }
    }
    
    if(slot != -1) {
        for(int j = 0; j < 11 && name[j]; j++) {
            ((char*)dir_buf)[slot*16 + j] = name[j];
        }
        ((char*)dir_buf)[slot*16 + 11] = 1;
        
        *new_sector = dir_counter++;
        dir_buf[slot*8 + 6] = *new_sector;
        dir_buf[slot*8 + 7] = 0;
        write_sector(parent_sector, dir_buf);
        
        uint16_t empty_buf[256];
        for(int i = 0; i < 256; i++) empty_buf[i] = 0;
        write_sector(*new_sector, empty_buf);
    }
}

static void create_file(uint16_t parent_sector, const char* name, const char* content) {
    if(file_counter > 3000) {
        kprint("[ERROR] Too many files!\n");
        return;
    }
    
    uint16_t dir_buf[256];
    read_sector(parent_sector, dir_buf);
    
    int slot = -1;
    for(int i = 0; i < 32; i++) {
        if(((char*)dir_buf)[i*16] == 0) {
            slot = i;
            break;
        }
    }
    
    if(slot != -1) {
        for(int j = 0; j < 11 && name[j]; j++) {
            ((char*)dir_buf)[slot*16 + j] = name[j];
        }
        ((char*)dir_buf)[slot*16 + 11] = 0;
        
        int len = my_strlen(content);
        uint16_t data_buf[256] = {0};
        for(int i = 0; i < len && i < 510; i++) {
            if(i % 2 == 0) data_buf[i/2] = content[i];
            else data_buf[i/2] |= (content[i] << 8);
        }
        
        int file_sector = file_counter++;
        write_sector(file_sector, data_buf);
        
        dir_buf[slot*8 + 6] = file_sector;
        dir_buf[slot*8 + 7] = len;
        write_sector(parent_sector, dir_buf);
    }
}

static void lowformat_disk(int gb) {
    uint32_t total_sectors = (uint64_t)gb * 1024 * 1024 * 1024 / 512;
    uint32_t start_time = seconds;
    
    kprint("\nLow-level formatting ");
    kprint_int(gb);
    kprint(" GB (");
    kprint_int(total_sectors);
    kprint(" sectors)\n");
    
    uint16_t zero_buf[256];
    for(int i = 0; i < 256; i++) zero_buf[i] = 0;
    
    int last_percent = -1;
    
    for(uint32_t i = 0; i < total_sectors; i++) {
        write_sector(i, zero_buf);
        
        int percent = (i * 100) / total_sectors;
        if(percent != last_percent && percent % 2 == 0) {
            clear_screen();
            update_time_display();
            kprint("\nNOTE: Please wait for the complete formatting.\n");
            kprint("It will take from 15 minutes to 1 hour. Do not turn off the computer\n");
            kprint("Recommendation: Do not open the CD-ROM or remove the CD until the installation is complete.\n");
            kprint("[");
            int bars = (percent * 40) / 100;
            for(int b = 0; b < bars; b++) kprint("#");
            for(int b = bars; b < 40; b++) kprint(".");
            kprint("] ");
            kprint_int(percent);
            kprint("%");
            last_percent = percent;
        }
        
        if(i % 10000 == 0 && i > 0) {
            if(inb(0x64) & 1) {
                uint8_t sc = inb(0x60);
                if(sc == 0x01) {
                    kprint("\nCancelled\n");
                    return;
                }
            }
        }
    }
    
    uint32_t total_time = seconds - start_time;
    kprint("\nFormat complete! Time: ");
    kprint_int(total_time);
    kprint(" seconds\n");
    
    write_install_config(0, "installation_stage_1_completed = true\n");
    kprint("[INSTALL] Stage 1 config created\n");
}


static const char* cpu_list[] = {
    "Intel 386 (1985)", "Intel 486 (1989)", "Intel Pentium (1993)",
    "Intel Pentium MMX (1996)", "Intel Pentium Pro (1995)", "Intel Pentium II (1997)",
    "Intel Celeron (1998)", "Intel Pentium III (1999)", "Intel Pentium 4 (2000)",
    "Intel Celeron D (2004)", "Intel Pentium D (2005)", "Intel Core 2 Duo (2006)",
    "Intel Core 2 Quad (2007)", "Intel Core i3-530 (2010)", "Intel Core i5-750 (2009)",
    "Intel Core i7-920 (2008)", "Intel Core i3-2100 (2011)", "Intel Core i5-2500K (2011)",
    "Intel Core i7-2600K (2011)", "Intel Core i3-3220 (2012)", "Intel Core i5-3570K (2012)",
    "Intel Core i7-3770K (2012)", "Intel Core i3-4130 (2013)", "Intel Core i5-4670K (2013)",
    "Intel Core i7-4770K (2013)", "Intel Core i3-6100 (2015)", "Intel Core i5-6600K (2015)",
    "Intel Core i7-6700K (2015)", "Intel Core i3-7100 (2017)", "Intel Core i5-7600K (2017)",
    "Intel Core i7-7700K (2017)", "Intel Core i3-8100 (2017)", "Intel Core i5-8400 (2017)",
    "Intel Core i7-8700K (2017)", "Intel Core i9-9900K (2018)", "Intel Core i3-10100 (2020)",
    "Intel Core i5-10600K (2020)", "Intel Core i7-10700K (2020)", "Intel Core i9-10900K (2020)",
    "AMD K5 (1996)", "AMD K6 (1997)", "AMD K6-2 (1998)", "AMD K6-III (1999)",
    "AMD Athlon (1999)", "AMD Duron (2000)", "AMD Athlon XP (2001)", "AMD Sempron (2004)",
    "AMD Athlon 64 (2003)", "AMD Athlon 64 X2 (2005)", "AMD Phenom (2007)",
    "AMD Phenom II (2008)", "AMD Athlon II (2009)", "AMD FX-8150 (2011)",
    "AMD FX-8350 (2012)", "AMD A10-5800K (2012)", "AMD Ryzen 3 1200 (2017)",
    "AMD Ryzen 5 1600 (2017)", "AMD Ryzen 7 1700 (2017)", "AMD Ryzen 3 2200G (2018)",
    "AMD Ryzen 5 2600 (2018)", "AMD Ryzen 7 2700X (2018)", "AMD Ryzen 3 3100 (2020)",
    "AMD Ryzen 5 3600 (2019)", "AMD Ryzen 7 3700X (2019)", "AMD Ryzen 9 3900X (2019)",
    "AMD Ryzen 5 5600X (2020)", "AMD Ryzen 7 5800X (2020)", "AMD Ryzen 9 5900X (2020)",
    "AMD Ryzen 9 5950X (2020)",
    "Cyrix 5x86 (1995)", "Cyrix 6x86 (1996)", "Cyrix MII (1998)",
    "VIA Cyrix III (1999)", "VIA C3 (2000)", "VIA C7 (2005)",
    "Rise mP6 (1998)", "IDT WinChip (1997)", "NexGen Nx586 (1994)",
    "UMC U5 (1995)", "Transmeta Crusoe (2000)", "Transmeta Efficeon (2003)"
};
static int cpu_count = sizeof(cpu_list) / sizeof(cpu_list[0]);

static void detect_system_info(void) {
    uint32_t start_ticks, end_ticks;
    __asm__ volatile("rdtsc" : "=A"(start_ticks));
    for(volatile int i = 0; i < 1000000; i++);
    __asm__ volatile("rdtsc" : "=A"(end_ticks));
    cpu_mhz_detected = (end_ticks - start_ticks) / 1000000;
    if(cpu_mhz_detected < 1) cpu_mhz_detected = 33;
    
    uint32_t detected_ram = 0;
    for(int i = 0x100000; i < 0x8000000; i += 0x100000) {
        volatile uint32_t* test = (uint32_t*)i;
        uint32_t old = *test;
        *test = 0x55AA55AA;
        if(*test == 0x55AA55AA) detected_ram++;
        *test = old;
    }
    ram_mb_detected = detected_ram * 16;
    if(ram_mb_detected < 16) ram_mb_detected = 16;
    if(ram_mb_detected > 512) ram_mb_detected = 512;
}

static int vga_edit_field(int x, int y, char* buffer, int max_len, int is_password) {
    int pos = my_strlen(buffer);
    int cursor = pos;
    int editing = 1;
    int redraw = 1;
    
    while(editing) {
        if(redraw) {
            vga_rect(x, y - 2, max_len * 6 + 10, 12, 0x07, 0x07);
            
            if(is_password) {
                for(int i = 0; i < pos; i++) vga_char(x + i * 6, y, '*', 0x0F);
            } else {
                for(int i = 0; i < pos; i++) vga_char(x + i * 6, y, buffer[i], 0x0F);
            }
            
            vga_char(x + cursor * 6, y, '_', 0x0C);
            
            vga_vsync();
            vga_flip();
            redraw = 0;
        }
        
        if(vga_inb(0x64) & 1) {
            uint8_t k = vga_inb(0x60);
            if(k == 0x1C) {
                editing = 0;
            } else if(k == 0x0E && cursor > 0) { 
                cursor--;
                for(int i = cursor; i < pos; i++) buffer[i] = buffer[i+1];
                pos--;
                buffer[pos] = 0;
                redraw = 1;
            } else if(k == 0x01) { 
                return 0; 
            } else if(cursor < max_len - 1) {
                char ch = 0;
                if(k >= 0x02 && k <= 0x0B) ch = '1' + (k - 0x02);
                else if(k == 0x0C) ch = '-';
                else if(k == 0x0D) ch = '=';
                else if(k >= 0x10 && k <= 0x19) ch = 'q' + (k - 0x10);
                else if(k >= 0x1E && k <= 0x26) ch = 'a' + (k - 0x1E);
                else if(k >= 0x2C && k <= 0x32) ch = 'z' + (k - 0x2C);
                else if(k == 0x39) ch = ' ';
                else if(k == 0x34) ch = '.';
                
                if(ch) {
                    for(int i = pos; i > cursor; i--) buffer[i] = buffer[i-1];
                    buffer[cursor++] = ch;
                    pos++;
                    buffer[pos] = 0;
                    redraw = 1;
                }
            }
        }
        vga_delay(500);
    }
    return 1;
}

static int vga_cpu_menu(int current_cpu) {
    int cpu_selected = current_cpu;
    int cpu_scroll = 0;
    int cpu_running = 1;
    int cpu_redraw = 1;
    
    while(cpu_running) {
        if(cpu_redraw) {
            vga_clear(0x01);
            
            vga_rect(40, 20, 240, 160, 0x07, 0x0F);
            vga_text(120, 26, "SELECT CPU", 0x0F);
            vga_line(50, 36, 270, 36, 0x0F);
            
            for(int i = 0; i < 10 && cpu_scroll + i < cpu_count; i++) {
                int idx = cpu_scroll + i;
                uint8_t col = (idx == cpu_selected) ? 0x1F : 0x00;
                if(idx == cpu_selected) {
                    vga_rect(55, 42 + i*12, 210, 10, 0x1F, 0x0F);
                }
                vga_text(60, 43 + i*12, cpu_list[idx], idx == cpu_selected ? 0x0F : 0x00);
            }
            
            if(cpu_scroll > 0) vga_text(270, 42, "^", 0x0B);
            if(cpu_scroll + 10 < cpu_count) vga_text(270, 162, "v", 0x0B);
            
            vga_text(60, 170, "UP/DOWN: Select  ENTER: Confirm  ESC: Cancel", 0x0B);
            
            vga_cursor(mx, my);
            vga_vsync();
            vga_flip();
            cpu_redraw = 0;
        }
        
        if(vga_inb(0x64) & 1) {
            uint8_t k = vga_inb(0x60);
            if(k < 0x80) {
                if(k == 0x48 && cpu_selected > 0) {
                    cpu_selected--;
                    if(cpu_selected < cpu_scroll) cpu_scroll = cpu_selected;
                    cpu_redraw = 1;
                } else if(k == 0x50 && cpu_selected < cpu_count - 1) {
                    cpu_selected++;
                    if(cpu_selected >= cpu_scroll + 10) cpu_scroll = cpu_selected - 9;
                    cpu_redraw = 1;
                } else if(k == 0x1C) {
                    return cpu_selected;
                } else if(k == 0x01) {
                    return current_cpu;
                }
                if(k == 0x48 && my > 0) my--;
                if(k == 0x50 && my < 199) my++;
                if(k == 0x4B && mx > 0) mx--;
                if(k == 0x4D && mx < 309) mx++;
            }
        }
        vga_delay(500);
    }
    return current_cpu;
}

#define COL_BG          0x01 
#define COL_WIN         0x07 
#define COL_BORDER      0x0F 
#define COL_TITLE       0x0F 
#define COL_LABEL       0x0F  
#define COL_VALUE       0x00  
#define COL_ACTIVE_BG   0x70 
#define COL_ACTIVE_TEXT 0x00 
#define COL_CURSOR      0x0C 
#define COL_HINT        0x0B  
#define COL_CHECK_ON    0x0A 
#define COL_CHECK_OFF   0x08  

static char scancode_to_ascii(uint8_t sc) {
    if(sc == 0x02) return '1';
    if(sc == 0x03) return '2';
    if(sc == 0x04) return '3';
    if(sc == 0x05) return '4';
    if(sc == 0x06) return '5';
    if(sc == 0x07) return '6';
    if(sc == 0x08) return '7';
    if(sc == 0x09) return '8';
    if(sc == 0x0A) return '9';
    if(sc == 0x0B) return '0';
    if(sc == 0x10) return 'q';
    if(sc == 0x11) return 'w';
    if(sc == 0x12) return 'e';
    if(sc == 0x13) return 'r';
    if(sc == 0x14) return 't';
    if(sc == 0x15) return 'y';
    if(sc == 0x16) return 'u';
    if(sc == 0x17) return 'i';
    if(sc == 0x18) return 'o';
    if(sc == 0x19) return 'p';
    if(sc == 0x1E) return 'a';
    if(sc == 0x1F) return 's';
    if(sc == 0x20) return 'd';
    if(sc == 0x21) return 'f';
    if(sc == 0x22) return 'g';
    if(sc == 0x23) return 'h';
    if(sc == 0x24) return 'j';
    if(sc == 0x25) return 'k';
    if(sc == 0x26) return 'l';
    if(sc == 0x2C) return 'z';
    if(sc == 0x2D) return 'x';
    if(sc == 0x2E) return 'c';
    if(sc == 0x2F) return 'v';
    if(sc == 0x30) return 'b';
    if(sc == 0x31) return 'n';
    if(sc == 0x32) return 'm';
    if(sc == 0x0C) return '-';
    if(sc == 0x0D) return '=';
    if(sc == 0x27) return ';';
    if(sc == 0x33) return ',';
    if(sc == 0x34) return '.';
    if(sc == 0x35) return '/';
    if(sc == 0x39) return ' ';
    if(sc == 0x1A) return '[';
    if(sc == 0x1B) return ']';
    if(sc == 0x2B) return '\\';
    if(sc == 0x28) return '\'';
    
    return 0;
}

static void vga_draw_field_bg(int x, int y, int w, int active) {
    if(active) {
        for(int dx = 0; dx < w; dx++) {
            for(int dy = -3; dy < 10; dy++) {
                vga_buf_pixel(x + dx, y + dy, COL_ACTIVE_BG);
            }
        }
        vga_line(x, y - 3, x + w, y - 3, 0x0C);
        vga_line(x, y + 7, x + w, y + 7, 0x0C);
        vga_line(x, y - 3, x, y + 7, 0x0C);
        vga_line(x + w, y - 3, x + w, y + 7, 0x0C);
    }
}

static int vga_edit_field_v2(int x, int y, char* buffer, int max_len, int is_password) {
    int pos = 0;
    while(buffer[pos]) pos++;
    int cursor = pos;
    int editing = 1;
    int redraw = 1;
    int field_width = max_len * 6 + 4;
    
    while(editing) {
        if(redraw) {
            vga_draw_field_bg(x - 2, y, field_width, 1);
            
            if(is_password) {
                for(int i = 0; i < pos; i++) {
                    vga_char(x + i * 6, y, '*', COL_ACTIVE_TEXT);
                }
            } else {
                for(int i = 0; i < pos; i++) {
                    vga_char(x + i * 6, y, buffer[i], COL_ACTIVE_TEXT);
                }
            }
            
            vga_char(x + cursor * 6, y, '_', COL_CURSOR);
            
            vga_vsync();
            vga_flip();
            redraw = 0;
        }
        
        if(vga_inb(0x64) & 1) {
            uint8_t sc = vga_inb(0x60);
            if(sc < 0x80) {
                if(sc == 0x1C) { 
                    editing = 0;
                } 
                else if(sc == 0x01) { 
                    return 0;
                }
                else if(sc == 0x0E && cursor > 0) {
                    cursor--;
                    for(int i = cursor; i < pos; i++) {
                        buffer[i] = buffer[i + 1];
                    }
                    pos--;
                    buffer[pos] = '\0';
                    redraw = 1;
                }
                else if(sc == 0x4B && cursor > 0) {
                    cursor--;
                    redraw = 1;
                }
                else if(sc == 0x4D && cursor < pos) { 
                    cursor++;
                    redraw = 1;
                }
                else {
                    char ch = scancode_to_ascii(sc);
                    if(ch && pos < max_len - 1) {
                        for(int i = pos; i > cursor; i--) {
                            buffer[i] = buffer[i - 1];
                        }
                        buffer[cursor] = ch;
                        cursor++;
                        pos++;
                        buffer[pos] = '\0';
                        redraw = 1;
                    }
                }
            }
        }
        vga_delay(500);
    }
    
    return 1;
}

static void sound_click() {
    outb(0x61, inb(0x61) | 3);
    for(volatile int i = 0; i < 10000; i++);
    outb(0x61, inb(0x61) & 0xFC);
}

static void stage2_input(void) {
    clear_screen_bg(COLOR_GRAY);

    int win_x = 15, win_y = 3, win_w = 50, win_h = 22;
    int label_x = win_x + 4;
    int value_x = win_x + 20;
    
    detect_system_info();
    
    char hostname[32] = {0};
    char timezone[32] = {0};
    char lang[16] = {0};
    char keyboard_layout[16] = {0};
    char computer_name[32] = {0};
    int user_timezone_offset = 0;
    int user_autologin = 0;
    int user_desktop_effect = 1;
    int user_wnkui_autostart = 1;
    int selected_cpu = 0;
    
const char* cpu_list[] = {
    "Intel 386 (1985)", "Intel 486 (1989)", "Intel Pentium (1993)",
    "Intel Pentium MMX (1996)", "Intel Pentium Pro (1995)", "Intel Pentium II (1997)",
    "Intel Celeron (1998)", "Intel Pentium III (1999)", "Intel Pentium 4 (2000)",
    "Intel Celeron D (2004)", "Intel Pentium D (2005)", "Intel Core 2 Duo (2006)",
    "Intel Core 2 Quad (2007)", "Intel Core i3-530 (2010)", "Intel Core i5-750 (2009)",
    "Intel Core i7-920 (2008)", "Intel Core i3-2100 (2011)", "Intel Core i5-2500K (2011)",
    "Intel Core i7-2600K (2011)", "Intel Core i3-3220 (2012)", "Intel Core i5-3570K (2012)",
    "Intel Core i7-3770K (2012)", "Intel Core i3-4130 (2013)", "Intel Core i5-4670K (2013)",
    "Intel Core i7-4770K (2013)", "Intel Core i3-6100 (2015)", "Intel Core i5-6600K (2015)",
    "Intel Core i7-6700K (2015)", "Intel Core i3-7100 (2017)", "Intel Core i5-7600K (2017)",
    "Intel Core i7-7700K (2017)", "Intel Core i3-8100 (2017)", "Intel Core i5-8400 (2017)",
    "Intel Core i7-8700K (2017)", "Intel Core i9-9900K (2018)", "Intel Core i3-10100 (2020)",
    "Intel Core i5-10600K (2020)", "Intel Core i7-10700K (2020)", "Intel Core i9-10900K (2020)",
    "AMD K5 (1996)", "AMD K6 (1997)", "AMD K6-2 (1998)", "AMD K6-III (1999)",
    "AMD Athlon (1999)", "AMD Duron (2000)", "AMD Athlon XP (2001)", "AMD Sempron (2004)",
    "AMD Athlon 64 (2003)", "AMD Athlon 64 X2 (2005)", "AMD Phenom (2007)",
    "AMD Phenom II (2008)", "AMD Athlon II (2009)", "AMD FX-8150 (2011)",
    "AMD FX-8350 (2012)", "AMD A10-5800K (2012)", "AMD Ryzen 3 1200 (2017)",
    "AMD Ryzen 5 1600 (2017)", "AMD Ryzen 7 1700 (2017)", "AMD Ryzen 3 2200G (2018)",
    "AMD Ryzen 5 2600 (2018)", "AMD Ryzen 7 2700X (2018)", "AMD Ryzen 3 3100 (2020)",
    "AMD Ryzen 5 3600 (2019)", "AMD Ryzen 7 3700X (2019)", "AMD Ryzen 9 3900X (2019)",
    "AMD Ryzen 5 5600X (2020)", "AMD Ryzen 7 5800X (2020)", "AMD Ryzen 9 5900X (2020)",
    "AMD Ryzen 9 5950X (2020)",
    "Cyrix 5x86 (1995)", "Cyrix 6x86 (1996)", "Cyrix MII (1998)",
    "VIA Cyrix III (1999)", "VIA C3 (2000)", "VIA C7 (2005)",
    "Rise mP6 (1998)", "IDT WinChip (1997)", "NexGen Nx586 (1994)",
    "UMC U5 (1995)", "Transmeta Crusoe (2000)", "Transmeta Efficeon (2003)"
};
int cpu_count = sizeof(cpu_list) / sizeof(cpu_list[0]);
    int cpu_scroll = 0;
    int cpu_selected = 0;
    int cpu_menu_active = 0;
    int current_field = 0;
    int running = 1;
    int redraw = 1;
    
    while(running) {
        if(redraw) {
            clear_screen_bg(COLOR_GRAY);
            
            int win_x = 15, win_y = 3, win_w = 50, win_h = 22;
            draw_shadow_window(win_x, win_y, win_w, win_h, COLOR_BLUE, TXT_WHITE, "WNKA OS SETUP - STAGE 2");
            
            draw_dframe(win_x + 2, win_y + 2, win_w - 4, 3, COLOR_BLUE, TXT_YELLOW);
            kprint_at("System Configuration Wizard", win_x + (win_w - 24)/2, win_y + 3, (COLOR_BLUE << 4) | TXT_YELLOW);
            
            int y = win_y + 6;
            int label_x = win_x + 4;
            int value_x = win_x + 20;
            
            uint8_t color0 = (current_field == 0) ? TXT_GREEN : TXT_WHITE;
            uint8_t color1 = (current_field == 1) ? TXT_GREEN : TXT_WHITE;
            uint8_t color2 = (current_field == 2) ? TXT_GREEN : TXT_WHITE;
            uint8_t color3 = (current_field == 3) ? TXT_GREEN : TXT_WHITE;
            uint8_t color4 = (current_field == 4) ? TXT_GREEN : TXT_WHITE;
            uint8_t color5 = (current_field == 5) ? TXT_GREEN : TXT_WHITE;
            uint8_t color6 = (current_field == 6) ? TXT_GREEN : TXT_WHITE;
            uint8_t color7 = (current_field == 7) ? TXT_GREEN : TXT_WHITE;
            
            kprint_at("Username:", label_x, y, (COLOR_BLACK << 4) | color0);
            kprint_at(">", value_x - 2, y, (COLOR_BLACK << 4) | TXT_GREEN);
            kprint_at(username, value_x, y, (COLOR_BLACK << 4) | TXT_WHITE);
            for(int i = my_strlen(username); i < 16; i++) kprint_at(" ", value_x + i, y, (COLOR_BLACK << 4) | TXT_BLACK);
            
            kprint_at("Password:", label_x, y + 1, (COLOR_BLACK << 4) | color1);
            kprint_at(">", value_x - 2, y + 1, (COLOR_BLACK << 4) | TXT_GREEN);
            for(int i = 0; i < my_strlen(user_password); i++) kprint_at("*", value_x + i, y + 1, (COLOR_BLACK << 4) | TXT_GREEN);
            
            kprint_at("Hostname:", label_x, y + 2, (COLOR_BLACK << 4) | color2);
            kprint_at(">", value_x - 2, y + 2, (COLOR_BLACK << 4) | TXT_GREEN);
            kprint_at(hostname, value_x, y + 2, (COLOR_BLACK << 4) | TXT_WHITE);
            
            kprint_at("Computer:", label_x, y + 3, (COLOR_BLACK << 4) | color3);
            kprint_at(">", value_x - 2, y + 3, (COLOR_BLACK << 4) | TXT_GREEN);
            kprint_at(computer_name, value_x, y + 3, (COLOR_BLACK << 4) | TXT_WHITE);
            
            kprint_at("CPU Type:", label_x, y + 4, (COLOR_BLACK << 4) | color4);
            kprint_at("[", value_x - 1, y + 4, (COLOR_BLACK << 4) | TXT_CYAN);
            kprint_at(cpu_list[cpu_selected], value_x, y + 4, (COLOR_BLACK << 4) | TXT_YELLOW);
            kprint_at("]", value_x + my_strlen(cpu_list[cpu_selected]), y + 4, (COLOR_BLACK << 4) | TXT_CYAN);
            
            kprint_at("Auto-login:", label_x, y + 5, (COLOR_BLACK << 4) | color5);
            kprint_at(user_autologin ? "[X]" : "[ ]", value_x, y + 5, (COLOR_BLACK << 4) | (user_autologin ? TXT_GREEN : TXT_WHITE));
            
            kprint_at("Desktop Effects:", label_x, y + 6, (COLOR_BLACK << 4) | color6);
            kprint_at(user_desktop_effect ? "[X]" : "[ ]", value_x, y + 6, (COLOR_BLACK << 4) | (user_desktop_effect ? TXT_GREEN : TXT_WHITE));
            
            kprint_at("WnkUI Autostart:", label_x, y + 7, (COLOR_BLACK << 4) | color7);
            kprint_at(user_wnkui_autostart ? "[X]" : "[ ]", value_x, y + 7, (COLOR_BLACK << 4) | (user_wnkui_autostart ? TXT_GREEN : TXT_WHITE));
            
            draw_hline(win_x + 1, win_y + win_h - 4, win_w - 2, COLOR_BLUE, TXT_WHITE, S_HLINE);
            kprint_at("UP/DOWN: Move   ENTER: Select/Edit   SPACE: Toggle", win_x + 5, win_y + win_h - 3, (COLOR_BLUE << 4) | TXT_YELLOW);
            kprint_at("F1: CPU Menu   F2: Language   F3: Keyboard   ESC: Cancel", win_x + 5, win_y + win_h - 2, (COLOR_BLUE << 4) | TXT_CYAN);
            
            redraw = 0;
        }
        
        move_cursor(79, 24);
        
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc < 0x80) {
                if(sc == 0x48 && current_field > 0) {
                    current_field--;
                    redraw = 1;
                    sound_click();
                }
                else if(sc == 0x50 && current_field < 7) {
                    current_field++;
                    redraw = 1;
                    sound_click();
                }
                else if(sc == 0x1C) {
                    if(current_field == 0) {
                        int pos = my_strlen(username);
                        int cursor = pos;
                        int editing = 1;
                        while(editing) {
                            kprint_at("_", value_x + cursor, win_y + 6, (COLOR_BLACK << 4) | TXT_RED);
                            if(inb(0x64) & 1) {
                                uint8_t k = inb(0x60);
                                if(k == 0x1C) editing = 0;
                                else if(k == 0x0E && cursor > 0) {
                                    cursor--;
                                    for(int i = cursor; i < pos; i++) username[i] = username[i+1];
                                    pos--;
                                    kprint_at(" ", value_x + cursor, win_y + 6, (COLOR_BLACK << 4) | TXT_BLACK);
                                    redraw = 1;
                                }
                                else if(k >= 0x02 && k <= 0x0D && cursor < 15) {
                                    char ch = "1234567890-="[k - 0x02];
                                    for(int i = pos; i > cursor; i--) username[i] = username[i-1];
                                    username[cursor++] = ch;
                                    pos++;
                                    redraw = 1;
                                }
                                else if(k >= 0x10 && k <= 0x19 && cursor < 15) {
                                    char ch = "qwertyuiop"[k - 0x10];
                                    for(int i = pos; i > cursor; i--) username[i] = username[i-1];
                                    username[cursor++] = ch;
                                    pos++;
                                    redraw = 1;
                                }
                                else if(k >= 0x1E && k <= 0x26 && cursor < 15) {
                                    char ch = "asdfghjkl"[k - 0x1E];
                                    for(int i = pos; i > cursor; i--) username[i] = username[i-1];
                                    username[cursor++] = ch;
                                    pos++;
                                    redraw = 1;
                                }
                                else if(k >= 0x2C && k <= 0x32 && cursor < 15) {
                                    char ch = "zxcvbnm"[k - 0x2C];
                                    for(int i = pos; i > cursor; i--) username[i] = username[i-1];
                                    username[cursor++] = ch;
                                    pos++;
                                    redraw = 1;
                                }
                                else if(k == 0x39 && cursor < 15) {
                                    for(int i = pos; i > cursor; i--) username[i] = username[i-1];
                                    username[cursor++] = ' ';
                                    pos++;
                                    redraw = 1;
                                }
                                username[pos] = '\0';
                                if(redraw) {
                                    kprint_at(username, value_x, win_y + 6, (COLOR_BLACK << 4) | TXT_WHITE);
                                    for(int i = pos; i < 16; i++) kprint_at(" ", value_x + i, win_y + 6, (COLOR_BLACK << 4) | TXT_BLACK);
                                }
                            }
                        }
                        kprint_at(" ", value_x + cursor, win_y + 6, (COLOR_BLACK << 4) | TXT_BLACK);
                        redraw = 1;
                    }
                    else if(current_field == 1) {
                        int pos = my_strlen(user_password);
                        int cursor = pos;
                        int editing = 1;
                        while(editing) {
                            kprint_at("_", value_x + cursor, win_y + 7, (COLOR_BLACK << 4) | TXT_RED);
                            if(inb(0x64) & 1) {
                                uint8_t k = inb(0x60);
                                if(k == 0x1C) editing = 0;
                                else if(k == 0x0E && cursor > 0) {
                                    cursor--;
                                    for(int i = cursor; i < pos; i++) user_password[i] = user_password[i+1];
                                    pos--;
                                    redraw = 1;
                                }
                                else if(k >= 0x02 && k <= 0x0D && cursor < 15) {
                                    user_password[cursor++] = "1234567890"[k - 0x02];
                                    pos++;
                                    redraw = 1;
                                }
                                else if(k >= 0x10 && k <= 0x19 && cursor < 15) {
                                    user_password[cursor++] = "qwertyuiop"[k - 0x10];
                                    pos++;
                                    redraw = 1;
                                }
                                else if(k >= 0x1E && k <= 0x26 && cursor < 15) {
                                    user_password[cursor++] = "asdfghjkl"[k - 0x1E];
                                    pos++;
                                    redraw = 1;
                                }
                                else if(k >= 0x2C && k <= 0x32 && cursor < 15) {
                                    user_password[cursor++] = "zxcvbnm"[k - 0x2C];
                                    pos++;
                                    redraw = 1;
                                }
                                user_password[pos] = '\0';
                                if(redraw) {
                                    for(int i = 0; i < pos; i++) kprint_at("*", value_x + i, win_y + 7, (COLOR_BLACK << 4) | TXT_GREEN);
                                    for(int i = pos; i < 16; i++) kprint_at(" ", value_x + i, win_y + 7, (COLOR_BLACK << 4) | TXT_BLACK);
                                }
                            }
                        }
                        kprint_at(" ", value_x + cursor, win_y + 7, (COLOR_BLACK << 4) | TXT_BLACK);
                        redraw = 1;
                    }
                    else if(current_field == 2) {
                        int pos = my_strlen(hostname);
                        int cursor = pos;
                        int editing = 1;
                        while(editing) {
                            kprint_at("_", value_x + cursor, win_y + 8, (COLOR_BLACK << 4) | TXT_RED);
                            if(inb(0x64) & 1) {
                                uint8_t k = inb(0x60);
                                if(k == 0x1C) editing = 0;
                                else if(k == 0x0E && cursor > 0) {
                                    cursor--;
                                    for(int i = cursor; i < pos; i++) hostname[i] = hostname[i+1];
                                    pos--;
                                    redraw = 1;
                                }
                                else if(k >= 0x02 && k <= 0x0D && cursor < 15) {
                                    char ch = "1234567890-="[k - 0x02];
                                    for(int i = pos; i > cursor; i--) hostname[i] = hostname[i-1];
                                    hostname[cursor++] = ch;
                                    pos++;
                                    redraw = 1;
                                }
                                else if(k >= 0x10 && k <= 0x19 && cursor < 15) {
                                    char ch = "qwertyuiop"[k - 0x10];
                                    for(int i = pos; i > cursor; i--) hostname[i] = hostname[i-1];
                                    hostname[cursor++] = ch;
                                    pos++;
                                    redraw = 1;
                                }
                                else if(k >= 0x1E && k <= 0x26 && cursor < 15) {
                                    char ch = "asdfghjkl"[k - 0x1E];
                                    for(int i = pos; i > cursor; i--) hostname[i] = hostname[i-1];
                                    hostname[cursor++] = ch;
                                    pos++;
                                    redraw = 1;
                                }
                                else if(k >= 0x2C && k <= 0x32 && cursor < 15) {
                                    char ch = "zxcvbnm"[k - 0x2C];
                                    for(int i = pos; i > cursor; i--) hostname[i] = hostname[i-1];
                                    hostname[cursor++] = ch;
                                    pos++;
                                    redraw = 1;
                                }
                                else if(k == 0x39 && cursor < 15) {
                                    for(int i = pos; i > cursor; i--) hostname[i] = hostname[i-1];
                                    hostname[cursor++] = ' ';
                                    pos++;
                                    redraw = 1;
                                }
                                hostname[pos] = '\0';
                                if(redraw) kprint_at(hostname, value_x, win_y + 8, (COLOR_BLACK << 4) | TXT_WHITE);
                            }
                        }
                        kprint_at(" ", value_x + cursor, win_y + 8, (COLOR_BLACK << 4) | TXT_BLACK);
                        redraw = 1;
                    }
                    else if(current_field == 3) {
                        int pos = my_strlen(computer_name);
                        int cursor = pos;
                        int editing = 1;
                        while(editing) {
                            kprint_at("_", value_x + cursor, win_y + 9, (COLOR_BLACK << 4) | TXT_RED);
                            if(inb(0x64) & 1) {
                                uint8_t k = inb(0x60);
                                if(k == 0x1C) editing = 0;
                                else if(k == 0x0E && cursor > 0) {
                                    cursor--;
                                    for(int i = cursor; i < pos; i++) computer_name[i] = computer_name[i+1];
                                    pos--;
                                    redraw = 1;
                                }
                                else if(k >= 0x10 && k <= 0x19 && cursor < 15) {
                                    computer_name[cursor++] = "qwertyuiop"[k - 0x10];
                                    pos++;
                                    redraw = 1;
                                }
                                else if(k >= 0x1E && k <= 0x26 && cursor < 15) {
                                    computer_name[cursor++] = "asdfghjkl"[k - 0x1E];
                                    pos++;
                                    redraw = 1;
                                }
                                else if(k >= 0x2C && k <= 0x32 && cursor < 15) {
                                    computer_name[cursor++] = "zxcvbnm"[k - 0x2C];
                                    pos++;
                                    redraw = 1;
                                }
                                else if(k >= 0x02 && k <= 0x0B && cursor < 15) {
                                    computer_name[cursor++] = "1234567890"[k - 0x02];
                                    pos++;
                                    redraw = 1;
                                }
                                else if(k == 0x39 && cursor < 15) {
                                    computer_name[cursor++] = ' ';
                                    pos++;
                                    redraw = 1;
                                }
                                computer_name[pos] = '\0';
                                if(redraw) kprint_at(computer_name, value_x, win_y + 9, (COLOR_BLACK << 4) | TXT_WHITE);
                            }
                        }
                        kprint_at(" ", value_x + cursor, win_y + 9, (COLOR_BLACK << 4) | TXT_BLACK);
                        redraw = 1;
                    }
                    else if(current_field == 4) {
                        cpu_menu_active = 1;
                        cpu_scroll = 0;
                        int cpu_selected_temp = cpu_selected;
                        int cpu_running = 1;
                        int cpu_redraw = 1;
                        
                        while(cpu_running) {
                            if(cpu_redraw) {
                                int menu_x = win_x + 10, menu_y = win_y + 4, menu_w = 30, menu_h = 12;
                                draw_shadow_window(menu_x, menu_y, menu_w, menu_h, COLOR_GRAY, TXT_WHITE, "Select CPU");
                                
                                for(int i = 0; i < 10 && cpu_scroll + i < cpu_count; i++) {
                                    int idx = cpu_scroll + i;
                                    uint8_t color = (idx == cpu_selected_temp) ? TXT_GREEN : TXT_WHITE;
                                    kprint_at(cpu_list[idx], menu_x + 2, menu_y + 2 + i, (COLOR_GRAY << 4) | color);
                                }
                                
                                if(cpu_scroll > 0) kprint_at("^", menu_x + menu_w - 3, menu_y + 2, (COLOR_GRAY << 4) | TXT_CYAN);
                                if(cpu_scroll + 10 < cpu_count) kprint_at("v", menu_x + menu_w - 3, menu_y + menu_h - 3, (COLOR_GRAY << 4) | TXT_CYAN);
                                
                                kprint_at("UP/DOWN: Move  ENTER: Select  ESC: Cancel", menu_x + 2, menu_y + menu_h - 2, (COLOR_GRAY << 4) | TXT_YELLOW);
                                cpu_redraw = 0;
                            }
                            
                            if(inb(0x64) & 1) {
                                uint8_t k = inb(0x60);
                                if(k < 0x80) {
                                    if(k == 0x48 && cpu_selected_temp > 0) {
                                        cpu_selected_temp--;
                                        if(cpu_selected_temp < cpu_scroll) cpu_scroll = cpu_selected_temp;
                                        cpu_redraw = 1;
                                    }
                                    else if(k == 0x50 && cpu_selected_temp < cpu_count - 1) {
                                        cpu_selected_temp++;
                                        if(cpu_selected_temp >= cpu_scroll + 10) cpu_scroll = cpu_selected_temp - 9;
                                        cpu_redraw = 1;
                                    }
                                    else if(k == 0x1C) {
                                        cpu_selected = cpu_selected_temp;
                                        cpu_running = 0;
                                        redraw = 1;
                                    }
                                    else if(k == 0x01) {
                                        cpu_running = 0;
                                        redraw = 1;
                                    }
                                }
                            }
                        }
                        cpu_menu_active = 0;
                    }
                    else if(current_field == 5) {
                        user_autologin = !user_autologin;
                        redraw = 1;
                    }
                    else if(current_field == 6) {
                        user_desktop_effect = !user_desktop_effect;
                        redraw = 1;
                    }
                    else if(current_field == 7) {
                        user_wnkui_autostart = !user_wnkui_autostart;
                        redraw = 1;
                    }
                }
                else if(sc == 0x39) {
                    if(current_field == 5) {
                        user_autologin = !user_autologin;
                        redraw = 1;
                    }
                    else if(current_field == 6) {
                        user_desktop_effect = !user_desktop_effect;
                        redraw = 1;
                    }
                    else if(current_field == 7) {
                        user_wnkui_autostart = !user_wnkui_autostart;
                        redraw = 1;
                    }
                }
                else if(sc == 0x3B) {
                    current_field = 4;
                    redraw = 1;
                }
                else if(sc == 0x3C) {
                    kprint_at("Language menu (not implemented)", win_x + 5, win_y + win_h - 1, (COLOR_BLACK << 4) | TXT_YELLOW);
                    inst_delay(1000);
                    redraw = 1;
                }
                else if(sc == 0x3D) {
                    kprint_at("Keyboard menu (not implemented)", win_x + 5, win_y + win_h - 1, (COLOR_BLACK << 4) | TXT_YELLOW);
                    inst_delay(1000);
                    redraw = 1;
                }
                else if(sc == 0x01) {
                    running = 0;
                }
            }
        }
        
        inst_delay(30);
    }
    kprint_color("\n[Saving configuration...]\n", TXT_GREEN);
    
    char config[1536];
    my_sprintf(config, 
        "username=%s\n"
        "password=%s\n"
        "hostname=%s\n"
        "computer_name=%s\n"
        "timezone=%s\n"
        "timezone_offset=%d\n"
        "language=%s\n"
        "keyboard=%s\n"
        "ram=%d\n"
        "mouse=%d\n"
        "monitor=%d\n"
        "sound=%d\n"
        "network=%d\n"
        "autologin=%d\n"
        "desktop_effects=%d\n"
        "wnkui_autostart=%d\n"
        "cpu_type=%d\n"
        "cpu_mhz=%d\n"
        "detected_ram=%d\n"
        "stage2_completed=true\n",
        username, user_password, hostname, computer_name, timezone, user_timezone_offset,
        lang, keyboard_layout, user_ram_mb, user_mouse, user_monitor, 
        user_sound, user_network, user_autologin, user_desktop_effect,
        user_wnkui_autostart, cpu_selected, cpu_mhz_detected, ram_mb_detected);
    write_install_config(1, config);
    
    write_install_config(0, "installation_stage_1_completed = true\n");
    
    uint16_t pass_buf[256] = {0};
    for(int i = 0; user_password[i]; i++) {
        if(i % 2 == 0) pass_buf[i/2] = user_password[i];
        else pass_buf[i/2] |= (user_password[i] << 8);
    }
    write_sector(103, pass_buf);
    
    uint16_t host_buf[256] = {0};
    for(int i = 0; hostname[i]; i++) {
        if(i % 2 == 0) host_buf[i/2] = hostname[i];
        else host_buf[i/2] |= (hostname[i] << 8);
    }
    write_sector(104, host_buf);
    
    uint16_t user_buf[256] = {0};
    for(int i = 0; username[i]; i++) {
        if(i % 2 == 0) user_buf[i/2] = username[i];
        else user_buf[i/2] |= (username[i] << 8);
    }
    write_sector(105, user_buf);
    
    uint16_t settings_buf[256] = {0};
    settings_buf[0] = user_ram_mb;
    settings_buf[1] = user_mouse;
    settings_buf[2] = user_monitor;
    settings_buf[3] = user_sound;
    settings_buf[4] = user_network;
    settings_buf[5] = user_autologin;
    settings_buf[6] = user_desktop_effect;
    settings_buf[7] = user_timezone_offset;
    settings_buf[8] = cpu_selected;
    settings_buf[9] = cpu_mhz_detected;
    settings_buf[10] = ram_mb_detected;
    settings_buf[11] = user_wnkui_autostart;
    write_sector(106, settings_buf);
    
    uint16_t tz_buf[256] = {0};
    for(int i = 0; timezone[i]; i++) {
        if(i % 2 == 0) tz_buf[i/2] = timezone[i];
        else tz_buf[i/2] |= (timezone[i] << 8);
    }
    write_sector(107, tz_buf);
    
    uint16_t lang_buf[256] = {0};
    for(int i = 0; lang[i]; i++) {
        if(i % 2 == 0) lang_buf[i/2] = lang[i];
        else lang_buf[i/2] |= (lang[i] << 8);
    }
    for(int i = 0; keyboard_layout[i]; i++) {
        if(i % 2 == 0) lang_buf[16 + i/2] = keyboard_layout[i];
        else lang_buf[16 + i/2] |= (keyboard_layout[i] << 8);
    }
    write_sector(108, lang_buf);
    
    uint16_t comp_buf[256] = {0};
    for(int i = 0; computer_name[i]; i++) {
        if(i % 2 == 0) comp_buf[i/2] = computer_name[i];
        else comp_buf[i/2] |= (computer_name[i] << 8);
    }
    write_sector(109, comp_buf);

    clear_screen_bg(COLOR_GRAY);
    
    draw_shadow_window(12, 6, 56, 10, COLOR_BLUE, TXT_WHITE, "CONFIGURATION SAVED");
    
    draw_dframe(14, 8, 52, 6, COLOR_BLACK, TXT_GREEN);
    kprint_at("All settings have been saved successfully!", 18, 10, (COLOR_BLACK << 4) | TXT_GREEN);
    
    kprint_at("Username:", 18, 13, (COLOR_BLACK << 4) | TXT_CYAN);
    kprint_at(username, 29, 13, (COLOR_BLACK << 4) | TXT_WHITE);
    
    kprint_at("Hostname:", 18, 14, (COLOR_BLACK << 4) | TXT_CYAN);
    kprint_at(hostname, 29, 14, (COLOR_BLACK << 4) | TXT_WHITE);
    
    kprint_at("Computer:", 18, 15, (COLOR_BLACK << 4) | TXT_CYAN);
    kprint_at(computer_name, 29, 15, (COLOR_BLACK << 4) | TXT_WHITE);
    
    kprint_at("CPU:", 18, 16, (COLOR_BLACK << 4) | TXT_CYAN);
    kprint_at(cpu_list[cpu_selected], 24, 16, (COLOR_BLACK << 4) | TXT_GREEN);
    
    kprint_at("WnkUI Autostart:", 18, 17, (COLOR_BLACK << 4) | TXT_CYAN);
    kprint_at(user_wnkui_autostart ? "ENABLED" : "DISABLED", 35, 17, (COLOR_BLACK << 4) | (user_wnkui_autostart ? TXT_GREEN : TXT_RED));
    
    draw_hline(14, 19, 52, COLOR_BLUE, TXT_YELLOW, D_HLINE);
    
    kprint_color("     Sit back and wait while WnkaOS writes the configs...\n", TXT_CYAN);
    
    draw_hline(14, 21, 52, COLOR_BLUE, TXT_YELLOW, D_HLINE);
    
    for(int i = 0; i < 6; i++) {
        kprint_at(".", 40 + i, 18, (COLOR_BLACK << 4) | TXT_GREEN);
        inst_delay(300);
    }
    kprint("\n");
    
    inst_delay(2000);
}
typedef struct {
    const char* name;
    int enabled;
    const char* desc;
} install_option_t;

static install_option_t install_options[] = {
    {"Base System", 1, "Core OS files and directories (REQUIRED)"},
    {"Games", 1, "Pacman, Flappy Bird, Snake"},
    {"WnkC Scripts", 1, "Calculator, sysinfo, password checker"},
    {"Graphics Demos", 0, "Matrix, Fire, Plasma effects"},
    {"Network Support", 0, "E1000 driver, ping, DNS"},
    {"Sound Effects", 1, "PC speaker sounds"},
    {"Development Tools", 0, "TCC compiler"},
    {"Debug Tools", 0, "Memory dump, crash test"},
    {"Documentation", 1, "README, help files"},
    {"Autoexec Script", 1, "Auto-start welcome script"},
    {"WnkaSXS Backup", 1, "System backup directory"},
    {"Multitasking", 0, "Background processes"},
};
static int option_count = sizeof(install_options) / sizeof(install_option_t);



static const char* ads[] = {
    "WNKA OS X32: 40MB RAM only!",
    "Built-in C compiler (TCC)",
    "WnkC scripting language",
    "UNIX-like filesystem",
    "Live CD mode - no install needed",
    "Full GUI with mouse support",
    "Games: Pacman, Flappy, Snake",
    "Multitasking scheduler",
    "Network stack: ping, DNS, HTTP",
    "Open source and free",
    "Runs on any 32-bit CPU",
    "Boots in under 2 seconds",
    "No bloatware, no registry",
    "Your OS, your rules"
};
static int ad_count = sizeof(ads) / sizeof(ads[0]);

typedef struct {
    const char* name;
    int enabled;
    const char* desc;
    int size;
    int category;
} wnc_script_t;

static wnc_script_t wnc_scripts[] = {
    {"welcome.wnc", 1, "Welcome screen with full guide", 3800, 0},
    {"sysinfo.wnc", 1, "Complete system information", 3200, 0},
    {"fetch.wnc", 1, "Hardware info", 1200, 0},
    {"uptime.wnc", 1, "System uptime", 800, 0},
    {"diskfree.wnc", 1, "Free disk space", 900, 0},
    {"process.wnc", 0, "Process manager", 4200, 0},
    {"services.wnc", 0, "Service control", 3800, 0},
    {"backup.wnc", 0, "Full system backup", 5600, 0},
    {"restore.wnc", 0, "System restore", 4900, 0},
    {"logview.wnc", 0, "System log viewer", 3100, 0},
    {"calc.wnc", 1, "Basic calculator", 2500, 1},
    {"calc_adv.wnc", 0, "Advanced calculator", 4800, 1},
    {"dice.wnc", 0, "Dice roller", 1200, 1},
    {"random.wnc", 0, "Random number generator", 1400, 1},
    {"prime.wnc", 0, "Prime number checker", 2500, 1},
    {"fibonacci.wnc", 0, "Fibonacci sequence", 2100, 1},
    {"timer.wnc", 0, "Countdown timer", 2400, 1},
    {"stopwatch.wnc", 0, "Stopwatch", 2200, 1},
    {"todo.wnc", 0, "Todo list manager", 4100, 1},
    {"notes.wnc", 0, "Quick notes", 3500, 1},
    {"reminder.wnc", 0, "Reminder system", 4300, 1},
    {"password_gen.wnc", 0, "Password generator", 2800, 1},
    {"guess.wnc", 0, "Guess the number", 3200, 2},
    {"quiz.wnc", 0, "Trivia quiz (20 questions)", 8500, 2},
    {"rps.wnc", 0, "Rock paper scissors", 2800, 2},
    {"eightball.wnc", 0, "Magic 8-ball", 1800, 2},
    {"hangman.wnc", 0, "Hangman game", 5600, 2},
    {"mastermind.wnc", 0, "Mastermind code game", 5200, 2},
    {"battleship.wnc", 0, "Simple battleship", 6000, 2},
    {"slot.wnc", 0, "Slot machine", 3400, 2},
    {"blackjack.wnc", 0, "Blackjack card game", 9800, 2},
    {"roulette.wnc", 0, "Roulette simulator", 4800, 2},
    {"cowsay.wnc", 0, "Cow says (with animations)", 3500, 3},
    {"fortune.wnc", 0, "Fortune cookie (50 sayings)", 10800, 3},
    {"matrix.wnc", 0, "Matrix rain effect", 2200, 3},
    {"fire.wnc", 0, "Fire effect", 2800, 3},
    {"stars.wnc", 0, "Starfield effect", 2600, 3},
    {"asciiart.wnc", 0, "ASCII art gallery (30 arts)", 16500, 3},
    {"textanim.wnc", 0, "Animated text effects", 6200, 3},
    {"rainbow.wnc", 0, "Rainbow color effect", 3400, 3},
    {"banner.wnc", 0, "ASCII banner generator", 4100, 3},
    {"benchmark.wnc", 0, "CPU benchmark", 5800, 4},
    {"colors.wnc", 0, "Color test", 3200, 4},
    {"keytest.wnc", 0, "Keyboard test", 2900, 4},
    {"beep.wnc", 0, "Sound test", 1800, 4},
    {"testmath.wnc", 0, "Math functions test", 4600, 4},
    {"teststring.wnc", 0, "String operations test", 5200, 4},
    {"testarray.wnc", 0, "Array operations test", 4800, 4},
    {"debug.wnc", 0, "Debug helper", 5600, 4},
    {"profile.wnc", 0, "Performance profiler", 6300, 4}
};
static int script_count = sizeof(wnc_scripts) / sizeof(wnc_script_t);
static const char* category_names[] = {"System", "Utilities", "Games", "Demos", "Development"};

static const char* get_script_content(const char* name) {
    if(my_strcmp(name, "welcome.wnc") == 0) {
        return "print ================================================================================\n"
               "print                     WELCOME TO WNKA OS X32\n"
               "print ================================================================================\n"
               "print \n"
               "print [SYSTEM INFORMATION]\n"
               "run fetch\n"
               "print \n"
               "print [STORAGE]\n"
               "run df\n"
               "print \n"
               "print [SYSTEM UPTIME]\n"
               "run uptime\n"
               "print \n"
               "print ================================================================================\n"
               "print [QUICK START GUIDE]\n"
               "print ================================================================================\n"
               "print \n"
               "print 1. BASIC NAVIGATION\n"
               "print    ls              - list files in current directory\n"
               "print    cd <dir>        - change directory\n"
               "print    pwd             - show current path\n"
               "print    mkdir <name>    - create new directory\n"
               "print \n"
               "print 2. FILE OPERATIONS\n"
               "print    create <file>   - create empty file\n"
               "print    cat <file>      - display file content\n"
               "print    copy <src> <dst>- copy file\n"
               "print    move <src> <dst>- move or rename file\n"
               "print    delete <file>   - delete file\n"
               "print    hexedit <file>  - edit file in hex mode\n"
               "print \n"
               "print 3. SYSTEM COMMANDS\n"
               "print    reboot          - restart system\n"
               "print    shut            - shutdown computer\n"
               "print    halt            - halt system\n"
               "print    panic           - kernel panic (debug)\n"
               "print \n"
               "print 4. WNKC SCRIPTING\n"
               "print    wnkc <file.wnc> - execute WnkC script\n"
               "print    wnkc 'print Hello' - execute inline code\n"
               "print    wnkc vars       - show all variables\n"
               "print \n"
               "print 5. PACKAGE MANAGER\n"
               "print    wpm install <pkg> - install package from repo\n"
               "print    wpm list        - show installed packages\n"
               "print \n"
               "print 6. GRAPHICAL ENVIRONMENT\n"
               "print    ui              - start WnkUI desktop\n"
               "print    paint           - open Paint application\n"
               "print    piano           - play piano on PC Speaker\n"
               "print    calc            - calculator\n"
               "print \n"
               "print 7. GAMES\n"
               "print    pacman          - classic Pacman game\n"
               "print    snake           - Snake game\n"
               "print    flappy          - Flappy Bird clone\n"
               "print    mines           - Minesweeper\n"
               "print \n"
               "print 8. VISUAL EFFECTS\n"
               "print    matrix          - Matrix rain effect\n"
               "print    fire            - Fire effect\n"
               "print    plasma          - Plasma effect\n"
               "print    stars           - Starfield effect\n"
               "print \n"
               "print 9. NETWORK (if configured)\n"
               "print    net init        - initialize network\n"
               "print    ping <host>     - ping a host\n"
               "print    http <url>      - make HTTP request\n"
               "print \n"
               "print 10. UTILITIES\n"
               "print    clock           - ASCII clock\n"
               "print    fetch           - show system info\n"
               "print    df              - disk free space\n"
               "print    tree            - show directory tree\n"
               "print    find <name>     - search for files\n"
               "print \n"
               "print ================================================================================\n"
               "print For more information, type: help\n"
               "print ================================================================================\n"
               "print \n"
               "print Tip: Run help to see all commands!!!\n"
               "print Tip: Type 'ui' to start graphical desktop environment\n"
               "print Tip: Type 'wnkc welcome.wnc' to see this message again\n"
               "print \n";
    }
    if(my_strcmp(name, "quiz.wnc") == 0) {
        return "print =========================================\n"
               "print         TRIVIA QUIZ (20 Questions)\n"
               "print =========================================\n"
               "let score = 0\n"
               "print Q1: What is the capital of France?\n"
               "print 1.Berlin 2.Madrid 3.Paris 4.London\ninput a\nif a == 3 then let score = score + 1\n"
               "print Q2: What is 2+2?\ninput a\nif a == 4 then let score = score + 1\n"
               "print Q3: Who painted the Mona Lisa?\n"
               "print 1.Van Gogh 2.Da Vinci 3.Picasso 4.Rembrandt\ninput a\nif a == 2 then let score = score + 1\n"
               "print Q4: What is the largest planet?\n"
               "print 1.Earth 2.Mars 3.Jupiter 4.Saturn\ninput a\nif a == 3 then let score = score + 1\n"
               "print Q5: What year did WW2 end?\ninput a\nif a == 1945 then let score = score + 1\n"
               "print Q6: Who wrote Romeo and Juliet?\n"
               "print 1.Dickens 2.Hemingway 3.Shakespeare 4.Austen\ninput a\nif a == 3 then let score = score + 1\n"
               "print Q7: What is the square root of 64?\ninput a\nif a == 8 then let score = score + 1\n"
               "print Q8: What is the fastest animal?\n"
               "print 1.Lion 2.Cheetah 3.Leopard 4.Horse\ninput a\nif a == 2 then let score = score + 1\n"
               "print Q9: Who discovered penicillin?\n"
               "print 1.Pasteur 2.Curie 3.Fleming 4.Einstein\ninput a\nif a == 3 then let score = score + 1\n"
               "print Q10: What is the chemical symbol for Gold?\n"
               "print 1.Go 2.Gd 3.Au 4.Ag\ninput a\nif a == 3 then let score = score + 1\n"
               "print Q11: Who painted the Sistine Chapel?\n"
               "print 1.Da Vinci 2.Michelangelo 3.Raphael 4.Donatello\ninput a\nif a == 2 then let score = score + 1\n"
               "print Q12: What is the longest river?\n"
               "print 1.Amazon 2.Nile 3.Yangtze 4.Mississippi\ninput a\nif a == 2 then let score = score + 1\n"
               "print Q13: Who invented the telephone?\n"
               "print 1.Edison 2.Tesla 3.Bell 4.Marconi\ninput a\nif a == 3 then let score = score + 1\n"
               "print Q14: What is the freezing point of water?\ninput a\nif a == 0 then let score = score + 1\n"
               "print Q15: Who wrote 1984?\n"
               "print 1.Huxley 2.Orwell 3.Bradbury 4.Verne\ninput a\nif a == 2 then let score = score + 1\n"
               "print Q16: What is the capital of Japan?\ninput a\nif a == 'Tokyo' then let score = score + 1\n"
               "print Q17: Who painted Starry Night?\n"
               "print 1.Monet 2.Van Gogh 3.Renoir 4.Degas\ninput a\nif a == 2 then let score = score + 1\n"
               "print Q18: What is the speed of light?\ninput a\nif a == 299792458 then let score = score + 1\n"
               "print Q19: Who developed the theory of relativity?\n"
               "print 1.Newton 2.Galileo 3.Einstein 4.Hawking\ninput a\nif a == 3 then let score = score + 1\n"
               "print Q20: What is the smallest prime number?\ninput a\nif a == 2 then let score = score + 1\n"
               "print =========================================\n"
               "print Your score: $score/20\n"
               "if score == 20 then print PERFECT SCORE! Genius!\n"
               "if score >= 15 then print Excellent!\nif score >= 10 then print Good job!\n"
               "if score < 10 then print Keep learning!\n"
               "print =========================================\n";
    }
    if(my_strcmp(name, "asciiart.wnc") == 0) {
        return "print =========================================\n"
               "print         ASCII ART GALLERY\n"
               "print =========================================\n"
               "print 1.Dragon 2.Eagle 3.Wolf 4.Rose 5.Skull\n"
               "print 6.Heart 7.Sword 8.Castle 9.Ship 10.Tree\n"
               "print 11.Owl 12.Phoenix 13.Unicorn 14.Dragonfly\n"
               "print 15.Butterfly 16.Spider 17.Snake 18.Fish\n"
               "print 19.Cat 20.Dog 21.Rabbit 22.Fox 23.Bear\n"
               "print 24.Lion 25.Tiger 26.Elephant 27.Giraffe 28.Monkey\n"
               "print 29.Penguin 30.Dolphin\nEnter choice (1-30):\ninput choice\n"
               "if choice == 1 then print .--.\\n   |o_o |\\n   |:_/ |\\n  //   \\ \\\n (|     |)\\n/'\\_   _/`\\\n\\___)=(___/\n"
               "if choice == 2 then print      __\\n    .-'  '-.\\n   /       \\\\n   |   .   |\\n   \\     _/\\n    '-.__'\n"
               "if choice == 3 then print      /\\\n     /  \\\\n    /    \\\\n   /  /\\  \\\\n  /  /  \\  \\\\n /__/    \\__\\\n"
               "if choice == 4 then print     .--.\\n    /    \\\\n   |      |\\n   |  ()  |\\n    \\    /\\n     '--'\n"
               "if choice == 5 then print      ______\\n    .-\"\"\"\"\"-.\\n   /        \\\\n  |  O  O   |\\n  |    ^    |\\n  |  \\_/    |\\n   \\       /\\n    '-...-'\n"
               "if choice == 6 then print   .-\"\"\"\"\"-.\n  |       |\n  |  .-.  |\n  |  `-'  |\n   \\     /\n    `-.-'\n"
               "if choice == 7 then print      /\\\n     /  \\\\n    /    \\\\n   /      \\\\n  /________\\\n         |\n         |\n"
               "if choice == 8 then print      /\\\n     /  \\\\n    /____\\\n   /      \\\\n  /        \\\\n /__________\\\n"
               "if choice == 9 then print      |\\n     / \\\\n    |   |\n    |___|\n   /     \\\\n  /       \\\\n /________\\\n"
               "if choice == 10 then print      /\\\n     /  \\\\n    /    \\\\n   /______\\\n   |  ||  |\n   |  ||  |\n"
               "if choice == 11 then print      .---.\n     /     \\\n    | O   O |\n    |   ^   |\n     \\     /\n      `-.-'\n"
               "if choice == 12 then print        .\n       / \\\\n      /   \\\\n     /     \\\\n    /       \\\\n   /         \\\\n  /___________\\\n"
               "else print Art not found\n";
    }
    if(my_strcmp(name, "blackjack.wnc") == 0) {
        return "print =========================================\n"
               "print           BLACKJACK\n"
               "print =========================================\n"
               "let money = 100\nwhile money > 0\n"
               "    print You have $money coins\n"
               "    print Enter bet:\n    input bet\n"
               "    if bet > money then print Not enough money! break end\n"
               "    let player = 10 + (rand % 10)\n    let dealer = 10 + (rand % 10)\n"
               "    print Your card: $player\n    print Dealer shows: $dealer\n"
               "    while 1\n"
               "        print Hit or Stand? (h/s)\n        input move\n"
               "        if move == 'h' then\n"
               "            let card = 1 + (rand % 11)\n            let player = player + card\n"
               "            print You got: $card (Total: $player)\n"
               "            if player > 21 then print BUST! You lose! break end\n"
               "        else break end\n"
               "    end\n"
               "    if player <= 21 then\n"
               "        while dealer < 17\n"
               "            let card = 1 + (rand % 11)\n            let dealer = dealer + card\n"
               "            print Dealer takes: $card (Total: $dealer)\n"
               "        end\n"
               "        if dealer > 21 then print Dealer BUST! You win!\n            let money = money + bet\n"
               "        else if player > dealer then print You win!\n            let money = money + bet\n"
               "        else if player < dealer then print You lose!\n            let money = money - bet\n"
               "        else print Push! end\n"
               "    else let money = money - bet end\n"
               "end\nprint Game over!\n";
    }
    if(my_strcmp(name, "fortune.wnc") == 0) {
        return "let fortunes[50]\n"
               "fortunes[0] = 'You will have a great day'\n"
               "fortunes[1] = 'Good news is coming'\n"
               "fortunes[2] = 'Adventure awaits you'\n"
               "fortunes[3] = 'Trust your instincts'\n"
               "fortunes[4] = 'A surprise is waiting'\n"
               "fortunes[5] = 'Help is on the way'\n"
               "fortunes[6] = 'Stay positive'\n"
               "fortunes[7] = 'Great success is near'\n"
               "fortunes[8] = 'New opportunities arise'\n"
               "fortunes[9] = 'Your hard work pays off'\n"
               "fortunes[10] = 'A friend will help you'\n"
               "fortunes[11] = 'Today is your lucky day'\n"
               "fortunes[12] = 'Something amazing awaits'\n"
               "fortunes[13] = 'You will meet someone special'\n"
               "fortunes[14] = 'A journey begins soon'\n"
               "fortunes[15] = 'Happiness is around the corner'\n"
               "fortunes[16] = 'Your creativity shines'\n"
               "fortunes[17] = 'A problem will solve itself'\n"
               "fortunes[18] = 'You will learn something new'\n"
               "fortunes[19] = 'Peace and harmony prevail'\n"
               "fortunes[20] = 'An unexpected gift arrives'\n"
               "fortunes[21] = 'Your patience will be rewarded'\n"
               "fortunes[22] = 'A dream comes true'\n"
               "fortunes[23] = 'You will make someone smile'\n"
               "fortunes[24] = 'Success follows effort'\n"
               "fortunes[25] = 'The future looks bright'\n"
               "fortunes[26] = 'A new friendship forms'\n"
               "fortunes[27] = 'Your kindness returns'\n"
               "fortunes[28] = 'An old memory brings joy'\n"
               "fortunes[29] = 'You will find what you seek'\n"
               "fortunes[30] = 'Good luck follows you'\n"
               "fortunes[31] = 'A pleasant surprise awaits'\n"
               "fortunes[32] = 'Your efforts will be rewarded'\n"
               "fortunes[33] = 'New paths will open'\n"
               "fortunes[34] = 'You will overcome challenges'\n"
               "fortunes[35] = 'Joy comes from small things'\n"
               "fortunes[36] = 'Bold decisions lead to success'\n"
               "fortunes[37] = 'The stars align for you'\n"
               "fortunes[38] = 'Your wisdom grows daily'\n"
               "fortunes[39] = 'Happiness finds you'\n"
               "fortunes[40] = 'A new chapter begins'\n"
               "fortunes[41] = 'You are stronger than you know'\n"
               "fortunes[42] = 'Peace will find you'\n"
               "fortunes[43] = 'Small steps bring big changes'\n"
               "fortunes[44] = 'Your smile lights up others'\n"
               "fortunes[45] = 'A kind word changes someone'\n"
               "fortunes[46] = 'Today is a gift'\n"
               "fortunes[47] = 'You make a difference'\n"
               "fortunes[48] = 'Trust the journey'\n"
               "fortunes[49] = 'Your best days are ahead'\n"
               "let r = rand % 50\nprint =========================================\n"
               "print Fortune Cookie:\nprint $fortunes[r]\n"
               "print =========================================\n";
    }
    if(my_strcmp(name, "backup.wnc") == 0) {
        return "print =========================================\n"
               "print         SYSTEM BACKUP TOOL\n"
               "print =========================================\n"
               "print 1. Create backup\n2. List backups\n3. Restore backup\n4. Delete backup\n"
               "print Enter choice:\ninput choice\n"
               "if choice == 1 then\n"
               "    print Enter backup name:\n    input bname\n"
               "    print Creating backup: $bname\n"
               "    run mkdir /backup/$bname\n"
               "    run cp /etc/* /backup/$bname/ 2>/dev/null\n"
               "    run cp /bin/* /backup/$bname/bin/ 2>/dev/null\n"
               "    run cp /usr/programs/*.wnc /backup/$bname/ 2>/dev/null\n"
               "    print Backup complete!\n"
               "end\n"
               "if choice == 2 then\n    print Listing backups:\n    run ls /backup/\nend\n"
               "if choice == 3 then\n    print Enter backup name:\n    input bname\n"
               "    print Restoring from $bname\n"
               "    run cp /backup/$bname/* /etc/ 2>/dev/null\n"
               "    print Restore complete!\n"
               "end\n"
               "if choice == 4 then\n    print Enter backup name:\n    input bname\n"
               "    run rmdir /backup/$bname\n    print Deleted\nend\n";
    }
    if(my_strcmp(name, "sysinfo.wnc") == 0) {
        return "print =========================================\nprint           SYSTEM INFORMATION\nprint =========================================\nrun fetch\nrun df\nrun uptime\nprint CPU: \nrun cat /proc/cpuinfo\nprint Memory:\nrun cat /proc/meminfo\n";
    }
    if(my_strcmp(name, "process.wnc") == 0) {
        return "print =========================================\nprint         PROCESS MANAGER\nprint =========================================\nrun ps\nprint \nprint 1. Kill process\n2. Refresh\n3. Exit\ninput choice\nif choice == 1 then\n    print Enter PID:\n    input pid\n    run kill $pid\nend\nif choice == 2 then run ps\nif choice == 3 then print Exiting\n";
    }
    if(my_strcmp(name, "logview.wnc") == 0) {
        return "print =========================================\nprint         SYSTEM LOG VIEWER\nprint =========================================\nprint 1. View system log\n2. View kernel log\n3. Clear logs\ninput choice\nif choice == 1 then run cat /var/log/system.log\nif choice == 2 then run cat /var/log/kernel.log\nif choice == 3 then print Logs cleared (simulated)\n";
    }
    if(my_strcmp(name, "fetch.wnc") == 0) return "run fetch\n";
    if(my_strcmp(name, "uptime.wnc") == 0) return "run uptime\n";
    if(my_strcmp(name, "diskfree.wnc") == 0) return "run df\n";
    if(my_strcmp(name, "calc.wnc") == 0) return "print Enter first number:\ninput a\nprint Enter operator (+ - * /):\ninput op\nprint Enter second number:\ninput b\nif op == '+' then let r = a + b\nif op == '-' then let r = a - b\nif op == '*' then let r = a * b\nif op == '/' then if b != 0 then let r = a / b\nif op == '/' then if b == 0 then print Error: division by zero\nprint Result: $a $op $b = $r\n";
    if(my_strcmp(name, "calc_adv.wnc") == 0) return "let mem = 0\nprint Advanced Calculator (M for memory)\nwhile 1\n    print Enter: \n    input expr\n    if expr == 'exit' then break\n    if expr == 'M' then print Memory: $mem\nend\n";
    if(my_strcmp(name, "dice.wnc") == 0) return "print How many dice? (1-6)\ninput count\nlet total = 0\nlet i = 0\nwhile i < count\n    let roll = 1 + (rand % 6)\n    print Roll $i+1: $roll\n    let total = total + roll\n    let i = i + 1\nend\nprint Total: $total\n";
    if(my_strcmp(name, "random.wnc") == 0) return "print Enter min:\ninput min\nprint Enter max:\ninput max\nlet r = min + (rand % (max - min + 1))\nprint Random: $r\n";
    if(my_strcmp(name, "prime.wnc") == 0) return "print Enter number:\ninput n\nlet is_prime = 1\nlet d = 2\nwhile d * d <= n\n    if n % d == 0 then let is_prime = 0\n    let d = d + 1\nend\nif n < 2 then let is_prime = 0\nif is_prime == 1 then print $n is prime\nelse print $n is not prime\n";
    if(my_strcmp(name, "fibonacci.wnc") == 0) return "print How many numbers?\ninput n\nlet a = 0\nlet b = 1\nlet i = 0\nwhile i < n\n    print $a\n    let c = a + b\n    let a = b\n    let b = c\n    let i = i + 1\nend\n";
    if(my_strcmp(name, "guess.wnc") == 0) return "let secret = 1 + (rand % 100)\nlet tries = 0\nprint Guess (1-100):\nwhile 1\n    input guess\n    let tries = tries + 1\n    if guess == secret then\n        print Correct in $tries tries\n        break\n    end\n    if guess < secret then print Too low\n    if guess > secret then print Too high\nend\n";
    if(my_strcmp(name, "rps.wnc") == 0) return "print Rock/paper/scissors? (r/p/s)\ninput p\nlet c = rand % 3\nif c == 0 then let comp = 'r'\nif c == 1 then let comp = 'p'\nif c == 2 then let comp = 's'\nprint Computer: $comp\nif p == comp then print Draw\nelse if p == 'r' and comp == 's' then print You win\nelse if p == 's' and comp == 'p' then print You win\nelse if p == 'p' and comp == 'r' then print You win\nelse print You lose\n";
    if(my_strcmp(name, "eightball.wnc") == 0) return "let answers = ['Yes','No','Maybe','Ask later','Definitely','Never']\nprint Ask a question:\ninput q\nlet r = rand % 6\nprint Answer: $answers[r]\n";
    if(my_strcmp(name, "hangman.wnc") == 0) return "let word = 'wnka'\nlet guessed = '____'\nlet attempts = 6\nwhile attempts > 0 and guessed != word\n    print Word: $guessed\n    print Attempts: $attempts\n    input l\n    let found = 0\n    let i = 0\n    while i < 4\n        if word[i] == l then\n            let guessed[i] = l\n            let found = 1\n        end\n        let i = i + 1\n    end\n    if found == 0 then let attempts = attempts - 1\nend\nif guessed == word then print You win!\nelse print You lose! Word: $word\n";
    if(my_strcmp(name, "mastermind.wnc") == 0) return "let code = '1234'\nlet attempts = 10\nwhile attempts > 0\n    print Enter 4-digit guess:\n    input guess\n    if guess == code then\n        print Correct! You win\n        break\n    end\n    let attempts = attempts - 1\n    print Attempts left: $attempts\nend\n";
    if(my_strcmp(name, "battleship.wnc") == 0) return "let ship = 5\nlet attempts = 5\nlet hit = 0\nwhile attempts > 0 and hit == 0\n    print Guess position (1-10):\n    input pos\n    if pos == ship then\n        print Hit!\n        let hit = 1\n    else\n        print Miss\n        let attempts = attempts - 1\n    end\nend\n";
    if(my_strcmp(name, "slot.wnc") == 0) return "print Bet:\ninput bet\nlet r1 = rand % 7\nlet r2 = rand % 7\nlet r3 = rand % 7\nprint $r1 $r2 $r3\nif r1 == r2 and r2 == r3 then print Jackpot! Win $bet*10\nelse if r1 == r2 or r2 == r3 or r1 == r3 then print Match! Win $bet*2\nelse print You lose\n";
    if(my_strcmp(name, "timer.wnc") == 0) return "print Seconds:\ninput sec\nwhile sec > 0\n    print $sec sec\n    let sec = sec - 1\n    sleep 1\nend\nprint TIME'S UP!\n";
    if(my_strcmp(name, "stopwatch.wnc") == 0) return "print Press any key to start...\nrun getkey\nlet start = time\nprint Press any key to stop...\nrun getkey\nlet elapsed = time - start\nprint Elapsed: $elapsed sec\n";
    if(my_strcmp(name, "todo.wnc") == 0) return "print 1.Add 2.List 3.Clear\ninput choice\nif choice == 1 then\n    print Task:\n    input task\n    write /tmp/todo.txt $task\n    print Added\nend\nif choice == 2 then run cat /tmp/todo.txt\nif choice == 3 then delete /tmp/todo.txt\n";
    if(my_strcmp(name, "notes.wnc") == 0) return "print 1.Write 2.Read\ninput choice\nif choice == 1 then\n    print Filename:\n    input fname\n    print Text (end with .):\n    let text = ''\n    while 1\n        input line\n        if line == '.' then break\n        let text = text + line + '\n'\n    end\n    write $fname $text\nend\nif choice == 2 then\n    print Filename:\n    input fname\n    run cat $fname\nend\n";
    if(my_strcmp(name, "reminder.wnc") == 0) return "print 1.Set 2.Show\ninput choice\nif choice == 1 then\n    print Reminder:\n    input rem\n    write /tmp/reminder.txt $rem\nend\nif choice == 2 then run cat /tmp/reminder.txt\n";
    if(my_strcmp(name, "password_gen.wnc") == 0) return "let chars = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%'\nlet len = 16\nlet pass = ''\nlet i = 0\nwhile i < len\n    let idx = rand % 70\n    let pass = pass + chars[idx]\n    let i = i + 1\nend\nprint Password: $pass\n";
    if(my_strcmp(name, "benchmark.wnc") == 0) return "let start = time\nlet i = 0\nwhile i < 100000\n    let x = i * i\n    let i = i + 1\nend\nlet elapsed = time - start\nprint 100K iterations in $elapsed sec\n";
    if(my_strcmp(name, "cowsay.wnc") == 0) return "print Message:\ninput msg\nprint  __________\nprint < $msg >\nprint  ----------\nprint         \\   ^__^\nprint          \\  (oo)\\_______\nprint             (__)\\       )\\/\\\nprint                 ||----w |\nprint                 ||     ||\n";
    if(my_strcmp(name, "matrix.wnc") == 0) return "run matrix\n";
    if(my_strcmp(name, "fire.wnc") == 0) return "run fire\n";
    if(my_strcmp(name, "stars.wnc") == 0) return "run stars\n";
    if(my_strcmp(name, "colors.wnc") == 0) return "print Color test\n";
    if(my_strcmp(name, "keytest.wnc") == 0) return "print Press any key...\nrun getkey\nprint Key pressed!\n";
    if(my_strcmp(name, "beep.wnc") == 0) return "run beep\nprint Beep!\n";
    if(my_strcmp(name, "textanim.wnc") == 0) return "print Text animation demo\n";
    if(my_strcmp(name, "rainbow.wnc") == 0) return "print Rainbow effect\n";
    if(my_strcmp(name, "banner.wnc") == 0) return "banner\n";
    if(my_strcmp(name, "testmath.wnc") == 0) return "print Math test\n";
    if(my_strcmp(name, "teststring.wnc") == 0) return "print String test\n";
    if(my_strcmp(name, "testarray.wnc") == 0) return "print Array test\n";
    if(my_strcmp(name, "debug.wnc") == 0) return "print Debug helper\n";
    if(my_strcmp(name, "profile.wnc") == 0) return "run profile\n";
    if(my_strcmp(name, "restore.wnc") == 0) return "run wnakasxs_restore\n";
    if(my_strcmp(name, "roulette.wnc") == 0) return "print Roulette\n";
    
    return "print Script not found\n";
}

static void save_selected_components(void) {
    uint16_t components_buf[256] = {0};
    for(int i = 0; i < option_count && i < 16; i++) {
        if(install_options[i].enabled) components_buf[0] |= (1 << i);
    }
    for(int i = 0; i < script_count && i < 32; i++) {
        if(wnc_scripts[i].enabled) {
            if(i < 16) components_buf[1] |= (1 << i);
            else components_buf[2] |= (1 << (i - 16));
        }
    }
    components_buf[255] = 0x574E;
    write_sector(110, components_buf);
}

static void load_selected_components(void) {
    uint16_t components_buf[256] = {0};
    read_sector(110, components_buf);
    if(components_buf[255] != 0x574E) return;
    for(int i = 0; i < option_count && i < 16; i++) {
        install_options[i].enabled = (components_buf[0] >> i) & 1;
    }
    for(int i = 0; i < script_count && i < 16; i++) {
        wnc_scripts[i].enabled = (components_buf[1] >> i) & 1;
    }
    for(int i = 16; i < script_count && i < 32; i++) {
        wnc_scripts[i].enabled = (components_buf[2] >> (i - 16)) & 1;
    }
    install_options[0].enabled = 1;
}

static void select_components(void) {
    int selected = 0;
    int running = 1;
    int redraw = 1;
    
    while(running) {
        if(redraw) {
            clear_screen_bg(COLOR_GRAY);
            draw_shadow_window(10, 1, 60, 23, COLOR_BLUE, TXT_WHITE, "WNKA OS - Select Components");
            kprint_at("W/S:Move, Space:Toggle, Enter:Continue", 12, 3, (COLOR_BLACK << 4) | TXT_CYAN);
            
            int y = 6;
            for(int i = 0; i < option_count; i++) {
                uint8_t color = (i == selected) ? TXT_GREEN : TXT_WHITE;
                kprint_at(install_options[i].enabled ? "[X]" : "[ ]", 14, y, (COLOR_BLACK << 4) | color);
                kprint_at(install_options[i].name, 19, y, (COLOR_BLACK << 4) | color);
                y += 2;
            }
            redraw = 0;
        }
        move_cursor(79, 24);
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc < 0x80) {
                if(sc == 0x48 && selected > 0) { selected--; redraw = 1; }
                else if(sc == 0x50 && selected < option_count - 1) { selected++; redraw = 1; }
                else if(sc == 0x39) { install_options[selected].enabled = !install_options[selected].enabled; redraw = 1; }
                else if(sc == 0x1C) running = 0;
            }
            while(inb(0x64) & 1) inb(0x60);
        }
        inst_delay(30);
    }
    save_selected_components();
}

static void select_scripts_in_category(int category) {
    int selected = 0;
    int scroll = 0;
    int visible = 15;
    int redraw = 1;
    int running = 1;
    
    int cat_scripts[50];
    int cat_count = 0;
    for(int i = 0; i < script_count; i++) {
        if(wnc_scripts[i].category == category) {
            cat_scripts[cat_count++] = i;
        }
    }
    
    char title[64];
    my_sprintf(title, "Select %s Scripts", category_names[category]);
    
    while(running) {
        if(redraw) {
            clear_screen_bg(COLOR_GRAY);
            draw_shadow_window(10, 1, 60, 23, COLOR_BLUE, TXT_WHITE, title);
            kprint_at("W/S:Move, Space:Toggle, A:All in category, Enter:Back", 12, 3, (COLOR_BLACK << 4) | TXT_CYAN);
            
            int y = 6;
            for(int i = scroll; i < cat_count && i < scroll + visible; i++) {
                int idx = cat_scripts[i];
                uint8_t color = (i == selected) ? TXT_GREEN : TXT_WHITE;
                kprint_at(wnc_scripts[idx].enabled ? "[X]" : "[ ]", 14, y, (COLOR_BLACK << 4) | color);
                kprint_at(wnc_scripts[idx].name, 19, y, (COLOR_BLACK << 4) | color);
                
                char size_str[16];
                if(wnc_scripts[idx].size >= 1024) {
                    my_sprintf(size_str, "(%d KB)", wnc_scripts[idx].size / 1024);
                } else {
                    my_sprintf(size_str, "(%d B)", wnc_scripts[idx].size);
                }
                kprint_at(size_str, 45, y, (COLOR_BLACK << 4) | COLOR_DARK_GRAY);
                y++;
            }
            if(scroll > 0) kprint_at("↑", 58, 6, (COLOR_BLACK << 4) | TXT_CYAN);
            if(scroll + visible < cat_count) kprint_at("↓", 58, 20, (COLOR_BLACK << 4) | TXT_CYAN);
            
            int selected_idx = cat_scripts[selected];
            kprint_at("Description:", 14, 21, (COLOR_BLACK << 4) | TXT_YELLOW);
            if(wnc_scripts[selected_idx].desc && wnc_scripts[selected_idx].desc[0]) {
                kprint_at(wnc_scripts[selected_idx].desc, 14, 22, (COLOR_BLACK << 4) | TXT_WHITE);
            }
            redraw = 0;
        }
        
        move_cursor(79, 24);
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc < 0x80) {
                if(sc == 0x48 && selected > 0) { selected--; if(selected < scroll) scroll = selected; redraw = 1; }
                else if(sc == 0x50 && selected < cat_count - 1) { selected++; if(selected >= scroll + visible) scroll = selected - visible + 1; redraw = 1; }
                else if(sc == 0x39) { 
                    int idx = cat_scripts[selected];
                    wnc_scripts[idx].enabled = !wnc_scripts[idx].enabled;
                    redraw = 1;
                }
                else if(sc == 0x1E) {
                    int all_enabled = 1;
                    for(int i = 0; i < cat_count; i++) {
                        if(!wnc_scripts[cat_scripts[i]].enabled) { all_enabled = 0; break; }
                    }
                    for(int i = 0; i < cat_count; i++) {
                        wnc_scripts[cat_scripts[i]].enabled = !all_enabled;
                    }
                    redraw = 1;
                }
                else if(sc == 0x1C) running = 0;
            }
            while(inb(0x64) & 1) inb(0x60);
        }
        inst_delay(30);
    }
    save_selected_components();
}

static void select_wnc_scripts(void) {
    int selected = 0;
    int running = 1;
    int redraw = 1;
    
    while(running) {
        if(redraw) {
            clear_screen_bg(COLOR_GRAY);
            draw_shadow_window(10, 1, 60, 23, COLOR_BLUE, TXT_WHITE, "WnkC Scripts by Category");
            kprint_at("W/S:Move, Enter:Open Category, ESC:Back", 12, 3, (COLOR_BLACK << 4) | TXT_CYAN);
            
            int y = 6;
            for(int i = 0; i < 5; i++) {
                uint8_t color = (i == selected) ? TXT_GREEN : TXT_WHITE;
                int enabled_count = 0, total_count = 0;
                for(int j = 0; j < script_count; j++) {
                    if(wnc_scripts[j].category == i) {
                        total_count++;
                        if(wnc_scripts[j].enabled) enabled_count++;
                    }
                }
                kprint_at("[", 14, y, (COLOR_BLACK << 4) | color);
                kprint_at(category_names[i], 15, y, (COLOR_BLACK << 4) | color);
                kprint_at("]", 15 + my_strlen(category_names[i]), y, (COLOR_BLACK << 4) | color);
                kprint_at(" - ", 17 + my_strlen(category_names[i]), y, (COLOR_BLACK << 4) | color);
                kprint_int_at(enabled_count, 20 + my_strlen(category_names[i]), y, (COLOR_BLACK << 4) | TXT_GREEN);
                kprint_at("/", 22 + my_strlen(category_names[i]), y, (COLOR_BLACK << 4) | color);
                kprint_int_at(total_count, 24 + my_strlen(category_names[i]), y, (COLOR_BLACK << 4) | TXT_YELLOW);
                kprint_at(" scripts", 27 + my_strlen(category_names[i]), y, (COLOR_BLACK << 4) | color);
                y += 2;
            }
            
            int total_enabled = 0;
            for(int i = 0; i < script_count; i++) if(wnc_scripts[i].enabled) total_enabled++;
            kprint_at("Total selected: ", 14, 20, (COLOR_BLACK << 4) | TXT_CYAN);
            kprint_int_at(total_enabled, 30, 20, (COLOR_BLACK << 4) | TXT_GREEN);
            kprint_at("/", 33, 20, (COLOR_BLACK << 4) | TXT_WHITE);
            kprint_int_at(script_count, 35, 20, (COLOR_BLACK << 4) | TXT_YELLOW);
            kprint_at("[Enter] - Open category  [ESC] - Back", 14, 22, (COLOR_BLACK << 4) | TXT_CYAN);
            redraw = 0;
        }
        move_cursor(79, 24);
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc < 0x80) {
                if(sc == 0x48 && selected > 0) { selected--; redraw = 1; }
                else if(sc == 0x50 && selected < 4) { selected++; redraw = 1; }
                else if(sc == 0x1C) { select_scripts_in_category(selected); redraw = 1; }
                else if(sc == 0x01) running = 0;
            }
            while(inb(0x64) & 1) inb(0x60);
        }
        inst_delay(30);
    }
}

static void calculate_total_size(int* total_bytes, int* total_files, int* total_dirs) {
    *total_bytes = 0;
    *total_files = 0;
    *total_dirs = 12;
    
    *total_bytes += 13 * 100;
    *total_files += 13;
    *total_bytes += 500;
    *total_files += 5;
    
    for(int i = 0; i < script_count; i++) {
        if(wnc_scripts[i].enabled) {
            *total_bytes += wnc_scripts[i].size;
            (*total_files)++;
        }
    }
    
    if(install_options[1].enabled) {
        *total_bytes += 3 * 100;
        *total_files += 3;
        (*total_dirs)++;
    }
    if(install_options[3].enabled) {
        *total_bytes += 3 * 100;
        *total_files += 3;
        (*total_dirs)++;
    }
    *total_dirs += 6;
    if(install_options[6].enabled) (*total_dirs) += 2;
    *total_bytes += 1500;
    *total_files += 3;
}

static void stage3_install(void) {
    uint16_t settings_buf[256];
    read_sector(106, settings_buf);
    int cpu_selected = settings_buf[8];
    
    const char* cpu_list[] = {
        "Intel 386 (1985)", "Intel 486 (1989)", "Intel Pentium (1993)",
        "Intel Pentium MMX (1996)", "Intel Pentium Pro (1995)", "Intel Pentium II (1997)",
        "Intel Celeron (1998)", "Intel Pentium III (1999)", "Intel Pentium 4 (2000)",
        "Intel Celeron D (2004)", "Intel Pentium D (2005)", "Intel Core 2 Duo (2006)",
        "Intel Core 2 Quad (2007)", "Intel Core i3-530 (2010)", "Intel Core i5-750 (2009)",
        "Intel Core i7-920 (2008)", "Intel Core i3-2100 (2011)", "Intel Core i5-2500K (2011)",
        "Intel Core i7-2600K (2011)", "Intel Core i3-3220 (2012)", "Intel Core i5-3570K (2012)",
        "Intel Core i7-3770K (2012)", "Intel Core i3-4130 (2013)", "Intel Core i5-4670K (2013)",
        "Intel Core i7-4770K (2013)", "Intel Core i3-6100 (2015)", "Intel Core i5-6600K (2015)",
        "Intel Core i7-6700K (2015)", "Intel Core i3-7100 (2017)", "Intel Core i5-7600K (2017)",
        "Intel Core i7-7700K (2017)", "Intel Core i3-8100 (2017)", "Intel Core i5-8400 (2017)",
        "Intel Core i7-8700K (2017)", "Intel Core i9-9900K (2018)", "Intel Core i3-10100 (2020)",
        "Intel Core i5-10600K (2020)", "Intel Core i7-10700K (2020)", "Intel Core i9-10900K (2020)",
        "AMD K5 (1996)", "AMD K6 (1997)", "AMD K6-2 (1998)", "AMD K6-III (1999)",
        "AMD Athlon (1999)", "AMD Duron (2000)", "AMD Athlon XP (2001)", "AMD Sempron (2004)",
        "AMD Athlon 64 (2003)", "AMD Athlon 64 X2 (2005)", "AMD Phenom (2007)",
        "AMD Phenom II (2008)", "AMD Athlon II (2009)", "AMD FX-8150 (2011)",
        "AMD FX-8350 (2012)", "AMD A10-5800K (2012)", "AMD Ryzen 3 1200 (2017)",
        "AMD Ryzen 5 1600 (2017)", "AMD Ryzen 7 1700 (2017)", "AMD Ryzen 3 2200G (2018)",
        "AMD Ryzen 5 2600 (2018)", "AMD Ryzen 7 2700X (2018)", "AMD Ryzen 3 3100 (2020)",
        "AMD Ryzen 5 3600 (2019)", "AMD Ryzen 7 3700X (2019)", "AMD Ryzen 9 3900X (2019)",
        "AMD Ryzen 5 5600X (2020)", "AMD Ryzen 7 5800X (2020)", "AMD Ryzen 9 5900X (2020)",
        "AMD Ryzen 9 5950X (2020)", "VIA C3 (2000)", "Cyrix 6x86 (1996)"
    };
    
    if(cpu_selected < 0 || cpu_selected >= (int)(sizeof(cpu_list)/sizeof(cpu_list[0]))) {
        cpu_selected = 0;
    }
    
    int total_bytes = 0;
    int total_files = 0;
    int total_dirs = 0;
    
    load_selected_components();
    select_wnc_scripts();
    
    clear_screen_bg(COLOR_GRAY);
    update_time_display();
    
    draw_dframe(0, 0, 80, 3, COLOR_BLUE, TXT_WHITE);
    kprint_at("WNKA OS X32 - Stage 3 Installation", 25, 1, (COLOR_BLUE << 4) | TXT_YELLOW);
    draw_progress(50, 1, 25, 0, COLOR_GRAY, COLOR_GREEN);
    kprint_at("0%", 77, 1, (COLOR_BLUE << 4) | TXT_WHITE);
    
    draw_frame(0, 3, 25, 22, COLOR_GRAY, TXT_WHITE);
    kprint_at("System Information", 4, 4, (COLOR_GRAY << 4) | TXT_CYAN);
    
    uint32_t cpu_mhz = 0;
    uint32_t start, end;
    __asm__ volatile("rdtsc" : "=A"(start));
    for(volatile int i = 0; i < 1000000; i++);
    __asm__ volatile("rdtsc" : "=A"(end));
    cpu_mhz = (end - start) / 1000000;
    if(cpu_mhz < 1) cpu_mhz = 33;
    
    kprint_at("CPU:", 2, 6, (COLOR_BLACK << 4) | TXT_WHITE);
    kprint_at(cpu_list[cpu_selected], 2, 7, (COLOR_BLACK << 4) | TXT_GREEN);
    kprint_int_at(cpu_mhz, 2, 8, (COLOR_BLACK << 4) | TXT_GREEN);
    kprint_at(" MHz", 2, 9, (COLOR_BLACK << 4) | TXT_WHITE);
    
    kprint_at("RAM:", 2, 11, (COLOR_BLACK << 4) | TXT_WHITE);
    kprint_int_at(ram_mb_detected, 2, 12, (COLOR_BLACK << 4) | TXT_GREEN);
    kprint_at(" MB", 2, 13, (COLOR_BLACK << 4) | TXT_WHITE);
    
    uint16_t identify_buf[256];
    outb(ata_base_port + 6, 0xA0);
    outb(ata_base_port + 7, 0xEC);
    for(volatile int i = 0; i < 100000; i++);
    uint8_t status = inb(ata_base_port + 7);
    uint32_t disk_gb = 2;
    if(status != 0 && status != 0xFF) {
        for(int i = 0; i < 256; i++) identify_buf[i] = inw(ata_base_port);
        uint32_t sectors = identify_buf[60] | (identify_buf[61] << 16);
        disk_gb = (sectors * 512) / (1024 * 1024 * 1024);
    }
    
    kprint_at("Disk:", 2, 15, (COLOR_BLACK << 4) | TXT_WHITE);
    kprint_int_at(disk_gb, 2, 16, (COLOR_BLACK << 4) | TXT_GREEN);
    kprint_at(" GB", 2, 17, (COLOR_BLACK << 4) | TXT_WHITE);
    
    kprint_at("License:", 2, 19, (COLOR_BLACK << 4) | TXT_WHITE);
    kprint_at("ACTIVE", 2, 20, (COLOR_BLACK << 4) | TXT_GREEN);
    
    draw_frame(55, 3, 25, 22, COLOR_BLUE, TXT_WHITE);
    kprint_at("Why WNKA OS?", 60, 4, (COLOR_BLUE << 4) | TXT_YELLOW);
    
    int current_ad = 0;
    uint32_t last_ad_change = seconds;
    
    const char* first_ad = ads[0];
    int ad_line = 6;
    int ad_pos = 0;
    while(first_ad[ad_pos] && ad_line < 23) {
        char s[2] = {first_ad[ad_pos], 0};
        kprint_at(s, 57 + (ad_pos % 20), ad_line, (COLOR_BLUE << 4) | TXT_GREEN);
        ad_pos++;
        if(ad_pos % 20 == 0) ad_line++;
    }
    
    draw_frame(25, 3, 30, 22, COLOR_GRAY, TXT_WHITE);
    kprint_at("Installation Progress", 30, 4, (COLOR_GRAY << 4) | TXT_CYAN);
    
    uint32_t start_time = seconds;
    uint16_t root_sector = 100;
    uint16_t new_sector;
    uint16_t etc_sector, home_sector, usr_sector, var_sector, sxs_sector;
    uint16_t bin_sector, boot_sector, dev_sector, mnt_sector, proc_sector, tmp_sector;
    uint16_t opt_sector, programs_sector, games_sector, demos_sector;
    uint16_t autorun_sector, enabled_sector, disabled_sector;
    
    const char* dirs[] = {"bin", "boot", "dev", "etc", "home", "mnt", "proc", "tmp", "usr", "var", "WnkaSXS", "opt"};
    uint16_t dir_sectors[12];
    int total_steps = 12 + 6 + 1 + script_count + 3;
    int current_step = 0;

    for(int i = 0; i < 12; i++) {
        create_dir(root_sector, dirs[i], &new_sector);
        dir_sectors[i] = new_sector;
        current_step++;
        int percent = (current_step * 100) / total_steps;
        draw_progress(28, 7, 24, percent, COLOR_GRAY, COLOR_GREEN);
        kprint_int_at(percent, 54, 7, (COLOR_BLACK << 4) | TXT_GREEN);
        kprint_at("%", 56, 7, (COLOR_BLACK << 4) | TXT_WHITE);
        
        if(seconds - last_ad_change >= 10) {
            last_ad_change = seconds;
            current_ad = (current_ad + 1) % ad_count;
            for(int ad_y = 6; ad_y <= 22; ad_y++) {
                for(int ad_x = 57; ad_x < 78; ad_x++) {
                    put_pixel(ad_x, ad_y, COLOR_BLUE, TXT_WHITE, ' ');
                }
            }
            const char* ad = ads[current_ad];
            ad_line = 6;
            ad_pos = 0;
            while(ad[ad_pos] && ad_line < 23) {
                char s[2] = {ad[ad_pos], 0};
                kprint_at(s, 57 + (ad_pos % 20), ad_line, (COLOR_BLUE << 4) | TXT_GREEN);
                ad_pos++;
                if(ad_pos % 20 == 0) ad_line++;
            }
        }
        
        inst_delay(20);
    }
    
    bin_sector = dir_sectors[0];
    boot_sector = dir_sectors[1];
    dev_sector = dir_sectors[2];
    etc_sector = dir_sectors[3];
    home_sector = dir_sectors[4];
    mnt_sector = dir_sectors[5];
    proc_sector = dir_sectors[6];
    tmp_sector = dir_sectors[7];
    usr_sector = dir_sectors[8];
    var_sector = dir_sectors[9];
    sxs_sector = dir_sectors[10];
    opt_sector = dir_sectors[11];
    
    create_dir(usr_sector, "bin", &new_sector);
    create_dir(usr_sector, "lib", &new_sector);
    create_dir(usr_sector, "share", &new_sector);
    create_dir(var_sector, "log", &new_sector);
    create_dir(var_sector, "tmp", &new_sector);
    create_dir(etc_sector, "init.d", &new_sector);
    create_dir(opt_sector, "bin", &new_sector);
    create_dir(opt_sector, "lib", &new_sector);
    create_dir(usr_sector, "programs", &programs_sector);
    create_dir(usr_sector, "games", &games_sector);
    create_dir(usr_sector, "demos", &demos_sector);
    
    current_step += 6;
    int percent = (current_step * 100) / total_steps;
    draw_progress(28, 7, 24, percent, COLOR_GRAY, COLOR_GREEN);
    kprint_int_at(percent, 54, 7, (COLOR_BLACK << 4) | TXT_GREEN);
    kprint_at("%", 56, 7, (COLOR_BLACK << 4) | TXT_WHITE);
    inst_delay(50);
    
    uint16_t user_sector;
    create_dir(home_sector, username, &user_sector);
    current_step++;
    percent = (current_step * 100) / total_steps;
    draw_progress(28, 7, 24, percent, COLOR_GRAY, COLOR_GREEN);
    kprint_int_at(percent, 54, 7, (COLOR_BLACK << 4) | TXT_GREEN);
    kprint_at("%", 56, 7, (COLOR_BLACK << 4) | TXT_WHITE);
    inst_delay(50);
    
    char sys_config[512];
    my_sprintf(sys_config, "hostname=%s\nram=%d\nmouse=%d\nmonitor=%d\nsound=%d\nnetwork=%d\ncpu_type=%d\ncpu_name=%s\nversion=1.0\nbuild=1376\n", 
        username, user_ram_mb, user_mouse, user_monitor, user_sound, user_network, cpu_selected, cpu_list[cpu_selected]);
    create_file(etc_sector, "system.cfg", sys_config);
    create_file(etc_sector, "shadow", "");
    create_file(etc_sector, "hostname", username);
    create_file(etc_sector, "issue", "WNKA OS v1.0\nLogin: ");
    create_file(etc_sector, "fstab", "# /etc/fstab\n/dev/hda1  /  wnka  rw\nproc /proc proc defaults\n");
    
    create_file(bin_sector, "help", "#!/bin/wnkc\nrun help\n");
    create_file(bin_sector, "wnkc", "#!/bin/wnkc\nrun wnkc\n");
    create_file(bin_sector, "cls", "#!/bin/wnkc\nrun cls\n");
    create_file(bin_sector, "clear", "#!/bin/wnkc\nrun cls\n");
    create_file(bin_sector, "reboot", "#!/bin/wnkc\nrun reboot\n");
    create_file(bin_sector, "shut", "#!/bin/wnkc\nrun shutdown\n");
    create_file(bin_sector, "shutdown", "#!/bin/wnkc\nrun shutdown\n");
    create_file(bin_sector, "halt", "#!/bin/wnkc\nrun halt\n");
    create_file(bin_sector, "about", "#!/bin/wnkc\nrun about\n");
    create_file(bin_sector, "fetch", "#!/bin/wnkc\nrun fetch\n");
    create_file(bin_sector, "beep", "#!/bin/wnkc\nrun beep\n");
    create_file(bin_sector, "say", "#!/bin/wnkc\nrun say\n");
    create_file(bin_sector, "hello", "#!/bin/wnkc\nrun hello\n");
    create_file(bin_sector, "sleep", "#!/bin/wnkc\nrun sleep\n");
    create_file(bin_sector, "crashme", "#!/bin/wnkc\nrun crashme\n");
    create_file(bin_sector, "panic", "#!/bin/wnkc\nrun panic\n");
    create_file(bin_sector, "time", "#!/bin/wnkc\nrun time\n");
    create_file(bin_sector, "timer", "#!/bin/wnkc\nrun timer\n");
    create_file(bin_sector, "install", "#!/bin/wnkc\nrun install\n");
    create_file(bin_sector, "ls", "#!/bin/wnkc\nrun ls\n");
    create_file(bin_sector, "cd", "#!/bin/wnkc\nrun cd\n");
    create_file(bin_sector, "cat", "#!/bin/wnkc\nrun cat\n");
    create_file(bin_sector, "create", "#!/bin/wnkc\nrun create\n");
    create_file(bin_sector, "delete", "#!/bin/wnkc\nrun delete\n");
    create_file(bin_sector, "rm", "#!/bin/wnkc\nrun delete\n");
    create_file(bin_sector, "mkdir", "#!/bin/wnkc\nrun mkdir\n");
    create_file(bin_sector, "rmdir", "#!/bin/wnkc\nrun rmdir\n");
    create_file(bin_sector, "copy", "#!/bin/wnkc\nrun copy\n");
    create_file(bin_sector, "cp", "#!/bin/wnkc\nrun copy\n");
    create_file(bin_sector, "move", "#!/bin/wnkc\nrun move\n");
    create_file(bin_sector, "mv", "#!/bin/wnkc\nrun move\n");
    create_file(bin_sector, "rename", "#!/bin/wnkc\nrun rename\n");
    create_file(bin_sector, "find", "#!/bin/wnkc\nrun find\n");
    create_file(bin_sector, "pwd", "#!/bin/wnkc\nrun pwd\n");
    create_file(bin_sector, "tree", "#!/bin/wnkc\nrun tree\n");
    create_file(bin_sector, "df", "#!/bin/wnkc\nrun df\n");
    create_file(bin_sector, "stat", "#!/bin/wnkc\nrun stat\n");
    create_file(bin_sector, "hexdump", "#!/bin/wnkc\nrun hexdump\n");
    create_file(bin_sector, "hexedit", "#!/bin/wnkc\nrun hexedit\n");
    create_file(bin_sector, "format", "#!/bin/wnkc\nrun format\n");
    create_file(bin_sector, "lowformat", "#!/bin/wnkc\nrun lowformat\n");
    create_file(bin_sector, "diskinfo", "#!/bin/wnkc\nrun diskinfo\n");
    create_file(bin_sector, "diskbench", "#!/bin/wnkc\nrun diskbench\n");
    create_file(bin_sector, "speed", "#!/bin/wnkc\nrun speed\n");
    create_file(bin_sector, "verify", "#!/bin/wnkc\nrun verify\n");
    create_file(bin_sector, "filldisk", "#!/bin/wnkc\nrun filldisk\n");
    create_file(bin_sector, "filltest", "#!/bin/wnkc\nrun filltest\n");
    create_file(bin_sector, "stresstest", "#!/bin/wnkc\nrun stresstest\n");
    create_file(bin_sector, "zerofill", "#!/bin/wnkc\nrun zerofill\n");
    create_file(bin_sector, "finit", "#!/bin/wnkc\nrun finit\n");
    create_file(bin_sector, "fformat", "#!/bin/wnkc\nrun fformat\n");
    create_file(bin_sector, "fcreate", "#!/bin/wnkc\nrun fcreate\n");
    create_file(bin_sector, "fwrite", "#!/bin/wnkc\nrun fwrite\n");
    create_file(bin_sector, "fcat", "#!/bin/wnkc\nrun fcat\n");
    create_file(bin_sector, "fls", "#!/bin/wnkc\nrun fls\n");
    create_file(bin_sector, "fdel", "#!/bin/wnkc\nrun fdel\n");
    create_file(bin_sector, "ftest", "#!/bin/wnkc\nrun ftest\n");
    create_file(bin_sector, "pacman", "#!/bin/wnkc\nrun pacman\n");
    create_file(bin_sector, "flappy", "#!/bin/wnkc\nrun flappy\n");
    create_file(bin_sector, "snake", "#!/bin/wnkc\nrun snake\n");
    create_file(bin_sector, "matrix", "#!/bin/wnkc\nrun matrix\n");
    create_file(bin_sector, "fire", "#!/bin/wnkc\nrun fire\n");
    create_file(bin_sector, "rain", "#!/bin/wnkc\nrun rain\n");
    create_file(bin_sector, "stars", "#!/bin/wnkc\nrun stars\n");
    create_file(bin_sector, "plasma", "#!/bin/wnkc\nrun plasma\n");
    create_file(bin_sector, "waves", "#!/bin/wnkc\nrun waves\n");
    create_file(bin_sector, "tunnel", "#!/bin/wnkc\nrun tunnel\n");
    create_file(bin_sector, "menus", "#!/bin/wnkc\nrun menus\n");
    create_file(bin_sector, "art", "#!/bin/wnkc\nrun art\n");
    create_file(bin_sector, "screensaver", "#!/bin/wnkc\nrun screensaver\n");
    create_file(bin_sector, "clock", "#!/bin/wnkc\nrun clock\n");
    create_file(bin_sector, "calc", "#!/bin/wnkc\nrun calc\n");
    create_file(bin_sector, "galc", "#!/bin/wnkc\nrun galc\n");
    create_file(bin_sector, "paint", "#!/bin/wnkc\nrun paint\n");
    create_file(bin_sector, "piano", "#!/bin/wnkc\nrun piano\n");
    create_file(bin_sector, "notepad", "#!/bin/wnkc\nrun notepad\n");
    create_file(bin_sector, "edit", "#!/bin/wnkc\nrun edit\n");
    create_file(bin_sector, "ide", "#!/bin/wnkc\nrun ide\n");
    create_file(bin_sector, "tcc", "#!/bin/wnkc\nrun tcc\n");
    create_file(bin_sector, "wnkc", "#!/bin/wnkc\nrunscript\n");
    create_file(bin_sector, "vga", "#!/bin/wnkc\nrun vga\n");
    create_file(bin_sector, "vesa", "#!/bin/wnkc\nrun vesa\n");
    create_file(bin_sector, "vesainit", "#!/bin/wnkc\nrun vesainit\n");
    create_file(bin_sector, "vesademo", "#!/bin/wnkc\nrun vesademo\n");
    create_file(bin_sector, "vesatest", "#!/bin/wnkc\nrun vesatest\n");
    create_file(bin_sector, "vesablue", "#!/bin/wnkc\nrun vesablue\n");
    create_file(bin_sector, "ui", "#!/bin/wnkc\nrun ui\n");
    create_file(bin_sector, "gui", "#!/bin/wnkc\nrun gui\n");
    create_file(bin_sector, "fm", "#!/bin/wnkc\nrun fm\n");
    create_file(bin_sector, "ramfs", "#!/bin/wnkc\nrun ramfs\n");
    create_file(bin_sector, "monitor", "#!/bin/wnkc\nrun monitor\n");
    create_file(bin_sector, "theme", "#!/bin/wnkc\nrun theme\n");
    create_file(bin_sector, "sound", "#!/bin/wnkc\nrun sound\n");
    create_file(bin_sector, "wpm", "#!/bin/wnkc\nrun wpm\n");
    create_file(bin_sector, "uidev", "#!/bin/wnkc\nrun uidev\n");
    create_file(bin_sector, "sheet", "#!/bin/wnkc\nrun sheet\n");
    create_file(bin_sector, "slides", "#!/bin/wnkc\nrun slides\n");
    create_file(bin_sector, "netinit", "#!/bin/wnkc\nrun netinit\n");
    create_file(bin_sector, "ping", "#!/bin/wnkc\nrun ping\n");
    create_file(bin_sector, "dns", "#!/bin/wnkc\nrun dns\n");
    create_file(bin_sector, "browse", "#!/bin/wnkc\nrun browse\n");
    create_file(bin_sector, "http", "#!/bin/wnkc\nrun http\n");
    create_file(bin_sector, "netstat", "#!/bin/wnkc\nrun netstat\n");
    create_file(bin_sector, "ps", "#!/bin/wnkc\nrun ps\n");
    create_file(bin_sector, "kill", "#!/bin/wnkc\nrun kill\n");
    create_file(bin_sector, "multitest", "#!/bin/wnkc\nrun multitest\n");
    create_file(bin_sector, "bg", "#!/bin/wnkc\nrun bg\n");
    create_file(bin_sector, "jobs", "#!/bin/wnkc\nrun jobs\n");
    create_file(bin_sector, "fg", "#!/bin/wnkc\nrun fg\n");
    create_file(bin_sector, "bgkill", "#!/bin/wnkc\nrun bgkill\n");
    create_file(bin_sector, "copy", "#!/bin/wnkc\nrun copy\n");
    create_file(bin_sector, "paste", "#!/bin/wnkc\nrun paste\n");
    create_file(bin_sector, "key", "#!/bin/wnkc\nrun key\n");
    create_file(bin_sector, "secret", "#!/bin/wnkc\nrun secret\n");
    create_file(bin_sector, "mykey", "#!/bin/wnkc\nrun mykey\n");
    create_file(bin_sector, "coin", "#!/bin/wnkc\nrun coin\n");
    create_file(bin_sector, "dice", "#!/bin/wnkc\nrun dice\n");
    create_file(bin_sector, "rps", "#!/bin/wnkc\nrun rps\n");
    create_file(bin_sector, "guess", "#!/bin/wnkc\nrun guess\n");
    create_file(bin_sector, "8ball", "#!/bin/wnkc\nrun 8ball\n");
    create_file(bin_sector, "fortune", "#!/bin/wnkc\nrun fortune\n");
    create_file(bin_sector, "cowsay", "#!/bin/wnkc\nrun cowsay\n");
    create_file(bin_sector, "linux", "#!/bin/wnkc\nrun linux\n");
    create_file(bin_sector, "os", "#!/bin/wnkc\nrun os\n");
    create_file(bin_sector, "dostest", "#!/bin/wnkc\nrun dostest\n");
    create_file(bin_sector, "mode", "#!/bin/wnkc\nrun mode\n");
    create_file(bin_sector, "modes", "#!/bin/wnkc\nrun modes\n");
    create_file(bin_sector, "modeinfo", "#!/bin/wnkc\nrun modeinfo\n");
    create_file(bin_sector, "modeauto", "#!/bin/wnkc\nrun modeauto\n");
    create_file(bin_sector, "modenext", "#!/bin/wnkc\nrun modenext\n");
    create_file(bin_sector, "modeprev", "#!/bin/wnkc\nrun modeprev\n");
    create_file(bin_sector, "80x25", "#!/bin/wnkc\nrun 80x25\n");
    create_file(bin_sector, "80x50", "#!/bin/wnkc\nrun 80x50\n");
    create_file(bin_sector, "132x43", "#!/bin/wnkc\nrun 132x43\n");
    create_file(bin_sector, "132x60", "#!/bin/wnkc\nrun 132x60\n");
    create_file(bin_sector, "initfs", "#!/bin/wnkc\nrun initfs\n");
    create_file(bin_sector, "initds", "#!/bin/wnkc\nrun initds\n");
    create_file(bin_sector, "testds", "#!/bin/wnkc\nrun testds\n");
    create_file(bin_sector, "check", "#!/bin/wnkc\nrun check\n");
    create_file(bin_sector, "menu", "#!/bin/wnkc\nrun menu\n");
    create_file(bin_sector, "reformat", "#!/bin/wnkc\nrun reformat\n");
    create_file(bin_sector, "scanpci", "#!/bin/wnkc\nrun scanpci\n");
    create_file(bin_sector, "memtest", "#!/bin/wnkc\nrun memtest\n");
    create_file(bin_sector, "pci", "#!/bin/wnkc\nrun pci\n");
    create_file(bin_sector, "wnkasxs", "#!/bin/wnkc\nrun wnakasxs\n");
    create_file(bin_sector, "wnkasxs_restore", "#!/bin/wnkc\nrun wnakasxs_restore\n");
    
    current_step += 5;
    percent = (current_step * 100) / total_steps;
    draw_progress(28, 7, 24, percent, COLOR_GRAY, COLOR_GREEN);
    kprint_int_at(percent, 54, 7, (COLOR_BLACK << 4) | TXT_GREEN);
    kprint_at("%", 56, 7, (COLOR_BLACK << 4) | TXT_WHITE);
    for(int i = 0; i < script_count; i++) {
        if(wnc_scripts[i].enabled) {
            create_file(programs_sector, wnc_scripts[i].name, get_script_content(wnc_scripts[i].name));
            current_step++;
            percent = (current_step * 100) / total_steps;
            draw_progress(28, 7, 24, percent, COLOR_GRAY, COLOR_GREEN);
            kprint_int_at(percent, 54, 7, (COLOR_BLACK << 4) | TXT_GREEN);
            kprint_at("%", 56, 7, (COLOR_BLACK << 4) | TXT_WHITE);
            inst_delay(5);
        }
    }
    
    if(install_options[1].enabled) {
        create_file(games_sector, "pacman.wnc", "print Run 'pacman' to play\n");
        create_file(games_sector, "flappy.wnc", "print Run 'flappy' to play\n");
        create_file(games_sector, "snake.wnc", "print Run 'snake' to play\n");
    }
    if(install_options[3].enabled) {
        create_file(demos_sector, "matrix.wnc", "run matrix\n");
        create_file(demos_sector, "fire.wnc", "run fire\n");
        create_file(demos_sector, "plasma.wnc", "run plasma\n");
    }
    create_dir(etc_sector, "autorun", &autorun_sector);
    create_dir(autorun_sector, "enabled", &enabled_sector);
    create_dir(autorun_sector, "disabled", &disabled_sector);
    
    const char* autorun_conf = 
        "# WNKA OS Autorun Configuration\n"
        "mode = folder\n"
        "delay = 1\n"
        "show_output = yes\n"
        "continue_on_error = yes\n";
    create_file(autorun_sector, "autorun.conf", autorun_conf);
    
    const char* example_script = 
        "print \"Welcome to WNKA OS\"\nrun fetch\n";
    create_file(disabled_sector, "example.wnc", example_script);
    
    current_step += 3;
    percent = (current_step * 100) / total_steps;
    draw_progress(28, 7, 24, percent, COLOR_GRAY, COLOR_GREEN);
    kprint_int_at(percent, 54, 7, (COLOR_BLACK << 4) | TXT_GREEN);
    kprint_at("%", 56, 7, (COLOR_BLACK << 4) | TXT_WHITE);
    inst_delay(30);
    char welcome_user[256];
    my_sprintf(welcome_user, "print Welcome to WNKA OS, %s\nprint Type 'help' for commands\n", username);
    create_file(user_sector, ".welcome.wnc", welcome_user);
    create_file(user_sector, ".bashrc", "alias ll='ls -l'\nalias cls='clear'\nexport PS1='\\u@\\h:\\w$ '\n");
    create_file(user_sector, ".history", "");
    
    calculate_total_size(&total_bytes, &total_files, &total_dirs);
    
    char readme[1024];
    my_sprintf(readme, 
        "WNKA OS X32 v1.0\n================\n\n"
        "Installation completed!\n\n"
        "Total size: %d bytes (%d KB)\n"
        "Directories: %d\n"
        "Files: %d\n\n"
        "CPU: %s\n"
        "RAM: %d MB\n\n"
        "Commands in: /bin\n"
        "WnkC scripts in: /usr/programs/\n"
        "Games in: /usr/games/\n"
        "Demos in: /usr/demos/\n\n"
        "Autorun scripts in: /etc/autorun/enabled/\n"
        "Type 'help' to see all commands\n\n"
        "(C) Wnka-Software 2025-2026\n", 
        total_bytes, total_bytes/1024, total_dirs, total_files, cpu_list[cpu_selected], ram_mb_detected);
    create_file(root_sector, "README.txt", readme);
    
    if(install_options[9].enabled) {
        create_file(root_sector, "autoexec.wnc", "runscript /usr/programs/welcome.wnc\n");
    }
    
    char install_info[256];
    my_sprintf(install_info, "installed=1\ndate=%d\nuser=%s\ncpu=%s\nversion=1.0\nbuild=1376\nsize=%d\nfiles=%d\ndirs=%d\n", 
        seconds, username, cpu_list[cpu_selected], total_bytes, total_files, total_dirs);
    create_file(root_sector, ".installed", install_info);
    
    if(install_options[10].enabled) {
        create_file(sxs_sector, "README.txt", "WnkaSXS - System backup. DO NOT DELETE!\n");
    }
    
    uint16_t empty_buf[256];
    for(int i = 0; i < 256; i++) empty_buf[i] = 0;
    write_sector(101, empty_buf);
    write_sector(102, empty_buf);
    
    percent = 100;
    draw_progress(28, 7, 24, percent, COLOR_GRAY, COLOR_GREEN);
    kprint_int_at(percent, 54, 7, (COLOR_BLACK << 4) | TXT_GREEN);
    kprint_at("%", 56, 7, (COLOR_BLACK << 4) | TXT_WHITE);
    inst_delay(500);
    
    clear_screen_bg(COLOR_GRAY);
    
    char info[256];
    my_sprintf(info, "Total size: %d KB", total_bytes/1024);
    
    draw_dframe(10, 5, 60, 14, COLOR_BLUE, TXT_WHITE);
    kprint_at("INSTALLATION COMPLETE", 25, 7, (COLOR_BLUE << 4) | TXT_YELLOW);
    kprint_at("WNKA OS X32 has been successfully installed!", 18, 9, (COLOR_BLACK << 4) | TXT_GREEN);
    kprint_at(info, 18, 11, (COLOR_BLACK << 4) | TXT_CYAN);
    kprint_at("Files installed: ", 18, 12, (COLOR_BLACK << 4) | TXT_WHITE);
    kprint_int_at(total_files, 36, 12, (COLOR_BLACK << 4) | TXT_GREEN);
    kprint_at("CPU: ", 18, 13, (COLOR_BLACK << 4) | TXT_CYAN);
    kprint_at(cpu_list[cpu_selected], 24, 13, (COLOR_BLACK << 4) | TXT_GREEN);
    
    kprint_at("Rebooting in 3 seconds...", 22, 16, (COLOR_BLACK << 4) | TXT_YELLOW);
    
    for(int i = 3; i > 0; i--) {
        kprint_at("   ", 48, 16, (COLOR_BLACK << 4) | TXT_YELLOW);
        kprint_int_at(i, 48, 16, (COLOR_BLACK << 4) | TXT_RED);
        inst_delay(1000);
    }
    
    inst_delay(1000);
}

static int check_install_stage(void) {
    char buffer[256];
    
    read_install_config(0, buffer, 256);
    
    if(my_strstr(buffer, "installation_stage_1_completed = true") != NULL) {
        read_install_config(1, buffer, 256);
        
        if(my_strstr(buffer, "stage2_completed=true") != NULL) {
            return 3;
        }
        return 2;
    }
    return 1;
}
static void inst_draw_complete(void) {
    clear_screen_bg(COLOR_GRAY);
    
    draw_window(20, 4, 40, 16, COLOR_BLUE);
    kprint_at("Installation Complete", 24, 6, (COLOR_BLUE << 4) | COLOR_YELLOW);
    kprint_at("WNKA OS installed successfully!", 21, 8, (COLOR_BLACK << 4) | COLOR_GREEN);
    kprint_at("", 20, 9, (COLOR_BLACK << 4) | COLOR_WHITE);
    kprint_at("Press any key to reboot...", 23, 19, (COLOR_BLACK << 4) | COLOR_YELLOW);
}

static void inst_show_progress(void) {
    clear_screen_bg(COLOR_GRAY);
    
    draw_window(15, 5, 50, 12, COLOR_BLUE);
    kprint_at("Installing WNKA OS", 28, 7, (COLOR_BLUE << 4) | COLOR_WHITE);
    kprint_at("Creating UNIX-like filesystem...", 18, 9, (COLOR_BLACK << 4) | COLOR_WHITE);
    
    draw_progress(20, 11, 40, install_progress, COLOR_GRAY, COLOR_GREEN);
    
    char percent_str[8];
    int_to_str(install_progress, percent_str);
    kprint_at(percent_str, 62, 11, (COLOR_BLACK << 4) | COLOR_WHITE);
    kprint_at("%", 66, 11, (COLOR_BLACK << 4) | COLOR_WHITE);
}

static char inst_wait_key(void) {
    while(1) {
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc < 0x80) {
                if(sc == 0x1C) return '\n';
                if(sc == 0x01) return 27;
                if(sc == 0x48) return 0xE0;
                if(sc == 0x50) return 0xE1;
                if(sc == 0x11) return 'w';
                if(sc == 0x1F) return 's';
                if(sc == 0x39) return ' ';
            }
        }
    }
}


static void inst_draw_menu(void) {
    clear_screen_bg(COLOR_GRAY);
    
    draw_window(15, 5, 50, 16, COLOR_BLUE);
    kprint_at("WNKA OS Installer", 28, 7, (COLOR_BLUE << 4) | COLOR_WHITE);
    kprint_at("Install WNKA OS with UNIX structure", 18, 9, (COLOR_BLACK << 4) | COLOR_WHITE);
    kprint_at("and WnkC scripting support", 22, 10, (COLOR_BLACK << 4) | COLOR_GREEN);
    
    if(selected == 0) {
        draw_frame(20, 12, 40, 2, COLOR_BLUE, COLOR_WHITE);
        kprint_at("> Install", 35, 13, (COLOR_BLUE << 4) | COLOR_WHITE);
    } else {
        draw_frame(20, 12, 40, 2, COLOR_GRAY, COLOR_WHITE);
        kprint_at("  Install", 35, 13, (COLOR_GRAY << 4) | COLOR_BLACK);
    }
    
    if(selected == 1) {
        draw_frame(20, 15, 40, 2, COLOR_BLUE, COLOR_WHITE);
        kprint_at("> Low Format + Install", 28, 16, (COLOR_BLUE << 4) | COLOR_YELLOW);
    } else {
        draw_frame(20, 15, 40, 2, COLOR_GRAY, COLOR_WHITE);
        kprint_at("  Low Format + Install", 28, 16, (COLOR_GRAY << 4) | COLOR_BLACK);
    }
    
    if(selected == 2) {
        draw_frame(20, 18, 40, 2, COLOR_BLUE, COLOR_WHITE);
        kprint_at("> Cancel", 35, 19, (COLOR_BLUE << 4) | COLOR_WHITE);
    } else {
        draw_frame(20, 18, 40, 2, COLOR_GRAY, COLOR_WHITE);
        kprint_at("  Cancel", 35, 19, (COLOR_GRAY << 4) | COLOR_BLACK);
    }
    
    kprint_at("Use W/S or Arrows, ENTER to select", 18, 22, (COLOR_BLACK << 4) | COLOR_CYAN);
}

static void stage1_install(void) {
    selected = 0;
    
    while(1) {
        inst_draw_menu();
        move_cursor(79, 24);
        
        char key = inst_wait_key();
        
        if((key == 0xE0 || key == ' ') && selected > 0) {
            selected--;
        }
        else if((key == 0xE1) && selected < 2) {
            selected++;
        }
        else if(key == ' ' && selected == 0) {
            selected = 2;
        }
        else if(key == ' ' && selected == 2) {
            selected = 0;
        }
        else if(key == '\n') {
            if(selected == 0) {
                install_progress = 0;
                inst_show_progress();
                inst_delay(200);
                
                stage3_install();
                
                inst_draw_complete();
                move_cursor(79, 24);
                inst_wait_key();
                outb(0x64, 0xFE);
            } 
            else if(selected == 1) {
                kprint_color("\nLOW LEVEL FORMAT will erase ALL data!\n", TXT_RED);
                kprint("Enter size in GB (1-12): ");
                
                int gb = 0;
                int got = 0;
                while(!got) {
                    if(inb(0x64) & 1) {
                        uint8_t sc = inb(0x60);
                        if(sc >= 0x02 && sc <= 0x0B) {
                            int digit = sc - 0x02;
                            if(digit == 10) digit = 0;
                            gb = gb * 10 + digit;
                            char s[2] = {'0' + digit, 0};
                            kprint(s);
                        }
                        else if(sc == 0x0E) {
                            gb = 0;
                            kprint("\b \b");
                        }
                        else if(sc == 0x1C) {
                            got = 1;
                        }
                    }
                }
                
                if(gb < 1) gb = 1;
                if(gb > 12) gb = 12;
                
                kprint("\n");
                lowformat_disk(gb);
                write_install_config(0, "installation_stage_1_completed = true\n");
                kprint_color("\n[INSTALL] Stage 1 complete. Rebooting for stage 2...\n", TXT_GREEN);
                inst_delay(2000);
                outb(0x64, 0xFE);
            }
            else {
                return;
            }
        }
        else if(key == 27) {
            return;
        }
    }
}

void wnk_install(void) {
    int stage = check_install_stage();
    
    if(stage == 1) {
        stage1_install();
    } else if(stage == 2) {
        stage2_input();
        kprint_color("[INSTALL] Forcing stage 3...\n", TXT_YELLOW);
        outb(0x64, 0xFE);
    } else if(stage == 3) {
        stage3_install();
        outb(0x64, 0xFE);
    }
}