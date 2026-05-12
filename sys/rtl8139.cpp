#include "rtl8139.h"
#include "video.h"
#include "kernel_stubs.h"
#include <stdint.h>

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

rtl8139_t rtl8139_dev;

static inline void outb_rtl(uint32_t port, uint8_t val) {
    outb(port, val);
}

static inline uint8_t inb_rtl(uint32_t port) {
    return inb(port);
}

static inline void outw_rtl(uint32_t port, uint16_t val) {
    outw(port, val);
}

static inline uint16_t inw_rtl(uint32_t port) {
    return inw(port);
}

static inline void outl_rtl(uint32_t port, uint32_t val) {
    outl(port, val);
}

static inline uint32_t inl_rtl(uint32_t port) {
    return inl(port);
}

static uint32_t pci_read_config_rtl(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(0xCF8, address);
    return inl(0xCFC);
}

static int rtl8139_find_device(uint32_t* iobase) {
    for(int bus = 0; bus < 256; bus++) {
        for(int slot = 0; slot < 32; slot++) {
            uint32_t vendor = pci_read_config_rtl(bus, slot, 0, 0);
            if((vendor & 0xFFFF) == RTL8139_VENDOR) {
                uint32_t device = vendor >> 16;
                if(device == RTL8139_DEVICE) {
                    kprint("[RTL8139] Found at ");
                    kprint_int(bus); kprint(":");
                    kprint_int(slot); kprint(".0\n");
                    
                    uint32_t bar0 = pci_read_config_rtl(bus, slot, 0, 0x10);
                    *iobase = bar0 & ~0x3;
                    
                    uint32_t cmd = pci_read_config_rtl(bus, slot, 0, 0x04);
                    cmd |= 0x05;
                    outl(0xCF8, (1 << 31) | (bus << 16) | (slot << 11) | (0 << 8) | 0x04);
                    outl(0xCFC, cmd);
                    
                    return 1;
                }
            }
        }
    }
    return 0;
}

int rtl8139_init(void) {
    kprint("[RTL8139] Initializing...\n");
    
    uint32_t iobase = 0;
    if(!rtl8139_find_device(&iobase)) {
        kprint("[RTL8139] Device not found!\n");
        return -1;
    }
    
    rtl8139_dev.iobase = iobase;
    
    kprint("[RTL8139] IO base: 0x");
    kprint_hex32(iobase);
    kprint("\n");
    
    outb_rtl(iobase + RTL8139_REG_CFG9346, 0x00);
    
    outb_rtl(iobase + RTL8139_REG_CMD, RTL8139_CMD_RST);
    for(volatile int i = 0; i < 100000; i++);
    while(inb_rtl(iobase + RTL8139_REG_CMD) & RTL8139_CMD_RST);
    
    for(int i = 0; i < 6; i++) {
        rtl8139_dev.mac[i] = inb_rtl(iobase + RTL8139_REG_MAC0 + i);
    }
    
    kprint("[RTL8139] MAC: ");
    for(int i = 0; i < 6; i++) {
        kprint_hex8(rtl8139_dev.mac[i]);
        if(i < 5) kprint(":");
    }
    kprint("\n");
    
    rtl8139_dev.rx_buffer = (uint8_t*)0x800000;
    rtl8139_dev.rx_buffer_len = RTL8139_RX_BUFFER_SIZE;
    rtl8139_dev.rx_cur = 0;
    rtl8139_dev.tx_cur = 0;
    
    outl_rtl(iobase + RTL8139_REG_RXBUF, (uint32_t)rtl8139_dev.rx_buffer);
    
    outl_rtl(iobase + RTL8139_REG_RXCFG, RTL8139_RXCFG_AB | RTL8139_RXCFG_AM | 
             (RTL8139_RBLEN_32K << 11) | (7 << 8) | RTL8139_RXCFG_WRAP);
    
    outl_rtl(iobase + RTL8139_REG_TXCFG, (7 << 8) | (3 << 24));
    
    outb_rtl(iobase + RTL8139_REG_CMD, RTL8139_CMD_RXEN | RTL8139_CMD_TXEN);
    
    outw_rtl(iobase + RTL8139_REG_IMR, RTL8139_INT_ROK | RTL8139_INT_TOK | 
             RTL8139_INT_RER | RTL8139_INT_TER | RTL8139_INT_RXOVW);
    
    outw_rtl(iobase + RTL8139_REG_MULTIINTR, 0x0000);
    
    for(int i = 0; i < 6; i++) {
        rtl8139_dev.netif.mac.addr[i] = rtl8139_dev.mac[i];
    }
    rtl8139_dev.netif.ip.addr[0] = 10;
    rtl8139_dev.netif.ip.addr[1] = 0;
    rtl8139_dev.netif.ip.addr[2] = 2;
    rtl8139_dev.netif.ip.addr[3] = 15;
    rtl8139_dev.netif.gateway.addr[0] = 10;
    rtl8139_dev.netif.gateway.addr[1] = 0;
    rtl8139_dev.netif.gateway.addr[2] = 2;
    rtl8139_dev.netif.gateway.addr[3] = 1;
    rtl8139_dev.netif.output = rtl8139_send;
    mac_addr_t gateway_mac;
    gateway_mac.addr[0] = 0x52;
    gateway_mac.addr[1] = 0x55;
    gateway_mac.addr[2] = 0x0A;
    gateway_mac.addr[3] = 0x00;
    gateway_mac.addr[4] = 0x02;
    gateway_mac.addr[5] = 0x01;
    
    ip_addr_t gateway_ip;
    gateway_ip.addr[0] = 10;
    gateway_ip.addr[1] = 0;
    gateway_ip.addr[2] = 2;
    gateway_ip.addr[3] = 1;
    
    arp_add(gateway_ip, gateway_mac);
    
    kprint("[RTL8139] Static ARP added for gateway\n");
    
    kprint("[RTL8139] IP: 10.0.2.15\n");
    kprint("[RTL8139] Ready!\n");
    
    return 0;
}

