#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

#define PS2_DATA     0x60
#define PS2_STATUS   0x64

extern "C" {
    extern int mouse_x;
    extern int mouse_y;
    extern int old_x;
    extern int old_y;
    extern uint8_t mouse_btn;
    extern uint16_t saved_char;
    extern int mouse_present;
}

void init_mouse(void);
void poll_mouse(void);
void draw_mouse(void);
void disable_mouse(void);
int mouse_over(int x, int y, int w, int h);

#endif