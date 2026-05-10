#ifndef MY_BASIC_STUBS_H
#define MY_BASIC_STUBS_H

#include <stddef.h>
#include <stdint.h>

static inline void* malloc(size_t size) {
    static uint8_t heap[8192];
    static uint32_t heap_ptr = 0;
    if(heap_ptr + size > sizeof(heap)) return NULL;
    void* ptr = &heap[heap_ptr];
    heap_ptr += size;
    return ptr;
}

static inline void free(void* ptr) {
    (void)ptr;
}

static inline void* realloc(void* ptr, size_t size) {
    (void)ptr;
    (void)size;
    return NULL;
}

static inline void* calloc(size_t nmemb, size_t size) {
    void* ptr = malloc(nmemb * size);
    if(ptr) {
        for(size_t i = 0; i < nmemb * size; i++) {
            ((char*)ptr)[i] = 0;
        }
    }
    return ptr;
}

static inline int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = s1;
    const unsigned char* p2 = s2;
    for(size_t i = 0; i < n; i++) {
        if(p1[i] != p2[i]) return p1[i] - p2[i];
    }
    return 0;
}

static inline void* memcpy(void* dest, const void* src, size_t n) {
    char* d = dest;
    const char* s = src;
    for(size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

static inline void* memset(void* s, int c, size_t n) {
    char* p = s;
    for(size_t i = 0; i < n; i++) p[i] = c;
    return s;
}

static inline char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while(*src) *d++ = *src++;
    *d = '\0';
    return dest;
}

static inline char* strcat(char* dest, const char* src) {
    char* d = dest;
    while(*d) d++;
    while(*src) *d++ = *src++;
    *d = '\0';
    return dest;
}

static inline int strcmp(const char* s1, const char* s2) {
    while(*s1 && *s2 && *s1 == *s2) { s1++; s2++; }
    return *s1 - *s2;
}

static inline char* strchr(const char* s, int c) {
    while(*s) {
        if(*s == c) return (char*)s;
        s++;
    }
    return NULL;
}

static inline size_t strlen(const char* s) {
    size_t len = 0;
    while(s[len]) len++;
    return len;
}

static inline int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static inline int isdigit(int c) {
    return c >= '0' && c <= '9';
}

#endif