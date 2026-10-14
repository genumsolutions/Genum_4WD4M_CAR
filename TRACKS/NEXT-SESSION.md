# NEXT SESSION — Genum_4WD4M_CAR (updated 2026-10-13, U-105: transport toggles ride, beacon never parked, `4WD4MCar_WiFi` — 1.3.5 UNFLASHED, owner flash target; app rides along as 3.2.7 JS-only OTA)

## U-108 2026-10-13 — fresh start + timestamps (1.3.6, UNFLASHED — owner flash target)

Owner round 4, Phase 7 (car half). Two new system commands ride the SAME hook as
WIFICFG/ROUTERS/RELAYCFG (`Genum_4WD4M_CAR.ino` → `WebServerComm::handleFactoryCommand`):

- **`FACTORY;RESET`** — `Preferences::clear()` on the whole `botcfg` namespace (routers,
  credentials, last-good, toggles, mode/speed, relay token, `fw_mark`, crash counters), restores
  RAM defaults (Wi-Fi ON, BT ON, boot auto-join ON, no stored pair, empty router list), calls
  `crashLogResetCounters()` (boot→1, crash→0, record dropped) and replies `FACTORY;CLEARED`.
  No mid-command reboot — the reply must reach the app first; the next power cycle is the virgin boot.
- **`TIME;<unixEpoch>`** — the app is the car's clock (no RTC). Adopted into RAM; `TIME?` replies
  `TIME;<now>;<configEpoch>`.
- **`botcfg/config_epoch`** stamped on every configured write (WIFICFG store/reset + the three
  toggles + ROUTERS ADD/USE/DEL/CLEAR). Boot-state (`mode`/`spd`) intentionally unstamped.

**App half DONE (U-108 Phase 7b):** `resetCarData()` in `useControlHub.ts` sends `FACTORY;RESET`
over every live transport, wipes ALL car-related AsyncStorage keys, resets hub state. Remote
screen gets a red "Reset car data" button with destructive confirmation (`testID:
reset-car-data`). Gates: `tsc` 0 · vitest **688/688** · prettier clean. JS-only OTA (3.2.7/60).

**DB migration PENDING (U-108 Phase 7c):** additive SQL
`genumsolutions-website/supabase/migrations/20261013120000_phase7c_fresh_start_cleanup.sql`
(delete this car `1FB608` + owner's car-related rows + retention functions). Owner runs in
Supabase SQL Editor after replacing the `owner_user_id` placeholder.

Files: `WebServerComm.h/.cpp` (command + epoch + `stampConfigWrite`), `CrashLog.h/.cpp`
(`crashLogResetCounters`), `Genum_4WD4M_CAR.ino` (hook), `TRACKS/RECORDS-INVENTORY.md` §5.

Gates: node `check-webpage-js` + `check-relay-contract`, `check-firmware-syntax.ps1` (10/10),
`arduino-cli compile` PASS (flash 1823610 B / 57%, RAM 85404 B / 26%). Device verify pending
(Phase 9 adds U-108 rows).

## U-108 2026-10-13 — donor dead-code purge (Phase 8, 1.3.6, UNFLASHED — owner flash target)

**DONE — effectively no-op per owner request.** The SSID case rename (`4WD4MCar_WiFi`) was
completed in Phase 5. The CS modes (PATH, OBS_US, OBS_IR, MANUAL, AUTO) are RETAINED per owner
request to show "Coming Soon" on the OLED and remain toggleable via app/remote. No other unused
donor code found — all remaining paths are live. No app/DB/donor-repo changes.

Gates: all green (webpage-js, relay-contract, firmware-syntax 10/10, compile PASS).

## U-108 2026-10-13 — docs / DEVICE-TESTS rows / close-out (Phase 9)

**DONE.** Added U-108 device rows to `TRACKS/DEVICE-TESTS.md` (U-108-1..6: FACTORY;RESET,
TIME;epoch, config_epoch, Reset car data button, auto-join labels, on-connect epoch sync).
Updated `TRACKS/DEVICE-TESTS.md` U-108 section. App `TESTING.md` U-108-1..4 added.
`RESUME-HERE.md` and both PLAN files updated with close-out status.
All repos at Phase 9 complete.

## U-107 2026-10-13 — OLED last-two-lines rule (1.3.6, UNFLASHED — owner flash target)

Owner request: the OLED's last two rows must alternate BT ⇄ Wi-Fi info when both transports
are on, show a single transport always, and the Wi-Fi line must alternate the network name
⇄ the IP (~1 s). The old code rotated only while **idle** and keyed off the link state.

- `OledDisplay::setTransportToggles(wifi, bt)` (+ `Oled_1_3` override) and
  `ModeManager::pushTransportToggles()` carry the live **U-105 toggles** to the panel
  (change-gated; a toggle repaints at once). `ModeManager::run()` calls it every loop.
- `Oled_1_3::redrawNow()`: both ON → alternate the Bluetooth frame ⇄ the Wi-Fi frame
  (`ROTATE_INTERVAL_MS` 1500 → **1000**); one ON → that frame always; the Wi-Fi frame's
  last line alternates **SSID ⇄ IP** (`WIFI_INFO_INTERVAL_MS` 1000); a **relay (Internet)**
  link keeps its own fixed frame. `wasIdle` removed.
- `Config.h` gains `BT_SPP_NAME` (`"4WD CAR"`) — the ONE source of the advertised BT name;
  `BluetoothComm::begin()` passes it to `SerialBT.begin()` and the OLED's Bluetooth frame
  shows it (was a second hard-coded literal).

Gates: node `check-webpage-js` + `check-relay-contract`, `check-firmware-syntax.ps1`,
`arduino-cli compile`. **Device verify pending** (`TRACKS/DEVICE-TESTS.md` U-107-1..5).

## U-106 2026-10-13 — crash-TIME evidence survives a PANIC + phase coverage (`…`, 1.3.6, UNFLASHED — owner flash target)

The field crash table (board `1FB608`) had ~754 unexpected boots, all `PANIC`, all
`last_crash_phase = ''`; a live **COM7** capture on 2026-10-13 printed the same
(`[CRASH] boot=980 crash=754 reason=PANIC heap=176400 lastphase=''`). Root cause of the
EMPTY phase: the flash-backed phase record is written on TRANSITIONS only (rate-limited up
to 30 s) and `crashLogPhase()` ran from just **three** places — none of them the
BT/Wi-Fi/WS/motor paths the owner reported.

1. **RTC breadcrumb** (`CrashLog.h/.cpp`): an `RTC_NOINIT_ATTR CrashRtc` written on every
   phase change + a per-loop heartbeat. It names the operation, the FreeRTOS **task**, the
   free heap **at that instant** and the low-water heap, and it **survives a
   PANIC/WDT/software reset** (magic-guarded against a cold boot). No flash wear.
2. **`crashLogBegin()`** captures it FIRST and prefers its fresh phase over the rate-limited
   NVS copy — the `lastphase=''` fix — persists the extras (`botcfg/lheapc`,`lminh`,`lmsec`,
   `ltask`) and prints `[CRASH] rtc: marks=… phase='…' task='…' heap=… minHeap=… atMs=…`.
3. **Phase coverage** on the reported paths: main loop (`switch`/`cmd`/`web`/`relay`),
   `WebServerComm` (`handle`/`wifi-rejoin`/`wifi-scan`/`ws-evt`/`wifi-ap`/`wifi-connect`/
   `wifi-stop`), Bluetooth (`bt-begin`/`bt-read`/`bt-write`), motor (`motor`), OLED (`oled`),
   relay (`relay`/`relay-task`), `state`.
4. **`/status`** gains `last_crash_heap_at_crash`, `last_crash_heap_min`, `last_crash_task`,
   `last_crash_millis`, `live_min_heap` (additive).
5. `FW_VERSION` 1.3.5 → **1.3.6**. Gates green: node `check-webpage-js` + `check-relay-contract`,
   `check-firmware-syntax.ps1` 10/10, `arduino-cli compile` PASS. **Device verify pending**
   (`TRACKS/DEVICE-TESTS.md` U-106-1..5).

## PLAN 2026-10-13 (Phase 0) — field-fix + cleanup round, notes-first, phased

Owner's new round: fix the reported fields (telemetry/status, BT-connect restart not surfaced in the
app, OLED last-two-lines rules, Wi-Fi connect freeze, SSID, duplicate auto-join settings); the car
must **self-capture and ship crash/monitor reports**; clean out old car + user-profile records for a
**fresh start with proper timestamps**; remove **unused donor ("wireless car") leftovers from the
4WD4M car + the SSID name change only** (nothing else); deliver in **phases, pushing after each fix**.

Full plan: `TRACKS/PLAN-2026-10-13-cleanup-rebuild.md` (master) + app mirror
`genumsolutions-app\TRACKS\PLAN-2026-10-13-cleanup-rebuild.md`; records reference
`TRACKS/RECORDS-INVENTORY.md`.

Owner decisions: SSID exact **`4WD4MCar_WiFi`**; **keep both** auto-join toggles (clearer labels);
firmware-only donor purge; disconnect buttons only under Back; the app sends the real epoch on
connect; DB fresh start = **this car (`1FB608`) + the owner's account**.

Live bench capture 2026-10-13 (COM7, `tools/serial_capture.py`): `[CRASH] boot=980 crash=754
reason=PANIC heap=176400 lastphase=''` + `Own AP active: 4WD4MCar_WiFi` + `192.168.245.1` — **the
bench car is on a 1.3.x build, NOT 1.0.5 as the prior ledger said**, and still PANICs with an empty
phase → Phase 1 target. Toolchain confirmed locally: bundled `arduino-cli` + ESP32 core 3.3.12 +
`WebSockets`/`U8g2`; owner flashes. **No code changed yet (Phase 0 = notes).**

## U-105 2026-10-13 — transport toggles ride, beacon never parked, one own-hotspot name (`4645562`, PUSHED, 1.3.5, UNFLASHED — owner flash target)

Per the owner's device-report failures + redesign mandate (Q&A 2026-10-12/13): remove the
parallel + power-save settings entirely, replace with Wi-Fi/BT/auto-join toggles, single
own-hotspot name, beacon never parked, and fix "deleted network survives power cycle".
Full round log: `RESUME-HERE.md` (R3 section). The app half is the same-version 3.2.7 JS-only
OTA (F-65 stayed 3.2.7; this round is Phase B of that rebuild).

1. **Toggles** — `WIFICFG;WIFI;ON|OFF` (NVS `botcfg/t_wifi`), `WIFICFG;BT;ON|OFF` (NVS
   `botcfg/t_bt`), `WIFICFG;AUTOJOIN;ON|OFF` (NVS `botcfg/boot_autojoin`), all default ON,
   clamped so they can never both be OFF (handler + `loadStoredWifi()` + U-79 loop). Both
   transports stay up at once when enabled — the one-active-link teardown
   (`saveLastTransport`/`loadLastTransport`/`LastTransport`/grace) is deleted. `/status`
   JSON + STATE keys: `wifi_enabled`/`bt_enabled`/`boot_autojoin` (and `;WIFI=`/`;BT=`/
   `;AUTOJOIN=`).
2. **Parking deleted** — `updateApPowerPolicy()` rewritten to guarantee-only (bail on
   `!wifiEnabled_`; restore beacon when STA down). `apParked_`, `AP_PARK_GRACE_MS`,
   `AP_PARK_HTTP_MS`, `lastHttpAtMs_` stamp removed. `WIFICFG;PARALLEL`/`WIFICFG;APSAVE` and
   `ap_psave`/`allow_parallel`/`last_transport` residue wiped on fresh install.
3. **Identity** — `WIFI_AP_NAME = "4WD4MCar_WiFi"` (ONE name; `WirelessCar_Wifi` +
   `4WDCar_Wifi` purged from app registry + firmware). `WIFI_AP_IP[] = "192.168.245.1"`;
   OLED fallbacks now `WIFI_AP_NAME`/`WIFI_AP_IP`.
4. **Deleted-network root cause** — `ROUTERS;DEL` registry-row + `wasActive` matching is now
   case-INSENSITIVE (`strcasecmp`), so a different-case delete removes the row + active pair
   and the router stays gone after a power cycle.
5. **App companion (3.2.7, same version)** — registry/own-AP name mirror `4WD4MCar_WiFi`;
   Remote settings show the three linked-gated toggles (Wi-Fi/BT/auto-join) that query on
   link and persist to DevicePrefs + cloud; arbitration is both-live; deleted-network deep
   fix via `wifi_enabled` cloud write-through.

**Gates (car):** syntax 10/10 · `check-webpage-js.mjs` PASS (section 8 rewritten to the
no-park + toggle contract) · `check-relay-contract.mjs` ALL PASS — source-level only; device
verify stays owner-side. **Owner flash target is now 1.3.5** (re-flash is REQUIRED for the
SSID rename — bench car still 1.0.5, F-61). After flashing, the app needs the **force-close
×2** same-version OTA dance to pick up the new registry + toggles.

