# Chiến lược kiểm thử netsketch

> Đáp ứng yêu cầu của đề: **đầy đủ unit test (coverage > 90%)** và **bài test khả năng chịu tải**.
> Tài liệu liên quan: `AGENTS.md` (luật), `docs/protocol.md` (hợp đồng giao thức), `docs/ARCHITECTURE.md` (các điểm tiêm), `docs/DECISIONS.md` (D-005, D-020, D-021).
> Trạng thái: bản nháp v1.0, 11/11/2026.

## 1. Mục tiêu và các tầng kiểm thử

| Tầng | Công cụ | Chạy bằng | Mục đích | Khi nào chạy |
|---|---|---|---|---|
| **Unit** | cmocka | `make test` | Kiểm tra logic từng module, không socket, không thời gian thật | Mỗi lần sửa code, mỗi lần push |
| **Integration** | pytest + server thật | `make itest` | Kiểm tra cả hệ thống qua TCP thật: ghép dòng, `epoll`, ván chơi đầy đủ, tắt an toàn | Mỗi push |
| **Coverage** | gcov + gcovr | `make coverage` | Đo và chặn nếu dưới 90% | Mỗi push (CI chặn) |
| **Bộ nhớ** | ASan, UBSan, Valgrind | `make asan` | Không rò rỉ, không tràn bộ đệm, không hành vi không xác định | Mỗi push (ASan), hằng tuần (Valgrind) |
| **Chịu tải** | `tools/loadgen.py` | `make loadtest` | Đo độ trễ, thông lượng, lỗi, tài nguyên ở 100, 500, 1000 client | Thủ công, bản chính thức ở tuần 6 |
| **Bền vững** | Script test lỗi đầu vào | `make itest` (nhóm RB) | Server không sập khi gặp dữ liệu rác | Mỗi push |

Gộp lại, `make check` chạy unit + integration + kiểm tra quy ước. Nó là lệnh cần xanh trước mỗi lần commit.

> **Cần cập nhật `AGENTS.md` mục 3:** thêm các target `itest`, `check`, `loadtest` (xem mục 12).

## 2. Nguyên tắc chung

1. **Xác định:** test không dùng `sleep`, không đọc thời gian thật, không phụ thuộc thứ tự chạy. Thời gian đi qua `Clock` giả, ngẫu nhiên đi qua `Rng` giả (xem `ARCHITECTURE.md` mục 12).
2. **Độc lập:** mỗi test tự dựng môi trường (`setup`) và dọn dẹp (`teardown`).
3. **Test hành vi, không test cài đặt:** kiểm tra mã lỗi và dòng giao thức theo `protocol.md`, không kiểm tra biến nội bộ khi có thể tránh.
4. **So sánh mã lỗi, không so sánh câu mô tả:** dùng `assert_err(out, 111)` (kiểm tra tiền tố `ERR|111|`) để test không gãy khi đổi câu chữ.
5. **Viết test cùng lúc với code,** không để dồn cuối tuần (xem `AGENTS.md` mục 8).
6. **Lỗi được phát hiện thì viết test tái hiện trước khi sửa** (mục 11).
7. **Không sửa test cho qua** khi code sai. Nếu test sai thật, ghi lý do trong commit.
8. **Test không được phụ thuộc mạng ngoài.** Chỉ dùng `127.0.0.1` và `socketpair()`.

## 3. Lệnh và cấu hình

```bash
make test       # unit test (cmocka), build thường
make itest      # integration test: dựng server rồi chạy pytest
make check      # test + itest + kiểm tra quy ước
make coverage   # unit + integration với --coverage, rồi gcovr, chặn nếu < 90% dòng
make asan       # build với ASan + UBSan, chạy unit + integration
make loadtest SCENARIO=lt2     # chạy một kịch bản tải
```

**Biên dịch:**

| Bản build | Cờ | Dùng cho |
|---|---|---|
| Thường | `-std=c99 -O2 -Wall -Wextra -Werror` | Chạy hằng ngày, load test |
| Coverage | `-O0 -g --coverage` | `make coverage` (thư mục `build/cov/`) |
| ASan | `-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer` | `make asan` (thư mục `build/asan/`) |

Load test **luôn dùng bản build thường** (`-O2`), không dùng bản coverage hay ASan vì chậm hơn nhiều và làm sai số liệu.

## 4. Coverage

### 4.1 Quy định

