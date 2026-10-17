#pragma once
// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: CrashLog.h — persisted record of why the car last rebooted
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include <stdint.h>

// U-95 / F-79. WHY THIS EXISTS.
//
// The owner reported "the car resets again and again" with no cause attached,
// and the reason nobody could attach one is that the only reset diagnostic the
// firmware had was a Serial line. The bench car has no serial cable attached,
// so "why did it restart" reduced to "it restarted", which is not a
// diagnosis. Worse, the one label that WOULD have identified a hung loop() -
// ESP_RST_TASK_WDT - could never be printed, because the watchdog was
// configured with .trigger_panic = true, which routes a watchdog trip through
// the panic path and reports ESP_RST_PANIC instead. The boot banner even told
// the operator at a label that was therefore unreachable. (This used to quote the
// banner verbatim as "TASK WATCHDECK"; that string was a typo that survives
// nowhere else, and the banner it referred to has since been replaced — the
// authoritative account is in Genum_4WD4M_CAR.ino's crashLogBegin() block.)
//
// So this module does three things the serial print could not:
//   1. counts boots, so a reboot loop is visible as a NUMBER rather than a
//      feeling ("boot 47 in ten minutes" is a fact; "it keeps restarting" is
//      not);
//   2. counts UNEXPECTED boots separately, using the reset reason, so a person
//      power-cycling the car five times to try something is not mistaken for
//      five crashes;
//   3. remembers the last PHASE the car was executing when it died, so the
//      report can name a suspect ("it died serving the root page") instead of a
//      subsystem.
//
// WHY THERE IS NO "uptime at reset" HERE, since the plan asked for one.
// To know how long the previous boot lasted, something has to write the
// running clock to flash periodically, and that is flash wear bought for a
// number we can mostly do without: the phase plus a boot count pins the failure
// far better than a duration would. Recording a duration would also have been
// quietly wrong — on ESP32 there is no RTC, so on a power-on reset the car has
// no idea what time it was, and any timestamp would silently be measured from
// an arbitrary epoch. A number that is sometimes wrong is worse than no number.
//
// The keys live in the SAME "botcfg" Preferences namespace as mode/speed and
// WIFICFG provisioning, so there is one namespace to reason about and no
// collision risk. Only usable from setup() onwards: NVS is initialized by the
// Arduino core immediately before setup().

// What crashLogBegin() found. `valid` means "a recorded unexpected boot
// exists" — NOT "the previous boot was unexpected". Those are different on
// purpose: the record SURVIVES a clean power cycle, because the moment the
// owner can actually read it is after they have unplugged the car and plugged
// it back in to see what it says. Clearing it on the next clean boot would
// throw the evidence away at exactly the wrong moment.
struct CrashInfo {
  bool valid;            // a recorded unexpected boot exists
  uint8_t reason;        // esp_reset_reason_t recorded for that boot
  const char* label;     // human label for that reason
  uint32_t heapAtBoot;   // free heap observed at that boot (0 if unknown)
  uint32_t bootCount;    // boots since the counters were first initialised
  uint32_t crashCount;   // unexpected boots only
  char phase[24];        // what the car was doing when it died ("" = idle)

  // U-106 (2026-10-13): crash-TIME evidence captured in RTC RAM, which survives
  // a PANIC/WDT/software reset (but not a power cycle). The NVS phase above is
  // rate-limited (up to 30 s stale) and is written on TRANSITIONS only, so a
  // crash between transitions recorded an empty phase. The breadcrumb below is
  // written on every mark with no flash cost, so it names the operation that was
  // actually running when the chip died - and the heap at that instant, not the
  // (healthy) heap at the boot that followed.
  bool rtcValid;          // an RTC breadcrumb survived from the dying boot
  uint32_t heapAtCrash;   // free heap at the last breadcrumb (0 = unknown)
  uint32_t minHeap;       // low-water free heap over the dying boot
  uint32_t millisAtCrash; // millis() at the last breadcrumb
  uint32_t marks;         // breadcrumbs written in the dying boot
  char task[16];          // FreeRTOS task that wrote the last breadcrumb
};

// Floor on how often a non-empty phase may reach flash. See crashLogPhase().
constexpr uint32_t CRASHLOG_PHASE_MIN_MS = 30000u;

// ---------------------------------------------------------------------
// P6 (2026-10-17, owner item D): the CAR'S EVENT LOG — a BOUNDED ring of
// the facts a bug/reset/freeze report is built from, tied to the boot
// counter, readable by the app (`LOG?` / `LOG;GET;<n>` / `LOG;CLEAR`,
// WebServerComm::handleLogCommand) and summarised in /status
// (`event_log_count`).
//
// WHY A BOUNDED RING, stated once: F-89 "an on-device log must be bounded
// before it is useful". A log that grows with every event turns a debugging
// aid into a flash-exhaustion bug, so the ring has a HARD cap and the OLDEST
// event is the one dropped. The cap is a compile-time constant (never
// re-tuned at run time), and every append that hits it keeps the newest
// CAR_EVENT_CAP facts and forgets the rest — forgetfulness is the feature.
//
// STORAGE. One fixed array persisted as ONE NVS blob per append (the appends
// are rare: boots, faults, heap-guard edges, link up/down transitions), so
// the write count stays predictable and there is never an unbounded number
// of tiny NVS keys. Events live in the same `botcfg` namespace as the rest
// of the car's config; `FACTORY;RESET` clears the ring with the namespace.
//
// NO SECRETS: entries carry the boot counter, millis, free heap, an event
// kind, a fault reset reason and — only ever — firmware string LITERALS for
// the phase, exactly like /status `last_crash_phase`. A runtime-built phase
// string from a phone or the network would be a JSON/injection hazard in
// LOG;GET's reply; the ring only ever stores what the firmware wrote. Do not
// feed it network input.
// ---------------------------------------------------------------------

