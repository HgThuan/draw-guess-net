#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <string.h>

#include "dispatch.h"
#include "room.h"
#include "session.h"

static Server s_srv;

static void clear_out(Conn *c)
{
    conn_consume_out(c, c->out_len);
    conn_clear_dirty(&s_srv.conns);
}

static Conn *make_authed_conn(const char *username)
{
    Conn *c = conn_alloc(&s_srv.conns, -1, 1000);
    assert_non_null(c);
    char line[64];
    (void)snprintf(line, sizeof(line), "REGISTER|%s|Secret123", username);
    dispatch_line(&s_srv, c, line);
    clear_out(c);
    (void)snprintf(line, sizeof(line), "LOGIN|%s|Secret123", username);
    dispatch_line(&s_srv, c, line);
    clear_out(c);
    return c;
}

static void test_room_create_and_validation_bounds(void **state)
{
    Conn *c;
    (void)state;

    server_init(&s_srv, NULL);
    user_store_init(&s_srv.users, NULL);
    c = make_authed_conn("alice");

    /* Non-numeric parameters -> 100 */
    dispatch_line(&s_srv, c, "CREATE_ROOM|Valid|abc|3|60");
    assert_non_null(strstr(c->out, "ERR|100|"));
    clear_out(c);

    /* Invalid room names -> 205 */
    dispatch_line(&s_srv, c, "CREATE_ROOM|   |4|3|60");
    assert_non_null(strstr(c->out, "ERR|205|"));
    clear_out(c);

    dispatch_line(&s_srv, c, "CREATE_ROOM|1234567890123456789012345|4|3|60");
    assert_non_null(strstr(c->out, "ERR|205|"));
    clear_out(c);

    dispatch_line(&s_srv, c, "CREATE_ROOM|Bad@Name|4|3|60");
    assert_non_null(strstr(c->out, "ERR|205|"));
    clear_out(c);

    /* Parameter bounds: max_players (2..8), rounds (1..10), draw_time (30..180) */
    dispatch_line(&s_srv, c, "CREATE_ROOM|R|1|3|60");
    assert_non_null(strstr(c->out, "ERR|205|"));
    clear_out(c);

    dispatch_line(&s_srv, c, "CREATE_ROOM|R|9|3|60");
    assert_non_null(strstr(c->out, "ERR|205|"));
    clear_out(c);

    dispatch_line(&s_srv, c, "CREATE_ROOM|R|4|0|60");
    assert_non_null(strstr(c->out, "ERR|205|"));
    clear_out(c);

    dispatch_line(&s_srv, c, "CREATE_ROOM|R|4|11|60");
    assert_non_null(strstr(c->out, "ERR|205|"));
    clear_out(c);

    dispatch_line(&s_srv, c, "CREATE_ROOM|R|4|3|29");
    assert_non_null(strstr(c->out, "ERR|205|"));
    clear_out(c);

    dispatch_line(&s_srv, c, "CREATE_ROOM|R|4|3|181");
    assert_non_null(strstr(c->out, "ERR|205|"));
    clear_out(c);

    /* Valid CREATE_ROOM with leading/trailing spaces trimmed */
    dispatch_line(&s_srv, c, "CREATE_ROOM|  Phong Vui_1-A  |2|10|30");
    assert_string_equal(
        c->out,
        "OK|1\nROOM_INFO|1|Phong Vui_1-A|alice|2|10|30|LOBBY|alice\n"
    );
    assert_int_equal(c->room_id, 1);
    clear_out(c);

    /* Already in room -> 201 */
    dispatch_line(&s_srv, c, "CREATE_ROOM|Another|4|3|60");
    assert_non_null(strstr(c->out, "ERR|201|"));
    clear_out(c);

    /* Room limit reached -> 208 */
    c->room_id = -1;
    for (int i = 0; i < MAX_ROOMS; i++) {
        s_srv.rooms[i].id = i + 1;
    }
    dispatch_line(&s_srv, c, "CREATE_ROOM|Full|4|3|60");
    assert_non_null(strstr(c->out, "ERR|208|"));
    clear_out(c);
}

