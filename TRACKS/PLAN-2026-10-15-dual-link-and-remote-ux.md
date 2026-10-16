# PLAN 2026-10-15 — dual link + remote-screen UX + BT heap (CAR half, master)

> **2026-10-16 status addendum.** **U-115** landed (`5d26e9c`) — the boot auto-join
> now probes the air before it rejoins, so an absent router no longer holds the own
> hotspot on a moving channel (owner report 2026-10-16: "detected but won't connect");
> new rule `boot-scan-first` + mutants M96-M100, gates green, `FW_VERSION` **1.3.12**.
> **P4 is CLOSED** — E1 captured **6/6** and `STATUS_HEAP_FLOOR_BYTES` re-tuned
> 49152 → **20000** (`cce743e`); P6's headroom gate is therefore satisfied.
> **P6 (issue 1, dual link) is CODE-DONE** (`052379c`) — the app's Bluetooth scan no
> longer hides behind a live Wi-Fi link (`dualLinkConnect.test.ts`, 4 mutations → red)
> and the firmware premise is pinned by `dual-link-kept` + M101-M103. **No firmware
> byte changed**, so 1.3.12 stands; the device half is rows **U-113-12..14**, still
> owed from the owner's bench. **Q3** (which link is primary) is still open — default
> = broadcast lines on both, since `chosenRadio` stays null and mode parity picks the
> radio. The original 2026-10-15 text below is kept for the P7/P8 detail still owed.
> **The E3/E4/E11 captures and Q1-Q3 remain the owner's.**

