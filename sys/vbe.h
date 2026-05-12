#ifndef VBE_H
#define VBE_H

#include <stdint.h>

#define VBE_CONTROLLER_INFO    0x4F00
#define VBE_MODE_INFO          0x4F01
#define VBE_SET_MODE           0x4F02
#define VBE_CURRENT_MODE       0x4F03
#define VBE_SAVE_RESTORE       0x4F04
#define VBE_DISPLAY_WINDOW     0x4F05
#define VBE_SCAN_LINE_LENGTH   0x4F06
#define VBE_DISPLAY_START      0x4F07
#define VBE_DAC_PALETTE        0x4F08
#define VBE_SET_PALETTE        0x4F09
#define VBE_PROTECTED_MODE     0x4F0A
#define VBE_PIXEL_CLOCK        0x4F0B
#define VBE_VERSION_MAJOR 2
#define VBE_VERSION_MINOR 0

#define VBE_MODE_LINEAR        0x80
#define VBE_MEMORY_PACKED      0x04
#define VBE_MEMORY_DIRECT      0x06

#define VBE_SUCCESS            0x4F
#define VBE_FAILED             0x14F
#define VBE_NOT_SUPPORTED      0x24F
#define VBE_INVALID            0x34F

typedef struct {
    char     signature[4];
    uint16_t version;
    uint32_t oem_string;
    uint32_t capabilities;
    uint32_t video_modes;
    uint16_t total_memory;
    uint16_t oem_software_rev;
    uint32_t oem_vendor;
    uint32_t oem_product;
    uint32_t oem_product_rev;
    char     reserved[222];
    char     oem_data[256];
} __attribute__((packed)) vbe_info_t;

typedef struct {
    uint16_t mode_attributes;
    uint8_t  win_a_attr;
    uint8_t  win_b_attr;
    uint16_t win_granularity;
    uint16_t win_size;
    uint16_t win_a_segment;
    uint16_t win_b_segment;
    uint32_t win_func_ptr;
    uint16_t bytes_per_scanline;
    uint16_t x_resolution;
    uint16_t y_resolution;
    uint8_t  x_char_size;
    uint8_t  y_char_size;
    uint8_t  number_of_planes;
    uint8_t  bits_per_pixel;
    uint8_t  number_of_banks;
    uint8_t  memory_model;
    uint8_t  bank_size;
    uint8_t  number_of_image_pages;
    uint8_t  reserved1;
    uint8_t  red_mask_size;
    uint8_t  red_field_position;
    uint8_t  green_mask_size;
    uint8_t  green_field_position;
    uint8_t  blue_mask_size;
    uint8_t  blue_field_position;
    uint8_t  reserved_mask_size;
    uint8_t  reserved_field_position;
    uint8_t  direct_color_mode;
    uint32_t phys_base_ptr;
    uint32_t reserved2;
    uint16_t reserved3;
    uint16_t lin_bytes_per_scanline;
    uint8_t  bnk_image_pages;
    uint8_t  lin_image_pages;
    uint8_t  lin_red_mask_size;
    uint8_t  lin_red_field_position;
    uint8_t  lin_green_mask_size;
    uint8_t  lin_green_field_position;
    uint8_t  lin_blue_mask_size;
    uint8_t  lin_blue_field_position;
    uint8_t  lin_reserved_mask_size;
    uint8_t  lin_reserved_field_position;
    uint32_t max_pixel_clock;
    char     reserved4[189];
} __attribute__((packed)) vbe_mode_info_t;

typedef struct {
    uint16_t mode;
    uint16_t width;
    uint16_t height;
    uint8_t  bpp;
    uint8_t  memory_model;
    uint32_t framebuffer;
    uint32_t pitch;
    uint16_t red_mask;
    uint16_t green_mask;
    uint16_t blue_mask;
} vesa_mode_t;

void vbe_init(void);
int  vbe_get_controller_info(vbe_info_t* info);
int  vbe_get_mode_info(uint16_t mode, vbe_mode_info_t* info);
int  vbe_set_mode(uint16_t mode);
int  vbe_get_current_mode(uint16_t* mode);
int  vbe_set_palette(uint8_t first, uint8_t count, uint8_t* data);
int  vbe_get_supported_modes(vesa_mode_t* modes, int max_modes);
void vbe_enable_auto(void);
void vbe_draw_hologram(void);

#endif