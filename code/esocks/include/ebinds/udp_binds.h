#include <esocks/ip_address.h>
#include <events/event.h>
#include <sys/socket.h>

#ifndef EBINDS_UDP

int udp_create(enet_fd *fd, eaddr_t address){
    if (eaddr_netfd(address, fd) != 0) return -1;

    fd->rfd = socket(
        address.t == EADDR_IPV4 ? AF_INET : AF_INET6,
        SOCK_DGRAM,
        0
    );

    if (fd->rfd < 0) return -1;

    int opt = 1;
    setsockopt(fd->rfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    fcntl(fd->rfd, F_SETFL, O_NONBLOCK);

    return 0;
}

int udp_bind(enet_fd *fd){
    return bind(fd->rfd, (struct sockaddr*)&fd->addr, fd->addr_len);
}

int udp_connect(enet_fd *fd){
    return connect(fd->rfd, (struct sockaddr*)&fd->addr, fd->addr_len);
}

ssize_t udp_srecv(enet_fd server, im_fd *fd){
    r_fd rfd_wrapper = mfd_rfd(server.rfd, true, true);
    t_fd src;  mfd_tfd(&rfd_wrapper, RAW_FD, &src);
    t_fd dest; mfd_tfd(fd, IMM_FD, &dest);

    return mfd_redirect(&dest, &src);
}

ssize_t udp_ssend(enet_fd server, im_fd *fd){
    r_fd rfd_wrapper = mfd_rfd(server.rfd, true, true);
    t_fd dest; mfd_tfd(&rfd_wrapper, RAW_FD, &dest);
    t_fd src;  mfd_tfd(fd, IMM_FD, &src);

    return mfd_redirect(&dest, &src);
}

ssize_t udp_crecv(enet_fd client, im_fd *fd){
    return udp_srecv(client, fd);
}

ssize_t udp_csend(enet_fd client, im_fd *fd){
    return udp_ssend(client, fd);
}

int udp_newcli(enet_fd server, enet_fd *newcli, im_fd *rx_buf){
    struct sockaddr_storage client_addr;
    socklen_t addr_len = sizeof(client_addr);
    uint8_t tmp[4096];

    ssize_t r = recvfrom(server.rfd, tmp, sizeof(tmp), 0, 
                         (struct sockaddr *)&client_addr, &addr_len);
    if (r <= 0) return -1;

    int fd = socket(client_addr.ss_family, SOCK_DGRAM, 0);
    
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    if (bind(fd, (struct sockaddr *)&server.addr, server.addr_len) < 0) {
        perror("udp_newcli: bind failed");
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&client_addr, addr_len) < 0) {
        close(fd);
        return -1;
    }

    newcli->rfd = fd;
    newcli->addr = client_addr;
    newcli->addr_len = addr_len;
    
    mfd_write(rx_buf, tmp, r);
    return 0;
}

#endif
#define EBINDS_UDP