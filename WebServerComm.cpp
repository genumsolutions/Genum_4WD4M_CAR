// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: WebServerComm.cpp — web + WebSocket server transport: command
//       queue bridge, JSON status broadcast, WIFICFG provisioning
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include "WebServerComm.h"
#include "WebPage.h"
#include <WiFi.h>
#include <pgmspace.h>

WebServerComm* WebServerComm::instanceForWs = nullptr;

// Connections-Hub: the stable board identity — last 6 hex of ESP.getEfuseMac()
// in uppercase. Never changes across renames/AP-join/AP-fallback state, which
// is exactly what the app needs to key a per-device profile on.
void WebServerComm::boardIdHex(char* out, size_t outLen) {
  if (!out || outLen == 0) return;
  uint64_t mac = ESP.getEfuseMac();
  // 0xFFFFFF keeps only the (low-address) bytes that differ per board — the
  // ESP32 OUI bytes are identical fleet-wide, so the last 6 hex DO identify.
  snprintf(out, outLen, "%06X", static_cast<unsigned int>(mac & 0xFFFFFF));
}

WebServerComm::WebServerComm()
  : cmdHead(0), cmdTail(0), cmdCount(0), readPos(0),
  connectingPending_(false), pendingRejoin_(false), connectStartMs_(0), lastWifiRetryMs_(0),
  staRetryCount_(0), staGiveUpLogged_(false), lastApRetryMs_(0),
  scanPending_(false), scanBusy_(false), scanStartMs_(0), scanWaitMs_(0), scanCount_(0),
    serverRunning(false), startedAsAP(false), apRunning_(false),
    modeManager(nullptr), pendingModeAtMs(0)
{
  for (size_t i = 0; i < CMD_QUEUE_LEN; ++i) cmdQueue[i][0] = '\0';
  pendingModeBuf[0] = '\0';
  hasStoredWifi_ = false;
  storedSsid_[0] = '\0';
  storedPass_[0] = '\0';
  // T-48: saved-router registry starts empty (loaded in setup() once NVS is
  // live — same timing rule as loadStoredWifi(), T-33).
  savedRouterCount_ = 0;
  for (int i = 0; i < MAX_SAVED_ROUTERS; ++i) {
    savedRouterSsid_[i][0] = '\0';
    savedRouterPass_[i][0] = '\0';
  }
  // T-33: DO NOT loadStoredWifi() here — global constructors run BEFORE the
  // Arduino core calls nvs_flash_init() (right before setup()), so a
  // Preferences read at ctor time always fails and the car reverts to the
  // compile-time default credentials after every power cycle (owner report
  // 2026-09-15). setup() calls loadStoredWifi() once NVS is live.
}

// ---- WiFi provisioning (v1.4.0: app → BT → car) ----
// Consumed lines (ModeManager routes them here via the system-command hook):
//   WIFICFG;<ssid>;<password>  store credentials, reply WIFICFG;STORED;<ssid>
//                              and switch to ESP_SER (router join + web page)
//   WIFICFG?                   reply WIFICFG;SSID;<stored ssid> (never the password)
//   WIFICFG;RESET              erase stored credentials → compile-time fallback
bool WebServerComm::handleWifiConfigCommand(const char* cmd) {
  if (!cmd) return false;
  if (strncasecmp(cmd, "WIFICFG", 7) != 0) return false;

  char reply[96];
  const char* rest = cmd + 7;

  if (rest[0] == '?' || rest[0] == '\0') {
    // T-64: never leak the compile-time placeholder — with nothing stored the
    // answer is empty (the car's own WiFi is not a router credential).
    snprintf(reply, sizeof(reply), "WIFICFG;SSID;%s", hasStoredWifi_ ? storedSsid_ : "");
  } else if (strncasecmp(rest, ";RESET", 6) == 0) {
    Preferences prefs;
    prefs.begin("botcfg", false);
    prefs.remove("wifi_ssid");
    prefs.remove("wifi_pass");
    prefs.end();
    hasStoredWifi_ = false;
    storedSsid_[0] = '\0';
    storedPass_[0] = '\0';
    snprintf(reply, sizeof(reply), "WIFICFG;CLEARED");
  } else if (rest[0] == ';') {
    // WIFICFG;<ssid>;<password> — password may be empty (open network).
    const char* ssid = rest + 1;
    const char* semi = strchr(ssid, ';');
    const char* pass = semi ? semi + 1 : "";
    size_t ssidLen = semi ? static_cast<size_t>(semi - ssid) : strlen(ssid);
    if (ssidLen == 0 || ssidLen > 32) {
      snprintf(reply, sizeof(reply), "WIFICFG;ERROR;SSID length");
    } else if (strlen(pass) > 64) {
      snprintf(reply, sizeof(reply), "WIFICFG;ERROR;Password too long");
    } else {
      strncpy(storedSsid_, ssid, ssidLen);
      storedSsid_[ssidLen] = '\0';
      strncpy(storedPass_, pass, sizeof(storedPass_) - 1);
      storedPass_[sizeof(storedPass_) - 1] = '\0';
      Preferences prefs;
      prefs.begin("botcfg", false);
      prefs.putString("wifi_ssid", storedSsid_);
      prefs.putString("wifi_pass", storedPass_);
      prefs.end();
      hasStoredWifi_ = true;
      FW_LOG_VERBOSE("[WIFICFG] stored SSID=\"%s\"\n", storedSsid_);
      snprintf(reply, sizeof(reply), "WIFICFG;STORED;%s", storedSsid_);
      // v2: WIFICFG stores the pair and rejoins — it NO LONGER switches the
      // car into ESP_SER (that mode is retired; the web layer is always-on
      // and handles the join from any mode, drive or not).
      if (modeManager) {
        rejoinNetwork();
      }
    }
  } else {
    snprintf(reply, sizeof(reply), "WIFICFG;ERROR;Syntax");
  }

  lastWifiReply_ = reply;
  return true;
}

void WebServerComm::loadStoredWifi() {
  Preferences prefs;
  prefs.begin("botcfg", true);
  String ssid = prefs.getString("wifi_ssid", "");
  String pass = prefs.getString("wifi_pass", "");
  prefs.end();
  if (ssid.length() > 0 && ssid.length() <= 32) {
    ssid.toCharArray(storedSsid_, sizeof(storedSsid_));
    pass.toCharArray(storedPass_, sizeof(storedPass_));
    hasStoredWifi_ = true;
  } else {
    hasStoredWifi_ = false;
    storedSsid_[0] = '\0';
    storedPass_[0] = '\0';
  }
}

