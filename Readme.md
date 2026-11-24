Version 1.3.20

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
| Bluetooth SPP name | **`4WD4M Car`** (PIN 1234) — `Config.h` `BT_SPP_NAME` (U-110: renamed from `4WD CAR` so the phone list, the OLED and the app match; re-pair once after the change) |
| Wi-Fi AP SSID | **`4WD4MCar_WiFi`** (open) — `Config.h` `WIFI_AP_NAME` (1.3.5: the ONE own-hotspot name fleet-wide; the donor `WirelessCar_Wifi` and the older `4WDCar_Wifi` are fully removed) |
| AP subnet | **192.168.245.1/24** (NOT 192.168.4.x — phone-side subnet collision is a proven past bug; the .244 subnet belongs to the WIRELESS car so two cars can coexist) |
| FW_NAME / FW_VERSION | `4WD4M Car` / `1.3.20` — `Config.h` |

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
4.0.0 (the CI pin; WebServer/BluetoothSerial/Preferences ship with the core, and
`BluetoothComm::begin()` version-guards the SPP bring-up so the sketch still
builds on 3.3.x).

## 6. Build Metadata

| Item | Value |
|---|---|
| Board | ESP32 Dev Module (`esp32:esp32:esp32`, core 4.0.0 — CI pin) |
| Partition | **`huge_app` REQUIRED** (same as donor) |
| Flash baud | 115200 |
| Baseline build | donor 1.8.4 measured 1,740,754 B (55%) / RAM 69,492 B (21%) — current figures reported by the CI compile gate |
| Build tool | `arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app .` |
| NVS namespace | `botcfg` (wifi_ssid/wifi_pass, rnet0-5s/p, last_ok_ssid/pass, last_transport, fw_mark, mode/speed) |
| CI | `.github/workflows/arduino-ci.yml` — compile gate + deltas report, secrets-free |

## 7. Modules
Identical file set to the donor 1.8.4 (renamed): `Genum_4WD4M_CAR.ino` (entry, boot splash,
watchdog) · `Config.h` (identity/pins/constants) · `ModeManager` (registry, gating, failsafe,
STATE) · `BluetoothComm` (SPP transport) · `WebServerComm` (AP/STA + HTTP/WS + provisioning) ·
`L298NControl`/`MotorControl`/`TwoWD1M` (drive) · `Oled_1_3`/`OledDisplay` (UI, see
`storyboard.md`) · `ParamsStore` (NVS) · `secrets.h.example` (template — `secrets.h` is
gitignored, never committed; the sketch compiles without it).

## 8. Changelog

- **Unreleased (based on 1.3.19):** boot now honors the persisted Wi-Fi and
  Bluetooth switches before raising either radio. Bluetooth-only startup no
  longer briefly starts the Wi-Fi/AP/web stack or arms router auto-join, and a
  low-heap boot cannot bypass second-link admission. The loop also skips web
  handling while Wi-Fi is deliberately down; Bluetooth command/state exchange
  remains active, including the `;PAGE=` state used to synchronize the webpage
  toggle. `FW_VERSION` is intentionally unchanged: changing it triggers the
  firmware's fresh-install configuration wipe, which would erase saved router
  and transport settings. Bench coverage is pending in `TRACKS/DEVICE-TESTS.md`.
- **1.3.15 (2026-10-22, Q1–Q3 close-out)** — **the firmware boot-hop is
  STOPPED.** `botcfg/boot_autojoin` defaults OFF (and FACTORY;RESET follows), so
  a power-up stays on the own hotspot and only the app's smart-link joins a
  router; the `WIFICFG;AUTOJOIN` verb, STATE `;AUTOJOIN=` and `/status
  boot_autojoin` stay honest (console opt-in keeps the probe-first rejoin).
  `check-webpage-js.mjs` pins the new default. Q2/Q3 (telemetry-first drop ·
  both-links fan-out) are app-side pins, no firmware byte. **UNFLASHED (F-61)**
  — owner flash target; device rows Q1-1/Q1-2.
- **1.3.14 (2026-10-20, U-117/U-118)** — the page-server state rides the STATE
  line (`;PAGE=ON|OFF` + public `pageServerIsOn()`), contract rule `page-state`.
  See `TRACKS/CHANGELOG.md`. **UNFLASHED (F-61)**.