**Committed + pushed (2026-10-13):** car `4645562` (1.3.5) · app `4cc5b7e` + docs `a01c44c`
(JS-only OTA 3.2.7, gates green both repos). **Next:** device verify per the new U-105 rows in
`DEVICE-TESTS.md` after the owner flashes **1.3.5** (the SSID rename + toggle commands need
the flash; bench car still 1.0.5, F-61), then the app's `TESTING.md` U-105-0..8 in the same
session (force-close ×2 for the app OTA first).

## U-104 2026-10-13 — watchdog feed before the sendState() WS fan-out (`02e1081`, PUSHED, 1.3.4, UNFLASHED)

Owner's post-1.3.3 field failures (car not (re)connecting, restart soon after connecting, flaky
WiFi/parallel display, absent car page, restart/freeze) — root-caused round; full round log:
`RESUME-HERE.md`. The app half is the F-65 same-version OTA (`bc9062c`); HERE the repo carried one
provably-safe firmware hardening + the version label that owns it.

1. **`WebServerComm::write()`** — NEW `esp_task_wdt_reset()` immediately before each
   `ws.broadcastTXT()`. `ModeManager::sendStateOn()` delivers STATE / CAPS / NETW / SCAN through
   this one function (up to four WS broadcasts per transport per 1.5 s ticker) and it was the ONLY
   WS-write path with no feed of its own (`broadcastStatus()` and `ws.loop()` have theirs). A WS
   client that stops draining TCP can hold a broadcast for up to 5 s (U-77 measurement) — the
   heartbeat owns long-lived stallers, but there was nothing between a watchdog tick and a write
   that STARTS here. Now there is: the write owns a fresh `LOOP_TASK_WDT_TIMEOUT_MS` window. Gate
   rule `ws-broadcast-feed` (relay-contract PASS).
2. **`FW_VERSION` 1.3.3 → 1.3.4** — the binary that carries this change names itself 1.3.4 on
   STATE, `/status` and `device_crash_reports.fw_version` (the 931b31f lesson applied again).
3. **Power conclusion (owner answer, documented, NO firmware change):** separate 2×3.7 V packs —
   the L298N alone on one, a second 2S through a buck converter for ESP+OLED. The crash DB
   (board `1FB608`, 929 boots / 746 crashes, PANIC + BROWNOUT, `last_crash_phase` NULL) therefore
   points at the buck / regulator / wiring rail, not at firmware. Wiring guidance: prefer a 3.3 V
   buck feeding the 3V3 pin directly (past the board's AMS1117-class LDO), or a ≥1 A rated buck
   with a low-ESR 100-470 µF bulk cap at the board input; short, thick power leads; confirm the
   brownout detector doesn't trip under radio TX bursts. The firmware power budget (motor power
   deferred, power-budgeted boot) stays as-is.
4. **Reviewed, no change:** the U-79 spinlock reads under `portENTER_CRITICAL`
   (`btComm.connected()` / `hasWsClients()` / `relayComm.connected()`) are plain flag reads by
   design (RelayComm publishes `socketUp_`/`enrolled_` from its task); the boot auto-join
   beacon-blank window is the documented FIN-40/D2 trade-off with retries already capped; the
   PANIC rows stay un-named until a `last_crash_phase` capture exists (no serial available).

**Gates:** syntax 10/10 · `check-webpage-js.mjs` PASS · `check-relay-contract.mjs` ALL PASS
(source-level only — device verify stays owner-side; owner flash target is now **1.3.4**).

**App counterpart:** F-65 `bc9062c` — fleet-AP discovery sweep (`OWN_AP_NAMES` registry, so a
4WD4M car on `4WDCar_Wifi` is re-found instead of a donor `WirelessCar_Wifi`), honest
`reconnecting` status on WS drop (no fake green / no vanish), bounded keep-looking re-sweeps
(15 s × 6 ≈ 90 s) after a failed rejoin. JS-only OTA, app version 3.2.7 unchanged.

## M3 2026-10-13 — `currentLink()` names BOTH live links under parallel (`d1e33bc`, PUSHED, 1.3.3, UNFLASHED)

Owner directive (second connectivity review): parallel BT+WiFi must connect AND display both links
at once on the app AND the car. The U-79 loop already keeps both transports up under
`WIFICFG;PARALLEL;ON`; the OLED panel still lied by naming only one link.

