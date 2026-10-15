# RUNSHEET — flash 1.3.11 + the U-111 / U-112 device pass

> One sheet for the owner's next bench session. Firmware **1.3.11** (UNFLASHED;
> 1.3.9 IS on the car) +
> app **3.2.7 / versionCode 60** (OTA published; live manifest = commit `e5caaa8`).
> Tick the rows in the row sheets as you go: car `TRACKS/DEVICE-TESTS.md`
> (U-112-1..6 first — the reset/freeze round — then U-111-1..7) · app
> `mobile/TESTING.md` (U-112-1..3, then U-111-1..6).
> **F-61: every gate behind this build is green and none of it is device evidence.**

## 0 · What this one flash carries (1.3.9 → 1.3.11, plus everything below it)

The bench car is on an older build. One flash of `main` brings all of it:

| Round | What you should notice |
|---|---|
| U-110 (1.3.7) | OLED last two rows = `Bluetooth : 4WD4M Car` / `Type : SPP`, rotating with `WiFi : <ssid>` / `IP : <car ip>`; the advertised BT name is **`4WD4M Car`** (was `4WD CAR`); Bluetooth starts after the radio's resting `AP_STA` mode; `btUp` is honest and retries a refused bring-up every 2 s |
| U-111 (1.3.8) | **Bluetooth pairing completes** (SSP "Just Works" — no PIN; `1234` only as legacy fallback); the own hotspot no longer re-runs `softAP()` every loop pass (the freeze-shaped radio churn); the 11 dBm AP cap is boot-window only — 17 dBm again after 15 s |
| U-111b (1.3.9, ON THE CAR) | **The Internet / cloud-relay method is gone** — no `[RELAY]` log lines, no `RELAYCFG`, the OLED link row only ever reads Bluetooth / WiFi |
| **U-112 (1.3.11)** | **The reset-while-pairing and freeze-while-page-open round.** The status broadcast defers below a 49 kB heap floor (time-boxed 4 s); scans refuse below 65 kB with an honest BUSY; the page send aborts a reader that stops draining, so the app link keeps running while a page is open. Serial grows `[HEAP] …` and `[SCAN] refused` lines — that is the evidence this flash exists to produce |
| **U-114 (1.3.11)** | **Build/wiring fix (no new radio behaviour).** HEAD did not compile and `/status` reported its neighbours' values for `wifi_enabled`/`bt_enabled`/`boot_autojoin` (the app adopts those). `/status` now also carries `tele_params":"freq=1600, vyse=0"` (additive, nothing reads it yet) |
| **P4/E1 + diet (1.3.11)** | **The BT heap work (issue 6).** Serial gains one `[HEAPW] <stage> free=… max=…` line per radio stage: `boot`, `bt-stack`, `spp-connected`, `wifi-ap`, `page-served`, `both-links`. The diet's first step is also in: Bluetooth now comes up **classic-BT-only**, releasing the BLE controller/host memory this SPP-only car never uses. The six numbers (step **6b**) are therefore the **post-diet** figures the floor re-tune is read from |
| app U-110 | the "Connection lost / reconnect?" prompt appears only when **no** link is live, and both prompts are centered dialogs |
| app U-111 | the app adopts the car's Wi-Fi / Bluetooth / boot-auto-join settings on connect |
| app U-112 | **exactly TWO methods** — Bluetooth · WiFi (LAN); no Internet pill or card anywhere |

⚠ A firmware-version change wipes the saved routers + transport toggles on the
first boot (`wipeRoutersOnFreshFirmware()`): both transports ON, auto-join ON,
no router saved. Re-add your router in Step 5.

## 1 · Flash 1.3.11 (USB)

ESP32 Dev Module, **Partition Scheme = Huge APP (required)**, upload **115200**
(this CH340 fails at the default 921600):

    arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app .
    arduino-cli upload  --fqbn esp32:esp32:esp32:PartitionScheme=huge_app -p COM<port> --upload-property upload.speed=115200 .

> **If `arduino-cli` answers `Platform 'esp32:esp32' not found`:** on this box it
> defaults its data dir to `%TEMP%\opencode\tools\data` while the real install
> is in `%LOCALAPPDATA%\Arduino15` (esp32 core 3.3.12). Add
> `--config-file <file>` to both commands, with that file carrying
> `directories.data: C:\Users\caspe\AppData\Local\Arduino15`.
> **DONE (2026-10-15, COM8, CH340):** compiled 1820766 B (57%), hash-verified
> upload, post-flash reset `POWERON_RESET`, no new crash (same historical RTC
> `[CRASH]` record as before the flash), router store cleared by the
> version-change wipe, `AP=4WD4MCar_WiFi` non-empty. See the bench log in
> `TRACKS/DEVICE-TESTS.md`.

(1.3.9 is already on the car if you flashed it — 1.3.11 is a normal upgrade
step, another version-change wipe of saved routers.
**Now flashed: 1.3.11 is ON THE CAR** — 1.3.9 is superseded.)

Capture the first boot while you are at it (~15 s, resets the board):

    python tools/serial_capture.py COM<port> 15 boot-1.3.11.log

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

Check `ap` = `4WD4MCar_WiFi` (**never empty**), `fw` = `1.3.11`, and the crash
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

### 6b · E1 — capture the BT heap waterfall (P4, issue 6)

