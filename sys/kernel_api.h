#ifndef KERNEL_API_H
#define KERNEL_API_H

#include <stdint.h>

typedef struct {
    void (*kprint)(const char* str);
    void (*kprint_int)(uint32_t n);
    void (*kprint_hex32)(uint32_t n);
    void (*kprint_color)(const char* str, uint8_t color);
    void (*kprint_at)(const char* str, int x, int y, uint8_t color);
    void (*clear_screen)(void);
    void (*read_sector)(uint32_t lba, uint16_t* buffer);
    void (*write_sector)(uint32_t lba, uint16_t* buffer);
    unsigned int (*strlen)(const char* s);
    int  (*strcmp)(const char* a, const char* b);
    char* (*strcpy)(char* d, const char* s);
    int  (*atoi)(const char* s);
    uint8_t (*inb)(uint16_t port);
    void    (*outb)(uint16_t port, uint8_t val);
    void (*serial_init)(uint16_t port, uint16_t baud);
    void (*serial_write_string)(uint16_t port, const char* str);
    void (*play_startup_sound)(void);
    void (*play_shutdown_sound)(void);
    void (*init_disk_system)(void);
    void (*check_sata_mode)(void);
    void (*process_command)(char* buf, int& ptr);
    void (*process_debug_command)(char* buf, int& ptr);
    void (*update_time_display)(void);
    int  (*is_system_installed)(void);
    void (*wnk_install)(void);
    void (*wnkcui_run)(void);
    int  (*wnc_execute_file)(const char* filename);
    void (*int80_handler)(void);
    
} kernel_api_t;

typedef void (*module_init_t)(kernel_api_t* api);

extern kernel_api_t* g_api;

#endif