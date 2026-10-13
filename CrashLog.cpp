// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: CrashLog.cpp — persisted record of why the car last rebooted
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include "CrashLog.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_attr.h>          // U-106: RTC_NOINIT_ATTR crash breadcrumb
#include <freertos/FreeRTOS.h> // U-106: FreeRTOS.h before task.h
#include <freertos/task.h>     // U-106: pcTaskGetName()
#include <string.h>

static const char *PREF_NS = "botcfg";
static const char *KEY_BOOT   = "bcount";  // total boots
static const char *KEY_CRASH  = "ccount";  // unexpected boots only
static const char *KEY_REASON = "lreason"; // last unexpected reset reason
static const char *KEY_HEAP   = "lheap";   // free heap at that boot
static const char *KEY_PHASE  = "lphase";  // phase during that boot
// U-106 (2026-10-13): crash-TIME evidence mirrored from the RTC breadcrumb.
static const char *KEY_HEAPC = "lheapc";  // free heap at the last breadcrumb
static const char *KEY_MINH  = "lminh";   // low-water heap over the dying boot
static const char *KEY_MILLI = "lmsec";   // millis() at the last breadcrumb
static const char *KEY_TASK  = "ltask";   // task that wrote the last breadcrumb

// U-106. A breadcrumb written to RTC SLOW RAM on every phase change and every
// loop pass. RTC_NOINIT survives a PANIC / WDT / software reset (it is the same
// memory the bootloader's RTC data uses); it does NOT survive a power cycle, so
// the magic guards against reading uninitialised RAM on a cold boot. Nothing
// here writes flash, so it is cheap enough for the BT/WS/Wi-Fi hot paths and the
// main loop - which is the whole point, since the flash-backed phase above is
// written on transitions only and was empty in every PANIC row in the field.
#define CRASH_RTC_MAGIC 0x43344D52u   // 'C4MR'
struct CrashRtc {
  uint32_t magic;
  uint32_t seq;        // breadcrumbs written this boot
  uint32_t millis;     // millis() at the last write
  uint32_t heap;       // free heap at the last write
  uint32_t minHeap;    // low-water free heap this boot
  char phase[24];
  char task[16];
};
RTC_NOINIT_ATTR static CrashRtc s_rtc;
// The breadcrumb left by the boot that just ended, captured once at boot before
// anything re-arms s_rtc.
static bool s_rtcPrevValid = false;
static CrashRtc s_rtcPrev;
// RAM low-water for the CURRENT boot (survives nothing; reported live).
static uint32_t s_bootMinHeap = 0;


static CrashInfo s_info;
// What the car is doing RIGHT NOW. RAM only, so reading it every loop costs
// nothing and never touches flash.
static char s_livePhase[sizeof(s_info.phase)] = {0};
// What is currently in NVS. Kept separate from s_livePhase because a phase
// change can be rate-limited (see CrashLog.h): the RAM copy always tracks the
// present, the flash copy tracks what we could afford to record. Without this
// split, a rate-limited write would be skipped once and then suppressed
// forever by an early "unchanged" return.
static char s_flashedPhase[sizeof(s_info.phase)] = {0};
static uint32_t s_lastPhaseWriteMs = 0;
static bool s_phaseWriteSeen = false;

// The reset reasons that are NOT faults, and therefore must not move the crash
// counter. Note what is deliberately NOT in this list: ESP_RST_UNKNOWN is
// counted as a crash. An unexplained reset is precisely the thing this module
// exists to find, and classifying "we do not know" as "probably fine" would
// hide it. ESP_RST_SW is excluded as a deliberate esp_restart() — this firmware
// never calls one, so if it ever shows up the record is worth reading.
static bool isFaultReset(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:
    case ESP_RST_EXT:
    case ESP_RST_DEEPSLEEP:
    case ESP_RST_SW:
      return false;
    default:
      return true;
  }
}

static const char *resetLabel(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "POWER_ON";
    case ESP_RST_EXT:       return "EXTERNAL_RESET";
    case ESP_RST_SW:        return "SOFTWARE_RESTART";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INTERRUPT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "OTHER_WDT";
    case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP_WAKE";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
  }
}

