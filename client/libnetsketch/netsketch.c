#define _POSIX_C_SOURCE 200809L

#include "netsketch.h"
#include "framing.h"

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct ns_conn {
    int     fd;
    LineBuf lb;
    char    rx_buf[1024];
    size_t  rx_pos;
    size_t  rx_len;
};

ns_conn *ns_from_fd(int fd)
{
    if (fd < 0) {
        return NULL;
    }
    ns_conn *c = (ns_conn *)malloc(sizeof(*c));
    if (!c) {
        return NULL; /* GCOVR_EXCL_LINE */
    }
    c->fd = fd;
    linebuf_init(&c->lb);
    c->rx_pos = 0;
    c->rx_len = 0;
    return c;
}

ns_conn *ns_connect(const char *host, int port)
{
    if (!host || host[0] == '\0' || port < 1 || port > 65535) {
        return NULL;
    }

    char port_str[16];
    (void)snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints;
    struct addrinfo *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        return NULL;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res); /* GCOVR_EXCL_LINE */
        return NULL;       /* GCOVR_EXCL_LINE */
    }

    int one = 1;
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        close(fd);
        freeaddrinfo(res);
        return NULL;
    }
    freeaddrinfo(res);

    ns_conn *c = ns_from_fd(fd);
    if (!c) {
        close(fd); /* GCOVR_EXCL_LINE */
    }
    return c;
}

int ns_send_line(ns_conn *c, const char *line)
{
    if (!c || c->fd < 0 || !line) {
        return -1;
    }

    size_t len = strlen(line);
    if (len + 1 > FRAME_MAX_LINE) {
        return -1;
    }

    char buf[FRAME_MAX_LINE];
    if (len > 0) {
        memcpy(buf, line, len);
    }
    buf[len] = '\n';
    size_t total = len + 1;
    size_t sent = 0;

    while (sent < total) {
        ssize_t n = send(c->fd, buf + sent, total - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue; /* GCOVR_EXCL_LINE */
        } else {
            return -1;
        }
    }
    return 0;
}

int ns_recv_line(ns_conn *c, char *out, size_t cap, int timeout_ms)
{
    if (!c || c->fd < 0 || !out || cap == 0) {
        return -1;
    }

    for (;;) {
        while (c->rx_pos < c->rx_len) {
            size_t consumed = 0;
            FrameStatus st = linebuf_feed(&c->lb,
                                          c->rx_buf + c->rx_pos,
                                          c->rx_len - c->rx_pos,
                                          &consumed, out, cap);
            c->rx_pos += consumed;
            if (st == FRAME_OK) {
                return 1;
            }
            if (st == FRAME_TOO_LONG) {
                return -1;
            }
        }

        struct pollfd pfd;
        pfd.fd = c->fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int rc = poll(&pfd, 1, timeout_ms);
        if (rc == 0) {
            return 0;
        }
        if (rc < 0) {
            if (errno == EINTR) {
                continue; /* GCOVR_EXCL_LINE */
            }
            return -1; /* GCOVR_EXCL_LINE */
        }

        ssize_t n = recv(c->fd, c->rx_buf, sizeof(c->rx_buf), 0);
        if (n <= 0) {
            return -1;
        }
        c->rx_pos = 0;
        c->rx_len = (size_t)n;
    }
}

void ns_close(ns_conn *c)
{
    if (!c) {
        return;
    }
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
    free(c);
}
