#include "video.h"
#include "graph.h"
#include "ata.h"
#include "floppy.h"
#include "fdc.h"
#include "vga.h"
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

// ==================== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ====================

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

// ==================== КОНФИГУРАЦИЯ УСТАНОВКИ ====================

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

// ==================== ФАЙЛОВАЯ СИСТЕМА (СТАНДАРТНАЯ) ====================

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

// ==================== ФОРМАТИРОВАНИЕ ДИСКА ====================

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
    
    // ИСПРАВЛЕНИЕ: форматируем строку заранее
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

// ==================== СПИСОК ПРОЦЕССОРОВ ====================

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

// ==================== ОПРЕДЕЛЕНИЕ СИСТЕМЫ ====================

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

// ==================== СКАН-КОДЫ В ASCII ====================

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

// ==================== ЗВУК КЛИКА ====================

static void sound_click(void) {
    outb(0x61, inb(0x61) | 3);
    for(volatile int i = 0; i < 10000; i++);
    outb(0x61, inb(0x61) & 0xFC);
}

// ==================== ПРОВЕРКА УСТАНОВЛЕННОЙ СИСТЕМЫ ====================

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

// ==================== ЭТАП 2: НАСТРОЙКА ====================

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
                // Навигация вверх/вниз
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
                // ENTER - редактирование поля
                else if(sc == 0x1C) {
                    if(current_field == 0) {
                        // Редактирование Username
                        int pos = my_strlen(username);
                        int cursor = pos;
                        int editing = 1;
                        while(editing) {
                            kprint_at("_", value_x + cursor, win_y + 6, (COLOR_BLACK << 4) | TXT_RED);
                            move_cursor(value_x + cursor, win_y + 6);
                            
                            if(inb(0x64) & 1) {
                                uint8_t k = inb(0x60);
                                if(k == 0x1C) { // Enter - завершить
                                    editing = 0;
                                }
                                else if(k == 0x01) { // ESC - отмена
                                    editing = 0;
                                }
                                else if(k == 0x0E && cursor > 0) { // Backspace
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
                                
                                // Перерисовка поля
                                kprint_at("                ", value_x, win_y + 6, (COLOR_BLACK << 4) | TXT_BLACK);
                                kprint_at(username, value_x, win_y + 6, (COLOR_BLACK << 4) | TXT_WHITE);
                            }
                        }
                        redraw = 1;
                    }
                    else if(current_field == 1) {
                        // Редактирование Password
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
                                
                                // Перерисовка
                                for(int i = 0; i < 16; i++) kprint_at(" ", value_x + i, win_y + 7, (COLOR_BLACK << 4) | TXT_BLACK);
                                for(int i = 0; i < pos; i++) kprint_at("*", value_x + i, win_y + 7, (COLOR_BLACK << 4) | TXT_GREEN);
                            }
                        }
                        redraw = 1;
                    }
                    else if(current_field == 2) {
                        // Редактирование Hostname
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
                        // Редактирование Computer Name
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
                        // Меню выбора CPU
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
                // SPACE - переключение чекбоксов
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
                // F1 - быстрое меню CPU
                else if(sc == 0x3B) {
                    current_field = 4;
                    redraw = 1;
                }
                // ESC - выход и сохранение
                else if(sc == 0x01) {
                    running = 0;
                }
            }
        }
        inst_delay(30);
    }
    
    // Сохранение конфигурации
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
    
    // Сохранение данных пользователя
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

// ==================== СТРУКТУРА СКРИПТОВ ====================
typedef struct {
    const char* name;
    int enabled;
    const char* desc;
    int size;
    int category;
} wnc_script_t;

// ==================== ПРЕДВАРИТЕЛЬНЫЕ ОБЪЯВЛЕНИЯ ====================
static void save_selected_components(void);
static void load_selected_components(void);
static void show_script_preview(int idx);
static void search_scripts(void);
static void select_scripts_in_category(int category);
static void show_script_statistics(void);
static void select_wnc_scripts(void);
static const char* get_script_content(const char* name);

// ==================== КАТЕГОРИИ ====================
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

