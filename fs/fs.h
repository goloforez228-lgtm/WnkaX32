#pragma once
#include "video.h"

struct File {
    const char* name;
    const char* content;
    uint32_t size;
};

extern "C" const char* read_file(const char* filename);
extern "C" void list_files();
