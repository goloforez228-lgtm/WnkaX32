#include "wnkc.h"
#include "video.h"
#include "ata.h"
#include "graph.h"
#include "kernel_stubs.h"
#include "string_utils.h"
#include <stdint.h>

extern "C" void process_command(char* buf, int& ptr);

#define WNKC_MAX_ARRAYS      32
#define WNKC_MAX_STRUCTS     16
#define WNKC_MAX_FUNCTIONS   32
#define WNKC_MAX_IMPORTS     16
#define WNKC_MAX_VARS        64
#define WNKC_MAX_LINE        512
#define WNKC_MAX_BODY        4096
#define WNKC_MAX_STRING      256
#define WNKC_NAME_LEN        32
#define WNKC_MAX_PARAMS      8
#define WNKC_MAX_COND        128
#define WNKC_MAX_EXPR        128
#define WNKC_MAX_CODE        512
#define WNKC_MAX_LOOP_ITER   10000

#define WNKC_TYPE_NUMBER     0
#define WNKC_TYPE_STRING     1
#define WNKC_TYPE_ARRAY      2
#define WNKC_TYPE_STRUCT     3

typedef struct {
    char name[WNKC_NAME_LEN];
    int  values[256];
    int  size;
} wnc_array_t;

typedef struct {
    char name[WNKC_NAME_LEN];
    char fields[16][WNKC_NAME_LEN];
    int  field_values[16];
    int  field_count;
} wnc_struct_t;

typedef struct {
    char name[WNKC_NAME_LEN];
    char params[WNKC_MAX_PARAMS][WNKC_NAME_LEN];
    int  param_count;
    char body[WNKC_MAX_BODY];
    int  is_static;
    int  is_void;
} wnc_function_t;

static uint16_t        current_dir_sector = 100;
static wnc_var_t       wnc_vars[WNKC_MAX_VARS];
static wnc_array_t     wnc_arrays[WNKC_MAX_ARRAYS];
static wnc_struct_t    wnc_structs[WNKC_MAX_STRUCTS];
static wnc_function_t  wnc_functions[WNKC_MAX_FUNCTIONS];
static char            wnc_imported_files[WNKC_MAX_IMPORTS][WNKC_MAX_STRING];
static int             wnc_var_count       = 0;
static int             wnc_array_count     = 0;
static int             wnc_struct_count    = 0;
static int             wnc_function_count  = 0;
static int             wnc_import_count    = 0;
static int             wnc_error           = 0;
static int             wnc_line            = 1;
static int             wnc_log_enabled     = 1;
static int             wnc_break_flag      = 0;
static int             wnc_continue_flag   = 0;
static int             wnc_return_flag     = 0;
static int             wnc_return_value    = 0;
static int             wnc_in_function     = 0;

static void kprint_char(char c) {
    char s[2] = {c, 0};
    kprint(s);
}

static int wnc_strlen(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static int wnc_strcmp(const char* a, const char* b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

static int wnc_strncmp(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
        if (a[i] == 0) return 0;
    }
    return 0;
}

static void wnc_strcpy(char* dest, const char* src) {
    while (*src) *dest++ = *src++;
    *dest = 0;
}

static int wnc_atoi(const char* s) {
    int result = 0, sign = 1;
    if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }
    return result * sign;
}

static void wnc_itoa(int n, char* str) {
    if (n == 0) { str[0] = '0'; str[1] = 0; return; }
    char temp[16];
    int  i = 0, sign = n;
    if (n < 0) n = -n;
    while (n) { temp[i++] = '0' + (n % 10); n /= 10; }
    int j = 0;
    if (sign < 0) str[j++] = '-';
    while (i > 0) str[j++] = temp[--i];
    str[j] = 0;
}

static int wnc_rand(int min, int max) {
    static unsigned int seed = 12345;
    seed = seed * 1103515245 + 12345;
    return min + (seed % (max - min + 1));
}

static int wnc_find_var(const char* name) {
    for (int i = 0; i < wnc_var_count; i++) {
        if (wnc_strcmp(wnc_vars[i].name, name) == 0) return i;
    }
    return -1;
}

static void wnc_set_var(const char* name, int value) {
    int idx = wnc_find_var(name);
    if (idx == -1 && wnc_var_count < WNKC_MAX_VARS) {
        idx = wnc_var_count++;
        wnc_strcpy(wnc_vars[idx].name, name);
    }
    if (idx != -1) {
        wnc_vars[idx].num_value = value;
        wnc_vars[idx].type = WNKC_TYPE_NUMBER;
    }
}

static void wnc_set_var_str(const char* name, const char* value) {
    int idx = wnc_find_var(name);
    if (idx == -1 && wnc_var_count < WNKC_MAX_VARS) {
        idx = wnc_var_count++;
        wnc_strcpy(wnc_vars[idx].name, name);
    }
    if (idx != -1) {
        wnc_strcpy(wnc_vars[idx].str_value, value);
        wnc_vars[idx].type = WNKC_TYPE_STRING;
    }
}

static int wnc_get_var(const char* name) {
    int idx = wnc_find_var(name);
    if (idx != -1 && wnc_vars[idx].type == WNKC_TYPE_NUMBER) {
        return wnc_vars[idx].num_value;
    }
    return 0;
}

static const char* wnc_get_var_str(const char* name) {
    int idx = wnc_find_var(name);
    if (idx != -1 && wnc_vars[idx].type == WNKC_TYPE_STRING) {
        return wnc_vars[idx].str_value;
    }
    return "";
}

static int wnc_find_array(const char* name) {
    for (int i = 0; i < wnc_array_count; i++) {
        if (wnc_strcmp(wnc_arrays[i].name, name) == 0) return i;
    }
    return -1;
}

static int wnc_create_array(const char* name, int size) {
    if (size < 1) size = 1;
    if (size > 256) size = 256;
    int idx = wnc_find_array(name);
    if (idx != -1) {
        wnc_arrays[idx].size = size;
        for (int j = 0; j < size; j++) wnc_arrays[idx].values[j] = 0;
        return idx;
    }
    if (wnc_array_count >= WNKC_MAX_ARRAYS) return -1;
    wnc_strcpy(wnc_arrays[wnc_array_count].name, name);
    wnc_arrays[wnc_array_count].size = size;
    for (int j = 0; j < size; j++) wnc_arrays[wnc_array_count].values[j] = 0;
    wnc_array_count++;
    return wnc_array_count - 1;
}

static void wnc_array_set(const char* name, int index, int value) {
    int idx = wnc_find_array(name);
    if (idx == -1 || index < 0 || index >= wnc_arrays[idx].size) {
        kprint_color("Array index error\n", TXT_RED);
        return;
    }
    wnc_arrays[idx].values[index] = value;
}

static int wnc_array_get(const char* name, int index) {
    int idx = wnc_find_array(name);
    if (idx == -1 || index < 0 || index >= wnc_arrays[idx].size) {
        kprint_color("Array index error\n", TXT_RED);
        return 0;
    }
    return wnc_arrays[idx].values[index];
}

static void wnc_array_print(const char* name) {
    int idx = wnc_find_array(name);
    if (idx == -1) { kprint_color("Array not found\n", TXT_RED); return; }
    kprint(name);
    kprint(" = [");
    for (int j = 0; j < wnc_arrays[idx].size; j++) {
        kprint_int(wnc_arrays[idx].values[j]);
        if (j < wnc_arrays[idx].size - 1) kprint(", ");
    }
    kprint("]\n");
}

static int wnc_find_struct(const char* name) {
    for (int i = 0; i < wnc_struct_count; i++) {
        if (wnc_strcmp(wnc_structs[i].name, name) == 0) return i;
    }
    return -1;
}

