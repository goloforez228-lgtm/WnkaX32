#include "graph.h"
#include "watchdog.h"
#include "video.h"

static watchdog_t watchdogs[16];
static int watchdog_count = 0;
static uint32_t last_watchdog_check = 0;

void watchdog_init(void) {
    for(int i = 0; i < 16; i++) {
        watchdogs[i].enabled = 0;
    }
    watchdog_count = 0;
    kprint("[WATCHDOG] Initialized\n");
}

void watchdog_start(const char* task_name, uint32_t timeout_ms) {
    if(watchdog_count >= 16) return;
    
    watchdogs[watchdog_count].last_tick = seconds * 1000;
    watchdogs[watchdog_count].timeout = timeout_ms;
    watchdogs[watchdog_count].enabled = 1;
    watchdogs[watchdog_count].task_name = task_name;
    watchdog_count++;
    
    kprint("[WATCHDOG] Watching: ");
    kprint(task_name);
    kprint(" (");
    kprint_int(timeout_ms);
    kprint("ms)\n");
}

void watchdog_kick(void) {
    if(watchdog_count > 0) {
        watchdogs[0].last_tick = seconds * 1000;
    }
}

void watchdog_check(void) {
    uint32_t now = seconds * 1000;
    
    for(int i = 0; i < watchdog_count; i++) {
        if(watchdogs[i].enabled) {
            if(now - watchdogs[i].last_tick > watchdogs[i].timeout) {
                kprint_color("\n  WATCHDOG TIMEOUT: ", TXT_RED);
                kprint(watchdogs[i].task_name);
                kprint("\n");
                
                kprint_color("Attempting to recover...\n", TXT_YELLOW);
                watchdogs[i].last_tick = now;
            }
        }
    }
}