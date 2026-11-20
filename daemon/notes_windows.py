"""Desktop editor for the note displayed on Clawdmeter."""

import json
from queue import Empty, Queue
from threading import Thread
import tkinter as tk
from tkinter import ttk

from wifi_sync_windows import CONFIG, WifiSync

NOTE_FILE = CONFIG.with_name("note.txt")


def main():
    window = tk.Tk()
    window.title("Заметки Clawdmeter")
    window.minsize(400, 230)
    form = ttk.Frame(window, padding=14)
    form.pack(fill="both", expand=True)
    ttk.Label(form, text="Список для экранчика · одно дело на строку").pack(anchor="w")
    editor = tk.Text(form, width=45, height=8, wrap="word", font=("Segoe UI", 12))
    editor.pack(fill="both", expand=True, pady=8)
    try:
        editor.insert("1.0", NOTE_FILE.read_text(encoding="utf-8"))
    except FileNotFoundError:
        pass
    status = tk.StringVar(value="Ctrl+Enter — отправить. Пустой текст очищает экран.")
    results = Queue()

    def finish():
        try:
            status.set(results.get_nowait())
        except Empty:
            window.after(100, finish)
            return
        button.state(["!disabled"])

    def submit(_event=None):
        if button.instate(["disabled"]):
            return "break"
        note = editor.get("1.0", "end-1c")
        if sum(bool(line.strip()) for line in note.split("\n")) > 12:
            status.set("На экран можно отправить до 12 дел.")
            return "break"
        payload = {"note": note}
        if len(json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode()) > 512:
            status.set("Заметка слишком длинная. Сократи текст и попробуй снова.")
            return "break"
        button.state(["disabled"])
        status.set("Отправляю по Wi‑Fi…")

        def send_note():
            try:
                delivered = WifiSync().send(payload)
            except (OSError, ValueError):
                delivered = False
            if not delivered:
                results.put("Экран не ответил. Проверь питание и Wi‑Fi, затем повтори.")
                return
            try:
                NOTE_FILE.write_text(note, encoding="utf-8")
            except OSError:
                results.put("Экран получил заметку. Локальная копия не сохранилась.")
                return
            results.put("Заметка отправлена." if note else "Заметка очищена.")

        Thread(target=send_note, daemon=True).start()
        window.after(100, finish)
        return "break"

    button = ttk.Button(form, text="Отправить", command=submit)
    button.pack(anchor="w")
    ttk.Label(form, textvariable=status, wraplength=370).pack(anchor="w", pady=(8, 0))
    window.bind("<Control-Return>", submit)
    editor.focus_set()
    window.mainloop()


if __name__ == "__main__":
    main()
