#include "video.h"
#include "graph.h"
#include "screensaver.h"
#include <stdint.h>

extern "C" {
    void play_sound(uint32_t freq);
    void nosound(void);
}

static uint32_t rand_state = 1;

static void srand(uint32_t seed) {
    rand_state = seed;
}

static uint32_t rand() {
    rand_state = rand_state * 1103515245 + 12345;
    return (rand_state / 65536) % 32768;
}

#ifndef LIGHT_GRAY
#define LIGHT_GRAY 0x07
#endif

void start_screensaver() {
    clear_screen_bg(BLACK);
    
    int x = 40, y = 12;
    int dx = 1, dy = 1;
    int color = 0;
    int frame = 0;
    
    int running = 1;
    while(running) {
        put_pixel(x, y, BLACK, TXT_WHITE, ' ');
        
        x += dx;
        y += dy;
        
        if(x <= 0 || x >= 79) {
            dx = -dx;
            play_sound(440);
            for(volatile int i = 0; i < 50000; i++);
            nosound();
        }
        if(y <= 0 || y >= 24) {
            dy = -dy;
            play_sound(880);
            for(volatile int i = 0; i < 50000; i++);
            nosound();
        }
        
        color = (color + 1) % 16;
        
        put_pixel(x, y, color, TXT_WHITE, '*');
        
        if(inb(0x64) & 1) {
            uint8_t key = inb(0x60);
            if(key == 0x01) {
                running = 0;
                nosound();
            }
        }
        
        move_cursor(79, 24);
        for(volatile int i = 0; i < 100000; i++);
    }
    
    clear_screen();
}

void starfield_screensaver() {
    clear_screen_bg(BLACK);
    
    struct Star {
        int x, y;
        int speed;
    };
    
    Star stars[50];
    
    for(int i = 0; i < 50; i++) {
        stars[i].x = rand() % 80;
        stars[i].y = rand() % 25;
        stars[i].speed = 1 + (rand() % 3);
    }
    
    int running = 1;
    while(running) {
        for(int i = 0; i < 50; i++) {
            put_pixel(stars[i].x, stars[i].y, BLACK, TXT_WHITE, ' ');
        }
        
        for(int i = 0; i < 50; i++) {
            stars[i].y += stars[i].speed;
            
            if(stars[i].y >= 25) {
                stars[i].y = 0;
                stars[i].x = rand() % 80;
                stars[i].speed = 1 + (rand() % 3);
            }
            
            uint8_t star_color;
            if(stars[i].speed == 1) {
                star_color = GRAY;
            } else if(stars[i].speed == 2) {
                star_color = LIGHT_GRAY;
            } else {
                star_color = WHITE;
            }
            
            put_pixel(stars[i].x, stars[i].y, star_color, TXT_BLACK, '*');
        }
        
        if(inb(0x64) & 1) {
            uint8_t key = inb(0x60);
            if(key == 0x01) {
                running = 0;
            }
        }
        
        move_cursor(79, 24);
        for(volatile int i = 0; i < 10000000; i++);
    }
    
    clear_screen();
}