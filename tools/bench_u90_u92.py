#!/usr/bin/env python3
"""U-90 / U-92 device rows over the car's WebSocket (ws://<ip>:81/).

Everything here is a BENCH row from TRACKS/DEVICE-TESTS.md, driven against real
hardware. Nothing is inferred: a row prints PASS only when the car's own bytes
say so, and every assertion names the evidence it saw.

Usage:
    python tools/bench_u90_u92.py 192.168.1.100            # safe rows only
    python tools/bench_u90_u92.py 192.168.1.100 --switch   # + rows that move the radio
"""
import argparse
import asyncio
import json
import sys
import urllib.request

import websockets

WS_PATH = "/"
AP_NAME = "4WDCar_Wifi"

results: list[tuple[str, bool, str]] = []


def record(row: str, ok: bool, evidence: str) -> bool:
    results.append((row, ok, evidence))
    print(f"{'PASS' if ok else 'FAIL'}  {row}\n        {evidence}")
    return ok


class Car:
    """One WebSocket session; commands in, text frames collected out."""

    def __init__(self, url: str):
        self.url = url
        self.frames: list[str] = []

    async def __aenter__(self):
        self.ws = await websockets.connect(self.url, open_timeout=10, close_timeout=2)
        # Drain the connect-time burst (the car sends STATE + CAPS + NETW the
        # moment a client attaches) so a row's window starts from silence.
        await self.collect(1.2)
        self.frames = []
        return self

    async def __aexit__(self, *exc):
        await self.ws.close()

    async def send(self, line: str) -> None:
        await self.ws.send(line)

    async def collect(self, seconds: float) -> list[str]:
        """Drain inbound frames for a window."""
        got: list[str] = []
        loop = asyncio.get_event_loop()
        end = loop.time() + seconds
        while True:
            left = end - loop.time()
            if left <= 0:
                break
            try:
                msg = await asyncio.wait_for(self.ws.recv(), timeout=left)
            except asyncio.TimeoutError:
                break
            if isinstance(msg, (bytes, bytearray)):
                msg = msg.decode("utf-8", "replace")
            got.append(msg)
        self.frames.extend(got)
        return got


def http_json(ip: str, path: str = "/status") -> dict:
    with urllib.request.urlopen(f"http://{ip}{path}", timeout=8) as r:
        return json.loads(r.read().decode("utf-8"))


def http_text(ip: str, path: str = "/") -> tuple[int, str]:
    with urllib.request.urlopen(f"http://{ip}{path}", timeout=15) as r:
        return r.status, r.read().decode("utf-8", "replace")


async def count_prefix(car: Car, prefix: str, seconds: float) -> int:
    """Count frames starting with `prefix` inside a window.

    The car re-broadcasts STATE + CAPS + NETW every STATE_INTERVAL_MS (1500 ms,
    Config.h:75) for as long as ANY transport is connected, so an ABSOLUTE frame
    count proves nothing about the command under test. Every "one command, one
    answer" row must therefore count the EXTRA answers a command adds on top of
    the idle broadcast — an absolute count is a guaranteed-false row.
    """
    got = await car.collect(seconds)
    return sum(1 for f in got if f.startswith(prefix))


def fields(frame: str) -> dict[str, str]:
    """`STATE;MODE=4WD4M;SPD=255;...` -> {'MODE': '4WD4M', ...}."""
    out: dict[str, str] = {}
    for part in frame.split(";")[1:]:
        if "=" in part:
            k, v = part.split("=", 1)
            out[k] = v
    return out


async def latest_state(car: Car, seconds: float = 3.0) -> dict[str, str] | None:
    """Double-read: the car broadcasts STATE every 1.5 s, and a read taken
    right after a switch can catch the PRE-change broadcast (U-82 lesson)."""
    best = None
    seen = 0
    loop = asyncio.get_event_loop()
    end = loop.time() + seconds
    while loop.time() < end and seen < 2:
        try:
            msg = await asyncio.wait_for(car.ws.recv(), timeout=max(0.05, end - loop.time()))
        except asyncio.TimeoutError:
            break
        if isinstance(msg, (bytes, bytearray)):
            msg = msg.decode("utf-8", "replace")
        car.frames.append(msg)
        if msg.startswith("STATE"):
            best = fields(msg)
            seen += 1
    return best


