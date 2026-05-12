#include "syscall_linux.h"
#include "video.h"
#include "graph.h"
#include "elf_linux.h"
#include "kernel_stubs.h"
#include "vfs_linux.h"
#include <stdint.h>

static int linux_initialized = 0;
static uint32_t linux_brk_start = 0x30000000;
static uint32_t linux_brk_end   = 0x30000000;
static uint32_t linux_brk_max   = 0x40000000;

void linux_syscall_init(void) {
    if(linux_initialized) return;
    
    vfs_init();
    elf_init();
    
    linux_initialized = 1;
    kprint_color("[LINUX] Syscall emulation layer initialized\n", TXT_GREEN);
    kprint("[LINUX] Heap: 0x");
    kprint_hex32(linux_brk_start);
    kprint(" - 0x");
    kprint_hex32(linux_brk_max);
    kprint(" (256 MB)\n");
}

static uint32_t linux_sys_brk(uint32_t addr) {
    if(addr == 0) {
        return linux_brk_end;
    }
    
    if(addr >= linux_brk_start && addr <= linux_brk_max) {
        uint32_t old_brk = linux_brk_end;
        
        if(addr > linux_brk_end) {
            for(uint32_t p = linux_brk_end; p < addr; p++) {
                *((volatile uint8_t*)p) = 0;
            }
        }
        
        linux_brk_end = addr;
        return old_brk;
    }
    
    return linux_brk_end;
}

static uint32_t linux_sys_mmap(uint32_t addr, uint32_t length, int prot, 
                                int flags, int fd, uint32_t offset) {
    (void)fd;
    (void)offset;
    (void)prot;
    
    if(!(flags & 0x20)) {
        addr = 0;
    }
    
    if(addr == 0) {
        uint32_t result = linux_brk_end;
        linux_brk_end += length;
        
        if(linux_brk_end & 0xFFF) {
            linux_brk_end = (linux_brk_end + 0x1000) & ~0xFFF;
        }
        
        for(uint32_t p = result; p < result + length; p++) {
            *((volatile uint8_t*)p) = 0;
        }
        
        return result;
    }
    
    return addr;
}

static int linux_sys_munmap(uint32_t addr, uint32_t length) {
    (void)addr;
    (void)length;
    return 0;
}

static int linux_sys_uname(uint32_t buf_addr) {
    if(buf_addr == 0) return -1;
    
    struct linux_utsname {
        char sysname[65];
        char nodename[65];
        char release[65];
        char version[65];
        char machine[65];
    };
    
    struct linux_utsname* uts = (struct linux_utsname*)buf_addr;
    
    const char* sysname = "Linux";
    const char* nodename = "wnka";
    const char* release = "5.15.0-wnka";
    const char* version = "#1 WNKA Compat SMP";
    const char* machine = "i686";
    
    for(int i = 0; i < 65; i++) {
        uts->sysname[i] = (i < 6) ? sysname[i] : 0;
        uts->nodename[i] = (i < 5) ? nodename[i] : 0;
        uts->release[i] = (i < 13) ? release[i] : 0;
        uts->version[i] = (i < 19) ? version[i] : 0;
        uts->machine[i] = (i < 5) ? machine[i] : 0;
    }
    
    return 0;
}

static int linux_sys_getcwd(char* buf, int size) {
    if(buf && size > 0) {
        buf[0] = '/';
        if(size > 1) buf[1] = '\0';
    }
    return (int)(uint32_t)buf;
}