1. **`ModeManager.cpp` `currentLink()`** — new FIRST branch before the single-transport returns:
   `if (ws && ws->allowParallel() && wsClients && btUp) return {"Parallel", "BT+WiFi", true};`
   (the previous session's RESUME-HERE claimed this line existed; it did NOT until this commit).
2. **`Oled_1_3.cpp`** — row 3 gains `Parallel : BT+WiFi` (was falling into the generic `Wifi` else branch).
3. Not touched: U-79 teardown, `updateApPowerPolicy()`, `isConnectedNow()`, STATE/JSON parallel+apsave fields, blink fix.

**Gates:** `node tools\check-webpage-js.mjs` PASS · `node tools\check-relay-contract.mjs` all PASS
(source-level only — device verify stays owner-side; bench car still 1.0.5, F-61).

**App counterpart:** the parallel display round is app `08f0351` (connection strip, inventories,
`BT · name + WiFi · ip` labels) in the genumsolutions-app repo — same owner ask, one round.

## M3-fix 2026-10-13 — version label made coherent (`FW_VERSION` 1.3.2 → 1.3.3) + CI re-run

Owner reported an error on the M3 commits. Two findings, both resolved:

1. **CI red was a flake, not code.** Run `38357096202` (docs commit `51c229b`) failed at
   `arduino/compile-sketches` while DOWNLOADING the arduino-cli
   (`urllib... RemoteDisconnected`), before compiling anything — the identical source had
   compiled green on `d1e33bc` minutes earlier. Re-ran the failed job: compile `4m25s` PASS,
   page-js PASS, relay-contract PASS. No code change.
2. **`FW_VERSION` never bumped.** `d1e33bc`/`51c229b` labelled this round **1.3.3** and the
   owner's flash target is 1.3.3, but `Config.h` still reported `"1.3.2"` and there was no
   1.3.3 changelog entry — the binary the owner flashes would have named itself 1.3.2 on
   STATE, `/status` and `device_crash_reports.fw_version`, undoing U-R4 crash-phase
   disambiguation. Fixed: `Config.h` `FW_VERSION` → `"1.3.3"`; `TRACKS/CHANGELOG.md` 1.3.3
   entry; `Readme.md` + `CATALOG.md` version rows updated.

## U-R3 2026-10-12 — connectivity rebuild + restart record (1.3.2, UNFLASHED)

Owner directive: car restart/freeze truth over Bluetooth, stop relay dropping long STATE, plus connectivity rebuild — ap_psave/allow_parallel reported on STATE and /status, OLED blinks last two rows.

**Connectivity rebuild (car half for U-R3):**
1. **`ModeManager.cpp` `sendStateOn()`** appends `;APSAVE=ON|OFF` and `;PARALLEL=ON|OFF` to every STATE line (via `WebServerComm::apPowerSave()` / `allowParallel()` getters). Mirrors the /status JSON fields so a Bluetooth-only remote reads the car's stored config without WS round-trip.
2. **`WebServerComm.cpp` `buildStatusJson()`** adds `"ap_psave"` and `"allow_parallel"` boolean fields to `/status` JSON.
3. **`WebServerComm.h`** adds `bool apPowerSave() const` getter (parallels existing `allowParallel()`).
4. **`Oled_1_3.cpp` `redrawNow()`** blinks the last two body rows (network method + IP) at ~1 Hz (`BLINK_INTERVAL_MS = 500`). `blinkState` toggles in `redrawNow`; rows 3/4 drawn only when `blinkState` true.
5. **Restart record on STATE** (from prior U-R3): `;RST=...;BC=...;CC=...;PH=...;LH=...` appended to every STATE line via `crashLogInfo()`.
6. **Relay byte budget 448→512** (from prior U-R3): `Config.h::RELAY_MAX_OUT_BYTES`, `RelayComm.h::kOutLineLen`, static_asserts, `check-relay-contract.mjs` pin `512`, website relay `maxBytesCarToController`.

**Gates green:** syntax 10/10 · relay-contract PASS (cap 512) · webpage-js PASS.
**Owner action:** flash **1.3.2** (USB, 115200, `huge_app`) — along with owed **1.3.1**. Then connect over Bluetooth and confirm:
- Diagnostics restart row shows reason+phase
- `device_crash_reports` syncs over Bluetooth
- STATE line carries `APSAVE`/`PARALLEL` (check via app/remote)
- `/status` JSON includes `ap_psave`/`allow_parallel`
- OLED last two rows blink

**UNFLASHED (F-61):** bench car on 1.0.5; 1.3.1 and 1.3.2 both need flash.

## U-R3 2026-10-12 — the restart record over Bluetooth (1.3.2, UNFLASHED)

Owner directive (car restarts/freezes + no readable error/log medium): make the car's restart truth
reachable over **Bluetooth** as well as Wi-Fi, and stop the relay silently dropping the longest STATE.

- **`ModeManager.cpp` `sendStateOn()`** now appends the persisted restart record to **every** STATE
  line (both links): `;RST=<reason>;BC=<boot count>;CC=<crash count>;PH=<phase-at-boot>;LH=<heap-at-boot>`
  from `crashLogInfo()` (`#include "CrashLog.h"` added). `wifiKeys` 224→288, `tKeys` 96→176.
  Phase strings are firmware literals (same rule as `buildStatusJson`).
- **Relay byte budget 448→512** — a STATE with the record + a full `REPLY=` exceeds 448, and the relay
  drops anything over its cap (the U-94 Phase 1 silent-drop class). Changed together:
  `Config.h::RELAY_MAX_OUT_BYTES`, `RelayComm.h::kOutLineLen`, both `RelayComm.cpp` static_asserts,
  `tools/check-relay-contract.mjs` (pin `512`), and the website relay's `maxBytesCarToController`.
- `FW_VERSION` 1.3.1→1.3.2; `TRACKS/CHANGELOG.md` entry added.
- **Gates green:** syntax 10/10 · relay-contract PASS (`outbound cap 512`) · webpage-js PASS.
- **Owner action:** flash **1.3.2** (USB, 115200, `huge_app`) — along with the owed **1.3.1**. Then
  connect over Bluetooth and confirm the Diagnostics restart row + `device_crash_reports` sync.
- App half: `carProtocol.ts` STATE parsing (`RST`/`BC`/`CC`/`PH`/`LH`) + the Admin "System Logs" tab
  reading `device_crash_reports` + the new `app_logs` table.

## Connectivity-rebuild 2026-10-12 — ALL PHASES DONE (car half); one 1.3.0 flash owed

The multi-repo "connectivity rebuild" (`TRACKS/PLAN-2026-10-12-connectivity-rebuild.md`) is finished
on the car side. Phases 1+4 ship as ONE **1.3.0** batch (never flashed — the bench car is on 1.0.5):

- **Phase 1 (U-100)** — the own hotspot is **always reachable**; parking is opt-in via NVS
  `botcfg/ap_psave` (default OFF) + `WIFICFG;APSAVE;ON|OFF`.
- **Phase 4 (U-100 Phase 4)** — **parallel Bluetooth + WiFi**; NVS `botcfg/allow_parallel` (default
  OFF) + `WIFICFG;PARALLEL;ON|OFF`; the U-79 loop keeps both links up when ON.

`FW_VERSION` stays **1.3.0** (bumped in Phase 1; a second bump would name a binary never built).
Gates green: syntax 10/10 · relay-contract PASS · webpage-js PASS · mutate-gates PASS.
**Owner action:** flash `main` **1.3.0** (USB, 115200, `huge_app`) → `tools/bench_u90_u92.py <ip>`
(U-92/U-90 rows + P4-1..3) → `TRACKS/DEVICE-TESTS.md`. **F-61**: nothing here is device-verified yet.

## READ THIS FIRST — WHICH REPO IS THE CAR

There are TWO car repositories and they are easy to confuse:

- **`Genum_4WD4M_CAR` (THIS repo) — the LIVE one.** Every car fix since 2026-09-28 lands here.
  `main` carries `b59236a` (2026-10-11, U-98 D1–D3) plus the P2 tip on top; CI green. U-98
  (1.3.0) is **committed but UNFLASHED** — see the session block below.
- **`Genum_WIRELESS_CAR` — the OLD one, frozen.** `main` = `d0bf8c0` (1.8.4, 2026-09-26).
  It has no recent commits because nothing is developed there any more; 4WD4M was bootstrapped
  from it as a "fresh baseline" (see the repo description). If a repo page shows
  "no recent commits", you are almost certainly looking at THIS old one.

Owner report (2026-10-10): *"the car repo says no recent commits but which is not true."* Verified
via the GitHub API: `Genum_4WD4M_CAR` HEAD on `main` == local HEAD == `c5b8b9d`, all eight
repos under `genumsolutions` are in sync with their remotes, and no clone in the workspace has
unpushed commits. The report was the stale old repo, not lost work.

## This session (2026-10-12, third round) — U-100 Phase 4: parallel Bluetooth + WiFi (power-saving toggle, default OFF)

Part of the multi-repo "connectivity rebuild" (phased; see
`TRACKS/PLAN-2026-10-12-connectivity-rebuild.md`). Source-only, **UNFLASHED (F-61)**.

- **Owner request #6.** Parallel Bluetooth + WiFi with a power-saving toggle, default OFF.
- **Fix.** The U-79 loop's two teardown branches (a BT client stops WiFi; a wired client stops BT)
  are now skipped when the switch is ON — both links stay up at once, and a transport that is still
  down is re-raised with the idle path's idempotent starts. OFF (default) is unchanged.
- **Switch.** `WIFICFG;PARALLEL;ON|OFF` (bare/`?` reports), NVS `botcfg/allow_parallel`, read in
  `loadStoredWifi()`; exposed to the loop via `WebServerComm::allowParallel()`. Rides the single
  system-command hook (BT or web). Not in the fresh-install wipe (a user preference, not router data).
- **Gates.** syntax 10/10 · relay-contract PASS · webpage-js PASS (two new section-8 rules) ·
  mutate-gates PASS. New `bench_u90_u92.py` rows P4-1..P4-3 + a DEVICE-TESTS round.
- **Next.** Phase 5 (ledgers + root `RESUME-ME.md`) — **DONE**; see the top block.

## This session (2026-10-12, second round) — U-100: the own hotspot is ALWAYS reachable (parking is opt-in, default OFF)

Part of the multi-repo "connectivity rebuild" (phased; see
`TRACKS/PLAN-2026-10-12-connectivity-rebuild.md`). Source-only, **UNFLASHED (F-61)**.

- **Owner report.** *"the car now goes into one of the other router and doesnt display its own
  homepage and doesnt connect to the app too."* Ruling: keep `4WDCar_Wifi` always reachable, but
  keep an app setting to turn the power saving back on.
- **Cause.** ESP32 AP_STA is single-radio: joining a router moves the softAP's channel, so a phone
  on the car's own hotspot loses it (`discovery.ts:12-19`); U-98's boot auto-join makes it happen
  unattended. And U-86 could park the beacon without being able to re-raise it while still
  associated.
- **Fix.** Park is now opt-in: `updateApPowerPolicy()` returns early unless `apPowerSave_`
  (NVS `botcfg/ap_psave`) is on. Default OFF → the own hotspot is never parked → always a way in.
  New command `WIFICFG;APSAVE;ON|OFF` (bare/`?` reports) sets + persists it; a fresh install wipes
  the key with the other router keys.
- **Gates.** syntax 10/10 · relay-contract PASS · webpage-js PASS (two new section-8 rules).
- **Next.** App UI toggle (Remote → Connectivity settings) + the rest of the phased rebuild.

## This session (2026-10-12) — U-99: the own hotspot is configured ONCE (no duplicate hotspot / two IPs)

The owner flashed/started 1.3.0 for the first time and the OLED showed the car's OWN hotspot
TWICE — one blank with `192.168.4.1`, one `4WDCar_Wifi` with `192.168.245.1`. Source-only fix (not
yet re-flashed; **F-61**):

- **Root cause.** `beginAlwaysOn()` ran `beginStation()` in `WIFI_AP` (AP up as the real
  `4WDCar_Wifi · 192.168.245.1`), then `WiFi.mode(WIFI_AP_STA)`. Core 3.3.12 `WiFiGeneric::mode()`
  returns early only for the SAME mode; AP→AP_STA calls `esp_wifi_set_mode()`, which stops/restarts
  the radio and rebuilds the softAP from the SDK defaults (blank name, `192.168.4.1`). The panel
  then held BOTH identities.
- **Fix.** Set the final mode FIRST, then bring the AP up once: `WiFi.mode(WIFI_AP_STA)` →
  `beginStation()` (which no longer calls `WiFi.mode(WIFI_AP)` — `startOwnAP()` is now the only
  wifi-config op). One softAP, one name, one IP. R-16 kept: `startOwnAP()` still runs with nothing
  joined (`staSharing` false) → own AP on channel 1.
- **App note (same report, checked).** `WifiPanel`'s row builder deduped the scan list
  case-sensitively and did not skip empty SSIDs, so a hidden/blank beacon produced a nameless row
  and a differently-cased duplicate of a saved network. Fixed in `genumsolutions-app` with ONE
  empty-safe, case-insensitive key for the whole list (+ mirrored test + two new cases).
- **Gates.** syntax 10/10 · relay-contract PASS · webpage-js PASS. No hardware; F-61.

## This session (2026-10-11, third round) — U-98: a flash starts clean, and a boot returns to the last-switched router

Committed and **UNFLASHED** (F-61): D1–D3 at `b59236a`, P2 on top. `FW_VERSION` → `1.3.0`.
Four owner reports, one root-caused cluster:

- *"clear all the other saved router datas and only have its own data each time i flash the car …
  the car is clean and fresh always"* · *"the car should always go to the last switched router
  network always; if it is not available it should show its own router …"* · *"i deleted the saved
  router but the car restarted with the old router that i was previously connected"* · *"the car is
  stuck on one of the outside router and not even showing its own webpage too so that i can change
  and delete and edit things"*.

- **D1 — a re-flash did not reset the router surface.** Arduino flashing does NOT erase NVS, so
  saved routers + the stored pair + the last-good pair survived every re-install. New
  `botcfg/fw_mark` stores `FW_VERSION`; `wipeRoutersOnFreshFirmware()` — called in `setup()`
  before `loadStoredWifi()`/`loadSavedRouters()` — erases every router key and writes the mark on
  a mismatch. Same-firmware power cycles keep the routers (U-84); only a firmware (re-)install
  wipes them. Mode/speed/relay keys in `botcfg` are untouched.
- **D2 — a deleted router resurrected on the next boot.** `armBootAutoJoin()` trusted
  `lastGoodSsid_` (`last_ok_ssid`), and `ROUTERS;DEL` cleared only `wifi_ssid`, so the deleted
  network came back. Boot now joins `storedSsid_` (the last-SWITCHED router); the last-good pair
  is informational and is erased with the stored pair at all four clear sites
  (`WIFICFG;RESET` · `ROUTERS;USE 4WDCar_Wifi` · `ROUTERS;DEL` active · `ROUTERS;CLEAR`).
- **D3 — "stuck on an outside router, no own webpage"** is D2's symptom: the car hopped to a
  router the phone was not on, and in AP_STA the softAP follows the STA channel, so
  `4WDCar_Wifi` was not findable. D1+D2 fix it at the source.
- **Budget:** `BOOT_STA_TIMEOUT_MS`/`BOOT_STA_MAX_RETRIES` (6 s / 1) removed — the boot auto-join
  uses the full explicit-switch budget (`WIFI_STA_TIMEOUT_MS`/`STA_MAX_RETRIES`); the own hotspot
  beacon stays up the whole retry, so it can never strand the owner. `lastTransport` no longer
  gates the hop (boot log line only).
- **P2 — the OLED extra row ticks apart from the WS broadcast.** `updateExtraInfo()` (the OLED
  network/extra row) and `broadcastStatus()` used to share one `EXTRA_INTERVAL` 1 s tick, so the
  OLED could not refresh faster without doubling the status JSON to every client. Split:
  `EXTRA_INTERVAL` **1000 → 500 ms** for the extra row, broadcast keeps its own
  `STATUS_INTERVAL = 1000 ms`. New gate rule `extra-status-decoupled` + mutants M68/M69.
- **Gates after this session:** syntax **10/10** · `check-webpage-js.mjs` PASS ·
  `check-relay-contract.mjs` PASS (incl. `extra-status-decoupled`) · `mutate-gates.mjs` **69/69**
  byte-exact. All source-level — **nothing flashed; F-61 stands.** Files: `Config.h`,
  `WebServerComm.h/.cpp`, `Genum_4WD4M_CAR.ino`, `tools/check-relay-contract.mjs`,
  `tools/mutate-gates.mjs`.

## This session (2026-10-11, second round) — U-96i done: the car tells the app its version

- **The car reports its firmware version on `/status`** (`"fw":"<FW_VERSION>"`, commit
  `3168cac`). The app had no way to know what firmware a car runs: the bundled registry
  catalog value was presented as truth and the fleet crash table's `fw_version` was filled
  from it. Gate rule `status-fw` pins both halves — the slot exists, and FW_VERSION feeds it
  in that slot's snprintf position (argument order IS the contract). Mutants M66 + M67 added;
  harness now 67/67.
- **App half** (`genumsolutions-app 25e72a9`): `j.fw` → `telemetry.fwVersion`; the hub passes
  it to `syncCrashReport`; the `statusJson.fw: telemetry.id` mislabel (BOARD ID in a field
  named fw) is fixed; the 4wd4m catalog row synced to 1.2.4 as a model fact. Three
  source-reader rules, all mutation-proven — including one that initially PASSED over a
  deleted dep (the slice included the call body; narrowed to the deps array — the recurring
  vacuous-pass class, recorded in FAILSAFE).
- **Gates after this session:** syntax 10/10 · page-js · relay-contract (incl. status-fw) ·
  mutate-gates 67/67 byte-exact · app tsc 0 / vitest 671/671 / prettier clean.
- **Pre-1.2.5 cars send no `fw`** — the app treats absent as unknown. Only a flash makes the
  handshake live; F-61 stands.

## This session (2026-10-10, second round) — the branch compiles again, and the hotspot stops dropping itself

The previous round ended with `c0e4ce6` pushed, **the branch red**, and half of U-98 still in the
tree. Owner: *remove the `[1/2]` badge, treat all router networks equally, keep it clean, and the
ESP32 "is not working at 100%" — the car's own hotspot drops after a while and will not connect
or switch cleanly.*

- **`c0e4ce6` had broken the build.** `server.hasClient()` — `hasClient()` belongs to
  `NetworkServer`, which `WebServer` **contains**, not inherits (`WebServer.h:342`, protected).
  CI run `38041257338` was red. Now 10/10.
- **The bug under it was worse than the compile error, and it IS the hotspot report.**
  `inUse = ws.connectedClients() > 0` counts clients and cannot tell which side they arrived on,
  so a phone driving the car **from `4WDCar_Wifi`** counted as a reason to **park `4WDCar_Wifi`**
  — `AP_PARK_GRACE_MS` (20 s) after the page opened the car dropped the very link it was being
  driven over. `inUse` is now `WiFi.softAPgetStationNum() == 0 && (ws.connectedClients() > 0 ||
  httpActive)`: anyone on the AP forces it false. `softAPgetStationNum()` is a cached driver
  count — no radio work, so U-71/F-24's one-call-per-pass still holds. The HTTP half is real
  now (`armHttpGuard()` stamps `lastHttpAtMs_` in every handler; bounded by `AP_PARK_HTTP_MS`).
- **`[1/2]` removed END TO END.** Only the `snprintf` had gone. `cycleNetworkDisplay()`,
  `setNetworkCycling()`, `setSavedRouterCount()`, the three state fields, the `|Nets:` parse, the
  two callers in `updateExtraInfo()` and the `|Nets:` producer are all gone. That tail had one
  reader and it truncated the IP line the owner asked for in v1.2.4.
- **The car's own page needed nothing.** Verified, not assumed: `WebPage.h` rebuilds the router
  dropdown from `j.networks` as plain options — no ranking, no own-hotspot label, no index — and
  has no guidance prose in its network section. No change made.
- **Gate: `check-webpage-js.mjs` asserted only the WS half of `inUse`**, which is exactly the half
  that cannot see this bug. It now asserts both halves + that they share one expression + that
  the HTTP branch is fed. Mutation-proven both directions, including deleting the stamp (which
  used to leave it green).
- **Fixed 2026-10-11 — the gate defect the previous session flagged and left open.**
  `check-firmware-syntax.ps1` set `$ErrorActionPreference = "Stop"`, so PowerShell 5.1 threw a
  `NativeCommandError` on gcc's stderr at the invocation line — **before** the `FAIL <file>` /
  `Select-String 'error:'` reporting could run. On a failing TU it printed **no error at all**
  and the `foreach` ABORTED, so the remaining translation units were never checked: exit 1, but
  a partial-coverage run with no diagnosis — the U-96 class. **Reproduced first, then fixed:**
  the gcc call now captures stderr as data with EAP locally relaxed (`Continue`) inside
  `try/finally` that restores the script-wide `Stop`, so every other line still fails fast.
  **Proven in both directions on PS 5.1:** one broken TU → `FAIL BluetoothComm.cpp` + the gcc
  error line + `1 / 10` (the other 9 still checked); TWO broken TUs (a `.cpp` AND the `.ino`)
  → both reported by name, `2 / 10`, loop not aborted; clean tree → `PASS 10/10`, exit 0.
  NOTE: only Windows PowerShell 5.1 exists on this machine (`pwsh` not installed) — that is the
  exact runtime where the defect lived.
- **Verified:** syntax 10/10 · page-js · relay-contract · mutate-gates 65/65 (byte-exact).
  **F-61: source-level. Nothing flashed. `RELAY_HOST` still unset. T-REL-1..4 still unrun.**

## This session (2026-10-10) — relay Phase 2: the dial moves off the loop task

**UPDATE 2026-10-11: this "draft" is MERGED and COMPILED.** Verified by ancestry, not assumed:
`14cbf5d` (the task commit) is an ancestor of `main`, `git branch --merged main` lists
`feat/relay-phase2-own-task`, and CI run `38108951033` compiled `main` (which contains the
`xTaskCreate` RelayComm) green — page-js + relay-contract + compile, all three jobs. The history
is linear: `14cbf5d` and its follow-ups sit directly on `main`'s first-parent line (no merge
commit); `feat/relay-phase2-own-task` is just a stale pointer at the same tip. Still **not
flashed** (F-61);
`RELAY_HOST` still unset, so T-REL-1..4 still unrun. `FW_VERSION` deliberately left at `1.2.4`.

