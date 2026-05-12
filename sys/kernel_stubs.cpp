#include "kernel_api.h"
#define NULL 0

kernel_api_t* g_api = NULL;

extern "C" void kprint(const char* str) {
    if (g_api && g_api->kprint) g_api->kprint(str);
}

extern "C" void kprint_int(uint32_t n) {
    if (g_api && g_api->kprint_int) g_api->kprint_int(n);
}

extern "C" void kprint_hex32(uint32_t n) {
    if (g_api && g_api->kprint_hex32) g_api->kprint_hex32(n);
}

extern "C" void kprint_color(const char* str, uint8_t color) {
    if (g_api && g_api->kprint_color) g_api->kprint_color(str, color);
}

extern "C" void kprint_at(const char* str, int x, int y, uint8_t color) {
    if (g_api && g_api->kprint_at) g_api->kprint_at(str, x, y, color);
}

extern "C" void clear_screen(void) {
    if (g_api && g_api->clear_screen) g_api->clear_screen();
}

extern "C" void read_sector(uint32_t lba, uint16_t* buffer) {
    if (g_api && g_api->read_sector) g_api->read_sector(lba, buffer);
}

extern "C" void write_sector(uint32_t lba, uint16_t* buffer) {
    if (g_api && g_api->write_sector) g_api->write_sector(lba, buffer);
}

extern "C" void serial_init(uint16_t port, uint16_t baud) {
    if (g_api && g_api->serial_init) g_api->serial_init(port, baud);
}

extern "C" void serial_write_string(uint16_t port, const char* str) {
    if (g_api && g_api->serial_write_string) g_api->serial_write_string(port, str);
}

extern "C" void play_startup_sound(void) {
    if (g_api && g_api->play_startup_sound) g_api->play_startup_sound();
}

extern "C" void play_shutdown_sound(void) {
    if (g_api && g_api->play_shutdown_sound) g_api->play_shutdown_sound();
}

extern "C" void init_disk_system(void) {
    if (g_api && g_api->init_disk_system) g_api->init_disk_system();
}

extern "C" void check_sata_mode(void) {
    if (g_api && g_api->check_sata_mode) g_api->check_sata_mode();
}

extern "C" void process_command(char* buf, int& ptr) {
    if (g_api && g_api->process_command) g_api->process_command(buf, ptr);
}

extern "C" void process_debug_command(char* buf, int& ptr) {
    if (g_api && g_api->process_debug_command) g_api->process_debug_command(buf, ptr);
}

extern "C" void update_time_display(void) {
    if (g_api && g_api->update_time_display) g_api->update_time_display();
}

extern "C" int is_system_installed(void) {
    if (g_api && g_api->is_system_installed) return g_api->is_system_installed();
    return 0;
}

extern "C" void wnk_install(void) {
    if (g_api && g_api->wnk_install) g_api->wnk_install();
}

extern "C" void wnkcui_run(void) {
    if (g_api && g_api->wnkcui_run) g_api->wnkcui_run();
}

extern "C" int wnc_execute_file(const char* filename) {
    if (g_api && g_api->wnc_execute_file) return g_api->wnc_execute_file(filename);
    return -1;
}