#include "graph.h"
#include "video.h"
#include "themes.h"
#include "string_utils.h"

#define KEY_C 0x2E    
#define KEY_HOME 0x47   
#define KEY_END 0x4F   
#define KEY_ESC 0x01  
#define KEY_ENTER 0x1C 
#define KEY_UP 0x48   
#define KEY_DOWN 0x50  

typedef struct {
    char name[20];
    uint8_t text_color;
    uint8_t bg_color;
    uint8_t accent_color;
    uint8_t error_color;
    uint8_t success_color;
} CustomTheme;

CustomTheme themes[THEME_COUNT] = {
    {"CLASSIC",    TXT_WHITE, BLUE,      TXT_YELLOW, TXT_RED,   TXT_GREEN},
    {"HACKER",     TXT_GREEN, BLACK,     TXT_WHITE,  TXT_RED,   TXT_CYAN},
    {"LIGHT",      TXT_BLACK, WHITE,     TXT_BLUE,   TXT_RED,   TXT_GREEN},
    {"NIGHT",      TXT_CYAN,  DARK_GRAY, TXT_YELLOW, TXT_RED,   TXT_GREEN},
    {"FOREST",     TXT_YELLOW, GREEN,     TXT_WHITE,  TXT_RED,   TXT_CYAN},
    {"OCEAN",      TXT_WHITE, CYAN,      TXT_YELLOW, TXT_RED,   TXT_GREEN},
    {"MATRIX",     TXT_GREEN, BLACK,     TXT_WHITE,  TXT_RED,   TXT_CYAN},
    {"RETRO",      TXT_YELLOW, BROWN,     TXT_WHITE,  TXT_RED,   TXT_GREEN},
    {"AMBER",      TXT_YELLOW, BLACK,     TXT_WHITE,  TXT_RED,   TXT_GREEN},
    {"CUSTOM",     TXT_WHITE, BLUE,      TXT_YELLOW, TXT_RED,   TXT_GREEN}
};

CustomTheme current_theme = themes[0];
int current_theme_id = 0;
int custom_theme_created = 0;

extern uint8_t console_color;

uint8_t get_terminal_color(void) {
    return (current_theme.bg_color << 4) | current_theme.text_color;
}

uint8_t get_highlight_color(void) {
    return (current_theme.bg_color << 4) | current_theme.accent_color;
}

uint8_t get_error_color(void) {
    return (current_theme.bg_color << 4) | current_theme.error_color;
}

uint8_t get_success_color(void) {
    return (current_theme.bg_color << 4) | current_theme.success_color;
}

void apply_terminal_theme(void) {
    console_color = get_terminal_color();
    
    kprint("\n╔════════════════════════════════╗\n");
    kprint("║        THEME APPLIED           ║\n");
    kprint("╠════════════════════════════════╣\n");
    kprint("║ Name: ");
    kprint(current_theme.name);
    kprint("\n║ Colors: ");
    kprint_color("Text ", get_terminal_color());
    kprint_color("Accent ", get_highlight_color());
    kprint_color("Error ", get_error_color());
    kprint_color("Success ", get_success_color());
    kprint("\n╚════════════════════════════════╝\n");
}

void set_theme(int id) {
    if(id < 0 || id >= THEME_COUNT) {
        kprint_color("Invalid theme ID!\n", TXT_RED);
        return;
    }
    
    current_theme = themes[id];
    current_theme_id = id;
    apply_terminal_theme();
}

static int wait_arrow_choice(int* selected, int max, int* custom_key) {
    while(1) {
        if(inb(0x64) & 1) {
            uint8_t key = inb(0x60);
            *custom_key = 0; 
            
            if(key == KEY_UP) {
                (*selected)--;
                if(*selected < 0) *selected = max - 1;
                return 1; 
            }
            if(key == KEY_DOWN) {
                (*selected)++;
                if(*selected >= max) *selected = 0;
                return 1;
            }
            if(key == KEY_HOME) {
                *selected = 0;
                return 1;
            }
            if(key == KEY_END) {
                *selected = max - 1;
                return 1;
            }
            if(key == KEY_ENTER) {
                return 2; 
            }
            if(key == KEY_ESC) {
                return 0; 
            }
            if(key == KEY_C) { 
                *custom_key = 1;
                return 3; 
            }
        }
    }
}

static void wait_for_key(void) {
    kprint("Press any key to continue...");
    while(!(inb(0x64) & 1));
    while(inb(0x64) & 1) inb(0x60);
    kprint("\n");
}

