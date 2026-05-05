[bits 32]

MB_MAGIC    equ 0x1BADB002
MB_FLAGS    equ (1 << 0) | (1 << 1)
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

section .multiboot
    align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM

section .text
global _start
global idt_load
extern kmain     
extern idtp     
extern kmaindb    

_start:
    cli
    cld

    ;mov ax, 0x10
    ;mov ds, ax
    ;mov es, ax
    ;mov fs, ax
    ;mov gs, ax
    ;mov ss, ax
    mov esp, stack_top

    ;call read_disk_sector  
    
    ;call check_integrity
    test eax, eax      
    jz .force_install

    xor ebx, ebx
    mov byte [timer_sec_val], '9' 
    mov byte [timer_active], 1    
    jmp draw_menu            

.force_install:
    call kmaindb
    jmp hang

find_string:
    push ebp
    mov ebp, esp
    push edi
    push esi

    mov esi, disk_buffer
    mov ebx, 512
.loop:
    push esi
    push edi
    mov ecx, edx
    repe cmpsb
    pop edi
    pop esi
    je .found
    
    inc esi
    dec ebx
    jnz .loop
    
    xor eax, eax
    jmp .done
.found:
    mov eax, 1
.done:
    pop esi
    pop edi
    mov esp, ebp
    pop ebp
    ret
.missing:
    xor eax, eax
    ret

read_disk_sector:
    mov dx, 0x1F6
    mov al, 0xE0      
    out dx, al
    mov dx, 0x1F2
    mov al, 1           
    out dx, al
    mov dx, 0x1F3
    mov al, 2         
    out dx, al
    mov dx, 0x1F4
    mov al, 0
    out dx, al
    mov dx, 0x1F5
    mov al, 0
    out dx, al
    mov dx, 0x1F7
    mov al, 0x20     
    out dx, al
.wait:
    in al, dx
    test al, 8
    jz .wait
    
    mov edi, disk_buffer
    mov ecx, 256
    mov dx, 0x1F0
    cld
    rep insw            
    ret

draw_menu:
    mov edi, 0xB8000
    mov ecx, 80 * 25
    mov ax, 0x0020          
    rep stosw

    mov edi, 0xB8000 + 160
    mov ecx, 80
    mov ax, 0x7020          
    rep stosw
    mov edi, 0xB8000 + 160 + 60
    mov esi, header_str
    mov dl, 0x70
    call print_string

    mov edi, 0xB8000 + (160 * 3)
    mov esi, info_top
    mov dl, 0x07
    call print_string
    mov edi, 0xB8000 + (160 * 5)
    mov esi, info_license
    mov dl, 0x07
    call print_string

    xor esi, esi
item_loop:
    push esi
    mov eax, esi
    add eax, 9              
    imul eax, 160
    add eax, 0xB8000 + 4    
    mov dl, 0x07            
    cmp esi, ebx
    jne .draw_bg
    mov dl, 0x70            
.draw_bg:
    push eax
    mov ecx, 72             
.bg_l:
    mov [eax+1], dl
    mov byte [eax], ' '     
    add eax, 2
    loop .bg_l
    pop eax
    push eax
    call get_item_str       
    pop edi
    add edi, 4              
    call print_string
    pop esi
    inc esi
    cmp esi, 4              
    jl item_loop

    mov edi, 0xB8000 + (160 * 23)
    mov ecx, 80
    mov ax, 0x7020
    rep stosw
    mov edi, 0xB8000 + (160 * 23)
    mov esi, footer_str
    mov dl, 0x70 
    call print_string

input_loop:
    cmp byte [timer_active], 1
    jne .check_keyboard
    
    mov edi, 0xB8000 + (160 * 21)
    mov esi, timer_text_main
    mov dl, 0x07
    call print_string
    mov al, [timer_sec_val]
    mov [edi], al
    mov byte [edi+1], 0x0F   
    add edi, 2
    mov esi, timer_text_end
    mov dl, 0x07
    call print_string

    mov ecx, 0x07FFFFFF 
.delay:
    in al, 0x64              
    test al, 1
    jnz .key_pressed         
    loop .delay
    dec byte [timer_sec_val]
    cmp byte [timer_sec_val], '0'
    jl confirm               
    jmp input_loop

.key_pressed:
    mov byte [timer_active], 0
    mov edi, 0xB8000 + (160 * 21)
    mov ecx, 80
.cl: mov word [edi], 0x0020   
    add edi, 2
    loop .cl

.check_keyboard:
    in al, 0x60              
    cmp al, 0x48
    je move_up
    cmp al, 0x50
    je move_down
    cmp al, 0x1C
    je confirm
    jmp input_loop

move_up:
    dec ebx
    and ebx, 3
    call wait_key_release
    jmp draw_menu

move_down:
    inc ebx
    and ebx, 3
    call wait_key_release
    jmp draw_menu

confirm:
    call wait_key_release
    cmp ebx, 0
    je .do_start
    cmp ebx, 1
    je .do_yn
    cmp ebx, 2
    je do_reboot
    cmp ebx, 3
    je do_shutdown
    jmp input_loop

.do_start:
    cli
    call kmain
    jmp hang

.do_yn:
    mov edi, 0xB8000 + (160 * 21)
    mov esi, ask_confirm
    mov dl, 0x0C
    call print_string
.yn:
    in al, 0x60
    cmp al, 0x15
    je .go
    cmp al, 0x31
    je draw_menu
    jmp .yn
.go:
    cli
    call kmaindb
    jmp hang

print_string:
.l: lodsb
    or al, al
    jz .d
    mov [edi], al
    mov [edi+1], dl
    add edi, 2
    jmp .l
.d: ret

get_item_str:
    cmp esi, 0
    je .s0
    cmp esi, 1
    je .s1
    cmp esi, 2
    je .s2
    mov esi, m_off
    ret
.s0: mov esi, m_start
    ret
.s1: mov esi, m_debug
    ret
.s2: mov esi, m_reboot
    ret

wait_key_release:
    in al, 0x60
    test al, 0x80
    jz wait_key_release
    ret

do_reboot:
    mov al, 0xFE
    out 0x64, al
    jmp hang

do_shutdown:
    mov dx, 0x604
    mov ax, 0x2000
    out dx, ax
    mov dx, 0xB004
    mov ax, 0x2000
    out dx, ax
    jmp hang

hang:
    cli
    hlt
    jmp hang

idt_load:
    lidt [idtp]
    ret

section .data
header_str    db "Wnka Boot Manager", 0
info_top      db "WnkaOS 32x - Run WnkaOS WnkaOS 32x (debug) - debug mode", 0
info_license  db "Wnka OS is created by Wnka-Software. GNU/GPL 3.0", 0
footer_str    db " ENTER=Select | ARROWS=Move                                                 ", 0
ask_confirm   db "Confirm: Start Debug Mode WNKA OS? (Y/N)", 0
m_start       db "WnkaOS 32x", 0
m_debug       db "WnkaOS 32x (Debug mode)", 0
m_reboot      db "Restart", 0
m_off         db "Shut Down", 0
timer_text_main db "Automatic boot in ", 0
timer_sec_val   db '9'
timer_text_end  db " seconds.", 0
timer_active    db 1

section .bss
align 16
disk_buffer:  resb 512
stack_bottom: resb 65536  ; 64 KB вместо 16 KB
stack_top:
