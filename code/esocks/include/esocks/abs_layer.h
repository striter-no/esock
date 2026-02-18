#include <esocks/ip_address.h>
#include <events/event.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#ifndef ESOCKS_ABS_LAYER

typedef enum {
    ESOCK_SERVER,
    ESOCK_CLIENT
} esock_abs_type;

typedef struct {
    eaddr_t destination;
    void    *data;
    size_t   data_size;
} esock_pack; // do I actually need this?

typedef struct {
    enet_fd nfd;
    im_fd   *rx_fd; // data from net
    im_fd   *tx_fd; // data to net
} esock_abs_dest;

typedef struct {
    enet_fd nfd;
    im_fd   *rx_fd;
    im_fd   *tx_fd;
} esock_abs_pdest;

typedef struct {
    esock_ev_system *evsys;
    esock_abs_type   type;

    int  (*sock_create)(enet_fd *fd, eaddr_t address);

    int  (*ssock_bind)(enet_fd *fd);
    int  (*ssock_newcli)(enet_fd server, enet_fd *newcli);
    ssize_t (*ssock_recv)(enet_fd server, im_fd *fd);
    ssize_t (*ssock_send)(enet_fd server, im_fd *fd);

    int  (*csock_connect)(enet_fd *fd);
    ssize_t (*csock_recv)(enet_fd client, im_fd *fd); // read from real socket, write to imfd
    ssize_t (*csock_send)(enet_fd client, im_fd *fd); // read() from imfd, write()/sendto() to real socket

    im_fd fd;
    enet_fd nfd;
    pthread_t main_thread;
    atomic_bool is_running;

    esock_abs_dest *dests;
    size_t          dest_n;
    pthread_mutex_t dest_mtx;

    esock_abs_dest *pending_conn;
    size_t pending_conn_n;
    
    int *pending_disc;
    size_t pending_disc_n;

    pthread_mutex_t pending_mtx;
} esock_abs;

typedef struct {
    int     (*create)(enet_fd *fd, eaddr_t address);
    
    int     (*s_bind)(enet_fd *fd);
    int     (*s_newcli)(enet_fd server, enet_fd *newcli);
    ssize_t (*s_recv)(enet_fd server, im_fd *fd);
    ssize_t (*s_send)(enet_fd server, im_fd *fd);

    int     (*c_connect)(enet_fd *fd);
    ssize_t (*c_recv)(enet_fd client, im_fd *fd);
    ssize_t (*c_send)(enet_fd client, im_fd *fd);
} esock_vtable;

void esock_abs_reqconn(esock_abs *abs, esock_abs_dest dest) {
    pthread_mutex_lock(&abs->pending_mtx);
    abs->pending_conn = realloc(abs->pending_conn, sizeof(esock_abs_dest) * (abs->pending_conn_n + 1));
    abs->pending_conn[abs->pending_conn_n++] = dest;
    pthread_mutex_unlock(&abs->pending_mtx);
}

void esock_abs_reqdisconn(esock_abs *abs, int rfd) {
    pthread_mutex_lock(&abs->pending_mtx);
    abs->pending_disc = realloc(abs->pending_disc, sizeof(int) * (abs->pending_disc_n + 1));
    abs->pending_disc[abs->pending_disc_n++] = rfd;
    pthread_mutex_unlock(&abs->pending_mtx);
}

enet_fd *make_enfd_copy(enet_fd fd){
    enet_fd *cp = malloc(sizeof(fd));
    memcpy(cp, &fd, sizeof(fd));
    return cp;
}

esock_abs_pdest *make_dest_copy(esock_abs_dest *dest){
    esock_abs_pdest *cp = malloc(sizeof(*dest));
    memcpy(cp, dest, sizeof(*dest));
    return cp;
}

im_fd *make_dyn_imfd(){
    im_fd *o = malloc(sizeof(im_fd));
    *o = mfd_imfd();
    return o;
}

