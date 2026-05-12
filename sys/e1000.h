#ifndef E1000_H
#define E1000_H

#include "net.h"

int e1000_init(void);
int e1000_send(netif_t* netif, uint8_t* data, uint32_t len);
int e1000_recv(netif_t* netif, uint8_t* buffer, uint32_t max_len);
int e1000_link_up(void);
void e1000_dump_stats(void);
void e1000_dump_regs(void);
void e1000_get_mac(uint8_t* mac);
void e1000_set_ip(ip_addr_t ip);
void e1000_set_gateway(ip_addr_t gw);

#endif