// U-106: the current FreeRTOS task name (loopTask on core 1, the Wi-Fi/BT
// event task on core 0, ...). Tells us WHICH task died, not just where.
static void crashRtcTaskName(char *dst, size_t n) {
  const char *tn = pcTaskGetName(nullptr);
  if (tn == nullptr) tn = "";
  strncpy(dst, tn, n - 1);
  dst[n - 1] = '\0';
}

void crashRtcMark(const char *phase) {
  const uint32_t h = ESP.getFreeHeap();
  if (s_bootMinHeap == 0 || h < s_bootMinHeap) s_bootMinHeap = h;
  if (phase != nullptr) {
    strncpy(s_rtc.phase, phase, sizeof(s_rtc.phase) - 1);
    s_rtc.phase[sizeof(s_rtc.phase) - 1] = '\0';
  }
  s_rtc.seq++;
  s_rtc.millis = millis();
  s_rtc.heap = h;
  if (s_rtc.minHeap == 0 || h < s_rtc.minHeap) s_rtc.minHeap = h;
  crashRtcTaskName(s_rtc.task, sizeof(s_rtc.task));
}

void crashRtcHeartbeat() {
  const uint32_t h = ESP.getFreeHeap();
  if (s_bootMinHeap == 0 || h < s_bootMinHeap) s_bootMinHeap = h;
  s_rtc.millis = millis();
  s_rtc.heap = h;
  if (s_rtc.minHeap == 0 || h < s_rtc.minHeap) s_rtc.minHeap = h;
}

