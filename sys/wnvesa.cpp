#include "wnvesa.h"
#include "vesa.h"
#include "ata.h"
#include <stdint.h>

static uint16_t backbuffer[1024 * 768];
static uint16_t* vesa_fb_ptr = 0;

static int mx = 512;
static int my = 384;

static int start_menu = 0;
static int power_dialog = 0;
static int settings_open = 0;
static int first_setup = 0;

static int clock_h = 14;
static int clock_m = 45;
static int clock_s = 0;

static int drag_win = -1;
static int drag_off_x = 0;
static int drag_off_y = 0;

static int settings_theme_tab = 0;

static int calc_value = 0;
static int calc_new = 1;
static char calc_op = 0;

static int notepad_lines = 1;
static int notepad_cursor = 0;
static char notepad_text[20][64] = {"Welcome to WNKA Notepad!"};

static int screensaver_active = 0;
static int idle_frames = 0;
static int last_mx = 512;
static int last_my = 384;

static int ss_frame = 0;
static float ss_logo_x = 512.0f;
static float ss_logo_y = 384.0f;
static float ss_logo_dx = 2.5f;
static float ss_logo_dy = 1.8f;

typedef struct {
    char magic[4];
    int theme;
    int wallpaper;
    int taskbar_auto_hide;
    int clock_24h;
    int show_seconds;
    int screensaver_enabled;
    int screensaver_timeout;
    int screensaver_type;
    int animation_enabled;
    char username[32];
    char computer_name[32];
    int first_run;
} vesa_settings_t;

static vesa_settings_t settings;

#define MAX_FILES 64
typedef struct {
    char name[32];
    int is_dir;
    int size;
} file_entry_t;

static file_entry_t current_files[MAX_FILES];
static int current_file_count = 0;
static char current_path[256] = "/";
static int file_selected = 0;

#define SETTINGS_SECTOR 9999

#define WN_BLACK       0x0000
#define WN_WHITE       0xFFFF
#define WN_RED         0xF800
#define WN_GREEN       0x07E0
#define WN_BLUE        0x001F
#define WN_YELLOW      0xFFE0
#define WN_CYAN        0x07FF
#define WN_ORANGE      0xFC00
#define WN_GRAY        0x8410
#define WN_DKGRAY      0x4A69
#define WN_LTGRAY      0xC618
#define WN_SILVER      0xBDF7
#define WN_DARK_BG     0x18E3
#define WN_DARK_WIN    0x2965
#define WN_DARK_TITLE  0x39C7

#define WN_RGB565(r, g, b) ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

static void str_cpy(char* d, const char* s) {
    while (*s) {
        *d = *s;
        d++;
        s++;
    }
    *d = 0;
}

static int str_len(const char* s) {
    int l = 0;
    while (*s++) l++;
    return l;
}

static int str_cmp(const char* a, const char* b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a - *b;
}

static void int_to_str(int n, char* s) {
    if (n == 0) {
        s[0] = '0';
        s[1] = 0;
        return;
    }
    char t[12];
    int i = 0;
    while (n > 0) {
        t[i++] = '0' + (n % 10);
        n /= 10;
    }
    int j = 0;
    while (i > 0) {
        s[j++] = t[--i];
    }
    s[j] = 0;
}

static void save_settings(void) {
    str_cpy(settings.magic, "WNKA");
    settings.first_run = 0;
    
    uint16_t buf[256];
    uint8_t* d = (uint8_t*)&settings;
    
    for (int i = 0; i < 256; i++) {
        if (i * 2 < (int)sizeof(settings)) {
            buf[i] = (d[i * 2] | (d[i * 2 + 1] << 8));
        } else {
            buf[i] = 0;
        }
    }
    
    write_sector(SETTINGS_SECTOR, buf);
}

static void load_settings(void) {
    uint16_t buf[256];
    read_sector(SETTINGS_SECTOR, buf);
    
    uint8_t* d = (uint8_t*)&settings;
    for (int i = 0; i < (int)sizeof(settings); i++) {
        if (i % 2 == 0) {
            d[i] = buf[i / 2] & 0xFF;
        } else {
            d[i] = (buf[i / 2] >> 8) & 0xFF;
        }
    }
    
    if (settings.magic[0] != 'W' || settings.magic[1] != 'N' ||
        settings.magic[2] != 'K' || settings.magic[3] != 'A') {
        
        settings.theme = 0;
        settings.wallpaper = 0;
        settings.taskbar_auto_hide = 0;
        settings.clock_24h = 1;
        settings.show_seconds = 0;
        settings.screensaver_enabled = 1;
        settings.screensaver_timeout = 300;
        settings.screensaver_type = 0;
        settings.animation_enabled = 1;
        settings.first_run = 1;
        
        str_cpy(settings.username, "User");
        str_cpy(settings.computer_name, "WNKA-PC");
        
        save_settings();
    }
}

static void fm_refresh(void) {
    current_file_count = 0;
    file_selected = 0;
    
    uint16_t dir_buf[256];
    uint32_t sector = (current_path[0] == '/' && current_path[1] == 0) ? 100 : 300;
    read_sector(sector, dir_buf);
    
    for (int i = 0; i < 32; i++) {
        char name[12] = {0};
        for (int j = 0; j < 11; j++) {
            name[j] = ((char*)dir_buf)[i * 16 + j];
        }
        
        if (name[0] != 0) {
            str_cpy(current_files[current_file_count].name, name);
            current_files[current_file_count].is_dir = (((char*)dir_buf)[i * 16 + 11] == 1);
            current_files[current_file_count].size = dir_buf[i * 8 + 7];
            current_file_count++;
        }
    }
}

static void fm_create_dir(const char* name) {
    uint16_t dir_buf[256];
    read_sector(100, dir_buf);
    
    int slot = -1;
    for (int i = 0; i < 32; i++) {
        if (((char*)dir_buf)[i * 16] == 0) {
            slot = i;
            break;
        }
    }
    
    if (slot != -1) {
        for (int j = 0; j < 11 && name[j]; j++) {
            ((char*)dir_buf)[slot * 16 + j] = name[j];
        }
        ((char*)dir_buf)[slot * 16 + 11] = 1;
        dir_buf[slot * 8 + 6] = 300 + slot;
        dir_buf[slot * 8 + 7] = 0;
        
        write_sector(100, dir_buf);
        
        uint16_t empty[256];
        for (int i = 0; i < 256; i++) empty[i] = 0;
        write_sector(300 + slot, empty);
        
        fm_refresh();
    }
}

static void fm_create_file(const char* name, const char* content) {
    uint16_t dir_buf[256];
    read_sector(100, dir_buf);
    
    int slot = -1;
    for (int i = 0; i < 32; i++) {
        if (((char*)dir_buf)[i * 16] == 0) {
            slot = i;
            break;
        }
    }
    
    if (slot != -1) {
        for (int j = 0; j < 11 && name[j]; j++) {
            ((char*)dir_buf)[slot * 16 + j] = name[j];
        }
        ((char*)dir_buf)[slot * 16 + 11] = 0;
        
        int flen = str_len(content);
        uint16_t dbuf[256] = {0};
        
        for (int i = 0; i < flen && i < 510; i++) {
            if (i % 2 == 0) {
                dbuf[i / 2] = content[i];
            } else {
                dbuf[i / 2] |= (content[i] << 8);
            }
        }
        
        int fs = 500 + slot;
        write_sector(fs, dbuf);
        
        dir_buf[slot * 8 + 6] = fs;
        dir_buf[slot * 8 + 7] = flen;
        write_sector(100, dir_buf);
        
        fm_refresh();
    }
}

void wn_vesa_init(void) {
    vesa_enable();
    vesa_fb_ptr = (uint16_t*)vesa_fb;
    for (int i = 0; i < 1024 * 768; i++) {
        backbuffer[i] = 0;
    }
}

void wn_vesa_pixel(int x, int y, uint16_t c) {
    if (x >= 0 && x < 1024 && y >= 0 && y < 768) {
        backbuffer[y * 1024 + x] = c;
    }
}

void wn_vesa_rect(int x, int y, int w, int h, uint16_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 1024) w = 1024 - x;
    if (y + h > 768) h = 768 - y;
    if (w <= 0 || h <= 0) return;
    
    for (int dy = 0; dy < h; dy++) {
        uint16_t* l = backbuffer + (y + dy) * 1024 + x;
        for (int dx = 0; dx < w; dx++) {
            l[dx] = c;
        }
    }
}

