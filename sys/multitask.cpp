#include "multitask.h"
#include "video.h"
#include "graph.h"
#include "kernel_stubs.h"
#include "string_utils.h"
#include <stdint.h>

#define NULL 0

static process_t processes[MAX_PROCESSES];
static int process_count = 0;
static int current_pid = -1;
static int next_pid = 1;
static int scheduler_enabled = 0;
static uint32_t system_timer = 0;

typedef struct {
    int pid;
    char name[32];
    char cmd[256];
    int active;
} bg_job_t;

static bg_job_t bg_jobs[16];
static int bg_job_count = 0;

extern void process_command(char* input_buffer, int& input_ptr);

void mutex_init(mutex_t* m, const char* name) {
    m->locked = 0;
    m->owner_pid = -1;
    m->wait_count = 0;
    m->name = name;
}

void mutex_lock(mutex_t* m) {
    while(m->locked) task_yield();
    m->locked = 1;
    m->owner_pid = task_getpid();
}

void mutex_unlock(mutex_t* m) {
    if(m->owner_pid != task_getpid()) return;
    m->locked = 0;
    m->owner_pid = -1;
}

static process_t* find_process(int pid) {
    for(int i = 0; i < MAX_PROCESSES; i++) {
        if(processes[i].state != PROC_TERMINATED && processes[i].pid == pid) {
            return &processes[i];
        }
    }
    return NULL;
}
static int find_free_slot(void) {
    for(int i = 0; i < MAX_PROCESSES; i++) {
        if(processes[i].state == PROC_TERMINATED) {
            return i;
        }
    }
    return -1;
}

int task_create(int (*entry)(void*), void* arg, const char* name, int priority) {
    if(process_count >= MAX_PROCESSES) return -1;
    
    int slot = find_free_slot();
    if(slot < 0) return -1;
    
    process_t* p = &processes[slot];
    
    p->pid = next_pid++;
    p->ppid = (current_pid >= 0) ? current_pid : 0;
    p->state = PROC_READY;
    p->priority = priority;
    p->nice = 0;
    p->ticks_left = QUANTUM;
    p->ticks_total = 0;
    p->wakeup_time = 0;
    p->exit_code = 0;
    p->cpu_time = 0;
    p->entry = entry;
    p->arg = arg;
    my_strcpy(p->name, name);
    
    process_count++;
    return p->pid;
}

void task_exit(int code) {
    if(current_pid >= 0) {
        process_t* p = find_process(current_pid);
        if(p) {
            p->state = PROC_TERMINATED;
            p->exit_code = code;
            process_count--;
        }
        task_yield();
    }
}

void schedule(void) {
    if(!scheduler_enabled) return;
    if(process_count == 0) return;
    
    int start = (current_pid + 1) % MAX_PROCESSES;
    int found = -1;
    
    for(int i = 0; i < MAX_PROCESSES; i++) {
        int idx = (start + i) % MAX_PROCESSES;
        if(processes[idx].state == PROC_READY) {
            found = idx;
            break;
        }
    }
    
    if(found >= 0 && current_pid != found) {
        if(current_pid >= 0) {
            processes[current_pid].state = PROC_READY;
        }
        current_pid = found;
        processes[current_pid].state = PROC_RUNNING;
    }
}

void timer_handler(void) {
    if(!scheduler_enabled) return;
    system_timer++;
    
    for(int i = 0; i < MAX_PROCESSES; i++) {
        if(processes[i].state == PROC_SLEEPING && 
           processes[i].wakeup_time <= system_timer) {
            processes[i].state = PROC_READY;
        }
    }
    
    if(current_pid >= 0) {
        process_t* p = find_process(current_pid);
        if(p) {
            p->ticks_left--;
            p->cpu_time++;
            if(p->ticks_left <= 0) {
                p->ticks_left = QUANTUM;
                schedule();
            }
        }
    }
}

void task_yield(void) {
    if(scheduler_enabled) schedule();
}

void task_sleep(int ms) {
    if(current_pid >= 0) {
        process_t* p = find_process(current_pid);
        if(p) {
            p->state = PROC_SLEEPING;
            p->wakeup_time = system_timer + ms;
            schedule();
        }
    }
}

