#include "wnkc.h"
#include "video.h"
#include "ata.h"
#include "graph.h"
#include "string_utils.h"
#include <stdint.h>

extern "C" void process_command(char* buf, int& ptr);

#define NULL 0
#define WNKC_MAX_ARRAYS 32
#define WNKC_MAX_STRUCTS 16
#define WNKC_MAX_FUNCTIONS 32
#define WNKC_MAX_IMPORTS 16
#define WNKC_MAX_VARS 64
#define WNKC_MAX_LINE 512

#define WNKC_TYPE_NUMBER 0
#define WNKC_TYPE_STRING 1
#define WNKC_TYPE_ARRAY 2
#define WNKC_TYPE_STRUCT 3

typedef struct {
    char name[32];
    int values[256];
    int size;
} wnc_array_t;

typedef struct {
    char name[32];
    char fields[16][32];
    int field_values[16];
    int field_count;
} wnc_struct_t;

typedef struct {
    char name[32];
    char params[8][32];
    int param_count;
    char body[4096];
    int is_static;
    int is_void;
} wnc_function_t;

static uint16_t current_dir_sector = 100;
static wnc_var_t wnc_vars[WNKC_MAX_VARS];
static wnc_array_t wnc_arrays[WNKC_MAX_ARRAYS];
static wnc_struct_t wnc_structs[WNKC_MAX_STRUCTS];
static wnc_function_t wnc_functions[WNKC_MAX_FUNCTIONS];
static char wnc_imported_files[WNKC_MAX_IMPORTS][256];
static int wnc_var_count = 0;
static int wnc_array_count = 0;
static int wnc_struct_count = 0;
static int wnc_function_count = 0;
static int wnc_import_count = 0;
static int wnc_error = 0;
static int wnc_line = 1;
static int wnc_log_enabled = 1;
static int wnc_break_flag = 0;
static int wnc_continue_flag = 0;
static int wnc_return_flag = 0;
static int wnc_return_value = 0;
static int wnc_in_function = 0;

static void kprint_char(char c) { char s[2] = {c, 0}; kprint(s); }
static int wnc_strlen(const char* s) { int l=0; while(s[l])l++; return l; }
static int wnc_strcmp(const char* a, const char* b) { while(*a && *b && *a==*b){a++;b++;} return *a-*b; }
static int wnc_strncmp(const char* a, const char* b, int n) { for(int i=0;i<n;i++){if(a[i]!=b[i])return a[i]-b[i]; if(a[i]==0)return 0;} return 0; }
static void wnc_strcpy(char* d, const char* s) { while(*s) *d++ = *s++; *d=0; }
static int wnc_atoi(const char* s) { int r=0, sign=1; if(*s=='-'){sign=-1;s++;} while(*s>='0'&&*s<='9'){r=r*10+(*s-'0');s++;} return r*sign; }
static void wnc_itoa(int n, char* str) { if(n==0){str[0]='0';str[1]=0;return;} char t[16]; int i=0,s=n; if(n<0)n=-n; while(n){t[i++]='0'+(n%10);n/=10;} int j=0; if(s<0)str[j++]='-'; while(i>0)str[j++]=t[--i]; str[j]=0; }
static int wnc_rand(int min, int max) { static unsigned int seed=12345; seed=seed*1103515245+12345; return min+(seed%(max-min+1)); }
static const char* wnc_strstr(const char* h, const char* n) { if(!h||!n)return NULL; int nl=wnc_strlen(n); for(int i=0;h[i];i++){int m=1;for(int j=0;j<nl;j++){if(h[i+j]!=n[j]){m=0;break;}}if(m)return&h[i];} return NULL; }

static int wnc_find_var(const char* name) { for(int i=0;i<wnc_var_count;i++) if(wnc_strcmp(wnc_vars[i].name,name)==0) return i; return -1; }
static void wnc_set_var(const char* name, int value) { int i=wnc_find_var(name); if(i==-1&&wnc_var_count<WNKC_MAX_VARS){i=wnc_var_count++;wnc_strcpy(wnc_vars[i].name,name);} if(i!=-1){wnc_vars[i].num_value=value; wnc_vars[i].type=WNKC_TYPE_NUMBER;} }
static void wnc_set_var_str(const char* name, const char* value) { int i=wnc_find_var(name); if(i==-1&&wnc_var_count<WNKC_MAX_VARS){i=wnc_var_count++;wnc_strcpy(wnc_vars[i].name,name);} if(i!=-1){wnc_strcpy(wnc_vars[i].str_value,value); wnc_vars[i].type=WNKC_TYPE_STRING;} }
static int wnc_get_var(const char* name) { int i=wnc_find_var(name); if(i!=-1&&wnc_vars[i].type==WNKC_TYPE_NUMBER) return wnc_vars[i].num_value; return 0; }
static const char* wnc_get_var_str(const char* name) { int i=wnc_find_var(name); if(i!=-1&&wnc_vars[i].type==WNKC_TYPE_STRING) return wnc_vars[i].str_value; return ""; }

static int wnc_find_array(const char* name) { for(int i=0;i<wnc_array_count;i++) if(wnc_strcmp(wnc_arrays[i].name,name)==0) return i; return -1; }
static int wnc_create_array(const char* name, int size) { if(size<1)size=1; if(size>256)size=256; int i=wnc_find_array(name); if(i!=-1){wnc_arrays[i].size=size; for(int j=0;j<size;j++)wnc_arrays[i].values[j]=0; return i;} if(wnc_array_count>=WNKC_MAX_ARRAYS)return -1; wnc_strcpy(wnc_arrays[wnc_array_count].name,name); wnc_arrays[wnc_array_count].size=size; for(int j=0;j<size;j++)wnc_arrays[wnc_array_count].values[j]=0; wnc_array_count++; return wnc_array_count-1; }
static void wnc_array_set(const char* name, int idx, int val) { int i=wnc_find_array(name); if(i==-1||idx<0||idx>=wnc_arrays[i].size){kprint_color("Array index error\n",TXT_RED); return;} wnc_arrays[i].values[idx]=val; }
static int wnc_array_get(const char* name, int idx) { int i=wnc_find_array(name); if(i==-1||idx<0||idx>=wnc_arrays[i].size){kprint_color("Array index error\n",TXT_RED); return 0;} return wnc_arrays[i].values[idx]; }
static void wnc_array_print(const char* name) { int i=wnc_find_array(name); if(i==-1){kprint_color("Array not found\n",TXT_RED); return;} kprint(name); kprint(" = ["); for(int j=0;j<wnc_arrays[i].size;j++){kprint_int(wnc_arrays[i].values[j]); if(j<wnc_arrays[i].size-1)kprint(", ");} kprint("]\n"); }