| Mục | Giá trị |
|---|---|
| Chỉ số chặn | **Coverage dòng tổng thể >= 90%** |
| Loại khỏi tính toán | `server/src/main.c`, `tests/`, `build/`, `tools/` |
| Chỉ số chỉ báo cáo (không chặn) | Coverage nhánh |
| Khuyến nghị theo từng file | Mỗi file logic >= 80%, không để một file yếu bị che khuất bởi trung bình |
| Từ khi nào chặn | Từ tuần 2 (tuần 1 chỉ báo cáo) |

Việc loại `main.c` chỉ hợp lý vì file đó chỉ đọc tham số rồi gọi hàm khởi tạo. **Không thêm file nào khác vào danh sách loại** nếu không ghi lý do trong `DECISIONS.md`. `net_loop.c` **không** được loại (D-021).

### 4.2 Lệnh đo

```bash
find build/cov -name '*.gcda' -delete          # xóa số liệu cũ
make -C build/cov unit-tests && ./build/cov/unit-tests
pytest tests/integration --server build/cov/netsketch-server
gcovr -r . \
  --exclude 'tests/.*' --exclude 'tools/.*' --exclude 'build/.*' \
  --exclude 'server/src/main.c' \
  --print-summary --fail-under-line 90 \
  --html-details build/coverage/index.html --xml build/coverage.xml
```

### 4.3 Cách đạt coverage cho phần mạng

- Server dưới test **phải thoát bình thường** (nhận `SIGTERM`, thoát vòng lặp, `return`), vì `gcov` chỉ ghi dữ liệu khi tiến trình thoát bình thường. Fixture của integration test kiểm tra `returncode == 0`.
- Các nhánh lỗi hiếm trong `net_loop.c` (`accept` thất bại, `send` trả `EAGAIN`, client quá chậm) được phủ bằng các test nhóm RB ở mục 7.
- Nếu một dòng thật sự không thể kích hoạt (ví dụ `EINTR` từ `epoll_wait`), dùng macro đánh dấu ngoại lệ của `gcovr` (`GCOVR_EXCL_LINE`) và ghi lý do ngay cạnh. Chỉ dùng cho từng dòng riêng lẻ, không dùng cho cả khối.

## 5. Unit test

### 5.1 Quy ước

| Mục | Quy ước |
|---|---|
| Vị trí và tên file | `tests/unit/test_<module>.c` |
| Tên test | `test_<module>_<hành_vi>_<kết_quả>`, ví dụ `test_auth_login_wrong_password_returns_111` |
| Cấu trúc | Chuẩn bị, thực hiện, kiểm tra (một hành vi mỗi test) |
| Dữ liệu | Hằng số đặt tên rõ ràng, không "số ma thuật" |
| Thư mục tạm | `mkdtemp` cho `users.dat`, xóa ở `teardown` |
| Từ khóa test | `tests/data/words_small.txt`, vài từ cố định |

### 5.2 Bộ trợ giúp dùng chung (`tests/unit/helpers.h`)

Phác thảo, cho phép viết test không cần socket:

```c
typedef struct TestEnv TestEnv;

TestEnv    *env_new(void);                 /* Server không socket, Clock giả = 0, Rng xác định */
void        env_free(TestEnv *e);
Conn       *env_conn(TestEnv *e);          /* thêm một Conn với fd = -1 */
void        env_login(TestEnv *e, Conn *c, const char *user);   /* đăng ký + đăng nhập, xóa out */
void        env_send(TestEnv *e, Conn *c, const char *line);    /* thêm '\n', framing_feed + dispatch */
const char *env_out(Conn *c);              /* trả dữ liệu đã ghi vào out, rồi xóa */
void        env_advance(TestEnv *e, int64_t ms);   /* tăng đồng hồ giả + server_tick */
const char *env_log(TestEnv *e);           /* nội dung log đã ghi vào sink bộ nhớ */

#define assert_err(out, code)  /* kiểm tra out bắt đầu bằng "ERR|<code>|" */
#define assert_ok(out)        /* kiểm tra out bắt đầu bằng "OK" */
```

### 5.3 Ví dụ

