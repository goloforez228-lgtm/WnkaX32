#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

#define COM1_PORT 0x3F8
#define COM2_PORT 0x2F8
#define COM3_PORT 0x3E8
#define COM4_PORT 0x2E8

#define COM_DATA   0
#define COM_IER    1
#define COM_IIR    2
#define COM_LCR    3
#define COM_MCR    4
#define COM_LSR    5
#define COM_MSR    6

#define COM_BAUD_115200 1
#define COM_BAUD_57600   2
#define COM_BAUD_38400   3
#define COM_BAUD_19200   6
#define COM_BAUD_9600    12

#define COM_LSR_DATA_READY  0x01
#define COM_LSR_TX_EMPTY    0x20
#define COM_LSR_RX_READY    0x01

void serial_init(uint16_t port, uint16_t baud);
void serial_write_char(uint16_t port, char c);
char serial_read_char(uint16_t port);
void serial_write_string(uint16_t port, const char* str);
int serial_received(uint16_t port);
int serial_is_transmit_empty(uint16_t port);

#endif