static int wnc_find_struct(const char* name) { for(int i=0;i<wnc_struct_count;i++) if(wnc_strcmp(wnc_structs[i].name,name)==0) return i; return -1; }
static int wnc_create_struct(const char* name) { if(wnc_find_struct(name)!=-1)return -1; if(wnc_struct_count>=WNKC_MAX_STRUCTS)return -1; wnc_strcpy(wnc_structs[wnc_struct_count].name,name); wnc_structs[wnc_struct_count].field_count=0; wnc_struct_count++; return wnc_struct_count-1; }
static void wnc_struct_add_field(const char* sname, const char* fname) { int i=wnc_find_struct(sname); if(i==-1){kprint_color("Struct not found\n",TXT_RED); return;} if(wnc_structs[i].field_count<16){wnc_strcpy(wnc_structs[i].fields[wnc_structs[i].field_count],fname); wnc_structs[i].field_values[wnc_structs[i].field_count]=0; wnc_structs[i].field_count++;} }
static void wnc_struct_set(const char* sname, const char* fname, int val) { int i=wnc_find_struct(sname); if(i==-1)return; for(int j=0;j<wnc_structs[i].field_count;j++){if(wnc_strcmp(wnc_structs[i].fields[j],fname)==0){wnc_structs[i].field_values[j]=val; return;}} }
static int wnc_struct_get(const char* sname, const char* fname) { int i=wnc_find_struct(sname); if(i==-1)return 0; for(int j=0;j<wnc_structs[i].field_count;j++){if(wnc_strcmp(wnc_structs[i].fields[j],fname)==0)return wnc_structs[i].field_values[j];} return 0; }
static void wnc_struct_print(const char* name) { int i=wnc_find_struct(name); if(i==-1){kprint_color("Struct not found\n",TXT_RED); return;} kprint(name); kprint(" = { "); for(int j=0;j<wnc_structs[i].field_count;j++){kprint(wnc_structs[i].fields[j]); kprint(":"); kprint_int(wnc_structs[i].field_values[j]); if(j<wnc_structs[i].field_count-1)kprint(", ");} kprint(" }\n"); }

static int wnc_find_function(const char* name) { for(int i=0;i<wnc_function_count;i++) if(wnc_strcmp(wnc_functions[i].name,name)==0) return i; return -1; }
static int wnc_add_function(const char* name, const char* params, const char* body, int is_static, int is_void) { if(wnc_find_function(name)!=-1&&!is_static)return -1; if(wnc_function_count>=WNKC_MAX_FUNCTIONS)return -1; wnc_strcpy(wnc_functions[wnc_function_count].name,name); wnc_functions[wnc_function_count].param_count=0; wnc_functions[wnc_function_count].is_static=is_static; wnc_functions[wnc_function_count].is_void=is_void; wnc_strcpy(wnc_functions[wnc_function_count].body,body); return wnc_function_count++; }
static int wnc_call_function(const char* name) { int i=wnc_find_function(name); if(i==-1){kprint_color("Function not found: ",TXT_RED); kprint(name); kprint("\n"); return 0;} int saved_return=wnc_return_flag; int saved_value=wnc_return_value; wnc_return_flag=0; wnc_in_function=1; wnc_execute(wnc_functions[i].body); wnc_in_function=0; int result=wnc_return_value; if(!saved_return)wnc_return_flag=0; wnc_return_value=saved_value; return result; }

static int wnc_is_imported(const char* filename) { for(int i=0;i<wnc_import_count;i++) if(wnc_strcmp(wnc_imported_files[i],filename)==0) return 1; return 0; }
static void wnc_import_file(const char* filename) { if(wnc_is_imported(filename)){kprint_color("Already imported: ",TXT_YELLOW); kprint(filename); kprint("\n"); return;} if(wnc_import_count>=WNKC_MAX_IMPORTS){kprint_color("Too many imports\n",TXT_RED); return;} wnc_strcpy(wnc_imported_files[wnc_import_count],filename); wnc_import_count++; kprint_color("Importing: ",TXT_CYAN); kprint(filename); kprint("\n"); wnc_execute_file(filename); }

static int wnc_eval_expr(const char* expr) {
    int a=0,b=0; char op=0; int i=0,sign=1;
    while(expr[i]==' ')i++;
    if(expr[i]=='-'){sign=-1;i++;}
    if(expr[i]>='0'&&expr[i]<='9'){while(expr[i]>='0'&&expr[i]<='9'){a=a*10+(expr[i]-'0');i++;}a*=sign;}
    else if((expr[i]>='a'&&expr[i]<='z')||(expr[i]>='A'&&expr[i]<='Z')||expr[i]=='_'){
        int s=i;while(expr[i]&&expr[i]!=' '&&expr[i]!='+'&&expr[i]!='-'&&expr[i]!='*'&&expr[i]!='/')i++;
        char vn[32]={0};for(int j=0;j<i-s&&j<31;j++)vn[j]=expr[s+j];a=wnc_get_var(vn)*sign;}
    while(expr[i]==' ')i++;
    if(expr[i]=='+'||expr[i]=='-'||expr[i]=='*'||expr[i]=='/'){op=expr[i];i++;}else return a;
    while(expr[i]==' ')i++;
    sign=1;if(expr[i]=='-'){sign=-1;i++;}
    if(expr[i]>='0'&&expr[i]<='9'){while(expr[i]>='0'&&expr[i]<='9'){b=b*10+(expr[i]-'0');i++;}b*=sign;}
    else if((expr[i]>='a'&&expr[i]<='z')||(expr[i]>='A'&&expr[i]<='Z')||expr[i]=='_'){
        int s=i;while(expr[i]&&expr[i]!=' '&&expr[i]!='+'&&expr[i]!='-'&&expr[i]!='*'&&expr[i]!='/')i++;
        char vn[32]={0};for(int j=0;j<i-s&&j<31;j++)vn[j]=expr[s+j];b=wnc_get_var(vn)*sign;}
    switch(op){case '+' :return a+b;case '-' :return a-b;case '*' :return a*b;case '/' :if(b!=0)return a/b;return 0;}return a;
}

