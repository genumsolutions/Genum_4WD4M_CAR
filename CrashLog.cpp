// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: CrashLog.cpp — persisted record of why the car last rebooted
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include "CrashLog.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <string.h>

static const char *PREF_NS = "botcfg";
static const char *KEY_BOOT   = "bcount";  // total boots
static const char *KEY_CRASH  = "ccount";  // unexpected boots only
static const char *KEY_REASON = "lreason"; // last unexpected reset reason
static const char *KEY_HEAP   = "lheap";   // free heap at that boot
static const char *KEY_PHASE  = "lphase";  // phase during that boot

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

void crashLogBegin() {
  const esp_reset_reason_t reason = esp_reset_reason();

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
    prefs.putUInt(KEY_REASON, s_info.reason);
    prefs.putUInt(KEY_HEAP, heap);
    prefs.putBytes(KEY_PHASE, s_info.phase, sizeof(s_info.phase));
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
}

void crashLogPhase(const char *phase) {
  const char *src = (phase != nullptr) ? phase : "";
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
  // The FIRST write of a session is always allowed. Without this, a crash
  // during the first 30 s of a boot could never record a non-idle phase,
  // because millis() is small and the floor would swallow it.
  if (s_phaseWriteSeen && (now - s_lastPhaseWriteMs) < CRASHLOG_PHASE_MIN_MS) return;
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
