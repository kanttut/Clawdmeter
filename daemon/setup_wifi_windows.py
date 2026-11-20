"""Configure Clawdmeter over USB with a local, masked Wi-Fi password field."""

import ipaddress
import json
import secrets
import threading
import time
import tkinter as tk
from tkinter import ttk

import serial
from serial.tools.list_ports import comports

from wifi_sync_windows import CONFIG, WifiSync


def configure(ssid, password):
    ports = [port.device for port in comports()
             if (port.vid, port.pid) == (0x303A, 0x1001)]
    if len(ports) != 1:
        return "Подключи одну плату Clawdmeter по USB и повтори."

    key = secrets.token_hex(32)
    settings = json.dumps({"ssid": ssid, "password": password, "key": key},
                          ensure_ascii=False, separators=(",", ":"))
    port = ports[0]
    try:
        with serial.Serial(port, 115200, timeout=0.4, write_timeout=3) as device:
            device.write(("wifi-config " + settings + "\n").encode("utf-8"))
            until = time.monotonic() + 5
            while time.monotonic() < until:
                line = device.readline()
                if b"WIFI_CONFIG_ERROR" in line:
                    return "Плата не сохранила настройки. Проверь имя сети и пароль."
                if b"WIFI_CONFIG_SAVED" in line:
                    break
    except serial.SerialException:
        pass  # USB can disappear briefly after the firmware saves and reboots.

    until = time.monotonic() + 45
    while time.monotonic() < until:
        try:
            with serial.Serial(port, 115200, timeout=0.5, write_timeout=2) as device:
                device.write(b"wifi-status\n")
                for _ in range(4):
                    line = device.readline().decode("utf-8", errors="ignore").strip()
                    if line.startswith("WIFI_READY "):
                        _, ip, mask = line.split()
                        network = ipaddress.IPv4Interface(f"{ip}/{mask}").network
                        config = {
                            "ip": ip, "broadcast": str(network.broadcast_address), "key": key,
                        }
                        if not WifiSync(config).send({"ok": False}):
                            time.sleep(0.5)
                            continue
                        CONFIG.parent.mkdir(parents=True, exist_ok=True)
                        staged = CONFIG.with_suffix(".tmp")
                        staged.write_text(json.dumps(config), encoding="utf-8")
                        staged.replace(CONFIG)
                        return None
        except serial.SerialException:
            time.sleep(0.7)
    return "Не удалось подключиться. Проверь пароль и доступность сети 2,4 ГГц."


def main():
    window = tk.Tk()
    window.title("Clawdmeter · Настройка Wi‑Fi")
    window.resizable(False, False)
    window.attributes("-topmost", True)
    form = ttk.Frame(window, padding=18)
    form.grid(sticky="nsew")
    ttk.Label(form, text="Сеть Wi‑Fi 2,4 ГГц в той же локальной сети, что и ПК").grid(sticky="w")
    ttk.Label(form, text="Имя сети (SSID)").grid(sticky="w", pady=(12, 0))
    ssid_entry = ttk.Entry(form, width=45)
    ssid_entry.grid(sticky="ew")
    ttk.Label(form, text="Пароль").grid(sticky="w", pady=(8, 0))
    password_entry = ttk.Entry(form, show="*", width=45)
    password_entry.grid(sticky="ew")
    status = tk.StringVar()
    result = []

    def check_result():
        if not result:
            window.after(200, check_result)
            return
        message = result.pop()
        if message is None:
            password_entry.delete(0, "end")
            status.set("Clawdmeter подключён к Wi‑Fi.")
            button.configure(text="Закрыть", command=window.destroy, state="normal")
        else:
            status.set(message)
            button.configure(state="normal")

    def submit():
        ssid, password = ssid_entry.get(), password_entry.get()
        if not 1 <= len(ssid.encode("utf-8")) <= 32 or len(password.encode("utf-8")) > 64:
            status.set("Проверь длину имени сети и пароля.")
            return
        button.configure(state="disabled")
        status.set("Подключаю Clawdmeter. Оставь USB подключённым.")

        def worker():
            try:
                result.append(configure(ssid, password))
            except (OSError, ValueError):
                result.append("Не удалось завершить настройку. Повтори подключение.")

        threading.Thread(target=worker, daemon=True).start()
        window.after(200, check_result)

    button = ttk.Button(form, text="Подключить", command=submit)
    button.grid(sticky="w", pady=(12, 0))
    ttk.Label(form, textvariable=status, wraplength=340).grid(sticky="w", pady=(8, 0))
    ssid_entry.focus_set()
    window.mainloop()


if __name__ == "__main__":
    main()
