#ifndef ELF_LINUX_H
#define ELF_LINUX_H

#include <stdint.h>

void elf_init(void);
int  elf_load(const char* path, uint32_t* entry_point, uint32_t* brk_addr);
int  elf_load_from_memory(const uint8_t* data, uint32_t size, uint32_t* entry_point, uint32_t* brk_addr);
int  elf_execve(const char* path, char* const argv[], char* const envp[]);

#endif