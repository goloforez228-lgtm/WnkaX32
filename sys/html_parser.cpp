#include "video.h"

typedef struct {
    char tag[32];
    char attr[256];
    int self_closing;
} html_tag_t;

void html_render_text(const char* text) {
    while(*text) {
        if(*text == '<') {

            while(*text && *text != '>') text++;
            if(*text) text++;
        } else {

            kprint_char(*text);
            text++;
        }
    }
}

void html_parse(const char* html) {
    kprint("\n=== PAGE CONTENT ===\n\n");
    
    int in_tag = 0;
    while(*html) {
        if(*html == '<') {
            in_tag = 1;
        } else if(*html == '>') {
            in_tag = 0;
        } else if(!in_tag) {
            kprint_char(*html);
        }
        html++;
    }
    
    kprint("\n\n=== END OF PAGE ===\n");
}