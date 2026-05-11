#include "graph.h"
#include "exception.h"
#include "video.h"

static exception_handler_t handlers[32];

static const char* exception_names[] = {
    "Division by Zero",       
    "Debug",            
    "Non-Maskable Interrupt",   
    "Breakpoint",            
    "Overflow",               
    "Bounds Check",             
    "Invalid Opcode",         
    "FPU Not Available",       
    "Double Fault",            
    "FPU Segment Overrun",      
    "Invalid TSS",             
    "Segment Not Present",   
    "Stack Fault",             
    "General Protection",      
    "Page Fault",            
    "Reserved",               
    "FPU Error",            
    "Alignment Check",       
    "Machine Check",         
    "SIMD Error"                  
};

static int default_handler(exception_frame_t* frame) {
    kprint_color("\n╔════════════════════════════════════════╗\n", TXT_RED);
    kprint_color("║           EXCEPTION CAUGHT!            ║\n", TXT_RED);
    kprint_color("╠════════════════════════════════════════╣\n", TXT_RED);
    
    char buf[64];
    const char* name = (frame->vector < 20) ? exception_names[frame->vector] : "Unknown";
    kprint("║ ");
    kprint(name);
    kprint("\n");
    
    kprint("║ EIP: 0x");
    kprint_hex32(frame->eip);
    kprint("\n");
    
    kprint("║ ESP: 0x");
    kprint_hex32(frame->esp);
    kprint("\n");
    
    kprint("║ Error: 0x");
    kprint_hex32(frame->error);
    kprint("\n");
    
    kprint_color("╠════════════════════════════════════════╣\n", TXT_RED);
    kprint_color("║  System will attempt to continue...   ║\n", TXT_YELLOW);
    kprint_color("╚════════════════════════════════════════╝\n", TXT_RED);
    
    return 1;
}

void register_exception_handler(int vector, int (*handler)(exception_frame_t*)) {
    if(vector >= 0 && vector < 32) {
        handlers[vector].handler = handler;
    }
}

extern "C" void exception_stub(void);

__attribute__((naked))
void exception_stub(void) {
    __asm__ volatile(
        "pusha\n"
        "mov %ds, %ax\n"
        "push %eax\n"
        "mov $0x10, %ax\n"
        "mov %ax, %ds\n"
        "mov %ax, %es\n"
        "mov %ax, %fs\n"
        "mov %ax, %gs\n"
        "call exception_handler_wrapper\n"
        "pop %eax\n"
        "mov %ax, %ds\n"
        "mov %ax, %es\n"
        "mov %ax, %fs\n"
        "mov %ax, %gs\n"
        "popa\n"
        "add $8, %esp\n"
        "iret\n"
    );
}

extern "C" void exception_handler_wrapper(exception_frame_t* frame) {
    int vector = frame->vector;
    
    if(vector >= 0 && vector < 32 && handlers[vector].handler) {
        if(handlers[vector].handler(frame)) {
            return;
        }
    }
    
    unhandled_exception(frame);
}

void unhandled_exception(exception_frame_t* frame) {
    kprint_color("\n!!! UNHANDLED EXCEPTION !!!\n", TXT_RED);
    kprint("Vector: "); kprint_int(frame->vector); kprint("\n");
    
    kprint_color("Attempting to recover...\n", TXT_YELLOW);
    
    __asm__ volatile("mov %0, %%esp" : : "r"(frame->esp + 8));
    
    frame->eip += 1;
}

void install_exception_handlers(void) {
    for(int i = 0; i < 32; i++) {
        handlers[i].handler = default_handler;
        handlers[i].name = (i < 20) ? exception_names[i] : "Unknown";
        handlers[i].recoverable = (i != 8 && i != 18);
    }
    
    kprint("[EXCEPTION] Exception handlers installed\n");
}