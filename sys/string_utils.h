#ifndef STRING_UTILS_H
#define STRING_UTILS_H

static inline char* my_strcpy(char* dest, const char* src) {
    char* d = dest;
    while(*src) {
        *d++ = *src++;
    }
    *d = '\0';
    return dest;
}

static inline int my_strcmp(const char* s1, const char* s2) {
    while(*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static inline unsigned int my_strlen(const char* s) {
    unsigned int len = 0;
    while(s[len]) len++;
    return len;
}

#endif