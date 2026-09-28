# CATALOG — Genum_4WD4M_CAR

> Quick reference. Full detail: `Readme.md` · master plan:
> `guide/PLAN-2026-09-28-4WD4M-CAR-REFERENCE-DESIGN.md`.

## Identity (v1.0.0)

| Item | Value | Source |
|---|---|---|
| FW_NAME | `4WD4M Car` | `Config.h:9` |
| FW_VERSION | `1.0.0` | `Config.h:10` |
| Bluetooth SPP | `4WD CAR` (server, PIN 1234) | `BluetoothComm.cpp:13` |
| Wi-Fi AP | `4WDCar_Wifi`, open, channel 1, max 4 clients | `Config.h:104` + `WebServerComm.cpp` |
| AP subnet | **192.168.245.1/24** (O-1 adopted) | `WebServerComm.cpp:435-439` |
| Partition | `huge_app` REQUIRED | CI + Readme §6 |
| NVS | ns `botcfg` (wifi_ssid/wifi_pass, rnet0-5s/p, mode, speed, trim) | `ParamsStore` |

## Modes (v1.0.0 baseline = donor behaviour; v2 targets Phase 1)

| Token | Baseline | v2 target (Phase 1) |
|---|---|---|
| 4WD4M | LIVE (BT drive) | LIVE — drive over **BT + Wi-Fi** |
| ESP_SER | LIVE (Wi-Fi drive mode) | **CS** — retired as a mode; Wi-Fi layer always-on instead |
| ESP_CLI | WIP | **CS** |
| PATH OBS_US OBS_IR MAN AUTO 2WD1M | CS | CS (roadmap unchanged) |

CAPS broadcast on connect + mode change; `S` always allowed; legacy `BT`/`BLUETOOTH` alias → 4WD4M.

## Protocol (unchanged from donor — the compatibility backbone)

Line-oriented `\n`, 128-byte buffer, `\r` skipped, 20 ms read. In: `F B L R S` · `SPD<n>`
· `MODE_<token>` · `REQ_STATE` · `REQ_NETW` · `WIFICFG;<ssid>;<pass>` (+`?`/`RESET`) ·
`ROUTERS;LIST|ADD|USE|DEL|CLEAR` · `ESTOP`. Out: `STATE;…` · `CAPS;…` · `NACK;…` · `NETW;…` ·
`TEL;…`; WS JSON status/telemetry dialect kept.

## Lineage

`Genum_WIRELESS_CAR` 1.8.4 @ `d0bf8c0` (2026-09-27) — copied via `git archive`
(tracked files only), then renamed. No donor history/tags carried.