void crashLogBegin() {
  const esp_reset_reason_t reason = esp_reset_reason();

  // U-106: capture the RTC breadcrumb from the boot that just ended FIRST, before
  // anything re-arms s_rtc. The magic guards a cold boot (uninitialised RTC RAM).
  s_rtcPrevValid = (s_rtc.magic == CRASH_RTC_MAGIC);
  if (s_rtcPrevValid) s_rtcPrev = s_rtc;   // struct copy - RTC RAM, not flash

  Preferences prefs;
  // Read-write even on the first ever boot: Preferences::begin(ns, true)
  // FAILS when the namespace does not exist yet, and a module whose very first
  // job is to record a counter cannot afford to skip its own first write.
  if (!prefs.begin(PREF_NS, false)) {
    Serial.println("[CRASH] NVS unavailable - no crash record this boot");
    s_info.bootCount = 0;
    s_info.crashCount = 0;
    s_info.heapAtBoot = ESP.getFreeHeap();
    s_info.reason = (uint8_t)reason;
    s_info.label = resetLabel(reason);
    s_info.valid = false;
    return;
  }

  s_info.bootCount = prefs.getUInt(KEY_BOOT, 0) + 1u;
  s_info.crashCount = prefs.getUInt(KEY_CRASH, 0);

  // The heap figure is captured HERE, before setup() allocates anything, so it
  // describes the boot rather than the steady state. If the previous boot was
  // already tight, this is the number that says so.
  const uint32_t heap = ESP.getFreeHeap();

  if (isFaultReset(reason)) {
    s_info.crashCount++;
    s_info.valid = true;
    s_info.reason = (uint8_t)reason;
    s_info.label = resetLabel(reason);
    s_info.heapAtBoot = heap;
    prefs.getBytes(KEY_PHASE, s_info.phase, sizeof(s_info.phase));
    s_info.phase[sizeof(s_info.phase) - 1] = '\0';
    // U-106: the RTC breadcrumb is FRESHER than the rate-limited NVS phase, so it
    // wins when it has a non-empty name. This is the fix for `lastphase=''`.
    if (s_rtcPrevValid) {
      if (s_rtcPrev.phase[0]) {
        strncpy(s_info.phase, s_rtcPrev.phase, sizeof(s_info.phase) - 1);
        s_info.phase[sizeof(s_info.phase) - 1] = '\0';
      }
      s_info.rtcValid = true;
      s_info.heapAtCrash = s_rtcPrev.heap;
      s_info.minHeap = s_rtcPrev.minHeap;
      s_info.millisAtCrash = s_rtcPrev.millis;
      s_info.marks = s_rtcPrev.seq;
      strncpy(s_info.task, s_rtcPrev.task, sizeof(s_info.task) - 1);
      s_info.task[sizeof(s_info.task) - 1] = '\0';
    }
    prefs.putUInt(KEY_REASON, s_info.reason);
    prefs.putUInt(KEY_HEAP, heap);
    prefs.putBytes(KEY_PHASE, s_info.phase, sizeof(s_info.phase));
    prefs.putUInt(KEY_HEAPC, s_info.heapAtCrash);
    prefs.putUInt(KEY_MINH, s_info.minHeap);
    prefs.putUInt(KEY_MILLI, s_info.millisAtCrash);
    prefs.putBytes(KEY_TASK, s_info.task, sizeof(s_info.task));
  } else {
    // A clean boot does NOT clear the previous crash record. See CrashLog.h:
    // the record has to outlive the power cycle that follows a crash, because
    // that is when a human goes to read it. It is overwritten by the next
    // fault, and until then it keeps telling the truth about the last bad boot.
    s_info.valid = prefs.getUInt(KEY_REASON, 0xFFu) != 0xFFu;
    if (s_info.valid) {
      s_info.reason = (uint8_t)prefs.getUInt(KEY_REASON, 0);
      s_info.label = resetLabel((esp_reset_reason_t)s_info.reason);
      s_info.heapAtBoot = prefs.getUInt(KEY_HEAP, 0);
      prefs.getBytes(KEY_PHASE, s_info.phase, sizeof(s_info.phase));
      s_info.phase[sizeof(s_info.phase) - 1] = '\0';
      // U-106: restore the crash-time extras persisted by the fault above, so
      // /status keeps reporting them through clean boots until the next fault.
      s_info.heapAtCrash = prefs.getUInt(KEY_HEAPC, 0);
      s_info.minHeap = prefs.getUInt(KEY_MINH, 0);
      s_info.millisAtCrash = prefs.getUInt(KEY_MILLI, 0);
      prefs.getBytes(KEY_TASK, s_info.task, sizeof(s_info.task));
      s_info.task[sizeof(s_info.task) - 1] = '\0';
      s_info.rtcValid = (s_info.heapAtCrash != 0 || s_info.task[0] != '\0');
      s_info.marks = 0;
    } else {
      s_info.reason = (uint8_t)reason;
      s_info.label = resetLabel(reason);
      s_info.heapAtBoot = heap;
    }
  }

  prefs.putUInt(KEY_BOOT, s_info.bootCount);
  prefs.putUInt(KEY_CRASH, s_info.crashCount);
  prefs.end();

  // Adopt whatever is already in NVS, so the first idle transition does not
  // rewrite a string that is already correct.
  strncpy(s_flashedPhase, s_info.phase, sizeof(s_flashedPhase) - 1);
  s_flashedPhase[sizeof(s_flashedPhase) - 1] = '\0';
  s_livePhase[0] = '\0';

  Serial.printf("[CRASH] boot=%u crash=%u reason=%s heap=%u lastphase='%s'\n",
                (unsigned)s_info.bootCount, (unsigned)s_info.crashCount,
                s_info.label, (unsigned)heap, s_info.phase);
  if (s_info.rtcValid) {
    // U-106: the RTC breadcrumb is the answer an empty lastphase never was -
    // WHICH operation and WHICH task, with the heap at that instant.
    Serial.printf("[CRASH] rtc: marks=%u phase='%s' task='%s' heap=%u minHeap=%u atMs=%u\n",
                  (unsigned)s_info.marks, s_info.phase, s_info.task,
                  (unsigned)s_info.heapAtCrash, (unsigned)s_info.minHeap,
                  (unsigned)s_info.millisAtCrash);
  }

  // U-106: arm the breadcrumb for THIS boot so the next crash has fresh evidence.
  // Magic is written LAST so a crash mid-init cannot be read as a valid crumb.
  s_rtc.magic = 0;
  s_rtc.seq = 0;
  s_rtc.millis = 0;
  s_rtc.heap = heap;
  s_rtc.minHeap = heap;
  strncpy(s_rtc.phase, "boot", sizeof(s_rtc.phase) - 1);
  s_rtc.phase[sizeof(s_rtc.phase) - 1] = '\0';
  crashRtcTaskName(s_rtc.task, sizeof(s_rtc.task));
  s_bootMinHeap = heap;
  s_rtc.magic = CRASH_RTC_MAGIC;
}

