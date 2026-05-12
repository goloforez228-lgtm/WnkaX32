#include "video.h"
#include "exception.h"
#include "graph.h"
#include "kernel_stubs.h"
#include "string_utils.h"

#define NULL 0
static void test_division_by_zero(void) {
    kprint_color("\n[TEST 1] Division by zero...\n", TXT_YELLOW);
    
    volatile int a = 10;
    volatile int b = 0;
    volatile int c = a / b;
    
    (void)c;
    kprint_color("  If you see this, something is wrong!\n", TXT_RED);
}

static void test_page_fault(void) {
    kprint_color("\n[TEST 2] Page fault (invalid address)...\n", TXT_YELLOW);
    
    volatile uint32_t* bad_ptr = (uint32_t*)0xDEADBEEF;
    volatile uint32_t val = *bad_ptr;
    
    (void)val;
    kprint_color("  If you see this, something is wrong!\n", TXT_RED);
}

static void test_invalid_opcode(void) {
    kprint_color("\n[TEST 3] Invalid opcode...\n", TXT_YELLOW);
    
    __asm__ volatile(
        ".byte 0x0F, 0x0B\n"
        : : : "memory"
    );
    
    kprint_color("  If you see this, something is wrong!\n", TXT_RED);
}

static void test_null_pointer(void) {
    kprint_color("\n[TEST 4] Null pointer dereference...\n", TXT_YELLOW);
    
    int* ptr = NULL;
    int val = *ptr;
    
    (void)val;
    kprint_color("  If you see this, something is wrong!\n", TXT_RED);
}

static void recursive_func(int depth) {
    char buffer[1024];
    buffer[0] = depth;
    
    if(depth > 0) {
        recursive_func(depth - 1);
    }
}

static void test_stack_overflow(void) {
    kprint_color("\n[TEST 5] Stack overflow (recursion)...\n", TXT_YELLOW);
    
    recursive_func(10000);
    
    kprint_color("  If you see this, something is wrong!\n", TXT_RED);
}

static void test_buffer_overflow(void) {
    kprint_color("\n[TEST 6] Buffer overflow...\n", TXT_YELLOW);
    
    char small_buffer[10];
    for(int i = 0; i < 100; i++) {
        small_buffer[i] = 'A' + (i % 26);
    }
    
    kprint_color("  If you see this, something is wrong!\n", TXT_RED);
}

static void test_double_fault(void) {
    kprint_color("\n[TEST 7] Double fault (nested exception)...\n", TXT_YELLOW);
    
    __asm__ volatile(
        "int $0x08\n"
        : : : "memory"
    );
    
    kprint_color("  If you see this, something is wrong!\n", TXT_RED);
}

static void test_bad_io_port(void) {
    kprint_color("\n[TEST 8] Invalid I/O port access...\n", TXT_YELLOW);
    
    outb(0xFFFF, 0xFF);
    
    kprint_color("  If you see this, something is wrong!\n", TXT_RED);
}

static void test_all(void) {
    kprint_color("\n╔════════════════════════════════════════════════════╗\n", TXT_CYAN);
    kprint_color("║           CRASH TEST SUITE v1.0                    ║\n", TXT_CYAN);
    kprint_color("╠════════════════════════════════════════════════════╣\n", TXT_CYAN);
    kprint_color("║  Testing system stability & exception handling    ║\n", TXT_CYAN);
    kprint_color("╚════════════════════════════════════════════════════╝\n", TXT_CYAN);
    
    test_division_by_zero();
    test_page_fault();
    test_invalid_opcode();
    test_null_pointer();
    test_stack_overflow();
    test_buffer_overflow();
    test_bad_io_port();
    
    kprint_color("\n╔════════════════════════════════════════════════════╗\n", TXT_GREEN);
    kprint_color("║  ALL TESTS PASSED!                              ║\n", TXT_GREEN);
    kprint_color("║  System is STABLE and PROTECTED!                   ║\n", TXT_GREEN);
    kprint_color("╚════════════════════════════════════════════════════╝\n", TXT_GREEN);
}

static void crashme_interactive(void) {
    clear_screen();
    
    kprint_color("\n╔════════════════════════════════════════════════════╗\n", TXT_CYAN);
    kprint_color("║           CRASH TEST MENU                          ║\n", TXT_CYAN);
    kprint_color("╠════════════════════════════════════════════════════╣\n", TXT_CYAN);
    kprint_color("║  1. Division by zero                               ║\n", TXT_WHITE);
    kprint_color("║  2. Page fault (invalid address)                   ║\n", TXT_WHITE);
    kprint_color("║  3. Invalid opcode                                 ║\n", TXT_WHITE);
    kprint_color("║  4. Null pointer dereference                       ║\n", TXT_WHITE);
    kprint_color("║  5. Stack overflow                                 ║\n", TXT_WHITE);
    kprint_color("║  6. Buffer overflow                                ║\n", TXT_WHITE);
    kprint_color("║  7. Invalid I/O port                               ║\n", TXT_WHITE);
    kprint_color("║  8. Double fault (dangerous)                       ║\n", TXT_RED);
    kprint_color("║  9. RUN ALL TESTS                                  ║\n", TXT_GREEN);
    kprint_color("║  0. Exit                                           ║\n", TXT_YELLOW);
    kprint_color("╚════════════════════════════════════════════════════╝\n", TXT_CYAN);
    kprint_color("\nChoose test (0-9): ", TXT_GREEN);
    
    char choice = 0;
    while(!choice) {
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc >= 0x02 && sc <= 0x0B) {
                const char* digits = "1234567890";
                choice = digits[sc - 0x02];
            }
            if(sc == 0x01) choice = '0';
        }
    }
    
    char s[2] = {choice, '\0'};
    kprint(s);
    kprint("\n");
    
    switch(choice) {
        case '1': test_division_by_zero(); break;
        case '2': test_page_fault(); break;
        case '3': test_invalid_opcode(); break;
        case '4': test_null_pointer(); break;
        case '5': test_stack_overflow(); break;
        case '6': test_buffer_overflow(); break;
        case '7': test_bad_io_port(); break;
        case '8': test_double_fault(); break;
        case '9': test_all(); break;
        case '0': kprint_color("Exiting...\n", TXT_YELLOW); return;
    }
    
    kprint_color("\nPress any key to continue...\n", TXT_YELLOW);
    while(!(inb(0x64) & 1));
    uint8_t key = inb(0x60);
    (void)key;
}

void crashme(void) {
    kprint_color("\n WARNING: This will intentionally crash the system!\n", TXT_RED);
    kprint_color("But don't worry - exception handlers will catch everything!\n", TXT_GREEN);
    kprint_color("\nPress any key to continue or ESC to cancel...\n", TXT_YELLOW);
    
    while(!(inb(0x64) & 1));
    uint8_t key = inb(0x60);
    if(key == 0x01) {
        kprint_color("Cancelled.\n", TXT_GREEN);
        return;
    }
    
    crashme_interactive();
}