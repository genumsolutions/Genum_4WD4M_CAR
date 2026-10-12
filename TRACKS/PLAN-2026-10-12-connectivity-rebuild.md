# PLAN 2026-10-12 — Remote "Connectivity" rebuild + car-on-router reachability (CAR half)

Status: **IN PROGRESS — phased.** This file is a *resume point*: if a session dies,
read the "RESUME HERE" line, then the matching block in
`genumsolutions-app\TRACKS\PLAN-2026-10-12-connectivity-rebuild.md`.

Owner request (2026-10-12), verbatim intent:
1. Bug: *"the car now goes into one of the other router and doesnt display its own
   homepage and doesnt connect to the app too"*.
2. Move ALL connection things (Bluetooth + WiFi) into the Remote screen, in one
   panel renamed from "WiFi & Router" → **Connectivity**.
3. Remove the app's OLED mirror (every usage) and put telemetry (link / uptime /
   heap / signal) around the drive controls.
4. Remove the Remote screen's **Select** button; add per-link disconnect (BT + WiFi).
5. Strip the Control Panel's top telemetry + connection sections.
6. Parallel Bluetooth + WiFi with a power-saving toggle (**default OFF**).
7. *"dont change other thing that i havent asked to you"* — strict scope.

Owner answers to the 4 decisions:
- OLED removal: remove the `OledDisplay` mirror component + **every usage** (robocar
  deck, hidden-mode, drone, smart-home columns); nothing else in those decks changes.
- Car-on-router: **auto-discover on the phone's network.**
- Parallel: **app toggle (default off) + car NVS flag via a command.**
- Delivery: **phased**, gated, committed **and pushed** per phase; owner device-tests
  only at the end.

## Phases (car-side work only; app side is the mirrored doc)
- [x] **Phase 1 — car-on-router reachability** (DONE, U-100, source-only). Park is now opt-in
      (NVS `botcfg/ap_psave`, default OFF) so `4WDCar_Wifi` is always reachable; command
      `WIFICFG;APSAVE;ON|OFF`; two new gate rules in `tools/check-webpage-js.mjs` section 8.
      See "Phase 1" below.
- [x] **Phase 4 — parallel transports** (DONE, U-100 Phase 4). `Genum_4WD4M_CAR.ino` U-79 loop gained
      a parallel branch; NVS `botcfg/allow_parallel` read in `loadStoredWifi()` + `allowParallel()`
      getter; command `WIFICFG;PARALLEL;ON|OFF`. See "Phase 4" below.
- [ ] **Phase 5 — final sweep** (`FW_VERSION` bump, ledgers, `RESUME-ME.md`).

## RESUME HERE
> **Phase 4 car half DONE** (U-100 Phase 4). Firmware: NVS `botcfg/allow_parallel` (default OFF, read
> in `loadStoredWifi()`), `WebServerComm::allowParallel()`, command `WIFICFG;PARALLEL;ON|OFF` (rides
> the single system-command hook, persisted, answered over BT/web), and a parallel branch in the U-79
> loop that skips both teardown branches and re-raises any transport that is down. Two new
> `check-webpage-js.mjs` section-8 rules (switch defaults OFF; getter exists). New `bench_u90_u92.py`
> rows P4-1..P4-3 + a DEVICE-TESTS round. Gates green. Next: **Phase 5** (FW_VERSION bump, ledgers).

---

## Phase 1 — car auto-joins a saved router → own page + app unreachable

### Root cause (source-confirmed)
ESP32 AP_STA is single-radio: when the car joins a router the softAP **follows the
router's channel**, so a phone on `4WDCar_Wifi` (192.168.245.1) loses the link
(`genumsolutions-app .../connection/discovery.ts:12-19`). U-98 makes the car
auto-join the last-switched router at boot (`Genum_4WD4M_CAR.ino:289-300`), so this
now happens *unattended at power-up*. The app then keeps dialling the stale AP
address. U-87/U-86's `updateApPowerPolicy()` already refuses to park the beacon while
nobody is on the router side — but it does not distinguish "associated, idle" from
"reachable and in use" strongly enough, and the give-up path must restore the beacon.