void scheduler_init(void) {
    for(int i = 0; i < MAX_PROCESSES; i++) {
        processes[i].state = PROC_TERMINATED;
    }
    current_pid = -1;
    process_count = 0;
    next_pid = 1;
    scheduler_enabled = 0;
    system_timer = 0;
    bg_job_count = 0;
}

void scheduler_start(void) {
    scheduler_enabled = 1;
    kprint_color("[SCHED] Scheduler started\n", TXT_GREEN);
    schedule();
}

void task_list(void) {
    kprint("\n=== PROCESS LIST ===\n");
    kprint("PID  NAME      STATE    PRIOR  CPU(ms)\n");
    kprint("---  --------  -------- -----  ------\n");
    
    for(int i = 0; i < MAX_PROCESSES; i++) {
        if(processes[i].state != PROC_TERMINATED) {
            process_t* p = &processes[i];
            
            if(p->pid < 10) kprint(" ");
            kprint_int(p->pid);
            kprint("   ");
            kprint(p->name);
            for(int s = my_strlen(p->name); s < 9; s++) kprint(" ");
            kprint(" ");
            
            switch(p->state) {
                case PROC_READY:    kprint("READY   "); break;
                case PROC_RUNNING:  kprint("RUNNING "); break;
                case PROC_WAITING:  kprint("WAITING "); break;
                case PROC_SLEEPING: kprint("SLEEPING"); break;
                default:            kprint("UNKNOWN "); break;
            }
            kprint(" ");
            kprint_int(p->priority);
            kprint("     ");
            kprint_int(p->cpu_time / 10);
            kprint("\n");
        }
    }
}

void task_kill(int pid) {
    process_t* p = find_process(pid);
    if(p) {
        p->state = PROC_TERMINATED;
        process_count--;
        kprint_color("Process ", TXT_YELLOW);
        kprint_int(pid); kprint_color(" killed\n", TXT_YELLOW);
    } else {
        kprint_color("Process not found\n", TXT_RED);
    }
}

int task_getpid(void) {
    return current_pid >= 0 ? processes[current_pid].pid : -1;
}

int bg_command_wrapper(void* arg) {
    char* cmd = (char*)arg;
    
    kprint_color("[BG] Executing: ", TXT_CYAN);
    kprint(cmd);
    kprint("\n");
    
    if(my_strcmp(cmd, "cowsay") == 0) {
    } else if(my_strcmp(cmd, "matrix") == 0) {
    } else {
        kprint_color("[BG] Unknown command\n", TXT_RED);
    }
    
    return 0;
}

int run_background_command(const char* cmd, const char* name) {
    if(bg_job_count >= 16) {
        kprint_color("Too many background jobs\n", TXT_RED);
        return -1;
    }
    
    my_strcpy(bg_jobs[bg_job_count].cmd, cmd);
    my_strcpy(bg_jobs[bg_job_count].name, name);
    
    int pid = task_create(bg_command_wrapper, bg_jobs[bg_job_count].cmd, name, 2);
    
    if(pid > 0) {
        bg_jobs[bg_job_count].pid = pid;
        bg_jobs[bg_job_count].active = 1;
        bg_job_count++;
        
        kprint_color("[BG] Started: ", TXT_GREEN);
        kprint(name); kprint(" (PID=");
        kprint_int(pid); kprint(")\n");
        
        scheduler_start();
        return pid;
    }
    return -1;
}

void list_background(void) {
    if(bg_job_count == 0) {
        kprint_color("No background jobs\n", TXT_YELLOW);
        return;
    }
    
    kprint_color("\n=== BACKGROUND JOBS ===\n", TXT_CYAN);
    for(int i = 0; i < bg_job_count; i++) {
        if(bg_jobs[i].active) {
            process_t* p = find_process(bg_jobs[i].pid);
            if(p && p->state != PROC_TERMINATED) {
                kprint_int(i); kprint(": ");
                kprint(bg_jobs[i].name);
                kprint(" (PID="); kprint_int(bg_jobs[i].pid); kprint(")\n");
            } else {
                bg_jobs[i].active = 0;
            }
        }
    }
}

