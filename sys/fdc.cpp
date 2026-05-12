#include "fdc.h"
#include "video.h"
#include "kernel_stubs.h"
#include "graph.h"
#include <stdint.h>

static int fdc_write_protected = 0;
static int fdc_initialized = 0;

static void fdc_wait(void) {
    for(volatile int i = 0; i < 100000; i++);
}

static void fdc_write_cmd(uint8_t cmd) {
    int timeout = 1000000;
    while(timeout-- > 0) {
        if(inb(FDC_MSR) & 0x80) break;
    }
    outb(FDC_FIFO, cmd);
}

static uint8_t fdc_read_byte(void) {
    int timeout = 1000000;
    while(timeout-- > 0) {
        if(inb(FDC_MSR) & 0x80) break;
    }
    return inb(FDC_FIFO);
}

int fdc_init(void) {
    kprint("[FDC] Initializing floppy controller...\n");
    
    outb(FDC_DOR, 0x00);
    fdc_wait();
    outb(FDC_DOR, 0x0C);
    fdc_wait();
    
    fdc_write_cmd(FDC_CMD_SPECIFY);
    fdc_write_cmd(0xDF);
    fdc_write_cmd(0x02);
    
    fdc_write_cmd(FDC_CMD_RECALIBRATE);
    fdc_write_cmd(0x00);
    fdc_wait();
    
    fdc_initialized = 1;
    kprint("[FDC] Floppy controller ready\n");
    return 0;
}

void fdc_read_sector(uint8_t drive, uint8_t head, uint8_t track, uint8_t sector, uint8_t* buffer) {
    if(!fdc_initialized) {
        kprint_color("[FDC] Not initialized!\n", TXT_RED);
        return;
    }
    
    outb(FDC_DOR, 0x1C | (drive << 2));
    fdc_wait();
    
    fdc_write_cmd(FDC_CMD_READ_DATA);
    fdc_write_cmd((head << 2) | drive);
    fdc_write_cmd(track);
    fdc_write_cmd(head);
    fdc_write_cmd(sector);
    fdc_write_cmd(2);
    fdc_write_cmd(1);
    fdc_write_cmd(0x1B);
    fdc_write_cmd(0xFF);
    
    for(int i = 0; i < 512; i++) {
        buffer[i] = fdc_read_byte();
    }
    
    uint8_t st0 = fdc_read_byte();
    (void)st0;
    
    outb(FDC_DOR, 0x0C);
}

void fdc_write_sector(uint8_t drive, uint8_t head, uint8_t track, uint8_t sector, uint8_t* buffer) {
    if(fdc_write_protected) {
        kprint_color("[FDC] ERROR: Floppy is write protected!\n", TXT_RED);
        return;
    }
    
    if(!fdc_initialized) {
        kprint_color("[FDC] Not initialized!\n", TXT_RED);
        return;
    }
    
    outb(FDC_DOR, 0x1C | (drive << 2));
    fdc_wait();
    
    fdc_write_cmd(FDC_CMD_WRITE_DATA);
    fdc_write_cmd((head << 2) | drive);
    fdc_write_cmd(track);
    fdc_write_cmd(head);
    fdc_write_cmd(sector);
    fdc_write_cmd(2);
    fdc_write_cmd(1);
    fdc_write_cmd(0x1B);
    fdc_write_cmd(0xFF);
    
    for(int i = 0; i < 512; i++) {
        outb(FDC_FIFO, buffer[i]);
    }
    
    uint8_t st0 = fdc_read_byte();
    (void)st0;
    
    outb(FDC_DOR, 0x0C);
}

void fdc_set_write_protect(int enable) {
    fdc_write_protected = enable;
    if(enable) {
        kprint_color("[FDC] Floppy write PROTECTED\n", TXT_YELLOW);
    } else {
        kprint_color("[FDC] Floppy write ENABLED\n", TXT_GREEN);
    }
}

int fdc_is_write_protected(void) {
    return fdc_write_protected;
}

int fdc_read_sector_with_retry(uint8_t drive, uint8_t head, uint8_t track, uint8_t sector, uint8_t* buffer) {
    int max_retries = 3;
    
    for(int retry = 0; retry < max_retries; retry++) {
        fdc_read_sector(drive, head, track, sector, buffer);
        
        if(buffer[0] != 0xFF || buffer[1] != 0xFF) {
            if(retry > 0) {
                kprint("[FDC] Success after ");
                kprint_int(retry);
                kprint(" retries\n");
            }
            return 1;
        }
        
        if(retry < max_retries - 1) {
            kprint("[FDC] Retry ");
            kprint_int(retry + 1);
            kprint("/");
            kprint_int(max_retries);
            kprint("\n");
        }
    }
    
    kprint_color("[FDC] Read failed after all retries\n", TXT_RED);
    return 0;
}