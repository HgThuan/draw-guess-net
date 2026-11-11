# Giao thức netsketch v1

> **Nguồn sự thật** cho server (C) và client (C + Python). Mọi thay đổi giao thức phải sửa file này **trước**, rồi mới sửa code (xem `AGENTS.md`).
> Trạng thái: bản nháp v1.0, 11/11/2026.

## 1. Tổng quan

- Truyền tải: **TCP**, server lắng nghe một cổng (mặc định `9000`).
- Giao thức **dạng text**, mỗi thông điệp là **một dòng**, kết thúc bằng `\n`. Dễ debug bằng `nc` và dễ unit test.
- Server dùng `epoll` một luồng. Mọi lệnh của một kết nối được xử lý **theo đúng thứ tự gửi**.
- Có hai loại thông điệp từ server gửi về client:
  - **Phản hồi** (`OK|...` hoặc `ERR|...`): trả lời đúng một lệnh, theo đúng thứ tự lệnh.
  - **Sự kiện** (mọi dòng còn lại): server chủ động đẩy xuống, có thể chen giữa các phản hồi. Client phân biệt bằng từ đầu tiên của dòng.

## 2. Định dạng dòng

```
LENH|tham_so_1|tham_so_2|...\n
```

| Quy tắc | Giá trị |
|---|---|
| Phân cách trường | `\|` |
| Mã hóa | UTF-8 |
| Độ dài tối đa một dòng | **512 byte**, tính cả `\n` |
| Tên lệnh và sự kiện | CHỮ HOA, phân biệt hoa thường |
| Ký tự `\r` ở cuối dòng | Server bỏ qua (để dùng được với `telnet`, `nc`) |
| Dòng rỗng | Bỏ qua, không phản hồi |
| Số nguyên | Chỉ gồm chữ số thập phân, không dấu, không khoảng trắng |

**Dòng dài hơn 512 byte:** server bỏ phần dữ liệu cho đến khi gặp `\n`, trả `ERR|104`, không đóng kết nối.

**Truyền dòng (framing):** TCP là dòng byte, một lần `recv` có thể chứa nửa dòng hoặc nhiều dòng. Server và client phải có bộ đệm, chỉ xử lý khi đã gặp `\n`.

**Trường văn bản tự do** (`GUESS`, `CHAT`): luôn là **trường cuối cùng** của thông điệp. Khi tách dòng, trường này lấy toàn bộ phần còn lại, nên được phép chứa ký tự `|`. Không được chứa ký tự điều khiển (mã < 0x20 hoặc 0x7F).

### Ràng buộc dữ liệu

| Trường | Quy tắc |
|---|---|
| `user` | 3 đến 16 ký tự, chỉ `A-Z a-z 0-9 _`. Phân biệt hoa thường |
| `pass` | 6 đến 32 byte, không chứa `\|`, khoảng trắng hay ký tự điều khiển |
| `room name` | 1 đến 24 ký tự, chỉ `A-Z a-z 0-9`, dấu cách, `_`, `-`. Server cắt khoảng trắng đầu và cuối |
| `text` (chat, đoán) | 1 đến 200 byte sau khi cắt khoảng trắng đầu và cuối |
| `color` | 6 chữ số hex `RRGGBB` |
| Tọa độ | Số nguyên **0 đến 1000** trên cả hai trục (chuẩn hóa), client tự scale theo kích thước canvas |

## 3. Mã lỗi

Phản hồi lỗi: `ERR|<ma>|<mo_ta_ngan>`.

