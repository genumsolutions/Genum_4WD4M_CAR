#pragma once

// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: WebServerComm.h — WiFi web + WebSocket server transport: command
//       queue bridge, JSON status broadcast, WIFICFG provisioning
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include "CommInterface.h"
#include "ModeManager.h"
#include "Config.h"
#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Preferences.h>
#include <vector>

#define CMD_QUEUE_LEN 8
#define CMD_BUF_LEN 128
#define WS_PORT 81
#define WIFI_RETRY_INTERVAL_MS 10000ul  // F-20a: WiFi retry backoff
// FIN-40/D2 (T-71a FAIL, 2026-09-18 device gate): CAPPED STA retries. The
// ESP32's default auto-reconnect (and any disconnect/begin churn) drives STA
// scans that blank the softAP beacon, so an unreachable saved router must not
// be retried forever. Auto-reconnect is OFF (beginStation/rejoinNetwork) and
// only STA_MAX_RETRIES explicit attempts (spaced WIFI_RETRY_INTERVAL_MS) run;
// after that the car settles AP-ONLY so 4WDCar_Wifi stays rock-solid for
// phone joins. A later ROUTERS;USE / WIFICFG / mode re-entry starts STA fresh.
// R-16 (2026-09-18): with the own AP now the boot default (no auto-join on
// mode entry), this budget only ever applies to EXPLICIT user joins
// (ROUTERS;USE / WIFICFG) — a deliberate request earns a fuller budget
// (6 × 10 s) before the AP-only settle.
#define STA_MAX_RETRIES 6
// FIN-40/D1: retry cadence for a softAP() that failed to start (radio busy).
#define AP_RETRY_INTERVAL_MS 2000ul
// D3 (U-68): the car-side nearby-network scan. SCAN_SETTLE_MS is the pause
// between "the command asked" and "the radio may be asked" — the radio can
// still be finishing an association, and scanning into that returns nothing.
// SCAN_TIMEOUT_MS bounds the wait so a radio that never finishes reports
// honestly instead of leaving the app waiting. SCAN_MAX_RESULTS caps both the
// kept list and the JSON fragment so the status buffer stays bounded.
// U-71: the boot auto-join gets a SHORT window and ONE retry. It must fall back
// to the car's own hotspot quickly and CERTAINLY: the hotspot is the only way
// in when the router is absent, so a boot must never leave the car unreachable.
#define BOOT_STA_TIMEOUT_MS 6000ul
#define BOOT_STA_MAX_RETRIES 1
// U-76: how often a DEFERRED mDNS publication is retried once the STA settles.
#define MDNS_RETRY_INTERVAL_MS 2000ul
#define SCAN_SETTLE_MS 400ul
#define SCAN_TIMEOUT_MS 8000ul
#define SCAN_MAX_RESULTS 12
// T-48 (round-5): saved-router registry — the car is the source of truth.
// Max 6 routers (ssid ≤32 / pass ≤64, no ';' in either — split-rule parity
// with WIFICFG). Names sync OUT only; passwords never leave the car's NVS.
// T-62 (round-9): ROUTERS;CLEAR wipes the whole user registry + stored pair.
// T-66 (round-9): the car's OWN network (WIFI_AP_NAME = 4WDCar_Wifi) is a
// protected virtual entry — always FIRST in ROUTERS/NETW/JSON lists while in
// webserver mode, never stored, never deletable; DEL/CLEAR refuse it and
// USE on it reverts the car to its own AP as the default network.
#define MAX_SAVED_ROUTERS 6
#define ROUTER_SSID_LEN 33   // 32 + NUL
#define ROUTER_PASS_LEN 65   // 64 + NUL

class WebServerComm : public CommInterface {
public:
  WebServerComm();
  ~WebServerComm();

