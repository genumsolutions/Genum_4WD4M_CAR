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
// the operator to look for the TASK WATCHDECK label that was unreachable.
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
};

// Floor on how often a non-empty phase may reach flash. See crashLogPhase().
constexpr uint32_t CRASHLOG_PHASE_MIN_MS = 30000u;

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

// The record captured at boot. Valid for the whole uptime.
const CrashInfo& crashLogInfo();

// The phase currently being executed ("" when idle). RAM only - reading it
// never touches flash, so it is safe to call every loop.
const char* crashLogLivePhase();

// Convenience for the status JSON and the OLED banner.
uint32_t crashLogBootCount();
uint32_t crashLogCrashCount();
