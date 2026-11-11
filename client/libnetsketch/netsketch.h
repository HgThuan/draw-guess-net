/* client/libnetsketch/netsketch.h — Giao diện thư viện C mỏng cho client */
#ifndef NETSKETCH_H
#define NETSKETCH_H

#include <stddef.h>

typedef struct ns_conn ns_conn;

/* Kết nối tới server qua TCP, trả về NULL nếu lỗi. */
ns_conn *ns_connect(const char *host, int port);

/* Gửi một dòng (tự thêm '\n'), trả về 0 nếu thành công, < 0 nếu lỗi. */
int ns_send_line(ns_conn *c, const char *line);

/* Nhận một dòng (không kèm '\n').
 * Trả về: 1 nếu có dòng, 0 nếu hết thời gian chờ, < 0 nếu lỗi hoặc đóng. */
int ns_recv_line(ns_conn *c, char *out, size_t cap, int timeout_ms);

/* Đóng kết nối và giải phóng tài nguyên. */
void ns_close(ns_conn *c);

#endif /* NETSKETCH_H */
