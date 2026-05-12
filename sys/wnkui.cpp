#include "vga.h"
#include "kernel_stubs.h"
#include "ata.h"
#include <stdint.h>

typedef struct {
    int theme;
    int wallpaper;
    int click_key;
    int menu_key;
    int window_shadows;
    int title_height;
    int animation_speed;
    int configured;
    int taskbar_auto_hide;
    int clock_24h;
    int show_seconds;
    int screensaver_enabled;
    int screensaver_timeout;
    int screensaver_type;
    int title_style;
    int custom_r, custom_g, custom_b;
} settings_t;

static settings_t settings = {
    0, 0, 0x10, 0x13, 1, 14, 5, 0, 0, 1, 1, 1, 300, 0, 0, 0, 0, 0
};

static const uint8_t theme_desktop[6] = {0x01, 0x00, 0x02, 0x03, 0x06, 0x01};
static const uint8_t theme_window[6]  = {0x07, 0x08, 0x07, 0x07, 0x07, 0x07};
static const uint8_t theme_title[6]   = {0x01, 0x00, 0x02, 0x03, 0x04, 0x01};
static const uint8_t theme_inactive[6]= {0x08, 0x08, 0x0A, 0x0B, 0x0C, 0x08};
static const uint8_t theme_taskbar[6] = {0x07, 0x08, 0x07, 0x07, 0x07, 0x07};

typedef struct {
    char name[32];
    int is_dir;
    int size;
} file_entry_t;

typedef struct {
    int x, y, w, h;
    char title[32];
    int visible;
    int minimized;
    int active;
    int type;
    int anim_state;
    int anim_timer;
    char notepad_text[2048];
    int notepad_len;
    int notepad_cursor;
    int notepad_scroll;
    long calc_val1, calc_val2;
    char calc_op;
    int calc_state;
    char calc_display[32];
    char shell_buf[1024];
    int shell_pos;
    int shell_cursor;
    int shell_history[16];
    int shell_hist_pos;
    int help_scroll;
    uint8_t canvas[200][320];
    int paint_tool;
    uint8_t paint_color;
    int paint_drawing;
    int paint_zoom;
    int media_playing;
    int media_track;
    int media_volume;
    int clock_popup;
} window_t;

static window_t wins[20];
static int win_count = 0;

static int mx = 160;
static int my = 100;

static int start_menu = 0;
static int programs_submenu = 0;
static int power_dialog = 0;
static int settings_open = 0;
static int settings_tab = 0;
static int clock_popup = 0;

static int clock_h = 14;
static int clock_m = 45;
static int clock_s = 0;
static int day = 15;
static int month = 4;
static int year = 2026;

static int anim_frame = 0;
static int frame_counter = 0;

static int drag_mode = 0;
static int drag_win = -1;
static int drag_off_x = 0;
static int drag_off_y = 0;

static int rename_active = 0;
static char rename_buf[64];
static int rename_pos = 0;

static int dialog_active = 0;
static char dialog_msg[128];

static int copy_buffer_valid = 0;
static char copy_buffer[256];
static int file_operation = 0;

static int notification_active = 0;
static char notification_msg[128];
static int notification_timer = 0;

static int screensaver_active = 0;
static int idle_frames = 0;
static int last_mx = 160;
static int last_my = 100;
static int ss_frame = 0;

static float ss_logo_x = 160.0f;
static float ss_logo_y = 100.0f;
static float ss_logo_dx = 1.7f;
static float ss_logo_dy = 1.3f;
static char ss_user_text[64] = "WNKA OS";
static float ss_plane_x = -50.0f;
static float ss_plane_y = 100.0f;
static int matrix_y[80] = {0};
static int star_x[100], star_y[100], star_init = 0;
static int snow_x[100], snow_y[100], snow_init = 0;
static int fire_x[50], fire_y[50], fire_life[50], fire_init = 0;
static int bubble_x[30], bubble_y[30], bubble_init = 0;

static file_entry_t current_files[128];
static int current_file_count = 0;
static char current_path[256] = "/";
static int file_selected = 0;
static int file_scroll = 0;

static void str_cat(char* d, const char* s) {
    while (*d) d++;
    while (*s) { *d = *s; d++; s++; }
    *d = 0;
}

static void str_cpy(char* d, const char* s) {
    while (*s) { *d = *s; d++; s++; }
    *d = 0;
}

