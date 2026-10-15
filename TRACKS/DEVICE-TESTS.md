# DEVICE-TESTS — Genum_4WD4M_CAR

> Owner-at-the-bench sheets. Test order per standing rule: physical remote first — EXCEPT this
> car, until O-3 (remote pairing update for `4WD4M Car`) lands → app-first for round 1.

## U-112 round — reset while pairing BT + own hotspot · freeze using the page with the app connected (2026-10-14, firmware 1.3.11 — DEVICE VERIFY PENDING)

> Flash **1.3.11** (supersedes 1.3.9, which IS on the owner's car). Root-cause
> round for the post-1.3.9 report; the field record showed free_heap 13-18 kB.
> Run sheet: `TRACKS/RUNSHEET.md`. Watch the serial for the new `[HEAP]` /
> `[SCAN] refused` lines — they are this round's evidence.

- [ ] **U-112-1 — ⭐ Bluetooth + own hotspot together, no reset.** Power the
      car (1.3.11), pair BT, and join `4WD4MCar_WiFi` on the same phone.
      Both links live for >= 5 min: the car does NOT reset, the app stays
      connected on BT, and the crash counters in `/status` do not move
      (previously: reset at crash 757 during this exact sequence).
- [ ] **U-112-2 — ⭐ the page + the app, no freeze.** App connected over Wi-Fi
      (live telemetry updating); open `http://<car-ip>/` in the phone
      browser and load the page repeatedly. The app's telemetry keeps moving
      WHILE the page loads (no multi-second freeze), the page renders
      completely, and the car does not reset.
- [ ] **U-112-3 — an undrained reader cannot own the loop.** With the app
      connected, open the page and switch away mid-load (browser stops
      reading): the serial shows `[HTTP] root page paused (no client
      progress after 2 attempts)` within ~a second and the app keeps
      receiving STATE. Reloading the page still delivers it in full.
- [ ] **U-112-4 — the scan answers honestly on a low heap.** Tap Search for
      networks while the app is connected: either the list appears, or the
      app shows BUSY within a moment (`ROUTERS;SCAN;BUSY`) — never an 8 s
      wait ending in an EMPTY list with no reason. Serial shows
      `[SCAN] refused: low heap` when refused.
- [ ] **U-112-5 — telemetry degrades under pressure, never black-holes.**
      Serial under load (page + app + scan): `[HEAP] status broadcast
      deferred (free=…)` may appear, and is always followed by
      `resumed (free=…)` within <= 4 s while the load continues. The app's
      telemetry never goes silent for longer than a few seconds.
- [ ] **U-112-6 — if it STILL resets or freezes: capture the serial.**
      `python tools/serial_capture.py COM<port> 60 u112.log` across the
      event, and report the `[CRASH] rtc: …` line (phase + task + heap) from
      the next boot. That line names the operation; without it the freeze
      cannot be pinned (every historical capture predates the breadcrumb).
- [ ] **U-114-1 — `/status` reports the truth (the U-114 fix).** Open
      `http://<car-ip>/status` and read `tele_params` — it must be
      `freq=1600, vyse=0` (not `freq=0`/empty). Then toggle Wi-Fi / Bluetooth /
      boot-auto-join in the app and reload `/status`: `wifi_enabled`,
      `bt_enabled` and `boot_autojoin` must match what you toggled. Before this
      fix they reported a neighbour's value (`1600`/`0`), which the app then
      ADOPTED — so also confirm the app's Connectivity panel still shows the
      toggles you set, not flipped ones.

## U-111 round — real Bluetooth pairing + a hotspot that joins + car↔app state round-trip (2026-10-14, firmware 1.3.9 — DEVICE VERIFY PENDING)

> Flash **1.3.9** (supersedes 1.3.8 / 1.3.7 / 1.3.6 — none flashed; 1.3.9 also
> deletes the Internet/relay method, U-111b). Third pass on the
> owner's report: Bluetooth "appears but won't pair", the car's own hotspot
>
> Step-by-step run sheet for this pass: `TRACKS/RUNSHEET.md`.
> sometimes won't connect, flaky Wi-Fi with an occasional freeze — plus the
> app↔car settings round-trip the owner asked for.
>
> **Before testing, forget the old `4WD CAR` bond AND any stale `4WD4MCar_WiFi`
> profile** (Android caches both). Flashing 1.3.8 is a version change, so the car
> wipes its saved routers + transport toggles on first boot (clean start, both
> transports ON, auto-join ON).

- [ ] **U-111-1 — ⭐ Bluetooth actually PAIRS now.** Forget `4WD CAR` in Android
      Settings → Bluetooth. Power the car (1.3.8) and pair to `4WD4M Car`:
      **no PIN is asked** (SSP "Just Works"); if a PIN is ever requested it is
      `1234`. The bond completes and the app's Bluetooth link opens. The old build
      answered Android's PIN request with an EMPTY PIN, so this never worked.
- [ ] **U-111-2 — ⭐ the phone joins `4WD4MCar_WiFi` first try.** Forget/re-add the
      network once (Android caches a stale profile against an open AP). The car's
      hotspot appears within ~5 s of boot and the phone gets `192.168.245.x`;
      `http://192.168.245.1/status` answers.
- [ ] **U-111-3 — no AP retry storm in the log.** With no router saved (STA down),
      the boot log shows the own AP come up once (`Own AP active: …`) and does NOT
      repeat the softAP bring-up every pass.
- [ ] **U-111-4 — ⭐ the app shows the car's settings on connect (the round-trip).**
      Connect (BT or Wi-Fi) with a FRESH app profile: the Wi-Fi / Bluetooth /
      Boot-auto-join toggles show the car's real values immediately (both ON by
      default after the 1.3.8 flash) — the app no longer shows "don't know".
- [ ] **U-111-5 — ⭐ a change round-trips and survives a power cycle.** Toggle
      Boot-auto-join OFF in the app; the car confirms and the app shows OFF.
      **Power-cycle the car** — it returns with auto-join OFF (persisted). Turn it
      back ON. (Wi-Fi/Bluetooth likewise; both-off is clamped to Wi-Fi ON so the
      car can never vanish.)
- [ ] **U-111-6 — freeze: capture the reset reason + phase (STILL OPEN).** If the
      car still freezes, reconnect and read the app/OLED restart reason, reset count
      and **crash phase** (U-106 breadcrumb). This is the evidence that names the
      cause; U-111 removes one plausible cause (the AP retry churn) but does NOT
      claim the freeze fixed.

- [ ] **U-111-7 — the Internet method is GONE (U-111b, 1.3.9).** The OLED's
      link rows only ever read `Bluetooth : …` / `WiFi : …` (never `Internet`
      / `WSS`), the boot log has no `[RELAY]` lines, `grep -ri relay` over the
      sources is empty, and the app offers exactly TWO methods (Bluetooth,
      WiFi (LAN)) with no Internet pill anywhere. Nothing dials a cloud relay.
## U-110 round — OLED format + Bluetooth pairing + honest BT state (2026-10-14, firmware 1.3.7 — DEVICE VERIFY PENDING)

> Flash **1.3.7** (supersedes 1.3.6 — never flashed). From the owner's post-flash
> report: OLED last-two-lines layout, Bluetooth "appears but won't pair", flaky
> Wi-Fi with an occasional manual-restart freeze, and a stale app reconnect
> prompt.

- [ ] **U-110-1 — ⭐ the OLED last-two-rows read exactly this.** With both
      transports ON the two frames alternate ~1 s:
      `Bluetooth : 4WD4M Car` / `Type : SPP`, then `WiFi : <ssid>` /
      `IP : <car IP>`. With a single transport enabled only that frame shows.
- [ ] **U-110-2 — ⭐ the phone pairs to `4WD4M Car`.** **FORGET the old `4WD CAR`
      pairing first** (the name changed). The car appears in the BT list as
      `4WD4M Car`; pairing succeeds (PIN 1234 if asked) and the app's Bluetooth
      link opens.
- [ ] **U-110-3 — the BT stack is up, or the log says so.** At boot, either
      `[POWER] Bluetooth advertising…` prints, or `[BT] begin refused` followed
      by a retry (the stack is genuinely up — not a masked failure).
- [ ] **U-110-4 — Wi-Fi freeze: capture the reset reason + phase.** If the car
      still freezes/restarts on Wi-Fi, reconnect and read the app/OLED restart
      reason, reset count and **crash phase** (U-106 breadcrumb). This is the
      evidence that names the cause — it is NOT claimed fixed in 1.3.7.
- [ ] **U-110-5 — the app no longer nags while connected.** With the car live on
      its Wi-Fi link, the "Connection lost / Reconnect to your car?" prompt does
      NOT appear; if the link really drops it appears as a CENTERED modal
      (dimmed backdrop in the middle of the screen), not a bottom banner.
- [ ] **U-110-6 — the car-restart prompt is also centered.** After a real restart
      the "Car restarted" prompt shows as a centered modal with
      Reconnect / Leave / Cancel.

## U-109 round — the car's own hotspot comes up at boot (2026-10-14, firmware 1.3.6 — DEVICE VERIFY PENDING)

> Flash **1.3.6** (supersedes 1.3.5 — never flashed). Benchtop capture with
> `python tools/serial_capture.py COM7 20 <out.log>`. The bug: the boot softAP was
> skipped (`[RADIO] skip-softAP/sta-busy`) and `/status` `ap` read `""`, so the phone
> could neither join the hotspot nor be found by the app's discovery sweep.

- [ ] **U-109-1 — ⭐ the own hotspot is actually up after boot.** Power-on 1.3.6, wait ~5 s:
      in the phone's Wi-Fi list `4WD4MCar_WiFi` appears **without** the car ever having
      joined a router. (No `[RADIO] skip-softAP/sta-busy` line at boot; the bring-up line
      `Own AP active: 4WD4MCar_WiFi` prints.)
