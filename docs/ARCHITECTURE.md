# Kiến trúc netsketch

> Mô tả cách chia module, cấu trúc dữ liệu và luồng xử lý của server (C) và client.
> Giao thức xem `docs/protocol.md` (nguồn sự thật về thông điệp). Luật làm việc xem `AGENTS.md`.
> Trạng thái: bản nháp v1.0, 11/11/2026.

## 1. Nguyên tắc thiết kế

| # | Nguyên tắc | Lý do |
|---|---|---|
| 1 | **Một luồng, một vòng lặp `epoll`**, không thread, không khóa | Không có race condition, dễ debug, phù hợp 1000 kết nối |
| 2 | **Tách I/O khỏi logic.** Logic không include `<sys/socket.h>`, `<unistd.h>`, không biết `fd` là gì | Unit test logic không cần mạng, dễ đạt coverage > 90% |
| 3 | **Đầu ra của logic là bộ đệm gửi của kết nối.** Logic ghi dòng vào `conn->out`, tầng mạng gửi đi sau | Test chỉ cần đọc `conn->out`, không cần socket giả |
| 4 | **Phụ thuộc ngoài được tiêm vào:** đồng hồ, bộ sinh số ngẫu nhiên, logger, kho người dùng | Test xác định, không phụ thuộc thời gian thật |
| 5 | **Điều phối theo bảng** (`CmdSpec`): kiểm tra đăng nhập, phòng, trạng thái ván ở một chỗ duy nhất | Đảm bảo đúng thứ tự lỗi trong `protocol.md` mục 3 |
| 6 | **Không biến toàn cục** ngoài một cấu trúc `Server` truyền tường minh | Dễ khởi tạo nhiều server trong test |
| 7 | **Mã dùng chung server và client** (`framing`, `protocol`) nằm trong `common/` | Một bộ test, hai nơi dùng, không lệch định dạng |

## 2. Sơ đồ tổng quan

```
            Python UI (Tkinter)  <--ctypes-->  libnetsketch (C)  ----+
                                                                     | TCP
  loadgen.py (asyncio), nc, test client  ----------------------------+
                                                                     v
 +------------------------------ netsketch-server -------------------------------+
 |                                                                                |
 |  TẦNG MẠNG (có I/O)                                                            |
 |  net_loop: epoll_wait -> accept / read / write / close, tick định kỳ           |
 |        |  byte thô                                    ^ dòng đã đóng gói       |
 |        v                                              |                        |
 |  --------------------------- ranh giới I/O ------------------------------      |
 |  TẦNG LOGIC (thuần, không biết socket)                                         |
 |  framing -> protocol(parse) -> dispatch(CmdSpec) -> handlers                   |
 |                                    |                                           |
 |        +--------+--------+---------+--------+---------+                        |
 |        v        v        v         v        v         v                        |
 |      auth   session    room      game    scoring     chat                      |
 |        |                 |         |                                           |
 |        v                 +---------+-> words, clock, rng                        |
 |   user_store                                                                   |
 |                                                                                |
 |  logger (dùng ở mọi tầng)                                                      |
 +--------------------------------------------------------------------------------+
```

Chiều dữ liệu: `net_loop` đọc byte, đưa cho `framing` cắt thành dòng, `protocol` phân tích thành lệnh, `dispatch` kiểm tra điều kiện rồi gọi handler. Handler thay đổi trạng thái và **ghi dòng phản hồi, sự kiện vào `conn->out` của từng người nhận**. Cuối mỗi vòng lặp, `net_loop` gửi các bộ đệm đã có dữ liệu.

## 3. Cấu trúc thư mục

> Có thêm thư mục `common/` so với `AGENTS.md` mục 4. **Cần cập nhật `AGENTS.md`** (xem mục 14).

