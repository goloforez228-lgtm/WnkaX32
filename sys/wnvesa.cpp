#include "wnvesa.h"
#include "vesa.h"
#include "ata.h"
#include "kernel_stubs.h"
#include "mouse.h"
#include "wnx.h"
#include <stdint.h>

extern uint8_t inb(uint16_t port);
extern bool shift_pressed;
extern const char kbd_us[128];
extern const char kbd_us_shift[128];

static uint16_t backbuffer[1024 * 768];
static uint16_t* vesa_fb_ptr = 0;
static int vesa_mx = 512, vesa_my = 384;
static int mouse_sensitivity = 3;
static int cpu_speed_mhz = 100;
static int frame_counter = 0;

static int start_menu = 0, power_dialog = 0, first_setup = 0;
static int context_menu = 0, context_menu_x = 0, context_menu_y = 0, context_menu_item = -1;
static int clock_h = 14, clock_m = 45, clock_s = 0;
static int drag_win = -1, drag_off_x = 0, drag_off_y = 0;
static int settings_theme_tab = 0;
static int calc_value = 0, calc_new = 1;
static char calc_op = 0;
static int screensaver_active = 0, idle_frames = 0;
static int last_vesa_mx = 512, last_vesa_my = 384;
static int ss_frame = 0;
static float ss_logo_x = 512.0f, ss_logo_y = 384.0f;
static float ss_logo_dx = 2.5f, ss_logo_dy = 1.8f;
static int last_click = 0, last_right_click = 0;

typedef struct {
    char magic[4];
    int theme, wallpaper, taskbar_auto_hide, clock_24h, show_seconds;
    int screensaver_enabled, screensaver_timeout, screensaver_type, animation_enabled;
    char username[32], computer_name[32];
    int first_run, vesa_width, vesa_height, vesa_bpp, mouse_sensitivity;
    int clock_show_date, animations, font_size, taskbar_position;
    int timezone, language, color_scheme, icon_size;
} vesa_settings_t;
static vesa_settings_t settings;

#define MAX_FILES 64
typedef struct { char name[32]; int is_dir; int size; } file_entry_t;
static file_entry_t current_files[MAX_FILES];
static int current_file_count = 0, file_selected = 0;
static char current_path[256] = "/";
static char clipboard_path[256] = "";
static int clipboard_valid = 0;
static int clipboard_is_cut = 0;
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
#define MIZU_TITLEBAR  0x1F5F
#define MIZU_TITLEBAR_TEXT 0xFFFF
#define MIZU_BORDER    0x10A2
#define MIZU_WIN_BG    0xFFFF
#define MIZU_TASKBAR   0x18E3
#define CLASSIC_TITLEBAR 0x001F
#define CLASSIC_WIN_BG 0xC618
#define DARK_TITLEBAR  0x18E3
#define DARK_WIN_BG    0x2104
#define NORD_BG        0x1F3F5F
#define NORD_WIN       0x2E4A6E  
#define NORD_TITLE     0x3B68A3
#define NORD_ACTIVE    0x5E88B8
#define NORD_TASKBAR   0x1F3F5F
#define WN_RGB565(r, g, b) ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

