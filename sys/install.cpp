#include "video.h"
#include "graph.h"
#include "ata.h"
#include "kernel_stubs.h"
#include "floppy.h"
#include "fdc.h"
#include "vga.h"
#include "cdrom.h"
#include "string_utils.h"
#include "wnkfs.h"
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
static int use_wnkfs = 0;
static uint16_t current_dir_sector = 100;
static int wnkfs_super_floppy = 0;
static char wnkfs_volume_name[32] = "WNKA";
extern void update_time_display(void);
extern int seconds;
extern int clock_h, clock_m, clock_s;
extern int day, month, year;

static int my_atoi(const char* s) {
    int res = 0;
    while(*s >= '0' && *s <= '9') {
        res = res * 10 + (*s - '0');
        s++;
    }
    return res;
}
static void strcpy(char* d, const char* s) { 
    while(*s){
        *d=*s;d++;s++;
    } *d=0; 
}

static void str_cpy(char* d, const char* s) { while(*s){*d=*s;d++;s++;} *d=0; }
static int str_len(const char* s) { int l=0; while(*s++)l++; return l; }
static int str_cmp(const char* a, const char* b) { while(*a&&*b&&*a==*b){a++;b++;} return *a-*b; }
static void sprintf(char* buf, const char* fmt, ...) {
    char* b = buf; const char* f = fmt;
    __builtin_va_list args; __builtin_va_start(args, fmt);
    while(*f){ 
        if(*f=='%'){ 
            f++; 
            if(*f=='s'){ 
                const char* s=__builtin_va_arg(args,const char*); 
                while(*s)*b++=*s++; 
                f++; 
            } else if(*f=='d'){ 
                int n=__builtin_va_arg(args,int); 
                if(n==0)*b++='0'; 
                else{ 
                    char t[16]; int i=0; int num=n<0?-n:n;
                    while(num>0){t[i++]='0'+(num%10);num/=10;}
                    if(n<0)*b++='-';
                    while(i>0)*b++=t[--i]; 
                } 
                f++; 
            } 
        } else *b++=*f++; 
    }
    *b=0; __builtin_va_end(args);
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
            }
            else if(*f == 'd') {
                int n = va_arg(args, int);
                if(n == 0) *b++ = '0';
                else {
                    char temp[16];
                    int i = 0;
                    while(n > 0) {
                        temp[i++] = '0' + (n % 10);
                        n /= 10;
                    }
                    while(i--) *b++ = temp[i];
                }
                f++;
            }
            else if(*f == '%') {
                *b++ = '%';
                f++;
            }
        }
        else if(*f == '\\') {
            f++;
            if(*f == 'n') { *b++ = '\n'; f++; }
            else if(*f == 'r') { *b++ = '\r'; f++; }
            else if(*f == 't') { *b++ = '\t'; f++; }
            else { *b++ = '\\'; *b++ = *f++; }
        }
        else { *b++ = *f++; }
    }
    *b = '\0';
    va_end(args);
}