static int wnc_create_struct(const char* name) {
    if (wnc_find_struct(name) != -1) return -1;
    if (wnc_struct_count >= WNKC_MAX_STRUCTS) return -1;
    wnc_strcpy(wnc_structs[wnc_struct_count].name, name);
    wnc_structs[wnc_struct_count].field_count = 0;
    wnc_struct_count++;
    return wnc_struct_count - 1;
}

static void wnc_struct_add_field(const char* struct_name, const char* field_name) {
    int idx = wnc_find_struct(struct_name);
    if (idx == -1) { kprint_color("Struct not found\n", TXT_RED); return; }
    if (wnc_structs[idx].field_count < 16) {
        wnc_strcpy(wnc_structs[idx].fields[wnc_structs[idx].field_count], field_name);
        wnc_structs[idx].field_values[wnc_structs[idx].field_count] = 0;
        wnc_structs[idx].field_count++;
    }
}

static void wnc_struct_set(const char* struct_name, const char* field_name, int value) {
    int idx = wnc_find_struct(struct_name);
    if (idx == -1) return;
    for (int j = 0; j < wnc_structs[idx].field_count; j++) {
        if (wnc_strcmp(wnc_structs[idx].fields[j], field_name) == 0) {
            wnc_structs[idx].field_values[j] = value;
            return;
        }
    }
}

static int wnc_struct_get(const char* struct_name, const char* field_name) {
    int idx = wnc_find_struct(struct_name);
    if (idx == -1) return 0;
    for (int j = 0; j < wnc_structs[idx].field_count; j++) {
        if (wnc_strcmp(wnc_structs[idx].fields[j], field_name) == 0) {
            return wnc_structs[idx].field_values[j];
        }
    }
    return 0;
}

static void wnc_struct_print(const char* name) {
    int idx = wnc_find_struct(name);
    if (idx == -1) { kprint_color("Struct not found\n", TXT_RED); return; }
    kprint(name);
    kprint(" = { ");
    for (int j = 0; j < wnc_structs[idx].field_count; j++) {
        kprint(wnc_structs[idx].fields[j]);
        kprint(": ");
        kprint_int(wnc_structs[idx].field_values[j]);
        if (j < wnc_structs[idx].field_count - 1) kprint(", ");
    }
    kprint(" }\n");
}

static int wnc_find_function(const char* name) {
    for (int i = 0; i < wnc_function_count; i++) {
        if (wnc_strcmp(wnc_functions[i].name, name) == 0) return i;
    }
    return -1;
}

static int wnc_add_function(const char* name, const char* params, const char* body, int is_static, int is_void) {
    if (wnc_find_function(name) != -1 && !is_static) return -1;
    if (wnc_function_count >= WNKC_MAX_FUNCTIONS) return -1;
    wnc_strcpy(wnc_functions[wnc_function_count].name, name);
    wnc_functions[wnc_function_count].param_count = 0;
    wnc_functions[wnc_function_count].is_static = is_static;
    wnc_functions[wnc_function_count].is_void = is_void;
    wnc_strcpy(wnc_functions[wnc_function_count].body, body);
    return wnc_function_count++;
}

static int wnc_call_function(const char* name) {
    int idx = wnc_find_function(name);
    if (idx == -1) {
        kprint_color("Function not found: ", TXT_RED);
        kprint(name);
        kprint("\n");
        return 0;
    }
    int saved_return = wnc_return_flag;
    int saved_value  = wnc_return_value;
    wnc_return_flag  = 0;
    wnc_in_function  = 1;
    wnc_execute(wnc_functions[idx].body);
    wnc_in_function = 0;
    int result = wnc_return_value;
    if (!saved_return) wnc_return_flag = 0;
    wnc_return_value = saved_value;
    return result;
}

static int wnc_is_imported(const char* filename) {
    for (int i = 0; i < wnc_import_count; i++) {
        if (wnc_strcmp(wnc_imported_files[i], filename) == 0) return 1;
    }
    return 0;
}

static void wnc_import_file(const char* filename) {
    if (wnc_is_imported(filename)) {
        kprint_color("Already imported: ", TXT_YELLOW);
        kprint(filename);
        kprint("\n");
        return;
    }
    if (wnc_import_count >= WNKC_MAX_IMPORTS) {
        kprint_color("Too many imports\n", TXT_RED);
        return;
    }
    wnc_strcpy(wnc_imported_files[wnc_import_count], filename);
    wnc_import_count++;
    kprint_color("Importing: ", TXT_CYAN);
    kprint(filename);
    kprint("\n");
    wnc_execute_file(filename);
}

static int wnc_eval_expr(const char* expr) {
    int a = 0, b = 0;
    char op = 0;
    int i = 0, sign = 1;

    while (expr[i] == ' ') i++;
    if (expr[i] == '-') { sign = -1; i++; }

    if (expr[i] >= '0' && expr[i] <= '9') {
        while (expr[i] >= '0' && expr[i] <= '9') {
            a = a * 10 + (expr[i] - '0');
            i++;
        }
        a *= sign;
    } else if ((expr[i] >= 'a' && expr[i] <= 'z') || (expr[i] >= 'A' && expr[i] <= 'Z') || expr[i] == '_') {
        int start = i;
        while (expr[i] && expr[i] != ' ' && expr[i] != '+' && expr[i] != '-' && expr[i] != '*' && expr[i] != '/') i++;
        char var_name[WNKC_NAME_LEN] = {0};
        for (int j = 0; j < i - start && j < WNKC_NAME_LEN - 1; j++) var_name[j] = expr[start + j];
        a = wnc_get_var(var_name) * sign;
    }

    while (expr[i] == ' ') i++;
    if (expr[i] == '+' || expr[i] == '-' || expr[i] == '*' || expr[i] == '/') {
        op = expr[i];
        i++;
    } else {
        return a;
    }

    while (expr[i] == ' ') i++;
    sign = 1;
    if (expr[i] == '-') { sign = -1; i++; }

    if (expr[i] >= '0' && expr[i] <= '9') {
        while (expr[i] >= '0' && expr[i] <= '9') {
            b = b * 10 + (expr[i] - '0');
            i++;
        }
        b *= sign;
    } else if ((expr[i] >= 'a' && expr[i] <= 'z') || (expr[i] >= 'A' && expr[i] <= 'Z') || expr[i] == '_') {
        int start = i;
        while (expr[i] && expr[i] != ' ' && expr[i] != '+' && expr[i] != '-' && expr[i] != '*' && expr[i] != '/') i++;
        char var_name[WNKC_NAME_LEN] = {0};
        for (int j = 0; j < i - start && j < WNKC_NAME_LEN - 1; j++) var_name[j] = expr[start + j];
        b = wnc_get_var(var_name) * sign;
    }

    switch (op) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': return (b != 0) ? a / b : 0;
    }
    return a;
}

