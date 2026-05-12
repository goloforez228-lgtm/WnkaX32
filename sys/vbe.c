#include "vbe.h"
#include "vesa.h"
#include "video.h"

static vbe_info_t vbe_info;
static vbe_mode_info_t vbe_current_mode;

void vbe_enable_auto(void)
{
    kprint("[VBE] Auto-enabling VBE ");
    kprint_int(VBE_VERSION_MAJOR);
    kprint(".");
    kprint_int(VBE_VERSION_MINOR);
    kprint("...\n");
    
    vbe_init();
    
    vesa_mode_t modes[16];
    int count = vbe_get_supported_modes(modes, 16);
    
    if (count > 0)
    {
        int best = count - 1;
        kprint("[VBE] Best mode: ");
        kprint_int(modes[best].width);
        kprint("x");
        kprint_int(modes[best].height);
        kprint("x");
        kprint_int(modes[best].bpp);
        kprint("bpp\n");
        
        vbe_set_mode(modes[best].mode);
        vesa_ok = 1;
        
        vesa_fb = (uint16_t*)modes[best].framebuffer;
        vesa_w = modes[best].width;
        vesa_h = modes[best].height;
        vesa_pitch = modes[best].pitch;
        vesa_bpp = modes[best].bpp;
        
        kprint("[VBE] Ready! ");
        kprint_int(vesa_w);
        kprint("x");
        kprint_int(vesa_h);
        kprint("@");
        kprint_int(vesa_bpp);
        kprint("bpp\n");
    }
}

void vbe_draw_hologram(void)
{
    if (!vesa_ok || !vesa_fb) return;
    
    for (int y = 0; y < vesa_h; y++)
    {
        for (int x = 0; x < vesa_w; x++)
        {
            int cx = x - vesa_w / 2;
            int cy = y - vesa_h / 2;
            int dist = cx * cx + cy * cy;
            
            if (dist < 10000)
            {
                uint8_t r = (x * 4) & 0xFF;
                uint8_t g = (y * 4) & 0xFF;
                uint8_t b = ((x + y) * 2) & 0xFF;
                uint16_t color = vesa_rgb565(r, g, b);
                vesa_fb[y * (vesa_pitch / 2) + x] = color;
            }
            else if (dist < 40000)
            {
                uint8_t r = (y * 2) & 0xFF;
                uint8_t g = (x * 2) & 0xFF;
                uint8_t b = 128;
                uint16_t color = vesa_rgb565(r, g, b);
                vesa_fb[y * (vesa_pitch / 2) + x] = color;
            }
        }
    }
    
    kprint("[VBE] Hologram drawn!\n");
}

void vbe_init(void)
{
    kprint("[VBE] Initializing VBE ");
    kprint_int(VBE_VERSION_MAJOR);
    kprint(".");
    kprint_int(VBE_VERSION_MINOR);
    kprint(" miniport...\n");
    
    vbe_info_t info;
    if (vbe_get_controller_info(&info) == 0)
    {
        kprint("[VBE] VBE Controller found!\n");
        kprint("  Signature: ");
        for (int i = 0; i < 4; i++) {
            char s[2] = {info.signature[i], 0};
            kprint(s);
        }
        kprint("\n");
        kprint("  Memory: ");
        kprint_int(info.total_memory * 64);
        kprint(" KB\n");
    }
}

int vbe_get_controller_info(vbe_info_t* info)
{
    outw(0x1CE, 0x00);
    uint16_t id = inw(0x1CF);
    
    if (id < 0xB0C0 || id > 0xB0C5)
    {
        kprint("[VBE] No BGA/VBE device\n");
        return -1;
    }
    
    info->signature[0] = 'V';
    info->signature[1] = 'E';
    info->signature[2] = 'S';
    info->signature[3] = 'A';
    info->version = VBE_VERSION_MAJOR * 0x100 + VBE_VERSION_MINOR;
    info->total_memory = 16;
    
    return 0;
}

int vbe_get_mode_info(uint16_t mode, vbe_mode_info_t* info)
{
    for (int i = 0; i < sizeof(vbe_mode_info_t); i++)
    {
        ((uint8_t*)info)[i] = 0;
    }
    
    switch (mode)
    {
        case 0x4101: 
            info->x_resolution = 640;
            info->y_resolution = 480;
            info->bits_per_pixel = 8;
            info->memory_model = VBE_MEMORY_PACKED;
            info->mode_attributes = VBE_MODE_LINEAR;
            info->phys_base_ptr = 0xE0000000;
            info->bytes_per_scanline = 640;
            info->lin_bytes_per_scanline = 640;
            break;
            
        case 0x4105:
            info->x_resolution = 1024;
            info->y_resolution = 768;
            info->bits_per_pixel = 16;
            info->memory_model = VBE_MEMORY_DIRECT;
            info->mode_attributes = VBE_MODE_LINEAR;
            info->phys_base_ptr = 0xE0000000;
            info->bytes_per_scanline = 2048;
            info->lin_bytes_per_scanline = 2048;
            info->red_mask_size = 5;
            info->green_mask_size = 6;
            info->blue_mask_size = 5;
            info->red_field_position = 11;
            info->green_field_position = 5;
            info->blue_field_position = 0;
            break;
            
        default:
            return -1;
    }
    
    return 0;
}

