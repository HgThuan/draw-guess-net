#ifndef NETSKETCH_PROTOCOL_H
#define NETSKETCH_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include "framing.h"

#define PROTO_MAX_ARGS 4

typedef enum {
    CMD_UNKNOWN = 0,
    CMD_PING,
    CMD_REGISTER,
    CMD_LOGIN,
    CMD_LOGOUT,
    CMD_CREATE_ROOM,
    CMD_LIST_ROOMS,
    CMD_JOIN_ROOM,
    CMD_LEAVE_ROOM,
    CMD_START,
    CMD_SCOREBOARD,
    CMD_CHOOSE_WORD,
    CMD_STROKE,
    CMD_CLEAR,
    CMD_GUESS,
    CMD_CHAT
} CmdType;

typedef enum {
    ERR_OK                  = 0,
    ERR_BAD_FORMAT          = 100,
    ERR_NOT_LOGGED_IN       = 101,
    ERR_ALREADY_LOGGED_IN   = 102,
    ERR_UNKNOWN_CMD         = 103,
    ERR_LINE_TOO_LONG       = 104,
    ERR_USER_EXISTS         = 110,
    ERR_BAD_CREDENTIALS     = 111,
    ERR_INVALID_USERNAME    = 112,
    ERR_INVALID_PASSWORD    = 113,
    ERR_ACCOUNT_IN_USE      = 114,
    ERR_ROOM_NOT_FOUND      = 200,
    ERR_ALREADY_IN_ROOM     = 201,
    ERR_ROOM_FULL           = 202,
    ERR_ROOM_IN_GAME        = 203,
    ERR_NOT_IN_ROOM         = 204,
    ERR_INVALID_ROOM_PARAMS = 205,
    ERR_NOT_HOST            = 206,
    ERR_NOT_ENOUGH_PLAYERS  = 207,
    ERR_ROOM_LIMIT          = 208,
    ERR_WRONG_STATE         = 300,
    ERR_NOT_DRAWER          = 301,
    ERR_IS_DRAWER           = 302,
    ERR_ALREADY_GUESSED     = 303,
    ERR_INVALID_WORD_CHOICE = 304,
    ERR_INVALID_STROKE      = 305,
    ERR_CHAT_BLOCKED        = 306,
    ERR_INTERNAL            = 900,
    ERR_SERVER_FULL         = 901
} ErrCode;

typedef struct {
    CmdType type;
    int     argc;
    char   *argv[PROTO_MAX_ARGS];
    char    buf[FRAME_MAX_LINE];
} Command;

/* Trả về ERR_OK (0) hoặc mã lỗi cú pháp (100, 103, 104). */
int protocol_parse(const char *line, Command *cmd);

/* Parse số nguyên thập phân không dấu (0..INT_MAX), từ chối dấu và khoảng trắng. */
bool protocol_parse_int(const char *s, int *out);

const char *protocol_err_msg(int code);
int protocol_format_err(char *out, size_t cap, int code);

#endif
