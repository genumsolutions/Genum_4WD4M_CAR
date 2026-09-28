Version 1.0.0

# 4WD4M Car — Genum_4WD4M_CAR (v2 fleet reference design)

## 1. Project Name
**4WD4M Car (`Genum_4WD4M_CAR`) — 4WD modular car (Bluetooth SPP + Wi-Fi AP/STA setup-and-control + OLED + remote/app)**

The **newest fleet reference design (v2 architecture)**. Baseline: fresh copy of
`Genum_WIRELESS_CAR` 1.8.4 (`d0bf8c0`, 2026-09-27) with a renamed identity — **no donor history,
no donor tags** (fresh git history, owner ruling R-2). The donor repo remains untouched.

## 2. Identity (unique per car — fleet rule)

| Item | Value |
|---|---|
| Bluetooth SPP name | **`4WD CAR`** (PIN 1234) — `BluetoothComm.cpp` |
| Wi-Fi AP SSID | **`4WDCar_Wifi`** (open) — `Config.h` `WIFI_AP_NAME` |
| AP subnet | **192.168.245.1/24** (NOT 192.168.4.x — phone-side subnet collision is a proven past bug; the .244 subnet belongs to the WIRELESS car so two cars can coexist) |
| FW_NAME / FW_VERSION | `4WD4M Car` / `1.0.0` — `Config.h` |

## 3. Modes (fleet 9-token registry + CAPS)

Every token stays selectable and is announced via
`CAPS;4WD4M:LIVE;ESP_SER:CS;…;2WD1M:CS` (device order, no trailing `;`).

| Token | Display | Avail (v1.0.0) | Notes |
|---|---|---|---|
| 4WD4M | 4WD4M | **LIVE** | the single drive mode — over **Bluetooth AND Wi-Fi** |
| ESP_SER | Server (ESP_SER) | CS | retired as a mode in v2 (was the donor's Wi-Fi drive mode) |
| ESP_CLI | Client (ESP_CLI) | CS | retired as a mode in v2 |
| PATH / OBS_US / OBS_IR / MAN / AUTO / 2WD1M | — | CS | fleet roadmap, unchanged |

Legacy alias `BT`/`BLUETOOTH` → 4WD4M still accepted (fleet convention).

## 4. v2 architecture — what differs from the donor (planned work)

- **Wi-Fi is an always-on connection/setup channel, never a mode**: AP WebServer + WebSocket
  run from boot (non-blocking) regardless of active mode; STA joins stored routers with
  auto-rejoin.
- **Drive over both transports in 4WD4M** — the donor's rule "Wi-Fi drive happens in ESP_SER"
  is retired here. Same line protocol (`\n`-terminated), same `handleCommand()` funnel, same
  `STATE`/`CAPS`/`NACK`/`NETW` responses, JSON-over-WS telemetry kept.
- **Setup over both transports**: `WIFICFG;<ssid>;<pass>` + `ROUTERS;ADD|USE|DEL|CLEAR`
  (max 6 routers, NVS `botcfg`) work over Bluetooth **and** over the AP WebSocket.
- Phase 1 of `guide/PLAN-2026-09-28-4WD4M-CAR-REFERENCE-DESIGN.md` implements this; until that
  phase lands, this repo compiles and behaves as the renamed donor baseline.

## 5. Materials / Pins / Libraries
Same bill of materials and wiring as the donor hardware (the 4WD4M testbed IS the former
WIRELESS-car hardware refit): ESP32 DevKit, L298N (4 × brushed motors, skid-steer), 1.3" SH1106
OLED (I2C), Li-ion pack. Pins in `Config.h`. Libraries: WebSockets 2.7.2, U8g2, ESP32 core
3.3.11 (WebServer/BluetoothSerial/Preferences ship with the core).

## 6. Build Metadata

| Item | Value |
|---|---|
| Board | ESP32 Dev Module (`esp32:esp32:esp32`, core 3.3.11) |
| Partition | **`huge_app` REQUIRED** (same as donor) |
| Flash baud | 115200 |
| Baseline build | donor 1.8.4 measured 1,740,754 B (55%) / RAM 69,492 B (21%) — v1.0.0 figures land with the Phase-1 CI run |
| Build tool | `arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app .` |
| NVS namespace | `botcfg` (wifi_ssid/wifi_pass, mode/speed) |
| CI | `.github/workflows/arduino-ci.yml` — compile gate + deltas report, secrets-free |

## 7. Modules
Identical file set to the donor 1.8.4 (renamed): `Genum_4WD4M_CAR.ino` (entry, boot splash,
watchdog) · `Config.h` (identity/pins/constants) · `ModeManager` (registry, gating, failsafe,
STATE) · `BluetoothComm` (SPP transport) · `WebServerComm` (AP/STA + HTTP/WS + provisioning) ·
`L298NControl`/`MotorControl`/`TwoWD1M` (drive) · `Oled_1_3`/`OledDisplay` (UI, see
`storyboard.md`) · `ParamsStore` (NVS) · `secrets.h.example` (template — `secrets.h` is
gitignored, never committed; the sketch compiles without it).

## 8. Changelog

- **1.0.0 (2026-09-28)** — fresh baseline from `Genum_WIRELESS_CAR` 1.8.4 (`d0bf8c0`):
  identity rename (BT `4WD CAR` · AP `4WDCar_Wifi` · AP 192.168.245.1/24 · FW_NAME `4WD4M Car`),
  fresh git history. v2 re-architecture (always-on Wi-Fi channel, Wi-Fi drive in 4WD4M, ESP_SER/
  ESP_CLI → CS) lands in Phase 1 of the reference plan.
