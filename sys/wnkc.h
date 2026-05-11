#ifndef WNKC_H
#define WNKC_H

#include <stdint.h>

#define WNKC_MAX_VARS 64
#define WNKC_MAX_NAME 32
#define WNKC_MAX_STR 256
#define WNKC_MAX_LINE 512

#define WNKC_TYPE_NUMBER 0
#define WNKC_TYPE_STRING 1

typedef struct {
    char name[WNKC_MAX_NAME];
    int type;
    int num_value;
    char str_value[WNKC_MAX_STR];
} wnc_var_t;

typedef struct {
    wnc_var_t vars[WNKC_MAX_VARS];
    int var_count;
    int line;
    int error;
} wnc_state_t;

int wnc_execute(const char* code);
int wnc_execute_file(const char* filename);
void wnc_init(void);
void wnc_set_dir(uint16_t dir);

#endif