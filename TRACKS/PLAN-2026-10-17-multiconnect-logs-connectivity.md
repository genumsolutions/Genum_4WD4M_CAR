# PLAN 2026-10-17 — multi-connect + bug/freeze logs + connectivity responsiveness (CAR half, master)

> **Status: PLANNING ONLY (2026-10-17).** No product byte changed in either repo. This
> document is the deliverable the owner asked for ("study properly and plan to implement
> only for now"; "smaller phases, don't overwhelm, proper failsafe notes, push properly").
> Implementation starts only when the owner says go — one phase at a time, each ending in
> green gates + commit + push.
>
> App half mirrored in
> `genumsolutions-app\TRACKS\PLAN-2026-10-17-multiconnect-logs-connectivity.md` (keep in
> sync). Cross-repo round note stays in the root `RESUME-HERE.md`.
>
> **Builds on, and does not reopen, the 2026-10-15 round** (`PLAN-2026-10-15-dual-link-and-
> remote-ux.md`, P1–P8, source-complete). That round delivered the *first* dual-link fix
> (P6), the *first* dialog pass (P2/issue 14), the BT heap diet (P4) and the settings
> round-trip (P7). This round covers what the owner reported **after** testing that body.

## Owner request (2026-10-17, verbatim intent — six items)

1. **Dual-link BT + Wi-Fi connect problem** — connecting both still misbehaves; the concern
   is *when* the second link is raised and how the app tells the user.
2. **Reconnect dialog** — it does not appear at the proper time, it shows stale/unnecessary
   messages, and it should offer **only Reconnect + Cancel** and be more advanced
   (state-aware), not a generic banner.
3. **Reset status in the control panel** — should show the **bare minimum (last reset only)**
   but be done *better* than the current `Diagnostics` strip.
4. **Bug / reset / freeze log** — a feature to **see and act on** the log of bugs, resets and
   freezes, **tied to the car's unique profile** (a profile under the user's profile) so it
   can actually be addressed per car.
5. **Connectivity responsiveness** — the app must react faster to networks / updates /
   changes; the user wants to be able to **tell the app where the car's network is**, or have
   it work **automatically** if that is achievable.
6. **Car freeze on multi-connect** — when the car is connected by multiple methods at once it
   sometimes freezes; resolve by **managing heap/resources**, *or* have the app know that
   multi-connect cannot be made because of ESP limits (heap or other).

Standing directives carried into this round: **make smaller phases; don't overwhelm; make
proper failsafe notes; push properly; plan before implementing.**

## 0 · Current state (what already exists — do not rebuild)

- **Dual link is structurally supported since U-105.** The car's `loop()` "both transports
  on" branch (`Genum_4WD4M_CAR.ino` L415–436) only *starts* a downed transport and never
  stops one; `linkManager` adds a link without tearing down the previous one; `sendCommand`
  fans out to every live transport (`useControlHub.ts` ~L2617). P6/issue 1 fixed the app-side
  gate: `ConnectionSection.tsx` now keys the Bluetooth scan on `btLive` (Bluetooth's own
  `sppStatus`), not the shared `linkLive`.
- **The reconnect/restart prompts already exist** as centered opaque modals
  (`RemoteControlScreen.tsx` `renderPromptShell` L617–655; `renderReconnectBanner`
  L657–697 → Reconnect + Cancel; `renderRestartBanner` L699–759 → Reconnect + Leave +
  Cancel). P2/issue 14 already made the card opaque and the copy state-aware.
- **A reset record already reaches the panel.** `ToolsScreen.tsx` L253–262 builds one
  `restartField` via `buildRestartField` (`telemetryFormat.ts` L187–247) and shows it inside
  the `Diagnostics` disclosure (L437–449, "Last restart record").
- **Fleet diagnostics already upload.** `crashReportService.ts` writes one row per
  connection session to Supabase `device_crash_reports` (reset_reason, boot_count,
  crash_count, last_crash_phase, last_crash_heap). `appLogService.ts` keeps a bounded local
  ring of warn/error lines and best-effort-uploads to `app_logs`; `listLocalLogs()`
  (L215–216) is **already written and currently unused — "for any future on-device log
  viewer"**. `AdminScreen.tsx` L4297+ / L4376 renders crash rows for staff.
