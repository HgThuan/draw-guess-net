"""Tkinter UI for netsketch (authentication, room lobby, and room waiting view).

Runs strictly on a single thread via root.after polling and keeps all room state
in RoomModel updated from server events.
"""
from __future__ import annotations

import argparse
import logging
import time
import tkinter as tk
from tkinter import ttk
from typing import List, Optional, Tuple

from netsketch import (
    ROOMS_PER_PAGE,
    Event,
    NetSketchClient,
    Response,
    RoomModel,
    RoomSummary,
    StrokeBatcher,
    StrokeItem,
    calc_total_pages,
    format_error_vi,
    format_word_hint,
    norm_to_pixel,
    parse_room_list,
    pixel_to_norm,
    scale_stroke_width,
    validate_password,
    validate_room_params,
    validate_username,
)

PALETTE_COLORS: List[Tuple[str, str]] = [
    ("Đen", "000000"),
    ("Đỏ", "E53935"),
    ("Lá", "43A047"),
    ("Dương", "1E88E5"),
    ("Vàng", "FDD835"),
    ("Cam", "FB8C00"),
    ("Tím", "8E24AA"),
    ("Nâu", "6D4C41"),
]
SILENT_ASYNC_CODES = {300, 301, 302, 303, 305}

logger = logging.getLogger("netsketch.ui")


class AuthFrame(ttk.Frame):
    """Connect, Register, and Login screen."""

    def __init__(self, master: tk.Widget, app: "NetSketchApp") -> None:
        super().__init__(master, padding=24)
        self.app = app

        title = ttk.Label(self, text="netsketch — Vẽ và Đoán Từ", font=("sans-serif", 16, "bold"))
        title.grid(row=0, column=0, columnspan=2, pady=(0, 16))

        ttk.Label(self, text="Tài khoản:").grid(row=1, column=0, sticky="e", padx=6, pady=4)
        self.user_var = tk.StringVar()
        self.user_entry = ttk.Entry(self, textvariable=self.user_var, width=24)
        self.user_entry.grid(row=1, column=1, sticky="w", padx=6, pady=4)

        ttk.Label(self, text="Mật khẩu:").grid(row=2, column=0, sticky="e", padx=6, pady=4)
        pass_row = ttk.Frame(self)
        pass_row.grid(row=2, column=1, sticky="w", padx=6, pady=4)
        self.pass_var = tk.StringVar()
        self._show_password = False
        self.pass_entry = ttk.Entry(pass_row, textvariable=self.pass_var, show="*", width=20)
        self.pass_entry.pack(side="left")
        self.toggle_pass_btn = ttk.Button(
            pass_row, text="Hiện", width=5, command=self.toggle_password_visibility
        )
        self.toggle_pass_btn.pack(side="left", padx=(4, 0))

        btn_frame = ttk.Frame(self)
        btn_frame.grid(row=3, column=0, columnspan=2, pady=12)

        self.login_btn = ttk.Button(btn_frame, text="Đăng nhập", command=self.on_login)
        self.login_btn.pack(side="left", padx=6)

        self.register_btn = ttk.Button(btn_frame, text="Đăng ký", command=self.on_register)
        self.register_btn.pack(side="left", padx=6)

        self.status_var = tk.StringVar(value="Nhập thông tin để đăng nhập hoặc đăng ký.")
        self.status_lbl = ttk.Label(self, textvariable=self.status_var, foreground="#333333", wraplength=380)
        self.status_lbl.grid(row=4, column=0, columnspan=2, pady=8)

    def toggle_password_visibility(self) -> None:
        self._show_password = not self._show_password
        self.pass_entry.configure(show="" if self._show_password else "*")
        self.toggle_pass_btn.configure(text="Ẩn" if self._show_password else "Hiện")

    def reset_password_visibility(self) -> None:
        self._show_password = False
        self.pass_entry.configure(show="*")
        self.toggle_pass_btn.configure(text="Hiện")

    def set_busy(self, busy: bool) -> None:
        state = "disabled" if busy else "normal"
        self.login_btn.configure(state=state)
        self.register_btn.configure(state=state)

    def set_status(self, text: str, is_error: bool = False) -> None:
        self.status_var.set(text)
        self.status_lbl.configure(foreground="#b00020" if is_error else "#006400")

    def _check_inputs_and_connect(self) -> Optional[tuple[str, str]]:
        username = self.user_var.get().strip()
        password = self.pass_var.get()

        u_err = validate_username(username)
        if u_err:
            self.set_status(u_err, is_error=True)
            return None

        p_err = validate_password(password)
        if p_err:
            self.set_status(p_err, is_error=True)
            return None

        host = self.app.default_host or "127.0.0.1"
        port = self.app.default_port
        if not self.app.ensure_connected(host, port):
            self.set_status(f"Không thể kết nối tới máy chủ ({host}:{port})", is_error=True)
            return None

        return username, password

    def on_register(self) -> None:
        if self.app.busy:
            return
        creds = self._check_inputs_and_connect()
        if not creds:
            return
        username, password = creds
        self.app.set_busy(True)
        self.set_status("Đang đăng ký tài khoản...", is_error=False)

        def _done(resp: Response) -> None:
            self.app.set_busy(False)
            if resp.ok:
                self.set_status(f"Đăng ký '{username}' thành công! Hãy bấm Đăng nhập.", is_error=False)
            else:
                self.set_status(format_error_vi(resp.code, resp.args[0] if resp.args else ""), is_error=True)

        self.app.client.send_cmd(f"REGISTER|{username}|{password}", _done)

    def on_login(self) -> None:
        if self.app.busy:
            return
        creds = self._check_inputs_and_connect()
        if not creds:
            return
        username, password = creds
        self.app.set_busy(True)
        self.set_status("Đang đăng nhập...", is_error=False)

        def _done(resp: Response) -> None:
            self.app.set_busy(False)
            if resp.ok:
                sid = resp.args[0] if resp.args else ""
                self.pass_var.set("")
                self.reset_password_visibility()
                self.app.on_login_success(username, sid)
            else:
                self.set_status(format_error_vi(resp.code, resp.args[0] if resp.args else ""), is_error=True)

        self.app.client.send_cmd(f"LOGIN|{username}|{password}", _done)