void wn_vesa_clear(uint16_t c) {
    for (int i = 0; i < 1024 * 768; i++) {
        backbuffer[i] = c;
    }
}

static void wn_vesa_hline(int x, int y, int w, uint16_t c) {
    if (y < 0 || y >= 768) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > 1024) w = 1024 - x;
    if (w <= 0) return;
    
    uint16_t* l = backbuffer + y * 1024 + x;
    for (int i = 0; i < w; i++) {
        l[i] = c;
    }
}

void wn_vesa_gradient_v(int x, int y, int w, int h, uint16_t c1, uint16_t c2) {
    if (h <= 0) return;
    
    int r1 = (c1 >> 11) & 0x1F;
    int g1 = (c1 >> 5) & 0x3F;
    int b1 = c1 & 0x1F;
    int r2 = (c2 >> 11) & 0x1F;
    int g2 = (c2 >> 5) & 0x3F;
    int b2 = c2 & 0x1F;
    
    for (int dy = 0; dy < h; dy++) {
        int dr = r1 + (r2 - r1) * dy / h;
        int dg = g1 + (g2 - g1) * dy / h;
        int db = b1 + (b2 - b1) * dy / h;
        uint16_t color = (dr << 11) | (dg << 5) | db;
        wn_vesa_hline(x, y + dy, w, color);
    }
}

static const uint8_t font_8x8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x10,0x10,0x10,0x10,0x10,0x00,0x10,0x00},
    {0x28,0x28,0x28,0x00,0x00,0x00,0x00,0x00},{0x28,0x28,0x7C,0x28,0x7C,0x28,0x28,0x00},
    {0x10,0x3C,0x50,0x38,0x14,0x78,0x10,0x00},{0x60,0x64,0x08,0x10,0x20,0x4C,0x0C,0x00},
    {0x30,0x48,0x30,0x60,0x54,0x48,0x34,0x00},{0x10,0x10,0x10,0x00,0x00,0x00,0x00,0x00},
    {0x08,0x10,0x20,0x20,0x20,0x10,0x08,0x00},{0x20,0x10,0x08,0x08,0x08,0x10,0x20,0x00},
    {0x00,0x28,0x10,0x7C,0x10,0x28,0x00,0x00},{0x00,0x10,0x10,0x7C,0x10,0x10,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x10,0x10,0x20},{0x00,0x00,0x00,0x7C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x10,0x10,0x00},{0x04,0x08,0x10,0x10,0x20,0x40,0x40,0x00},
    {0x38,0x44,0x44,0x44,0x44,0x44,0x38,0x00},{0x10,0x30,0x10,0x10,0x10,0x10,0x38,0x00},
    {0x38,0x44,0x04,0x08,0x10,0x20,0x7C,0x00},{0x38,0x44,0x04,0x18,0x04,0x44,0x38,0x00},
    {0x08,0x18,0x28,0x48,0x7C,0x08,0x08,0x00},{0x7C,0x40,0x78,0x04,0x04,0x44,0x38,0x00},
    {0x38,0x40,0x78,0x44,0x44,0x44,0x38,0x00},{0x7C,0x04,0x08,0x10,0x20,0x20,0x20,0x00},
    {0x38,0x44,0x44,0x38,0x44,0x44,0x38,0x00},{0x38,0x44,0x44,0x3C,0x04,0x08,0x30,0x00},
    {0x00,0x00,0x10,0x00,0x00,0x10,0x00,0x00},{0x00,0x00,0x10,0x00,0x00,0x10,0x10,0x20},
    {0x08,0x10,0x20,0x40,0x20,0x10,0x08,0x00},{0x00,0x00,0x7C,0x00,0x7C,0x00,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x08,0x10,0x20,0x00},{0x38,0x44,0x04,0x08,0x10,0x00,0x10,0x00},
    {0x38,0x44,0x5C,0x54,0x5C,0x40,0x38,0x00},{0x38,0x44,0x44,0x7C,0x44,0x44,0x44,0x00},
    {0x78,0x44,0x44,0x78,0x44,0x44,0x78,0x00},{0x38,0x44,0x40,0x40,0x40,0x44,0x38,0x00},
    {0x78,0x44,0x44,0x44,0x44,0x44,0x78,0x00},{0x7C,0x40,0x40,0x78,0x40,0x40,0x7C,0x00},
    {0x7C,0x40,0x40,0x78,0x40,0x40,0x40,0x00},{0x38,0x44,0x40,0x5C,0x44,0x44,0x38,0x00},
    {0x44,0x44,0x44,0x7C,0x44,0x44,0x44,0x00},{0x38,0x10,0x10,0x10,0x10,0x10,0x38,0x00},
    {0x1C,0x08,0x08,0x08,0x48,0x48,0x30,0x00},{0x44,0x48,0x50,0x60,0x50,0x48,0x44,0x00},
    {0x40,0x40,0x40,0x40,0x40,0x40,0x7C,0x00},{0x44,0x6C,0x54,0x44,0x44,0x44,0x44,0x00},
    {0x44,0x64,0x54,0x4C,0x44,0x44,0x44,0x00},{0x38,0x44,0x44,0x44,0x44,0x44,0x38,0x00},
    {0x78,0x44,0x44,0x78,0x40,0x40,0x40,0x00},{0x38,0x44,0x44,0x44,0x54,0x48,0x34,0x00},
    {0x78,0x44,0x44,0x78,0x50,0x48,0x44,0x00},{0x38,0x44,0x40,0x38,0x04,0x44,0x38,0x00},
    {0x7C,0x10,0x10,0x10,0x10,0x10,0x10,0x00},{0x44,0x44,0x44,0x44,0x44,0x44,0x38,0x00},
    {0x44,0x44,0x44,0x28,0x28,0x10,0x10,0x00},{0x44,0x44,0x44,0x54,0x54,0x54,0x28,0x00},
    {0x44,0x28,0x10,0x10,0x28,0x44,0x44,0x00},{0x44,0x44,0x28,0x10,0x10,0x10,0x10,0x00},
    {0x7C,0x04,0x08,0x10,0x20,0x40,0x7C,0x00},{0x38,0x20,0x20,0x20,0x20,0x20,0x38,0x00},
    {0x40,0x20,0x10,0x10,0x08,0x04,0x04,0x00},{0x38,0x08,0x08,0x08,0x08,0x08,0x38,0x00},
    {0x10,0x28,0x44,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x7C,0x00},
    {0x20,0x10,0x08,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x38,0x04,0x3C,0x44,0x3C,0x00},{0x40,0x40,0x78,0x44,0x44,0x44,0x78,0x00},
    {0x00,0x00,0x38,0x40,0x40,0x44,0x38,0x00},{0x04,0x04,0x3C,0x44,0x44,0x44,0x3C,0x00},
    {0x00,0x00,0x38,0x44,0x7C,0x40,0x38,0x00},{0x18,0x24,0x20,0x78,0x20,0x20,0x20,0x00},
    {0x00,0x00,0x3C,0x44,0x3C,0x04,0x38,0x00},{0x40,0x40,0x78,0x44,0x44,0x44,0x44,0x00},
    {0x10,0x00,0x30,0x10,0x10,0x10,0x38,0x00},{0x08,0x00,0x18,0x08,0x08,0x48,0x30,0x00},
    {0x40,0x40,0x48,0x50,0x60,0x50,0x48,0x00},{0x30,0x10,0x10,0x10,0x10,0x10,0x38,0x00},
    {0x00,0x00,0x68,0x54,0x54,0x44,0x44,0x00},{0x00,0x00,0x78,0x44,0x44,0x44,0x44,0x00},
    {0x00,0x00,0x38,0x44,0x44,0x44,0x38,0x00},{0x00,0x00,0x78,0x44,0x78,0x40,0x40,0x00},
    {0x00,0x00,0x3C,0x44,0x3C,0x04,0x04,0x00},{0x00,0x00,0x58,0x64,0x40,0x40,0x40,0x00},
    {0x00,0x00,0x38,0x40,0x38,0x04,0x78,0x00},{0x20,0x20,0x78,0x20,0x20,0x24,0x18,0x00},
    {0x00,0x00,0x44,0x44,0x44,0x4C,0x34,0x00},{0x00,0x00,0x44,0x44,0x28,0x28,0x10,0x00},
    {0x00,0x00,0x44,0x54,0x54,0x54,0x28,0x00},{0x00,0x00,0x44,0x28,0x10,0x28,0x44,0x00},
    {0x00,0x00,0x44,0x44,0x3C,0x04,0x38,0x00},{0x00,0x00,0x7C,0x08,0x10,0x20,0x7C,0x00},
    {0x08,0x10,0x10,0x20,0x10,0x10,0x08,0x00},{0x10,0x10,0x10,0x00,0x10,0x10,0x10,0x00},
    {0x20,0x10,0x10,0x08,0x10,0x10,0x20,0x00}
};