static int wnc_check_condition(const char* cond) {
    char var[32]={0},op[4]={0},val[32]={0};
    const char* p=cond;while(*p==' ')p++;if(*p=='(')p++;
    int i=0;if(*p=='$')p++;while(*p&&*p!=' '&&i<31)var[i++]=*p++;var[i]=0;while(*p==' ')p++;
    i=0;while(*p&&*p!=' '&&i<3)op[i++]=*p++;op[i]=0;while(*p==' ')p++;
    i=0;int is_str=0;if(*p=='"'){is_str=1;p++;while(*p&&*p!='"'&&i<31)val[i++]=*p++;val[i]=0;}
    else{while(*p&&*p!=' '&&*p!=')'&&i<31)val[i++]=*p++;val[i]=0;}
    int vv=wnc_get_var(var),cv=wnc_atoi(val);const char* vs=wnc_get_var_str(var);
    if(wnc_strcmp(op,"==")==0){if(is_str)return wnc_strcmp(vs,val)==0;else return vv==cv;}
    if(wnc_strcmp(op,"!=")==0){if(is_str)return wnc_strcmp(vs,val)!=0;else return vv!=cv;}
    if(wnc_strcmp(op,"<")==0)return vv<cv;if(wnc_strcmp(op,">")==0)return vv>cv;
    if(wnc_strcmp(op,"<=")==0)return vv<=cv;if(wnc_strcmp(op,">=")==0)return vv>=cv;
    return 0;
}

static void wnc_print(const char* line) {
    const char* p=line;while(*p==' ')p++;
    while(*p){
        if(*p=='$'){p++;char vn[32]={0};int i=0;while(*p&&*p!=' '&&*p!='\n'&&i<31)vn[i++]=*p++;vn[i]=0;
            int idx=wnc_find_var(vn);if(idx!=-1){if(wnc_vars[idx].type==WNKC_TYPE_NUMBER)kprint_int(wnc_vars[idx].num_value);
            else kprint(wnc_vars[idx].str_value);}}
        else if(*p=='\\'&&*(p+1)=='n'){kprint("\n");p+=2;}
        else{char s[2]={*p,0};kprint(s);p++;}}
    kprint("\n");
}

static void wnc_input(const char* line) {
    char vn[32]={0};const char* p=line;while(*p==' ')p++;
    int i=0;while(*p&&*p!=' '&&*p!='\n'&&i<31)vn[i++]=*p++;vn[i]=0;
    kprint("> ");char buf[256]={0};int pos=0;
    while(1){if(inb(0x64)&1){uint8_t sc=inb(0x60);if(sc==0x1C)break;if(sc==0x0E&&pos>0){pos--;kprint("\b \b");}
        else if(sc>=0x02&&sc<=0x0B&&pos<255){buf[pos++]="1234567890"[sc-0x02];kprint_char(buf[pos-1]);}
        else if(sc>=0x10&&sc<=0x19&&pos<255){buf[pos++]="qwertyuiop"[sc-0x10];kprint_char(buf[pos-1]);}
        else if(sc>=0x1E&&sc<=0x26&&pos<255){buf[pos++]="asdfghjkl"[sc-0x1E];kprint_char(buf[pos-1]);}
        else if(sc>=0x2C&&sc<=0x32&&pos<255){buf[pos++]="zxcvbnm"[sc-0x2C];kprint_char(buf[pos-1]);}
        else if(sc==0x39&&pos<255){buf[pos++]=' ';kprint_char(' ');}}}
    buf[pos]=0;kprint("\n");int is_num=1;for(int j=0;buf[j];j++)if(buf[j]<'0'||buf[j]>'9'){is_num=0;break;}
    if(is_num)wnc_set_var(vn,wnc_atoi(buf));else wnc_set_var_str(vn,buf);
}

static void wnc_let(const char* line) {
    char vn[32]={0},expr[128]={0};const char* p=line;while(*p==' ')p++;
    int i=0;while(*p&&*p!='='&&*p!=' '&&i<31)vn[i++]=*p++;vn[i]=0;
    while(*p&&*p!='=')p++;if(*p=='=')p++;while(*p==' ')p++;
    if(*p=='"'){p++;char sv[256]={0};i=0;while(*p&&*p!='"'&&i<255)sv[i++]=*p++;sv[i]=0;wnc_set_var_str(vn,sv);}
    else{i=0;while(*p&&*p!='\n'&&i<127)expr[i++]=*p++;expr[i]=0;wnc_set_var(vn,wnc_eval_expr(expr));}
}

static void wnc_array_cmd(const char* line) {
    char cmd[32]={0},name[32]={0};const char* p=line;while(*p==' ')p++;
    int i=0;while(*p&&*p!=' '&&i<31)cmd[i++]=*p++;cmd[i]=0;while(*p==' ')p++;
    if(wnc_strcmp(cmd,"create")==0){i=0;while(*p&&*p!=' '&&i<31)name[i++]=*p++;name[i]=0;while(*p==' ')p++;int sz=wnc_atoi(p);wnc_create_array(name,sz);kprint("Array created: ");kprint(name);kprint("[");kprint_int(sz);kprint("]\n");}
    else if(wnc_strcmp(cmd,"set")==0){i=0;while(*p&&*p!='['&&i<31)name[i++]=*p++;name[i]=0;while(*p!='[')p++;p++;int idx=wnc_atoi(p);while(*p!=']')p++;p++;while(*p==' ')p++;int val=wnc_atoi(p);wnc_array_set(name,idx,val);}
    else if(wnc_strcmp(cmd,"get")==0){i=0;while(*p&&*p!='['&&i<31)name[i++]=*p++;name[i]=0;while(*p!='[')p++;p++;int idx=wnc_atoi(p);int val=wnc_array_get(name,idx);kprint_int(val);kprint("\n");}
    else if(wnc_strcmp(cmd,"print")==0){i=0;while(*p&&*p!=' '&&i<31)name[i++]=*p++;name[i]=0;wnc_array_print(name);}
}

