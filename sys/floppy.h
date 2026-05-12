#ifndef FLOPPY_H
#define FLOPPY_H

#include <stdint.h>

typedef struct {
    uint8_t boot_sector[512];
    uint8_t fat[9 * 512];
    uint8_t root_dir[14 * 512];
    int mounted;
} floppy_t;

extern floppy_t floppy;

void floppy_init(void);
void floppy_save(void);
int floppy_find_free_slot(void);
int floppy_find_file(const char* filename);
int floppy_create_file(const char* filename);
int floppy_write_file(const char* filename, const char* data);
int floppy_read_file(const char* filename, char* buffer, int max_size);
int floppy_delete_file(const char* filename);
void floppy_list_files(void);

#endif