static void my_strcat(char* dest, const char* src) {
    while(*dest) dest++;
    while(*src) *dest++ = *src++;
    *dest = '\0';
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
            kprint("It will take from 15 minutes to 1 hour.\n");
            if(use_wnkfs) {
                kprint("Filesystem: WnkFS");
                if(wnkfs_super_floppy) kprint(" (SuperFloppy mode)");
                kprint("\n");
            }
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
    
    if(use_wnkfs) {
        kprint("\nCreating WnkFS filesystem...\n");
        if(wnkfs_super_floppy) {
            kprint("[WnkFS] SuperFloppy mode: no partition table\n");
        }
        wnkfs_format();
        kprint("[WnkFS] Filesystem created!\n");
    }
    
    uint32_t total_time = seconds - start_time;
    kprint("\nFormat complete! Time: ");
    kprint_int(total_time);
    kprint(" seconds\n");
    
    char config_str[256];
    if(use_wnkfs) {
        my_sprintf(config_str, "filesystem=wnkfs\nwnkfs_super_floppy=%d\ninstallation_stage_1_completed=true\n", wnkfs_super_floppy);
    }
    else {
        my_strcpy(config_str, "installation_stage_1_completed=true\n");
    }
    write_install_config(0, config_str);
    
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


static char scancode_to_ascii(uint8_t sc) {
    if(sc >= 0x02 && sc <= 0x0B) return "1234567890"[sc - 0x02];
    if(sc >= 0x10 && sc <= 0x19) return "qwertyuiop"[sc - 0x10];
    if(sc >= 0x1E && sc <= 0x26) return "asdfghjkl"[sc - 0x1E];
    if(sc >= 0x2C && sc <= 0x32) return "zxcvbnm"[sc - 0x2C];
    if(sc == 0x39) return ' ';
    if(sc == 0x0C) return '-';
    if(sc == 0x0D) return '=';
    if(sc == 0x34) return '.';
    if(sc == 0x27) return ';';
    if(sc == 0x33) return ',';
    if(sc == 0x35) return '/';
    if(sc == 0x1A) return '[';
    if(sc == 0x1B) return ']';
    if(sc == 0x2B) return '\\';
    if(sc == 0x28) return '\'';
    return 0;
}


static void sound_click(void) {
    outb(0x61, inb(0x61) | 3);
    for(volatile int i = 0; i < 10000; i++);
    outb(0x61, inb(0x61) & 0xFC);
}

static int check_install_stage(void) {
    char buffer[256];
    read_install_config(0, buffer, 256);
    
    if(my_strstr(buffer, "filesystem=wnkfs") != NULL) {
        use_wnkfs = 1;
    }
    if(my_strstr(buffer, "wnkfs_super_floppy=1") != NULL) {
        wnkfs_super_floppy = 1;
    }
    
    if(my_strstr(buffer, "installation_stage_1_completed=true") != NULL) {
        read_install_config(1, buffer, 256);
        if(my_strstr(buffer, "stage2_completed=true") != NULL) {
            return 3;
        }
        return 2;
    }
    return 1;
}

static void stage2_input(void) {
    clear_screen_bg(COLOR_GRAY);
    int win_x = 15;
    int win_y = 3;
    int win_w = 50;
    int win_h = 22;
    int label_x = win_x + 4;
    int value_x = win_x + 20;
    
    detect_system_info();
    
    char stage1_config[256];
    read_install_config(0, stage1_config, 256);
    if(my_strstr(stage1_config, "filesystem=wnkfs") != NULL) {
        use_wnkfs = 1;
    }
    else {
        use_wnkfs = 0;
    }
    
    if(my_strstr(stage1_config, "wnkfs_super_floppy=1") != NULL) {
        wnkfs_super_floppy = 1;
    }
    
    char hostname[32] = {0};
    char computer_name[32] = {0};
    int user_autologin = 0;
    int user_desktop_effect = 1;
    int user_wnkui_autostart = 1;
    int cpu_selected = 0;
    int current_field = 0;
    int running = 1;
    int redraw = 1;
    
    while(running) {
        if(redraw) {
            clear_screen_bg(COLOR_GRAY);
            draw_shadow_window(win_x, win_y, win_w, win_h, COLOR_BLUE, TXT_WHITE, "WNKA OS SETUP - STAGE 2");
            draw_dframe(win_x + 2, win_y + 2, win_w - 4, 3, COLOR_BLUE, TXT_YELLOW);
            kprint_at("System Configuration Wizard", win_x + (win_w - 24)/2, win_y + 3, (COLOR_BLUE << 4) | TXT_YELLOW);
            
            int y = win_y + 6;
            
            uint8_t c0 = (current_field == 0) ? TXT_GREEN : TXT_WHITE;
            uint8_t c1 = (current_field == 1) ? TXT_GREEN : TXT_WHITE;
            uint8_t c2 = (current_field == 2) ? TXT_GREEN : TXT_WHITE;
            uint8_t c3 = (current_field == 3) ? TXT_GREEN : TXT_WHITE;
            uint8_t c4 = (current_field == 4) ? TXT_GREEN : TXT_WHITE;
            uint8_t c5 = (current_field == 5) ? TXT_GREEN : TXT_WHITE;
            uint8_t c6 = (current_field == 6) ? TXT_GREEN : TXT_WHITE;
            uint8_t c7 = (current_field == 7) ? TXT_GREEN : TXT_WHITE;
            
            kprint_at("Username:", label_x, y, (COLOR_BLACK << 4) | c0);
            kprint_at(">", value_x - 2, y, (COLOR_BLACK << 4) | TXT_GREEN);
            kprint_at(username, value_x, y, (COLOR_BLACK << 4) | TXT_WHITE);
            for(int i = my_strlen(username); i < 16; i++) kprint_at(" ", value_x + i, y, (COLOR_BLACK << 4) | TXT_BLACK);
            
            kprint_at("Password:", label_x, y + 1, (COLOR_BLACK << 4) | c1);
            kprint_at(">", value_x - 2, y + 1, (COLOR_BLACK << 4) | TXT_GREEN);
            for(int i = 0; i < my_strlen(user_password); i++) {
                kprint_at("*", value_x + i, y + 1, (COLOR_BLACK << 4) | TXT_GREEN);
            }
            for(int i = my_strlen(user_password); i < 16; i++) kprint_at(" ", value_x + i, y + 1, (COLOR_BLACK << 4) | TXT_BLACK);
            
            kprint_at("Hostname:", label_x, y + 2, (COLOR_BLACK << 4) | c2);
            kprint_at(">", value_x - 2, y + 2, (COLOR_BLACK << 4) | TXT_GREEN);
            kprint_at(hostname, value_x, y + 2, (COLOR_BLACK << 4) | TXT_WHITE);
            for(int i = my_strlen(hostname); i < 16; i++) kprint_at(" ", value_x + i, y + 2, (COLOR_BLACK << 4) | TXT_BLACK);
            
            kprint_at("Computer:", label_x, y + 3, (COLOR_BLACK << 4) | c3);
            kprint_at(">", value_x - 2, y + 3, (COLOR_BLACK << 4) | TXT_GREEN);
            kprint_at(computer_name, value_x, y + 3, (COLOR_BLACK << 4) | TXT_WHITE);
            for(int i = my_strlen(computer_name); i < 16; i++) kprint_at(" ", value_x + i, y + 3, (COLOR_BLACK << 4) | TXT_BLACK);
            
            kprint_at("CPU Type:", label_x, y + 4, (COLOR_BLACK << 4) | c4);
            kprint_at("[", value_x - 1, y + 4, (COLOR_BLACK << 4) | TXT_CYAN);
            kprint_at(cpu_list[cpu_selected], value_x, y + 4, (COLOR_BLACK << 4) | TXT_YELLOW);
            kprint_at("]", value_x + my_strlen(cpu_list[cpu_selected]), y + 4, (COLOR_BLACK << 4) | TXT_CYAN);
            
            kprint_at("Auto-login:", label_x, y + 5, (COLOR_BLACK << 4) | c5);
            kprint_at(user_autologin ? "[X]" : "[ ]", value_x, y + 5, (COLOR_BLACK << 4) | (user_autologin ? TXT_GREEN : TXT_WHITE));
            
            kprint_at("Desktop Effects:", label_x, y + 6, (COLOR_BLACK << 4) | c6);
            kprint_at(user_desktop_effect ? "[X]" : "[ ]", value_x, y + 6, (COLOR_BLACK << 4) | (user_desktop_effect ? TXT_GREEN : TXT_WHITE));
            
            kprint_at("WnkUI Autostart:", label_x, y + 7, (COLOR_BLACK << 4) | c7);
            kprint_at(user_wnkui_autostart ? "[X]" : "[ ]", value_x, y + 7, (COLOR_BLACK << 4) | (user_wnkui_autostart ? TXT_GREEN : TXT_WHITE));
            
            draw_hline(win_x + 1, win_y + win_h - 4, win_w - 2, COLOR_BLUE, TXT_WHITE, S_HLINE);
            kprint_at("UP/DOWN: Move   ENTER: Edit   SPACE: Toggle", win_x + 5, win_y + win_h - 3, (COLOR_BLUE << 4) | TXT_YELLOW);
            kprint_at("F1: CPU Menu   ESC: Save & Continue", win_x + 5, win_y + win_h - 2, (COLOR_BLUE << 4) | TXT_CYAN);
            
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
                            move_cursor(value_x + cursor, win_y + 6);
                            
                            if(inb(0x64) & 1) {
                                uint8_t k = inb(0x60);
                                if(k == 0x1C) {
                                    editing = 0;
                                }
                                else if(k == 0x01) {
                                    editing = 0;
                                }
                                else if(k == 0x0E && cursor > 0) {
                                    cursor--;
                                    for(int i = cursor; i < pos; i++) {
                                        username[i] = username[i+1];
                                    }
                                    pos--;
                                    username[pos] = '\0';
                                }
                                else if(cursor < 31) {
                                    char ch = scancode_to_ascii(k);
                                    if(ch) {
                                        for(int i = pos; i > cursor; i--) {
                                            username[i] = username[i-1];
                                        }
                                        username[cursor++] = ch;
                                        pos++;
                                    }
                                }
                                username[pos] = '\0';
                                
                                kprint_at("                ", value_x, win_y + 6, (COLOR_BLACK << 4) | TXT_BLACK);
                                kprint_at(username, value_x, win_y + 6, (COLOR_BLACK << 4) | TXT_WHITE);
                            }
                        }
                        redraw = 1;
                    }
                    else if(current_field == 1) {
                        int pos = my_strlen(user_password);
                        int cursor = pos;
                        int editing = 1;
                        while(editing) {
                            kprint_at("_", value_x + cursor, win_y + 7, (COLOR_BLACK << 4) | TXT_RED);
                            move_cursor(value_x + cursor, win_y + 7);
                            
                            if(inb(0x64) & 1) {
                                uint8_t k = inb(0x60);
                                if(k == 0x1C) { editing = 0; }
                                else if(k == 0x01) { editing = 0; }
                                else if(k == 0x0E && cursor > 0) {
                                    cursor--;
                                    for(int i = cursor; i < pos; i++) {
                                        user_password[i] = user_password[i+1];
                                    }
                                    pos--;
                                    user_password[pos] = '\0';
                                }
                                else if(cursor < 63) {
                                    char ch = scancode_to_ascii(k);
                                    if(ch) {
                                        user_password[cursor++] = ch;
                                        pos++;
                                    }
                                }
                                user_password[pos] = '\0';
                                
                                for(int i = 0; i < 16; i++) kprint_at(" ", value_x + i, win_y + 7, (COLOR_BLACK << 4) | TXT_BLACK);
                                for(int i = 0; i < pos; i++) kprint_at("*", value_x + i, win_y + 7, (COLOR_BLACK << 4) | TXT_GREEN);
                            }
                        }
                        redraw = 1;
                    }
                    else if(current_field == 2) {
                        int pos = my_strlen(hostname);
                        int cursor = pos;
                        int editing = 1;
                        while(editing) {
                            kprint_at("_", value_x + cursor, win_y + 8, (COLOR_BLACK << 4) | TXT_RED);
                            move_cursor(value_x + cursor, win_y + 8);
                            
                            if(inb(0x64) & 1) {
                                uint8_t k = inb(0x60);
                                if(k == 0x1C) { editing = 0; }
                                else if(k == 0x01) { editing = 0; }
                                else if(k == 0x0E && cursor > 0) {
                                    cursor--;
                                    for(int i = cursor; i < pos; i++) hostname[i] = hostname[i+1];
                                    pos--;
                                    hostname[pos] = '\0';
                                }
                                else if(cursor < 31) {
                                    char ch = scancode_to_ascii(k);
                                    if(ch) {
                                        for(int i = pos; i > cursor; i--) hostname[i] = hostname[i-1];
                                        hostname[cursor++] = ch;
                                        pos++;
                                    }
                                }
                                hostname[pos] = '\0';
                                
                                kprint_at("                ", value_x, win_y + 8, (COLOR_BLACK << 4) | TXT_BLACK);
                                kprint_at(hostname, value_x, win_y + 8, (COLOR_BLACK << 4) | TXT_WHITE);
                            }
                        }
                        redraw = 1;
                    }
                    else if(current_field == 3) {
                        int pos = my_strlen(computer_name);
                        int cursor = pos;
                        int editing = 1;
                        while(editing) {
                            kprint_at("_", value_x + cursor, win_y + 9, (COLOR_BLACK << 4) | TXT_RED);
                            move_cursor(value_x + cursor, win_y + 9);
                            
                            if(inb(0x64) & 1) {
                                uint8_t k = inb(0x60);
                                if(k == 0x1C) { editing = 0; }
                                else if(k == 0x01) { editing = 0; }
                                else if(k == 0x0E && cursor > 0) {
                                    cursor--;
                                    for(int i = cursor; i < pos; i++) computer_name[i] = computer_name[i+1];
                                    pos--;
                                    computer_name[pos] = '\0';
                                }
                                else if(cursor < 31) {
                                    char ch = scancode_to_ascii(k);
                                    if(ch) {
                                        computer_name[cursor++] = ch;
                                        pos++;
                                    }
                                }
                                computer_name[pos] = '\0';
                                
                                kprint_at("                ", value_x, win_y + 9, (COLOR_BLACK << 4) | TXT_BLACK);
                                kprint_at(computer_name, value_x, win_y + 9, (COLOR_BLACK << 4) | TXT_WHITE);
                            }
                        }
                        redraw = 1;
                    }
                    else if(current_field == 4) {
                        int cpu_scroll = 0;
                        int cpu_selected_temp = cpu_selected;
                        int cpu_running = 1;
                        int cpu_redraw = 1;
                        
                        while(cpu_running) {
                            if(cpu_redraw) {
                                int menu_x = win_x + 10;
                                int menu_y = win_y + 4;
                                int menu_w = 30;
                                int menu_h = 12;
                                draw_shadow_window(menu_x, menu_y, menu_w, menu_h, COLOR_GRAY, TXT_WHITE, "Select CPU");
                                
                                for(int i = 0; i < 10 && cpu_scroll + i < cpu_count; i++) {
                                    int idx = cpu_scroll + i;
                                    uint8_t color = (idx == cpu_selected_temp) ? TXT_GREEN : TXT_WHITE;
                                    kprint_at(cpu_list[idx], menu_x + 2, menu_y + 2 + i, (COLOR_GRAY << 4) | color);
                                }
                                
                                if(cpu_scroll > 0) {
                                    kprint_at("^", menu_x + menu_w - 3, menu_y + 2, (COLOR_GRAY << 4) | TXT_CYAN);
                                }
                                if(cpu_scroll + 10 < cpu_count) {
                                    kprint_at("v", menu_x + menu_w - 3, menu_y + menu_h - 3, (COLOR_GRAY << 4) | TXT_CYAN);
                                }
                                
                                kprint_at("UP/DOWN: Move  ENTER: Select  ESC: Cancel", menu_x + 2, menu_y + menu_h - 2, (COLOR_GRAY << 4) | TXT_YELLOW);
                                cpu_redraw = 0;
                            }
                            
                            move_cursor(79, 24);
                            
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
        "autologin=%d\n"
        "desktop_effects=%d\n"
        "wnkui_autostart=%d\n"
        "cpu_type=%d\n"
        "cpu_mhz=%d\n"
        "detected_ram=%d\n"
        "filesystem=%s\n"
        "stage2_completed=true\n",
        username, user_password, hostname, computer_name,
        user_autologin, user_desktop_effect, user_wnkui_autostart,
        cpu_selected, cpu_mhz_detected, ram_mb_detected,
        use_wnkfs ? "wnkfs" : "wnkafs");
    write_install_config(1, config);
    
    if(use_wnkfs) {
        write_install_config(0, "filesystem=wnkfs\ninstallation_stage_1_completed=true\n");
    }
    else {
        write_install_config(0, "installation_stage_1_completed=true\n");
    }
    
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
    settings_buf[8] = cpu_selected;
    settings_buf[9] = cpu_mhz_detected;
    settings_buf[10] = ram_mb_detected;
    settings_buf[11] = user_wnkui_autostart;
    settings_buf[12] = use_wnkfs;
    settings_buf[13] = wnkfs_super_floppy;
    write_sector(106, settings_buf);
    
    uint16_t comp_buf[256] = {0};
    for(int i = 0; computer_name[i]; i++) {
        if(i % 2 == 0) comp_buf[i/2] = computer_name[i];
        else comp_buf[i/2] |= (computer_name[i] << 8);
    }
    write_sector(109, comp_buf);
    
    clear_screen_bg(COLOR_GRAY);
    draw_shadow_window(12, 6, 56, 12, COLOR_BLUE, TXT_WHITE, "CONFIGURATION SAVED");
    draw_dframe(14, 8, 52, 8, COLOR_BLACK, TXT_GREEN);
    kprint_at("All settings have been saved!", 18, 10, (COLOR_BLACK << 4) | TXT_GREEN);
    kprint_at("Filesystem: ", 20, 12, (COLOR_BLACK << 4) | TXT_CYAN);
    kprint_at(use_wnkfs ? "WnkFS" : "Standard (WnkaFS)", 33, 12, (COLOR_BLACK << 4) | (use_wnkfs ? TXT_GREEN : TXT_YELLOW));
    kprint_at("Press any key to continue...", 22, 15, (COLOR_BLACK << 4) | TXT_WHITE);
    while(!(inb(0x64) & 1));
    while(inb(0x64) & 1) inb(0x60);
}

typedef struct {
    const char* name;
    int enabled;
    const char* desc;
    int size;
    int category;
} wnc_script_t;

static void save_selected_components(void);
static void load_selected_components(void);
static void show_script_preview(int idx);
static void search_scripts(void);
static void select_scripts_in_category(int category);
static void show_script_statistics(void);
static void select_wnc_scripts(void);
static const char* get_script_content(const char* name);

static const char* category_names[] = {
    "System Tools",
    "File Utilities", 
    "Math & Calc",
    "Converters",
    "Generators",
    "Text Tools",
    "Games",
    "Visual Effects",
    "Fun & Entertainment"
};

static wnc_script_t wnc_scripts[] = {
    {"welcome.wnc", 1, "Welcome screen with full guide", 3200, 0},
    {"sysinfo.wnc", 1, "Complete system information", 2800, 0},
    {"fetch.wnc", 1, "Hardware info display", 1800, 0},
    {"uptime.wnc", 0, "System uptime counter", 1200, 0},
    {"diskfree.wnc", 1, "Free disk space checker", 1500, 0},
    {"taskmgr.wnc", 0, "Simple task manager", 3200, 0},
    {"memtest.wnc", 0, "Memory usage monitor", 2400, 0},
    {"cpu_load.wnc", 0, "CPU load monitor", 2000, 0},
    {"battery.wnc", 0, "Battery status check", 1600, 0},
    {"temperature.wnc", 0, "System temperature", 1800, 0},
    {"filefind.wnc", 0, "Advanced file search", 2800, 1},
    {"filerename.wnc", 0, "Batch file renamer", 3200, 1},
    {"filecompare.wnc", 0, "Compare two files", 2600, 1},
    {"fileencrypt.wnc", 0, "Simple file encryption", 3400, 1},
    {"filedecrypt.wnc", 0, "Simple file decryption", 3400, 1},
    {"filecompress.wnc", 0, "Basic file compression", 3800, 1},
    {"filedecompress.wnc", 0, "Basic file decompression", 3800, 1},
    {"filesplit.wnc", 0, "Split large files", 2800, 1},
    {"filemerge.wnc", 0, "Merge split files", 2600, 1},
    {"fileinfo.wnc", 0, "Detailed file information", 2200, 1},
    {"calc.wnc", 1, "Basic calculator", 2000, 2},
    {"calc_sci.wnc", 0, "Scientific calculator", 4200, 2},
    {"calc_hex.wnc", 0, "Hex/binary calculator", 3400, 2},
    {"prime_finder.wnc", 0, "Find prime numbers", 2800, 2},
    {"fibonacci.wnc", 0, "Fibonacci generator", 2400, 2},
    {"factorial.wnc", 0, "Factorial calculator", 1800, 2},
    {"gcd_lcm.wnc", 0, "GCD and LCM finder", 2200, 2},
    {"quadratic.wnc", 0, "Quadratic solver", 2600, 2},
    {"pythagorean.wnc", 0, "Pythagorean calculator", 2000, 2},
    {"statistics.wnc", 0, "Mean/median/mode calculator", 3200, 2},
    {"temp_conv.wnc", 0, "Temperature converter", 1800, 3},
    {"length_conv.wnc", 0, "Length unit converter", 2200, 3},
    {"weight_conv.wnc", 0, "Weight unit converter", 2200, 3},
    {"speed_conv.wnc", 0, "Speed unit converter", 2000, 3},
    {"area_conv.wnc", 0, "Area unit converter", 2200, 3},
    {"volume_conv.wnc", 0, "Volume unit converter", 2200, 3},
    {"time_conv.wnc", 0, "Time unit converter", 2400, 3},
    {"data_conv.wnc", 0, "Data size converter", 2000, 3},
    {"angle_conv.wnc", 0, "Angle converter", 1800, 3},
    {"currency_conv.wnc", 0, "Currency converter", 2800, 3},
    {"password_gen.wnc", 0, "Strong password generator", 2600, 4},
    {"username_gen.wnc", 0, "Random username generator", 2200, 4},
    {"email_gen.wnc", 0, "Random email generator", 2000, 4},
    {"phone_gen.wnc", 0, "Phone number generator", 2000, 4},
    {"address_gen.wnc", 0, "Address generator", 2400, 4},
    {"uuid_gen.wnc", 0, "UUID generator", 1800, 4},
    {"color_gen.wnc", 0, "Color palette generator", 2200, 4},
    {"date_gen.wnc", 0, "Random date generator", 2000, 4},
    {"lorem_ipsum.wnc", 0, "Lorem ipsum generator", 2400, 4},
    {"qr_code.wnc", 0, "Simple QR code generator", 3600, 4},
    {"wordcount.wnc", 0, "Word counter", 1800, 5},
    {"chardetect.wnc", 0, "Character frequency", 2200, 5},
    {"textreverse.wnc", 0, "Reverse text", 1600, 5},
    {"textupper.wnc", 0, "Convert to uppercase", 1400, 5},
    {"textlower.wnc", 0, "Convert to lowercase", 1400, 5},
    {"textcapitalize.wnc", 0, "Capitalize text", 1800, 5},
    {"texttrim.wnc", 0, "Trim whitespace", 1600, 5},
    {"textreplace.wnc", 0, "Find and replace", 2400, 5},
    {"textsort.wnc", 0, "Sort lines", 2200, 5},
    {"textunique.wnc", 0, "Remove duplicate lines", 2000, 5},
    {"guess_num.wnc", 0, "Guess the number game", 2400, 6},
    {"rps_game.wnc", 0, "Rock paper scissors", 2200, 6},
    {"dice_roller.wnc", 0, "Dice rolling game", 2000, 6},
    {"coin_flip.wnc", 0, "Coin flip game", 1400, 6},
    {"hangman.wnc", 0, "Hangman word game", 4200, 6},
    {"tictactoe.wnc", 0, "Tic-tac-toe", 3800, 6},
    {"blackjack.wnc", 0, "Blackjack card game", 5200, 6},
    {"slot_machine.wnc", 0, "Slot machine", 2800, 6},
    {"roulette.wnc", 0, "Roulette game", 3400, 6},
    {"word_scramble.wnc", 0, "Word scramble game", 3200, 6},
    {"math_race.wnc", 0, "Math racing game", 3600, 6},
    {"typing_game.wnc", 0, "Typing speed game", 3800, 6},
    {"memory_game.wnc", 0, "Memory matching game", 4400, 6},
    {"riddle_game.wnc", 0, "Riddle quiz game", 4800, 6},
    {"trivia_game.wnc", 0, "General trivia quiz", 5200, 6},
    {"matrix_rain.wnc", 0, "Matrix rain effect", 2400, 7},
    {"fire_effect.wnc", 0, "Fire simulation", 2800, 7},
    {"starfield.wnc", 0, "Starfield animation", 2600, 7},
    {"plasma_effect.wnc", 0, "Plasma effect", 3000, 7},
    {"wave_effect.wnc", 0, "Wave animation", 2400, 7},
    {"tunnel_3d.wnc", 0, "3D tunnel effect", 3200, 7},
    {"snowfall.wnc", 0, "Snowfall animation", 2600, 7},
    {"fireworks.wnc", 0, "Fireworks display", 3600, 7},
    {"particles.wnc", 0, "Particle system", 3400, 7},
    {"kaleidoscope.wnc", 0, "Kaleidoscope pattern", 3000, 7},
    {"fortune.wnc", 0, "Fortune cookie messages", 4200, 8},
    {"cowsay.wnc", 0, "Animated cow says", 2800, 8},
    {"magic8ball.wnc", 0, "Magic 8-ball answers", 2000, 8},
    {"joke_teller.wnc", 0, "Random joke teller", 3200, 8},
    {"compliment.wnc", 0, "Random compliments", 2600, 8},
    {"insult_gen.wnc", 0, "Shakespeare insults", 2400, 8},
    {"affirmation.wnc", 0, "Daily affirmations", 2800, 8},
    {"haiku_gen.wnc", 0, "Haiku poem generator", 3000, 8},
    {"madlibs.wnc", 0, "Mad Libs story game", 3400, 8},
    {"zodiac_sign.wnc", 0, "Zodiac sign finder", 2200, 8},
    {"backup_sys.wnc", 0, "System backup tool", 4500, 0},
    {"restore_sys.wnc", 0, "System restore tool", 4800, 0},
    {"log_cleaner.wnc", 0, "Log file cleaner", 2200, 0},
    {"disk_analyzer.wnc", 0, "Disk usage analyzer", 3200, 0},
    {"process_killer.wnc", 0, "Process killer", 2000, 0},
    {"service_mgr.wnc", 0, "Service manager", 3800, 0},
    {"startup_mgr.wnc", 0, "Startup manager", 3000, 0},
    {"network_info.wnc", 0, "Network info", 2600, 0},
    {"ping_test.wnc", 0, "Ping test utility", 2400, 0},
    {"trace_route.wnc", 0, "Trace route tool", 3400, 0},
};

static int script_count = sizeof(wnc_scripts) / sizeof(wnc_script_t);

static const char* get_script_content(const char* name) {
    
    
    if(my_strcmp(name, "welcome.wnc") == 0) {
        return "print \"========================================\"\n"
               "print \"     WELCOME TO WNKA OS X32\"\n"
               "print \"========================================\"\n"
               "print \"\"\n"
               "print \"[System Information]\"\n"
               "run fetch\n"
               "print \"\"\n"
               "print \"[Quick Start]\"\n"
               "print \"  help     - show all commands\"\n"
               "print \"  ls       - list files\"\n"
               "print \"  calc     - calculator\"\n"
               "print \"  ui       - start desktop\"\n"
               "print \"\"\n"
               "print \"Type 'help' for complete command list\"\n";
    }
    
    if(my_strcmp(name, "sysinfo.wnc") == 0) {
        return "print \"=== SYSTEM INFORMATION ===\"\n"
               "print \"\"\n"
               "run fetch\n"
               "print \"\"\n"
               "print \"[Memory]\"\n"
               "run memtest\n"
               "print \"\"\n"
               "print \"[Disk]\"\n"
               "run diskfree\n"
               "print \"\"\n"
               "print \"[Uptime]\"\n"
               "run uptime\n";
    }
    
    if(my_strcmp(name, "fetch.wnc") == 0) {
        return "print \"OS: WNKA X32\"\n"
               "print \"Kernel: v1.0\"\n"
               "print \"Shell: WnkaShell\"\n"
               "run time\n"
               "print \"\"\n"
               "graph fill 0x01\n"
               "graph text 15 10 0x0F \"WNKA OS\"\n";
    }
    
    if(my_strcmp(name, "uptime.wnc") == 0) {
        return "run time\n"
               "let sec = seconds\n"
               "let hours = sec / 3600\n"
               "let mins = (sec % 3600) / 60\n"
               "let secs = sec % 60\n"
               "print \"Uptime: \" + hours + \"h \" + mins + \"m \" + secs + \"s\"\n";
    }
    
    if(my_strcmp(name, "diskfree.wnc") == 0) {
        return "run df\n";
    }
    
    if(my_strcmp(name, "taskmgr.wnc") == 0) {
        return "run ps\n";
    }
    
    if(my_strcmp(name, "memtest.wnc") == 0) {
        return "print \"Memory Info:\"\n"
               "print \"  Total RAM: \" + total_ram / 1024 + \" KB\"\n"
               "print \"  Used RAM: \" + used_ram / 1024 + \" KB\"\n"
               "let free = (total_ram - used_ram) / 1024\n"
               "print \"  Free RAM: \" + free + \" KB\"\n";
    }
    
    if(my_strcmp(name, "cpu_load.wnc") == 0) {
        return "run cpuinfo\n";
    }
    
    if(my_strcmp(name, "battery.wnc") == 0) {
        return "print \"Battery status not available\"\n"
               "print \"Running on AC power\"\n";
    }
    
    if(my_strcmp(name, "temperature.wnc") == 0) {
        return "print \"Temperature sensors not available\"\n"
               "print \"System seems cool\"\n";
    }
    
    
    if(my_strcmp(name, "filefind.wnc") == 0) {
        return "print \"File Finder\"\n"
               "input \"Enter filename to search: \" pattern\n"
               "print \"Searching for: \" + pattern + \"...\"\n"
               "run find \" + pattern\n";
    }
    
    if(my_strcmp(name, "filerename.wnc") == 0) {
        return "print \"Batch File Renamer\"\n"
               "input \"Enter old name part: \" oldpart\n"
               "input \"Enter new name part: \" newpart\n"
               "print \"Renaming files containing '\" + oldpart + \"' to '\" + newpart + \"'...\"\n"
               "run rename\n";
    }
    
    if(my_strcmp(name, "filecompare.wnc") == 0) {
        return "print \"File Compare\"\n"
               "input \"First file: \" file1\n"
               "input \"Second file: \" file2\n"
               "print \"Comparing \" + file1 + \" and \" + file2 + \"...\"\n"
               "run diff \" + file1 + \" \" + file2\n";
    }
    
    if(my_strcmp(name, "fileencrypt.wnc") == 0) {
        return "print \"File Encryptor\"\n"
               "input \"File to encrypt: \" filename\n"
               "input \"Password: \" pass\n"
               "print \"Encrypting \" + filename + \"...\"\n";
    }
    
    if(my_strcmp(name, "filedecrypt.wnc") == 0) {
        return "print \"File Decryptor\"\n"
               "input \"File to decrypt: \" filename\n"
               "input \"Password: \" pass\n"
               "print \"Decrypting \" + filename + \"...\"\n";
    }
    
    if(my_strcmp(name, "filecompress.wnc") == 0) {
        return "print \"File Compressor\"\n"
               "input \"File to compress: \" filename\n"
               "print \"Compressing \" + filename + \"...\"\n";
    }
    
    if(my_strcmp(name, "filedecompress.wnc") == 0) {
        return "print \"File Decompressor\"\n"
               "input \"File to decompress: \" filename\n"
               "print \"Decompressing \" + filename + \"...\"\n";
    }
    
    if(my_strcmp(name, "filesplit.wnc") == 0) {
        return "print \"File Splitter\"\n"
               "input \"File to split: \" filename\n"
               "input \"Max size per part (KB): \" size\n"
               "print \"Splitting \" + filename + \" into \" + size + \"KB parts...\"\n";
    }
    
    if(my_strcmp(name, "filemerge.wnc") == 0) {
        return "print \"File Merger\"\n"
               "input \"Output file: \" output\n"
               "input \"Number of parts: \" parts\n"
               "print \"Merging \" + parts + \" parts into \" + output + \"...\"\n";
    }
    
    if(my_strcmp(name, "fileinfo.wnc") == 0) {
        return "input \"Filename: \" filename\n"
               "run stat \" + filename + \"\n";
    }
    
    
    if(my_strcmp(name, "calc.wnc") == 0) {
        return "run calc\n";
    }
    
    if(my_strcmp(name, "calc_sci.wnc") == 0) {
        return "print \"Scientific Calculator\"\n"
               "print \"sin(x), cos(x), tan(x), sqrt(x), log(x)\"\n"
               "input \"Expression: \" expr\n"
               "run calc_sci \" + expr\n";
    }
    
    if(my_strcmp(name, "calc_hex.wnc") == 0) {
        return "print \"Hex/Binary Calculator\"\n"
               "input \"Number (decimal): \" num\n"
               "let hex = num\n"
               "print \"Hex: 0x\"\n"
               "print \"Binary: \"\n";
    }
    
    if(my_strcmp(name, "prime_finder.wnc") == 0) {
        return "print \"Prime Number Finder\"\n"
               "input \"Find primes up to: \" limit\n"
               "print \"Primes up to \" + limit + \":\"\n"
               "let n = 2\n"
               "while n < limit\n"
               "  let is_prime = 1\n"
               "  let d = 2\n"
               "  while d * d <= n\n"
               "    if n % d == 0 then is_prime = 0\n"
               "    let d = d + 1\n"
               "  end\n"
               "  if is_prime then print \"  \" + n\n"
               "  let n = n + 1\n"
               "end\n";
    }
    
    if(my_strcmp(name, "fibonacci.wnc") == 0) {
        return "print \"Fibonacci Generator\"\n"
               "input \"How many numbers: \" count\n"
               "let a = 0\n"
               "let b = 1\n"
               "print \"Fibonacci: \"\n"
               "let i = 0\n"
               "while i < count\n"
               "  print \"  \" + a\n"
               "  let c = a + b\n"
               "  let a = b\n"
               "  let b = c\n"
               "  let i = i + 1\n"
               "end\n";
    }
    
    if(my_strcmp(name, "factorial.wnc") == 0) {
        return "print \"Factorial Calculator\"\n"
               "input \"Enter number: \" n\n"
               "let result = 1\n"
               "let i = 1\n"
               "while i <= n\n"
               "  let result = result * i\n"
               "  let i = i + 1\n"
               "end\n"
               "print n + \"! = \" + result\n";
    }
    
    if(my_strcmp(name, "gcd_lcm.wnc") == 0) {
        return "print \"GCD and LCM Calculator\"\n"
               "input \"First number: \" a\n"
               "input \"Second number: \" b\n"
               "let x = a\n"
               "let y = b\n"
               "while y != 0\n"
               "  let t = y\n"
               "  let y = x % y\n"
               "  let x = t\n"
               "end\n"
               "let gcd = x\n"
               "let lcm = (a * b) / gcd\n"
               "print \"GCD: \" + gcd\n"
               "print \"LCM: \" + lcm\n";
    }
    
    if(my_strcmp(name, "quadratic.wnc") == 0) {
        return "print \"Quadratic Equation Solver\"\n"
               "input \"a: \" a\n"
               "input \"b: \" b\n"
               "input \"c: \" c\n"
               "let d = b * b - 4 * a * c\n"
               "if d < 0 then\n"
               "  print \"No real roots\"\n"
               "else\n"
               "  let x1 = (-b + sqrt(d)) / (2 * a)\n"
               "  let x2 = (-b - sqrt(d)) / (2 * a)\n"
               "  print \"x1 = \" + x1\n"
               "  print \"x2 = \" + x2\n"
               "end\n";
    }
    
    if(my_strcmp(name, "pythagorean.wnc") == 0) {
        return "print \"Pythagorean Calculator\"\n"
               "input \"Side a: \" a\n"
               "input \"Side b: \" b\n"
               "let c = sqrt(a * a + b * b)\n"
               "print \"Hypotenuse: \" + c\n";
    }
    
    if(my_strcmp(name, "statistics.wnc") == 0) {
        return "print \"Statistics Calculator\"\n"
               "print \"Enter numbers (0 to finish):\"\n"
               "let sum = 0\n"
               "let count = 0\n"
               "while 1\n"
               "  input \"Number: \" num\n"
               "  if num == 0 then break\n"
               "  let sum = sum + num\n"
               "  let count = count + 1\n"
               "end\n"
               "if count > 0\n"
               "  print \"Sum: \" + sum\n"
               "  print \"Count: \" + count\n"
               "  print \"Average: \" + (sum / count)\n"
               "end\n";
    }
    
    
    if(my_strcmp(name, "temp_conv.wnc") == 0) {
        return "print \"Temperature Converter\"\n"
               "input \"Celsius: \" c\n"
               "let f = (c * 9 / 5) + 32\n"
               "let k = c + 273.15\n"
               "print c + \"°C = \" + f + \"°F\"\n"
               "print c + \"°C = \" + k + \"K\"\n";
    }
    
    if(my_strcmp(name, "length_conv.wnc") == 0) {
        return "print \"Length Converter\"\n"
               "input \"Meters: \" m\n"
               "let cm = m * 100\n"
               "let km = m / 1000\n"
               "let feet = m * 3.28084\n"
               "let inches = m * 39.3701\n"
               "print m + \"m = \" + cm + \"cm\"\n"
               "print m + \"m = \" + km + \"km\"\n"
               "print m + \"m = \" + feet + \"ft\"\n"
               "print m + \"m = \" + inches + \"in\"\n";
    }
    
    if(my_strcmp(name, "weight_conv.wnc") == 0) {
        return "print \"Weight Converter\"\n"
               "input \"Kilograms: \" kg\n"
               "let g = kg * 1000\n"
               "let lb = kg * 2.20462\n"
               "let oz = kg * 35.274\n"
               "print kg + \"kg = \" + g + \"g\"\n"
               "print kg + \"kg = \" + lb + \"lb\"\n"
               "print kg + \"kg = \" + oz + \"oz\"\n";
    }
    
    if(my_strcmp(name, "speed_conv.wnc") == 0) {
        return "print \"Speed Converter\"\n"
               "input \"km/h: \" kmh\n"
               "let ms = kmh / 3.6\n"
               "let mph = kmh / 1.60934\n"
               "let knots = kmh / 1.852\n"
               "print kmh + \"km/h = \" + ms + \"m/s\"\n"
               "print kmh + \"km/h = \" + mph + \"mph\"\n"
               "print kmh + \"km/h = \" + knots + \"knots\"\n";
    }
    
    if(my_strcmp(name, "area_conv.wnc") == 0) {
        return "print \"Area Converter\"\n"
               "input \"Square meters: \" m2\n"
               "let cm2 = m2 * 10000\n"
               "let km2 = m2 / 1000000\n"
               "let acres = m2 / 4046.86\n"
               "print m2 + \"m² = \" + cm2 + \"cm²\"\n"
               "print m2 + \"m² = \" + km2 + \"km²\"\n"
               "print m2 + \"m² = \" + acres + \"acres\"\n";
    }
    
    if(my_strcmp(name, "volume_conv.wnc") == 0) {
        return "print \"Volume Converter\"\n"
               "input \"Liters: \" L\n"
               "let ml = L * 1000\n"
               "let gal = L / 3.78541\n"
               "let quarts = L * 1.05669\n"
               "print L + \"L = \" + ml + \"mL\"\n"
               "print L + \"L = \" + gal + \"gal\"\n"
               "print L + \"L = \" + quarts + \"qt\"\n";
    }
    
    if(my_strcmp(name, "time_conv.wnc") == 0) {
        return "print \"Time Converter\"\n"
               "input \"Seconds: \" s\n"
               "let min = s / 60\n"
               "let hours = s / 3600\n"
               "let days = s / 86400\n"
               "print s + \"s = \" + min + \"min\"\n"
               "print s + \"s = \" + hours + \"h\"\n"
               "print s + \"s = \" + days + \"days\"\n";
    }
    
    if(my_strcmp(name, "data_conv.wnc") == 0) {
        return "print \"Data Size Converter\"\n"
               "input \"KB: \" kb\n"
               "let bytes = kb * 1024\n"
               "let mb = kb / 1024\n"
               "let gb = kb / 1048576\n"
               "print kb + \"KB = \" + bytes + \"B\"\n"
               "print kb + \"KB = \" + mb + \"MB\"\n"
               "print kb + \"KB = \" + gb + \"GB\"\n";
    }
    
    if(my_strcmp(name, "angle_conv.wnc") == 0) {
        return "print \"Angle Converter\"\n"
               "input \"Degrees: \" deg\n"
               "let rad = deg * 3.14159 / 180\n"
               "let grad = deg * 200 / 180\n"
               "print deg + \"° = \" + rad + \"rad\"\n"
               "print deg + \"° = \" + grad + \"grad\"\n";
    }
    
    if(my_strcmp(name, "currency_conv.wnc") == 0) {
        return "print \"Currency Converter (USD to others)\"\n"
               "input \"USD: \" usd\n"
               "let eur = usd * 0.92\n"
               "let gbp = usd * 0.79\n"
               "let rub = usd * 92.5\n"
               "let cny = usd * 7.25\n"
               "let jpy = usd * 150.5\n"
               "print usd + \"USD = \" + eur + \"EUR\"\n"
               "print usd + \"USD = \" + gbp + \"GBP\"\n"
               "print usd + \"USD = \" + rub + \"RUB\"\n"
               "print usd + \"USD = \" + cny + \"CNY\"\n"
               "print usd + \"USD = \" + jpy + \"JPY\"\n";
    }
    
    
    if(my_strcmp(name, "password_gen.wnc") == 0) {
        return "print \"Password Generator\"\n"
               "input \"Length: \" len\n"
               "let chars = \"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%\"\n"
               "let result = \"\"\n"
               "let i = 0\n"
               "while i < len\n"
               "  let r = rand(0, 70)\n"
               "  let result = result + chars[r]\n"
               "  let i = i + 1\n"
               "end\n"
               "print \"Password: \" + result\n";
    }
    
    if(my_strcmp(name, "username_gen.wnc") == 0) {
        return "let prefixes = \"Cool,Smart,Quick,Fast,Big,Small,Red,Blue,Green\"\n"
               "let suffixes = \"User,Pro,Master,Coder,Hacker,Dev,Wizard,King,Lord\"\n"
               "let p = rand(0, 8)\n"
               "let s = rand(0, 8)\n"
               "print \"Username: \" + prefixes[p] + suffixes[s] + rand(100, 999)\n";
    }
    
    if(my_strcmp(name, "email_gen.wnc") == 0) {
        return "let names = \"john,jane,mike,sarah,david,lisa,tom,anna,alex,mary\"\n"
               "let domains = \"gmail.com,yahoo.com,hotmail.com,outlook.com,wnka.com\"\n"
               "let n = rand(0, 9)\n"
               "let d = rand(0, 4)\n"
               "print \"Email: \" + names[n] + rand(1, 999) + \"@\" + domains[d] + \"\\n\"\n";
    }
    
    if(my_strcmp(name, "phone_gen.wnc") == 0) {
        return "let phone = \"+\"\n"
               "let codes = \"1,44,7,49,33,81,86,91,55,61\"\n"
               "let c = rand(0, 9)\n"
               "phone = phone + codes[c] + \"-\"\n"
               "let i = 0\n"
               "while i < 10\n"
               "  phone = phone + rand(0, 9)\n"
               "  let i = i + 1\n"
               "end\n"
               "print \"Phone: \" + phone + \"\\n\"\n";
    }
    
    if(my_strcmp(name, "address_gen.wnc") == 0) {
        return "let streets = \"Main,Broadway,Maple,Oak,Pine,Elm,Washington,Park,Lake,Hill\"\n"
               "let cities = \"New York,Los Angeles,Chicago,Houston,Phoenix,Philadelphia,San Antonio,San Diego,Dallas,Austin\"\n"
               "let s = rand(0, 9)\n"
               "let c = rand(0, 9)\n"
               "let num = rand(1, 9999)\n"
               "print \"Address: \" + num + \" \" + streets[s] + \" St, \" + cities[c] + \", \" + rand(10000, 99999) + \"\\n\"\n";
    }
    
    if(my_strcmp(name, "uuid_gen.wnc") == 0) {
        return "let hex = \"0123456789abcdef\"\n"
               "let uuid = \"\"\n"
               "let i = 0\n"
               "while i < 32\n"
               "  uuid = uuid + hex[rand(0, 15)]\n"
               "  if i == 7 or i == 11 or i == 15 or i == 19 then uuid = uuid + \"-\"\n"
               "  let i = i + 1\n"
               "end\n"
               "print \"UUID: \" + uuid + \"\\n\"\n";
    }
    
    if(my_strcmp(name, "color_gen.wnc") == 0) {
        return "let r = rand(0, 255)\n"
               "let g = rand(0, 255)\n"
               "let b = rand(0, 255)\n"
               "print \"RGB: (\" + r + \",\" + g + \",\" + b + \")\"\n"
               "print \"Hex: #\"\n"
               "graph fill BLACK\n"
               "graph rect 10 10 50 50 r g b\n"
               "print \"   Color preview displayed\"\n";
    }
    
    if(my_strcmp(name, "date_gen.wnc") == 0) {
        return "let year = rand(1950, 2025)\n"
               "let month = rand(1, 12)\n"
               "let day = rand(1, 28)\n"
               "print \"Random date: \" + day + \"/\" + month + \"/\" + year + \"\\n\"\n";
    }
    
    if(my_strcmp(name, "lorem_ipsum.wnc") == 0) {
        return "let words = \"lorem,ipsum,dolor,sit,amet,consectetur,adipiscing,elit,sed,do,eiusmod,tempor,incididunt,ut,labore,et,dolore,magna,aliqua\"\n"
               "input \"Number of words: \" count\n"
               "let i = 0\n"
               "while i < count\n"
               "  let w = rand(0, 19)\n"
               "  print words[w]\n"
               "  if i < count - 1 then print \" \"\n"
               "  let i = i + 1\n"
               "end\n"
               "print \"\\n\"\n";
    }
    
    if(my_strcmp(name, "qr_code.wnc") == 0) {
        return "print \"QR Code Generator\"\n"
               "input \"Text to encode: \" text\n"
               "print \"Generating QR code for: \" + text + \"\\n\"\n"
               "print \"[Simple ASCII QR representation]\"\n"
               "print \"████████████████████\"\n"
               "print \"█░░░░░░░░░░░░░░░░░░█\"\n"
               "print \"█░░\" + text + \"░░█\"\n"
               "print \"█░░░░░░░░░░░░░░░░░░█\"\n"
               "print \"████████████████████\"\n";
    }
    
    
    if(my_strcmp(name, "wordcount.wnc") == 0) {
        return "input \"Text: \" text\n"
               "let words = 1\n"
               "let i = 0\n"
               "while i < len(text)\n"
               "  if text[i] == 32 then words = words + 1\n"
               "  let i = i + 1\n"
               "end\n"
               "print \"Words: \" + words + \"\\n\"\n"
               "print \"Chars: \" + len(text) + \"\\n\"\n";
    }
    
    if(my_strcmp(name, "chardetect.wnc") == 0) {
        return "input \"Text: \" text\n"
               "let counts[256]\n"
               "let i = 0\n"
               "while i < 256\n"
               "  counts[i] = 0\n"
               "  let i = i + 1\n"
               "end\n"
               "let i = 0\n"
               "while i < len(text)\n"
               "  let c = text[i]\n"
               "  counts[c] = counts[c] + 1\n"
               "  let i = i + 1\n"
               "end\n"
               "print \"Character frequencies:\\n\"\n"
               "let i = 0\n"
               "while i < 256\n"
               "  if counts[i] > 0 then\n"
               "    if i >= 32 and i <= 126 then\n"
               "      print \"  '\" + chr(i) + \"': \" + counts[i] + \"\\n\"\n"
               "    end\n"
               "  end\n"
               "  let i = i + 1\n"
               "end\n";
    }
    
    if(my_strcmp(name, "textreverse.wnc") == 0) {
        return "input \"Text: \" text\n"
               "let result = \"\"\n"
               "let i = len(text) - 1\n"
               "while i >= 0\n"
               "  result = result + text[i]\n"
               "  let i = i - 1\n"
               "end\n"
               "print \"Reversed: \" + result + \"\\n\"\n";
    }
    
    if(my_strcmp(name, "textupper.wnc") == 0) {
        return "input \"Text: \" text\n"
               "let result = \"\"\n"
               "let i = 0\n"
               "while i < len(text)\n"
               "  let c = text[i]\n"
               "  if c >= 97 and c <= 122 then\n"
               "    result = result + chr(c - 32)\n"
               "  else\n"
               "    result = result + chr(c)\n"
               "  end\n"
               "  let i = i + 1\n"
               "end\n"
               "print \"Uppercase: \" + result + \"\\n\"\n";
    }
    
    if(my_strcmp(name, "textlower.wnc") == 0) {
        return "input \"Text: \" text\n"
               "let result = \"\"\n"
               "let i = 0\n"
               "while i < len(text)\n"
               "  let c = text[i]\n"
               "  if c >= 65 and c <= 90 then\n"
               "    result = result + chr(c + 32)\n"
               "  else\n"
               "    result = result + chr(c)\n"
               "  end\n"
               "  let i = i + 1\n"
               "end\n"
               "print \"Lowercase: \" + result + \"\\n\"\n";
    }
    
    if(my_strcmp(name, "textcapitalize.wnc") == 0) {
        return "input \"Text: \" text\n"
               "let result = \"\"\n"
               "let cap = 1\n"
               "let i = 0\n"
               "while i < len(text)\n"
               "  let c = text[i]\n"
               "  if cap and c >= 97 and c <= 122 then\n"
               "    result = result + chr(c - 32)\n"
               "    cap = 0\n"
               "  else\n"
               "    result = result + chr(c)\n"
               "    if c == 32 then cap = 1\n"
               "  end\n"
               "  let i = i + 1\n"
               "end\n"
               "print \"Capitalized: \" + result + \"\\n\"\n";
    }
    
    if(my_strcmp(name, "texttrim.wnc") == 0) {
        return "input \"Text: \" text\n"
               "let start = 0\n"
               "let end = len(text) - 1\n"
               "while start < len(text) and text[start] == 32\n"
               "  start = start + 1\n"
               "end\n"
               "while end >= 0 and text[end] == 32\n"
               "  end = end - 1\n"
               "end\n"
               "let result = \"\"\n"
               "let i = start\n"
               "while i <= end\n"
               "  result = result + text[i]\n"
               "  let i = i + 1\n"
               "end\n"
               "print \"Trimmed: \\\"\" + result + \"\\\"\\n\"\n";
    }
    
    if(my_strcmp(name, "textreplace.wnc") == 0) {
        return "input \"Text: \" text\n"
               "input \"Find: \" find\n"
               "input \"Replace: \" replace\n"
               "let result = \"\"\n"
               "let i = 0\n"
               "while i < len(text)\n"
               "  let match = 1\n"
               "  let j = 0\n"
               "  while j < len(find)\n"
               "    if i + j >= len(text) or text[i + j] != find[j] then\n"
               "      match = 0\n"
               "      break\n"
               "    end\n"
               "    j = j + 1\n"
               "  end\n"
               "  if match then\n"
               "    result = result + replace\n"
               "    i = i + len(find)\n"
               "  else\n"
               "    result = result + text[i]\n"
               "    i = i + 1\n"
               "  end\n"
               "end\n"
               "print \"Result: \" + result + \"\\n\"\n";
    }
    
    if(my_strcmp(name, "textsort.wnc") == 0) {
        return "print \"Sort lines manually (use external tool)\"\n"
               "print \"Type 'sort' command in shell\"\n";
    }
    
    if(my_strcmp(name, "textunique.wnc") == 0) {
        return "print \"Remove duplicate lines manually\"\n";
    }
    
    
    if(my_strcmp(name, "guess_num.wnc") == 0) {
        return "print \"=== GUESS THE NUMBER ===\"\n"
               "let secret = rand(1, 100)\n"
               "let attempts = 0\n"
               "while 1\n"
               "  input \"Your guess (1-100): \" guess\n"
               "  attempts = attempts + 1\n"
               "  if guess < secret then\n"
               "    print \"Too low!\\n\"\n"
               "  elseif guess > secret then\n"
               "    print \"Too high!\\n\"\n"
               "  else\n"
               "    print \"Correct! You got it in \" + attempts + \" attempts!\\n\"\n"
               "    break\n"
               "  end\n"
               "end\n";
    }
    
    if(my_strcmp(name, "rps_game.wnc") == 0) {
        return "print \"=== ROCK PAPER SCISSORS ===\"\n"
               "let choices = \"rock,paper,scissors\"\n"
               "while 1\n"
               "  input \"Your choice (rock/paper/scissors/quit): \" player\n"
               "  if player == \"quit\" then break\n"
               "  let comp = rand(0, 2)\n"
               "  print \"Computer: \" + choices[comp] + \"\\n\"\n"
               "  if player == choices[comp] then\n"
               "    print \"Draw!\\n\"\n"
               "  elseif (player == \"rock\" and comp == 2) or\n"
               "        (player == \"paper\" and comp == 0) or\n"
               "        (player == \"scissors\" and comp == 1) then\n"
               "    print \"You win!\\n\"\n"
               "  else\n"
               "    print \"You lose!\\n\"\n"
               "  end\n"
               "end\n";
    }
    
    if(my_strcmp(name, "dice_roller.wnc") == 0) {
        return "print \"=== DICE ROLLER ===\"\n"
               "input \"Number of dice: \" count\n"
               "let total = 0\n"
               "let i = 0\n"
               "while i < count\n"
               "  let roll = rand(1, 6)\n"
               "  print \"Die \" + (i + 1) + \": \" + roll + \"\\n\"\n"
               "  total = total + roll\n"
               "  let i = i + 1\n"
               "end\n"
               "print \"Total: \" + total + \"\\n\"\n";
    }
    
    if(my_strcmp(name, "coin_flip.wnc") == 0) {
        return "print \"=== COIN FLIP ===\"\n"
               "input \"Flip (h/t): \" choice\n"
               "let result = rand(0, 1)\n"
               "let coin = \"\"\n"
               "if result == 0 then coin = \"heads\" else coin = \"tails\"\n"
               "print \"Coin: \" + coin + \"\\n\"\n"
               "if (choice == \"h\" and result == 0) or (choice == \"t\" and result == 1) then\n"
               "  print \"You win!\\n\"\n"
               "else\n"
               "  print \"You lose!\\n\"\n"
               "end\n";
    }
    
    if(my_strcmp(name, "hangman.wnc") == 0) {
        return "print \"=== HANGMAN ===\"\n"
               "let words = \"python,java,cplusplus,assembly,linux,kernel,driver,compiler,debugger,terminal\"\n"
               "let word = words[rand(0, 9)]\n"
               "let guessed = \"\"\n"
               "let attempts = 6\n"
               "let i = 0\n"
               "while i < len(word)\n"
               "  guessed = guessed + \"_\"\n"
               "  i = i + 1\n"
               "end\n"
               "while attempts > 0\n"
               "  print \"\\nWord: \" + guessed + \"\\n\"\n"
               "  print \"Attempts left: \" + attempts + \"\\n\"\n"
               "  input \"Guess a letter: \" letter\n"
               "  let found = 0\n"
               "  let i = 0\n"
               "  while i < len(word)\n"
               "    if word[i] == letter then\n"
               "      guessed[i] = letter\n"
               "      found = 1\n"
               "    end\n"
               "    i = i + 1\n"
               "  end\n"
               "  if found == 0 then\n"
               "    attempts = attempts - 1\n"
               "    print \"Wrong!\\n\"\n"
               "  end\n"
               "  if guessed == word then\n"
               "    print \"You win! The word was: \" + word + \"\\n\"\n"
               "    break\n"
               "  end\n"
               "end\n"
               "if attempts == 0 then print \"Game over! The word was: \" + word + \"\\n\"\n";
    }
    
    if(my_strcmp(name, "tictactoe.wnc") == 0) {
        return "print \"=== TIC TAC TOE ===\"\n"
               "let board = \".........\"\n"
               "let player = \"X\"\n"
               "let moves = 0\n"
               "while moves < 9\n"
               "  print \"\\n\"\n"
               "  print \" \" + board[0] + \" | \" + board[1] + \" | \" + board[2] + \"\\n\"\n"
               "  print \"---+---+---\\n\"\n"
               "  print \" \" + board[3] + \" | \" + board[4] + \" | \" + board[5] + \"\\n\"\n"
               "  print \"---+---+---\\n\"\n"
               "  print \" \" + board[6] + \" | \" + board[7] + \" | \" + board[8] + \"\\n\"\n"
               "  input \"Position (0-8): \" pos\n"
               "  if board[pos] != \".\" then\n"
               "    print \"Position taken!\\n\"\n"
               "    continue\n"
               "  end\n"
               "  board[pos] = player\n"
               "  moves = moves + 1\n"
               "  let win = 0\n"
               "  let wins = \"012,345,678,036,147,258,048,246\"\n"
               "  let i = 0\n"
               "  while i < 8\n"
               "    let a = wins[i*3]\n"
               "    let b = wins[i*3+1]\n"
               "    let c = wins[i*3+2]\n"
               "    if board[a] == player and board[b] == player and board[c] == player then\n"
               "      win = 1\n"
               "      break\n"
               "    end\n"
               "    i = i + 1\n"
               "  end\n"
               "  if win then\n"
               "    print \"Player \" + player + \" wins!\\n\"\n"
               "    break\n"
               "  end\n"
               "  if player == \"X\" then player = \"O\" else player = \"X\"\n"
               "end\n"
               "if moves == 9 then print \"Draw!\\n\"\n";
    }
    
    if(my_strcmp(name, "blackjack.wnc") == 0) {
        return "print \"=== BLACKJACK ===\"\n"
               "let player_hand = 0\n"
               "let dealer_hand = 0\n"
               "let card = rand(1, 11)\n"
               "player_hand = player_hand + card\n"
               "print \"Your card: \" + card + \"\\n\"\n"
               "card = rand(1, 11)\n"
               "player_hand = player_hand + card\n"
               "print \"Your card: \" + card + \"\\n\"\n"
               "print \"Your total: \" + player_hand + \"\\n\"\n"
               "card = rand(1, 11)\n"
               "dealer_hand = dealer_hand + card\n"
               "print \"Dealer shows: \" + card + \"\\n\"\n"
               "while 1\n"
               "  input \"Hit or stand? (h/s): \" choice\n"
               "  if choice == \"h\" then\n"
               "    card = rand(1, 11)\n"
               "    player_hand = player_hand + card\n"
               "    print \"You drew: \" + card + \"\\n\"\n"
               "    print \"Your total: \" + player_hand + \"\\n\"\n"
               "    if player_hand > 21 then\n"
               "      print \"Bust! You lose.\\n\"\n"
               "      return\n"
               "    end\n"
               "  else\n"
               "    break\n"
               "  end\n"
               "end\n"
               "print \"Dealer's turn...\\n\"\n"
               "while dealer_hand < 17\n"
               "  card = rand(1, 11)\n"
               "  dealer_hand = dealer_hand + card\n"
               "  print \"Dealer drew: \" + card + \"\\n\"\n"
               "  print \"Dealer total: \" + dealer_hand + \"\\n\"\n"
               "end\n"
               "if dealer_hand > 21 then\n"
               "  print \"Dealer busts! You win!\\n\"\n"
               "elseif player_hand > dealer_hand then\n"
               "  print \"You win!\\n\"\n"
               "elseif dealer_hand > player_hand then\n"
               "  print \"Dealer wins!\\n\"\n"
               "else\n"
               "  print \"Push!\\n\"\n"
               "end\n";
    }
    
    if(my_strcmp(name, "slot_machine.wnc") == 0) {
        return "print \"=== SLOT MACHINE ===\"\n"
               "let coins = 100\n"
               "while coins > 0\n"
               "  print \"\\nCoins: \" + coins + \"\\n\"\n"
               "  input \"Bet (1-10, q to quit): \" bet\n"
               "  if bet == \"q\" then break\n"
               "  let bet_val = bet\n"
               "  if bet_val < 1 then bet_val = 1\n"
               "  if bet_val > 10 then bet_val = 10\n"
               "  if bet_val > coins then\n"
               "    print \"Not enough coins!\\n\"\n"
               "    continue\n"
               "  end\n"
               "  let slot1 = rand(1, 7)\n"
               "  let slot2 = rand(1, 7)\n"
               "  let slot3 = rand(1, 7)\n"
               "  print \"[ \" + slot1 + \" ] [ \" + slot2 + \" ] [ \" + slot3 + \" ]\\n\"\n"
               "  if slot1 == slot2 and slot2 == slot3 then\n"
               "    let win = bet_val * 10\n"
               "    coins = coins + win\n"
               "    print \"JACKPOT! You win \" + win + \" coins!\\n\"\n"
               "  elseif slot1 == slot2 or slot2 == slot3 or slot1 == slot3 then\n"
               "    let win = bet_val * 2\n"
               "    coins = coins + win\n"
               "    print \"You win \" + win + \" coins!\\n\"\n"
               "  else\n"
               "    coins = coins - bet_val\n"
               "    print \"You lose!\\n\"\n"
               "  end\n"
               "end\n"
               "print \"Game over! Final coins: \" + coins + \"\\n\"\n";
    }
    
    if(my_strcmp(name, "roulette.wnc") == 0) {
        return "print \"=== ROULETTE ===\"\n"
               "let coins = 100\n"
               "while coins > 0\n"
               "  print \"\\nCoins: \" + coins + \"\\n\"\n"
               "  input \"Bet on (0-36): \" number\n"
               "  let bet = number\n"
               "  if bet < 0 or bet > 36 then\n"
               "    print \"Invalid number!\\n\"\n"
               "    continue\n"
               "  end\n"
               "  let ball = rand(0, 36)\n"
               "  print \"Ball landed on: \" + ball + \"\\n\"\n"
               "  if ball == bet then\n"
               "    coins = coins + 35\n"
               "    print \"You win 35 coins!\\n\"\n"
               "  else\n"
               "    coins = coins - 1\n"
               "    print \"You lose 1 coin!\\n\"\n"
               "  end\n"
               "end\n"
               "print \"Game over!\\n\"\n";
    }
    
    if(my_strcmp(name, "word_scramble.wnc") == 0) {
        return "print \"=== WORD SCRAMBLE ===\"\n"
               "let words = \"computer,keyboard,monitor,printer,network,kernel,driver,memory,processor,hardware\"\n"
               "let original = words[rand(0, 9)]\n"
               "let scrambled = \"\"\n"
               "let indices = array_create(len(original))\n"
               "let i = 0\n"
               "while i < len(original)\n"
               "  indices[i] = i\n"
               "  i = i + 1\n"
               "end\n"
               "i = len(original) - 1\n"
               "while i > 0\n"
               "  let j = rand(0, i)\n"
               "  let t = indices[i]\n"
               "  indices[i] = indices[j]\n"
               "  indices[j] = t\n"
               "  i = i - 1\n"
               "end\n"
               "i = 0\n"
               "while i < len(original)\n"
               "  scrambled = scrambled + original[indices[i]]\n"
               "  i = i + 1\n"
               "end\n"
               "print \"Scrambled: \" + scrambled + \"\\n\"\n"
               "input \"Your guess: \" guess\n"
               "if guess == original then\n"
               "  print \"Correct!\\n\"\n"
               "else\n"
               "  print \"Wrong! The word was: \" + original + \"\\n\"\n"
               "end\n";
    }
    
    if(my_strcmp(name, "math_race.wnc") == 0) {
        return "print \"=== MATH RACE ===\"\n"
               "let score = 0\n"
               "let i = 0\n"
               "while i < 10\n"
               "  let a = rand(1, 10)\n"
               "  let b = rand(1, 10)\n"
               "  let op = rand(0, 2)\n"
               "  if op == 0 then\n"
               "    let answer = a + b\n"
               "    print a + \" + \" + b + \" = \"\n"
               "  elseif op == 1 then\n"
               "    let answer = a - b\n"
               "    print a + \" - \" + b + \" = \"\n"
               "  else\n"
               "    let answer = a * b\n"
               "    print a + \" * \" + b + \" = \"\n"
               "  end\n"
               "  input \"\" user\n"
               "  if user == answer then\n"
               "    score = score + 10\n"
               "    print \"Correct! +10 points\\n\"\n"
               "  else\n"
               "    print \"Wrong! Answer was \" + answer + \"\\n\"\n"
               "  end\n"
               "  i = i + 1\n"
               "end\n"
               "print \"Final score: \" + score + \"\\n\"\n";
    }
    
    if(my_strcmp(name, "typing_game.wnc") == 0) {
        return "print \"=== TYPING GAME ===\"\n"
               "let words = \"the,quick,brown,fox,jumps,over,lazy,dog,hello,world,typing,game,speed,test\"\n"
               "let score = 0\n"
               "let i = 0\n"
               "while i < 5\n"
               "  let word = words[rand(0, 13)]\n"
               "  print \"Type: \" + word + \"\\n\"\n"
               "  input \">\" user\n"
               "  if user == word then\n"
               "    score = score + 20\n"
               "    print \"Correct! +20 points\\n\"\n"
               "  else\n"
               "    print \"Wrong! The word was: \" + word + \"\\n\"\n"
               "  end\n"
               "  i = i + 1\n"
               "end\n"
               "print \"Final score: \" + score + \"\\n\"\n";
    }
    
    if(my_strcmp(name, "memory_game.wnc") == 0) {
        return "print \"=== MEMORY GAME ===\"\n"
               "let numbers = \"\"\n"
               "let i = 0\n"
               "while i < 5\n"
               "  numbers = numbers + rand(0, 9)\n"
               "  i = i + 1\n"
               "end\n"
               "print \"Memorize: \" + numbers + \"\\n\"\n"
               "sleep 3\n"
               "graph fill BLACK\n"
               "input \"Enter the sequence: \" guess\n"
               "if guess == numbers then\n"
               "  print \"Correct!\\n\"\n"
               "else\n"
               "  print \"Wrong! The sequence was: \" + numbers + \"\\n\"\n"
               "end\n";
    }
    
    if(my_strcmp(name, "riddle_game.wnc") == 0) {
        return "print \"=== RIDDLE GAME ===\"\n"
               "let riddles = \"What has keys but no locks? (keyboard),What has a face and two hands but no arms? (clock),What gets wetter as it dries? (towel),What has a head, a tail, but no body? (coin),What has to be broken before you can use it? (egg),What runs but never walks? (river),What has a heart that doesn't beat? (artichoke)\"\n"
               "let answers = \"keyboard,clock,towel,coin,egg,river,artichoke\"\n"
               "let idx = rand(0, 6)\n"
               "let riddle = riddles[idx]\n"
               "let answer = answers[idx]\n"
               "print \"Riddle: \" + riddle + \"\\n\"\n"
               "input \"Answer: \" guess\n"
               "if guess == answer then\n"
               "  print \"Correct!\\n\"\n"
               "else\n"
               "  print \"Wrong! The answer was: \" + answer + \"\\n\"\n"
               "end\n";
    }
    
    if(my_strcmp(name, "trivia_game.wnc") == 0) {
        return "print \"=== TRIVIA GAME ===\"\n"
               "let questions = \"What is the capital of France? (Paris),What is 2+2? (4),What color is the sky? (blue),Who wrote Romeo and Juliet? (Shakespeare),What is the largest planet? (Jupiter)\"\n"
               "let answers = \"paris,4,blue,shakespeare,jupiter\"\n"
               "let score = 0\n"
               "let i = 0\n"
               "while i < 5\n"
               "  let q = questions[i]\n"
               "  let a = answers[i]\n"
               "  print \"Q\" + (i+1) + \": \" + q + \"\\n\"\n"
               "  input \"Your answer: \" guess\n"
               "  if guess == a then\n"
               "    score = score + 20\n"
               "    print \"Correct!\\n\"\n"
               "  else\n"
               "    print \"Wrong! Answer was: \" + a + \"\\n\"\n"
               "  end\n"
               "  i = i + 1\n"
               "end\n"
               "print \"Final score: \" + score + \"\\n\"\n";
    }
    
    
    if(my_strcmp(name, "matrix_rain.wnc") == 0) {
        return "run matrix\n";
    }
    
    if(my_strcmp(name, "fire_effect.wnc") == 0) {
        return "run fire\n";
    }
    
    if(my_strcmp(name, "starfield.wnc") == 0) {
        return "run stars\n";
    }
    
    if(my_strcmp(name, "plasma_effect.wnc") == 0) {
        return "run plasma\n";
    }
    
    if(my_strcmp(name, "wave_effect.wnc") == 0) {
        return "run waves\n";
    }
    
    if(my_strcmp(name, "tunnel_3d.wnc") == 0) {
        return "run tunnel\n";
    }
    
    if(my_strcmp(name, "snowfall.wnc") == 0) {
        return "run snow\n";
    }
    
    if(my_strcmp(name, "fireworks.wnc") == 0) {
        return "graph fill BLACK\n"
               "let frame = 0\n"
               "while frame < 50\n"
               "  let x = rand(10, 310)\n"
               "  let y = rand(10, 200)\n"
               "  let r = rand(0, 15)\n"
               "  graph circle x y 5 r\n"
               "  sleep 0.1\n"
               "  frame = frame + 1\n"
               "end\n";
    }
    
    if(my_strcmp(name, "particles.wnc") == 0) {
        return "graph fill BLACK\n"
               "let i = 0\n"
               "while i < 100\n"
               "  let x = rand(0, 320)\n"
               "  let y = rand(0, 200)\n"
               "  let c = rand(0, 15)\n"
               "  graph pixel x y c\n"
               "  i = i + 1\n"
               "end\n"
               "print \"100 particles drawn\\n\"\n";
    }
    
    if(my_strcmp(name, "kaleidoscope.wnc") == 0) {
        return "graph fill BLACK\n"
               "let i = 0\n"
               "while i < 50\n"
               "  let x = rand(0, 160)\n"
               "  let y = rand(0, 100)\n"
               "  let c = rand(0, 15)\n"
               "  graph pixel x y c\n"
               "  graph pixel 319-x y c\n"
               "  graph pixel x 199-y c\n"
               "  graph pixel 319-x 199-y c\n"
               "  i = i + 1\n"
               "end\n"
               "print \"Kaleidoscope pattern\\n\"\n";
    }
    
    
    if(my_strcmp(name, "fortune.wnc") == 0) {
        return "let fortunes = \"You will have a great day!,Tomorrow will be better,Watch out for surprises,Success is near,Stay positive,Happiness is coming,You will meet someone special,A new opportunity awaits,Trust your instincts,Dream big\"\n"
               "let idx = rand(0, 9)\n"
               "print \"Fortune: \" + fortunes[idx] + \"\\n\"\n";
    }
    
    if(my_strcmp(name, "cowsay.wnc") == 0) {
        return "input \"Message: \" msg\n"
               "print \" __________\\n\"\n"
               "print \"< \" + msg + \" >\\n\"\n"
               "print \" ----------\\n\"\n"
               "print \"        \\\\   ^__^\\n\"\n"
               "print \"         \\\\  (oo)\\\\_______\\n\"\n"
               "print \"            (__)\\\\       )\\\\/\\\\\\n\"\n"
               "print \"                ||----w |\\n\"\n"
               "print \"                ||     ||\\n\"\n";
    }
    
    if(my_strcmp(name, "magic8ball.wnc") == 0) {
        return "input \"Ask a question: \" question\n"
               "let answers = \"Yes,No,Maybe,Ask again later,Definitely,Probably not,Outlook good,Very doubtful,Certainly,Not likely\"\n"
               "let idx = rand(0, 9)\n"
               "print \"8-Ball says: \" + answers[idx] + \"\\n\"\n";
    }
    
    if(my_strcmp(name, "joke_teller.wnc") == 0) {
        return "let jokes = \"Why did the chicken cross the road? To get to the other side!,What do you call a fake noodle? An impasta!,Why don't scientists trust atoms? Because they make up everything!,What do you call a fish with no eyes? A fsh!,Why did the scarecrow win an award? Because he was outstanding in his field!\"\n"
               "let idx = rand(0, 4)\n"
               "print \"Joke: \" + jokes[idx] + \"\\n\"\n";
    }
    
    if(my_strcmp(name, "compliment.wnc") == 0) {
        return "let compliments = \"You are awesome!,You look great today!,You are very smart!,You are doing a fantastic job!,You are a genius!,You are very creative!,You have a great sense of humor!,You are a wonderful person!\"\n"
               "let idx = rand(0, 7)\n"
               "print \"Compliment: \" + compliments[idx] + \"\\n\"\n";
    }
    
    if(my_strcmp(name, "insult_gen.wnc") == 0) {
        return "let insults = \"You have a keyboard and a soul? I'd return the soul.,You bring joy to everyone you leave.,You're like a software update. I see you, but I ignore you.,You're the 'runtime error' of people.,You have the charm of a segfault.\"\n"
               "let idx = rand(0, 4)\n"
               "print \"Insult: \" + insults[idx] + \"\\n\"\n";
    }
    
    if(my_strcmp(name, "affirmation.wnc") == 0) {
        return "let affirmations = \"I am capable of achieving my goals,I believe in myself,I am worthy of success,I am strong and resilient,I am learning and growing every day,I deserve happiness,I am enough,I can handle anything that comes my way\"\n"
               "let idx = rand(0, 7)\n"
               "print \"Affirmation: \" + affirmations[idx] + \"\\n\"\n";
    }
    
    if(my_strcmp(name, "haiku_gen.wnc") == 0) {
        return "let lines1 = \"An old silent pond,The light of a candle,In the twilight rain,Over the wintry forest,Piercing the darkness\"\n"
               "let lines2 = \"A frog jumps into the pond,Is transferred to another candle,Brilliantly shining,The wind howls in rage,A single star appears\"\n"
               "let lines3 = \"Splash! Silence again.,Spring rain.,In the evening rain.,Bare trees stand.,Guiding me home.\"\n"
               "let a = rand(0, 4)\n"
               "let b = rand(0, 4)\n"
               "let c = rand(0, 4)\n"
               "print lines1[a] + \"\\n\"\n"
               "print lines2[b] + \"\\n\"\n"
               "print lines3[c] + \"\\n\"\n";
    }
    
    if(my_strcmp(name, "madlibs.wnc") == 0) {
        return "print \"=== MAD LIBS ===\"\n"
               "input \"Enter a noun: \" noun1\n"
               "input \"Enter a verb: \" verb1\n"
               "input \"Enter an adjective: \" adj1\n"
               "input \"Enter another noun: \" noun2\n"
               "input \"Enter a place: \" place\n"
               "print \"\\n--- STORY ---\\n\"\n"
               "print \"One day, a \" + adj1 + \" \" + noun1 + \" decided to \" + verb1 + \" to \" + place + \".\\n\"\n"
               "print \"There, it met a \" + noun2 + \" and they became best friends.\\n\"\n"
               "print \"The end!\\n\"\n";
    }
    
    if(my_strcmp(name, "zodiac_sign.wnc") == 0) {
        return "input \"Enter your birth month (1-12): \" month\n"
               "input \"Enter your birth day: \" day\n"
               "if (month == 3 and day >= 21) or (month == 4 and day <= 19) then\n"
               "  print \"Aries: March 21 - April 19\\n\"\n"
               "elseif (month == 4 and day >= 20) or (month == 5 and day <= 20) then\n"
               "  print \"Taurus: April 20 - May 20\\n\"\n"
               "elseif (month == 5 and day >= 21) or (month == 6 and day <= 20) then\n"
               "  print \"Gemini: May 21 - June 20\\n\"\n"
               "elseif (month == 6 and day >= 21) or (month == 7 and day <= 22) then\n"
               "  print \"Cancer: June 21 - July 22\\n\"\n"
               "elseif (month == 7 and day >= 23) or (month == 8 and day <= 22) then\n"
               "  print \"Leo: July 23 - August 22\\n\"\n"
               "elseif (month == 8 and day >= 23) or (month == 9 and day <= 22) then\n"
               "  print \"Virgo: August 23 - September 22\\n\"\n"
               "elseif (month == 9 and day >= 23) or (month == 10 and day <= 22) then\n"
               "  print \"Libra: September 23 - October 22\\n\"\n"
               "elseif (month == 10 and day >= 23) or (month == 11 and day <= 21) then\n"
               "  print \"Scorpio: October 23 - November 21\\n\"\n"
               "elseif (month == 11 and day >= 22) or (month == 12 and day <= 21) then\n"
               "  print \"Sagittarius: November 22 - December 21\\n\"\n"
               "elseif (month == 12 and day >= 22) or (month == 1 and day <= 19) then\n"
               "  print \"Capricorn: December 22 - January 19\\n\"\n"
               "elseif (month == 1 and day >= 20) or (month == 2 and day <= 18) then\n"
               "  print \"Aquarius: January 20 - February 18\\n\"\n"
               "elseif (month == 2 and day >= 19) or (month == 3 and day <= 20) then\n"
               "  print \"Pisces: February 19 - March 20\\n\"\n"
               "else\n"
               "  print \"Invalid date!\\n\"\n"
               "end\n";
    }
    
    
    if(my_strcmp(name, "backup_sys.wnc") == 0) {
        return "print \"=== SYSTEM BACKUP ===\"\n"
               "print \"Backing up system files...\\n\"\n"
               "run wnakasxs\n";
    }
    
    if(my_strcmp(name, "restore_sys.wnc") == 0) {
        return "print \"=== SYSTEM RESTORE ===\"\n"
               "print \"WARNING: This will restore system files!\\n\"\n"
               "input \"Type 'yes' to confirm: \" confirm\n"
               "if confirm == \"yes\" then\n"
               "  run wnakasxs_restore\n"
               "else\n"
               "  print \"Cancelled\\n\"\n"
               "end\n";
    }
    
    if(my_strcmp(name, "log_cleaner.wnc") == 0) {
        return "print \"=== LOG CLEANER ===\"\n"
               "print \"Cleaning system logs...\\n\"\n"
               "run clean\n";
    }
    
    if(my_strcmp(name, "disk_analyzer.wnc") == 0) {
        return "print \"=== DISK ANALYZER ===\"\n"
               "run df\n"
               "run speed\n";
    }
    
    if(my_strcmp(name, "process_killer.wnc") == 0) {
        return "print \"=== PROCESS KILLER ===\"\n"
               "run ps\n"
               "input \"PID to kill: \" pid\n"
               "run kill \" + pid\n";
    }
    
    if(my_strcmp(name, "service_mgr.wnc") == 0) {
        return "print \"=== SERVICE MANAGER ===\"\n"
               "print \"1. Start service\\n\"\n"
               "print \"2. Stop service\\n\"\n"
               "print \"3. List services\\n\"\n"
               "input \"Choice: \" choice\n"
               "if choice == 1 or choice == 2 then\n"
               "  input \"Service name: \" svc\n"
               "elseif choice == 3 then\n"
               "  print \"services: network, sound, gui\\n\"\n"
               "end\n";
    }
    
    if(my_strcmp(name, "startup_mgr.wnc") == 0) {
        return "print \"=== STARTUP MANAGER ===\"\n"
               "print \"1. Add to startup\\n\"\n"
               "print \"2. Remove from startup\\n\"\n"
               "print \"3. List startup items\\n\"\n"
               "input \"Choice: \" choice\n";
    }
    
    if(my_strcmp(name, "network_info.wnc") == 0) {
        return "run netinfo\n";
    }
    
    if(my_strcmp(name, "ping_test.wnc") == 0) {
        return "input \"Host to ping: \" host\n"
               "run ping \" + host + \"\n";
    }
    
    if(my_strcmp(name, "trace_route.wnc") == 0) {
        return "print \"=== TRACE ROUTE ===\"\n"
               "input \"Destination: \" dest\n"
               "print \"Tracing route to \" + dest + \"...\\n\"\n"
               "let i = 1\n"
               "while i <= 30\n"
               "  print \" \" + i + \"  * * * Request timed out\\n\"\n"
               "  i = i + 1\n"
               "end\n";
    }
    
    return "print \"Script: \" + name + \"\\n\"\n"
           "print \"WnkC Script v1.0\\n\"\n"
           "print \"Ready to use!\\n\"\n";
}

static void save_selected_components(void) {
    uint16_t components_buf[256] = {0};
    
    for(int i = 0; i < script_count && i < 256; i++) {
        if(wnc_scripts[i].enabled) {
            components_buf[i / 16] |= (1 << (i % 16));
        }
    }
    
    components_buf[255] = 0x574E;
    write_sector(110, components_buf);
}

static void load_selected_components(void) {
    uint16_t components_buf[256] = {0};
    read_sector(110, components_buf);
    
    if(components_buf[255] != 0x574E) {
        return;
    }
    
    for(int i = 0; i < script_count && i < 256; i++) {
        wnc_scripts[i].enabled = (components_buf[i / 16] >> (i % 16)) & 1;
    }
}

static void show_script_preview(int idx) {
    clear_screen_bg(COLOR_GRAY);
    
    draw_shadow_window(5, 1, 70, 22, COLOR_BLUE, TXT_WHITE, "SCRIPT PREVIEW");
    
    kprint_at("Name:", 7, 3, (COLOR_BLUE << 4) | TXT_CYAN);
    kprint_at(wnc_scripts[idx].name, 13, 3, (COLOR_BLUE << 4) | TXT_YELLOW);
    
    kprint_at("Category:", 7, 4, (COLOR_BLUE << 4) | TXT_CYAN);
    kprint_at(category_names[wnc_scripts[idx].category], 17, 4, (COLOR_BLUE << 4) | TXT_GREEN);
    
    kprint_at("Size:", 7, 5, (COLOR_BLUE << 4) | TXT_CYAN);
    kprint_int_at(wnc_scripts[idx].size, 13, 5, (COLOR_BLUE << 4) | TXT_WHITE);
    kprint_at(" bytes", 17, 5, (COLOR_BLUE << 4) | TXT_WHITE);
    
    kprint_at("Description:", 7, 6, (COLOR_BLUE << 4) | TXT_CYAN);
    kprint_at(wnc_scripts[idx].desc, 7, 7, (COLOR_BLUE << 4) | TXT_WHITE);
    
    kprint_at("Status:", 7, 8, (COLOR_BLUE << 4) | TXT_CYAN);
    if(wnc_scripts[idx].enabled) {
        kprint_at("[INSTALLED]", 15, 8, (COLOR_BLUE << 4) | TXT_GREEN);
    } else {
        kprint_at("[NOT INSTALLED]", 15, 8, (COLOR_BLUE << 4) | TXT_RED);
    }
    
    draw_hline(7, 10, 66, COLOR_BLUE, TXT_WHITE, S_HLINE);
    kprint_at("CODE PREVIEW:", 7, 11, (COLOR_BLUE << 4) | TXT_YELLOW);
    
    const char* code = get_script_content(wnc_scripts[idx].name);
    int y = 12;
    int x = 7;
    int i = 0;
    while(code[i] && y < 21) {
        if(code[i] == '\n') {
            y++;
            x = 7;
        } else if(x < 72) {
            char s[2] = {code[i], 0};
            kprint_at(s, x, y, (COLOR_BLUE << 4) | TXT_WHITE);
            x++;
        }
        i++;
    }
    
    kprint_at("[I]nstall [U]ninstall [B]ack", 20, 22, (COLOR_BLACK << 4) | TXT_CYAN);
}

static void search_scripts(void) {
    char search_term[32] = {0};
    int search_pos = 0;
    int searching = 1;
    
    clear_screen_bg(COLOR_GRAY);
    draw_shadow_window(10, 5, 60, 10, COLOR_BLUE, TXT_WHITE, "SEARCH SCRIPTS");
    kprint_at("Enter search term:", 14, 8, (COLOR_BLUE << 4) | TXT_WHITE);
    
    while(searching) {
        kprint_at("> ", 14, 10, (COLOR_BLUE << 4) | TXT_GREEN);
        kprint_at(search_term, 16, 10, (COLOR_BLUE << 4) | TXT_WHITE);
        kprint_at("_", 16 + search_pos, 10, (COLOR_BLUE << 4) | TXT_RED);
        move_cursor(16 + search_pos, 10);
        
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc == 0x1C) {
                searching = 0;
            }
            else if(sc == 0x01) {
                return;
            }
            else if(sc == 0x0E && search_pos > 0) { 
                search_pos--;
                search_term[search_pos] = '\0';
            }
            else if(search_pos < 31) {
                char ch = scancode_to_ascii(sc);
                if(ch) {
                    search_term[search_pos++] = ch;
                    search_term[search_pos] = '\0';
                }
            }
            while(inb(0x64) & 1) inb(0x60);
        }
        inst_delay(30);
    }
    
    int results[100];
    int result_count = 0;
    
    for(int i = 0; i < script_count; i++) {
        if(my_strstr(wnc_scripts[i].name, search_term) || 
           my_strstr(wnc_scripts[i].desc, search_term)) {
            if(result_count < 100) {
                results[result_count++] = i;
            }
        }
    }
    
    clear_screen_bg(COLOR_GRAY);
    
    if(result_count == 0) {
        draw_shadow_window(15, 8, 50, 6, COLOR_BLUE, TXT_WHITE, "NO RESULTS");
        kprint_at("No scripts found for:", 18, 11, (COLOR_BLUE << 4) | TXT_WHITE);
        kprint_at(search_term, 18, 12, (COLOR_BLUE << 4) | TXT_YELLOW);
        kprint_at("Press any key...", 25, 14, (COLOR_BLUE << 4) | TXT_CYAN);
        while(!(inb(0x64) & 1));
        while(inb(0x64) & 1) inb(0x60);
        return;
    }
    
    int selected_result = 0;
    int result_scroll = 0;
    int result_redraw = 1;
    int result_running = 1;
    
    while(result_running) {
        if(result_redraw) {
            clear_screen_bg(COLOR_GRAY);
            
            char title[64];
            my_sprintf(title, "SEARCH: %s (%d found)", search_term, result_count);
            draw_shadow_window(10, 1, 60, 22, COLOR_BLUE, TXT_WHITE, title);
            
            for(int i = 0; i < 18 && result_scroll + i < result_count; i++) {
                int idx = results[result_scroll + i];
                uint8_t color = (result_scroll + i == selected_result) ? TXT_GREEN : TXT_WHITE;
                
                kprint_at(wnc_scripts[idx].enabled ? "[X]" : "[ ]", 14, 4 + i, (COLOR_BLUE << 4) | color);
                kprint_at(wnc_scripts[idx].name, 19, 4 + i, (COLOR_BLUE << 4) | color);
                kprint_at("- ", 40, 4 + i, (COLOR_BLUE << 4) | color);
                kprint_at(wnc_scripts[idx].desc, 42, 4 + i, (COLOR_BLUE << 4) | color);
            }
            
            kprint_at("SPACE:Toggle  ENTER:Preview  A:All  N:None  ESC:Back", 12, 22, (COLOR_BLACK << 4) | TXT_CYAN);
            result_redraw = 0;
        }
        
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc < 0x80) {
                if(sc == 0x48 && selected_result > 0) {
                    selected_result--;
                    if(selected_result < result_scroll) result_scroll = selected_result;
                    result_redraw = 1;
                }
                else if(sc == 0x50 && selected_result < result_count - 1) {
                    selected_result++;
                    if(selected_result >= result_scroll + 18) result_scroll = selected_result - 17;
                    result_redraw = 1;
                }
                else if(sc == 0x39) { 
                    int idx = results[selected_result];
                    wnc_scripts[idx].enabled = !wnc_scripts[idx].enabled;
                    result_redraw = 1;
                }
                else if(sc == 0x1C) { 
                    int idx = results[selected_result];
                    show_script_preview(idx);
                    result_redraw = 1;
                }
                else if(sc == 0x1E) {
                    for(int i = 0; i < result_count; i++) {
                        wnc_scripts[results[i]].enabled = 1;
                    }
                    result_redraw = 1;
                }
                else if(sc == 0x31) {
                    for(int i = 0; i < result_count; i++) {
                        wnc_scripts[results[i]].enabled = 0;
                    }
                    result_redraw = 1;
                }
                else if(sc == 0x01) {
                    result_running = 0;
                }
            }
            while(inb(0x64) & 1) inb(0x60);
        }
        inst_delay(30);
    }
}

