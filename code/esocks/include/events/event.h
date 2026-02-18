#ifndef ESOCKS_EV_HEAD_STEP
#define ESOCKS_EV_HEAD_STEP 10
#endif

#include <mfd/streams.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <sys/poll.h>
#include <pthread.h>

#ifndef ESOCKS_EVENTS

typedef enum {
    EV_CONNECT,
    EV_DISCONNECT,
    EV_CONNECTED,
    EV_ERROR,
    EV_POLLIN,
    EV_POLLOUT
} esock_evtype;

typedef struct {
    esock_evtype type;
    t_fd         fd;
    void         *data_ptr;
} esock_ev;

typedef struct {
    esock_ev *events;
    size_t    events_n, events_head;

    int       new_event_evfd;
    pthread_mutex_t mtx;
} esock_ev_system;

int esock_ev_sysinit(
    esock_ev_system *sys
){
    sys->events = NULL;
    sys->events_n = 0;
    sys->events_head = 0;
    sys->new_event_evfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (sys->new_event_evfd < 0) 
        return -1;
    pthread_mutex_init(&sys->mtx, NULL);
    return 0;
}

int esock_ev_push(
    esock_ev_system *sys,
    esock_ev         event
){
    pthread_mutex_lock(&sys->mtx);
    if (sys->events_head <= sys->events_n){
        sys->events = realloc(
            sys->events, 
            sizeof(event) * (sys->events_head + ESOCKS_EV_HEAD_STEP)
        );
        
        if (!sys->events){ 
            pthread_mutex_unlock(&sys->mtx);
            return -1;
        }

        sys->events_head += ESOCKS_EV_HEAD_STEP;
    }

    sys->events[sys->events_n] = event;
    sys->events_n++;

    uint64_t u = 1;
    write(sys->new_event_evfd, &u, sizeof(u));

    pthread_mutex_unlock(&sys->mtx);
    return 0;
}

int esock_wait_events(
    esock_ev_system *sys,
    int              timeout
){
    struct pollfd fds[1] = {
        {sys->new_event_evfd, POLLIN}
    };

    int ret = poll(fds, 1, timeout);
    if (ret < 0) return -1;
    if (ret == 0) return 1;
    if (!(fds[0].revents & POLLIN)) return -1;

    uint64_t u;
    read(sys->new_event_evfd, &u, sizeof(u));

    return 0;
}

void esock_ev_clear(
    esock_ev_system *sys
){
    pthread_mutex_lock(&sys->mtx);
    free(sys->events);
    sys->events = NULL;
    sys->events_n = 0;
    sys->events_head = 0;
    pthread_mutex_unlock(&sys->mtx);
}

void esock_ev_sysend(
    esock_ev_system *sys
){
    pthread_mutex_lock(&sys->mtx);
    close(sys->new_event_evfd);
    
    free(sys->events);
    sys->events = NULL;
    sys->events_n = 0;
    sys->events_head = 0;
    pthread_mutex_unlock(&sys->mtx);
    pthread_mutex_destroy(&sys->mtx);
}

#endif
#define ESOCKS_EVENTS