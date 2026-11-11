# Công nghệ sử dụng (Tech stack) netsketch

> Liệt kê **công nghệ đã chốt** cho từng thành phần, lý do chọn và quyết định liên quan.
> Nguồn gốc: `AGENTS.md` mục 2 và `docs/DECISIONS.md`. Khi hai nơi mâu thuẫn, `DECISIONS.md` là nơi ghi lý do, file này chỉ tổng hợp.
> Trạng thái: bản nháp v1.0, 11/11/2026.

## 1. Bảng tổng hợp

| Thành phần | Lựa chọn | Lý do | Quyết định |
|---|---|---|---|
| **Server** | C99 (gcc, Linux), `epoll` level-triggered, socket non-blocking, **một luồng, không thread, không `fork`** | Đạt điểm "cài đặt cơ chế vào/ra socket", chịu được khoảng 1000 kết nối, không có race condition | D-001, D-003 |
| **Mã dùng chung** | `common/`: `framing`, `protocol`, `sha256` (C99), biên dịch vào cả server và client | Hai phía dùng cùng một định dạng dòng, một bộ test | D-018 |
| **Client core** | Thư viện C `libnetsketch` (`.so`), 4 hàm: `ns_connect`, `ns_send_line`, `ns_recv_line`, `ns_close` | Phần mạng cốt lõi bằng C đúng yêu cầu đề, giữ ranh giới `ctypes` thật mỏng | D-001, D-019 |
| **UI** | Python 3 + **Tkinter**, gọi `libnetsketch` qua `ctypes`. Một luồng, đọc mạng bằng `root.after(20, poll)` | Tkinter có sẵn trong Python, vẽ canvas nhanh. Không thread nên không lỗi đồng bộ với Tkinter. UI không chứa logic game | D-001, D-019 |
| **Lưu tài khoản** | File văn bản `users.dat`, mỗi dòng `name:salt_hex:hash_hex`, nạp vào bảng băm khi khởi động, ghi nối khi đăng ký | Không thêm thư viện, dễ test bằng thư mục tạm | D-009 |
| **Bộ từ khóa** | `data/words.txt`, chữ không dấu, mỗi dòng một từ | So khớp ASCII không phân biệt hoa thường, đơn giản | D-014 |
| **Log** | File văn bản, tự viết module `logger` (mức `ERROR`/`WARN`/`INFO`/`DEBUG`, sink thay được) | Không phụ thuộc, test được nội dung log | `ARCHITECTURE.md` mục 10 |
| **Mật khẩu** | `SHA-256(salt ‖ password)`, salt 16 byte ngẫu nhiên, **tự cài** trong `common/sha256.c`, kiểm tra bằng test vector chuẩn | Không lưu mật khẩu thô, không thêm thư viện, không nghẽn một luồng khi đăng nhập hàng loạt | D-010 |
| **Unit test** | **cmocka** | Có sẵn cơ chế setup/teardown, assert phong phú cho C | D-024 |
| **Coverage** | `gcov` + **`gcovr`**, chặn nếu dưới 90% dòng (loại `main.c`) | `gcovr` gộp kết quả unit và integration, xuất HTML/XML cho CI | D-021, D-024 |
| **Kiểm tra bộ nhớ** | **ASan + UBSan** (mỗi push), **Valgrind** (hằng tuần) | Phát hiện tràn bộ đệm, rò rỉ, hành vi không xác định | `TESTING.md` mục 8 |
| **Integration test** | Python 3 + **pytest** + thư viện chuẩn `socket`, chạy server thật | Fixture dựng và tắt server gọn, đọc kết quả dễ | D-024 |
| **Load test** | `tools/loadgen.py`, Python 3 **asyncio**, tài khoản tạo sẵn | Viết nhanh, đủ tạo 100 đến 1000 bot, dễ đổi kịch bản | D-020 |
| **Biểu đồ kết quả tải** | Python **matplotlib** | Vẽ độ trễ, CPU, RSS cho báo cáo | D-024 |
| **Build** | GNU `make` | Đủ cho quy mô dự án, một lệnh cho mọi việc | `AGENTS.md` mục 3 |
| **CI** | GitHub Actions: build, kiểm tra quy ước, unit, integration, coverage (chặn dưới 90%), ASan | Tự động kiểm tra mỗi lần push | `TESTING.md` mục 10 |
| **Quản lý mã** | Git, GitHub. **Người làm tự commit** | Đảm bảo tối thiểu 5 commit mỗi tuần, cách nhau tối thiểu 30 phút | D-022 |

> [!NOTE]
> **pytest** và **matplotlib** chỉ dùng cho **kiểm thử và báo cáo**, không nằm trong sản phẩm chạy (server, `libnetsketch`, UI). Server và client không phụ thuộc thư viện Python bên ngoài nào.

## 2. Phân loại phụ thuộc

| Loại | Thành phần | Có trong sản phẩm nộp |
|---|---|---|
| Chạy server | gcc, glibc (Linux: `epoll`, socket POSIX) | Có |
| Chạy client | `libnetsketch.so`, Python 3, Tkinter (thư viện chuẩn) | Có |
| Build | GNU `make`, gcc | Có |
| Kiểm thử C | cmocka, gcov, gcovr, ASan/UBSan (đi kèm gcc), Valgrind | Không (chỉ môi trường dev và CI) |
| Kiểm thử Python | pytest | Không |
| Báo cáo | matplotlib | Không |

Không dùng: OpenSSL, libsodium, SQLite, PyQt, thư viện JSON, thư viện mạng bên thứ ba.

