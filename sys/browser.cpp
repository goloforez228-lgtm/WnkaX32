#include "video.h"
#include "net.h"
#include "http.h"

int browse_url(netif_t* netif, const char* url) {
    if(!netif) return -1;
    
    clear_screen();
    kprint_color("=== WNKA BROWSER ===\n", TXT_CYAN);
    kprint("URL: ");
    kprint(url);
    kprint("\n\n");
    
    kprint_color("[Browser] Opening: ", TXT_YELLOW);
    kprint(url);
    kprint("\n");
    
    kprint("\n=== PAGE CONTENT ===\n");
    kprint("This is a simple browser.\n");
    kprint("Full HTTP support coming soon!\n");
    kprint("\nRequested URL: ");
    kprint(url);
    kprint("\n");
    
    kprint("\nPress ESC to exit\n");
    
    while(1) {
        if(inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if(sc == 0x01) break;
        }
        for(volatile int i = 0; i < 1000; i++);
    }
    
    clear_screen();
    return 0;
}