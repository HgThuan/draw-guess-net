# Danh sách công việc (TASKS) netsketch

> **Đọc file này đầu tiên mỗi phiên làm việc** (`AGENTS.md` mục 5 và 10).
> Mỗi phiên chọn **một task nhỏ**, nêu kế hoạch, chờ đồng ý, làm kèm test, báo kết quả thật.
> Tài liệu liên quan: `protocol.md`, `ARCHITECTURE.md`, `TESTING.md`, `DECISIONS.md`, `TECKSTACK.md`.
> Trạng thái: bản nháp v1.0, 11/11/2026.

## 1. Cách dùng file này

- Mỗi task có mã `T-xxx`, **không đổi số**. Task mới thêm vào cuối tuần tương ứng.
- Một task vừa với **một phiên làm việc và một đến hai commit** (code + test của nó).
- Trạng thái: `TODO`, `DOING`, `DONE`, `BLOCKED` (ghi lý do), `CUT` (cắt khi trễ, ghi lý do).
- Khi xong một task: đánh dấu `DONE`, ghi ngày, kiểm tra **định nghĩa hoàn thành chung** (mục 3).
- Phát hiện việc mới ngoài phạm vi: ghi vào mục 13 (Việc phát sinh), **không tự làm**.

## 2. Phiên hiện tại

| Mục | Giá trị |
|---|---|
| Task đang làm | **T-005** (`common/protocol`: parse lệnh, mã lỗi, định dạng phản hồi) |
| Việc chặn | (không) |
| Tuần | 1 (11/11 đến 16/11) |

## 3. Định nghĩa hoàn thành chung (áp dụng cho mọi task code)

Lấy từ `AGENTS.md` mục 14:

- [ ] `make` không cảnh báo, `make test` xanh, `make asan` sạch
- [ ] Có unit test cho trường hợp đúng, sai và biên
- [ ] Coverage không giảm dưới 90% (bắt đầu chặn từ tuần 2)
- [ ] `protocol.md` và `ARCHITECTURE.md` khớp với code
- [ ] Đã thử tay bằng `nc` hoặc client (với tính năng liên quan mạng)
- [ ] Người làm đã đọc diff và hiểu code
- [ ] Đã có đề xuất commit message

Mỗi task bên dưới chỉ ghi **tiêu chí riêng**, ngầm hiểu phải thỏa cả mục này.

## 4. Việc chặn và quyết định đang chờ

| Mã | Việc | Chặn task | Trạng thái |
|---|---|---|---|
| B-1 | Xác nhận **D-018** (thư mục `common/`) | T-001, T-004, T-005 | DONE (11/11) |
| B-2 | Xác nhận **D-010** (SHA-256 tự cài) | T-201 | DONE (11/11) |
| B-3 | Cập nhật `AGENTS.md` mục 2, 3, 4, 5 (công nghệ, target `make`, `common/`, danh sách tài liệu) | T-001 | DONE (11/11) |
| B-4 | Kiểm tra máy đã có gcc, make, cmocka, gcovr, valgrind, python3, tkinter, pytest | T-001 | DONE (11/11) |
| B-5 | Hỏi giáo viên về quy định dùng AI (D-022) | Báo cáo | TODO |

## 5. Lộ trình tổng quát

Tuần tính theo `TESTING.md` mục 12. **Đóng băng tính năng 14/12**, hạn nộp **22/12** (dự phòng 29/12).

