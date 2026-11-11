# AGENTS.md — netsketch

Hướng dẫn dành cho AI agent làm việc trong repo này. Hãy đọc hết file trước khi làm bất cứ việc gì.

## 1. Tổng quan dự án

**netsketch** là trò chơi vẽ và đoán hình qua mạng (kiểu Skribbl.io), đồ án môn Lập trình mạng.

- Một người vẽ, những người còn lại đoán từ khóa. Đoán đúng càng nhanh điểm càng cao.
- **Dự án do một người thực hiện.** Người đó phải hiểu, bảo vệ và demo toàn bộ mã nguồn.
- Hạn nộp: **22/12** (dự phòng 29/12). Đóng băng tính năng: **14/12**.
- Sản phẩm nộp: báo cáo, slide, mã nguồn.

## 2. Công nghệ (không tự ý thay đổi)

| Thành phần | Công nghệ |
|---|---|
| Server | **C99** (gcc, Linux), `epoll`, **một luồng duy nhất, không dùng thread** |
| Mã dùng chung | **C99** (`common/`): `framing`, `protocol`, `sha256` (băm mật khẩu tự cài kèm salt, không dùng OpenSSL) |
| Thư viện client | **C**, `libnetsketch` (`.so`): connect, gửi/nhận dòng. Giữ rất mỏng |
| UI | Python 3 + Tkinter, gọi `libnetsketch` qua `ctypes`. UI phải mỏng, không chứa logic game |
| Lưu trữ | File văn bản (`users.dat`: salt + SHA-256 hash mật khẩu), `data/words.txt`, file log |
| Unit test & bộ nhớ | cmocka + `gcov`/`gcovr`, ASan/UBSan, Valgrind |
| Integration và load test | Python 3 (`pytest` cho integration, `asyncio` + `matplotlib` cho load test và biểu đồ báo cáo) |
| Build & CI | GNU `make`, GitHub Actions |

Phần mạng cốt lõi **bắt buộc viết bằng C**. Không thêm thư viện ngoài danh sách trên nếu chưa hỏi.

## 3. Lệnh thường dùng

```bash
make                 # build server và libnetsketch
make test            # chạy toàn bộ unit test (cmocka)
make itest           # chạy integration test (pytest + server thật)
make check           # chạy unit + integration + kiểm tra quy ước
make coverage        # báo cáo coverage, yêu cầu >= 90% dòng (loại main.c)
make asan            # chạy test với AddressSanitizer + UBSan
make loadtest        # chạy một kịch bản tải
make clean
./build/netsketch-server --port 9000
python3 tools/loadgen.py --clients 100
nc localhost 9000    # thử tay giao thức
```

Nếu một target chưa tồn tại, hãy tạo nó trong `Makefile` thay vì dùng lệnh riêng lẻ.

## 4. Cấu trúc thư mục

```
common/        framing, protocol, sha256 (dùng chung cho server và client)
server/src/    main, config, server, net_loop, conn, dispatch, auth, session, room, game, scoring, chat, words, clock, rng, logger
client/        libnetsketch/ (C), ui/ (Python)
tools/         loadgen.py, check_commits.py
tests/         unit/, integration/, data/
docs/          protocol.md, ARCHITECTURE.md, TASKS.md, TESTING.md, DECISIONS.md, TECKSTACK.md
data/          words.txt
```

## 5. Tài liệu liên quan (đọc khi cần)

- `docs/TASKS.md`: **việc hiện tại và tiêu chí hoàn thành.** Đọc đầu tiên mỗi phiên.
- `docs/protocol.md`: hợp đồng giao thức, nguồn sự thật cho server và client.
- `docs/ARCHITECTURE.md`: module, luồng dữ liệu, sơ đồ trạng thái.
- `docs/TESTING.md`: chiến lược test và load test.
- `docs/DECISIONS.md`: nhật ký quyết định thiết kế.
- `docs/TECKSTACK.md`: tổng hợp công nghệ, cờ biên dịch và phiên bản môi trường.

Nếu một file chưa tồn tại hoặc thiếu thông tin, hãy nói cho tôi biết thay vì tự đoán.

## 6. Quy ước giao thức

- Text, mỗi thông điệp một dòng: `LENH|tham_so1|tham_so2\n`. Tối đa 512 byte mỗi dòng, UTF-8.
- Phản hồi: `OK|...` hoặc `ERR|<ma_loi>|<mo_ta>`.
- Server phải có **bộ đệm ghép dòng**: xử lý đúng khi TCP gửi dở một dòng hoặc dính nhiều dòng vào một lần `recv`.
- **Mọi thay đổi giao thức phải sửa `docs/protocol.md` trước**, rồi mới sửa code.
- Dữ liệu từ client là không đáng tin: luôn kiểm tra độ dài, định dạng, quyền và trạng thái trước khi xử lý.

## 7. Quy ước viết code

