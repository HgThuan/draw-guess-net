#ifndef NETSKETCH_H
#define NETSKETCH_H

#include <stddef.h>

typedef struct ns_conn ns_conn;

ns_conn *ns_connect(const char *host, int port);
int ns_send_line(ns_conn *c, const char *line);
/* Trả về: 1 nếu có dòng, 0 nếu hết thời gian chờ, < 0 nếu lỗi hoặc đóng. */
int ns_recv_line(ns_conn *c, char *out, size_t cap, int timeout_ms);
void ns_close(ns_conn *c);

#endif
