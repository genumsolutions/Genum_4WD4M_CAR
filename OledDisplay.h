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
  // live transport truth. v1.2.4 (2026-10-09): the link rows render the METHOD +
  // network name and the car's CURRENT IP (the number the app shows); the
  // transport type is only surfaced as the Bluetooth status ("Bluetooth : SPP").
  // U-107 (2026-10-13): the last two rows are now driven by setTransportToggles()
  // below (the U-105 toggles), so this method supplies the live link truth they
  // annotate. Default no-op keeps other displays source-compatible.
  virtual void setLinkContext(const char* /*method*/, const char* /*type*/, bool /*connected*/) {}
  // U-107 (owner 2026-10-13): the last two rows follow the U-105 TRANSPORT
  // TOGGLES, not the link/idle state — both ON alternates the Bluetooth frame
  // ⇄ the Wi-Fi frame (~1 s); one ON shows that transport's frame always; and
  // the Wi-Fi frame's last line alternates the network name ⇄ the IP (~1 s).
  // ModeManager pushes the live toggle state; a change repaints pronto.
  // Default no-op keeps other display classes source-compatible.
  virtual void setTransportToggles(bool /*wifiEnabled*/, bool /*btEnabled*/) {}
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
  void setTransportToggles(bool wifiEnabled, bool btEnabled) override;
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

  // U-107 (owner 2026-10-13): the last-two-rows rule follows the U-105 transport
  // TOGGLES — both ON alternates the Bluetooth frame ⇄ the Wi-Fi frame every
  // ROTATE_INTERVAL_MS; one ON shows that frame always. (The U-105 clamp makes
  // "both OFF" impossible.)
  static constexpr uint32_t ROTATE_INTERVAL_MS = 1000;   // owner: ~1 s per frame
  uint32_t lastRotateMs = 0;
  bool rotatePhase = false;     // false = Bluetooth frame, true = Wi-Fi frame
  // U-107: the Wi-Fi frame's last line alternates the network name ⇄ the IP.
  static constexpr uint32_t WIFI_INFO_INTERVAL_MS = 1000;
  uint32_t lastWifiInfoMs = 0;
  bool wifiInfoPhase = false;   // false = network name, true = IP
  // U-107: the live U-105 toggles, pushed by ModeManager on change.
  bool wifiOn = true;
  bool btOn = true;

  void safeCopy(char* dst, size_t dstLen, const char* src);
  void redrawNow(int speed, bool connected);
};