static void str_cpy(char* d, const char* s) { while(*s){*d=*s;d++;s++;} *d=0; }
static int str_len(const char* s) { int l=0; while(*s++)l++; return l; }
static int str_cmp(const char* a, const char* b) { while(*a&&*b&&*a==*b){a++;b++;} return *a-*b; }
static void int_to_str(int n, char* s) { 
    if(n==0){s[0]='0';s[1]=0;return;} 
    char t[12]; int i=0; int num=n<0?-n:n;
    while(num>0){t[i++]='0'+(num%10);num/=10;} 
    int j=0; if(n<0)s[j++]='-';
    while(i>0)s[j++]=t[--i]; 
    s[j]=0; 
}
static void str_cat(char* d, const char* s) {
    while(*d) d++;
    while(*s) { *d = *s; d++; s++; }
    *d = 0;
}
static void my_sprintf(char* buf, const char* fmt, ...) {
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

static void fm_refresh(void) {
    current_file_count = 0;
    file_selected = 0;
    uint16_t dir_buf[256];
    if(current_path[0] == '/' && current_path[1] == 0) {
        read_sector(100, dir_buf);
    } else {
        int sector = 100;
        char* p = current_path + 1;
        while(*p) {
            char name[12] = {0};
            int i = 0;
            while(*p && *p != '/' && i < 11) name[i++] = *p++;
            if(*p == '/') p++;
            read_sector(sector, dir_buf);
            int found = -1;
            for(int j = 0; j < 32; j++) {
                char n[12] = {0};
                for(int k = 0; k < 11; k++) n[k] = ((char*)dir_buf)[j*16 + k];
                if(str_cmp(n, name) == 0 && ((char*)dir_buf)[j*16 + 11] == 1) {
                    found = j;
                    break;
                }
            }
            if(found == -1) {
                sector = 100;
                current_path[0] = '/';
                current_path[1] = 0;
                read_sector(100, dir_buf);
                break;
            }
            sector = dir_buf[found*8 + 6];
        }
        read_sector(sector, dir_buf);
    }
    if(current_path[0] != '/' || current_path[1] != 0) {
        str_cpy(current_files[current_file_count].name, "..");
        current_files[current_file_count].is_dir = 1;
        current_files[current_file_count].size = 0;
        current_file_count++;
    }
    for(int i = 0; i < 32 && current_file_count < MAX_FILES; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
        if(name[0] != 0 && name[0] != 0xE5) {
            str_cpy(current_files[current_file_count].name, name);
            current_files[current_file_count].is_dir = (((char*)dir_buf)[i*16 + 11] == 1);
            current_files[current_file_count].size = dir_buf[i*8 + 7];
            current_file_count++;
        }
    }
}

static void fm_change_dir(const char* name) {
    if(str_cmp(name, "..") == 0) {
        int len = str_len(current_path);
        if(len > 1) {
            int i = len - 2;
            while(i > 0 && current_path[i] != '/') i--;
            if(i <= 0) {
                current_path[0] = '/';
                current_path[1] = 0;
            } else {
                current_path[i + 1] = 0;
            }
        }
    } else {
        int len = str_len(current_path);
        if(current_path[len - 1] != '/') {
            current_path[len] = '/';
            current_path[len + 1] = 0;
            len++;
        }
        str_cpy(current_path + len, name);
    }
    fm_refresh();
}

static void fm_create_file(const char* name, const char* content) {
    uint16_t dir_buf[256];
    int sector = 100;
    if(current_path[0] != '/' || current_path[1] != 0) {
        char* p = current_path + 1;
        while(*p) {
            char folder[12] = {0};
            int i = 0;
            while(*p && *p != '/' && i < 11) folder[i++] = *p++;
            if(*p == '/') p++;
            read_sector(sector, dir_buf);
            for(int j = 0; j < 32; j++) {
                char n[12] = {0};
                for(int k = 0; k < 11; k++) n[k] = ((char*)dir_buf)[j*16 + k];
                if(str_cmp(n, folder) == 0 && ((char*)dir_buf)[j*16 + 11] == 1) {
                    sector = dir_buf[j*8 + 6];
                    break;
                }
            }
        }
    }
    read_sector(sector, dir_buf);
    int slot = -1;
    for(int i = 0; i < 32; i++) {
        if(((char*)dir_buf)[i*16] == 0) {
            slot = i;
            break;
        }
    }
    if(slot != -1) {
        for(int j = 0; j < 11 && name[j]; j++) ((char*)dir_buf)[slot*16 + j] = name[j];
        ((char*)dir_buf)[slot*16 + 11] = 0;
        int flen = str_len(content);
        uint16_t dbuf[256] = {0};
        for(int i = 0; i < flen && i < 510; i++) {
            if(i % 2 == 0) dbuf[i/2] = content[i];
            else dbuf[i/2] |= (content[i] << 8);
        }
        static int file_counter = 500;
        int fs = file_counter++;
        write_sector(fs, dbuf);
        dir_buf[slot*8 + 6] = fs;
        dir_buf[slot*8 + 7] = flen;
        write_sector(sector, dir_buf);
        fm_refresh();
    }
}

static void fm_create_dir(const char* name) {
    uint16_t dir_buf[256];
    int sector = 100;
    if(current_path[0] != '/' || current_path[1] != 0) {
        char* p = current_path + 1;
        while(*p) {
            char folder[12] = {0};
            int i = 0;
            while(*p && *p != '/' && i < 11) folder[i++] = *p++;
            if(*p == '/') p++;
            read_sector(sector, dir_buf);
            for(int j = 0; j < 32; j++) {
                char n[12] = {0};
                for(int k = 0; k < 11; k++) n[k] = ((char*)dir_buf)[j*16 + k];
                if(str_cmp(n, folder) == 0 && ((char*)dir_buf)[j*16 + 11] == 1) {
                    sector = dir_buf[j*8 + 6];
                    break;
                }
            }
        }
    }
    read_sector(sector, dir_buf);
    int slot = -1;
    for(int i = 0; i < 32; i++) {
        if(((char*)dir_buf)[i*16] == 0) {
            slot = i;
            break;
        }
    }
    if(slot != -1) {
        for(int j = 0; j < 11 && name[j]; j++) ((char*)dir_buf)[slot*16 + j] = name[j];
        ((char*)dir_buf)[slot*16 + 11] = 1;
        static int dir_counter = 300;
        int ds = dir_counter++;
        dir_buf[slot*8 + 6] = ds;
        dir_buf[slot*8 + 7] = 0;
        write_sector(sector, dir_buf);
        uint16_t empty[256];
        for(int i = 0; i < 256; i++) empty[i] = 0;
        write_sector(ds, empty);
        fm_refresh();
    }
}

static void fm_delete_file(const char* name) {
    uint16_t dir_buf[256];
    int sector = 100;
    if(current_path[0] != '/' || current_path[1] != 0) {
        char* p = current_path + 1;
        while(*p) {
            char folder[12] = {0};
            int i = 0;
            while(*p && *p != '/' && i < 11) folder[i++] = *p++;
            if(*p == '/') p++;
            read_sector(sector, dir_buf);
            for(int j = 0; j < 32; j++) {
                char n[12] = {0};
                for(int k = 0; k < 11; k++) n[k] = ((char*)dir_buf)[j*16 + k];
                if(str_cmp(n, folder) == 0 && ((char*)dir_buf)[j*16 + 11] == 1) {
                    sector = dir_buf[j*8 + 6];
                    break;
                }
            }
        }
    }
    read_sector(sector, dir_buf);
    for(int i = 0; i < 32; i++) {
        char fname[12] = {0};
        for(int j = 0; j < 11; j++) fname[j] = ((char*)dir_buf)[i*16 + j];
        if(str_cmp(name, fname) == 0) {
            for(int j = 0; j < 16; j++) ((char*)dir_buf)[i*16 + j] = 0;
            write_sector(sector, dir_buf);
            break;
        }
    }
    fm_refresh();
}

static void fm_copy_file(const char* name) {
    str_cpy(clipboard_path, current_path);
    str_cat(clipboard_path, "/");
    str_cat(clipboard_path, name);
    clipboard_valid = 1;
    clipboard_is_cut = 0;
}

static void fm_cut_file(const char* name) {
    str_cpy(clipboard_path, current_path);
    str_cat(clipboard_path, "/");
    str_cat(clipboard_path, name);
    clipboard_valid = 1;
    clipboard_is_cut = 1;
}

static void fm_paste_file(void) {
    if(!clipboard_valid) return;
    const char* name = clipboard_path;
    const char* last_slash = name;
    while(*name) {
        if(*name == '/') last_slash = name + 1;
        name++;
    }
    uint16_t src_dir_buf[256];
    char src_path[256];
    str_cpy(src_path, clipboard_path);
    int last = str_len(src_path) - 1;
    while(last > 0 && src_path[last] != '/') last--;
    if(last > 0) src_path[last] = 0;
    else src_path[0] = '/', src_path[1] = 0;
    int src_sector = 100;
    if(src_path[0] != '/' || src_path[1] != 0) {
        char* p = src_path + 1;
        while(*p && *p != '/') {
            char folder[12] = {0};
            int i = 0;
            while(*p && *p != '/' && i < 11) folder[i++] = *p++;
            if(*p == '/') p++;
            read_sector(src_sector, src_dir_buf);
            for(int j = 0; j < 32; j++) {
                char n[12] = {0};
                for(int k = 0; k < 11; k++) n[k] = ((char*)src_dir_buf)[j*16 + k];
                if(str_cmp(n, folder) == 0 && ((char*)src_dir_buf)[j*16 + 11] == 1) {
                    src_sector = src_dir_buf[j*8 + 6];
                    break;
                }
            }
        }
    }
    read_sector(src_sector, src_dir_buf);
    int src_slot = -1;
    for(int i = 0; i < 32; i++) {
        char n[12] = {0};
        for(int j = 0; j < 11; j++) n[j] = ((char*)src_dir_buf)[i*16 + j];
        if(str_cmp(n, last_slash) == 0) {
            src_slot = i;
            break;
        }
    }
    if(src_slot == -1) return;
    int src_file_sector = src_dir_buf[src_slot*8 + 6];
    int src_file_size = src_dir_buf[src_slot*8 + 7];
    int is_dir = ((char*)src_dir_buf)[src_slot*16 + 11] == 1;
    uint16_t data_buf[256];
    if(!is_dir) read_sector(src_file_sector, data_buf);
    fm_create_file(last_slash, is_dir ? "" : (const char*)data_buf);
    if(clipboard_is_cut) {
        if(is_dir) {
            for(int j = 0; j < 16; j++) ((char*)src_dir_buf)[src_slot*16 + j] = 0;
            write_sector(src_sector, src_dir_buf);
        } else {
            for(int j = 0; j < 16; j++) ((char*)src_dir_buf)[src_slot*16 + j] = 0;
            write_sector(src_sector, src_dir_buf);
        }
        clipboard_valid = 0;
    }
    fm_refresh();
}

static void save_settings(void) {
    str_cpy(settings.magic, "WNKA"); 
    settings.first_run = 0;
    uint16_t buf[256]; 
    uint8_t* d = (uint8_t*)&settings;
    for(int i = 0; i < 256; i++) { 
        if(i * 2 < (int)sizeof(settings)) buf[i] = (d[i*2] | (d[i*2+1] << 8)); 
        else buf[i] = 0; 
    }
    write_sector(SETTINGS_SECTOR, buf);
}

static void load_settings(void) {
    uint16_t buf[256]; 
    read_sector(SETTINGS_SECTOR, buf);
    uint8_t* d = (uint8_t*)&settings;
    for(int i = 0; i < (int)sizeof(settings); i++) { 
        if(i % 2 == 0) d[i] = buf[i/2] & 0xFF; 
        else d[i] = (buf[i/2] >> 8) & 0xFF; 
    }
    if(settings.magic[0] != 'W' || settings.magic[1] != 'N' || settings.magic[2] != 'K' || settings.magic[3] != 'A'){
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
        settings.vesa_width = 1024; 
        settings.vesa_height = 768; 
        settings.vesa_bpp = 16; 
        settings.mouse_sensitivity = 3;
        settings.clock_show_date = 0; 
        settings.animations = 1; 
        settings.font_size = 1; 
        settings.taskbar_position = 3;
        settings.timezone = 3; 
        settings.language = 0; 
        settings.color_scheme = 0; 
        settings.icon_size = 1;
        str_cpy(settings.username, "User"); 
        str_cpy(settings.computer_name, "WNKA-PC"); 
        save_settings();
    }
    mouse_sensitivity = settings.mouse_sensitivity;
}

void wn_vesa_init(void) { 
    vesa_enable(); 
    vesa_fb_ptr = (uint16_t*)vesa_fb; 
    for(int i = 0; i < 1024 * 768; i++) backbuffer[i] = 0; 
}

void wn_vesa_pixel(int x, int y, uint16_t c) { 
    if(x >= 0 && x < 1024 && y >= 0 && y < 768) backbuffer[y * 1024 + x] = c; 
}

void wn_vesa_rect(int x, int y, int w, int h, uint16_t c) { 
    if(x < 0) { w += x; x = 0; } 
    if(y < 0) { h += y; y = 0; } 
    if(x + w > 1024) w = 1024 - x; 
    if(y + h > 768) h = 768 - y; 
    if(w <= 0 || h <= 0) return; 
    for(int dy = 0; dy < h; dy++) { 
        uint16_t* l = backbuffer + (y + dy) * 1024 + x; 
        for(int dx = 0; dx < w; dx++) l[dx] = c; 
    } 
}

void wn_vesa_clear(uint16_t c) { 
    for(int i = 0; i < 1024 * 768; i++) backbuffer[i] = c; 
}

static void wn_vesa_hline(int x, int y, int w, uint16_t c) { 
    if(y < 0 || y >= 768) return; 
    if(x < 0) { w += x; x = 0; } 
    if(x + w > 1024) w = 1024 - x; 
    if(w <= 0) return; 
    uint16_t* l = backbuffer + y * 1024 + x; 
    for(int i = 0; i < w; i++) l[i] = c; 
}

void wn_vesa_gradient_v(int x, int y, int w, int h, uint16_t c1, uint16_t c2) { 
    if(h <= 0) return; 
    int r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F; 
    int r2 = (c2 >> 11) & 0x1F, g2 = (c2 >> 5) & 0x3F, b2 = c2 & 0x1F; 
    for(int dy = 0; dy < h; dy++) { 
        int dr = r1 + (r2 - r1) * dy / h;
        int dg = g1 + (g2 - g1) * dy / h;
        int db = b1 + (b2 - b1) * dy / h;
        uint16_t color = (dr << 11) | (dg << 5) | db; 
        wn_vesa_hline(x, y + dy, w, color); 
    } 
}

static const uint8_t font_8x8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00},
    {0x6C,0x6C,0x6C,0x00,0x00,0x00,0x00,0x00},
    {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00},
    {0x18,0x7E,0xC0,0x7C,0x06,0xFC,0x18,0x00},
    {0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00},
    {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00},
    {0x30,0x30,0x60,0x00,0x00,0x00,0x00,0x00},
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30},
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00},
    {0x7C,0xCE,0xDE,0xF6,0xE6,0xC6,0x7C,0x00},
    {0x18,0x38,0x78,0x18,0x18,0x18,0x7E,0x00},
    {0x7C,0xC6,0x06,0x0C,0x18,0x30,0xFE,0x00},
    {0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00},
    {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x00},
    {0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00},
    {0x38,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00},
    {0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30,0x00},
    {0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00},
    {0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00},
    {0x00,0x00,0x18,0x00,0x00,0x18,0x00,0x00},
    {0x00,0x00,0x18,0x00,0x00,0x18,0x18,0x30},
    {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00},
    {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00},
    {0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00},
    {0x7C,0xC6,0x0C,0x18,0x18,0x00,0x18,0x00},
    {0x7C,0xC6,0xDE,0xDE,0xDE,0xC0,0x7C,0x00},
    {0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0x00},
    {0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00},
    {0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00},
    {0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00},
    {0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00},
    {0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0x00},
    {0x3C,0x66,0xC0,0xC0,0xCE,0x66,0x3E,0x00},
    {0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00},
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x7E,0x00},
    {0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00},
    {0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00},
    {0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00},
    {0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00},
    {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00},
    {0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00},
    {0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00},
    {0x7C,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x06},
    {0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00},
    {0x7C,0xC6,0xE0,0x78,0x0E,0xC6,0x7C,0x00},
    {0xFF,0xDB,0x99,0x18,0x18,0x18,0x3C,0x00},
    {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00},
    {0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00},
    {0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00},
    {0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00},
    {0xC3,0xC3,0x66,0x3C,0x18,0x18,0x3C,0x00},
    {0xFE,0xC6,0x8C,0x18,0x32,0x66,0xFE,0x00},
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00},
    {0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00},
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00},
    {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
    {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x78,0x0C,0x7C,0xCC,0x76,0x00},
    {0xE0,0x60,0x7C,0x66,0x66,0x66,0xDC,0x00},
    {0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00},
    {0x1C,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00},
    {0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00},
    {0x38,0x6C,0x60,0xF0,0x60,0x60,0xF0,0x00},
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x78},
    {0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00},
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00},
    {0x06,0x00,0x0E,0x06,0x06,0x66,0x66,0x3C},
    {0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00},
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    {0x00,0x00,0xEC,0xFE,0xD6,0xC6,0xC6,0x00},
    {0x00,0x00,0xF8,0xCC,0xCC,0xCC,0xCC,0x00},
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00},
    {0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0},
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E},
    {0x00,0x00,0xDC,0x76,0x60,0x60,0xF0,0x00},
    {0x00,0x00,0x7C,0xC0,0x7C,0x06,0xFC,0x00},
    {0x10,0x30,0xFC,0x30,0x30,0x34,0x18,0x00},
    {0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00},
    {0x00,0x00,0xC6,0xC6,0x6C,0x38,0x10,0x00},
    {0x00,0x00,0xC6,0xD6,0xFE,0xEE,0xC6,0x00},
    {0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00},
    {0x00,0x00,0xC6,0xC6,0x7E,0x06,0xFC,0x00},
    {0x00,0x00,0xFE,0x8C,0x18,0x32,0xFE,0x00},
    {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00},
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},
    {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00}
};

void wn_vesa_char(int x, int y, char ch, uint16_t c) { 
    int idx = ((unsigned char)ch) - 32; 
    if(idx < 0 || idx >= 95) return; 
    const uint8_t* bm = font_8x8[idx]; 
    for(int r = 0; r < 8; r++) { 
        uint8_t m = bm[r]; 
        for(int cl = 0; cl < 8; cl++) { 
            if(m & (0x80 >> cl)) wn_vesa_pixel(x + cl, y + r, c); 
        } 
    } 
}

void wn_vesa_text(int x, int y, const char* s, uint16_t c) { 
    int cx = x; 
    int cy = y; 
    while(*s) { 
        if(*s == '\n') { 
            cx = x; 
            cy += 10; 
            s++;
            continue; 
        } 
        wn_vesa_char(cx, cy, *s, c); 
        cx += 9; 
        s++; 
    } 
}

static void draw_icon(int x, int y, int t){
    if(t==0){ 
        wn_vesa_rect(x+4,y+2,28,22,WN_BLACK); 
        wn_vesa_rect(x+5,y+3,26,20,WN_BLUE); 
        wn_vesa_rect(x+6,y+4,24,18,WN_CYAN); 
        wn_vesa_rect(x+12,y+24,12,4,WN_GRAY); 
        wn_vesa_rect(x+8,y+28,20,4,WN_GRAY); 
        wn_vesa_pixel(x+30,y+8,WN_GREEN); 
    }
    if(t==1){ 
        wn_vesa_rect(x+4,y+2,28,30,WN_WHITE); 
        wn_vesa_rect(x+5,y+3,26,6,WN_BLUE); 
        for(int i=0;i<5;i++) wn_vesa_hline(x+8,y+14+i*3,16,WN_GRAY); 
    }
    if(t==2){ 
        wn_vesa_rect(x+4,y+6,28,26,WN_GRAY); 
        wn_vesa_rect(x+6,y+8,24,8,WN_WHITE); 
        wn_vesa_text(x+8,y+9,"12",WN_BLACK); 
    }
    if(t==3){ 
        wn_vesa_rect(x+4,y+4,28,28,WN_BLACK); 
        wn_vesa_text(x+8,y+8,">_",WN_GREEN); 
    }
    if(t==4){ 
        wn_vesa_rect(x+4,y+4,28,28,WN_WHITE); 
        wn_vesa_rect(x+8,y+8,8,8,WN_RED); 
        wn_vesa_pixel(x+20,y+8,WN_BLUE); 
    }
}

