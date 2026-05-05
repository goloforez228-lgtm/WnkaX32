#ifndef VGA256_H
#define VGA256_H

#include <stdint.h>

#define VGA_WIDTH        320
#define VGA_HEIGHT       200
#define VGA_BLACK        0
#define VGA_BLUE         1
#define VGA_GREEN        2
#define VGA_CYAN         3
#define VGA_RED          4
#define VGA_MAGENTA      5
#define VGA_BROWN        6
#define VGA_LIGHT_GRAY   7
#define VGA_DARK_GRAY    8
#define VGA_LIGHT_BLUE   9
#define VGA_LIGHT_GREEN  10
#define VGA_LIGHT_CYAN   11
#define VGA_LIGHT_RED    12
#define VGA_LIGHT_MAGENTA 13
#define VGA_YELLOW       14
#define VGA_WHITE        15

int vga_init(void);
void vga_exit(void);
void vga_pixel(int x, int y, uint8_t color);
void vga_clear(uint8_t color);
void vga_flip(void);
void vga_wait_key(void);
void vga_set_palette(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
void vga_set_palette_rgb(uint8_t index, uint32_t rgb);
void vga_blue_screen(void);
void vga_demo_color_bars(void);
void vga_demo_plasma(void);
void vga_demo_mandelbrot(void);

#endif