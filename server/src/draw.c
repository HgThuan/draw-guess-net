#include "draw.h"
#include "dispatch.h"
#include "room.h"
#include "server.h"
#include <stdio.h>
#include <string.h>

static bool is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

int stroke_validate(const char *color, int width, const char *points)
{
    if (!color || !points || width < 1 || width > 20) {
        return ERR_INVALID_STROKE;
    }

    if (strlen(color) != 6) {
        return ERR_INVALID_STROKE;
    }
    for (int i = 0; i < 6; i++) {
        if (!is_hex_digit(color[i])) {
            return ERR_INVALID_STROKE;
        }
    }

    if (points[0] == '\0') {
        return ERR_INVALID_STROKE;
    }

    int ncoords = 0;
    const char *p = points;
    for (;;) {
        if (*p < '0' || *p > '9') {
            return ERR_INVALID_STROKE;
        }

        int val = 0;
        while (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            if (val > 1000) {
                return ERR_INVALID_STROKE;
            }
            p++;
        }

        ncoords++;
        if (ncoords > 80) {
            return ERR_INVALID_STROKE;
        }

        if (*p == '\0') {
            break;
        }
        if (*p != ',') {
            return ERR_INVALID_STROKE;
        }
        p++;
    }

    if (ncoords < 4 || (ncoords % 2) != 0) {
        return ERR_INVALID_STROKE;
    }
    return ERR_OK;
}

void h_stroke(struct Server *s, Conn *c, const Command *cmd)
{
    Room *r = room_find(s, c->room_id);
    if (!r) {
        reply_err(s, c, ERR_NOT_IN_ROOM); /* GCOVR_EXCL_LINE */
        return;                           /* GCOVR_EXCL_LINE */
    }

    /* Step 5: Role check (only current drawer) */
    if (strcmp(c->user, r->drawer) != 0) {
        reply_err(s, c, ERR_NOT_DRAWER);
        return;
    }

    /* Step 6: Validate color, width, and points */
    int width = 0;
    (void)protocol_parse_int(cmd->argv[1], &width);
    int rc = stroke_validate(cmd->argv[0], width, cmd->argv[2]);
    if (rc != ERR_OK) {
        reply_err(s, c, rc);
        return;
    }

    /* Rebuild clean line from parsed parameters and forward to everyone except drawer */
    char ev[MAX_LINE];
    (void)snprintf(ev, sizeof(ev), "STROKE|%s|%d|%s",
                   cmd->argv[0], width, cmd->argv[2]);
    room_broadcast(s, r, ev, conn_id(c));
}

void h_clear(struct Server *s, Conn *c, const Command *cmd)
{
    (void)cmd;
    Room *r = room_find(s, c->room_id);
    if (!r) {
        reply_err(s, c, ERR_NOT_IN_ROOM); /* GCOVR_EXCL_LINE */
        return;                           /* GCOVR_EXCL_LINE */
    }

    if (strcmp(c->user, r->drawer) != 0) {
        reply_err(s, c, ERR_NOT_DRAWER);
        return;
    }

    room_broadcast(s, r, "CLEAR", conn_id(c));
}
