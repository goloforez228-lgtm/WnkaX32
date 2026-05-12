#ifndef SYSCALL_LINUX_H
#define SYSCALL_LINUX_H

#include <stdint.h>

#define SYS_EXIT          1
#define SYS_FORK          2
#define SYS_READ          3
#define SYS_WRITE         4
#define SYS_OPEN          5
#define SYS_CLOSE         6
#define SYS_WAITPID       7
#define SYS_CREAT         8
#define SYS_LINK          9
#define SYS_UNLINK        10
#define SYS_EXECVE        11
#define SYS_CHDIR         12
#define SYS_TIME          13
#define SYS_MKNOD         14
#define SYS_CHMOD         15
#define SYS_LCHOWN        16
#define SYS_BREAK         17
#define SYS_STAT          18
#define SYS_LSEEK         19
#define SYS_GETPID        20
#define SYS_MOUNT         21
#define SYS_UMOUNT        22
#define SYS_SETUID        23
#define SYS_GETUID        24
#define SYS_STIME         25
#define SYS_PTRACE        26
#define SYS_ALARM         27
#define SYS_FSTAT         28
#define SYS_PAUSE         29
#define SYS_UTIME         30
#define SYS_ACCESS        33
#define SYS_NICE          34
#define SYS_SYNC          36
#define SYS_KILL          37
#define SYS_RENAME        38
#define SYS_MKDIR         39
#define SYS_RMDIR         40
#define SYS_DUP           41
#define SYS_PIPE          42
#define SYS_TIMES         43
#define SYS_BRK           45
#define SYS_SETGID        46
#define SYS_GETGID        47
#define SYS_SIGNAL        48
#define SYS_GETEUID       49
#define SYS_GETEGID       50
#define SYS_IOCTL         54
#define SYS_FCNTL         55
#define SYS_SETPGID       57
#define SYS_DUP2          63
#define SYS_GETPPID       64
#define SYS_GETPGRP       65
#define SYS_READLINK      85
#define SYS_MMAP          90
#define SYS_MUNMAP        91
#define SYS_LSTAT         107
#define SYS_SOCKETCALL    102
#define SYS_SETITIMER     104
#define SYS_GETITIMER     105
#define SYS_SIGACTION     67
#define SYS_SIGPROCMASK   126
#define SYS_GETDENTS      141
#define SYS_WRITEV        146
#define SYS_NANOSLEEP     162
#define SYS_GETCWD        183
#define SYS_UNAME        122

typedef struct {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    uint32_t eip;
    uint32_t esp;
    uint32_t eflags;
} linux_regs_t;

void linux_syscall_init(void);
int  linux_syscall_handler(linux_regs_t* regs);
void syscall_init(void);
extern "C" void int80_handler(void);

#endif