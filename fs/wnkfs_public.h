#ifndef WNKFS_PUBLIC_H
#define WNKFS_PUBLIC_H

#include "wnkfs.h"

void kprint_char(char c);

extern wnkfs_super_t super;
extern int wnkfs_mounted;
extern uint32_t current_dir;

int read_block(uint32_t block, uint8_t* buffer);
int write_block(uint32_t block, uint8_t* buffer);

#endif