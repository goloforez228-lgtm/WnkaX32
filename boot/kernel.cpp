#include "video.h"
#include "themes.h"
#include "string_utils.h"
#include "sounds.h"
#include "exception.h"
#include "install.h"
#include "ata.h"
#include "syscall_linux.h"
#include "wnkui.h"
#include "wnkc.h"
#include "memory_protect.h"
#include "watchdog.h"
#include "graph.h"

#define NULL 0
int seconds = 0;
int cursor_x = 0;
int cursor_y = 0;
char input_buffer[256];
int input_ptr = 0;
bool shift_pressed = false;

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

extern "C" void idt_load(); 
extern "C" void process_command(char* buf, int& ptr);
extern void update_time_display(void);
extern "C" void process_debug_command(char* buf, int& ptr);
extern void kprint_int(int num); 

static void my_sprintf(char* buf, const char* fmt, int a, int b, int c) {
    char* p = buf;
    const char* f = fmt;
    
    while(*f) {
        if(*f == '%' && *(f+1) == 'd') {

            int num = a;
            if(num == 0) {
                *p++ = '0';
            } else {
                char temp[12];
                int i = 0;
                while(num > 0) {
                    temp[i++] = '0' + (num % 10);
                    num /= 10;
                }
                while(i > 0) {
                    *p++ = temp[--i];
                }
            }
            f += 2;
            
            while(*f && *f != '%') {
                *p++ = *f++;
            }
            if(*f == '%') {
                f++;
                if(*f == 'd') {
                    num = b;
                    if(num == 0) {
                        *p++ = '0';
                    } else {
                        char temp[12];
                        int i = 0;
                        while(num > 0) {
                            temp[i++] = '0' + (num % 10);
                            num /= 10;
                        }
                        while(i > 0) {
                            *p++ = temp[--i];
                        }
                    }
                    f += 2;
                    
                    while(*f && *f != '%') {
                        *p++ = *f++;
                    }
                    if(*f == '%') {
                        f++;
                        if(*f == 'd') {
                            num = c;
                            if(num == 0) {
                                *p++ = '0';
                            } else {
                                char temp[12];
                                int i = 0;
                                while(num > 0) {
                                    temp[i++] = '0' + (num % 10);
                                    num /= 10;
                                }
                                while(i > 0) {
                                    *p++ = temp[--i];
                                }
                            }
                            f += 2;
                        }
                    }
                }
            }
        } else {
            *p++ = *f++;
        }
    }
    *p = '\0';
}

static const char* my_strstr(const char* haystack, const char* needle) {
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
        if(match) return &haystack[i];
    }
    return NULL;
}

void kprint_int(int num) {
    if (num == 0) {
        kprint("0");
        return;
    }
    char buf[12];
    int i = 10;
    buf[11] = '\0';
    while (num > 0 && i >= 0) {
        buf[i--] = (num % 10) + '0';
        num /= 10;
    }
    kprint(&buf[i+1]);
}
void print_log_line(int row, const char* status, const char* msg, uint8_t color) {
    volatile char* video = (volatile char*)0xB8000;
    int offset = row * 160 + 4;

    const char* prefix = "[  0.0000  ] ";
    for(int i=0; prefix[i]; i++) { 
        video[offset++] = prefix[i]; 
        video[offset++] = 0x07; 
    }

    for(int i=0; status[i]; i++) { 
        video[offset++] = status[i]; 
        video[offset++] = color; 
    }
    video[offset++] = ':'; 
    video[offset++] = color;
    video[offset++] = ' '; 
    video[offset++] = 0x07;

    for(int i=0; msg[i]; i++) { 
        video[offset++] = msg[i]; 
        video[offset++] = 0x07; 
    }
}

