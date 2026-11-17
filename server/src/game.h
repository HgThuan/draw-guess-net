#ifndef NETSKETCH_GAME_H
#define NETSKETCH_GAME_H

#include <stdbool.h>
#include <stdint.h>
#include "conn.h"
#include "protocol.h"
#include "room.h"

struct Server;

bool game_next_drawer(struct Server *s, Room *r);
bool game_all_guessed(const Room *r);
void game_begin_drawing(struct Server *s, Room *r, int choice_idx, int64_t now_ms);
void game_on_player_left(struct Server *s, Room *r, const char *left_user, bool was_drawer);
void game_tick(struct Server *s, int64_t now_ms);

void h_start(struct Server *s, Conn *c, const Command *cmd);
void h_choose_word(struct Server *s, Conn *c, const Command *cmd);
void h_guess(struct Server *s, Conn *c, const Command *cmd);

#endif
