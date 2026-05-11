#ifndef WNX_H
#define WNX_H

#include <stdint.h>

// Магическое число "WNX1"
#define WNX_MAGIC 0x31584E57

// Типы секций
#define WNX_SECTION_CODE  0x01
#define WNX_SECTION_DATA  0x02
#define WNX_SECTION_STACK 0x03
#define WNX_SECTION_ENTRY 0x04

// Флаги
#define WNX_FLAG_GUI      0x01
#define WNX_FLAG_CONSOLE  0x02
#define WNX_FLAG_CRYPT    0x04

// Заголовок WNX файла
typedef struct {
    uint32_t magic;         // "WNX1"
    uint16_t version;       // Версия формата
    uint16_t flags;         // Флаги (GUI/CONSOLE/CRYPT)
    uint32_t entry_point;   // Точка входа (смещение от начала)
    uint32_t code_size;     // Размер кода
    uint32_t data_size;     // Размер данных
    uint32_t stack_size;    // Размер стека
    uint32_t heap_size;     // Размер кучи
    char     name[32];      // Имя программы
    char     author[32];    // Автор
    char     description[128]; // Описание
    uint32_t icon;          // Иконка в GUI
    uint32_t checksum;      // Контрольная сумма
    uint8_t  reserved[32];  // Зарезервировано
} __attribute__((packed)) wnx_header_t;

// WNX контекст выполнения
typedef struct {
    wnx_header_t header;
    uint8_t* code;
    uint8_t* data;
    uint8_t* stack;
    uint32_t stack_ptr;
    uint32_t entry;
    int running;
    char args[256];
} wnx_context_t;

// Функции для работы с WNX
int  wnx_load(const char* filename, wnx_context_t* ctx);
int  wnx_execute(wnx_context_t* ctx);
void wnx_run(wnx_context_t* ctx);
void wnx_stop(wnx_context_t* ctx);
int  wnx_list_files(void);
int  wnx_create_stub(const char* filename, const char* name);

// GUI функции для WNX
void wnx_browser(void);
int  wnx_get_icon(uint32_t icon_id);

#endif