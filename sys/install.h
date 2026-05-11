#ifndef INSTALL_H
#define INSTALL_H

extern int select_partition(int index);
extern int current_partition;
void wnk_install(void);
int is_system_installed(void);

#endif