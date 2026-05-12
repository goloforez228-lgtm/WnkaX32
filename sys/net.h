#ifndef NET_H
#define NET_H

#include <stdint.h>

typedef struct {
    uint8_t addr[6];
} mac_addr_t;

typedef struct {
    uint8_t addr[4];
} ip_addr_t;

typedef struct {
    mac_addr_t dst;
    mac_addr_t src;
    uint16_t type;
} __attribute__((packed)) eth_hdr_t;

typedef struct {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t hw_size;
    uint8_t proto_size;
    uint16_t opcode;
    mac_addr_t sender_mac;
    ip_addr_t sender_ip;
    mac_addr_t target_mac;
    ip_addr_t target_ip;
} __attribute__((packed)) arp_hdr_t;

typedef struct {
    uint8_t version_ihl;
    uint8_t tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    ip_addr_t src;
    ip_addr_t dst;
} __attribute__((packed)) ip_hdr_t;

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t len;
    uint16_t checksum;
} __attribute__((packed)) udp_hdr_t;

typedef struct netif {
    char name[16];
    mac_addr_t mac;
    ip_addr_t ip;
    ip_addr_t netmask;
    ip_addr_t gateway;
    int (*output)(struct netif* netif, uint8_t* data, uint32_t len);
    void* state;
} netif_t;

int arp_lookup(ip_addr_t ip, mac_addr_t* mac);
void arp_add(ip_addr_t ip, mac_addr_t mac);
void arp_request(netif_t* netif, ip_addr_t target_ip);
void arp_handle(netif_t* netif, uint8_t* packet, uint32_t len);
void arp_init(void);
int dns_resolve(netif_t* netif, const char* hostname);
int browse_url(netif_t* netif, const char* url);
int icmp_ping(netif_t* netif, ip_addr_t dest, int timeout_ms);
void ping_command(const char* host);

extern netif_t e1000_netif;
extern uint16_t ping_seq;

#endif