static void draw_window_buttons(int x, int y, int w){
    int btn_w = 34, btn_h = 20, btn_y = y + 3;
    int cx_btn = x + w - btn_w - 6;
    wn_vesa_gradient_v(cx_btn, btn_y, btn_w, btn_h, WN_RGB565(220,65,65), WN_RGB565(170,20,20)); 
    wn_vesa_rect(cx_btn, btn_y, btn_w, btn_h, WN_DKGRAY);
    wn_vesa_hline(cx_btn + 1, btn_y + 1, btn_w - 2, WN_RGB565(255,180,180));
    int cxx = cx_btn + btn_w / 2, cyy = btn_y + btn_h / 2; 
    for(int i = -3; i <= 3; i++) { 
        wn_vesa_pixel(cxx + i, cyy + i, WN_WHITE); 
        wn_vesa_pixel(cxx + i, cyy - i, WN_WHITE); 
    }
    int max_btn = cx_btn - btn_w - 4; 
    wn_vesa_gradient_v(max_btn, btn_y, btn_w, btn_h, WN_RGB565(180,180,180), WN_RGB565(140,140,140)); 
    wn_vesa_rect(max_btn, btn_y, btn_w, btn_h, WN_DKGRAY);
    wn_vesa_hline(max_btn + 1, btn_y + 1, btn_w - 2, WN_RGB565(230,230,230)); 
    wn_vesa_rect(max_btn + 10, btn_y + 5, 13, 9, WN_BLACK);
    int min_btn = max_btn - btn_w - 4; 
    wn_vesa_gradient_v(min_btn, btn_y, btn_w, btn_h, WN_RGB565(180,180,180), WN_RGB565(140,140,140)); 
    wn_vesa_rect(min_btn, btn_y, btn_w, btn_h, WN_DKGRAY);
    wn_vesa_hline(min_btn + 1, btn_y + 1, btn_w - 2, WN_RGB565(230,230,230)); 
    wn_vesa_hline(min_btn + 10, btn_y + btn_h - 6, btn_w - 20, WN_BLACK);
}

typedef struct { 
    int x, y, w, h; 
    char title[32]; 
    int visible, active, type, z_order; 
    int minimized; 
    int orig_x, orig_y, orig_w, orig_h; 
    int maximized; 
    char term_buf[32][128]; 
    int term_lines, term_cx, term_cy, term_scroll; 
    char input_buf[256]; 
    int input_pos; 
    char notepad_text[20][64]; 
    int notepad_lines;
    int notepad_scroll;
} vesa_window_t;

static vesa_window_t vesa_wins[10];
static int vesa_win_count = 0, vesa_top_z = 0;

static void draw_context_menu(void){
    if(!context_menu) return;
    int mx = context_menu_x, my = context_menu_y;
    const char* items[] = {"New Folder", "New File", "Refresh", "Copy", "Cut", "Paste", "Delete", "Run WNX", "Properties"};
    int count = 9;
    int w = 140, h = count * 24 + 8;
    if(mx + w > 1024) mx = 1024 - w;
    if(my + h > 768) my = 768 - h;
    wn_vesa_rect(mx + 3, my + 3, w, h, 0x4228);
    wn_vesa_gradient_v(mx, my, w, h, WN_RGB565(240,240,245), WN_RGB565(220,220,230));
    wn_vesa_rect(mx, my, w, h, WN_GRAY);
    for(int i = 0; i < count; i++){
        int iy = my + 4 + i * 22;
        uint16_t color = WN_BLACK;
        int is_selected = (i == context_menu_item);
        if(is_selected) {
            wn_vesa_gradient_v(mx + 2, iy - 2, w - 4, 20, WN_RGB565(60,140,240), WN_RGB565(40,100,200));
            color = WN_WHITE;
        }
        if((i == 3 || i == 4 || i == 5) && !clipboard_valid) color = WN_GRAY;
        wn_vesa_text(mx + 10, iy + 2, items[i], color);
        wn_vesa_hline(mx + 4, iy + 20, w - 8, WN_SILVER);
    }
}

static void draw_window_mizu(int x, int y, int w, int h, const char* title, int active){
    for(int dy = 2; dy < h + 2; dy++) { backbuffer[(y + dy) * 1024 + (x + w + 1)] = 0x4228; backbuffer[(y + dy) * 1024 + (x + w + 2)] = 0x2104; }
    for(int dx = 2; dx < w + 2; dx++) { backbuffer[(y + h + 1) * 1024 + (x + dx)] = 0x4228; backbuffer[(y + h + 2) * 1024 + (x + dx)] = 0x2104; }
    wn_vesa_rect(x, y, w, h, MIZU_BORDER); 
    if(active) wn_vesa_gradient_v(x + 1, y + 1, w - 2, 22, MIZU_TITLEBAR, WN_RGB565(0,40,120)); 
    else wn_vesa_gradient_v(x + 1, y + 1, w - 2, 22, WN_RGB565(180,180,180), WN_RGB565(150,150,150));
    wn_vesa_rect(x + 1, y + 23, w - 2, h - 24, MIZU_WIN_BG); 
    wn_vesa_text(x + 8, y + 5, title, active ? MIZU_TITLEBAR_TEXT : WN_BLACK); 
    draw_window_buttons(x, y, w);
}

static void draw_window_classic(int x, int y, int w, int h, const char* title, int active){
    for(int dy = 2; dy < h + 2; dy++) { backbuffer[(y + dy) * 1024 + (x + w + 1)] = 0x4228; backbuffer[(y + dy) * 1024 + (x + w + 2)] = 0x2104; }
    for(int dx = 2; dx < w + 2; dx++) { backbuffer[(y + h + 1) * 1024 + (x + dx)] = 0x4228; backbuffer[(y + h + 2) * 1024 + (x + dx)] = 0x2104; }
    wn_vesa_rect(x, y, w, h, WN_DKGRAY); 
    wn_vesa_rect(x + 1, y + 1, w - 2, h - 2, CLASSIC_WIN_BG); 
    if(active) wn_vesa_rect(x + 1, y + 1, w - 2, 20, CLASSIC_TITLEBAR); 
    else wn_vesa_rect(x + 1, y + 1, w - 2, 20, WN_GRAY);
    wn_vesa_text(x + 6, y + 4, title, WN_WHITE); 
    draw_window_buttons(x, y, w);
}

static void draw_window_dark(int x, int y, int w, int h, const char* title, int active){
    for(int dy = 2; dy < h + 2; dy++) { backbuffer[(y + dy) * 1024 + (x + w + 1)] = 0x4228; backbuffer[(y + dy) * 1024 + (x + w + 2)] = 0x2104; }
    for(int dx = 2; dx < w + 2; dx++) { backbuffer[(y + h + 1) * 1024 + (x + dx)] = 0x4228; backbuffer[(y + h + 2) * 1024 + (x + dx)] = 0x2104; }
    wn_vesa_rect(x, y, w, h, WN_BLACK); 
    wn_vesa_rect(x + 1, y + 1, w - 2, h - 2, DARK_WIN_BG); 
    if(active) wn_vesa_gradient_v(x + 1, y + 1, w - 2, 22, DARK_TITLEBAR, WN_RGB565(10,10,25)); 
    else wn_vesa_rect(x + 1, y + 1, w - 2, 22, WN_DKGRAY);
    wn_vesa_text(x + 6, y + 4, title, WN_SILVER); 
    draw_window_buttons(x, y, w);
}

static void draw_window_nord(int x, int y, int w, int h, const char* title, int active){
    for(int dy = 2; dy < h + 2; dy++) { backbuffer[(y + dy) * 1024 + (x + w + 1)] = 0x4228; backbuffer[(y + dy) * 1024 + (x + w + 2)] = 0x2104; }
    for(int dx = 2; dx < w + 2; dx++) { backbuffer[(y + h + 1) * 1024 + (x + dx)] = 0x4228; backbuffer[(y + h + 2) * 1024 + (x + dx)] = 0x2104; }
    wn_vesa_rect(x, y, w, h, NORD_BG); 
    if(active) wn_vesa_gradient_v(x + 1, y + 1, w - 2, 22, NORD_TITLE, NORD_ACTIVE); 
    else wn_vesa_gradient_v(x + 1, y + 1, w - 2, 22, WN_RGB565(80,100,120), WN_RGB565(60,80,100));
    wn_vesa_rect(x + 1, y + 23, w - 2, h - 24, NORD_WIN); 
    wn_vesa_text(x + 8, y + 5, title, active ? WN_WHITE : WN_SILVER); 
    draw_window_buttons(x, y, w);
}

static void draw_window(int x, int y, int w, int h, const char* title, int active){
    if(settings.theme == 4) draw_window_nord(x, y, w, h, title, active);
    else if(settings.theme == 3) draw_window_mizu(x, y, w, h, title, active);
    else if(settings.theme == 1) draw_window_classic(x, y, w, h, title, active);
    else if(settings.theme == 2) draw_window_dark(x, y, w, h, title, active);
    else { 
        for(int dy = 2; dy < h + 2; dy++) { backbuffer[(y + dy) * 1024 + (x + w + 1)] = 0x4228; backbuffer[(y + dy) * 1024 + (x + w + 2)] = 0x2104; } 
        for(int dx = 2; dx < w + 2; dx++) { backbuffer[(y + h + 1) * 1024 + (x + dx)] = 0x4228; backbuffer[(y + h + 2) * 1024 + (x + dx)] = 0x2104; }
        wn_vesa_rect(x, y, w, h, WN_DKGRAY); 
        wn_vesa_rect(x + 1, y + 1, w - 2, h - 2, WN_LTGRAY); 
        for(int i = 0; i < 20; i++) { 
            uint16_t g = active ? WN_RGB565(0,50 + i * 8,160 + i * 4) : WN_RGB565(120,120,140 + i * 3); 
            wn_vesa_hline(x + 1, y + 1 + i, w - 2, g); 
        }
        if(active) wn_vesa_hline(x + 2, y + 3, w - 4, WN_RGB565(180,220,255)); 
        wn_vesa_text(x + 6, y + 5, title, WN_WHITE); 
        draw_window_buttons(x, y, w); 
    }
}

