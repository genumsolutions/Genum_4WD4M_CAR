# DEVICE-TESTS — Genum_4WD4M_CAR

> Owner-at-the-bench sheets. Test order per standing rule: physical remote first — EXCEPT this
> car, until O-3 (remote pairing update for `4WD CAR`) lands → app-first for round 1.

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
- [ ] T10 — CAPS shows `4WD4M:LIVE`, `ESP_SER:CS`, `ESP_CLI:CS`; app marks the CS ones Coming Soon

Verdict: RC-PASS / RC-FAIL → snag round numbering per fleet convention.
