#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <arpa/inet.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#ifndef ESOCKS_IP_ADDR

typedef struct {
    char     ip[INET_ADDRSTRLEN];
    uint16_t port;
} eaddr_ipv4;

typedef struct {
    char     ip[INET6_ADDRSTRLEN];
    uint16_t port;
} eaddr_ipv6;

typedef enum {
    EADDR_IPV4,
    EADDR_IPV6
} eaddr_type;

typedef struct {
    union {
        eaddr_ipv4 v4;
        eaddr_ipv6 v6;
    } ip;
    eaddr_type t;
} eaddr_t;

typedef struct {
    int rfd;
    struct sockaddr_storage addr;
    socklen_t               addr_len;
} enet_fd;

// --- methods

eaddr_ipv4 eipv4(const char *ip, uint16_t port){
    eaddr_ipv4 addr;
    strcpy(addr.ip, ip);
    addr.port = port;
    return addr;
}

eaddr_ipv6 eipv6(const char *ip, uint16_t port){
    eaddr_ipv6 addr;
    strcpy(addr.ip, ip);
    addr.port = port;
    return addr;
}

eaddr_t eaddr_make4(eaddr_ipv4 ipv4){
    return (eaddr_t){
        .ip.v4 = ipv4,
        .t = EADDR_IPV4
    };
}

eaddr_t eaddr_make6(eaddr_ipv6 ipv6){
    return (eaddr_t){
        .ip.v6 = ipv6,
        .t = EADDR_IPV6
    };
}

// NOTICE: port IS NOT set (== 0) after operation
int eaddr_resolve_domain(const char *domain, eaddr_t *output){
    struct hostent *he;
    struct in_addr **addr_list;
    int i;
         
    if ( (he = gethostbyname( domain ) ) == NULL) {
        herror("gethostbyname");
        return 1;
    }
 
    addr_list = (struct in_addr **) he->h_addr_list;
     
    char ip[INET6_ADDRSTRLEN] = {0};
    for(i = 0; addr_list[i] != NULL; i++){
        strcpy(ip , inet_ntoa(*addr_list[i]) );
        break;
    }
    
    if (ip[0] == '\0') return -1;

    // is IPv6?
    if (strchr(ip, ':') != NULL){
        output->t = EADDR_IPV6;
        strncpy(output->ip.v6.ip, ip, INET6_ADDRSTRLEN);
        output->ip.v6.port = 0;
        return 0;
    }

    output->t = EADDR_IPV4;
    strncpy(output->ip.v4.ip, ip, INET_ADDRSTRLEN);
    output->ip.v4.port = 0;

    return 0;
}

// NOTICE: fd IS NOT set (== -1), only address
int eaddr_netfd(eaddr_t addr, enet_fd *out){
    memset(out, 0, sizeof(*out));
    
    out->rfd = -1;
    switch (addr.t){
        case EADDR_IPV4:{
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)&out->addr;
            if (inet_pton(AF_INET, addr.ip.v4.ip, &(ipv4->sin_addr)) == 1) {
                ipv4->sin_family = AF_INET;
                ipv4->sin_port = htons(addr.ip.v4.port);
                out->addr_len = sizeof(struct sockaddr_in);
                return 0;
            }
            return -1;
        } break;
        
        case EADDR_IPV6:{
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)&out->addr;
            if (inet_pton(AF_INET6, addr.ip.v6.ip, &(ipv6->sin6_addr)) == 1) {
                ipv6->sin6_family = AF_INET6;
                ipv6->sin6_port = htons(addr.ip.v6.port);
                out->addr_len = sizeof(struct sockaddr_in6);
                return 0;
            }
            return -1;
        } break;

        default: {
            return -1;
        }
    }
    
    return 0;
}

eaddr_t eaddr_nfd2str(enet_fd fd){
    static char str[INET6_ADDRSTRLEN];
    
    struct sockaddr_storage *addr = &fd.addr;

    if (addr->ss_family == AF_INET) {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)addr;
        inet_ntop(AF_INET, &(ipv4->sin_addr), str, INET_ADDRSTRLEN);
        return eaddr_make4(eipv4(
            str, ntohs(ipv4->sin_port)
        ));
    } 
    else if (addr->ss_family == AF_INET6) {
        struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)addr;
        inet_ntop(AF_INET6, &(ipv6->sin6_addr), str, INET6_ADDRSTRLEN);
        return eaddr_make6(eipv6(
            str, ntohs(ipv6->sin6_port)
        ));
    } 
    return (eaddr_t){0};
}

#endif
#define ESOCKS_IP_ADDR