// T-48: load the saved-router registry from NVS (contiguous "rnet<N>s"/"rnet<N>p"
// keys), then ensure the active stored pair appears so the live network always
// shows in the list. Names + passwords only; nothing is ever printed.
void WebServerComm::loadSavedRouters() {
  savedRouterCount_ = 0;
  for (int i = 0; i < MAX_SAVED_ROUTERS; ++i) {
    savedRouterSsid_[i][0] = '\0';
    savedRouterPass_[i][0] = '\0';
  }
  Preferences prefs;
  prefs.begin("botcfg", true);
  for (int i = 0; i < MAX_SAVED_ROUTERS; ++i) {
    static const char* sKeys[] = { "rnet0s", "rnet1s", "rnet2s", "rnet3s", "rnet4s", "rnet5s" };
    static const char* pKeys[] = { "rnet0p", "rnet1p", "rnet2p", "rnet3p", "rnet4p", "rnet5p" };
    String s = prefs.getString(sKeys[i], "");
    if (s.length() == 0 || s.length() >= ROUTER_SSID_LEN) break; // contiguous until first gap
    s.toCharArray(savedRouterSsid_[i], ROUTER_SSID_LEN);
    String p = prefs.getString(pKeys[i], "");
    p.toCharArray(savedRouterPass_[i], ROUTER_PASS_LEN);
    ++savedRouterCount_;
  }
  // Promote the stored active pair (wifi_ssid) up front so the connected
  // router always appears in the saved list even after a WIFICFG-only setup.
  if (hasStoredWifi_ && storedSsid_[0]) {
    bool found = false;
    for (int i = 0; i < savedRouterCount_; ++i) {
      if (strcmp(savedRouterSsid_[i], storedSsid_) == 0) { found = true; break; }
    }
    if (!found && savedRouterCount_ < MAX_SAVED_ROUTERS) {
      strncpy(savedRouterSsid_[savedRouterCount_], storedSsid_, ROUTER_SSID_LEN - 1);
      strncpy(savedRouterPass_[savedRouterCount_], storedPass_, ROUTER_PASS_LEN - 1);
      savedRouterSsid_[savedRouterCount_][ROUTER_SSID_LEN - 1] = '\0';
      savedRouterPass_[savedRouterCount_][ROUTER_PASS_LEN - 1] = '\0';
      ++savedRouterCount_;
      // Persist the promoted entry so the promotion survives reboots.
      static const char* sKeys[] = { "rnet0s", "rnet1s", "rnet2s", "rnet3s", "rnet4s", "rnet5s" };
      static const char* pKeys[] = { "rnet0p", "rnet1p", "rnet2p", "rnet3p", "rnet4p", "rnet5p" };
      int i = savedRouterCount_ - 1;
      prefs.putString(sKeys[i], savedRouterSsid_[i]);
      prefs.putString(pKeys[i], savedRouterPass_[i]);
    }
  }
  prefs.end();
  FW_LOG_VERBOSE("[ROUTERS] %d saved router(s) loaded\n", savedRouterCount_);
}

