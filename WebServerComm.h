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
  const char* storedSsid() const { return storedSsid_; }
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
  // Reply produced by the last consumed WIFICFG line ("" when none).
  const String& lastWifiReply() const { return lastWifiReply_; }
  void clearLastWifiReply() { lastWifiReply_ = ""; }

  bool isRunning() const;

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