static void draw_taskbar(void){
    int ty = 768 - 36;
    if(settings.theme == 4) { 
        wn_vesa_gradient_v(0, ty, 1024, 36, NORD_TASKBAR, WN_RGB565(15,35,55)); 
        wn_vesa_hline(0, ty, 1024, WN_RGB565(50,80,120)); 
    }
    else if(settings.theme == 3) { 
        wn_vesa_gradient_v(0, ty, 1024, 36, MIZU_TASKBAR, WN_RGB565(5,5,30)); 
        wn_vesa_hline(0, ty, 1024, WN_RGB565(30,60,120)); 
    }
    else if(settings.theme == 1) { 
        wn_vesa_rect(0, ty, 1024, 36, WN_GRAY); 
        wn_vesa_rect(0, ty, 1024, 36, WN_BLACK); 
        wn_vesa_hline(1, ty + 1, 1022, WN_WHITE); 
    }
    else if(settings.theme == 2) { 
        wn_vesa_gradient_v(0, ty, 1024, 36, WN_RGB565(45,45,65), WN_RGB565(25,25,40)); 
        wn_vesa_hline(0, ty, 1024, WN_RGB565(80,80,100)); 
    }
    else { 
        wn_vesa_gradient_v(0, ty, 1024, 36, WN_RGB565(40,80,160), WN_RGB565(20,40,100)); 
        wn_vesa_hline(0, ty, 1024, WN_SILVER); 
        wn_vesa_hline(0, ty + 1, 1024, WN_RGB565(100,150,220)); 
    }
    if(settings.theme == 4) { 
        wn_vesa_gradient_v(2, ty + 2, 96, 32, NORD_TITLE, NORD_ACTIVE); 
        wn_vesa_rect(2, ty + 2, 96, 32, WN_DKGRAY); 
        wn_vesa_text(24, ty + 11, "Start", WN_WHITE); 
    }
    else if(settings.theme == 3) { 
        wn_vesa_gradient_v(2, ty + 2, 96, 32, WN_RGB565(0,80,200), WN_RGB565(0,40,140)); 
        wn_vesa_rect(2, ty + 2, 96, 32, WN_DKGRAY); 
        wn_vesa_text(24, ty + 11, "Start", WN_WHITE); 
    }
    else if(settings.theme == 1) { 
        wn_vesa_rect(2, ty + 3, 90, 30, WN_DKGRAY); 
        wn_vesa_text(22, ty + 10, "Start", WN_WHITE); 
    }
    else if(settings.theme == 2) { 
        wn_vesa_gradient_v(2, ty + 3, 90, 30, WN_RGB565(70,70,100), WN_RGB565(40,40,60)); 
        wn_vesa_text(22, ty + 10, "Start", WN_WHITE); 
    }
    else { 
        wn_vesa_gradient_v(2, ty + 2, 96, 32, WN_RGB565(60,160,240), WN_RGB565(30,100,180)); 
        wn_vesa_rect(2, ty + 2, 96, 32, WN_DKGRAY); 
        wn_vesa_hline(3, ty + 3, 94, WN_RGB565(140,220,255)); 
        wn_vesa_text(24, ty + 10, "Start", WN_WHITE); 
    }
    int btn_x = 105;
    for(int i = 0; i < vesa_win_count; i++){
        if(!vesa_wins[i].visible) continue;
        int bw = 130, bh = 28;
        if(vesa_wins[i].active && !vesa_wins[i].minimized){
            wn_vesa_gradient_v(btn_x, ty + 4, bw, bh, WN_RGB565(80,180,255), WN_RGB565(30,120,200));
        } else {
            wn_vesa_gradient_v(btn_x, ty + 4, bw, bh, WN_RGB565(60,100,160), WN_RGB565(30,60,110));
        }
        wn_vesa_rect(btn_x, ty + 4, bw, bh, WN_DKGRAY);
        char t[18]; 
        int j = 0;
        while(vesa_wins[i].title[j] && j < 15) { t[j] = vesa_wins[i].title[j]; j++; } 
        t[j] = 0;
        wn_vesa_text(btn_x + 6, ty + 11, t, WN_WHITE);
        btn_x += 134;
        if(btn_x > 900) break;
    }
    char ts[16]; 
    int h = settings.clock_24h ? clock_h : (clock_h % 12);
    if(h == 0 && !settings.clock_24h) h = 12;
    ts[0] = '0' + (h / 10);
    ts[1] = '0' + (h % 10);
    ts[2] = ':';
    ts[3] = '0' + (clock_m / 10);
    ts[4] = '0' + (clock_m % 10);
    if(settings.show_seconds){
        ts[5] = ':';
        ts[6] = '0' + (clock_s / 10);
        ts[7] = '0' + (clock_s % 10);
        ts[8] = 0;
    } else {
        ts[5] = 0;
    }
    wn_vesa_text(970, ty + 10, ts, WN_WHITE);
}

static void draw_screensaver(void){ 
    ss_logo_x += ss_logo_dx; 
    ss_logo_y += ss_logo_dy; 
    if(ss_logo_x < 50 || ss_logo_x > 974) ss_logo_dx = -ss_logo_dx; 
    if(ss_logo_y < 50 || ss_logo_y > 718) ss_logo_dy = -ss_logo_dy; 
    wn_vesa_clear(WN_BLACK);
    if(settings.screensaver_type == 0){ 
        for(int i = 0; i < 8; i++){ 
            float a = i * 0.785f + ss_frame * 0.05f; 
            float s = a - (a*a*a)/6.0f + (a*a*a*a*a)/120.0f; 
            float c2 = (a + 1.57f) - ((a + 1.57f)*(a + 1.57f)*(a + 1.57f))/6.0f; 
            wn_vesa_pixel((int)(ss_logo_x + 30 * s), (int)(ss_logo_y + 30 * c2), WN_RED); 
            wn_vesa_pixel((int)(ss_logo_x + 50 * s), (int)(ss_logo_y + 50 * c2), WN_RED); 
        } 
        wn_vesa_text((int)ss_logo_x - 30, (int)ss_logo_y - 10, "WNKA", WN_WHITE); 
    }
    else if(settings.screensaver_type == 1){
        for(int i = 0; i < 200; i++) wn_vesa_pixel((ss_frame * 3 + i * 13) % 1024, (i * 7 + ss_frame) % 768, WN_GREEN);
    }
    else {
        for(int i = 0; i < 100; i++) wn_vesa_text((i * 10) % 1024, (i * 23 + ss_frame * 3) % 768, ss_frame % 2 ? "0" : "1", WN_GREEN); 
    }
    ss_frame++; 
}

static void draw_start_menu(void){ 
    if(!start_menu) return; 
    int sx = 4, sy = 768 - 36 - 310, sw = 250, sh = 310; 
    wn_vesa_rect(sx + 3, sy + 3, sw, sh, 0x4228); 
    wn_vesa_gradient_v(sx, sy, sw, sh, WN_RGB565(240,240,245), WN_RGB565(220,220,230)); 
    wn_vesa_rect(sx, sy, sw, sh, WN_GRAY); 
    wn_vesa_gradient_v(sx + 1, sy + 1, 28, sh - 2, WN_RGB565(0,80,200), WN_RGB565(0,30,120));
    wn_vesa_text(sx + 6, sy + 18, "W", WN_WHITE); 
    wn_vesa_text(sx + 6, sy + 42, "N", WN_WHITE); 
    wn_vesa_text(sx + 6, sy + 66, "K", WN_WHITE); 
    wn_vesa_text(sx + 6, sy + 90, "A", WN_WHITE);
    const char* items[] = {"File Manager", "Notepad", "Calculator", "Terminal", "Paint", "Settings", "Power Off"};
    for(int i = 0; i < 7; i++){ 
        int iy = sy + 8 + i * 38; 
        if(vesa_mx >= sx + 30 && vesa_mx < sx + sw - 4 && vesa_my >= iy - 4 && vesa_my < iy + 32){ 
            wn_vesa_gradient_v(sx + 30, iy - 2, sw - 34, 32, WN_RGB565(60,140,240), WN_RGB565(40,100,200)); 
            wn_vesa_text(sx + 36, iy + 6, items[i], WN_WHITE); 
        } else {
            wn_vesa_text(sx + 36, iy + 6, items[i], WN_BLACK); 
        }
        wn_vesa_hline(sx + 30, iy + 29, sw - 34, WN_SILVER); 
    } 
}

static void draw_power_dialog(void){ 
    if(!power_dialog) return; 
    for(int y = 0; y < 768; y++) for(int x = 0; x < 1024; x += 2) backbuffer[y * 1024 + x] = ((backbuffer[y * 1024 + x] >> 2) & 0x39E7); 
    int dx = 362, dy = 334, dw = 300, dh = 140; 
    wn_vesa_rect(dx + 3, dy + 3, dw, dh, 0x4228); 
    wn_vesa_rect(dx, dy, dw, dh, WN_LTGRAY); 
    wn_vesa_gradient_v(dx + 1, dy + 1, dw - 2, 24, WN_RGB565(0,80,200), WN_RGB565(0,30,120)); 
    wn_vesa_text(dx + 70, dy + 6, "Turn off computer", WN_WHITE); 
    wn_vesa_text(dx + 45, dy + 50, "Reboot", WN_BLACK); 
    wn_vesa_text(dx + 130, dy + 50, "Shutdown", WN_BLACK); 
    wn_vesa_text(dx + 225, dy + 50, "Cancel", WN_BLACK); 
    for(int b = 0; b < 3; b++){ 
        int bx = dx + 25 + b * 95; 
        wn_vesa_rect(bx, dy + 68, 75, 28, WN_LTGRAY); 
        wn_vesa_hline(bx + 1, dy + 69, 73, WN_WHITE); 
    } 
}