void get_cpu_model(char* buffer) {
    uint32_t* b = (uint32_t*)buffer;
    for (int i = 0; i < 3; i++) {
        uint32_t eax_val = 0x80000002 + i;
        __asm__ volatile ("cpuid"
            : "=a"(b[i*4]), "=b"(b[i*4+1]), "=c"(b[i*4+2]), "=d"(b[i*4+3])
            : "a"(eax_val));
    }
    buffer[48] = '\0';
}
static void hex_to_str(uint32_t val, char* buf) {
    const char* hex = "0123456789ABCDEF";
    buf[0] = '0';
    buf[1] = 'x';
    for(int i = 0; i < 8; i++) {
        buf[9 - i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[10] = '\0';
}

static void dec_to_str(int val, char* buf) {
    if(val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    char temp[12];
    int i = 0;
    int num = val;
    while(num > 0) {
        temp[i++] = (num % 10) + '0';
        num /= 10;
    }
    for(int j = 0; j < i; j++) {
        buf[j] = temp[i - 1 - j];
    }
    buf[i] = '\0';
}

static void str_copy(char* dest, const char* src) {
    while(*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}
static void print_panic_line(int line, const char* label, const char* value, uint8_t color) {
    volatile char* video = (volatile char*)0xB8000;
    int offset = line * 160;
    
    int i = 0;
    while(label[i] && i < 10) {
        video[offset + i*2] = label[i];
        video[offset + i*2 + 1] = color;
        i++;
    }
    
    int j = 0;
    while(value[j] && i + j < 70) {
        video[offset + (i + j)*2] = value[j];
        video[offset + (i + j)*2 + 1] = color;
        j++;
    }
}
extern "C" void play_sound(uint32_t nFrequence) {
    if (nFrequence == 0) return;
    uint32_t Div = 1193180 / nFrequence;
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(Div));
    outb(0x42, (uint8_t)(Div >> 8));
    uint8_t tmp = inb(0x61);
    if (tmp != (tmp | 3)) outb(0x61, tmp | 3);
}

extern "C" void nosound() {
    outb(0x61, inb(0x61) & 0xFC);
}
extern "C" void kernel_panic(const char* message, const char* file, int line, const char* func) {
    __asm__ volatile("cli");
    
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
    
    char cpu_name[64];
    const char* selected_cpu = cpu_list[cpu_selected];
    for(int i = 0; selected_cpu[i] && i < 60; i++) {
        cpu_name[i] = selected_cpu[i];
    }
    cpu_name[60] = '\0';
    
    uint32_t real_cpu_id = 0;
    char real_cpu_vendor[13] = {0};
    __asm__ volatile("cpuid" : "=a"(real_cpu_id) : "a"(0));
    __asm__ volatile("cpuid" : "=b"(real_cpu_vendor[0]), "=d"(real_cpu_vendor[4]), "=c"(real_cpu_vendor[8]) : "a"(0));
    
uint32_t eax, ebx, ecx, edx, esp, ebp, eip;
__asm__ volatile(
    "mov %%eax, %0\n"
    "mov %%ebx, %1\n"
    "mov %%ecx, %2\n"
    "mov %%edx, %3\n"
    "mov %%esp, %4\n"
    "mov %%ebp, %5\n"
    "call 1f\n"
    "1: pop %6\n"
    : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx), "=m"(esp), "=m"(ebp), "=m"(eip)
    :
    : "memory"
);
    
    volatile char* video = (volatile char*)0xB8000;
    for (int i = 0; i < 80 * 25; i++) {
        video[i*2] = ' ';
        video[i*2+1] = 0x4F;
    }
    
    int line_num = 0;
    
    const char* panic_title = "!!! KERNEL PANIC !!!";
    int title_x = (80 - 20) / 2;
    for(int i = 0; panic_title[i]; i++) {
        video[line_num * 160 + i*2] = panic_title[i];
        video[line_num * 160 + i*2 + 1] = 0x4F;
    }
    line_num += 2;
    
    print_panic_line(line_num++, "Error:", message, 0x4F);
    line_num++;
    
    char location[256];
    int pos = 0;
    const char* f = file;
    while(*f && pos < 100) location[pos++] = *f++;
    location[pos++] = ':';
    char line_buf[10];
    int temp = line;
    int digits = 0;
    while(temp) { digits++; temp /= 10; }
    temp = line;
    for(int i = digits-1; i >= 0; i--) {
        line_buf[i] = (temp % 10) + '0';
        temp /= 10;
    }
    for(int i = 0; i < digits; i++) location[pos++] = line_buf[i];
    location[pos++] = ' ';
    location[pos++] = '(';
    while(*func) location[pos++] = *func++;
    location[pos++] = ')';
    location[pos] = '\0';
    
    print_panic_line(line_num++, "Location:", location, 0x4F);
    line_num++;
    
    print_panic_line(line_num++, "Selected CPU:", cpu_name, 0x4F);
    line_num++;
    
    char real_cpu[64];
    for(int i = 0; i < 12; i++) real_cpu[i] = real_cpu_vendor[i];
    real_cpu[12] = '\0';
    print_panic_line(line_num++, "Real CPU:", real_cpu, 0x4F);
    line_num++;
    
    char regs[256];
    int reg_pos = 0;
    
    const char* eax_str = "EAX:";
    for(int i = 0; eax_str[i]; i++) regs[reg_pos++] = eax_str[i];
    hex_to_str(eax, &regs[reg_pos]);
    while(regs[reg_pos]) reg_pos++;
    regs[reg_pos++] = ' ';
    
    const char* ebx_str = "EBX:";
    for(int i = 0; ebx_str[i]; i++) regs[reg_pos++] = ebx_str[i];
    hex_to_str(ebx, &regs[reg_pos]);
    while(regs[reg_pos]) reg_pos++;
    regs[reg_pos++] = ' ';
    
    const char* ecx_str = "ECX:";
    for(int i = 0; ecx_str[i]; i++) regs[reg_pos++] = ecx_str[i];
    hex_to_str(ecx, &regs[reg_pos]);
    while(regs[reg_pos]) reg_pos++;
    regs[reg_pos++] = ' ';
    
    const char* edx_str = "EDX:";
    for(int i = 0; edx_str[i]; i++) regs[reg_pos++] = edx_str[i];
    hex_to_str(edx, &regs[reg_pos]);
    while(regs[reg_pos]) reg_pos++;
    regs[reg_pos] = '\0';
    
    print_panic_line(line_num++, "Regs:", regs, 0x4F);
    
    reg_pos = 0;
    const char* esp_str = "ESP:";
    for(int i = 0; esp_str[i]; i++) regs[reg_pos++] = esp_str[i];
    hex_to_str(esp, &regs[reg_pos]);
    while(regs[reg_pos]) reg_pos++;
    regs[reg_pos++] = ' ';
    
    const char* ebp_str = "EBP:";
    for(int i = 0; ebp_str[i]; i++) regs[reg_pos++] = ebp_str[i];
    hex_to_str(ebp, &regs[reg_pos]);
    while(regs[reg_pos]) reg_pos++;
    regs[reg_pos++] = ' ';
    
    const char* eip_str = "EIP:";
    for(int i = 0; eip_str[i]; i++) regs[reg_pos++] = eip_str[i];
    hex_to_str(eip, &regs[reg_pos]);
    while(regs[reg_pos]) reg_pos++;
    regs[reg_pos] = '\0';
    
    print_panic_line(line_num++, "", regs, 0x4F);
    line_num++;
    
    const char* footer = "System halted. Press any key to reboot...";
    int footer_x = (80 - 40) / 2;
    for(int i = 0; footer[i]; i++) {
        video[23 * 160 + footer_x*2 + i*2] = footer[i];
        video[23 * 160 + footer_x*2 + i*2 + 1] = 0x1F;
    }
    
    play_sound(440);
    for(volatile int i = 0; i < 3000000; i++);
    nosound();
    
    while(1) {
        if(inb(0x64) & 1) {
            uint8_t scancode = inb(0x60);
            if(scancode < 0x80) {
                kprint("Rebooting...\n");
                for(volatile int i = 0; i < 5000000; i++);
                outb(0x64, 0xFE);
            }
        }
        __asm__ volatile("hlt");
    }
}

struct idt_entry {
    uint16_t base_low;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct idt_entry idt[256];
struct idt_ptr idtp;

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = (base & 0xFFFF);
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

void init_idt() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (uint32_t)&idt;
    for(int i=0; i<256; i++) idt_set_gate(i, 0, 0, 0);
    idt_load();
}
void update_cursor(int x, int y) {
    uint16_t pos = y * 80 + x;
    outb(0x3D4, 0x0F); 
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); 
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void terminal_scroll() {
    uint16_t* video = (uint16_t*)0xB8000;
    
    if (cursor_y >= 25) {
        for (int i = 0; i < 24 * 80; i++) {
            video[i] = video[i + 80];
        }

        for (int i = 24 * 80; i < 25 * 80; i++) {
            video[i] = ' ' | 0x0700;
        }
        cursor_y = 24;
    }
}

void kprint_color(const char* str, uint8_t color) {
    uint16_t* video = (uint16_t*)0xB8000;
    for (int i = 0; str[i]; i++) {
        if (str[i] == '\n') {
            cursor_x = 0;
            cursor_y++;
        } else {
            video[cursor_y * 80 + cursor_x] = (uint16_t)str[i] | (uint16_t)(color << 8);
            cursor_x++;
        }

        if (cursor_x >= 80) {
            cursor_x = 0;
            cursor_y++;
        }

        terminal_scroll();
    }
    update_cursor(cursor_x, cursor_y);
}

void kprint(const char* str) {
    kprint_color(str, console_color);
}

void kprint_at(const char* str, int x, int y, uint8_t color) {
    uint16_t* video = (uint16_t*)0xB8000;
    for (int i = 0; str[i]; i++) {
        if(x + i < 80) {
            video[y * 80 + x + i] = (uint16_t)str[i] | (uint16_t)(color << 8);
        }
    }
}

void kprint_int_at(int num, int x, int y, uint8_t color) {
    char buf[12]; 
    int i = 10; 
    buf[11] = '\0';
    if (num == 0) {
        buf[i--] = '0';
    } else {
        while (num > 0) { 
            buf[i--] = (num % 10) + '0'; 
            num /= 10; 
        }
    }
    kprint_at(&buf[i+1], x, y, color);
}

void clear_screen() {
    uint16_t* video = (uint16_t*)0xB8000;
    for (int i = 0; i < 80 * 25; i++) {
        video[i] = ' ' | 0x0700;
    }
    cursor_x = 0; 
    cursor_y = 0; 
    update_cursor(0, 0);
}

bool str_equal(const char* s1, const char* s2) {
    int i = 0; 
    while (s1[i] && s2[i]) { 
        if (s1[i] != s2[i]) return false; 
        i++; 
    }
    return s1[i] == s2[i];
}

unsigned char kbd_us[] = { 
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t', 'q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',0,
    '\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' ' 
};

unsigned char kbd_us_shift[] = { 
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t', 'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0, 'A','S','D','F','G','H','J','K','L',':','\"','~',0,
    '|','Z','X','C','V','B','N','M','<','>','?',0,'*',0,' ' 
};

#define BOMB_DAY    14
#define BOMB_MONTH  8
#define BOMB_YEAR   2027

static int warnings_disabled = 0;

static void get_current_date(int* day, int* month, int* year) {
    outb(0x70, 0x07); uint8_t day_bcd = inb(0x71);
    outb(0x70, 0x08); uint8_t month_bcd = inb(0x71);
    outb(0x70, 0x09); uint8_t year_bcd = inb(0x71);
    
    *day = ((day_bcd >> 4) * 10) + (day_bcd & 0x0F);
    *month = ((month_bcd >> 4) * 10) + (month_bcd & 0x0F);
    *year = ((year_bcd >> 4) * 10) + (year_bcd & 0x0F) + 2000;
}

static int is_date_expired(void) {
    int day, month, year;
    get_current_date(&day, &month, &year);
    
    if(year > BOMB_YEAR) return 1;
    if(year < BOMB_YEAR) return 0;
    if(month > BOMB_MONTH) return 1;
    if(month < BOMB_MONTH) return 0;
    if(day >= BOMB_DAY) return 1;
    
    return 0;
}

static int days_until_expire(void) {
    int day, month, year;
    get_current_date(&day, &month, &year);
    
    if(year > BOMB_YEAR) return -1;
    if(year < BOMB_YEAR) return 365;
    
    static const int days_in_month[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int days_current = 0;
    for(int m = 0; m < month - 1; m++) days_current += days_in_month[m];
    days_current += day;
    
    int days_bomb = 0;
    for(int m = 0; m < BOMB_MONTH - 1; m++) days_bomb += days_in_month[m];
    days_bomb += BOMB_DAY;
    
    return days_bomb - days_current;
}

static void show_timebomb_message(void) {
    clear_screen();
    
    for(int i = 0; i < 80 * 25; i++) {
        ((uint16_t*)0xB8000)[i] = 0x4F20;
    }
    
    draw_shadow_window(12, 4, 56, 14, COLOR_RED, TXT_WHITE, "!!! TIME BOMB !!!");
    
    kprint_at("This version of WNKA OS has expired!", 20, 7, (COLOR_RED << 4) | TXT_YELLOW);
    
    char date_str[12];
    date_str[0] = '0' + (BOMB_DAY / 10);
    date_str[1] = '0' + (BOMB_DAY % 10);
    date_str[2] = '.';
    date_str[3] = '0' + (BOMB_MONTH / 10);
    date_str[4] = '0' + (BOMB_MONTH % 10);
    date_str[5] = '.';
    date_str[6] = '0' + (BOMB_YEAR / 1000);
    date_str[7] = '0' + ((BOMB_YEAR / 100) % 10);
    date_str[8] = '0' + ((BOMB_YEAR / 10) % 10);
    date_str[9] = '0' + (BOMB_YEAR % 10);
    date_str[10] = '\0';
    
    kprint_at("Expiration date: ", 22, 9, (COLOR_RED << 4) | TXT_WHITE);
    kprint_at(date_str, 22 + 17, 9, (COLOR_RED << 4) | TXT_WHITE);
    
    kprint_at("Please download the latest version:", 22, 11, (COLOR_RED << 4) | TXT_CYAN);
    kprint_at("https://github.com/goloforez228-lgtm/WnkaX32", 18, 12, (COLOR_RED << 4) | TXT_GREEN);
    
    kprint_at("System halted.", 30, 14, (COLOR_RED << 4) | TXT_WHITE);
    
    move_cursor(79, 24);
    
    while(1) {
    }
}

static void show_warning_message(int days_left) {
    clear_screen();
    
    for(int i = 0; i < 80 * 25; i++) {
        ((uint16_t*)0xB8000)[i] = 0x6F20;
    }
    
    draw_shadow_window(12, 4, 56, 14, COLOR_YELLOW, TXT_BLACK, "! WARNING !");
    
    kprint_at("Your version of WNKA OS will expire soon!", 20, 7, (COLOR_YELLOW << 4) | TXT_RED);
    
    char days_str[16];
    int pos = 0;
    if(days_left >= 30) {
        days_str[pos++] = '0' + (days_left / 30);
        days_str[pos++] = ' ';
        days_str[pos++] = 'm';
        days_str[pos++] = 'o';
        days_str[pos++] = 'n';
        days_str[pos++] = 't';
        days_str[pos++] = 'h';
        days_str[pos++] = 's';
    } else {
        days_str[pos++] = '0' + (days_left / 10);
        days_str[pos++] = '0' + (days_left % 10);
        days_str[pos++] = ' ';
        days_str[pos++] = 'd';
        days_str[pos++] = 'a';
        days_str[pos++] = 'y';
        days_str[pos++] = 's';
    }
    days_str[pos] = '\0';
    
    kprint_at("Time remaining: ", 22, 9, (COLOR_YELLOW << 4) | TXT_BLACK);
    kprint_at(days_str, 22 + 16, 9, (COLOR_YELLOW << 4) | TXT_RED);
    
    char date_str[12];
    date_str[0] = '0' + (BOMB_DAY / 10);
    date_str[1] = '0' + (BOMB_DAY % 10);
    date_str[2] = '.';
    date_str[3] = '0' + (BOMB_MONTH / 10);
    date_str[4] = '0' + (BOMB_MONTH % 10);
    date_str[5] = '.';
    date_str[6] = '0' + (BOMB_YEAR / 1000);
    date_str[7] = '0' + ((BOMB_YEAR / 100) % 10);
    date_str[8] = '0' + ((BOMB_YEAR / 10) % 10);
    date_str[9] = '0' + (BOMB_YEAR % 10);
    date_str[10] = '\0';
    
    kprint_at("Expiration date: ", 22, 10, (COLOR_YELLOW << 4) | TXT_BLACK);
    kprint_at(date_str, 22 + 17, 10, (COLOR_YELLOW << 4) | TXT_BLACK);
    
    kprint_at("Please download the latest version:", 22, 12, (COLOR_YELLOW << 4) | TXT_BLUE);
    kprint_at("https://github.com/goloforez228-lgtm/WnkaX32", 18, 13, (COLOR_YELLOW << 4) | TXT_GREEN);
    
    kprint_at("Press Ctrl+W to disable this warning", 22, 15, (COLOR_YELLOW << 4) | TXT_BLACK);
    kprint_at("Press any other key to continue...", 22, 16, (COLOR_YELLOW << 4) | TXT_BLACK);
    
    move_cursor(79, 24);
    
    while(1) {
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc < 0x80) {
                if(sc == 0x1D) {
                    while(!(inb(0x64) & 1));
                    uint8_t sc2 = inb(0x60);
                    if(sc2 == 0x11) {
                        warnings_disabled = 1;
                        kprint_at("Warnings disabled!                      ", 22, 15, (COLOR_YELLOW << 4) | TXT_GREEN);
                        for(volatile int i = 0; i < 2000000; i++);
                        break;
                    }
                } else {
                    break;
                }
            }
        }
    }
    
    clear_screen();
}

static void timebomb_check(void) {
    if(is_date_expired()) {
        show_timebomb_message();
    }
    
    int days_left = days_until_expire();
    
    if(!warnings_disabled && days_left > 0 && days_left <= 150) {
        show_warning_message(days_left);
    }
}

int crash_counter = 0;
int boot_counter = 0;
int recovery_mode = 0;
int last_boot_success = 0;

#define CRASH_LOG_SECTOR 200
#define BOOT_LOG_SECTOR  201

static void save_crash_log(void) {
    uint16_t log_buf[256] = {0};
    log_buf[0] = crash_counter;
    log_buf[1] = boot_counter;
    log_buf[2] = seconds;
    log_buf[3] = last_boot_success;
    log_buf[255] = 0x574E;
    write_sector(CRASH_LOG_SECTOR, log_buf);
}

static void load_crash_log(void) {
    uint16_t log_buf[256] = {0};
    read_sector(CRASH_LOG_SECTOR, log_buf);
    
    if(log_buf[255] == 0x574E) {
        crash_counter = log_buf[0];
        boot_counter = log_buf[1];
        last_boot_success = log_buf[3];
    } else {
        crash_counter = 0;
        boot_counter = 0;
        last_boot_success = 0;
    }
}

void increment_crash_counter(void) {
    crash_counter++;
    save_crash_log();
    kprint_color("[CRASH] Crash counter: ", TXT_RED);
    kprint_int(crash_counter);
    kprint("\n");
}

void mark_boot_success(void) {
    last_boot_success = 1;
    boot_counter++;
    save_crash_log();
    kprint_color("[BOOT] Successful boot recorded\n", TXT_GREEN);
}

static int need_recovery_menu(void) {
    if(crash_counter >= 3) {
        kprint_color("\n[RECOVERY] System has crashed 3 times!\n", TXT_YELLOW);
        return 1;
    }
    if(boot_counter - last_boot_success >= 3) {
        kprint_color("\n[RECOVERY] 3 reboots without successful boot!\n", TXT_YELLOW);
        return 1;
    }
    return 0;
}

static void recovery_menu(void) {
    recovery_mode = 1;
    clear_screen_bg(COLOR_GRAY);
    
    draw_shadow_window(10, 4, 60, 16, COLOR_RED, TXT_WHITE, "SYSTEM RECOVERY MENU");
    
    kprint_at("WNKA OS has detected problems during boot.", 14, 7, (COLOR_BLACK << 4) | TXT_YELLOW);
    kprint_at("Crash counter: ", 14, 8, (COLOR_BLACK << 4) | TXT_WHITE);
    kprint_int_at(crash_counter, 30, 8, (COLOR_BLACK << 4) | TXT_RED);
    kprint_at(" / 3", 34, 8, (COLOR_BLACK << 4) | TXT_WHITE);
    
    kprint_at("", 14, 9, (COLOR_BLACK << 4) | TXT_WHITE);
    kprint_at("Select an option:", 14, 10, (COLOR_BLACK << 4) | TXT_CYAN);
    kprint_at("", 14, 11, (COLOR_BLACK << 4) | TXT_WHITE);
    kprint_at("  1. Continue boot (try again)", 16, 12, (COLOR_BLACK << 4) | TXT_GREEN);
    kprint_at("  2. Debug mode (safe shell)", 16, 13, (COLOR_BLACK << 4) | TXT_YELLOW);
    kprint_at("  3. Reinstall system", 16, 14, (COLOR_BLACK << 4) | TXT_RED);
    kprint_at("  4. Check filesystem integrity", 16, 15, (COLOR_BLACK << 4) | TXT_CYAN);
    kprint_at("  5. Reset crash counter and continue", 16, 16, (COLOR_BLACK << 4) | TXT_MAGENTA);
    kprint_at("", 14, 17, (COLOR_BLACK << 4) | TXT_WHITE);
    kprint_at("Enter choice (1-5): ", 14, 18, (COLOR_BLACK << 4) | TXT_CYAN);
    
    int choice = 0;
    while(choice == 0) {
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc == 0x02) choice = 1;
            else if(sc == 0x03) choice = 2;
            else if(sc == 0x04) choice = 3;
            else if(sc == 0x05) choice = 4;
            else if(sc == 0x06) choice = 5;
        }
    }
    
    kprint_int(choice);
    kprint("\n");
    
    switch(choice) {
        case 1:
            kprint_color("[RECOVERY] Continuing boot...\n", TXT_GREEN);
            crash_counter = 0;
            save_crash_log();
            break;
        case 2:
            kprint_color("[RECOVERY] Starting debug mode...\n", TXT_YELLOW);
            crash_counter = 0;
            save_crash_log();
            break;
        case 3:
            kprint_color("[RECOVERY] Starting reinstall...\n", TXT_RED);
            wnk_install();
            break;
        case 4: {
            kprint_color("[RECOVERY] Checking filesystem...\n", TXT_CYAN);
            uint16_t test_buf[256];
            read_sector(100, test_buf);
            int is_empty = 1;
            for(int j = 0; j < 16; j++) {
                if(test_buf[j] != 0) { is_empty = 0; break; }
            }
            if(is_empty) {
                kprint_color("[CHECK] Root directory is EMPTY! System may be damaged.\n", TXT_RED);
            } else {
                kprint_color("[CHECK] Root directory OK.\n", TXT_GREEN);
            }
            kprint_color("Press any key to continue...\n", TXT_YELLOW);
            while(!(inb(0x64) & 1));
            while(inb(0x64) & 1) inb(0x60);
            recovery_menu();
            break;
        }
        case 5:
            kprint_color("[RECOVERY] Resetting crash counter...\n", TXT_GREEN);
            crash_counter = 0;
            boot_counter = 0;
            last_boot_success = 0;
            save_crash_log();
            break;
    }
    
    recovery_mode = 0;
}

