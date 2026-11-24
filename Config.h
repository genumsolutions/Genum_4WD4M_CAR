#pragma once
// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: Config.h — project-wide constants (boards, pins, timing, identity)
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================

// Include Arduino.h FIRST so portMUX_TYPE, portENTER_CRITICAL, etc. are available
// for any file that includes Config.h. This is the central config header.
#include <Arduino.h>

#include <stdint.h>
#include <cstddef>
#include <cstring>

// ---- T-31 project identity (single source; boot splash + docs) ----
#define FW_NAME    "4WD4M Car"
// The version VALUE lives here; the per-release NOTES live in TRACKS/CHANGELOG.md.
// They used to be one 3,247-character line on this define — 34% of the file —
// which made Config.h unreadable and ungreppable. Nothing is lost: the history
// was extracted verbatim, including two entries that were already truncated
// mid-sentence in the original.
// U-133 (2026-11-24): 1.3.19 -> 1.3.20. Owner asked for a version upgrade with
// every build so a flash is identifiable from the app and /status. Note the U-98
// ruling: the FIRST boot after this flash is a version change, so
// wipeRoutersOnFreshFirmware() clears the saved routers and transport toggles by
// design — re-add the router on the bench (RUNSHEET Step 5).
#define FW_VERSION "1.3.20"  // see TRACKS/CHANGELOG.md
#define FW_VENDOR  "GENUM SOLUTIONS PVT. LTD."
// U-132 (2026-11-23): the canonical project id on the wire. The app groups
// devices by this id, never by a name: `FW_NAME`/`BT_SPP_NAME` are free text and
// have already been renamed once per fleet, while this value is the key that
// survives a rename. Transport mirrors: /status `"project"` and STATE
// `;PROJ=` — both already parsed by the app (`carProtocol.ts`). Lowercase, no
// spaces; the app normalises to alphanumerics ("4wd4mcar" -> "4wd4m").
#define FW_PROJECT "4wd4mcar"

// ---- T66 (round-9) · U-114: the telemetry-parameter config surface ----
// The car firmware and the TSH gate BOTH read this one line. Every scalar
// product of `tele.parameters` must live here — no other place in this repo
// re-ships the same `frequency` / shaping values; that surface is the only
// source of truth (TSH/test_suites/test_config_integrity.py fails otherwise).
//
// THE SURFACE IS ONE STRING LITERAL, and that is load-bearing in BOTH
// directions (U-114):
//   - the TSH gate reads this file as TEXT and needs the CSV row
//       tele.parameters,frequency,<Hz>,<flag-key>,<shaping>
//     with `frequency` AND the flag key present, and that flag key occurring
//     EXACTLY ONCE across every .cpp/.h/.ino in the repo. The gate counts that
//     key, so this comment deliberately does not spell it out a second time.
//   - WebServerComm::buildStatusJson assigns the same macro to a `const char*`,
//     so it MUST be a quoted literal. The bare-token form compiled to
//     `const char* x = <first token>, …` — an undeclared identifier — and the
//     sketch did not build at all (check-firmware-syntax.ps1 caught it at HEAD).
//
// Fields, counted from 0 by WebServerComm's teleSurfaceField():
//   0 <surface> | 1 frequency | 2 <Hz> | 3 <flag-key> | 4 <shaping>
//   frequency — update cadence the tele loop runs at (Hz).
//   shaping   — 1 = single-slot model (faster, one product feed), 0 = dual-slot
//               model (two independent feeds). Set it once here; the tele loop
//               branches on this value.
#define CONFIG_TELEMETRY_PARAMETERS   "tele.parameters,frequency,1600,vysexcvr,0"

// T-xx / U-107: the advertised Bluetooth SPP name, ONE source.
// BluetoothComm::begin() passes it to SerialBT.begin(); the OLED's Bluetooth
// frame shows it (the "real BT name", not a second hard-coded literal that can
// drift from what the radio actually advertises). Keep in sync with CATALOG.md.
// U-110 (owner 2026-10-14): renamed "4WD CAR" -> the project identity
// "4WD4M Car", so the phone's pairing list, this OLED frame and the app's
// Bluetooth picker name the car the same thing (the owner could not match the
// "4WD CAR" the radio advertised to the "4WD4M Car" the app/OLED showed). The
// first boot after this change needs a fresh pairing: the phone keeps the old
// bond against the previous name.
#define BT_SPP_NAME "4WD4M Car"

