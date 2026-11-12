# Nhật ký quyết định (DECISIONS)

> Ghi lại **vì sao** dự án chọn như hiện tại. Đây cũng là nguồn chính để viết mục "Thiết kế và lựa chọn kỹ thuật" trong báo cáo và để trả lời câu hỏi khi bảo vệ.
> Tài liệu liên quan: `AGENTS.md`, `docs/protocol.md`, `docs/ARCHITECTURE.md`.

## Cách dùng file này

- Mỗi quyết định có một mã `D-xxx`, **đánh số tăng dần, không đổi số**.
- Chỉ **thêm mới**, không xóa. Khi đổi ý, thêm quyết định mới và đổi trạng thái quyết định cũ thành `Thay thế bởi D-yyy`.
- Thêm một mục khi: chọn giữa hai hướng đáng kể, đổi giao thức, thay công nghệ, chấp nhận một hạn chế, hoặc cắt giảm tính năng.
- Mỗi mục gồm: **Bối cảnh**, **Quyết định**, **Phương án đã cân nhắc**, **Hệ quả** (cả lợi và hại).
- Trạng thái: `Chấp nhận`, `Đề xuất` (chờ xác nhận), `Thay thế bởi D-yyy`, `Bỏ`.

## Mẫu cho mục mới

```markdown
### D-0xx: Tiêu đề ngắn
- **Ngày:** dd/mm/yyyy    **Trạng thái:** Chấp nhận
- **Bối cảnh:** vấn đề cần giải quyết, ràng buộc.
- **Quyết định:** chọn gì.
- **Phương án đã cân nhắc:** A (vì sao không), B (vì sao không).
- **Hệ quả:** lợi, hại, việc phải làm tiếp.
```

## Mục lục

| Mã | Quyết định | Trạng thái |
|---|---|---|
| D-001 | Ngôn ngữ: C cho phần mạng, Python cho UI | Chấp nhận |
| D-002 | Đề tài và phạm vi: trò chơi vẽ và đoán hình, làm một mình | Chấp nhận |
| D-003 | Server một luồng, `epoll` level-triggered | Chấp nhận |
| D-004 | Giao thức dạng text, mỗi dòng một thông điệp | Chấp nhận |
| D-005 | Tách I/O khỏi logic, dùng các điểm tiêm để kiểm thử | Chấp nhận |
| D-006 | Điều phối lệnh theo bảng `CmdSpec`, thứ tự kiểm tra lỗi cố định | Chấp nhận |
| D-007 | `ConnId` có số thế hệ | Chấp nhận |
| D-008 | Gộp ghi cuối vòng lặp, đóng client quá chậm | Chấp nhận |
| D-009 | Lưu tài khoản bằng file `users.dat` | Chấp nhận |
| D-010 | Băm mật khẩu SHA-256 có salt, tự cài | Chấp nhận |
| D-011 | Phiên gắn với kết nối, không nối lại phiên | Chấp nhận |
| D-012 | Chỉ vào phòng khi phòng ở sảnh | Chấp nhận |
| D-013 | `players[0]` luôn là chủ phòng | Chấp nhận |
| D-014 | Từ khóa không dấu, so khớp ASCII không phân biệt hoa thường | Chấp nhận |
| D-015 | Tọa độ chuẩn hóa 0 đến 1000, nét vẽ là polyline, không phản hồi | Chấp nhận |
| D-016 | Công thức chấm điểm | Chấp nhận |
| D-017 | Quy tắc chat và chặn lộ đáp án | Chấp nhận |
| D-018 | Thư mục `common/` dùng chung server và client | Chấp nhận |
| D-019 | UI Tkinter một luồng, đọc mạng bằng polling | Chấp nhận |
| D-020 | Load test bằng Python `asyncio`, tài khoản tạo sẵn | Chấp nhận |
| D-021 | Coverage 90% tính cả `net_loop` qua integration test | Chấp nhận |
| D-022 | Quy trình agent: không tự commit | Chấp nhận |
| D-023 | Đóng băng tính năng ngày 14/12, tính năng tùy chọn để sau | Chấp nhận |
| D-024 | Chốt công cụ kiểm thử và báo cáo: cmocka, gcovr, pytest, matplotlib | Chấp nhận |
| D-025 | Quản lý tài liệu thiết kế và quy ước ở nhánh riêng, không commit vào nhánh code | Chấp nhận |

