#ifndef THEMES_H
#define THEMES_H

#include <stdint.h>

#define THEME_COUNT 10

void theme_command(const char* arg);
void set_theme(int id);
uint8_t get_terminal_color(void);
uint8_t get_highlight_color(void);
uint8_t get_error_color(void);
uint8_t get_success_color(void);

#endif