# Owner audit — 2026-11-27 (2WD1M new car + 4WD4M car)

Written **before** any code change in this round, as instructed. Every row says
what the code does TODAY, where, and the verdict. Source-level evidence only:
nothing here is a flash/bench result (F-61).

Cars reviewed line by line: `Genum_2WD1M_CAR_NEW` (all `.ino/.cpp/.h`, page,
tools) and `Genum_4WD4M_CAR` (same files, diffed against the 2WD tree). The two
trees are the same code base with the LIVE-mode registry flipped and the 2WD
steering-servo/trim path added; every functional difference found was one of
those two deliberate ones plus identity strings (see § Parity).

---

## 1. Boot display: blink both ways in, pin the live one

| Claim | Code today | Verdict |
| --- | --- | --- |
| On boot the last two OLED rows alternate Bluetooth ⇄ Wi-Fi | `Oled_1_3::advanceRotationPhases()` (`Oled_1_3.cpp:139`), called from `tick()` every loop, repaint every `ROTATE_INTERVAL_MS = 1000` | OK |
| Blinking is driven even when nothing else changes | `ModeManager::run()` calls `oled_->tick()`; `tick()` repaints only on a visible flip | OK (this was S-01/U-107b) |
| When one method connects, the blinking stops and only that method is shown | `redrawNow()` link branch (`Oled_1_3.cpp:403`) keyed on `linkConnected` + `linkMethodBuf` from `setLinkContext()` | OK (S-01) |
| The other method is turned off when one connects | `arbitrateLinkOwnership()` (`Genum_2WD1M_CAR_NEW.ino:419`): Wi-Fi live ⇒ `btComm.stop()`; BT live ⇒ `webServerComm.suspendAP()` | OK (U-129; both reopen after `LINK_OWNER_HOLD_MS` = 4 s with no client) |

## 2. State survives a power cycle, including the router

| Claim | Code today | Verdict |
| --- | --- | --- |
| Mode + speed restored on boot | `ModeManager::applyBootState()` (`ModeManager.cpp:1017`) via `ParamsStore` (`botcfg/mode`,`spd`), validated/clamped, refuses non-LIVE modes | OK |
| Steering trim + steer limit restored | `applyBootState()` → `loadDriveTrim()` / `loadSteerLimit()` (2WD tree; the 4WD tree has no servo/trim by design) | OK |
| Web page ON/OFF remembered | `loadStoredWifi()` reads `botcfg/page_off` (`WebServerComm.cpp:528`); `served` path gates on `pageOff_` | OK |
| "Remember last router" remembered | `loadStoredWifi()` reads `boot_autojoin`; defaults **ON** (T4) | OK |
| Next boot shows/joins the LAST saved router, not another | `armBootAutoJoin()` (`WebServerComm.cpp:2260`) targets `storedSsid_`; `updateExtraInfo()` paints `AP:<saved ssid>,joining <n>s` during the boot join window | OK for AUTOJOIN **ON** — see Finding 2 for the OFF case |
| A version change wipes saved routers | `wipeRoutersOnFreshFirmware()` (U-98) — deliberate; re-add on the bench after a flash | By design |

## 3. Reset car button

| Claim | Code today | Verdict |
| --- | --- | --- |
| Wipes stored data + settings to defaults | `handleFactoryCommand()` `FACTORY;RESET` (`WebServerComm.cpp:311`) clears the whole `botcfg` namespace, restores RAM credentials/routers/toggles/autojoin, resets crash counters | OK |
| Running session returns to defaults immediately | `.ino` system hook keyed on `FACTORY;CLEARED` calls `ModeManager::factoryResetState()` (`ModeManager.cpp:966`), which shares `resetHeldDriveState()` with link loss | OK |
| **Every** setting returns to its default | `pageOff_` (the webpage ON/OFF setting) is **not** reset in RAM; it is only wiped in NVS | **Finding 1 — fixed this round** |

## 4. Disconnect button / prompt / reconnect

| Claim | Code today | Verdict |
| --- | --- | --- |
| Disconnect control on the left, fixed, right of Back | app `RemoteControlScreen.tsx` (`testID="remote-disconnect"`), committed `c9fdb12` | OK (app repo) |
| App notices a drop pronto and offers reconnect / cancel / go back | app `linkPrompt.ts` + `useControlHub.ts` (`reconnectDue`, `handleReconnectConfirm`, `handleReconnectPromptCancel`); one prompt, no duplicate | OK (verify on device) |
| Car notices a drop promptly | `BluetoothComm::connected()` (`BluetoothComm.cpp:263`) = attached **and** still accepting TX; `ws.connectedClients()` for Wi-Fi; `ModeManager::checkFailsafe()` true→false edge calls `onLinkLost()` which stops the drive and repaints in the same tick | OK (U-142/T3) |
| After a drop the car reopens BOTH links | `arbitrateLinkOwnership()` releases the park after `LINK_OWNER_HOLD_MS`; `setOtherLinkParked(false)` clears LINKCAP | OK |
| Router state + broadcast IP shown properly | `/status` `ssid`,`want`,`ap`,`station_ip`,`ap_ip`,`broadcast_ip`,`subnet`; OLED `WiFi : <ssid>` / `IP : <ip>` | OK |
| 4WD4M "connects once, then doesn't respond" | SPP liveness now TX-progress (`sppWedged()`, `BT_SPP_TX_STALL_MS = 30 s`), so a latched `hasClient()` reports down, the AP park releases and the BT restart watchdog can fire | Fixed in code (U-142), unflashed |

