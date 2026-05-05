#include "net.h"

#define UDP_MAX_SOCKETS 16

typedef struct {
    int used;
    uint16_t port;
    void (*callback)(uint8_t* data, uint32_t len, ip_addr_t src, uint16_t src_port);
} udp_socket_t;

static udp_socket_t udp_sockets[UDP_MAX_SOCKETS];

void udp_init(void) {
    for(int i = 0; i < UDP_MAX_SOCKETS; i++) {
        udp_sockets[i].used = 0;
    }
}

uint16_t udp_checksum(ip_addr_t src, ip_addr_t dst, uint8_t* data, uint32_t len) {
    uint32_t sum = 0;
    
    sum += (src.addr[0] << 8) | src.addr[1];
    sum += (src.addr[2] << 8) | src.addr[3];
    sum += (dst.addr[0] << 8) | dst.addr[1];
    sum += (dst.addr[2] << 8) | dst.addr[3];
    sum += 17;
    sum += len;
    
    uint16_t* words = (uint16_t*)data;
    uint32_t word_len = (len + 1) / 2;
    for(uint32_t i = 0; i < word_len; i++) {
        sum += words[i];
    }
    
    while(sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum & 0xFFFF;
}

int udp_bind(uint16_t port, void (*callback)(uint8_t*, uint32_t, ip_addr_t, uint16_t)) {
    for(int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if(!udp_sockets[i].used) {
            udp_sockets[i].used = 1;
            udp_sockets[i].port = port;
            udp_sockets[i].callback = callback;
            return i;
        }
    }
    return -1;
}

int udp_send(netif_t* netif, ip_addr_t dst, uint16_t dst_port, uint16_t src_port, uint8_t* data, uint32_t len) {
    mac_addr_t dst_mac;
    if(!arp_lookup(dst, &dst_mac)) {
        arp_request(netif, dst);
        return -1;
    }
    
    uint8_t packet[sizeof(eth_hdr_t) + sizeof(ip_hdr_t) + sizeof(udp_hdr_t) + len];
    
    eth_hdr_t* eth = (eth_hdr_t*)packet;
    eth->dst = dst_mac;
    eth->src = netif->mac;
    eth->type = 0x0008;  
    ip_hdr_t* ip = (ip_hdr_t*)(packet + sizeof(eth_hdr_t));
    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = (sizeof(ip_hdr_t) + sizeof(udp_hdr_t) + len);
    ip->id = 0;
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->protocol = 17;
    ip->checksum = 0;
    ip->src = netif->ip;
    ip->dst = dst;
    udp_hdr_t* udp = (udp_hdr_t*)(packet + sizeof(eth_hdr_t) + sizeof(ip_hdr_t));
    udp->src_port = src_port;
    udp->dst_port = dst_port;
    udp->len = sizeof(udp_hdr_t) + len;
    udp->checksum = 0;
    uint8_t* udp_data = (uint8_t*)(packet + sizeof(eth_hdr_t) + sizeof(ip_hdr_t) + sizeof(udp_hdr_t));
    for(uint32_t i = 0; i < len; i++) {
        udp_data[i] = data[i];
    }
    uint32_t sum = 0;
    uint16_t* ip_words = (uint16_t*)ip;
    for(int i = 0; i < 10; i++) {
        sum += ip_words[i];
    }
    while(sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    ip->checksum = ~sum & 0xFFFF;
    udp->checksum = udp_checksum(netif->ip, dst, (uint8_t*)udp, sizeof(udp_hdr_t) + len);
    
    return netif->output(netif, packet, sizeof(packet));
}

void udp_handle(netif_t* netif, uint8_t* packet, uint32_t len) {
    if(len < sizeof(udp_hdr_t)) return;
    
    udp_hdr_t* udp = (udp_hdr_t*)packet;
    ip_hdr_t* ip = (ip_hdr_t*)(packet - sizeof(ip_hdr_t));
    
    uint16_t dst_port = (udp->dst_port >> 8) | (udp->dst_port << 8);
    
    for(int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if(udp_sockets[i].used && udp_sockets[i].port == dst_port) {
            uint8_t* data = packet + sizeof(udp_hdr_t);
            uint32_t data_len = len - sizeof(udp_hdr_t);
            udp_sockets[i].callback(data, data_len, ip->src, 
                                   (udp->src_port >> 8) | (udp->src_port << 8));
            return;
        }
    }
}