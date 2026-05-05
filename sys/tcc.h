#ifndef TCC_H
#define TCC_H

#include <stdint.h>

typedef uint32_t tcc_addr_t;
typedef struct TCCState TCCState;

#define TCC_OUTPUT_MEMORY  1
#define TCC_OUTPUT_EXE     2
#define TCC_OUTPUT_OBJ     3

#define TCC_RELOCATE_AUTO  1
#define TCC_RELOCATE_MANUAL 2

TCCState* tcc_new(void);

void tcc_delete(TCCState* s);

int tcc_compile_string(TCCState* s, const char* str);

int tcc_compile_file(TCCState* s, const char* filename);

void tcc_set_output_type(TCCState* s, int output_type);

int tcc_relocate(TCCState* s, void* ptr);
int tcc_run(TCCState* s, int argc, char** argv);

void* tcc_get_symbol(TCCState* s, const char* name);

void tcc_add_include_path(TCCState* s, const char* path);

void tcc_add_library_path(TCCState* s, const char* path);

#endif