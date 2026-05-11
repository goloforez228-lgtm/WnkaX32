#ifndef ICMP_H
#define ICMP_H

#include "net.h"

#define ICMP_ECHO_REPLY   0
#define ICMP_ECHO_REQUEST 8

typedef struct {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
    uint8_t data[56];
} __attribute__((packed)) icmp_hdr_t;

uint16_t icmp_checksum(uint16_t* buffer, int size);
int icmp_ping(netif_t* netif, ip_addr_t dest, int timeout_ms);
void ping_command(const char* host);
void icmp_handle_packet(netif_t* netif, uint8_t* packet, uint32_t len);

#endif