| Tuần | Thời gian | Mục tiêu | Tính năng (điểm) |
|---|---|---|---|
| 1 | 11/11 đến 16/11 | Hạ tầng, framing, protocol, vòng lặp `epoll`, CI | Truyền dòng (1), Vào/ra socket (2) |
| 2 | 17/11 đến 23/11 | Tài khoản, phiên, logger. **Bắt đầu chặn coverage 90%** | Tài khoản (2), Phiên (2), Log (1) |
| 3 | 24/11 đến 30/11 | Phòng chơi, dispatch theo bảng, `libnetsketch` | Phòng (2) |
| 4 | 01/12 đến 07/12 | Ván chơi, nét vẽ, đoán, chấm điểm, chat, thoát giữa chừng | Nét vẽ (3), Lượt (2), Đoán (2), Xếp hạng (1), Thoát (1), Chat (1) |
| 5 | 08/12 đến 14/12 | UI hoàn chỉnh, test độ bền, `loadgen`. **Đóng băng 14/12** | (hoàn thiện, demo) |
| 6 | 15/12 đến 21/12 | Load test chính thức, sửa lỗi, báo cáo, slide | (báo cáo) |

> [!NOTE]
> `TESTING.md` mục 13 xếp test `conn` vào tuần 3, nhưng `net_loop` ở tuần 1 đã cần bảng kết nối và bộ đệm gửi. Kế hoạch này đưa phần cơ bản của `conn` lên tuần 1 (T-006). Cần sửa `TESTING.md` mục 13 cho khớp.

## 6. Tuần 1 (11/11 đến 16/11): hạ tầng, truyền dòng, socket

| Mã | Task | File chính | Tiêu chí riêng | Phụ thuộc | Trạng thái |
|---|---|---|---|---|---|
| T-001 | Khung thư mục, `Makefile` (`all`, `test`, `clean`), `.gitignore` | `Makefile`, cây thư mục theo `ARCHITECTURE.md` mục 3 | `make` và `make test` chạy được với một test cmocka mẫu | B-1, B-3, B-4 | DONE (11/11) |
| T-002 | Target `coverage`, `asan` trong `Makefile` | `Makefile` | Ba bản build tách thư mục `build/`, `build/cov/`, `build/asan/` với cờ đúng `TECKSTACK.md` mục 3; `gcovr` in tóm tắt (chưa chặn) | T-001 | DONE (12/11) |
| T-003 | CI GitHub Actions: build, `grep` quy ước, unit, coverage (chỉ báo cáo), asan | `.github/workflows/ci.yml` | Workflow xanh trên lần push đầu. Bước `grep` báo lỗi nếu module logic chứa `sys/socket.h`, `unistd.h`, `sys/epoll.h` hoặc có `gets`, `sprintf`, `strcpy` | T-002 | DONE (12/11) |
| T-004 | `common/framing`: bộ đệm ghép dòng | `common/framing.[ch]`, `tests/unit/test_framing.c` | Đủ các trường hợp ở `TESTING.md` mục 6 hàng `framing` (cắt đôi, dính nhiều dòng, 512 và 513 byte, `\r\n`, dòng rỗng, từng byte); RB-07 phần framing | T-001 | DONE (12/11) |
| T-005 | `common/protocol`: parse dòng thành `Command`, bảng mã lỗi, hàm định dạng `OK`/`ERR` | `common/protocol.[ch]`, `tests/unit/test_protocol.c` | Tách đúng mọi lệnh ở `protocol.md` mục 6 (số tham số, trường văn bản tự do chứa `\|`); `103`, `100`; kiểm tra số nguyên; RB-07 phần parse | T-004 | TODO |
| T-006 | `clock`, `rng` (thật và giả), `conn` cơ bản (bảng slot, `ConnId` có thế hệ, `out` và danh sách `dirty`) | `server/src/{clock,rng,conn}.[ch]`, test tương ứng | `ConnId` cũ không hợp lệ sau khi tái dùng slot; `out` đầy thì báo; đồng hồ giả điều khiển được | T-001 | TODO |
| T-007 | `config.h` + đọc tham số dòng lệnh (`--port`, `--data-dir`, `--words`, `--choose-time`, `--turn-pause`, `--idle-timeout`, `--max-clients`, `--verbose`) | `server/src/{config.h,server.[ch],main.c}` | Mặc định đúng `protocol.md` mục 4; tham số sai thì in hướng dẫn và thoát mã khác 0 | T-001 | TODO |
| T-008 | `net_loop`: `epoll`, `accept`, `recv` -> framing, gửi `HELLO`, gộp ghi, đóng hoãn, tắt an toàn bằng `SIGTERM`/`SIGINT` | `server/src/net_loop.[ch]` | Thử tay `nc localhost 9000` nhận `HELLO\|netsketch\|1`; `SIGTERM` thoát mã 0 | T-004, T-006, T-007 | TODO |
| T-009 | `dispatch` khung + lệnh `PING` | `server/src/dispatch.[ch]`, `tests/unit/test_dispatch.c` | `PING` -> `PONG` ở mọi trạng thái; lệnh lạ -> `103`; dòng quá dài -> `104` không đóng kết nối | T-005, T-008 | TODO |
| T-010 | Hạ tầng integration: fixture `server`, lớp `Client`, target `itest`, `check` | `tests/integration/conftest.py`, `Makefile` | IT-02 (ghép dòng qua TCP thật), IT-08 (tắt an toàn, `returncode == 0`); CI chạy `itest` | T-009 | TODO |

