#ifndef TCP_H
#define TCP_H

#include "net.h"

#define TCP_STATE_CLOSED      0
#define TCP_STATE_LISTEN      1
#define TCP_STATE_SYN_SENT    2
#define TCP_STATE_SYN_RECV    3
#define TCP_STATE_ESTABLISHED 4
#define TCP_STATE_FIN_WAIT1   5
#define TCP_STATE_FIN_WAIT2   6
#define TCP_STATE_CLOSE_WAIT  7
#define TCP_STATE_CLOSING     8
#define TCP_STATE_LAST_ACK    9
#define TCP_STATE_TIME_WAIT   10

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_offset;
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed)) tcp_hdr_t;

typedef struct {
    int used;
    int state;
    uint16_t local_port;
    uint16_t remote_port;
    ip_addr_t remote_ip;
    uint32_t seq_num;
    uint32_t ack_num;
    uint32_t remote_seq;
    uint32_t remote_ack;
    uint8_t* rx_buffer;
    uint32_t rx_size;
    uint32_t rx_pos;
    void (*callback)(int socket, uint8_t* data, uint32_t len);
} tcp_socket_t;

#define TCP_MAX_SOCKETS 16
#define TCP_BUFFER_SIZE 8192

extern tcp_socket_t tcp_sockets[TCP_MAX_SOCKETS];

void tcp_init(void);
int tcp_socket_create(void);
int tcp_connect(int socket, ip_addr_t dest_ip, uint16_t dest_port);
int tcp_send(int socket, uint8_t* data, uint32_t len);
int tcp_recv(int socket, uint8_t* buffer, uint32_t max_len);
void tcp_close(int socket);
void tcp_handle_packet(netif_t* netif, uint8_t* packet, uint32_t len);
uint16_t tcp_checksum(ip_addr_t src, ip_addr_t dst, uint8_t* tcp_hdr, uint16_t tcp_len);
int tcp_bind(int socket, uint16_t port, void (*callback)(int, uint8_t*, uint32_t));
int tcp_listen(int socket);

#endif