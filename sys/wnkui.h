#ifndef WNKUI_H
#define WNKUI_H

#include <stdint.h>

#define WNKCUI_CLASSIC   0
#define WNKCUI_WIN31     1
#define WNKCUI_XP        2

void wnkcui_init(int style);
void wnkcui_desktop(void);
void wnkcui_window(int x, int y, int w, int h, const char* title);
void wnkcui_button(int x, int y, int w, int h, const char* text);
void wnkcui_menu(void);
void wnkcui_update(void);
void wnkcui_exit(void);
void wnkcui_run(void);

#endif