```c
static void test_scoring_first_guess_at_45s_returns_400(void **state) {
    (void)state;
    assert_int_equal(scoring_guess(1, 45, 60), 400);
}

static void test_auth_login_wrong_password_returns_111(void **state) {
    TestEnv *e = *state;
    Conn *c = env_conn(e);
    env_send(e, c, "REGISTER|alice|secret1");
    assert_ok(env_out(c));
    env_send(e, c, "LOGIN|alice|wrong");
    assert_err(env_out(c), 111);
}

static void test_game_choosing_timeout_picks_random_word(void **state) {
    TestEnv *e = *state;
    /* dựng phòng 2 người, START, không gửi CHOOSE_WORD */
    env_advance(e, 15000);   /* hết giờ chọn từ (Clock giả) */
    /* mong đợi: DRAW_START đã được gửi cho cả hai người */
}
```

## 6. Ma trận test theo module

Mỗi hàng là **tối thiểu** các trường hợp phải có. Thêm trường hợp biên khi thấy cần.

| Module | Trường hợp bắt buộc |
|---|---|
| `framing` | Một dòng hoàn chỉnh; dòng bị cắt đôi qua hai lần `feed`; nhiều dòng trong một lần `feed`; dòng đúng 512 byte; dòng 513 byte (báo lỗi, bỏ đến `\n`, dòng sau vẫn đọc được); `\r\n`; dòng rỗng; `feed` từng byte một; dữ liệu cuối không có `\n` (chưa trả dòng) |
| `protocol` | Tách đúng từng lệnh trong `protocol.md`; thiếu và thừa tham số (`100`); lệnh lạ (`103`); trường văn bản tự do chứa `\|`; tên lệnh sai chữ hoa chữ thường; định dạng từng sự kiện khớp ví dụ trong `protocol.md`; kiểm tra giá trị số (chữ cái, dấu, số rất lớn) |
| `sha256` | Test vector chuẩn (chuỗi rỗng, `"abc"`, chuỗi dài hơn một khối); cùng mật khẩu khác salt cho hash khác |
| `scoring` | **Bốn ví dụ trong `protocol.md` mục 8**; điểm chạm sàn 50; `remaining = 0`; `remaining = draw_time`; người vẽ khi không ai đoán đúng (0); người vẽ với phòng 2 người; chia nguyên làm tròn xuống |
| `auth` | Đăng ký mới; trùng tên (`110`); tên và mật khẩu ngoài ràng buộc (`112`, `113`) ở các giá trị biên (2 và 3, 16 và 17 ký tự); đăng nhập đúng và sai; hai kết nối cùng tài khoản (`114`); nạp lại `users.dat` sau khi khởi động lại; file hỏng hoặc rỗng; mật khẩu không xuất hiện thô trong file |
| `session` | `LOGOUT`; hết hạn 120 giây bằng đồng hồ giả (nhận `KICK\|IDLE`); `PING` làm mới hạn; ngắt kết nối khi đang trong phòng thì dọn dẹp sạch; sau ngắt, tài khoản đăng nhập lại được |
| `room` | Tạo với tham số trong và ngoài phạm vi (`205`); `LIST_ROOMS` phân trang (0, 1, 8, 9, 17 phòng); vào phòng đầy (`202`), phòng đang chơi (`203`), không tồn tại (`200`); đã ở phòng (`201`); rời phòng; chủ phòng rời thì chuyển chủ (`HOST_CHANGED`); phòng rỗng bị xóa; đạt giới hạn phòng (`208`); `PLAYER_JOINED`, `PLAYER_LEFT` đến đúng người |
| `game` | `START` bởi người không phải chủ (`206`), thiếu người (`207`); luân phiên đúng thứ tự vẽ; hết giờ chọn từ thì tự chọn; `CHOOSE_WORD` sai chỉ số (`304`); `TIME` mỗi 5 giây; kết thúc lượt bởi `TIMEUP`, `ALL_GUESSED`, `DRAWER_LEFT`; kết thúc ván `FINISHED` và `NOT_ENOUGH_PLAYERS`; người đoán rời giữa lượt; người rời được bỏ qua ở vòng sau; sau `GAME_END` phòng về sảnh và điểm reset; `WORD_OPTIONS` chỉ đến người vẽ; `DRAW_START` hiển thị từ khóa cho người vẽ và gạch dưới cho người khác (kể cả từ có dấu cách) |
| `chat` | Chat ở sảnh; người vẽ chat (`302`); nội dung chứa từ khóa (`306`), kể cả lẫn trong câu và khác hoa thường; người đã đoán đúng chỉ nhắn được với người vẽ và nhóm đã đoán đúng; chat sau `TURN_END` đến cả phòng |
| `words` | Nạp file; chọn 3 từ khác nhau; file có ít hơn 3 từ; dòng trống và khoảng trắng thừa; chuẩn hóa so khớp (hoa thường, khoảng trắng) |
| `dispatch` | **Test theo bảng** quét mọi lệnh x mọi trạng thái, đối chiếu với ma trận `protocol.md` mục 5.3 (xem 6.1); đúng thứ tự kiểm tra lỗi (ví dụ lệnh lạ khi chưa đăng nhập trả `103`, không phải `101`) |
| `conn` | Cấp và giải phóng slot; `ConnId` cũ không còn hợp lệ sau khi slot được tái dùng; `out` đầy thì báo; gộp nhiều dòng vào một lần gửi |
| `logger` | Ghi đúng định dạng và mức; lọc theo mức; **mật khẩu không bao giờ xuất hiện** kể cả ở `DEBUG`; sink bộ nhớ |

