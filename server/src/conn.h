#ifndef NETSKETCH_CONN_H
#define NETSKETCH_CONN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "config.h"
#include "framing.h"

#define CONN_IDX_NONE 0xFFFFu

typedef struct {
    uint16_t idx;
    uint16_t gen;
} ConnId;

typedef enum {
    CONN_FREE = 0,
    CONN_CONNECTED,
    CONN_AUTHED
} ConnState;

typedef struct {
    int       fd;
    uint16_t  idx;
    uint16_t  gen;
    ConnState state;
    LineBuf   in;
    char      out[OUT_BUF];
    size_t    out_len;
    bool      in_dirty;
    bool      closing;
    bool      overflow;
    int64_t   last_active_ms;
    char      user[NAME_MAX_LEN + 1];
    char      session_id[17];
    int       room_id;
} Conn;

typedef struct {
    Conn conns[MAX_CLIENTS];
    int  max_clients;
    int  active_count;
    int  dirty[MAX_CLIENTS];
    int  ndirty;
} ConnTable;

void   conn_table_init(ConnTable *t, int max_clients);
Conn  *conn_alloc(ConnTable *t, int fd, int64_t now_ms);
void   conn_free(ConnTable *t, Conn *c);
ConnId conn_id(const Conn *c);
Conn  *conn_get(ConnTable *t, ConnId id);

bool conn_send_raw(ConnTable *t, Conn *c, const char *data, size_t len);
bool conn_send_line(ConnTable *t, Conn *c, const char *line);
bool conn_sendf(ConnTable *t, Conn *c, const char *fmt, ...);
void conn_consume_out(Conn *c, size_t sent);
void conn_clear_dirty(ConnTable *t);

#endif
