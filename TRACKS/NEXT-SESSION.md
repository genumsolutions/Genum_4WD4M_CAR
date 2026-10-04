# NEXT SESSION — Genum_4WD4M_CAR (updated 2026-10-04: U-92 own-hotspot fix; firmware 1.0.5)

**U-92 (2026-10-04) - THE OWN HOTSPOT: A DOUBLE ENTRY, A "SWITCH" THAT NEVER LEFT, AND A HIDDEN
JOIN-YOUR-OWN-BEACON TRAP.** Owner: *"webpage has double entry of esps own hotspot please remove
that too"* · *"through the webpage the router is not switching to the esp own hotspot"* ·
*"says, car switched to 4wdcar_wifi but doent switch that one, but switches to other router
fine"* · *"webpage doesnt show the router name but shows select router"* · *"check the app also
for same problem"*.

1. **THE DOUBLE ENTRY — the Reserved guards never actually guarded.** The checks compared the
   WHOLE unterminated command field (`strcasecmp(ssid, WIFI_AP_NAME)` where `ssid` still carried
   `;<password>`), so any ADD/USE/WIFICFG with a password after the name BYPASSED the refusal
   and 4WDCar_Wifi entered the saved registry — and `loadSavedRouters()`'s boot promotion then
   copied the poisoned active pair in and persisted it. Now field-bounded everywhere (ADD, USE
   saved-match + add-path, WIFICFG gets a guard it never had), the poisoned rows are PURGED at
   load (NVS rewritten, logged) and the promotion refuses the own name — an already-poisoned
   car heals itself on first boot, no owner action.
2. **THE "SWITCH" THAT NEVER LEFT.** The T-66 own-hotspot return answered `ROUTERS;USED;
   4WDCar_Wifi` and then armed NOTHING — `pendingRejoin_` keys on `hasStoredWifi_`, which the
   branch had just cleared, so `REJOIN_STEP_BEGIN` early-returned with zero radio work and the
   car stayed on the router ("switches to other router fine" — those have a stored target).
   Now the `!hasStoredWifi_` BEGIN case hands off to the existing one-call-per-pass teardown
   (disconnect → `WiFi.mode(WIFI_AP)` → startOwnAP) — the sequence the give-up path already
   proved on hardware — and the serial line says which of the two paths ran. The CLEAR path
   (T-62) inherits the same fix.
3. **THE HIDDEN TRAP.** Unguarded `WIFICFG;4WDCar_Wifi;<pass>` stored the AP as the ACTIVE pair:
   boot auto-join then tried to associate the car with its OWN beacon. `loadStoredWifi()` now
   refuses + erases it; WIFICFG refuses it outright.
4. **WEBPAGE (WebPage.h):** the router dropdown's placeholder DISPLAYS the car's current
   network (`Now on: <ssid>` — car truth, F-57: never an asserted request; the joined SSID rides
   the rebuild signature), and the page finally parses `WIFICFG;STORED;<ssid>` — it used to look
   for `WIFICFG;OK;`, a reply the car has never sent, so "Add router…" saves were never
   confirmed.
5. **APP CHECK (owner asked):** no app change needed — `routerList.ts` already dedupes by name
   ("the user must not see two rows that are one router") and `switchableRouters()` excludes the
   own AP; the app's hotspot switch sends the same `ROUTERS;USE` this round fixes, and the
   U-80 phone-switch prompt fires on the USED answer as before. The car-side fixes serve the
   app automatically.
6. **Verified:** page-js gate ALL STAGES PASS; xtensa -fsyntax-only CLEAN (WebServerComm /
   BluetoothComm / ModeManager). **Firmware 1.0.5.** Device rows U-92-1..4 (below) — all
   unticked; the car was not on the bench network this session.

**U-90 (2026-10-04) - THE CAR WAS STUCK ON ONE ROUTER, AND EVERY LATER "SWITCH" WAS A LIE.**
Owner: *"the car seems stuck on one router and doesnt connect even if on same network"* ·
*"the mode toggle doesnt allows all other comming soon modes to change through the drop down
menu, all those are dim please allow those comming soon too that changes the modes on the car
too"* · *"the menu for router change is also not working"* · *"please fix these first and push
properly".*