void wn_vesa_char(int x, int y, char ch, uint16_t c) {
    int idx = (int)ch - 32;
    if (idx < 0 || idx >= 96) return;
    const uint8_t* bm = font_8x8[idx];
    for (int r = 0; r < 8; r++) {
        uint8_t m = bm[r];
        for (int cl = 0; cl < 8; cl++) {
            if (m & (0x80 >> cl)) {
                wn_vesa_pixel(x + cl, y + r, c);
            }
        }
    }
}

void wn_vesa_text(int x, int y, const char* s, uint16_t c) {
    int cx = x;
    while (*s) {
        if (*s == '\n') {
            cx = x;
            y += 10;
            s++;
            continue;
        }
        wn_vesa_char(cx, y, *s, c);
        cx += 9;
        s++;
    }
}

static void draw_icon(int x, int y, int t) {
    if (t == 0) {
        wn_vesa_rect(x+4, y+2, 28, 22, WN_BLACK);
        wn_vesa_rect(x+5, y+3, 26, 20, WN_BLUE);
        wn_vesa_rect(x+6, y+4, 24, 18, WN_CYAN);
        wn_vesa_rect(x+12, y+24, 12, 4, WN_GRAY);
        wn_vesa_rect(x+8, y+28, 20, 4, WN_GRAY);
        wn_vesa_pixel(x+30, y+8, WN_GREEN);
    }
    if (t == 1) {
        wn_vesa_rect(x+4, y+2, 28, 30, WN_WHITE);
        wn_vesa_rect(x+5, y+3, 26, 6, WN_BLUE);
        for (int i = 0; i < 5; i++) {
            wn_vesa_hline(x+8, y+14+i*3, 16, WN_GRAY);
        }
    }
    if (t == 2) {
        wn_vesa_rect(x+4, y+6, 28, 26, WN_GRAY);
        wn_vesa_rect(x+6, y+8, 24, 8, WN_WHITE);
        wn_vesa_text(x+8, y+9, "12", WN_BLACK);
    }
    if (t == 3) {
        wn_vesa_rect(x+4, y+4, 28, 28, WN_BLACK);
        wn_vesa_text(x+8, y+8, ">_", WN_GREEN);
    }
    if (t == 4) {
        wn_vesa_rect(x+4, y+4, 28, 28, WN_WHITE);
        wn_vesa_rect(x+8, y+8, 8, 8, WN_RED);
        wn_vesa_pixel(x+20, y+8, WN_BLUE);
    }
}

static void draw_window(int x, int y, int w, int h, const char* title, int active) {
    for (int dy = 2; dy < h + 2; dy++) {
        backbuffer[(y + dy) * 1024 + (x + w + 1)] = 0x4228;
        backbuffer[(y + dy) * 1024 + (x + w + 2)] = 0x2104;
    }
    for (int dx = 2; dx < w + 2; dx++) {
        backbuffer[(y + h + 1) * 1024 + (x + dx)] = 0x4228;
        backbuffer[(y + h + 2) * 1024 + (x + dx)] = 0x2104;
    }
    
    if (settings.theme == 0) {
        wn_vesa_rect(x, y, w, h, WN_DKGRAY);
        wn_vesa_rect(x+1, y+1, w-2, h-2, WN_LTGRAY);
        for (int i = 0; i < 20; i++) {
            uint16_t g;
            if (active) {
                g = WN_RGB565(0, 50 + i * 8, 160 + i * 4);
            } else {
                g = WN_RGB565(120, 120, 140 + i * 3);
            }
            wn_vesa_hline(x + 1, y + 1 + i, w - 2, g);
        }
        if (active) {
            wn_vesa_hline(x + 2, y + 3, w - 4, WN_RGB565(180, 220, 255));
        }
        wn_vesa_text(x + 6, y + 5, title, WN_WHITE);
    }
    else if (settings.theme == 1) {
        wn_vesa_rect(x, y, w, h, WN_BLACK);
        wn_vesa_rect(x+1, y+1, w-2, h-2, WN_GRAY);
        for (int i = 0; i < 20; i++) {
            uint16_t g;
            if (active) {
                g = WN_RGB565(0, 0, 150 + i * 3);
            } else {
                g = WN_RGB565(100, 100, 100);
            }
            wn_vesa_hline(x + 1, y + 1 + i, w - 2, g);
        }
        wn_vesa_text(x + 6, y + 5, title, WN_WHITE);
    }
    else {
        wn_vesa_rect(x, y, w, h, WN_BLACK);
        wn_vesa_rect(x+1, y+1, w-2, h-2, WN_DARK_WIN);
        for (int i = 0; i < 20; i++) {
            uint16_t g;
            if (active) {
                g = WN_RGB565(60, 60, 80 + i * 5);
            } else {
                g = WN_RGB565(35, 35, 50 + i * 2);
            }
            wn_vesa_hline(x + 1, y + 1 + i, w - 2, g);
        }
        if (active) {
            wn_vesa_hline(x + 2, y + 3, w - 4, WN_RGB565(100, 100, 130));
        }
        wn_vesa_text(x + 6, y + 5, title, WN_SILVER);
    }
    
    int bx = x + w - 20;
    wn_vesa_rect(bx, y + 4, 14, 12, WN_RED);
    wn_vesa_rect(bx, y + 4, 14, 12, WN_DKGRAY);
    wn_vesa_hline(bx + 1, y + 5, 12, WN_RGB565(255, 150, 150));
    for (int t = 0; t < 6; t++) {
        wn_vesa_pixel(bx + 4 + t, y + 7 + t, WN_WHITE);
        wn_vesa_pixel(bx + 10 - t, y + 7 + t, WN_WHITE);
    }
    
    int bx2 = x + w - 38;
    wn_vesa_rect(bx2, y + 4, 14, 12, WN_LTGRAY);
    wn_vesa_rect(bx2, y + 4, 14, 12, WN_DKGRAY);
    wn_vesa_hline(bx2 + 3, y + 10, 8, WN_WHITE);
}

static void draw_taskbar(void) {
    int ty = 768 - 36;
    
    if (settings.theme == 0) {
        wn_vesa_gradient_v(0, ty, 1024, 36, WN_RGB565(40, 80, 160), WN_RGB565(20, 40, 100));
        wn_vesa_hline(0, ty, 1024, WN_SILVER);
        wn_vesa_hline(0, ty + 1, 1024, WN_RGB565(100, 150, 220));
        wn_vesa_gradient_v(6, ty + 4, 64, 28, WN_RGB565(60, 160, 240), WN_RGB565(30, 100, 180));
    }
    else if (settings.theme == 1) {
        wn_vesa_rect(0, ty, 1024, 36, WN_GRAY);
        wn_vesa_rect(0, ty, 1024, 36, WN_BLACK);
        wn_vesa_hline(1, ty + 1, 1022, WN_WHITE);
        wn_vesa_rect(6, ty + 4, 64, 28, WN_DKGRAY);
    }
    else {
        wn_vesa_gradient_v(0, ty, 1024, 36, WN_RGB565(45, 45, 65), WN_RGB565(25, 25, 40));
        wn_vesa_hline(0, ty, 1024, WN_RGB565(80, 80, 100));
        wn_vesa_gradient_v(6, ty + 4, 64, 28, WN_RGB565(70, 70, 100), WN_RGB565(40, 40, 60));
    }
    
    wn_vesa_rect(6, ty + 4, 64, 28, WN_DKGRAY);
    uint16_t highlight = (settings.theme == 2) ? WN_RGB565(100, 100, 130) : WN_RGB565(140, 220, 255);
    wn_vesa_hline(7, ty + 5, 62, highlight);
    wn_vesa_text(14, ty + 10, "Start", WN_WHITE);
}