// U-79: Transport power arbitration mutex - declared extern so it can be
// shared between .ino and transport classes (WebServerComm, BluetoothComm)
// for atomic state transitions during stop/begin/restart operations.
// Uses ESP32 Arduino's portMUX (via Arduino.h) for critical sections.
// The actual definition is in Genum_4WD4M_CAR.ino to avoid multiple definitions.
extern portMUX_TYPE transportPowerMux;

// U-79: Critical section macros for transport power arbitration.
// Uses ESP32 Arduino's portMUX via portENTER_CRITICAL/portEXIT_CRITICAL.
#define TRANSPORT_POWER_ENTER_CRITICAL() portENTER_CRITICAL(&transportPowerMux)
#define TRANSPORT_POWER_EXIT_CRITICAL() portEXIT_CRITICAL(&transportPowerMux)

// U-79: Safe string copy utility — guarantees null-termination and prevents
// buffer overflows. Replaces unsafe strncpy/strcpy calls throughout the codebase.
// Usage: safe_strcpy(dst, src, sizeof(dst));
inline void safe_strcpy(char* dst, const char* src, size_t dstSize) {
  if (!dst || dstSize == 0) return;
  if (!src) { dst[0] = '\0'; return; }
  size_t srcLen = strlen(src);
  size_t copyLen = (srcLen < dstSize - 1) ? srcLen : dstSize - 1;
  memcpy(dst, src, copyLen);
  dst[copyLen] = '\0';
}

// ---- T-15 (wireless) / R-9 (remote): serial diagnostics gating ----
// FW_LOG_LEVEL: 1 = warnings/errors only (production default); 2 = verbose
// diagnostics (heap dumps, the per-second free-heap print, provisioning and
// DBG confirmations). Level-2 logs are COMPILED OUT (#if, not if) so their
// format strings don't consume flash — the gate doubles as flash headroom.
#define FW_LOG_LEVEL 1

#if FW_LOG_LEVEL >= 2
  #define FW_LOG_VERBOSE(...) Serial.printf(__VA_ARGS__)
#else
  #define FW_LOG_VERBOSE(...) do {} while (0)
#endif
#define FW_LOG(...) Serial.printf(__VA_ARGS__)

// ===== Board / hardware selection =====
enum class BoardType : uint8_t { ESP32 };
enum class MotorDriver : uint8_t { L298N };
enum class CommType : uint8_t { BLUETOOTH, WIFI, WEBSERVER, WEBCLIENT };
enum class OledSize : uint8_t { OLED_1_3 };
enum class SwitchType : uint8_t { MOMENTARY, NORMAL };

constexpr BoardType BOARD_TYPE = BoardType::ESP32;
constexpr MotorDriver MOTOR_DRIVER = MotorDriver::L298N;
constexpr CommType COMM_TYPE = CommType::BLUETOOTH;
constexpr OledSize OLED_SIZE = OledSize::OLED_1_3;
constexpr SwitchType SWITCH_TYPE = SwitchType::MOMENTARY;

// ===== PWM / timeouts =====
// 8-bit PWM for the motor driver (LEDC high-speed timer 0). The driver speaks to
// the IDF LEDC API (driver/ledc.h) directly, which exists on both core 3.x and
// 4.x - the Arduino ledcSetup()/ledcWrite() wrappers are not used anywhere.
constexpr uint32_t PWM_FREQ = 20000u;
constexpr uint8_t PWM_RESOLUTION = 8u;
constexpr uint32_t FAILSAFE_TIMEOUT_MS = 2000u;

// ---- U-132: motor soft-start and reversal dead-time ----
// An L298N takes the full battery rail through a stalled motor when the
// direction pins and the full PWM duty land in the same instruction. That
// current step is what the brownout reports came from, so drive() now ramps the
// duty up over MOTOR_SOFTSTART_MS and, on a direction change, drops to zero and
// waits MOTOR_REVERSAL_DEADTIME_MS before the opposite pair is energised. Both
// are ticked from L298NControl::update() — ModeManager::run() calls it every
// pass — never by delay(), so the command path and the watchdog stay
// responsive. STOP never ramps: a stop that eases off is a runaway.
constexpr uint32_t MOTOR_SOFTSTART_MS = 250u;
constexpr uint32_t MOTOR_REVERSAL_DEADTIME_MS = 60u;