static void wnc_struct_cmd(const char* line) {
    char cmd[32]={0},name[32]={0},field[32]={0};const char* p=line;while(*p==' ')p++;
    int i=0;while(*p&&*p!=' '&&i<31)cmd[i++]=*p++;cmd[i]=0;while(*p==' ')p++;
    if(wnc_strcmp(cmd,"create")==0){i=0;while(*p&&*p!=' '&&i<31)name[i++]=*p++;name[i]=0;wnc_create_struct(name);kprint("Struct created: ");kprint(name);kprint("\n");}
    else if(wnc_strcmp(cmd,"field")==0){i=0;while(*p&&*p!='.'&&i<31)name[i++]=*p++;name[i]=0;if(*p=='.')p++;i=0;while(*p&&*p!=' '&&i<31)field[i++]=*p++;field[i]=0;wnc_struct_add_field(name,field);}
    else if(wnc_strcmp(cmd,"set")==0){i=0;while(*p&&*p!='.'&&i<31)name[i++]=*p++;name[i]=0;if(*p=='.')p++;i=0;while(*p&&*p!='='&&i<31)field[i++]=*p++;field[i]=0;while(*p!='=')p++;p++;int val=wnc_atoi(p);wnc_struct_set(name,field,val);}
    else if(wnc_strcmp(cmd,"get")==0){i=0;while(*p&&*p!='.'&&i<31)name[i++]=*p++;name[i]=0;if(*p=='.')p++;i=0;while(*p&&*p!=' '&&i<31)field[i++]=*p++;field[i]=0;int val=wnc_struct_get(name,field);kprint_int(val);kprint("\n");}
    else if(wnc_strcmp(cmd,"print")==0){i=0;while(*p&&*p!=' '&&i<31)name[i++]=*p++;name[i]=0;wnc_struct_print(name);}
}

static void wnc_func_cmd(const char* line) {
    char type[16]={0},name[32]={0},params[256]={0},body[4096]={0};const char* p=line;while(*p==' ')p++;
    int i=0;while(*p&&*p!=' '&&i<15)type[i++]=*p++;type[i]=0;while(*p==' ')p++;
    i=0;while(*p&&*p!='('&&i<31)name[i++]=*p++;name[i]=0;while(*p!='(')p++;p++;
    i=0;while(*p&&*p!=')'){params[i++]=*p++;}params[i]=0;while(*p!=')')p++;p++;
    while(*p==' ')p++;if(*p=='{'){p++;int depth=1;int j=0;while(*p&&depth>0&&j<4095){if(*p=='{')depth++;if(*p=='}')depth--;if(depth>0)body[j++]=*p;p++;}body[j]=0;}
    int is_void=(wnc_strcmp(type,"void")==0);wnc_add_function(name,params,body,0,is_void);
}

static void wnc_import_cmd(const char* line) { char fname[256]={0};const char* p=line;while(*p==' ')p++;int i=0;while(*p&&*p!=' '&&*p!='\n'&&i<255)fname[i++]=*p++;fname[i]=0;wnc_import_file(fname);}

static void wnc_return_cmd(const char* line) { const char* p=line;while(*p==' ')p++;wnc_return_value=wnc_eval_expr(p);wnc_return_flag=1;}

