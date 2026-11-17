#include "room.h"
#include "dispatch.h"
#include "game.h"
#include "server.h"
#include <stdio.h>
#include <string.h>

#define ROOMS_PER_PAGE 8

static const char *room_state_str(RoomState st)
{
    return (st == ROOM_LOBBY) ? "LOBBY" : "PLAYING";
}

static bool trim_and_validate_room_name(const char *raw, char out[ROOM_NAME_MAX + 1])
{
    if (!raw) {
        return false;
    }
    while (*raw == ' ') {
        raw++;
    }
    size_t len = strlen(raw);
    while (len > 0 && raw[len - 1] == ' ') {
        len--;
    }
    if (len < 1 || len > ROOM_NAME_MAX) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = raw[i];
        bool ok = (c >= 'A' && c <= 'Z') ||
                  (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') ||
                  (c == ' ') || (c == '_') || (c == '-');
        if (!ok) {
            return false;
        }
    }
    memcpy(out, raw, len);
    out[len] = '\0';
    return true;
}

void room_table_init(struct Server *s)
{
    if (!s) {
        return;
    }
    memset(s->rooms, 0, sizeof(s->rooms));
    s->next_room_id = 1;
}

Room *room_find(struct Server *s, int room_id)
{
    if (!s || room_id < 1) {
        return NULL;
    }
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (s->rooms[i].id == room_id) {
            return &s->rooms[i];
        }
    }
    return NULL;
}

void room_broadcast(struct Server *s, const Room *r, const char *line, int exclude_idx)
{
    if (!s || !r || !line) {
        return;
    }
    for (int i = 0; i < r->nplayers; i++) {
        if (i == exclude_idx) {
            continue;
        }
        Conn *pc = conn_get(&s->conns, r->players[i].conn);
        if (pc && !pc->closing) {
            conn_send_line(&s->conns, pc, line);
        }
    }
}

void room_send_info(struct Server *s, const Room *r, Conn *c)
{
    if (!s || !r || !c || r->nplayers < 1) {
        return;
    }

    char plist[MAX_PLAYERS * (NAME_MAX_LEN + 1) + 1];
    size_t pos = 0;
    plist[0] = '\0';

    for (int i = 0; i < r->nplayers; i++) {
        int n = snprintf(plist + pos, sizeof(plist) - pos, "%s%s",
                         (i > 0) ? "," : "", r->players[i].name);
        if (n > 0 && pos + (size_t)n < sizeof(plist)) {
            pos += (size_t)n;
        }
    }

    conn_sendf(&s->conns, c, "ROOM_INFO|%d|%s|%s|%d|%d|%d|%s|%s",
               r->id, r->name, r->players[0].name,
               r->max_players, r->rounds, r->draw_time,
               room_state_str(r->state), plist);
}

void h_create_room(struct Server *s, Conn *c, const Command *cmd)
{
    int max_players = 0;
    int rounds = 0;
    int draw_time = 0;

    if (!protocol_parse_int(cmd->argv[1], &max_players) ||
        !protocol_parse_int(cmd->argv[2], &rounds) ||
        !protocol_parse_int(cmd->argv[3], &draw_time)) {
        reply_err(s, c, ERR_BAD_FORMAT);
        return;
    }

    char clean_name[ROOM_NAME_MAX + 1];
    if (!trim_and_validate_room_name(cmd->argv[0], clean_name) ||
        max_players < 2 || max_players > MAX_PLAYERS ||
        rounds < 1 || rounds > 10 ||
        draw_time < 30 || draw_time > 180) {
        reply_err(s, c, ERR_INVALID_ROOM_PARAMS);
        return;
    }

    Room *slot = NULL;
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (s->rooms[i].id == 0) {
            slot = &s->rooms[i];
            break;
        }
    }
    if (!slot) {
        reply_err(s, c, ERR_ROOM_LIMIT);
        return;
    }

    memset(slot, 0, sizeof(*slot));
    slot->id = s->next_room_id++;
    memcpy(slot->name, clean_name, strlen(clean_name) + 1);
    slot->state = ROOM_LOBBY;
    slot->max_players = max_players;
    slot->rounds = rounds;
    slot->draw_time = draw_time;
    slot->nplayers = 1;
    slot->players[0].conn = conn_id(c);
    memcpy(slot->players[0].name, c->user, strlen(c->user) + 1);

    c->room_id = slot->id;

    char id_buf[16];
    (void)snprintf(id_buf, sizeof(id_buf), "%d", slot->id);
    reply_ok(s, c, id_buf);
    room_send_info(s, slot, c);
}

