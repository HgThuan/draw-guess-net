#ifndef NETSKETCH_CONFIG_H
#define NETSKETCH_CONFIG_H

#include <stdbool.h>

#define MAX_CLIENTS   2048
#define MAX_ROOMS     512
#define MAX_PLAYERS   8
#define MAX_LINE      512
#define IN_BUF        1024
#define OUT_BUF       16384
#define NAME_MAX_LEN  16
#define ROOM_NAME_MAX 24
#define WORD_MAX      32

#define DEFAULT_PORT         9000
#define DEFAULT_CHOOSE_TIME  15
#define DEFAULT_TURN_PAUSE   5
#define DEFAULT_TIME_TICK    5
#define DEFAULT_IDLE_TIMEOUT 120

typedef struct {
    int         port;
    const char *data_dir;
    const char *words_path;
    int         choose_time_s;
    int         turn_pause_s;
    int         idle_timeout_s;
    int         max_clients;
    bool        verbose;
} Config;

void config_init_defaults(Config *cfg);
bool config_parse_args(Config *cfg, int argc, char **argv);

#endif