### Firmware change
- `WebServerComm.cpp:1258` `updateApPowerPolicy()`:
  - Keep the required safety form (gate-pinned): `staUp`, `inUse` containing BOTH
    `ws.connectedClients() > 0` and `WiFi.softAPgetStationNum() == 0`, `AP_PARK_GRACE_MS`,
    park via `WiFi.softAPdisconnect(false)` only (NEVER `WiFi.mode()`), `!staUp` branch
    restores via `startOwnAP()`, `GIVEUP_AP` clears `apParked_` + `startOwnAP()`.
  - Behavioural refinement to land: never count the own-hotspot association as
    "reachable"; the beacon must stay up whenever no router-side client is present,
    and must be restored the moment the last router-side client leaves (already the
    intent — assert it explicitly).
- `tools/check-webpage-js.mjs:599-813`: update the regexes for any changed expression
  shape so the gate still fails on a real regression (do NOT weaken it).
- `tools/mutate-gates.mjs`: mutants for the changed rule must still be caught.

### App change (mirrored doc)
- Auto-discovery on the phone's current network (NetInfo network change →
  `findCarForUser`: remembered `lastRouterIp` first, then bounded /24 sweep).

### Gates to run (car)
```
powershell -File tools\check-firmware-syntax.ps1        # timeout >= 240000 ms, 10 TUs
node tools\check-relay-contract.mjs
node tools\check-webpage-js.mjs
node tools\mutate-gates.mjs                              # ALONE, on a CLEAN tree
```

---

## Phase 4 — parallel Bluetooth + WiFi (power-saving, default OFF)

- New NVS key `allow_parallel` (namespace `botcfg`) read at boot.
- New command (WebSocket/BT, single command path via `ModeManager::handleCommand`)
  to set the flag and persist it.
- `Genum_4WD4M_CAR.ino:393-501` (U-79 transport power arbitration): when the flag is
  ON, skip the "a client on ONE transport switches the OTHER off" branches; when OFF
  (default) behaviour is unchanged. `isConnectedNow()` (`ModeManager.cpp:756`) already
  ORs all transports, so the 2 s failsafe stays correct.
- Gate: `check-relay-contract.mjs` / `mutate-gates.mjs` updated if the loop shape is
  asserted; `bench_u90_u92.py` + DEVICE-TESTS rows added.

---

## Constraints / invariants (do not break)
- `.3.x`/`.4.x` addresses: `192.168.4.x` is FORBIDDEN fleet-wide (FIN-48). Car AP is
  `4WDCar_Wifi` @ `192.168.245.1` (`Config.h:156`).
- The U-92 own-AP guards in `loadStoredWifi()`/`loadSavedRouters()` stay.
- `startOwnAP()` is the only wifi-config op (U-99); `beginAlwaysOn()` sets
  `WIFI_AP_STA` first.
- F-61: green gates are NOT device evidence. Nothing here is device-verified until
  the owner flashes and runs `TRACKS/DEVICE-TESTS.md`.

## Key file map
- `WebServerComm.cpp`: `updateApPowerPolicy()` :1258, `giveUpStepMachine()` :1366,
  `staBusy()` :1439, `startOwnAP()` ~:1034, `loadStoredWifi()` :160,
  `loadSavedRouters()` :250.
- `Genum_4WD4M_CAR.ino`: transport power policy :56-93, setup comm wiring :271-300,
  U-79 arbitration loop :393-501.
- `ModeManager.cpp`: `isConnectedNow()` :756, `handleCommand()` (single path),
  `currentLink()` :129, `pushLinkInfo()` :153.
- `Config.h`: `WIFI_AP_NAME` :156, `FW_VERSION`, `extern transportPowerMux`.
