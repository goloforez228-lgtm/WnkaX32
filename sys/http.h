#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>

int http_get(const char* url, uint8_t* buffer, uint32_t max_size);
int http_get_real(const char* url, uint8_t* buffer, uint32_t max_size);
int http_download(const char* url, const char* filename);
void http_parse_url(const char* url, char* host, int* port, char* path);

#endif