#include "http.h"
#include "video.h"
#include "net.h"
#include "kernel_stubs.h"
#include "tcp.h"
#include "ata.h"
#include "e1000.h"
#include "graph.h"
#include "rtl8139.h"
#include "string_utils.h"
#include <stdint.h>

static int my_strcpy_len(char* dest, const char* src) {
    int len = 0;
    while(*src) {
        *dest++ = *src++;
        len++;
    }
    return len;
}

#define HTTP_BUFFER_SIZE 8192

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
    
    if(my_strcmp(host, "example.com") == 0) {
        ip->addr[0] = 93;
        ip->addr[1] = 184;
        ip->addr[2] = 216;
        ip->addr[3] = 34;
        return 0;
    }
    
    if(my_strcmp(host, "github.com") == 0) {
        ip->addr[0] = 140;
        ip->addr[1] = 82;
        ip->addr[2] = 112;
        ip->addr[3] = 3;
        return 0;
    }
    
    kprint_color("[DNS] Failed to resolve: ", TXT_RED);
    kprint(host);
    kprint("\n");
    return -1;
}

int http_get_real(const char* url, uint8_t* buffer, uint32_t max_size) {
    kprint("[HTTP] GET ");
    kprint(url);
    kprint("\n");
    
    char host[256];
    int port;
    char path[256];
    
    http_parse_url(url, host, &port, path);
    
    kprint("[HTTP] Host: ");
    kprint(host);
    kprint(" Port: ");
    kprint_int(port);
    kprint(" Path: ");
    kprint(path);
    kprint("\n");
    
    ip_addr_t server_ip;
    if(http_resolve(host, &server_ip) != 0) {
        const char* error = "HTTP/1.0 500 DNS Error\r\n\r\n<html><body><h1>DNS Error</h1></body></html>";
        int len = my_strlen(error);
        for(int i = 0; i < len && i < (int)max_size - 1; i++) buffer[i] = error[i];
        buffer[len] = 0;
        return len;
    }
    
    int sock = tcp_socket_create();
    if(sock < 0) {
        const char* error = "HTTP/1.0 500 No Sockets\r\n\r\n<html><body><h1>No free sockets</h1></body></html>";
        int len = my_strlen(error);
        for(int i = 0; i < len && i < (int)max_size - 1; i++) buffer[i] = error[i];
        buffer[len] = 0;
        return len;
    }
    
    kprint("[HTTP] Socket created: ");
    kprint_int(sock);
    kprint("\n");
    
    if(tcp_connect(sock, server_ip, port) != 0) {
        tcp_close(sock);
        const char* error = "HTTP/1.0 500 Connect Failed\r\n\r\n<html><body><h1>Connection failed</h1></body></html>";
        int len = my_strlen(error);
        for(int i = 0; i < len && i < (int)max_size - 1; i++) buffer[i] = error[i];
        buffer[len] = 0;
        return len;
    }
    
    char request[512];
    int req_len = 0;
    req_len += my_strcpy_len(request + req_len, "GET ");
    req_len += my_strcpy_len(request + req_len, path);
    req_len += my_strcpy_len(request + req_len, " HTTP/1.1\r\n");
    req_len += my_strcpy_len(request + req_len, "Host: ");
    req_len += my_strcpy_len(request + req_len, host);
    req_len += my_strcpy_len(request + req_len, "\r\n");
    req_len += my_strcpy_len(request + req_len, "Connection: close\r\n");
    req_len += my_strcpy_len(request + req_len, "\r\n");
    
    kprint("[HTTP] Sending request...\n");
    tcp_send(sock, (uint8_t*)request, req_len);
    
    kprint("[HTTP] Waiting for response...\n");
    uint32_t total_received = 0;
    
    for(int wait = 0; wait < 500; wait++) {
        uint8_t recv_buf[2048];
        int recv_len = tcp_recv(sock, recv_buf, sizeof(recv_buf) - 1);
        
        if(recv_len > 0) {
            for(int i = 0; i < recv_len && total_received < max_size - 1; i++) {
                buffer[total_received++] = recv_buf[i];
            }
        }
        
        if(total_received > 0 && recv_len == 0) {
            for(int stable = 0; stable < 20; stable++) {
                for(volatile int d = 0; d < 50000; d++);
                if(tcp_recv(sock, recv_buf, sizeof(recv_buf) - 1) > 0) break;
            }
            break;
        }
        
        for(volatile int d = 0; d < 50000; d++);
    }
    
    buffer[total_received] = '\0';
    
    tcp_close(sock);
    
    kprint("[HTTP] Received ");
    kprint_int(total_received);
    kprint(" bytes\n");
    
    return total_received;
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