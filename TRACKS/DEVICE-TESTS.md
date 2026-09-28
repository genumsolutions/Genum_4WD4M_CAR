# DEVICE-TESTS — Genum_4WD4M_CAR

> Owner-at-the-bench sheets. Test order per standing rule: physical remote first — EXCEPT this
> car, until O-3 (remote pairing update for `4WD CAR`) lands → app-first for round 1.

## Round 1 — v1.0.1 (R1 fix round — T0 PASS, T1–T10 verdicts NOT YET TRANSCRIBED)

> The v1.0.1 source is on `main` (`93817b2` OLED link-context, `Config.h` version bump) and the
> v1.0.0 bench flash below is real. **The T1–T10 rows are still unchecked on purpose.** The owner
> ran a round in the session that produced the app-side R1 fixes, but the per-row verdicts were
> never written into this file. They are NOT inferred here. Fill them in at the bench, then change
> the verdict line to RC-PASS / RC-FAIL.

### Verified in-session (do not re-run)

- [x] T0 — bench flash + first boot, v1.0.0, 2026-09-28. Details in the v1.0.0 round below.
- [x] v1.0.1 compiles in CI (`.github/workflows/arduino-ci.yml`,
      `esp32:esp32:esp32:PartitionScheme=huge_app`).
- [x] App-side R1 fixes shipped in `genumsolutions-app` (`d77c35d`, `0a86f64`), OTA-published:
      flicker, Wi-Fi connect hard-timeout (8 s), control-hub state plumbing.
- [x] App-side R1 (this session): Wi-Fi link **verification** is now a separate truth from
      "socket open" — `linkVerified` only flips true when the car actually answers
      (`wifiService.waitForCarAnswer`, 9 new tests in `mobile/src/services/wifiService.test.ts`).

### Needs the owner at the bench

- [ ] T1 — BT pair to `4WD CAR` (PIN 1234) from the app; drive F/B/L/R/S
- [ ] T2 — median command round-trip over BT (10 × `SPD150` → STATE echo), recorded ms
- [ ] T3 — phone joins AP `4WDCar_Wifi`; HTTP :80 page loads; WS drive works (4WD4M, no mode switch)
- [ ] T4 — median command round-trip over WS (10 commands), recorded ms
- [ ] T5 — provisioning E2E: send `WIFICFG;<home>;…` over BT → car joins home LAN → `REQ_NETW` confirms
- [ ] T6 — provisioning over AP WS: `ROUTERS;LIST` returns list (v2 path, after Phase 1)
- [ ] T7 — ESTOP over BT AND over WS; link-loss safe-stop on both (kill app / close socket)
- [ ] T8 — 100-command burst over WS; packet loss % recorded
- [ ] T9 — old-car regression: donor WIRELESS car still pairs/drives as before (untouched repo proof)
- [ ] T10 — CAPS shows `4WD4M:LIVE`, `ESP_SER:CS`, `ESP_CLI:CS`; app marks the CS ones Coming Soon
- [ ] Flash v1.0.1 to the board and confirm the new OLED line renders
      (BT paired vs Wi-Fi mode + RSSI) and the splash reports 1.0.1

Verdict: **RC-PENDING** — T0 PASS only; T1–T10 unrecorded.

## Round 0 — v1.0.0 baseline (flash + boot PASS 2026-09-28)

Pre-flight:
- [x] Bench flash via USB, 115200, **PartitionScheme=huge_app** — **DONE 2026-09-28 by the AI
      session**: COM7 (CH340), build `build/v2`, upload at 115200 (default 921600 failed twice —
      flaky CH340 link, known workaround), **"Hash of data verified"**, hard reset OK. Board MAC
      `08:b6:1f:98:2f:f4`. Sketch 1,742,162 B (55%).
- [x] First boot: **T0 PASS** — serial capture (DTR/RTS reset + PowerShell reader):
      ```
      [BOOT] Restored mode=ESP_SER is retired (CS) in v2 - keeping 4WD4M.
      [BOOT] Restored speed=255
      Own AP active: 4WDCar_Wifi
      Stored router available - join via ROUTERS;USE / WIFICFG (own AP stays default)
      [V2] Web layer always-on: AP=4WDCar_Wifi ip=192.168.245.1 http=:80 ws=:81
      ```
      The retired-mode guard fired exactly as designed (donor NVS held mode=SERVER from the
      WIRELESS firmware; v2 kept 4WD4M). Splash itself renders on the OLED (owner eyeballs it).

Rows (rows 1–10 carried into the v1.0.1 round above; this v1.0.0 round is closed at T0):
- [ ] T1 — BT pair to `4WD CAR` (PIN 1234) from the app; drive F/B/L/R/S
- [ ] T2 — median command round-trip over BT (10 × `SPD150` → STATE echo), recorded ms
- [ ] T3 — phone joins AP `4WDCar_Wifi`; HTTP :80 page loads; WS drive works (4WD4M, no mode switch)
- [ ] T4 — median command round-trip over WS (10 commands), recorded ms
- [ ] T5 — provisioning E2E: send `WIFICFG;<home>;…` over BT → car joins home LAN → `REQ_NETW` confirms
- [ ] T6 — provisioning over AP WS: `ROUTERS;LIST` returns list (v2 path, after Phase 1)
- [ ] T7 — ESTOP over BT AND over WS; link-loss safe-stop on both (kill app / close socket)
- [ ] T8 — 100-command burst over WS; packet loss % recorded
- [ ] T9 — old-car regression: donor WIRELESS car still pairs/drives as before (untouched repo proof)
- [ ] T10 — CAPS shows `4WD4M:LIVE`, `ESP_SER:CS`, `ESP_CLI:CS`; app marks the CS ones Coming Soon

Verdict: RC-PASS / RC-FAIL → snag round numbering per fleet convention.