static int str_len(const char* s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

static int str_cmp(const char* a, const char* b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

static void int_to_str(int n, char* s) {
    if (n == 0) { s[0] = '0'; s[1] = 0; return; }
    int i = 0, t = n > 0 ? n : -n;
    while (t) { t /= 10; i++; }
    if (n < 0) { s[0] = '-'; s++; n = -n; }
    s[i] = 0;
    while (i--) { s[i] = '0' + (n % 10); n /= 10; }
}

static void show_notification(const char* msg) {
    str_cpy(notification_msg, msg);
    notification_active = 1;
    notification_timer = 180;
}

static float SIN(float x) {
    while (x > 6.28318f) x -= 6.28318f;
    while (x < 0.0f) x += 6.28318f;
    float x3 = x * x * x;
    float x5 = x3 * x * x;
    float x7 = x5 * x * x;
    return x - x3 / 6.0f + x5 / 120.0f - x7 / 5040.0f;
}

static float COS(float x) {
    return SIN(x + 1.570796f);
}

static void read_cmos_time(void) {
    vga_outb(0x70, 0x04);
    uint8_t rh = vga_inb(0x71);
    vga_outb(0x70, 0x02);
    uint8_t rm = vga_inb(0x71);
    vga_outb(0x70, 0x00);
    uint8_t rs = vga_inb(0x71);
    vga_outb(0x70, 0x07);
    uint8_t rd = vga_inb(0x71);
    vga_outb(0x70, 0x08);
    uint8_t rmonth = vga_inb(0x71);
    vga_outb(0x70, 0x09);
    uint8_t ryear = vga_inb(0x71);
    
    clock_h = ((rh >> 4) * 10) + (rh & 0x0F);
    clock_m = ((rm >> 4) * 10) + (rm & 0x0F);
    clock_s = ((rs >> 4) * 10) + (rs & 0x0F);
    day = ((rd >> 4) * 10) + (rd & 0x0F);
    month = ((rmonth >> 4) * 10) + (rmonth & 0x0F);
    year = ((ryear >> 4) * 10) + (ryear & 0x0F) + 2000;
}

static void save_config(void) {
    uint16_t buf[256] = {0};
    buf[0] = 0x574E; buf[1] = 0x4B41;
    buf[2] = settings.theme; buf[3] = settings.wallpaper;
    buf[4] = settings.click_key; buf[5] = settings.menu_key;
    buf[6] = settings.window_shadows; buf[7] = settings.title_height;
    buf[8] = settings.animation_speed; buf[9] = 1;
    buf[10] = settings.taskbar_auto_hide; buf[11] = settings.clock_24h;
    buf[12] = settings.show_seconds; buf[13] = settings.screensaver_enabled;
    buf[14] = settings.screensaver_timeout / 10; buf[15] = settings.screensaver_type;
    buf[16] = settings.title_style; buf[17] = settings.custom_r;
    buf[18] = settings.custom_g; buf[19] = settings.custom_b;
    write_sector(110, buf);
}

static int load_config(void) {
    uint16_t buf[256];
    read_sector(110, buf);
    if (buf[0] != 0x574E || buf[1] != 0x4B41) return 0;
    settings.theme = buf[2]; settings.wallpaper = buf[3];
    settings.click_key = buf[4]; settings.menu_key = buf[5];
    settings.window_shadows = buf[6]; settings.title_height = buf[7];
    settings.animation_speed = buf[8]; settings.configured = buf[9];
    if (buf[10] != 0xFFFF) settings.taskbar_auto_hide = buf[10];
    if (buf[11] != 0xFFFF) settings.clock_24h = buf[11];
    if (buf[12] != 0xFFFF) settings.show_seconds = buf[12];
    if (buf[13] != 0xFFFF) settings.screensaver_enabled = buf[13];
    if (buf[14] != 0xFFFF) settings.screensaver_timeout = buf[14] * 10;
    if (buf[15] != 0xFFFF) settings.screensaver_type = buf[15];
    if (buf[16] != 0xFFFF) settings.title_style = buf[16];
    if (buf[17] != 0xFFFF) settings.custom_r = buf[17];
    if (buf[18] != 0xFFFF) settings.custom_g = buf[18];
    if (buf[19] != 0xFFFF) settings.custom_b = buf[19];
    return 1;
}

static void fill_rect(int x, int y, int w, int h, uint8_t color) {
    for (int dx = 0; dx < w; dx++) {
        for (int dy = 0; dy < h; dy++) {
            int px = x + dx;
            int py = y + dy;
            if (px >= 0 && px < 320 && py >= 0 && py < 240) {
                vga_buf_pixel(px, py, color);
            }
        }
    }
}

static void draw_border(int x, int y, int w, int h, uint8_t color) {
    for (int i = 0; i < w; i++) {
        if (x + i >= 0 && x + i < 320 && y >= 0 && y < 240) {
            vga_buf_pixel(x + i, y, color);
        }
        if (x + i >= 0 && x + i < 320 && y + h - 1 >= 0 && y + h - 1 < 240) {
            vga_buf_pixel(x + i, y + h - 1, color);
        }
    }
    for (int i = 0; i < h; i++) {
        if (x >= 0 && x < 320 && y + i >= 0 && y + i < 240) {
            vga_buf_pixel(x, y + i, color);
        }
        if (x + w - 1 >= 0 && x + w - 1 < 320 && y + i >= 0 && y + i < 240) {
            vga_buf_pixel(x + w - 1, y + i, color);
        }
    }
}

static void draw_rect(int x, int y, int w, int h, uint8_t fill, uint8_t border) {
    fill_rect(x, y, w, h, fill);
    if (border != fill) draw_border(x, y, w, h, border);
}

static void draw_button(int x, int y, int w, int h, int pressed) {
    draw_rect(x, y, w, h, 0x07, 0x00);
    if (pressed) {
        vga_hline(x, y, w, 0x08);
        vga_vline(x, y, h, 0x08);
    } else {
        vga_hline(x, y, w, 0x0F);
        vga_vline(x, y, h, 0x0F);
        vga_hline(x + 1, y + h - 1, w - 1, 0x08);
        vga_vline(x + w - 1, y + 1, h - 1, 0x08);
    }
}

static int mouse_in(int x, int y, int w, int h) {
    return (mx >= x && mx < x + w && my >= y && my < y + h);
}

static void draw_cursor(void) {
    vga_cursor(mx, my);
}

static int is_mouse_over_window(void) {
    for (int i = 0; i < win_count; i++) {
        if (!wins[i].visible || wins[i].minimized) continue;
        if (mouse_in(wins[i].x, wins[i].y, wins[i].w, wins[i].h)) return 1;
    }
    return 0;
}

static int simple_rand(void) {
    static unsigned int seed = 12345;
    seed = seed * 1103515245 + 12345;
    return (int)((seed >> 16) & 0x7FFF);
}

static void fm_refresh(void) {
    current_file_count = 0;
    uint16_t buf[256];
    read_sector(100, buf);
    
    for (int i = 0; i < 32; i++) {
        char* e = (char*)&buf[i * 8];
        if (e[0] == 0) continue;
        
        char name[32];
        int ni = 0;
        for (int j = 0; j < 11 && e[j]; j++) {
            if (e[j] != ' ') name[ni++] = e[j];
        }
        name[ni] = 0;
        if (name[0] == 0) continue;
        
        str_cpy(current_files[current_file_count].name, name);
        current_files[current_file_count].is_dir = (e[11] == 1);
        current_files[current_file_count].size = e[12] | (e[13] << 8);
        current_file_count++;
    }
}

static void fm_open_dir(const char* name) {
    int len = 0;
    while (current_path[len]) len++;
    if (current_path[len - 1] != '/') {
        current_path[len] = '/';
        current_path[len + 1] = 0;
    }
    str_cat(current_path, name);
    fm_refresh();
}

static void fm_go_up(void) {
    int len = 0;
    while (current_path[len]) len++;
    if (len <= 1) return;
    int i = len - 2;
    while (i > 0 && current_path[i] != '/') i--;
    current_path[i + 1] = 0;
    fm_refresh();
}

static void fm_delete_file(void) {
    if (file_selected >= current_file_count) return;
    uint16_t buf[256];
    read_sector(100, buf);
    for (int i = 0; i < 32; i++) {
        char* e = (char*)&buf[i * 8];
        int m = 1;
        for (int j = 0; current_files[file_selected].name[j]; j++) {
            if (e[j] != current_files[file_selected].name[j]) { m = 0; break; }
        }
        if (m) {
            e[0] = 0;
            write_sector(100, buf);
            fm_refresh();
            show_notification("File deleted!");
            return;
        }
    }
}

static void fm_copy_file(void) {
    if (file_selected >= current_file_count) return;
    str_cpy(copy_buffer, current_files[file_selected].name);
    copy_buffer_valid = 1;
    file_operation = 1;
    show_notification("Copied!");
}

static void fm_move_file(void) {
    if (file_selected >= current_file_count) return;
    str_cpy(copy_buffer, current_files[file_selected].name);
    copy_buffer_valid = 1;
    file_operation = 2;
    show_notification("Cut!");
}

static void fm_paste_file(void) {
    if (!copy_buffer_valid) return;
    uint16_t buf[256];
    read_sector(100, buf);
    for (int i = 0; i < 32; i++) {
        char* e = (char*)&buf[i * 8];
        if (e[0] == 0) {
            for (int j = 0; copy_buffer[j] && j < 11; j++) e[j] = copy_buffer[j];
            e[11] = 0;
            write_sector(100, buf);
            if (file_operation == 2) fm_delete_file();
            copy_buffer_valid = 0;
            fm_refresh();
            show_notification("Pasted!");
            return;
        }
    }
    str_cpy(dialog_msg, "Directory full!");
    dialog_active = 1;
}

static void fm_new_file(void) {
    uint16_t buf[256];
    read_sector(100, buf);
    for (int i = 0; i < 32; i++) {
        char* e = (char*)&buf[i * 8];
        if (e[0] == 0) {
            const char* nn = "NewFile.txt";
            for (int j = 0; j < 11 && nn[j]; j++) e[j] = nn[j];
            e[11] = 0;
            write_sector(100, buf);
            fm_refresh();
            show_notification("New file created!");
            return;
        }
    }
}

static void fm_new_dir(void) {
    uint16_t buf[256];
    read_sector(100, buf);
    for (int i = 0; i < 32; i++) {
        char* e = (char*)&buf[i * 8];
        if (e[0] == 0) {
            const char* nn = "NewFolder";
            for (int j = 0; j < 11 && nn[j]; j++) e[j] = nn[j];
            e[11] = 1;
            write_sector(100, buf);
            fm_refresh();
            show_notification("New folder created!");
            return;
        }
    }
}

static void fm_rename_start(void) {
    if (file_selected >= current_file_count) return;
    rename_active = 1;
    str_cpy(rename_buf, current_files[file_selected].name);
    rename_pos = str_len(rename_buf);
}

static void fm_apply_rename(void) {
    if (!rename_active) return;
    if (file_selected >= current_file_count) return;
    uint16_t buf[256];
    read_sector(100, buf);
    for (int i = 0; i < 32; i++) {
        char* e = (char*)&buf[i * 8];
        int m = 1;
        for (int j = 0; current_files[file_selected].name[j]; j++) {
            if (e[j] != current_files[file_selected].name[j]) { m = 0; break; }
        }
        if (m) {
            for (int j = 0; rename_buf[j] && j < 11; j++) e[j] = rename_buf[j];
            write_sector(100, buf);
            fm_refresh();
            show_notification("Renamed!");
            break;
        }
    }
    rename_active = 0;
}

static void notepad_init(window_t* w) {
    const char* txt = "=== WNKA Notepad ===\n\nWelcome!\n\nType your text below:\n\n";
    int i = 0;
    while (txt[i] && i < 2043) {
        w->notepad_text[i] = txt[i];
        i++;
    }
    w->notepad_text[i] = 0;
    w->notepad_len = i;
    w->notepad_cursor = i;
    w->notepad_scroll = 0;
}

static void notepad_handle_key(window_t* w, int key) {
    if (key == 12 && w->notepad_cursor > 0) {
        for (int i = w->notepad_cursor - 1; i < w->notepad_len; i++) {
            w->notepad_text[i] = w->notepad_text[i + 1];
        }
        w->notepad_len--;
        w->notepad_cursor--;
    }
    else if (key == 9 && w->notepad_len < 2000) {
        for (int i = w->notepad_len; i > w->notepad_cursor; i--) {
            w->notepad_text[i] = w->notepad_text[i - 1];
        }
        w->notepad_text[w->notepad_cursor] = '\n';
        w->notepad_len++;
        w->notepad_cursor++;
    }
    else if (key >= ' ' && key <= 'z' && w->notepad_len < 2000) {
        for (int i = w->notepad_len; i > w->notepad_cursor; i--) {
            w->notepad_text[i] = w->notepad_text[i - 1];
        }
        w->notepad_text[w->notepad_cursor] = (char)key;
        w->notepad_len++;
        w->notepad_cursor++;
    }
}

static void calculator_init(window_t* w) {
    w->calc_val1 = 0;
    w->calc_val2 = 0;
    w->calc_op = 0;
    w->calc_state = 0;
    str_cpy(w->calc_display, "0");
}

static void calculator_handle_click(window_t* w, int cx, int cy) {
    int sx = w->x + 6;
    int sy = w->y + 20;
    int col = (cx - sx) / 22;
    int row = (cy - sy - 18) / 14;
    if (col < 0 || col > 4 || row < 0 || row > 3) return;
    int id = row * 5 + col;
    
    if (id >= 0 && id <= 9) {
        if (w->calc_state == 0) w->calc_val1 = w->calc_val1 * 10 + id;
        else w->calc_val2 = w->calc_val2 * 10 + id;
    }
    else if (id == 10) { w->calc_op = '/'; w->calc_state = 1; }
    else if (id == 13) { w->calc_op = '*'; w->calc_state = 1; }
    else if (id == 15) { w->calc_op = '-'; w->calc_state = 1; }
    else if (id == 18) { w->calc_op = '+'; w->calc_state = 1; }
    else if (id == 4) {
        w->calc_val1 = 0; w->calc_val2 = 0; w->calc_op = 0; w->calc_state = 0;
        str_cpy(w->calc_display, "0");
    }
    else if (id == 17) {
        if (w->calc_op) {
            switch (w->calc_op) {
                case '+': w->calc_val1 = w->calc_val1 + w->calc_val2; break;
                case '-': w->calc_val1 = w->calc_val1 - w->calc_val2; break;
                case '*': w->calc_val1 = w->calc_val1 * w->calc_val2; break;
                case '/': if (w->calc_val2 != 0) w->calc_val1 = w->calc_val1 / w->calc_val2; break;
            }
            w->calc_val2 = 0;
            w->calc_op = 0;
            w->calc_state = 0;
        }
    }
    
    long val = w->calc_state ? w->calc_val2 : w->calc_val1;
    char buf[32];
    int_to_str((int)val, buf);
    str_cpy(w->calc_display, buf);
}

static void terminal_init(window_t* w) {
    w->shell_buf[0] = 0;
    w->shell_pos = 0;
    w->shell_cursor = 0;
    for (int i = 0; i < 16; i++) w->shell_history[i] = 0;
    w->shell_hist_pos = 0;
}

static void terminal_handle_key(window_t* w, int key) {
    if (key == 12 && w->shell_pos > 0) {
        w->shell_pos--;
        w->shell_buf[w->shell_pos] = 0;
        w->shell_cursor = w->shell_pos;
    }
    else if (key == 9 && w->shell_pos < 1000) {
        w->shell_buf[w->shell_pos] = '\n';
        w->shell_pos++;
        w->shell_buf[w->shell_pos] = 0;
        w->shell_cursor = w->shell_pos;
    }
    else if (key >= ' ' && key <= 'z' && w->shell_pos < 1000) {
        w->shell_buf[w->shell_pos] = (char)key;
        w->shell_pos++;
        w->shell_buf[w->shell_pos] = 0;
        w->shell_cursor = w->shell_pos;
    }
}

static void paint_init(window_t* w) {
    w->canvas[0][0] = 0xFF;
    w->paint_tool = 0;
    w->paint_color = 0x0C;
    w->paint_drawing = 0;
    w->paint_zoom = 1;
}

static void paint_draw_tools(window_t* w) {
    int cx = w->x + 6;
    int cy = w->y + 20;
    int canvas_w = w->w - 90;
    int canvas_h = w->h - 40;
    
    draw_rect(cx, cy, canvas_w, canvas_h, 0x0F, 0x00);
    
    for (int y = 0; y < canvas_h && y < 200; y++) {
        for (int x = 0; x < canvas_w && x < 320; x++) {
            if (w->canvas[y][x] != 0xFF) {
                vga_buf_pixel(cx + x, cy + y, w->canvas[y][x]);
            }
        }
    }
    
    const char* tools[] = {"Pen", "Line", "Rect", "Fill", "Erase", "Clear", "Zoom"};
    for (int i = 0; i < 7; i++) {
        int ty = cy + i * 14;
        draw_button(cx + canvas_w + 4, ty, 50, 12, 0);
        vga_text(cx + canvas_w + 8, ty + 2, tools[i], 0x00);
        if (w->paint_tool == i) fill_rect(cx + canvas_w + 4, ty, 50, 12, 0x01);
    }
    
    for (int i = 0; i < 16; i++) {
        int px = cx + canvas_w + 4 + (i % 2) * 26;
        int py = cy + 110 + (i / 2) * 8;
        fill_rect(px, py, 22, 6, i);
        draw_border(px - 1, py - 1, 24, 8, 0x0F);
        if (i == w->paint_color) draw_border(px - 1, py - 1, 24, 8, 0x0F);
    }
}

static void paint_handle_click(window_t* w, int cx, int cy) {
    int sx = w->x + 6;
    int sy = w->y + 20;
    int canvas_w = w->w - 90;
    int canvas_h = w->h - 40;
    
    if (cx >= sx && cx < sx + canvas_w && cy >= sy && cy < sy + canvas_h) {
        int px = cx - sx;
        int py = cy - sy;
        if (px >= 0 && px < canvas_w && py >= 0 && py < canvas_h) {
            w->canvas[py][px] = w->paint_color;
        }
    }
    
    for (int i = 0; i < 7; i++) {
        int ty = sy + i * 14;
        if (mouse_in(sx + canvas_w + 4, ty, 50, 12)) {
            w->paint_tool = i;
            if (i == 5) {
                for (int y = 0; y < canvas_h && y < 200; y++) {
                    for (int x = 0; x < canvas_w && x < 320; x++) {
                        w->canvas[y][x] = 0xFF;
                    }
                }
            }
        }
    }
    
    for (int i = 0; i < 16; i++) {
        int px = sx + canvas_w + 4 + (i % 2) * 26;
        int py = sy + 110 + (i / 2) * 8;
        if (mouse_in(px, py, 22, 6)) w->paint_color = i;
    }
}

static void draw_screensaver(void) {
    if (!screensaver_active) return;
    vga_clear(0x00);
    
    switch (settings.screensaver_type) {
        case 0: {  
            ss_logo_x += ss_logo_dx;
            ss_logo_y += ss_logo_dy;
            if (ss_logo_x < 40 || ss_logo_x > 280) ss_logo_dx = -ss_logo_dx;
            if (ss_logo_y < 40 || ss_logo_y > 200) ss_logo_dy = -ss_logo_dy;
            for (int i = 0; i < 8; i++) {
                float a = i * 0.785f + ss_frame * 0.05f;
                int x1 = (int)(ss_logo_x + 15 * SIN(a));
                int y1 = (int)(ss_logo_y + 15 * COS(a));
                int x2 = (int)(ss_logo_x + 25 * SIN(a));
                int y2 = (int)(ss_logo_y + 25 * COS(a));
                vga_line(x1, y1, x2, y2, 0x0E + (i % 6));
            }
            vga_text((int)ss_logo_x - 10, (int)ss_logo_y - 4, "WNKA", 0x0F);
            break;
        }
        case 1: { 
            if (!star_init) {
                for (int i = 0; i < 100; i++) {
                    star_x[i] = simple_rand() % 320;
                    star_y[i] = simple_rand() % 240;
                }
                star_init = 1;
            }
            for (int i = 0; i < 100; i++) {
                vga_buf_pixel(star_x[i], star_y[i], 0x0F);
                star_x[i] += (simple_rand() % 3) - 1;
                star_y[i] += (simple_rand() % 3) - 1;
                if (star_x[i] < 0) star_x[i] = 319;
                if (star_x[i] > 319) star_x[i] = 0;
                if (star_y[i] < 0) star_y[i] = 239;
                if (star_y[i] > 239) star_y[i] = 0;
            }
            break;
        }
        case 2: { 
            for (int i = 0; i < 80; i++) {
                if (simple_rand() % 100 < 2) matrix_y[i] = 0;
                if (matrix_y[i] < 240) {
                    vga_char(i * 4, matrix_y[i], '0' + (simple_rand() % 2), 0x0A);
                    matrix_y[i] += 2;
                }
            }
            break;
        }
        case 3: { 
            ss_plane_x += 0.5f;
            if (ss_plane_x > 380) ss_plane_x = -80;
            vga_line((int)ss_plane_x + 20, (int)ss_plane_y, (int)ss_plane_x + 40, (int)ss_plane_y + 5, 0x07);
            vga_line((int)ss_plane_x + 20, (int)ss_plane_y + 10, (int)ss_plane_x + 40, (int)ss_plane_y + 5, 0x07);
            vga_line((int)ss_plane_x + 20, (int)ss_plane_y, (int)ss_plane_x + 20, (int)ss_plane_y + 10, 0x07);
            fill_rect((int)ss_plane_x + 40, (int)ss_plane_y + 2, 60, 6, 0x0C);
            vga_text((int)ss_plane_x + 43, (int)ss_plane_y + 3, "WNKA OS", 0x0F);
            break;
        }
        case 4: { 
            ss_frame++;
            int offset = ss_frame % (str_len(ss_user_text) * 6 + 340);
            vga_text(340 - offset, 120, ss_user_text, 0x0E);
            break;
        }
        case 5: {  
            if (!snow_init) {
                for (int i = 0; i < 100; i++) {
                    snow_x[i] = simple_rand() % 320;
                    snow_y[i] = simple_rand() % 240;
                }
                snow_init = 1;
            }
            for (int i = 0; i < 100; i++) {
                vga_buf_pixel(snow_x[i], snow_y[i], 0x0F);
                snow_y[i] += 1;
                if (snow_y[i] > 239) {
                    snow_y[i] = 0;
                    snow_x[i] = simple_rand() % 320;
                }
            }
            break;
        }
        case 6: {  
            if (!fire_init) {
                for (int i = 0; i < 50; i++) {
                    fire_x[i] = simple_rand() % 320;
                    fire_y[i] = simple_rand() % 240;
                    fire_life[i] = simple_rand() % 100;
                }
                fire_init = 1;
            }
            for (int i = 0; i < 50; i++) {
                uint8_t color = 0x0C - (fire_life[i] / 10);
                if (color < 0x04) color = 0x04;
                vga_buf_pixel(fire_x[i], fire_y[i], color);
                fire_life[i]--;
                if (fire_life[i] < 0) {
                    fire_life[i] = 100;
                    fire_x[i] = simple_rand() % 320;
                    fire_y[i] = 239;
                }
            }
            break;
        }
        case 7: { 
            if (!bubble_init) {
                for (int i = 0; i < 30; i++) {
                    bubble_x[i] = simple_rand() % 320;
                    bubble_y[i] = simple_rand() % 240;
                }
                bubble_init = 1;
            }
            for (int i = 0; i < 30; i++) {
                vga_buf_pixel(bubble_x[i], bubble_y[i], 0x0B);
                bubble_y[i] -= 1;
                if (bubble_y[i] < 0) {
                    bubble_y[i] = 239;
                    bubble_x[i] = simple_rand() % 320;
                }
            }
            break;
        }
    }
    ss_frame++;
}

static void draw_wallpaper(void) {
    switch (settings.wallpaper) {
        case 0: break;
        case 1:
            for (int y = 0; y < 240; y += 8)
                for (int x = 0; x < 320; x++) vga_buf_pixel(x, y, 0x09);
            break;
        case 2:
            for (int y = 0; y < 240; y += 15)
                for (int x = 0; x < 320; x += 15) {
                    vga_buf_pixel(x, y, 0x0F);
                    vga_buf_pixel(x + 1, y + 1, 0x0F);
                }
            break;
        case 3:
            for (int y = 0; y < 240; y += 20)
                for (int x = 0; x < 320; x++) vga_buf_pixel(x, y, 0x08);
            for (int x = 0; x < 320; x += 20)
                for (int y = 0; y < 240; y++) vga_buf_pixel(x, y, 0x08);
            break;
        case 4:
            for (int y = 0; y < 240; y++) {
                int wx = (anim_frame + y) % 320;
                vga_buf_pixel(wx, y, 0x0B);
                vga_buf_pixel(wx + 10, y, 0x03);
            }
            break;
        case 5:
            for (int i = 0; i < 80; i++) {
                int sx = (i * 67 + anim_frame / 2) % 320;
                int sy = (i * 43) % 240;
                vga_buf_pixel(sx, sy, 0x0F);
            }
            break;
        case 6:
            for (int y = 0; y < 240; y += 12)
                for (int x = 0; x < 320; x += 24)
                    for (int dy = 0; dy < 6; dy++)
                        for (int dx = 0; dx < 12; dx++) {
                            int off = ((y / 12) % 2) * 12;
                            vga_buf_pixel(x + dx + off, y + dy, 0x06);
                        }
            break;
        case 7:
            for (int y = 0; y < 240; y += 50)
                for (int x = 0; x < 320; x += 50)
                    for (int i = 0; i < 8; i++) {
                        float a = i * 0.785f;
                        int x1 = x + (int)(7 * SIN(a));
                        int y1 = y + (int)(7 * COS(a));
                        int x2 = x + (int)(15 * SIN(a));
                        int y2 = y + (int)(15 * COS(a));
                        vga_line(x1, y1, x2, y2, 0x0E + (i % 6));
                    }
            break;
        case 8:
            for (int y = 0; y < 240; y++)
                for (int x = 0; x < 320; x++)
                    vga_buf_pixel(x, y, ((x / 4 + y / 4) % 2) ? 0x02 : 0x0A);
            break;
    }
}

static void draw_title_bar(int x, int y, int w, int h, uint8_t color, int active) {
    uint8_t base = active ? color : theme_inactive[settings.theme];
    switch (settings.title_style) {
        case 0: fill_rect(x, y, w, h, base); break;
        case 1:
            for (int i = 0; i < h; i++) {
                vga_hline(x, y + i, w, base + (i % 3));
            }
            break;
        case 2:
            fill_rect(x, y, w, h, base);
            for (int i = 2; i < w; i += 4) vga_vline(x + i, y, h, 0x0F);
            break;
    }
}

static void draw_desktop_icons(void) {
    fill_rect(6, 6, 36, 36, 0x0E);
    draw_border(6, 6, 36, 36, 0x00);
    vga_text(6, 42, "Computer", 0x0F);
    
    fill_rect(56, 6, 36, 36, 0x01);
    draw_border(56, 6, 36, 36, 0x00);
    vga_text(56, 42, "Welcome", 0x0F);
    
    fill_rect(106, 6, 36, 36, 0x0A);
    draw_border(106, 6, 36, 36, 0x00);
    vga_text(106, 42, "Notepad", 0x0F);
    
    fill_rect(156, 6, 36, 36, 0x0C);
    draw_border(156, 6, 36, 36, 0x00);
    vga_text(156, 42, "Calc", 0x0F);
    
    fill_rect(206, 6, 36, 36, 0x09);
    draw_border(206, 6, 36, 36, 0x00);
    vga_text(206, 42, "Terminal", 0x0F);
    
    fill_rect(256, 6, 36, 36, 0x03);
    draw_border(256, 6, 36, 36, 0x00);
    vga_text(256, 42, "Paint", 0x0F);
    
    fill_rect(6, 56, 36, 36, 0x08);
    draw_border(6, 56, 36, 36, 0x00);
    vga_text(6, 92, "Settings", 0x0F);
    
    fill_rect(56, 56, 36, 36, 0x0D);
    draw_border(56, 56, 36, 36, 0x00);
    vga_text(56, 92, "Help", 0x0F);
    
    fill_rect(106, 56, 36, 36, 0x0B);
    draw_border(106, 56, 36, 36, 0x00);
    vga_text(106, 92, "Media", 0x0F);
    
    fill_rect(156, 56, 36, 36, 0x08);
    draw_border(156, 56, 36, 36, 0x00);
    vga_text(156, 92, "Recycle", 0x0F);
}

static void draw_clock_popup(void) {
    if (!clock_popup) return;
    
    int px = 260;
    int py = 180;
    int pw = 90;
    int ph = 40;
    
    draw_rect(px, py, pw, ph, 0x07, 0x0F);
    fill_rect(px + 2, py + 2, pw - 4, 16, 0x01);
    vga_text(px + 15, py + 5, "Date & Time", 0x0F);
    
    char date_str[32];
    int_to_str(day, date_str);
    str_cat(date_str, "/");
    char month_str[8];
    int_to_str(month, month_str);
    str_cat(date_str, month_str);
    str_cat(date_str, "/");
    char year_str[8];
    int_to_str(year, year_str);
    str_cat(date_str, year_str);
    vga_text(px + 8, py + 22, date_str, 0x00);
    
    char time_str[16];
    int_to_str(clock_h, time_str);
    str_cat(time_str, ":");
    char minute_str[8];
    int_to_str(clock_m, minute_str);
    if (clock_m < 10) str_cat(time_str, "0");
    str_cat(time_str, minute_str);
    if (settings.show_seconds) {
        str_cat(time_str, ":");
        char sec_str[8];
        int_to_str(clock_s, sec_str);
        if (clock_s < 10) str_cat(time_str, "0");
        str_cat(time_str, sec_str);
    }
    vga_text(px + 20, py + 32, time_str, 0x00);
}

static void draw_taskbar(void) {
    if (settings.taskbar_auto_hide && !start_menu && !power_dialog && !settings_open && !clock_popup) {
        return;
    }
    
    int ty = 240 - 60;
    
    draw_rect(0, ty, 320, 20, theme_taskbar[settings.theme], 0x0F);
    vga_hline(0, ty, 320, 0x0F);
    
    draw_button(2, ty + 2, 55, 14, start_menu);
    vga_text(8, ty + 4, "Start", 0x00);
    
    int bx = 62;
    for (int i = 0; i < win_count; i++) {
        if (!wins[i].visible) continue;
        int bw = str_len(wins[i].title) * 6 + 10;
        if (wins[i].active && !wins[i].minimized) {
            draw_button(bx, ty + 2, bw, 14, 1);
            vga_text(bx + 4, ty + 4, wins[i].title, 0x0F);
        } else {
            draw_button(bx, ty + 2, bw, 14, 0);
            vga_text(bx + 4, ty + 4, wins[i].title, 0x00);
        }
        bx += bw + 3;
    }
    
    char t[12];
    int h = settings.clock_24h ? clock_h : clock_h % 12;
    if (h == 0 && !settings.clock_24h) h = 12;
    t[0] = '0' + h / 10;
    t[1] = '0' + h % 10;
    t[2] = ':';
    t[3] = '0' + clock_m / 10;
    t[4] = '0' + clock_m % 10;
    t[5] = 0;
    
    draw_rect(270, ty + 2, 48, 14, theme_taskbar[settings.theme], 0x0F);
    vga_text(275, ty + 4, t, 0x00);
}

static void draw_start_menu(void) {
    if (!start_menu) return;
    
    int mxx = 2;
    int myy = 240 - 155;
    
    draw_rect(mxx, myy, 155, 155, 0x07, 0x00);
    fill_rect(mxx + 2, myy + 2, 18, 151, 0x01);
    
    vga_text(mxx + 4, myy + 20, "W", 0x0F);
    vga_text(mxx + 4, myy + 35, "N", 0x0F);
    vga_text(mxx + 4, myy + 50, "K", 0x0F);
    vga_text(mxx + 4, myy + 65, "A", 0x0F);
    
    const char* items[] = {
        "Programs >", "Settings", "File Manager",
        "Terminal", "Help", "", "Power"
    };
    
    for (int i = 0; i < 7; i++) {
        if (items[i][0] == 0) {
            vga_hline(mxx + 22, myy + 4 + i * 13 + 6, 131, 0x08);
            continue;
        }
        
        int iy = myy + 4 + i * 13;
        if (mouse_in(mxx + 22, iy, 131, 11)) {
            fill_rect(mxx + 22, iy, 131, 11, 0x01);
            vga_text(mxx + 26, iy + 2, items[i], 0x0F);
            if (i == 0) programs_submenu = 1;
        } else {
            vga_text(mxx + 26, iy + 2, items[i], 0x00);
        }
    }
    
    if (programs_submenu) {
        int sx = mxx + 157;
        int sy = myy + 4;
        draw_rect(sx, sy, 140, 126, 0x07, 0x00);
        
        const char* progs[] = {
            "My Computer", "Notepad", "Calculator", "Terminal",
            "Paint", "Media Player", "Settings", "Welcome", "Help"
        };
        
        for (int i = 0; i < 9; i++) {
            int iy = sy + 2 + i * 14;
            if (mouse_in(sx, iy, 138, 12)) {
                fill_rect(sx + 1, iy, 138, 12, 0x01);
                vga_text(sx + 4, iy + 3, progs[i], 0x0F);
            } else {
                vga_text(sx + 4, iy + 3, progs[i], 0x00);
            }
        }
    }
}

static void draw_power_dialog(void) {
    if (!power_dialog) return;
    
    for (int i = 0; i < 320 * 240; i++) {
        uint8_t p = vga_backbuffer[i];
        vga_backbuffer[i] = (p & 0xF0) | ((p & 0x0F) / 2);
    }
    
    int dx = 80, dy = 65, dw = 160, dh = 100;
    draw_rect(dx, dy, dw, dh, 0x07, 0x0F);
    fill_rect(dx + 2, dy + 2, dw - 4, 16, 0x01);
    vga_text(dx + 40, dy + 5, "WNKA Power", 0x0F);
    vga_text(dx + 15, dy + 28, "What do you want to do?", 0x00);
    
    draw_button(dx + 25, dy + 44, 50, 18, 0);
    vga_text(dx + 32, dy + 48, "Reboot", 0x00);
    
    draw_button(dx + 85, dy + 44, 55, 18, 0);
    vga_text(dx + 92, dy + 48, "Shutdown", 0x00);
    
    draw_button(dx + 15, dy + 70, 60, 16, 0);
    vga_text(dx + 28, dy + 72, "Cancel", 0x00);
}

static void draw_settings_window(void) {
    if (!settings_open) return;
    
    int sx = 15, sy = 20, sw = 290, sh = 190;
    draw_rect(sx, sy, sw, sh, 0x07, 0x0F);
    fill_rect(sx + 2, sy + 2, sw - 4, 16, 0x01);
    vga_text(sx + 90, sy + 5, "SETTINGS", 0x0F);
    
    const char* tabs[] = {"Theme", "Wallpaper", "TitleBar", "Screensaver", "Taskbar"};
    for (int i = 0; i < 5; i++) {
        int tx = sx + 5 + i * 56;
        int ty = sy + 20;
        uint8_t bg = (settings_tab == i) ? 0x01 : 0x07;
        fill_rect(tx, ty, 54, 14, bg);
        draw_border(tx, ty, 54, 14, 0x00);
        vga_text(tx + 6, ty + 3, tabs[i], (settings_tab == i) ? 0x0F : 0x00);
        if (mouse_in(tx, ty, 54, 14)) settings_tab = i;
    }
    
    if (settings_tab == 0) {
        const char* tn[] = {"Classic", "Dark", "Forest", "Ocean", "Sunset", "Gradient"};
        for (int i = 0; i < 6; i++) {
            int iy = sy + 40 + i * 14;
            if (mouse_in(sx + 10, iy, 130, 12)) {
                fill_rect(sx + 10, iy, 130, 12, 0x01);
                vga_text(sx + 15, iy + 2, tn[i], 0x0F);
                settings.theme = i;
            } else {
                vga_text(sx + 15, iy + 2, tn[i], 0x00);
            }
            if (settings.theme == i) vga_text(sx + 145, iy + 2, "[X]", 0x0A);
        }
    }
    
    if (settings_tab == 1) {
        const char* wn[] = {"Solid", "Stripes", "Dots", "Grid", "Waves", "Stars", "Bricks", "Logo", "Grass"};
        for (int i = 0; i < 9; i++) {
            int iy = sy + 40 + i * 12;
            if (mouse_in(sx + 10, iy, 130, 10)) {
                fill_rect(sx + 10, iy, 130, 10, 0x01);
                vga_text(sx + 15, iy + 1, wn[i], 0x0F);
                settings.wallpaper = i;
            } else {
                vga_text(sx + 15, iy + 1, wn[i], 0x00);
            }
            if (settings.wallpaper == i) vga_text(sx + 145, iy + 1, "[X]", 0x0A);
        }
    }
    
    if (settings_tab == 2) {
        const char* ts[] = {"Solid", "Gradient", "Stripes"};
        for (int i = 0; i < 3; i++) {
            int iy = sy + 40 + i * 14;
            if (mouse_in(sx + 10, iy, 130, 12)) {
                fill_rect(sx + 10, iy, 130, 12, 0x01);
                vga_text(sx + 15, iy + 2, ts[i], 0x0F);
                settings.title_style = i;
            } else {
                vga_text(sx + 15, iy + 2, ts[i], 0x00);
            }
            if (settings.title_style == i) vga_text(sx + 145, iy + 2, "[X]", 0x0A);
        }
    }
    
    if (settings_tab == 3) {
        const char* ss[] = {"Bounce", "Stars", "Matrix", "Plane", "Marquee", "Snow", "Fire", "Bubbles"};
        for (int i = 0; i < 8; i++) {
            int iy = sy + 40 + i * 14;
            if (mouse_in(sx + 10, iy, 130, 12)) {
                fill_rect(sx + 10, iy, 130, 12, 0x01);
                vga_text(sx + 15, iy + 2, ss[i], 0x0F);
                settings.screensaver_type = i;
            } else {
                vga_text(sx + 15, iy + 2, ss[i], 0x00);
            }
            if (settings.screensaver_type == i) vga_text(sx + 145, iy + 2, "[X]", 0x0A);
        }
        
        vga_text(sx + 10, sy + 40 + 8*14 + 5, "Enabled:", 0x00);
        const char* state = settings.screensaver_enabled ? "[ON]" : "[OFF]";
        vga_text(sx + 80, sy + 40 + 8*14 + 5, state, settings.screensaver_enabled ? 0x0A : 0x0C);
        if (mouse_in(sx + 80, sy + 40 + 8*14 + 5, 45, 12)) {
            settings.screensaver_enabled = !settings.screensaver_enabled;
        }
        
        vga_text(sx + 10, sy + 40 + 9*14 + 5, "Timeout:", 0x00);
        char tb[8];
        int_to_str(settings.screensaver_timeout / 10, tb);
        vga_text(sx + 80, sy + 40 + 9*14 + 5, tb, 0x0E);
        vga_text(sx + 100, sy + 40 + 9*14 + 5, "sec", 0x00);
        
        draw_button(sx + 140, sy + 40 + 9*14 + 4, 16, 10, 0);
        vga_text(sx + 144, sy + 40 + 9*14 + 5, "+", 0x00);
        draw_button(sx + 158, sy + 40 + 9*14 + 4, 16, 10, 0);
        vga_text(sx + 162, sy + 40 + 9*14 + 5, "-", 0x00);
        
        if (mouse_in(sx + 140, sy + 40 + 9*14 + 4, 16, 10) && settings.screensaver_timeout < 3600) {
            settings.screensaver_timeout += 60;
        }
        if (mouse_in(sx + 158, sy + 40 + 9*14 + 4, 16, 10) && settings.screensaver_timeout > 60) {
            settings.screensaver_timeout -= 60;
        }
    }
    
    if (settings_tab == 4) {
        vga_text(sx + 10, sy + 40, "Auto-hide taskbar:", 0x00);
        vga_text(sx + 140, sy + 40, settings.taskbar_auto_hide ? "[ON]" : "[OFF]", settings.taskbar_auto_hide ? 0x0A : 0x0C);
        if (mouse_in(sx + 140, sy + 40, 45, 12)) {
            settings.taskbar_auto_hide = !settings.taskbar_auto_hide;
        }
    }
    
    int bx = sx + sw - 50;
    int by = sy + sh - 20;
    draw_button(bx, by, 40, 14, 0);
    vga_text(bx + 8, by + 3, "Save", 0x00);
    if (mouse_in(bx, by, 40, 14)) {
        save_config();
        settings_open = 0;
        show_notification("Settings saved!");
    }
}

static void draw_notification(void) {
    if (!notification_active) return;
    if (notification_timer > 0) notification_timer--;
    else { notification_active = 0; return; }
    
    int nl = str_len(notification_msg);
    int nw = nl * 6 + 20;
    draw_rect(5, 5, nw, 14, 0x09, 0x0F);
    vga_text(9, 7, notification_msg, 0x0F);
}

static void draw_file_manager(window_t* w) {
    if (w->anim_state != 0) return;
    
    int cx = w->x + 4;
    int cy = w->y + 18;
    
    vga_text(cx, cy, "Path:", 0x00);
    vga_text(cx + 30, cy, current_path, 0x01);
    vga_hline(w->x + 2, cy + 10, w->w - 4, 0x08);
    
    int ly = cy + 14;
    int vf = (w->h - 60) / 12;
    
    for (int i = 0; i < vf && i + file_scroll < current_file_count; i++) {
        int idx = i + file_scroll;
        int iy = ly + i * 11;
        uint8_t bg = (idx == file_selected) ? 0x01 : 0x07;
        uint8_t fg = (idx == file_selected) ? 0x0F : 0x00;
        fill_rect(w->x + 3, iy, w->w - 8, 9, bg);
        vga_text(cx, iy + 1, current_files[idx].is_dir ? "[DIR]" : "[   ]", current_files[idx].is_dir ? 0x0E : 0x08);
        vga_text(cx + 32, iy + 1, current_files[idx].name, fg);
    }
    
    int by = w->y + w->h - 24;
    int bh = 10;
    draw_button(cx - 2, by, 40, bh, 0);
    vga_text(cx + 4, by + 1, "Open", 0x00);
    draw_button(cx + 40, by, 42, bh, 0);
    vga_text(cx + 44, by + 1, "Rename", 0x00);
    draw_button(cx + 84, by, 40, bh, 0);
    vga_text(cx + 90, by + 1, "Delete", 0x00);
    draw_button(cx + 126, by, 30, bh, 0);
    vga_text(cx + 130, by + 1, "New", 0x00);
    draw_button(cx + 158, by, 25, bh, 0);
    vga_text(cx + 162, by + 1, "^", 0x00);
    
    int by2 = by - 12;
    draw_button(cx - 2, by2, 35, bh, 0);
    vga_text(cx + 4, by2 + 1, "Copy", 0x00);
    draw_button(cx + 35, by2, 35, bh, 0);
    vga_text(cx + 40, by2 + 1, "Cut", 0x00);
    draw_button(cx + 72, by2, 35, bh, 0);
    vga_text(cx + 76, by2 + 1, "Paste", 0x00);
}

static void handle_file_manager_click(window_t* w) {
    int cx = w->x + 4;
    int cy = w->y + 18;
    int ly = cy + 14;
    
    if (mouse_in(w->x + 3, ly, w->w - 8, w->h - 60)) {
        int idx = file_scroll + (my - ly) / 11;
        if (idx < current_file_count) file_selected = idx;
        return;
    }
    
    int by = w->y + w->h - 24;
    int by2 = by - 12;
    
    if (mouse_in(cx - 2, by, 40, 10)) {
        if (file_selected < current_file_count && current_files[file_selected].is_dir) {
            if (str_cmp(current_files[file_selected].name, "..") == 0) fm_go_up();
            else fm_open_dir(current_files[file_selected].name);
        }
    }
    if (mouse_in(cx + 40, by, 42, 10)) fm_rename_start();
    if (mouse_in(cx + 84, by, 40, 10)) fm_delete_file();
    if (mouse_in(cx + 126, by, 30, 10)) fm_new_file();
    if (mouse_in(cx + 158, by, 25, 10)) fm_go_up();
    if (mouse_in(cx - 2, by2, 35, 10)) fm_copy_file();
    if (mouse_in(cx + 35, by2, 35, 10)) fm_move_file();
    if (mouse_in(cx + 72, by2, 35, 10)) fm_paste_file();
}

static void draw_notepad_content(window_t* w) {
    if (w->anim_state != 0) return;
    int cx = w->x + 6;
    int cy = w->y + 20;
    vga_text(cx, cy, "File Edit View Help", 0x00);
    vga_hline(w->x + 2, cy + 10, w->w - 4, 0x08);
    
    int lx = cx;
    int ly = cy + 14 - w->notepad_scroll;
    for (int i = 0; i < w->notepad_len && ly < w->y + w->h - 20; i++) {
        if (w->notepad_text[i] == '\n') {
            ly += 10;
            lx = w->x + 6;
        } else {
            vga_char(lx, ly, w->notepad_text[i], 0x00);
            lx += 6;
            if (lx > w->x + w->w - 12) {
                ly += 10;
                lx = w->x + 6;
            }
        }
    }
}

static void draw_calculator_content(window_t* w) {
    if (w->anim_state != 0) return;
    int cx = w->x + 6;
    int cy = w->y + 20;
    draw_rect(cx, cy, w->w - 12, 14, 0x0F, 0x00);
    vga_text(cx + 2, cy + 3, w->calc_display, 0x00);
    
    const char* btns[] = {"7","8","9","/","C","4","5","6","*","<","1","2","3","-","%","0",".","=","+","M"};
    for (int i = 0; i < 20; i++) {
        int bx = cx + (i % 5) * 22;
        int by = cy + 18 + (i / 5) * 14;
        draw_button(bx, by, 20, 12, 0);
        vga_text(bx + 6, by + 2, btns[i], 0x00);
    }
}

static void draw_terminal_content(window_t* w) {
    if (w->anim_state != 0) return;
    int cx = w->x + 6;
    int cy = w->y + 20;
    vga_text(cx, cy, "WNKA Terminal v2.0", 0x0E);
    vga_hline(w->x + 2, cy + 10, w->w - 4, 0x08);
    vga_text(cx, cy + 14, "C:\\>", 0x0A);
    vga_text(cx + 24, cy + 14, w->shell_buf, 0x0F);
}

static void draw_help_content(window_t* w) {
    if (w->anim_state != 0) return;
    int cx = w->x + 6;
    int cy = w->y + 20;
    vga_text(cx, cy, "WNKA HELP v3.0", 0x0E);
    vga_hline(w->x + 2, cy + 10, w->w - 4, 0x08);
    
    const char* hl[] = {
        "=== CONTROLS ===",
        "WASD / Arrows - Move mouse",
        "Q - Left click",
        "R - Start Menu",
        "ESC - Close menu",
        "M - Drag windows",
        "",
        "=== APPS ===",
        "File Manager - File operations",
        "Notepad - Text editor",
        "Calculator - Math",
        "Terminal - Command line",
        "Paint - Drawing",
        "",
        "=== FEATURES ===",
        "8 screensaver types",
        "9 wallpaper styles",
        "Date/Time popup",
        "Taskbar with auto-hide",
        "Window animations"
    };
    
    int visible = 20;
    for (int i = 0; i < visible && i + w->help_scroll < 23; i++) {
        vga_text(cx, cy + 14 + i * 10, hl[i + w->help_scroll], 0x00);
    }
}

static void draw_welcome_content(window_t* w) {
    vga_text(w->x + 10, w->y + 25, "Welcome to WNKA OS v3.0!", 0x00);
    vga_text(w->x + 10, w->y + 40, "Complete desktop environment", 0x08);
    vga_text(w->x + 10, w->y + 55, "Click Start to explore!", 0x0A);
    vga_text(w->x + 10, w->y + 70, "Click on clock for date/time", 0x08);
    
    int r = 20 + (anim_frame / 10) % 5;
    int cx = w->x + w->w / 2;
    int cy = w->y + w->h / 2 - 10;
    
    for (int i = 0; i < 8; i++) {
        float a = i * 0.785f + (anim_frame * 0.05f);
        int x1 = cx + (int)(r * 0.5f * SIN(a));
        int y1 = cy + (int)(r * 0.5f * COS(a));
        int x2 = cx + (int)(r * SIN(a));
        int y2 = cy + (int)(r * COS(a));
        vga_line(x1, y1, x2, y2, 0x0E + (i % 6));
    }
    vga_text(cx - 10, cy - 4, "WNKA", 0x0F);
}

static void draw_media_player(window_t* w) {
    if (w->anim_state != 0) return;
    int cx = w->x + 6;
    int cy = w->y + 20;
    
    vga_text(cx + 20, cy + 10, "WNKA Media Player", 0x0E);
    vga_hline(w->x + 2, cy + 20, w->w - 4, 0x08);
    
    char track_str[32];
    int_to_str(w->media_track + 1, track_str);
    str_cat(track_str, "/5");
    vga_text(cx + 10, cy + 30, "Track:", 0x00);
    vga_text(cx + 60, cy + 30, track_str, 0x0F);
    
    vga_text(cx + 10, cy + 45, "Volume:", 0x00);
    char vol_str[8];
    int_to_str(w->media_volume, vol_str);
    vga_text(cx + 60, cy + 45, vol_str, 0x0F);
    
    draw_button(cx + 10, cy + 60, 40, 14, 0);
    vga_text(cx + 17, cy + 63, "Prev", 0x00);
    
    draw_button(cx + 55, cy + 60, 40, 14, w->media_playing);
    vga_text(cx + 60, cy + 63, w->media_playing ? "Pause" : "Play", 0x00);
    
    draw_button(cx + 100, cy + 60, 40, 14, 0);
    vga_text(cx + 107, cy + 63, "Next", 0x00);
    
    draw_button(cx + 55, cy + 80, 40, 14, 0);
    vga_text(cx + 58, cy + 83, "Stop", 0x00);
    
    draw_button(cx + 10, cy + 80, 30, 14, 0);
    vga_text(cx + 16, cy + 83, "Vol+", 0x00);
    
    draw_button(cx + 100, cy + 80, 30, 14, 0);
    vga_text(cx + 106, cy + 83, "Vol-", 0x00);
}

static void draw_recycle_bin(window_t* w) {
    if (w->anim_state != 0) return;
    int cx = w->x + 6;
    int cy = w->y + 20;
    
    vga_text(cx + 20, cy + 10, "Recycle Bin", 0x0E);
    vga_hline(w->x + 2, cy + 20, w->w - 4, 0x08);
    
    vga_text(cx + 10, cy + 35, "Empty", 0x08);
    vga_text(cx + 10, cy + 50, "No files in trash", 0x08);
    
    draw_button(cx + 40, cy + 70, 60, 16, 0);
    vga_text(cx + 48, cy + 73, "Empty Bin", 0x00);
}

static void update_animations(void) {
    for (int i = 0; i < win_count; i++) {
        if (wins[i].anim_state == 1) {
            wins[i].anim_timer++;
            if (wins[i].anim_timer > 10) {
                wins[i].anim_state = 0;
                wins[i].anim_timer = 0;
                wins[i].visible = 1;
            }
        }
        else if (wins[i].anim_state == 2) {
            wins[i].anim_timer++;
            if (wins[i].anim_timer > 10) {
                wins[i].anim_state = 0;
                wins[i].anim_timer = 0;
                wins[i].visible = 0;
                wins[i].minimized = 1;
            }
        }
        else if (wins[i].anim_state == 3) {
            wins[i].anim_timer++;
            if (wins[i].anim_timer > 10) {
                wins[i].anim_state = 0;
                wins[i].anim_timer = 0;
                wins[i].minimized = 1;
            }
        }
        else if (wins[i].anim_state == 4) {
            wins[i].anim_timer++;
            if (wins[i].anim_timer > 10) {
                wins[i].anim_state = 0;
                wins[i].anim_timer = 0;
                wins[i].minimized = 0;
            }
        }
    }
}

static void draw_window_frame(window_t* w) {
    if (!w->visible || w->minimized) return;
    
    float scale = 1.0f;
    if (w->anim_state == 1) scale = 0.1f + w->anim_timer * 0.9f / 10.0f;
    else if (w->anim_state == 2) scale = 1.0f - w->anim_timer * 0.9f / 10.0f;
    
    int sw = (int)(w->w * scale);
    int sh = (int)(w->h * scale);
    int sx = w->x + (w->w - sw) / 2;
    int sy = w->y + (w->h - sh) / 2;
    
    if (settings.window_shadows && scale > 0.3f) {
        fill_rect(sx + 2, sy + sh, sw, 2, 0x08);
        fill_rect(sx + sw, sy + 2, 2, sh, 0x08);
    }
    
    draw_rect(sx, sy, sw, sh, theme_window[settings.theme], 0x00);
    draw_title_bar(sx + 2, sy + 2, sw - 4, settings.title_height, theme_title[settings.theme], w->active);
    
    if (scale > 0.3f) vga_text(sx + 6, sy + 3, w->title, 0x0F);
    if (scale > 0.5f) {
        draw_button(sx + sw - 18, sy + 2, 14, 12, 0);
        vga_text(sx + sw - 15, sy + 3, "X", 0x00);
        draw_button(sx + sw - 34, sy + 2, 14, 12, 0);
        vga_text(sx + sw - 31, sy + 3, "_", 0x00);
    }
}

static int get_input(void) {
    if (vga_inb(0x64) & 1) {
        uint8_t sc = vga_inb(0x60);
        if (sc < 0x80) {
            if (sc == 0x11 || sc == 0x48) return 1;
            if (sc == 0x1F || sc == 0x50) return 2;
            if (sc == 0x1E || sc == 0x4B) return 3;
            if (sc == 0x20 || sc == 0x4D) return 4;
            if (sc == settings.click_key) return 5;
            if (sc == settings.menu_key) return 8;
            if (sc == 0x01) return 7;
            if (sc == 0x32) return 10;
            if (sc == 0x1C) return 9;
            if (sc == 0x0E) return 12;
            if (sc >= 0x10 && sc <= 0x19) return 'q' + (sc - 0x10);
            if (sc >= 0x1E && sc <= 0x26) return 'a' + (sc - 0x1E);
            if (sc >= 0x2C && sc <= 0x32) return 'z' + (sc - 0x2C);
            if (sc >= 0x02 && sc <= 0x0B) return '1' + (sc - 0x02);
        }
    }
    return 0;
}

static void first_time_setup(void) {
    int tab = 0, sel = 0, done = 0, logo_x = 260, logo_y = 100;
    while (!done) {
        int key = 0;
        if (vga_inb(0x64) & 1) {
            uint8_t sc = vga_inb(0x60);
            if (sc < 0x80) {
                if (sc == 0x11 || sc == 0x48) key = 1;
                if (sc == 0x1F || sc == 0x50) key = 2;
                if (sc == 0x1E || sc == 0x4B) key = 3;
                if (sc == 0x20 || sc == 0x4D) key = 4;
                if (sc == 0x10) key = 5;
                if (sc == 0x1C) key = 6;
            }
        }
        if (key == 1 && sel > 0) sel--;
        if (key == 2 && sel < 5) sel++;
        if (key == 3 && tab > 0) tab--;
        if (key == 4 && tab < 3) tab++;
        if (key == 5) { if (tab == 0) settings.theme = sel; if (tab == 1) settings.wallpaper = sel; }
        if (key == 6) { save_config(); done = 1; }
        if (logo_x > 30) logo_x -= 3;
        
        vga_clear(0x01);
        int r = 20;
        for (int i = 0; i < 8; i++) {
            float a = i * 0.785f;
            int x1 = logo_x + (int)(r * 0.5f * SIN(a));
            int y1 = logo_y + (int)(r * 0.5f * COS(a));
            int x2 = logo_x + (int)(r * SIN(a));
            int y2 = logo_y + (int)(r * COS(a));
            vga_line(x1, y1, x2, y2, 0x0E + (i % 6));
        }
        vga_text(logo_x - 10, logo_y - 4, "WNKA", 0x0F);
        
        if (logo_x <= 30) {
            draw_rect(30, 60, 260, 30, 0x07, 0x0F);
            vga_text(55, 68, "WELCOME TO WNKA OS!", 0x0F);
            
            int sx = 15, sy = 100, sw = 290, sh = 120;
            draw_rect(sx, sy, sw, sh, 0x07, 0x0F);
            draw_rect(sx + 2, sy + 2, sw - 4, 16, 0x01, 0x01);
            vga_text(sx + 80, sy + 5, "INITIAL SETUP", 0x0F);
            
            const char* tabs[] = {"Theme", "Wallpaper", "Keys", "Windows"};
            for (int i = 0; i < 4; i++) {
                int tx = sx + 5 + i * 72;
                int ty = sy + 20;
                uint8_t bg = (tab == i) ? 0x01 : 0x07;
                fill_rect(tx, ty, 70, 14, bg);
                draw_border(tx, ty, 70, 14, 0x00);
                vga_text(tx + 8, ty + 3, tabs[i], (tab == i) ? 0x0F : 0x00);
            }
            
            if (tab == 0) {
                const char* tn[] = {"Classic", "Dark", "Forest", "Ocean", "Sunset", "Gradient"};
                for (int i = 0; i < 6; i++) {
                    int iy = sy + 38 + i * 12;
                    fill_rect(sx + 10, iy, 120, 10, (sel == i) ? 0x01 : 0x07);
                    vga_text(sx + 15, iy + 1, tn[i], (sel == i) ? 0x0F : 0x00);
                }
            }
            if (tab == 1) {
                const char* wn[] = {"Solid", "Stripes", "Dots", "Grid", "Waves", "Stars"};
                for (int i = 0; i < 6; i++) {
                    int iy = sy + 38 + i * 12;
                    fill_rect(sx + 10, iy, 120, 10, (sel == i) ? 0x01 : 0x07);
                    vga_text(sx + 15, iy + 1, wn[i], (sel == i) ? 0x0F : 0x00);
                }
            }
            vga_text(sx + 10, sy + 100, "Q:Select  ENTER:Save & Reboot", 0x0F);
        }
        vga_vsync(); vga_flip(); vga_delay(1800);
    }
    
    vga_clear(0x01);
    draw_rect(50, 100, 220, 40, 0x07, 0x0F);
    vga_text(80, 112, "Configuration saved!", 0x0A);
    vga_text(60, 126, "Press any key to reboot...", 0x0F);
    vga_vsync(); vga_flip();
    
    while (1) {
        if (vga_inb(0x64) & 1) { vga_inb(0x60); break; }
    }
    vga_exit(); vga_outb(0x64, 0xFE);
}
static void draw_progress_bar(int x, int y, int w, int progress, uint8_t color) {
    fill_rect(x, y, w, 10, 0x08);
    draw_border(x, y, w, 10, 0x0F);
    
    int fill_w = w * progress / 100;
    if (fill_w > 0) {
        fill_rect(x + 1, y + 1, fill_w - 1, 8, color);
    }
}
static void draw_load(void) {
    vga_clear(0x00);
    vga_text(120, 100, "Loading WnkUI", 0x0F);

    for (int i = 0; i <= 100; i += 1) {
        draw_progress_bar(60, 120, 200, i, 0x0A);
        vga_flip();
        vga_delay(100000);

    }
}
void wnkcui_run(void) {
    vga_init();
    draw_load();
    if (!load_config()) { first_time_setup(); return; }
    vga_palette(0x01, 0, 25, 55);
    vga_palette(0x07, 35, 35, 35);
    read_cmos_time();
    fm_refresh();
    
    win_count = 11;
    
    wins[0].x = 15;  wins[0].y = 10;  wins[0].w = 200; wins[0].h = 185;
    wins[0].visible = 1; wins[0].minimized = 0; wins[0].active = 1;
    wins[0].type = 0; wins[0].anim_state = 0;
    str_cpy(wins[0].title, "My Computer");
    
    wins[1].x = 35;  wins[1].y = 25;  wins[1].w = 250; wins[1].h = 170;
    wins[1].visible = 1; wins[1].minimized = 0; wins[1].active = 0;
    wins[1].type = 1; wins[1].anim_state = 0;
    str_cpy(wins[1].title, "Welcome");
    
    wins[2].x = 0;   wins[2].y = 0;   wins[2].w = 260; wins[2].h = 180;
    wins[2].visible = 0; wins[2].minimized = 1; wins[2].active = 0;
    wins[2].type = 2; wins[2].anim_state = 0;
    str_cpy(wins[2].title, "Notepad");
    notepad_init(&wins[2]);
    
    wins[3].x = 0;   wins[3].y = 0;   wins[3].w = 200; wins[3].h = 160;
    wins[3].visible = 0; wins[3].minimized = 1; wins[3].active = 0;
    wins[3].type = 3; wins[3].anim_state = 0;
    str_cpy(wins[3].title, "Calculator");
    calculator_init(&wins[3]);
    
    wins[4].x = 0;   wins[4].y = 0;   wins[4].w = 280; wins[4].h = 180;
    wins[4].visible = 0; wins[4].minimized = 1; wins[4].active = 0;
    wins[4].type = 4; wins[4].anim_state = 0;
    str_cpy(wins[4].title, "Terminal");
    terminal_init(&wins[4]);
    
    wins[5].x = 0;   wins[5].y = 0;   wins[5].w = 280; wins[5].h = 200;
    wins[5].visible = 0; wins[5].minimized = 1; wins[5].active = 0;
    wins[5].type = 5; wins[5].anim_state = 0;
    str_cpy(wins[5].title, "Paint");
    paint_init(&wins[5]);
    
    wins[6].x = 0;   wins[6].y = 0;   wins[6].w = 280; wins[6].h = 180;
    wins[6].visible = 0; wins[6].minimized = 1; wins[6].active = 0;
    wins[6].type = 6; wins[6].anim_state = 0;
    str_cpy(wins[6].title, "Media Player");
    wins[6].media_playing = 0;
    wins[6].media_track = 0;
    wins[6].media_volume = 75;
    
    wins[7].x = 0;   wins[7].y = 0;   wins[7].w = 250; wins[7].h = 180;
    wins[7].visible = 0; wins[7].minimized = 1; wins[7].active = 0;
    wins[7].type = 7; wins[7].anim_state = 0;
    str_cpy(wins[7].title, "Recycle Bin");
    
    wins[8].x = 0;   wins[8].y = 0;   wins[8].w = 200; wins[8].h = 150;
    wins[8].visible = 0; wins[8].minimized = 1; wins[8].active = 0;
    wins[8].type = 8; wins[8].anim_state = 0;
    str_cpy(wins[8].title, "Settings");
    
    wins[9].x = 0;   wins[9].y = 0;   wins[9].w = 250; wins[9].h = 180;
    wins[9].visible = 0; wins[9].minimized = 1; wins[9].active = 0;
    wins[9].type = 9; wins[9].anim_state = 0; wins[9].help_scroll = 0;
    str_cpy(wins[9].title, "Help");
    
    wins[10].x = 0;  wins[10].y = 0;  wins[10].w = 220; wins[10].h = 150;
    wins[10].visible = 0; wins[10].minimized = 1; wins[10].active = 0;
    wins[10].type = 10; wins[10].anim_state = 0;
    str_cpy(wins[10].title, "About");
    
    int running = 1;
    int frame = 0;
    last_mx = mx;
    last_my = my;
    
    while (running) {
        int key = get_input();
        if (frame % 30 == 0) read_cmos_time();
        
        if (mx != last_mx || my != last_my) {
            idle_frames = 0;
            last_mx = mx;
            last_my = my;
        } else {
            idle_frames++;
        }
        if (settings.screensaver_enabled && idle_frames > settings.screensaver_timeout && !start_menu && !power_dialog && !settings_open && !clock_popup) {
            screensaver_active = 1;
            key = 0;
        }
        if (screensaver_active) {
            if (vga_inb(0x64) & 1) {
                uint8_t sc = vga_inb(0x60);
                if (sc < 0x80) {
                    screensaver_active = 0;
                    idle_frames = 0;
                    ss_frame = 0;
                    continue;
                }
            }
            if (mx != last_mx || my != last_my) {
                screensaver_active = 0;
                idle_frames = 0;
                ss_frame = 0;
                continue;
            }
            draw_screensaver();
            vga_vsync();
            vga_flip();
            vga_delay(5000);
            ss_frame++;
            anim_frame++;
            frame++;
            continue;
        }
        
        if (key == 1 && my > 0) my -= 3;
        if (key == 2 && my < 239) my += 3;
        if (key == 3 && mx > 0) mx -= 3;
        if (key == 4 && mx < 319) mx += 3;
        if (mx < 0) mx = 0;
        if (mx > 319) mx = 319;
        if (my < 0) my = 0;
        if (my > 239) my = 239;
        
        if (!start_menu) programs_submenu = 0;
        update_animations();
        
        if (key == 10) { drag_mode = !drag_mode; drag_win = -1; }
        if (drag_mode && key == 5) {
            if (drag_win == -1) {
                for (int i = win_count - 1; i >= 0; i--) {
                    if (!wins[i].visible || wins[i].minimized) continue;
                    if (mouse_in(wins[i].x, wins[i].y, wins[i].w, 16)) {
                        drag_win = i;
                        drag_off_x = mx - wins[i].x;
                        drag_off_y = my - wins[i].y;
                        for (int j = 0; j < win_count; j++) wins[j].active = 0;
                        wins[i].active = 1;
                        break;
                    }
                }
            } else {
                drag_win = -1;
            }
        }
        if (drag_mode && drag_win >= 0) {
            wins[drag_win].x = mx - drag_off_x;
            wins[drag_win].y = my - drag_off_y;
        }
        
        if (wins[2].active && !wins[2].minimized && !drag_mode) notepad_handle_key(&wins[2], key);
        if (wins[4].active && !wins[4].minimized && !drag_mode) terminal_handle_key(&wins[4], key);
        if (rename_active) {
            if (key >= ' ' && key <= 'z' && rename_pos < 62) { rename_buf[rename_pos++] = key; rename_buf[rename_pos] = 0; }
            if (key == 12 && rename_pos > 0) { rename_pos--; rename_buf[rename_pos] = 0; }
            if (key == 9) { fm_apply_rename(); rename_active = 0; }
        }
        if (wins[9].active && !wins[9].minimized) {
            if (key == 2 && wins[9].help_scroll < 10) wins[9].help_scroll++;
            if (key == 1 && wins[9].help_scroll > 0) wins[9].help_scroll--;
        }
        
        if (key == 7) {
            if (dialog_active) dialog_active = 0;
            else if (power_dialog) power_dialog = 0;
            else if (settings_open) settings_open = 0;
            else if (clock_popup) clock_popup = 0;
            else if (start_menu) start_menu = 0;
        }
        
        if (key == 8 && !drag_mode) {
            start_menu = !start_menu;
            clock_popup = 0;
        }
        
        if (key == 5 && !drag_mode && !rename_active) {
            if (dialog_active) {
                dialog_active = 0;
            }
            else if (power_dialog) {
                if (mouse_in(105, 109, 50, 18)) { vga_exit(); vga_outb(0x64, 0xFE); return; }
                if (mouse_in(165, 109, 55, 18)) { vga_exit(); vga_outb(0x64, 0xFE); return; }
                power_dialog = 0;
            }
            else if (clock_popup) {
                clock_popup = 0;
            }
            else if (start_menu) {
                if (programs_submenu) {
                    int psx = 159, psy = 240 - 171;
                    if (mouse_in(psx, psy + 2, 138, 12)) {
                        start_menu = 0;
                        for (int j = 0; j < win_count; j++) wins[j].active = 0;
                        wins[0].visible = 1; wins[0].minimized = 0; wins[0].active = 1; wins[0].anim_state = 4;
                        fm_refresh();
                    }
                    else if (mouse_in(psx, psy + 16, 138, 12)) {
                        start_menu = 0;
                        for (int j = 0; j < win_count; j++) wins[j].active = 0;
                        wins[2].visible = 1; wins[2].minimized = 0; wins[2].active = 1; wins[2].anim_state = 4;
                    }
                    else if (mouse_in(psx, psy + 30, 138, 12)) {
                        start_menu = 0;
                        for (int j = 0; j < win_count; j++) wins[j].active = 0;
                        wins[3].visible = 1; wins[3].minimized = 0; wins[3].active = 1; wins[3].anim_state = 4;
                    }
                    else if (mouse_in(psx, psy + 44, 138, 12)) {
                        start_menu = 0;
                        for (int j = 0; j < win_count; j++) wins[j].active = 0;
                        wins[4].visible = 1; wins[4].minimized = 0; wins[4].active = 1; wins[4].anim_state = 4;
                    }
                    else if (mouse_in(psx, psy + 58, 138, 12)) {
                        start_menu = 0;
                        for (int j = 0; j < win_count; j++) wins[j].active = 0;
                        wins[5].visible = 1; wins[5].minimized = 0; wins[5].active = 1; wins[5].anim_state = 4;
                    }
                    else if (mouse_in(psx, psy + 72, 138, 12)) {
                        start_menu = 0;
                        for (int j = 0; j < win_count; j++) wins[j].active = 0;
                        wins[6].visible = 1; wins[6].minimized = 0; wins[6].active = 1; wins[6].anim_state = 4;
                    }
                    else if (mouse_in(psx, psy + 86, 138, 12)) {
                        start_menu = 0;
                        settings_open = 1;
                    }
                    else if (mouse_in(psx, psy + 100, 138, 12)) {
                        start_menu = 0;
                        for (int j = 0; j < win_count; j++) wins[j].active = 0;
                        wins[1].visible = 1; wins[1].minimized = 0; wins[1].active = 1; wins[1].anim_state = 4;
                    }
                    else if (mouse_in(psx, psy + 114, 138, 12)) {
                        start_menu = 0;
                        for (int j = 0; j < win_count; j++) wins[j].active = 0;
                        wins[9].visible = 1; wins[9].minimized = 0; wins[9].active = 1; wins[9].anim_state = 4;
                    }
                    else {
                        programs_submenu = 0;
                    }
                }
                else if (mouse_in(24, 240 - 155 + 4 + 6 * 13, 131, 11)) {
                    start_menu = 0;
                    power_dialog = 1;
                }
                else if (mouse_in(24, 240 - 155 + 4 + 1 * 13, 131, 11)) {
                    start_menu = 0;
                    settings_open = 1;
                }
                else {
                    start_menu = 0;
                }
            }
            else {
                if (mouse_in(270, 180, 48, 14)) {
                    clock_popup = !clock_popup;
                }
                
                int over_window = is_mouse_over_window();
                
                if (!over_window && !clock_popup) {
                    if (mouse_in(6, 6, 36, 36)) {  
                        for (int j = 0; j < win_count; j++) wins[j].active = 0;
                        wins[0].visible = 1; wins[0].minimized = 0; wins[0].active = 1; wins[0].anim_state = 4;
                        fm_refresh();
                    }
                    else if (mouse_in(56, 6, 36, 36)) { 
                        for (int j = 0; j < win_count; j++) wins[j].active = 0;
                        wins[1].visible = 1; wins[1].minimized = 0; wins[1].active = 1; wins[1].anim_state = 4;
                    }
                    else if (mouse_in(106, 6, 36, 36)) { 
                        for (int j = 0; j < win_count; j++) wins[j].active = 0;
                        wins[2].visible = 1; wins[2].minimized = 0; wins[2].active = 1; wins[2].anim_state = 4;
                    }
                    else if (mouse_in(156, 6, 36, 36)) { 
                        for (int j = 0; j < win_count; j++) wins[j].active = 0;
                        wins[3].visible = 1; wins[3].minimized = 0; wins[3].active = 1; wins[3].anim_state = 4;
                    }
                    else if (mouse_in(206, 6, 36, 36)) { 
                        for (int j = 0; j < win_count; j++) wins[j].active = 0;
                        wins[4].visible = 1; wins[4].minimized = 0; wins[4].active = 1; wins[4].anim_state = 4;
                    }
                    else if (mouse_in(256, 6, 36, 36)) {  
                        for (int j = 0; j < win_count; j++) wins[j].active = 0;
                        wins[5].visible = 1; wins[5].minimized = 0; wins[5].active = 1; wins[5].anim_state = 4;
                    }
                    else if (mouse_in(6, 56, 36, 36)) {  
                        settings_open = 1;
                    }
                    else if (mouse_in(56, 56, 36, 36)) {  
                        for (int j = 0; j < win_count; j++) wins[j].active = 0;
                        wins[9].visible = 1; wins[9].minimized = 0; wins[9].active = 1; wins[9].anim_state = 4;
                    }
                    else if (mouse_in(106, 56, 36, 36)) {  
                        for (int j = 0; j < win_count; j++) wins[j].active = 0;
                        wins[6].visible = 1; wins[6].minimized = 0; wins[6].active = 1; wins[6].anim_state = 4;
                    }
                    else if (mouse_in(156, 56, 36, 36)) {  
                        for (int j = 0; j < win_count; j++) wins[j].active = 0;
                        wins[7].visible = 1; wins[7].minimized = 0; wins[7].active = 1; wins[7].anim_state = 4;
                    }
                }
                
                for (int i = win_count - 1; i >= 0; i--) {
                    if (!wins[i].visible || wins[i].minimized) continue;
                    if (mouse_in(wins[i].x, wins[i].y, wins[i].w, wins[i].h)) {
                        for (int j = 0; j < win_count; j++) wins[j].active = 0;
                        wins[i].active = 1;
                        if (wins[i].type == 0) handle_file_manager_click(&wins[i]);
                        if (wins[i].type == 3) calculator_handle_click(&wins[i], mx, my);
                        if (wins[i].type == 5) paint_handle_click(&wins[i], mx, my);
                        if (mouse_in(wins[i].x + wins[i].w - 18, wins[i].y + 2, 14, 12)) wins[i].anim_state = 2;
                        if (mouse_in(wins[i].x + wins[i].w - 34, wins[i].y + 2, 14, 12)) wins[i].anim_state = 3;
                        break;
                    }
                }
                
                int ty = 240 - 60;
                int bx = 62;
                for (int i = 0; i < win_count; i++) {
                    if (!wins[i].visible) continue;
                    int bw = str_len(wins[i].title) * 6 + 10;
                    if (mouse_in(bx, ty + 2, bw, 14)) {
                        for (int j = 0; j < win_count; j++) wins[j].active = 0;
                        wins[i].active = 1;
                        wins[i].minimized = !wins[i].minimized;
                        if (!wins[i].minimized) wins[i].anim_state = 4;
                        break;
                    }
                    bx += bw + 3;
                }
                
                if (wins[6].active && !wins[6].minimized) {
                    int cx = wins[6].x + 6;
                    int cy = wins[6].y + 20;
                    if (mouse_in(cx + 10, cy + 60, 40, 14)) {  
                        wins[6].media_track = (wins[6].media_track - 1 + 5) % 5;
                        show_notification("Previous track");
                    }
                    if (mouse_in(cx + 55, cy + 60, 40, 14)) {  
                        wins[6].media_playing = !wins[6].media_playing;
                        show_notification(wins[6].media_playing ? "Playing" : "Paused");
                    }
                    if (mouse_in(cx + 100, cy + 60, 40, 14)) {  
                        wins[6].media_track = (wins[6].media_track + 1) % 5;
                        show_notification("Next track");
                    }
                    if (mouse_in(cx + 55, cy + 80, 40, 14)) {  
                        wins[6].media_playing = 0;
                        show_notification("Stopped");
                    }
                    if (mouse_in(cx + 10, cy + 80, 30, 14)) { 
                        wins[6].media_volume += 5;
                        if (wins[6].media_volume > 100) wins[6].media_volume = 100;
                    }
                    if (mouse_in(cx + 100, cy + 80, 30, 14)) { 
                        wins[6].media_volume -= 5;
                        if (wins[6].media_volume < 0) wins[6].media_volume = 0;
                    }
                }
            }
        }
        
        vga_clear(theme_desktop[settings.theme]);
        draw_wallpaper();
        draw_desktop_icons();
        
        for (int i = 0; i < win_count; i++) {
            if (wins[i].visible && !wins[i].minimized) {
                draw_window_frame(&wins[i]);
                if (wins[i].anim_state == 0) {
                    switch (wins[i].type) {
                        case 0: draw_file_manager(&wins[i]); break;
                        case 1: draw_welcome_content(&wins[i]); break;
                        case 2: draw_notepad_content(&wins[i]); break;
                        case 3: draw_calculator_content(&wins[i]); break;
                        case 4: draw_terminal_content(&wins[i]); break;
                        case 5: paint_draw_tools(&wins[i]); break;
                        case 6: draw_media_player(&wins[i]); break;
                        case 7: draw_recycle_bin(&wins[i]); break;
                        case 9: draw_help_content(&wins[i]); break;
                    }
                }
            }
        }
        
        draw_start_menu();
        draw_power_dialog();
        draw_settings_window();
        draw_taskbar();
        draw_clock_popup();
        draw_notification();
        
        if (rename_active) {
            draw_rect(wins[0].x + 6, wins[0].y + 20 + 16 + 12 * 6, 150, 14, 0x0F, 0x00);
            vga_text(wins[0].x + 8, wins[0].y + 20 + 16 + 12 * 6 + 3, rename_buf, 0x00);
        }
        
        draw_cursor();
        
        vga_vsync();
        vga_flip();
        anim_frame++;
        frame++;
        vga_delay(1800 - settings.animation_speed * 100);
    }
    vga_exit();
}

void wnkcui_init(int s) { (void)s; }
void wnkcui_exit(void) {}