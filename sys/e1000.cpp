#include "video.h"
#include "net.h"
#include "kernel_stubs.h"
#include "ahci.h"
#include "graph.h"
#include <stdint.h>



#define NULL 0
volatile uint32_t* e1000_base = NULL;

#define E1000_VENDOR 0x8086
#define E1000_DEVICE 0x100E
#define E1000_CTRL     0x0000
#define E1000_STATUS   0x0008
#define E1000_EECD     0x0010
#define E1000_RCTL     0x0100
#define E1000_TCTL     0x0400
#define E1000_IMS      0x00D0
#define E1000_ICR      0x00C0
#define E1000_RDBAL    0x2800
#define E1000_RDBAH    0x2804
#define E1000_RDLEN    0x2808
#define E1000_RDH      0x2810
#define E1000_RDT      0x2818
#define E1000_TDBAL    0x3800
#define E1000_TDBAH    0x3804
#define E1000_TDLEN    0x3808
#define E1000_TDH      0x3810
#define E1000_TDT      0x3818
#define E1000_RA       0x5400
#define E1000_MTA      0x5200
#define E1000_MANC     0x5820

typedef struct {
    volatile uint64_t addr;
    volatile uint16_t length;
    volatile uint16_t checksum;
    volatile uint8_t status;
    volatile uint8_t errors;
    volatile uint16_t special;
} __attribute__((packed)) e1000_rx_desc_t;

typedef struct {
    volatile uint64_t addr;
    volatile uint16_t length;
    volatile uint8_t cso;
    volatile uint8_t cmd;
    volatile uint8_t status;
    volatile uint8_t css;
    volatile uint16_t special;
} __attribute__((packed)) e1000_tx_desc_t;

static volatile uint32_t* e1000 = NULL;
static e1000_rx_desc_t rx_desc[256] __attribute__((aligned(16)));
static e1000_tx_desc_t tx_desc[256] __attribute__((aligned(16)));
static uint8_t rx_buffers[256][2048] __attribute__((aligned(16)));
static uint8_t tx_buffers[256][2048] __attribute__((aligned(16)));

static uint16_t rx_cur = 0;
static uint16_t tx_cur = 0;
static uint8_t mac_addr[6];

netif_t e1000_netif;

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(0xCF8, address);
    return inl(0xCFC);
}

static void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(0xCF8, address);
    outl(0xCFC, value);
}

static int e1000_find_device(void) {
    for(int bus = 0; bus < 256; bus++) {
        for(int slot = 0; slot < 32; slot++) {
            uint32_t vendor = pci_read_config(bus, slot, 0, 0);
            if((vendor & 0xFFFF) == E1000_VENDOR) {
                uint32_t device = vendor >> 16;
                if(device == E1000_DEVICE) {
                    kprint("[E1000] Found at ");
                    kprint_int(bus); kprint(":");
                    kprint_int(slot); kprint(".0\n");
                    
                    uint32_t bar0 = pci_read_config(bus, slot, 0, 0x10);
                    e1000 = (uint32_t*)(uintptr_t)(bar0 & ~0xF);
                    
                    uint32_t cmd = pci_read_config(bus, slot, 0, 0x04);
                    cmd |= 0x06;
                    pci_write_config(bus, slot, 0, 0x04, cmd);
                    
                    return 1;
                }
            }
        }
    }
    return 0;
}

static void e1000_delay(int ms) {
    for(volatile int i = 0; i < ms * 100000; i++);
}

static void e1000_read_mac(void) {
    uint32_t mac_low = e1000[E1000_RA / 4];
    uint32_t mac_high = e1000[(E1000_RA + 4) / 4];
    
    mac_addr[0] = mac_low & 0xFF;
    mac_addr[1] = (mac_low >> 8) & 0xFF;
    mac_addr[2] = (mac_low >> 16) & 0xFF;
    mac_addr[3] = (mac_low >> 24) & 0xFF;
    mac_addr[4] = mac_high & 0xFF;
    mac_addr[5] = (mac_high >> 8) & 0xFF;
}

static void e1000_init_rx(void) {
    for(int i = 0; i < 256; i++) {
        rx_desc[i].addr = (uint64_t)(uintptr_t)rx_buffers[i];
        rx_desc[i].status = 0;
    }
    
    e1000[E1000_RDBAL / 4] = (uint32_t)(uintptr_t)rx_desc;
    e1000[E1000_RDBAH / 4] = (uint32_t)((uint64_t)(uintptr_t)rx_desc >> 32);
    e1000[E1000_RDLEN / 4] = sizeof(rx_desc);
    e1000[E1000_RDH / 4] = 0;
    e1000[E1000_RDT / 4] = 255;
    
    e1000[E1000_RCTL / 4] = 0x00008002 | (1 << 6) | (1 << 7);
}