static void select_scripts_in_category(int category) {
    int selected = 0;
    int scroll = 0;
    int visible = 16;
    int redraw = 1;
    int running = 1;
    
    int cat_scripts[100];
    int cat_count = 0;
    for(int i = 0; i < script_count; i++) {
        if(wnc_scripts[i].category == category) {
            cat_scripts[cat_count++] = i;
        }
    }
    
    if(cat_count == 0) {
        kprint_at("No scripts in this category!", 20, 12, (COLOR_BLACK << 4) | TXT_YELLOW);
        inst_delay(1000);
        return;
    }
    
    while(running) {
        if(redraw) {
            clear_screen_bg(COLOR_GRAY);
            
            char title[64];
            my_sprintf(title, "SELECT %s SCRIPTS", category_names[category]);
            draw_shadow_window(10, 1, 60, 23, COLOR_BLUE, TXT_WHITE, title);
            
            kprint_at("W/S:Move  SPACE:Toggle  A:All  N:None  Enter:Preview  /:Search  ESC:Back", 2, 3, (COLOR_BLACK << 4) | TXT_CYAN);
            
            for(int i = 0; i < visible && scroll + i < cat_count; i++) {
                int idx = cat_scripts[scroll + i];
                uint8_t color = (i == selected) ? TXT_GREEN : TXT_WHITE;
                
                kprint_at(wnc_scripts[idx].enabled ? "[X]" : "[ ]", 12, 6 + i, (COLOR_BLUE << 4) | color);
                kprint_at(wnc_scripts[idx].name, 16, 6 + i, (COLOR_BLUE << 4) | color);
                
                char size_str[16];
                my_sprintf(size_str, "(%d KB)", wnc_scripts[idx].size / 1024);
                kprint_at(size_str, 35, 6 + i, (COLOR_BLUE << 4) | COLOR_DARK_GRAY);
                
                kprint_at(wnc_scripts[idx].desc, 45, 6 + i, (COLOR_BLUE << 4) | color);
            }
            
            if(scroll > 0) kprint_at("^ More ^", 35, 5, (COLOR_BLUE << 4) | TXT_CYAN);
            if(scroll + visible < cat_count) kprint_at("v More v", 35, 22, (COLOR_BLUE << 4) | TXT_CYAN);
            
            redraw = 0;
        }
        
        move_cursor(79, 24);
        
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc < 0x80) {
                if(sc == 0x48 && selected > 0) {
                    selected--;
                    if(selected < scroll) scroll = selected;
                    redraw = 1;
                }
                else if(sc == 0x50 && selected < cat_count - 1) {
                    selected++;
                    if(selected >= scroll + visible) scroll = selected - visible + 1;
                    redraw = 1;
                }
                else if(sc == 0x39) { 
                    int idx = cat_scripts[selected];
                    wnc_scripts[idx].enabled = !wnc_scripts[idx].enabled;
                    redraw = 1;
                }
                else if(sc == 0x1E) { 
                    for(int i = 0; i < cat_count; i++) {
                        wnc_scripts[cat_scripts[i]].enabled = 1;
                    }
                    redraw = 1;
                }
                else if(sc == 0x31) {
                    for(int i = 0; i < cat_count; i++) {
                        wnc_scripts[cat_scripts[i]].enabled = 0;
                    }
                    redraw = 1;
                }
                else if(sc == 0x1C) { 
                    int idx = cat_scripts[selected];
                    show_script_preview(idx);
                    redraw = 1;
                }
                else if(sc == 0x35) { 
                    search_scripts();
                    redraw = 1;
                }
                else if(sc == 0x01) { 
                    running = 0;
                }
            }
            while(inb(0x64) & 1) inb(0x60);
        }
        inst_delay(30);
    }
    
    save_selected_components();
}