### 6.1 Test theo bảng cho ma trận lệnh (đề xuất cách viết)

Một test duy nhất duyệt qua mảng các bộ `{lệnh, trạng thái, kết quả mong đợi}` lấy từ `protocol.md` mục 5.3. Nhờ vậy khi đổi giao thức, chỉ cần sửa bảng và test tự cập nhật. Các trạng thái cần dựng: chưa đăng nhập, đã đăng nhập ngoài phòng, trong phòng ở sảnh, trong phòng đang chọn từ, đang vẽ (cho cả người vẽ và người đoán).

### 6.2 Checklist theo lệnh

Theo `AGENTS.md`, **mỗi lệnh có ít nhất một test đúng và một test sai**. Đánh dấu khi xong:

| Lệnh | Đúng | Sai (mã lỗi tiêu biểu) |
|---|---|---|
| `REGISTER` | `OK` | `110`, `112`, `113` |
| `LOGIN` | `OK\|session_id` | `111`, `114`, `102` |
| `LOGOUT` | `OK` | `101` |
| `PING` | `PONG` | (không có lỗi nghiệp vụ, test dòng sai định dạng `100`) |
| `CREATE_ROOM` | `OK\|room_id` + `ROOM_INFO` | `205`, `201`, `208` |
| `LIST_ROOMS` | `OK\|total\|page\|...` | `100` (page không phải số) |
| `JOIN_ROOM` | `OK\|room_id` + `ROOM_INFO` | `200`, `202`, `203`, `201` |
| `LEAVE_ROOM` | `OK` | `204` |
| `START` | `OK` + `GAME_START` | `206`, `207`, `300` |
| `SCOREBOARD` | `OK\|...` | `204` |
| `CHOOSE_WORD` | `OK` | `301`, `304`, `300` |
| `STROKE` | (không phản hồi) + chuyển tiếp | `305` (số lẻ, ngoài 0..1000, quá 40 điểm, màu sai, `width` ngoài 1..20), `301`, `300` |
| `CLEAR` | (không phản hồi) + chuyển tiếp | `301`, `300` |
| `GUESS` | `OK\|CORRECT\|points` và `OK\|WRONG` | `302`, `303`, `300` |
| `CHAT` | `OK` | `302`, `306`, `204` |

## 7. Integration test

### 7.1 Hạ tầng

- Thư mục `tests/integration/`, dùng **pytest** và thư viện chuẩn `socket`.
- **Fixture `server`:** chọn cổng trống, tạo thư mục tạm cho `users.dat`, khởi chạy server thật với tham số cho phép chạy nhanh, đợi cổng mở (tối đa 5 giây), trả về đối tượng điều khiển. Khi kết thúc: gửi `SIGTERM`, đợi tối đa 5 giây, **khẳng định `returncode == 0`** (đảm bảo coverage được ghi). Với bản ASan, khẳng định `stderr` không có `ERROR: AddressSanitizer` hay `LeakSanitizer`.
- **Lớp `Client`:** bọc socket, có `send(line)`, `recv_line(timeout)`, `expect(prefix)`. **Mọi thao tác chờ đều có thời gian chờ tối đa** để test không treo.
- **Không dùng `sleep` cố định.** Chờ theo điều kiện, ví dụ "đọc cho đến khi gặp sự kiện X hoặc quá hạn".

**Tham số cấu hình server cần có để test chạy nhanh** (mặc định lấy từ `protocol.md` mục 4):

