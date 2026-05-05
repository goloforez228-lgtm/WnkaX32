#ifndef SOUNDS_H
#define SOUNDS_H

#include <stdint.h>

void play_startup_sound(void);
void play_shutdown_sound(void);
void play_error_sound(void);
void play_success_sound(void);
void play_click_sound(void);
void play_info_sound(void);

#endif