void foreground_command(int id) {
    if(id < 0 || id >= bg_job_count) {
        kprint_color("Invalid job ID\n", TXT_RED);
        return;
    }
    
    if(!bg_jobs[id].active) {
        kprint_color("Job not active\n", TXT_RED);
        return;
    }
    
    process_t* p = find_process(bg_jobs[id].pid);
    if(p && p->state != PROC_TERMINATED) {
        kprint_color("[FG] Bringing to foreground: ", TXT_GREEN);
        kprint(bg_jobs[id].name);
        kprint("\n");
        
        scheduler_enabled = 0;
        current_pid = bg_jobs[id].pid;
        processes[current_pid].state = PROC_RUNNING;
        
        if(processes[current_pid].entry) {
            processes[current_pid].entry(processes[current_pid].arg);
        }
        
        scheduler_enabled = 1;
        bg_jobs[id].active = 0;
    } else {
        kprint_color("Process not running\n", TXT_RED);
        bg_jobs[id].active = 0;
    }
}

void kill_background(int id) {
    if(id < 0 || id >= bg_job_count) {
        kprint_color("Invalid job ID\n", TXT_RED);
        return;
    }
    
    if(!bg_jobs[id].active) {
        kprint_color("Job not active\n", TXT_RED);
        return;
    }
    
    task_kill(bg_jobs[id].pid);
    bg_jobs[id].active = 0;
    kprint_color("[BG] Job killed: ", TXT_RED);
    kprint(bg_jobs[id].name);
    kprint("\n");
}

static int test_counter1 = 0;
static int test_counter2 = 0;
static int test_counter3 = 0;
static mutex_t print_mutex;

int test_worker1(void* arg) {
    while(1) {
        mutex_lock(&print_mutex);
        kprint_color("[WORKER1] ", TXT_GREEN);
        kprint_int(test_counter1++);
        kprint(" (PID="); kprint_int(task_getpid()); kprint(")\n");
        mutex_unlock(&print_mutex);
        task_sleep(500);
    }
    return 0;
}

int test_worker2(void* arg) {
    while(1) {
        mutex_lock(&print_mutex);
        kprint_color("[WORKER2] ", TXT_YELLOW);
        kprint_int(test_counter2++);
        kprint(" (PID="); kprint_int(task_getpid()); kprint(")\n");
        mutex_unlock(&print_mutex);
        task_sleep(700);
    }
    return 0;
}

int test_worker3(void* arg) {
    while(1) {
        mutex_lock(&print_mutex);
        kprint_color("[WORKER3] ", TXT_CYAN);
        kprint_int(test_counter3++);
        kprint(" (PID="); kprint_int(task_getpid()); kprint(")\n");
        mutex_unlock(&print_mutex);
        task_sleep(300);
    }
    return 0;
}

int test_cpu(void* arg) {
    int counter = 0;
    while(1) {
        counter++;
        if(counter % 2000 == 0) {
            kprint_color("[CPU] ", TXT_MAGENTA);
            kprint_int(counter); kprint(" iterations\n");
        }
        task_yield();
    }
    return 0;
}

void test_multitask(void) {
    kprint_color("\n╔══════════════════════════════════════════════════╗\n", TXT_CYAN);
    kprint_color("║              MULTITASKING TEST                  ║\n", TXT_CYAN);
    kprint_color("╚══════════════════════════════════════════════════╝\n", TXT_CYAN);
    
    scheduler_init();
    mutex_init(&print_mutex, "print");
    
    task_create(test_worker1, NULL, "worker1", 1);
    task_create(test_worker2, NULL, "worker2", 1);
    task_create(test_worker3, NULL, "worker3", 1);
    task_create(test_cpu, NULL, "cpu", 0);
    
    kprint_color("\n[INFO] Processes created:\n", TXT_WHITE);
    task_list();
    
    kprint_color("\n[INFO] Starting scheduler...\n", TXT_GREEN);
    scheduler_start();
}