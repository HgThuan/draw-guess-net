#include "game.h"
#include "dispatch.h"
#include "scoring.h"
#include "server.h"
#include "words.h"
#include <stdio.h>
#include <string.h>

static void snapshot_round_order(Room *r)
{
    r->order_n = r->nplayers;
    r->order_pos = 0;
    for (int i = 0; i < r->nplayers; i++) {
        memcpy(r->order[i], r->players[i].name, sizeof(r->order[i]));
    }
}

static int find_player_idx(const Room *r, const char *name)
{
    for (int i = 0; i < r->nplayers; i++) {
        if (strcmp(r->players[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

bool game_next_drawer(struct Server *s, Room *r)
{
    (void)s;
    if (!r || r->nplayers < 2) {
        return false;
    }

    for (;;) {
        while (r->order_pos < r->order_n) {
            const char *cand = r->order[r->order_pos];
            if (find_player_idx(r, cand) >= 0) {
                memcpy(r->drawer, cand, sizeof(r->drawer));
                return true;
            }
            r->order_pos++;
        }

        r->round++;
        if (r->round > r->rounds) {
            return false;
        }
        snapshot_round_order(r);
    }
}

bool game_all_guessed(const Room *r)
{
    if (!r || r->nplayers < 2) {
        return false;
    }

    int guesser_total = 0;
    int guessed_now = 0;
    for (int i = 0; i < r->nplayers; i++) {
        if (strcmp(r->players[i].name, r->drawer) == 0) {
            continue;
        }
        guesser_total++;
        if (r->players[i].guessed) {
            guessed_now++;
        }
    }
    return guesser_total > 0 && guessed_now == guesser_total;
}

static void enter_choosing(struct Server *s, Room *r,
                           const char picked[3][WORD_MAX], int64_t now_ms)
{
    for (int i = 0; i < 3; i++) {
        (void)snprintf(r->options[i], sizeof(r->options[i]), "%s", picked[i]);
    }
    r->state = ROOM_CHOOSING;
    r->word[0] = '\0';
    r->guessed_count = 0;
    r->turn_guess_total = 0;
    for (int i = 0; i < r->nplayers; i++) {
        r->players[i].guessed = false;
        r->players[i].turn_delta = 0;
    }
    r->phase_deadline_ms = now_ms + (int64_t)s->cfg.choose_time_s * 1000;

    char ev[128];
    (void)snprintf(ev, sizeof(ev), "TURN_START|%d|%s|%d",
                   r->round, r->drawer, s->cfg.choose_time_s);
    room_broadcast(s, r, ev, CONN_ID_NONE);

    int didx = find_player_idx(r, r->drawer);
    if (didx >= 0) {
        Conn *dc = conn_get(&s->conns, r->players[didx].conn);
        if (dc && !dc->closing) {
            conn_sendf(&s->conns, dc, "WORD_OPTIONS|%s|%s|%s",
                       r->options[0], r->options[1], r->options[2]);
        }
    }
}

void game_begin_drawing(struct Server *s, Room *r, int choice_idx, int64_t now_ms)
{
    if (!s || !r || choice_idx < 0 || choice_idx > 2) {
        return;
    }

    memcpy(r->word, r->options[choice_idx], sizeof(r->word));
    r->state = ROOM_DRAWING;
    r->guessed_count = 0;
    r->turn_guess_total = 0;
    for (int i = 0; i < r->nplayers; i++) {
        r->players[i].guessed = false;
        r->players[i].turn_delta = 0;
    }

    r->phase_deadline_ms = now_ms + (int64_t)r->draw_time * 1000;
    r->next_tick_ms = now_ms + (int64_t)DEFAULT_TIME_TICK * 1000;

    char hint[WORD_MAX];
    words_make_hint(r->word, hint);

    for (int i = 0; i < r->nplayers; i++) {
        Conn *pc = conn_get(&s->conns, r->players[i].conn);
        if (!pc || pc->closing) {
            continue;
        }
        bool is_drawer = (strcmp(r->players[i].name, r->drawer) == 0);
        conn_sendf(&s->conns, pc, "DRAW_START|%s|%d|%s",
                   r->drawer, r->draw_time, is_drawer ? r->word : hint);
    }
}

void h_start(struct Server *s, Conn *c, const Command *cmd)
{
    (void)cmd;
    Room *r = room_find(s, c->room_id);
    if (!r) {
        reply_err(s, c, ERR_NOT_IN_ROOM); /* GCOVR_EXCL_LINE */
        return;                           /* GCOVR_EXCL_LINE */
    }

    /* Check host by ConnId (with generation) */
    if (r->nplayers < 1 ||
        r->players[0].conn.idx != c->idx ||
        r->players[0].conn.gen != c->gen) {
        reply_err(s, c, ERR_NOT_HOST);
        return;
    }

    if (r->nplayers < 2) {
        reply_err(s, c, ERR_NOT_ENOUGH_PLAYERS);
        return;
    }

    /* Pick words BEFORE mutating room state or sending OK */
    char picked[3][WORD_MAX];
    if (!words_pick3(&s->words, &s->rng, picked)) {
        reply_err(s, c, ERR_INTERNAL);
        return;
    }

    reply_ok(s, c, NULL);

    for (int i = 0; i < r->nplayers; i++) {
        r->players[i].score = 0;
        r->players[i].guessed = false;
        r->players[i].turn_delta = 0;
    }

    r->round = 1;
    snapshot_round_order(r);
    (void)game_next_drawer(s, r);

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

    char ev[256];
    (void)snprintf(ev, sizeof(ev), "GAME_START|%d|%s", r->rounds, plist);
    room_broadcast(s, r, ev, CONN_ID_NONE);

    enter_choosing(s, r, picked, clock_now_ms(&s->clock));
}

void h_choose_word(struct Server *s, Conn *c, const Command *cmd)
{
    Room *r = room_find(s, c->room_id);
    if (!r) {
        reply_err(s, c, ERR_NOT_IN_ROOM); /* GCOVR_EXCL_LINE */
        return;                           /* GCOVR_EXCL_LINE */
    }

    /* Step 5: Role check (drawer only) */
    if (strcmp(c->user, r->drawer) != 0) {
        reply_err(s, c, ERR_NOT_DRAWER);
        return;
    }

    /* Step 6: Choice value check (1..3) */
    int idx = 0;
    (void)protocol_parse_int(cmd->argv[0], &idx);
    if (idx < 1 || idx > 3) {
        reply_err(s, c, ERR_INVALID_WORD_CHOICE);
        return;
    }

    reply_ok(s, c, NULL);
    game_begin_drawing(s, r, idx - 1, clock_now_ms(&s->clock));
}

void h_guess(struct Server *s, Conn *c, const Command *cmd)
{
    Room *r = room_find(s, c->room_id);
    if (!r) {
        reply_err(s, c, ERR_NOT_IN_ROOM); /* GCOVR_EXCL_LINE */
        return;                           /* GCOVR_EXCL_LINE */
    }

    int pidx = find_player_idx(r, c->user);
    if (pidx < 0) {
        reply_err(s, c, ERR_NOT_IN_ROOM); /* GCOVR_EXCL_LINE */
        return;                           /* GCOVR_EXCL_LINE */
    }

    /* Step 5: Drawer cannot guess (302), already guessed cannot guess again (303) */
    if (strcmp(c->user, r->drawer) == 0) {
        reply_err(s, c, ERR_IS_DRAWER);
        return;
    }

    Player *p = &r->players[pidx];
    if (p->guessed) {
        reply_err(s, c, ERR_ALREADY_GUESSED);
        return;
    }

    const char *text = cmd->argv[0];
    if (!words_match(text, r->word)) {
        reply_ok(s, c, "WRONG");
        char ev[MAX_LINE];
        (void)snprintf(ev, sizeof(ev), "GUESS_WRONG|%s|%s", c->user, text);
        room_broadcast(s, r, ev, CONN_ID_NONE);
        return;
    }

    int64_t now_ms = clock_now_ms(&s->clock);
    int64_t rem_ms = r->phase_deadline_ms - now_ms;
    int remaining_s = (rem_ms > 0) ? (int)(rem_ms / 1000) : 0;
    if (remaining_s > r->draw_time) {
        remaining_s = r->draw_time; /* GCOVR_EXCL_LINE */
    }

    r->guessed_count++;
    int pts = scoring_guess(r->guessed_count, remaining_s, r->draw_time);
    p->guessed = true;
    p->turn_delta = pts;
    p->score += pts;
    r->turn_guess_total += pts;

    char ok_payload[32];
    (void)snprintf(ok_payload, sizeof(ok_payload), "CORRECT|%d", pts);
    reply_ok(s, c, ok_payload);

    char ev[128];
    (void)snprintf(ev, sizeof(ev), "GUESS_CORRECT|%s|%d", c->user, pts);
    room_broadcast(s, r, ev, CONN_ID_NONE);
}

void game_on_player_left(struct Server *s, Room *r, const char *left_user, bool was_drawer)
{
    (void)left_user;
    if (!s || !r || r->state == ROOM_LOBBY) {
        return;
    }

    if (r->nplayers < 2) {
        r->state = ROOM_LOBBY;
        r->drawer[0] = '\0';
        return;
    }

    if (was_drawer) {
        r->order_pos++;
        char picked[3][WORD_MAX];
        if (game_next_drawer(s, r) && words_pick3(&s->words, &s->rng, picked)) {
            enter_choosing(s, r, picked, clock_now_ms(&s->clock));
        } else {
            r->state = ROOM_LOBBY;
        }
    }
}

static void tick_choosing(struct Server *s, Room *r, int64_t now_ms)
{
    if (now_ms >= r->phase_deadline_ms) {
        int choice = rng_range(&s->rng, 0, 2);
        game_begin_drawing(s, r, choice, now_ms);
    }
}

static void tick_drawing(struct Server *s, Room *r, int64_t now_ms)
{
    /* Note: TIMEUP turn transition is added in the turn-end task; do not emit TIME|0 at deadline. */
    if (now_ms >= r->phase_deadline_ms || now_ms < r->next_tick_ms) {
        return;
    }

    int64_t step_ms = (int64_t)DEFAULT_TIME_TICK * 1000;
    int64_t scheduled_ms = r->next_tick_ms;
    while (r->next_tick_ms <= now_ms) {
        scheduled_ms = r->next_tick_ms;
        r->next_tick_ms += step_ms;
    }

    if (scheduled_ms < r->phase_deadline_ms) {
        int rem = (int)((r->phase_deadline_ms - scheduled_ms) / 1000);
        if (rem > 0) {
            char ev[32];
            (void)snprintf(ev, sizeof(ev), "TIME|%d", rem);
            room_broadcast(s, r, ev, CONN_ID_NONE);
        }
    }
}

void game_tick(struct Server *s, int64_t now_ms)
{
    if (!s) {
        return;
    }
    for (int i = 0; i < MAX_ROOMS; i++) {
        Room *r = &s->rooms[i];
        if (r->id == 0) {
            continue;
        }
        switch (r->state) {
        case ROOM_CHOOSING:
            tick_choosing(s, r, now_ms);
            break;
        case ROOM_DRAWING:
            tick_drawing(s, r, now_ms);
            break;
        case ROOM_LOBBY:
        case ROOM_TURN_END:
        default:
            break;
        }
    }
}
