#include "netsketch.h"

ns_conn *ns_connect(const char *host, int port)
{
    (void)host;
    (void)port;
    return NULL;
}

int ns_send_line(ns_conn *c, const char *line)
{
    (void)c;
    (void)line;
    return -1;
}

int ns_recv_line(ns_conn *c, char *out, size_t cap, int timeout_ms)
{
    (void)c;
    (void)out;
    (void)cap;
    (void)timeout_ms;
    return -1;
}

void ns_close(ns_conn *c)
{
    (void)c;
}