*(As written 2026-10-10: draft on `feat/relay-phase2-own-task`; the local `arduino-cli` is
present but blocked by an application-control policy, so CI was to be the first real build.)*

- **The F-84 fix, implemented.** `RelayComm` now runs the dial, the TLS handshake and every
  `client_.loop()` on its own FreeRTOS task (`xTaskCreate`, 20 KB stack) that is NOT subscribed to
  the loop-task watchdog. The loop task only queues outbound lines, drains inbound lines, and
  supervises a task heartbeat; it never touches `client_`. A supervisor in `handle()` drops the link
  flags if the task stalls, so a wedged relay cannot keep the 2 s safe-stop switched off.
  `connected()` = `started_ && socketUp_ && enrolled_`, where `socketUp_` is published by the task
  from `client_.isConnected()`.
- **Gates changed deliberately.** `armed-handshake` no longer refuses a non-empty `RELAY_HOST` (the
  task is the evidence the handshake is bounded) but still refuses arming the relay with the dial
  back on the loop task; `enrolment` now checks both halves — `connected()` gates on `socketUp_` +
  `enrolled_`, AND the task fills `socketUp_` from `client_.isConnected()`. Mutants: M18 retargeted
  (arm the host AND remove the task → must fail `armed-handshake`), M63 added (sever the socket feed
  → must fail `enrolment`).
- **Verified here:** `node tools/check-relay-contract.mjs` → green. Arming `RELAY_HOST` now passes
  `armed-handshake` (proven by temp edit + revert); severing the `socketUp_` feed fails `enrolment`
  (proven). **Not verified:** the compile (CI only) and the bench (T-REL still pending).
- **Files:** `RelayComm.h`, `RelayComm.cpp`, `tools/check-relay-contract.mjs`,
  `tools/mutate-gates.mjs`, plus the `TRACKS/` docs. See `TRACKS/CHANGELOG.md` § DRAFT.
- **Review-pass fix (same draft): the relay could never pair.** `controlEventIs()`/`controlNumber()`
  ran `strstr`/`strchr` on the whole frame, whose first byte is the prefix's NUL — so they searched
  an empty string and matched nothing, and `enrolled_` never went true. Now they search from
  `frame + kControlPrefixLen`. **Pre-existing on `main` (`6da6788`), not from the F-84 work.** Added a
  `control-parse` gate rule (both helpers must offset) + mutant **M64** (revert the offset → red).
  Also: `answerPing()` now copies the prefix as bytes and sends with an explicit length (a `%s`/`strlen`
  build dropped the NUL prefix and leaked the echo to the phone as a car line); `stop()` no longer
  clears `sInstance`, so RESET-then-reprovision can re-pair.
- **Two review LOWs fixed (same draft).** `available()` is now boolean like `WebServerComm`'s — the
  byte-count form returned 0 while a line's `'\n'` terminator was still pending, so the drain spun its
  full 20 ms timeout each pass. `handleTextFrame()` now rejects at `len >= kLineLen` (max 127), so a
  128-byte line is refused instead of silently losing its last byte in `queuePush()`. New gate rule
  `inbound-fit` + mutant **M65**.

## This session (2026-10-09, round 2) — the 4WD4M OLED's network lines are done, v1.2.4

Owner directive (2026-10-09), delivered and corrected in-session:

- Row 3 (2nd-last line) = the current method + network: `Wifi : <ssid>` for the WiFi
  method, `Bluetooth : <transport status>` (SPP / Off) for Bluetooth, the hotspot's name
  while the own AP broadcasts.
- Row 4 (last line) = **the car's CURRENT IP** — the number the app shows (`WiFi · <ip>`),
  NOT the network's directed-broadcast address (the first rendering, corrected same-session
  on the owner's bench: *"broadcast ip shown by car oled is wrong. fix this in car oled
  like the app"*).
- Removed the "LAN ready" message repo-wide, dropped the 2.5 s blink, moved the link bars
  to the Speed row so lines 3/4 own the full width. Broadcast still lives on `/status
  broadcast_ip` + the page's `Bcast:` row. Commits `7731f2b` + `0285eee`; full detail in
  `TRACKS/CHANGELOG.md` § v1.2.4; gates 10/10, 62/62.
- A `backup` branch now tracks `main` at 0285eee on origin.

**STILL NOT FLASHED. Relay still BLOCKED (RELAY_HOST unset). The reset loop is still open —
first read after flash is the app's Restarts row / `/status`.**

> **READ THIS FIRST (added 2026-10-06, second pass).** `main` did not compile when this session
> started. Two commits in a row had tried to fix U-96's brownout line and both guessed a symbol
> that does not exist on ESP32 core 3.3.x (`c3c29e6`, `d34b52c`). The build is now fixed and
> `tools/check-firmware-syntax.ps1` has been changed so this cannot happen silently again. Everything
> below about U-95/U-96 is still source-level only — **nothing has been flashed.** Full detail in
> `TRACKS/FAILSAFE.md` #12–#14 and `TRACKS/TODO.md` § Phase U-96 follow-up.

> **READ THIS FIRST (added 2026-10-09).** The car's own page no longer offers a scan: the
> network table, its Scan button, the `lastScanSig` gate and both `.nets` CSS blocks are gone
> (`78b15e9`, `fb218dc`, and this sweep). The router dropdown is now the page's only network
> control; the firmware scan and the app's `ROUTERS;SCAN` are untouched, and the page still
> narrates `STARTED`/`BUSY`/`CANCELLED` replies. The page gate rule `page-net-table` was
> retired; the mutation harness's five orphans (M56-M62) were re-aimed at
> `page-router-dropdown`, and that rule was made non-vacuous, so a mutant that breaks
> switching is now caught instead of passing. The dead `modeHasDash` OLED member was removed;
> the unreachable ESP_SER drawing block is retained (its header comment corrected to say so).
> **No firmware was flashed** - this is all source-only. Details in `TRACKS/TODO.md` § Sweep.

## The three facts that matter most

1. **The brownout threshold cannot be set from a sketch on core 3.3.x.** Not "hard to find" —
   *not reachable*. It moved to a Kconfig option compiled into the prebuilt `esp32-libs`; there is
   no `DET_LVL` register in any public header and `esp_brownout_init()` takes no level. The write is
   gone. An `#error` guard now pins `CONFIG_ESP_BROWNOUT_DET_LVL == 0` so a core bump trips the build.
2. **The old comment's polarity was inverted.** IDF runs SEL_0 = *lowest* (~2.43V) → SEL_7 =
   *highest* (~2.80V). The code read "0 = 2.69V … default 1" and wrote 0 to *raise* the threshold; it
   would have *lowered* it. The core already ships level 0 — the most tolerant setting — so the
   mitigation U-96 was reaching for is **already in effect by default**.
3. **The gate reported `PASS: 9/9` the whole time.** It filtered `-Filter *.cpp`, so it never
   compiled the `.ino` — the one file the break was in. A gate that covers 9 of the 10 files that
   make up the firmware is worse than no gate, because it converts an uncompiled file into a trusted
   one. The sweep now includes `*.ino` (`-x c++` + sketch dir on the include path) and **exits 2 if
   it finds no `.ino`**. Proven in both directions: a deliberately broken sketch makes it FAIL, the
   restored sketch makes it PASS 10/10.

**Also done this session — the app half (no firmware change needed).** The car already sent U-95's
`reset_reason` / `boot_count` / `crash_count` / `last_crash_phase` / `last_crash_heap` on `/status`.
The app parsed `free_heap` out of the *same JSON object* and silently dropped all six. U-95's whole
purpose was to put the evidence where the owner already is — and the owner was looking at their
phone. Now parsed, and shown as a **Restarts** row on the Tools panel: `None` for a real zero, the
count plus `TASK_WDT during http-root`-style hint when it is climbing, and **no row at all** on
pre-1.2.0 firmware rather than a dash the owner would read as reassurance. Offline ⇒ dash, because
a stale record is not a current fact.

**Gates after this session:** syntax **10/10** · `check-webpage-js.mjs` all stages pass ·
`check-relay-contract.mjs` all checks passed · app: tsc 0, vitest **626/626** (was 615),
expo-doctor 18/18, prettier clean.

**STILL TRUE FROM BEFORE — the reset LOOP is not cured.** U-95 made it observable; nothing more.
`http-root` and `relay-tls` remain the two named suspects. First read after flash: `/status` →
restart record, or the app's Restarts row.

---

# (earlier session notes follow)

**U-95 PHASE 0 AND PHASE 1 ARE WRITTEN AND PUSHED. FIRMWARE IS NOW `1.2.0` AND IS STILL NOT ON A
CAR.** Everything below marked U-95 is source-level. The bench car is still running `1.0.5`. F-61.
Commits, in order: `883626b` (crash record), `29b8eaf` (page-send budget + watchdog feed), `83ecf15`
(idle attribution), `d78c27d` (relay guards + deployment interlock), `f73aded` (bench sheet). CI green
on all five; no firmware has been flashed.

**WHAT U-95 ACTUALLY FIXED, AND WHAT IT DID NOT.** The owner's report was "the car resets again and
again" with no cause attached. U-95's first job was to make the resets *legible*, because the only
reset diagnostic was a Serial line the bench car does not have, and `.trigger_panic = true` reported
a hung `loop()` as `PANIC` instead of `TASK_WDT` — so the one label that would have named the cause
was unreachable.

- **Fixed — the confirmed reset cause (F-77, `29b8eaf`).** `serveRootBounded()` had a 6000 ms body
  budget against a 5000 ms watchdog, so a client that stopped reading reset the car *every time*; and
  on a zero-byte write the loop did `yield(); continue;` with no watchdog feed. Budget is now 3500 ms
  with 1500 ms of headroom, tied to the watchdog by `static_assert`, and the stall path feeds and
  waits a real `delay(1)` gap.
- **Fixed — attribution (F-79, `883626b`, `83ecf15`).** Boot count, crash count, reset reason, boot
  heap and the phase the car was executing now persist to NVS and surface on the OLED header, in
  `/status` and in the page. The second commit fixed a real bug in the first: the write floor
  rate-limited the *idle* transition too, so a parked-car crash was blamed on whatever the car last
  did. The header had promised otherwise and no gate noticed.
- **NOT fixed in 1.2.4 — the fix is DRAFTED on branch `feat/relay-phase2-own-task` (2026-10-10, uncompiled; see the session note at the top; F-84, `d78c27d`).** The relay TLS handshake runs
  on the loop task with `handshake_timeout = 120000` (core default, `NetworkClientSecure.cpp:41`) and
  the task watchdog measures wall-clock time, so the handshake's `vTaskDelay(2)` does not save it. A
  host that completes TCP then stalls TLS resets the car for up to two minutes. It cannot be tuned
  from here: `WebSocketsClient` keeps its `WiFiClientSecure` in a `protected` member, recreates it
  every retry, and `WEBSOCKETS_TCP_TIMEOUT` is read inside the library's own translation unit, so
  `#define`ing it in the sketch is a no-op. **The `armed-handshake` gate now FAILS THE BUILD for any
  config that sets `RELAY_HOST` while the dial is still on the loop task.** The fix is moving the dial
  to its own FreeRTOS task — now drafted (2026-10-10) on `feat/relay-phase2-own-task`, still awaiting a
  compile + a live-host bench. A TCP pre-flight was considered and
  REJECTED: it cannot help, because the dangerous host is one that *accepts* the connection and then
  stalls TLS.

