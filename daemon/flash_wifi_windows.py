"""Upload a Clawdmeter application image using the installed Arduino OTA tool."""

import argparse
import hmac
import logging
from pathlib import Path
import runpy
import socket

from wifi_sync_windows import WifiSync


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--espota", required=True, type=Path)
    args = parser.parse_args()
    with args.image.open("rb") as image:
        header = image.read(36)
    if not header.startswith(b"\xe9") or header[32:36] != b"\x32\x54\xcd\xab":
        parser.error("Use the application firmware.bin, not a factory image.")
    sync = WifiSync()
    if not sync.send({"ok": False}):
        parser.exit(1, "Clawdmeter did not respond on the local network.\n")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
        route.connect((sync.last_peer, 3232))
        local_ip = route.getsockname()[0]
    secret = hmac.digest(sync.key, b"Clawdmeter OTA", "sha256").hex()
    ota = runpy.run_path(str(args.espota))
    ota["serve"].__globals__.update(TIMEOUT=24, PROGRESS=False)
    logging.basicConfig(level=logging.INFO)
    return ota["serve"](sync.last_peer, local_ip, 3232, 32323, secret, False, str(args.image))


if __name__ == "__main__":
    raise SystemExit(main())