```
netsketch/
├── common/               # dùng chung server và client
│   ├── framing.[ch]      # bộ đệm ghép dòng
│   ├── protocol.[ch]     # parse lệnh, định dạng thông điệp, mã lỗi
│   └── sha256.[ch]       # băm mật khẩu (tự cài, có test vector)
├── server/src/
│   ├── main.c            # đọc tham số, khởi tạo, chạy vòng lặp (loại khỏi coverage)
│   ├── config.h          # hằng số cấu hình
│   ├── server.[ch]       # cấu trúc Server, khởi tạo, tick
│   ├── net_loop.[ch]     # epoll, accept, đọc, ghi, đóng, tín hiệu
│   ├── conn.[ch]         # bảng kết nối, ConnId, bộ đệm gửi
│   ├── dispatch.[ch]     # bảng CmdSpec, kiểm tra điều kiện, gọi handler
│   ├── auth.[ch]         # đăng ký, đăng nhập, user_store
│   ├── session.[ch]      # trạng thái phiên, hết hạn, đăng xuất, ngắt kết nối
│   ├── room.[ch]         # phòng, người chơi, vào, rời, chủ phòng
│   ├── game.[ch]         # máy trạng thái ván chơi, tick thời gian
│   ├── scoring.[ch]      # công thức điểm (hàm thuần)
│   ├── chat.[ch]         # chat, chặn lộ đáp án
│   ├── words.[ch]        # nạp và chọn từ khóa
│   ├── clock.[ch]        # giao diện đồng hồ (thật và giả)
│   ├── rng.[ch]          # giao diện số ngẫu nhiên (thật và giả)
│   └── logger.[ch]       # ghi log, có thể thay sink khi test
├── client/
│   ├── libnetsketch/     # thư viện C mỏng, tái dùng common/
│   └── ui/               # Python Tkinter
├── tools/loadgen.py
├── tests/{unit,integration}/
├── data/words.txt
└── docs/
```

## 4. Danh sách module

| Module | Trách nhiệm | Phụ thuộc | Có I/O | Cách kiểm thử |
|---|---|---|---|---|
| `framing` | Cắt dòng từ dòng byte, xử lý dòng bị cắt đôi, dính nhau, quá 512 byte | (không) | Không | Unit |
| `protocol` | Tách dòng thành `Command`, định dạng phản hồi, sự kiện, bảng mã lỗi | (không) | Không | Unit |
| `sha256` | Băm mật khẩu | (không) | Không | Unit (test vector chuẩn) |
| `clock`, `rng` | Thời gian và số ngẫu nhiên có thể giả lập | (không) | Hệ thống | Unit |
| `conn` | Bảng kết nối, cấp `ConnId`, bộ đệm gửi, danh sách kết nối cần gửi | `framing` | Không (chỉ giữ fd) | Unit |
| `auth` | `user_store`, đăng ký, đăng nhập, kiểm tra mật khẩu | `sha256`, `rng` | File `users.dat` | Unit (thư mục tạm) |
| `session` | Gắn phiên với kết nối, 1 tài khoản 1 kết nối, hết hạn, dọn dẹp khi ngắt | `auth`, `room` | Không | Unit |
| `room` | Tạo, liệt kê, vào, rời, chủ phòng, danh sách người chơi, gửi cho cả phòng | `conn` | Không | Unit |
| `game` | Máy trạng thái ván chơi, tick thời gian, chuyển lượt, kết thúc ván | `room`, `scoring`, `words`, `clock`, `rng` | Không | Unit (đồng hồ giả) |
| `scoring` | Điểm người đoán và người vẽ | (không) | Không | Unit (4 ví dụ trong `protocol.md`) |
| `chat` | Quy tắc chat, lọc đáp án, kênh riêng người đã đoán đúng | `room`, `game` | Không | Unit |
| `words` | Nạp `words.txt`, chọn 3 từ khác nhau | `rng` | Đọc file | Unit |
| `dispatch` | Bảng lệnh, kiểm tra điều kiện chung, gọi handler | tất cả handler | Không | Unit |
| `logger` | Ghi log theo mức, sink thay được | (không) | File | Unit |
| `net_loop` | `epoll`, `accept`, `recv`, `send`, đóng kết nối, tick, tín hiệu | `server`, `conn`, `dispatch` | **Có** | Integration (server thật) |