---

## Quyết định

### D-001: Ngôn ngữ: C cho phần mạng, Python cho UI
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** Đề yêu cầu công nghệ mạng cốt lõi bằng C, giao diện dùng ngôn ngữ bất kỳ.
- **Quyết định:** Server và thư viện client bằng C99 (gcc, Linux). UI bằng Python 3 + Tkinter, gọi thư viện C qua `ctypes`.
- **Phương án đã cân nhắc:** UI bằng C với GTK hoặc SDL (tốn thời gian hơn nhiều với một người, ít điểm cộng); UI web với WebSocket (phải thêm tầng WebSocket, lệch khỏi trọng tâm môn học).
- **Hệ quả:** Vẽ canvas nhanh bằng Tkinter. Phải duy trì ranh giới `ctypes`, nên thư viện C chỉ có vài hàm (xem D-019).

### D-002: Đề tài và phạm vi: trò chơi vẽ và đoán hình, làm một mình
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** Các đề trong file `mmt.xlsx` hầu hết đã có nhóm. Chọn đề tự đề xuất (đề 16), dự án làm một người, hạn nộp 22/12.
- **Quyết định:** Làm trò chơi vẽ và đoán hình (kiểu Skribbl) theo thang 20 điểm đã chia ở `AGENTS.md` mục 9. Giữ đủ tính năng, đơn giản hóa cách làm thay vì cắt tính năng.
- **Phương án đã cân nhắc:** Đề 10 (thi trắc nghiệm, còn một chỗ): ít thách thức hơn; đề IoT (đèn giao thông): ít tương tác thời gian thực.
- **Hệ quả:** Phần khó nhất là truyền nét vẽ thời gian thực và tải. Khi trễ tiến độ, cắt chat nâng cao và hiệu ứng UI trước (thứ tự ưu tiên ở `AGENTS.md` mục 9).

### D-003: Server một luồng, `epoll` level-triggered
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** Cần xử lý nhiều kết nối (mục tiêu 1000) và chứng minh cơ chế vào/ra socket (2 điểm).
- **Quyết định:** Một luồng, một vòng lặp `epoll`, socket non-blocking, level-triggered. Không thread, không khóa.
- **Phương án đã cân nhắc:** Thread cho mỗi kết nối (tốn bộ nhớ, phải đồng bộ trạng thái phòng, khó test); thread pool (phức tạp, dễ có race); `epoll` edge-triggered (nhanh hơn chút nhưng dễ sai khi phải đọc cạn bộ đệm); `select`/`poll` (không mở rộng tốt bằng).
- **Hệ quả:** Không có race condition, dễ debug. Chỉ dùng một lõi CPU, nên nút cổ chai (nếu có) sẽ là CPU của một lõi. Mọi thao tác trong handler phải ngắn, không chặn.

### D-004: Giao thức dạng text, mỗi dòng một thông điệp
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** Cần giao thức đơn giản, dễ debug, có "xử lý truyền dòng" (1 điểm).
- **Quyết định:** `LENH|tham_so|...\n`, tối đa 512 byte, UTF-8. Trường văn bản tự do luôn đứng cuối. Chi tiết trong `protocol.md`.
- **Phương án đã cân nhắc:** Nhị phân với độ dài đứng trước (gọn hơn nhưng khó debug bằng `nc`, khó viết test); JSON (cần thư viện phân tích trong C, nặng hơn).
- **Hệ quả:** Debug bằng `nc`, test bằng chuỗi. Tốn băng thông hơn nhị phân (chấp nhận được). Phải xử lý dòng bị cắt đôi và dính nhau (module `framing`).

