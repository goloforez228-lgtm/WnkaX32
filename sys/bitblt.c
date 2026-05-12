#include "bitblt.h"
#include "vga.h"
#include "video.h"

extern uint8_t vga_backbuffer[];
extern int vga_graphic;

static uint8_t vga_get_pixel(int x, int y) {
    if (!vga_graphic) return 0;
    if (x < 0 || x >= 320 || y < 0 || y >= 200) return 0;
    return vga_backbuffer[y * 320 + x];
}

void vga_bitblt_solid(int x, int y, int w, int h, uint8_t color, uint32_t rop)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 320) w = 320 - x;
    if (y + h > 200) h = 200 - y;
    if (w <= 0 || h <= 0) return;
    
    switch (rop & 0xFF)
    {
        case 0x42:
            for (int dy = 0; dy < h; dy++)
                for (int dx = 0; dx < w; dx++)
                    vga_buf_pixel(x + dx, y + dy, 0);
            break;
            
        case 0x62:
            for (int dy = 0; dy < h; dy++)
                for (int dx = 0; dx < w; dx++)
                    vga_buf_pixel(x + dx, y + dy, 15);
            break;
            
        case 0x21:
            for (int dy = 0; dy < h; dy++)
                for (int dx = 0; dx < w; dx++)
                    vga_buf_pixel(x + dx, y + dy, color);
            break;
            
        case 0x09:
            for (int dy = 0; dy < h; dy++)
                for (int dx = 0; dx < w; dx++) {
                    uint8_t old = vga_get_pixel(x + dx, y + dy);
                    vga_buf_pixel(x + dx, y + dy, old ^ color);
                }
            break;
            
        default:
            for (int dy = 0; dy < h; dy++)
                for (int dx = 0; dx < w; dx++)
                    vga_buf_pixel(x + dx, y + dy, color);
            break;
    }
}

void vga_bitblt_copy(int dst_x, int dst_y, int w, int h,
                     uint8_t* src, int src_pitch)
{
    for (int dy = 0; dy < h; dy++)
    {
        uint8_t* src_line = src + dy * src_pitch;
        for (int dx = 0; dx < w; dx++)
        {
            vga_buf_pixel(dst_x + dx, dst_y + dy, src_line[dx]);
        }
    }
}

void vga_bitblt(int dst_x, int dst_y, int w, int h,
                uint8_t* src, int src_x, int src_y, int src_pitch,
                uint32_t rop)
{
    int src_offset = src_y * src_pitch + src_x;
    
    switch (rop)
    {
        case SRCCOPY:
            vga_bitblt_copy(dst_x, dst_y, w, h, src + src_offset, src_pitch);
            break;
            
        case BLACKNESS:
            vga_bitblt_solid(dst_x, dst_y, w, h, 0, rop);
            break;
            
        case WHITENESS:
            vga_bitblt_solid(dst_x, dst_y, w, h, 15, rop);
            break;
            
        case DSTINVERT:
            for (int dy = 0; dy < h; dy++)
                for (int dx = 0; dx < w; dx++) {
                    uint8_t old = vga_get_pixel(dst_x + dx, dst_y + dy);
                    vga_buf_pixel(dst_x + dx, dst_y + dy, ~old);
                }
            break;
            
        default:
            vga_bitblt_copy(dst_x, dst_y, w, h, src + src_offset, src_pitch);
            break;
    }
}