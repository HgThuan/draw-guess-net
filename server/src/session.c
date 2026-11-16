#include "session.h"
#include "auth.h"
#include "dispatch.h"
#include "logger.h"
#include "server.h"
#include <string.h>

static void gen_session_id(Rng *rng, char out[17])
{
    static const char HEX[] = "0123456789abcdef";
    uint8_t raw[8];
    rng_bytes(rng, raw, sizeof(raw));
    for (size_t i = 0; i < sizeof(raw); i++) {
        out[i * 2]     = HEX[(raw[i] >> 4) & 0x0Fu];
        out[i * 2 + 1] = HEX[raw[i] & 0x0Fu];
    }
    out[16] = '\0';
}

static void clear_user_online(Server *s, Conn *c)
{
    if (c->state == CONN_AUTHED && c->user[0] != '\0') {
        User *u = user_store_find(&s->users, c->user);
        if (u && u->online.idx == c->idx && u->online.gen == c->gen) {
            u->online.idx = CONN_IDX_NONE;
            u->online.gen = 0;
        }
    }
}

void h_login(struct Server *s, Conn *c, const Command *cmd)
{
    int64_t now_ms = clock_now_ms(&s->clock);
    const char *username = cmd->argv[0];
    const char *password = cmd->argv[1];
    User *u = NULL;

    int rc = auth_verify(&s->users, username, password, &u);
    if (rc != ERR_OK || !u) {
        logger_log(&s->log, LOG_INFO, now_ms,
                   "sid=- room=- event=LOGIN_FAIL user=%s code=%d",
                   username, ERR_BAD_CREDENTIALS);
        reply_err(s, c, ERR_BAD_CREDENTIALS);
        return;
    }

    Conn *active = conn_get(&s->conns, u->online);
    if (active && !active->closing && active->state == CONN_AUTHED) {
        logger_log(&s->log, LOG_INFO, now_ms,
                   "sid=- room=- event=LOGIN_FAIL user=%s code=%d",
                   username, ERR_ACCOUNT_IN_USE);
        reply_err(s, c, ERR_ACCOUNT_IN_USE);
        return;
    }

    gen_session_id(&s->rng, c->session_id);
    size_t ulen = strlen(u->name);
    memcpy(c->user, u->name, ulen + 1);
    c->state = CONN_AUTHED;
    c->room_id = -1;
    c->last_active_ms = now_ms;
    u->online = conn_id(c);

    logger_log(&s->log, LOG_INFO, now_ms,
               "sid=%s room=- event=LOGIN user=%s",
               c->session_id, c->user);
    reply_ok(s, c, c->session_id);
}

void h_logout(struct Server *s, Conn *c, const Command *cmd)
{
    (void)cmd;
    int64_t now_ms = clock_now_ms(&s->clock);

    logger_log(&s->log, LOG_INFO, now_ms,
               "sid=%s room=%d event=LOGOUT user=%s",
               c->session_id[0] ? c->session_id : "-",
               c->room_id,
               c->user[0] ? c->user : "-");

    clear_user_online(s, c);
    c->state = CONN_CONNECTED;
    c->user[0] = '\0';
    c->session_id[0] = '\0';
    c->room_id = -1;

    reply_ok(s, c, NULL);
}

void session_on_disconnect(struct Server *s, Conn *c)
{
    if (!s || !c || c->state == CONN_FREE) {
        return;
    }

    int64_t now_ms = clock_now_ms(&s->clock);
    logger_log(&s->log, LOG_INFO, now_ms,
               "sid=%s room=%d event=DISCONNECT user=%s",
               c->session_id[0] ? c->session_id : "-",
               c->room_id,
               c->user[0] ? c->user : "-");

    clear_user_online(s, c);
    c->user[0] = '\0';
    c->session_id[0] = '\0';
    c->room_id = -1;
}

void session_tick(struct Server *s, int64_t now_ms)
{
    if (!s) {
        return;
    }

    int64_t timeout_ms = (int64_t)s->cfg.idle_timeout_s * 1000;
    for (int i = 0; i < s->conns.max_clients; i++) {
        Conn *c = &s->conns.conns[i];
        if (c->state != CONN_FREE && !c->closing) {
            if (now_ms - c->last_active_ms >= timeout_ms) {
                logger_log(&s->log, LOG_INFO, now_ms,
                           "sid=%s room=%d event=KICK reason=IDLE user=%s",
                           c->session_id[0] ? c->session_id : "-",
                           c->room_id,
                           c->user[0] ? c->user : "-");
                conn_send_line(&s->conns, c, "KICK|IDLE");
                c->closing = true;
            }
        }
    }
}
