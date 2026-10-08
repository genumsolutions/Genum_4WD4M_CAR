#pragma once

// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: ModeManager.h — mode engine: mode registry, single command path
//       (T-1), failsafe, STATE/CAP=STUB/NACK broadcast, per-mode
//       availability (LIVE/WIP/CS) + transport-aware drive gate
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include "Config.h"
#include "MotorControl.h"
#include "CommInterface.h"
#include "OledDisplay.h"
#include <stdint.h>
#include <stddef.h>

class ModeManager {
public:
  enum class Mode : uint8_t {
    BT = 0,
    PATH,
    OBS_US,
    OBS_IR,
    MANUAL,
    AUTO,
    M2WD1M,   // D-2: 2WD1M-compat tank steer (signed SPD + SERVO)
    MODE_LAST
  };
  // U-79 (2026-10-03, owner ruling): ESP_SER / ESP_CLI are REMOVED from the
  // registry entirely. The Wi-Fi web layer is the always-on connection
  // channel (v2 R-4) — a "webserver mode" token was a SECOND Wi-Fi setup
  // that only ever rendered COMING SOON and gated drive (the owner's
  // "webpage controls stopped working": the page had switched the car into
  // the stub). No token, no trap. Legacy wire aliases for them are gone too;
  // sending one now earns a NACK;UNKNOWN_MODE. NVS parity: old stored mode
  // indices 1/2 (SERVER/CLIENT) now name CS modes, which applyBootState()
  // already refuses — donor-era NVS cannot restore them.

  static constexpr int MODE_COUNT = static_cast<int>(Mode::MODE_LAST);

  // Per-mode availability announced to controllers (CAPS broadcast / JSON
  // "caps"). LIVE = functional now, WIP = in progress, CS = coming soon.
  //   isStub() == (avail != LIVE)  → WIP and CS are both inert for driving.
  enum class ModeAvail : uint8_t {
    LIVE = 0,
    WIP,
    CS
  };

  // Transport the command arrived on. T-29 intent: acceptsBtDrive scopes the
  // BLUETOOTH transport only; WIRE (WebSocket/HTTP) drive only needs the mode
  // LIVE and drive-capable. This fixed the v1.5.0 regression where ESP_SER
  // drove over neither channel.
  enum class CmdSource : uint8_t {
    BLUETOOTH = 0,
    WIRE
  };

  using NowFn = uint32_t(*)();

  ModeManager(MotorControl* motor, CommInterface* primaryComm, OledDisplay* oled);
  ~ModeManager();

  void setMode(Mode newMode);
  /** One implementation of the mode walk for every caller: the GPIO switch
   *  (.ino) uses the default +1, and MODECYCLE/MODENEXT/MODEPREV (U-95 Phase 3
   *  tokens) pass ±1 so the page, app and relay all get the SAME behaviour.
   *  Wraps both directions: +1 past the last registry row returns to 0, -1
   *  below 0 lands on MODE_COUNT-1. */
  void cycleMode(int delta = 1);
  void handleCommand(const char* cmd, CmdSource src = CmdSource::BLUETOOTH);
  void run();
  void checkFailsafe();
  void resetFailsafe();
  void setComm(CommInterface* newComm);
  void setAuxComm(CommInterface* aux);
  /** U-94 Phase 2: the Internet transport. A THIRD slot, not a replacement for
   *  either of the others: the relay is a pipe like Bluetooth and the web page,
   *  and it is how a car gets driven from a phone that is not on this network.
   *
   *  It is a separate pointer rather than an assignment to comm_/auxComm_ on
   *  purpose. Those two are the U-79 power-policy pair, and swapping the relay
   *  into either would take the Wi-Fi AP down whenever a phone joined over the
   *  Internet - i.e. exactly when the relay is most needed. */
  void setRelayComm(CommInterface* relay);
  void setExtraInfo(const char* info);

