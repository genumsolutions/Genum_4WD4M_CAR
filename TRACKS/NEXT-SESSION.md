# NEXT-SESSION — Genum_4WD4M_CAR

**Resume point (2026-09-28, after Phase 1):**

1. ~~Phase 0~~ DONE (repo on GitHub, CI green) · ~~Phase 1 v2 re-architecture~~ DONE
   (commit `a48705e`, run `36377627226` GREEN: 1,742,098 B / 55%).
2. **NEXT: device round 1** (`TRACKS/DEVICE-TESTS.md`) — owner flashes `a48705e` via USB
   (115200, **huge_app**) at the bench and runs rows T0–T10. NOTE: donor NVS on the testbed
   may hold mode=SERVER (ESP_SER) — v2 boot restore refuses retired modes and keeps 4WD4M
   (expect the `[BOOT] ... retired (CS) in v2 - keeping 4WD4M` serial line once).
3. Device round PASS → Phase 2 (app integration, OTA-safe steps) per the master plan.

Open owner decisions referenced: O-3 (remote pairing update — needed before any remote-first
row), O-4 (e-stop UI) — neither blocks the app-first round 1.
