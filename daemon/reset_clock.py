"""Format a reset timestamp in the Windows local timezone."""

from datetime import datetime


def reset_clock(timestamp) -> str:
    try:
        epoch = float(timestamp)
        return datetime.fromtimestamp(epoch).strftime("%H:%M") if epoch > 0 else ""
    except (TypeError, ValueError, OverflowError, OSError):
        return ""