| Tham số | Ví dụ khi test | Dùng cho |
|---|---|---|
| `--port` | cổng trống | Chạy song song nhiều server |
| `--data-dir` | thư mục tạm | Cô lập `users.dat` và log |
| `--words` | `tests/data/words_small.txt` | Từ khóa cố định |
| `--choose-time` | `2` | Test tự chọn từ không phải chờ 15 giây |
| `--turn-pause` | `1` | Rút ngắn nghỉ giữa lượt |
| `--idle-timeout` | `2` | Test hết hạn phiên |
| `--max-clients` | `5` | Test `SERVER_FULL` |

Giá trị mặc định khi chạy thật vẫn đúng như `protocol.md`. Thêm các tham số này vào `config.h` và ghi trong `ARCHITECTURE.md`.

### 7.2 Kịch bản chính (IT)

| Mã | Kịch bản | Kiểm tra |
|---|---|---|
| IT-01 | Ván đầy đủ 3 người, 2 vòng | Toàn bộ chuỗi sự kiện đúng thứ tự như ví dụ ở `protocol.md` mục 10, điểm cuối khớp công thức |
| IT-02 | Ghép dòng qua TCP thật | Gửi một lệnh chia thành nhiều gói (từng byte), gửi nhiều lệnh trong một gói; kết quả như nhau |
| IT-03 | Người vẽ ngắt kết nối giữa lượt | Cả phòng nhận `PLAYER_LEFT`, `TURN_END\|DRAWER_LEFT`, ván tiếp tục |
| IT-04 | Còn một người trong lúc chơi | `GAME_END\|NOT_ENOUGH_PLAYERS`, phòng về sảnh |
| IT-05 | Hết hạn phiên | Kết nối im lặng quá `--idle-timeout`, nhận `KICK\|IDLE` rồi bị đóng; `PING` giữ được kết nối |
| IT-06 | Đạt số kết nối tối đa | Kết nối thứ `max+1` nhận `ERR\|901` và bị đóng; sau khi có người rời thì kết nối mới được chấp nhận |
| IT-07 | Tài khoản bền vững | Đăng ký, tắt server, bật lại cùng `--data-dir`, đăng nhập được |
| IT-08 | Tắt an toàn | `SIGTERM` khi có client đang kết nối, server thoát với mã 0 |
| IT-09 | Một tài khoản hai kết nối | Kết nối thứ hai nhận `114` |
| IT-10 | Nhiều phòng song song | Hai phòng chơi cùng lúc, sự kiện không lẫn sang phòng khác |

### 7.3 Kịch bản độ bền (RB)

Mục tiêu: server **không sập, không đọc ngoài biên (ASan sạch), kết nối khác không bị ảnh hưởng**.

| Mã | Kịch bản |
|---|---|
| RB-01 | Gửi 1 MB byte ngẫu nhiên (cố định hạt giống `seed`) |
| RB-02 | Gửi 100 KB không có `\n`, rồi gửi một lệnh hợp lệ (lệnh hợp lệ vẫn được xử lý, hoặc kết nối bị đóng có kiểm soát) |
| RB-03 | Dòng chứa byte NUL, ký tự điều khiển, UTF-8 không hợp lệ |
| RB-04 | Mở và đóng 1000 kết nối liên tiếp, một số đóng ngay giữa lúc gửi dở một dòng |
| RB-05 | Client **không đọc** dữ liệu: gửi vài trăm nghìn `PING` mà không `recv` (đặt `SO_RCVBUF` nhỏ để nhanh đạt ngưỡng). Server đóng kết nối vì `OUT_BUF` đầy, các client khác vẫn bình thường |
| RB-06 | Gửi lệnh sai trạng thái dồn dập (ví dụ `STROKE` khi chưa đăng nhập) |
| RB-07 | "Fuzz nhẹ" ở mức unit: sinh chuỗi ngẫu nhiên có hạt giống cố định, đưa vào `protocol_parse` và `framing_feed` (chạy dưới ASan), không được sập |

## 8. Kiểm thử bộ nhớ

- **`make asan`:** build ASan + UBSan, chạy toàn bộ unit và integration. Báo cáo ASan hay UBSan nào cũng là **lỗi, phải sửa**.
- **Rò rỉ:** LeakSanitizer báo khi tiến trình thoát, nên cần cơ chế tắt an toàn (đã có trong `ARCHITECTURE.md` mục 6). Mục tiêu: **0 byte rò rỉ**.
- **Valgrind (hằng tuần):** chạy riêng bộ unit test: `valgrind --leak-check=full --error-exitcode=1 ./build/unit-tests`.
- **Rò rỉ chậm:** kiểm tra bằng bài **soak** ở LT-4 (mục 9), theo dõi RSS của server theo thời gian.