1. **THE ROOT CAUSE — a switch resolved as SUCCESS on the router it never left.**
   `rejoinNetwork()` arms the U-71 step machine; `REJOIN_STEP_MODE` ran `WiFi.mode(WIFI_AP_STA)`
   only `if (WiFi.getMode() != WIFI_AP_STA)`. After the car's FIRST switch the driver is already
   AP_STA (beginAlwaysOn() leaves it there), so every LATER switch skipped the driver reset —
   and `REJOIN_STEP_BEGIN` issued `WiFi.begin()` ON TOP of the still-associated STA. The IDF
   refuses config on a busy STA (the exact U-76 error: *"sta is connecting, cannot set
   config"*), and the resolve block then saw `WiFi.status() == WL_CONNECTED` — satisfied by the
   OLD association — so the switch resolved INSTANTLY as success on the OLD network: no rejoin,
   no fresh DHCP lease, and no retry (no failure was ever observed). That is the stale-lease
   *"stuck on one router, won't connect even on the same network"* report. **FIX:
   `REJOIN_STEP_MODE` is now UNCONDITIONAL** — one driver restart per explicit switch (the same
   cost the first-ever switch already paid), still one radio call per loop pass (U-71/F-24
   untouched). Re-joining the router the car is ALREADY on also works now: fresh association,
   fresh lease.
2. **COMING-SOON MODES ARE SELECTABLE (owner override of the U-79 page gate).** The U-79 gate
   existed so a tap could never park the car in a stub; the owner now wants the dropdown to
   change the car's mode. WebPage.h: the CAPS sync NEVER disables an option and keeps the
   "(coming soon)" suffix as information only; unknown tokens still earn `NACK;E=UNKNOWN_MODE`
   (F-62). Known consequence, accepted by the owner: entering a stub mode gates the drive
   controls until 4WD4M is picked again (one dropdown tap).
3. **Router dropdown honesty (F-57 applied to the router list).** The rebuild used to
   re-select whatever the select happened to show — asserting a switch request nothing
   confirmed. Now it never preselects; the neutral placeholder holds until the user acts.
4. **Verified:** page-js gate ALL STAGES PASS locally (mode list written once per real change,
   never per frame, never while open; tokens cross-checked both ways; one command one answer;
   beacon-park policy intact). Firmware syntax gate (xtensa g++ -fsyntax-only, core 3.3.12 +
   esp32-libs SDK + WebSockets) CLEAN on WebServerComm/BluetoothComm/ModeManager — note for the
   next session: on THIS machine the SDK headers live at
   `Arduino15/packages/esp32/tools/esp32-libs/3.3.12/include` (NOT the classic
   `hardware/esp32/*/tools/sdk` path), and `arduino-cli.exe` is Device-Guard-blocked. **Firmware
   1.0.4.** NOT device-verified: the car is not on this bench's network this session (probe of
   192.168.1.100/.57 dead, ARP shows no car) — the U-90 rows in DEVICE-TESTS.md need the
   owner's flash + bench run (huge_app, upload.speed=115200).

**U-87 / U-88 (2026-10-03, backfilled 2026-10-04) - RECOVERED FROM THE COMMIT BODIES
(`0fdb1d1` + `5dddd5a` + `12c930c`, `5e04df7`).** This ledger was not written while the rounds
happened; the git commits are the primary record.

1. **U-87 (`0fdb1d1`, corrected in `5dddd5a`) — the setup beacon parks only while the car is IN
   USE.** Owner asked whether the car's own hotspot staying active while it works on a router is
   necessary. First version parked the beacon whenever the STA was connected — and made the car
   UNREACHABLE on the very next bench boot (joined a router the bench was not on; no beacon, no
   address, no way in). The mistake: "the car is on a router" does not mean "YOU can reach that
   router". The corrected policy: `in use -> parked, idle -> up` — `inUse` is computed from
   `ws.connectedClients()`, a connected client being proof the user is on the router already.
   Bench-proven idle (beacon up at idle, restored the moment the last client leaves, page kept
   serving through a park — it is a beacon, not the web server); parking WHILE a client is
   connected is still unverified on hardware (the car sits on a network the bench cannot reach).
   Never calls WiFi.mode() on this path — driver restart is the documented U-75 watchdog-cause.
   T-65 narrowed, not reversed: during a switch the beacon is still up, which is when it matters.
2. **U-88 (`5e04df7`) — one WebSocket command, one answer.** Owner: *"switching between the
   router is not working and the addition and editing and deleting of the router is not properly
   built in the app"*. Found by driving the real car: `ROUTERS;DEL` produced TWO answers
   (`DELETED` then `ERROR;Not saved`) because `handleWebSocketEvent()` dispatched each line
   immediately AND queued it — every Wi-Fi command ran twice (ADD looked fine because it is
   idempotent; USE drove the radio through two full rejoins). T-1 is explicit: ONE command path,
   lines are queued and dispatched by ModeManager::run(); the immediate call was removed.
   Measured after, on the car: LIST/ADD/DEL/USE and even unknown commands answer exactly once
   (the NACK count halving from 2 to 1 is the cleanest proof). U-81 device suite 12/12 after the
   change. Gate stage 9 fails if the WS path ever calls handleCommand() again or stops queueing.
   Verified with a throwaway SSID, removed afterwards; the owner's registry untouched.


1. **THE "WEBPAGE CONTROLS STOPPED WORKING" MYSTERY — SOLVED BY THE OWNER'S OWN SERIAL LOG.**
   The U-77 breadcrumbs showed `[MODE] set 4WD4M -> ESP32(WEBSERVER)`: the page's mode dropdown
   (which offered the retired ESP_SER token) parked the car in a COMING SOON stub — and stubs gate
   drive AND `#<n>` speed. That is the "double Wi-Fi setup": an always-on web layer PLUS a dead
   webserver mode that only ever broke things. Per the ruling, ESP_SER/ESP_CLI are DELETED:
   registry rows, Mode enum values, legacy ESP_SERVER/ESP_CLIENT aliases (now → NACK;UNKNOWN_MODE),
   and both page dropdown options. NVS parity is safe: old stored mode bytes 1/2 now name CS modes,
   which applyBootState() already refuses → 4WD4M.
