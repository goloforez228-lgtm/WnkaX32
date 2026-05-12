#include "sounds.h"
#include "kernel_stubs.h"
#include "video.h"
#include <stdint.h>


static void beep(uint32_t freq, uint32_t duration_ms) {
    if(freq == 0) return;
    
    uint8_t tmp = inb(0x61);
    outb(0x61, tmp | 3);
    
    uint32_t div = 1193180 / freq;
    outb(0x43, 0xB6);
    outb(0x42, div & 0xFF);
    outb(0x42, (div >> 8) & 0xFF);
    
    for(volatile uint32_t i = 0; i < duration_ms * 30000; i++);
    
    outb(0x61, tmp & 0xFC);
}

static void delay_ms(uint32_t ms) {
    for(volatile uint32_t i = 0; i < ms * 30000; i++);
}


void play_startup_sound(void) {
    beep(262, 300);   
    delay_ms(100);
    beep(294, 300);   
    delay_ms(100);
    beep(330, 300);   
    delay_ms(100);
    beep(349, 300); 
    delay_ms(100);
    beep(392, 300); 
    delay_ms(100);
    beep(440, 300);  
    delay_ms(100);
    beep(494, 300);  
    delay_ms(100);
    beep(523, 400);  
    delay_ms(150);
    beep(659, 350);  
    delay_ms(120);
    beep(784, 350);  
    delay_ms(120);
    beep(880, 400);   
    delay_ms(150);
    beep(1047, 500); 
    delay_ms(200);
    beep(523, 300);
    delay_ms(100);
    beep(392, 300);
    delay_ms(100);
    beep(330, 400);
    delay_ms(150);
    beep(262, 600);
    delay_ms(250);
    beep(523, 200);
    delay_ms(50);
    beep(659, 200);
    delay_ms(50);
    beep(784, 300);
    delay_ms(100);
    beep(1047, 400);
}

void play_shutdown_sound(void) {
    beep(880, 150);
    delay_ms(500);
    beep(659, 180);
    delay_ms(100);
    beep(784, 150);  
    delay_ms(80);
    beep(587, 180);  
    delay_ms(100);
    beep(523, 300);
}

void play_error_sound(void) {
    beep(880, 350);
    delay_ms(150);
    beep(440, 300);
}

void play_success_sound(void) {
    beep(523, 150);
    delay_ms(80);
    beep(659, 150);
    delay_ms(80);
    beep(784, 300);
}

void play_click_sound(void) {
    beep(1200, 40);
}

void play_info_sound(void) {
    beep(880, 100);
    delay_ms(80);
    beep(880, 100);
}

void play_warning_sound(void) {
    beep(500, 400);
    delay_ms(150);
    beep(500, 400);
}