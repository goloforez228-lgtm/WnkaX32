#include "tcp.h"
#include "video.h"
#include "e1000.h"
#include "rtl8139.h"
#include "kernel_stubs.h"

tcp_socket_t tcp_sockets[TCP_MAX_SOCKETS];
static uint16_t next_port = 1024;

uint16_t tcp_checksum(ip_addr_t src, ip_addr_t dst, uint8_t* tcp_hdr, uint16_t tcp_len) {
    uint32_t sum = 0;
    
    sum += (src.addr[0] << 8) | src.addr[1];
    sum += (src.addr[2] << 8) | src.addr[3];
    sum += (dst.addr[0] << 8) | dst.addr[1];
    sum += (dst.addr[2] << 8) | dst.addr[3];
    sum += 6;
    sum += tcp_len;
    
    uint16_t* words = (uint16_t*)tcp_hdr;
    uint32_t word_len = (tcp_len + 1) / 2;
    for(uint32_t i = 0; i < word_len; i++) {
        sum += words[i];
    }
    
    while(sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum & 0xFFFF;
}

void tcp_init(void) {
    for(int i = 0; i < TCP_MAX_SOCKETS; i++) {
        tcp_sockets[i].used = 0;
        tcp_sockets[i].state = TCP_STATE_CLOSED;
        tcp_sockets[i].rx_buffer = (uint8_t*)0x900000 + i * TCP_BUFFER_SIZE;
        tcp_sockets[i].rx_size = TCP_BUFFER_SIZE;
        tcp_sockets[i].rx_pos = 0;
    }
}

int tcp_socket_create(void) {
    for(int i = 0; i < TCP_MAX_SOCKETS; i++) {
        if(!tcp_sockets[i].used) {
            tcp_sockets[i].used = 1;
            tcp_sockets[i].state = TCP_STATE_CLOSED;
            tcp_sockets[i].local_port = next_port++;
            tcp_sockets[i].seq_num = 0x12345678;
            tcp_sockets[i].rx_pos = 0;
            return i;
        }
    }
    return -1;
}

int tcp_connect(int socket, ip_addr_t dest_ip, uint16_t dest_port) {
    if(socket < 0 || socket >= TCP_MAX_SOCKETS || !tcp_sockets[socket].used) {
        return -1;
    }
    
    tcp_socket_t* sock = &tcp_sockets[socket];
    sock->remote_ip = dest_ip;
    sock->remote_port = dest_port;
    sock->state = TCP_STATE_SYN_SENT;
    
    mac_addr_t dest_mac;
    if(!arp_lookup(dest_ip, &dest_mac)) {
        arp_request(&e1000_netif, dest_ip);
        for(int retry = 0; retry < 30; retry++) {
            for(volatile int d = 0; d < 100000; d++);
            if(arp_lookup(dest_ip, &dest_mac)) break;
        }
        if(!arp_lookup(dest_ip, &dest_mac)) {
            return -1;
        }
    }
    
    uint8_t packet[sizeof(eth_hdr_t) + sizeof(ip_hdr_t) + sizeof(tcp_hdr_t)];
    
    for(int i = 0; i < sizeof(packet); i++) packet[i] = 0;
    
    tcp_hdr_t* tcp = (tcp_hdr_t*)(packet + sizeof(eth_hdr_t) + sizeof(ip_hdr_t));
    tcp->src_port = (sock->local_port >> 8) | (sock->local_port << 8);
    tcp->dst_port = (dest_port >> 8) | (dest_port << 8);
    tcp->seq_num = (sock->seq_num >> 24) | ((sock->seq_num >> 8) & 0xFF00) | 
                   ((sock->seq_num << 8) & 0xFF0000) | (sock->seq_num << 24);
    tcp->ack_num = 0;
    tcp->data_offset = 0x50;
    tcp->flags = TCP_FLAG_SYN;
    tcp->window = 0xFFFF;
    tcp->checksum = 0;
    tcp->urgent = 0;
    
    ip_hdr_t* ip = (ip_hdr_t*)(packet + sizeof(eth_hdr_t));
    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = (sizeof(ip_hdr_t) + sizeof(tcp_hdr_t));
    ip->id = 0;
    ip->flags_frag = 0x4000;
    ip->ttl = 64;
    ip->protocol = 6;
    ip->checksum = 0;
    ip->src = e1000_netif.ip;
    ip->dst = dest_ip;
    
    uint16_t* ip_words = (uint16_t*)ip;
    uint32_t ip_sum = 0;
    for(int i = 0; i < 10; i++) {
        ip_sum += ip_words[i];
    }
    while(ip_sum >> 16) ip_sum = (ip_sum & 0xFFFF) + (ip_sum >> 16);
    ip->checksum = ~ip_sum & 0xFFFF;
    
    tcp->checksum = tcp_checksum(e1000_netif.ip, dest_ip, (uint8_t*)tcp, sizeof(tcp_hdr_t));
    
    eth_hdr_t* eth = (eth_hdr_t*)packet;
    for(int i = 0; i < 6; i++) {
        eth->dst.addr[i] = dest_mac.addr[i];
        eth->src.addr[i] = e1000_netif.mac.addr[i];
    }
    eth->type = 0x0008;
    
    e1000_netif.output(&e1000_netif, packet, sizeof(packet));
    
    for(int retry = 0; retry < 100; retry++) {
        for(volatile int d = 0; d < 100000; d++);
        if(sock->state == TCP_STATE_ESTABLISHED) return 0;
    }
    
    return -1;
}

int tcp_send(int socket, uint8_t* data, uint32_t len) {
    if(socket < 0 || socket >= TCP_MAX_SOCKETS || !tcp_sockets[socket].used) {
        return -1;
    }
    
    tcp_socket_t* sock = &tcp_sockets[socket];
    if(sock->state != TCP_STATE_ESTABLISHED) return -1;
    
    mac_addr_t dest_mac;
    if(!arp_lookup(sock->remote_ip, &dest_mac)) return -1;
    
    uint32_t total_len = sizeof(eth_hdr_t) + sizeof(ip_hdr_t) + sizeof(tcp_hdr_t) + len;
    uint8_t* packet = (uint8_t*)0xA00000;
    
    for(uint32_t i = 0; i < total_len; i++) packet[i] = 0;
    
    tcp_hdr_t* tcp = (tcp_hdr_t*)(packet + sizeof(eth_hdr_t) + sizeof(ip_hdr_t));
    tcp->src_port = (sock->local_port >> 8) | (sock->local_port << 8);
    tcp->dst_port = (sock->remote_port >> 8) | (sock->remote_port << 8);
    tcp->seq_num = (sock->seq_num >> 24) | ((sock->seq_num >> 8) & 0xFF00) | 
                   ((sock->seq_num << 8) & 0xFF0000) | (sock->seq_num << 24);
    tcp->ack_num = (sock->remote_seq >> 24) | ((sock->remote_seq >> 8) & 0xFF00) | 
                   ((sock->remote_seq << 8) & 0xFF0000) | (sock->remote_seq << 24);
    tcp->data_offset = 0x50;
    tcp->flags = TCP_FLAG_PSH | TCP_FLAG_ACK;
    tcp->window = 0xFFFF;
    tcp->checksum = 0;
    tcp->urgent = 0;
    
    uint8_t* tcp_data = packet + sizeof(eth_hdr_t) + sizeof(ip_hdr_t) + sizeof(tcp_hdr_t);
    for(uint32_t i = 0; i < len; i++) {
        tcp_data[i] = data[i];
    }
    
    ip_hdr_t* ip = (ip_hdr_t*)(packet + sizeof(eth_hdr_t));
    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = (sizeof(ip_hdr_t) + sizeof(tcp_hdr_t) + len);
    ip->id = 0;
    ip->flags_frag = 0x4000;
    ip->ttl = 64;
    ip->protocol = 6;
    ip->checksum = 0;
    ip->src = e1000_netif.ip;
    ip->dst = sock->remote_ip;
    
    uint16_t* ip_words = (uint16_t*)ip;
    uint32_t ip_sum = 0;
    for(int i = 0; i < 10; i++) ip_sum += ip_words[i];
    while(ip_sum >> 16) ip_sum = (ip_sum & 0xFFFF) + (ip_sum >> 16);
    ip->checksum = ~ip_sum & 0xFFFF;
    
    tcp->checksum = tcp_checksum(e1000_netif.ip, sock->remote_ip, (uint8_t*)tcp, sizeof(tcp_hdr_t) + len);
    
    eth_hdr_t* eth = (eth_hdr_t*)packet;
    for(int i = 0; i < 6; i++) {
        eth->dst.addr[i] = dest_mac.addr[i];
        eth->src.addr[i] = e1000_netif.mac.addr[i];
    }
    eth->type = 0x0008;
    
    sock->seq_num += len;
    
    return e1000_netif.output(&e1000_netif, packet, total_len);
}

int tcp_recv(int socket, uint8_t* buffer, uint32_t max_len) {
    if(socket < 0 || socket >= TCP_MAX_SOCKETS || !tcp_sockets[socket].used) {
        return -1;
    }
    
    tcp_socket_t* sock = &tcp_sockets[socket];
    if(sock->rx_pos == 0) return 0;
    
    uint32_t to_copy = sock->rx_pos;
    if(to_copy > max_len) to_copy = max_len;
    
    for(uint32_t i = 0; i < to_copy; i++) {
        buffer[i] = sock->rx_buffer[i];
    }
    
    uint32_t remaining = sock->rx_pos - to_copy;
    for(uint32_t i = 0; i < remaining; i++) {
        sock->rx_buffer[i] = sock->rx_buffer[to_copy + i];
    }
    sock->rx_pos = remaining;
    
    return to_copy;
}

void tcp_close(int socket) {
    if(socket >= 0 && socket < TCP_MAX_SOCKETS) {
        tcp_sockets[socket].used = 0;
        tcp_sockets[socket].state = TCP_STATE_CLOSED;
    }
}

void tcp_handle_packet(netif_t* netif, uint8_t* packet, uint32_t len) {
    if(len < sizeof(ip_hdr_t) + sizeof(tcp_hdr_t)) return;
    
    ip_hdr_t* ip = (ip_hdr_t*)packet;
    tcp_hdr_t* tcp = (tcp_hdr_t*)(packet + sizeof(ip_hdr_t));
    
    uint16_t src_port = (tcp->src_port >> 8) | (tcp->src_port << 8);
    uint16_t dst_port = (tcp->dst_port >> 8) | (tcp->dst_port << 8);
    
    uint32_t seq = (tcp->seq_num >> 24) | ((tcp->seq_num >> 8) & 0xFF00) | 
                   ((tcp->seq_num << 8) & 0xFF0000) | (tcp->seq_num << 24);
    uint32_t ack = (tcp->ack_num >> 24) | ((tcp->ack_num >> 8) & 0xFF00) | 
                   ((tcp->ack_num << 8) & 0xFF0000) | (tcp->ack_num << 24);
    
    for(int i = 0; i < TCP_MAX_SOCKETS; i++) {
        tcp_socket_t* sock = &tcp_sockets[i];
        if(!sock->used) continue;
        if(sock->local_port != dst_port) continue;
        
        if(sock->state == TCP_STATE_SYN_SENT && (tcp->flags & TCP_FLAG_SYN) && (tcp->flags & TCP_FLAG_ACK)) {
            sock->remote_seq = seq + 1;
            sock->ack_num = ack;
            sock->state = TCP_STATE_ESTABLISHED;
            
            tcp_send(i, (uint8_t*)"", 0);
        }
        else if(sock->state == TCP_STATE_ESTABLISHED && (tcp->flags & TCP_FLAG_ACK)) {
            uint32_t data_len = len - sizeof(ip_hdr_t) - sizeof(tcp_hdr_t);
            if(data_len > 0) {
                uint8_t* tcp_data = packet + sizeof(ip_hdr_t) + sizeof(tcp_hdr_t);
                for(uint32_t j = 0; j < data_len && sock->rx_pos < sock->rx_size; j++) {
                    sock->rx_buffer[sock->rx_pos++] = tcp_data[j];
                }
                sock->remote_seq += data_len;
                
                if(sock->callback) {
                    sock->callback(i, tcp_data, data_len);
                }
            }
        }
        break;
    }
}