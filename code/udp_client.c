#include <esocks/abs_layer.h>
#include <ebinds/udp_binds.h>

const esock_vtable UDP_OPT = {
    .create    = udp_create,
    .s_bind    = udp_bind,
    .s_newcli  = udp_newcli,
    .s_recv    = udp_srecv,
    .s_send    = udp_ssend,
    .c_connect = udp_connect,
    .c_recv    = udp_crecv,
    .c_send    = udp_csend
};

static void handle_event(esock_abs *abs, esock_ev *ev);

int main(void){
    esock_ev_system evsys;
    esock_ev_sysinit(&evsys);

    esock_abs abs;
    esock_abs_init(&abs, &evsys, ESOCK_CLIENT);
    esock_abs_set_opts(&abs, UDP_OPT, eaddr_make4(eipv4("127.0.0.1", 9000)));
    
    if (0 > esock_abs_connect(&abs)) {
        perror("connect failed");
        goto cleanup;
    }

    esock_abs_run(&abs);

    while (atomic_load(&abs.is_running)){
        esock_wait_events(&evsys, -1);

        for (size_t n = 0; n < evsys.events_n; n++){
            esock_ev ev = evsys.events[n];
            handle_event(&abs, &ev);
        }
        esock_ev_clear(&evsys);
    }

cleanup:
    esock_abs_end(&abs);
    esock_ev_sysend(&evsys);
    return 0;
}

static void handle_event(esock_abs *abs, esock_ev *ev){
    esock_abs_pdest *dest = ev->data_ptr;
    eaddr_t addr = eaddr_nfd2str(dest->nfd);
    
    switch (ev->type) {
        case EV_CONNECT:
            printf("connected to server %s:%u\n", addr.ip.v4.ip, addr.ip.v4.port);
            mfd_write(dest->tx_fd, "hello from client", 17);
            break;

        case EV_POLLIN: {
            uint8_t buf[1024];
            int n = mfd_read(dest->rx_fd, buf, sizeof(buf));
            printf("server says: %.*s\n", n, buf);
            esock_abs_reqdisconn(abs, dest->nfd.rfd);
        } break;

        case EV_DISCONNECT:
            printf("disconnected from server.\n");
            atomic_store(&abs->is_running, false);
            break;

        default: break;
    }
    free(dest);
}