| Mã | Tên | Ý nghĩa |
|---|---|---|
| 100 | BAD_FORMAT | Sai cú pháp, thiếu hoặc thừa tham số, giá trị không phải số |
| 101 | NOT_LOGGED_IN | Chưa đăng nhập |
| 102 | ALREADY_LOGGED_IN | Kết nối này đã đăng nhập |
| 103 | UNKNOWN_COMMAND | Không có lệnh này |
| 104 | LINE_TOO_LONG | Dòng vượt quá 512 byte |
| 110 | USER_EXISTS | Tên đã được đăng ký |
| 111 | BAD_CREDENTIALS | Sai tên hoặc mật khẩu |
| 112 | INVALID_USERNAME | Tên không hợp lệ |
| 113 | INVALID_PASSWORD | Mật khẩu không hợp lệ |
| 114 | ACCOUNT_IN_USE | Tài khoản đang đăng nhập ở kết nối khác |
| 200 | ROOM_NOT_FOUND | Phòng không tồn tại |
| 201 | ALREADY_IN_ROOM | Đang ở trong một phòng |
| 202 | ROOM_FULL | Phòng đã đủ người |
| 203 | ROOM_IN_GAME | Phòng đang chơi, không thể vào |
| 204 | NOT_IN_ROOM | Chưa vào phòng nào |
| 205 | INVALID_ROOM_PARAMS | Tên hoặc thông số phòng ngoài phạm vi cho phép |
| 206 | NOT_HOST | Chỉ chủ phòng mới được dùng lệnh này |
| 207 | NOT_ENOUGH_PLAYERS | Cần ít nhất 2 người để bắt đầu |
| 208 | ROOM_LIMIT | Server đã đạt số phòng tối đa |
| 300 | WRONG_STATE | Lệnh không hợp lệ ở trạng thái ván hiện tại |
| 301 | NOT_DRAWER | Chỉ người vẽ mới được dùng lệnh này |
| 302 | IS_DRAWER | Người vẽ không được dùng lệnh này |
| 303 | ALREADY_GUESSED | Đã đoán đúng trong lượt này |
| 304 | INVALID_WORD_CHOICE | Lựa chọn từ khóa không hợp lệ |
| 305 | INVALID_STROKE | Dữ liệu nét vẽ không hợp lệ |
| 306 | CHAT_BLOCKED | Tin nhắn bị chặn vì chứa đáp án |
| 900 | INTERNAL | Lỗi nội bộ server |
| 901 | SERVER_FULL | Server đã đạt số kết nối tối đa (kèm đóng kết nối) |

### Thứ tự kiểm tra (để kết quả xác định, dễ viết test)

Server kiểm tra lần lượt, gặp lỗi nào thì trả lỗi đó:

1. **Cú pháp:** 104, 103, 100
2. **Trạng thái đăng nhập:** 101, 102
3. **Có trong phòng hay không:** 204, 201
4. **Trạng thái phòng hoặc ván:** 300, 203
5. **Vai trò và quyền:** 206, 301, 302, 303
6. **Giá trị tham số và quy tắc nghiệp vụ:** các mã còn lại

## 4. Giới hạn và thời gian (cấu hình được trong `config.h`)

| Tham số | Mặc định |
|---|---|
| Số kết nối tối đa | 2048 |
| Số phòng tối đa | 512 |
| Số người mỗi phòng | 2 đến 8 |
| Số vòng mỗi ván | 1 đến 10 |
| Thời gian vẽ mỗi lượt | 30 đến 180 giây |
| Thời gian chọn từ | 15 giây |
| Nghỉ giữa hai lượt | 5 giây |
| Gửi `TIME` mỗi | 5 giây |
| Hết hạn phiên khi không có dữ liệu | 120 giây (server gửi `KICK\|IDLE` rồi đóng) |

## 5. Trạng thái

### 5.1 Trạng thái kết nối

```
CONNECTED --LOGIN--> AUTHENTICATED --CREATE_ROOM/JOIN_ROOM--> IN_ROOM
    ^                    |    ^                                  |
    |                    |    +-------------LEAVE_ROOM-----------+
    +-------LOGOUT-------+
```

Khi kết nối bị đóng đột ngột: server xử lý như `LEAVE_ROOM` rồi `LOGOUT` (lý do `DISCONNECT`).

### 5.2 Trạng thái phòng và ván chơi