- **Car identity is already unique per car.** `carFwId` = `ESP.getEfuseMac` last-6-hex;
  `addressForMemory` (`useControlHub.ts` L715–731) yields `fw:<boardId>` (preferred) →
  BT-MAC → `wifi:<ssid|ap|url>`. Per-car prefs live in `deviceMemory`
  (`genum.device.<address>`) and mirror to `car_profiles` keyed `(user_id, profile_key)`.
- **Discovery already exists.** `discovery.ts` sweeps a /24 for the car's `/status`
  (`findCarOnNetwork`, `OWN_AP_NAMES = ["4WD4MCar_WiFi"]`); a NetInfo watcher
  (`useControlHub.ts` L2410–2429) fires `autoDiscoverAndConnect()` (L2325–2399) on a genuine
  phone-network move, with a 10 s cooldown / 15 s re-arm (max 6 ≈ 90 s).
- **Heap guards already exist** — status-broadcast deferral (`STATUS_HEAP_FLOOR_BYTES`
  20000, `WebServerComm.h` L175), scan refusal (`SCAN_HEAP_FLOOR_BYTES` 65536, L187), boot
  probe refusal (`BOOT_SCAN_HEAP_FLOOR_BYTES` 20000, L64), emergency motor stop
  (`HEAP_CRITICAL_BYTES` 4000, `Config.h` L127). **There is no heap-based link admission and
  no "cannot accept another link" report anywhere** — that is the gap in item 6.

## 1 · Findings per item (source-level, no device needed)

### A · Dual link — the remaining gap is *timing/ordering + honesty*, not capability
- Both links up is supported; P6 fixed Wi-Fi-then-BT. The remaining risk is the **connect
  sequence**: `handleConnect` sets `connected=true` and `btLinked=true`
  (`useControlHub.ts` ~L2031) and the Wi-Fi status handler owns `connected` too, so the UI's
  "linked" notion can be optimistic while the second link is still negotiating.
- There is **no link-admission signal** from the car, so the app cannot tell "second link is
  coming up" from "second link was refused". This is the same root as item 6 — plan them
  together (phases 3–5).

### B · Reconnect dialog — present, but the *trigger* and *vocabulary* are the complaint
- `renderReconnectBanner` is gated on `showSppsRetry && !linked` (L662). `showSppsRetry` is
  raised only on SPP backoff exhaustion when neither Wi-Fi nor BLE is up, and cleared when
  `connected || wifiConnected` turns true. The complaint "not at the proper time" points at
  this single boolean being the *only* trigger: a Wi-Fi drop, a restart, an AP move and a BT
  drop all collapse into either "nothing" or "Bluetooth disconnected".
- The **restart** prompt still carries three actions (**Reconnect + Leave + Cancel**), which
  the owner explicitly wants reduced to **Reconnect + Cancel**.
- "Stale/unnecessary messages": the prompt is a snapshot; it has no age, no live "link now"
  refresh for the reconnect case, and nothing suppresses a prompt whose cause has already
  cleared.

### C · Reset status — the `Diagnostics` strip is more than "last reset only"
- Today it renders **one field** whose *value* is the crash count, with the reason+phase+heap
  buried in `hint` (`telemetryFormat.ts` L226–246). So it reads "Restarts: 3" first, not
  "last reset: TASK_WDT during http-root".
- Owner's ask: **bare minimum — the last reset only** — but cleaner. That is a *presentation*
  change: lead with the last reset fact; keep counts secondary/collapsed.

### D · Bug/reset/freeze log tied to the car profile — half the plumbing exists
- Car side: there is **no log-fetch verb** (`CRASH`/`LOG` do not exist). The car exposes only
  the *current* restart record: STATE `;RST/;BC/;CC/;PH/;LH` (`ModeManager.cpp` L597–604) and
  `/status` JSON `reset_reason/boot_count/crash_count/last_crash_*`. `CrashLog.cpp` keeps an
  RTC breadcrumb (`crashRtcMark/heartbeat`, `CrashInfo`) but **only the last event**; there is
  no ring of past events.
- App side: `crashReportService` pushes per-session rows to `device_crash_reports`;
  `appLogService` has a local ring + an unused `listLocalLogs()`. **No per-car viewer exists.**
  The natural home is a screen under the car's profile (`fw:<boardId>`), fed by (i) the car's
  own event ring, (ii) the app's local ring scoped to that car, (iii) the cloud rows.