**Kế hoạch commit tuần 1** (tối thiểu 5, cách nhau tối thiểu 30 phút): T-001, T-002, T-003, T-004, T-005, T-006, T-008, T-009, T-010.

## 7. Tuần 2 (17/11 đến 23/11): tài khoản, phiên, log

| Mã | Task | File chính | Tiêu chí riêng | Phụ thuộc | Trạng thái |
|---|---|---|---|---|---|
| T-101 | Bật chặn coverage 90% trong `make coverage` và CI | `Makefile`, `ci.yml` | `gcovr --fail-under-line 90`, loại đúng `main.c`, `tests/`, `tools/`, `build/` | T-010 | TODO |
| T-102 | `logger`: mức, định dạng, sink file và sink bộ nhớ, `fflush` mỗi giây | `server/src/logger.[ch]` | Định dạng đúng `ARCHITECTURE.md` mục 10; lọc theo mức | T-006 | TODO |
| T-201 | `common/sha256` | `common/sha256.[ch]` | Test vector chuẩn (rỗng, `"abc"`, chuỗi dài hơn một khối) | B-2 | TODO |
| T-202 | `auth`: `user_store` (bảng băm, nạp và ghi nối `users.dat`), kiểm tra tên và mật khẩu | `server/src/auth.[ch]` | Nạp lại sau khởi động; file rỗng và hỏng; mật khẩu không xuất hiện thô trong file | T-201 | TODO |
| T-203 | Lệnh `REGISTER` | `auth`, `dispatch` | `OK`; `110`, `112`, `113` ở giá trị biên (2/3, 16/17 ký tự tên; 5/6, 32/33 byte mật khẩu); `102` | T-202, T-009 | TODO |
| T-204 | Lệnh `LOGIN`, `session_id`, một tài khoản một kết nối | `auth`, `session` | `OK\|session_id` (16 hex, `Rng` giả); `111` cho cả sai tên và sai mật khẩu; `114`; `102` | T-203 | TODO |
| T-205 | `LOGOUT`, hết hạn 120 giây (`KICK\|IDLE`), dọn dẹp khi ngắt kết nối | `session` | `101`; hết hạn bằng đồng hồ giả; `PING` làm mới hạn; sau ngắt thì đăng nhập lại được | T-204 | TODO |
| T-206 | Ghi log các sự kiện bắt buộc của tài khoản và phiên; che mật khẩu ở `DEBUG` | `logger`, `auth`, `session`, `net_loop` | Test sink bộ nhớ: không chứa mật khẩu kể cả `--verbose` | T-102, T-205 | TODO |
| T-207 | Integration IT-05, IT-07, IT-09 | `tests/integration/` | Cả ba qua, server thoát mã 0 | T-205, T-010 | TODO |

## 8. Tuần 3 (24/11 đến 30/11): phòng chơi, dispatch, thư viện client