void *__esock_abs_worker(void *_args) {
    esock_abs *abs = _args;

    struct pollfd *fds = NULL;
    size_t allocated_fds = 0;

    while (atomic_load(&abs->is_running)) {
        pthread_mutex_lock(&abs->dest_mtx);
        
        size_t needed_fds = 1 + (abs->dest_n * 2);
        if (needed_fds > allocated_fds) {
            struct pollfd *tmp = realloc(fds, sizeof(struct pollfd) * needed_fds);
            if (!tmp) {
                pthread_mutex_unlock(&abs->dest_mtx);
                continue; 
            }
            fds = tmp;
            allocated_fds = needed_fds;
        }

        if (abs->type == ESOCK_SERVER) {
            fds[0].fd = abs->nfd.rfd;
            fds[0].events = POLLIN;
        } else {
            fds[0].fd = -1;
            fds[0].events = 0;
        }        
        fds[0].revents = 0;

        for (size_t i = 0; i < abs->dest_n; i++) {
            fds[1 + i*2].fd = abs->dests[i].nfd.rfd;
            fds[1 + i*2].events = POLLIN;
            fds[1 + i*2].revents = 0;

            fds[2 + i*2].fd = abs->dests[i].tx_fd->pollin_ev;
            fds[2 + i*2].events = POLLIN;
            fds[2 + i*2].revents = 0;
        }
        
        pthread_mutex_unlock(&abs->dest_mtx);

        int ret = poll(fds, needed_fds, 100);
        if (ret <= 0) goto pending;

        pthread_mutex_lock(&abs->dest_mtx);
        
        if (fds[0].revents & POLLIN) {
            if (abs->type == ESOCK_SERVER) {
                enet_fd new_cli;
                if (abs->ssock_newcli && abs->ssock_newcli(abs->nfd, &new_cli) == 0) {
                    esock_abs_reqconn(
                        abs, 
                        (esock_abs_dest){ 
                            .nfd = new_cli, 
                            .tx_fd = make_dyn_imfd(), 
                            .rx_fd = make_dyn_imfd() 
                        }
                    );
                }
            } else {
                if (abs->csock_send) 
                    abs->csock_send(abs->nfd, &abs->fd);
            }
        }

        for (size_t i = 0; i < abs->dest_n; i++) {
            if (fds[1 + i*2].revents & (POLLIN | POLLHUP | POLLERR)) {
                enet_fd d_nfd = abs->dests[i].nfd;
                ssize_t r = (abs->type == ESOCK_SERVER) ? 
                            abs->ssock_recv(d_nfd, abs->dests[i].rx_fd) : 
                            abs->csock_recv(d_nfd, abs->dests[i].rx_fd);
                
                if (r <= 0 || (fds[1 + i*2].revents & (POLLHUP | POLLERR))) {
                    esock_abs_reqdisconn(abs, abs->dests[i].nfd.rfd);
                    continue; // pending block will delete dead fds
                }

                if (r > 0) {
                    r_fd tmp_rfd = {abs->dests[i].nfd.rfd, true, true};
                    t_fd tfd; mfd_tfd(&tmp_rfd, RAW_FD, &tfd);
                    
                    esock_ev ev = { 
                        .type = EV_POLLIN, 
                        .fd = tfd, 
                        .data_ptr = make_dest_copy(&abs->dests[i]) 
                    };
                    esock_ev_push(abs->evsys, ev);
                }
            }

            if (fds[2 + i*2].revents & POLLIN) {
                enet_fd d_nfd = abs->dests[i].nfd;
                ssize_t sent = 0;
                
                if (abs->type == ESOCK_SERVER) {
                    sent = abs->ssock_send(d_nfd, abs->dests[i].tx_fd);
                } else {
                    sent = abs->csock_send(d_nfd, abs->dests[i].tx_fd);
                }

                if (sent > 0 && abs->dests[i].tx_fd->used == 0) {
                    r_fd tmp_rfd = {abs->dests[i].nfd.rfd, true, true};
                    t_fd tfd; mfd_tfd(&tmp_rfd, RAW_FD, &tfd);

                    esock_ev ev = { 
                        .type = EV_POLLOUT, 
                        .fd = tfd, 
                        .data_ptr = make_dest_copy(&abs->dests[i]) 
                    };
                    esock_ev_push(abs->evsys, ev);
                }
            }
        }

        pthread_mutex_unlock(&abs->dest_mtx);
pending:
        pthread_mutex_lock(&abs->pending_mtx);
        pthread_mutex_lock(&abs->dest_mtx);

        // only 1 place to delete resources
        for (size_t i = 0; i < abs->pending_disc_n; i++) {
            int target_rfd = abs->pending_disc[i];
            for (size_t j = 0; j < abs->dest_n; j++) {
                if (abs->dests[j].nfd.rfd == target_rfd) {
                    r_fd tmp_rfd = {abs->dests[j].nfd.rfd, false, false};
                    t_fd tfd; mfd_tfd(&tmp_rfd, RAW_FD, &tfd);
                    
                    esock_ev ev = { .type = EV_DISCONNECT, .fd = tfd, .data_ptr = make_dest_copy(
                        &abs->dests[j]
                    ) };
                    esock_ev_push(abs->evsys, ev);

                    close(abs->dests[j].tx_fd->pollin_ev);
                    close(abs->dests[j].tx_fd->pollout_ev);
                    close(abs->dests[j].rx_fd->pollin_ev);
                    close(abs->dests[j].rx_fd->pollout_ev);
                    close(abs->dests[j].nfd.rfd);
                    free(abs->dests[j].tx_fd);
                    free(abs->dests[j].rx_fd);
                    
                    abs->dests[j] = abs->dests[abs->dest_n - 1];
                    abs->dest_n--;
                    break;
                }
            }
        }
        abs->pending_disc_n = 0;

        for (size_t i = 0; i < abs->pending_conn_n; i++) {
            abs->dests = realloc(abs->dests, sizeof(esock_abs_dest) * (abs->dest_n + 1));
            abs->dests[abs->dest_n] = abs->pending_conn[i];
            
            r_fd rfd = {abs->dests[abs->dest_n].nfd.rfd, false, false};
            t_fd tfd; mfd_tfd(&rfd, RAW_FD, &tfd);
            esock_ev ev = { .type = EV_CONNECT, .fd = tfd, .data_ptr = make_dest_copy(
                &abs->dests[abs->dest_n]
            ) };
            esock_ev_push(abs->evsys, ev);

            esock_ev ev_out = { .type = EV_POLLOUT, .fd = tfd, .data_ptr = make_dest_copy(&abs->dests[abs->dest_n]) };
            esock_ev_push(abs->evsys, ev_out);
            
            abs->dest_n++;
        }
        abs->pending_conn_n = 0;

        pthread_mutex_unlock(&abs->dest_mtx);
        pthread_mutex_unlock(&abs->pending_mtx);
    }

    free(fds);
    return NULL;
}

