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
   idle, not wedged. **FAILSAFES F-47: a transport failure must never take down the whole car.**
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
