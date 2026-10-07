Version 1.2.3

# 4WD4M Car — Genum_4WD4M_CAR (v2 fleet reference design)

> `Config.h:10` is the single source of truth for the version. Per-release notes
> are appended at the bottom of this file; the full history is in
> `TRACKS/CHANGELOG.md`.

## 1. Project Name
**4WD4M Car (`Genum_4WD4M_CAR`) — 4WD modular car (Bluetooth SPP + Wi-Fi AP/STA setup-and-control + OLED + remote/app)**

The **newest fleet reference design (v2 architecture)**. Baseline: fresh copy of
`Genum_WIRELESS_CAR` 1.8.4 (`d0bf8c0`, 2026-09-27) with a renamed identity — **no donor history,
no donor tags** (fresh git history, owner ruling R-2). The donor repo remains untouched.

## 2. Identity (unique per car — fleet rule)

| Item | Value |
|---|---|
| Bluetooth SPP name | **`4WD CAR`** (PIN 1234) — `BluetoothComm.cpp` |
| Wi-Fi AP SSID | **`4WDCar_Wifi`** (open) — `Config.h` `WIFI_AP_NAME` |
| AP subnet | **192.168.245.1/24** (NOT 192.168.4.x — phone-side subnet collision is a proven past bug; the .244 subnet belongs to the WIRELESS car so two cars can coexist) |
| FW_NAME / FW_VERSION | `4WD4M Car` / `1.2.3` — `Config.h` |

## 3. Modes (fleet 9-token registry + CAPS)

Every LIVE token stays selectable and is announced via
`CAPS;4WD4M:LIVE;PATH:CS;OBS_US:CS;OBS_IR:CS;MAN:CS;AUTO:CS;2WD1M:CS` (device order, no trailing
`;`). Since U-79 (1.0.3) `ESP_SER`/`ESP_CLI` are DELETED from the registry: the correct CAPS
expectation is their **ABSENCE** — never `CS`, never `WIP` (see the rewritten T10 in
TRACKS/DEVICE-TESTS.md).

| Token | Display | Avail (v1.0.3) | Notes |
|---|---|---|---|
| 4WD4M | 4WD4M | **LIVE** | the single drive mode — over **Bluetooth AND Wi-Fi** |
| ESP_SER | Server (ESP_SER) | **REMOVED (U-79)** | was retired to CS in v2; deleted entirely in 1.0.3 — the Wi-Fi web layer is always-on and is the ONLY Wi-Fi setup |
| ESP_CLI | Client (ESP_CLI) | **REMOVED (U-79)** | was retired to CS in v2; deleted entirely in 1.0.3 |
| PATH / OBS_US / OBS_IR / MAN / AUTO / 2WD1M | — | CS | fleet roadmap, unchanged |

Legacy alias `BT`/`BLUETOOTH` → 4WD4M still accepted (fleet convention).

## 4. v2 architecture — what differs from the donor (planned work)

- **Wi-Fi is an always-on connection/setup channel, never a mode**: AP WebServer + WebSocket
  run from boot (non-blocking) regardless of active mode; STA joins stored routers with
  auto-rejoin. **(Shipped — Phase 1 landed; see TRACKS/TODO.md P1-2.)**
- **Drive over both transports in 4WD4M** — the donor's rule "Wi-Fi drive happens in ESP_SER"
  is retired here. Same line protocol (`\n`-terminated), same `handleCommand()` funnel, same
  `STATE`/`CAPS`/`NACK`/`NETW` responses, JSON-over-WS telemetry kept.
- **Setup over both transports**: `WIFICFG;<ssid>;<pass>` + `ROUTERS;ADD|USE|DEL|CLEAR`
  (max 6 routers, NVS `botcfg`) work over Bluetooth **and** over the AP WebSocket.
- Phase 1 of `guide/PLAN-2026-09-28-4WD4M-CAR-REFERENCE-DESIGN.md` **has landed** — the
  always-on web layer, dual-transport drive and dual-transport provisioning are live behavior.

## 5. Materials / Pins / Libraries
Same bill of materials and wiring as the donor hardware (the 4WD4M testbed IS the former
WIRELESS-car hardware refit): ESP32 DevKit, L298N (4 × brushed motors, skid-steer), 1.3" SH1106
OLED (I2C), Li-ion pack. Pins in `Config.h`. Libraries: WebSockets 2.7.2, U8g2, ESP32 core
3.3.11 (WebServer/BluetoothSerial/Preferences ship with the core).

## 6. Build Metadata

