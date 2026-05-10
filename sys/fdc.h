#ifndef FDC_H
#define FDC_H

#include <stdint.h>

#define FDC_DOR     0x3F2
#define FDC_MSR     0x3F4
#define FDC_FIFO    0x3F5
#define FDC_CCR     0x3F7
#define FDC_CMD_READ_DATA   0x06
#define FDC_CMD_WRITE_DATA  0x05
#define FDC_CMD_SPECIFY     0x03
#define FDC_CMD_SENSE_INT   0x08
#define FDC_CMD_RECALIBRATE 0x07

int  fdc_init(void);
void fdc_read_sector(uint8_t drive, uint8_t head, uint8_t track, uint8_t sector, uint8_t* buffer);
void fdc_write_sector(uint8_t drive, uint8_t head, uint8_t track, uint8_t sector, uint8_t* buffer);
void fdc_set_write_protect(int enable);
int  fdc_is_write_protected(void);

#endif