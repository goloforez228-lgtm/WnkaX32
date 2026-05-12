#include "tcc.h"
#include "video.h"
#include <stdint.h>
#include "kernel_stubs.h"
#include <stddef.h>

static char heap[8 * 1024 * 1024];
static uint32_t heap_ptr = 0;

static void* tcc_malloc(uint32_t size) {
    if(heap_ptr + size > sizeof(heap)) return NULL;
    void* ptr = &heap[heap_ptr];
    heap_ptr += size;
    return ptr;
}

static void* tcc_malloc_zero(uint32_t size) {
    void* ptr = tcc_malloc(size);
    if(ptr) {
        for(uint32_t i = 0; i < size; i++) ((char*)ptr)[i] = 0;
    }
    return ptr;
}

static void tcc_free(void* ptr) { (void)ptr; }

static int tcc_strlen(const char* s) {
    if(!s) return 0;
    int len = 0;
    while(s[len]) len++;
    return len;
}

static int tcc_strcmp(const char* s1, const char* s2) {
    if(!s1 || !s2) return -1;
    while(*s1 && *s2 && *s1 == *s2) { s1++; s2++; }
    return *s1 - *s2;
}

static void tcc_strcpy(char* dest, const char* src) {
    if(!dest || !src) return;
    while(*src) { *dest++ = *src++; }
    *dest = '\0';
}

static char* tcc_strdup(const char* s) {
    if(!s) return NULL;
    int len = tcc_strlen(s) + 1;
    char* new_s = (char*)tcc_malloc(len);
    if(new_s) tcc_strcpy(new_s, s);
    return new_s;
}

enum {
    TOK_EOF = 0,
    TOK_INT, TOK_CHAR, TOK_VOID, TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FOR, TOK_RETURN,
    TOK_BREAK, TOK_CONTINUE, TOK_IDENT, TOK_NUMBER, TOK_STRING,
    TOK_EQ, TOK_NE, TOK_LE, TOK_GE, TOK_LAND, TOK_LOR,
    TOK_INC, TOK_DEC, TOK_PTR, TOK_SHL, TOK_SHR,
    TOK_ADD_EQ, TOK_SUB_EQ, TOK_MUL_EQ, TOK_DIV_EQ, TOK_MOD_EQ,
    TOK_AND_EQ, TOK_OR_EQ, TOK_XOR_EQ, TOK_SHL_EQ, TOK_SHR_EQ,
};

typedef struct Token {
    int type;
    int val;
    char* str;
    struct Token* next;
} Token;

typedef struct Sym {
    char* name;
    int type;
    int size;
    struct Sym* next;
} Sym;

typedef struct TCCState {
    Token* tokens;
    Token* current_token;
    Sym* symbols;
    char* output_buffer;
    int output_size;
    void* code;
    int depth;
} TCCState;

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static int is_digit(char c) { return c >= '0' && c <= '9'; }
static int is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }

static Token* new_token(TCCState* s, int type, int val, char* str, int len) {
    Token* t = (Token*)tcc_malloc_zero(sizeof(Token));
    if(!t) return NULL;
    t->type = type;
    t->val = val;
    if(str && len > 0) {
        t->str = (char*)tcc_malloc(len + 1);
        if(t->str) {
            for(int i = 0; i < len; i++) t->str[i] = str[i];
            t->str[len] = '\0';
        }
    }
    return t;
}

static int token_type(const char* s, int len) {
    if(len == 3 && tcc_strcmp(s, "int") == 0) return TOK_INT;
    if(len == 4 && tcc_strcmp(s, "char") == 0) return TOK_CHAR;
    if(len == 4 && tcc_strcmp(s, "void") == 0) return TOK_VOID;
    if(len == 2 && tcc_strcmp(s, "if") == 0) return TOK_IF;
    if(len == 4 && tcc_strcmp(s, "else") == 0) return TOK_ELSE;
    if(len == 5 && tcc_strcmp(s, "while") == 0) return TOK_WHILE;
    if(len == 3 && tcc_strcmp(s, "for") == 0) return TOK_FOR;
    if(len == 6 && tcc_strcmp(s, "return") == 0) return TOK_RETURN;
    return TOK_IDENT;
}

