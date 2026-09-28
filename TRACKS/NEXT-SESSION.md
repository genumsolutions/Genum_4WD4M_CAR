# NEXT-SESSION — Genum_4WD4M_CAR

**Resume point (2026-09-28, after Phase 1 + Phase 2 app half):**

1. ~~Phase 0~~ DONE (repo on GitHub, CI green) · ~~Phase 1 v2 re-architecture~~ DONE
   (commit `a48705e`, run `36377627226` GREEN: 1,742,098 B / 55%).
2. ~~Phase 2 (app integration)~~ DONE — app `8d54fed` pushed, CI + **OTA published**
   (208/208 tests, tsc clean): OWN_AP_NAMES registry + isOwnApName, DEFAULT_AP_IP
   192.168.245.1 / DEFAULT_WS_URL across hub/Tools/Remote/RouterPanel, 3 new v2 test pins.
   The app is ALREADY v2-ready on devices via OTA.
3. **NEXT: owner device round 1** (`TRACKS/DEVICE-TESTS.md`) — flash `fc5cf96` via USB
   (115200, **huge_app**), reopen the app (OTA auto-applies), run rows T0–T10 app-first.
   Donor NVS may hold mode=SERVER — expect the one-time `[BOOT] ... retired (CS) in v2 -
   keeping 4WD4M` line.
4. Round PASS → fleet port planning (Phase 4 of the master plan).

Open owner decisions: O-3 (remote pairing update — needed before any remote-first row),
O-4 (e-stop UI).