int rtl8139_send(netif_t* netif, uint8_t* data, uint32_t len) {
    if(len > RTL8139_TX_BUFFER_SIZE) return -1;
    
    uint32_t iobase = rtl8139_dev.iobase;
    int tx_idx = rtl8139_dev.tx_cur;
    
    for(uint32_t i = 0; i < len; i++) {
        rtl8139_dev.tx_buffers[tx_idx][i] = data[i];
    }
    
    outl_rtl(iobase + RTL8139_REG_TXADDR0 + tx_idx * 4, 
             (uint32_t)rtl8139_dev.tx_buffers[tx_idx]);
    outl_rtl(iobase + RTL8139_REG_TXSTAT0 + tx_idx * 4, len);
    
    rtl8139_dev.tx_cur = (tx_idx + 1) % 4;
    
    return len;
}

int rtl8139_recv(netif_t* netif, uint8_t* buffer, uint32_t max_len) {
    uint32_t iobase = rtl8139_dev.iobase;
    
    uint16_t cmd = inb_rtl(iobase + RTL8139_REG_CMD);
    if(!(cmd & RTL8139_CMD_RXEN)) return 0;
    
    uint32_t rx_cur = rtl8139_dev.rx_cur;
    uint8_t* rx_buf = rtl8139_dev.rx_buffer;
    
    uint16_t status = *(uint16_t*)(rx_buf + rx_cur);
    uint16_t pkt_len = *(uint16_t*)(rx_buf + rx_cur + 2);
    
    if(!(status & RTL8139_INT_ROK)) return 0;
    
    if(pkt_len > max_len) pkt_len = max_len;
    
    for(uint16_t i = 0; i < pkt_len; i++) {
        buffer[i] = rx_buf[rx_cur + 4 + i];
    }
    
    rx_cur = (rx_cur + pkt_len + 4 + 3) & ~3;
    if(rx_cur >= rtl8139_dev.rx_buffer_len) rx_cur -= rtl8139_dev.rx_buffer_len;
    
    rtl8139_dev.rx_cur = rx_cur;
    outw_rtl(iobase + RTL8139_REG_CAPR, rx_cur - 16);
    
    return pkt_len;
}

int rtl8139_link_up(void) {
    uint32_t iobase = rtl8139_dev.iobase;
    uint16_t bmsr = inw(iobase + RTL8139_REG_BMSR);
    kprint("[RTL8139] BMSR reg: 0x");
    kprint_hex16(bmsr);
    kprint("\n");
    return (bmsr & 0x0004) ? 1 : 0;
}