static void wnc_graph_cmd(const char* line) {
    char cmd[32]={0};const char* p=line;while(*p==' ')p++;
    int i=0;while(*p&&*p!=' '&&*p!='('&&i<31)cmd[i++]=*p++;cmd[i]=0;while(*p==' '||*p=='(')p++;
    if(wnc_strcmp(cmd,"clear")==0)clear_screen();
    else if(wnc_strcmp(cmd,"pixel")==0){int x=wnc_atoi(p);while(*p>='0'||*p=='-')p++;while(*p==' '||*p==',')p++;int y=wnc_atoi(p);while(*p>='0'||*p=='-')p++;while(*p==' '||*p==',')p++;int c=wnc_atoi(p);put_pixel(x,y,c,0x0F,' ');}
    else if(wnc_strcmp(cmd,"line")==0){int x1=wnc_atoi(p);while(*p>='0'||*p=='-')p++;while(*p==' '||*p==',')p++;int y1=wnc_atoi(p);while(*p>='0'||*p=='-')p++;while(*p==' '||*p==',')p++;int x2=wnc_atoi(p);while(*p>='0'||*p=='-')p++;while(*p==' '||*p==',')p++;int y2=wnc_atoi(p);while(*p>='0'||*p=='-')p++;while(*p==' '||*p==',')p++;int c=wnc_atoi(p);int dx=(x2>x1)?(x2-x1):(x1-x2),sx=x1<x2?1:-1,dy=(y2>y1)?-(y2-y1):-(y1-y2),sy=y1<y2?1:-1,err=dx+dy;while(1){put_pixel(x1,y1,c,0x0F,' ');if(x1==x2&&y1==y2)break;int e2=2*err;if(e2>=dy){err+=dy;x1+=sx;}if(e2<=dx){err+=dx;y1+=sy;}}}
    else if(wnc_strcmp(cmd,"rect")==0){int x=wnc_atoi(p);while(*p>='0'||*p=='-')p++;while(*p==' '||*p==',')p++;int y=wnc_atoi(p);while(*p>='0'||*p=='-')p++;while(*p==' '||*p==',')p++;int w=wnc_atoi(p);while(*p>='0'||*p=='-')p++;while(*p==' '||*p==',')p++;int h=wnc_atoi(p);while(*p>='0'||*p=='-')p++;while(*p==' '||*p==',')p++;int c=wnc_atoi(p);for(int i=0;i<w;i++){put_pixel(x+i,y,c,0x0F,' ');put_pixel(x+i,y+h-1,c,0x0F,' ');}for(int i=0;i<h;i++){put_pixel(x,y+i,c,0x0F,' ');put_pixel(x+w-1,y+i,c,0x0F,' ');}}
    else if(wnc_strcmp(cmd,"fill")==0){int c=wnc_atoi(p);for(int i=0;i<80;i++)for(int j=0;j<25;j++)put_pixel(i,j,c,0x0F,' ');}
    else if(wnc_strcmp(cmd,"circle")==0){int cx=wnc_atoi(p);while(*p>='0'||*p=='-')p++;while(*p==' '||*p==',')p++;int cy=wnc_atoi(p);while(*p>='0'||*p=='-')p++;while(*p==' '||*p==',')p++;int r=wnc_atoi(p);while(*p>='0'||*p=='-')p++;while(*p==' '||*p==',')p++;int c=wnc_atoi(p);int x=0,y=r,d=3-2*r;while(y>=x){put_pixel(cx+x,cy+y,c,0x0F,' ');put_pixel(cx-x,cy+y,c,0x0F,' ');put_pixel(cx+x,cy-y,c,0x0F,' ');put_pixel(cx-x,cy-y,c,0x0F,' ');put_pixel(cx+y,cy+x,c,0x0F,' ');put_pixel(cx-y,cy+x,c,0x0F,' ');put_pixel(cx+y,cy-x,c,0x0F,' ');put_pixel(cx-y,cy-x,c,0x0F,' ');x++;if(d<0)d+=4*x+6;else{d+=4*(x-y)+10;y--;}}}
    else if(wnc_strcmp(cmd,"text")==0){int x=wnc_atoi(p);while(*p>='0'||*p=='-')p++;while(*p==' '||*p==',')p++;int y=wnc_atoi(p);while(*p>='0'||*p=='-')p++;while(*p==' '||*p==',')p++;int c=wnc_atoi(p);while(*p==' '||*p==',')p++;if(*p=='"'){p++;char txt[256]={0};int i=0;while(*p&&*p!='"'&&i<255)txt[i++]=*p++;txt[i]=0;kprint_at(txt,x,y,c);}}
    else if(wnc_strcmp(cmd,"frame")==0){int x=wnc_atoi(p);while(*p>='0'||*p=='-')p++;while(*p==' '||*p==',')p++;int y=wnc_atoi(p);while(*p>='0'||*p=='-')p++;while(*p==' '||*p==',')p++;int w=wnc_atoi(p);while(*p>='0'||*p=='-')p++;while(*p==' '||*p==',')p++;int h=wnc_atoi(p);while(*p>='0'||*p=='-')p++;while(*p==' '||*p==',')p++;int bg=wnc_atoi(p);while(*p>='0'||*p=='-')p++;while(*p==' '||*p==',')p++;int fg=wnc_atoi(p);for(int i=0;i<w;i++){put_pixel(x+i,y,bg,fg,S_HLINE);put_pixel(x+i,y+h-1,bg,fg,S_HLINE);}for(int i=0;i<h;i++){put_pixel(x,y+i,bg,fg,S_VLINE);put_pixel(x+w-1,y+i,bg,fg,S_VLINE);}put_pixel(x,y,bg,fg,S_TL);put_pixel(x+w-1,y,bg,fg,S_TR);put_pixel(x,y+h-1,bg,fg,S_BL);put_pixel(x+w-1,y+h-1,bg,fg,S_BR);}
}

static void wnc_key_cmd(const char* line) {
    char cmd[32]={0};const char* p=line;while(*p==' ')p++;
    int i=0;while(*p&&*p!=' '&&i<31)cmd[i++]=*p++;cmd[i]=0;while(*p==' ')p++;
    if(wnc_strcmp(cmd,"wait")==0){while(!(inb(0x64)&1));uint8_t sc=inb(0x60);kprint_int(sc);kprint("\n");}
    else if(wnc_strcmp(cmd,"get")==0){char vn[32]={0};i=0;while(*p&&*p!=' '&&i<31)vn[i++]=*p++;vn[i]=0;while(!(inb(0x64)&1));uint8_t sc=inb(0x60);wnc_set_var(vn,sc);}
    else if(wnc_strcmp(cmd,"check")==0){char vn[32]={0};i=0;while(*p&&*p!=' '&&i<31)vn[i++]=*p++;vn[i]=0;int pressed=0;if(inb(0x64)&1){uint8_t sc=inb(0x60);pressed=sc;}wnc_set_var(vn,pressed);}
}