2. **TRANSPORT POWER POLICY (the .ino owns it; transports never gated by mode):** IDLE = Bluetooth
   AND Wi-Fi broadcast (hotspot + router per boot preference); a client on ONE transport turns the
   OTHER off (3 s grace before a re-enable, and never the transport in use); when that connection
   drops, both come back. Last-used transport persists in NVS (`botcfg/last_transport`): boot and
   every resume call `armBootAutoJoin()` ONLY when Wi-Fi was last used (last-good router, short
   budget, hotspot as the fallback of last resort); Bluetooth-last boots on hotspot + BT with NO
   router hop. `armBootAutoJoin()` moved OUT of beginAlwaysOn() for exactly this. The BT
   hard-restart watchdog (T-7b) is gated on `btUp` — a deliberate power-down is policy, not a
   wedged stack, and must never be "repaired" by re-enabling BT under an active web client.
3. **WEBPAGE (WebPage.h):** mode dropdown gated by the car's CAPS map (non-LIVE = disabled,
   "(coming soon)" — a tap can never kill drive again); speed slider no longer fights the 1 s
   status sync while dragging (explicit interaction flag + 1.5 s tail — the old
   `document.activeElement` guard misses on touch, where the range is often NOT focused during a
   drag); router dropdown = ONE place (saved routers + own hotspot from car truth) plus an
   "Add router…" entry that sends WIFICFG (stores AND switches) with an honest "Switching…" note —
   the link drop during a switch is EXPECTED (hotspot channel hop) and the page now says so and
   reconnects automatically.
4. **Verified:** compile gate (xtensa-esp32-elf-g++ -fsyntax-only, core 3.3.12 + WebSockets 2.7.2)
   green on ModeManager, WebServerComm, BluetoothComm, Oled_1_3, and the .ino as C++.

**Bench:** flash 1.0.3 (huge_app). Expect on serial: boot preference line
(`[BOOT] Last used: Bluetooth - staying on hotspot + BT` or the `[WIFICFG] boot:` join line),
`[POWER] Bluetooth off - web client active` the moment the webpage/app-over-WiFi connects, and
`[POWER] Wi-Fi off - Bluetooth client active` the moment an SPP client connects. Drive from the
webpage must NEVER die from a mode tap again — no dead mode exists to tap.

---

**U-79 (2026-10-03) - THE WEBSERVER MODE IS GONE + TRANSPORT POWER POLICY. Owner: *"remove the
webserver mode of the car totally ... so that the car doesnt have the double setup for the wifi
setup"* · *"all the networks at one place ... the default hotspot is the last option"* · *"broadcast
both bluetooth and the wifi until user selects one and turn off the other to save power"* · plus
the device evidence: `[MODE] set 4WD4M -> ESP32(WEBSERVER)` in the serial log — the webpage's mode
dropdown had parked the car in the retired CS stub, which gates ALL drive commands ("COMING SOON").
That is the "webpage controls stopped working" report, root-caused.** Version -> 1.0.3.

