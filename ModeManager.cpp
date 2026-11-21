// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: ModeManager.cpp — mode engine: handleCommand() single path (T-1),
//       registry gating, failsafe, STATE/CAP/NACK broadcast
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include "ModeManager.h"
#include "WebServerComm.h"
#include "ParamsStore.h"
#include "CrashLog.h"
#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include <ctype.h>
#include "TwoWD1M.h"

static uint32_t defaultNow() { return millis(); }

// 2WD1M-compat drive handler (D-2 approved; ported from the
// Genum_SELF_BALANCE_CAR reference where it is proven on hardware).
static TwoWD1M g_2wd1mHandler;

// ---- Mode registry (D-2; v2 fleet reference) ----
// token = canonical protocol token (MODE_CMDS[] parity with the ESP remote).
// Adding a future mode = new Mode enum value + one row here + (optional) a
// drive handler; dispatch, STATE emission and token mapping read from this
// table (kills the F-21/F-23 token-drift class structurally).
// Capability flags (T-17/T-19, W-13/W-10, owner matrix 2026-09-15):
//   acceptsDrive  = mode accepts F/B/L/R/S direction letters (driving mode)
//   avail         = LIVE (functional now) / WIP (in progress) / CS (coming soon).
//                   isStub()==true (WIP + CS) is announced via WS JSON
//                   "stub":true AND the CAPS;... map to all controllers, so
//                   clients mark the mode but still allow toggling to it (the
//                   car renders its own COMING SOON / IN PROGRESS frame).
// v2 ARCHITECTURE (owner ruling R-4, plan § Phase 1): the web layer is an
// ALWAYS-ON connection/setup channel, NOT a mode. ESP_SER (webserver) and
// ESP_CLI (webclient) are retired as drive modes → CS; 4WD4M is the single
// live drive mode and accepts drive over BOTH transports (Bluetooth via
// acceptsBtDrive, WebSocket/HTTP via the WIRE source — see handleCommand()).
const ModeManager::ModeEntry ModeManager::MODE_REGISTRY[ModeManager::MODE_COUNT] = {
  //                     token     display             sgn   auto  drive avail       btDrive
  { Mode::BT,      "4WD4M",   "4WD4M",            false, false, true,  ModeAvail::LIVE, true  },
  { Mode::PATH,    "PATH",    "PATH",             false, true,  false, ModeAvail::CS,   false },
  { Mode::OBS_US,  "OBS_US",  "OBSTACLE(US)",     false, true,  false, ModeAvail::CS,   false },
  { Mode::OBS_IR,  "OBS_IR",  "OBSTACLE(IR)",     false, true,  false, ModeAvail::CS,   false },
  { Mode::MANUAL,  "MAN",     "MANUAL (RF)",      false, false, true,  ModeAvail::CS,   false },
  { Mode::AUTO,    "AUTO",    "AUTONOMOUS",       false, true,  false, ModeAvail::CS,   true  },
  { Mode::M2WD1M,  "2WD1M",   "2WD1M",            true,  false, true,  ModeAvail::CS,   true  },
};

const ModeManager::ModeEntry& ModeManager::registryFor(Mode m) {
  for (int i = 0; i < MODE_COUNT; ++i) {
    if (MODE_REGISTRY[i].mode == m) return MODE_REGISTRY[i];
  }
  return MODE_REGISTRY[0];
}

const char* ModeManager::modeToCommandToken(Mode m) {
  return registryFor(m).token;
}

const char* ModeManager::getModeToken() const {
  return registryFor(currentMode_).token;
}

void ModeManager::buildCapsMap(char* buf, size_t bufLen) const {
  if (!buf || bufLen == 0) return;
  buf[0] = '\0';
  size_t used = 0;
  for (int i = 0; i < MODE_COUNT; ++i) {
    const ModeEntry& e = MODE_REGISTRY[i];
    const char* state = (e.avail == ModeAvail::LIVE) ? "LIVE"
                      : (e.avail == ModeAvail::WIP)  ? "WIP"
                                                     : "CS";
    int n = snprintf(buf + used, bufLen - used, "%s:%s;", e.token, state);
    if (n < 0 || static_cast<size_t>(n) >= bufLen - used) {
      buf[used] = '\0';  // truncated; keep the complete prefix
      return;
    }
    used += static_cast<size_t>(n);
  }
  if (used > 0) buf[used - 1] = '\0';  // drop trailing ';'
}

// Map internal status text to a whitelisted protocol status word for STATE
// lines and WS JSON (guide F-23: protocol consumers must never receive
// display noise; the app whitelist is carProtocol.ts ALLOWED_DRIVE_STATUSES).
static void whitelistedStatus(const char* raw, char* out, size_t outLen) {
  static const char* const kAllowed[] = { "Forward", "Backward", "Left", "Right", "Stopped" };
  if (raw && raw[0]) {
    for (const char* a : kAllowed) {
      if (strncasecmp(raw, a, strlen(a)) == 0) { snprintf(out, outLen, "%s", a); return; }
    }
    if (strncasecmp(raw, "Speed", 5) == 0) { snprintf(out, outLen, "Speed set"); return; }
  }
  // Failsafe Stop / Ready / Disconnected / mode change / stub screens are all
  // safe-stop truths on this car today.
  snprintf(out, outLen, "Stopped");
}

ModeManager::ModeManager(MotorControl* motor, CommInterface* primaryComm, OledDisplay* oled)
  : motor_(motor),
    comm_(primaryComm),
    auxComm_(nullptr),
    oled_(oled),
    currentMode_(Mode::BT),
    lastCommandTimeMs_(0),
    speed_(MIN_SPEED),
    failsafeActive_(false),
    failsafeStartMs_(0),
    readyDisplayed_(false),
    lastConnected_(false),
    lastOledMs_(0),
    lastStateMs_(0),
    systemCmdHook_(nullptr),
    pendingModeSpeedSave_(false),
    modeSpeedSaveMs_(0),
    nowFn_(defaultNow)
{
  statusText_[0] = '\0';
  extraInfo_[0] = '\0';
  lastOledStatus_[0] = '\0';
  pendingReply_[0] = '\0';
  linkContext_[0] = '\0';
  g_2wd1mHandler.reset();
}

