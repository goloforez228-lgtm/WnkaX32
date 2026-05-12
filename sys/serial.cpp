#include "serial.h"
#include "kernel_stubs.h"
#include "video.h"

void serial_init(uint16_t port, uint16_t baud) {
    outb(port + COM_IER, 0x00);
    outb(port + COM_LCR, 0x80);
    outb(port + COM_DATA, baud & 0xFF);
    outb(port + COM_IER, (baud >> 8) & 0xFF);
    outb(port + COM_LCR, 0x03);
    outb(port + COM_MCR, 0x03);
    outb(port + COM_IER, 0x00);
    
    kprint("[SERIAL] Port 0x");
    kprint_hex16(port);
    kprint(" initialized at ");
    kprint_int(115200 / baud);
    kprint(" baud\n");
}

int serial_is_transmit_empty(uint16_t port) {
    return inb(port + COM_LSR) & COM_LSR_TX_EMPTY;
}

void serial_write_char(uint16_t port, char c) {
    while (!serial_is_transmit_empty(port));
    outb(port + COM_DATA, c);
}

void serial_write_string(uint16_t port, const char* str) {
    for (int i = 0; str[i]; i++) {
        if (str[i] == '\n') serial_write_char(port, '\r');
        serial_write_char(port, str[i]);
    }
}

int serial_received(uint16_t port) {
    return inb(port + COM_LSR) & COM_LSR_RX_READY;
}

char serial_read_char(uint16_t port) {
    while (!serial_received(port));
    return inb(port + COM_DATA);
}