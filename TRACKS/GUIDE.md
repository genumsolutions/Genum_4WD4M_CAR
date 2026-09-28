# GUIDE — Genum_4WD4M_CAR (how this repo fits the fleet)

## Role
**Newest fleet reference design (v2 architecture).** Everything the fleet becomes later is
prototyped here first: Wi-Fi as an always-on connection/setup channel (never a mode), single
live drive mode (4WD4M) over BOTH Bluetooth and Wi-Fi, unique on-air identity.

## Lineage
- Baseline: `Genum_WIRELESS_CAR` 1.8.4 @ `d0bf8c0` (2026-09-27), copied via `git archive`
  (tracked files only — no donor history, no tags, no `secrets.h`, no `build/`).
- The donor repo is FROZEN for this work: no changes until the Phase-4 fleet port (and even
  then, one car at a time, owner-driven).

## Compatibility contract (old cars ↔ new car)
- The app talks to BOTH generations: old cars keep `ESP_SER:LIVE` (Wi-Fi drive = ESP_SER mode),
  this car announces `ESP_SER:CS` and accepts drive over Wi-Fi directly in 4WD4M. The app keys
  off CAPS, never off car identity, for behaviour branching.
- On-air names are per-car unique: `WIRELESS CAR`/`WirelessCar_Wifi` (donor) vs
  `4WD CAR`/`4WDCar_Wifi` (this car). AP subnets differ (.244 vs .245) so two cars coexist.
- The command protocol grammar is IDENTICAL across generations — never forked.

## Where the plan lives
`guide/PLAN-2026-09-28-4WD4M-CAR-REFERENCE-DESIGN.md` (phases, gates, rulings R-1..R-10,
open decisions O-1..O-4).
