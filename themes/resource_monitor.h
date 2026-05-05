#ifndef RESOURCE_MONITOR_H
#define RESOURCE_MONITOR_H

#include <stdint.h>

void show_resource_monitor(void);
void draw_progress_bar(int x, int y, int width, int percent, uint8_t color);
void update_stats(void);

#endif