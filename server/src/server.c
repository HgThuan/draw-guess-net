#include "server.h"
#include "protocol.h"
#include <string.h>

void config_init_defaults(Config *cfg)
{
    if (!cfg) {
        return;
    }
    cfg->port = DEFAULT_PORT;
    cfg->data_dir = ".";
    cfg->words_path = "data/words.txt";
    cfg->choose_time_s = DEFAULT_CHOOSE_TIME;
    cfg->turn_pause_s = DEFAULT_TURN_PAUSE;
    cfg->idle_timeout_s = DEFAULT_IDLE_TIMEOUT;
    cfg->max_clients = MAX_CLIENTS;
    cfg->verbose = false;
}

bool config_parse_args(Config *cfg, int argc, char **argv)
{
    int i;
    int val;

    if (!cfg || argc < 1 || !argv) {
        return false;
    }

    config_init_defaults(cfg);

    for (i = 1; i < argc; i++) {
        if (!argv[i]) {
            return false;
        }
        if (strcmp(argv[i], "--verbose") == 0) {
            cfg->verbose = true;
            continue;
        }

        if (i + 1 >= argc || !argv[i + 1]) {
            return false;
        }

        if (strcmp(argv[i], "--port") == 0) {
            if (!protocol_parse_int(argv[++i], &val) || val < 1 || val > 65535) {
                return false;
            }
            cfg->port = val;
        } else if (strcmp(argv[i], "--data-dir") == 0) {
            if (argv[++i][0] == '\0') {
                return false;
            }
            cfg->data_dir = argv[i];
        } else if (strcmp(argv[i], "--words") == 0) {
            if (argv[++i][0] == '\0') {
                return false;
            }
            cfg->words_path = argv[i];
        } else if (strcmp(argv[i], "--choose-time") == 0) {
            if (!protocol_parse_int(argv[++i], &val) || val < 1) {
                return false;
            }
            cfg->choose_time_s = val;
        } else if (strcmp(argv[i], "--turn-pause") == 0) {
            if (!protocol_parse_int(argv[++i], &val) || val < 1) {
                return false;
            }
            cfg->turn_pause_s = val;
        } else if (strcmp(argv[i], "--idle-timeout") == 0) {
            if (!protocol_parse_int(argv[++i], &val) || val < 1) {
                return false;
            }
            cfg->idle_timeout_s = val;
        } else if (strcmp(argv[i], "--max-clients") == 0) {
            if (!protocol_parse_int(argv[++i], &val) || val < 1 || val > MAX_CLIENTS) {
                return false;
            }
            cfg->max_clients = val;
        } else {
            return false;
        }
    }

    return true;
}

void server_init(Server *s, const Config *cfg)
{
    if (!s) {
        return;
    }
    if (cfg) {
        s->cfg = *cfg;
    } else {
        config_init_defaults(&s->cfg);
    }
    clock_init_real(&s->clock);
    rng_init_real(&s->rng);
    conn_table_init(&s->conns, s->cfg.max_clients);
    s->stop = false;
}