// v1.0.1: compute the CURRENT live link.
// Precedence: an actively-linked controller wins, then server reachability.
ModeManager::LinkInfo ModeManager::currentLink() const {
  // Precedence: an actively-LINKED controller wins (WS client, then BT SPP);
  // mere reachability (joined router / own AP broadcasting) only shows when
  // no controller is linked. The panel must always tell the truth about
  // WHO is connected right now, pronto.
  const bool btUp = auxComm_ && auxComm_->connected();
  WebServerComm* ws = WebServerComm::instance();
  const bool wsClients = ws && ws->hasWsClients();
  const bool staJoined = ws && ws->isStaJoined();
  // M3 (owner 2026-10-13): with BOTH transport toggles enabled (U-105) the U-79
  // loop keeps BOTH transports up, so the CURRENT live link is BOTH at once —
  // reporting only the web client would be the old "panel lies when a second
  // transport is live" all over again. This branch outranks the single
  // transport states precisely because both are really connected.
  if (ws && ws->wifiEnabled() && ws->btEnabled() && wsClients && btUp) {
    return {"Parallel", "BT+WiFi", true};
  }
  if (wsClients) return {"WiFi", "WebServer", true};
  if (btUp) return {"Bluetooth", "SPP", true};
  if (staJoined) return {"WiFi", "WiFi", true};
  if (ws && (ws->apBroadcastId()[0] != '\0')) return {"None", "AP ready", false};
  return {"None", "--", false};
}

void ModeManager::pushLinkInfo() {
  LinkInfo li = currentLink();
  char ctx[LINK_BUF_LEN];
  snprintf(ctx, sizeof(ctx), "%s|%s", li.method, li.type);
  if (strcmp(ctx, linkContext_) == 0) return;  // unchanged - no repaint
  strncpy(linkContext_, ctx, LINK_BUF_LEN - 1);
  linkContext_[LINK_BUF_LEN - 1] = '\0';
  if (oled_) {
    oled_->setLinkContext(li.method, li.type, li.connected);
    // Pronto: a link change must repaint NOW, not wait for the next status
    // change. draw() consumes the OLED's linkDirty flag so this frame lands.
    oled_->draw(statusText_, speed_, isConnectedNow(), getModeString(), extraInfo_);
  }
}

// U-107 (owner 2026-10-13): the last-two-rows rule follows the U-105 transport
// toggles. WebServerComm owns them (`WIFICFG;WIFI|BT`); push them to the OLED on
// CHANGE so a toggle repaints the panel at once instead of waiting a tick.
void ModeManager::pushTransportToggles() {
  WebServerComm* ws = WebServerComm::instance();
  const bool w = ws ? ws->wifiEnabled() : true;
  const bool b = ws ? ws->btEnabled() : true;
  if (w == lastWifiOn_ && b == lastBtOn_) return;   // unchanged - no repaint
  lastWifiOn_ = w;
  lastBtOn_ = b;
  if (!oled_) return;
  oled_->setTransportToggles(w, b);
  // Pronto: setTransportToggles() set linkDirty, so this frame lands now.
  oled_->draw(statusText_, speed_, isConnectedNow(), getModeString(), extraInfo_);
}

// U-107c: push exclusive mode to OLED. When one connection is active and both
// transports are enabled (the other is parked), hide connection-method rows.
// Only when both are available (idle) do the BT/WiFi frames alternate.
void ModeManager::pushExclusiveMode() {
  WebServerComm* ws = WebServerComm::instance();
  const bool w = ws ? ws->wifiEnabled() : true;
  const bool b = ws ? ws->btEnabled() : true;
  const bool anyConnected = isConnectedNow();
  // Exclusive: a client is connected while both transports are ON (one parked).
  const bool exclusive = anyConnected && w && b;
  if (exclusive == lastExclusiveMode_) return;  // unchanged - no repaint
  lastExclusiveMode_ = exclusive;
  if (!oled_) return;
  oled_->setExclusiveMode(exclusive);
  // Repaint pronto so connection rows hide/show immediately.
  oled_->draw(statusText_, speed_, isConnectedNow(), getModeString(), extraInfo_);
}

void ModeManager::queueReply(const char* text) {
  if (!text || !text[0]) return;
  strncpy(pendingReply_, text, sizeof(pendingReply_) - 1);
  pendingReply_[sizeof(pendingReply_) - 1] = '\0';
}

ModeManager::~ModeManager() = default;

const char* ModeManager::modeToString(Mode m) const {
  return registryFor(m).displayName;  // registry-driven (D-2)
}

void ModeManager::setComm(CommInterface* newComm) {
  if (newComm) comm_ = newComm;
}

void ModeManager::setAuxComm(CommInterface* aux) {
  auxComm_ = aux;
}

void ModeManager::setExtraInfo(const char* info) {
  if (!info) {
    if (extraInfo_[0] == '\0') return;   // already blank - nothing to repaint
    extraInfo_[0] = '\0';
    if (oled_) oled_->draw(statusText_, speed_, isConnectedNow(), getModeString(), extraInfo_);
    return;
  }
  // U-72: THE OLED WAS NOT REPAINTED WHEN THE NETWORK CHANGED.
  //
  // `setExtraInfo()` used to copy the string and nothing else. The display was
  // repainted ONLY by unrelated events - sendState(), a command, a mode change -
  // so `updateExtraInfo()` (which runs every second and holds the real joined
  // SSID / IP) updated a buffer nobody looked at. The owner's report - *"the
  // oled shows the wrong wifi detail and is not sync with the car's state"* - is
  // exactly this: the data was correct and the picture was stale, which from the
  // bench is indistinguishable from the data being wrong.
  //
  // Repaint ONLY when the content actually changed (the guard is what keeps this
  // from repainting 1x/second forever), and repaint straight away rather than
  // waiting for the next unrelated event - which is what "pronto" requires.
  char next[EXTRA_INFO_LEN];
  strncpy(next, info, EXTRA_INFO_LEN - 1);
  next[EXTRA_INFO_LEN - 1] = '\0';
  if (strcmp(next, extraInfo_) == 0) return;
  strncpy(extraInfo_, next, EXTRA_INFO_LEN - 1);
  extraInfo_[EXTRA_INFO_LEN - 1] = '\0';
  if (oled_) oled_->draw(statusText_, speed_, isConnectedNow(), getModeString(), extraInfo_);
}

size_t ModeManager::getStatusText(char* buf, size_t bufLen) const {
  if (!buf || bufLen == 0) return 0;
  strncpy(buf, statusText_, bufLen - 1);
  buf[bufLen - 1] = '\0';
  return strlen(buf);
}

int ModeManager::getSpeed() const { return static_cast<int>(speed_); }
ModeManager::Mode ModeManager::getMode() const { return currentMode_; }
const char* ModeManager::getModeString() const { return modeToString(currentMode_); }
bool ModeManager::isConnectedNowPublic() const { return isConnectedNow(); }