static void draw_screensaver(void) {
    ss_logo_x += ss_logo_dx;
    ss_logo_y += ss_logo_dy;
    
    if (ss_logo_x < 50 || ss_logo_x > 974) ss_logo_dx = -ss_logo_dx;
    if (ss_logo_y < 50 || ss_logo_y > 718) ss_logo_dy = -ss_logo_dy;
    
    wn_vesa_clear(WN_BLACK);
    
    if (settings.screensaver_type == 0) {
        for (int i = 0; i < 8; i++) {
            float angle = i * 0.785f + ss_frame * 0.05f;
            float s = angle - (angle*angle*angle)/6.0f + (angle*angle*angle*angle*angle)/120.0f;
            float c2 = (angle+1.57f) - ((angle+1.57f)*(angle+1.57f)*(angle+1.57f))/6.0f;
            int x1 = (int)(ss_logo_x + 30.0f * s);
            int y1 = (int)(ss_logo_y + 30.0f * c2);
            int x2 = (int)(ss_logo_x + 50.0f * s);
            int y2 = (int)(ss_logo_y + 50.0f * c2);
            wn_vesa_pixel(x1, y1, WN_RED);
            wn_vesa_pixel(x2, y2, WN_RED);
        }
        wn_vesa_text((int)ss_logo_x - 30, (int)ss_logo_y - 10, "WNKA", WN_WHITE);
    }
    else if (settings.screensaver_type == 1) {
        for (int i = 0; i < 200; i++) {
            int sx = (ss_frame * 3 + i * 13) % 1024;
            int sy = (i * 7 + ss_frame) % 768;
            wn_vesa_pixel(sx, sy, WN_GREEN);
        }
    }
    else if (settings.screensaver_type == 2) {
        for (int i = 0; i < 100; i++) {
            int y2 = (i * 23 + ss_frame * 3) % 768;
            wn_vesa_text((i * 10) % 1024, y2, ss_frame % 2 ? "0" : "1", WN_GREEN);
        }
    }
    
    ss_frame++;
}

static void draw_start_menu(void) {
    if (!start_menu) return;
    
    int sx = 4;
    int sy = 768 - 36 - 280;
    int sw = 200;
    int sh = 280;
    
    wn_vesa_rect(sx + 3, sy + 3, sw, sh, 0x4228);
    wn_vesa_gradient_v(sx, sy, sw, sh, WN_RGB565(240, 240, 245), WN_RGB565(220, 220, 230));
    wn_vesa_rect(sx, sy, sw, sh, WN_GRAY);
    wn_vesa_gradient_v(sx + 1, sy + 1, 24, sh - 2, WN_RGB565(0, 80, 200), WN_RGB565(0, 30, 120));
    
    wn_vesa_text(sx + 6, sy + 15, "W", WN_WHITE);
    wn_vesa_text(sx + 6, sy + 35, "N", WN_WHITE);
    wn_vesa_text(sx + 6, sy + 55, "K", WN_WHITE);
    wn_vesa_text(sx + 6, sy + 75, "A", WN_WHITE);
    
    const char* items[] = {
        "File Manager", "Notepad", "Calculator", "Terminal",
        "Paint", "Clock", "Settings", "Power Off"
    };
    
    for (int i = 0; i < 8; i++) {
        int iy = sy + 8 + i * 28;
        
        if (mx >= sx + 28 && mx < sx + sw - 2 && my >= iy && my < iy + 24) {
            wn_vesa_gradient_v(sx + 28, iy, sw - 30, 24,
                              WN_RGB565(60, 140, 240), WN_RGB565(40, 100, 200));
            wn_vesa_text(sx + 34, iy + 5, items[i], WN_WHITE);
        } else {
            wn_vesa_text(sx + 34, iy + 5, items[i], WN_BLACK);
        }
        wn_vesa_hline(sx + 28, iy + 24, sw - 30, WN_SILVER);
    }
}

static void draw_power_dialog(void) {
    if (!power_dialog) return;
    
    for (int y = 0; y < 768; y++) {
        for (int x = 0; x < 1024; x += 2) {
            uint16_t c = backbuffer[y * 1024 + x];
            backbuffer[y * 1024 + x] = ((c >> 2) & 0x39E7);
        }
    }
    
    int dx = 362;
    int dy = 334;
    int dw = 300;
    int dh = 140;
    
    wn_vesa_rect(dx + 3, dy + 3, dw, dh, 0x4228);
    wn_vesa_rect(dx, dy, dw, dh, WN_LTGRAY);
    wn_vesa_rect(dx, dy, dw, dh, WN_DKGRAY);
    wn_vesa_gradient_v(dx + 1, dy + 1, dw - 2, 24, WN_RGB565(0, 80, 200), WN_RGB565(0, 30, 120));
    wn_vesa_text(dx + 70, dy + 6, "Turn off computer", WN_WHITE);
    
    wn_vesa_text(dx + 45, dy + 50, "Reboot", WN_BLACK);
    wn_vesa_text(dx + 130, dy + 50, "Shutdown", WN_BLACK);
    wn_vesa_text(dx + 225, dy + 50, "Cancel", WN_BLACK);
    
    for (int b = 0; b < 3; b++) {
        int btn_x = dx + 25 + b * 95;
        wn_vesa_rect(btn_x, dy + 68, 75, 28, WN_LTGRAY);
        wn_vesa_rect(btn_x, dy + 68, 75, 28, WN_DKGRAY);
        wn_vesa_hline(btn_x + 1, dy + 69, 73, WN_WHITE);
    }
}

static void draw_first_setup(void) {
    int win_x = 200;
    int win_y = 150;
    int win_w = 624;
    int win_h = 400;
    
    for (int y = 0; y < 768; y++) {
        for (int x = 0; x < 1024; x += 2) {
            uint16_t c = backbuffer[y * 1024 + x];
            backbuffer[y * 1024 + x] = ((c >> 2) & 0x39E7);
        }
    }
    
    wn_vesa_rect(win_x + 4, win_y + 4, win_w, win_h, 0x4228);
    wn_vesa_rect(win_x, win_y, win_w, win_h, WN_LTGRAY);
    wn_vesa_rect(win_x, win_y, win_w, win_h, WN_DKGRAY);
    
    wn_vesa_gradient_v(win_x + 1, win_y + 1, win_w - 2, 30,
                      WN_RGB565(0, 80, 200), WN_RGB565(0, 30, 120));
    wn_vesa_text(win_x + win_w/2 - 80, win_y + 8, "Welcome to WNKA OS Setup", WN_WHITE);
    
    wn_vesa_text(win_x + 40, win_y + 50, "Username:", WN_BLACK);
    wn_vesa_rect(win_x + 180, win_y + 48, 250, 24, WN_WHITE);
    wn_vesa_rect(win_x + 180, win_y + 48, 250, 24, WN_DKGRAY);
    wn_vesa_text(win_x + 185, win_y + 53, settings.username, WN_BLACK);
    
    wn_vesa_text(win_x + 40, win_y + 82, "Computer Name:", WN_BLACK);
    wn_vesa_rect(win_x + 180, win_y + 80, 250, 24, WN_WHITE);
    wn_vesa_rect(win_x + 180, win_y + 80, 250, 24, WN_DKGRAY);
    wn_vesa_text(win_x + 185, win_y + 85, settings.computer_name, WN_BLACK);
    
    wn_vesa_text(win_x + 40, win_y + 114, "Theme:", WN_BLACK);
    const char* themes[] = {"WNKA Standard", "Classic", "Dark"};
    for (int i = 0; i < 3; i++) {
        int tx = win_x + 180 + i * 100;
        if (settings.theme == i) {
            wn_vesa_rect(tx, win_y + 110, 90, 22, WN_BLUE);
        } else {
            wn_vesa_rect(tx, win_y + 110, 90, 22, WN_LTGRAY);
        }
        wn_vesa_rect(tx, win_y + 110, 90, 22, WN_DKGRAY);
        wn_vesa_text(tx + 5, win_y + 115, themes[i], settings.theme == i ? WN_WHITE : WN_BLACK);
    }
    
    wn_vesa_text(win_x + 40, win_y + 146, "Wallpaper:", WN_BLACK);
    const char* walls[] = {"Gradient", "Stars", "Grid", "Waves", "Matrix"};
    for (int i = 0; i < 5; i++) {
        int w2 = win_x + 180 + i * 90;
        if (settings.wallpaper == i) {
            wn_vesa_rect(w2, win_y + 142, 80, 22, WN_BLUE);
        } else {
            wn_vesa_rect(w2, win_y + 142, 80, 22, WN_LTGRAY);
        }
        wn_vesa_rect(w2, win_y + 142, 80, 22, WN_DKGRAY);
        wn_vesa_text(w2 + 5, win_y + 147, walls[i], settings.wallpaper == i ? WN_WHITE : WN_BLACK);
    }
    
    wn_vesa_text(win_x + 40, win_y + 178, "Screensaver:", WN_BLACK);
    const char* ss[] = {"Bounce", "Stars", "Matrix"};
    for (int i = 0; i < 3; i++) {
        int sx2 = win_x + 180 + i * 100;
        if (settings.screensaver_type == i) {
            wn_vesa_rect(sx2, win_y + 174, 90, 22, WN_BLUE);
        } else {
            wn_vesa_rect(sx2, win_y + 174, 90, 22, WN_LTGRAY);
        }
        wn_vesa_rect(sx2, win_y + 174, 90, 22, WN_DKGRAY);
        wn_vesa_text(sx2 + 5, win_y + 179, ss[i], settings.screensaver_type == i ? WN_WHITE : WN_BLACK);
    }
    
    int btn_x = win_x + win_w/2 - 50;
    int btn_y = win_y + win_h - 50;
    wn_vesa_gradient_v(btn_x, btn_y, 100, 30, WN_RGB565(60, 180, 60), WN_RGB565(30, 120, 30));
    wn_vesa_rect(btn_x, btn_y, 100, 30, WN_DKGRAY);
    wn_vesa_text(btn_x + 25, btn_y + 8, "Let's Go!", WN_WHITE);
}