// T-48: registry command dispatcher (rides the system-command hook like
// WIFICFG). Returns true for every ROUTERS;... line. Per FAILSAFE W-14 these
// are SYSTEM commands — they can switch the network but never drive.
bool WebServerComm::handleRouterCommand(const char* cmd) {
  if (!cmd) return false;
  if (strncasecmp(cmd, "ROUTERS", 7) != 0) return false;

  char reply[160] = "";
  const char* rest = cmd + 7;

  if (rest[0] == '\0' || rest[0] == ';' && (rest[1] == '\0' || strncasecmp(rest, ";LIST", 5) == 0)) {
    // ROUTERS / ROUTERS;LIST — names only, never passwords. T-66: the car's
    // OWN network leads the list first while in webserver mode (protected).
    strcpy(reply, "ROUTERS");
    if (showReservedEntry()) {
      strcat(reply, ";");
      strcat(reply, WIFI_AP_NAME);
    }
    for (int i = 0; i < savedRouterCount_; ++i) {
      strcat(reply, ";");
      strcat(reply, savedRouterSsid_[i]);
    }
  } else if (rest[0] == ';' && strncasecmp(rest, ";ADD;", 5) == 0) {
    const char* ssid = rest + 5;
    const char* semi = strchr(ssid, ';');
    const char* pass = semi ? semi + 1 : "";
    size_t ssidLen = semi ? static_cast<size_t>(semi - ssid) : strlen(ssid);
    if (ssidLen == 0 || ssidLen >= ROUTER_SSID_LEN) {
      snprintf(reply, sizeof(reply), "ROUTERS;ERROR;SSID length");
    } else if (strcasecmp(ssid, WIFI_AP_NAME) == 0) {
      // T-66: the car's own network is not a router you add to the list.
      snprintf(reply, sizeof(reply), "ROUTERS;ERROR;Reserved");
    } else if (strlen(pass) >= ROUTER_PASS_LEN) {
      snprintf(reply, sizeof(reply), "ROUTERS;ERROR;Password too long");
    } else {
      int slot = -1;
      for (int i = 0; i < savedRouterCount_; ++i) {
        if (strlen(savedRouterSsid_[i]) == ssidLen &&
            strncmp(savedRouterSsid_[i], ssid, ssidLen) == 0) { slot = i; break; }
      }
      if (slot < 0 && savedRouterCount_ >= MAX_SAVED_ROUTERS) {
        snprintf(reply, sizeof(reply), "ROUTERS;FULL");
      } else {
        if (slot < 0) slot = savedRouterCount_++;
        strncpy(savedRouterSsid_[slot], ssid, ssidLen);
        savedRouterSsid_[slot][ssidLen] = '\0';
        strncpy(savedRouterPass_[slot], pass, ROUTER_PASS_LEN - 1);
        savedRouterPass_[slot][ROUTER_PASS_LEN - 1] = '\0';
        Preferences prefs;
        prefs.begin("botcfg", false);
        static const char* sKeys[] = { "rnet0s", "rnet1s", "rnet2s", "rnet3s", "rnet4s", "rnet5s" };
        static const char* pKeys[] = { "rnet0p", "rnet1p", "rnet2p", "rnet3p", "rnet4p", "rnet5p" };
        prefs.putString(sKeys[slot], savedRouterSsid_[slot]);
        prefs.putString(pKeys[slot], savedRouterPass_[slot]);
        prefs.end();
        FW_LOG_VERBOSE("[ROUTERS] added SSID=\"%s\" (slot %d)\n", savedRouterSsid_[slot], slot);
        snprintf(reply, sizeof(reply), "ROUTERS;ADDED;%s", savedRouterSsid_[slot]);
      }
    }
  } else if (rest[0] == ';' && strncasecmp(rest, ";USE;", 5) == 0) {
    const char* ssid = rest + 5;
    if (strcasecmp(ssid, WIFI_AP_NAME) == 0) {
      // T-66: "USE 4WDCar_Wifi" = switch back to the car's own network
      // (nothing stored → rejoin to the own AP as the default).
      hasStoredWifi_ = false;
      storedSsid_[0] = '\0';
      storedPass_[0] = '\0';
      Preferences prefs;
      prefs.begin("botcfg", false);
      prefs.remove("wifi_ssid");
      prefs.remove("wifi_pass");
      prefs.end();
      snprintf(reply, sizeof(reply), "ROUTERS;USED;%s", WIFI_AP_NAME);
      if (serverRunning) rejoinNetwork();
      else updateExtraInfo();
    } else {
      // First check if the ssid is already in the saved routers list
      int slot = -1;
      for (int i = 0; i < savedRouterCount_; ++i) {
        if (strcmp(savedRouterSsid_[i], ssid) == 0) { slot = i; break; }
      }
      if (slot >= 0) {
        // Switch the ACTIVE pair to this saved router (the car holds its pass).
        strncpy(storedSsid_, savedRouterSsid_[slot], ROUTER_SSID_LEN - 1);
        strncpy(storedPass_, savedRouterPass_[slot], ROUTER_PASS_LEN - 1);
        storedSsid_[ROUTER_SSID_LEN - 1] = '\0';
        storedPass_[ROUTER_PASS_LEN - 1] = '\0';
        hasStoredWifi_ = true;
        Preferences prefs;
        prefs.begin("botcfg", false);
        prefs.putString("wifi_ssid", storedSsid_);
        prefs.putString("wifi_pass", storedPass_);
        prefs.end();
        FW_LOG_VERBOSE("[ROUTERS] USE SSID=\"%s\" (saved router)\n", storedSsid_);
        snprintf(reply, sizeof(reply), "ROUTERS;USED;%s", storedSsid_);
        // Server stays up during the hand-over (T-34 proven path).
        if (serverRunning) rejoinNetwork();
        else updateExtraInfo();
      } else {
        // ssid not in saved list — attempt to add it temporarily then switch
        const char* semi = strchr(ssid, ';');
        const char* pass = semi ? semi + 1 : "";
        size_t ssidLen = semi ? static_cast<size_t>(semi - ssid) : strlen(ssid);
        if (ssidLen == 0 || ssidLen >= ROUTER_SSID_LEN) {
          snprintf(reply, sizeof(reply), "ROUTERS;ERROR;SSID length");
        } else if (strlen(pass) >= ROUTER_PASS_LEN) {
          snprintf(reply, sizeof(reply), "ROUTERS;ERROR;Password too long");
        } else if (strcasecmp(ssid, WIFI_AP_NAME) == 0) {
          // T-66: the car's own network is protected — cannot be added to saved list
          snprintf(reply, sizeof(reply), "ROUTERS;ERROR;Reserved");
        } else {
          // Attempt to add this router to the saved list
          int newSlot = -1;
          if (savedRouterCount_ >= MAX_SAVED_ROUTERS) {
            snprintf(reply, sizeof(reply), "ROUTERS;FULL");
          } else {
            newSlot = savedRouterCount_++;
            strncpy(savedRouterSsid_[newSlot], ssid, ssidLen);
            savedRouterSsid_[newSlot][ssidLen] = '\0';
            strncpy(savedRouterPass_[newSlot], pass, ROUTER_PASS_LEN - 1);
            savedRouterPass_[newSlot][ROUTER_PASS_LEN - 1] = '\0';
            Preferences prefs;
            prefs.begin("botcfg", false);
            static const char* sKeys[] = { "rnet0s", "rnet1s", "rnet2s", "rnet3s", "rnet4s", "rnet5s" };
            static const char* pKeys[] = { "rnet0p", "rnet1p", "rnet2p", "rnet3p", "rnet4p", "rnet5p" };
            prefs.putString(sKeys[newSlot], savedRouterSsid_[newSlot]);
            prefs.putString(pKeys[newSlot], savedRouterPass_[newSlot]);
            prefs.end();
            FW_LOG_VERBOSE("[ROUTERS] added SSID=\"%s\" (slot %d, on-the-fly)\n", savedRouterSsid_[newSlot], newSlot);
          }
          // Now switch to the newly added router
          if (newSlot >= 0) {
            strncpy(storedSsid_, savedRouterSsid_[newSlot], ROUTER_SSID_LEN - 1);
            strncpy(storedPass_, savedRouterPass_[newSlot], ROUTER_PASS_LEN - 1);
            storedSsid_[ROUTER_SSID_LEN - 1] = '\0';
            storedPass_[ROUTER_PASS_LEN - 1] = '\0';
            hasStoredWifi_ = true;
            FW_LOG_VERBOSE("[ROUTERS] USE SSID=\"%s\" (newly added)\n", storedSsid_);
            snprintf(reply, sizeof(reply), "ROUTERS;USED;%s", storedSsid_);
            // Server stays up during the hand-over (T-34 proven path). In BT mode
            // the pair is simply stored — the next ESP_SER begin() uses it.
            if (serverRunning) rejoinNetwork();
            else updateExtraInfo();
          } else {
            // Should not reach here, but fallback to error
            snprintf(reply, sizeof(reply), "ROUTERS;ERROR;Could not add router");
          }
        }
      }
    }
  } else if (rest[0] == ';' && strncasecmp(rest, ";DEL;", 5) == 0) {
    const char* ssid = rest + 5;
    if (strcasecmp(ssid, WIFI_AP_NAME) == 0) {
      // T-66: the car's own WiFi is protected — it can never be deleted.
      snprintf(reply, sizeof(reply), "ROUTERS;ERROR;Reserved");
    } else {
      bool removed = false;
      for (int i = 0; i < savedRouterCount_; ++i) {
        if (strcmp(savedRouterSsid_[i], ssid) == 0) {
          for (int j = i; j < savedRouterCount_ - 1; ++j) {
            strcpy(savedRouterSsid_[j], savedRouterSsid_[j + 1]);
            strcpy(savedRouterPass_[j], savedRouterPass_[j + 1]);
          }
          --savedRouterCount_;
          savedRouterSsid_[savedRouterCount_][0] = '\0';
          savedRouterPass_[savedRouterCount_][0] = '\0';
          removed = true;
          break;
        }
      }
      Preferences prefs;
      prefs.begin("botcfg", false);
      for (int i = 0; i < MAX_SAVED_ROUTERS; ++i) {
        static const char* sKeys[] = { "rnet0s", "rnet1s", "rnet2s", "rnet3s", "rnet4s", "rnet5s" };
        static const char* pKeys[] = { "rnet0p", "rnet1p", "rnet2p", "rnet3p", "rnet4p", "rnet5p" };
        if (i < savedRouterCount_) {
          prefs.putString(sKeys[i], savedRouterSsid_[i]);
          prefs.putString(pKeys[i], savedRouterPass_[i]);
        } else {
          prefs.remove(sKeys[i]);
          prefs.remove(pKeys[i]);
        }
      }
      prefs.end();
      if (!removed) {
        snprintf(reply, sizeof(reply), "ROUTERS;ERROR;Not saved:%s", ssid);
      } else {
        // If the deleted router was the ACTIVE pair, fall back to the car's
        // own WiFi (default). T-62: rejoin immediately so the car doesn't hang
        // on the removed network until the STA timeout.
        bool wasActive = (hasStoredWifi_ && strcmp(storedSsid_, ssid) == 0);
        if (wasActive) {
          hasStoredWifi_ = false;
          storedSsid_[0] = '\0';
          storedPass_[0] = '\0';
        }
        snprintf(reply, sizeof(reply), "ROUTERS;DELETED;%s", ssid);
        if (wasActive) {
          if (serverRunning) rejoinNetwork();
          else updateExtraInfo();
        }
      }
    }
  } else if (rest[0] == ';' && strncasecmp(rest, ";CLEAR", 6) == 0) {
    // T-62: wipe ALL saved routers + the stored active pair. The car's OWN
    // network (4WDCar_Wifi) is virtual/protected — it is never removed.
    savedRouterCount_ = 0;
    for (int i = 0; i < MAX_SAVED_ROUTERS; ++i) {
      savedRouterSsid_[i][0] = '\0';
      savedRouterPass_[i][0] = '\0';
    }
    Preferences prefs;
    prefs.begin("botcfg", false);
    for (int i = 0; i < MAX_SAVED_ROUTERS; ++i) {
      static const char* sKeys[] = { "rnet0s", "rnet1s", "rnet2s", "rnet3s", "rnet4s", "rnet5s" };
      static const char* pKeys[] = { "rnet0p", "rnet1p", "rnet2p", "rnet3p", "rnet4p", "rnet5p" };
      prefs.remove(sKeys[i]);
      prefs.remove(pKeys[i]);
    }
    prefs.remove("wifi_ssid");
    prefs.remove("wifi_pass");
    prefs.end();
    hasStoredWifi_ = false;
    storedSsid_[0] = '\0';
    storedPass_[0] = '\0';
    snprintf(reply, sizeof(reply), "ROUTERS;CLEARED");
    // The car reverts to its own AP as the network (default) right away.
    if (serverRunning) rejoinNetwork();
    else updateExtraInfo();
  } else if (rest[0] == ';' && strncasecmp(rest, ";SCAN", 5) == 0) {
    // D3 (U-68, 2026-10-02): ROUTERS;SCAN NOW EXISTS.
    //
    // The app has been sending this since the Connections-Hub round and the
    // car has answered `ROUTERS;ERROR;Syntax` every time — and the app then
    // ignored that answer, so the failure was invisible. There was no
    // "nearby networks" list to select from anywhere in the system, which is
    // the owner's "the selecting of the network is not proper".
    //
    // WiFi.scanNetworks() BLOCKS for seconds, so it CANNOT run here: this
    // handler runs inside the command path, and a multi-second block would
    // starve loop() and trip the task watchdog (F-24) — the car would reboot,
    // which is the one thing this whole subsystem exists to stop (F-60).
    //
    // So the scan is started ASYNCHRONOUSLY and the result is collected in
    // handle(). The reply is the honest acknowledgement; the RESULT arrives as
    // its own line, exactly like the saved-network list does.
    startScan();
    snprintf(reply, sizeof(reply), "ROUTERS;SCAN;STARTED");
  } else {
    snprintf(reply, sizeof(reply), "ROUTERS;ERROR;Syntax");
  }

  lastWifiReply_ = reply;
  return true;
}