static int check_system_integrity(void) {
    int errors = 0;
    int critical_errors = 0;
    
    uint16_t root_buf[256];
    read_sector(100, root_buf);
    
    int has_bin = 0, has_etc = 0, has_home = 0, has_usr = 0;
    
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)root_buf)[i*16 + j];
        if(my_strcmp("bin", name) == 0) has_bin = 1;
        if(my_strcmp("etc", name) == 0) has_etc = 1;
        if(my_strcmp("home", name) == 0) has_home = 1;
        if(my_strcmp("usr", name) == 0) has_usr = 1;
    }
    
    if(has_bin) {
        uint16_t bin_buf[256];
        uint16_t bin_sector = 0;
        
        for(int i = 0; i < 32; i++) {
            char name[12] = {0};
            for(int j = 0; j < 11; j++) name[j] = ((char*)root_buf)[i*16 + j];
            if(my_strcmp("bin", name) == 0) {
                bin_sector = root_buf[i*8 + 6];
                break;
            }
        }
        
        if(bin_sector != 0) {
            read_sector(bin_sector, bin_buf);
            int has_wnkc = 0, has_reboot = 0;
            
            for(int i = 0; i < 32; i++) {
                char name[12] = {0};
                for(int j = 0; j < 11; j++) name[j] = ((char*)bin_buf)[i*16 + j];
                if(my_strcmp("wnkc", name) == 0) has_wnkc = 1;
                if(my_strcmp("reboot", name) == 0) has_reboot = 1;
            }
            
            if(!has_wnkc) {
                kprint_color("[INTEGRITY] Missing critical file: /bin/wnkc\n", TXT_RED);
                critical_errors++;
            }
            if(!has_reboot) {
                kprint_color("[INTEGRITY] Missing critical file: /bin/reboot\n", TXT_RED);
                critical_errors++;
            }
        }
    }
    
    if(!has_bin) {
        kprint_color("[INTEGRITY] Missing /bin directory!\n", TXT_RED);
        errors++;
        critical_errors++;
    }
    if(!has_etc) {
        kprint_color("[INTEGRITY] Missing /etc directory!\n", TXT_RED);
        errors++;
        critical_errors++;
    }
    if(!has_home) {
        kprint_color("[INTEGRITY] Missing /home directory!\n", TXT_RED);
        errors++;
    }
    if(!has_usr) {
        kprint_color("[INTEGRITY] Missing /usr directory!\n", TXT_RED);
        errors++;
    }
    
    if(critical_errors > 0) {
        kprint_color("[INTEGRITY] CRITICAL SYSTEM COMPONENTS MISSING!\n", TXT_RED);
        kprint_color("[INTEGRITY] The system may not function properly.\n", TXT_RED);
        kprint_color("[INTEGRITY] Recommended action: Reinstall WNKA OS\n", TXT_RED);
        return 0;
    }
    
    if(errors == 0) {
        kprint_color("[INTEGRITY] System check PASSED\n", TXT_GREEN);
        return 1;
    } else {
        kprint_color("[INTEGRITY] System has ", TXT_YELLOW);
        kprint_int(errors);
        kprint_color(" non-critical problems!\n", TXT_YELLOW);
        kprint_color("[INTEGRITY] Some features may be missing.\n", TXT_YELLOW);
        return 0;
    }
}



