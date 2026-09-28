// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: ParamsStore.cpp — boot-state persistence (last mode + speed, T-13)
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include "ParamsStore.h"
#include <Preferences.h>

static const char *PREF_NS = "botcfg";
static const char *KEY_MODE = "mode";
static const char *KEY_SPD  = "spd";

bool loadBootState(uint8_t &mode, uint16_t &speed) {
  Preferences prefs;
  if (!prefs.begin(PREF_NS, true)) return false;
  mode = prefs.getUChar(KEY_MODE, 0xFF);
  speed = prefs.getUShort(KEY_SPD, 0xFFFF);
  prefs.end();
  if (mode == 0xFF || speed == 0xFFFF) return false;
  return true;
}

bool saveBootState(uint8_t mode, uint16_t speed) {
  Preferences prefs;
  if (!prefs.begin(PREF_NS, false)) return false;
  prefs.putUChar(KEY_MODE, mode);
  prefs.putUShort(KEY_SPD, speed);
  prefs.end();
  return true;
}