**Quy tắc phụ thuộc:** mũi tên chỉ đi từ trên xuống trong sơ đồ mục 2. Module logic không được include header của hệ thống mạng. CI nên kiểm tra bằng `grep` rằng các file `auth, session, room, game, scoring, chat, words, dispatch` không chứa `sys/socket.h`, `unistd.h`, `sys/epoll.h`.

## 5. Cấu trúc dữ liệu chính

Các kích thước lấy từ `protocol.md` mục 2 và 4. Đây là **phác thảo**, được phép chỉnh khi cài đặt nhưng phải giữ ý nghĩa.

```c
/* config.h */
#define MAX_CLIENTS      2048
#define MAX_ROOMS        512
#define MAX_PLAYERS      8
#define MAX_LINE         512      /* gồm cả '\n' */
#define IN_BUF           1024     /* chứa tối đa một dòng dở + dữ liệu mới */
#define OUT_BUF          16384    /* vượt ngưỡng này: coi là client quá chậm, đóng kết nối */
#define NAME_MAX_LEN     16
#define ROOM_NAME_MAX    24
#define WORD_MAX         32
```

```c
/* conn.h: ConnId = chỉ số + thế hệ, tránh trỏ nhầm vào kết nối đã đóng và tái dùng slot */
typedef struct { uint16_t idx; uint16_t gen; } ConnId;

typedef enum { CONN_FREE, CONN_CONNECTED, CONN_AUTHED } ConnState;

typedef struct {
    int       fd;                    /* -1 khi chạy test không có socket */
    uint16_t  gen;
    ConnState state;
    LineBuf   in;                    /* framing, dùng chung với client */
    char      out[OUT_BUF];          /* bộ đệm gửi: handler ghi vào, net_loop gửi đi */
    size_t    out_len;
    bool      closing;               /* đóng sau khi gửi xong (ví dụ sau KICK) */
    int64_t   last_active_ms;        /* để tính hết hạn 120 giây */
    char      user[NAME_MAX_LEN + 1];
    char      session_id[17];
    int       room_id;               /* -1 nếu ngoài phòng */
} Conn;
```

```c
/* room.h, game.h */
typedef enum { ROOM_LOBBY, ROOM_CHOOSING, ROOM_DRAWING, ROOM_TURN_END } RoomState;

typedef struct {
    ConnId conn;
    char   name[NAME_MAX_LEN + 1];
    int    score;
    bool   guessed;                  /* đã đoán đúng trong lượt hiện tại */
    int    turn_delta;               /* điểm nhận được trong lượt hiện tại */
} Player;

typedef struct {
    int       id;                    /* tăng dần, không tái dùng */
    char      name[ROOM_NAME_MAX + 1];
    RoomState state;
    Player    players[MAX_PLAYERS];  /* theo thứ tự vào phòng; players[0] luôn là chủ phòng */
    int       nplayers;
    int       max_players, rounds, draw_time;

    /* ván chơi */
    int       round;                 /* 1..rounds */
    char      order[MAX_PLAYERS][NAME_MAX_LEN + 1];  /* thứ tự vẽ của vòng hiện tại */
    int       order_n, order_pos;
    char      drawer[NAME_MAX_LEN + 1];
    char      options[3][WORD_MAX];
    char      word[WORD_MAX];
    int       guessed_count;         /* thứ hạng k của người đoán kế tiếp = guessed_count + 1 */

    /* thời gian */
    int64_t   phase_deadline_ms;     /* hết giờ chọn từ, hết giờ vẽ, hoặc hết nghỉ */
    int64_t   next_tick_ms;          /* lần gửi TIME kế tiếp */
} Room;
```

```c
/* auth.h */
typedef struct {
    char    name[NAME_MAX_LEN + 1];
    uint8_t salt[16];
    uint8_t hash[32];                /* SHA-256(salt || password) */
    ConnId  online;                  /* kết nối đang dùng tài khoản này, idx = 0xFFFF nếu không có */
} User;
/* user_store: bảng băm theo tên, nạp từ users.dat khi khởi động, thêm mới thì ghi nối vào file */
```