### D-005: Tách I/O khỏi logic, dùng các điểm tiêm để kiểm thử
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** Yêu cầu coverage trên 90%. Mã gắn chặt với socket và thời gian thật rất khó test.
- **Quyết định:** Logic không biết socket. Đồng hồ (`Clock`), số ngẫu nhiên (`Rng`), logger (`sink`) được tiêm vào. Đầu ra của logic là bộ đệm gửi của kết nối (`conn->out`).
- **Phương án đã cân nhắc:** Mock hàm hệ thống bằng linker (`--wrap`): phức tạp, dễ gãy; test mọi thứ qua socket thật (chậm, khó xác định lỗi, khó đo coverage nhánh lỗi).
- **Hệ quả:** Unit test nhanh và xác định. Phải kỷ luật khi viết code: không include header mạng trong module logic (CI kiểm tra bằng `grep`).

### D-006: Điều phối lệnh theo bảng `CmdSpec`, thứ tự kiểm tra lỗi cố định
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** Mỗi lệnh cần kiểm tra đăng nhập, trạng thái phòng, vai trò. Viết rải rác trong từng handler dễ sai và lệch nhau.
- **Quyết định:** Một bảng `CmdSpec` khai báo số tham số, yêu cầu đăng nhập, yêu cầu ở trong phòng, trạng thái phòng cho phép. `dispatch` kiểm tra theo thứ tự cố định (cú pháp, đăng nhập, phòng, trạng thái, vai trò, giá trị), handler chỉ lo vai trò và giá trị.
- **Phương án đã cân nhắc:** Mỗi handler tự kiểm tra (lặp mã, dễ lệch thứ tự lỗi).
- **Hệ quả:** Một đầu vào luôn cho cùng một mã lỗi, test dễ viết. Thêm lệnh mới phải nhớ thêm một dòng vào bảng.

### D-007: `ConnId` có số thế hệ
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** Slot kết nối được tái dùng. Phòng và `User.online` giữ tham chiếu tới kết nối, có thể trỏ nhầm sang kết nối mới sau khi kết nối cũ đóng.
- **Quyết định:** `ConnId = {chỉ số, thế hệ}`. Tăng thế hệ mỗi khi giải phóng slot, so sánh cả hai khi tra cứu.
- **Phương án đã cân nhắc:** Con trỏ trần (nguy cơ dùng sau khi giải phóng); `fd` làm định danh (`fd` cũng được tái dùng).
- **Hệ quả:** Tránh một nhóm lỗi khó tìm. Tốn thêm một trường 16 bit.

### D-008: Gộp ghi cuối vòng lặp, đóng client quá chậm
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** Phát nét vẽ cho cả phòng tạo nhiều lần ghi nhỏ. Client chậm có thể làm đầy bộ nhớ nếu server giữ dữ liệu vô hạn.
- **Quyết định:** Handler chỉ ghi vào `conn->out`. Cuối mỗi vòng lặp mới gọi `send` một lần cho mỗi kết nối. `OUT_BUF` = 16 KB, vượt thì đóng kết nối và ghi log.
- **Phương án đã cân nhắc:** `send` ngay mỗi lần (nhiều syscall); hàng đợi không giới hạn (rủi ro cạn bộ nhớ); bỏ gói cũ của client chậm (làm hỏng thứ tự thông điệp, nguy hiểm với giao thức có trạng thái).
- **Hệ quả:** Ít syscall hơn khi tải cao. Có thêm độ trễ tối đa một vòng lặp (dưới 200 ms, thường rất nhỏ). Client chậm bị ngắt, chấp nhận được.