static void draw_first_setup(void){ 
    int wx = 200, wy = 100, ww = 624, wh = 560; 
    for(int y = 0; y < 768; y++) for(int x = 0; x < 1024; x += 2) backbuffer[y * 1024 + x] = ((backbuffer[y * 1024 + x] >> 2) & 0x39E7); 
    wn_vesa_rect(wx + 4, wy + 4, ww, wh, 0x4228); 
    wn_vesa_rect(wx, wy, ww, wh, WN_LTGRAY); 
    wn_vesa_gradient_v(wx + 1, wy + 1, ww - 2, 30, WN_RGB565(0,80,200), WN_RGB565(0,30,120)); 
    wn_vesa_text(wx + ww/2 - 80, wy + 8, "Welcome to WNKA OS Setup", WN_WHITE);
    wn_vesa_text(wx + 40, wy + 50, "Username:", WN_BLACK); 
    wn_vesa_rect(wx + 180, wy + 48, 250, 24, WN_WHITE); 
    wn_vesa_text(wx + 185, wy + 53, settings.username, WN_BLACK);
    wn_vesa_text(wx + 40, wy + 82, "Computer:", WN_BLACK); 
    wn_vesa_rect(wx + 180, wy + 80, 250, 24, WN_WHITE); 
    wn_vesa_text(wx + 185, wy + 85, settings.computer_name, WN_BLACK);
    wn_vesa_text(wx + 40, wy + 114, "Theme:", WN_BLACK); 
    const char* themes[] = {"WNKA Standard", "Classic", "Dark", "Mizu (Win10)", "Nord"}; 
    for(int i = 0; i < 5; i++){ 
        int tx = wx + 180 + i * 90; 
        if(settings.theme == i) wn_vesa_rect(tx, wy + 110, 80, 22, WN_BLUE); 
        else wn_vesa_rect(tx, wy + 110, 80, 22, WN_LTGRAY); 
        wn_vesa_text(tx + 5, wy + 115, themes[i], settings.theme == i ? WN_WHITE : WN_BLACK); 
    }
    wn_vesa_text(wx + 40, wy + 146, "Wallpaper:", WN_BLACK); 
    const char* walls[] = {"Gradient", "Stars", "Grid", "Waves", "Matrix"}; 
    for(int i = 0; i < 5; i++){ 
        int w2 = wx + 180 + i * 90; 
        if(settings.wallpaper == i) wn_vesa_rect(w2, wy + 142, 80, 22, WN_BLUE); 
        else wn_vesa_rect(w2, wy + 142, 80, 22, WN_LTGRAY); 
        wn_vesa_text(w2 + 5, wy + 147, walls[i], settings.wallpaper == i ? WN_WHITE : WN_BLACK); 
    }
    wn_vesa_text(wx + 40, wy + 178, "Screensaver:", WN_BLACK); 
    const char* ss[] = {"Bounce", "Stars", "Matrix"}; 
    for(int i = 0; i < 3; i++){ 
        int sx2 = wx + 180 + i * 100; 
        if(settings.screensaver_type == i) wn_vesa_rect(sx2, wy + 174, 90, 22, WN_BLUE); 
        else wn_vesa_rect(sx2, wy + 174, 90, 22, WN_LTGRAY); 
        wn_vesa_text(sx2 + 5, wy + 179, ss[i], settings.screensaver_type == i ? WN_WHITE : WN_BLACK); 
    }
    wn_vesa_text(wx + 40, wy + 210, "Resolution:", WN_BLACK); 
    int res[][3] = {{1024,768,16},{1280,720,16},{800,600,16},{640,480,16},{1920,1080,16}}; 
    for(int i = 0; i < 5; i++){ 
        int rx = wx + 140 + (i % 3) * 130, ry = wy + 206 + (i / 3) * 22; 
        char rs[24]; 
        int p = 0; 
        int_to_str(res[i][0], rs); 
        while(rs[p]) p++; 
        rs[p++] = 'x'; 
        char t[8]; 
        int_to_str(res[i][1], t); 
        int q = 0; 
        while(t[q]) rs[p++] = t[q++]; 
        rs[p++] = '@'; 
        int_to_str(res[i][2], t); 
        q = 0; 
        while(t[q]) rs[p++] = t[q++]; 
        rs[p++] = 'b'; rs[p++] = 'p'; rs[p++] = 'p'; 
        rs[p] = 0; 
        if(settings.vesa_width == res[i][0] && settings.vesa_height == res[i][1] && settings.vesa_bpp == res[i][2]){ 
            wn_vesa_rect(rx, ry - 2, str_len(rs) * 9 + 8, 18, WN_BLUE); 
            wn_vesa_text(rx + 3, ry, rs, WN_WHITE); 
        } else {
            wn_vesa_text(rx + 3, ry, rs, WN_BLACK); 
        }
    }
    wn_vesa_text(wx + 40, wy + 270, "Mouse Speed:", WN_BLACK); 
    char sens[4]; 
    int_to_str(settings.mouse_sensitivity, sens); 
    wn_vesa_text(wx + 180, wy + 270, sens, WN_GREEN); 
    wn_vesa_rect(wx + 200, wy + 268, 30, 22, WN_GRAY); 
    wn_vesa_text(wx + 208, wy + 271, "-", WN_BLACK); 
    wn_vesa_rect(wx + 240, wy + 268, 30, 22, WN_GRAY); 
    wn_vesa_text(wx + 248, wy + 271, "+", WN_BLACK);
    wn_vesa_text(wx + 40, wy + 310, "Time Zone:", WN_BLACK); 
    const char* tz[] = {"UTC-12", "UTC-8", "UTC-5", "UTC+0", "UTC+3", "UTC+8", "UTC+12"}; 
    for(int i = 0; i < 7; i++){ 
        int tzx = wx + 180 + i * 80; 
        if(settings.timezone == i - 3) wn_vesa_rect(tzx, wy + 308, 70, 22, WN_BLUE); 
        else wn_vesa_rect(tzx, wy + 308, 70, 22, WN_LTGRAY); 
        wn_vesa_text(tzx + 5, wy + 313, tz[i], settings.timezone == i - 3 ? WN_WHITE : WN_BLACK); 
    }
    wn_vesa_text(wx + 40, wy + 350, "Icon Size:", WN_BLACK); 
    const char* isz[] = {"Small", "Normal", "Large"}; 
    for(int i = 0; i < 3; i++){ 
        int izx = wx + 180 + i * 90; 
        if(settings.icon_size == i) wn_vesa_rect(izx, wy + 348, 80, 22, WN_BLUE); 
        else wn_vesa_rect(izx, wy + 348, 80, 22, WN_LTGRAY); 
        wn_vesa_text(izx + 5, wy + 353, isz[i], settings.icon_size == i ? WN_WHITE : WN_BLACK); 
    }
    wn_vesa_text(wx + 40, wy + 390, "Animations:", WN_BLACK); 
    wn_vesa_rect(wx + 180, wy + 388, 60, 22, settings.animations ? WN_GREEN : WN_RED); 
    wn_vesa_text(wx + 185, wy + 393, settings.animations ? "ON" : "OFF", WN_WHITE);
    wn_vesa_text(wx + 40, wy + 430, "Clock:", WN_BLACK); 
    wn_vesa_rect(wx + 180, wy + 428, 80, 22, settings.clock_24h ? WN_BLUE : WN_LTGRAY); 
    wn_vesa_text(wx + 185, wy + 433, settings.clock_24h ? "24-Hour" : "12-Hour", WN_WHITE);
    int bx = wx + ww/2 - 50, by = wy + wh - 50; 
    wn_vesa_gradient_v(bx, by, 100, 30, WN_RGB565(60,180,60), WN_RGB565(30,120,30)); 
    wn_vesa_rect(bx, by, 100, 30, WN_DKGRAY); 
    wn_vesa_text(bx + 25, by + 8, "Let's Go!", WN_WHITE); 
}

typedef struct {
    char path[256];
    char name[32];
    char author[32];
    char desc[128];
    uint32_t icon;
    int sector;
} wnx_gui_entry_t;

static wnx_gui_entry_t wnx_gui_found[100];
static int wnx_gui_count = 0;

static void wnx_scan_directory(uint16_t dir_sector, const char* current_path) {
    uint16_t dir_buf[256];
    read_sector(dir_sector, dir_buf);
    for(int i = 0; i < 32 && wnx_gui_count < 100; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
        if(name[0] == 0 || name[0] == 0xE5) continue;
        int is_dir = ((char*)dir_buf)[i*16 + 11] == 1;
        int file_sector = dir_buf[i*8 + 6];
        if(is_dir) {
            char new_path[256];
            str_cpy(new_path, current_path);
            if(new_path[str_len(new_path)-1] != '/') str_cat(new_path, "/");
            str_cat(new_path, name);
            wnx_scan_directory(file_sector, new_path);
        } else {
            uint16_t hdr_buf[256];
            read_sector(file_sector, hdr_buf);
            uint32_t magic = hdr_buf[0] | (hdr_buf[1] << 16);
            if(magic == 0x31584E57) {
                wnx_gui_entry_t* e = &wnx_gui_found[wnx_gui_count];
                str_cpy(e->path, current_path);
                if(current_path[str_len(current_path)-1] != '/') str_cat(e->path, "/");
                str_cat(e->path, name);
                e->sector = file_sector;
                e->icon = hdr_buf[38] | (hdr_buf[39] << 16);
                for(int j = 0; j < 16 && j < 32; j++) {
                    if(j % 2 == 0) e->name[j] = hdr_buf[16 + j/2] & 0xFF;
                    else e->name[j] = (hdr_buf[16 + j/2] >> 8) & 0xFF;
                }
                e->name[31] = 0;
                for(int j = 0; j < 16 && j < 32; j++) {
                    if(j % 2 == 0) e->author[j] = hdr_buf[32 + j/2] & 0xFF;
                    else e->author[j] = (hdr_buf[32 + j/2] >> 8) & 0xFF;
                }
                e->author[31] = 0;
                for(int j = 0; j < 64 && j < 128; j++) {
                    if(j % 2 == 0) e->desc[j] = hdr_buf[48 + j/2] & 0xFF;
                    else e->desc[j] = (hdr_buf[48 + j/2] >> 8) & 0xFF;
                }
                e->desc[127] = 0;
                wnx_gui_count++;
            }
        }
    }
}

static void run_wnx_from_gui(const char* full_path) {
    const char* filename = full_path;
    const char* last_slash = filename;
    while(*filename) {
        if(*filename == '/') last_slash = filename + 1;
        filename++;
    }
    uint16_t saved_buf[1024*768];
    for(int i = 0; i < 1024*768; i++) saved_buf[i] = backbuffer[i];
    vesa_exit();
    wnx_context_t ctx;
    if(wnx_load(last_slash, &ctx) == 0) {
        ctx.args[0] = 0;
        wnx_execute(&ctx);
    }
    wn_vesa_init();
    vesa_enable();
    load_settings();
    fm_refresh();
    for(int i = 0; i < 1024*768; i++) backbuffer[i] = saved_buf[i];
}

static int vesa_create_window(int x, int y, int w, int h, const char* title, int type){
    if(vesa_win_count >= 10) return -1; 
    vesa_window_t* win = &vesa_wins[vesa_win_count]; 
    win->x = x; win->y = y; win->w = w; win->h = h; 
    win->visible = 1; win->type = type; win->minimized = 0; win->maximized = 0;
    win->orig_x = x; win->orig_y = y; win->orig_w = w; win->orig_h = h;
    int i = 0; 
    while(title[i] && i < 31) { win->title[i] = title[i]; i++; } 
    win->title[i] = 0; 
    for(int j = 0; j < vesa_win_count; j++) vesa_wins[j].active = 0; 
    win->active = 1; 
    win->z_order = vesa_top_z++;
    if(type == 8){ 
        for(int r = 0; r < 32; r++) for(int c = 0; c < 128; c++) win->term_buf[r][c] = ' '; 
        str_cpy(win->term_buf[0], "WNKA VESA Terminal"); 
        str_cpy(win->term_buf[1], "Type 'help' for commands"); 
        win->term_lines = 2; 
        win->term_cx = 0; win->term_cy = 2; win->term_scroll = 0; 
        win->input_buf[0] = 0; win->input_pos = 0; 
    }
    if(type == 1){ 
        for(int l = 0; l < 20; l++) win->notepad_text[l][0] = 0; 
        str_cpy(win->notepad_text[0], "Welcome to WNKA Notepad!"); 
        win->notepad_lines = 1;
        win->notepad_scroll = 0;
    }
    vesa_win_count++; 
    return vesa_win_count - 1;
}

static void vesa_bring_to_front(int idx){ 
    for(int j = 0; j < vesa_win_count; j++) vesa_wins[j].active = 0; 
    vesa_wins[idx].active = 1; 
    vesa_wins[idx].z_order = vesa_top_z++; 
}

static void terminal_exec(vesa_window_t* win, const char* cmd){
    if(win->term_lines < 32){ 
        str_cpy(win->term_buf[win->term_lines], "> "); 
        str_cpy(win->term_buf[win->term_lines] + 2, cmd); 
        win->term_lines++; 
    }
    if(str_cmp(cmd, "help") == 0){ 
        if(win->term_lines < 32) str_cpy(win->term_buf[win->term_lines++], "cls time about ls calc exit help"); 
    }
    else if(str_cmp(cmd, "cls") == 0 || str_cmp(cmd, "clear") == 0){ 
        for(int i = 0; i < 32; i++) for(int j = 0; j < 128; j++) win->term_buf[i][j] = ' '; 
        win->term_lines = 0; 
    }
    else if(str_cmp(cmd, "time") == 0){ 
        if(win->term_lines < 32){ 
            char b[64]; 
            my_sprintf(b, "%02d:%02d:%02d", clock_h, clock_m, clock_s);
            str_cpy(win->term_buf[win->term_lines++], b); 
        } 
    }
    else if(str_cmp(cmd, "about") == 0){ 
        if(win->term_lines < 32) str_cpy(win->term_buf[win->term_lines++], "WNKA OS VESA Terminal v1.0"); 
    }
    else if(str_cmp(cmd, "ls") == 0){ 
        fm_refresh(); 
        for(int i = 0; i < current_file_count && win->term_lines < 32; i++){ 
            char b[64]; 
            my_sprintf(b, "%s %s %d", current_files[i].is_dir ? "[DIR]" : "[FILE]", current_files[i].name, current_files[i].size); 
            str_cpy(win->term_buf[win->term_lines++], b); 
        } 
    }
    else if(cmd[0]){ 
        if(win->term_lines < 32){ 
            char b[128]; 
            my_sprintf(b, "Unknown: %s", cmd); 
            str_cpy(win->term_buf[win->term_lines++], b); 
        } 
    }
    win->term_cy = win->term_lines; 
    win->term_scroll = (win->term_lines > 28) ? win->term_lines - 28 : 0;
}

