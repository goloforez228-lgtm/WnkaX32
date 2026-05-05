#ifndef RECOVERY_H
#define RECOVERY_H

#include <stdint.h>

typedef struct {
    uint32_t eip;
    uint32_t esp;
    uint32_t ebp;
    uint32_t saved_regs[8];
} recovery_point_t;

void recovery_save(recovery_point_t* point);
void recovery_restore(recovery_point_t* point);
void recovery_init(void);

#define RECOVERY_POINT() \
    recovery_point_t __point; \
    recovery_save(&__point); \
    if(setjmp((jmp_buf)&__point) == 0) {

#define RECOVERY_RESTORE() \
    } else { \
        kprint_color("[RECOVERY] Restored from checkpoint\n", TXT_GREEN); \
    }

#endif