  void init();
  bool begin();
  bool beginStation();
  // v2 (owner plan 2026-09-28, Phase 1): ALWAYS-ON web layer — bring up the
  // own AP + HTTP/WS servers once at boot and leave them running for the
  // whole uptime. The donor tied this lifecycle to ESP_SER (SERVER) mode;
  // v2 has no mode-gated transport at all (stop() remains for shutdown/OTA
  // style use, never called from loop()).
  void beginAlwaysOn();
  // T-34: force a disconnect + reconnect to the CURRENT stored credentials
  // (new WIFICFG pair). Fixes re-provisioning while already in ESP_SER: the
  // rejoin previously sat behind the edge-triggered mode-change check + the
  // WL_CONNECTED retry guard, so the new network only applied on the NEXT
  // mode cycle (owner report 2026-09-15). Non-blocking like beginStation();
  // handle() resolves the pending STA connect (success or AP fallback).
  void rejoinNetwork();
  void stop();
  void handle();
  void setModeManager(ModeManager* mm);

  // ---- WiFi provisioning (v1.4.0: app → BT → car) ----
  // The app sends `WIFICFG;<ssid>;<password>` over the Bluetooth SPP link
  // while the car is in BT mode. The car stores the pair in Preferences
  // (namespace "botcfg"), then switches itself to ESP_SER to join the
  // router and host its web page. `WIFICFG?` asks the car for the stored
  // SSID (never the password) for the app's pre-fill display.
  bool handleWifiConfigCommand(const char* cmd);
  // T-48 (round-5): saved-router registry commands — ROUTERS;LIST /
  // ROUTERS;ADD;<ssid>;<pass> / ROUTERS;USE;<ssid> / ROUTERS;DEL;<ssid>.
  // True when the line was a ROUTERS command (consumed). USE stores the pair
  // as the ACTIVE credentials + rejoinNetwork() (non-blocking, servers survive).
  bool handleRouterCommand(const char* cmd);
  // Load stored credentials (falling back to the compile-time constants)
  // into the module-level WIFI_SSID/WIFI_PASSWORD buffers.
  void loadStoredWifi();
  // T-48: load the saved-router NVS registry at boot (contiguous keys
  // rnet0s/rnet0p …); promotes the stored active pair (wifi_ssid) when it is
  // not already present so the live network always shows in the list.
  void loadSavedRouters();
  // T-49: broadcast the registry names-only. SPP line: "NETW;<s1>;<s2>;…"
  // (fed into the remote's saved-networks menu). WS JSON: networks:[...].
  void buildNetworksLine(char* buf, size_t bufLen);
  void buildNetworksJson(char* buf, size_t bufLen);
  /**
   * F-68 (U-68): the acknowledgement form of a command line, with every
   * credential stripped. `ROUTERS;ADD;<ssid>;<pass>` -> `ROUTERS;ADD;<ssid>`.
   * An ack confirms the request; it must never replay it, because the raw line
   * travels back over WebSocket (and over HTTP, where it is also broadcast to
   * every WS client) and would otherwise sit in any proxy log in plain text.
   */
  static void redactForAck(const char* cmd, char* out, size_t outLen);

  /**
   * D3 (U-68, 2026-10-02): the car-side nearby-network scan.
   *
   * `ROUTERS;SCAN` existed in the app since the Connections-Hub round and the
   * car answered `ROUTERS;ERROR;Syntax` every time — there was no "networks
   * near the car" list anywhere in the system. This is it.
   *
   * ASYNCHRONOUS ON PURPOSE: `WiFi.scanNetworks()` blocks for seconds. Called
   * from the command path it would starve loop() and trip the task watchdog
   * (F-24), rebooting the car — the exact failure F-60 was written to stop.
   * So the command only ARMS the scan; handle() runs and collects it.
   */
  void startScan();
  void runScanIfDue();
  void collectScanIfDone();
  /** SPP form: `SCAN;<ssid>,<rssi>,<1|0>;…` or `SCAN;NONE`. */
  void buildScanLine(char* buf, size_t bufLen) const;
  /** WS JSON form: `[{"ssid":…,"rssi":…,"open":true|false},…]`. */
  size_t buildScanJson(char* buf, size_t bufLen) const;
  int savedRouterCount() const { return savedRouterCount_; }
  const char* savedRouterSsid(int i) const {
    return (i >= 0 && i < MAX_SAVED_ROUTERS) ? savedRouterSsid_[i] : "";
  }
  const char* savedRouterPass(int i) const {
    return (i >= 0 && i < MAX_SAVED_ROUTERS) ? savedRouterPass_[i] : "";
  }
  // True when runtime credentials have been stored via WIFICFG.
  bool hasStoredWifi() const { return hasStoredWifi_; }
  // SSID currently stored ("" when none) — safe to show in the app.
  //
  // U-70 (2026-10-02): THIS IS THE ROUTER WE ASKED FOR, NOT THE ONE WE ARE ON.
  // Reporting it as "the network the car is on" is a lie the moment a join
  // fails - exactly what the owner saw: the car displayed the new router's name
  // while it never left its own hotspot. Anything that claims to describe the
  // CURRENT network must use joinedSsid() instead (U-70 rule 1).
  //
  // The two are different questions and must stay separate: "which router did
  // the user ask for" vs "which router are we on".
  const char* storedSsid() const { return storedSsid_; }
  // The network the car is ACTUALLY on: the joined STA SSID when associated,
  // the car's own broadcast id on its hotspot, "" when neither. The only value
  // that may be presented as the current network.
  String joinedSsid() const;