```c
/* server.h: toàn bộ trạng thái, truyền tường minh vào mọi hàm */
typedef struct Server {
    Config     cfg;
    Clock      clock;                /* now_ms() thật hoặc giả */
    Rng        rng;
    Logger    *log;
    Conn      *conns;                /* mảng MAX_CLIENTS cấp phát một lần */
    UserStore  users;
    Room       rooms[MAX_ROOMS];     /* slot có id == 0 là rỗng */
    WordList   words;
    int        dirty[MAX_CLIENTS];   /* kết nối có dữ liệu chờ gửi */
    int        ndirty;
} Server;
```

**Ước lượng bộ nhớ:** mỗi `Conn` khoảng 17 KB, 2048 kết nối khoảng 35 MB. Chấp nhận được.

**Tìm kiếm:** tìm phòng theo `id` và duyệt `LIST_ROOMS` dùng quét tuyến tính trên tối đa 512 phòng. Chi phí không đáng kể so với 1000 kết nối, chỉ tối ưu nếu đo thấy là điểm nghẽn.

## 6. Vòng lặp sự kiện (`net_loop`)

```
khởi tạo: socket, setsockopt(SO_REUSEADDR), bind, listen, epoll_create1,
          bỏ qua SIGPIPE, SIGINT/SIGTERM chỉ đặt cờ stop

while (!stop):
    timeout = min(200 ms, thời gian tới lần tick kế)
    n = epoll_wait(ep, events, N, timeout)          # EINTR: lặp lại
    now = clock.now_ms()

    với mỗi sự kiện:
        listen fd:  accept (non-blocking); hết slot -> gửi ERR|901, KICK, đóng
                    ngược lại: tạo Conn, gửi HELLO|netsketch|1
        EPOLLIN:    recv tối đa 4096 byte -> framing_feed -> mỗi dòng: protocol_parse -> dispatch
                    recv == 0 hoặc lỗi thật -> đánh dấu đóng (session_on_disconnect)
        EPOLLOUT:   gửi tiếp phần còn lại, hết thì bỏ EPOLLOUT
        EPOLLERR/HUP: đóng

    server_tick(now):      # game_tick từng phòng đang chơi, kiểm tra hết hạn 120 giây
    flush_dirty():         # mỗi kết nối có dữ liệu chờ: một lần send() gộp nhiều dòng
    reap_closed():         # đóng fd, giải phóng slot, tăng gen
```

**Quyết định chính:**

- **Level-triggered**, socket non-blocking. Đơn giản hơn edge-triggered và đủ tải. Mỗi sự kiện đọc tối đa 4096 byte để một client không chiếm hết vòng lặp.
- **Gộp ghi (write coalescing):** handler chỉ ghi vào `conn->out`, kết nối được đưa vào `dirty`. Cuối vòng lặp mới `send` một lần cho mỗi kết nối. Khi phát nét vẽ cho cả phòng, số lần gọi `send` giảm đáng kể.
- **Gửi dở:** `send` trả `EAGAIN` hoặc gửi thiếu thì giữ phần còn lại trong `out`, bật `EPOLLOUT`.
- **Client quá chậm:** `out_len` vượt `OUT_BUF` thì đóng kết nối và ghi log, không chặn cả server.
- **Đóng hoãn lại:** handler không đóng `fd` trực tiếp. Đặt `closing = true` hoặc đưa vào danh sách đóng, `reap_closed` xử lý cuối vòng lặp. Tránh dùng con trỏ đã giải phóng giữa chừng một vòng lặp.
- **Tick:** mỗi vòng lặp gọi `server_tick`. Với 512 phòng và 2048 kết nối, quét toàn bộ mỗi 200 ms là chi phí nhỏ. Độ chính xác thời gian khoảng 200 ms, đủ cho game.
- **Tắt an toàn:** `SIGINT`/`SIGTERM` đặt cờ `stop`, thoát vòng lặp, đóng kết nối, đóng log, `return` bình thường. **Bắt buộc**, vì `gcov` chỉ ghi dữ liệu coverage khi tiến trình thoát bình thường.