int vbe_set_mode(uint16_t mode)
{
    if (vbe_get_mode_info(mode, &vbe_current_mode) != 0)
    {
        return -1;
    }
    
    outw(0x1CE, 0x04);
    outw(0x1CF, 0x00);
    for(volatile int i = 0; i < 100; i++);
    
    outw(0x1CE, 0x01);
    outw(0x1CF, vbe_current_mode.x_resolution);
    for(volatile int i = 0; i < 100; i++);
    
    outw(0x1CE, 0x02);
    outw(0x1CF, vbe_current_mode.y_resolution);
    for(volatile int i = 0; i < 100; i++);
    
    outw(0x1CE, 0x03);
    outw(0x1CF, vbe_current_mode.bits_per_pixel);
    for(volatile int i = 0; i < 100; i++);
    
    outw(0x1CE, 0x04);
    outw(0x1CF, 0x41);
    for(volatile int i = 0; i < 500000; i++);
    
    vesa_w = vbe_current_mode.x_resolution;
    vesa_h = vbe_current_mode.y_resolution;
    vesa_bpp = vbe_current_mode.bits_per_pixel;
    vesa_pitch = vbe_current_mode.lin_bytes_per_scanline;
    vesa_ok = 1;
    
    vesa_fb = (uint16_t*)vbe_current_mode.phys_base_ptr;
    
    for (int i = 0; i < vesa_w * vesa_h; i++)
    {
        vesa_fb[i] = 0x0000;
    }
    
    kprint("[VBE] Mode: ");
    kprint_int(vesa_w);
    kprint("x");
    kprint_int(vesa_h);
    kprint("\n");
    kprint("[VBE] Press any key to return to text mode...\n");
    
    while (!(inb(0x64) & 1));
    uint8_t key = inb(0x60);
    (void)key;
    while (inb(0x64) & 1) inb(0x60);
    
    outw(0x1CE, 0x04);
    outw(0x1CF, 0x00);
    for(volatile int i = 0; i < 100000; i++);
    
    vesa_ok = 0;
    clear_screen();
    kprint("[VBE] Text mode restored\n");
    
    return 0;
}

int vbe_get_current_mode(uint16_t* mode)
{
    outw(0x1CE, 0x01);
    uint16_t w = inw(0x1CF);
    outw(0x1CE, 0x02);
    uint16_t h = inw(0x1CF);
    outw(0x1CE, 0x03);
    uint16_t bpp = inw(0x1CF);
    
    if (w == 640 && h == 480 && bpp == 8) *mode = 0x4101;
    else if (w == 1024 && h == 768 && bpp == 16) *mode = 0x4105;
    else *mode = 0x4105;
    
    return 0;
}

int vbe_set_palette(uint8_t first, uint8_t count, uint8_t* data)
{
    outb(0x03C8, first);
    for (int i = 0; i < count * 3; i += 3)
    {
        outb(0x03C9, data[i]);
        outb(0x03C9, data[i + 1]);
        outb(0x03C9, data[i + 2]);
    }
    return 0;
}

int vbe_get_supported_modes(vesa_mode_t* modes, int max_modes)
{
    vesa_mode_t supported[] = {
        {0x4101, 640,  480, 8,  VBE_MEMORY_PACKED, 0xE0000000, 640,  0, 0, 0},
        {0x4103, 800,  600, 8,  VBE_MEMORY_PACKED, 0xE0000000, 800,  0, 0, 0},
        {0x4105, 1024, 768, 16, VBE_MEMORY_DIRECT, 0xE0000000, 2048, 0xF800, 0x07E0, 0x001F},
        {0x4107, 1280, 1024, 16, VBE_MEMORY_DIRECT, 0xE0000000, 2560, 0xF800, 0x07E0, 0x001F},
        {0x4108, 1600, 1200, 16, VBE_MEMORY_DIRECT, 0xE0000000, 3200, 0xF800, 0x07E0, 0x001F},
    };
    
    int count = sizeof(supported) / sizeof(supported[0]);
    if (count > max_modes) count = max_modes;
    
    for (int i = 0; i < count; i++)
    {
        modes[i] = supported[i];
    }
    
    return count;
}