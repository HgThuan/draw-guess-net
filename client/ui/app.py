"""Tkinter UI for netsketch (authentication, room lobby, and room waiting view).

Runs strictly on a single thread via root.after polling and keeps all room state
in RoomModel updated from server events.
"""
from __future__ import annotations

import argparse
import logging
import tkinter as tk
from tkinter import ttk
from typing import List, Optional

from netsketch import (
    ROOMS_PER_PAGE,
    Event,
    NetSketchClient,
    Response,
    RoomModel,
    RoomSummary,
    calc_total_pages,
    format_error_vi,
    parse_room_list,
    validate_password,
    validate_room_params,
    validate_username,
)

logger = logging.getLogger("netsketch.ui")


class AuthFrame(ttk.Frame):
    """Connect, Register, and Login screen."""

    def __init__(self, master: tk.Widget, app: "NetSketchApp") -> None:
        super().__init__(master, padding=24)
        self.app = app

        title = ttk.Label(self, text="netsketch — Vẽ và Đoán Từ", font=(" sans-serif", 16, "bold"))
        title.grid(row=0, column=0, columnspan=2, pady=(0, 16))

        ttk.Label(self, text="Máy chủ (Host):").grid(row=1, column=0, sticky="e", padx=6, pady=4)
        self.host_var = tk.StringVar(value=app.default_host)
        self.host_entry = ttk.Entry(self, textvariable=self.host_var, width=24)
        self.host_entry.grid(row=1, column=1, sticky="w", padx=6, pady=4)

        ttk.Label(self, text="Cổng (Port):").grid(row=2, column=0, sticky="e", padx=6, pady=4)
        self.port_var = tk.StringVar(value=str(app.default_port))
        self.port_entry = ttk.Entry(self, textvariable=self.port_var, width=10)
        self.port_entry.grid(row=2, column=1, sticky="w", padx=6, pady=4)

        ttk.Label(self, text="Tài khoản:").grid(row=3, column=0, sticky="e", padx=6, pady=4)
        self.user_var = tk.StringVar()
        self.user_entry = ttk.Entry(self, textvariable=self.user_var, width=24)
        self.user_entry.grid(row=3, column=1, sticky="w", padx=6, pady=4)

        ttk.Label(self, text="Mật khẩu:").grid(row=4, column=0, sticky="e", padx=6, pady=4)
        self.pass_var = tk.StringVar()
        self.pass_entry = ttk.Entry(self, textvariable=self.pass_var, show="*", width=24)
        self.pass_entry.grid(row=4, column=1, sticky="w", padx=6, pady=4)

        btn_frame = ttk.Frame(self)
        btn_frame.grid(row=5, column=0, columnspan=2, pady=12)

        self.login_btn = ttk.Button(btn_frame, text="Đăng nhập", command=self.on_login)
        self.login_btn.pack(side="left", padx=6)

        self.register_btn = ttk.Button(btn_frame, text="Đăng ký", command=self.on_register)
        self.register_btn.pack(side="left", padx=6)

        self.status_var = tk.StringVar(value="Nhập thông tin để đăng nhập hoặc đăng ký.")
        self.status_lbl = ttk.Label(self, textvariable=self.status_var, foreground="#333333", wraplength=380)
        self.status_lbl.grid(row=6, column=0, columnspan=2, pady=8)

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

        try:
            port = int(self.port_var.get().strip())
        except ValueError:
            self.set_status("Cổng máy chủ phải là số nguyên (1..65535)", is_error=True)
            return None

        host = self.host_var.get().strip() or "127.0.0.1"
        if not self.app.ensure_connected(host, port):
            self.set_status(f"Không thể kết nối tới {host}:{port}", is_error=True)
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

        self.create_btn = ttk.Button(create_box, text="Tạo phòng", command=self.on_create_room)
        self.create_btn.grid(row=8, column=0, sticky="ew")

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
        self.page_lbl.set if hasattr(self.page_lbl, "set") else None
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
    """Room waiting & status screen driven strictly by RoomModel."""

    def __init__(self, master: tk.Widget, app: "NetSketchApp") -> None:
        super().__init__(master, padding=16)
        self.app = app

        header = ttk.Frame(self)
        header.pack(fill="x", pady=(0, 12))

        self.title_lbl = ttk.Label(header, text="", font=("sans-serif", 13, "bold"))
        self.title_lbl.pack(side="left")

        self.leave_btn = ttk.Button(header, text="Rời phòng", command=self.on_leave_room)
        self.leave_btn.pack(side="right")

        self.start_btn = ttk.Button(header, text="Bắt đầu ván (START)", command=self.on_start_game)
        self.start_btn.pack(side="right", padx=8)

        body = ttk.Frame(self)
        body.pack(fill="both", expand=True)

        players_box = ttk.LabelFrame(body, text="Người chơi trong phòng", padding=8)
        players_box.pack(side="left", fill="y", padx=(0, 8))

        self.players_list = tk.Listbox(players_box, width=28, height=12)
        self.players_list.pack(fill="both", expand=True)

        info_box = ttk.LabelFrame(body, text="Trạng thái phòng & ván chơi", padding=12)
        info_box.pack(side="left", fill="both", expand=True)

        self.info_var = tk.StringVar(value="")
        ttk.Label(info_box, textvariable=self.info_var, justify="left").pack(anchor="nw")

        self.status_var = tk.StringVar(value="")
        self.status_lbl = ttk.Label(self, textvariable=self.status_var, foreground="#333333")
        self.status_lbl.pack(fill="x", pady=(8, 0))

    def set_busy(self, busy: bool) -> None:
        if busy:
            self.leave_btn.configure(state="disabled")
            self.start_btn.configure(state="disabled")
        else:
            self.leave_btn.configure(state="normal")
            self._update_start_btn()

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

    def render_from_model(self) -> None:
        m = self.app.room_model
        if not m.in_room:
            return
        self.title_lbl.configure(
            text=f"Phòng #{m.room_id}: {m.name}  |  Trạng thái: {m.state}"
        )
        self.players_list.delete(0, tk.END)
        for p in m.players:
            tags = []
            if p == m.host:
                tags.append("Chủ phòng")
            if m.drawer and p == m.drawer:
                tags.append("Đang vẽ")
            suffix = f" ({', '.join(tags)})" if tags else ""
            self.players_list.insert(tk.END, f"{p}{suffix}")

        details = [
            f"Chủ phòng: {m.host}",
            f"Số người: {len(m.players)} / {m.max_players}",
            f"Số vòng: {m.rounds}  |  Thời gian vẽ: {m.draw_time} giây",
        ]
        if m.state != "LOBBY":
            details.append(f"Vòng hiện tại: {m.round_no}  |  Người vẽ: {m.drawer or '-'}")
            if m.word_hint:
                details.append(f"Từ / Gợi ý: {m.word_hint}  |  Còn lại: {m.remaining_s}s")
        self.info_var.set("\n".join(details))
        self._update_start_btn()

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

    def __init__(self, root: tk.Tk, host: str = "127.0.0.1", port: int = 9000) -> None:
        self.root = root
        self.default_host = host
        self.default_port = port
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
        self.root.geometry("760x480")
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

        if changed and self.active_frame is self.room_frame:
            self.room_frame.render_from_model()
            return

        # Events for gameplay phases not yet expanded in the waiting view are logged safely.
        logger.debug("Unhandled or passive event in current view: %s", ev.name)

    def on_async_error(self, resp: Response) -> None:
        msg = format_error_vi(resp.code, resp.args[0] if resp.args else "")
        if self.active_frame is self.room_frame:
            self.room_frame.set_status(msg, is_error=True)
        elif self.active_frame is self.lobby_frame:
            self.lobby_frame.set_status(msg, is_error=True)
        else:
            self.auth_frame.set_status(msg, is_error=True)

    def on_disconnect(self, reason: str) -> None:
        self.set_busy(False)
        self.username = ""
        self.session_id = ""
        self.room_model.reset()
        self._switch_frame(self.auth_frame)
        self.auth_frame.set_status(f"Đã ngắt kết nối khỏi máy chủ ({reason}).", is_error=True)

    def on_window_close(self) -> None:
        self.client.close(notify=False)
        self.root.destroy()


def main() -> None:
    parser = argparse.ArgumentParser(description="netsketch Tkinter client")
    parser.add_argument("--host", default="127.0.0.1", help="Server host")
    parser.add_argument("--port", type=int, default=9000, help="Server port")
    args = parser.parse_args()

    root = tk.Tk()
    NetSketchApp(root, host=args.host, port=args.port)
    root.mainloop()


if __name__ == "__main__":
    main()