static int wnc_check_condition(const char* cond) {
    char var_name[WNKC_NAME_LEN] = {0};
    char op_str[4] = {0};
    char val_str[WNKC_NAME_LEN] = {0};

    const char* p = cond;
    while (*p == ' ') p++;
    if (*p == '(') p++;

    int i = 0;
    if (*p == '$') p++;
    while (*p && *p != ' ' && i < WNKC_NAME_LEN - 1) var_name[i++] = *p++;
    while (*p == ' ') p++;

    i = 0;
    while (*p && *p != ' ' && i < 3) op_str[i++] = *p++;
    while (*p == ' ') p++;

    i = 0;
    int is_string = 0;
    if (*p == '"') {
        is_string = 1;
        p++;
        while (*p && *p != '"' && i < WNKC_NAME_LEN - 1) val_str[i++] = *p++;
    } else {
        while (*p && *p != ' ' && *p != ')' && i < WNKC_NAME_LEN - 1) val_str[i++] = *p++;
    }

    int var_value = wnc_get_var(var_name);
    int cmp_value = wnc_atoi(val_str);
    const char* var_str = wnc_get_var_str(var_name);

    if (wnc_strcmp(op_str, "==") == 0) {
        if (is_string) return wnc_strcmp(var_str, val_str) == 0;
        else return var_value == cmp_value;
    }
    if (wnc_strcmp(op_str, "!=") == 0) {
        if (is_string) return wnc_strcmp(var_str, val_str) != 0;
        else return var_value != cmp_value;
    }
    if (wnc_strcmp(op_str, "<")  == 0) return var_value <  cmp_value;
    if (wnc_strcmp(op_str, ">")  == 0) return var_value >  cmp_value;
    if (wnc_strcmp(op_str, "<=") == 0) return var_value <= cmp_value;
    if (wnc_strcmp(op_str, ">=") == 0) return var_value >= cmp_value;
    return 0;
}

static void wnc_print(const char* line) {
    const char* p = line;
    while (*p == ' ') p++;
    while (*p) {
        if (*p == '$') {
            p++;
            char var_name[WNKC_NAME_LEN] = {0};
            int i = 0;
            while (*p && *p != ' ' && *p != '\n' && i < WNKC_NAME_LEN - 1) var_name[i++] = *p++;
            int idx = wnc_find_var(var_name);
            if (idx != -1) {
                if (wnc_vars[idx].type == WNKC_TYPE_NUMBER) kprint_int(wnc_vars[idx].num_value);
                else kprint(wnc_vars[idx].str_value);
            }
        } else if (*p == '\\' && *(p + 1) == 'n') {
            kprint("\n");
            p += 2;
        } else {
            char s[2] = {*p, 0};
            kprint(s);
            p++;
        }
    }
    kprint("\n");
}

static void wnc_input(const char* line) {
    char var_name[WNKC_NAME_LEN] = {0};
    const char* p = line;
    while (*p == ' ') p++;
    int i = 0;
    while (*p && *p != ' ' && *p != '\n' && i < WNKC_NAME_LEN - 1) var_name[i++] = *p++;
    kprint("> ");
    char buffer[WNKC_MAX_STRING] = {0};
    int pos = 0;
    while (1) {
        if (inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if (sc == 0x1C) break;
            if (sc == 0x0E && pos > 0) { pos--; kprint("\b \b"); }
            else if (sc >= 0x02 && sc <= 0x0B && pos < WNKC_MAX_STRING - 1) {
                buffer[pos++] = "1234567890"[sc - 0x02];
                kprint_char(buffer[pos - 1]);
            } else if (sc >= 0x10 && sc <= 0x19 && pos < WNKC_MAX_STRING - 1) {
                buffer[pos++] = "qwertyuiop"[sc - 0x10];
                kprint_char(buffer[pos - 1]);
            } else if (sc >= 0x1E && sc <= 0x26 && pos < WNKC_MAX_STRING - 1) {
                buffer[pos++] = "asdfghjkl"[sc - 0x1E];
                kprint_char(buffer[pos - 1]);
            } else if (sc >= 0x2C && sc <= 0x32 && pos < WNKC_MAX_STRING - 1) {
                buffer[pos++] = "zxcvbnm"[sc - 0x2C];
                kprint_char(buffer[pos - 1]);
            } else if (sc == 0x39 && pos < WNKC_MAX_STRING - 1) {
                buffer[pos++] = ' ';
                kprint_char(' ');
            }
        }
    }
    buffer[pos] = 0;
    kprint("\n");
    int is_number = 1;
    for (int j = 0; buffer[j]; j++) {
        if (buffer[j] < '0' || buffer[j] > '9') { is_number = 0; break; }
    }
    if (is_number) wnc_set_var(var_name, wnc_atoi(buffer));
    else wnc_set_var_str(var_name, buffer);
}

static void wnc_let(const char* line) {
    char var_name[WNKC_NAME_LEN] = {0};
    char expr[WNKC_MAX_EXPR] = {0};
    const char* p = line;
    while (*p == ' ') p++;
    int i = 0;
    while (*p && *p != '=' && *p != ' ' && i < WNKC_NAME_LEN - 1) var_name[i++] = *p++;
    while (*p && *p != '=') p++;
    if (*p == '=') p++;
    while (*p == ' ') p++;
    if (*p == '"') {
        p++;
        char str_value[WNKC_MAX_STRING] = {0};
        i = 0;
        while (*p && *p != '"' && i < WNKC_MAX_STRING - 1) str_value[i++] = *p++;
        wnc_set_var_str(var_name, str_value);
    } else {
        i = 0;
        while (*p && *p != '\n' && i < WNKC_MAX_EXPR - 1) expr[i++] = *p++;
        wnc_set_var(var_name, wnc_eval_expr(expr));
    }
}

static void wnc_array_cmd(const char* line) {
    char cmd[WNKC_NAME_LEN] = {0};
    char name[WNKC_NAME_LEN] = {0};
    const char* p = line;
    while (*p == ' ') p++;
    int i = 0;
    while (*p && *p != ' ' && i < WNKC_NAME_LEN - 1) cmd[i++] = *p++;
    while (*p == ' ') p++;
    if (wnc_strcmp(cmd, "create") == 0) {
        i = 0;
        while (*p && *p != ' ' && i < WNKC_NAME_LEN - 1) name[i++] = *p++;
        while (*p == ' ') p++;
        int size = wnc_atoi(p);
        wnc_create_array(name, size);
        kprint("Array created: "); kprint(name); kprint("["); kprint_int(size); kprint("]\n");
    } else if (wnc_strcmp(cmd, "set") == 0) {
        i = 0;
        while (*p && *p != '[' && i < WNKC_NAME_LEN - 1) name[i++] = *p++;
        while (*p != '[') p++;
        p++;
        int index = wnc_atoi(p);
        while (*p != ']') p++;
        p++;
        while (*p == ' ') p++;
        int value = wnc_atoi(p);
        wnc_array_set(name, index, value);
    } else if (wnc_strcmp(cmd, "get") == 0) {
        i = 0;
        while (*p && *p != '[' && i < WNKC_NAME_LEN - 1) name[i++] = *p++;
        while (*p != '[') p++;
        p++;
        int index = wnc_atoi(p);
        int value = wnc_array_get(name, index);
        kprint_int(value); kprint("\n");
    } else if (wnc_strcmp(cmd, "print") == 0) {
        i = 0;
        while (*p && *p != ' ' && i < WNKC_NAME_LEN - 1) name[i++] = *p++;
        wnc_array_print(name);
    }
}