void ModeManager::setMode(Mode newMode) {
  Mode prevMode = currentMode_;
  currentMode_ = newMode;
  if (motor_) motor_->drive(Action::STOP, 0);
  g_2wd1mHandler.reset();  // clear 2WD1M joystick state on mode entry

  failsafeActive_ = false;
  readyDisplayed_ = false;
  lastCommandTimeMs_ = nowFn_();

  // T-18 (W-8): push registry truth (token + capability flags) to the OLED so
  // layout decisions never depend on display strings (2026-09-15: isStub +
  // isWip so WIP modes render an IN-PROGRESS frame, not a live dashboard).
  if (oled_) {
    oled_->setModeContext(getModeToken(),
                          registryFor(currentMode_).isStub(),
                          registryFor(currentMode_).avail == ModeAvail::WIP,
                          registryFor(currentMode_).acceptsDrive);
  }

  // v2: NO transport swapping on mode change. comm_ stays the always-on web
  // transport and auxComm_ stays Bluetooth for the whole uptime (wired once
  // in setup()); both are polled every loop() and STATE fans out to both.
  // (Donor behaviour — `comm_ = auxComm_` outside SERVER — is retired.)

  bool conn = isConnectedNow();
  snprintf(statusText_, STATUS_TEXT_LEN, "mode changed.");
  // W-9/T-18: a mode change must ALWAYS produce one immediate OLED frame —
  // bypass the status-changed check and the draw throttle (W-3-style early
  // path). MANUAL entry rendered slowly on hardware because this draw was
  // skipped when the status text repeated.
  if (oled_) oled_->draw(statusText_, speed_, conn, modeToString(currentMode_), extraInfo_);
  strncpy(lastOledStatus_, statusText_, STATUS_TEXT_LEN - 1);
  lastOledStatus_[STATUS_TEXT_LEN - 1] = '\0';
  lastOledMs_ = nowFn_();

  // F-22: push authoritative state so remote/app chips update instantly.
  sendState();

  // T-41 (round-3): persist the mode IMMEDIATELY (no 1 s debounce) so a reboot
  // right after a switch restores the SAME mode — the owner's ESP_CLI revert
  // (2WD1M after a while) is a candidate stale-boot-restore. Speed persist
  // stays debounced (setSpeed). Level-1 serial log so the next flash session
  // can tell a reboot-restore from a never-delivered command.
  saveBootState(static_cast<uint8_t>(currentMode_), speed_);
  FW_LOG("[MODE] set %s -> %s (speed=%d, btFed=%d)\n",
         modeToString(prevMode), modeToString(currentMode_), static_cast<int>(speed_),
         auxComm_ ? (auxComm_->connected() ? 1 : 0) : -1);

  FW_LOG_VERBOSE("Free heap (Mode change): %d\n", static_cast<int>(ESP.getFreeHeap()));
}

void ModeManager::cycleMode(int delta) {
  int next = static_cast<int>(currentMode_) + delta;
  if (next >= MODE_COUNT) next = 0;
  if (next < 0) next = MODE_COUNT - 1;
  FW_LOG("[MODE] cycle button -> %s\n", modeToString(static_cast<Mode>(next)));
  setMode(static_cast<Mode>(next));
}

static void trimWhitespace(char* s) {
  if (!s) return;
  char* start = s;
  while (*start && isspace((unsigned char)*start)) start++;
  if (start != s) memmove(s, start, strlen(start) + 1);
  size_t len = strlen(s);
  while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
}

