#include <esocks/abs_layer.h>
#include <ebinds/tcp_binds.h>

int main(void){
    esock_ev_system evsys;
    if (0 > esock_ev_sysinit(&evsys)){
        perror("esock_ev_sysinit");
        return -1;
    }

    esock_abs abs;
    esock_abs_init(&abs, &evsys, ESOCK_SERVER);

    esock_abs_create(&abs, tcp_create, eaddr_make4(
        eipv4("127.0.0.1", 9000)
    ));
    esock_abs_sbinds(
        &abs, 
        tcp_bind, tcp_newcli, 
        tcp_srecv, tcp_ssend
    );
    
    esock_abs_bind(&abs);
    esock_abs_run(&abs);

    while (atomic_load(&abs.is_running)){
        int r = esock_wait_events(&evsys, -1);
        if (r < 0) break;

        for (size_t n = 0; n < evsys.events_n; n++){
            esock_ev ev = evsys.events[n];
            esock_abs_pdest *dest = ev.data_ptr;
            eaddr_t addr = eaddr_nfd2str(dest->nfd);
            
            switch (ev.type) {
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
        esock_ev_clear(&evsys);
    }

    esock_abs_end(&abs);
    esock_ev_sysend(&evsys);
}