```
LOBBY --START--> CHOOSING_WORD --chọn từ hoặc hết 15s--> DRAWING --kết thúc lượt--> TURN_END
   ^                   ^                                                               |
   |                   +---------------------còn lượt-----------------------------------+
   +------------------------------------hết ván (GAME_END)-------------------------------+
```

- Một **vòng** là mọi người chơi mỗi người vẽ một lần, theo thứ tự vào phòng. Tổng số lượt = `rounds` x số người.
- Danh sách người vẽ được chốt ở đầu mỗi vòng. Người đã rời phòng thì bị bỏ qua.
- **Lượt kết thúc** khi một trong ba điều kiện xảy ra: hết giờ (`TIMEUP`), tất cả người đoán còn trong phòng đã đoán đúng (`ALL_GUESSED`), hoặc người vẽ rời đi (`DRAWER_LEFT`).
- Sau `GAME_END`, phòng quay về `LOBBY`, điểm được reset.

### 5.3 Ma trận lệnh theo trạng thái

`OK` là hợp lệ. Còn lại là mã lỗi trả về.

| Lệnh | Chưa đăng nhập | Ngoài phòng | Phòng LOBBY | Đang chơi |
|---|---|---|---|---|
| `PING` | OK | OK | OK | OK |
| `REGISTER`, `LOGIN` | OK | 102 | 102 | 102 |
| `LOGOUT` | 101 | OK | OK (rời phòng trước) | OK (rời phòng trước) |
| `LIST_ROOMS` | 101 | OK | OK | OK |
| `CREATE_ROOM`, `JOIN_ROOM` | 101 | OK | 201 | 201 |
| `LEAVE_ROOM` | 101 | 204 | OK | OK |
| `START` | 101 | 204 | OK (chủ phòng) | 300 |
| `SCOREBOARD` | 101 | 204 | OK | OK |
| `CHOOSE_WORD` | 101 | 204 | 300 | OK (người vẽ, giai đoạn CHOOSING_WORD) |
| `STROKE`, `CLEAR` | 101 | 204 | 300 | OK (người vẽ, giai đoạn DRAWING) |
| `GUESS` | 101 | 204 | 300 | OK (người đoán, giai đoạn DRAWING) |
| `CHAT` | 101 | 204 | OK | OK (có điều kiện, xem 7.4) |

## 6. Lệnh Client gửi Server

Mỗi lệnh nhận **đúng một phản hồi**, trừ `STROKE` và `CLEAR` (xem bên dưới).

### 6.1 Tài khoản và phiên

| Lệnh | Cú pháp | Phản hồi thành công | Lỗi có thể gặp |
|---|---|---|---|
| `REGISTER` | `REGISTER\|user\|pass` | `OK` | 100, 102, 110, 112, 113 |
| `LOGIN` | `LOGIN\|user\|pass` | `OK\|session_id` | 100, 102, 111, 114 |
| `LOGOUT` | `LOGOUT` | `OK` | 101 |
| `PING` | `PING` | `PONG` | (không) |

- `session_id`: chuỗi 16 ký tự hex ngẫu nhiên do server sinh, dùng để ghi log và đối chiếu khi debug. Phiên gắn với kết nối TCP, **v1 không hỗ trợ nối lại phiên**.
- Mỗi tài khoản chỉ được đăng nhập ở **một kết nối** tại một thời điểm (lỗi 114).
- Để tránh lộ thông tin, `LOGIN` sai tên hoặc sai mật khẩu đều trả `111`.

### 6.2 Phòng chơi