int linux_syscall_handler(linux_regs_t* regs) {
    int syscall_num = regs->eax;
    int arg1 = regs->ebx;
    int arg2 = regs->ecx;
    int arg3 = regs->edx;
    int arg4 = regs->esi;
    int arg5 = regs->edi;
    
    switch(syscall_num) {
        
        case SYS_OPEN:
            regs->eax = vfs_open((const char*)arg1, arg2, arg3);
            break;
            
        case SYS_READ:
            regs->eax = vfs_read(arg1, (void*)arg2, arg3);
            break;
            
        case SYS_WRITE:
            regs->eax = vfs_write(arg1, (const void*)arg2, arg3);
            break;
            
        case SYS_CLOSE:
            regs->eax = vfs_close(arg1);
            break;
            
        case SYS_LSEEK:
            regs->eax = vfs_lseek(arg1, arg2, arg3);
            break;
            
        case SYS_STAT:
            regs->eax = vfs_stat((const char*)arg1, (struct vfs_stat_t*)arg2);
            break;
            
        case SYS_FSTAT:
            regs->eax = vfs_fstat(arg1, (struct vfs_stat_t*)arg2);
            break;
            
        case SYS_LSTAT:
            regs->eax = vfs_lstat((const char*)arg1, (struct vfs_stat_t*)arg2);
            break;
            
        case SYS_ACCESS:
            regs->eax = vfs_access((const char*)arg1, arg2);
            break;
            
        case SYS_RENAME:
            regs->eax = vfs_rename((const char*)arg1, (const char*)arg2);
            break;
            
        case SYS_MKDIR:
            regs->eax = vfs_mkdir((const char*)arg1, arg2);
            break;
            
        case SYS_RMDIR:
            regs->eax = vfs_rmdir((const char*)arg1);
            break;
            
        case SYS_UNLINK:
            regs->eax = vfs_unlink((const char*)arg1);
            break;
            
        case SYS_GETDENTS:
            regs->eax = vfs_getdents(arg1, (struct vfs_dirent_t*)arg2, arg3);
            break;
            
        case SYS_FCNTL:
            regs->eax = vfs_fcntl(arg1, arg2, arg3);
            break;
            
        case SYS_IOCTL:
            regs->eax = vfs_ioctl(arg1, arg2, (void*)arg3);
            break;
            
        case SYS_WRITEV:
            regs->eax = vfs_writev(arg1, (const struct iovec*)arg2, arg3);
            break;
            
        case SYS_PIPE:
            regs->eax = vfs_pipe((int*)arg1);
            break;
            
        case SYS_DUP:
            regs->eax = arg1;
            break;
            
        case SYS_DUP2:
            regs->eax = arg2;
            break;
            
        case SYS_READLINK:
            regs->eax = -1;
            break;
            
        case SYS_CREAT:
            regs->eax = vfs_open((const char*)arg1, 0x41, arg2);
            break;
            
        case SYS_GETCWD:
            regs->eax = linux_sys_getcwd((char*)arg1, arg2);
            break;
            
        case SYS_CHDIR:
            regs->eax = 0;
            break;
            
        case SYS_BRK:
            regs->eax = linux_sys_brk(arg1);
            break;
            
        case SYS_MMAP:
            regs->eax = linux_sys_mmap(arg1, arg2, arg3, arg4, arg5, 
                                        *(uint32_t*)(regs->esp + 4));
            break;
            
        case SYS_MUNMAP:
            regs->eax = linux_sys_munmap(arg1, arg2);
            break;
            
        case SYS_EXIT:
            kprint("[LINUX] Exit(");
            kprint_int(arg1);
            kprint(")\n");
            regs->eax = arg1;
            return -1;
            
        case SYS_EXECVE:
            elf_execve((const char*)arg1, (char* const*)arg2, (char* const*)arg3);
            break;
            
        case SYS_GETPID:
            regs->eax = 1;
            break;
            
        case SYS_GETUID:
            regs->eax = 0;
            break;
            
        case SYS_GETEUID:
            regs->eax = 0;
            break;
            
        case SYS_GETGID:
            regs->eax = 0;
            break;
            
        case SYS_GETEGID:
            regs->eax = 0;
            break;
            
        case SYS_GETPPID:
            regs->eax = 0;
            break;
            
        case SYS_SETUID:
            regs->eax = 0;
            break;
            
        case SYS_SETGID:
            regs->eax = 0;
            break;
            
        case SYS_WAITPID:
            regs->eax = 0;
            break;
            
        case SYS_KILL:
            regs->eax = 0;
            break;
            
        case SYS_FORK:
            regs->eax = -1;
            break;
            
        case SYS_TIME:
            regs->eax = (uint32_t)seconds;
            break;
            
        case SYS_TIMES:
            regs->eax = 0;
            break;
            
        case SYS_NANOSLEEP:
            regs->eax = 0;
            break;
            
        case SYS_ALARM:
            regs->eax = 0;
            break;
            
        case SYS_UNAME:
            regs->eax = linux_sys_uname(arg1);
            break;
            
        case SYS_SYNC:
            regs->eax = 0;
            break;
            
            
        case SYS_SOCKETCALL:
            regs->eax = -1;
            break;
            
        case SYS_SIGNAL:
            regs->eax = 0;
            break;
            
        case SYS_SIGACTION:
            regs->eax = 0;
            break;
            
        case SYS_SIGPROCMASK:
            regs->eax = 0;
            break;
            
        case SYS_SETPGID:
            regs->eax = 0;
            break;
            
        case SYS_GETPGRP:
            regs->eax = 0;
            break;
            
        case SYS_PTRACE:
            regs->eax = -1;
            break;
            
        case SYS_LINK:
            regs->eax = 0;
            break;
            
        case SYS_MKNOD:
            regs->eax = 0;
            break;
            
        case SYS_CHMOD:
            regs->eax = 0;
            break;
            
        case SYS_LCHOWN:
            regs->eax = 0;
            break;
            
        case SYS_UTIME:
            regs->eax = 0;
            break;
            
        case SYS_MOUNT:
            regs->eax = 0;
            break;
            
        case SYS_UMOUNT:
            regs->eax = 0;
            break;
            
        default:
            kprint("[LINUX] Unknown syscall #");
            kprint_int(syscall_num);
            kprint(" (0x");
            kprint_hex32(syscall_num);
            kprint(")\n");
            regs->eax = -38;
            break;
    }
    
    return 0;
}
extern "C" void linux_syscall_handler_c(linux_regs_t* regs) {
    int result = linux_syscall_handler(regs);
    regs->eax = result;
}