void ModeManager::handleCommand(const char* cmdC, CmdSource src) {
  if (!cmdC) return;
  char cmdBuf[128];
  strncpy(cmdBuf, cmdC, sizeof(cmdBuf) - 1);
  cmdBuf[sizeof(cmdBuf) - 1] = '\0';
  trimWhitespace(cmdBuf);
  if (cmdBuf[0] == '\0') return;

  // v1.4.0 provisioning: system commands (WIFICFG) run before every other
  // path — they must work in every mode and never collide with mode tokens.
  if (systemCmdHook_ && systemCmdHook_(cmdBuf)) {
    return;
  }

  if (strncmp(cmdBuf, "MODE_", 5) == 0) {
    setModeFromString(cmdBuf + 5);
    return;
  }

  // U-95 Phase 3: the mode-walk tokens. cycleMode() was reachable ONLY from
  // the GPIO switch (.ino) — no token existed, so the page and the app could
  // each have grown their own implementation and both would have drifted. One
  // hook, one walk, ±1 with wrap (MAN's registry token is
  // MAN — the walk visits it like any other row; stub rows stay gated by
  // setMode()'s own rules, unchanged here). Gate rule: `mode-cycle`.
  if (strcmp(cmdBuf, "MODECYCLE") == 0 || strcmp(cmdBuf, "MODENEXT") == 0) {
    cycleMode(1);
    return;
  }
  if (strcmp(cmdBuf, "MODEPREV") == 0) {
    cycleMode(-1);
    return;
  }

  // F-22: authoritative state request (remote sends this on connect,
  // app polls it every 2s).
  if (strncasecmp(cmdBuf, "REQ_STATE", 9) == 0) {
    sendState();
    return;
  }
  // T-49 (round-5): saved-networks request — sendState() broadcasts STATE +
  // CAPS + the NETW; names line on every connected transport.
  if (strncasecmp(cmdBuf, "REQ_NETW", 8) == 0) {
    sendState();
    return;
  }

  // W-13/T-17: a REJECTED command must NOT reset the failsafe clock, so the
  // idle-clock refresh happens only where the command is actually accepted.
  // Stop ('S') is always accepted (stopping is safe in every mode).
  const bool modeAcceptsDrive = registryFor(currentMode_).acceptsDrive;

  // D-2: 2WD1M-compat mode routes signed SPD/SERVO to the drive handler
  // BEFORE the absolute-speed parsing below (remote parity, see reference
  // Genum_SELF_BALANCE_CAR/2WD1M.cpp). v1.4.0: skipped in stub modes — a
  // coming-soon mode must not drive through the signed path either.
  if (registryFor(currentMode_).usesSignedSpd && !isCurrentModeStub()) {
    // T-29: Bluetooth transport drives 2WD1M only where the registry says so;
    // WIRE (WS/HTTP) is always allowed for a LIVE drive-capable mode.
    if ((src == CmdSource::WIRE || registryFor(currentMode_).acceptsBtDrive) &&
        g_2wd1mHandler.handleCommand(cmdBuf, motor_, speed_, this)) {
      lastCommandTimeMs_ = nowFn_();
      failsafeActive_ = false;
      readyDisplayed_ = false;
      return;
    }
  }

  if (cmdBuf[0] == '#') {
    // v1.4.0: '#<n>' speed set is refused in stub modes too.
    if (isCurrentModeStub()) {
      snprintf(statusText_, STATUS_TEXT_LEN, "COMING SOON");
      if (oled_) oled_->draw(statusText_, speed_, isConnectedNow(), modeToString(currentMode_), extraInfo_);
      return;
    }
    lastCommandTimeMs_ = nowFn_();
    failsafeActive_ = false;
    readyDisplayed_ = false;
    int val = atoi(cmdBuf + 1);
    if (val < static_cast<int>(MIN_SPEED)) val = MIN_SPEED;
    if (val > static_cast<int>(MAX_SPEED)) val = MAX_SPEED;
    speed_ = static_cast<uint16_t>(val);
    // T-13: persist the authoritative speed (debounced write).
    scheduleModeSpeedPersist();
    snprintf(statusText_, STATUS_TEXT_LEN, "Speed Update");
    uint32_t now = nowFn_();
    const unsigned long OLED_MIN_INTERVAL = 150;
    if (now - lastOledMs_ >= OLED_MIN_INTERVAL) {
      if (oled_ && strncmp(statusText_, lastOledStatus_, STATUS_TEXT_LEN) != 0) {
        oled_->draw(statusText_, speed_, isConnectedNow(), modeToString(currentMode_), extraInfo_);
        strncpy(lastOledStatus_, statusText_, STATUS_TEXT_LEN - 1);
        lastOledStatus_[STATUS_TEXT_LEN - 1] = '\0';
      }
      lastOledMs_ = now;
    }
    return;
  }

  if (strlen(cmdBuf) == 1) {
    char c = cmdBuf[0];
    if (c == 'F' || c == 'B' || c == 'L' || c == 'R' || c == 'S') {
      // W-13/T-17: drive letters are mode-scoped by registry declaration
      // (acceptsDrive). T-29 + 2026-09-15 (owner bug #1): the transport is
      // source-gated, NOT the mode: BLUETOOTH may drive only rows with
      // acceptsBtDrive (4WD4M — MAN is RF-transmitter-only); WIRE (WS/HTTP)
      // may drive any LIVE drive-capable mode, which fixed ESP_SER driving
      // over neither channel in v1.5.0. `S` (stop) is ALWAYS accepted —
      // stopping is safe in every mode. Rejected commands announce on the
      // OLED and do NOT reset the failsafe.
      const bool chanMayDrive = (src == CmdSource::WIRE) ||
                                registryFor(currentMode_).acceptsBtDrive;
      if (c != 'S' && (!modeAcceptsDrive || isCurrentModeStub() || !chanMayDrive)) {
        snprintf(statusText_, STATUS_TEXT_LEN, isCurrentModeStub() ? "COMING SOON" : "Cmd Ignored (mode)");
        if (oled_) oled_->draw(statusText_, speed_, isConnectedNow(), modeToString(currentMode_), extraInfo_);
        return;
      }
      lastCommandTimeMs_ = nowFn_();
      failsafeActive_ = false;
      if (c == 'F') { if (motor_) motor_->drive(Action::FORWARD, speed_); snprintf(statusText_, STATUS_TEXT_LEN, "Forward"); }
      else if (c == 'B') { if (motor_) motor_->drive(Action::BACKWARD, speed_); snprintf(statusText_, STATUS_TEXT_LEN, "Backward"); }
      else if (c == 'L') { if (motor_) motor_->drive(Action::LEFT, speed_); snprintf(statusText_, STATUS_TEXT_LEN, "Left"); }
      else if (c == 'R') { if (motor_) motor_->drive(Action::RIGHT, speed_); snprintf(statusText_, STATUS_TEXT_LEN, "Right"); }
      else if (c == 'S') {
        if (motor_) motor_->drive(Action::STOP, 0);
        snprintf(statusText_, STATUS_TEXT_LEN, "Stopped");
        // Immediate OLED update, no throttle (W-3)
        if (oled_) oled_->draw(statusText_, speed_, isConnectedNow(), modeToString(currentMode_), extraInfo_);
        return; // exit early so stop is applied instantly
      }

      const unsigned long OLED_MOVE_INTERVAL = 20;
      uint32_t now = nowFn_();
      if (now - lastOledMs_ >= OLED_MOVE_INTERVAL) {
        if (oled_ && strncmp(statusText_, lastOledStatus_, STATUS_TEXT_LEN) != 0) {
          oled_->draw(statusText_, speed_, isConnectedNow(), modeToString(currentMode_), extraInfo_);
          strncpy(lastOledStatus_, statusText_, STATUS_TEXT_LEN - 1);
          lastOledStatus_[STATUS_TEXT_LEN - 1] = '\0';
        }
        lastOledMs_ = now;
      }
      return;
    }
  }

  if (strncasecmp(cmdBuf, "SPD", 3) == 0) {
    // v1.4.0: stub modes take no speed changes (SPD0 keeps its safe-stop below).
    int stubProbe = atoi(cmdBuf + 3);
    if (stubProbe != 0 && isCurrentModeStub()) {
      snprintf(statusText_, STATUS_TEXT_LEN, "COMING SOON");
      if (oled_) oled_->draw(statusText_, speed_, isConnectedNow(), modeToString(currentMode_), extraInfo_);
      return;
    }
    // Accepted command (speed set or safe-stop) → refresh the failsafe clock.
    lastCommandTimeMs_ = nowFn_();
    failsafeActive_ = false;
    readyDisplayed_ = false;
    int val = atoi(cmdBuf + 3);
    if (val == 0) {
      // Safe-stop from app/remote (SPD0): stop motors, KEEP the stored speed
      // (v2.0.2 semantics — do not clamp to MIN_SPEED and do not raise speed_).
      if (motor_) motor_->drive(Action::STOP, 0);
      snprintf(statusText_, STATUS_TEXT_LEN, "Stopped");
      if (oled_) oled_->draw(statusText_, speed_, isConnectedNow(), modeToString(currentMode_), extraInfo_);
      return;
    }
    if (val < static_cast<int>(MIN_SPEED)) val = MIN_SPEED;
    if (val > static_cast<int>(MAX_SPEED)) val = MAX_SPEED;
    speed_ = static_cast<uint16_t>(val);
    // T-13: persist the authoritative speed (debounced write).
    scheduleModeSpeedPersist();
    snprintf(statusText_, STATUS_TEXT_LEN, "Speed %d", val);
    uint32_t now = nowFn_();
    const unsigned long OLED_MIN_INTERVAL = 150;
    if (now - lastOledMs_ >= OLED_MIN_INTERVAL) {
      if (oled_) oled_->draw(statusText_, speed_, isConnectedNow(), modeToString(currentMode_), extraInfo_);
      lastOledMs_ = now;
    }
    return;
  }

  // F-21: bare mode tokens (app carProtocol.ts + remote MODE_CMDS[]). The
  // registry match makes unknown tokens show "Unknown mode:" (existing OLED
  // behavior) without eating the command path.
  setModeFromString(cmdBuf);
}

void ModeManager::sendState() {
  // v2: fan out to EVERY transport in EVERY mode — comm_ is the always-on
  // web transport (WS/HTTP) and auxComm_ is Bluetooth. (Donor mirrored BT
  // only inside SERVER mode; v2 has no mode-gated transport at all.)
  if (comm_ && comm_->connected()) {
    sendStateOn(*comm_);
  }
  if (auxComm_ && auxComm_ != comm_ && auxComm_->connected()) {
    sendStateOn(*auxComm_);
  }
}

