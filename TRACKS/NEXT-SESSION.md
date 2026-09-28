# NEXT-SESSION — Genum_4WD4M_CAR

**Resume point (2026-09-28, after the U-49 app-only round — NO firmware change was made):**

1. ~~Phase 0~~ DONE (repo on GitHub, CI green) · ~~Phase 1 v2 re-architecture~~ DONE
   (commit `a48705e`, run `36377627226` GREEN: 1,742,098 B / 55%).
2. ~~Phase 2 (app integration)~~ DONE — app `8d54fed` pushed, CI + **OTA published**
   (208/208 tests, tsc clean): OWN_AP_NAMES registry + isOwnApName, DEFAULT_AP_IP
   192.168.245.1 / DEFAULT_WS_URL across hub/Tools/Remote/RouterPanel, 3 new v2 test pins.
3. **U-49 DONE in code, NOT shippable yet.** The app now has a single-active-transport
   `LinkManager` + a Control Panel transport picker (BT Classic / BLE / Car WiFi / Home WiFi), a
   Wi-Fi diagnostics engine, a fixed `caps` string parser, and the Wi-Fi permissions + cleartext
   manifest plugin. Gates: **tsc 0 + 243/243 tests**. Uncommitted; no push, no build.
4. **NEXT (blocking everything Wi-Fi): build the APK.** The F-30 fix is a **manifest** change, so
   OTA can never deliver it. Build from `C:\bs` per `genumsolutions-app/mobile/AGENTS.md`, then
   prove the manifest with `aapt dump xmltree` (F-8) before installing.
5. **NEXT (owner at the bench): device round 1** (`TRACKS/DEVICE-TESTS.md`). Start with **T3's
   browser check** — join `4WDCar_Wifi`, load `http://192.168.245.1` in the phone browser. Loads ⇒
   the car is fine and it is purely the app build. Then run T1–T10 app-first (flash `fc5cf96` via
   USB, 115200, **huge_app**). Donor NVS may hold mode=SERVER — expect the one-time
   `[BOOT] ... retired (CS) in v2 - keeping 4WD4M` line.
6. Row **T7 is stale** — there is no ESTOP in this firmware; it now tests `S`/`SPD0` + link-loss
   safe-stop instead (`TRACKS/FAILSAFE.md` item 11).
7. Round PASS → fleet port planning (Phase 4 of the master plan).

Open owner decisions: O-3 (remote pairing update — needed before any remote-first row),
O-4 (e-stop UI — **now also blocked on firmware**, which has no ESTOP handler).

**Do not assume Wi-Fi is fixed until an APK is on the phone and `aapt` has shown the permissions.**