typedef struct {
    int x, y, w, h;
    char title[32];
    int visible;
    int active;
    int type;
    int z_order;
} vesa_window_t;

static vesa_window_t vesa_wins[10];
static int vesa_win_count = 0;
static int vesa_top_z = 0;

static int vesa_create_window(int x, int y, int w, int h, const char* title, int type) {
    if (vesa_win_count >= 10) return -1;
    
    vesa_window_t* win = &vesa_wins[vesa_win_count];
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->visible = 1;
    win->type = type;
    
    int i = 0;
    while (title[i] && i < 31) {
        win->title[i] = title[i];
        i++;
    }
    win->title[i] = 0;
    
    for (int j = 0; j < vesa_win_count; j++) {
        vesa_wins[j].active = 0;
    }
    win->active = 1;
    win->z_order = vesa_top_z++;
    vesa_win_count++;
    
    return vesa_win_count - 1;
}

static void vesa_bring_to_front(int idx) {
    for (int j = 0; j < vesa_win_count; j++) {
        vesa_wins[j].active = 0;
    }
    vesa_wins[idx].active = 1;
    vesa_wins[idx].z_order = vesa_top_z++;
}

static void vesa_draw_windows(void) {
    int order[10];
    int cnt = 0;
    for (int i = 0; i < vesa_win_count; i++) {
        if (vesa_wins[i].visible) {
            order[cnt++] = i;
        }
    }
    
    for (int i = 0; i < cnt - 1; i++) {
        for (int j = 0; j < cnt - 1 - i; j++) {
            if (vesa_wins[order[j]].z_order > vesa_wins[order[j+1]].z_order) {
                int t = order[j];
                order[j] = order[j+1];
                order[j+1] = t;
            }
        }
    }
    
    for (int o = 0; o < cnt; o++) {
        int i = order[o];
        vesa_window_t* win = &vesa_wins[i];
        
        draw_window(win->x, win->y, win->w, win->h, win->title, win->active);
        
        int wx = win->x;
        int wy = win->y;
        int ww = win->w;
        int wh = win->h;
        
        switch (win->type) {
            case 0:
                wn_vesa_text(wx + 10, wy + 28, "Path: /", WN_BLACK);
                wn_vesa_hline(wx + 4, wy + 40, ww - 8, WN_GRAY);
                
                for (int f = 0; f < current_file_count && f < 15; f++) {
                    int fy = wy + 44 + f * 18;
                    if (f == file_selected) {
                        wn_vesa_rect(wx + 4, fy, ww - 8, 18, WN_BLUE);
                    }
                    wn_vesa_text(wx + 10, fy + 2,
                                current_files[f].is_dir ? "[DIR]" : "[FILE]",
                                current_files[f].is_dir ? WN_CYAN : WN_GRAY);
                    wn_vesa_text(wx + 50, fy + 2, current_files[f].name,
                                f == file_selected ? WN_WHITE : WN_BLACK);
                }
                break;
                
            case 1:
                wn_vesa_text(wx + 8, wy + 28, "Notepad", WN_BLACK);
                wn_vesa_hline(wx + 4, wy + 38, ww - 8, WN_GRAY);
                for (int t = 0; t < notepad_lines && t < 15; t++) {
                    wn_vesa_text(wx + 8, wy + 44 + t * 14, notepad_text[t], WN_BLACK);
                }
                break;
                
            case 2:
                wn_vesa_text(wx + 10, wy + 30, "Welcome!", WN_BLACK);
                wn_vesa_text(wx + 10, wy + 50, "WNKA OS v5.0", WN_BLACK);
                wn_vesa_text(wx + 10, wy + 70, "VESA GUI Edition", WN_BLACK);
                break;
                
            case 3:
                {
                    wn_vesa_rect(wx + 8, wy + 28, 180, 24, WN_WHITE);
                    wn_vesa_rect(wx + 8, wy + 28, 180, 24, WN_DKGRAY);
                    char val[20];
                    int_to_str(calc_value, val);
                    wn_vesa_text(wx + 170 - str_len(val) * 9, wy + 33, val, WN_BLACK);
                    
                    const char* btns[] = {"7","8","9","/","4","5","6","*","1","2","3","-","0",".","=","+"};
                    for (int b = 0; b < 16; b++) {
                        int bx = wx + 8 + (b % 4) * 46;
                        int by = wy + 58 + (b / 4) * 26;
                        wn_vesa_rect(bx, by, 42, 22, WN_GRAY);
                        wn_vesa_rect(bx, by, 42, 22, WN_DKGRAY);
                        wn_vesa_text(bx + 18, by + 5, btns[b], WN_BLACK);
                    }
                }
                break;
                
            case 4:
                wn_vesa_rect(wx + 4, wy + 22, ww - 8, wh - 28, WN_BLACK);
                wn_vesa_text(wx + 8, wy + 26, "WNKA Terminal", WN_GREEN);
                wn_vesa_text(wx + 8, wy + 42, "C:\\>", WN_WHITE);
                break;
                
            case 5:
                wn_vesa_rect(wx + 4, wy + 22, ww - 8, wh - 28, WN_WHITE);
                {
                    uint16_t cols[] = {WN_BLACK, WN_RED, WN_GREEN, WN_BLUE, WN_YELLOW, WN_CYAN};
                    for (int c = 0; c < 6; c++) {
                        wn_vesa_rect(wx + 8 + c * 22, wy + 26, 20, 20, cols[c]);
                        wn_vesa_rect(wx + 8 + c * 22, wy + 26, 20, 20, WN_DKGRAY);
                    }
                }
                break;
                
            case 6:
                wn_vesa_text(wx + 8, wy + 28, "ASCII Clock", WN_BLACK);
                {
                    char ct[16];
                    int h = clock_h % 12;
                    if (h == 0) h = 12;
                    ct[0] = '0' + (h / 10);
                    ct[1] = '0' + (h % 10);
                    ct[2] = ':';
                    ct[3] = '0' + (clock_m / 10);
                    ct[4] = '0' + (clock_m % 10);
                    if (settings.show_seconds) {
                        ct[5] = ':';
                        ct[6] = '0' + (clock_s / 10);
                        ct[7] = '0' + (clock_s % 10);
                        ct[8] = 0;
                    } else {
                        ct[5] = 0;
                    }
                    wn_vesa_text(wx + 10, wy + 50, ct, WN_CYAN);
                }
                break;
                
            case 7:
                {
                    wn_vesa_gradient_v(wx + 4, wy + 24, 120, wh - 30,
                                      WN_RGB565(40, 80, 140), WN_RGB565(20, 40, 80));
                    
                    const char* tabs[] = {"Theme", "Wallpaper", "Screensaver", "Clock", "About"};
                    for (int t = 0; t < 5; t++) {
                        int ty = wy + 44 + t * 26;
                        if (t == settings_theme_tab) {
                            wn_vesa_rect(wx + 4, ty, 116, 22, WN_RGB565(0, 120, 220));
                        }
                        wn_vesa_text(wx + 12, ty + 5, tabs[t],
                                    t == settings_theme_tab ? WN_WHITE : WN_SILVER);
                    }
                    
                    if (settings_theme_tab == 0) {
                        wn_vesa_text(wx + 140, wy + 28, "Theme Settings", WN_BLACK);
                        const char* themes[] = {"WNKA Standard", "Classic", "Dark"};
                        for (int t = 0; t < 3; t++) {
                            int ty2 = wy + 48 + t * 26;
                            if (settings.theme == t) {
                                wn_vesa_rect(wx + 140, ty2, 160, 22, WN_BLUE);
                            } else {
                                wn_vesa_rect(wx + 140, ty2, 160, 22, WN_GRAY);
                            }
                            wn_vesa_rect(wx + 140, ty2, 160, 22, WN_DKGRAY);
                            wn_vesa_text(wx + 145, ty2 + 5, themes[t],
                                        settings.theme == t ? WN_WHITE : WN_BLACK);
                        }
                    }
                    
                    if (settings_theme_tab == 1) {
                        wn_vesa_text(wx + 140, wy + 28, "Wallpaper", WN_BLACK);
                        const char* walls[] = {"Gradient", "Stars", "Grid", "Waves", "Matrix"};
                        for (int w = 0; w < 5; w++) {
                            int wy2 = wy + 48 + w * 22;
                            if (settings.wallpaper == w) {
                                wn_vesa_rect(wx + 140, wy2, 100, 18, WN_BLUE);
                            } else {
                                wn_vesa_rect(wx + 140, wy2, 100, 18, WN_GRAY);
                            }
                            wn_vesa_rect(wx + 140, wy2, 100, 18, WN_DKGRAY);
                            wn_vesa_text(wx + 145, wy2 + 3, walls[w],
                                        settings.wallpaper == w ? WN_WHITE : WN_BLACK);
                        }
                    }
                    
                    if (settings_theme_tab == 2) {
                        wn_vesa_text(wx + 140, wy + 28, "Screensaver", WN_BLACK);
                        const char* ss[] = {"Bounce", "Stars", "Matrix"};
                        for (int s = 0; s < 3; s++) {
                            int sy2 = wy + 48 + s * 26;
                            if (settings.screensaver_type == s) {
                                wn_vesa_rect(wx + 140, sy2, 100, 22, WN_BLUE);
                            } else {
                                wn_vesa_rect(wx + 140, sy2, 100, 22, WN_GRAY);
                            }
                            wn_vesa_rect(wx + 140, sy2, 100, 22, WN_DKGRAY);
                            wn_vesa_text(wx + 145, sy2 + 5, ss[s],
                                        settings.screensaver_type == s ? WN_WHITE : WN_BLACK);
                        }
                        wn_vesa_text(wx + 140, wy + 130, "Enabled:", WN_BLACK);
                        wn_vesa_text(wx + 220, wy + 130,
                                    settings.screensaver_enabled ? "[ON]" : "[OFF]",
                                    settings.screensaver_enabled ? WN_GREEN : WN_RED);
                    }
                    
                    if (settings_theme_tab == 3) {
                        wn_vesa_text(wx + 140, wy + 28, "Clock", WN_BLACK);
                        wn_vesa_text(wx + 140, wy + 48, "24h:", WN_BLACK);
                        wn_vesa_text(wx + 220, wy + 48,
                                    settings.clock_24h ? "[ON]" : "[OFF]",
                                    settings.clock_24h ? WN_GREEN : WN_RED);
                        wn_vesa_text(wx + 140, wy + 68, "Seconds:", WN_BLACK);
                        wn_vesa_text(wx + 240, wy + 68,
                                    settings.show_seconds ? "[ON]" : "[OFF]",
                                    settings.show_seconds ? WN_GREEN : WN_RED);
                    }
                    
                    if (settings_theme_tab == 4) {
                        wn_vesa_text(wx + 140, wy + 28, "About WNKA OS", WN_BLACK);
                        wn_vesa_text(wx + 140, wy + 50, "Version 5.0", WN_BLACK);
                        wn_vesa_text(wx + 140, wy + 68, "VESA GUI Edition", WN_BLACK);
                        wn_vesa_text(wx + 140, wy + 86, "2026 WNKA Software", WN_GRAY);
                    }
                    
                    wn_vesa_rect(wx + ww - 60, wy + wh - 28, 50, 20, WN_GRAY);
                    wn_vesa_text(wx + ww - 52, wy + wh - 24, "Apply", WN_BLACK);
                }
                break;
        }
    }
}