static void vesa_draw_windows(void){
    int order[10], cnt = 0; 
    for(int i = 0; i < vesa_win_count; i++) if(vesa_wins[i].visible && !vesa_wins[i].minimized) order[cnt++] = i;
    for(int i = 0; i < cnt - 1; i++) for(int j = 0; j < cnt - 1 - i; j++) if(vesa_wins[order[j]].z_order > vesa_wins[order[j + 1]].z_order){ int t = order[j]; order[j] = order[j + 1]; order[j + 1] = t; }
    
    for(int o = 0; o < cnt; o++){ 
        int i = order[o]; 
        vesa_window_t* win = &vesa_wins[i]; 
        draw_window(win->x, win->y, win->w, win->h, win->title, win->active); 
        int wx = win->x, wy = win->y, ww = win->w, wh = win->h;
        
        switch(win->type){
            case 0: {
                fm_refresh(); 
                wn_vesa_text(wx + 10, wy + 28, "Path:", WN_BLACK); 
                wn_vesa_text(wx + 50, wy + 28, current_path, WN_BLUE);
                wn_vesa_hline(wx + 4, wy + 40, ww - 8, WN_GRAY);
                for(int f = 0; f < current_file_count && f < 18; f++){ 
                    int fy = wy + 44 + f * 18; 
                    if(f == file_selected) wn_vesa_rect(wx + 4, fy, ww - 8, 18, WN_BLUE);
                    wn_vesa_text(wx + 10, fy + 2, current_files[f].is_dir ? "[DIR]" : "[FILE]", current_files[f].is_dir ? WN_CYAN : WN_GRAY); 
                    wn_vesa_text(wx + 50, fy + 2, current_files[f].name, f == file_selected ? WN_WHITE : WN_BLACK); 
                }
                wn_vesa_rect(wx + 4, wy + wh - 28, 60, 20, WN_RED); 
                wn_vesa_text(wx + 10, wy + wh - 25, "Delete", WN_WHITE);
                wn_vesa_rect(wx + 70, wy + wh - 28, 60, 20, WN_GREEN); 
                wn_vesa_text(wx + 76, wy + wh - 25, "New", WN_WHITE);
                wn_vesa_rect(wx + 136, wy + wh - 28, 60, 20, WN_BLUE); 
                wn_vesa_text(wx + 142, wy + wh - 25, "Copy", WN_WHITE);
                wn_vesa_rect(wx + 202, wy + wh - 28, 60, 20, WN_ORANGE); 
                wn_vesa_text(wx + 208, wy + wh - 25, "Cut", WN_WHITE);
                wn_vesa_rect(wx + 268, wy + wh - 28, 60, 20, WN_CYAN); 
                wn_vesa_text(wx + 274, wy + wh - 25, "Paste", WN_WHITE);
                break;
            }
            case 1: {
                wn_vesa_rect(wx + 4, wy + 22, ww - 8, wh - 28, WN_WHITE); 
                wn_vesa_text(wx + 8, wy + 26, "Notepad", WN_BLACK); 
                wn_vesa_hline(wx + 4, wy + 36, ww - 8, WN_GRAY);
                for(int t = win->notepad_scroll; t < win->notepad_lines && t < win->notepad_scroll + 18; t++){
                    wn_vesa_text(wx + 8, wy + 40 + (t - win->notepad_scroll) * 14, win->notepad_text[t], WN_BLACK);
                }
                wn_vesa_rect(wx + ww - 70, wy + wh - 28, 60, 20, WN_BLUE); 
                wn_vesa_text(wx + ww - 64, wy + wh - 25, "Save", WN_WHITE);
                break;
            }
            case 2: {
                wn_vesa_text(wx + 10, wy + 30, "Welcome!", WN_BLACK); 
                wn_vesa_text(wx + 10, wy + 50, "WNKA OS v6.0", WN_BLACK); 
                char cpus[16]; 
                int_to_str(cpu_speed_mhz, cpus); 
                wn_vesa_text(wx + 10, wy + 70, "CPU:", WN_BLACK); 
                wn_vesa_text(wx + 50, wy + 70, cpus, WN_GREEN); 
                wn_vesa_text(wx + 80, wy + 70, "MHz", WN_GREEN); 
                break;
            }
            case 3: {
                wn_vesa_rect(wx + 8, wy + 28, 180, 24, WN_WHITE); 
                char val[20]; 
                int_to_str(calc_value, val); 
                wn_vesa_text(wx + 170 - str_len(val) * 9, wy + 33, val, WN_BLACK); 
                const char* btns[] = {"7","8","9","/","4","5","6","*","1","2","3","-","0",".","=","+"}; 
                for(int b = 0; b < 16; b++){ 
                    int bx = wx + 8 + (b % 4) * 46, by = wy + 58 + (b / 4) * 26; 
                    wn_vesa_rect(bx, by, 42, 22, WN_GRAY); 
                    wn_vesa_text(bx + 18, by + 5, btns[b], WN_BLACK); 
                } 
                break;
            }
            case 7: {
                wn_vesa_gradient_v(wx + 4, wy + 24, 130, wh - 30, WN_RGB565(40,80,140), WN_RGB565(20,40,80)); 
                const char* tabs[] = {"Theme","Wallpaper","Screen","Clock","Mouse","TimeZone","Icons","Anim","About"}; 
                for(int t = 0; t < 9; t++){ 
                    int ty = wy + 44 + t * 24; 
                    if(t == settings_theme_tab) wn_vesa_rect(wx + 4, ty, 122, 22, WN_RGB565(0,120,220)); 
                    wn_vesa_text(wx + 10, ty + 4, tabs[t], t == settings_theme_tab ? WN_WHITE : WN_SILVER); 
                }
                if(settings_theme_tab == 0){ 
                    const char* th[] = {"WNKA Standard", "Classic", "Dark", "Mizu (Win10)", "Nord"}; 
                    for(int t = 0; t < 5; t++){ 
                        int ty2 = wy + 48 + t * 24; 
                        if(settings.theme == t) wn_vesa_rect(wx + 144, ty2, 150, 22, WN_BLUE); 
                        else wn_vesa_rect(wx + 144, ty2, 150, 22, WN_GRAY); 
                        wn_vesa_text(wx + 148, ty2 + 4, th[t], settings.theme == t ? WN_WHITE : WN_BLACK); 
                    } 
                }
                if(settings_theme_tab == 1){ 
                    const char* wl[] = {"Gradient", "Stars", "Grid", "Waves", "Matrix"}; 
                    for(int t = 0; t < 5; t++){ 
                        int ty2 = wy + 48 + t * 24; 
                        if(settings.wallpaper == t) wn_vesa_rect(wx + 144, ty2, 150, 22, WN_BLUE); 
                        else wn_vesa_rect(wx + 144, ty2, 150, 22, WN_GRAY); 
                        wn_vesa_text(wx + 148, ty2 + 4, wl[t], settings.wallpaper == t ? WN_WHITE : WN_BLACK); 
                    } 
                }
                if(settings_theme_tab == 2){ 
                    const char* ss[] = {"Bounce", "Stars", "Matrix"}; 
                    for(int t = 0; t < 3; t++){ 
                        int ty2 = wy + 48 + t * 24; 
                        if(settings.screensaver_type == t) wn_vesa_rect(wx + 144, ty2, 150, 22, WN_BLUE); 
                        else wn_vesa_rect(wx + 144, ty2, 150, 22, WN_GRAY); 
                        wn_vesa_text(wx + 148, ty2 + 4, ss[t], settings.screensaver_type == t ? WN_WHITE : WN_BLACK); 
                    } 
                }
                if(settings_theme_tab == 3){ 
                    wn_vesa_text(wx + 144, wy + 28, "24-Hour:", WN_BLACK); 
                    wn_vesa_rect(wx + 220, wy + 48, 80, 22, settings.clock_24h ? WN_GREEN : WN_RED); 
                    wn_vesa_text(wx + 226, wy + 53, settings.clock_24h ? "ON" : "OFF", WN_WHITE); 
                    wn_vesa_text(wx + 144, wy + 72, "Seconds:", WN_BLACK); 
                    wn_vesa_rect(wx + 220, wy + 92, 80, 22, settings.show_seconds ? WN_GREEN : WN_RED); 
                    wn_vesa_text(wx + 226, wy + 97, settings.show_seconds ? "ON" : "OFF", WN_WHITE); 
                }
                if(settings_theme_tab == 4){ 
                    wn_vesa_text(wx + 144, wy + 28, "Speed:", WN_BLACK); 
                    char s[4]; 
                    int_to_str(mouse_sensitivity, s); 
                    wn_vesa_text(wx + 200, wy + 48, s, WN_GREEN); 
                    wn_vesa_rect(wx + 144, wy + 68, 35, 24, WN_GRAY); 
                    wn_vesa_text(wx + 152, wy + 71, "-", WN_BLACK); 
                    wn_vesa_rect(wx + 185, wy + 68, 35, 24, WN_GRAY); 
                    wn_vesa_text(wx + 193, wy + 71, "+", WN_BLACK); 
                }
                if(settings_theme_tab == 5){ 
                    const char* tz[] = {"UTC-12", "UTC-8", "UTC-5", "UTC+0", "UTC+3", "UTC+8", "UTC+12"}; 
                    for(int t = 0; t < 7; t++){ 
                        int ty2 = wy + 48 + t * 24; 
                        if(settings.timezone == t - 3) wn_vesa_rect(wx + 144, ty2, 120, 22, WN_BLUE); 
                        else wn_vesa_rect(wx + 144, ty2, 120, 22, WN_GRAY); 
                        wn_vesa_text(wx + 148, ty2 + 4, tz[t], settings.timezone == t - 3 ? WN_WHITE : WN_BLACK); 
                    } 
                }
                if(settings_theme_tab == 6){ 
                    const char* isz[] = {"Small", "Normal", "Large"}; 
                    for(int t = 0; t < 3; t++){ 
                        int ty2 = wy + 48 + t * 24; 
                        if(settings.icon_size == t) wn_vesa_rect(wx + 144, ty2, 120, 22, WN_BLUE); 
                        else wn_vesa_rect(wx + 144, ty2, 120, 22, WN_GRAY); 
                        wn_vesa_text(wx + 148, ty2 + 4, isz[t], settings.icon_size == t ? WN_WHITE : WN_BLACK); 
                    } 
                }
                if(settings_theme_tab == 7){ 
                    wn_vesa_rect(wx + 144, wy + 68, 80, 22, settings.animations ? WN_GREEN : WN_RED); 
                    wn_vesa_text(wx + 150, wy + 73, settings.animations ? "ENABLED" : "DISABLED", WN_WHITE); 
                }
                if(settings_theme_tab == 8){ 
                    wn_vesa_text(wx + 144, wy + 28, "WNKA OS v6.0", WN_BLACK); 
                    wn_vesa_text(wx + 144, wy + 48, "VESA GUI Shell", WN_BLACK); 
                }
                wn_vesa_rect(wx + ww - 60, wy + wh - 28, 50, 20, WN_GRAY); 
                wn_vesa_text(wx + ww - 52, wy + wh - 24, "Apply", WN_BLACK); 
                break;
            }
            case 8: {
                wn_vesa_rect(wx + 4, wy + 22, ww - 8, wh - 28, WN_BLACK);
                for(int l = 0; l < 28 && l + win->term_scroll < win->term_lines; l++){
                    wn_vesa_text(wx + 8, wy + 24 + l * 10, win->term_buf[l + win->term_scroll], WN_GREEN);
                }
                wn_vesa_rect(wx + 4, wy + wh - 26, ww - 8, 18, WN_DKGRAY); 
                wn_vesa_text(wx + 8, wy + wh - 24, ">", WN_CYAN); 
                wn_vesa_text(wx + 17, wy + wh - 24, win->input_buf, WN_WHITE);
                if((frame_counter % 60) < 30){ 
                    char c[2] = {'_', 0}; 
                    wn_vesa_text(wx + 17 + win->input_pos * 9, wy + wh - 24, c, WN_WHITE); 
                } 
                break;
            }
            case 9: {
                static int app_scroll = 0;
                static int app_selected = 0;
                static int scanned = 0;
                static wnx_gui_entry_t wnx_local[100];
                static int wnx_local_count = 0;
                
                if(!scanned) {
                    wnx_local_count = 0;
                    for(int i = 0; i < wnx_gui_count && i < 100; i++) {
                        wnx_local[i] = wnx_gui_found[i];
                        wnx_local_count++;
                    }
                    scanned = 1;
                    app_selected = 0;
                    app_scroll = 0;
                }
                
                wn_vesa_rect(wx + 4, wy + 22, ww - 8, wh - 28, WN_DARK_BG);
                wn_vesa_text(wx + 10, wy + 30, "Applications on Disk:", WN_YELLOW);
                wn_vesa_hline(wx + 4, wy + 40, ww - 8, WN_GRAY);
                
                int apps_per_page = 5;
                int y_offset = 46;
                for(int a = 0; a < apps_per_page && app_scroll + a < wnx_local_count; a++) {
                    int idx = app_scroll + a;
                    int iy = wy + y_offset + a * 56;
                    if(vesa_mx >= wx + 6 && vesa_mx < wx + ww - 6 && vesa_my >= iy && vesa_my < iy + 50) {
                        app_selected = idx;
                        if(get_mouse_btn() & 1 && !last_click) {
                            run_wnx_from_gui(wnx_local[idx].path);
                            scanned = 0;
                        }
                    }
                    uint16_t bg = (idx == app_selected) ? WN_BLUE : WN_DARK_BG;
                    wn_vesa_rect(wx + 6, iy, ww - 12, 50, bg);
                    wn_vesa_rect(wx + 6, iy, ww - 12, 50, WN_GRAY);
                    uint32_t icon = wnx_local[idx].icon;
                    if(icon % 6 == 0) wn_vesa_rect(wx + 12, iy + 6, 32, 32, WN_BLUE);
                    else if(icon % 6 == 1) wn_vesa_rect(wx + 12, iy + 6, 32, 32, WN_GREEN);
                    else if(icon % 6 == 2) wn_vesa_rect(wx + 12, iy + 6, 32, 32, WN_RED);
                    else if(icon % 6 == 3) wn_vesa_rect(wx + 12, iy + 6, 32, 32, WN_ORANGE);
                    else wn_vesa_rect(wx + 12, iy + 6, 32, 32, WN_GRAY);
                    wn_vesa_text(wx + 50, iy + 8, wnx_local[idx].name, (idx == app_selected) ? WN_WHITE : WN_SILVER);
                    wn_vesa_text(wx + 50, iy + 22, "by ", WN_GRAY);
                    wn_vesa_text(wx + 65, iy + 22, wnx_local[idx].author, WN_CYAN);
                    char short_desc[42];
                    for(int d = 0; d < 40 && wnx_local[idx].desc[d]; d++) short_desc[d] = wnx_local[idx].desc[d];
                    short_desc[40] = 0;
                    wn_vesa_text(wx + 50, iy + 36, short_desc, (idx == app_selected) ? WN_WHITE : WN_GRAY);
                }
                if(app_scroll > 0) wn_vesa_text(wx + ww - 20, wy + 46, "^", WN_RED);
                if(app_scroll + apps_per_page < wnx_local_count) wn_vesa_text(wx + ww - 20, wy + wh - 40, "v", WN_RED);
                break;
            }
        }
    }
}