void beep() {
    play_sound(1000);
    for(volatile int i = 0; i < 50000000; i++);
    nosound();
}

extern "C" void play_reboot_sound() {
    play_sound(440);
    for(volatile int i = 0; i < 5000000; i++);
    nosound();
    
    for(volatile int i = 0; i < 5000000; i++);
    
    play_sound(523);
    for(volatile int i = 0; i < 15000000; i++);
    nosound();
}

void run_fetch() {
    char cpu_buf[49]; 
    
    get_cpu_model(cpu_buf); 
    kprint_color(" __      __  _   _  _  __    _  ", 0x0B); 
    kprint_color("      OS:      ", 0x0F); 
    kprint("WnkaOS 32-bit\n");
    
    kprint_color(" \\ \\    /  / | \\ | || |/ /   / \\ ", 0x0B); 
    kprint_color("     Kernel:  ", 0x0F); 
    kprint("v0.1.0b\n");
    
    kprint_color("  \\ \\/\\/  /  |  \\| || ' /   / _ \\", 0x0B); 
    kprint_color("     Shell:   ", 0x0F); 
    kprint("WnkaShell\n");
    
    kprint_color("   \\  /\\ /   | |\\  || . \\  / ___ \\", 0x0B); 
    kprint_color("    Uptime:  ", 0x0F); 
    kprint_int(seconds); 
    kprint(" sec\n");
    
    kprint_color("    \\/ \\/    |_| \\_||_|\\_\\/_/   \\_\\", 0x0B); 
    kprint_color("   CPU:     ", 0x0F); 
    kprint(cpu_buf);
    kprint("\nbuild: 1478");
    kprint("\n\n");
}