// Event.kind values. Numbers ride the wire (LOG;GET), the labels are for
// humans; keep both in step with carEventKindLabel().
#define CAR_EV_BOOT      0u   // every boot, clean or not
#define CAR_EV_FAULT     1u   // an unexpected reset (isFaultReset true)
#define CAR_EV_LOWHEAP   2u   // a heap guard deferred/refused work
#define CAR_EV_LINKCAP   3u   // the car REFUSED a second link (heap floor)
#define CAR_EV_LINKUP    4u   // a transport came up ("wifi" / "bt")
#define CAR_EV_LINKDOWN  5u   // a transport went down

// The HARD cap. 12 events at a fixed 32-byte record is one 384-byte NVS
// blob — small enough that even a router-switch flap costs grams of wear
// and the blob stays a single predictable write.
#define CAR_EVENT_CAP    12u

// One event, fixed size so the blob is a flat array read/write with one
// call. `phase` is a firmware string LITERAL only (see above).
struct CarEvent {
  uint8_t kind;        // CAR_EV_* above
  uint8_t reason;      // esp_reset_reason_t (faults) / 0 otherwise
  uint32_t bootCount;  // the boot counter at the event
  uint32_t millis;     // millis() at the event
  uint32_t heap;       // free heap at the event
  char phase[16];      // "" when not applicable; firmware literals only
};

// Append one bounded-ring event and persist the ring. `phase` may be nullptr
// or ""; only ever pass firmware literals. Safe from setup() onwards.
void crashLogEvent(uint8_t kind, uint8_t reason, const char* phase);

// How many events the ring currently holds (0..CAR_EVENT_CAP).
uint16_t carLogCount();

// Event `idx` counting NEWEST-FIRST: 0 is the most recent.
const CarEvent& carLogAt(uint16_t idx);

// Human label for a CAR_EV_* kind ("BOOT", "FAULT", ..."). Unknown → "EV?".
const char* carEventKindLabel(uint8_t kind);

// Drop every stored event, RAM and NVS. Called by LOG;CLEAR and — via
// crashLogResetCounters() — by FACTORY;RESET.
void carLogClear();

// Call this FIRST in setup(), before anything else allocates or runs, so the
// heap figure recorded is the heap at boot rather than the heap after setup.
void crashLogBegin();

// Declare what the car is about to do, and clear it with "" when idle.
//
// THIS IS CALLED FROM CODE THAT RUNS EVERY LOOP, so the write discipline
// matters. The trade, stated plainly:
//
//   - Returning to idle is ALWAYS written. That sounds wasteful and is not: if
//     the crash record is not cleared on idle, then a car that crashed while
//     simply parked would report the last operation it ever performed, and
//     "died serving the root page" would be a lie. An honest "it died idle" is
//     worth a write.
//   - A NON-EMPTY phase is written at most once per CRASHLOG_PHASE_MIN_MS. A
//     caller that churns through distinct phases every pass is either a bug or
//     a flash-wear bug, and neither is worth destroying the flash over. The
//     cost of this floor is that a non-idle phase can be up to 30 s stale in
//     the record; that is stated here rather than papered over, because a
//     watchdog diagnostic that is usually right and sometimes 30 s stale is
//     still worth having, and one that confidently names the wrong operation is
//     not.
//   - Normal operation writes NOTHING, because a parked car is idle and idle is
//     only written on the idle TRANSITION (i.e. when it was previously
//     non-idle).
//
// Pass a short stable string literal. It is copied into NVS, truncated to fit.
void crashLogPhase(const char* phase);

// U-106 (2026-10-13). Crash breadcrumb in RTC RAM. NEVER touches flash, so it
// is safe to call on hot paths (BT/WS/Wi-Fi callbacks, every loop). It survives
// a PANIC - exactly the reset the NVS phase above could not describe - and is
// read back by crashLogBegin() on the next boot. `phase` names the operation
// ("" keeps the previous name; pass nullptr to refresh only). The current task
// name and free heap are recorded alongside it.
void crashRtcMark(const char* phase);

// Cheap per-loop refresh of millis/heap/low-water WITHOUT changing the phase or
// the mark counter. Call once per loop() pass so a stall/crash still has a
// recent heap and timestamp.
void crashRtcHeartbeat();

// The record captured at boot. Valid for the whole uptime.
const CrashInfo& crashLogInfo();

// The phase currently being executed ("" when idle). RAM only - reading it
// never touches flash, so it is safe to call every loop.
const char* crashLogLivePhase();

// Convenience for the status JSON and the OLED banner.
uint32_t crashLogBootCount();
uint32_t crashLogCrashCount();

// U-106: low-water free heap for the CURRENT boot (RAM only, never flash).
uint32_t crashLogMinHeap();

// U-108 Phase 7 (owner 2026-10-13). Fresh start: zero the boot/crash counters and
// drop the recorded crash record, both in RAM and in NVS. Called by the car's
// `FACTORY;RESET` after it clears the namespace, so the very next boot counts
// from a clean slate (boot=1, crash=0) instead of continuing the old tally. The
// serial line and the OLED reflect the reset immediately.
void crashLogResetCounters();
