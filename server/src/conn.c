#include "conn.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void conn_table_init(ConnTable *t, int max_clients)
{
    if (!t) {
        return;
    }
    memset(t, 0, sizeof(*t));
    if (max_clients <= 0 || max_clients > MAX_CLIENTS) {
        max_clients = MAX_CLIENTS;
    }
    t->max_clients = max_clients;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        t->conns[i].fd = -1;
        t->conns[i].idx = (uint16_t)i;
        t->conns[i].gen = 1;
        t->conns[i].room_id = -1;
    }
}

Conn *conn_alloc(ConnTable *t, int fd, int64_t now_ms)
{
    if (!t || t->active_count >= t->max_clients) {
        return NULL;
    }
    for (int i = 0; i < t->max_clients; i++) {
        Conn *c = &t->conns[i];
        if (c->state == CONN_FREE) {
            c->fd = fd;
            c->state = CONN_CONNECTED;
            linebuf_init(&c->in);
            c->out_len = 0;
            c->in_dirty = false;
            c->closing = false;
            c->overflow = false;
            c->last_active_ms = now_ms;
            c->user[0] = '\0';
            c->session_id[0] = '\0';
            c->room_id = -1;
            t->active_count++;
            return c;
        }
    }
    return NULL;
}

void conn_free(ConnTable *t, Conn *c)
{
    if (!t || !c || c->state == CONN_FREE) {
        return;
    }
    c->fd = -1;
    c->state = CONN_FREE;
    c->out_len = 0;
    c->in_dirty = false;
    c->closing = false;
    c->overflow = false;
    c->user[0] = '\0';
    c->session_id[0] = '\0';
    c->room_id = -1;
    c->gen++;
    if (c->gen == 0) {
        c->gen = 1;
    }
    if (t->active_count > 0) {
        t->active_count--;
    }
}

ConnId conn_id(const Conn *c)
{
    if (!c || c->state == CONN_FREE) {
        ConnId none = { CONN_IDX_NONE, 0 };
        return none;
    }
    ConnId id = { c->idx, c->gen };
    return id;
}

Conn *conn_get(ConnTable *t, ConnId id)
{
    if (!t || id.idx >= (uint16_t)t->max_clients) {
        return NULL;
    }
    Conn *c = &t->conns[id.idx];
    if (c->state == CONN_FREE || c->gen != id.gen) {
        return NULL;
    }
    return c;
}

bool conn_send_raw(ConnTable *t, Conn *c, const char *data, size_t len)
{
    if (!t || !c || c->state == CONN_FREE || !data) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    if (c->out_len + len > OUT_BUF) {
        /* Vượt OUT_BUF: coi là client quá chậm và đánh dấu đóng (D-008) */
        c->overflow = true;
        c->closing = true;
        return false;
    }
    memcpy(c->out + c->out_len, data, len);
    c->out_len += len;
    if (!c->in_dirty && t->ndirty < MAX_CLIENTS) {
        t->dirty[t->ndirty++] = c->idx;
        c->in_dirty = true;
    }
    return true;
}

bool conn_send_line(ConnTable *t, Conn *c, const char *line)
{
    if (!line) {
        return false;
    }
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
        return conn_send_raw(t, c, line, len);
    }
    if (!t || !c || c->state == CONN_FREE) {
        return false;
    }
    if (c->out_len + len + 1 > OUT_BUF) {
        c->overflow = true;
        c->closing = true;
        return false;
    }
    if (len > 0) {
        memcpy(c->out + c->out_len, line, len);
    }
    c->out[c->out_len + len] = '\n';
    c->out_len += len + 1;
    if (!c->in_dirty && t->ndirty < MAX_CLIENTS) {
        t->dirty[t->ndirty++] = c->idx;
        c->in_dirty = true;
    }
    return true;
}

bool conn_sendf(ConnTable *t, Conn *c, const char *fmt, ...)
{
    if (!t || !c || !fmt) {
        return false;
    }
    char buf[MAX_LINE];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof(buf)) {
        return false;
    }
    return conn_send_line(t, c, buf);
}

void conn_consume_out(Conn *c, size_t sent)
{
    if (!c || sent == 0) {
        return;
    }
    if (sent >= c->out_len) {
        c->out_len = 0;
        return;
    }
    memmove(c->out, c->out + sent, c->out_len - sent);
    c->out_len -= sent;
}

void conn_clear_dirty(ConnTable *t)
{
    if (!t) {
        return;
    }
    for (int i = 0; i < t->ndirty; i++) {
        int idx = t->dirty[i];
        if (idx >= 0 && idx < MAX_CLIENTS) {
            t->conns[idx].in_dirty = false;
        }
    }
    t->ndirty = 0;
}