class LobbyFrame(ttk.Frame):
    """Room list with pagination, room creation form, and join/logout actions."""

    def __init__(self, master: tk.Widget, app: "NetSketchApp") -> None:
        super().__init__(master, padding=16)
        self.app = app
        self.current_page = 1
        self.total_pages = 1
        self.total_rooms = 0
        self.rooms: List[RoomSummary] = []

        top = ttk.Frame(self)
        top.pack(fill="x", pady=(0, 8))

        self.user_lbl = ttk.Label(top, text="", font=("sans-serif", 11, "bold"))
        self.user_lbl.pack(side="left")

        self.logout_btn = ttk.Button(top, text="Đăng xuất", command=self.on_logout)
        self.logout_btn.pack(side="right")

        mid = ttk.Frame(self)
        mid.pack(fill="both", expand=True)

        # Left column: Room list
        list_box = ttk.LabelFrame(mid, text="Danh sách phòng", padding=8)
        list_box.pack(side="left", fill="both", expand=True, padx=(0, 8))

        cols = ("id", "name", "players", "state")
        self.tree = ttk.Treeview(list_box, columns=cols, show="headings", height=10, selectmode="browse")
        self.tree.heading("id", text="ID")
        self.tree.heading("name", text="Tên phòng")
        self.tree.heading("players", text="Người chơi")
        self.tree.heading("state", text="Trạng thái")
        self.tree.column("id", width=50, anchor="center")
        self.tree.column("name", width=180, anchor="w")
        self.tree.column("players", width=90, anchor="center")
        self.tree.column("state", width=90, anchor="center")
        self.tree.pack(fill="both", expand=True)

        nav = ttk.Frame(list_box)
        nav.pack(fill="x", pady=(8, 0))

        self.prev_btn = ttk.Button(nav, text="<< Trang trước", command=self.on_prev_page)
        self.prev_btn.pack(side="left")

        self.page_lbl = ttk.Label(nav, text="Trang 1 / 1 (0 phòng)")
        self.page_lbl.pack(side="left", padx=10)

        self.next_btn = ttk.Button(nav, text="Trang sau >>", command=self.on_next_page)
        self.next_btn.pack(side="left")

        self.refresh_btn = ttk.Button(nav, text="Làm mới", command=lambda: self.request_page(self.current_page))
        self.refresh_btn.pack(side="right")

        join_bar = ttk.Frame(list_box)
        join_bar.pack(fill="x", pady=(8, 0))
        ttk.Label(join_bar, text="ID phòng:").pack(side="left")
        self.join_id_var = tk.StringVar()
        self.join_entry = ttk.Entry(join_bar, textvariable=self.join_id_var, width=8)
        self.join_entry.pack(side="left", padx=6)
        self.join_btn = ttk.Button(join_bar, text="Vào phòng đã chọn / theo ID", command=self.on_join_room)
        self.join_btn.pack(side="left", padx=4)

        # Right column: Create room
        create_box = ttk.LabelFrame(mid, text="Tạo phòng mới", padding=12)
        create_box.pack(side="right", fill="y")

        ttk.Label(create_box, text="Tên phòng (1..24):").grid(row=0, column=0, sticky="w", pady=4)
        self.rname_var = tk.StringVar(value="Phong Vui")
        ttk.Entry(create_box, textvariable=self.rname_var, width=20).grid(row=1, column=0, sticky="ew", pady=(0, 8))

        ttk.Label(create_box, text="Số người tối đa (2..8):").grid(row=2, column=0, sticky="w", pady=4)
        self.rmax_var = tk.StringVar(value="4")
        ttk.Spinbox(create_box, from_=2, to=8, textvariable=self.rmax_var, width=8).grid(row=3, column=0, sticky="w", pady=(0, 8))

        ttk.Label(create_box, text="Số vòng (1..10):").grid(row=4, column=0, sticky="w", pady=4)
        self.rrounds_var = tk.StringVar(value="2")
        ttk.Spinbox(create_box, from_=1, to=10, textvariable=self.rrounds_var, width=8).grid(row=5, column=0, sticky="w", pady=(0, 8))

        ttk.Label(create_box, text="Thời gian vẽ (30..180 giây):").grid(row=6, column=0, sticky="w", pady=4)
        self.rtime_var = tk.StringVar(value="60")
        ttk.Spinbox(create_box, from_=30, to=180, increment=10, textvariable=self.rtime_var, width=8).grid(row=7, column=0, sticky="w", pady=(0, 12))

        self.create_btn = tk.Button(
            create_box,
            text="+ Tạo phòng mới",
            command=self.on_create_room,
            bg="#1E88E5",
            fg="#FFFFFF",
            activebackground="#1565C0",
            activeforeground="#FFFFFF",
            disabledforeground="#E0E0E0",
            font=("sans-serif", 10, "bold"),
            pady=6,
            relief="raised",
            cursor="hand2",
        )
        self.create_btn.grid(row=8, column=0, sticky="ew", pady=(4, 0))

        self.status_var = tk.StringVar(value="")
        self.status_lbl = ttk.Label(self, textvariable=self.status_var, foreground="#333333")
        self.status_lbl.pack(fill="x", pady=(8, 0))

    def set_busy(self, busy: bool) -> None:
        state = "disabled" if busy else "normal"
        for btn in (self.logout_btn, self.refresh_btn, self.join_btn, self.create_btn):
            btn.configure(state=state)
        if busy:
            self.prev_btn.configure(state="disabled")
            self.next_btn.configure(state="disabled")
        else:
            self._update_pagination_buttons()

    def set_status(self, text: str, is_error: bool = False) -> None:
        self.status_var.set(text)
        self.status_lbl.configure(foreground="#b00020" if is_error else "#006400")

    def _update_pagination_buttons(self) -> None:
        self.prev_btn.configure(state="normal" if self.current_page > 1 else "disabled")
        self.next_btn.configure(state="normal" if self.current_page < self.total_pages else "disabled")
        self.page_lbl.configure(
            text=f"Trang {self.current_page} / {self.total_pages} ({self.total_rooms} phòng)"
        )

    def request_page(self, page: int) -> None:
        if self.app.busy:
            return
        target = max(1, page)
        self.app.set_busy(True)

        def _done(resp: Response) -> None:
            self.app.set_busy(False)
            if not resp.ok:
                self.set_status(format_error_vi(resp.code, resp.args[0] if resp.args else ""), is_error=True)
                return
            try:
                total, pg, rooms = parse_room_list(resp.args)
            except ValueError:
                self.set_status("Dữ liệu danh sách phòng không hợp lệ", is_error=True)
                return

            self.total_rooms = total
            self.total_pages = calc_total_pages(total, ROOMS_PER_PAGE)
            # If the current page is now empty because rooms were deleted, step back to last valid page.
            if pg > self.total_pages and total > 0:
                self.request_page(self.total_pages)
                return

            self.current_page = pg
            self.rooms = rooms
            for item in self.tree.get_children():
                self.tree.delete(item)
            for r in rooms:
                self.tree.insert(
                    "",
                    "end",
                    iid=str(r.room_id),
                    values=(r.room_id, r.name, f"{r.nplayers}/{r.max_players}", r.state),
                )
            self._update_pagination_buttons()

        self.app.client.send_cmd(f"LIST_ROOMS|{target}", _done)

    def on_prev_page(self) -> None:
        if self.current_page > 1:
            self.request_page(self.current_page - 1)

    def on_next_page(self) -> None:
        if self.current_page < self.total_pages:
            self.request_page(self.current_page + 1)

    def on_create_room(self) -> None:
        if self.app.busy:
            return
        name = self.rname_var.get()
        try:
            max_p = int(self.rmax_var.get().strip())
            rounds = int(self.rrounds_var.get().strip())
            draw_time = int(self.rtime_var.get().strip())
        except ValueError:
            self.set_status("Số người, số vòng và thời gian vẽ phải là số nguyên", is_error=True)
            return

        err = validate_room_params(name, max_p, rounds, draw_time)
        if err:
            self.set_status(err, is_error=True)
            return

        self.app.set_busy(True)
        self.set_status("Đang tạo phòng...", is_error=False)

        def _done(resp: Response) -> None:
            self.app.set_busy(False)
            if resp.ok:
                # Screen transition happens on ROOM_INFO event
                self.set_status("Đã tạo phòng, đang tải thông tin phòng...", is_error=False)
            else:
                self.set_status(format_error_vi(resp.code, resp.args[0] if resp.args else ""), is_error=True)

        self.app.client.send_cmd(f"CREATE_ROOM|{name.strip(' ')}|{max_p}|{rounds}|{draw_time}", _done)

    def on_join_room(self) -> None:
        if self.app.busy:
            return
        sel = self.tree.selection()
        raw_id = self.join_id_var.get().strip() or (sel[0] if sel else "")
        if not raw_id:
            self.set_status("Hãy chọn một phòng trong bảng hoặc nhập ID phòng", is_error=True)
            return
        try:
            room_id = int(raw_id)
            if room_id < 1:
                raise ValueError
        except ValueError:
            self.set_status("ID phòng phải là số nguyên dương", is_error=True)
            return

        self.app.set_busy(True)
        self.set_status(f"Đang vào phòng #{room_id}...", is_error=False)

        def _done(resp: Response) -> None:
            self.app.set_busy(False)
            if resp.ok:
                self.set_status(f"Đã vào phòng #{room_id}, đang đồng bộ...", is_error=False)
            else:
                self.set_status(format_error_vi(resp.code, resp.args[0] if resp.args else ""), is_error=True)
                if resp.code in (200, 202, 203):
                    self.request_page(self.current_page)

        self.app.client.send_cmd(f"JOIN_ROOM|{room_id}", _done)

    def on_logout(self) -> None:
        if self.app.busy:
            return
        self.app.set_busy(True)

        def _done(resp: Response) -> None:
            self.app.set_busy(False)
            if resp.ok:
                self.app.on_logout_success()
            else:
                self.set_status(format_error_vi(resp.code, resp.args[0] if resp.args else ""), is_error=True)

        self.app.client.send_cmd("LOGOUT", _done)