static void e1000_init_tx(void) {
    for(int i = 0; i < 256; i++) {
        tx_desc[i].addr = (uint64_t)(uintptr_t)tx_buffers[i];
        tx_desc[i].cmd = 0;
        tx_desc[i].status = 0;
    }
    
    e1000[E1000_TDBAL / 4] = (uint32_t)(uintptr_t)tx_desc;
    e1000[E1000_TDBAH / 4] = (uint32_t)((uint64_t)(uintptr_t)tx_desc >> 32);
    e1000[E1000_TDLEN / 4] = sizeof(tx_desc);
    e1000[E1000_TDH / 4] = 0;
    e1000[E1000_TDT / 4] = 0;
    
    e1000[E1000_TCTL / 4] = 0x0000F040 | (1 << 1);
}

static void e1000_reset(void) {
    e1000[E1000_CTRL / 4] |= (1 << 26);
    e1000_delay(10);
    while(e1000[E1000_CTRL / 4] & (1 << 26));
    e1000_delay(10);
}

static void e1000_enable_interrupts(void) {
    e1000[E1000_IMS / 4] = 0x1F;
}

int e1000_init(void) {
    kprint("[E1000] Initializing...\n");
    
    if(!e1000_find_device()) {
        kprint("[E1000] Device not found!\n");
        return -1;
    }
    
    kprint("[E1000] Registers at 0x");
    kprint_hex32((uint32_t)e1000);
    kprint("\n");
    
    e1000_reset();
    e1000_read_mac();
    e1000_init_rx();
    e1000_init_tx();
    e1000_enable_interrupts();
    
    e1000[E1000_RCTL / 4] |= 1;
    e1000[E1000_TCTL / 4] |= 1;
    
    kprint("[E1000] MAC: ");
    for(int i = 0; i < 6; i++) {
        kprint_hex8(mac_addr[i]);
        if(i < 5) kprint(":");
    }
    kprint("\n");
    
    for(int i = 0; i < 6; i++) {
        e1000_netif.mac.addr[i] = mac_addr[i];
    }
    e1000_netif.ip.addr[0] = 10;
    e1000_netif.ip.addr[1] = 0;
    e1000_netif.ip.addr[2] = 2;
    e1000_netif.ip.addr[3] = 15;
    
    kprint("[E1000] IP: 10.0.2.15\n");
    kprint("[E1000] Ready!\n");
    
    return 0;
}

int e1000_send(netif_t* netif, uint8_t* data, uint32_t len) {
    (void)netif;
    
    if(!e1000_base) return -1;
    if(len > 2048) return -1;
    
    uint32_t tdt = e1000_base[E1000_TDT/4];
    uint32_t next = tdt;
    
    for(uint32_t i = 0; i < len; i++) {
        tx_buffers[next][i] = data[i];
    }
    
    tx_desc[next].length = len;
    tx_desc[next].cmd = 0x0F;
    tx_desc[next].status = 0;
    
    e1000_base[E1000_TDT/4] = (next + 1) % 32;
    
    return len;
}

int e1000_recv(netif_t* netif, uint8_t* buffer, uint32_t max_len) {
    (void)netif;
    
    if(!e1000_base) return 0;
    
    uint32_t rdh = e1000_base[E1000_RDH/4];
    uint32_t rdt = e1000_base[E1000_RDT/4];
    
    if(rdh == rdt) return 0;
    
    if(!(rx_desc[rdh].status & 0x01)) return 0;
    
    uint16_t len = rx_desc[rdh].length;
    if(len > max_len) len = max_len;
    
    for(uint16_t i = 0; i < len; i++) {
        buffer[i] = rx_buffers[rdh][i];
    }
    
    rx_desc[rdh].status = 0;
    e1000_base[E1000_RDT/4] = (rdh + 1) % 32;
    
    return len;
}

int e1000_link_up(void) {
    if(!e1000_base) return 0;
    uint32_t status = e1000_base[E1000_STATUS/4];
    kprint("[E1000] STATUS reg: 0x");
    kprint_hex32(status);
    kprint("\n");
    return (status & 0x02) ? 1 : 0;
}

void e1000_dump_stats(void) {
    if(!e1000_base) {
        kprint("[E1000] Not initialized\n");
        return;
    }
    
    kprint("\n=== E1000 STATISTICS ===\n");
    kprint("Status: 0x"); kprint_hex32(e1000_base[E1000_STATUS/4]); kprint("\n");
    kprint("Link: ");
    if(e1000_link_up()) kprint_color("UP\n", TXT_GREEN);
    else kprint_color("DOWN\n", TXT_RED);
    kprint("RX Descriptors: "); kprint_int(e1000_base[E1000_RDH/4]); kprint("/");
    kprint_int(e1000_base[E1000_RDT/4]); kprint("\n");
    kprint("TX Descriptors: "); kprint_int(e1000_base[E1000_TDH/4]); kprint("/");
    kprint_int(e1000_base[E1000_TDT/4]); kprint("\n");
}
int dns_resolve(netif_t* netif, const char* hostname) {
    (void)netif;
    kprint("[DNS] Resolving ");
    kprint(hostname);
    kprint("\n");
    return 0;
}

int browse_url(netif_t* netif, const char* url) {
    (void)netif;
    kprint("[BROWSER] Opening ");
    kprint(url);
    kprint("\n");
    return 0;
}