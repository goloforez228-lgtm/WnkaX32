#ifndef WNVESA_H
#define WNVESA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void wn_vesa_init(void);
void wn_vesa_rect(int x, int y, int w, int h, uint16_t color);
void wn_vesa_clear(uint16_t color);
void wn_demo(void);

#ifdef __cplusplus
}
#endif

#endif