### E · Connectivity responsiveness — cadence and an explicit "where is the car" control
- Auto-discovery only reacts to a *genuine* NetInfo move and then waits 10 s / re-arms every
  15 s; it requires a prior Wi-Fi interest. There is **no way for the user to tell the app
  which network/subnet the car is on**, and no react-to-`isConnected`/`details` (rather than
  just `ssid`) path. mDNS (`genum-car.local`) is advertised by the car but the app does not
  use it as a discovery shortcut.

### F · Multi-connect freeze — the car has guards, not admission
- Measured pressure (from the previous round's bench): both-links `free ≈ 21.7 kB`,
  `maxAlloc ≈ 10.2–10.7 kB`; a PANIC was observed in `loopTask` ~138 s after boot. The
  existing guards *defer telemetry* and *refuse scans* but **never refuse or shed a link**,
  and there is no reserved budget for a second transport's stacks.
- User's allowed outcome: fix it **or** tell the app honestly that multi-connect cannot be
  made (ESP limit). Both are in scope; the plan does the fix *and* the honest report, so a
  car that genuinely cannot host both says so instead of freezing.

## 2 · Traceability — item → phase

| Item | A dual | B dialog | C reset | D logs | E connectivity | F multi-connect |
|---|---|---|---|---|---|---|
| Phase | P3 (app), P5 (app) | **P1** | **P2** | P6 (car) · P7 (app) | P8 (app) · P9 (car) | P4 (car) · P5 (app) |

## 3 · Phases (one at a time; each ends green gates + commit + push)

Each phase lists: **Scope · Finding · Approach · Acceptance · Failsafe · Device rows.**
"Failsafe" names the existing invariant(s) it must not break and the new ledger entry it
adds. Device rows use a new **U-116** series in `genumsolutions-app/mobile/TESTING.md` and
`Genum_4WD4M_CAR/TRACKS/DEVICE-TESTS.md`.

### P0 — Evidence + failsafe notes (NO code)
- Capture on the current flash (1.3.12) before any code: **M1 multi-connect soak** (serial
  across connect-BT-then-Wi-Fi and the reverse, 60 s: which `[HEAPW]` pair, does `loopTask`
  PANIC, at what `free`/`max`); **M2 reconnect timing** (when exactly the prompt appears vs
  the link state); **M3 reset strip** (screenshot of the `Diagnostics` row).
- Record the **failsafe notes** (below) as ledger entries once P1 lands.
- Acceptance: captures attached to P0 in the ledgers; no code touched.

### P1 — [B] Reconnect dialog: right trigger, right words, two buttons (APP, JS-only OTA)
- **Scope:** `RemoteControlScreen.tsx` prompts + the hub flags that drive them;
  `useControlHub.ts` (`showSppsRetry`, `carRestartNotice`, `handleReconnectPromptCancel`,
  `handleSppsRetry`, `handleRestartReconnect/Dismiss`).
- **Approach:** (1) introduce ONE **prompt-state** decision (pure function) that takes the
  live link facts (`btLinked`, `wifiConnected`, `wifiReconnecting`, recent restart notice,
  prompt age) and returns *which prompt (if any)* should be on screen — so a Wi-Fi-only drop,
  a BT-only drop, a both-down state and a car-restart each get their own honest copy, and a
  prompt whose cause has cleared auto-dismisses (kills stale messages). (2) Reduce the
  restart prompt to **Reconnect + Cancel** (drop Leave; Back already leaves). (3) Add a short
  "as of <n>s ago / link now: …" line so the prompt is never a stale snapshot.
- **Acceptance:** prompts never show while `linked`; a cleared cause dismisses within one
  tick; restart prompt has exactly two actions; new `reconnectPrompt.test.ts` pins each state
  both ways (mutations → red).
- **Failsafe:** keeps invariant 2 (silence → stop) untouched (this is UI only); adds
  **F-85 "a prompt is a claim about the present"**.
- **Device rows:** U-116-1 (BT drop → prompt at the right moment), U-116-2 (Wi-Fi drop →
  correct copy), U-116-3 (restart → two buttons), U-116-4 (cause clears → prompt goes).

### P2 — [C] Reset status: last reset only, done cleanly (APP, JS-only OTA)
- **Scope:** `ToolsScreen.tsx` Diagnostics block (L437–449) + `telemetryFormat.ts`
  `buildRestartField` (or a new `buildLastResetField`).
- **Approach:** lead with **the last reset** as the value (`TASK_WDT · http-root`), with the
  heap only when small; move `boot`/`crash` counts to a single secondary hint (or a collapsed
  detail). Keep the three honest states (never-crashed / old-firmware / offline) exactly as
  the current doc-comment defines them. Consider a **per-car** framing (this car) so it is
  the same fact the profile log (P7) shows.
- **Acceptance:** the row's primary value is the last reset, not a count; tests updated
  (`telemetryFormat.test.ts`); no false "None" when offline.
- **Failsafe:** F-79 (three indistinguishable dash states) preserved; adds **F-86 "the
  headline row is the fact the owner acts on"**.
- **Device rows:** U-116-5 (crash car shows reason+phase), U-116-6 (clean car shows "None" +
  clean-boot hint), U-116-7 (offline shows dash, never a stale record as current).

### P3 — [A] Dual-link connect ordering + honest "coming up" state (APP, JS-only OTA)
- **Scope:** `useControlHub.ts` connect flow (`handleConnect` ~L2031, `handleWifiConnect`
  ~L2806–2836), `ConnectionSection.tsx`, `RemoteControlScreen.tsx` linked label.
- **Approach:** make the second-link path **explicit and ordered** (raise the second link
  only after the first is confirmed, never optimistically), track the second link's own
  pending/failed state, and show **"Wi-Fi coming up…" / "Bluetooth coming up…"** instead of
  flipping the shared `connected` flag early. Depends on P5 for the car's admission answer.
- **Acceptance:** the app never claims `BT+WiFi` before both are actually live; a refused
  second link is shown as refused, not as linked. New `dualLinkOrder.test.ts`.
- **Failsafe:** F-61 (device truth, not a sent command); keeps `oneActiveLink` tests green.
- **Device rows:** U-116-8 (BT→Wi-Fi order), U-116-9 (Wi-Fi→BT order).

### P4 — [F car] Multi-connect budget + link admission (CAR firmware → **1.3.13**)
- **Scope:** `WebServerComm.h` (new budget constant), `Config.h` (`HEAPW_*` stage for
  admission), `Genum_4WD4M_CAR.ino` (the both-transports branch, `ensureBtAdvertising`),
  `WebServerComm.cpp` (`startOwnAP`, `broadcastStatus`), `ModeManager.cpp` (status JSON +
  STATE line).
- **Approach:** add ONE **`linkAdmission()`** decision (`free` and `getMaxAllocHeap()` vs a
  reserved budget) that gates *raising a second transport*. When the budget is not there:
  keep the incumbent link live, **do not start the second**, and **report it** (a `/status`
  field + a STATE key such as `;LINKCAP=1|0;` and a reason). Never enter a state that can
  PANIC on allocation — reserve the floor *before* the second stack is created and shed
  deferrable work (telemetry, scans) first. Bump `FW_VERSION` 1.3.12 → **1.3.13**.
- **Acceptance:** a 10-min both-links soak holds `free ≥ budget`, zero resets; when the
  budget is absent the car answers "cannot add second link", not a freeze. New contract rule
  `link-admission` + mutants; `check-firmware-syntax` 9/9; `heap-waterfall` unchanged.
- **Failsafe:** `.ino` invariant 1 (non-blocking boot) and invariant 2 (silence → stop)
  preserved; the admission path must **clear the second transport's flags** so the failsafe
  still sees the incumbent link. Adds **F-87 "a resource that cannot be reserved must be
  refused, not attempted"**.
- **Device rows:** U-116-10 (both links fit → both up, `LINKCAP=1`), U-116-11 (budget absent
  → second refused, incumbent survives, `LINKCAP=0` + reason), U-116-12 (10-min soak).

### P5 — [F app] Surface the car's link capacity (APP, JS-only OTA)
- **Scope:** `carProtocol.ts` (parse the new field), `useControlHub.ts`, `ConnectionSection.tsx`
  / `RemoteControlScreen.tsx`.
- **Approach:** when the car reports `LINKCAP=0`, the app **disables the second connect
  action** with an honest reason ("The car is low on memory right now — one link only."),
  rather than letting the user start a link that will be refused. If the car is an older
  firmware with no field, behaviour is unchanged (assume capable) — no false refusal.
- **Acceptance:** a `LINKCAP=0` car cannot start a second link from the UI; older firmware
  unchanged; new `linkCapacity.test.ts`.
- **Failsafe:** false-refusal guard (absent field ⇒ capable) is the F-86/F-82 shape; adds
  **F-88 "capability absent ≠ capability denied"**.
- **Device rows:** U-116-13 (LINKCAP=0 disables the second connect with the reason).

### P6 — [D car] A bounded event log the car can hand over (CAR firmware → **1.3.13**)
- **Scope:** `CrashLog.h/.cpp` (a ring of the last N events: boot, fault reset, low-heap
  event, link add/drop, phase at fault) in RTC + NVS; a new verb in `ModeManager` /
  `WebServerComm` system hook (`LOG?`, `LOG;GET;<n>`, `LOG;CLEAR`); `/status` JSON summary.
- **Approach:** a **bounded** ring (fixed cap, oldest dropped) so flash and RAM stay
  predictable; `LOG;GET` returns the newest entries first, chunked, NACK-free for old
  clients (unknown verbs already NACK safely). `FACTORY;RESET` continues to clear it
  (`crashLogResetCounters`).
- **Acceptance:** the car answers `LOG?` with a bounded, parseable list; a car with no events
  says so honestly; contract rule `event-log` + mutants; syntax 9/9.
- **Failsafe:** bounded ring (never unbounded flash growth); no secrets in entries; adds
  **F-89 "an on-device log must be bounded before it is useful"**.
- **Device rows:** U-116-14 (force a reset → event appears), U-116-15 (`LOG;CLEAR` empties).

### P7 — [D app] Per-car log viewer under the car profile (APP, JS-only OTA)
- **Scope:** a new screen/panel reachable from the car's profile (Remote Settings and/or the
  Control Panel "Saved settings" `CarProfileCard`); fed by `LOG;GET` (P6), the local
  `appLogService` ring scoped to the car, and cloud `device_crash_reports`; reuse
  `crashReportService` write path. Local cache keyed `fw:<boardId>`.
