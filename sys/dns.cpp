#include "net.h"

#define DNS_SERVER_IP 0x08080808

typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed)) dns_hdr_t;

typedef struct {
    uint16_t type;
    uint16_t class;
} __attribute__((packed)) dns_question_t;

typedef struct {
    uint16_t name_ptr;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t rdlength;
    uint32_t rdata;
} __attribute__((packed)) dns_answer_t;

static int dns_socket = -1;
static ip_addr_t dns_server = {8,8,8,8};
static uint16_t dns_id = 0x1234;

void dns_callback(uint8_t* data, uint32_t len, ip_addr_t src, uint16_t src_port) {
    if(len < sizeof(dns_hdr_t)) return;
    
    dns_hdr_t* hdr = (dns_hdr_t*)data;
    
    if(hdr->id != dns_id) return;
    if(hdr->ancount == 0) {
        kprint("[DNS] No answer\n");
        return;
    }
    
    uint8_t* ptr = data + sizeof(dns_hdr_t);
    while(*ptr) {
        ptr += (*ptr) + 1;
    }
    ptr += 5;
    
    dns_answer_t* ans = (dns_answer_t*)ptr;
    
    if(ans->type == 0x0100) {
        ip_addr_t ip;
        ip.addr[0] = ans->rdata & 0xFF;
        ip.addr[1] = (ans->rdata >> 8) & 0xFF;
        ip.addr[2] = (ans->rdata >> 16) & 0xFF;
        ip.addr[3] = (ans->rdata >> 24) & 0xFF;
        
        kprint("[DNS] Resolved: ");
        kprint_int(ip.addr[0]); kprint(".");
        kprint_int(ip.addr[1]); kprint(".");
        kprint_int(ip.addr[2]); kprint(".");
        kprint_int(ip.addr[3]); kprint("\n");
        
    }
}

int dns_init(void) {
    dns_socket = udp_bind(53, dns_callback);
    return dns_socket >= 0 ? 0 : -1;
}

int dns_resolve(netif_t* netif, const char* hostname) {
    uint8_t packet[512];
    
    dns_hdr_t* hdr = (dns_hdr_t*)packet;
    hdr->id = dns_id++;
    hdr->flags = 0x0100; 
    hdr->qdcount = 0x0100; 
    hdr->ancount = 0;
    hdr->nscount = 0;
    hdr->arcount = 0;
    
    uint8_t* name = (uint8_t*)(packet + sizeof(dns_hdr_t));
    uint8_t* name_ptr = name;
    
    const char* part = hostname;
    while(*part) {
        const char* dot = part;
        while(*dot && *dot != '.') dot++;
        
        uint8_t len = dot - part;
        *name_ptr++ = len;
        
        for(int i = 0; i < len; i++) {
            *name_ptr++ = part[i];
        }
        
        part = (*dot) ? dot + 1 : dot;
    }
    *name_ptr++ = 0;
    dns_question_t* q = (dns_question_t*)name_ptr;
    q->type = 0x0100;  
    q->class = 0x0100; 
    
    uint32_t total_len = sizeof(dns_hdr_t) + (name_ptr - name) + sizeof(dns_question_t);
    
    return udp_send(netif, dns_server, 53, 53, packet, total_len);
}