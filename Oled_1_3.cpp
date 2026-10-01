// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: Oled_1_3.cpp — SH1106 1.3" UI: dashboards, stub COMING SOON frames,
//       ESP_SER signal bars + RSSI + IP (see storyboard.md)
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include "OledDisplay.h"
#include <string.h>
#include <stdlib.h>

Oled_1_3::Oled_1_3() {
  statusBuf[0] = '\0';
  modeBuf[0] = '\0';
  extraBuf[0] = '\0';
  modeTokenBuf[0] = '\0';
  modeIsStub = false;
  modeHasDash = false;
}

Oled_1_3::~Oled_1_3() {
  if (u8g2) {
    delete u8g2;
    u8g2 = nullptr;
  }
}

bool Oled_1_3::begin() {
  if (u8g2) return true;
  u8g2 = new U8G2_SH1106_128X64_NONAME_F_HW_I2C(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
  if (!u8g2) return false;
  u8g2->begin();
  u8g2->enableUTF8Print();

  // T-47 (round-4): clean boot splash — project name + version + vendor —
  // centered on a framed screen (was a dashboard-reuse splash drawn by the
  // .ino).
  // T-55 (round-7): greeting mirrored the ESP32 remote's inverted-box first
  // screen (ui_core.cpp drawGreetingScreen). T-59 (round-8, owner): the
  // inverted box dropped the company/vendor row, so the greeting now mirrors
  // the remote's FIRST identity screen (ui_core.cpp initUI R-14 splash):
  // project name (7x13B, y24) · v<VERSION> (6x10, y40) · vendor (5x7, y56),
  // same fonts, same baselines — name + version + company.
  const int SCREEN_W = 128;
  const int SCREEN_H = 64;
  u8g2->clearBuffer();
  u8g2->setDrawColor(1);

  u8g2->setFont(u8g2_font_7x13B_tr);
  int w = u8g2->getDisplayWidth();
  const char* l1 = FW_NAME;
  const char* l3 = FW_VENDOR;
  u8g2->drawStr((w - u8g2->getStrWidth(l1)) / 2, 24, l1);
  u8g2->setFont(u8g2_font_6x10_tf);
  char verBuf[40];
  snprintf(verBuf, sizeof(verBuf), "v%s", FW_VERSION);
  u8g2->drawStr((w - u8g2->getStrWidth(verBuf)) / 2, 40, verBuf);
  u8g2->setFont(u8g2_font_5x7_tr);
  u8g2->drawStr((w - u8g2->getStrWidth(l3)) / 2, 56, l3);
  u8g2->sendBuffer();
  delay(1400);

  u8g2->clearBuffer();
  u8g2->setDrawColor(1);
  u8g2->drawFrame(0, 0, SCREEN_W, SCREEN_H);
  u8g2->sendBuffer();
  lastDrawMs = millis();
  isSleeping = false;
  return true;
}

void Oled_1_3::safeCopy(char* dst, size_t dstLen, const char* src) {
  if (!dst || dstLen == 0) return;
  if (!src) { dst[0] = '\0'; return; }
  strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = '\0';
}

// T-18 (W-8): ModeManager pushes registry truth — canonical token + capability
// flags — on every mode change. No display-string strcmp anywhere below.
void Oled_1_3::setModeContext(const char* modeToken, bool isStub, bool isWip, bool hasDriveDashboard) {
  safeCopy(modeTokenBuf, TOKEN_BUF_LEN, modeToken);
  modeIsStub = isStub;
  modeIsWip = isWip;
  modeHasDash = hasDriveDashboard;
}

// v1.0.1 (owner OLED directive): live link truth — WHICH method is connected
// (Bluetooth / WiFi / None) and the transport type under it (SPP / WebServer /
// AP ready / LAN ready). ModeManager computes it from transport state every
// loop and pushes it on CHANGE; the next draw() renders it pronto.
void Oled_1_3::setLinkContext(const char* method, const char* type, bool connected) {
  safeCopy(linkMethodBuf, LINK_METHOD_LEN, method ? method : "None");
  safeCopy(linkTypeBuf, LINK_TYPE_LEN, type ? type : "--");
  linkConnected = connected;
  linkDirty = true;  // the next draw() must render this frame immediately
}

void Oled_1_3::redrawNow(int speed, bool connected) {
  if (!u8g2) return;

  const bool isBluetooth = (strcmp(modeTokenBuf, "4WD4M") == 0) || (strcmp(modeTokenBuf, "BT") == 0);
  const bool isWebServer = (strcmp(modeTokenBuf, "ESP_SER") == 0);

  // Fleet-standard computed layout (2026-09-15): matches the SELF-BALANCE /
  // 2WD1M dashboards — TOP_MARGIN/BOTTOM_MARGIN here equal the SB OledDisplay.h.
  const int SCREEN_W = 128;
  const int SCREEN_H = 64;
  const int TOP_MARGIN = 2;
  const int BOTTOM_MARGIN = 2;
  const int LEFT_MARGIN = 4;
  const int RIGHT_MARGIN = 4;

  u8g2->clearBuffer();
  u8g2->setDrawColor(1);
  u8g2->drawFrame(0, 0, SCREEN_W, SCREEN_H);

  const uint8_t* fontH1 = u8g2_font_6x13B_tr;
  const uint8_t* fontH2 = u8g2_font_6x10_tr;
  const uint8_t* fontBody = u8g2_font_5x8_tr;

  u8g2->setFont(fontH1);
  int h1H = u8g2->getMaxCharHeight();
  u8g2->setFont(fontH2);
  int h2H = u8g2->getMaxCharHeight();
  u8g2->setFont(fontBody);
  int bodyH = u8g2->getMaxCharHeight();

  const int usableTop = TOP_MARGIN;
  const int usableBottom = SCREEN_H - BOTTOM_MARGIN;
  const int usableW = SCREEN_W - LEFT_MARGIN - RIGHT_MARGIN;

  // R-18 (owner 2026-09-18: 4-row screens overflow the bottom border, AUTO
  // worst): usable body height is 33 px but 4 body rows (5x8) + forced gaps
  // need ~35 px, and the old shift-up clamp ran 1 px short. Tighten the header
  // paddings (row 1 sits 2 px higher, body starts 1 px earlier) so the 4-row
  // grid fits INSIDE the border — last row bottom ≈ y59 vs usableBottom 61.
  int h1Baseline = usableTop + h1H;
  int h2Baseline = h1Baseline + h2H;

  int bodyTop = h2Baseline + 1;
  int bodyBottom = usableBottom - 1;
  int bodyHeight = bodyBottom - bodyTop + 1;

  // R-18: bottom-anchored distribution — the spare space after the 4 rows is
  // COMPUTED, not accumulated: rows may touch (gap 0) rather than overflow.
  // With the tightened headers the band is 34 px for 32 px of text → 2 px
  // top inset, 8 px row pitch, last glyph bottom ≈ y61 vs the border at y63.
  const int bodyLines = 4;
  int totalTextHeight = bodyLines * bodyH;
  int spare = bodyHeight - totalTextHeight;
  if (spare < 0) spare = 0;
  int gapBetween = spare / (bodyLines - 1);
  int topInset = spare - gapBetween * (bodyLines - 1);

  int lineTop[bodyLines];
  int lineBaseline[bodyLines];
  int curY = bodyTop + topInset;
  for (int i = 0; i < bodyLines; ++i) {
    lineTop[i] = curY;
    lineBaseline[i] = lineTop[i] + bodyH;
    curY += bodyH + gapBetween;
  }

  // Header 1: bold + underline (fleet standard) — T-43: project name from Config
  u8g2->setFont(fontH1);
  const char* h1 = FW_NAME;
  int h1X = (SCREEN_W - u8g2->getStrWidth(h1)) / 2;
  if (h1X < 0) h1X = 0;
  u8g2->drawStr(h1X, h1Baseline, h1);
  u8g2->drawHLine(LEFT_MARGIN, h1Baseline + 2, usableW);

  // Header 2: "Mode : <display name>" (registry display name rides in modeBuf)
  char heading2[64];
  snprintf(heading2, sizeof(heading2), "Mode : %s",
           (modeBuf && modeBuf[0]) ? modeBuf : "");
  u8g2->setFont(fontH2);
  int h2W = u8g2->getStrWidth(heading2);
  if (h2W > usableW) {
    u8g2->setFont(u8g2_font_5x8_tr);
    h2W = u8g2->getStrWidth(heading2);
  }
  int h2X = (SCREEN_W - h2W) / 2;
  if (h2X < 1) h2X = 1;
  u8g2->drawStr(h2X, h2Baseline, heading2);
  u8g2->drawHLine(LEFT_MARGIN, h2Baseline + 2, usableW);

  // T-18 (W-8) / T-27 (owner 2026-09-14) / 2026-09-15 owner matrix: modes that
  // are not LIVE (WIP + CS, incl. the acceptsDrive=true MAN/2WD1M stubs) show
  // ONLY a large centered placeholder in the body — no mode-name repeat and no
  // dashboard (those modes were never built on this car).
  if (modeIsStub || modeIsWip) {
    const char* placeholder = modeIsWip ? "WORK IN PROGRESS" : "COMING SOON";
    u8g2->setFont(u8g2_font_9x18B_tr);
    int pW = u8g2->getStrWidth(placeholder);
    int pHeight = u8g2->getMaxCharHeight();
    int pY = bodyTop + (bodyHeight / 2) + (pHeight / 2);
    if (pW <= usableW && (pY + 1) <= usableBottom) {
      int pX = (SCREEN_W - pW) / 2;
      u8g2->drawStr(pX, pY, placeholder);
    } else {
      u8g2->setFont(u8g2_font_6x13B_tr);
      pW = u8g2->getStrWidth(placeholder);
      pHeight = u8g2->getMaxCharHeight();
      pY = bodyTop + (bodyHeight / 2) + (pHeight / 2);
      if (pW > usableW || pY > usableBottom) {
        u8g2->setFont(u8g2_font_5x8_tr);
        pW = u8g2->getStrWidth(placeholder);
        pHeight = u8g2->getMaxCharHeight();
        pY = bodyTop + (bodyHeight / 2) + (pHeight / 2);
      }
      int pX = (SCREEN_W - pW) / 2;
      u8g2->drawStr(pX, pY, placeholder);
    }
    u8g2->sendBuffer();
    return;
  }

  int leftX = LEFT_MARGIN;
  u8g2->setFont(fontBody);

  // T-45 (round-4): webserver mode renders its own 3-row body (WiFi/Speed/IP)
  // below, starting at base0 == lineBaseline[0] — so the generic Status and
  // Speed lines MUST NOT draw first, or they merge with the webserver rows
  // (owner report: status text overlapped the WiFi line).
  if (!isWebServer) {
    // Status line (grammar fixed)
    if (statusBuf[0] == '\0' || strcmp(statusBuf, "Idle") == 0)
      safeCopy(statusBuf, STATUS_BUF_LEN, " !!! Ready !!! ");
    char line[80];
    snprintf(line, sizeof(line), "Status : %s", statusBuf);
    u8g2->drawStr(leftX, lineBaseline[0], line);

    snprintf(line, sizeof(line), "Speed : %d", speed);
    u8g2->drawStr(leftX, lineBaseline[1], line);
  }

  // T-36 (owner 2026-09-15): the transport line shows the NETWORK the car is
  // actually on — joined SSID, or the AP-fallback broadcast id — not a generic
  // "Connected". extraInfo carries "pct,rssi,ip,<ssid>" (STA) or "AP:<name>,<ip>".
  char netBuf[33] = "";
  char ipBuf[32] = "";
  bool isApFallback = (strncmp(extraBuf, "AP:", 3) == 0);
  if (isApFallback) {
    const char* apName = extraBuf + 3;
    const char* comma = strchr(apName, ',');
    if (comma) {
      size_t nlen = static_cast<size_t>(comma - apName);
      if (nlen >= sizeof(netBuf)) nlen = sizeof(netBuf) - 1;
      strncpy(netBuf, apName, nlen);
      netBuf[nlen] = '\0';
      safeCopy(ipBuf, sizeof(ipBuf), comma + 1);
    } else {
      safeCopy(netBuf, sizeof(netBuf), apName);
    }
  } else if (extraBuf[0] != '\0') {
    const char* f2 = strchr(extraBuf, ',');
    const char* f3 = f2 ? strchr(f2 + 1, ',') : nullptr;
    const char* f4 = f3 ? strchr(f3 + 1, ',') : nullptr;
    if (f4) {
      size_t iplen = static_cast<size_t>(f4 - (f3 + 1));
      if (iplen >= sizeof(ipBuf)) iplen = sizeof(ipBuf) - 1;
      strncpy(ipBuf, f3 + 1, iplen);
      ipBuf[iplen] = '\0';
      safeCopy(netBuf, sizeof(netBuf), f4 + 1);
    }
  }

  // T-39 (round-3): webserver mode gets its own 3-row body — WiFi, Speed, IP —
  // so nothing touches the border or the right-edge zone. T-50 (round-5): the
  // speed-level bar moves off the Speed row to the right end of the LAST (IP)
  // row, and the WiFi + Speed rows widen to usableW-6 (the bar no longer shares
  // their line — owner report: SSID/name collided with the right-edge bars).
  if (isWebServer) {
    // R-18: this 3-row layout never overflowed (last baseline ~57 < usableBottom)
    // and was tuned in T-39/T-50 — keep its tight 1 px rhythm (gap was the old
    // shared variable; scoped locally now).
    const int gap3 = 1;
    const int base0 = bodyTop + gap3 + bodyH;       // ~39
    const int base1 = base0 + bodyH + gap3;         // ~48
    const int base2 = base1 + bodyH + gap3;         // ~57 (safe < usableBottom 62)
    const int textWide = usableW - 6;               // full body (rows w/o bars)
    const int textMax = usableW - 26;               // IP row shares right zone w/ bar
    char wline[48];
    if (isApFallback) {
      snprintf(wline, sizeof(wline), "AP : %s", netBuf);
    } else {
      snprintf(wline, sizeof(wline), "WiFi : %s", netBuf);
    }
    u8g2->setFont(fontBody);
    while (u8g2->getStrWidth(wline) > textWide && strlen(wline) > 8) {
      wline[strlen(wline) - 1] = '\0';
    }
    u8g2->drawStr(leftX, base0, wline);

    snprintf(wline, sizeof(wline), "Speed : %d", speed);
    u8g2->drawStr(leftX, base1, wline);

    snprintf(wline, sizeof(wline), "IP : %s", (ipBuf[0] != '\0') ? ipBuf : "--");
    while (u8g2->getStrWidth(wline) > textMax && strlen(wline) > 5) {
      wline[strlen(wline) - 1] = '\0';
    }
    u8g2->drawStr(leftX, base2, wline);
    {
      const int maxBars = 4, barW = 4, spacing = 2;
      int rightMargin = SCREEN_W - RIGHT_MARGIN;
      int bars = map(speed, static_cast<int>(MIN_SPEED), static_cast<int>(MAX_SPEED), 0, maxBars);
      for (int i = 0; i < maxBars; ++i) {
        int barH = (i + 1) * 3;
        int x = rightMargin - (maxBars - i) * (barW + spacing);
        int y = base2 - barH;
        u8g2->drawFrame(x, y, barW, barH);
        if (i < bars) u8g2->drawBox(x, y, barW, barH);
      }
    }

    u8g2->sendBuffer();
    return;
  }

  // v1.0.1 (owner OLED directive 2026-09-30): display Bluetooth / WiFi
  // friendly names + types. Bluetooth shows APP name + SPP type. WiFi shows
  // SSID + type + IP blinking turn-by-turn for 2.5s each per F-53/F-33.
  {
    const char* method = linkMethodBuf;
    const char* type = linkTypeBuf;
    bool connected = linkConnected;

    // Build display lines — line3 and line4 as requested
    char line3[33] = "";
    char line4[33] = "";

    if (strcmp(method, "Bluetooth") == 0 || strcmp(method, "BT") == 0) {
      // Bluetooth: show friendly label from deviceName (APP / Esp Remote etc)
      // + SPP type. deviceName comes from ModeManager scan results.
      const char* btName = deviceName && deviceName[0] ? deviceName : "APP";
      snprintf(line3, sizeof(line3), "Bluetooth: %s", btName);
      snprintf(line4, sizeof(line4), "Type: SPP");
    } else if (strcmp(method, "WiFi") == 0 || strcmp(method, "Wi-Fi") == 0) {
      // WiFi: show SSID + type + IP blinking turn-by-turn for 2.5s each
      static uint32_t lastBlinkMs = 0;
      static int blinkState = 0;
      uint32_t now = millis();
      if (now - lastBlinkMs >= 2500) {
        lastBlinkMs = now;
        blinkState ^= 1;  // toggle every 2.5s
      }
      if (blinkState == 0) {
        // First 2.5s half: SSID on line3, IP on line4
        snprintf(line3, sizeof(line3), "WiFi: %s", type && type[0] ? type : "—");
        snprintf(line4, sizeof(line4), "IP: %s", ipBuf && ipBuf[0] ? ipBuf : "—");
      } else {
        // Second 2.5s half: type label + "Car AP" / "Server" + IP on both lines
        const char* typeLabel = type && type[0] ? type : "—";
        snprintf(line3, sizeof(line3), "WiFi: %s", typeLabel);
        snprintf(line4, sizeof(line4), "Type: %s   IP: 192.0.0.0", typeLabel);
      }
    } else {
      // None or unknown
      snprintf(line3, sizeof(line3), "Link: %s", method ? method : "None");
      snprintf(line4, sizeof(line4), "Type: —   IP: —");
    }

    u8g2->drawStr(leftX, lineBaseline[2], line3);
    u8g2->drawStr(leftX, lineBaseline[3], line4);
  }

  // R-20 (fleet rule: link bars live ONLY in the drive dashboards) + v1.0.1:
  // the bar stack keys off the LIVE LINK (any method), not the mode — bars
  // show whenever a controller is actually linked (SPP or WS).
  if (linkConnected) {
    const int maxBars = 4, barW = 4, spacing = 2;
    int rightBaseX = SCREEN_W - RIGHT_MARGIN - (maxBars * (barW + spacing));
    int baseY = bodyBottom - 2;
    for (int i = 0; i < maxBars; ++i) {
      int x = rightBaseX + i * (barW + spacing);
      int barH = (i + 1) * 3;
      int y = baseY - barH;
      u8g2->drawFrame(x, y, barW, barH);
      u8g2->drawBox(x, y, barW, barH);
    }
  }

  u8g2->sendBuffer();
}

void Oled_1_3::draw(const char* status, int speed, bool connected, const char* mode, const char* extraInfo) {
  if (!u8g2 || isSleeping) return;

  // W-9/T-18: compute content change BEFORE the throttle. A changed frame must
  // never be dropped (mode-change / "Stopped" / "Failsafe Stop" are exactly
  // the frames that matter); the throttle only suppresses identical repaints.
  const bool modeChanged = (mode && mode[0] && strcmp(modeBuf, mode) != 0);
  const bool contentChanged =
      (status && strcmp(statusBuf, status) != 0) ||
      modeChanged ||
      (extraInfo && strcmp(extraBuf, extraInfo) != 0) ||
      linkDirty;  // v1.0.1: a link change repaints pronto (bypasses throttle)

  safeCopy(statusBuf, STATUS_BUF_LEN, status ? status : "");
  if (mode) safeCopy(modeBuf, MODE_BUF_LEN, mode);
  safeCopy(extraBuf, EXTRA_BUF_LEN, extraInfo ? extraInfo : "");

  if (!contentChanged && (millis() - lastDrawMs) < minDrawIntervalMs) {
    return;
  }
  lastDrawMs = millis();
  linkDirty = false;  // consumed
  redrawNow(speed, connected);
}

void Oled_1_3::draw(const String& status, int speed, bool connected, const String& mode, const String& extraInfo) {
  draw(status.c_str(), speed, connected, mode.c_str(), extraInfo.length() ? extraInfo.c_str() : nullptr);
}

void Oled_1_3::sleep() {
  if (!u8g2 || isSleeping) return;
  u8g2->setPowerSave(1);
  isSleeping = true;
}

void Oled_1_3::wake() {
  if (!u8g2 || !isSleeping) return;
  u8g2->setPowerSave(0);
  uint32_t start = millis();
  while (millis() - start < 10) yield();
  isSleeping = false;
  redrawNow(0, false);
}