  // v1.0.1 (owner OLED directive 2026-09-28): the dashboard must ALWAYS show
  // which link method is connected - pronto - and the transport type under it
  // (SPP / BLE / WebServer / WebClient / RF). Computed each run() from live
  // transport truth: BT (SPP) > WiFi (WebServer WS clients, else WebServer AP/
  // STA) > none. Pushed to the OLED as a two-part context string.
  struct LinkInfo {
    const char* method;  // "Bluetooth" / "WiFi" / "None"
    const char* type;    // "SPP" / "WebServer" / "AP ready" / "LAN ready" / "--"
    bool connected;
  };
  LinkInfo currentLink() const;
  void pushLinkInfo();
  size_t getStatusText(char* buf, size_t bufLen) const;
  int getSpeed() const;
  Mode getMode() const;
  const char* getModeString() const;
  bool isConnectedNowPublic() const;
  void setNowProvider(NowFn nowFn);

  // U-98: Get OLED display for network cycling control
  OledDisplay* getOledDisplay() const { return oled_; }

  // ---- System-command hook (v1.4.0 provisioning) ----
  // Runs BEFORE every other command path. Return true = consumed (never a
  // mode token, so registry matching is unaffected). Used by WebServerComm
  // to intercept WIFICFG;... provisioning lines sent over Bluetooth.
  using SystemCmdFn = bool (*)(const char* cmd);
  void setSystemCommandHook(SystemCmdFn fn) { systemCmdHook_ = fn; }

  /** Queue a raw reply line for the app (WIFICFG responses). It rides the
   *  next STATE broadcast as REPLY=<text> and is then cleared. */
  void queueReply(const char* text);

  void setSpeed(int newSpeed);

  // Allow setting mode from app strings
  void setModeFromString(const char* modeStr);

  // Get current 2WD1M trim offset.
  int getTrim() const;

  // T-13: applies the persisted boot state (last mode + speed, ParamsStore)
  // with all mode-entry side effects (motor stop, failsafe reset, OLED
  // context + immediate frame). Called from setup() AFTER webServerComm.init()
  // so a restored SERVER session can join WiFi from loop(); a fresh boot with
  // no saved state keeps the constructor defaults (BT / MIN_SPEED).
  void applyBootState();

  // ---- Mode registry (D-2: extensible, data-driven) ----
  // One entry per mode: canonical token, display name, capability flags.
  // Adding a future mode = add a Mode + a REGISTRY row; the dispatch,
  // STATE emission and token mapping read from here (kills the F-21/F-23
  // token-drift class structurally).
  struct ModeEntry {
    Mode mode;
    const char* token;       // canonical protocol token (MODE_CMDS[] parity)
    const char* displayName; // human-readable (OLED only, never protocol)
    bool usesSignedSpd;      // accepts signed SPD±255 (2WD1M-style handler)
    bool runsAutonomous;     // command silence is normal (failsafe exemption)
    bool acceptsDrive;       // T-17 (W-13): accepts F/B/L/R/S direction letters
    ModeAvail avail;         // LIVE / WIP / CS — announced via CAPS + "stub" JSON
    bool acceptsBtDrive;     // T-29: Bluetooth transport may DRIVE this mode
                             // (system commands stay universal; WIRE transport
                             // only needs avail==LIVE + acceptsDrive)
    bool isStub() const { return avail != ModeAvail::LIVE; }  // WIP + CS inert
  };
  static const ModeEntry MODE_REGISTRY[MODE_COUNT];
  static const ModeEntry& registryFor(Mode m);
  static const char* modeToCommandToken(Mode m);
  const char* getModeToken() const;

  /** T-19 (W-10): true when the current mode is not LIVE (WIP or CS). */
  bool isCurrentModeStub() const { return registryFor(currentMode_).isStub(); }