// --- D3: the car-side nearby-network scan -------------------------------
//
// `WiFi.scanNetworks()` blocks for seconds, so it is never called from the
// command path (that would starve loop() and trip the task watchdog — F-24 —
// rebooting the car, which is exactly what F-60 forbids). Instead:
//   startScan()         arms a short settle delay, then kicks ONE non-blocking
//                       pass and remembers when it started;
//   runScanIfDue()      is that kick, called from handle();
//   collectScanIfDone() reads the result once the radio reports it finished,
//                       keeps the strongest reading per SSID, and broadcasts it.
//
// The result is delivered two ways, so both link types and both app parsers
// work without either being special:
//   - as its own SPP line   `SCAN;<ssid>,<rssi>,<open>;…`  (or `SCAN;NONE`)
//   - in the status JSON    `"scan":[{"ssid":…,"rssi":…,"open":…}]`
// Names only — a scan can never reveal a password (W-14).
//
// While the STA is ASSOCIATED, ESP32 scan results are unreliable (the radio is
// busy and the channel follows the router), so strength is reported only as a
// hint. The saved-router list remains the source of truth for what the car
// will actually join.

void WebServerComm::startScan() {
  if (scanPending_ || scanBusy_) return;  // armed or in flight already
  scanPending_ = true;
  scanStartMs_ = 0;  // armed; handle() kicks it on a later pass
}

void WebServerComm::runScanIfDue() {
  if (!scanPending_ || scanBusy_) return;
  const unsigned long now = millis();
  // A short settle delay: kicking the scan in the same millisecond as the
  // command that asked for it races the radio, which may still be finishing
  // the previous association.
  if (scanStartMs_ == 0) {
    scanStartMs_ = now;
    return;
  }
  if (now - scanStartMs_ < SCAN_SETTLE_MS) return;

  scanPending_ = false;
  scanBusy_ = true;
  scanCount_ = WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
  scanWaitMs_ = now;
}

void WebServerComm::collectScanIfDone() {
  if (!scanBusy_) return;
  const int found = WiFi.scanComplete();
  const unsigned long now = millis();
  if (found < 0 && now - scanWaitMs_ < SCAN_TIMEOUT_MS) return;  // still running

  scanBusy_ = false;
  scanCount_ = (found >= 0) ? found : 0;

  // Keep the strongest reading per SSID: one pass can report the same name
  // more than once, and the app shows one row per router.
  scanNetworks_.clear();
  int n = scanCount_;
  if (n > SCAN_MAX_RESULTS) n = SCAN_MAX_RESULTS;
  for (int i = 0; i < n; ++i) {
    const char* name = WiFi.SSID(i).c_str();
    if (!name || !name[0]) continue;
    const int32_t rssi = WiFi.RSSI(i);
    const bool open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    bool merged = false;
    for (size_t k = 0; k < scanNetworks_.size(); ++k) {
      if (strcasecmp(scanNetworks_[k].ssid.c_str(), name) == 0) {
        if (rssi > scanNetworks_[k].rssi) {
          scanNetworks_[k].rssi = rssi;
          scanNetworks_[k].open = open;
        }
        merged = true;
        break;
      }
    }
    if (!merged && scanNetworks_.size() < SCAN_MAX_RESULTS) {
      ScannedNetwork entry;
      snprintf(entry.ssid, sizeof(entry.ssid), "%s", name);
      entry.rssi = rssi;
      entry.open = open;
      scanNetworks_.push_back(entry);
    }
  }

  FW_LOG_VERBOSE("[SCAN] %u network(s) found\n",
                 static_cast<unsigned int>(scanNetworks_.size()));
  // The SPP form rides the next STATE (the same way NETW does); the JSON form
  // goes out on this broadcast so a WS client sees it straight away.
  if (modeManager) modeManager->sendState();
  broadcastStatus();
}

void WebServerComm::buildScanLine(char* buf, size_t bufLen) const {
  if (!buf || bufLen == 0) return;
  if (scanNetworks_.empty()) {
    snprintf(buf, bufLen, "SCAN;NONE");
    return;
  }
  size_t used = 0;
  used += snprintf(buf + used, bufLen - used, "SCAN");
  for (size_t i = 0; i < scanNetworks_.size() && used < bufLen; ++i) {
    int n = snprintf(buf + used, bufLen - used, ";%s,%d,%d",
                     scanNetworks_[i].ssid, static_cast<int>(scanNetworks_[i].rssi),
                     scanNetworks_[i].open ? 1 : 0);
    if (n < 0) break;
    used += static_cast<size_t>(n);
  }
}

size_t WebServerComm::buildScanJson(char* buf, size_t bufLen) const {
  if (!buf || bufLen == 0) return 0;
  size_t used = 0;
  int n = snprintf(buf, bufLen, "[");
  if (n > 0) used = static_cast<size_t>(n);
  for (size_t i = 0; i < scanNetworks_.size() && used < bufLen; ++i) {
    n = snprintf(buf + used, bufLen - used, "%s{\"ssid\":\"%s\",\"rssi\":%d,\"open\":%s}",
                 i ? "," : "", scanNetworks_[i].ssid,
                 static_cast<int>(scanNetworks_[i].rssi),
                 scanNetworks_[i].open ? "true" : "false");
    if (n < 0) break;
    used += static_cast<size_t>(n);
  }
  if (used < bufLen) {
    n = snprintf(buf + used, bufLen - used, "]");
    if (n > 0) used += static_cast<size_t>(n);
  }
  return used;
}

// T-49: names-only SPP line — "NETW;<ssid1>;<ssid2>;..." (fed to the remote's
// SW saved-networks menu). Never contains passwords. T-66: the car's own
// network (4WDCar_Wifi) leads the list first while in webserver mode.
void WebServerComm::buildNetworksLine(char* buf, size_t bufLen) {
  if (!buf || bufLen == 0) return;
  strncpy(buf, "NETW", bufLen - 1);
  buf[bufLen - 1] = '\0';
  if (showReservedEntry()) {
    strcat(buf, ";");
    strcat(buf, WIFI_AP_NAME);
  }
  for (int i = 0; i < savedRouterCount_; ++i) {
    if (strlen(buf) + 1 + strlen(savedRouterSsid_[i]) >= bufLen) break;
    strcat(buf, ";");
    strcat(buf, savedRouterSsid_[i]);
  }
}

// T-49: WS JSON array fragment — ["a","b",...] — inserted into the status
// JSON so the app/website can render the saved-router selector. T-66: the
// car's own network is the leading, protected entry in webserver mode.
void WebServerComm::buildNetworksJson(char* buf, size_t bufLen) {
  if (!buf || bufLen == 0) return;
  size_t pos = 0;
  const bool reserved = showReservedEntry();
  if (reserved) {
    pos += snprintf(buf + pos, bufLen - pos, "\"%s\"", WIFI_AP_NAME);
  }
  for (int i = 0; i < savedRouterCount_; ++i) {
    if (pos + 4 + strlen(savedRouterSsid_[i]) >= bufLen) break;
    pos += snprintf(buf + pos, bufLen - pos, "%s\"%s\"",
                    (reserved || i > 0) ? "," : "", savedRouterSsid_[i]);
  }
}

bool WebServerComm::showReservedEntry() const {
  // v2: the web layer is always-on — the own-AP reserved entry is permanent.
  return true;
}

