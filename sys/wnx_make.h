#ifndef WNX_MAKE_H
#define WNX_MAKE_H

#include <stdint.h>

typedef struct {
    const char* name;   
    const char* author; 
    const char* description; 
    uint32_t icon;     
    uint32_t flags;    
    uint32_t stack_size;   
    uint32_t heap_size;  
    const char* source;    
    int source_size;      
} wnx_build_config_t;

int wnx_create_from_source(const char* source_path, const char* output_name);
int wnx_create_from_code(const char* code, int code_size, const char* output_name);
int wnx_create_ex(const wnx_build_config_t* config, const char* output_name);
int wnx_create_template(const char* name, const char* output_name);
int wnx_compile(const char* script_path, const char* output_name);
void wnx_info(const char* filename);
int wnx_extract_source(const char* filename, const char* output_path);
int wnx_list_all(void);

#endif