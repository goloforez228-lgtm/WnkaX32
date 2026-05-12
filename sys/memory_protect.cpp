#include "memory_protect.h"
#include "video.h"
#include "graph.h"

static protected_region_t protected_regions[16];
static int region_count = 0;

void protect_region(uint32_t start, uint32_t end, uint32_t flags, const char* name) {
    if(region_count >= 16) return;
    
    protected_regions[region_count].start = start;
    protected_regions[region_count].end = end;
    protected_regions[region_count].flags = flags;
    protected_regions[region_count].name = name;
    region_count++;
    
    kprint("[MEM] Protected: ");
    kprint(name);
    kprint(" (0x");
    kprint_hex32(start);
    kprint("-0x");
    kprint_hex32(end);
    kprint(")\n");
}

int check_memory_access(uint32_t addr, uint32_t size, int write) {
    for(int i = 0; i < region_count; i++) {
        if(addr >= protected_regions[i].start && 
           addr + size <= protected_regions[i].end) {
            
            if(write && !(protected_regions[i].flags & PAGE_WRITE)) {
                kprint_color("[MEM] WRITE PROTECTED: ", TXT_RED);
                kprint(protected_regions[i].name);
                kprint("\n");
                return 0;
            }
            return 1;
        }
    }
    return 1;
}

void mark_kernel_pages(void) {
    protect_region(0x100000, 0x200000, PAGE_READONLY, "Kernel Code");
    protect_region(0x200000, 0x300000, PAGE_READONLY, "Kernel Data");
    protect_region(0xA0000, 0xB0000, PAGE_READONLY, "Video Memory");
    protect_region(0xB8000, 0xC0000, PAGE_READONLY, "Text Video");
    
    kprint("[MEM] Kernel pages protected\n");
}