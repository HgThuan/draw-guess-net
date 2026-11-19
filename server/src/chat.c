#include "chat.h"
#include "dispatch.h"
#include "game.h"
#include "room.h"
#include "server.h"
#include "words.h"
#include <stdio.h>
#include <string.h>

/* Players who already know the answer talk only among themselves and the drawer,
 * so their messages cannot hint the answer to people still guessing. */
static void send_to_solved_channel(struct Server *s, const Room *r, const char *line)
{
    for (int i = 0; i < r->nplayers; i++) {
        const Player *p = &r->players[i];
        if (!p->guessed && strcmp(p->name, r->drawer) != 0) {
            continue;
        }
        Conn *pc = conn_get(&s->conns, p->conn);
        if (pc && !pc->closing) {
            conn_send_line(&s->conns, pc, line);
        }
    }
}

void h_chat(struct Server *s, Conn *c, const Command *cmd)
{
    Room *r = room_find(s, c->room_id);
    if (!r) {
        reply_err(s, c, ERR_NOT_IN_ROOM); /* GCOVR_EXCL_LINE */
        return;                           /* GCOVR_EXCL_LINE */
    }

    const char *text = cmd->argv[0];
    bool solved = false;
    if (r->state == ROOM_DRAWING) {
        if (!game_is_current_drawer(r, c->user)) {
            for (int i = 0; i < r->nplayers; i++) {
                if (strcmp(r->players[i].name, c->user) == 0) {
                    solved = r->players[i].guessed;
                    break;
                }
            }
        }
        /* Both drawer and unguessed players are blocked from leaking the answer in chat. */
        if (!solved && words_contains(text, r->word)) {
            reply_err(s, c, ERR_CHAT_BLOCKED);
            return;
        }
    }

    char ev[MAX_LINE];
    (void)snprintf(ev, sizeof(ev), "CHAT|%s|%s", c->user, text);
    reply_ok(s, c, NULL);
    if (solved) {
        send_to_solved_channel(s, r, ev);
    } else {
        room_broadcast(s, r, ev, CONN_ID_NONE);
    }
}