// ---- U-133: the SPD floor must move the car ----
// MIN_SPEED used to map to duty 0 (the mapping subtracted the floor from both
// ends), so the app's slowest speed was a stop and its default was ~45% of the
// rail. The floor now delivers this percentage of full duty and the rest of the
// range climbs linearly to 100% (L298NControl::mapSpeedToDuty). 25% is above the
// free-running threshold of the geared motors on 6-12 V and still well under the
// stall current the soft-start exists to limit.
constexpr uint32_t MOTOR_MIN_DUTY_PERCENT = 25u;

// ===== P1b/T-7b · BT hard-restart watchdog (2WD1M car parity) =====
// The BT stack is re-initialized (BluetoothComm::restart(), throttled to once
// per 60 s) after the link has been disconnected longer than this.
constexpr unsigned long BT_HARD_RESTART_MS = 120000u;

// ===== P1b/T-7c · power/heap guards (2WD1M car parity) =====
// Never drive with less free heap than this (emergency motor stop).
constexpr int HEAP_CRITICAL_BYTES = 4000;
// Optional low-voltage motor stop. Requires a battery-divider on an ADC pin:
// uncomment BOTH when the hardware has one (this car's wiring documents no
// divider, so the block in the .ino compiles out — same as the 2WD1M car).
// #define VOLTAGE_MONITOR_PIN 34
// constexpr float VOLTAGE_CRITICAL_V = 6.6f;

// ===== P4 (issue 6, 2026-10-15) · the E1 heap waterfall ====================
// Issue 6 (BT ON collapses free heap ~110 KB -> ~10 KB; BT+Wi-Fi below 14 KB
// resets the car). The diet's first step — a classic-BT-only bring-up, so the
// BLE controller/host memory this SPP-only car never uses is released — now
// lives in BluetoothComm::begin() (SerialBT.begin(name, false, true)). The
// firmware ALSO prints ONE breadcrumb per radio lifecycle stage, so whatever BT
// cost remains is measured, not assumed: the six tokens below are the wire
// contract that the E1 capture (TRACKS/RUNSHEET.md) and
// tools/check-firmware-contract.mjs both key on; they appear once each, in this
// order: boot -> bt-stack -> spp-connected -> wifi-ap -> page-served ->
// both-links. `max` is printed BESIDE `free` on purpose: the T-7b BT-restart
// watchdog can fragment the heap (the largest contiguous block shrinks) without
// moving the TOTAL, and free heap alone cannot see that (P4 prime suspect (c)).
// Cost is six prints at boot/connect - nothing on the per-loop path.
#define HEAPW_STAGE_BOOT  "boot"
#define HEAPW_STAGE_BT    "bt-stack"
#define HEAPW_STAGE_SPP   "spp-connected"
#define HEAPW_STAGE_AP    "wifi-ap"
#define HEAPW_STAGE_PAGE  "page-served"
#define HEAPW_STAGE_BOTH  "both-links"

// `stage` is always one of the six literals above (a static string, never
// caller memory), so it is safe to print directly. One Serial.printf per call
// - the same call FW_LOG expands to - so the breadcrumb lands on the same
// diagnostic line as everything else.
inline void heapWaterfall(const char *stage) {
  Serial.printf("[HEAPW] %s free=%u max=%u\n", stage,
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getMaxAllocHeap()));
}

// ===== Speed limits (match app/remote range 100–255) =====
constexpr uint16_t MIN_SPEED = 100u;
constexpr uint16_t MAX_SPEED = 255u;

// ===== Authoritative state broadcast (remote sync) =====
// Periodically announce STATE;MODE=..;SPD=..;STATUS=.. so the ESP remote's
// mode/speed mirror stays fresh (must stay below the remote's
// TELEMETRY_FRESH_MS = 5000). Mirrors Genum_SELF_BALANCE_CAR.
// R-16/pronto (owner 2026-09-18 — "make every sync pronto"): the periodic
// STATE broadcast keeps remote + app mirrors (mode/speed/SSID/AP/IP) fresh.
// 4 s could leave the remote dashboard showing stale truth for seconds after
// a change; 1.5 s is still negligible bandwidth (≈2 × 60 B lines) and well
// within every link's budget. Event paths (mode change, wifi truth change,
// provisioning) send STATE immediately regardless of this tick.
constexpr uint32_t STATE_INTERVAL_MS = 1500u;