- C99, biên dịch với `-Wall -Wextra -Werror`. Không có cảnh báo.
- Bắt buộc kiểm tra giá trị trả về của `read`, `write`, `accept`, `epoll_*`, `malloc`. Socket đặt non-blocking.
- Không buffer overflow: dùng `snprintf`, `strncpy` có kết thúc chuỗi, kiểm tra độ dài. Không dùng `gets`, `sprintf`, `strcpy`.
- Mật khẩu **không lưu thô**: dùng salt + hash. Không ghi mật khẩu hay token vào log.
- **Tách logic khỏi I/O.** Logic nghiệp vụ (`parse_line`, `room_join`, `check_guess`, `score_calc`...) là hàm thuần, không đụng socket, để unit test được. `main.c` chỉ khởi tạo và gọi.
- Thời gian lấy qua một hàm bao (clock) để test có thể giả lập, không gọi `time()` rải rác.
- Không dùng biến toàn cục ngoài trạng thái server đã mô tả trong `ARCHITECTURE.md`.
- Mỗi `malloc` phải có đường `free` tương ứng. ASan phải sạch.
- Định danh và message commit viết bằng tiếng Anh. Chú thích có thể tiếng Việt hoặc Anh, ngắn gọn, giải thích "vì sao" chứ không lặp lại "cái gì".

## 8. Quy ước kiểm thử

- Mỗi hàm mới đi kèm unit test **trong cùng lần làm việc**. Mỗi lệnh trong `protocol.md` có ít nhất 1 test đúng và 1 test sai.
- Test cả trường hợp biên: chuỗi rỗng, dài quá giới hạn, phòng đầy, hết giờ, người chơi thoát giữa chừng.
- **Coverage dòng >= 90%.** Nếu tụt, bổ sung test, không hạ ngưỡng.
- **Không sửa test cho "qua" khi code sai.** Hãy báo lỗi và giải thích nguyên nhân.
- Không viết test phụ thuộc thời gian thật hoặc mạng ngoài. Dùng đồng hồ giả và `socketpair()`.
- Load test: kịch bản 100, 500, 1000 client. Đo latency p50/p95/p99, throughput, tỉ lệ lỗi, CPU/RAM.

## 9. Tính năng cần hoàn thành (thang 20 điểm)

| Tính năng | Điểm |
|---|---|
| Xử lý truyền dòng | 1 |
| Cài đặt cơ chế vào/ra socket trên server | 2 |
| Đăng ký và quản lý tài khoản | 2 |
| Đăng nhập và quản lý phiên | 2 |
| Tạo, liệt kê, vào phòng chơi | 2 |
| Truyền nét vẽ realtime | 3 |
| Chọn từ khóa, luân phiên lượt vẽ | 2 |
| Đoán từ, chấm điểm theo thời gian | 2 |
| Bảng xếp hạng cuối ván | 1 |
| Xử lý người chơi thoát giữa chừng | 1 |
| Chat trong phòng (chặn lộ đáp án) | 1 |
| Ghi log | 1 |

Thứ tự ưu tiên khi trễ tiến độ: socket + truyền dòng, tài khoản, phòng, nét vẽ, đoán và chấm điểm. Chat nâng cao và hiệu ứng UI cắt trước.

## 10. Quy trình làm việc

1. Đọc `docs/TASKS.md`, xác định **một task nhỏ** của phiên này.
2. **Nêu kế hoạch ngắn** (file nào sẽ đổi, hàm nào sẽ thêm) và chờ tôi đồng ý.
3. Viết test trước hoặc cùng lúc với code.
4. Chạy `make test` (và `make asan` nếu đụng bộ nhớ). **Báo kết quả thật**, kể cả khi lỗi.
5. Giải thích ngắn gọn code đã viết bằng tiếng Việt để tôi hiểu và bảo vệ được.
6. Đề xuất cập nhật `TASKS.md`, `protocol.md`, `DECISIONS.md` nếu có thay đổi.

Chỉ làm đúng phạm vi task. Phát hiện việc khác cần sửa thì ghi lại và hỏi, không tự làm thêm.

## 11. Git và commit

- **Không tự chạy `git commit`, `git push`, `git rebase`, `git reset --hard`, hay sửa lịch sử.** Tôi tự commit để đảm bảo yêu cầu: mỗi tuần tối thiểu 5 commit, mỗi commit cách nhau tối thiểu 30 phút.
- Cuối mỗi bước nhỏ, **đề xuất** nội dung commit message theo dạng `type(scope): mô tả`, ví dụ `feat(room): add join_room`, `test(auth): cover wrong password`. Các type: `feat`, `fix`, `test`, `docs`, `refactor`, `chore`.
- Mỗi bước nhỏ nên tách thành một commit riêng (code + test của nó), để lịch sử rõ ràng.

## 12. Điều KHÔNG được làm

- Không thêm thư viện hay công cụ ngoài danh sách ở mục 2 khi chưa hỏi.
- Không dùng thread hay `fork` trong server.
- Không đưa logic game vào UI Python.
- Không xóa, vô hiệu hóa hay nới lỏng test để làm CI xanh.
- Không hạ ngưỡng coverage, không tắt cảnh báo trình biên dịch.
- Không thay đổi `protocol.md` một cách âm thầm.
- Không bịa kết quả: nếu chưa chạy được lệnh hoặc không chắc chắn, hãy nói rõ.

## 13. Khi không chắc chắn

Hỏi tôi. Một câu hỏi ngắn tốt hơn một giả định sai kéo dài nhiều giờ.

## 14. Định nghĩa "hoàn thành" một task

- [ ] `make` không cảnh báo, `make test` xanh, `make asan` sạch
- [ ] Có unit test cho trường hợp đúng, sai và biên
- [ ] Coverage không giảm dưới 90%
- [ ] `protocol.md` và `ARCHITECTURE.md` khớp với code
- [ ] Đã thử tay bằng `nc` hoặc client (với tính năng liên quan mạng)
- [ ] Tôi đã đọc diff và hiểu code
- [ ] Đã có đề xuất commit message
