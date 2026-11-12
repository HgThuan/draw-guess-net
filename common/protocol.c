#include "protocol.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    CmdType     type;
    int         argc;
    bool        free_text;
} CmdDef;

static const CmdDef CMD_DEFS[] = {
    { "PING",        CMD_PING,        0, false },
    { "REGISTER",    CMD_REGISTER,    2, false },
    { "LOGIN",       CMD_LOGIN,       2, false },
    { "LOGOUT",      CMD_LOGOUT,      0, false },
    { "CREATE_ROOM", CMD_CREATE_ROOM, 4, false },
    { "LIST_ROOMS",  CMD_LIST_ROOMS,  1, false },
    { "JOIN_ROOM",   CMD_JOIN_ROOM,   1, false },
    { "LEAVE_ROOM",  CMD_LEAVE_ROOM,  0, false },
    { "START",       CMD_START,       0, false },
    { "SCOREBOARD",  CMD_SCOREBOARD,  0, false },
    { "CHOOSE_WORD", CMD_CHOOSE_WORD, 1, false },
    { "STROKE",      CMD_STROKE,      3, false },
    { "CLEAR",       CMD_CLEAR,       0, false },
    { "GUESS",       CMD_GUESS,       1, true  },
    { "CHAT",        CMD_CHAT,        1, true  },
};

static bool has_ctrl_char(const char *s)
{
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p < 0x20 || *p == 0x7F) {
            return true;
        }
    }
    return false;
}

int protocol_parse(const char *line, Command *cmd)
{
    if (!line || !cmd) {
        return ERR_BAD_FORMAT;
    }

    size_t len = strlen(line);
    if (len >= FRAME_MAX_LINE) {
        return ERR_LINE_TOO_LONG;
    }

    memcpy(cmd->buf, line, len + 1);
    cmd->type = CMD_UNKNOWN;
    cmd->argc = 0;

    char *sep = strchr(cmd->buf, '|');
    char *rest = NULL;
    if (sep) {
        *sep = '\0';
        rest = sep + 1;
    }

    const CmdDef *def = NULL;
    size_t n_defs = sizeof(CMD_DEFS) / sizeof(CMD_DEFS[0]);
    for (size_t i = 0; i < n_defs; i++) {
        if (strcmp(cmd->buf, CMD_DEFS[i].name) == 0) {
            def = &CMD_DEFS[i];
            break;
        }
    }
    if (!def) {
        return ERR_UNKNOWN_CMD;
    }

    if (rest && has_ctrl_char(rest)) {
        return ERR_BAD_FORMAT;
    }

    if (def->argc == 0) {
        if (rest != NULL) {
            return ERR_BAD_FORMAT;
        }
        cmd->type = def->type;
        return ERR_OK;
    }

    if (!rest) {
        return ERR_BAD_FORMAT;
    }

    if (def->free_text) {
        cmd->type = def->type;
        cmd->argc = 1;
        cmd->argv[0] = rest;
        return ERR_OK;
    }

    char *p = rest;
    for (int i = 0; i < def->argc - 1; i++) {
        char *next = strchr(p, '|');
        if (!next) {
            return ERR_BAD_FORMAT;
        }
        *next = '\0';
        cmd->argv[i] = p;
        p = next + 1;
    }

    if (strchr(p, '|') != NULL) {
        return ERR_BAD_FORMAT;
    }
    cmd->argv[def->argc - 1] = p;
    cmd->argc = def->argc;
    cmd->type = def->type;
    return ERR_OK;
}

bool protocol_parse_int(const char *s, int *out)
{
    if (!s || *s == '\0' || !out) {
        return false;
    }

    long val = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        val = val * 10 + (*p - '0');
        if (val > INT_MAX) {
            return false;
        }
    }
    *out = (int)val;
    return true;
}

const char *protocol_err_msg(int code)
{
    switch (code) {
    case ERR_BAD_FORMAT:          return "bad format";
    case ERR_NOT_LOGGED_IN:       return "not logged in";
    case ERR_ALREADY_LOGGED_IN:   return "already logged in";
    case ERR_UNKNOWN_CMD:         return "unknown command";
    case ERR_LINE_TOO_LONG:       return "line too long";
    case ERR_USER_EXISTS:         return "user exists";
    case ERR_BAD_CREDENTIALS:     return "bad credentials";
    case ERR_INVALID_USERNAME:    return "invalid username";
    case ERR_INVALID_PASSWORD:    return "invalid password";
    case ERR_ACCOUNT_IN_USE:      return "account in use";
    case ERR_ROOM_NOT_FOUND:      return "room not found";
    case ERR_ALREADY_IN_ROOM:     return "already in room";
    case ERR_ROOM_FULL:           return "room full";
    case ERR_ROOM_IN_GAME:        return "room in game";
    case ERR_NOT_IN_ROOM:         return "not in room";
    case ERR_INVALID_ROOM_PARAMS: return "invalid room params";
    case ERR_NOT_HOST:            return "not host";
    case ERR_NOT_ENOUGH_PLAYERS:  return "not enough players";
    case ERR_ROOM_LIMIT:          return "room limit";
    case ERR_WRONG_STATE:         return "wrong state";
    case ERR_NOT_DRAWER:          return "not drawer";
    case ERR_IS_DRAWER:           return "is drawer";
    case ERR_ALREADY_GUESSED:     return "already guessed";
    case ERR_INVALID_WORD_CHOICE: return "invalid word choice";
    case ERR_INVALID_STROKE:      return "invalid stroke";
    case ERR_CHAT_BLOCKED:        return "chat blocked";
    case ERR_SERVER_FULL:         return "server full";
    case ERR_INTERNAL:
    default:                      return "internal error";
    }
}

int protocol_format_err(char *out, size_t cap, int code)
{
    if (!out || cap == 0) {
        return -1;
    }
    int n = snprintf(out, cap, "ERR|%d|%s\n", code, protocol_err_msg(code));
    if (n < 0 || (size_t)n >= cap) {
        return -1;
    }
    return n;
}