void WebServerComm::startOwnAP() {
  if (apRunning_) return;
  // T-71 (round-11): keep the radio beaconing for the whole webserver session.
  // In WIFI_AP_STA the STA side scans/associates to a saved router; with the
  // default duty-cycling the softAP can blink out right when a phone races to
  // join `4WDCar_Wifi`, which surfaces as Android's intermittent "unable
  // to connect" from the phone's Wi-Fi search/connect menus (owner report).
  // setSleep(false) removes those gaps; explicit open-AP params (no password,
  // channel 1 follows the STA router in AP_STA, visible, up to 4 clients) keep
  // the broadcast rock-solid. Companion phone step (one time): forget the
  // network and re-add — Android can cache a stale profile against an open AP.
  // FIN-40/D1 (2026-09-18 device gate, T-71a FAIL): softAP() can fail while the
  // radio is busy (mode entry / BT stack / scan). The old code logged the
  // warning but STILL set apRunning_ = true, so startOwnAP() never retried and
  // the car could sit in ESP_SER with NO broadcast. Now: only a real success
  // marks the AP running; a failure leaves apRunning_ false and handle()
  // retries every AP_RETRY_INTERVAL_MS until it sticks.
  WiFi.setSleep(false);
  String apName = String(WIFI_AP_NAME);
  // FIN-48 (owner gate 2026-09-20, T-71a still failing on v1.8.2: "only the car's
  // default AP was not connecting in webserver mode; all external routers work"):
  // the softAP ran on the SDK DEFAULT subnet 192.168.4.1/24, which collides with
  // the phone-side networks every modern phone also carries (its own hotspot,
  // laptop/tether and telcoma CGNAT ranges all use 192.168.x). The PHONE then
  // rejects or APIPA-fails the association — the beacon was always up (X-9:
  // the remote's saved-networks menu sees + USEs the AP fine), the JOIN died
  // client-side. External-router joins were never affected — matching the
  // symptom exactly. Fix: pin the AP to a rarely-used private subnet
  // (192.168.245.x) via softAPconfig BEFORE softAP(). Non-invasive: only takes
  // effect when the AP is not already on that subnet (survives future SDK
  // default changes), AP name/channel/openness/clients all unchanged; the OTA
  // default of 192.168.4.1 is left alone entirely.
  const IPAddress apIp(192, 168, 245, 1);
  if (WiFi.softAPIP() != apIp) {
    WiFi.softAPConfig(apIp,               // car's address on its own network
                      IPAddress(192, 168, 245, 1),   // gateway (the car itself)
                      IPAddress(255, 255, 255, 0));  // /24 — up to ~250 clients
  }
  const bool apOk = WiFi.softAP(apName.c_str(), /*passphrase=*/nullptr,
                                /*channel=*/1, /*ssid_hidden=*/0,
                                /*max_connection=*/4);
  if (!apOk) {
    // Retry is driven by handle() (AP_RETRY_INTERVAL_MS backoff); log here so
    // the first failure is always visible on the diagnostic serial line.
    FW_LOG("WARN: softAP(%s) failed to start (radio busy?) - will retry\n", apName.c_str());
    apName_ = apName;  // keep the broadcast id for OLED/JSON even before it is up
    return;
  }
  apRunning_ = true;
  apName_ = apName;
  Serial.print("Own AP active: ");
  Serial.println(apName);
}

WebServerComm::~WebServerComm() {
  stop();
}

void WebServerComm::init() {
  server.on("/", [this]() { handleRoot(); });
  server.on("/forward", [this]() { handleMove("F"); });
  server.on("/backward", [this]() { handleMove("B"); });
  server.on("/left", [this]() { handleMove("L"); });
  server.on("/right", [this]() { handleMove("R"); });
  server.on("/stop", [this]() { handleMove("S"); });
  server.on("/speed", [this]() { handleSpeed(); });
  server.on("/mode", [this]() { handleMode(); });
  server.on("/status", [this]() { handleStatus(); });
  server.onNotFound([this]() { handleNotFound(); });

  setupWebSocket();
}

bool WebServerComm::begin() { return true; }

// v2 (owner plan 2026-09-28, Phase 1): ALWAYS-ON web layer. Own AP + HTTP :80
// + WS :81 come up at boot and NEVER stop; a stored router is joined ONLY on
// explicit request (ROUTERS;USE / WIFICFG), so the AP stays the default
// network exactly as R-16 established for the donor. Replaces the donor's
// SERVER-mode-entry beginStation() path.
void WebServerComm::beginAlwaysOn() {
  beginStation();  // WIFI_AP + startOwnAP() + server.begin() + ws.begin()
  WiFi.mode(WIFI_AP_STA);  // ready for explicit router joins without a mode dance
  updateExtraInfo();
  FW_LOG("[V2] Web layer always-on: AP=%s ip=192.168.245.1 http=:80 ws=:81\n",
         apName_.c_str());
}

bool WebServerComm::beginStation() {
  // F-20a: NON-blocking connect — never stall loop() waiting for the AP.
  // handle() resolves the pending transition (STA ok → connected state;
  // timeout → own AP is the network).
  FW_LOG_VERBOSE("DBG: beginStation() start (non-blocking)\n");
  // R-16 (owner 2026-09-18 — "only 4WDCar_Wifi misbehaves, other routers
  // are all good; make it standard"): the own AP is THE default network —
  // PURE AP on a FIXED channel 1. On ESP32, once the STA side associates to a
  // router the hardware FORCES the softAP beacon onto the router's channel,
  // so a phone scanning for 4WDCar_Wifi on channel 1 finds nothing until
  // its next slow rescan — exactly the intermittent vanish owner saw on boot.
  // A stored router is now joined ONLY on explicit request (ROUTERS;USE /
  // WIFICFG via rejoinNetwork — the paths the owner confirms work), never
  // automatically on mode entry. Own AP channel therefore never hops on boot.
  // T-64 still holds: with nothing stored the own network IS the network.
  WiFi.mode(WIFI_AP);
  startOwnAP();
  // FIN-40/D2 state stays initialised so rejoinNetwork() budgets are the only
  // STA retry paths (no auto-join means handle()'s retry branch stays idle).
  WiFi.setAutoReconnect(false);
  staRetryCount_ = 0;
  staGiveUpLogged_ = false;
  if (hasStoredWifi_) {
    Serial.println("Stored router available - join via ROUTERS;USE / WIFICFG (own AP stays default)");
  }
  startedAsAP = true;
  connectingPending_ = false;

  server.begin();
  serverRunning = true;
  ws.begin();
  FW_LOG_VERBOSE("DBG: WebSocket server begun\n");

  updateExtraInfo();
  return serverRunning;
}

void WebServerComm::rejoinNetwork() {
  // T-34: re-provisioning while already in ESP_SER. Force off the OLD network
  // (router or AP fallback), then apply the CURRENT target. Non-blocking —
  // handle() resolves connectingPending_ exactly like beginStation() does.
  pendingRejoin_ = hasStoredWifi_;
  WiFi.disconnect(/*wifioff=*/false);
  // T-65: the own hotspot is never torn down during a network change.
  WiFi.mode(WIFI_AP_STA);
  startOwnAP();
  // FIN-40/D2: fresh retry budget for the new target (ROUTERS;USE / WIFICFG).
  WiFi.setAutoReconnect(false);
  staRetryCount_ = 0;
  staGiveUpLogged_ = false;
  if (hasStoredWifi_) {
    connectingPending_ = true;
    connectStartMs_ = millis();
    WiFi.begin(storedSsid_, storedPass_);
  } else {
    connectingPending_ = false;
    startedAsAP = true;
  }
  // Servers are already running in SERVER mode — leave them alone so WS
  // clients (the app's Remote window) survive the network hand-over. Only
  // start them if this was somehow invoked outside a running server session.
  if (!serverRunning) {
    server.begin();
    serverRunning = true;
    ws.begin();
  }
  updateExtraInfo();
  FW_LOG_VERBOSE("[WIFICFG] rejoin requested (pendingRejoin_=%d)\n", pendingRejoin_ ? 1 : 0);
}

void WebServerComm::stop() {
  FW_LOG_VERBOSE("DBG: stop() called, serverRunning=%d\n", serverRunning ? 1 : 0);

  connectingPending_ = false;

  if (serverRunning) {
    server.stop();
    serverRunning = false;
    FW_LOG_VERBOSE("DBG: HTTP server stopped\n");
  }
  ws.close();
  ws.disconnect();
  FW_LOG_VERBOSE("DBG: WebSocket closed\n");

  WiFi.disconnect();
  if (apRunning_) WiFi.softAPdisconnect(true);
  apRunning_ = false;

  startedAsAP = false;
  queueClear();
  FW_LOG_VERBOSE("Free heap after stop: %d\n", static_cast<int>(ESP.getFreeHeap()));
}