- [ ] **U-109-2 — `/status` `ap` is non-empty.** GET `http://192.168.245.1/status` (phone on
      the car's hotspot): `"ap":"4WD4MCar_WiFi"` — not `""`.
- [ ] **U-109-3 — the app finds the car with no router involved.** Open the app's
      Connect/Link screen with the phone on `4WD4MCar_WiFi`: the discovery sweep matches the
      car by own-AP name.
- [ ] **U-109-4 — boot log no longer prints the LEDC errors.** The `E ledc: LEDC is not
      initialized` lines that followed `[BOOT] Restored mode=…` are gone (the motor guard).
- [ ] **U-109-5 — a retired stored mode cannot boot the car into COMING SOON.** (Refit/testbed
      only) with a donor-era `mode` in NVS, the boot log says
      `[BOOT] Restored mode=… is not LIVE in v2 - keeping 4WD4M` and the OLED/panel shows 4WD4M.

## U-106 round — crash-TIME evidence survives a PANIC (2026-10-13, firmware 1.3.6 — DEVICE VERIFY PENDING)

> Flash **1.3.6** (supersedes 1.3.5 — never flashed). The bench car is TETHERED on **COM7**;
> capture with `python tools/serial_capture.py COM7 20 <out.log>` (a reset shows the banner).
> Goal: a PANIC reports the **operation + task + heap-at-crash** instead of `lastphase=''`.
> This is the fix for the field crash table (board `1FB608`: ~754 `PANIC`, every phase empty).

- [ ] **U-106-1 — the boot banner names the last crash's operation.** Power-on 1.3.6, capture 20 s:
      the `[CRASH]` line still prints, and when the record carries RTC data a second line
      `[CRASH] rtc: marks=… phase='…' task='…' heap=… minHeap=… atMs=…` follows it.
- [ ] **U-106-2 — ⭐ reproduce the reset; the phase is no longer empty.** Drive over Bluetooth, then
      connect a second phone over BT (the reported restart trigger). After the reset the banner's
      `rtc:` line names a real phase (`bt-read` / `bt-begin` / `ws-evt`) and the task that died —
      NOT `phase=''` — plus the heap at that instant.
- [ ] **U-106-3 — the Wi-Fi connect path is named too.** Trigger the Wi-Fi freeze (switch/select a
      saved router, open the page): the reset names `wifi-rejoin` / `wifi-connect` / `web` and the
      crash heap.
- [ ] **U-106-4 — `/status` carries the crash-TIME fields.** GET `http://192.168.245.1/status`:
      `last_crash_phase` matches the serial `rtc:` phase, and `last_crash_heap_at_crash`,
      `last_crash_heap_min`, `last_crash_task`, `last_crash_millis`, `live_min_heap` are present
      (non-zero on a crashed car).
- [ ] **U-106-5 — the heap low-water is believable.** On a car that has run a while,
      `live_min_heap` / `last_crash_heap_min` fall well below the ~176 KB boot heap — the ~12 KB
      the DB reported is the suspicion this round exists to confirm.

## U-107 round — OLED last-two-lines rule (2026-10-13, firmware 1.3.6 — DEVICE VERIFY PENDING)

> Same 1.3.6 flash as U-106. The last two OLED rows follow the **U-105 Wi-Fi / Bluetooth
> toggles** (set them from the app's Connectivity panel or `WIFICFG;WIFI;ON|OFF` /
> `WIFICFG;BT;ON|OFF`). Watch the panel; each frame (~1 s).

- [ ] **U-107-1 — both ON → BT ⇄ Wi-Fi alternate.** With both toggles ON, the last two rows
      alternate "Bluetooth / <advertised name>" ⇄ "Wifi / SSID:<name>" / "IP:<ip>" about once a
      second.
- [ ] **U-107-2 — the Wi-Fi frame's last line alternates name ⇄ IP.** In the Wi-Fi frame the
      bottom line flips "SSID : <name>" ⇄ "IP : <ip>" ~1 s.
- [ ] **U-107-3 — one ON → that frame only.** Turn Bluetooth OFF: the rows stay on the Wi-Fi frame
      with no alternation. Turn Wi-Fi OFF instead: the rows stay on the Bluetooth frame.
- [ ] **U-107-4 — the advertised BT name is real.** The Bluetooth frame shows the name the radio
      actually advertises (`Config.h` `BT_SPP_NAME`, "4WD CAR") — one source, not a second literal.
- [ ] **U-107-5 — a relay (Internet) link keeps its own frame.** With a relay configured and
      connected, the rows show "Internet : WSS" + IP (no BT/Wi-Fi alternation).

## U-108 round — fresh start + timestamps + auto-join labels + Reset car data (2026-10-13, firmware 1.3.6 — DEVICE VERIFY PENDING)

> Same 1.3.6 flash as U-106/U-107. The car has `FACTORY;RESET` (wipes NVS + counters),
> `TIME;<epoch>` (app clock sync), `TIME?` (reads epoch + config_epoch), and
> `botcfg/config_epoch` stamped on config writes. The app has `resetCarData()` (sends
> FACTORY;RESET + wipes local storage) and a red "Reset car data" button in the
> Remote settings panel. The two auto-join toggles are now clearly labelled:
> "Boot auto-join (car)" vs "Auto-join saved router (app)".

- [ ] **U-108-1 — FACTORY;RESET wipes the car clean.** Send `FACTORY;RESET` (app button or
      raw line): reply `FACTORY;CLEARED`. After a power-cycle: the car boots on its own
      hotspot `4WD4MCar_WiFi`; STATE shows `SSID=4WD4MCar_WiFi` (no router); STATE `;WIFI=ON`,
      `;BT=ON`, `;AUTOJOIN=ON`; OLED shows the AP IP `192.168.245.1`; saved router list
      (`NETW;`) contains only `4WD4MCar_WiFi`; the boot/crash counters are `boot=1`,
      `crash=0`; the crash record is dropped (`reset_reason` gone).
- [ ] **U-108-2 — TIME;<epoch> + TIME? work.** With the car connected, the app sends
      `TIME;<unixEpoch>` on every connect. Send `TIME?` (raw): reply `TIME;<now>;<configEpoch>`
      where `now` = epoch + elapsed, and `configEpoch` = last config-write stamp. After a
      `WIFICFG;<ssid>;<pass>` store, send `TIME?` again: `configEpoch` has advanced (non-zero,
      greater than before).
- [ ] **U-108-3 — config_epoch stamped on config writes.** Do each: toggle Wi-Fi OFF→ON,
      toggle BT OFF→ON, toggle AUTOJOIN OFF→ON, `ROUTERS;ADD;X;pass`, `ROUTERS;USE;X`,
      `ROUTERS;DEL;Y`, `ROUTERS;CLEAR`. After each, `TIME?` shows a new `configEpoch` (each
      write advances the stamp). `WIFICFG;RESET` also stamps it.
- [ ] **U-108-4 — app Reset car data button works.** In Remote screen → Settings (⚙) panel,
      scroll to the bottom "Reset car data" red button. Tap → destructive confirm dialog
      (`testID: reset-car-data`). Confirm: the car receives `FACTORY;RESET` (serial shows
      `[FACTORY] botcfg namespace cleared`), the app clears all car AsyncStorage keys, and
      shows "Car data reset — power cycle the car for a fresh start" success toast. The
      Remote panel resets to defaults (both toggles ON, no router list, auto-join ON).
- [ ] **U-108-5 — auto-join labels are clear.** In Remote settings panel: the CAR toggle
      reads "Boot auto-join (car)" with `power` icon and caption "On power-up the CAR
      rejoins its last router — off keeps it on its own hotspot." The APP toggle reads
      "Auto-join saved router (app)" with `refresh-cw` icon and caption "When you open
      the car, the APP switches it to the strongest saved router it hears." Distinct
      accessibility labels present. CarProfileCard shows "Auto-join saved router (app)"
      and cross-references the car setting.
- [ ] **U-108-6 — on-connect epoch sync.** On every connect (SPP / BLE / WiFi), the app
      sends `TIME;<epoch>` alongside `REQ_STATE` (capture serial: `TIME;1760000000`
      followed by `REQ_STATE`). The car's `TIME?` reply reflects the adopted epoch.

## U-105 round — transport toggles ride, beacon never parked, one own-hotspot name (2026-10-13, firmware 1.3.5 — DEVICE VERIFY PENDING)

> App half of this round (the Remote "Connectivity" panel: Wi-Fi / Bluetooth / auto-join toggles)
> is the `genumsolutions-app` rebuild (3.2.7, same-version JS-only OTA — force-close the app twice
> to load it). These are the CAR-side rows. Flash 1.3.5 first. The toggle rows can also be driven
> unattended: `python tools/bench_u90_u92.py <car-ip>` runs P4-1..P4-5 (query-only + a BT round
> trip; the Wi-Fi-off half is here because it kills the bench's own web link).
>
> **The old parallel + power-save settings are GONE (`WIFICFG;PARALLEL` / `WIFICFG;APSAVE` are
> not recognised — the app no longer shows the buttons either).** The three toggles are
> `WIFICFG;WIFI;ON|OFF` / `WIFICFG;BT;ON|OFF` / `WIFICFG;AUTOJOIN;ON|OFF; all default ON; Wi-Fi
> and Bluetooth can never BOTH be off.**

- [ ] **U-105-1 — first flash: both transports open at once.** Fresh 1.3.5 boot: serial `[FIRST-BOOT]`
      wipes clean; the own hotspot `4WD4MCar_WiFi` and Bluetooth `4WD CAR` are BOTH up simultaneously
      — a second phone's scan sees the AP while the first drives over BT, and STATE frames arrive on
      both (this is the "parallel" default — always-on, no teardown).
- [ ] **U-105-2 — `WIFICFG;WIFI;OFF` stops the web layer; `;ON` brings it back.** Send
      `WIFICFG;WIFI;OFF` (app toggle or raw) → reply `WIFICFG;WIFI;OFF`. The AP disappears from a
      phone scan and :80/:81 stop answering; Bluetooth keeps working (drive over BT). Send
      `WIFICFG;WIFI;ON` → the AP `4WD4MCar_WiFi` returns. Power-cycle: the setting persists.
- [ ] **U-105-3 — `WIFICFG;BT;OFF` stops Bluetooth; `;ON` brings it back.** Send `WIFICFG;BT;OFF` →
      reply `WIFICFG;BT;OFF`; `4WD CAR` is gone from a phone's BT scan while the web/AP link (and its
      STATE) keeps working. `WIFICFG;BT;ON` restores it. Power-cycle: persists.
- [ ] **U-105-4 — never both OFF.** With Wi-Fi OFF, send `WIFICFG;BT;OFF`: it is REFUSED (reply stays
      `WIFICFG;BT;ON`, serial shows the both-off clamp) so the car can never be left with no
      transport. Power-cycle with both off is impossible by construction.
- [ ] **U-105-5 — toggles survive a drive session + power cycle.** Toggle Wi-Fi OFF mid-session while
      driving over Bluetooth; the car does not drop the BT link and does not reboot (no watchdog
      feed change). Power-cycle: toggle states are exactly as set (STATE `;WIFI=`/`;BT=`/`;AUTOJOIN=`
      and /status `wifi_enabled`/`bt_enabled`/`boot_autojoin` agree).
- [ ] **U-105-6 — boot auto-join toggle.** `WIFICFG;AUTOJOIN;OFF` then power-cycle with a reachable
      saved router: the car boots on its own hotspot and does NOT join the router. `;ON` restores
      the boot hop to the last-switched router.
- [ ] **U-105-7 — ⭐ the own hotspot name is `4WD4MCar_WiFi` EVERYWHERE.** One phone scanning sees
      exactly ONE own network: `4WD4MCar_WiFi` (open, 192.168.245.1). No `4WD4MCar_WiFi`, no
      `WirelessCar_Wifi` on the wire, in the app's registry, in the page dropdown, or on the OLED
      fallback (car with no STA: OLED shows `Wifi : 4WD4MCar_WiFi` / `IP : 192.168.245.1`, never a
      bare name or `192.168.4.1`).
- [ ] **U-105-8 — ⭐ an existing saved router survives the flash-and-settle.** This flash wipes the
      router surface ONCE (fresh install). Add a router + switch to it, power-cycle: the car returns
      to it (auto-join ON) and the router is listed again. Then **delete it with a DIFFERENT CASE**
      (`ROUTERS;DEL;<Name>` vs the case it was saved with) and power-cycle: it stays deleted — the
      case-insensitive delete removed the row AND the active pair (pre-fix it answered "Not saved"
      and the router survived the reboot).

## U-98 round — a flash starts clean, a boot returns to the last-switched router (2026-10-11, firmware 1.3.0 — DEVICE VERIFY PENDING)

> Owner: *"clear all the other saved router datas and only have its own data each time i flash the
> car … the car is clean and fresh always"* · *"the car should always go to the last switched router
> network always; if it is not available it should show its own router …"* · *"i deleted the saved
> router but the car restarted with the old router that i was previously connected"* · *"the car is
> stuck on one of the outside router and not even showing its own webpage"*. **Flash 1.3.0 first** —
> the FIRST boot after this flash must come up with an EMPTY router registry (the `[FIRST-BOOT]` line).

- [ ] **U-98-1 — the first boot after a flash is clean.** Right after flashing 1.3.0 the serial shows
      `[FIRST-BOOT] firmware 1.3.0 is a fresh install - saved router/config NVS erased`. The saved
      router list (`NETW;` / the page dropdown) contains only `4WD4MCar_WiFi`; no pre-flash router
      survives. This runs ONCE — a later power-cycle of the same 1.3.0 wipes nothing.
- [ ] **U-98-2 — same-firmware power cycles KEEP the routers (U-84).** Add a router, power-cycle the
      car (NO re-flash): the router is still saved, no `[FIRST-BOOT]` line, and the boot log shows the
      return-to-router attempt. The wipe must never fire on a plain power cycle.
- [ ] **U-98-3 — boot returns to the last-SWITCHED router.** With a reachable router saved,
      power-cycle: serial shows `[WIFICFG] boot: returning to last-switched router "<ssid>" (full
      budget)` and the car associates (STATE `SSID=` = the router, a real DHCP IP). The own hotspot is
      up the whole time (a phone scan can see `4WD4MCar_WiFi` during the attempt).
- [ ] **U-98-4 — ⭐ the deleted router does NOT come back (the owner's report).** Save router A and
      switch to it, then **delete A** (`ROUTERS;DEL;A`, page or app). Power-cycle. The car must NOT
      return to A — it comes up on its own hotspot (`4WD4MCar_WiFi`, `192.168.245.1`). Pre-fix it
      re-joined A from the surviving last-good pair. Confirm `NETW;` no longer lists A.
- [ ] **U-98-5 — an unreachable last-switched router still ends on the own hotspot.** Save router B,
      switch to it, then turn B OFF. Power-cycle: the car spends the full budget on B, then
      `WiFi STA gave up (router unreachable) - AP-only mode`; `4WD4MCar_WiFi` is joinable and the page
      loads at `192.168.245.1`. It must never sit on B with no reachable page.
- [ ] **U-98-6 — "back to my own hotspot" then power-cycle stays on the own hotspot.** From a router,
      pick `4WD4MCar_WiFi` (page dropdown / app), let it return, then power-cycle: the car comes up on
      its own hotspot and does NOT hop back to the router (the last-good pair was cleared with the
      stored pair). STATE `SSID=` shows the AP / no router association.
- [ ] **U-98-7 — WIFICFG;RESET and ROUTERS;CLEAR wipe the last-good pair too.** With a router set, send
      `WIFICFG;?;RESET` (or `ROUTERS;CLEAR`), then power-cycle: the car stays on its own hotspot;
      nothing re-joins.
- [ ] **U-98-8 — the OLED network row is "pronto" (P2).** With the car on a router, watch the OLED's
      network row as the signal changes (walk away/back): the RSSI %/IP line refreshes within about
      half a second, and it still snaps immediately when the network actually switches (U-72). In the
      page's DevTools, the WebSocket status frame must still arrive about **once** a second — the
      extra-row cadence is 500 ms but the broadcast stays 1 Hz.

## U-95 round — evidence gathering + the reset loop itself (2026-10-06/07, firmware 1.2.3 — DEVICE VERIFY PENDING)

> Owner report was `_the car resets again and again_` with no cause attached. Nothing could name a
> cause: the only reset diagnostic was a Serial line, the bench car has no serial cable, and
> `.trigger_panic = true` meant a hung `loop()` reported `PANIC` instead of `TASK_WDT` — so the one
> label that would have identified it was unreachable. **This round makes the resets legible; it does
> not claim to have cured them.** Commits `883626b` (record), `29b8eaf` (page-send budget + watchdog
> feed), `83ecf15` (idle attribution), `d78c27d` (relay guards).

- [ ] **U-95-1 — flash 1.2.3 and read the record BEFORE anything else.** (Covers 1.2.0's record,
      1.2.1's WebSocket watchdog feeds, 1.2.2's router-switch + scan work, 1.2.3's page/status Phase 3
      — one flash, whole round.)
      After the first boot, open
      the car's page: the header carries a restart row and `/status` carries `reset_reason`,
      `boot_count`, `crash_count`, `last_crash_phase`, `last_crash_heap`, `phase`. Record
      `boot_count` and `crash_count`. A `boot_count` climbing with `crash_count` tracking it means the
      loop is still happening and the record will now say which phase.
- [ ] **U-95-2 — the reset reason is honest.** Force a reset (power-cycle is a *clean* reset and must
      NOT be counted as a crash — that distinction is the point of U-95-4). To see a fault, force a
      real hang and let the watchdog fire; expect `TASK_WDT`, **not** `PANIC`. If the banner still says
      `PANIC`, `trigger_panic` has not been reconfigured — stop and report, do not continue the sheet.
- [ ] **U-95-3 — the page no longer resets the car on a slow client (U-95/F-77).** This was the
      confirmed reset cause: the send budget was 6000 ms against a 5000 ms watchdog, and a client that
      stopped reading reset the car *every time*. Test: connect a phone to the car's own hotspot, load
      the page, and while it loads put the phone in aeroplane mode so the socket stops draining. The
      car must serve a truncated page and **stay up**. Confirm `boot_count` and `crash_count` are
      unchanged afterwards. If the car resets here, F-77 is not fixed — report immediately.
- [ ] **U-95-4 — a clean power cycle must not inflate the crash count.** Power-cycle the car five
      times deliberately while trying something. `boot_count` rises by 5; **`crash_count` must not
      move**, and the restart row must keep showing the *last real fault*, not disappear. If
      `crash_count` rises on clean power-ons, the reason classification is wrong.
- [ ] **U-95-5 — an idle crash is not blamed on the last operation.** Serve the page once (so the
      record is written as `http-root`), then leave the car completely idle and cause a fault. The
      recorded phase must be empty/idle, **not** `http-root`. This is the `83ecf15` fix: the idle
      transition used to be rate-limited away, so a parked-car crash inherited the last operation.
- [ ] **U-95-6 — the relay sheet still CANNOT be run on 1.2.4, by design.** `RELAY_HOST` ships `""` and
      the `armed-handshake` gate refuses any config that arms it while the TLS handshake runs on the
      loop task with a 120 s timeout against a 5 s watchdog (F-84). **The fix is DRAFTED on branch
      `feat/relay-phase2-own-task` (2026-10-10):** the dial moves to its own FreeRTOS task, after which
      arming passes the gate. But that branch is UNCOMPILED and unflashed, so T-REL-1..4 stay blocked on
      `main` until it builds, ships, and `RELAY_HOST` is set. Do not work around the gate on `main`.
- [ ] **U-95-7 — the switch teardown walks all the way to `WiFi.begin()` (1.2.2, Phase 2a).** Car on
      router X, phone on X: pick router Y → `REPLY=ROUTERS;USED;Y`, then serial `[RADIO]` steps in
      order — `rejoin/mode-AP` → (confirmed by `getMode()`, no `rejoin/mode-AP-not-taken` breadcrumb)
      → `rejoin/softAP` → raise → `WiFi.begin` — and the car actually arrives on Y (phone on Y can
      reach `192.168.245.1`). The own hotspot may blink once at the softAP step (beacon restart) but
      returns; it must never stay down. `ROUTERS;USED;Y` alone proves nothing — verify association.
- [ ] **U-95-8 — the retry's disconnect and begin never share a pass (1.2.2, Phase 2b).** Configure a
      router with a WRONG password and switch to it: serial shows `retry/disconnect` on one pass and
      `retry/begin` on a LATER pass (distinct `[RADIO]` breadcrumbs, never back-to-back), the car
      never resets mid-retry, and after a successful session the 10 s backoff has restarted (the
      retry does not fire the instant the step machine starts walking).
- [ ] **U-95-9 — the scan answers honestly (1.2.2, Phase 2c).** Tap scan twice quickly: first tap →
      `Scanning for networks...` (green), second tap → `The car is busy - try the scan again in a
      moment.` shown in the REFUSAL colour. If the second tap also says `Scanning...`, the handler
      is claiming STARTED for a scan it never started — report immediately.
- [ ] **U-95-10 — the scan list is ranked and one-row-per-radio (1.2.2, Phase 2c).** In range of two
      routers broadcasting the SAME name: the list shows BOTH (one row per radio/BSSID), strongest
      first overall, the same radio never appears twice, and at most 12 rows arrive. Name-dedupe
      would show one row; radio-order would shuffle the strengths — both are the failure.
- [ ] **U-95-11 — no scan during a switch (1.2.2, Phase 2c).** Tap a router switch, then immediately
      tap scan: `The car is busy` (a scan kicked mid-join races the driver and returns garbage), never
      `Scanning...` + a junk list. After the switch resolves, scan works again normally.
- [ ] **U-95-12 — MODECYCLE walks both ways (1.2.3, Phase 3a).** From the page's note area (or app /
      relay), send `MODECYCLE` → the mode advances one registry row; `MODEPREV` steps back; `MODECYCLE`
      on the last row wraps to the first, `MODEPREV` on the first wraps to the last — the `#mode`
      readout and the dropdown both track (F-57), no NACK for the three tokens.
- [ ] **U-95-13 — the status data carries the network facts (1.2.3, Phase 3b).** `/status` (or the
      page's WS frame) now shows `station_ip`/`ap_ip`/`broadcast_ip`/`subnet` all populated while the
      AP is up (broadcast must be `192.168.245.255`, NOT empty — `WiFi.softAPBroadcastIP()` is called
      for the first time anywhere in this repo), and after a scan every entry of `scan` has
      `channel`/`own`/`saved`/`current`. A row naming the car's own hotspot must have `own:true`; a
      saved router `saved:true`; the network the STA is associated to `current:true`. Old app rows
      must still parse (`ip`/`ssid`/`networks` unchanged).
- [ ] **U-95-14 — the page's network table (1.2.3, Phase 3c).** On the car's page: tap **Scan** →
      `Scanning for networks...`, then a table with the headings Network/Signal/Ch/Flags appears with
      >=44px rows inside its own scroll region (the page itself never scrolls). The car's own hotspot
      appears as a labelled row with the OWN flag even when the scan did not list it; tapping it
      answers `ROUTERS;USED;4WD4MCar_WiFi` and the phone ends up back on the car's hotspot. Tapping an
      UNSAVED network shows a refusal-coloured note and sends nothing. The status rail shows the
      Hotspot (`j.ap`) and Bcast (`broadcast_ip`) lines, not `--`.
- [ ] **U-95-15 — the reconnect backs off and respects a hidden tab (1.2.3, Phase 3c).** Kill the
      car's AP while the page is open: the Conn line goes Disconnected and reconnection attempts
      visibly slow down (1s, 2s, 4s, 8s, then every 15s — Serial shows one dial per attempt, no
      faster than the schedule). Put the phone in another app for a minute, return: the page
      reconnects IMMEDIATELY (no 3s limbo) and the Conn line returns to Connected. During the hidden
      minute the car must NOT see a reconnect attempt every 3s (the old fixed timer).
## U-92 round — own-hotspot: double entry gone, the return actually returns (2026-10-04, firmware 1.0.5 — DEVICE VERIFY PENDING)

> Owner: _"webpage has double entry of esps own hotspot please remove that too"_ · _"through
> the webpage the router is not switching to the esp own hotspot"_ · _"says, car switched to
> 4wdcar_wifi but doent switch that one, but switches to other router fine"_ · _"webpage doesnt
> show the router name but shows select router"_. **Flash 1.0.5 first** — the poisoned registry
> row heals itself at the first boot (watch for the `[ROUTERS] removed the car's own hotspot`
> line).

- [ ] **U-92-1 — the double entry is gone.** After flashing 1.0.5, the router dropdown (webpage)
      and the app list show the car's hotspot EXACTLY ONCE. If the registry was poisoned, the
      serial prints `[ROUTERS] removed the car's own hotspot from the saved list (U-92)` at
      boot.
- [ ] **U-92-1b — switching away still works (U-90 regression re-check).** After the guard
      rewrite (ADD/USE with a password now refuse the own name; every ordinary router name still
      adds/switches normally).
- [ ] **U-92-2 — "back to my own hotspot" actually returns.** Car on router X (phone on X):
      pick 4WD4MCar_WiFi from the dropdown → one `REPLY=ROUTERS;USED;4WD4MCar_WiFi`, then serial
      `[RADIO] rejoin/...` steps ending in **`WiFi STA left the router - back on the car's own
      hotspot`** — the car is genuinely AP-only and the phone must rejoin 4WD4MCar_WiFi to follow.
- [ ] **U-92-3 — the placeholder shows the truth.** The dropdown's first row reads
      `Now on: <network>` and follows every switch (own hotspot ↔ router); it is never a stale
      static "Select router".
- [ ] **U-92-4 — refusals hold.** `ROUTERS;ADD;4WD4MCar_WiFi;x` → one `ROUTERS;ERROR;Reserved`;
      `WIFICFG;4WD4MCar_WiFi;x` → one `WIFICFG;ERROR;Reserved`; a save via "Add router…" now
      shows `Saved on the car: <ssid>` (the STORED reply is parsed at last).

## U-90 round — stuck-router fix + selectable coming-soon modes (2026-10-04, firmware 1.0.4 — DEVICE VERIFY PENDING)

> Owner: _"the car seems stuck on one router and doesnt connect even if on same network"_ ·
> _"allow those comming soon too that changes the modes on the car too"_ · _"menu for router
> change is also not working"_. **Flash 1.0.4 (huge_app, upload.speed=115200) FIRST** — the fix
> is inside the rejoin step machine, so old firmware keeps the old behavior no matter what the
> page does.

- [ ] **U-90-1 — switching to the router the car is ALREADY on works.** Car associated to
      router X (or "stuck" on it): send `ROUTERS;USE;X` (webpage console or app). The driver
      must restart and re-associate with a FRESH lease — serial shows `[RADIO]
      rejoin/mode-AP_STA` → `rejoin/softAP` → `rejoin/begin`, and STATE's `IP=` reflects the
      new lease. (Pre-fix this "succeeded" instantly on the stale link and nothing changed.)
- [ ] **U-90-2 — switching AWAY actually leaves.** Car on router X: `ROUTERS;USE;Y` → the car
      associates to Y (NOT X) within the switch budget; STATE `SSID=` shows Y. Then switch
      back to X. Every switch must behave like the first one — the old bug made every later
      switch resolve on whatever network the car already held.
- [ ] **U-90-3 — the router menu drives the car.** From the phone: select a saved router in
      the page dropdown → exactly one `REPLY=ROUTERS;USED;<ssid>`; the "＋ Add router…"
      entry still stores + switches via WIFICFG; the dropdown NEVER auto-highlights a router
      on load (it must sit on "— Select router —" until the user acts).
- [ ] **U-90-4 — coming-soon modes are selectable and change the car.** The mode dropdown
      offers 4WD4M + PATH/OBS-US/OBS-IR/MANUAL/AUTO/2WD1M, the coming-soon ones labelled,
      ALL selectable. Pick OBS-US: the car's mode actually changes (OLED + `#mode` readout +
      app agree). Drive controls pause in a stub mode (expected, known to the owner); picking
      4WD4M restores drive.
- [ ] **U-90-5 — an unknown mode still refuses.** Send `MODE_ESP_SER`: exactly one
      `NACK;E=UNKNOWN_MODE`, the note area shows the refusal, the car stays put.

> ⚠ **SUPERSEDED by U-105 (1.3.5):** beacon PARKING is REMOVED (the beacon is never parked — only
> `WIFICFG;WIFI;OFF` stops it) and the "one transport powers the other down" arbitration rows below
> no longer describe 1.3.5 (both transports stay up when enabled). Rows kept as history.

## U-87 / U-88 round — beacon parks only while in use, one answer per command (2026-10-03, `5dddd5a` + `5e04df7` — DEVICE VERIFY PENDING)

> Owner: _"the esp's own router seems to be active for the connection even when the car is working
> with another external route … save power and energy as much as you can"_ · _"switching between
> the router is not working and the addition and editing and deleting of the router is not
> properly built in the app"_. Flash `5e04df7` (huge_app, upload.speed=115200) unless the bench
> flash from the U-88 verification is still installed. Backfilled 2026-10-04 from the commit
> bodies.

- [ ] **U-88-1 — one command, one answer (the headline).** From the webpage console: add a
      router → exactly ONE `ROUTERS;ADDED;<ssid>`; delete it → exactly ONE
      `ROUTERS;DELETED;<ssid>`; delete it again → exactly ONE `ROUTERS;ERROR;Not saved:<ssid>`.
      Before U-88 every Wi-Fi command ran twice — a delete that WORKED reported a failure. (The
      U-88 bench measured all of these at one answer each; re-confirm on your flash.)
- [ ] **U-88-2 — one switch, one rejoin.** `ROUTERS;USE` on a saved router drives the radio
      through exactly ONE rejoin sequence and reports `ROUTERS;USED;<ssid>` once (it used to
      drive two full rejoins — and USE also drove the app's router-switch prompt from U-80).
- [ ] **U-87-1 — idle: the beacon is up.** Car joined to a router, NO client anywhere:
      serial `Own AP active: 4WD4MCar_WiFi`, no `[POWER] … parked` line, and 4WD4MCar_WiFi visible in
      a phone scan. (Bench-proven after the U-87 correction; here for the owner's eyes.)
- [ ] **U-87-2 — in use: the beacon parks.** A client connected over WiFi for >20 s:
      serial `[POWER] router up - car hotspot beacon parked`, and the page KEEPS serving on the
      router's address (it is a beacon, not the web server). **STILL UNVERIFIED ON HARDWARE** —
      the bench could never reach the network the car's router was on.
- [ ] **U-87-3 — drop: the beacon comes straight back.** Disconnect the last client (or lose the
      router association entirely): 4WD4MCar_WiFi returns on its own. There must be NO state where
      the car believes the beacon is down while it is up, and no state with no way in.
- [ ] **U-87-4 — the first U-87 stays dead.** Boot the car onto a router the phone is NOT on,
      with no client: the beacon must STAY UP (idle = reachable, always). The pre-correction
      U-87 parked it here and the car became unreachable — this row is the regression net.

## U-79 round — webserver mode gone, transport power policy, page fixes (2026-10-03, `abb7297`, CI ✓)

> Flash `abb7297` (huge_app) FIRST. The app side needs the U-80 OTA (d97d6f1): force-close +
> reopen the app ×2. Full plan: genumsolutions-app `guide/PLAN-2026-10-03-U79-U80-ONE-WIFI-SETUP.md`.

- [ ] **U-79-1 — the trap is gone.** Open the car's web page → the mode dropdown has NO
      "ESP32 (WS)" / "ESP32 (CLIENT)" options; the remaining coming-soon modes are DISABLED and
      say "(coming soon)". Driving after opening the dropdown can no longer park the car in a
      dead mode (the `[MODE] set 4WD4M -> ESP32(WEBSERVER)` serial line from the owner log must
      never appear again).
- [ ] **U-79-2 — one Wi-Fi setup.** Sending `ESP_SER` / `ESP_CLI` (webpage console or BT) gets a
      `NACK;E=UNKNOWN_MODE` and the car STAYS in 4WD4M.
- [ ] **U-79-3 — power policy, idle.** Car with no client: Bluetooth advertising AND 4WD4MCar_WiFi
      up simultaneously (BT visible on the phone; hotspot visible in WiFi scan).
- [ ] **U-79-4 — power policy, one client.** Connect the app over WiFi → serial
      `[POWER] Bluetooth off - web client active` and BT disappears from the phone's scan.
      Then connect a BT-only client instead → `[POWER] Wi-Fi off - Bluetooth client active`.
- [ ] **U-79-5 — power policy, drop.** Disconnect everything → within ~3 s
      `[POWER] Bluetooth advertising` + `[POWER] Wi-Fi up`, both reachable again.
- [ ] **U-79-6 — boot prefers last-used transport.** Last session on Wi-Fi + power-cycle →
      `[WIFICFG] boot: will try last-good router "…"` and it rejoins. Last session on BT +
      power-cycle → `[BOOT] Last used: Bluetooth - staying on hotspot + BT` and NO router hop.
- [ ] **U-79-7 — router fallback of last resort.** Router OFF + power-cycle → back on
      4WD4MCar_WiFi within seconds (one 6 s window, U-71 semantics unchanged).
- [ ] **U-79-8 — page: speed slider drag-safe.** Drag the slider SLOWLY for >1 s — the thumb
      never jumps back mid-drag; on release the value sticks (the 1 s status sync must not
      fight the finger).
- [ ] **U-79-9 — page: one place for networks.** The router dropdown lists the saved routers +
      "＋ Add router…"; adding sends WIFICFG (stores AND switches); on switch the page shows
      "Switching… reconnects when this phone joins <ssid>" and the WS link drop is expected,
      then reconnects once the phone follows.
- [ ] **U-79-10 — no regression of the U-77 watchdog class.** Repeat the original repro
      (switch router, open webpage immediately): NO reboot; reset reason stays power-on.


## ⭐ MASTER RUN — one bench session, everything outstanding (2026-09-30, RC-PENDING)

> **Owner instruction 2026-09-30: finish all the code first, run the device tests at the end, as
> one round.** So this is the consolidated sheet. Rounds E, H, N and 1 below are the detail; this
> is the running order. **Do the SAFETY rows first (A, then B).**
>
> Everything in the "CI-proved instead" column no longer needs the car — that is the point of
> moving the F-41 gate into a unit test.

| # | Row | What it proves | Needs the car? |
|---|-----|----------------|----------------|
| **A** | **T1 + T19 (e-stop)** | Bluetooth drives; **EMERGENCY STOP raises no error and keeps its label** (F-52) | **YES — do first** |
| **B** | **T2 + T16 (speed/stop)** | Speed is real, `SPD0` **stops** and never becomes speed 100, `S`/`ESTOP` agree across methods (F-51 safety) | **YES — do first** |
| C | T3–T6 | Reconnect, link loss → safe stop, mode + speed persistence, STATUS text | YES |
| D | T4 + T17 | WebSocket row: drives, mode tokens, clean STATE JSON | YES |
| E | T7–T10 | Round-1 regression (verdicts still untranscribed — please record them) | YES |
| F | **T15** | **HTTP/REST: direction commands actually MOVE the car** (the 404 defect) | YES |
| G | T18a | `/status` in a browser first, then the JSON agrees with the app | YES |
| H | T20 | Trim / steering-limit edits raise no error; no stray mode row; car still drives | YES |
| I | T11–T14 | Phase B envelope wiring — **gate still OFF**, use a scratch build | YES (scratch) |
| J | T18b | HTTP row is NOT selectable (F-41 still holds) | **NO — CI-proved** (`transportGate.test.ts`) |
| K | T19b | A genuinely unsupported MODE token still says "not supported by this car" | YES |
| L | T20b | 2WD1M / self-balance: `SERVO90` centring + `TRIM<n>` still work | YES (other car) |
| M | — | Donors untouched: clean `git status` in the four other car repos | NO — desk check |
| N2 | **T21–T23 (Round 2)** | OLED link rows are real (no fake SSID/IP), `ROUTERS;USE` adds+switches without resetting, web page no longer flips the car's mode | **YES — flash v1.0.1+ first** |

**Record the verdict for every row you run** (PASS / FAIL / NOT RUN). The standing release rule
holds: no FIN-36 release tag and no sign-off until these are PASS on hardware.

## Round E — Connection-Manager Phase B acceptance (T11–T14, 2026-09-30, READY — gate still OFF)

> **This car is the ONLY test target for the envelope work** (owner scope ruling 2026-09-30:
> donor cars wireless/2WD/balance/remote are not test targets and must not change). Plan:
> `guide/PLAN-2026-09-30-CONNECTION-MANAGER-ROADMAP.md` §4.
>
> **No firmware change ships with this round.** Phase B is app-side only: the JSON envelope is
> translated to the wire lines this firmware ALREADY understands (`F`/`SPD<n>`/`SERVO<n>`/
> `STEER<n>`/`TRIM<n>`/`4WD4M`/`REQ_STATE`/`ROUTERS;*`/`WIFICFG;*`). So the car stays v1.0.1
> and this round is an ACCEPTANCE round, not a firmware round — if a row fails, the fix is in the
> app's translation layer, never in a new token on the car (F-21/F-23/FIN-23/24).
>
> **HOW TO RUN IT:** the intake ships OFF (F-41 — nothing is user-selectable until it is proven
> here). To exercise it, flip `setEnvelopeIntakeEnabled(true)` in a scratch build
> (`mobile/src/transports/envelopeWiring.ts`) — NOT in a shipped build — and drive it from the
> dev console / a temporary hook. Then T11–T14. Record PASS/FAIL/NA per row with a note; no
> "all fine" summaries.

### T11 — every envelope type drives the car over BT

- [ ] Flip the gate, pair to `4WD CAR` over BT, then send each of: `{type:"drive",dir:"F"}`,
      `{type:"speed",value:140}`, `{type:"servo",value:90}`, `{type:"steerLimit",value:30}`,
      `{type:"trim",value:5}`, `{type:"mode",token:"4WD4M"}`, `{type:"requestState"}` — the car
      responds to every one exactly as it does to the hand-written line (wheels move, `STATE;`
      echoes `SPD`/`TRIM`, `CAPS;` still lists `4WD4M:LIVE`).
- [ ] Wheels stop moving at `SPD0`/`S` the instant the command is sent — no lag introduced by the
      extra translation hop.

### T12 — the SAME envelopes over the car's WebSocket (AP `4WD4MCar_WiFi`, ws :81)

- [ ] Repeat T11's full list over WS. Every row behaves identically to BT — **the envelope must not
      care which link carries it** (that is the whole premise of the additive form).
- [ ] Repeat T11's rows once more with the phone on the **home-router** WS if a `ROUTERS;USE`
      entry exists, so the STA path is covered too.

### T13 — malformed envelopes FAIL CLOSED with a readable reason

- [ ] Each of these emits **nothing at all** on the wire and returns its error (no wheel twitch,
      no partial command, no timeout on the car): `{v:99,...}` (bad version) · `{type:"mode",
      token:"obstacle_avoid"}` (free-text name — the FIN-23 registry is the only vocab) ·
      `{type:"speed",value:42}` (below `SPEED_MIN`) · `{type:"speed",value:300}` (above
      `SPEED_MAX`) · `{type:"trim",value:91}` · malformed JSON text.
- [ ] With the gate still ON, a valid command immediately afterwards still works (the failed
      attempt left no stuck state).

### T14 — byte parity + no behaviour change (the regression guard)

- [ ] With a serial logger attached, compare the bytes for the SAME action sent both ways: a
      hand-written `SPD150` from the existing UI vs `{type:"speed",value:150}` through the intake.
      **The serial log must be indistinguishable.** Any difference is a Phase B defect.
- [ ] With the gate OFF (the shipped default), the entire Control Panel behaves exactly as before
      this round: driving, mode switching, speed/servo/trim, the router panel, the deck layouts.
      No screen changed, so nothing here should feel different.
- [ ] Donors untouched: wireless/2WD/balance/remote firmware NOT reflashed and NOT retested
      (they are not test targets). Prove it by `git status` staying clean in those four repos.

Verdict: **RC-PENDING** — awaiting the owner's bench round.

## Round H — HTTP/REST method on the app (T15–T18, 2026-09-30, READY — app fix shipped, method still F-41-gated)

> Phase C item 2 of `guide/PLAN-2026-09-30-CONNECTION-MANAGER-ROADMAP.md`: the roadmap called
> HTTP "already implemented end-to-end; cheapest win; do first". **It was not** — auditing it
> against this firmware found two defects, both fixed app-side in `adapters.ts` and recorded as
> **FAILSAFES F-51**. No firmware change: the car's routes were always correct; the app was
> calling the wrong ones.
>
> - **F-50/F-51 defect 1:** the app sent `/f` `/b` `/l` `/r`; this car registers `/forward`
>   `/backward` `/left` `/right` → every direction command 404'd while `/status` kept the link
>   looking healthy.
> - **F-51 defect 2 (SAFETY):** `SPD0` (a stop line) mapped to `/speed?val=0`, but
>   `handleSpeed()` clamps to `MIN_SPEED` = 100 → a **stop became speed 100 and the car drove**.
>
> ⚠ The HTTP row is still **F-41-gated** (not user-selectable). To test: flip the transport's
> `isSupported()` for `http` in a scratch build, or call `linkManager.activate("http")` from a
> dev path. Do NOT ship the flip before these rows pass.

### T15 — direction commands actually move the car (the defect that was hiding)

- [ ] HTTP link connected, car on **wheels up** (stand it on a stand first — this row makes
      the car drive for the first time over this method). Send `F`, `B`, `L`, `R`, `S` one at
      a time. **Every one must move the car the right way.**
- [ ] Then confirm the negative: with the app sending nothing, the car is still. If a command
      appears to "work" while the car never moved, that is the old 404 behaviour — re-check the
      route names.

### T16 — speed behaves, and the floor is a floor (SAFETY row — do this one first)

- [ ] `SPD100`, `SPD180`, `SPD255` each set that speed. `SPD255` must not exceed the ceiling.
- [ ] **`SPD0` STOPS the car — it must NOT set speed 100.** Watch the wheels. If the car
      accelerates on `SPD0`, the `/speed?val=n` floor bug is back; stop the round.
- [ ] A speed below the floor (`SPD50`) also stops rather than driving at 100.
- [ ] `S` and `ESTOP` still stop. All four stop paths behave the SAME over HTTP as over BT/WS.

### T17 — mode tokens over HTTP

- [ ] `4WD4M` switches mode; the app reflects the new mode from the next `/status`.
- [ ] An invalid token is refused by the car and the app surfaces a readable error — it does not
      report a false success.

### T18 — link honesty + regression (the F-41 gate)

- [ ] `/status` in a browser first (`http://192.168.245.1/status`): JSON confirms the car is
      healthy before blaming the app (F-30's browser-first rule).
- [ ] With the HTTP row **gated off** (the shipped default), the Control Panel is unchanged and
      HTTP is not selectable — confirming the F-41 gate still holds after the fix.
- [ ] BT (T1) and WS (T4) still pass: the fix must not have touched the other transports.
- [ ] Donors untouched: wireless/2WD/balance/remote NOT reflashed. Prove with clean `git status`
      in those four repos.

Verdict: **RC-PENDING** — awaiting the owner's bench round.

## Round N — LIVE-transport command-rejection audit (T19–T20, 2026-09-30, READY — app fix shipped)

> Applying F-51's rule to the transports people actually drive (Bluetooth SPP + WebSocket), not
> just the gated ones. **F-52**: the car answers *any* line it does not handle with
> `NACK;E=UNKNOWN_MODE` (ModeManager.cpp:436 falls through to `setModeFromString`), so the error
> code covers "no such command" as well as "unknown mode". The app believed it, so **every
> EMERGENCY STOP press** raised a red *"ESTOP is not supported by this car"* toast, overwrote the
> EMERGENCY STOP status, and parked a phantom stub — this car implements none of `ESTOP`,
> `TRIM<n>`, `STEER<n>`. The car did stop (`SPD0` is the real stop, sent second in the batch),
> which is why the round read as PASS. App-side fix: classify before reacting.

### T19 — EMERGENCY STOP must be silent and must stay labelled EMERGENCY STOP

- [ ] Wheels up, car driving, then press **EMERGENCY STOP** over **Bluetooth**.
- [ ] The car stops. **No red error toast appears** — specifically nothing saying "not supported
      by this car", and nothing naming `ESTOP`.
- [ ] The drive status still reads **EMERGENCY STOP** (it must not flip to "Not supported by car").
- [ ] Repeat over **WebSocket** (`wifi-ap-ws`). Same three results.
- [ ] Sanity check that the fix did not silence real problems: connect to a car that genuinely
      lacks the mode and select it — the "not supported by this car" message MUST still appear
      for a real MODE token.

### T20 — trim / steering-limit edits raise no error on this car

- [ ] Adjust **TRIM** and the **steering limit** on the 4WD4M. No error toast; the value still
      updates and persists.
- [ ] Nothing in the mode list gains a stray row, and the car keeps driving normally afterwards
      (a wrongly-parked stub must not block a later legit mode).
- [ ] Cross-car regression: on a 2WD1M / self-balance car, `SERVO90` centring and `TRIM<n>` must
      STILL work — the fix moved the interpretation, it did not delete the commands.

Verdict: **RC-PENDING** — awaiting the owner's bench round.

## Round 3 — U-67 router-switch reset + one-go switch (T24–T26, 2026-10-02, READY — NOT YET FLASHED) ⭐ RUN FIRST

> Owner bench 2026-10-02: *"the 4wd4m car is resetting when the wifi router is switched from the
> app and the car is not always switching properly… the car doesnt go switch the router in one go
> propely."* Source on `main` through `26d41d5`, **Arduino CI ✓ `37001533166`** (1,746,294 B /
> 55% of `huge_app`). **These are the rows for the reported bug — run them BEFORE T21–T23.**
> They are the SAME flash: flash `main` once for Round 3 + Round 2 + Round 1's pending OLED row.
>
> ⚠ **T22 is NOT this bug.** `cd3158f` fixed `ROUTERS;USE` on an *unknown* router rebooting the
> car. The reset reported here was a different path (the BT watchdog) and was still live on the
> board. If T24 reproduces after the flash, the flash did not take.

### T24 — ⭐ the car does NOT reset when switching to the home router (`26d41d5`, F-60)

- [ ] Connect over the car's AP **or** BT, then switch the car to the home router
      (`ROUTERS;USE;<ssid>` from the app, or the car's web page).
- [ ] **The critical row:** leave the car completely alone for **3+ minutes** (past
      `BT_HARD_RESTART_MS` = 120 s) with **no** Bluetooth client attached. The car must stay UP on
      the router — OLED keeps the router SSID + its real IP, and it still answers on the router.
- [ ] An **unattended** car parked on the router for ~10 minutes must **not** reboot on a ~60 s
      loop. (Old binary: reboots repeatedly here — `restart()`'s `ESP.restart()` escalation.)
- [ ] A genuine mid-session BT drop **still** recovers: connect a phone over BT, walk out of
      range / kill BT, come back within ~2 min → the BT stack re-inits (`[BT-WD] Hard restart` on
      the serial log) and advertises again. The watchdog is now armed only on a link that was
      connected and then dropped.

### T25 — ⭐ the switch completes in ONE go (`26d41d5`)

- [ ] From the car's own AP, send `ROUTERS;USE;<home-router>` and then **do nothing** — no app
      interaction at all. The car must end up ON the router by itself.
- [ ] Serial log shows at most one `WiFi STA: first attempt timed out - retrying` per attempt
      (the retry budget), and then `WiFi connected, IP: <router ip>` — the switch must NOT stop
      after a single failed association (the old code gave up at 8 s and never retried).
- [ ] A router that is genuinely unreachable still **settles AP-only** after the capped budget
      (`WiFi STA gave up (router unreachable) - AP-only mode`) and the car's own hotspot stays
      joinable throughout (FIN-40/D2: the beacon must never blank long enough to lose the phone).
- [ ] After a successful retry the car pushes STATE promptly — the app must not sit on
      "getting an address" for a car that is already on the router (this needed the
      `connectingPending_` re-arm; without it a successful retry was invisible).

### T26 — no regression in the rest of the network behaviour

- [ ] With nothing stored, `ROUTERS;USE;<own ap>` still returns to the own network and the
      settle path is immediate (no retry loop when there is no target).
- [ ] Router reachable → the car still joins and reports its DHCP address in the STATUS JSON
      (`"connected":true`, a non-gateway `ip`) — the retry path must not delay or suppress it.
- [ ] Drive commands, failsafe safe-stop and the OLED are untouched by this round (no
      protocol/drive/failsafe change was made).

Verdict: **RC-PENDING** — not flashed.

## Round 2 — OLED names + ROUTERS;USE + web-mode fixes (T21–T23, 2026-10-01→02, READY — NOT YET FLASHED)

> Source on `main` through `dfda326`, CI ✓ `36909403120`. **A bench flash is required** —
> none of this is on the board yet. The flash for Round 1's "flash v1.0.1 + confirm the OLED
> line" row and this round are THE SAME FLASH: flash `main` once, then run T21–T23 followed by
> the still-untranscribed T1–T10 verdicts.

### T21 — OLED shows real link truth (v1.0.1 directive, fixes through `dfda326`)

- [ ] Bluetooth link: OLED prints the plain BT label + `Type: SPP`; the APP banner carries the
      controller name (the firmware has no peer-name channel — owner ruling Q2=B).
- [ ] Wi-Fi: row 3 shows the REAL joined SSID / AP id (parsed from extraInfo), never the link
      type string; the IP row shows the car's live `ipBuf` — never the hardcoded `192.0.0.0`.
- [ ] No garbage glyphs anywhere (UTF-8 em-dashes were removed from literals); a long SSID
      cannot run into the right-edge bar zone (40-byte rows + width trim).
- [ ] WiFi type blinks ~2.5 s per entry on the link line, per the directive.

### T22 — `ROUTERS;USE` adds-and-switches instead of resetting (`cd3158f`)

- [ ] Send `ROUTERS;USE;<new-ssid>;…` for a router the car has never stored → the car ADDS it
      on the fly, joins it, and STAYS UP — no reset/reboot, no crash on unknown router.
- [ ] Switching back to a stored router still works (`ROUTERS;LIST` → `USE`).

### T23 — web page never changes the car's mode (`64c2781` + F-57 `2478735`)

- [ ] Note the car's mode (app or `STATUS`), open the car web page in a browser → the mode is
      UNCHANGED (the page only pre-selects the dropdown for display; v1 used to force
      `MODE_ESP_SERVER` on load, which also PERSISTED it — F-31).
- [ ] Picking a mode in the dropdown still sends it and the car acts on it; the dropdown
      re-syncs to the car's real mode (no empty option can be sent).

Verdict: **RC-PENDING** — not flashed.

## Round 1 — v1.0.1 (R1 fix round — T0 PASS, T1–T10 verdicts NOT YET TRANSCRIBED)

> The v1.0.1 source is on `main` (`93817b2` OLED link-context, `Config.h` version bump) and the
> v1.0.0 bench flash below is real. **The T1–T10 rows are still unchecked on purpose.** The owner
> ran a round in the session that produced the app-side R1 fixes, but the per-row verdicts were
> never written into this file. They are NOT inferred here. Fill them in at the bench, then change
> the verdict line to RC-PASS / RC-FAIL.

### Verified in-session (do not re-run)

- [x] T0 — bench flash + first boot, v1.0.0, 2026-09-28. Details in the v1.0.0 round below.
- [x] v1.0.1 compiles in CI (`.github/workflows/arduino-ci.yml`,
      `esp32:esp32:esp32:PartitionScheme=huge_app`).
- [x] App-side R1 fixes shipped in `genumsolutions-app` (`d77c35d`, `0a86f64`), OTA-published:
      flicker, Wi-Fi connect hard-timeout (8 s), control-hub state plumbing.
- [x] App-side R1 (this session): Wi-Fi link **verification** is now a separate truth from
      "socket open" — `linkVerified` only flips true when the car actually answers
      (`wifiService.waitForCarAnswer`, 9 new tests in `mobile/src/services/wifiService.test.ts`).

### Needs the owner at the bench

- [ ] T1 — BT pair to `4WD CAR` (PIN 1234) from the app; drive F/B/L/R/S
- [ ] T2 — median command round-trip over BT (10 × `SPD150` → STATE echo), recorded ms
- [ ] T3 — phone joins AP `4WD4MCar_WiFi`; HTTP :80 page loads; WS drive works (4WD4M, no mode switch)
      — **app half UNBLOCKED 2026-09-28:** the manifest fix shipped in **app 3.2.7/60**
      (`5c99357`, release run `36436791172`) and was verified with `aapt` **in the published APK**
      (all four Wi-Fi permissions, `NEARBY_WIFI_DEVICES` + `neverForLocation`,
      `usesCleartextTraffic=true`), so the app can now legally open the socket. Install **3.2.7** —
      a **new APK, not an OTA**; an OTA cannot deliver a manifest change — and this row can pass.
      Do the browser half first: load `http://192.168.245.1` on the car AP; page loads ⇒ the car is
      healthy, so any later failure is the app's and not the firmware's. (`guide/FAILSAFES.md`
      **F-30**.)
- [ ] T4 — median command round-trip over WS (10 commands), recorded ms
- [ ] T5 — provisioning E2E: send `WIFICFG;<home>;…` over BT → car joins home LAN → `REQ_NETW` confirms
- [ ] T6 — provisioning over AP WS: `ROUTERS;LIST` returns list (v2 path, after Phase 1)
- [ ] T7 — ~~ESTOP over BT AND over WS~~ → **STALE ROW, rewritten 2026-09-28:** this firmware has
      **no ESTOP handler at all** (and the app's e-stop UI was removed by owner directive), so
      there is nothing to test. Test instead: **`S` and `SPD0` over BT AND over WS**, plus
      **link-loss safe-stop on both** (kill the app / close the socket) within
      `FAILSAFE_TIMEOUT_MS` = 2000 ms. Do not re-add an ESTOP control without a firmware ruling.
- [ ] T8 — 100-command burst over WS; packet loss % recorded
- [ ] T9 — old-car regression: donor WIRELESS car still pairs/drives as before (untouched repo proof)
- [ ] T10 — ~~CAPS shows `4WD4M:LIVE`, `ESP_SER:CS`, `ESP_CLI:CS`; app marks the CS ones Coming
      Soon~~ → **STALE — superseded by the rewritten row further below (U-81 correction,
      2026-10-03):** U-79 deleted `ESP_SER`/`ESP_CLI` from the registry, so expect their
      **ABSENCE** from the CAPS line — never `CS`
- [ ] Flash v1.0.1 to the board and confirm the new OLED line renders
      (BT paired vs Wi-Fi mode + RSSI) and the splash reports 1.0.1

Verdict: **RC-PENDING** — T0 PASS only; T1–T10 unrecorded.

## Round 0 — v1.0.0 baseline (flash + boot PASS 2026-09-28)

Pre-flight:
- [x] Bench flash via USB, 115200, **PartitionScheme=huge_app** — **DONE 2026-09-28 by the AI
      session**: COM7 (CH340), build `build/v2`, upload at 115200 (default 921600 failed twice —
      flaky CH340 link, known workaround), **"Hash of data verified"**, hard reset OK. Board MAC
      `08:b6:1f:98:2f:f4`. Sketch 1,742,162 B (55%).
- [x] First boot: **T0 PASS** — serial capture (DTR/RTS reset + PowerShell reader):
      ```
      [BOOT] Restored mode=ESP_SER is retired (CS) in v2 - keeping 4WD4M.
      [BOOT] Restored speed=255
      Own AP active: 4WD4MCar_WiFi
      Stored router available - join via ROUTERS;USE / WIFICFG (own AP stays default)
      [V2] Web layer always-on: AP=4WD4MCar_WiFi ip=192.168.245.1 http=:80 ws=:81
      ```
      The retired-mode guard fired exactly as designed (donor NVS held mode=SERVER from the
      WIRELESS firmware; v2 kept 4WD4M). Splash itself renders on the OLED (owner eyeballs it).

Rows (rows 1–10 carried into the v1.0.1 round above; this v1.0.0 round is closed at T0):
- [ ] T1 — BT pair to `4WD CAR` (PIN 1234) from the app; drive F/B/L/R/S
- [ ] T2 — median command round-trip over BT (10 × `SPD150` → STATE echo), recorded ms
- [ ] T3 — phone joins AP `4WD4MCar_WiFi`; HTTP :80 page loads; WS drive works (4WD4M, no mode switch)
      — **app half UNBLOCKED 2026-09-28:** the manifest fix shipped in **app 3.2.7/60**
      (`5c99357`, release run `36436791172`) and was verified with `aapt` **in the published APK**
      (all four Wi-Fi permissions, `NEARBY_WIFI_DEVICES` + `neverForLocation`,
      `usesCleartextTraffic=true`), so the app can now legally open the socket. Install **3.2.7** —
      a **new APK, not an OTA**; an OTA cannot deliver a manifest change — and this row can pass.
      Do the browser half first: load `http://192.168.245.1` on the car AP; page loads ⇒ the car is
      healthy, so any later failure is the app's and not the firmware's. (`guide/FAILSAFES.md`
      **F-30**.)
- [ ] T4 — median command round-trip over WS (10 commands), recorded ms
- [ ] T5 — provisioning E2E: send `WIFICFG;<home>;…` over BT → car joins home LAN → `REQ_NETW` confirms
- [ ] T6 — provisioning over AP WS: `ROUTERS;LIST` returns list (v2 path, after Phase 1)
- [ ] T7 — ~~ESTOP over BT AND over WS~~ → **STALE ROW, rewritten 2026-09-28:** this firmware has
      **no ESTOP handler at all** (and the app's e-stop UI was removed by owner directive), so
      there is nothing to test. Test instead: **`S` and `SPD0` over BT AND over WS**, plus
      **link-loss safe-stop on both** (kill the app / close the socket) within
      `FAILSAFE_TIMEOUT_MS` = 2000 ms. Do not re-add an ESTOP control without a firmware ruling.
- [ ] T8 — 100-command burst over WS; packet loss % recorded
- [ ] T9 — old-car regression: donor WIRELESS car still pairs/drives as before (untouched repo proof)
- [ ] T10 — ~~CAPS shows `4WD4M:LIVE`, `ESP_SER:CS`, `ESP_CLI:CS`~~ → **STALE ROW, rewritten
      2026-10-03 (U-81):** U-79 REMOVED `ESP_SER`/`ESP_CLI` from `MODE_REGISTRY` outright, so the
      car no longer emits them **in any state** — not `CS`, not `WIP`. The correct expectation is
      now **absence**: `CAPS;4WD4M:LIVE;PATH:CS;OBS_US:CS;OBS_IR:CS;MAN:CS;AUTO:CS;2WD1M:CS`,
      with no `ESP_SER` key at all. The app half of U-81 reads that absence as `GONE` and must
      show the two website modes as "Not on this car" and refuse to send them. **Test:**
      (a) the `CAPS;` frame the car broadcasts carries exactly the 7 keys above — it rides
      `REQ_STATE` (`sendState()` writes STATE + CAPS + NETW, ModeManager.cpp:542). **`REQ_CAPS`
      IS NOT A COMMAND** — the firmware has no such verb and will NACK it; (b) the app greys out
      EspWebServer /
      EspWebClient and cannot select them; (c) an OLD car still announcing `ESP_SER:LIVE` /
      `ESP_CLI:WIP` keeps both modes selectable — that is the fleet-parity half and is the row
      most likely to regress.

Verdict: RC-PASS / RC-FAIL → snag round numbering per fleet convention.

---

## U-81 (2026-10-03) — the WiFi text protocol was a sink

**F-61 applies to every row here: CI compiles, it does not prove behaviour.** U-81's entire
finding is that a green Arduino build shipped a `write()` that threw every byte away, and no
test in any repo could see it. Flash before believing any of this.

- [ ] U81-1 — **the root cause, directly.** Open the page on the car AP (Wi-Fi), DevTools →
      WebSocket frames → Messages. Send `REQ_STATE` from the console. **A `CAPS;…` text frame must
      appear** (it rides the same `sendState()`). ⚠ Do **NOT** send `REQ_CAPS`: it is not a firmware
      command, so the car NACKs it and this row would report a failure on a perfectly healthy car.
      Before U-81 the frame never existed: `WebServerComm::write()` returned `len`
      without sending. This is THE row; the rest are consequences of it.
- [ ] U81-2 — **router add over Wi-Fi, end to end.** App → add a home router → the page/car show
      it immediately (`ROUTERS;ADDED;<ssid>`), then the car joins it and the socket drops. Join
      the same network and reopen: **the router is in the list.** Before U-81 the ack was
      discarded, the socket died, and the router appeared nowhere.
- [ ] U81-3 — **no phantom `STARTED` router.** After any scan, the saved list contains only real
      SSIDs. `ROUTERS;SCAN;STARTED` must never be persisted as a router named `STARTED`.
- [ ] U81-4 — router **switch** over Wi-Fi: `ROUTERS;USED;<ssid>` arrives, the U-80
      "join <ssid> on this phone" notice appears, and the saved list gains the router.
- [ ] U81-5 — **refusals are reported as refusals.** Force each one and confirm the page and the
      app both show the car's own reason, never a success: `ROUTERS;FULL`, `Reserved`
      (the car's own AP name), `Password too long`, `SSID length`, `Not saved:<ssid>`.
- [ ] U81-6 — **a refused mode is visible.** Tap EspWebServer (or any mode this car lacks): the
      car answers `NACK;E=UNKNOWN_MODE;ARG=…`, the app says "not supported by this car" and the
      mode display **rolls back** instead of lying until the next `MODE=` frame.
- [ ] U81-7 — **Bluetooth unaffected.** Repeat U81-5/6 over BT: `auxComm_` already really wrote to
      `SerialBT`, so this must still work. A regression here means the NACK fan-out
      (`ModeManager.cpp:841`) double-sent or leaked.
- [ ] U81-8 — **the root page loads completely.** Load `http://192.168.245.1` on a phone. The page
      must render fully and quickly, never a blank/hanging document. This row covers both halves
      of the fix: the budget is now 6 s, and a short body now CLOSES the socket instead of leaving
      the browser waiting on a `Content-Length` the car will never honour. Repeat ~10× on a phone
      that has just joined the AP (worst case for a cold TCP window).
- [ ] U81-9 — **watchdog.** While loading the page repeatedly, confirm no reboot and no
      `Brownout`/`Task WDT` in the serial log. The per-write cap (500 ms) + `yield()` is what
      protects loop(), not the total budget.
- [ ] U81-10 — **old-car regression (do not skip).** A pre-U-79 car must still show all 9 modes
      selectable, still pair over BT, and still drive. U-81 changed the app's availability
      resolver; a car that sends no `CAPS` line must keep the fleet fallback.
- [ ] U81-11 — 100-command burst over WS again (T8) with the text protocol live, to confirm
      `broadcastTXT` in `loop()` cannot stall under load.

Verdict: **RC-PENDING** — needs a flash.

## U-82 — CORRECTION. The website mode dropdown was never broken

This section first recorded a defect that does not exist. Keeping the correction
here so the next session does not "re-fix" it.

**The false claim.** An audit read `setMode()` as sending each option's internal
id (`MODE_AUTO`, `MODE_PATH`, …) while `setModeFromString()` matches only
`MODE_REGISTRY[].token` (`4WD4M PATH OBS_US OBS_IR MAN AUTO 2WD1M`) plus the
aliases `BT`/`MANUAL`. Conclusion drawn: all seven options NACK and the dropdown
was 100% dead.

**Why it was wrong.** The grep missed the handler. `ModeManager::handleCommand`
(ModeManager.cpp:302) matches the five characters `"MODE_"` and passes the
*remainder* to `setModeFromString`:

```cpp
if (strncmp(cmdBuf, "MODE_", 5) == 0) { setModeFromString(cmdBuf + 5); return; }
```

So `MODE_MANUAL` → `MANUAL` (alias) → `Mode::MANUAL`, `MODE_PATH` → `PATH` →
`Mode::PATH`. A prefix-length skip, which is why no literal `MODE_MANUAL` string
exists in the firmware to grep for. The markup comment claiming the mapping lives
in the firmware was accurate after all.

**Measured on the car** (fresh socket per token, mode recorded before and after,
no-op control stable, car restored to 4WD4M after every trial):

| sent | mode after | verdict |
|---|---|---|
| *(nothing)* | 4WD4M | control stable — harness reliable |
| `MODE_MANUAL` | MAN | accepted |
| `MODE_PATH` | PATH | accepted |
| `MODE_AUTO` | AUTO | accepted |
| `MODE_2WD1M` | 2WD1M | accepted |
| `MAN` / `AUTO` / `2WD1M` | same | accepted (bare form works too) |
| `ESP_SER` | 4WD4M | refused, `NACK;E=UNKNOWN_MODE` (U-79, correct) |

**Correction applied.** The one-line `setMode()` change is reverted. The page
gate keeps only the invariant that is real — every `data-canon` is a genuine
`MODE_REGISTRY` token, in both directions — and stage 6 now accepts either wire
form.

- [x] U82-1 — website mode dropdown switches the car: **PASS** (measured above).
- [x] U82-2 — bare canonical tokens also work: **PASS**.
- [x] U82-3 — a genuinely removed mode is still refused: **PASS** (`ESP_SER`).
- [ ] U82-4 — owner eyeballs the dropdown in a browser and confirms each entry
      switches the mode. Not automatable; the tokens are proven above.

**Lesson.** The static "the two halves must agree" instinct was right, but I
asserted a contract from one side of a handshake and only read the half I
already had open. The device answered in one probe. Verify the mechanism exists
before reporting the defect — and when a grep for a literal comes back empty,
that is evidence the encoding is indirect, not evidence the code is missing.

---

## Bench run 2026-10-03 � car flashed, U-81 root cause verified on the device

Hardware: ESP32-D0WDQ6 rev 1.1, MAC `08:b6:1f:98:2f:f4`, CH340 on **COM8**, 4 MB flash.
Built `esp32:esp32:esp32:PartitionScheme=huge_app` with core **3.3.11** (the CI pin).
Firmware `4d5d6bc`, **1,797,518 bytes (57% of huge_app)**, hash verified on write.

**The flash was confirmed against the build, not assumed.** The app partition was read back off
the car and hashed:

    built   FB3D3B46391524BA5DA799916B7F810A95672EFC75AB2F954CDF98665B366200
    on-car  FB3D3B46391524BA5DA799916B7F810A95672EFC75AB2F954CDF98665B366200

**NVS survived the partition change.** `nvs@0x9000` is byte-identical between the old
app0+app1 OTA table and the huge_app table, so nothing was lost:
`[BOOT] Restored mode=4WD4M`, `[BOOT] Restored speed=230`,
`will try last-good router "nijandangal_2.4"`. A backup was taken before flashing.

Automated over `ws://192.168.1.100:81/` � **12/12 passed**:

| row | check | result |
|---|---|---|
| U81-1 | `CAPS;` frame arrives over WiFi | PASS � `CAPS;4WD4M:LIVE;PATH:CS;OBS_US:CS;OBS_IR:CS;MAN:CS;AUTO:CS;2WD1M:CS` |
| U81-2 | `STATE;` frame arrives over WiFi | PASS � `STATE;MODE=4WD4M;SPD=230;TRIM=0;STATUS=Stopped;SSID=nijandangal_2.4;�` |
| U81-3 | saved routers listed | PASS � `NETW;4WD4MCar_WiFi;Esp;nijandangal_2.4` |
| U81-4 | telemetry JSON complete | PASS � `mode=4WD4M spd=230 rssi=-57 ip=192.168.1.100` |
| F-62 | refuses a mode this car dropped | PASS � `NACK;E=UNKNOWN_MODE;ARG=ESP_SER` |
| F-62 | refuses an unknown command | PASS � `NACK;E=UNKNOWN_MODE;ARG=NOT_A_COMMAND` |
| U82-1 | `MODE_PATH` switches the car | PASS � mode became `PATH` |
| U82-1 | bare `PATH` switches the car | PASS � mode became `PATH` |
| U82-1 | `4WD4M` switches the car | PASS |
| U82-2 | speed command honoured | PASS � `speed=150` |
| U81-8 | drive commands accepted, no wedge | PASS � 15 frames still flowing after F/S/B/S |
| � | car restored to `4WD4M`, speed 230 | PASS |

U81-1 is the one that mattered: it is the root cause. Before the fix the car returned **nothing**
over WiFi. These frames are the proof the sink is gone.

Root page over HTTP: **25,116 bytes, HTTP 200**, containing `handleTextLine`, `ROUTERS;USED;`, and
all four U-83 markers (`lastCapsSig`, both conditional writes, the open-guard). So the page served
by the car is the page in this repo.

### Still NOT verified � needs a human

Do not read the table above as U-81 being closed. These need eyes or hardware this bench lacks:

- [ ] U81-5 / U81-6 � **Bluetooth SPP.** Needs a paired phone. The `auxComm_` NACK fan-out is the
      untested half of the U-81 change.
- [ ] U81-7 � a refused mode must be *visible* in the page, in the error colour.
- [ ] U81-9 � **watchdog**: load the page repeatedly, confirm no reboot.
- [ ] U81-10 � **old-car regression.** Needs a second, pre-U-79 car.
- [ ] U81-11 � 100-command burst over WS (T8).
- [ ] U82-4 � the mode dropdown **visually**: no flicker, rows usable. The tokens are proven; the
      feel is not. This is the row that matters most for the original complaint.
- [ ] OLED rendering and the physical buttons/joystick.

### Toolchain gotchas, so nobody loses an hour to them

- `arduino-cli upload` **fails at the default 921600 baud** on this CH340 with
  `Unable to verify flash chip connection`. Use `--upload-property "upload.speed=115200"`.
- The Arduino IDE on this machine has core **3.3.12**; CI pins **3.3.11**. Built with 3.3.11 here.
- The car's WiFi join takes **60�90 s** after boot. The log stops at `[RADIO] defer-mdns/sta-busy`
  and prints nothing further � the car is simply not on the network yet. Ports 80/81 refuse
  connections until it is. Do not reset in a loop waiting for it.
- Never flash `*.merged.bin` at 0x0: it includes the NVS region and would wipe the saved routers
  and credentials. Use `arduino-cli upload`, or `write_flash` of bootloader/partitions/app only.
---

## Bench run 2026-10-03 (second pass) � U-87 parking proven on hardware

Car flashed with `5dddd5a` and confirmed by reading the app partition back off the
hardware and hashing it (`1CD8F39B...5214` both sides). Joined `nijandangal_2.4`.

**The U-87 power cycle, captured live on the serial console:**

    10:48:10  [POWER] Bluetooth off - web client active
    10:48:27  [POWER] in use on router - hotspot beacon parked
    10:49:10  [POWER] car idle - hotspot beacon restored (stay reachable)
    10:49:14  [POWER] Bluetooth advertising (no client)

That is the whole property: 20 s after a client connects the beacon goes down,
and the moment the last client leaves it comes back. **U-87-1 through U87-4 PASS.**

The page kept working throughout the park � HTTP 200, 25,116 bytes, with
`handleTextLine` and all three U-83 markers present. Parking is a beacon, not the
web server.

**U-81 device suite: 12/12.** Two rows first failed and BOTH were harness faults,
not car faults � re-checked one variable at a time and the car was correct:

| row | first run | re-check |
|---|---|---|
| F-62 refuses unknown command | FAIL | 2x `NACK;E=UNKNOWN_MODE;ARG=NOT_A_COMMAND`, both attempts � the 2.5 s capture window was too short |
| U82-1 `MODE_PATH` -> PATH | FAIL | mode became `PATH`, 0 NACKs, stable across 3 reads � the read caught the pre-change broadcast |

The suite now double-reads after every switch and gives refusals a 4.5 s window.

### Still needs the owner (cannot be automated here)

- [ ] **U87-5** � confirm `4WD4MCar_WiFi` actually disappears from a PHONE's Wi-Fi
      list while parked and reappears after. The car says it did; only a radio can
      confirm the beacon is really gone from the air.
- [ ] **U81-5/6** � Bluetooth SPP: a refused mode must be visible on the phone.
      This is the untested half of U-81 (the `auxComm_` fan-out).
- [ ] **U81-7** � a refused mode visible on the WEB page in the error colour.
- [ ] **U81-9** � watchdog: reload the page repeatedly, confirm no reboot.
- [ ] **U81-10** � old-car regression (needs a pre-U-79 car).
- [ ] **U81-11** � 100-command burst over WS.
- [ ] **U82-4** � the mode dropdown **visually**: no flicker, rows usable.
- [ ] **U86 rows** � the app's one-list Wi-Fi panel, the hotspot row being
      tappable, the fixed page header. Gates and CI are green; no human has seen it.
- [ ] OLED rendering, physical buttons/joystick.

## U-94 Phase 2 (2026-10-05) - T-REL: the Internet method, on the car

**State: NOT RUN. Nothing in this section has been observed.** No car has been
flashed with `1.1.0`; the relay has not been deployed; the migration has not
been applied. F-61 stands: the relay-contract gate and the Arduino compile are
source-level evidence and nothing more. Read that sentence again before quoting
any row below as a result.

**(2026-10-10) The F-84 blocker now has a DRAFT fix on branch
`feat/relay-phase2-own-task`** — the relay dial (and every `client_.loop()`) runs
on its own FreeRTOS task, so arming `RELAY_HOST` passes `armed-handshake`. That
branch is UNCOMPILED and unflashed, so nothing here changes yet.

**(2026-10-10) A review pass found the relay could never pair, on `main` as well
as the branch, and it is now fixed on the branch.** `controlEventIs()`/
`controlNumber()` searched the whole frame with `strstr`/`strchr` even though a
control frame's first byte is the prefix's NUL — so the T-REL-2 `paired` line
below was unreachable: every control event matched nothing and `enrolled_` never
went true. Parsing now starts at `frame + kControlPrefixLen`, guarded by the new
`control-parse` gate rule + M64. So the T-REL rows are still NOT RUN, but a car
flashed from the branch (or a merged `main`) can now actually reach `paired`.

**Owner actions required first, in this order.** `RELAY_HOST` in `Config.h`
must name the Supabase project (it ships empty, so the transport is inert until
someone fills it in deliberately). The migration
`20261005130000_relay_token_digest.sql` must be applied. `RELAY_TOKEN_PEPPER`
must be set as an edge-function secret. Only then can a car be enrolled, and
only then is a token available to paste.

### T-REL-1 - the handshake

- [ ] `RELAYCFG;<32 lowercase hex>` over Bluetooth -> `RELAYCFG;STORED` in
      `REPLY=`, and **nothing else**. Read the whole `REPLY=` aloud: if the
      token appears anywhere in it, the car is compromised and the token is
      burned. This is the single most important check in the section.
- [ ] `RELAYCFG;SHOW` -> `RELAYCFG;SET`. Then power-cycle and `SHOW` again
      -> still `SET`. (Proves NVS survived the reboot.)
- [ ] `RELAYCFG;RESET` -> `RELAYCFG;CLEARED`, then `SHOW` -> `NOT_SET`,
      and the log shows `[RELAY] no token stored` at the next boot.
- [ ] **The negative row:** `RELAYCFG;ABC` (too short) and
      `RELAYCFG;<32 UPPERCASE hex>` -> both `ERROR`, and neither is stored.
      Uppercase is rejected on purpose: the relay's regex is lowercase-only, and
      silently accepting a different spelling of the same token would produce a
      baffling 4401 later.

### T-REL-2 - the link, and what the panel says

- [ ] Serial shows `[RELAY] dialling ...`, then `socket up, saying hello`,
      then **`paired - link is live`**. `paired` is the only line that means
      connected; the socket being up does not.
- [ ] OLED reads `Internet|WSS` while paired. Then pull the router's antenna
      (or rename the SSID): within a heartbeat interval the OLED must fall back
      and the car must **stop within 2 s**. A car that keeps driving because a
      dead link still claims to be connected is the F-74 failure mode.
- [ ] `[RELAY] refused by relay` appears when the token is wrong. Confirm the
      car is NOT connected and the safe-stop is armed.
- [ ] Recovery: restore the router, confirm the client reconnects **by itself**
      with no reboot, and re-confirms `paired`.

### T-REL-3 - driving over it, from a phone not on this network

- [ ] Phone on mobile data, car on the router. `F`/`B`/`L`/`R`/`S` and
      `SPD<n>` all work, and the car's panel shows `Internet|WSS`.
- [ ] The speed on the phone's STATE track matches the car, and the periodic
      STATE keeps arriving. **This is the row that would have failed in Phase 1:**
      a STATE line is ~110 bytes before any `REPLY=`, and the old cap was 128
      in both directions.
- [ ] Kill the phone's network mid-drive: the car must stop within 2 s, not
      continue. (Failsafe, not the relay.)
- [ ] **Latency is honest now.** The phone's ping measures the phone, not the
      relay. Confirm the relay's log shows the forwarded `ping` and the car's
      `ping-echo`. A latency number that does not include the phone is worse
      than no number, because it hides the phone's own network.
- [ ] `REQ_STATE` and a `REPLY=` line over the relay - the 448-byte
      direction, with a multi-router list.

### T-REL-4 - regressions that are not optional

- [ ] Bluetooth driving still works with the relay live, and the power policy
      does **not** switch Wi-Fi off under an active BT client (that was the bug
      the `wiredClient` line in the sketch exists to prevent).
- [ ] Wi-Fi page + WS still works with the relay live.
- [ ] Boot with no token: no dialling, no log noise beyond one line, and every
      existing behaviour unchanged. This is the donor-car case.
- [ ] Flash/SRAM deltas from the compile job are inside the Huge APP budget.

### T-REL-5 - what this section may not be used to claim

Green gates do not make T-REL-1..4 true. Until these rows have a verdict from a
human on a car, "the Internet method works" is not a sentence anyone in this
project is entitled to write.
### Monitor

`monitor_car.py <port> [seconds]` decodes the serial console and tags the lines
that matter. Use it in one terminal while doing something to the car in another �
it is how the table above was captured.
