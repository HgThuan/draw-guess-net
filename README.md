# netsketch

**netsketch** là trò chơi vẽ và đoán từ khóa thời gian thực qua mạng (lấy cảm hứng từ *Skribbl.io*), được xây dựng trong khuôn khổ đồ án môn **Lập trình mạng**.

Trong mỗi phòng chơi, người chơi lần lượt đảm nhận vai trò họa sĩ để vẽ từ khóa được chọn, trong khi những người chơi còn lại dự đoán từ khóa trong thời gian giới hạn. Đoán đúng càng sớm, điểm số nhận được càng cao.

---

## 1. Tính năng chính

- **Truyền dòng & I/O đa hợp**: Bộ đệm ghép dòng (`framing`) xử lý phân mảnh/dính gói TCP; server đơn luồng sử dụng `epoll` (level-triggered) với non-blocking sockets và kỹ thuật gộp ghi (*write coalescing*).
- **Tài khoản & Phiên**: Đăng ký, đăng nhập với mật khẩu băm `SHA-256` kèm `salt` ngẫu nhiên lưu trong `users.dat`; quản lý phiên gắn với kết nối TCP, chặn đăng nhập trùng tài khoản và tự động thu hồi phiên treo quá 120 giây.
- **Quản lý phòng chơi**: Tạo phòng với thông số tùy chỉnh (số người, số vòng, thời gian vẽ), liệt kê danh sách phòng có phân trang, vào/rời phòng và tự động chuyển quyền chủ phòng.
- **Luân phiên lượt & Truyền nét vẽ thời gian thực**: Chọn từ khóa từ 3 gợi ý (tự chọn ngẫu nhiên nếu hết giờ), chuẩn hóa tọa độ `0..1000` và phát sóng nét vẽ (`STROKE`, `CLEAR`) tới toàn phòng.
- **Đoán từ & Chấm điểm**: So khớp đáp án không phân biệt hoa thường, tính điểm theo thời gian còn lại và thứ tự đoán đúng, tổng hợp bảng xếp hạng sau mỗi lượt và cuối ván.
- **Chat & Chặn lộ đáp án**: Hỗ trợ trò chuyện trong phòng, chặn tin nhắn chứa từ khóa của người chưa đoán đúng và tách kênh chat riêng cho nhóm người chơi đã đoán đúng.
- **Xử lý ngoại lệ & Ghi log**: Xử lý mượt mà khi họa sĩ hoặc người đoán mất kết nối giữa chừng; ghi log sự kiện theo cấp độ (`ERROR`, `WARN`, `INFO`, `DEBUG`) đảm bảo che mật khẩu.

---

## 2. Công nghệ sử dụng

Chi tiết lý do lựa chọn và các phương án đã cân nhắc xem tại [docs/TECKSTACK.md](docs/TECKSTACK.md) và [docs/DECISIONS.md](docs/DECISIONS.md).

| Thành phần | Công nghệ |
|---|---|
| **Server** | **C99** (`gcc`, Linux), `epoll` level-triggered, non-blocking sockets, **đơn luồng** (không dùng `pthread` hay `fork`) |
| **Mã dùng chung (`common/`)** | **C99**: Bộ đệm ghép dòng (`framing`), phân tích giao thức (`protocol`), băm mật khẩu (`sha256`) |
| **Thư viện Client** | **C99** (`libnetsketch.so`): Kết nối TCP, gửi và nhận dòng văn bản |
| **Giao diện (UI)** | **Python 3 + Tkinter**: Gọi `libnetsketch.so` qua `ctypes`, đọc sự kiện bằng vòng lặp polling (`root.after`), không chứa logic game |
| **Lưu trữ** | File văn bản: `users.dat` (tài khoản), `data/words.txt` (từ khóa), file log |
| **Kiểm thử & Chất lượng** | **cmocka** (Unit test), **pytest** (Integration test), **gcov + gcovr** (Coverage $\ge 90\%$), **ASan/UBSan & Valgrind** (Kiểm tra bộ nhớ) |
| **Kiểm thử chịu tải** | **Python 3 (`asyncio`)** (`tools/loadgen.py`) + **matplotlib** (xuất biểu đồ báo cáo cho kịch bản 100 – 1000 clients) |
| **CI** | **GitHub Actions** |

---

