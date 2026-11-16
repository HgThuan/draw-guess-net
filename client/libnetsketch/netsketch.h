#ifndef NETSKETCH_H
#define NETSKETCH_H

#include <stddef.h>

typedef struct ns_conn ns_conn;

ns_conn *ns_connect(const char *host, int port);
ns_conn *ns_from_fd(int fd);
int ns_send_line(ns_conn *c, const char *line);
int ns_recv_line(ns_conn *c, char *out, size_t cap, int timeout_ms);
void ns_close(ns_conn *c);

#endif
