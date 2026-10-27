# RUNSHEET — flash 1.3.18 + the U-121 E/E2 connect-path device pass

> One sheet for the owner's next bench session. Firmware **1.3.18** (UNFLASHED;
> **1.3.11 IS on the car**) + app **3.2.7 / versionCode 60** (unchanged this
> round — every app phase is JS-only and rides the OTA channel).
> Tick the rows in the row sheets as you go, NEWEST ROUND FIRST — car `TRACKS/DEVICE-TESTS.md`:
> (**U-124-⑫ — this flash's own fix — then** U-121-E2 · U-121-E · Q1-1/Q1-2 · U-120-B · U-117-1..3 · U-119-1 · U-116-10..15,
> U-115-1..3 · U-113-8..15) · app `mobile/TESTING.md` — **U-124-1..9** · U-123-1..4 · U-122-1..4 · U-121-A1..C2 + R1-A..D · U-120-C1..E3 ·
> U-117-1..4 · U-118-1..4 · U-119-1 · U-116-1..9, 13, 16..20).
> **F-61: every gate behind this build is green and none of it is device evidence.**

## 0 · What this one flash carries (1.3.11 → 1.3.18)

The car is on **1.3.11** already; 1.3.12 + 1.3.13 + 1.3.14 + 1.3.15 + 1.3.16 + 1.3.17 + 1.3.18 all ride this flash:

| Round | What you should notice |
|---|---|
| **U-124 ⑫ (1.3.18)** | **The turned-off page finally says so.** With the Webpage toggle OFF a browser got `Content-Length: 0` and the body past it — a **white screen**; the "The car's web page is turned off" message never rendered. One line (`setContentLength(len)` before the same `send`) — row **U-124-⑫**; the app's half (page/Wi-Fi state and IP visibility) is app row **U-124-5/6** |
| **U-121 E2 (1.3.17)** | **Connecting the Wi-Fi FROM THE APP never resets the car** — the owner's *"resets again and again whenever i try to connect the wifi"*. The switch machines' `WiFi.mode()`/`WiFi.begin()` steps now sit on the same 32000 B floor the retry defers on: below it the step is deferred with the radio untouched (`[RADIO] step deferred: low heap`), and after 4 s the switch is **abandoned honestly** (`[RADIO] switch abandoned: low heap`) rather than run into a starved heap — row **U-121-E2** |
| **U-121 E (1.3.16)** | **The BT-on + Wi-Fi-drop retry stops churning.** Below the floor the retry is deferred with the capped budget and backoff untouched (`[REJOIN] retry deferred: low heap`), resuming the pass Bluetooth goes away — row **U-121-E** |
| **Q1 (1.3.15)** | **The boot-hop is stopped.** Power-up stays on `4WD4MCar_WiFi` (`;AUTOJOIN=OFF`, serial `boot auto-join disabled - staying on own hotspot`); only the APP's smart-link joins a router. Opt back in with `WIFICFG;AUTOJOIN;ON` — rows Q1-1/Q1-2 in `DEVICE-TESTS.md` |
| **U-120 B (1.3.15)** | **The Webpage toggle actually works.** The app's `WIFICFG;WIFI;PAGE;…` used to be swallowed by the earlier `;WIFI` branch (always `ERROR;Syntax`, so the switch snapped back to ON) — fixed; row **U-120-B** in `DEVICE-TESTS.md` |
| **U-117 (1.3.14)** | **The webpage state is on every surface.** STATE carries `;PAGE=ON|OFF` (Bluetooth-only apps finally read it), `/status` keeps `page_on`, and a turned-OFF page stays OFF across a power cycle — the "toggle not working" report had one car half and one app half, and both are in this flash |
| **P4 / item F (1.3.13)** | **The car REFUSES a second link it cannot afford.** Both links normally come up as before; under the old freeze window the second raise is held back, the incumbent keeps working, and `;LINKCAP=0` / `"link_cap":0` say why (serial: `[LINKCAP] …`) |
| **P6 / item D (1.3.13)** | **The car keeps a bounded event log.** `LOG?` → `LOG;COUNT=…;CAP=12`; `LOG;GET;0` → newest-first `K,boot,ms,heap,reason,phase` events; `LOG;CLEAR` empties; `/status` `event_log_count` agrees. BOOT/FAULT/low-heap/link-refusal/link-edge events |

