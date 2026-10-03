#pragma once
// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: Config.h — project-wide constants (boards, pins, timing, identity)
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================

// ---- T-31 project identity (single source; boot splash + docs) ----
#define FW_NAME    "4WD4M Car"
#define FW_VERSION "1.0.2"  // v1.0.2 (U-77): task-watchdog reboots eliminated — WS stalled-client heartbeat, total-bounded root-page write, per-write HTTP send cap, mDNS success/failure condition un-inverted (mDNS could never advertise), retry-branch breadcrumbs. Baseline was v1.0.0 (fresh repo from Genum_WIRELESS_CAR 1.8.4 @ d0bf8c0; renamed identity: BT "4WD CAR" / AP 4WDCar_Wifi / AP 192.168.245.1/24)
#define FW_VENDOR  "GENUM SOLUTIONS PVT. LTD."

#include <stdint.h>

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

// ===== Switch pin and debounce =====
constexpr int SWITCH_PIN = 33;
constexpr unsigned long SWITCH_DEBOUNCE_MS = 50u;

// ===== WiFi credentials =====
// T-64 (round-9): compile-time creds are NO LONGER joined as a fallback. The
// RUNTIME source of truth is Preferences (namespace "botcfg", keys
// "wifi_ssid"/"wifi_pass"), written by the app/website (ROUTERS/WIFICFG).
// With nothing stored, the car's OWN network (WIFI_AP_NAME below) simply IS
// the network in ESP_SER — join it directly instead of a hidden "Esp" join.
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
constexpr char WIFI_AP_NAME[] = "4WDCar_Wifi";
// How long beginStation() waits for a STORED router before the own AP is
// declared the network (the hotspot itself is up the whole time — T-65).
constexpr uint32_t WIFI_STA_TIMEOUT_MS = 8000u;