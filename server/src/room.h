#ifndef NETSKETCH_ROOM_H
#define NETSKETCH_ROOM_H

#include <stdbool.h>
#include <stdint.h>
#include "config.h"
#include "conn.h"
#include "protocol.h"

struct Server;

typedef enum {
    ROOM_LOBBY = 0,
    ROOM_CHOOSING,
    ROOM_DRAWING,
    ROOM_TURN_END
} RoomState;

typedef struct {
    ConnId conn;
    char   name[NAME_MAX_LEN + 1];
    int    score;
    bool   guessed;
    int    turn_delta;
} Player;

typedef struct {
    int       id;
    char      name[ROOM_NAME_MAX + 1];
    RoomState state;
    Player    players[MAX_PLAYERS];
    int       nplayers;
    int       max_players;
    int       rounds;
    int       draw_time;

    int       round;
    char      order[MAX_PLAYERS][NAME_MAX_LEN + 1];
    int       order_n;
    int       order_pos;
    char      drawer[NAME_MAX_LEN + 1];
    char      options[3][WORD_MAX];
    char      word[WORD_MAX];
    int       guessed_count;
    int       turn_guess_total;
    int64_t   phase_deadline_ms;
    int64_t   next_tick_ms;
} Room;

#define CONN_ID_NONE ((ConnId){ CONN_IDX_NONE, 0u })

void  room_table_init(struct Server *s);
Room *room_find(struct Server *s, int room_id);
void  room_broadcast(struct Server *s, const Room *r, const char *line, ConnId exclude);
void  room_send_info(struct Server *s, const Room *r, Conn *c);
void  room_leave(struct Server *s, Conn *c, const char *reason);

void  h_create_room(struct Server *s, Conn *c, const Command *cmd);
void  h_list_rooms(struct Server *s, Conn *c, const Command *cmd);
void  h_join_room(struct Server *s, Conn *c, const Command *cmd);
void  h_leave_room(struct Server *s, Conn *c, const Command *cmd);

#endif