bool WebServerComm::isRunning() const { return serverRunning; }

// v1.0.1 (owner OLED directive): differentiated link truth for the OLED.
bool WebServerComm::hasWsClients() { return ws.connectedClients() > 0; }

bool WebServerComm::isStaJoined() const { return WiFi.status() == WL_CONNECTED; }

void WebServerComm::handle() {
  if (serverRunning) server.handleClient();
  ws.loop();

  if (pendingModeBuf[0] != '\0') {
    unsigned long now = millis();
    if (now - pendingModeAtMs >= 200) {
      pendingModeBuf[0] = '\0';
      pendingModeAtMs = 0;
    }
  }

  // D3 (U-68): drive the car-side scan from the loop, never from the command
  // path. runScanIfDue kicks a non-blocking pass once the settle delay has
  // passed; collectScanIfDone reads it when the radio finishes and broadcasts.
  runScanIfDue();
  collectScanIfDone();

  // F-20a: resolve the pending STA connect without blocking the loop.
  if (connectingPending_) {
    if (WiFi.status() == WL_CONNECTED) {
      connectingPending_ = false;
      pendingRejoin_ = false;
      startedAsAP = false;
      FW_LOG_VERBOSE("WiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());
      updateExtraInfo();
      // R-16/pronto: the moment the STA truth changes, push STATE so the
      // remote + app mirrors (SSID/AP/IP rows) update same-second instead of
      // waiting up to STATE_INTERVAL_MS.
      if (modeManager) modeManager->sendState();
      if (modeManager) modeManager->resetFailsafe();
    } else if (millis() - connectStartMs_ > WIFI_STA_TIMEOUT_MS) {
      connectingPending_ = false;
      // T-65: the own hotspot is already broadcasting — it simply IS the
      // network now (no 8 s hidden-window, no separate softAP hand-over).
      startedAsAP = true;
      // OWNER BUG 2026-10-02 ("the car doesn't always switch to the router
      // properly / doesn't do it in one go"). WIFI_STA_TIMEOUT_MS is only the
      // FIRST attempt's budget, not the whole switch. The old code settled
      // AP-only here AND cleared pendingRejoin_, which also made the retry
      // branch below UNREACHABLE (it keys on pendingRejoin_ || !startedAsAP,
      // and these lines had just set pendingRejoin_ false + startedAsAP true)
      // - so STA_MAX_RETRIES never ran and ONE flaky association failed the
      // switch PERMANENTLY, with the car silently left on its own AP.
      //
      // The timeout now hands OFF to that existing capped budget: up to
      // STA_MAX_RETRIES more attempts spaced WIFI_RETRY_INTERVAL_MS, and only
      // once the budget is spent does the give-up branch settle AP-only. The
      // cap still holds, so an unreachable router cannot blank the softAP
      // beacon forever (FIN-40/D2).
      if (hasStoredWifi_) {
        // pendingRejoin_ TRUE is what arms the retry budget below.
        pendingRejoin_ = true;
        // Prompt the first retry instead of inheriting an old backoff stamp.
        lastWifiRetryMs_ = 0;
        Serial.println("WiFi STA: first attempt timed out - retrying");
      } else {
        // Nothing stored (the "back to my own network" case): there is no
        // target to retry, so the own AP simply IS the network now.
        pendingRejoin_ = false;
        Serial.println("WiFi STA failed, own AP is the network");
      }
      updateExtraInfo();
      // R-16/pronto: mirror the fallback switch immediately (STATE SSID= drops,
      // AP=/IP= re-point at the own network).
      if (modeManager) modeManager->sendState();
    }
  } else if (pendingRejoin_ || (!startedAsAP && WiFi.status() != WL_CONNECTED)) {
    // F-20a: guarded retry with backoff — never reset the radio every loop
    // (continuous disconnect/begin stops STA from ever associating).
    // T-34: pendingRejoin_ additionally forces the retry even while the STA is
    // reporting WL_CONNECTED, in case the disconnect in rejoinNetwork() had
    // not taken effect at the first handle() pass. T-64: only a STORED router
    // is ever joined — with nothing stored the own AP already IS the network.
    // FIN-40/D2 (T-71a device-gate FAIL): the retry budget is now CAPPED —
    // every disconnect/begin() cycle runs a STA scan that blanks the softAP
    // beacon, so an unreachable router used to be retried forever and phone
    // joins to 4WDCar_Wifi failed intermittently. After STA_MAX_RETRIES
    // spaced attempts the car settles AP-ONLY (the beacon never blanks again);
    // a later ROUTERS;USE / WIFICFG / mode re-entry starts STA with a fresh
    // budget. No drive/protocol/failsafe behaviour is touched.
    unsigned long now = millis();
    if (hasStoredWifi_ && staRetryCount_ < STA_MAX_RETRIES) {
      if (now - lastWifiRetryMs_ >= WIFI_RETRY_INTERVAL_MS) {
        lastWifiRetryMs_ = now;
        staRetryCount_++;
        // Each retry gets its OWN connect window. This branch is an `else if`
        // behind `if (connectingPending_)`, so a retry that did not re-arm the
        // flag would never be RESOLVED: a successful association would be
        // invisible (no updateExtraInfo, no sendState - the app would sit on
        // "getting an address" while the car was already on the router), and
        // pendingRejoin_ would stay set so this branch kept yanking a WORKING
        // association down every WIFI_RETRY_INTERVAL_MS. Arming
        // connectingPending_ hands the outcome back to the block above, which
        // clears both flags on success and re-arms the budget on the next
        // timeout. WIFI_STA_TIMEOUT_MS is therefore the real per-attempt
        // spacing; WIFI_RETRY_INTERVAL_MS is only the floor.
        connectingPending_ = true;
        connectStartMs_ = now;
        WiFi.disconnect();
        WiFi.begin(storedSsid_, storedPass_);
      }
    } else if (hasStoredWifi_ && !staGiveUpLogged_) {
      // Budget spent: settle AP-only. Force a fresh softAP call in case the
      // AP_STA→AP switch bounced the beacon (never trust apRunning_ here).
      staGiveUpLogged_ = true;
      startedAsAP = true;
      WiFi.disconnect();
      WiFi.mode(WIFI_AP);
      apRunning_ = false;
      startOwnAP();
      Serial.println("WiFi STA gave up (router unreachable) - AP-only mode");
      updateExtraInfo();
      // R-16/pronto: immediate STATE so the remote + app drop the stale SSID
      // row the same second the car settles AP-only.
      if (modeManager) modeManager->sendState();
    }
  }

  // FIN-40/D1: a softAP() that failed at bring-up (radio busy) is retried with
  // backoff until the beacon is actually up — apRunning_ only turns true on a
  // real success now, so ESP_SER always ends up broadcasting 4WDCar_Wifi.
  {
    const unsigned long apNow = millis();
    if (!apRunning_ && apNow - lastApRetryMs_ >= AP_RETRY_INTERVAL_MS) {
      lastApRetryMs_ = apNow;
      startOwnAP();
    }
  }

  static unsigned long lastExtraMs = 0;
  const unsigned long EXTRA_INTERVAL = 1000;
  unsigned long now = millis();
  if (now - lastExtraMs >= EXTRA_INTERVAL) {
    lastExtraMs = now;
    updateExtraInfo();
    broadcastStatus();
    FW_LOG_VERBOSE("Free heap: %d\n", static_cast<int>(ESP.getFreeHeap()));
  }
}

void WebServerComm::setModeManager(ModeManager* mm) { modeManager = mm; }

// --- Queue helpers ---
bool WebServerComm::queuePush(const char* s) {
  if (!s) return false;
  if (cmdCount >= CMD_QUEUE_LEN) return false;
  size_t n = strlen(s);
  if (n >= CMD_BUF_LEN - 2) return false;
  strncpy(cmdQueue[cmdTail], s, CMD_BUF_LEN - 2);
  cmdQueue[cmdTail][CMD_BUF_LEN - 2] = '\0';
  size_t len = strlen(cmdQueue[cmdTail]);
  cmdQueue[cmdTail][len] = '\n';
  cmdQueue[cmdTail][len + 1] = '\0';
  cmdTail = (cmdTail + 1) % CMD_QUEUE_LEN;
  cmdCount++;
  return true;
}

bool WebServerComm::queuePopChar(char* out) {
  if (cmdCount == 0) return false;
  char* cur = cmdQueue[cmdHead];
  if (cur[readPos] == '\0') {
    cmdQueue[cmdHead][0] = '\0';
    cmdHead = (cmdHead + 1) % CMD_QUEUE_LEN;
    cmdCount--;
    readPos = 0;
    return queuePopChar(out);
  }
  *out = cur[readPos++];
  return true;
}

void WebServerComm::queueClear() {
  for (size_t i = 0; i < CMD_QUEUE_LEN; ++i) cmdQueue[i][0] = '\0';
  cmdHead = cmdTail = cmdCount = 0;
  readPos = 0;
}

size_t WebServerComm::available() { return cmdCount > 0 ? 1 : 0; }

int WebServerComm::read() {
  char c;
  if (!queuePopChar(&c)) return -1;
  return static_cast<int>(c);
}

size_t WebServerComm::readBytes(char* buf, size_t len, unsigned long timeoutMs) {
  if (!buf || len == 0) return 0;
  size_t idx = 0;
  unsigned long start = millis();
  while (idx < len) {
    int v = read();
    if (v < 0) {
      if (timeoutMs == 0) break;
      if (millis() - start >= timeoutMs) break;
      yield();
      continue;
    }
    buf[idx++] = static_cast<char>(v);
    if (buf[idx - 1] == '\n') break;
  }
  if (idx < len) buf[idx] = '\0';
  return idx;
}

int WebServerComm::peek() {
  if (cmdCount == 0) return -1;
  char* cur = cmdQueue[cmdHead];
  if (!cur) return -1;
  if (cur[readPos] == '\0') return -1;
  return static_cast<int>(cur[readPos]);
}

size_t WebServerComm::write(const char* buf, size_t len) { return len; }

bool WebServerComm::send(const char* msg) {
  if (!msg || !serverRunning) return false;
  server.sendContent(msg);
  return true;
}

void WebServerComm::flush() {}

bool WebServerComm::connected() {
  // v2 transport-honest: WS clients win, then the joined STA network. The
  // own-AP-only state (broadcasting, nobody linked to the WS) is NOT a
  // command channel — a safe-stop must still fire on it. (The donor returned
  // WiFi.status()==WL_CONNECTED, which in AP-only sessions was always false —
  // correct for its ESP_SER dashboard, wrong as a link-loss signal for an
  // always-on layer.)
  if (ws.connectedClients() > 0) return true;
  return WiFi.status() == WL_CONNECTED;
}

void WebServerComm::currentIp(char* buf, size_t bufLen) {
  if (!buf || bufLen == 0) return;
  buf[0] = '\0';
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.localIP().toString().toCharArray(buf, bufLen);
  } else if (apRunning_) {
    // Own-network session: the softAP gateway (192.168.245.1 since FIN-48) is
    // the reachable IP.
    WiFi.softAPIP().toString().toCharArray(buf, bufLen);
  }
}

