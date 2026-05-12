#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>
#include "net.h"

#define RTL8139_VENDOR 0x10EC
#define RTL8139_DEVICE 0x8139

#define RTL8139_REG_MAC0      0x00
#define RTL8139_REG_MAR0      0x08
#define RTL8139_REG_TXSTAT0   0x10
#define RTL8139_REG_TXADDR0   0x20
#define RTL8139_REG_RXBUF     0x30
#define RTL8139_REG_RXEARLY   0x34
#define RTL8139_REG_CMD       0x37
#define RTL8139_REG_CAPR      0x38
#define RTL8139_REG_IMR       0x3C
#define RTL8139_REG_ISR       0x3E
#define RTL8139_REG_TXCFG     0x40
#define RTL8139_REG_RXCFG     0x44
#define RTL8139_REG_MPC       0x4C
#define RTL8139_REG_CFG9346   0x50
#define RTL8139_REG_CONFIG1   0x52
#define RTL8139_REG_TIMERINT  0x54
#define RTL8139_REG_MSR       0x58
#define RTL8139_REG_CONFIG3   0x59
#define RTL8139_REG_CONFIG4   0x5A
#define RTL8139_REG_MULTIINTR 0x5C
#define RTL8139_REG_BMCR      0x62
#define RTL8139_REG_BMSR      0x64
#define RTL8139_REG_ANLPAR    0x68
#define RTL8139_REG_ANER      0x6C

#define RTL8139_CMD_RXEN      0x01
#define RTL8139_CMD_TXEN      0x04
#define RTL8139_CMD_RST       0x10
#define RTL8139_CMD_RE        0x08

#define RTL8139_INT_ROK       0x0001
#define RTL8139_INT_RER       0x0002
#define RTL8139_INT_TOK       0x0004
#define RTL8139_INT_TER       0x0008
#define RTL8139_INT_RXOVW     0x0010
#define RTL8139_INT_PUNLC     0x0020
#define RTL8139_INT_FOVW      0x0040
#define RTL8139_INT_TDU       0x0080
#define RTL8139_INT_TIMEOUT   0x4000
#define RTL8139_INT_SERR      0x8000

#define RTL8139_RXCFG_APM     0x01
#define RTL8139_RXCFG_AB      0x02
#define RTL8139_RXCFG_AM      0x04
#define RTL8139_RXCFG_APM_MASK 0x08
#define RTL8139_RXCFG_WRAP    0x80
#define RTL8139_RXCFG_RXFTH   0x0E00
#define RTL8139_RXCFG_RBLEN   0x1800
#define RTL8139_RXCFG_MXDMA   0x0700
#define RTL8139_RXCFG_RER8    0x0100

#define RTL8139_TXCFG_TXRR    0x00000700
#define RTL8139_TXCFG_MXDMA   0x00007000
#define RTL8139_TXCFG_IFG     0x03000000
#define RTL8139_TXCFG_LOOPBK  0x00060000

#define RTL8139_RBLEN_8K      0
#define RTL8139_RBLEN_16K     1
#define RTL8139_RBLEN_32K     2
#define RTL8139_RBLEN_64K     3

#define RTL8139_RX_READ_POINTER_MASK 0xFFFF

#define RTL8139_TX_BUFFER_SIZE 2048
#define RTL8139_RX_BUFFER_SIZE 32768

typedef struct {
    uint8_t mac[6];
    uint32_t iobase;
    uint8_t* rx_buffer;
    uint32_t rx_buffer_len;
    uint32_t rx_cur;
    uint8_t tx_buffers[4][RTL8139_TX_BUFFER_SIZE];
    int tx_cur;
    netif_t netif;
} rtl8139_t;

int rtl8139_init(void);
int rtl8139_send(netif_t* netif, uint8_t* data, uint32_t len);
int rtl8139_recv(netif_t* netif, uint8_t* buffer, uint32_t max_len);
int rtl8139_link_up(void);

extern rtl8139_t rtl8139_dev;

#endif