#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>

typedef struct {
    uint32_t last_tick;
    uint32_t timeout;
    int enabled;
    const char* task_name;
} watchdog_t;

void watchdog_init(void);
void watchdog_start(const char* task_name, uint32_t timeout_ms);
void watchdog_kick(void);
void watchdog_check(void);
void watchdog_reset(void);

#endif