## 9. Test chịu tải

### 9.1 Mục tiêu

Trả lời được các câu hỏi sau bằng số liệu: server chịu được bao nhiêu client, độ trễ chuyển tiếp nét vẽ là bao nhiêu, bắt đầu lỗi ở mức nào, và có rò rỉ khi chạy lâu không.

Các ngưỡng dưới đây lấy từ `ARCHITECTURE.md` mục 13. Đó là **mục tiêu**, kết quả thật được ghi lại như đã đo, kể cả khi chưa đạt (và giải thích nguyên nhân trong báo cáo).

### 9.2 Môi trường

Ghi vào báo cáo cho từng lần đo: CPU (số lõi, tên), RAM, hệ điều hành và phiên bản nhân, trình biên dịch, phiên bản Python, **máy chạy server và máy chạy `loadgen` là một hay hai máy**, số tiến trình `loadgen`.

Chuẩn bị trước khi đo:

```bash
ulimit -n 65536            # cho cả server và loadgen (1000 client cần > 1024 fd)
sysctl net.core.somaxconn  # nên >= 1024; server gọi listen() với backlog >= 1024
```

Nếu chạy `loadgen` cùng máy với server, hai bên tranh CPU và làm số liệu xấu đi. Khi có điều kiện nên dùng máy thứ hai, hoặc ít nhất ghim server vào một lõi riêng (`taskset -c 0`) và `loadgen` vào các lõi khác.

### 9.3 Hành vi mỗi bot

- Bot đăng nhập bằng tài khoản **tạo sẵn** (`loadgen.py --seed-users` tạo trước khi đo, D-020).
- Mỗi phòng có 8 bot, **nằm trọn trong một tiến trình** của `loadgen` (để đo độ trễ bằng cùng một đồng hồ `time.perf_counter()`).
- **Người vẽ:** gửi `STROKE` với tốc độ `--stroke-rate` (mặc định 20 lệnh mỗi giây), mỗi lệnh 10 điểm.
- **Người đoán:** gửi `GUESS` sai mỗi 3 đến 8 giây; một người đoán đúng ở khoảng 20 giây để lượt chuyển thường xuyên.
- Hết ván thì chủ phòng gửi `START` lại, nên tải liên tục.

**Đo độ trễ chuyển tiếp nét vẽ:** người vẽ gán cho mỗi `STROKE` một số thứ tự `n` và mã hóa vào trường `color` (24 bit, `n` viết dạng hex 6 chữ số, hợp lệ theo `protocol.md`). Bot đoán nhận `STROKE`, giải mã `n`, tra thời điểm gửi tương ứng và tính độ trễ.

### 9.4 Kịch bản

| Mã | Số client | Số phòng (khoảng) | Thời gian ổn định | Mục đích |
|---|---|---|---|---|
| LT-1 | 100 | 13 | 60 giây | Mức nhẹ, đường cơ sở |
| LT-2 | 500 | 63 | 120 giây | Mức vừa |
| LT-3 | 1000 | 125 | 120 giây | Mức nặng, mục tiêu của đề |
| LT-4 | 500 | 63 | 30 phút | Bền vững (soak): RSS có tăng mãi không, độ trễ có xấu dần không |
| LT-5 | 200 nền + vào ra liên tục | 25 | 60 giây | Kết nối và ngắt liên tục (khoảng 50 chu kỳ mỗi giây) |
| LT-6 | Tăng dần mỗi 250 | | Mỗi bậc 60 giây | **Tìm điểm bão hòa** (tùy chọn): mức đầu tiên có lỗi hoặc p95 vượt 500 ms |

Mỗi kịch bản có giai đoạn **tăng dần** (ramp-up, khoảng 50 client mỗi giây, tránh làm tràn hàng đợi `listen`), giai đoạn **ổn định** (số liệu đo lấy ở đây), giai đoạn **giảm dần**. **Chạy mỗi kịch bản 3 lần và báo cáo trung vị.**

Tải ước tính để đối chiếu: số `STROKE` vào mỗi giây = số phòng x `stroke-rate`; số dòng ra = số vào x (số người trong phòng - 1). Với LT-3 là 125 x 20 = 2500 vào, khoảng 17500 ra mỗi giây.

### 9.5 Chỉ số và tiêu chí

