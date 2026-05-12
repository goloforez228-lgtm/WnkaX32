#include "vfs_linux.h"
#include "video.h"
#include "graph.h"
#include "kernel_stubs.h"
#include "ata.h"
#include "string_utils.h"
#include <stdint.h>

static void kprint_char(char c) {
    char s[2] = {c, '\0'};
    kprint(s);
}
typedef struct {
    int used;
    char path[256];
    uint32_t offset;
    int flags;
    uint8_t* data;    
    uint32_t data_size;
    uint32_t data_capacity;
    int is_special;   
} vfs_fd_t;

static vfs_fd_t vfs_fds[256];
static int vfs_initialized = 0;

void vfs_init(void) {
    if(vfs_initialized) return;
    
    for(int i = 0; i < 256; i++) {
        vfs_fds[i].used = 0;
        vfs_fds[i].data = 0;
    }
    
    vfs_fds[0].used = 1;
    vfs_fds[0].data = 0;
    vfs_fds[0].is_special = 0;
    my_strcpy(vfs_fds[0].path, "/dev/stdin");
    
    vfs_fds[1].used = 1;
    vfs_fds[1].data = 0;
    vfs_fds[1].is_special = 0;
    my_strcpy(vfs_fds[1].path, "/dev/stdout");
    
    vfs_fds[2].used = 1;
    vfs_fds[2].data = 0;
    vfs_fds[2].is_special = 0;
    my_strcpy(vfs_fds[2].path, "/dev/stderr");
    
    vfs_initialized = 1;
    kprint_color("[VFS] Virtual File System initialized\n", TXT_GREEN);
}

int vfs_open(const char* path, int flags, int mode) {
    (void)mode;
    
    for(int fd = 3; fd < 256; fd++) {
        if(!vfs_fds[fd].used) {
            vfs_fds[fd].used = 1;
            vfs_fds[fd].offset = 0;
            vfs_fds[fd].flags = flags;
            
            my_strcpy(vfs_fds[fd].path, path);
            
            uint16_t dir_buf[256];
            read_sector(100, dir_buf);
            
            const char* filename = path;
            if(filename[0] == '/') filename++;
            
            if(my_strcmp(filename, "dev/null") == 0 || my_strcmp(path, "/dev/null") == 0) {
                vfs_fds[fd].is_special = 1;
                vfs_fds[fd].data = 0;
                vfs_fds[fd].data_size = 0;
                return fd;
            }
            
            for(int i = 0; i < 32; i++) {
                char name[12] = {0};
                for(int j = 0; j < 11; j++) {
                    name[j] = ((char*)dir_buf)[i*16 + j];
                }
                
                if(my_strcmp(filename, name) == 0) {
                    int file_sector = dir_buf[i*8 + 6];
                    int file_size = dir_buf[i*8 + 7];
                    
                    vfs_fds[fd].is_special = 1;
                    vfs_fds[fd].data = (uint8_t*)0x28000000 + fd * 4096;
                    vfs_fds[fd].data_capacity = 4096;
                    vfs_fds[fd].data_size = file_size;
                    
                    uint16_t data_buf[256];
                    read_sector(file_sector, data_buf);
                    
                    for(int j = 0; j < file_size && j < 4096; j++) {
                        if(j % 2 == 0) {
                            vfs_fds[fd].data[j] = data_buf[j/2] & 0xFF;
                        } else {
                            vfs_fds[fd].data[j] = (data_buf[j/2] >> 8) & 0xFF;
                        }
                    }
                    
                    return fd;
                }
            }
            
            vfs_fds[fd].used = 0;
            return -2;
        }
    }
    
    return -24;
}

int vfs_read(int fd, void* buf, int count) {
    if(fd < 0 || fd >= 256 || !vfs_fds[fd].used) {
        return -9; 
    }
    
    if(fd == 0) {
        return 0;
    }
    
    if(vfs_fds[fd].data) {
        uint32_t available = vfs_fds[fd].data_size - vfs_fds[fd].offset;
        if(available <= 0) return 0;
        
        uint32_t to_read = (uint32_t)count;
        if(to_read > available) to_read = available;
        
        uint8_t* dest = (uint8_t*)buf;
        for(uint32_t i = 0; i < to_read; i++) {
            dest[i] = vfs_fds[fd].data[vfs_fds[fd].offset + i];
        }
        
        vfs_fds[fd].offset += to_read;
        return to_read;
    }
    
    return 0;
}

int vfs_write(int fd, const void* buf, int count) {
    if(fd < 0 || fd >= 256 || !vfs_fds[fd].used) {
        return -9; 
    }
    
    if(fd == 1 || fd == 2) {
        const char* text = (const char*)buf;
        for(int i = 0; i < count; i++) {
            kprint_char(text[i]);
        }
        return count;
    }
    
    return count;
}

int vfs_close(int fd) {
    if(fd >= 3 && fd < 256) {
        vfs_fds[fd].used = 0;
    }
    return 0;
}

int vfs_lseek(int fd, int offset, int whence) {
    if(fd < 0 || fd >= 256 || !vfs_fds[fd].used) return -9;
    
    if(whence == 0) vfs_fds[fd].offset = offset;   
    else if(whence == 1) vfs_fds[fd].offset += offset; 
    else if(whence == 2) vfs_fds[fd].offset = vfs_fds[fd].data_size + offset; 
    
    return vfs_fds[fd].offset;
}

int vfs_stat(const char* path, struct vfs_stat_t* buf) {
    int fd = vfs_open(path, 0, 0);
    if(fd < 0) return fd;
    int ret = vfs_fstat(fd, buf);
    vfs_close(fd);
    return ret;
}

int vfs_fstat(int fd, struct vfs_stat_t* buf) {
    if(fd < 0 || fd >= 256 || !vfs_fds[fd].used) return -9;
    
    for(int i = 0; i < (int)sizeof(struct vfs_stat_t); i++) {
        ((uint8_t*)buf)[i] = 0;
    }
    
    buf->st_mode = 0x81A4; 
    buf->st_size = vfs_fds[fd].data_size;
    buf->st_nlink = 1;
    buf->st_uid = 0;
    buf->st_gid = 0;
    
    return 0;
}

int vfs_lstat(const char* path, struct vfs_stat_t* buf) {
    return vfs_stat(path, buf);
}

int vfs_access(const char* path, int mode) {
    int fd = vfs_open(path, 0, 0);
    if(fd < 0) return fd;
    vfs_close(fd);
    return 0; 
}

int vfs_rename(const char* oldpath, const char* newpath) {
    (void)oldpath;
    (void)newpath;
    return 0;
}

int vfs_mkdir(const char* path, int mode) {
    (void)path;
    (void)mode;
    return 0;
}

int vfs_rmdir(const char* path) {
    (void)path;
    return 0;
}

int vfs_unlink(const char* path) {
    (void)path;
    return 0;
}

int vfs_getdents(int fd, struct vfs_dirent_t* dirp, int count) {
    (void)fd;
    (void)dirp;
    return 0;  
}

int vfs_fcntl(int fd, int cmd, int arg) {
    (void)fd;
    (void)cmd;
    (void)arg;
    return 0;
}

int vfs_ioctl(int fd, int request, void* argp) {
    (void)fd;
    (void)request;
    (void)argp;
    return 0; 
}

int vfs_writev(int fd, const struct iovec* iov, int iovcnt) {
    (void)iov;
    int total = 0;
    for(int i = 0; i < iovcnt; i++) {
        total += 0;
    }
    return total;
}

int vfs_pipe(int pipefd[2]) {
    pipefd[0] = -1;
    pipefd[1] = -1;
    return -1;
}