static void test_room_list_pagination(void **state)
{
    Conn *c;
    (void)state;

    server_init(&s_srv, NULL);
    user_store_init(&s_srv.users, NULL);
    c = make_authed_conn("alice");

    /* Invalid page -> 100 */
    dispatch_line(&s_srv, c, "LIST_ROOMS|abc");
    assert_non_null(strstr(c->out, "ERR|100|"));
    clear_out(c);

    dispatch_line(&s_srv, c, "LIST_ROOMS|0");
    assert_non_null(strstr(c->out, "ERR|100|"));
    clear_out(c);

    /* 0 rooms */
    dispatch_line(&s_srv, c, "LIST_ROOMS|1");
    assert_string_equal(c->out, "OK|0|1\n");
    clear_out(c);

    /* 1 room */
    dispatch_line(&s_srv, c, "CREATE_ROOM|R1|4|3|60");
    clear_out(c);
    dispatch_line(&s_srv, c, "LIST_ROOMS|1");
    assert_string_equal(c->out, "OK|1|1|1,R1,1,4,LOBBY\n");
    clear_out(c);
    dispatch_line(&s_srv, c, "LIST_ROOMS|2");
    assert_string_equal(c->out, "OK|1|2\n");
    clear_out(c);

    /* Populate up to 8, 9, and 17 rooms */
    for (int i = 2; i <= 8; i++) {
        Room *r = &s_srv.rooms[i - 1];
        r->id = i;
        (void)snprintf(r->name, sizeof(r->name), "R%d", i);
        r->nplayers = 1;
        r->max_players = 4;
        r->state = ROOM_LOBBY;
    }
    dispatch_line(&s_srv, c, "LIST_ROOMS|1");
    assert_true(strncmp(c->out, "OK|8|1|1,R1,1,4,LOBBY|", 22) == 0);
    assert_non_null(strstr(c->out, "|8,R8,1,4,LOBBY\n"));
    clear_out(c);
    dispatch_line(&s_srv, c, "LIST_ROOMS|2");
    assert_string_equal(c->out, "OK|8|2\n");
    clear_out(c);

    /* 9 rooms: page 2 has 1 room */
    s_srv.rooms[8].id = 9;
    (void)snprintf(s_srv.rooms[8].name, sizeof(s_srv.rooms[8].name), "R9");
    s_srv.rooms[8].nplayers = 2;
    s_srv.rooms[8].max_players = 6;
    s_srv.rooms[8].state = ROOM_DRAWING;

    dispatch_line(&s_srv, c, "LIST_ROOMS|2");
    assert_string_equal(c->out, "OK|9|2|9,R9,2,6,PLAYING\n");
    clear_out(c);

    /* 17 rooms: page 3 has 1 room, page 4 is empty */
    for (int i = 10; i <= 17; i++) {
        Room *r = &s_srv.rooms[i - 1];
        r->id = i;
        (void)snprintf(r->name, sizeof(r->name), "R%d", i);
        r->nplayers = 1;
        r->max_players = 4;
        r->state = ROOM_LOBBY;
    }
    dispatch_line(&s_srv, c, "LIST_ROOMS|3");
    assert_string_equal(c->out, "OK|17|3|17,R17,1,4,LOBBY\n");
    clear_out(c);
    dispatch_line(&s_srv, c, "LIST_ROOMS|4");
    assert_string_equal(c->out, "OK|17|4\n");
    clear_out(c);
}

