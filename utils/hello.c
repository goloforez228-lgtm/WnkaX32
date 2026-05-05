void _start() {
    char *msg = "Hello from ELF in WnkaOS!\n";
    
    volatile char *video = (volatile char*)0xB8000;
    for(int i = 0; msg[i]; i++) {
        video[i*2] = msg[i];
        video[i*2+1] = 0x0A;
    }
    while(1) {
        __asm__ volatile("hlt");
    }
}