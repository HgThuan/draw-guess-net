#define _POSIX_C_SOURCE 200809L

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "netsketch.h"
#include "net_loop.h"

static Server s_srv;

static void test_client_lib_socketpair_send_recv(void **state)
{
    int sv[2];
    char out[64];
    (void)state;

    assert_null(ns_from_fd(-1));
    assert_null(ns_connect(NULL, 9000));
    assert_null(ns_connect("127.0.0.1", 0));
    assert_null(ns_connect("invalid.host.local.invalid", 9000));

    assert_int_equal(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    ns_conn *c = ns_from_fd(sv[0]);
    assert_non_null(c);

    /* Timeout when no data available (returns 0) */
    assert_int_equal(ns_recv_line(c, out, sizeof(out), 0), 0);

    /* ns_send_line appends '\n' */
    assert_int_equal(ns_send_line(c, "PING"), 0);
    char peer_buf[64];
    ssize_t n = recv(sv[1], peer_buf, sizeof(peer_buf) - 1, 0);
    assert_true(n == 5);
    peer_buf[n] = '\0';
    assert_string_equal(peer_buf, "PING\n");

    /* Overlong line in ns_send_line returns -1 */
    char huge[600];
    memset(huge, 'A', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = '\0';
    assert_int_equal(ns_send_line(c, huge), -1);

    /* Partial write from peer followed by completion and coalesced second line */
    assert_true(send(sv[1], "HELLO|net", 9, 0) == 9);
    assert_int_equal(ns_recv_line(c, out, sizeof(out), 5), 0);

    assert_true(send(sv[1], "sketch|1\nPONG\n", 14, 0) == 14);
    assert_int_equal(ns_recv_line(c, out, sizeof(out), 20), 1);
    assert_string_equal(out, "HELLO|netsketch|1");

    /* Second coalesced line is returned immediately from internal buffer */
    assert_int_equal(ns_recv_line(c, out, sizeof(out), 0), 1);
    assert_string_equal(out, "PONG");

    /* Buffer too small for incoming line returns < 0 */
    assert_true(send(sv[1], "LONG_LINE_HERE\n", 15, 0) == 15);
    assert_int_equal(ns_recv_line(c, out, 4, 20), -1);

    /* Peer closes socket -> returns < 0 */
    close(sv[1]);
    assert_int_equal(ns_recv_line(c, out, sizeof(out), 20), -1);

    ns_close(c);
}

static void test_client_lib_connect_to_net_loop(void **state)
{
    Config cfg;
    NetLoop nl;
    char out[128];
    (void)state;

    config_init_defaults(&cfg);
    cfg.port = 0;
    server_init(&s_srv, &cfg);
    assert_int_equal(net_loop_init(&nl, &s_srv), 0);

    ns_conn *c = ns_connect("127.0.0.1", nl.bound_port);
    assert_non_null(c);

    assert_int_equal(net_loop_step(&nl, &s_srv, 20), 0);
    assert_int_equal(ns_recv_line(c, out, sizeof(out), 50), 1);
    assert_string_equal(out, "HELLO|netsketch|1");

    assert_int_equal(ns_send_line(c, "PING"), 0);
    assert_int_equal(net_loop_step(&nl, &s_srv, 20), 0);
    assert_int_equal(ns_recv_line(c, out, sizeof(out), 50), 1);
    assert_string_equal(out, "PONG");

    ns_close(c);
    net_loop_cleanup(&nl, &s_srv);
}

int run_client_lib_tests(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_client_lib_socketpair_send_recv),
        cmocka_unit_test(test_client_lib_connect_to_net_loop),
    };
    return cmocka_run_group_tests_name("client_lib", tests, NULL, NULL);
}