static Token* lex(TCCState* s, const char* input, int* pos) {
    while(input[*pos] && is_space(input[*pos])) (*pos)++;
    if(!input[*pos]) return new_token(s, TOK_EOF, 0, NULL, 0);
    
    char c = input[*pos];
    
    if(is_digit(c)) {
        int start = *pos;
        while(is_digit(input[*pos])) (*pos)++;
        int len = *pos - start;
        char* num_str = (char*)tcc_malloc(len + 1);
        if(!num_str) return NULL;
        for(int i = 0; i < len; i++) num_str[i] = input[start + i];
        num_str[len] = '\0';
        int val = 0;
        for(int i = 0; i < len; i++) val = val * 10 + (num_str[i] - '0');
        tcc_free(num_str);
        return new_token(s, TOK_NUMBER, val, NULL, 0);
    }
    
    if(is_alpha(c)) {
        int start = *pos;
        while(is_alpha(input[*pos]) || is_digit(input[*pos])) (*pos)++;
        int len = *pos - start;
        char* ident = (char*)tcc_malloc(len + 1);
        if(!ident) return NULL;
        for(int i = 0; i < len; i++) ident[i] = input[start + i];
        ident[len] = '\0';
        int type = token_type(ident, len);
        Token* t = new_token(s, type, 0, ident, len);
        tcc_free(ident);
        return t;
    }
    
    if(c == '"') {
        (*pos)++;
        int start = *pos;
        while(input[*pos] && input[*pos] != '"') (*pos)++;
        int len = *pos - start;
        char* str_lit = (char*)tcc_malloc(len + 1);
        if(!str_lit) return NULL;
        for(int i = 0; i < len; i++) str_lit[i] = input[start + i];
        str_lit[len] = '\0';
        Token* t = new_token(s, TOK_STRING, 0, str_lit, len);
        if(input[*pos] == '"') (*pos)++;
        return t;
    }
    
    (*pos)++;
    switch(c) {
        case '+':
            if(input[*pos] == '+') { (*pos)++; return new_token(s, TOK_INC, 0, NULL, 0); }
            if(input[*pos] == '=') { (*pos)++; return new_token(s, TOK_ADD_EQ, 0, NULL, 0); }
            return new_token(s, '+', 0, NULL, 0);
        case '-':
            if(input[*pos] == '-') { (*pos)++; return new_token(s, TOK_DEC, 0, NULL, 0); }
            if(input[*pos] == '=') { (*pos)++; return new_token(s, TOK_SUB_EQ, 0, NULL, 0); }
            if(input[*pos] == '>') { (*pos)++; return new_token(s, TOK_PTR, 0, NULL, 0); }
            return new_token(s, '-', 0, NULL, 0);
        case '*':
            if(input[*pos] == '=') { (*pos)++; return new_token(s, TOK_MUL_EQ, 0, NULL, 0); }
            return new_token(s, '*', 0, NULL, 0);
        case '/':
            if(input[*pos] == '=') { (*pos)++; return new_token(s, TOK_DIV_EQ, 0, NULL, 0); }
            return new_token(s, '/', 0, NULL, 0);
        case '=':
            if(input[*pos] == '=') { (*pos)++; return new_token(s, TOK_EQ, 0, NULL, 0); }
            return new_token(s, '=', 0, NULL, 0);
        case '!':
            if(input[*pos] == '=') { (*pos)++; return new_token(s, TOK_NE, 0, NULL, 0); }
            return new_token(s, '!', 0, NULL, 0);
        case '<':
            if(input[*pos] == '=') { (*pos)++; return new_token(s, TOK_LE, 0, NULL, 0); }
            if(input[*pos] == '<') { (*pos)++; return new_token(s, TOK_SHL, 0, NULL, 0); }
            return new_token(s, '<', 0, NULL, 0);
        case '>':
            if(input[*pos] == '=') { (*pos)++; return new_token(s, TOK_GE, 0, NULL, 0); }
            if(input[*pos] == '>') { (*pos)++; return new_token(s, TOK_SHR, 0, NULL, 0); }
            return new_token(s, '>', 0, NULL, 0);
        case '&':
            if(input[*pos] == '&') { (*pos)++; return new_token(s, TOK_LAND, 0, NULL, 0); }
            return new_token(s, '&', 0, NULL, 0);
        case '|':
            if(input[*pos] == '|') { (*pos)++; return new_token(s, TOK_LOR, 0, NULL, 0); }
            return new_token(s, '|', 0, NULL, 0);
        case '(': return new_token(s, '(', 0, NULL, 0);
        case ')': return new_token(s, ')', 0, NULL, 0);
        case '{': return new_token(s, '{', 0, NULL, 0);
        case '}': return new_token(s, '}', 0, NULL, 0);
        case ';': return new_token(s, ';', 0, NULL, 0);
        case ',': return new_token(s, ',', 0, NULL, 0);
        default: return new_token(s, c, 0, NULL, 0);
    }
}