Issue 6 is *"Bluetooth ON collapses free heap 110 KB → ~10 KB; BT+Wi-Fi together
sometimes < 14 KB → the car resets"*. This build carries the diet's first step
(Bluetooth comes up **classic-BT-only**, so the unused BLE memory is released) AND
emits one breadcrumb per stage — so these are the **post-diet** numbers, and they
are what the floor re-tune is read from. Take the run in ONE uninterrupted window,
from a cold boot:

    python tools/serial_capture.py COM<port> 60 e1-heap-waterfall.log

While it runs: wait for the boot to finish → pair/connect Bluetooth → open the
car's own page in a browser → keep the app connected over Wi-Fi too. **Expect
exactly six lines, once each:**

    [HEAPW] boot        free=… max=…
    [HEAPW] wifi-ap     free=… max=…
    [HEAPW] bt-stack    free=… max=…
    [HEAPW] spp-connected free=… max=…
    [HEAPW] page-served free=… max=…
    [HEAPW] both-links  free=… max=…        ← the state issue 6 is about

**Read them in the order the car prints them, which is NOT the order
`Config.h:143` claims:** on 1.3.11 the own AP comes up *before* the BT stack
(`btComm.begin()` runs after the always-on web layer in `setup()`), so the real
sequence is `boot → wifi-ap → bt-stack → spp-connected → …`, and `page-served`
lands wherever the first browser hit happens. Six is the count that matters,
the order is not pinned by any gate.

**Already captured (2026-10-15, source-agent cold boot, NO phone attached):**
4 of the 6 — `boot free=181884 max=110580`, `wifi-ap free=123568 max=110580`,
`bt-stack free=23216 max=12276`, `both-links free=21716 max=10228`. **Still owed
from the owner's phone: `spp-connected` and `page-served`** (plus the live
`both-links` figure with the app connected over Wi-Fi) — without those two the
re-tune is reading a waterfall with a hole in it. Also visible in that log:
`[HEAP] status broadcast deferred (free=21716, floor=49152)` — the 49152 B floor
is unreachable once BT is up, so the broadcast is deferred by design of the
current constants, not by a new failure.

**Send back all six `free=` / `max=` pairs.** `max` is the largest contiguous
free block: if `free` stays high while `max` collapses, the problem is
FRAGMENTATION (the T-7b BT restart) rather than a leak — that is a different fix.
Also report the **deltas** `boot → bt-stack` and `wifi-ap → both-links`, and
whether the car reset anywhere inside the window.

→ Phase 0 capture **E1** in `TRACKS/PLAN-2026-10-15-dual-link-and-remote-ux.md`.
A missing stage means the waterfall has a hole; say which one is absent rather
than filling it in.

### 6c · E3 + E4 — the Wi-Fi switch, both ways (P5, issues 7 + 9)

P5 fixed issue 7 **in source**: `WIFICFG;WIFI;OFF` used to leave the own hotspot
broadcasting, because `stop()` dropped the beacon only `if (apRunning_)` — and
`handle()`'s AP retry then re-created the whole thing ~2 s later, having only
tested `!apRunning_`. The fix is in this flash, and **E3 is the proof it holds on
a real radio** (source gates cannot show this — F-61).

- **E3 — issue 7: OFF hides everything.** With BT ON, send `WIFICFG;WIFI;OFF`
  (app Connectivity panel, or the page console) and keep the phone's Wi-Fi scan
  open on the same phone. Capture the serial for ~60 s:
  `python tools/serial_capture.py COM<port> 60 e3-wifi-off.log`
  Within a few seconds the scan must stop listing `4WD4MCar_WiFi` and
  `http://192.168.245.1` must become unreachable. **Leave it OFF for >= 1 min and
  re-scan**: the SSID must NOT come back. Report whether the serial printed the
  web-layer stop, and whether the hotspot reappeared. `WIFICFG;WIFI;ON` brings
  everything back.
- **E4 — issue 9: the page with nobody attached.** Wi-Fi ON and the **app
  closed**. From the phone's browser open `http://192.168.245.1`: the page must
  render fully — no blank, no hang, no waiting for the app to connect first.
  Report **renders / blank / hangs**.
- While both toggles are ON and no client is attached, confirm the third
  condition of issue 7b too: hotspot visible, page browsable, `4WD4M Car`
  advertising — all at once (row **U-113-11**).

→ rows **U-113-9** (E3) and **U-113-10** (E4) in `TRACKS/DEVICE-TESTS.md`.

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

- **The BT heap diet (P4, issue 6) is IN this flash — its first step.** Bluetooth
  comes up classic-BT-only (`SerialBT.begin(..., true)`), releasing the unused BLE
  memory; this 1.3.11 body ALSO measures the rest (step 6b). Once those six
  `[HEAPW]` numbers are in, the U-112 floors are re-tuned against them.
- **P5 (issues 7 + 9) is IN this flash too.** `WIFICFG;WIFI;OFF` now actually hides
  everything: `stop()` drops the beacon unconditionally, `startOwnAP()` refuses
  while the transport is off, and `handle()`'s AP retry no longer resurrects the
  hotspot ~2 s after the switch. Step **6c** (E3/E4) is what proves that on the
  radio. Issue 9 needed no new fix — U-105 already removed the beacon park — so
  its check is a regression guard plus the E4 render.
- **DB Phase 7c migration** — owner runs
  `genumsolutions-website/supabase/migrations/20261013120000_phase7c_fresh_start_cleanup.sql`
  in the Supabase SQL editor (replace `owner_user_id` first).
- Every row above stays **DEVICE VERIFY PENDING** until it is ticked: a green
  pipeline is a necessary gate, never a sufficient one (F-61).
