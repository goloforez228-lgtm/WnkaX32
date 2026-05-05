#ifndef MEMORY_PROTECT_H
#define MEMORY_PROTECT_H

#include <stdint.h>

#define PAGE_PRESENT   0x01
#define PAGE_WRITE     0x02
#define PAGE_USER      0x04
#define PAGE_READONLY  0x00

typedef struct {
    uint32_t start;
    uint32_t end;
    uint32_t flags;
    const char* name;
} protected_region_t;

void protect_region(uint32_t start, uint32_t end, uint32_t flags, const char* name);
int check_memory_access(uint32_t addr, uint32_t size, int write);
void mark_kernel_pages(void);
void protect_kernel_heap(void);

#endif