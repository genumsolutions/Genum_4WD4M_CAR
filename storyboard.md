# Car OLED Storyboard

1.3" SH1106 128×64 driven by U8g2 (`Oled_1_3`).  The header is always the
project name `4WD4M Car` (Config.h `FW_NAME`), followed by
`Mode : <registry display name>`.  The body depends on the active mode and
whether it is a stub (coming-soon) mode.  Redraws are throttled; mode changes
and "Stopped"/"Failsafe" frames are never dropped (content-change is tested
before the throttle).  `ModeManager` calls `tick()` every loop so the
transport-frame rotation keeps advancing even while the car sits idle.

**Mode taxonomy (fleet registry, v1.5.0)**

| Token    | Live?   | Display          | Notes                            |
|----------|---------|------------------|----------------------------------|
| 4WD4M    | live    | 4WD4M            | WAS `BT`/`BLUETOOTH` pre-X-8     |
| PATH     | stub    | PATH             | coming-soon                      |
| OBS_US   | stub    | OBS_US           | coming-soon                      |
| OBS_IR   | stub    | OBS_IR           | coming-soon                      |
| MAN      | stub    | MAN              | coming-soon (RF-only)            |
| AUTO     | stub    | AUTO             | coming-soon                      |
| 2WD1M    | stub    | 2WD1M            | coming-soon (owner parked it)    |

`ESP_SER` / `ESP_CLI` were **retired (U-79)**: the WiFi web-server is an
always-on connection layer, not a drive mode, so neither is reachable or
displayed.

Legacy alias: old controllers emit `BT` on the wire → normalised to
`4WD4M` everywhere in the fleet (receivers MUST accept both forever).

---

## 1. Boot splash

```
+----------------------------------+
|                                  |
|           4WD4M Car              |
|            v1.3.7                |
|      GENUM SOLUTIONS PVT. LTD.   |
|                                  |
+----------------------------------+
```

Drawn by `Oled_1_3::begin()`: `FW_NAME` (7x13B, y24) · `v<FW_VERSION>`
(6x10, y40) · `FW_VENDOR` (5x7, y56), then a framed blank screen, held
~1400 ms.  After the delay `setup()` enters the restored mode and the framed
dashboard takes over.

## 2. Live dashboard (4WD4M)

```
+----------------------------------+
|            4WD4M Car             |  heading 1 = FW_NAME (underlined)
|        Mode : 4WD4M              |  heading 2 = "Mode : <display>"
| Status : Forward                 |
| Speed : 170                 ▮▮▮▮ |  bars while a link is up
| Bluetooth : 4WD4M Car            |  row 3 — see section 7
| Type : SPP                       |  row 4 — see section 7
+----------------------------------+
```

- `Status` shows the live drive word; an empty/"Idle" status renders as
  ` !!! Ready !!! `.
- The right-edge signal bars on the Speed row are drawn whenever a controller
  is actually linked (SPP or WebSocket).

## 3. Stub modes (COMING SOON)

All stub modes share the same OLED frame:

```
+----------------------------------+
|            4WD4M Car             |
|        Mode : OBS_US             |
|                                  |
|          COMING SOON             |
+----------------------------------+
```

- Heading 2 always shows the mode's display name (registry token).
- Body: **only** a large centred `COMING SOON` (`u8g2_font_9x18B_tr`, stepping
  down fonts if it would overflow) — no status line, no speed, no "(stub)"
  suffix: the top bar already carries the mode identity (T-27 owner decision
  2026-09-14).
- The WIP variant renders `WORK IN PROGRESS` in the same slot.
- Driving commands are refused; `S` (stop) is still accepted and resets the
  failsafe clock.

Modes shown this way: **PATH, OBS_US, OBS_IR, MAN, AUTO, 2WD1M**.

## 4. Failsafe stop

```
+----------------------------------+
|            4WD4M Car             |
|        Mode : 4WD4M              |
| Status : Failsafe Stop !!!       |
| Speed : 0                        |
| Bluetooth : 4WD4M Car            |
| Type : SPP                       |
+----------------------------------+
```

- The status word becomes `Failsafe Stop !!!` (three trailing exclamation
  marks, from `checkFailsafe()`).
- Failsafe fires after 2 s of command silence in drive modes.
- `S` is always accepted and clears the failsafe immediately.
- In stub modes failsafe still runs — `checkFailsafe()` is not gated on
  `isCurrentModeStub()`.

## 5. Disconnected state

```
+----------------------------------+
|            4WD4M Car             |
|        Mode : 4WD4M              |
| Status :  !!! Ready !!!          |
| Speed : 170                      |
| WiFi : 4WD4MCar_WiFi             |
| IP : 192.168.245.1               |
+----------------------------------+
```

- When `statusBuf` is empty or `"Idle"`, the OLED renders
  ` !!! Ready !!! ` instead.

## 6. Boot restore behaviour

- The car restores the last **mode** and **speed** on boot (Preferences
  namespace `botcfg`, keys `mode`/`spd`) and enters the same mode/speed it had
  before power-off.
- The remote restores its own copy, then re-syncs over Bluetooth
  (`REQ_STATE` → `STATE;MODE=..;SPD=..`) as soon as it connects, so both sides
  always converge on the same state.

## 7. Last two rows — transport frames (U-107 / U-110)

Rows 3 and 4 are the TRANSPORT FRAMES.  They follow the U-105 transport
TOGGLES (`WIFICFG;WIFI;ON|OFF` and `WIFICFG;BT;ON|OFF`), not the live link
state:

| Toggles          | Rows 3–4                                                    |
|------------------|-------------------------------------------------------------|
| Wi-Fi ON + BT ON | the two frames alternate every `ROTATE_INTERVAL_MS` (~1 s)  |
| Bluetooth only   | the Bluetooth frame only                                    |
| Wi-Fi only       | the Wi-Fi frame only                                        |

Both frames are two rows:

```
Bluetooth frame              Wi-Fi frame
+----------------------+     +----------------------+
| Bluetooth : <name>   |     | WiFi : <ssid>        |
| Type : SPP           |     | IP : <car IP>        |
+----------------------+     +----------------------+
```

- `<name>` is `BT_SPP_NAME` from Config.h — ONE source, not a second hard-coded
  literal that can drift from the radio.  Since **U-110** it is `4WD4M Car`, so
  the phone's pairing list, this frame and the app all name the car the same
  thing.
- `<ssid>` is the joined router's SSID, or the car's own hotspot name
  (`4WD4MCar_WiFi`) while it is on its own AP.
- `<car IP>` is the car's interface address on that network
  (`WIFI_AP_IP` = `192.168.245.1` on its own AP).
- **U-110** dropped the old name⇄IP alternation *inside* the Wi-Fi frame: the
  name and the IP are shown at once, so the Bluetooth⇄Wi-Fi swap is the only
  remaining alternation.
- A live RELAY (Internet) link is a third transport the toggles do not name, so
  it keeps its own pinned `Internet : <type>` / `IP : <ip>` frame.

`Oled_1_3::tick()` (U-107b) drives the rotation every loop, so an idle car with
a constant `extraInfo` no longer freezes on one frame.