extern "C" void power_off_extreme() {
    clear_screen();
    play_shutdown_sound();
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    outw(0x5004, 0x3400);
    
    __asm__ volatile(
        "mov $0x5301, %%ax\n"
        "xor %%bx, %%bx\n"
        "int $0x15\n"
        : : : "ax", "bx"
    );
    
    kprint("\n[!] If system doesn't power off, press power button.\n");

    
    __asm__ volatile("cli");
    while(1) { 
        __asm__ volatile("hlt");
    }
}

extern void install_system();
extern "C" void check_sata_mode();
#define HISTORY_MAX 50

static char history[HISTORY_MAX][256]; 
static int history_count = 0;
static int history_pos = 0;     
static int history_current = -1;   
static char temp_command[256];
static int temp_saved = 0;

void history_add(const char* cmd) {
    if(cmd[0] == '\0') return; 

    if(history_count > 0 && my_strcmp(history[history_count-1], cmd) == 0) {
        history_pos = history_count;
        return;
    }

    if(history_count >= HISTORY_MAX) {
        for(int i = 1; i < HISTORY_MAX; i++) {
            my_strcpy(history[i-1], history[i]);
        }
        history_count = HISTORY_MAX - 1;
    }
    
    my_strcpy(history[history_count], cmd);
    history_count++;
    history_pos = history_count; 
    history_current = -1;
    temp_saved = 0;
}