| Lệnh | Cú pháp | Phản hồi thành công | Lỗi có thể gặp |
|---|---|---|---|
| `CREATE_ROOM` | `CREATE_ROOM\|name\|max_players\|rounds\|draw_time` | `OK\|room_id` | 100, 101, 201, 205, 208 |
| `LIST_ROOMS` | `LIST_ROOMS\|page` | `OK\|total\|page\|r1\|r2\|...` | 100, 101 |
| `JOIN_ROOM` | `JOIN_ROOM\|room_id` | `OK\|room_id` | 100, 101, 201, 200, 202, 203 |
| `LEAVE_ROOM` | `LEAVE_ROOM` | `OK` | 101, 204 |
| `START` | `START` | `OK` | 101, 204, 300, 206, 207 |
| `SCOREBOARD` | `SCOREBOARD` | `OK\|user:score,user:score,...` | 101, 204 |

- `CREATE_ROOM`: người tạo tự động vào phòng và là **chủ phòng**. `max_players` 2 đến 8, `rounds` 1 đến 10, `draw_time` 30 đến 180. Sau `OK` server gửi sự kiện `ROOM_INFO` cho người tạo.
- `LIST_ROOMS`: `page` bắt đầu từ 1, mỗi trang tối đa **8 phòng**. `total` là tổng số phòng. Mỗi phần tử `r` có dạng `id,name,cur,max,state`, với `state` là `LOBBY` hoặc `PLAYING`. Trang không có phòng nào vẫn trả `OK|total|page` (không có phần tử).
- `JOIN_ROOM`: chỉ vào được khi phòng ở trạng thái `LOBBY`. Sau `OK`, người vào nhận `ROOM_INFO`, những người khác nhận `PLAYER_JOINED`.
- `START`: chỉ chủ phòng, phòng ở `LOBBY`, có ít nhất 2 người. Sau `OK`, server bắt đầu phát sự kiện ván chơi.
- `SCOREBOARD`: điểm hiện tại, sắp xếp giảm dần theo điểm, hòa điểm thì theo `user` tăng dần.

### 6.3 Trong ván chơi

| Lệnh | Cú pháp | Phản hồi thành công | Lỗi có thể gặp |
|---|---|---|---|
| `CHOOSE_WORD` | `CHOOSE_WORD\|index` | `OK` | 100, 204, 300, 301, 304 |
| `STROKE` | `STROKE\|color\|width\|points` | **(không có)** | 100, 204, 300, 301, 305 |
| `CLEAR` | `CLEAR` | **(không có)** | 204, 300, 301 |
| `GUESS` | `GUESS\|text` | `OK\|CORRECT\|points` hoặc `OK\|WRONG` | 100, 204, 300, 302, 303 |
| `CHAT` | `CHAT\|text` | `OK` | 100, 204, 302, 306 |

- `CHOOSE_WORD`: `index` là 1, 2 hoặc 3, tương ứng thứ tự trong `WORD_OPTIONS`.
- `STROKE`: một nét vẽ gồm nhiều đoạn nối tiếp (polyline). `width` từ 1 đến 20. `points` là danh sách `x1,y1,x2,y2,...` gồm **2 đến 40 điểm** (4 đến 80 số, số lượng số phải chẵn), mỗi tọa độ từ 0 đến 1000. Tẩy xóa được thể hiện bằng nét màu trắng `FFFFFF`. Để giảm lưu lượng, **thành công thì không có phản hồi**, chỉ trả `ERR` khi lỗi.
- `CLEAR`: xóa toàn bộ canvas, cũng không có phản hồi khi thành công.
- `GUESS`: so sánh đáp án sau khi cắt khoảng trắng đầu và cuối, **không phân biệt hoa thường** (chỉ ASCII). Bộ từ trong `data/words.txt` dùng chữ không dấu. Đoán sai không bị trừ điểm.

## 7. Sự kiện Server gửi Client

### 7.1 Kết nối và phòng