## 7. Luồng xử lý một lệnh

```
recv(fd) -> framing_feed -> dòng "GUESS|cat"
   -> protocol_parse    : Command{type=CMD_GUESS, argv=["cat"]}   (lỗi 100/103/104 trả ngay)
   -> dispatch          : tra CmdSpec[CMD_GUESS]
        1. đã đăng nhập?              (101/102)
        2. đang ở trong phòng?        (204/201)
        3. trạng thái phòng hợp lệ?   (300)   GUESS chỉ hợp lệ ở ROOM_DRAWING
        4. gọi handler h_guess
   -> h_guess           : người vẽ? (302)  đã đoán đúng? (303)
                          chuẩn hóa văn bản, so với room->word
                          đúng: scoring_guess(k, remaining, draw_time)
                                cập nhật Player, trả "OK|CORRECT|points"
                                gửi GUESS_CORRECT cho cả phòng
                                nếu mọi người đã đoán xong: game_end_turn(ALL_GUESSED)
                          sai:  trả "OK|WRONG", gửi GUESS_WRONG cho cả phòng
   -> cuối vòng lặp     : flush_dirty() gửi các bộ đệm
```

### Bảng điều phối `CmdSpec`

Mỗi lệnh khai báo điều kiện chung một lần. Handler chỉ kiểm tra vai trò và giá trị (bước 5 và 6 trong `protocol.md` mục 3).

| Lệnh | Số tham số | Cần đăng nhập | Cần ở trong phòng | Trạng thái phòng cho phép | Handler |
|---|---|---|---|---|---|
| `PING` | 0 | Không | Không | (mọi trạng thái) | `h_ping` |
| `REGISTER`, `LOGIN` | 2 | Phải **chưa** đăng nhập | Không | | `h_register`, `h_login` |
| `LOGOUT` | 0 | Có | Không | | `h_logout` |
| `LIST_ROOMS` | 1 | Có | Không | | `h_list_rooms` |
| `CREATE_ROOM` | 4 | Có | Phải **chưa** ở phòng | | `h_create_room` |
| `JOIN_ROOM` | 1 | Có | Phải **chưa** ở phòng | | `h_join_room` |
| `LEAVE_ROOM` | 0 | Có | Có | mọi trạng thái | `h_leave_room` |
| `START` | 0 | Có | Có | `ROOM_LOBBY` | `h_start` |
| `SCOREBOARD` | 0 | Có | Có | mọi trạng thái | `h_scoreboard` |
| `CHOOSE_WORD` | 1 | Có | Có | `ROOM_CHOOSING` | `h_choose_word` |
| `STROKE` | 3 | Có | Có | `ROOM_DRAWING` | `h_stroke` |
| `CLEAR` | 0 | Có | Có | `ROOM_DRAWING` | `h_clear` |
| `GUESS` | 1, trường cuối là văn bản tự do | Có | Có | `ROOM_DRAWING` | `h_guess` |
| `CHAT` | 1, trường cuối là văn bản tự do | Có | Có | mọi trạng thái | `h_chat` |

Chữ ký handler thống nhất: `void h_xxx(Server *s, Conn *c, const Command *cmd);`. Handler phản hồi bằng `reply_ok(c, ...)` hoặc `reply_err(c, code)`, gửi sự kiện bằng `room_broadcast(...)` hoặc `conn_send(...)`.

## 8. Máy trạng thái ván chơi (`game`)

Trạng thái và hành động khi **vào** trạng thái, được thực thi bởi `game_*` (gọi từ handler hoặc `game_tick`).