### D-009: Lưu tài khoản bằng file `users.dat`
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** Cần lưu tài khoản bền vững qua lần khởi động lại. Làm một mình, cần giảm phụ thuộc.
- **Quyết định:** File văn bản, mỗi dòng `name:salt_hex:hash_hex`, nạp vào bảng băm khi khởi động, ghi nối khi đăng ký.
- **Phương án đã cân nhắc:** SQLite (mạnh nhưng thêm thư viện, thêm phần phải test và giải thích); chỉ lưu trong RAM (mất tài khoản khi khởi động lại).
- **Hệ quả:** Đơn giản, dễ test bằng thư mục tạm. Ghi đồng bộ nên chặn vòng lặp một chút khi đăng ký (khi load test phải tạo sẵn tài khoản, xem D-020). Không chịu được nhiều tiến trình cùng ghi (không cần thiết ở đây).

### D-010: Băm mật khẩu SHA-256 có salt, tự cài
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** Không lưu mật khẩu thô. 1000 bot đăng nhập cùng lúc trên một luồng không được làm nghẽn server.
- **Quyết định:** `SHA-256(salt || password)`, salt 16 byte ngẫu nhiên, cài đặt trong `common/sha256.c` và kiểm tra bằng test vector chuẩn.
- **Phương án đã cân nhắc:** OpenSSL hoặc libsodium với bcrypt, argon2 (an toàn hơn trước dò mật khẩu nhưng thêm phụ thuộc, và cố ý chậm nên nghẽn một luồng khi đăng nhập hàng loạt).
- **Hệ quả:** Không phụ thuộc thư viện ngoài. **Hạn chế đã biết:** không phải thuật toán băm chậm chuyên dụng, yếu hơn trước tấn công dò mật khẩu ngoại tuyến. Ghi rõ trong báo cáo mục "Hạn chế".

### D-011: Phiên gắn với kết nối, không nối lại phiên
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** Đề yêu cầu "quản lý phiên" (2 điểm). Nối lại phiên sau khi mất kết nối làm tăng nhiều độ phức tạp (giữ ghế trong ván, hết hạn).
- **Quyết định:** Phiên bắt đầu khi `LOGIN` thành công và kết thúc khi đóng kết nối. Mỗi tài khoản một kết nối (mã `114`). Hết hạn sau 120 giây không có dữ liệu. `session_id` sinh ngẫu nhiên chỉ để ghi log.
- **Phương án đã cân nhắc:** Token và lệnh `RESUME` (đáng làm nếu thừa thời gian, nhưng kéo theo cơ chế giữ chỗ người chơi trong ván).
- **Hệ quả:** Mất kết nối đồng nghĩa với rời phòng. Đơn giản và dễ test. Nếu muốn nối lại phiên, phải thêm quyết định mới.

### D-012: Chỉ vào phòng khi phòng ở sảnh
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** Cho vào giữa ván phải xử lý điểm, thứ tự vẽ, trạng thái canvas cho người mới.
- **Quyết định:** `JOIN_ROOM` chỉ hợp lệ khi phòng ở `LOBBY`, ngược lại trả `203`.
- **Phương án đã cân nhắc:** Vào giữa ván (giống Skribbl thật, nhưng phải gửi lại toàn bộ nét vẽ đã có và cập nhật thứ tự vẽ).
- **Hệ quả:** Đơn giản hơn nhiều, người đến muộn phải chờ ván sau. Có thể mở rộng sau.

### D-013: `players[0]` luôn là chủ phòng
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** Khi chủ phòng rời, cần chuyển quyền cho người vào phòng sớm nhất còn lại.
- **Quyết định:** Mảng `players` giữ thứ tự vào phòng, người tạo là phần tử đầu. Khi xóa người chơi thì dồn mảng, `players[0]` mới là chủ phòng.
- **Phương án đã cân nhắc:** Trường `host` riêng (phải giữ đồng bộ với danh sách).
- **Hệ quả:** Không có trạng thái thừa. Phải giữ đúng thứ tự khi xóa (có test riêng).