**THE PART THAT SHOULD MAKE THE NEXT PERSON HUMBLE.** Three gates written during U-95 could not have
worked, and two of them printed `ok` while doing it (F-83). One counted `esp_task_wdt_reset()`
occurrences in the raw file and reported four watchdog feeds for a loop with three — the fourth was
the function's name inside the comment explaining the fix. One matched `next[0] != '\0'` against
comment-stripped source, where `code()` blanks character literals, so it failed on the *fixed* tree.
One used `indexOf("loadToken()")` and matched the word inside the comment explaining why the guard
must precede `loadToken()`. Every new source rule now states which view of the file it reads, and is
proven in both directions: revert the fix, confirm red, restore, confirm green. Green-only proves
nothing.

**U-94 PHASE 2 IS WRITTEN AND HAS NEVER BEEN ON A CAR.** `RelayComm` exists, the
third `ModeManager` slot is wired, `RELAYCFG` provisions a token, and the gates are
green. Firmware is now `1.1.0`. **The car on the bench is still running `1.0.5`.**
Nothing in this file's Phase 2 paragraphs is a result; they are intentions with
source-level checks behind them. F-61.

**1. THE PHASE 1 CONTRACT HAD THREE DEFECTS, ALL FOUND BY WRITING THE CAR SIDE.**
Writing the firmware against the relay is the only way to find these — a relay that
has never had a car talk to it cannot check a car's contract. All three were green
across three suites:
- **Every STATE line was dropped** (`039f014`). The frame cap was 128 in *both*
  directions, justified by a comment about `handleCommand`'s buffer — true of the
  phone's commands, false of the car's output, which is built into 448. A paired
  phone would have met a connected, authenticated, permanently silent car.
- **The latency pair never crossed** (`039f014`). The relay answered the phone's
  `ping` with its *own* echo, so the number measured the relay and was reported as
  the car, while the car's real `ping-echo` was refused as an unknown event.
- **The pairing key could never match** (`40dac31`). `BOARD_ID_RE` demanded 12 hex;
  `WebServerComm::boardIdHex()` prints 6 (`%06X` of `efuseMac & 0xFFFFFF`). Every
  real car would have been refused at the hello. Fixed on the relay side, because
  those 6 chars are already the car's identity in `STATE;...;ID=`, on its page, and
  in the app's `fw:<id>` profile. F-75.

**2. THE NEW GATE, AND WHY IT IS NOT MORE `static_assert`s** (`tools/check-relay-contract.mjs`,
CI job `relay-contract`, F-74). The Arduino job answers "does it compile". A transport
that compiles and is never connected looks identical to one that works, and three of
the four missing seams are safety-relevant: no drain means commands vanish, no STATE
fan-out means the phone watches a stopped car, and an `isConnectedNow()` that ignores
the relay means **the 2 s safe-stop fires continuously while the owner is driving**.
The gate derives the transport list from the class declarations and asserts each one
appears in `run()`, `sendState()`, `isConnectedNow()` and `currentLink()`, plus: the
grammar lock, the NUL control prefix checked before any content read, `beginSslWithCA`
and no `setInsecure()`, a heartbeat, and four rules about the token never reaching a
log or a reply. 12 mutations, one per rule, all caught by name.
*Self-inflicted lesson worth keeping:* the first version of this gate **failed
itself**, because prose in a comment (`never setInsecure()` on a car) matched the rule
banning it. Every structural rule now reads a copy of the source with comments and
string literals blanked out.

**2b. THE ANCHOR RULE WAS THE SAME ERROR, ONE LEVEL DEEPER (F-76).** The rule that
the trust anchor is ISRG Root X1 was `/ISRG Root X1/.test(source)` — and the only
occurrence of that string is the **comment** naming the anchor. It was green, and it
was evidence of nothing: it would have passed just as happily over a wrong, truncated
or absent certificate. Found by verifying the anchor by hand after the gate had been
green for a day, which is the only reason it was found at all.
The rule now parses the embedded PEM and asserts the artifact: subject is ISRG Root
X1, `issuer == subject`, `ca == true`, and the DER SHA-256 equals a pinned
fingerprint (`96:BC:EC:...:08:C6`). The pin is a second value that must be edited
deliberately, so rotating an anchor is a visible act. `rootCertificates` is read as a
second opinion when present, never as the assertion — a CI runner's trust store is
not the firmware's trust decision.
*Mutation-tested, because a check that has never rejected anything is a comment with
better manners:* one character changed in the base64 (valid DER, wrong fingerprint),
corrupt base64 (unparseable), a **real** ISRG Root X2 swapped in from the platform
store, and the block deleted — all four exit 1, control green. In every one of those
four the comment still said "ISRG Root X1", which is the point.
*Two costs worth recording:* a corrupt PEM threw out of `X509Certificate` as
`bad base64 decode`, which fails CI while telling the owner nothing, and is now caught
and named; and the "different CA" fixture was at first a certificate I typed from
memory into base64 — which did not parse, for exactly the reason the rule exists. A
fixture typed from memory is F-75 again. Take it from the trust store on disk.

**3. INERT BY DEFAULT, DELIBERATELY.** `RELAY_HOST` ships `""` and `begin()` refuses
to dial without a token, so a default build never opens a socket and a donor car
changes nothing. The gate fails if that ever stops being true. Filling in `RELAY_HOST`
is a decision someone has to make on purpose.

**4. NEXT SESSION, IN THIS ORDER.**
- Run every gate again before touching anything: `tools/check-relay-contract.mjs`,
  `tools/check-firmware-syntax.ps1` (8/8 TUs), the two CI jobs.
- **Ask the owner for `RELAY_HOST`** (the Supabase project host). Nothing can be
  bench-tested without it, and it is deliberately not a value I may guess.
- Then the owner's sequence: apply the migration, set `RELAY_TOKEN_PEPPER`, deploy
  `car-relay --no-verify-jwt`, enrol the car, then **T-REL-1..4** in
  `DEVICE-TESTS.md`. T-REL-1's first row — read the whole `REPLY=` aloud and confirm
  the token is not in it — is the one that matters most.
- Phase 3 (app) and Phase 4 (owner bench) are untouched. Phases 3/4 are *not* mine to
  claim complete.

**U-95 (2026-10-05) - VERIFICATION ROUND: the bench harness was lying before it ever ran. No
firmware change; firmware stays 1.0.5.** The owner could not flash, so this session re-ran every
gate on the machine instead of quoting prose, then audited the two untracked scripts in `tools/`
before they were ever pointed at a car.

1. **THREE HARNESS ROWS COULD NEVER PASS (`5b61250`, F-70 in `guide/FAILSAFES.md`).** Each would
   have read as a firmware fault on a healthy car:
   - **`REQ_CAPS` is not a firmware verb.** Only `REQ_STATE`/`REQ_NETW` exist
     (`ModeManager.cpp:309/315`); `CAPS;` rides the same `sendState()` (`:542`). **This is the
     fourth appearance of the invented-command trap** after the U-82 `NACK;...ARG=REQ_CAPS` probe
     and the two "does not answer" rounds that were the harness's own `REPLY=` fault. The row now
     drives `REQ_STATE`. Do NOT "fix" the firmware by adding `REQ_CAPS`.
   - **Absolute frame counts cannot work.** The car re-broadcasts `STATE` + `CAPS` + `NETW` every
     `STATE_INTERVAL_MS` = 1500 ms (`Config.h:75`) while any transport is connected, so "exactly
     one `STATE`" was guaranteed wrong. "One command, one answer" is now measured as a **delta
     against the idle cadence captured in the same session** (`count_prefix()`).
   - **A rendered DOM string cannot be asserted from a raw HTTP GET.** The U-92-3 row also asserted
     the ABSENCE of "— Select router —", which the page deliberately ships (F-57: never
     preselect) and only rewrites to `Now on: <ssid>` when the 1 Hz JSON frame lands. Split into two
     rows that prove the code paths plus the car truth they read; the rendered string stays an
     **owner-eyes** row.
2. **Two ledger rows told the owner to send `REQ_CAPS`** — `DEVICE-TESTS.md` **T10(a)** and
   **U81-1**. U81-1 is *the* headline U-81 row ("the root cause, directly"); run as written it
   reports FAIL on a car that is fixed. Both corrected to `REQ_STATE` with an explicit warning.
   `Car.__aenter__` also drains the connect-time burst so each row's window starts from silence.
3. **`tools/check-firmware-syntax.ps1` (`016d2f9`) - a runnable syntax gate.** The Arduino CI is the
   real build, so the "xtensa -fsyntax-only CLEAN" claims in earlier rounds had no *local* command
   behind them. This gate runs
   `xtensa-esp-elf-gcc -fsyntax-only` over all 7 TUs and **exits non-zero on failure**;
   **mutation-tested** (injecting `this_should_not_compile;` → exit 1, file+line named), so it is
   not a gate that always passes. **Current: PASS 7/7.** Two traps documented in the file, both of
   which produce baffling failures rather than obvious ones:
   - **Never define `-DESP32`.** `Config.h` declares `enum class BoardType : uint8_t { ESP32 };`
     and the numeric macro turns that enumerator into a numeric constant, so EVERY file including
     `Config.h` fails with "expected identifier before numeric constant".
   - **`-DWEBSOCKETS_NETWORK_TYPE=4`** (`NETWORK_ESP32`) is required; without it the WebSockets
     library defaults to W5100 and wants an `Ethernet.h` that core 3.3.12 does not ship.
   - Toolchain facts for the next session: core **3.3.12** puts the SDK headers in
     `Arduino15/packages/esp32/tools/esp32-libs/3.3.12/include/...` — NOT the classic
     `hardware/esp32/*/tools/sdk` path — and the sketchbook libraries (`U8g2`, `WebSockets`) live
     in `%USERPROFILE%\Documents\Arduino\libraries`, not in the package. `arduino-cli.exe` remains
     Device-Guard-blocked on this machine; the IDE `.exe` still not found.
4. **Gates re-run 2026-10-05, not quoted:** page-js ALL STAGES PASS (exit 0) · firmware syntax
   **7/7** · app **tsc 0 · vitest 615/615 (42 files) · prettier clean**. App needed no change.

**STILL OWED, unchanged: the car is not on this bench** (no COM port, no ARP at 192.168.1.100/.57),
so every row in U-87…U-93 stays unticked. When hardware is available the sequence is: flash
`main` 1.0.5 (USB, **115200**, `huge_app`) → `python tools/bench_u90_u92.py <ip>` (safe rows;
`--switch` also moves the radio) → the manual rows U-92-1..4 + U-90-1..5 → app U-93-1..5. **F-61:
all of the above is green and none of it is device evidence.**

**U-92 (2026-10-04) - THE OWN HOTSPOT: A DOUBLE ENTRY, A "SWITCH" THAT NEVER LEFT, AND A HIDDEN
JOIN-YOUR-OWN-BEACON TRAP.** Owner: *"webpage has double entry of esps own hotspot please remove
that too"* · *"through the webpage the router is not switching to the esp own hotspot"* ·
*"says, car switched to 4wdcar_wifi but doent switch that one, but switches to other router
fine"* · *"webpage doesnt show the router name but shows select router"* · *"check the app also
for same problem"*.

1. **THE DOUBLE ENTRY — the Reserved guards never actually guarded.** The checks compared the
   WHOLE unterminated command field (`strcasecmp(ssid, WIFI_AP_NAME)` where `ssid` still carried
   `;<password>`), so any ADD/USE/WIFICFG with a password after the name BYPASSED the refusal
   and 4WDCar_Wifi entered the saved registry — and `loadSavedRouters()`'s boot promotion then
   copied the poisoned active pair in and persisted it. Now field-bounded everywhere (ADD, USE
   saved-match + add-path, WIFICFG gets a guard it never had), the poisoned rows are PURGED at
   load (NVS rewritten, logged) and the promotion refuses the own name — an already-poisoned
   car heals itself on first boot, no owner action.
2. **THE "SWITCH" THAT NEVER LEFT.** The T-66 own-hotspot return answered `ROUTERS;USED;
   4WDCar_Wifi` and then armed NOTHING — `pendingRejoin_` keys on `hasStoredWifi_`, which the
   branch had just cleared, so `REJOIN_STEP_BEGIN` early-returned with zero radio work and the
   car stayed on the router ("switches to other router fine" — those have a stored target).
   Now the `!hasStoredWifi_` BEGIN case hands off to the existing one-call-per-pass teardown
   (disconnect → `WiFi.mode(WIFI_AP)` → startOwnAP) — the sequence the give-up path already
   proved on hardware — and the serial line says which of the two paths ran. The CLEAR path
   (T-62) inherits the same fix.