// ===== Switch pin and debounce =====
constexpr int SWITCH_PIN = 33;
constexpr unsigned long SWITCH_DEBOUNCE_MS = 50u;

// ===== WiFi credentials =====
// T-64 (round-9): compile-time creds are NO LONGER joined as a fallback. The
// RUNTIME source of truth is Preferences (namespace "botcfg", keys
// "wifi_ssid"/"wifi_pass"), written by the app/website (ROUTERS/WIFICFG).
// With nothing stored, the car's OWN network (WIFI_AP_NAME below) simply IS
// the network — join it directly instead of a hidden "Esp" join.
// secrets.h is still included only to keep the legacy wiring compiling; its
// values are never fed to WiFi.begin() anymore.
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #warning "secrets.h not found. Create secrets.h with WIFI_SSID and WIFI_PASSWORD."
  // Placeholder fallback — real credentials belong in secrets.h (gitignored)
  // or can be provisioned at runtime from the app (WIFICFG over Bluetooth).
  constexpr char WIFI_SSID[] = "YOUR_WIFI_SSID";
  constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
#endif

// The car's OWN network (broadcast by the ESP32, never a saved router).
// T-56 (round-7): the name is exactly this — the old "ESP32_Car_<mac>"
// suffix is gone per owner (name is fixed, no MAC). T-66 (round-9): in ESP_SER
// this network is the DEFAULT + protected (it leads every saved-router list
// and can never be deleted/added/saved).
// U-105 (1.3.5, owner 2026-10-13): ONE own-AP identity. The old fleet carried
// both the donor's `WirelessCar_Wifi` and the earlier `4WDCar_Wifi`, which
// duplicated/confused the "car's own hotspot" row everywhere. The owner ruled:
// *"remove discrepancies and keep fallback default 4WD4MCar_WiFi only and
// remove that genum car and donor things completely."* This is the ONLY name
// in the system now (the app registry holds exactly this too).
constexpr char WIFI_AP_NAME[] = "4WD4MCar_WiFi";
// FIN-48: the own-hotspot gateway. One source for the OLED fallback line so
// the old "192.168.4.1" placeholder (a DIFFERENT subnet) can never appear on
// the display next to the real AP identity.
constexpr char WIFI_AP_IP[] = "192.168.245.1";
// How long beginStation() waits for a STORED router before the own AP is
// declared the network (the hotspot itself is up the whole time — T-65).
constexpr uint32_t WIFI_STA_TIMEOUT_MS = 8000u;

// U-121 Phase C: default SPP (`btEnabled_`) ON so the car's car-IP coupling
// path begins from a Bluetooth-first reachability posture and the app's
// Connectivity panel can pick the method up as a read-only fact.
constexpr bool DEFAULT_BT_ENABLED = true;

// U-121 Phase C: default Wi-Fi station (`wifiEnabled_`) ON so a phone on the
// car's own hotspot can raise the STA transport on demand, and the app's
// Connectivity panel can pick the method up as a read-only fact.
constexpr bool DEFAULT_WIFI_ENABLED = true;

// U-121 Phase C: the FREEZE question on the firmware side.
//
// The owner's report (BT ON, Wi-Fi drops, heap collapses to ~15 KB, the car
// still shows a link) is a heap-churn / reachability observation the app's
// silence detector (`isCarSilent`) can see but not reproduce by itself. The
// firmware half of that same question is: does the U-79 transport-power path
// even ATTEMPT a Wi-Fi STA raise while BT is freshly active? This flag is the
// only compile-time place that records the answer's shape: the shipped default
// poses the question and does NOT attempt the raise; the bench build for the
// question answers it by defining it true.
//
// Default is false for the released build. Do not flip for feature work. The
// only thing the flag controls is whether the U-79 loop is built with the
// BT-then-STA-raise attempt in scope; it never changes the shipped default
// transport posture (both transports default ON and the user raises what they
// want). Gate rule `bt-sta-from-bt-active` pins the default and the single
// definition site; the next records line in TRACKS/CHANGELOG.md carries one
// sentence on why the flag exists and what set it.
const bool BT_STA_FROM_BT_ACTIVE_FREEZE_QUESTION = false;

// ===== Motor / servo / trim pins =====