- **Approach:** show the car's events newest-first with a level chip (bug/reset/freeze),
  the phase/heap and a human line ("TASK_WDT during http-root · low heap 8.2 KB · 3m ago").
  Actions: **refresh from car**, **copy/share**, **upload** (existing cloud path), **clear on
  car** (`LOG;CLEAR`, confirmed). Never invent events; offline shows the cached view, marked
  as cached.
- **Acceptance:** the viewer shows real car events for the selected car only; clear is a
  confirmed live command; upload dedupes per session; new `carLogViewer.test.ts`.
- **Failsafe:** per-car scoping (no cross-car bleed); F-61 (cached vs live labelled); adds
  **F-90 "logs are per-device or they are rumours"**.
- **Device rows:** U-116-16 (viewer lists real events), U-116-17 (clear works), U-116-18
  (offline shows cached, labelled).

### P8 — [E] Connectivity responsiveness + "where is the car" (APP, JS-only OTA)
- **Scope:** `useControlHub.ts` NetInfo watcher (L2410–2429) + `autoDiscoverAndConnect`
  (L2325–2399); `discovery.ts`; a new per-car **"Car network"** setting in the connection
  panel (`ConnectionSection.tsx` / `WifiPanel.tsx`).
- **Approach:** (1) react to `isConnected`/`details` changes (not only `ssid`) and shorten
  the settle/cooldown so a genuine change is picked up faster; (2) add a user field —
  **"Car network" (SSID / subnet or IP)** — persisted per car (`DevicePrefs`), which
  auto-discovery probes FIRST before a full sweep; (3) try the car's mDNS
  (`genum-car.local`) as a shortcut before the /24 sweep; (4) keep the manual override
  authoritative when set.