  /** Fleet availability map for clients, built from MODE_REGISTRY in device
   *  order — e.g. "4WD4M:LIVE;PATH:CS;…;2WD1M:CS". Sent on STATE lines as
   *  CAPS;... and in WS JSON "caps".
   *  The example previously shown here read "4WD4M:LIVE;ESP_SER:LIVE;…", which
   *  this car cannot emit: ESP_SER was removed from the registry in U-79.
   *  TRACKS/GUIDE.md documents the corrected CAPS line. */
void buildCapsMap(char* buf, size_t bufLen) const;

  // ---- Authoritative state (F-22) ----
  void sendState();
  /** T-35: sendState() to an EXPLICIT transport. Used when the caller needs the
   *  frame to leave over a named link rather than the current `comm_` — the
   *  REPLY= fan-out sends to both transports when an app command needs a
   *  confirmation. (The old comment here described "a no-op web transport in
   *  ESP_SER"; there is no such thing — comm_ is the always-on web layer, set
   *  once in setup(), and ESP_SER no longer exists.) Shares sendState()'s
   *  formatter, so pendingReply_ (REPLY=…) rides the line exactly once. */
  void sendStateOn(CommInterface& comm);
  void setStatusText(const char* s);
  /** Whitelisted protocol status word for the current status text (F-23). */
  void getStatusWord(char* buf, size_t bufLen) const;

private:
  const char* modeToString(Mode m) const;
  bool isConnectedNow() const;

  MotorControl* motor_;
  CommInterface* comm_;
  CommInterface* auxComm_;
  CommInterface* relayComm_;  // U-94 Phase 2. Null unless setRelayComm() ran.
  OledDisplay* oled_;

  Mode currentMode_;
  uint32_t lastCommandTimeMs_;
  uint16_t speed_;

  // v1.0.1: two-part link context for the OLED ("method|type").
  static constexpr size_t LINK_BUF_LEN = 40;
  char linkContext_[LINK_BUF_LEN];
  static constexpr size_t EXTRA_INFO_LEN = 64;
  static constexpr size_t STATUS_TEXT_LEN = 64;
  char extraInfo_[EXTRA_INFO_LEN];
  mutable char statusText_[STATUS_TEXT_LEN];

  bool failsafeActive_;
  uint32_t failsafeStartMs_;
  bool readyDisplayed_;

  bool lastConnected_;

  uint32_t lastOledMs_;
  char lastOledStatus_[STATUS_TEXT_LEN];

  uint32_t lastStateMs_;

  SystemCmdFn systemCmdHook_;

  // v1.4.0: reply text produced by the system-command hook (WIFICFG replies).
  // sendState() appends it as REPLY=<text> on the next STATE line when set.
  //
  // D7 (U-68, 2026-10-02): this was 64 bytes while the hook builds its reply
  // in 160 — so a `ROUTERS;<ownAP>;<ssid>…` list was SILENTLY TRUNCATED at 63
  // characters, right where it matters: with two or three saved routers the
  // answer stopped mid-list. Nobody noticed because the app discarded the
  // reply entirely (that was D2). Now that the answer is consumed, a truncated
  // one would be a wrong one, so the buffer matches the largest reply the hook
  // can build (a full saved-router list: 6 x 32 + separators + the own AP).
  //
  // It rides ONE STATE line, so the STATE builder's own buffer must be able to
  // hold it — see sendStateOn()'s `buf`.
  char pendingReply_[192];

  // T-13: debounced NVS persistence (mode + speed). scheduleModeSpeedPersist()
  // is called from every path that modifies the authoritative mode/speed;
  // flushModeSpeedPersistIfDue() runs once per loop and writes only when the
  // debounce window has elapsed. Mirrors the Genum_2WD1M_CAR reference.
  void scheduleModeSpeedPersist();
  void flushModeSpeedPersistIfDue();
  bool pendingModeSpeedSave_;
  uint32_t modeSpeedSaveMs_;
  static constexpr unsigned long MODE_SPEED_SAVE_DEBOUNCE_MS = 1000;

  NowFn nowFn_;
};