#ifndef WNX_MAKE_H
#define WNX_MAKE_H

#include <stdint.h>

// Структура для настройки создания WNX файла
typedef struct {
    const char* name;        // Имя программы
    const char* author;      // Автор
    const char* description; // Описание
    uint32_t icon;           // Тип иконки (0-5)
    uint32_t flags;          // Флаги (GUI/CONSOLE)
    uint32_t stack_size;     // Размер стека (по умолчанию 64KB)
    uint32_t heap_size;      // Размер кучи (по умолчанию 64KB)
    const char* source;      // Исходный код на WnkC
    int source_size;         // Размер исходного кода
} wnx_build_config_t;

// Создание WNX файла из исходного кода
int wnx_create_from_source(const char* source_path, const char* output_name);

// Создание WNX файла из кода в памяти
int wnx_create_from_code(const char* code, int code_size, const char* output_name);

// Создание WNX файла с расширенными настройками
int wnx_create_ex(const wnx_build_config_t* config, const char* output_name);

// Создание пустого шаблона WNX файла
int wnx_create_template(const char* name, const char* output_name);

// Компиляция WnkC скрипта в WNX
int wnx_compile(const char* script_path, const char* output_name);

// Просмотр информации о WNX файле
void wnx_info(const char* filename);

// Извлечение исходного кода из WNX файла
int wnx_extract_source(const char* filename, const char* output_path);

// Список всех WNX файлов на диске
int wnx_list_all(void);

#endif