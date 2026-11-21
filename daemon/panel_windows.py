"""Windows control panel for Clawdmeter."""
import json
from queue import Empty, Queue
from threading import Thread
import tkinter as tk
import customtkinter as ctk

from wifi_sync_windows import CONFIG, WifiSync

NOTE_FILE = CONFIG.with_name("note.txt")
PAGES = ("Claude", "Codex", "Заметки")
LEVELS = (64, 128, 200, 255)


def main():
    ctk.set_appearance_mode("dark")
    window = ctk.CTk(fg_color="#101114")
    window.title("Пульт Clawdmeter")
    window.geometry("650x675")
    window.minsize(610, 570)
    surface, raised = "#1a1c21", "#262930"
    text, muted, accent = "#f4f4f5", "#a7abb5", "#cab7fb"

    def label(parent, caption="", *, value=None, size=13, tone=text, bold=False):
        return ctk.CTkLabel(parent, text=caption, textvariable=value, text_color=tone,
                            font=ctk.CTkFont(family="Segoe UI", size=size, weight="bold" if bold else "normal"),
                            anchor="w")

    def button(parent, text, callback, *, primary=False, value=None, width=80):
        return ctk.CTkButton(parent, text=text, textvariable=value, command=callback, width=width,
                             height=34, corner_radius=8, fg_color=accent if primary else raised,
                             hover_color="#ddceff" if primary else "#353945",
                             text_color="#171120" if primary else "#f4f4f5")

    form = ctk.CTkFrame(window, fg_color="transparent")
    form.pack(fill="both", expand=True, padx=24, pady=20)
    header = ctk.CTkFrame(form, fg_color="transparent")
    header.pack(fill="x", pady=(0, 11))
    label(header, "Clawdmeter", size=28, bold=True).pack(side="left")
    state_text = tk.StringVar(value="Ищу экран…")
    label(header, value=state_text, tone=accent, bold=True).pack(side="right")
    limit_text = tk.StringVar()
    label(form, value=limit_text, tone=muted).pack(anchor="w", pady=(0, 10))

    tabs = ctk.CTkTabview(form, fg_color="transparent", corner_radius=10,
                          segmented_button_fg_color=surface,
                          segmented_button_selected_color=raised,
                          segmented_button_selected_hover_color="#353945")
    tabs.pack(fill="both", expand=True)
    controls_tab = tabs.add("Управление")
    screen_time_tab = tabs.add("Экранное время")

    controls = ctk.CTkFrame(controls_tab, fg_color=surface, corner_radius=14)
    controls.pack(fill="x")
    label(controls, "Экран", size=16, bold=True).pack(anchor="w", padx=16, pady=(10, 4))
    pages = ctk.CTkFrame(controls, fg_color="transparent")
    pages.pack(fill="x", padx=16)
    for index, name in enumerate(PAGES):
        button(pages, name, lambda i=index: control(page=i)).pack(side="left", padx=(0, 6))
    button(pages, "Заставка", lambda: control(mode="splash")).pack(side="left", padx=(0, 6))
    button(pages, "Игра", lambda: control(mode="game"), width=66).pack(side="left")
    settings = ctk.CTkFrame(controls, fg_color="transparent")
    settings.pack(fill="x", padx=16, pady=(10, 14))
    pin_text = tk.StringVar(value="Закрепить")
    button(settings, "", lambda: control(pin=not last_state.get("pinned", False)),
           value=pin_text, width=108).pack(side="left", padx=(0, 16))
    label(settings, "Яркость", tone=muted).pack(side="left")
    brightness_values = ("25%", "50%", "78%", "100%")
    brightness = ctk.CTkOptionMenu(settings, values=list(brightness_values), width=76,
                                   fg_color=raised, button_color="#353945",
                                   command=lambda value: control(brightness=brightness_values.index(value)))
    brightness.set("—")
    brightness.pack(side="left", padx=(6, 16))
    label(settings, "Смена, сек.", tone=muted).pack(side="left")
    interval_var = tk.StringVar()
    interval = ctk.CTkEntry(settings, textvariable=interval_var, width=47, fg_color=raised, border_width=0)
    interval.pack(side="left", padx=6)

    def change_interval():
        try:
            seconds = int(interval_var.get())
            if not 3 <= seconds <= 60:
                raise ValueError()
        except ValueError:
            action.set("Интервал должен быть от 3 до 60 секунд.")
            return
        control(interval=seconds)

    button(settings, "Применить", change_interval, width=100).pack(side="left")

    notes = ctk.CTkFrame(controls_tab, fg_color=surface, corner_radius=14)
    notes.pack(fill="both", expand=True, pady=12)
    label(notes, "Заметки", size=16, bold=True).pack(anchor="w", padx=16, pady=(10, 1))
    label(notes, "Одно дело на строку", tone=muted).pack(anchor="w", padx=16, pady=(0, 7))
    editor = ctk.CTkTextbox(notes, height=120, wrap="word", fg_color="#101114", text_color=text,
                           corner_radius=9, font=ctk.CTkFont(family="Segoe UI", size=15), padx=10, pady=8)
    editor.pack(fill="both", expand=True, padx=16)
    try:
        saved = NOTE_FILE.read_text(encoding="utf-8")
    except OSError:
        saved = ""
    editor.insert("1.0", saved)
    completion = tk.StringVar()
    label(notes, value=completion, tone=muted).pack(anchor="w", padx=16, pady=(5, 0))
    button(notes, "Отправить список", lambda: submit(), primary=True, width=150).pack(
        anchor="w", padx=16, pady=(5, 12))
    action = tk.StringVar(value="Ctrl+Enter отправляет список. Пустой текст очищает его.")
    label(form, value=action, tone=muted).pack(anchor="w")
    screen_values = {}
    for key, caption in (("screenToday", "Сегодня"), ("screenWeek", "Последние 7 дней"),
                         ("screenTotal", "Всё время с установки счётчика"),
                         ("chargeAge", "С последней зарядки")):
        card = ctk.CTkFrame(screen_time_tab, fg_color=surface, corner_radius=14)
        card.pack(fill="x", pady=(9, 0))
        label(card, caption, tone=muted).pack(anchor="w", padx=16, pady=(10, 0))
        value = screen_values[key] = tk.StringVar(value="—")
        label(card, value=value, size=25, bold=True).pack(anchor="w", padx=16, pady=(0, 10))
    display_state = tk.StringVar(value="Счётчик идёт, пока дисплей включён, включая заставку.")
    label(screen_time_tab, value=display_state, tone=muted).pack(anchor="w", pady=(12, 2))
    results = Queue()
    busy = False
    last_state = {}

    def age(seconds):
        if seconds < 0:
            return "данных пока нет"
        return "только что" if seconds < 60 else f"{seconds // 60} мин назад"

    def finish():
        nonlocal busy, last_state, saved
        try:
            response, message, sent = results.get_nowait()
        except Empty:
            window.after(60, finish)
            return
        busy = False
        if sent is not None:
            saved = sent
        if message:
            action.set(message)
        if not response:
            state_text.set("Экран не ответил по Wi‑Fi")
            limit_text.set("")
            return
        last_state = response
        if "displayLit" in response:
            display_state.set("Дисплей включён · счётчик идёт" if response["displayLit"]
                              else "Дисплей спит · счётчик на паузе")
        for key, display in screen_values.items():
            seconds = response.get(key)
            if key == "chargeAge" and isinstance(seconds, int) and seconds < 0:
                display.set("На внешнем питании" if seconds == -2 else "Жду отключения зарядки")
                continue
            if isinstance(seconds, int) and seconds >= 0:
                hours, remainder = divmod(seconds, 3600)
                minutes, seconds = divmod(remainder, 60)
                display.set(f"{hours:02d}:{minutes:02d}:{seconds:02d}")
        mode = {"game": "Игра", "splash": "Заставка", "demo": "Тестовый режим"}.get(
            response["mode"], PAGES[response["page"]])
        state_text.set(f"На связи · {mode}" + (" · закреплено" if response["pinned"] else ""))
        limit_text.set(f"Claude: {age(response['claudeAge'])}     Codex: {age(response['codexAge'])}")
        pin_text.set("Открепить" if response["pinned"] else "Закрепить")
        brightness.set(brightness_values[LEVELS.index(response["brightness"])])
        if not str(window.focus_get()).startswith(str(interval)):
            interval_var.set(str(response["interval"]))
        items = [line.strip() for line in saved.split("\n") if line.strip()]
        checked = [name for index, name in enumerate(items) if response["checks"] & (1 << index)]
        completion.set(f"Выполнено на экране: {len(checked)}/{len(items)}" +
                       (" · " + ", ".join(checked) if checked else ""))

    def run(settings=None, note=None, quiet=False):
        nonlocal busy
        if busy:
            if not quiet:
                action.set("Дождись ответа экранчика и повтори действие.")
            return
        busy = True

        def worker():
            sent = None
            message = ""
            response = None
            try:
                sync = WifiSync()
                if note is not None:
                    if sync.send({"note": note}):
                        NOTE_FILE.write_text(note, encoding="utf-8")
                        sent = note
                        message = "Список отправлен."
                    else:
                        message = "Не удалось отправить список."
                response = sync.control(**(settings or {}))
                if settings and response:
                    message = "Настройка применена."
            except (OSError, ValueError, KeyError):
                message = "Не удалось связаться с экраном."
            results.put((response, message, sent))

        Thread(target=worker, daemon=True).start()
        window.after(60, finish)

    def control(**settings):
        run(settings=settings)

    def submit(_event=None):
        note = editor.get("1.0", "end-1c")
        if sum(bool(line.strip()) for line in note.split("\n")) > 12:
            action.set("Список может содержать до 12 дел.")
        elif len(json.dumps({"note": note}, ensure_ascii=False, separators=(",", ":")).encode()) > 512:
            action.set("Список слишком длинный. Сократи текст.")
        else:
            run(note=note)
        return "break"

    def refresh():
        run(quiet=True)
        window.after(4000, refresh)

    window.bind("<Control-Return>", submit)
    interval.bind("<Return>", lambda _: change_interval())
    refresh()
    window.mainloop()


if __name__ == "__main__":
    main()
