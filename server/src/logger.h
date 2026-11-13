#ifndef NETSKETCH_LOGGER_H
#define NETSKETCH_LOGGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define LOG_MEM_CAP 8192

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3
} LogLevel;

typedef void (*LogSinkFn)(void *userdata, const char *line, size_t len);

typedef struct {
    char   buf[LOG_MEM_CAP];
    size_t len;
} LogMemSink;

typedef struct {
    LogLevel  min_level;
    LogSinkFn sink;
    void     *userdata;
    FILE     *fp;
    int64_t   last_flush_ms;
} Logger;

void logger_init_file(Logger *lg, FILE *fp, LogLevel min_level);
void logger_init_mem(Logger *lg, LogMemSink *mem, LogLevel min_level);
void logger_log(Logger *lg, LogLevel level, int64_t now_ms, const char *fmt, ...);
void logger_log_proto(Logger *lg, int64_t now_ms, const char *sid, const char *line);
void logger_flush(Logger *lg);
void logger_tick(Logger *lg, int64_t now_ms);
void logger_close(Logger *lg);

#endif