  // ---- U-71 (2026-10-02): the deferred rejoin state machine ------------
  // (U-70 deferred only the `WiFi.begin()` via a `staBeginPending_` flag; U-71
  // replaces that entirely, because deferring one call still left `WiFi.mode()`
  // and `softAP()` running inside the command path.)
  // rejoinNetwork() does NO radio work; it only arms this, and handle()
  // advances it with ONE radio call per loop pass. That is what stops a router
  // switch from occupying loop() long enough to trip the 5 s task watchdog —
  // and a WDT trip is a reboot (F-24), which is the most plausible cause of
  // "the car restarted as soon as I tried to access the webpage".
  enum RejoinStep : uint8_t {
    REJOIN_STEP_IDLE = 0,
    REJOIN_STEP_MODE,   // WiFi.mode(WIFI_AP_STA) if needed
    REJOIN_STEP_AP,     // startOwnAP() — the hotspot stays up throughout
    REJOIN_STEP_BEGIN,  // WiFi.begin(ssid, pass)
  };
void advanceRejoinStep();
  uint8_t rejoinStep_;

  // U-75 (2026-10-02): OWNER HARD EVIDENCE - the device finally told us why.
  //
  //   [BOOT] reset reason: 6 (TASK WATCHDOG (loop hung >5s))
  //
  // That is ESP_RST_TASK_WDT: `loop()` was blocked for more than 5 s, so the
  // watchdog fired and its action IS a reboot. It is not a crash and not a
  // brownout — which is exactly what had been suspected for weeks. The fault is
  // a BLOCKING CALL IN LOOP.
  //
  // `advanceRejoinStep()` above already solved this for the switch path: one
  // radio call per pass, loop() returns between steps. The one place that still
  // violated it was the GIVE-UP fallback, which ran, in a single pass:
  //
  //     WiFi.disconnect(); WiFi.mode(WIFI_AP); startOwnAP();
  //
  // i.e. a teardown plus a full WiFi driver RESTART plus a softAP bring-up,
  // back to back. `WiFi.mode()` is the expensive one — it stops and restarts the
  // driver — and doing that while an association is being torn down is the most
  // expensive case there is.
  //
  // U-71 made this path far more likely to be taken: boot auto-join gives every
  // power-up ONE short attempt at the last-good router, so an unreachable router
  // now reaches the give-up branch on EVERY boot rather than only after a user
  // asked for a switch. The hazard and the new exposure arrived together.
  //
  // So the same treatment: one radio call per pass, loop() fed in between.
  enum GiveUpStep : uint8_t {
    GIVEUP_IDLE = 0,
    GIVEUP_DISCONNECT,  // WiFi.disconnect()
    GIVEUP_MODE,        // WiFi.mode(WIFI_AP) — the driver restart, on its own
    GIVEUP_AP,          // startOwnAP() — softAPConfig() + softAP()
  };
  void giveUpStepMachine();
  uint8_t giveUpStep_;

  // U-75: name the blocking step BEFORE it runs. A hang produces no serial
  // output of its own, so the last breadcrumb printed is the step that hung —
  // which turns the next occurrence from a hunt into a diagnosis. This is the
  // cheap half of the fix; splitting the passes above is the real one.
  void noteRadioStep(const char* step);
  // U-76: STA busy = connecting OR connected (DHCP included). While true, no
  // driver/netif config set is legal - see startOwnAP().
  bool staBusy() const;
  // U-76: mDNS deferred until the STA settles, then retried by handle().
  bool mdnsPending_;
  unsigned long lastMdnsRetryMs_;