| Mã | Task | File chính | Tiêu chí riêng | Phụ thuộc | Trạng thái |
|---|---|---|---|---|---|
| T-301 | `room` cơ bản + `CREATE_ROOM` + sự kiện `ROOM_INFO` | `server/src/room.[ch]` | `OK\|room_id`; `205` ở các biên tham số; `201`; `208` | T-204 | TODO |
| T-302 | `LIST_ROOMS` phân trang | `room` | 0, 1, 8, 9, 17 phòng; trang rỗng vẫn `OK\|total\|page`; `100` khi page không phải số | T-301 | TODO |
| T-303 | `JOIN_ROOM` + `PLAYER_JOINED` | `room` | `200`, `201`, `202`, `203`; sự kiện đến đúng người | T-301 | TODO |
| T-304 | `LEAVE_ROOM`, chuyển chủ phòng (`HOST_CHANGED`), xóa phòng rỗng, `PLAYER_LEFT` | `room`, `session` | `204`; `players[0]` mới là chủ; ngắt kết nối cũng đi qua `room_leave` | T-303, T-205 | TODO |
| T-305 | Bảng `CmdSpec` đầy đủ + test theo bảng ma trận `protocol.md` mục 5.3 | `dispatch` | Đúng thứ tự kiểm tra lỗi (`protocol.md` mục 3); mọi ô ma trận đã có lệnh đều được kiểm tra | T-304 | TODO |
| T-306 | Đóng client quá chậm (`OUT_BUF` đầy), `SERVER_FULL` (`ERR\|901`, `KICK`) | `net_loop`, `conn` | IT-06; RB-05 | T-010 | TODO |
| T-307 | `libnetsketch`: 4 hàm, dùng lại `common/framing` | `client/libnetsketch/` | Unit test bằng `socketpair()`; `ns_recv_line` trả 1/0/<0 đúng định nghĩa | T-004 | TODO |
| T-308 | Integration IT-10 (nhiều phòng song song) | `tests/integration/` | Sự kiện không lẫn phòng | T-304 | TODO |

## 9. Tuần 4 (01/12 đến 07/12): ván chơi

| Mã | Task | File chính | Tiêu chí riêng | Phụ thuộc | Trạng thái |
|---|---|---|---|---|---|
| T-401 | `words`: nạp file, chọn 3 từ khác nhau, chuẩn hóa so khớp | `server/src/words.[ch]`, `data/words.txt`, `tests/data/words_small.txt` | File ít hơn 3 từ; dòng trống, khoảng trắng thừa | T-006 | TODO |
| T-402 | `scoring` (hàm thuần) | `server/src/scoring.[ch]` | Bốn ví dụ `protocol.md` mục 8; sàn 50; `remaining` = 0 và = `draw_time`; người vẽ khi không ai đoán | (không) | TODO |
| T-403 | `START`, `GAME_START`, `TURN_START`, `WORD_OPTIONS`, `CHOOSE_WORD`, tự chọn khi hết giờ | `server/src/game.[ch]` | `206`, `207`, `300`, `301`, `304`; `WORD_OPTIONS` chỉ đến người vẽ | T-305, T-401 | TODO |
| T-404 | `DRAW_START` (gợi ý gạch dưới), `TIME` mỗi 5 giây, `game_tick` | `game`, `server` | Từ có dấu cách hiển thị đúng; `TIME` bằng đồng hồ giả | T-403 | TODO |
| T-405 | `STROKE`, `CLEAR`: kiểm tra và chuyển tiếp (trừ người vẽ), không phản hồi khi thành công | `game` hoặc `server/src/draw.[ch]` | `305` (số lẻ, ngoài 0..1000, dưới 2 hoặc quá 40 điểm, màu sai, `width` ngoài 1..20); `301`; `300` | T-404 | TODO |
| T-406 | `GUESS`, `GUESS_WRONG`, `GUESS_CORRECT`, chấm điểm theo thời gian | `game`, `scoring` | `302`, `303`, `300`; so khớp không phân biệt hoa thường, cắt khoảng trắng | T-405, T-402 | TODO |
| T-407 | Kết thúc lượt (`TIMEUP`, `ALL_GUESSED`), `TURN_END`, nghỉ, luân phiên người vẽ, `GAME_END\|FINISHED`, về sảnh và reset điểm | `game` | Luân phiên đúng thứ tự; điểm người vẽ đúng công thức; ranking sắp đúng | T-406 | TODO |
| T-408 | `SCOREBOARD` | `game`, `dispatch` | Sắp giảm dần theo điểm, hòa theo tên tăng dần; `204` | T-407 | TODO |
| T-409 | Thoát giữa chừng: người vẽ rời (`DRAWER_LEFT`), người đoán cuối rời, còn dưới 2 người (`NOT_ENOUGH_PLAYERS`), người rời bị bỏ qua ở vòng sau | `room`, `game` | Đủ các dòng của `protocol.md` mục 9 | T-407 | TODO |
| T-410 | `CHAT` và chặn lộ đáp án | `server/src/chat.[ch]` | Đủ bảng `protocol.md` mục 7.4: `302`, `306` (lẫn trong câu, khác hoa thường), kênh riêng người đã đoán đúng | T-406 | TODO |
| T-411 | Log sự kiện phòng và ván (tạo, vào, rời, bắt đầu, kết thúc lượt, kết thúc ván) | `logger`, `room`, `game` | Kiểm tra bằng sink bộ nhớ | T-407 | TODO |
| T-412 | Integration IT-01, IT-03, IT-04 | `tests/integration/` | IT-01 khớp chuỗi sự kiện ở `protocol.md` mục 10 | T-409 | TODO |

