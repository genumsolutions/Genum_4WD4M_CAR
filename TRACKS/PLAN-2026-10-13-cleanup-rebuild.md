# PLAN 2026-10-13 — Field-fix + cleanup round (CAR half, master)

Status: **PHASE 0 DONE (notes) — phases 1..9 pending.** This file is the master
resume point. The app half is mirrored in
`genumsolutions-app\TRACKS\PLAN-2026-10-13-cleanup-rebuild.md`; the shared
records/timestamps reference is `TRACKS/RECORDS-INVENTORY.md`; the cross-repo
round log stays in the root `RESUME-HERE.md`.

Owner request (2026-10-13), verbatim intent:
1. Telemetry/status layout wrong across app + car; BT-connect car restart not surfaced in the app.
2. OLED last-two-lines rules (alternate BT/Wi-Fi when both are on; single method = always that one;
   Wi-Fi line alternates network name ⇄ IP ~1 s).
3. Wi-Fi connect freeze; hotspot SSID must read exactly **`4WD4MCar_WiFi`**; duplicate auto-join settings.
4. After a car restart the app must prompt **reconnect / leave / cancel** and carry the restart info on
   the next connect.
5. Remote screen: remove the top "Not connected/Connected" line + disconnect-button space, remove
   extra top padding; status only right of the Back button; separate **Wi-Fi** and **Bluetooth**
   disconnect buttons on the line below Back.
6. **"make the car save those error report or monitor or bugs and get through the database or app"** —
   the car must catch and ship its own crash/monitor data. Owner will tether the car for testing.
7. **Clean up old records** not needed from the car + user profile; start fresh; **keep proper
   timestamps** so cleaning DB/app/car is easy.
8. **Divide the work into phases and push properly after each fix.**
9. **Remove unused/unnecessary donor ("wireless car") leftovers from the 4WD4M car** + the name change,
   **nothing else**.

Owner decisions (answers to the 2026-10-13 questions):
- **PANIC capture** — car saves error/bug reports itself, shipped via DB/app. Car is tetherable.
- **SSID** — exact **`4WD4MCar_WiFi`** (supersedes `4WD4MCar_WiFi`; matching stays case-insensitive).
- **Auto-join** — **keep two** toggles, clearer labels (do NOT merge).
- **Donor** — firmware-only: unused donor leftovers + the name change. No app/DB/donor-repo work.
- **DB fresh start** — everything car-related **for this car + the owner's account** (not the shared
  `device_models` registry, not other fleet cars).
- **Disconnect buttons** — **only** under the Back button on the Remote screen.
- **NVS timestamps** — the app sends a real Unix epoch on connect; the car stamps records with it.
- **Toolchain** — use the installed Arduino IDE/`arduino-cli` to compile; **owner flashes later**.

## Bench evidence captured 2026-10-13 (COM7, live)

`tools/serial_capture.py COM7` on the connected car returned a real PANK record on the
current bench firmware:

```
[CRASH] boot=980 crash=754 reason=PANIC heap=176400 lastphase=''
[BOOT] reset reason: PANIC
[BOOT] *** UNSOLICITED RESTART #754 - last was PANIC, phase '', heap 176400 ***
[V2] Web layer always-on: AP= ip=192.168.245.1 http=:80 ws=:81
Own AP active: 4WD4MCar_WiFi
[RELAY] RELAY_HOST is empty - the relay transport stays off (see Config.h)
```

Confirms: the bench car IS on a 1.3.x build (new IP `.245.1`, new AP name), **still PANICs**,
and `lastphase` is empty because nothing survives the panic — which is exactly what Phase 1 fixes.

## Local toolchain (this PC)

- **Compile:** bundled `arduino-cli` at
  `D:\Arduino\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe` (1.5.1).
  ESP32 core **3.3.12** + tools are in `%LOCALAPPDATA%\Arduino15`; libraries `WebSockets` + `U8g2`
  in `%USERPROFILE%\Documents\Arduino\libraries`. The sandbox redirects the CLI's data dir, so pass a
  config file (`C:\Users\caspe\AppData\Local\Temp\opencode\arduino-cli.yaml`) that points
  `directories.data` at `%LOCALAPPDATA%\Arduino15` and `directories.user` at `%USERPROFILE%\Documents\Arduino`.
- **Compile command:**
  `arduino-cli --config-file <cfg> compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app "…\Genum_4WD4M_CAR"`
- **Serial capture:** `python tools\serial_capture.py COM7 <secs> <out.log>` (pyserial 3.5 present).
- **Flash:** owner-side (Arduino IDE), FQBN `esp32:esp32:esp32:PartitionScheme=huge_app`, 115200.
- **Source gates:** `powershell -NoProfile -ExecutionPolicy Bypass -File tools\check-firmware-syntax.ps1`
  (10 TUs, timeout ≥ 240000), `node tools\check-webpage-js.mjs`, `node tools\check-relay-contract.mjs`,
  `node tools\mutate-gates.mjs` (ALONE, clean tree).

## Phases (car-side; app detail lives in the app plan)