  // Remember the router that ACTUALLY associated, so a boot can retry it and a
  // failed switch cannot overwrite a working router with a broken one.
  void saveLastGoodRouter();
  void armBootAutoJoin();
  char lastGoodSsid_[ROUTER_SSID_LEN];
  char lastGoodPass_[ROUTER_PASS_LEN];

  // Per-attempt association window and the retry cap. Both are shorter for the
  // boot auto-join than for an explicit user switch, so an unreachable router
  // never keeps the car off its own hotspot for long (the reason R-16 existed).
  unsigned long staTimeoutMs_;
  uint8_t staRetryCap_;
  // R-12: the AP fallback broadcast id (4WDCar_Wifi, T-56) once started;
  // empty before.
  const char* apBroadcastId() const { return apName_.length() > 0 ? apName_.c_str() : ""; }
  // R-13 (owner matrix 2026-09-15): the car's current reachable IP — the
  // joined station IP when WiFi is up, else the AP fallback (192.168.245.1
  // since FIN-48 — previously the SDK default 192.168.4.1).
  // Empties buf when neither is active. Rides `;IP=` on STATE lines and the
  // status JSON `ip` for the OLED / hand-held remote / app to display.
  void currentIp(char* buf, size_t bufLen);
  // R-12: ModeManager::sendState() reads WiFi truth (SSID / AP id) for STATE lines.
  static WebServerComm* instance() { return instanceForWs; }
  // Connections-Hub (2026-09-28): the car's stable BOARD identity — the last
  // 6 hex digits of ESP.getEfuseMac() (uppercase), e.g. "1A2B3C". This is the
  // per-device profile key (`fw:<id>`) the app uses for WiFi-only cars. Rides
  // `;ID=` on STATE lines and the status JSON `id`. Stable across renames.
  static void boardIdHex(char* out, size_t outLen);
  // Reply produced by the last consumed WIFICFG line ("" when none).
  const String& lastWifiReply() const { return lastWifiReply_; }
  void clearLastWifiReply() { lastWifiReply_ = ""; }

  bool isRunning() const;
  // v1.0.1 (owner OLED directive 2026-09-28): differentiated link truth for
  // the dashboard's Link/Type rows. hasWsClients() = a controller is linked
  // over the WebSocket (the ACTIVE control path); isStaJoined() = the STA
  // side joined a router (server reachable on the LAN even with no client).
  // (Non-const: WebSocketsServer::connectedClients() is not const-safe.)
  bool hasWsClients();
  bool isStaJoined() const;

  // CommInterface overrides
  size_t available() override;
  int read() override;
  size_t readBytes(char* buf, size_t len, unsigned long timeoutMs = 0) override;
  int peek() override;
  size_t write(const char* buf, size_t len) override;
  bool send(const char* msg) override;
  void flush() override;
  bool connected() override;

  // Queue helpers
  bool queuePush(const char* s);
  bool queuePopChar(char* out);
  void queueClear();

  // Public wrapper to refresh extra info immediately
  void refreshExtraInfo();

  // ---- U-74 (2026-10-02): mDNS advertisement --------------------------
  // The car advertises itself as `genum-car.local` on whatever network it is
  // on, re-publishing on every network change. This exists because of a
  // STRUCTURAL consequence of AP_STA, not a UI bug: the softAP and the STA
  // share one radio and therefore one channel, so when the car joins a router
  // its hotspot MOVES to that router's channel and the phone's hotspot link
  // drops. With no link the app has no address for the car, so the drive deck
  // cannot open. mDNS is what makes the car findable BY NAME once the phone is
  // on the router - the alternative being the user reading an IP off the OLED
  // and typing it.
  //
  // NOTE for the app side: React Native cannot resolve a `.local` name without a
  // NATIVE MODULE, and a native module cannot be delivered by an OTA - it needs
  // a new APK. The advertisement is therefore the prerequisite half; the
  // resolver is queued behind that APK (see the app ledger). Until then the
  // phone can still reach the car by the address the OLED shows.
  static constexpr const char* MDNS_HOSTNAME = "genum-car";
  void startMdns();
  void stopMdns();
  bool mdnsRunning_;

private:
  void handleRoot();
  void handleMove(const char* cmd);
  void handleSpeed();
  void handleMode();
  void handleStatus();
  void handleNotFound();
  void buildStatusJson(char* buf, size_t bufLen);