# --------------------------------------------------------------------------- #
# Rows that do NOT move the radio (safe to run unattended).
# --------------------------------------------------------------------------- #
async def safe_rows(ip: str) -> None:
    url = f"ws://{ip}:81{WS_PATH}"

    # --- U-92-1: the double entry is gone -------------------------------
    st = http_json(ip)
    nets = st.get("networks", [])
    own = [n for n in nets if n.strip().lower() == AP_NAME.lower()]
    record(
        "U-92-1  hotspot appears EXACTLY ONCE in the registry",
        len(own) == 1,
        f"networks={nets} (own-hotspot rows={len(own)})",
    )

    # --- U-92-4: refusals hold, one answer each -------------------------
    async with Car(url) as car:
        await car.send(f"ROUTERS;ADD;{AP_NAME};somepass")
        got = await car.collect(4.5)
        adds = [f for f in got if "ROUTERS;" in f and ("Reserved" in f or "ERROR" in f)]
        record(
            "U-92-4a ROUTERS;ADD;<own>;pass -> exactly one ROUTERS;ERROR;Reserved",
            len(adds) == 1 and "Reserved" in adds[0],
            f"answers={adds}",
        )

    async with Car(url) as car:
        await car.send(f"WIFICFG;{AP_NAME};somepass")
        got = await car.collect(4.5)
        cfg = [f for f in got if "WIFICFG;" in f]
        record(
            "U-92-4b WIFICFG;<own>;pass -> exactly one WIFICFG;ERROR;Reserved",
            len(cfg) == 1 and "Reserved" in cfg[0],
            f"answers={cfg}",
        )

    # The refusals must not have poisoned anything.
    st2 = http_json(ip)
    record(
        "U-92-4c the registry still holds no second own-hotspot row",
        len([n for n in st2.get("networks", []) if n.strip().lower() == AP_NAME.lower()]) == 1,
        f"networks={st2.get('networks')}",
    )

    # --- U-90-5: an unknown mode still refuses, exactly one NACK --------
    async with Car(url) as car:
        before = await latest_state(car, 3.0)
        await car.send("MODE_ESP_SER")
        got = await car.collect(5.0)
        nacks = [f for f in got if f.startswith("NACK") and "UNKNOWN_MODE" in f]
        record(
            "U-90-5 MODE_ESP_SER -> exactly one NACK;E=UNKNOWN_MODE",
            len(nacks) == 1,
            f"nacks={nacks} (all frames={got})",
        )
        after = await latest_state(car, 4.0)
        record(
            "U-90-5b the car stayed in its mode after the refusal",
            bool(before) and bool(after) and before.get("MODE") == after.get("MODE"),
            f"mode before={before.get('MODE') if before else None} after={after.get('MODE') if after else None}",
        )

    # --- U-90-4: a coming-soon mode really changes the car --------------
    async with Car(url) as car:
        await car.send("MODE_OBS_US")
        await car.collect(2.0)
        st_obs = await latest_state(car, 5.0)
        await car.send("4WD4M")
        await car.collect(2.0)
        st_live = await latest_state(car, 5.0)
        record(
            "U-90-4 coming-soon mode OBS_US is selectable and changes the car",
            bool(st_obs) and st_obs.get("MODE") == "OBS_US",
            f"after OBS_US mode={st_obs.get('MODE') if st_obs else None}",
        )
        record(
            "U-90-4b 4WD4M restores drive",
            bool(st_live) and st_live.get("MODE") == "4WD4M",
            f"after 4WD4M mode={st_live.get('MODE') if st_live else None}",
        )

    # --- U-88-1 / U-81-1: one command, one answer ----------------------
    # `REQ_STATE` is a REAL command (ModeManager.cpp:309). `REQ_CAPS` IS NOT —
    # the firmware has no such verb, and a row that sends it will always NACK.
    # CAPS rides the same sendState() as STATE (ModeManager.cpp:542-545), so
    # REQ_STATE is what proves the U-81 text transport is not a sink.
    async with Car(url) as car:
        base_caps = await count_prefix(car, "CAPS", 4.5)
        base_state = await count_prefix(car, "STATE", 4.5)
        await car.send("REQ_STATE")
        got = await car.collect(4.5)
        caps = [f for f in got if f.startswith("CAPS")]
        states = [f for f in got if f.startswith("STATE")]
        record(
            "U-81-1  REQ_STATE adds exactly ONE CAPS frame (the write() is not a sink)",
            len(caps) - base_caps == 1,
            f"idle CAPS/4.5s={base_caps}, command window={len(caps)} "
            f"(delta={len(caps) - base_caps}); caps={caps}",
        )
        record(
            "U-88-1  REQ_STATE adds exactly ONE STATE frame (one command, one answer)",
            len(states) - base_state == 1,
            f"idle STATE/4.5s={base_state}, command window={len(states)} "
            f"(delta={len(states) - base_state})",
        )
        nacks = [f for f in got if f.startswith("NACK")]
        record(
            "U-88-1b REQ_STATE is not refused (no NACK rides the answer)",
            not nacks,
            f"nacks={nacks}",
        )

    # --- U-92-3 / U-81-8: the page builds its placeholder from car truth
    # The static HTML SHIPS the neutral "— Select router —" option on purpose
    # (WebPage.h:462) and the "Now on: <ssid>" text is written at runtime by the
    # 1 Hz JSON handler from the car's own `ssid` (WebPage.h:286). A raw GET
    # cannot see a RENDERED value, so these two rows prove the code paths and the
    # truth they read; the rendered string is the owner's-eyes row in
    # DEVICE-TESTS.md U-92-3. Do not "fix" this row by asserting the page text.
    status, page = http_text(ip, "/")
    cur = http_json(ip).get("ssid", "")
    record(
        "U-92-3a the page builds the placeholder from the car's own ssid",
        "Now on: " in page and "j.ssid" in page,
        f"HTTP {status}, {len(page)} bytes, runtime builder present="
        f"{'Now on: ' in page and 'j.ssid' in page}",
    )
    record(
        "U-92-3b the car's current network is real, and nothing is preselected",
        bool(cur) and "— Select router —" in page,
        f"car ssid={cur!r} (the value the page renders); static neutral "
        f"placeholder shipped={'— Select router —' in page}",
    )
    record(
        "U-81-8 the root page loads completely (bytes + markers)",
        status == 200 and "handleTextLine" in page,
        f"HTTP {status}, {len(page)} bytes, handleTextLine present={'handleTextLine' in page}",
    )