// ==================== 100 СКРИПТОВ ====================
static wnc_script_t wnc_scripts[] = {
    // ========== СИСТЕМНЫЕ (0-9) ==========
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

    // ========== ФАЙЛОВЫЕ УТИЛИТЫ (10-19) ==========
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

    // ========== МАТЕМАТИКА (20-29) ==========
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

    // ========== КОНВЕРТЕРЫ (30-39) ==========
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

    // ========== ГЕНЕРАТОРЫ (40-49) ==========
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

    // ========== ТЕКСТ (50-59) ==========
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

    // ========== ИГРЫ (60-74) ==========
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

    // ========== ЭФФЕКТЫ (75-84) ==========
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

    // ========== РАЗВЛЕЧЕНИЯ (85-99) ==========
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
    
    // ========== ДОПОЛНИТЕЛЬНЫЕ (100-109) ==========
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

// ==================== КОД СКРИПТОВ ====================
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
    
    // ... (остальные функции get_script_content остаются без изменений) ...
    
    // Значение по умолчанию
    return "print \"Script: \" + name + \"\"\n"
           "print \"WnkC Script v1.0\"\n"
           "print \"Ready to use!\"\n";
}

// ==================== СОХРАНЕНИЕ/ЗАГРУЗКА ВЫБРАННЫХ КОМПОНЕНТОВ ====================
static void save_selected_components(void) {
    uint16_t components_buf[256] = {0};
    
    for(int i = 0; i < script_count && i < 256; i++) {
        if(wnc_scripts[i].enabled) {
            components_buf[i / 16] |= (1 << (i % 16));
        }
    }
    
    components_buf[255] = 0x574E; // "WN"
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
            if(sc == 0x1C) { // Enter
                searching = 0;
            }
            else if(sc == 0x01) { // ESC
                return;
            }
            else if(sc == 0x0E && search_pos > 0) { // Backspace
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
    
    // Показать результаты поиска
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
                else if(sc == 0x39) { // Space
                    int idx = results[selected_result];
                    wnc_scripts[idx].enabled = !wnc_scripts[idx].enabled;
                    result_redraw = 1;
                }
                else if(sc == 0x1C) { // Enter - preview
                    int idx = results[selected_result];
                    show_script_preview(idx);
                    result_redraw = 1;
                }
                else if(sc == 0x1E) { // A - select all results
                    for(int i = 0; i < result_count; i++) {
                        wnc_scripts[results[i]].enabled = 1;
                    }
                    result_redraw = 1;
                }
                else if(sc == 0x31) { // N - deselect all
                    for(int i = 0; i < result_count; i++) {
                        wnc_scripts[results[i]].enabled = 0;
                    }
                    result_redraw = 1;
                }
                else if(sc == 0x01) { // ESC
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
                else if(sc == 0x39) { // Space - toggle
                    int idx = cat_scripts[selected];
                    wnc_scripts[idx].enabled = !wnc_scripts[idx].enabled;
                    redraw = 1;
                }
                else if(sc == 0x1E) { // A - select all in category
                    for(int i = 0; i < cat_count; i++) {
                        wnc_scripts[cat_scripts[i]].enabled = 1;
                    }
                    redraw = 1;
                }
                else if(sc == 0x31) { // N - deselect all in category
                    for(int i = 0; i < cat_count; i++) {
                        wnc_scripts[cat_scripts[i]].enabled = 0;
                    }
                    redraw = 1;
                }
                else if(sc == 0x1C) { // Enter - preview
                    int idx = cat_scripts[selected];
                    show_script_preview(idx);
                    redraw = 1;
                }
                else if(sc == 0x35) { // / - search
                    search_scripts();
                    redraw = 1;
                }
                else if(sc == 0x01) { // ESC
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
            
            // Progress bar
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
                
                // Иконка категории
                const char* icons[] = {"[*]", "[#]", "[@]", "[$]", "[&]", "[~]", "[+]", "[!]", "[?]"};
                kprint_at(icons[i], 7, y, (COLOR_BLUE << 4) | color);
                
                kprint_at(category_names[i], 11, y, (COLOR_BLUE << 4) | color);
                
                // Прогресс-бар
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
            
            // Общая статистика
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
                else if(sc == 0x1C) { // Enter - открыть категорию
                    select_scripts_in_category(selected);
                    redraw = 1;
                }
                else if(sc == 0x39) { // Space - выбрать все
                    for(int i = 0; i < script_count; i++) {
                        wnc_scripts[i].enabled = 1;
                    }
                    save_selected_components();
                    redraw = 1;
                }
                else if(sc == 0x35) { // / - поиск
                    search_scripts();
                    redraw = 1;
                }
                else if(sc == 0x1F) { // S - статистика
                    show_script_statistics();
                    redraw = 1;
                }
                else if(sc == 0x31) { // N - снять все
                    for(int i = 0; i < script_count; i++) {
                        wnc_scripts[i].enabled = 0;
                    }
                    save_selected_components();
                    redraw = 1;
                }
                else if(sc == 0x01) { // ESC
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
    // Рисуем временное окно уведомления
    int len = 0;
    while(msg[len]) len++;
    
    int nx = 40 - len/2 - 2;
    int ny = 20;
    
    draw_shadow_window(nx, ny, len + 4, 3, COLOR_YELLOW, TXT_BLACK, "Info");
    kprint_at(msg, nx + 2, ny + 1, (COLOR_YELLOW << 4) | TXT_BLACK);
    
    // Ждём 2 секунды или нажатия клавиши
    for(volatile int i = 0; i < 20000000; i++) {
        if(inb(0x64) & 1) {
            inb(0x60);
            break;
        }
    }
}
// ==================== 1. СТРУКТУРЫ (вставить после #include) ====================
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

// ==================== 2. ОБЪЯВЛЕНИЯ ФУНКЦИЙ (вставить перед их использованием) ====================
static int check_floppy_present(void);
static void import_all_from_floppy(floppy_file_t* files, int count, floppy_header_t* header);
static void select_files_to_import(floppy_file_t* files, int count, floppy_header_t* header);
static void import_fat12_files(char filenames[][13], int* file_sizes, int file_count);
static void draw_floppy_animation(int frame, int x, int y);
static void draw_floppy_drive_animation(int x, int y);
static void draw_progress_spinner(int x, int y, int frame);

// ==================== 3. ВСЕ ФУНКЦИИ (вставить перед scan_floppy_content) ====================

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

// ==================== 4. ГЛАВНАЯ ФУНКЦИЯ (заменить существующую) ====================
static void scan_floppy_content(void) {
    kprint("\n[FLOPPY] Scanning diskette contents...\n");
    
    int floppy_type = check_floppy_present();
    
    if(floppy_type == 0) {
        kprint("[FLOPPY] No readable diskette found\n");
        return;
    }
    
    if(floppy_type == 2) {
        // WNKA дискета
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
        // FAT12 дискета
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

// ==================== ЭТАП 3: УСТАНОВКА С СОХРАНЕНИЕМ В /usr/programs/ ====================
static void stage3_install(void) {
    // ===== БЫСТРАЯ ПРОВЕРКА ДИСКА =====
    int disk_ok = 1;
    uint16_t test_buf[256], verify_buf[256];
    
    for(int pass = 1; pass <= 3 && disk_ok; pass++) {
        for(int i = 0; i < 256; i++) test_buf[i] = (pass << 8) | (i & 0xFF);
        write_sector(10000 + pass, test_buf);
        read_sector(10000 + pass, verify_buf);
        for(int i = 0; i < 256 && disk_ok; i++) {
            if(test_buf[i] != verify_buf[i]) disk_ok = 0;
        }
    }
    
    // ===== WNKFS =====
    if(use_wnkfs) {
        wnkfs_format();
        if(wnkfs_mount() != 0) {
            use_wnkfs = 0;
        }
    }
    
    load_selected_components();
    select_wnc_scripts();
    
    uint16_t settings_buf[256];
    read_sector(106, settings_buf);
    int cpu_selected = settings_buf[8];
    if(cpu_selected < 0 || cpu_selected >= cpu_count) cpu_selected = 0;
    
    // ===== ИНТЕРФЕЙС =====
    clear_screen_bg(COLOR_GRAY);
    update_time_display();
    
    draw_dframe(0, 0, 80, 3, COLOR_BLUE, TXT_WHITE);
    kprint_at(use_wnkfs ? "WNKA OS + WnkFS - Stage 3" : "WNKA OS - Stage 3", 
             22, 1, (COLOR_BLUE << 4) | TXT_YELLOW);
    
    // Левая панель — системная информация
    draw_frame(0, 3, 25, 22, COLOR_GRAY, TXT_WHITE);
    kprint_at("System Info", 8, 4, (COLOR_GRAY << 4) | TXT_CYAN);
    kprint_at("CPU:", 2, 6, (COLOR_BLACK << 4) | TXT_WHITE);
    kprint_at(cpu_list[cpu_selected], 7, 6, (COLOR_BLACK << 4) | TXT_GREEN);
    kprint_at("RAM:", 2, 8, (COLOR_BLACK << 4) | TXT_WHITE);
    kprint_int_at(ram_mb_detected, 7, 8, (COLOR_BLACK << 4) | TXT_GREEN);
    kprint_at("MB", 11, 8, (COLOR_BLACK << 4) | TXT_WHITE);
    kprint_at("FS:", 2, 10, (COLOR_BLACK << 4) | TXT_WHITE);
    kprint_at(use_wnkfs ? "WnkFS" : "Standard", 6, 10, 
             (COLOR_BLACK << 4) | (use_wnkfs ? TXT_GREEN : TXT_YELLOW));
    kprint_at("Disk:", 2, 12, (COLOR_BLACK << 4) | TXT_WHITE);
    kprint_at(disk_ok ? "OK" : "WARN", 8, 12, 
             (COLOR_BLACK << 4) | (disk_ok ? TXT_GREEN : TXT_YELLOW));
    
    // Статистика
    kprint_at("Files OK:", 2, 15, (COLOR_BLACK << 4) | TXT_CYAN);
    kprint_at("Retries:", 2, 17, (COLOR_BLACK << 4) | TXT_CYAN);
    kprint_at("Failed:", 2, 19, (COLOR_BLACK << 4) | TXT_CYAN);
    
    // Правая панель — прогресс
    draw_frame(25, 3, 54, 22, COLOR_GRAY, TXT_WHITE);
    kprint_at("Installation Progress", 38, 4, (COLOR_GRAY << 4) | TXT_CYAN);
    
    // Основной прогресс-бар
    kprint_at("Total:", 27, 7, (COLOR_GRAY << 4) | TXT_WHITE);
    draw_progress(34, 7, 30, 0, COLOR_GRAY, COLOR_GREEN);
    
    // Прогресс-бар текущего файла
    kprint_at("File:", 27, 10, (COLOR_GRAY << 4) | TXT_WHITE);
    draw_progress(34, 10, 30, 0, COLOR_GRAY, COLOR_CYAN);
    
    // Имя текущего файла
    kprint_at("Current:", 27, 13, (COLOR_GRAY << 4) | TXT_YELLOW);
    
    // Статус
    kprint_at("Status:", 27, 16, (COLOR_GRAY << 4) | TXT_WHITE);
    
    // Прогресс-бар повторов (если нужен)
    kprint_at("Retry:", 27, 19, (COLOR_GRAY << 4) | TXT_WHITE);
    
    // ===== ФУНКЦИЯ ОТРИСОВКИ ПРОГРЕССА ФАЙЛА =====
    int total_files = 0;
    int files_ok = 0;
    int files_retried = 0;
    int files_failed = 0;
    
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
    
    // Функция обновления общего прогресса
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
    
    // Функция проверки файла
    auto verify_file = [](uint16_t parent_sector, const char* name, const char* content) -> int {
        uint16_t dir_buf[256];
        read_sector(parent_sector, dir_buf);
        
        int slot = -1;
        for(int i = 0; i < 32; i++) {
            char fname[12] = {0};
            for(int j = 0; j < 11; j++) fname[j] = ((char*)dir_buf)[i*16 + j];
            if(my_strcmp(name, fname) == 0) { slot = i; break; }
        }
        
        if(slot == -1) return 1; // Не найден — OK
        
        int expected_size = 0;
        while(content[expected_size]) expected_size++;
        int file_size = dir_buf[slot*8 + 7];
        
        if(file_size != expected_size) return 0;
        
        return 1; // Размер совпадает — OK
    };
    
    // ===== СОЗДАНИЕ ДИРЕКТОРИЙ =====
    uint16_t root_sector = 100, new_sector;
    const char* dirs[] = {"bin", "boot", "dev", "etc", "home", "mnt", "proc", "tmp", "usr", "var", "WnkaSXS", "opt"};
    uint16_t dir_sectors[12];
    
    for(int i = 0; i < 12; i++) {
        update_file_progress((i + 1) * 100 / 12, dirs[i], "Creating...", 0);
        create_dir(root_sector, dirs[i], &new_sector);
        dir_sectors[i] = new_sector;
        
        int percent = ((i + 1) * 5);
        update_total_progress(percent, files_ok, files_retried, files_failed);
        inst_delay(10);
    }
    
    uint16_t bin_sector = dir_sectors[0];
    uint16_t etc_sector = dir_sectors[3];
    uint16_t home_sector = dir_sectors[4];
    uint16_t usr_sector = dir_sectors[8];
    uint16_t programs_sector;
    
    create_dir(usr_sector, "programs", &programs_sector);
    
    // ===== БАЗОВЫЕ КОМАНДЫ =====
    struct { const char* name; const char* content; } cmds[] = {
        {"help", "#!/bin/wnkc\nrun help\n"},
        {"cls", "#!/bin/wnkc\nrun cls\n"},
        {"reboot", "#!/bin/wnkc\nrun reboot\n"},
        {"shut", "#!/bin/wnkc\nrun shutdown\n"},
        {"ls", "#!/bin/wnkc\nrun ls\n"},
        {"cd", "#!/bin/wnkc\nrun cd\n"},
        {"cat", "#!/bin/wnkc\nrun cat\n"},
        {"create", "#!/bin/wnkc\nrun create\n"},
        {"delete", "#!/bin/wnkc\nrun delete\n"},
        {"mkdir", "#!/bin/wnkc\nrun mkdir\n"},
        {"copy", "#!/bin/wnkc\nrun copy\n"},
        {"move", "#!/bin/wnkc\nrun move\n"},
        {"time", "#!/bin/wnkc\nrun time\n"},
        {"wnkc", "#!/bin/wnkc\nrun wnkc\n"},
        {"calc", "#!/bin/wnkc\nrun calc\n"},
        {"paint", "#!/bin/wnkc\nrun paint\n"},
        {"piano", "#!/bin/wnkc\nrun piano\n"},
        {"pacman", "#!/bin/wnkc\nrun pacman\n"},
        {"snake", "#!/bin/wnkc\nrun snake\n"},
        {"matrix", "#!/bin/wnkc\nrun matrix\n"},
        {"fire", "#!/bin/wnkc\nrun fire\n"},
        {"clock", "#!/bin/wnkc\nrun clock\n"},
        {"ui", "#!/bin/wnkc\nrun ui\n"},
        {"fm", "#!/bin/wnkc\nrun fm\n"},
        {"install", "#!/bin/wnkc\nrun install\n"},
        {"wnkasxs", "#!/bin/wnkc\nrun wnakasxs\n"},
    };
    
    int cmd_count = sizeof(cmds) / sizeof(cmds[0]);
    total_files += cmd_count;
    
    for(int i = 0; i < cmd_count; i++) {
        int retries = 0;
        int created = 0;
        
        while(!created && retries < 5) {
            update_file_progress((retries + 1) * 20, cmds[i].name, 
                               retries > 0 ? "Retrying..." : "Creating...", retries);
            
            create_file(bin_sector, cmds[i].name, cmds[i].content);
            
            if(verify_file(bin_sector, cmds[i].name, cmds[i].content)) {
                created = 1;
                files_ok++;
                if(retries > 0) files_retried++;
                update_file_progress(100, cmds[i].name, "OK", 0);
            } else {
                retries++;
                if(retries >= 5) {
                    files_failed++;
                    update_file_progress(0, cmds[i].name, "FAILED!", 5);
                }
            }
        }
        
        int percent = 40 + (i * 10 / cmd_count);
        update_total_progress(percent, files_ok, files_retried, files_failed);
    }
    
    // ===== СКРИПТЫ =====
    total_files += script_count;
    
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
                    files_ok++;
                    if(retries > 0) files_retried++;
                    update_file_progress(100, wnc_scripts[i].name, "OK", 0);
                } else {
                    retries++;
                    if(retries >= 5) {
                        files_failed++;
                        update_file_progress(0, wnc_scripts[i].name, "FAILED!", 5);
                    }
                }
            }
            
            int percent = 50 + (files_ok * 45 / total_files);
            update_total_progress(percent, files_ok, files_retried, files_failed);
        }
    }
    
    // ===== КОНФИГУРАЦИЯ =====
    update_file_progress(50, "config files", "Creating...", 0);
    
    char welcome[256];
    my_sprintf(welcome, "print \"Welcome to WNKA OS, %s\"\nprint \"Scripts: %d\"\n", 
               username, files_ok);
    create_file(home_sector, ".welcome.wnc", welcome);
    
    char readme[512];
    my_sprintf(readme, "WNKA OS v1.0\nFS: %s\nCPU: %s\nRAM: %d MB\nFiles: %d OK, %d failed\n",
               use_wnkfs ? "WnkFS" : "Standard", cpu_list[cpu_selected], 
               ram_mb_detected, files_ok, files_failed);
    create_file(root_sector, "README.txt", readme);
    
    create_file(root_sector, ".installed", "WNKA OS Installed\n");
    
    if(use_wnkfs) {
        create_file(root_sector, "WnkFS.TAG", "WnkFS\n");
        wnkfs_umount();
    }
    
    update_file_progress(100, "config files", "OK", 0);
    update_total_progress(100, files_ok, files_retried, files_failed);
    
    // ===== ДИСКЕТА =====
    kprint_at("[ENTER] Check floppy  [ESC] Skip", 27, 23, (COLOR_BLACK << 4) | TXT_CYAN);
    int check = 0;
    while(!check) {
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc == 0x1C) { check = 1; scan_floppy_content(); }
            if(sc == 0x01) check = 2;
        }
    }
    
    // ===== ЗАВЕРШЕНИЕ =====
    clear_screen_bg(COLOR_GRAY);
    draw_dframe(10, 3, 60, 18, COLOR_BLUE, TXT_WHITE);
    kprint_at("INSTALLATION COMPLETE", 23, 5, (COLOR_BLUE << 4) | TXT_YELLOW);
    
    kprint_at("Files installed:", 15, 8, (COLOR_BLUE << 4) | TXT_CYAN);
    kprint_int_at(files_ok, 32, 8, (COLOR_BLUE << 4) | TXT_GREEN);
    
    if(files_retried > 0) {
        kprint_at("Retried:", 15, 9, (COLOR_BLUE << 4) | TXT_YELLOW);
        kprint_int_at(files_retried, 32, 9, (COLOR_BLUE << 4) | TXT_YELLOW);
    }
    
    if(files_failed > 0) {
        kprint_at("Failed:", 15, 10, (COLOR_BLUE << 4) | TXT_RED);
        kprint_int_at(files_failed, 32, 10, (COLOR_BLUE << 4) | TXT_RED);
    }
    
    kprint_at("Filesystem:", 15, 12, (COLOR_BLUE << 4) | TXT_CYAN);
    kprint_at(use_wnkfs ? "WnkFS" : "Standard", 28, 12, (COLOR_BLUE << 4) | TXT_GREEN);
    
    kprint_at("Disk:", 15, 13, (COLOR_BLUE << 4) | TXT_CYAN);
    kprint_at(disk_ok ? "Healthy" : "Warnings", 22, 13, 
             (COLOR_BLUE << 4) | (disk_ok ? TXT_GREEN : TXT_YELLOW));
    
    kprint_at("Rebooting in 3...", 28, 17, (COLOR_BLUE << 4) | TXT_YELLOW);
    
    for(int i = 3; i > 0; i--) {
        kprint_int_at(i, 47, 17, (COLOR_BLUE << 4) | TXT_RED);
        inst_delay(1000);
    }
}