- **Acceptance:** a user-set network is probed first and remembered; a phone network change
  triggers a bounded re-find faster than today; older cars unaffected; new
  `connectivityResponsiveness.test.ts`.
- **Failsafe:** R-16a (no silent auto-join of a *router*; this is discovery only); extends
  F-38 (chosen method leads); adds **F-91 "a user can always say where the car is"**.
- **Device rows:** U-116-19 (set Car network → found first), U-116-20 (phone network change →
  re-find within the new bound).

### P9 — [E car] Make the car easy to find (CAR firmware → **1.3.13**, if needed)
- Only if P8's device pass shows the app cannot find the car reliably. Scope: ensure mDNS
  advertises stably (already `genum-car.local`, `startMdns`), and consider including the
  broadcast/subnet truth in a compact, already-existing status field. **Deliberately last and
  conditional** — most of item 5 is app-side.
- **Acceptance / failsafe:** gated behind the P8 result; no new wire field without an app
  consumer in the same commit (CAPS-truth discipline).

### P10 — Close-out（finalize）
- All gates green: car (syntax, webpage-js, firmware-contract incl. new rules,
  mutate-gates clean-tree) + app (vitest, `tsc --noEmit`, prettier).
- Ledgers + records updated: car `TRACKS/CHANGELOG.md` (1.3.13), `TRACKS/DEVICE-TESTS.md`
  (U-116-*), `TRACKS/RUNSHEET.md` (flash 1.3.13); app `mobile/TESTING.md` (U-116-*),
  both `TRACKS/NEXT-SESSION.md`, root `RESUME-HERE.md`, `Readme.md`.