int esock_abs_init(
    esock_abs *abs, esock_ev_system *evsys, esock_abs_type type
){
    if (!abs || !evsys) return -1;
    abs->evsys = evsys;
    abs->type = type;
    abs->fd = mfd_imfd();
    atomic_store(&abs->is_running, false);
    pthread_mutexattr_t atrs;
    pthread_mutexattr_init(&atrs);
    pthread_mutexattr_settype(&atrs, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&abs->dest_mtx, &atrs);
    pthread_mutexattr_destroy(&atrs);

    pthread_mutex_init(&abs->pending_mtx, NULL);

    abs->dest_n = 0;
    abs->dests = NULL;

    abs->pending_conn = NULL;
    abs->pending_disc = NULL;
    abs->pending_conn_n = 0;
    abs->pending_disc_n = 0;
    return 0;
}

int esock_abs_bind(
    esock_abs *abs
){
    if (!abs || !abs->ssock_bind) return -1;
    return abs->ssock_bind(&abs->nfd);
}

int esock_abs_connect(esock_abs *abs) {
    if (!abs || !abs->csock_connect) return -1;

    if (abs->csock_connect(&abs->nfd) != 0) {
        return -1; 
    }

    esock_abs_dest client_dest;
    client_dest.nfd = abs->nfd;
    
    client_dest.rx_fd = malloc(sizeof(im_fd));
    client_dest.tx_fd = malloc(sizeof(im_fd));
    *client_dest.rx_fd = mfd_imfd();
    *client_dest.tx_fd = mfd_imfd();

    esock_abs_reqconn(abs, client_dest);
    return 0;
}

void esock_abs_set_opts(
    esock_abs    *abs,
    esock_vtable  methods,
    eaddr_t       addr
){
    if (!abs) return;
    abs->csock_connect = methods.c_connect;
    abs->csock_recv    = methods.c_recv;
    abs->csock_send = methods.c_send;
    abs->ssock_bind = methods.s_bind;
    abs->ssock_send = methods.s_send;
    abs->ssock_recv = methods.s_recv;
    abs->ssock_newcli = methods.s_newcli;

    methods.create(&abs->nfd, addr);
}

int esock_abs_run(
    esock_abs *abs
){

    atomic_store(&abs->is_running, true);
    if (0 > pthread_create(
        &abs->main_thread, NULL, 
        &__esock_abs_worker, 
        abs
    )){
        return -1;
    }
    return 0;
}

int esock_abs_end(
    esock_abs *abs
){
    atomic_store(&abs->is_running, false);
    pthread_join(abs->main_thread, NULL);

    pthread_mutex_lock(&abs->pending_mtx);
    pthread_mutex_lock(&abs->dest_mtx);

    for (size_t j = 0; j < abs->dest_n; j++) {
        close(abs->dests[j].rx_fd->pollin_ev);
        close(abs->dests[j].rx_fd->pollout_ev);
        close(abs->dests[j].tx_fd->pollin_ev);
        close(abs->dests[j].tx_fd->pollout_ev);
        close(abs->dests[j].nfd.rfd);
        free(abs->dests[j].tx_fd);
        free(abs->dests[j].rx_fd);
    }
    abs->pending_disc_n = 0;
    abs->pending_conn_n = 0;
    
    free(abs->pending_conn);
    free(abs->pending_disc);
    abs->pending_conn = NULL;
    abs->pending_disc = NULL;

    free(abs->dests);
    abs->dests  = NULL;
    abs->dest_n = 0;

    pthread_mutex_unlock(&abs->dest_mtx);
    pthread_mutex_unlock(&abs->pending_mtx);

    pthread_mutex_destroy(&abs->dest_mtx);
    pthread_mutex_destroy(&abs->pending_mtx);
    return 0;
}

#endif
#define ESOCKS_ABS_LAYER