static void wnc_struct_cmd(const char* line) {
    char cmd[WNKC_NAME_LEN] = {0};
    char name[WNKC_NAME_LEN] = {0};
    char field[WNKC_NAME_LEN] = {0};
    const char* p = line;
    while (*p == ' ') p++;
    int i = 0;
    while (*p && *p != ' ' && i < WNKC_NAME_LEN - 1) cmd[i++] = *p++;
    while (*p == ' ') p++;
    if (wnc_strcmp(cmd, "create") == 0) {
        i = 0;
        while (*p && *p != ' ' && i < WNKC_NAME_LEN - 1) name[i++] = *p++;
        wnc_create_struct(name);
        kprint("Struct created: "); kprint(name); kprint("\n");
    } else if (wnc_strcmp(cmd, "field") == 0) {
        i = 0;
        while (*p && *p != '.' && i < WNKC_NAME_LEN - 1) name[i++] = *p++;
        if (*p == '.') p++;
        i = 0;
        while (*p && *p != ' ' && i < WNKC_NAME_LEN - 1) field[i++] = *p++;
        wnc_struct_add_field(name, field);
    } else if (wnc_strcmp(cmd, "set") == 0) {
        i = 0;
        while (*p && *p != '.' && i < WNKC_NAME_LEN - 1) name[i++] = *p++;
        if (*p == '.') p++;
        i = 0;
        while (*p && *p != '=' && i < WNKC_NAME_LEN - 1) field[i++] = *p++;
        while (*p != '=') p++;
        p++;
        int value = wnc_atoi(p);
        wnc_struct_set(name, field, value);
    } else if (wnc_strcmp(cmd, "get") == 0) {
        i = 0;
        while (*p && *p != '.' && i < WNKC_NAME_LEN - 1) name[i++] = *p++;
        if (*p == '.') p++;
        i = 0;
        while (*p && *p != ' ' && i < WNKC_NAME_LEN - 1) field[i++] = *p++;
        int value = wnc_struct_get(name, field);
        kprint_int(value); kprint("\n");
    } else if (wnc_strcmp(cmd, "print") == 0) {
        i = 0;
        while (*p && *p != ' ' && i < WNKC_NAME_LEN - 1) name[i++] = *p++;
        wnc_struct_print(name);
    }
}

static void wnc_func_cmd(const char* line) {
    char type[16] = {0};
    char name[WNKC_NAME_LEN] = {0};
    char params[WNKC_MAX_STRING] = {0};
    char body[WNKC_MAX_BODY] = {0};
    const char* p = line;
    while (*p == ' ') p++;
    int i = 0;
    while (*p && *p != ' ' && i < 15) type[i++] = *p++;
    while (*p == ' ') p++;
    i = 0;
    while (*p && *p != '(' && i < WNKC_NAME_LEN - 1) name[i++] = *p++;
    while (*p != '(') p++;
    p++;
    i = 0;
    while (*p && *p != ')') { if (i < WNKC_MAX_STRING - 1) params[i++] = *p++; else p++; }
    while (*p != ')') p++;
    p++;
    while (*p == ' ') p++;
    if (*p == '{') {
        p++;
        int depth = 1;
        int j = 0;
        while (*p && depth > 0 && j < WNKC_MAX_BODY - 1) {
            if (*p == '{') depth++;
            if (*p == '}') depth--;
            if (depth > 0) body[j++] = *p;
            p++;
        }
    }
    int is_void = (wnc_strcmp(type, "void") == 0);
    wnc_add_function(name, params, body, 0, is_void);
}

static void wnc_import_cmd(const char* line) {
    char filename[WNKC_MAX_STRING] = {0};
    const char* p = line;
    while (*p == ' ') p++;
    int i = 0;
    while (*p && *p != ' ' && *p != '\n' && i < WNKC_MAX_STRING - 1) filename[i++] = *p++;
    wnc_import_file(filename);
}

static void wnc_return_cmd(const char* line) {
    const char* p = line;
    while (*p == ' ') p++;
    wnc_return_value = wnc_eval_expr(p);
    wnc_return_flag = 1;
}

static void wnc_graph_cmd(const char* line) {
    char cmd[WNKC_NAME_LEN] = {0};
    const char* p = line;
    while (*p == ' ') p++;
    int i = 0;
    while (*p && *p != ' ' && *p != '(' && i < WNKC_NAME_LEN - 1) cmd[i++] = *p++;
    while (*p == ' ' || *p == '(') p++;

    if (wnc_strcmp(cmd, "clear") == 0) {
        clear_screen();
    } else if (wnc_strcmp(cmd, "pixel") == 0) {
        int x = wnc_atoi(p); while ((*p >= '0' && *p <= '9') || *p == '-') p++;
        while (*p == ' ' || *p == ',') p++;
        int y = wnc_atoi(p); while ((*p >= '0' && *p <= '9') || *p == '-') p++;
        while (*p == ' ' || *p == ',') p++;
        int c = wnc_atoi(p);
        put_pixel(x, y, c, 0x0F, ' ');
    } else if (wnc_strcmp(cmd, "line") == 0) {
        int x1 = wnc_atoi(p); while ((*p >= '0' && *p <= '9') || *p == '-') p++;
        while (*p == ' ' || *p == ',') p++;
        int y1 = wnc_atoi(p); while ((*p >= '0' && *p <= '9') || *p == '-') p++;
        while (*p == ' ' || *p == ',') p++;
        int x2 = wnc_atoi(p); while ((*p >= '0' && *p <= '9') || *p == '-') p++;
        while (*p == ' ' || *p == ',') p++;
        int y2 = wnc_atoi(p); while ((*p >= '0' && *p <= '9') || *p == '-') p++;
        while (*p == ' ' || *p == ',') p++;
        int c  = wnc_atoi(p);
        int dx = (x2 > x1) ? (x2 - x1) : (x1 - x2);
        int sx = x1 < x2 ? 1 : -1;
        int dy = (y2 > y1) ? -(y2 - y1) : -(y1 - y2);
        int sy = y1 < y2 ? 1 : -1;
        int err = dx + dy;
        while (1) {
            put_pixel(x1, y1, c, 0x0F, ' ');
            if (x1 == x2 && y1 == y2) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x1 += sx; }
            if (e2 <= dx) { err += dx; y1 += sy; }
        }
    } else if (wnc_strcmp(cmd, "rect") == 0) {
        int x = wnc_atoi(p); while ((*p >= '0' && *p <= '9') || *p == '-') p++;
        while (*p == ' ' || *p == ',') p++;
        int y = wnc_atoi(p); while ((*p >= '0' && *p <= '9') || *p == '-') p++;
        while (*p == ' ' || *p == ',') p++;
        int w = wnc_atoi(p); while ((*p >= '0' && *p <= '9') || *p == '-') p++;
        while (*p == ' ' || *p == ',') p++;
        int h = wnc_atoi(p); while ((*p >= '0' && *p <= '9') || *p == '-') p++;
        while (*p == ' ' || *p == ',') p++;
        int c = wnc_atoi(p);
        for (int i = 0; i < w; i++) {
            put_pixel(x + i, y, c, 0x0F, ' ');
            put_pixel(x + i, y + h - 1, c, 0x0F, ' ');
        }
        for (int i = 0; i < h; i++) {
            put_pixel(x, y + i, c, 0x0F, ' ');
            put_pixel(x + w - 1, y + i, c, 0x0F, ' ');
        }
    } else if (wnc_strcmp(cmd, "fill") == 0) {
        int c = wnc_atoi(p);
        for (int i = 0; i < 80; i++)
            for (int j = 0; j < 25; j++)
                put_pixel(i, j, c, 0x0F, ' ');
    } else if (wnc_strcmp(cmd, "circle") == 0) {
        int cx = wnc_atoi(p); while ((*p >= '0' && *p <= '9') || *p == '-') p++;
        while (*p == ' ' || *p == ',') p++;
        int cy = wnc_atoi(p); while ((*p >= '0' && *p <= '9') || *p == '-') p++;
        while (*p == ' ' || *p == ',') p++;
        int r  = wnc_atoi(p); while ((*p >= '0' && *p <= '9') || *p == '-') p++;
        while (*p == ' ' || *p == ',') p++;
        int c  = wnc_atoi(p);
        int x = 0, y = r, d = 3 - 2 * r;
        while (y >= x) {
            put_pixel(cx + x, cy + y, c, 0x0F, ' ');
            put_pixel(cx - x, cy + y, c, 0x0F, ' ');
            put_pixel(cx + x, cy - y, c, 0x0F, ' ');
            put_pixel(cx - x, cy - y, c, 0x0F, ' ');
            put_pixel(cx + y, cy + x, c, 0x0F, ' ');
            put_pixel(cx - y, cy + x, c, 0x0F, ' ');
            put_pixel(cx + y, cy - x, c, 0x0F, ' ');
            put_pixel(cx - y, cy - x, c, 0x0F, ' ');
            x++;
            if (d < 0) d += 4 * x + 6;
            else { d += 4 * (x - y) + 10; y--; }
        }
    } else if (wnc_strcmp(cmd, "text") == 0) {
        int x = wnc_atoi(p); while ((*p >= '0' && *p <= '9') || *p == '-') p++;
        while (*p == ' ' || *p == ',') p++;
        int y = wnc_atoi(p); while ((*p >= '0' && *p <= '9') || *p == '-') p++;
        while (*p == ' ' || *p == ',') p++;
        int c = wnc_atoi(p);
        while (*p == ' ' || *p == ',') p++;
        if (*p == '"') {
            p++;
            char txt[WNKC_MAX_STRING] = {0};
            int i = 0;
            while (*p && *p != '"' && i < WNKC_MAX_STRING - 1) txt[i++] = *p++;
            kprint_at(txt, x, y, c);
        }
    } else if (wnc_strcmp(cmd, "frame") == 0) {
        int x  = wnc_atoi(p); while ((*p >= '0' && *p <= '9') || *p == '-') p++;
        while (*p == ' ' || *p == ',') p++;
        int y  = wnc_atoi(p); while ((*p >= '0' && *p <= '9') || *p == '-') p++;
        while (*p == ' ' || *p == ',') p++;
        int w  = wnc_atoi(p); while ((*p >= '0' && *p <= '9') || *p == '-') p++;
        while (*p == ' ' || *p == ',') p++;
        int h  = wnc_atoi(p); while ((*p >= '0' && *p <= '9') || *p == '-') p++;
        while (*p == ' ' || *p == ',') p++;
        int bg = wnc_atoi(p); while ((*p >= '0' && *p <= '9') || *p == '-') p++;
        while (*p == ' ' || *p == ',') p++;
        int fg = wnc_atoi(p);
        for (int i = 0; i < w; i++) {
            put_pixel(x + i, y, bg, fg, S_HLINE);
            put_pixel(x + i, y + h - 1, bg, fg, S_HLINE);
        }
        for (int i = 0; i < h; i++) {
            put_pixel(x, y + i, bg, fg, S_VLINE);
            put_pixel(x + w - 1, y + i, bg, fg, S_VLINE);
        }
        put_pixel(x, y, bg, fg, S_TL);
        put_pixel(x + w - 1, y, bg, fg, S_TR);
        put_pixel(x, y + h - 1, bg, fg, S_BL);
        put_pixel(x + w - 1, y + h - 1, bg, fg, S_BR);
    }
}