void WebServerComm::updateExtraInfo() {
  if (!modeManager) return;
  char extra[128];
  if (WiFi.status() == WL_CONNECTED) {
    int rssi = WiFi.RSSI();
    int pct = map(constrain(rssi, -100, -50), -100, -50, 0, 100);
    char ipStr[32];
    WiFi.localIP().toString().toCharArray(ipStr, sizeof(ipStr));
    // T-36: append the JOINED network name so the OLED can show the real SSID
    // instead of the generic "Connected" (owner report 2026-09-15). extraInfo_
    // (64) fits: pct+dBm+ip+32-char SSID ≈ 59 bytes max.
    snprintf(extra, sizeof(extra), "%d,%ddBm,%s,%s", pct, rssi, ipStr,
             WiFi.SSID().c_str());
  } else if (apRunning_) {
    // Own-network session: show the broadcast ID + the softAP IP on the
    // OLED so the user knows which network to join (the IP contains a dot,
    // which keeps isConnectedNow() true for the SERVER branch).
    char ipStr[32];
    WiFi.softAPIP().toString().toCharArray(ipStr, sizeof(ipStr));
    snprintf(extra, sizeof(extra), "AP:%s,%s", apName_.c_str(), ipStr);
  } else {
    extra[0] = '\0';
  }
  modeManager->setExtraInfo(extra);
}

void WebServerComm::refreshExtraInfo() { updateExtraInfo(); }

void WebServerComm::setupWebSocket() {
  instanceForWs = this;
  ws.onEvent([](uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    if (instanceForWs) instanceForWs->handleWebSocketEvent(num, type, payload, length);
  });
}

void WebServerComm::handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_TEXT && payload && length > 0) {
    char msg[128];
    const size_t copyLen = (length < sizeof(msg) - 1) ? length : sizeof(msg) - 1;
    strncpy(msg, (char*)payload, copyLen);
    msg[copyLen] = '\0';
    // W-12/T-21 hardening: strip a trailing \r\n so line-oriented clients
    // (website sendLine() appends \n over WS) can never leak the newline into
    // token matching or the JSON ack echo. handleCommand() also trims.
    size_t mlen = strlen(msg);
    while (mlen > 0 && (msg[mlen - 1] == '\n' || msg[mlen - 1] == '\r')) {
      msg[--mlen] = '\0';
    }
    if (mlen == 0) return;
    // 2026-09-15: WS is the WIRE transport — the drive gate scopes BT with
    // acceptsBtDrive, so webserver-mode drive must not fall under that rule.
    if (modeManager) modeManager->handleCommand(msg, ModeManager::CmdSource::WIRE);
    queuePush(msg);
    // F-22/F-23: state requests get a live status broadcast on the WS channel.
    if (strncasecmp(msg, "REQ_STATE", 9) == 0) {
      broadcastStatus();
      return;
    }
    // T-49: a networks request gets the same live broadcast (status JSON now
    // carries the saved-router names array).
    if (strncasecmp(msg, "REQ_NETW", 8) == 0) {
      broadcastStatus();
      return;
    }
    // F-68 (U-68, 2026-10-02): the ack used to echo the command VERBATIM, so
    // `ROUTERS;ADD;Home;hunter2` came straight back to the phone — and would
    // land in any WebSocket proxy log on the way. The app happened to ignore
    // the field; that was luck, not design.
    //
    // An acknowledgement confirms the REQUEST, it never replays it. Password-
    // bearing commands are confirmed by operation + SSID only. Same for the
    // HTTP route (the next block) and the WIFICFG family.
    char ack[128];
    char safe[96];
    redactForAck(msg, safe, sizeof(safe));
    snprintf(ack, sizeof(ack), "{\"ok\":true,\"cmd\":\"%s\"}", safe);
    ws.sendTXT(num, ack);
  }
}

// F-68: build the acknowledgement form of a command line — every credential
// stripped. `ROUTERS;ADD;<ssid>;<pass>` becomes `ROUTERS;ADD;<ssid>` and
// `WIFICFG;<ssid>;<pass>` becomes `WIFICFG;<ssid>`. Defined at file scope so
// both the WS and the HTTP ack paths use the one rule.
void WebServerComm::redactForAck(const char* cmd, char* out, size_t outLen) {
  if (!cmd || !out || outLen == 0) return;
  out[0] = '\0';
  if (!cmd) return;

  // Split on ';' and keep at most the operation plus ONE further field: for
  // ADD/WIFICFG that field is the SSID, which is not secret; everything after
  // it (the password) is dropped.
  const char* semi = strchr(cmd, ';');
  if (!semi) {
    snprintf(out, outLen, "%s", cmd);
    return;
  }
  size_t headLen = static_cast<size_t>(semi - cmd);
  const char* tail = semi + 1;
  const char* semi2 = strchr(tail, ';');
  size_t keep = semi2 ? static_cast<size_t>(semi2 - tail) : strlen(tail);
  snprintf(out, outLen, "%.*s;%.*s", static_cast<int>(headLen), cmd,
           static_cast<int>(keep), tail);
}

