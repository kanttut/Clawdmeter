"""Send authenticated, encrypted limit updates to a provisioned Clawdmeter."""

import asyncio
from datetime import datetime
import hmac
import json
import os
from pathlib import Path
import secrets
import socket
import time

from cryptography.hazmat.primitives.ciphers.aead import AESGCM

CONFIG = Path(os.environ["LOCALAPPDATA"]) / "Clawdmeter" / "wifi.json"
PORT = 43621


class WifiSync:
    def __init__(self, config=None):
        if config is None:
            config = json.loads(CONFIG.read_text(encoding="utf-8"))
        self.key = bytes.fromhex(config["key"])
        self.cipher = AESGCM(self.key)
        self.targets = dict.fromkeys((config["ip"], config["broadcast"], "255.255.255.255"))

    def send(self, payload):
        return self._exchange(payload) is not None

    def control(self, **settings):
        response = self._exchange({"control": True, **settings})
        return json.loads(response) if response else None

    def _exchange(self, payload):
        if "note" not in payload:
            now = datetime.now().astimezone()
            payload = {**payload, "localTime": int(now.timestamp() + now.utcoffset().total_seconds())}
        data = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        if len(data) > 512:
            raise ValueError("Limits packet exceeds the firmware buffer")
        nonce = secrets.token_bytes(12)
        message = nonce + self.cipher.encrypt(nonce, data, None)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            for address in self.targets:
                try:
                    udp.sendto(message, (address, PORT))
                except OSError:
                    continue
                deadline = time.monotonic() + 0.8
                while (remaining := deadline - time.monotonic()) > 0:
                    udp.settimeout(remaining)
                    try:
                        reply, peer = udp.recvfrom(352)
                    except OSError:
                        break
                    signature, response = reply[:32], reply[32:]
                    if hmac.compare_digest(signature, hmac.digest(self.key, nonce + response, "sha256")):
                        self.last_peer = peer[0]
                        return response
        return None


async def run_wifi_sync(meter, stop_event, tray_state):
    from daemon.codex_usage_windows import read_codex_limits

    sync = WifiSync()
    meter.log("Clawdmeter Wi-Fi sync started")
    while not stop_event.is_set():
        try:
            token = meter.read_token()
            payload = await meter.poll_api(token) if token else None
        except meter.AuthError:
            payload = None
        try:
            codex = await read_codex_limits()
        except (OSError, TimeoutError, ValueError):
            codex = None
        if codex is not None:
            payload = payload or {"ok": False}
            payload["codex"] = codex
        delivered = False
        if payload:
            try:
                delivered = await asyncio.to_thread(sync.send, payload)
            except (OSError, ValueError):
                pass
        if delivered:
            meter.log("Wi-Fi limits acknowledged" +
                      (" (Claude and Codex)" if payload.get("ok") and codex else ""))
            if tray_state:
                tray_state.set_connected(time.time())
        elif tray_state:
            tray_state.set_scanning()
        try:
            await asyncio.wait_for(stop_event.wait(), timeout=meter.POLL_INTERVAL)
        except TimeoutError:
            pass