3. **THE HIDDEN TRAP.** Unguarded `WIFICFG;4WDCar_Wifi;<pass>` stored the AP as the ACTIVE pair:
   boot auto-join then tried to associate the car with its OWN beacon. `loadStoredWifi()` now
   refuses + erases it; WIFICFG refuses it outright.
4. **WEBPAGE (WebPage.h):** the router dropdown's placeholder DISPLAYS the car's current
   network (`Now on: <ssid>` — car truth, F-57: never an asserted request; the joined SSID rides
   the rebuild signature), and the page finally parses `WIFICFG;STORED;<ssid>` — it used to look
   for `WIFICFG;OK;`, a reply the car has never sent, so "Add router…" saves were never
   confirmed.
5. **APP CHECK (owner asked):** no app change needed — `routerList.ts` already dedupes by name
   ("the user must not see two rows that are one router") and `switchableRouters()` excludes the
   own AP; the app's hotspot switch sends the same `ROUTERS;USE` this round fixes, and the
   U-80 phone-switch prompt fires on the USED answer as before. The car-side fixes serve the
   app automatically.
6. **Verified:** page-js gate ALL STAGES PASS; xtensa -fsyntax-only CLEAN (WebServerComm /
   BluetoothComm / ModeManager). **Firmware 1.0.5.** Device rows U-92-1..4 (below) — all
   unticked; the car was not on the bench network this session.

**U-90 (2026-10-04) - THE CAR WAS STUCK ON ONE ROUTER, AND EVERY LATER "SWITCH" WAS A LIE.**
Owner: *"the car seems stuck on one router and doesnt connect even if on same network"* ·
*"the mode toggle doesnt allows all other comming soon modes to change through the drop down
menu, all those are dim please allow those comming soon too that changes the modes on the car
too"* · *"the menu for router change is also not working"* · *"please fix these first and push
properly".*

1. **THE ROOT CAUSE — a switch resolved as SUCCESS on the router it never left.**
   `rejoinNetwork()` arms the U-71 step machine; `REJOIN_STEP_MODE` ran `WiFi.mode(WIFI_AP_STA)`
   only `if (WiFi.getMode() != WIFI_AP_STA)`. After the car's FIRST switch the driver is already
   AP_STA (beginAlwaysOn() leaves it there), so every LATER switch skipped the driver reset —
   and `REJOIN_STEP_BEGIN` issued `WiFi.begin()` ON TOP of the still-associated STA. The IDF
   refuses config on a busy STA (the exact U-76 error: *"sta is connecting, cannot set
   config"*), and the resolve block then saw `WiFi.status() == WL_CONNECTED` — satisfied by the
   OLD association — so the switch resolved INSTANTLY as success on the OLD network: no rejoin,
   no fresh DHCP lease, and no retry (no failure was ever observed). That is the stale-lease
   *"stuck on one router, won't connect even on the same network"* report. **FIX:
   `REJOIN_STEP_MODE` is now UNCONDITIONAL** — one driver restart per explicit switch (the same
   cost the first-ever switch already paid), still one radio call per loop pass (U-71/F-24
   untouched). Re-joining the router the car is ALREADY on also works now: fresh association,
   fresh lease.
2. **COMING-SOON MODES ARE SELECTABLE (owner override of the U-79 page gate).** The U-79 gate
   existed so a tap could never park the car in a stub; the owner now wants the dropdown to
   change the car's mode. WebPage.h: the CAPS sync NEVER disables an option and keeps the
   "(coming soon)" suffix as information only; unknown tokens still earn `NACK;E=UNKNOWN_MODE`
   (F-62). Known consequence, accepted by the owner: entering a stub mode gates the drive
   controls until 4WD4M is picked again (one dropdown tap).
3. **Router dropdown honesty (F-57 applied to the router list).** The rebuild used to
   re-select whatever the select happened to show — asserting a switch request nothing
   confirmed. Now it never preselects; the neutral placeholder holds until the user acts.
4. **Verified:** page-js gate ALL STAGES PASS locally (mode list written once per real change,
   never per frame, never while open; tokens cross-checked both ways; one command one answer;
   beacon-park policy intact). Firmware syntax gate (xtensa g++ -fsyntax-only, core 3.3.12 +
   esp32-libs SDK + WebSockets) CLEAN on WebServerComm/BluetoothComm/ModeManager — note for the
   next session: on THIS machine the SDK headers live at
   `Arduino15/packages/esp32/tools/esp32-libs/3.3.12/include` (NOT the classic
   `hardware/esp32/*/tools/sdk` path), and `arduino-cli.exe` is Device-Guard-blocked. **Firmware
   1.0.4.** NOT device-verified: the car is not on this bench's network this session (probe of
   192.168.1.100/.57 dead, ARP shows no car) — the U-90 rows in DEVICE-TESTS.md need the
   owner's flash + bench run (huge_app, upload.speed=115200).

**U-87 / U-88 (2026-10-03, backfilled 2026-10-04) - RECOVERED FROM THE COMMIT BODIES
(`0fdb1d1` + `5dddd5a` + `12c930c`, `5e04df7`).** This ledger was not written while the rounds
happened; the git commits are the primary record.

1. **U-87 (`0fdb1d1`, corrected in `5dddd5a`) — the setup beacon parks only while the car is IN
   USE.** Owner asked whether the car's own hotspot staying active while it works on a router is
   necessary. First version parked the beacon whenever the STA was connected — and made the car
   UNREACHABLE on the very next bench boot (joined a router the bench was not on; no beacon, no
   address, no way in). The mistake: "the car is on a router" does not mean "YOU can reach that
   router". The corrected policy: `in use -> parked, idle -> up` — `inUse` is computed from
   `ws.connectedClients()`, a connected client being proof the user is on the router already.
   Bench-proven idle (beacon up at idle, restored the moment the last client leaves, page kept
   serving through a park — it is a beacon, not the web server); parking WHILE a client is
   connected is still unverified on hardware (the car sits on a network the bench cannot reach).
   Never calls WiFi.mode() on this path — driver restart is the documented U-75 watchdog-cause.
   T-65 narrowed, not reversed: during a switch the beacon is still up, which is when it matters.
2. **U-88 (`5e04df7`) — one WebSocket command, one answer.** Owner: *"switching between the
   router is not working and the addition and editing and deleting of the router is not properly
   built in the app"*. Found by driving the real car: `ROUTERS;DEL` produced TWO answers
   (`DELETED` then `ERROR;Not saved`) because `handleWebSocketEvent()` dispatched each line
   immediately AND queued it — every Wi-Fi command ran twice (ADD looked fine because it is
   idempotent; USE drove the radio through two full rejoins). T-1 is explicit: ONE command path,
   lines are queued and dispatched by ModeManager::run(); the immediate call was removed.
   Measured after, on the car: LIST/ADD/DEL/USE and even unknown commands answer exactly once
   (the NACK count halving from 2 to 1 is the cleanest proof). U-81 device suite 12/12 after the
   change. Gate stage 9 fails if the WS path ever calls handleCommand() again or stops queueing.
   Verified with a throwaway SSID, removed afterwards; the owner's registry untouched.


1. **THE "WEBPAGE CONTROLS STOPPED WORKING" MYSTERY — SOLVED BY THE OWNER'S OWN SERIAL LOG.**
   The U-77 breadcrumbs showed `[MODE] set 4WD4M -> ESP32(WEBSERVER)`: the page's mode dropdown
   (which offered the retired ESP_SER token) parked the car in a COMING SOON stub — and stubs gate
   drive AND `#<n>` speed. That is the "double Wi-Fi setup": an always-on web layer PLUS a dead
   webserver mode that only ever broke things. Per the ruling, ESP_SER/ESP_CLI are DELETED:
   registry rows, Mode enum values, legacy ESP_SERVER/ESP_CLIENT aliases (now → NACK;UNKNOWN_MODE),
   and both page dropdown options. NVS parity is safe: old stored mode bytes 1/2 now name CS modes,
   which applyBootState() already refuses → 4WD4M.
2. **TRANSPORT POWER POLICY (the .ino owns it; transports never gated by mode):** IDLE = Bluetooth
   AND Wi-Fi broadcast (hotspot + router per boot preference); a client on ONE transport turns the
   OTHER off (3 s grace before a re-enable, and never the transport in use); when that connection
   drops, both come back. Last-used transport persists in NVS (`botcfg/last_transport`): boot and
   every resume call `armBootAutoJoin()` ONLY when Wi-Fi was last used (last-good router, short
   budget, hotspot as the fallback of last resort); Bluetooth-last boots on hotspot + BT with NO
   router hop. `armBootAutoJoin()` moved OUT of beginAlwaysOn() for exactly this. The BT
   hard-restart watchdog (T-7b) is gated on `btUp` — a deliberate power-down is policy, not a
   wedged stack, and must never be "repaired" by re-enabling BT under an active web client.
3. **WEBPAGE (WebPage.h):** mode dropdown gated by the car's CAPS map (non-LIVE = disabled,
   "(coming soon)" — a tap can never kill drive again); speed slider no longer fights the 1 s
   status sync while dragging (explicit interaction flag + 1.5 s tail — the old
   `document.activeElement` guard misses on touch, where the range is often NOT focused during a
   drag); router dropdown = ONE place (saved routers + own hotspot from car truth) plus an
   "Add router…" entry that sends WIFICFG (stores AND switches) with an honest "Switching…" note —
   the link drop during a switch is EXPECTED (hotspot channel hop) and the page now says so and
   reconnects automatically.
4. **Verified:** compile gate (xtensa-esp32-elf-g++ -fsyntax-only, core 3.3.12 + WebSockets 2.7.2)
   green on ModeManager, WebServerComm, BluetoothComm, Oled_1_3, and the .ino as C++.

**Bench:** flash 1.0.3 (huge_app). Expect on serial: boot preference line
(`[BOOT] Last used: Bluetooth - staying on hotspot + BT` or the `[WIFICFG] boot:` join line),
`[POWER] Bluetooth off - web client active` the moment the webpage/app-over-WiFi connects, and
`[POWER] Wi-Fi off - Bluetooth client active` the moment an SPP client connects. Drive from the
webpage must NEVER die from a mode tap again — no dead mode exists to tap.

---

**U-79 (2026-10-03) - THE WEBSERVER MODE IS GONE + TRANSPORT POWER POLICY. Owner: *"remove the
webserver mode of the car totally ... so that the car doesnt have the double setup for the wifi
setup"* · *"all the networks at one place ... the default hotspot is the last option"* · *"broadcast
both bluetooth and the wifi until user selects one and turn off the other to save power"* · plus
the device evidence: `[MODE] set 4WD4M -> ESP32(WEBSERVER)` in the serial log — the webpage's mode
dropdown had parked the car in the retired CS stub, which gates ALL drive commands ("COMING SOON").
That is the "webpage controls stopped working" report, root-caused.** Version -> 1.0.3.