static void wnc_key_cmd(const char* line) {
    char cmd[WNKC_NAME_LEN] = {0};
    const char* p = line;
    while (*p == ' ') p++;
    int i = 0;
    while (*p && *p != ' ' && i < WNKC_NAME_LEN - 1) cmd[i++] = *p++;
    while (*p == ' ') p++;
    if (wnc_strcmp(cmd, "wait") == 0) {
        while (!(inb(0x64) & 1));
        uint8_t sc = inb(0x60);
        kprint_int(sc); kprint("\n");
    } else if (wnc_strcmp(cmd, "get") == 0) {
        char var_name[WNKC_NAME_LEN] = {0};
        i = 0;
        while (*p && *p != ' ' && *p != '\n' && i < WNKC_NAME_LEN - 1) var_name[i++] = *p++;
        while (!(inb(0x64) & 1));
        uint8_t sc = inb(0x60);
        wnc_set_var(var_name, sc);
    } else if (wnc_strcmp(cmd, "check") == 0) {
        char var_name[WNKC_NAME_LEN] = {0};
        i = 0;
        while (*p && *p != ' ' && *p != '\n' && i < WNKC_NAME_LEN - 1) var_name[i++] = *p++;
        int pressed = 0;
        if (inb(0x64) & 1) { uint8_t sc = inb(0x60); pressed = sc; }
        wnc_set_var(var_name, pressed);
    }
}

static void wnc_mkdir(const char* line) {
    char dirname[WNKC_NAME_LEN] = {0};
    const char* p = line;
    while (*p == ' ') p++;
    int i = 0;
    while (*p && *p != ' ' && *p != '\n' && i < WNKC_NAME_LEN - 1) dirname[i++] = *p++;
    if (dirname[0] == 0) { kprint("Usage: mkdir <name>\n"); return; }
    uint16_t dir_buf[256];
    read_sector(current_dir_sector, dir_buf);
    int slot = -1;
    for (i = 0; i < 32; i++) {
        char name[12] = {0};
        for (int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i * 16 + j];
        if (name[0] == 0) { slot = i; break; }
    }
    if (slot == -1) { kprint("No free slots\n"); return; }
    for (i = 0; i < 11 && dirname[i]; i++) ((char*)dir_buf)[slot * 16 + i] = dirname[i];
    ((char*)dir_buf)[slot * 16 + 11] = 1;
    static int dir_counter = 300;
    int new_dir_sector = dir_counter++;
    dir_buf[slot * 8 + 6] = new_dir_sector;
    dir_buf[slot * 8 + 7] = 0;
    write_sector(current_dir_sector, dir_buf);
    uint16_t empty_buf[256];
    for (i = 0; i < 256; i++) empty_buf[i] = 0;
    write_sector(new_dir_sector, empty_buf);
    kprint("Directory created: "); kprint(dirname); kprint("\n");
}

static void wnc_cd(const char* line) {
    char dirname[WNKC_NAME_LEN] = {0};
    const char* p = line;
    while (*p == ' ') p++;
    int i = 0;
    while (*p && *p != ' ' && *p != '\n' && i < WNKC_NAME_LEN - 1) dirname[i++] = *p++;
    if (dirname[0] == 0 || wnc_strcmp(dirname, "/") == 0) {
        current_dir_sector = 100;
        kprint("Changed to root\n");
        return;
    }
    uint16_t dir_buf[256];
    read_sector(current_dir_sector, dir_buf);
    for (i = 0; i < 32; i++) {
        char name[12] = {0};
        for (int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i * 16 + j];
        if (wnc_strcmp(dirname, name) == 0 && ((char*)dir_buf)[i * 16 + 11] == 1) {
            current_dir_sector = dir_buf[i * 8 + 6];
            kprint("Changed to: "); kprint(dirname); kprint("\n");
            return;
        }
    }
    kprint("Directory not found: "); kprint(dirname); kprint("\n");
}

static void wnc_pwd(const char* line) {
    (void)line;
    if (current_dir_sector == 100) kprint("/\n");
    else { kprint("/dir_"); kprint_int(current_dir_sector - 300); kprint("\n"); }
}

static void wnc_ls(const char* line) {
    (void)line;
    uint16_t dir_buf[256];
    read_sector(current_dir_sector, dir_buf);
    kprint("\n=== DIRECTORY ===\n");
    int count = 0;
    for (int i = 0; i < 32; i++) {
        char name[12] = {0};
        for (int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i * 16 + j];
        if (name[0] != 0) {
            int is_dir = ((char*)dir_buf)[i * 16 + 11] == 1;
            int size = dir_buf[i * 8 + 7];
            if (is_dir) kprint("  [DIR]  ");
            else kprint("  [FILE] ");
            kprint(name);
            if (!is_dir) { kprint(" ("); kprint_int(size); kprint(" bytes)"); }
            kprint("\n");
            count++;
        }
    }
    if (count == 0) kprint("  (empty)\n");
    kprint("================\n");
}