- **1.3.13 (2026-10-17, P4 / item F)** — **the car REFUSES a second link it
  cannot afford**, so multi-connect can no longer starve the run-time (the
  2026-10-17 owner report: connecting BOTH links at once can freeze/reset the
  car; field `free_heap` 13-18 kB at those resets). Telemetry and scans already
  refused below a heap floor — a LINK never did, so the second transport came up
  into a starved heap. `WebServerComm::admitSecondLink()` gates **only the
  SECOND** link (the FIRST is never gated — a car with no way in is worse than a
  car with one), leaves the incumbent link untouched, and **latches** the verdict
  so `;LINKCAP=0|1` (STATE line, Bluetooth-only apps) and `\"link_cap\":0|1`
  (`/status`, the web transport) report it honestly; `linkCapCode()` folds the
  transport toggles (one transport off ⇒ 1, nothing to deny) and the latch clears
  the moment both links are up. `LINK_ADMISSION_FLOOR_BYTES` **32000** (above the
  20000 status floor, below the 65536 scan floor; bench-tuned pending P0). The
  `.ino` snapshots each link's state before the optimistic `webUp = true` mark,
  and a refused Wi-Fi raise reverts it so the loop retries once the heap recovers.
  New `link-admission` rule + mutants M110-M116; mutate-gates **110/110**.
  Failsafe **F-87** (an unreservable resource must be refused, not attempted).
  The app half is P5 (F-88: an ABSENT `link_cap` means CAPABLE, never denied).
  UNFLASHED (F-61) — owner flash target; 1.3.11 remains the last build on the car.
- **1.3.12 (2026-10-16, U-115 + the P6/P7 gate rules)** — **the boot auto-join
  probes the air first**, so an absent router no longer holds the own hotspot on a
  **moving channel** (the owner's 2026-10-16 field report: the own hotspot was
  *detected but would not connect*). `armBootAutoJoin()` now runs ONE non-blocking
  reachability scan before arming the rejoin: router present → rejoin, absent →
  stay AP-only (own hotspot stable from the first second), inconclusive → fall back
  to the plain rejoin. New `boot-scan-first` rule + mutants M96-M100. Two further
  rules pin the firmware's half of app issues 1 and 8 with **no firmware byte
  changed**: `dual-link-kept` (the both-enabled branch raises a downed transport and
  contains no `stop()`) + M101-M103, and `settings-round-trip` (each
  `WIFICFG;WIFI|BT|AUTOJOIN` persists to NVS before ack, answers with the state it
  holds, rides both the STATE line and the `/status` JSON, and keeps the
  last-transport-off clamp) + M104-M109. mutate-gates **103/103**. UNFLASHED (F-61)
  — owner flash target; 1.3.11 remains the last build on the car.
- **1.3.11 (2026-10-15, U-112 + U-114 + T66 + P4/E1 + P5)** — **heap floor for
  status/scan + the page
  stall-abort** (the post-1.3.9 report: reset while pairing BT + the own
  hotspot; freeze using the page with the app connected; field heap 13-18 kB).
  `broadcastStatus()` defers below 49152 B time-boxed to 4 s;
  `startScan()` refuses below 65536 B with an honest BUSY; the root-page send
  aborts a non-draining reader after 2 zero-writes so loop() keeps running the
  app link. New `heap-guard` gate rule + mutants M70-M75; mutation also
  re-pinned `scan-honest`'s in-flight refusal BY TEXT (the new floor's extra
  `return false` had propped its count up). mutate-gates **69/69**.
  Also **U-114**: a `CONFIG_TELEMETRY_PARAMETERS` surface added to this same
  unreleased body shipped unquoted (the sketch did not compile — syntax gate
  **1/9** at HEAD) and with its `/status` `%s` slots fed by the wrong snprintf
  arguments, so `wifi_enabled`/`bt_enabled`/`boot_autojoin` — which the app
  ADOPTS — reported neighbours' values. The surface is now one string literal
  read through a bounded, NULL-safe `teleSurfaceField()`; new `tele-params`
  rule + mutants M76-M80; syntax 9/9, mutate-gates **74/74**.
  Also **P4 (issue 6, the BT heap)**: `heapWaterfall(stage)` prints
  one `[HEAPW] <stage> free=… max=…` line per radio lifecycle stage
  (boot → bt-stack → spp-connected → wifi-ap → page-served → both-links; `max` is
  `getMaxAllocHeap()`, so fragmentation shows without moving the free total), and the
  diet's first step brings Bluetooth up **classic-BT-only**
  (`SerialBT.begin(name, false, true)`), so the BLE controller/host memory this SPP-only
  car never uses is released — the waterfall then measures what remains. New
  `heap-waterfall` + `bt-heap` rules; mutants M81-M88, M89/M90; syntax 9/9,
  mutate-gates **84/84**.
  Also **P5 (issues 7 + 9)**: `WIFICFG;WIFI;OFF` now actually hides
  everything — `stop()` drops the beacon unconditionally, `startOwnAP()` refuses
  while the Wi-Fi transport is off, and `handle()`'s AP retry is gated on
  `wifiEnabled_`. It was worse than a skipped teardown: the retry tested only
  `!apRunning_`, which `stop()` had just cleared, so the hotspot came back ~2 s
  after the switch and was never taken down again. Issue 9 needed no new fix
  (U-105 already removed the beacon park) and is now pinned by a gate. New
  `wifi-off-hides-ap` + `page-always-on` rules; mutants M91-M95; syntax 9/9,
  mutate-gates **89/89**.

