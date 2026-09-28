#pragma once

// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: OledDisplay.h — SH1106 1.3" status screen driver (Oled_1_3)
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include "Config.h"
#include <Arduino.h>
#include <U8g2lib.h>
#include <cstdint>
#include <stddef.h>

class OledDisplay {
public:
  virtual bool begin() = 0;
  virtual void draw(const char* status, int speed, bool connected, const char* mode, const char* extraInfo = nullptr) = 0;
  // T-18 (W-8): the OLED keys off the MODE REGISTRY, never off display
  // strings. ModeManager pushes the canonical token + capability-derived
  // flags on every mode change; layouts and the COMING SOON / IN PROGRESS
  // decisions read these (2026-09-15: WIP modes render an IN PROGRESS frame).
  // Default no-op keeps other display classes source-compatible.
  virtual void setModeContext(const char* /*modeToken*/, bool /*isStub*/, bool /*isWip*/, bool /*hasDriveDashboard*/) {}
  virtual void sleep() = 0;
  virtual void wake() = 0;
  virtual ~OledDisplay() = default;
};

class Oled_1_3 : public OledDisplay {
public:
  Oled_1_3();
  ~Oled_1_3() override;

  bool begin() override;
  void draw(const char* status, int speed, bool connected, const char* mode, const char* extraInfo = nullptr) override;
  void setModeContext(const char* modeToken, bool isStub, bool isWip, bool hasDriveDashboard) override;
  void sleep() override;
  void wake() override;

  void draw(const String& status, int speed, bool connected, const String& mode, const String& extraInfo = String());

private:
  U8G2_SH1106_128X64_NONAME_F_HW_I2C* u8g2 = nullptr;

  static constexpr size_t STATUS_BUF_LEN = 48;
  static constexpr size_t MODE_BUF_LEN = 24;
  static constexpr size_t EXTRA_BUF_LEN = 64;
  static constexpr size_t TOKEN_BUF_LEN = 12;

  char statusBuf[STATUS_BUF_LEN];
  char modeBuf[MODE_BUF_LEN];
  char extraBuf[EXTRA_BUF_LEN];
  char modeTokenBuf[TOKEN_BUF_LEN];  // canonical token (registry truth, T-18)
  bool modeIsStub = false;           // registry avail != LIVE → "COMING SOON"
  bool modeIsWip = false;            // registry avail == WIP → "IN PROGRESS"
  bool modeHasDash = false;          // registry drive mode → full status layout

  uint32_t lastDrawMs = 0;
  uint32_t minDrawIntervalMs = 100;
  bool isSleeping = false;

  void safeCopy(char* dst, size_t dstLen, const char* src);
  void redrawNow(int speed, bool connected);
};