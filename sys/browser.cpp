#include "video.h"
#include "net.h"

#define BROWSER_LINES 24
#define BROWSER_COLS 80

static char browser_buffer[16384];
static uint32_t browser_pos = 0;
static int browser_scroll = 0;

void browser_clear(void) {
    browser_pos = 0;
    browser_scroll = 0;
    clear_screen();
}

void browser_add_text(const char* text) {
    while(*text && browser_pos < sizeof(browser_buffer) - 1) {
        browser_buffer[browser_pos++] = *text++;
    }
}

void browser_render(void) {
    clear_screen();
    
    kprint_at("                   WNKA TEXT BROWSER v0.1                   ", 0, 1, 0x1F);
    int line = 0;
    int col = 0;
    int pos = browser_scroll * 80;
    
    while(pos < browser_pos && line < 20) {
        char c = browser_buffer[pos++];
        
        if(c == '\n') {
            line++;
            col = 0;
        } else if(col < 79) {
            kprint_at(&c, 1 + col, 4 + line, 0x07);
            col++;
        }
        
        if(col >= 79) {
            line++;
            col = 0;
        }
    }
    kprint_at("[↑/↓] Scroll  [ESC] Exit", 25, 24, 0x70);
}

int browse_url(netif_t* netif, const char* url) {
    browser_clear();
    
    kprint("[Browser] Loading ");
    kprint(url);
    kprint("...\n");
    
    browser_add_text("<html><body><h1>Hello, WNKA!</h1>");
    browser_add_text("<p>This is a test page.</p>");
    browser_add_text("<p>Your browser works!</p>");
    browser_add_text("</body></html>");
    
    browser_render();
    
    int running = 1;
    while(running) {
        if(inb(0x64) & 1) {
            uint8_t key = inb(0x60);
            if(key < 0x80) {
                if(key == 0x48 && browser_scroll > 0) {  
                    browser_scroll--;
                    browser_render();
                }
                else if(key == 0x50) { 
                    browser_scroll++;
                    browser_render();
                }
                else if(key == 0x01) { 
                    running = 0;
                }
            }
            while(inb(0x64) & 1) inb(0x60);
        }
    }
    
    clear_screen();
    return 0;
}