# CATALOG — Genum_4WD4M_CAR

> Quick reference. Full detail: `Readme.md` · per-release history:
> `TRACKS/CHANGELOG.md`.

## Identity

| Item | Value | Source |
|---|---|---|
| FW_NAME | `4WD4M Car` | `Config.h` `FW_NAME` |
| FW_VERSION | `1.3.8` | `Config.h` |
| Bluetooth SPP | `4WD4M Car` (server, PIN 1234) — U-110 rename from `4WD CAR` | `Config.h` `BT_SPP_NAME` |
| Wi-Fi AP | `4WD4MCar_WiFi`, open, channel 1, max 4 clients | `Config.h` `WIFI_AP_NAME` + `WebServerComm.cpp` |
| AP subnet | **192.168.245.1/24** (O-1 adopted) | `WebServerComm.cpp` `startOwnAP()` |
| Partition | `huge_app` REQUIRED | CI + Readme §6 |
| NVS | ns `botcfg` (wifi_ssid/wifi_pass, rnet0-5s/p, last_ok_ssid/pass, t_wifi, t_bt, boot_autojoin, fw_mark, mode, spd) | `ParamsStore` |

> Two corrections (2026-10-06). The version said `1.0.0` and `Readme.md` said
> `1.0.5` while `Config.h` said `1.2.0` — `Config.h` is the truth. And the NVS row
> listed a `trim` key that is **never written**: the only code that could set it
> is the 2WD1M drive path, which is unreachable because that mode is `CS`. That
> is also why `TRIM=` on the wire and `"trim"` in `/status` are always `0`.

> **U-98/U-105 (2026-10-11/13, 1.3.0/1.3.5).** `fw_mark` = the `FW_VERSION` that last wrote the router surface;
> a boot whose running firmware differs wipes every router key (saved `rnet<N>s/p`, the stored
> pair, the last-good `last_ok_*`) plus the transport toggles (`t_wifi`/`t_bt`) and `boot_autojoin`,
> so a re-flash starts clean (BLUETOOTH/Wi-Fi both ON). `last_ok_ssid/
> pass` are now **informational** — boot auto-join reads the last-SWITCHED `wifi_ssid`, not the
> last-good pair. U-105: `t_wifi`/`t_bt` (default ON, never both off) and `boot_autojoin` (default
> ON) replaced the removed `ap_psave`/`allow_parallel`/`last_transport` keys.
>
> **(P2, same release.)** The OLED extra row and the WS bit both used `EXTRA_INTERVAL`; they are now
> two ticks — `EXTRA_INTERVAL` (extra row) 500 ms and `STATUS_INTERVAL` (broadcast) 1000 ms — so the
> OLED refreshes pronto without doubling the status JSON to every client.

## Modes (current registry — 7 tokens)

| Token | Avail | Note |
|---|---|---|
| 4WD4M | LIVE | the only drivable mode; legacy alias `BT` → 4WD4M |
| PATH OBS_US OBS_IR MAN AUTO 2WD1M | CS | announced, not drivable. `2WD1M`'s drive path is therefore unreachable — see above |

ESP_SER and ESP_CLI were **removed from the registry** in U-79; they no longer
appear in the CAPS line at all. Absence means GONE, not CS. `TRACKS/GUIDE.md`
carries the corrected CAPS string.

## Protocol (unchanged from donor — the compatibility backbone)

Line-oriented `\n`, 128-byte buffer, `\r` skipped, 20 ms read. In: `F B L R S` · `SPD<n>`
· `MODE_<token>` · `REQ_STATE` · `REQ_NETW` · `WIFICFG;<ssid>;<pass>` (+`?`/`RESET`) ·
`ROUTERS;LIST|ADD|USE|DEL|CLEAR` · `RELAYCFG`. Out: `STATE;…` · `CAPS;…` · `NACK;…` ·
`NETW;…` · `TEL;…`; WS JSON status/telemetry dialect kept.
**There is no `ESTOP`** in the firmware — see FAILSAFE #11. Safe stop is `S` /
`SPD0` / disconnect.

## Lineage

`Genum_WIRELESS_CAR` 1.8.4 @ `d0bf8c0` (2026-09-27) — copied via `git archive`
(tracked files only), then renamed. No donor history/tags carried.
