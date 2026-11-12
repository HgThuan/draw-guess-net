#include "framing.h"
#include <string.h>

void linebuf_init(LineBuf *lb)
{
    if (!lb) {
        return;
    }
    lb->len = 0;
    lb->discarding = false;
}

FrameStatus linebuf_feed(LineBuf *lb, const char *data, size_t data_len,
                         size_t *consumed, char *out, size_t out_cap)
{
    if (!lb || !consumed || !out || out_cap == 0 || (!data && data_len > 0)) {
        if (consumed) {
            *consumed = 0;
        }
        return FRAME_TOO_LONG;
    }

    for (size_t i = 0; i < data_len; i++) {
        char c = data[i];

        if (lb->discarding) {
            if (c == '\n') {
                lb->discarding = false;
                lb->len = 0;
                *consumed = i + 1;
                out[0] = '\0';
                return FRAME_TOO_LONG;
            }
            continue;
        }

        if (c == '\n') {
            size_t n = lb->len;
            if (n > 0 && lb->buf[n - 1] == '\r') {
                n--;
            }
            lb->len = 0;
            *consumed = i + 1;
            if (n >= out_cap) {
                out[0] = '\0';
                return FRAME_TOO_LONG;
            }
            if (n > 0) {
                memcpy(out, lb->buf, n);
            }
            out[n] = '\0';
            return FRAME_OK;
        }

        if (lb->len == FRAME_MAX_LINE - 1) {
            /* Byte thứ 512 vẫn chưa phải '\n' nên cả dòng chắc chắn > 512 byte */
            lb->discarding = true;
            lb->len = 0;
        } else {
            lb->buf[lb->len++] = c;
        }
    }

    *consumed = data_len;
    return FRAME_NEED_MORE;
}
