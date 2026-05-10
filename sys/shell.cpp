#include "video.h"
#include "graph.h"
#include "mouse.h"
#include "wnkfs.h"
#include "ahci.h"
#include "wnkfs_public.h"
#include "string_utils.h"
#include "wnkc.h"
#include "elf_linux.h"
#include "syscall_linux.h"
#include "vfs_linux.h"
#include "scheduler.h"
#include "install.h"
#include "net.h"
#include "e1000.h"
#include "dos_emu.h"
#include "ramfs.h"
#include "ide.h"
#include "fdc.h"
#include "cdrom.h"
#include "vga.h"
#include "vesa.h"
#include "wnvesa.h"
#include "http.h"
#include "floppy.h"
#include "wnkui.h"
#include "sounds.h"
#include "tcc.h"
#include "icmp.h"
#include "multitask.h"
#include "resource_monitor.h"
#include "themes.h"
#include "screensaver.h"
#include <stdint.h>
#include <stdarg.h>


extern netif_t e1000_netif;
extern int e1000_init(void);
extern int dns_resolve(netif_t* netif, const char* hostname);
extern int browse_url(netif_t* netif, const char* url);
extern bool shift_pressed;
extern "C" {
    extern int current_port;
    extern volatile uint32_t* ahci_base;
    extern int send_cmd(int port, uint8_t cmd, uint32_t lba, uint16_t* buffer);
    extern int eject_cdrom(int port);
    extern int close_tray(int port);
    extern int check_cdrom_status(int port);
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
static void inst_delay(int ms) {
    for(volatile int i = 0; i < ms * 10000; i++);
}
static int levenshtein_distance(const char* s1, const char* s2) {
    int len1 = my_strlen(s1);
    int len2 = my_strlen(s2);
    int matrix[32][32];
    
    for(int i = 0; i <= len1; i++) matrix[i][0] = i;
    for(int j = 0; j <= len2; j++) matrix[0][j] = j;
    
    for(int i = 1; i <= len1; i++) {
        for(int j = 1; j <= len2; j++) {
            int cost = (s1[i-1] == s2[j-1]) ? 0 : 1;
            int del = matrix[i-1][j] + 1;
            int ins = matrix[i][j-1] + 1;
            int sub = matrix[i-1][j-1] + cost;
            matrix[i][j] = del;
            if(ins < matrix[i][j]) matrix[i][j] = ins;
            if(sub < matrix[i][j]) matrix[i][j] = sub;
        }
    }
    return matrix[len1][len2];
}
static int my_strncmp(const char* s1, const char* s2, int n) {
    for(int i = 0; i < n; i++) {
        if(s1[i] != s2[i]) return s1[i] - s2[i];
        if(s1[i] == '\0') return 0;
    }
    return 0;
}
#define NULL 0
#ifndef COLOR_BLACK
#define COLOR_BLACK      0x00
#define COLOR_BLUE       0x01
#define COLOR_GREEN      0x02
#define COLOR_CYAN       0x03
#define COLOR_RED        0x04
#define COLOR_MAGENTA    0x05
#define COLOR_BROWN      0x06
#define COLOR_GRAY       0x07
#define COLOR_DARK_GRAY  0x08
#define COLOR_LIGHT_BLUE 0x09
#define COLOR_LIGHT_GREEN 0x0A
#define COLOR_LIGHT_CYAN 0x0B
#define COLOR_LIGHT_RED  0x0C
#define COLOR_LIGHT_MAGENTA 0x0D
#define COLOR_YELLOW     0x0E
#define COLOR_WHITE      0x0F
#endif

#ifndef TXT_BLACK
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
#endif
#define TAB_SIZE 4
#define COLOR_KEYWORD    0x0E
#define COLOR_STRING     0x0C
#define COLOR_COMMENT    0x0A
#define COLOR_NUMBER     0x0B
#define COLOR_FUNCTION   0x0D
#define COLOR_VARIABLE   0x0F
#define COLOR_ERROR      0x04
#define COLOR_DEFAULT    0x07

static const char* keywords[] = {
    "print", "input", "let", "if", "else", "while", "for", "break", "continue",
    "return", "func", "void", "static", "import", "array", "struct", "run",
    "time", "sleep", "rand", "clear", "log", "runscript", "getkey", "graph",
    "key", "mkdir", "cd", "pwd", "ls", "create", "write", "read", "delete",
    "copy", "move", "call", NULL
};

static const char* functions[] = {
    "print", "input", "getkey", "sleep", "rand", "time", "array_create",
    "array_set", "array_get", "struct_create", "struct_field", "struct_set",
    "struct_get", "graph_pixel", "graph_line", "graph_rect", "graph_circle",
    "graph_text", "graph_clear", "key_wait", "key_get", "key_check", NULL
};

static void draw_notification(const char* msg, int duration);
static int wait_key_timeout(int timeout_ms);
static void draw_notification(const char* msg, int duration) {
    static int notification_active = 0;
    static int notification_timer = 0;
    static char notification_msg[64];
    
    notification_active = 1;
    my_strcpy(notification_msg, msg);
    notification_timer = duration * 10;
    
    draw_frame(60, 0, 19, 2, 0x0E, 0x0F);
    kprint_at(notification_msg, 61, 1, 0x0F);
    
    for(int i = 0; i < duration * 10; i++) {
        for(volatile int d = 0; d < 10000; d++);
    }
    
    for(int x = 60; x < 79; x++) {
        put_pixel(x, 0, 0x00, 0x00, ' ');
        put_pixel(x, 1, 0x00, 0x00, ' ');
    }
}
static int wait_key_timeout(int timeout_ms) {
    int timeout = timeout_ms * 1000;
    while(timeout > 0) {
        if(inb(0x64) & 1) return 1;
        for(volatile int i = 0; i < 100; i++);
        timeout--;
    }
    return 0;
}
static int abs(int x) {
    return x < 0 ? -x : x;
}

static unsigned int next = 1;

static int my_rand() {
    next = next * 1103515245 + 12345;
    return (unsigned int)(next / 65536) % 32768;
}

static void my_srand(unsigned int seed) {
    next = seed;
}
void kprint_char(char c) {
    char str[2] = {c, '\0'};
    kprint(str);
}
extern bool str_equal(const char* s1, const char* s2);
extern void beep();
extern void stop();
extern int seconds;  
extern void run_fetch();
uint8_t console_color = 0x07;
extern void kprint_int_at(int num, int x, int y, uint8_t color);
extern void mstop();
extern void mbeep();
extern void show_resource_monitor(void);
extern void show_theme_selector(void);
extern void start_screensaver(void);
extern void starfield_screensaver(void);
extern "C" {
    void check_disk_health();
    void disk_menu();
    void restore_disk_brute();
    void init_disk_system();
    extern uint16_t ata_base_port;
    void read_file(const char* filename);
    void read_sector(uint32_t lba, uint16_t* buffer);
    void write_sector(uint32_t lba, uint16_t* buffer);
    void kernel_panic(const char* message, const char* file, int line, const char* func);
    void power_off_extreme();
    void disk_death();
    void play_reboot_sound();
    void play_sound(uint32_t freq);
    void nosound();
    void megalovania();
    void play_plead_ost();
    void play_compass_ost();
    
}

void kprint_hex8_at(uint8_t n, int x, int y, uint8_t color) {
    const char* hex = "0123456789ABCDEF";
    char buf[3];
    buf[0] = hex[(n >> 4) & 0xF];
    buf[1] = hex[n & 0xF];
    buf[2] = '\0';
    kprint_at(buf, x, y, color);
}

void kprint_hex16_at(uint16_t n, int x, int y, uint8_t color) {
    const char* hex = "0123456789ABCDEF";
    char buf[5];
    buf[0] = hex[(n >> 12) & 0xF];
    buf[1] = hex[(n >> 8) & 0xF];
    buf[2] = hex[(n >> 4) & 0xF];
    buf[3] = hex[n & 0xF];
    buf[4] = '\0';
    kprint_at(buf, x, y, color);
}

void kprint_hex32_at(uint32_t n, int x, int y, uint8_t color) {
    kprint_hex16_at(n >> 16, x, y, color);
    kprint_hex16_at(n & 0xFFFF, x + 4, y, color);
}

void kprint_hex8(uint8_t n) {
    const char* hex = "0123456789ABCDEF";
    char buf[3];
    buf[0] = hex[(n >> 4) & 0xF];
    buf[1] = hex[n & 0xF];
    buf[2] = '\0';
    kprint(buf);
}

void kprint_hex16(uint16_t n) {
    const char* hex = "0123456789ABCDEF";
    char buf[5];
    buf[0] = hex[(n >> 12) & 0xF];
    buf[1] = hex[(n >> 8) & 0xF];
    buf[2] = hex[(n >> 4) & 0xF];
    buf[3] = hex[n & 0xF];
    buf[4] = '\0';
    kprint(buf);
}

void kprint_hex32(uint32_t n) {
    kprint_hex16(n >> 16);
    kprint_hex16(n & 0xFFFF);
}

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(0xCF8, address);
    for(volatile int i = 0; i < 500; i++); 
    return inl(0xCFC);
}

void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t data) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(0xCF8, address);
    outl(0xCFC, data);
}

struct DiskDriver {
    void (*read)(uint32_t lba, uint16_t* buf);
    void (*write)(uint32_t lba, uint16_t* buf);
    const char* name;
};

struct DiskFile {
    char name[8];
    uint32_t start_sector;
    uint32_t size;
    uint8_t parent_id;
};

struct Directory {
    char name[16];
    uint32_t start_sector;
    uint32_t max_files;
    int id;
};

int dir_count = 4; 
int current_dir_id = 0;
uint16_t disk_io_buf[256];

extern DiskDriver ide_driver;
extern DiskDriver real_pc_driver;
extern DiskDriver* current_driver;

char wait_key() {
    while (inb(0x64) & 1) inb(0x60); 
    while (!(inb(0x64) & 1)); 
    uint8_t key = inb(0x60);
    while (!(inb(0x64) & 1));
    inb(0x60); 

    if (key == 0x15) return 'y';
    if (key == 0x01) return 27;
    if (key == 0x02) return '1';
    if (key == 0x03) return '2';
    if (key == 0x04) return '3';
    if (key == 0x05) return '4';
    return 0;
}

int atoi(const char* str) {
    int res = 0;
    int sign = 1;
    int i = 0;
    if (str[0] == '-') {
        sign = -1;
        i++;
    }
    for (; str[i] != '\0'; ++i) {
        if (str[i] < '0' || str[i] > '9') break;
        res = res * 10 + str[i] - '0';
    }
    return sign * res;
}

const char scancode_to_ascii[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

char get_editor_char() {
    while (1) {
        uint8_t status;
        __asm__ volatile("inb $0x64, %0" : "=a"(status));
        if (status & 0x01) {
            uint8_t scancode;
            __asm__ volatile("inb $0x60, %0" : "=a"(scancode));
            if (scancode < 0x80) {
                if (scancode >= sizeof(scancode_to_ascii)) continue;
                return scancode_to_ascii[scancode];
            }
        }
    }
}

#define PACMAN_UP    0
#define PACMAN_DOWN  1
#define PACMAN_LEFT  2
#define PACMAN_RIGHT 3

struct Position { int x, y; };
struct Ghost { Position pos; int dir; int color; };

static char map[21][21];
static Position pacman = {10, 10};
static int pacman_dir = PACMAN_RIGHT;
static int score = 0;
static int lives = 3;
static int dots = 0;
static Ghost ghosts[2] = {{{5, 5}, PACMAN_RIGHT, 0x0C}, {{15, 15}, PACMAN_LEFT, 0x0B}};

static void init_map() {
    for(int x = 0; x < 21; x++) {
        for(int y = 0; y < 21; y++) {
            if(x == 0 || x == 20 || y == 0 || y == 20) map[y][x] = 0;
            else map[y][x] = 1;
        }
    }
    for(int i = 5; i < 16; i++) {
        map[5][i] = 0; map[15][i] = 0; map[i][5] = 0; map[i][15] = 0;
    }
    dots = 0;
    for(int y = 0; y < 21; y++) {
        for(int x = 0; x < 21; x++) {
            if(map[y][x] == 1) dots++;
        }
    }
}

static int is_wall(int x, int y) {
    if(x < 0 || x >= 21 || y < 0 || y >= 21) return 1;
    return (map[y][x] == 0);
}

static void draw_map() {
    for(int y = 0; y < 21; y++) {
        for(int x = 0; x < 21; x++) {
            int sx = x * 2 + 20, sy = y + 3;
            char c = ' ';
            int color = 0x07;
            if(map[y][x] == 0) { c = '#'; color = 0x08; }
            else if(map[y][x] == 1) { c = '.'; color = 0x07; }
            put_block(sx, sy, color, c);
            put_block(sx+1, sy, 0x00, ' ');
        }
    }
}

static void draw_info() {
    kprint_at("SCORE:", 2, 0, 0x0F);
    kprint_int_at(score, 9, 0, 0x0A);
    kprint_at("LIVES:", 2, 1, 0x0F);
    for(int i = 0; i < lives; i++) put_block(9 + i, 1, 0x0C, 'C');
    kprint_at("DOTS:", 2, 2, 0x0F);
    kprint_int_at(dots, 8, 2, 0x0A);
}

static void draw_pacman() {
    int sx = pacman.x * 2 + 20, sy = pacman.y + 3;
    char c = 'C';
    static int anim = 0; anim = (anim + 1) % 4;
    if(anim < 2) {
        switch(pacman_dir) {
            case PACMAN_RIGHT: c = '>'; break;
            case PACMAN_LEFT:  c = '<'; break;
            case PACMAN_UP:    c = '^'; break;
            case PACMAN_DOWN:  c = 'v'; break;
        }
    }
    put_block(sx, sy, 0x0E, c);
    put_block(sx+1, sy, 0x00, ' ');
}

static void draw_ghosts() {
    for(int i = 0; i < 2; i++) {
        int sx = ghosts[i].pos.x * 2 + 20, sy = ghosts[i].pos.y + 3;
        put_block(sx, sy, ghosts[i].color, 'G');
        put_block(sx+1, sy, 0x00, ' ');
    }
}

static void move_pacman(int dir) {
    if(dir != -1) pacman_dir = dir;
    int nx = pacman.x, ny = pacman.y;
    switch(pacman_dir) {
        case PACMAN_LEFT:  nx--; break;
        case PACMAN_RIGHT: nx++; break;
        case PACMAN_UP:    ny--; break;
        case PACMAN_DOWN:  ny++; break;
    }
    if(!is_wall(nx, ny)) {
        if(map[ny][nx] == 1) { score += 10; dots--; map[ny][nx] = 2; }
        pacman.x = nx; pacman.y = ny;
    }
}

static void move_ghosts() {
    for(int i = 0; i < 2; i++) {
        int dx = pacman.x - ghosts[i].pos.x;
        int dy = pacman.y - ghosts[i].pos.y;
        int mx = 0, my = 0;
        if(abs(dx) > abs(dy)) mx = (dx > 0) ? 1 : -1;
        else my = (dy > 0) ? 1 : -1;
        int nx = ghosts[i].pos.x + mx, ny = ghosts[i].pos.y + my;
        if(!is_wall(nx, ny)) { ghosts[i].pos.x = nx; ghosts[i].pos.y = ny; }
        else ghosts[i].dir = (ghosts[i].dir + 1) % 4;
    }
}

static int check_collision() {
    for(int i = 0; i < 2; i++) {
        if(ghosts[i].pos.x == pacman.x && ghosts[i].pos.y == pacman.y) return 1;
    }
    return 0;
}

static void show_menu() {
    clear_screen();
    kprint_at("====================", 25, 5, 0x0E);
    kprint_at("   ASCII PACMAN     ", 25, 6, 0x0E);
    kprint_at("====================", 25, 7, 0x0E);
    kprint_at("1. Start Game       ", 25, 9, 0x0A);
    kprint_at("2. How to Play      ", 25, 10, 0x0F);
    kprint_at("3. Exit             ", 25, 11, 0x0C);
    kprint_at("ESC to return       ", 25, 15, 0x07);
}

static void show_help() {
    clear_screen();
    kprint_at("=== HOW TO PLAY ===", 25, 5, 0x0E);
    kprint_at("ARROWS - Move C    ", 25, 7, 0x0F);
    kprint_at("# - Walls          ", 25, 8, 0x08);
    kprint_at(". - Dots (10 pts)  ", 25, 9, 0x07);
    kprint_at("G - Ghosts         ", 25, 10, 0x0C);
    kprint_at("C - Pacman         ", 25, 11, 0x0E);
    kprint_at("Press any key...   ", 25, 14, 0x07);
    while(!(inb(0x64) & 1));
    while(inb(0x64) & 1) inb(0x60);
}

extern "C" void start_pacman() {
    while(1) {
        show_menu();
        int choice = 0;
        while(!choice) {
            if(inb(0x64) & 1) {
                uint8_t key = inb(0x60);
                if(key == 0x02) choice = 1;
                if(key == 0x03) choice = 2;
                if(key == 0x04) choice = 3;
                if(key == 0x01) return;
            }
        }
        if(choice == 2) { show_help(); continue; }
        if(choice == 3) continue;
        
        init_map();
        pacman.x = 10; pacman.y = 10;
        ghosts[0].pos.x = 5; ghosts[0].pos.y = 5;
        ghosts[1].pos.x = 15; ghosts[1].pos.y = 15;
        score = 0; lives = 3;
        
        int game_over = 0, move_timer = 0;
        clear_screen();
        
        while(!game_over) {
            draw_map(); draw_info(); draw_ghosts(); draw_pacman();
            move_cursor(79, 24);
            
            int dir = -1;
            if(inb(0x64) & 1) {
                uint8_t key = inb(0x60);
                if(key == 0x4B) dir = PACMAN_LEFT;
                else if(key == 0x4D) dir = PACMAN_RIGHT;
                else if(key == 0x48) dir = PACMAN_UP;
                else if(key == 0x50) dir = PACMAN_DOWN;
                else if(key == 0x01) game_over = 1;
            }
            
            if(++move_timer > 5) {
                move_timer = 0;
                move_pacman(dir);
                move_ghosts();
                
                if(check_collision()) {
                    lives--;
                    if(lives <= 0) {
                        kprint_at("=== GAME OVER ===", 25, 20, 0x4F);
                        for(int i = 0; i < 30000000; i++);
                        game_over = 1;
                    } else {
                        pacman.x = 10; pacman.y = 10;
                        for(int i = 0; i < 30000000; i++);
                    }
                }
                if(dots == 0) {
                    kprint_at("=== YOU WIN! ===", 25, 20, 0x2F);
                    for(int i = 0; i < 30000000; i++);
                    game_over = 1;
                }
            }
            for(int i = 0; i < 5000; i++);
        }
    }
}

void start_flappy() {
    int restart_game = 1;
    while (restart_game) {
        clear_screen();
        int current_jump = 3, current_g_ticks = 5, current_delay = 10000000, current_gap = 6;
        const char* mode_name = "NORMAL";
        uint8_t mode_color = 0x0E;

        kprint_at("=== SELECT DIFFICULTY ===", 28, 5, 0x0F);
        kprint_at("1. EASY   (Wide & Slow)     ", 25, 8, 0x0A);
        kprint_at("2. NORMAL (Standard)        ", 25, 10, 0x0E);
        kprint_at("3. HARD   (Narrow & Fast)   ", 25, 12, 0x0C);
        kprint_at("4. INSANE (SPEEDCORE MODE)  ", 25, 14, 0x0D);
        kprint_at("Press ESC to Exit", 31, 18, 0x07);

        int selected = 0;
        while(!selected) {
            uint8_t m_key = inb(0x60);
            if (m_key == 0x02) {
                current_jump = 4; current_g_ticks = 7; current_delay = 15000000; current_gap = 9;
                mode_name = "EASY"; mode_color = 0x0A; selected = 1;
            }
            else if (m_key == 0x03) {
                current_jump = 3; current_g_ticks = 5; current_delay = 10000000; current_gap = 7;
                mode_name = "NORMAL"; mode_color = 0x0E; selected = 1;
            }
            else if (m_key == 0x04) {
                current_jump = 3; current_g_ticks = 4; current_delay = 2500000; current_gap = 6;
                mode_name = "HARD"; mode_color = 0x0C; selected = 1;
            }
            else if (m_key == 0x05) {
                current_jump = 2; current_g_ticks = 8; current_delay = 2500000; current_gap = 5;
                mode_name = "INSANE"; mode_color = 0x0D; selected = 1;
            }
            else if (m_key == 0x01) return;
        }

        int running = 1;
        int b_y = 12, score = 0, g_count = 0;
        int locked = 0;
        int p_x[2] = {78, 118}; 
        int p_gap[2] = {8, 12};
        
        int old_b_y = b_y;
        int old_p_x[2] = {p_x[0], p_x[1]};
        int old_p_gap[2] = {p_gap[0], p_gap[1]};

        clear_screen();
        while (running) {
            uint8_t key = inb(0x60);
            
            if (key == 0x39) {
                if (!locked) { 
                    b_y -= current_jump; 
                    g_count = 0; 
                    locked = 1; 
                }
            } 
            else if (key == 0xB9 || key == 0x00) { 
                locked = 0; 
            }
            
            if (key == 0x01) { 
                running = 0; 
                restart_game = 0; 
                break; 
            }

            g_count++;
            if (g_count >= current_g_ticks) { 
                b_y++; 
                g_count = 0; 
            }

            if (old_b_y >= 1 && old_b_y <= 24) {
                kprint_at("  ", 10, old_b_y, 0x07);
            }
            
            for (int i = 0; i < 2; i++) {
                if (old_p_x[i] >= 0 && old_p_x[i] < 80) {
                    for(int y=1; y<25; y++) {
                        kprint_at(" ", old_p_x[i], y, 0x07);
                    }
                }
            }

            for (int i = 0; i < 2; i++) {
                p_x[i]--;
                if (p_x[i] < 1) {
                    p_x[i] = 79;
                    p_gap[i] = 2 + (seconds * (i + 7) % 13); 
                    score++;
                }
                
                if (p_x[i] >= 9 && p_x[i] <= 11) {
                    if (b_y < p_gap[i] || b_y > p_gap[i] + current_gap) {
                        running = 0;
                    }
                }
            }
            
            if (b_y < 1) b_y = 1;
            if (b_y > 24) running = 0;

            if (b_y >= 1 && b_y <= 24) {
                kprint_at("O>", 10, b_y, 0x0E);
            }

            for (int i = 0; i < 2; i++) {
                if (p_x[i] >= 0 && p_x[i] < 80) {
                    for(int y=1; y<25; y++) {
                        if (y < p_gap[i] || y > p_gap[i] + current_gap) {
                            kprint_at("#", p_x[i], y, 0x02);
                        }
                    }
                }
            }

            old_b_y = b_y;
            for (int i = 0; i < 2; i++) {
                old_p_x[i] = p_x[i];
                old_p_gap[i] = p_gap[i];
            }

            kprint_at(" SCORE: ", 2, 0, 0x0F);
            kprint_int_at(score, 10, 0, 0x0B);
            kprint_at(" MODE: ", 15, 0, 0x0F);
            kprint_at(mode_name, 22, 0, mode_color);

            for (volatile int i = 0; i < current_delay; i++); 
        }

        kprint_at(" !!! GAME OVER !!! ", 31, 10, 0x4F);
        kprint_at(" Final Score: ", 33, 12, 0x0F);
        kprint_int_at(score, 47, 12, 0x0B);
        kprint_at(" [ENTER] - Try Again   [ESC] - Exit ", 24, 15, 0x0E);

        int decision = 0;
        while (!decision) {
            uint8_t f_key = inb(0x60);
            if (f_key == 0x1C) { 
                decision = 1; 
                restart_game = 1; 
            }
            if (f_key == 0x01) { 
                decision = 1; 
                restart_game = 0; 
            }
        }
    }
    clear_screen();
}

struct Button {
    int x, y, w, h;
    const char* text;
    uint8_t normal_color, hover_color;
    int id, last_hover;
};

static Button calc_buttons[16] = {
    {28,9,5,2,"7",0x70,0x2F,7,0}, {34,9,5,2,"8",0x70,0x2F,8,0},
    {40,9,5,2,"9",0x70,0x2F,9,0}, {46,9,5,2,"/",0x70,0x2F,10,0},
    {28,11,5,2,"4",0x70,0x2F,4,0}, {34,11,5,2,"5",0x70,0x2F,5,0},
    {40,11,5,2,"6",0x70,0x2F,6,0}, {46,11,5,2,"*",0x70,0x2F,11,0},
    {28,13,5,2,"1",0x70,0x2F,1,0}, {34,13,5,2,"2",0x70,0x2F,2,0},
    {40,13,5,2,"3",0x70,0x2F,3,0}, {46,13,5,2,"-",0x70,0x2F,12,0},
    {28,15,5,2,"C",0x70,0x4F,13,0}, {34,15,5,2,"0",0x70,0x2F,0,0},
    {40,15,5,2,"=",0x70,0x2F,14,0}, {46,15,5,2,"+",0x70,0x2F,15,0}
};

static long calc_val1 = 0, calc_val2 = 0;
static char calc_op = 0;
static int calc_state = 0, calc_clicked = 0;
static long last_display_value = -1;

static void draw_display(long value) {
    if(value == last_display_value) return;
    for(int i = 0; i < 22; i++) put_block(29 + i, 8, 0x00, ' ');
    char num_str[12];
    long temp = value;
    int len = 0;
    if(temp == 0) { num_str[0] = '0'; len = 1; }
    else {
        while(temp > 0 && len < 11) {
            num_str[len++] = (temp % 10) + '0';
            temp /= 10;
        }
        for(int i = 0; i < len/2; i++) {
            char t = num_str[i];
            num_str[i] = num_str[len-1-i];
            num_str[len-1-i] = t;
        }
    }
    num_str[len] = '\0';
    kprint_at(num_str, 48 - len, 8, 0x0F);
    last_display_value = value;
}

static void draw_button(Button* btn, int hover) {
    if(btn->last_hover == hover) return;
    uint8_t color = hover ? btn->hover_color : btn->normal_color;
    draw_window(btn->x, btn->y, btn->w, btn->h, color);
    int tx = btn->x + (btn->w - 1)/2;
    int ty = btn->y + (btn->h - 1)/2;
    kprint_at(btn->text, tx, ty, (btn->text[0] >= '0' && btn->text[0] <= '9') ? 0x0F : 0x0E);
    if(!hover && btn->last_hover == -1) {
        put_block(btn->x + btn->w, btn->y + 1, 0x08, ' ');
        put_block(btn->x + 1, btn->y + btn->h, 0x08, ' ');
    }
    btn->last_hover = hover;
}

static void draw_background() {
    clear_text_graph(1);
    draw_window_with_shadow(25,5,30,15,7);
    kprint_at("+------------+", 32, 5, 0x70);
    kprint_at("| CALCULATOR |", 32, 6, 0x70);
    kprint_at("+------------+", 32, 7, 0x70);
    draw_window(28,7,24,3,0x70);
    draw_box(28,7,24,3,0x70);
    draw_window(29,8,22,1,0x00);
    last_display_value = -1;
    for(int i=0;i<16;i++) calc_buttons[i].last_hover = -1;
}

extern "C" void run_calc() {
    init_mouse();
    draw_background();
    calc_val1 = 0; calc_val2 = 0; calc_op = 0; calc_state = 0; calc_clicked = 0;
    uint16_t* vga = (uint16_t*)0xB8000;
    uint16_t saved = vga[mouse_y * 80 + mouse_x];
    int last_mouse_x = mouse_x, last_mouse_y = mouse_y;
    uint8_t last_mouse_btn = mouse_btn;
    int running = 1;
    
    while(running) {
        poll_mouse();
        
        if(mouse_x < 0) mouse_x = 0;
        if(mouse_x > 79) mouse_x = 79;
        if(mouse_y < 0) mouse_y = 0;
        if(mouse_y > 24) mouse_y = 24;
        
        int changed = (mouse_x != last_mouse_x || mouse_y != last_mouse_y || mouse_btn != last_mouse_btn);
        
        if(changed) {
            for(int i=0;i<16;i++) {
                Button* btn = &calc_buttons[i];
                int hover = (mouse_x >= btn->x && mouse_x < btn->x + btn->w &&
                            mouse_y >= btn->y && mouse_y < btn->y + btn->h);
                draw_button(btn, hover);
                
                if(hover && (mouse_btn & 1) && !calc_clicked) {
                    if(btn->id >= 0 && btn->id <= 9) {
                        if(calc_state == 0) calc_val1 = calc_val1 * 10 + btn->id;
                        else calc_val2 = calc_val2 * 10 + btn->id;
                        draw_display(calc_state ? calc_val2 : calc_val1);
                    }
                    else if(btn->id == 10) { calc_op = '/'; calc_state = 1; }
                    else if(btn->id == 11) { calc_op = '*'; calc_state = 1; }
                    else if(btn->id == 12) { calc_op = '-'; calc_state = 1; }
                    else if(btn->id == 13) { 
                        calc_val1 = 0; calc_val2 = 0; calc_op = 0; calc_state = 0;
                        draw_display(0);
                    }
                    else if(btn->id == 14) {
                        if(calc_op) {
                            switch(calc_op) {
                                case '+': calc_val1 = calc_val1 + calc_val2; break;
                                case '-': calc_val1 = calc_val1 - calc_val2; break;
                                case '*': calc_val1 = calc_val1 * calc_val2; break;
                                case '/': 
                                    if(calc_val2 != 0) calc_val1 = calc_val1 / calc_val2;
                                    else calc_val1 = 999999;
                                    break;
                            }
                            calc_val2 = 0; calc_op = 0; calc_state = 0;
                            draw_display(calc_val1);
                        }
                    }
                    else if(btn->id == 15) { calc_op = '+'; calc_state = 1; }
                    calc_clicked = 1;
                }
            }
            
            if(mouse_x != last_mouse_x || mouse_y != last_mouse_y) {
                if(last_mouse_x >= 0 && last_mouse_y >= 0) {
                    vga[last_mouse_y * 80 + last_mouse_x] = saved;
                }
                saved = vga[mouse_y * 80 + mouse_x];
                vga[mouse_y * 80 + mouse_x] = 0x7058;
                last_mouse_x = mouse_x;
                last_mouse_y = mouse_y;
            }
            last_mouse_btn = mouse_btn;
        }
        
        if(!(mouse_btn & 1)) calc_clicked = 0;
        move_cursor(79,24);
        
        if((inb(0x64)&1) && !(inb(0x64)&0x20)) {
            if(inb(0x60) == 0x01) running = 0;
        }
        
        for(volatile int i=0;i<5000;i++);
    }
    
    disable_mouse();
    clear_screen();
}
extern "C" void edcode_repl();
extern "C" void edcode_run_file(const char* filename);
extern "C" void show_gui() {
    init_mouse();
    clear_text_graph(1);
    draw_window_with_shadow(20,7,40,10,7);
    kprint_at("+----------------------+", 25, 7, 0x70);
    kprint_at("|   GUI TEST WINDOW    |", 25, 8, 0x70);
    kprint_at("+----------------------+", 25, 9, 0x70);
    kprint_at("This is a test window", 25, 11, 0x0F);
    kprint_at("with mouse support!", 28, 12, 0x0F);
    
    int btn_x=35, btn_y=13, btn_w=10, btn_h=2, btn_hover=0, btn_clicked=0;
    uint16_t* vga=(uint16_t*)0xB8000;
    uint16_t saved = vga[mouse_y*80+mouse_x];
    int last_mouse_x=mouse_x, last_mouse_y=mouse_y;
    int running=1;
    
    while(running) {
        poll_mouse();
        
        if(mouse_x < 0) mouse_x = 0;
        if(mouse_x > 79) mouse_x = 79;
        if(mouse_y < 0) mouse_y = 0;
        if(mouse_y > 24) mouse_y = 24;
        
        int new_hover = (mouse_x>=btn_x && mouse_x<btn_x+btn_w && mouse_y>=btn_y && mouse_y<btn_y+btn_h);
        if(new_hover && (mouse_btn&1) && !btn_clicked) { running=0; btn_clicked=1; }
        if(!(mouse_btn&1)) btn_clicked=0;
        
        if(new_hover != btn_hover) {
            uint8_t col = new_hover ? 0x2F : 0x4F;
            draw_window(btn_x,btn_y,btn_w,btn_h,col);
            put_block(btn_x,btn_y,col,'['); put_block(btn_x+btn_w-1,btn_y,col,']');
            put_block(btn_x,btn_y+btn_h-1,col,'['); put_block(btn_x+btn_w-1,btn_y+btn_h-1,col,']');
            kprint_at(" EXIT ", btn_x+2, btn_y+(btn_h-1)/2, 0x0F);
            btn_hover = new_hover;
        }
        
        if(mouse_x!=last_mouse_x || mouse_y!=last_mouse_y) {
            if(last_mouse_x>=0 && last_mouse_y>=0) {
                vga[last_mouse_y*80+last_mouse_x] = saved;
            }
            saved = vga[mouse_y*80+mouse_x];
            vga[mouse_y*80+mouse_x] = 0x7058;
            last_mouse_x=mouse_x; last_mouse_y=mouse_y;
        }
        
        move_cursor(79,24);
        if((inb(0x64)&1) && !(inb(0x64)&0x20) && inb(0x60)==0x01) running=0;
        for(volatile int i=0;i<5000;i++);
    }
    
    disable_mouse();
    clear_screen();
}

#define CLIPBOARD_SIZE 1024
static char clipboard[CLIPBOARD_SIZE] = {0};
static char paste_buffer[CLIPBOARD_SIZE] = {0};

void clipboard_copy(const char* text, int execute) {
    if(text[0] == '\0') {
        kprint("Nothing to copy\n");
        return;
    }
    
    int i;
    for(i = 0; i < CLIPBOARD_SIZE-1 && text[i]; i++) {
        clipboard[i] = text[i];
    }
    clipboard[i] = '\0';
    
    kprint("✓ Copied: \"");
    kprint(clipboard);
    kprint("\"");
    if(execute) kprint(" [auto-execute]");
    kprint("\n");
}

void clipboard_paste(int execute) {
    if(!clipboard[0]) {
        kprint("Clipboard empty\n");
        return;
    }
    
    int i;
    for(i = 0; i < CLIPBOARD_SIZE-1 && clipboard[i]; i++) {
        paste_buffer[i] = clipboard[i];
    }
    paste_buffer[i] = '\0';
    
    kprint("Pasted: ");
    kprint(paste_buffer);
    if(execute) kprint(" [auto-execute]");
    kprint("\n");
}
typedef struct {
    char     magic[4];
    uint32_t sample_rate;
    uint32_t samples;
    uint16_t channels;
    uint16_t delay;
} wka_header_t;

void play_wka(const char* filename) {
    wka_header_t header;
    
    if(wnkfs_read_file(filename, (uint8_t*)&header, sizeof(header)) <= 0) {
        kprint("File not found\n");
        return;
    }
    
    if(header.magic[0] != 'W' || header.magic[1] != 'K' || 
       header.magic[2] != 'A' || header.magic[3] != '1') {
        kprint("Not a WKA file\n");
        return;
    }
    
    kprint("Playing: "); kprint(filename); kprint("\n");
    
    uint8_t buffer[512];
    uint32_t played = 0;
    uint32_t total = header.samples;
    
    kprint("[");
    
    while(played < total) {
        uint32_t to_read = 512;
        if(to_read > total - played) to_read = total - played;
        
        int read = wnkfs_read_file(filename, buffer + played, to_read);
        if(read <= 0) break;
        
        for(int i = 0; i < read; i++) {
            uint32_t freq = 200 + (buffer[i] * 2);
            play_sound(freq);
            for(volatile int d = 0; d < header.delay * 100; d++);
        }
        
        played += read;
        
        int percent = (played * 20) / total;
        static int last_percent = -1;
        if(percent != last_percent) {
            kprint("#");
            last_percent = percent;
        }
    }
    
    kprint("]\nDone!\n");
    nosound();
}

void draw_ascii_clock(int hour, int minute, int second) {
    clear_screen();
    
    const char* digits[10][5] = {
        {" 000 ", "0   0", "0   0", "0   0", " 000 "}, 
        {"  1  ", " 11  ", "  1  ", "  1  ", " 111 "}, 
        {" 222 ", "    2", " 222 ", "2    ", " 222 "}, 
        {" 333 ", "    3", " 333 ", "    3", " 333 "}, 
        {"4   4", "4   4", " 444 ", "    4", "    4"}, 
        {" 555 ", "5    ", " 555 ", "    5", " 555 "}, 
        {" 666 ", "6    ", " 666 ", "6   6", " 666 "}, 
        {" 777 ", "    7", "    7", "    7", "    7"}, 
        {" 888 ", "8   8", " 888 ", "8   8", " 888 "}, 
        {" 999 ", "9   9", " 999 ", "    9", " 999 "} 
    };
    
    int h1 = hour / 10;
    int h2 = hour % 10;
    int m1 = minute / 10;
    int m2 = minute % 10;
    int s1 = second / 10;
    int s2 = second % 10;
    
    for(int row = 0; row < 5; row++) {

        kprint_at(digits[h1][row], 20, 5 + row, 0x0E);
        kprint_at(digits[h2][row], 26, 5 + row, 0x0E);

        if(row == 1 || row == 3) {
            kprint_at(":", 32, 5 + row, 0x0F);
        } else {
            kprint_at(" ", 32, 5 + row, 0x0F);
        }
        
        kprint_at(digits[m1][row], 34, 5 + row, 0x0A);
        kprint_at(digits[m2][row], 40, 5 + row, 0x0A);
        
        if(row == 1 || row == 3) {
            kprint_at(":", 46, 5 + row, 0x0F);
        } else {
            kprint_at(" ", 46, 5 + row, 0x0F);
        }

        kprint_at(digits[s1][row], 48, 5 + row, 0x0B);
        kprint_at(digits[s2][row], 54, 5 + row, 0x0B);
    }
    
    kprint_at("================================", 20, 12, 0x0F);
    uint8_t day, month, year;
    __asm__ volatile (
        "mov $0x04, %%ah\n"
        "int $0x1A\n"
        : "=c"(day), "=d"(month), "=b"(year)
        :
        : "ah"
    );
    
    day = ((day >> 4) * 10) + (day & 0x0F);
    month = ((month >> 4) * 10) + (month & 0x0F);
    year = ((year >> 4) * 10) + (year & 0x0F);
    
    char date_str[20];
    int pos = 0;
    date_str[pos++] = (day / 10) + '0';
    date_str[pos++] = (day % 10) + '0';
    date_str[pos++] = '/';
    date_str[pos++] = (month / 10) + '0';
    date_str[pos++] = (month % 10) + '0';
    date_str[pos++] = '/';
    date_str[pos++] = (year / 10) + '0';
    date_str[pos++] = (year % 10) + '0';
    date_str[pos] = '\0';
    
    kprint_at("Date: ", 30, 13, 0x0F);
    kprint_at(date_str, 36, 13, 0x0E);
    
    kprint_at("Press any key to exit", 25, 20, 0x07);
}
extern Time get_time(void);
extern void print_time(Time t);
void ascii_clock(void) {
    clear_screen();
    
    int running = 1;
    int last_second = -1;
    
    kprint_at("ASCII CLOCK - Press ESC to exit", 20, 0, 0x0F);
    
    while(running) {
        Time t = get_time();
        if(t.second != last_second) {
            last_second = t.second;
            
            char time_str[9];
            time_str[0] = (t.hour / 10) + '0';
            time_str[1] = (t.hour % 10) + '0';
            time_str[2] = ':';
            time_str[3] = (t.minute / 10) + '0';
            time_str[4] = (t.minute % 10) + '0';
            time_str[5] = ':';
            time_str[6] = (t.second / 10) + '0';
            time_str[7] = (t.second % 10) + '0';
            time_str[8] = '\0';
            for(int y = 10; y < 15; y++) {
                for(int x = 30; x < 50; x++) {
                    put_pixel(x, y, BLACK, TXT_BLACK, ' ');
                }
            }
            kprint_at(time_str, 35, 12, 0x0F);
            uint8_t day, month;
            uint16_t year;
            get_date(&day, &month, &year);
            
            char date_str[11];
            date_str[0] = (day / 10) + '0';
            date_str[1] = (day % 10) + '0';
            date_str[2] = '.';
            date_str[3] = (month / 10) + '0';
            date_str[4] = (month % 10) + '0';
            date_str[5] = '.';
            date_str[6] = (year / 1000) + '0';
            date_str[7] = ((year / 100) % 10) + '0';
            date_str[8] = ((year / 10) % 10) + '0';
            date_str[9] = (year % 10) + '0';
            date_str[10] = '\0';
            
            kprint_at(date_str, 35, 14, 0x0A);
        }
        if(inb(0x64) & 1) {
            uint8_t key = inb(0x60);
            if(key == 0x01) {
                running = 0;
            }
            while(inb(0x64) & 1) inb(0x60);
        }
        
        for(volatile int i = 0; i < 50000; i++);
    }
    
    clear_screen();
}
#define SNAKE_MAX 100
#define SNAKE_UP 0
#define SNAKE_DOWN 1
#define SNAKE_LEFT 2
#define SNAKE_RIGHT 3

struct SnakeSegment {
    int x, y;
};

static SnakeSegment snake[SNAKE_MAX];
static int snake_length;
static int snake_dir;
static int food_x, food_y;
static int snake_score;
static int snake_speed;

static void snake_init() {
    snake_length = 3;
    snake_dir = SNAKE_RIGHT;
    snake[0].x = 10; snake[0].y = 10;
    snake[1].x = 9; snake[1].y = 10;
    snake[2].x = 8; snake[2].y = 10;
    
    food_x = 15 + (my_rand() % 50);
    food_y = 5 + (my_rand() % 15);
    snake_score = 0;
    snake_speed = 5000000;
}

static void snake_draw() {
    clear_screen();
    
    for(int i = 5; i < 75; i++) {
        put_pixel(i, 3, BLACK, TXT_GREEN, '#');
        put_pixel(i, 20, BLACK, TXT_GREEN, '#');
    }
    for(int i = 3; i < 21; i++) {
        put_pixel(5, i, BLACK, TXT_GREEN, '#');
        put_pixel(74, i, BLACK, TXT_GREEN, '#');
    }
    
    for(int i = 0; i < snake_length; i++) {
        uint8_t color = (i == 0) ? 0x0E : 0x0A;
        put_pixel(snake[i].x, snake[i].y, BLACK, color, 'O');
    }
    
    put_pixel(food_x, food_y, BLACK, TXT_RED, '@');
    
    kprint_at("SCORE: ", 2, 0, 0x0F);
    kprint_int_at(snake_score, 9, 0, 0x0A);
    kprint_at("LENGTH: ", 15, 0, 0x0F);
    kprint_int_at(snake_length, 23, 0, 0x0A);
}

static int snake_move() {
    for(int i = snake_length - 1; i > 0; i--) {
        snake[i] = snake[i-1];
    }
    
    switch(snake_dir) {
        case SNAKE_UP:    snake[0].y--; break;
        case SNAKE_DOWN:  snake[0].y++; break;
        case SNAKE_LEFT:  snake[0].x--; break;
        case SNAKE_RIGHT: snake[0].x++; break;
    }
    
    if(snake[0].x <= 5 || snake[0].x >= 74 || 
       snake[0].y <= 3 || snake[0].y >= 20) {
        return 0;
    }

    for(int i = 1; i < snake_length; i++) {
        if(snake[0].x == snake[i].x && snake[0].y == snake[i].y) {
            return 0;
        }
    }
    
    if(snake[0].x == food_x && snake[0].y == food_y) {
        snake_length++;
        snake_score += 10;
        food_x = 6 + (my_rand() % 67);
        food_y = 4 + (my_rand() % 15);
        if(snake_speed > 500000) snake_speed -= 900000;
    }
    
    return 1;
}

void snake_game() {
    snake_init();
    int running = 1;
    int last_dir = snake_dir;
    
    while(running) {
        snake_draw();
        if(inb(0x64) & 1) {
            uint8_t key = inb(0x60);
            if(key < 0x80) {
                if(key == 0x48 && last_dir != SNAKE_DOWN) snake_dir = SNAKE_UP;
                else if(key == 0x50 && last_dir != SNAKE_UP) snake_dir = SNAKE_DOWN;
                else if(key == 0x4B && last_dir != SNAKE_RIGHT) snake_dir = SNAKE_LEFT;
                else if(key == 0x4D && last_dir != SNAKE_LEFT) snake_dir = SNAKE_RIGHT;
                else if(key == 0x01) running = 0;
            }
            while(inb(0x64) & 1) inb(0x60);
            last_dir = snake_dir;
        }
        if(!snake_move()) {
            kprint_at("=== GAME OVER ===", 30, 12, 0x4F);
            kprint_at("Final Score: ", 30, 14, 0x0F);
            kprint_int_at(snake_score, 43, 14, 0x0B);
            kprint_at("Press any key...", 30, 16, 0x07);
            while(!(inb(0x64) & 1));
            while(inb(0x64) & 1) inb(0x60);
            running = 0;
        }
        
        for(volatile int i = 0; i < snake_speed; i++);
    }
    clear_screen();
}
void ascii_rain() {
    clear_screen();
    int drops[80] = {0};
    int symbols[80] = {0};
    int running = 1;
    
    kprint_at("ASCII RAIN - Press ESC to exit", 20, 0, 0x0F);
    
    while(running) {
        for(int i = 0; i < 80; i++) {
            if(drops[i] > 0) {
                put_pixel(i, drops[i], BLACK, TXT_BLACK, ' ');
                drops[i]++;
                if(drops[i] >= 24) {
                    drops[i] = 0;
                } else {
                    char c = symbols[i];
                    if(c == 0) c = '|';
                    uint8_t color = 0x09 + (drops[i] % 7);
                    put_pixel(i, drops[i], BLACK, color, c);
                }
            } else {
                if(my_rand() % 10 == 0) {
                    drops[i] = 1;
                    int sym = my_rand() % 5;
                    if(sym == 0) symbols[i] = '|';
                    else if(sym == 1) symbols[i] = '/';
                    else if(sym == 2) symbols[i] = '\\';
                    else if(sym == 3) symbols[i] = '*';
                    else symbols[i] = '.';
                }
            }
        }
        if(inb(0x64) & 1) {
            uint8_t key = inb(0x60);
            if(key < 0x80 && key == 0x01) running = 0;
            while(inb(0x64) & 1) inb(0x60);
        }
        for(volatile int i = 0; i < 6000000; i++);
    }
    clear_screen();
}
static unsigned int rand_seed = 1;

static int simple_rand() {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (unsigned int)(rand_seed / 65536) % 32768;
}

static void simple_srand(unsigned int seed) {
    rand_seed = seed;
}
void ascii_fire() {
    clear_screen();
    int fire[80][25] = {0};
    int running = 1;
    int frame = 0;
    simple_srand(12345);
    
    kprint_at("ASCII FIRE - Press ESC to exit", 20, 0, 0x0F);
    
    while(running) {
        for(int x = 0; x < 80; x++) {
            if(frame % 5 == 0) {
                fire[x][24] = 8 + (simple_rand() % 8);
            }
            
            for(int y = 23; y >= 0; y--) {
                int sum = 0;
                int count = 0;
                
                if(y < 24) {
                    sum += fire[x][y+1];
                    count++;
                }
                if(x > 0 && y < 24) {
                    sum += fire[x-1][y+1];
                    count++;
                }
                if(x < 79 && y < 24) {
                    sum += fire[x+1][y+1];
                    count++;
                }
                
                if(count > 0) {
                    fire[x][y] = sum / count;
                    if(fire[x][y] > 0) fire[x][y]--;
                }
            }
        }
        
        for(int y = 0; y < 25; y++) {
            for(int x = 0; x < 80; x++) {
                int val = fire[x][y];
                char c = ' ';
                uint8_t color = 0x00;
                
                if(val > 12) { c = '#'; color = 0x0F; }
                else if(val > 8) { c = '#'; color = 0x0E; }
                else if(val > 4) { c = '#'; color = 0x0C; }
                else if(val > 2) { c = '.'; color = 0x06; }
                else if(val > 0) { c = '.'; color = 0x08; }
                
                put_pixel(x, y, BLACK, color, c);
            }
        }
        
        if(inb(0x64) & 1) {
            uint8_t key = inb(0x60);
            if(key == 0x01) running = 0;
            while(inb(0x64) & 1) inb(0x60);
        }
        
        frame++;
        for(volatile int i = 0; i < 10000000; i++);
    }
    clear_screen();
}

static int min(int a, int b) {
    return a < b ? a : b;
}

static int max(int a, int b) {
    return a > b ? a : b;
}
void plasma_effect() {
    clear_screen();
    int running = 1;
    int time = 0;
    
    kprint_at("PLASMA - Press ESC to exit", 25, 0, 0x0F);
    
    while(running) {
        time++;
        
        for(int y = 1; y < 24; y++) {
            for(int x = 0; x < 80; x++) {
                int v1 = ((x + time) % 32) / 2;
                int v2 = ((y * 2 + time) % 32) / 2;
                int v3 = ((x + y + time) % 32) / 2;
                int val = (v1 + v2 + v3) / 3;
                
                char c = ' ';
                uint8_t color = 0;
                
                if(val > 12) { c = '#'; color = 0x0F; }
                else if(val > 10) { c = '#'; color = 0x0E; }
                else if(val > 8) { c = '#'; color = 0x0C; }
                else if(val > 6) { c = '#'; color = 0x0D; }
                else if(val > 4) { c = '#'; color = 0x0B; }
                else if(val > 2) { c = '#'; color = 0x0A; }
                else if(val > 1) { c = '.'; color = 0x09; }
                else { c = '.'; color = 0x08; }
                
                put_pixel(x, y, BLACK, color, c);
            }
        }
        
        if(inb(0x64) & 1) {
            uint8_t key = inb(0x60);
            if(key == 0x01) running = 0;
            while(inb(0x64) & 1) inb(0x60);
        }
        
        for(volatile int i = 0; i < 5000; i++);
    }
    clear_screen();
}
void waves_effect() {
    clear_screen();
    int running = 1;
    int time = 0;
    
    kprint_at("WAVES - Press ESC to exit", 25, 0, 0x0F);
    
    while(running) {
        time++;
        
        for(int y = 1; y < 24; y++) {
            for(int x = 0; x < 80; x++) {
                int val = ((x * 3 + time) % 16 + (y * 5 + time) % 16) / 2;
                
                char c = ' ';
                uint8_t color = 0;
                
                if(val > 12) { c = '#'; color = 0x0B; }
                else if(val > 9) { c = '#'; color = 0x0E; }
                else if(val > 6) { c = '#'; color = 0x0C; }
                else if(val > 3) { c = '#'; color = 0x0A; }
                else if(val > 1) { c = '.'; color = 0x09; }
                else { c = ' '; color = 0x00; }
                
                put_pixel(x, y, BLACK, color, c);
            }
        }
        
        if(inb(0x64) & 1) {
            uint8_t key = inb(0x60);
            if(key == 0x01) running = 0;
            while(inb(0x64) & 1) inb(0x60);
        }
        
        for(volatile int i = 0; i < 5000; i++);
    }
    clear_screen();
}
void tunnel_effect() {
    clear_screen();
    int running = 1;
    int time = 0;
    
    kprint_at("3D TUNNEL - Press ESC to exit", 25, 0, 0x0F);
    
    while(running) {
        time++;
        
        for(int y = 1; y < 24; y++) {
            for(int x = 0; x < 80; x++) {
                int dx = x - 40;
                int dy = y - 12;
                int dist = abs(dx) + abs(dy);
                int angle = (dx * 4 + dy * 2) / 8;
                int val = (dist / 3 + time) % 16;
                val = (val + angle) % 16;
                
                char c = ' ';
                uint8_t color = 0;
                
                if(val > 12) { c = '#'; color = 0x0F; }
                else if(val > 8) { c = '#'; color = 0x0E; }
                else if(val > 4) { c = '#'; color = 0x0C; }
                else if(val > 2) { c = '.'; color = 0x0B; }
                else { c = '.'; color = 0x08; }
                
                put_pixel(x, y, BLACK, color, c);
            }
        }
        
        if(inb(0x64) & 1) {
            uint8_t key = inb(0x60);
            if(key == 0x01) running = 0;
            while(inb(0x64) & 1) inb(0x60);
        }
        
        for(volatile int i = 0; i < 5000; i++);
    }
    clear_screen();
}
#define KEY_STORAGE_SECTOR 2049

static int system_activated = 0;
static char saved_key[16] = {0};

static int validate_key_format(const char* key) {
    if(!key) return 0;
    
    int len = 0;
    while(key[len] && len < 16) len++;
    
    if(len != 13) return 0;
    
    for(int i = 0; i < len; i++) {
        if(i == 5 || i == 11) {
            if(key[i] != '-') return 0;
        } else {
            char c = key[i];
            if(!((c >= '0' && c <= '9') || 
                 (c >= 'A' && c <= 'F') || 
                 (c >= 'a' && c <= 'f'))) return 0;
        }
    }
    return 1;
}

static int verify_product_key(const char* key) {
    if(!validate_key_format(key)) return 0;

    int sum = 0;
    for(int i = 0; i < 13; i++) {
        char c = key[i];
        if(c == '-') continue;
        
        int val;
        if(c >= '0' && c <= '9') val = c - '0';
        else if(c >= 'A' && c <= 'F') val = 10 + (c - 'A');
        else if(c >= 'a' && c <= 'f') val = 10 + (c - 'a');
        else continue;
        
        sum += val;
    }
    
    char last = key[12];
    int last_val;
    if(last >= '0' && last <= '9') last_val = last - '0';
    else if(last >= 'A' && last <= 'F') last_val = 10 + (last - 'A');
    else if(last >= 'a' && last <= 'f') last_val = 10 + (last - 'a');
    else return 0;
    
    int expected = sum % 16;
    
    return (last_val == expected);
}

static void save_key_to_memory(const char* key) {
    my_strcpy(saved_key, key);
    system_activated = 1;
    kprint_color("✓ System activated!\n", 0x0A);
}

static int is_activated(void) {
    return system_activated;
}

static void load_key_from_disk(void) {
    uint16_t buffer[256];
    read_sector(KEY_STORAGE_SECTOR, buffer);
    
    char disk_key[16] = {0};
    for(int i = 0; i < 15; i++) {
        disk_key[i] = buffer[i] & 0xFF;
    }
    
    if(verify_product_key(disk_key)) {
        save_key_to_memory(disk_key);
        kprint_color("✓ License key loaded from disk\n", 0x0A);
    }
}

static void save_key_to_disk(const char* key) {
    uint16_t buffer[256] = {0};
    for(int i = 0; key[i] && i < 15; i++) {
        buffer[i] = key[i];
    }
    write_sector(KEY_STORAGE_SECTOR, buffer);
    kprint_color("✓ License key saved to disk\n", 0x0A);
}
void halt_system(void) {
    clear_screen();
    kprint_color("            SYSTEM HALTED              \n", TXT_YELLOW);
    kprint_color("  It is now safe to turn off your      \n", TXT_YELLOW);
    kprint_color("  computer.                            \n", TXT_YELLOW);

    
    play_shutdown_sound();

    kprint("\n[!] If system doesn't power off, press power button.\n");

    __asm__ volatile("cli");
    
    while(1) {
        __asm__ volatile("hlt");
    }
}

#define MAX_UI_WIDGETS 256
#define MAX_UI_NAME 32
#define MAX_UI_TEXT 128

typedef enum {
    UI_WINDOW = 0,
    UI_BUTTON,
    UI_LABEL,
    UI_TEXTBOX,
    UI_CHECKBOX,
    UI_PROGRESS,
    UI_SLIDER,
    UI_PANEL,
    UI_LISTBOX
} UIWidgetType;

typedef enum {
    ACTION_NONE = 0,
    ACTION_REBOOT,
    ACTION_SHUTDOWN,
    ACTION_BEEP,
    ACTION_CLEAR_SCREEN,
    ACTION_SHOW_MESSAGE,
    ACTION_EXIT_APP,
    ACTION_SAVE_FILE,
    ACTION_DELETE_FILE,
    ACTION_OPEN_FILE,
    ACTION_COPY,
    ACTION_PASTE,
    ACTION_HELP
} UIActionType;

typedef struct {
    int id;
    UIWidgetType type;
    char name[MAX_UI_NAME];
    char text[MAX_UI_TEXT];
    int x, y, w, h;
    uint8_t bg_color;
    uint8_t fg_color;
    uint8_t border_color;
    int visible;
    int enabled;
    int value;
    int min_value;
    int max_value;
    int checked;
    UIActionType action;
    char tooltip[MAX_UI_TEXT];
    int parent_id;
    int fill_type;
    char fill_char;
} UIWidget;

static uint16_t ui_buffer[80 * 25];
static char clipboard_ui[256] = {0};

static const char* action_names[] = {
    "0:None", "1:Reboot", "2:Shutdown", "3:Beep", "4:Clear",
    "5:Message", "6:Exit", "7:Save", "8:Delete", "9:Open",
    "10:Copy", "11:Paste", "12:Help"
};
#define ACTION_COUNT 13

static const char* type_names[] = {
    "Window", "Button", "Label", "TextBox", "CheckBox",
    "Progress", "Slider", "Panel", "ListBox"
};

static void ui_put_pixel(int x, int y, uint8_t bg, uint8_t fg, uint8_t ch) {
    if(x < 0 || x >= 80 || y < 0 || y >= 25) return;
    ui_buffer[y * 80 + x] = (uint16_t)((bg << 12) | (fg << 8) | ch);
}

static void ui_hline(int x, int y, int len, uint8_t bg, uint8_t fg, uint8_t ch) {
    for(int i = 0; i < len; i++) ui_put_pixel(x + i, y, bg, fg, ch);
}

static void ui_vline(int x, int y, int len, uint8_t bg, uint8_t fg, uint8_t ch) {
    for(int i = 0; i < len; i++) ui_put_pixel(x, y + i, bg, fg, ch);
}

static void ui_frame(int x, int y, int w, int h, uint8_t bg, uint8_t fg) {
    if(w < 2 || h < 2) return;
    ui_hline(x + 1, y, w - 2, bg, fg, S_HLINE);
    ui_hline(x + 1, y + h - 1, w - 2, bg, fg, S_HLINE);
    ui_vline(x, y + 1, h - 2, bg, fg, S_VLINE);
    ui_vline(x + w - 1, y + 1, h - 2, bg, fg, S_VLINE);
    ui_put_pixel(x, y, bg, fg, S_TL);
    ui_put_pixel(x + w - 1, y, bg, fg, S_TR);
    ui_put_pixel(x, y + h - 1, bg, fg, S_BL);
    ui_put_pixel(x + w - 1, y + h - 1, bg, fg, S_BR);
}

static void ui_shadow_window(int x, int y, int w, int h, uint8_t bg, uint8_t fg, const char* title) {
    for(int i = 1; i < h; i++) ui_put_pixel(x + w, y + i + 1, BLACK, TXT_BLACK, ' ');
    for(int i = 1; i <= w; i++) ui_put_pixel(x + i, y + h, BLACK, TXT_BLACK, ' ');
    
    ui_frame(x, y, w, h, bg, fg);
    ui_hline(x + 1, y, w - 2, bg, fg, S_HLINE);
    
    for(int i = 0; title[i]; i++) ui_put_pixel(x + 2 + i, y, bg, TXT_YELLOW, title[i]);
    
    for(int i = y + 1; i < y + h - 1; i++) {
        for(int j = x + 1; j < x + w - 1; j++) {
            ui_put_pixel(j, i, bg, fg, ' ');
        }
    }
}

static void ui_kprint(const char* str, int x, int y, uint8_t color) {
    uint8_t bg = (color >> 4) & 0x0F;
    uint8_t fg = color & 0x0F;
    for(int i = 0; str[i]; i++) {
        if(x + i >= 0 && x + i < 80 && y >= 0 && y < 25) {
            ui_buffer[y * 80 + x + i] = (uint16_t)((bg << 12) | (fg << 8) | str[i]);
        }
    }
}

static void ui_fill_rect(int x, int y, int w, int h, uint8_t bg, uint8_t fg, int fill_type, char fill_char) {
    if(fill_type == 0) return;
    
    char ch = (fill_char != 0) ? fill_char : ' ';
    if(fill_type == 1) ch = ' ';
    
    for(int i = 0; i < h; i++) {
        for(int j = 0; j < w; j++) {
            if(fill_type == 2) {
                ch = ((i + j) % 2 == 0) ? BLOCK : ' ';
            }
            ui_put_pixel(x + j, y + i, bg, fg, ch);
        }
    }
}

static void ui_button_draw(int x, int y, int w, int h, const char* text, uint8_t bg, uint8_t fg) {
    ui_frame(x, y, w, h, bg, fg);
    int tx = x + (w - my_strlen(text)) / 2;
    int ty = y + (h - 1) / 2;
    for(int i = 0; text[i]; i++) {
        if(tx + i >= 0 && tx + i < 80 && ty >= 0 && ty < 25) {
            ui_buffer[ty * 80 + tx + i] = (uint16_t)((bg << 12) | (fg << 8) | text[i]);
        }
    }
}

static void ui_flip(void) {
    volatile uint16_t* video = (volatile uint16_t*)0xB8000;
    for(int i = 0; i < 80 * 25; i++) video[i] = ui_buffer[i];
}

static void int_to_str(int num, char* str) {
    if(num == 0) { str[0] = '0'; str[1] = '\0'; return; }
    char temp[16];
    int i = 0, n = num;
    while(n > 0) { temp[i++] = (n % 10) + '0'; n /= 10; }
    for(int j = 0; j < i; j++) str[j] = temp[i - j - 1];
    str[i] = '\0';
}

static int my_strcpy_len(char* dest, const char* src) {
    int len = 0;
    while(*src) {
        *dest++ = *src++;
        len++;
    }
    return len;
}

static void draw_button_direct(int x, int y, int w, int h, const char* text, uint8_t bg, uint8_t fg) {
    for(int i = 0; i < w; i++) {
        put_pixel(x + i, y, bg, fg, S_HLINE);
        put_pixel(x + i, y + h - 1, bg, fg, S_HLINE);
    }
    for(int i = 0; i < h; i++) {
        put_pixel(x, y + i, bg, fg, S_VLINE);
        put_pixel(x + w - 1, y + i, bg, fg, S_VLINE);
    }
    put_pixel(x, y, bg, fg, S_TL);
    put_pixel(x + w - 1, y, bg, fg, S_TR);
    put_pixel(x, y + h - 1, bg, fg, S_BL);
    put_pixel(x + w - 1, y + h - 1, bg, fg, S_BR);
    
    int tx = x + (w - my_strlen(text)) / 2;
    int ty = y + (h - 1) / 2;
    kprint_at(text, tx, ty, (bg << 4) | fg);
}

static void ui_show_notification(const char* text) {
    int len = my_strlen(text);
    int nx = 40 - len/2 - 2;
    int ny = 20;
    
    draw_shadow_window(nx, ny, len + 4, 3, YELLOW, TXT_BLACK, "Info");
    kprint_at(text, nx + 2, ny + 1, (YELLOW << 4) | TXT_BLACK);
    move_cursor(79, 24);
    
    for(volatile int d = 0; d < 3000000; d++);
}

void ui_designer(const char* filename) {
    static UIWidget widgets[MAX_UI_WIDGETS];
    static int widget_count = 0;
    static int selected_widget = -1;
    static UIWidgetType current_tool = UI_BUTTON;
    static int grid_size = 8;
    static int snap_to_grid = 1;
    static int show_grid = 1;
    static int show_properties = 1;
    static int show_toolbar = 1;
    static int show_statusbar = 1;
    static int running = 1;
    static int cursor_x = 40;
    static int cursor_y = 12;
    static int edit_mode = 0;
    static int edit_prop = 0;
    static int need_redraw = 1;
    static int test_mode = 0;
    static int action_menu = 0;
    static int action_selected = 0;
    static char size_buf[16];
    static int size_pos = 0;
    
    if(filename && filename[0]) {
        int fd = ramfs_open(filename, 0);
        if(fd >= 0) {
            ramfs_read(fd, &widget_count, 4);
            ramfs_read(fd, widgets, sizeof(UIWidget) * widget_count);
            ramfs_close(fd);
        }
    }

    if(widget_count == 0) {
        widgets[0].id = 0; widgets[0].type = UI_WINDOW;
        my_strcpy(widgets[0].name, "main"); my_strcpy(widgets[0].text, "Text Editor");
        widgets[0].x = 5; widgets[0].y = 3; widgets[0].w = 70; widgets[0].h = 18;
        widgets[0].bg_color = BLUE; widgets[0].fg_color = TXT_WHITE;
        widgets[0].border_color = TXT_WHITE;
        widgets[0].visible = 1; widgets[0].enabled = 1;
        widgets[0].fill_type = 0;
        
        widgets[1].id = 1; widgets[1].type = UI_LABEL;
        my_strcpy(widgets[1].name, "lbl_file"); my_strcpy(widgets[1].text, "untitled.txt");
        widgets[1].x = 7; widgets[1].y = 4; widgets[1].w = 20; widgets[1].h = 1;
        widgets[1].bg_color = BLUE; widgets[1].fg_color = TXT_CYAN;
        widgets[1].visible = 1; widgets[1].enabled = 1;
        widgets[1].fill_type = 0;
        
        widgets[2].id = 2; widgets[2].type = UI_TEXTBOX;
        my_strcpy(widgets[2].name, "txt_editor"); my_strcpy(widgets[2].text, "");
        widgets[2].x = 7; widgets[2].y = 5; widgets[2].w = 66; widgets[2].h = 12;
        widgets[2].bg_color = WHITE; widgets[2].fg_color = TXT_BLACK;
        widgets[2].border_color = TXT_BLACK;
        widgets[2].visible = 1; widgets[2].enabled = 1;
        widgets[2].fill_type = 1;
        
        widgets[3].id = 3; widgets[3].type = UI_BUTTON;
        my_strcpy(widgets[3].name, "btn_save"); my_strcpy(widgets[3].text, "Save");
        my_strcpy(widgets[3].tooltip, "Save file");
        widgets[3].x = 48; widgets[3].y = 18; widgets[3].w = 10; widgets[3].h = 2;
        widgets[3].bg_color = GRAY; widgets[3].fg_color = TXT_WHITE;
        widgets[3].visible = 1; widgets[3].enabled = 1;
        widgets[3].action = ACTION_SAVE_FILE;
        widgets[3].fill_type = 0;
        
        widgets[4].id = 4; widgets[4].type = UI_BUTTON;
        my_strcpy(widgets[4].name, "btn_exit"); my_strcpy(widgets[4].text, "Exit");
        my_strcpy(widgets[4].tooltip, "Exit editor");
        widgets[4].x = 60; widgets[4].y = 18; widgets[4].w = 10; widgets[4].h = 2;
        widgets[4].bg_color = GRAY; widgets[4].fg_color = TXT_WHITE;
        widgets[4].visible = 1; widgets[4].enabled = 1;
        widgets[4].action = ACTION_EXIT_APP;
        widgets[4].fill_type = 0;
        
        widget_count = 5;
    }
    
    while(running) {
        if(inb(0x64) & 1) {
            uint8_t key = inb(0x60);
            
            if(key < 0x80) {
                if(key == 0x01) {
                    if(edit_mode > 0) { edit_mode = 0; edit_prop = 0; size_pos = 0; need_redraw = 1; }
                    else if(action_menu) { action_menu = 0; need_redraw = 1; }
                    else if(test_mode) { test_mode = 0; need_redraw = 1; }
                    else { running = 0; }
                }
                
                else if(key == 0x3E && !test_mode && !action_menu && edit_mode == 0) {
                    char fname[64];
                    if(filename && filename[0]) {
                        my_strcpy(fname, filename);
                        int len = my_strlen(fname);
                        if(len > 3 && fname[len-3] == '.' && fname[len-2] == 'u' && fname[len-1] == 'i') {
                            fname[len-2] = 'c'; fname[len-1] = 'p'; fname[len] = 'p'; fname[len+1] = '\0';
                        } else {
                            my_strcpy(fname, "ui_export.cpp");
                        }
                    } else {
                        my_strcpy(fname, "ui_export.cpp");
                    }
                    
                    char code[8192];
                    int pos = 0;
                    
                    pos += my_strcpy_len(code + pos, "// Generated by UIDEV\n");
                    pos += my_strcpy_len(code + pos, "#include \"graph.h\"\n\n");
                    pos += my_strcpy_len(code + pos, "void create_ui(void) {\n");
                    pos += my_strcpy_len(code + pos, "    clear_screen_bg(BLACK);\n\n");
                    
                    for(int i = 0; i < widget_count; i++) {
                        UIWidget* w = &widgets[i];
                        if(!w->visible) continue;
                        
                        char x_str[8], y_str[8], w_str[8], h_str[8];
                        int_to_str(w->x, x_str);
                        int_to_str(w->y, y_str);
                        int_to_str(w->w, w_str);
                        int_to_str(w->h, h_str);
                        
                        if(w->type == UI_WINDOW) {
                            pos += my_strcpy_len(code + pos, "    draw_shadow_window(");
                            pos += my_strcpy_len(code + pos, x_str); pos += my_strcpy_len(code + pos, ", ");
                            pos += my_strcpy_len(code + pos, y_str); pos += my_strcpy_len(code + pos, ", ");
                            pos += my_strcpy_len(code + pos, w_str); pos += my_strcpy_len(code + pos, ", ");
                            pos += my_strcpy_len(code + pos, h_str); pos += my_strcpy_len(code + pos, ", BLUE, TXT_WHITE, \"");
                            pos += my_strcpy_len(code + pos, w->text);
                            pos += my_strcpy_len(code + pos, "\");\n");
                        }
                        else if(w->type == UI_BUTTON) {
                            pos += my_strcpy_len(code + pos, "    draw_button(");
                            pos += my_strcpy_len(code + pos, x_str); pos += my_strcpy_len(code + pos, ", ");
                            pos += my_strcpy_len(code + pos, y_str); pos += my_strcpy_len(code + pos, ", ");
                            pos += my_strcpy_len(code + pos, w_str); pos += my_strcpy_len(code + pos, ", ");
                            pos += my_strcpy_len(code + pos, h_str); pos += my_strcpy_len(code + pos, ", \"");
                            pos += my_strcpy_len(code + pos, w->text);
                            pos += my_strcpy_len(code + pos, "\", GRAY, TXT_WHITE);\n");
                        }
                        else if(w->type == UI_LABEL) {
                            pos += my_strcpy_len(code + pos, "    kprint_at(\"");
                            pos += my_strcpy_len(code + pos, w->text);
                            pos += my_strcpy_len(code + pos, "\", ");
                            pos += my_strcpy_len(code + pos, x_str); pos += my_strcpy_len(code + pos, ", ");
                            pos += my_strcpy_len(code + pos, y_str); pos += my_strcpy_len(code + pos, ", (BLUE << 4) | TXT_CYAN);\n");
                        }
                        else if(w->type == UI_TEXTBOX) {
                            pos += my_strcpy_len(code + pos, "    draw_frame(");
                            pos += my_strcpy_len(code + pos, x_str); pos += my_strcpy_len(code + pos, ", ");
                            pos += my_strcpy_len(code + pos, y_str); pos += my_strcpy_len(code + pos, ", ");
                            pos += my_strcpy_len(code + pos, w_str); pos += my_strcpy_len(code + pos, ", ");
                            pos += my_strcpy_len(code + pos, h_str); pos += my_strcpy_len(code + pos, ", WHITE, TXT_BLACK);\n");
                        }
                        else if(w->type == UI_CHECKBOX) {
                            pos += my_strcpy_len(code + pos, "    kprint_at(\"[ ]\", ");
                            pos += my_strcpy_len(code + pos, x_str); pos += my_strcpy_len(code + pos, ", ");
                            pos += my_strcpy_len(code + pos, y_str); pos += my_strcpy_len(code + pos, ", (BLUE << 4) | TXT_WHITE);\n");
                            pos += my_strcpy_len(code + pos, "    kprint_at(\"");
                            pos += my_strcpy_len(code + pos, w->text);
                            pos += my_strcpy_len(code + pos, "\", ");
                            pos += my_strcpy_len(code + pos, x_str); pos += my_strcpy_len(code + pos, "+4, ");
                            pos += my_strcpy_len(code + pos, y_str); pos += my_strcpy_len(code + pos, ", (BLUE << 4) | TXT_WHITE);\n");
                        }
                        else if(w->type == UI_PROGRESS) {
                            pos += my_strcpy_len(code + pos, "    draw_progress(");
                            pos += my_strcpy_len(code + pos, x_str); pos += my_strcpy_len(code + pos, ", ");
                            pos += my_strcpy_len(code + pos, y_str); pos += my_strcpy_len(code + pos, ", ");
                            pos += my_strcpy_len(code + pos, w_str); pos += my_strcpy_len(code + pos, ", 50, BLUE, TXT_GREEN);\n");
                        }
                        else if(w->type == UI_PANEL) {
                            pos += my_strcpy_len(code + pos, "    draw_frame(");
                            pos += my_strcpy_len(code + pos, x_str); pos += my_strcpy_len(code + pos, ", ");
                            pos += my_strcpy_len(code + pos, y_str); pos += my_strcpy_len(code + pos, ", ");
                            pos += my_strcpy_len(code + pos, w_str); pos += my_strcpy_len(code + pos, ", ");
                            pos += my_strcpy_len(code + pos, h_str); pos += my_strcpy_len(code + pos, ", BLUE, TXT_WHITE);\n");
                        }
                        code[pos++] = '\n';
                    }
                    
                    pos += my_strcpy_len(code + pos, "}\n");
                    
                    int fd = ramfs_open(fname, 1);
                    if(fd >= 0) {
                        ramfs_write(fd, code, pos);
                        ramfs_close(fd);
                    }
                    
                    char msg[64] = "Exported to ";
                    my_strcpy(msg + 12, fname);
                    ui_show_notification(msg);
                    need_redraw = 1;
                }
                
                else if(key == 0x3F) {
                    test_mode = !test_mode;
                    edit_mode = 0;
                    action_menu = 0;
                    need_redraw = 1;
                }
                
                else if(key == 0x3C && !test_mode && !action_menu && edit_mode == 0) {
                    const char* fname = filename ? filename : "untitled.ui";
                    int fd = ramfs_open(fname, 1);
                    if(fd >= 0) { 
                        ramfs_write(fd, &widget_count, 4); 
                        ramfs_write(fd, widgets, sizeof(UIWidget) * widget_count); 
                        ramfs_close(fd);
                        ui_show_notification("Saved!");
                    }
                    need_redraw = 1;
                }
                
                else if(key == 0x3D && !test_mode && !action_menu && edit_mode == 0) {
                    const char* fname = filename ? filename : "untitled.ui";
                    int fd = ramfs_open(fname, 0);
                    if(fd >= 0) { 
                        ramfs_read(fd, &widget_count, 4); 
                        ramfs_read(fd, widgets, sizeof(UIWidget) * widget_count); 
                        ramfs_close(fd); 
                        selected_widget = -1;
                        ui_show_notification("Loaded!");
                        need_redraw = 1; 
                    }
                }
                
                else if(test_mode) {
                    int step = 1;
                    if(key == 0x11) { if(cursor_y > 2) cursor_y -= step; need_redraw = 1; }
                    else if(key == 0x1F) { if(cursor_y < 22) cursor_y += step; need_redraw = 1; }
                    else if(key == 0x1E) { if(cursor_x > 0) cursor_x -= step; need_redraw = 1; }
                    else if(key == 0x20) { if(cursor_x < 79) cursor_x += step; need_redraw = 1; }
                    else if(key == 0x39) {
                        for(int i = 0; i < widget_count; i++) {
                            UIWidget* w = &widgets[i];
                            if(!w->visible || !w->enabled) continue;
                            if(w->type != UI_BUTTON) continue;
                            
                            if(cursor_x >= w->x && cursor_x < w->x + w->w &&
                               cursor_y >= w->y && cursor_y < w->y + w->h) {
                                
                                if(w->action == ACTION_REBOOT) {
                                    clear_screen(); kprint("Rebooting...\n"); outb(0x64, 0xFE);
                                }
                                else if(w->action == ACTION_SHUTDOWN) {
                                    clear_screen(); kprint("Shutting down...\n"); while(1) { __asm__ volatile("hlt"); }
                                }
                                else if(w->action == ACTION_BEEP) {
                                    play_sound(800); for(volatile int d = 0; d < 100000; d++); nosound();
                                }
                                else if(w->action == ACTION_CLEAR_SCREEN) {
                                    clear_screen(); need_redraw = 1;
                                }
                                else if(w->action == ACTION_SHOW_MESSAGE) {
                                    const char* msg = w->tooltip[0] ? w->tooltip : w->text;
                                    if(msg[0] == '\0') msg = "Message";
                                    ui_show_notification(msg);
                                    need_redraw = 1;
                                }
                                else if(w->action == ACTION_EXIT_APP) {
                                    test_mode = 0; need_redraw = 1;
                                }
                                else if(w->action == ACTION_SAVE_FILE) {
                                    ui_show_notification("File saved!");
                                    need_redraw = 1;
                                }
                                else if(w->action == ACTION_COPY) {
                                    const char* txt = w->tooltip[0] ? w->tooltip : w->text;
                                    my_strcpy(clipboard_ui, txt);
                                    ui_show_notification("Copied!");
                                    need_redraw = 1;
                                }
                                else if(w->action == ACTION_PASTE) {
                                    if(clipboard_ui[0]) ui_show_notification(clipboard_ui);
                                    else ui_show_notification("Clipboard empty");
                                    need_redraw = 1;
                                }
                                else if(w->action == ACTION_HELP) {
                                    ui_show_notification("UIDEV v9.0 - UI Designer");
                                    need_redraw = 1;
                                }
                                break;
                            }
                        }
                    }
                }
                
                else if(action_menu) {
                    if(key == 0x11 && action_selected > 0) { action_selected--; need_redraw = 1; }
                    else if(key == 0x1F && action_selected < ACTION_COUNT - 1) { action_selected++; need_redraw = 1; }
                    else if(key == 0x1C) {
                        widgets[selected_widget].action = (UIActionType)action_selected;
                        action_menu = 0;
                        need_redraw = 1;
                    }
                }
                
                else if(edit_mode == 3) {
                    if(key == 0x1C) { edit_mode = 0; edit_prop = 0; need_redraw = 1; }
                    else if(key == 0x0E && selected_widget >= 0) {
                        UIWidget* w = &widgets[selected_widget];
                        char* str = (edit_prop == 1) ? w->name : ((edit_prop == 2) ? w->text : w->tooltip);
                        int len = my_strlen(str);
                        if(len > 0) { str[len-1] = '\0'; need_redraw = 1; }
                    }
                    else {
                        char ch = 0;
                        if(key >= 0x10 && key <= 0x19) ch = "qwertyuiop"[key - 0x10];
                        else if(key >= 0x1E && key <= 0x26) ch = "asdfghjkl"[key - 0x1E];
                        else if(key >= 0x2C && key <= 0x32) ch = "zxcvbnm"[key - 0x2C];
                        else if(key >= 0x02 && key <= 0x0B) ch = "1234567890"[key - 0x02];
                        else if(key == 0x39) ch = ' ';
                        else if(key == 0x0C) ch = '-';
                        else if(key == 0x34) ch = '.';
                        
                        if(ch && selected_widget >= 0) {
                            UIWidget* w = &widgets[selected_widget];
                            char* str = (edit_prop == 1) ? w->name : ((edit_prop == 2) ? w->text : w->tooltip);
                            int max_len = (edit_prop == 1) ? MAX_UI_NAME - 1 : MAX_UI_TEXT - 1;
                            int len = my_strlen(str);
                            if(len < max_len) { str[len] = ch; str[len+1] = '\0'; need_redraw = 1; }
                        }
                    }
                }
                
                else if(edit_mode == 5 && selected_widget >= 0) {
                    UIWidget* w = &widgets[selected_widget];
                    
                    if(key == 0x1C) {
                        int new_w = 0, new_h = 0;
                        int i = 0;
                        while(size_buf[i] && size_buf[i] != 'x' && size_buf[i] != 'X') {
                            if(size_buf[i] >= '0' && size_buf[i] <= '9') new_w = new_w * 10 + (size_buf[i] - '0');
                            i++;
                        }
                        if(size_buf[i] == 'x' || size_buf[i] == 'X') i++;
                        while(size_buf[i]) {
                            if(size_buf[i] >= '0' && size_buf[i] <= '9') new_h = new_h * 10 + (size_buf[i] - '0');
                            i++;
                        }
                        if(new_w >= 3 && new_w <= 79) w->w = new_w;
                        if(new_h >= 1 && new_h <= 22) w->h = new_h;
                        size_pos = 0; size_buf[0] = '\0';
                        edit_mode = 1;
                        need_redraw = 1;
                    }
                    else if(key == 0x01) { size_pos = 0; size_buf[0] = '\0'; edit_mode = 1; need_redraw = 1; }
                    else if(key == 0x0E && size_pos > 0) { size_pos--; size_buf[size_pos] = '\0'; need_redraw = 1; }
                    else {
                        char ch = 0;
                        if(key >= 0x02 && key <= 0x0B) ch = "1234567890"[key - 0x02];
                        else if(key == 0x2D) ch = 'x';
                        if(ch && size_pos < 15) { size_buf[size_pos++] = ch; size_buf[size_pos] = '\0'; need_redraw = 1; }
                    }
                }
                
                else if(edit_mode == 1 && selected_widget >= 0) {
                    UIWidget* w = &widgets[selected_widget];
                    int step = snap_to_grid ? grid_size : 1;
                    
                    if(key == 0x11) { w->y -= step; need_redraw = 1; }
                    else if(key == 0x1F) { w->y += step; need_redraw = 1; }
                    else if(key == 0x1E) { w->x -= step; need_redraw = 1; }
                    else if(key == 0x20) { w->x += step; need_redraw = 1; }
                    
                    else if(key == 0x10) { if(w->w > 3) w->w -= step; need_redraw = 1; }
                    else if(key == 0x12) { w->w += step; need_redraw = 1; }
                    else if(key == 0x13) { if(w->h > 1) w->h -= step; need_redraw = 1; }
                    else if(key == 0x21) { w->h += step; need_redraw = 1; }
                    
                    else if(key == 0x2E) { w->bg_color = (w->bg_color + 1) % 16; need_redraw = 1; }
                    else if(key == 0x2F) { w->bg_color = (w->bg_color + 15) % 16; need_redraw = 1; }
                    else if(key == 0x30) { w->fg_color = (w->fg_color + 1) % 16; need_redraw = 1; }
                    else if(key == 0x31) { w->fg_color = (w->fg_color + 15) % 16; need_redraw = 1; }
                    else if(key == 0x2C) { w->border_color = (w->border_color + 1) % 16; need_redraw = 1; }
                    else if(key == 0x2D) { w->border_color = (w->border_color + 15) % 16; need_redraw = 1; }
                    
                    else if(key == 0x22) { w->fill_type = (w->fill_type + 1) % 3; need_redraw = 1; }
                    else if(key == 0x23) {
                        if(w->fill_char == ' ') w->fill_char = BLOCK;
                        else if(w->fill_char == BLOCK) w->fill_char = 0xB1;
                        else if(w->fill_char == 0xB1) w->fill_char = 0xB2;
                        else if(w->fill_char == 0xB2) w->fill_char = 0xB0;
                        else w->fill_char = ' ';
                        need_redraw = 1;
                    }
                    
                    else if(key == 0x0E) { size_pos = 0; size_buf[0] = '\0'; edit_mode = 5; need_redraw = 1; }
                    else if(key == 0x1C) { edit_mode = 0; need_redraw = 1; }
                    
                    if(w->x < 0) w->x = 0;
                    if(w->y < 2) w->y = 2;
                    if(w->x + w->w > 79) w->x = 79 - w->w;
                    if(w->y + w->h > 22) w->y = 22 - w->h;
                }
                
                else if(edit_mode == 2 && selected_widget >= 0) {
                    UIWidget* w = &widgets[selected_widget];
                    int step = snap_to_grid ? grid_size : 1;
                    
                    if(key == 0x11) { if(w->h > 1) { w->h -= step; need_redraw = 1; } }
                    else if(key == 0x1F) { w->h += step; need_redraw = 1; }
                    else if(key == 0x1E) { if(w->w > 3) { w->w -= step; need_redraw = 1; } }
                    else if(key == 0x20) { w->w += step; need_redraw = 1; }
                    else if(key == 0x1C) { edit_mode = 0; need_redraw = 1; }
                    
                    if(w->x + w->w > 79) w->w = 79 - w->x;
                    if(w->y + w->h > 22) w->h = 22 - w->y;
                }
                
                else {
                    int step = snap_to_grid ? grid_size : 1;
                    
                    if(key == 0x11) { if(cursor_y > 2) cursor_y -= step; need_redraw = 1; }
                    else if(key == 0x1F) { if(cursor_y < 22) cursor_y += step; need_redraw = 1; }
                    else if(key == 0x1E) { if(cursor_x > 0) cursor_x -= step; need_redraw = 1; }
                    else if(key == 0x20) { if(cursor_x < 79) cursor_x += step; need_redraw = 1; }
                    
                    else if(key == 0x02) { current_tool = UI_WINDOW; need_redraw = 1; }
                    else if(key == 0x03) { current_tool = UI_BUTTON; need_redraw = 1; }
                    else if(key == 0x04) { current_tool = UI_LABEL; need_redraw = 1; }
                    else if(key == 0x05) { current_tool = UI_TEXTBOX; need_redraw = 1; }
                    else if(key == 0x06) { current_tool = UI_CHECKBOX; need_redraw = 1; }
                    else if(key == 0x07) { current_tool = UI_PROGRESS; need_redraw = 1; }
                    else if(key == 0x08) { current_tool = UI_SLIDER; need_redraw = 1; }
                    else if(key == 0x09) { current_tool = UI_PANEL; need_redraw = 1; }
                    else if(key == 0x0A) { current_tool = UI_LISTBOX; need_redraw = 1; }
                    
                    else if(key == 0x22) { show_grid = !show_grid; need_redraw = 1; }
                    else if(key == 0x13) { snap_to_grid = !snap_to_grid; need_redraw = 1; }
                    else if(key == 0x0D) { if(grid_size < 32) { grid_size += 2; need_redraw = 1; } }
                    else if(key == 0x0C) { if(grid_size > 4) { grid_size -= 2; need_redraw = 1; } }
                    
                    else if(key == 0x39) {
                        int found = -1;
                        for(int i = widget_count - 1; i >= 0; i--) {
                            UIWidget* w = &widgets[i];
                            if(!w->visible) continue;
                            int ww = (w->type == UI_LABEL) ? my_strlen(w->text) : w->w;
                            int wh = (w->type == UI_LABEL) ? 1 : w->h;
                            if(cursor_x >= w->x && cursor_x < w->x + ww &&
                               cursor_y >= w->y && cursor_y < w->y + wh) {
                                found = i; break;
                            }
                        }
                        if(found >= 0) { selected_widget = found; need_redraw = 1; }
                    }
                    
                    else if(key == 0x0F) {
                        if(widget_count > 0) { selected_widget = (selected_widget + 1) % widget_count; need_redraw = 1; }
                    }
                    
                    else if(key == 0x31 && widget_count < MAX_UI_WIDGETS) {
                        UIWidget* w = &widgets[widget_count];
                        w->id = widget_count; w->type = current_tool;
                        w->x = cursor_x; w->y = cursor_y;
                        w->w = (current_tool == UI_WINDOW) ? 40 : 12;
                        w->h = (current_tool == UI_WINDOW) ? 10 : 2;
                        w->bg_color = BLUE; w->fg_color = TXT_WHITE;
                        w->border_color = TXT_WHITE;
                        w->visible = 1; w->enabled = 1;
                        w->parent_id = -1;
                        w->value = 50;
                        w->min_value = 0; w->max_value = 100;
                        w->fill_type = 0;
                        w->fill_char = ' ';
                        
                        char name[32]; my_strcpy(name, "widget");
                        int_to_str(widget_count, name + 6); my_strcpy(w->name, name);
                        
                        if(current_tool == UI_BUTTON) my_strcpy(w->text, "Button");
                        else if(current_tool == UI_LABEL) my_strcpy(w->text, "Label");
                        else my_strcpy(w->text, "New");
                        
                        selected_widget = widget_count;
                        widget_count++;
                        need_redraw = 1;
                    }
                    
                    else if(key == 0x32 && selected_widget >= 0) { edit_mode = 1; need_redraw = 1; }
                    else if(key == 0x13 && selected_widget >= 0) { edit_mode = 2; need_redraw = 1; }
                    else if(key == 0x12 && selected_widget >= 0) { edit_mode = 3; edit_prop = 2; need_redraw = 1; }
                    else if(key == 0x15 && selected_widget >= 0) { edit_mode = 3; edit_prop = 1; need_redraw = 1; }
                    else if(key == 0x14 && selected_widget >= 0) { edit_mode = 3; edit_prop = 3; need_redraw = 1; }
                    
                    else if(key == 0x1D && selected_widget >= 0) {
                        action_menu = 1;
                        action_selected = widgets[selected_widget].action;
                        need_redraw = 1;
                    }
                    
                    else if(key == 0x20 && selected_widget >= 0 && widget_count < MAX_UI_WIDGETS) {
                        widgets[widget_count] = widgets[selected_widget];
                        widgets[widget_count].id = widget_count;
                        widgets[widget_count].x += grid_size;
                        widgets[widget_count].y += grid_size;
                        selected_widget = widget_count;
                        widget_count++;
                        need_redraw = 1;
                    }
                    
                    else if(key == 0x53 && selected_widget >= 0) {
                        for(int i = selected_widget; i < widget_count - 1; i++) {
                            widgets[i] = widgets[i + 1];
                            widgets[i].id = i;
                        }
                        widget_count--;
                        selected_widget = -1;
                        need_redraw = 1;
                    }
                    
                    else if(key == 0x2D) { selected_widget = -1; need_redraw = 1; }
                }
            }
            while(inb(0x64) & 1) inb(0x60);
        }
        
        if(need_redraw) {
            for(int i = 0; i < 80 * 25; i++) ui_buffer[i] = (BLACK << 12) | (TXT_WHITE << 8) | ' ';
            
            int content_start_y = 0;
            
            if(show_toolbar) {
                ui_hline(0, 0, 80, BLUE, TXT_WHITE, ' ');
                ui_kprint("UIDEV v9.0", 1, 0, (BLUE << 4) | TXT_YELLOW);
                
                const char* tools[] = {"1:Win", "2:Btn", "3:Lbl", "4:Txt", "5:Chk", "6:Prg", "7:Sld", "8:Pnl", "9:Lst"};
                int tool_x = 22;
                for(int i = 0; i < 9; i++) {
                    uint8_t color = (current_tool == i) ? TXT_GREEN : TXT_WHITE;
                    ui_kprint(tools[i], tool_x, 0, (BLUE << 4) | color);
                    tool_x += 6;
                }
                
                ui_hline(0, 1, 80, BLUE, TXT_WHITE, S_HLINE);
                content_start_y = 2;
            }
            
            int work_height = 23 - (show_statusbar ? 2 : 0);
            for(int y = content_start_y; y < work_height; y++) {
                for(int x = 0; x < 80; x++) {
                    ui_put_pixel(x, y, GRAY, TXT_WHITE, ' ');
                }
            }
            
            if(show_grid && !test_mode && !action_menu) {
                for(int x = 0; x < 80; x += grid_size) {
                    for(int y = content_start_y; y < work_height; y++) {
                        ui_put_pixel(x, y, GRAY, DARK_GRAY, '.');
                    }
                }
                for(int y = content_start_y; y < work_height; y += grid_size) {
                    for(int x = 0; x < 80; x++) {
                        ui_put_pixel(x, y, GRAY, DARK_GRAY, '.');
                    }
                }
            }
            
            for(int i = 0; i < widget_count; i++) {
                UIWidget* w = &widgets[i];
                if(!w->visible) continue;
                
                int is_selected = (i == selected_widget) && !test_mode && !action_menu;
                uint8_t border = is_selected ? LIGHT_GREEN : w->border_color;
                
                if(w->fill_type > 0 && w->type != UI_LABEL) {
                    ui_fill_rect(w->x + 1, w->y + 1, w->w - 2, w->h - 2, w->bg_color, w->fg_color, w->fill_type, w->fill_char);
                }
                
                if(w->type == UI_WINDOW) {
                    ui_shadow_window(w->x, w->y, w->w, w->h, w->bg_color, border, w->text);
                }
                else if(w->type == UI_BUTTON) {
                    ui_button_draw(w->x, w->y, w->w, w->h, w->text, w->bg_color, w->fg_color);
                    if(is_selected) {
                        ui_frame(w->x-1, w->y-1, w->w+2, w->h+2, BLACK, LIGHT_GREEN);
                    }
                }
                else if(w->type == UI_LABEL) {
                    ui_kprint(w->text, w->x, w->y, (w->bg_color << 4) | w->fg_color);
                    if(is_selected) {
                        ui_frame(w->x-1, w->y-1, my_strlen(w->text)+2, 3, w->bg_color, LIGHT_GREEN);
                    }
                }
                else if(w->type == UI_TEXTBOX) {
                    ui_frame(w->x, w->y, w->w, w->h, w->bg_color, border);
                    if(w->text[0]) ui_kprint(w->text, w->x+1, w->y, (w->bg_color << 4) | w->fg_color);
                }
                else if(w->type == UI_CHECKBOX) {
                    ui_kprint(w->checked ? "[X]" : "[ ]", w->x, w->y, (w->bg_color << 4) | border);
                    ui_kprint(w->text, w->x+4, w->y, (w->bg_color << 4) | w->fg_color);
                }
                else if(w->type == UI_PROGRESS) {
                    ui_frame(w->x, w->y, w->w, w->h, w->bg_color, border);
                    int fill = (w->value * (w->w - 2)) / 100;
                    for(int j = 0; j < fill; j++) {
                        ui_put_pixel(w->x+1+j, w->y, w->bg_color, w->fg_color, BLOCK);
                    }
                }
                else if(w->type == UI_SLIDER) {
                    ui_hline(w->x, w->y, w->w, w->bg_color, border, S_HLINE);
                    int pos = w->x + (w->value * (w->w - 1)) / (w->max_value - w->min_value);
                    ui_put_pixel(pos, w->y, w->fg_color, TXT_WHITE, BLOCK);
                }
                else if(w->type == UI_PANEL) {
                    ui_frame(w->x, w->y, w->w, w->h, w->bg_color, border);
                }
                else if(w->type == UI_LISTBOX) {
                    ui_frame(w->x, w->y, w->w, w->h, w->bg_color, border);
                    ui_kprint("Item 1", w->x+2, w->y+1, (w->bg_color << 4) | w->fg_color);
                    ui_kprint("Item 2", w->x+2, w->y+2, (w->bg_color << 4) | w->fg_color);
                }
            }
            
            if(edit_mode == 0 && !test_mode && !action_menu) {
                ui_put_pixel(cursor_x, cursor_y, LIGHT_GREEN, TXT_WHITE, '+');
            }
            
            if(test_mode) {
                ui_put_pixel(cursor_x, cursor_y, TXT_WHITE, TXT_BLACK, 0xDB);
            }
            
            if(action_menu) {
                int mx = 20, my = 5;
                int menu_w = 25;
                int menu_h = ACTION_COUNT + 3;
                ui_shadow_window(mx, my, menu_w, menu_h, BLUE, TXT_WHITE, "Select Action");
                
                for(int i = 0; i < ACTION_COUNT; i++) {
                    uint8_t color = (i == action_selected) ? TXT_GREEN : TXT_WHITE;
                    ui_kprint(action_names[i], mx + 2, my + 2 + i, (BLUE << 4) | color);
                }
                
                ui_kprint("W/S:Select Enter:OK", mx + 2, my + menu_h - 2, (BLUE << 4) | TXT_YELLOW);
            }
            
            if(show_properties && !test_mode && !action_menu) {
                int px = show_toolbar ? 55 : 60;
                ui_vline(px - 1, content_start_y, work_height - content_start_y, DARK_GRAY, TXT_WHITE, S_VLINE);
                
                if(selected_widget >= 0) {
                    UIWidget* w = &widgets[selected_widget];
                    ui_kprint("PROPERTIES", px, content_start_y, (DARK_GRAY << 4) | TXT_YELLOW);
                    ui_hline(px - 1, content_start_y + 1, 80 - px + 1, DARK_GRAY, TXT_WHITE, S_HLINE);
                    
                    char buf[32]; int y = content_start_y + 2;
                    
                    ui_kprint("ID:", px, y++, (DARK_GRAY << 4) | TXT_CYAN);
                    int_to_str(w->id, buf); ui_kprint(buf, px + 4, y-1, (DARK_GRAY << 4) | TXT_WHITE);
                    
                    ui_kprint("Type:", px, y++, (DARK_GRAY << 4) | TXT_CYAN);
                    ui_kprint(type_names[w->type], px + 6, y-1, (DARK_GRAY << 4) | TXT_GREEN);
                    
                    ui_kprint("Name:", px, y++, (DARK_GRAY << 4) | (edit_prop == 1 ? TXT_GREEN : TXT_CYAN));
                    ui_kprint(w->name, px + 6, y-1, (DARK_GRAY << 4) | TXT_WHITE);
                    
                    ui_kprint("Text:", px, y++, (DARK_GRAY << 4) | (edit_prop == 2 ? TXT_GREEN : TXT_CYAN));
                    if(w->text[0]) ui_kprint(w->text, px + 6, y-1, (DARK_GRAY << 4) | TXT_WHITE);
                    
                    ui_kprint("Tip:", px, y++, (DARK_GRAY << 4) | (edit_prop == 3 ? TXT_GREEN : TXT_CYAN));
                    if(w->tooltip[0]) ui_kprint(w->tooltip, px + 5, y-1, (DARK_GRAY << 4) | TXT_WHITE);
                    y++;
                    
                    ui_kprint("X:", px, y, (DARK_GRAY << 4) | TXT_CYAN);
                    int_to_str(w->x, buf); ui_kprint(buf, px + 3, y, (DARK_GRAY << 4) | TXT_WHITE);
                    ui_kprint("Y:", px + 8, y, (DARK_GRAY << 4) | TXT_CYAN);
                    int_to_str(w->y, buf); ui_kprint(buf, px + 11, y++, (DARK_GRAY << 4) | TXT_WHITE);
                    
                    ui_kprint("W:", px, y, (DARK_GRAY << 4) | TXT_CYAN);
                    int_to_str(w->w, buf); ui_kprint(buf, px + 3, y, (DARK_GRAY << 4) | TXT_WHITE);
                    ui_kprint("H:", px + 8, y, (DARK_GRAY << 4) | TXT_CYAN);
                    int_to_str(w->h, buf); ui_kprint(buf, px + 11, y++, (DARK_GRAY << 4) | TXT_WHITE);
                    y++;
                    
                    ui_kprint("BG:", px, y, (DARK_GRAY << 4) | TXT_CYAN);
                    int_to_str(w->bg_color, buf); ui_kprint(buf, px + 4, y, (DARK_GRAY << 4) | TXT_WHITE);
                    ui_kprint("FG:", px + 8, y, (DARK_GRAY << 4) | TXT_CYAN);
                    int_to_str(w->fg_color, buf); ui_kprint(buf, px + 12, y, (DARK_GRAY << 4) | TXT_WHITE);
                    ui_kprint("BR:", px + 16, y, (DARK_GRAY << 4) | TXT_CYAN);
                    int_to_str(w->border_color, buf); ui_kprint(buf, px + 20, y++, (DARK_GRAY << 4) | TXT_WHITE);
                    y++;
                    
                    ui_kprint("Fill:", px, y++, (DARK_GRAY << 4) | TXT_CYAN);
                    const char* fill_names[] = {"None", "Solid", "Pattern"};
                    ui_kprint(fill_names[w->fill_type], px + 6, y-1, (DARK_GRAY << 4) | TXT_GREEN);
                    y++;
                    
                    ui_kprint("Action:", px, y++, (DARK_GRAY << 4) | TXT_YELLOW);
                    ui_kprint(action_names[w->action], px + 2, y-1, (DARK_GRAY << 4) | TXT_GREEN);
                } else {
                    ui_kprint("NO WIDGET", px, content_start_y + 2, (DARK_GRAY << 4) | TXT_YELLOW);
                    ui_kprint("SELECTED", px, content_start_y + 3, (DARK_GRAY << 4) | TXT_YELLOW);
                }
            }
            
            if(show_statusbar) {
                ui_hline(0, 23, 80, BLUE, TXT_WHITE, ' ');
                
                if(test_mode) {
                    ui_kprint("TEST MODE - WASD:Move  Space:Click  ESC:Exit", 15, 23, (BLUE << 4) | TXT_YELLOW);
                } else if(action_menu) {
                    ui_kprint("SELECT ACTION - W/S:Choose  Enter:Confirm  ESC:Cancel", 12, 23, (BLUE << 4) | TXT_YELLOW);
                } else if(edit_mode == 0) {
                    ui_kprint("F2:Save F3:Load F4:Export C++ F5:Test | WASD:Move Space:Select N:New M:Move E:Text T:Tip Ctrl+A:Action", 1, 23, (BLUE << 4) | TXT_CYAN);
                } else if(edit_mode == 1) {
                    ui_kprint("MOVE: WASD Q/E:W R/F:H C/V:BG B/N:FG Z/X:Border G:Fill H:Pat Bksp:Size Enter:OK", 1, 23, (BLUE << 4) | TXT_YELLOW);
                } else if(edit_mode == 2) {
                    ui_kprint("RESIZE: WASD  Enter:OK  ESC:Cancel", 25, 23, (BLUE << 4) | TXT_YELLOW);
                } else if(edit_mode == 3) {
                    ui_kprint("EDIT: Type text  Enter:OK  ESC:Cancel", 25, 23, (BLUE << 4) | TXT_YELLOW);
                } else if(edit_mode == 5) {
                    ui_kprint("ENTER SIZE (WxH): Type numbers and 'x', Enter:OK", 10, 23, (BLUE << 4) | TXT_YELLOW);
                    if(size_pos > 0) {
                        ui_kprint(size_buf, 35, 22, (BLUE << 4) | TXT_GREEN);
                    }
                }
                
                ui_kprint("X:", 55, 24, (BLUE << 4) | TXT_CYAN);
                char cx[4]; int_to_str(cursor_x, cx); ui_kprint(cx, 57, 24, (BLUE << 4) | TXT_WHITE);
                ui_kprint("Y:", 61, 24, (BLUE << 4) | TXT_CYAN);
                char cy[4]; int_to_str(cursor_y, cy); ui_kprint(cy, 63, 24, (BLUE << 4) | TXT_WHITE);
                
                char wcount[8]; int_to_str(widget_count, wcount);
                ui_kprint("W:", 68, 24, (BLUE << 4) | TXT_CYAN);
                ui_kprint(wcount, 70, 24, (BLUE << 4) | TXT_WHITE);
            }
            
            ui_flip();
            move_cursor(79, 24);
            need_redraw = 0;
        }
        
        for(volatile int i = 0; i < 2000; i++);
    }
    
    clear_screen();
    kprint_color("UIDEV closed\n", TXT_GREEN);
}

int SCREEN_WIDTH = 80;
int SCREEN_HEIGHT = 25;

typedef struct {
    int cols;
    int rows;
    const char* name;
    uint8_t vga_reg;
} TextMode;

TextMode text_modes[] = {
    {80, 25, "80x25", 0x03},
    {80, 50, "80x50", 0x12},
    {132, 25, "132x25", 0x14},
    {132, 43, "132x43", 0x54},
    {132, 50, "132x50", 0x55},
    {132, 60, "132x60", 0x6A},
    {90, 30, "90x30", 0xFF},
    {90, 60, "90x60", 0xFF},
    {100, 37, "100x37", 0xFF},
    {128, 48, "128x48", 0xFF},
    {0, 0, NULL, 0}
};

void set_font_8x8(void) {
    outb(0x3C4, 0x01);
    outb(0x3C5, inb(0x3C5) & 0xFE);
    outb(0x3C4, 0x04);
    outb(0x3C5, 0x06);
    outb(0x3D4, 0x09);
    uint8_t max_scan = inb(0x3D5);
    outb(0x3D5, (max_scan & 0xE0) | 7);
}

void set_font_8x16(void) {
    outb(0x3C4, 0x01);
    outb(0x3C5, inb(0x3C5) | 0x01);
    outb(0x3C4, 0x04);
    outb(0x3C5, 0x02);
    outb(0x3D4, 0x09);
    uint8_t max_scan = inb(0x3D5);
    outb(0x3D5, (max_scan & 0xE0) | 15);
}

void set_font_8x14(void) {
    outb(0x3C4, 0x01);
    outb(0x3C5, inb(0x3C5) | 0x01);
    outb(0x3C4, 0x04);
    outb(0x3C5, 0x04);
    outb(0x3D4, 0x09);
    uint8_t max_scan = inb(0x3D5);
    outb(0x3D5, (max_scan & 0xE0) | 13);
}

void set_cols_80(void) {
    outb(0x3CC, inb(0x3CC) & 0xFE);
    outb(0x3C2, 0x63);
}

void set_cols_132(void) {
    outb(0x3CC, inb(0x3CC) | 0x01);
    outb(0x3D4, 0x11);
    outb(0x3D5, inb(0x3D5) & 0x7F);
    outb(0x3C2, 0x67);
}

void set_text_mode_advanced(int cols, int rows) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
    
    if(cols == 132) {
        set_cols_132();
    } else {
        set_cols_80();
    }
    
    if(rows >= 50) {
        set_font_8x8();
    } else if(rows >= 43) {
        set_font_8x14();
    } else {
        set_font_8x16();
    }
    
    volatile uint16_t* video = (volatile uint16_t*)0xB8000;
    for(int i = 0; i < cols * rows; i++) {
        video[i] = 0x0720;
    }
    
    SCREEN_WIDTH = cols;
    SCREEN_HEIGHT = rows;
}

void set_80x25(void) { set_text_mode_advanced(80, 25); }
void set_80x50(void) { set_text_mode_advanced(80, 50); }
void set_132x25(void) { set_text_mode_advanced(132, 25); }
void set_132x43(void) { set_text_mode_advanced(132, 43); }
void set_132x50(void) { set_text_mode_advanced(132, 50); }
void set_132x60(void) { set_text_mode_advanced(132, 60); }
void set_90x30(void) { set_text_mode_advanced(90, 30); }
void set_90x60(void) { set_text_mode_advanced(90, 60); }
void set_100x37(void) { set_text_mode_advanced(100, 37); }
void set_128x48(void) { set_text_mode_advanced(128, 48); }

void get_current_mode_info(char* buf) {
    int_to_str(SCREEN_WIDTH, buf);
    int len = my_strlen(buf);
    buf[len++] = 'x';
    int_to_str(SCREEN_HEIGHT, buf + len);
}

void show_mode_menu(void) {
    clear_screen();
    kprint_color("=== TEXT MODE SELECTOR ===\n\n", TXT_YELLOW);
    
    for(int i = 0; text_modes[i].name; i++) {
        kprint_int(i + 1);
        kprint(". ");
        kprint(text_modes[i].name);
        kprint("\n");
    }
    
    kprint("\n0. Exit\n");
    kprint("Select mode: ");
}

void interactive_mode_select(void) {
    char choice;
    while(1) {
        show_mode_menu();
        choice = wait_key();
        
        if(choice == '0' || choice == 27) break;
        
        int idx = choice - '1';
        if(idx >= 0 && idx < 10 && text_modes[idx].name) {
            set_text_mode_advanced(text_modes[idx].cols, text_modes[idx].rows);
            clear_screen();
            kprint_color("Mode set to ", TXT_GREEN);
            kprint(text_modes[idx].name);
            kprint("\nPress any key...");
            wait_key();
        }
    }
    clear_screen();
}

void cmd_mode(const char* arg) {
    if(arg[0] == '\0') {
        kprint("Usage: mode <");
        for(int i = 0; text_modes[i].name; i++) {
            if(i > 0) kprint("|");
            kprint(text_modes[i].name);
        }
        kprint("|menu>\n");
        kprint("Current: ");
        kprint_int(SCREEN_WIDTH);
        kprint("x");
        kprint_int(SCREEN_HEIGHT);
        kprint("\n");
        return;
    }
    
    if(my_strcmp(arg, "menu") == 0) {
        interactive_mode_select();
        return;
    }
    
    for(int i = 0; text_modes[i].name; i++) {
        if(my_strcmp(arg, text_modes[i].name) == 0) {
            set_text_mode_advanced(text_modes[i].cols, text_modes[i].rows);
            clear_screen();
            kprint_color("Mode: ", TXT_GREEN);
            kprint(text_modes[i].name);
            kprint("\n");
            return;
        }
    }
    
    kprint_color("Unknown mode. Try 'mode menu'\n", TXT_RED);
}

void mode_auto(void) {
    set_text_mode_advanced(132, 60);
    if(SCREEN_WIDTH == 132 && SCREEN_HEIGHT == 60) {
        kprint_color("Auto: 132x60 (Best)\n", TXT_GREEN);
        return;
    }
    
    set_text_mode_advanced(80, 50);
    if(SCREEN_HEIGHT == 50) {
        kprint_color("Auto: 80x50 (Good)\n", TXT_GREEN);
        return;
    }
    
    set_80x25();
    kprint_color("Auto: 80x25 (Default)\n", TXT_YELLOW);
}

void mode_info(void) {
    kprint("Screen: ");
    kprint_int(SCREEN_WIDTH);
    kprint("x");
    kprint_int(SCREEN_HEIGHT);
    kprint("\n");
    
    kprint("Video memory: 0xB8000\n");
    kprint("Buffer size: ");
    kprint_int(SCREEN_WIDTH * SCREEN_HEIGHT * 2);
    kprint(" bytes\n");
    
    kprint("Available modes: ");
    int count = 0;
    for(int i = 0; text_modes[i].name; i++) count++;
    kprint_int(count);
    kprint("\n");
}

static int current_mode_index = 0;

void mode_next(void) {
    int count = 0;
    while(text_modes[count].name) count++;
    
    current_mode_index = (current_mode_index + 1) % count;
    set_text_mode_advanced(text_modes[current_mode_index].cols, 
                           text_modes[current_mode_index].rows);
    clear_screen();
    kprint_color("Mode: ", TXT_GREEN);
    kprint(text_modes[current_mode_index].name);
    kprint("\n");
}

void mode_prev(void) {
    int count = 0;
    while(text_modes[count].name) count++;
    
    current_mode_index = (current_mode_index - 1 + count) % count;
    set_text_mode_advanced(text_modes[current_mode_index].cols,
                           text_modes[current_mode_index].rows);
    clear_screen();
    kprint_color("Mode: ", TXT_GREEN);
    kprint(text_modes[current_mode_index].name);
    kprint("\n");
}


void history_add(const char* cmd);
void history_up(char* buffer, int* cursor_pos);
void history_down(char* buffer, int* cursor_pos);




extern "C" void process_debug_command(char* input_buffer, int& input_ptr) {
    char original_cmd[256];
    int orig_len = input_ptr;
    for(int i = 0; i < orig_len && i < 255; i++) {
        original_cmd[i] = input_buffer[i];
    }
    original_cmd[orig_len] = '\0';
    
    input_buffer[input_ptr] = '\0';

    if(input_ptr == 0) {
        kprint("\n");
        history_add(input_buffer);
        kprint_color("debug@wnka> ", TXT_GREEN);
        return;
    }
    
    char cmd_copy[256];
    int i;
    for(i = 0; i < input_ptr; i++) cmd_copy[i] = input_buffer[i];
    cmd_copy[i] = '\0';
    
    input_ptr = 0;
    
    char* arg = (char*)""; 
    int space_idx = -1;
    for(int i = 0; cmd_copy[i]; i++) {
        if(cmd_copy[i] == ' ') {
            space_idx = i;
            break;
        }
    }
    
    if (space_idx != -1) {
        cmd_copy[space_idx] = '\0';
        arg = &cmd_copy[space_idx + 1];
    }
    
    kprint("\n");

if (my_strcmp(input_buffer, "help") == 0) {
    int current_page = 0;
    int total_pages = 3;
    int running = 1;
    
    while(running) {
        clear_screen_bg(BLACK);
        
        kprint_at("========================================", 20, 1, (BLACK << 4) | TXT_YELLOW);
        kprint_at("     WNKA OS DEBUG COMMANDS v1.0", 25, 2, (BLACK << 4) | TXT_CYAN);
        kprint_at("========================================", 20, 3, (BLACK << 4) | TXT_YELLOW);

        if(current_page == 0) {
            kprint_at("== DEBUG MODE INFO ==", 28, 5, (BLACK << 4) | TXT_RED);
            kprint_at("  ATTENTION THIS IS A DEBUG MODE", 20, 7, (BLACK << 4) | TXT_WHITE);
            kprint_at("  DOES NOT INCLUDE ALL FEATURES", 20, 8, (BLACK << 4) | TXT_WHITE);
            kprint_at("  IF YOU ACCIDENTALLY GET INTO IT, RESTART", 20, 9, (BLACK << 4) | TXT_WHITE);
            kprint_at("  IF YOU ENTERED HERE TO CHECK THE OS, KEEP WORKING", 20, 10, (BLACK << 4) | TXT_WHITE);
            kprint_at("  GO TO PAGE 2-3 TO VIEW COMMANDS", 20, 11, (BLACK << 4) | TXT_WHITE);
            kprint_at("  NO WNKFS OR OTHER FS HERE", 20, 12, (BLACK << 4) | TXT_WHITE);
            kprint_at("", 20, 13, (BLACK << 4) | TXT_WHITE);
            kprint_at("  Available commands: ~35 debug commands", 20, 14, (BLACK << 4) | TXT_GREEN);
        }
        
        if(current_page == 1) {
            kprint_at("  cls      - clear screen", 20, 7, (BLACK << 4) | TXT_WHITE);
            kprint_at("  reboot   - restart system", 20, 8, (BLACK << 4) | TXT_WHITE);
            kprint_at("  shut     - power off", 20, 9, (BLACK << 4) | TXT_WHITE);
            kprint_at("  halt     - halt system", 20, 10, (BLACK << 4) | TXT_WHITE);
            kprint_at("  fetch    - show PC info", 20, 11, (BLACK << 4) | TXT_WHITE);
            kprint_at("  beep     - make beep sound", 20, 12, (BLACK << 4) | TXT_WHITE);
            kprint_at("  tcc      - tiny C compiler", 20, 13, (BLACK << 4) | TXT_WHITE);
            kprint_at("  regs     - show CPU registers", 20, 14, (BLACK << 4) | TXT_WHITE);
            kprint_at("  stack    - show stack dump", 20, 15, (BLACK << 4) | TXT_WHITE);
            kprint_at("  stackcheck - check stack overflow", 20, 16, (BLACK << 4) | TXT_WHITE);
            kprint_at("  flags    - show EFLAGS", 20, 17, (BLACK << 4) | TXT_WHITE);
            kprint_at("  cpuid    - CPU vendor info", 20, 18, (BLACK << 4) | TXT_WHITE);
            kprint_at("  cpuinfo  - CPU features (MMX, SSE)", 20, 19, (BLACK << 4) | TXT_WHITE);
            kprint_at("  gdt      - GDT table dump", 20, 20, (BLACK << 4) | TXT_WHITE);
            kprint_at("  timer    - PIT counter", 20, 21, (BLACK << 4) | TXT_WHITE);
        }
        
        if(current_page == 2) {
            kprint_at("  peek <addr> - view memory at address", 20, 7, (BLACK << 4) | TXT_WHITE);
            kprint_at("  dump <addr> - memory dump", 20, 8, (BLACK << 4) | TXT_WHITE);
            kprint_at("  testmem [mb] - test memory (with progress)", 20, 9, (BLACK << 4) | TXT_WHITE);
            kprint_at("  testram     - RAM speed test", 20, 10, (BLACK << 4) | TXT_WHITE);
            kprint_at("  memmap      - memory map", 20, 11, (BLACK << 4) | TXT_WHITE);
            kprint_at("  benchmark   - performance benchmark", 20, 12, (BLACK << 4) | TXT_WHITE);
            kprint_at("  bench       - string benchmark", 20, 13, (BLACK << 4) | TXT_WHITE);
            kprint_at("  profile     - performance profile", 20, 14, (BLACK << 4) | TXT_WHITE);
            kprint_at("  testint     - interrupt test", 20, 15, (BLACK << 4) | TXT_WHITE);
            kprint_at("  irq         - IRQ status", 20, 16, (BLACK << 4) | TXT_WHITE);
            kprint_at("  intstat     - interrupt statistics", 20, 17, (BLACK << 4) | TXT_WHITE);
            kprint_at("  pciscan     - PCI bus scan", 20, 18, (BLACK << 4) | TXT_WHITE);
            kprint_at("  calc <expr> - calculator (5+3)", 20, 19, (BLACK << 4) | TXT_WHITE);
            kprint_at("  sleep <n>   - pause for N seconds", 20, 20, (BLACK << 4) | TXT_WHITE);
            kprint_at("  crashme     - test system stability", 20, 21, (BLACK << 4) | TXT_WHITE);
        }

        kprint_at("========================================", 20, 22, (BLACK << 4) | TXT_YELLOW);
        kprint_at("Page: ", 30, 23, (BLACK << 4) | TXT_CYAN);
        kprint_int_at(current_page + 1, 36, 23, (BLACK << 4) | TXT_GREEN);
        kprint_at("/", 38, 23, (BLACK << 4) | TXT_WHITE);
        kprint_int_at(total_pages, 40, 23, (BLACK << 4) | TXT_GREEN);
        
        kprint_at("[L] Prev  [R] Next  [ESC] Exit", 25, 24, (BLACK << 4) | TXT_YELLOW);
        
        int key_processed = 0;
        while(!key_processed) {
            if(inb(0x64) & 1) {
                uint8_t key = inb(0x60);
                
                if(key < 0x80) {
                    if(key == 0x26) {
                        if(current_page > 0) current_page--;
                        key_processed = 1;
                    }
                    else if(key == 0x13) {
                        if(current_page < total_pages - 1) current_page++;
                        key_processed = 1;
                    }
                    else if(key == 0x01) {
                        running = 0;
                        key_processed = 1;
                    }
                }
                while(inb(0x64) & 1) inb(0x60);
            }
        }
    }
    
    clear_screen();
}
else if (my_strcmp(input_buffer, "cls") == 0) {
    clear_screen();
}
else if (my_strcmp(input_buffer, "reboot") == 0) {
        kprint("Restarting...\n");
        play_reboot_sound();
        outb(0x64, 0xFE);  
}
else if (my_strcmp(input_buffer, "fetch") == 0) {
    run_fetch();
}
else if (my_strcmp(input_buffer, "shut") == 0) {
    power_off_extreme();
}
else if(my_strcmp(cmd_copy, "tcc") == 0) {
    if(arg[0] == '\0') {
        kprint("TinyCC - C compiler\n");
        kprint("Usage: tcc <code>\n");
        kprint("Example: tcc 'int main() { return 42; }'\n");
    } else {
        TCCState* s = tcc_new();
        if(s) {
            if(tcc_compile_string(s, arg) == 0) {
                if(tcc_relocate(s, NULL) == 0) {
                    int result = tcc_run(s, 0, NULL);
                    kprint("Result: ");
                    kprint_int(result);
                    kprint("\n");
                }
            }
            tcc_delete(s);
        }
    }
}
else if (my_strcmp(input_buffer, "beep") == 0) {
    beep();
}
else if(my_strcmp(cmd_copy, "regs") == 0) {
    uint32_t eax, ebx, ecx, edx, ebp, esp;
    
    __asm__ volatile("mov %%eax, %0" : "=r"(eax));
    __asm__ volatile("mov %%ebx, %0" : "=r"(ebx));
    __asm__ volatile("mov %%ecx, %0" : "=r"(ecx));
    __asm__ volatile("mov %%edx, %0" : "=r"(edx));
    __asm__ volatile("mov %%ebp, %0" : "=r"(ebp));
    __asm__ volatile("mov %%esp, %0" : "=r"(esp));
    
    kprint("EAX: 0x"); kprint_hex32(eax); kprint("\n");
    kprint("EBX: 0x"); kprint_hex32(ebx); kprint("\n");
    kprint("ECX: 0x"); kprint_hex32(ecx); kprint("\n");
    kprint("EDX: 0x"); kprint_hex32(edx); kprint("\n");
    kprint("EBP: 0x"); kprint_hex32(ebp); kprint("\n");
    kprint("ESP: 0x"); kprint_hex32(esp); kprint("\n");
}
else if(my_strcmp(cmd_copy, "stack") == 0) {
    uint32_t esp;
    __asm__ volatile("mov %%esp, %0" : "=r"(esp));
    for(int i = 0; i < 16; i++) {
        kprint_hex32(esp + i*4);
        kprint(": 0x");
        kprint_hex32(*(uint32_t*)(esp + i*4));
        kprint("\n");
    }
}
else if(my_strncmp(cmd_copy, "dump", 4) == 0) {
    uint32_t addr = 0;
    char* p = arg;
    while(*p >= '0' && *p <= '9') {
        addr = addr * 16 + (*p - '0');
        p++;
    }
    for(int i = 0; i < 16; i++) {
        kprint_hex32(addr + i*4);
        kprint(": ");
        for(int j = 0; j < 4; j++) {
            kprint_hex8(*(uint8_t*)(addr + i*4 + j));
            kprint(" ");
        }
        kprint("\n");
    }
}
else if(my_strncmp(cmd_copy, "peek", 4) == 0) {
    uint32_t addr = 0;
    char* p = arg;
    while(*p >= '0' && *p <= '9') {
        addr = addr * 16 + (*p - '0');
        p++;
    }
    kprint("0x"); kprint_hex32(addr);
    kprint(": 0x"); kprint_hex32(*(uint32_t*)addr);
    kprint("\n");
}
else if(my_strcmp(cmd_copy, "testmem") == 0) {
    uint32_t start_addr = 0x1000000;
    uint32_t size_mb = 1;
    int write_mode = 0;
    if(arg[0] != '\0') {
        if(arg[0] == '-' && arg[1] == 'w') {
            write_mode = 1;
            arg += 2;
            while(*arg == ' ') arg++;
        }
        
        size_mb = 0;
        for(char* p = arg; *p >= '0' && *p <= '9'; p++) {
            size_mb = size_mb * 10 + (*p - '0');
        }
        if(size_mb < 1) size_mb = 1;
        if(size_mb > 512) size_mb = 512;
    }
    
    uint32_t total_bytes = (uint64_t)size_mb * 1024 * 1024;
    uint32_t total_dwords = total_bytes / 4;
    uint32_t bar_width = 40;
    uint32_t block_size = total_dwords / bar_width;
    
    if(block_size < 1) block_size = 1;
    
    kprint("Testing ");
    kprint_int(size_mb);
    kprint(" MB of memory at 0x");
    kprint_hex32(start_addr);
    kprint("\n");
    
    if(write_mode) {
        kprint_color("WRITE MODE ENABLED! This will modify memory!\n", TXT_YELLOW);
        if(size_mb > 64) {
            kprint_color("ERROR: Write mode limited to 64 MB for safety\n", TXT_RED);
            return;
        }
    } else {
        kprint_color("Read-only mode (safe)\n", TXT_GREEN);
    }
    
    kprint("Press ESC to cancel\n\n");
    
    volatile uint32_t* mem = (uint32_t*)start_addr;
    uint32_t errors = 0;
    int cancelled = 0;
    int last_percent = -1;
    
    uint32_t* backup = NULL;
    if(write_mode) {
        uint32_t backup_size = total_dwords;
        backup = (uint32_t*)0x5000000;
        for(uint32_t i = 0; i < backup_size; i++) {
            backup[i] = mem[i];
        }
    }
    
    kprint("[");
    for(uint32_t i = 0; i < total_dwords; i++) {
        if(write_mode) {
            mem[i] = 0x55AA55AA;
            if(mem[i] != 0x55AA55AA) {
                errors++;
            }
        } else {
            uint32_t val = mem[i];
            if(val == 0xFFFFFFFF) {
                errors++;
            }
        }
        
        int percent = (i * 100) / total_dwords;
        if(percent != last_percent && percent % 2 == 0) {
            kprint("#");
            last_percent = percent;
        }
        
        if(i % 100000 == 0 && i > 0) {
            if(inb(0x64) & 1) {
                uint8_t sc = inb(0x60);
                if(sc == 0x01) cancelled = 1;
                break;
            }
        }
    }
    kprint("]\n");
    
    if(write_mode && backup) {
        kprint("Restoring original data...\n");
        for(uint32_t i = 0; i < total_dwords; i++) {
            mem[i] = backup[i];
        }
    }
    
    if(cancelled) {
        kprint_color("Test cancelled by user\n", TXT_YELLOW);
        return;
    }
    
    kprint("\nMemory test: ");
    if(errors == 0) {
        kprint_color("PASSED\n", TXT_GREEN);
        kprint_int(total_dwords);
        kprint(" dwords tested, 0 errors\n");
        
        if(size_mb >= 64) {
            kprint("All memory is accessible\n");
        }
    } else {
        kprint_color("FAILED\n", TXT_RED);
        kprint_int(errors);
        kprint(" errors found\n");
        
        if(!write_mode && errors > 0) {
            kprint("(Memory beyond available RAM returns 0xFFFFFFFF)\n");
        }
    }
}
else if(my_strcmp(cmd_copy, "testram") == 0) {
    kprint("=== RAM SPEED TEST ===\n");
    
    volatile uint32_t* test = (uint32_t*)0x200000;
    uint32_t start, end;
    
    __asm__ volatile("rdtsc" : "=A"(start));
    for(int i = 0; i < 100000; i++) {
        test[i] = i;
    }
    __asm__ volatile("rdtsc" : "=A"(end));
    kprint("Write 100KB: "); kprint_int(end - start); kprint(" cycles\n");
    
    __asm__ volatile("rdtsc" : "=A"(start));
    volatile uint32_t sum = 0;
    for(int i = 0; i < 100000; i++) {
        sum += test[i];
    }
    __asm__ volatile("rdtsc" : "=A"(end));
    kprint("Read 100KB: "); kprint_int(end - start); kprint(" cycles\n");
}
else if(my_strcmp(cmd_copy, "cpuid") == 0) {
    uint32_t eax, ebx, ecx, edx;
    
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    
    kprint("CPU Vendor: ");
    kprint_char((ebx >> 0) & 0xFF);
    kprint_char((ebx >> 8) & 0xFF);
    kprint_char((ebx >> 16) & 0xFF);
    kprint_char((ebx >> 24) & 0xFF);
    kprint_char((edx >> 0) & 0xFF);
    kprint_char((edx >> 8) & 0xFF);
    kprint_char((edx >> 16) & 0xFF);
    kprint_char((edx >> 24) & 0xFF);
    kprint_char((ecx >> 0) & 0xFF);
    kprint_char((ecx >> 8) & 0xFF);
    kprint_char((ecx >> 16) & 0xFF);
    kprint_char((ecx >> 24) & 0xFF);
    kprint("\n");
    
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    
    uint32_t family = (eax >> 8) & 0xF;
    uint32_t model = (eax >> 4) & 0xF;
    uint32_t stepping = eax & 0xF;
    
    kprint("Family: "); kprint_int(family);
    kprint(", Model: "); kprint_int(model);
    kprint(", Stepping: "); kprint_int(stepping); kprint("\n");
}
else if(my_strcmp(cmd_copy, "testint") == 0) {
    kprint("=== INTERRUPT TEST ===\n");
    
    uint32_t ticks_before = seconds;
    for(volatile int i = 0; i < 10000000; i++);
    uint32_t ticks_after = seconds;
    
    if(ticks_after > ticks_before) {
        kprint("Timer IRQ: OK\n");
    } else {
        kprint("Timer IRQ: FAILED\n");
    }
    
    kprint("Press any key to test keyboard IRQ...\n");
    int key_pressed = 0;
    for(int i = 0; i < 5000000; i++) {
        if(inb(0x64) & 1) {
            inb(0x60);
            key_pressed = 1;
            break;
        }
    }
    if(key_pressed) {
        kprint("Keyboard IRQ: OK\n");
    } else {
        kprint("Keyboard IRQ: TIMEOUT\n");
    }
}
else if(my_strcmp(cmd_copy, "benchmark") == 0) {
    kprint("=== BENCHMARK ===\n");
    kprint("Running standard tests...\n\n");
    
    uint32_t start, end;
    __asm__ volatile("rdtsc" : "=A"(start));
    for(int i = 0; i < 1000000; i++) {
        asm volatile("nop");
    }
    __asm__ volatile("rdtsc" : "=A"(end));
    uint32_t nop_cycles = end - start;
    
    kprint("1M NOP instructions: ");
    kprint_int(nop_cycles);
    kprint(" cycles\n");
    
    __asm__ volatile("rdtsc" : "=A"(start));
    volatile int sum = 0;
    for(int i = 0; i < 100000; i++) {
        sum += i;
    }
    __asm__ volatile("rdtsc" : "=A"(end));
    uint32_t add_cycles = end - start;
    
    kprint("100K additions: ");
    kprint_int(add_cycles);
    kprint(" cycles\n");
    
    __asm__ volatile("rdtsc" : "=A"(start));
    volatile int prod = 1;
    for(int i = 0; i < 10000; i++) {
        prod *= (i + 1);
    }
    __asm__ volatile("rdtsc" : "=A"(end));
    uint32_t mul_cycles = end - start;
    
    kprint("10K multiplications: ");
    kprint_int(mul_cycles);
    kprint(" cycles\n");
    
    uint32_t total_cycles = nop_cycles + add_cycles + mul_cycles;
    kprint("\nTotal cycles: ");
    kprint_int(total_cycles);
    kprint("\n");
    
    kprint("Performance: ");
    if(total_cycles < 5000000) {
        kprint_color("EXCELLENT (Native/Host)\n", TXT_CYAN);
    } else if(total_cycles < 20000000) {
        kprint_color("GOOD (Fast emulation)\n", TXT_GREEN);
    } else if(total_cycles < 50000000) {
        kprint_color("NORMAL (QEMU/KVM)\n", TXT_YELLOW);
    } else if(total_cycles < 200000000) {
        kprint_color("SLOW (QEMU/TCG)\n", TXT_LRED);
    } else {
        kprint_color("VERY SLOW (Heavy emulation)\n", TXT_RED);
    }
    
    if(total_cycles > 100000000) {
        kprint("\nYou are running in a slow emulator\n");
        kprint("   For better performance, try:\n");
        kprint("   - Use KVM acceleration\n");
        kprint("   - Run on real hardware\n");
        kprint("   - Increase RAM in VM\n");
    }
}
else if(my_strcmp(cmd_copy, "profile") == 0) {
    uint32_t start, end;
    __asm__ volatile("rdtsc" : "=A"(start));
    
    for(int i = 0; i < 1000000; i++) {
        asm volatile("nop");
    }
    
    __asm__ volatile("rdtsc" : "=A"(end));
    uint32_t cycles = end - start;
    
    kprint("1,000,000 NOP instructions: ");
    kprint_int(cycles);
    kprint(" cycles\n");
}
else if(my_strcmp(cmd_copy, "irq") == 0) {
    uint8_t irq_mask = inb(0x21) | (inb(0xA1) << 8);
    kprint("IRQ Status:\n");
    for(int i = 0; i < 16; i++) {
        if(!(irq_mask & (1 << i))) {
            kprint("  IRQ"); kprint_int(i); kprint(": ENABLED\n");
        } else {
            kprint("  IRQ"); kprint_int(i); kprint(": DISABLED\n");
        }
    }
}
else if(my_strcmp(cmd_copy, "memmap") == 0) {
    kprint("=== MEMORY MAP ===\n");
    kprint("0x00000000 - 0x0009FFFF : Conventional (640 KB)\n");
    kprint("0x000A0000 - 0x000FFFFF : Video/BIOS (384 KB)\n");
    kprint("0x00100000 - 0x001FFFFF : Kernel (1 MB)\n");
    kprint("0x00200000 - 0x00FFFFFF : Free (14 MB)\n");
    kprint("0x01000000 - 0x7FFFFFFF : Extended (>16 MB)\n");
}
else if(my_strcmp(cmd_copy, "cpuinfo") == 0) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    
    kprint("CPU Features:\n");
    if(edx & (1 << 23)) kprint("  MMX\n");
    if(edx & (1 << 25)) kprint("  SSE\n");
    if(edx & (1 << 26)) kprint("  SSE2\n");
    if(ecx & (1 << 0)) kprint("  SSE3\n");
    if(ecx & (1 << 9)) kprint("  SSSE3\n");
}
else if(my_strcmp(cmd_copy, "bench") == 0) {
    uint32_t start, end;
    
    __asm__ volatile("rdtsc" : "=A"(start));
    for(int i = 0; i < 10000; i++) {
        my_strlen("test");
    }
    __asm__ volatile("rdtsc" : "=A"(end));
    kprint("strlen: "); kprint_int(end - start); kprint(" cycles\n");
    
    char src[256], dst[256];
    __asm__ volatile("rdtsc" : "=A"(start));
    for(int i = 0; i < 1000; i++) {
        my_strcpy(dst, src);
    }
    __asm__ volatile("rdtsc" : "=A"(end));
    kprint("strcpy: "); kprint_int(end - start); kprint(" cycles\n");
}
else if(my_strcmp(cmd_copy, "stackcheck") == 0) {
    uint32_t esp;
    __asm__ volatile("mov %%esp, %0" : "=r"(esp));
    
    if(esp < 0x100000) {
        kprint_color("WARNING: Stack near kernel area!\n", TXT_YELLOW);
    } else {
        kprint("Stack OK at 0x"); kprint_hex32(esp); kprint("\n");
    }
}
else if(my_strcmp(cmd_copy, "flags") == 0) {
    uint32_t eflags;
    __asm__ volatile("pushf\npop %0" : "=r"(eflags));
    kprint("EFLAGS: 0x"); kprint_hex32(eflags); kprint("\n");
}
else if(my_strcmp(cmd_copy, "calc") == 0) {
    int a = 0, b = 0;
    char op = 0;
    
    char* p = arg;
    while(*p >= '0' && *p <= '9') {
        a = a * 10 + (*p - '0');
        p++;
    }
    op = *p;
    p++;
    while(*p >= '0' && *p <= '9') {
        b = b * 10 + (*p - '0');
        p++;
    }
    
    int result = 0;
    switch(op) {
        case '+': result = a + b; break;
        case '-': result = a - b; break;
        case '*': result = a * b; break;
        case '/': 
            if(b != 0) result = a / b;
            else { kprint("Error: division by zero\n"); return; }
            break;
        default: kprint("Error: unknown operator\n"); return;
    }
    
    kprint_int(a);
    kprint_char(' ');
    kprint_char(op);
    kprint_char(' ');
    kprint_int(b);
    kprint(" = ");
    kprint_int(result);
    kprint("\n");
}
else if(my_strcmp(cmd_copy, "timer") == 0) {
    uint8_t pit_low, pit_high;
    outb(0x43, 0x00);
    pit_low = inb(0x40);
    pit_high = inb(0x40);
    uint16_t pit_value = pit_low | (pit_high << 8);
    
    kprint("PIT counter: "); kprint_int(pit_value); kprint("\n");
    kprint("System uptime: "); kprint_int(seconds); kprint(" sec\n");
}
else if(my_strcmp(cmd_copy, "gdt") == 0) {
    uint32_t gdt_addr;
    uint16_t gdt_limit;
    __asm__ volatile("sgdt %0" : "=m"(gdt_limit), "=m"(gdt_addr));
    
    kprint("GDT base: 0x"); kprint_hex32(gdt_addr); kprint("\n");
    kprint("GDT limit: "); kprint_int(gdt_limit); kprint("\n");
    
    for(int i = 0; i < 3; i++) {
        kprint("Entry "); kprint_int(i); kprint(": ");
        uint32_t* entry = (uint32_t*)(gdt_addr + i*8);
        kprint_hex32(entry[0]); kprint(" "); kprint_hex32(entry[1]); kprint("\n");
    }
}
else if(my_strcmp(cmd_copy, "intstat") == 0) {
    kprint("Interrupt statistics:\n");
    for(int i = 0; i < 16; i++) {
        kprint("  INT"); kprint_int(i); kprint(": 0\n");
    }
    kprint("(counter not implemented yet)\n");
}
else if(my_strcmp(cmd_copy, "pciscan") == 0) {
    kprint("PCI devices:\n");
    for(int bus = 0; bus < 256; bus++) {
        for(int dev = 0; dev < 32; dev++) {
            uint32_t vendor = pci_read(bus, dev, 0, 0);
            if(vendor != 0xFFFFFFFF && vendor != 0) {
                kprint("  Bus "); kprint_int(bus); 
                kprint(" Dev "); kprint_int(dev); 
                kprint(": 0x"); kprint_hex16(vendor >> 16);
                kprint("/0x"); kprint_hex16(vendor & 0xFFFF); kprint("\n");
            }
        }
    }
}
else if(my_strcmp(cmd_copy, "bg") == 0) {
    if(arg[0] == '\0') {
        kprint("Usage: bg <command>\n");
        kprint("Examples:\n");
        kprint("  bg cowsay Hello\n");
        kprint("  bg matrix\n");
        kprint("  bg flappy\n");
    } else {
        run_background_command(arg, arg); 
    }
}
else if(my_strcmp(cmd_copy, "jobs") == 0) {
    list_background();
}
else if(my_strcmp(cmd_copy, "fg") == 0) {
    int id = atoi(arg);
    foreground_command(id);
}
else if(my_strcmp(cmd_copy, "bgkill") == 0) {
    int id = atoi(arg);
    kill_background(id);
}
else if(my_strcmp(cmd_copy, "multitest") == 0) {
    test_multitask();
}
else if(my_strcmp(cmd_copy, "ps") == 0) {
    task_list();
}
else if(my_strcmp(cmd_copy, "kill") == 0) {
    int pid = atoi(arg);
    task_kill(pid);
}
    else {
        kprint_color("Unknown command: ", 0x0C);
        kprint(original_cmd);
        play_error_sound();
        kprint("\n");
    }
    kprint_color("debug@wnka> ", TXT_GREEN);
}

extern void vesa_simple_test(void);
extern void vesa_scan_fb(void);
extern void vesa_info(void);
extern void vesa_test_pitch(void);
extern void vesa_test_red(void);
extern void vesa_test_blue(void);
extern void paint_main(void);
extern void piano_main(void);
extern "C" void kmaindb(void);
extern void crashme(void);
void format_disk(void);
extern "C" void init_disk_system(void);
extern "C" int test_disk(void);
extern "C" int identify_drive(uint16_t port);
typedef struct {
    uint16_t port;
    char model[41];
    int present;
    uint32_t total_sectors;
    uint16_t current_dir_sector;
} disk_t;

#define MAX_DISKS 4
disk_t disks[MAX_DISKS];
int current_disk = 0;

void scan_all_disks(void) {
    uint16_t ports[] = {0x1F0, 0x170, 0x1E8, 0x168};
    const char* names[] = {"Primary", "Secondary", "Third", "Fourth"};
    
    for(int i = 0; i < MAX_DISKS; i++) {
        disks[i].port = ports[i];
        disks[i].present = 0;
        
        if(identify_drive(ports[i])) {
            disks[i].present = 1;
            disks[i].current_dir_sector = 100 + i * 100;
            kprint("Found disk on ");
            kprint(names[i]);
            kprint(" port: ");
            kprint(disks[i].model);
            kprint("\n");
        }
    }
}
static char* get_arg(char* cmd) {
    while(*cmd && *cmd != ' ') cmd++;
    if(*cmd == ' ') {
        cmd++;
        while(*cmd == ' ') cmd++;
        return cmd;
    }
    return NULL;
}

static void trim(char* s) {
    int len = my_strlen(s);
    while(len > 0 && (s[len-1] == ' ' || s[len-1] == '\n')) {
        s[len-1] = '\0';
        len--;
    }
}
static int hex_char_to_int(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'A' && c <= 'F') return 10 + (c - 'A');
    if(c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return 0;
}

static char int_to_hex_char(int n) {
    n &= 0x0F;
    if(n < 10) return '0' + n;
    return 'A' + (n - 10);
}
static uint8_t shell_heap[1024 * 1024];
static uint32_t shell_heap_ptr = 0;

static void* shell_malloc(uint32_t size) {
    if(shell_heap_ptr + size > sizeof(shell_heap)) {
        return NULL;
    }
    void* ptr = &shell_heap[shell_heap_ptr];
    shell_heap_ptr += size;
    return ptr;
}

static void shell_free(void* ptr) {
    (void)ptr;
}

static uint16_t current_dir_sector = 100;
void copy_directory_recursive(uint16_t src_sector, uint16_t dst_sector, const char* src_path, const char* dst_path) {
    uint16_t src_buf[256];
    uint16_t dst_buf[256];
    read_sector(src_sector, src_buf);
    
    read_sector(dst_sector, dst_buf);
    
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)src_buf)[i*16 + j];
        
        if(name[0] != 0) {
            int is_dir = ((char*)src_buf)[i*16 + 11] == 1;
            int src_file_sector = src_buf[i*8 + 6];
            int size = src_buf[i*8 + 7];
            
            int dst_slot = -1;
            for(int j = 0; j < 32; j++) {
                char dst_name[12] = {0};
                for(int k = 0; k < 11; k++) dst_name[k] = ((char*)dst_buf)[j*16 + k];
                if(dst_name[0] == 0) {
                    dst_slot = j;
                    break;
                }
            }
            
            if(dst_slot != -1) {
                if(is_dir) {
                    kprint("  [DIR]  ");
                    kprint(name);
                    kprint("\n");
                    
                    for(int j = 0; j < 11 && name[j]; j++) {
                        ((char*)dst_buf)[dst_slot*16 + j] = name[j];
                    }
                    ((char*)dst_buf)[dst_slot*16 + 11] = 1;
                    
                    uint16_t new_folder_buf[256];
                    for(int j = 0; j < 256; j++) new_folder_buf[j] = 0;
                    int new_folder_sector = 3000 + dst_slot;
                    write_sector(new_folder_sector, new_folder_buf);
                    
                    dst_buf[dst_slot*8 + 6] = new_folder_sector;
                    dst_buf[dst_slot*8 + 7] = 0;
                    
                    write_sector(dst_sector, dst_buf);
                    
                    char new_src_path[128];
                    char new_dst_path[128];
                    my_strcpy(new_src_path, src_path);
                    my_strcpy(new_dst_path, dst_path);
                    int len = my_strlen(new_src_path);
                    new_src_path[len] = '/';
                    new_dst_path[len] = '/';
                    my_strcpy(new_src_path + len + 1, name);
                    my_strcpy(new_dst_path + len + 1, name);
                    
                    copy_directory_recursive(src_file_sector, new_folder_sector, new_src_path, new_dst_path);
                    read_sector(dst_sector, dst_buf);
                } else {
                    kprint("  [FILE] ");
                    kprint(name);
                    kprint(" (");
                    kprint_int(size);
                    kprint(" bytes)\n");
                    
                    uint16_t data_buf[256];
                    read_sector(src_file_sector, data_buf);
                    
                    int dest_file_sector = 2000 + dst_slot;
                    write_sector(dest_file_sector, data_buf);
                    
                    for(int j = 0; j < 11 && name[j]; j++) {
                        ((char*)dst_buf)[dst_slot*16 + j] = name[j];
                    }
                    ((char*)dst_buf)[dst_slot*16 + 11] = 0;
                    dst_buf[dst_slot*8 + 6] = dest_file_sector;
                    dst_buf[dst_slot*8 + 7] = size;
                }
            }
        }
    }
    
    write_sector(dst_sector, dst_buf);
}
static int check_user_password(void) {
    char config_pass[64] = {0};
    uint16_t pass_buf[256];
    read_sector(103, pass_buf);
    for(int i = 0; i < 64; i++) {
        config_pass[i] = pass_buf[i/2] >> ((i%2)*8);
        if(config_pass[i] == 0) break;
    }
    
    if(config_pass[0] == 0) {
        return 1;
    }
    
    kprint("Enter password to continue: ");
    char input[64] = {0};
    int pos = 0;
    while(1) {
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc == 0x1C) break;
            if(sc == 0x0E && pos > 0) {
                pos--;
                kprint("\b \b");
            }
            else if(sc >= 0x02 && sc <= 0x0B && pos < 63) {
                input[pos++] = "1234567890"[sc - 0x02];
                kprint("*");
            }
            else if(sc >= 0x10 && sc <= 0x19 && pos < 63) {
                input[pos++] = "qwertyuiop"[sc - 0x10];
                kprint("*");
            }
            else if(sc >= 0x1E && sc <= 0x26 && pos < 63) {
                input[pos++] = "asdfghjkl"[sc - 0x1E];
                kprint("*");
            }
            else if(sc >= 0x2C && sc <= 0x32 && pos < 63) {
                input[pos++] = "zxcvbnm"[sc - 0x2C];
                kprint("*");
            }
        }
    }
    input[pos] = '\0';
    kprint("\n");
    
    if(my_strcmp(input, config_pass) == 0) {
        return 1;
    }
    return 0;
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
            kprint("NOTE: Please wait for the complete formatting.\n");
            kprint("It will take from 15 minutes to 1 hour. Do not turn off the computer\n");
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
}
#ifdef __cplusplus
extern "C" {
#endif

void* memset(void* dest, int c, int n) {
    char* d = (char*)dest;
    for(int i = 0; i < n; i++) d[i] = (char)c;
    return dest;
}

#ifdef __cplusplus
}
#endif
static int show_time_in_shell = 1;
static int last_second = -1;

void update_time_display(void) {
    if(!show_time_in_shell) return;
    
    outb(0x70, 0x04); uint8_t hour = inb(0x71);
    outb(0x70, 0x02); uint8_t minute = inb(0x71);
    outb(0x70, 0x00); uint8_t second = inb(0x71);
    
    hour = ((hour >> 4) * 10) + (hour & 0x0F);
    minute = ((minute >> 4) * 10) + (minute & 0x0F);
    second = ((second >> 4) * 10) + (second & 0x0F);
    
    int old_x = cursor_x, old_y = cursor_y;
    
    char time_str[9];
    if(hour < 10) time_str[0] = '0';
    else time_str[0] = (hour / 10) + '0';
    time_str[1] = (hour % 10) + '0';
    time_str[2] = ':';
    if(minute < 10) time_str[3] = '0';
    else time_str[3] = (minute / 10) + '0';
    time_str[4] = (minute % 10) + '0';
    time_str[5] = ':';
    if(second < 10) time_str[6] = '0';
    else time_str[6] = (second / 10) + '0';
    time_str[7] = (second % 10) + '0';
    time_str[8] = '\0';
    
    kprint_at(time_str, 70, 0, (COLOR_BLACK << 4) | TXT_GREEN);
    
    move_cursor(old_x, old_y);
}

    static const float sin_tab[91] = {
        0.000f,0.017f,0.035f,0.052f,0.070f,0.087f,0.105f,0.122f,0.139f,0.156f,
        0.174f,0.191f,0.208f,0.225f,0.242f,0.259f,0.276f,0.292f,0.309f,0.326f,
        0.342f,0.358f,0.375f,0.391f,0.407f,0.423f,0.438f,0.454f,0.469f,0.485f,
        0.500f,0.515f,0.530f,0.545f,0.559f,0.574f,0.588f,0.602f,0.616f,0.629f,
        0.643f,0.656f,0.669f,0.682f,0.695f,0.707f,0.719f,0.731f,0.743f,0.755f,
        0.766f,0.777f,0.788f,0.799f,0.809f,0.819f,0.829f,0.839f,0.848f,0.857f,
        0.866f,0.875f,0.883f,0.891f,0.899f,0.906f,0.914f,0.921f,0.927f,0.934f,
        0.940f,0.946f,0.951f,0.956f,0.961f,0.966f,0.970f,0.974f,0.978f,0.982f,
        0.985f,0.988f,0.990f,0.993f,0.995f,0.996f,0.998f,0.999f,0.999f,1.000f,
        1.000f
    };
    
    static float SIN(float x) {
        int deg = (int)(x * 57.2958f) % 360;
        if(deg < 0) deg += 360;
        if(deg <= 90) return sin_tab[deg];
        if(deg <= 180) return sin_tab[180 - deg];
        if(deg <= 270) return -sin_tab[deg - 180];
        return -sin_tab[360 - deg];
    }
    
    static float COS(float x) {
        return SIN(x + 1.5708f);
    }
void apple_3d_demo(void) {
    vga_init();

    for(int i = 0; i < 64; i++) {
        vga_palette(i, i, 0, 0);
    }
    for(int i = 64; i < 80; i++) {
        vga_palette(i, 63, (i-64)*4, 0);
    }
    for(int i = 80; i < 96; i++) {
        vga_palette(i, 0, 42, 0);
    }
    for(int i = 96; i < 112; i++) {
        vga_palette(i, 32, 16, 0);
    }
    for(int i = 112; i < 128; i++) {
        vga_palette(i, 21, 21, 42);
    }
    

    
    typedef struct { float x,y,z; } vec3;
    
    static vec3 sphere_verts[162];
    static float sphere_r[162];
    static int sphere_count = 0;
    
    for(int lat = 0; lat < 9; lat++) {
        for(int lon = 0; lon < 18; lon++) {
            float phi = lat * 3.14159f / 8.0f;
            float theta = lon * 6.28318f / 18.0f;
            
            float r = 25.0f;
            if(phi < 0.6f) r -= (0.6f - phi) * 22.0f;
            if(phi > 2.4f) r += (phi - 2.4f) * 18.0f;
            
            sphere_verts[sphere_count].x = r * SIN(phi) * COS(theta);
            sphere_verts[sphere_count].y = r * SIN(phi) * SIN(theta);
            sphere_verts[sphere_count].z = r * COS(phi);
            sphere_r[sphere_count] = r;
            sphere_count++;
        }
    }
    
    int running = 1;
    float rot_x = 0.3f, rot_y = 0.0f, rot_z = 0.2f;
    int frame = 0;
    
    while(running) {
        while(vga_inb(0x64) & 1) {
            uint8_t sc = vga_inb(0x60);
            if(sc < 0x80) {
                if(sc == 0x48) rot_x += 0.15f;
                if(sc == 0x50) rot_x -= 0.15f;
                if(sc == 0x4B) rot_y -= 0.15f;
                if(sc == 0x4D) rot_y += 0.15f;
                if(sc == 0x01) running = 0;
            }
        }
        
        rot_y += 0.04f;
        
        for(int i = 0; i < 64000; i++) vga_backbuffer[i] = 112 + (i/320/15);
        
        static float zbuf[320];
        for(int x = 0; x < 320; x++) zbuf[x] = 9999.0f;
        
        float sx = SIN(rot_x), cx = COS(rot_x);
        float sy = SIN(rot_y), cy = COS(rot_y);
        float sz = SIN(rot_z), cz = COS(rot_z);
        
        for(int lat = 0; lat < 8; lat++) {
            for(int lon = 0; lon < 18; lon++) {
                int a = lat * 18 + lon;
                int b = lat * 18 + (lon + 1) % 18;
                int c = (lat + 1) * 18 + lon;
                int d = (lat + 1) * 18 + (lon + 1) % 18;
                
                vec3 p[3];
                p[0] = sphere_verts[a]; p[1] = sphere_verts[b]; p[2] = sphere_verts[c];
                
                float z_avg = 0;
                int sx_p[3], sy_p[3];
                
                for(int k = 0; k < 3; k++) {
                    float x = p[k].x, y = p[k].y, z = p[k].z;

                    float y1 = y*cx - z*sx;
                    float z1 = y*sx + z*cx;
                    float x1 = x*cy + z1*sy;
                    float z2 = -x*sy + z1*cy;
                    float x2 = x1*cz - y1*sz;
                    float y2 = x1*sz + y1*cz;
                    
                    float scale = 100.0f / (z2 + 70.0f);
                    sx_p[k] = (int)(x2 * scale) + 160;
                    sy_p[k] = (int)(y2 * scale) + 100;
                    z_avg += z2;
                }
                z_avg /= 3.0f;
                
                float light = (z_avg + 25.0f) / 50.0f;
                if(light < 0.2f) light = 0.2f;
                if(light > 1.0f) light = 1.0f;
                
                uint8_t color = (uint8_t)(light * 58.0f);
                if(light > 0.75f) color = 64 + (uint8_t)((light - 0.75f) * 60.0f);
                
                int min_y = sy_p[0], max_y = sy_p[0];
                for(int k = 1; k < 3; k++) {
                    if(sy_p[k] < min_y) min_y = sy_p[k];
                    if(sy_p[k] > max_y) max_y = sy_p[k];
                }
                
                for(int py = min_y; py <= max_y; py++) {
                    if(py < 0 || py >= 200) continue;
                    
                    int min_x = 320, max_x = 0;
                    for(int e = 0; e < 3; e++) {
                        int nxt = (e+1)%3;
                        if((sy_p[e] <= py && sy_p[nxt] > py) || 
                           (sy_p[nxt] <= py && sy_p[e] > py)) {
                            if(sy_p[nxt] != sy_p[e]) {
                                int ix = sx_p[e] + (py - sy_p[e]) * 
                                         (sx_p[nxt] - sx_p[e]) / (sy_p[nxt] - sy_p[e]);
                                if(ix < min_x) min_x = ix;
                                if(ix > max_x) max_x = ix;
                            }
                        }
                    }
                    
                    if(min_x < 0) min_x = 0;
                    if(max_x >= 320) max_x = 319;
                    
                    for(int px = min_x; px <= max_x; px++) {
                        if(z_avg < zbuf[px]) {
                            zbuf[px] = z_avg;
                            vga_buf_pixel(px, py, color);
                        }
                    }
                }
                
                p[0] = sphere_verts[b]; p[1] = sphere_verts[d]; p[2] = sphere_verts[c];
                
                z_avg = 0;
                for(int k = 0; k < 3; k++) {
                    float x = p[k].x, y = p[k].y, z = p[k].z;
                    float y1 = y*cx - z*sx;
                    float z1 = y*sx + z*cx;
                    float x1 = x*cy + z1*sy;
                    float z2 = -x*sy + z1*cy;
                    float x2 = x1*cz - y1*sz;
                    float y2 = x1*sz + y1*cz;
                    
                    float scale = 100.0f / (z2 + 70.0f);
                    sx_p[k] = (int)(x2 * scale) + 160;
                    sy_p[k] = (int)(y2 * scale) + 100;
                    z_avg += z2;
                }
                z_avg /= 3.0f;
                
                float light2 = (z_avg + 25.0f) / 50.0f;
                if(light2 < 0.2f) light2 = 0.2f;
                if(light2 > 1.0f) light2 = 1.0f;
                
                uint8_t color2 = (uint8_t)(light2 * 58.0f);
                if(light2 > 0.75f) color2 = 64 + (uint8_t)((light2 - 0.75f) * 60.0f);
                
                min_y = sy_p[0]; max_y = sy_p[0];
                for(int k = 1; k < 3; k++) {
                    if(sy_p[k] < min_y) min_y = sy_p[k];
                    if(sy_p[k] > max_y) max_y = sy_p[k];
                }
                
                for(int py = min_y; py <= max_y; py++) {
                    if(py < 0 || py >= 200) continue;
                    
                    int min_x = 320, max_x = 0;
                    for(int e = 0; e < 3; e++) {
                        int nxt = (e+1)%3;
                        if((sy_p[e] <= py && sy_p[nxt] > py) || 
                           (sy_p[nxt] <= py && sy_p[e] > py)) {
                            if(sy_p[nxt] != sy_p[e]) {
                                int ix = sx_p[e] + (py - sy_p[e]) * 
                                         (sx_p[nxt] - sx_p[e]) / (sy_p[nxt] - sy_p[e]);
                                if(ix < min_x) min_x = ix;
                                if(ix > max_x) max_x = ix;
                            }
                        }
                    }
                    
                    if(min_x < 0) min_x = 0;
                    if(max_x >= 320) max_x = 319;
                    
                    for(int px = min_x; px <= max_x; px++) {
                        if(z_avg < zbuf[px]) {
                            zbuf[px] = z_avg;
                            vga_buf_pixel(px, py, color2);
                        }
                    }
                }
            }
        }
        
        vec3 leaf[3] = {{0, -15, 20}, {-5, -18, 18}, {5, -18, 18}};
        int lsx[3], lsy[3];
        for(int k = 0; k < 3; k++) {
            float x = leaf[k].x, y = leaf[k].y, z = leaf[k].z;
            float y1 = y*cx - z*sx;
            float z1 = y*sx + z*cx;
            float x1 = x*cy + z1*sy;
            float z2 = -x*sy + z1*cy;
            float x2 = x1*cz - y1*sz;
            float y2 = x1*sz + y1*cz;
            float scale = 100.0f / (z2 + 70.0f);
            lsx[k] = (int)(x2 * scale) + 160;
            lsy[k] = (int)(y2 * scale) + 100;
        }
        int lmin_y = lsy[0], lmax_y = lsy[0];
        for(int k = 1; k < 3; k++) { if(lsy[k]<lmin_y)lmin_y=lsy[k]; if(lsy[k]>lmax_y)lmax_y=lsy[k]; }
        for(int py = lmin_y; py <= lmax_y; py++) {
            if(py < 0 || py >= 200) continue;
            int min_x = 320, max_x = 0;
            for(int e = 0; e < 3; e++) {
                int nxt = (e+1)%3;
                if((lsy[e]<=py&&lsy[nxt]>py)||(lsy[nxt]<=py&&lsy[e]>py)) {
                    if(lsy[nxt]!=lsy[e]) {
                        int ix = lsx[e]+(py-lsy[e])*(lsx[nxt]-lsx[e])/(lsy[nxt]-lsy[e]);
                        if(ix<min_x)min_x=ix; if(ix>max_x)max_x=ix;
                    }
                }
            }
            if(min_x<0)min_x=0; if(max_x>=320)max_x=319;
            for(int px = min_x; px <= max_x; px++) vga_buf_pixel(px, py, 85);
        }
        
        for(float t = 0; t < 1.0f; t += 0.1f) {
            float x = 0, y = -20.0f + t*5.0f, z = 25.0f - t*3.0f;
            float y1 = y*cx - z*sx;
            float z1 = y*sx + z*cx;
            float x1 = x*cy + z1*sy;
            float z2 = -x*sy + z1*cy;
            float x2 = x1*cz - y1*sz;
            float y2 = x1*sz + y1*cz;
            float scale = 100.0f / (z2 + 70.0f);
            int sx = (int)(x2*scale) + 160;
            int sy = (int)(y2*scale) + 100;
            if(sx>=0&&sx<320&&sy>=0&&sy<200) {
                vga_buf_pixel(sx, sy, 100);
                vga_buf_pixel(sx+1, sy, 100);
            }
        }
        
        vga_text(5, 5, "3D APPLE", 0x0F);
        vga_text(5, 15, "Arrows:Rotate  ESC:Exit", 0x0A);
        
        vga_vsync();
        vga_flip();
        
        frame++;
        for(volatile int d = 0; d < 15000; d++);
    }
    
    vga_exit();
}
extern "C" void process_command(char* input_buffer, int& input_ptr) {
    char original_cmd[256];
    int orig_len = input_ptr;
    for(int i = 0; i < orig_len && i < 255; i++) {
        original_cmd[i] = input_buffer[i];
    }
    original_cmd[orig_len] = '\0';
    
    input_buffer[input_ptr] = '\0';

    if(input_ptr == 0) {
        update_time_display();
        kprint("\n");
        history_add(input_buffer);
        kprint_color("root@wnka> ", TXT_GREEN);
        return;
    }
    
    char cmd_copy[256];
    int i;
    for(i = 0; i < input_ptr; i++) cmd_copy[i] = input_buffer[i];
    cmd_copy[i] = '\0';
    
    input_ptr = 0;
    
    char* arg = (char*)""; 
    int space_idx = -1;
    for(int i = 0; cmd_copy[i]; i++) {
        if(cmd_copy[i] == ' ') {
            space_idx = i;
            break;
        }
    }
    
    if (space_idx != -1) {
        cmd_copy[space_idx] = '\0';
        arg = &cmd_copy[space_idx + 1];
    }
    
    kprint("\n");

    int cmd_len = 0;
    while(cmd_copy[cmd_len]) cmd_len++;
    
    if(cmd_len > 4 && 
       cmd_copy[cmd_len-4] == '.' && 
       cmd_copy[cmd_len-3] == 'w' && 
       cmd_copy[cmd_len-2] == 'n' && 
       cmd_copy[cmd_len-1] == 'c') {
        
        kprint_color("[AUTO] Running: ", TXT_CYAN);
        kprint(cmd_copy);
        kprint("\n");
        
        uint16_t saved_dir = current_dir_sector;
        
        uint16_t dir_buf[256];
        read_sector(current_dir_sector, dir_buf);
        
        int found = 0;
        for(int i = 0; i < 32; i++) {
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
            if(my_strcmp(cmd_copy, name) == 0) {
                found = 1;
                break;
            }
        }
        
        if(found) {
            wnc_set_dir(current_dir_sector);
            wnc_execute_file(cmd_copy);
        } else {
            kprint_color("[AUTO] Script not found: ", TXT_RED);
            kprint(cmd_copy);
            kprint("\n");
        }
        
        kprint_color("root@wnka> ", TXT_GREEN);
        return;
    }

if (my_strcmp(input_buffer, "help") == 0) {
    int current_page = 0;
    int total_pages = 5;
    int running = 1;
    
    while(running) {
        clear_screen_bg(BLACK);
        
        kprint_at("========================================", 20, 1, (BLACK << 4) | TXT_YELLOW);
        kprint_at("     WNKA OS COMMANDS v1.0", 25, 2, (BLACK << 4) | TXT_CYAN);
        kprint_at("========================================", 20, 3, (BLACK << 4) | TXT_YELLOW);
        
        if(current_page == 0) {
            kprint_at("== SYSTEM ==", 30, 5, (BLACK << 4) | TXT_GREEN);
            kprint_at("  help     - this help", 20, 7, (BLACK << 4) | TXT_WHITE);
            kprint_at("  cls      - clear screen", 20, 8, (BLACK << 4) | TXT_WHITE);
            kprint_at("  reboot   - restart system", 20, 9, (BLACK << 4) | TXT_WHITE);
            kprint_at("  shut     - power off", 20, 10, (BLACK << 4) | TXT_WHITE);
            kprint_at("  halt     - halt system", 20, 11, (BLACK << 4) | TXT_WHITE);
            kprint_at("  about    - system info", 20, 12, (BLACK << 4) | TXT_WHITE);
            kprint_at("  fetch    - show PC info", 20, 13, (BLACK << 4) | TXT_WHITE);
            kprint_at("  beep     - make beep sound", 20, 14, (BLACK << 4) | TXT_WHITE);
            kprint_at("  say      - repeat text", 20, 15, (BLACK << 4) | TXT_WHITE);
            kprint_at("  hello    - hello world", 20, 16, (BLACK << 4) | TXT_WHITE);
            kprint_at("  sleep <n>- pause for N seconds", 20, 17, (BLACK << 4) | TXT_WHITE);
            kprint_at("  crashme  - test system stability", 20, 18, (BLACK << 4) | TXT_WHITE);
            kprint_at("  panic    - kernel panic", 20, 19, (BLACK << 4) | TXT_RED);
            kprint_at("  time     - show current time", 20, 20, (BLACK << 4) | TXT_WHITE);
        }
        
        else if(current_page == 1) {
            kprint_at("== FILE & DISK ==", 28, 5, (BLACK << 4) | TXT_GREEN);
            kprint_at("  ls       - list files", 20, 7, (BLACK << 4) | TXT_WHITE);
            kprint_at("  cd <dir> - change directory", 20, 8, (BLACK << 4) | TXT_WHITE);
            kprint_at("  cat <f>  - show file", 20, 9, (BLACK << 4) | TXT_WHITE);
            kprint_at("  create <f>- create file", 20, 10, (BLACK << 4) | TXT_WHITE);
            kprint_at("  delete <f>   - delete file", 20, 11, (BLACK << 4) | TXT_WHITE);
            kprint_at("  mkdir <d>- create directory", 20, 12, (BLACK << 4) | TXT_WHITE);
            kprint_at("  rmdir <d>- remove directory", 20, 13, (BLACK << 4) | TXT_WHITE);
            kprint_at("  hexdump <f>- hex view", 20, 20, (BLACK << 4) | TXT_WHITE);
        }
        
        else if(current_page == 2) {
            kprint_at("== GAMES & GRAPHICS ==", 26, 5, (BLACK << 4) | TXT_GREEN);
            kprint_at("  pacman   - Pacman game", 20, 7, (BLACK << 4) | TXT_WHITE);
            kprint_at("  flappy   - Flappy Bird", 20, 8, (BLACK << 4) | TXT_WHITE);
            kprint_at("  snake    - Snake game", 20, 9, (BLACK << 4) | TXT_WHITE);
            kprint_at("  matrix   - Matrix rain", 20, 10, (BLACK << 4) | TXT_WHITE);
            kprint_at("  fire     - Fire effect", 20, 11, (BLACK << 4) | TXT_WHITE);
            kprint_at("  rain     - ASCII rain", 20, 12, (BLACK << 4) | TXT_WHITE);
            kprint_at("  stars    - Starfield", 20, 13, (BLACK << 4) | TXT_WHITE);
            kprint_at("  plasma   - Plasma effect", 20, 14, (BLACK << 4) | TXT_WHITE);
            kprint_at("  waves    - Waves effect", 20, 15, (BLACK << 4) | TXT_WHITE);
            kprint_at("  tunnel   - 3D tunnel", 20, 16, (BLACK << 4) | TXT_WHITE);
            kprint_at("  menus    - effects menu", 20, 17, (BLACK << 4) | TXT_WHITE);
            kprint_at("  art      - ASCII gallery", 20, 18, (BLACK << 4) | TXT_WHITE);
            kprint_at("  vga      - VGA 256 color mode", 20, 19, (BLACK << 4) | TXT_WHITE);
            kprint_at("  vesablue - VESA blue screen", 20, 20, (BLACK << 4) | TXT_WHITE);
            kprint_at("  screensaver- screen saver", 20, 21, (BLACK << 4) | TXT_WHITE);
        }

        else if(current_page == 3) {
            kprint_at("== APPS & TOOLS ==", 28, 5, (BLACK << 4) | TXT_GREEN);
            kprint_at("  calc     - text calculator", 20, 7, (BLACK << 4) | TXT_WHITE);
            kprint_at("  galc     - graphic calculator", 20, 8, (BLACK << 4) | TXT_WHITE);
            kprint_at("  ide      - text editor", 20, 9, (BLACK << 4) | TXT_WHITE);
            kprint_at("  tcc      - C compiler", 20, 10, (BLACK << 4) | TXT_WHITE);
            kprint_at("  paint    - drawing program", 20, 11, (BLACK << 4) | TXT_WHITE);
            kprint_at("  piano    - PC speaker piano", 20, 12, (BLACK << 4) | TXT_WHITE);
            kprint_at("  ui       - desktop environment", 20, 13, (BLACK << 4) | TXT_WHITE);
            kprint_at("  gui      - GUI test", 20, 14, (BLACK << 4) | TXT_WHITE);
            kprint_at("  ramfs    - RAM filesystem", 20, 15, (BLACK << 4) | TXT_WHITE);
            kprint_at("  fm       - file manager", 20, 16, (BLACK << 4) | TXT_WHITE);
            kprint_at("  clock    - ASCII clock", 20, 17, (BLACK << 4) | TXT_WHITE);
            kprint_at("  monitor  - resource monitor", 20, 18, (BLACK << 4) | TXT_WHITE);
            kprint_at("  theme    - change theme", 20, 19, (BLACK << 4) | TXT_WHITE);
            kprint_at("  sound    - sound commands", 20, 20, (BLACK << 4) | TXT_WHITE);
            kprint_at("  install  - install system", 20, 21, (BLACK << 4) | TXT_WHITE);
        }
        

        else if(current_page == 4) {
            kprint_at("== NET & MULTITASK ==", 26, 5, (BLACK << 4) | TXT_GREEN);
            kprint_at("  netinit  - init network", 20, 7, (BLACK << 4) | TXT_WHITE);
            kprint_at("  ping     - ping pong", 20, 8, (BLACK << 4) | TXT_WHITE);
            kprint_at("  dns      - DNS resolve", 20, 9, (BLACK << 4) | TXT_WHITE);
            kprint_at("  browse   - web browser", 20, 10, (BLACK << 4) | TXT_WHITE);
            kprint_at("  http     - HTTP request", 20, 11, (BLACK << 4) | TXT_WHITE);
            kprint_at("  netstat  - network status", 20, 12, (BLACK << 4) | TXT_WHITE);
            kprint_at("  ps       - process list", 20, 13, (BLACK << 4) | TXT_WHITE);
            kprint_at("  kill <p> - kill process", 20, 14, (BLACK << 4) | TXT_WHITE);
            kprint_at("  multitest- multitasking test", 20, 15, (BLACK << 4) | TXT_WHITE);
            kprint_at("  bg <cmd> - background command", 20, 16, (BLACK << 4) | TXT_WHITE);
            kprint_at("  jobs     - list background", 20, 17, (BLACK << 4) | TXT_WHITE);
            kprint_at("  fg <id>  - foreground job", 20, 18, (BLACK << 4) | TXT_WHITE);
            kprint_at("  bgkill <id>- kill background", 20, 19, (BLACK << 4) | TXT_WHITE);
            kprint_at("  copy     - copy to clipboard", 20, 20, (BLACK << 4) | TXT_WHITE);
            kprint_at("  paste    - paste from clipboard", 20, 21, (BLACK << 4) | TXT_WHITE);
        }

        kprint_at("========================================", 20, 22, (BLACK << 4) | TXT_YELLOW);
        kprint_at("Page: ", 30, 23, (BLACK << 4) | TXT_CYAN);
        kprint_int_at(current_page + 1, 36, 23, (BLACK << 4) | TXT_GREEN);
        kprint_at("/", 38, 23, (BLACK << 4) | TXT_WHITE);
        kprint_int_at(total_pages, 40, 23, (BLACK << 4) | TXT_GREEN);
        
        kprint_at("[L] Prev  [R] Next  [ESC] Exit", 25, 24, (BLACK << 4) | TXT_YELLOW);

        int key_processed = 0;
        while(!key_processed) {
            if(inb(0x64) & 1) {
                uint8_t key = inb(0x60);
                
                if(key < 0x80) {
                    if(key == 0x26) {
                        if(current_page > 0) {
                            current_page--;
                        }
                        key_processed = 1;
                    }
                    else if(key == 0x13) {
                        if(current_page < total_pages - 1) {
                            current_page++;
                        }
                        key_processed = 1;
                    }
                    else if(key == 0x01) {
                        running = 0;
                        key_processed = 1;
                    }
                }
                while(inb(0x64) & 1) inb(0x60);
            }
        }
    }
    
    clear_screen();
}

else if(my_strcmp(cmd_copy, "vesaui") == 0) {
    wn_demo();
}
else if(my_strcmp(cmd_copy, "linux_init") == 0) {
    linux_syscall_init();
    kprint_color("[SHELL] Linux compatibility layer ready\n", TXT_GREEN);
    kprint_color("        Now you can run Linux ELF binaries!\n", TXT_CYAN);
    kprint_color("        Example: linux_run /usr/lib/wine/wine\n", TXT_CYAN);
}

else if(my_strcmp(cmd_copy, "linux_run") == 0) {
    if(arg[0] == '\0') {
        kprint_color("Usage: linux_run <elf_file>\n", TXT_YELLOW);
        kprint_color("Example: linux_run /bin/busybox\n", TXT_YELLOW);
        kprint_color("         linux_run /usr/lib/wine/wine notepad.exe\n", TXT_YELLOW);
    } else {
        kprint_color("[LINUX] Loading ELF: ", TXT_CYAN);
        kprint(arg);
        kprint("\n");
        
        char* argv[16];
        int argc = 0;
        argv[0] = arg;
        argc = 1;
        
        char* p = arg;
        while(*p && *p != ' ') p++;
        while(*p == ' ') {
            *p = '\0';
            p++;
            while(*p == ' ') p++;
            if(*p && argc < 15) {
                argv[argc++] = p;
                while(*p && *p != ' ') p++;
            }
        }
        argv[argc] = 0;
        
        kprint_color("[LINUX] Arguments: ", TXT_CYAN);
        for(int i = 0; i < argc; i++) {
            kprint(argv[i]);
            kprint(" ");
        }
        kprint("\n");
        
        int result = elf_execve(argv[0], argv, 0);
        
        if(result != 0) {
            kprint_color("[LINUX] Failed to execute: ", TXT_RED);
            kprint(arg);
            kprint("\n");
            kprint_color("        Make sure linux_init was called first!\n", TXT_YELLOW);
        }
    }
}

else if(my_strcmp(cmd_copy, "linux_wine") == 0) {
    if(arg[0] == '\0') {
        kprint_color("Usage: linux_wine <windows_program.exe>\n", TXT_YELLOW);
        kprint_color("Example: linux_wine notepad.exe\n", TXT_YELLOW);
        kprint_color("         linux_wine C:\\windows\\system32\\calc.exe\n", TXT_YELLOW);
    } else {
        kprint_color("         WINE LAUNCHER FOR WNKA             \n", TXT_CYAN);
        
        kprint_color("[WINE] Starting Wine for: ", TXT_GREEN);
        kprint(arg);
        kprint("\n");
        char* wine_argv[4];
        wine_argv[0] = (char*)"/usr/lib/wine/wine";
        wine_argv[1] = arg;
        wine_argv[2] = 0;
        
        kprint("[WINE] Command: ");
        kprint(wine_argv[0]);
        kprint(" ");
        kprint(wine_argv[1]);
        kprint("\n\n");
        
        
        int result = elf_execve(wine_argv[0], wine_argv, 0);
        
        
        if(result != 0) {
            kprint_color("\n[WINE] Failed to start Wine!\n", TXT_RED);
            kprint_color("       Make sure:\n", TXT_YELLOW);
            kprint_color("       1. Wine is installed in /usr/lib/wine/\n", TXT_YELLOW);
            kprint_color("       2. linux_init was called\n", TXT_YELLOW);
            kprint_color("       3. The Windows .exe file exists\n", TXT_YELLOW);
        }
    }
}

else if(my_strcmp(cmd_copy, "linux_ls") == 0) {
    const char* dir = arg[0] ? arg : "/";
    
    kprint_color("[VFS] Listing: ", TXT_CYAN);
    kprint(dir);
    kprint("\n\n");
    
    int fd = vfs_open(dir, 0, 0);
    if(fd >= 0) {
        struct vfs_dirent_t dirent;
        int count = vfs_getdents(fd, &dirent, sizeof(dirent));
        
        if(count <= 0) {
            kprint_color("  (empty or not a directory)\n", TXT_YELLOW);
        }
        
        vfs_close(fd);
    } else {
        kprint_color("  Cannot open directory\n", TXT_RED);
    }
}

else if(my_strcmp(cmd_copy, "linux_stat") == 0) {
    if(arg[0] == '\0') {
        kprint_color("Usage: linux_stat <path>\n", TXT_YELLOW);
    } else {
        struct vfs_stat_t st;
        int result = vfs_stat(arg, &st);
        
        if(result == 0) {
            kprint_color("\n=== STAT: ", TXT_CYAN);
            kprint(arg);
            kprint_color(" ===\n", TXT_CYAN);
            
            kprint("Size: ");
            kprint_int(st.st_size);
            kprint(" bytes\n");
            
            kprint("Mode: 0");
            kprint_int(st.st_mode);
            kprint("\n");
            
            kprint("Links: ");
            kprint_int(st.st_nlink);
            kprint("\n");
            
            kprint("UID: ");
            kprint_int(st.st_uid);
            kprint("  GID: ");
            kprint_int(st.st_gid);
            kprint("\n");
            
            kprint("============================\n");
        } else {
            kprint_color("Cannot stat: ", TXT_RED);
            kprint(arg);
            kprint("\n");
        }
    }
}

else if(my_strcmp(cmd_copy, "linux_info") == 0) {
    kprint_color("\n╔════════════════════════════════════════════╗\n", TXT_CYAN);
    kprint_color("║     LINUX COMPATIBILITY LAYER STATUS       ║\n", TXT_CYAN);
    kprint_color("╚════════════════════════════════════════════╝\n\n", TXT_CYAN);
    
    kprint_color("[VFS] Virtual File System: ", TXT_GREEN);
    kprint_color("ACTIVE\n", TXT_GREEN);
    
    kprint_color("[ELF] ELF Loader: ", TXT_GREEN);
    kprint_color("READY\n", TXT_GREEN);
    
    kprint_color("[SYSCALL] Linux Syscalls: ", TXT_GREEN);
    kprint_color("EMULATED\n", TXT_GREEN);
    
    kprint_color("\nSupported syscalls:\n", TXT_WHITE);
    kprint("  open, read, write, close, lseek\n");
    kprint("  stat, fstat, lstat, access\n");
    kprint("  brk, mmap, munmap\n");
    kprint("  exit, execve, getpid, getuid\n");
    kprint("  uname, time, times\n");
    kprint("  rename, mkdir, rmdir, unlink\n");
    kprint("  ioctl, fcntl, writev, pipe\n");
    
    kprint_color("\nWine support: ", TXT_YELLOW);
    kprint_color("BASIC (console apps)\n", TXT_YELLOW);
    
    kprint_color("\nUse 'linux_run <elf>' to execute Linux binaries\n", TXT_CYAN);
    kprint_color("Use 'linux_wine <exe>' to run Windows programs\n", TXT_CYAN);
}

else if(my_strcmp(cmd_copy, "linux_help") == 0) {
    kprint_color("\n=== LINUX COMPATIBILITY COMMANDS ===\n\n", TXT_CYAN);
    
    kprint_color("linux_init", TXT_GREEN);
    kprint("       - Initialize Linux compatibility layer\n");
    
    kprint_color("linux_run <elf>", TXT_GREEN);
    kprint("   - Run a Linux ELF executable\n");
    kprint("                   Example: linux_run /bin/busybox\n");
    
    kprint_color("linux_wine <exe>", TXT_GREEN);
    kprint("  - Run Windows .exe through Wine\n");
    kprint("                   Example: linux_wine notepad.exe\n");
    
    kprint_color("linux_ls [dir]", TXT_GREEN);
    kprint("    - List files in VFS directory\n");
    
    kprint_color("linux_stat <path>", TXT_GREEN);
    kprint(" - Show file information\n");
    
    kprint_color("linux_info", TXT_GREEN);
    kprint("       - Show compatibility layer status\n");
    
    kprint_color("linux_help", TXT_GREEN);
    kprint("       - This help message\n");
    
    kprint_color("\nQuick Start:\n", TXT_YELLOW);
    kprint_color("  1. linux_init\n", TXT_WHITE);
    kprint_color("  2. linux_wine your_app.exe\n", TXT_WHITE);
    kprint_color("\n", 0x07);
}
else if(my_strcmp(cmd_copy, "runelf205") == 0) {
    kprint_color("[ELF] Loading ELF from sector 205 with proper parsing...\n", TXT_CYAN);
    
    static uint8_t elf_data[10240];
    int total_bytes = 0;
    
    for(int sec = 205; sec < 225; sec++) {
        uint16_t sec_buf[256];
        read_sector(sec, sec_buf);
        
        for(int i = 0; i < 256 && total_bytes < 10240; i++) {
            elf_data[total_bytes++] = sec_buf[i] & 0xFF;
            elf_data[total_bytes++] = (sec_buf[i] >> 8) & 0xFF;
        }
    }
    
    kprint("[ELF] Read ");
    kprint_int(total_bytes);
    kprint(" bytes\n");
    
    linux_syscall_init();
    
    int fd = vfs_open("/tmp/elf205", 0x41, 0755);
    if(fd >= 0) {
        vfs_write(fd, elf_data, total_bytes);
        vfs_close(fd);
        
        kprint_color("[ELF] Written to /tmp/elf205\n", TXT_GREEN);
        kprint_color("[ELF] Executing via Linux layer...\n", TXT_YELLOW);
        
        char* argv[] = {(char*)"/tmp/elf205", 0};
        elf_execve("/tmp/elf205", argv, 0);
        
        kprint_color("[ELF] Program finished\n", TXT_GREEN);
    } else {
        kprint_color("[ELF] Cannot create temp file\n", TXT_RED);
    }
}
else if(my_strcmp(cmd_copy, "safe205") == 0) {
    kprint_color("\n=== SAFE ELF LOADER ===\n\n", TXT_CYAN);
    
    uint16_t buf[256];
    read_sector(205, buf);
    
    uint8_t elf_hdr[52];
    for(int i = 0; i < 26; i++) {
        elf_hdr[i*2] = buf[i] & 0xFF;
        elf_hdr[i*2+1] = (buf[i] >> 8) & 0xFF;
    }
    
    if(elf_hdr[0] != 0x7F || elf_hdr[1] != 'E' || 
       elf_hdr[2] != 'L' || elf_hdr[3] != 'F') {
        kprint_color("Not an ELF file!\n", TXT_RED);
        return;
    }
    
    uint32_t entry = *(uint32_t*)(elf_hdr + 24);
    
    uint32_t phoff = *(uint32_t*)(elf_hdr + 28);
    uint16_t phnum = *(uint16_t*)(elf_hdr + 44);
    
    kprint("Entry: 0x");
    kprint_hex32(entry);
    kprint("\n");
    kprint("PH: ");
    kprint_int(phnum);
    kprint(" at offset 0x");
    kprint_hex32(phoff);
    kprint("\n\n");
    
    static uint8_t prog[32768];
    uint32_t prog_size = 0;
    
    for(int sec = 205; sec < 270; sec++) {
        uint16_t sb[256];
        read_sector(sec, sb);
        
        for(int i = 0; i < 256 && prog_size < 32768; i++) {
            prog[prog_size++] = sb[i] & 0xFF;
            prog[prog_size++] = (sb[i] >> 8) & 0xFF;
        }
        if(prog_size >= 52) {
        }
    }
    
    kprint("Total program size: ");
    kprint_int(prog_size);
    kprint(" bytes\n\n");
    uint32_t load_addr = 0x08048000;
    uint8_t* mem = (uint8_t*)load_addr;
    for(uint32_t i = 0; i < prog_size && i < 32000; i++) {
        mem[i] = prog[i];
    }
    
    kprint_color("Program loaded to 0x08048000\n", TXT_GREEN);
    kprint_color("Trying to execute at entry point...\n\n", TXT_YELLOW);

    void (*func)() = (void(*)())(load_addr + (entry & 0xFFF));
    
    kprint("Press any key to execute (or wait 3 sec)...\n");
    
    for(int i = 0; i < 300; i++) {
        if(inb(0x64) & 1) {
            inb(0x60);
            break;
        }
        for(volatile int d = 0; d < 10000; d++);
    }
    
    kprint_color("Executing now...\n\n", TXT_RED);
    
    func();
    
    kprint_color("Program returned successfully!\n", TXT_GREEN);
}
else if(my_strcmp(cmd_copy, "elfinfo205") == 0) {
    kprint_color("\n=== ELF ANALYSIS: SECTOR 205 ===\n\n", TXT_CYAN);
    
    uint16_t buf[256];
    read_sector(205, buf);
    
    uint8_t magic[4];
    magic[0] = buf[0] & 0xFF;
    magic[1] = (buf[0] >> 8) & 0xFF;
    magic[2] = buf[1] & 0xFF;
    magic[3] = (buf[1] >> 8) & 0xFF;
    
    if(magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F') {
        kprint_color("OK: Valid ELF file\n\n", TXT_GREEN);
        
        uint8_t elf_header[52];
        for(int i = 0; i < 26 && i < 256; i++) {
            elf_header[i*2] = buf[i] & 0xFF;
            elf_header[i*2+1] = (buf[i] >> 8) & 0xFF;
        }
        
        uint16_t e_type = elf_header[16] | (elf_header[17] << 8);
        kprint("Type: ");
        switch(e_type) {
            case 0: kprint("NONE\n"); break;
            case 1: kprint("REL (Relocatable)\n"); break;
            case 2: kprint_color("EXEC (Executable)\n", TXT_GREEN); break;
            case 3: kprint_color("DYN (Shared Object)\n", TXT_YELLOW); break;
            case 4: kprint("CORE\n"); break;
            default: 
                kprint_int(e_type); 
                kprint("\n"); 
                break;
        }
        
        uint16_t e_machine = elf_header[18] | (elf_header[19] << 8);
        kprint("Machine: ");
        switch(e_machine) {
            case 3: kprint("i386 (x86 32-bit)\n"); break;
            case 62: kprint("x86-64 (64-bit)\n"); break;
            default: 
                kprint_int(e_machine); 
                kprint("\n"); 
                break;
        }
        
        uint32_t e_entry = *(uint32_t*)(elf_header + 24);
        kprint("Entry:   0x");
        kprint_hex32(e_entry);
        kprint("\n");
        
        uint16_t e_phnum = elf_header[44] | (elf_header[45] << 8);
        uint32_t e_phoff = *(uint32_t*)(elf_header + 28);
        
        kprint("PH count: ");
        kprint_int(e_phnum);
        kprint("\n");
        kprint("PH offset: 0x");
        kprint_hex32(e_phoff);
        kprint("\n\n");
        
        if(e_phnum > 0 && e_phoff < 512) {
            kprint("--- Segments ---\n");
            
            for(int ph = 0; ph < e_phnum && ph < 10; ph++) {
                uint32_t ph_addr = e_phoff + ph * 32;
                
                if(ph_addr + 32 <= 512) {
                    uint32_t p_type = *(uint32_t*)(elf_header + e_phoff + ph * 32);
                    uint32_t p_offset = *(uint32_t*)(elf_header + e_phoff + ph * 32 + 4);
                    uint32_t p_vaddr = *(uint32_t*)(elf_header + e_phoff + ph * 32 + 8);
                    uint32_t p_filesz = *(uint32_t*)(elf_header + e_phoff + ph * 32 + 16);
                    uint32_t p_memsz = *(uint32_t*)(elf_header + e_phoff + ph * 32 + 20);
                    
                    kprint("  [");
                    kprint_int(ph);
                    kprint("] ");
                    
                    if(p_type == 1) {
                        kprint_color("LOAD  ", TXT_GREEN);
                    } else if(p_type == 2) {
                        kprint_color("DYNAMIC", TXT_YELLOW);
                    } else if(p_type == 3) {
                        kprint_color("INTERP", TXT_CYAN);
                    } else if(p_type == 4) {
                        kprint_color("NOTE  ", TXT_WHITE);
                    } else {
                        kprint("OTHER ");
                        kprint_int(p_type);
                    }
                    
                    kprint(" vaddr=0x");
                    kprint_hex32(p_vaddr);
                    kprint(" filesz=");
                    kprint_int(p_filesz);
                    kprint(" memsz=");
                    kprint_int(p_memsz);
                    kprint("\n");
                }
            }
        } else if(e_phoff >= 512) {
            kprint("(Headers in sector ");
            kprint_int(205 + e_phoff / 512);
            kprint(")\n");
        }
        
        uint16_t e_shnum = elf_header[48] | (elf_header[49] << 8);
        uint32_t e_shoff = *(uint32_t*)(elf_header + 32);
        
        if(e_shnum > 0) {
            kprint("\nSections: ");
            kprint_int(e_shnum);
            kprint(" (offset 0x");
            kprint_hex32(e_shoff);
            kprint(")\n");
        }
        
    } else {
        kprint_color("ERROR: Not an ELF file\n", TXT_RED);
        kprint("First 4 bytes: ");
        kprint_hex8(magic[0]); kprint(" ");
        kprint_hex8(magic[1]); kprint(" ");
        kprint_hex8(magic[2]); kprint(" ");
        kprint_hex8(magic[3]); kprint("\n");
        
        kprint("ASCII: ");
        for(int i = 0; i < 4; i++) {
            if(magic[i] >= 32 && magic[i] <= 126) {
                char s[2] = {(char)magic[i], 0};
                kprint(s);
            } else {
                kprint(".");
            }
        }
        kprint("\n");
    }
    
    kprint_color("================================\n", TXT_CYAN);
}
else if(my_strcmp(cmd_copy, "timeon") == 0) {
    show_time_in_shell = 1;
    kprint_color("Time display enabled\n", TXT_GREEN);
}
else if(my_strcmp(cmd_copy, "cddump") == 0) {
    uint32_t lba = 16;
    if(arg[0] != '\0') {
        lba = 0;
        char* p = arg;
        while(*p >= '0' && *p <= '9') {
            lba = lba * 10 + (*p - '0');
            p++;
        }
    }
    
    kprint("[CDROM] Dumping LBA ");
    kprint_int(lba);
    kprint("...\n");
    
    uint8_t sector[2048];
    if(atapi_read_sector(lba, sector) > 0) {
        for(int i = 0; i < 64 && i < 2048; i++) {
            if(i % 16 == 0) {
                kprint("\n  ");
                kprint_hex16(i);
                kprint(": ");
            }
            kprint_hex8(sector[i]);
            kprint(" ");
        }
        kprint("\n");
        kprint("\n  ASCII: ");
        for(int i = 0; i < 64 && i < 2048; i++) {
            char c = sector[i];
            if(c >= 32 && c <= 126) {
                char s[2] = {c, 0};
                kprint(s);
            } else {
                kprint(".");
            }
        }
        kprint("\n");
    } else {
        kprint("[CDROM] Cannot read LBA ");
        kprint_int(lba);
        kprint("\n");
    }
}

else if(my_strcmp(cmd_copy, "cdmount") == 0) {
    if(atapi_mount_iso() == 0) {
        kprint_color("[OK] ISO mounted\n", TXT_GREEN);
    } else {
        kprint_color("[FAIL] Cannot mount ISO\n", TXT_RED);
    }
}

else if(my_strcmp(cmd_copy, "cdls") == 0) {
    cdrom_list_root();
}
else if(my_strncmp(cmd_copy, "cdcopy ", 7) == 0) {
    char* filename = arg;
    while(*filename == ' ') filename++;
    cdrom_copy_file(filename, "/");
}

else if(my_strncmp(cmd_copy, "cdread ", 7) == 0) {
    char* filename = arg;
    while(*filename == ' ') filename++;
    if(filename[0] == '\0') {
        kprint("Usage: cdread <filename>\n");
    } else {
        cdrom_copy_file(filename, "/");
    }
}

else if(my_strcmp(cmd_copy, "cdhex") == 0) {
    uint32_t lba = 16;
    uint32_t count = 1;
    if(arg[0] != '\0') {
        lba = 0;
        char* p = arg;
        while(*p >= '0' && *p <= '9') {
            lba = lba * 10 + (*p - '0');
            p++;
        }
        if(*p == ' ') {
            p++;
            count = 0;
            while(*p >= '0' && *p <= '9') {
                count = count * 10 + (*p - '0');
                p++;
            }
        }
    }
    
    for(uint32_t i = 0; i < count; i++) {
        uint8_t sector[2048];
        if(atapi_read_sector(lba + i, sector) > 0) {
            kprint("\n[HEX] LBA ");
            kprint_int(lba + i);
            kprint(":\n");
            for(int row = 0; row < 16; row++) {
                kprint("  ");
                for(int col = 0; col < 16; col++) {
                    kprint_hex8(sector[row * 16 + col]);
                    kprint(" ");
                }
                kprint(" |");
                for(int col = 0; col < 16; col++) {
                    char c = sector[row * 16 + col];
                    if(c >= 32 && c <= 126) {
                        char s[2] = {c, 0};
                        kprint(s);
                    } else {
                        kprint(".");
                    }
                }
                kprint("|\n");
            }
        } else {
            kprint("[HEX] Cannot read LBA ");
            kprint_int(lba + i);
            kprint("\n");
        }
    }
}

else if(my_strcmp(cmd_copy, "timeoff") == 0) {
    show_time_in_shell = 0;
    for(int i = 70; i < 79; i++) {
        kprint_at(" ", i, 0, 0x07);
    }
    kprint_color("Time display disabled\n", TXT_YELLOW);
}
else if (my_strcmp(cmd_copy, "apple") == 0) {
    apple_3d_demo();
}
else if(my_strcmp(cmd_copy, "datetime") == 0) {
    outb(0x70, 0x04); uint8_t hour = inb(0x71);
    outb(0x70, 0x02); uint8_t minute = inb(0x71);
    outb(0x70, 0x00); uint8_t second = inb(0x71);
    outb(0x70, 0x07); uint8_t day = inb(0x71);
    outb(0x70, 0x08); uint8_t month = inb(0x71);
    outb(0x70, 0x09); uint8_t year = inb(0x71);
    
    hour = ((hour >> 4) * 10) + (hour & 0x0F);
    minute = ((minute >> 4) * 10) + (minute & 0x0F);
    second = ((second >> 4) * 10) + (second & 0x0F);
    day = ((day >> 4) * 10) + (day & 0x0F);
    month = ((month >> 4) * 10) + (month & 0x0F);
    year = ((year >> 4) * 10) + (year & 0x0F);
    
    kprint("Date: ");
    if(day < 10) kprint("0");
    kprint_int(day);
    kprint(".");
    if(month < 10) kprint("0");
    kprint_int(month);
    kprint(".");
    kprint_int(2000 + year);
    kprint("  Time: ");
    if(hour < 10) kprint("0");
    kprint_int(hour);
    kprint(":");
    if(minute < 10) kprint("0");
    kprint_int(minute);
    kprint(":");
    if(second < 10) kprint("0");
    kprint_int(second);
    kprint("\n");
}
else if(my_strcmp(cmd_copy, "wnkc") == 0) {
    if(arg[0] == '\0') {
        kprint("WnkC - Wnka C Script Language\n");
        kprint("Usage:\n");
        kprint("  wnkc <code>           - execute string\n");
        kprint("  wnkc <filename>       - execute file\n");
        kprint("  wnkc vars             - show variables\n");
        kprint("\nCommands:\n");
        kprint("  print <text>          - print text\n");
        kprint("  let var = value       - set variable\n");
        kprint("  if var == value then  - conditional\n");
        kprint("  rem comment           - comment\n");
        kprint("  vars                  - show all variables\n");
        kprint("  clear                 - clear all variables\n");
        kprint("\nExamples:\n");
        kprint("  wnkc 'print Hello'\n");
        kprint("  wnkc 'let x=5' 'print $x'\n");
        kprint("  wnkc script.txt\n");
    }
    else if(my_strcmp(arg, "vars") == 0) {
        wnc_execute("vars");
    }
    else if(my_strcmp(arg, "clear") == 0) {
        wnc_execute("clear");
    }
    else {
        uint16_t dir_buf[256];
        read_sector(current_dir_sector, dir_buf);
        
        int is_file = 0;
        for(int i = 0; i < 32; i++) {
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
            if(my_strcmp(arg, name) == 0) {
                is_file = 1;
                break;
            }
        }
        
        if(is_file) {
            wnc_set_dir(current_dir_sector);
            wnc_execute_file(arg);
        } else {
            wnc_execute(arg);
        }
    }
}
else if(my_strncmp(cmd_copy, "wsm", 3) == 0) {
    kprint("\n=== WSM - Wnka Script Module ===\n\n");
    kprint("1. Create package\n");
    kprint("2. Install package\n");
    kprint("3. List package contents\n");
    kprint("4. Package info\n");
    kprint("\nSelect option (1-4): ");
    
    int choice = 0;
    while(!choice) {
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc >= 0x02 && sc <= 0x05) {
                choice = sc - 0x01;
                char num[2] = {'0' + choice, 0};
                kprint(num);
            }
            if(sc == 0x01) { kprint("\nCancelled\n"); return; }
        }
    }
    kprint("\n");
    
    switch(choice) {
        case 1: {
            kprint("\n--- CREATE PACKAGE ---\n");
            kprint("Package name: ");
            
            char pkg_name[32] = {0};
            int pos = 0;
            while(pos < 31) {
                if(inb(0x64) & 1) {
                    uint8_t sc = inb(0x60);
                    if(sc == 0x1C) break;
                    if(sc == 0x01) { kprint("\nCancelled\n"); return; }
                    if(sc == 0x0E && pos > 0) { pos--; kprint("\b \b"); }
                    else if(sc >= 0x10 && sc <= 0x19 && pos < 31) {
                        pkg_name[pos++] = "qwertyuiop"[sc - 0x10];
                        kprint_char(pkg_name[pos-1]);
                    }
                    else if(sc >= 0x1E && sc <= 0x26 && pos < 31) {
                        pkg_name[pos++] = "asdfghjkl"[sc - 0x1E];
                        kprint_char(pkg_name[pos-1]);
                    }
                    else if(sc >= 0x2C && sc <= 0x32 && pos < 31) {
                        pkg_name[pos++] = "zxcvbnm"[sc - 0x2C];
                        kprint_char(pkg_name[pos-1]);
                    }
                    else if(sc >= 0x02 && sc <= 0x0B && pos < 31) {
                        pkg_name[pos++] = "1234567890"[sc - 0x02];
                        kprint_char(pkg_name[pos-1]);
                    }
                }
            }
            pkg_name[pos] = 0;
            kprint("\n");
            
            kprint("How many scripts to add (1-10): ");
            int script_count = 0;
            while(!script_count) {
                if(inb(0x64) & 1) {
                    uint8_t sc = inb(0x60);
                    if(sc >= 0x02 && sc <= 0x0B) {
                        script_count = sc - 0x01;
                        if(script_count == 10) script_count = 0;
                        if(script_count == 0) script_count = 10;
                        kprint_int(script_count);
                    }
                    if(sc == 0x01) { kprint("\nCancelled\n"); return; }
                }
            }
            kprint("\n");
            
            char files[10][32];
            for(int f = 0; f < script_count; f++) {
                kprint("Script #");
                kprint_int(f + 1);
                kprint(" name: ");
                
                int pos2 = 0;
                while(pos2 < 31) {
                    if(inb(0x64) & 1) {
                        uint8_t sc = inb(0x60);
                        if(sc == 0x1C) break;
                        if(sc == 0x01) { kprint("\nCancelled\n"); return; }
                        if(sc == 0x0E && pos2 > 0) { pos2--; kprint("\b \b"); }
                        else if(sc >= 0x10 && sc <= 0x19 && pos2 < 31) {
                            files[f][pos2++] = "qwertyuiop"[sc - 0x10];
                            kprint_char(files[f][pos2-1]);
                        }
                        else if(sc >= 0x1E && sc <= 0x26 && pos2 < 31) {
                            files[f][pos2++] = "asdfghjkl"[sc - 0x1E];
                            kprint_char(files[f][pos2-1]);
                        }
                        else if(sc >= 0x2C && sc <= 0x32 && pos2 < 31) {
                            files[f][pos2++] = "zxcvbnm"[sc - 0x2C];
                            kprint_char(files[f][pos2-1]);
                        }
                        else if(sc >= 0x02 && sc <= 0x0B && pos2 < 31) {
                            files[f][pos2++] = "1234567890"[sc - 0x02];
                            kprint_char(files[f][pos2-1]);
                        }
                        else if(sc == 0x34 && pos2 < 31) {
                            files[f][pos2++] = '.';
                            kprint_char('.');
                        }
                    }
                }
                files[f][pos2] = 0;
                kprint("\n");
            }
            
            kprint("\nCreating package...\n");
            
            static uint8_t wsm_buf[65536];
            for(int b = 0; b < 65536; b++) wsm_buf[b] = 0;
            int wsm_pos = 0;
            
            wsm_buf[0] = 'W'; wsm_buf[1] = 'N'; wsm_buf[2] = 'S'; wsm_buf[3] = 'M';
            wsm_buf[4] = 1;
            
            for(int j = 0; j < 32 && pkg_name[j]; j++) wsm_buf[6 + j] = pkg_name[j];
            wsm_buf[103] = script_count;
            wsm_pos = 104 + script_count * 24;
            
            int success = 1;
            for(int f = 0; f < script_count && success; f++) {
                uint16_t dir_buf[256];
                read_sector(current_dir_sector, dir_buf);
                
                int found = -1;
                uint16_t fs_sec = 0;
                int fs_size = 0;
                
                for(int j = 0; j < 32; j++) {
                    char name[12] = {0};
                    for(int k = 0; k < 11; k++) name[k] = ((char*)dir_buf)[j*16 + k];
                    if(my_strcmp(files[f], name) == 0 && ((char*)dir_buf)[j*16 + 11] == 0) {
                        found = j;
                        fs_sec = dir_buf[j*8 + 6];
                        fs_size = dir_buf[j*8 + 7];
                        break;
                    }
                }
                
                if(found == -1) {
                    kprint("File not found: ");
                    kprint(files[f]);
                    kprint("\n");
                    success = 0;
                    break;
                }
                
                kprint("  + ");
                kprint(files[f]);
                kprint(" (");
                kprint_int(fs_size);
                kprint("b)\n");
                
                int to = 104 + f * 24;
                for(int j = 0; j < 16 && files[f][j]; j++) wsm_buf[to + j] = files[f][j];
                wsm_buf[to + 16] = fs_size & 0xFF;
                wsm_buf[to + 17] = (fs_size >> 8) & 0xFF;
                wsm_buf[to + 20] = wsm_pos & 0xFF;
                wsm_buf[to + 21] = (wsm_pos >> 8) & 0xFF;
                
                uint16_t fbuf[256];
                read_sector(fs_sec, fbuf);
                for(int b = 0; b < fs_size && wsm_pos + b < 65000; b++) {
                    if(b % 2 == 0) wsm_buf[wsm_pos + b] = fbuf[b/2] & 0xFF;
                    else wsm_buf[wsm_pos + b] = (fbuf[b/2] >> 8) & 0xFF;
                }
                wsm_pos += fs_size;
            }
            
            if(success) {
                uint16_t dir_buf[256];
                read_sector(current_dir_sector, dir_buf);
                
                int slot = -1;
                for(int j = 0; j < 32; j++) {
                    if(((char*)dir_buf)[j*16] == 0) { slot = j; break; }
                }
                
                if(slot == -1) {
                    kprint("Directory full!\n");
                } else {
                    char full_name[32];
                    int ni = 0;
                    for(int j = 0; j < 11 && pkg_name[j]; j++) full_name[ni++] = pkg_name[j];
                    full_name[ni++] = '.'; full_name[ni++] = 'w';
                    full_name[ni++] = 's'; full_name[ni++] = 'm'; full_name[ni] = 0;
                    
                    for(int j = 0; j < 11 && full_name[j]; j++) {
                        ((char*)dir_buf)[slot*16 + j] = full_name[j];
                    }
                    
                    uint16_t wsm_sec[256] = {0};
                    for(int j = 0; j < wsm_pos && j < 510; j++) {
                        if(j % 2 == 0) wsm_sec[j/2] = wsm_buf[j];
                        else wsm_sec[j/2] |= (wsm_buf[j] << 8);
                    }
                    
                    int fs = 500 + current_dir_sector + slot;
                    write_sector(fs, wsm_sec);
                    dir_buf[slot*8 + 6] = fs;
                    dir_buf[slot*8 + 7] = wsm_pos;
                    write_sector(current_dir_sector, dir_buf);
                    
                    kprint("\nCreated: ");
                    kprint(full_name);
                    kprint(" (");
                    kprint_int(wsm_pos);
                    kprint(" bytes)\n");
                }
            }
            break;
        }
        
        case 2: {
            kprint("\n--- INSTALL PACKAGE ---\n");
            kprint("Package filename: ");
            
            char pkg_name[32] = {0};
            int pos = 0;
            while(pos < 31) {
                if(inb(0x64) & 1) {
                    uint8_t sc = inb(0x60);
                    if(sc == 0x1C) break;
                    if(sc == 0x01) { kprint("\nCancelled\n"); return; }
                    if(sc == 0x0E && pos > 0) { pos--; kprint("\b \b"); }
                    else if(sc >= 0x10 && sc <= 0x19 && pos < 31) {
                        pkg_name[pos++] = "qwertyuiop"[sc - 0x10];
                        kprint_char(pkg_name[pos-1]);
                    }
                    else if(sc >= 0x1E && sc <= 0x26 && pos < 31) {
                        pkg_name[pos++] = "asdfghjkl"[sc - 0x1E];
                        kprint_char(pkg_name[pos-1]);
                    }
                    else if(sc >= 0x2C && sc <= 0x32 && pos < 31) {
                        pkg_name[pos++] = "zxcvbnm"[sc - 0x2C];
                        kprint_char(pkg_name[pos-1]);
                    }
                    else if(sc >= 0x02 && sc <= 0x0B && pos < 31) {
                        pkg_name[pos++] = "1234567890"[sc - 0x02];
                        kprint_char(pkg_name[pos-1]);
                    }
                    else if(sc == 0x34 && pos < 31) {
                        pkg_name[pos++] = '.';
                        kprint_char('.');
                    }
                }
            }
            pkg_name[pos] = 0;
            kprint("\nInstalling: ");
            kprint(pkg_name);
            kprint("...\n");
            
            uint16_t dir_buf[256];
            read_sector(current_dir_sector, dir_buf);
            
            int found = -1;
            uint16_t pkg_sector = 0;
            int pkg_size = 0;
            
            for(int j = 0; j < 32; j++) {
                char name[12] = {0};
                for(int k = 0; k < 11; k++) name[k] = ((char*)dir_buf)[j*16 + k];
                if(my_strcmp(pkg_name, name) == 0) {
                    found = j;
                    pkg_sector = dir_buf[j*8 + 6];
                    pkg_size = dir_buf[j*8 + 7];
                    break;
                }
            }
            
            if(found == -1) {
                kprint("Package not found!\n");
            } else {
                static uint8_t pd[65536];
                uint16_t ps[256];
                read_sector(pkg_sector, ps);
                for(int b = 0; b < pkg_size; b++) {
                    if(b % 2 == 0) pd[b] = ps[b/2] & 0xFF;
                    else pd[b] = (ps[b/2] >> 8) & 0xFF;
                }
                
                if(pd[0] != 'W' || pd[1] != 'N' || pd[2] != 'S' || pd[3] != 'M') {
                    kprint("Invalid WSM file!\n");
                } else {
                    int fc = pd[103];
                    for(int f = 0; f < fc; f++) {
                        int to = 104 + f * 24;
                        char fn[17] = {0};
                        for(int j = 0; j < 16; j++) fn[j] = pd[to + j];
                        int fsize = pd[to + 16] | (pd[to + 17] << 8);
                        int foff = pd[to + 20] | (pd[to + 21] << 8);
                        
                        uint16_t d2[256];
                        read_sector(current_dir_sector, d2);
                        int slot = -1;
                        for(int j = 0; j < 32; j++) {
                            if(((char*)d2)[j*16] == 0) { slot = j; break; }
                        }
                        
                        if(slot != -1) {
                            for(int j = 0; j < 11 && fn[j]; j++) ((char*)d2)[slot*16 + j] = fn[j];
                            uint16_t fb[256] = {0};
                            for(int b = 0; b < fsize && b < 510; b++) {
                                if(b % 2 == 0) fb[b/2] = pd[foff + b];
                                else fb[b/2] |= (pd[foff + b] << 8);
                            }
                            int fsec = 500 + current_dir_sector + slot;
                            write_sector(fsec, fb);
                            d2[slot*8 + 6] = fsec;
                            d2[slot*8 + 7] = fsize;
                            write_sector(current_dir_sector, d2);
                            kprint("  OK: ");
                            kprint(fn);
                            kprint("\n");
                        }
                    }
                    kprint("Done!\n");
                }
            }
            break;
        }
        
        case 3:
        case 4: {
            kprint("\n--- PACKAGE ");
            kprint(choice == 3 ? "LIST" : "INFO");
            kprint(" ---\n");
            kprint("Package filename: ");
            
            char pkg_name[32] = {0};
            int pos = 0;
            while(pos < 31) {
                if(inb(0x64) & 1) {
                    uint8_t sc = inb(0x60);
                    if(sc == 0x1C) break;
                    if(sc == 0x01) { kprint("\nCancelled\n"); return; }
                    if(sc == 0x0E && pos > 0) { pos--; kprint("\b \b"); }
                    else if(sc >= 0x10 && sc <= 0x19 && pos < 31) {
                        pkg_name[pos++] = "qwertyuiop"[sc - 0x10];
                        kprint_char(pkg_name[pos-1]);
                    }
                    else if(sc >= 0x1E && sc <= 0x26 && pos < 31) {
                        pkg_name[pos++] = "asdfghjkl"[sc - 0x1E];
                        kprint_char(pkg_name[pos-1]);
                    }
                    else if(sc >= 0x2C && sc <= 0x32 && pos < 31) {
                        pkg_name[pos++] = "zxcvbnm"[sc - 0x2C];
                        kprint_char(pkg_name[pos-1]);
                    }
                    else if(sc >= 0x02 && sc <= 0x0B && pos < 31) {
                        pkg_name[pos++] = "1234567890"[sc - 0x02];
                        kprint_char(pkg_name[pos-1]);
                    }
                    else if(sc == 0x34 && pos < 31) {
                        pkg_name[pos++] = '.';
                        kprint_char('.');
                    }
                }
            }
            pkg_name[pos] = 0;
            kprint("\n");
            
            uint16_t dir_buf[256];
            read_sector(current_dir_sector, dir_buf);
            
            int found = 0;
            for(int j = 0; j < 32 && !found; j++) {
                char name[12] = {0};
                for(int k = 0; k < 11; k++) name[k] = ((char*)dir_buf)[j*16 + k];
                if(my_strcmp(pkg_name, name) == 0) {
                    found = 1;
                    uint16_t ps[256];
                    read_sector(dir_buf[j*8 + 6], ps);
                    int psz = dir_buf[j*8 + 7];
                    
                    static uint8_t pd[65536];
                    for(int b = 0; b < psz; b++) {
                        if(b % 2 == 0) pd[b] = ps[b/2] & 0xFF;
                        else pd[b] = (ps[b/2] >> 8) & 0xFF;
                    }
                    
                    kprint("\n=== PACKAGE: ");
                    kprint(pkg_name);
                    kprint(" ===\n");
                    
                    char pn[33] = {0};
                    for(int k = 0; k < 32; k++) pn[k] = pd[6 + k];
                    kprint("Name: ");
                    kprint(pn);
                    kprint("\n");
                    kprint("Files: ");
                    kprint_int(pd[103]);
                    kprint("\n\n");
                    
                    for(int f = 0; f < pd[103]; f++) {
                        int to = 104 + f * 24;
                        char fn[17] = {0};
                        for(int k = 0; k < 16; k++) fn[k] = pd[to + k];
                        kprint("  ");
                        kprint(fn);
                        kprint(" (");
                        kprint_int(pd[to + 16] | (pd[to + 17] << 8));
                        kprint("b)\n");
                    }
                }
            }
            if(!found) kprint("Package not found\n");
            break;
        }
    }
}

else if(my_strcmp(input_buffer, "finit") == 0) {
    fdc_init();
    
    uint8_t test_buf[512];
    fdc_read_sector(0, 0, 1, 100, test_buf);
    
    int empty = 1;
    for(int i = 0; i < 16; i++) {
        if(test_buf[i] != 0) { empty = 0; break; }
    }
    
    if(empty) {
        kprint("[FLOPPY] Creating directory on sector 100...\n");
        for(int i = 0; i < 512; i++) test_buf[i] = 0;
        fdc_write_sector(0, 0, 1, 100, test_buf);
        kprint_color("[FLOPPY] Directory created\n", TXT_GREEN);
    }
    
    kprint_color("[FLOPPY] Ready\n", TXT_GREEN);
    kprint("Commands: fcreate, fwrite, fcat, fls, fdel, fformat\n");
}

else if(my_strcmp(input_buffer, "fformat") == 0) {
    if(fdc_is_write_protected()) {
        kprint_color("ERROR: Floppy is write protected!\n", TXT_RED);
    } else {
        kprint_color("Quick formatting floppy...\n", TXT_YELLOW);
        
        uint8_t empty[512];
        for(int i = 0; i < 512; i++) empty[i] = 0;
        fdc_write_sector(0, 0, 1, 100, empty);
        
        kprint_color("Format complete!\n", TXT_GREEN);
    }
}
else if(my_strcmp(input_buffer, "ftest") == 0) {
    uint8_t buf[512];
    kprint("Reading sector 0 from floppy...\n");
    fdc_read_sector(0, 0, 0, 1, buf);
    
    kprint("First 16 bytes: ");
    for(int i = 0; i < 16; i++) {
        kprint_hex8(buf[i]);
        kprint(" ");
    }
    kprint("\n");
    
    if(buf[510] == 0x55 && buf[511] == 0xAA) {
        kprint_color("Boot sector found (FAT12 detected)\n", TXT_GREEN);
    } else {
        kprint_color("No boot sector! Need to format floppy first.\n", TXT_RED);
        kprint("Run: fformat\n");
    }
}
else if(my_strncmp(input_buffer, "fcreate ", 8) == 0) {
    char* filename = input_buffer + 8;
    trim(filename);
    
    kprint("Creating file: ");
    kprint(filename);
    kprint("\n");
    
    uint8_t dir_buf[512];
    fdc_read_sector(0, 0, 1, 100, dir_buf);
    
    int slot = -1;
    for(int i = 0; i < 32; i++) {
        if(dir_buf[i * 16] == 0) {
            slot = i;
            break;
        }
    }
    
    if(slot == -1) {
        kprint("Directory full!\n");
    } else {
        for(int j = 0; j < 11 && filename[j]; j++) {
            dir_buf[slot * 16 + j] = filename[j];
        }
        dir_buf[slot * 16 + 12] = 200 + slot;
        dir_buf[slot * 16 + 14] = 0;
        
        fdc_write_sector(0, 0, 1, 100, dir_buf);
        kprint_color("OK\n", TXT_GREEN);
    }
}

else if(my_strncmp(input_buffer, "fwrite ", 7) == 0) {
    char* rest = input_buffer + 7;
    char filename[32] = {0};
    char* data = NULL;
    
    int i = 0;
    while(rest[i] && rest[i] != ' ' && i < 31) {
        filename[i] = rest[i];
        i++;
    }
    if(rest[i] == ' ') {
        data = rest + i + 1;
    }
    
    if(!data || data[0] == '\0') {
        kprint("Usage: fwrite <filename> <text>\n");
    } else {
        kprint("Writing to: ");
        kprint(filename);
        kprint("\n");
        
        uint8_t dir_buf[512];
        fdc_read_sector(0, 0, 1, 100, dir_buf);
        
        int slot = -1;
        for(int i = 0; i < 32; i++) {
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = dir_buf[i * 16 + j];
            if(my_strcmp(filename, name) == 0) {
                slot = i;
                break;
            }
        }
        
        if(slot == -1) {
            kprint("File not found!\n");
        } else {
            uint8_t data_buf[512] = {0};
            int len = 0;
            while(data[len] && len < 511) {
                data_buf[len] = data[len];
                len++;
            }
            
            int sector = 200 + slot;
            fdc_write_sector(0, 0, 1, sector, data_buf);
            
            dir_buf[slot * 16 + 14] = len & 0xFF;
            dir_buf[slot * 16 + 15] = (len >> 8) & 0xFF;
            fdc_write_sector(0, 0, 1, 100, dir_buf);
            
            kprint_color("OK\n", TXT_GREEN);
        }
    }
}

else if(my_strncmp(input_buffer, "fcat ", 5) == 0) {
    char* filename = input_buffer + 5;
    trim(filename);
    
    uint8_t dir_buf[512];
    fdc_read_sector(0, 0, 1, 100, dir_buf);
    
    int slot = -1;
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = dir_buf[i * 16 + j];
        if(my_strcmp(filename, name) == 0) {
            slot = i;
            break;
        }
    }
    
    if(slot == -1) {
        kprint("File not found: ");
        kprint(filename);
        kprint("\n");
    } else {
        int size = dir_buf[slot * 16 + 14] | (dir_buf[slot * 16 + 15] << 8);
        int sector = 200 + slot;
        
        uint8_t data_buf[512];
        fdc_read_sector(0, 0, 1, sector, data_buf);
        
        kprint("\n=== ");
        kprint(filename);
        kprint(" ===\n\n");
        
        for(int i = 0; i < size; i++) {
            char c = (char)data_buf[i];
            if(c >= 32 && c <= 126) {
                char s[2] = {c, 0};
                kprint(s);
            }
        }
        kprint("\n\n");
    }
}

else if(my_strcmp(input_buffer, "fls") == 0) {
    uint8_t dir_buf[512];
    fdc_read_sector(0, 0, 1, 100, dir_buf);
    
    kprint("\n=== FLOPPY FILES ===\n");
    int count = 0;
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = dir_buf[i * 16 + j];
        if(name[0] != 0) {
            int size = dir_buf[i * 16 + 14] | (dir_buf[i * 16 + 15] << 8);
            kprint("  ");
            kprint(name);
            kprint(" (");
            kprint_int(size);
            kprint(" bytes)\n");
            count++;
        }
    }
    if(count == 0) {
        kprint("  (empty)\n");
    }
    kprint("===================\n");
}
else if(my_strcmp(input_buffer, "initfs") == 0) {
    format_disk();
    test_disk();
    init_disk_system();
    fdc_init();
}
else if(my_strcmp(input_buffer, "initds") == 0) {
    init_disk_system();
    fdc_init();
}
else if(my_strcmp(cmd_copy, "format") == 0) {
    if(!check_user_password()) {
        kprint_color("Access denied! Wrong password.\n", TXT_RED);
    } else {
        kprint_color("\nFormatting disk...\n", TXT_YELLOW);
        format_disk();
    }
}
else if(my_strcmp(input_buffer, "testds") == 0) {
    test_disk();
}

else if(my_strcmp(input_buffer, "diskbench") == 0) {
    kprint("\n=== DISK BENCHMARK ===\n");
    
    uint16_t buf[256];
    for(int i = 0; i < 256; i++) buf[i] = i;
    
    uint32_t start, end;
    
    kprint("Writing 100 sectors... ");
    start = seconds * 1000;
    for(int i = 0; i < 100; i++) {
        write_sector(1000 + i, buf);
    }
    end = seconds * 1000;
    kprint_int(end - start);
    kprint(" ms\n");
    
    kprint("Reading 100 sectors...  ");
    start = seconds * 1000;
    for(int i = 0; i < 100; i++) {
        read_sector(1000 + i, buf);
    }
    end = seconds * 1000;
    kprint_int(end - start);
    kprint(" ms\n");
    
    int speed = (100 * 512 * 1000) / ((end - start) + 1);
    kprint("Speed: ~");
    kprint_int(speed);
    kprint(" KB/s\n");
}

else if(my_strcmp(input_buffer, "lowformat") == 0) {

    if(!check_user_password()) {
        kprint_color("Access denied! Wrong password.\n", TXT_RED);
        return;
    }
    
    kprint_color("\n[WARNING] Low-level format will ERASE ALL DATA!\n", TXT_RED);
    kprint_color("[WARNING] All sectors will be overwritten with ZEROES!\n", TXT_RED);
    
    kprint_color("\nSelect option:\n", TXT_YELLOW);
    kprint_color("  1. Format entire disk (max)\n", TXT_CYAN);
    kprint_color("  2. Enter custom size (1-12 GB)\n", TXT_CYAN);
    kprint("Choice (1-2): ");
    
    int choice = 0;
    while(choice == 0) {
        if(inb(0x64) & 1) {
            uint8_t key = inb(0x60);
            if(key == 0x02) choice = 1;
            if(key == 0x03) choice = 2;
        }
    }
    
    int gb = 0;
    
    if(choice == 1) {
        uint16_t identify_buf[256];
        outb(ata_base_port + 6, 0xA0);
        outb(ata_base_port + 7, 0xEC);
        for(volatile int i = 0; i < 100000; i++);
        uint8_t status = inb(ata_base_port + 7);
        if(status != 0 && status != 0xFF) {
            for(int i = 0; i < 256; i++) identify_buf[i] = inw(ata_base_port);
            uint32_t sectors = identify_buf[60] | (identify_buf[61] << 16);
            gb = (sectors * 512) / (1024 * 1024 * 1024);
        }
        if(gb < 1) gb = 2;
        if(gb > 12) gb = 12;
        kprint("\nFormatting entire disk: ");
        kprint_int(gb);
        kprint(" GB\n");
    } else {
        kprint_color("\nEnter number of GB to format (1-12): ", TXT_YELLOW);
        int got = 0;
        while(!got) {
            if(inb(0x64) & 1) {
                uint8_t key = inb(0x60);
                if(key >= 0x02 && key <= 0x0B) {
                    int digit = key - 0x02;
                    if(digit == 10) digit = 0;
                    gb = gb * 10 + digit;
                    char s[2] = {'0' + digit, 0};
                    kprint(s);
                }
                else if(key == 0x0E) {
                    gb = 0;
                    kprint("\b \b");
                }
                else if(key == 0x1C) {
                    got = 1;
                }
            }
        }
        if(gb < 1) gb = 1;
        if(gb > 12) gb = 12;
    }
    
    kprint("\n");
    lowformat_disk(gb);
}
else if(my_strcmp(input_buffer, "speed") == 0) {
    kprint("Testing disk speed (reading 5 MB)...\n");
    kprint("Press ESC to cancel\n\n");
    
    uint32_t test_sectors = 10000;
    uint32_t total_bytes = test_sectors * 512;
    
    kprint("Sectors: ");
    kprint_int(test_sectors);
    kprint(" (");
    kprint_int(total_bytes / 1024);
    kprint(" KB)\n");
    
    uint16_t buf[256];
    
    uint32_t start_ticks = seconds * 1000;
    
    for(uint32_t i = 0; i < test_sectors; i++) {
        read_sector(10000 + i, buf);
        
        if(i % 1000 == 0 && i > 0) {
            int percent = (i * 100) / test_sectors;
            kprint("\rProgress: ");
            kprint_int(percent);
            kprint("%   ");
        }

        if(i % 1000 == 0) {
            if(inb(0x64) & 1) {
                uint8_t sc = inb(0x60);
                if(sc == 0x01) {
                    kprint("\n\nTest cancelled\n");
                    return;
                }
            }
        }
    }
    
    uint32_t end_ticks = seconds * 1000;
    uint32_t ms = end_ticks - start_ticks;
    if(ms < 1) ms = 1;
    
    kprint("\rProgress: 100%   \n\n");
    
    uint32_t speed_kb_sec = (total_bytes / 1024) * 1000 / ms;
    
    kprint("=== RESULTS ===\n");
    kprint("Data: ");
    kprint_int(total_bytes / 1024);
    kprint(" KB\n");
    
    kprint("Time: ");
    kprint_int(ms);
    kprint(" ms (");
    kprint_int(ms / 1000);
    kprint(".");
    kprint_int((ms % 1000) / 100);
    kprint(" sec)\n");
    
    kprint("Speed: ");
    kprint_int(speed_kb_sec);
    kprint(" KB/s (");
    kprint_int(speed_kb_sec / 1024);
    kprint(" MB/s)\n");
    
    if(speed_kb_sec < 100) {
        kprint("Rating: EXTREMELY SLOW\n");
    } else if(speed_kb_sec < 500) {
        kprint("Rating: VERY SLOW (3600 RPM)\n");
    } else if(speed_kb_sec < 2000) {
        kprint("Rating: SLOW (3600 RPM typical)\n");
    } else if(speed_kb_sec < 5000) {
        kprint("Rating: NORMAL (5400 RPM)\n");
    } else if(speed_kb_sec < 15000) {
        kprint("Rating: FAST (7200 RPM)\n");
    } else {
        kprint("Rating: VERY FAST (cached or SSD)\n");
    }
}
else if(my_strncmp(input_buffer, "filltest ", 8) == 0) {
    char* sectors_str = input_buffer + 9;
    int sectors = atoi(sectors_str);
    if(sectors < 1) sectors = 100;
    if(sectors > 10000) sectors = 10000;
    
    kprint("Filling ");
    kprint_int(sectors);
    kprint(" sectors with test data...\n");
    
    uint16_t buf[256];
    for(int i = 0; i < 256; i++) buf[i] = i & 0xFF;
    
    for(int i = 0; i < sectors; i++) {
        write_sector(2000 + i, buf);
        if(i % 100 == 0) {
            kprint("#");
        }
    }
    kprint("\nDone!\n");
}

else if(my_strcmp(input_buffer, "verify") == 0) {
    kprint("Verifying disk integrity...\n");
    
    uint16_t buf1[256], buf2[256];
    int errors = 0;
    
    for(int i = 0; i < 100; i++) {
        read_sector(2000 + i, buf1);
        read_sector(2000 + i, buf2);
        
        for(int j = 0; j < 256; j++) {
            if(buf1[j] != buf2[j]) errors++;
        }
    }
    
    if(errors == 0) {
        kprint_color(" No errors found\n", TXT_GREEN);
    } else {
        kprint_color(" Found ", TXT_RED);
        kprint_int(errors);
        kprint_color(" errors\n", TXT_RED);
    }
}

else if(my_strcmp(input_buffer, "diskinfo") == 0) {
    kprint("\n=== DISK INFO ===\n");
    kprint("Port: 0x");
    kprint_hex16(ata_base_port);
    kprint("\n");
    
    uint16_t boot_buf[256];
    read_sector(0, boot_buf);
    
    kprint("Sector 0 signature: 0x");
    kprint_hex16(boot_buf[255]);
    kprint("\n");
    
    if(boot_buf[255] == 0xAA55) {
        kprint_color("Boot sector: VALID\n", TXT_GREEN);
    } else {
        kprint_color("Boot sector: INVALID\n", TXT_RED);
    }
}

else if(my_strcmp(input_buffer, "stresstest") == 0) {
    kprint("=== DISK STRESS TEST ===\n");
    kprint("Writing/Reading 1000 sectors...\n");
    
    uint16_t write_buf[256], read_buf[256];
    for(int i = 0; i < 256; i++) write_buf[i] = i;
    
    uint32_t errors = 0;
    
    for(int sector = 3000; sector < 4000; sector++) {
        write_sector(sector, write_buf);
        read_sector(sector, read_buf);
        
        for(int i = 0; i < 256; i++) {
            if(write_buf[i] != read_buf[i]) errors++;
        }
        
        if((sector - 3000) % 100 == 0) {
            kprint(".");
        }
    }
    
    kprint("\nErrors: ");
    kprint_int(errors);
    if(errors == 0) {
        kprint_color(" - PASSED\n", TXT_GREEN);
    } else {
        kprint_color(" - FAILED\n", TXT_RED);
    }
}

else if(my_strncmp(input_buffer, "zerofill ", 9) == 0) {
    char* start_str = input_buffer + 9;
    int start = atoi(start_str);
    
    char* end_str = start_str;
    while(*end_str >= '0' && *end_str <= '9') end_str++;
    while(*end_str == ' ') end_str++;
    int end = atoi(end_str);
    
    if(start < 0 || end < start || end > 10000) {
        kprint("Usage: zerofill <start_sector> <end_sector>\n");
    } else {
        kprint("Zero-filling sectors ");
        kprint_int(start); kprint(" - "); kprint_int(end); kprint("\n");
        
        uint16_t zero_buf[256];
        for(int i = 0; i < 256; i++) zero_buf[i] = 0;
        
        for(int i = start; i <= end; i++) {
            write_sector(i, zero_buf);
            if((i - start) % 100 == 0) kprint(".");
        }
        kprint("\nDone!\n");
    }
}
else if(my_strncmp(input_buffer, "hexdump ", 7) == 0) {
    char* filename = input_buffer + 8;
    trim(filename);
    
    uint16_t dir_buf[256];
    read_sector(100, dir_buf);
    
    int slot = -1;
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
        if(my_strcmp(filename, name) == 0) {
            slot = i;
            break;
        }
    }
    
    if(slot == -1) {
        kprint("File not found: ");
        kprint(filename);
        kprint("\n");
    } else {
        uint16_t data_buf[256];
        read_sector(200 + slot, data_buf);
        int size = dir_buf[slot*8 + 7];
        
        kprint("\n=== HEXDUMP: ");
        kprint(filename);
        kprint(" (");
        kprint_int(size);
        kprint(" bytes) ===\n\n");
        
        for(int i = 0; i < size; i += 16) {
            kprint_hex16(i);
            kprint(": ");
            
            for(int j = 0; j < 16 && i + j < size; j++) {
                uint8_t byte;
                if((i + j) % 2 == 0) {
                    byte = data_buf[(i + j)/2] & 0xFF;
                } else {
                    byte = (data_buf[(i + j)/2] >> 8) & 0xFF;
                }
                kprint_hex8(byte);
                kprint(" ");
                if(j == 7) kprint(" ");
            }
            
            for(int j = size - i; j < 16; j++) {
                kprint("   ");
                if(j == 7) kprint(" ");
            }
            kprint(" |");
            
            for(int j = 0; j < 16 && i + j < size; j++) {
                uint8_t byte;
                if((i + j) % 2 == 0) {
                    byte = data_buf[(i + j)/2] & 0xFF;
                } else {
                    byte = (data_buf[(i + j)/2] >> 8) & 0xFF;
                }
                if(byte >= 32 && byte <= 126) {
                    char s[2] = {byte, 0};
                    kprint(s);
                } else {
                    kprint(".");
                }
            }
            kprint("|\n");
        }
        kprint("\n");
    }
}

else if(my_strncmp(input_buffer, "hexedit", 7) == 0) {
    char* filename = input_buffer + 7;
    while(*filename == ' ') filename++;
    trim(filename);
    
    if(filename[0] == '\0') {
        kprint("Usage: hexedit <filename>\n");
    } else {
        uint16_t dir_buf[256];
        read_sector(100, dir_buf);
        
        int slot = -1;
        for(int i = 0; i < 32; i++) {
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
            if(my_strcmp(filename, name) == 0) {
                slot = i;
                break;
            }
        }
        
        if(slot == -1) {
            kprint("File not found. Use 'create' first.\n");
        } else {
            uint16_t data_buf[256];
            read_sector(200 + slot, data_buf);
            int size = dir_buf[slot*8 + 7];
            
            int editing = 1;
            int modified = 0;
            int offset = 0;
            int scroll = 0;
            
            while(editing) {
                clear_screen_bg(BLACK);
                
                draw_shadow_window(1, 0, 78, 2, BLUE, TXT_WHITE, "HEX EDITOR");
                kprint_at(filename, 40 - my_strlen(filename)/2, 1, (BLUE << 4) | TXT_YELLOW);
                
                char info[64];
                draw_frame(1, 3, 78, 2, GRAY, TXT_WHITE);
                kprint_at("File: ", 3, 4, (GRAY << 4) | TXT_CYAN);
                kprint_at(filename, 9, 4, (GRAY << 4) | TXT_WHITE);
                kprint_at("Size: ", 30, 4, (GRAY << 4) | TXT_CYAN);
                kprint_int_at(size, 36, 4, (GRAY << 4) | TXT_GREEN);
                kprint_at("Offset: ", 50, 4, (GRAY << 4) | TXT_CYAN);
                kprint_int_at(offset, 58, 4, (GRAY << 4) | TXT_YELLOW);
                if(modified) {
                    kprint_at("[MODIFIED]", 68, 4, (GRAY << 4) | TXT_RED);
                }
                
                draw_frame(1, 6, 78, 15, GRAY, TXT_WHITE);
                
                int start_line = scroll;
                int lines = 13;
                
                for(int line = 0; line < lines && start_line + line < (size + 15) / 16; line++) {
                    int line_offset = (start_line + line) * 16;
                    int y = 7 + line;
                    
                    kprint_hex16_at(line_offset, 3, y, (GRAY << 4) | TXT_CYAN);
                    kprint_at(":", 7, y, (GRAY << 4) | TXT_WHITE);
                    
                    for(int j = 0; j < 16; j++) {
                        int pos = line_offset + j;
                        int x = 10 + j * 3;
                        if(pos < size) {
                            uint8_t byte;
                            if(pos % 2 == 0) {
                                byte = data_buf[pos/2] & 0xFF;
                            } else {
                                byte = (data_buf[pos/2] >> 8) & 0xFF;
                            }
                            if(pos == offset) {
                                draw_frame(x-1, y-1, 3, 2, GREEN, TXT_BLACK);
                                kprint_hex8_at(byte, x, y, (GREEN << 4) | TXT_BLACK);
                            } else {
                                kprint_hex8_at(byte, x, y, (GRAY << 4) | TXT_WHITE);
                            }
                        } else {
                            kprint_at("  ", x, y, (GRAY << 4) | TXT_DGRAY);
                        }
                        if(j == 7) kprint_at(" ", x+2, y, (GRAY << 4) | TXT_BLACK);
                    }
                    
                    for(int j = 0; j < 16; j++) {
                        int pos = line_offset + j;
                        int x = 60 + j;
                        if(pos < size) {
                            uint8_t byte;
                            if(pos % 2 == 0) {
                                byte = data_buf[pos/2] & 0xFF;
                            } else {
                                byte = (data_buf[pos/2] >> 8) & 0xFF;
                            }
                            char s[2] = {(byte >= 32 && byte <= 126) ? (char)byte : '.', 0};
                            if(pos == offset) {
                                draw_frame(x-1, y-1, 2, 2, GREEN, TXT_BLACK);
                                kprint_at(s, x, y, (GREEN << 4) | TXT_BLACK);
                            } else {
                                kprint_at(s, x, y, (GRAY << 4) | TXT_WHITE);
                            }
                        } else {
                            kprint_at(" ", x, y, (GRAY << 4) | TXT_BLACK);
                        }
                    }
                }
                
                draw_frame(1, 22, 78, 3, BLUE, TXT_WHITE);
                kprint_at("[A] Add   [D] Delete   [C] Change   [S] Save   [ESC] Exit", 10, 23, (BLUE << 4) | TXT_YELLOW);
                kprint_at("[< >] Move   [↑ ↓] Line   [PgUp/PgDn] Page", 20, 24, (BLUE << 4) | TXT_CYAN);
                
                move_cursor(79, 24);
                
                uint8_t key = 0;
                while(!key) {
                    if(inb(0x64) & 1) {
                        key = inb(0x60);
                    }
                }
                
                if(key == 0x4B && offset > 0) {     
                    offset--;
                    if(offset / 16 < scroll) scroll = offset / 16;
                }
                else if(key == 0x4D && offset < size - 1) { 
                    offset++;
                    if(offset / 16 >= scroll + 13) scroll = offset / 16 - 12;
                }
                else if(key == 0x48 && offset >= 16) {    
                    offset -= 16;
                    if(offset / 16 < scroll) scroll = offset / 16;
                }
                else if(key == 0x50 && offset + 16 < size) { 
                    offset += 16;
                    if(offset / 16 >= scroll + 13) scroll = offset / 16 - 12;
                }
                else if(key == 0x49 && offset >= 16*13) { 
                    offset -= 16*13;
                    if(offset < 0) offset = 0;
                    scroll = offset / 16;
                }
                else if(key == 0x51 && offset + 16*13 < size) { 
                    offset += 16*13;
                    if(offset >= size) offset = size - 1;
                    scroll = offset / 16;
                    if(scroll + 13 > (size + 15) / 16) scroll = (size + 15) / 16 - 13;
                }
                else if(key == 0x1E && size < 510) {
                    kprint_color("\nEnter byte (00-FF): ", TXT_GREEN);
                    char hex[3] = {0};
                    int pos = 0;
                    while(pos < 2) {
                        if(inb(0x64) & 1) {
                            uint8_t sc = inb(0x60);
                            if(sc >= 0x02 && sc <= 0x0B) {
                                hex[pos++] = "1234567890"[sc - 0x02];
                                kprint_char(hex[pos-1]);
                            }
                            else if(sc >= 0x10 && sc <= 0x19) {
                                hex[pos++] = "qwertyuiop"[sc - 0x10];
                                kprint_char(hex[pos-1]);
                            }
                            else if(sc >= 0x1E && sc <= 0x26) {
                                hex[pos++] = "asdfghjkl"[sc - 0x1E];
                                kprint_char(hex[pos-1]);
                            }
                            else if(sc == 0x0E && pos > 0) {
                                pos--;
                                kprint("\b \b");
                            }
                        }
                    }
                    int val = (hex_char_to_int(hex[0]) << 4) | hex_char_to_int(hex[1]);
                    if(val >= 0 && val <= 255) {
                        int byte_pos = size;
                        if(byte_pos % 2 == 0) {
                            data_buf[byte_pos/2] = val;
                        } else {
                            data_buf[byte_pos/2] |= (val << 8);
                        }
                        size++;
                        modified = 1;
                        kprint_color("\n✓ Byte added\n", TXT_GREEN);
                        for(volatile int d = 0; d < 2000000; d++);
                    }
                }
                else if(key == 0x20 && size > 0) {
                    for(int i = offset; i < size - 1; i++) {
                        uint8_t next_byte;
                        if((i+1) % 2 == 0) {
                            next_byte = data_buf[(i+1)/2] & 0xFF;
                        } else {
                            next_byte = (data_buf[(i+1)/2] >> 8) & 0xFF;
                        }
                        if(i % 2 == 0) {
                            data_buf[i/2] = (data_buf[i/2] & 0xFF00) | next_byte;
                        } else {
                            data_buf[i/2] = (data_buf[i/2] & 0x00FF) | (next_byte << 8);
                        }
                    }
                    size--;
                    modified = 1;
                    if(offset >= size) offset = size - 1;
                    if(offset < 0) offset = 0;
                    kprint_color("\n✓ Byte deleted\n", TXT_YELLOW);
                    for(volatile int d = 0; d < 2000000; d++);
                }
                else if(key == 0x2E) {
                    kprint_color("\nEnter new byte (00-FF): ", TXT_GREEN);
                    char hex[3] = {0};
                    int pos = 0;
                    while(pos < 2) {
                        if(inb(0x64) & 1) {
                            uint8_t sc = inb(0x60);
                            if(sc >= 0x02 && sc <= 0x0B) {
                                hex[pos++] = "1234567890"[sc - 0x02];
                                kprint_char(hex[pos-1]);
                            }
                            else if(sc >= 0x10 && sc <= 0x19) {
                                hex[pos++] = "qwertyuiop"[sc - 0x10];
                                kprint_char(hex[pos-1]);
                            }
                            else if(sc >= 0x1E && sc <= 0x26) {
                                hex[pos++] = "asdfghjkl"[sc - 0x1E];
                                kprint_char(hex[pos-1]);
                            }
                            else if(sc == 0x0E && pos > 0) {
                                pos--;
                                kprint("\b \b");
                            }
                        }
                    }
                    int val = (hex_char_to_int(hex[0]) << 4) | hex_char_to_int(hex[1]);
                    if(val >= 0 && val <= 255) {
                        if(offset % 2 == 0) {
                            data_buf[offset/2] = (data_buf[offset/2] & 0xFF00) | val;
                        } else {
                            data_buf[offset/2] = (data_buf[offset/2] & 0x00FF) | (val << 8);
                        }
                        modified = 1;
                        kprint_color("\n✓ Byte changed\n", TXT_GREEN);
                        for(volatile int d = 0; d < 2000000; d++);
                    }
                }
                else if(key == 0x1F) {
                    write_sector(200 + slot, data_buf);
                    dir_buf[slot*8 + 7] = size;
                    write_sector(100, dir_buf);
                    modified = 0;
                    kprint_color("\n✓ SAVED!\n", TXT_GREEN);
                    for(volatile int d = 0; d < 2000000; d++);
                }
                else if(key == 0x01) {
                    if(modified) {
                        draw_shadow_window(25, 10, 30, 5, BLACK, TXT_WHITE, "Save?");
                        kprint_at("Save changes?", 32, 12, (BLACK << 4) | TXT_YELLOW);
                        kprint_at("[Y] Yes  [N] No", 33, 14, (BLACK << 4) | TXT_CYAN);
                        char save = 0;
                        while(save != 'y' && save != 'Y' && save != 'n' && save != 'N') {
                            if(inb(0x64) & 1) {
                                uint8_t sc = inb(0x60);
                                if(sc == 0x15 || sc == 0x2C) save = 'y';
                                if(sc == 0x31 || sc == 0x35) save = 'n';
                            }
                        }
                        if(save == 'y' || save == 'Y') {
                            write_sector(200 + slot, data_buf);
                            dir_buf[slot*8 + 7] = size;
                            write_sector(100, dir_buf);
                        }
                    }
                    editing = 0;
                }
            }
            clear_screen();
            kprint_color("Hexedit exited\n", TXT_GREEN);
        }
    }
}
else if(my_strncmp(input_buffer, "mkdir ", 6) == 0) {
    char* dirname = input_buffer + 6;
    trim(dirname);
    
    uint16_t dir_buf[256];
    read_sector(current_dir_sector, dir_buf);
    int slot = -1;
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
        if(name[0] == 0) {
            slot = i;
            break;
        }
    }
    
    if(slot == -1) {
        kprint("No free slots\n");
    } else {
        for(int j = 0; j < 11 && dirname[j]; j++) {
            ((char*)dir_buf)[slot*16 + j] = dirname[j];
        }
        ((char*)dir_buf)[slot*16 + 11] = 1;
        dir_buf[slot*8 + 6] = 300 + slot;
        dir_buf[slot*8 + 7] = 0;
        
        write_sector(current_dir_sector, dir_buf);

        uint16_t folder_buf[256];
        for(int i = 0; i < 256; i++) folder_buf[i] = 0;
        write_sector(300 + slot, folder_buf);
        
        kprint("Created directory: ");
        kprint(dirname);
        kprint("\n");
    }
}

else if(my_strncmp(input_buffer, "cd ", 3) == 0) {
    char* dirname = input_buffer + 3;
    trim(dirname);
    
    if(my_strcmp(dirname, "/") == 0 || my_strcmp(dirname, "root") == 0) {
        current_dir_sector = 100;
        kprint("Changed to root\n");
    } else {
        uint16_t dir_buf[256];
        read_sector(current_dir_sector, dir_buf);
        
        int found = -1;
        for(int i = 0; i < 32; i++) {
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
            if(my_strcmp(dirname, name) == 0) {
                found = i;
                break;
            }
        }
        
        if(found != -1 && ((char*)dir_buf)[found*16 + 11] == 1) {
            current_dir_sector = dir_buf[found*8 + 6];
            kprint("Changed to: ");
            kprint(dirname);
            kprint("\n");
        } else {
            kprint("Directory not found: ");
            kprint(dirname);
            kprint("\n");
        }
    }
}

else if(my_strcmp(input_buffer, "pwd") == 0) {
    if(current_dir_sector == 100) {
        kprint("/\n");
    } else {
        kprint("/dir_");
        kprint_int(current_dir_sector - 300);
        kprint("\n");
    }
}

else if(my_strcmp(cmd_copy, "ide") == 0) {
    char filename[256] = {0};
    
    if(arg[0] != '\0') {
        my_strcpy(filename, arg);
    } else {
        my_strcpy(filename, "untitled.wnc");
    }
    
    char lines[500][200];
    int line_count = 0;
    int cursor_line = 0;
    int cursor_col = 0;
    int scroll = 0;
    int modified = 0;
    int show_suggestions = 1;
    int suggest_scroll = 0;
    int current_suggestion = 0;
    int error_line = -1;
    int show_help_window = 0;
    char error_msg[256] = {0};
    
    const char* all_commands[] = {
        "print", "input", "let", "if", "else", "while", "for", "break", "continue",
        "return", "func", "void", "static", "import", "array", "struct", "run",
        "time", "sleep", "rand", "clear", "log", "runscript", "getkey", "graph",
        "key", "mkdir", "cd", "pwd", "ls", "create", "write", "read", "delete",
        "copy", "move", "call", "vars", "exit"
    };
    int cmd_count = sizeof(all_commands) / sizeof(all_commands[0]);
    
    uint16_t dir_buf[256];
    read_sector(current_dir_sector, dir_buf);
    
    int slot = -1;
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
        if(my_strcmp(filename, name) == 0) {
            slot = i;
            break;
        }
    }
    
    if(slot != -1) {
        int sector = dir_buf[slot*8 + 6];
        int size = dir_buf[slot*8 + 7];
        uint16_t data_buf[256];
        read_sector(sector, data_buf);
        
        char text[4096] = {0};
        for(int i = 0; i < size && i < 4095; i++) {
            if(i % 2 == 0) text[i] = data_buf[i/2] & 0xFF;
            else text[i] = (data_buf[i/2] >> 8) & 0xFF;
        }
        
        int line = 0, col = 0;
        for(int i = 0; text[i] && line < 500; i++) {
            if(text[i] == '\n') {
                lines[line][col] = '\0';
                line++;
                col = 0;
            } else if(col < 199) {
                lines[line][col++] = text[i];
            }
        }
        if(col > 0 || line == 0) {
            lines[line][col] = '\0';
            line_count = line + 1;
        } else {
            line_count = line;
        }
    } else {
        my_strcpy(lines[0], "");
        line_count = 1;
    }
    
    auto is_keyword = [&](const char* word) -> int {
        const char* kw[] = {"print","input","let","if","else","while","for","break","continue",
                            "return","func","void","static","import","array","struct","run",
                            "time","sleep","rand","clear","log","runscript","getkey","graph","key",NULL};
        for(int i = 0; kw[i]; i++) if(my_strcmp(kw[i], word) == 0) return 1;
        return 0;
    };
    
    auto is_function = [&](const char* word) -> int {
        const char* fn[] = {"print","input","getkey","sleep","rand","time","call",NULL};
        for(int i = 0; fn[i]; i++) if(my_strcmp(fn[i], word) == 0) return 1;
        return 0;
    };
    
    auto check_syntax = [&]() -> int {
        error_line = -1;
        error_msg[0] = '\0';
        
        for(int l = 0; l < line_count; l++) {
            int in_string = 0;
            for(int i = 0; lines[l][i]; i++) {
                if(lines[l][i] == '"' && (i == 0 || lines[l][i-1] != '\\')) {
                    in_string = !in_string;
                }
            }
            if(in_string) {
                error_line = l;
                my_strcpy(error_msg, "Unclosed string literal");
                return 0;
            }
            
            int brace_count = 0, paren_count = 0;
            for(int i = 0; lines[l][i]; i++) {
                if(lines[l][i] == '{') brace_count++;
                if(lines[l][i] == '}') brace_count--;
                if(lines[l][i] == '(') paren_count++;
                if(lines[l][i] == ')') paren_count--;
            }
            if(brace_count != 0) {
                error_line = l;
                my_strcpy(error_msg, "Unmatched braces {}");
                return 0;
            }
            if(paren_count != 0) {
                error_line = l;
                my_strcpy(error_msg, "Unmatched parentheses ()");
                return 0;
            }
        }
        return 1;
    };
    
    clear_screen();
    
    int running = 1;
    int redraw = 1;
    
    while(running) {
        if(redraw) {
            clear_screen_bg(COLOR_BLACK);
            
            for(int i = 0; i < 80; i++) put_pixel(i, 0, COLOR_BLUE, TXT_WHITE, S_HLINE);
            kprint_at("WNKC IDE v1.0", 2, 0, (COLOR_BLUE << 4) | TXT_YELLOW);
            kprint_at("File: ", 20, 0, (COLOR_BLUE << 4) | TXT_CYAN);
            kprint_at(filename, 26, 0, (COLOR_BLUE << 4) | TXT_WHITE);
            if(modified) kprint_at(" [MODIFIED]", 40, 0, (COLOR_BLUE << 4) | TXT_RED);
            kprint_at("Ln: ", 65, 0, (COLOR_BLUE << 4) | TXT_CYAN);
            kprint_int_at(cursor_line + 1, 69, 0, (COLOR_BLUE << 4) | TXT_GREEN);
            kprint_at(" Col: ", 73, 0, (COLOR_BLUE << 4) | TXT_CYAN);
            kprint_int_at(cursor_col + 1, 78, 0, (COLOR_BLUE << 4) | TXT_GREEN);
            
            for(int i = 0; i < 80; i++) put_pixel(i, 23, COLOR_BLUE, TXT_WHITE, S_HLINE);
            kprint_at("[F1]Menu  [F2]Run  [F3]Check  [F4]Save  [F5]New  [F6]Help  [ESC]Exit", 2, 24, (COLOR_BLUE << 4) | TXT_YELLOW);
            
            if(show_suggestions) {
                draw_frame(55, 10, 24, 12, COLOR_GRAY, TXT_WHITE);
                kprint_at("COMMANDS", 60, 11, (COLOR_GRAY << 4) | TXT_GREEN);
                int visible = 10;
                for(int i = suggest_scroll; i < cmd_count && i < suggest_scroll + visible; i++) {
                    if(i == current_suggestion) {
                        for(int x = 56; x < 78; x++) put_pixel(x, 13 + (i - suggest_scroll), COLOR_BLUE, TXT_WHITE, ' ');
                        kprint_at(all_commands[i], 57, 13 + (i - suggest_scroll), (COLOR_BLUE << 4) | TXT_YELLOW);
                    } else {
                        kprint_at(all_commands[i], 57, 13 + (i - suggest_scroll), (COLOR_GRAY << 4) | TXT_WHITE);
                    }
                }
            }
            
            if(show_help_window) {
                draw_frame(10, 5, 60, 18, COLOR_BLUE, TXT_WHITE);
                kprint_at("=== WNKC IDE HELP ===", 25, 7, (COLOR_BLUE << 4) | TXT_YELLOW);
                kprint_at("F1 - File menu", 15, 9, (COLOR_BLUE << 4) | TXT_WHITE);
                kprint_at("F2 - Run", 15, 10, (COLOR_BLUE << 4) | TXT_WHITE);
                kprint_at("F3 - Check syntax", 15, 11, (COLOR_BLUE << 4) | TXT_WHITE);
                kprint_at("F4 - Save", 15, 12, (COLOR_BLUE << 4) | TXT_WHITE);
                kprint_at("F5 - New file", 15, 13, (COLOR_BLUE << 4) | TXT_WHITE);
                kprint_at("F6 - This help", 15, 14, (COLOR_BLUE << 4) | TXT_WHITE);
                kprint_at("F7 - Toggle suggestions", 15, 15, (COLOR_BLUE << 4) | TXT_WHITE);
                kprint_at("ESC - Exit", 15, 16, (COLOR_BLUE << 4) | TXT_WHITE);
                kprint_at("Arrows - Move", 15, 17, (COLOR_BLUE << 4) | TXT_WHITE);
                kprint_at("Tab - 4 spaces", 15, 18, (COLOR_BLUE << 4) | TXT_WHITE);
                kprint_at("Enter - New line", 15, 19, (COLOR_BLUE << 4) | TXT_WHITE);
                kprint_at("Press any key to close", 25, 21, (COLOR_BLUE << 4) | TXT_CYAN);
            }
            
            if(error_line >= 0 && error_msg[0]) {
                for(int i = 0; i < 80; i++) put_pixel(i, 22, COLOR_RED, TXT_WHITE, S_HLINE);
                kprint_at("ERROR: ", 2, 22, (COLOR_RED << 4) | TXT_YELLOW);
                kprint_at(error_msg, 9, 22, (COLOR_RED << 4) | TXT_WHITE);
                kprint_at(" line ", 50, 22, (COLOR_RED << 4) | TXT_YELLOW);
                kprint_int_at(error_line + 1, 56, 22, (COLOR_RED << 4) | TXT_GREEN);
            }
            
            for(int i = 0; i < 20 && scroll + i < line_count; i++) {
                int y = 2 + i;
                int line_idx = scroll + i;
                
                for(int x = 0; x < 80; x++) put_pixel(x, y, COLOR_BLACK, TXT_WHITE, ' ');
                
                for(int j = 0; j < 200 && lines[line_idx][j]; j++) {
                    char c = lines[line_idx][j];
                    uint8_t color = COLOR_DEFAULT;
                    
                    if(c == '"') color = COLOR_STRING;
                    else if((c >= '0' && c <= '9')) color = COLOR_NUMBER;
                    else if(c == '#') color = COLOR_COMMENT;
                    else color = COLOR_DEFAULT;
                    
                    if(line_idx == error_line) color = COLOR_ERROR;
                    
                    char s[2] = {c, 0};
                    kprint_at(s, 2 + j, y, (COLOR_BLACK << 4) | color);
                }
            }
            
            int cursor_y = 2 + cursor_line - scroll;
            if(cursor_y >= 2 && cursor_y < 22 && cursor_col >= 0 && cursor_col < 200) {
                uint16_t* video = (uint16_t*)0xB8000;
                int pos = cursor_y * 80 + 2 + cursor_col;
                video[pos] = (video[pos] & 0xFF00) | 0x70;
            }
            
            redraw = 0;
        }
        
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc < 0x80) {
                if(sc == 0x3B) {
                    int menu_selected = 0;
                    int menu_running = 1;
                    while(menu_running) {
                        draw_frame(20, 8, 45, 10, COLOR_BLUE, TXT_WHITE);
                        kprint_at("=== FILE MENU ===", 30, 10, (COLOR_BLUE << 4) | TXT_YELLOW);
                        kprint_at(menu_selected == 0 ? "> Save" : "  Save", 30, 12, (COLOR_BLUE << 4) | (menu_selected == 0 ? TXT_GREEN : TXT_WHITE));
                        kprint_at(menu_selected == 1 ? "> Save As" : "  Save As", 30, 13, (COLOR_BLUE << 4) | (menu_selected == 1 ? TXT_GREEN : TXT_WHITE));
                        kprint_at(menu_selected == 2 ? "> New File" : "  New File", 30, 14, (COLOR_BLUE << 4) | (menu_selected == 2 ? TXT_GREEN : TXT_WHITE));
                        kprint_at(menu_selected == 3 ? "> Load File" : "  Load File", 30, 15, (COLOR_BLUE << 4) | (menu_selected == 3 ? TXT_GREEN : TXT_WHITE));
                        kprint_at(menu_selected == 4 ? "> Exit" : "  Exit", 30, 16, (COLOR_BLUE << 4) | (menu_selected == 4 ? TXT_GREEN : TXT_WHITE));
                        kprint_at("UP/DOWN: Move, ENTER: Select", 25, 19, (COLOR_BLUE << 4) | TXT_CYAN);
                        
                        if(inb(0x64) & 1) {
                            uint8_t k = inb(0x60);
                            if(k < 0x80) {
                                if((k == 0x48 || k == 0x11) && menu_selected > 0) menu_selected--;
                                else if((k == 0x50 || k == 0x1F) && menu_selected < 4) menu_selected++;
                                else if(k == 0x1C) {
                                    if(menu_selected == 0) {
                                        char save_text[8192] = {0};
                                        int pos = 0;
                                        for(int i = 0; i < line_count && pos < 8000; i++) {
                                            for(int j = 0; lines[i][j] && pos < 8000; j++) {
                                                save_text[pos++] = lines[i][j];
                                            }
                                            if(i < line_count - 1 && pos < 8000) save_text[pos++] = '\n';
                                        }
                                        
                                        uint16_t db[256];
                                        read_sector(current_dir_sector, db);
                                        int s2 = -1;
                                        for(int i = 0; i < 32; i++) {
                                            char n[12] = {0};
                                            for(int j = 0; j < 11; j++) n[j] = ((char*)db)[i*16 + j];
                                            if(my_strcmp(filename, n) == 0) { s2 = i; break; }
                                        }
                                        if(s2 == -1) {
                                            for(int i = 0; i < 32; i++) {
                                                char n[12] = {0};
                                                for(int j = 0; j < 11; j++) n[j] = ((char*)db)[i*16 + j];
                                                if(n[0] == 0) { s2 = i; break; }
                                            }
                                        }
                                        if(s2 != -1) {
                                            uint16_t dbuf[256] = {0};
                                            for(int i = 0; save_text[i] && i < 510; i++) {
                                                if(i % 2 == 0) dbuf[i/2] = save_text[i];
                                                else dbuf[i/2] |= (save_text[i] << 8);
                                            }
                                            int sect = 500 + s2 + (current_dir_sector - 100) * 32;
                                            write_sector(sect, dbuf);
                                            for(int j = 0; j < 11 && filename[j]; j++) ((char*)db)[s2*16 + j] = filename[j];
                                            ((char*)db)[s2*16 + 11] = 0;
                                            db[s2*8 + 6] = sect;
                                            db[s2*8 + 7] = pos;
                                            write_sector(current_dir_sector, db);
                                            modified = 0;
                                            kprint_color("\nSaved!\n", TXT_GREEN);
                                            for(volatile int d = 0; d < 5000000; d++);
                                        }
                                    } else if(menu_selected == 1) {
                                        kprint_at("Filename: ", 25, 20, (COLOR_BLACK << 4) | TXT_CYAN);
                                        char new_name[256] = {0};
                                        int np = 0;
                                        while(np < 255 && !(inb(0x64) & 1 && inb(0x60) == 0x1C)) {
                                            if(inb(0x64) & 1) {
                                                uint8_t k = inb(0x60);
                                                if(k == 0x0E && np > 0) { np--; kprint("\b \b"); }
                                                else if(k >= 0x10 && k <= 0x19 && np < 255) {
                                                    new_name[np++] = "qwertyuiop"[k - 0x10];
                                                    kprint_char(new_name[np-1]);
                                                } else if(k >= 0x1E && k <= 0x26 && np < 255) {
                                                    new_name[np++] = "asdfghjkl"[k - 0x1E];
                                                    kprint_char(new_name[np-1]);
                                                } else if(k >= 0x2C && k <= 0x32 && np < 255) {
                                                    new_name[np++] = "zxcvbnm"[k - 0x2C];
                                                    kprint_char(new_name[np-1]);
                                                } else if(k >= 0x02 && k <= 0x0B && np < 255) {
                                                    new_name[np++] = "1234567890"[k - 0x02];
                                                    kprint_char(new_name[np-1]);
                                                }
                                            }
                                        }
                                        new_name[np] = 0;
                                        my_strcpy(filename, new_name);
                                        modified = 1;
                                    } else if(menu_selected == 2) {
                                        for(int i = 0; i < line_count; i++) lines[i][0] = 0;
                                        lines[0][0] = 0;
                                        line_count = 1;
                                        cursor_line = 0;
                                        cursor_col = 0;
                                        scroll = 0;
                                        modified = 1;
                                        my_strcpy(filename, "untitled.wnc");
                                    } else if(menu_selected == 3) {
                                        kprint_at("Filename: ", 25, 20, (COLOR_BLACK << 4) | TXT_CYAN);
                                        char load_name[256] = {0};
                                        int np = 0;
                                        while(np < 255 && !(inb(0x64) & 1 && inb(0x60) == 0x1C)) {
                                            if(inb(0x64) & 1) {
                                                uint8_t k = inb(0x60);
                                                if(k == 0x0E && np > 0) { np--; kprint("\b \b"); }
                                                else if(k >= 0x10 && k <= 0x19 && np < 255) {
                                                    load_name[np++] = "qwertyuiop"[k - 0x10];
                                                    kprint_char(load_name[np-1]);
                                                } else if(k >= 0x1E && k <= 0x26 && np < 255) {
                                                    load_name[np++] = "asdfghjkl"[k - 0x1E];
                                                    kprint_char(load_name[np-1]);
                                                } else if(k >= 0x2C && k <= 0x32 && np < 255) {
                                                    load_name[np++] = "zxcvbnm"[k - 0x2C];
                                                    kprint_char(load_name[np-1]);
                                                } else if(k >= 0x02 && k <= 0x0B && np < 255) {
                                                    load_name[np++] = "1234567890"[k - 0x02];
                                                    kprint_char(load_name[np-1]);
                                                }
                                            }
                                        }
                                        load_name[np] = 0;
                                        my_strcpy(filename, load_name);
                                        
                                        uint16_t db[256];
                                        read_sector(current_dir_sector, db);
                                        int s3 = -1;
                                        for(int i = 0; i < 32; i++) {
                                            char n[12] = {0};
                                            for(int j = 0; j < 11; j++) n[j] = ((char*)db)[i*16 + j];
                                            if(my_strcmp(filename, n) == 0) { s3 = i; break; }
                                        }
                                        if(s3 != -1) {
                                            int sect = db[s3*8 + 6];
                                            int sz = db[s3*8 + 7];
                                            uint16_t dbuf[256];
                                            read_sector(sect, dbuf);
                                            char txt[4096] = {0};
                                            for(int i = 0; i < sz && i < 4095; i++) {
                                                if(i % 2 == 0) txt[i] = dbuf[i/2] & 0xFF;
                                                else txt[i] = (dbuf[i/2] >> 8) & 0xFF;
                                            }
                                            int l = 0, c = 0;
                                            for(int i = 0; txt[i] && l < 500; i++) {
                                                if(txt[i] == '\n') { lines[l][c] = 0; l++; c = 0; }
                                                else if(c < 199) lines[l][c++] = txt[i];
                                            }
                                            if(c > 0 || l == 0) { lines[l][c] = 0; line_count = l + 1; }
                                            else line_count = l;
                                            cursor_line = 0; cursor_col = 0; scroll = 0; modified = 0;
                                        }
                                    }
                                    menu_running = 0;
                                    redraw = 1;
                                } else if(k == 0x01) {
                                    menu_running = 0;
                                    redraw = 1;
                                }
                            }
                        }
                        for(volatile int d = 0; d < 10000; d++);
                    }
                }
                else if(sc == 0x3C) {
                    clear_screen();
                    kprint_color("=== COMPILING ===\n", TXT_CYAN);
                    if(check_syntax()) {
                        kprint_color("Syntax OK!\n", TXT_GREEN);
                        kprint_color("Running...\n", TXT_YELLOW);
                        char full_code[16384] = {0};
                        int pos = 0;
                        for(int i = 0; i < line_count && pos < 16000; i++) {
                            for(int j = 0; lines[i][j] && pos < 16000; j++) full_code[pos++] = lines[i][j];
                            if(i < line_count - 1 && pos < 16000) full_code[pos++] = '\n';
                        }
                        wnc_execute(full_code);
                        kprint_color("\n=== DONE ===\n", TXT_GREEN);
                    } else {
                        kprint_color("Compilation failed!\n", TXT_RED);
                    }
                    kprint_color("Press any key...\n", TXT_YELLOW);
                    while(!(inb(0x64) & 1));
                    while(inb(0x64) & 1) inb(0x60);
                    redraw = 1;
                }
                else if(sc == 0x3D) {
                    if(check_syntax()) {
                        kprint_color("\nSyntax OK!\n", TXT_GREEN);
                        for(volatile int d = 0; d < 3000000; d++);
                        error_line = -1;
                    }
                    redraw = 1;
                }
                else if(sc == 0x3E) {
                    char save_text[8192] = {0};
                    int pos = 0;
                    for(int i = 0; i < line_count && pos < 8000; i++) {
                        for(int j = 0; lines[i][j] && pos < 8000; j++) save_text[pos++] = lines[i][j];
                        if(i < line_count - 1 && pos < 8000) save_text[pos++] = '\n';
                    }
                    uint16_t db[256];
                    read_sector(current_dir_sector, db);
                    int s2 = -1;
                    for(int i = 0; i < 32; i++) {
                        char n[12] = {0};
                        for(int j = 0; j < 11; j++) n[j] = ((char*)db)[i*16 + j];
                        if(my_strcmp(filename, n) == 0) { s2 = i; break; }
                    }
                    if(s2 == -1) {
                        for(int i = 0; i < 32; i++) {
                            char n[12] = {0};
                            for(int j = 0; j < 11; j++) n[j] = ((char*)db)[i*16 + j];
                            if(n[0] == 0) { s2 = i; break; }
                        }
                    }
                    if(s2 != -1) {
                        uint16_t dbuf[256] = {0};
                        for(int i = 0; save_text[i] && i < 510; i++) {
                            if(i % 2 == 0) dbuf[i/2] = save_text[i];
                            else dbuf[i/2] |= (save_text[i] << 8);
                        }
                        int sect = 500 + s2 + (current_dir_sector - 100) * 32;
                        write_sector(sect, dbuf);
                        for(int j = 0; j < 11 && filename[j]; j++) ((char*)db)[s2*16 + j] = filename[j];
                        ((char*)db)[s2*16 + 11] = 0;
                        db[s2*8 + 6] = sect;
                        db[s2*8 + 7] = pos;
                        write_sector(current_dir_sector, db);
                        modified = 0;
                        kprint_color("\nSaved!\n", TXT_GREEN);
                        for(volatile int d = 0; d < 3000000; d++);
                    }
                    redraw = 1;
                }
                else if(sc == 0x3F) {
                    for(int i = 0; i < line_count; i++) lines[i][0] = 0;
                    lines[0][0] = 0;
                    line_count = 1;
                    cursor_line = 0;
                    cursor_col = 0;
                    scroll = 0;
                    modified = 1;
                    my_strcpy(filename, "untitled.wnc");
                    redraw = 1;
                }
                else if(sc == 0x40) {
                    show_help_window = !show_help_window;
                    redraw = 1;
                }
                else if(sc == 0x41) {
                    show_suggestions = !show_suggestions;
                    redraw = 1;
                }
                else if(sc == 0x01) {
                    if(modified) {
                        draw_frame(25, 10, 30, 6, COLOR_RED, TXT_WHITE);
                        kprint_at("Save changes?", 32, 12, (COLOR_RED << 4) | TXT_YELLOW);
                        kprint_at("[Y] Yes  [N] No", 32, 14, (COLOR_RED << 4) | TXT_CYAN);
                        int choice = 0;
                        while(choice == 0) {
                            if(inb(0x64) & 1) {
                                uint8_t k = inb(0x60);
                                if(k == 0x15 || k == 0x2C) {
                                    char save_text[8192] = {0};
                                    int pos = 0;
                                    for(int i = 0; i < line_count && pos < 8000; i++) {
                                        for(int j = 0; lines[i][j] && pos < 8000; j++) save_text[pos++] = lines[i][j];
                                        if(i < line_count - 1 && pos < 8000) save_text[pos++] = '\n';
                                    }
                                    uint16_t db[256];
                                    read_sector(current_dir_sector, db);
                                    int s2 = -1;
                                    for(int i = 0; i < 32; i++) {
                                        char n[12] = {0};
                                        for(int j = 0; j < 11; j++) n[j] = ((char*)db)[i*16 + j];
                                        if(my_strcmp(filename, n) == 0) { s2 = i; break; }
                                    }
                                    if(s2 == -1) {
                                        for(int i = 0; i < 32; i++) {
                                            char n[12] = {0};
                                            for(int j = 0; j < 11; j++) n[j] = ((char*)db)[i*16 + j];
                                            if(n[0] == 0) { s2 = i; break; }
                                        }
                                    }
                                    if(s2 != -1) {
                                        uint16_t dbuf[256] = {0};
                                        for(int i = 0; save_text[i] && i < 510; i++) {
                                            if(i % 2 == 0) dbuf[i/2] = save_text[i];
                                            else dbuf[i/2] |= (save_text[i] << 8);
                                        }
                                        int sect = 500 + s2 + (current_dir_sector - 100) * 32;
                                        write_sector(sect, dbuf);
                                        for(int j = 0; j < 11 && filename[j]; j++) ((char*)db)[s2*16 + j] = filename[j];
                                        ((char*)db)[s2*16 + 11] = 0;
                                        db[s2*8 + 6] = sect;
                                        db[s2*8 + 7] = pos;
                                        write_sector(current_dir_sector, db);
                                    }
                                    choice = 1;
                                } else if(k == 0x31 || k == 0x35) {
                                    choice = 1;
                                }
                            }
                        }
                    }
                    running = 0;
                }
                else if(sc == 0x48 && cursor_line > 0) {
                    cursor_line--;
                    if(cursor_line < scroll) scroll = cursor_line;
                    int len = my_strlen(lines[cursor_line]);
                    if(cursor_col > len) cursor_col = len;
                    redraw = 1;
                }
                else if(sc == 0x50 && cursor_line < line_count - 1) {
                    cursor_line++;
                    if(cursor_line >= scroll + 20) scroll = cursor_line - 19;
                    int len = my_strlen(lines[cursor_line]);
                    if(cursor_col > len) cursor_col = len;
                    redraw = 1;
                }
                else if(sc == 0x4B && cursor_col > 0) {
                    cursor_col--;
                    redraw = 1;
                }
                else if(sc == 0x4D) {
                    int len = my_strlen(lines[cursor_line]);
                    if(cursor_col < len) cursor_col++;
                    redraw = 1;
                }
                else if(sc == 0x0F) {
                    for(int i = 0; i < 4; i++) {
                        int len = my_strlen(lines[cursor_line]);
                        for(int j = len; j >= cursor_col; j--) lines[cursor_line][j+1] = lines[cursor_line][j];
                        lines[cursor_line][cursor_col] = ' ';
                        cursor_col++;
                    }
                    modified = 1;
                    redraw = 1;
                }
                else if(sc == 0x1C) {
                    if(line_count < 500) {
                        for(int i = line_count; i > cursor_line + 1; i--) my_strcpy(lines[i], lines[i-1]);
                        char temp[200];
                        my_strcpy(temp, lines[cursor_line] + cursor_col);
                        lines[cursor_line][cursor_col] = '\0';
                        my_strcpy(lines[cursor_line + 1], temp);
                        line_count++;
                        cursor_line++;
                        cursor_col = 0;
                        if(cursor_line >= scroll + 20) scroll = cursor_line - 19;
                        modified = 1;
                        redraw = 1;
                    }
                }
                else if(sc == 0x0E && cursor_col > 0) {
                    int len = my_strlen(lines[cursor_line]);
                    for(int i = cursor_col - 1; i < len; i++) lines[cursor_line][i] = lines[cursor_line][i+1];
                    cursor_col--;
                    modified = 1;
                    redraw = 1;
                }
                else if(sc == 0x0E && cursor_col == 0 && cursor_line > 0) {
                    int prev_len = my_strlen(lines[cursor_line - 1]);
                    int curr_len = my_strlen(lines[cursor_line]);
                    for(int i = 0; i <= curr_len; i++) lines[cursor_line - 1][prev_len + i] = lines[cursor_line][i];
                    for(int i = cursor_line; i < line_count - 1; i++) my_strcpy(lines[i], lines[i+1]);
                    line_count--;
                    cursor_line--;
                    cursor_col = prev_len;
                    if(cursor_line < scroll) scroll = cursor_line;
                    modified = 1;
                    redraw = 1;
                }
                else {
                    char ch = 0;
                    int shift = 0;
                    if(sc == 0x2A || sc == 0x36) shift = 1;
                    if(sc >= 0x02 && sc <= 0x0B) ch = shift ? "!@#$%^&*()"[sc-0x02] : "1234567890"[sc-0x02];
                    else if(sc >= 0x10 && sc <= 0x19) ch = shift ? "QWERTYUIOP"[sc-0x10] : "qwertyuiop"[sc-0x10];
                    else if(sc >= 0x1E && sc <= 0x26) ch = shift ? "ASDFGHJKL"[sc-0x1E] : "asdfghjkl"[sc-0x1E];
                    else if(sc >= 0x2C && sc <= 0x32) ch = shift ? "ZXCVBNM"[sc-0x2C] : "zxcvbnm"[sc-0x2C];
                    else if(sc == 0x39) ch = ' ';
                    else if(sc == 0x0C) ch = shift ? '_' : '-';
                    else if(sc == 0x34) ch = shift ? '>' : '.';
                    else if(sc == 0x33) ch = shift ? '<' : ',';
                    else if(sc == 0x35) ch = shift ? '?' : '/';
                    else if(sc == 0x27) ch = shift ? ':' : ';';
                    else if(sc == 0x28) ch = shift ? '"' : '\'';
                    
                    if(ch) {
                        int len = my_strlen(lines[cursor_line]);
                        if(len < 199) {
                            for(int i = len; i >= cursor_col; i--) lines[cursor_line][i+1] = lines[cursor_line][i];
                            lines[cursor_line][cursor_col] = ch;
                            cursor_col++;
                            modified = 1;
                            redraw = 1;
                        }
                    }
                }
            }
        }
        
        for(volatile int d = 0; d < 1000; d++);
    }
    
    clear_screen();
    kprint_color("IDE closed\n", TXT_GREEN);
}

else if(my_strncmp(input_buffer, "wpm ", 4) == 0) {
    char* cmd = input_buffer + 4;
    while(*cmd == ' ') cmd++;
    
    if(my_strncmp(cmd, "install ", 8) == 0) {
        char* pkg = cmd + 8;
        while(*pkg == ' ') pkg++;
        char url[256];
        char* p = url;
        const char* base = "https://goloforez228-lgtm.github.io/wnka-rep/";
        while(*base) *p++ = *base++;
        const char* q = pkg;
        while(*q) *p++ = *q++;
        *p++ = '.';
        *p++ = 'w';
        *p++ = 'n';
        *p++ = 'c';
        *p = '\0';
        
        kprint_color("Installing: ", TXT_CYAN);
        kprint(pkg);
        kprint(".wnc\n");
        int success = 0;
        uint8_t buffer[4096];
        int size = 0;
        
        for(int attempt = 1; attempt <= 3; attempt++) {
            kprint_color("[Attempt ", TXT_YELLOW);
            kprint_int(attempt);
            kprint_color("/3] ", TXT_YELLOW);
            size = http_get_real(url, buffer, sizeof(buffer) - 1);
            if(size > 10) {
                int is_error = 0;
                if(size > 100 && buffer[0] == '<') {
                    for(int i = 0; i < 100 && i < size; i++) {
                        if(buffer[i] == '4' && buffer[i+1] == '0' && buffer[i+2] == '4') {
                            is_error = 1;
                            break;
                        }
                        if(buffer[i] == 'e' && buffer[i+1] == 'r' && buffer[i+2] == 'r') {
                            is_error = 1;
                            break;
                        }
                    }
                }
                
                if(!is_error) {
                    success = 1;
                    kprint_color("OK!\n", TXT_GREEN);
                    break;
                } else {
                    kprint_color("File not found (404)\n", TXT_RED);
                }
            } else {
                kprint_color("Connection failed\n", TXT_RED);
            }
            
            if(attempt < 3) {
                kprint_color("Retrying in 1 second...\n", TXT_YELLOW);
                for(volatile int d = 0; d < 10000000; d++);
            }
        }
        
        if(!success) {
            kprint_color("\n✗ Failed to download after 3 attempts!\n", TXT_RED);
            kprint_color("Package not found in repository.\n", TXT_YELLOW);
            kprint_color("Check: https://github.com/goloforez228-lgtm/wnka-rep\n", TXT_CYAN);
            return;
        }
        
        if(size > 0 && buffer[0] != 'p' && buffer[0] != '#' && buffer[0] != 'l' && 
           buffer[0] != 'i' && buffer[0] != 'w' && buffer[0] != 'r') {
            kprint_color("Warning: Downloaded file may not be a valid WnkC script\n", TXT_YELLOW);
        }
        
        char filename[64];
        char* r = filename;
        const char* s = pkg;
        while(*s) *r++ = *s++;
        *r++ = '.';
        *r++ = 'w';
        *r++ = 'n';
        *r++ = 'c';
        *r = '\0';
        
        uint16_t dir_buf[256];
        read_sector(current_dir_sector, dir_buf);
        
        int slot = -1;
        for(int i = 0; i < 32; i++) {
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
            if(name[0] == 0) {
                slot = i;
                break;
            }
        }
        
        if(slot == -1) {
            kprint_color("Directory full!\n", TXT_RED);
            return;
        }
        
        for(int i = 0; i < 11 && filename[i]; i++) {
            ((char*)dir_buf)[slot*16 + i] = filename[i];
        }
        ((char*)dir_buf)[slot*16 + 11] = 0;
        
        static int file_counter = 500;
        int file_sector = file_counter++;
        dir_buf[slot*8 + 6] = file_sector;
        dir_buf[slot*8 + 7] = size;
        write_sector(current_dir_sector, dir_buf);
        
        uint16_t data_buf[256] = {0};
        for(int i = 0; i < size && i < 510; i++) {
            if(i % 2 == 0) data_buf[i/2] = buffer[i];
            else data_buf[i/2] |= (buffer[i] << 8);
        }
        write_sector(file_sector, data_buf);
        
        kprint_color("\n✓ Installed: ", TXT_GREEN);
        kprint(filename);
        kprint(" (");
        kprint_int(size);
        kprint(" bytes)\n");
        kprint_color("Run with: wnkc ", TXT_CYAN);
        kprint(filename);
        kprint("\n");
    }
}

else if(my_strcmp(input_buffer, "ls") == 0) {
    uint16_t dir_buf[256];
    read_sector(current_dir_sector, dir_buf);
    
    kprint("\n=== DIRECTORY ===\n");
    int count = 0;
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
        
        if(name[0] != 0) {
            int is_dir = ((char*)dir_buf)[i*16 + 11] == 1;
            int size = dir_buf[i*8 + 7];
            
            if(is_dir) {
                kprint("  [");
                kprint_color("DIR", TXT_CYAN);
                kprint("] ");
            } else {
                kprint("  [");
                kprint_color("FILE", TXT_GREEN);
                kprint("] ");
            }
            
            kprint(name);
            
            if(!is_dir) {
                kprint(" (");
                kprint_int(size);
                kprint(" bytes)");
            }
            kprint("\n");
            count++;
        }
    }
    if(count == 0) {
        kprint("  (empty)\n");
    }
    kprint("================\n");
}

else if(my_strncmp(input_buffer, "rmdir ", 6) == 0) {
    char* dirname = input_buffer + 6;
    trim(dirname);
    
    uint16_t dir_buf[256];
    read_sector(current_dir_sector, dir_buf);
    
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
        if(my_strcmp(dirname, name) == 0) {
            
            uint16_t folder_sector = dir_buf[i*8 + 6];
            uint16_t folder_buf[256];
            read_sector(folder_sector, folder_buf);
            
            for(int j = 0; j < 32; j++) {
                char fname[12] = {0};
                for(int k = 0; k < 11; k++) fname[k] = ((char*)folder_buf)[j*16 + k];
                if(fname[0] != 0) {
                    for(int k = 0; k < 16; k++) ((char*)folder_buf)[j*16 + k] = 0;
                }
            }
            write_sector(folder_sector, folder_buf);
            
            for(int j = 0; j < 16; j++) ((char*)dir_buf)[i*16 + j] = 0;
            write_sector(current_dir_sector, dir_buf);
            
            kprint("Removed directory: ");
            kprint(dirname);
            kprint("\n");
            break;
        }
    }
}

else if(my_strcmp(input_buffer, "tree") == 0) {
    kprint("\n=== DIRECTORY TREE ===\n");
    kprint("/\n");
    
    uint16_t root_buf[256];
    read_sector(100, root_buf);
    
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)root_buf)[i*16 + j];
        if(name[0] != 0 && ((char*)root_buf)[i*16 + 11] == 1) {
            kprint("  |-- ");
            kprint(name);
            kprint("\n");
        }
    }
    kprint("==================\n");
}

else if(my_strncmp(input_buffer, "create ", 7) == 0) {
    char* filename = input_buffer + 7;
    int len = my_strlen(filename);
    if(len > 0 && filename[len-1] == ' ') filename[len-1] = '\0';
    
    uint16_t dir_buf[256];
    read_sector(current_dir_sector, dir_buf);
    
    int exists = 0;
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
        if(my_strcmp(filename, name) == 0) {
            exists = 1;
            break;
        }
    }
    
    if(exists) {
        kprint("File already exists: ");
        kprint(filename);
        kprint("\n");
    } else {
        int slot = -1;
        for(int i = 0; i < 32; i++) {
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
            if(name[0] == 0) {
                slot = i;
                break;
            }
        }
        
        if(slot == -1) {
            kprint("ERROR: No free slots (max 32 files)\n");
        } else {
            for(int j = 0; j < 16; j++) ((char*)dir_buf)[slot*16 + j] = 0;
            for(int j = 0; j < 11 && filename[j]; j++) {
                ((char*)dir_buf)[slot*16 + j] = filename[j];
            }
            ((char*)dir_buf)[slot*16 + 11] = 0;
            dir_buf[slot*8 + 6] = 200 + slot + (current_dir_sector - 100) * 32;
            dir_buf[slot*8 + 7] = 0;
            
            write_sector(current_dir_sector, dir_buf);
            kprint("Created: ");
            kprint(filename);
            kprint("\n");
        }
    }
}

else if(my_strncmp(input_buffer, "save ", 5) == 0) {
    char* rest = input_buffer + 5;
    char filename[12] = {0};
    char* data = NULL;
    
    int i = 0;
    while(rest[i] && rest[i] != ' ' && i < 11) {
        filename[i] = rest[i];
        i++;
    }
    
    if(rest[i] == ' ') {
        data = rest + i + 1;
        while(*data == ' ') data++;
    }
    
    if(data == NULL || data[0] == '\0') {
        kprint("Usage: save <filename> <text>\n");
    } else {
        uint16_t dir_buf[256];
        read_sector(current_dir_sector, dir_buf);
        
        int slot = -1;
        for(int i = 0; i < 32; i++) {
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
            if(my_strcmp(filename, name) == 0) {
                slot = i;
                break;
            }
        }
        
        if(slot == -1) {
            kprint("File not found. Use 'create' first.\n");
        } else {
            uint16_t data_buf[256] = {0};
            int len = 0;
            for(int j = 0; data[j] && j < 510; j++) {
                if(j % 2 == 0) data_buf[j/2] = data[j];
                else data_buf[j/2] |= (data[j] << 8);
                len++;
            }
            
            int sector = dir_buf[slot*8 + 6];
            write_sector(sector, data_buf);
            
            dir_buf[slot*8 + 7] = len;
            write_sector(current_dir_sector, dir_buf);
            
            kprint("Saved: ");
            kprint(filename);
            kprint(" (");
            kprint_int(len);
            kprint(" bytes)\n");
        }
    }
}
else if(my_strncmp(input_buffer, "delete ", 7) == 0) {
    char* filename = input_buffer + 7;
    trim(filename);
    
    uint16_t dir_buf[256];
    read_sector(current_dir_sector, dir_buf);
    
    int found = 0;
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
        if(my_strcmp(filename, name) == 0) {
            int is_dir = ((char*)dir_buf)[i*16 + 11];
            if(is_dir == 1) {
                kprint("Cannot delete directory. Use 'rmdir' instead.\n");
                return;
            }
            for(int j = 0; j < 16; j++) ((char*)dir_buf)[i*16 + j] = 0;
            write_sector(current_dir_sector, dir_buf);
            kprint("Deleted: ");
            kprint(filename);
            kprint("\n");
            found = 1;
            break;
        }
    }
    if(!found) {
        kprint("File not found: ");
        kprint(filename);
        kprint("\n");
    }
}
else if(my_strncmp(input_buffer, "rename ", 7) == 0) {
    char* rest = input_buffer + 7;
    char oldname[12] = {0};
    char newname[12] = {0};
    
    int i = 0;
    while(rest[i] && rest[i] != ' ' && i < 11) {
        oldname[i] = rest[i];
        i++;
    }
    
    if(rest[i] == ' ') {
        i++;
        int j = 0;
        while(rest[i] && rest[i] != ' ' && j < 11) {
            newname[j++] = rest[i++];
        }
    }
    
    if(oldname[0] == 0 || newname[0] == 0) {
        kprint("Usage: rename <oldname> <newname>\n");
    } else {
        uint16_t dir_buf[256];
        read_sector(current_dir_sector, dir_buf);
        
        int found = 0;
        for(int i = 0; i < 32; i++) {
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
            if(my_strcmp(oldname, name) == 0) {
                for(int j = 0; j < 11; j++) ((char*)dir_buf)[i*16 + j] = 0;
                for(int j = 0; j < 11 && newname[j]; j++) {
                    ((char*)dir_buf)[i*16 + j] = newname[j];
                }
                write_sector(current_dir_sector, dir_buf);
                kprint("Renamed: ");
                kprint(oldname);
                kprint(" -> ");
                kprint(newname);
                kprint("\n");
                found = 1;
                break;
            }
        }
        if(!found) {
            kprint("File/Directory not found: ");
            kprint(oldname);
            kprint("\n");
        }
    }
}

else if(my_strcmp(input_buffer, "notepad") == 0) {
    char filename[32] = {0};
    
    kprint("Enter filename: ");
    int pos = 0;
    while(pos < 31) {
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc == 0x1C) break;
            if(sc == 0x01) { kprint("\nCancelled\n"); return; }
            
            char ch = 0;
            int shift = 0;
            
            if(sc == 0x2A || sc == 0x36) {
                shift = 1;
                continue;
            }
            
            if(sc >= 0x02 && sc <= 0x0B) {
                const char* normal = "1234567890";
                const char* shifted = "!@#$%^&*()";
                if(shift) ch = shifted[sc - 0x02];
                else ch = normal[sc - 0x02];
            }
            else if(sc >= 0x10 && sc <= 0x19) {
                const char* normal = "qwertyuiop";
                const char* shifted = "QWERTYUIOP";
                if(shift) ch = shifted[sc - 0x10];
                else ch = normal[sc - 0x10];
            }
            else if(sc >= 0x1E && sc <= 0x26) {
                const char* normal = "asdfghjkl";
                const char* shifted = "ASDFGHJKL";
                if(shift) ch = shifted[sc - 0x1E];
                else ch = normal[sc - 0x1E];
            }
            else if(sc >= 0x2C && sc <= 0x32) {
                const char* normal = "zxcvbnm";
                const char* shifted = "ZXCVBNM";
                if(shift) ch = shifted[sc - 0x2C];
                else ch = normal[sc - 0x2C];
            }
            else if(sc == 0x39) {
                ch = ' ';
            }
            else if(sc == 0x0C) {
                if(shift) ch = '_';
                else ch = '-';
            }
            else if(sc == 0x0D) {
                if(shift) ch = '+';
                else ch = '=';
            }
            else if(sc == 0x34) {
                if(shift) ch = '>';
                else ch = '.';
            }
            else if(sc == 0x33) {
                if(shift) ch = '<';
                else ch = ',';
            }
            else if(sc == 0x35) {
                if(shift) ch = '?';
                else ch = '/';
            }
            else if(sc == 0x27) {
                if(shift) ch = ':';
                else ch = ';';
            }
            else if(sc == 0x28) {
                if(shift) ch = '"';
                else ch = '\'';
            }
            else if(sc == 0x29) {
                if(shift) ch = '~';
                else ch = '`';
            }
            else if(sc == 0x1A) {
                if(shift) ch = '{';
                else ch = '[';
            }
            else if(sc == 0x1B) {
                if(shift) ch = '}';
                else ch = ']';
            }
            else if(sc == 0x2B) {
                if(shift) ch = '|';
                else ch = '\\';
            }
            else if(sc == 0x0E && pos > 0) {
                pos--;
                kprint("\b \b");
            }
            
            if(ch && pos < 31) {
                filename[pos++] = ch;
                kprint_char(ch);
            }
        }
    }
    filename[pos] = '\0';
    
    if(filename[0] == '\0') {
        kprint("No filename entered\n");
    } else {
        char lines[100][80];
        int line_count = 0;
        int cursor_line = 0;
        int cursor_col = 0;
        int scroll = 0;
        int running = 1;
        int need_redraw = 1;
        int old_cursor_line = -1, old_cursor_col = -1, old_scroll = -1;
        int shift_pressed = 0;
        
        uint16_t dir_buf[256];
        read_sector(current_dir_sector, dir_buf);
        
        int slot = -1;
        for(int i = 0; i < 32; i++) {
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
            if(my_strcmp(filename, name) == 0) {
                slot = i;
                break;
            }
        }
        
        if(slot != -1) {
            int sector = dir_buf[slot*8 + 6];
            int size = dir_buf[slot*8 + 7];
            uint16_t data_buf[256];
            read_sector(sector, data_buf);
            
            char text[4096] = {0};
            for(int i = 0; i < size; i++) {
                if(i % 2 == 0) text[i] = data_buf[i/2] & 0xFF;
                else text[i] = (data_buf[i/2] >> 8) & 0xFF;
            }
            
            int line = 0;
            int col = 0;
            for(int i = 0; text[i] && line < 100; i++) {
                if(text[i] == '\n') {
                    lines[line][col] = '\0';
                    line++;
                    col = 0;
                } else if(col < 79) {
                    lines[line][col++] = text[i];
                }
            }
            if(col > 0 || line == 0) {
                lines[line][col] = '\0';
                line_count = line + 1;
            } else {
                line_count = line;
            }
        } else {
            lines[0][0] = '\0';
            line_count = 1;
        }
        
        clear_screen();
        
        while(running) {
            if(need_redraw || scroll != old_scroll) {
                clear_screen_bg(BLACK);
                
                draw_frame(1, 0, 78, 2, BLUE, TXT_WHITE);
                kprint_at("NOTEPAD - ", 3, 1, (BLUE << 4) | TXT_YELLOW);
                kprint_at(filename, 13, 1, (BLUE << 4) | TXT_WHITE);
                kprint_at("[ESC] Exit  [F1] Save", 55, 1, (BLUE << 4) | TXT_GREEN);
                
                draw_frame(1, 3, 78, 20, GRAY, TXT_WHITE);
                
                int start_line = scroll;
                int end_line = scroll + 18;
                if(end_line > line_count) end_line = line_count;
                
                for(int i = start_line; i < end_line; i++) {
                    int y = 4 + i - scroll;
                    kprint_at(lines[i], 3, y, (GRAY << 4) | TXT_WHITE);
                }
                
                draw_frame(1, 24, 78, 1, BLUE, TXT_WHITE);
                kprint_at("Line: ", 3, 24, (BLUE << 4) | TXT_CYAN);
                kprint_int_at(cursor_line + 1, 10, 24, (BLUE << 4) | TXT_WHITE);
                kprint_at(" Col: ", 18, 24, (BLUE << 4) | TXT_CYAN);
                kprint_int_at(cursor_col + 1, 24, 24, (BLUE << 4) | TXT_WHITE);
                kprint_at(" Lines: ", 32, 24, (BLUE << 4) | TXT_CYAN);
                kprint_int_at(line_count, 40, 24, (BLUE << 4) | TXT_WHITE);
                kprint_at("[F1] Save", 60, 24, (BLUE << 4) | TXT_GREEN);
                if(shift_pressed) kprint_at(" SHIFT", 68, 24, (BLUE << 4) | TXT_YELLOW);
                
                need_redraw = 0;
                old_scroll = scroll;
            }
            
            if(old_cursor_line != cursor_line || old_scroll != scroll) {
                if(old_cursor_line >= scroll && old_cursor_line < scroll + 18) {
                    int y = 4 + old_cursor_line - scroll;
                    kprint_at("                                                                              ", 3, y, (GRAY << 4) | TXT_WHITE);
                    kprint_at(lines[old_cursor_line], 3, y, (GRAY << 4) | TXT_WHITE);
                }
                if(cursor_line >= scroll && cursor_line < scroll + 18) {
                    int y = 4 + cursor_line - scroll;
                    kprint_at("                                                                              ", 3, y, (GRAY << 4) | TXT_WHITE);
                    kprint_at(lines[cursor_line], 3, y, (GRAY << 4) | TXT_WHITE);
                }
                old_cursor_line = cursor_line;
            }
            
            move_cursor(3 + cursor_col, 4 + cursor_line - scroll);
            
            if(inb(0x64) & 1) {
                uint8_t sc = inb(0x60);
                
                if(sc == 0x2A || sc == 0x36) {
                    shift_pressed = 1;
                }
                else if(sc == 0xAA || sc == 0xB6) {
                    shift_pressed = 0;
                }
                else if(sc == 0x01) {
                    running = 0;
                }
                else if(sc == 0x3B) {
                    char save_text[4096] = {0};
                    int p = 0;
                    for(int i = 0; i < line_count && p < 4000; i++) {
                        for(int j = 0; lines[i][j] && p < 4000; j++) {
                            save_text[p++] = lines[i][j];
                        }
                        if(i < line_count - 1 && p < 4000) {
                            save_text[p++] = '\n';
                        }
                    }
                    
                    if(slot == -1) {
                        for(int i = 0; i < 32; i++) {
                            char name[12] = {0};
                            for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
                            if(name[0] == 0) {
                                slot = i;
                                break;
                            }
                        }
                    }
                    
                    if(slot != -1) {
                        uint16_t data_buf[256] = {0};
                        for(int i = 0; save_text[i] && i < 510; i++) {
                            if(i % 2 == 0) data_buf[i/2] = save_text[i];
                            else data_buf[i/2] |= (save_text[i] << 8);
                        }
                        
                        int sector = 200 + slot + (current_dir_sector - 100) * 32;
                        write_sector(sector, data_buf);
                        
                        for(int j = 0; j < 11 && filename[j]; j++) {
                            ((char*)dir_buf)[slot*16 + j] = filename[j];
                        }
                        ((char*)dir_buf)[slot*16 + 11] = 0;
                        dir_buf[slot*8 + 6] = sector;
                        dir_buf[slot*8 + 7] = p;
                        write_sector(current_dir_sector, dir_buf);
                        
                        kprint_at("Saved!           ", 60, 24, (BLUE << 4) | TXT_GREEN);
                        for(volatile int d = 0; d < 500000; d++);
                        kprint_at("                ", 60, 24, (BLUE << 4) | TXT_BLACK);
                    }
                    need_redraw = 1;
                }
                else if(sc == 0x48 && cursor_line > 0) {
                    cursor_line--;
                    if(cursor_line < scroll) scroll = cursor_line;
                    int len = my_strlen(lines[cursor_line]);
                    if(cursor_col > len) cursor_col = len;
                    need_redraw = 1;
                }
                else if(sc == 0x50 && cursor_line < line_count - 1) {
                    cursor_line++;
                    if(cursor_line >= scroll + 18) scroll = cursor_line - 17;
                    int len = my_strlen(lines[cursor_line]);
                    if(cursor_col > len) cursor_col = len;
                    need_redraw = 1;
                }
                else if(sc == 0x4B && cursor_col > 0) {
                    cursor_col--;
                }
                else if(sc == 0x4D) {
                    int len = my_strlen(lines[cursor_line]);
                    if(cursor_col < len) cursor_col++;
                }
                else if(sc == 0x0E && cursor_col > 0) {
                    int len = my_strlen(lines[cursor_line]);
                    for(int i = cursor_col - 1; i < len; i++) {
                        lines[cursor_line][i] = lines[cursor_line][i+1];
                    }
                    cursor_col--;
                    need_redraw = 1;
                }
                else if(sc == 0x0E && cursor_col == 0 && cursor_line > 0) {
                    int prev_len = my_strlen(lines[cursor_line - 1]);
                    int curr_len = my_strlen(lines[cursor_line]);
                    for(int i = 0; i <= curr_len; i++) {
                        lines[cursor_line - 1][prev_len + i] = lines[cursor_line][i];
                    }
                    for(int i = cursor_line; i < line_count - 1; i++) {
                        my_strcpy(lines[i], lines[i+1]);
                    }
                    line_count--;
                    cursor_line--;
                    cursor_col = prev_len;
                    if(cursor_line < scroll) scroll = cursor_line;
                    need_redraw = 1;
                }
                else if(sc == 0x1C) {
                    if(line_count < 100) {
                        for(int i = line_count; i > cursor_line + 1; i--) {
                            my_strcpy(lines[i], lines[i-1]);
                        }
                        char temp[80];
                        my_strcpy(temp, lines[cursor_line] + cursor_col);
                        lines[cursor_line][cursor_col] = '\0';
                        my_strcpy(lines[cursor_line + 1], temp);
                        line_count++;
                        cursor_line++;
                        cursor_col = 0;
                        if(cursor_line >= scroll + 18) scroll = cursor_line - 17;
                        need_redraw = 1;
                    }
                }
                else {
                    char ch = 0;
                    if(sc >= 0x02 && sc <= 0x0B) {
                        const char* normal = "1234567890";
                        const char* shifted = "!@#$%^&*()";
                        if(shift_pressed) ch = shifted[sc - 0x02];
                        else ch = normal[sc - 0x02];
                    }
                    else if(sc >= 0x10 && sc <= 0x19) {
                        const char* normal = "qwertyuiop";
                        const char* shifted = "QWERTYUIOP";
                        if(shift_pressed) ch = shifted[sc - 0x10];
                        else ch = normal[sc - 0x10];
                    }
                    else if(sc >= 0x1E && sc <= 0x26) {
                        const char* normal = "asdfghjkl";
                        const char* shifted = "ASDFGHJKL";
                        if(shift_pressed) ch = shifted[sc - 0x1E];
                        else ch = normal[sc - 0x1E];
                    }
                    else if(sc >= 0x2C && sc <= 0x32) {
                        const char* normal = "zxcvbnm";
                        const char* shifted = "ZXCVBNM";
                        if(shift_pressed) ch = shifted[sc - 0x2C];
                        else ch = normal[sc - 0x2C];
                    }
                    else if(sc == 0x39) ch = ' ';
                    else if(sc == 0x0C) ch = shift_pressed ? '_' : '-';
                    else if(sc == 0x0D) ch = shift_pressed ? '+' : '=';
                    else if(sc == 0x34) ch = shift_pressed ? '>' : '.';
                    else if(sc == 0x33) ch = shift_pressed ? '<' : ',';
                    else if(sc == 0x35) ch = shift_pressed ? '?' : '/';
                    else if(sc == 0x27) ch = shift_pressed ? ':' : ';';
                    else if(sc == 0x28) ch = shift_pressed ? '"' : '\'';
                    else if(sc == 0x29) ch = shift_pressed ? '~' : '`';
                    else if(sc == 0x1A) ch = shift_pressed ? '{' : '[';
                    else if(sc == 0x1B) ch = shift_pressed ? '}' : ']';
                    else if(sc == 0x2B) ch = shift_pressed ? '|' : '\\';
                    
                    if(ch) {
                        int len = my_strlen(lines[cursor_line]);
                        if(len < 79) {
                            for(int i = len; i >= cursor_col; i--) {
                                lines[cursor_line][i+1] = lines[cursor_line][i];
                            }
                            lines[cursor_line][cursor_col] = ch;
                            cursor_col++;
                            need_redraw = 1;
                        }
                    }
                }
            }
        }
        clear_screen();
    }
}
else if(my_strncmp(input_buffer, "copy ", 5) == 0) {
    char* rest = input_buffer + 5;
    char source[32] = {0};
    char dest[32] = {0};
    int i = 0;
    while(rest[i] && rest[i] != ' ' && i < 31) {
        source[i] = rest[i];
        i++;
    }
    if(rest[i] == ' ') {
        i++;
        int j = 0;
        while(rest[i] && rest[i] != ' ' && j < 31) {
            dest[j++] = rest[i++];
        }
    }
    
    if(source[0] == 0 || dest[0] == 0) {
        kprint("Usage: copy <source> <dest>\n");
    } else {
        uint16_t dir_buf[256];
        read_sector(current_dir_sector, dir_buf);
        
        int src_slot = -1;
        uint16_t src_sector = 0;
        uint16_t src_size = 0;
        
        for(int i = 0; i < 32; i++) {
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
            if(my_strcmp(source, name) == 0) {
                src_slot = i;
                src_sector = dir_buf[i*8 + 6];
                src_size = dir_buf[i*8 + 7];
                break;
            }
        }
        
        if(src_slot == -1) {
            kprint("Source file not found: ");
            kprint(source);
            kprint("\n");
        } else {
            int dest_slot = -1;
            for(int i = 0; i < 32; i++) {
                char name[12] = {0};
                for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
                if(my_strcmp(dest, name) == 0) {
                    dest_slot = i;
                    break;
                }
            }
            
            if(dest_slot != -1) {
                kprint("Destination file already exists: ");
                kprint(dest);
                kprint("\n");
            } else {
                int free_slot = -1;
                for(int i = 0; i < 32; i++) {
                    char name[12] = {0};
                    for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
                    if(name[0] == 0) {
                        free_slot = i;
                        break;
                    }
                }
                
                if(free_slot == -1) {
                    kprint("Directory full!\n");
                } else {
                    uint16_t src_buf[256];
                    read_sector(src_sector, src_buf);
                    
                    int dest_sector = 200 + free_slot + (current_dir_sector - 100) * 32;
                    write_sector(dest_sector, src_buf);
                    
                    for(int j = 0; j < 11 && dest[j]; j++) {
                        ((char*)dir_buf)[free_slot*16 + j] = dest[j];
                    }
                    ((char*)dir_buf)[free_slot*16 + 11] = 0;
                    dir_buf[free_slot*8 + 6] = dest_sector;
                    dir_buf[free_slot*8 + 7] = src_size;
                    write_sector(current_dir_sector, dir_buf);
                    
                    kprint("Copied: ");
                    kprint(source);
                    kprint(" -> ");
                    kprint(dest);
                    kprint("\n");
                }
            }
        }
    }
}

else if(my_strncmp(input_buffer, "move ", 5) == 0) {
    char* rest = input_buffer + 5;
    char source[32] = {0};
    char dest[32] = {0};
    
    int i = 0;
    while(rest[i] && rest[i] != ' ' && i < 31) {
        source[i] = rest[i];
        i++;
    }
    
    if(rest[i] == ' ') {
        i++;
        int j = 0;
        while(rest[i] && rest[i] != ' ' && j < 31) {
            dest[j++] = rest[i++];
        }
    }
    
    if(source[0] == 0 || dest[0] == 0) {
        kprint("Usage: move <source> <dest>\n");
        kprint("Examples:\n");
        kprint("  move file.txt newfile.txt     - rename\n");
        kprint("  move file.txt folder/         - move to folder\n");
        kprint("  move file.txt folder/new.txt  - move and rename\n");
    } else {
        uint16_t src_dir_sector = current_dir_sector;
        uint16_t dst_dir_sector = current_dir_sector;
        char src_name[32] = {0};
        char dst_name[32] = {0};
        char dst_path[32] = {0};
        
        my_strcpy(dst_path, dest);
        
        int has_slash = 0;
        int slash_pos = 0;
        for(int k = 0; dst_path[k]; k++) {
            if(dst_path[k] == '/') {
                has_slash = 1;
                slash_pos = k;
                break;
            }
        }
        
        if(has_slash) {
            char folder_name[32] = {0};
            for(int k = 0; k < slash_pos; k++) {
                folder_name[k] = dst_path[k];
            }
            
            int file_start = slash_pos + 1;
            if(dst_path[file_start] != 0) {
                for(int k = file_start; dst_path[k]; k++) {
                    dst_name[k - file_start] = dst_path[k];
                }
            } else {
                my_strcpy(dst_name, source);
            }
            
            uint16_t dir_buf[256];
            read_sector(current_dir_sector, dir_buf);
            
            int found_folder = -1;
            for(int i = 0; i < 32; i++) {
                char name[12] = {0};
                for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
                if(my_strcmp(folder_name, name) == 0) {
                    int is_dir = ((char*)dir_buf)[i*16 + 11];
                    if(is_dir == 1) {
                        found_folder = i;
                        dst_dir_sector = dir_buf[i*8 + 6];
                        break;
                    }
                }
            }
            
            if(found_folder == -1) {
                kprint("Destination folder not found: ");
                kprint(folder_name);
                kprint("\n");
                return;
            }
        } else {
            my_strcpy(dst_name, dest);
        }
        
        uint16_t src_dir_buf[256];
        read_sector(src_dir_sector, src_dir_buf);
        
        int src_slot = -1;
        uint16_t src_sector = 0;
        uint16_t src_size = 0;
        
        for(int i = 0; i < 32; i++) {
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = ((char*)src_dir_buf)[i*16 + j];
            if(my_strcmp(source, name) == 0) {
                src_slot = i;
                src_sector = src_dir_buf[i*8 + 6];
                src_size = src_dir_buf[i*8 + 7];
                break;
            }
        }
        
        if(src_slot == -1) {
            kprint("Source file not found: ");
            kprint(source);
            kprint("\n");
        } else {
            uint16_t dst_dir_buf[256];
            read_sector(dst_dir_sector, dst_dir_buf);
            
            int dst_slot = -1;
            for(int i = 0; i < 32; i++) {
                char name[12] = {0};
                for(int j = 0; j < 11; j++) name[j] = ((char*)dst_dir_buf)[i*16 + j];
                if(my_strcmp(dst_name, name) == 0) {
                    dst_slot = i;
                    break;
                }
            }
            
            if(dst_slot != -1) {
                kprint("Destination file already exists: ");
                kprint(dst_name);
                kprint("\n");
            } else {
                int free_slot = -1;
                for(int i = 0; i < 32; i++) {
                    char name[12] = {0};
                    for(int j = 0; j < 11; j++) name[j] = ((char*)dst_dir_buf)[i*16 + j];
                    if(name[0] == 0) {
                        free_slot = i;
                        break;
                    }
                }
                
                if(free_slot == -1) {
                    kprint("Destination directory full!\n");
                } else {
                    if(src_dir_sector != dst_dir_sector) {
                        uint16_t data_buf[256];
                        read_sector(src_sector, data_buf);
                        
                        int new_sector = 200 + free_slot + (dst_dir_sector - 100) * 32;
                        write_sector(new_sector, data_buf);
                        
                        for(int j = 0; j < 11 && dst_name[j]; j++) {
                            ((char*)dst_dir_buf)[free_slot*16 + j] = dst_name[j];
                        }
                        ((char*)dst_dir_buf)[free_slot*16 + 11] = 0;
                        dst_dir_buf[free_slot*8 + 6] = new_sector;
                        dst_dir_buf[free_slot*8 + 7] = src_size;
                        write_sector(dst_dir_sector, dst_dir_buf);
                        
                        for(int j = 0; j < 16; j++) ((char*)src_dir_buf)[src_slot*16 + j] = 0;
                        write_sector(src_dir_sector, src_dir_buf);
                    } else {
                        for(int j = 0; j < 11 && dst_name[j]; j++) {
                            ((char*)src_dir_buf)[src_slot*16 + j] = dst_name[j];
                        }
                        for(int j = my_strlen(dst_name); j < 11; j++) {
                            ((char*)src_dir_buf)[src_slot*16 + j] = 0;
                        }
                        write_sector(src_dir_sector, src_dir_buf);
                    }
                    
                    kprint("Moved: ");
                    kprint(source);
                    kprint(" -> ");
                    if(has_slash) {
                        kprint(dest);
                    } else {
                        kprint(dst_name);
                    }
                    kprint("\n");
                }
            }
        }
    }
}

else if(my_strncmp(input_buffer, "find ", 5) == 0) {
    char* search_name = input_buffer + 5;
    trim(search_name);
    
    if(search_name[0] == '\0') {
        kprint("Usage: find <filename>\n");
    } else {
        kprint("\n=== SEARCHING FOR: ");
        kprint(search_name);
        kprint(" ===\n\n");
        
        int found_count = 0;
        
        uint16_t dir_stack[100];
        char dir_names[100][64];
        int stack_ptr = 1;
        
        dir_stack[0] = 100;
        my_strcpy(dir_names[0], "/");
        
        while(stack_ptr > 0) {
            stack_ptr--;
            uint16_t current_sector = dir_stack[stack_ptr];
            char* current_path = dir_names[stack_ptr];
            
            uint16_t dir_buf[256];
            read_sector(current_sector, dir_buf);
            
            for(int i = 0; i < 32; i++) {
                char name[12] = {0};
                for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
                
                if(name[0] != 0) {
                    int is_dir = ((char*)dir_buf)[i*16 + 11] == 1;
                    int size = dir_buf[i*8 + 7];
                    
                    if(my_strcmp(search_name, name) == 0) {
                        found_count++;
                        if(is_dir) {
                            kprint("  [");
                            kprint_color("DIR", TXT_CYAN);
                            kprint("] ");
                        } else {
                            kprint("  [");
                            kprint_color("FILE", TXT_GREEN);
                            kprint("] ");
                        }
                        kprint(current_path);
                        if(my_strcmp(current_path, "/") != 0) kprint("/");
                        kprint(name);
                        if(!is_dir) {
                            kprint(" (");
                            kprint_int(size);
                            kprint(" bytes)");
                        }
                        kprint("\n");
                    }
                    
                    if(is_dir && current_sector != dir_buf[i*8 + 6]) {
                        dir_stack[stack_ptr] = dir_buf[i*8 + 6];
                        
                        if(my_strcmp(current_path, "/") == 0) {
                            my_strcpy(dir_names[stack_ptr], "/");
                            my_strcpy(dir_names[stack_ptr] + 1, name);
                        } else {
                            my_strcpy(dir_names[stack_ptr], current_path);
                            int len = my_strlen(dir_names[stack_ptr]);
                            dir_names[stack_ptr][len] = '/';
                            my_strcpy(dir_names[stack_ptr] + len + 1, name);
                        }
                        stack_ptr++;
                    }
                }
            }
        }
        
        if(found_count == 0) {
            kprint("  No files or directories found matching: ");
            kprint(search_name);
            kprint("\n");
        } else {
            kprint("\nFound ");
            kprint_int(found_count);
            kprint(" item");
            if(found_count > 1) kprint("s");
            kprint("\n");
        }
        kprint("========================\n");
    }
}

else if(my_strcmp(input_buffer, "df") == 0) {
    kprint("\n=== DISK FREE SPACE ===\n");
    
    uint16_t identify_buf[256];
    outb(ata_base_port + 6, 0xA0);
    outb(ata_base_port + 7, 0xEC);
    
    for(volatile int i = 0; i < 100000; i++);
    
    uint8_t status = inb(ata_base_port + 7);
    if(status == 0 || status == 0xFF) {
        kprint("Cannot read disk info\n");
        kprint("========================\n");
        return;
    }
    
    for(int i = 0; i < 256; i++) {
        identify_buf[i] = inw(ata_base_port);
    }
    
    uint32_t total_sectors = identify_buf[60] | (identify_buf[61] << 16);
    if(total_sectors == 0) {
        kprint("Cannot determine disk size\n");
        kprint("========================\n");
        return;
    }
    
    uint32_t total_mb = (total_sectors * 512) / (1024 * 1024);
    uint32_t total_gb = total_mb / 1024;
    
    uint32_t used_sectors = 0;
    uint16_t dir_stack[256];
    int stack_ptr = 0;
    dir_stack[stack_ptr++] = 100;
    
    while(stack_ptr > 0) {
        stack_ptr--;
        uint16_t current_sector = dir_stack[stack_ptr];
        
        uint16_t dir_buf[256];
        read_sector(current_sector, dir_buf);
        
        used_sectors++;
        
        for(int i = 0; i < 32; i++) {
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
            
            if(name[0] != 0) {
                int is_dir = ((char*)dir_buf)[i*16 + 11] == 1;
                int size = dir_buf[i*8 + 7];
                
                if(is_dir) {
                    uint16_t folder_sector = dir_buf[i*8 + 6];
                    if(folder_sector != 0 && folder_sector != current_sector && stack_ptr < 255) {
                        int already = 0;
                        for(int s = 0; s < stack_ptr; s++) {
                            if(dir_stack[s] == folder_sector) {
                                already = 1;
                                break;
                            }
                        }
                        if(!already) {
                            dir_stack[stack_ptr++] = folder_sector;
                        }
                    }
                } else {
                    int file_sectors = (size + 511) / 512;
                    used_sectors += file_sectors;
                }
            }
        }
    }
    
    uint32_t free_sectors = (total_sectors > used_sectors) ? (total_sectors - used_sectors) : 0;
    uint32_t free_mb = (free_sectors * 512) / (1024 * 1024);
    uint32_t free_gb = free_mb / 1024;
    uint32_t used_mb = (used_sectors * 512) / (1024 * 1024);
    
    kprint("Total:  ");
    kprint_int(total_mb);
    kprint(" MB (");
    kprint_int(total_gb);
    kprint(" GB)\n");
    
    kprint("Used:   ");
    kprint_int(used_mb);
    kprint(" MB (");
    kprint_int((used_sectors * 512) / 1024);
    kprint(" KB)\n");
    
    kprint("Free:   ");
    kprint_int(free_mb);
    kprint(" MB (");
    kprint_int(free_gb);
    kprint(" GB)\n");
    
    int percent = 0;
    if(total_sectors > 0) {
        percent = (used_sectors * 100) / total_sectors;
    }
    kprint("Usage:  ");
    kprint_int(percent);
    kprint("%");
    
    kprint(" [");
    int bars = percent / 5;
    if(bars > 20) bars = 20;
    for(int i = 0; i < bars; i++) kprint("#");
    for(int i = bars; i < 20; i++) kprint(".");
    kprint("]\n");
    
    kprint("Sectors: ");
    kprint_int(used_sectors);
    kprint(" used / ");
    kprint_int(total_sectors);
    kprint(" total\n");
    
    kprint("========================\n");
}
else if(my_strncmp(input_buffer, "tar create ", 11) == 0) {
    char* rest = input_buffer + 11;
    char archive_name[32] = {0};
    char files[10][32];
    int file_count = 0;
    
    int i = 0;
    while(rest[i] && rest[i] != ' ' && i < 31) {
        archive_name[i] = rest[i];
        i++;
    }
    
    while(rest[i] == ' ') i++;
    while(rest[i] && file_count < 10) {
        int j = 0;
        while(rest[i] && rest[i] != ' ' && j < 31) {
            files[file_count][j++] = rest[i++];
        }
        files[file_count][j] = '\0';
        file_count++;
        while(rest[i] == ' ') i++;
    }
    
    if(archive_name[0] == 0 || file_count == 0) {
        kprint("Usage: tar create <archive.tar> <file1> [file2] ...\n");
    } else {
        uint8_t tar_data[512 * 100] = {0};
        int tar_pos = 0;
        
        for(int f = 0; f < file_count; f++) {
            uint16_t dir_buf[256];
            read_sector(current_dir_sector, dir_buf);
            
            int slot = -1;
            uint16_t file_sector = 0;
            uint16_t file_size = 0;
            
            for(int i = 0; i < 32; i++) {
                char name[12] = {0};
                for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
                if(my_strcmp(files[f], name) == 0) {
                    slot = i;
                    file_sector = dir_buf[i*8 + 6];
                    file_size = dir_buf[i*8 + 7];
                    break;
                }
            }
            
            if(slot == -1) {
                kprint("File not found: ");
                kprint(files[f]);
                kprint("\n");
                return;
            }
            
            uint8_t header[512] = {0};
            
            for(int j = 0; files[f][j] && j < 99; j++) {
                header[j] = files[f][j];
            }
            
            int size_octal = file_size;
            char size_str[12] = {0};
            for(int j = 10; j >= 0 && size_octal > 0; j--) {
                size_str[j] = '0' + (size_octal % 8);
                size_octal /= 8;
            }
            for(int j = 0; j < 11; j++) {
                if(size_str[j] != 0) header[124 + j] = size_str[j];
                else header[124 + j] = '0';
            }
            
            header[156] = '0';
            
            header[148] = ' ';
            header[149] = ' ';
            header[150] = ' ';
            header[151] = ' ';
            
            for(int j = 0; j < 512; j++) {
                tar_data[tar_pos + j] = header[j];
            }
            tar_pos += 512;
            
            uint16_t data_buf[256];
            read_sector(file_sector, data_buf);
            
            for(int j = 0; j < file_size; j++) {
                if(j % 2 == 0) {
                    tar_data[tar_pos + j] = data_buf[j/2] & 0xFF;
                } else {
                    tar_data[tar_pos + j] = (data_buf[j/2] >> 8) & 0xFF;
                }
            }
            tar_pos += file_size;
            
            while(tar_pos % 512 != 0) {
                tar_data[tar_pos++] = 0;
            }
            
            kprint("Added: ");
            kprint(files[f]);
            kprint(" (");
            kprint_int(file_size);
            kprint(" bytes)\n");
        }
        
        uint16_t dir_buf[256];
        read_sector(current_dir_sector, dir_buf);
        
        int slot = -1;
        for(int i = 0; i < 32; i++) {
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
            if(name[0] == 0) {
                slot = i;
                break;
            }
        }
        
        if(slot == -1) {
            kprint("Directory full!\n");
        } else {
            int sectors_needed = (tar_pos + 511) / 512;
            int archive_sector = 200 + slot + (current_dir_sector - 100) * 32;
            
            for(int s = 0; s < sectors_needed; s++) {
                uint16_t sector_buf[256];
                for(int j = 0; j < 256; j++) {
                    int idx = s * 512 + j * 2;
                    if(idx < tar_pos) {
                        sector_buf[j] = tar_data[idx] | (tar_data[idx + 1] << 8);
                    } else {
                        sector_buf[j] = 0;
                    }
                }
                write_sector(archive_sector + s, sector_buf);
            }
            
            for(int j = 0; j < 11 && archive_name[j]; j++) {
                ((char*)dir_buf)[slot*16 + j] = archive_name[j];
            }
            ((char*)dir_buf)[slot*16 + 11] = 0;
            dir_buf[slot*8 + 6] = archive_sector;
            dir_buf[slot*8 + 7] = tar_pos;
            write_sector(current_dir_sector, dir_buf);
            
            kprint("\nArchive created: ");
            kprint(archive_name);
            kprint(" (");
            kprint_int(tar_pos);
            kprint(" bytes, ");
            kprint_int(sectors_needed);
            kprint(" sectors)\n");
        }
    }
}
else if(my_strncmp(input_buffer, "tar list ", 9) == 0) {
    char* archive_name = input_buffer + 9;
    trim(archive_name);
    
    uint16_t dir_buf[256];
    read_sector(current_dir_sector, dir_buf);
    
    int slot = -1;
    uint16_t archive_sector = 0;
    uint16_t archive_size = 0;
    
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
        if(my_strcmp(archive_name, name) == 0) {
            slot = i;
            archive_sector = dir_buf[i*8 + 6];
            archive_size = dir_buf[i*8 + 7];
            break;
        }
    }
    
    if(slot == -1) {
        kprint("Archive not found: ");
        kprint(archive_name);
        kprint("\n");
    } else {
        kprint("\n=== CONTENTS OF ");
        kprint(archive_name);
        kprint(" ===\n");
        
        uint8_t tar_data[512];
        int pos = 0;
        
        while(pos < archive_size) {
            int sector = archive_sector + pos / 512;
            int offset = pos % 512;
            
            uint16_t sector_buf[256];
            read_sector(sector, sector_buf);
            
            for(int i = 0; i < 512 && pos + i < archive_size; i++) {
                if(i % 2 == 0) tar_data[i] = sector_buf[i/2] & 0xFF;
                else tar_data[i] = (sector_buf[i/2] >> 8) & 0xFF;
            }
            
            char filename[101] = {0};
            for(int i = 0; i < 100 && tar_data[i] != 0; i++) {
                filename[i] = tar_data[i];
            }
            
            if(filename[0] == 0) break;
            
            int size = 0;
            for(int i = 0; i < 11; i++) {
                char c = tar_data[124 + i];
                if(c >= '0' && c <= '7') {
                    size = size * 8 + (c - '0');
                }
            }
            
            kprint("  ");
            kprint(filename);
            kprint(" (");
            kprint_int(size);
            kprint(" bytes)\n");
            
            pos += 512 + ((size + 511) / 512) * 512;
        }
        kprint("========================\n");
    }
}

else if(my_strncmp(input_buffer, "stat ", 5) == 0) {
    char* name = input_buffer + 5;
    trim(name);
    
    if(name[0] == '\0') {
        kprint("Usage: stat <filename>\n");
    } else {
        uint16_t dir_buf[256];
        read_sector(current_dir_sector, dir_buf);
        
        int found = -1;
        uint16_t file_sector = 0;
        uint16_t file_size = 0;
        int is_dir = 0;
        char file_name[12] = {0};
        
        for(int i = 0; i < 32; i++) {
            char entry_name[12] = {0};
            for(int j = 0; j < 11; j++) entry_name[j] = ((char*)dir_buf)[i*16 + j];
            
            if(my_strcmp(name, entry_name) == 0) {
                found = i;
                is_dir = ((char*)dir_buf)[i*16 + 11] == 1;
                file_sector = dir_buf[i*8 + 6];
                file_size = dir_buf[i*8 + 7];
                my_strcpy(file_name, entry_name);
                break;
            }
        }
        
        if(found == -1) {
            kprint("File/Directory not found: ");
            kprint(name);
            kprint("\n");
        } else {
            kprint("\n=== STAT: ");
            kprint(file_name);
            kprint(" ===\n");
            
            if(is_dir) {
                kprint("Type:     ");
                kprint_color("DIRECTORY\n", TXT_CYAN);
            } else {
                kprint("Type:     ");
                kprint_color("FILE\n", TXT_GREEN);
            }
            
            kprint("Size:     ");
            kprint_int(file_size);
            kprint(" bytes (");
            kprint_int(file_size / 1024);
            kprint(" KB)\n");
            
            int sectors = (file_size + 511) / 512;
            if(sectors == 0 && file_size > 0) sectors = 1;
            kprint("Sectors:  ");
            kprint_int(sectors);
            kprint(" (");
            kprint_int(sectors * 512);
            kprint(" bytes on disk)\n");
            
            kprint("Start:    sector ");
            kprint_int(file_sector);
            kprint(" (0x");
            kprint_hex16(file_sector);
            kprint(")\n");
            
            if(current_dir_sector == 100) {
                kprint("Location: /\n");
            } else {
                kprint("Location: /dir_");
                kprint_int(current_dir_sector - 300);
                kprint("\n");
            }
            
            uint8_t hour = inb(0x70); hour = inb(0x71);
            uint8_t minute = inb(0x70); minute = inb(0x71);
            uint8_t second = inb(0x70); second = inb(0x71);
            
            kprint("Modified: ");
            kprint_int(((hour >> 4) * 10) + (hour & 0x0F));
            kprint(":");
            kprint_int(((minute >> 4) * 10) + (minute & 0x0F));
            kprint(":");
            kprint_int(((second >> 4) * 10) + (second & 0x0F));
            kprint("\n");
            
            if(!is_dir && file_size > 0) {
                uint16_t data_buf[256];
                read_sector(file_sector, data_buf);
                
                kprint("\nPreview (first 16 bytes):\n  HEX: ");
                for(int i = 0; i < 16 && i < file_size; i++) {
                    uint8_t byte;
                    if(i % 2 == 0) byte = data_buf[i/2] & 0xFF;
                    else byte = (data_buf[i/2] >> 8) & 0xFF;
                    kprint_hex8(byte);
                    kprint(" ");
                }
                
                kprint("\n  ASCII: ");
                for(int i = 0; i < 16 && i < file_size; i++) {
                    uint8_t byte;
                    if(i % 2 == 0) byte = data_buf[i/2] & 0xFF;
                    else byte = (data_buf[i/2] >> 8) & 0xFF;
                    if(byte >= 32 && byte <= 126) {
                        char s[2] = {(char)byte, 0};
                        kprint(s);
                    } else {
                        kprint(".");
                    }
                }
                kprint("\n");
            }
            
            kprint("========================\n");
        }
    }
}

else if(my_strncmp(input_buffer, "filldisk ", 7) == 0) {
    char* arg = input_buffer + 9;
    uint32_t size_mb = 0;
    
    for(int i = 0; arg[i] >= '0' && arg[i] <= '9'; i++) {
        size_mb = size_mb * 10 + (arg[i] - '0');
    }
    
    if(size_mb < 1) size_mb = 1;
    if(size_mb > 12000) size_mb = 12000;
    
    uint32_t total_sectors = size_mb * 2048;
    uint32_t total_bytes = (uint64_t)size_mb * 1024 * 1024;
    
    kprint_color("\nWARNING: This will write ", TXT_YELLOW);
    kprint_int(size_mb);
    kprint_color(" MB to disk!\n", TXT_YELLOW);
    kprint("This will take a long time on 3600 RPM\n");
    kprint("Press ESC to cancel, any other key to continue...\n");
    
    int cancelled = 0;
    uint32_t start_time = seconds;
    
    while(!(inb(0x64) & 1));
    uint8_t key = inb(0x60);
    if(key == 0x01) {
        kprint_color("\nCancelled\n", TXT_RED);
        return;
    }
    
    kprint("\nFilling disk with test data...\n");
    kprint("Size: ");
    kprint_int(size_mb);
    kprint(" MB (");
    kprint_int(total_bytes / 1024);
    kprint(" KB, ");
    kprint_int(total_sectors);
    kprint(" sectors)\n\n");
    
    uint16_t buf[256];
    int last_percent = -1;
    uint32_t last_update = 0;
    
    kprint("[");
    for(int i = 0; i < 40; i++) kprint(".");
    kprint("] 0% 0s ETA: ---s");
    
    for(uint32_t sector = 0; sector < total_sectors; sector++) {
        for(int i = 0; i < 256; i++) {
            buf[i] = (sector * 256 + i) & 0xFFFF;
        }
        write_sector(10000 + sector, buf);
        int percent = (sector * 100) / total_sectors;
        if(percent != last_percent && percent % 2 == 0) {
            last_percent = percent;
            
            uint32_t elapsed = seconds - start_time;
            uint32_t eta = 0;
            if(percent > 0) {
                eta = (elapsed * (100 - percent)) / percent;
            }
            kprint("\r[");
            int bars = (percent * 40) / 100;
            for(int i = 0; i < bars; i++) kprint("#");
            for(int i = bars; i < 40; i++) kprint(".");
            kprint("] ");
            kprint_int(percent);
            kprint("% ");
            kprint_int(elapsed);
            kprint("s ETA: ");
            kprint_int(eta);
            kprint("s  ");
        }
        
        if(sector % 1000 == 0 && sector > 0) {
            if(inb(0x64) & 1) {
                uint8_t sc = inb(0x60);
                if(sc == 0x01) {
                    cancelled = 1;
                    break;
                }
            }
        }
    }
    
    uint32_t total_time = seconds - start_time;
    
    if(cancelled) {
        kprint("\n\nCancelled at ");
        kprint_int(last_percent);
        kprint("%\n");
    } else {
        kprint("\n\nFill complete!\n");
    }
    
    kprint("Time: ");
    kprint_int(total_time);
    kprint(" seconds (");
    kprint_int(total_time / 60);
    kprint("m ");
    kprint_int(total_time % 60);
    kprint("s)\n");
    
    if(total_time > 0) {
        uint32_t speed_kb = (total_bytes / 1024) / total_time;
        kprint("Speed: ");
        kprint_int(speed_kb);
        kprint(" KB/s (");
        kprint_int(speed_kb / 1024);
        kprint(" MB/s)\n");
    }
    
    kprint("Sectors written: ");
    kprint_int(total_sectors);
    kprint("\n");
}

else if(my_strncmp(input_buffer, "filltest ", 9) == 0) {
    char* arg = input_buffer + 9;
    uint32_t size_mb = 0;
    
    for(int i = 0; arg[i] >= '0' && arg[i] <= '9'; i++) {
        size_mb = size_mb * 10 + (arg[i] - '0');
    }
    
    if(size_mb < 1) size_mb = 1;
    if(size_mb > 12000) size_mb = 12000;
    
    uint32_t total_sectors = size_mb * 2048;
    uint32_t errors = 0;
    
    kprint("\nVerifying filled data...\n");
    kprint("Size: ");
    kprint_int(size_mb);
    kprint(" MB (");
    kprint_int(total_sectors);
    kprint(" sectors)\n\n");
    
    kprint("[");
    for(int i = 0; i < 40; i++) kprint(".");
    kprint("] 0%");
    
    int last_percent = -1;
    uint32_t start_time = seconds;
    
    for(uint32_t sector = 0; sector < total_sectors; sector++) {
        uint16_t buf[256];
        read_sector(10000 + sector, buf);
        
        for(int i = 0; i < 256; i++) {
            uint16_t expected = (sector * 256 + i) & 0xFFFF;
            if(buf[i] != expected) {
                errors++;
                if(errors < 10) {
                    kprint("\nError at sector ");
                    kprint_int(sector);
                    kprint(" offset ");
                    kprint_int(i);
                    kprint(": expected 0x");
                    kprint_hex16(expected);
                    kprint(" got 0x");
                    kprint_hex16(buf[i]);
                    kprint("\n");
                }
            }
        }
        
        int percent = (sector * 100) / total_sectors;
        if(percent != last_percent && percent % 5 == 0) {
            last_percent = percent;
            kprint("\r[");
            int bars = (percent * 40) / 100;
            for(int i = 0; i < bars; i++) kprint("#");
            for(int i = bars; i < 40; i++) kprint(".");
            kprint("] ");
            kprint_int(percent);
            kprint("%");
        }
        
        if(sector % 1000 == 0 && sector > 0) {
            if(inb(0x64) & 1) {
                uint8_t sc = inb(0x60);
                if(sc == 0x01) {
                    kprint("\n\nCancelled\n");
                    return;
                }
            }
        }
    }
    
    uint32_t total_time = seconds - start_time;
    
    kprint("\r[########################################] 100%\n\n");
    
    if(errors == 0) {
        kprint_color("VERIFICATION PASSED! No errors found.\n", TXT_GREEN);
    } else {
        kprint_color("VERIFICATION FAILED! ", TXT_RED);
        kprint_int(errors);
        kprint(" errors found.\n");
    }
    
    kprint("Time: ");
    kprint_int(total_time);
    kprint(" seconds\n");
}

else if(my_strncmp(input_buffer, "man /c ", 7) == 0) {
    char* name = input_buffer + 7;
    trim(name);
    
    if(name[0] == '\0') {
        kprint("Usage: man /c <name>\n");
    } else {
        uint16_t man_buf[256];
        int man_sector = 7000;
        int found = 0;
        
        for(int i = 0; i < 100; i++) {
            read_sector(man_sector + i, man_buf);
            char existing_name[32] = {0};
            for(int j = 0; j < 31; j++) {
                existing_name[j] = ((char*)man_buf)[j];
            }
            if(existing_name[0] != 0 && my_strcmp(name, existing_name) == 0) {
                found = 1;
                break;
            }
            if(((char*)man_buf)[0] == 0) break;
        }
        
        if(found) {
            kprint("Man page already exists for: ");
            kprint(name);
            kprint("\n");
        } else {
            int free_sector = -1;
            for(int i = 0; i < 100; i++) {
                read_sector(man_sector + i, man_buf);
                if(((char*)man_buf)[0] == 0) {
                    free_sector = man_sector + i;
                    break;
                }
            }
            
            if(free_sector == -1) {
                kprint("No free space for man pages\n");
            } else {
                uint8_t man_data[512];
                for(int i = 0; i < 512; i++) man_data[i] = 0;
                
                for(int i = 0; name[i] && i < 31; i++) {
                    man_data[i] = name[i];
                }
                
                const char* header = "\nNAME\n     ";
                for(int i = 0; header[i]; i++) man_data[32 + i] = header[i];
                for(int i = 0; name[i]; i++) man_data[32 + 6 + i] = name[i];
                
                const char* desc = " - \n\nSYNOPSIS\n     \n\nDESCRIPTION\n     \n\nEXAMPLES\n     \n\n";
                for(int i = 0; desc[i]; i++) man_data[32 + 6 + 32 + i] = desc[i];
                
                uint16_t write_buf[256];
                for(int i = 0; i < 256; i++) {
                    write_buf[i] = man_data[i*2] | (man_data[i*2+1] << 8);
                }
                write_sector(free_sector, write_buf);
                
                kprint("Man page created for: ");
                kprint(name);
                kprint("\n");
                kprint("Use 'man /e ");
                kprint(name);
                kprint("' to edit\n");
            }
        }
    }
}

else if(my_strncmp(input_buffer, "man /r ", 7) == 0) {
    char* name = input_buffer + 7;
    trim(name);
    
    int man_sector = 7000;
    int found = 0;
    
    for(int i = 0; i < 100; i++) {
        uint16_t man_buf[256];
        read_sector(man_sector + i, man_buf);
        
        char existing_name[32] = {0};
        for(int j = 0; j < 31; j++) {
            existing_name[j] = ((char*)man_buf)[j];
        }
        
        if(existing_name[0] != 0 && my_strcmp(name, existing_name) == 0) {
            uint8_t man_data[512];
            for(int j = 0; j < 256; j++) {
                man_data[j*2] = man_buf[j] & 0xFF;
                man_data[j*2+1] = (man_buf[j] >> 8) & 0xFF;
            }
            
            kprint("\n");
            for(int j = 32; j < 512 && man_data[j] != 0; j++) {
                char c = (char)man_data[j];
                if(c >= 32 && c <= 126) {
                    char s[2] = {c, 0};
                    kprint(s);
                }
            }
            kprint("\n");
            found = 1;
            break;
        }
        if(((char*)man_buf)[0] == 0) break;
    }
    
    if(!found) {
        kprint("No man page found for: ");
        kprint(name);
        kprint("\n");
    }
}

else if(my_strncmp(input_buffer, "man /d ", 7) == 0) {
    char* name = input_buffer + 7;
    trim(name);
    
    int man_sector = 7000;
    int found = 0;
    
    for(int i = 0; i < 100; i++) {
        uint16_t man_buf[256];
        read_sector(man_sector + i, man_buf);
        
        char existing_name[32] = {0};
        for(int j = 0; j < 31; j++) {
            existing_name[j] = ((char*)man_buf)[j];
        }
        
        if(existing_name[0] != 0 && my_strcmp(name, existing_name) == 0) {
            uint16_t empty_buf[256];
            for(int j = 0; j < 256; j++) empty_buf[j] = 0;
            write_sector(man_sector + i, empty_buf);
            kprint("Man page deleted: ");
            kprint(name);
            kprint("\n");
            found = 1;
            break;
        }
        if(((char*)man_buf)[0] == 0) break;
    }
    
    if(!found) {
        kprint("No man page found for: ");
        kprint(name);
        kprint("\n");
    }
}

else if(my_strncmp(input_buffer, "man /e ", 7) == 0) {
    char* name = input_buffer + 7;
    trim(name);
    
    int man_sector = 7000;
    int target_sector = -1;
    
    for(int i = 0; i < 100; i++) {
        uint16_t man_buf[256];
        read_sector(man_sector + i, man_buf);
        
        char existing_name[32] = {0};
        for(int j = 0; j < 31; j++) {
            existing_name[j] = ((char*)man_buf)[j];
        }
        
        if(existing_name[0] != 0 && my_strcmp(name, existing_name) == 0) {
            target_sector = man_sector + i;
            break;
        }
        if(((char*)man_buf)[0] == 0 && target_sector == -1) {
            target_sector = man_sector + i;
        }
    }
    
    if(target_sector == -1) {
        kprint("No free space for man page\n");
    } else {
        uint16_t man_buf[256];
        read_sector(target_sector, man_buf);
        
        char original_text[512] = {0};
        char man_text[512] = {0};
        for(int i = 0; i < 256; i++) {
            man_text[i*2] = man_buf[i] & 0xFF;
            man_text[i*2+1] = (man_buf[i] >> 8) & 0xFF;
            original_text[i*2] = man_buf[i] & 0xFF;
            original_text[i*2+1] = (man_buf[i] >> 8) & 0xFF;
        }
        
        if(man_text[0] == 0) {
            for(int i = 0; name[i] && i < 31; i++) man_text[i] = name[i];
            const char* templ = "\nNAME\n     ";
            for(int i = 0; templ[i]; i++) man_text[32 + i] = templ[i];
            for(int i = 0; name[i]; i++) man_text[32 + 6 + i] = name[i];
            const char* desc = " - \n\nSYNOPSIS\n     \n\nDESCRIPTION\n     \n\nEXAMPLES\n     \n\n";
            for(int i = 0; desc[i]; i++) man_text[32 + 6 + 32 + i] = desc[i];
        }
        
        int modified = 0;
        int cursor_pos = 0;
        int scroll = 0;
        int running = 1;
        int need_redraw = 1;
        int old_cursor_pos = -1;
        int old_scroll = -1;
        
        clear_screen();
        
        while(running) {
            if(need_redraw || scroll != old_scroll) {
                clear_screen_bg(BLACK);
                
                draw_frame(1, 0, 78, 2, BLUE, TXT_WHITE);
                kprint_at("MAN PAGE EDITOR - ", 3, 1, (BLUE << 4) | TXT_YELLOW);
                kprint_at(name, 21, 1, (BLUE << 4) | TXT_WHITE);
                kprint_at("[F1] Save  [ESC] Exit", 55, 1, (BLUE << 4) | TXT_GREEN);
                
                draw_frame(1, 3, 78, 20, GRAY, TXT_WHITE);
                
                int start_pos = scroll;
                int end_pos = scroll + 380;
                if(end_pos > 400) end_pos = 400;
                
                int line = 0;
                int col = 0;
                
                for(int i = start_pos; i < end_pos && man_text[32 + i] != 0; i++) {
                    char c = man_text[32 + i];
                    if(c == '\n') {
                        line++;
                        col = 0;
                    } else if(col < 76 && line < 19) {
                        char s[2] = {c, 0};
                        kprint_at(s, 3 + col, 4 + line, (GRAY << 4) | TXT_WHITE);
                        col++;
                    }
                }
                
                draw_frame(1, 24, 78, 1, BLUE, TXT_WHITE);
                kprint_at("Pos: ", 3, 24, (BLUE << 4) | TXT_CYAN);
                kprint_int_at(cursor_pos, 9, 24, (BLUE << 4) | TXT_WHITE);
                if(modified) {
                    kprint_at(" [MODIFIED]", 20, 24, (BLUE << 4) | TXT_RED);
                }
                
                need_redraw = 0;
                old_scroll = scroll;
            }
            
            if(old_cursor_pos != cursor_pos) {
                old_cursor_pos = cursor_pos;
            }
            
            int cursor_line = 0;
            int cursor_col = 0;
            for(int i = 0; i < cursor_pos && man_text[32 + i] != 0; i++) {
                if(man_text[32 + i] == '\n') {
                    cursor_line++;
                    cursor_col = 0;
                } else {
                    cursor_col++;
                }
            }
            if(cursor_line >= 0 && cursor_line < 19 && cursor_col >= 0 && cursor_col < 76) {
                move_cursor(3 + cursor_col, 4 + cursor_line);
            }
            
            if(inb(0x64) & 1) {
                uint8_t sc = inb(0x60);
                
                if(sc == 0x01) {
                    if(modified) {
                        int changed = 0;
                        for(int i = 0; i < 512; i++) {
                            if(man_text[i] != original_text[i]) {
                                changed = 1;
                                break;
                            }
                        }
                        if(changed) {
                            kprint("\n\nSave changes? (Y/N): ");
                            char save = 0;
                            while(save != 'y' && save != 'Y' && save != 'n' && save != 'N') {
                                if(inb(0x64) & 1) {
                                    uint8_t k = inb(0x60);
                                    if(k == 0x15 || k == 0x2C) save = 'y';
                                    if(k == 0x31 || k == 0x35) save = 'n';
                                }
                            }
                            if(save == 'y' || save == 'Y') {
                                uint8_t save_data[512];
                                for(int i = 0; i < 512; i++) save_data[i] = 0;
                                for(int i = 0; name[i] && i < 31; i++) save_data[i] = name[i];
                                for(int i = 0; i < 400; i++) {
                                    save_data[32 + i] = man_text[32 + i];
                                }
                                uint16_t write_buf[256];
                                for(int i = 0; i < 256; i++) {
                                    write_buf[i] = save_data[i*2] | (save_data[i*2+1] << 8);
                                }
                                write_sector(target_sector, write_buf);
                                kprint("\nSaved!\n");
                                for(volatile int d = 0; d < 1000000; d++);
                            }
                        }
                    }
                    running = 0;
                }
                else if(sc == 0x3B) {
                    uint8_t save_data[512];
                    for(int i = 0; i < 512; i++) save_data[i] = 0;
                    for(int i = 0; name[i] && i < 31; i++) save_data[i] = name[i];
                    for(int i = 0; i < 400; i++) {
                        save_data[32 + i] = man_text[32 + i];
                    }
                    uint16_t write_buf[256];
                    for(int i = 0; i < 256; i++) {
                        write_buf[i] = save_data[i*2] | (save_data[i*2+1] << 8);
                    }
                    write_sector(target_sector, write_buf);
                    modified = 0;
                    for(int i = 0; i < 512; i++) original_text[i] = man_text[i];
                    need_redraw = 1;
                }
                else if(sc == 0x48 && cursor_pos > 0) {
                    cursor_pos--;
                    if(cursor_pos < scroll) scroll = cursor_pos;
                    need_redraw = 1;
                }
                else if(sc == 0x50 && cursor_pos < 399) {
                    cursor_pos++;
                    if(cursor_pos >= scroll + 380) scroll = cursor_pos - 379;
                    need_redraw = 1;
                }
                else if(sc == 0x4B && cursor_pos > 0) {
                    cursor_pos--;
                    need_redraw = 1;
                }
                else if(sc == 0x4D) {
                    if(cursor_pos < 399 && man_text[32 + cursor_pos] != 0) {
                        cursor_pos++;
                        need_redraw = 1;
                    }
                }
                else if(sc == 0x0E && cursor_pos > 0) {
                    for(int i = cursor_pos - 1; i < 399; i++) {
                        man_text[32 + i] = man_text[32 + i + 1];
                    }
                    cursor_pos--;
                    modified = 1;
                    need_redraw = 1;
                }
                else if(sc == 0x1C) {
                    for(int i = 399; i > cursor_pos; i--) {
                        man_text[32 + i] = man_text[32 + i - 1];
                    }
                    man_text[32 + cursor_pos] = '\n';
                    cursor_pos++;
                    modified = 1;
                    need_redraw = 1;
                }
                else {
                    char ch = 0;
                    if(sc >= 0x02 && sc <= 0x0B) ch = "1234567890"[sc - 0x02];
                    else if(sc >= 0x10 && sc <= 0x19) ch = "qwertyuiop"[sc - 0x10];
                    else if(sc >= 0x1E && sc <= 0x26) ch = "asdfghjkl"[sc - 0x1E];
                    else if(sc >= 0x2C && sc <= 0x32) ch = "zxcvbnm"[sc - 0x2C];
                    else if(sc == 0x39) ch = ' ';
                    else if(sc == 0x0C) ch = '-';
                    else if(sc == 0x34) ch = '.';
                    
                    if(ch && cursor_pos < 399) {
                        for(int i = 399; i > cursor_pos; i--) {
                            man_text[32 + i] = man_text[32 + i - 1];
                        }
                        man_text[32 + cursor_pos] = ch;
                        cursor_pos++;
                        modified = 1;
                        need_redraw = 1;
                    }
                }
            }
        }
        clear_screen();
    }
}

else if(my_strncmp(input_buffer, "cat ", 4) == 0) {
    char* name = input_buffer + 4;
    trim(name);
    
    int is_man = 0;
    if(my_strncmp(name, "/m ", 3) == 0) {
        is_man = 1;
        name += 3;
        while(*name == ' ') name++;
    }
    
    if(is_man) {
        int man_sector = 7000;
        int found = 0;
        
        for(int i = 0; i < 100; i++) {
            uint16_t man_buf[256];
            read_sector(man_sector + i, man_buf);
            
            char existing_name[32] = {0};
            for(int j = 0; j < 31; j++) {
                existing_name[j] = ((char*)man_buf)[j];
            }
            
            if(existing_name[0] != 0 && my_strcmp(name, existing_name) == 0) {
                uint8_t man_data[512];
                for(int j = 0; j < 256; j++) {
                    man_data[j*2] = man_buf[j] & 0xFF;
                    man_data[j*2+1] = (man_buf[j] >> 8) & 0xFF;
                }
                
                kprint("\n");
                for(int j = 32; j < 512 && man_data[j] != 0; j++) {
                    char c = (char)man_data[j];
                    if(c >= 32 && c <= 126) {
                        char s[2] = {c, 0};
                        kprint(s);
                    } else if(c == '\n') {
                        kprint("\n");
                    }
                }
                kprint("\n");
                found = 1;
                break;
            }
            if(((char*)man_buf)[0] == 0) break;
        }
        
        if(!found) {
            kprint("No man page found for: ");
            kprint(name);
            kprint("\n");
        }
    } else {
        uint16_t dir_buf[256];
        read_sector(current_dir_sector, dir_buf);
        
        int slot = -1;
        for(int i = 0; i < 32; i++) {
            char filename[12] = {0};
            for(int j = 0; j < 11; j++) filename[j] = ((char*)dir_buf)[i*16 + j];
            if(my_strcmp(name, filename) == 0) {
                slot = i;
                break;
            }
        }
        
        if(slot == -1) {
            kprint("File not found: ");
            kprint(name);
            kprint("\n");
            kprint("Try 'cat /m ");
            kprint(name);
            kprint("' for man page\n");
        } else {
            int sector = dir_buf[slot*8 + 6];
            int size = dir_buf[slot*8 + 7];
            uint16_t data_buf[256];
            read_sector(sector, data_buf);
            
            kprint("\n");
            for(int i = 0; i < size; i++) {
                char c;
                if(i % 2 == 0) c = data_buf[i/2] & 0xFF;
                else c = (data_buf[i/2] >> 8) & 0xFF;
                if(c >= 32 && c <= 126) {
                    char s[2] = {c, 0};
                    kprint(s);
                }
            }
            kprint("\n");
        }
    }
}

else if(my_strcmp(input_buffer, "wnkasxs") == 0) {
    kprint_color("\n========================================\n", TXT_CYAN);
    kprint_color("     WNKA SXS - SYSTEM BACKUP\n", TXT_CYAN);
    kprint_color("========================================\n\n", TXT_CYAN);
    
    uint16_t dir_buf[256];
    read_sector(100, dir_buf);
    
    int sxs_slot = -1;
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
        if(my_strcmp("WnkaSXS", name) == 0) {
            sxs_slot = i;
            break;
        }
    }
    
    if(sxs_slot == -1) {
        kprint("Creating WnkaSXS directory...\n");
        
        for(int i = 0; i < 32; i++) {
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
            if(name[0] == 0) {
                sxs_slot = i;
                for(int j = 0; j < 7; j++) ((char*)dir_buf)[i*16 + j] = "WnkaSXS"[j];
                ((char*)dir_buf)[i*16 + 11] = 1;
                dir_buf[i*8 + 6] = 300 + i;
                write_sector(100, dir_buf);
                
                uint16_t folder_buf[256];
                for(int j = 0; j < 256; j++) folder_buf[j] = 0;
                write_sector(300 + i, folder_buf);
                break;
            }
        }
        kprint_color("✓ Created\n\n", TXT_GREEN);
    }
    
    if(sxs_slot == -1) {
        kprint_color("ERROR: Cannot create WnkaSXS directory!\n", TXT_RED);
    } else {
        int sxs_sector = dir_buf[sxs_slot*8 + 6];
        
        kprint("Backing up files...\n\n");
        
        uint16_t root_buf[256];
        read_sector(100, root_buf);
        
        int copied = 0;
        
        for(int i = 0; i < 32; i++) {
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = ((char*)root_buf)[i*16 + j];
            
            if(name[0] != 0 && my_strcmp(name, "WnkaSXS") != 0) {
                int is_dir = ((char*)root_buf)[i*16 + 11] == 1;
                int src_sector = root_buf[i*8 + 6];
                int size = root_buf[i*8 + 7];
                
                if(is_dir) {
                    kprint("[DIR]  ");
                    kprint(name);
                    kprint("\n");
                    
                    uint16_t sxs_buf[256];
                    read_sector(sxs_sector, sxs_buf);
                    
                    int new_slot = -1;
                    for(int j = 0; j < 32; j++) {
                        char n[12] = {0};
                        for(int k = 0; k < 11; k++) n[k] = ((char*)sxs_buf)[j*16 + k];
                        if(n[0] == 0) {
                            new_slot = j;
                            break;
                        }
                    }
                    
                    if(new_slot != -1) {
                        for(int j = 0; j < 11 && name[j]; j++) {
                            ((char*)sxs_buf)[new_slot*16 + j] = name[j];
                        }
                        ((char*)sxs_buf)[new_slot*16 + 11] = 1;
                        int new_folder_sector = 400 + new_slot;
                        sxs_buf[new_slot*8 + 6] = new_folder_sector;
                        sxs_buf[new_slot*8 + 7] = 0;
                        write_sector(sxs_sector, sxs_buf);
                        
                        uint16_t empty_buf[256];
                        for(int j = 0; j < 256; j++) empty_buf[j] = 0;
                        write_sector(new_folder_sector, empty_buf);
                        
                        uint16_t src_folder_buf[256];
                        read_sector(src_sector, src_folder_buf);
                        
                        uint16_t dst_folder_buf[256];
                        read_sector(new_folder_sector, dst_folder_buf);
                        
                        for(int j = 0; j < 32; j++) {
                            char fname[12] = {0};
                            for(int k = 0; k < 11; k++) fname[k] = ((char*)src_folder_buf)[j*16 + k];
                            
                            if(fname[0] != 0) {
                                int f_is_dir = ((char*)src_folder_buf)[j*16 + 11] == 1;
                                int f_src_sector = src_folder_buf[j*8 + 6];
                                int f_size = src_folder_buf[j*8 + 7];
                                
                                int dst_slot = -1;
                                for(int k = 0; k < 32; k++) {
                                    char n[12] = {0};
                                    for(int m = 0; m < 11; m++) n[m] = ((char*)dst_folder_buf)[k*16 + m];
                                    if(n[0] == 0) {
                                        dst_slot = k;
                                        break;
                                    }
                                }
                                
                                if(dst_slot != -1) {
                                    if(f_is_dir) {
                                        for(int k = 0; k < 11 && fname[k]; k++) {
                                            ((char*)dst_folder_buf)[dst_slot*16 + k] = fname[k];
                                        }
                                        ((char*)dst_folder_buf)[dst_slot*16 + 11] = 1;
                                        int subfolder_sector = 500 + dst_slot;
                                        dst_folder_buf[dst_slot*8 + 6] = subfolder_sector;
                                        write_sector(new_folder_sector, dst_folder_buf);
                                        
                                        uint16_t empty2[256];
                                        for(int k = 0; k < 256; k++) empty2[k] = 0;
                                        write_sector(subfolder_sector, empty2);
                                    } else {
                                        uint16_t file_data[256];
                                        read_sector(f_src_sector, file_data);
                                        
                                        int dest_sector = 600 + dst_slot;
                                        write_sector(dest_sector, file_data);
                                        
                                        for(int k = 0; k < 11 && fname[k]; k++) {
                                            ((char*)dst_folder_buf)[dst_slot*16 + k] = fname[k];
                                        }
                                        ((char*)dst_folder_buf)[dst_slot*16 + 11] = 0;
                                        dst_folder_buf[dst_slot*8 + 6] = dest_sector;
                                        dst_folder_buf[dst_slot*8 + 7] = f_size;
                                        write_sector(new_folder_sector, dst_folder_buf);
                                        
                                        kprint("    [FILE] ");
                                        kprint(fname);
                                        kprint(" (");
                                        kprint_int(f_size);
                                        kprint(" bytes)\n");
                                        copied++;
                                    }
                                }
                            }
                        }
                    }
                } else {
                    kprint("[FILE] ");
                    kprint(name);
                    kprint(" (");
                    kprint_int(size);
                    kprint(" bytes)\n");
                    
                    uint16_t sxs_buf[256];
                    read_sector(sxs_sector, sxs_buf);
                    
                    int new_slot = -1;
                    for(int j = 0; j < 32; j++) {
                        char n[12] = {0};
                        for(int k = 0; k < 11; k++) n[k] = ((char*)sxs_buf)[j*16 + k];
                        if(n[0] == 0) {
                            new_slot = j;
                            break;
                        }
                    }
                    
                    if(new_slot != -1) {
                        uint16_t file_data[256];
                        read_sector(src_sector, file_data);
                        
                        int dest_sector = 700 + new_slot;
                        write_sector(dest_sector, file_data);
                        
                        for(int j = 0; j < 11 && name[j]; j++) {
                            ((char*)sxs_buf)[new_slot*16 + j] = name[j];
                        }
                        ((char*)sxs_buf)[new_slot*16 + 11] = 0;
                        sxs_buf[new_slot*8 + 6] = dest_sector;
                        sxs_buf[new_slot*8 + 7] = size;
                        write_sector(sxs_sector, sxs_buf);
                        copied++;
                    }
                }
            }
        }
        
        kprint_color("\n========================================\n", TXT_GREEN);
        kprint_color("     BACKUP COMPLETE!\n", TXT_GREEN);
        kprint_color("========================================\n", TXT_GREEN);
        kprint("Files copied: ");
        kprint_int(copied);
        kprint("\nLocation: /WnkaSXS\n");
    }
}

else if(my_strcmp(input_buffer, "wnkasxs_restore") == 0) {
    kprint_color("\n========================================\n", TXT_YELLOW);
    kprint_color("     WNKA SXS - SYSTEM RESTORE\n", TXT_YELLOW);
    kprint_color("========================================\n\n", TXT_YELLOW);
    
    kprint_color("WARNING: This will RESTORE all files from backup!\n", TXT_RED);
    kprint("Type 'kkk' to confirm: ");
    
    char confirm[4];
    int pos = 0;
    while(pos < 3) {
        if(inb(0x64) & 1) {
            uint8_t key = inb(0x60);
            if(key == 0x1C) break;
            if(key >= 0x04 && key <= 0x1D) {
                confirm[pos++] = key - 0x04 + 'a';
                kprint_char(confirm[pos-1]);
            }
        }
    }
    confirm[pos] = '\0';
    
        kprint("\nRestoring files...\n\n");
        uint16_t root_buf[256];
        read_sector(100, root_buf);
        
        int sxs_slot = -1;
        for(int i = 0; i < 32; i++) {
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = ((char*)root_buf)[i*16 + j];
            if(my_strcmp("WnkaSXS", name) == 0) {
                sxs_slot = i;
                break;
            }
        }
        
        if(sxs_slot == -1) {
            kprint_color("ERROR: No backup found! Run 'wnkasxs' first.\n", TXT_RED);
        } else {
            int sxs_sector = root_buf[sxs_slot*8 + 6];
            uint16_t sxs_buf[256];
            read_sector(sxs_sector, sxs_buf);
            
            int restored = 0;
            
            for(int i = 0; i < 32; i++) {
                char name[12] = {0};
                for(int j = 0; j < 11; j++) name[j] = ((char*)sxs_buf)[i*16 + j];
                
                if(name[0] != 0) {
                    int is_dir = ((char*)sxs_buf)[i*16 + 11] == 1;
                    int src_sector = sxs_buf[i*8 + 6];
                    int size = sxs_buf[i*8 + 7];
                    
                    if(is_dir) {
                        kprint("[DIR]  ");
                        kprint(name);
                        kprint("\n");
                        
                        int exists = -1;
                        for(int j = 0; j < 32; j++) {
                            char n[12] = {0};
                            for(int k = 0; k < 11; k++) n[k] = ((char*)root_buf)[j*16 + k];
                            if(my_strcmp(name, n) == 0) {
                                exists = j;
                                break;
                            }
                        }
                        
                        if(exists == -1) {
                            for(int j = 0; j < 32; j++) {
                                char n[12] = {0};
                                for(int k = 0; k < 11; k++) n[k] = ((char*)root_buf)[j*16 + k];
                                if(n[0] == 0) {
                                    exists = j;
                                    break;
                                }
                            }
                        }
                        
                        if(exists != -1) {
                            for(int j = 0; j < 11 && name[j]; j++) {
                                ((char*)root_buf)[exists*16 + j] = name[j];
                            }
                            ((char*)root_buf)[exists*16 + 11] = 1;
                            
                            int new_folder_sector = 300 + exists;
                            root_buf[exists*8 + 6] = new_folder_sector;
                            root_buf[exists*8 + 7] = 0;
                            write_sector(100, root_buf);
                            
                            uint16_t empty_buf[256];
                            for(int j = 0; j < 256; j++) empty_buf[j] = 0;
                            write_sector(new_folder_sector, empty_buf);
                            
                            uint16_t src_folder_buf[256];
                            read_sector(src_sector, src_folder_buf);
                            
                            uint16_t dst_folder_buf[256];
                            read_sector(new_folder_sector, dst_folder_buf);
                            
                            for(int j = 0; j < 32; j++) {
                                char fname[12] = {0};
                                for(int k = 0; k < 11; k++) fname[k] = ((char*)src_folder_buf)[j*16 + k];
                                
                                if(fname[0] != 0) {
                                    int f_is_dir = ((char*)src_folder_buf)[j*16 + 11] == 1;
                                    int f_src_sector = src_folder_buf[j*8 + 6];
                                    int f_size = src_folder_buf[j*8 + 7];
                                    
                                    int dst_slot = -1;
                                    for(int k = 0; k < 32; k++) {
                                        char n[12] = {0};
                                        for(int m = 0; m < 11; m++) n[m] = ((char*)dst_folder_buf)[k*16 + m];
                                        if(n[0] == 0) {
                                            dst_slot = k;
                                            break;
                                        }
                                    }
                                    
                                    if(dst_slot != -1) {
                                        if(f_is_dir) {
                                            for(int k = 0; k < 11 && fname[k]; k++) {
                                                ((char*)dst_folder_buf)[dst_slot*16 + k] = fname[k];
                                            }
                                            ((char*)dst_folder_buf)[dst_slot*16 + 11] = 1;
                                            int subfolder_sector = 500 + dst_slot;
                                            dst_folder_buf[dst_slot*8 + 6] = subfolder_sector;
                                            write_sector(new_folder_sector, dst_folder_buf);
                                            
                                            uint16_t empty2[256];
                                            for(int k = 0; k < 256; k++) empty2[k] = 0;
                                            write_sector(subfolder_sector, empty2);
                                        } else {
                                            uint16_t file_data[256];
                                            read_sector(f_src_sector, file_data);
                                            
                                            int dest_sector = 600 + dst_slot;
                                            write_sector(dest_sector, file_data);
                                            
                                            for(int k = 0; k < 11 && fname[k]; k++) {
                                                ((char*)dst_folder_buf)[dst_slot*16 + k] = fname[k];
                                            }
                                            ((char*)dst_folder_buf)[dst_slot*16 + 11] = 0;
                                            dst_folder_buf[dst_slot*8 + 6] = dest_sector;
                                            dst_folder_buf[dst_slot*8 + 7] = f_size;
                                            write_sector(new_folder_sector, dst_folder_buf);
                                            
                                            kprint("    [FILE] ");
                                            kprint(fname);
                                            kprint(" (");
                                            kprint_int(f_size);
                                            kprint(" bytes)\n");
                                            restored++;
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        kprint("[FILE] ");
                        kprint(name);
                        kprint(" (");
                        kprint_int(size);
                        kprint(" bytes)\n");
                        
                        int exists = -1;
                        for(int j = 0; j < 32; j++) {
                            char n[12] = {0};
                            for(int k = 0; k < 11; k++) n[k] = ((char*)root_buf)[j*16 + k];
                            if(my_strcmp(name, n) == 0) {
                                exists = j;
                                break;
                            }
                        }
                        
                        if(exists == -1) {
                            for(int j = 0; j < 32; j++) {
                                char n[12] = {0};
                                for(int k = 0; k < 11; k++) n[k] = ((char*)root_buf)[j*16 + k];
                                if(n[0] == 0) {
                                    exists = j;
                                    break;
                                }
                            }
                        }
                        
                        if(exists != -1) {
                            uint16_t file_data[256];
                            read_sector(src_sector, file_data);
                            
                            int dest_sector = 200 + exists;
                            write_sector(dest_sector, file_data);
                            
                            for(int j = 0; j < 11 && name[j]; j++) {
                                ((char*)root_buf)[exists*16 + j] = name[j];
                            }
                            ((char*)root_buf)[exists*16 + 11] = 0;
                            root_buf[exists*8 + 6] = dest_sector;
                            root_buf[exists*8 + 7] = size;
                            write_sector(100, root_buf);
                            restored++;
                        }
                    }
                }
            }
            
            kprint_color("\n========================================\n", TXT_GREEN);
            kprint_color("     RESTORE COMPLETE!\n", TXT_GREEN);
            kprint_color("========================================\n", TXT_GREEN);
            kprint("Files restored: ");
            kprint_int(restored);
            kprint("\n");
        }
}

else if(my_strcmp(cmd_copy, "mode") == 0) {
    cmd_mode(arg);
    kprint_color("root@wnka> ", TXT_GREEN);
}
else if(my_strcmp(cmd_copy, "modes") == 0) {
    kprint("Available modes:\n");
    for(int i = 0; text_modes[i].name; i++) {
        kprint("  ");
        kprint(text_modes[i].name);
        kprint("\n");
    }
    kprint_color("root@wnka> ", TXT_GREEN);
}
else if(my_strcmp(cmd_copy, "modeinfo") == 0) {
    mode_info();
    kprint_color("root@wnka> ", TXT_GREEN);
}
else if(my_strcmp(cmd_copy, "modeauto") == 0) {
    mode_auto();
    kprint_color("root@wnka> ", TXT_GREEN);
}
else if(my_strcmp(cmd_copy, "modenext") == 0) {
    mode_next();
    kprint_color("root@wnka> ", TXT_GREEN);
}
else if(my_strcmp(cmd_copy, "modeprev") == 0) {
    mode_prev();
    kprint_color("root@wnka> ", TXT_GREEN);
}
else if(my_strcmp(cmd_copy, "80x25") == 0) { set_80x25(); clear_screen(); kprint_color("root@wnka> ", TXT_GREEN); }
else if(my_strcmp(cmd_copy, "80x50") == 0) { set_80x50(); clear_screen(); kprint_color("root@wnka> ", TXT_GREEN); }
else if(my_strcmp(cmd_copy, "132x43") == 0) { set_132x43(); clear_screen(); kprint_color("root@wnka> ", TXT_GREEN); }
else if(my_strcmp(cmd_copy, "132x60") == 0) { set_132x60(); clear_screen(); kprint_color("root@wnka> ", TXT_GREEN); }
else if(my_strcmp(cmd_copy, "uidev") == 0) {
    ui_designer(arg);
    kprint_color("root@wnka> ", TXT_GREEN);
}
else if(my_strcmp(cmd_copy, "ui") == 0) {
    wnkcui_run();
}
else if(my_strcmp(cmd_copy, "net") == 0) {
    if(arg[0] == '\0') {
        kprint_color("Network commands:\n", TXT_CYAN);
        kprint("  net init   - initialize E1000\n");
        kprint("  net status - show link status\n");
        kprint("  net stats  - show statistics\n");
        kprint("  net test   - test send/recv\n");
    }
    else if(my_strcmp(arg, "init") == 0) {
        e1000_init();
    }
    else if(my_strcmp(arg, "status") == 0) {
        if(e1000_link_up()) {
            kprint_color("Link: UP\n", TXT_GREEN);
        } else {
            kprint_color("Link: DOWN\n", TXT_RED);
        }
    }
    else if(my_strcmp(arg, "stats") == 0) {
        e1000_dump_stats();
    }
    else if(my_strcmp(arg, "test") == 0) {
        kprint("[TEST] Sending test packet...\n");
        uint8_t test_packet[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
                                  0x52,0x54,0x00,0x12,0x34,0x56,
                                  0x08,0x00};
        e1000_send(NULL, test_packet, sizeof(test_packet));
        kprint("[TEST] Done!\n");
    }
}
else if(my_strcmp(cmd_copy, "timer") == 0) {
    int seconds = 10;
    
    if(arg[0] != '\0') {
        seconds = 0;
        for(char* p = arg; *p >= '0' && *p <= '9'; p++) {
            seconds = seconds * 10 + (*p - '0');
        }
        if(seconds < 1) seconds = 1;
        if(seconds > 3600) seconds = 3600;
    }
    
    kprint("Timer set for ");
    kprint_int(seconds);
    kprint(" seconds\n");
    kprint("Press any key to stop\n\n");
    
    for(int i = seconds; i > 0; i--) {
        kprint("\r  ");
        kprint_int(i);
        kprint(" sec remaining... ");

        
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc < 0x80) {
                kprint("\nTimer cancelled!\n");
                return;
            }
        }
        
        for(volatile int d = 0; d < 100000000; d++);
        clear_screen();
    }
    
    kprint("\rTIME'S UP!          \n");
    
    kprint("Press any key to stop alarm...\n");
    
    while(1) {
        play_sound(80);
        for(volatile int d = 0; d < 100000000; d++);
        nosound();
        for(volatile int d = 0; d < 100000000; d++);
        
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc < 0x80) {
                break;
            }
        }
    }
    
    nosound();
    kprint("Alarm stopped.\n");
}
else if(my_strcmp(cmd_copy, "sleep") == 0) {
    int seconds_to_sleep = 1;
    
    if(arg[0] != '\0') {
        seconds_to_sleep = 0;
        for(char* p = arg; *p >= '0' && *p <= '9'; p++) {
            seconds_to_sleep = seconds_to_sleep * 10 + (*p - '0');
        }
        if(seconds_to_sleep < 1) seconds_to_sleep = 1;
        if(seconds_to_sleep > 3600) seconds_to_sleep = 3600;
    }
    
    kprint("Sleeping for ");
    kprint_int(seconds_to_sleep);
    kprint(" second");
    if(seconds_to_sleep != 1) kprint("s");
    kprint("...\n");
    for(int i = seconds_to_sleep; i > 0; i--) {
        kprint("\r  ");
        kprint_int(i);
        kprint(" sec remaining... ");
        for(volatile int d = 0; d < 100000000; d++);
        clear_screen();
    }
    kprint("\rDone!                     \n");
}
else if(my_strcmp(cmd_copy, "crashme") == 0) {
    crashme();
}
else if(my_strcmp(cmd_copy, "halt") == 0) {
    halt_system();
}
else if(my_strcmp(cmd_copy, "bg") == 0) {
    if(arg[0] == '\0') {
        kprint("Usage: bg <command>\n");
        kprint("Examples:\n");
        kprint("  bg cowsay Hello\n");
        kprint("  bg matrix\n");
        kprint("  bg flappy\n");
    } else {
        run_background_command(arg, arg);
    }
}
else if(my_strcmp(cmd_copy, "jobs") == 0) {
    list_background();
}
else if(my_strcmp(cmd_copy, "fg") == 0) {
    int id = atoi(arg);
    foreground_command(id);
}
else if(my_strcmp(cmd_copy, "bgkill") == 0) {
    int id = atoi(arg);
    kill_background(id);
}
else if(my_strcmp(cmd_copy, "multitest") == 0) {
    test_multitask();
}
else if(my_strcmp(cmd_copy, "ps") == 0) {
    task_list();
}
else if(my_strcmp(cmd_copy, "kill") == 0) {
    int pid = atoi(arg);
    task_kill(pid);
}
else if(my_strcmp(cmd_copy, "paint") == 0) {
    paint_main();
    kprint_color("root@wnka> ", TXT_GREEN);
}
else if(my_strcmp(cmd_copy, "piano") == 0) {
    piano_main();
    kprint_color("root@wnka> ", TXT_GREEN);
}
else if(my_strcmp(cmd_copy, "coin") == 0) {
    int flip = my_rand() % 2;
    if(flip == 0) kprint("Heads\n");
    else kprint("Tails\n");
}
else if (my_strcmp(input_buffer, "wnkadebug") == 0) {
    kprint("start debug mode...");
    clear_screen();
    kprint("debug mode start");
    kmaindb();
}
else if(my_strcmp(cmd_copy, "dice") == 0) {
    int sides = 6;
    if(arg[0] != '\0') {
        sides = 0;
        for(int i = 0; arg[i] && arg[i] >= '0' && arg[i] <= '9'; i++) {
            sides = sides * 10 + (arg[i] - '0');
        }
        if(sides < 2) sides = 6;
        if(sides > 100) sides = 100;
    }
    int roll = (my_rand() % sides) + 1;
    kprint("You rolled: ");
    kprint_int(roll);
    kprint(" (d");
    kprint_int(sides);
    kprint(")\n");
}
else if(my_strcmp(cmd_copy, "echo") == 0) {
    kprint(arg);
    kprint("\n");
}
else if (my_strcmp(cmd_copy, "dostest") == 0) {
    dos_test();
}
else if(my_strcmp(cmd_copy, "ide") == 0) {
   ide();
}
else if(my_strcmp(cmd_copy, "tcc") == 0) {
    if(arg[0] == '\0') {
        kprint("TinyCC - C compiler\n");
        kprint("Usage:\n");
        kprint("  tcc <code>               - compile string\n");
        kprint("  tcc <filename>           - compile file\n");
        kprint("Examples:\n");
        kprint("  tcc 'int main() { return 42; }'\n");
        kprint("  tcc test.c\n");
    } else {
        char* code = NULL;
        int is_file = 0;
        char filename[32] = {0};
        
        my_strcpy(filename, arg);
        
        uint16_t dir_buf[256];
        read_sector(current_dir_sector, dir_buf);
        
        for(int i = 0; i < 32; i++) {
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
            if(my_strcmp(arg, name) == 0) {
                is_file = 1;
                break;
            }
        }
        
        int becap_slot = -1;
        for(int i = 0; i < 32; i++) {
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
            if(my_strcmp("becap", name) == 0) {
                becap_slot = i;
                break;
            }
        }
        
        if(becap_slot == -1) {
            for(int i = 0; i < 32; i++) {
                char name[12] = {0};
                for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
                if(name[0] == 0) {
                    becap_slot = i;
                    for(int j = 0; j < 5; j++) ((char*)dir_buf)[i*16 + j] = "becap"[j];
                    ((char*)dir_buf)[i*16 + 11] = 1;
                    dir_buf[i*8 + 6] = 300 + i;
                    write_sector(current_dir_sector, dir_buf);
                    
                    uint16_t folder_buf[256];
                    for(int j = 0; j < 256; j++) folder_buf[j] = 0;
                    write_sector(300 + i, folder_buf);
                    break;
                }
            }
        }
        
        if(is_file) {
            int slot = -1;
            for(int i = 0; i < 32; i++) {
                char name[12] = {0};
                for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
                if(my_strcmp(arg, name) == 0) {
                    slot = i;
                    break;
                }
            }
            
            if(slot != -1) {
                int sector = dir_buf[slot*8 + 6];
                int size = dir_buf[slot*8 + 7];
                uint16_t data_buf[256];
                read_sector(sector, data_buf);
                
                static char file_buffer[4096];
                int pos = 0;
                for(int i = 0; i < size && pos < 4095; i++) {
                    char c;
                    if(i % 2 == 0) c = data_buf[i/2] & 0xFF;
                    else c = (data_buf[i/2] >> 8) & 0xFF;
                    if(c != 0) file_buffer[pos++] = c;
                }
                file_buffer[pos] = '\0';
                code = file_buffer;
                
                kprint("Compiling file: ");
                kprint(arg);
                kprint("\n");
            } else {
                kprint("File not found: ");
                kprint(arg);
                kprint("\n");
                return;
            }
        } else {
            code = (char*)arg;
            kprint("Compiling string...\n");
        }
        
        if(code && code[0]) {
            TCCState* s = tcc_new();
            if(s) {
                int has_main = 0;
                for(int i = 0; code[i]; i++) {
                    if(code[i] == 'm' && code[i+1] == 'a' && code[i+2] == 'i' && code[i+3] == 'n') {
                        has_main = 1;
                        break;
                    }
                }
                
                int compile_ok = (tcc_compile_string(s, code) == 0);
                
                if(compile_ok) {
                    kprint_color("Compilation successful!\n", TXT_GREEN);
                    
                    if(tcc_relocate(s, NULL) == 0) {
                        if(has_main) {
                            int result = tcc_run(s, 0, NULL);
                            kprint("Result: ");
                            kprint_int(result);
                            kprint("\n");
                        } else {
                            kprint("No main to run\n");
                        }
                    } else {
                        kprint("Relocation failed\n");
                    }
                    if(is_file && becap_slot != -1) {
                        uint16_t becap_buf[256];
                        int becap_dir_sector = dir_buf[becap_slot*8 + 6];
                        read_sector(becap_dir_sector, becap_buf);
                        
                        int dest_slot = -1;
                        for(int i = 0; i < 32; i++) {
                            char name[12] = {0};
                            for(int j = 0; j < 11; j++) name[j] = ((char*)becap_buf)[i*16 + j];
                            if(name[0] == 0) {
                                dest_slot = i;
                                break;
                            }
                        }
                        
                        if(dest_slot != -1) {
                            uint16_t src_buf[256];
                            int src_sector = 0;
                            int src_size = 0;
                            for(int i = 0; i < 32; i++) {
                                char name[12] = {0};
                                for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
                                if(my_strcmp(filename, name) == 0) {
                                    src_sector = dir_buf[i*8 + 6];
                                    src_size = dir_buf[i*8 + 7];
                                    break;
                                }
                            }
                            read_sector(src_sector, src_buf);
                            int dest_sector_num = 500 + dest_slot;
                            write_sector(dest_sector_num, src_buf);
                            for(int j = 0; j < 11 && filename[j]; j++) {
                                ((char*)becap_buf)[dest_slot*16 + j] = filename[j];
                            }
                            ((char*)becap_buf)[dest_slot*16 + 11] = 0;
                            becap_buf[dest_slot*8 + 6] = dest_sector_num;
                            becap_buf[dest_slot*8 + 7] = src_size;
                            write_sector(becap_dir_sector, becap_buf);
                            
                            kprint_color("Backup saved to becap/\n", TXT_GREEN);
                        }
                    }
                } else {
                    kprint_color("Compilation failed!\n", TXT_RED);
                    
                    if(is_file && becap_slot != -1) {
                        uint16_t becap_buf[256];
                        int becap_dir_sector = dir_buf[becap_slot*8 + 6];
                        read_sector(becap_dir_sector, becap_buf);
                        
                        int backup_slot = -1;
                        for(int i = 0; i < 32; i++) {
                            char name[12] = {0};
                            for(int j = 0; j < 11; j++) name[j] = ((char*)becap_buf)[i*16 + j];
                            if(my_strcmp(filename, name) == 0) {
                                backup_slot = i;
                                break;
                            }
                        }
                        
                        if(backup_slot != -1) {
                            kprint("Running last successful version from becap...\n");
                            
                            int backup_sector = becap_buf[backup_slot*8 + 6];
                            int backup_size = becap_buf[backup_slot*8 + 7];
                            uint16_t backup_buf[256];
                            read_sector(backup_sector, backup_buf);
                            
                            static char backup_code[4096];
                            int pos = 0;
                            for(int i = 0; i < backup_size && pos < 4095; i++) {
                                char c;
                                if(i % 2 == 0) c = backup_buf[i/2] & 0xFF;
                                else c = (backup_buf[i/2] >> 8) & 0xFF;
                                if(c != 0) backup_code[pos++] = c;
                            }
                            backup_code[pos] = '\0';
                            
                            TCCState* s2 = tcc_new();
                            if(s2) {
                                if(tcc_compile_string(s2, backup_code) == 0) {
                                    if(tcc_relocate(s2, NULL) == 0) {
                                        int result = tcc_run(s2, 0, NULL);
                                        kprint("Result from backup: ");
                                        kprint_int(result);
                                        kprint("\n");
                                    }
                                }
                                tcc_delete(s2);
                            }
                        } else {
                            kprint("No backup found in becap/\n");
                        }
                    }
                }
                tcc_delete(s);
            } else {
                kprint("TCC initialization failed\n");
            }
        }
    }
}
else if (my_strcmp(cmd_copy, "menus") == 0) {
    if(arg[0] == '\0') {
        kprint("\n=== VISUAL EFFECTS ===\n");
        kprint("1. plasma   - Plasma effect\n");
        kprint("2. waves    - Waves effect\n");
        kprint("3. tunnel   - 3D tunnel\n");
        kprint("4. fire     - Fire effect\n");
        kprint("Usage: menus <number>\n");
    } else {
        int choice = atoi(arg);
        switch(choice) {
            case 1:
                plasma_effect();
                break;
            case 2:
                waves_effect();
                break;
            case 3:
                tunnel_effect();
                break;
            case 4:
                ascii_fire();
                break;
            default:
                kprint("Invalid choice (1-5)\n");
                break;
        }
    }
}
else if (my_strcmp(cmd_copy, "netinit") == 0) {
    e1000_init();
    kprint("Network initialized\n");
}


else if (my_strcmp(cmd_copy, "http") == 0) {
    if(arg[0] == '\0') {
        kprint("Usage: http <url>\n");
        play_info_sound();
    } else {
        kprint("HTTP request to "); kprint(arg); kprint("\n");
        play_info_sound();
    }
}
else if (my_strcmp(input_buffer, "fire") == 0) {
    ascii_fire();
}
else if (my_strcmp(input_buffer, "cls") == 0) {
    clear_screen();
}

else if (my_strcmp(input_buffer, "clock") == 0) {
    ascii_clock();
}

else if (my_strcmp(input_buffer, "snake") == 0) {
    snake_game();
}

else if (my_strcmp(input_buffer, "rain") == 0) {
    ascii_rain();
}
    else if (my_strcmp(input_buffer, "monitor") == 0) {
    show_resource_monitor();
    input_ptr = 0;
    return;
}

else if (my_strcmp(input_buffer, "screensaver") == 0) {
    start_screensaver();
    input_ptr = 0;
    return;
}
else if (my_strcmp(input_buffer, "stars") == 0) {
    starfield_screensaver();
    input_ptr = 0;
    return;
}

else if (my_strcmp(cmd_copy, "copy") == 0) {
    int execute = 0;
    const char* text = "";
    
    if(arg[0] == '\0') {
        kprint("Usage: copy [-c] [-cr] <text>\n");
        kprint("  -c  : copy only\n");
        kprint("  -cr : copy and execute\n");
    }
    else {
        if(arg[0] == '-') {
            if(arg[1] == 'c' && arg[2] == 'r') {
                execute = 1;
                if(arg[3] == ' ') {
                    text = arg + 4;
                } else {
                    text = arg + 3;
                }
            }
            else if(arg[1] == 'c') {
                execute = 0;
                if(arg[2] == ' ') {
                    text = arg + 3;
                } else {
                    text = arg + 2;
                }
            }
            else {
                text = arg;
            }
        }
        else {
            text = arg;
        }
        while(*text == ' ') text++;
        if(text[0] != '\0') {
            clipboard_copy(text, execute);
            if(execute) {
                char cmd[256];
                int j;
                for(j = 0; j < 255 && text[j]; j++) {
                    cmd[j] = text[j];
                }
                cmd[j] = '\0';
                
                kprint("\n");
                char cmd_buffer[256];
                for(j = 0; j < 255 && cmd[j]; j++) {
                    cmd_buffer[j] = cmd[j];
                }
                cmd_buffer[j] = '\0';
                
                int temp_ptr = 0;
                process_command(cmd_buffer, temp_ptr);
            }
        } else {
            kprint("Nothing to copy\n");
        }
    }
}
else if (my_strcmp(cmd_copy, "paste") == 0) {
    int execute = 0;
    if(arg[0] == '-') {
        if(arg[1] == 'p' && arg[2] == 'r') {
            execute = 1;
        }
        else if(arg[1] == 'p') {
            execute = 0;
        }
        else {
            kprint("Unknown flag: ");
            kprint(arg);
            play_error_sound();
            kprint("\n");
        }
    }
    
    clipboard_paste(execute);
    
    if(execute && clipboard[0]) {
        static int in_paste_execute = 0;
        if(!in_paste_execute) {
            in_paste_execute = 1;
            process_command(clipboard, input_ptr);
            in_paste_execute = 0;
            return;
        }
    }
}
    else if (my_strcmp(input_buffer, "reboot") == 0) {
        kprint("Restarting...\n");
        play_reboot_sound();
        outb(0x64, 0xFE);
    }
    else if (my_strcmp(input_buffer, "shut") == 0) {
        power_off_extreme();
    }
    else if (my_strcmp(input_buffer, "about") == 0) {
        kprint_color("WnkaOS 32-bit Core v0.1.0b\n", 0x0E);
        kprint_color("AHCI + WNKFS Edition\n", 0x0A);
        play_info_sound();
    }
    else if (my_strcmp(input_buffer, "fetch") == 0) {
        run_fetch();
    }
    else if (my_strcmp(input_buffer, "beep") == 0) {
        kprint("Beep!\n");
        beep();
    }
    else if (my_strcmp(input_buffer, "say") == 0) {
        kprint("OS says: ");
        kprint(arg);
        play_info_sound();
        kprint("\n");
    }
    else if (my_strcmp(input_buffer, "hello") == 0) {
        kprint("Hello, World! Time: ");
        kprint_int(seconds);
        kprint(" seconds\n");
        play_info_sound();
    }
else if(my_strcmp(cmd_copy, "key") == 0) {
    if(arg[0] == '\0') {
        kprint("Usage: key XXXXX-XXXXX-X\n");
        kprint("Example: key 1A2B3-4C5D6-7\n");
        kprint("Format: 5 hex digits - 5 hex digits - 1 hex digit\n");
        if(is_activated()) {
            kprint_color("System is ACTIVATED\n", 0x0A);
            kprint("Your key: ");
            kprint(saved_key);
            kprint("\n");
        } else {
            kprint_color("System is NOT activated\n", 0x0C);
        }
    } else {
        if(verify_product_key(arg)) {
            save_key_to_memory(arg);
            save_key_to_disk(arg);
        } else {
            kprint_color("Invalid key format!\n", 0x0C);
            kprint("Use: 5 hex digits - 5 hex digits - 1 hex digit\n");
            kprint("Example: 1A2B3-4C5D6-7\n");
        }
    }
}

else if(my_strcmp(cmd_copy, "secret") == 0) {
    if(is_activated()) {
        kprint_color("ACCESS GRANTED. DEVELOPER MODE UNLOCKED.\n", 0x0A);
        kprint_color("Welcome to WNKA developer mode!\n", TXT_CYAN);
        kprint("You can now use:\n");
        kprint("  - Low-level hardware access\n");
        kprint("  - Kernel debugging\n");
        kprint("  - Custom module loading\n");
    } else {
        kprint_color("System not activated! Enter product key first.\n", 0x0C);
        kprint("Use: key XXXXX-XXXXX-X\n");
    }
}

else if(my_strcmp(cmd_copy, "mykey") == 0) {
    if(is_activated()) {
        kprint("Your key: ");
        kprint(saved_key);
        kprint("\n");
    } else {
        kprint("No key saved. System not activated.\n");
    }
}

    else if (my_strcmp(input_buffer, "check") == 0) {
        kprint("Checking port: ");
        kprint_hex16(ata_base_port);
        kprint("\n");
        check_disk_health();
    }
    else if (my_strcmp(input_buffer, "menu") == 0) {
        disk_menu();
    }
    else if (my_strcmp(input_buffer, "reformat") == 0) {
        restore_disk_brute();
    }
    else if (my_strcmp(input_buffer, "-disk") == 0) {
        disk_death();
    }
    else if (my_strcmp(input_buffer, "dump") == 0) {
        uint32_t sector = (uint32_t)atoi(arg);
        uint16_t dump_buf[256];
        read_sector(sector, dump_buf);
        kprint("Sector "); kprint_int(sector); kprint(":\n");
        for(int i = 0; i < 16; i++) {
            kprint_hex16(i*16); kprint(": ");
            for(int j = 0; j < 8; j++) {
                uint16_t w = dump_buf[i*8 + j];
                kprint_hex8(w & 0xFF); kprint(" ");
                kprint_hex8(w >> 8); kprint(" ");
            }
            kprint("\n");
        }
    }
    else if (my_strcmp(input_buffer, "disktest") == 0) {
        read_sector(0, disk_io_buf);
        kprint("Sector 0: ");
        for(int i = 0; i < 8; i++) {
            char c1 = (char)(disk_io_buf[i] & 0xFF);
            char c2 = (char)((disk_io_buf[i] >> 8) & 0xFF);
            char buf[2] = {c1, '\0'};
            kprint(buf);
            buf[0] = c2;
            kprint(buf);
        }
        kprint("\n");
    }
else if (my_strcmp(input_buffer, "cdrom") == 0) {
    kprint("CD-ROM commands:\n");
    kprint("  cdrom eject  - open tray\n");
    kprint("  cdrom close  - close tray\n");
    kprint("  cdrom status - check if media present\n");
    kprint("  cdrom play   - try to play audio CD\n");
    kprint("  cdrom stop   - stop audio CD\n");
}

else if (my_strcmp(input_buffer, "cdrom eject") == 0) {
    if(current_port == -1) {
        kprint("No CD-ROM selected\n");
    } else {
        eject_cdrom(current_port);
    }
}

else if (my_strcmp(input_buffer, "cdrom close") == 0) {
    if(current_port == -1) {
        kprint("No CD-ROM selected\n");
    } else {
        kprint("Closing tray...\n");
        static uint16_t dummy[256];
        if(send_cmd(current_port, 0x1B, 1, dummy)) {
            kprint("Tray closed\n");
        } else {
            kprint("Failed to close tray\n");
        }
    }
}

else if (my_strcmp(input_buffer, "cdrom status") == 0) {
    if(current_port == -1) {
        kprint("No CD-ROM selected\n");
    } else {
        volatile uint32_t* port_base = ahci_base + (0x80 + current_port * 0x20)/4;
        uint32_t tfd = port_base[8];
        uint32_t ssts = port_base[10];
        
        kprint("CD-ROM Status:\n");
        kprint("  SSTS: 0x"); kprint_hex32(ssts); kprint("\n");
        
        if(ssts & 0x01) {
            kprint("  Media present\n");
        } else {
            kprint("  No media\n");
        }
        
        if(tfd & 0x40) {
            kprint("  Tray is open\n");
        } else {
            kprint("  Tray is closed\n");
        }
    }
}

else if (my_strcmp(input_buffer, "cdrom play") == 0) {
    if(current_port == -1) {
        kprint("No CD-ROM selected\n");
    } else {
        kprint("Attempting to play audio CD...\n");
        static uint16_t dummy[256];
        if(send_cmd(current_port, 0x47, 0, dummy)) {
            kprint("Playing...\n");
        } else {
            kprint("Failed to play (may not be audio CD)\n");
        }
    }
}

else if (my_strcmp(input_buffer, "cdrom stop") == 0) {
    if(current_port == -1) {
        kprint("No CD-ROM selected\n");
    } else {
        kprint("Stopping audio...\n");
        static uint16_t dummy[256];
        if(send_cmd(current_port, 0x4F, 0, dummy)) {
            kprint("Stopped\n");
        } else {
            kprint("Failed to stop\n");
        }
    }
}
else if (my_strcmp(input_buffer, "scanpci") == 0) {
    find_all_controllers();
}

else if(my_strcmp(cmd_copy, "sheet") == 0) {
    char filename[256] = {0};
    
    if(arg[0] != '\0') {
        int i;
        for(i = 0; arg[i] && i < 255; i++) {
            filename[i] = arg[i];
        }
        filename[i] = '\0';
    } else {
        my_strcpy(filename, "sheet.csv");
    }
    
    char cells[30][30][128];
    int rows = 10, cols = 5;
    for(int i = 0; i < 30; i++) {
        for(int j = 0; j < 30; j++) {
            cells[i][j][0] = '\0';
        }
    }
    
    my_strcpy(cells[0][0], "Name");
    my_strcpy(cells[0][1], "Age");
    my_strcpy(cells[0][2], "City");
    my_strcpy(cells[0][3], "Phone");
    my_strcpy(cells[0][4], "Email");
    
    my_strcpy(cells[1][0], "John");
    my_strcpy(cells[1][1], "25");
    my_strcpy(cells[1][2], "NYC");
    
    my_strcpy(cells[2][0], "Jane");
    my_strcpy(cells[2][1], "30");
    my_strcpy(cells[2][2], "Boston");
    
    my_strcpy(cells[3][0], "Bob");
    my_strcpy(cells[3][1], "35");
    my_strcpy(cells[3][2], "Chicago");
    
    int fd = ramfs_open(filename, 0);
    if(fd >= 0) {
        char buffer[4096];
        int bytes = ramfs_read(fd, (uint8_t*)buffer, 4095);
        if(bytes > 0) {
            buffer[bytes] = '\0';
            int row = 0, col = 0, pos = 0;
            int cell_pos = 0;
            
            for(int i = 0; i < 30; i++) {
                for(int j = 0; j < 30; j++) {
                    cells[i][j][0] = '\0';
                }
            }
            
            while(buffer[pos] && row < 30) {
                if(buffer[pos] == ',') {
                    cells[row][col][cell_pos] = '\0';
                    col++;
                    cell_pos = 0;
                }
                else if(buffer[pos] == '\n') {
                    cells[row][col][cell_pos] = '\0';
                    row++;
                    col = 0;
                    cell_pos = 0;
                }
                else if(cell_pos < 127) {
                    cells[row][col][cell_pos++] = buffer[pos];
                }
                pos++;
            }
            rows = row + 1;
            cols = col + 1;
            if(rows < 1) rows = 1;
            if(cols < 1) cols = 1;
        }
        ramfs_close(fd);
    }
    
    int cursor_row = 0;
    int cursor_col = 0;
    int scroll_row = 0;
    int scroll_col = 0;
    int running = 1;
    int editing = 0;
    char edit_buffer[128];
    int edit_pos = 0;
    int redraw = 1;
    int ctrl_pressed = 0;
    
    const char* col_letters[] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J"};
    
    clear_screen();
    
    while(running) {
        if(redraw) {
            draw_frame(1, 0, 78, 24, BLUE, TXT_WHITE);
            
            kprint_at("SHEET EDITOR", 30, 0, (BLUE << 4) | TXT_YELLOW);
            kprint_at("File: ", 2, 0, (BLUE << 4) | TXT_CYAN);
            kprint_at(filename, 8, 0, (BLUE << 4) | TXT_WHITE);
            
            kprint_at("Cell: ", 55, 0, (BLUE << 4) | TXT_CYAN);
            kprint_at(col_letters[cursor_col], 61, 0, (BLUE << 4) | TXT_GREEN);
            kprint_int_at(cursor_row + 1, 63, 0, (BLUE << 4) | TXT_GREEN);
            
            kprint_at("[F1]Save [F2]Exit [Enter]Edit", 15, 23, (BLACK << 4) | TXT_YELLOW);
            kprint_at("[Ctrl+L/U/D/R] Move", 48, 23, (BLACK << 4) | TXT_CYAN);
            
            for(int c = 0; c < 8 && scroll_col + c < cols; c++) {
                int x = 4 + c * 10;
                draw_frame(x - 1, 1, 9, 1, BLUE, TXT_WHITE);
                kprint_at(col_letters[scroll_col + c], x + 3, 1, (BLUE << 4) | TXT_GREEN);
            }
            
            for(int r = 0; r < 20 && scroll_row + r < rows; r++) {
                int y = 2 + r;
                char num[4];
                int row_num = scroll_row + r + 1;
                if(row_num < 10) {
                    num[0] = row_num + '0';
                    num[1] = '\0';
                } else {
                    num[0] = (row_num / 10) + '0';
                    num[1] = (row_num % 10) + '0';
                    num[2] = '\0';
                }
                draw_frame(1, y, 2, 1, BLUE, TXT_WHITE);
                kprint_at(num, 2, y, (BLUE << 4) | TXT_GREEN);
            }
            
            for(int r = 0; r < 20 && scroll_row + r < rows; r++) {
                for(int c = 0; c < 8 && scroll_col + c < cols; c++) {
                    int x = 4 + c * 10;
                    int y = 2 + r;
                    int is_cursor = (cursor_row == scroll_row + r && cursor_col == scroll_col + c);
                    
                    kprint_at("          ", x, y, (BLACK << 4) | TXT_BLACK);
                    
                    char display[11];
                    int len = 0;
                    while(cells[scroll_row + r][scroll_col + c][len] && len < 9) {
                        display[len] = cells[scroll_row + r][scroll_col + c][len];
                        len++;
                    }
                    display[len] = '\0';
                    
                    if(is_cursor && !editing) {
                        draw_frame(x - 1, y - 1, 11, 2, GREEN, TXT_BLACK);
                        kprint_at(display, x, y, (GREEN << 4) | TXT_BLACK);
                    } else {
                        kprint_at(display, x, y, (BLACK << 4) | TXT_WHITE);
                    }
                }
            }
            
            if(editing) {
                draw_frame(10, 18, 60, 4, BLACK, TXT_WHITE);
                kprint_at("EDIT CELL: ", 12, 19, (BLACK << 4) | TXT_CYAN);
                kprint_at(col_letters[cursor_col], 26, 19, (BLACK << 4) | TXT_GREEN);
                kprint_int_at(cursor_row + 1, 28, 19, (BLACK << 4) | TXT_GREEN);
                kprint_at(": ", 30, 19, (BLACK << 4) | TXT_WHITE);
                kprint_at(edit_buffer, 32, 19, (BLACK << 4) | TXT_YELLOW);
                kprint_at("                        ", 32 + edit_pos, 19, (BLACK << 4) | TXT_BLACK);
                kprint_at("[Enter]OK [ESC]Cancel", 20, 21, (BLACK << 4) | TXT_YELLOW);
            }
            
            redraw = 0;
        }
        
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            
            if(sc == 0x1D) {
                ctrl_pressed = 1;
            }
            else if(sc == 0x9D) {
                ctrl_pressed = 0;
            }
            
            if(sc < 0x80) {
                
                if(editing) {
                    if(sc == 0x1C) {
                        my_strcpy(cells[cursor_row][cursor_col], edit_buffer);
                        editing = 0;
                        redraw = 1;
                    }
                    else if(sc == 0x01) { 
                        editing = 0;
                        redraw = 1;
                    }
                    else if(sc == 0x0E && edit_pos > 0) {  
                        edit_pos--;
                        edit_buffer[edit_pos] = '\0';
                        redraw = 1;
                    }
                    else if(sc >= 0x02 && sc <= 0x0D && edit_pos < 127) {
                        const char* chars = "1234567890-=";
                        edit_buffer[edit_pos++] = chars[sc - 0x02];
                        edit_buffer[edit_pos] = '\0';
                        redraw = 1;
                    }
                    else if(sc >= 0x10 && sc <= 0x19 && edit_pos < 127) {
                        const char* chars = "qwertyuiop";
                        edit_buffer[edit_pos++] = chars[sc - 0x10];
                        edit_buffer[edit_pos] = '\0';
                        redraw = 1;
                    }
                    else if(sc >= 0x1E && sc <= 0x26 && edit_pos < 127) {
                        const char* chars = "asdfghjkl";
                        edit_buffer[edit_pos++] = chars[sc - 0x1E];
                        edit_buffer[edit_pos] = '\0';
                        redraw = 1;
                    }
                    else if(sc >= 0x2C && sc <= 0x32 && edit_pos < 127) {
                        const char* chars = "zxcvbnm";
                        edit_buffer[edit_pos++] = chars[sc - 0x2C];
                        edit_buffer[edit_pos] = '\0';
                        redraw = 1;
                    }
                    else if(sc == 0x39 && edit_pos < 127) {
                        edit_buffer[edit_pos++] = ' ';
                        edit_buffer[edit_pos] = '\0';
                        redraw = 1;
                    }
                }
                else {
                    if(sc == 0x01) {
                        running = 0;
                    }
                    else if(sc == 0x3B) {
                        int new_fd = ramfs_open(filename, 1);
                        if(new_fd >= 0) {
                            char save_buffer[16384];
                            int save_pos = 0;
                            for(int r = 0; r < rows; r++) {
                                for(int c = 0; c < cols; c++) {
                                    for(int i = 0; cells[r][c][i] && save_pos < 16383; i++) {
                                        save_buffer[save_pos++] = cells[r][c][i];
                                    }
                                    if(c < cols - 1 && save_pos < 16383) {
                                        save_buffer[save_pos++] = ',';
                                    }
                                }
                                if(save_pos < 16383) save_buffer[save_pos++] = '\n';
                            }
                            ramfs_write(new_fd, (uint8_t*)save_buffer, save_pos);
                            ramfs_close(new_fd);
                            redraw = 1;
                            kprint_at("Saved!", 60, 23, (BLACK << 4) | TXT_GREEN);
                        }
                    }
                    else if(sc == 0x3C) {
                        running = 0;
                    }
                    else if(sc == 0x1C) {
                        my_strcpy(edit_buffer, cells[cursor_row][cursor_col]);
                        edit_pos = my_strlen(edit_buffer);
                        editing = 1;
                        redraw = 1;
                    }
                    else if(ctrl_pressed && sc == 0x26) {
                        if(cursor_col > 0) {
                            cursor_col--;
                            if(cursor_col < scroll_col) scroll_col = cursor_col;
                            redraw = 1;
                        }
                    }
                    else if(ctrl_pressed && sc == 0x16) {  
                        if(cursor_row > 0) {
                            cursor_row--;
                            if(cursor_row < scroll_row) scroll_row = cursor_row;
                            redraw = 1;
                        }
                    }
                    else if(ctrl_pressed && sc == 0x20) {  
                        if(cursor_row < rows - 1) {
                            cursor_row++;
                            if(cursor_row >= scroll_row + 20) scroll_row = cursor_row - 19;
                            redraw = 1;
                        }
                    }
                    else if(ctrl_pressed && sc == 0x13) { 
                        if(cursor_col < cols - 1) {
                            cursor_col++;
                            if(cursor_col >= scroll_col + 8) scroll_col = cursor_col - 7;
                            redraw = 1;
                        }
                    }
                    else if(sc == 0x48 && cursor_row > 0) { 
                        cursor_row--;
                        if(cursor_row < scroll_row) scroll_row = cursor_row;
                        redraw = 1;
                    }
                    else if(sc == 0x50 && cursor_row < rows - 1) { 
                        cursor_row++;
                        if(cursor_row >= scroll_row + 20) scroll_row = cursor_row - 19;
                        redraw = 1;
                    }
                    else if(sc == 0x4B && cursor_col > 0) {  
                        cursor_col--;
                        if(cursor_col < scroll_col) scroll_col = cursor_col;
                        redraw = 1;
                    }
                    else if(sc == 0x4D && cursor_col < cols - 1) { 
                        cursor_col++;
                        if(cursor_col >= scroll_col + 8) scroll_col = cursor_col - 7;
                        redraw = 1;
                    }
                }
            }
            while(inb(0x64) & 1) inb(0x60);
        }
        
        for(volatile int i = 0; i < 5000; i++);
    }
    
    clear_screen();
    kprint_color("Sheet editor closed\n", TXT_GREEN);
}


    else if (my_strcmp(input_buffer, "fwrite") == 0) {
        char* filename = arg;
        char* text = (char*)"";
        for(int i = 0; arg[i]; i++) {
            if(arg[i] == ' ') {
                arg[i] = '\0';
                text = &arg[i+1];
                break;
            }
        }
        uint16_t dir_buf[256];
        read_sector(2, dir_buf);
        DiskFile* list = (DiskFile*)dir_buf;
        for(int i = 0; i < 32; i++) {
            if(my_strcmp(filename, list[i].name) == 0) {
                uint16_t data_buf[256] = {0};
                for(int j = 0; text[j] && j < 510; j += 2) {
                    data_buf[j/2] = (uint16_t)text[j] | ((uint16_t)text[j+1] << 8);
                }
                write_sector(list[i].start_sector, data_buf);
                kprint("Written to "); kprint(filename); kprint("\n");
                break;
            }
        }
    }
else if (my_strcmp(input_buffer, "fm") == 0) {
    uint16_t dir_buf_left[256], dir_buf_right[256];
    uint16_t left_dir_sector = current_dir_sector;
    uint16_t right_dir_sector = current_dir_sector;
    char left_path[64] = "/";
    char right_path[64] = "/";
    int left_cursor = 0, right_cursor = 0;
    int active_panel = 0;
    bool running = true;
    int scroll_left = 0, scroll_right = 0;
    char input_buf[64] = {0};
    int input_pos = 0;
    bool input_mode = false;
    char status_msg[64];
status_msg[0] = 'F'; status_msg[1] = '1'; status_msg[2] = '-'; status_msg[3] = 'H'; status_msg[4] = 'e'; status_msg[5] = 'l'; status_msg[6] = 'p';
status_msg[7] = ' '; status_msg[8] = 'F'; status_msg[9] = '2'; status_msg[10] = '-'; status_msg[11] = 'S'; status_msg[12] = 'a'; status_msg[13] = 'v'; status_msg[14] = 'e';
status_msg[15] = ' '; status_msg[16] = 'F'; status_msg[17] = '3'; status_msg[18] = '-'; status_msg[19] = 'V'; status_msg[20] = 'i'; status_msg[21] = 'e'; status_msg[22] = 'w';
status_msg[23] = ' '; status_msg[24] = 'F'; status_msg[25] = '4'; status_msg[26] = '-'; status_msg[27] = 'E'; status_msg[28] = 'd'; status_msg[29] = 'i'; status_msg[30] = 't';
status_msg[31] = ' '; status_msg[32] = 'F'; status_msg[33] = '5'; status_msg[34] = '-'; status_msg[35] = 'C'; status_msg[36] = 'o'; status_msg[37] = 'p'; status_msg[38] = 'y';
status_msg[39] = ' '; status_msg[40] = 'F'; status_msg[41] = '6'; status_msg[42] = '-'; status_msg[43] = 'M'; status_msg[44] = 'o'; status_msg[45] = 'v'; status_msg[46] = 'e';
status_msg[47] = ' '; status_msg[48] = 'F'; status_msg[49] = '7'; status_msg[50] = '-'; status_msg[51] = 'M'; status_msg[52] = 'k'; status_msg[53] = 'D'; status_msg[54] = 'i'; status_msg[55] = 'r';
status_msg[56] = ' '; status_msg[57] = 'F'; status_msg[58] = '1'; status_msg[59] = '0'; status_msg[60] = '-'; status_msg[61] = 'Q'; status_msg[62] = 'u'; status_msg[63] = 'i'; status_msg[64] = 't'; status_msg[65] = 0;
    int key_repeat = 0;
    int last_key = 0;

    while(running) {
        read_sector(left_dir_sector, dir_buf_left);
        read_sector(right_dir_sector, dir_buf_right);
        
        clear_screen();
        
        kprint_at("WNKA TC v1.0", 32, 0, 0x1F);
        
        for(int x = 0; x < 40; x++) {
            put_pixel(x, 1, 0x70, 0x0F, S_HLINE);
            put_pixel(x, 2, 0x70, 0x0F, ' ');
        }
        kprint_at(left_path, 1, 2, (active_panel == 0) ? 0x4F : 0x70);
        
        for(int x = 0; x < 40; x++) {
            put_pixel(x, 3, 0x70, 0x0F, S_HLINE);
        }
        
        for(int i = 0; i < 18 && scroll_left + i < 32; i++) {
            int idx = scroll_left + i;
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf_left)[idx*16 + j];
            if(name[0] != 0) {
                int is_dir = ((char*)dir_buf_left)[idx*16 + 11] == 1;
                int size = dir_buf_left[idx*8 + 7];
                uint8_t color;
                if(active_panel == 0 && idx == left_cursor) {
                    color = 0x2F;
                    for(int x = 0; x < 39; x++) put_pixel(x, 4 + i, 0x20, 0x0F, ' ');
                } else {
                    color = 0x07;
                }
                
                char line[40] = {0};
                if(is_dir) {
                    line[0] = '/';
                    int j=0; while(name[j] && j<10) { line[j+1] = name[j]; j++; }
                    kprint_at(line, 1, 4 + i, color);
                    kprint_at("<DIR>", 30, 4 + i, 0x0E);
                } else {
                    int j=0; while(name[j] && j<11) { line[j] = name[j]; j++; }
                    kprint_at(line, 1, 4 + i, color);
                    char size_str[12];
                    int_to_str(size, size_str);
                    kprint_at(size_str, 30, 4 + i, 0x08);
                }
            }
        }
        
        for(int x = 40; x < 80; x++) {
            put_pixel(x, 1, 0x70, 0x0F, S_HLINE);
            put_pixel(x, 2, 0x70, 0x0F, ' ');
        }
        kprint_at(right_path, 41, 2, (active_panel == 1) ? 0x4F : 0x70);
        
        for(int x = 40; x < 80; x++) {
            put_pixel(x, 3, 0x70, 0x0F, S_HLINE);
        }
        
        for(int i = 0; i < 18 && scroll_right + i < 32; i++) {
            int idx = scroll_right + i;
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf_right)[idx*16 + j];
            if(name[0] != 0) {
                int is_dir = ((char*)dir_buf_right)[idx*16 + 11] == 1;
                int size = dir_buf_right[idx*8 + 7];
                uint8_t color;
                if(active_panel == 1 && idx == right_cursor) {
                    color = 0x2F;
                    for(int x = 40; x < 79; x++) put_pixel(x, 4 + i, 0x20, 0x0F, ' ');
                } else {
                    color = 0x07;
                }
                
                char line[40] = {0};
                if(is_dir) {
                    line[0] = '/';
                    int j=0; while(name[j] && j<10) { line[j+1] = name[j]; j++; }
                    kprint_at(line, 41, 4 + i, color);
                    kprint_at("<DIR>", 70, 4 + i, 0x0E);
                } else {
                    int j=0; while(name[j] && j<11) { line[j] = name[j]; j++; }
                    kprint_at(line, 41, 4 + i, color);
                    char size_str[12];
                    int_to_str(size, size_str);
                    kprint_at(size_str, 70, 4 + i, 0x08);
                }
            }
        }
        
        for(int y = 1; y < 24; y++) {
            put_pixel(39, y, 0x70, 0x0F, S_VLINE);
            put_pixel(40, y, 0x70, 0x0F, S_VLINE);
        }
        
        for(int x = 0; x < 80; x++) {
            put_pixel(x, 22, 0x70, 0x0F, S_HLINE);
            put_pixel(x, 23, 0x70, 0x0F, ' ');
        }
        kprint_at(status_msg, 1, 23, 0x0F);
        
        if(input_mode) {
            kprint_at(">", 0, 24, 0x0F);
            kprint_at(input_buf, 2, 24, 0x0F);
            kprint_at("_", 2 + input_pos, 24, 0x8F);
        } else {
            kprint_at("1Help 2Save 3View 4Edit 5Copy 6Move 7MkDir 8Delete 10Quit", 1, 24, 0x0F);
        }
        
        move_cursor(79, 24);
        
        if(inb(0x64) & 1) {
            uint8_t key = inb(0x60);
            if(key < 0x80) {
                if(input_mode) {
                    if(key == 0x1C) {
                        input_buf[input_pos] = 0;
                        input_mode = false;
                        
                        if(input_buf[0]) {
                            if(active_panel == 0) {
                                int slot = -1;
                                for(int j = 0; j < 32; j++) {
                                    if(((char*)dir_buf_left)[j*16] == 0) { slot = j; break; }
                                }
                                if(slot != -1) {
                                    for(int j = 0; j < 11 && input_buf[j]; j++) {
                                        ((char*)dir_buf_left)[slot*16 + j] = input_buf[j];
                                    }
                                    ((char*)dir_buf_left)[slot*16 + 11] = 1;
                                    dir_buf_left[slot*8 + 6] = 300 + slot;
                                    dir_buf_left[slot*8 + 7] = 0;
                                    write_sector(left_dir_sector, dir_buf_left);
                                }
                            } else {
                                int slot = -1;
                                for(int j = 0; j < 32; j++) {
                                    if(((char*)dir_buf_right)[j*16] == 0) { slot = j; break; }
                                }
                                if(slot != -1) {
                                    for(int j = 0; j < 11 && input_buf[j]; j++) {
                                        ((char*)dir_buf_right)[slot*16 + j] = input_buf[j];
                                    }
                                    ((char*)dir_buf_right)[slot*16 + 11] = 1;
                                    dir_buf_right[slot*8 + 6] = 300 + slot;
                                    dir_buf_right[slot*8 + 7] = 0;
                                    write_sector(right_dir_sector, dir_buf_right);
                                }
                            }
                        }
                        input_pos = 0;
                        for(int i=0;i<64;i++) input_buf[i]=0;
                    }
                    else if(key == 0x01) { input_mode = false; input_pos = 0; }
                    else if(key == 0x0E && input_pos > 0) { input_pos--; input_buf[input_pos]=0; }
                    else if(input_pos < 30) {
                        char ch = 0;
                        if(key >= 0x10 && key <= 0x19) ch = "qwertyuiop"[key-0x10];
                        else if(key >= 0x1E && key <= 0x26) ch = "asdfghjkl"[key-0x1E];
                        else if(key >= 0x2C && key <= 0x32) ch = "zxcvbnm"[key-0x2C];
                        else if(key >= 0x02 && key <= 0x0B) ch = "1234567890"[key-0x02];
                        else if(key == 0x39) ch = ' ';
                        else if(key == 0x34) ch = '.';
                        if(ch) { input_buf[input_pos++] = ch; input_buf[input_pos] = 0; }
                    }
                } else {
                    if(key == 0x01) { running = false; }
                    if(key == 0x0F || key == 0x4B || key == 0x4D) { active_panel = !active_panel; }
                    
                    if(key == 0x48) {
                        if(active_panel == 0 && left_cursor > 0) { 
                            left_cursor--; 
                            if(left_cursor < scroll_left) scroll_left = left_cursor;
                        }
                        if(active_panel == 1 && right_cursor > 0) { 
                            right_cursor--; 
                            if(right_cursor < scroll_right) scroll_right = right_cursor;
                        }
                    }
                    if(key == 0x50) {
                        if(active_panel == 0 && left_cursor < 31) { 
                            left_cursor++; 
                            if(left_cursor >= scroll_left + 18) scroll_left = left_cursor - 17;
                        }
                        if(active_panel == 1 && right_cursor < 31) { 
                            right_cursor++; 
                            if(right_cursor >= scroll_right + 18) scroll_right = right_cursor - 17;
                        }
                    }
                    
                    if(key == 0x1C) {
                        if(active_panel == 0) {
                            char name[12] = {0};
                            for(int j=0;j<11;j++) name[j] = ((char*)dir_buf_left)[left_cursor*16+j];
                            if(name[0] && ((char*)dir_buf_left)[left_cursor*16+11]==1) {
                                left_dir_sector = dir_buf_left[left_cursor*8+6];
                                left_cursor = 0;
                                scroll_left = 0;
                            }
                        } else {
                            char name[12] = {0};
                            for(int j=0;j<11;j++) name[j] = ((char*)dir_buf_right)[right_cursor*16+j];
                            if(name[0] && ((char*)dir_buf_right)[right_cursor*16+11]==1) {
                                right_dir_sector = dir_buf_right[right_cursor*8+6];
                                right_cursor = 0;
                                scroll_right = 0;
                            }
                        }
                    }
                    
                    if(key == 0x41) {
                        input_mode = true;
                        input_pos = 0;
                        for(int i=0;i<64;i++) input_buf[i]=0;
                    }
                    
                    if(key == 0x44) { running = false; }
                }
            }
            while(inb(0x64) & 1) inb(0x60);
        }
        
        for(volatile int i = 0; i < 100000; i++);
    }
    clear_screen();
}
else if(my_strcmp(cmd_copy, "calc") == 0) {
    int a = 0, b = 0;
    char op = 0;
    
    char* p = arg;
    while(*p >= '0' && *p <= '9') {
        a = a * 10 + (*p - '0');
        p++;
    }
    op = *p;
    p++;
    while(*p >= '0' && *p <= '9') {
        b = b * 10 + (*p - '0');
        p++;
    }
    
    int result = 0;
    switch(op) {
        case '+': result = a + b; break;
        case '-': result = a - b; break;
        case '*': result = a * b; break;
        case '/': 
            if(b != 0) result = a / b;
            else { kprint("Error: division by zero\n"); return; }
            break;
        default: kprint("Error: unknown operator\n"); return;
    }
    
    kprint_int(a);
    kprint_char(' ');
    kprint_char(op);
    kprint_char(' ');
    kprint_int(b);
    kprint(" = ");
    kprint_int(result);
    kprint("\n");
}
else if(my_strcmp(cmd_copy, "fortune") == 0) {
    const char* quotes[] = {
        "The only limit is your imagination.",
        "Code is poetry.",
        "WNKA OS - made with love.",
        "Keep coding!",
        "Bugs are features in disguise.",
        "First solve the problem, then write the code.",
        NULL
    };
    int count = 0;
    while(quotes[count]) count++;
    int r = my_rand() % count;
    kprint(quotes[r]);
    kprint("\n");
}
else if(my_strcmp(cmd_copy, "matrix") == 0) {
    kprint("[MATRIX] Press ESC to exit\n");
    
    my_srand(12345);
    
    int drops[80];
    int symbols[80];
    
    for(int i = 0; i < 80; i++) {
        drops[i] = my_rand() % 25;
        symbols[i] = my_rand() % 10;
    }
    
    while(1) {
        for(int x = 0; x < 80; x++) {
            if(drops[x] > 0) {
                kprint_at(" ", x, drops[x] - 1, 0x07);
            }
            
            drops[x]++;
            if(drops[x] >= 25) {
                drops[x] = 0;
                symbols[x] = my_rand() % 10;
            }
            
            char c = '0' + symbols[x];
            int brightness = drops[x] / 3;
            int color = 0x0A;
            if(brightness < 2) color = 0x08;
            else if(brightness < 5) color = 0x0A;
            else color = 0x0F;
            
            kprint_at(&c, x, drops[x], color);
        }
        
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc == 0x01) break;
        }
        
        for(volatile int d = 0; d < 5000000; d++);
    }
    
    clear_screen();
    kprint("[MATRIX] Exited\n");
}
else if(my_strcmp(cmd_copy, "cowsay") == 0) {
    const char* text = arg;
    static const char* default_text = "Moo!";
    if(text[0] == '\0') text = default_text;
    kprint(" __________\n");
    kprint("< ");
    kprint(text);
    kprint(" >\n");
    kprint(" ----------\n");
    kprint("        \\   ^__^\n");
    kprint("         \\  (oo)\\_______\n");
    kprint("            (__)\\       )\\/\\\n");
    kprint("                ||----w |\n");
    kprint("                ||     ||\n");
}
else if(my_strcmp(cmd_copy, "linux") == 0) {
    kprint("   .--.\n");
    kprint("  |o_o |\n");
    kprint("  |:_/ |\n");
    kprint(" //   \\ \\\n");
    kprint("(|     |)\n");
    kprint("/'\\_   _/`\\\n");
    kprint("\\___)=(___/\n");
    kprint("\nsorry is not linux\n");
}
else if(my_strcmp(cmd_copy, "os") == 0) {
    const char* oss[] = {"Windows", "Linux", "macOS", "FreeBSD", "KolibriOS", "ReactOS", "Haiku", "WNKA"};
    int r = my_rand() % 8;
    kprint("Your OS is: ");
    kprint(oss[r]);
    kprint("\n");
    if(my_strcmp(oss[r], "WNKA") == 0) {
        kprint("The best one!\n");
    }
}
else if(my_strcmp(cmd_copy, "rps") == 0) {
    const char* choices[] = {"rock", "paper", "scissors"};
    int user = 0;
    if(arg[0] != '\0') {
        if(my_strcmp(arg, "rock") == 0) user = 0;
        else if(my_strcmp(arg, "paper") == 0) user = 1;
        else if(my_strcmp(arg, "scissors") == 0) user = 2;
        else { kprint("Choose: rock, paper, scissors\n"); return; }
    } else {
        kprint("Usage: rps [rock|paper|scissors]\n");
        return;
    }
    int comp = my_rand() % 3;
    kprint("You: "); kprint(choices[user]); kprint("\n");
    kprint("AI:  "); kprint(choices[comp]); kprint("\n");
    
    if(user == comp) kprint("Draw!\n");
    else if((user == 0 && comp == 2) ||
            (user == 1 && comp == 0) ||
            (user == 2 && comp == 1)) {
        kprint("You win!\n");
    } else {
        kprint("You lose!\n");
    }
}
else if(my_strcmp(cmd_copy, "guess") == 0) {
    int secret = (my_rand() % 100) + 1;
    int guess = 0;
    int attempts = 0;
    char input[10];
    int input_pos = 0;
    
    kprint("I'm thinking of a number between 1 and 100.\n");
    
    while(guess != secret) {
        kprint("Your guess: ");
        
        input_pos = 0;
        while(1) {
            if(inb(0x64) & 1) {
                uint8_t sc = inb(0x60);
                if(sc == 0x1C) {
                    input[input_pos] = '\0';
                    break;
                }
                else if(sc == 0x0E && input_pos > 0) {
                    input_pos--;
                    kprint("\b \b");
                }
                else if(sc >= 0x02 && sc <= 0x0B && input_pos < 9) {
                    char ch = "1234567890"[sc - 0x02];
                    input[input_pos++] = ch;
                    kprint_char(ch);
                }
            }
        }
        
        guess = 0;
        for(int i = 0; input[i] && input[i] >= '0' && input[i] <= '9'; i++) {
            guess = guess * 10 + (input[i] - '0');
        }
        
        attempts++;
        
        if(guess < secret) {
            kprint("Too low!\n");
        }
        else if(guess > secret) {
            kprint("Too high!\n");
        }
    }
    
    kprint("Correct! You got it in ");
    kprint_int(attempts);
    kprint(" attempts!\n");
}
else if(my_strcmp(cmd_copy, "8ball") == 0) {
    const char* answers[] = {
        "Yes", "No", "Maybe", "Ask again",
        "Definitely", "Never", "Probably", "Doubtful"
    };
    int r = my_rand() % 8;
    kprint("(8) ");
    kprint(answers[r]);
    kprint("\n");
}
else if(my_strcmp(cmd_copy, "art") == 0) {
    const char* arts[] = {
        "   /\\_/\\\n  (o.o)\n   >^<\n",
        "   .--.\n  |o_o |\n  |:_/ |\n //   \\ \\\n(|     |)\n",
        "    *\n   * *\n  *   *\n *     *\n*********\n",
        "  ♥♥♥\n ♥   ♥\n♥     ♥\n ♥   ♥\n  ♥♥♥\n"
    };
    int current = 0;
    kprint("ASCII Gallery - Use ←/→ to browse, ESC to exit\n");
    
    while(1) {
        kprint_at(arts[current], 30, 5, 0x0F);
        kprint_at("Image ", 30, 15, 0x0F);
        kprint_int(current+1);
        kprint_at("/4", 37, 15, 0x0F);
        
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc == 0x01) break;
            if(sc == 0x4B && current > 0) current--;
            if(sc == 0x4D && current < 3) current++;
        }
        for(volatile int d = 0; d < 1000000; d++);
    }
}
    else if (my_strcmp(input_buffer, "galc") == 0) {
        run_calc();
        clear_screen();
    }
    else if (my_strcmp(cmd_copy, "dcmouse") == 0) {
        disable_mouse();
    }
    else if (my_strcmp(input_buffer, "gui") == 0) {
        show_gui();
        clear_screen();
    }
    else if (my_strcmp(input_buffer, "pacman") == 0) {
        start_pacman();
        clear_screen();
    }
    else if (my_strcmp(input_buffer, "flappy") == 0) {
        start_flappy();
        clear_screen();
    }
    else if (my_strcmp(input_buffer, "pci") == 0) {
        int page = 0;
        bool viewing = true;
        while(viewing) {
            clear_screen();
            kprint_at("PCI DEVICES (page ", 0, 0, 0x0F);
            kprint_int_at(page, 18, 0, 0x0F);
            kprint_at(")", 20, 0, 0x0F);
            
            for(int i = page*10; i < page*10+10 && i < 256; i++) {
                uint32_t id = pci_read(0, i, 0, 0);
                if(id != 0xFFFFFFFF) {
                    kprint_int_at(i, 5, 2+i-page*10, 0x0B);
                    kprint_at(": 0x", 7, 2+i-page*10, 0x07);
                    kprint_hex16_at(id >> 16, 11, 2+i-page*10, 0x0F);
                    kprint_hex16_at(id & 0xFFFF, 16, 2+i-page*10, 0x0F);
                    kprint_at("\n", 21, 2+i-page*10, 0x07);
                }
            }
            
            kprint_at("N-next P-prev ESC-exit", 20, 22, 0x0E);
            
            uint8_t key = inb(0x60);
            if(key == 0x31) page++;
            if(key == 0x19 && page > 0) page--;
            if(key == 0x01) viewing = false;
            
            for(volatile int i = 0; i < 500000; i++);
        }
        clear_screen();
    }
    else if (my_strcmp(input_buffer, "memtest") == 0) {
        uint32_t start = 0x100000;
        uint32_t size = 16;
        if(arg[0]) size = atoi(arg);
        kprint("Testing "); kprint_int(size); kprint(" MB...\n");
        volatile uint32_t* mem = (uint32_t*)start;
        uint32_t errors = 0;
        for(uint32_t i = 0; i < size * 256 * 1024; i++) {
            uint32_t pat = 0x55AA55AA ^ i;
            mem[i] = pat;
            if(mem[i] != pat) errors++;
            if(i % (1024*1024) == 0) kprint(".");
        }
        kprint("\nErrors: "); kprint_int(errors); kprint("\n");
    }


else if (my_strcmp(cmd_copy, "time") == 0) {
    Time t = get_time();
    kprint("Time: ");
    print_time(t);
    kprint("\n");
}
else if (my_strcmp(input_buffer, "panic") == 0) {
    kernel_panic("User quit", __FILE__, __LINE__, __FUNCTION__);
}

else if (my_strcmp(cmd_copy, "theme") == 0) {
    theme_command(arg);
}
else if(my_strcmp(cmd_copy, "sound") == 0) {
    if(arg[0] == '\0') {
        kprint("Sound commands:\n");
        kprint("  sound boot    - boot sound\n");
        kprint("  sound error   - error sound\n");
        kprint("  sound success - success sound\n");
        kprint("  sound click   - click\n");
    }
    else if(my_strcmp(arg, "boot") == 0) {
        play_startup_sound();
    }
    else if(my_strcmp(arg, "error") == 0) {
        play_error_sound();
    }
    else if(my_strcmp(arg, "success") == 0) {
        play_success_sound();
    }
    else if(my_strcmp(arg, "click") == 0) {
        play_click_sound();
    }
}
else if(my_strcmp(cmd_copy, "edit") == 0) {
    int saved_cursor_x = cursor_x;
    int saved_cursor_y = cursor_y;
    int saved_input_ptr = input_ptr;
    char saved_input_buffer[256];
    my_strcpy(saved_input_buffer, input_buffer);
    
    clear_screen();
    
    kprint_color(" __      __  _   _  _  __    _  ", 0x0B);
    kprint("\n");
    kprint_color(" \\ \\    /  / | \\ | || |/ /   / \\ ", 0x0B);
    kprint("\n");
    kprint_color("  \\ \\/\\/  /  |  \\| || ' /   / _ \\", 0x0B);
    kprint("\n");
    kprint_color("   \\  /\\ /   | |\\  || . \\  / ___ \\", 0x0B);
    kprint("\n");
    kprint_color("    \\/ \\/    |_| \\_||_|\\_\\/_/   \\_\\", 0x0B);
    kprint("\n\n");
    
    kprint_color("                     ===== Wnka TXT Editor v0.1 =====\n\n", TXT_GREEN);
    kprint_color("                        Wnka-Software 2025-2026\n\n", TXT_YELLOW);
    kprint_color("                       Press any key to continue...\n", TXT_CYAN);
    
    while(1) {
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc < 0x80) break;
        }
    }
    while(inb(0x64) & 1) inb(0x60);
    
    char filename[256] = {0};
    
    if(arg[0] != '\0') {
        int i;
        for(i = 0; arg[i] && i < 255; i++) {
            filename[i] = arg[i];
        }
        filename[i] = '\0';
    } else {
        my_strcpy(filename, "unnamed.txt");
    }
    
    int fd = ramfs_open(filename, 0);
    char lines[100][80];
    int line_count = 0;
    uint8_t line_colors[100][80];
    
    for(int i = 0; i < 100; i++) {
        lines[i][0] = '\0';
        for(int j = 0; j < 80; j++) {
            line_colors[i][j] = TXT_WHITE;
        }
    }
    
    if(fd >= 0) {
        char buffer[4096];
        int bytes = ramfs_read(fd, buffer, 4096);
        if(bytes > 0) {
            buffer[bytes] = '\0';
            int line = 0;
            int pos = 0;
            int char_pos = 0;
            while(buffer[pos] && line < 100) {
                if(buffer[pos] == '\n') {
                    lines[line][char_pos] = '\0';
                    line++;
                    char_pos = 0;
                } else if(char_pos < 79) {
                    lines[line][char_pos++] = buffer[pos];
                }
                pos++;
            }
            if(char_pos > 0) {
                lines[line][char_pos] = '\0';
                line_count = line + 1;
            } else {
                line_count = line;
            }
        }
        ramfs_close(fd);
    } else {
        lines[0][0] = '\0';
        line_count = 1;
    }
    
    int cursor_line = 0;
    int cursor_col = 0;
    int scroll = 0;
    int running = 1;
    int insert_mode = 1;
    int need_redraw = 1;
    int shift_pressed_local = 0;
    int ctrl_pressed = 0;
    int menu_active = 0;
    int menu_selection = 0;
    int color_menu_active = 0;
    int color_selection = 0;
    uint8_t current_color = TXT_WHITE;
    
    const char* color_names[] = {"White", "Green", "Red", "Blue", "Magenta", "Cyan", "Gray", "Yellow", "LightGreen"};
    uint8_t color_values[] = {TXT_WHITE, TXT_GREEN, TXT_RED, TXT_BLUE, TXT_MAGENTA, TXT_CYAN, TXT_LGRAY, TXT_YELLOW, TXT_LGREEN};
    int color_count = 9;
    
    while(running) {
        if(need_redraw) {
            clear_screen();
            
            draw_frame(1, 0, 78, 23, BLUE, TXT_WHITE);
            kprint_at("Wnka TXT Editor", 33, 0, (BLUE << 4) | TXT_YELLOW);
            kprint_at("File: ", 2, 0, (BLUE << 4) | TXT_CYAN);
            kprint_at(filename, 8, 0, (BLUE << 4) | TXT_WHITE);
            
            kprint_at("Line: ", 55, 0, (BLUE << 4) | TXT_CYAN);
            kprint_int_at(cursor_line + 1, 61, 0, (BLUE << 4) | TXT_WHITE);
            kprint_at("Col: ", 66, 0, (BLUE << 4) | TXT_CYAN);
            kprint_int_at(cursor_col + 1, 70, 0, (BLUE << 4) | TXT_WHITE);
            
            if(insert_mode) {
                kprint_at("[INS]", 74, 0, (BLUE << 4) | TXT_GREEN);
            } else {
                kprint_at("[OVR]", 74, 0, (BLUE << 4) | TXT_RED);
            }
            
            kprint_at("[F1]Save [F2]Exit [INS]Mode [Ctrl+M]Menu [Ctrl+C]Color", 16, 23, (BLACK << 4) | TXT_YELLOW);
            
            for(int i = 0; i < 22 && scroll + i < line_count; i++) {
                int line_idx = scroll + i;
                int y = 2 + i;
                for(int x = 0; lines[line_idx][x] && x < 79; x++) {
                    char s[2] = {lines[line_idx][x], '\0'};
                    kprint_at(s, 2 + x, y, line_colors[line_idx][x]);
                }
            }
            
            if(menu_active) {
                draw_frame(30, 8, 20, 10, BLACK, TXT_WHITE);
                kprint_at("=== MENU ===", 34, 9, (BLACK << 4) | TXT_YELLOW);
                const char* menu_items[] = {"Save", "Save As...", "Help", "Exit"};
                for(int i = 0; i < 4; i++) {
                    uint8_t color = (i == menu_selection) ? TXT_GREEN : TXT_WHITE;
                    kprint_at(menu_items[i], 34, 11 + i, (BLACK << 4) | color);
                }
                kprint_at("U/D: move, Enter: select, ESC: close", 25, 22, (BLACK << 4) | TXT_CYAN);
            }
            
            if(color_menu_active) {
                draw_frame(30, 5, 25, 12, BLACK, TXT_WHITE);
                kprint_at("=== SELECT COLOR ===", 32, 6, (BLACK << 4) | TXT_YELLOW);
                for(int i = 0; i < color_count; i++) {
                    uint8_t color = (i == color_selection) ? color_values[i] : TXT_WHITE;
                    kprint_at(color_names[i], 34, 8 + i, (BLACK << 4) | color);
                }
                kprint_at("U/D: move, Enter: select, ESC: close", 25, 22, (BLACK << 4) | TXT_CYAN);
            }
            
            need_redraw = 0;
        }
        
        int cursor_y = 2 + cursor_line - scroll;
        if(cursor_y >= 2 && cursor_y < 24 && !menu_active && !color_menu_active) {
            uint16_t* video = (uint16_t*)0xB8000;
            int pos = cursor_y * 80 + 2 + cursor_col;
            
            if(insert_mode) {
                video[pos] = (video[pos] & 0xFF00) | '_';
            } else {
                video[pos] = (video[pos] & 0xFF00) | 0x70;
            }
        }
        
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            
            if(sc == 0x1D) ctrl_pressed = 1;
            else if(sc == 0x9D) ctrl_pressed = 0;
            else if(sc == 0x2A || sc == 0x36) shift_pressed_local = 1;
            else if(sc == 0xAA || sc == 0xB6) shift_pressed_local = 0;
            
            if(sc == 0x01) {
                if(menu_active) {
                    menu_active = 0;
                    need_redraw = 1;
                } else if(color_menu_active) {
                    color_menu_active = 0;
                    need_redraw = 1;
                } else {
                    running = 0;
                }
                while(inb(0x64) & 1) inb(0x60);
                continue;
            }
            
            if(menu_active) {
                if((sc == 0x48 || sc == 0x16) && menu_selection > 0) {
                    menu_selection--;
                    need_redraw = 1;
                }
                else if((sc == 0x50 || sc == 0x20) && menu_selection < 3) {
                    menu_selection++;
                    need_redraw = 1;
                }
                else if(sc == 0x1C) {
                    if(menu_selection == 0) {
                        int new_fd = ramfs_open(filename, 1);
                        if(new_fd >= 0) {
                            char save_buffer[4096];
                            int save_pos = 0;
                            for(int l = 0; l < line_count; l++) {
                                for(int c = 0; lines[l][c] && save_pos < 4095; c++) {
                                    save_buffer[save_pos++] = lines[l][c];
                                }
                                if(save_pos < 4095 && l < line_count - 1) {
                                    save_buffer[save_pos++] = '\n';
                                }
                            }
                            ramfs_write(new_fd, save_buffer, save_pos);
                            ramfs_close(new_fd);
                        }
                        menu_active = 0;
                        need_redraw = 1;
                    }
                    else if(menu_selection == 1) {
                        menu_active = 0;
                        need_redraw = 1;
                        kprint_at("Save As... not implemented", 28, 20, (BLACK << 4) | TXT_YELLOW);
                        for(volatile int d = 0; d < 20000000; d++);
                        need_redraw = 1;
                    }
                    else if(menu_selection == 2) {
                        menu_active = 0;
                        need_redraw = 1;
                        clear_screen();
                        kprint_color("=== HELP ===\n", TXT_CYAN);
                        kprint_color("F1 - Save file\n", TXT_WHITE);
                        kprint_color("F2 - Exit\n", TXT_WHITE);
                        kprint_color("INS - Insert/Overwrite mode\n", TXT_WHITE);
                        kprint_color("Ctrl+M - Open menu\n", TXT_WHITE);
                        kprint_color("Ctrl+C - Open color menu\n", TXT_WHITE);
                        kprint_color("Arrows - Move\n", TXT_WHITE);
                        kprint_color("Backspace - Delete\n", TXT_WHITE);
                        kprint_color("Enter - New line\n", TXT_WHITE);
                        kprint_color("\nPress any key...\n", TXT_YELLOW);
                        while(!(inb(0x64) & 1));
                        while(inb(0x64) & 1) inb(0x60);
                        need_redraw = 1;
                    }
                    else if(menu_selection == 3) {
                        running = 0;
                    }
                }
                while(inb(0x64) & 1) inb(0x60);
                continue;
            }
            
            if(color_menu_active) {
                if((sc == 0x48 || sc == 0x16) && color_selection > 0) {
                    color_selection--;
                    need_redraw = 1;
                }
                else if((sc == 0x50 || sc == 0x20) && color_selection < color_count - 1) {
                    color_selection++;
                    need_redraw = 1;
                }
                else if(sc == 0x1C) {
                    current_color = color_values[color_selection];
                    color_menu_active = 0;
                    if(lines[cursor_line][cursor_col] != '\0') {
                        line_colors[cursor_line][cursor_col] = current_color;
                    }
                    need_redraw = 1;
                }
                while(inb(0x64) & 1) inb(0x60);
                continue;
            }
            
            if(ctrl_pressed && sc == 0x32) {
                menu_active = 1;
                menu_selection = 0;
                need_redraw = 1;
            }
            else if(ctrl_pressed && sc == 0x2E) {
                color_menu_active = 1;
                color_selection = 0;
                need_redraw = 1;
            }
            else if(sc == 0x3B) {
                int new_fd = ramfs_open(filename, 1);
                if(new_fd >= 0) {
                    char save_buffer[4096];
                    int save_pos = 0;
                    for(int l = 0; l < line_count; l++) {
                        for(int c = 0; lines[l][c] && save_pos < 4095; c++) {
                            save_buffer[save_pos++] = lines[l][c];
                        }
                        if(save_pos < 4095 && l < line_count - 1) {
                            save_buffer[save_pos++] = '\n';
                        }
                    }
                    ramfs_write(new_fd, save_buffer, save_pos);
                    ramfs_close(new_fd);
                    need_redraw = 1;
                }
            }
            else if(sc == 0x3C) {
                running = 0;
            }
            else if(sc == 0x52) {
                insert_mode = !insert_mode;
                need_redraw = 1;
            }
            else if(sc == 0x48 && cursor_line > 0) {
                cursor_line--;
                if(cursor_line < scroll) scroll = cursor_line;
                int len = my_strlen(lines[cursor_line]);
                if(cursor_col > len) cursor_col = len;
                need_redraw = 1;
            }
            else if(sc == 0x50 && cursor_line < line_count - 1) {
                cursor_line++;
                if(cursor_line >= scroll + 22) scroll = cursor_line - 21;
                int len = my_strlen(lines[cursor_line]);
                if(cursor_col > len) cursor_col = len;
                need_redraw = 1;
            }
            else if(sc == 0x4B && cursor_col > 0) {
                cursor_col--;
                need_redraw = 1;
            }
            else if(sc == 0x4D) {
                int len = my_strlen(lines[cursor_line]);
                if(cursor_col < len) cursor_col++;
                need_redraw = 1;
            }
            else if(sc == 0x0E && cursor_col > 0) {
                int len = my_strlen(lines[cursor_line]);
                for(int i = cursor_col - 1; i < len; i++) {
                    lines[cursor_line][i] = lines[cursor_line][i+1];
                    line_colors[cursor_line][i] = line_colors[cursor_line][i+1];
                }
                cursor_col--;
                need_redraw = 1;
            }
            else if(sc == 0x0E && cursor_col == 0 && cursor_line > 0) {
                int prev_len = my_strlen(lines[cursor_line - 1]);
                int curr_len = my_strlen(lines[cursor_line]);
                for(int i = 0; i <= curr_len; i++) {
                    lines[cursor_line - 1][prev_len + i] = lines[cursor_line][i];
                    line_colors[cursor_line - 1][prev_len + i] = line_colors[cursor_line][i];
                }
                for(int i = cursor_line; i < line_count - 1; i++) {
                    my_strcpy(lines[i], lines[i+1]);
                    for(int j = 0; j < 80; j++) line_colors[i][j] = line_colors[i+1][j];
                }
                line_count--;
                cursor_line--;
                cursor_col = prev_len;
                if(cursor_line < scroll) scroll = cursor_line;
                need_redraw = 1;
            }
            else if(sc == 0x1C) {
                if(line_count < 100) {
                    for(int i = line_count; i > cursor_line + 1; i--) {
                        my_strcpy(lines[i], lines[i-1]);
                        for(int j = 0; j < 80; j++) line_colors[i][j] = line_colors[i-1][j];
                    }
                    char temp[80];
                    my_strcpy(temp, lines[cursor_line] + cursor_col);
                    lines[cursor_line][cursor_col] = '\0';
                    my_strcpy(lines[cursor_line + 1], temp);
                    for(int j = cursor_col; j < 80; j++) {
                        line_colors[cursor_line + 1][j - cursor_col] = line_colors[cursor_line][j];
                    }
                    line_count++;
                    cursor_line++;
                    cursor_col = 0;
                    if(cursor_line >= scroll + 22) scroll = cursor_line - 21;
                    need_redraw = 1;
                }
            }
            else if(sc >= 0x02 && sc <= 0x0D) {
                const char* lower = "1234567890-=";
                const char* upper = "!@#$%^&*()_+";
                char ch = shift_pressed_local ? upper[sc - 0x02] : lower[sc - 0x02];
                int len = my_strlen(lines[cursor_line]);
                
                if(insert_mode) {
                    if(len < 79) {
                        for(int i = len; i >= cursor_col; i--) {
                            lines[cursor_line][i+1] = lines[cursor_line][i];
                            line_colors[cursor_line][i+1] = line_colors[cursor_line][i];
                        }
                        lines[cursor_line][cursor_col] = ch;
                        line_colors[cursor_line][cursor_col] = current_color;
                        cursor_col++;
                        need_redraw = 1;
                    }
                } else {
                    if(cursor_col < 79) {
                        lines[cursor_line][cursor_col] = ch;
                        line_colors[cursor_line][cursor_col] = current_color;
                        cursor_col++;
                        if(cursor_col > len) lines[cursor_line][cursor_col] = '\0';
                        need_redraw = 1;
                    }
                }
            }
            else if(sc >= 0x10 && sc <= 0x19) {
                const char* lower = "qwertyuiop";
                const char* upper = "QWERTYUIOP";
                char ch = shift_pressed_local ? upper[sc - 0x10] : lower[sc - 0x10];
                int len = my_strlen(lines[cursor_line]);
                
                if(insert_mode) {
                    if(len < 79) {
                        for(int i = len; i >= cursor_col; i--) {
                            lines[cursor_line][i+1] = lines[cursor_line][i];
                            line_colors[cursor_line][i+1] = line_colors[cursor_line][i];
                        }
                        lines[cursor_line][cursor_col] = ch;
                        line_colors[cursor_line][cursor_col] = current_color;
                        cursor_col++;
                        need_redraw = 1;
                    }
                } else {
                    if(cursor_col < 79) {
                        lines[cursor_line][cursor_col] = ch;
                        line_colors[cursor_line][cursor_col] = current_color;
                        cursor_col++;
                        if(cursor_col > len) lines[cursor_line][cursor_col] = '\0';
                        need_redraw = 1;
                    }
                }
            }
            else if(sc >= 0x1E && sc <= 0x26) {
                const char* lower = "asdfghjkl";
                const char* upper = "ASDFGHJKL";
                char ch = shift_pressed_local ? upper[sc - 0x1E] : lower[sc - 0x1E];
                int len = my_strlen(lines[cursor_line]);
                
                if(insert_mode) {
                    if(len < 79) {
                        for(int i = len; i >= cursor_col; i--) {
                            lines[cursor_line][i+1] = lines[cursor_line][i];
                            line_colors[cursor_line][i+1] = line_colors[cursor_line][i];
                        }
                        lines[cursor_line][cursor_col] = ch;
                        line_colors[cursor_line][cursor_col] = current_color;
                        cursor_col++;
                        need_redraw = 1;
                    }
                } else {
                    if(cursor_col < 79) {
                        lines[cursor_line][cursor_col] = ch;
                        line_colors[cursor_line][cursor_col] = current_color;
                        cursor_col++;
                        if(cursor_col > len) lines[cursor_line][cursor_col] = '\0';
                        need_redraw = 1;
                    }
                }
            }
            else if(sc >= 0x2C && sc <= 0x32) {
                const char* lower = "zxcvbnm";
                const char* upper = "ZXCVBNM";
                char ch = shift_pressed_local ? upper[sc - 0x2C] : lower[sc - 0x2C];
                int len = my_strlen(lines[cursor_line]);
                
                if(insert_mode) {
                    if(len < 79) {
                        for(int i = len; i >= cursor_col; i--) {
                            lines[cursor_line][i+1] = lines[cursor_line][i];
                            line_colors[cursor_line][i+1] = line_colors[cursor_line][i];
                        }
                        lines[cursor_line][cursor_col] = ch;
                        line_colors[cursor_line][cursor_col] = current_color;
                        cursor_col++;
                        need_redraw = 1;
                    }
                } else {
                    if(cursor_col < 79) {
                        lines[cursor_line][cursor_col] = ch;
                        line_colors[cursor_line][cursor_col] = current_color;
                        cursor_col++;
                        if(cursor_col > len) lines[cursor_line][cursor_col] = '\0';
                        need_redraw = 1;
                    }
                }
            }
            else if(sc == 0x39) {
                char ch = ' ';
                int len = my_strlen(lines[cursor_line]);
                
                if(insert_mode) {
                    if(len < 79) {
                        for(int i = len; i >= cursor_col; i--) {
                            lines[cursor_line][i+1] = lines[cursor_line][i];
                            line_colors[cursor_line][i+1] = line_colors[cursor_line][i];
                        }
                        lines[cursor_line][cursor_col] = ch;
                        line_colors[cursor_line][cursor_col] = current_color;
                        cursor_col++;
                        need_redraw = 1;
                    }
                } else {
                    if(cursor_col < 79) {
                        lines[cursor_line][cursor_col] = ch;
                        line_colors[cursor_line][cursor_col] = current_color;
                        cursor_col++;
                        if(cursor_col > len) lines[cursor_line][cursor_col] = '\0';
                        need_redraw = 1;
                    }
                }
            }
            else if(sc == 0x0C && shift_pressed_local) {
                char ch = '_';
                int len = my_strlen(lines[cursor_line]);
                
                if(insert_mode) {
                    if(len < 79) {
                        for(int i = len; i >= cursor_col; i--) {
                            lines[cursor_line][i+1] = lines[cursor_line][i];
                            line_colors[cursor_line][i+1] = line_colors[cursor_line][i];
                        }
                        lines[cursor_line][cursor_col] = ch;
                        line_colors[cursor_line][cursor_col] = current_color;
                        cursor_col++;
                        need_redraw = 1;
                    }
                } else {
                    if(cursor_col < 79) {
                        lines[cursor_line][cursor_col] = ch;
                        line_colors[cursor_line][cursor_col] = current_color;
                        cursor_col++;
                        if(cursor_col > len) lines[cursor_line][cursor_col] = '\0';
                        need_redraw = 1;
                    }
                }
            }
            
            while(inb(0x64) & 1) inb(0x60);
        }
        
        for(volatile int i = 0; i < 1000; i++);
    }
    
    clear_screen();
    kprint_color("[EDITOR] Exited\n", TXT_GREEN);
    
    while(inb(0x64) & 1) inb(0x60);
    
    shift_pressed = false;
    ctrl_pressed = 0;
    input_ptr = 0;
    input_buffer[0] = '\0';
    
    cursor_x = saved_cursor_x;
    cursor_y = saved_cursor_y;
    update_cursor(cursor_x, cursor_y);

    clear_screen();
}
else if(my_strcmp(cmd_copy, "slides") == 0) {
    char filename[256] = {0};
    
    if(arg[0] != '\0') {
        int i;
        for(i = 0; arg[i] && i < 255; i++) {
            filename[i] = arg[i];
        }
        filename[i] = '\0';
    } else {
        my_strcpy(filename, "presentation.sld");
    }

    char slides[20][50][80];
    char slide_titles[20][40];
    int slide_count = 1;
    int current_slide = 0;
    uint8_t slide_bg[20];
    
    for(int i = 0; i < 20; i++) {
        slide_titles[i][0] = '\0';
        slide_bg[i] = BLUE;
        for(int j = 0; j < 50; j++) {
            slides[i][j][0] = '\0';
        }
    }
    
    my_strcpy(slide_titles[0], "Welcome to WNKA");
    my_strcpy(slides[0][0], "Presentation Software");
    my_strcpy(slides[0][1], "for WNKA OS");
    my_strcpy(slides[0][2], "");
    my_strcpy(slides[0][3], "Press F5 to start slideshow");
    my_strcpy(slides[0][4], "Press F1 to edit");
    
    int fd = ramfs_open(filename, 0);
    if(fd >= 0) {
        char buffer[16384];
        int bytes = ramfs_read(fd, (uint8_t*)buffer, 16383);
        if(bytes > 0) {
            buffer[bytes] = '\0';
            int slide = 0, line = 0, pos = 0;
            int in_title = 1;
            
            while(buffer[pos] && slide < 20) {
                if(buffer[pos] == '|' && buffer[pos+1] == '|') {
                    slide++;
                    line = 0;
                    in_title = 1;
                    pos += 2;
                    continue;
                }
                else if(buffer[pos] == '\n') {
                    line++;
                    pos++;
                    continue;
                }
                else if(in_title && buffer[pos] != '|') {
                    int i = 0;
                    while(buffer[pos] && buffer[pos] != '\n' && i < 39) {
                        slide_titles[slide][i++] = buffer[pos++];
                    }
                    slide_titles[slide][i] = '\0';
                    in_title = 0;
                }
                else if(!in_title && line < 50) {
                    int i = 0;
                    while(buffer[pos] && buffer[pos] != '\n' && i < 79) {
                        slides[slide][line][i++] = buffer[pos++];
                    }
                    slides[slide][line][i] = '\0';
                    line++;
                }
                pos++;
            }
            slide_count = slide + 1;
        }
        ramfs_close(fd);
    }
    
    int editing = 1;
    int cursor_line = 0;
    int cursor_col = 0;
    int scroll = 0;
    int need_redraw = 1;
    int selected_bg = 0;
    int shift_pressed_local = 0;
    
    const char* bg_names[] = {"Blue", "Black", "White", "Red", "Green", "Cyan"};
    uint8_t bg_colors[] = {BLUE, BLACK, WHITE, RED, GREEN, CYAN};
    int bg_count = 6;
    
    int saved_cursor_x = cursor_x;
    int saved_cursor_y = cursor_y;
    
    while(editing) {
        if(need_redraw) {
            clear_screen_bg(slide_bg[current_slide]);
            
            draw_dframe(1, 0, 78, 24, slide_bg[current_slide], TXT_WHITE);
            kprint_at("SLIDE EDITOR", 30, 0, (slide_bg[current_slide] << 4) | TXT_YELLOW);
            kprint_at("File: ", 2, 0, (slide_bg[current_slide] << 4) | TXT_CYAN);
            kprint_at(filename, 8, 0, (slide_bg[current_slide] << 4) | TXT_WHITE);
            
            kprint_at("Slide: ", 55, 0, (slide_bg[current_slide] << 4) | TXT_CYAN);
            kprint_int_at(current_slide + 1, 62, 0, (slide_bg[current_slide] << 4) | TXT_GREEN);
            kprint_at("/", 64, 0, (slide_bg[current_slide] << 4) | TXT_WHITE);
            kprint_int_at(slide_count, 66, 0, (slide_bg[current_slide] << 4) | TXT_GREEN);
            
            draw_frame(2, 1, 76, 2, slide_bg[current_slide], TXT_WHITE);
            kprint_at(slide_titles[current_slide], 4, 2, (slide_bg[current_slide] << 4) | TXT_YELLOW);
            
            for(int i = 0; i < 18 && scroll + i < 50; i++) {
                int y = 4 + i;
                if(slides[current_slide][scroll + i][0] != '\0') {
                    kprint_at(slides[current_slide][scroll + i], 4, y, (slide_bg[current_slide] << 4) | TXT_WHITE);
                }
            }
            
            draw_hline(1, 22, 78, slide_bg[current_slide], TXT_WHITE, S_HLINE);
            kprint_at("[F1]Save [F2]Exit [F3]New [F4]Del [F5]Play [F6]BG [F7]Slide", 10, 23, (slide_bg[current_slide] << 4) | TXT_YELLOW);
            kprint_at("Shift+Tab - Jump to top/bottom", 20, 24, (slide_bg[current_slide] << 4) | TXT_CYAN);
            
            need_redraw = 0;
        }
        
        int cursor_y = 4 + cursor_line - scroll;
        if(cursor_y >= 4 && cursor_y < 22) {
            uint16_t* video = (uint16_t*)0xB8000;
            int pos = cursor_y * 80 + 4 + cursor_col;
            video[pos] = (video[pos] & 0xFF00) | 0x70;
        }
        
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            
            if(sc == 0x2A || sc == 0x36) {
                shift_pressed_local = 1;
            }
            else if(sc == 0xAA || sc == 0xB6) {
                shift_pressed_local = 0;
            }
            
            if(sc < 0x80) {
                if(sc == 0x0F && shift_pressed_local) {
                    cursor_line = 0;
                    cursor_col = 0;
                    scroll = 0;
                    need_redraw = 1;
                }
                else if(sc == 0x0F) {
                    int last_line = 0;
                    for(int i = 49; i >= 0; i--) {
                        if(slides[current_slide][i][0] != '\0') {
                            last_line = i;
                            break;
                        }
                    }
                    cursor_line = last_line;
                    cursor_col = my_strlen(slides[current_slide][last_line]);
                    if(cursor_line >= scroll + 18) scroll = cursor_line - 17;
                    need_redraw = 1;
                }
                else if(sc == 0x3B) {
                    int new_fd = ramfs_open(filename, 1);
                    if(new_fd >= 0) {
                        char save_buffer[16384];
                        int save_pos = 0;
                        for(int s = 0; s < slide_count; s++) {
                            for(int i = 0; slide_titles[s][i] && save_pos < 16383; i++) {
                                save_buffer[save_pos++] = slide_titles[s][i];
                            }
                            save_buffer[save_pos++] = '\n';
                            for(int l = 0; l < 50 && slides[s][l][0] && save_pos < 16383; l++) {
                                for(int c = 0; slides[s][l][c] && save_pos < 16383; c++) {
                                    save_buffer[save_pos++] = slides[s][l][c];
                                }
                                save_buffer[save_pos++] = '\n';
                            }
                            if(s < slide_count - 1 && save_pos < 16383) {
                                save_buffer[save_pos++] = '|';
                                save_buffer[save_pos++] = '|';
                                save_buffer[save_pos++] = '\n';
                            }
                        }
                        ramfs_write(new_fd, (uint8_t*)save_buffer, save_pos);
                        ramfs_close(new_fd);
                        need_redraw = 1;
                    }
                }
                else if(sc == 0x3C) {
                    editing = 0;
                }
                else if(sc == 0x3D) {
                    if(slide_count < 20) {
                        for(int i = slide_count; i > current_slide + 1; i--) {
                            my_strcpy(slide_titles[i], slide_titles[i-1]);
                            slide_bg[i] = slide_bg[i-1];
                            for(int l = 0; l < 50; l++) {
                                my_strcpy(slides[i][l], slides[i-1][l]);
                            }
                        }
                        slide_titles[current_slide + 1][0] = '\0';
                        slide_bg[current_slide + 1] = BLUE;
                        for(int l = 0; l < 50; l++) {
                            slides[current_slide + 1][l][0] = '\0';
                        }
                        slide_count++;
                        need_redraw = 1;
                    }
                }
                else if(sc == 0x3E) {
                    if(slide_count > 1) {
                        for(int i = current_slide; i < slide_count - 1; i++) {
                            my_strcpy(slide_titles[i], slide_titles[i+1]);
                            slide_bg[i] = slide_bg[i+1];
                            for(int l = 0; l < 50; l++) {
                                my_strcpy(slides[i][l], slides[i+1][l]);
                            }
                        }
                        slide_count--;
                        if(current_slide >= slide_count) current_slide = slide_count - 1;
                        need_redraw = 1;
                    }
                }
                else if(sc == 0x3F) {
                    int play_slide = 0;
                    int play_running = 1;
                    while(play_running && play_slide < slide_count) {
                        clear_screen_bg(slide_bg[play_slide]);
                        
                        kprint_at(slide_titles[play_slide], 40 - my_strlen(slide_titles[play_slide])/2, 5, (slide_bg[play_slide] << 4) | TXT_YELLOW);
                        
                        for(int l = 0; l < 50 && slides[play_slide][l][0]; l++) {
                            kprint_at(slides[play_slide][l], 20, 10 + l, (slide_bg[play_slide] << 4) | TXT_WHITE);
                        }
                        
                        kprint_at("Press any key for next slide...", 25, 22, (slide_bg[play_slide] << 4) | TXT_CYAN);
                        
                        while(1) {
                            if(inb(0x64) & 1) {
                                uint8_t k = inb(0x60);
                                if(k < 0x80) {
                                    if(k == 0x01) {
                                        play_running = 0;
                                        break;
                                    }
                                    break;
                                }
                            }
                        }
                        play_slide++;
                    }
                    need_redraw = 1;
                }
                else if(sc == 0x40) {
                    selected_bg = (selected_bg + 1) % bg_count;
                    slide_bg[current_slide] = bg_colors[selected_bg];
                    need_redraw = 1;
                }
                else if(sc == 0x41) {
                    int menu_selection = current_slide;
                    int menu_running = 1;
                    int old_selection = menu_selection;
                    
                    draw_frame(25, 5, 30, 12, BLACK, TXT_WHITE);
                    kprint_at("=== SLIDE MENU ===", 33, 7, (BLACK << 4) | TXT_YELLOW);
                    
                    for(int i = 0; i < slide_count && i < 8; i++) {
                        char num[4];
                        if(i+1 < 10) {
                            num[0] = (i+1) + '0';
                            num[1] = ':';
                            num[2] = ' ';
                            num[3] = '\0';
                        } else {
                            num[0] = ((i+1)/10) + '0';
                            num[1] = ((i+1)%10) + '0';
                            num[2] = ':';
                            num[3] = ' ';
                            num[4] = '\0';
                        }
                        
                        char short_title[20];
                        int len = my_strlen(slide_titles[i]);
                        if(len > 18) len = 18;
                        for(int j = 0; j < len; j++) short_title[j] = slide_titles[i][j];
                        short_title[len] = '\0';
                        
                        if(i == menu_selection) {
                            kprint_at(num, 29, 9 + i, (BLACK << 4) | TXT_GREEN);
                            kprint_at(short_title, 34, 9 + i, (BLACK << 4) | TXT_GREEN);
                        } else {
                            kprint_at(num, 29, 9 + i, (BLACK << 4) | TXT_WHITE);
                            kprint_at(short_title, 34, 9 + i, (BLACK << 4) | TXT_WHITE);
                        }
                    }
                    
                    kprint_at("U/D: move, Enter: select, ESC: close", 22, 18, (BLACK << 4) | TXT_CYAN);
                    
                    while(menu_running) {
                        if(inb(0x64) & 1) {
                            uint8_t k = inb(0x60);
                            if(k < 0x80) {
                                if(k == 0x48 && menu_selection > 0) {
                                    old_selection = menu_selection;
                                    menu_selection--;
                                    
                                    char old_num[4];
                                    if(old_selection+1 < 10) {
                                        old_num[0] = (old_selection+1) + '0';
                                        old_num[1] = ':';
                                        old_num[2] = ' ';
                                        old_num[3] = '\0';
                                    } else {
                                        old_num[0] = ((old_selection+1)/10) + '0';
                                        old_num[1] = ((old_selection+1)%10) + '0';
                                        old_num[2] = ':';
                                        old_num[3] = ' ';
                                        old_num[4] = '\0';
                                    }
                                    char old_title[20];
                                    int old_len = my_strlen(slide_titles[old_selection]);
                                    if(old_len > 18) old_len = 18;
                                    for(int j = 0; j < old_len; j++) old_title[j] = slide_titles[old_selection][j];
                                    old_title[old_len] = '\0';
                                    kprint_at(old_num, 29, 9 + old_selection, (BLACK << 4) | TXT_WHITE);
                                    kprint_at(old_title, 34, 9 + old_selection, (BLACK << 4) | TXT_WHITE);
                                    
                                    char new_num[4];
                                    if(menu_selection+1 < 10) {
                                        new_num[0] = (menu_selection+1) + '0';
                                        new_num[1] = ':';
                                        new_num[2] = ' ';
                                        new_num[3] = '\0';
                                    } else {
                                        new_num[0] = ((menu_selection+1)/10) + '0';
                                        new_num[1] = ((menu_selection+1)%10) + '0';
                                        new_num[2] = ':';
                                        new_num[3] = ' ';
                                        new_num[4] = '\0';
                                    }
                                    char new_title[20];
                                    int new_len = my_strlen(slide_titles[menu_selection]);
                                    if(new_len > 18) new_len = 18;
                                    for(int j = 0; j < new_len; j++) new_title[j] = slide_titles[menu_selection][j];
                                    new_title[new_len] = '\0';
                                    kprint_at(new_num, 29, 9 + menu_selection, (BLACK << 4) | TXT_GREEN);
                                    kprint_at(new_title, 34, 9 + menu_selection, (BLACK << 4) | TXT_GREEN);
                                }
                                else if(k == 0x50 && menu_selection < slide_count - 1) {
                                    old_selection = menu_selection;
                                    menu_selection++;
                                    
                                    char old_num[4];
                                    if(old_selection+1 < 10) {
                                        old_num[0] = (old_selection+1) + '0';
                                        old_num[1] = ':';
                                        old_num[2] = ' ';
                                        old_num[3] = '\0';
                                    } else {
                                        old_num[0] = ((old_selection+1)/10) + '0';
                                        old_num[1] = ((old_selection+1)%10) + '0';
                                        old_num[2] = ':';
                                        old_num[3] = ' ';
                                        old_num[4] = '\0';
                                    }
                                    char old_title[20];
                                    int old_len = my_strlen(slide_titles[old_selection]);
                                    if(old_len > 18) old_len = 18;
                                    for(int j = 0; j < old_len; j++) old_title[j] = slide_titles[old_selection][j];
                                    old_title[old_len] = '\0';
                                    kprint_at(old_num, 29, 9 + old_selection, (BLACK << 4) | TXT_WHITE);
                                    kprint_at(old_title, 34, 9 + old_selection, (BLACK << 4) | TXT_WHITE);
                                    
                                    char new_num[4];
                                    if(menu_selection+1 < 10) {
                                        new_num[0] = (menu_selection+1) + '0';
                                        new_num[1] = ':';
                                        new_num[2] = ' ';
                                        new_num[3] = '\0';
                                    } else {
                                        new_num[0] = ((menu_selection+1)/10) + '0';
                                        new_num[1] = ((menu_selection+1)%10) + '0';
                                        new_num[2] = ':';
                                        new_num[3] = ' ';
                                        new_num[4] = '\0';
                                    }
                                    char new_title[20];
                                    int new_len = my_strlen(slide_titles[menu_selection]);
                                    if(new_len > 18) new_len = 18;
                                    for(int j = 0; j < new_len; j++) new_title[j] = slide_titles[menu_selection][j];
                                    new_title[new_len] = '\0';
                                    kprint_at(new_num, 29, 9 + menu_selection, (BLACK << 4) | TXT_GREEN);
                                    kprint_at(new_title, 34, 9 + menu_selection, (BLACK << 4) | TXT_GREEN);
                                }
                                else if(k == 0x1C) {
                                    current_slide = menu_selection;
                                    menu_running = 0;
                                    need_redraw = 1;
                                }
                                else if(k == 0x01) {
                                    menu_running = 0;
                                    need_redraw = 1;
                                }
                            }
                            while(inb(0x64) & 1) inb(0x60);
                        }
                    }
                }
                else if(sc == 0x48 && cursor_line > 0) {
                    cursor_line--;
                    if(cursor_line < scroll) scroll = cursor_line;
                    need_redraw = 1;
                }
                else if(sc == 0x50 && cursor_line < 49) {
                    cursor_line++;
                    if(cursor_line >= scroll + 18) scroll = cursor_line - 17;
                    need_redraw = 1;
                }
                else if(sc == 0x4B && cursor_col > 0) {
                    cursor_col--;
                    need_redraw = 1;
                }
                else if(sc == 0x4D) {
                    int len = my_strlen(slides[current_slide][cursor_line]);
                    if(cursor_col < len) cursor_col++;
                    need_redraw = 1;
                }
                else if(sc == 0x0E && cursor_col > 0) {
                    int len = my_strlen(slides[current_slide][cursor_line]);
                    for(int i = cursor_col - 1; i < len; i++) {
                        slides[current_slide][cursor_line][i] = slides[current_slide][cursor_line][i+1];
                    }
                    cursor_col--;
                    need_redraw = 1;
                }
                else if(sc == 0x1C) {
                    if(cursor_line < 49) {
                        for(int i = 49; i > cursor_line; i--) {
                            my_strcpy(slides[current_slide][i], slides[current_slide][i-1]);
                        }
                        slides[current_slide][cursor_line + 1][0] = '\0';
                        cursor_line++;
                        need_redraw = 1;
                    }
                }
                else if(sc >= 0x02 && sc <= 0x0D) {
                    const char* chars = "1234567890-=";
                    char ch = chars[sc - 0x02];
                    int len = my_strlen(slides[current_slide][cursor_line]);
                    if(len < 79) {
                        for(int i = len; i >= cursor_col; i--) {
                            slides[current_slide][cursor_line][i+1] = slides[current_slide][cursor_line][i];
                        }
                        slides[current_slide][cursor_line][cursor_col] = ch;
                        cursor_col++;
                        need_redraw = 1;
                    }
                }
                else if(sc >= 0x10 && sc <= 0x19) {
                    const char* chars = "qwertyuiop";
                    char ch = chars[sc - 0x10];
                    int len = my_strlen(slides[current_slide][cursor_line]);
                    if(len < 79) {
                        for(int i = len; i >= cursor_col; i--) {
                            slides[current_slide][cursor_line][i+1] = slides[current_slide][cursor_line][i];
                        }
                        slides[current_slide][cursor_line][cursor_col] = ch;
                        cursor_col++;
                        need_redraw = 1;
                    }
                }
                else if(sc >= 0x1E && sc <= 0x26) {
                    const char* chars = "asdfghjkl";
                    char ch = chars[sc - 0x1E];
                    int len = my_strlen(slides[current_slide][cursor_line]);
                    if(len < 79) {
                        for(int i = len; i >= cursor_col; i--) {
                            slides[current_slide][cursor_line][i+1] = slides[current_slide][cursor_line][i];
                        }
                        slides[current_slide][cursor_line][cursor_col] = ch;
                        cursor_col++;
                        need_redraw = 1;
                    }
                }
                else if(sc >= 0x2C && sc <= 0x32) {
                    const char* chars = "zxcvbnm";
                    char ch = chars[sc - 0x2C];
                    int len = my_strlen(slides[current_slide][cursor_line]);
                    if(len < 79) {
                        for(int i = len; i >= cursor_col; i--) {
                            slides[current_slide][cursor_line][i+1] = slides[current_slide][cursor_line][i];
                        }
                        slides[current_slide][cursor_line][cursor_col] = ch;
                        cursor_col++;
                        need_redraw = 1;
                    }
                }
                else if(sc == 0x39) {
                    int len = my_strlen(slides[current_slide][cursor_line]);
                    if(len < 79) {
                        for(int i = len; i >= cursor_col; i--) {
                            slides[current_slide][cursor_line][i+1] = slides[current_slide][cursor_line][i];
                        }
                        slides[current_slide][cursor_line][cursor_col] = ' ';
                        cursor_col++;
                        need_redraw = 1;
                    }
                }
                else if(sc == 0x0C) {
                    int len = my_strlen(slides[current_slide][cursor_line]);
                    if(len < 79) {
                        for(int i = len; i >= cursor_col; i--) {
                            slides[current_slide][cursor_line][i+1] = slides[current_slide][cursor_line][i];
                        }
                        slides[current_slide][cursor_line][cursor_col] = '_';
                        cursor_col++;
                        need_redraw = 1;
                    }
                }
                else if(sc == 0x01) {
                    editing = 0;
                }
            }
            while(inb(0x64) & 1) inb(0x60);
        }
        
        for(volatile int i = 0; i < 1000; i++);
    }
    
    clear_screen();
    kprint_color("[SLIDES] Exited\n", TXT_GREEN);
    
    while(inb(0x64) & 1) inb(0x60);
    shift_pressed = false;
    cursor_x = saved_cursor_x;
    cursor_y = saved_cursor_y;
    update_cursor(cursor_x, cursor_y);
    input_ptr = 0;
    input_buffer[0] = '\0';
    kprint_color("root@wnka> ", TXT_GREEN);
}

    else if (my_strcmp(cmd_copy, "install") == 0) {
    wnk_install();
}
else {
    kprint_color("Unknown command: ", 0x0C);
    kprint(original_cmd);
    play_error_sound();
    kprint("\n");
    
    const char* all_commands[] = {
        "help", "cls", "clear", "reboot", "shut", "halt", "about", "fetch",
        "beep", "sleep", "crashme", "panic", "calc", "galc", "paint",
        "piano", "ui", "gui", "pacman", "flappy", "snake", "matrix", "fire",
        "rain", "stars", "plasma", "waves", "tunnel", "menus", "art", "vga",
        "vesablue", "screensaver", "clock", "timer", "ide", "tcc", "ls", "cd",
        "cat", "touch", "rm", "mkdir", "rmdir", "mv", "cp", "write", "stat",
        "du", "find", "pwd", "tree", "mount", "umount", "df", "fdisk", "mkfs",
        "fsck", "ahci", "megascan", "ls-disk", "check", "menu", "pci", "memtest",
        "install", "netinit", "ping", "dns", "browse", "http", "netstat",
        "ps", "kill", "multitest", "bg", "jobs", "fg", "bgkill", "copy", "paste",
        "key", "secret", "mykey", "echo", "say", "hello", "coin", "dice", "rps",
        "guess", "8ball", "fortune", "cowsay", "linux", "os", "rev", "yes",
        "halt", "power", "theme", "sound", "monitor",
        NULL
    };
    
    const char* best_match = NULL;
    int best_distance = 3;
    
    for(int i = 0; all_commands[i] != NULL; i++) {
        int dist = levenshtein_distance(original_cmd, all_commands[i]);
        if(dist < best_distance) {
            best_distance = dist;
            best_match = all_commands[i];
        }
    }
    
    static int wrong_cmd_count = 0;
    
    if(best_match != NULL && best_distance <= 2) {
        kprint_color("\nDid you mean: ", TXT_YELLOW);
        kprint_color(best_match, TXT_GREEN);
        kprint_color(" ?\n", TXT_YELLOW);
        wrong_cmd_count++;
    } else {
        wrong_cmd_count++;
    }
    
    if(wrong_cmd_count >= 3) {
        kprint_color("\nTip: Type 'help' to see all available commands!\n", TXT_CYAN);
        wrong_cmd_count = 0;
    }
}
    kprint_color("root@wnka> ", TXT_GREEN);
}