static void show_script_statistics(void) {
    clear_screen_bg(COLOR_GRAY);
    draw_shadow_window(10, 3, 60, 18, COLOR_BLUE, TXT_WHITE, "SCRIPT STATISTICS");
    
    int total_installed = 0;
    int total_size = 0;
    int cat_installed[10] = {0};
    int cat_total[10] = {0};
    
    for(int i = 0; i < script_count; i++) {
        int cat = wnc_scripts[i].category;
        cat_total[cat]++;
        if(wnc_scripts[i].enabled) {
            total_installed++;
            total_size += wnc_scripts[i].size;
            cat_installed[cat]++;
        }
    }
    
    kprint_at("Total scripts available:", 14, 6, (COLOR_BLUE << 4) | TXT_CYAN);
    kprint_int_at(script_count, 40, 6, (COLOR_BLUE << 4) | TXT_YELLOW);
    
    kprint_at("Currently installed:", 14, 7, (COLOR_BLUE << 4) | TXT_CYAN);
    kprint_int_at(total_installed, 40, 7, (COLOR_BLUE << 4) | TXT_GREEN);
    
    kprint_at("Total install size:", 14, 8, (COLOR_BLUE << 4) | TXT_CYAN);
    kprint_int_at(total_size / 1024, 40, 8, (COLOR_BLUE << 4) | TXT_WHITE);
    kprint_at(" KB", 44, 8, (COLOR_BLUE << 4) | TXT_WHITE);
    
    draw_hline(12, 9, 56, COLOR_BLUE, TXT_WHITE, S_HLINE);
    
    int y = 10;
    for(int i = 0; i < 9; i++) {
        if(cat_total[i] > 0) {
            kprint_at(category_names[i], 14, y, (COLOR_BLUE << 4) | TXT_CYAN);
            kprint_at(": ", 30, y, (COLOR_BLUE << 4) | TXT_WHITE);
            kprint_int_at(cat_installed[i], 32, y, (COLOR_BLUE << 4) | TXT_GREEN);
            kprint_at("/", 35, y, (COLOR_BLUE << 4) | TXT_WHITE);
            kprint_int_at(cat_total[i], 37, y, (COLOR_BLUE << 4) | TXT_YELLOW);
            
            int percent = (cat_total[i] > 0) ? (cat_installed[i] * 100 / cat_total[i]) : 0;
            int bars = percent / 5;
            for(int b = 0; b < 20; b++) {
                if(b < bars) {
                    kprint_at("#", 40 + b, y, (COLOR_BLUE << 4) | TXT_GREEN);
                } else {
                    kprint_at(".", 40 + b, y, (COLOR_BLUE << 4) | TXT_DGRAY);
                }
            }
            kprint_int_at(percent, 62, y, (COLOR_BLUE << 4) | TXT_WHITE);
            kprint_at("%", 65, y, (COLOR_BLUE << 4) | TXT_WHITE);
            
            y++;
        }
    }
    
    kprint_at("Press any key to continue...", 22, 20, (COLOR_BLUE << 4) | TXT_CYAN);
    while(!(inb(0x64) & 1));
    while(inb(0x64) & 1) inb(0x60);
}

static void select_wnc_scripts(void) {
    int selected = 0;
    int running = 1;
    int redraw = 1;
    int num_categories = 9;
    
    while(running) {
        if(redraw) {
            clear_screen_bg(COLOR_GRAY);
            draw_shadow_window(5, 1, 70, 23, COLOR_BLUE, TXT_WHITE, "WNKC SCRIPT SELECTOR v2.0");
            
            kprint_at("W/S:Navigate  Enter:Open  SPACE:Select All  /:Search  S:Stats  ESC:Back", 2, 3, (COLOR_BLACK << 4) | TXT_CYAN);
            
            int y = 6;
            for(int i = 0; i < num_categories; i++) {
                uint8_t color = (i == selected) ? TXT_GREEN : TXT_WHITE;
                int enabled_count = 0, total_count = 0;
                for(int j = 0; j < script_count; j++) {
                    if(wnc_scripts[j].category == i) {
                        total_count++;
                        if(wnc_scripts[j].enabled) enabled_count++;
                    }
                }
                
                const char* icons[] = {"[*]", "[#]", "[@]", "[$]", "[&]", "[~]", "[+]", "[!]", "[?]"};
                kprint_at(icons[i], 7, y, (COLOR_BLUE << 4) | color);
                
                kprint_at(category_names[i], 11, y, (COLOR_BLUE << 4) | color);
                
                int percent = (total_count > 0) ? (enabled_count * 100 / total_count) : 0;
                int bars = percent / 10;
                kprint_at("[", 30, y, (COLOR_BLUE << 4) | color);
                for(int b = 0; b < 10; b++) {
                    if(b < bars) {
                        kprint_at("#", 31 + b, y, (COLOR_BLUE << 4) | TXT_GREEN);
                    } else {
                        kprint_at(".", 31 + b, y, (COLOR_BLUE << 4) | TXT_DGRAY);
                    }
                }
                kprint_at("]", 41, y, (COLOR_BLUE << 4) | color);
                
                kprint_int_at(enabled_count, 44, y, (COLOR_BLUE << 4) | TXT_GREEN);
                kprint_at("/", 47, y, (COLOR_BLUE << 4) | color);
                kprint_int_at(total_count, 49, y, (COLOR_BLUE << 4) | TXT_YELLOW);
                
                y += 2;
            }
            
            int total_enabled = 0;
            for(int i = 0; i < script_count; i++) if(wnc_scripts[i].enabled) total_enabled++;
            
            draw_hline(7, 20, 66, COLOR_BLUE, TXT_WHITE, S_HLINE);
            kprint_at("TOTAL:", 10, 21, (COLOR_BLUE << 4) | TXT_CYAN);
            kprint_int_at(total_enabled, 17, 21, (COLOR_BLUE << 4) | TXT_GREEN);
            kprint_at("/", 20, 21, (COLOR_BLUE << 4) | TXT_WHITE);
            kprint_int_at(script_count, 22, 21, (COLOR_BLUE << 4) | TXT_YELLOW);
            kprint_at("scripts selected", 25, 21, (COLOR_BLUE << 4) | TXT_WHITE);
            
            kprint_at("[Enter]Open  [Space]All  [/]Search  [S]Stats  [ESC]Back", 8, 23, (COLOR_BLACK << 4) | TXT_CYAN);
            redraw = 0;
        }
        
        move_cursor(79, 24);
        
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc < 0x80) {
                if(sc == 0x48 && selected > 0) {
                    selected--;
                    redraw = 1;
                }
                else if(sc == 0x50 && selected < num_categories - 1) {
                    selected++;
                    redraw = 1;
                }
                else if(sc == 0x1C) { 
                    select_scripts_in_category(selected);
                    redraw = 1;
                }
                else if(sc == 0x39) { 
                    for(int i = 0; i < script_count; i++) {
                        wnc_scripts[i].enabled = 1;
                    }
                    save_selected_components();
                    redraw = 1;
                }
                else if(sc == 0x35) { 
                    search_scripts();
                    redraw = 1;
                }
                else if(sc == 0x1F) {
                    show_script_statistics();
                    redraw = 1;
                }
                else if(sc == 0x31) { 
                    for(int i = 0; i < script_count; i++) {
                        wnc_scripts[i].enabled = 0;
                    }
                    save_selected_components();
                    redraw = 1;
                }
                else if(sc == 0x01) {
                    save_selected_components();
                    running = 0;
                }
            }
            while(inb(0x64) & 1) inb(0x60);
        }
        inst_delay(30);
    }
}
static void show_notification(const char* msg) {
    int len = 0;
    while(msg[len]) len++;
    
    int nx = 40 - len/2 - 2;
    int ny = 20;
    
    draw_shadow_window(nx, ny, len + 4, 3, COLOR_YELLOW, TXT_BLACK, "Info");
    kprint_at(msg, nx + 2, ny + 1, (COLOR_YELLOW << 4) | TXT_BLACK);
    
    for(volatile int i = 0; i < 20000000; i++) {
        if(inb(0x64) & 1) {
            inb(0x60);
            break;
        }
    }
}
#define FLOPPY_SIGNATURE_WNKA  0x574E4B41
#define FLOPPY_SIGNATURE_DATA  0x44415441

