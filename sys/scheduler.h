#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

#define MAX_TASKS 16
#define TASK_NAME_LEN 32
#define CMD_LEN 256

#define TASK_RUNNING 0
#define TASK_SLEEPING 1
#define TASK_WAITING 2
#define TASK_DONE 3
#define TASK_ERROR 4

typedef struct {
    int id;
    char name[TASK_NAME_LEN];
    char command[CMD_LEN];
    int state;
    uint32_t start_time;
    uint32_t end_time;
    uint32_t deadline;
    int pid; 
    int exit_code;
    int background; 
} Task;

typedef struct {
    int hour;
    int minute;
    int second;
} Time;
extern "C" void process_command(char* cmd, int& ptr);

Time get_time(void);
void set_time(Time t);
uint32_t time_to_seconds(Time t);
Time seconds_to_time(uint32_t seconds);
void print_time(Time t);
void get_date(uint8_t* day, uint8_t* month, uint16_t* year);
void print_full_time(Time t);


#endif