#ifndef EXCEPTION_H
#define EXCEPTION_H

#include <stdint.h>

typedef struct {
    uint32_t eip;  
    uint32_t cs;    
    uint32_t eflags;  
    uint32_t esp;   
    uint32_t ss; 
    uint32_t error;  
    uint32_t vector;  
} exception_frame_t;

typedef struct {
    int (*handler)(exception_frame_t* frame);
    const char* name;
    int recoverable;
} exception_handler_t;

void install_exception_handlers(void);
void register_exception_handler(int vector, int (*handler)(exception_frame_t*));
void unhandled_exception(exception_frame_t* frame);

#endif