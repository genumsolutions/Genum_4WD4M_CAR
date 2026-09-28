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

## Phase 1 — v2 re-architecture — DONE 2026-09-28 (commit `a48705e`, CI GREEN)
- [x] **P1-1** drive 4WD4M over BOTH transports: the WIRE source already passed the drive gate;
      the real blocker was transport ROUTING — fixed by permanent wiring (comm_ = web,
      auxComm_ = BT, no mode-based swap) + unconditional `handle()` + always-on servers.
      Queued web lines now drain as WIRE source (was default-source echo).
- [x] **P1-2** `WebServerComm::beginAlwaysOn()` — AP + HTTP :80 + WS :81 up at boot, never
      stopped; `handle()` unconditional; WIFICFG stores + rejoins WITHOUT switching modes;
      own-AP reserved entry always listed; `connected()` transport-honest (WS clients or
      joined STA — broadcasting AP alone is not a link).
- [x] **P1-3** `ESP_SER`/`ESP_CLI` → CS in the registry (CAPS announces; tokens still toggle,
      car renders COMING SOON, WiFi truth rides extraInfo_ on those frames).
- [x] **P1-4** setup commands over AP WS: works via WS → `handleCommand(…, WIRE)` → system
      hook (was already valid; verified + kept; `REQ_STATE`/`REQ_NETW` broadcast live).
- [x] **P1-5** failsafe/safe-stop unchanged (2 s silence, `S`/`SPD0`/`ESTOP` universal,
      link-loss safe-stop now correct on BOTH transports — `isConnectedNow()` = any link up);
      boot restore REFUSES retired (CS) modes → donor-NVS ESP_SER state cannot boot the
      refit testbed into a stub. Boot splash v1.0.0 verify = device round T0.
- [x] **P1-6** compile size: local **1,742,162 B (55%) / 69,516 B (21%)** · CI run
      `36377627226` **1,742,098 B (55%)** — +64 B over baseline for the whole v2 change.

## Phase 2 — app integration (BLOCKED on Phase 1; app repo, OTA-safe steps)
See `guide/PLAN-2026-09-28-4WD4M-CAR-REFERENCE-DESIGN.md` § Phase 2.

## Phase 3 — device verification round (BLOCKED on Phase 1 + bench)
See `TRACKS/DEVICE-TESTS.md` round 1.