static void wnc_create(const char* line) {
    char filename[WNKC_NAME_LEN] = {0};
    const char* p = line;
    while (*p == ' ') p++;
    int i = 0;
    while (*p && *p != ' ' && *p != '\n' && i < WNKC_NAME_LEN - 1) filename[i++] = *p++;
    if (filename[0] == 0) { kprint("Usage: create <filename>\n"); return; }
    uint16_t dir_buf[256];
    read_sector(current_dir_sector, dir_buf);
    for (i = 0; i < 32; i++) {
        char name[12] = {0};
        for (int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i * 16 + j];
        if (wnc_strcmp(filename, name) == 0) { kprint("File exists\n"); return; }
    }
    int slot = -1;
    for (i = 0; i < 32; i++) {
        char name[12] = {0};
        for (int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i * 16 + j];
        if (name[0] == 0) { slot = i; break; }
    }
    if (slot == -1) { kprint("Directory full\n"); return; }
    for (i = 0; i < 11 && filename[i]; i++) ((char*)dir_buf)[slot * 16 + i] = filename[i];
    ((char*)dir_buf)[slot * 16 + 11] = 0;
    int file_sector = 500 + slot + (current_dir_sector - 100) * 32;
    dir_buf[slot * 8 + 6] = file_sector;
    dir_buf[slot * 8 + 7] = 0;
    write_sector(current_dir_sector, dir_buf);
    uint16_t empty_buf[256];
    for (i = 0; i < 256; i++) empty_buf[i] = 0;
    write_sector(file_sector, empty_buf);
    kprint("File created: "); kprint(filename); kprint("\n");
}

static void wnc_write(const char* line) {
    char filename[WNKC_NAME_LEN] = {0};
    char content[WNKC_MAX_STRING] = {0};
    const char* p = line;
    while (*p == ' ') p++;
    int i = 0;
    while (*p && *p != ' ' && i < WNKC_NAME_LEN - 1) filename[i++] = *p++;
    while (*p == ' ') p++;
    if (*p == '"') {
        p++;
        i = 0;
        while (*p && *p != '"' && i < WNKC_MAX_STRING - 1) content[i++] = *p++;
    } else {
        i = 0;
        while (*p && *p != '\n' && i < WNKC_MAX_STRING - 1) content[i++] = *p++;
    }
    content[i] = 0;
    if (filename[0] == 0) { kprint("Usage: write <file> <content>\n"); return; }
    uint16_t dir_buf[256];
    read_sector(current_dir_sector, dir_buf);
    int slot = -1;
    for (i = 0; i < 32; i++) {
        char name[12] = {0};
        for (int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i * 16 + j];
        if (wnc_strcmp(filename, name) == 0) { slot = i; break; }
    }
    if (slot == -1) { kprint("File not found\n"); return; }
    int length = wnc_strlen(content);
    uint16_t data_buf[256] = {0};
    for (i = 0; i < length && i < 510; i++) {
        if (i % 2 == 0) data_buf[i / 2] = content[i];
        else data_buf[i / 2] |= (content[i] << 8);
    }
    int file_sector = dir_buf[slot * 8 + 6];
    write_sector(file_sector, data_buf);
    dir_buf[slot * 8 + 7] = length;
    write_sector(current_dir_sector, dir_buf);
    kprint("Written "); kprint_int(length); kprint(" bytes\n");
}

static void wnc_read(const char* line) {
    char filename[WNKC_NAME_LEN] = {0};
    const char* p = line;
    while (*p == ' ') p++;
    int i = 0;
    while (*p && *p != ' ' && *p != '\n' && i < WNKC_NAME_LEN - 1) filename[i++] = *p++;
    if (filename[0] == 0) { kprint("Usage: read <filename>\n"); return; }
    uint16_t dir_buf[256];
    read_sector(current_dir_sector, dir_buf);
    int slot = -1;
    for (i = 0; i < 32; i++) {
        char name[12] = {0};
        for (int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i * 16 + j];
        if (wnc_strcmp(filename, name) == 0) { slot = i; break; }
    }
    if (slot == -1) { kprint("File not found\n"); return; }
    int file_sector = dir_buf[slot * 8 + 6];
    int file_size = dir_buf[slot * 8 + 7];
    uint16_t data_buf[256];
    read_sector(file_sector, data_buf);
    kprint("\n=== "); kprint(filename); kprint(" ===\n\n");
    for (i = 0; i < file_size; i++) {
        char c;
        if (i % 2 == 0) c = data_buf[i / 2] & 0xFF;
        else c = (data_buf[i / 2] >> 8) & 0xFF;
        if (c >= 32 && c <= 126) { char s[2] = {c, 0}; kprint(s); }
    }
    kprint("\n\n");
}

static void wnc_delete(const char* line) {
    char filename[WNKC_NAME_LEN] = {0};
    const char* p = line;
    while (*p == ' ') p++;
    int i = 0;
    while (*p && *p != ' ' && *p != '\n' && i < WNKC_NAME_LEN - 1) filename[i++] = *p++;
    if (filename[0] == 0) { kprint("Usage: delete <filename>\n"); return; }
    uint16_t dir_buf[256];
    read_sector(current_dir_sector, dir_buf);
    int slot = -1;
    for (i = 0; i < 32; i++) {
        char name[12] = {0};
        for (int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i * 16 + j];
        if (wnc_strcmp(filename, name) == 0) { slot = i; break; }
    }
    if (slot == -1) { kprint("File not found\n"); return; }
    for (i = 0; i < 16; i++) ((char*)dir_buf)[slot * 16 + i] = 0;
    write_sector(current_dir_sector, dir_buf);
    kprint("Deleted: "); kprint(filename); kprint("\n");
}

static void wnc_copy(const char* line) {
    char src[WNKC_NAME_LEN] = {0};
    char dst[WNKC_NAME_LEN] = {0};
    const char* p = line;
    while (*p == ' ') p++;
    int i = 0;
    while (*p && *p != ' ' && i < WNKC_NAME_LEN - 1) src[i++] = *p++;
    while (*p == ' ') p++;
    i = 0;
    while (*p && *p != ' ' && *p != '\n' && i < WNKC_NAME_LEN - 1) dst[i++] = *p++;
    if (src[0] == 0 || dst[0] == 0) { kprint("Usage: copy <src> <dst>\n"); return; }
    uint16_t dir_buf[256];
    read_sector(current_dir_sector, dir_buf);
    int src_slot = -1, src_sector = 0, src_size = 0;
    for (i = 0; i < 32; i++) {
        char name[12] = {0};
        for (int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i * 16 + j];
        if (wnc_strcmp(src, name) == 0) { src_slot = i; src_sector = dir_buf[i * 8 + 6]; src_size = dir_buf[i * 8 + 7]; break; }
    }
    if (src_slot == -1) { kprint("Source not found\n"); return; }
    for (i = 0; i < 32; i++) {
        char name[12] = {0};
        for (int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i * 16 + j];
        if (wnc_strcmp(dst, name) == 0) { kprint("Destination exists\n"); return; }
    }
    int dst_slot = -1;
    for (i = 0; i < 32; i++) {
        char name[12] = {0};
        for (int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i * 16 + j];
        if (name[0] == 0) { dst_slot = i; break; }
    }
    if (dst_slot == -1) { kprint("Directory full\n"); return; }
    uint16_t data_buf[256];
    read_sector(src_sector, data_buf);
    int dst_sector = 500 + dst_slot + (current_dir_sector - 100) * 32;
    write_sector(dst_sector, data_buf);
    for (i = 0; i < 11 && dst[i]; i++) ((char*)dir_buf)[dst_slot * 16 + i] = dst[i];
    ((char*)dir_buf)[dst_slot * 16 + 11] = 0;
    dir_buf[dst_slot * 8 + 6] = dst_sector;
    dir_buf[dst_slot * 8 + 7] = src_size;
    write_sector(current_dir_sector, dir_buf);
    kprint("Copied\n");
}