static void wnc_mkdir(const char* line) { char dn[32]={0};const char* p=line;while(*p==' ')p++;int i=0;while(*p&&*p!=' '&&*p!='\n'&&i<31)dn[i++]=*p++;dn[i]=0;if(dn[0]==0){kprint("Usage: mkdir <name>\n");return;}uint16_t db[256];read_sector(current_dir_sector,db);int slot=-1;for(i=0;i<32;i++){char n[12]={0};for(int j=0;j<11;j++)n[j]=((char*)db)[i*16+j];if(n[0]==0){slot=i;break;}}if(slot==-1){kprint("No free slots\n");return;}for(i=0;i<11&&dn[i];i++)((char*)db)[slot*16+i]=dn[i];((char*)db)[slot*16+11]=1;static int dc=300;int nds=dc++;db[slot*8+6]=nds;db[slot*8+7]=0;write_sector(current_dir_sector,db);uint16_t eb[256];for(i=0;i<256;i++)eb[i]=0;write_sector(nds,eb);kprint("Directory created: ");kprint(dn);kprint("\n");}
static void wnc_cd(const char* line) { char dn[32]={0};const char* p=line;while(*p==' ')p++;int i=0;while(*p&&*p!=' '&&*p!='\n'&&i<31)dn[i++]=*p++;dn[i]=0;if(dn[0]==0||wnc_strcmp(dn,"/")==0){current_dir_sector=100;kprint("Changed to root\n");return;}uint16_t db[256];read_sector(current_dir_sector,db);for(i=0;i<32;i++){char n[12]={0};for(int j=0;j<11;j++)n[j]=((char*)db)[i*16+j];if(wnc_strcmp(dn,n)==0&&((char*)db)[i*16+11]==1){current_dir_sector=db[i*8+6];kprint("Changed to: ");kprint(dn);kprint("\n");return;}}kprint("Directory not found: ");kprint(dn);kprint("\n");}
static void wnc_pwd(const char* line) {(void)line;if(current_dir_sector==100)kprint("/\n");else{kprint("/dir_");kprint_int(current_dir_sector-300);kprint("\n");}}
static void wnc_ls(const char* line) {(void)line;uint16_t db[256];read_sector(current_dir_sector,db);kprint("\n=== DIRECTORY ===\n");int cnt=0;for(int i=0;i<32;i++){char n[12]={0};for(int j=0;j<11;j++)n[j]=((char*)db)[i*16+j];if(n[0]!=0){int is_dir=((char*)db)[i*16+11]==1;int sz=db[i*8+7];if(is_dir)kprint("  [DIR]  ");else kprint("  [FILE] ");kprint(n);if(!is_dir){kprint(" (");kprint_int(sz);kprint(" bytes)");}kprint("\n");cnt++;}}if(cnt==0)kprint("  (empty)\n");kprint("================\n");}
static void wnc_create(const char* line) { char fn[32]={0};const char* p=line;while(*p==' ')p++;int i=0;while(*p&&*p!=' '&&*p!='\n'&&i<31)fn[i++]=*p++;fn[i]=0;if(fn[0]==0){kprint("Usage: create <filename>\n");return;}uint16_t db[256];read_sector(current_dir_sector,db);for(i=0;i<32;i++){char n[12]={0};for(int j=0;j<11;j++)n[j]=((char*)db)[i*16+j];if(wnc_strcmp(fn,n)==0){kprint("File exists\n");return;}}int slot=-1;for(i=0;i<32;i++){char n[12]={0};for(int j=0;j<11;j++)n[j]=((char*)db)[i*16+j];if(n[0]==0){slot=i;break;}}if(slot==-1){kprint("Directory full\n");return;}for(i=0;i<11&&fn[i];i++)((char*)db)[slot*16+i]=fn[i];((char*)db)[slot*16+11]=0;int fs=500+slot+(current_dir_sector-100)*32;db[slot*8+6]=fs;db[slot*8+7]=0;write_sector(current_dir_sector,db);uint16_t eb[256];for(i=0;i<256;i++)eb[i]=0;write_sector(fs,eb);kprint("File created: ");kprint(fn);kprint("\n");}
static void wnc_write(const char* line) { char fn[32]={0},cnt[512]={0};const char* p=line;while(*p==' ')p++;int i=0;while(*p&&*p!=' '&&i<31)fn[i++]=*p++;fn[i]=0;while(*p==' ')p++;if(*p=='"'){p++;i=0;while(*p&&*p!='"'&&i<511)cnt[i++]=*p++;cnt[i]=0;}else{i=0;while(*p&&*p!='\n'&&i<511)cnt[i++]=*p++;cnt[i]=0;}if(fn[0]==0){kprint("Usage: write <file> <content>\n");return;}uint16_t db[256];read_sector(current_dir_sector,db);int slot=-1;for(i=0;i<32;i++){char n[12]={0};for(int j=0;j<11;j++)n[j]=((char*)db)[i*16+j];if(wnc_strcmp(fn,n)==0){slot=i;break;}}if(slot==-1){kprint("File not found\n");return;}int len=0;while(cnt[len])len++;uint16_t dbuf[256]={0};for(i=0;i<len&&i<510;i++){if(i%2==0)dbuf[i/2]=cnt[i];else dbuf[i/2]|=(cnt[i]<<8);}int fs=db[slot*8+6];write_sector(fs,dbuf);db[slot*8+7]=len;write_sector(current_dir_sector,db);kprint("Written ");kprint_int(len);kprint(" bytes\n");}
static void wnc_read(const char* line) { char fn[32]={0};const char* p=line;while(*p==' ')p++;int i=0;while(*p&&*p!=' '&&*p!='\n'&&i<31)fn[i++]=*p++;fn[i]=0;if(fn[0]==0){kprint("Usage: read <filename>\n");return;}uint16_t db[256];read_sector(current_dir_sector,db);int slot=-1;for(i=0;i<32;i++){char n[12]={0};for(int j=0;j<11;j++)n[j]=((char*)db)[i*16+j];if(wnc_strcmp(fn,n)==0){slot=i;break;}}if(slot==-1){kprint("File not found\n");return;}int fs=db[slot*8+6];int sz=db[slot*8+7];uint16_t dbuf[256];read_sector(fs,dbuf);kprint("\n=== ");kprint(fn);kprint(" ===\n\n");for(i=0;i<sz;i++){char c;if(i%2==0)c=dbuf[i/2]&0xFF;else c=(dbuf[i/2]>>8)&0xFF;if(c>=32&&c<=126){char s[2]={c,0};kprint(s);}}kprint("\n\n");}
static void wnc_delete(const char* line) { char fn[32]={0};const char* p=line;while(*p==' ')p++;int i=0;while(*p&&*p!=' '&&*p!='\n'&&i<31)fn[i++]=*p++;fn[i]=0;if(fn[0]==0){kprint("Usage: delete <filename>\n");return;}uint16_t db[256];read_sector(current_dir_sector,db);int slot=-1;for(i=0;i<32;i++){char n[12]={0};for(int j=0;j<11;j++)n[j]=((char*)db)[i*16+j];if(wnc_strcmp(fn,n)==0){slot=i;break;}}if(slot==-1){kprint("File not found\n");return;}for(i=0;i<16;i++)((char*)db)[slot*16+i]=0;write_sector(current_dir_sector,db);kprint("Deleted: ");kprint(fn);kprint("\n");}
static void wnc_copy(const char* line) { char src[32]={0},dst[32]={0};const char* p=line;while(*p==' ')p++;int i=0;while(*p&&*p!=' '&&i<31)src[i++]=*p++;src[i]=0;while(*p==' ')p++;i=0;while(*p&&*p!=' '&&*p!='\n'&&i<31)dst[i++]=*p++;dst[i]=0;if(src[0]==0||dst[0]==0){kprint("Usage: copy <src> <dst>\n");return;}uint16_t db[256];read_sector(current_dir_sector,db);int sslot=-1,ssect=0,ssz=0;for(i=0;i<32;i++){char n[12]={0};for(int j=0;j<11;j++)n[j]=((char*)db)[i*16+j];if(wnc_strcmp(src,n)==0){sslot=i;ssect=db[i*8+6];ssz=db[i*8+7];break;}}if(sslot==-1){kprint("Source not found\n");return;}for(i=0;i<32;i++){char n[12]={0};for(int j=0;j<11;j++)n[j]=((char*)db)[i*16+j];if(wnc_strcmp(dst,n)==0){kprint("Destination exists\n");return;}}int dslot=-1;for(i=0;i<32;i++){char n[12]={0};for(int j=0;j<11;j++)n[j]=((char*)db)[i*16+j];if(n[0]==0){dslot=i;break;}}if(dslot==-1){kprint("Directory full\n");return;}uint16_t dbuf[256];read_sector(ssect,dbuf);int dsect=500+dslot+(current_dir_sector-100)*32;write_sector(dsect,dbuf);for(i=0;i<11&&dst[i];i++)((char*)db)[dslot*16+i]=dst[i];((char*)db)[dslot*16+11]=0;db[dslot*8+6]=dsect;db[dslot*8+7]=ssz;write_sector(current_dir_sector,db);kprint("Copied\n");}
static void wnc_move(const char* line) { char src[32]={0},dst[32]={0};const char* p=line;while(*p==' ')p++;int i=0;while(*p&&*p!=' '&&i<31)src[i++]=*p++;src[i]=0;while(*p==' ')p++;i=0;while(*p&&*p!=' '&&*p!='\n'&&i<31)dst[i++]=*p++;dst[i]=0;if(src[0]==0||dst[0]==0){kprint("Usage: move <src> <dst>\n");return;}uint16_t db[256];read_sector(current_dir_sector,db);int slot=-1;for(i=0;i<32;i++){char n[12]={0};for(int j=0;j<11;j++)n[j]=((char*)db)[i*16+j];if(wnc_strcmp(src,n)==0){slot=i;break;}}if(slot==-1){kprint("Source not found\n");return;}for(i=0;i<11&&dst[i];i++)((char*)db)[slot*16+i]=dst[i];for(i=wnc_strlen(dst);i<11;i++)((char*)db)[slot*16+i]=0;write_sector(current_dir_sector,db);kprint("Moved\n");}