void h_list_rooms(struct Server *s, Conn *c, const Command *cmd)
{
    int page = 0;
    if (!protocol_parse_int(cmd->argv[0], &page) || page < 1) {
        reply_err(s, c, ERR_BAD_FORMAT);
        return;
    }

    int total = 0;
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (s->rooms[i].id != 0) {
            total++;
        }
    }

    char out[MAX_LINE];
    int n = snprintf(out, sizeof(out), "OK|%d|%d", total, page);
    size_t pos = (n > 0) ? (size_t)n : 0;

    int start = (page - 1) * ROOMS_PER_PAGE;
    int end = start + ROOMS_PER_PAGE;
    int seen = 0;

    for (int i = 0; i < MAX_ROOMS && seen < end; i++) {
        const Room *r = &s->rooms[i];
        if (r->id == 0) {
            continue;
        }
        if (seen >= start) {
            int w = snprintf(out + pos, sizeof(out) - pos, "|%d,%s,%d,%d,%s",
                             r->id, r->name, r->nplayers, r->max_players,
                             room_state_str(r->state));
            if (w > 0 && pos + (size_t)w < sizeof(out)) {
                pos += (size_t)w;
            }
        }
        seen++;
    }

    conn_send_line(&s->conns, c, out);
}

void h_join_room(struct Server *s, Conn *c, const Command *cmd)
{
    int room_id = 0;
    if (!protocol_parse_int(cmd->argv[0], &room_id) || room_id < 1) {
        reply_err(s, c, ERR_BAD_FORMAT);
        return;
    }

    Room *r = room_find(s, room_id);
    if (!r) {
        reply_err(s, c, ERR_ROOM_NOT_FOUND);
        return;
    }
    if (r->state != ROOM_LOBBY) {
        reply_err(s, c, ERR_ROOM_IN_GAME);
        return;
    }
    if (r->nplayers >= r->max_players) {
        reply_err(s, c, ERR_ROOM_FULL);
        return;
    }

    int idx = r->nplayers++;
    memset(&r->players[idx], 0, sizeof(Player));
    r->players[idx].conn = conn_id(c);
    memcpy(r->players[idx].name, c->user, strlen(c->user) + 1);
    c->room_id = r->id;

    char id_buf[16];
    (void)snprintf(id_buf, sizeof(id_buf), "%d", r->id);
    reply_ok(s, c, id_buf);
    room_send_info(s, r, c);

    char ev[64];
    (void)snprintf(ev, sizeof(ev), "PLAYER_JOINED|%s", c->user);
    room_broadcast(s, r, ev, idx);
}

void room_leave(struct Server *s, Conn *c, const char *reason)
{
    if (!s || !c || c->room_id < 0) {
        return;
    }

    Room *r = room_find(s, c->room_id);
    c->room_id = -1;
    if (!r) {
        return;
    }

    int pidx = -1;
    for (int i = 0; i < r->nplayers; i++) {
        if (strcmp(r->players[i].name, c->user) == 0 ||
            (r->players[i].conn.idx == c->idx && r->players[i].conn.gen == c->gen)) {
            pidx = i;
            break;
        }
    }
    if (pidx < 0) {
        return;
    }

    char left_name[NAME_MAX_LEN + 1];
    memcpy(left_name, r->players[pidx].name, sizeof(left_name));
    bool was_drawer = (r->state != ROOM_LOBBY && strcmp(r->drawer, left_name) == 0);

    if (pidx + 1 < r->nplayers) {
        memmove(&r->players[pidx], &r->players[pidx + 1],
                (size_t)(r->nplayers - pidx - 1) * sizeof(Player));
    }
    r->nplayers--;

    if (r->nplayers <= 0) {
        memset(r, 0, sizeof(*r));
        return;
    }

    char ev[64];
    (void)snprintf(ev, sizeof(ev), "PLAYER_LEFT|%s|%s",
                   left_name, (reason && reason[0]) ? reason : "LEAVE");
    room_broadcast(s, r, ev, -1);

    if (pidx == 0) {
        (void)snprintf(ev, sizeof(ev), "HOST_CHANGED|%s", r->players[0].name);
        room_broadcast(s, r, ev, -1);
    }

    if (r->state != ROOM_LOBBY) {
        game_on_player_left(s, r, left_name, was_drawer);
    }
}

void h_leave_room(struct Server *s, Conn *c, const Command *cmd)
{
    (void)cmd;
    reply_ok(s, c, NULL);
    room_leave(s, c, "LEAVE");
}