typedef struct {
    uint32_t magic;
    char     label[32];
    char     creator[32];
    uint32_t total_files;
    uint32_t used_space;
    uint32_t free_space;
    char     description[128];
} floppy_header_t;

typedef struct {
    char     filename[32];
    uint32_t size;
    uint32_t type;
    char     description[64];
    uint32_t offset;
} floppy_file_t;

static int check_floppy_present(void);
static void import_all_from_floppy(floppy_file_t* files, int count, floppy_header_t* header);
static void select_files_to_import(floppy_file_t* files, int count, floppy_header_t* header);
static void import_fat12_files(char filenames[][13], int* file_sizes, int file_count);
static void draw_floppy_animation(int frame, int x, int y);
static void draw_floppy_drive_animation(int x, int y);
static void draw_progress_spinner(int x, int y, int frame);


static int check_floppy_present(void) {
    kprint("\n[FLOPPY] Checking for diskette in drive A:...\n");
    fdc_init();
    
    uint8_t boot_sector[512];
    fdc_read_sector(0, 0, 0, 1, boot_sector);
    
    if(boot_sector[510] == 0x55 && boot_sector[511] == 0xAA) {
        kprint("[FLOPPY] FAT12 diskette detected!\n");
        return 1;
    }
    
    floppy_header_t* header = (floppy_header_t*)&boot_sector[0];
    if(header->magic == FLOPPY_SIGNATURE_WNKA || 
       header->magic == FLOPPY_SIGNATURE_DATA) {
        kprint("[FLOPPY] WNKA data diskette found!\n");
        return 2;
    }
    
    return 0;
}

static void import_all_from_floppy(floppy_file_t* files, int count, floppy_header_t* header) {
    kprint("\n[IMPORT] Importing all files...\n");
    int imported = 0;
    
    for(int i = 0; i < count; i++) {
        kprint("  Copying: ");
        kprint(files[i].filename);
        kprint("... ");
        
        uint8_t buffer[5120];
        int sectors = (files[i].size + 511) / 512;
        for(int s = 0; s < sectors; s++) {
            fdc_read_sector(0, 0, 0, (files[i].offset / 512) + s + 1, buffer + s * 512);
        }
        
        create_file(current_dir_sector, files[i].filename, (char*)buffer);
        kprint("OK\n");
        imported++;
    }
    
    kprint("\n[IMPORT] Imported ");
    kprint_int(imported);
    kprint(" files!\n");
}

static void select_files_to_import(floppy_file_t* files, int count, floppy_header_t* header) {
    int selected[50] = {0};
    int current = 0;
    int running = 1;
    
    while(running) {
        clear_screen_bg(COLOR_GRAY);
        draw_shadow_window(5, 2, 70, 22, COLOR_BLUE, TXT_WHITE, "SELECT FILES");
        
        for(int i = 0; i < count && i < 18; i++) {
            uint8_t color = (i == current) ? TXT_GREEN : TXT_WHITE;
            kprint_at(selected[i] ? "[X]" : "[ ]", 8, 5 + i, (COLOR_BLUE << 4) | color);
            kprint_at(files[i].filename, 13, 5 + i, (COLOR_BLUE << 4) | color);
        }
        
        kprint_at("SPACE:Toggle A:All N:None ENTER:Import ESC:Cancel", 10, 23, (COLOR_BLACK << 4) | TXT_CYAN);
        
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc < 0x80) {
                if(sc == 0x48 && current > 0) current--;
                else if(sc == 0x50 && current < count - 1) current++;
                else if(sc == 0x39) selected[current] = !selected[current];
                else if(sc == 0x1E) { for(int i = 0; i < count; i++) selected[i] = 1; }
                else if(sc == 0x31) { for(int i = 0; i < count; i++) selected[i] = 0; }
                else if(sc == 0x1C) {
                    int imp = 0;
                    for(int i = 0; i < count; i++) {
                        if(selected[i]) {
                            uint8_t buf[5120];
                            int sec = (files[i].size + 511) / 512;
                            for(int s = 0; s < sec; s++) {
                                fdc_read_sector(0, 0, 0, (files[i].offset / 512) + s + 1, buf + s * 512);
                            }
                            create_file(current_dir_sector, files[i].filename, (char*)buf);
                            imp++;
                        }
                    }
                    running = 0;
                }
                else if(sc == 0x01) running = 0;
            }
            while(inb(0x64) & 1) inb(0x60);
        }
    }
}

static void import_fat12_files(char filenames[][13], int* file_sizes, int file_count) {
    kprint("\n[IMPORT] Copying compatible files from FAT12...\n");
    int imported = 0;
    
    for(int i = 0; i < file_count; i++) {
        int len = 0;
        while(filenames[i][len]) len++;
        
        int compatible = 0;
        if(len > 4) {
            if(filenames[i][len-4] == '.' && filenames[i][len-3] == 'w' && 
               filenames[i][len-2] == 'n' && filenames[i][len-1] == 'c') compatible = 1;
            if(filenames[i][len-4] == '.' && filenames[i][len-3] == 't' && 
               filenames[i][len-2] == 'x' && filenames[i][len-1] == 't') compatible = 1;
        }
        
        if(compatible) {
            kprint("  Importing: ");
            kprint(filenames[i]);
            kprint("... ");
            
            uint8_t buffer[5120];
            floppy_read_file(filenames[i], (char*)buffer, sizeof(buffer));
            create_file(current_dir_sector, filenames[i], (char*)buffer);
            
            kprint("OK\n");
            imported++;
        }
    }
    
    kprint("\n[IMPORT] Imported ");
    kprint_int(imported);
    kprint(" files!\n");
}

static void scan_floppy_content(void) {
    kprint("\n[FLOPPY] Scanning diskette contents...\n");
    
    int floppy_type = check_floppy_present();
    
    if(floppy_type == 0) {
        kprint("[FLOPPY] No readable diskette found\n");
        return;
    }
    
    if(floppy_type == 2) {
        uint8_t sector[512];
        fdc_read_sector(0, 0, 0, 1, sector);
        floppy_header_t* header = (floppy_header_t*)sector;
        
        kprint("[FLOPPY] Label: ");
        kprint(header->label);
        kprint("\n[FLOPPY] Files: ");
        kprint_int(header->total_files);
        kprint("\n");
        
        floppy_file_t files[50];
        int files_count = 0;
        
        for(int sec = 1; sec < 10 && files_count < 50; sec++) {
            fdc_read_sector(0, 0, 0, sec + 1, sector);
            floppy_file_t* fl = (floppy_file_t*)sector;
            int n = 8;
            if(sec == 1) n = (header->total_files > 8) ? 8 : header->total_files;
            else n = (header->total_files - 8*sec + 8 > 8) ? 8 : (header->total_files - 8*sec + 8);
            for(int i = 0; i < n && files_count < header->total_files; i++) {
                files[files_count++] = fl[i];
            }
        }
        
        kprint("1. Import ALL\n2. Select files\n3. Skip\nChoice: ");
        int choice = 0;
        while(!choice) {
            if(inb(0x64) & 1) {
                uint8_t sc = inb(0x60);
                if(sc >= 0x02 && sc <= 0x04) { choice = sc - 0x01; kprint_int(choice); }
                if(sc == 0x01) { kprint("3\n"); return; }
            }
        }
        kprint("\n");
        
        if(choice == 1) import_all_from_floppy(files, files_count, header);
        else if(choice == 2) select_files_to_import(files, files_count, header);
    }
    else if(floppy_type == 1) {
        uint8_t sector[512];
        fdc_read_sector(0, 0, 1, 19, sector);
        
        int file_count = 0;
        char filenames[50][13];
        int file_sizes[50];
        
        for(int i = 0; i < 224 && file_count < 50; i++) {
            uint8_t* entry = &sector[i * 32];
            if(entry[0] == 0x00) break;
            if(entry[0] == 0xE5) continue;
            
            char name[13];
            int pos = 0;
            for(int j = 0; j < 8 && entry[j] != ' '; j++) name[pos++] = entry[j];
            if(entry[8] != ' ') {
                name[pos++] = '.';
                for(int j = 0; j < 3 && entry[8+j] != ' '; j++) name[pos++] = entry[8+j];
            }
            name[pos] = 0;
            if(name[0] == 0) continue;
            
            my_strcpy(filenames[file_count], name);
            file_sizes[file_count] = entry[28] | (entry[29] << 8) | (entry[30] << 16) | (entry[31] << 24);
            file_count++;
        }
        
        if(file_count == 0) {
            kprint("[FLOPPY] Empty diskette\n");
            return;
        }
        
        kprint("Import compatible files? (Y/N): ");
        char answer = 0;
        while(!answer) {
            if(inb(0x64) & 1) {
                uint8_t sc = inb(0x60);
                if(sc == 0x15) answer = 'Y';
                if(sc == 0x31) answer = 'N';
            }
        }
        kprint_char(answer);
        kprint("\n");
        
        if(answer == 'Y') import_fat12_files(filenames, file_sizes, file_count);
    }
}
typedef struct {
    char timestamp[32];
    char stage[32];
    char message[256];
    int success;
} install_log_entry_t;

static install_log_entry_t install_log[200];
static int install_log_count = 0;
static int swap_sector_start = 0;
static int swap_sector_count = 0;
static int files_ok = 0;
static int files_retried = 0;
static int files_failed = 0;

static void add_install_log(const char* stage, const char* message, int success) {
    if(install_log_count < 200) {
        int h = (seconds / 3600) % 24;
        int m = (seconds / 60) % 60;
        int s = seconds % 60;
        
        char* p = install_log[install_log_count].timestamp;
        if(h < 10) { *p++ = '0'; } else { *p++ = '0' + (h/10); }
        *p++ = '0' + (h%10);
        *p++ = ':';
        if(m < 10) { *p++ = '0'; } else { *p++ = '0' + (m/10); }
        *p++ = '0' + (m%10);
        *p++ = ':';
        if(s < 10) { *p++ = '0'; } else { *p++ = '0' + (s/10); }
        *p++ = '0' + (s%10);
        *p = '\0';
        
        char* d = install_log[install_log_count].stage;
        const char* src = stage;
        while(*src && (d - install_log[install_log_count].stage) < 31) *d++ = *src++;
        *d = '\0';
        
        d = install_log[install_log_count].message;
        src = message;
        while(*src && (d - install_log[install_log_count].message) < 255) *d++ = *src++;
        *d = '\0';
        
        install_log[install_log_count].success = success;
        install_log_count++;
        
        kprint_color("[", TXT_CYAN);
        kprint(install_log[install_log_count-1].timestamp);
        kprint_color("] ", TXT_CYAN);
        kprint(stage);
        kprint(": ");
        if(success) {
            kprint_color("OK - ", TXT_GREEN);
        } else {
            kprint_color("FAIL - ", TXT_RED);
        }
        kprint(message);
        kprint("\n");
    }
}

static void save_install_log(uint16_t root_sector) {
    
    char log_text[16384] = {0};
    int pos = 0;
    
    outb(0x70, 0x00); uint8_t second_bcd = inb(0x71);
    outb(0x70, 0x02); uint8_t minute_bcd = inb(0x71);
    outb(0x70, 0x04); uint8_t hour_bcd = inb(0x71);
    outb(0x70, 0x07); uint8_t day_bcd = inb(0x71);
    outb(0x70, 0x08); uint8_t month_bcd = inb(0x71);
    outb(0x70, 0x09); uint8_t year_bcd = inb(0x71);
    
    int sec = ((second_bcd >> 4) * 10) + (second_bcd & 0x0F);
    int min = ((minute_bcd >> 4) * 10) + (minute_bcd & 0x0F);
    int hr = ((hour_bcd >> 4) * 10) + (hour_bcd & 0x0F);
    int dd = ((day_bcd >> 4) * 10) + (day_bcd & 0x0F);
    int mm = ((month_bcd >> 4) * 10) + (month_bcd & 0x0F);
    int yy = ((year_bcd >> 4) * 10) + (year_bcd & 0x0F) + 2000;
    
    const char* header = "WNKA X32 INSTALLATION LOG\n==========================\n";
    const char* h = header;
    while(*h && pos < 16383) log_text[pos++] = *h++;
    
    const char* date_prefix = "Date: ";
    h = date_prefix;
    while(*h && pos < 16383) log_text[pos++] = *h++;
    
    if(dd < 10) log_text[pos++] = '0';
    char d1 = '0' + (dd / 10);
    char d2 = '0' + (dd % 10);
    if(dd >= 10) log_text[pos++] = d1;
    log_text[pos++] = d2;
    log_text[pos++] = '/';
    
    if(mm < 10) log_text[pos++] = '0';
    char m1 = '0' + (mm / 10);
    char m2 = '0' + (mm % 10);
    if(mm >= 10) log_text[pos++] = m1;
    log_text[pos++] = m2;
    log_text[pos++] = '/';
    
    char y1 = '0' + (yy / 1000);
    char y2 = '0' + ((yy / 100) % 10);
    char y3 = '0' + ((yy / 10) % 10);
    char y4 = '0' + (yy % 10);
    log_text[pos++] = y1;
    log_text[pos++] = y2;
    log_text[pos++] = y3;
    log_text[pos++] = y4;
    log_text[pos++] = '\n';
    
    const char* time_prefix = "Time: ";
    h = time_prefix;
    while(*h && pos < 16383) log_text[pos++] = *h++;
    
    if(hr < 10) log_text[pos++] = '0';
    char ch1 = '0' + (hr / 10);
    char ch2 = '0' + (hr % 10);
    if(hr >= 10) log_text[pos++] = ch1;
    log_text[pos++] = ch2;
    log_text[pos++] = ':';
    
    if(min < 10) log_text[pos++] = '0';
    char cm1 = '0' + (min / 10);
    char cm2 = '0' + (min % 10);
    if(min >= 10) log_text[pos++] = cm1;
    log_text[pos++] = cm2;
    log_text[pos++] = ':';
    
    if(sec < 10) log_text[pos++] = '0';
    char cs1 = '0' + (sec / 10);
    char cs2 = '0' + (sec % 10);
    if(sec >= 10) log_text[pos++] = cs1;
    log_text[pos++] = cs2;
    log_text[pos++] = '\n';
    log_text[pos++] = '\n';
    
    for(int i = 0; i < install_log_count && pos < 16000; i++) {
        log_text[pos++] = '[';
        const char* ts = install_log[i].timestamp;
        while(*ts && pos < 16383) log_text[pos++] = *ts++;
        log_text[pos++] = ']';
        log_text[pos++] = ' ';
        
        const char* st = install_log[i].stage;
        while(*st && pos < 16383) log_text[pos++] = *st++;
        log_text[pos++] = ':';
        log_text[pos++] = ' ';
        
        if(install_log[i].success) {
            log_text[pos++] = 'O';
            log_text[pos++] = 'K';
            log_text[pos++] = ' ';
            log_text[pos++] = '-';
            log_text[pos++] = ' ';
        } else {
            log_text[pos++] = 'F';
            log_text[pos++] = 'A';
            log_text[pos++] = 'I';
            log_text[pos++] = 'L';
            log_text[pos++] = ' ';
            log_text[pos++] = '-';
            log_text[pos++] = ' ';
        }
        
        const char* msg = install_log[i].message;
        while(*msg && pos < 16383) log_text[pos++] = *msg++;
        log_text[pos++] = '\n';
    }
    
    const char* total_prefix = "\nTotal files: ";
    h = total_prefix;
    while(*h && pos < 16383) log_text[pos++] = *h++;

    int temp = files_ok;
    if(temp == 0) {
        log_text[pos++] = '0';
    } else {
        char buf[16];
        int idx = 0;
        while(temp > 0) { buf[idx++] = '0' + (temp % 10); temp /= 10; }
        for(int j = idx-1; j >= 0; j--) log_text[pos++] = buf[j];
    }
    
    const char* ok_prefix = " OK, ";
    h = ok_prefix;
    while(*h && pos < 16383) log_text[pos++] = *h++;
    
    temp = files_failed;
    if(temp == 0) {
        log_text[pos++] = '0';
    } else {
        char buf[16];
        int idx = 0;
        while(temp > 0) { buf[idx++] = '0' + (temp % 10); temp /= 10; }
        for(int j = idx-1; j >= 0; j--) log_text[pos++] = buf[j];
    }
    
    const char* failed_prefix = " failed\n";
    h = failed_prefix;
    while(*h && pos < 16383) log_text[pos++] = *h++;
    
    if(swap_sector_count > 0) {
        const char* swap_prefix = "Swap created: ";
        h = swap_prefix;
        while(*h && pos < 16383) log_text[pos++] = *h++;
        
        temp = swap_sector_count / 2048;
        if(temp == 0) {
            log_text[pos++] = '0';
        } else {
            char buf[16];
            int idx = 0;
            while(temp > 0) { buf[idx++] = '0' + (temp % 10); temp /= 10; }
            for(int j = idx-1; j >= 0; j--) log_text[pos++] = buf[j];
        }
        
        const char* mb_prefix = " MB at sector ";
        h = mb_prefix;
        while(*h && pos < 16383) log_text[pos++] = *h++;
        
        temp = swap_sector_start;
        if(temp == 0) {
            log_text[pos++] = '0';
        } else {
            char buf[16];
            int idx = 0;
            while(temp > 0) { buf[idx++] = '0' + (temp % 10); temp /= 10; }
            for(int j = idx-1; j >= 0; j--) log_text[pos++] = buf[j];
        }
        log_text[pos++] = '\n';
    }
    
    log_text[pos] = '\0';
    create_file(root_sector, "install.log", log_text);
}

static int calculate_recommended_swap(int ram_mb) {
    if(ram_mb < 64) return ram_mb * 2;
    if(ram_mb < 256) return ram_mb;
    return 256;
}

static int user_choose_swap_size(int recommended_mb, int max_mb) {
    clear_screen_bg(COLOR_GRAY);
    draw_shadow_window(15, 5, 50, 12, COLOR_BLUE, TXT_WHITE, "SWAP PARTITION");
    
    kprint_at("Your system has ", 17, 7, (COLOR_BLACK << 4) | TXT_WHITE);
    kprint_int_at(ram_mb_detected, 33, 7, (COLOR_BLACK << 4) | TXT_GREEN);
    kprint_at(" MB RAM", 36, 7, (COLOR_BLACK << 4) | TXT_WHITE);
    
    kprint_at("Recommended swap size: ", 17, 9, (COLOR_BLACK << 4) | TXT_CYAN);
    kprint_int_at(recommended_mb, 40, 9, (COLOR_BLACK << 4) | TXT_GREEN);
    kprint_at(" MB", 44, 9, (COLOR_BLACK << 4) | TXT_WHITE);
    
    kprint_at("Maximum available: ", 17, 10, (COLOR_BLACK << 4) | TXT_CYAN);
    kprint_int_at(max_mb, 36, 10, (COLOR_BLACK << 4) | TXT_YELLOW);
    kprint_at(" MB", 40, 10, (COLOR_BLACK << 4) | TXT_WHITE);
    
    kprint_at("\nSelect swap size (MB):", 17, 12, (COLOR_BLACK << 4) | TXT_YELLOW);
    kprint_at("  [1] Auto (recommended)", 17, 14, (COLOR_BLACK << 4) | TXT_GREEN);
    kprint_at("  [2] Custom size", 17, 15, (COLOR_BLACK << 4) | TXT_CYAN);
    kprint_at("  [3] No swap", 17, 16, (COLOR_BLACK << 4) | TXT_RED);
    kprint_at("  [4] Full disk as swap", 17, 17, (COLOR_BLACK << 4) | TXT_YELLOW);
    
    int choice = 0;
    while(choice == 0) {
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc == 0x02) choice = 1;
            else if(sc == 0x03) choice = 2;
            else if(sc == 0x04) choice = 3;
            else if(sc == 0x05) choice = 4;
            else if(sc == 0x01) choice = 3;
        }
    }
    
    if(choice == 1) return recommended_mb;
    if(choice == 3) return 0;
    if(choice == 4) return max_mb;
    
    kprint_at("\nEnter size in MB (1-", 17, 19, (COLOR_BLACK << 4) | TXT_CYAN);
    kprint_int_at(max_mb, 32, 19, (COLOR_BLACK << 4) | TXT_WHITE);
    kprint_at("): ", 35, 19, (COLOR_BLACK << 4) | TXT_WHITE);
    
    int custom_size = 0;
    int got = 0;
    while(!got) {
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc >= 0x02 && sc <= 0x0B) {
                int digit = sc - 0x02;
                if(digit == 10) digit = 0;
                custom_size = custom_size * 10 + digit;
                kprint_char('0' + digit);
            }
            else if(sc == 0x0E) {
                custom_size = 0;
                kprint("\b \b");
            }
            else if(sc == 0x1C) {
                got = 1;
            }
        }
    }
    
    if(custom_size < 1) custom_size = recommended_mb;
    if(custom_size > max_mb) custom_size = max_mb;
    
    return custom_size;
}

static void create_swap_partition(int size_mb) {
    if(size_mb <= 0) {
        add_install_log("SWAP", "Skipped (user choice)", 1);
        return;
    }
    
    add_install_log("SWAP", "Creating swap partition...", 1);
    
    uint32_t sectors_needed = size_mb * 2048;
    
    uint32_t total_used_sectors = 0;
    
    for(int i = 0; i < 32; i++) {
        uint16_t dir_buf[256];
        read_sector(100 + i, dir_buf);
    }
    
    swap_sector_start = 50000;
    swap_sector_count = sectors_needed;
    
    kprint_color("\n[SWAP] Creating ", TXT_CYAN);
    kprint_int(size_mb);
    kprint_color(" MB swap partition...\n", TXT_CYAN);
    
    uint16_t swap_buf[256];
    for(int i = 0; i < 256; i++) swap_buf[i] = 0x00;
    
    for(uint32_t i = 0; i < sectors_needed; i++) {
        write_sector(swap_sector_start + i, swap_buf);
        
        if(i % 1000 == 0 && i > 0) {
            int percent = (i * 100) / sectors_needed;
            draw_progress(34, 19, 30, percent, COLOR_GRAY, COLOR_CYAN);
        }
        
        if(i % 10000 == 0 && i > 0) {
            if(inb(0x64) & 1) {
                uint8_t sc = inb(0x60);
                if(sc == 0x01) {
                    add_install_log("SWAP", "Cancelled by user", 0);
                    return;
                }
            }
        }
    }

    uint16_t swap_info[256] = {0};
    swap_info[0] = swap_sector_start;
    swap_info[1] = sectors_needed;
    swap_info[2] = size_mb;
    write_sector(9998, swap_info);
    
    char msg[64];
    sprintf(msg, "Created %d MB swap at sector %d", size_mb, swap_sector_start);
    add_install_log("SWAP", msg, 1);
    
    kprint_color("\n[SWAP] Created successfully!\n", TXT_GREEN);
}

static void my_memset(void* ptr, int value, int num) {
    unsigned char* p = (unsigned char*)ptr;
    for(int i = 0; i < num; i++) {
        p[i] = (unsigned char)value;
    }
}

static char get_key(void) {
    while(1) {
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc < 0x80) {
                if(sc == 0x15) return 'Y';
                if(sc == 0x31) return 'N';
                if(sc == 0x01) return 27;
                if(sc >= 0x02 && sc <= 0x0B) {
                    const char* digits = "1234567890";
                    return digits[sc - 0x02];
                }
            }
        }
    }
}

static void reboot_countdown(int seconds) {
    for(int i = seconds; i > 0; i--) {
        kprint("\rRebooting in ");
        kprint_int(i);
        kprint(" seconds...  ");
        for(volatile int d = 0; d < 1000000; d++);
    }
    kprint("\n");
    outb(0x64, 0xFE);
}

static int wait_esc(void) {
    for(int i = 0; i < 100; i++) {
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc == 0x01) return 1;
            if(sc == 0x1C) return 0;
        }
        for(volatile int d = 0; d < 10000; d++);
    }
    return 0;
}

