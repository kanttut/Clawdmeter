"""Read the signed-in Codex account's limits through its local app server."""

import asyncio
import json
import math
from pathlib import Path
import shutil
import subprocess
import time

from .reset_clock import reset_clock


def find_codex_cli() -> str | None:
    cli = shutil.which("codex.exe")
    if cli:
        return cli

    # The desktop app's bundled CLI is absent from PATH at Windows logon.
    # Resolve the registered package so this keeps working after app updates.
    try:
        package = subprocess.run(
            ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
             "[Console]::OutputEncoding = [Text.Encoding]::UTF8; "
             "Get-AppxPackage -Name 'OpenAI.Codex' | "
             "Sort-Object Version -Descending | Select-Object -ExpandProperty InstallLocation"],
            capture_output=True, encoding="utf-8", check=True, timeout=8,
            creationflags=subprocess.CREATE_NO_WINDOW,
        )
    except (OSError, subprocess.SubprocessError):
        return None

    for location in package.stdout.splitlines():
        bundled_cli = Path(location) / "app" / "resources" / "codex.exe"
        if location and bundled_cli.is_file():
            return str(bundled_cli)
    return None


async def read_codex_limits() -> dict | None:
    cli = await asyncio.to_thread(find_codex_cli)
    if not cli:
        return None

    process = await asyncio.create_subprocess_exec(
        cli, "app-server",
        stdin=asyncio.subprocess.PIPE,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.DEVNULL,
        creationflags=subprocess.CREATE_NO_WINDOW,
    )
    try:
        async with asyncio.timeout(12):
            requests = (
                {"id": 1, "method": "initialize",
                 "params": {"clientInfo": {"name": "clawdmeter", "version": "1.0"}}},
                {"method": "initialized"},
                {"id": 2, "method": "account/rateLimits/read"},
            )
            for request in requests:
                process.stdin.write(json.dumps(request).encode() + b"\n")
            await process.stdin.drain()
            while line := await process.stdout.readline():
                reply = json.loads(line)
                if reply.get("id") != 2:
                    continue
                result = reply.get("result", {})
                limits = (result.get("rateLimitsByLimitId") or {}).get("codex")
                limits = limits or result.get("rateLimits") or {}
                short = limits.get("primary") or {}
                weekly = limits.get("secondary") or {}
                if short.get("windowDurationMins") != 300 or weekly.get("windowDurationMins") != 10080:
                    return None
                if not all(isinstance(window.get("usedPercent"), (int, float))
                           for window in (short, weekly)):
                    return None
                now = time.time()
                payload = {}
                for window, percent, reset in ((short, "s", "sr"), (weekly, "w", "wr")):
                    payload[percent] = window["usedPercent"]
                    resets_at = window.get("resetsAt")
                    payload[reset] = max(0, round((resets_at - now) / 60)) if resets_at else -1
                    payload["sa" if reset == "sr" else "wa"] = reset_clock(resets_at)
                    payload["ss" if reset == "sr" else "ws"] = max(0, math.ceil(resets_at - now)) if resets_at else -1
                return payload
    finally:
        if process.returncode is None:
            try:
                process.terminate()
            except ProcessLookupError:
                pass
        await process.wait()
    return None