## 3. Cờ biên dịch

| Bản build | Cờ | Thư mục | Dùng cho |
|---|---|---|---|
| Thường | `-std=c99 -O2 -Wall -Wextra -Werror` | `build/` | Chạy hằng ngày, load test |
| Coverage | `-std=c99 -O0 -g --coverage -Wall -Wextra -Werror` | `build/cov/` | `make coverage` |
| ASan | `-std=c99 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror` | `build/asan/` | `make asan` |
| Thư viện client | thêm `-fPIC -shared` | `build/` | `libnetsketch.so` |

Load test **luôn dùng bản build thường**. Không bao giờ tắt cảnh báo để build qua (`AGENTS.md` mục 12).

## 4. Phương án đã cân nhắc và loại bỏ

Bảng phác thảo ban đầu của dự án có nhiều phương án "hoặc". Dưới đây là lựa chọn cuối cùng:

| Thành phần | Phương án ban đầu | Chọn | Lý do loại phương án còn lại |
|---|---|---|---|
| Server I/O | `epoll` hoặc `poll` | `epoll` level-triggered | `poll`/`select` không mở rộng tốt khi có 1000 fd; edge-triggered dễ sai khi phải đọc cạn bộ đệm (D-003) |
| UI | PyQt hoặc Tkinter | Tkinter | PyQt là thư viện ngoài, nặng, cần cài thêm; Tkinter có sẵn (D-019) |
| Lưu tài khoản | SQLite | File `users.dat` | SQLite thêm thư viện, thêm phần phải test và giải thích khi bảo vệ (D-009) |
| Mật khẩu | OpenSSL hoặc libsodium | SHA-256 tự cài | Thêm phụ thuộc; bcrypt/argon2 cố ý chậm nên nghẽn server một luồng khi đăng nhập hàng loạt (D-010) |
| Unit test | cmocka hoặc Unity | cmocka | Chọn một để thống nhất. cmocka có fixture setup/teardown sẵn |
| Coverage | `gcovr` hoặc `lcov` | `gcovr` | Một công cụ vừa in tóm tắt vừa chặn ngưỡng (`--fail-under-line`) vừa xuất HTML/XML |
| Load test | C hoặc Python asyncio | Python asyncio | Viết bằng C tốn thời gian của một người, đề không yêu cầu công cụ tải viết bằng C (D-020) |

## 5. Hạn chế đã biết của tech stack

| Hạn chế | Ảnh hưởng | Cách xử lý |
|---|---|---|
| Server một luồng | Chỉ dùng một lõi CPU | Handler phải ngắn, không chặn. Đo CPU khi load test (D-003) |
| SHA-256 không phải hàm băm chậm | Yếu hơn bcrypt/argon2 trước tấn công dò mật khẩu ngoại tuyến | Ghi vào mục "Hạn chế" của báo cáo (D-010) |
| Ghi `users.dat` đồng bộ | Chặn vòng lặp một chút khi đăng ký | Load test dùng tài khoản tạo sẵn (D-009, D-020) |
| `loadgen` bằng Python | Có thể thành nút cổ chai ở tải rất cao | Theo dõi CPU máy tạo tải, chia nhiều tiến trình hoặc chạy máy khác (D-020) |
| Chỉ chạy trên Linux | Phụ thuộc `epoll` | Chấp nhận, môi trường demo là Linux |

## 6. Phiên bản môi trường

> Ghi nhận thực tế ngày 11/11/2026 trên máy phát triển.

| Công cụ | Phiên bản |
|---|---|
| Hệ điều hành, nhân Linux | Ubuntu 24.04, Linux 7.0.0-42-generic |
| gcc | 13.3.0 |
| GNU make | 4.3 |
| cmocka | 1.1.7-3 |
| gcovr | 7.0 |
| Valgrind | 3.22.0 |
| Python 3 | 3.12.3 |
| Tkinter (Tk) | 8.6 |
| pytest | 7.4.4 |
| matplotlib | (cài khi chạy báo cáo tải ở tuần 5–6) |

## 7. Quy tắc thêm công nghệ mới

1. Không thêm thư viện hay công cụ ngoài bảng ở mục 1 khi chưa được đồng ý (`AGENTS.md` mục 2 và 12).
2. Khi được đồng ý: thêm một mục `D-xxx` vào `DECISIONS.md` trước, rồi cập nhật file này và `AGENTS.md` mục 2.
3. Phần mạng cốt lõi (server, `libnetsketch`) luôn viết bằng C.

## 8. Việc cần đồng bộ với tài liệu khác

| Việc | Tài liệu | Ghi chú |
|---|---|---|
| Thêm pytest, matplotlib (công cụ test và báo cáo), GNU `make`, Valgrind, ASan/UBSan vào danh sách công nghệ | `AGENTS.md` mục 2 | Đã đồng bộ (11/11/2026) |
| Ghi rõ băm mật khẩu bằng `common/sha256` tự cài | `AGENTS.md` mục 2 | Đã đồng bộ (11/11/2026) |
| Thêm thư mục `common/` | `AGENTS.md` mục 4 | Đã đồng bộ (11/11/2026) |
| Thêm `docs/TECKSTACK.md` vào danh sách tài liệu | `AGENTS.md` mục 5 | Đã đồng bộ (11/11/2026) |
| Ghi quyết định chọn cmocka thay Unity, gcovr thay lcov, thêm pytest và matplotlib | `DECISIONS.md` | Đã thêm D-024 (11/11/2026) |
