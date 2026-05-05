#ifndef MULTITASK_H
#define MULTITASK_H

#include <stdint.h>

#define MAX_PROCESSES 16
#define QUANTUM 10
#define MAX_PROC_NAME 32

#define PROC_TERMINATED 0
#define PROC_READY      1
#define PROC_RUNNING    2
#define PROC_WAITING    3
#define PROC_SLEEPING   4

typedef struct {
    int locked;
    int owner_pid;
    int wait_count;
    const char* name;
} mutex_t;

typedef struct {
    int pid;
    int ppid;
    char name[MAX_PROC_NAME];
    int state;
    int priority;
    int ticks_left;
    int exit_code;
    uint32_t cpu_time;
    uint32_t wakeup_time;
    int (*entry)(void*);
    void* arg;
    int nice;
    uint32_t ticks_total;
} process_t;

int task_create(int (*entry)(void*), void* arg, const char* name, int priority);
void task_exit(int code);
void task_yield(void);
void task_sleep(int ms);
void task_list(void);
void task_kill(int pid);
int task_getpid(void);
void scheduler_init(void);
void scheduler_start(void);
void timer_handler(void);
void schedule(void);


void mutex_init(mutex_t* m, const char* name);
void mutex_lock(mutex_t* m);
void mutex_unlock(mutex_t* m);


int run_background_command(const char* cmd, const char* name);
void list_background(void);
void foreground_command(int id);
void kill_background(int id);


void test_multitask(void);

#endif