# --------------------------------------------------------------------------- #
# Rows that MOVE the radio — opt-in via --switch.
# --------------------------------------------------------------------------- #
async def switch_rows(ip: str, current: str) -> None:
    url = f"ws://{ip}:81{WS_PATH}"
    if current.lower() == AP_NAME.lower():
        print("\n--switch skipped: the car is already on its own hotspot.\n")
        return
    async with Car(url) as car:
        await car.send(f"ROUTERS;USE;{AP_NAME}")
        got = await car.collect(6.0)
        used = [f for f in got if "ROUTERS;USED;" in f]
        record(
            "U-92-2a USE <own hotspot> answers exactly one ROUTERS;USED",
            len(used) == 1,
            f"answers={used}",
        )
    # The car must leave the router; the socket will drop, so poll HTTP.
    left = False
    seen_ip = ""
    import time

    t0 = time.time()
    while time.time() - t0 < 45:
        time.sleep(3)
        try:
            st = http_json(ip)
        except Exception:
            left = True  # router address no longer answers
            break
        seen_ip = st.get("ip", "")
        if st.get("ssid", "").strip().lower() == AP_NAME.lower():
            left = True
            break
    record(
        "U-92-2b the car LEFT the router after 'back to my own hotspot'",
        left,
        f"router still answers ip={seen_ip!r} ssid={http_json(ip).get('ssid') if not left else 'n/a (unreachable)'}",
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("ip")
    ap.add_argument("--switch", action="store_true", help="also run the rows that move the radio")
    args = ap.parse_args()

    st = http_json(args.ip)
    print(f"car at {args.ip}: mode={st.get('mode')} ssid={st.get('ssid')} "
          f"ip={st.get('ip')} fw-caps={st.get('caps')}\n")

    asyncio.run(safe_rows(args.ip))
    if args.switch:
        asyncio.run(switch_rows(args.ip, st.get("ssid", "")))

    failed = [r for r in results if not r[1]]
    print(f"\n=== {len(results) - len(failed)}/{len(results)} rows passed ===")
    for row, _, ev in failed:
        print(f"  FAIL {row}  ({ev})")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())