| Trạng thái | Khi vào | Hết hạn hoặc điều kiện thoát | Chuyển tới |
|---|---|---|---|
| `ROOM_LOBBY` | Reset điểm, gửi `ROOM_INFO` (sau `GAME_END`) | `START` hợp lệ | `ROOM_CHOOSING` (lượt đầu), gửi `GAME_START` |
| `ROOM_CHOOSING` | Chốt người vẽ kế, chọn 3 từ, gửi `TURN_START`, `WORD_OPTIONS` (chỉ người vẽ). Hạn: +15 giây | `CHOOSE_WORD` hợp lệ, hoặc hết hạn thì tự chọn ngẫu nhiên | `ROOM_DRAWING` |
| `ROOM_DRAWING` | Reset cờ `guessed`, `guessed_count = 0`, gửi `DRAW_START` (người vẽ thấy từ, người khác thấy gạch dưới). Hạn: +`draw_time`. Gửi `TIME` mỗi 5 giây | Hết giờ, hoặc mọi người đoán (còn trong phòng) đã đoán đúng, hoặc người vẽ rời | `ROOM_TURN_END` |
| `ROOM_TURN_END` | Tính điểm người vẽ, gửi `TURN_END`. Hạn: +5 giây | Hết hạn | `ROOM_CHOOSING` (còn lượt) hoặc `ROOM_LOBBY` sau `GAME_END` |

**Chọn người vẽ kế tiếp:** đầu mỗi vòng chụp danh sách `order` từ `players` hiện tại. Khi tới lượt, bỏ qua người không còn trong phòng. Hết `order` thì sang vòng mới (`round + 1`). Vượt `rounds` thì `GAME_END|FINISHED`.

**Sự kiện bất thường** (xem `protocol.md` mục 9), xử lý trong `room_leave` rồi gọi lại `game`:

| Tình huống | Xử lý |
|---|---|
| Người đoán rời | `PLAYER_LEFT`. Nếu mọi người đoán còn lại đã đoán đúng thì `game_end_turn(ALL_GUESSED)` |
| Người vẽ rời | `game_end_turn(DRAWER_LEFT)`, người vẽ không nhận điểm |
| Chủ phòng rời | Bỏ khỏi `players`, `players[0]` mới là chủ phòng, gửi `HOST_CHANGED` |
| Còn dưới 2 người khi đang chơi | `GAME_END|NOT_ENOUGH_PLAYERS`, về `ROOM_LOBBY` |
| Phòng rỗng | Giải phóng slot phòng |

## 9. Quản lý tài khoản và phiên

- **`users.dat`:** mỗi dòng `name:salt_hex:hash_hex`. Nạp vào bảng băm khi khởi động. `REGISTER` thêm vào bảng rồi **ghi nối** một dòng vào file. Một lần ghi nhỏ đồng bộ chấp nhận được. Khi chạy load test, **nên tạo sẵn tài khoản** để không đo nhầm chi phí ghi file.
- **Băm mật khẩu:** `SHA-256(salt || password)`, `salt` 16 byte ngẫu nhiên. **Hạn chế đã biết:** đây không phải thuật toán băm chậm chuyên dụng (như bcrypt, argon2). Chọn vậy để không phụ thuộc thư viện ngoài và không làm nghẽn một luồng khi 1000 bot đăng nhập. Ghi vào báo cáo.
- **Một tài khoản một kết nối:** trường `User.online` giữ `ConnId`. `LOGIN` thấy `online` còn hợp lệ thì trả `114`. Kiểm tra bằng `gen` nên không bị nhầm với slot được tái dùng.
- **Phiên:** gắn với kết nối. `session_id` sinh khi đăng nhập thành công (8 byte ngẫu nhiên, mã hóa hex), dùng cho log.
- **Hết hạn:** `last_active_ms` cập nhật mỗi khi nhận dữ liệu. Quá 120 giây không nhận gì thì gửi `KICK|IDLE` rồi đóng.
- **Dọn dẹp khi ngắt kết nối:** `session_on_disconnect` thực hiện lần lượt: `room_leave(reason=DISCONNECT)`, xóa `User.online`, ghi log, giải phóng `Conn`.

