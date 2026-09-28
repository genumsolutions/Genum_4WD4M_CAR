# Car OLED Storyboard

1.3" SH1106 128×64 driven by U8g2 (`Oled_1_3`).  The header is always
`4WD CAR`, followed by `MOD-MM.1:<display>`.  The body depends on
the active mode and whether it is a stub (coming-soon) mode.  Redraws
are throttled; mode changes and "Stopped"/"Failsafe" frames are never
dropped (content-change is tested before the throttle).

**Mode taxonomy (fleet registry, v1.5.0)**

| Token    | Live?   | Display          | Notes                            |
|----------|---------|------------------|----------------------------------|
| 4WD4M    | live    | 4WD4M            | WAS `BT`/`BLUETOOTH` pre-X-8     |
| ESP_SER  | live    | Server (ESP_SER) | WiFi web-server drive            |
| ESP_CLI  | stub    | Client (ESP_CLI) | coming-soon                      |
| PATH     | stub    | PATH             | coming-soon                      |
| OBS_US   | stub    | OBS_US           | coming-soon                      |
| OBS_IR   | stub    | OBS_IR           | coming-soon                      |
| MAN      | stub    | MAN              | coming-soon (RF-only)            |
| AUTO     | stub    | AUTO             | coming-soon                      |
| 2WD1M    | stub    | 2WD1M            | coming-soon (owner parked it)    |

Legacy alias: old controllers emit `BT` on the wire → normalised to
`4WD4M` everywhere in the fleet (receivers MUST accept both forever).

---

## 1. Boot splash

```
+----------------------------------+
|          4WD CAR            |
|----------------------------------|
|       MOD-MM.1:1.5.0             |
|                                  |
|        Genum_4WD4M_CAR        |
|   GENUM SOLUTIONS PVT. LTD.      |
+----------------------------------+
```

Shown for ~1400 ms at boot via `Oled_1_3::begin()` (clearBuffer → centered FW_NAME
6x13B → `v<VERSION>` 5x8 → FW_VENDOR → sendBuffer → delay(1400) → framed).
After the delay, `setup()` enters the restored mode and the normal framed view takes over.

## 2. 4WD4M view (was BLUETOOTH pre-X-8)

```
+----------------------------------+
|          4WD CAR            |
|----------------------------------|
|        MOD-MM.1:4WD4M            |
| Status: Forward                  |
| Speed: 170                       |
| Bluetooth: Connected             |
+----------------------------------+
```

- `Status` scrolls if longer than the row width.
- `BT Connected` / `BT Not Connected` on the bottom line.
- 4WD4M is the **only** live BT drive mode (ESP_SER is the only live WiFi drive mode).

## 3. ESP_SER view (WiFi web-server)

```
+----------------------------------+
|          4WD CAR            |
|----------------------------------|
|       MOD-MM.1:Server            |
| Status: Forward                  |
| Speed: 210     ████              |
| WiFi: Connected      -58 dBm    |
| 192.168.4.1                      |
+----------------------------------+
```

- Heading 2 hard-coded to `Server` (not the raw token).
- Right-edge signal-strength bars (0–4 filled, derived from RSSI %)
  appear on the Speed row.
- RSSI string drawn bottom-right; IP address bottom-left.
- Both are only shown while `extraBuf` (pushed by `WebServerComm`) is
  non-empty.

## 4. Stub modes (COMING SOON)

All stub modes share the same OLED frame:

```
+----------------------------------+
|          4WD CAR            |
|----------------------------------|
|      MOD-MM.1:OBS_US             |
|                                  |
|          COMING SOON             |
+----------------------------------+
```

- Heading 2 always shows the mode's display name (registry token).
- Body: **only** a large centred `COMING SOON` (`u8g2_font_9x15B_tr`,
  ~100 px centred in 128 px → comfortable margins).
- No status line, no speed, no "(stub)" suffix — the top bar already
  carries the mode identity (T-27 owner decision 2026-09-14).
- Driving commands are refused; `S` (stop) still accepted and resets
  the failsafe clock.

Modes shown this way: **ESP_CLI, PATH, OBS_US, OBS_IR, MAN, AUTO, 2WD1M**.

## 5. Failsafe stop

```
+----------------------------------+
|          4WD CAR            |
|----------------------------------|
|        MOD-MM.1:4WD4M            |
| Status: Failsafe Stop !!!        |
| Speed: 0                         |
| Bluetooth: Connected             |
+----------------------------------+
```

- The status word becomes `Failsafe Stop !!!` (three trailing exclamation
  marks, from `checkFailsafe()`).
- Failsafe fires after 2 s of command silence in drive modes.
- `S` always accepted and clears the failsafe immediately.
- In stub modes failsafe still runs — `checkFailsafe()` is not gated
  on `isCurrentModeStub()`.

## 6. Disconnected state

```
+----------------------------------+
|          4WD CAR            |
|----------------------------------|
|        MOD-MM.1:4WD4M            |
| !!! Ready !!!                    |
| Speed: 170                       |
| Bluetooth: Not Connected         |
+----------------------------------+
```

- When `statusBuf` is empty or `"Idle"`, the OLED renders
  `!!! Ready !!!` instead (line 104–105 of `Oled_1_3.cpp`).

## 7. Boot restore behaviour

- The car restores the last **mode** and **speed** on boot (Preferences
  namespace `botcfg`, keys `mode`/`spd`) and enters the same mode/speed
  it had before power-off.
- The remote restores its own copy, then re-syncs over Bluetooth
  (`REQ_STATE` → `STATE;MODE=..;SPD=..`) as soon as it connects,
  so both sides always converge on the same state.