void ModeManager::sendStateOn(CommInterface& comm) {
  crashRtcMark("state");
  char word[24];
  getStatusWord(word, sizeof(word));
  // T-19 (W-10): stub truth is announced on BOTH channels from the SAME
  // registry source: `;CAP=STUB` on STATE lines (Bluetooth consumers — the
  // hand-held remote, R-10) and `"stub":true` in the WS JSON (WiFi consumers
  // — app/site). The suffix retuned in v1.4.0 (iv); reinstated 2026-09-14 (v)
  // because the BT-only remote can never see WS JSON. Both read
  // registryFor(currentMode_).isStub() so they can never disagree.
  // D7 (U-68): sized so a full `REPLY=` (a complete saved-router list, up to
  // 191 bytes in pendingReply_) plus the fixed STATE fields and wifiKeys still
  // fit. At 248 a two-or-three-router list was truncated mid-answer, which is
  // now a WRONG answer rather than an ignored one. The Bluetooth link buffers
  // a line at a time, so a longer line is fine as long as the builder holds it.
  // U-102: grew again (448 -> 512) for the new telemetry keys (UP/HEAP/SIG/RSSI,
  // see the append below) — the previous worst case (~438 B) plus ~30 B would
  // have truncated the STATUS/wifi tail on a full REPLY.
  // U-R3 (1.3.2): + the restart record (RST/BC/CC/PH/LH below, up to ~56 B) so a
  // Bluetooth-only controller finally sees WHY the car reset — the same fields
  // /status has carried since U-95. wifiKeys grew 224 -> 288 to hold it.
  char buf[704];
  // R-12: STATE also carries the configured SSID (never the password) and the
  // AP fallback broadcast id so the remote's dashboard can show the car's
  // WiFi truth (the app gets the same via the WS JSON `ssid`/`ap`).
  // R-13: plus the car's LIVE reachable IP (`;IP=`), so the remote OLED and
  // the app deck show where to point a browser / WS even over pure WiFi.
  // Connections-Hub: plus the stable board id (`;ID=`), the fw:<id> profile
  // key source for WiFi-only cars (STATE:000000..FFFFFF hex).
  char wifiKeys[384] = "";
  // v2: WiFi truth (SSID / AP id / reachable IP) rides EVERY STATE line in
  // EVERY mode — the web layer is always-on and no longer SERVER-scoped.
  // (instanceForWs may be null pre-setup(); guarded.)
  {
    WebServerComm* ws = WebServerComm::instance();  // may be null pre-setup
    // U-70 (2026-10-02): SSID= is the network the car is ACTUALLY ON, not the
    // one it was asked to join. It used to be `storedSsid()`, which is written
    // the moment ROUTERS;USE stores the pair and BEFORE any association is
    // attempted - so a failed switch announced the new router while the car sat
    // on its own hotspot, and neither the remote nor the app could tell. The
    // target the user asked for still ships, as a SEPARATE field (WANT=) so a
    // switch in progress is visible and honest instead of being invisible.
    {
      // U-heap diet: char buffer instead of the String-by-value API - this
      // block runs on EVERY STATE tick (1.5 s) and every link event.
      char joined[33] = "";
      if (ws) ws->joinedSsid(joined, sizeof(joined));
      if (joined[0]) snprintf(wifiKeys + strlen(wifiKeys), sizeof(wifiKeys) - strlen(wifiKeys), ";SSID=%s", joined);
      const char* want = ws ? ws->storedSsid() : "";
      if (want[0] && strcmp(joined, want) != 0) snprintf(wifiKeys + strlen(wifiKeys), sizeof(wifiKeys) - strlen(wifiKeys), ";WANT=%s", want);
    }
    if (ws && ws->apBroadcastId()[0]) snprintf(wifiKeys + strlen(wifiKeys), sizeof(wifiKeys) - strlen(wifiKeys), ";AP=%s", ws->apBroadcastId());
    char ip[32] = "";
    if (ws) ws->currentIp(ip, sizeof(ip));
    if (ip[0]) snprintf(wifiKeys + strlen(wifiKeys), sizeof(wifiKeys) - strlen(wifiKeys), ";IP=%s", ip);
    if (ws) {
      char id[8] = "";
      WebServerComm::boardIdHex(id, sizeof(id));
      snprintf(wifiKeys + strlen(wifiKeys), sizeof(wifiKeys) - strlen(wifiKeys), ";ID=%s", id);
    }
  }
  // U-102 (2026-10-12): the STATE line now carries the car's LIVE telemetry
  // over BOTH links, so a Bluetooth-only controller (which never sees the WS
  // JSON) renders the same uptime/heap/signal the app shows on its top status
  // line. UP=/HEAP= are always present and meaningful; SIG=/RSSI= are STA truth
  // only (mirror of buildStatusJson: on the car's own AP there is no client
  // signal, so the app keeps those rows dashed). App half: carProtocol.ts.
  // U-R3 (1.3.2): + the persisted restart record — ;RST=<reason>;BC=<boots>;
  // CC=<crashes>;PH=<phase at that boot>;LH=<heap at that boot>. Same fields,
  // same meaning as the JSON's reset_reason/boot_count/crash_count/
  // last_crash_phase/last_crash_heap, so a car VIA BLUETOOTH reports WHY it
  // reset without this phone ever seeing the WS page. The phase strings are
  // firmware string LITERALS (crashLogPhase() call sites), never anything a
  // phone sent — mirrors the JSON escaping note in buildStatusJson.
  {
    const CrashInfo& ci = crashLogInfo();
    char tKeys[224] = "";
    snprintf(tKeys + strlen(tKeys), sizeof(tKeys) - strlen(tKeys), ";UP=%lu",
             static_cast<unsigned long>(millis()));
    snprintf(tKeys + strlen(tKeys), sizeof(tKeys) - strlen(tKeys), ";HEAP=%u",
             static_cast<unsigned int>(ESP.getFreeHeap()));
    if (WiFi.status() == WL_CONNECTED) {
      const int rssi = WiFi.RSSI();
      const int pct = map(constrain(rssi, -100, -50), -100, -50, 0, 100);
      snprintf(tKeys + strlen(tKeys), sizeof(tKeys) - strlen(tKeys), ";SIG=%d", pct);
      snprintf(tKeys + strlen(tKeys), sizeof(tKeys) - strlen(tKeys), ";RSSI=%d", rssi);
    }
    snprintf(tKeys + strlen(tKeys), sizeof(tKeys) - strlen(tKeys), ";RST=%s", ci.label);
    snprintf(tKeys + strlen(tKeys), sizeof(tKeys) - strlen(tKeys), ";BC=%lu",
             static_cast<unsigned long>(ci.bootCount));
    snprintf(tKeys + strlen(tKeys), sizeof(tKeys) - strlen(tKeys), ";CC=%lu",
             static_cast<unsigned long>(ci.crashCount));
    if (ci.phase[0]) snprintf(tKeys + strlen(tKeys), sizeof(tKeys) - strlen(tKeys), ";PH=%s", ci.phase);
    snprintf(tKeys + strlen(tKeys), sizeof(tKeys) - strlen(tKeys), ";LH=%lu",
             static_cast<unsigned long>(ci.heapAtBoot));
    // U-105 (1.3.5): transport toggles + boot auto-join on every STATE line so a
    // Bluetooth-only remote/app can read the car's stored config without needing
    // a WS JSON round-trip. Mirrors the /status JSON fields.
    {
      WebServerComm* ws = WebServerComm::instance();
      if (ws) {
        snprintf(tKeys + strlen(tKeys), sizeof(tKeys) - strlen(tKeys), ";WIFI=%s",
                 ws->wifiEnabled() ? "ON" : "OFF");
        snprintf(tKeys + strlen(tKeys), sizeof(tKeys) - strlen(tKeys), ";BT=%s",
                 ws->btEnabled() ? "ON" : "OFF");
        snprintf(tKeys + strlen(tKeys), sizeof(tKeys) - strlen(tKeys), ";AUTOJOIN=%s",
                 ws->bootAutojoin() ? "ON" : "OFF");
        // U-117/U-118 (2026-10-20, owner: "the webpage toggle off button is
        // not working"): the web-page server state rides every STATE line too.
        // Until now only `/status` carried it (`page_on`), so a Bluetooth-only
        // app could never learn whether the car is serving its page and showed
        // an ASSUMED off after every (re)connect — a switch that reads wrong
        // reads broken. Absent on older firmware by definition (the key simply
        // is not there) and the app treats absence as "don't know", never OFF.
        // Same-commit app consumer: pageToggle.test.ts (genumsolutions-app).
        snprintf(tKeys + strlen(tKeys), sizeof(tKeys) - strlen(tKeys), ";PAGE=%s",
                 ws->pageServerIsOn() ? "ON" : "OFF");
        // P4 (2026-10-17, owner item F): the link-admission verdict rides every
        // STATE line too, so a Bluetooth-only app learns whether the car can host
        // a second link WITHOUT a WS round-trip. 0 = the car refused the second
        // link to keep the run-time alive (the incumbent link stays up); 1 = a
        // second link is (or could be) admitted. Absent on pre-1.3.13 firmware,
        // and the app reads ABSENCE as "capable" (F-88), never as a denial.
        snprintf(tKeys + strlen(tKeys), sizeof(tKeys) - strlen(tKeys), ";LINKCAP=%d",
                 ws->linkCapCode());
      }
    }
    strncat(wifiKeys, tKeys, sizeof(wifiKeys) - strlen(wifiKeys) - 1);
  }
  // v1.4.0: a queued provisioning reply (WIFICFG;STORED;… etc.) rides the
  // next STATE line as REPLY=<text> so it reaches the app over Bluetooth.
  if (pendingReply_[0] != '\0') {
    snprintf(buf, sizeof(buf), "STATE;MODE=%s;SPD=%d;TRIM=%d;STATUS=%s%s%s;REPLY=%s\n",
             getModeToken(), static_cast<int>(speed_), getTrim(), word,
             registryFor(currentMode_).isStub() ? ";CAP=STUB" : "", wifiKeys, pendingReply_);
    pendingReply_[0] = '\0';
  } else {
    snprintf(buf, sizeof(buf), "STATE;MODE=%s;SPD=%d;TRIM=%d;STATUS=%s%s%s\n",
             getModeToken(), static_cast<int>(speed_), getTrim(), word,
             registryFor(currentMode_).isStub() ? ";CAP=STUB" : "", wifiKeys);
  }
  comm.write(buf, strlen(buf));

  // 2026-09-15 (owner matrix): broadcast the FULL per-mode availability map
  // on every STATE so the hand-held remote / app learn each mode's state
  // (LIVE/WIP/CS) for the CURRENTLY PAIRED car up front — ESP_SER (and any
  // WIP) becomes reachable immediately without waiting for car truth on each
  // token. Clients parse CAPS;TOKEN:STATE;... and mark non-LIVE modes.
  char caps[128];
  buildCapsMap(caps, sizeof(caps));
  snprintf(buf, sizeof(buf), "CAPS;%s\n", caps);
  comm.write(buf, strlen(buf));

  // T-49 (round-5): names-only saved-router sync — the hand-held remote's SW
  // saved-networks menu consumes this (never passwords; source of truth is the
  // car's NVS registry). Idempotent for the remote (its cache is change-guarded).
  if (WebServerComm::instance()) {
    char networks[200] = "";
    WebServerComm::instance()->buildNetworksLine(networks, sizeof(networks));
    if (networks[0]) {
      snprintf(buf, sizeof(buf), "%s\n", networks);
      comm.write(buf, strlen(buf));
    }
    // D3 (U-68): the car-side nearby-network scan, as its own line — the
    // Bluetooth encoding, exactly as `SCAN;<ssid>,<rssi>,<open>;…` / `SCAN;NONE`.
    // The WS JSON carries the same data as `"scan":[…]`, so one app parser
    // serves both links (F-66). `SCAN;NONE` when there is nothing to show, so
    // the app can tell "scanned, found none" from "never scanned".
    char scan[320] = "";
    WebServerComm::instance()->buildScanLine(scan, sizeof(scan));
    if (scan[0]) {
      snprintf(buf, sizeof(buf), "%s\n", scan);
      comm.write(buf, strlen(buf));
    }
  }
}

