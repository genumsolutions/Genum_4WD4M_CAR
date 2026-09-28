# NEXT-SESSION — Genum_4WD4M_CAR

**Resume point (2026-09-28, after the Connections Hub firmware round — board-id broadcast landed):**

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