- Ship: owner flashes **1.3.13** (huge_app, 115200); app publishes the JS-only OTA
  (**3.2.7/60 unchanged** unless a native change sneaks in — none planned); both CIs green;
  `backup` branches re-snapshotted.
- F-61 honesty: every U-116 row is the owner's bench, not our gates.

## 4 · Failsafe notes (to record when the owning phase lands)

These extend `guide/FAILSAFES.md` (next free number **F-85**). Each is the *shape* the
mistake would take if the phase were done carelessly; each phase's test must reject it.

- **F-85 — a prompt is a claim about the present.** A dialog that outlives its cause, or that
  names the wrong link, is a lie the user acts on. The prompt state must be derived from live
  link facts every tick and dismissed when the cause clears. (P1)
- **F-86 — the headline row is the fact the owner acts on.** "Restarts: 3" is a count; "last
  reset: TASK_WDT during http-root" is the bug report. Counts are secondary. (P2)
- **F-87 — a resource that cannot be reserved must be refused, not attempted.** Admission is
  checked *before* the second transport's stacks are allocated; a failed allocation on a real
  device is a reset, not an error return. (P4)
- **F-88 — capability absent ≠ capability denied.** A car that never sends a capacity field
  must be treated as capable (attempt the link); only an explicit refusal disables it. (P5)
- **F-89 — an on-device log must be bounded before it is useful.** A ring without a hard cap
  turns a debugging aid into a flash-exhaustion bug. Cap first, then read. (P6)
- **F-90 — logs are per-device or they are rumours.** Events must be keyed to the car's unique
  profile (`fw:<boardId>`); a merged feed misleads across the fleet. (P7)
- **F-91 — a user can always say where the car is.** Automatic discovery is best-effort; a
  manual "Car network" override must exist, persist and win. (P8)

Also carried as review gates: **F-61** (device truth ≠ green gates) on every phase;
**F-79** (the three distinguishable dash states) in P2; the car's `.ino` invariants
**1** (non-blocking boot) and **2** (silence → stop) in P4/P6.

## 5 · Open questions for the owner (defaults chosen — say the word to change)

- **Q4 (items 1/6):** when the car cannot host a second link, should the app (a) simply
  disable the second connect with a reason (default), or (b) allow it and let the car refuse
  live? Default = (a).
- **Q5 (item 6):** is the acceptable fix "manage the heap and keep both" **or** "refuse
  honestly" if the heap truly cannot hold both? Default = do both: reserve/shed to keep both
  when the budget exists, refuse honestly when it does not.
- **Q6 (item 4):** where does the per-car log live — a new **per-car screen** off the car
  profile (default), or folded into the existing Control Panel `Diagnostics` disclosure?
- **Q7 (item 4):** should the app upload the car's event log to Supabase automatically
  (fleet diagnostics, default yes, deduped) or only on an explicit "share" tap?
- **Q8 (item 5):** is a manual **"Car network" (SSID/subnet)** field acceptable in the
  connection panel (default yes), remembering that R-16a forbids silent *router* auto-join —
  this is discovery only?

## 6 · Definition of done

All six items closed with **device evidence** attached to their U-116 rows; every phase's
gates green including mutation proof; both repos pushed with clean trees; CI green on both;
**1.3.13** flashed and the app OTA published; `backup` updated. F-61 honesty applies
throughout: a green gate is not device evidence.
