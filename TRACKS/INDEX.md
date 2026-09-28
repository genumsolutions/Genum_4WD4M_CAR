# TRACKS — Genum_4WD4M_CAR (INDEX)

> Per-project tracking layer (fleet rule: updated BEFORE and AFTER every session that touches
> this repo). Cross-project ledger: workspace `guide/GUIDE.md`. Master plan:
> `guide/PLAN-2026-09-28-4WD4M-CAR-REFERENCE-DESIGN.md`.

**Status (2026-09-28, later session — NO FIRMWARE CHANGE):** Phase 0 + Phase 1 DONE, CI GREEN. v2
architecture LIVE in firmware:
**Wi-Fi = always-on connection/setup channel (AP `4WDCar_Wifi` @ 192.168.245.1/24 + HTTP :80 +
WS :81, up from boot, never stopped)** A→ **4WD4M is the single live drive mode over BOTH
Bluetooth and Wi-Fi (no mode switch)** A→ `ESP_SER`/`ESP_CLI` retired → CS via CAPS A→ WIFICFG
no longer switches modes A→ boot restore refuses retired modes A→ failsafe/link-loss safe-stop
transport-honest on both links. Commits: `ac1a9c8` baseline → `a0a06b1` → `1ccbb34` →
**`a48705e` v2 (run `36377627226`: 1,742,098 B / 55%)**.

**This session was app-only (U-49).** The owner's "Wi-Fi never connects at all" report was
**root-caused to the app, not the car** — the release manifest has no Wi-Fi permissions and no
cleartext allowance, so the app could not legally open a socket (`guide/FAILSAFES.md` **F-30**).
Firmware re-verified correct. Firmware-facing consequences recorded in `TRACKS/FAILSAFE.md`
items 9–11: the `caps` **string** carrier, `setMode()` persisting `CS` tokens, and **no ESTOP
exists** (T7 rewritten). Next: **build the APK** — F-30 cannot be fixed by OTA — then owner device
round 1 (`DEVICE-TESTS.md`, T3 gated on the new APK) and Phase 2 app verification
(`genumsolutions-app/mobile/TESTING.md` U-49).

| File | Purpose |
|---|---|
| `CATALOG.md` | identity, pins, protocol, build facts |
| `GUIDE.md` | repo role + lineage + compatibility contract |
| `TODO.md` | phase checklist (P0 done; P1 next) |
| `FAILSAFE.md` | do-not-break invariants (9–11 added 2026-09-28) |
| `DEVICE-TESTS.md` | owner-at-the-bench sheets (T3 gated on APK; T7 rewritten) |
| `NEXT-SESSION.md` | resume point |

**Isolation status:** donor + 5 other repos untouched ✔ (verify before each session close).
