# DEVICE-TESTS — Genum_4WD4M_CAR

> Owner-at-the-bench sheets. Test order per standing rule: physical remote first — EXCEPT this
> car, until O-3 (remote pairing update for `4WD CAR`) lands → app-first for round 1.

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
- [ ] **U-79-3 — power policy, idle.** Car with no client: Bluetooth advertising AND 4WDCar_Wifi
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
      4WDCar_Wifi within seconds (one 6 s window, U-71 semantics unchanged).
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

### T12 — the SAME envelopes over the car's WebSocket (AP `4WDCar_Wifi`, ws :81)

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
- [ ] T3 — phone joins AP `4WDCar_Wifi`; HTTP :80 page loads; WS drive works (4WD4M, no mode switch)
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
- [ ] T10 — CAPS shows `4WD4M:LIVE`, `ESP_SER:CS`, `ESP_CLI:CS`; app marks the CS ones Coming Soon
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
      Own AP active: 4WDCar_Wifi
      Stored router available - join via ROUTERS;USE / WIFICFG (own AP stays default)
      [V2] Web layer always-on: AP=4WDCar_Wifi ip=192.168.245.1 http=:80 ws=:81
      ```
      The retired-mode guard fired exactly as designed (donor NVS held mode=SERVER from the
      WIRELESS firmware; v2 kept 4WD4M). Splash itself renders on the OLED (owner eyeballs it).

Rows (rows 1–10 carried into the v1.0.1 round above; this v1.0.0 round is closed at T0):
- [ ] T1 — BT pair to `4WD CAR` (PIN 1234) from the app; drive F/B/L/R/S
- [ ] T2 — median command round-trip over BT (10 × `SPD150` → STATE echo), recorded ms
- [ ] T3 — phone joins AP `4WDCar_Wifi`; HTTP :80 page loads; WS drive works (4WD4M, no mode switch)
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
      (a) `REQ_CAPS` returns exactly the 7 keys above; (b) the app greys out EspWebServer /
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
      WebSocket frames → Messages. Send `REQ_CAPS` from the console. **A `CAPS;…` text frame must
      appear.** Before U-81 the frame never existed: `WebServerComm::write()` returned `len`
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

## U-82 — the website's mode dropdown sent tokens the car cannot parse

Found while auditing U-81. Not a new fault: **pre-existing and 100% broken.** `setMode()` sent the
`<option value='MODE_*'>` internal id, and `ModeManager::setModeFromString` has never known those
names — it matches `MODE_REGISTRY[].token` (`4WD4M` `PATH` `OBS_US` `OBS_IR` `MAN` `AUTO` `2WD1M`)
plus the receive-side aliases `BT` and `MANUAL`. Every option therefore drew
`NACK;E=UNKNOWN_MODE;ARG=MODE_…`.

U-81 made this *visible* (the NACK now reaches the page) rather than causing it: before U-81 the
refusal was discarded by the same sink that hid everything else, so the dropdown just did nothing.

- [ ] U82-1 — **the root cause row.** Open `http://192.168.245.1`, devtools → Network → WS frames.
      Tap `AUTO`. The frame sent MUST be exactly `AUTO` — never `MODE_AUTO`. The car MUST NOT
      answer `NACK;E=UNKNOWN_MODE`.
- [ ] U82-2 — **all seven.** Repeat for `4WD4M`, `PATH`, `OBS-US`, `OBS-IR`, `MANUAL`, `AUTO`,
      `2WD1M`. Each must be sent as `4WD4M` `PATH` `OBS_US` `OBS_IR` `MAN` `AUTO` `2WD1M`
      respectively, and each must actually take (the `#mode` readout and the OLED agree).
- [ ] U82-3 — **display is not the token.** `OBS-US` is shown with a hyphen but must be *sent* as
      `OBS_US`; `MANUAL` is shown in full but must be sent as `MAN`. Reading the label as the
      wire token is the original mistake — confirm the car accepts both.
- [ ] U82-4 — **disabled rows stay refused.** `PATH`/`OBS-*`/`MAN`/`AUTO`/`2WD1M` are COMING SOON
      in the car's CAPS map, so the page disables them. Confirm the fix did not make a disabled
      row selectable, and that a disabled tap still cannot park the car in a dead mode.
- [ ] U82-5 — **the empty option stays silent (F-57).** With the car not yet reporting, tap the
      `— current mode —` row: nothing may be sent.
- [ ] U82-6 — **no regression in the truthful notes.** A genuine refusal (send `ESP_SER`, which
      this car removed in U-79) must STILL show "The car refused mode …" in the error colour. The
      U-82 fix must not make real refusals look like successes.

Verdict: **RC-PENDING** — needs a flash. The page gate `tools/check-webpage-js.mjs` stages 5+6
cover this statically in CI (reverting the one-line fix fails 7 checks); these rows prove it on
the car.