| Sự kiện | Cú pháp | Gửi tới | Khi nào |
|---|---|---|---|
| `HELLO` | `HELLO\|netsketch\|1` | Client vừa kết nối | Ngay khi `accept` |
| `ROOM_INFO` | `ROOM_INFO\|room_id\|name\|host\|max\|rounds\|draw_time\|state\|p1,p2,...` | Người vừa vào phòng | Sau `OK` của `CREATE_ROOM`, `JOIN_ROOM`, và sau `GAME_END` |
| `PLAYER_JOINED` | `PLAYER_JOINED\|user` | Mọi người khác trong phòng | Có người vào |
| `PLAYER_LEFT` | `PLAYER_LEFT\|user\|reason` | Mọi người còn lại | `reason`: `LEAVE`, `DISCONNECT`, `KICK` |
| `HOST_CHANGED` | `HOST_CHANGED\|user` | Cả phòng | Chủ phòng rời, chuyển cho người vào phòng sớm nhất còn lại |
| `KICK` | `KICK\|reason` | Client bị đóng kết nối | `reason`: `IDLE`, `SERVER_FULL`. Sau đó server đóng kết nối |

Phòng không còn ai thì bị xóa.

### 7.2 Diễn biến ván chơi

| Sự kiện | Cú pháp | Gửi tới | Ý nghĩa |
|---|---|---|---|
| `GAME_START` | `GAME_START\|rounds\|p1,p2,...` | Cả phòng | Bắt đầu ván, kèm thứ tự vẽ |
| `TURN_START` | `TURN_START\|round\|drawer\|choose_time` | Cả phòng | Bắt đầu lượt, giai đoạn chọn từ |
| `WORD_OPTIONS` | `WORD_OPTIONS\|w1\|w2\|w3` | **Chỉ người vẽ** | 3 từ khóa khác nhau, chọn ngẫu nhiên từ bộ từ |
| `DRAW_START` | `DRAW_START\|drawer\|draw_time\|hint` | Cả phòng | Bắt đầu giai đoạn vẽ |
| `TIME` | `TIME\|remaining` | Cả phòng | Số giây còn lại, gửi mỗi 5 giây |
| `TURN_END` | `TURN_END\|reason\|word\|scores` | Cả phòng | Kết thúc lượt, công bố từ khóa và điểm |
| `GAME_END` | `GAME_END\|reason\|ranking` | Cả phòng | Kết thúc ván |

- `DRAW_START.hint`: **người vẽ nhận chính từ khóa**, những người khác nhận dạng gạch dưới, mỗi chữ cái một `_`, giữ nguyên khoảng trắng. Ví dụ từ `ice cream` hiện thành `___ _____`.
- Nếu người vẽ không chọn từ trong `choose_time`, server tự chọn ngẫu nhiên một trong ba từ.
- `TURN_END.reason`: `TIMEUP`, `ALL_GUESSED`, `DRAWER_LEFT`.
- `TURN_END.scores`: `user:total:delta` cách nhau bởi dấu phẩy, `total` là tổng điểm sau lượt, `delta` là điểm nhận được trong lượt.
- `GAME_END.reason`: `FINISHED` hoặc `NOT_ENOUGH_PLAYERS` (còn dưới 2 người). `ranking` là `user:score` cách nhau bởi dấu phẩy, sắp xếp giảm dần theo điểm, hòa điểm thì theo `user` tăng dần.
- Sau `TURN_END`, server nghỉ 5 giây rồi gửi `TURN_START` của lượt kế hoặc `GAME_END`.

### 7.3 Vẽ và đoán

| Sự kiện | Cú pháp | Gửi tới | Ý nghĩa |
|---|---|---|---|
| `STROKE` | `STROKE\|color\|width\|points` | Cả phòng **trừ người vẽ** | Chuyển tiếp nét vẽ, giữ nguyên nội dung |
| `CLEAR` | `CLEAR` | Cả phòng trừ người vẽ | Xóa canvas |
| `GUESS_WRONG` | `GUESS_WRONG\|user\|text` | Cả phòng, kể cả người gửi | Có người đoán sai |
| `GUESS_CORRECT` | `GUESS_CORRECT\|user\|points` | Cả phòng, kể cả người đoán | Có người đoán đúng, **không lộ từ khóa** |
| `CHAT` | `CHAT\|user\|text` | Xem 7.4 | Tin nhắn trong phòng |

