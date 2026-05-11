#include "video.h"
#include "ahci.h"
#include "net.h"

#define E1000_VENDOR 0x8086
#define E1000_DEVICE 0x100E
#define E1000_REG_CTRL     0x0000
#define E1000_REG_STATUS   0x0008
#define E1000_REG_RCTL     0x0100
#define E1000_REG_TCTL     0x0400
#define E1000_REG_RDBAL    0x2800
#define E1000_REG_RDBAH    0x2804
#define E1000_REG_RDLEN    0x2808
#define E1000_REG_RDH      0x2810
#define E1000_REG_RDT      0x2818
#define E1000_REG_TDBAL    0x3800
#define E1000_REG_TDBAH    0x3804
#define E1000_REG_TDLEN    0x3808
#define E1000_REG_TDH      0x3810
#define E1000_REG_TDT      0x3818
#define E1000_CTRL_FD       0x00000001  
#define E1000_CTRL_ASDE     0x00000020 
#define E1000_CTRL_SLU      0x00000040
#define E1000_RCTL_EN       0x00000002
#define E1000_TCTL_EN       0x00000002
#define E1000_TCTL_PSP      0x00000008

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed)) e1000_rx_desc_t;

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed)) e1000_tx_desc_t;

static volatile uint32_t* e1000_base = 0;
static uint8_t rx_buffers[32][2048] __attribute__((aligned(16)));
static e1000_rx_desc_t rx_desc[32] __attribute__((aligned(16)));
static uint8_t tx_buffers[32][2048] __attribute__((aligned(16)));
static e1000_tx_desc_t tx_desc[32] __attribute__((aligned(16)));
static netif_t e1000_netif;

static int e1000_find(void) {
    for(int bus = 0; bus < 256; bus++) {
        for(int slot = 0; slot < 32; slot++) {
            for(int func = 0; func < 8; func++) {
                uint32_t vendor = pci_read(bus, slot, func, 0);
                uint32_t device = vendor >> 16;
                vendor &= 0xFFFF;
                
                if(vendor == E1000_VENDOR && device == E1000_DEVICE) {
                    kprint("[E1000] Found at ");
                    kprint_int(bus); kprint(":");
                    kprint_int(slot); kprint(".");
                    kprint_int(func); kprint("\n");
                    
                    uint32_t bar0 = pci_read(bus, slot, func, 0x10);
                    if(bar0 & 1) bar0 &= ~1;
                    
                    e1000_base = (uint32_t*)(uintptr_t)bar0;
                    
                    uint32_t cmd = pci_read(bus, slot, func, 0x04);
                    cmd |= 0x06;
                    pci_write(bus, slot, func, 0x04, cmd);
                    
                    return 1;
                }
            }
        }
    }
    return 0;
}

int e1000_init(void) {
    if(!e1000_find()) {
        kprint("[E1000] Not found\n");
        return -1;
    }
    
    kprint("[E1000] Initializing...\n");
    
    e1000_base[E1000_REG_CTRL/4] |= 0x04000000;
    for(volatile int i = 0; i < 1000000; i++);
    
    for(int i = 0; i < 32; i++) {
        rx_desc[i].addr = (uint64_t)(uintptr_t)rx_buffers[i];
        rx_desc[i].status = 0;
    }
    
    e1000_base[E1000_REG_RDBAL/4] = (uint32_t)(uintptr_t)rx_desc;
    e1000_base[E1000_REG_RDBAH/4] = 0;
    e1000_base[E1000_REG_RDLEN/4] = sizeof(rx_desc);
    e1000_base[E1000_REG_RDH/4] = 0;
    e1000_base[E1000_REG_RDT/4] = 32 - 1;
    
    for(int i = 0; i < 32; i++) {
        tx_desc[i].addr = (uint64_t)(uintptr_t)tx_buffers[i];
        tx_desc[i].cmd = 0;
        tx_desc[i].status = 0;
    }
    
    e1000_base[E1000_REG_TDBAL/4] = (uint32_t)(uintptr_t)tx_desc;
    e1000_base[E1000_REG_TDBAH/4] = 0;
    e1000_base[E1000_REG_TDLEN/4] = sizeof(tx_desc);
    e1000_base[E1000_REG_TDH/4] = 0;
    e1000_base[E1000_REG_TDT/4] = 0;
    
    e1000_base[E1000_REG_RCTL/4] = E1000_RCTL_EN | 0x04008002;
    e1000_base[E1000_REG_TCTL/4] = E1000_TCTL_EN | E1000_TCTL_PSP | 0x0000F000;
    e1000_base[E1000_REG_CTRL/4] |= E1000_CTRL_SLU | E1000_CTRL_FD | E1000_CTRL_ASDE;
    
    int timeout = 10000000;
    while(timeout--) {
        if(e1000_base[E1000_REG_STATUS/4] & 0x02) break;
    }
    
    uint32_t mac_low = e1000_base[0x5400/4];
    uint32_t mac_high = e1000_base[0x5404/4];
    
    e1000_netif.mac.addr[0] = mac_low & 0xFF;
    e1000_netif.mac.addr[1] = (mac_low >> 8) & 0xFF;
    e1000_netif.mac.addr[2] = (mac_low >> 16) & 0xFF;
    e1000_netif.mac.addr[3] = (mac_low >> 24) & 0xFF;
    e1000_netif.mac.addr[4] = mac_high & 0xFF;
    e1000_netif.mac.addr[5] = (mac_high >> 8) & 0xFF;
    
    kprint("[E1000] MAC: ");
    for(int i = 0; i < 6; i++) {
        kprint_hex8(e1000_netif.mac.addr[i]);
        if(i < 5) kprint(":");
    }
    kprint("\n");
    
    e1000_netif.ip.addr[0] = 10;
    e1000_netif.ip.addr[1] = 0;
    e1000_netif.ip.addr[2] = 2;
    e1000_netif.ip.addr[3] = 15;
    
    e1000_netif.gateway.addr[0] = 10;
    e1000_netif.gateway.addr[1] = 0;
    e1000_netif.gateway.addr[2] = 2;
    e1000_netif.gateway.addr[3] = 1;
    
    kprint("[E1000] Ready\n");
    return 0;
}

int e1000_send(netif_t* netif, uint8_t* data, uint32_t len) {
    if(len > 2048) return -1;
    
    uint32_t tdt = e1000_base[E1000_REG_TDT/4];
    uint32_t next = tdt;
    
    for(uint32_t i = 0; i < len; i++) {
        tx_buffers[next][i] = data[i];
    }
    
    tx_desc[next].length = len;
    tx_desc[next].cmd = 0x0F;
    tx_desc[next].status = 0;
    
    e1000_base[E1000_REG_TDT/4] = (next + 1) % 32;
    
    return len;
}

int e1000_recv(netif_t* netif, uint8_t* buffer, uint32_t max_len) {
    uint32_t tdt = e1000_base[E1000_REG_TDT/4];
    uint32_t rdh = e1000_base[E1000_REG_RDH/4];
    
    if(rdh == tdt) return 0;  
    
    if(!(rx_desc[rdh].status & 0x01)) return 0;
    
    uint16_t len = rx_desc[rdh].length;
    if(len > max_len) len = max_len;
    
    for(uint16_t i = 0; i < len; i++) {
        buffer[i] = rx_buffers[rdh][i];
    }
    
    rx_desc[rdh].status = 0;
    e1000_base[E1000_REG_RDH/4] = (rdh + 1) % 32;
    
    return len;
}