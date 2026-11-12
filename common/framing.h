#ifndef NETSKETCH_FRAMING_H
#define NETSKETCH_FRAMING_H

#include <stdbool.h>
#include <stddef.h>

#define FRAME_MAX_LINE 512 /* Bao gồm '\n' */

typedef enum {
    FRAME_NEED_MORE = 0,
    FRAME_OK        = 1,
    FRAME_TOO_LONG  = -1
} FrameStatus;

typedef struct {
    char   buf[FRAME_MAX_LINE];
    size_t len;
    bool   discarding;
} LineBuf;

void linebuf_init(LineBuf *lb);

FrameStatus linebuf_feed(LineBuf *lb, const char *data, size_t data_len,
                         size_t *consumed, char *out, size_t out_cap);

#endif