## 10. Logging

- Một dòng mỗi sự kiện: `2026-11-11T09:30:12.345Z INFO sid=9f3a1c2b room=1 event=GUESS_CORRECT user=carol points=400`.
- Mức: `ERROR`, `WARN`, `INFO`, `DEBUG`. Mặc định `INFO`. `DEBUG` bật bằng cờ `--verbose`, ghi từng dòng giao thức.
- **Sự kiện bắt buộc ghi** (để đạt mục "Ghi log" và phục vụ báo cáo): kết nối và ngắt, đăng ký, đăng nhập thành công và thất bại, tạo, vào, rời phòng, bắt đầu và kết thúc ván, kết thúc lượt, kick, đóng do client quá chậm, mọi lỗi `9xx`.
- **Không bao giờ ghi mật khẩu**, kể cả ở mức `DEBUG`. Khi ghi `LOGIN` và `REGISTER` ở mức `DEBUG` phải che trường mật khẩu.
- Logger nhận một `sink` (con trỏ hàm). Production ghi ra file, test ghi vào bộ nhớ để kiểm tra nội dung log.
- Ghi bằng bộ đệm của `stdio`, `fflush` mỗi giây trong `server_tick` và khi thoát. Không `fflush` từng dòng để tránh tốn syscall khi tải cao.

## 11. Kiến trúc client

```
Tkinter UI  ->  netsketch.py (ctypes)  ->  libnetsketch.so  ->  TCP
(vẽ, nhập)      (mô hình, phân tích       (kết nối, gửi dòng,
                 sự kiện)                  nhận dòng)
```

API thư viện C (mỏng, tái dùng `common/framing` và `common/protocol`):

```c
ns_conn *ns_connect(const char *host, int port);
int      ns_send_line(ns_conn *c, const char *line);              /* thêm '\n', 0 nếu OK */
int      ns_recv_line(ns_conn *c, char *out, size_t cap, int timeout_ms);
                                    /* 1: có một dòng, 0: hết thời gian, <0: lỗi hoặc đóng */
void     ns_close(ns_conn *c);
```

- **UI một luồng:** `root.after(20, poll)` gọi `ns_recv_line(timeout_ms=0)` lặp cho đến khi không còn dòng, mỗi dòng được phân tích và cập nhật giao diện. Không dùng thread nên không có lỗi đồng bộ với Tkinter.
- **Phân biệt phản hồi và sự kiện:** dòng bắt đầu bằng `OK`, `ERR`, `PONG` là phản hồi của lệnh gửi trước đó (khớp theo thứ tự hàng đợi lệnh đang chờ). Dòng khác là sự kiện.
- **Gửi nét vẽ:** gom các điểm chuột trong khoảng 30 ms hoặc tối đa 40 điểm thành một `STROKE`. Tọa độ chuẩn hóa về 0 đến 1000 trước khi gửi.
- **Logic game không nằm trong UI.** UI chỉ hiển thị và gửi lệnh, đúng quy định trong `AGENTS.md`.
- **Công cụ test và load test** (`tools/loadgen.py`) nói chuyện trực tiếp qua `asyncio`, không cần `libnetsketch`.

## 12. Kiểm thử gắn với kiến trúc

| Điểm tiêm (seam) | Dùng trong test để |
|---|---|
| `Clock` giả | Điều khiển hết giờ chọn từ, hết giờ vẽ, hết hạn 120 giây mà không chờ thật |
| `Rng` giả | Cố định từ khóa, `session_id`, `salt` để so sánh kết quả chính xác |
| `conn->out` | Đọc toàn bộ phản hồi và sự kiện mà không cần socket (tạo `Conn` với `fd = -1`) |
| Logger `sink` | Kiểm tra sự kiện được ghi log và mật khẩu không bị lộ |
| Thư mục tạm cho `users.dat` | Kiểm tra lưu và nạp tài khoản |

**Cách đạt coverage > 90%:**