static void wnc_move(const char* line) {
    char src[WNKC_NAME_LEN] = {0};
    char dst[WNKC_NAME_LEN] = {0};
    const char* p = line;
    while (*p == ' ') p++;
    int i = 0;
    while (*p && *p != ' ' && i < WNKC_NAME_LEN - 1) src[i++] = *p++;
    while (*p == ' ') p++;
    i = 0;
    while (*p && *p != ' ' && *p != '\n' && i < WNKC_NAME_LEN - 1) dst[i++] = *p++;
    if (src[0] == 0 || dst[0] == 0) { kprint("Usage: move <src> <dst>\n"); return; }
    uint16_t dir_buf[256];
    read_sector(current_dir_sector, dir_buf);
    int slot = -1;
    for (i = 0; i < 32; i++) {
        char name[12] = {0};
        for (int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i * 16 + j];
        if (wnc_strcmp(src, name) == 0) { slot = i; break; }
    }
    if (slot == -1) { kprint("Source not found\n"); return; }
    for (i = 0; i < 11 && dst[i]; i++) ((char*)dir_buf)[slot * 16 + i] = dst[i];
    for (i = wnc_strlen(dst); i < 11; i++) ((char*)dir_buf)[slot * 16 + i] = 0;
    write_sector(current_dir_sector, dir_buf);
    kprint("Moved\n");
}

static void wnc_execute_line(const char* line) {
    if (line[0] == 0 || line[0] == '#' || line[0] == ';') return;

    if (wnc_strncmp(line, "print", 5) == 0) {
        wnc_print(line + 5);
    } else if (wnc_strncmp(line, "input", 5) == 0) {
        wnc_input(line + 5);
    } else if (wnc_strncmp(line, "let", 3) == 0) {
        wnc_let(line + 3);
    } else if (wnc_strncmp(line, "array", 5) == 0) {
        wnc_array_cmd(line + 5);
    } else if (wnc_strncmp(line, "struct", 6) == 0) {
        wnc_struct_cmd(line + 6);
    } else if (wnc_strncmp(line, "func", 4) == 0) {
        wnc_func_cmd(line + 4);
    } else if (wnc_strncmp(line, "import", 6) == 0) {
        wnc_import_cmd(line + 6);
    } else if (wnc_strcmp(line, "return") == 0) {
        wnc_return_cmd("");
    } else if (wnc_strncmp(line, "return", 6) == 0) {
        wnc_return_cmd(line + 6);
    } else if (wnc_strncmp(line, "if", 2) == 0) {
        char condition[WNKC_MAX_COND] = {0};
        char code[WNKC_MAX_CODE] = {0};
        const char* p = line + 2;
        while (*p == ' ') p++;
        int i = 0;
        while (*p && *p != '{' && i < WNKC_MAX_COND - 1) condition[i++] = *p++;
        while (*p && *p != '{') p++;
        if (*p == '{') {
            p++;
            i = 0;
            int depth = 1;
            while (*p && depth > 0 && i < WNKC_MAX_CODE - 1) {
                if (*p == '{') depth++;
                if (*p == '}') depth--;
                if (depth > 0) code[i++] = *p;
                p++;
            }
        }
        if (wnc_check_condition(condition)) {
            char line_buf[WNKC_MAX_LINE];
            int line_pos = 0;
            for (int j = 0; code[j]; j++) {
                if (code[j] == ';' || code[j] == '\n') {
                    if (line_pos > 0) { line_buf[line_pos] = 0; wnc_execute_line(line_buf); line_pos = 0; }
                } else if (line_pos < WNKC_MAX_LINE - 1) {
                    line_buf[line_pos++] = code[j];
                }
            }
            if (line_pos > 0) { line_buf[line_pos] = 0; wnc_execute_line(line_buf); }
        }
    } else if (wnc_strncmp(line, "while", 5) == 0) {
        char condition[WNKC_MAX_COND] = {0};
        char code[WNKC_MAX_CODE] = {0};
        const char* p = line + 5;
        while (*p == ' ') p++;
        int i = 0;
        while (*p && *p != '{' && i < WNKC_MAX_COND - 1) condition[i++] = *p++;
        while (*p && *p != '{') p++;
        if (*p == '{') {
            p++;
            i = 0;
            int depth = 1;
            while (*p && depth > 0 && i < WNKC_MAX_CODE - 1) {
                if (*p == '{') depth++;
                if (*p == '}') depth--;
                if (depth > 0) code[i++] = *p;
                p++;
            }
        }
        int max_iter = WNKC_MAX_LOOP_ITER;
        wnc_break_flag = 0;
        while (wnc_check_condition(condition) && max_iter-- > 0 && !wnc_break_flag) {
            if (wnc_continue_flag) { wnc_continue_flag = 0; continue; }
            char line_buf[WNKC_MAX_LINE];
            int line_pos = 0;
            for (int j = 0; code[j]; j++) {
                if (code[j] == ';' || code[j] == '\n') {
                    if (line_pos > 0) { line_buf[line_pos] = 0; wnc_execute_line(line_buf); line_pos = 0; }
                } else if (line_pos < WNKC_MAX_LINE - 1) {
                    line_buf[line_pos++] = code[j];
                }
            }
            if (line_pos > 0) { line_buf[line_pos] = 0; wnc_execute_line(line_buf); }
        }
    } else if (wnc_strncmp(line, "for", 3) == 0) {
        char var_name[WNKC_NAME_LEN] = {0};
        int start = 0, end = 0, step = 1;
        char code[WNKC_MAX_CODE] = {0};
        const char* p = line + 3;
        while (*p == ' ') p++;
        int i = 0;
        while (*p && *p != '=' && *p != ' ' && i < WNKC_NAME_LEN - 1) var_name[i++] = *p++;
        while (*p == ' ' || *p == '=') p++;
        start = wnc_atoi(p);
        while ((*p >= '0' && *p <= '9') || *p == '-') p++;
        while (*p == ' ') p++;
        if (wnc_strncmp(p, "to", 2) == 0) p += 2;
        while (*p == ' ') p++;
        end = wnc_atoi(p);
        while ((*p >= '0' && *p <= '9') || *p == '-') p++;
        while (*p == ' ') p++;
        if (*p == '{') {
            p++;
            i = 0;
            int depth = 1;
            while (*p && depth > 0 && i < WNKC_MAX_CODE - 1) {
                if (*p == '{') depth++;
                if (*p == '}') depth--;
                if (depth > 0) code[i++] = *p;
                p++;
            }
        }
        wnc_break_flag = 0;
        for (int val = start; (step > 0 ? val <= end : val >= end) && !wnc_break_flag; val += step) {
            if (wnc_continue_flag) { wnc_continue_flag = 0; continue; }
            wnc_set_var(var_name, val);
            char line_buf[WNKC_MAX_LINE];
            int line_pos = 0;
            for (int j = 0; code[j]; j++) {
                if (code[j] == ';' || code[j] == '\n') {
                    if (line_pos > 0) { line_buf[line_pos] = 0; wnc_execute_line(line_buf); line_pos = 0; }
                } else if (line_pos < WNKC_MAX_LINE - 1) {
                    line_buf[line_pos++] = code[j];
                }
            }
            if (line_pos > 0) { line_buf[line_pos] = 0; wnc_execute_line(line_buf); }
        }
    } else if (wnc_strcmp(line, "break") == 0) {
        wnc_break_flag = 1;
    } else if (wnc_strcmp(line, "continue") == 0) {
        wnc_continue_flag = 1;
    } else if (wnc_strncmp(line, "graph", 5) == 0) {
        wnc_graph_cmd(line + 5);
    } else if (wnc_strncmp(line, "key", 3) == 0) {
        wnc_key_cmd(line + 3);
    } else if (wnc_strncmp(line, "run", 3) == 0) {
        const char* cmd = line + 3;
        while (*cmd == ' ') cmd++;
        if (cmd[0]) {
            char temp_cmd[WNKC_MAX_STRING];
            wnc_strcpy(temp_cmd, cmd);
            int dummy_ptr = 0;
            process_command(temp_cmd, dummy_ptr);
        }
    } else if (wnc_strcmp(line, "time") == 0) {
        extern int seconds;
        int h = (seconds / 3600) % 24;
        int m = (seconds / 60) % 60;
        int s = seconds % 60;
        if (h < 10) kprint("0"); kprint_int(h); kprint(":");
        if (m < 10) kprint("0"); kprint_int(m); kprint(":");
        if (s < 10) kprint("0"); kprint_int(s); kprint("\n");
    } else if (wnc_strcmp(line, "getkey") == 0) {
        while (!(inb(0x64) & 1));
        while (inb(0x64) & 1) inb(0x60);
    } else if (wnc_strncmp(line, "sleep", 5) == 0) {
        const char* p = line + 5;
        while (*p == ' ') p++;
        int sec = wnc_atoi(p);
        if (sec < 1) sec = 1;
        if (sec > 3600) sec = 3600;
        for (int i = 0; i < sec; i++) for (volatile int d = 0; d < 100000000; d++);
    } else if (wnc_strcmp(line, "rand") == 0) {
        int r = wnc_rand(1, 100);
        kprint_int(r); kprint("\n");
    } else if (wnc_strcmp(line, "vars") == 0) {
        kprint("\n=== VARIABLES ===\n");
        for (int i = 0; i < wnc_var_count; i++) {
            kprint("  "); kprint(wnc_vars[i].name); kprint(" = ");
            if (wnc_vars[i].type == WNKC_TYPE_NUMBER) kprint_int(wnc_vars[i].num_value);
            else { kprint("\""); kprint(wnc_vars[i].str_value); kprint("\""); }
            kprint("\n");
        }
    } else if (wnc_strcmp(line, "clear") == 0) {
        wnc_var_count = 0; wnc_array_count = 0; wnc_struct_count = 0;
        kprint("Cleared\n");
    } else if (wnc_strncmp(line, "log", 3) == 0) {
        const char* p = line + 3;
        while (*p == ' ') p++;
        if (wnc_strncmp(p, "on", 2) == 0) { wnc_log_enabled = 1; kprint("Logging on\n"); }
        else if (wnc_strncmp(p, "off", 3) == 0) { wnc_log_enabled = 0; kprint("Logging off\n"); }
        else kprint("Usage: log on/off\n");
    } else if (wnc_strncmp(line, "runscript", 9) == 0) {
        const char* p = line + 9;
        while (*p == ' ') p++;
        char filename[WNKC_NAME_LEN] = {0};
        int i = 0;
        while (*p && *p != ' ' && *p != '\n' && i < WNKC_NAME_LEN - 1) filename[i++] = *p++;
        wnc_execute_file(filename);
    } else if (wnc_strncmp(line, "mkdir", 5) == 0) {
        wnc_mkdir(line + 5);
    } else if (wnc_strncmp(line, "cd", 2) == 0) {
        wnc_cd(line + 2);
    } else if (wnc_strcmp(line, "pwd") == 0) {
        wnc_pwd("");
    } else if (wnc_strcmp(line, "ls") == 0) {
        wnc_ls("");
    } else if (wnc_strncmp(line, "create", 6) == 0) {
        wnc_create(line + 6);
    } else if (wnc_strncmp(line, "write", 5) == 0) {
        wnc_write(line + 5);
    } else if (wnc_strncmp(line, "read", 4) == 0) {
        wnc_read(line + 4);
    } else if (wnc_strncmp(line, "delete", 6) == 0) {
        wnc_delete(line + 6);
    } else if (wnc_strncmp(line, "copy", 4) == 0) {
        wnc_copy(line + 4);
    } else if (wnc_strncmp(line, "move", 4) == 0) {
        wnc_move(line + 4);
    } else {
        char func_name[WNKC_NAME_LEN] = {0};
        int i = 0;
        while (line[i] && line[i] != '(' && i < WNKC_NAME_LEN - 1) {
            func_name[i] = line[i];
            i++;
        }
        if (wnc_find_function(func_name) != -1) {
            wnc_call_function(func_name);
        } else {
            kprint_color("Unknown: ", TXT_RED);
            kprint(line);
            kprint("\n");
        }
    }
}