void crashLogPhase(const char *phase) {
  const char *src = (phase != nullptr) ? phase : "";
  // U-106: keep the flash-free RTC breadcrumb in step with the (rate-limited) NVS
  // phase so this call site also feeds crash-TIME evidence.
  crashRtcMark(src);
  char next[sizeof(s_livePhase)];
  next[0] = '\0';
  strncpy(next, src, sizeof(next) - 1);
  next[sizeof(next) - 1] = '\0';

  // Already in flash: nothing to record, but the RAM copy is refreshed below
  // only if it differs, so this is the common-case early out for a parked car.
  if (strcmp(next, s_flashedPhase) == 0) {
    if (strcmp(next, s_livePhase) != 0) strncpy(s_livePhase, next, sizeof(s_livePhase) - 1);
    return;
  }

  strncpy(s_livePhase, next, sizeof(s_livePhase) - 1);
  s_livePhase[sizeof(s_livePhase) - 1] = '\0';

  const uint32_t now = millis();
  // The floor applies to NON-EMPTY phases ONLY, and the distinction is the whole
  // point of the module rather than a detail. CrashLog.h promises that returning
  // to idle is always written, because a rate-limited idle transition leaves a
  // parked-car crash reporting whatever it last did: the record says "died
  // serving the root page" about a crash that had nothing to do with the page.
  // That is the one failure this module exists to prevent, so it is the one
  // write not allowed to be stale.
  //
  // It costs nothing, which is why the exemption is safe. This runs on a
  // TRANSITION, not on a poll: an idle car calls this every loop with "", and the
  // `strcmp(next, s_flashedPhase)` early-out above returns before flash on every
  // one of those passes. Only the pass that actually changes state gets here.
  //
  // The FIRST non-empty write of a session is also always allowed. Without that,
  // a crash during the first 30 s of a boot could never record a non-idle phase,
  // because millis() is small and the floor would swallow it.
  if (next[0] != '\0' && s_phaseWriteSeen && (now - s_lastPhaseWriteMs) < CRASHLOG_PHASE_MIN_MS) return;
  s_phaseWriteSeen = true;
  s_lastPhaseWriteMs = now;

  Preferences prefs;
  if (!prefs.begin(PREF_NS, false)) return;
  prefs.putBytes(KEY_PHASE, s_livePhase, sizeof(s_livePhase));
  prefs.end();
  strncpy(s_flashedPhase, s_livePhase, sizeof(s_flashedPhase) - 1);
  s_flashedPhase[sizeof(s_flashedPhase) - 1] = '\0';
}

const CrashInfo &crashLogInfo() { return s_info; }
const char *crashLogLivePhase() { return s_livePhase; }
uint32_t crashLogBootCount() { return s_info.bootCount; }
uint32_t crashLogCrashCount() { return s_info.crashCount; }
uint32_t crashLogMinHeap() { return s_bootMinHeap; }

// U-108 Phase 7 (owner 2026-10-13): the `FACTORY;RESET` fresh start. This is the
// ONLY writer that lowers the counters on purpose — normal operation only ever
// increments them. RAM is reset so the running session reports honestly, and NVS
// is cleared so the next boot reads a virgin tally (crashLogBegin() then yields
// boot=1, crash=0). The recorded crash record is dropped too: after a factory
// reset there is no "last unexpected boot" to attribute.
void crashLogResetCounters() {
  s_info.bootCount = 1;   // this boot is the first of the fresh start
  s_info.crashCount = 0;
  s_info.valid = false;
  s_info.reason = 0;
  s_info.label = resetLabel(ESP_RST_UNKNOWN);
  s_info.heapAtBoot = 0;
  s_info.phase[0] = '\0';
  s_info.rtcValid = false;
  s_info.heapAtCrash = 0;
  s_info.minHeap = 0;
  s_info.millisAtCrash = 0;
  s_info.marks = 0;
  s_info.task[0] = '\0';
  s_flashedPhase[0] = '\0';

  Preferences prefs;
  if (prefs.begin(PREF_NS, false)) {
    prefs.putUInt(KEY_BOOT, s_info.bootCount);
    prefs.putUInt(KEY_CRASH, s_info.crashCount);
    prefs.remove(KEY_REASON);
    prefs.remove(KEY_HEAP);
    prefs.remove(KEY_PHASE);
    prefs.remove(KEY_HEAPC);
    prefs.remove(KEY_MINH);
    prefs.remove(KEY_MILLI);
    prefs.remove(KEY_TASK);
    prefs.end();
  }
  Serial.println("[CRASH] counters reset by FACTORY;RESET (boot=1 crash=0)");
}