class RoomFrame(ttk.Frame):
    """Room waiting, drawing canvas, word choice, guess/chat, and scoreboard screen."""

    def __init__(self, master: tk.Widget, app: "NetSketchApp") -> None:
        super().__init__(master, padding=10)
        self.app = app
        self.canvas_w = max(240, int(app.canvas_width))
        self.canvas_h = int((self.canvas_w * 3) // 4)
        self._batch_timer_id = None
        self._clock_timer_id = None
        self._rendered_feed_len = 0

        self.batcher = StrokeBatcher(
            send_line_fn=self.app.client.send_no_reply,
            on_local_batch=self._on_local_batch_emitted,
        )

        header = ttk.Frame(self)
        header.pack(fill="x", pady=(0, 6))

        self.title_lbl = ttk.Label(header, text="", font=("sans-serif", 11, "bold"))
        self.title_lbl.pack(side="left")

        self.leave_btn = ttk.Button(header, text="Rời phòng", command=self.on_leave_room)
        self.leave_btn.pack(side="right")

        self.start_btn = ttk.Button(header, text="Bắt đầu (START)", command=self.on_start_game)
        self.start_btn.pack(side="right", padx=4)

        self.score_btn = ttk.Button(header, text="Bảng điểm", command=self.on_request_scoreboard)
        self.score_btn.pack(side="right", padx=4)

        self.clock_lbl = ttk.Label(header, text="", font=("sans-serif", 11, "bold"), foreground="#b00020")
        self.clock_lbl.pack(side="right", padx=8)

        # Banner for word hint / current word
        self.hint_var = tk.StringVar(value="")
        self.hint_lbl = ttk.Label(self, textvariable=self.hint_var, font=("monospace", 12, "bold"))
        self.hint_lbl.pack(fill="x", pady=(0, 4))

        # Word choice bar (only shown to drawer during CHOOSING_WORD)
        self.choose_bar = ttk.Frame(self)
        ttk.Label(self.choose_bar, text="Chọn từ để vẽ:", font=("sans-serif", 10, "bold")).pack(side="left", padx=4)
        self.word_btns: List[ttk.Button] = []
        for idx in (1, 2, 3):
            b = ttk.Button(self.choose_bar, text="", command=lambda i=idx: self.on_choose_word(i))
            b.pack(side="left", padx=4)
            self.word_btns.append(b)

        # Final ranking banner (persists after GAME_END + ROOM_INFO until dismissed or next GAME_START)
        self.ranking_box = ttk.LabelFrame(self, text="Bảng xếp hạng cuối ván", padding=6)
        self.ranking_var = tk.StringVar(value="")
        ttk.Label(self.ranking_box, textvariable=self.ranking_var, justify="left", font=("sans-serif", 10, "bold")).pack(
            side="left", fill="x", expand=True
        )
        ttk.Button(self.ranking_box, text="Đóng", command=self.on_dismiss_ranking).pack(side="right", padx=4)

        body = ttk.Frame(self)
        body.pack(fill="both", expand=True)

        # Left column: Players & live scores
        left_col = ttk.Frame(body)
        left_col.pack(side="left", fill="y", padx=(0, 8))

        players_box = ttk.LabelFrame(left_col, text="Người chơi & Điểm", padding=4)
        players_box.pack(fill="both", expand=True)

        self.players_list = tk.Listbox(players_box, width=24, height=11)
        self.players_list.pack(fill="both", expand=True)

        info_box = ttk.LabelFrame(left_col, text="Thông tin", padding=4)
        info_box.pack(fill="x", pady=(4, 0))
        self.info_var = tk.StringVar(value="")
        ttk.Label(info_box, textvariable=self.info_var, justify="left").pack(anchor="nw")

        # Center/Right column: Canvas + Toolbar + Compact Guess/Chat below canvas
        center_col = ttk.Frame(body)
        center_col.pack(side="left", fill="both", expand=True)

        self.canvas = tk.Canvas(
            center_col,
            width=self.canvas_w,
            height=self.canvas_h,
            bg="#FFFFFF",
            highlightthickness=1,
            highlightbackground="#888888",
            cursor="crosshair",
        )
        self.canvas.pack(anchor="center", pady=(0, 4))
        self.canvas.bind("<ButtonPress-1>", self._on_canvas_press)
        self.canvas.bind("<B1-Motion>", self._on_canvas_motion)
        self.canvas.bind("<ButtonRelease-1>", self._on_canvas_release)
        self.canvas.bind("<Configure>", self._on_canvas_configure)

        toolbar = ttk.Frame(center_col)
        toolbar.pack(fill="x", pady=(0, 4))

        self.tool_buttons: List[tk.Widget] = []
        for label, hex_color in PALETTE_COLORS:
            btn = tk.Button(
                toolbar,
                text=label,
                bg=f"#{hex_color}",
                fg="#FFFFFF" if hex_color in ("000000", "E53935", "1E88E5", "8E24AA", "6D4C41") else "#000000",
                width=4,
                relief="raised",
                command=lambda c=hex_color: self.on_select_color(c),
            )
            btn.pack(side="left", padx=1)
            self.tool_buttons.append(btn)

        self.eraser_btn = ttk.Button(
            toolbar,
            text="Tẩy",
            width=4,
            command=lambda: self.on_select_color("FFFFFF"),
        )
        self.eraser_btn.pack(side="left", padx=2)
        self.tool_buttons.append(self.eraser_btn)

        ttk.Label(toolbar, text="Nét:").pack(side="left", padx=(2, 1))
        self.width_var = tk.StringVar(value="4")
        self.width_spin = ttk.Spinbox(
            toolbar,
            from_=1,
            to=20,
            width=3,
            textvariable=self.width_var,
            command=self.on_change_width,
        )
        self.width_spin.pack(side="left")
        self.tool_buttons.append(self.width_spin)

        self.clear_btn = ttk.Button(toolbar, text="Xóa (CLEAR)", command=self.on_clear_canvas)
        self.clear_btn.pack(side="right", padx=1)
        self.tool_buttons.append(self.clear_btn)

        # Compact Guess & Chat panel placed below canvas so window width fits 3-up on 1920px screens
        chat_box = ttk.LabelFrame(center_col, text="Đoán từ (GUESS) & Trò chuyện (CHAT)", padding=4)
        chat_box.pack(fill="both", expand=True)

        self.feed_text = tk.Text(chat_box, height=6, state="disabled", wrap="word", font=("sans-serif", 9))
        self.feed_text.pack(fill="both", expand=True, pady=(0, 4))
        self.feed_text.tag_configure("system", foreground="#555555")
        self.feed_text.tag_configure("chat", foreground="#0d47a1")
        self.feed_text.tag_configure("solved_chat", foreground="#6a1b9a")
        self.feed_text.tag_configure("guess_wrong", foreground="#333333")
        self.feed_text.tag_configure("guess_correct", foreground="#1b5e20", font=("sans-serif", 9, "bold"))

        entry_row = ttk.Frame(chat_box)
        entry_row.pack(fill="x")

        self.input_var = tk.StringVar()
        self.input_entry = ttk.Entry(entry_row, textvariable=self.input_var)
        self.input_entry.pack(side="left", fill="x", expand=True, padx=(0, 4))
        self.input_entry.bind("<Return>", self.on_enter_pressed)

        self.guess_btn = ttk.Button(entry_row, text="Đoán (GUESS)", command=self.on_send_guess)
        self.guess_btn.pack(side="left", padx=2)

        self.chat_btn = ttk.Button(entry_row, text="Chat (CHAT)", command=self.on_send_chat)
        self.chat_btn.pack(side="left", padx=2)

        ttk.Label(
            chat_box,
            text="Lưu ý: Khi đang đoán, nhấn Enter sẽ tính là một lần Đoán từ (GUESS); còn lại sẽ gửi Chat.",
            foreground="#666666",
            font=("sans-serif", 8),
        ).pack(anchor="w", pady=(2, 0))

        self.status_var = tk.StringVar(value="")
        self.status_lbl = ttk.Label(self, textvariable=self.status_var, foreground="#333333")
        self.status_lbl.pack(fill="x", pady=(4, 0))

    def _now_ms(self) -> float:
        return time.monotonic() * 1000.0

    def _can_draw_now(self) -> bool:
        m = self.app.room_model
        return (
            m.in_room
            and m.state == "DRAWING"
            and m.drawer == self.app.username
            and m.current_remaining_s() > 0
        )

    def _can_guess_now(self) -> bool:
        m = self.app.room_model
        return (
            not self.app.busy
            and m.in_room
            and m.state == "DRAWING"
            and m.drawer != self.app.username
            and self.app.username not in m.guessed_users
            and m.current_remaining_s() > 0
        )

    def _can_chat_now(self) -> bool:
        m = self.app.room_model
        if self.app.busy or not m.in_room:
            return False
        if m.state in ("CHOOSING_WORD", "DRAWING") and m.drawer == self.app.username:
            return False
        return True

    def _active_viewport(self) -> Tuple[int, int, int, int]:
        """Return (offset_x, offset_y, vp_w, vp_h) preserving a fixed 4:3 aspect ratio."""
        w = max(1, self.canvas.winfo_width())
        h = max(1, self.canvas.winfo_height())
        if w <= 1 or h <= 1:
            w, h = self.canvas_w, self.canvas_h
        if w * 3 <= h * 4:
            vp_w = w
            vp_h = max(1, (w * 3) // 4)
        else:
            vp_h = h
            vp_w = max(1, (h * 4) // 3)
        ox = (w - vp_w) // 2
        oy = (h - vp_h) // 2
        return ox, oy, vp_w, vp_h

    def _draw_stroke_item_on_canvas(self, item: StrokeItem) -> None:
        if len(item.points) < 2:
            return
        ox, oy, vp_w, vp_h = self._active_viewport()
        px_w = scale_stroke_width(item.width, vp_w)
        hex_col = f"#{item.color}"

        px_pts = [
            (ox + norm_to_pixel(nx, ny, vp_w, vp_h)[0], oy + norm_to_pixel(nx, ny, vp_w, vp_h)[1])
            for nx, ny in item.points
        ]
        # Single-point dot (or zero-length segment): use create_oval for cross-platform visibility.
        if all(p == px_pts[0] for p in px_pts):
            cx, cy = px_pts[0]
            r = max(1.0, px_w / 2.0)
            self.canvas.create_oval(cx - r, cy - r, cx + r, cy + r, fill=hex_col, outline=hex_col)
            return

        flat: List[int] = []
        for x, y in px_pts:
            flat.extend((x, y))
        self.canvas.create_line(
            *flat,
            fill=hex_col,
            width=px_w,
            capstyle=tk.ROUND,
            joinstyle=tk.ROUND,
        )

    def redraw_all_strokes(self) -> None:
        self.canvas.delete("all")
        for stroke in self.app.room_model.strokes:
            self._draw_stroke_item_on_canvas(stroke)

    def _on_local_batch_emitted(self, item: StrokeItem) -> None:
        self.app.room_model.strokes.append(item)

    def _schedule_batch_tick(self) -> None:
        if self._batch_timer_id is not None:
            return

        def _on_tick() -> None:
            self._batch_timer_id = None
            if not self._can_draw_now():
                self.batcher.cancel()
                return
            self.batcher.tick(self._now_ms())
            if self.batcher.dragging:
                self._schedule_batch_tick()

        self._batch_timer_id = self.app.root.after(30, _on_tick)

    def cancel_active_stroke(self) -> None:
        if self._batch_timer_id is not None:
            try:
                self.app.root.after_cancel(self._batch_timer_id)
            except Exception:
                pass
            self._batch_timer_id = None
        if self._clock_timer_id is not None:
            try:
                self.app.root.after_cancel(self._clock_timer_id)
            except Exception:
                pass
            self._clock_timer_id = None
        self.batcher.cancel()

    def _schedule_clock_tick(self) -> None:
        if self._clock_timer_id is not None:
            return

        def _tick_clock() -> None:
            self._clock_timer_id = None
            m = self.app.room_model
            if not m.in_room:
                return
            rem = m.current_remaining_s()
            if m.state in ("CHOOSING_WORD", "DRAWING"):
                self.clock_lbl.configure(text=f"Thời gian: {rem}s")
                if rem == 0 and self.batcher.dragging:
                    self.batcher.cancel()
                self._update_drawing_tools()
                self._update_guess_chat_buttons()
                self._clock_timer_id = self.app.root.after(200, _tick_clock)
            else:
                self.clock_lbl.configure(text="")

        self._clock_timer_id = self.app.root.after(200, _tick_clock)

    def _event_to_norm(self, event) -> Tuple[int, int]:
        ox, oy, vp_w, vp_h = self._active_viewport()
        return pixel_to_norm(event.x - ox, event.y - oy, vp_w, vp_h)

    def _sync_width_from_spin(self) -> int:
        try:
            w = int(self.width_var.get().strip())
        except ValueError:
            w = 4
        return max(1, min(20, w))

    def _on_canvas_press(self, event) -> None:
        if not self._can_draw_now():
            return
        nx, ny = self._event_to_norm(event)
        self.batcher.start_stroke(
            nx,
            ny,
            color=self.batcher.color,
            width=self._sync_width_from_spin(),
            now_ms=self._now_ms(),
        )
        self._schedule_batch_tick()

    def _on_canvas_motion(self, event) -> None:
        if not self._can_draw_now() or not self.batcher.dragging:
            return
        nx, ny = self._event_to_norm(event)
        seg = self.batcher.add_point(nx, ny, now_ms=self._now_ms())
        if seg is not None:
            p0, p1 = seg
            self._draw_stroke_item_on_canvas(
                StrokeItem(color=self.batcher.color, width=self.batcher.width, points=[p0, p1])
            )

    def _on_canvas_release(self, event) -> None:
        if not self.batcher.dragging:
            return
        if self._can_draw_now():
            nx, ny = self._event_to_norm(event)
            seg = self.batcher.add_point(nx, ny, now_ms=self._now_ms())
            if seg is not None:
                p0, p1 = seg
                self._draw_stroke_item_on_canvas(
                    StrokeItem(color=self.batcher.color, width=self.batcher.width, points=[p0, p1])
                )
            dot = self.batcher.end_stroke(now_ms=self._now_ms())
            if dot is not None:
                self._draw_stroke_item_on_canvas(
                    StrokeItem(color=self.batcher.color, width=self.batcher.width, points=[dot, dot])
                )
        else:
            self.batcher.cancel()

    def _on_canvas_configure(self, _event) -> None:
        if self.app.room_model.in_room:
            self.redraw_all_strokes()

    def on_select_color(self, hex_color: str) -> None:
        # Flush any in-progress points before changing color (do not cancel!)
        self.batcher.change_tool(color=hex_color, now_ms=self._now_ms())

    def on_change_width(self) -> None:
        self.batcher.change_tool(width=self._sync_width_from_spin(), now_ms=self._now_ms())

    def on_clear_canvas(self) -> None:
        if not self._can_draw_now():
            return
        self.cancel_active_stroke()
        self.app.room_model.strokes.clear()
        self.canvas.delete("all")
        self.app.client.send_no_reply("CLEAR")

    def set_busy(self, busy: bool) -> None:
        if busy:
            self.leave_btn.configure(state="disabled")
            self.start_btn.configure(state="disabled")
            self.score_btn.configure(state="disabled")
        else:
            self.leave_btn.configure(state="normal")
            self.score_btn.configure(state="normal")
            self._update_start_btn()
        self._update_drawing_tools()
        self._update_guess_chat_buttons()

    def set_status(self, text: str, is_error: bool = False) -> None:
        self.status_var.set(text)
        self.status_lbl.configure(foreground="#b00020" if is_error else "#006400")

    def _update_start_btn(self) -> None:
        m = self.app.room_model
        can_start = (
            not self.app.busy
            and m.in_room
            and m.state == "LOBBY"
            and m.host == self.app.username
            and len(m.players) >= 2
        )
        self.start_btn.configure(state="normal" if can_start else "disabled")

    def _update_drawing_tools(self) -> None:
        state = "normal" if self._can_draw_now() else "disabled"
        for w in self.tool_buttons:
            try:
                w.configure(state=state)
            except Exception:
                pass

    def _update_guess_chat_buttons(self) -> None:
        self.guess_btn.configure(state="normal" if self._can_guess_now() else "disabled")
        self.chat_btn.configure(state="normal" if self._can_chat_now() else "disabled")

    def _sync_feed_widget(self) -> None:
        m = self.app.room_model
        if len(m.feed) < self._rendered_feed_len:
            self.feed_text.configure(state="normal")
            self.feed_text.delete("1.0", tk.END)
            self.feed_text.configure(state="disabled")
            self._rendered_feed_len = 0

        if len(m.feed) == self._rendered_feed_len:
            return

        self.feed_text.configure(state="normal")
        for item in m.feed[self._rendered_feed_len :]:
            self.feed_text.insert(tk.END, item.text + "\n", item.kind)
        self.feed_text.see(tk.END)
        self.feed_text.configure(state="disabled")
        self._rendered_feed_len = len(m.feed)

    def render_from_model(self) -> None:
        m = self.app.room_model
        if not m.in_room:
            return
        if not self._can_draw_now():
            self.batcher.cancel()

        round_str = f"  |  Vòng: {m.round_no}/{m.rounds}" if m.state != "LOBBY" and m.round_no > 0 else ""
        self.title_lbl.configure(
            text=f"Phòng #{m.room_id}: {m.name}{round_str}  |  {m.state}"
        )

        # Countdown clock
        rem = m.current_remaining_s()
        if m.state in ("CHOOSING_WORD", "DRAWING"):
            self.clock_lbl.configure(text=f"Thời gian: {rem}s")
            self._schedule_clock_tick()
        else:
            self.clock_lbl.configure(text="")

        # Word / Hint banner
        is_drawer = (m.drawer == self.app.username)
        if m.state == "DRAWING" and m.word_hint:
            formatted = format_word_hint(m.word_hint, is_drawer=is_drawer)
            prefix = "Từ cần vẽ" if is_drawer else "Gợi ý từ khóa"
            self.hint_var.set(f"{prefix}:  {formatted}")
        elif m.state == "CHOOSING_WORD":
            self.hint_var.set(f"Người vẽ '{m.drawer}' đang chọn từ khóa...")
        elif m.state == "TURN_END" and m.last_turn_word:
            self.hint_var.set(f"Đáp án lượt vừa rồi:  {m.last_turn_word}")
        else:
            self.hint_var.set("")

        # Word choice bar for drawer
        if m.state == "CHOOSING_WORD" and is_drawer and len(m.word_options) >= 3:
            for i in range(3):
                self.word_btns[i].configure(text=f"[{i + 1}] {m.word_options[i]}", state="normal")
            self.choose_bar.pack(fill="x", pady=(0, 4), before=self.canvas.master.master)
        else:
            self.choose_bar.pack_forget()

        # Final ranking banner (preserved after GAME_END + ROOM_INFO until dismissed or next GAME_START)
        if m.final_ranking:
            title_reason = (
                "Ván kết thúc sớm vì không đủ người chơi"
                if m.game_end_reason == "NOT_ENOUGH_PLAYERS"
                else "Bảng xếp hạng cuối ván (Hoàn tất)"
            )
            self.ranking_box.configure(text=title_reason)
            lines = [f"Hạng {idx + 1}: {u} — {sc} điểm" for idx, (u, sc) in enumerate(m.final_ranking)]
            self.ranking_var.set("   |   ".join(lines))
            self.ranking_box.pack(fill="x", pady=(0, 4), before=self.canvas.master.master)
        else:
            self.ranking_box.pack_forget()

        # Players list with live scores and safe ASCII/Vietnamese tags
        self.players_list.delete(0, tk.END)
        for p in m.players:
            sc = m.scores.get(p, 0)
            delta = m.turn_deltas.get(p, 0)
            delta_str = f" (+{delta})" if delta > 0 else ""
            tags = []
            if p == m.host:
                tags.append("Chủ")
            if m.drawer and p == m.drawer:
                tags.append("Vẽ")
            if p in m.guessed_users:
                tags.append("Đã đoán")
            suffix = f" [{', '.join(tags)}]" if tags else ""
            self.players_list.insert(tk.END, f"{p}: {sc}{delta_str}{suffix}")

        details = [
            f"Chủ phòng: {m.host}",
            f"Người chơi: {len(m.players)}/{m.max_players}",
            f"Số vòng: {m.rounds} | Vẽ: {m.draw_time}s",
        ]
        self.info_var.set("\n".join(details))

        self._update_start_btn()
        self._update_drawing_tools()
        self._update_guess_chat_buttons()
        self._sync_feed_widget()
        self.redraw_all_strokes()

    def on_choose_word(self, index_1based: int) -> None:
        if self.app.busy:
            return
        for b in self.word_btns:
            b.configure(state="disabled")
        self.app.set_busy(True)

        def _done(resp: Response) -> None:
            self.app.set_busy(False)
            if not resp.ok and resp.code not in SILENT_ASYNC_CODES:
                self.set_status(format_error_vi(resp.code, resp.args[0] if resp.args else ""), is_error=True)

        self.app.client.send_cmd(f"CHOOSE_WORD|{index_1based}", _done)

    def on_dismiss_ranking(self) -> None:
        self.app.room_model.dismiss_final_ranking()
        self.ranking_box.pack_forget()

    def on_request_scoreboard(self) -> None:
        if self.app.busy or not self.app.room_model.in_room:
            return
        self.app.set_busy(True)

        def _done(resp: Response) -> None:
            self.app.set_busy(False)
            if resp.ok:
                self.app.room_model.apply_scoreboard(resp.args)
                self.render_from_model()
                self.set_status("Đã cập nhật bảng điểm từ máy chủ.", is_error=False)
            else:
                self.set_status(format_error_vi(resp.code, resp.args[0] if resp.args else ""), is_error=True)

        self.app.client.send_cmd("SCOREBOARD", _done)

    def on_enter_pressed(self, _event) -> None:
        if self._can_guess_now():
            self.on_send_guess()
        elif self._can_chat_now():
            self.on_send_chat()

    def on_send_guess(self) -> None:
        if not self._can_guess_now():
            return
        text = self.input_var.get().strip(" ")
        if not text:
            return
        if len(text.encode("utf-8", errors="replace")) > 200:
            self.set_status("Nội dung đoán không được vượt quá 200 byte", is_error=True)
            return

        self.app.set_busy(True)

        def _done(resp: Response) -> None:
            self.app.set_busy(False)
            if resp.ok:
                # Model & feed are updated strictly from GUESS_CORRECT / GUESS_WRONG events
                self.input_var.set("")
                self.set_status("", is_error=False)
            elif resp.code not in SILENT_ASYNC_CODES:
                self.set_status(format_error_vi(resp.code, resp.args[0] if resp.args else ""), is_error=True)

        self.app.client.send_cmd(f"GUESS|{text}", _done)

    def on_send_chat(self) -> None:
        if not self._can_chat_now():
            return
        text = self.input_var.get().strip(" ")
        if not text:
            return
        if len(text.encode("utf-8", errors="replace")) > 200:
            self.set_status("Tin nhắn không được vượt quá 200 byte", is_error=True)
            return

        self.app.set_busy(True)

        def _done(resp: Response) -> None:
            self.app.set_busy(False)
            if resp.ok:
                # Feed is updated strictly from CHAT event broadcast by server
                self.input_var.set("")
                self.set_status("", is_error=False)
            elif resp.code == 306 or resp.code not in SILENT_ASYNC_CODES:
                self.set_status(format_error_vi(resp.code, resp.args[0] if resp.args else ""), is_error=True)

        self.app.client.send_cmd(f"CHAT|{text}", _done)

    def on_start_game(self) -> None:
        if self.app.busy:
            return
        self.app.set_busy(True)

        def _done(resp: Response) -> None:
            self.app.set_busy(False)
            if resp.ok:
                self.set_status("Ván chơi đã bắt đầu!", is_error=False)
            else:
                self.set_status(format_error_vi(resp.code, resp.args[0] if resp.args else ""), is_error=True)

        self.app.client.send_cmd("START", _done)

    def on_leave_room(self) -> None:
        if self.app.busy:
            return
        self.cancel_active_stroke()
        self.app.set_busy(True)

        def _done(resp: Response) -> None:
            self.app.set_busy(False)
            if resp.ok:
                self.app.room_model.reset()
                self.app.show_lobby()
            else:
                self.set_status(format_error_vi(resp.code, resp.args[0] if resp.args else ""), is_error=True)

        self.app.client.send_cmd("LEAVE_ROOM", _done)


class NetSketchApp:
    """Top-level controller coordinating Tkinter frames, RoomModel, and NetSketchClient."""

    def __init__(
        self,
        root: tk.Tk,
        host: str = "127.0.0.1",
        port: int = 9000,
        canvas_width: int = 480,
    ) -> None:
        self.root = root
        self.default_host = host
        self.default_port = port
        self.canvas_width = canvas_width
        self.username = ""
        self.session_id = ""
        self.busy = False

        self.room_model = RoomModel()
        self.client = NetSketchClient(
            on_event=self.on_server_event,
            on_async_error=self.on_async_error,
            on_disconnect=self.on_disconnect,
        )

        self.root.title("netsketch")
        win_w = max(620, self.canvas_width + 240)
        win_h = max(460, int((self.canvas_width * 3) // 4) + 140)
        self.root.geometry(f"{win_w}x{win_h}")
        self.root.protocol("WM_DELETE_WINDOW", self.on_window_close)

        self.container = ttk.Frame(self.root)
        self.container.pack(fill="both", expand=True)

        self.auth_frame = AuthFrame(self.container, self)
        self.lobby_frame = LobbyFrame(self.container, self)
        self.room_frame = RoomFrame(self.container, self)
        self.active_frame: ttk.Frame = self.auth_frame
        self.auth_frame.pack(fill="both", expand=True)

    def _switch_frame(self, target: ttk.Frame) -> None:
        if self.active_frame is not target:
            self.active_frame.pack_forget()
            self.active_frame = target
            self.active_frame.pack(fill="both", expand=True)

    def set_busy(self, busy: bool) -> None:
        self.busy = busy
        self.auth_frame.set_busy(busy)
        self.lobby_frame.set_busy(busy)
        self.room_frame.set_busy(busy)

    def ensure_connected(self, host: str, port: int) -> bool:
        if self.client.connected:
            return True
        if not self.client.connect(host, port):
            return False
        self.client.start_polling(self.root, interval_ms=20)
        return True

    def on_login_success(self, username: str, session_id: str) -> None:
        self.username = username
        self.session_id = session_id
        self.root.title(f"netsketch — {username}")
        self.lobby_frame.user_lbl.configure(text=f"Xin chào, {username} (sid: {session_id[:8]})")
        self.show_lobby()

    def on_logout_success(self) -> None:
        self.username = ""
        self.session_id = ""
        self.room_model.reset()
        self.root.title("netsketch")
        self.auth_frame.set_status("Đã đăng xuất.", is_error=False)
        self._switch_frame(self.auth_frame)

    def show_lobby(self) -> None:
        self._switch_frame(self.lobby_frame)
        self.lobby_frame.request_page(1)

    def on_server_event(self, ev: Event) -> None:
        # RoomModel is always updated first; ROOM_INFO drives transitioning to RoomFrame.
        changed = self.room_model.apply_event(ev)
        if ev.name == "ROOM_INFO" and self.room_model.in_room:
            self._switch_frame(self.room_frame)
            self.room_frame.render_from_model()
            return

        if ev.name == "STROKE" and self.active_frame is self.room_frame and len( self.room_model.strokes ) > 0:
            self.room_frame._draw_stroke_item_on_canvas(self.room_model.strokes[-1])
            return

        if ev.name == "CLEAR" and self.active_frame is self.room_frame:
            self.room_frame.canvas.delete("all")
            return

        if changed and self.active_frame is self.room_frame:
            self.room_frame.render_from_model()
            return

        # Events for gameplay phases not yet expanded in the waiting view are logged safely.
        logger.debug("Unhandled or passive event in current view: %s", ev.name)

    def on_async_error(self, resp: Response) -> None:
        # Late STROKE/CLEAR/GUESS/CHOOSE_WORD errors due to network race are logged silently.
        if resp.code in SILENT_ASYNC_CODES:
            logger.debug("Ignored late/race gameplay error %d: %s", resp.code, resp.args)
            return
        msg = format_error_vi(resp.code, resp.args[0] if resp.args else "")
        if self.active_frame is self.room_frame:
            self.room_frame.set_status(msg, is_error=True)
        elif self.active_frame is self.lobby_frame:
            self.lobby_frame.set_status(msg, is_error=True)
        else:
            self.auth_frame.set_status(msg, is_error=True)

    def on_disconnect(self, reason: str) -> None:
        self.room_frame.cancel_active_stroke()
        self.set_busy(False)
        self.username = ""
        self.session_id = ""
        self.room_model.reset()
        self._switch_frame(self.auth_frame)
        self.auth_frame.set_status(f"Đã ngắt kết nối khỏi máy chủ ({reason}).", is_error=True)

    def on_window_close(self) -> None:
        self.room_frame.cancel_active_stroke()
        self.client.close(notify=False)
        self.root.destroy()


def main() -> None:
    parser = argparse.ArgumentParser(description="netsketch Tkinter client")
    parser.add_argument("--host", default="127.0.0.1", help="Server host")
    parser.add_argument("--port", type=int, default=9000, help="Server port")
    parser.add_argument("--canvas-w", type=int, default=400, help="Canvas width in pixels (4:3 aspect, 400 fits 3 windows on 1920px)")
    args = parser.parse_args()

    root = tk.Tk()
    NetSketchApp(root, host=args.host, port=args.port, canvas_width=args.canvas_w)
    root.mainloop()


if __name__ == "__main__":
    main()