void WebServerComm::broadcastStatus() {
  if (!modeManager) return;
  // 1200: status JSON + ssid(32) + ap(21) + caps map(~90) + networks (6×33 ≈ 200)
  // + scan (D3, 12 × ~48 ≈ 600). Grown for the scan field; `buildStatusJson`
  // bounds every fragment it writes, so a full scan cannot overflow this.
  char buf[1200];
  buildStatusJson(buf, sizeof(buf));
  ws.broadcastTXT(buf);
}

// --- HTTP handlers ---
void WebServerComm::handleRoot() {
  FW_LOG_VERBOSE("DBG: HTTP GET / received\n");
  server.send_P(200, "text/html; charset=utf-8", PAGE_HTML);
}

void WebServerComm::handleMove(const char* cmd) {
  if (modeManager) {
    modeManager->handleCommand(cmd, ModeManager::CmdSource::WIRE);
    queuePush(cmd);
    if (strcmp(cmd, "S") == 0) queueClear();
  }
  // F-68: same redaction as the WS ack — this one is also broadcast to every
  // WS client, so an unredacted password would reach all of them.
  char out[128];
  char safe[96];
  redactForAck(cmd, safe, sizeof(safe));
  snprintf(out, sizeof(out), "{\"ok\":true,\"cmd\":\"%s\"}", safe);
  server.send(200, "application/json", out);
  ws.broadcastTXT(out);
}

void WebServerComm::handleSpeed() {
  String sval = server.arg("val");
  if (sval.length() == 0) {
    server.send(400, "application/json", "{\"ok\":false,\"message\":\"missing val\"}");
    return;
  }
  int requested = sval.toInt();
  int speed = constrain(requested, static_cast<int>(MIN_SPEED), static_cast<int>(MAX_SPEED));
  if (modeManager) modeManager->setSpeed(speed);
  char buf[32];
  snprintf(buf, sizeof(buf), "#%d", speed);
  queuePush(buf);
  char out[128];
  snprintf(out, sizeof(out), "{\"ok\":true,\"speed\":%d}", speed);
  server.send(200, "application/json", out);
  ws.broadcastTXT(out);
}

void WebServerComm::handleMode() {
  String sval = server.arg("val");
  if (sval.length() == 0) {
    server.send(400, "application/json", "{\"ok\":false,\"message\":\"missing val\"}");
    return;
  }
  const char* val = sval.c_str();
  if (modeManager) modeManager->setModeFromString(val);
  strncpy(pendingModeBuf, val, sizeof(pendingModeBuf) - 1);
  pendingModeBuf[sizeof(pendingModeBuf) - 1] = '\0';
  pendingModeAtMs = millis();
  char out[256];
  snprintf(out, sizeof(out), "{\"ok\":true,\"mode\":\"%s\"}", val);
  server.send(200, "application/json", out);
  ws.broadcastTXT(out);
}

void WebServerComm::handleStatus() {
  char buf[1200];
  buildStatusJson(buf, sizeof(buf));
  server.send(200, "application/json", buf);
}

void WebServerComm::handleNotFound() {
  FW_LOG_VERBOSE("DBG: HTTP NotFound for URI: %s\n", server.uri().c_str());
  server.send(404, "text/plain", "Not found");
}

void WebServerComm::buildStatusJson(char* buf, size_t bufLen) {
  if (!buf || bufLen == 0) return;
  // F-23: protocol values only — mode = canonical token (registry-driven),
  // status = live whitelisted word. Display name rides in mode_name.
  const char* modeToken = modeManager ? modeManager->getModeToken() : "N/A";
  const char* modeName  = modeManager ? modeManager->getModeString() : "N/A";
  bool conn = modeManager ? modeManager->isConnectedNowPublic() : false;
  char statusWord[24] = "Stopped";
  if (modeManager) modeManager->getStatusWord(statusWord, sizeof(statusWord));
  char ipStr[32] = "";
  int rssi = 0, pct = 0;
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.localIP().toString().toCharArray(ipStr, sizeof(ipStr));
    rssi = WiFi.RSSI();
    pct = map(constrain(rssi, -100, -50), -100, -50, 0, 100);
  } else if (apRunning_) {
    // R-13: in an own-network session the reachable IP is the softAP gateway
    // (192.168.245.1 since FIN-48) — announce it like the OLED already does so
    // the JSON `ip` is never empty in ESP_SER (app deck + tap-to-open).
    WiFi.softAPIP().toString().toCharArray(ipStr, sizeof(ipStr));
  }  int speed = modeManager ? modeManager->getSpeed() : static_cast<int>(MIN_SPEED);
  unsigned long uptime = millis();
  size_t free_heap = ESP.getFreeHeap();
  // T-19 (W-10): stub modes are announced in the JSON too so website/app UIs
  // can render COMING SOON from car truth (mirrors STATE;...;CAP=STUB).
  const bool stubMode = modeManager ? modeManager->isCurrentModeStub() : false;
  // 2026-09-15 (owner matrix): full per-mode availability map — the app marks
  // every mode LIVE / IN PROGRESS / COMING SOON from car truth, not a client
  // hard-coded list. Mirror of the BT "CAPS;..." STATE line.
  char caps[128] = "";
  if (modeManager) modeManager->buildCapsMap(caps, sizeof(caps));
  // v1.4.0 provisioning truth: configured SSID (never the password) and the
  // AP broadcast id — the app shows both in the ESP_SER deck. T-64: when
  // nothing is stored, the car's OWN network (4WDCar_Wifi) IS the
  // network — the compile-time secrets.h fallback is never joined nor named.
  const char* ssidOut = hasStoredWifi_ ? storedSsid_ : "";
  const char* apOut = apName_.length() > 0 ? apName_.c_str() : "";
  // T-49: saved-router names for the app/website (names only — passwords never
  // leave the car; W-14).
  char networks[260] = "";
  buildNetworksJson(networks, sizeof(networks));
  // D3 (U-68): the car-side nearby-network scan as `"scan":[…]` — the WS half
  // of the same data the SPP `SCAN;…` line carries. `[]` until a scan has run,
  // so the app can tell "not scanned yet" from "scanned, found nothing".
  char scanJson[512] = "";
  buildScanJson(scanJson, sizeof(scanJson));
  // Connections-Hub: the stable board id (fw:<id> profile key for WiFi cars).
  char boardId[8] = "";
  boardIdHex(boardId, sizeof(boardId));
  snprintf(buf, bufLen,
     "{"
       "\"status\":\"%s\","
       "\"mode\":\"%s\","
       "\"mode_name\":\"%s\","
       "\"stub\":%s,"
       "\"caps\":\"%s\","
       "\"connected\":%s,"
       "\"ip\":\"%s\","
       "\"rssi\":%d,"
       "\"signal\":%d,"
       "\"uptime_ms\":%lu,"
       "\"free_heap\":%u,"
       "\"speed\":%d,"
       "\"trim\":%d,"
       "\"min_speed\":%d,"
       "\"max_speed\":%d,"
       "\"ssid\":\"%s\","
       "\"ap\":\"%s\","
       "\"id\":\"%s\","
       "\"networks\":[%s],"
       "\"scan\":%s"
     "}",
     statusWord,
     modeToken,
     modeName,
     stubMode ? "true" : "false",
     caps,
     conn ? "true" : "false",
     ipStr,
     rssi,
     pct,
     uptime,
     static_cast<unsigned int>(free_heap),
     speed,
     static_cast<int>(modeManager ? modeManager->getTrim() : 0),
     static_cast<int>(MIN_SPEED),
     static_cast<int>(MAX_SPEED),
     ssidOut,
     apOut,
     boardId,
     networks,
     scanJson
  );
}