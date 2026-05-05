#include "video.h"
#include "scheduler.h"
#include "ahci.h"
#include "wnkfs.h"

static char* my_strcpy(char* dest, const char* src) {
    char* d = dest;
    while(*src) {
        *d++ = *src++;
    }
    *d = '\0';
    return dest;
}

static void run_command(const char* cmd) {
    char cmd_copy[256];
    int i = 0;
    while(cmd[i] && i < 255) {
        cmd_copy[i] = cmd[i];
        i++;
    }
    cmd_copy[i] = '\0';
    int dummy_ptr = i;
    process_command(cmd_copy, dummy_ptr);
}

extern void process_command(char* cmd, int& ptr);

static Task tasks[MAX_TASKS];
static int task_count = 0;
static int next_pid = 1000;
static uint32_t system_ticks = 0;

static Time current_time = {0, 0, 0};

static uint8_t read_cmos_safe(uint8_t reg) {
    int timeout = 100000;
    
    __asm__ volatile("cli");
    
    outb(0x70, reg);
    
    while((inb(0x70) & 0x80) && timeout--) {
        for(volatile int i = 0; i < 10; i++);
    }
    
    uint8_t value = 0;
    if(timeout > 0) {
        value = inb(0x71);
    }
    
    __asm__ volatile("sti");
    
    return value;
}

Time get_time(void) {
    Time t = {0, 0, 0}; 
    
    uint8_t hour = read_cmos_safe(0x04);
    uint8_t minute = read_cmos_safe(0x02);
    uint8_t second = read_cmos_safe(0x00);
    

    if(hour <= 0x23 && minute <= 0x59 && second <= 0x59) {
        t.hour = ((hour >> 4) * 10) + (hour & 0x0F);
        t.minute = ((minute >> 4) * 10) + (minute & 0x0F);
        t.second = ((second >> 4) * 10) + (second & 0x0F);
    }
    
    return t;
}

void get_date(uint8_t* day, uint8_t* month, uint16_t* year) {
    uint8_t d = read_cmos_safe(0x07);
    uint8_t m = read_cmos_safe(0x08);
    uint8_t y = read_cmos_safe(0x09);
    uint8_t c = read_cmos_safe(0x32);
    
    *day = ((d >> 4) * 10) + (d & 0x0F);
    *month = ((m >> 4) * 10) + (m & 0x0F);
    *year = (c * 100) + ((y >> 4) * 10) + (y & 0x0F);
}

void set_time(Time t) {
    current_time = t;
}

uint32_t time_to_seconds(Time t) {
    return (uint32_t)t.hour * 3600 + (uint32_t)t.minute * 60 + (uint32_t)t.second;
}

Time seconds_to_time(uint32_t seconds) {
    Time t;
    t.hour = (seconds / 3600) % 24;
    seconds %= 3600;
    t.minute = seconds / 60;
    t.second = seconds % 60;
    return t;
}

void print_time(Time t) {
    if(t.hour < 10) kprint("0");
    kprint_int(t.hour);
    kprint(":");
    if(t.minute < 10) kprint("0");
    kprint_int(t.minute);
}

void print_full_time(Time t) {
    if(t.hour < 10) kprint("0");
    kprint_int(t.hour);
    kprint(":");
    if(t.minute < 10) kprint("0");
    kprint_int(t.minute);
    kprint(":");
    if(t.second < 10) kprint("0");
    kprint_int(t.second);
}