int wnc_execute(const char* code) {
    if (!code) return -1;
    wnc_error = 0;
    wnc_line = 1;
    wnc_break_flag = 0;
    wnc_continue_flag = 0;
    wnc_return_flag = 0;
    char line[WNKC_MAX_LINE];
    int pos = 0, line_pos = 0;
    while (code[pos] && !wnc_error && !wnc_return_flag) {
        char c = code[pos];
        if (c == '\n') {
            line[line_pos] = 0;
            if (line_pos > 0) wnc_execute_line(line);
            wnc_line++;
            line_pos = 0;
        } else if (line_pos < WNKC_MAX_LINE - 1) {
            line[line_pos++] = c;
        }
        pos++;
    }
    if (line_pos > 0) { line[line_pos] = 0; wnc_execute_line(line); }
    return wnc_error ? -1 : 0;
}

int wnc_execute_file(const char* filename) {
    if (!filename) return -1;
    uint16_t dir_buf[256];
    read_sector(current_dir_sector, dir_buf);
    int slot = -1;
    for (int i = 0; i < 32; i++) {
        char name[12] = {0};
        for (int j = 0; j < 11; j++) name[j] = ((char*)dir_buf)[i * 16 + j];
        if (wnc_strcmp(filename, name) == 0) { slot = i; break; }
    }
    if (slot == -1) { kprint("File not found: "); kprint(filename); kprint("\n"); return -1; }
    int sector = dir_buf[slot * 8 + 6];
    int size = dir_buf[slot * 8 + 7];
    uint16_t data_buf[256];
    read_sector(sector, data_buf);
    static char file_buffer[WNKC_MAX_BODY];
    int pos = 0;
    for (int i = 0; i < size && pos < WNKC_MAX_BODY - 1; i++) {
        char c;
        if (i % 2 == 0) c = data_buf[i / 2] & 0xFF;
        else c = (data_buf[i / 2] >> 8) & 0xFF;
        if (c != 0 && c != '\r') file_buffer[pos++] = c;
    }
    file_buffer[pos] = 0;
    kprint("Running: "); kprint(filename); kprint("\n");
    return wnc_execute(file_buffer);
}

void wnc_init(void) {
    wnc_var_count = 0;
    wnc_array_count = 0;
    wnc_struct_count = 0;
    wnc_function_count = 0;
    wnc_import_count = 0;
    wnc_error = 0;
    wnc_line = 1;
    wnc_log_enabled = 1;
}

void wnc_set_dir(uint16_t dir) {
    current_dir_sector = dir;
}