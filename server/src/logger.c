#define _POSIX_C_SOURCE 200809L

#include "logger.h"
#include <stdarg.h>
#include <string.h>
#include <time.h>

static void file_sink_fn(void *userdata, const char *line, size_t len)
{
    FILE *fp = (FILE *)userdata;
    if (fp && len > 0) {
        (void)fwrite(line, 1, len, fp);
    }
}

static void mem_sink_fn(void *userdata, const char *line, size_t len)
{
    LogMemSink *mem = (LogMemSink *)userdata;
    if (!mem || len == 0) {
        return;
    }
    size_t avail = (mem->len < LOG_MEM_CAP - 1) ? (LOG_MEM_CAP - 1 - mem->len) : 0;
    size_t copy = (len < avail) ? len : avail;
    if (copy > 0) {
        memcpy(mem->buf + mem->len, line, copy);
        mem->len += copy;
        mem->buf[mem->len] = '\0';
    }
}

static const char *level_str(LogLevel level)
{
    switch (level) {
    case LOG_DEBUG: return "DEBUG";
    case LOG_INFO:  return "INFO";
    case LOG_WARN:  return "WARN";
    case LOG_ERROR:
    default:        return "ERROR";
    }
}

static void format_iso8601_utc(int64_t now_ms, char out[64])
{
    if (now_ms < 0) {
        now_ms = 0;
    }
    time_t sec = (time_t)(now_ms / 1000);
    int ms = (int)(now_ms % 1000);
    struct tm tm_utc;
    char date_part[32];
    memset(&tm_utc, 0, sizeof(tm_utc));
    gmtime_r(&sec, &tm_utc);
    (void)strftime(date_part, sizeof(date_part), "%Y-%m-%dT%H:%M:%S", &tm_utc);
    (void)snprintf(out, 64, "%s.%03dZ", date_part, ms);
}

void logger_init_file(Logger *lg, FILE *fp, LogLevel min_level)
{
    if (!lg) {
        return;
    }
    lg->min_level = min_level;
    lg->sink = file_sink_fn;
    lg->userdata = fp;
    lg->fp = fp;
    lg->last_flush_ms = 0;
}

void logger_init_mem(Logger *lg, LogMemSink *mem, LogLevel min_level)
{
    if (!lg) {
        return;
    }
    if (mem) {
        mem->len = 0;
        mem->buf[0] = '\0';
    }
    lg->min_level = min_level;
    lg->sink = mem_sink_fn;
    lg->userdata = mem;
    lg->fp = NULL;
    lg->last_flush_ms = 0;
}

void logger_log(Logger *lg, LogLevel level, int64_t now_ms, const char *fmt, ...)
{
    char ts[64];
    char msg[512];
    char line[640];

    if (!lg || !lg->sink || !fmt || level < lg->min_level) {
        return;
    }

    format_iso8601_utc(now_ms, ts);

    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    int n = snprintf(line, sizeof(line), "%s %s %s\n", ts, level_str(level), msg);
    if (n > 0) {
        size_t len = ((size_t)n < sizeof(line)) ? (size_t)n : (sizeof(line) - 1);
        lg->sink(lg->userdata, line, len);
    }
}

void logger_log_proto(Logger *lg, int64_t now_ms, const char *sid, const char *line)
{
    char masked[256];
    const char *out_line = line;

    if (!lg || !line || lg->min_level > LOG_DEBUG) {
        return;
    }

    /* Mask password field in REGISTER|user|pass and LOGIN|user|pass */
    if (strncmp(line, "REGISTER|", 9) == 0 || strncmp(line, "LOGIN|", 6) == 0) {
        const char *first_sep = strchr(line, '|');
        const char *second_sep = first_sep ? strchr(first_sep + 1, '|') : NULL;
        if (second_sep) {
            size_t prefix_len = (size_t)(second_sep - line) + 1;
            if (prefix_len + 4 < sizeof(masked)) {
                memcpy(masked, line, prefix_len);
                memcpy(masked + prefix_len, "***", 4);
                out_line = masked;
            }
        }
    }

    logger_log(lg, LOG_DEBUG, now_ms, "sid=%s proto=%s",
               (sid && sid[0] != '\0') ? sid : "-", out_line);
}

void logger_flush(Logger *lg)
{
    if (lg && lg->fp) {
        (void)fflush(lg->fp);
    }
}

void logger_tick(Logger *lg, int64_t now_ms)
{
    if (!lg) {
        return;
    }
    if (now_ms - lg->last_flush_ms >= 1000) {
        logger_flush(lg);
        lg->last_flush_ms = now_ms;
    }
}

void logger_close(Logger *lg)
{
    if (!lg) {
        return;
    }
    logger_flush(lg);
    lg->sink = NULL;
    lg->userdata = NULL;
    lg->fp = NULL;
}
