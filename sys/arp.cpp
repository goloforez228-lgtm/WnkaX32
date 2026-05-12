#include "video.h"
#include "net.h"
#include "e1000.h"

#define ARP_CACHE_SIZE 16

typedef struct {
    ip_addr_t ip;
    mac_addr_t mac;
    uint32_t timeout;
    int used;
} arp_entry_t;

static arp_entry_t arp_cache[ARP_CACHE_SIZE];

void arp_init(void) {
    for(int i = 0; i < ARP_CACHE_SIZE; i++) {
        arp_cache[i].used = 0;
    }
    
    mac_addr_t gateway_mac;
    gateway_mac.addr[0] = 0x52;
    gateway_mac.addr[1] = 0x55;
    gateway_mac.addr[2] = 0x0A;
    gateway_mac.addr[3] = 0x00;
    gateway_mac.addr[4] = 0x02;
    gateway_mac.addr[5] = 0x02;
    
    ip_addr_t gateway_ip;
    gateway_ip.addr[0] = 10;
    gateway_ip.addr[1] = 0;
    gateway_ip.addr[2] = 2;
    gateway_ip.addr[3] = 1;
    
    arp_add(gateway_ip, gateway_mac);
}

int arp_lookup(ip_addr_t ip, mac_addr_t* mac) {
    for(int i = 0; i < ARP_CACHE_SIZE; i++) {
        if(arp_cache[i].used) {
            if(arp_cache[i].ip.addr[0] == ip.addr[0] &&
               arp_cache[i].ip.addr[1] == ip.addr[1] &&
               arp_cache[i].ip.addr[2] == ip.addr[2] &&
               arp_cache[i].ip.addr[3] == ip.addr[3]) {
                for(int j = 0; j < 6; j++) {
                    mac->addr[j] = arp_cache[i].mac.addr[j];
                }
                return 1;
            }
        }
    }
    return 0;
}

void arp_add(ip_addr_t ip, mac_addr_t mac) {
    int free_slot = -1;
    for(int i = 0; i < ARP_CACHE_SIZE; i++) {
        if(!arp_cache[i].used) {
            free_slot = i;
            break;
        }
    }
    
    if(free_slot == -1) {
        free_slot = 0;
    }
    
    arp_cache[free_slot].ip = ip;
    arp_cache[free_slot].mac = mac;
    arp_cache[free_slot].timeout = 300;
    arp_cache[free_slot].used = 1;
}

void arp_request(netif_t* netif, ip_addr_t target_ip) {
    uint8_t packet[sizeof(eth_hdr_t) + sizeof(arp_hdr_t)];
    
    for(int i = 0; i < sizeof(packet); i++) {
        packet[i] = 0;
    }
    
    eth_hdr_t* eth = (eth_hdr_t*)packet;
    arp_hdr_t* arp = (arp_hdr_t*)(packet + sizeof(eth_hdr_t));
    
    for(int i = 0; i < 6; i++) {
        eth->dst.addr[i] = 0xFF;
        eth->src.addr[i] = netif->mac.addr[i];
    }
    eth->type = 0x0608;
    
    arp->hw_type = 0x0100;
    arp->proto_type = 0x0008;
    arp->hw_size = 6;
    arp->proto_size = 4;
    arp->opcode = 0x0100;
    
    for(int i = 0; i < 6; i++) {
        arp->sender_mac.addr[i] = netif->mac.addr[i];
    }
    arp->sender_ip = netif->ip;
    
    for(int i = 0; i < 6; i++) {
        arp->target_mac.addr[i] = 0;
    }
    arp->target_ip = target_ip;
    
    if(netif->output) {
        netif->output(netif, packet, sizeof(packet));
    }
}

void arp_handle(netif_t* netif, uint8_t* packet, uint32_t len) {
    (void)len;
    arp_hdr_t* arp = (arp_hdr_t*)(packet + sizeof(eth_hdr_t));
    
    if(arp->opcode == 0x0100) {
        if(arp->target_ip.addr[0] == netif->ip.addr[0] &&
           arp->target_ip.addr[1] == netif->ip.addr[1] &&
           arp->target_ip.addr[2] == netif->ip.addr[2] &&
           arp->target_ip.addr[3] == netif->ip.addr[3]) {
            
            arp_add(arp->sender_ip, arp->sender_mac);
            
            uint8_t reply[sizeof(eth_hdr_t) + sizeof(arp_hdr_t)];
            
            for(int i = 0; i < sizeof(reply); i++) {
                reply[i] = 0;
            }
            
            eth_hdr_t* eth_reply = (eth_hdr_t*)reply;
            arp_hdr_t* arp_reply = (arp_hdr_t*)(reply + sizeof(eth_hdr_t));
            
            for(int i = 0; i < 6; i++) {
                eth_reply->dst.addr[i] = arp->sender_mac.addr[i];
                eth_reply->src.addr[i] = netif->mac.addr[i];
            }
            eth_reply->type = 0x0608;
            
            arp_reply->hw_type = 0x0100;
            arp_reply->proto_type = 0x0008;
            arp_reply->hw_size = 6;
            arp_reply->proto_size = 4;
            arp_reply->opcode = 0x0200;
            
            for(int i = 0; i < 6; i++) {
                arp_reply->sender_mac.addr[i] = netif->mac.addr[i];
                arp_reply->target_mac.addr[i] = arp->sender_mac.addr[i];
            }
            arp_reply->sender_ip = netif->ip;
            arp_reply->target_ip = arp->sender_ip;
            
            if(netif->output) {
                netif->output(netif, reply, sizeof(reply));
            }
        }
    }
    else if(arp->opcode == 0x0200) {
        arp_add(arp->sender_ip, arp->sender_mac);
    }
}