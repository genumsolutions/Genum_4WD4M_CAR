# RUNSHEET — flash 1.3.10 + the U-111 / U-112 device pass

> One sheet for the owner's next bench session. Firmware **1.3.10** (UNFLASHED;
> 1.3.9 IS on the car) +
> app **3.2.7 / versionCode 60** (OTA published; live manifest = commit `e5caaa8`).
> Tick the rows in the row sheets as you go: car `TRACKS/DEVICE-TESTS.md`
> (U-112-1..6 first — the reset/freeze round — then U-111-1..7) · app
> `mobile/TESTING.md` (U-112-1..3, then U-111-1..6).
> **F-61: every gate behind this build is green and none of it is device evidence.**

## 0 · What this one flash carries (1.3.9 → 1.3.10, plus everything below it)

The bench car is on an older build. One flash of `main` brings all of it:

| Round | What you should notice |
|---|---|
| U-110 (1.3.7) | OLED last two rows = `Bluetooth : 4WD4M Car` / `Type : SPP`, rotating with `WiFi : <ssid>` / `IP : <car ip>`; the advertised BT name is **`4WD4M Car`** (was `4WD CAR`); Bluetooth starts after the radio's resting `AP_STA` mode; `btUp` is honest and retries a refused bring-up every 2 s |
| U-111 (1.3.8) | **Bluetooth pairing completes** (SSP "Just Works" — no PIN; `1234` only as legacy fallback); the own hotspot no longer re-runs `softAP()` every loop pass (the freeze-shaped radio churn); the 11 dBm AP cap is boot-window only — 17 dBm again after 15 s |
| U-111b (1.3.9, ON THE CAR) | **The Internet / cloud-relay method is gone** — no `[RELAY]` log lines, no `RELAYCFG`, the OLED link row only ever reads Bluetooth / WiFi |
| **U-112 (1.3.10)** | **The reset-while-pairing and freeze-while-page-open round.** The status broadcast defers below a 49 kB heap floor (time-boxed 4 s); scans refuse below 65 kB with an honest BUSY; the page send aborts a reader that stops draining, so the app link keeps running while a page is open. Serial grows `[HEAP] …` and `[SCAN] refused` lines — that is the evidence this flash exists to produce |
| **U-114 (1.3.10)** | **Build/wiring fix (no new radio behaviour).** HEAD did not compile and `/status` reported its neighbours' values for `wifi_enabled`/`bt_enabled`/`boot_autojoin` (the app adopts those). `/status` now also carries `tele_params":"freq=1600, vyse=0"` (additive, nothing reads it yet) |
| app U-110 | the "Connection lost / reconnect?" prompt appears only when **no** link is live, and both prompts are centered dialogs |
| app U-111 | the app adopts the car's Wi-Fi / Bluetooth / boot-auto-join settings on connect |
| app U-112 | **exactly TWO methods** — Bluetooth · WiFi (LAN); no Internet pill or card anywhere |

⚠ A firmware-version change wipes the saved routers + transport toggles on the
first boot (`wipeRoutersOnFreshFirmware()`): both transports ON, auto-join ON,
no router saved. Re-add your router in Step 5.

## 1 · Flash 1.3.10 (USB)

ESP32 Dev Module, **Partition Scheme = Huge APP (required)**, upload **115200**
(this CH340 fails at the default 921600):

    arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app .
    arduino-cli upload  --fqbn esp32:esp32:esp32:PartitionScheme=huge_app -p COM<port> --upload-property upload.speed=115200 .

(1.3.9 is already on the car if you flashed it — 1.3.10 is a normal upgrade
step, another version-change wipe of saved routers.)

Capture the first boot while you are at it (~15 s, resets the board):

    python tools/serial_capture.py COM<port> 15 boot-1.3.10.log