### D-014: Từ khóa không dấu, so khớp ASCII không phân biệt hoa thường
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** Người dùng gõ tiếng Việt có thể có nhiều dạng mã hóa dấu (dựng sẵn hoặc tổ hợp). So khớp chính xác phức tạp.
- **Quyết định:** `words.txt` dùng chữ không dấu. So khớp sau khi cắt khoảng trắng đầu và cuối, so sánh không phân biệt hoa thường chỉ với ASCII.
- **Phương án đã cân nhắc:** Chuẩn hóa Unicode (NFC) và bỏ dấu (nặng, cần thư viện hoặc bảng riêng, rủi ro lỗi).
- **Hệ quả:** Đơn giản, dễ test. Người chơi phải gõ không dấu. Từ khóa chỉ gồm chữ cái và dấu cách.

### D-015: Tọa độ chuẩn hóa 0 đến 1000, nét vẽ là polyline, không phản hồi
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** Nét vẽ là lưu lượng lớn nhất. Mỗi client có kích thước canvas khác nhau.
- **Quyết định:** Tọa độ chuẩn hóa 0 đến 1000. Một `STROKE` chứa 2 đến 40 điểm. `STROKE` và `CLEAR` không có phản hồi khi thành công, chỉ trả `ERR` khi lỗi.
- **Phương án đã cân nhắc:** Mỗi điểm một thông điệp (quá nhiều dòng, nghẽn); có `OK` cho mỗi `STROKE` (nhân đôi lưu lượng).
- **Hệ quả:** Giảm lưu lượng, hiển thị đúng ở mọi kích thước. Client không biết một nét có tới nơi hay không, nhưng TCP đã đảm bảo thứ tự và chuyển giao.

### D-016: Công thức chấm điểm
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** Cần cách chấm điểm thưởng đoán nhanh và thưởng người vẽ khi nhiều người đoán đúng.
- **Quyết định:** Người đoán thứ `k`: `max(50, 100 + 400 x còn_lại / draw_time - 25 x (k - 1))`. Người vẽ: tổng điểm người đoán đúng chia cho (số người trong phòng - 1). Chia nguyên làm tròn xuống. Chi tiết và ví dụ ở `protocol.md` mục 8.
- **Phương án đã cân nhắc:** Điểm cố định mỗi lần đoán đúng (không thưởng tốc độ); người vẽ nhận điểm cố định (không khuyến khích vẽ dễ đoán).
- **Hệ quả:** Có đủ trường hợp biên để kiểm thử (sàn 50, không ai đoán đúng). Hằng số (100, 400, 25, 50) là tham số cân bằng game, có thể chỉnh trong `config.h`.

### D-017: Quy tắc chat và chặn lộ đáp án
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** Đề có yêu cầu chat trong phòng, nhưng không để người chơi thông đồng đáp án.
- **Quyết định:** Người vẽ không chat trong lượt của mình. Người chưa đoán đúng mà chat có chứa từ khóa thì bị chặn (`306`). Người đã đoán đúng chỉ nói chuyện được với người vẽ và những người cũng đã đoán đúng.
- **Phương án đã cân nhắc:** Chỉ chặn khi tin nhắn đúng hoàn toàn bằng từ khóa (dễ vượt qua bằng cách thêm chữ); gộp chung chat và đoán (khó phân biệt ý định, khó test).
- **Hệ quả:** Chặn theo chuỗi con có thể chặn nhầm một câu vô tình chứa từ khóa ngắn. Chấp nhận, vì ưu tiên giữ tính công bằng.

### D-018: Thư mục `common/` dùng chung server và client
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** `framing` và `protocol` cần giống nhau ở hai phía. Viết hai bản sẽ dễ lệch và phải test hai lần.
- **Quyết định:** Đặt `framing`, `protocol`, `sha256` trong `common/`, biên dịch vào cả server và `libnetsketch`.
- **Phương án đã cân nhắc:** Mỗi bên tự viết (lặp mã, dễ lệch định dạng); thư viện tĩnh riêng (thêm bước build, không cần thiết cho quy mô này).
- **Hệ quả:** Một bộ test, hai nơi dùng.

