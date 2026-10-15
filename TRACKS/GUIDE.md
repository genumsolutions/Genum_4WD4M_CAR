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
- The app talks to BOTH generations, and keys off CAPS, never off car identity, for behaviour
  branching.
- **A `CAPS;` line is this car's COMPLETE registry, not a delta.** `ModeManager::buildCapsMap`
  walks all of `MODE_REGISTRY`, so a token that is ABSENT from the line is a mode this car does
  not have and cannot be sent. The app reads that absence as `GONE` (U-81) and shows it as
  "Not on this car".
- **Corrected 2026-10-03 (U-81).** This line used to read "this car announces `ESP_SER:CS`".
  U-79 changed that: `ESP_SERVER`/`ESP_CLI` were **removed from `MODE_REGISTRY` entirely**, so
  this car emits neither token in any state:
  `CAPS;4WD4M:LIVE;PATH:CS;OBS_US:CS;OBS_IR:CS;MAN:CS;AUTO:CS;2WD1M:CS` — 7 keys, no `ESP_SER`,
  no `ESP_CLI`. Expect **absence**, not `CS`. Old cars still announce `ESP_SER:LIVE` /
  `ESP_CLI:WIP` and are unaffected; see DEVICE-TESTS T10.
- This car accepts drive over Wi-Fi directly in `4WD4M`; there is no website-server mode to
  select.
- On-air names are per-car unique: `WIRELESS CAR`/`WirelessCar_Wifi` (donor) vs
  `4WD4M Car`/`4WD4MCar_WiFi` (this car — U-110: the BT name now matches the
  project identity; 1.3.5 pinned the ONE own-hotspot name, and the earlier
  `4WDCar_Wifi` is gone). AP subnets differ (.244 vs .245) so two cars coexist.
- The command protocol grammar is IDENTICAL across generations — never forked.

## Where the plan lives
- **Current round (2026-10-15, PLANNED, no code yet):** `TRACKS/PLAN-2026-10-15-dual-link-and-remote-ux.md`
  (master; app mirror in `genumsolutions-app\TRACKS\`). The owner's 14-issue list: dual BT+Wi-Fi
  link, Remote-screen line merge, conditional disconnect chips, drop the phone-net chip, wider
  settings panel, the BT heap diet (110 KB → ~10 KB when BT on — the MAIN issue), Wi-Fi-off must
  hide the hotspot + page, settings sync "pronto", page always up when Wi-Fi on, IP-chip-as-link +
  de-duplicated chrome, dead mode-rotate button, auto-join toggle cleanup + settings audit,
  close-out, and the opaque reconnect dialog. **Owner directive: plan and notes only this round —
  no code until the phases are executed one at a time.**
- Baseline design plan: `guide/PLAN-2026-09-28-4WD4M-CAR-REFERENCE-DESIGN.md` (phases, gates,
  rulings R-1..R-10, open decisions O-1..O-4).
