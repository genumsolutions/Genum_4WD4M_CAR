# CHANGELOG - Genum_4WD4M_CAR

Per-release firmware notes, newest first.

Extracted 2026-10-06 out of the `FW_VERSION` define in `Config.h`, where the whole
history had grown into a single 3,247-character line - 34% of that file - which
made `Config.h` hard to read, diff and blame. The notes were worth keeping; the
one-line dump was not. `Config.h:10` remains the source of truth for the value.

> UNFLASHED. Everything from 1.2.4 down was source-only when written; the bench
> car was still on an older build. See `TRACKS/NEXT-SESSION.md`.

## v1.2.4 - 4WD4M OLED network lines (2026-10-09)

Owner directive (2026-10-09): the 4WD4M OLED's last two lines are the NETWORK truth.
Shipped in two parts, because the first rendering was corrected on the owner's bench in
the same session:

- **v1.2.3 work (`7731f2b`)** — Row 3 = the current method + network (`Wifi : <ssid>`
  for the WiFi method, `Bluetooth : <transport status>` / `Off` for the Bluetooth method,
  the hotspot's name while the own AP is up); Row 4 = the network's directed-broadcast
  address. The 2.5 s turn-by-turn blink (F-53/F-33) is gone, the **"LAN ready" wording is
  removed repo-wide** (`ModeManager::currentLink()` STA type → "WiFi" plus every stale
  comment), and the link bars moved from the network rows up to the Speed row so rows 3/4
  own the full body width. `/status` now also fills `broadcast_ip` + `subnet` on a joined
  LAN — they were AP-only since U-95 Phase 3, so the page's Bcast row showed `--` on a
  router.
- **Corrected (`0285eee`)** — the owner reported the OLED's broadcast address wrong; the
  app shows the car's OWN IP, so Row 4 is now `IP : <car's current IP>` (STA lease, else
  the softAP IP), matching the app's `WiFi · <ip>`. The directed broadcast stays on
  `/status broadcast_ip` + the page's `Bcast:` row; the `|BC=` carrier that fed the
  OLED-only line was dropped from `extraInfo` (STA rows are back to `pct,rssi,ip,<ssid>`,
  AP rows to `AP:<name>,<ip>|Nets:…`).

Gates at 0285eee: firmware syntax 10/10, relay-contract, page-js, mutation harness 62/62
(byte-exact restore). `backup` branch added on origin at 0285eee.

## v1.2.3 - U-95 Phase 3 (the car's own page)

U-95 Phase 3: (3a) `MODECYCLE` / `MODEPREV` / `MODENEXT` tokens ride the existing system-command hook into ONE shared `ModeManager::cycleMode(delta)` — the walk that used to be reachable only from the GPIO switch — with wrap both directions, so the page, the app and the relay all get the same implementation. (3b) The status data carries what only the car knows: every scan row gains `channel` (captured at collect time, while the driver still held it), `own` (names the car's hotspot), `saved` (consults the saved-router registry), `current` (the STA is ASSOCIATED to that row) — computed once in `buildScanJson`, shipped as facts, because three app modules already disagree about the own-network rule; the status JSON gains `station_ip` / `ap_ip` / `broadcast_ip` / `subnet`, the first call to `WiFi.softAPBroadcastIP()` anywhere in this repository; both status copies are now `static char buf[3584]` (8 KB loopTask, single-task by construction) and the scan fragment 512 → 1856, sized for the worst-case 12 rows. (3c) The page finally reads it: a `<table>` with Network/Signal/Ch/Flags headings, >=44px rows in its own scrollable region inside the stage band (the page itself still never scrolls), the car's own hotspot as a real LABELLED row (synthesised from `j.ap` when the scan missed it, tappable to `ROUTERS;USE` — the way back to the car), a Scan button (the STARTED/BUSY notes existed but nothing on the page could send the command), `j.ap` and `broadcast_ip` surfaced in the status rail, and the bare 3 s reconnect `setTimeout` replaced with backoff 1s → 15s capped, reset on open, silent while the tab is hidden, and an immediate reconnect on `visibilitychange`. Gate rules `mode-cycle` + `status-net-fields` (contract) and `page-net-table` + `page-reconnect` (page harness, which now also records scheduled delays), `status-buffer` moved to the static form; mutants M43-M62 (20 new), all mutation-proven both directions; full harness 62/62. Page 38,171 bytes (was 28,761), still served in 512-byte chunks under the Phase 1c budget. Still UNFLASHED - the bench car runs pre-1.2.1 firmware.

### v1.2.3 postscript — scan removed from the page (2026-10-09)

Owner: the scan feature is gone from the car's own page. The network table, its Scan
button, the `lastScanSig` diff gate and both `.nets` CSS blocks were removed
(`78b15e9`, `fb218dc`, plus this sweep); the router dropdown is now the only network
control on the page. The firmware scan and the app's `ROUTERS;SCAN` are untouched, and
the page still narrates the `STARTED`/`BUSY`/`CANCELLED` reply notes. The page gate rule
`page-net-table` was retired and `page-router-dropdown` made non-vacuous, so harness
mutants M56-M62 now prove the dropdown's markup + `ROUTERS;USE`/`WIFICFG`/`ROUTERS;DEL`
wiring. The dead `modeHasDash` OLED member is removed; the unreachable ESP_SER drawing
block is retained, with its header comment corrected to match.

## v1.2.2 - U-95 Phase 2 (router switching + scan)

U-95 Phase 2: (2a) the rejoin path WALKS the switch teardown as a step machine, one radio call per pass: MODE drops to WIFI_AP first — the old unconditional `WiFi.mode(WIFI_AP_STA)` was a guaranteed no-op because WiFiGeneric.cpp:602 early-returns when the mode already matches, i.e. on every switch after the first (the U-90 teardown existed only in a comment); MODE_WAIT confirms the drop via WiFi.getMode() with a bounded re-issue (at most 2, breadcrumb `rejoin/mode-AP-not-taken` on timeout); AP restores the hotspot inside the STA-down window (startOwnAP(), apRunning_ forced false first — the beacon may blink once, the state never lies); RAISE raises to WIFI_AP_STA with apRunning_ untouched (boot-path proof: beginAlwaysOn() raises the mode every boot without bouncing the beacon); BEGIN issues WiFi.begin(). WiFi.persistent(false) now runs as the FIRST radio-touch of every boot — the Arduino default (persistent=true) rewrote flash NVS on every WiFi.begin()/softAP() against ~100k erase cycles, and router retries multiply the wear. (2b) The retry's disconnect() and begin() no longer share a loop pass — the U-75 shape that rebooted the car: retryBeginPending_ arms on the disconnect pass, a guarded consumer issues the begin on a later pass; rejoinNetwork() cancels an in-flight begin and re-stamps lastWifiRetryMs_, so a stale deferred begin can't land mid-teardown and the 10 s backoff can't fire on pass 1 of the step machine. (2c) The scan answers honestly and ranks truthfully: startScan() returns bool and refuses BOTH an in-flight scan and one kicked while the STA is mid-join; the handler replies ROUTERS;SCAN;BUSY instead of claiming STARTED for a scan it silently dropped (the old page waited forever for a result that was never coming); the page shows BUSY in the refusal colour. Results dedupe per BSSID (one row per RADIO — name-dedupe silently merged two DIFFERENT routers broadcasting one SSID and hid a multi-AP router's second radio), sort strongest-first (hand-rolled insertion sort — no <algorithm> on the Arduino TU), and the SCAN_MAX_RESULTS cap applies exactly ONCE, after the sort (it used to apply before the loop AND at insert: first-N-in-radio-order, not strongest-first). The BSSID is internal only — not in the SPP line or status JSON (buffer sized to the fixed fragments; Phase 3 owns that). New gate rules switch-teardown, wifi-persistent, retry-split, scan-honest, scan-quality in check-relay-contract.mjs + a BUSY row in check-webpage-js.mjs; mutants M23-M42 (20 new), all mutation-proven both directions. Still UNFLASHED - the bench car runs pre-1.2.1 firmware.

## v1.2.1 - U-95 Phase 1b/1c

U-95 Phase 1b/1c: the two remaining WebSocket watchdog races in loop(). (1b) ws.loop() can sit inside a partial-frame read for up to WEBSOCKETS_TCP_TIMEOUT (5000 ms - the library reads that timeout in its OWN translation unit, so a #define in the sketch is a no-op and the owner's IDE passes no build flags); (1c) broadcastStatus() measured 5-8 s writing to a client that stopped draining TCP (U-77: 5 s of no-progress retries + the core's 3 s socket send timeout) and ran with NO connectedClients() guard - the 1 s ticker built a 1536-byte JSON and walked every socket every second of every uptime, even with nobody listening. Both operations drew on whatever was LEFT of the pass's window against a 5000 ms watchdog fed once per pass, which made both stopwatches racing the reset. The fix: (1) feed the watchdog IMMEDIATELY before each untunable operation, so each gets a fresh window; (2) client-guard the broadcast - no client returns before the JSON is built; (3) LOOP_TASK_WDT_TIMEOUT_MS 5000 -> 10000, putting the library's measured worst (8000 ms) STRICTLY below the watchdog instead of tying it (a tie is the F-77 failure mode again). Cost, stated plainly: a genuine hang now resets in 10 s instead of 5 s; the benefit is that a phone sending a half-frame can no longer reboot the car. New gate rules ws-loop-feed, ws-guard, ws-broadcast-feed, ws-budget in check-relay-contract.mjs, all four mutation-proven (mutants M19-M22 in mutate-gates.mjs). Still UNFLASHED - the bench car runs pre-1.2.1 firmware.

## v1.2.0 - U-95 Phase 0

U-95 Phase 0: the car now RECORDS why it rebooted instead of printing it to a serial line nobody is reading. .trigger_panic = false, so a hung loop() finally reports TASK_WDT instead of an indistinguishable PANIC; a persisted record (boot count, crash count, last reset reason, boot heap, and the phase it was executing) is on the OLED header, in /status JSON, and in the page's restart row. The reset LOOP ITSELF is not fixed yet - that is Phase 1, which bounds the page-send budget below the watchdog and feeds the watchdog in the send loop. This is evidence-gathering, not a cure: after this build the owner can finally tell us WHICH operation kills the car, and the two known suspects are named in the record as 'http-root' and 'relay-tls'.

## v1.1.0 - U-94 Phase 2

U-94 Phase 2: the Internet/relay transport is compiled in but INERT - no dialling unless RELAY_HOST is set, no authentication without a stored token, and it is not yet flashed or bench-tested. Nothing about existing Wi-Fi/BT behaviour changed.

## v1.0.5 - U-92, owner

U-92, owner: "webpage has double entry of esps own hotspot" + "says, car switched to 4wdcar_wifi but doent switch that one" + "webpage doesnt show the router name"): the Reserved-name guards are FIELD-BOUNDED now (the old checks compared the whole unterminated command field, so ADD/USE/WIFICFG with a password after the name bypassed them and the hotspot entered the registry / the active pair), poisoned rows are PURGED at load and the promotion refuses the own name (self-heals the double entry); "back to my own hotspot" actually LEAVES the router now (the T-66 return armed no radio work at all — it answered USED and stayed associated); the webpage placeholder shows the car's CURRENT network and WIFICFG;STORED; is finally parsed *(Truncated: this entry was already cut off mid-sentence in the original Config.h define.)*

## v1.0.4 - U-90, owner

U-90, owner: "car stuck on one router, doesnt connect even if on same network" + "allow those comming soon modes" + "menu for router change not working"): the rejoin MODE step is now UNCONDITIONAL — WiFi.mode(WIFI_AP_STA) restarts the driver on EVERY explicit switch, so begin() never lands on a still-associated STA; the stale association used to satisfy the WL_CONNECTED resolve check, so a switch to ANY router "succeeded" instantly on the OLD network and no retry ever ran (stale lease, car unreachable on its own network). Webpage: coming-soon modes are SELECTABLE (owner override of the U-79 page gate; labels keep the (coming soon) hint; unknown tokens still NACK), and the router dropdown no longer preselects a network nothing confirmed (F-57 for the router list) *(Truncated: this entry was already cut off mid-sentence in the original Config.h define.)*

## v1.0.3 - U-79
U-79: webserver mode REMOVED (no second Wi-Fi setup); transport power policy (idle = both broadcast, one client = other off, drop = both return); boot/resume prefer last-used transport; page: caps-gated mode dropdown, drag-safe speed slider, add-router + switch feedback

## v1.0.2 - U-77
U-77: task-watchdog reboots eliminated — WS stalled-client heartbeat, total-bounded root-page write, per-write HTTP send cap, mDNS success/failure condition un-inverted (mDNS could never advertise), retry-branch breadcrumbs. Baseline was v1.0.0 (fresh repo from Genum_WIRELESS_CAR 1.8.4 @ d0bf8c0; renamed identity: BT "4WD CAR" / AP 4WDCar_Wifi / AP 192.168.245.1/24