### D-019: UI Tkinter một luồng, đọc mạng bằng polling
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** Tkinter không an toàn khi cập nhật giao diện từ thread khác.
- **Quyết định:** Không dùng thread. `root.after(20, poll)` gọi `ns_recv_line` với thời gian chờ 0 cho đến khi hết dòng. Thư viện C chỉ có 4 hàm (`ns_connect`, `ns_send_line`, `ns_recv_line`, `ns_close`). Logic game không nằm trong UI.
- **Phương án đã cân nhắc:** Thread đọc mạng và hàng đợi gửi sang luồng UI (phức tạp hơn, dễ lỗi đồng bộ).
- **Hệ quả:** Đơn giản, ít lỗi. Độ trễ hiển thị tối đa khoảng 20 ms, chấp nhận được với game vẽ.

### D-020: Load test bằng Python `asyncio`, tài khoản tạo sẵn
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** Cần tạo hàng trăm đến 1000 client. Viết công cụ này bằng C tốn nhiều thời gian của một người.
- **Quyết định:** `tools/loadgen.py` dùng `asyncio`. Mỗi bot đăng nhập, vào phòng, vẽ và đoán. Tài khoản được tạo sẵn trước khi đo để không đo nhầm chi phí ghi `users.dat`. Đo p50, p95, p99, throughput, tỉ lệ lỗi, CPU và RAM.
- **Phương án đã cân nhắc:** Công cụ C (nhanh hơn, nhưng yêu cầu của đề là có bài test chịu tải, không yêu cầu công cụ viết bằng C).
- **Hệ quả:** Viết nhanh, dễ chỉnh kịch bản. Bản thân công cụ Python có thể trở thành nút cổ chai ở tải rất cao, nên khi đo cần kiểm tra CPU của máy tạo tải (chạy trên máy khác nếu có thể) và ghi chú trong báo cáo.

### D-021: Coverage 90% tính cả `net_loop` qua integration test
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** `net_loop.c` gắn chặt với `epoll`, khó unit test. Nếu loại ra khỏi coverage thì con số 90% kém ý nghĩa.
- **Quyết định:** Đo coverage dòng. Loại `main.c`. Biên dịch server với `--coverage` khi chạy `make coverage`, integration test khởi chạy server thật rồi gửi `SIGTERM`. Server thoát bình thường nên ghi được dữ liệu `gcov`, `gcovr` gộp với kết quả unit test. CI chặn nếu dưới 90% từ tuần 2.
- **Phương án đã cân nhắc:** Loại luôn `net_loop.c` (con số đẹp nhưng không trung thực); mock `epoll` (phức tạp, ít giá trị).
- **Hệ quả:** Con số coverage phản ánh đúng thực tế. Bắt buộc phải có cơ chế tắt server an toàn (đã đưa vào `ARCHITECTURE.md` mục 6). Integration test chậm hơn unit test.

### D-022: Quy trình agent: không tự commit
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** Đề yêu cầu mỗi tuần tối thiểu 5 commit, giãn cách tối thiểu 30 phút, thể hiện đóng góp thật. Dự án có dùng agent hỗ trợ.
- **Quyết định:** Agent chỉ đề xuất nội dung commit message, **bạn tự commit** sau khi đọc diff và hiểu code. Mỗi bước nhỏ một commit.
- **Phương án đã cân nhắc:** Để agent tự commit (lịch sử bất thường, vi phạm giãn cách, khó chứng minh đóng góp).
- **Hệ quả:** Lịch sử commit phản ánh đúng nhịp làm việc. Cần hỏi giáo viên về quy định dùng AI và ghi rõ trong báo cáo phần có sự hỗ trợ.