static int copy_file_from_cdrom(uint32_t lba, uint32_t size, uint32_t dest_sector) {
    kprint("  Copying LBA ");
    kprint_int(lba);
    kprint(" -> sector ");
    kprint_int(dest_sector);
    kprint("\n");
    
    uint8_t buffer[2048];
    uint32_t sectors_needed = (size + 2047) / 2048;
    
    for(uint32_t i = 0; i < sectors_needed; i++) {
        if(atapi_read_sector(lba + i, buffer) <= 0) {
            kprint_color("  Read error!\n", TXT_RED);
            return -1;
        }
        
        uint16_t sector_buf[256];
        for(int j = 0; j < 1024; j++) {
            sector_buf[j] = buffer[j*2] | (buffer[j*2+1] << 8);
        }
        write_sector(dest_sector + i, sector_buf);
        
        if(i % 10 == 0) kprint(".");
    }
    
    kprint(" OK\n");
    return sectors_needed;
}

static int check_cdrom_present(void) {
    kprint("[CDROM] Checking for CD-ROM...\n");
    
    uint8_t sector[2048];
    if(atapi_read_sector(16, sector) > 0) {
        kprint_color("[CDROM] CD-ROM detected!\n", TXT_GREEN);
        return 1;
    }
    
    kprint_color("[CDROM] No CD-ROM found\n", TXT_RED);
    return 0;
}

#define CD_FS_ISO9660    1 
#define CD_FS_JOLIET     2 
#define CD_FS_ROCKRIDGE  3 
#define CD_FS_ELTORITO   4  
#define CD_FS_UDF        5  
#define CD_FS_HFS        6 
#define CD_FS_HFS_PLUS   7 
#define CD_FS_HYBRID     8  
#define CD_FS_RAW        9 

typedef struct {
    uint8_t type;
    char name[64];
    uint32_t root_lba;
    uint32_t root_size;
    uint32_t total_sectors;
    int bootable;
    char volume_id[32];
    char system_id[32];
    char publisher[128];
    char application[128];
} cd_fs_info_t;

static cd_fs_info_t cd_info;

static int detect_iso9660(void) {
    uint8_t sector[2048];
    if(atapi_read_sector(16, sector) <= 0) return 0;
    
    for(int offset = 0; offset < 2048 - 100; offset++) {
        if(sector[offset] == 1 && 
           sector[offset+1] == 'C' &&
           sector[offset+2] == 'D' &&
           sector[offset+3] == '0' &&
           sector[offset+4] == '0' &&
           sector[offset+5] == '1') {
            
            cd_info.type = CD_FS_ISO9660;
            cd_info.root_lba = *(uint32_t*)(sector + offset + 158); 
            cd_info.root_size = *(uint32_t*)(sector + offset + 166); 
            cd_info.total_sectors = *(uint32_t*)(sector + offset + 80); 
            
            for(int i = 0; i < 32; i++) {
                cd_info.volume_id[i] = sector[offset + 40 + i];
            }
            cd_info.volume_id[31] = 0;
            
            for(int i = 0; i < 32; i++) {
                cd_info.system_id[i] = sector[offset + 8 + i];
            }
            cd_info.system_id[31] = 0;
            
            str_cpy(cd_info.name, "ISO 9660");
            return 1;
        }
    }
    return 0;
}

static int detect_eltorito(void) {
    uint8_t sector[2048];
    if(atapi_read_sector(17, sector) <= 0) return 0;
    
    for(int offset = 0; offset < 2048 - 32; offset++) {
        if(sector[offset] == 0 &&
           sector[offset+1] == 'C' &&
           sector[offset+2] == 'D' &&
           sector[offset+3] == '0' &&
           sector[offset+4] == '0' &&
           sector[offset+5] == '1') {
            
            cd_info.bootable = 1;
            str_cpy(cd_info.name, "El Torito Bootable");
            return 1;
        }
    }
    return 0;
}