| Chỉ số | Cách đo | Mục tiêu |
|---|---|---|
| Độ trễ chuyển tiếp nét vẽ p50 / p95 / p99 | Mục 9.3 | p95 dưới 100 ms ở LT-2 |
| Độ trễ đăng nhập, vào phòng, `GUESS` (gửi đến `OK`) | Đo từ lệnh đến phản hồi | Ghi lại, không đặt ngưỡng |
| Thông lượng | Số `STROKE` nhận được mỗi giây (tổng toàn hệ thống) | Khớp với tải ước tính |
| Tỉ lệ lỗi | `ERR` bất thường, kết nối bị đóng ngoài ý muốn, quá thời gian chờ, chia cho tổng số lệnh | **0%** ở LT-1 và LT-2; ghi lại ở LT-3 |
| Kết nối ổn định tối đa | Số client lớn nhất không có lỗi | 1000 |
| CPU server | Đọc `/proc/<pid>/stat` mỗi giây | Ghi lại; một lõi chạm 100% là dấu hiệu bão hòa (D-003) |
| RSS server | Đọc `/proc/<pid>/status` mỗi giây | Dưới 100 MB ở 1000 client; ở LT-4 **không tăng liên tục** |
| Rò rỉ | Chạy riêng một lần ngắn dưới bản ASan | 0 |

### 9.6 Đầu ra

`loadgen` ghi vào `results/<ngày>_<mã kịch bản>/`:

- `summary.json`: toàn bộ chỉ số tổng hợp, kèm tham số chạy và thông tin môi trường.
- `latency.csv`: độ trễ từng mẫu theo thời gian.
- `resources.csv`: CPU và RSS theo thời gian.
- Biểu đồ (matplotlib): độ trễ theo thời gian, phân phối độ trễ, CPU và RSS theo thời gian, thông lượng theo số client.

**Lưu ý khi đọc kết quả:** nếu CPU của máy chạy `loadgen` tiến sát 100% thì số liệu phản ánh giới hạn của công cụ chứ không phải của server (D-020). Khi đó tách `loadgen` ra nhiều tiến trình (`--procs N`) hoặc chạy trên máy khác rồi đo lại.

## 10. CI

Một workflow GitHub Actions (`.github/workflows/ci.yml`), chạy mỗi lần push:

| Bước | Nội dung | Chặn khi |
|---|---|---|
| 1. Build | `make` với `-Wall -Wextra -Werror` | Có cảnh báo hoặc lỗi |
| 2. Kiểm tra quy ước | Module logic không chứa `sys/socket.h`, `unistd.h`, `sys/epoll.h` (`grep`); không có `gets`, `sprintf`, `strcpy` | Có vi phạm |
| 3. Unit | `make test` | Có test hỏng |
| 4. Integration | `make itest` | Có test hỏng hoặc server thoát khác 0 |
| 5. Coverage | `make coverage` | Dưới 90% dòng (từ tuần 2) |
| 6. ASan | `make asan` | Có báo cáo ASan, UBSan, LeakSanitizer |
| 7. Tải nhẹ (chỉ nhánh `main`) | `loadgen` 100 client trong 20 giây | Có lỗi giao thức hoặc server sập |

Tải chính thức (LT-2 đến LT-6) **không chạy trên CI** vì máy CI không ổn định và không đại diện. Chạy thủ công trên máy bạn rồi lưu kết quả vào `results/`.

Tải lên báo cáo coverage (`build/coverage/`) làm artifact để xem lại.

## 11. Quy trình khi gặp lỗi

1. **Viết test tái hiện lỗi trước** (unit nếu có thể, nếu không thì integration). Test phải **hỏng** trước khi sửa.
2. Sửa code đến khi test **qua**.
3. Đặt tên test có tiền tố `test_regression_`, ví dụ `test_regression_chat_blocked_when_word_inside_sentence`, và ghi một dòng về nguyên nhân trong thông điệp commit.
4. Nếu lỗi bộc lộ vấn đề thiết kế, thêm một mục vào `DECISIONS.md`.
5. **Test chập chờn (flaky):** không dùng "chạy lại cho qua". Tìm nguyên nhân (thường là phụ thuộc thời gian thật hoặc thứ tự), sửa gốc.

## 12. Kiểm tra quy định nộp bài

Các yêu cầu về commit không phải là test mã nguồn, nhưng nên kiểm tra tự động để khỏi sót. Script **chỉ đọc**, không sửa lịch sử git. Lưu thành `tools/check_commits.py`:

