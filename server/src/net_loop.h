#ifndef NETSKETCH_NET_LOOP_H
#define NETSKETCH_NET_LOOP_H

#include <stdbool.h>
#include "server.h"

typedef struct {
    int  listen_fd;
    int  epoll_fd;
    int  bound_port;
    bool epollout_on[MAX_CLIENTS];
} NetLoop;

int  net_loop_init(NetLoop *nl, Server *s);
int  net_loop_step(NetLoop *nl, Server *s, int timeout_ms);
void net_loop_cleanup(NetLoop *nl, Server *s);
int  net_loop_run(Server *s);

#endif