void ModeManager::setStatusText(const char* s) {
  if (!s) return;
  strncpy(statusText_, s, STATUS_TEXT_LEN - 1);
  statusText_[STATUS_TEXT_LEN - 1] = '\0';
  // Throttled OLED redraw (same cadence as drive-command updates).
  uint32_t now = nowFn_();
  if (oled_ && strncmp(statusText_, lastOledStatus_, STATUS_TEXT_LEN) != 0 &&
      (now - lastOledMs_ >= 20)) {
    oled_->draw(statusText_, speed_, isConnectedNow(), modeToString(currentMode_), extraInfo_);
    strncpy(lastOledStatus_, statusText_, STATUS_TEXT_LEN - 1);
    lastOledStatus_[STATUS_TEXT_LEN - 1] = '\0';
    lastOledMs_ = now;
  }
}

void ModeManager::getStatusWord(char* buf, size_t bufLen) const {
  whitelistedStatus(statusText_, buf, bufLen);
}

void ModeManager::checkFailsafe() {
  uint32_t now = nowFn_();
  bool connectedNow = isConnectedNow();

  if (connectedNow != lastConnected_) {
    lastConnected_ = connectedNow;
    failsafeActive_ = false;
    readyDisplayed_ = false;
    lastCommandTimeMs_ = now;
    // T-46 (round-4): dedupe the edge frame — skip the redraw when the
    // new message matches the last shown status, so rapid Reconnected /
    // Disconnected chattering during BT settling no longer floods the OLED.
    snprintf(statusText_, STATUS_TEXT_LEN, connectedNow ? "Reconnected" : "Disconnected");
    if (strcmp(statusText_, lastOledStatus_) != 0 && oled_) {
      oled_->draw(statusText_, speed_, connectedNow, modeToString(currentMode_), extraInfo_);
      strncpy(lastOledStatus_, statusText_, STATUS_TEXT_LEN - 1);
      lastOledStatus_[STATUS_TEXT_LEN - 1] = '\0';
      lastOledMs_ = now;
    }
    return;
  }
  if (readyDisplayed_) return;

  if (!failsafeActive_ && !registryFor(currentMode_).runsAutonomous &&
      (now - lastCommandTimeMs_ > FAILSAFE_TIMEOUT_MS)) {
    if (motor_) motor_->drive(Action::STOP, 0);
    snprintf(statusText_, STATUS_TEXT_LEN, "Failsafe Stop");
    if (oled_) oled_->draw(statusText_, 0, isConnectedNow(), modeToString(currentMode_), extraInfo_);
    failsafeActive_ = true;
    failsafeStartMs_ = now;
    FW_LOG_VERBOSE("Free heap (Failsafe Stop): %d\n", static_cast<int>(ESP.getFreeHeap()));
    return;
  }
  if (failsafeActive_ && (now - failsafeStartMs_ > 1500)) {
    failsafeActive_ = false;
    // T-27: parked modes show ONLY "COMING SOON" in the body — the top bar
    // already carries the mode name, and the old "(stub)" suffix is gone.
    if (isCurrentModeStub()) {
      if (oled_) oled_->draw("COMING SOON", 0, isConnectedNow(), modeToString(currentMode_), extraInfo_);
    } else {
      snprintf(statusText_, STATUS_TEXT_LEN, " !!! Ready !!! ");
      if (oled_) oled_->draw(statusText_, 0, isConnectedNow(), modeToString(currentMode_), extraInfo_);
    }
    readyDisplayed_ = true;
  }
}