- [x] **Phase 1 — crash capture + PANIC fix (BLOCKER).** *Firmware capture DONE in 1.3.6*
      (`RTC_NOINIT_ATTR` breadcrumb + per-loop heartbeat + broad phase marks; `crashLogBegin()`
      prefers the fresh phase over the rate-limited NVS copy; `/status` crash-TIME fields; node +
      syntax gates green; `arduino-cli` compile PASS). *The reset itself is still OPEN* — it needs
      the on-car repro once the owner flashes 1.3.6; the breadcrumb now names the operation/task/
      heap-at-crash so the next COM7 capture identifies it.
- [x] **Phase 2 — OLED last-two-lines rule.** DONE in 1.3.6 (U-107): the last two rows follow the
      U-105 transport toggles via `setTransportToggles()`/`ModeManager::pushTransportToggles()` —
      both ON → alternate the Bluetooth frame ⇄ the Wi-Fi frame ~1 s; one ON → that frame always;
      the Wi-Fi frame's last line alternates **network name ⇄ IP**; a relay (Internet) link keeps
      its own frame. `BT_SPP_NAME` single-sources the advertised name (was a second literal).
      Compile PASS, gates green.
- [x] **Phase 3 — (app) Remote layout / IP ⇄ name rotation** — DONE (app): status only right of Back,
      per-link Bluetooth/Wi-Fi disconnect row below Back, IP ⇄ name rotation, strip removed from the
      Connectivity panel. See the app plan.
- [x] **Phase 4 — (app) connection truth + restart prompt** — DONE (app): `phoneSsid` + boot-count
      restart prompt (Reconnect/Leave/Cancel). See the app plan.
- [x] **Phase 5 — SSID = `4WD4MCar_WiFi`.** DONE (car half): `Config.h` `WIFI_AP_NAME` →
      `4WD4MCar_WiFi`; the rest of the repo (comments/docs/tools) renamed too. App half DONE (registry
      + fixtures/docs; matching stays case-insensitive). Compile PASS, gates green.
- [x] **Phase 6 — (app) auto-join labels** — DONE (app): both toggles kept; “Boot auto-join (car)”
      vs “Auto-join saved router (app)” with distinct icons + captions. See the app plan.
- [x] **Phase 7 — fresh start + timestamps.** Car: a factory-reset that wipes every `botcfg` key +
      stamps records with the app-provided epoch. App + DB work mirrored in the app/website plan.
      **Car half DONE (U-108 Phase 7a):** `FACTORY;RESET` (whole-namespace wipe + RAM defaults +
      counter reset) and `TIME;<epoch>` + `botcfg/config_epoch` stamping (see RECORDS-INVENTORY.md
      §5). **App half DONE (U-108 Phase 7b):** `resetCarData()` + Remote screen button.
      **DB migration PENDING (U-108 Phase 7c):** additive SQL in
      `genumsolutions-website/supabase/migrations/20261013120000_phase7c_fresh_start_cleanup.sql`.
      Compile PASS, gates green.
- [x] **Phase 8 — donor dead-code purge (firmware only).** Remove unused/unnecessary donor leftovers
      from the 4WD4M car; keep every live path. No app/DB/donor-repo change.
      **DONE (U-108 Phase 8):** The SSID case rename (`4WD4MCar_WiFi`) was completed in Phase 5.
      The CS modes (PATH, OBS_US, OBS_IR, MANUAL, AUTO) are retained per owner request to show
      "Coming Soon" on the OLED and remain toggleable. No other unused donor code found — all
      remaining paths are live. Compile PASS, gates green.
- [ ] **Phase 9 — docs / DEVICE-TESTS rows / close-out.**

### Flash batching
Phases 1/2/5/8 are firmware. They are committed/pushed individually but **owner-flashed once** at the
end. Since 1.3.5 was never flashed, this batch supersedes it — final flash target will be the version
this round produces (likely **1.3.6**); confirm at the Phase 1 bump.

## Constraints / invariants (do not break)
- Car AP IP `192.168.245.1`; `192.168.4.x` FORBIDDEN fleet-wide (FIN-48).
- `startOwnAP()` remains the only wifi-config op; `beginAlwaysOn()` sets `WIFI_AP_STA` first.
- `.3.x`/`.4.x` address ban, U-92 own-AP guards, case-insensitive router delete — keep.
- **F-61**: green gates are NOT device evidence. Nothing is device-verified until the owner flashes.

## RESUME HERE
> Car firmware **1.3.6** now carries Phases 1/2/5/7/8: crash-TIME evidence (U-106), the OLED
> last-two-lines rule (U-107), the `4WD4MCar_WiFi` SSID case (Phase 5), fresh start + timestamps
> (`FACTORY;RESET` / `TIME;<epoch>`, U-108 Phase 7a), and donor dead-code purge (Phase 8 —
> effectively no-op, CS modes retained). **App Phases 7b DONE** (`resetCarData` + Remote button).
> **DB Phase 7c PENDING** (additive migration in website repo). Remaining: **Phase 9**
> (docs/DEVICE-TESTS rows/close-out). Nothing is device-verified until the owner flashes (F-61);
> the PANIC itself is confirmed by the next tethered COM7 capture after that flash.