```python
#!/usr/bin/env python3
"""Báo cáo số commit mỗi tuần và khoảng cách giữa các commit liên tiếp."""
import subprocess
from datetime import datetime

WEEK_STARTS = ["2026-11-11", "2026-11-17", "2026-11-24", "2026-12-01",
               "2026-12-08", "2026-12-15", "2026-12-22"]
MIN_COMMITS, MIN_GAP_MIN = 5, 30

bounds = [datetime.fromisoformat(d) for d in WEEK_STARTS]
out = subprocess.check_output(["git", "log", "--reverse", "--format=%ct|%s"], text=True)
commits = []
for line in out.splitlines():
    ts, subject = line.split("|", 1)
    commits.append((datetime.fromtimestamp(int(ts)), subject))

for i in range(len(bounds) - 1):
    week = [c for c in commits if bounds[i] <= c[0] < bounds[i + 1]]
    gaps = [(b[0] - a[0]).total_seconds() / 60 for a, b in zip(week, week[1:])]
    short = sum(1 for g in gaps if g < MIN_GAP_MIN)
    status = "OK" if len(week) >= MIN_COMMITS and short == 0 else "CẦN XEM LẠI"
    print(f"Tuần {i + 1}: {len(week)} commit, {short} khoảng cách dưới {MIN_GAP_MIN} phút -> {status}")
```

Chạy `python tools/check_commits.py` vào cuối mỗi tuần. Nếu có tuần thiếu, **làm thêm việc thật** trong các ngày còn lại của tuần đó, không sửa ngày commit.

## 13. Lịch kiểm thử theo tuần

| Tuần | Việc kiểm thử |
|---|---|
| 1 | Dựng CI, Makefile (`test`, `itest`, `coverage`, `asan`). Test `framing`, `protocol` (một phần). Fixture `server` và IT đầu tiên (kết nối, `HELLO`, tắt an toàn). Chưa chặn coverage |
| 2 | Test `sha256`, `auth`, `session`, `logger`, `clock`, `rng`. **Bắt đầu chặn coverage 90%.** IT-07, IT-09, IT-05 |
| 3 | Test `room`, `conn`, `dispatch` (test theo bảng). IT-10, IT-06 |
| 4 | Test `game`, `scoring`, `chat`, `words`. IT-01, IT-03, IT-04, IT-02 |
| 5 | Bổ sung test để coverage ổn định trên 90%. Nhóm RB. Viết `loadgen.py`, chạy thử LT-1 |
| 6 | Chạy chính thức LT-1 đến LT-5 (và LT-6 nếu còn thời gian). `make asan` sạch. Valgrind. Tổng hợp biểu đồ cho báo cáo |

## 14. Mẫu trình bày kết quả trong báo cáo

**Bảng coverage** (lấy từ `gcovr --print-summary` và báo cáo theo file):

| Module | Dòng | Dòng được phủ | Coverage |
|---|---|---|---|
| framing | | | |
| protocol | | | |
| ... | | | |
| **Tổng** | | | **>= 90%** |

**Bảng tải:**

| Kịch bản | Client | p50 (ms) | p95 (ms) | p99 (ms) | Lỗi (%) | CPU server (%) | RSS (MB) |
|---|---|---|---|---|---|---|---|
| LT-1 | 100 | | | | | | |
| LT-2 | 500 | | | | | | |
| LT-3 | 1000 | | | | | | |

Kèm theo: thông tin môi trường (mục 9.2), biểu đồ (mục 9.6), nhận xét về điểm nghẽn và đối chiếu với mục tiêu ở `ARCHITECTURE.md` mục 13.

## 15. Danh sách kiểm tra trước khi nộp

- [ ] `make check` xanh trên bản `main` mới nhất
- [ ] `make coverage` >= 90%, đính kèm báo cáo HTML
- [ ] `make asan` sạch (không báo cáo nào), Valgrind không rò rỉ
- [ ] Mọi lệnh trong `protocol.md` có test đúng và test sai (mục 6.2)
- [ ] IT-01 đến IT-10 và RB-01 đến RB-07 đều qua
- [ ] Đã chạy LT-1 đến LT-5, có `results/` kèm biểu đồ và thông tin môi trường
- [ ] `tools/check_commits.py` không báo tuần nào cần xem lại
- [ ] Số liệu trong báo cáo khớp với file trong `results/`