static void tokenize(TCCState* s, const char* input) {
    int pos = 0;
    Token* first = NULL;
    Token* last = NULL;
    while(1) {
        Token* t = lex(s, input, &pos);
        if(!t) break;
        if(!first) first = t;
        if(last) last->next = t;
        last = t;
        if(t->type == TOK_EOF) break;
    }
    s->tokens = first;
    s->current_token = first;
}

static void next_token(TCCState* s) {
    if(s->current_token) s->current_token = s->current_token->next;
}

static int accept(TCCState* s, int type) {
    if(s->current_token && s->current_token->type == type) {
        next_token(s);
        return 1;
    }
    return 0;
}

static Sym* find_sym(TCCState* s, const char* name) {
    Sym* sym = s->symbols;
    while(sym) {
        if(sym->name && tcc_strcmp(sym->name, name) == 0) return sym;
        sym = sym->next;
    }
    return NULL;
}

static Sym* add_sym(TCCState* s, const char* name, int type, int size) {
    Sym* sym = (Sym*)tcc_malloc_zero(sizeof(Sym));
    if(!sym) return NULL;
    sym->name = tcc_strdup(name);
    sym->type = type;
    sym->size = size;
    sym->next = s->symbols;
    s->symbols = sym;
    return sym;
}

static void gen_prolog(TCCState* s) {
    s->output_buffer = (char*)tcc_malloc(1024);
    if(!s->output_buffer) return;
    s->output_size = 0;
    s->output_buffer[s->output_size++] = 0x55;
    s->output_buffer[s->output_size++] = 0x89;
    s->output_buffer[s->output_size++] = 0xE5;
}

static void gen_epilog(TCCState* s) {
    s->output_buffer[s->output_size++] = 0x58;
    s->output_buffer[s->output_size++] = 0x89;
    s->output_buffer[s->output_size++] = 0xEC;
    s->output_buffer[s->output_size++] = 0x5D;
    s->output_buffer[s->output_size++] = 0xC3;
}

static void gen_push_imm(TCCState* s, int val) {
    s->output_buffer[s->output_size++] = 0x68;
    s->output_buffer[s->output_size++] = val & 0xFF;
    s->output_buffer[s->output_size++] = (val >> 8) & 0xFF;
    s->output_buffer[s->output_size++] = (val >> 16) & 0xFF;
    s->output_buffer[s->output_size++] = (val >> 24) & 0xFF;
}

static void gen_add(TCCState* s) {
    s->output_buffer[s->output_size++] = 0x58;
    s->output_buffer[s->output_size++] = 0x59;
    s->output_buffer[s->output_size++] = 0x01;
    s->output_buffer[s->output_size++] = 0xC8;
    s->output_buffer[s->output_size++] = 0x50;
}

static void gen_sub(TCCState* s) {
    s->output_buffer[s->output_size++] = 0x58;
    s->output_buffer[s->output_size++] = 0x59;
    s->output_buffer[s->output_size++] = 0x29;
    s->output_buffer[s->output_size++] = 0xC8;
    s->output_buffer[s->output_size++] = 0x50;
}

static void gen_mul(TCCState* s) {
    s->output_buffer[s->output_size++] = 0x58;
    s->output_buffer[s->output_size++] = 0x59;
    s->output_buffer[s->output_size++] = 0xF7;
    s->output_buffer[s->output_size++] = 0xE9;
    s->output_buffer[s->output_size++] = 0x50;
}

static void gen_div(TCCState* s) {
    s->output_buffer[s->output_size++] = 0x58;
    s->output_buffer[s->output_size++] = 0x59;
    s->output_buffer[s->output_size++] = 0x99;
    s->output_buffer[s->output_size++] = 0xF7;
    s->output_buffer[s->output_size++] = 0xF9;
    s->output_buffer[s->output_size++] = 0x50;
}

