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
#define FW_VERSION "1.3.8"  // see TRACKS/CHANGELOG.md
#define FW_VENDOR  "GENUM SOLUTIONS PVT. LTD."
// T-xx / U-107 (2026-10-13): the advertised Bluetooth SPP name, ONE source.
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
constexpr uint32_t PWM_FREQ = 20000u;
constexpr uint8_t PWM_RESOLUTION = 8u;
constexpr uint32_t FAILSAFE_TIMEOUT_MS = 2000u;

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

// ===== Internet method / cloud relay (U-94 Phase 2) =====
// The relay endpoint. NOT a secret: it is the public function URL of a Supabase
// project, it is already in the app's build, and the credential that protects
// it (the per-car token) is a separate thing that lives in NVS only.
// Leave RELAY_HOST empty to ship a car that does not dial out at all - that is
// the default, and it is also what a donor car must stay on.
#define RELAY_HOST ""          // e.g. "xyzcompany.supabase.co"
#define RELAY_PORT 443
#define RELAY_PATH "/functions/v1/car-relay"

// The relay's cap on what the CAR may send (maxBytesCarToController). It is 512
// because that is the size of the car's own STATE buffer (the capacity the car
// builds a worst-case STATE+REPLY into, 1.3.2+ — see ModeManager::sendStateOn);
// the phone direction is capped at 128 because that is handleCommand's buffer.
// The two directions are NOT the same number, and treating them as one number
// is what made every STATE line get dropped as "oversized" in U-94 Phase 1.
// U-R3 (1.3.2): grew 448 -> 512 when the STATE line added the restart record
// keys (RST/BC/CC/PH/LH) — a >448-char STATE was being dropped whole by the relay.
constexpr uint16_t RELAY_MAX_OUT_BYTES = 512;

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