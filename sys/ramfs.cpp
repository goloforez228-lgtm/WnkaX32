#include "ramfs.h"
#include "video.h"
#include "kernel_stubs.h"
#include <stdint.h>
#include <stddef.h>

ramfs_super_t ramfs_super;
uint8_t* ramfs_memory = NULL;
int ramfs_mounted = 0;

static uint8_t ramfs_buffer[16 * 1024 * 1024];

static int my_strlen(const char* s) {
    int len = 0;
    while(s[len]) len++;
    return len;
}

static int my_strcmp(const char* s1, const char* s2) {
    while(*s1 && *s2 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

static void my_strcpy(char* dest, const char* src) {
    while(*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

static void my_memset(void* ptr, int value, int num) {
    unsigned char* p = (unsigned char*)ptr;
    for(int i = 0; i < num; i++) {
        p[i] = (unsigned char)value;
    }
}

static void my_memcpy(void* dest, const void* src, int num) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    for(int i = 0; i < num; i++) {
        d[i] = s[i];
    }
}

static ramfs_inode_t* ramfs_get_inode(uint32_t inode_num) {
    if(inode_num >= ramfs_super.total_inodes) return NULL;
    
    uint32_t offset = sizeof(ramfs_super_t) + inode_num * sizeof(ramfs_inode_t);
    return (ramfs_inode_t*)(ramfs_memory + offset);
}
static void ramfs_free_inode(uint32_t inode_num) {
    ramfs_inode_t* inode = ramfs_get_inode(inode_num);
    if(inode) {
        inode->refcount = 0;
        ramfs_super.free_inodes++;
    }
}
static uint32_t ramfs_alloc_inode(void) {
    if(ramfs_super.free_inodes == 0) return -1;
    
    for(uint32_t i = 1; i < ramfs_super.total_inodes; i++) {
        ramfs_inode_t* inode = ramfs_get_inode(i);
        if(inode->refcount == 0) {
            my_memset(inode, 0, sizeof(ramfs_inode_t));
            inode->inode = i;
            inode->refcount = 1;
            ramfs_super.free_inodes--;
            return i;
        }
    }
    
    return -1;
}

static uint32_t ramfs_alloc_block(void) {
    if(ramfs_super.free_blocks == 0) return -1;
    
    uint32_t inode_table_size = ramfs_super.total_inodes * sizeof(ramfs_inode_t);
    uint32_t data_start = sizeof(ramfs_super_t) + inode_table_size;
    uint32_t data_end = ramfs_super.total_blocks * ramfs_super.block_size;
    
    for(uint32_t i = 0; i < ramfs_super.total_blocks; i++) {
        uint32_t block_addr = data_start + i * ramfs_super.block_size;
        if(block_addr + ramfs_super.block_size > data_end) break;
        
        int used = 0;
        for(uint32_t j = 0; j < ramfs_super.total_inodes; j++) {
            ramfs_inode_t* inode = ramfs_get_inode(j);
            if(inode->refcount) {
                for(int k = 0; k < 16; k++) {
                    if(inode->block_ptrs[k] == i) {
                        used = 1;
                        break;
                    }
                }
            }
            if(used) break;
        }
        
        if(!used) {
            ramfs_super.free_blocks--;
            return i;
        }
    }
    
    return -1;
}

static void ramfs_split_path(const char* path, char* dir, char* file) {
    int len = my_strlen(path);
    int last_slash = -1;
    
    for(int i = len - 1; i >= 0; i--) {
        if(path[i] == '/') {
            last_slash = i;
            break;
        }
    }
    
    if(last_slash == -1) {
        my_strcpy(dir, "/");
        my_strcpy(file, path);
    } else {
        int dir_len = last_slash;
        if(dir_len == 0) dir_len = 1;
        for(int i = 0; i < dir_len; i++) {
            dir[i] = path[i];
        }
        dir[dir_len] = '\0';
        my_strcpy(file, path + last_slash + 1);
    }
}

static int ramfs_find_in_dir(uint32_t dir_inode, const char* name, uint32_t* inode_num) {
    ramfs_inode_t* dir = ramfs_get_inode(dir_inode);
    if(!dir || !(dir->flags & 1)) return -1;
    
    for(int b = 0; b < 16 && dir->block_ptrs[b]; b++) {
        uint32_t block_addr = ramfs_super.data_start + dir->block_ptrs[b] * ramfs_super.block_size;
        ramfs_dirent_t* entry = (ramfs_dirent_t*)(ramfs_memory + block_addr);
        
        for(int i = 0; i < ramfs_super.block_size / sizeof(ramfs_dirent_t); i++) {
            if(entry[i].inode != 0 && my_strcmp(entry[i].name, name) == 0) {
                *inode_num = entry[i].inode;
                return 0;
            }
        }
    }
    
    return -1;
}

static int ramfs_find_file(const char* path, uint32_t* parent_inode, char* filename) {
    char dir_path[256];
    char file_name[256];
    
    ramfs_split_path(path, dir_path, file_name);
    my_strcpy(filename, file_name);
    
    uint32_t current = 0;
    
    if(my_strcmp(dir_path, "/") == 0) {
        *parent_inode = 0;
        return 0;
    }
    
    char* token = dir_path;
    if(token[0] == '/') token++;
    
    char* slash;
    while((slash = token) != NULL) {
        char part[256];
        int i = 0;
        while(*token && *token != '/') {
            part[i++] = *token++;
        }
        part[i] = '\0';
        if(*token == '/') token++;
        
        uint32_t next;
        if(ramfs_find_in_dir(current, part, &next) != 0) {
            return -1;
        }
        
        ramfs_inode_t* inode = ramfs_get_inode(next);
        if(!inode || !(inode->flags & 1)) {
            return -1;
        }
        
        current = next;
        if(*token == '\0') break;
    }
    
    *parent_inode = current;
    return 0;
}

int ramfs_init(uint32_t size_mb) {
    kprint("[RAMFS] Initializing...\n");
    
    if(size_mb > 16) size_mb = 16;
    
    ramfs_memory = ramfs_buffer;
    my_memset(ramfs_memory, 0, size_mb * 1024 * 1024);
    
    ramfs_format();
    
    return 0;
}

void ramfs_format(void) {
    uint32_t total_memory = 16 * 1024 * 1024;
    uint32_t inode_count = 1024;
    uint32_t block_count = (total_memory - sizeof(ramfs_super_t) - inode_count * sizeof(ramfs_inode_t)) / RAMFS_BLOCK_SIZE;
    
    
    ramfs_super.magic = RAMFS_MAGIC;
    ramfs_super.version = 1;
    ramfs_super.total_inodes = inode_count;
    ramfs_super.free_inodes = inode_count - 1;
    ramfs_super.total_blocks = block_count;
    ramfs_super.free_blocks = block_count - 10;
    ramfs_super.block_size = RAMFS_BLOCK_SIZE;
    ramfs_super.root_inode = 0;
    ramfs_super.inode_table = sizeof(ramfs_super_t);
    ramfs_super.data_start = sizeof(ramfs_super_t) + inode_count * sizeof(ramfs_inode_t);
    ramfs_super.max_files = 1024;
    my_strcpy((char*)ramfs_super.volume_name, "RAMFS");
    

    ramfs_inode_t* root = ramfs_get_inode(0);
    my_memset(root, 0, sizeof(ramfs_inode_t));
    root->inode = 0;
    root->mode = 0755;
    root->flags = 1;
    root->refcount = 1;
    root->blocks = 1;
    root->block_ptrs[0] = 0;
    
    kprint("[RAMFS] Formatted: 16 MB, ");
    kprint_int(inode_count);
    kprint(" inodes\n");
}

int ramfs_mount(void) {
    if(ramfs_super.magic != RAMFS_MAGIC) {
        kprint("[RAMFS] Not formatted!\n");
        return -1;
    }
    
    ramfs_mounted = 1;
    kprint("[RAMFS] Mounted\n");
    return 0;
}

void ramfs_umount(void) {
    ramfs_mounted = 0;
    kprint("[RAMFS] Unmounted\n");
}

int ramfs_open(const char* path, int flags) {
    if(!ramfs_mounted) return -1;
    
    uint32_t parent_inode;
    char filename[256];
    
    if(ramfs_find_file(path, &parent_inode, filename) == 0) {
        uint32_t inode_num;
        if(ramfs_find_in_dir(parent_inode, filename, &inode_num) == 0) {
            return inode_num;
        }
    }
    
    uint32_t new_inode = ramfs_alloc_inode();
    if(new_inode == (uint32_t)-1) return -1;
    
    ramfs_inode_t* inode = ramfs_get_inode(new_inode);
    inode->mode = 0644;
    inode->flags = 0;
    inode->size = 0;
    inode->blocks = 0;
    
    ramfs_inode_t* parent = ramfs_get_inode(parent_inode);
    if(!parent) {
        ramfs_free_inode(new_inode);
        return -1;
    }
    
    for(int b = 0; b < 16; b++) {
        if(parent->block_ptrs[b] == 0) {
            parent->block_ptrs[b] = ramfs_alloc_block();
            if(parent->block_ptrs[b] == (uint32_t)-1) break;
            parent->blocks++;
        }
        
        uint32_t block_addr = ramfs_super.data_start + parent->block_ptrs[b] * ramfs_super.block_size;
        ramfs_dirent_t* entry = (ramfs_dirent_t*)(ramfs_memory + block_addr);
        
        for(int i = 0; i < ramfs_super.block_size / sizeof(ramfs_dirent_t); i++) {
            if(entry[i].inode == 0) {
                entry[i].inode = new_inode;
                my_strcpy(entry[i].name, filename);
                entry[i].name_len = my_strlen(filename);
                return new_inode;
            }
        }
    }
    
    ramfs_free_inode(new_inode);
    return -1;
}

int ramfs_close(int fd) {
    return 0;
}

int ramfs_read(int fd, void* buffer, uint32_t size) {
    if(!ramfs_mounted) return -1;
    
    ramfs_inode_t* inode = ramfs_get_inode(fd);
    if(!inode) return -1;
    
    uint32_t to_read = size;
    if(to_read > inode->size) to_read = inode->size;
    
    uint32_t bytes_read = 0;
    uint32_t block = 0;
    uint32_t offset = 0;
    
    while(bytes_read < to_read) {
        if(block >= 16 || inode->block_ptrs[block] == 0) break;
        
        uint32_t block_addr = ramfs_super.data_start + inode->block_ptrs[block] * ramfs_super.block_size;
        uint32_t block_offset = offset % ramfs_super.block_size;
        uint32_t to_copy = to_read - bytes_read;
        uint32_t block_remain = ramfs_super.block_size - block_offset;
        
        if(to_copy > block_remain) to_copy = block_remain;
        
        my_memcpy((uint8_t*)buffer + bytes_read, ramfs_memory + block_addr + block_offset, to_copy);
        
        bytes_read += to_copy;
        offset += to_copy;
        if(offset % ramfs_super.block_size == 0) block++;
    }
    
    return bytes_read;
}

int ramfs_write(int fd, const void* buffer, uint32_t size) {
    if(!ramfs_mounted) return -1;
    
    ramfs_inode_t* inode = ramfs_get_inode(fd);
    if(!inode) return -1;
    
    uint32_t bytes_written = 0;
    uint32_t block = 0;
    uint32_t offset = 0;
    
    while(bytes_written < size) {
        if(block >= 16) break;
        
        if(inode->block_ptrs[block] == 0) {
            inode->block_ptrs[block] = ramfs_alloc_block();
            if(inode->block_ptrs[block] == (uint32_t)-1) break;
            inode->blocks++;
        }
        
        uint32_t block_addr = ramfs_super.data_start + inode->block_ptrs[block] * ramfs_super.block_size;
        uint32_t block_offset = offset % ramfs_super.block_size;
        uint32_t to_copy = size - bytes_written;
        uint32_t block_remain = ramfs_super.block_size - block_offset;
        
        if(to_copy > block_remain) to_copy = block_remain;
        
        my_memcpy(ramfs_memory + block_addr + block_offset, (uint8_t*)buffer + bytes_written, to_copy);
        
        bytes_written += to_copy;
        offset += to_copy;
        if(offset % ramfs_super.block_size == 0) block++;
    }
    
    if(offset > inode->size) inode->size = offset;
    
    return bytes_written;
}

int ramfs_mkdir(const char* path) {
    if(!ramfs_mounted) return -1;
    
    uint32_t parent_inode;
    char dirname[256];
    
    if(ramfs_find_file(path, &parent_inode, dirname) == 0) return -1;
    
    uint32_t new_inode = ramfs_alloc_inode();
    if(new_inode == (uint32_t)-1) return -1;
    
    ramfs_inode_t* inode = ramfs_get_inode(new_inode);
    inode->mode = 0755;
    inode->flags = 1;
    inode->blocks = 1;
    inode->block_ptrs[0] = ramfs_alloc_block();
    
    ramfs_inode_t* parent = ramfs_get_inode(parent_inode);
    
    for(int b = 0; b < 16; b++) {
        if(parent->block_ptrs[b] == 0) {
            parent->block_ptrs[b] = ramfs_alloc_block();
            if(parent->block_ptrs[b] == (uint32_t)-1) break;
            parent->blocks++;
        }
        
        uint32_t block_addr = ramfs_super.data_start + parent->block_ptrs[b] * ramfs_super.block_size;
        ramfs_dirent_t* entry = (ramfs_dirent_t*)(ramfs_memory + block_addr);
        
        for(int i = 0; i < ramfs_super.block_size / sizeof(ramfs_dirent_t); i++) {
            if(entry[i].inode == 0) {
                entry[i].inode = new_inode;
                my_strcpy(entry[i].name, dirname);
                entry[i].name_len = my_strlen(dirname);
                return 0;
            }
        }
    }
    
    ramfs_free_inode(new_inode);
    return -1;
}

int ramfs_ls(const char* path) {
    if(!ramfs_mounted) return -1;
    
    uint32_t dir_inode;
    uint32_t parent_inode;
    char filename[256];
    
    if(my_strcmp(path, "/") == 0) {
        dir_inode = 0;
    } else {
        if(ramfs_find_file(path, &parent_inode, filename) != 0) return -1;
        uint32_t inode_num;
        if(ramfs_find_in_dir(parent_inode, filename, &inode_num) != 0) return -1;
        dir_inode = inode_num;
    }
    
    ramfs_inode_t* dir = ramfs_get_inode(dir_inode);
    if(!dir || !(dir->flags & 1)) return -1;
    
    kprint("\nDirectory listing:\n");
    kprint("------------------\n");
    
    for(int b = 0; b < 16 && dir->block_ptrs[b]; b++) {
        uint32_t block_addr = ramfs_super.data_start + dir->block_ptrs[b] * ramfs_super.block_size;
        ramfs_dirent_t* entry = (ramfs_dirent_t*)(ramfs_memory + block_addr);
        
        for(int i = 0; i < ramfs_super.block_size / sizeof(ramfs_dirent_t); i++) {
            if(entry[i].inode != 0) {
                ramfs_inode_t* file = ramfs_get_inode(entry[i].inode);
                if(file->flags & 1) {
                    kprint("[DIR]  ");
                } else {
                    kprint("[FILE] ");
                }
                kprint(entry[i].name);
                
                if(!(file->flags & 1)) {
                    kprint(" (");
                    kprint_int(file->size);
                    kprint(" bytes)");
                }
                kprint("\n");
            }
        }
    }
    
    return 0;
}

int ramfs_find(const char* path) {
    if(!ramfs_mounted) return -1;
    
    uint32_t parent_inode;
    char filename[256];
    
    if(ramfs_find_file(path, &parent_inode, filename) != 0) {
        kprint("File not found\n");
        return -1;
    }
    
    uint32_t inode_num;
    if(ramfs_find_in_dir(parent_inode, filename, &inode_num) != 0) {
        kprint("File not found\n");
        return -1;
    }
    
    ramfs_inode_t* inode = ramfs_get_inode(inode_num);
    
    kprint("Found: ");
    kprint(path);
    kprint("\n");
    kprint("Size: ");
    kprint_int(inode->size);
    kprint(" bytes\n");
    if(inode->flags & 1) {
        kprint("Type: Directory\n");
    } else {
        kprint("Type: File\n");
    }
    
    return 0;
}

void ramfs_info(void) {
    if(!ramfs_mounted) {
        kprint("RAMFS not mounted\n");
        return;
    }
    
    uint32_t total_mb = ramfs_super.total_blocks * ramfs_super.block_size / 1024 / 1024;
    uint32_t free_mb = ramfs_super.free_blocks * ramfs_super.block_size / 1024 / 1024;
    uint32_t used_mb = total_mb - free_mb;
    
    kprint("\n=== RAMFS INFO ===\n");
    kprint("Volume: ");
    for(int i = 0; i < 32 && ramfs_super.volume_name[i]; i++) {
        char s[2] = {ramfs_super.volume_name[i], '\0'};
        kprint(s);
    }
    kprint("\n");
    kprint("Total: "); kprint_int(total_mb); kprint(" MB\n");
    kprint("Used:  "); kprint_int(used_mb); kprint(" MB\n");
    kprint("Free:  "); kprint_int(free_mb); kprint(" MB\n");
    kprint("Inodes: "); kprint_int(ramfs_super.total_inodes - ramfs_super.free_inodes);
    kprint("/"); kprint_int(ramfs_super.total_inodes); kprint("\n");
}

void ramfs_debug(void) {
    kprint("\n=== RAMFS DEBUG ===\n");
    kprint("Magic: 0x"); kprint_hex32(ramfs_super.magic); kprint("\n");
    kprint("Version: "); kprint_int(ramfs_super.version); kprint("\n");
    kprint("Total inodes: "); kprint_int(ramfs_super.total_inodes); kprint("\n");
    kprint("Free inodes: "); kprint_int(ramfs_super.free_inodes); kprint("\n");
    kprint("Total blocks: "); kprint_int(ramfs_super.total_blocks); kprint("\n");
    kprint("Free blocks: "); kprint_int(ramfs_super.free_blocks); kprint("\n");
    kprint("Block size: "); kprint_int(ramfs_super.block_size); kprint("\n");
    kprint("Root inode: "); kprint_int(ramfs_super.root_inode); kprint("\n");
}