void wn_demo(void){
    wn_vesa_init(); 
    init_mouse(); 
    load_settings(); 
    fm_refresh();
    uint32_t st, en; 
    __asm__ volatile("rdtsc" : "=A"(st)); 
    for(volatile int i = 0; i < 1000000; i++) __asm__ volatile("nop"); 
    __asm__ volatile("rdtsc" : "=A"(en)); 
    cpu_speed_mhz = (en - st) / 1000000; 
    if(cpu_speed_mhz < 1) cpu_speed_mhz = 10;
    if(settings.first_run) first_setup = 1;
    vesa_create_window(50, 50, 400, 320, "My Computer", 0); 
    vesa_create_window(480, 80, 380, 280, "Welcome", 2);
    vesa_create_window(300, 150, 500, 450, "Applications", 9);
    int running = 1, frame = 0;
    int shift = 0;
    while(running){
        poll_mouse();
        if(mouse_present){ 
            vesa_mx = get_mouse_x(); 
            vesa_my = get_mouse_y(); 
            if(vesa_mx < 0) vesa_mx = 0; 
            if(vesa_mx > 1023) vesa_mx = 1023; 
            if(vesa_my < 0) vesa_my = 0; 
            if(vesa_my > 767) vesa_my = 767; 
        }
        static uint32_t last_sec = 0;
        if(frame % 60 == 0 || last_sec != (uint32_t)frame) {
            last_sec = frame;
            clock_s++;
            if(clock_s >= 60){ clock_s = 0; clock_m++; }
            if(clock_m >= 60){ clock_m = 0; clock_h++; }
            if(clock_h >= 24) clock_h = 0;
        }
        if(vesa_mx != last_vesa_mx || vesa_my != last_vesa_my){ 
            idle_frames = 0; 
            last_vesa_mx = vesa_mx; 
            last_vesa_my = vesa_my; 
        } else { idle_frames++; }
        if(settings.screensaver_enabled && idle_frames > settings.screensaver_timeout && !start_menu && !power_dialog && !first_setup && !context_menu) 
            screensaver_active = 1;
        if(screensaver_active){ 
            if(get_mouse_btn()){ screensaver_active = 0; idle_frames = 0; } 
            draw_screensaver(); 
            for(int i = 0; i < 10; i++){ 
                if(vesa_mx + i < 1024) backbuffer[vesa_my * 1024 + (vesa_mx + i)] = WN_WHITE; 
                if(vesa_my + i < 768) backbuffer[(vesa_my + i) * 1024 + vesa_mx] = WN_WHITE; 
            } 
            backbuffer[vesa_my * 1024 + vesa_mx] = WN_BLACK; 
            for(int i = 0; i < 1024 * 768; i++) vesa_fb_ptr[i] = backbuffer[i]; 
            vesa_wait_vsync(); 
            frame++; 
            continue; 
        }
        if(inb(0x64) & 1){
            uint8_t sc = inb(0x60);
            if(sc == 0x2A || sc == 0x36) { shift = 1; continue; }
            if(sc == 0xAA || sc == 0xB6) { shift = 0; continue; }
            if(sc == 0x01){
                if(power_dialog) power_dialog = 0;
                else if(start_menu) start_menu = 0;
                else if(context_menu) context_menu = 0;
                continue;
            }
            if(sc == 0x5B || sc == 0x5C || sc == 0x13){ start_menu = !start_menu; continue; }
            if(sc < 0x80){
                char ch = 0;
                const char* table = shift ? kbd_us_shift : kbd_us;
                if(sc < 128) ch = table[sc];
                int aw = -1;
                for(int i = vesa_win_count - 1; i >= 0; i--){ 
                    if(vesa_wins[i].visible && vesa_wins[i].active && !vesa_wins[i].minimized){ aw = i; break; } 
                }
                if(aw >= 0){
                    vesa_window_t* w = &vesa_wins[aw];
                    if(w->type == 8 && ch){
                        if(ch == '\n'){
                            w->input_buf[w->input_pos] = 0;
                            terminal_exec(w, w->input_buf);
                            w->input_buf[0] = 0;
                            w->input_pos = 0;
                        }
                        else if(ch == '\b' && w->input_pos > 0){
                            w->input_pos--;
                            w->input_buf[w->input_pos] = 0;
                        }
                        else if(ch >= 32 && ch <= 126 && w->input_pos < 250){
                            w->input_buf[w->input_pos++] = ch;
                            w->input_buf[w->input_pos] = 0;
                        }
                    }
                    if(w->type == 1 && ch){
                        int line = w->notepad_lines - 1;
                        if(line < 0){ line = 0; w->notepad_lines = 1; }
                        if(ch == '\n'){
                            if(w->notepad_lines < 19){
                                w->notepad_text[w->notepad_lines][0] = 0;
                                w->notepad_lines++;
                            }
                        }
                        else if(ch == '\b'){
                            int len = str_len(w->notepad_text[line]);
                            if(len > 0) w->notepad_text[line][len - 1] = 0;
                            else if(line > 0) w->notepad_lines--;
                        }
                        else if(ch >= 32 && ch <= 126){
                            int len = str_len(w->notepad_text[line]);
                            if(len < 63){
                                w->notepad_text[line][len] = ch;
                                w->notepad_text[line][len + 1] = 0;
                            }
                        }
                        if(w->notepad_lines > 18) w->notepad_scroll = w->notepad_lines - 18;
                    }
                }
            }
        }
        uint8_t mb = get_mouse_btn();
        uint8_t lb = mb & 1, rb = (mb >> 1) & 1;
        if(lb && !last_click){ 
            last_click = 1;
            if(context_menu){ 
                if(context_menu_item >= 0){
                    if(context_menu_item == 0) fm_create_dir("NewFolder");
                    if(context_menu_item == 1) fm_create_file("NewFile.txt", "");
                    if(context_menu_item == 2) fm_refresh();
                    if(context_menu_item == 3 && file_selected < current_file_count) fm_copy_file(current_files[file_selected].name);
                    if(context_menu_item == 4 && file_selected < current_file_count) fm_cut_file(current_files[file_selected].name);
                    if(context_menu_item == 5) fm_paste_file();
                    if(context_menu_item == 6 && file_selected < current_file_count) fm_delete_file(current_files[file_selected].name);
                    if(context_menu_item == 7 && file_selected < current_file_count) run_wnx_from_gui(current_files[file_selected].name);
                }
                context_menu = 0;
                goto draw_frame;
            }
            int tby = 768 - 36;
            if(vesa_my >= tby && vesa_mx >= 2 && vesa_mx <= 98){ 
                start_menu = !start_menu; 
                power_dialog = 0;
                goto draw_frame;
            }
            if(vesa_my >= tby && vesa_my < 768){ 
                int bx = 105; 
                for(int j = 0; j < vesa_win_count; j++){ 
                    if(!vesa_wins[j].visible) continue; 
                    if(vesa_mx >= bx && vesa_mx < bx + 130){ 
                        if(vesa_wins[j].minimized){ 
                            vesa_wins[j].minimized = 0; 
                            vesa_bring_to_front(j); 
                        } else if(vesa_wins[j].active){ 
                            vesa_wins[j].minimized = 1; 
                        } else { 
                            vesa_bring_to_front(j); 
                        } 
                        goto draw_frame;
                    } 
                    bx += 134; 
                } 
            }
            if(first_setup){ 
                int dx = 200, dy = 100;
                if(vesa_mx >= dx + 262 && vesa_mx < dx + 362 && vesa_my >= dy + 490 && vesa_my < dy + 520){ 
                    save_settings(); 
                    first_setup = 0; 
                    fm_create_dir("Documents"); fm_create_dir("Music"); fm_create_dir("Pictures"); 
                    fm_create_file("README.TXT", "Welcome to WNKA OS!"); 
                    fm_refresh(); 
                    goto draw_frame;
                }
                for(int i = 0; i < 5; i++){ 
                    int tx = dx + 180 + i * 90; 
                    if(vesa_mx >= tx && vesa_mx < tx + 80 && vesa_my >= dy + 110 && vesa_my < dy + 132){ 
                        settings.theme = i; 
                    } 
                }
                for(int i = 0; i < 5; i++){ 
                    int w2 = dx + 180 + i * 90; 
                    if(vesa_mx >= w2 && vesa_mx < w2 + 80 && vesa_my >= dy + 142 && vesa_my < dy + 164){ 
                        settings.wallpaper = i; 
                    } 
                }
                goto draw_frame;
            }
            if(vesa_my >= 20 && vesa_my <= 65){ 
                if(vesa_mx >= 20 && vesa_mx <= 52){ 
                    vesa_create_window(60, 40, 500, 400, "File Manager", 0);
                    fm_refresh();
                    goto draw_frame;
                }
                if(vesa_mx >= 100 && vesa_mx <= 132){ 
                    vesa_create_window(120, 80, 420, 320, "Notepad", 1);
                    goto draw_frame;
                }
                if(vesa_mx >= 180 && vesa_mx <= 212){ 
                    vesa_create_window(180, 120, 250, 280, "Calculator", 3);
                    goto draw_frame;
                }
                if(vesa_mx >= 260 && vesa_mx <= 292){ 
                    vesa_create_window(150, 80, 700, 500, "VESA Terminal", 8);
                    goto draw_frame;
                }
                if(vesa_mx >= 340 && vesa_mx <= 372){ 
                    vesa_create_window(300, 150, 500, 450, "Applications", 9);
                    goto draw_frame;
                }
            }
            if(power_dialog){ 
                if(vesa_mx >= 387 && vesa_mx < 462 && vesa_my >= 402 && vesa_my < 430){ 
                    vesa_exit(); 
                    vesa_outb(0x64, 0xFE); 
                    return; 
                } 
                if(vesa_mx >= 587 && vesa_mx < 662 && vesa_my >= 402 && vesa_my < 430) power_dialog = 0; 
                goto draw_frame;
            }
            if(start_menu){ 
                int sy = 768 - 36 - 310; 
                for(int i = 0; i < 7; i++){ 
                    int iy = sy + 8 + i * 38; 
                    if(vesa_mx >= 30 && vesa_mx < 250 && vesa_my >= iy - 6 && vesa_my < iy + 34){ 
                        start_menu = 0; 
                        switch(i){ 
                            case 0: vesa_create_window(60, 40, 500, 400, "File Manager", 0); fm_refresh(); break; 
                            case 1: vesa_create_window(120, 80, 420, 320, "Notepad", 1); break; 
                            case 2: vesa_create_window(180, 120, 250, 280, "Calculator", 3); break; 
                            case 3: vesa_create_window(150, 80, 700, 500, "VESA Terminal", 8); break; 
                            case 4: vesa_create_window(160, 100, 440, 340, "Paint", 5); break; 
                            case 5: vesa_create_window(250, 150, 480, 380, "Settings", 7); break; 
                            case 6: power_dialog = 1; break; 
                        } 
                        goto draw_frame;
                    } 
                } 
            }
            for(int i = vesa_win_count - 1; i >= 0; i--){ 
                if(!vesa_wins[i].visible || vesa_wins[i].minimized) continue; 
                int wx = vesa_wins[i].x, wy = vesa_wins[i].y, ww = vesa_wins[i].w, wh = vesa_wins[i].h;
                if(vesa_mx >= wx && vesa_mx < wx + ww && vesa_my >= wy && vesa_my < wy + wh && !context_menu){ 
                    vesa_bring_to_front(i);
                    if(vesa_mx >= wx + ww - 40 && vesa_mx < wx + ww - 6 && vesa_my >= wy + 3 && vesa_my < wy + 23){ 
                        vesa_wins[i].visible = 0; 
                        goto draw_frame;
                    }
                    else if(vesa_mx >= wx + ww - 98 && vesa_mx < wx + ww - 70 && vesa_my >= wy + 3 && vesa_my < wy + 21){ 
                        vesa_wins[i].minimized = 1; 
                        goto draw_frame;
                    }
                    else if(vesa_mx >= wx && vesa_mx < wx + ww - 110 && vesa_my >= wy && vesa_my < wy + 24){ 
                        drag_win = i; 
                        drag_off_x = vesa_mx - wx; 
                        drag_off_y = vesa_my - wy; 
                        goto draw_frame;
                    }
                    if(vesa_wins[i].type == 0){
                        int fy = (vesa_my - wy - 44) / 18; 
                        if(fy >= 0 && fy < current_file_count){
                            file_selected = fy;
                            if(frame - last_click < 30 && current_files[fy].is_dir){
                                fm_change_dir(current_files[fy].name);
                            }
                        }
                        if(vesa_mx >= wx + 4 && vesa_mx < wx + 64 && vesa_my >= wy + wh - 28 && vesa_my < wy + wh - 8){ 
                            if(file_selected < current_file_count){ 
                                fm_delete_file(current_files[file_selected].name); 
                            } 
                        }
                        else if(vesa_mx >= wx + 70 && vesa_mx < wx + 130 && vesa_my >= wy + wh - 28 && vesa_my < wy + wh - 8){ 
                            fm_create_file("NewFile.txt", "New file created!"); 
                            fm_refresh(); 
                        }
                        else if(vesa_mx >= wx + 136 && vesa_mx < wx + 196 && vesa_my >= wy + wh - 28 && vesa_my < wy + wh - 8){ 
                            if(file_selected < current_file_count) fm_copy_file(current_files[file_selected].name); 
                        }
                        else if(vesa_mx >= wx + 202 && vesa_mx < wx + 262 && vesa_my >= wy + wh - 28 && vesa_my < wy + wh - 8){ 
                            if(file_selected < current_file_count) fm_cut_file(current_files[file_selected].name); 
                        }
                        else if(vesa_mx >= wx + 268 && vesa_mx < wx + 328 && vesa_my >= wy + wh - 28 && vesa_my < wy + wh - 8){ 
                            fm_paste_file(); 
                        }
                        goto draw_frame;
                    }
                    else if(vesa_wins[i].type == 3){
                        int col = (vesa_mx - wx - 8) / 46;
                        int row = (vesa_my - wy - 58) / 26;
                        if(col >= 0 && col < 4 && row >= 0 && row < 4){
                            const char* btns = "789/456*123-0.=";
                            char btn = btns[row * 4 + col];
                            if(btn >= '0' && btn <= '9'){
                                if(calc_new){ calc_value = btn - '0'; calc_new = 0; }
                                else calc_value = calc_value * 10 + (btn - '0');
                            } 
                            else if(btn == '+' || btn == '-' || btn == '*' || btn == '/'){
                                calc_op = btn;
                                calc_new = 1;
                            } 
                            else if(btn == '='){
                                if(calc_op == '+') calc_value = calc_value + calc_value;
                                else if(calc_op == '-') calc_value = calc_value - calc_value;
                                else if(calc_op == '*') calc_value = calc_value * calc_value;
                                else if(calc_op == '/') calc_value = 1;
                                calc_new = 1;
                            }
                        }
                        goto draw_frame;
                    }
                    else if(vesa_wins[i].type == 7){
                        for(int t = 0; t < 9; t++){ 
                            if(vesa_mx >= wx + 4 && vesa_mx < wx + 130 && vesa_my >= wy + 44 + t * 24 && vesa_my < wy + 44 + t * 24 + 22){ 
                                settings_theme_tab = t; 
                            } 
                        }
                        if(settings_theme_tab == 0){
                            for(int t = 0; t < 5; t++){ 
                                if(vesa_mx >= wx + 144 && vesa_mx < wx + 294 && vesa_my >= wy + 48 + t * 24 && vesa_my < wy + 48 + t * 24 + 22){ 
                                    settings.theme = t; 
                                } 
                            }
                        }
                        if(settings_theme_tab == 1){
                            for(int t = 0; t < 5; t++){ 
                                if(vesa_mx >= wx + 144 && vesa_mx < wx + 294 && vesa_my >= wy + 48 + t * 24 && vesa_my < wy + 48 + t * 24 + 22){ 
                                    settings.wallpaper = t; 
                                } 
                            }
                        }
                        if(vesa_mx >= wx + ww - 60 && vesa_mx < wx + ww - 10 && vesa_my >= wy + wh - 28 && vesa_my < wy + wh - 8){ 
                            save_settings(); 
                        }
                        goto draw_frame;
                    }
                    else if(vesa_wins[i].type == 9){
                        int apps_per_page = 5;
                        int y_offset = 46;
                        for(int a = 0; a < apps_per_page; a++) {
                            int iy = wy + y_offset + a * 56;
                            if(vesa_mx >= wx + 6 && vesa_mx < wx + ww - 6 && vesa_my >= iy && vesa_my < iy + 50) {
                                int idx = a;
                                if(frame - last_click < 30 && a < wnx_gui_count) {
                                    run_wnx_from_gui(wnx_gui_found[a].path);
                                }
                            }
                        }
                        int apps_per_page_vis = 5;
                        goto draw_frame;
                    }
                    goto draw_frame;
                } 
            }
        }
        if(!lb) last_click = 0;
        if(rb && !last_right_click){ 
            last_right_click = 1;
            context_menu = 1; 
            context_menu_x = vesa_mx; 
            context_menu_y = vesa_my; 
            int count = 9;
            int w = 140, h = count * 24 + 8;
            int mx = context_menu_x, my = context_menu_y;
            if(mx + w > 1024) mx = 1024 - w;
            if(my + h > 768) my = 768 - h;
            context_menu_item = -1;
            for(int i = 0; i < count; i++){
                int iy = my + 4 + i * 22;
                if(vesa_mx >= mx + 2 && vesa_mx < mx + w - 2 && vesa_my >= iy - 2 && vesa_my < iy + 20){
                    context_menu_item = i;
                    break;
                }
            }
            goto draw_frame;
        }
        if(!rb) last_right_click = 0;
        if(drag_win != -1 && lb){ 
            vesa_wins[drag_win].x = vesa_mx - drag_off_x; 
            vesa_wins[drag_win].y = vesa_my - drag_off_y; 
        } else { 
            drag_win = -1; 
        }
        draw_frame:
        uint16_t bt, bb; 
        if(settings.theme == 2 || settings.theme == 3 || settings.theme == 4){ bt = WN_RGB565(35,35,55); bb = WN_RGB565(15,15,30); } 
        else { bt = WN_RGB565(80,140,210); bb = WN_RGB565(0,50,130); } 
        wn_vesa_gradient_v(0, 0, 1024, 768 - 36, bt, bb);
        if(settings.wallpaper == 1) for(int i = 0; i < 100; i++) wn_vesa_pixel((i * 67 + frame / 2) % 1024, (i * 43) % 768, WN_WHITE);
        if(settings.wallpaper == 2) for(int y = 0; y < 768; y += 20) for(int x = 0; x < 1024; x += 20) wn_vesa_pixel(x, y, WN_DKGRAY);
        if(settings.wallpaper == 3) wn_vesa_gradient_v(0, 0, 1024, 768 - 36, WN_RGB565(0,80,160), WN_RGB565(0,20,60));
        if(settings.wallpaper == 4) for(int i = 0; i < 80; i++) wn_vesa_text((i * 13) % 1024, (i * 7 + frame) % 768, frame % 2 ? "0" : "1", WN_GREEN);
        draw_icon(20, 20, 0); wn_vesa_text(25, 52, "Files", WN_WHITE); 
        draw_icon(100, 20, 1); wn_vesa_text(105, 52, "Notepad", WN_WHITE);
        draw_icon(180, 20, 2); wn_vesa_text(185, 52, "Calc", WN_WHITE); 
        draw_icon(260, 20, 3); wn_vesa_text(265, 52, "Terminal", WN_WHITE);
        draw_icon(340, 20, 4); wn_vesa_text(345, 52, "Paint", WN_WHITE);
        vesa_draw_windows(); 
        if(start_menu) draw_start_menu(); 
        if(power_dialog) draw_power_dialog(); 
        if(first_setup) draw_first_setup(); 
        if(context_menu) draw_context_menu(); 
        draw_taskbar();
        for(int i = 0; i < 10; i++){ 
            if(vesa_mx + i < 1024) backbuffer[vesa_my * 1024 + (vesa_mx + i)] = WN_WHITE; 
            if(vesa_my + i < 768) backbuffer[(vesa_my + i) * 1024 + vesa_mx] = WN_WHITE; 
        } 
        backbuffer[vesa_my * 1024 + vesa_mx] = WN_BLACK;
        for(int i = 0; i < 1024 * 768; i++) vesa_fb_ptr[i] = backbuffer[i]; 
        vesa_wait_vsync(); 
        frame_counter++; 
        frame++;
    }
    disable_mouse();
}