1. **ESP_SER / ESP_CLI REMOVED FROM THE REGISTRY (owner ruling, ask_user answered "Remove tokens
   fully").** `Mode::SERVER`/`Mode::CLIENT` deleted from the enum, the registry, and the legacy
   `ESP_SERVER`/`ESP_CLIENT` alias branches in setModeFromString — sending one now earns
   `NACK;E=UNKNOWN_MODE` instead of booting a dead mode. NVS parity is free: old stored indices 1/2
   now name CS modes, which applyBootState() already refuses (donor-era NVS cannot restore them).
   The mode dropdown on the page lost both options. **Old cars keep these modes — this ruling is
   4WD4M-only; the app stays caps-driven (parseCapsBody) so nothing breaks fleet-wide.**
2. **TRANSPORT POWER POLICY (owner ruling, verbatim):** idle (no client anywhere) = Bluetooth AND
   Wi-Fi both broadcast until the user picks one; a client on ONE transport switches the OTHER off
   (`BluetoothComm::stop()` added — SerialBT.end(); begin() is idempotent and brings it back; the
   web layer uses the existing stop()/beginAlwaysOn()); when that connection goes away both return
   after a 3 s grace. The transport WITH a client is never touched. The BT hard-restart watchdog is
   gated on `btUp` — a policy-induced BT absence can never trigger the F-60 restart.
3. **BOOT/RESUME PREFER THE LAST USED TRANSPORT** (NVS `botcfg/last_transport`, new):
   Wi-Fi last -> armBootAutoJoin() runs (last-good router, short budget, own hotspot as the
   fallback of last resort — U-71 semantics kept); Bluetooth last -> the router hop is SKIPPED
   (`[BOOT] Last used: Bluetooth`). armBootAutoJoin() therefore no longer fires unconditionally
   from beginAlwaysOn() — beginAlwaysOn() is now also the power-policy RESUME path and the .ino
   decides per last-used transport.
4. **WEBPAGE (WebPage.h):** mode dropdown is gated by the car's CAPS map (non-LIVE -> disabled +
   "(coming soon)", so a tap can never kill drive again); the speed slider no longer fights the
   1 s status sync while dragging (explicit drag flag + 1.5 s tail — the old focus guard misses on
   touch, where the range is often not document.activeElement mid-drag: the "speed control not
   working properly" report); the router dropdown is ONE place for every network — saved routers
   (REQ_NETW) + an "Add router…" entry that sends WIFICFG (stores AND switches); switching shows an
   honest "Switching… … this page reconnects when this phone joins <ssid>" note (the AP link drop
   during a channel hop is EXPECTED, U-74).
5. Compile gate: all five touched sources pass xtensa-esp32-elf-g++ -fsyntax-only (core 3.3.12 +
   WebSockets 2.7.2; NetworkClientSecure is the 3.3.12 header dir WebSockets' WiFiClientSecure.h
   include resolves through). CI runs on push.

---

**U-77 (2026-10-03) - THE WATCHDOG REBOOT CLASS IS CLOSED. Owner log: TWO MORE `reset reason: 6
(TASK WATCHDOG, loop hung >5s)` reboots, ~92-97 s after boot, WITH the U-75 breadcrumbs exhausted.**
Both hangs left NO radio breadcrumb, so the blocker was not a radio call - it was the two remaining
UN-BOUNDED blocking paths in loop(), both client-write paths, both now proven against the installed
sources (WebSockets 2.7.2 + arduino-esp32 3.3.12):

1. **WS stalled-client broadcast** (the ~29 s gap in the owner log is the TCP buffer-fill time):
   `broadcastStatus()` runs EVERY SECOND to every client; a phone that stalls (screen off, app
   killed without a close, or its link orphaned by the AP channel hop mid-switch - U-74) stops
   draining TCP. `WebSockets::write()` retries until `WEBSOCKETS_TCP_TIMEOUT` (5 s) of no progress
   and each inner `tcp->write()` itself blocks on the full lwIP buffer (core default socket send
   timeout 3 s) - one stalled client = loop() blocked 5-8 s = WDT = reboot, no breadcrumb.
   **Fix: `ws.enableHeartbeat(3000, 1000, 2)`** in `setupWebSocket()` (supported by 2.7.2 server
   side, new clients inherit it): a staller is pinged and dropped in ~8-9 s, long before its
   buffers can wedge a write.
2. **HTTP root-page send had NO total bound**: `server.send_P(PAGE_HTML)` (~18 kB) to a
   slow-but-moving client (weak signal right after a network switch - the owner's "restarted as
   soon as I tried to access the webpage") blocked loop() chunk after chunk past 5 s even with
   every single write under the 3 s default. **Fix: `serveRootBounded()`** - exact Content-Length
   headers, then the body in 512 B chunks with a 500 ms per-write socket cap
   (`armHttpGuard()`, applied to EVERY HTTP handler) and a 3 s TOTAL budget; a hopeless client
   gets a truncated page (it is dead anyway) instead of a rebooted car.
3. **mDNS could NEVER advertise - inverted condition.** `if (MDNS.begin(...) != 0)` treats
   SUCCESS (non-zero) as failure: printed "[MDNS] unavailable" and returned before addService(),
   while a REAL failure fell through to addService(). Now `== 0` = failure, honestly logged once.
4. **Boot auto-join was silently dead.** `loadSavedRouters()` wiped `lastGoodSsid_` right after
   `loadStoredWifi()` had loaded it from NVS and never re-read - `armBootAutoJoin()` always saw an
   empty last-good router (owner log boot 1: no `[WIFICFG] boot:` line). Registry load no longer
   touches last-good state; `loadStoredWifi()` is its single owner (and now ONE read-only NVS open
   instead of two, one needlessly writable).
5. **Retry-branch radio calls got breadcrumbs** (`[RADIO] retry/disconnect` / `retry/begin`) - they
   were the last silent radio calls; a hang there would have been another breadcrumb-less reboot.
6. **stopMdns() was dead code** - now called from stop(). Duplicate `apRunning_ = true;` in
   startOwnAP() removed. Version -> 1.0.2, Readme synced (was stale at 1.0.0).

**Verification:** all edited sources pass the xtensa-esp32-elf-g++ `-fsyntax-only` gate against
core 3.3.12 + WebSockets 2.7.2 (adapted from the build/v2 compile_commands.json). CI compile gate
runs on push. **Bench:** flash `main` (huge_app), then repeat the owner's repro: switch car to home
router from the app, phone follows onto the router, open the webpage. Expect: NO reboot, and on a
radio event the `[RADIO] ...` breadcrumbs name every step. If a WDT ever fires again the last
breadcrumb names the step (U-75 rule now covers 100% of radio calls). Stale cross-machine
`build/` artifacts removed from the working tree (gitignored anyway).

---

**U-71 (2026-10-02) - FINISHES THE INTERRUPTED U-70 WORK. Owner: *"i used the other router switch
thing but the car restarted as soon as i tried to access the webpage"* · *"save the last connected
router configs for the power restart too if that router is in the range to connect if not goes back
to esps own hottspot"* · *"remember old setting in the database"*.** The owner asked whether the
previous round had been interrupted: **yes** - U-70 deferred only `WiFi.begin()`, and the boot
auto-join and the last-good persistence had not been started. All of it is now done.

**1. THE RESTART - THE STRONGEST EXPLANATION YET, AND IT IS A REAL DEFECT.** U-70 deferred only the
`begin()`. **`WiFi.mode()` and `softAP()` were still running synchronously inside `rejoinNetwork()`,
which is called from `ModeManager::run()` INSIDE `loop()`.** On ESP32 `WiFi.mode()` RESTARTS the WiFi
driver; a driver restart plus `softAP()` plus `begin()` can occupy `loop()` long enough to trip the
**5 s task watchdog - and the WDT's action IS a reboot (F-24)**. That fits the report exactly: the
switch churns the radio in the command path, and the very next thing to arrive is an HTTP request
served by that same driver. `rejoinNetwork()` now does **NO RADIO WORK AT ALL** - it only arms a
state machine that `handle()` advances with **one radio call per loop pass**
(`REJOIN_STEP_MODE` -> `REJOIN_STEP_AP` -> `REJOIN_STEP_BEGIN`). `loop()` returns between steps, the
WDT is fed, and the web server can **answer** a request in between - which is exactly the window that
used to be missing. U-70's `staBeginPending_` flag is **removed**, not layered on top of: U-71
supersedes it. *I am not claiming the restart is fixed until U-70-4 has been run - but this is now a
mechanism with an explanation rather than a mystery, and the reset-reason line will confirm or refute
it.*

**2. THE CAR REMEMBERS THE ROUTER THAT WORKED, NOT THE ONE IT WAS ASKED FOR.** `storedSsid_` is
written to NVS the moment `ROUTERS;USE` stores the pair - **before any association is attempted** - so
a FAILED switch overwrote a working router with a broken one, and a boot that trusted it would
dutifully retry the router that had just failed. New `lastGoodSsid_` / `lastGoodPass_` (NVS
`last_ok_ssid` / `last_ok_pass`) are written **only from the connect-success path**. *"What we tried"*
and *"what worked"* are now separate facts, and only the second survives a power cycle.

**3. BOOT AUTO-JOIN, DELIBERATELY BOUNDED. `armBootAutoJoin()` runs at the end of
`beginAlwaysOn()`.** ⚠ **THIS REVERSES R-16/T-64** ("the own AP is THE default network; a stored
router is joined ONLY on explicit request") - the owner has now overridden that ruling, and it is
recorded here rather than quietly done. **The reason the rule existed still stands, so the override is
bounded:** the boot attempt gets `BOOT_STA_TIMEOUT_MS` (6 s) and `BOOT_STA_MAX_RETRIES` (**1**), then
settles AP-only immediately. The full retry budget stays reserved for an **explicit** user switch,
where the user is watching. An unreachable router therefore cannot keep the car off its own hotspot
for more than a few seconds - and the hotspot is the **only way in** when the router is absent. A
router that has never once connected is never auto-joined, so the boot window is never spent on
something already known to fail. The budget is per-attempt state (`staTimeoutMs_`, `staRetryCap_`) and
restores to normal the instant an association succeeds.

Unchanged: the saved-router registry, the protocol, safe-stop, the scan, and the AP itself.

**Ship evidence:** car `7ba6152` - **Arduino CI ✓ `37035295780`**, 69,764 B SRAM (21%, +112 B).
App side of the same report is `d495839` (remembering CONFIRMED router changes in AsyncStorage and the
Supabase `car_profiles` row - a real gap U-68 introduced, where the user's most common action,
switching a router, was never saved at all). Device rows: `genumsolutions-app/mobile/TESTING.md`
**U-71-1..3**. **NOT flashed yet.**
**U-70 (2026-10-02) - THE CAR ANNOUNCED THE ROUTER IT WAS ASKED TO JOIN, NOT THE ONE IT WAS ON.**
Owner bench: *"the car is displaying the new router in its oled but not switching actually to that new
router network and the car still show the website only on its own esp hotspot. and doesnt show to new
router"* + *"the car restarts when the app directs to cars webpage"*.

**1. THE CAR LIED, AND THAT IS WHY THE SECOND SYMPTOM WAS INVISIBLE.** `storedSsid()` is written the
moment `ROUTERS;USE` stores the pair - **before any association is attempted** - and both the STATE
line's `SSID=` and the JSON `ssid` used it. A FAILED switch therefore announced the new router while
the car sat on its own hotspot, and nothing could tell the difference. **The owner's own observation
was the lie, not the success it looked like.** The OLED itself was already truthful (it reads
`WiFi.SSID()` through `extraInfo`); it was the **app-facing** fields that lied, which is exactly why
the two disagreed. Now `SSID=` / `ssid` is the new `joinedSsid()` (joined STA SSID, else the own
broadcast id, else empty), and the requested target ships **separately** as `WANT=` / `"want"` so a
switch in progress is visible instead of indistinguishable from a finished one. *"Which router did
the user ask for"* and *"which router are we on"* are different questions and must stop sharing one
field.

**2. THE JOIN STARTED IN THE SAME TICK AS A MODE CHANGE.** `rejoinNetwork()` did
`mode(WIFI_AP_STA)` -> `softAP()` -> `WiFi.begin()` inside one call. On ESP32 **`WiFi.mode()`
RESTARTS the WiFi driver**, so a `softAP()` and a `WiFi.begin()` issued in the same tick race a
driver that is still initialising: the STA fails to associate while the softAP still comes up —
precisely *"accepted and displayed the new router, never actually joined it, still reachable only on
its own hotspot"*. Fixed conservatively: `WiFi.mode()` is now called **only when the radio is not
already `AP_STA`** (so switching routers — the common case — never restarts the driver at all), and
`WiFi.begin()` is handed to `handle()` on the **next** pass via `staBeginPending_` /
`beginStaIfArmed()`, so the driver has a tick to settle. Retry budget, the AP, safe-stop and the
protocol are untouched.

**3. THE RESTART IS INSTRUMENTED, NOT FIXED.** *"the car restarts when the app directs to cars
webpage"* could not be acted on: a watchdog abort, a stack smash, a brownout and an intentional
reset all look identical from outside, and it cannot be reproduced on the bench. Rather than guess,
`setup()` now prints **`esp_reset_reason()` on every boot** and shouts when the reason was not
power-on/external/deep-sleep. **That is what turns the next occurrence into an answer: `TASK
WATCHDOG` means `loop()` blocked, `PANIC` means a crash, `BROWNOUT` means undervoltage.** Read the
serial line at 115200 and paste it — do not record the restart as fixed or as not-reproducible until
that line exists. (`TESTING.md` U-70-4 in the app repo is the row for it.)

**Ship evidence:** car `13a406f`, **Arduino CI ✓ `37033364835`**, 69,772 B SRAM (21%, −8 B). App side
of the same report is `b21fed5` (the Bluetooth scan list — bounded scrolling window, hidden whenever
any link is up). Device rows: `genumsolutions-app/mobile/TESTING.md` **U-70-1..4**; **U-70-1/2 need
this flash, U-70-3 is app-only.** NOT flashed yet.
**✅ U-68 PHASE 3 SHIPPED 2026-10-02 — THE CAR'S NETWORK LAYER REBUILT. Arduino CI ✓ `37012141471`
(1,748,602 B = 55% of the 3 MB `huge_app`, 69,772 B SRAM = 21%). NOT FLASHED YET.** Three defects
the app could not work around, all on the connection/provisioning path:

1. **`ROUTERS;SCAN` DID NOT EXIST — D3.** The app has sent it since the Connections-Hub round and
   the car answered `ROUTERS;ERROR;Syntax` every time; the app then discarded that answer, so the
   failure was invisible. **There was no "networks near the car" list anywhere in the system** —
   the owner's *"the selecting of the network is not proper"*. Now implemented and delivered BOTH
   ways so one app parser serves both links (F-66): the SPP line `SCAN;<ssid>,<rssi>,<1|0>;…`
   (or `SCAN;NONE`) and `"scan":[{"ssid":…,"rssi":…,"open":…}]` in the status JSON. An empty
   array / `SCAN;NONE` until a scan has run, so the app can tell *not scanned yet* from *scanned,
   found nothing*. Names only — a scan can never reveal a password (W-14).
   **ASYNC ON PURPOSE:** `WiFi.scanNetworks()` blocks for seconds; from the command path it would
   starve `loop()` and trip the task watchdog (F-24), **rebooting the car** — exactly what F-60
   exists to stop. So the command only ARMS the scan (`startScan`), `handle()` runs it after a
   settle delay (`runScanIfDue`) and collects it when the radio finishes (`collectScanIfDone`),
   keeping the strongest reading per SSID and broadcasting on both transports. One pass at a time,
   bounded by `SCAN_TIMEOUT_MS` so a radio that never finishes reports honestly instead of leaving
   the app waiting. Note the caveat: while the STA is ASSOCIATED the radio is busy and the channel
   follows the router, so **strength is a hint only** — the saved-router list stays the source of
   truth for what the car will actually join.
2. **The router reply was TRUNCATED at 63 characters — D7.** `ModeManager::pendingReply_` was 64
   bytes while the command hook builds its reply in 160, so `ROUTERS;<ownAP>;<ssid>…` was cut off
   mid-list as soon as two or three routers were saved. Nobody noticed because the app threw the
   reply away (that was D2). Now the answer is consumed, a truncated one would be a *wrong* one, so
   the buffer matches the largest reply the hook can build and the STATE builder's buffer was grown
   (`buf[248]` → `buf[448]`) to carry a full `REPLY=` in one line.
3. **A credential rode every acknowledgement — F-68.** Each WebSocket command was answered
   `{"ok":true,"cmd":"<the full line it was sent>"}`, so `ROUTERS;ADD;Home;hunter2` came straight
   back to the phone and would land in any WebSocket proxy log. The HTTP route did the same **and
   broadcast it to every WS client**. New `redactForAck()` confirms the request by operation + SSID
   only. *An acknowledgement confirms a request; it must never replay it.*

**Bench:** the app half is U-68 (`genumsolutions-app` — see its `TRACKS/NEXT-SESSION.md`); device
rows **`genumsolutions-app/mobile/TESTING.md` U-68-4** (nearby-network selection) are the ones that
**cannot pass until this is flashed**, because no earlier binary has `ROUTERS;SCAN`. The
flash-verify car rows to add alongside: T24–T26 (U-67, still open) and T21–T23.

**Lesson recorded (it cost two CI runs):** the phase-3 commit shipped with `handleRouterCommand()`
closing *inside* its if-chain — a follow-up edit had replaced the SCAN branch's body along with the
anchor it matched — so everything after it parsed as declarations. The **Arduino CI compile gate
is the only thing that can catch a brace-level mistake in firmware**; treat a red compile as a real
defect, never as CI noise. (Second failure was `strcasecmp` on a `char[33]`'s `.c_str()`.)
# NEXT-SESSION — Genum_4WD4M_CAR

**⚠️ U-67 (2026-10-02, owner bench) — THE ROUTER-SWITCH RESET IS FIXED, AND IT WAS NOT
`cd3158f`. ROOT CAUSE: the BT watchdog, not `ROUTERS;USE`.** Owner, verbatim: "the 4wd4m car is
resetting when the wifi router is switched from the app and the car is not always switching
properly… the car doesnt go switch the router in one go propely." Two separate firmware bugs, both
real, neither the one the ledgers predicted:

1. **THE RESET — `BluetoothComm::restart()` + the .ino watchdog.** `restart()` ended a failed
   re-init with **`ESP.restart()`**, and the .ino armed the watchdog on **"no BT client"** rather
   than on "a client that dropped". Switching the car to the home router takes the phone OFF the
   car's BT, so `hasClient()` goes false and STAYS false; after `BT_HARD_RESTART_MS` (120 s) the
   watchdog fired, tore bluedroid down, and the escalation rebooted the car **mid-handoff**. It came
   back on its own AP and the switch never completed. Worse than a switch bug: the throttle is
   60 s, so an **unattended car parked on the router rebooted on a loop**. Fixed: no
   `ESP.restart()` from the BT path ever again (a failed re-init is reported and the car KEEPS
   RUNNING on AP/router — it stays reachable and driveable), and the watchdog arms only on a link
   that was CONNECTED and then dropped (`btEverConnected`): "never had a client since boot" is
   idle, not wedged. **FAILSAFES F-60: a transport failure must never take down the whole car.**
   *Flashing the previously-committed firmware does NOT fix this — `cd3158f` never touched it.*
2. **"NOT ALWAYS SWITCHING / DOESN'T DO IT IN ONE GO" — `WebServerComm::handle()`.** The 8 s
   `WIFI_STA_TIMEOUT_MS` was treated as the WHOLE switch: on timeout it cleared `pendingRejoin_`
   and set `startedAsAP`, which made the retry branch (`else if (pendingRejoin_ || !startedAsAP &&
   …)`) **unreachable** — so `STA_MAX_RETRIES` never ran and ONE flaky association failed the
   switch **permanently**, the car silently left on its own AP. Fixed: the timeout hands OFF to
   that existing capped budget (up to 6 more attempts) and only the spent budget settles AP-only
   (the cap still holds, so an unreachable router cannot blank the softAP beacon forever —
   FIN-40/D2 intact). Each retry also re-arms `connectingPending_` with its own 8 s window: that
   branch is an `else if` behind `if (connectingPending_)`, so without re-arming, a successful
   retry was **invisible** (no `updateExtraInfo`/`sendState` — the app sat on "getting an address"
   while the car was already on the router) and `pendingRejoin_` stayed set, so the branch kept
   yanking a WORKING association down every 10 s.

**Bench:** app side is U-67 (one standard switch confirm — `genumsolutions-app` `bfe1cda`); device
rows **U-67-1..4** in `genumsolutions-app/mobile/TESTING.md`, of which **U-67-3/4 (the reset and
the one-go switch) are the firmware rows and CANNOT pass until this is flashed.** Run them FIRST —
if the flash did not take they reproduce and every later router row is meaningless. The car-side
rows to add alongside: the two above plus the existing **T21–T23**.
**SHIPPED (owner go given 2026-10-02 for the firmware push):** `26d41d5` (fix) · `3f080c2`
(TRACKS), pushed `030726f..3f080c2`; **Arduino CI ✓ `37001533166`** — 1,746,294 B flash (**55%**
of the 3 MB `huge_app` partition) · 69,604 B SRAM global (21%). App half: `bfe1cda`/`7919d5c`,
CI ✓ `37000785055`, OTA ✓ `37000785239` published (group `75dc154a`), live manifest =
`Short update (7919d5c…)`. **Both halves are now on the board's owners' side: FLASH the car, and
close+reopen the app ×2.**

**Resume point (2026-10-01→02, integration session with the app — firmware changed, NOT yet flashed):**

Three firmware fixes landed for the live-integration round, all CI green, none on the board:

1. **`64c2781` — the web page no longer force-sets `MODE_ESP_SERVER` on load.** Opening the
   car's page used to `setMode()` (persisted, F-31) — a browser visit silently re-moded the
   car. The onload handler now only pre-selects the dropdown; the mode is sent only on a
   user change. F-57 (`2478735`) keeps the dropdown synced to the car's real mode and blocks
   empty-option sends.
2. **`cd3158f` — `ROUTERS;USE` adds a new router on-the-fly and switches to it** without
   rebooting the car (an unknown router used to reset it).
3. **OLED v1.0.1 directive complete (`0fc7b89` → `602e42a` → `dfda326`, CI ✓ `36909403120`):**
   BT label + `Type: SPP`, real joined SSID (parsed from extraInfo — the link-type string is
   no longer printed as the SSID), live `ipBuf` instead of the hardcoded `192.0.0.0`, UTF-8
   em-dashes removed (u8g2 rendered garbage), row buffers widened 33→40 with a width trim.
   `602e42a` did not compile (undeclared `deviceName`) — caught by CI, repaired in `dfda326`;
   owner ruling Q2=B recorded: the APP banner shows the controller name, firmware has no
   peer-name channel.

**Bench:** Round 2 rows **T21–T23** in `TRACKS/DEVICE-TESTS.md` — flash `main` ONCE (same
flash covers Round 1's pending "flash v1.0.1 + OLED line" row), then run T21–T23 + the still
untranscribed T1–T10, then the ⭐ MASTER RUN safety rows A/B. App side of this session:
`genumsolutions-app` `edb3fd7`→`f3223ce` (method switching while connected etc.), device rows
U-60-1..5 there.

**⚠ 2026-10-02 (evening) — THE OWNER'S BENCH SESSION CONFIRMED THE UNFLASHED-BINARY SYMPTOM
LIVE:** trying to switch the car to the home router from the app "resets the car and it never
reaches the new router settings" — exactly `cd3158f`'s bug (`ROUTERS;USE` on an unknown
router rebooting the flashed binary; fixed on `main`, CI green, NOT on the board). The app
side of that report is separately queued in `genumsolutions-app/TRACKS/NEXT-SESSION.md`
(handoff card needs a real confirm button + a link-loss phase). **Flash `main` before any
T21–T23 / home-router attempt — on the current board those rows cannot pass by design.**

**Previous resume point (2026-09-28, after the Connections Hub firmware round — board-id broadcast landed):**

0. **DONE `8a25dbb`: the car broadcasts its stable board id.** `WebServerComm::boardIdHex()`
   (`ESP.getEfuseMac() & 0xFFFFFF`, 6 uppercase hex) ships inside STATUS JSON as `"id":"%s"` and on
   every STATE line as `;ID=<6hex>` (ModeManager `sendStateOn`; `wifiKeys[160]` / `buf[248]`).
   Purpose: the app's per-device profile key `fw:<boardId>` — stable no matter which SSID/AP the
   car is parked on. See the app-side Connections Hub round (`feb406f` + earlier). **Requires the
   owner to flash + rerun the app device round** (old binary has no `;ID=` → app falls back to
   `wifi:<ssid>` keys + migration). Compile check pending on the bench (Arduino IDE, huge_app).

1. ~~Phase 0~~ DONE (repo on GitHub, CI green) · ~~Phase 1 v2 re-architecture~~ DONE
   (commit `a48705e`, run `36377627226` GREEN: 1,742,098 B / 55%).
2. ~~Phase 2 (app integration)~~ DONE — app `8d54fed` pushed, CI + **OTA published**
   (208/208 tests, tsc clean): OWN_AP_NAMES registry + isOwnApName, DEFAULT_AP_IP
   192.168.245.1 / DEFAULT_WS_URL across hub/Tools/Remote/RouterPanel, 3 new v2 test pins.
3. **U-49 DONE AND SHIPPED.** App `5c99357` = **v3.2.6/59 → v3.2.7/60** (`site.ts` synced), pushed to
   `main`; CI `36436791234` green; **Release `36436791172` green → `genum-solutions-3.2.7.apk`
   (42.9 MB) + `latest` + `release.json` published**; `ota-only` skipped by its release-guard.
   F-30's manifest was verified **in the published APK** with `aapt` (all four WiFi permissions,
   `NEARBY_WIFI_DEVICES` + `neverForLocation`, `usesCleartextTraffic=true`). App now has a
   single-active-transport `LinkManager` + Control Panel transport picker (BT Classic / BLE /
   Car-WiFi WS / Home-WiFi WS), Wi-Fi diagnostics, and the fixed `caps` string parser.
   Gates: **tsc 0 + 243/243 tests**.
4. **NEXT (owner): install 3.2.7 and run the device round.** It is a **new APK, not an OTA** — an OTA
   cannot deliver a manifest change. U-49-0 first (join `4WDCar_Wifi`, open `http://192.168.245.1`
   in the phone browser) to prove the car is healthy, then U-49-2..6 + T0–T10 below.
5. **NEXT (owner at the bench): device round 1** (`TRACKS/DEVICE-TESTS.md`). Flash `fc5cf96` via USB
   (115200, **huge_app**). Donor NVS may hold mode=SERVER — expect the one-time
   `[BOOT] ... retired (CS) in v2 - keeping 4WD4M` line. T3's app half is unblocked now that 3.2.7
   is published.
6. Row **T7 is stale** — there is no ESTOP in this firmware; it now tests `S`/`SPD0` + link-loss
   safe-stop instead (`TRACKS/FAILSAFE.md` item 11).
7. Round PASS → fleet port planning (Phase 4 of the master plan).

Open owner decisions: O-3 (remote pairing update — needed before any remote-first row),
O-4 (e-stop UI — **now also blocked on firmware**, which has no ESTOP handler).

**Wi-Fi is expected to work on 3.2.7, but it is UNPROVEN until the owner runs the round.**
