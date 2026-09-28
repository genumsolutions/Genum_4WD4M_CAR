#pragma once
// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: ParamsStore.h — boot-state persistence (last mode + speed, T-13)
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include <stdint.h>

// Boot-state persistence: last mode + speed (T-13), kept in sync with the
// remote/app. Both live under lightweight keys in the SAME "botcfg"
// Preferences namespace as WIFICFG provisioning (wifi_ssid / wifi_pass) —
// the mode/spd keys do not collide. Mirrors the Genum_2WD1M_CAR /
// Genum_SELF_BALANCE_CAR reference (ParamsStore), trimmed to just the
// boot state (no PID params on this car).
// Only usable from setup() onwards: NVS (Preferences) is initialized by the
// Arduino core right before setup() — never at global-ctor time.
// Returns true if both values were found (mode 0..MODE_COUNT-1, speed 0..65535).
bool loadBootState(uint8_t &mode, uint16_t &speed);

// Persist the current mode + speed. Callers debounce (ModeManager keeps a
// 1 s debounce and flushes once per loop from run()).
bool saveBootState(uint8_t mode, uint16_t speed);