## 3. Kiến trúc tổng quan

Hệ thống tách biệt hoàn toàn **tầng mạng có I/O** (`net_loop`) và **tầng nghiệp vụ thuần** (`framing`, `protocol`, `dispatch`, `auth`, `session`, `room`, `game`, `scoring`, `chat`, `words`). Các phụ thuộc ngoại vi như đồng hồ (`Clock`), bộ sinh số ngẫu nhiên (`Rng`) và đầu ra log (`sink`) được tiêm vào cấu trúc `Server` để phục vụ kiểm thử đơn vị xác định (*deterministic unit testing*).

```
            Python UI (Tkinter)  <--ctypes-->  libnetsketch.so (C)  ---+
                                                                       | TCP
  loadgen.py (asyncio), nc, pytest client  ----------------------------+
                                                                       v
 +------------------------------ netsketch-server -------------------------------+
 |  TẦNG MẠNG (I/O)                                                              |
 |  net_loop: epoll_wait -> accept / recv / send / close, server_tick định kỳ    |
 |        | byte thô                                      ^ dòng đã đóng gói     |
 |        v                                               |                      |
 |  --------------------------- ranh giới I/O ------------------------------     |
 |  TẦNG LOGIC (thuần, không phụ thuộc socket)                                   |
 |  framing -> protocol(parse) -> dispatch(CmdSpec) -> handlers                  |
 |        +---> auth / session / room / game / scoring / chat / words            |
 +-------------------------------------------------------------------------------+
```

Xem chi tiết tại [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

---

## 4. Cấu trúc thư mục

```text
netsketch/
├── common/               # Mã C dùng chung cho cả server và client (framing, protocol, sha256)
├── server/src/           # Mã nguồn server (main, net_loop, conn, dispatch, auth, session, room, game, ...)
├── client/
│   ├── libnetsketch/     # Thư viện C mỏng (.so) giao tiếp mạng
│   └── ui/               # Giao diện người dùng Python Tkinter
├── tests/
│   ├── unit/             # Unit test bằng cmocka
│   └── integration/      # Integration & robustness test bằng pytest
├── tools/                # Công cụ tạo tải (loadgen.py) và kiểm tra quy ước commit
├── data/                 # Bộ từ khóa mẫu (words.txt)
└── docs/                 # Tài liệu giao thức, kiến trúc, kiểm thử, quyết định và công việc
```

---

## 5. Yêu cầu môi trường & Cài đặt

Dự án yêu cầu môi trường **Linux** (hỗ trợ `epoll`). Cài đặt các công cụ biên dịch và kiểm thử trên Ubuntu/Debian:

```bash
sudo apt update
sudo apt install -y build-essential libcmocka-dev gcovr valgrind \
                    python3 python3-tk python3-pytest python3-matplotlib
```

---

## 6. Hướng dẫn biên dịch, kiểm thử và chạy

### Biên dịch và kiểm thử

```bash
make                 # Biên dịch server (build/netsketch-server) và thư viện client (build/libnetsketch.so)
make test            # Chạy toàn bộ unit test (cmocka)
make itest           # Chạy integration test qua TCP thật (pytest)
make check           # Chạy unit test + integration test + kiểm tra quy ước kiến trúc
make coverage        # Đo độ phủ mã nguồn (yêu cầu >= 90% dòng, loại trừ main.c)
make asan            # Biên dịch và chạy bộ test với AddressSanitizer + UBSan
make clean           # Xóa thư mục build/
```

### Chạy Server và Client

1. **Khởi chạy server** (mặc định lắng nghe cổng `9000`):
   ```bash
   ./build/netsketch-server --port 9000
   ```

2. **Thử nghiệm nhanh giao thức bằng `netcat`**:
   ```bash
   nc localhost 9000
   # < HELLO|netsketch|1
   # > PING
   # < PONG
   # > REGISTER|alice|secret123
   # < OK
   # > LOGIN|alice|secret123
   # < OK|<session_id>
   ```

3. **Chạy giao diện người dùng (UI)**:
   ```bash
   python3 client/ui/netsketch.py --host 127.0.0.1 --port 9000
   ```

4. **Chạy kiểm thử chịu tải (Load test)**:
   ```bash
   python3 tools/loadgen.py --clients 100 --port 9000
   ```