- Logic (`auth, session, room, game, scoring, chat, words, dispatch, framing, protocol`) kiểm thử bằng unit test với các điểm tiêm ở trên.
- `net_loop.c` kiểm thử qua **integration test**: `make coverage` biên dịch server với `--coverage`, test Python khởi chạy server thật, chạy kịch bản rồi gửi `SIGTERM`. Server thoát bình thường nên `gcov` ghi được dữ liệu, `gcovr` gộp lại cùng kết quả unit test.
- `main.c` loại khỏi báo cáo.
- Từ tuần 2, `make coverage` trong CI phải đạt ngưỡng (xem `TESTING.md`).

## 13. Mục tiêu hiệu năng (cần kiểm chứng bằng load test)

Đây là **giả thuyết thiết kế**, chưa phải kết quả đo.

| Chỉ số | Mục tiêu ban đầu |
|---|---|
| Số kết nối đồng thời ổn định | 1000 |
| Độ trễ chuyển tiếp nét vẽ (p95) | dưới 100 ms ở 500 client |
| Tỉ lệ lỗi giao thức | 0% ở mức tải nhẹ và vừa |
| RAM ở 1000 kết nối | dưới 100 MB |
| Rò rỉ bộ nhớ | 0 (ASan sạch) |

**Ước lượng tải để so sánh với kết quả đo:** 1000 client xấp xỉ 125 phòng 8 người. Nếu mỗi người vẽ gửi 30 `STROKE` mỗi giây thì có khoảng 3750 dòng vào mỗi giây, mỗi dòng phát cho 7 người, tức khoảng 26000 dòng ra mỗi giây. Nhờ gộp ghi, số lần gọi `send` thấp hơn nhiều con số này.

**Giới hạn chống lạm dụng:** dòng tối đa 512 byte, `OUT_BUF` 16 KB, hết hạn 120 giây, tối đa 2048 kết nối. Giới hạn tốc độ lệnh trên mỗi kết nối chưa có trong v1 (xem mục 14).

## 14. Quyết định mở và việc cần cập nhật

**Cần cập nhật các tài liệu khác:**

- `AGENTS.md` mục 4: thêm `common/` (`framing`, `protocol`, `sha256`) và liệt kê thêm `conn, dispatch, server, clock, rng, words` trong `server/src/`.
- `AGENTS.md` mục 2: ghi rõ băm mật khẩu bằng `common/sha256` tự cài (không dùng OpenSSL), nếu bạn đồng ý với mục 9.

**Ghi vào `DECISIONS.md`:**

| Quyết định | Lý do | Đánh đổi |
|---|---|---|
| `epoll` level-triggered một luồng | Đơn giản, không khóa | Dùng một lõi CPU |
| Gộp ghi cuối mỗi vòng lặp | Giảm syscall khi phát nét vẽ | Thêm độ trễ tối đa một vòng lặp (dưới 200 ms, thường rất nhỏ) |
| `ConnId` có thế hệ | Tránh nhầm kết nối khi slot được tái dùng | Thêm một trường |
| `players[0]` là chủ phòng | Chuyển chủ phòng không cần trường riêng | Phải giữ đúng thứ tự khi xóa |
| SHA-256 có salt, tự cài | Không phụ thuộc thư viện, không nghẽn khi login hàng loạt | Yếu hơn bcrypt hoặc argon2 trước tấn công dò mật khẩu |
| Ghi nối `users.dat` đồng bộ | Đơn giản | Chặn vòng lặp một chút khi đăng ký |
| `common/` dùng chung server và client | Một bộ test, hai nơi dùng | Client phụ thuộc mã nguồn chung |

**Chưa quyết (làm sau 14/12 nếu còn thời gian):**

- Giới hạn tốc độ lệnh mỗi kết nối (ví dụ quá 200 dòng mỗi giây thì đóng).
- Chuyển danh sách phòng sang bảng băm nếu đo thấy quét tuyến tính là nút thắt.
- Nối lại phiên sau khi mất kết nối (v1 không hỗ trợ).
