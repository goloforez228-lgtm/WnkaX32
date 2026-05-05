#ifndef ARP_H
#define ARP_H

#include "net.h"

void arp_init(void);
int arp_lookup(ip_addr_t ip, mac_addr_t* mac);
void arp_add(ip_addr_t ip, mac_addr_t mac);
void arp_request(netif_t* netif, ip_addr_t target_ip);
void arp_handle(netif_t* netif, uint8_t* packet, uint32_t len);

#endif