static void wnc_execute_line(const char* line) {
    if(line[0]==0||line[0]=='#'||line[0]==';')return;
    if(wnc_strncmp(line,"print",5)==0)wnc_print(line+5);
    else if(wnc_strncmp(line,"input",5)==0)wnc_input(line+5);
    else if(wnc_strncmp(line,"let",3)==0)wnc_let(line+3);
    else if(wnc_strncmp(line,"array",5)==0)wnc_array_cmd(line+5);
    else if(wnc_strncmp(line,"struct",6)==0)wnc_struct_cmd(line+6);
    else if(wnc_strncmp(line,"func",4)==0)wnc_func_cmd(line+4);
    else if(wnc_strncmp(line,"import",6)==0)wnc_import_cmd(line+6);
    else if(wnc_strcmp(line,"return")==0)wnc_return_cmd("");
    else if(wnc_strncmp(line,"return",6)==0)wnc_return_cmd(line+6);
    else if(wnc_strncmp(line,"if",2)==0){char cond[128]={0},cmd[512]={0};const char* p=line+2;while(*p==' ')p++;int i=0;while(*p&&*p!='{'&&i<127)cond[i++]=*p++;cond[i]=0;while(*p&&*p!='{')p++;if(*p=='{'){p++;i=0;int depth=1;while(*p&&depth>0&&i<511){if(*p=='{')depth++;if(*p=='}')depth--;if(depth>0)cmd[i++]=*p;p++;}cmd[i]=0;}if(wnc_check_condition(cond)){char lb[256];int lp=0;for(int j=0;cmd[j];j++){if(cmd[j]==';'||cmd[j]=='\n'){if(lp>0){lb[lp]=0;wnc_execute_line(lb);lp=0;}}else if(lp<255)lb[lp++]=cmd[j];}if(lp>0){lb[lp]=0;wnc_execute_line(lb);}}}
    else if(wnc_strncmp(line,"while",5)==0){char cond[128]={0},cmd[512]={0};const char* p=line+5;while(*p==' ')p++;int i=0;while(*p&&*p!='{'&&i<127)cond[i++]=*p++;cond[i]=0;while(*p&&*p!='{')p++;if(*p=='{'){p++;i=0;int depth=1;while(*p&&depth>0&&i<511){if(*p=='{')depth++;if(*p=='}')depth--;if(depth>0)cmd[i++]=*p;p++;}cmd[i]=0;}int max_iter=10000;wnc_break_flag=0;while(wnc_check_condition(cond)&&max_iter-->0&&!wnc_break_flag){if(wnc_continue_flag){wnc_continue_flag=0;continue;}char lb[256];int lp=0;for(int j=0;cmd[j];j++){if(cmd[j]==';'||cmd[j]=='\n'){if(lp>0){lb[lp]=0;wnc_execute_line(lb);lp=0;}}else if(lp<255)lb[lp++]=cmd[j];}if(lp>0){lb[lp]=0;wnc_execute_line(lb);}}}
    else if(wnc_strncmp(line,"for",3)==0){char var[32]={0};int start=0,end=0,step=1;const char* p=line+3;while(*p==' ')p++;int i=0;while(*p&&*p!='='&&*p!=' '&&i<31)var[i++]=*p++;var[i]=0;while(*p==' '||*p=='=')p++;start=wnc_atoi(p);while((*p>='0'||*p=='-')&&*p)p++;while(*p==' ')p++;if(wnc_strncmp(p,"to",2)==0)p+=2;while(*p==' ')p++;end=wnc_atoi(p);while((*p>='0'||*p=='-')&&*p)p++;while(*p==' ')p++;char cmd[512]={0};if(*p=='{'){p++;i=0;int depth=1;while(*p&&depth>0&&i<511){if(*p=='{')depth++;if(*p=='}')depth--;if(depth>0)cmd[i++]=*p;p++;}cmd[i]=0;}wnc_break_flag=0;for(int val=start;(step>0?val<=end:val>=end)&&!wnc_break_flag;val+=step){if(wnc_continue_flag){wnc_continue_flag=0;continue;}wnc_set_var(var,val);char lb[256];int lp=0;for(int j=0;cmd[j];j++){if(cmd[j]==';'||cmd[j]=='\n'){if(lp>0){lb[lp]=0;wnc_execute_line(lb);lp=0;}}else if(lp<255)lb[lp++]=cmd[j];}if(lp>0){lb[lp]=0;wnc_execute_line(lb);}}}
    else if(wnc_strcmp(line,"break")==0)wnc_break_flag=1;
    else if(wnc_strcmp(line,"continue")==0)wnc_continue_flag=1;
    else if(wnc_strncmp(line,"graph",5)==0)wnc_graph_cmd(line+5);
    else if(wnc_strncmp(line,"key",3)==0)wnc_key_cmd(line+3);
    else if(wnc_strncmp(line,"mkdir",5)==0)wnc_mkdir(line+5);
    else if(wnc_strncmp(line,"cd",2)==0)wnc_cd(line+2);
    else if(wnc_strcmp(line,"pwd")==0)wnc_pwd(line);
    else if(wnc_strcmp(line,"ls")==0)wnc_ls(line);
    else if(wnc_strncmp(line,"create",6)==0)wnc_create(line+6);
    else if(wnc_strncmp(line,"write",5)==0)wnc_write(line+5);
    else if(wnc_strncmp(line,"read",4)==0)wnc_read(line+4);
    else if(wnc_strncmp(line,"delete",6)==0)wnc_delete(line+6);
    else if(wnc_strncmp(line,"copy",4)==0)wnc_copy(line+4);
    else if(wnc_strncmp(line,"move",4)==0)wnc_move(line+4);
    else if(wnc_strncmp(line,"run",3)==0){const char* cmd=line+3;while(*cmd==' ')cmd++;if(cmd[0]){char tc[256];wnc_strcpy(tc,cmd);int dp=0;process_command(tc,dp);}}
    else if(wnc_strcmp(line,"time")==0){extern int seconds;int h=(seconds/3600)%24,m=(seconds/60)%60,s=seconds%60;if(h<10)kprint("0");kprint_int(h);kprint(":");if(m<10)kprint("0");kprint_int(m);kprint(":");if(s<10)kprint("0");kprint_int(s);kprint("\n");}
    else if(wnc_strcmp(line,"getkey")==0){while(!(inb(0x64)&1));while(inb(0x64)&1)inb(0x60);}
    else if(wnc_strncmp(line,"sleep",5)==0){const char* p=line+5;while(*p==' ')p++;int sec=wnc_atoi(p);if(sec<1)sec=1;if(sec>3600)sec=3600;for(int i=0;i<sec;i++)for(volatile int d=0;d<100000000;d++);}
    else if(wnc_strcmp(line,"rand")==0){int r=wnc_rand(1,100);kprint_int(r);kprint("\n");}
    else if(wnc_strcmp(line,"vars")==0){kprint("\n=== VARIABLES ===\n");for(int i=0;i<wnc_var_count;i++){kprint("  ");kprint(wnc_vars[i].name);kprint(" = ");if(wnc_vars[i].type==WNKC_TYPE_NUMBER)kprint_int(wnc_vars[i].num_value);else{kprint("\"");kprint(wnc_vars[i].str_value);kprint("\"");}kprint("\n");}}
    else if(wnc_strcmp(line,"clear")==0){wnc_var_count=0;wnc_array_count=0;wnc_struct_count=0;kprint("Cleared\n");}
    else if(wnc_strncmp(line,"log",3)==0){const char* p=line+3;while(*p==' ')p++;if(wnc_strncmp(p,"on",2)==0){wnc_log_enabled=1;kprint("Logging on\n");}else if(wnc_strncmp(p,"off",3)==0){wnc_log_enabled=0;kprint("Logging off\n");}else kprint("Usage: log on/off\n");}
    else if(wnc_strncmp(line,"runscript",9)==0){const char* p=line+9;while(*p==' ')p++;char fn[32]={0};int i=0;while(*p&&*p!=' '&&*p!='\n'&&i<31)fn[i++]=*p++;fn[i]=0;wnc_execute_file(fn);}
    else{char fname[32]={0};int i=0;while(line[i]&&line[i]!='('&&i<31){fname[i]=line[i];i++;}fname[i]=0;if(wnc_find_function(fname)!=-1){wnc_call_function(fname);}else{kprint_color("Unknown: ",TXT_RED);kprint(line);kprint("\n");}}
}