static size_t readLineFromComm(CommInterface* c, char* buf, size_t bufLen, unsigned long timeoutMs = 20) {
  if (!c || !buf || bufLen == 0) return 0;
  size_t idx = 0;
  unsigned long start = millis();
  while (millis() - start < timeoutMs && idx < bufLen - 1) {
    if (c->available()) {
      int ch = c->read();
      if (ch < 0) continue;
      if (ch == '\r') continue;
      if (ch == '\n') break;
      buf[idx++] = static_cast<char>(ch);
    } else {
      yield();
    }
  }
  buf[idx] = '\0';
  trimWhitespace(buf);
  return idx;
}

void ModeManager::run() {
  char cmdBuf[128];

  // T-13: flush any pending debounced NVS persist (mode + speed) when due.
  flushModeSpeedPersistIfDue();

  // Single command path (F-20b): both channels go through handleCommand().
  if (auxComm_ != nullptr && auxComm_->available()) {
    size_t n = readLineFromComm(auxComm_, cmdBuf, sizeof(cmdBuf), 20);
    if (n > 0) handleCommand(cmdBuf);
  }

  if (comm_ != nullptr && comm_ != auxComm_ && comm_->available()) {
    size_t n = readLineFromComm(comm_, cmdBuf, sizeof(cmdBuf), 20);
    // v2: comm_ is the always-on web transport - queued lines are WIRE-sourced.
    // U-88: the WS and HTTP handlers queue ONLY (no live dispatch), so this
    // drain is the single dispatch point for every web line; handleCommand()
    // here is what makes it run, with consistent source truth.
    if (n > 0) handleCommand(cmdBuf, CmdSource::WIRE);
  }

  // F-22: periodic STATE broadcast keeps the remote's mode/speed mirror fresh
  // (prevents the remote's stale-telemetry safe-stop while connected).
  // v2: the tick fires when EITHER transport is live — the app can sit on a
  // WebSocket to the car's own AP while the BT remote is also connected.
  bool anyConnected = (comm_ != nullptr && comm_->connected()) ||
                      (auxComm_ != nullptr && auxComm_->connected());

  // v1.0.1 (owner OLED directive): refresh the Link/Type context every loop
  // (cheap; the OLED change-gates on the string so no repaint storms).
  pushLinkInfo();
  // U-107 (owner 2026-10-13): keep the OLED's last-two-rows rule in step with
  // the U-105 transport toggles (change-gated internally).
  pushTransportToggles();
  // U-107c: hide connection-method rows when one link is active (exclusive mode).
  pushExclusiveMode();
  // U-107b (owner 2026-10-14): drive the frame/name-vs-IP rotation on its own
  // clock. Without this the phases only advanced inside a content-changing
  // draw() — and an idle car never changes content, so the alternation froze on
  // whichever frame was showing. Cheap: tick() repaints only when the visible
  // frame actually flips (~2x/s), not every loop.
  if (oled_) oled_->tick();
  if (anyConnected) {
    uint32_t now = nowFn_();
    if (now - lastStateMs_ >= STATE_INTERVAL_MS) {
      sendState();
      lastStateMs_ = now;
    }
  }

  checkFailsafe();
}

bool ModeManager::isConnectedNow() const {
  // v2: "connected" = ANY transport link up — Bluetooth (auxComm_) or the
  // always-on web transport (comm_: joined STA network OR a live WS client).
  // Mode-independent by design: the web layer is not a mode anymore.
  if (auxComm_ && auxComm_ != comm_ && auxComm_->connected()) return true;
  return comm_ ? comm_->connected() : false;
}

void ModeManager::resetFailsafe() {
  failsafeActive_ = false;
  readyDisplayed_ = false;
  lastCommandTimeMs_ = nowFn_();
}

// T-13: debounced NVS persistence of the authoritative mode + speed. The
// write itself is cheap and rare (once per change, 1 s debounce); scheduling
// happens on every accepted mode/speed change, flushing once per loop.
void ModeManager::scheduleModeSpeedPersist() {
  pendingModeSpeedSave_ = true;
  modeSpeedSaveMs_ = nowFn_();
}

void ModeManager::flushModeSpeedPersistIfDue() {
  if (!pendingModeSpeedSave_) return;
  uint32_t now = nowFn_();
  if (now - modeSpeedSaveMs_ >= MODE_SPEED_SAVE_DEBOUNCE_MS) {
    saveBootState(static_cast<uint8_t>(currentMode_), speed_);
    pendingModeSpeedSave_ = false;
    modeSpeedSaveMs_ = now;
  }
}