static int get_input(void) {
    if (vesa_inb(0x64) & 1) {
        uint8_t sc = vesa_inb(0x60);
        if (sc < 0x80) {
            if (sc == 0x11 || sc == 0x48) return 1;  
            if (sc == 0x1F || sc == 0x50) return 2;  
            if (sc == 0x1E || sc == 0x4B) return 3; 
            if (sc == 0x20 || sc == 0x4D) return 4;  
            if (sc == 0x10) return 5;   
            if (sc == 0x13) return 8;   
            if (sc == 0x01) return 7;   
            if (sc == 0x39) return 11;  
        }
    }
    return 0;
}

#define CURSOR_NONE      0  
#define CURSOR_DESKTOP   1  
#define CURSOR_ICON      2  
#define CURSOR_WINDOW    3   
#define CURSOR_TITLEBAR  4  
#define CURSOR_TASKBAR   5   
#define CURSOR_STARTBTN  6  

static int check_cursor_position(int* out_icon_idx, int* out_win_idx) {
    if (out_icon_idx) *out_icon_idx = -1;
    if (out_win_idx) *out_win_idx = -1;
    
    int start_btn_y = 768 - 36;
    if (mx >= 6 && mx < 70 && my >= start_btn_y + 4 && my < start_btn_y + 32) {
        return CURSOR_STARTBTN;
    }
    
    if (my >= 768 - 36) {
        return CURSOR_TASKBAR;
    }
    
    if (start_menu) {
        int menu_x = 4;
        int menu_y = 768 - 36 - 280;
        int menu_w = 200;
        int menu_h = 280;
        if (mx >= menu_x && mx < menu_x + menu_w &&
            my >= menu_y && my < menu_y + menu_h) {
            return CURSOR_WINDOW; 
        }
    }
    
    if (power_dialog) {
        int dx = 362;
        int dy = 334;
        int dw = 300;
        int dh = 140;
        if (mx >= dx && mx < dx + dw && my >= dy && my < dy + dh) {
            return CURSOR_WINDOW;
        }
    }

    if (first_setup) {
        int sx = 200;
        int sy = 150;
        int sw = 624;
        int sh = 400;
        if (mx >= sx && mx < sx + sw && my >= sy && my < sy + sh) {
            return CURSOR_WINDOW;
        }
    }
    

    int visible_wins[10];
    int visible_count = 0;
    for (int i = 0; i < vesa_win_count; i++) {
        if (vesa_wins[i].visible) {
            visible_wins[visible_count++] = i;
        }
    }
    
    for (int i = 0; i < visible_count - 1; i++) {
        for (int j = 0; j < visible_count - 1 - i; j++) {
            if (vesa_wins[visible_wins[j]].z_order < vesa_wins[visible_wins[j+1]].z_order) {
                int tmp = visible_wins[j];
                visible_wins[j] = visible_wins[j+1];
                visible_wins[j+1] = tmp;
            }
        }
    }
    
    for (int vi = 0; vi < visible_count; vi++) {
        int i = visible_wins[vi];
        int wx = vesa_wins[i].x;
        int wy = vesa_wins[i].y;
        int ww = vesa_wins[i].w;
        int wh = vesa_wins[i].h;
        
        if (mx >= wx && mx < wx + ww && my >= wy && my < wy + wh) {
            if (my >= wy && my < wy + 20) {
                if (mx >= wx + ww - 20 && mx < wx + ww - 6 && my >= wy + 4 && my < wy + 16) {
                    if (out_win_idx) *out_win_idx = i;
                    return CURSOR_TITLEBAR;
                }
                if (mx >= wx + ww - 38 && mx < wx + ww - 24 && my >= wy + 4 && my < wy + 16) {
                    if (out_win_idx) *out_win_idx = i;
                    return CURSOR_TITLEBAR; 
                }
                if (out_win_idx) *out_win_idx = i;
                return CURSOR_TITLEBAR;
            }
            
            if (out_win_idx) *out_win_idx = i;
            return CURSOR_WINDOW;
        }
    }
    
    for (int i = 0; i < 5; i++) {
        int icon_x, icon_y;
        switch (i) {
            case 0: icon_x = 20;  icon_y = 20;  break; 
            case 1: icon_x = 100; icon_y = 20;  break; 
            case 2: icon_x = 180; icon_y = 20;  break; 
            case 3: icon_x = 260; icon_y = 20;  break; 
            case 4: icon_x = 340; icon_y = 20;  break; 
            default: continue;
        }
        
        if (mx >= icon_x && mx < icon_x + 36 && my >= icon_y && my < icon_y + 60) {
            if (out_icon_idx) *out_icon_idx = i;
            return CURSOR_ICON;
        }
    }
    
    return CURSOR_DESKTOP;
}
void wn_demo(void) {
    wn_vesa_init();
    load_settings();
    fm_refresh();
    
    if (settings.first_run) {
        first_setup = 1;
    }
    
    vesa_create_window(50, 50, 400, 320, "My Computer", 0);
    vesa_create_window(480, 80, 380, 280, "Welcome", 2);
    
    int running = 1;
    int frame = 0;
    int last_click = 0;
    
    while (running) {
        int key = get_input();
        
        if (frame % 60 == 0) {
            clock_s++;
            if (clock_s >= 60) {
                clock_s = 0;
                clock_m++;
            }
            if (clock_m >= 60) {
                clock_m = 0;
                clock_h++;
            }
            if (clock_h >= 24) {
                clock_h = 0;
            }
        }
        
        if (mx < 0) mx = 0;
        if (mx > 1023) mx = 1023;
        if (my < 0) my = 0;
        if (my > 767) my = 767;

        int icon_idx = -1;
        int win_idx = -1;
        int cursor_pos = check_cursor_position(&icon_idx, &win_idx);
        
        int speed = 16;
        if (key == 1) my -= speed;
        if (key == 2) my += speed;
        if (key == 3) mx -= speed;
        if (key == 4) mx += speed;
        
        if (mx != last_mx || my != last_my) {
            idle_frames = 0;
            last_mx = mx;
            last_my = my;
        } else {
            idle_frames++;
        }
        
        if (settings.screensaver_enabled &&
            idle_frames > settings.screensaver_timeout &&
            !start_menu && !power_dialog && !first_setup) {
            screensaver_active = 1;
        }
        
        if (screensaver_active) {
            if (vesa_inb(0x64) & 1) {
                uint8_t sc = vesa_inb(0x60);
                if (sc < 0x80) {
                    screensaver_active = 0;
                    idle_frames = 0;
                    ss_frame = 0;
                }
            }
            if (mx != last_mx || my != last_my) {
                screensaver_active = 0;
                idle_frames = 0;
            }
            draw_screensaver();
            for (int i = 0; i < 1024 * 768; i++) {
                vesa_fb_ptr[i] = backbuffer[i];
            }
            vesa_wait_vsync();
            frame++;
            continue;
        }
        
        if (key == 8 && !last_click) {
            last_click = 1;
            start_menu = !start_menu;
        }
        
        if (key == 7 && !last_click) {
            last_click = 1;
            if (power_dialog) {
                power_dialog = 0;
            } else if (start_menu) {
                start_menu = 0;
            }
        }
        
        if ((key == 5 || key == 11) && !last_click) {
            last_click = 1;
            
            if (first_setup) {
                int dx = 200, dy = 150;
                
                for (int i = 0; i < 3; i++) {
                    int tx = dx + 180 + i * 100;
                    if (mx >= tx && mx < tx + 90 && my >= dy + 110 && my < dy + 132) {
                        settings.theme = i;
                    }
                }
                
                for (int i = 0; i < 5; i++) {
                    int w2 = dx + 180 + i * 90;
                    if (mx >= w2 && mx < w2 + 80 && my >= dy + 142 && my < dy + 164) {
                        settings.wallpaper = i;
                    }
                }
                
                for (int i = 0; i < 3; i++) {
                    int sx2 = dx + 180 + i * 100;
                    if (mx >= sx2 && mx < sx2 + 90 && my >= dy + 174 && my < dy + 196) {
                        settings.screensaver_type = i;
                    }
                }
                
                if (mx >= dx + 262 && mx < dx + 362 && my >= dy + 350 && my < dy + 380) {
                    save_settings();
                    first_setup = 0;
                    fm_create_dir("Documents");
                    fm_create_dir("Music");
                    fm_create_dir("Pictures");
                    fm_create_file("README.TXT", "Welcome to WNKA OS!");
                    fm_refresh();
                }
                goto draw_frame;
            }
            
            if (my >= 20 && my <= 65) {
                if (mx >= 20 && mx <= 52) {
                    vesa_create_window(60, 40, 500, 400, "File Manager", 0);
                    goto draw_frame;
                }
                if (mx >= 100 && mx <= 132) {
                    vesa_create_window(120, 80, 420, 320, "Notepad", 1);
                    goto draw_frame;
                }
                if (mx >= 180 && mx <= 212) {
                    vesa_create_window(180, 120, 250, 280, "Calculator", 3);
                    goto draw_frame;
                }
                if (mx >= 260 && mx <= 292) {
                    vesa_create_window(200, 160, 420, 260, "Terminal", 4);
                    goto draw_frame;
                }
                if (mx >= 340 && mx <= 372) {
                    vesa_create_window(160, 100, 440, 340, "Paint", 5);
                    goto draw_frame;
                }
            }
            
            if (power_dialog) {
                if (mx >= 387 && mx < 462 && my >= 402 && my < 430) {
                    for (int i = 0; i < 1024 * 768; i++) {
                        vesa_fb_ptr[i] = backbuffer[i];
                    }
                    vesa_wait_vsync();
                    vesa_exit();
                    vesa_outb(0x64, 0xFE);
                    return;
                }
                if (mx >= 482 && mx < 557 && my >= 402 && my < 430) {
                    for (int i = 0; i < 1024 * 768; i++) {
                        vesa_fb_ptr[i] = backbuffer[i];
                    }
                    vesa_wait_vsync();
                    vesa_exit();
                    vesa_outb(0x64, 0xFE);
                    return;
                }
                if (mx >= 587 && mx < 662 && my >= 402 && my < 430) {
                    power_dialog = 0;
                }
                goto draw_frame;
            }
            
            if (start_menu) {
                int sy = 768 - 36 - 280;
                for (int i = 0; i < 8; i++) {
                    int iy = sy + 8 + i * 28;
                    if (mx >= 32 && mx < 202 && my >= iy && my < iy + 24) {
                        start_menu = 0;
                        switch (i) {
                            case 0: vesa_create_window(60, 40, 500, 400, "File Manager", 0); fm_refresh(); break;
                            case 1: vesa_create_window(120, 80, 420, 320, "Notepad", 1); break;
                            case 2: vesa_create_window(180, 120, 250, 280, "Calculator", 3); break;
                            case 3: vesa_create_window(200, 160, 420, 260, "Terminal", 4); break;
                            case 4: vesa_create_window(160, 100, 440, 340, "Paint", 5); break;
                            case 5: vesa_create_window(300, 200, 300, 200, "Clock", 6); break;
                            case 6: vesa_create_window(250, 150, 400, 350, "Settings", 7); break;
                            case 7: power_dialog = 1; break;
                        }
                        break;
                    }
                }
                goto draw_frame;
            }
            
            for (int i = vesa_win_count - 1; i >= 0; i--) {
                if (vesa_wins[i].visible && vesa_wins[i].type == 7) {
                    vesa_window_t* w = &vesa_wins[i];
                    int wx = w->x, wy = w->y, ww = w->w, wh = w->h;
                    
                    for (int t = 0; t < 5; t++) {
                        if (mx >= wx + 4 && mx < wx + 120 &&
                            my >= wy + 44 + t * 26 && my < wy + 44 + t * 26 + 22) {
                            settings_theme_tab = t;
                            goto draw_frame;
                        }
                    }
                    
                    if (settings_theme_tab == 0) {
                        for (int t = 0; t < 3; t++) {
                            if (mx >= wx + 140 && mx < wx + 300 &&
                                my >= wy + 48 + t * 26 && my < wy + 48 + t * 26 + 22) {
                                settings.theme = t;
                                goto draw_frame;
                            }
                        }
                    }
                    
                    if (settings_theme_tab == 1) {
                        for (int w2 = 0; w2 < 5; w2++) {
                            if (mx >= wx + 140 && mx < wx + 240 &&
                                my >= wy + 48 + w2 * 22 && my < wy + 48 + w2 * 22 + 18) {
                                settings.wallpaper = w2;
                                goto draw_frame;
                            }
                        }
                    }
                    
                    if (settings_theme_tab == 2) {
                        for (int s = 0; s < 3; s++) {
                            if (mx >= wx + 140 && mx < wx + 240 &&
                                my >= wy + 48 + s * 26 && my < wy + 48 + s * 26 + 22) {
                                settings.screensaver_type = s;
                                goto draw_frame;
                            }
                        }
                        if (mx >= wx + 220 && mx < wx + 260 &&
                            my >= wy + 130 && my < wy + 148) {
                            settings.screensaver_enabled = !settings.screensaver_enabled;
                            goto draw_frame;
                        }
                    }
                    
                    if (settings_theme_tab == 3) {
                        if (mx >= wx + 220 && mx < wx + 260 &&
                            my >= wy + 48 && my < wy + 66) {
                            settings.clock_24h = !settings.clock_24h;
                            goto draw_frame;
                        }
                        if (mx >= wx + 240 && mx < wx + 280 &&
                            my >= wy + 68 && my < wy + 86) {
                            settings.show_seconds = !settings.show_seconds;
                            goto draw_frame;
                        }
                    }
                    
                    if (mx >= wx + ww - 60 && mx < wx + ww - 10 &&
                        my >= wy + wh - 28 && my < wy + wh - 8) {
                        save_settings();
                        goto draw_frame;
                    }
                    break;
                }
            }
            
            for (int i = vesa_win_count - 1; i >= 0; i--) {
                if (!vesa_wins[i].visible) continue;
                
                int wx = vesa_wins[i].x;
                int wy = vesa_wins[i].y;
                int ww = vesa_wins[i].w;
                int wh = vesa_wins[i].h;
                
                if (mx >= wx + ww - 20 && mx < wx + ww - 6 && my >= wy + 4 && my < wy + 16) {
                    vesa_wins[i].visible = 0;
                    goto draw_frame;
                }
                
                if (mx >= wx + ww - 38 && mx < wx + ww - 24 && my >= wy + 4 && my < wy + 16) {
                    vesa_wins[i].visible = 0;
                    goto draw_frame;
                }
                
                if (mx >= wx && mx < wx + ww && my >= wy && my < wy + 20) {
                    vesa_bring_to_front(i);
                    drag_win = i;
                    drag_off_x = mx - wx;
                    drag_off_y = my - wy;
                    goto draw_frame;
                }
                
                if (mx >= wx && mx < wx + ww && my >= wy + 20 && my < wy + wh) {
                    vesa_bring_to_front(i);
                    
                    if (vesa_wins[i].type == 0) {
                        int fy = (my - wy - 44) / 18;
                        if (fy >= 0 && fy < current_file_count) {
                            file_selected = fy;
                        }
                    }
                    
                    if (vesa_wins[i].type == 3) {
                        int col = (mx - wx - 8) / 46;
                        int row = (my - wy - 58) / 26;
                        if (col >= 0 && col < 4 && row >= 0 && row < 4) {
                            const char* btns = "789/456*123-0.=";
                            char btn = btns[row * 4 + col];
                            if (btn >= '0' && btn <= '9') {
                                if (calc_new) {
                                    calc_value = btn - '0';
                                    calc_new = 0;
                                } else {
                                    calc_value = calc_value * 10 + (btn - '0');
                                }
                            } else if (btn == '+' || btn == '-' || btn == '*' || btn == '/') {
                                calc_op = btn;
                                calc_new = 1;
                            } else if (btn == '=') {
                                calc_new = 1;
                            }
                        }
                    }
                    
                    goto draw_frame;
                }
            }
        }
        
        if (key != 5 && key != 11 && key != 7 && key != 8) {
            last_click = 0;
        }
        
        if (drag_win != -1 && (vesa_inb(0x64) & 1) == 0) {
            vesa_wins[drag_win].x = mx - drag_off_x;
            vesa_wins[drag_win].y = my - drag_off_y;
        } else {
            drag_win = -1;
        }
        
        draw_frame:
        
        uint16_t bg_top, bg_bottom;
        if (settings.theme == 2) {
            bg_top = WN_RGB565(35, 35, 55);
            bg_bottom = WN_RGB565(15, 15, 30);
        } else {
            bg_top = WN_RGB565(80, 140, 210);
            bg_bottom = WN_RGB565(0, 50, 130);
        }
        wn_vesa_gradient_v(0, 0, 1024, 768 - 36, bg_top, bg_bottom);
        
        if (settings.wallpaper == 1) {
            for (int i = 0; i < 100; i++) {
                int sx = (i * 67 + frame / 2) % 1024;
                int sy = (i * 43) % 768;
                wn_vesa_pixel(sx, sy, WN_WHITE);
            }
        }
        if (settings.wallpaper == 2) {
            for (int y = 0; y < 768; y += 20) {
                for (int x = 0; x < 1024; x += 20) {
                    wn_vesa_pixel(x, y, WN_DKGRAY);
                }
            }
        }
        if (settings.wallpaper == 3) {
            wn_vesa_gradient_v(0, 0, 1024, 768 - 36, WN_RGB565(0, 80, 160), WN_RGB565(0, 20, 60));
        }
        if (settings.wallpaper == 4) {
            for (int i = 0; i < 80; i++) {
                int y2 = (i * 7 + frame) % 768;
                wn_vesa_text((i * 13) % 1024, y2, frame % 2 ? "0" : "1", WN_GREEN);
            }
        }
        
        draw_icon(20, 20, 0);
        wn_vesa_text(25, 52, "Files", WN_WHITE);
        draw_icon(100, 20, 1);
        wn_vesa_text(105, 52, "Notepad", WN_WHITE);
        draw_icon(180, 20, 2);
        wn_vesa_text(185, 52, "Calculator", WN_WHITE);
        draw_icon(260, 20, 3);
        wn_vesa_text(265, 52, "Terminal", WN_WHITE);
        draw_icon(340, 20, 4);
        wn_vesa_text(345, 52, "Paint", WN_WHITE);
        
        vesa_draw_windows();
        
        draw_start_menu();
        draw_power_dialog();
        if (first_setup) {
            draw_first_setup();
        }
        
        draw_taskbar();
        
        char time_str[16];
        int h = clock_h % 12;
        if (h == 0) h = 12;
        time_str[0] = '0' + (h / 10);
        time_str[1] = '0' + (h % 10);
        time_str[2] = ':';
        time_str[3] = '0' + (clock_m / 10);
        time_str[4] = '0' + (clock_m % 10);
        if (settings.show_seconds) {
            time_str[5] = ':';
            time_str[6] = '0' + (clock_s / 10);
            time_str[7] = '0' + (clock_s % 10);
            time_str[8] = 0;
        } else {
            time_str[5] = 0;
        }
        wn_vesa_text(970, 768 - 28, time_str, WN_WHITE);
        
        for (int i = 0; i < 10; i++) {
            if (mx + i < 1024) {
                backbuffer[my * 1024 + (mx + i)] = WN_WHITE;
            }
            if (my + i < 768) {
                backbuffer[(my + i) * 1024 + mx] = WN_WHITE;
            }
        }
        backbuffer[my * 1024 + mx] = WN_BLACK;
        
        for (int i = 0; i < 1024 * 768; i++) {
            vesa_fb_ptr[i] = backbuffer[i];
        }
        
        vesa_wait_vsync();
        frame++;
    }
}