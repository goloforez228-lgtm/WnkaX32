#include "http.h"
#include "video.h"
#include "net.h"
#include "ata.h"
#include "e1000.h"
#include "string_utils.h"
#include <stdint.h>

#define NULL 0
#define HTTP_BUFFER_SIZE 8192
#define HTTP_TIMEOUT_MS 5000

#ifndef TXT_RED
#define TXT_RED     0x04
#define TXT_GREEN   0x02
#define TXT_YELLOW  0x0E
#define TXT_CYAN    0x03
#define TXT_WHITE   0x0F
#endif

static uint16_t current_dir_sector = 100;
extern netif_t e1000_netif;

void http_parse_url(const char* url, char* host, int* port, char* path) {
    *port = 80;
    
    const char* p = url;
    if(p[0] == 'h' && p[1] == 't' && p[2] == 't' && p[3] == 'p' && 
       p[4] == ':' && p[5] == '/' && p[6] == '/') {
        p += 7;
    }
    
    int i = 0;
    while(*p && *p != '/' && *p != ':' && i < 255) {
        host[i++] = *p++;
    }
    host[i] = '\0';
    
    if(*p == ':') {
        p++;
        *port = 0;
        while(*p >= '0' && *p <= '9') {
            *port = *port * 10 + (*p - '0');
            p++;
        }
    }
    
    if(*p == '/') {
        i = 0;
        while(*p && i < 255) {
            path[i++] = *p++;
        }
        path[i] = '\0';
    } else {
        path[0] = '/';
        path[1] = '\0';
    }
}

static int http_resolve(const char* host, ip_addr_t* ip) {
    if(dns_resolve(&e1000_netif, host) == 0) {
        return 0;
    }
    
    if(my_strcmp(host, "raw.githubusercontent.com") == 0) {
        ip->addr[0] = 185;
        ip->addr[1] = 199;
        ip->addr[2] = 108;
        ip->addr[3] = 153;
        return 0;
    }
    if(my_strcmp(host, "goloforez228-lgtm.github.io") == 0) {
        ip->addr[0] = 185;
        ip->addr[1] = 199;
        ip->addr[2] = 108;
        ip->addr[3] = 153;
        return 0;
    }
    
    kprint_color("[DNS] Failed to resolve: ", TXT_RED);
    kprint(host);
    kprint("\n");
    return -1;
}

static int tcp_connect(ip_addr_t ip, int port) {
    kprint("[TCP] Connecting to ");
    kprint_int(ip.addr[0]); kprint(".");
    kprint_int(ip.addr[1]); kprint(".");
    kprint_int(ip.addr[2]); kprint(".");
    kprint_int(ip.addr[3]);
    kprint(":");
    kprint_int(port);
    kprint("\n");
    
    return 0;
}

int http_get_real(const char* url, uint8_t* buffer, uint32_t max_size) {
    kprint("[HTTP] GET ");
    kprint(url);
    kprint("\n");
    
    // Пока возвращаем заглушку
    const char* response = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n\r\n"
        "<html><body>"
        "<h1>WNKA Browser</h1>"
        "<p>This is a test page.</p>"
        "<p>Full HTTP support coming soon!</p>"
        "</body></html>";
    
    int len = 0;
    while(response[len] && len < (int)max_size - 1) {
        buffer[len] = response[len];
        len++;
    }
    buffer[len] = '\0';
    return len;
}

int http_get(const char* url, uint8_t* buffer, uint32_t max_size) {
    return http_get_real(url, buffer, max_size);
}

int http_download(const char* url, const char* filename) {
    uint8_t buffer[HTTP_BUFFER_SIZE];
    int size = http_get_real(url, buffer, HTTP_BUFFER_SIZE - 1);
    
    if(size <= 0) {
        kprint_color("[HTTP] Download failed\n", TXT_RED);
        return -1;
    }
    
    uint8_t* body = buffer;
    int body_len = size;
    
    for(int i = 0; i < size - 3; i++) {
        if(buffer[i] == '\r' && buffer[i+1] == '\n' && 
           buffer[i+2] == '\r' && buffer[i+3] == '\n') {
            body = buffer + i + 4;
            body_len = size - (i + 4);
            break;
        }
    }
    
    uint16_t dir_buf[256];
    read_sector(current_dir_sector, dir_buf);
    
    int slot = -1;
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i*16 + j];
        if(name[0] == 0) {
            slot = i;
            break;
        }
    }
    
    if(slot == -1) {
        kprint_color("[HTTP] Directory full!\n", TXT_RED);
        return -1;
    }
    
    for(int i = 0; i < 11 && filename[i]; i++) {
        ((char*)dir_buf)[slot*16 + i] = filename[i];
    }
    ((char*)dir_buf)[slot*16 + 11] = 0;
    
    static int file_counter = 3000;
    int file_sector = file_counter++;
    dir_buf[slot*8 + 6] = file_sector;
    dir_buf[slot*8 + 7] = body_len;
    write_sector(current_dir_sector, dir_buf);
    
    uint16_t data_buf[256] = {0};
    for(int i = 0; i < body_len && i < 510; i++) {
        if(i % 2 == 0) data_buf[i/2] = body[i];
        else data_buf[i/2] |= (body[i] << 8);
    }
    write_sector(file_sector, data_buf);
    
    kprint_color("[HTTP] Saved: ", TXT_GREEN);
    kprint(filename);
    kprint(" (");
    kprint_int(body_len);
    kprint(" bytes)\n");
    
    return body_len;
}