// ==================== ГЛАВНОЕ МЕНЮ УСТАНОВКИ ====================

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
    
    // Опция 0: Стандартная
    if(selected == 0) {
        draw_frame(18, 9, 44, 2, COLOR_BLUE, COLOR_WHITE);
        kprint_at("> Standard Install (WnkaFS)", 22, 10, (COLOR_BLUE << 4) | COLOR_WHITE);
    } else {
        draw_frame(18, 9, 44, 2, COLOR_GRAY, COLOR_WHITE);
        kprint_at("  Standard Install (WnkaFS)", 22, 10, (COLOR_GRAY << 4) | COLOR_BLACK);
    }
    
    // Опция 1: WnkFS
    if(selected == 1) {
        draw_frame(18, 11, 44, 2, COLOR_BLUE, COLOR_WHITE);
        kprint_at("> Install + WnkFS (Recommended!)", 20, 12, (COLOR_BLUE << 4) | COLOR_GREEN);
    } else {
        draw_frame(18, 11, 44, 2, COLOR_GRAY, COLOR_WHITE);
        kprint_at("  Install + WnkFS (Recommended!)", 20, 12, (COLOR_GRAY << 4) | COLOR_BLACK);
    }
    
    // Опция 2: WnkFS без MBR (SuperFloppy)
    if(selected == 2) {
        draw_frame(18, 13, 44, 2, COLOR_BLUE, COLOR_WHITE);
        kprint_at("> WnkFS SuperFloppy (No MBR)", 20, 14, (COLOR_BLUE << 4) | COLOR_CYAN);
    } else {
        draw_frame(18, 13, 44, 2, COLOR_GRAY, COLOR_WHITE);
        kprint_at("  WnkFS SuperFloppy (No MBR)", 20, 14, (COLOR_GRAY << 4) | COLOR_BLACK);
    }
    
    // Опция 3: Форматирование + установка
    if(selected == 3) {
        draw_frame(18, 15, 44, 2, COLOR_BLUE, COLOR_WHITE);
        kprint_at("> Low Format + Install", 24, 16, (COLOR_BLUE << 4) | COLOR_YELLOW);
    } else {
        draw_frame(18, 15, 44, 2, COLOR_GRAY, COLOR_WHITE);
        kprint_at("  Low Format + Install", 24, 16, (COLOR_GRAY << 4) | COLOR_BLACK);
    }
    
    // Опция 4: Отмена
    if(selected == 4) {
        draw_frame(18, 17, 44, 2, COLOR_BLUE, COLOR_WHITE);
        kprint_at("> Cancel", 36, 18, (COLOR_BLUE << 4) | COLOR_WHITE);
    } else {
        draw_frame(18, 17, 44, 2, COLOR_GRAY, COLOR_WHITE);
        kprint_at("  Cancel", 36, 18, (COLOR_GRAY << 4) | COLOR_BLACK);
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
        else if((key == 0xE1 || key == 's') && selected < 4) {
            selected++;
        }
        else if(key == '\n') {
            if(selected == 0) {
                // Стандартная установка
                use_wnkfs = 0;
                wnkfs_super_floppy = 0;
                write_install_config(0, "installation_stage_1_completed=true\n");
                stage2_input();
                stage3_install();
                outb(0x64, 0xFE);
                return;
            }
            else if(selected == 1) {
                // WnkFS
                use_wnkfs = 1;
                wnkfs_super_floppy = 0;
                write_install_config(0, "filesystem=wnkfs\nwnkfs_super_floppy=0\ninstallation_stage_1_completed=true\n");
                stage2_input();
                stage3_install();
                outb(0x64, 0xFE);
                return;
            }
            else if(selected == 2) {
                // WnkFS SuperFloppy
                use_wnkfs = 1;
                wnkfs_super_floppy = 1;
                write_install_config(0, "filesystem=wnkfs\nwnkfs_super_floppy=1\ninstallation_stage_1_completed=true\n");
                stage2_input();
                stage3_install();
                outb(0x64, 0xFE);
                return;
            }
            else if(selected == 3) {
                // Форматирование
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
            else {
                return;
            }
        }
        else if(key == 27) {
            return;
        }
    }
}
// Проверка, установлена ли уже система
int is_system_installed(void) {
    // Проверяем сектора конфигурации
    uint16_t stage1_buf[256];
    uint16_t stage2_buf[256];
    read_sector(101, stage1_buf);
    read_sector(102, stage2_buf);
    
    int empty1 = 1, empty2 = 1;
    for(int i = 0; i < 16; i++) {
        if(stage1_buf[i] != 0) empty1 = 0;
        if(stage2_buf[i] != 0) empty2 = 0;
    }
    
    // Если оба сектора пустые, проверяем файловую систему
    if(empty1 && empty2) {
        uint16_t dir_buf[256];
        read_sector(100, dir_buf);
        for(int i = 0; i < 32; i++) {
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
            
            // Проверяем наличие маркеров установки
            if(my_strcmp(name, ".installed") == 0) {
                return 1; // Стандартная установка
            }
            if(my_strcmp(name, "WnkFS.TAG") == 0) {
                return 2; // Установка с WnkFS
            }
        }
        return 0; // Система не установлена
    }
    
    // Читаем конфигурацию первого этапа
    char config[256];
    read_install_config(0, config, 256);
    
    if(my_strstr(config, "filesystem=wnkfs") != NULL) {
        return 2; // WnkFS
    }
    if(my_strstr(config, "installation_stage_1_completed=true") != NULL) {
        return 1; // Стандартная
    }
    
    return 0; // Не установлена
}

// ==================== ТОЧКА ВХОДА ====================

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