[bits 32]
section .text
global int80_handler

extern linux_syscall_handler_c

int80_handler:
    pusha
    push ds
    push es
    push fs
    push gs
    
    mov ax, 0x10          ; Селектор данных ядра
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Сохраняем все регистры в структуру linux_regs_t
    push 0                ; eflags (заглушка)
    push 0                ; esp (заглушка)  
    push 0                ; eip (заглушка)
    push ebp
    push edi
    push esi
    push edx
    push ecx
    push ebx
    push eax              ; eax = syscall номер
    
    push esp
    call linux_syscall_handler_c
    add esp, 4
    
    ; Возвращаемое значение в eax
    mov [esp], eax
    
    pop eax
    pop ebx
    pop ecx
    pop edx
    pop esi
    pop edi
    pop ebp
    add esp, 12           ; Убираем eip, esp, eflags
    
    pop gs
    pop fs
    pop es
    pop ds
    popa
    iret