| Round | What you should notice |
|---|---|
| **U-115 (1.3.12)** | **The boot auto-join probes the air first** — the 2026-10-16 report *"the own hotspot is detected but will not connect"*. When the saved router is OFF, the car no longer holds its own hotspot on a **moving channel** while it retries the absent router: `armBootAutoJoin()` runs one non-blocking reachability scan — router present → rejoin, absent → stay AP-only (hotspot stable from the first second), inconclusive → plain rejoin. Serial names the probe result |

Everything below already rides **1.3.11** (in the car) and the app **OTA** — the
bench pass below verifies them, not re-flashes them:

| Already in | What you should notice |
|---|---|
| U-112 (1.3.11) | the reset-while-pairing / freeze-while-page-open round — the status broadcast defers below a heap floor (time-boxed 4 s), scans refuse with an honest BUSY, the page send aborts a non-draining reader. Serial grows `[HEAP] …` / `[SCAN] refused` |
| U-114 (1.3.11) | `/status` reports its OWN `wifi_enabled` / `bt_enabled` / `boot_autojoin` (the app ADOPTS those); `tele_params` present |
| P4/E1 + diet (1.3.11) | the `[HEAPW] <stage> free=… max=…` waterfall + Bluetooth up **classic-BT-only**; **P4 is CLOSED** — E1 came back **6/6** and `STATUS_HEAP_FLOOR_BYTES` was re-tuned 49152 → **20000** |
| P5 (1.3.11) | issues 7 + 9 — `WIFICFG;WIFI;OFF` really hides everything (Step 8b / E3) |
| app P1..P3, P6, P7 (OTA) | the 14-issue Remote/UX + dual-link + persistence work (rows in `mobile/TESTING.md`) |

⚠ A firmware-version change wipes the saved routers + transport toggles on the
first boot (`wipeRoutersOnFreshFirmware()`): both transports ON, auto-join OFF (Q1),
no router saved. Re-add your router in Step 5.

## 1 · Flash 1.3.18 (USB)

ESP32 Dev Module, **Partition Scheme = Huge APP (required)**, upload **115200**
(this CH340 fails at the default 921600):

    arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app .
    arduino-cli upload  --fqbn esp32:esp32:esp32:PartitionScheme=huge_app -p COM<port> --upload-property upload.speed=115200 .

> **If `arduino-cli` answers `Platform 'esp32:esp32' not found`:** on this box it
> defaults its data dir to `%TEMP%\opencode\tools\data` while the real install
> is in `%LOCALAPPDATA%\Arduino15` (esp32 core 3.3.12). Add
> `--config-file <file>` to both commands, with that file carrying
> `directories.data: C:\Users\caspe\AppData\Local\Arduino15`.

(1.3.11 is on the car; 1.3.17 is another version-change wipe of the saved
routers — expected. **1.3.9 and older are superseded.**)

Capture the first boot while you are at it (~15 s, resets the board):

    python tools/serial_capture.py COM<port> 15 boot-1.3.18.log