static int parse_expression(TCCState* s);
static int parse_primary(TCCState* s);

static int parse_primary(TCCState* s) {
    if(s->depth > 100) return 0;
    Token* t = s->current_token;
    if(accept(s, TOK_NUMBER)) {
        gen_push_imm(s, t->val);
        return 1;
    }
    if(accept(s, '(')) {
        s->depth++;
        int r = parse_expression(s);
        s->depth--;
        accept(s, ')');
        return r;
    }
    return 0;
}

static int parse_mul(TCCState* s) {
    if(!parse_primary(s)) return 0;
    while(1) {
        if(accept(s, '*')) { if(!parse_primary(s)) return 0; gen_mul(s); }
        else if(accept(s, '/')) { if(!parse_primary(s)) return 0; gen_div(s); }
        else break;
    }
    return 1;
}

static int parse_add(TCCState* s) {
    if(!parse_mul(s)) return 0;
    while(1) {
        if(accept(s, '+')) { if(!parse_mul(s)) return 0; gen_add(s); }
        else if(accept(s, '-')) { if(!parse_mul(s)) return 0; gen_sub(s); }
        else break;
    }
    return 1;
}

static int parse_expression(TCCState* s) { return parse_add(s); }

static int parse_statement(TCCState* s) {
    if(accept(s, TOK_RETURN)) {
        if(!parse_expression(s)) return 0;
        accept(s, ';');
        return 1;
    }
    if(accept(s, ';')) return 1;
    if(accept(s, '{')) {
        while(!accept(s, '}')) {
            if(!parse_statement(s)) return 0;
        }
        return 1;
    }
    if(accept(s, TOK_INT)) {
        if(accept(s, TOK_IDENT)) {
            const char* name = s->current_token ? s->current_token->str : "";
            if(name) add_sym(s, name, TOK_INT, 4);
            accept(s, ';');
            return 1;
        }
    }
    return 0;
}

static int parse_function(TCCState* s) {
    if(accept(s, TOK_INT) || accept(s, TOK_VOID)) {
        if(accept(s, TOK_IDENT)) {
            if(accept(s, '(')) {
                accept(s, ')');
                gen_prolog(s);
                if(accept(s, '{')) {
                    while(!accept(s, '}')) {
                        if(!parse_statement(s)) return 0;
                    }
                }
                gen_epilog(s);
                return 1;
            }
        }
    }
    return 0;
}

TCCState* tcc_new(void) {
    TCCState* s = (TCCState*)tcc_malloc_zero(sizeof(TCCState));
    if(!s) return NULL;
    s->depth = 0;
    return s;
}

void tcc_delete(TCCState* s) { (void)s; }

int tcc_compile_string(TCCState* s, const char* str) {
    if(!s || !str) return -1;
    
    tokenize(s, str);
    
    while(s->current_token && s->current_token->type != TOK_EOF) {
        if(!parse_function(s)) next_token(s);
    }
    return 0;
}

int tcc_compile_file(TCCState* s, const char* filename) {
    (void)s; (void)filename;
    return -1;
}

int tcc_relocate(TCCState* s, void* ptr) {
    if(!s) return -1;
    if(!s->output_buffer || s->output_size == 0) return -1;
    if(ptr) {
        for(int i = 0; i < s->output_size; i++) ((char*)ptr)[i] = s->output_buffer[i];
        s->code = ptr;
    } else {
        s->code = tcc_malloc(s->output_size);
        if(!s->code) return -1;
        for(int i = 0; i < s->output_size; i++) ((char*)s->code)[i] = s->output_buffer[i];
    }
    return 0;
}

int tcc_run(TCCState* s, int argc, char** argv) {
    if(!s || !s->code) return -1;
    
    if(s->output_size > 0) {
        uint8_t last_byte = ((uint8_t*)s->code)[s->output_size - 1];
        if(last_byte != 0xC3) return -2;
    }
    
    int (*main_func)(int, char**) = (int (*)(int, char**))s->code;
    return main_func(argc, argv);
}

void* tcc_get_symbol(TCCState* s, const char* name) {
    (void)s; (void)name;
    return NULL;
}

void tcc_add_include_path(TCCState* s, const char* path) { (void)s; (void)path; }
void tcc_add_library_path(TCCState* s, const char* path) { (void)s; (void)path; }