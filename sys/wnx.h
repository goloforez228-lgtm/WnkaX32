#ifndef WNX_H
#define WNX_H

#include <stdint.h>

#define WNX_MAGIC 0x31584E57

#define WNX_SECTION_CODE  0x01
#define WNX_SECTION_DATA  0x02
#define WNX_SECTION_STACK 0x03
#define WNX_SECTION_ENTRY 0x04

#define WNX_FLAG_GUI      0x01
#define WNX_FLAG_CONSOLE  0x02
#define WNX_FLAG_CRYPT    0x04

typedef struct {
    uint32_t magic;     
    uint16_t version;    
    uint16_t flags;      
    uint32_t entry_point; 
    uint32_t code_size;   
    uint32_t data_size;    
    uint32_t stack_size;  
    uint32_t heap_size;   
    char     name[32];   
    char     author[32];  
    char     description[128]; 
    uint32_t icon;   
    uint32_t checksum;     
    uint8_t  reserved[32];  
} __attribute__((packed)) wnx_header_t;

typedef struct {
    wnx_header_t header;
    uint8_t* code;
    uint8_t* data;
    uint8_t* stack;
    uint32_t stack_ptr;
    uint32_t entry;
    int running;
    char args[256];
} wnx_context_t;

int  wnx_load(const char* filename, wnx_context_t* ctx);
int  wnx_execute(wnx_context_t* ctx);
void wnx_run(wnx_context_t* ctx);
void wnx_stop(wnx_context_t* ctx);
int  wnx_list_files(void);
int  wnx_create_stub(const char* filename, const char* name);

void wnx_browser(void);
int  wnx_get_icon(uint32_t icon_id);

#endif