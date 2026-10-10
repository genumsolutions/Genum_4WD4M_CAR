#pragma once

// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: OledDisplay.h — SH1106 1.3" status screen driver (Oled_1_3)
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include <Arduino.h>
#include "Config.h"
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
  // v1.0.1 (owner OLED directive 2026-09-28): the dashboard shows WHICH link
  // method is connected (Bluetooth / WiFi / None) and the transport type under
  // it (SPP / WebServer / AP ready / WiFi / --), updated pronto from
  // live transport truth. v1.2.4 (2026-10-09): rows 3/4 render the METHOD +
  // network name and the car's CURRENT IP (the number the app shows); the
  // transport type is only surfaced as the Bluetooth status ("Bluetooth : SPP").
  // Default no-op keeps other displays source-compatible.
  virtual void setLinkContext(const char* /*method*/, const char* /*type*/, bool /*connected*/) {}
  // REMOVED (2026-10-10, owner: "the car's own hotspot name is displayed with
  // [1/2] ... please remove that and treat all router networks equally").
  // setNetworkCycling()/setSavedRouterCount()/cycleNetworkDisplay() drove a
  // 3-second rotation through saved networks that rendered as that badge, and
  // it ranked the car's own network against the rest. The whole mechanism is
  // gone - not just the format string - so no caller can re-enable it.
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
  void setLinkContext(const char* method, const char* type, bool connected) override;
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
  // v1.0.1: live link method + transport type (owner OLED directive).
  static constexpr size_t LINK_METHOD_LEN = 16;
  static constexpr size_t LINK_TYPE_LEN = 16;
  char linkMethodBuf[LINK_METHOD_LEN] = "None";
  char linkTypeBuf[LINK_TYPE_LEN] = "--";
  bool linkConnected = false;
  bool linkDirty = false;  // set by setLinkContext; consumed by the next draw()
  bool modeIsStub = false;           // registry avail != LIVE → "COMING SOON"
  bool modeIsWip = false;            // registry avail == WIP → "IN PROGRESS"

  uint32_t lastDrawMs = 0;
  uint32_t minDrawIntervalMs = 100;
  bool isSleeping = false;

  void safeCopy(char* dst, size_t dstLen, const char* src);
  void redrawNow(int speed, bool connected);
};