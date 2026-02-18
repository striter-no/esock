#include <esocks/ip_address.h>
#include <events/event.h>

// proto
#ifndef EBINDS_PROTO

int proto_create(enet_fd *fd, eaddr_t address){}
int proto_bind(enet_fd *fd){}
int proto_newcli(enet_fd server, enet_fd *newcli){}
ssize_t proto_srecv(enet_fd server, im_fd *fd){}
ssize_t proto_ssend(enet_fd server, im_fd *fd){}
int proto_connect(enet_fd *fd){}
ssize_t proto_crecv(enet_fd client, im_fd *fd){}
ssize_t proto_csend(enet_fd client, im_fd *fd){}

#endif
#define EBINDS_PROTO