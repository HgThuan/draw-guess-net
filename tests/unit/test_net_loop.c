#define _POSIX_C_SOURCE 200809L

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "net_loop.h"

static Server s_srv;

static int connect_local(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert_true(fd >= 0);

    int flags = fcntl(fd, F_GETFL, 0);
    assert_true(flags >= 0);
    assert_int_equal(fcntl(fd, F_SETFL, flags | O_NONBLOCK), 0);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);

    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    assert_true(rc == 0 || errno == EINPROGRESS);
    return fd;
}

static void test_net_loop_invalid_and_bind_conflict(void **state)
{
    Config cfg;
    NetLoop nl1;
    NetLoop nl2;
    (void)state;

    assert_int_equal(net_loop_init(NULL, NULL), -1);
    assert_int_equal(net_loop_step(NULL, NULL, 0), -1);
    net_loop_cleanup(NULL, NULL);
    assert_int_equal(net_loop_run(NULL), 1);

    config_init_defaults(&cfg);
    cfg.port = 0;
    server_init(&s_srv, &cfg);
    assert_int_equal(net_loop_init(&nl1, &s_srv), 0);
    assert_true(nl1.bound_port > 0);

    /* Second listen on the same bound port must fail bind() */
    s_srv.cfg.port = nl1.bound_port;
    assert_int_equal(net_loop_init(&nl2, &s_srv), -1);

    net_loop_cleanup(&nl1, &s_srv);
}

static void test_net_loop_accept_hello_and_server_full(void **state)
{
    Config cfg;
    NetLoop nl;
    char buf[128];
    ssize_t n;
    (void)state;

    config_init_defaults(&cfg);
    cfg.port = 0;
    cfg.max_clients = 2;
    server_init(&s_srv, &cfg);
    assert_int_equal(net_loop_init(&nl, &s_srv), 0);

    int c1 = connect_local(nl.bound_port);
    int c2 = connect_local(nl.bound_port);
    assert_int_equal(net_loop_step(&nl, &s_srv, 50), 0);
    assert_int_equal(s_srv.conns.active_count, 2);

    memset(buf, 0, sizeof(buf));
    n = recv(c1, buf, sizeof(buf) - 1, 0);
    assert_true(n > 0);
    assert_string_equal(buf, "HELLO|netsketch|1\n");

    memset(buf, 0, sizeof(buf));
    n = recv(c2, buf, sizeof(buf) - 1, 0);
    assert_true(n > 0);
    assert_string_equal(buf, "HELLO|netsketch|1\n");

    /* 3rd connection exceeds max_clients=2 -> ERR|901 + KICK|SERVER_FULL */
    int c3 = connect_local(nl.bound_port);
    assert_int_equal(net_loop_step(&nl, &s_srv, 50), 0);
    assert_int_equal(s_srv.conns.active_count, 2);

    memset(buf, 0, sizeof(buf));
    n = recv(c3, buf, sizeof(buf) - 1, 0);
    assert_true(n > 0);
    assert_non_null(strstr(buf, "ERR|901|"));
    assert_non_null(strstr(buf, "KICK|SERVER_FULL\n"));
    assert_int_equal(recv(c3, buf, sizeof(buf), 0), 0);
    close(c3);

    close(c1);
    close(c2);
    net_loop_cleanup(&nl, &s_srv);
}

static void test_net_loop_framing_too_long_and_reap(void **state)
{
    Config cfg;
    NetLoop nl;
    char buf[600];
    ssize_t n;
    (void)state;

    config_init_defaults(&cfg);
    cfg.port = 0;
    server_init(&s_srv, &cfg);
    assert_int_equal(net_loop_init(&nl, &s_srv), 0);

    int c1 = connect_local(nl.bound_port);
    int c2 = connect_local(nl.bound_port);
    assert_int_equal(net_loop_step(&nl, &s_srv, 50), 0);
    (void)recv(c1, buf, sizeof(buf), 0);
    (void)recv(c2, buf, sizeof(buf), 0);

    /* Split line + empty line + 513-byte overlong line */
    assert_true(send(c1, "PI", 2, 0) == 2);
    assert_int_equal(net_loop_step(&nl, &s_srv, 20), 0);
    assert_true(send(c1, "NG\n\n", 4, 0) == 4);
    assert_int_equal(net_loop_step(&nl, &s_srv, 20), 0);

    memset(buf, 'A', 512);
    buf[512] = '\n';
    assert_true(send(c1, buf, 513, 0) == 513);
    assert_int_equal(net_loop_step(&nl, &s_srv, 50), 0);

    memset(buf, 0, sizeof(buf));
    n = recv(c1, buf, sizeof(buf) - 1, 0);
    assert_true(n > 0);
    assert_non_null(strstr(buf, "ERR|104|"));
    assert_int_equal(s_srv.conns.active_count, 2);

    /* Disconnect c1 and verify reap_closed frees slot 0 */
    close(c1);
    assert_int_equal(net_loop_step(&nl, &s_srv, 50), 0);
    assert_int_equal(s_srv.conns.active_count, 1);
    assert_int_equal(s_srv.conns.conns[0].state, CONN_FREE);

    /* Trigger overflow on c2 and verify reap_closed closes it */
    s_srv.conns.conns[1].overflow = true;
    assert_int_equal(net_loop_step(&nl, &s_srv, 10), 0);
    assert_int_equal(s_srv.conns.active_count, 0);

    close(c2);
    net_loop_cleanup(&nl, &s_srv);
}

static void test_net_loop_signal_and_run_stop(void **state)
{
    Config cfg;
    NetLoop nl;
    (void)state;

    config_init_defaults(&cfg);
    cfg.port = 0;
    server_init(&s_srv, &cfg);
    assert_int_equal(net_loop_init(&nl, &s_srv), 0);
    assert_int_equal(raise(SIGTERM), 0);
    net_loop_cleanup(&nl, &s_srv);

    server_init(&s_srv, &cfg);
    s_srv.stop = true;
    assert_int_equal(net_loop_run(&s_srv), 0);
}

int run_net_loop_tests(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_net_loop_invalid_and_bind_conflict),
        cmocka_unit_test(test_net_loop_accept_hello_and_server_full),
        cmocka_unit_test(test_net_loop_framing_too_long_and_reap),
        cmocka_unit_test(test_net_loop_signal_and_run_stop),
    };
    return cmocka_run_group_tests_name("net_loop", tests, NULL, NULL);
}