// T-13: applies the persisted boot state (last mode + speed) with the same
// entry side effects as setMode(). Must run from setup(): NVS (Preferences)
// is only usable after nvs_flash_init(), which the Arduino core performs
// right before setup() — NOT at global-ctor time. Mirrors the
// Genum_2WD1M_CAR / Genum_SELF_BALANCE_CAR reference.
void ModeManager::applyBootState() {
  uint8_t bootMode = 0xFF;
  uint16_t bootSpeed = 0xFFFF;
  if (loadBootState(bootMode, bootSpeed)) {
    // v2: only restore modes that are still LIVE. A retired (CS) or
    // in-progress (WIP) restored mode — e.g. ESP_SER from donor-firmware NVS on
    // the refit testbed — would boot the car into a COMING SOON frame; keep
    // 4WD4M instead.
    if (bootMode < MODE_COUNT &&
        registryFor(static_cast<Mode>(bootMode)).avail == ModeAvail::LIVE) {
      currentMode_ = static_cast<Mode>(bootMode);
      // T-41 (round-3): level-1 so reboot-restore is visible on the flash
      // session.
      FW_LOG("[BOOT] Restored mode=%s\n", modeToString(currentMode_));
    } else if (bootMode < MODE_COUNT) {
      // Reachable whenever the stored mode is a valid registry row that is not
      // LIVE (WIP/CS) — the case the branch above used to swallow.
      FW_LOG("[BOOT] Restored mode=%s is not LIVE in v2 - keeping 4WD4M.\n",
             registryFor(static_cast<Mode>(bootMode)).token);
    } else {
      FW_LOG("[BOOT] Stored mode %u out of range - keeping 4WD4M.\n",
             static_cast<unsigned>(bootMode));
    }
    if (bootSpeed >= static_cast<uint16_t>(MIN_SPEED) && bootSpeed <= static_cast<uint16_t>(MAX_SPEED)) {
      speed_ = bootSpeed;
      FW_LOG("[BOOT] Restored speed=%u\n", (unsigned)speed_);
    }
  } else {
    FW_LOG("[BOOT] No boot state found; keeping defaults (BT / MIN_SPEED).\n");
  }

  if (motor_) motor_->drive(Action::STOP, 0);

  failsafeActive_ = false;
  readyDisplayed_ = false;
  lastCommandTimeMs_ = nowFn_();

  // T-18 parity: push registry truth to the OLED (token + capability flags),
  // then one immediate frame so the restored mode renders on boot.
  snprintf(statusText_, STATUS_TEXT_LEN, "Ready");
  if (oled_) {
    oled_->setModeContext(getModeToken(),
                          registryFor(currentMode_).isStub(),
                          registryFor(currentMode_).avail == ModeAvail::WIP,
                          registryFor(currentMode_).acceptsDrive);
    oled_->draw(statusText_, speed_, isConnectedNow(),
                modeToString(currentMode_), extraInfo_);
  }
  strncpy(lastOledStatus_, statusText_, STATUS_TEXT_LEN - 1);
  lastOledStatus_[STATUS_TEXT_LEN - 1] = '\0';
  lastOledMs_ = nowFn_();
}

void ModeManager::setNowProvider(NowFn nowFn) {
  nowFn_ = nowFn ? nowFn : defaultNow;
}

void ModeManager::setSpeed(int newSpeed) {
  if (newSpeed < static_cast<int>(MIN_SPEED)) newSpeed = MIN_SPEED;
  if (newSpeed > static_cast<int>(MAX_SPEED)) newSpeed = MAX_SPEED;
  speed_ = static_cast<uint16_t>(newSpeed);
  // T-13: persist the authoritative speed (debounced write).
  scheduleModeSpeedPersist();
  snprintf(statusText_, STATUS_TEXT_LEN, "Speed set %d", newSpeed);
  uint32_t now = nowFn_();
  const unsigned long OLED_MIN_INTERVAL = 150;
  if (now - lastOledMs_ >= OLED_MIN_INTERVAL) {
    if (oled_) oled_->draw(statusText_, speed_, isConnectedNow(), modeToString(currentMode_), extraInfo_);
    lastOledMs_ = now;
  }
}

// Accept canonical registry tokens first (parity with the remote's
// MODE_CMDS[]), then web-page/legacy aliases (F-20c).
int ModeManager::getTrim() const {
  return g_2wd1mHandler.getTrim();
}

void ModeManager::setModeFromString(const char* modeStr) {
  if (!modeStr) return;

  for (int i = 0; i < MODE_COUNT; ++i) {
    if (strcasecmp(modeStr, MODE_REGISTRY[i].token) == 0) {
      FW_LOG("[MODE] token='%s' accepted -> %s\n", modeStr, MODE_REGISTRY[i].token);
      setMode(MODE_REGISTRY[i].mode);
      return;
    }
  }

  // X-8 permanent legacy alias: pre-v1.5.0 controllers send "BT" for the
  // 4WD4M mode. Receivers accept both tokens forever (compat matrix in
  // INTEGRATION.md §2d X-8); senders emit "4WD4M".
  if (strcasecmp(modeStr, "BT") == 0) {
    setMode(Mode::BT);
    return;
  }

  if (strcasecmp(modeStr, "MANUAL") == 0) {
    setMode(Mode::MANUAL);
    return;
  }

  // Unknown mode: show on OLED. U-79: ESP_SER / ESP_CLI (and their
  // ESP_SERVER/ESP_CLIENT aliases) now land here — an explicit NACK instead
  // of silently booting a dead COMING SOON mode.
  snprintf(statusText_, STATUS_TEXT_LEN, "Unknown mode: %s", modeStr);
  if (oled_) oled_->draw(statusText_, speed_, isConnectedNow(), modeToString(currentMode_), extraInfo_);
  // R-4: explicit fleet NACK so the hand-held remote / app surface
  // "Not supported by car" instead of a silent no-op (mixed-pair case:
  // e.g. new remote sending the renamed 4WD4M token to a legacy car).
  //
  // U-81: fan it out over EVERY connected transport, the same way sendState()
  // does (ModeManager.cpp:459-468). It used to go to `comm_` only, behind a
  // `comm_->connected()` guard — so a token that arrived over BLUETOOTH was
  // refused in silence whenever the web transport had no client, and the user
  // watched the app show the wrong mode for MODE_CHANGE_GRACE_MS and then snap
  // back with no message. The refusal must reach the link that sent the token.
  {
    char nack[96];
    snprintf(nack, sizeof(nack), "NACK;E=UNKNOWN_MODE;ARG=%s\n", modeStr);
    if (comm_ && comm_->connected()) comm_->write(nack, strlen(nack));
    if (auxComm_ && auxComm_ != comm_ && auxComm_->connected())
      auxComm_->write(nack, strlen(nack));
  }
}