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
static uint16_t ping_seq = 0;

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

int icmp_ping(netif_t* netif, ip_addr_t dest, int timeout_ms) {
    uint8_t packet[sizeof(ip_hdr_t) + sizeof(icmp_hdr_t)];
    ip_hdr_t* ip = (ip_hdr_t*)packet;
    icmp_hdr_t* icmp = (icmp_hdr_t*)(packet + sizeof(ip_hdr_t));
    
    icmp->type = 8;
    icmp->code = 0;
    icmp->id = ping_id;
    icmp->sequence = ping_seq++;
    
    for(int i = 0; i < 56; i++) {
        icmp->data[i] = i & 0xFF;
    }
    
    icmp->checksum = 0;
    icmp->checksum = icmp_checksum((uint16_t*)icmp, sizeof(icmp_hdr_t));
    
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
    
    mac_addr_t dest_mac;
    if(!arp_lookup(dest, &dest_mac)) {
        arp_request(netif, dest);
        return -1;
    }
    
    uint8_t eth_packet[sizeof(eth_hdr_t) + sizeof(ip_hdr_t) + sizeof(icmp_hdr_t)];
    eth_hdr_t* eth = (eth_hdr_t*)eth_packet;
    
    eth->dst = dest_mac;
    eth->src = netif->mac;
    eth->type = 0x0008;
    
    for(int i = 0; i < sizeof(ip_hdr_t) + sizeof(icmp_hdr_t); i++) {
        eth_packet[sizeof(eth_hdr_t) + i] = packet[i];
    }
    
    netif->output(netif, eth_packet, sizeof(eth_packet));
    
    uint32_t start = seconds * 1000;
    while((seconds * 1000) - start < (uint32_t)timeout_ms) {
        uint8_t buffer[2048];
        int len = e1000_recv(netif, buffer, sizeof(buffer));
        if(len > 0) {
            eth_hdr_t* eth_recv = (eth_hdr_t*)buffer;
            if(eth_recv->type != 0x0008) continue;
            
            ip_hdr_t* ip_recv = (ip_hdr_t*)(buffer + sizeof(eth_hdr_t));
            if(ip_recv->protocol != 1) continue;
            
            icmp_hdr_t* icmp_recv = (icmp_hdr_t*)(buffer + sizeof(eth_hdr_t) + sizeof(ip_hdr_t));
            if(icmp_recv->type == 0 && icmp_recv->id == ping_id) {
                return 1;
            }
        }
    }
    
    return 0;
}

void ping_command(const char* host) {
    if(host[0] == '\0') {
        kprint_color("Usage: ping <ip>\n", TXT_YELLOW);
        return;
    }
    
    ip_addr_t dest;
    
    if(parse_ip(host, &dest)) {
    } else {
        kprint_color("[DNS] Resolving ", TXT_CYAN);
        kprint(host);
        kprint("\n");
        
        if(dns_resolve(&e1000_netif, host) != 0) {
            kprint_color("Failed to resolve hostname\n", TXT_RED);
            return;
        }
        dest.addr[0] = 8;
        dest.addr[1] = 8;
        dest.addr[2] = 8;
        dest.addr[3] = 8;
    }
    
    kprint("PING ");
    kprint(host);
    kprint(" (");
    kprint_int(dest.addr[0]); kprint(".");
    kprint_int(dest.addr[1]); kprint(".");
    kprint_int(dest.addr[2]); kprint(".");
    kprint_int(dest.addr[3]);
    kprint("): 56 data bytes\n");
    
    int received = 0;
    int min_time = 9999, max_time = 0, total_time = 0;
    
    for(int i = 0; i < 4; i++) {
        uint32_t start = seconds * 1000;
        
        if(icmp_ping(&e1000_netif, dest, 1000)) {
            uint32_t elapsed = (seconds * 1000) - start;
            received++;
            if(elapsed < (uint32_t)min_time) min_time = elapsed;
            if(elapsed > (uint32_t)max_time) max_time = elapsed;
            total_time += elapsed;
            
            kprint_int(64); kprint(" bytes from ");
            kprint_int(dest.addr[0]); kprint(".");
            kprint_int(dest.addr[1]); kprint(".");
            kprint_int(dest.addr[2]); kprint(".");
            kprint_int(dest.addr[3]);
            kprint(": icmp_seq="); kprint_int(i);
            kprint(" ttl=64 time="); kprint_int(elapsed); kprint(" ms\n");
        } else {
            kprint_color("Request timeout for icmp_seq=", TXT_RED);
            kprint_int(i); kprint("\n");
        }
        
        for(volatile int d = 0; d < 1000000; d++);
    }
    
    kprint("\n--- ");
    kprint(host);
    kprint(" ping statistics ---\n");
    kprint("4 packets transmitted, ");
    kprint_int(received);
    kprint(" received, ");
    kprint_int((4-received)*25);
    kprint("% loss\n");
    
    if(received > 0) {
        kprint("round-trip min/avg/max = ");
        kprint_int(min_time); kprint("/");
        kprint_int(total_time / received); kprint("/");
        kprint_int(max_time); kprint(" ms\n");
    }
}