static uint8_t select_color(const char* prompt, uint8_t default_color) {
    int selected = default_color;
    int running = 1;
    
    while(running) {
        clear_screen_bg(BLACK);
        
        draw_frame(0, 1, 60, 20, BLACK, TXT_WHITE);
        
        kprint_at(prompt, 25, 2, (BLACK << 4) | TXT_WHITE);
        
        for(int i = 0; i < 16; i++) {
            int y = 4 + i;
            
            if(i == selected) {
                kprint_at("▶", 2, y, get_highlight_color());
            }
            
            char num[3];
            num[0] = (i / 10) + '0';
            num[1] = (i % 10) + '0';
            num[2] = '\0';
            kprint_at(num, 5, y, (BLACK << 4) | TXT_WHITE);
            
            for(int p = 0; p < 6; p++) {
                for(int q = 0; q < 3; q++) {
                    put_pixel(10 + p, y + q, i, i, ' ');
                }
            }
            
            const char* color_name;
            switch(i) {
                case 0: color_name = "BLACK"; break;
                case 1: color_name = "BLUE"; break;
                case 2: color_name = "GREEN"; break;
                case 3: color_name = "CYAN"; break;
                case 4: color_name = "RED"; break;
                case 5: color_name = "PURPLE"; break;
                case 6: color_name = "BROWN"; break;
                case 7: color_name = "GRAY"; break;
                case 8: color_name = "DARK_GRAY"; break;
                case 9: color_name = "LIGHT_BLUE"; break;
                case 10: color_name = "LIGHT_GREEN"; break;
                case 11: color_name = "LIGHT_CYAN"; break;
                case 12: color_name = "LIGHT_RED"; break;
                case 13: color_name = "LIGHT_PURPLE"; break;
                case 14: color_name = "YELLOW"; break;
                case 15: color_name = "WHITE"; break;
                default: color_name = ""; break;
            }
            
            uint8_t text_color = (i == selected) ? get_highlight_color() : ((BLACK << 4) | TXT_WHITE);
            kprint_at(color_name, 18, y, text_color);
        }
        
        kprint_at("[↑/↓] Move | [ENTER] Select | [ESC] Cancel", 10, 22, (BLACK << 4) | TXT_WHITE);
        
        int custom = 0;
        int result = wait_arrow_choice(&selected, 16, &custom);
        
        if(result == 2) return selected;
        if(result == 0) return default_color;
    }
    
    return default_color;
}

void customize_theme(void) {
    clear_screen_bg(BLACK);
    
    draw_frame(10, 2, 50, 18, BLACK, TXT_WHITE);
    kprint_at("CUSTOM THEME CREATOR", 20, 3, (BLACK << 4) | TXT_YELLOW);
    
    CustomTheme old_theme = themes[9];
    
    uint8_t bg = select_color("Select BACKGROUND color", current_theme.bg_color);
    uint8_t text = select_color("Select TEXT color", current_theme.text_color);
    uint8_t accent = select_color("Select ACCENT color", current_theme.accent_color);
    uint8_t error = select_color("Select ERROR color", current_theme.error_color);
    uint8_t success = select_color("Select SUCCESS color", current_theme.success_color);
    
    clear_screen_bg(BLACK);
    draw_frame(15, 5, 40, 12, BLACK, TXT_WHITE);
    
    kprint_at("Preview:", 25, 7, (BLACK << 4) | TXT_WHITE);
    
    kprint_at("Background ", 20, 9, (bg << 4) | text);
    kprint_at("Text ", 20, 10, (bg << 4) | text);
    kprint_at("Accent ", 20, 11, (bg << 4) | accent);
    kprint_at("Error ", 20, 12, (bg << 4) | error);
    kprint_at("Success ", 20, 13, (bg << 4) | success);
    
    kprint_at("Apply? (Y/N)", 25, 15, (BLACK << 4) | TXT_YELLOW);
    
    int confirm = 0;
    while(!confirm) {
        if(inb(0x64) & 1) {
            uint8_t key = inb(0x60);
            if(key == 0x15) {
                themes[9].bg_color = bg;
                themes[9].text_color = text;
                themes[9].accent_color = accent;
                themes[9].error_color = error;
                themes[9].success_color = success;
                custom_theme_created = 1;
                
                set_theme(9);
                confirm = 1;
            }
            else if(key == 0x31) {
                themes[9] = old_theme;
                confirm = 1;
            }
        }
    }
    
    wait_for_key();
}