**Expect in that log:** `[BOOT] Restored mode=4WD4M`, the router-wipe line
(version change), `Own AP active: 4WD4MCar_WiFi`, and
`[V2] Web layer always-on: AP=4WD4MCar_WiFi ip=192.168.245.1 http=:80 ws=:81`
— **`AP=` must NOT be empty** (that was U-109's bug). No
`[RADIO] skip-softAP/sta-busy`, no `[RELAY]` lines. The boot probe runs here too
(Step 6).

## 2 · Get the OTA onto the phone

Force-close the app **twice** (close + reopen ×2) so expo-updates loads the
published bundle. The live manifest
(`https://bkylfnlybtsujwzropru.supabase.co/storage/v1/object/public/app-releases/release.json`)
must read `notes: "OTA · Short update (9bb4850f0df39ec54c81ed64614d63c2a09f510b)"`, `version 3.2.7`, (current live build, published 2026-10-24 05:45 UTC; earlier builds: `2476b2ef…` 2026-10-17, `e02db5c2b4c…` 2026-10-16 19:06 UTC)
`version_code 60` — verified live 2026-10-24 05:45 UTC. The app's About screen
still says **3.2.7** (JS-only OTA — no APK, no bump).

## 3 · Bluetooth: forget and re-pair

1. Android Settings → Bluetooth → **forget the old `4WD CAR` bond** (that name
   is dead — the firmware no longer advertises it).
2. In the app, scan → pair to **`4WD4M Car`**. **No PIN is asked**; if one is
   ever requested it is `1234`.
3. The bond completes and the app's Bluetooth link opens.

→ confirm the earlier car rows if still open: **U-111-1**, **U-111-3**,
**U-110-2**, **U-110-3**.

## 4 · Own hotspot: forget, re-join, read /status

Android caches a stale profile against an open AP — forget `4WD4MCar_WiFi`
once, re-join. Expect an `192.168.245.x` address, then open:

    http://192.168.245.1/status

Check `ap` = `4WD4MCar_WiFi` (**never empty**), `fw` = `1.3.17`, and the crash
fields (`crash_count`, `reset_reason`, the phase). Also check `tele_params` reads
`freq=1600, vyse=0` (U-114) and that `wifi_enabled`/`bt_enabled`/`boot_autojoin`
report your actual toggles, not `1600`/`0`. The page's mode dropdown and
router table must work.

→ confirm the earlier car rows if still open: **U-111-2**, **U-111-7**, **U-109-1..3**.

## 5 · Re-add a router (the wipe in Step 0 is expected)

App → Connectivity panel → Wi-Fi → add your router; the car switches and
reports the new IP on the OLED and in `/status`. Optional bench script for the
switch rows: `python tools/bench_u90_u92.py <car-ip>`.

## 6 · U-115 — the boot probe (this flash's fix) — do these FIRST

- **U-115-1 — ⭐ the own hotspot is joinable the second it appears (router ABSENT).**
  Switch the saved router OFF (or move the car out of range), then cold-boot. The
  phone's Wi-Fi scan sees `4WD4MCar_WiFi` and a **join completes within seconds** —
  no "detected but won't connect", no waiting for a Bluetooth-off moment. Serial
  shows the boot reachability probe and then **no rejoin churn** (the car stays
  AP-only because the router was absent).
- **U-115-2 — a present router still rejoins as before.** Power the router back on
  and cold-boot: the probe finds it and the car rejoins normally. Serial names the
  probe result and the rejoin.
- **U-115-3 — a probe that cannot run never strands the car.** An
  inconclusive/refused probe (low boot heap, or the radio never answers) degrades
  to the plain rejoin — the car still returns to its router. An explicit switch
  (`ROUTERS;USE` / `WIFICFG`) or `WIFICFG;WIFI;OFF` clears a stale armed probe (no
  second rejoin after a manual pick). Serial evidence per case.

## 7 · The U-113 checks (the 14-issue round) — car-side rows

The issue list is in `TRACKS/PLAN-2026-10-15-dual-link-and-remote-ux.md`. The
app-side rows (U-113-1..7, U-113-16) are in `mobile/TESTING.md`; the car-side rows
that the app cannot answer are here.

- **U-113-8 — ⭐ P4 acceptance (issue 6): both links live, the heap holds.** With BT
  and the own hotspot both up and the app connected, drive + open the page
  repeatedly; the telemetry must keep moving and nothing may reset.
- **U-113-12 — ⭐ issue 1: BOTH links up at the same time, in both orders.** Pair BT
  first then join Wi-Fi; then reverse — Wi-Fi first, then open the Bluetooth scan
  (it must no longer hide behind a live Wi-Fi link). Both chips show live at once.
- **U-113-13 — issue 1: each link disconnects independently.** With both up, drop
  each; only that method's chip goes down.
- **U-113-14 — issue 1: commands over each link, no double-apply.** Drive over BT,
  then over Wi-Fi; the car must not apply a line twice.
- **U-113-9 — ⭐ issue 7: Wi-Fi OFF hides everything** — see Step 8b (E3).
- **U-113-10 — issue 9: the page is up with NO app attached** — see Step 8b (E4).
- **U-113-11 — issue 7b: both ON ⇒ both offered.** With both toggles ON and no
  client attached: hotspot visible, page browsable, `4WD4M Car` advertising — all
  at once.
- **U-113-15 — ⭐ issue 8: a setting changed on ONE surface survives every restart.**
  Change a setting on the app OR the car (mode / speed / router / toggle), then
  kill the app, reboot the car, and change it from the other surface — the value
  must persist and stay in sync on both.

## 8 · Heap + Wi-Fi-switch captures (still owed from the bench)

### 8a · E1 — the `[HEAPW]` waterfall

P4 is CLOSED (E1 came back **6/6** on 2026-10-16 and the floor was re-tuned to
**20000**, in this flash). Re-reading the waterfall is optional **unless** the
reset/freeze returns. If you do run it, take ONE uninterrupted cold-boot window:

    python tools/serial_capture.py COM<port> 60 e1-heap-waterfall.log

While it runs: wait for boot → pair/connect Bluetooth → open the car's page in a
browser → keep the app connected over Wi-Fi too. **Expect exactly six lines, once
each:** `boot`, `wifi-ap`, `bt-stack`, `spp-connected`, `page-served`,
`both-links` (the state issue 6 is about). **Read them in the order the car
prints them, which is NOT `Config.h:143`'s order:** on 1.3.11 the own AP comes up
*before* the BT stack (`btComm.begin()` runs after the always-on web layer in
`setup()`), so the real sequence is `boot → wifi-ap → bt-stack → …`. Six is the
count that matters; the order is not gate-pinned. Report the `free=`/`max=` pairs
and the deltas `boot → bt-stack` and `wifi-ap → both-links` (`max` collapsing while
`free` holds = FRAGMENTATION, a different fix).

### 8b · E3 + E4 — the Wi-Fi switch, both ways (P5, issues 7 + 9)

- **E3 — issue 7: OFF hides everything.** With BT ON, send `WIFICFG;WIFI;OFF` (app
  Connectivity panel, or the page console) and keep the phone's Wi-Fi scan open.
  Capture for ~60 s: `python tools/serial_capture.py COM<port> 60 e3-wifi-off.log`
  Within a few seconds the scan must stop listing `4WD4MCar_WiFi` and
  `http://192.168.245.1` must become unreachable. **Leave it OFF ≥ 1 min and
  re-scan**: the SSID must NOT come back. `WIFICFG;WIFI;ON` brings everything back.
- **E4 — issue 9: the page with nobody attached.** Wi-Fi ON and the **app closed**.
  From the phone's browser open `http://192.168.245.1`: the page must render fully
  — no blank, no hang, no waiting for the app to connect first. Report **renders /
  blank / hangs**.

### 8c · E2 / E5 / E11 + the P3 audit log (still owed)

Owner-captured Phase-0 evidence per the plan's table. Capture as you go; the P3
control AUDIT is the app-side pass (rows U-113-5..7).

## 9 · The freeze — capture it, do not guess (STILL OPEN)

The freeze is **not** claimed fixed: every capture so far predates the own-hotspot
fix, so none can name this report's cause. If the car freezes:

1. Note what the OLED / app shows before you power-cycle, if you can.
2. Capture the serial log across the reset:
   `python tools/serial_capture.py COM<port> 30 freeze.log`
3. Report the **reset reason + phase + boot/crash counters**. U-106's RTC
   breadcrumb records phase + task + heap at the crash — that datum is the whole
   reason this round exists.

## 10 · Still open after the pass

- **Q1–Q3 — DECIDED and implemented 2026-10-22** (owner confirmed the defaults,
  no further word needed):
  - **Q1:** toggle removed (P3) + firmware boot-hop **STOPPED in 1.3.15**
    (`boot_autojoin` default OFF) — bench rows **Q1-1/Q1-2** in `DEVICE-TESTS.md`.
  - **Q2:** the merged row drops the telemetry fields FIRST at `width < 700` —
    pinned in the app (`remoteChrome.test.ts`), no bench row.
  - **Q3:** both-links fan-out, no primary link — pinned in the app
    (`bothLinksDrive.test.ts`); the device half stays **U-113-14**.
- **DB Phase 7c migration** — owner runs
  `genumsolutions-website/supabase/migrations/20261013120000_phase7c_fresh_start_cleanup.sql`
  in the Supabase SQL editor (replace `owner_user_id` first).
- Every row above stays **DEVICE VERIFY PENDING** until it is ticked: a green
  pipeline is a necessary gate, never a sufficient one (F-61).
