# TRACKS — Genum_4WD4M_CAR (INDEX)

> Per-project tracking layer (fleet rule: updated BEFORE and AFTER every session that touches
> this repo). Cross-project ledger: workspace `guide/GUIDE.md`. Master plan:
> `guide/PLAN-2026-09-28-4WD4M-CAR-REFERENCE-DESIGN.md`.

**Status (2026-09-28):** Phase 0 EXECUTED + GATE G0 PASSED — fresh v1.0.0 baseline from
`Genum_WIRELESS_CAR` 1.8.4 (`d0bf8c0`), renamed identity (BT `4WD CAR` · AP `4WDCar_Wifi` ·
AP 192.168.245.1/24), baseline commit **`ac1a9c8`** (main, 30 files), compile gate green
**1,742,066 B (55%) / 69,516 B (21%) RAM**. No remote/push yet (owner decides). Phase 1
(v2 re-architecture) BLOCKED on owner "go Phase 1".

| File | Purpose |
|---|---|
| `CATALOG.md` | identity, pins, protocol, build facts |
| `GUIDE.md` | repo role + lineage + compatibility contract |
| `TODO.md` | phase checklist (P0 done; P1 next) |
| `FAILSAFE.md` | do-not-break invariants |
| `DEVICE-TESTS.md` | owner-at-the-bench sheets (round 1 pending Phase 1) |
| `NEXT-SESSION.md` | resume point |

**Isolation status:** donor + 5 other repos untouched ✔ (verify before each session close).
