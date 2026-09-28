# TRACKS — Genum_4WD4M_CAR (INDEX)

> Per-project tracking layer (fleet rule: updated BEFORE and AFTER every session that touches
> this repo). Cross-project ledger: workspace `guide/GUIDE.md`. Master plan:
> `guide/PLAN-2026-09-28-4WD4M-CAR-REFERENCE-DESIGN.md`.

**Status (2026-09-28):** Phase 0 + Phase 1 DONE, CI GREEN. v2 architecture LIVE in firmware:
**Wi-Fi = always-on connection/setup channel (AP `4WDCar_Wifi` @ 192.168.245.1/24 + HTTP :80 +
WS :81, up from boot, never stopped)** · **4WD4M is the single live drive mode over BOTH
Bluetooth and Wi-Fi (no mode switch)** · `ESP_SER`/`ESP_CLI` retired → CS via CAPS · WIFICFG
no longer switches modes · boot restore refuses retired modes · failsafe/link-loss safe-stop
transport-honest on both links. Commits: `ac1a9c8` baseline → `a0a06b1` → `1ccbb34` →
**`a48705e` v2 (run `36377627226`: 1,742,098 B / 55%)**. Next: owner device round 1
(`DEVICE-TESTS.md`, bench flash, huge_app) → then Phase 2 (app integration, OTA-safe).

| File | Purpose |
|---|---|
| `CATALOG.md` | identity, pins, protocol, build facts |
| `GUIDE.md` | repo role + lineage + compatibility contract |
| `TODO.md` | phase checklist (P0 done; P1 next) |
| `FAILSAFE.md` | do-not-break invariants |
| `DEVICE-TESTS.md` | owner-at-the-bench sheets (round 1 pending Phase 1) |
| `NEXT-SESSION.md` | resume point |

**Isolation status:** donor + 5 other repos untouched ✔ (verify before each session close).
