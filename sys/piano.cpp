#include "video.h"
#include "graph.h"
#include "kernel_stubs.h"
#include <stdint.h>

#define NULL 0

#define TXT_BLACK    0x00
#define TXT_BLUE     0x01
#define TXT_GREEN    0x02
#define TXT_CYAN     0x03
#define TXT_RED      0x04
#define TXT_MAGENTA  0x05
#define TXT_BROWN    0x06
#define TXT_LGRAY    0x07
#define TXT_DGRAY    0x08
#define TXT_LBLUE    0x09
#define TXT_LGREEN   0x0A
#define TXT_LCYAN    0x0B
#define TXT_LRED     0x0C
#define TXT_LMAGENTA 0x0D
#define TXT_YELLOW   0x0E
#define TXT_WHITE    0x0F
#define BLACK 0x00
#define BLUE  0x01
#define GREEN 0x02

extern "C" void play_sound(uint32_t nFrequence);
extern "C" void nosound(void);

static const uint16_t note_freq[] = {
    0,    
    130,    
    138,  
    146,   
    155,    
    164,    
    174, 
    185,    
    196,    
    207,   
    220,   
    233,    
    246     
};

static int recording = 0;
static int record_buffer[100][2];
static int record_count = 0;

static void delay_ms(int ms) {
    for(volatile int i = 0; i < ms * 200000; i++);
}

static void play_note(int note, int duration_ms) {
    if(note > 0 && note <= 12 && note_freq[note] != 0) {
        play_sound(note_freq[note]);
    } else {
        nosound();
    }
    
    delay_ms(duration_ms);
    nosound();
}

static void draw_keyboard(void) {
    const char* white_keys[] = {"Z", "X", "C", "V", "B", "N", "M"};
    const char* white_notes[] = {"C2", "D2", "E2", "F2", "G2", "A2", "B2"};
    
    for(int i = 0; i < 7; i++) {
        for(int y = 0; y < 3; y++) {
            kprint_at("#########", 3 + i * 10, 17 + y, (BLUE << 4) | TXT_WHITE);
        }
        kprint_at(white_keys[i], 5 + i * 10, 18, (BLACK << 4) | TXT_BLACK);
        kprint_at(white_notes[i], 5 + i * 10, 19, (BLACK << 4) | TXT_BLUE);
    }
    
    kprint_at("S", 12, 17, (BLACK << 4) | TXT_YELLOW);
    kprint_at("D", 22, 17, (BLACK << 4) | TXT_YELLOW);
    kprint_at("G", 42, 17, (BLACK << 4) | TXT_YELLOW);
    kprint_at("H", 52, 17, (BLACK << 4) | TXT_YELLOW);
    kprint_at("J", 62, 17, (BLACK << 4) | TXT_YELLOW);
    
    kprint_at("C#2", 10, 18, (BLACK << 4) | TXT_BLACK);
    kprint_at("D#2", 20, 18, (BLACK << 4) | TXT_BLACK);
    kprint_at("F#2", 40, 18, (BLACK << 4) | TXT_BLACK);
    kprint_at("G#2", 50, 18, (BLACK << 4) | TXT_BLACK);
    kprint_at("A#2", 60, 18, (BLACK << 4) | TXT_BLACK);
}

static void highlight_key(int x, int y, int color) {
    kprint_at(">>>", x - 2, y, (color << 4) | TXT_YELLOW);
    delay_ms(100);
    kprint_at("   ", x - 2, y, (BLUE << 4) | TXT_WHITE);
}

static void show_status(const char* msg, int line, int color) {
    kprint_at(msg, 65, line, (BLACK << 4) | color);
}

void piano_main(void) {
    clear_screen();
    
    kprint_color("=== PIANO v2.0 (PC Speaker) ===\n", TXT_CYAN);
    kprint_color("White keys: Z X C V B N M\n", TXT_WHITE);
    kprint_color("Black keys: S D   G H J\n", TXT_WHITE);
    kprint_color("Controls: [R]ecord [P]lay [C]lear [ESC]-exit\n\n", TXT_YELLOW);
    
    draw_keyboard();
    
    while(1) {
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            
            if(sc == 0x01) break;
            
            int note = 0;
            int is_sharp = 0;
            int x_pos = 0;
            
            switch(sc) {
                case 0x2C: note = 1; is_sharp = 0; x_pos = 5; break;   
                case 0x1F: note = 2; is_sharp = 1; x_pos = 12; break; 
                case 0x2D: note = 3; is_sharp = 0; x_pos = 15; break; 
                case 0x20: note = 4; is_sharp = 1; x_pos = 22; break;  
                case 0x2E: note = 5; is_sharp = 0; x_pos = 25; break;  
                case 0x2F: note = 6; is_sharp = 0; x_pos = 35; break;  
                case 0x22: note = 7; is_sharp = 1; x_pos = 42; break; 
                case 0x30: note = 8; is_sharp = 0; x_pos = 45; break; 
                case 0x23: note = 9; is_sharp = 1; x_pos = 52; break;  
                case 0x31: note = 10; is_sharp = 0; x_pos = 55; break; 
                case 0x24: note = 11; is_sharp = 1; x_pos = 62; break; 
                case 0x32: note = 12; is_sharp = 0; x_pos = 65; break; 
                default: break;
            }
            
            if(note > 0) {
                play_note(note, 200);
                
                if(is_sharp) {
                    highlight_key(x_pos, 17, TXT_RED);
                } else {
                    highlight_key(x_pos, 17, TXT_GREEN);
                }
                
                if(recording && record_count < 100) {
                    record_buffer[record_count][0] = note;
                    record_buffer[record_count][1] = 200;
                    record_count++;
                    show_status("REC", 0, TXT_RED);
                }
            }
            
            if(sc == 0x13) {
                recording = !recording;
                if(recording) {
                    record_count = 0;
                    show_status("RECORDING...", 0, TXT_RED);
                    play_note(5, 100);
                } else {
                    show_status("            ", 0, TXT_BLACK);
                    play_note(8, 100);
                }
            }
            
            if(sc == 0x19) {
                show_status("PLAYING...", 1, TXT_GREEN);
                for(int i = 0; i < record_count; i++) {
                    play_note(record_buffer[i][0], record_buffer[i][1]);
                }
                show_status("          ", 1, TXT_BLACK);
            }
            
            if(sc == 0x1E) {
                record_count = 0;
                show_status("CLEARED", 2, TXT_YELLOW);
                play_note(12, 100);
            }
        }
        
        for(volatile int i = 0; i < 500; i++);
    }
    
    clear_screen();
    nosound();
    kprint_color("Piano exited\n", TXT_GREEN);
}