## 10. Tuần 5 (08/12 đến 14/12): UI, độ bền, loadgen. Đóng băng 14/12

| Mã | Task | File chính | Tiêu chí riêng | Phụ thuộc | Trạng thái |
|---|---|---|---|---|---|
| T-501 | UI: lớp `ctypes` bọc `libnetsketch`, vòng `poll` bằng `root.after`, phân biệt phản hồi và sự kiện | `client/ui/netsketch.py` | Không thread; không có logic game | T-307 | TODO |
| T-502 | UI: màn hình đăng ký, đăng nhập, danh sách phòng, tạo và vào phòng | `client/ui/` | Thử tay với server thật | T-501, T-304 | TODO |
| T-503 | UI: canvas vẽ (gom điểm 30 ms hoặc 40 điểm, chuẩn hóa 0..1000), nhận và vẽ `STROKE`, `CLEAR` | `client/ui/` | Hai cửa sổ vẽ và thấy nhau | T-502, T-405 | TODO |
| T-504 | UI: chọn từ, đoán, chat, thời gian, bảng điểm, bảng xếp hạng cuối ván | `client/ui/` | Chơi trọn một ván 3 người | T-503, T-410 | TODO |
| T-505 | Nhóm test độ bền RB-01 đến RB-06 | `tests/integration/` | Server không sập, ASan sạch, client khác không bị ảnh hưởng | T-412 | TODO |
| T-506 | Bổ sung test để mỗi file logic từ 80%, tổng từ 90% | `tests/` | Báo cáo `gcovr` theo file | T-412 | TODO |
| T-507 | `tools/loadgen.py`: `--seed-users`, bot vẽ và đoán, đo độ trễ qua trường `color`, ghi `results/` | `tools/loadgen.py` | Chạy thử LT-1 thành công | T-412 | TODO |
| T-508 | Bước tải nhẹ trên CI (100 client, 20 giây, chỉ nhánh `main`) | `ci.yml` | Không lỗi giao thức | T-507 | TODO |
| T-509 | **Đóng băng tính năng**: rà soát `protocol.md`, `ARCHITECTURE.md` khớp code | `docs/` | Không còn sai lệch | Tất cả | TODO |