static void test_room_join_and_events(void **state)
{
    Conn *c1;
    Conn *c2;
    Conn *c3;
    (void)state;

    server_init(&s_srv, NULL);
    user_store_init(&s_srv.users, NULL);
    c1 = make_authed_conn("alice");
    c2 = make_authed_conn("bob");
    c3 = make_authed_conn("carol");

    dispatch_line(&s_srv, c1, "CREATE_ROOM|RoomA|2|3|60");
    clear_out(c1);

    /* Bad format -> 100 */
    dispatch_line(&s_srv, c2, "JOIN_ROOM|abc");
    assert_non_null(strstr(c2->out, "ERR|100|"));
    clear_out(c2);

    /* Not found -> 200 */
    dispatch_line(&s_srv, c2, "JOIN_ROOM|999");
    assert_non_null(strstr(c2->out, "ERR|200|"));
    clear_out(c2);

    /* Room in game -> 203 */
    Room *r = room_find(&s_srv, 1);
    assert_non_null(r);
    r->state = ROOM_DRAWING;
    dispatch_line(&s_srv, c2, "JOIN_ROOM|1");
    assert_non_null(strstr(c2->out, "ERR|203|"));
    clear_out(c2);
    r->state = ROOM_LOBBY;

    /* Valid join: c2 gets OK + ROOM_INFO, c1 gets PLAYER_JOINED|bob */
    dispatch_line(&s_srv, c2, "JOIN_ROOM|1");
    assert_string_equal(
        c2->out,
        "OK|1\nROOM_INFO|1|RoomA|alice|2|3|60|LOBBY|alice,bob\n"
    );
    assert_string_equal(c1->out, "PLAYER_JOINED|bob\n");
    clear_out(c1);
    clear_out(c2);

    /* Already in room -> 201 */
    dispatch_line(&s_srv, c2, "JOIN_ROOM|1");
    assert_non_null(strstr(c2->out, "ERR|201|"));
    clear_out(c2);

    /* Room full (max_players=2) -> 202 */
    dispatch_line(&s_srv, c3, "JOIN_ROOM|1");
    assert_non_null(strstr(c3->out, "ERR|202|"));
    clear_out(c3);
}

static void test_room_leave_host_transfer_and_deletion(void **state)
{
    Conn *c1;
    Conn *c2;
    Conn *c3;
    (void)state;

    server_init(&s_srv, NULL);
    user_store_init(&s_srv.users, NULL);
    c1 = make_authed_conn("alice");
    c2 = make_authed_conn("bob");
    c3 = make_authed_conn("carol");

    /* Not in room -> 204 */
    dispatch_line(&s_srv, c1, "LEAVE_ROOM");
    assert_non_null(strstr(c1->out, "ERR|204|"));
    clear_out(c1);

    dispatch_line(&s_srv, c1, "CREATE_ROOM|RoomB|4|3|60");
    dispatch_line(&s_srv, c2, "JOIN_ROOM|1");
    dispatch_line(&s_srv, c3, "JOIN_ROOM|1");
    clear_out(c1);
    clear_out(c2);
    clear_out(c3);

    /* Host (alice) leaves -> bob becomes host, bob & carol receive PLAYER_LEFT + HOST_CHANGED */
    dispatch_line(&s_srv, c1, "LEAVE_ROOM");
    assert_string_equal(c1->out, "OK\n");
    assert_int_equal(c1->room_id, -1);
    assert_string_equal(c2->out, "PLAYER_LEFT|alice|LEAVE\nHOST_CHANGED|bob\n");
    assert_string_equal(c3->out, "PLAYER_LEFT|alice|LEAVE\nHOST_CHANGED|bob\n");
    clear_out(c1);
    clear_out(c2);
    clear_out(c3);

    /* Non-host (carol) disconnects -> bob receives PLAYER_LEFT|carol|DISCONNECT */
    session_on_disconnect(&s_srv, c3);
    assert_string_equal(c2->out, "PLAYER_LEFT|carol|DISCONNECT\n");
    clear_out(c2);

    /* Last player (bob) logs out -> room is deleted */
    dispatch_line(&s_srv, c2, "LOGOUT");
    assert_string_equal(c2->out, "OK\n");
    assert_null(room_find(&s_srv, 1));
    clear_out(c2);

    /* Null safety */
    room_table_init(NULL);
    assert_null(room_find(NULL, 1));
    assert_null(room_find(&s_srv, 0));
    room_broadcast(NULL, NULL, NULL, -1);
    room_send_info(NULL, NULL, NULL);
    room_leave(NULL, NULL, NULL);
}

int run_room_tests(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_room_create_and_validation_bounds),
        cmocka_unit_test(test_room_list_pagination),
        cmocka_unit_test(test_room_join_and_events),
        cmocka_unit_test(test_room_leave_host_transfer_and_deletion),
    };
    return cmocka_run_group_tests_name("room", tests, NULL, NULL);
}