- **1.3.9 (2026-10-14, U-111b)** — **the Internet / cloud-relay method is removed.**
  `RelayComm.cpp`/`.h` are deleted and every seam is unwired (sketch, `ModeManager`,
  OLED, `WebServerComm`, `Config.h`): no `[RELAY]` log lines, no `RELAYCFG`, and the
  OLED link rotation is back to the two live transports (Bluetooth / WiFi). CI job
  `relay-contract` → `firmware-contract`, with the non-relay rules the rename had
  dropped ported back in (mutate-gates 63/63). Full notes: `TRACKS/CHANGELOG.md`.
- **1.3.8 (2026-10-14, U-111)** — **Bluetooth pairing actually completes, the own-hotspot
  bring-up stops retrying every loop pass, and the 11 dBm AP cap becomes a boot-window
  cap.** (a) `BluetoothSerial` defaults to `_enableSSP = false` / a zero-length PIN and the
  firmware set neither, so pairing ran LEGACY and the car answered Android's PIN request with
  an EMPTY PIN — the car listed fine and the bond failed every time (the owner's "appears but
  doesn't pair"). `BluetoothComm::begin()` now calls `enableSSP(false, false)` (SSP "Just
  Works", no PIN) + `setPin("1234", 4)` (legacy fallback) before `SerialBT.begin()`. (b)
  `updateApPowerPolicy()` called `startOwnAP()` ungated every pass when the STA was down; a
  repeatedly failing `softAP()` became a per-pass radio churn that starved `loop()` ("the car
  side freezes totally"). The direct call is removed — `handle()`'s `AP_RETRY_INTERVAL_MS` +
  `staBusy()` retry is the only bring-up retry. (c) The U-96 `WIFI_POWER_11dBm` cap now lasts
  only `WIFI_BOOT_SETTLE_MS` (15 s); `handle()` then restores `WIFI_POWER_17dBm` once. 1.3.7
  (U-110: OLED two-row transport frames, BT name `4WD CAR` → `4WD4M Car`, honest `btUp`) rides
  in the same flash; full notes in `TRACKS/CHANGELOG.md`.

- **1.3.6 (2026-10-13, U-106)** — **crash-TIME evidence that survives a PANIC, plus phase
  coverage on the reported paths.** The field crash table (`device_crash_reports`, board
  `1FB608`) had ~754 unexpected boots, all `PANIC`, all `last_crash_phase = ''` — the
  flash-backed phase record is written on transitions only and `crashLogPhase()` ran from
  just three places, none of them the BT/Wi-Fi/WS/motor paths the owner reported. This adds
  an `RTC_NOINIT_ATTR` breadcrumb (no flash wear) written on every phase change and a
  per-loop heartbeat: it names the operation, the FreeRTOS **task**, the free heap **at that
  instant** and the low-water heap, and it **survives a PANIC/WDT reset**. `crashLogBegin()`
  now prefers that fresh phase over the rate-limited NVS copy (the fix for `lastphase=''`),
  persists the extras (`botcfg/lheapc`,`lminh`,`lmsec`,`ltask`) and prints
  `[CRASH] rtc: …` on serial. `/status` gains `last_crash_heap_at_crash`,
  `last_crash_heap_min`, `last_crash_task`, `last_crash_millis`, `live_min_heap`. Full
  detail: `TRACKS/CHANGELOG.md`; plan: `TRACKS/PLAN-2026-10-13-cleanup-rebuild.md`.

- **1.3.5 (2026-10-13, U-105)** — **the transport toggles ride, the beacon is never parked, the
  own-hotspot name is fleet-single.** (a) The whole parallel-links + power-save pair
  (`WIFICFG;PARALLEL` / `WIFICFG;APSAVE`, NVS `allow_parallel` / `ap_psave`, members `allowParallel_`
  / `apPowerSave_`) is REMOVED per owner ruling and replaced by **`WIFICFG;WIFI;ON|OFF`** (NVS
  `botcfg/t_wifi`), **`WIFICFG;BT;ON|OFF`** (NVS `botcfg/t_bt`), **`WIFICFG;AUTOJOIN;ON|OFF`** (NVS
  `botcfg/boot_autojoin`). Wi-Fi + Bluetooth default ON; boot auto-join defaults OFF since Q1 (1.3.15 — the stopped boot-hop); both transport toggles are clamped so they can never
  both be OFF (handler, `loadStoredWifi()`, and the U-79 loop each enforce it). Both transports stay
  up SIMULTANEOUSLY when enabled — the "one client powers the other down" teardown
  (`saveLastTransport`/`LastTransport`/grace timer) is gone. (b) `updateApPowerPolicy()` was rewritten
  to a **guarantee-beacon** policy: no parking branch, no `softAPdisconnect`, no `WiFi.mode()` on the
  policy path; it bails when `wifiEnabled_` is false and otherwise only restores the beacon when the
  STA is down. `apParked_`, `AP_PARK_GRACE_MS`, `AP_PARK_HTTP_MS`, and the `lastHttpAtMs_` stamp — all
  parking-era residue — are deleted. (c) **`WIFI_AP_NAME` is now `4WD4MCar_WiFi`** — the ONE own-
  hotspot name; the donor `WirelessCar_Wifi` and the earlier `4WDCar_Wifi` are purged from the app
  registry and all firmware comments. `Config.h` gains `WIFI_AP_IP[] = "192.168.245.1"`; the OLED
  fallback literals in `Oled_1_3.cpp` now use `WIFI_AP_NAME`/`WIFI_AP_IP` (was `"GenumCar"` and the
  wrong-subnet `"192.168.4.1"`). (d) **Deleted networks stay deleted**: the registry-row + `wasActive`
  delete in `ROUTERS;DEL` now matches case-insensitively (`strcasecmp`), so a different-case delete of
  a saved router actually removes it instead of answering "Not saved" while the router survived the
  power cycle. `/status` JSON + STATE keys rename to `wifi_enabled`/`bt_enabled`/`boot_autojoin`; the
  boot auto-join is gated on `bootAutojoin_`. App 3.2.7 rides along (same-version JS-only OTA). Gates:
  syntax 10/10 + the rewritten `check-webpage-js.mjs` transport-toggle contract + `check-firmware-contract.mjs`.
  UNFLASHED (car-side OTA pending owner).
- **1.3.4 (2026-10-13, U-104)** — **watchdog feed before the `sendState()` WS fan-out.**
  `WebServerComm::write()` — the delivery path `ModeManager::sendStateOn()` uses for STATE / CAPS /
  NETW / SCAN (up to four WS broadcasts per transport per 1.5 s tick) — was the ONLY WS-write path
  with no feed of its own (`broadcastStatus()` and `ws.loop()` have theirs). A WS client that stops
  draining TCP can hold a broadcast for up to 5 s (the U-77 measurement); the write now starts with
  a fresh `LOOP_TASK_WDT_TIMEOUT_MS` window. `FW_VERSION` 1.3.3 → 1.3.4. Power review this round:
  BROWNOUT dominates the crash DB and the owner's separate-2S-pack supply (ESP+OLED through a buck)
  makes the buck/regulator/wiring rail the suspect — documented, no blind firmware change. Gate rule
  `ws-broadcast-feed`. UNFLASHED (F-61); 1.3.1→1.3.3 notes: `TRACKS/CHANGELOG.md`.
- **1.3.0 (2026-10-11, U-98)** — **a flash starts clean, and a boot returns to the last-switched
  router.** (a) `botcfg/fw_mark` stores `FW_VERSION`; on the first boot after a (re-)install
  `wipeRoutersOnFreshFirmware()` erases every saved router + the stored pair + the last-good pair
  and writes the mark — Arduino flashing does not erase NVS, so the owner's *"clean and fresh
  always"* now actually happens (same-firmware power cycles keep the routers, U-84). (b) Boot
  auto-join now targets `storedSsid_` (the last-SWITCHED router) instead of `last_ok_ssid`, which
  a `ROUTERS;DEL` of the active router left behind — that was *"i deleted the saved router but the
  car restarted with the old router"*. The last-good pair is informational and is erased with the
  stored pair at all four clear sites. (c) `BOOT_STA_TIMEOUT_MS`/`BOOT_STA_MAX_RETRIES` removed:
  the boot join uses the full explicit-switch budget, safe because the own hotspot beacon stays up
  the whole retry. (d) **The OLED extra row is "pronto"**: `updateExtraInfo()` and
  `broadcastStatus()` no longer share one 1 s tick — the extra row polls every 500 ms while the WS
  broadcast stays at 1 Hz (`EXTRA_INTERVAL`/`STATUS_INTERVAL`; gate rule `extra-status-decoupled`).
  Still UNFLASHED.
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
  `check-firmware-contract.mjs`, mutants M23-M42 (20 new), all mutation-proven both directions.
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