## 11. Tuần 6 (15/12 đến 21/12): đo tải, báo cáo, slide

Chỉ sửa lỗi và hoàn thiện, **không thêm tính năng** (D-023).

| Mã | Task | Tiêu chí riêng | Trạng thái |
|---|---|---|---|
| T-601 | Ghi phiên bản môi trường vào `TECKSTACK.md` mục 6 và `TESTING.md` mục 9.2 | Đủ các dòng | TODO |
| T-602 | Chạy chính thức LT-1 đến LT-5 (mỗi kịch bản 3 lần, lấy trung vị), LT-6 nếu còn thời gian | `results/` có `summary.json`, CSV, biểu đồ | TODO |
| T-603 | `make asan` sạch, Valgrind không rò rỉ | Không báo cáo nào | TODO |
| T-604 | `tools/check_commits.py`, kiểm tra quy định commit | Không tuần nào "CẦN XEM LẠI" | TODO |
| T-605 | Báo cáo: thiết kế, giao thức, kiểm thử, kết quả tải, hạn chế, hướng phát triển (theo gợi ý cuối `DECISIONS.md`) | Số liệu khớp `results/` | TODO |
| T-606 | Slide và kịch bản demo (2 đến 3 cửa sổ UI + `nc`) | Demo thử trọn vẹn một lần | TODO |
| T-607 | Danh sách kiểm tra trước khi nộp (`TESTING.md` mục 15) | Mọi ô đã đánh dấu | TODO |

## 12. Thứ tự cắt khi trễ tiến độ

Theo `AGENTS.md` mục 9, cắt từ trên xuống:

1. Mở rộng tùy chọn: `HINT`, giới hạn tốc độ lệnh, nối lại phiên, bảng băm cho phòng (D-023)
2. LT-6 (tìm điểm bão hòa), bước tải trên CI (T-508)
3. Hiệu ứng UI, trau chuốt giao diện (một phần T-504)
4. Kênh chat riêng cho người đã đoán đúng (giữ phần chặn lộ đáp án `306`)

**Không bao giờ cắt:** socket và truyền dòng, tài khoản, phòng, nét vẽ, đoán và chấm điểm, coverage 90%, load test 100/500/1000.

## 13. Việc phát sinh (ghi lại, chưa làm)

| Ngày | Việc | Nguồn | Quyết định |
|---|---|---|---|
| 11/11/2026 | `TESTING.md` mục 13 cần đưa `conn` cơ bản lên tuần 1 cho khớp T-006 | Lập kế hoạch | Chờ |
| 11/11/2026 | Thêm mục D-024 vào `DECISIONS.md` (cmocka thay Unity, gcovr thay lcov, pytest, matplotlib) | `TECKSTACK.md` mục 8 | Đã xong (11/11) |
| 11/11/2026 | `ARCHITECTURE.md` chưa ghi các tham số dòng lệnh của server (`TESTING.md` mục 7.1 yêu cầu) | Lập kế hoạch | Chờ |

## 14. Nhật ký hoàn thành

| Ngày | Task | Commit gợi ý | Ghi chú |
|---|---|---|---|
| 11/11/2026 | (tài liệu) | `docs: initialize project docs, decisions, and tech stack` | `AGENTS.md`, `README.md`, `docs/*` |
| 11/11/2026 | T-001 | `chore(build): add project directory skeleton, Makefile, and initial cmocka test` | `make` và `make test` xanh |
| 12/11/2026 | T-002, T-003 | `ci(build): add coverage, asan targets and GitHub Actions workflow` | `make coverage`, `make asan` và CI workflow |
| 12/11/2026 | (tài liệu) | `docs(rules): enforce no-docs commit rule on code branches (D-025)` | `AGENTS.md`, `DECISIONS.md`, `TASKS.md` |
| 12/11/2026 | T-004 | `feat(framing): implement TCP line framing buffer and unit tests` | 9 unit tests, coverage 97.9% |
