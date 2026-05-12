#ifndef VFS_LINUX_H
#define VFS_LINUX_H

#include <stdint.h>

struct vfs_stat_t {
    uint32_t st_dev;
    uint16_t __pad1;
    uint32_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_rdev;
    uint16_t __pad2;
    uint32_t st_size;
    uint32_t st_blksize;
    uint32_t st_blocks;
    uint32_t st_atime;
    uint32_t __unused1;
    uint32_t st_mtime;
    uint32_t __unused2;
    uint32_t st_ctime;
    uint32_t __unused3;
    uint32_t __unused4;
    uint32_t __unused5;
};

struct vfs_dirent_t {
    uint32_t d_ino;
    uint32_t d_off;
    uint16_t d_reclen;
    char     d_name[256];
};

void vfs_init(void);

int  vfs_open(const char* path, int flags, int mode);
int  vfs_read(int fd, void* buf, int count);
int  vfs_write(int fd, const void* buf, int count);
int  vfs_close(int fd);
int  vfs_lseek(int fd, int offset, int whence);
int  vfs_stat(const char* path, struct vfs_stat_t* buf);
int  vfs_fstat(int fd, struct vfs_stat_t* buf);
int  vfs_lstat(const char* path, struct vfs_stat_t* buf);
int  vfs_access(const char* path, int mode);
int  vfs_rename(const char* oldpath, const char* newpath);
int  vfs_mkdir(const char* path, int mode);
int  vfs_rmdir(const char* path);
int  vfs_unlink(const char* path);
int  vfs_getdents(int fd, struct vfs_dirent_t* dirp, int count);
int  vfs_fcntl(int fd, int cmd, int arg);
int  vfs_ioctl(int fd, int request, void* argp);
int  vfs_writev(int fd, const struct iovec* iov, int iovcnt);
int  vfs_pipe(int pipefd[2]);

#endif