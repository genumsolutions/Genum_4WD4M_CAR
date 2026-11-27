# Bench-readiness snapshot — 2026-11-27 (taken while 2WD1M flash is running)

This is a convenience snapshot for when you return to the bench. It is not a new fix.
It records what is pushed, what is green on disk, and the exact device order to run.

## Pushed firmware (verified local HEAD == origin/main)

### 2WD1M — Genum_2WD1M_CAR_NEW
- main = bf0f6ee
- FW_VERSION = 2.0.6 (Config.h)
- local HEAD == remote main: YES
- tree clean: YES

### 4WD4M — Genum_4WD4M_CAR
- main = 19f0d41
- FW_VERSION = 1.3.24 (Config.h)
- local HEAD == remote main: YES
- tree clean: YES

## What this push contains

### 2WD1M 2.0.6 (U-143 + U-144)
- U-143a: FACTORY;RESET now resets the webpage setting too (pageOff_ = false).
  Before this, a muted page stayed muted until the next power cycle.
- U-143b: boot OLED only names the saved router when auto-join is ON.
  With auto-join OFF it shows the own hotspot from the first frame.
- U-144: residue cleanup only — stale BT <-> SERVER header line removed,
  retired ESP_CLI page note removed, wrong auto-join-OFF wipe comment removed,
  dead KEY_TRIM / KEY_STEER_LIMIT literals replaced with the constants in
  ParamsStore.cpp, boot log corrected from BT to 2WD1M.

### 4WD4M 1.3.24 (U-143 + U-144)
- same U-143a and U-143b fixes, same shape
- U-144: same kind of residue cleanup — stale BT <-> SERVER header removed,
  retired ESP_CLI page note removed, wrong auto-join-OFF wipe comment removed,
  duplicate pgmspace.h include removed from WebServerComm.cpp, boot log
  corrected from BT to 4WD4M.

## Local gates checked on disk (this session)

### 2WD1M
- targeted .ino syntax gate: PASS 1 / 1 (Genum_2WD1M_CAR_NEW.ino), core 4.0.0, exit 0
- full 10-unit syntax sweep: timed out this run (exit 124), did NOT complete in time.
  The .ino itself parses (targeted run above). Treat the full sweep as "not re-confirmed
  this session" rather than "failed".
- residue cleanup: .tools/cmdline.zip deleted and confirmed gone.

### 4WD4M
- targeted .ino syntax gate: PASS 1 / 1 (Genum_4WD4M_CAR.ino), core 4.0.0, exit 0
- full 9-unit syntax sweep: not re-run this session.

### App (genumsolutions-app/mobile)
- npx tsc --noEmit: exit 0
- vitest (reconnectPrompt, resetCarData, linkCapacity, oneActiveLink, carSilence):
  Test Files 5 passed (5), Tests 93 passed (93)

## Flash note (by design)

The version changed on each flash, so the first boot after each flash wipes the saved
routers and transport toggles. First boot is AP-only on purpose.
Re-add the router on the bench (RUNSHEET Step 5).

## Device test order (from TRACKS/DEVICE-TESTS.md)

Run U-143 FIRST, then U-142. U-143 leaves the car clean for U-142.

### 2WD1M — flash 2.0.6 (bf0f6ee)

U-143 (do first):
- U-143-1 (star): turn page OFF (WIFICFG;WIFI;PAGE;OFF), then FACTORY;RESET.
  Same session /status reads "page_on":1 and http://192.168.245.1/ serves the real page.
- U-143-2 (star): save a router, WIFICFG;AUTOJOIN;OFF, cold-boot -> OLED must show
  WiFi : 2WD1MCar_WiFi from the first frame, never the saved router / IP : joining Ns.
  Then AUTOJOIN;ON, cold-boot again -> names the saved router while joining, settles on
  the joined SSID. Second half needs a live router.

U-142 (run on 2.0.6 after U-143):
- U-142-1 (star): Bluetooth pairs AND connects, then drives.
- U-142-2 (star): after Bluetooth disconnects, the own hotspot joins.
- U-142-3 (star): dead socket noticed, hotspot returns WITHOUT a power cycle.
  Within ~35 s serial should print:
  [LINK] no live client - both methods open again
  and /status reads "link_owner":"both" and "link_cap":1.
  Do NOT power-cycle to make this pass.
- U-142-4: link comes back after the recovery — reconnect over Bluetooth and drive again.
- U-142-5 (warn): idle live link must NOT be declared stalled — leave BT connected and
  alone for 90 s, must stay up (no [LINK] open again, no [BT-WD] Hard restart).
- U-142-6: with BT connected /status reads "link_owner":"bt" and "link_cap":0.
  After U-142-3 it reads "both" / 1.
- U-142-7: /status "fw" reads 2.0.6 (and OLED header agrees).

### 4WD4M — flash 1.3.24 (19f0d41)

U-143 (do first):
- U-143-1 (star): turn page OFF (WIFICFG;WIFI;PAGE;OFF), then FACTORY;RESET.
  Same session /status reads "page_on":1 and http://192.168.245.1/ serves the real page.
- U-143-2 (star): save a router, WIFICFG;AUTOJOIN;OFF, cold-boot -> OLED must show
  WiFi : 4WD4MCar_WiFi from the first frame, never the saved router / IP : joining Ns.
  Then AUTOJOIN;ON, cold-boot again -> names the saved router while joining, settles on
  the joined SSID. Second half needs a live router.

U-142 (run on 1.3.24 after U-143):
- U-142-1 (star): first connect responds, and so does the second — connect, drive,
  disconnect, connect again, drive again.
- U-142-2 (star): dead socket noticed, own hotspot returns WITHOUT a power cycle.
  Within ~35 s serial should print:
  [LINK] no live client - both methods open again
  and /status reads "link_owner":"both" and "link_cap":1.
- U-142-3: link comes back after the recovery — reconnect over Bluetooth and drive again.
- U-142-4 (warn): idle live link must NOT be declared stalled — leave BT connected and
  alone for 90 s, must stay up (no [LINK] open again, no [BT-WD] Hard restart).
- U-142-5: with BT connected /status reads "link_owner":"bt" and "link_cap":0.
  After U-142-2 it reads "both" / 1.
- U-142-6: /status "fw" reads 1.3.24 (and OLED header agrees).

## What is still owed (cannot be done from here)

- flash each car with the pushed main build
- run the U-143 rows, then the U-142 rows, on each car
- after booth passes, update TRACKS/TODO.md in each repo:
  2026-11-27 U-143 round: WIP -> DEVICE -> DONE, with the firmware version confirmed on the car

## Logs captured this session

- /tmp/syntax-2wd-ino.log — 2WD .ino targeted syntax gate
- /tmp/syntax-4wd-ino.log — 4WD .ino targeted syntax gate