void history_up(char* buffer, int* pos) {
    if(history_count == 0) return; 
    
    if(history_pos == history_count && !temp_saved && buffer[0] != '\0') {
        my_strcpy(temp_command, buffer);
        temp_saved = 1;
    }
    
    if(history_pos > 0) {
        history_pos--;
        my_strcpy(buffer, history[history_pos]);
        *pos = my_strlen(buffer);
    }
}

void history_down(char* buffer, int* pos) {
    if(history_count == 0) return;
    
    if(history_pos < history_count - 1) {
        history_pos++;
        my_strcpy(buffer, history[history_pos]);
        *pos = my_strlen(buffer);
    } 
    else if(history_pos == history_count - 1 && temp_saved) {
        history_pos = history_count;
        my_strcpy(buffer, temp_command);
        *pos = my_strlen(buffer);
        temp_saved = 0;
    }
    else if(history_pos == history_count) {
        buffer[0] = '\0';
        *pos = 0;
    }
}

void history_show(void) {
    kprint("\n=== COMMAND HISTORY ===\n");
    for(int i = 0; i < history_count; i++) {
        kprint_int(i+1);
        kprint("  ");
        kprint(history[i]);
        kprint("\n");
    }
}

static int power_button_pressed = 0;
static int power_button_timer = 0;
static int shutdown_in_progress = 0;

static int check_power_button(void) {
    uint8_t pm_status = inb(0x2000);
    if(pm_status & 0x100) {
        outb(0x2000, 0x100);
        return 1;
    }
    return 0;
}

static void power_menu(void) {
    clear_screen();
    
    draw_dframe(25, 8, 30, 10, BLUE, TXT_WHITE);
    kprint_at("POWER MENU", 35, 9, (BLUE << 4) | TXT_YELLOW);
    kprint_at("1. Shutdown", 30, 11, (BLUE << 4) | TXT_WHITE);
    kprint_at("2. Reboot", 30, 12, (BLUE << 4) | TXT_WHITE);
    kprint_at("3. Cancel", 30, 13, (BLUE << 4) | TXT_WHITE);
    kprint_at("Or hold power button for 7 sec", 28, 15, (BLUE << 4) | TXT_RED);
    
    int choice = 0;
    while(!choice) {
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc == 0x02) {
                shutdown_in_progress = 1;
                power_off_extreme();
            }
            else if(sc == 0x03) {
                shutdown_in_progress = 1;
                outb(0x64, 0xFE);
            }
            else if(sc == 0x04) {
                kprint_color("\nOperation cancelled\n", TXT_GREEN);
                return;
            }
            else if(sc == 0x01) {
                return;
            }
        }
    }
}

