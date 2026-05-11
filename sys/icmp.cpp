#include "video.h"
#include "net.h"
#include "graph.h"
#include "e1000.h"
#include <stdint.h>

typedef struct {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
    uint8_t data[56];
} __attribute__((packed)) icmp_hdr_t;

static uint16_t ping_id = 0x1234;
uint16_t ping_seq = 0;

static uint16_t icmp_checksum(uint16_t* buffer, int size) {
    uint32_t sum = 0;
    for(int i = 0; i < size / 2; i++) {
        sum += buffer[i];
    }
    while(sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return ~sum & 0xFFFF;
}

static int parse_ip(const char* str, ip_addr_t* ip) {
    int a = 0, b = 0, c = 0, d = 0;
    int i = 0;
    
    while(str[i] >= '0' && str[i] <= '9') {
        a = a * 10 + (str[i] - '0');
        i++;
    }
    if(str[i] != '.') return 0;
    i++;
    
    while(str[i] >= '0' && str[i] <= '9') {
        b = b * 10 + (str[i] - '0');
        i++;
    }
    if(str[i] != '.') return 0;
    i++;
    
    while(str[i] >= '0' && str[i] <= '9') {
        c = c * 10 + (str[i] - '0');
        i++;
    }
    if(str[i] != '.') return 0;
    i++;
    
    while(str[i] >= '0' && str[i] <= '9') {
        d = d * 10 + (str[i] - '0');
        i++;
    }
    
    ip->addr[0] = a;
    ip->addr[1] = b;
    ip->addr[2] = c;
    ip->addr[3] = d;
    return 1;
}

static void delay_loop(int ms) {
    for(int i = 0; i < ms * 1000; i++) {
        for(volatile int j = 0; j < 100; j++);
    }
}

int icmp_ping(netif_t* netif, ip_addr_t dest, int timeout_ms) {
    (void)timeout_ms;
    
    if(!netif) return 0;
    
    static uint8_t packet[200];
    
    // Обнуляем
    for(int i = 0; i < 200; i++) packet[i] = 0;
    
    // === ICMP HEADER ===
    icmp_hdr_t* icmp = (icmp_hdr_t*)(packet + sizeof(eth_hdr_t) + sizeof(ip_hdr_t));
    icmp->type = 8;
    icmp->code = 0;
    icmp->id = ping_id;
    icmp->sequence = ping_seq++;
    
    for(int i = 0; i < 56; i++) {
        icmp->data[i] = i & 0xFF;
    }
    
    icmp->checksum = 0;
    icmp->checksum = icmp_checksum((uint16_t*)icmp, sizeof(icmp_hdr_t));
    
    // === IP HEADER ===
    ip_hdr_t* ip = (ip_hdr_t*)(packet + sizeof(eth_hdr_t));
    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = sizeof(ip_hdr_t) + sizeof(icmp_hdr_t);
    ip->id = ping_seq;
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->protocol = 1;
    ip->checksum = 0;
    ip->src = netif->ip;
    ip->dst = dest;
    ip->checksum = icmp_checksum((uint16_t*)ip, sizeof(ip_hdr_t));
    
    // === ARP LOOKUP ===
    mac_addr_t dest_mac;
    if(!arp_lookup(dest, &dest_mac)) {
        arp_request(netif, dest);
        for(int retry = 0; retry < 30; retry++) {
            delay_loop(10);
            if(arp_lookup(dest, &dest_mac)) break;
        }
        if(!arp_lookup(dest, &dest_mac)) {
            return 0;
        }
    }
    
    // === ETH HEADER ===
    eth_hdr_t* eth = (eth_hdr_t*)packet;
    eth->dst = dest_mac;
    eth->src = netif->mac;
    eth->type = 0x0008;
    
    // === SEND ===
    int packet_len = sizeof(eth_hdr_t) + sizeof(ip_hdr_t) + sizeof(icmp_hdr_t);
    int result = netif->output(netif, packet, packet_len);
    
    // Просто возвращаем успех, не ждём ответа
    return (result > 0) ? 1 : 0;
}

void ping_command(const char* host) {
    if(host[0] == '\0') {
        kprint_color("Usage: ping <ip>\n", TXT_YELLOW);
        return;
    }
    
    ip_addr_t dest;
    
    if(!parse_ip(host, &dest)) {
        kprint_color("Invalid IP address\n", TXT_RED);
        return;
    }
    
    kprint("PING ");
    kprint(host);
    kprint(" (");
    kprint_int(dest.addr[0]); kprint(".");
    kprint_int(dest.addr[1]); kprint(".");
    kprint_int(dest.addr[2]); kprint(".");
    kprint_int(dest.addr[3]);
    kprint("): 56 data bytes\n");
    
    int sent = 0;
    
    for(int i = 0; i < 4; i++) {
        kprint("  Request ");
        kprint_int(i);
        kprint("... ");
        
        int result = icmp_ping(&e1000_netif, dest, 2000);
        
        if(result) {
            sent++;
            kprint_color("Sent\n", TXT_GREEN);
        } else {
            kprint_color("Failed\n", TXT_RED);
        }
        
        delay_loop(100);
    }
    
    kprint("\n--- ");
    kprint(host);
    kprint(" ping statistics ---\n");
    kprint("4 packets sent, ");
    kprint_int(sent);
    kprint(" sent\n");
    kprint("(Reply handling disabled for debugging)\n");
}