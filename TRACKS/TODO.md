# TODO — Genum_4WD4M_CAR

## Phase 0 — repo baseline + rename (2026-09-28) — DONE
- [x] **P0-1** fresh copy from donor 1.8.4 (`d0bf8c0`) via `git archive` — no `.git/`, no
      `secrets.h`, no `build/`, donor TRACKS not carried
- [x] **P0-2** rename: `Genum_4WD4M_CAR.ino` · FW_NAME `4WD4M Car` · FW_VERSION `1.0.0` ·
      BT `4WD CAR` · AP `4WDCar_Wifi` · AP subnet 192.168.245.1/24 · CI workflow name ·
      header comments · `storyboard.md` · `secrets.h.example` · `.gitignore`
- [x] **P0-3** fresh `Readme.md` (v2 architecture, identity table, donor lineage)
- [x] **P0-4** `TRACKS/` skeleton (this folder)
- [x] **P0-5** rename sweep CLEAN (grep: zero `Genum_WIRELESS_CAR`/`WirelessCar_Wifi`/
      `WIRELESS CAR`/`192.168.244` outside storyboard history notes)
- [x] **P0-6** git init + baseline commit **`ac1a9c8`** (main, root commit; 30 files; secrets.h
      NOT tracked — verified)
- [x] **P0-7** Gate G0: **PASSED twice** — local `arduino-cli` (huge_app) **1,742,066 B (55%) /
      69,516 B (21%) RAM** + CI run `36376315823` **GREEN: 1,742,018 B (55%)** (run
      `36376298563` failed on the root commit — deltas-report needs a parent; one-time
      fresh-repo artifact, recorded here so it never reads as a regression)

## Phase 1 — v2 re-architecture (BLOCKED on owner "go Phase 1")
- [ ] **P1-1** `acceptsDrive` for 4WD4M true over BOTH comm paths (retire transport scoping)
- [ ] **P1-2** `WebServerComm` always-on from boot, decoupled from mode
- [ ] **P1-3** `MODE_ESP_SER`/`MODE_ESP_CLI` → CS in the registry (CAPS announces)
- [ ] **P1-4** setup commands (`WIFICFG`/`ROUTERS`/`REQ_NETW`) accepted over AP WS too
- [ ] **P1-5** boot splash v1.0.0 verify; failsafe/safe-stop unchanged on both transports
- [ ] **P1-6** compile size report (expect ≈ donor ~1.74 MB huge_app)

## Phase 2 — app integration (BLOCKED on Phase 1; app repo, OTA-safe steps)
See `guide/PLAN-2026-09-28-4WD4M-CAR-REFERENCE-DESIGN.md` § Phase 2.

## Phase 3 — device verification round (BLOCKED on Phase 1 + bench)
See `TRACKS/DEVICE-TESTS.md` round 1.