static int detect_joliet(void) {
    for(int lba = 17; lba < 25; lba++) {
        uint8_t sector[2048];
        if(atapi_read_sector(lba, sector) <= 0) continue;
        
        for(int offset = 0; offset < 2048 - 100; offset++) {
            if(sector[offset] == 2 &&
               sector[offset+1] == 'C' &&
               sector[offset+2] == 'D' &&
               sector[offset+3] == '0' &&
               sector[offset+4] == '0' &&
               sector[offset+5] == '1') {
                
                uint8_t esc1 = sector[offset + 88];
                uint8_t esc2 = sector[offset + 89];
                uint8_t esc3 = sector[offset + 90];
                
                if(esc1 == 0x25 && esc2 == 0x2F) {
                    cd_info.type = CD_FS_JOLIET;
                    str_cpy(cd_info.name, "Joliet (Microsoft)");
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int detect_rockridge(void) {
    uint8_t sector[2048];
    if(atapi_read_sector(16, sector) <= 0) return 0;
    
    for(int offset = 0; offset < 2048 - 4; offset++) {
        if(sector[offset] == 'R' && sector[offset+1] == 'R' &&
           sector[offset+2] == 0x49 && sector[offset+3] == 0x50) {
            cd_info.type = CD_FS_ROCKRIDGE;
            str_cpy(cd_info.name, "Rock Ridge (Unix)");
            return 1;
        }
    }
    return 0;
}

static int detect_udf(void) {
    uint8_t sector[2048];
    if(atapi_read_sector(256, sector) <= 0) return 0;

    uint8_t tag_id = sector[0];
    uint8_t tag_version = sector[1];
    
    if(tag_id == 2 && tag_version == 0x01) {
        cd_info.type = CD_FS_UDF;
        str_cpy(cd_info.name, "UDF (DVD/Blu-ray)");
        return 1;
    }
    
    if(atapi_read_sector(512, sector) > 0) {
        tag_id = sector[0];
        tag_version = sector[1];
        if(tag_id == 2 && tag_version == 0x01) {
            cd_info.type = CD_FS_UDF;
            str_cpy(cd_info.name, "UDF (DVD/Blu-ray)");
            return 1;
        }
    }
    
    return 0;
}

static int detect_hfs(void) {
    uint8_t sector[2048];
    
    if(atapi_read_sector(2, sector) <= 0) return 0;
    
    if(sector[0] == 0x42 && sector[1] == 0x44) { 
        cd_info.type = CD_FS_HFS;
        str_cpy(cd_info.name, "Apple HFS");
        return 1;
    }
    
    if(sector[0] == 'H' && sector[1] == '+') {
        cd_info.type = CD_FS_HFS_PLUS;
        str_cpy(cd_info.name, "Apple HFS+");
        return 1;
    }
    
    return 0;
}

static int detect_fat_on_cd(void) {
    uint8_t sector[2048];
    if(atapi_read_sector(0, sector) <= 0) return 0;
    
    if(sector[510] == 0x55 && sector[511] == 0xAA) {
        if(sector[0] == 0xEB || sector[0] == 0xE9) {
            cd_info.type = CD_FS_RAW;
            str_cpy(cd_info.name, "FAT12/16 (legacy)");
            return 1;
        }
    }
    return 0;
}


static int scan_cdrom_fs(void) {
    kprint_color("     SCANNING CD-ROM FILESYSTEMS       \n", TXT_CYAN);
    
    my_memset(&cd_info, 0, sizeof(cd_info));
    cd_info.bootable = 0;
    
    uint8_t test[2048];
    int readable = atapi_read_sector(0, test);
    
    if(readable <= 0) {
        kprint_color("[SCAN] Cannot read CD-ROM\n", TXT_RED);
        return 0;
    }
    
    kprint("[SCAN] CD-ROM detected, testing filesystems...\n\n");
    
    int found = 0;
    
    if(detect_udf()) {
        kprint_color("[SCAN] UDF filesystem found\n", TXT_GREEN);
        found |= CD_FS_UDF;
    }
    
    if(detect_hfs()) {
        kprint_color("[SCAN] HFS filesystem found\n", TXT_GREEN);
        if(cd_info.type == CD_FS_HFS_PLUS) {
            kprint_color("[SCAN]   Type: HFS+\n", TXT_GREEN);
        }
        found |= CD_FS_HFS;
    }
    
    if(detect_iso9660()) {
        kprint_color("[SCAN] ISO 9660 filesystem found\n", TXT_GREEN);
        found |= CD_FS_ISO9660;
        
        if(detect_rockridge()) {
            kprint_color("[SCAN]   + Rock Ridge extensions\n", TXT_GREEN);
            found |= CD_FS_ROCKRIDGE;
        }
        
        if(detect_joliet()) {
            kprint_color("[SCAN]   + Joliet extensions\n", TXT_GREEN);
            found |= CD_FS_JOLIET;
        }
        
        if(detect_eltorito()) {
            kprint_color("[SCAN]   + El Torito bootable\n", TXT_GREEN);
            found |= CD_FS_ELTORITO;
        }
    }
    
    if(!found && detect_fat_on_cd()) {
        kprint_color("[SCAN] FAT12/16 filesystem found\n", TXT_YELLOW);
        found |= CD_FS_RAW;
    }
    
    if(found & CD_FS_ISO9660 && found & CD_FS_UDF && found & CD_FS_HFS) {
        cd_info.type = CD_FS_HYBRID;
        str_cpy(cd_info.name, "Hybrid (ISO+UDF+HFS)");
        kprint_color("[SCAN] ★ HYBRID DISK DETECTED (ISO+UDF+HFS)\n", TXT_YELLOW);
    } else if(found & CD_FS_ISO9660 && found & CD_FS_UDF) {
        cd_info.type = CD_FS_HYBRID;
        str_cpy(cd_info.name, "Hybrid (ISO+UDF)");
        kprint_color("[SCAN] ★ Hybrid disk (ISO+UDF)\n", TXT_YELLOW);
    }
    
    kprint("\n[SCAN] Filesystem: ");
    kprint(cd_info.name);
    kprint("\n");
    
    if(cd_info.volume_id[0]) {
        kprint("[SCAN] Volume: ");
        kprint(cd_info.volume_id);
        kprint("\n");
    }
    
    if(cd_info.system_id[0]) {
        kprint("[SCAN] System: ");
        kprint(cd_info.system_id);
        kprint("\n");
    }
    
    if(cd_info.root_lba > 0) {
        kprint("[SCAN] Root directory at LBA: ");
        kprint_int(cd_info.root_lba);
        kprint("\n");
    }
    
    if(cd_info.total_sectors > 0) {
        kprint("[SCAN] Size: ");
        kprint_int(cd_info.total_sectors / 2); 
        kprint(" KB\n");
    }
    
    if(cd_info.bootable) {
        kprint_color("[SCAN] Boot record: YES\n", TXT_GREEN);
    }
    
    kprint("----------------------------------------\n");
    
    return found;
}

static void read_iso_directory(uint32_t dir_lba, uint32_t dir_size, 
                                void (*callback)(const char* name, uint32_t lba, 
                                                uint32_t size, uint8_t flags, int depth)) {
    uint32_t dir_sectors = (dir_size + 2047) / 2048;
    uint8_t* dir_buffer = (uint8_t*)0x200000;  
    
    for(uint32_t i = 0; i < dir_sectors && i < 1024; i++) {
        if(atapi_read_sector(dir_lba + i, dir_buffer + i * 2048) <= 0) {
            break;
        }
    }
    
    uint32_t offset = 0;
    while(offset < dir_size) {
        uint8_t rec_len = dir_buffer[offset];
        if(rec_len == 0) break;
        
        uint8_t name_len = dir_buffer[offset + 32];
        uint8_t flags = dir_buffer[offset + 25];
        
        if(name_len > 0) {
            char filename[256];
            int fn_len = 0;
            
            for(int i = 0; i < name_len && fn_len < 250; i++) {
                char c = dir_buffer[offset + 33 + i];
                if(c == ';') break; 
                if(c >= 32 && c <= 126) {
                    filename[fn_len++] = c;
                }
            }
            filename[fn_len] = '\0';
            
            if(fn_len == 0 || (fn_len == 1 && filename[0] == 0) ||
               (fn_len == 1 && filename[0] == 1)) {
                offset += rec_len;
                continue;
            }
            
            uint32_t file_lba = *(uint32_t*)(dir_buffer + offset + 2);
            uint32_t file_size = *(uint32_t*)(dir_buffer + offset + 10);
            
            callback(filename, file_lba, file_size, flags, 0);
        }
        
        offset += rec_len;
    }
}

static int copy_cd_directory(uint32_t dir_lba, uint32_t dir_size, 
                              uint16_t dest_parent_sector, int depth) {
    if(depth > 10) return 0;  
    
    uint32_t dir_sectors = (dir_size + 2047) / 2048;
    uint8_t* dir_buffer = (uint8_t*)0x200000;
    
    int copied = 0;
    
    for(uint32_t i = 0; i < dir_sectors && i < 1024; i++) {
        if(atapi_read_sector(dir_lba + i, dir_buffer + i * 2048) <= 0) {
            break;
        }
    }
    
    uint32_t offset = 0;
    while(offset < dir_size) {
        uint8_t rec_len = dir_buffer[offset];
        if(rec_len == 0) break;
        
        uint8_t name_len = dir_buffer[offset + 32];
        uint8_t flags = dir_buffer[offset + 25];
        
        if(name_len > 0) {
            char filename[256];
            int fn_len = 0;
            
            for(int i = 0; i < name_len && fn_len < 250; i++) {
                char c = dir_buffer[offset + 33 + i];
                if(c == ';') break;
                if(c >= 32 && c <= 126) {
                    filename[fn_len++] = c;
                }
            }
            filename[fn_len] = '\0';
            
            if(fn_len == 0 || (fn_len == 1 && filename[0] <= 1)) {
                offset += rec_len;
                continue;
            }
            
            uint32_t file_lba = *(uint32_t*)(dir_buffer + offset + 2);
            uint32_t file_size = *(uint32_t*)(dir_buffer + offset + 10);
            
            char short_name[13] = {0};
            int nc = 0;
            for(int j = 0; j < fn_len && nc < 11; j++) {
                if(filename[j] >= 'a' && filename[j] <= 'z') {
                    short_name[nc++] = filename[j] - 32;
                } else if(filename[j] >= 'A' && filename[j] <= 'Z') {
                    short_name[nc++] = filename[j];
                } else if(filename[j] >= '0' && filename[j] <= '9') {
                    short_name[nc++] = filename[j];
                } else if(filename[j] == '.' && nc > 0) {
                    int ext_start = nc;
                    j++;
                    for(int e = 0; e < 3 && j < fn_len && nc < 11; e++, j++) {
                        char c = filename[j];
                        if(c >= 'a' && c <= 'z') short_name[nc++] = c - 32;
                        else if(c >= 'A' && c <= 'Z') short_name[nc++] = c;
                        else if(c >= '0' && c <= '9') short_name[nc++] = c;
                        else break;
                    }
                    break;
                } else if(filename[j] == '_' || filename[j] == '-') {
                    short_name[nc++] = filename[j];
                }
            }
            short_name[nc] = '\0';
            
            if(nc == 0) {
                static int gen_count = 0;
                my_sprintf(short_name, "CDROM_%04d", gen_count++);
            }
            
            if(flags & 0x02) {
                uint16_t new_dir_sector;
                create_dir(dest_parent_sector, short_name, &new_dir_sector);
                
                if(file_size > 0) {
                    kprint("  [DIR] ");
                    kprint(short_name);
                    kprint(" -> RECURSIVE COPY\n");
                    
                    copy_cd_directory(file_lba, file_size, new_dir_sector, depth + 1);
                }
            } else { 
                if(file_size > 0) {
                    kprint("  [FILE] ");
                    kprint(short_name);
                    kprint(" (");
                    kprint_int(file_size);
                    kprint(" bytes)...");
                    
                    int result = copy_file_from_cdrom(file_lba, file_size, 
                                                      dest_parent_sector + copied);
                    if(result > 0) {
                        copied += result;
                        kprint_color(" OK\n", TXT_GREEN);
                    } else {
                        kprint_color(" FAIL\n", TXT_RED);
                    }
                }
            }
        }
        
        offset += rec_len;
    }
    
    return copied;
}


static void quick_install_from_cd(void) {
    kprint_color("    CD-ROM AUTOMATIC INSTALLATION       \n", TXT_CYAN);
    int fs_type = scan_cdrom_fs();
    
    if(!fs_type) {
        kprint_color("[ERROR] No supported filesystem found on CD\n", TXT_RED);
        kprint("Use standard installation instead\n");
        return;
    }
    
    kprint("\n[INSTALL] Found filesystem: ");
    kprint(cd_info.name);
    kprint("\n");
    
    if(cd_info.volume_id[0]) {
        kprint("[INSTALL] Volume: ");
        kprint(cd_info.volume_id);
        kprint("\n");
    }
    
    kprint("\n[INSTALL] This will copy all files from CD to HDD\n");
    kprint("[INSTALL] Press 'Y' to continue, any key to cancel...\n");
    
    if(get_key() != 'Y') return;
    
    kprint_color("\n[INSTALL] Starting file copy...\n", TXT_GREEN);
    uint16_t root_sector = 100;
    int total = copy_cd_directory(cd_info.root_lba, cd_info.root_size, 
                                   root_sector, 0);
    
char install_info[256];
my_sprintf(install_info, 
    "WNKA OS Installed from CD-ROM\n"
    "Source: %s\n"
    "Volume: %s\n"
    "Filesystem: %s\n",
    cd_info.name,
    cd_info.volume_id[0] ? cd_info.volume_id : "Unknown",
    cd_info.name);
    create_file(root_sector, ".cdinstall", install_info);
    
    kprint_color("\n[INSTALL] Installation complete!\n", TXT_GREEN);
    kprint_int(total);
    kprint(" sectors copied\n");
    kprint("[INSTALL] Press any key to reboot...\n");
    
    while(!(inb(0x64) & 1));
    while(inb(0x64) & 1) inb(0x60);
    
    reboot_countdown(5);
}


static void stage3_install(void) {
    uint16_t root_sector = 100, new_sector;
    int local_files_ok = 0;
    int local_files_retried = 0;
    int local_files_failed = 0;
    int local_total_files = 0;
    
    files_ok = 0;
    files_retried = 0;
    files_failed = 0;
    swap_sector_start = 0;
    swap_sector_count = 0;
    install_log_count = 0;
    
    add_install_log("STAGE3", "Starting installation stage 3", 1);
    
    int disk_ok = 1;
    uint16_t test_buf[256], verify_buf[256];
    
    add_install_log("DISK", "Testing disk integrity...", 1);
    
    for(int pass = 1; pass <= 3 && disk_ok; pass++) {
        for(int i = 0; i < 256; i++) test_buf[i] = (pass << 8) | (i & 0xFF);
        write_sector(10000 + pass, test_buf);
        read_sector(10000 + pass, verify_buf);
        for(int i = 0; i < 256 && disk_ok; i++) {
            if(test_buf[i] != verify_buf[i]) disk_ok = 0;
        }
    }
    
    if(disk_ok) {
        add_install_log("DISK", "Disk integrity check passed", 1);
    } else {
        add_install_log("DISK", "Disk integrity check failed", 0);
    }
    
    if(use_wnkfs) {
        add_install_log("FS", "Formatting with WnkFS...", 1);
        wnkfs_format();
        if(wnkfs_mount() != 0) {
            use_wnkfs = 0;
            add_install_log("FS", "WnkFS mount failed, using standard FS", 0);
        } else {
            add_install_log("FS", "WnkFS mounted successfully", 1);
        }
    }
    
    load_selected_components();
    select_wnc_scripts();
    
    uint16_t settings_buf[256];
    read_sector(106, settings_buf);
    int cpu_selected = settings_buf[8];
    if(cpu_selected < 0 || cpu_selected >= cpu_count) cpu_selected = 0;
    
    uint32_t total_disk_sectors = 0;
    uint16_t identify_buf[256];
    outb(ata_base_port + 6, 0xA0);
    outb(ata_base_port + 7, 0xEC);
    for(volatile int i = 0; i < 100000; i++);
    uint8_t status = inb(ata_base_port + 7);
    if(status != 0 && status != 0xFF) {
        for(int i = 0; i < 256; i++) identify_buf[i] = inw(ata_base_port);
        total_disk_sectors = identify_buf[60] | (identify_buf[61] << 16);
    }
    
    uint32_t used_sectors_estimate = 20000;
    uint32_t free_sectors = total_disk_sectors - used_sectors_estimate;
    int max_swap_mb = free_sectors / 2048;
    if(max_swap_mb > 512) max_swap_mb = 512;
    
    int recommended_swap = calculate_recommended_swap(ram_mb_detected);
    if(recommended_swap > max_swap_mb) recommended_swap = max_swap_mb;
    
    if(max_swap_mb > 10 && ram_mb_detected < 256) {
        add_install_log("SWAP", "Checking swap requirements...", 1);
        int swap_size = user_choose_swap_size(recommended_swap, max_swap_mb);
        create_swap_partition(swap_size);
    } else {
        add_install_log("SWAP", "Swap skipped (insufficient space or enough RAM)", 1);
    }
    
    clear_screen_bg(COLOR_GRAY);
    update_time_display();
    
    draw_dframe(0, 0, 80, 3, COLOR_BLUE, TXT_WHITE);
    kprint_at(use_wnkfs ? "WNKA OS + WnkFS - Stage 3" : "WNKA OS - Stage 3", 
             22, 1, (COLOR_BLUE << 4) | TXT_YELLOW);
    
    draw_frame(0, 3, 25, 22, COLOR_GRAY, TXT_WHITE);
    kprint_at("System Info", 8, 4, (COLOR_GRAY << 4) | TXT_CYAN);
    kprint_at("CPU:", 2, 6, (COLOR_BLACK << 4) | TXT_WHITE);
    kprint_at(cpu_list[cpu_selected], 7, 6, (COLOR_BLACK << 4) | TXT_GREEN);
    kprint_at("RAM:", 2, 8, (COLOR_BLACK << 4) | TXT_WHITE);
    kprint_int_at(ram_mb_detected, 7, 8, (COLOR_BLACK << 4) | TXT_GREEN);
    kprint_at("MB", 11, 8, (COLOR_BLACK << 4) | TXT_WHITE);
    
    if(swap_sector_count > 0) {
        kprint_at("Swap:", 2, 9, (COLOR_BLACK << 4) | TXT_WHITE);
        kprint_int_at(swap_sector_count / 2048, 8, 9, (COLOR_BLACK << 4) | TXT_GREEN);
        kprint_at("MB", 12, 9, (COLOR_BLACK << 4) | TXT_WHITE);
    }
    
    kprint_at("FS:", 2, 11, (COLOR_BLACK << 4) | TXT_WHITE);
    kprint_at(use_wnkfs ? "WnkFS" : "Standard", 6, 11, 
             (COLOR_BLACK << 4) | (use_wnkfs ? TXT_GREEN : TXT_YELLOW));
    kprint_at("Disk:", 2, 13, (COLOR_BLACK << 4) | TXT_WHITE);
    kprint_at(disk_ok ? "OK" : "WARN", 8, 13, 
             (COLOR_BLACK << 4) | (disk_ok ? TXT_GREEN : TXT_YELLOW));
    
    kprint_at("Files OK:", 2, 16, (COLOR_BLACK << 4) | TXT_CYAN);
    kprint_at("Retries:", 2, 18, (COLOR_BLACK << 4) | TXT_CYAN);
    kprint_at("Failed:", 2, 20, (COLOR_BLACK << 4) | TXT_CYAN);
    
    draw_frame(25, 3, 54, 22, COLOR_GRAY, TXT_WHITE);
    kprint_at("Installation Progress", 38, 4, (COLOR_GRAY << 4) | TXT_CYAN);
    
    kprint_at("Total:", 27, 7, (COLOR_GRAY << 4) | TXT_WHITE);
    draw_progress(34, 7, 30, 0, COLOR_GRAY, COLOR_GREEN);
    
    kprint_at("File:", 27, 10, (COLOR_GRAY << 4) | TXT_WHITE);
    draw_progress(34, 10, 30, 0, COLOR_GRAY, COLOR_CYAN);
    
    kprint_at("Current:", 27, 13, (COLOR_GRAY << 4) | TXT_YELLOW);
    kprint_at("Status:", 27, 16, (COLOR_GRAY << 4) | TXT_WHITE);
    kprint_at("Retry:", 27, 19, (COLOR_GRAY << 4) | TXT_WHITE);
    
    auto update_file_progress = [](int file_percent, const char* filename, const char* status, int retry_count) {
        draw_progress(34, 10, 30, file_percent, COLOR_GRAY, COLOR_CYAN);
        kprint_int_at(file_percent, 66, 10, (COLOR_GRAY << 4) | TXT_WHITE);
        kprint_at("%", 69, 10, (COLOR_GRAY << 4) | TXT_WHITE);
        
        kprint_at("                    ", 36, 13, (COLOR_GRAY << 4) | TXT_BLACK);
        kprint_at(filename, 36, 13, (COLOR_GRAY << 4) | TXT_GREEN);
        
        kprint_at("                    ", 36, 16, (COLOR_GRAY << 4) | TXT_BLACK);
        kprint_at(status, 36, 16, (COLOR_GRAY << 4) | (retry_count > 0 ? TXT_YELLOW : TXT_GREEN));
        
        if(retry_count > 0) {
            draw_progress(34, 19, 30, retry_count * 20, COLOR_GRAY, COLOR_YELLOW);
            kprint_at("Attempt ", 36, 21, (COLOR_GRAY << 4) | TXT_YELLOW);
            kprint_int_at(retry_count, 44, 21, (COLOR_GRAY << 4) | TXT_YELLOW);
            kprint_at("/5", 46, 21, (COLOR_GRAY << 4) | TXT_WHITE);
        } else {
            draw_progress(34, 19, 30, 0, COLOR_GRAY, COLOR_GRAY);
            kprint_at("                    ", 36, 21, (COLOR_GRAY << 4) | TXT_BLACK);
        }
    };
    
    auto update_total_progress = [](int total_percent, int ok, int retried, int failed) {
        draw_progress(34, 7, 30, total_percent, COLOR_GRAY, COLOR_GREEN);
        kprint_int_at(total_percent, 66, 7, (COLOR_GRAY << 4) | TXT_WHITE);
        kprint_at("%", 69, 7, (COLOR_GRAY << 4) | TXT_WHITE);
        
        kprint_int_at(ok, 12, 15, (COLOR_BLACK << 4) | TXT_GREEN);
        kprint_at("   ", 14, 15, (COLOR_BLACK << 4) | TXT_BLACK);
        
        kprint_int_at(retried, 12, 17, (COLOR_BLACK << 4) | TXT_YELLOW);
        kprint_at("   ", 14, 17, (COLOR_BLACK << 4) | TXT_BLACK);
        
        kprint_int_at(failed, 12, 19, (COLOR_BLACK << 4) | TXT_RED);
        kprint_at("   ", 14, 19, (COLOR_BLACK << 4) | TXT_BLACK);
    };
    
    auto verify_file = [](uint16_t parent_sector, const char* name, const char* content) -> int {
        uint16_t dir_buf[256];
        read_sector(parent_sector, dir_buf);
        
        int slot = -1;
        for(int i = 0; i < 32; i++) {
            char fname[12] = {0};
            for(int j = 0; j < 11; j++) fname[j] = ((char*)dir_buf)[i*16 + j];
            if(my_strcmp(name, fname) == 0) { slot = i; break; }
        }
        if(slot == -1) return 1;
        
        int expected_size = 0;
        while(content[expected_size]) expected_size++;
        int file_size = dir_buf[slot*8 + 7];
        if(file_size != expected_size) return 0;
        return 1;
    };
    const char* dirs[] = {"bin", "boot", "dev", "etc", "home", "mnt", "proc", "tmp", "usr", "var", "WnkaSXS", "opt"};
    uint16_t dir_sectors[12];
    
    for(int i = 0; i < 12; i++) {
        update_file_progress((i + 1) * 100 / 12, dirs[i], "Creating...", 0);
        create_dir(root_sector, dirs[i], &new_sector);
        dir_sectors[i] = new_sector;
        int percent = ((i + 1) * 5);
        update_total_progress(percent, local_files_ok, local_files_retried, local_files_failed);
        inst_delay(10);
    }
    
    uint16_t bin_sector = dir_sectors[0];
    uint16_t etc_sector = dir_sectors[3];
    uint16_t home_sector = dir_sectors[4];
    uint16_t usr_sector = dir_sectors[8];
    uint16_t programs_sector;
    create_dir(usr_sector, "programs", &programs_sector);
    
struct { const char* name; const char* content; } cmds[] = {
    {"help", "#!/bin/wnkc\n"
     "print \"=== WNKA X32 HELP ===\"\n"
     "print \"\"\n"
     "print \"System: reboot, shut, halt, cls, time, fetch\"\n"
     "print \"Files:  ls, cd, cat, create, delete, mkdir, rmdir\"\n"
     "print \"        copy, move, find, stat, hexdump, rename\"\n"
     "print \"Disk:   df, format, check, speed, diskinfo\"\n"
     "print \"Net:    netinit, ping, browse, http, dns\"\n"
     "print \"Apps:   edit, notepad, calc, galc, paint, piano\"\n"
     "print \"        clock, timer, slides, sheet, fm\"\n"
     "print \"Dev:    tcc, wnkc, ide, uidev, wnxmake, elf\"\n"
     "print \"Games:  pacman, snake, flappy, guess, rps, dice\"\n"
     "print \"Fun:    matrix, fire, plasma, waves, tunnel, art\"\n"
     "print \"        cowsay, fortune, 8ball, coin, linux\"\n"
     "print \"\"\n"
     "print \"Type 'man <command>' for detailed help\"\n"},

    {"cls", "#!/bin/wnkc\n"
     "graph fill 0x00\n"
     "print \"Screen cleared!\"\n"
     "sleep 1\n"
     "graph fill 0x00\n"},

    {"reboot", "#!/bin/wnkc\n"
     "print \"Restarting system...\"\n"
     "sleep 2\n"
     "print \"Saving data...\"\n"
     "sleep 1\n"
     "print \"Rebooting now!\"\n"
     "run reboot\n"},

    {"shut", "#!/bin/wnkc\n"
     "print \"Shutting down...\"\n"
     "sleep 2\n"
     "print \"It is now safe to turn off your computer.\"\n"
     "run shutdown\n"},

    {"ls", "#!/bin/wnkc\n"
     "print \"Directory listing:\"\n"
     "print \"------------------\"\n"
     "run ls\n"
     "print \"------------------\"\n"},

    {"cd", "#!/bin/wnkc\n"
     "print \"Change directory\"\n"
     "print \"Usage: cd <dirname>\"\n"
     "print \"       cd /  (go to root)\"\n"
     "run cd\n"},

    {"cat", "#!/bin/wnkc\n"
     "print \"File viewer\"\n"
     "print \"============\"\n"
     "run cat\n"},

    {"create", "#!/bin/wnkc\n"
     "print \"Create new file\"\n"
     "print \"===============\"\n"
     "print \"Usage: create <filename>\"\n"
     "print \"Then use 'save' to write content\"\n"},

    {"delete", "#!/bin/wnkc\n"
     "print \"Delete file\"\n"
     "print \"============\"\n"
     "print \"Usage: delete <filename>\"\n"
     "print \"WARNING: This cannot be undone!\"\n"},

    {"mkdir", "#!/bin/wnkc\n"
     "print \"Create directory\"\n"
     "print \"=================\"\n"
     "print \"Usage: mkdir <dirname>\"\n"},

    {"rmdir", "#!/bin/wnkc\n"
     "print \"Remove directory\"\n"
     "print \"=================\"\n"
     "print \"Usage: rmdir <dirname>\"\n"
     "print \"Directory must be empty first!\"\n"},

    {"copy", "#!/bin/wnkc\n"
     "print \"Copy file\"\n"
     "print \"==========\"\n"
     "print \"Usage: copy <source> <dest>\"\n"
     "print \"Example: copy file.txt backup.txt\"\n"},

    {"move", "#!/bin/wnkc\n"
     "print \"Move/Rename file\"\n"
     "print \"=================\"\n"
     "print \"Usage: move <oldname> <newname>\"\n"
     "print \"       move file.txt folder/\"\n"},

    {"time", "#!/bin/wnkc\n"
     "print \"Current system time:\"\n"
     "run time\n"
     "print \"\"\n"
     "print \"Tip: Use 'clock' for ASCII clock display\"\n"},

    {"wnkc", "#!/bin/wnkc\n"
     "print \"WnkC Scripting Language\"\n"
     "print \"========================\"\n"
     "print \"Commands: print, input, let, if, while, for\"\n"
     "print \"          array, struct, func, import, graph\"\n"
     "print \"          key, sleep, rand, getkey, run\"\n"
     "print \"\"\n"
     "print \"Example: wnkc 'print Hello World'\"\n"
     "print \"         wnkc myscript.wnc\"\n"},

    {"calc", "#!/bin/wnkc\n"
     "print \"WNKA Calculator\"\n"
     "print \"===============\"\n"
     "print \"Usage: calc <expression>\"\n"
     "print \"Example: calc 5+3\"\n"
     "print \"         calc 10*20\"\n"
     "print \"\"\n"
     "print \"Or type 'galc' for graphical calculator!\"\n"},

    {"paint", "#!/bin/wnkc\n"
     "print \"WNKA Paint\"\n"
     "print \"===========\"\n"
     "print \"Drawing program with:\"\n"
     "print \"  - 16 colors palette\"\n"
     "print \"  - Brush size control (+/-)\"\n"
     "print \"  - WASD movement\"\n"
     "print \"  - Space to draw, Shift+Space to erase\"\n"
     "print \"  - C to clear canvas\"\n"
     "print \"  - ESC to exit\"\n"},

    {"piano", "#!/bin/wnkc\n"
     "print \"WNKA Piano\"\n"
     "print \"===========\"\n"
     "print \"PC Speaker piano with:\"\n"
     "print \"  White keys: Z X C V B N M\"\n"
     "print \"  Black keys: S D   G H J\"\n"
     "print \"  R - Record, P - Play, C - Clear\"\n"
     "print \"  ESC - Exit\"\n"},

    {"pacman", "#!/bin/wnkc\n"
     "print \"ASCII Pacman\"\n"
     "print \"============\"\n"
     "print \"Classic arcade game!\"\n"
     "print \"  Arrows - Move\"\n"
     "print \"  Eat all dots to win\"\n"
     "print \"  Avoid ghosts!\"\n"
     "print \"  ESC - Exit\"\n"},

    {"snake", "#!/bin/wnkc\n"
     "print \"Snake Game\"\n"
     "print \"===========\"\n"
     "print \"Classic snake game!\"\n"
     "print \"  Arrows - Change direction\"\n"
     "print \"  Eat @ to grow\"\n"
     "print \"  Don't hit walls or yourself!\"\n"
     "print \"  ESC - Exit\"\n"},

    {"matrix", "#!/bin/wnkc\n"
     "print \"Matrix Rain Effect\"\n"
     "print \"===================\"\n"
     "print \"Press ESC to exit\"\n"
     "run matrix\n"},

    {"fire", "#!/bin/wnkc\n"
     "print \"Fire Effect\"\n"
     "print \"=============\"\n"
     "print \"Press ESC to exit\"\n"
     "run fire\n"},

    {"clock", "#!/bin/wnkc\n"
     "print \"ASCII Clock\"\n"
     "print \"============\"\n"
     "print \"Large ASCII digital clock\"\n"
     "print \"Shows date and time\"\n"
     "print \"Press ESC to exit\"\n"
     "run clock\n"},

    {"ui", "#!/bin/wnkc\n"
     "print \"Starting WnkUI Desktop...\"\n"
     "print \"=========================\"\n"
     "print \"Features:\"\n"
     "print \"  - Window manager with themes\"\n"
     "print \"  - File manager\"\n"
     "print \"  - Notepad, Calculator, Terminal\"\n"
     "print \"  - Paint, Media Player\"\n"
     "print \"  - Settings panel\"\n"
     "print \"  - Screensaver (8 types)\"\n"
     "print \"\"\n"
     "print \"Controls: WASD/Arrows - Move mouse\"\n"
     "print \"          Q - Click, R - Start menu\"\n"
     "print \"          M - Drag windows\"\n"
     "print \"          ESC - Close menus\"\n"
     "run ui\n"},

    {"fm", "#!/bin/wnkc\n"
     "print \"File Manager\"\n"
     "print \"=============\"\n"
     "print \"Two-panel file manager\"\n"
     "print \"Commands:\"\n"
     "print \"  F1 - Help    F2 - Save    F3 - View\"\n"
     "print \"  F4 - Edit    F5 - Copy    F6 - Move\"\n"
     "print \"  F7 - MkDir   F10 - Quit\"\n"
     "print \"  TAB - Switch panel\"\n"
     "print \"  Enter - Enter directory\"\n"
     "run fm\n"},

    {"install", "#!/bin/wnkc\n"
     "print \"WNKA OS Installer\"\n"
     "print \"==================\"\n"
     "print \"Installs WNKA OS to disk\"\n"
     "print \"Options:\"\n"
     "print \"  - WnkaFS (simple)\"\n"
     "print \"  - WnkFS (recommended)\"\n"
     "print \"  - CD-ROM installation\"\n"
     "print \"  - Low-level format\"\n"
     "run install\n"},

    {"wnkasxs", "#!/bin/wnkc\n"
     "print \"WNKA System Backup\"\n"
     "print \"===================\"\n"
     "print \"Creates backup of all system files\"\n"
     "print \"to /WnkaSXS directory\"\n"
     "print \"\"\n"
     "print \"Restore with: wnkasxs_restore\"\n"
     "run wnakasxs\n"},

    {"fetch", "#!/bin/wnkc\n"
     "print \"     SYSTEM INFO\"\n"
     "run fetch\n"},

    {"beep", "#!/bin/wnkc\n"
     "print \"Playing test sound...\"\n"
     "run beep\n"
     "print \"Done!\"\n"},

    {"df", "#!/bin/wnkc\n"
     "print \"Disk Free Space\"\n"
     "print \"================\"\n"
     "run df\n"},

    {"find", "#!/bin/wnkc\n"
     "print \"File Search\"\n"
     "print \"============\"\n"
     "print \"Usage: find <filename>\"\n"
     "print \"Searches all directories\"\n"},

    {"edit", "#!/bin/wnkc\n"
     "print \"WNKA Text Editor v2.0\"\n"
     "print \"======================\"\n"
     "print \"Features:\"\n"
     "print \"  - Syntax highlighting\"\n"
     "print \"  - Multiple colors\"\n"
     "print \"  - Insert/Overwrite mode\"\n"
     "print \"  - Ctrl+M for menu\"\n"
     "print \"  - F1 to save, F2 to exit\"\n"
     "print \"\"\n"
     "print \"Usage: edit <filename>\"\n"},

    {"notepad", "#!/bin/wnkc\n"
     "print \"Simple Notepad\"\n"
     "print \"===============\"\n"
     "print \"Quick text editor\"\n"
     "print \"  F1 - Save\"\n"
     "print \"  ESC - Exit\"\n"
     "print \"Usage: notepad <filename>\"\n"},

    {"tcc", "#!/bin/wnkc\n"
     "print \"Tiny C Compiler\"\n"
     "print \"================\"\n"
     "print \"Compile and run C code\"\n"
     "print \"Usage:\"\n"
     "print \"  tcc 'int main() { return 42; }'\"\n"
     "print \"  tcc myprogram.c\"\n"},

    {"ide", "#!/bin/wnkc\n"
     "print \"WNKA IDE\"\n"
     "print \"=========\"\n"
     "print \"Code editor with:\"\n"
     "print \"  - Syntax highlighting\"\n"
     "print \"  - Auto-suggestions\"\n"
     "print \"  - F1 Menu, F2 Run, F3 Check\"\n"
     "print \"  - F4 Save, F5 New, F6 Help\"\n"
     "print \"  - Arrows/Enter/Tab\"\n"},

    {"netinit", "#!/bin/wnkc\n"
     "print \"Network Initialization\"\n"
     "print \"======================\"\n"
     "print \"Initializes E1000 network card\"\n"
     "print \"Sets IP: 10.0.2.15\"\n"
     "run netinit\n"},

    {"ping", "#!/bin/wnkc\n"
     "print \"Ping Utility\"\n"
     "print \"=============\"\n"
     "print \"Usage: ping <ip>\"\n"
     "print \"Example: ping 8.8.8.8\"\n"
     "print \"         ping google.com\"\n"},

    {"screensaver", "#!/bin/wnkc\n"
     "print \"Screensavers\"\n"
     "print \"============\"\n"
     "print \"Available:\"\n"
     "print \"  stars    - Starfield\"\n"
     "print \"  screensaver - Random\"\n"
     "print \"Press ESC to exit\"\n"},

    {"timer", "#!/bin/wnkc\n"
     "print \"Countdown Timer\"\n"
     "print \"================\"\n"
     "print \"Usage: timer <seconds>\"\n"
     "print \"Example: timer 60\"\n"
     "print \"Plays alarm when done!\"\n"},

    {"slides", "#!/bin/wnkc\n"
     "print \"Slide Show Creator\"\n"
     "print \"====================\"\n"
     "print \"Create and play presentations\"\n"
     "print \"Usage: slides <filename>\"\n"
     "print \"  F1 Save, F2 Exit, F3 New slide\"\n"
     "print \"  F5 Play slideshow\"\n"},

    {"sheet", "#!/bin/wnkc\n"
     "print \"Spreadsheet Editor\"\n"
     "print \"===================\"\n"
     "print \"Simple CSV spreadsheet\"\n"
     "print \"Usage: sheet <filename>\"\n"
     "print \"  Enter to edit cell\"\n"
     "print \"  Arrows to navigate\"\n"},

    {"fortune", "#!/bin/wnkc\n"
     "print \"Fortune Cookie\"\n"
     "print \"===============\"\n"
     "run fortune\n"},

    {"cowsay", "#!/bin/wnkc\n"
     "print \"Cow Says\"\n"
     "print \"=========\"\n"
     "print \"Usage: cowsay <message>\"\n"
     "print \"Example: cowsay Hello World!\"\n"},

    {"art", "#!/bin/wnkc\n"
     "print \"ASCII Art Gallery\"\n"
     "print \"==================\"\n"
     "print \"Browse ASCII art collection\"\n"
     "print \"Left/Right arrows to navigate\"\n"
     "print \"ESC to exit\"\n"},

    {"rps", "#!/bin/wnkc\n"
     "print \"Rock Paper Scissors\"\n"
     "print \"====================\"\n"
     "print \"Usage: rps <rock|paper|scissors>\"\n"},

    {"guess", "#!/bin/wnkc\n"
     "print \"Guess the Number\"\n"
     "print \"=================\"\n"
     "print \"I'm thinking of a number 1-100\"\n"
     "print \"Can you guess it?\"\n"},

    {"dice", "#!/bin/wnkc\n"
     "print \"Dice Roller\"\n"
     "print \"============\"\n"
     "print \"Usage: dice [sides]\"\n"
     "print \"Default: d6\"\n"},

    {"coin", "#!/bin/wnkc\n"
     "print \"Coin Flip\"\n"
     "print \"==========\"\n"
     "print \"Flipping coin...\"\n"
     "print \"Result: Heads or Tails!\"\n"},

    {"8ball", "#!/bin/wnkc\n"
     "print \"Magic 8-Ball\"\n"
     "print \"============\"\n"
     "print \"Ask a question and get answer!\"\n"},

    {"linux", "#!/bin/wnkc\n"
     "print \"   .--.\"\n"
     "print \"  |o_o |\"\n"
     "print \"  |:_/ |\"\n"
     "print \" //   \\ \\\\\"\n"
     "print \"(|     |)\"\n"
     "print \"/'\\_   _/`\\\\\"\n"
     "print \"\\\\___)=(___/\"\n"
     "print \"\"\n"
     "print \"Not Linux, but close!\"\n"},

    {"memtest", "#!/bin/wnkc\n"
     "print \"Memory Test\"\n"
     "print \"============\"\n"
     "print \"Tests system RAM\"\n"
     "print \"Usage: memtest [size_in_mb]\"\n"},

    {"speed", "#!/bin/wnkc\n"
     "print \"Disk Speed Test\"\n"
     "print \"================\"\n"
     "print \"Tests read/write speed\"\n"
     "run speed\n"},

    {"format", "#!/bin/wnkc\n"
     "print \"WARNING: This will ERASE ALL DATA!\"\n"
     "print \"Use 'install' instead for safe setup\"\n"
     "print \"Or type 'lowformat' for low-level format\"\n"},
};

int cmd_count = sizeof(cmds) / sizeof(cmds[0]);
local_total_files += cmd_count;
    
    for(int i = 0; i < cmd_count; i++) {
        int retries = 0;
        int created = 0;
        
        while(!created && retries < 5) {
            update_file_progress((retries + 1) * 20, cmds[i].name, 
                               retries > 0 ? "Retrying..." : "Creating...", retries);
            
            create_file(bin_sector, cmds[i].name, cmds[i].content);
            
            if(verify_file(bin_sector, cmds[i].name, cmds[i].content)) {
                created = 1;
                local_files_ok++;
                if(retries > 0) local_files_retried++;
                update_file_progress(100, cmds[i].name, "OK", 0);
            } else {
                retries++;
                if(retries >= 5) {
                    local_files_failed++;
                    update_file_progress(0, cmds[i].name, "FAILED!", 5);
                }
            }
        }
        
        int percent = 40 + (i * 10 / cmd_count);
        update_total_progress(percent, local_files_ok, local_files_retried, local_files_failed);
    }
    
    local_total_files += script_count;
    
    for(int i = 0; i < script_count; i++) {
        if(wnc_scripts[i].enabled) {
            int retries = 0;
            int created = 0;
            const char* content = get_script_content(wnc_scripts[i].name);
            
            while(!created && retries < 5 && content) {
                update_file_progress((retries + 1) * 20, wnc_scripts[i].name,
                                   retries > 0 ? "Retrying..." : "Creating...", retries);
                
                create_file(programs_sector, wnc_scripts[i].name, content);
                
                if(verify_file(programs_sector, wnc_scripts[i].name, content)) {
                    created = 1;
                    local_files_ok++;
                    if(retries > 0) local_files_retried++;
                    update_file_progress(100, wnc_scripts[i].name, "OK", 0);
                } else {
                    retries++;
                    if(retries >= 5) {
                        local_files_failed++;
                        update_file_progress(0, wnc_scripts[i].name, "FAILED!", 5);
                    }
                }
            }
            
            int percent = 50 + (local_files_ok * 45 / local_total_files);
            update_total_progress(percent, local_files_ok, local_files_retried, local_files_failed);
        }
    }
    
    update_file_progress(50, "config files", "Creating...", 0);
    
    char welcome[256];
    my_sprintf(welcome, "print \"Welcome to WNKA OS, %s\"\nprint \"Scripts: %d\"\n", 
               username, local_files_ok);
    create_file(home_sector, ".welcome.wnc", welcome);
    
    char readme[512];
    my_sprintf(readme, "WNKA OS v1.0\nFS: %s\nCPU: %s\nRAM: %d MB\nFiles: %d OK, %d failed\n",
               use_wnkfs ? "WnkFS" : "Standard", cpu_list[cpu_selected], 
               ram_mb_detected, local_files_ok, local_files_failed);
    create_file(root_sector, "README.txt", readme);
    
char quickstart_text[4096];
sprintf(quickstart_text,
    "WNKA X32 - QUICK START GUIDE\n"
    "=============================\n"
    "\n"
    "FIRST BOOT:\n"
    "  1. Login with your password\n"
    "     - Default: no password (just press Enter)\n"
    "  2. Type 'help' to see all commands\n"
    "  3. Type 'ui' to start desktop environment\n"
    "\n"
    "BASIC COMMANDS:\n"
    "  ls       - list files in current directory\n"
    "  cd <dir> - change directory\n"
    "  cat <f>  - view file content\n"
    "  create   - create new file\n"
    "  delete   - delete file\n"
    "  mkdir    - create directory\n"
    "  rmdir    - remove directory\n"
    "  copy     - copy file\n"
    "  move     - move/rename file\n"
    "  pwd      - show current path\n"
    "  find     - search for files\n"
    "\n"
    "SYSTEM COMMANDS:\n"
    "  reboot   - restart computer\n"
    "  shut     - power off\n"
    "  halt     - halt system\n"
    "  time     - show current time\n"
    "  fetch    - show system info\n"
    "  beep     - test speaker\n"
    "  cls      - clear screen\n"
    "  help     - show all commands\n"
    "\n"
    "APPLICATIONS:\n"
    "  ui       - WnkUI desktop (GUI)\n"
    "  vesaui   - VESA GUI (1024x768)\n"
    "  fm       - file manager\n"
    "  edit     - text editor\n"
    "  notepad  - simple notepad\n"
    "  calc     - calculator\n"
    "  galc     - graphical calculator\n"
    "  paint    - drawing program\n"
    "  piano    - PC speaker piano\n"
    "  tcc      - C compiler\n"
    "  wnkc     - scripting language\n"
    "  ide      - code editor\n"
    "  clock    - ASCII clock\n"
    "  timer    - countdown timer\n"
    "\n"
    "GAMES:\n"
    "  pacman   - Pacman game\n"
    "  snake    - Snake game\n"
    "  flappy   - Flappy Bird\n"
    "  matrix   - Matrix rain effect\n"
    "  fire     - Fire effect\n"
    "  rain     - ASCII rain\n"
    "  stars    - Starfield\n"
    "  plasma   - Plasma effect\n"
    "  waves    - Waves effect\n"
    "  tunnel   - 3D tunnel effect\n"
    "\n"
    "NETWORK:\n"
    "  netinit  - initialize network\n"
    "  ping <ip>- test connection\n"
    "  dns <h>  - resolve hostname\n"
    "  browse   - web browser\n"
    "  http <u> - HTTP request\n"
    "  netinfo  - network status\n"
    "\n"
    "DISK TOOLS:\n"
    "  df       - disk free space\n"
    "  stat     - file info\n"
    "  hexdump  - hex viewer\n"
    "  hexedit  - hex editor\n"
    "  format   - format disk\n"
    "  check    - disk health\n"
    "  speed    - disk speed test\n"
    "\n"
    "MULTITASKING:\n"
    "  ps       - process list\n"
    "  kill <pid>- kill process\n"
    "  bg <cmd> - run in background\n"
    "  jobs     - list background jobs\n"
    "  fg <id>  - bring to foreground\n"
    "\n"
    "LINUX COMPATIBILITY:\n"
    "  linux_init - init Linux layer\n"
    "  linux_run  - run ELF binary\n"
    "  linux_wine - run Windows .exe\n"
    "\n"
    "Need help? Type 'help' for detailed info\n"
    "Or type 'man <command>' for specific help\n");
create_file(root_sector, "QUICKSTART.txt", quickstart_text);

char license_text[4096];
sprintf(license_text,
    "GNU GENERAL PUBLIC LICENSE\n"
    "Version 3, 29 June 2007\n"
    "\n"
    "Copyright (C) 2025 WNKA OS Project\n"
    "\n"
    "This program is free software: you can redistribute it and/or modify\n"
    "it under the terms of the GNU General Public License as published by\n"
    "the Free Software Foundation, either version 3 of the License, or\n"
    "(at your option) any later version.\n"
    "\n"
    "This program is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n"
    "GNU General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU General Public License\n"
    "along with this program. If not, see <https://www.gnu.org/licenses/>.\n"
    "\n"
    "WNKA X32 SPECIFIC TERMS:\n"
    "  - You may use this OS as a base for your own operating system\n"
    "  - You must keep the original copyright notices\n"
    "  - Your OS must also be open source under GPLv3\n"
    "  - You may not claim this OS as your own original work\n"
    "  - You must credit the WNKA OS Project\n"
    "\n"
    "THIRD-PARTY COMPONENTS:\n"
    "  - TinyCC (TCC) - LGPL license\n"
    "  - Font data - Public Domain\n"
    "  - PCI IDs - BSD license\n"
    "\n"
    "CONTACT:\n"
    "  GitHub: github.com/wnka-os\n"
    "  Email:  wnka@example.com\n");
create_file(root_sector, "LICENSE.txt", license_text);

char changelog_text[4096];
sprintf(changelog_text,
    "WNKA X32 - CHANGELOG\n"
    "====================\n"
    "\n"
    "Version 1.0 (2025)\n"
    "------------------\n"
    "  CORE:\n"
    "  - First stable release\n"
    "  - 32-bit protected mode kernel\n"
    "  - Custom bootloader (stage1+stage2)\n"
    "  - GDT/IDT setup\n"
    "  - IRQ handling (PIC 8259)\n"
    "  - PIT timer (100Hz)\n"
    "  - CMOS RTC clock\n"
    "  - Exception handlers (0-31)\n"
    "  - Page fault recovery\n"
    "\n"
    "  FILESYSTEMS:\n"
    "  - WnkFS (native filesystem)\n"
    "  - WnkaFS (simple 32-entry FS)\n"
    "  - RAMFS (in-memory FS, 16MB)\n"
    "  - FAT12 floppy support\n"
    "  - ISO 9660 CD-ROM support\n"
    "  - EXT2 read support\n"
    "  - MBR partition table\n"
    "\n"
    "  DRIVERS:\n"
    "  - ATA/IDE PIO mode\n"
    "  - AHCI SATA (basic)\n"
    "  - ATAPI CD-ROM\n"
    "  - Floppy controller (FDC)\n"
    "  - PCI bus scanner\n"
    "  - E1000 network card\n"
    "\n"
    "  GRAPHICS:\n"
    "  - Text mode 80x25 (default)\n"
    "  - Extended text modes (80x50, 132x43, 132x60)\n"
    "  - VGA 320x200x256 (Mode 13h)\n"
    "  - VESA/VBE 1024x768x16\n"
    "  - BGA (Bochs/QEMU) support\n"
    "  - VirtualBox VGA support\n"
    "  - VMware SVGA support\n"
    "  - Hardware mouse cursor\n"
    "\n"
    "  GUI:\n"
    "  - WnkUI (VGA 320x200 desktop)\n"
    "  - VESA GUI (1024x768 desktop)\n"
    "  - Window manager with themes\n"
    "  - File manager (two-panel)\n"
    "  - Notepad with syntax highlighting\n"
    "  - Calculator\n"
    "  - Terminal\n"
    "  - Paint program\n"
    "  - Piano (PC speaker)\n"
    "  - Settings panel\n"
    "  - Screensaver (8 types)\n"
    "  - Taskbar with clock\n"
    "  - Start menu\n"
    "  - Desktop icons\n"
    "  - Window animations\n"
    "  - Drag and drop\n"
    "  - Context menus\n"
    "\n"
    "  NETWORK:\n"
    "  - E1000 network driver\n"
    "  - ARP protocol\n"
    "  - IPv4 stack\n"
    "  - ICMP (ping)\n"
    "  - UDP sockets\n"
    "  - DNS resolver\n"
    "  - HTTP client\n"
    "  - Web browser (basic)\n"
    "\n"
    "  PROGRAMMING:\n"
    "  - WnkC scripting language\n"
    "    (variables, arrays, structs, functions,\n"
    "     if/while/for, graphics, file I/O)\n"
    "  - TCC (Tiny C Compiler) integration\n"
    "  - WNX executable format\n"
    "  - IDE with syntax highlighting\n"
    "  - UI Designer (UIDEV)\n"
    "\n"
    "  LINUX COMPATIBILITY:\n"
    "  - Linux ELF loader (32-bit)\n"
    "  - Linux syscall emulation (~60 syscalls)\n"
    "  - Virtual File System (VFS)\n"
    "  - Wine launcher (basic)\n"
    "  - /proc, /dev, /tmp\n"
    "\n"
    "  MULTITASKING:\n"
    "  - Cooperative multitasking\n"
    "  - Process manager (max 16)\n"
    "  - Mutex synchronization\n"
    "  - Background jobs\n"
    "  - Task list (ps)\n"
    "\n"
    "  GAMES:\n"
    "  - Pacman (with ghosts AI)\n"
    "  - Snake\n"
    "  - Flappy Bird (4 difficulties)\n"
    "  - Guess the number\n"
    "  - Rock Paper Scissors\n"
    "  - Dice roller\n"
    "  - Coin flip\n"
    "  - 8-Ball\n"
    "  - Fortune cookie\n"
    "\n"
    "  VISUAL EFFECTS:\n"
    "  - Matrix rain\n"
    "  - Fire effect\n"
    "  - ASCII rain\n"
    "  - Starfield\n"
    "  - Plasma effect\n"
    "  - Waves effect\n"
    "  - 3D tunnel\n"
    "  - 3D rotating apple (VGA)\n"
    "\n"
    "  TOOLS:\n"
    "  - ASCII clock\n"
    "  - Timer with alarm\n"
    "  - Resource monitor\n"
    "  - Disk benchmark\n"
    "  - Memory tester\n"
    "  - PCI scanner\n"
    "  - CPU info (CPUID)\n"
    "  - Crash test suite\n"
    "  - Watchdog timer\n"
    "\n"
    "  INSTALLER:\n"
    "  - Text-mode installer\n"
    "  - Multiple filesystem options\n"
    "  - WnkFS formatting\n"
    "  - Low-level format\n"
    "  - Script selector (110+ scripts)\n"
    "  - Swap partition creation\n"
    "  - User configuration\n"
    "  - CD-ROM installation\n"
    "  - Floppy import\n"
    "  - Installation log\n"
    "\n"
    "  SECURITY:\n"
    "  - User password protection\n"
    "  - Product key activation\n"
    "  - Memory protection\n"
    "  - Safe I/O wrappers\n"
    "  - Write protection for floppy\n"
    "\n"
    "KNOWN ISSUES:\n"
    "  - Some VESA cards may not work\n"
    "  - E1000 only (no other network cards)\n"
    "  - Maximum 32 files per directory (WnkaFS)\n"
    "  - No USB support\n"
    "  - No sound card support (PC speaker only)\n"
    "  - No SMP (single core only)\n"
    "  - Limited to 4GB RAM (32-bit)\n"
    "  - TCC limited to basic C code\n"
    "  - Wine support is experimental\n"
    "  - No filesystem journaling\n"
    "  - VESA GUI may flicker\n"
    "\n"
    "PLANNED FOR v2.0:\n"
    "  - USB stack (UHCI/EHCI)\n"
    "  - TCP/IP full stack\n"
    "  - Sound Blaster 16 driver\n"
    "  - TrueType font rendering\n"
    "  - PNG/JPEG image support\n"
    "  - ext4 filesystem\n"
    "  - SMP support\n"
    "  - Virtual memory\n"
    "  - Full Wine integration\n"
    "  - Package manager (WPM)\n");
create_file(root_sector, "CHANGELOG.txt", changelog_text);

char readme_text[4096];
sprintf(readme_text,
    "WNKA X32 OPERATING SYSTEM\n"
    "==========================\n"
    "\n"
    "Welcome to WNKA X32!\n"
    "\n"
    "WHAT IS WNKA X32?\n"
    "  WNKA X32 is a 32-bit hobby operating system written from scratch\n"
    "  in C++ and Assembly. It features a custom kernel, filesystem,\n"
    "  GUI, network stack, and Linux compatibility layer.\n"
    "\n"
    "SYSTEM REQUIREMENTS:\n"
    "  - CPU: i386 or better (Pentium recommended)\n"
    "  - RAM: 16 MB minimum (64 MB recommended)\n"
    "  - Disk: 20 MB minimum (1 GB recommended)\n"
    "  - Video: VGA or VESA compatible\n"
    "  - Input: PS/2 keyboard, PS/2 mouse (optional)\n"
    "\n"
    "INSTALLATION:\n"
    "  1. Boot from disk image\n"
    "  2. Type 'install' at the shell prompt\n"
    "  3. Follow the installation wizard\n"
    "  4. Reboot after installation\n"
    "\n"
    "FILESYSTEMS SUPPORTED:\n"
    "  WnkFS    - Native filesystem (recommended)\n"
    "  WnkaFS   - Simple 32-entry FS (legacy)\n"
    "  FAT12    - Floppy disks\n"
    "  ISO 9660 - CD-ROM\n"
    "  EXT2     - Linux (read-only)\n"
    "\n"
    "KEY FEATURES:\n"
    "  * Custom kernel with multitasking\n"
    "  * VESA GUI (1024x768, 16-bit color)\n"
    "  * VGA GUI (320x200, 256 colors)\n"
    "  * Text mode GUI (80x25, 16 colors)\n"
    "  * Built-in C compiler (TCC)\n"
    "  * Scripting language (WnkC)\n"
    "  * Linux ELF binary support\n"
    "  * Wine compatibility layer\n"
    "  * Network stack (E1000)\n"
    "  * Multiple games and effects\n"
    "\n"
    "DIRECTORY STRUCTURE:\n"
    "  /bin       - Executable commands\n"
    "  /boot      - Boot files\n"
    "  /dev       - Device files\n"
    "  /etc       - Configuration\n"
    "  /home      - User directories\n"
    "  /mnt       - Mount points\n"
    "  /proc      - Process info\n"
    "  /tmp       - Temporary files\n"
    "  /usr       - User programs\n"
    "  /var       - Variable data\n"
    "  /WnkaSXS   - System backup\n"
    "\n"
    "HOW TO COMPILE FROM SOURCE:\n"
    "  1. Install GCC cross-compiler for i386\n"
    "  2. Run 'make' in the project directory\n"
    "  3. Run 'make run' to test in QEMU\n"
    "  4. Run 'make iso' to create bootable ISO\n"
    "\n"
    "DEVELOPMENT TOOLS INCLUDED:\n"
    "  - TCC (Tiny C Compiler)\n"
    "  - WnkC (Scripting language)\n"
    "  - IDE (Code editor)\n"
    "  - UIDEV (UI Designer)\n"
    "  - Hex Editor\n"
    "  - Notepad (with colors)\n"
    "\n"
    "FOR DEVELOPERS:\n"
    "  Source code is organized in:\n"
    "  sys/       - Kernel and drivers\n"
    "  build/     - Compiled objects\n"
    "  disk.img   - Bootable disk image\n"
    "\n"
    "  Key source files:\n"
    "  kernel.cpp     - Kernel entry point\n"
    "  shell.cpp      - Command shell\n"
    "  wnvesa.cpp     - VESA GUI\n"
    "  wnkui.cpp      - VGA GUI\n"
    "  install.cpp    - System installer\n"
    "  wnkfs.cpp      - WnkFS filesystem\n"
    "  wnkc.cpp       - WnkC interpreter\n"
    "  tcc.cpp        - C compiler\n"
    "  syscall_linux.cpp - Linux emulation\n"
    "\n"
    "SUPPORT:\n"
    "  GitHub: github.com/wnka-os/wnka-x32\n"
    "  Issues: github.com/wnka-os/wnka-x32/issues\n"
    "  Wiki:   github.com/wnka-os/wnka-x32/wiki\n"
    "\n"
    "THANKS TO:\n"
    "  - OSDev.org community\n"
    "  - TinyCC developers\n"
    "  - QEMU developers\n"
    "  - All contributors\n"
    "\n"
    "Made with love for low-level programming!\n");
create_file(root_sector, "README.txt", readme_text);
    
    create_file(root_sector, ".installed", "WNKA OS Installed\n");
    
    if(use_wnkfs) {
        create_file(root_sector, "WnkFS.TAG", "WnkFS\n");
        wnkfs_umount();
    }
    
    update_file_progress(100, "config files", "OK", 0);
    update_total_progress(100, local_files_ok, local_files_retried, local_files_failed);
    
    files_ok = local_files_ok;
    files_retried = local_files_retried;
    files_failed = local_files_failed;
    
    save_install_log(root_sector);
    
    kprint_at("[ENTER] Check floppy  [ESC] Skip", 27, 23, (COLOR_BLACK << 4) | TXT_CYAN);
    int check = 0;
    while(!check) {
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc == 0x1C) { check = 1; scan_floppy_content(); }
            if(sc == 0x01) check = 2;
        }
    }
kprint("Defragmenting filesystem...\n");

uint16_t dir_buf[256];
read_sector(100, dir_buf);
int used_sectors[256] = {0};
int used_count = 0;

for(int i = 0; i < 32; i++) {
    char name[12] = {0};
    for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
    
    if(name[0] != 0 && name[0] != 0xE5) {
        int sector = dir_buf[i*8 + 6];
        if(sector > 0) {
            used_sectors[used_count++] = sector;
        }
    }
}

kprint("Found ");
kprint_int(used_count);
kprint(" used sectors\n");

int fragmented = 0;

for(int pass = 0; pass < 2; pass++) {
    kprint("\nDefrag pass ");
    kprint_int(pass + 1);
    kprint("/2...\n");
    
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
        
        if(name[0] == 0 || name[0] == 0xE5) continue;
        
        int is_dir = ((char*)dir_buf)[i*16 + 11] == 1;
        int file_sector = dir_buf[i*8 + 6];
        int file_size = dir_buf[i*8 + 7];
        
        if(!is_dir && file_sector > 0 && file_size > 0) {
            int expected_sector = 1000 + i + (pass * 32);
            
            if(file_sector != expected_sector || pass == 0) {
                uint16_t file_buf[256];
                read_sector(file_sector, file_buf);
                int has_data = 0;
                for(int j = 0; j < 256; j++) {
                    if(file_buf[j] != 0) {
                        has_data = 1;
                        break;
                    }
                }
                
                if(has_data || file_size > 0) {
                    uint16_t backup_buf[256];
                    read_sector(file_sector, backup_buf);
                    write_sector(expected_sector, file_buf);
                    uint16_t verify_buf[256];
                    read_sector(expected_sector, verify_buf);
                    
                    int write_ok = 1;
                    for(int j = 0; j < 256; j++) {
                        if(file_buf[j] != verify_buf[j]) {
                            write_ok = 0;
                            break;
                        }
                    }
                    
                    if(write_ok) {
                        dir_buf[i*8 + 6] = expected_sector;
                        write_sector(100, dir_buf);
                        
                        uint16_t zero_buf[256];
                        for(int j = 0; j < 256; j++) zero_buf[j] = 0;
                        write_sector(file_sector, zero_buf);
                        
                        fragmented++;
                        
                        kprint("  Defragged: ");
                        kprint(name);
                        kprint(" (sector ");
                        kprint_int(file_sector);
                        kprint(" -> ");
                        kprint_int(expected_sector);
                        kprint(")\n");
                    } else {
                        write_sector(file_sector, backup_buf);
                        kprint_color("  FAILED: ", TXT_RED);
                        kprint(name);
                        kprint(" - restored backup\n");
                    }
                }
            }
        }
    }
}

if(fragmented == 0) {
    kprint_color("Filesystem is already optimized!\n", TXT_GREEN);
} else {
    kprint_color("Defragmentation complete! ", TXT_GREEN);
    kprint_int(fragmented);
    kprint(" files optimized\n");
}
    
    clear_screen_bg(COLOR_GRAY);
    draw_dframe(10, 3, 60, 18, COLOR_BLUE, TXT_WHITE);
    kprint_at("INSTALLATION COMPLETE", 23, 5, (COLOR_BLUE << 4) | TXT_YELLOW);
    
    kprint_at("Files installed:", 15, 8, (COLOR_BLUE << 4) | TXT_CYAN);
    kprint_int_at(local_files_ok, 32, 8, (COLOR_BLUE << 4) | TXT_GREEN);
    
    if(local_files_retried > 0) {
        kprint_at("Retried:", 15, 9, (COLOR_BLUE << 4) | TXT_YELLOW);
        kprint_int_at(local_files_retried, 32, 9, (COLOR_BLUE << 4) | TXT_YELLOW);
    }
    
    if(local_files_failed > 0) {
        kprint_at("Failed:", 15, 10, (COLOR_BLUE << 4) | TXT_RED);
        kprint_int_at(local_files_failed, 32, 10, (COLOR_BLUE << 4) | TXT_RED);
    }
    
    kprint_at("Filesystem:", 15, 12, (COLOR_BLUE << 4) | TXT_CYAN);
    kprint_at(use_wnkfs ? "WnkFS" : "Standard", 28, 12, (COLOR_BLUE << 4) | TXT_GREEN);
    
    kprint_at("Disk:", 15, 13, (COLOR_BLUE << 4) | TXT_CYAN);
    kprint_at(disk_ok ? "Healthy" : "Warnings", 22, 13, 
             (COLOR_BLUE << 4) | (disk_ok ? TXT_GREEN : TXT_YELLOW));
    
    if(swap_sector_count > 0) {
        kprint_at("Swap:", 15, 14, (COLOR_BLUE << 4) | TXT_CYAN);
        kprint_int_at(swap_sector_count / 2048, 22, 14, (COLOR_BLUE << 4) | TXT_GREEN);
        kprint_at("MB created", 28, 14, (COLOR_BLUE << 4) | TXT_WHITE);
    }
    
    kprint_at("Log saved: /install.log", 18, 16, (COLOR_BLUE << 4) | TXT_CYAN);
    kprint_at("Press any key to reboot...", 22, 18, (COLOR_BLUE << 4) | TXT_YELLOW);
    
    while(!(inb(0x64) & 1));
    while(inb(0x64) & 1) inb(0x60);
    
    outb(0x64, 0xFE);
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
            }
        }
    }
}

