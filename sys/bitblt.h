#ifndef VGA_BITBLT_H
#define VGA_BITBLT_H

#include <stdint.h>

#define SRCCOPY     0x00CC0020
#define SRCPAINT    0x00EE0086
#define SRCAND      0x008800C6
#define SRCINVERT   0x00660046
#define SRCERASE    0x00440328
#define NOTSRCCOPY  0x00330008
#define NOTSRCERASE 0x001100A6
#define MERGECOPY   0x00C000CA
#define MERGEPAINT  0x00BB0226
#define PATCOPY     0x00F00021
#define PATPAINT    0x00FB0A09
#define PATINVERT   0x005A0049
#define DSTINVERT   0x00550009
#define BLACKNESS   0x00000042
#define WHITENESS   0x00FF0062

void vga_bitblt(int dst_x, int dst_y, int w, int h,
                uint8_t* src, int src_x, int src_y, int src_pitch,
                uint32_t rop);

void vga_bitblt_solid(int x, int y, int w, int h, uint8_t color, uint32_t rop);
void vga_bitblt_copy(int dst_x, int dst_y, int w, int h,
                     uint8_t* src, int src_pitch);

#endif