1. **ESP_SER / ESP_CLI REMOVED FROM THE REGISTRY (owner ruling, ask_user answered "Remove tokens
   fully").** `Mode::SERVER`/`Mode::CLIENT` deleted from the enum, the registry, and the legacy
   `ESP_SERVER`/`ESP_CLIENT` alias branches in setModeFromString — sending one now earns
   `NACK;E=UNKNOWN_MODE` instead of booting a dead mode. NVS parity is free: old stored indices 1/2
   now name CS modes, which applyBootState() already refuses (donor-era NVS cannot restore them).
   The mode dropdown on the page lost both options. **Old cars keep these modes — this ruling is
   4WD4M-only; the app stays caps-driven (parseCapsBody) so nothing breaks fleet-wide.**
2. **TRANSPORT POWER POLICY (owner ruling, verbatim):** idle (no client anywhere) = Bluetooth AND
   Wi-Fi both broadcast until the user picks one; a client on ONE transport switches the OTHER off
   (`BluetoothComm::stop()` added — SerialBT.end(); begin() is idempotent and brings it back; the
   web layer uses the existing stop()/beginAlwaysOn()); when that connection goes away both return
   after a 3 s grace. The transport WITH a client is never touched. The BT hard-restart watchdog is
   gated on `btUp` — a policy-induced BT absence can never trigger the F-60 restart.
3. **BOOT/RESUME PREFER THE LAST USED TRANSPORT** (NVS `botcfg/last_transport`, new):
   Wi-Fi last -> armBootAutoJoin() runs (last-good router, short budget, own hotspot as the
   fallback of last resort — U-71 semantics kept); Bluetooth last -> the router hop is SKIPPED
   (`[BOOT] Last used: Bluetooth`). armBootAutoJoin() therefore no longer fires unconditionally
   from beginAlwaysOn() — beginAlwaysOn() is now also the power-policy RESUME path and the .ino
   decides per last-used transport.
4. **WEBPAGE (WebPage.h):** mode dropdown is gated by the car's CAPS map (non-LIVE -> disabled +
   "(coming soon)", so a tap can never kill drive again); the speed slider no longer fights the
   1 s status sync while dragging (explicit drag flag + 1.5 s tail — the old focus guard misses on
   touch, where the range is often not document.activeElement mid-drag: the "speed control not
   working properly" report); the router dropdown is ONE place for every network — saved routers
   (REQ_NETW) + an "Add router…" entry that sends WIFICFG (stores AND switches); switching shows an
   honest "Switching… … this page reconnects when this phone joins <ssid>" note (the AP link drop
   during a channel hop is EXPECTED, U-74).
5. Compile gate: all five touched sources pass xtensa-esp32-elf-g++ -fsyntax-only (core 3.3.12 +
   WebSockets 2.7.2; NetworkClientSecure is the 3.3.12 header dir WebSockets' WiFiClientSecure.h
   include resolves through). CI runs on push.

---

**U-77 (2026-10-03) - THE WATCHDOG REBOOT CLASS IS CLOSED. Owner log: TWO MORE `reset reason: 6
(TASK WATCHDOG, loop hung >5s)` reboots, ~92-97 s after boot, WITH the U-75 breadcrumbs exhausted.**
Both hangs left NO radio breadcrumb, so the blocker was not a radio call - it was the two remaining
UN-BOUNDED blocking paths in loop(), both client-write paths, both now proven against the installed
sources (WebSockets 2.7.2 + arduino-esp32 3.3.12):

1. **WS stalled-client broadcast** (the ~29 s gap in the owner log is the TCP buffer-fill time):
   `broadcastStatus()` runs EVERY SECOND to every client; a phone that stalls (screen off, app
   killed without a close, or its link orphaned by the AP channel hop mid-switch - U-74) stops
   draining TCP. `WebSockets::write()` retries until `WEBSOCKETS_TCP_TIMEOUT` (5 s) of no progress
   and each inner `tcp->write()` itself blocks on the full lwIP buffer (core default socket send
   timeout 3 s) - one stalled client = loop() blocked 5-8 s = WDT = reboot, no breadcrumb.
   **Fix: `ws.enableHeartbeat(3000, 1000, 2)`** in `setupWebSocket()` (supported by 2.7.2 server
   side, new clients inherit it): a staller is pinged and dropped in ~8-9 s, long before its
   buffers can wedge a write.
2. **HTTP root-page send had NO total bound**: `server.send_P(PAGE_HTML)` (~18 kB) to a
   slow-but-moving client (weak signal right after a network switch - the owner's "restarted as
   soon as I tried to access the webpage") blocked loop() chunk after chunk past 5 s even with
   every single write under the 3 s default. **Fix: `serveRootBounded()`** - exact Content-Length
   headers, then the body in 512 B chunks with a 500 ms per-write socket cap
   (`armHttpGuard()`, applied to EVERY HTTP handler) and a 3 s TOTAL budget; a hopeless client
   gets a truncated page (it is dead anyway) instead of a rebooted car.
3. **mDNS could NEVER advertise - inverted condition.** `if (MDNS.begin(...) != 0)` treats
   SUCCESS (non-zero) as failure: printed "[MDNS] unavailable" and returned before addService(),
   while a REAL failure fell through to addService(). Now `== 0` = failure, honestly logged once.
4. **Boot auto-join was silently dead.** `loadSavedRouters()` wiped `lastGoodSsid_` right after
   `loadStoredWifi()` had loaded it from NVS and never re-read - `armBootAutoJoin()` always saw an
   empty last-good router (owner log boot 1: no `[WIFICFG] boot:` line). Registry load no longer
   touches last-good state; `loadStoredWifi()` is its single owner (and now ONE read-only NVS open
   instead of two, one needlessly writable).
5. **Retry-branch radio calls got breadcrumbs** (`[RADIO] retry/disconnect` / `retry/begin`) - they
   were the last silent radio calls; a hang there would have been another breadcrumb-less reboot.
6. **stopMdns() was dead code** - now called from stop(). Duplicate `apRunning_ = true;` in
   startOwnAP() removed. Version -> 1.0.2, Readme synced (was stale at 1.0.0).

**Verification:** all edited sources pass the xtensa-esp32-elf-g++ `-fsyntax-only` gate against
core 3.3.12 + WebSockets 2.7.2 (adapted from the build/v2 compile_commands.json). CI compile gate
runs on push. **Bench:** flash `main` (huge_app), then repeat the owner's repro: switch car to home
router from the app, phone follows onto the router, open the webpage. Expect: NO reboot, and on a
radio event the `[RADIO] ...` breadcrumbs name every step. If a WDT ever fires again the last
breadcrumb names the step (U-75 rule now covers 100% of radio calls). Stale cross-machine
`build/` artifacts removed from the working tree (gitignored anyway).

---

**U-71 (2026-10-02) - FINISHES THE INTERRUPTED U-70 WORK. Owner: *"i used the other router switch
thing but the car restarted as soon as i tried to access the webpage"* · *"save the last connected
router configs for the power restart too if that router is in the range to connect if not goes back
to esps own hottspot"* · *"remember old setting in the database"*.** The owner asked whether the
previous round had been interrupted: **yes** - U-70 deferred only `WiFi.begin()`, and the boot
auto-join and the last-good persistence had not been started. All of it is now done.

**1. THE RESTART - THE STRONGEST EXPLANATION YET, AND IT IS A REAL DEFECT.** U-70 deferred only the
`begin()`. **`WiFi.mode()` and `softAP()` were still running synchronously inside `rejoinNetwork()`,
which is called from `ModeManager::run()` INSIDE `loop()`.** On ESP32 `WiFi.mode()` RESTARTS the WiFi
driver; a driver restart plus `softAP()` plus `begin()` can occupy `loop()` long enough to trip the
**5 s task watchdog - and the WDT's action IS a reboot (F-24)**. That fits the report exactly: the
switch churns the radio in the command path, and the very next thing to arrive is an HTTP request
served by that same driver. `rejoinNetwork()` now does **NO RADIO WORK AT ALL** - it only arms a
state machine that `handle()` advances with **one radio call per loop pass**
(`REJOIN_STEP_MODE` -> `REJOIN_STEP_AP` -> `REJOIN_STEP_BEGIN`). `loop()` returns between steps, the
WDT is fed, and the web server can **answer** a request in between - which is exactly the window that
used to be missing. U-70's `staBeginPending_` flag is **removed**, not layered on top of: U-71
supersedes it. *I am not claiming the restart is fixed until U-70-4 has been run - but this is now a
mechanism with an explanation rather than a mystery, and the reset-reason line will confirm or refute
it.*

**2. THE CAR REMEMBERS THE ROUTER THAT WORKED, NOT THE ONE IT WAS ASKED FOR.** `storedSsid_` is
written to NVS the moment `ROUTERS;USE` stores the pair - **before any association is attempted** - so
a FAILED switch overwrote a working router with a broken one, and a boot that trusted it would
dutifully retry the router that had just failed. New `lastGoodSsid_` / `lastGoodPass_` (NVS
`last_ok_ssid` / `last_ok_pass`) are written **only from the connect-success path**. *"What we tried"*
and *"what worked"* are now separate facts, and only the second survives a power cycle.

**3. BOOT AUTO-JOIN, DELIBERATELY BOUNDED. `armBootAutoJoin()` runs at the end of
`beginAlwaysOn()`.** ⚠ **THIS REVERSES R-16/T-64** ("the own AP is THE default network; a stored
router is joined ONLY on explicit request") - the owner has now overridden that ruling, and it is
recorded here rather than quietly done. **The reason the rule existed still stands, so the override is
bounded:** the boot attempt gets `BOOT_STA_TIMEOUT_MS` (6 s) and `BOOT_STA_MAX_RETRIES` (**1**), then
settles AP-only immediately. The full retry budget stays reserved for an **explicit** user switch,
where the user is watching. An unreachable router therefore cannot keep the car off its own hotspot
for more than a few seconds - and the hotspot is the **only way in** when the router is absent. A
router that has never once connected is never auto-joined, so the boot window is never spent on
something already known to fail. The budget is per-attempt state (`staTimeoutMs_`, `staRetryCap_`) and
restores to normal the instant an association succeeds.

Unchanged: the saved-router registry, the protocol, safe-stop, the scan, and the AP itself.

**Ship evidence:** car `7ba6152` - **Arduino CI ✓ `37035295780`**, 69,764 B SRAM (21%, +112 B).
App side of the same report is `d495839` (remembering CONFIRMED router changes in AsyncStorage and the
Supabase `car_profiles` row - a real gap U-68 introduced, where the user's most common action,
switching a router, was never saved at all). Device rows: `genumsolutions-app/mobile/TESTING.md`
**U-71-1..3**. **NOT flashed yet.**
**U-70 (2026-10-02) - THE CAR ANNOUNCED THE ROUTER IT WAS ASKED TO JOIN, NOT THE ONE IT WAS ON.**
Owner bench: *"the car is displaying the new router in its oled but not switching actually to that new
router network and the car still show the website only on its own esp hotspot. and doesnt show to new
router"* + *"the car restarts when the app directs to cars webpage"*.

**1. THE CAR LIED, AND THAT IS WHY THE SECOND SYMPTOM WAS INVISIBLE.** `storedSsid()` is written the
moment `ROUTERS;USE` stores the pair - **before any association is attempted** - and both the STATE
line's `SSID=` and the JSON `ssid` used it. A FAILED switch therefore announced the new router while
the car sat on its own hotspot, and nothing could tell the difference. **The owner's own observation
was the lie, not the success it looked like.** The OLED itself was already truthful (it reads
`WiFi.SSID()` through `extraInfo`); it was the **app-facing** fields that lied, which is exactly why
the two disagreed. Now `SSID=` / `ssid` is the new `joinedSsid()` (joined STA SSID, else the own
broadcast id, else empty), and the requested target ships **separately** as `WANT=` / `"want"` so a
switch in progress is visible instead of indistinguishable from a finished one. *"Which router did
the user ask for"* and *"which router are we on"* are different questions and must stop sharing one
field.

**2. THE JOIN STARTED IN THE SAME TICK AS A MODE CHANGE.** `rejoinNetwork()` did
`mode(WIFI_AP_STA)` -> `softAP()` -> `WiFi.begin()` inside one call. On ESP32 **`WiFi.mode()`
RESTARTS the WiFi driver**, so a `softAP()` and a `WiFi.begin()` issued in the same tick race a
driver that is still initialising: the STA fails to associate while the softAP still comes up —
precisely *"accepted and displayed the new router, never actually joined it, still reachable only on
its own hotspot"*. Fixed conservatively: `WiFi.mode()` is now called **only when the radio is not
already `AP_STA`** (so switching routers — the common case — never restarts the driver at all), and
`WiFi.begin()` is handed to `handle()` on the **next** pass via `staBeginPending_` /
`beginStaIfArmed()`, so the driver has a tick to settle. Retry budget, the AP, safe-stop and the
protocol are untouched.

**3. THE RESTART IS INSTRUMENTED, NOT FIXED.** *"the car restarts when the app directs to cars
webpage"* could not be acted on: a watchdog abort, a stack smash, a brownout and an intentional
reset all look identical from outside, and it cannot be reproduced on the bench. Rather than guess,
`setup()` now prints **`esp_reset_reason()` on every boot** and shouts when the reason was not
power-on/external/deep-sleep. **That is what turns the next occurrence into an answer: `TASK
WATCHDOG` means `loop()` blocked, `PANIC` means a crash, `BROWNOUT` means undervoltage.** Read the
serial line at 115200 and paste it — do not record the restart as fixed or as not-reproducible until
that line exists. (`TESTING.md` U-70-4 in the app repo is the row for it.)

**Ship evidence:** car `13a406f`, **Arduino CI ✓ `37033364835`**, 69,772 B SRAM (21%, −8 B). App side
of the same report is `b21fed5` (the Bluetooth scan list — bounded scrolling window, hidden whenever
any link is up). Device rows: `genumsolutions-app/mobile/TESTING.md` **U-70-1..4**; **U-70-1/2 need
this flash, U-70-3 is app-only.** NOT flashed yet.
**✅ U-68 PHASE 3 SHIPPED 2026-10-02 — THE CAR'S NETWORK LAYER REBUILT. Arduino CI ✓ `37012141471`
(1,748,602 B = 55% of the 3 MB `huge_app`, 69,772 B SRAM = 21%). NOT FLASHED YET.** Three defects
the app could not work around, all on the connection/provisioning path:

1. **`ROUTERS;SCAN` DID NOT EXIST — D3.** The app has sent it since the Connections-Hub round and
   the car answered `ROUTERS;ERROR;Syntax` every time; the app then discarded that answer, so the
   failure was invisible. **There was no "networks near the car" list anywhere in the system** —
   the owner's *"the selecting of the network is not proper"*. Now implemented and delivered BOTH
   ways so one app parser serves both links (F-66): the SPP line `SCAN;<ssid>,<rssi>,<1|0>;…`
   (or `SCAN;NONE`) and `"scan":[{"ssid":…,"rssi":…,"open":…}]` in the status JSON. An empty
   array / `SCAN;NONE` until a scan has run, so the app can tell *not scanned yet* from *scanned,
   found nothing*. Names only — a scan can never reveal a password (W-14).
   **ASYNC ON PURPOSE:** `WiFi.scanNetworks()` blocks for seconds; from the command path it would
   starve `loop()` and trip the task watchdog (F-24), **rebooting the car** — exactly what F-60
   exists to stop. So the command only ARMS the scan (`startScan`), `handle()` runs it after a
   settle delay (`runScanIfDue`) and collects it when the radio finishes (`collectScanIfDone`),
   keeping the strongest reading per SSID and broadcasting on both transports. One pass at a time,
   bounded by `SCAN_TIMEOUT_MS` so a radio that never finishes reports honestly instead of leaving
   the app waiting. Note the caveat: while the STA is ASSOCIATED the radio is busy and the channel
   follows the router, so **strength is a hint only** — the saved-router list stays the source of
   truth for what the car will actually join.
2. **The router reply was TRUNCATED at 63 characters — D7.** `ModeManager::pendingReply_` was 64
   bytes while the command hook builds its reply in 160, so `ROUTERS;<ownAP>;<ssid>…` was cut off
   mid-list as soon as two or three routers were saved. Nobody noticed because the app threw the
   reply away (that was D2). Now the answer is consumed, a truncated one would be a *wrong* one, so
   the buffer matches the largest reply the hook can build and the STATE builder's buffer was grown
   (`buf[248]` → `buf[448]`) to carry a full `REPLY=` in one line.
3. **A credential rode every acknowledgement — F-68.** Each WebSocket command was answered
   `{"ok":true,"cmd":"<the full line it was sent>"}`, so `ROUTERS;ADD;Home;hunter2` came straight
   back to the phone and would land in any WebSocket proxy log. The HTTP route did the same **and
   broadcast it to every WS client**. New `redactForAck()` confirms the request by operation + SSID
   only. *An acknowledgement confirms a request; it must never replay it.*

**Bench:** the app half is U-68 (`genumsolutions-app` — see its `TRACKS/NEXT-SESSION.md`); device
rows **`genumsolutions-app/mobile/TESTING.md` U-68-4** (nearby-network selection) are the ones that
**cannot pass until this is flashed**, because no earlier binary has `ROUTERS;SCAN`. The
flash-verify car rows to add alongside: T24–T26 (U-67, still open) and T21–T23.

**Lesson recorded (it cost two CI runs):** the phase-3 commit shipped with `handleRouterCommand()`
closing *inside* its if-chain — a follow-up edit had replaced the SCAN branch's body along with the
anchor it matched — so everything after it parsed as declarations. The **Arduino CI compile gate
is the only thing that can catch a brace-level mistake in firmware**; treat a red compile as a real
defect, never as CI noise. (Second failure was `strcasecmp` on a `char[33]`'s `.c_str()`.)
# NEXT-SESSION — Genum_4WD4M_CAR

**⚠️ U-67 (2026-10-02, owner bench) — THE ROUTER-SWITCH RESET IS FIXED, AND IT WAS NOT
`cd3158f`. ROOT CAUSE: the BT watchdog, not `ROUTERS;USE`.** Owner, verbatim: "the 4wd4m car is
resetting when the wifi router is switched from the app and the car is not always switching
properly… the car doesnt go switch the router in one go propely." Two separate firmware bugs, both
real, neither the one the ledgers predicted:

1. **THE RESET — `BluetoothComm::restart()` + the .ino watchdog.** `restart()` ended a failed
   re-init with **`ESP.restart()`**, and the .ino armed the watchdog on **"no BT client"** rather
   than on "a client that dropped". Switching the car to the home router takes the phone OFF the
   car's BT, so `hasClient()` goes false and STAYS false; after `BT_HARD_RESTART_MS` (120 s) the
   watchdog fired, tore bluedroid down, and the escalation rebooted the car **mid-handoff**. It came
   back on its own AP and the switch never completed. Worse than a switch bug: the throttle is
   60 s, so an **unattended car parked on the router rebooted on a loop**. Fixed: no
   `ESP.restart()` from the BT path ever again (a failed re-init is reported and the car KEEPS
   RUNNING on AP/router — it stays reachable and driveable), and the watchdog arms only on a link
   that was CONNECTED and then dropped (`btEverConnected`): "never had a client since boot" is
   idle, not wedged. **FAILSAFES F-60: a transport failure must never take down the whole car.**
   *Flashing the previously-committed firmware does NOT fix this — `cd3158f` never touched it.*
2. **"NOT ALWAYS SWITCHING / DOESN'T DO IT IN ONE GO" — `WebServerComm::handle()`.** The 8 s
   `WIFI_STA_TIMEOUT_MS` was treated as the WHOLE switch: on timeout it cleared `pendingRejoin_`
   and set `startedAsAP`, which made the retry branch (`else if (pendingRejoin_ || !startedAsAP &&
   …)`) **unreachable** — so `STA_MAX_RETRIES` never ran and ONE flaky association failed the
   switch **permanently**, the car silently left on its own AP. Fixed: the timeout hands OFF to
   that existing capped budget (up to 6 more attempts) and only the spent budget settles AP-only
   (the cap still holds, so an unreachable router cannot blank the softAP beacon forever —
   FIN-40/D2 intact). Each retry also re-arms `connectingPending_` with its own 8 s window: that
   branch is an `else if` behind `if (connectingPending_)`, so without re-arming, a successful
   retry was **invisible** (no `updateExtraInfo`/`sendState` — the app sat on "getting an address"
   while the car was already on the router) and `pendingRejoin_` stayed set, so the branch kept
   yanking a WORKING association down every 10 s.

**Bench:** app side is U-67 (one standard switch confirm — `genumsolutions-app` `bfe1cda`); device
rows **U-67-1..4** in `genumsolutions-app/mobile/TESTING.md`, of which **U-67-3/4 (the reset and
the one-go switch) are the firmware rows and CANNOT pass until this is flashed.** Run them FIRST —
if the flash did not take they reproduce and every later router row is meaningless. The car-side
rows to add alongside: the two above plus the existing **T21–T23**.
**SHIPPED (owner go given 2026-10-02 for the firmware push):** `26d41d5` (fix) · `3f080c2`
(TRACKS), pushed `030726f..3f080c2`; **Arduino CI ✓ `37001533166`** — 1,746,294 B flash (**55%**
of the 3 MB `huge_app` partition) · 69,604 B SRAM global (21%). App half: `bfe1cda`/`7919d5c`,
CI ✓ `37000785055`, OTA ✓ `37000785239` published (group `75dc154a`), live manifest =
`Short update (7919d5c…)`. **Both halves are now on the board's owners' side: FLASH the car, and
close+reopen the app ×2.**

**Resume point (2026-10-01→02, integration session with the app — firmware changed, NOT yet flashed):**

Three firmware fixes landed for the live-integration round, all CI green, none on the board:

1. **`64c2781` — the web page no longer force-sets `MODE_ESP_SERVER` on load.** Opening the
   car's page used to `setMode()` (persisted, F-31) — a browser visit silently re-moded the
   car. The onload handler now only pre-selects the dropdown; the mode is sent only on a
   user change. F-57 (`2478735`) keeps the dropdown synced to the car's real mode and blocks
   empty-option sends.
2. **`cd3158f` — `ROUTERS;USE` adds a new router on-the-fly and switches to it** without
   rebooting the car (an unknown router used to reset it).
3. **OLED v1.0.1 directive complete (`0fc7b89` → `602e42a` → `dfda326`, CI ✓ `36909403120`):**
   BT label + `Type: SPP`, real joined SSID (parsed from extraInfo — the link-type string is
   no longer printed as the SSID), live `ipBuf` instead of the hardcoded `192.0.0.0`, UTF-8
   em-dashes removed (u8g2 rendered garbage), row buffers widened 33→40 with a width trim.
   `602e42a` did not compile (undeclared `deviceName`) — caught by CI, repaired in `dfda326`;
   owner ruling Q2=B recorded: the APP banner shows the controller name, firmware has no
   peer-name channel.

**Bench:** Round 2 rows **T21–T23** in `TRACKS/DEVICE-TESTS.md` — flash `main` ONCE (same
flash covers Round 1's pending "flash v1.0.1 + OLED line" row), then run T21–T23 + the still
untranscribed T1–T10, then the ⭐ MASTER RUN safety rows A/B. App side of this session:
`genumsolutions-app` `edb3fd7`→`f3223ce` (method switching while connected etc.), device rows
U-60-1..5 there.

**⚠ 2026-10-02 (evening) — THE OWNER'S BENCH SESSION CONFIRMED THE UNFLASHED-BINARY SYMPTOM
LIVE:** trying to switch the car to the home router from the app "resets the car and it never
reaches the new router settings" — exactly `cd3158f`'s bug (`ROUTERS;USE` on an unknown
router rebooting the flashed binary; fixed on `main`, CI green, NOT on the board). The app
side of that report is separately queued in `genumsolutions-app/TRACKS/NEXT-SESSION.md`
(handoff card needs a real confirm button + a link-loss phase). **Flash `main` before any
T21–T23 / home-router attempt — on the current board those rows cannot pass by design.**

**Previous resume point (2026-09-28, after the Connections Hub firmware round — board-id broadcast landed):**

0. **DONE `8a25dbb`: the car broadcasts its stable board id.** `WebServerComm::boardIdHex()`
   (`ESP.getEfuseMac() & 0xFFFFFF`, 6 uppercase hex) ships inside STATUS JSON as `"id":"%s"` and on
   every STATE line as `;ID=<6hex>` (ModeManager `sendStateOn`; `wifiKeys[160]` / `buf[248]`).
   Purpose: the app's per-device profile key `fw:<boardId>` — stable no matter which SSID/AP the
   car is parked on. See the app-side Connections Hub round (`feb406f` + earlier). **Requires the
   owner to flash + rerun the app device round** (old binary has no `;ID=` → app falls back to
   `wifi:<ssid>` keys + migration). Compile check pending on the bench (Arduino IDE, huge_app).

1. ~~Phase 0~~ DONE (repo on GitHub, CI green) · ~~Phase 1 v2 re-architecture~~ DONE
   (commit `a48705e`, run `36377627226` GREEN: 1,742,098 B / 55%).
2. ~~Phase 2 (app integration)~~ DONE — app `8d54fed` pushed, CI + **OTA published**
   (208/208 tests, tsc clean): OWN_AP_NAMES registry + isOwnApName, DEFAULT_AP_IP
   192.168.245.1 / DEFAULT_WS_URL across hub/Tools/Remote/RouterPanel, 3 new v2 test pins.
3. **U-49 DONE AND SHIPPED.** App `5c99357` = **v3.2.6/59 → v3.2.7/60** (`site.ts` synced), pushed to
   `main`; CI `36436791234` green; **Release `36436791172` green → `genum-solutions-3.2.7.apk`
   (42.9 MB) + `latest` + `release.json` published**; `ota-only` skipped by its release-guard.
   F-30's manifest was verified **in the published APK** with `aapt` (all four WiFi permissions,
   `NEARBY_WIFI_DEVICES` + `neverForLocation`, `usesCleartextTraffic=true`). App now has a
   single-active-transport `LinkManager` + Control Panel transport picker (BT Classic / BLE /
   Car-WiFi WS / Home-WiFi WS), Wi-Fi diagnostics, and the fixed `caps` string parser.
   Gates: **tsc 0 + 243/243 tests**.
4. **NEXT (owner): install 3.2.7 and run the device round.** It is a **new APK, not an OTA** — an OTA
   cannot deliver a manifest change. U-49-0 first (join `4WDCar_Wifi`, open `http://192.168.245.1`
   in the phone browser) to prove the car is healthy, then U-49-2..6 + T0–T10 below.
5. **NEXT (owner at the bench): device round 1** (`TRACKS/DEVICE-TESTS.md`). Flash `fc5cf96` via USB
   (115200, **huge_app**). Donor NVS may hold mode=SERVER — expect the one-time
   `[BOOT] ... retired (CS) in v2 - keeping 4WD4M` line. T3's app half is unblocked now that 3.2.7
   is published.
6. Row **T7 is stale** — there is no ESTOP in this firmware; it now tests `S`/`SPD0` + link-loss
   safe-stop instead (`TRACKS/FAILSAFE.md` item 11).
7. Round PASS → fleet port planning (Phase 4 of the master plan).

Open owner decisions: O-3 (remote pairing update — needed before any remote-first row),
O-4 (e-stop UI — **now also blocked on firmware**, which has no ESTOP handler).

**Wi-Fi is expected to work on 3.2.7, but it is UNPROVEN until the owner runs the round.**
