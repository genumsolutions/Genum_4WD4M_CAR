# DEVICE-TESTS — Genum_4WD4M_CAR

> Owner-at-the-bench sheets. Test order per standing rule: physical remote first — EXCEPT this
> car, until O-3 (remote pairing update for `4WD CAR`) lands → app-first for round 1.

## Round 1 — v1.0.0 baseline (PENDING — scheduled after Phase 1 compiles green)

Pre-flight:
- [ ] Bench flash via USB, 115200, **PartitionScheme=huge_app** (owner)
- [ ] First boot: serial log shows splash `4WD4M Car v1.0.0`, no blocking waits

Rows (fill in with results):
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
