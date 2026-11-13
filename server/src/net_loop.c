#define _POSIX_C_SOURCE 200809L

#include "net_loop.h"
#include "protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define EPOLL_TAG_LISTEN UINT64_MAX
#define EPOLL_MAX_EVENTS 64
#define RECV_CHUNK_SIZE  4096

static volatile sig_atomic_t g_stop_requested = 0;

static void on_stop_signal(int sig)
{
    (void)sig;
    g_stop_requested = 1;
}

static uint64_t pack_conn_id(ConnId id)
{
    return ((uint64_t)id.gen << 16) | (uint64_t)id.idx;
}

static ConnId unpack_conn_id(uint64_t val)
{
    ConnId id;
    id.idx = (uint16_t)(val & 0xFFFFu);
    id.gen = (uint16_t)((val >> 16) & 0xFFFFu);
    return id;
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1; /* GCOVR_EXCL_LINE */
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void flush_conn_out(NetLoop *nl, Conn *c)
{
    while (c->out_len > 0) {
        ssize_t n = send(c->fd, c->out, c->out_len, MSG_NOSIGNAL);
        if (n > 0) {
            conn_consume_out(c, (size_t)n);
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            break;
        } else {
            c->closing = true;
            return;
        }
    }

    bool want_out = (c->out_len > 0 && !c->closing);
    if (want_out != nl->epollout_on[c->idx]) {
        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN | (want_out ? EPOLLOUT : 0u);
        ev.data.u64 = pack_conn_id(conn_id(c));
        if (epoll_ctl(nl->epoll_fd, EPOLL_CTL_MOD, c->fd, &ev) == 0) {
            nl->epollout_on[c->idx] = want_out;
        }
    }
}

static void handle_accept(NetLoop *nl, Server *s, int64_t now_ms)
{
    for (;;) {
        int cfd = accept(nl->listen_fd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR) {
                continue; /* GCOVR_EXCL_LINE */
            }
            break;
        }

        if (set_nonblocking(cfd) < 0) {
            close(cfd); /* GCOVR_EXCL_LINE */
            continue;   /* GCOVR_EXCL_LINE */
        }

        int one = 1;
        (void)setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        Conn *c = conn_alloc(&s->conns, cfd, now_ms);
        if (!c) {
            static const char full_msg[] = "ERR|901|server full\nKICK|SERVER_FULL\n";
            ssize_t ignored = send(cfd, full_msg, sizeof(full_msg) - 1, MSG_NOSIGNAL);
            (void)ignored;
            close(cfd);
            continue;
        }

        nl->epollout_on[c->idx] = false;
        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN;
        ev.data.u64 = pack_conn_id(conn_id(c));
        if (epoll_ctl(nl->epoll_fd, EPOLL_CTL_ADD, cfd, &ev) < 0) {
            close(cfd);             /* GCOVR_EXCL_LINE */
            conn_free(&s->conns, c); /* GCOVR_EXCL_LINE */
            continue;               /* GCOVR_EXCL_LINE */
        }

        conn_send_line(&s->conns, c, "HELLO|netsketch|1");
    }
}

static void handle_read(Server *s, Conn *c, int64_t now_ms)
{
    char buf[RECV_CHUNK_SIZE];
    ssize_t n = recv(c->fd, buf, sizeof(buf), 0);
    if (n == 0) {
        c->closing = true;
        return;
    }
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            c->closing = true;
        }
        return;
    }

    c->last_active_ms = now_ms;
    size_t off = 0;
    while (off < (size_t)n && !c->closing) {
        size_t consumed = 0;
        char line[FRAME_MAX_LINE];
        FrameStatus st = linebuf_feed(&c->in, buf + off, (size_t)n - off,
                                      &consumed, line, sizeof(line));
        off += consumed;
        if (st == FRAME_OK) {
            if (line[0] != '\0') {
                server_on_line(s, c, line);
            }
        } else if (st == FRAME_TOO_LONG) {
            char err_buf[64];
            int len = protocol_format_err(err_buf, sizeof(err_buf), ERR_LINE_TOO_LONG);
            if (len > 0) {
                conn_send_raw(&s->conns, c, err_buf, (size_t)len);
            }
        }
        if (c->overflow) {
            c->closing = true;
        }
    }
}

static void flush_dirty(NetLoop *nl, Server *s)
{
    for (int i = 0; i < s->conns.ndirty; i++) {
        Conn *c = &s->conns.conns[s->conns.dirty[i]];
        if (c->state != CONN_FREE && c->fd >= 0) {
            flush_conn_out(nl, c);
        }
    }
    conn_clear_dirty(&s->conns);
}

static void reap_closed(NetLoop *nl, Server *s)
{
    for (int i = 0; i < s->conns.max_clients; i++) {
        Conn *c = &s->conns.conns[i];
        if (c->state != CONN_FREE && (c->closing || c->overflow)) {
            if (c->fd >= 0) {
                (void)epoll_ctl(nl->epoll_fd, EPOLL_CTL_DEL, c->fd, NULL);
                close(c->fd);
            }
            nl->epollout_on[i] = false;
            conn_free(&s->conns, c);
        }
    }
}