void show_theme_selector(void) {
    int selected = current_theme_id;
    int running = 1;
    
    while(running) {
        clear_screen_bg(BLACK);
        
        draw_frame(5, 1, 60, 20, BLACK, TXT_WHITE);
        kprint_at("TERMINAL THEME SELECTOR", 18, 2, (BLACK << 4) | TXT_CYAN);
        
        for(int i = 0; i < THEME_COUNT; i++) {
            int y = 4 + i;
            
            if(i == selected) {
                kprint_at("▶", 8, y, get_highlight_color());
            }
            
            char num[3];
            num[0] = ((i+1) / 10) + '0';
            num[1] = ((i+1) % 10) + '0';
            num[2] = '\0';
            kprint_at(num, 12, y, (BLACK << 4) | TXT_WHITE);
            
            uint8_t name_color = (i == selected) ? get_highlight_color() : ((BLACK << 4) | TXT_WHITE);
            kprint_at(themes[i].name, 18, y, name_color);
            
            kprint_at("Text", 32, y, (themes[i].bg_color << 4) | themes[i].text_color);
            kprint_at("Accent", 38, y, (themes[i].bg_color << 4) | themes[i].accent_color);
            kprint_at("Err", 45, y, (themes[i].bg_color << 4) | themes[i].error_color);
            kprint_at("OK", 50, y, (themes[i].bg_color << 4) | themes[i].success_color);
        }
        
        kprint_at("Preview: ", 12, 18, (BLACK << 4) | TXT_WHITE);
        kprint_at("Normal", 22, 18, get_terminal_color());
        kprint_at("Selected", 30, 18, get_highlight_color());
        kprint_at("Error", 40, 18, get_error_color());
        kprint_at("Success", 47, 18, get_success_color());
        
        kprint_at("[↑/↓] Move | [ENTER] Select | [ESC] Exit | [C] Custom", 8, 21, (BLACK << 4) | TXT_WHITE);
        
        int custom = 0;
        int result = wait_arrow_choice(&selected, THEME_COUNT, &custom);
        
        if(result == 2) {
            set_theme(selected);
            running = 0;
        }
        else if(result == 3 && custom) {
            customize_theme();
            selected = 9;
        }
        else if(result == 0) {
            running = 0;
        }
    }
    
    clear_screen();
    kprint("root@wnka> ");
}

void reset_custom_theme(void) {
    themes[9].bg_color = BLUE;
    themes[9].text_color = TXT_WHITE;
    themes[9].accent_color = TXT_YELLOW;
    themes[9].error_color = TXT_RED;
    themes[9].success_color = TXT_GREEN;
    custom_theme_created = 0;
    
    kprint_color("Custom theme reset to defaults\n", TXT_YELLOW);
}

void theme_command(const char* arg) {
    if(arg[0] == '\0') {
        show_theme_selector();
        return;
    }
    
    if(my_strcmp(arg, "custom") == 0) {
        customize_theme();
        return;
    }
    
    if(my_strcmp(arg, "reset") == 0) {
        reset_custom_theme();
        return;
    }
    
    if(my_strcmp(arg, "help") == 0 || my_strcmp(arg, "?") == 0) {
        kprint("\n╔════════════════════════════════╗\n");
        kprint("║        THEME COMMANDS         ║\n");
        kprint("╠════════════════════════════════╣\n");
        kprint("║ theme          - Open menu    ║\n");
        kprint("║ theme N        - Set theme N  ║\n");
        kprint("║ theme custom   - Create own   ║\n");
        kprint("║ theme reset    - Reset custom ║\n");
        kprint("║ theme help     - This help    ║\n");
        kprint("╚════════════════════════════════╝\n");
        
        kprint("\nAvailable themes:\n");
        for(int i = 0; i < THEME_COUNT; i++) {
            kprint("  ");
            kprint_int(i+1);
            kprint(". ");
            kprint(themes[i].name);
            if(i == 9 && custom_theme_created) kprint(" [modified]");
            if(i == current_theme_id) kprint(" [ACTIVE]");
            kprint("\n");
        }
        return;
    }
    
    int num = 0;
    const char* p = arg;
    while(*p) {
        if(*p < '0' || *p > '9') {
            num = 0;
            break;
        }
        num = num * 10 + (*p - '0');
        p++;
    }
    
    if(num >= 1 && num <= THEME_COUNT) {
        set_theme(num - 1);
        return;
    }
    
    kprint_color("Unknown theme command. Try 'theme help'\n", TXT_RED);
}