### 7.4 Chat và chặn lộ đáp án

| Tình huống | Hành vi |
|---|---|
| Ở `LOBBY` hoặc giai đoạn nghỉ (`TURN_END`) | Gửi cho cả phòng |
| Người vẽ muốn chat trong lượt của mình (`CHOOSING_WORD`, `DRAWING`) | Từ chối, `ERR\|302` |
| Người chưa đoán đúng, nội dung chứa từ khóa (không phân biệt hoa thường, kể cả khi từ khóa nằm lẫn trong câu) | Từ chối, `ERR\|306`, không chuyển tiếp |
| Người **đã đoán đúng**, trong giai đoạn `DRAWING` | Chỉ gửi cho người vẽ và những người cũng đã đoán đúng |
| Các trường hợp còn lại | Gửi cho cả phòng, kể cả người gửi |

## 8. Công thức chấm điểm

Toàn bộ dùng **chia nguyên (làm tròn xuống)**.

- Người đoán đúng thứ `k` (k = 1, 2, 3...), còn `remaining` giây trên tổng `draw_time`:

  ```
  points = max(50, 100 + 400 * remaining / draw_time - 25 * (k - 1))
  ```

- Người vẽ, cuối lượt:

  ```
  drawer_points = tổng points của mọi người đoán đúng / (số người trong phòng - 1)
  ```

  Không ai đoán đúng thì người vẽ nhận 0. Số người trong phòng tính tại thời điểm kết thúc lượt.

- Điểm không bao giờ âm. Người đoán sai không bị trừ điểm. Người rời giữa ván **giữ nguyên điểm đã có** cho đến khi rời, và không xuất hiện trong bảng xếp hạng cuối.

| Ví dụ (`draw_time` = 60) | Tính | Kết quả |
|---|---|---|
| Người đoán đầu tiên, còn 45 giây | 100 + 400x45/60 - 0 | **400** |
| Người đoán thứ hai, còn 30 giây | 100 + 400x30/60 - 25 | **275** |
| Người đoán thứ ba, còn 0 giây | max(50, 100 + 0 - 50) | **50** |
| Người vẽ, phòng 3 người, hai người đoán 400 và 275 | (400 + 275) / 2 | **337** |

## 9. Xử lý người chơi thoát giữa chừng

| Tình huống | Hành vi |
|---|---|
| Người đoán rời hoặc mất kết nối | Gửi `PLAYER_LEFT`. Nếu mọi người đoán còn lại đã đoán đúng thì kết thúc lượt (`ALL_GUESSED`) |
| Người vẽ rời hoặc mất kết nối | Kết thúc lượt ngay với `TURN_END\|DRAWER_LEFT`. Người vẽ không nhận điểm. Điểm của những người đã đoán đúng được giữ |
| Chủ phòng rời | Gửi `HOST_CHANGED` cho người vào phòng sớm nhất còn lại |
| Còn dưới 2 người trong lúc chơi | `GAME_END\|NOT_ENOUGH_PLAYERS\|...`, phòng về `LOBBY` |
| Phòng không còn ai | Xóa phòng |

## 10. Ví dụ một phiên chơi

Quy ước: `>` là client gửi, `<` là server gửi. Số điểm cuối là số liệu minh họa.