**Expect in that log:** `[BOOT] Restored mode=4WD4M`, the router-wipe line
(version change), `Own AP active: 4WD4MCar_WiFi`, and
`[V2] Web layer always-on: AP=4WD4MCar_WiFi ip=192.168.245.1 http=:80 ws=:81`
— **`AP=` must NOT be empty** (that was U-109's bug). No
`[RADIO] skip-softAP/sta-busy`, no `[RELAY]` lines.

## 2 · Get the OTA onto the phone

Force-close the app **twice** (close + reopen ×2) so expo-updates loads the
published bundle. The live manifest
(`https://bkylfnlybtsujwzropru.supabase.co/storage/v1/object/public/app-releases/release.json`)
must read `notes: "OTA · Short update (e5caaa891d…)"`, `version 3.2.7`,
`version_code 60` — verified live 2026-10-14 16:23 UTC. The app's About screen
still says **3.2.7** (JS-only OTA — no APK, no bump).

## 3 · Bluetooth: forget and re-pair

1. Android Settings → Bluetooth → **forget the old `4WD CAR` bond** (that name
   is dead — the firmware no longer advertises it).
2. In the app, scan → pair to **`4WD4M Car`**. **No PIN is asked**; if one is
   ever requested it is `1234`.
3. The bond completes and the app's Bluetooth link opens.

→ rows: car **U-111-1**, **U-111-3**, **U-110-2**, **U-110-3**.

## 4 · Own hotspot: forget, re-join, read /status

Android caches a stale profile against an open AP — forget `4WD4MCar_WiFi`
once, re-join. Expect an `192.168.245.x` address, then open:

    http://192.168.245.1/status

Check `ap` = `4WD4MCar_WiFi` (**never empty**), `fw` = `1.3.10`, and the crash
fields (`crash_count`, `reset_reason`, the phase). Also check `tele_params` reads
`freq=1600, vyse=0` (U-114) and that `wifi_enabled`/`bt_enabled`/`boot_autojoin`
report your actual toggles, not `1600`/`0`. The page's mode dropdown and
router table must work.

→ rows: car **U-111-2**, **U-111-7**, **U-109-1..3**.

## 5 · Re-add a router (the wipe in Step 0 is expected)

App → Connectivity panel → Wi-Fi → add your router; the car switches and
reports the new IP on the OLED and in `/status`. Optional bench script for the
switch rows: `python tools/bench_u90_u92.py <car-ip>`.

## 6 · The U-112 checks (reset + freeze round) — do these FIRST

With both links live (BT + own hotspot) and the app connected: load the car's
page in the browser repeatedly and keep using the app — the telemetry must keep
moving while the page loads and nothing may reset (rows U-112-1..2). Watch the
serial for `[HTTP] root page paused`, `[HEAP] status broadcast
deferred/resumed` and `[SCAN] refused` — if the freeze STILL happens, capture
60 s of serial across it (`serial_capture.py COM<port> 60 u112.log`) and send
the `[CRASH] rtc: …` line from the next boot; it names phase + task + heap.

## 7 · Settings round-trip + the two-method panel

App → Remote → Settings: Wi-Fi / Bluetooth / Boot auto-join show the car's
real values on connect. Flip **Boot auto-join OFF** → confirm → power-cycle the
car → it comes back with auto-join OFF (persisted). Then check the
Connectivity panel offers exactly **Bluetooth** and **WiFi (LAN)** — no
Internet pill, globe or "not set up yet" card anywhere.

→ app rows: **U-112-1..3**, **U-111-4..6**, **U-110-5/6**.

## 8 · The freeze — capture it, do not guess (STILL OPEN)

U-111 removed one plausible cause (the AP retry churn) but the freeze is **not
claimed fixed**: every capture so far predates the own-hotspot fix, so none of
them can name this report's cause. If the car freezes:

1. Note what the OLED / app shows before you power-cycle, if you can.
2. Capture the serial log across the reset:
   `python tools/serial_capture.py COM<port> 30 freeze.log`
3. Report the **reset reason + phase + boot/crash counters**. U-106's RTC
   breadcrumb records phase + task + heap at the crash — that datum is the
   whole reason this round exists (rows U-111-6 / U-110-4 / U-R4-7).

## 9 · Still open after the pass

- **DB Phase 7c migration** — owner runs
  `genumsolutions-website/supabase/migrations/20261013120000_phase7c_fresh_start_cleanup.sql`
  in the Supabase SQL editor (replace `owner_user_id` first).
- Every row above stays **DEVICE VERIFY PENDING** until it is ticked: a green
  pipeline is a necessary gate, never a sufficient one (F-61).