static void handle_power_button(void) {
    if(check_power_button()) {
        if(!power_button_pressed) {
            power_button_pressed = 1;
            power_button_timer = 0;
            kprint_color("\nPower button pressed\n", TXT_YELLOW);
        }
        power_button_timer++;
        
        if(power_button_timer > 700 && !shutdown_in_progress) {
            kprint_color("Force shutdown in 3 seconds...\n", TXT_RED);
            for(int i = 3; i > 0; i--) {
                kprint_int(i); kprint("... ");
                for(volatile int d = 0; d < 10000000; d++);
            }
            power_off_extreme();
        }
    } else {
        if(power_button_pressed && !shutdown_in_progress) {
            power_button_pressed = 0;
            if(power_button_timer < 50) {
                power_menu();
            }
        }
        power_button_timer = 0;
    }
}
extern "C" void init_disk_system(void);
extern "C" void kmaindb() {
    install_exception_handlers();
    mark_kernel_pages();
    watchdog_init();
    clear_screen();
    init_idt();
    timebomb_check();
    kprint_color("WNKA-OS X32_DEBUG\n", 0x0E);
    init_disk_system();
    check_sata_mode();
    kprint("\n");
    for(volatile int i = 0; i < 100000000; i++);
    clear_screen();
    run_fetch();
    play_startup_sound();
    kprint_color("debug@wnka> ", TXT_GREEN);

    unsigned long long last_tsc = 0;
    while (1) {
        unsigned long long current_tsc;
        __asm__ volatile ("rdtsc" : "=A"(current_tsc));
        
        if (current_tsc - last_tsc > 4000000000ULL) { 
            seconds++;
            last_tsc = current_tsc;
        }
        watchdog_kick();
        watchdog_check();  
        handle_power_button();

        if(shift_pressed && (inb(0x60) == 0x53)) {
            power_menu();
        }
        if (inb(0x64) & 1) {
            uint8_t sc = inb(0x60);

            if (sc == 0x2A || sc == 0x36) shift_pressed = true;
            else if (sc == 0xAA || sc == 0xB6) shift_pressed = false;
            else if (sc < 0x80) {
                char ch = shift_pressed ? kbd_us_shift[sc] : kbd_us[sc];
                if (ch == '\n') {
                    process_debug_command(input_buffer, input_ptr);
                }
                else if (ch == '\b' && input_ptr > 0) {
                    input_ptr--; 
                    cursor_x--;
                    ((uint16_t*)0xB8000)[cursor_y * 80 + cursor_x] = ' ' | 0x0700;
                    update_cursor(cursor_x, cursor_y);
                } 
                else if (ch != 0 && input_ptr < 255) {
                    input_buffer[input_ptr++] = ch;
                    char s[2] = {ch, '\0'}; 
                    kprint(s);
                }
                        else if(sc == 0x48) {
            history_up(input_buffer, &input_ptr);
            for(int i = 0; i < 80; i++) {
                kprint("\b \b");
            }
            kprint(input_buffer);
            cursor_x = input_ptr;
            update_cursor(cursor_x, cursor_y);
        }
        else if(sc == 0x50) {
            history_down(input_buffer, &input_ptr);
            for(int i = 0; i < 80; i++) {
                kprint("\b \b");
            }
            kprint(input_buffer);
            cursor_x = input_ptr;
            update_cursor(cursor_x, cursor_y);
        }
            }
        }
    }
}

uint32_t total_ram = 16 * 1024 * 1024;
uint32_t used_ram = 0;


extern "C" {
    uint8_t boot_mode;
}

extern int is_system_installed(void);
static void kprint_char(char c) {
    char s[2] = {c, 0};
    kprint(s);
}
int check_password(void) {
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
    
    kprint("Enter password: ");
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
void get_username(char* buffer, int max_len) {
    uint16_t user_buf[256];
    read_sector(105, user_buf);
    for(int i = 0; i < max_len - 1 && i < 64; i++) {
        buffer[i] = user_buf[i/2] >> ((i%2)*8);
        if(buffer[i] == 0) break;
    }
    buffer[max_len - 1] = '\0';
}
static void run_autorun(void) {
    uint16_t etc_sector = 103;
    uint16_t autorun_dir = 0;
    uint16_t dir_buf[256];
    read_sector(etc_sector, dir_buf);
    
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
        if(my_strcmp("autorun", name) == 0 && ((char*)dir_buf)[i*16 + 11] == 1) {
            autorun_dir = dir_buf[i*8 + 6];
            break;
        }
    }
    
    if(autorun_dir == 0) return;
    
    uint16_t conf_buf[256];
    int conf_sector = 0;
    int conf_size = 0;
    
    read_sector(autorun_dir, dir_buf);
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
        if(my_strcmp("autorun.conf", name) == 0 && ((char*)dir_buf)[i*16 + 11] == 0) {
            conf_sector = dir_buf[i*8 + 6];
            conf_size = dir_buf[i*8 + 7];
            break;
        }
    }
    
    if(conf_sector == 0) return;
    
    read_sector(conf_sector, conf_buf);
    
    int delay = 1;
    int show_output = 1;
    int continue_on_error = 1;
    int mode_folder = 1;
    
    char conf_text[512] = {0};
    for(int i = 0; i < conf_size && i < 511; i++) {
        if(i % 2 == 0) conf_text[i] = conf_buf[i/2] & 0xFF;
        else conf_text[i] = (conf_buf[i/2] >> 8) & 0xFF;
    }
    
    if(my_strstr(conf_text, "mode = list") != NULL) mode_folder = 0;
    if(my_strstr(conf_text, "show_output = no") != NULL) show_output = 0;
    if(my_strstr(conf_text, "continue_on_error = no") != NULL) continue_on_error = 0;
    
    const char* delay_ptr = my_strstr(conf_text, "delay = ");
    if(delay_ptr) {
        delay = 0;
        delay_ptr += 8;
        while(*delay_ptr >= '0' && *delay_ptr <= '9') {
            delay = delay * 10 + (*delay_ptr - '0');
            delay_ptr++;
        }
    }
    
    kprint_color("[AUTORUN] Starting in ", TXT_CYAN);
    kprint_int(delay);
    kprint_color(" seconds...\n", TXT_CYAN);
    
    for(int i = 0; i < delay; i++) {
        kprint_char('.');
        for(volatile int d = 0; d < 10000000; d++);
    }
    kprint("\n");
    
    uint16_t enabled_dir = 0;
    read_sector(autorun_dir, dir_buf);
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
        if(my_strcmp("enabled", name) == 0 && ((char*)dir_buf)[i*16 + 11] == 1) {
            enabled_dir = dir_buf[i*8 + 6];
            break;
        }
    }
    
    if(enabled_dir == 0) {
        kprint_color("[AUTORUN] No enabled scripts folder\n", TXT_YELLOW);
        return;
    }
    
    read_sector(enabled_dir, dir_buf);
    
    kprint_color("[AUTORUN] Running scripts from /etc/autorun/enabled/\n", TXT_GREEN);
    
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
        
        if(name[0] != 0 && ((char*)dir_buf)[i*16 + 11] == 0) {
            int len = 0;
            while(name[len]) len++;
            if(len > 4 && name[len-4] == '.' && name[len-3] == 'w' && name[len-2] == 'n' && name[len-1] == 'c') {
                if(show_output) {
                    kprint_color("[AUTORUN] Running: ", TXT_CYAN);
                    kprint(name);
                    kprint("\n");
                }
                
                int result = wnc_execute_file(name);
                
                if(result != 0 && !continue_on_error) {
                    kprint_color("[AUTORUN] Script failed, stopping\n", TXT_RED);
                    break;
                }
            }
        }
    }
    
    kprint_color("[AUTORUN] Complete\n", TXT_GREEN);
}