static void inst_draw_menu(void) {
    clear_screen_bg(COLOR_GRAY);
    draw_window(15, 3, 50, 20, COLOR_BLUE);
    
    kprint_at("WNKA OS Installer v2.0", 26, 5, (COLOR_BLUE << 4) | COLOR_YELLOW);
    kprint_at("Choose installation type:", 20, 7, (COLOR_BLACK << 4) | COLOR_WHITE);
    
    if(selected == 0) {
        draw_frame(18, 9, 44, 2, COLOR_BLUE, COLOR_WHITE);
        kprint_at("> Standard Install", 22, 10, (COLOR_BLUE << 4) | COLOR_WHITE);
    } else {
        draw_frame(18, 9, 44, 2, COLOR_GRAY, COLOR_WHITE);
        kprint_at("  Standard Install", 22, 10, (COLOR_GRAY << 4) | COLOR_BLACK);
    }
    
    if(selected == 1) {
        draw_frame(18, 11, 44, 2, COLOR_BLUE, COLOR_WHITE);
        kprint_at("> Install + WnkFS (Recommended!)", 20, 12, (COLOR_BLUE << 4) | COLOR_GREEN);
    } else {
        draw_frame(18, 11, 44, 2, COLOR_GRAY, COLOR_WHITE);
        kprint_at("  Install + WnkFS (Recommended!)", 20, 12, (COLOR_GRAY << 4) | COLOR_BLACK);
    }
    
    if(selected == 2) {
        draw_frame(18, 13, 44, 2, COLOR_BLUE, COLOR_WHITE);
        kprint_at("> WnkFS SuperFloppy (No MBR)", 20, 14, (COLOR_BLUE << 4) | COLOR_CYAN);
    } else {
        draw_frame(18, 13, 44, 2, COLOR_GRAY, COLOR_WHITE);
        kprint_at("  WnkFS SuperFloppy (No MBR)", 20, 14, (COLOR_GRAY << 4) | COLOR_BLACK);
    }
    
    if(selected == 3) {
        draw_frame(18, 15, 44, 2, COLOR_BLUE, COLOR_WHITE);
        kprint_at("> Low Format + Install", 24, 16, (COLOR_BLUE << 4) | COLOR_YELLOW);
    } else {
        draw_frame(18, 15, 44, 2, COLOR_GRAY, COLOR_WHITE);
        kprint_at("  Low Format + Install", 24, 16, (COLOR_GRAY << 4) | COLOR_BLACK);
    }
    
    if(selected == 4) {
        draw_frame(18, 17, 44, 2, COLOR_BLUE, COLOR_WHITE);
        kprint_at("> Install from CD-ROM (NEW!)", 20, 18, (COLOR_BLUE << 4) | TXT_GREEN);
    } else {
        draw_frame(18, 17, 44, 2, COLOR_GRAY, COLOR_WHITE);
        kprint_at("  Install from CD-ROM", 24, 18, (COLOR_GRAY << 4) | COLOR_BLACK);
    }
    
    if(selected == 5) {
        draw_frame(18, 19, 44, 2, COLOR_BLUE, COLOR_WHITE);
        kprint_at("> Cancel", 36, 20, (COLOR_BLUE << 4) | COLOR_WHITE);
    } else {
        draw_frame(18, 19, 44, 2, COLOR_GRAY, COLOR_WHITE);
        kprint_at("  Cancel", 36, 20, (COLOR_GRAY << 4) | COLOR_BLACK);
    }
    
    kprint_at("W/S or UP/DOWN: Select, ENTER: Confirm", 14, 21, (COLOR_BLACK << 4) | COLOR_CYAN);
    kprint_at("WnkFS is faster, supports larger files", 14, 22, (COLOR_BLACK << 4) | COLOR_GREEN);
}

static void stage1_install(void) {
    selected = 0;
    
    while(1) {
        inst_draw_menu();
        move_cursor(79, 24);
        
        char key = inst_wait_key();
        
        if((key == 0xE0 || key == 'w') && selected > 0) {
            selected--;
        }
        else if((key == 0xE1 || key == 's') && selected < 5) {
            selected++;
        }
        else if(key == '\n') {
            if(selected == 0) {
                use_wnkfs = 0;
                wnkfs_super_floppy = 0;
                write_install_config(0, "installation_stage_1_completed=true\n");
                stage2_input();
                stage3_install();
                outb(0x64, 0xFE);
                return;
            }
            else if(selected == 1) {
                use_wnkfs = 1;
                wnkfs_super_floppy = 0;
                write_install_config(0, "filesystem=wnkfs\nwnkfs_super_floppy=0\ninstallation_stage_1_completed=true\n");
                stage2_input();
                stage3_install();
                outb(0x64, 0xFE);
                return;
            }
            else if(selected == 2) {
                use_wnkfs = 1;
                wnkfs_super_floppy = 1;
                write_install_config(0, "filesystem=wnkfs\nwnkfs_super_floppy=1\ninstallation_stage_1_completed=true\n");
                stage2_input();
                stage3_install();
                outb(0x64, 0xFE);
                return;
            }
            else if(selected == 3) {
                kprint("\nEnter size in GB (1-12): ");
                int gb = 0;
                int got = 0;
                while(!got) {
                    if(inb(0x64) & 1) {
                        uint8_t sc = inb(0x60);
                        if(sc >= 0x02 && sc <= 0x0B) {
                            int d = sc - 0x02;
                            if(d == 10) d = 0;
                            gb = gb * 10 + d;
                            kprint_char('0' + d);
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
                
                use_wnkfs = 1;
                wnkfs_super_floppy = 0;
                lowformat_disk(gb);
                stage2_input();
                stage3_install();
                outb(0x64, 0xFE);
                return;
            }
            else if(selected == 4) {
                quick_install_from_cd();
                return;
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
int is_system_installed(void) {
    uint16_t stage1_buf[256];
    uint16_t stage2_buf[256];
    read_sector(101, stage1_buf);
    read_sector(102, stage2_buf);
    
    int empty1 = 1, empty2 = 1;
    for(int i = 0; i < 16; i++) {
        if(stage1_buf[i] != 0) empty1 = 0;
        if(stage2_buf[i] != 0) empty2 = 0;
    }
    
    if(empty1 && empty2) {
        uint16_t dir_buf[256];
        read_sector(100, dir_buf);
        for(int i = 0; i < 32; i++) {
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
            
            if(my_strcmp(name, ".installed") == 0) {
                return 1; 
            }
            if(my_strcmp(name, "WnkFS.TAG") == 0) {
                return 2;
            }
        }
        return 0;
    }
    
    char config[256];
    read_install_config(0, config, 256);
    
    if(my_strstr(config, "filesystem=wnkfs") != NULL) {
        return 2;
    }
    if(my_strstr(config, "installation_stage_1_completed=true") != NULL) {
        return 1;
    }
    
    return 0; 
}


void wnk_install(void) {
    int stage = check_install_stage();
    
    if(stage == 1) {
        stage1_install();
    }
    else if(stage == 2) {
        stage2_input();
        kprint_color("[INSTALL] Forcing stage 3...\n", TXT_YELLOW);
        outb(0x64, 0xFE);
    }
    else if(stage == 3) {
        stage3_install();
        outb(0x64, 0xFE);
    }
}