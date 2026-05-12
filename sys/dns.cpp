#include "net.h"
#include "video.h"

uint32_t dns_resolve(netif_t* netif, const char* hostname) {
    (void)netif;
    
    kprint_color("[DNS] Request for: ", TXT_CYAN);
    kprint(hostname);
    kprint("\n");
    
    if(my_strcmp(hostname, "google.com") == 0 ||
       my_strcmp(hostname, "www.google.com") == 0) {
        kprint_color("[DNS] Known host: google.com -> 8.8.8.8\n", TXT_GREEN);
        return 0x08080808;
    }
    
    if(my_strcmp(hostname, "github.com") == 0) {
        kprint_color("[DNS] Known host: github.com -> 140.82.112.3\n", TXT_GREEN);
        return 0x0370528C;
    }
    
    if(my_strcmp(hostname, "example.com") == 0) {
        kprint_color("[DNS] Known host: example.com -> 93.184.216.34\n", TXT_GREEN);
        return 0x22D8B85D; 
    }
    
    kprint_color("[DNS] Unknown host, using default\n", TXT_YELLOW);
    return 0x08080808;
}