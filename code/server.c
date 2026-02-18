#include <esocks/abs_layer.h>
#include <ebinds/tcp_binds.h>

const esock_vtable TCP_OPTS = {
    .create = tcp_create,
    .s_bind = tcp_bind,
    .s_newcli = tcp_newcli,
    .s_recv = tcp_srecv,
    .s_send = tcp_ssend
};

static void handle_events(esock_abs *abs, esock_ev *ev);
int main(void){
    esock_ev_system evsys;
    if (0 > esock_ev_sysinit(&evsys)){
        perror("esock_ev_sysinit");
        return -1;
    }

    esock_abs abs;
    esock_abs_init(&abs, &evsys, ESOCK_SERVER);
    esock_abs_set_opts(&abs, TCP_OPTS, eaddr_make4(eipv4("127.0.0.1", 9000)));
    
    esock_abs_bind(&abs);
    esock_abs_run(&abs);

    while (atomic_load(&abs.is_running)){
        esock_wait_events(&evsys, -1);
        
        for (size_t n = 0; n < evsys.events_n; n++){
            esock_ev ev = evsys.events[n];
            handle_events(&abs, &ev);
        }
        esock_ev_clear(&evsys);
    }

    esock_abs_end(&abs);
    esock_ev_sysend(&evsys);
}

static void handle_events(esock_abs *abs, esock_ev *ev){
    esock_abs_pdest *dest = ev->data_ptr;
    eaddr_t addr = eaddr_nfd2str(dest->nfd);
    
    switch (ev->type) {
        case EV_CONNECT:{
            printf("connected: %s:%u\n", addr.ip.v4.ip, addr.ip.v4.port);
        } break;
        case EV_DISCONNECT:{
            printf("disconnected: %s:%u\n", addr.ip.v4.ip, addr.ip.v4.port);
        } break;
        case EV_POLLIN:{
            uint8_t buf[1024];
            int n = mfd_read(dest->rx_fd, buf, sizeof(buf));
            printf("Received %d bytes: %.*s\n", n, n, buf);
            mfd_write(dest->tx_fd, "Hello from main!", 16);
        } break;
        default: break;
    }

    free(dest);
}