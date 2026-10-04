#!/usr/bin/env python3
"""Capture the car's serial console to a UTF-8 file (Windows stdout is cp1252
and the car's boot log carries bytes cp1252 cannot print)."""
import sys
import time

import serial


def capture(port: str, seconds: float, out_path: str, reset: bool) -> None:
    s = serial.Serial(port, 115200, timeout=0.2)
    if reset:
        s.dtr = False
        s.rts = False
        time.sleep(0.3)
        s.dtr = True  # EN/reset via DTR, GPIO0 held high by the CH340
        s.rts = True
        time.sleep(0.2)
        s.dtr = False
        s.rts = False
    buf = bytearray()
    t0 = time.time()
    while time.time() - t0 < seconds:
        chunk = s.read(4096)
        if chunk:
            buf += chunk
    s.close()
    with open(out_path, "wb") as fh:
        fh.write(bytes(buf))
    print(f"captured {len(buf)} bytes -> {out_path}")


if __name__ == "__main__":
    port = sys.argv[1] if len(sys.argv) > 1 else "COM9"
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 12
    out = sys.argv[3] if len(sys.argv) > 3 else "serial.log"
    do_reset = "--no-reset" not in sys.argv
    capture(port, secs, out, do_reset)