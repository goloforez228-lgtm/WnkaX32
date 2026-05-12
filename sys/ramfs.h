#ifndef RAMFS_H
#define RAMFS_H

#include <stdint.h>

#define RAMFS_MAGIC        0x52414D46  
#define RAMFS_MAX_FILES    1024
#define RAMFS_MAX_NAME     256
#define RAMFS_BLOCK_SIZE   4096
#define RAMFS_MAX_BLOCKS   65536


typedef struct {
    uint32_t inode;         
    char     name[256];     
    uint32_t mode;        
    uint32_t uid;  
    uint32_t gid;       
    uint32_t size;      
    uint32_t blocks;        
    uint32_t atime;        
    uint32_t mtime;      
    uint32_t ctime;       
    uint32_t refcount;    
    uint32_t flags;          
    uint32_t block_ptrs[16]; 
    uint32_t indirect;        
    uint32_t double_indirect; 
} ramfs_inode_t;

typedef struct {
    uint32_t magic;   
    uint32_t version;    
    uint32_t total_inodes;    
    uint32_t free_inodes;    
    uint32_t total_blocks;    
    uint32_t free_blocks;  
    uint32_t block_size;      
    uint32_t root_inode;   
    uint32_t inode_table;    
    uint32_t data_start;  
    uint32_t max_files;      
    uint8_t  volume_name[32]; 
} ramfs_super_t;

typedef struct {
    uint32_t inode;         
    uint32_t next;            
    uint32_t name_len;       
    char     name[256];     
} ramfs_dirent_t;

extern ramfs_super_t ramfs_super;
extern uint8_t* ramfs_memory;
extern int ramfs_mounted;

int ramfs_init(uint32_t size_mb);
int ramfs_mount(void);
void ramfs_umount(void);
void ramfs_format(void);
int ramfs_open(const char* path, int flags);
int ramfs_close(int fd);
int ramfs_read(int fd, void* buffer, uint32_t size);
int ramfs_write(int fd, const void* buffer, uint32_t size);
int ramfs_stat(const char* path, ramfs_inode_t* stat);
int ramfs_mkdir(const char* path);
int ramfs_ls(const char* path);
int ramfs_find(const char* path);
void ramfs_info(void);
void ramfs_debug(void);

#endif