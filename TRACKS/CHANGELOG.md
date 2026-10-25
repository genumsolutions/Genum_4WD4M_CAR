# CHANGELOG - Genum_4WD4M_CAR

Per-release firmware notes, newest first.

Extracted 2026-10-06 out of the `FW_VERSION` define in `Config.h`, where the whole
history had grown into a single 3,247-character line - 34% of that file - which
made `Config.h` hard to read, diff and blame. The notes were worth keeping; the
one-line dump was not. `Config.h:10` remains the source of truth for the value.

> UNFLASHED. Everything from 1.2.4 down was source-only when written; the bench
> car was still on an older build. See `TRACKS/SESSION-2026-10-22-U120.md` for
> the current resume point.

## UNRELEASED — 1.3.18 (U-124 ⑫, 2026-10-25): the off-page response stops being a white screen

**Owner report (2026-10-25):** *"webpage shows white screen in browser and
the ip is not hidden anywhere … fix these when webpage is off and make
things sensible."* (Car half of item ⑫; the app half — hiding the broadcast
IP on every surface — is `genumsolutions-app`, U-124 ⑫.)

- **Root cause (source-traced):** `serveGhostPage()` — the response a
  browser gets when the Webpage toggle is OFF — called
  `server.send(200, "text/html; charset=utf-8", "")`, which declares
  **`Content-Length: 0`** (the empty body string), and then wrote the ghost
  HTML *past* that declared length. The browser honoured the 0, stopped
  reading, and rendered **nothing**: the owner's white screen. The
  "The car's web page is turned off" message the ghost page exists to show
  never reached the screen.
- **Fix:** declare the real length first —
  `server.setContentLength(len);` before the same `send(...)`, the exact
  contract `serveRootBounded()` already keeps for the 18 kB control page.
  One line; no behaviour change beyond the header.
- **Pins:** contract rule **`page-state`** extended (the
  `setContentLength(len)` → `send(200, "text/html…", "")` ordering inside
  `serveGhostPage`) + mutant **M144** (removing the line must go red).

**Gates:** contract **0 failures** · webpage-js **PASS** · syntax **9/9** ·
mutate-gates **138/138** (M144 new — caught both directions). Status:
**source-level only (F-61)** — bench row `TRACKS/DEVICE-TESTS.md`
**U-124-⑫** added 2026-10-25 (unticked), alongside the app's U-124-5/U-124-6.
- `FW_VERSION` 1.3.17 → **1.3.18**.

## UNRELEASED — 1.3.17 (U-121 Phase E2, 2026-10-23): an app-driven Wi-Fi connect is HEAP-GATED too

**Owner report (2026-10-23):** *"the car resets again and again whenever i try
to connect the wifi from the app."*

- **Root cause (source-traced):** the two STEP MACHINES an explicit connect
  runs through — `advanceRejoinStep()` (the teardown walk: `mode(WIFI_AP)` →
  poll → `startOwnAP()` → `mode(WIFI_AP_STA)` → `WiFi.begin()`) and
  `giveUpStepMachine()` (the failed-join settlement) — carry the radio's
  heaviest calls (a full driver restart, a softAP bring-up, the first
  `WiFi.begin()` of the switch) and were the **last UNGATED radio path**:
  1.3.16 gated only the RETRY loop. With the BT stack engaged the free heap
  sits at the 13–20 kB field floor, so the first step of a switch allocated
  against the last kilobytes, `loop()` locked and the task watchdog rebooted
  the car — **once per attempt**, which is the "again and again".
- **Fix:** both machines consult `switchRadioMayRun()` before touching the
  radio. Above `STA_RETRY_HEAP_FLOOR_BYTES` (32000 B — the SAME floor the retry
  defers on, so the two paths agree): the step runs, and the recovery edge is
  logged once. Below: the step is **DEFERRED**, radio untouched, edge logged
  once per episode (`[RADIO] step deferred: low heap (free=… floor=…)` + a
  `CAR_EV_LOWHEAP` `switch-defer` event). Below the floor for longer than
  `SWITCH_HEAP_DEFER_MAX_MS` (4000 ms): **ABANDON** via
  `abandonSwitchOnLowHeap()` — BOTH machines cancelled with **not one radio
  call**, the incumbent link left exactly as it was (`startedAsAP` re-derived
  from `WiFi.status()`, never hard-coded), truth pushed on serial + STATE.
  The telemetry defer's escape hatch (let one attempt through anyway) is
  deliberately NOT used here: a status broadcast is worth the risk, a driver
  restart in a starved heap **is** the reset.
- **Also in this flash:** `check-webpage-js.mjs`'s two transport-default checks
  were RED on HEAD — U-121 Phase C routed `loadStoredWifi()`'s read defaults
  through `Config.h`'s `DEFAULT_WIFI_ENABLED` / `DEFAULT_BT_ENABLED`, and the
  rule pinned the literal `true`. The rule now RESOLVES the token (literal
  `true`, or `constexpr bool <token> = true;` in `Config.h`), so a `false` read
  default **or** a flipped constant still go red — the original claim, checked
  in both places it can live. Pre-existing red, not introduced by 1.3.17.
- **Gate:** new contract rule `switch-heap` (both machines gated · real floor ·
  one-shot defer log · time box ≤ 10000 ms · the box ABANDONS · the abandon
  cancels both machines and does no radio work); mutants **M137–M143** prove
  every clause. `sta-retry-heap` gained the check M136 always described but
  never triggered (see the correction under 1.3.16).
- `FW_VERSION` 1.3.16 → **1.3.17**.

**Status: source-level only (F-61).** Device rows: `TRACKS/DEVICE-TESTS.md`
**U-121-E2** (unticked) — BT on → connect the Wi-Fi FROM THE APP → serial shows
the defer (or a clean switch when the heap allows), `crash_count` does NOT
increment, the car does not reset. Gates at this commit: syntax **9/9** ·
contract **0 failures** (`switch-heap` new) · webpage-js **0 failures** ·
mutate **137/137** (M137–M143 new).

