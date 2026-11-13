#include <stdio.h>
#include "net_loop.h"
#include "server.h"

/* ConnTable holds 2048 Conn slots (~35 MB), so Server must not live on the stack. */
static Server g_server;

int main(int argc, char **argv)
{
    Config cfg;

    if (!config_parse_args(&cfg, argc, argv)) {
        fprintf(stderr,
                "Usage: %s [--port N] [--data-dir DIR] [--words PATH] "
                "[--choose-time S] [--turn-pause S] [--idle-timeout S] "
                "[--max-clients N] [--verbose]\n",
                (argc > 0 && argv && argv[0]) ? argv[0] : "netsketch-server");
        return 1;
    }

    server_init(&g_server, &cfg);
    return net_loop_run(&g_server);
}