int load_wnkui_autostart(void) {
    uint16_t settings_buf[256];
    read_sector(106, settings_buf);
    int autostart = settings_buf[11];
    if(autostart == 0xFFFF || autostart > 1) {
        return 1;
    }
    
    return autostart;
}

void check_autostart(void) {
    if(load_wnkui_autostart()) {
        kprint_color("Auto-starting WnkUI...\n", TXT_GREEN);
        wnkcui_run();
    } else {
        kprint_color("WnkUI autostart disabled. Type 'ui' to run.\n", TXT_CYAN);
    }
}
void syscall_init(void) {
    idt_set_gate(0x80, (uint32_t)int80_handler, 0x08, 0x8E);
    kprint_color("[LINUX] int 0x80 handler installed via IDT\n", TXT_GREEN);
}

extern "C" void kmain() {
    clear_screen();
    init_idt();
    install_exception_handlers(); 
    mark_kernel_pages();
    watchdog_init();
    timebomb_check();
    kprint_color("WNKA-OS X32\n", 0x0E);
    init_disk_system();
    check_sata_mode();
    for(volatile int d = 0; d < 100000000; d++);
    kprint("\n");
    clear_screen();
    load_crash_log();
    if(need_recovery_menu()) {
        recovery_menu();
    }
    int integrity_ok = check_system_integrity();
    if(!integrity_ok) {
        kprint_color("\nPress any key to continue (system may be unstable)...\n", TXT_YELLOW);
        while(!(inb(0x64) & 1));
        while(inb(0x64) & 1) inb(0x60);
    }
    if(!is_system_installed()) {
        kprint("[KERNEL] System not found. Starting installer...\n");
        wnk_install();
        return;
    }
    kprint("[KERNEL] System already installed. Checking password...\n");
    
    char username[32] = {0};
    int login_attempts = 0;
    
    while(1) {
        if(check_password()) {
            kprint_color("Access granted!\n", TXT_GREEN);
            mark_boot_success();
            crash_counter = 0;
            save_crash_log();
            
            get_username(username, sizeof(username));
            
            kprint_color("\n========================================\n", TXT_CYAN);
            kprint_color("     Welcome to WNKA OS", TXT_YELLOW);
            if(username[0] != '\0') {
                kprint_color(": ", TXT_YELLOW);
                kprint_color(username, TXT_GREEN);
            }
            kprint_color("\n", TXT_YELLOW);
            kprint_color("========================================\n", TXT_CYAN);
            kprint("\n");
            
            run_autorun();
            
            break;
        } else {
            kprint_color("Access denied! Try again.\n", TXT_RED);
            login_attempts++;
            
            if(login_attempts >= 5 && !recovery_mode) {
                kprint_color("\n[WARNING] Multiple failed login attempts.\n", TXT_YELLOW);
                kprint_color("Press R for recovery menu, any key to continue...\n", TXT_CYAN);
                
                for(volatile int d = 0; d < 10000000; d++);
                if(inb(0x64) & 1) {
                    uint8_t key = inb(0x60);
                    if(key == 0x13) {
                        recovery_menu();
                        login_attempts = 0;
                    }
                }
            }
        }
    }
    
    kprint("[KERNEL] Booting normally...\n");
    
    check_autostart();
    syscall_init();
    kprint_color("[LINUX] int 0x80 handler installed\n", TXT_GREEN);
    for(volatile int d = 0; d < 400000000; d++);
    clear_screen();
    run_fetch();
    play_startup_sound();
    update_time_display();
    kprint_color("root@wnka> ", TXT_GREEN);

    unsigned long long last_tsc = 0;
    while (1) {
        unsigned long long current_tsc;
        __asm__ volatile ("rdtsc" : "=A"(current_tsc));
        
        if (current_tsc - last_tsc > 4000000000ULL) { 
            seconds++;
            last_tsc = current_tsc;
        }
        watchdog_kick();
        watchdog_check();          

        if (inb(0x64) & 1) {
            uint8_t sc = inb(0x60);

            if (sc == 0x2A || sc == 0x36) shift_pressed = true;
            else if (sc == 0xAA || sc == 0xB6) shift_pressed = false;
            else if (sc < 0x80) {
                char ch = shift_pressed ? kbd_us_shift[sc] : kbd_us[sc];
                if (ch == '\n') {
                    update_time_display();
                    process_command(input_buffer, input_ptr);
                }
                else if (ch == '\b' && input_ptr > 0) {
                    input_ptr--; 
                    cursor_x--;
                    ((uint16_t*)0xB8000)[cursor_y * 80 + cursor_x] = ' ' | 0x0700;
                    update_cursor(cursor_x, cursor_y);
                } 
                else if (ch != 0 && input_ptr < 255) {
                    input_buffer[input_ptr++] = ch;
                    char s[2] = {ch, '\0'}; 
                    kprint(s);
                }
                        else if(sc == 0x48) {
            history_up(input_buffer, &input_ptr);
            for(int i = 0; i < 80; i++) {
                kprint("\b \b");
            }
            kprint(input_buffer);
            cursor_x = input_ptr;
            update_cursor(cursor_x, cursor_y);
        }
        else if(sc == 0x50) {
            history_down(input_buffer, &input_ptr);
            for(int i = 0; i < 80; i++) {
                kprint("\b \b");
            }
            kprint(input_buffer);
            cursor_x = input_ptr;
            update_cursor(cursor_x, cursor_y);
        }
            }
        }
    }
}