  void setupWebSocket();
  void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
  void broadcastStatus();

  char cmdQueue[CMD_QUEUE_LEN][CMD_BUF_LEN];
  size_t cmdHead;
  size_t cmdTail;
  size_t cmdCount;
  size_t readPos;

  // F-20a: non-blocking STA connect + guarded retry. connectingPending_ =
  // a begin()/rejoinNetwork() is in flight (resolved by handle() on connect
  // or WIFI_STA_TIMEOUT_MS → AP fallback); pendingRejoin_ (T-34) forces
  // reconnection to the NEW credentials even while WiFi still reports
  // WL_CONNECTED to the OLD network.
  bool connectingPending_;
  bool pendingRejoin_;
  unsigned long connectStartMs_;
  unsigned long lastWifiRetryMs_;
  // FIN-40/D2: capped-STA-retry bookkeeping (see STA_MAX_RETRIES above).
  uint8_t staRetryCount_;
  bool staGiveUpLogged_;
  // FIN-40/D1: retry cadence state for a softAP() that failed to start.
  unsigned long lastApRetryMs_;

  WebServer server{80};          // static object
  WebSocketsServer ws{WS_PORT};  // static object
  bool serverRunning;
  bool startedAsAP;
  // T-65 (round-9): the car's OWN WiFi (4WDCar_Wifi) stays broadcast for
  // the WHOLE webserver session — even while a saved router is joined (AP_STA)
  // — so the hotspot is never hidden. apRunning_ = the softAP interface is up.
  bool apRunning_;

  ModeManager* modeManager;

  // ---- WiFi provisioning state ----
  bool hasStoredWifi_;
  char storedSsid_[33];   // WiFi SSID max = 32 bytes + NUL
  char storedPass_[65];   // WiFi WPA2 password max = 64 bytes + NUL
  // T-48: saved-router registry (the car is the source of truth; passwords stay
  // in NVS `botcfg` and are never printed / sent off-device).
  char savedRouterSsid_[MAX_SAVED_ROUTERS][ROUTER_SSID_LEN];
  char savedRouterPass_[MAX_SAVED_ROUTERS][ROUTER_PASS_LEN];
  int savedRouterCount_;
  // Last WIFICFG reply, consumed by ModeManager's system-command hook so the
  // answer reaches the app over whichever comm is active.
  String lastWifiReply_;

  // D3 (U-68): the nearby-network scan. One pass at a time, armed by the
  // command and driven by handle(); scanNetworks_ survives until the next scan
  // so the app can read it at leisure (and so a passive status broadcast keeps
  // carrying it). scanCount_ is the raw pass count (diagnostics only).
  struct ScannedNetwork {
    char ssid[33];
    int32_t rssi;
    bool open;
  };
  bool scanPending_;
  bool scanBusy_;
  unsigned long scanStartMs_;
  unsigned long scanWaitMs_;
  int scanCount_;
  std::vector<ScannedNetwork> scanNetworks_;
  // AP fallback broadcast id ("4WDCar_Wifi", T-56), shown on the OLED and
  // in the status JSON ("ap") so the app/website can display it.
  String apName_;

  char pendingModeBuf[32];
  unsigned long pendingModeAtMs;

  void updateExtraInfo();

  // T-65 (round-9) + v2: bring the car's own hotspot (4WDCar_Wifi) up and
  // keep apRunning_/apName_ consistent. Idempotent; retried by handle() on a
  // softAP failure (FIN-40/D1) — the AP is up for the WHOLE uptime in v2.
  void startOwnAP();
  // T-66 (round-9) + v2: the reserved own-AP entry is ALWAYS listed — the
  // web layer (and its routers list) is always-on, no longer SERVER-scoped.
  bool showReservedEntry() const;

  static WebServerComm* instanceForWs;
};