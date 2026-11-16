#include "dispatch.h"
#include "auth.h"
#include "room.h"
#include "session.h"
#include <stddef.h>

static void h_ping(Server *s, Conn *c, const Command *cmd)
{
    (void)cmd;
    conn_send_line(&s->conns, c, "PONG");
}

static const CmdSpec CMD_SPECS[] = {
    { CMD_PING,        AUTH_ANY,        ROOM_ANY,      RSTATE_NONE,     h_ping        },
    { CMD_REGISTER,    AUTH_GUEST_ONLY, ROOM_ANY,      RSTATE_NONE,     h_register    },
    { CMD_LOGIN,       AUTH_GUEST_ONLY, ROOM_ANY,      RSTATE_NONE,     h_login       },
    { CMD_LOGOUT,      AUTH_REQUIRED,   ROOM_ANY,      RSTATE_NONE,     h_logout      },
    { CMD_CREATE_ROOM, AUTH_REQUIRED,   ROOM_OUT_ONLY, RSTATE_NONE,     h_create_room },
    { CMD_LIST_ROOMS,  AUTH_REQUIRED,   ROOM_ANY,      RSTATE_NONE,     h_list_rooms  },
    { CMD_JOIN_ROOM,   AUTH_REQUIRED,   ROOM_OUT_ONLY, RSTATE_NONE,     h_join_room   },
    { CMD_LEAVE_ROOM,  AUTH_REQUIRED,   ROOM_IN_ONLY,  RSTATE_ANY,      h_leave_room  },
    { CMD_START,       AUTH_REQUIRED,   ROOM_IN_ONLY,  RSTATE_LOBBY,    NULL          },
    { CMD_SCOREBOARD,  AUTH_REQUIRED,   ROOM_IN_ONLY,  RSTATE_ANY,      NULL          },
    { CMD_CHOOSE_WORD, AUTH_REQUIRED,   ROOM_IN_ONLY,  RSTATE_CHOOSING, NULL          },
    { CMD_STROKE,      AUTH_REQUIRED,   ROOM_IN_ONLY,  RSTATE_DRAWING,  NULL          },
    { CMD_CLEAR,       AUTH_REQUIRED,   ROOM_IN_ONLY,  RSTATE_DRAWING,  NULL          },
    { CMD_GUESS,       AUTH_REQUIRED,   ROOM_IN_ONLY,  RSTATE_DRAWING,  NULL          },
    { CMD_CHAT,        AUTH_REQUIRED,   ROOM_IN_ONLY,  RSTATE_ANY,      NULL          },
};

void reply_ok(Server *s, Conn *c, const char *payload)
{
    if (!s || !c) {
        return;
    }
    if (!payload || payload[0] == '\0') {
        conn_send_line(&s->conns, c, "OK");
    } else {
        conn_sendf(&s->conns, c, "OK|%s", payload);
    }
}

void reply_err(Server *s, Conn *c, int code)
{
    char buf[128];
    if (!s || !c) {
        return;
    }
    if (code >= 900) {
        logger_log(&s->log, LOG_ERROR, clock_now_ms(&s->clock),
                   "sid=%s room=%d event=ERROR code=%d",
                   c->session_id[0] ? c->session_id : "-",
                   c->room_id, code);
    }
    int n = protocol_format_err(buf, sizeof(buf), code);
    if (n > 0) {
        conn_send_raw(&s->conns, c, buf, (size_t)n);
    }
}

static const CmdSpec *find_spec(CmdType type)
{
    size_t n = sizeof(CMD_SPECS) / sizeof(CMD_SPECS[0]);
    for (size_t i = 0; i < n; i++) {
        if (CMD_SPECS[i].type == type) {
            return &CMD_SPECS[i];
        }
    }
    return NULL; /* GCOVR_EXCL_LINE */
}

void dispatch_line(Server *s, Conn *c, const char *line)
{
    Command cmd;
    if (!s || !c || !line) {
        return;
    }

    int64_t now_ms = clock_now_ms(&s->clock);
    c->last_active_ms = now_ms;
    logger_log_proto(&s->log, now_ms, c->session_id, line);

    int err = protocol_parse(line, &cmd);
    if (err != ERR_OK) {
        reply_err(s, c, err);
        return;
    }

    const CmdSpec *spec = find_spec(cmd.type);
    if (!spec) {
        reply_err(s, c, ERR_UNKNOWN_CMD); /* GCOVR_EXCL_LINE */
        return;                           /* GCOVR_EXCL_LINE */
    }

    if (spec->auth_req == AUTH_REQUIRED && c->state != CONN_AUTHED) {
        reply_err(s, c, ERR_NOT_LOGGED_IN);
        return;
    }
    if (spec->auth_req == AUTH_GUEST_ONLY && c->state == CONN_AUTHED) {
        reply_err(s, c, ERR_ALREADY_LOGGED_IN);
        return;
    }

    if (spec->room_req == ROOM_OUT_ONLY && c->room_id >= 0) {
        reply_err(s, c, ERR_ALREADY_IN_ROOM);
        return;
    }
    if (spec->room_req == ROOM_IN_ONLY) {
        Room *r = room_find(s, c->room_id);
        if (!r) {
            reply_err(s, c, ERR_NOT_IN_ROOM);
            return;
        }
        if ((spec->room_states & (1u << r->state)) == 0u) {
            reply_err(s, c, ERR_WRONG_STATE);
            return;
        }
    }

    if (!spec->handler) {
        reply_err(s, c, ERR_INTERNAL);
        return;
    }

    spec->handler(s, c, &cmd);
}
