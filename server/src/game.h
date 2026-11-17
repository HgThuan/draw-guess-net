#ifndef NETSKETCH_GAME_H
#define NETSKETCH_GAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "conn.h"
#include "protocol.h"
#include "room.h"

#define RANKING_BUF_SIZE 256
#define SCORES_BUF_SIZE  256

struct Server;

typedef enum {
    TURN_END_TIMEUP = 0,
    TURN_END_ALL_GUESSED,
    TURN_END_DRAWER_LEFT
} TurnEndReason;

typedef enum {
    GAME_END_FINISHED = 0,
    GAME_END_NOT_ENOUGH_PLAYERS
} GameEndReason;

bool game_is_current_drawer(const Room *r, const char *name);
bool game_next_drawer(struct Server *s, Room *r);
bool game_all_guessed(const Room *r);
void game_format_ranking(const Room *r, char *out, size_t cap);
void game_begin_drawing(struct Server *s, Room *r, int choice_idx, int64_t now_ms);
void game_end_turn(struct Server *s, Room *r, TurnEndReason reason, int64_t now_ms);
void game_end_game(struct Server *s, Room *r, GameEndReason reason);
void game_on_player_left(struct Server *s, Room *r, const char *left_user, bool was_drawer);
void game_tick(struct Server *s, int64_t now_ms);

void h_start(struct Server *s, Conn *c, const Command *cmd);
void h_choose_word(struct Server *s, Conn *c, const Command *cmd);
void h_guess(struct Server *s, Conn *c, const Command *cmd);
void h_scoreboard(struct Server *s, Conn *c, const Command *cmd);

#endif
