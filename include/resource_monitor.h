#ifndef RESOURCE_MONITOR_H
#define RESOURCE_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

void init_resource_monitor();
void update_resource_usage();
int get_memory_usage();
int get_cpu_usage();
void print_resource_stats();

#ifdef __cplusplus
}
#endif

#endif
