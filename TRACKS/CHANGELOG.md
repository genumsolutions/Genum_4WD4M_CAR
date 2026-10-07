# CHANGELOG - Genum_4WD4M_CAR

Per-release firmware notes, newest first.

Extracted 2026-10-06 out of the `FW_VERSION` define in `Config.h`, where the whole
history had grown into a single 3,247-character line - 34% of that file - which
made `Config.h` hard to read, diff and blame. The notes were worth keeping; the
one-line dump was not. `Config.h:10` remains the source of truth for the value.

> UNFLASHED. Everything from 1.2.2 down was source-only when written; the bench
> car was still on an older build. See `TRACKS/NEXT-SESSION.md`.

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

