#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

    extern int mouse_x;
    extern int mouse_y;
    extern uint8_t mouse_btn;
    extern int mouse_present;

    void init_mouse(void);
    void poll_mouse(void);
    void disable_mouse(void);
    
    int get_mouse_x(void);
    int get_mouse_y(void);
    uint8_t get_mouse_btn(void);
    void set_mouse_speed(int speed);
    int get_mouse_speed(void);
    void draw_mouse(void);
    int mouse_over(int x, int y, int w, int h);

#ifdef __cplusplus
}
#endif

#endif