### D-023: Đóng băng tính năng ngày 14/12, tính năng tùy chọn để sau
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** Tuần cuối cần dành cho load test, tối ưu, báo cáo và slide.
- **Quyết định:** Sau 14/12 chỉ sửa lỗi và hoàn thiện. Các mục tùy chọn (`HINT`, giới hạn tốc độ lệnh, nối lại phiên, chuyển danh sách phòng sang bảng băm) chỉ làm nếu đã xong mọi việc bắt buộc.
- **Phương án đã cân nhắc:** Tiếp tục thêm tính năng đến sát hạn (rủi ro không kịp báo cáo và test).
- **Hệ quả:** Ít rủi ro cuối kỳ. Một số cải tiến sẽ chuyển thành mục "Hướng phát triển" trong báo cáo.

### D-024: Chốt công cụ kiểm thử và báo cáo: cmocka, gcovr, pytest, matplotlib
- **Ngày:** 11/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** Bản phác thảo công nghệ ban đầu đưa ra các lựa chọn "cmocka hoặc Unity", "gcovr hoặc lcov", và chưa ghi rõ công cụ chạy integration test cũng như vẽ biểu đồ tải.
- **Quyết định:** Chọn `cmocka` cho unit test C, `gcovr` cho báo cáo và kiểm tra ngưỡng coverage, `pytest` cho integration test, `matplotlib` để vẽ biểu đồ kết quả tải cho báo cáo.
- **Phương án đã cân nhắc:** Unity (ít tiện ích setup/teardown hơn cmocka); `lcov` (cần thêm bước chuyển đổi, trong khi `gcovr` vừa in tóm tắt vừa kiểm tra `--fail-under-line` vừa xuất HTML/XML).
- **Hệ quả:** Thống nhất một bộ công cụ duy nhất trong `Makefile` và CI. `pytest` và `matplotlib` chỉ phục vụ kiểm thử và báo cáo, không nằm trong sản phẩm chạy.

### D-025: Quản lý tài liệu thiết kế và quy ước ở nhánh riêng, không commit vào nhánh code
- **Ngày:** 12/11/2026    **Trạng thái:** Chấp nhận
- **Bối cảnh:** Thư mục `docs/` và file `AGENTS.md` chứa tài liệu hướng dẫn và theo dõi tiến độ của đồ án, không phải mã nguồn chạy hay giao diện người dùng. Cần tách biệt để nhánh code (`main`) sạch sẽ và chỉ chứa sản phẩm bàn giao kỹ thuật.
- **Quyết định:** Toàn bộ tài liệu `docs/` và `AGENTS.md` được lưu trữ riêng biệt tại nhánh `future/docs-design-rule` (sẽ được xóa khi dự án hoàn thành). Trên nhánh `main` và các nhánh phát triển, cấu hình loại trừ qua `.git/info/exclude`. Mọi commit trên nhánh code tuyệt đối không được chứa `docs/` hay `AGENTS.md`.
- **Phương án đã cân nhắc:** Commit `docs/` chung vào `main` (làm loãng lịch sử commit mã nguồn).
- **Hệ quả:** Nhánh `main` giữ đúng cấu trúc nộp bài sạch sẽ, tài liệu vẫn được lưu trữ trên Git ở nhánh riêng và giữ cục bộ trên máy để tra cứu.

---

## Việc đang chờ

| Việc | Liên quan | Ghi chú |
|---|---|---|
| Hỏi giáo viên về quy định dùng AI | D-022 | Ghi kết quả vào mục này và vào báo cáo |
| Xác nhận ngưỡng hiệu năng 100 ms và 1000 client | `ARCHITECTURE.md` mục 13 | Chỉ là giả thuyết cho đến khi có kết quả đo, khi đó thêm quyết định mới nếu phải điều chỉnh |

## Gợi ý cách dùng khi viết báo cáo

- Mục "Thiết kế": lấy D-003, D-004, D-005, D-006, D-008.
- Mục "Bảo mật và hạn chế": lấy D-010, D-011, D-009.
- Mục "Kiểm thử": lấy D-005, D-020, D-021.
- Mục "Quy trình và đóng góp": lấy D-022, D-023.
- Mục "Hướng phát triển": các phương án đã loại trong D-011, D-012, D-023.
