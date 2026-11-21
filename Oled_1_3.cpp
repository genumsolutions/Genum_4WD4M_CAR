// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: Oled_1_3.cpp — SH1106 1.3" UI: dashboards + stub COMING SOON frames.
//       (U-79 removed ESP_SER from the mode registry and with it the old
//       "signal bars + RSSI + IP" layout; redrawNow() now draws only the LIVE
//       dashboard / transport frames.)
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include "OledDisplay.h"
#include "CrashLog.h"     // U-95/F-79: restart reason in the header row
#include <string.h>
#include <stdlib.h>

Oled_1_3::Oled_1_3() {
  statusBuf[0] = '\0';
  modeBuf[0] = '\0';
  extraBuf[0] = '\0';
  modeTokenBuf[0] = '\0';
  modeIsStub = false;
}

Oled_1_3::~Oled_1_3() {
  if (u8g2) {
    delete u8g2;
    u8g2 = nullptr;
  }
}

bool Oled_1_3::begin() {
  if (u8g2) return true;
  // U-heap diet (2026-10-19): PAGE-BUFFER mode. The _F_ driver malloc'd a
  // 1024-byte full frame buffer for the whole uptime; _1_ keeps only one
  // 128-byte page and every frame is drawn through the standard u8g2
  // firstPage/nextPage loop below - same pixels, ~900 B less heap.
  u8g2 = new U8G2_SH1106_128X64_NONAME_1_HW_I2C(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
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
  u8g2->firstPage();
  do {
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
  } while (u8g2->nextPage());
  delay(1400);

  u8g2->firstPage();
  do {
  u8g2->drawFrame(0, 0, SCREEN_W, SCREEN_H);
  } while (u8g2->nextPage());
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
void Oled_1_3::setModeContext(const char* modeToken, bool isStub, bool isWip, bool /*hasDriveDashboard*/) {
  safeCopy(modeTokenBuf, TOKEN_BUF_LEN, modeToken);
  modeIsStub = isStub;
  modeIsWip = isWip;
}

// v1.0.1 (owner OLED directive): live link truth — WHICH method is connected
// (Bluetooth / WiFi / None) and the transport type under it (SPP / WebServer /
// AP ready / WiFi). ModeManager computes it from transport state every loop and
// pushes it on CHANGE; the next draw() renders it pronto. v1.2.3 renders the
// type only as the Bluetooth status line; the "LAN ready" wording is gone.
void Oled_1_3::setLinkContext(const char* method, const char* type, bool connected) {
  safeCopy(linkMethodBuf, LINK_METHOD_LEN, method ? method : "None");
  safeCopy(linkTypeBuf, LINK_TYPE_LEN, type ? type : "--");
  linkConnected = connected;
  linkDirty = true;  // the next draw() must render this frame immediately
}

// U-107 (owner 2026-10-13): the last-two-rows rule rides the U-105 transport
// TOGGLES. ModeManager pushes the live state on change; a change repaints pronto
// (consumes linkDirty, the same contract as setLinkContext()).
void Oled_1_3::setTransportToggles(bool wifiEnabled, bool btEnabled) {
  if (wifiEnabled == wifiOn && btEnabled == btOn) return;
  wifiOn = wifiEnabled;
  btOn = btEnabled;
  linkDirty = true;
}

// U-107c: exclusive mode — one connection active, other transport parked.
// When true, the OLED hides connection-method rows entirely.
void Oled_1_3::setExclusiveMode(bool exclusive) {
  if (exclusiveMode == exclusive) return;
  exclusiveMode = exclusive;
  linkDirty = true;
}

// REMOVED (2026-10-10): cycleNetworkDisplay() / setNetworkCycling() /
// setSavedRouterCount() implemented the U-98 3-second rotation through saved
// networks. Its only visible output was the "[n/m]" badge the owner asked to
// drop, and it ranked the car's own network against the routers instead of
// showing all of them equally. The state it read (networkCyclingActive,
// currentNetworkIndex, lastNetworkCycleMs) went with it, so nothing can
// re-enable it.

// U-107b (owner 2026-10-14): advance the rotation phases on the ~1 s clocks and
// report whether the frame that is VISIBLE right now would change. This is the
// piece that was missing: the phases used to advance ONLY inside redrawNow(),
// and redrawNow() only ran from draw(), which only ran on a content change. An
// idle car's status/extra rows are constant, so draw() stopped being called and
// the alternation FROZE on one frame — exactly the owner's report ("it showed
// the Wi-Fi info, then the Bluetooth info, then the Wi-Fi info again and stayed
// there"). tick() now drives it every loop.
bool Oled_1_3::advanceRotationPhases(uint32_t nowMs) {
  // U-107 (owner 2026-10-13): the last two rows follow the U-105 TRANSPORT
  // TOGGLES, not the idle/link state. Both ON -> alternate the Bluetooth frame
  // <=> the Wi-Fi frame; one ON -> that frame always; the U-105 clamp makes
  // "both OFF" impossible.
  const bool bothOn = wifiOn && btOn;
  const bool prevRotate = rotatePhase;

  if (bothOn) {
    if (nowMs - lastRotateMs >= ROTATE_INTERVAL_MS) {
      rotatePhase = !rotatePhase;
      lastRotateMs = nowMs;
    }
  } else {
    // A single transport is enabled: pin its frame, no alternation at all.
    rotatePhase = wifiOn;   // Wi-Fi only -> true; Bluetooth only -> false
    lastRotateMs = nowMs;
  }

  // A rotate flip always changes the visible frame. (U-110 removed the Wi-Fi
  // frame's name⇄IP alternation — that frame renders the name AND the IP at
  // once now — so a flip is the only thing that can change the visible frame.)
  return (rotatePhase != prevRotate);
}

void Oled_1_3::tick() {
  if (!u8g2 || isSleeping) return;
  if (advanceRotationPhases(millis())) {
    redrawNow(lastSpeed, lastConnected);
  }
}

void Oled_1_3::redrawNow(int speed, bool connected) {
  if (!u8g2) return;

  // `isBluetooth` was removed here: it was computed and never read (one
  // definition, zero uses repo-wide), costing two strcmp()s per OLED frame.
  // The donor's ESP_SER "signal bars + RSSI + IP" block is gone too — U-79
  // dropped that mode, so no SERVER-scoped frame is reachable and this function
  // renders the LIVE dashboard / transport frames only.

  // Rotation timer for idle state (both BT + WiFi AP advertising)
  uint32_t nowMs = millis();

  // Fleet-standard computed layout (2026-09-15): matches the SELF-BALANCE /
  // 2WD1M dashboards — TOP_MARGIN/BOTTOM_MARGIN here equal the SB OledDisplay.h.
  const int SCREEN_W = 128;
  const int SCREEN_H = 64;
  const int TOP_MARGIN = 2;
  const int BOTTOM_MARGIN = 2;
  const int LEFT_MARGIN = 4;
  const int RIGHT_MARGIN = 4;

  // Page-buffer mode: the WHOLE frame is drawn inside this loop - u8g2
  // re-runs it once per page (8x, ~128 B of pixels each). All rotation /
  // phase side effects live in tick() (U-107b), never inside this body,
  // so re-running the drawing is pure and safe.
  u8g2->firstPage();
  do {
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

  // Header 1: bold + underline (fleet standard) — T-43: project name from Config.
  // The project name is ALWAYS shown here. Crash/restart diagnostics are moved
  // to the car profile and app Diagnostics screen (U-95/F-79), not the header.
  u8g2->setFont(fontH1);
  const char* h1 = FW_NAME;
  int h1W = u8g2->getStrWidth(h1);
  if (h1W > usableW) {
    u8g2->setFont(fontBody);
    h1W = u8g2->getStrWidth(h1);
  }
  int h1X = (SCREEN_W - h1W) / 2;
  if (h1X < 1) h1X = 1;
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
  } else {

  int leftX = LEFT_MARGIN;
  u8g2->setFont(fontBody);

  // Status line (grammar fixed)
  if (statusBuf[0] == '\0' || strcmp(statusBuf, "Idle") == 0)
    safeCopy(statusBuf, STATUS_BUF_LEN, " !!! Ready !!! ");
  char line[80];
  snprintf(line, sizeof(line), "Status : %s", statusBuf);
  u8g2->drawStr(leftX, lineBaseline[0], line);

  snprintf(line, sizeof(line), "Speed : %d", speed);
  u8g2->drawStr(leftX, lineBaseline[1], line);
  // v1.2.3: the link bars moved HERE from the network rows (they used to
  // steal the right end of row 4, forcing the Bcast line to shave 26 px).
  // Filled whenever a controller is actually linked (SPP or WS); the Speed
  // text (≤ 50 px at 5x8) never reaches the bar zone (x ≥ 102).
  if (linkConnected) {
    const int maxBars = 4, barW = 4, spacing = 2;
    const int rightMargin = SCREEN_W - RIGHT_MARGIN;
    for (int i = 0; i < maxBars; ++i) {
      int barH = (i + 1) * 3;
      int x = rightMargin - (maxBars - i) * (barW + spacing);
      int y = lineBaseline[1] - barH;
      u8g2->drawFrame(x, y, barW, barH);
      u8g2->drawBox(x, y, barW, barH);
    }
  }

  // T-36 (owner 2026-09-15): the transport rows show the NETWORK the car is
  // actually on — joined SSID, or the AP-fallback broadcast id — not a generic
  // "Connected". extraInfo carries "pct,rssi,ip,<ssid>" (STA) or
  // "AP:<name>,<ip>". v1.2.4 (owner 2026-10-09): the IP comes from ipBuf so the
  // OLED matches the app's "WiFi · <ip>". U-110 (owner 2026-10-14): the Wi-Fi
  // frame shows the name on one row AND the IP on the other (both at once); the
  // directed broadcast stays on /status + the page's Bcast row.
  char netBuf[33] = "";
  char ipBuf[32] = "";
  bool isApFallback = (strncmp(extraBuf, "AP:", 3) == 0);
  // REMOVED (2026-10-10): the "|Nets:" router-count parse and the 3-second
  // rotation index it fed. Both existed only to render the "[n/m]" badge; the
  // badge is gone, so the parse had no reader and the index no writer. Saved
  // routers are shown on the page and in the app as an unordered set, which is
  // what "treat all router networks equally" asks for.
  if (isApFallback) {
    const char* apName = extraBuf + 3;
    const char* comma = strchr(extraBuf, ',');
    if (comma) {
      size_t nlen = static_cast<size_t>(comma - extraBuf - 3);
      if (nlen >= sizeof(netBuf)) nlen = sizeof(netBuf) - 1;
      strncpy(netBuf, extraBuf + 3, nlen);
      netBuf[nlen] = '\0';
      safeCopy(ipBuf, sizeof(ipBuf), comma + 1);
    } else {
      safeCopy(netBuf, sizeof(netBuf), extraBuf + 3);
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

  // Defensive: if a future format ever appends a "|key=value" tail, cut it so
  // netBuf (= joined SSID / AP id) and ipBuf (the interface IP) stay clean.
  { char* nPipe = strchr(netBuf, '|'); if (nPipe) *nPipe = '\0'; }
  { char* iPipe = strchr(ipBuf, '|');  if (iPipe) *iPipe = '\0'; }

  // U-107b: advance the two rotation phases. Factored out so `tick()` can drive
  // the alternation on its own clock even when nothing else changes (see the
  // tick() note in the header). Idempotent within an interval, so a content
  // redraw landing right after a tick does not double-step.
  advanceRotationPhases(nowMs);

  // Build the last two rows.
  char line3[40] = "";
  char line4[40] = "";
  const char* ssidText = (netBuf[0] != '\0') ? netBuf : WIFI_AP_NAME;
  const char* ipText = (ipBuf[0] != '\0') ? ipBuf : WIFI_AP_IP;

  // U-107c: when one connection is active (exclusive mode), hide connection-method
  // rows entirely. Only render when both transports are available (idle).
  if (!exclusiveMode) {
    if (rotatePhase) {
      // Wi-Fi frame (owner format 2026-10-14): the network name on the top row,
      // the car's own IP on the bottom. Both at once — no name⇄IP alternation.
      snprintf(line3, sizeof(line3), "WiFi : %s", ssidText);
      snprintf(line4, sizeof(line4), "IP : %s", ipText);
    } else {
      // Bluetooth frame (owner format 2026-10-14): the REAL advertised name
      // (BT_SPP_NAME, single-sourced in Config.h — not a second hard-coded literal
      // that can drift from the radio) plus the transport type.
      snprintf(line3, sizeof(line3), "Bluetooth : %s", BT_SPP_NAME);
      snprintf(line4, sizeof(line4), "Type : SPP");
    }

    // Always draw both lines (no blinking)
    u8g2->setFont(fontBody);
    const int linkTextWide = usableW;
    for (int i = 0; i < 2; ++i) {
      char* row = (i == 0) ? line3 : line4;
      while (u8g2->getStrWidth(row) > linkTextWide && strlen(row) > 5) {
        row[strlen(row) - 1] = '\0';
      }
    }
    u8g2->drawStr(leftX, lineBaseline[2], line3);
    u8g2->drawStr(leftX, lineBaseline[3], line4);
  }
  }
  } while (u8g2->nextPage());
}

void Oled_1_3::draw(const char* status, int speed, bool connected, const char* mode, const char* extraInfo) {
  crashRtcMark("oled");
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

  // U-107b: remember the live drive values so tick() can repaint the rotating
  // frames on its own clock without ModeManager re-supplying them.
  lastSpeed = speed;
  lastConnected = connected;

  if (!contentChanged && (millis() - lastDrawMs) < minDrawIntervalMs) {
    return;
  }
  lastDrawMs = millis();
  linkDirty = false;  // consumed
  redrawNow(speed, connected);
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