int net_loop_init(NetLoop *nl, Server *s)
{
    if (!nl || !s) {
        return -1;
    }

    memset(nl, 0, sizeof(*nl));
    nl->listen_fd = -1;
    nl->epoll_fd = -1;
    g_stop_requested = 0;

    struct sigaction sa_ign;
    memset(&sa_ign, 0, sizeof(sa_ign));
    sa_ign.sa_handler = SIG_IGN;
    (void)sigaction(SIGPIPE, &sa_ign, NULL);

    struct sigaction sa_stop;
    memset(&sa_stop, 0, sizeof(sa_stop));
    sa_stop.sa_handler = on_stop_signal;
    (void)sigaction(SIGINT, &sa_stop, NULL);
    (void)sigaction(SIGTERM, &sa_stop, NULL);

    nl->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (nl->listen_fd < 0) {
        return -1; /* GCOVR_EXCL_LINE */
    }

    int one = 1;
    if (setsockopt(nl->listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0 ||
        set_nonblocking(nl->listen_fd) < 0) {
        net_loop_cleanup(nl, s); /* GCOVR_EXCL_LINE */
        return -1;               /* GCOVR_EXCL_LINE */
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)s->cfg.port);

    if (bind(nl->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(nl->listen_fd, 128) < 0) {
        net_loop_cleanup(nl, s);
        return -1;
    }

    socklen_t alen = sizeof(addr);
    if (getsockname(nl->listen_fd, (struct sockaddr *)&addr, &alen) == 0) {
        nl->bound_port = (int)ntohs(addr.sin_port);
    }

    nl->epoll_fd = epoll_create1(0);
    if (nl->epoll_fd < 0) {
        net_loop_cleanup(nl, s); /* GCOVR_EXCL_LINE */
        return -1;               /* GCOVR_EXCL_LINE */
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.u64 = EPOLL_TAG_LISTEN;
    if (epoll_ctl(nl->epoll_fd, EPOLL_CTL_ADD, nl->listen_fd, &ev) < 0) {
        net_loop_cleanup(nl, s); /* GCOVR_EXCL_LINE */
        return -1;               /* GCOVR_EXCL_LINE */
    }

    return 0;
}

int net_loop_step(NetLoop *nl, Server *s, int timeout_ms)
{
    if (!nl || !s || nl->epoll_fd < 0) {
        return -1;
    }

    struct epoll_event events[EPOLL_MAX_EVENTS];
    int n = epoll_wait(nl->epoll_fd, events, EPOLL_MAX_EVENTS, timeout_ms);
    if (n < 0) {
        if (errno == EINTR) {
            return 0;
        }
        return -1; /* GCOVR_EXCL_LINE */
    }

    int64_t now_ms = clock_now_ms(&s->clock);

    for (int i = 0; i < n; i++) {
        if (events[i].data.u64 == EPOLL_TAG_LISTEN) {
            handle_accept(nl, s, now_ms);
            continue;
        }

        Conn *c = conn_get(&s->conns, unpack_conn_id(events[i].data.u64));
        if (!c || c->closing) {
            continue;
        }

        uint32_t ev = events[i].events;
        if (ev & (EPOLLERR | EPOLLHUP)) {
            c->closing = true;
            continue;
        }
        if (ev & EPOLLIN) {
            handle_read(s, c, now_ms);
        }
        if ((ev & EPOLLOUT) && !c->closing) {
            flush_conn_out(nl, c);
        }
    }

    server_tick(s, now_ms);
    flush_dirty(nl, s);
    reap_closed(nl, s);
    return 0;
}

void net_loop_cleanup(NetLoop *nl, Server *s)
{
    if (!nl) {
        return;
    }
    if (s) {
        for (int i = 0; i < s->conns.max_clients; i++) {
            Conn *c = &s->conns.conns[i];
            if (c->state != CONN_FREE) {
                if (c->fd >= 0) {
                    close(c->fd);
                }
                conn_free(&s->conns, c);
            }
        }
    }
    if (nl->epoll_fd >= 0) {
        close(nl->epoll_fd);
        nl->epoll_fd = -1;
    }
    if (nl->listen_fd >= 0) {
        close(nl->listen_fd);
        nl->listen_fd = -1;
    }
}

int net_loop_run(Server *s)
{
    NetLoop nl;
    if (net_loop_init(&nl, s) < 0) {
        return 1;
    }

    while (!s->stop && !g_stop_requested) {
        if (net_loop_step(&nl, s, 200) < 0) {
            net_loop_cleanup(&nl, s); /* GCOVR_EXCL_LINE */
            return 1;                 /* GCOVR_EXCL_LINE */
        }
    }

    net_loop_cleanup(&nl, s);
    return 0;
}
