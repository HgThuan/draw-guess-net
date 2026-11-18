#include "game.h"
#include "dispatch.h"
#include "scoring.h"
#include "server.h"
#include "words.h"
#include <stdio.h>
#include <string.h>

static const char *turn_end_reason_str(TurnEndReason r)
{
    switch (r) {
    case TURN_END_ALL_GUESSED: return "ALL_GUESSED";
    case TURN_END_DRAWER_LEFT: return "DRAWER_LEFT";
    case TURN_END_TIMEUP:
    default:                   return "TIMEUP";
    }
}

static const char *game_end_reason_str(GameEndReason r)
{
    switch (r) {
    case GAME_END_NOT_ENOUGH_PLAYERS: return "NOT_ENOUGH_PLAYERS";
    case GAME_END_FINISHED:
    default:                          return "FINISHED";
    }
}

/* Snapshot current players into order[] and set order_pos = -1 so game_next_drawer() increments to 0. */
static void snapshot_round_order(Room *r)
{
    r->order_n = r->nplayers;
    r->order_pos = -1;
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

bool game_is_current_drawer(const Room *r, const char *name)
{
    if (!r || !name || name[0] == '\0') {
        return false;
    }
    return (r->state == ROOM_CHOOSING || r->state == ROOM_DRAWING) &&
           strcmp(r->drawer, name) == 0;
}

bool game_next_drawer(struct Server *s, Room *r)
{
    (void)s;
    if (!r || r->nplayers < 2) {
        return false;
    }

    for (;;) {
        r->order_pos++;
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

void game_format_ranking(const Room *r, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (!r || r->nplayers <= 0) {
        return;
    }

    Player sorted[MAX_PLAYERS];
    int n = r->nplayers;
    for (int i = 0; i < n; i++) {
        sorted[i] = r->players[i];
    }

    /* Insertion sort: score descending, tie-break by username ascending (ASCII strcmp) */
    for (int i = 1; i < n; i++) {
        Player key = sorted[i];
        int j = i - 1;
        while (j >= 0 &&
               (sorted[j].score < key.score ||
                (sorted[j].score == key.score && strcmp(sorted[j].name, key.name) > 0))) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }

    size_t pos = 0;
    for (int i = 0; i < n; i++) {
        int w = snprintf(out + pos, cap - pos, "%s%s:%d",
                         (i > 0) ? "," : "", sorted[i].name, sorted[i].score);
        if (w < 0 || pos + (size_t)w >= cap) {
            break; /* GCOVR_EXCL_LINE */
        }
        pos += (size_t)w;
    }
}

static void reset_room_to_lobby(Room *r)
{
    r->state = ROOM_LOBBY;
    r->round = 0;
    memset(r->order, 0, sizeof(r->order));
    r->order_n = 0;
    r->order_pos = 0;
    memset(r->drawer, 0, sizeof(r->drawer));
    memset(r->options, 0, sizeof(r->options));
    memset(r->word, 0, sizeof(r->word));
    r->guessed_count = 0;
    r->turn_guess_total = 0;
    r->phase_deadline_ms = 0;
    r->next_tick_ms = 0;

    for (int i = 0; i < r->nplayers; i++) {
        r->players[i].score = 0;
        r->players[i].guessed = false;
        r->players[i].turn_delta = 0;
    }
}

void game_end_game(struct Server *s, Room *r, GameEndReason reason)
{
    if (!s || !r || r->state == ROOM_LOBBY) {
        return;
    }

    char ranking[RANKING_BUF_SIZE];
    game_format_ranking(r, ranking, sizeof(ranking));

    logger_log(&s->log, LOG_INFO, clock_now_ms(&s->clock),
               "sid=- room=%d event=GAME_END reason=%s", r->id, game_end_reason_str(reason));

    char ev[MAX_LINE];
    (void)snprintf(ev, sizeof(ev), "GAME_END|%s|%s",
                   game_end_reason_str(reason), ranking);
    room_broadcast(s, r, ev, CONN_ID_NONE);

    reset_room_to_lobby(r);

    for (int i = 0; i < r->nplayers; i++) {
        Conn *pc = conn_get(&s->conns, r->players[i].conn);
        if (pc && !pc->closing) {
            room_send_info(s, r, pc);
        }
    }
}

static void enter_choosing(struct Server *s, Room *r,
                           const char picked[3][WORD_MAX], int64_t now_ms)
{
    for (int i = 0; i < 3; i++) {
        (void)snprintf(r->options[i], sizeof(r->options[i]), "%s", picked[i]);
    }
    r->state = ROOM_CHOOSING;
    memset(r->word, 0, sizeof(r->word));
    r->guessed_count = 0;
    r->turn_guess_total = 0;
    for (int i = 0; i < r->nplayers; i++) {
        r->players[i].guessed = false;
        r->players[i].turn_delta = 0;
    }
    r->phase_deadline_ms = now_ms + (int64_t)s->cfg.choose_time_s * 1000;
    r->next_tick_ms = 0;

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

void game_end_turn(struct Server *s, Room *r, TurnEndReason reason, int64_t now_ms)
{
    if (!s || !r) {
        return;
    }
    if (reason == TURN_END_DRAWER_LEFT) {
        if (r->state != ROOM_DRAWING && r->state != ROOM_CHOOSING) {
            return;
        }
    } else {
        if (r->state != ROOM_DRAWING) {
            return;
        }
    }

    if (reason != TURN_END_DRAWER_LEFT) {
        int didx = find_player_idx(r, r->drawer);
        if (didx >= 0) {
            int dpts = scoring_drawer(r->turn_guess_total, r->nplayers);
            r->players[didx].turn_delta = dpts;
            r->players[didx].score += dpts;
        }
    }

    char scores[SCORES_BUF_SIZE];
    size_t pos = 0;
    scores[0] = '\0';
    for (int i = 0; i < r->nplayers; i++) {
        int w = snprintf(scores + pos, sizeof(scores) - pos, "%s%s:%d:%d",
                         (i > 0) ? "," : "",
                         r->players[i].name,
                         r->players[i].score,
                         r->players[i].turn_delta);
        if (w < 0 || pos + (size_t)w >= sizeof(scores)) {
            break; /* GCOVR_EXCL_LINE */
        }
        pos += (size_t)w;
    }

    char ev[MAX_LINE];
    (void)snprintf(ev, sizeof(ev), "TURN_END|%s|%s|%s",
                   turn_end_reason_str(reason),
                   r->word,
                   scores);
    room_broadcast(s, r, ev, CONN_ID_NONE);
    /* The answer is deliberately left out of the log. */
    logger_log(&s->log, LOG_INFO, now_ms, "sid=- room=%d event=TURN_END reason=%s round=%d drawer=%s",
               r->id, turn_end_reason_str(reason), r->round, r->drawer);

    r->state = ROOM_TURN_END;
    r->phase_deadline_ms = now_ms + (int64_t)s->cfg.turn_pause_s * 1000;
    r->next_tick_ms = 0;
}

void h_start(struct Server *s, Conn *c, const Command *cmd)
{
    (void)cmd;
    Room *r = room_find(s, c->room_id);
    if (!r) {
        reply_err(s, c, ERR_NOT_IN_ROOM); /* GCOVR_EXCL_LINE */
        return;                           /* GCOVR_EXCL_LINE */
    }

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

    char picked[3][WORD_MAX];
    if (!words_pick3(&s->words, &s->rng, picked)) {
        reply_err(s, c, ERR_INTERNAL);
        return;
    }

    reply_ok(s, c, NULL);
    logger_log(&s->log, LOG_INFO, clock_now_ms(&s->clock),
               "sid=%s room=%d event=GAME_START rounds=%d nplayers=%d",
               c->session_id, r->id, r->rounds, r->nplayers);

    reset_room_to_lobby(r);
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

    if (strcmp(c->user, r->drawer) != 0) {
        reply_err(s, c, ERR_NOT_DRAWER);
        return;
    }

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

    /* Ordinal rank k counts all correct guesses in the turn (even if an earlier guesser left),
     * and turn_guess_total accumulates their points; drawer points divide by current nplayers - 1 at turn end. */
    r->guessed_count++;
    int pts = scoring_guess(r->guessed_count, remaining_s, r->draw_time);
    p->guessed = true;
    p->turn_delta = pts;
    p->score += pts;
    r->turn_guess_total += pts;

    char ok_payload[32];
    (void)snprintf(ok_payload, sizeof(ok_payload), "CORRECT|%d", pts);
    reply_ok(s, c, ok_payload);
    logger_log(&s->log, LOG_INFO, now_ms, "sid=%s room=%d event=GUESS_CORRECT user=%s points=%d",
               c->session_id, r->id, c->user, pts);

    char ev[128];
    (void)snprintf(ev, sizeof(ev), "GUESS_CORRECT|%s|%d", c->user, pts);
    room_broadcast(s, r, ev, CONN_ID_NONE);

    if (game_all_guessed(r)) {
        game_end_turn(s, r, TURN_END_ALL_GUESSED, now_ms);
    }
}

void h_scoreboard(struct Server *s, Conn *c, const Command *cmd)
{
    (void)cmd;
    Room *r = room_find(s, c->room_id);
    if (!r) {
        reply_err(s, c, ERR_NOT_IN_ROOM); /* GCOVR_EXCL_LINE */
        return;                           /* GCOVR_EXCL_LINE */
    }

    char ranking[RANKING_BUF_SIZE];
    game_format_ranking(r, ranking, sizeof(ranking));
    reply_ok(s, c, ranking);
}

void game_on_player_left(struct Server *s, Room *r, const char *left_user, bool was_drawer)
{
    (void)left_user;
    if (!s || !r || r->state == ROOM_LOBBY) {
        return;
    }

    if (r->nplayers < 2) {
        game_end_game(s, r, GAME_END_NOT_ENOUGH_PLAYERS);
        return;
    }

    if (was_drawer && (r->state == ROOM_CHOOSING || r->state == ROOM_DRAWING)) {
        game_end_turn(s, r, TURN_END_DRAWER_LEFT, clock_now_ms(&s->clock));
        return;
    }

    if (!was_drawer && r->state == ROOM_DRAWING && game_all_guessed(r)) {
        game_end_turn(s, r, TURN_END_ALL_GUESSED, clock_now_ms(&s->clock));
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
    if (now_ms >= r->phase_deadline_ms) {
        game_end_turn(s, r, TURN_END_TIMEUP, now_ms);
        return;
    }
    if (now_ms < r->next_tick_ms) {
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

static void tick_turn_end(struct Server *s, Room *r, int64_t now_ms)
{
    if (now_ms < r->phase_deadline_ms) {
        return;
    }

    if (r->nplayers < 2) {
        game_end_game(s, r, GAME_END_NOT_ENOUGH_PLAYERS);
        return;
    }

    if (!game_next_drawer(s, r)) {
        game_end_game(s, r, GAME_END_FINISHED);
        return;
    }

    char picked[3][WORD_MAX];
    if (!words_pick3(&s->words, &s->rng, picked)) {
        logger_log(&s->log, LOG_ERROR, now_ms,
                   "sid=- room=%d event=WORD_PICK_FAIL", r->id);
        game_end_game(s, r, GAME_END_FINISHED);
        return;
    }

    enter_choosing(s, r, picked, now_ms);
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
        case ROOM_TURN_END:
            tick_turn_end(s, r, now_ms);
            break;
        case ROOM_LOBBY:
        default:
            break;
        }
    }
}