| Item | Value |
|---|---|
| Board | ESP32 Dev Module (`esp32:esp32:esp32`, core 3.3.11) |
| Partition | **`huge_app` REQUIRED** (same as donor) |
| Flash baud | 115200 |
| Baseline build | donor 1.8.4 measured 1,740,754 B (55%) / RAM 69,492 B (21%) — current figures reported by the CI compile gate |
| Build tool | `arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app .` |
| NVS namespace | `botcfg` (wifi_ssid/wifi_pass, mode/speed) |
| CI | `.github/workflows/arduino-ci.yml` — compile gate + deltas report, secrets-free |

## 7. Modules
Identical file set to the donor 1.8.4 (renamed): `Genum_4WD4M_CAR.ino` (entry, boot splash,
watchdog) · `Config.h` (identity/pins/constants) · `ModeManager` (registry, gating, failsafe,
STATE) · `BluetoothComm` (SPP transport) · `WebServerComm` (AP/STA + HTTP/WS + provisioning) ·
`L298NControl`/`MotorControl`/`TwoWD1M` (drive) · `Oled_1_3`/`OledDisplay` (UI, see
`storyboard.md`) · `ParamsStore` (NVS) · `secrets.h.example` (template — `secrets.h` is
gitignored, never committed; the sketch compiles without it).

## 8. Changelog

- **1.2.3 (2026-10-07, U-95 Phase 3)** — **the car's own page**. (3a) `MODECYCLE`/`MODEPREV`/
  `MODENEXT` tokens on the one shared `cycleMode()` walk (previously GPIO-only), wrap both ways.
  (3b) Scan rows carry `channel`/`own`/`saved`/`current` computed ONCE car-side (three app modules
  disagree about the own-network rule — the car's answer is the one everyone converges on); status
  JSON gains `station_ip`/`ap_ip`/`broadcast_ip`/`subnet` — the first `WiFi.softAPBroadcastIP()`
  call in the repository; both status buffers are `static char buf[3584]`, scan fragment 1856.
  (3c) The page reads it: a real `<table>` (Network/Signal/Ch/Flags, >=44px rows, its own
  scrollable region inside the stage — the page itself never scrolls), the own hotspot as a
  labelled tappable row (`ROUTERS;USE` — the way back to the car), a Scan button, `j.ap`+
  `broadcast_ip` surfaced, and the reconnect is now backoff 1s→15s capped, reset on open, silent
  while hidden, immediate on `visibilitychange`. Gate rules `mode-cycle`/`status-net-fields`/
  `page-net-table`/`page-reconnect`, mutants M43-M62 (20 new); full harness 62/62. Page 38,171
  bytes. Still UNFLASHED.
- **1.2.2 (2026-10-07, U-95 Phase 2)** — **router switching + scan**. (2a) The rejoin path walks
  the switch as a step machine, one radio call per pass: drop to `WIFI_AP` (the old
  `mode(WIFI_AP_STA)` was a guaranteed no-op — `WiFiGeneric.cpp:602` early-returns when the mode
  already matches, i.e. on every switch after the first — the U-90 "teardown"), **confirm** the
  drop with a `getMode()` poll (bounded re-issue, breadcrumb `rejoin/mode-AP-not-taken` on
  timeout), restore the hotspot (`startOwnAP()` in the STA-down window; `apRunning_` forced false
  first — `apParked_`/`apRunning_` tracked exactly, the beacon may blink but the state never
  lies), raise `WIFI_AP_STA` (boot-path proof: no beacon bounce), then `WiFi.begin()`.
  `WiFi.persistent(false)` runs as the first radio-touch of every boot — the Arduino default
  rewrote flash NVS on every `WiFi.begin()`/`softAP()` (~100k erase cycles against retry traffic).
  (2b) The retry's `disconnect()` and `begin()` no longer share a loop pass (the U-75 reboot
  shape): `retryBeginPending_` arms on one pass, a guarded consumer issues the begin on a later
  one; `rejoinNetwork()` cancels an in-flight begin and re-stamps the backoff so a stale retry
  can't fire mid-teardown. (2c) The scan is honest and ranked: `startScan()` answers `bool`
  (refuses an in-flight scan and a scan kicked while the STA is mid-join), the handler replies
  `ROUTERS;SCAN;BUSY` instead of claiming `STARTED` for a scan it dropped, the page shows BUSY in
  the refusal colour; results dedupe per BSSID (one row per RADIO — name-dedupe silently merged
  two routers sharing an SSID), sort strongest-first (hand-rolled, no `<algorithm>` on the Arduino
  TU), and the `SCAN_MAX_RESULTS` cap applies ONCE, after the sort (it used to apply before the
  loop and at insert — first-N-in-radio-order, not strongest-first). Gate rules
  `switch-teardown`/`wifi-persistent`/`retry-split`/`scan-honest`/`scan-quality` in
  `check-relay-contract.mjs`, mutants M23-M42 (20 new), all mutation-proven both directions.
  Still UNFLASHED.