## 5. Car page

| Claim | Code today | Verdict |
| --- | --- | --- |
| Data overflowing on the right | landscape `.side span` / `.side b` now `min-width:0`, `overflow:hidden`, ellipsis; range input `min-width:0` (`WebPage.h`, commit `11fd9bc`) | OK |
| D-pad at the standard thumb position in landscape | `.pads{justify-content:space-between}` + `.pad{flex:1}` — each cross at its half's outer edge (S-16), portrait shows the rotate notice only | OK as ruled |
| Speed slider aligned + car scale | `min=100 max=255 step=5`, same numbers as firmware `MIN_SPEED..MAX_SPEED`; thumb is clamped inside the input box by the browser, value/limit columns are fixed-width | OK |

## 6. App remote screen connectivity

| Claim | Code today | Verdict |
| --- | --- | --- |
| Confusing, dull, sometimes unusable controls | `ConnectionSection.tsx` now names which radio owns the link and which is paused, and pairs a refused link with the one action that ends the pause (disconnect the owner). Committed `f1d0742`, `c9fdb12` | OK (app repo) |
| Speed slider shares the car's scale | app `SPEED_MIN = 100`, `SPEED_MAX = 255` (`carProtocol.test.ts` asserts both) and `quantizeSpeedToStep` used by slider + command | OK |

---

## Findings (the only code changes this round)

### Finding 1 — `FACTORY;RESET` does not return the webpage setting to its default

`handleFactoryCommand()` wipes NVS, but the live `pageOff_` flag keeps its
pre-reset value. If the owner had the page OFF and presses Reset, the page stays
OFF until the next power cycle even though the wipe's whole promise is "back to
defaults" (default is ON). This is a setting the owner named explicitly.

**Fix:** set `pageOff_ = false` in the FACTORY;RESET RAM restore, next to
`wifiEnabled_` / `btEnabled_` / `bootAutojoin_`. Both trees, same edit.

### Finding 2 — the boot OLED names a saved router even when auto-join is OFF

`updateExtraInfo()` shows `AP:<stored ssid>,joining <n>s` while
`apRunning_ && bootJoinPending_ && hasStoredWifi_`, without consulting
`bootAutojoin_`. With "remember last router" OFF the car intentionally stays on
its own hotspot, but the panel claims it is joining the saved router for the
15 s quiet window. The owner's rule is conditional: show the last router **if**
that setting is on.

**Fix:** clear `bootJoinPending_` when `!bootAutojoin_`, and require
`bootAutojoin_` in the placeholder branch. Both trees.

---

## Parity note

`diff --strip-trailing-cr` on every shared source file shows the differences are
exactly: the LIVE/CS registry flip (`ModeManager.cpp`), the 2WD steering servo +
trim/steer-limit path (`ModeManager.*`, `Config.h`, `ParamsStore.*`,
`SteeringServo.*`, `TwoWD1M.*`, `L298NControl.cpp`, `Oled*`, `WebPage.h`), and
identity strings. `BluetoothComm::connected()`'s U-142 liveness rule is present
in both trees (2WD has the longer history comment). No unexplained drift.

## Gates to run after the fix

`node tools/check-firmware-contract.mjs`, `node tools/check-webpage-js.mjs`,
`node tools/check-firmware-syntax.ps1` in both trees. The contract rule
`page-state` reads `page_off`; `boot-hop-off` reads the auto-join default — both
edits stay inside what those rules already pin.

---

## Result (after the change)

Both findings fixed identically in both trees, with a CHANGELOG entry each.
A follow-up residue pass (U-144) removed stale header/comment text, the dead
`KEY_TRIM`/`KEY_STEER_LIMIT` literals in the 2WD tree, a duplicate `<pgmspace.h>`
in the 4WD tree, and a boot log that named the wrong default mode. No behaviour
changed in U-144.

Final versions: 2WD1M **2.0.6**, 4WD4M **1.3.24**.

| gate | 2WD1M new | 4WD4M |
| --- | --- | --- |
| `check-firmware-contract.mjs` | 0 failures | 0 failures |
| `check-webpage-js.mjs` | all PASS | all PASS |
| `check-firmware-syntax.ps1` | 10/10 TUs | 9/9 TUs |
| real compile (core 4.0.0, huge_app) | 1,901,870 B / 81,456 B RAM, 0 warnings | 1,899,054 B / 81,416 B RAM, 0 warnings |

Not done, and cannot be done from here: flashing either car and running the
on-device acceptance rows. Gates and compiles prove source shape only (F-61).
Status in both `TRACKS/TODO.md` files is `WIP`, never `DONE`.
