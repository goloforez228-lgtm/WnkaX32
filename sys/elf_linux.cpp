#include "elf_linux.h"
#include "syscall_linux.h"
#include "video.h"
#include "kernel_stubs.h"
#include "vfs_linux.h"
#include "graph.h"
#include <stdint.h>

#define ELF_MAGIC    0x464C457F
#define ET_EXEC      2
#define ET_DYN       3
#define EM_386       3
#define PT_LOAD      1
#define PT_DYNAMIC   2
#define PT_INTERP    3
#define PT_GNU_STACK 0x6474E551

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

static int elf_initialized = 0;
static uint32_t current_elf_entry = 0;

void elf_init(void) {
    if(elf_initialized) return;
    
    linux_syscall_init();
    
    elf_initialized = 1;
    kprint_color("[ELF] Linux ELF loader initialized\n", TXT_GREEN);
}

int elf_load(const char* path, uint32_t* entry_point, uint32_t* brk_addr) {
    kprint("[ELF] Loading ");
    kprint(path);
    kprint("\n");
    
    uint8_t* file_data = 0;
    uint32_t file_size = 0;
    
    int fd = vfs_open(path, 0, 0);
    if(fd < 0) {
        kprint_color("[ELF] File not found: ", TXT_RED);
        kprint(path);
        kprint("\n");
        return -1;
    }
    
    struct vfs_stat_t st;
    if(vfs_fstat(fd, &st) < 0) {
        vfs_close(fd);
        return -1;
    }
    
    file_size = st.st_size;
    file_data = (uint8_t*)0x28000000;
    vfs_read(fd, file_data, file_size);
    vfs_close(fd);
    
    if(*(uint32_t*)file_data != ELF_MAGIC) {
        kprint_color("[ELF] Not a valid ELF file\n", TXT_RED);
        return -1;
    }
    
    Elf32_Ehdr* ehdr = (Elf32_Ehdr*)file_data;
    
    kprint("[ELF] Type: ");
    if(ehdr->e_type == ET_EXEC) kprint("EXECUTABLE\n");
    else if(ehdr->e_type == ET_DYN) kprint("SHARED OBJECT\n");
    else kprint("UNKNOWN\n");
    
    kprint("[ELF] Entry: 0x");
    kprint_hex32(ehdr->e_entry);
    kprint("\n");
    
    Elf32_Phdr* phdr = (Elf32_Phdr*)(file_data + ehdr->e_phoff);
    uint32_t max_addr = 0;
    
    for(int i = 0; i < ehdr->e_phnum; i++) {
        if(phdr[i].p_type == PT_LOAD) {
            uint8_t* dest = (uint8_t*)phdr[i].p_vaddr;
            const uint8_t* src = file_data + phdr[i].p_offset;
            
            for(uint32_t j = 0; j < phdr[i].p_filesz; j++) {
                dest[j] = src[j];
            }
            
            for(uint32_t j = phdr[i].p_filesz; j < phdr[i].p_memsz; j++) {
                dest[j] = 0;
            }
            
            uint32_t end_addr = phdr[i].p_vaddr + phdr[i].p_memsz;
            if(end_addr > max_addr) max_addr = end_addr;
            
            kprint("[ELF] Loaded 0x");
            kprint_hex32(phdr[i].p_vaddr);
            kprint(" - 0x");
            kprint_hex32(end_addr);
            kprint("\n");
        }
    }
    
    *entry_point = ehdr->e_entry;
    *brk_addr = max_addr;
    
    current_elf_entry = ehdr->e_entry;
    
    kprint_color("[ELF] Successfully loaded\n", TXT_GREEN);
    return 0;
}

int elf_execve(const char* path, char* const argv[], char* const envp[]) {
    (void)argv;
    (void)envp;
    
    uint32_t entry, brk;
    if(elf_load(path, &entry, &brk) != 0) {
        return -1;
    }
    
    kprint("[ELF] Starting execution at 0x");
    kprint_hex32(entry);
    kprint("\n");
    
    void (*entry_func)() = (void(*)())entry;
    entry_func();
    
    return 0;
}
int elf_load_from_memory(const uint8_t* data, uint32_t size, uint32_t* entry_point, uint32_t* brk_addr) {
    kprint("[ELF] Loading from memory (");
    kprint_int(size);
    kprint(" bytes)\n");
    
    if(*(uint32_t*)data != ELF_MAGIC) {
        kprint_color("[ELF] Not a valid ELF\n", TXT_RED);
        return -1;
    }
    
    Elf32_Ehdr* ehdr = (Elf32_Ehdr*)data;
    
    kprint("[ELF] Entry: 0x");
    kprint_hex32(ehdr->e_entry);
    kprint("\n");
    
    Elf32_Phdr* phdr = (Elf32_Phdr*)(data + ehdr->e_phoff);
    uint32_t max_addr = 0;
    
    for(int i = 0; i < ehdr->e_phnum; i++) {
        if(phdr[i].p_type == PT_LOAD) {
            uint32_t copy_size = phdr[i].p_filesz;
            if(phdr[i].p_offset + copy_size > size) {
                copy_size = size - phdr[i].p_offset;
            }
            
            uint32_t dest_addr = phdr[i].p_vaddr;
            if(dest_addr < 0x200000 || dest_addr > 0x30000000) {
                continue;
            }
            
            uint8_t* dest = (uint8_t*)dest_addr;
            const uint8_t* src = data + phdr[i].p_offset;
            
            for(uint32_t j = 0; j < copy_size; j++) dest[j] = src[j];
            for(uint32_t j = copy_size; j < phdr[i].p_memsz; j++) dest[j] = 0;
            
            uint32_t end_addr = dest_addr + phdr[i].p_memsz;
            if(end_addr > max_addr) max_addr = end_addr;
        }
    }
    
    *entry_point = ehdr->e_entry;
    *brk_addr = max_addr;
    
    kprint_color("[ELF] Loaded\n", TXT_GREEN);
    return 0;
}