Status: **P1 DONE · P2 DONE · P3 code landed (all app-side) · P4 STEP 1 + STEP 2 LANDED — the E1
heap-waterfall breadcrumb **and** the diet's first step (classic-BT-only bring-up, so the
never-used BLE memory is released) are now in the firmware (source-only, gates green). The E1
capture is still owed from the bench.** Phase 0 captures (E1 + E2/E5/E6 + the P3 audit log) are
still owed from the owner's 1.3.11 bench pass; P1 did not need them (source-level root cause, see
below). **Version: the current car body — U-112 + U-114 + T66 + P4/E1 + the P4 diet — is 1.3.11
(the owner's bench + E1 flash); P5 rides the same flash.** The app half is
mirrored in `genumsolutions-app\TRACKS\PLAN-2026-10-15-dual-link-and-remote-ux.md`;
cross-repo round log stays in the root `RESUME-HERE.md`.

Owner request (2026-10-15, pasted text, verbatim intent — 14 issues; #14 was missed in this
plan's first pass and added on the 2026-10-15 re-read of the same pasted text):

1. **Dual link** — app+car connected over Bluetooth AND Wi-Fi at the same time. Connecting both
   fails today; owner asks whether it is possible on the ESP32 at all.
2. **Merge the two lines** on the Remote screen: the line below Back (status/disconnect) and the
   line below that (tab selector: D-pad / Joystick / Telemetry / Connectivity) → ONE line, giving
   the freed space to the content below.
3. **Conditional disconnect buttons** — BT disconnect only while linked over BT, Wi-Fi disconnect
   only while linked over Wi-Fi; each must really disconnect its own method; appear only at
   sensible times.
4. **Remove the "phone Wi-Fi off / on &lt;ssid&gt;" chip** from that line — useless.
5. **Settings dropdown too narrow** (`w-64`) — widen it and arrange the rows so content fits.
6. **MAIN issue** — Bluetooth ON collapses free heap 110 KB → ~10 KB; BT+Wi-Fi together sometimes
   &lt; 14 KB → the car resets and freezes. Fix the heap itself, not only the guards.
7. **Wi-Fi OFF must hide everything** — the phone still sees the hotspot AND its webpage; the SSID
   must disappear entirely and the page must stop being served. With BT+Wi-Fi both ON, the car
   must offer BOTH the network and the webpage.
8. **Prompt persistence + sync** — every one of these settings must store in the car AND in the
   app promptly and stay in sync.
9. **Page always up when Wi-Fi is on** — never wait for the app/remote to connect first.
10. **Top-right of Back** — drop the duplicate/gibberish label (`BT · name + WiFi · ip (AP)`);
    keep the broadcast IP there and make IT the link to the car's page; remove the now-duplicate
    page link from the tab line below; sane, standard spacing everywhere.
11. **Mode rotate button dead** — the rotate icon on the Remote screen doesn't cycle modes (sticks
    on 4WD4M); the car's physical mode button works, the app's must too.
12. **Settings audit** — remove the "auto-join last router" toggle, make the OTHER auto-join fully
    working, and audit EVERY other control on that panel; fix whatever doesn't work.
13. **Finish both projects** — every issue closed with device evidence, gates green, both repos
    pushed, CI green.
14. **Reconnect dialog unreadable** — the network reconnect dialog "merges with background
    text" and says nothing useful about the connection itself; make it a proper alert dialog
    with an opaque, easy-to-read background.

## Feasibility answer (owner asked this for issue 1)

**Yes — simultaneous BT + Wi-Fi is possible on the ESP32, and it is already half-built:**

- The radio supports Classic-BT + AP/STA coexistence (RF is time-sliced). The car ALREADY runs BT
  advertising and its own hotspot at the same time — that is the normal state of the U-79 loop
  (`.ino` L395+: "both transports on" branch).
- The app already keeps both links: `linkManager.ts` L289 (U-105: *"no teardown of the previously
  active link — both transports may…"*), `parallelBoth = connected && wifiConnected`
  (`RemoteControlScreen.tsx` L323), `sendCommand` fans out to every live transport
  (`useControlHub.ts` L2617), `sendSystemLine` targets spp → ble → wifi (L2652).
- What is MISSING is (a) the connect sequence that gets the SECOND link up (Phase 6) and
  (b) enough heap to survive both (issue 6 / Phase 4) — at &lt;14 KB a second link is a crash.

## Traceability — issue → phase

| Issue | 1 dual | 2 merge | 3 cond-disc | 4 phone-chip | 5 width | 6 heap | 7 wifi-off | 8 sync | 9 page-up | 10 IP-link | 11 rotate | 12 settings | 13 finish | 14 dialog |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Phase | P6 | P2 | P2 | P2 | P3 | P4 | P5 | P7 | P5 | P2 | P1 | P3 | P8 | P2 |

## Phase 0 — evidence from the owner's current flash + device test (NO code)

The owner is tethered and testing 1.3.11 now. These captures turn each symptom into a named cause
before any code is touched. `python tools/serial_capture.py COMx <secs> <out.log>` throughout.

| # | Capture | How | Proof we want |
|---|---|---|---|
| E1 | BT heap waterfall — **the breadcrumb is now built in** | **RUNSHEET step 6b**: 60 s from one cold boot spanning pair → page → both links (`serial_capture.py COMx 60 e1-heap-waterfall.log`); six `[HEAPW] <stage> free=… max=…` lines | free **and** max per stage → where the ~100 KB goes (controller? host? BLE started but unused?) and whether `max` collapses while `free` holds, i.e. fragmentation vs leak |

> **Bench log (2026-10-15): 1.3.11 is FLASHED to the car (COM8) and boots verified**
> — see `TRACKS/DEVICE-TESTS.md` for the log. **E1 is PARTIAL: 4 of 6 stages**
> (`boot 181884/110580`, `wifi-ap 123568/110580`, `bt-stack 23216/12276`,
> `both-links 21716/10228` — the classic-BT-only diet already cost only ~12 KB of
> `free` where the BTDM collapse was ~100 KB, but `max` still falls to 10228 B,
> i.e. the fragmentation story is alive). **`spp-connected` + `page-served` are
> still owed** and are the owner's phone-side actions, so the floor re-tune is
> still open. E2–E6 remain un-captured.
| E2 | Mode rotate | car serial while tapping rotate 5× (plus app log if available) | does the token leave the app at all? does the car answer `NACK;E=UNKNOWN_MODE`? |
| E3 | Wi-Fi OFF keeps beacon | phone Wi-Fi scan + serial across `WIFICFG;WIFI;OFF` | `[POWER] Wi-Fi off` printed? `softAPdisconnect` actually ran? `apRunning_` truth? |
| E4 | Page without app | Wi-Fi ON, NO app connected, phone browses `http://192.168.245.1` | page renders vs blank/hang → issue 9 |
| E5 | Dual connect | serial + app screens while connecting BT first, then the Wi-Fi link (and vice versa) | exactly which link refuses/drops |
| E6 | Reset / freeze | 60 s serial across any recurrence | `[CRASH] rtc: …` line names phase, task, heap |

## Phases (one at a time; each ends green gates + commit + push, per the owner's standing ruling)

### P1 — Issue 11: mode rotate cycles again (app, JS-only OTA; car only if evidence says so)

**✅ DONE 2026-10-15 — APP-ONLY, NO CAR CHANGE. Root cause = H2 in a variant the plan did not
name: the NACK rollback.**

- **H1 ruled out** for this symptom: mode tokens are not in `EVERY_LINK_COMMANDS`, but the
  OLD `activeMode`'s transports for 4WD4M list BOTH radios, so the token did leave the app.
- **Actual cause:** `cycleMode` walked the full `REMOTE_MODE_ORDER` and sent the next token per
  R-10 ("planned/CS stay cyclable — the car will NACK if it rejects it"). But U-79 REMOVED
  `ESP_SER`/`ESP_CLI` from this car's registry — absent from CAPS = `GONE` (U-81). So every tap:
  4WD4M → ESP_SER → car answers `NACK;E=UNKNOWN_MODE` → `handleNack` rolls the optimistic pick
  back to car truth (4WD4M). "Seems like resetting to the same 4wd4m." The dropdown rows already
  refused GONE tokens (U-81); `cycleMode` was never taught the exception.
- **Fix (app `roboCarCatalog.ts` + `useControlHub.ts`):** new pure `nextCyclableToken(token,
  isUnavailable)` skips only what the caller marks; `cycleMode` marks
  `modeAvailStatus(...) === "GONE"` — WIP/CS stay cyclable (R-10 intact), no CAPS table ⇒ old
  behaviour. Tests + F-83 source-wiring pin (`cycleModeGone.test.ts`), mutate-proven both ways;
  tsc 0 · vitest 701/701 · prettier clean. Device row U-113-1 owed (F-61).
- E2 still useful for the ledger (does the NACK line appear when the app SENDS a retired token
  from other paths?), but did not gate this fix.

### P2 — Issues 2, 3, 4, 10: Remote screen chrome rebuilt (app, JS-only OTA)

**✅ DONE 2026-10-15** (app commit `c035e8c`; `remoteChrome.test.ts`; tsc 0 · vitest 705/705 ·
prettier clean). Device rows **U-113-2..4 + U-113-16** owed from the owner's bench pass (F-61).

All in `RemoteControlScreen.tsx`:

- **Merge** the disconnect row (L868-921, `h-7`) and the tabs row (L924-999, `h-7`) into ONE
  constant-height row: `[BT-disc?] [WiFi-disc?] · telemetry … · [D-pad][Joystick][Telemetry][Connectivity]`.
  The content area below gains ~28 px. Narrow-screen priority if it overflows: disconnect chips >
  tab pills > telemetry fields (final say with the owner, see Open Questions).
- **Conditional chips (issue 3):** today `connected` is a SHARED flag — the Wi-Fi status handler
  sets it true (`useControlHub.ts` L1745) and `handleWifiDisconnect` never clears it (L2324-2348),
  so the BT chip shows on a Wi-Fi-only link and survives its own disconnect. Introduce an explicit
  `btLinked = sppService.isConnected || bleService.isConnected` (mount reconcile L1266-1276 too),
  drive the BT chip from it, and make each chip disconnect ONLY its own transport
  (`handleWifiDisconnect` already does; give BT its own safe-stop that leaves Wi-Fi up).
- **Remove the phone-net chip** (L906-921, `testID="phone-net"`) and its now-unused selectors —
  the NetInfo discovery itself stays (smart-link uses it).
- **IP as THE link (issue 10):** chrome row right of Back keeps the broadcast IP, rendered as the
  tappable link (`handleOpenWebPage`); the long `deckLinkLabel` (L570-577) collapses to the short
  form (`BT+WiFi` / `Bluetooth` / `WiFi` + IP), and the duplicate page-link chip in the tabs row
  (L940-966) is removed. Keep the one link-status dot (U-108 ruling: ONE status).
- **Reconnect dialog (issue 14):** `renderPromptShell`'s transparent card lets the screen text
  show through (owner: "merges with background text"). Both prompts (reconnect + car-restart)
  get an OPAQUE, high-contrast surface (solid theme background + border, zero transparency) and
  copy that names the real connection state (which link dropped, which link is still up) instead
  of a generic "Connection lost". Device row U-113-16.
- Acceptance: one row below Back; chips only for live methods and each disconnects just itself;
  no phone-net chip; IP chip opens the page; no duplicate link below; device rows U-113-2..4 +
  U-113-16 with before/after screenshots.

### P3 — Issues 5, 12: Settings panel widened + fully audited (app, JS-only OTA)

**◑ CODE LANDED 2026-10-15** (app commit `d510a03`): the dropdown widened `w-64` → `w-80` and
the CAR's "Boot auto-join (car)" toggle removed (Q1's UI half). **The control AUDIT (issue 12)
is the owner's bench pass** — rows **U-113-5..7**. A 2026-10-15 cleanup also repaired the
removal, which had left a dead JSX fragment parked inside a JSX comment.

- Widen the dropdown (`absolute right-3 w-64`, L1307) to fit content (e.g. `w-80` / %-based with
  a max), re-lay the rows so labels + switches + helper text all fit without truncation, keep
  `maxHeight` scrolling behaviour.
- **Remove the "auto-join last router" toggle** (= "Boot auto-join (car)", L1391-1421 — its helper
  text literally says *"rejoins its last router"*). Open Question Q1 decides how deep the removal
  goes (UI only vs firmware default too).
- **Make the other auto-join fully working** ("Auto-join saved router (app)", `autoJoinRouter`):
  walk the smart-link path (`useControlHub.ts` L2945-3042, `carProfileService`) end-to-end on
  device; fix what's broken.
- **Audit EVERY control on the panel** and log pass/fail in the device rows: Wi-Fi toggle,
  BT toggle, Reset car data, Steering/Trim steppers (2WD1M gate), mode chooser + rotate (see P1),
  speed strip, the four tab pills, settings open/close, Back (navigate-without-disconnecting),
  reconnect/restart prompts, floating button drag. Fix the failures.
- Acceptance: TESTING.md checklist + device rows U-113-5..7; vitest green.

### P4 — Issue 6: BT heap diet (car firmware → 1.3.11) — THE main thing

- **✔ STEP 1 LANDED 2026-10-15 — the breadcrumb exists; the CAPTURE (E1) is still owed.**
  `heapWaterfall(stage)` (`Config.h`) plus six edge-gated call sites now print
  `[HEAPW] <stage> free=… max=…` for exactly the stages named below (boot → BT controller up →
  SPP connected → own AP up → page served → both links), with `max` =
  `ESP.getMaxAllocHeap()` so fragmentation shows without moving the `free` total. New
  `heap-waterfall` contract rule + mutants **M81-M88**; syntax 9/9 · webpage-js PASS ·
  contract 0 failures · TSH PASS. **The owner captures it as RUNSHEET step 6b.**
- **✔ STEP 2 LANDED 2026-10-15 — the diet's first step: classic-BT-only bring-up.**
  Prime suspect (a) turned out to be source-visible: `BluetoothSerial::begin()`'s `disableBLE`
  argument defaults to false (`BluetoothSerial.h:43`), so the old bare `begin(name)` ran the
  controller in **BTDM** mode and allocated BLE controller + host memory the SPP-only car never
  uses. `BluetoothComm::begin()` now calls `SerialBT.begin(BT_SPP_NAME, false, true)` →
  `ESP_BT_MODE_CLASSIC_BT` → core 3.3.12's `btStartMode()` releases the BLE half for us
  (`btMemRelease(BT_MODE_BLE)`, `esp32-hal-bt.c`). New `bt-heap` contract rule + mutants
  **M89/M90 (84/84)**. Folded into 1.3.11 rather than a separate body: nothing was flashed, so
  the E1 capture the owner takes is the **post-diet** figure the floor re-tune below needs.
- Spec for the rest of the measurement (still owed): a ~100 KB drop for classic BT is far above
  the usual 40-70 KB, so if the post-diet `bt-stack` line is still low the remaining suspects are
  (b) default ACL/bt config buffers and (c) the T-7b BT-restart watchdog causing heap
  fragmentation (check `ESP.getMaxAllocHeap()`, not just free heap).
- **Re-tune the U-112 floors with the new numbers (STILL OWED):** at 10-14 KB the 49152 B status
  floor defers telemetry to 1 attempt/4 s AND the 65536 B scan floor refuses EVERY scan
  (`ROUTERS;SCAN;BUSY`) — i.e. today, BT ON ≈ router list permanently busy. After the diet,
  re-measure steady-state and adjust the floors (or make them relative) so both guards still bite
  where they should.
- Acceptance: soak 10 min with BT+Wi-Fi+page+both links live, free heap sustained ≥ agreed floor,
  zero resets; serial evidence; device row U-113-8. Flash 1.3.11.

### P5 — Issues 7, 9: transport policy made honest (car firmware → rides the 1.3.11 flash)

- **✅ Wi-Fi OFF hides everything (issue 7) — SOURCE FIXED + gated (2026-10-15).** The plan's
  own hint (`stop()` drops the beacon only `if (apRunning_)`) turned out to be half of the
  cause, and reading the source named the whole thing without needing E3 first:
  1. `stop()` gated the beacon teardown on `apRunning_` — a *believed* flag the code itself
     force-writes false across an `AP_STA`↔`AP` mode bounce (`GIVEUP_AP`, `REJOIN_STEP_AP`), so
     a stale value silently skipped `softAPdisconnect()`;
  2. `handle()`'s FIN-40/D1 retry (`if (!apRunning_ && … >= AP_RETRY_INTERVAL_MS)`) then
     **re-created the hotspot ~2 s after `WIFICFG;WIFI;OFF`** — `stop()` had just cleared that
     flag, and `stop()` only runs while `webUp` is true, which the U-79 loop clears as it calls
     it, so the resurrected beacon was **never taken down again**. `updateApPowerPolicy()`
     correctly no-ops when `!wifiEnabled_`, so nothing else could.

  Fix (three guards): `WiFi.softAPdisconnect(true)` unconditional · `startOwnAP()` refuses while
  `!wifiEnabled_` (covers boot/rejoin/give-up, not just the retry) · the `handle()` retry is gated
  on `wifiEnabled_`. **E3 is now the device confirmation, not the diagnosis.** Gated by
  `wifi-off-hides-ap` + mutants **M91-M93**.
- **Page always up (issue 9) — no new fix needed; now regress-proof + gated.** U-105 removed the
  power-save *park*, and neither `handleRoot()` nor `serveRootBounded()` gates on a connected
  client, so the page is already served with NO app attached. Gated by `page-always-on` (+ mutants
  **M94/M95**); **E4** remains the device proof.
- **Both ON ⇒ both offered (issue 7b):** verify the U-79 "both enabled" branch end-to-end on
  device: hotspot visible + page browsable + BT advertising, simultaneously.
- Acceptance: device rows U-113-9..11; `check-webpage-js` / `check-firmware-contract` green with
  the new rules. ✅ **Gates green** (syntax 9/9 · contract 0 failures · webpage-js PASS · TSH PASS ·
  mutate-gates **89/89**). **Still owed:** the E3/E4 captures + rows U-113-9..11 from the owner's
  bench (F-61).

### P6 — Issue 1: dual BT+Wi-Fi link, end to end (app + car)

- Depends on **P4** (heap headroom) — do not attempt both links at <14 KB.
- Reproduce from E5, then close the gap in the app's connect sequence so the second link comes up
  while the first is live (the keep-both plumbing already exists — see Feasibility above).
- Confirm command fan-out is safe when both are live (`sendCommand` L2617: mode-based routing;
  `EVERY_LINK_COMMANDS` broadcast set) — no double-apply on the car; define which link is primary
  for commands (Q3).
- Extend `oneActiveLink.test.ts` / `linkManager.test.ts` (their current job is proving NO
  teardown) with the both-up connect path; device rows U-113-12..14: connect both, status shows
  `BT+WiFi`, disconnect each independently, commands work over each.
- **✅ CODE-DONE 2026-10-16 (car `052379c`, app `02569c1`) — the gap was ONE line,
  app-side.** `ConnectionSection` gated the Bluetooth scan on the SHARED `linkLive`
  (`btLive || wifiConnected`), so a live Wi-Fi link hid "Find my car" — and since
  `sppDevices` fills only from a scan, the paired list stayed empty too.
  **Wi-Fi-second already worked; Bluetooth-second could not.** The gate now keys on
  `btLive` (Bluetooth's own `sppStatus`); the dead `linkLive` prop is removed.
  New `dualLinkConnect.test.ts` pins both directions (**4 mutations → red**); the
  firmware premise is pinned by **`dual-link-kept` + M101-M103** with **no firmware
  byte changed**. E5 was not needed: the root cause is source-level (P1's precedent).
- **Still owed (F-61):** device rows **U-113-12..14** — the owner's bench. **Q3** stays
  open with default = broadcast lines on both (mode parity picks the radio;
  `chosenRadio` stays null, so no double-apply).

### P7 — Issue 8: prompt persistence + sync sweep (app + car)

- Table every setting from P2/P3 (Wi-Fi, BT, auto-join, mode, speed, trim, steering, per-car
  prefs): source of truth → car NVS reply → DevicePrefs → cloud profile, with latency and failure
  mode. Owner's bar: "store in the car and app **pronto**".
- Fix the gaps found (e.g. `toggleCommitRef` grace, `queryCarTransportToggle` refresh on focus,
  cloud write batching); extend `carProfileService.test.ts` / `deviceProfileRegistryService`
  tests. Device row U-113-15: toggle → kill app → reopen → car reboot → still in sync.

### P8 — Issue 13: close-out

- All gates green: car (syntax, webpage-js, firmware-contract incl. new rules, **mutate-gates
  clean-tree run with new mutants**) + app (vitest, typecheck, lint).
- Ledgers + records updated: `TRACKS/CHANGELOG.md`, `TRACKS/DEVICE-TESTS.md` (U-113-1..16),
  app `mobile/TESTING.md`, both `TRACKS/NEXT-SESSION.md`, root `RESUME-HERE.md`, `Readme.md`,
  `TRACKS/RUNSHEET.md` (flash 1.3.11 step).
- Ship: owner flashes 1.3.11 (huge_app, 115200), app pushes the JS-only OTA (3.2.7/60 unchanged),
  Arduino CI + app CI verified green on both repos, `backup` branches re-snapshotted.

## Open questions for the owner (defaults chosen — say the word to change them)

- **Q1 (issue 12):** how deep does "remove auto join last router" go? Default = remove the toggle
  from the Remote settings AND stop the firmware boot-hop (so only the app's smart-link
  auto-joins). Alternative = hide the toggle only, firmware behaviour untouched.
- **Q2 (issue 2):** if the merged single row overflows on a narrow phone, may we shrink/drop the
  telemetry fields FIRST and keep disconnect chips + tab pills always visible? Default = yes.
- **Q3 (issue 1):** with both links live, should drive commands go over BOTH (current fan-out,
  idempotent on the car) or over one primary with the other as status-only? Default = both, since
  `EVERY_LINK_COMMANDS` already assumes it.

## Definition of done (issue 13)

All 14 issues closed with device evidence attached to their U-113 rows; every phase's gates green
including mutation proof; both repos pushed with clean trees; CI green on both; 1.3.11 flashed and
the app OTA published; `backup` updated. F-61 style honesty applies: device-verified = the owner's
own captures, not our gates.
