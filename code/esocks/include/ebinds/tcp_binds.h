#include <esocks/ip_address.h>
#include <events/event.h>

#ifndef ESOCKS_TCP_CLIENT_FLAGS
#define ESOCKS_TCP_CLIENT_FLAGS (O_CLOEXEC | O_NONBLOCK)
#endif

#ifndef ESOCKS_TCP_MAX_CLIENTS
#define ESOCKS_TCP_MAX_CLIENTS 100
#endif

#ifndef EBINDS_TCP

int tcp_create(enet_fd *fd, eaddr_t address){
    if (eaddr_netfd(address, fd) != 0) 
        return -1;

    fd->rfd = socket(
        address.t == EADDR_IPV4 ? AF_INET: AF_INET6,
        SOCK_STREAM, 
        0
    );

    int opt = 1;
    setsockopt(fd->rfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (fd->rfd < 0) return -1;
    
    return 0;
}

int tcp_bind(enet_fd *fd){
    if (0 > bind(
        fd->rfd, 
        (struct sockaddr*)&fd->addr, 
        fd->addr_len
    )) return -1;

    return listen(fd->rfd, ESOCKS_TCP_MAX_CLIENTS);
}

int tcp_newcli(enet_fd server, enet_fd *newcli, im_fd *rx_buf){
    (void)rx_buf;
    
    struct sockaddr_storage client_addr;
    socklen_t addr_len = sizeof(client_addr);

    int fd = accept4(
        server.rfd, 
        (struct sockaddr *)&client_addr, 
        &addr_len, 
        ESOCKS_TCP_CLIENT_FLAGS
    );

    if (fd < 0) {
        perror("tcp_newcli:accept4()");
        return -1;
    }

    newcli->rfd = fd;
    memcpy(&newcli->addr, &client_addr, addr_len);
    newcli->addr_len = addr_len;

    return 0;
}

ssize_t tcp_srecv(enet_fd server, im_fd *fd){
    r_fd rfd_wrapper = mfd_rfd(server.rfd, true, true);
    t_fd src;  mfd_tfd(&rfd_wrapper, RAW_FD, &src);
    t_fd dest; mfd_tfd(fd, IMM_FD, &dest);

    return mfd_redirect(&dest, &src);
}

ssize_t tcp_ssend(enet_fd server, im_fd *fd){
    r_fd rfd_wrapper = mfd_rfd(server.rfd, true, true);
    t_fd dest; mfd_tfd(&rfd_wrapper, RAW_FD, &dest);
    t_fd src;  mfd_tfd(fd, IMM_FD, &src);

    return mfd_redirect(&dest, &src);
}

int tcp_connect(enet_fd *fd){
    return connect(fd->rfd, (struct sockaddr*)&fd->addr, fd->addr_len);
}

ssize_t tcp_crecv(enet_fd client, im_fd *fd){
    return tcp_srecv(client, fd);
}

ssize_t tcp_csend(enet_fd client, im_fd *fd){
    return tcp_ssend(client, fd);
}

#endif
#define EBINDS_TCP