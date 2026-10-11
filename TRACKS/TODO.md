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

## Phase U-96 follow-up — 2026-10-06 — DONE
- [x] **U-96a** fix the BROKEN BUILD: `WRITE_PERI_REG` on a brownout symbol that does not
      exist on core 3.3.x (`Genum_4WD4M_CAR.ino`). Two commits in a row had guessed a symbol
      wrong (`c3c29e6` then `d34b52c`); neither built. Removed — see FAILSAFE #12 for why it
      cannot be done, and why the old comment's polarity was inverted.
- [x] **U-96b** `#error` guard pinning `CONFIG_ESP_BROWNOUT_DET_LVL == 0`, so a future core
      bump that changes the default fails the BUILD instead of silently reintroducing
      spurious resets.
- [x] **U-96c** close the gate blind spot: `tools/check-firmware-syntax.ps1` now sweeps the
      `.ino` (was `-Filter *.cpp`) and **refuses to pass if it finds no `.ino`**. Proven to
      fail on a deliberately broken sketch, not merely to pass — see FAILSAFE #13.
- [x] **U-96d** app: surface the U-95 restart record. The car already sent
      `reset_reason`/`boot_count`/`crash_count`/`last_crash_phase`/`last_crash_heap` on
      `/status`; the app parsed `free_heap` from the same object and dropped these. Now
      parsed + shown as a "Restarts" row. **No firmware change was needed** — this was
      purely an app-side blind spot.
- [x] **U-96e** OWNER: flash main and confirm. Gates green / builds green on your toolchain. (Local gates: syntax 10/10, mutate-gates PASS, webpage-js PASS, relay-contract PASS - 2026-10-08)
- [x] **U-96f** align core versions: the local gate runs **3.3.12**; `arduino-ci.yml` pins
      **3.3.11**. They are not proving the same thing. Low risk, worth settling.
- [ ] **U-96g** the reset LOOP itself is still uncured. U-95 made it *observable*, nothing
      more. First read after flash: `/status` → restart record, or the app's Restarts row.
      If `crash_count` is climbing, `reset_reason` + `last_crash_phase` name the culprit —
      `http-root` and `relay-tls` are the two known suspects.
- [ ] **U-96h** `RELAYCFG` has ZERO app surface (firmware U-94 Phase 2 has it; the app has no
      hit for the string). Deliberate today — the `armed-handshake` gate blocks the relay by
      design until the relay service is deployed + `RELAY_HOST` is set. **Do not unblock it.**
- [x] **U-96i** the version handshake now exists, both repos (2026-10-11). The car's `/status`
      carries `"fw":"<FW_VERSION>"` (`3168cac`, pinned by the `status-fw` gate rule + mutants
      M66/M67 — the slot must exist AND be fed from FW_VERSION in its argument position). The
      app parses it into `telemetry.fwVersion`, passes it to `syncCrashReport` (the fleet
      table's `fw_version` was previously always `""`), and the `statusJson.fw` mislabel that
      carried the BOARD ID is fixed (`25e72a9`). The bundled catalog row is now a commented
      model fact (1.2.4), not a claim about a live car. Source-level only — F-61; the
      handshake is proven by gates, not by a device. NOTE: pre-1.2.5 cars send no `fw`, so
      the app treats absent as unknown until the owner flashes.

## Sweep — scan removed from the car's own page (2026-10-09) — DONE
- [x] **owner** the car's own page no longer offers a scan. The network table, its Scan
      button, the `lastScanSig` diff gate and both `.nets` CSS blocks (portrait + the
      landscape seam move) are gone (`78b15e9`, `fb218dc`, and this sweep). The router
      dropdown is now the page's only network control; the saved-router registry, the
      protocol and the app's scan are untouched.
- [x] **gate** `page-net-table` is retired. `page-router-dropdown` was vacuous (its only
      assertion was `rSel` truthy, and the sandbox manufactures an element for any id), so
      it now asserts the dropdown's markup and the wired commands against the page source:
      `ROUTERS;USE` on select, `WIFICFG` save on Add, `ROUTERS;DEL` on Delete, and the two
      `__add__`/`__delete__` option branches.
- [x] **harness** the five orphans left by the scan removal (M56-M62, still labelled
      `page-net-table`) are re-aimed at `page-router-dropdown`; the full harness runs green.
- [x] **OLED** the dead `modeHasDash` member (dangling since the drive-dashboard assignment
      was dropped) is removed. The unreachable ESP_SER drawing block is retained; the file
      header comment was corrected to say "retained", matching the in-code note.
- [x] **decision** the app still sends `ROUTERS;SCAN`; the page keeps the
      `STARTED`/`BUSY`/`CANCELLED` reply notes so a scan the app fires is still narrated on
      the page. Firmware scan is unchanged.