int wnc_execute(const char* code) {
    if(!code)return -1;
    wnc_error=0;wnc_line=1;wnc_break_flag=0;wnc_continue_flag=0;wnc_return_flag=0;
    char line[WNKC_MAX_LINE];int pos=0,lp=0;
    while(code[pos]&&!wnc_error&&!wnc_return_flag){
        char c=code[pos];
        if(c=='\n'){line[lp]=0;if(lp>0)wnc_execute_line(line);wnc_line++;lp=0;}
        else if(lp<WNKC_MAX_LINE-1)line[lp++]=c;
        pos++;
    }
    if(lp>0){line[lp]=0;wnc_execute_line(line);}
    return wnc_error?-1:0;
}

int wnc_execute_file(const char* filename) {
    if(!filename)return -1;
    uint16_t db[256];read_sector(current_dir_sector,db);
    int slot=-1;for(int i=0;i<32;i++){char n[12]={0};for(int j=0;j<11;j++)n[j]=((char*)db)[i*16+j];if(wnc_strcmp(filename,n)==0){slot=i;break;}}
    if(slot==-1){kprint("File not found: ");kprint(filename);kprint("\n");return -1;}
    int sect=db[slot*8+6],sz=db[slot*8+7];uint16_t dbuf[256];read_sector(sect,dbuf);
    static char fb[4096];int pos=0;for(int i=0;i<sz&&pos<4095;i++){char c;if(i%2==0)c=dbuf[i/2]&0xFF;else c=(dbuf[i/2]>>8)&0xFF;if(c!=0&&c!='\r')fb[pos++]=c;}fb[pos]=0;
    kprint("Running: ");kprint(filename);kprint("\n");
    return wnc_execute(fb);
}

void wnc_init(void) { wnc_var_count=0; wnc_array_count=0; wnc_struct_count=0; wnc_function_count=0; wnc_import_count=0; wnc_error=0; wnc_line=1; wnc_log_enabled=1; }
void wnc_set_dir(uint16_t dir) { current_dir_sector=dir; }