```
# Kết nối của alice
< HELLO|netsketch|1
> REGISTER|alice|secret1
< OK
> LOGIN|alice|secret1
< OK|9f3a1c2b7d4e5f60
> CREATE_ROOM|Phong Vui|3|2|60
< OK|1
< ROOM_INFO|1|Phong Vui|alice|3|2|60|LOBBY|alice

# bob và carol đã đăng nhập, lần lượt vào phòng 1
> JOIN_ROOM|1                           (bob)
< OK|1
< ROOM_INFO|1|Phong Vui|alice|3|2|60|LOBBY|alice,bob
< PLAYER_JOINED|bob                     (alice nhận)

# carol vào tương tự, alice nhận PLAYER_JOINED|carol

# alice bắt đầu ván
> START
< OK
< GAME_START|2|alice,bob,carol          (cả phòng nhận)
< TURN_START|1|alice|15                 (cả phòng nhận)
< WORD_OPTIONS|cat|house|tree           (chỉ alice nhận)
> CHOOSE_WORD|1
< OK
< DRAW_START|alice|60|cat               (alice thấy từ khóa)
< DRAW_START|alice|60|___               (bob, carol chỉ thấy gạch dưới)

# alice vẽ, bob và carol nhận lại nét vẽ
> STROKE|FF0000|4|100,100,200,150,300,200
< STROKE|FF0000|4|100,100,200,150,300,200

# bob đoán sai, cả phòng nhận
> GUESS|dog
< OK|WRONG
< GUESS_WRONG|bob|dog

# còn 45 giây, carol đoán đúng
> GUESS|cat
< OK|CORRECT|400
< GUESS_CORRECT|carol|400

# còn 30 giây, bob đoán đúng, mọi người đã đoán xong nên lượt kết thúc
> GUESS|cat
< OK|CORRECT|275
< GUESS_CORRECT|bob|275
< TURN_END|ALL_GUESSED|cat|alice:337:337,bob:275:275,carol:400:400

# nghỉ 5 giây rồi sang lượt của bob ... (các lượt còn lại)

< GAME_END|FINISHED|carol:1215,alice:1002,bob:870
< ROOM_INFO|1|Phong Vui|alice|3|2|60|LOBBY|alice,bob,carol
```

Ví dụ lỗi:

```
> LOGIN|alice|saimatkhau
< ERR|111|bad credentials
> GUESS|cat
< ERR|101|not logged in
> CHAT|abc            (khi chưa vào phòng)
< ERR|204|not in room
```

## 11. Mở rộng tùy chọn (chỉ làm nếu còn thời gian, sau khi đóng băng tính năng)

| Sự kiện | Cú pháp | Mô tả |
|---|---|---|
| `HINT` | `HINT\|pattern` | Khi còn 50% thời gian, tiết lộ một chữ cái cho người đoán. Dùng cùng định dạng như `hint` trong `DRAW_START` |

Chỉ thêm khi cập nhật bảng này và nâng số phiên bản trong `HELLO`.

## 12. Gợi ý checklist test từ giao thức

Theo `AGENTS.md`, mỗi lệnh phải có ít nhất **một test đúng và một test sai**:

| Nhóm | Các trường hợp cần phủ |
|---|---|
| Framing | Dòng bị cắt đôi, nhiều dòng trong một lần `recv`, dòng 512 byte, dòng 513 byte, `\r\n`, dòng rỗng |
| Tài khoản | Đăng ký trùng, tên và mật khẩu sai định dạng, đăng nhập sai, đăng nhập hai nơi, đăng nhập lại |
| Phòng | Tạo ngoài phạm vi tham số, phòng đầy, vào phòng đang chơi, rời phòng, chủ phòng rời, phân trang `LIST_ROOMS` |
| Ván chơi | Từ khóa tự chọn khi hết giờ, `STROKE` sai (số lẻ, ngoài 0..1000, quá 40 điểm), người vẽ `GUESS`, đoán lại sau khi đã đúng |
| Chấm điểm | Bốn ví dụ ở mục 8, không ai đoán đúng, `points` chạm sàn 50 |
| Chat | Chứa đáp án, người vẽ chat, người đã đoán đúng chat |
| Thoát giữa chừng | Người vẽ rời, người đoán cuối rời, còn dưới 2 người, hết thời gian chờ 120 giây |

## 13. Lịch sử thay đổi

| Phiên bản | Ngày | Nội dung |
|---|---|---|
| v1.0 | 11/11/2026 | Bản đầu tiên |