> **Correction to the 1.3.17 → 1.3.16 entry below:** its gate line ("webpage-js
> · mutate 130/130") was NOT true when written — `check-webpage-js.mjs` was
> already red on the transport-default checks and **M136 was UNDETECTED** (the
> `sta-retry-heap` rule asserted the budget-preserving clause only from the
> above-floor side, which a mutant moving `staRetryCount_++` INTO the defer
> branch satisfies). Both are fixed in 1.3.17; the harness baseline is green
> again at 137/137.

## UNRELEASED — 1.3.16 (U-121 Phase E, 2026-10-23): the BT-on + Wi-Fi-drop retry churn is HEAP-GATED

**Owner report (2026-10-23):** *"the heap drops from 109 KB to 15 KB if the
Bluetooth is turned on and the WiFi drop after that and the car freezes and the
app doesn't know about that."* Two halves; this flash is the FIRMWARE half.
The app half (the silence detector that finally KNOWS the car stopped answering)
is app U-121 Phase D, JS-only OTA.

- **Root cause (source-traced):** with the BT stack engaged the free heap sits
  at the 13–20 kB field floor (every guard since 1.3.11 assumes exactly this).
  A mid-session STA drop (`!startedAsAP && status != WL_CONNECTED`) arms the
  capped retry loop — and THAT loop had **no heap gate at all**: every
  `WIFI_RETRY_INTERVAL_MS` it ran `WiFi.disconnect()` + `WiFi.begin()`, a fresh
  association transient needing far more than the floor, until the allocation
  never happens and loop() locks up. Telemetry, scans, link admission and the
  page server all refused below a floor; the one path that CHURNED the radio
  repeatedly did not.
- **Fix:** new `STA_RETRY_HEAP_FLOOR_BYTES` (32000 B — same cushion as
  `LINK_ADMISSION_FLOOR_BYTES`: a join is a standing-cost operation). Below the
  floor a retry attempt is **DEFERRED, not consumed**: neither the capped budget
  (`staRetryCount_`/`staRetryCap_`) nor the backoff stamp (`lastWifiRetryMs_`)
  moves, so the retry resumes on the first pass the heap is back over the floor
  (e.g. once the BT link goes away) and the give-up settlement still follows a
  genuinely spent budget. The incumbent link is untouched — a live car beats a
  rejoin. The defer logs ONCE per arm (`staRetryDeferLogged_`, cleared when a
  retry runs and on every re-arm): `[REJOIN] retry deferred: low heap (free=…
  floor=…)`.
- **Gate:** new contract rule `sta-retry-heap` pins the floor (present, ≥ link
  admission), the gate itself, the one-shot log and the budget-preserving
  defer; mutants **M133–M136** prove every clause red→green.
- `FW_VERSION` 1.3.15 → **1.3.16**.

**Status: source-level only (F-61).** Device row: `TRACKS/DEVICE-TESTS.md`
**U-121-E** (unticked) — reproduce the owner's sequence (BT on → router drop),
expect the defer line on serial, the BT link keeps driving, and the retry
resumes after BT goes away. Gates at this commit: syntax · contract (`sta-retry-heap`
new) · webpage-js · mutate **130/130** (M133–M136 new).

## UNRELEASED — 1.3.15 (Q1–Q3 close-out, 2026-10-22): the firmware boot-hop is STOPPED

**Owner decisions confirmed 2026-10-22 — implement the Q1–Q3 defaults, no
confirmation needed.** Q1's UI half had already landed (P3, 2026-10-15: the Remote
panel's "Boot auto-join (car)" switch removed); this flash carries the FIRMWARE
half of the default.

- **Q1 — the car no longer hops to a router at power-up.** `botcfg/boot_autojoin`
  now defaults **OFF** (`loadStoredWifi()`: `getBool("boot_autojoin", false)`) and
  `FACTORY;RESET`'s RAM default follows. Boot stays on the own hotspot; the only
  auto-join left is the APP's smart-link (`ROUTERS;USE` on car selection). The
  verb is honest, not dead: `WIFICFG;AUTOJOIN;ON|OFF` still works (console
  opt-in), STATE `;AUTOJOIN=` and `/status boot_autojoin` still report the truth,
  and `armBootAutoJoin()` keeps its U-115 probe-first shape for an opted-in car.
  `wipeRoutersOnFreshFirmware()` already removes the key on every version change,
  so THIS flash boots AP-only regardless of any previously stored value.
- **Gate:** `check-webpage-js.mjs` flips with it — `getBool("boot_autojoin",
  false)` is the pinned form now and the FAIL text names Q1, so a default back to
  ON fails the gate. `boot-scan-first` and `settings-round-trip` stay green
  untouched (function shape and the verb's round-trip are unchanged).
- **Q2/Q3 are app-side pins, no firmware byte** — recorded here because the
  decisions close with this flash: Q2 pins the merged row's telemetry-first drop
  (`MERGED_ROW_TELEMETRY_MIN_PX`; app `remoteChrome.test.ts` — exactly ONE width
  gate, wrapping TelemetryLine); Q3 pins the both-links fan-out with
  `chosenRadio: null` and proves no relative token (MODECYCLE) can ride it, so a
  duplicate copy cannot double-apply (app, new `bothLinksDrive.test.ts`,
  mutation-proven both directions).
- `FW_VERSION` 1.3.14 → **1.3.15**.

**Status: source-level only (F-61).** Device rows: `TRACKS/DEVICE-TESTS.md`
**Q1-1..2** (unticked). Gates at this commit: syntax 9/9 · contract 0 failures
(rule `boot-hop-off` new) · webpage-js PASS · mutate-gates **123/123**
(M127-M129 new, one per fact the rule pins).

### Same flash — U-120 B (2026-10-22): the page toggle's command finally REACHES its handler

Owner report (this round): *"the webpage toggle is not working, when i turn off
it automatically toggles to on."* Root cause, source-proven and never
bench-tested (F-61): **the app sends `WIFICFG;WIFI;PAGE;ON|OFF` (set) and
`WIFICFG;WIFI;PAGE;?` (query) — and the dispatcher checked `;WIFI` (prefix,
length 5) BEFORE `;PAGE`, so every page command landed in the Wi-Fi-transport
branch with `arg = ";PAGE;…"`, matched none of `""`/`;?`/`;ON`/`;OFF`, and
answered `WIFICFG;ERROR;Syntax`.** The PAGE handler added by U-116 was
reachable only via the bare `WIFICFG;PAGE;…` form — which NOTHING in the fleet
sends (app, records and tests all use the nested form). So the toggle had never
worked on ANY firmware version: the app painted its optimistic OFF, then the
car's `;PAGE=ON` STATE truth snapped it back — exactly the report. Rounds of
gates (`page-state` mutants M123-M126 included) pinned the branch's EXISTENCE,
the STATE key, the NVS load and the serve gate — never the REACHABILITY of the
wire form.

**Fix.**
- The `;WIFI` branch condition now excludes the nested form
  (`strncasecmp(rest, ";WIFI;PAGE", 10) != 0`) so `;WIFI;PAGE;…` falls through
  to the PAGE branch.
- The PAGE branch accepts BOTH forms and selects `pageArg` at the matching
  offset (`rest + 10` nested / `rest + 5` short). Reply shape unchanged
  (`WIFICFG;WIFI;PAGE;%s`), NVS `page_off` write unchanged, app parser
  unchanged.
- Contract rule **`page-state` extended** with the three route claims (guard ·
  form · offset) + mutants **M130-M132**. App twin pin: `pageToggle.test.ts`
  now requires the PAGE reply branch to PRECEDE the generic `WIFICFG;WIFI;`
  branch (the mirror trap on the app side), mutation-proven.

**Gates:** contract 0 failures · webpage-js PASS · mutate-gates **126/126** ·
syntax 9/9. Device row **U-120-B** (unticked, F-61).

## UNRELEASED — 1.3.14 (U-117/U-118, 2026-10-20): the page-server state rides the STATE line — a Bluetooth-only app can finally READ the webpage toggle

**UNFLASHED (F-61).** Owner report: *"the webpage toggle off button is not
working."* The car already HAD the toggle (`WIFICFG;WIFI;PAGE`), the
persistence (NVS `page_off`, surviving a power cycle since U-116) and the
serve gate (`handleRoot` reads `pageOff_`) — what it did NOT have was a way
for an app on **Bluetooth** to learn the truth: `/status` (`page_on`) needs
the web transport, and the STATE line — the surface a BT-only app reads
every second — carried nothing. So the app painted an **assumed** state after
every (re)connect: a switch that reads wrong reads broken (half of the report
was app-side and is fixed there: `pageToggle.test.ts`, genumsolutions-app
`14bf5c0` — parse, adopt, query-on-link-up, honest `pageOn` naming).

**Fix.**
- STATE line gains `;PAGE=ON|OFF`, fed by `WebServerComm::pageServerIsOn()` —
  the same `pageOff_` the serve path and `/status` read, so every surface
  tells one story.
- `pageServerIsOn()` **declaration moved to the public section** of
  `WebServerComm.h` (body unchanged): a private accessor cannot answer a wire
  query.
- `FW_VERSION` 1.3.13 → **1.3.14**.

**Gates (source only — F-61):** new contract rule **`page-state`** (pins the
STATE key + its `pageServerIsOn()` feed, `page_on` in `/status`, the NVS
`page_off` boot load and the serve gate `pageOff_ || pageServerMustBeHeld()`)
+ mutants **M123-M126**; `check-firmware-syntax.ps1` **9/9** TUs ·
`check-firmware-contract.mjs` **0 failures** · `check-webpage-js.mjs` PASS.
Same-commit app consumer (wire field with no consumer is noise): app
`pageToggle.test.ts` parses `;PAGE=` / `page_on` with F-88-shape honesty
(absent ⇒ "don't know", never OFF).

## 1.3.13 (P4 / item F, 2026-10-17): the car REFUSES a second link it cannot afford — multi-connect stops starving the run-time

**UNFLASHED (F-61).** Owner flash target. Addresses the 2026-10-17 owner report
(item 6): **connecting BOTH links at once can freeze / reset the car**, the field
record showing `free_heap` 13-18 kB at the multi-connect resets.

**Root cause.** Telemetry and scans already REFUSE below a heap floor
(`STATUS_HEAP_FLOOR_BYTES`, `SCAN_HEAP_FLOOR_BYTES`) — a LINK never did. The
second transport was raised unconditionally into a starved heap, and the
run-time starved with it. Nothing in the car could say "not now".

**Fix.**
- `WebServerComm::admitSecondLink()` — the missing refusal. It gates **only the
  SECOND** link: the FIRST is never gated (a car with no way in is worse than a
  car with one). Refusing leaves the **incumbent link untouched** and **latches**
  the verdict so it can be reported; a live heap reading would make "refused"
  indistinguishable from "still coming up".
- `LINK_ADMISSION_FLOOR_BYTES` (32000) — above the 20000 status floor (a standing
  link costs more than the status path), below the 65536 scan floor (a scan is a
  transient, a link is a standing cost). **Bench-tuned pending P0** — the point of
  the rule is that the refusal EXISTS and is honest, not that 32000 is final.
- The `.ino` transport-power loop snapshots each link's state **before** the
  optimistic `webUp = true` mark, so "is this raise the SECOND link?" is answered
  against reality. A refused Wi-Fi raise **reverts** the optimistic mark (or the
  loop would believe Wi-Fi is up and never retry it once the heap recovers).
- The verdict is reported on **both** surfaces: `;LINKCAP=0|1` on the STATE line a
  Bluetooth-only app reads, `"link_cap":0|1` in the `/status` JSON the web
  transport reads. `linkCapCode()` folds the transport toggles — with one
  transport OFF there is no second link to deny, so the honest answer is **1**.
  The latch is cleared the moment both links are genuinely up.
- `FW_VERSION` 1.3.12 → **1.3.13**.

**Gates (source only — F-61):** new contract rule `link-admission` + mutants
**M110-M116**; `check-firmware-syntax.ps1` **9/9** TUs ·
`check-firmware-contract.mjs` **0 failures** · `mutate-gates.mjs`
**110/110 caught by their intended rule**.

**Failsafe.** F-87 — an unreservable resource must be REFUSED, not attempted
optimistically. **App half** is P5 (F-88: an ABSENT `link_cap` means CAPABLE,
never denied — the pre-1.3.13 reading).

### Same flash — P6 / item D (2026-10-17): the car hands over a BOUNDED event log

The owner's item 4: *see and act on the bug/reset/freeze history, tied to the
car's unique profile*. P6 is the car's half; the app's viewer is P7.

**What it is.** A HARD-BOUNDED ring (`CAR_EVENT_CAP` 12) of the facts a
reset/freeze report is built from: every **boot**, every **fault reset** (with
the phase/RTC evidence the restart record already assembles), each
**below-floor status-broadcast crossing** (low heap), each latched **link
refusal**, and each **link up/down transition**. One fixed 384-byte NVS blob
per append — the count of writes is predictable and there is never an
unbounded set of tiny keys. Oldest dropped at the cap; `FACTORY;RESET` dies
with the namespace wipe, so a fresh start never resurrects history.

**Wire (system hook, every mode, every transport; replies ride STATE
`REPLY=…`, bounded ≤191 B):**
- `LOG` / `LOG?` → `LOG;COUNT=<n>;CAP=12`
- `LOG;GET;<n>` → newest-first window of three, `LOG;GET;<n>;<total>=K,boot,ms,
  heap,reason,phase;…`; an empty ring answers a parseable zero (F-61: the car's
  answer, not a guess)
- `LOG;CLEAR` → `LOG;CLEARED`
- `/status` gains `event_log_count` (the app's cue that there is a log to fetch)

**Gates (source only — F-61):** new contract rule `event-log` + mutants
**M117-M122**; syntax **9/9** · firmware-contract **0 failures** ·
mutate-gates **116/116** (M108/M114 anchors retargeted after the JSON append).

**Failsafe.** F-89 — an on-device log must be bounded BEFORE it is useful; a
ring without a hard cap is a flash-exhaustion bug, so the cap is checked first.
No secrets in entries; phases are firmware literals only (never network input).

## UNRELEASED — 1.3.12 (U-115, 2026-10-16): the boot auto-join probes the air first — an absent router no longer holds the own hotspot on a moving channel

**UNFLASHED (F-61).** Owner flash target. Fixes the 2026-10-16 owner field
report on the 1.3.11 flash: the car's own hotspot was **detected but would not
connect**, and it "only connects once bluetooth is off".

**Root cause.** The boot auto-join handed the FULL retry budget to a router that
may simply be switched off. While that budget ran, `startOwnAP()`'s `staSharing`
branch set the AP channel to FOLLOW the STA (`apChannel 0`) and every
`WiFi.begin()` retry made the STA scan — so the own hotspot broadcast on a
**MOVING channel**. A phone SAW `4WD4MCar_WiFi` and could not complete the join.
The "only connects once bluetooth is off" detail is the retry-budget window
elapsing, not a Bluetooth effect.

**Fix.**
- `armBootAutoJoin()` now runs ONE non-blocking reachability scan before arming
  the rejoin: **router present** → rejoin (unchanged), **absent** → stay AP-only
  (own hotspot stable from the first second), **inconclusive** → fall back to the
  rejoin so the car can never lose its router to a probe it could not run.
- `startBootReachabilityScan()` / `serviceBootReachabilityScan()` drive the probe
  in the same arm/kick/evaluate shape the user scan already uses; they drive the
  driver directly (no collect vector, no SCAN broadcast) and `scanDelete()`
  immediately, so the tight boot heap is returned.
- `BOOT_SCAN_HEAP_FLOOR_BYTES` (20000) fits the measured cold-boot heap
  (~23.2 kB after the BT stack); a refused probe degrades to the plain rejoin.
- An explicit switch (`rejoinNetwork`) and a Wi-Fi OFF (`stop`) clear a stale probe.
- `FW_VERSION` 1.3.11 → **1.3.12**.

**Gates (source only — F-61):** new contract rule `boot-scan-first` + mutants
**M96-M100**; `check-firmware-syntax.ps1` **9/9** TUs · `check-webpage-js.mjs`
**PASS** · `check-firmware-contract.mjs` **0 failures** · `mutate-gates.mjs`
**94/94 caught by their intended rule**.

**Device rows:** U-115-1..3 in `TRACKS/DEVICE-TESTS.md` — owner's bench.

## P7 / issue 8 (2026-10-16): the settings round-trip — NO firmware byte changed, the car's half is now gated

**`FW_VERSION` stays 1.3.12** — nothing in the firmware moved. Issue 8's bar:
*"every one of these settings must store in the car AND in the app promptly and
stay in sync."* The car was already keeping its half of that promise: each
`WIFICFG;WIFI|BT|AUTOJOIN;ON|OFF` persists to NVS (`t_wifi` / `t_bt` /
`boot_autojoin`) **before it is acked**, the reply always carries the state the car
actually holds (a flip that would turn the LAST transport off is refused and
re-answered with the live state), and the three settings are readable back on
**both** surfaces a client can be attached to — ModeManager's STATE line
(`;WIFI=` / `;BT=` / `;AUTOJOIN=`, so a Bluetooth-only app can re-sync) and the
`/status` JSON (`wifi_enabled` / `bt_enabled` / `boot_autojoin`).
`bootAutojoin_` is restored from `botcfg/boot_autojoin` at boot.

The sync gaps were **all app-side**, and they had one shape: the app ADOPTED car
truth into live state but never wrote it into the per-car profile, so a restart
resurrected whatever disk last held (an adopted mode / speed / router registry);
two controls (the D-pad/Joystick pill, the auto-join-router switch) were raw state
setters that never persisted at all; and the `WIFICFG` **reply** path bypassed the
optimistic-commit grace, so a reply in flight during a user flip could roll the
panel back AND persist the pre-flip value. Fixed app-side in `genumsolutions-app`
(JS-only OTA, `3.2.7/60` unchanged) with a new `persistenceSync.test.ts`;
**8 mutations → red**, byte-exact restore.

**Gates (source only — F-61):** new contract rule **`settings-round-trip`** +
mutants **M104-M109** (the three NVS writes removed, the STATE keys removed, the
`/status` JSON fields removed, the last-transport-off clamp removed);
`check-firmware-syntax.ps1` **9/9** · `check-webpage-js.mjs` **PASS** ·
`check-firmware-contract.mjs` **0 failures** · `mutate-gates.mjs` **103/103**.

**Device row:** U-113-15 in `TRACKS/DEVICE-TESTS.md` — owner's bench.

## P8 / issue 13 (2026-10-16): close-out — no firmware byte changed, the round is source-complete

**`FW_VERSION` stays 1.3.12** — nothing in the firmware moved. P8 is the round's own
close-out: every phase's gates are green on a clean tree (firmware-contract **0
failures** · webpage-js **PASS** · syntax **9/9** · mutate-gates **103/103**), the app's
P6+P7 JS-only OTA **was published** at that point (`3.2.7/60`, manifest notes `e02db5c2b4c…`, 2026-10-16 19:06 UTC); the **current live build** is P8 (`OTA · Short update (2476b2ef4df17f9376cc1cd3f5f34aa6fd0aaba6)`, same `3.2.7/60`, published 2026-10-17). Both CIs are
green on `main`, and the `backup` branch was re-snapshotted to `main`. Records refreshed:
`Readme.md` (version snapshot + changelog 1.3.12), `TRACKS/RUNSHEET.md` (retargeted to
the 1.3.12 bench), `TRACKS/DEVICE-TESTS.md` (the P8 close-out accounting).

**Not device-verified (F-61).** All 14 issues are code-complete; the rows
(U-115-1..3 + U-113-8..15 here, U-113-1..7 + U-113-16 in the app), the E3/E4/E5/E11
captures and **Q1–Q3** remain the owner's bench.

## P6 / issue 1 (2026-10-16): dual BT+Wi-Fi link — NO firmware byte changed, the premise is now gated

**`FW_VERSION` stays 1.3.12** — nothing in the firmware moved. Issue 1 asked for the
app and the car connected over Bluetooth AND Wi-Fi at the same time, and the car was
already doing its half: the U-79 "both transports on" branch only *starts* a
transport that is down and never stops one, and U-115 removed the boot
moving-channel that made the own hotspot unjoinable in the first place.

What actually stopped the dual link was **one line in the app**:

```
{target.id === "bt-spp" && !linkLive ? ( … Find my car … )}
```

`linkLive` was the SHARED "any link up" flag (`btLive || wifiConnected`), so the
moment Wi-Fi connected the Bluetooth scan button vanished — and because
`sppDevices` is filled only by a scan, the paired list below it stayed empty too.
The dual link was **one-sided**: Wi-Fi-second worked, Bluetooth-second could not.
Fixed app-side (`genumsolutions-app`); the Wi-Fi direction, which was already open,
is now pinned so it cannot be "balanced" into a symmetric block later.

**Command fan-out confirmed (P6 item 2):** `routeCommand` lets mode parity pick the
radio, `EVERY_LINK_COMMANDS` broadcasts over whatever is live, and `chosenRadio`
stays `null` (the transports/LinkManager is deliberately not activated) — so a
non-broadcast drive line reaches exactly one radio and there is **no double-apply**
on the car. **Q3 stays open** with default = broadcast lines on both.

**Gates (source only — F-61):** new contract rule **`dual-link-kept`** + mutants
**M101-M103** (an injected `stop()`, the BT raise removed, the web raise removed);
`check-firmware-syntax.ps1` **9/9** · `check-webpage-js.mjs` **PASS** ·
`check-firmware-contract.mjs` **0 failures** · `mutate-gates.mjs` **97/97** ·
app vitest **711/711**, `tsc --noEmit` clean, prettier clean.

**Device rows:** U-113-12..14 in `TRACKS/DEVICE-TESTS.md` — owner's bench.

## UNRELEASED — 1.3.11 (U-112 + U-114 + T66 + P4/E1 + P5, 2026-10-15): heap floor for status/scan + the page stall-abort + the tele surface + the E1 heap waterfall + Wi-Fi OFF that actually hides everything (the post-1.3.9 reset/freeze report)

**FLASHED to the car** (COM8, 2026-10-15, hash-verified upload — see the bench
log in `TRACKS/DEVICE-TESTS.md`). The owner flashed 1.3.9 and reported:
the car **resets while connecting Bluetooth + the own hotspot together**, and
**freezes when the car's own page is opened while the app holds a Wi-Fi link**.
The field record (`device_crash_reports`, boots 1026-1029, fw 1.3.9) matches
both reads: **free_heap 13-18 kB at report time** with PANIC resets and an empty
phase — the car lives at the allocation floor, where every big transient is a
coin flip.

### Added — three guards, one mutation-proven rule each (`heap-guard`)

1. **The status broadcast respects a heap floor.** Below
   `STATUS_HEAP_FLOOR_BYTES` (49152 B) `broadcastStatus()` DEFERS — and the
   defer is TIME-BOXED (`STATUS_HEAP_DEFER_MAX_MS`, 4000 ms), after which one
   attempt goes through regardless, so a chronically low heap degrades
   telemetry to one attempt / 4 s instead of black-holing it forever. The
   policy lives INSIDE `broadcastStatus()`, so the 1 s tick and the
   `REQ_STATE`/`REQ_NETW` paths share ONE rule; each defer edge logs once
   (`[HEAP] status broadcast deferred/resumed`) so the serial capture shows it.
2. **The coexistence scan respects a higher floor.** `startScan()` refuses
   below `SCAN_HEAP_FLOOR_BYTES` (65536 B — above the status floor: the bigger
   transient gets the bigger cushion) with the honest `ROUTERS;SCAN;BUSY` the
   handler already answers, instead of kicking a scan whose allocation lands
   8 s later on a silently EMPTY router list. Refusals are counted and logged.
3. **The root page no longer lets an undrained reader own the loop.**
   `HTTP_STALL_ABORT_AFTER` (2) CONSECUTIVE zero-byte writes now end the page
   transfer for that pass: a non-draining reader used to cost the full 3500 ms
   budget on EVERY pass, during which no `ws.loop()`/BT/pins run — which is
   what *"freezes while the page is open with the app connected"* IS. The
   counter resets on progress, so slow-but-alive readers keep the full budget.

**Gate + mutants.** New `heap-guard` rule in `check-firmware-contract.mjs`
(5 assertions: defines parse · scan floor > status floor · defer <= 10 s ·
floor+time-box in `broadcastStatus()` · scan refusal · stall abort + reset).
Mutants **M70-M75**, one per assertion. Writing them also exposed a
**weakened old rule**: the U-112 floor's extra `return false` bumped
`scan-honest`'s return-COUNT back to 2 after M38 removed the in-flight
refusal — the count rule stayed green over a removed refusal (found by M38
going UNDETECTED, the F-83 shape again). `scan-honest` now pins the in-flight
refusal **by text** (`if (scanPending_ || scanBusy_) return false`), and M13's
anchor was re-sewn after the stall-abort moved the lines it matched.

**VERIFY — source only.** `check-firmware-syntax.ps1` **9/9** TUs ·
`check-webpage-js.mjs` PASS · `check-firmware-contract.mjs` 0 failures
(46 rules incl. 5x heap-guard + tele-params + heap-waterfall) ·
`TSH/test_config_integrity.py` PASS ·
`mutate-gates.mjs` **82/82 caught by their intended rule**.

### Also in 1.3.11 — T66: the `tele.parameters` config surface on `/status`

`Config.h` carries ONE `CONFIG_TELEMETRY_PARAMETERS` line
(`tele.parameters,frequency,1600,vysexcvr,0`) consumed in **two directions**:
`TSH/test_suites/test_config_integrity.py` parses it as TEXT (both keys present;
the shaping key appears exactly once repo-wide), and
`WebServerComm::buildStatusJson()` assigns the same line to a `const char*` and
reports it as `"tele_params":"freq=1600, vyse=0"`. Additive — nothing else in the
repo reads it yet.

### Fixed — U-114: the body did not compile at HEAD, and its `/status` lied

Commit `79e6d26` ("add CONFIG.telemetry.parameters surface … and wire it into
status JSON") landed a surface that broke the build and mis-wired the JSON. Three
defects, and only the first one was loud:

1. **No build.** The macro was a BARE token list, so
   `const char* teleParams = CONFIG_TELEMETRY_PARAMETERS;` expanded to
   `const char* teleParams = tele.parameters,frequency,1600,vysexcvr,0;` — `tele`
   is undeclared. `check-firmware-syntax.ps1` failed **1 / 9** TUs (`Config.h:40:
   'tele' was not declared in this scope`). The contract and page gates stayed
   green the whole time because neither compiles anything — the F-61 shape.
2. **`/status` reported the wrong values.** The format string puts the
   `tele_params` `%s` slots BETWEEN `scan` and `wifi_enabled`, but `freqBuf` /
   `vyseBuf` were appended AFTER `boot_autojoin_`. Every field from `tele_params`
   onward was fed by its neighbour's argument, so `wifi_enabled`, `bt_enabled` and
   `boot_autojoin` reported garbage — and **the app ADOPTS those on connect**
   (U-111): a silent settings-corruption bug, not a cosmetic one. GCC cannot catch
   it: `%s` against `char[]` / `const char*` is pointer-to-pointer, so the types
   match and only the ORDER is wrong (exactly what the new rule pins).
3. **Undefined behaviour + an empty last field.** `strchr(p, ',') + 1` added 1 to
   a NULL result the moment a field was missing, and the LAST field was read with
   `strchr(value, ',')` — NULL for a surface with no trailing comma — so the
   shaping value came out `""` instead of `0`.

Fixed by making the surface ONE string literal and reading it through a new
bounded, NULL-safe `teleSurfaceField(surface, idx, out, dst)` that indexes fields
by NUMBER (so re-ordering the surface cannot slide a value into the wrong slot)
and treats the last field as the remainder of the string.

**Gate + mutants.** New `tele-params` rule in `check-firmware-contract.mjs`
(5 assertions: the surface is one string literal · the `tele_params` slot exists ·
`freqBuf`/`vyseBuf` sit in that slot's argument position · the surface is read
through the helper with both field indices · no `strchr(...) + 1` chain is back).
Mutants **M76-M80**, one per assertion. This fix first landed under the label
**1.3.10** (the surface is a behaviour change to an already-labelled body) on the
reasoning that 1.3.10 had never been flashed. The label was then **bumped to
1.3.11** — see *Version* below — so a 1.3.10 binary that may already sit on the
car can never report a version this body would make ambiguous.

### Added — P4/E1: the six-stage `[HEAPW]` heap waterfall (measurement, not the fix)

Issue 6 — *"Bluetooth ON collapses free heap 110 KB → ~10 KB; BT+Wi-Fi together
sometimes < 14 KB → the car resets"* — is the round's **main** issue, and P4's
first instruction is **measure first**: the E1 waterfall is *boot → BT controller
up → SPP connected → own AP up → page served → both links*. Nothing emitted those
stages, so the owner's serial capture could not have produced the waterfall the
diet is supposed to be read from — every stage would have come back a flat line.

`Config.h` gains `heapWaterfall(stage)`: one `Serial.printf` carrying **free heap
AND `ESP.getMaxAllocHeap()`**, plus the six stage tokens as the wire contract.
The call sites sit where each stage really becomes true, each **edge-gated** so
the capture reads six lines and not one per loop pass:

| Stage | Where | Edge that fires it |
|---|---|---|
| `boot` | `setup()`, right after `crashLogBegin()` | once, before `[BOOT]` prints |
| `bt-stack` | `BluetoothComm::begin()` | idempotent begin → first bring-up |
| `spp-connected` | `maybeCheckSupplyAndBTWatchdog()` | `btEverConnected` false→true |
| `wifi-ap` | `startOwnAP()` after `apRunning_ = true` | the `if (apRunning_) return` guard |
| `page-served` | `handleRoot()` | first serve |
| `both-links` | `loop()`, after the U-79 enforcement | `btUp && webUp` false→true |

`max` is printed beside `free` **on purpose**: the T-7b BT-restart watchdog can
fragment the heap — the largest contiguous block shrinks — without moving the
TOTAL, and free heap alone cannot see that (P4 prime suspect (c)).

**No behaviour change at this step.** No U-112 floor was re-tuned and no
allocator was touched. (The diet's first step landed in the same version — see
*Fixed — P4/issue 6* below.)

**Gate + mutants.** New `heap-waterfall` rule in `check-firmware-contract.mjs`
(the wire format · `max` present · one call per stage, in the file that owns that
stage). Mutants **M81-M88**: one per stage, plus one that drops `max=` from the
format and one that prints `max` from the free value.

**VERIFY — source only.** syntax **9/9** TUs · webpage-js PASS · contract **0
failures** (incl. 5x heap-guard + tele-params + heap-waterfall) · TSH PASS (flag
key still x1) · `mutate-gates.mjs` **82/82 caught by their intended rule**.

### Fixed — P4/issue 6: the BT heap diet (classic-BT-only bring-up)

The `[HEAPW]` breadcrumb above exists to size this fix; the fix itself is now in
the tree, because the largest cause turned out to be plain in the source and did
not need the capture to name: `BluetoothSerial::begin()`'s third argument is
`disableBLE`, and it **defaults to false** (`BluetoothSerial.h:43`). The bare
`begin(BT_SPP_NAME)` this used to call therefore ran the controller in **BTDM**
mode — BLE *and* classic — allocating BLE controller + host memory that this car
never touches (it speaks Classic SPP only, and the app's BLE path is an explicit
placeholder).

`BluetoothComm::begin()` now calls `SerialBT.begin(BT_SPP_NAME, false, true)`.
`disableBLE=true` asks for `ESP_BT_MODE_CLASSIC_BT`, and core 3.3.12 releases the
BLE half before the controller starts: `btStartMode()` calls
`btMemRelease(BT_MODE_BLE)` (`esp32-hal-bt.c`), so this is the library's **own
supported path** — not a hand-rolled `esp_bt_controller_mem_release()` call, which
would have to be sequenced against the controller init itself.

This is the diet's **first step, not the whole diet**: the `[HEAPW]` waterfall
still reports whatever BT cost remains, so the U-112 floor re-tune stays
measurement-driven. **UNFLASHED (F-61)** — all of the above is source-level.

**Gate + mutants.** New `bt-heap` rule in `check-firmware-contract.mjs` (the THIRD
argument of `SerialBT.begin()` must be `true`). Mutants **M89/M90**: the argument
dropped (back to the library default) and the argument flipped to `false`.

**VERIFY — source only.** syntax **9/9** TUs · webpage-js PASS · contract **0
failures** (incl. 5x heap-guard + tele-params + heap-waterfall + bt-heap) · TSH
PASS (flag key still x1) · `mutate-gates.mjs` **84/84 caught by their intended
rule**.

### Fixed — P5/U-113 #7: Wi-Fi OFF now hides everything (the beacon was resurrecting itself)

Owner report: *"Wi-Fi OFF … the phone still sees the hotspot AND its webpage."*
The plan listed three candidate causes and said **E3** would pick one. Reading the
source picked it — the defect is unambiguous and needed no capture to name:

> **No own-hotspot bring-up path ever consulted the Wi-Fi transport switch.**

Two defects stacked:

1. `stop()` took the beacon down only `if (apRunning_)` (`WebServerComm.cpp`) —
   and `apRunning_` is a *believed* flag that the code itself **force-writes
   false** across an `AP_STA`↔`AP` mode bounce (`GIVEUP_AP`, `REJOIN_STEP_AP`:
   *"forced false, not trusted"*), precisely because it goes stale there. Gating
   the shutdown on a flag the rest of the file distrusts is how a switch that
   claims "hidden" leaves the radio broadcasting.
2. `handle()`'s FIN-40/D1 retry (`if (!apRunning_ && … >= AP_RETRY_INTERVAL_MS)`)
   then **re-created the hotspot ~2 s** after `WIFICFG;WIFI;OFF`, because it
   tested `!apRunning_` and nothing else — and `stop()` had just cleared that
   flag. `stop()` only runs while `webUp` is true, and the U-79 loop clears
   `webUp` as it calls it, so the resurrected beacon was **never taken down
   again**. `updateApPowerPolicy()` correctly no-ops when `!wifiEnabled_` and so
   never parks it either.

**The fix** (three one-line guards):

- `stop()`: `WiFi.softAPdisconnect(true)` is now **UNCONDITIONAL** (it is
  idempotent when nothing is up, and this path wants the radio down either way).
- `startOwnAP()`: **refuses while `!wifiEnabled_`** — the invariant, stated at the
  one function that actually raises the beacon. It covers every caller (boot
  bring-up, the rejoin and give-up machines, the retry), which the retry guard
  alone would not.
- `handle()`: the AP retry is **gated on `wifiEnabled_`**.

**Issue 9 (#9, "page always up when Wi-Fi is on") needs no new fix** — U-105
already removed the power-save *park*, and `handleRoot()`/`serveRootBounded()`
serve the page with no client gate. What it needed was for the property to be
**regress-proof**, so `page-always-on` now asserts it (no `hasWsClients()` gate
on the route; `updateApPowerPolicy()` never parks the beacon). The device half of
issues 7 and 9 is **E3/E4** on the owner's bench (F-61).

**Gate + mutants.** Two new rules in `check-firmware-contract.mjs`:
`wifi-off-hides-ap` (unconditional teardown · `startOwnAP()` refuses · the retry
is gated · the HTTP server is stopped) and `page-always-on` (no client gate on the
route or the send path · the beacon is never parked · the policy still returns on
a disabled transport). Mutants **M91-M95**.

**VERIFY — source only.** syntax **9/9** TUs · webpage-js PASS · contract **0
failures** (incl. 5x heap-guard + tele-params + heap-waterfall + bt-heap +
wifi-off-hides-ap + page-always-on) · TSH PASS (flag key still x1) ·
`mutate-gates.mjs` **89/89 caught by their intended rule**.

### Version — 1.3.10 → 1.3.11 (2026-10-15, owner ruling)

This body carried its U-112/U-114/T66/P4-E1 work under the label **1.3.10** while
1.3.10 had never been flashed (the U-114 note above says so). The owner then ruled
the version **bumped to 1.3.11**: a 1.3.10 binary may already have been put on the
car (or bench-flashed) for the U-112 pass, and a body that adds the U-114
compile/wiring fix and the P4/E1 breadcrumb must never be confusable with a
pre-fix 1.3.10 report in `device_crash_reports`. **The bench, E1 and diet flash is
1.3.11.** The diet (classic-BT-only, above) was folded into 1.3.11 rather than
held back for a separate 1.3.12: nothing had been flashed yet, so there is no
pre-diet binary to confuse it with, and the waterfall the owner captures (step 6b)
is then the **post-diet** figure the floor re-tune actually needs. **P5 rides
1.3.11**; there is no 1.3.12.

## 1.3.9 (U-111b, 2026-10-14, ON THE OWNER'S CAR): the Internet method (cloud relay) removed + the firmware-contract gate completed

**UNFLASHED (F-61).** Supersedes 1.3.8 (never flashed). Owner directive: the
Internet / remote-link ("control from anywhere") method is dropped entirely and
will be rebuilt from scratch later.

### Removed — the whole relay transport

`RelayComm.cpp` / `RelayComm.h` deleted; the four seams unwired from the
sketch, `ModeManager` (`run()` drain, `sendState()` fan-out, `isConnectedNow()`,
`currentLink()`), `Oled_1_3.cpp`, `WebServerComm` and `Config.h` (`RELAY_*`, the
`RELAYCFG` system-command hook). The OLED link rotation is back to the two live
transports, `currentLink()` can no longer return `Internet|WSS`, and `grep -ri
relay` over the sources returns nothing. `FW_VERSION` 1.3.8 → 1.3.9. CI job
`relay-contract` → `firmware-contract`; `Readme.md` / `TRACKS/CATALOG.md` drop
the `RELAYCFG` protocol row.

> Flashing 1.3.9 is a firmware-version change, so
> `wipeRoutersOnFreshFirmware()` clears the saved routers + transport toggles on
> the first boot (both transports ON, auto-join ON).

### Gates — the rename was incomplete until the follow-up commit

`check-relay-contract.mjs` → `check-firmware-contract.mjs` kept only 14 of the
old gate's 40 rules and left the old file reading the deleted `RelayComm.cpp`
(exit 1). `mutate-gates.mjs` runs `gateContract`, so **35 of the 63 mutants
went UNDETECTED and the harness failed**. The 10 missing NON-relay rule families
(`ws-guard`, `ws-budget`, `extra-status-decoupled`, `switch-teardown`,
`wifi-persistent`, `retry-split`, `scan-honest`, `scan-quality`, `mode-cycle`,
`status-net-fields`) are ported back in, and the dead gate is deleted.

**VERIFY — source only.** `check-firmware-syntax.ps1` **9/9** TUs (RelayComm.cpp
is gone) · `check-webpage-js.mjs` PASS · `check-firmware-contract.mjs` 37/37 ·
`mutate-gates.mjs` **63/63**.

## UNRELEASED — 1.3.8 (U-111, 2026-10-14): Bluetooth pairing actually completes + no AP retry storm + the 11 dBm cap becomes a boot-window cap **(superseded by 1.3.9 — never flashed)**

**UNFLASHED (F-61).** Supersedes 1.3.7 (never flashed). Third pass on the owner's
2026-10-14 report — Bluetooth "appears but does not pair", "the car's own hotspot
won't connect", flaky Wi-Fi with the car side sometimes freezing — plus the app
half of the state round-trip the owner asked for (below).

> Flashing 1.3.8 counts as a firmware-version change, so
> `wipeRoutersOnFreshFirmware()` clears every saved router, the transport toggles
> (`t_wifi`/`t_bt`) and `boot_autojoin` on the first boot — a clean start (both
> transports ON, auto-join ON). Re-join a router afterwards if wanted.

### Fixed — Bluetooth pairing could NEVER succeed (the real "appears but doesn't pair")

`BluetoothSerial`'s defaults are `_enableSSP = false` and `_pin_code_len = 0`, and
this firmware called neither `enableSSP()` nor `setPin()`. Pairing therefore ran as
LEGACY pairing, where the car answers Android's PIN request with a **zero-length**
PIN (`BluetoothSerial.cpp`: `esp_bt_gap_pin_reply(bda, true, 0, NULL)`), so
authentication could never complete: the car was discoverable and connectable, the
phone listed it, and the bond failed every time. That is the whole symptom — the
car was never failing to advertise, it was advertising a pairing it was guaranteed
to lose.

`BluetoothComm::begin()` now configures pairing **before** `SerialBT.begin()`:
`SerialBT.enableSSP(false, false)` selects IO capability NONE = SSP **"Just Works"**
(no PIN, no on-car confirmation — the only sane choice for a car with no keypad),
and `SerialBT.setPin("1234", 4)` is the legacy fallback so a remote that starts
legacy pairing gets the documented PIN instead of an empty one. Both settings are
read once, inside `BluetoothSerial::_init_bt()`, which is why they must be set
before `begin()` (and again after any `end()`/restart). The library's bare
`enableSSP()` is deliberately NOT used: it advertises IO capability IO, which
REQUIRES the sketch to answer the numeric-comparison callback and otherwise
**refuses** the bond.

### Fixed — the own-hotspot bring-up is no longer retried on EVERY loop pass

`updateApPowerPolicy()` called `startOwnAP()` ungated whenever `!apRunning_ &&
!staUp`. That probe runs every pass, so a `softAP()` that kept failing turned into
a per-pass radio churn (`setSleep` / `setTxPower` / `softAP`) that hammers the
driver and starves `loop()` — the shape of the owner's "the car side freezes
totally" report. `handle()` already owns that retry with an `AP_RETRY_INTERVAL_MS`
backoff and a `staBusy()` guard, so the direct call is removed.

### Changed — the 11 dBm AP cap is now a BOOT-window cap only

`startOwnAP()` still brings the radio up at `WIFI_POWER_11dBm` (U-96: keeps the
boot current spike small), but that cap used to apply for the whole uptime — a
permanent ~8.5 dB handicap that, on top of Bluetooth coexistence time-slicing the
antenna, makes association marginal (owner: "the car's own hotspot won't
connect"). `handle()` now restores `WIFI_POWER_17dBm` **once**, after
`WIFI_BOOT_SETTLE_MS` (15 s).

### Changed — comment hygiene

`BluetoothComm::stop()`'s doc comment and its serial line still described the
U-105-removed "one transport connected powers the OTHER down" power-save policy;
both now say the truth (BT goes down only when the owner disables it).

### App half (JS-only, OTA — no native change)

- The car already broadcasts its three persisted transport settings on every STATE
  line (`;WIFI=` / `;BT=` / `;AUTOJOIN=`) and in every WS JSON frame, and
  `carProtocol` has parsed them since U-105 — but NOTHING applied them. A fresh
  phone (or a cleared profile) showed "don't know" until the user queried each
  toggle by hand. `useControlHub` now **adopts car truth** into the panel the
  moment a link comes up (change-guarded, with the same short optimistic-commit
  grace as the mode pick, so the car's echo of the pre-command frame cannot undo a
  just-flipped toggle; persisted only on a real change).
- The app's registry/help text still named the car's Bluetooth as `4WD CAR`; it now
  says `4WD4M Car`, matching the renamed firmware advertisement.

## UNRELEASED — 1.3.7 (U-110, 2026-10-14): OLED format the owner asked for + Bluetooth pairing bring-up + honest BT state

**UNFLASHED (F-61).** Supersedes 1.3.6 (never flashed). Second half of the
owner's 2026-10-13/14 field-fix round, from the post-flash report on the 1.3.x
build: OLED last-two-lines layout, Bluetooth "appears but does not pair",
flaky Wi-Fi with the car sometimes needing a manual restart, and a stale
reconnect prompt in the app.

### Changed — OLED last-two-rows format (owner 2026-10-14)

The transport frames are now the exact two-row layout the owner specified:

```
Bluetooth frame              Wi-Fi frame
+----------------------+     +----------------------+
| Bluetooth : <name>   |     | WiFi : <ssid>        |
| Type : SPP           |     | IP : <car IP>        |
+----------------------+     +----------------------+
```

- The Bluetooth⇄Wi-Fi alternation (both toggles ON), and the single-frame
  pinning (one toggle ON), are unchanged from U-107.
- **The Wi-Fi frame's name⇄IP alternation is REMOVED** — the frame now carries
  the network name on one row and the IP on the other, so both are always on
  screen. `wifiInfoPhase` / `WIFI_INFO_INTERVAL_MS` are gone with it.
- `<name>` is `BT_SPP_NAME` (see below); `<ssid>` is the joined router or the
  car's own `4WD4MCar_WiFi`; `<car IP>` is `WIFI_AP_IP` (`192.168.245.1`) on its
  own AP.
- `storyboard.md` rewritten to current truth (header `4WD4M Car`, heading
  `Mode : <display>`, the new frames; the retired ESP_SER/ESP_CLI rows dropped).

### Changed — advertised Bluetooth name `4WD CAR` → `4WD4M Car`

`Config.h` `BT_SPP_NAME` now matches the project identity, so the phone's
pairing list, the OLED frame and the app all name the car the same thing (the
owner could not match the "4WD CAR" the radio advertised to what the app
showed). **A fresh pairing is needed on the first boot after this change** — the
phone keeps the old bond against the previous name.

### Fixed — Bluetooth bring-up order (owner: "it appears but doesn't pair")

`btComm.begin()` ran BEFORE `WebServerComm::beginAlwaysOn()`, i.e. before the
first `esp_wifi_set_mode()`. That mode set stops and restarts the radio (see the
U-99 note), which left the just-started classic BT/SPP stack wedged: the car
advertised and appeared in the phone's list, but pairing failed. Bluetooth now
comes up AFTER the Wi-Fi radio has settled into its resting `AP_STA` mode.

### Fixed — `btUp` no longer masks a failed `SerialBT.begin()`

`setup()` and the transport-enforcement loop set `btUp = true` without looking
at `begin()`'s result, so a refused bring-up was never retried and the car could
sit the whole uptime with no Bluetooth. `btUp` now mirrors the real result via
`ensureBtAdvertising()` (new `BluetoothComm::isStarted()`), which retries a
refused begin on a 2 s cadence instead of hammering the controller every pass.

### Known open — car "freezes totally" / Wi-Fi flakiness

The owner's report includes the car freezing and needing a manual restart on
Wi-Fi modes. The bench `out.log` is a PRE-U-109 capture (no own hotspot; 754
PANICs recorded against board `1FB608`) and cannot name the cause of this
report. U-106's RTC breadcrumb (shipped in 1.3.6/1.3.7) now records the
**phase + task + heap** at the crash, so the first post-1.3.7 capture should
name it. This is NOT claimed fixed here.

## UNRELEASED — 1.3.6 (U-106/U-107/U-108/U-109, 2026-10-13): crash-TIME evidence that survives a PANIC + the OLED last-two-lines rule + fresh start + the own-hotspot boot fix

**UNFLASHED (F-61).** This batch supersedes 1.3.5 (never flashed) and is the first
half of the owner's 2026-10-13 field-fix + cleanup round (full plan:
`TRACKS/PLAN-2026-10-13-cleanup-rebuild.md`).

### Why

The field `device_crash_reports` board `1FB608` showed **~754 unexpected boots, every
one `reset_reason = PANIC`, every one `last_crash_phase = ''`** — and a live COM7
capture on 2026-10-13 printed the same: `[CRASH] boot=980 crash=754 reason=PANIC
heap=176400 lastphase=''`. The phase was empty because the flash-backed phase record
is written on TRANSITIONS only (rate-limited up to 30 s) and `crashLogPhase()` was
called from just **three** places, none of them the BT/Wi-Fi/WS/motor paths the owner
reported. A PANIC between transitions therefore recorded an empty phase, and the heap
it recorded was the healthy heap of the boot that *followed* the crash, not the heap
at the crash.

### Added — RTC crash breadcrumb (`CrashLog.h/.cpp`)

- An `RTC_NOINIT_ATTR` breadcrumb (`CrashRtc`) written on every phase change and via
  a per-loop heartbeat: it names the operation, the **FreeRTOS task** that wrote it,
  the **free heap at that instant** and the **low-water heap**, and it **survives a
  PANIC / WDT / software reset** (it does not survive a power cycle — a magic guards a
  cold-boot read). No flash wear, so it is safe on hot paths.
- `crashLogBegin()` captures the dying boot's breadcrumb FIRST and, on a fault,
  prefers its (fresh, non-empty) phase over the rate-limited NVS copy — the fix for
  `lastphase=''` — then persists the extra fields to new NVS keys
  (`botcfg/lheapc`, `lminh`, `lmsec`, `ltask`) so `/status` keeps reporting them
  through clean boots until the next fault.
- New `crashRtcMark(phase)` / `crashRtcHeartbeat()` / `crashLogMinHeap()` API.
- The serial banner gains `[CRASH] rtc: marks=… phase='…' task='…' heap=… minHeap=…
  atMs=…` so a tethered capture names the crash immediately.

### Added — phase coverage on the reported paths

`crashRtcMark()` (flash-free) is called from the paths the owner's reports point at:
the main loop (`switch`/`cmd`/`web`/`relay`), `WebServerComm::handle()` +
`advanceRejoinStep()` + `runScanIfDue()` + WS events + `startOwnAP()` +
`rejoinNetwork()` + `stop()`, the BT paths (`bt-begin`/`bt-read`/`bt-write`),
`L298NControl::drive()` (`motor`), `Oled_1_3::draw()` (`oled`), `RelayComm`
(`relay`/`relay-task`) and `ModeManager::sendStateOn()` (`state`).

### Added — `/status` crash-TIME fields

`last_crash_heap_at_crash`, `last_crash_heap_min`, `last_crash_task`,
`last_crash_millis`, `live_min_heap` (fed from the breadcrumb; additive, the existing
`last_crash_*` keys are unchanged).

### Changed — OLED last-two-lines rule (U-107)

The last two rows now follow the U-105 **transport toggles** instead of the old
idle/link state, per the owner's 2026-10-13 request:

- **both toggles ON** → the Bluetooth frame ⇄ the Wi-Fi frame alternate ~1 s;
- **one ON** → that frame is shown always (no alternation at all);
- the **Wi-Fi frame's last line alternates the network name ⇄ the IP** ~1 s
  (**superseded by U-110** in 1.3.7: the name and the IP are now shown on two
  rows at once, so there is no alternation);
- a **relay (Internet)** link keeps its own fixed frame (a third transport the
  toggles do not name).

`Config.h` gains `BT_SPP_NAME` (`"4WD CAR"`) as the ONE source of the advertised
Bluetooth name — `BluetoothComm::begin()` passes it to `SerialBT.begin()` and the
OLED's Bluetooth frame shows it, replacing the second hard-coded literal.
`OledDisplay::setTransportToggles()` + `ModeManager::pushTransportToggles()` carry
the live toggles (change-gated; toggling repaints at once).

### Changed — own-hotspot SSID case (owner round-4 Phase 5)

`WIFI_AP_NAME` is now **`4WD4MCar_WiFi`** (was `4wd4mCar_Wifi`). The owner's
round-4 field-fix round pinned the exact casing; everything else about the name
is unchanged. The app's own-AP matching stays **case-insensitive** (`isOwnApName`
folds to upper case), so an app that still shows the old casing — or a car still
running the old firmware — keeps working across the flash.

### Added — fresh start + timestamps (owner round-4 Phase 7)

- **`FACTORY;RESET`** (and `FACTORY?` for the syntax reply) — wipes the WHOLE
  `botcfg` NVS namespace (routers, credentials, last-good, toggles, mode/speed,
  relay token, `fw_mark` and the crash counters), restores the RAM defaults
  (both transports ON, boot auto-join ON, no stored Wi-Fi, empty router list),
  resets the boot/crash counters via `crashLogResetCounters()` and replies
  `FACTORY;CLEARED`. There is deliberately NO mid-command reboot: the reply must
  reach the app first. The next power cycle boots a virgin car.
- **`TIME;<unixEpoch>`** — the app is the car's clock (the ESP32 has no RTC).
  Sent on every connect; the car derives the live epoch from that base + its own
  `millis()`. `TIME?` replies `TIME;<now>;<configEpoch>`.
- **`botcfg/config_epoch`** — stamped (from the app-provided epoch) on every
  configured write: `WIFICFG;RESET`, the WIFI/BT/AUTOJOIN toggles, a stored pair,
  and `ROUTERS;ADD/USE/DEL/CLEAR`. Surfaced as the second field of `TIME?`, so a
  stale config record is provable. Boot-state (`mode`/`spd`, `ParamsStore`) is
  intentionally NOT stamped — it changes on every drive action, and the app/DB
  timestamps cover it.

### Fixed — the car came up with NO own hotspot (U-109, "no connection on any method")

**The boot softAP was being skipped every time.** A live bench capture showed
`[RADIO] skip-softAP/sta-busy` immediately after `WiFi.mode(WIFI_AP_STA)`, then
`[V2] Web layer always-on: AP= …` — an **empty** broadcast id. With no own
hotspot and an empty `ap` field, a phone could neither join `4WD4MCar_WiFi` nor
be found by the app's discovery sweep.

- **Root cause — `staBusy()` misread the core's initial status.** On ESP32 core
  3.3.12 `STAClass::_status` is initialised to `WL_STOPPED` and stays there until
  the `arduino_events` task delivers `WIFI_EVENT_STA_START` (which sets
  `WL_DISCONNECTED`) — *asynchronously*, so `WiFi.status()` still reports
  `WL_STOPPED` (254) when the very first `startOwnAP()` runs. `staBusy()`'s
  settle allow-list omitted `WL_STOPPED`, so it fell through to `default: return
  true` (busy) and skipped the softAP. Fixed: **`WL_STOPPED` is now an allowed
  state** — a stopped STA interface is settled by definition.
- **`beginAlwaysOn()` no longer forces `apRunning_ = true`.** That claim marked
  the skipped bring-up as done, defeating the FIN-40/D1 retry in `handle()`
  (which keys on `!apRunning_`), so the hotspot never came up for the whole
  uptime. The flag now carries only what the radio actually achieved.
- **`apName_` is set before any early return in `startOwnAP()`.** The name is
  identity, not state: `buildStatusJson()` reads it for the `ap` field with no
  `apRunning_` guard, so the app's discovery sweep can identify the car even
  before the beacon is up.
- **Boot-port drive no longer touches the LEDC.** `applyBootState()` stops the
  motors before `motor.begin()` (deferred to the end of `setup()` by the U-96
  power budget); `L298NControl::drive()`/`setSpeed()` are now guarded by a
  `begun_` flag, removing the boot serial noise `E ledc: LEDC is not initialized`.
- **`applyBootState()` restores only a LIVE stored mode.** The old `else if`
  could not fire for a valid-but-not-LIVE row; a retired (CS) mode restored from
  NVS now correctly keeps 4WD4M (with a distinct log line), and an out-of-range
  value is logged rather than silently ignored.

### Changed — comment hygiene (U-109b, no behaviour change)

Removed stale/contradictory notes that described retired state as current. The
`Oled_1_3.cpp` header and `redrawNow()` claimed the donor's ESP_SER "signal bars +
RSSI + IP" drawing block was **retained as dead code** — U-79 removed that block,
so both comments are reworded to say so; a duplicated `Fleet-standard computed
layout` comment line (an artifact of the U-107b insert) is deleted; and comments
in `WebServerComm.cpp` / `ModeManager.cpp` / `Config.h` that still named the
retired `ESP_SER` mode or the old `4WDCar_Wifi` AP name as live are corrected.
Companion app change: the pre-U-105 two-name paragraph in the `OWN_AP_NAME` doc
comment is reworded to the single-name contract.

### Changed

- `FW_VERSION` `1.3.5 → 1.3.6`.

## UNRELEASED — 1.3.5 (U-105, 2026-10-13): transport toggles ride, beacon never parked, one own-hotspot name

**UNFLASHED (F-61).** Per the owner's device-report failures and redesign mandate
("remove the parallel thing from the setting entirely and power saving buttons and
their residues"; Q&A round 2026-10-12/13), the parallel-links and power-save
settings are removed wholesale and replaced by **Wi-Fi / Bluetooth / boot
auto-join toggles**, the old own-hotspot name is purged for a single fleet name,
the beacon parking paths are deleted, and the deleted-network survivor is fixed.

### Removed — `WIFICFG;PARALLEL` / `WIFICFG;APSAVE` and their residue

- `WIFICFG;APSAVE;ON|OFF` (NVS `botcfg/ap_psave`, member `apPowerSave_`) and
  `WIFICFG;PARALLEL;ON|OFF` (NVS `botcfg/allow_parallel`, member `allowParallel_`,
  `allowParallel()` getter) are GONE, along with their STATE keys (`;APSAVE=`,
  `;PARALLEL=`), their `/status` JSON fields, the one-active-link teardown in the
  U-79 loop (`LastTransport`/`saveLastTransport`/`loadLastTransport`/grace timer)
  and the `WIFICFG;PARALLEL`/`WIFICFG;APSAVE` command parsing.
- `wipeRoutersOnFreshFirmware()` (fresh-install wipe, U-98) now also erases
  `ap_psave` / `allow_parallel` / `last_transport` residue so a re-flash is clean.

### The three toggles (all default ON, never both transports off)

- **`WIFICFG;WIFI;ON|OFF`** — NVS `botcfg/t_wifi`, member `wifiEnabled_`/getter
  `wifiEnabled()`. OFF stops the whole web layer (`stop()`: AP + STA + WS + mDNS
  torn down; `updateApPowerPolicy()` no-ops while off). ON restarts it.
- **`WIFICFG;BT;ON|OFF`** — NVS `botcfg/t_bt`, member `btEnabled_`/getter
  `btEnabled()`. OFF stops `btComm`; ON restarts it.
- **`WIFICFG;AUTOJOIN;ON|OFF`** — NVS `botcfg/boot_autojoin`, member
  `bootAutojoin_`/getter `bootAutojoin()`. OFF stops the boot hop to the
  last-switched router; `armBootAutoJoin()` is gated on it.
- **Both-off is impossible**: `loadStoredWifi()`, the WIFI/BT command handler and
  the U-79 loop each clamp (`if (!wifiEnabled_ && !btEnabled_) wifiEnabled_ = true;`).
- Both transports stay up SIMULTANEOUSLY when enabled — the "own transport powers
  the OTHER one down" arbitration is deleted. /status JSON gains
  `wifi_enabled`/`bt_enabled`/`boot_autojoin`; STATE gains
  `;WIFI=ON|OFF`/`;BT=ON|OFF`/`;AUTOJOIN=ON|OFF`.
- Bench rows P4-1..P4-5 rewritten (query-only + BT round trip; Wi-Fi OFF is
  device-checklist-only because it kills the bench's own web link).

### Beacon never parked (U-86/U-100 parking deleted)

- `updateApPowerPolicy()` rewritten: bails on `!wifiEnabled_`, otherwise ONLY
  guarantees the beacon — restore `startOwnAP()` when the STA is down; no park
  branch, no `softAPdisconnect`, no `WiFi.mode()` on the policy path.
- `apParked_`, `AP_PARK_GRACE_MS`, `AP_PARK_HTTP_MS` and the `lastHttpAtMs_`
  stamp in `armHttpGuard()` are deleted (parking-era residue).

### Identity + OLED

- `WIFI_AP_NAME` is **`4WD4MCar_WiFi`** — the ONE own-hotspot name; the donor
  `WirelessCar_Wifi` and the earlier `4WDCar_Wifi` are purged from the app
  registry and every firmware comment that stated current behaviour.
- `Config.h` gains `WIFI_AP_IP[] = "192.168.245.1"`; `Oled_1_3.cpp` fallbacks use
  `WIFI_AP_NAME`/`WIFI_AP_IP` (was `"GenumCar"` and the wrong-subnet
  `"192.168.4.1"`).

### Deleted networks stay deleted (the owner's "deleted network survives power cycle")

- `ROUTERS;DEL` registry-row and `wasActive` pair matching switched to
  `strcasecmp` — a different-case delete now removes the row + active pair instead
  of answering "Not saved" and leaving the router to return after reboot.

**Gates:** `check-firmware-syntax.ps1` 10/10; `check-webpage-js.mjs` section 8
rewritten to the transport-toggle contract (never-park, default-ON reads, both-off
clamp, getter/command/loop enforcement); `check-relay-contract.mjs` green.
App half: 3.2.7 same-version JS-only OTA (registry `4WD4MCar_WiFi`, the three
linked-gated toggles, both-live arbitration, deleted-network deep fix).

## UNRELEASED — 1.3.4 (U-104, 2026-10-13): watchdog feed covers the sendState() WS fan-out

**UNFLASHED (F-61).** Hardening from the post-1.3.3 field review (owner's restart/
freeze/wifi-flap loop — see `RESUME-HERE.md`). App half of the round is the F-65
same-version 3.2.7 OTA; the dominant restart evidence on `device_crash_reports`
(board `1FB608`, 929 boots / 746 crashes) is BROWNOUT — a hardware-supply repair,
not firmware — and this round's firmware change is the one provably-safe margin
gain left on the WDT side.

- `WebServerComm::write()` — NEW watchdog feed immediately before each
  `ws.broadcastTXT()`. `ModeManager::sendStateOn()` delivers STATE / CAPS / NETW /
  SCAN through this one function (up to four WS broadcasts per transport per
  1.5 s ticker), and it was the ONLY WS-write path with no feed of its own
  (`broadcastStatus` and `ws.loop()` have theirs). A WS client that stops
  draining TCP can hold a broadcast for up to 5 s (the U-77 measurement); the
  feed gives the write a full `LOOP_TASK_WDT_TIMEOUT_MS` window. Gate rule:
  `ws-broadcast-feed`.
- `FW_VERSION` `1.3.3 → 1.3.4`, so the binary that carries this change names
  itself 1.3.4 on STATE, `/status` and `device_crash_reports.fw_version`.

**Not changed (reviewed, documented):** the U-79 spinlock reads
(`btComm.connected()` / `hasWsClients()` / `relayComm.connected()` under
`portENTER_CRITICAL`) are plain flag reads by design (RelayComm publishes
`socketUp_`/`enrolled_` from its task); the `WiFi.begin()` boot auto-join
beacon-blank window is the documented FIN-40/D2 trade-off with retries already
capped; the PANIC rows in the crash DB stay un-named until a serial/`last_crash_phase`
capture exists (no serial available to this round).

## UNRELEASED — 1.3.3 (M3, 2026-10-13): `currentLink()` + OLED name BOTH live links under parallel

**UNFLASHED (F-61).** With `WIFICFG;PARALLEL;ON` the U-79 loop keeps BT+WiFi up
at once, but `currentLink()` still named only ONE transport (the web client
outranked the Bluetooth pair) and the OLED line 3 collapsed into the generic
`Wifi : <ssid>` row — the same "panel lies when a second transport is live"
defect the owner re-reported on the second connectivity review.

- `ModeManager::currentLink()` — NEW **first** branch: `allowParallel() &&
  wsClients && btUp` returns `{Parallel, BT+WiFi, true}`, outranking the
  single-transport returns because under parallel *both* are really the live link.
- `Oled_1_3::redrawNow()` — line 3 gains the matching `Parallel : BT+WiFi` row
  instead of falling into the `Wifi` else-branch.
- `FW_VERSION` `1.3.2 → 1.3.3` (the target for the owner's U-R4 flash; the app
  half of this round ships as same-version 3.2.7 OTA).

## UNRELEASED — 1.3.2 (U-R3, 2026-10-12): the restart record rides the Bluetooth STATE line

**UNFLASHED (F-61).** Since U-95 only `/status` carried WHY the car reset
(`reset_reason` / `boot_count` / `crash_count` / `last_crash_phase` /
`last_crash_heap`) — a Bluetooth-only link never saw it, so the app's crash-report
sync and the Diagnostics restart row stayed empty over SPP. The STATE line now
carries the same record on **every** broadcast, both links:

- `;RST=<reason>` — the persisted reset-reason label (`ci.label`, e.g. `TASK_WDT`).
- `;BC=<boot count>` and `;CC=<crash count>` — the persisted counters.
- `;PH=<phase>` (only when set) and `;LH=<heap at boot>` — the recorded boot phase
  and boot-time heap, matching the JSON `last_crash_phase` / `last_crash_heap`.
- Phase strings are firmware string LITERALS (`crashLogPhase()` call sites), never
  anything a phone sent — same rule as `buildStatusJson`.
- STATE builder `wifiKeys` grew 224 → 288 (`tKeys` 96 → 176) to hold the record.
- **Relay byte budget grew 448 → 512** to match: a STATE carrying the record plus a
  full `REPLY=` exceeded 448, and the relay drops anything over its cap — the exact
  silent-drop class U-94 Phase 1 shipped with. `RELAY_MAX_OUT_BYTES` / `kOutLineLen`
  / the static_asserts / `check-relay-contract.mjs` / the website relay's
  `maxBytesCarToController` changed together.
- App half: `carProtocol.ts` STATE parsing (RST/BC/CC/PH/LH) + crash-report sync now
  fires over Bluetooth; `AdminScreen` gained a "System Logs" tab reading both
  `device_crash_reports` and the new `app_logs` table.

## UNRELEASED — 1.3.1 (U-102, 2026-10-12): live uptime/heap/signal telemetry on the Bluetooth STATE line

**UNFLASHED (F-61).** The Remote's top status line reads `uptime_ms` / `free_heap` /
`rssi` / `signal` from the WS JSON — but a Bluetooth-only link never sees that
JSON, so over SPP those rows were structurally `—` (the app had no wire to read).
The STATE line now carries the same truth:

- `;UP=<millis>` and `;HEAP=<free heap bytes>` on **every** STATE line (both links).
- `;SIG=<0..100>` and `;RSSI=<dBm>` **only while the car is a WiFi station**
  (`WiFi.status() == WL_CONNECTED`, mirroring `buildStatusJson`'s pct/rssi) — on the
  car's own AP there is no client signal, so those rows stay dashed honestly.
- STATE builder buffer grew 448 → 512 (the old worst case was already ~438 B with a
  full `REPLY=`); app half is `carProtocol.ts` STATE parsing (UP/UPTIME/HEAP/SIG/SIGNAL/RSSI).

## UNRELEASED — 1.3.0 (U-100 Phase 4, 2026-10-12): parallel Bluetooth + WiFi, power-saving toggle (default OFF)

**UNFLASHED (F-61).** Part of the multi-repo "connectivity rebuild" (phased; plan
`TRACKS/PLAN-2026-10-12-connectivity-rebuild.md`). Owner request #6: parallel Bluetooth + WiFi with a
power-saving toggle, **default OFF**.

**Behaviour.** The U-79 loop ran a one-radio power policy: a client on one transport powered the
OTHER down (a phone over Bluetooth dropped the WiFi/AP link, and vice versa) to save power. With the
new switch ON, the two teardown branches are skipped entirely — a Bluetooth client no longer stops
the web layer and a wired client no longer stops Bluetooth, so **both links stay connected at the
same time**; a transport that is still down is brought back with the same idempotent starts the idle
path already uses (`btComm.begin()` / `beginAlwaysOn()` + `armBootAutoJoin()`). OFF (default) is the
old arbitration, byte-for-byte unchanged.

**SWITCH — `WIFICFG;PARALLEL;ON|OFF` (bare / `?` reports).** Handled in
`handleWifiConfigCommand()`, persisted to NVS `botcfg/allow_parallel`, answered as
`WIFICFG;PARALLEL;ON|OFF` over the existing WIFICFG reply channel (BT or web) — it rides the SAME
single system-command hook as ROUTERS/WIFICFG/APSAVE/RELAYCFG. Read at boot in `loadStoredWifi()`
next to `ap_psave`. The U-79 loop reads it every pass through `WebServerComm::allowParallel()`.

**NVS / fresh install.** `botcfg/allow_parallel`, absent on a virgin board → `false`. Like
mode/speed prefs it is a user connection preference, so — unlike the router surface — it is
deliberately NOT in `wipeRoutersOnFreshFirmware()`'s erase set.

**Gates.** `tools/check-webpage-js.mjs` section 8 gained two rules (the switch must default OFF in
`loadStoredWifi()`, and `allowParallel()` must exist in `WebServerComm.h`). The U-79 loop shape
itself is not asserted by any gate (nothing reads the `.ino` loop), so no `mutate-gates.mjs` mutant
was added — same as the U-100 `ap_psave` rules. New bench rows P4-1..P4-3 in
`tools/bench_u90_u92.py` (reversible; restore the default) and the DEVICE-TESTS round
"own hotspot always reachable + parallel BT+WiFi".

**VERIFY — source only.** `tools/check-firmware-syntax.ps1` 10/10 · `check-relay-contract.mjs` PASS
· `check-webpage-js.mjs` PASS · `mutate-gates.mjs` PASS. Nothing flashed; F-61 stands.

## UNRELEASED — 1.3.0 (U-100, 2026-10-12): the own hotspot is ALWAYS reachable — parking is now opt-in (default OFF)

**UNFLASHED (F-61).** Owner report, verbatim: *"the car now goes into one of the other router and
doesnt display its own homepage and doesnt connect to the app too."* Owner ruling (2026-10-12,
this session): *"option 1 but always have the feature to turn those off through the app's remote
screen through the setting."* — i.e. keep `4WDCar_Wifi` broadcast while the car is on a router, and
keep a switch to re-enable the power saving.

**Root cause (source-confirmed).** ESP32 AP_STA is single-radio: when the car joins a router the
softAP follows the router's channel, so a phone on `4WDCar_Wifi` (`192.168.245.1`) loses the link
(`genumsolutions-app .../connection/discovery.ts:12-19`) and the app keeps dialling the stale AP
address. U-98 makes the car auto-join the last-switched router at boot, so it now happens
unattended. Separately, U-86's `updateApPowerPolicy()` could *park* the beacon (drop the AP ~20 s
after a router-side client connected) and then could not re-raise it while the car stayed
associated — so the own hotspot was not a guaranteed way in.

**FIX — parking is now OPT-IN and OFF by default.** The own hotspot is never parked unless the new
NVS switch `botcfg/ap_psave` is on. `updateApPowerPolicy()` gained `if (!apPowerSave_) return;`
before the park, and the parked branch restores whenever the switch is off as well as when the last
router-side client leaves. The U-86/U-87 safety form is untouched (still `softAPdisconnect(false)`
only, never `WiFi.mode()`; `inUse` still requires BOTH a router-side WS/HTTP client AND an empty own
AP; the `!staUp` branch still restores via `startOwnAP()`; a fresh install still wipes the key).

**SWITCH — `WIFICFG;APSAVE;ON|OFF` (bare / `?` reports).** Handled in
`handleWifiConfigCommand()`, persisted to `botcfg/ap_psave`, and answered as `WIFICFG;APSAVE;ON|OFF`
over the existing WIFICFG reply channel (BT or web). It rides the SAME single system-command hook as
ROUTERS/WIFICFG/RELAYCFG. `tools/check-webpage-js.mjs` section 8 now pins both halves: parking must
be gated on the switch, and the NVS read must default to `false`.

**App note.** The app-side mechanism the owner named — *"the app auto-runs findCarForUser on the
phone's network change"* — already exists (U-80b: `useControlHub` watches NetInfo and re-finds the
car). The Remote-screen toggle that drives `WIFICFG;APSAVE` lands in the Remote Connectivity rebuild
(next phase), where the connection settings live.

**VERIFY — source only.** `tools/check-firmware-syntax.ps1` 10/10 · `check-relay-contract.mjs` PASS
· `check-webpage-js.mjs` PASS. Nothing flashed; F-61 stands.

## UNRELEASED — 1.3.0 (U-99, 2026-10-12): the own hotspot is configured ONCE — no duplicate hotspot / two IPs on first start

**UNFLASHED (F-61).** Owner report, verbatim: *"when the very first time car starts the oled
displayes the same esp's hotspot two times one is blank and shows 192.168.4.1 and another shows
4WDCar_Wifi and 192.168.245.1. RESOLVE and keep only the proper one with the name and the proper
ip displayed in the cars oled and dont break other things."*

**D1 — `beginAlwaysOn()` changed the Wi-Fi mode AFTER the AP was up.** The boot path ran
`beginStation()` while the driver was in `WIFI_AP` (the own AP came up on `192.168.245.1`, name
`4WDCar_Wifi`, via `startOwnAP()`), then called `WiFi.mode(WIFI_AP_STA)`. On ESP32 core 3.3.12
`WiFiGeneric::mode()` only returns early for the SAME mode; an AP→AP_STA change calls
`esp_wifi_set_mode()`, which **stops and restarts the radio**. The restarted softAP was rebuilt
from the SDK-default config — a blank name on `192.168.4.1` — while the panel still held the real
`4WDCar_Wifi · 192.168.245.1`. The OLED showed BOTH: the owner's "same hotspot two times, one blank
with 192.168.4.1". **FIX: set the FINAL mode (`WIFI_AP_STA`) FIRST, then bring the AP up exactly
once** — `beginStation()` no longer sets the mode (`startOwnAP()` is the only wifi-config op, and
the AP config is the last one). One mode set, one softAP, one identity. R-16 is preserved:
`startOwnAP()` still runs with nothing joined (`staSharing` false), so the own AP stays on
channel 1.

**SAFETY — the U-92 own-AP guards are untouched and still hold.** The saved-router registry still
purges a row named like the own hotspot at load, and the stored active pair still refuses the own
name; this round is purely about the softAP being configured once, not about the registry.

**VERIFY — source only.** `tools/check-firmware-syntax.ps1` 10/10 · `check-relay-contract.mjs`
PASS · `check-webpage-js.mjs` PASS. Nothing flashed; F-61 stands.

## UNRELEASED — 1.3.0 (U-98, 2026-10-11): a fresh flash starts clean, and a boot returns to the LAST-SWITCHED router

**UNFLASHED (F-61).** Owner report, verbatim: *"clear all the other saved router
datas and only have its own data each time i flash the car so that i can always test from the
start and the car is clean and fresh always"* · *"the car should always go to the last switched
router network always; if it is not available it should show its own router for the user to
connect or control through the car's own webpage"* · *"i deleted the saved router but the car
restarted with the old router that i was previously connected"* · *"the car is stuck on one of
the outside router and not even showing its own webpage too so that i can change and delete and
edit things"*. `FW_VERSION` → `1.3.0`.

**D1 — A RE-FLASH DID NOT RESET THE ROUTER SURFACE.** Arduino flashing does NOT erase NVS, so
every saved router, the stored pair and the last-good pair survived each re-install, and the
next boot silently re-joined an outside router the owner had deleted. `botcfg/fw_mark` now
stores the `FW_VERSION`; `wipeRoutersOnFreshFirmware()` (called in `setup()` **before** the
loads) erases every router-related key (`rnet<N>s/p`, `wifi_ssid/pass`, `last_ok_ssid/pass`,
`last_transport`) and writes the new mark when it does not match. "First boot after a flash" is
the first boot where the RUNNING firmware differs — NOT a power cycle: same-firmware power
cycles keep the saved routers, which U-84 requires. Mode/speed/relay prefs share the `botcfg`
namespace and are deliberately untouched.

**D2 — A DELETED ROUTER RESURRECTED ON THE NEXT BOOT (the "restarted with the old router"
report).** `armBootAutoJoin()` trusted `lastGoodSsid_` (`last_ok_ssid`), and `ROUTERS;DEL`
cleared only `wifi_ssid` — so deleting the ACTIVE router left the last-good pair intact and the
next boot re-joined the very network the owner had just removed. Boot now joins `storedSsid_` —
the last-SWITCHED router, written by every `ROUTERS;USE`/`WIFICFG` — and the last-good pair is
now **informational**, erased alongside the stored pair at all four clear sites
(`WIFICFG;RESET`, `ROUTERS;USE 4WDCar_Wifi`, `ROUTERS;DEL` active, `ROUTERS;CLEAR`).
`storedSsid_` cannot resurrect a deleted network: DEL/CLEAR/RESET wipe it, the last-good pair
dies with it, and a fresh flash starts empty.

**D3 — "STUCK ON AN OUTSIDE ROUTER, NO OWN WEBPAGE."** The stale-router boot in D2 pulled the
car onto a router the phone was not on. The softAP's channel tracks the STA's in AP_STA, so a
phone could not find `4WDCar_Wifi` at the expected channel and had no reachable address for the
page. The fix is D1+D2: a clean car boots onto its own AP, and a stored router that is
unreachable ends in the give-up path, which restores the own hotspot. The channel-follow
limitation itself is unchanged and remains documented.

**BUDGET — THE OLD SHORT BOOT WINDOW IS GONE.** `BOOT_STA_TIMEOUT_MS` / `BOOT_STA_MAX_RETRIES`
(6 s / 1) are removed; the boot auto-join now uses the FULL explicit-switch budget
(`WIFI_STA_TIMEOUT_MS` / `STA_MAX_RETRIES`). This reverses R-16/T-64's short-window concession a
second time, and it is safe for the reason the beacon work (U-87 / FIN-40) established: the own
hotspot beacon stays UP for the WHOLE retry — parking only runs while the car is CONNECTED and
in use — so a long attempt can never strand the owner, and an unreachable router still ends by
restoring the hotspot. `lastTransport` no longer gates the boot hop: the last-SWITCH is about
the router the owner chose, not the last transport; it is now a boot log line only.

**P2 — THE OLED EXTRA ROW NO LONGER SHARES THE BROADCAST'S TICK.** `updateExtraInfo()` (the
OLED's network/extra row) and `broadcastStatus()` (the WebSocket status JSON) ran in ONE
`if (now - lastExtraMs >= EXTRA_INTERVAL)` block, so the OLED's refresh rate could not be
raised without also doubling the status JSON to every client — the wrong trade, and it still
left the row up to a full second stale after a network changed. Split into two ticks:
`EXTRA_INTERVAL` **1000 → 500 ms** for the extra row, while the broadcast keeps its own
`STATUS_INTERVAL = 1000 ms`. The event-driven "pronto" path (U-72: `setExtraInfo` /
`pushLinkInfo` repaint on any real change) is unchanged; this only halves the worst-case
staleness of the RSSI/IP row. New gate rule `extra-status-decoupled`, mutants M68/M69.

**Gates (source-level only).** syntax **10 / 10** · `check-webpage-js.mjs` **PASS** ·
`check-relay-contract.mjs` **PASS** · `mutate-gates.mjs` **69 / 69** byte-exact (run alone; the
tree was stashed clean first and the work restored after — F-61: none of this is device
evidence).

## UNRELEASED — the AP power policy, the `[1/2]` badge, and the build `c0e4ce6` broke

`c0e4ce6` (2026-10-10) did not compile and left half of U-98 behind. Both fixed; `FW_VERSION`
stays `1.2.4` because nothing has been flashed.

`main` was fast-forwarded to this tip (`c5b8b9d`, 2026-10-10) so the owner flashes from `main`;
CI run `38070703254` is green (relay-contract + page-js + compile). "Unflashed" still means the
bench car — CI compiling it is not a device having it.

**BUILD.** `server.hasClient()` does not exist. `hasClient()` belongs to `NetworkServer`, which
`WebServer` **contains** rather than inherits (`WebServer.h:342`, and `_server` is in the
protected section). The branch was red at CI run `38041257338`.

**THE BUG UNDER THE BUG.** The first half of that line was worse than the compile error.
`inUse = ws.connectedClients() > 0` counts clients, and a phone driving the car from
`4WDCar_Wifi` raises that count like any other client — so the car treated **its own hotspot as
a reason to park its own hotspot**, `AP_PARK_GRACE_MS` (20 s) after the page opened. That is
the owner's *"the car is not connected through its own hotspot for the phone after some period
of time … not allowing to connect and switch cleanly, also doesn't connect sometime."*
`inUse` is now `WiFi.softAPgetStationNum() == 0 && (ws.connectedClients() > 0 || httpActive)`:
anyone sitting on the AP forces it false, which both blocks a park and restores a beacon that
should never have been dropped. `softAPgetStationNum()` reads a cached driver count — no radio
work, no netif config — so the one-call-per-pass rule (U-71/F-24) still holds.

The HTTP half is real now rather than a call to a method that never existed: `armHttpGuard()`
runs first in **every** HTTP handler, so it stamps `lastHttpAtMs_`, and `httpActive` covers the
page-load → WebSocket gap under `AP_PARK_HTTP_MS` (15 s, bounded on purpose).

**`[1/2]` — REMOVED END TO END, NOT JUST THE FORMAT STRING.** `c0e4ce6` deleted the `snprintf`
that rendered the badge and nothing else. Gone now: `cycleNetworkDisplay()`,
`setNetworkCycling()`, `setSavedRouterCount()`, their three state fields
(`networkCyclingActive` / `currentNetworkIndex` / `lastNetworkCycleMs`), the `|Nets:` parse in
`Oled_1_3::redrawNow()`, the two callers in `updateExtraInfo()`, and the `|Nets:` producer.
That tail had exactly one reader and it was a **truncation hazard on the line the owner asked
for**: `extra` is 128 B and three long saved-router names pushed the IP out of the `snprintf`
(the `ModeManager.h` comment had recorded the AP row "pass 64" for exactly this reason).
`extraInfo` is OLED-internal — no app or page parses it — so the shorter form is invisible
everywhere except that it now always fits. Saved routers are an unordered set in the firmware,
the page and the app, which is what *"treat all router networks equally"* asks for.

**THE PAGE NEEDED NOTHING.** `WebPage.h` rebuilds the router dropdown from `j.networks` as plain
options — no ranking, no own-hotspot label, no index — and carries no guidance prose in its
network section. Verified rather than assumed; no change made.

**GATE — and why the old rule could not have caught this.** `check-webpage-js.mjs` asserted only
`inUse = ws.connectedClients() > 0`, which is precisely the half that cannot see WHICH side a
client arrived on. It now asserts the WS half, the AP-empty half, that both sit in **one**
expression, and that the HTTP branch is actually fed. Mutation-proven in both directions:
deleting either half fails by name, and deleting the `lastHttpAtMs_` stamp — which used to leave
this gate green — now fails too.**One gate defect found while using it — FIXED 2026-10-11.**
`check-firmware-syntax.ps1` set `$ErrorActionPreference = "Stop"`, so PowerShell 5.1 threw a
`NativeCommandError` on gcc's stderr at the invocation line — **before** the `FAIL <file>` /
`Select-String 'error:'` reporting ever ran. Consequence: on a failing TU the gate printed no
error text at all, and the `foreach` aborted, so the **remaining translation units were never
checked**. It still exited 1, so it was not a silent pass — but it reported a failure with no
cause and silently shrank to partial coverage on the fail path, which is the U-96 class this
gate exists to prevent.

**The fix (2026-10-11):** the gcc invocation now runs with `$ErrorActionPreference` locally
relaxed to `Continue` inside `try/finally` that restores the script-wide `Stop` immediately
after — stderr is captured as data (`2>&1`) instead of terminating the script. Reproduced the
broken behaviour first, then proven in both directions on PS 5.1: one broken TU → `FAIL
BluetoothComm.cpp` + the gcc error line + `SYNTAX GATE FAILED: 1 / 10` (the other 9 still
checked); two broken TUs (a `.cpp` AND the `.ino`) → both named, `2 / 10`, no abort; restored
tree → `PASS 10/10`, exit 0. 

Verified: syntax 10/10 · `check-webpage-js.mjs` · `check-relay-contract.mjs` · `mutate-gates.mjs`
65/65 (byte-exact restore). **F-61: source-level only. Nothing flashed.**

## U-94 Phase 2: the relay dial moves to its own task — UNRELEASED (in `main` since 2026-10-10)

> **IN `main` since 2026-10-10 (verified 2026-10-11 by ancestry: `14cbf5d` sits on `main`'s
> first-parent line; the history is linear, no merge commit), and COMPILED by CI on `main` —
> run `38108951033` (2026-10-11) is green on page-js + relay-contract + compile over the
> `xTaskCreate` RelayComm. Not flashed, not on a car (F-61).** The local `arduino-cli` is blocked
> by an application-control policy, so CI is the authoritative build. `FW_VERSION` is deliberately
> NOT bumped until a bench pass, so this entry has no released version behind it — F-61:
> source-level evidence and nothing more.

**The F-84 fix.** On the loop task, a relay host that completed TCP and then stalled TLS held
`WebSocketsClient::loop()` for up to `NetworkClientSecure`'s 120000 ms `handshake_timeout`,
against a loop-task watchdog measured in wall-clock milliseconds — so the car reset every time
the relay was slow to answer. That timeout is not reachable from the sketch: the client keeps
its `WiFiClientSecure` in a `protected` member with no setter and deletes/recreates it every
retry, and `WEBSOCKETS_TCP_TIMEOUT` is read inside the library's own translation unit.
`RelayComm` now runs the dial, the handshake and every `client_.loop()` on its OWN task
(`xTaskCreate`, 20 KB stack) that is deliberately NOT subscribed to the loop-task watchdog, so
the loop task keeps feeding its watchdog and keeps running motor safety while the handshake
waits.

**That moves ownership, not just a call.** `client_` is touched ONLY by the relay task. The
loop task queues outbound frames and drains inbound frames through two mutex-guarded queues and
reads one published scalar — `socketUp_`, filled by the task from `client_.isConnected()` after
every `loop()`. A supervisor in `handle()` drops the link flags if the task's heartbeat goes
stale, so a wedged relay can never keep the 2 s safe-stop switched OFF. `connected()` is now
`started_ && socketUp_ && enrolled_`.

**Gates, changed deliberately.** `armed-handshake` no longer refuses a non-empty `RELAY_HOST`
(the task IS the evidence the handshake is bounded) but still refuses a config that arms the
relay with the dial back on the loop task. The `enrolment` rule was extended to check both
halves of the socket fact: `connected()` gates on `socketUp_` + `enrolled_`, AND the task fills
`socketUp_` from `client_.isConnected()` (a second source of truth about a socket is the F-82
drift this forbids). Mutants: M18 retargeted (arm the host AND remove the task → must fail
`armed-handshake`), M63 added (sever the socket feed → must fail `enrolment`).
`tools/check-relay-contract.mjs` is green; the build is unproven here — CI on the PR is the
first compile.

**A review pass found the relay could never pair — fixed in the same draft.** A control frame is
`"\x00relay"` + JSON, and the WebSockets library hands the car that payload with the leading NUL
intact. `isControlFrame()` correctly compares with `memcmp`, but `controlEventIs()` /
`controlNumber()` then called `strstr`/`strchr` on the WHOLE frame — which begins with `'\0'`, so
`strstr` searched an empty string and matched nothing, every time. `paired` (and `hello-required`,
`ping`, `link-down`, `lagging`, `refused`) were all invisible, so `enrolled_` never became true and
the entire method was a no-op dressed as a working transport. The helpers now search from
`frame + kControlPrefixLen`. **This bug was on `main` too** (`6da6788`) — not introduced by the
F-84 work — and the `control-prefix` gate was blind to it because it only checked that the prefix
was *tested*, not that parsing *skips* it. So a `control-parse` rule was added (both helpers must
offset by `kControlPrefixLen`), with **M64** proving it: revert the offset and `control-parse` goes
red.

**Two smaller fixes in the same pass.** `answerPing()` built its echo with
`snprintf("%s{...}", kControlPrefix, ...)` — `%s` stops at the prefix's NUL, so the prefix was
dropped and the echo would be forwarded to the phone as a raw car line (breaking the latency row).
It now copies the prefix as BYTES and sends with an explicit length (`sendTXT(echo, n)`), because a
`strlen`-based overload would send zero bytes for a NUL-first buffer. And `stop()` no longer clears
`sInstance`: the task is never deleted (`stop()` only idles it), so a later
`RELAYCFG;RESET`-then-reprovision reconnect was running with a dead event callback and could never
re-pair.

**Two review LOWs, also in the same pass.** `RelayComm::available()` now returns a boolean, exactly
like `WebServerComm::available()`. The old byte-count form reported the current line's remaining
bytes, so once a line's characters were consumed `readPos_` reached its NUL and `available()`
returned 0 *while the `'\n'` terminator was still pending* — `readLineFromComm()` then spun its
whole 20 ms timeout instead of taking the terminator. And `handleTextFrame()` now rejects an inbound
line at `len >= kLineLen` rather than `len > kLineLen`: a line of exactly 128 bytes cannot be stored
with its NUL in a 128-byte slot, so the old guard admitted it and `queuePush()` silently dropped the
last byte. Refusing it (max accepted = 127) matches the outbound rule of never sending a truncated
frame. A new `inbound-fit` gate rule + mutant **M65** pin the boundary.

## U-94 Phase 2: the relay dial moves to its own task — UNRELEASED (in `main` since 2026-10-10)

> **MERGED INTO `main` 2026-10-10 (verified 2026-10-11 by ancestry: the 8 relay commits up to
> and including `57fa28c` sit on `main`'s first-parent line, the history is Linear — no merge
> commit; `feat/relay-phase2-own-task` is a stale pointer at the same tip). Folded in when `main`
> was fast-forwarded for the U-98 release (commit `5a7e7bf`, 2026-10-10 collects the AP power
> policy fix and the `57fa28c` relay harness update, both descendants of the task commit; the
> U-98 Wi-Fi auto-find documentation `c5b8b9d` sits on top).** COMPILED BY CI on `main` — run
> `38108951033` (2026-10-11) is green on page-js + relay-contract + compile over the
> `xTaskCreate` RelayComm. Not flashed, not on a car (F-61). The local `arduino-cli` is blocked
> by an application-control policy, so CI is the authoritative build. `FW_VERSION` is deliberately
> NOT bumped until a bench pass.

**The F-84 fix.** On the loop task, a relay host that completed TCP and then stalled TLS held
`WebSocketsClient::loop()` for up to `NetworkClientSecure`'s 120000 ms `handshake_timeout`,
against a loop-task watchdog measured in wall-clock milliseconds — so the car reset every time
the relay was slow to answer. That timeout is not reachable from the sketch: the client keeps
its `WiFiClientSecure` in a `protected` member with no setter and deletes/recreates it every
retry, and `WEBSOCKETS_TCP_TIMEOUT` is read inside the library's own translation unit.
`RelayComm` now runs the dial, the handshake and every `client_.loop()` on its OWN task
(`xTaskCreate`, 20 KB stack) that is deliberately NOT subscribed to the loop-task watchdog, so
the loop task keeps feeding its watchdog and keeps running motor safety while the handshake
waits.

**That moves ownership, not just a call.** `client_` is touched ONLY by the relay task. The
loop task queues outbound frames and drains inbound frames through two mutex-guarded queues and
reads one published scalar — `socketUp_`, filled by the task from `client_.isConnected()` after
every `loop()`. A supervisor in `handle()` drops the link flags if the task's heartbeat goes
stale, so a wedged relay can never keep the 2 s safe-stop switched OFF. `connected()` is now
`started_ && socketUp_ && enrolled_`.

**Gates, changed deliberately.** `armed-handshake` no longer refuses a non-empty `RELAY_HOST`
(the task IS the evidence the handshake is bounded) but still refuses a config that arms the
relay with the dial back on the loop task. The `enrolment` rule was extended to check both
halves of the socket fact: `connected()` gates on `socketUp_` + `enrolled_`, AND the task fills
`socketUp_` from `client_.isConnected()` (a second source of truth about a socket is the F-82
drift this forbids). Mutants: M18 retargeted (arm the host AND remove the task → must fail
`armed-handshake`), M63 added (sever the socket feed → must fail `enrolment`).

**Verified here:** `node tools/check-relay-contract.mjs` → green. Arming `RELAY_HOST` now passes
`armed-handshake` (proven by temp edit + revert); severing the `socketUp_` feed fails `enrolment`
(proven). **Not verified:** the compile (CI only) and the bench (T-REL still pending).

**Files:** `RelayComm.h`, `RelayComm.cpp`, `tools/check-relay-contract.mjs`,
`tools/mutate-gates.mjs`, plus the `TRACKS/` docs.

**A review pass found the relay could never pair — fixed in the same draft.** A control frame is
`"\x00relay"` + JSON, and the WebSockets library hands the car that payload with the leading NUL
intact. `isControlFrame()` correctly compares with `memcmp`, but `controlEventIs()` /
`controlNumber()` then called `strstr`/`strchr` on the WHOLE frame — which begins with `'\0'`, so
`strstr` searched an empty string and matched nothing, every time. `paired` (and `hello-required`,
`ping`, `link-down`, `lagging`, `refused`) were all invisible, so `enrolled_` never became true and
the entire method was a no-op dressed as a working transport. The helpers now search from
`frame + kControlPrefixLen`. **This bug was on `main` too** (`6da6788`) — not introduced by the
F-84 work — and the `control-prefix` gate was blind to it because it only checked that the prefix
was *tested*, not that parsing *skips* it. So a `control-parse` rule was added (both helpers must
offset by `kControlPrefixLen`), with **M64** proving it: revert the offset and `control-parse` goes
red.

**Two smaller fixes in the same pass.** `answerPing()` built its echo with
`snprintf("%s{...}", kControlPrefix, ...)` — `%s` stops at the prefix's NUL, so the prefix was
dropped and the echo would be forwarded to the phone as a raw car line (breaking the latency row).
It now copies the prefix as BYTES and sends with an explicit length (`sendTXT(echo, n)`), because a
`strlen`-based overload would send zero bytes for a NUL-first buffer. And `stop()` no longer clears
`sInstance`: the task is never deleted (`stop()` only idles it), so a later
`RELAYCFG;RESET`-then-reprovision reconnect was running with a dead event callback and could never
re-pair.

**Two review LOWs, also in the same pass.** `RelayComm::available()` now returns a boolean, exactly
like `WebServerComm::available()`. The old byte-count form reported the current line's remaining
bytes, so once a line's characters were consumed `readPos_` reached its NUL and `available()`
returned 0 *while the `'\n'` terminator was still pending* — `readLineFromComm()` then spun its
whole 20 ms timeout instead of taking the terminator. And `handleTextFrame()` now rejects an inbound
line at `len >= kLineLen` rather than `len > kLineLen`: a line of exactly 128 bytes cannot be stored
with its NUL in a 128-byte slot, so the old guard admitted it and `queuePush()` silently dropped the
last byte. Refusing it (max accepted = 127) matches the outbound rule of never sending a truncated
frame. A new `inbound-fit` gate rule + mutant **M65** pin the boundary.

## U-96i — firmware-app version handshake (2026-10-11)

**THESE TWO FORMS THE SAME ITEM — a car's version is now a fact on the wire, and the app reads it.**
The name U-96i came from the app TODO (the app's half, recorded first). The car half is commit
`3168cac` (Genum_4WD4M_CAR, 2026-10-11); the app half is commit `25e72a9` (genumsolutions-app, same day).

**Car (2026-10-11).** The firmware's WORST known-class problem had been exactly this shape for
sixteen days and the ledger called it "cosmetic" — the app had NO way to know what firmware a car
runs, so the bundled registry catalog value (a static guess, `fwVersion: "1.0.0"` against firmware
that was already `1.2.4`) was presented as truth, and the fleet crash table's `fw_version` column was
filled from it. Worse, the app already had an `fwVersion` input field that was NEVER passed, and
gave the field `fw` the board id's bytes (`statusJson.fw: telemetry.id`) — identity in a key named
for a version.

The vehicle change (commit `3168cac`, in `main` since 2026-10-11, the single commit covering the firmware field, the gate rule and the two mutants):

- `/status` now carries `"fw":"<FW_VERSION>"` — a snprintf `%s` slot fed by `FW_VERSION`.
  This is not optional text the car optionally emits: it is a `%s` in the format string, so devices
  that consume `/status` can only get a stable version SI if a SLOT exists — the EVAR form would be
  missing on a pre-1.2.5 car and the comparison would break. The slot was added as a `%s` fed by
  `FW_VERSION`, so source-level the form is stable: devices parse `j.fw`.
- Gate rule `status-fw` (check-relay-contract.mjs) pins both halves: the slot exists in the format
  string, AND FW_VERSION feeds it in the slot's snprintf position. Key checked on the RAW view,
  argument pairing on the CODE view, so a comment cannot satisfy it. Mutants M66 (key renamed out)
  + M67 (feed severed) added; harness now 67/67.

**App (2026-10-11, same day — commit `25e72a9` in `genumsolutions-app`).**

- `parseTelemetryLine` now maps `j.fw` → `telemetry.fwVersion`. Missing on firmware older than
  1.2.5 (pre-firmware version field) → absent — never overwritten by the catalog value.
- `syncCrashReport` receives `fwVersion: telemetry.fwVersion`; `statusJson.fw` is the VERSION,
  not the board id; `telemetry.fwVersion` joins the effect's dependency list.
- **The bundled device registry's `4wd4m` catalog row is now `fwVersion: "1.2.4"`** (mirror of
  `Config.h` FW_VERSION, 2026-10-11) — a MODEL fact commented as such, not a claim about a live
  car.
- **Tests (three new source-reader rules + one service test):** the parser parses /fw (and the /
  pre-1.2.5 absent case) in carProtocol.test.ts; buildCrashReport now carries `fw_version` from a
  free `fwVersion` input (carProtocol.test.ts + crashReportService.test.ts). Three oneActiveLink
  source-reader rules pin the hub wiring: fwVersion reaches syncCrashReport, statusJson.fw is the
  version not the id, and the dep list includes telemetry.fwVersion — all mutation-proven, including
  one that initially PASSED over a deleted dep (slice included the call body; narrowed to the deps
  array — the recurring vacuous-pass class).

**Status:** source-level only, both repos (F-61). The handshake is proven by gates, not by a device.
A device with firmware older than 1.2.5 (pre-firmware-version-field) reports NO `fw` and the app
treats absent as unknown until the owner flashes to 1.2.5+ (the field was added as a %s in Config.h).

**Why 1.2.5 and not 1.2.4:** 1.2.4 is the current `FW_VERSION` in Config.h for this session's local toolchain build; 1.2.5 reflects the `STATUS` version milestone after the U-96i handshake.

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