- **1.2.1 (2026-10-06, U-95 Phase 1b/1c)** — the two remaining WebSocket watchdog races:
  `ws.loop()`'s partial-frame read (up to 5000 ms, untunable — the library reads its timeout in
  its own translation unit) and `broadcastStatus()`'s write to a stalled client (5-8 s measured,
  U-77) each get a watchdog feed IMMEDIATELY before they run; the broadcast is now
  client-guarded (no client = no 1536-byte JSON build, no socket walk — the 1 s ticker did both
  to nobody); `LOOP_TASK_WDT_TIMEOUT_MS` moves 5000 → 10000 so the library's worst operation sits
  strictly BELOW the watchdog instead of tying it (a tie is the reset). Cost: a genuine hang
  resets in 10 s instead of 5 s. Gate rules `ws-loop-feed`/`ws-guard`/`ws-broadcast-feed`/
  `ws-budget`, mutation-proven.
- **1.0.5 (2026-10-04, U-92)** — **the own-hotspot fix**: field-bounded Reserved guards on ADD/USE/WIFICFG (the old checks compared the whole unterminated command field, so a password after the name bypassed them and 4WDCar_Wifi entered the registry / the active pair — the "double entry"); poisoned rows purged at load (self-heals existing cars); **"back to my own hotspot" actually leaves the router** (the T-66 return answered USED and armed no radio work); the webpage's router placeholder now shows the car's CURRENT network and parses `WIFICFG;STORED;`.
- **1.0.4 (2026-10-04, U-90)** — **the stuck-router fix**: the rejoin MODE step now restarts the WiFi driver on EVERY explicit switch, so `WiFi.begin()` never lands on a still-associated STA — the stale association used to satisfy the `WL_CONNECTED` resolve check, making a switch to ANY router "succeed" on the OLD network (stale DHCP lease, car unreachable on its own network, no retry ever triggered). Webpage: **coming-soon modes are selectable** (owner override; unknown tokens still NACK; the (coming soon) label stays as information) and the router dropdown no longer preselects an unconfirmed network.
- **1.0.3 (2026-10-03, U-79)** — **webserver mode REMOVED** (ESP_SER/ESP_CLI gone from the registry,
  page and NACK path — the Wi-Fi web layer is the only Wi-Fi setup, no second one to fall into);
  **transport power policy**: idle = Bluetooth + Wi-Fi both broadcast, a client on one turns the
  other off, a drop brings both back (3 s grace); boot/resume prefer the LAST USED transport
  (Wi-Fi → last-good router, hotspot as the fallback of last resort; Bluetooth → hotspot + BT).
  Page: mode dropdown gated by the car's CAPS (a tap can never park the car in a coming-soon mode),
  speed slider no longer fights the 1 s status sync while dragging, "Add router…" (WIFICFG stores
  + switches) and an honest "Switching…" note on the expected link drop.
- **1.0.2 (2026-10-03, U-77)** — task-watchdog reboot class eliminated: WS stalled-client
  heartbeat (server pings + drops non-responders before their TCP buffer can wedge a
  broadcast), total-bounded root-page write + per-write HTTP send cap, mDNS begin()
  success/failure condition un-inverted (mDNS could never advertise), retry-branch radio
  breadcrumbs, last-good router no longer wiped by the registry load (boot auto-join works
  again), stopMdns() wired into the shutdown path.
- **1.0.1 (R1 round 1)** — OLED link-context line (BT paired vs Wi-Fi mode + RSSI);
  app-side R1 flicker/timeout fixes.
- **1.0.0 (2026-09-28)** — fresh baseline from `Genum_WIRELESS_CAR` 1.8.4 (`d0bf8c0`):
  identity rename (BT `4WD CAR` · AP `4WDCar_Wifi` · AP 192.168.245.1/24 · FW_NAME `4WD4M Car`),
  fresh git history. v2 re-architecture (always-on Wi-Fi channel, Wi-Fi drive in 4WD4M, ESP_SER/
  ESP_CLI → CS) lands in Phase 1 of the reference plan.
