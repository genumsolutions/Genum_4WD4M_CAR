// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: WebServerComm.cpp — web + WebSocket server transport: command
//       queue bridge, JSON status broadcast, WIFICFG provisioning
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include "WebServerComm.h"
#include "WebPage.h"
#include "CrashLog.h"     // U-95/F-79: the restart record, surfaced in /status
#include <WiFi.h>
#include <esp_task_wdt.h> // U-95/F-77: the send loop must feed the watchdog

// U-95/F-77: the invariant that was only ever a comment, now checked by the
// compiler. HTTP_BODY_BUDGET_MS is the upper bound on how long serveRootBounded()
// may occupy loop(); LOOP_TASK_WDT_TIMEOUT_MS is how long loop() may occupy the
// task before the watchdog resets the chip. A budget at or above the watchdog
// does not "mostly work" — it is a reset with extra steps, and the failure is
// deterministic rather than racy, so it will happen on the bench every time.
//
// The second half is the margin. `>=` would permit a budget of exactly the
// watchdog's value, which leaves the watchdog and the budget expiring in the
// same millisecond and makes correctness depend on which one the scheduler
// reaches first. The slack (6500 ms since the watchdog moved to 10000 ms for
// Phase 1b — it was 1500 ms at 5000) absorbs the HTTP parser's own work and
// the rest of the loop pass.
static_assert(HTTP_BODY_BUDGET_MS < LOOP_TASK_WDT_TIMEOUT_MS,
              "F-77: the page-send budget must be below the watchdog timeout, or a "
              "slow client resets the car instead of getting a truncated page");
static_assert(LOOP_TASK_WDT_TIMEOUT_MS - HTTP_BODY_BUDGET_MS >= 1000ul,
              "F-77: leave at least 1s between the page-send budget and the watchdog, "
              "or the parser and the rest of loop() have no room");
#include <pgmspace.h>
#include <ESPmDNS.h>   // U-74: advertise the car by name on whatever network it is on

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
  mdnsRunning_(false), rejoinStep_(REJOIN_STEP_IDLE), staConnectedAtMs_(0), apParked_(false), staTimeoutMs_(WIFI_STA_TIMEOUT_MS), staRetryCap_(STA_MAX_RETRIES),
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
//                              and REJOIN that router. v2 does NOT switch the
//                              car into a mode — ESP_SER is retired (U-79) and
//                              the web layer is always-on regardless.
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
    } else if (ssidLen == strlen(WIFI_AP_NAME) &&
               strncasecmp(ssid, WIFI_AP_NAME, ssidLen) == 0) {
      // U-92 (owner: *"webpage has double entry of esps own hotspot"*): the
      // car's own hotspot is not a router to join. This branch had NO guard
      // at all — storing the AP as the ACTIVE pair made the car try to
      // associate with its OWN beacon, and the boot promotion then copied
      // the name into the saved registry (the double entry).
      snprintf(reply, sizeof(reply), "WIFICFG;ERROR;Reserved");
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
  // U-77: ONE read-only NVS open. This used to be two separate opens of the
  // same namespace (the second one pointlessly WRITABLE) with a duplicated
  // comment block - same reads, twice the NVS traffic.
  //
  // U-71: the last router that ACTUALLY associated, loaded for the boot
  // auto-join. Empty until one has worked, and deliberately NOT seeded from
  // storedSsid_ - 'tried' and 'worked' are different facts.
  lastGoodSsid_[0] = '\0';
  lastGoodPass_[0] = '\0';
  Preferences prefs;
  prefs.begin("botcfg", true);
  String ssid = prefs.getString("wifi_ssid", "");
  String pass = prefs.getString("wifi_pass", "");
  prefs.getString("last_ok_ssid", lastGoodSsid_, sizeof(lastGoodSsid_));
  prefs.getString("last_ok_pass", lastGoodPass_, sizeof(lastGoodPass_));
  prefs.end();
  lastGoodSsid_[ROUTER_SSID_LEN - 1] = '\0';
  lastGoodPass_[ROUTER_PASS_LEN - 1] = '\0';
  if (ssid.length() > 0 && ssid.length() <= 32 &&
      strcasecmp(ssid.c_str(), WIFI_AP_NAME) != 0) {
    ssid.toCharArray(storedSsid_, sizeof(storedSsid_));
    pass.toCharArray(storedPass_, sizeof(storedPass_));
    hasStoredWifi_ = true;
  } else {
    if (ssid.length() > 0) {
      // U-92: a stored ACTIVE pair named like the car's own hotspot (the
      // unguarded WIFICFG path) would make boot auto-join try to associate
      // with the car's OWN beacon. Drop it and erase the NVS keys so the
      // poison cannot survive a reboot.
      Preferences wprefs;
      wprefs.begin("botcfg", false);
      wprefs.remove("wifi_ssid");
      wprefs.remove("wifi_pass");
      wprefs.end();
      Serial.println(
          "[WIFICFG] removed the car's own hotspot from the stored pair (U-92)");
    }
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
  // U-77: NO last-good wipe here. loadSavedRouters() used to clear
  // lastGoodSsid_/lastGoodPass_ and never re-read them, so every boot the
  // registry load DESTROYED what loadStoredWifi() had just loaded - and
  // armBootAutoJoin() then always saw an empty last-good router, silently
  // disabling the owner's U-71 boot auto-join (U-77 device log: no
  // "[WIFICFG] boot:" line on a car with a working last-good router).
  // loadStoredWifi() is the single owner of the last-good state.
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
  // U-92: a registry row named like the car's OWN hotspot is poison — it got
  // in through the unguarded WIFICFG/ADD paths (their Reserved checks compared
  // the whole unterminated field, so a password after the name bypassed them)
  // and the promotion below then copied the active pair in. The owner's
  // "double entry of esp's own hotspot". Purge at load and rewrite NVS so an
  // already-poisoned car heals itself without owner action.
  {
    int w = 0;
    bool purged = false;
    for (int r = 0; r < savedRouterCount_; ++r) {
      if (strcasecmp(savedRouterSsid_[r], WIFI_AP_NAME) == 0) {
        purged = true;
        continue;
      }
      if (w != r) {
        strcpy(savedRouterSsid_[w], savedRouterSsid_[r]);
        strcpy(savedRouterPass_[w], savedRouterPass_[r]);
      }
      ++w;
    }
    if (purged) {
      for (int i = w; i < savedRouterCount_; ++i) {
        savedRouterSsid_[i][0] = '\0';
        savedRouterPass_[i][0] = '\0';
      }
      savedRouterCount_ = w;
      Preferences wprefs;
      wprefs.begin("botcfg", false);
      for (int i = 0; i < MAX_SAVED_ROUTERS; ++i) {
        static const char* sKeys[] = { "rnet0s", "rnet1s", "rnet2s", "rnet3s", "rnet4s", "rnet5s" };
        static const char* pKeys[] = { "rnet0p", "rnet1p", "rnet2p", "rnet3p", "rnet4p", "rnet5p" };
        if (i < savedRouterCount_) {
          wprefs.putString(sKeys[i], savedRouterSsid_[i]);
          wprefs.putString(pKeys[i], savedRouterPass_[i]);
        } else {
          wprefs.remove(sKeys[i]);
          wprefs.remove(pKeys[i]);
        }
      }
      wprefs.end();
      Serial.println(
          "[ROUTERS] removed the car's own hotspot from the saved list (U-92)");
    }
  }
  // Promote the stored active pair (wifi_ssid) up front so the connected
  // router always appears in the saved list even after a WIFICFG-only setup.
  // U-92: never promote the car's own hotspot — that is the other half of the
  // double-entry mechanism.
  if (hasStoredWifi_ && storedSsid_[0] &&
      strcasecmp(storedSsid_, WIFI_AP_NAME) != 0) {
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
    } else if (ssidLen == strlen(WIFI_AP_NAME) &&
               strncasecmp(ssid, WIFI_AP_NAME, ssidLen) == 0) {
      // T-66/U-92: the car's own network is not a router you add to the list.
      // The old check compared the WHOLE unterminated field, so a password
      // after the name bypassed it and the hotspot entered the registry.
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
    // U-92: bound the ssid field FIRST. `ssid` points into the raw command
    // line and still carries ";<password>" — every comparison below used to
    // run against the WHOLE remainder, so the saved-router match never
    // matched and the Reserved guard was bypassed whenever a password
    // followed the name.
    const char* semi = strchr(ssid, ';');
    const char* pass = semi ? semi + 1 : "";
    size_t ssidLen = semi ? static_cast<size_t>(semi - ssid) : strlen(ssid);
    const bool isOwnApName =
        (ssidLen == strlen(WIFI_AP_NAME) &&
         strncasecmp(ssid, WIFI_AP_NAME, ssidLen) == 0);
    if (isOwnApName) {
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
      // (U-92: field-bounded — see the note at the top of this branch).
      int slot = -1;
      for (int i = 0; i < savedRouterCount_; ++i) {
        if (strlen(savedRouterSsid_[i]) == ssidLen &&
            strncasecmp(savedRouterSsid_[i], ssid, ssidLen) == 0) { slot = i; break; }
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
        // (U-92: semi/pass/ssidLen/isOwnApName are computed at the top of
        // this branch now, field-bounded.)
        if (ssidLen == 0 || ssidLen >= ROUTER_SSID_LEN) {
          snprintf(reply, sizeof(reply), "ROUTERS;ERROR;SSID length");
        } else if (strlen(pass) >= ROUTER_PASS_LEN) {
          snprintf(reply, sizeof(reply), "ROUTERS;ERROR;Password too long");
        } else if (isOwnApName) {
          // T-66/U-92: the car's own network is protected — it can never be
          // added to the saved list (the old check compared the WHOLE
          // unterminated field, so a password after the name bypassed it).
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
      if (strcasecmp(scanNetworks_[k].ssid, name) == 0) {
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
  // U-76 (2026-10-02): THE ROOT CAUSE, from the owner's breadcrumbs.
  //
  //   [RADIO] rejoin/softAP
  //   [RADIO] rejoin/begin
  //   E wifi:sta is connecting, cannot set config
  //   [MDNS] unavailable on this network
  //   E task_wdt: Task watchdog got triggered ... loopTask (CPU 1)
  //
  // `wifi:sta is connecting, cannot set config` is an ESP-IDF REFUSAL: something
  // tried to CONFIGURE the WiFi driver while the STA was mid-connection. In this
  // firmware that "something" was one of exactly three calls - `softAPConfig()`,
  // `softAP()`, or `MDNS.begin()` (which sets the netif hostname, so it counts) -
  // and every one of them is illegal at that moment. The refusal is not the fatal
  // part; the fatal part is that `softAP()` against a live association does not
  // return promptly, so `handle()` sat in it, never fed loopTask, and the 5 s
  // task watchdog rebooted the car (F-24). `AP_RETRY_INTERVAL_MS` then re-armed
  // the same call forever.
  //
  // THE INVARIANT, which is the actual fix:
  //
  //     While the STA is connecting or connected, touch NOTHING that configures
  //     the driver or the netif. Not softAPConfig(), not softAP(), not
  //     MDNS.begin().
  //
  // That is safe precisely because of T-65: the own hotspot is NEVER torn down
  // during a network change, so when the STA is busy the beacon is already up and
  // there is genuinely nothing to do. The old code got this wrong by putting
  // `softAPConfig()` at the very TOP of this function, ABOVE the U-72 staSharing
  // guard added below - so the one call that can never be legal while the STA is
  // busy was the one call that was never guarded.
  if (staBusy()) {
    noteRadioStep("skip-softAP/sta-busy");
    return;
  }
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
  // U-76: this config set is now BELOW the staBusy() guard above, deliberately.
  // It used to be the first statement in the function, which meant it was the
  // one call in the whole AP path that ran unconditionally - and ESP-IDF refuses
  // it outright while the STA is connecting.
  if (WiFi.softAPIP() != apIp) {
    noteRadioStep("softAPConfig");
    WiFi.softAPConfig(apIp,               // car's address on its own network
                      IPAddress(192, 168, 245, 1),   // gateway (the car itself)
                      IPAddress(255, 255, 255, 0));  // /24 — up to ~250 clients
  }
  // U-72 (2026-10-02): THE AP CHANNEL MUST NOT BE PINNED WHILE A ROUTER IS JOINED.
  //
  // This was `WiFi.softAP(ssid, nullptr, /*channel=*/1, ...)` unconditionally,
  // and the comment above it claimed "channel 1 follows the STA router in
  // AP_STA". It does not — passing an explicit channel is precisely what STOPS
  // it following. In AP_STA the softAP and the STA share ONE radio and must
  // share ONE channel, so while the car is joined to a router on (say) channel
  // 6, a hard-pinned channel 1 leaves the driver with a channel conflict: the
  // AP beacons on one channel while the STA listens on another.
  //
  // That is why the owner's report is specific — *"the car still restart while
  // in other route"*: on its own hotspot there is no STA, so channel 1 is fine;
  // the instability appears exactly once a router is joined, and re-running
  // softAP() against a live association is a known way to wedge (and trip the
  // task watchdog, whose action is a reboot — F-24).
  //
  // Fix: channel 1 only when there is no station interface to share with.
  // Channel 0 tells the core to follow the STA's channel, which is the only
  // legal answer in AP_STA. The owner's own hotspot behaviour is unchanged,
  // because with nothing joined the AP still comes up on channel 1.
  const bool staSharing = (WiFi.getMode() == WIFI_AP_STA) &&
                          (WiFi.status() == WL_CONNECTED || connectingPending_ ||
                           rejoinStep_ != REJOIN_STEP_IDLE);
  const uint8_t apChannel = staSharing ? 0 : 1;  // 0 = follow the STA
  if (staSharing && apRunning_) {
    // The AP is already up on whatever channel the core settled on; reconfiguring
    // it mid-association is the very thing that destabilises the driver. Leave it.
    return;
  }
  // U-96: use minimum AP power (WIFI_POWER_11dBm) to reduce the init current
  // spike. The car's own hotspot only needs to reach a phone across a room —
  // 11dBm (~12mW) is more than enough and draws noticeably less current than
  // the default 20dBm during the vulnerable boot window.
  WiFi.setTxPower(WIFI_POWER_11dBm);
  const bool apOk = WiFi.softAP(apName.c_str(), /*passphrase=*/nullptr,
                                apChannel, /*ssid_hidden=*/0,
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
  // U-74: advertise by name. Re-published on every network change, so the
  // advertisement is never stale - see startMdns() for why the drive deck cannot
  // open on the router without a way to FIND the car.
  startMdns();
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
  // U-79: the boot auto-join (armBootAutoJoin) is NO LONGER fired from here.
  // This function now also runs on the U-79 power-policy RESUME path (idle ->
  // Wi-Fi back up), and whether the router join runs is the .ino's decision
  // (last-used transport: Wi-Fi -> join, Bluetooth -> stay on hotspot).
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
  giveUpStep_ = GIVEUP_IDLE;
  mdnsPending_ = false;
  lastMdnsRetryMs_ = 0;
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
  // U-71 (2026-10-02): THIS FUNCTION NOW DOES NO RADIO WORK AT ALL.
  //
  // It used to run mode(WIFI_AP_STA) -> softAP() -> WiFi.begin() synchronously,
  // in the command path, inside one call. Two separate faults lived in that:
  //
  //   a) WiFi.mode() RESTARTS the WiFi driver, so a softAP() and a WiFi.begin()
  //      in the same tick race a driver that is still initialising - the STA
  //      fails to associate while the softAP still comes up (U-70).
  //   b) More seriously: this runs from ModeManager::run() inside loop(). A
  //      driver restart plus softAP() plus begin() can occupy loop() for long
  //      enough to trip the 5 s task watchdog - and the WDT's action IS a
  //      reboot (F-24). That is the most plausible mechanism behind the owner's
  //      report *"i used the other router switch thing but the car restarted as
  //      soon as i tried to access the webpage"*: the switch churns the radio in
  //      the command path, and the very next thing that arrives is an HTTP
  //      request served by the same driver.
  //
  // So the whole sequence is now a STATE MACHINE advanced by handle(), with ONE
  // radio call per loop pass. loop() returns between steps, the WDT is fed, and
  // the web server can service a request in between - which is exactly the
  // window that used to be missing.
  //
  // This is F-24/F-60 applied properly: no single blocking operation on the
  // connection path may be able to take the car down.
  pendingRejoin_ = hasStoredWifi_;
  staRetryCount_ = 0;
  staGiveUpLogged_ = false;
  connectingPending_ = false;
  startedAsAP = true;
  rejoinStep_ = REJOIN_STEP_MODE;
  // U-75: an in-flight give-up must be cancelled by a new request, or its stale
  // `WiFi.mode(WIFI_AP)` step would land AFTER the user asked to join a router
  // and yank the car off the network they just selected.
  giveUpStep_ = GIVEUP_IDLE;
  // U-75: an EXPLICIT switch always gets the FULL retry budget. Previously
  // `armBootAutoJoin()` wrote the short boot budget here and only the
  // success path ever put it back — so after a failed boot auto-join the car
  // silently kept ONE retry and a 6 s window for the rest of the session, and
  // the comment promising "a later ROUTERS;USE starts STA with a fresh budget"
  // was false. `armBootAutoJoin()` now applies its short budget AFTER this call.
  staTimeoutMs_ = WIFI_STA_TIMEOUT_MS;
  staRetryCap_ = STA_MAX_RETRIES;
  // Servers: not radio work, and they must stay up so an HTTP request that
  // arrives during the switch is still ANSWERED (the pre-switch socket is what
  // the app is holding while the car changes networks).
  if (!serverRunning) {
    server.begin();
    serverRunning = true;
    ws.begin();
  }
  updateExtraInfo();
  FW_LOG_VERBOSE("[WIFICFG] rejoin ARMED (step=%d, target=\"%s\")\n",
                 (int)rejoinStep_, hasStoredWifi_ ? storedSsid_ : "");
}

// U-75 (2026-10-02): the give-up fallback, one blocking radio call per pass.
//
// The owner's `[BOOT] reset reason: 6 (TASK WATCHDOG)` is the proof this was
// needed: the three calls it replaces ran back to back inside ONE handle()
// pass, and `WiFi.mode(WIFI_AP)` restarts the WiFi driver, so loop() could
// exceed the 5 s task watchdog — and a WDT trip reboots the car. This is the
// same rule `advanceRejoinStep()` follows, and the reason is identical: no
// single blocking operation on the connection path may be able to take the car
// down (F-24/F-60).
//
// Every branch returns immediately after advancing, so `handle()` spends at
// most ONE driver call per loop iteration and returns, feeding the WDT and
// giving the web server a chance to answer a request in between.
// U-86: park the setup beacon while the car is healthy on a router.
//
// Owner asked whether the car's own AP can stop broadcasting once it is on an
// external router. It can, and the safety argument is short: the beacon is the
// setup path, so it is only dropped when the car is ALREADY reachable without
// it, and the existing give-up fallback restores it the moment the router goes
// away. Worst case is one dropped connection, never a car that cannot be set up.
//
// Deliberately NOT one blocking call per step here, because this is only ever
// softAPdisconnect()/softAP() — no `WiFi.mode()` driver restart, which is the
// call that caused the U-75 watchdog reboot. `softAPdisconnect` alone leaves the
// STA association and the driver up.
void WebServerComm::updateApPowerPolicy() {
  const bool staUp = (WiFi.status() == WL_CONNECTED);
  const unsigned long nowMs = millis();

  // U-87 CORRECTION (found on the bench, not by review).
  //
  // The first version of this policy parked the beacon whenever the STA was up.
  // That is wrong, and the bench proved it within one boot: the car joined a
  // router this laptop was NOT on, the STA came up, the beacon was parked - and
  // the car became completely unreachable. Before the change the beacon was
  // always up, so 4WDCar_Wifi was ALWAYS a way in, and that is the property
  // that matters. "The car is on a router" says nothing about whether YOU can
  // reach that router.
  //
  // The missing condition is the obvious one: only park while somebody is
  // actually connected, because a connected client is proof the user is on the
  // router already and does not need the beacon. The moment the last client
  // goes, the beacon comes back.
  //
  //   in use  -> beacon parked (the power saving, when it is safe)
  //   idle    -> beacon up     (always reachable, which is the safety property)
  //
  // So the beacon is down exactly when it is not needed and up exactly when it
  // might be. The idle case is the common one, so this saves less power than
  // first claimed - and that is the honest trade, because the alternative is a
  // car that cannot be set up.
  const bool inUse = ws.connectedClients() > 0;

  if (!staUp) {
    // Router gone or never joined: the beacon is the fallback, so make sure it
    // is up. `startOwnAP()` is idempotent and already retried by handle().
    staConnectedAtMs_ = 0;
    apParked_ = false;
    if (!apRunning_) startOwnAP();
    return;
  }

  // Track when THIS association came up. Re-armed on every drop so the grace
  // period always runs after a real transition, never mid-session.
  if (staConnectedAtMs_ == 0) staConnectedAtMs_ = nowMs;

  if (apParked_) {
    // Parked: stay parked ONLY while a client is actually using the car, and
    // only while there is a real lease to be using it over.
    if (inUse && storedSsid_[0] != '\0') return;
    apParked_ = false;
    if (!apRunning_) startOwnAP();
    Serial.println("[POWER] car idle - hotspot beacon restored (stay reachable)");
    return;
  }

  if (!apRunning_) {
    // Beacon is already down but we never parked it (a softAP failure). Leave
    // it alone rather than fighting the radio.
    return;
  }

  // Park only when ALL of these hold: associated, a lease exists, somebody is
  // connected right now, and it has held long enough that a router dropping
  // again immediately cannot cause a teardown/rebuild flap.
  if (!inUse) return;
  if (storedSsid_[0] == '\0') return;
  if (nowMs - staConnectedAtMs_ < AP_PARK_GRACE_MS) return;

  // One call, and it does not restart the driver.
  WiFi.softAPdisconnect(/*wifioff=*/false);
  apRunning_ = false;
  apParked_ = true;
  Serial.println("[POWER] in use on router - hotspot beacon parked");
}

void WebServerComm::giveUpStepMachine() {
  switch (giveUpStep_) {
    case GIVEUP_IDLE:
      return;

    case GIVEUP_DISCONNECT:
      noteRadioStep("giveup/disconnect");
      WiFi.disconnect();
      giveUpStep_ = GIVEUP_MODE;
      return;

    case GIVEUP_MODE:
      // On its own pass, deliberately: this is the driver restart, and it is the
      // single most expensive call on this path.
      noteRadioStep("giveup/mode-AP");
      WiFi.mode(WIFI_AP);
      giveUpStep_ = GIVEUP_AP;
      return;

    case GIVEUP_AP:
      // `apRunning_` is forced false rather than trusted, because the
      // AP_STA→AP switch can bounce the beacon (FIN-40/D1) — but only the beacon
      // claim is forced; the call itself still goes through `startOwnAP()`.
      noteRadioStep("giveup/softAP");
      apRunning_ = false;
      startOwnAP();
      // U-86: the beacon is back, so it is no longer parked. Without this the
      // power policy would believe the beacon is still down while it is up, and
      // the next association would skip the grace period.
      apParked_ = false;
      giveUpStep_ = GIVEUP_IDLE;
      // U-92: one sequence now serves two masters — the failed-join give-up
      // AND the deliberate "back to my own hotspot" return (T-66). Say which.
      if (hasStoredWifi_) {
        Serial.println("WiFi STA gave up (router unreachable) - AP-only mode");
      } else {
        Serial.println(
            "WiFi STA left the router - back on the car's own hotspot");
      }
      updateExtraInfo();
      if (modeManager) modeManager->sendState();
      return;
  }
}

// U-75: print the step BEFORE it runs.
//
// A hang emits no serial output of its own, so the last breadcrumb printed is
// the step that hung. Without this the owner can only ever be told "loop()
// blocked somewhere"; with it, the next occurrence names the culprit line. The
// cost is one short line per network transition.
void WebServerComm::noteRadioStep(const char* step) {
  Serial.print("[RADIO] ");
  Serial.println(step);
}

// U-76: is the STA interface busy? While it is, NOTHING may reconfigure the
// driver or the netif - see the long note at the top of startOwnAP().
//
// Written as an ALLOW-LIST of the settled states rather than a deny-list of the
// busy ones, for two reasons:
//
//   - This core's `wl_status_t` has no `WL_CONNECTING` at all (Arduino CI caught
//     that), so "connecting" cannot be named directly. It is covered by
//     `connectingPending_`, which we set the moment we issue `WiFi.begin()`.
//   - Anything this core adds later, or any transition state we have not
//     enumerated, must default to BUSY. Being wrong in that direction only defers
//     an AP bring-up by one retry interval; being wrong the other way is the
//     watchdog reboot this whole change exists to stop.
//
// WL_CONNECTED counts as busy on purpose: ESP-IDF keeps refusing config sets
// through DHCP, which is exactly the window the owner's log caught - our code
// believed the link was up while the driver said "connecting".
bool WebServerComm::staBusy() const {
  if (connectingPending_) return true;
  switch (WiFi.status()) {
    case WL_IDLE_STATUS:
    case WL_NO_SSID_AVAIL:
    case WL_SCAN_COMPLETED:
    case WL_CONNECT_FAILED:
    case WL_DISCONNECTED:
    case WL_CONNECTION_LOST:
      return false;  // settled, and not currently on a router
    default:
      return true;   // WL_CONNECTED + any unknown/transition state
  }
}

void WebServerComm::advanceRejoinStep() {
  // U-71: one radio operation per pass. Every branch returns immediately after
  // advancing, so the caller (handle()) never spends more than a single driver
  // call inside one loop iteration.
  switch (rejoinStep_) {
    case REJOIN_STEP_IDLE:
      return;

    case REJOIN_STEP_MODE:
      // U-90 (2026-10-04, owner: *"the car seems stuck on one router and doesnt
      // connect even if on same network"*): this step is now UNCONDITIONAL.
      //
      // It used to run only `if (WiFi.getMode() != WIFI_AP_STA)`. After the
      // FIRST switch of the car's life the driver is already WIFI_AP_STA
      // (beginAlwaysOn() left it there), so every LATER switch skipped the
      // driver reset entirely — and REJOIN_STEP_BEGIN then issued WiFi.begin()
      // ON TOP OF the still-associated STA. Two consequences, both proven by
      // the U-76 serial evidence (`E wifi:sta is connecting, cannot set
      // config`):
      //
      //   1. The IDF REFUSES the begin (the STA is busy), so the car never
      //      even attempted the router it was asked for.
      //   2. Worse: the resolve block keys on `WiFi.status() == WL_CONNECTED`
      //      — which the OLD association still satisfied — so the switch
      //      resolved as SUCCESS on the OLD network. The car reported the
      //      old router as joined, the DHCP lease was never refreshed, and
      //      no retry ever ran (no failure was ever seen). That is exactly
      //      "stuck on one router, and it won't connect even to the same
      //      network": the association and its lease went stale and every
      //      later request was swallowed by the stale link.
      //
      // `WiFi.mode(WIFI_AP_STA)` restarts the WiFi driver: the stale STA
      // state is torn down, and REJOIN_STEP_BEGIN now runs against a clean
      // slate. The cost is one driver restart per explicit switch — the same
      // cost the first-ever switch already paid, one radio call per loop
      // pass (U-71), so the watchdog rule (F-24/F-60) is untouched.
      noteRadioStep("rejoin/mode-AP_STA");
      WiFi.mode(WIFI_AP_STA);
      // U-76: `WiFi.mode()` RESTARTS the driver, which tears the softAP down -
      // but `apRunning_` was still true, so every later startOwnAP() returned
      // early at `if (apRunning_) return;` while the beacon was actually DOWN.
      // That is why the owner's log shows `rejoin/softAP` and then silence: the
      // AP step did nothing. Never trust apRunning_ across a driver restart.
      apRunning_ = false;
      rejoinStep_ = REJOIN_STEP_AP;
      return;

    case REJOIN_STEP_AP:
      // T-65: the own hotspot is never torn down during a network change, so
      // the phone keeps a way in for the whole switch.
      //
      // U-86: that guarantee is now scoped. During the switch the beacon stays
      // up (the car is mid-network-change and needs the fallback); once the new
      // router holds for AP_PARK_GRACE_MS, updateApPowerPolicy() parks it again.
      // So the beacon is up when it matters and down when it does not.
      noteRadioStep("rejoin/softAP");
      apParked_ = false;   // this switch brought the beacon back on purpose
      startOwnAP();
      rejoinStep_ = REJOIN_STEP_BEGIN;
      return;

    case REJOIN_STEP_BEGIN:
      rejoinStep_ = REJOIN_STEP_IDLE;
      if (!hasStoredWifi_) {
        connectingPending_ = false;
        startedAsAP = true;
        // U-92 (owner: *"says, car switched to 4wdcar_wifi but doent switch
        // that one, but switches to other router fine"*): "back to my own
        // hotspot" used to stop HERE — rejoinNetwork() armed nothing
        // (pendingRejoin_ keys on hasStoredWifi_, which T-66 had just
        // cleared), so the car ANSWERED `ROUTERS;USED;4WDCar_Wifi` and then
        // simply stayed associated to the router. There is no target to
        // begin(), but there IS an association to LEAVE: hand off to the
        // existing one-call-per-pass teardown (disconnect → mode(WIFI_AP) →
        // startOwnAP) — the exact sequence the give-up path already proved
        // on hardware.
        giveUpStep_ = GIVEUP_DISCONNECT;
        updateExtraInfo();
        return;
      }
      connectingPending_ = true;
      connectStartMs_ = millis();
      noteRadioStep("rejoin/begin");
      WiFi.begin(storedSsid_, storedPass_);
      FW_LOG_VERBOSE("[WIFICFG] STA begin issued for \"%s\"\n", storedSsid_);
      return;
  }
}

// U-71 (2026-10-02): the owner's request - *"save the last connected router
// configs for the power restart too if that router is in the range to connect if
// not goes back to esps own hottspot"*.
//
// `storedSsid_` is the router we were ASKED to join and is written to NVS the
// moment `ROUTERS;USE` stores it - BEFORE any association is attempted. So a
// FAILED switch overwrote the good router with a bad one, and a boot that
// trusted `storedSsid_` would dutifully retry the router that had just failed.
//
// `lastGoodSsid_` / `lastGoodPass_` record the router that ACTUALLY associated.
// They are written only from the connect-success path, and they are what a boot
// auto-join uses. "What we tried" and "what worked" are now separate facts.
void WebServerComm::saveLastGoodRouter() {
  if (!hasStoredWifi_ || !storedSsid_[0]) return;
  if (strcmp(lastGoodSsid_, storedSsid_) == 0 &&
      strcmp(lastGoodPass_, storedPass_) == 0) {
    return;  // unchanged - do not churn NVS on every reconnect
  }
  strncpy(lastGoodSsid_, storedSsid_, ROUTER_SSID_LEN - 1);
  lastGoodSsid_[ROUTER_SSID_LEN - 1] = '\0';
  strncpy(lastGoodPass_, storedPass_, ROUTER_PASS_LEN - 1);
  lastGoodPass_[ROUTER_PASS_LEN - 1] = '\0';
  Preferences prefs;
  prefs.begin("botcfg", false);
  prefs.putString("last_ok_ssid", lastGoodSsid_);
  prefs.putString("last_ok_pass", lastGoodPass_);
  prefs.end();
  FW_LOG_VERBOSE("[WIFICFG] last-good router remembered: \"%s\"\n", lastGoodSsid_);
}

void WebServerComm::armBootAutoJoin() {
  // U-71: on power-up, try the router that last WORKED - and only that one.
  //
  // The budget is deliberately SHORT. This reverses R-16/T-64 ("the own AP is THE
  // default network; a stored router is joined ONLY on explicit request"), which
  // the owner has now overridden - but the reason that rule existed still stands:
  // an unreachable router must never keep the car off its own hotspot, because
  // the hotspot is the only way in when the router is not there. So the boot
  // attempt gets ONE association window (BOOT_STA_TIMEOUT_MS) and then settles
  // AP-only immediately. The full retry budget is reserved for an EXPLICIT user
  // switch, where the user is waiting and watching.
  //
  // `saveLastGoodRouter()` is what fills these, so a router that has never
  // connected is never auto-joined - there is no point burning the boot window on
  // something that has already failed once.
  if (!lastGoodSsid_[0]) {
    FW_LOG_VERBOSE("[WIFICFG] boot: no last-good router, staying on own hotspot\n");
    return;
  }
  FW_LOG("[WIFICFG] boot: will try last-good router \"%s\" (short budget)\n",
         lastGoodSsid_);
  // Adopt it as the active target for this attempt only. The saved-routers
  // registry and `storedSsid_` are untouched, so an explicit switch still wins.
  strncpy(storedSsid_, lastGoodSsid_, ROUTER_SSID_LEN - 1);
  storedSsid_[ROUTER_SSID_LEN - 1] = '\0';
  strncpy(storedPass_, lastGoodPass_, ROUTER_PASS_LEN - 1);
  storedPass_[ROUTER_PASS_LEN - 1] = '\0';
  hasStoredWifi_ = true;
  rejoinNetwork();
  // U-75: the SHORT boot budget is applied AFTER rejoinNetwork(), because that
  // function now sets the full explicit-switch budget for every caller. Order
  // matters: this is a boot-time concession, not a policy for the session.
  staTimeoutMs_ = BOOT_STA_TIMEOUT_MS;
  staRetryCap_ = BOOT_STA_MAX_RETRIES;
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

  // U-77: stop() is the shutdown path - the mDNS advertisement belongs to the
  // network layer it advertised, so it must go down with it (stopMdns() was
  // defined but never called).
  stopMdns();
  WiFi.disconnect();
  if (apRunning_) WiFi.softAPdisconnect(true);
  apRunning_ = false;
  // U-86: everything is down, so nothing is "parked" — it was stopped. Otherwise
  // a later begin() would start with apParked_ true and the power policy would
  // refuse to bring the beacon back up.
  apParked_ = false;
  staConnectedAtMs_ = 0;

  startedAsAP = false;
  queueClear();
  FW_LOG_VERBOSE("Free heap after stop: %d\n", static_cast<int>(ESP.getFreeHeap()));
}

bool WebServerComm::isRunning() const { return serverRunning; }

// v1.0.1 (owner OLED directive): differentiated link truth for the OLED.
bool WebServerComm::hasWsClients() { return ws.connectedClients() > 0; }

bool WebServerComm::isStaJoined() const { return WiFi.status() == WL_CONNECTED; }
// U-70 (2026-10-02): the CURRENT network, as opposed to the stored target.
//
// The bug this fixes is the owner's 2026-10-02 report: *"the car is displaying
// the new router in its oled but not switching actually to that new router
// network and the car still show the website only on its own esp hotspot"*.
// `storedSsid()` is written the moment `ROUTERS;USE` stores the pair — BEFORE
// any association is attempted — and the STATE line's `SSID=` field used it.
// So the car announced the router it had been *asked* to join while sitting on
// its own hotspot, and nothing in the system could tell the difference. The app
// then read that as "on the router" and the phone looked for it there.
//
// Three states, three honest answers:
//   associated to a router -> that router
//   on its own hotspot     -> its own broadcast id
//   neither                -> "" (say nothing rather than guess)
// Returned BY VALUE on purpose: `WiFi.SSID()` returns a temporary String, and
// handing back its c_str() would leave the caller holding a pointer into freed
// memory — the classic Arduino String trap, and it would have shown up as
// garbage network names rather than an obvious crash.
String WebServerComm::joinedSsid() const {
  if (WiFi.status() == WL_CONNECTED) {
    const String s = WiFi.SSID();
    if (s.length() > 0) return s;
  }
  if (apRunning_ && apName_.length() > 0) return String(apName_);
  return String("");
}

void WebServerComm::handle() {
  if (serverRunning) server.handleClient();
  // U-95 Phase 1b: a fresh window for the one call here nobody can tune.
  // ws.loop() can sit inside a partial-frame read for up to
  // WEBSOCKETS_TCP_TIMEOUT (5000 ms — the library's own TU, untunable from the
  // sketch). Without this feed the stall spent whatever was LEFT of the pass's
  // budget — the feed at the top of loop() minus the switch check and
  // ModeManager::run() — racing the reset instead of fitting under it. Fed
  // here, the read gets the full LOOP_TASK_WDT_TIMEOUT_MS (10000 ms) to
  // finish, with 5000 ms to spare. The gate rule is `ws-loop-feed`.
  esp_task_wdt_reset();
  ws.loop();

  // U-86: park the setup beacon once the car is genuinely reachable on a
  // router. Cheap (no radio work unless the state actually flips) and it is
  // the single biggest available power win. Runs FIRST so a router that drops
  // gets its beacon back before anything else is attempted.
  updateApPowerPolicy();

  if (pendingModeBuf[0] != '\0') {
    unsigned long now = millis();
    if (now - pendingModeAtMs >= 200) {
      pendingModeBuf[0] = '\0';
      pendingModeAtMs = 0;
    }
  }

  // U-71: ONE step of the rejoin state machine per pass. This replaces the old

  // synchronous mode/softAP/begin sequence that ran inside the COMMAND path,

  // where a driver restart + softAP + begin could occupy loop() long enough to

  // trip the 5 s task watchdog - and a WDT trip is a reboot (F-24).

  advanceRejoinStep();

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
      // U-71: this association WORKED, so it becomes the router a power-up will
      // try. Written only here - never on request - so a failed switch cannot
      // overwrite a working router with a broken one.
      saveLastGoodRouter();
      // U-74: the car is now on a DIFFERENT network with a different address, so
      // re-publish rather than leave an advertisement pointing at the old one.
      startMdns();
      // A short boot budget must not follow us into normal operation.
      staTimeoutMs_ = WIFI_STA_TIMEOUT_MS;
      staRetryCap_ = STA_MAX_RETRIES;
      if (modeManager) modeManager->sendState();
      if (modeManager) modeManager->resetFailsafe();
    } else if (millis() - connectStartMs_ > staTimeoutMs_) {
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
    if (hasStoredWifi_ && staRetryCount_ < staRetryCap_) {
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
        // U-77: this branch had the only silent radio calls left — a hang here
        // produced a WDT reboot with no breadcrumb (U-75 rule: name the step
        // BEFORE it runs).
        noteRadioStep("retry/disconnect");
        WiFi.disconnect();
        noteRadioStep("retry/begin");
        WiFi.begin(storedSsid_, storedPass_);
      }
    } else if (hasStoredWifi_ && !staGiveUpLogged_) {
      // Budget spent: settle AP-only.
      //
      // U-75: this branch now DOES NO RADIO WORK. It used to run
      // `WiFi.disconnect(); WiFi.mode(WIFI_AP); apRunning_ = false;
      // startOwnAP();` in one pass — a teardown, a full driver RESTART and a
      // softAP bring-up back to back. The owner proved that is fatal by flashing
      // this firmware and getting `[BOOT] reset reason: 6 (TASK WATCHDOG)`:
      // loop() blocked past 5 s and the watchdog's action is a reboot.
      //
      // It is ARMED here and executed one call per pass in
      // `giveUpStepMachine()`, which `handle()` always runs — the same fix, and
      // for the same reason, as `advanceRejoinStep()`.
      //
      // Note the ordering consequence, which is intentional: `startedAsAP` is
      // set now so the UI immediately stops claiming a router, while the beacon
      // itself comes back a pass or two later. A truthful "falling back" that is
      // a second stale is strictly better than a watchdog reboot.
      staGiveUpLogged_ = true;
      startedAsAP = true;
      giveUpStep_ = GIVEUP_DISCONNECT;
      updateExtraInfo();
      // R-16/pronto: immediate STATE so the remote + app drop the stale SSID
      // row the same second the car settles AP-only.
      if (modeManager) modeManager->sendState();
    }
  }

  // U-76: U-76 gave up the give-up fallback, one blocking radio call per pass.
  // Never more than one driver call per loop iteration, so the task watchdog is
  // always fed.
  giveUpStepMachine();

  // U-76: service a mDNS publication that was DEFERRED because the STA was busy.
  // Without this the deferral would be permanent - the advertisement would simply
  // never appear on a router the car actually joined, which is the one case it
  // exists for. Retried on an interval, and only once the STA has settled, so it
  // can never be the thing that blocks loop().
  {
    const unsigned long nowMs = millis();
    if (mdnsPending_ && !staBusy() &&
        nowMs - lastMdnsRetryMs_ >= MDNS_RETRY_INTERVAL_MS) {
      lastMdnsRetryMs_ = nowMs;
      startMdns();
    }
  }

  // FIN-40/D1: a softAP() that failed at bring-up (radio busy) is retried with
  // backoff until the beacon is actually up — apRunning_ only turns true on a
  // real success now, so ESP_SER always ends up broadcasting 4WDCar_Wifi.
  {
    const unsigned long apNow = millis();
    if (!apRunning_ && apNow - lastApRetryMs_ >= AP_RETRY_INTERVAL_MS) {
      // U-76: never re-arm an AP bring-up while the STA is busy. `softAP()` and
      // `softAPConfig()` are both illegal then, and `softAP()` against a live
      // association can block long enough to trip the task watchdog - which is
      // the reboot the owner reported. The retry simply waits for the STA to
      // settle; nothing is lost, because the beacon is up whenever the STA is not
      // busy (T-65).
      if (staBusy()) {
        lastApRetryMs_ = apNow;  // do not spin: re-check after a full interval
      } else {
        lastApRetryMs_ = apNow;
        startOwnAP();
      }
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

// U-81 (2026-10-03): THIS WAS A SINK — `return len;` accepted every byte and
// threw it away. It is the reason the owner's report was "the new home router
// is not there".
//
// `comm_` IS this object (`Genum_4WD4M_CAR.ino:190` -> `setComm(&webServerComm)`),
// and every text line ModeManager produces goes through here:
//   ModeManager.cpp:535  STATE;...;REPLY=<the router/wifi answer>
//   ModeManager.cpp:545  CAPS;...        (the availability map)
//   ModeManager.cpp:555  NETW;...        (saved-router names, the BT shape)
//   ModeManager.cpp:566  SCAN;...        (nearby networks)
//   ModeManager.cpp:841  NACK;E=UNKNOWN_MODE;ARG=<token>
//
// So over WiFi the entire text protocol was unreachable, and with it every
// router acknowledgement. The owner-visible chain was:
//   add a router -> the car stores it and queues ROUTERS;ADDED -> that answer is
//   written to this void -> the car then joins the new network -> the WS link
//   drops -> the 1 s JSON `networks` frame cannot arrive either. The new router
//   therefore appeared NOWHERE: not in the app's list, not in its saved prefs.
//   A switch was the same silence, 6 s later, as a timeout. A refused mode was
//   the same silence, as a ~2.5 s lie and a silent snap-back.
//
// Bluetooth was unaffected because `auxComm_` is `btComm`, whose write() really
// writes SerialBT - which is exactly why this read as a WiFi-only fault.
//
// `broadcastTXT` is SAFE for loop(): WebSocketsServer buffers the payload into
// each client's send buffer and returns; the TCP flush happens in the library's
// own loop. A stalled client therefore cannot block here, and the U-77
// heartbeat (enableHeartbeat(3000,1000,2)) drops a non-draining client long
// before its buffer can grow. That is the whole reason U-77 added it.
size_t WebServerComm::write(const char* buf, size_t len) {
  if (!buf || len == 0) return 0;
  ws.broadcastTXT(buf, len);
  return len;
}

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

// U-74 (2026-10-02): ADVERTISE THE CAR BY NAME ON WHATEVER NETWORK IT IS ON.
//
// Why this exists, from the owner: *"the drive deck doesnt open when on other
// router is selected"*. The chain is not a UI bug, it is structural:
//
//   1. The phone is joined to the car's OWN hotspot (192.168.245.x) and talking
//      to the car at 192.168.245.1.
//   2. The car joins the home router. In AP_STA the softAP and the STA share
//      ONE radio and therefore ONE channel, so the hotspot MOVES to the router's
//      channel. U-72 stopped the car fighting that, but the consequence is
//      unavoidable and correct: the phone's hotspot link drops.
//   3. With no link, the app has no address for the car. It cannot reach the car
//      on the router, because the router gave the car a DHCP lease the app has
//      never been told. So nothing opens.
//
// The user can read the address off the OLED, but that is a manual step in the
// middle of a "just works" flow, and the phone cannot resolve a `.local` name
// without a native module (which needs a new APK). Advertising with mDNS means
// that the moment an APK with a resolver lands, the car is findable BY NAME on
// the router with no configuration - and, importantly, the advertisement
// re-publishes itself every time the car changes network, so it is never stale.
//
// The hostname is deliberately NOT the AP name: `4WDCar_Wifi` contains an
// underscore, which is not legal in a strict mDNS hostname. `genum-car` is the
// documented name (the `mdns` transport placeholder in the app already expects
// `genum-car.local`).
void WebServerComm::startMdns() {
  // U-76: `MDNS.begin()` sets the netif hostname, so it is a DRIVER CONFIG SET and
  // ESP-IDF refuses it while the STA is connecting - which is exactly what the
  // owner's log caught (`wifi:sta is connecting, cannot set config` immediately
  // followed by `[MDNS] unavailable on this network`). mDNS is a convenience, not
  // a safety feature, so it is DEFERRED rather than forced: the flag makes
  // `handle()` retry it once the STA settles, and by then the app can already
  // find the car the other way (U-74b sweeps the phone's /24).
  if (staBusy()) {
    mdnsPending_ = true;
    noteRadioStep("defer-mdns/sta-busy");
    return;
  }
  mdnsPending_ = false;
  // Safe to call repeatedly: end() first so a re-publish after a network change
  // does not leak a previous instance.
  MDNS.end();
  if (MDNS.begin(MDNS_HOSTNAME) == 0) {
    // U-77: 0 is MDNS.begin() FAILURE. This condition used to be inverted
    // (`!= 0`), so a SUCCESSFUL publication was reported as "unavailable" and
    // abandoned before addService() — mDNS could literally never advertise.
    // A genuine failure is not re-armed: it is logged once and the car stays
    // reachable by IP (mDNS is a convenience, not a safety feature).
    FW_LOG("[MDNS] unavailable on this network\n");
    return;
  }
  // Advertise BOTH ports the app actually uses: HTTP for the car's web page and
  // status, WS for the drive link.
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("ws", "tcp", 81);
  // The instance name is what a phone's Bonjourjour/mDNS browser shows, so put
  // the real AP/broadcast name in it rather than the hostname.
  MDNS.setInstanceName(apName_.length() ? apName_.c_str() : MDNS_HOSTNAME);
  mdnsRunning_ = true;
  FW_LOG("[MDNS] advertising %s.local (http:80 ws:81)\n", MDNS_HOSTNAME);
}

void WebServerComm::stopMdns() {
  if (!mdnsRunning_) return;
  MDNS.end();
  mdnsRunning_ = false;
  FW_LOG("[MDNS] stopped\n");
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
  // U-77 (2026-10-03): STALLED-CLIENT HEARTBEAT — the fix for the last
  // un-breadcrumbed watchdog reboot. broadcastStatus() runs every second to
  // every client; a phone that stalls (screen off, app killed without a
  // close, link orphaned by a channel hop mid-switch) stops draining TCP, the
  // socket buffer fills in ~20-30 s, and the next broadcast write BLOCKS
  // inside WebSocketsServer past the 5 s task watchdog — reboot, with no
  // breadcrumb (owner log 2026-10-03: WDT ~29 s after the rejoin began,
  // exactly the buffer-fill time). With the heartbeat the server pings every
  // 3 s and drops a client after two missed pongs (~8-9 s), so a staller is
  // GONE long before its buffers can wedge a write. New clients inherit the
  // settings (WebSockets 2.7.2), so this one call covers the whole uptime.
  ws.enableHeartbeat(WS_HEARTBEAT_PING_MS, WS_HEARTBEAT_PONG_TIMEOUT_MS,
                     WS_HEARTBEAT_DROP_AFTER);
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
    // U-88 (owner: *"switching between the router is not working and the
    // addition and editing and deleting of the router is not properly built"*).
    //
    // This handler used to do BOTH of these:
    //
    //     modeManager->handleCommand(msg, CmdSource::WIRE);   // immediate
    //     queuePush(msg);                                      // and again
    //
    // so every Wi-Fi command ran TWICE. The duplicate is invisible for ADD
    // (the second pass finds the same slot and re-saves the same values) but
    // it is destructive for the others, because the second pass sees state the
    // first pass already changed:
    //
    //   DEL   -> "ROUTERS;DELETED;<ssid>"  then  "ROUTERS;ERROR;Not saved:<ssid>"
    //   CLEAR -> "ROUTERS;CLEARED"          then  "ROUTERS;CLEARED" again
    //   USE   -> two full rejoin sequences
    //
    // Both answers ride the same `REPLY=` slot of two consecutive STATE lines,
    // and the app is entitled to treat the SECOND one as the outcome of the
    // user's action. So a delete that worked reported a failure, and a switch
    // drove the radio twice. Measured on the car before this fix:
    //
    //   > ROUTERS;DEL;_probe1
    //   REPLY=ROUTERS;DELETED;_probe1
    //   REPLY=ROUTERS;ERROR;Not saved:_probe1
    //
    // T-1 is explicit that there is ONE command path: lines are queued and
    // dispatched by ModeManager::run(). So the immediate call is the wrong one
    // and queuePush alone is correct - the command still runs on the next loop
    // pass, which is imperceptible.
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
  // U-95 Phase 1c: nobody listening, nothing to build or send. The 1 s ticker
  // (and the scan-complete and REQ_STATE/REQ_NETW paths) used to build this
  // 1536-byte JSON and walk every socket every second of every uptime with no
  // connectedClients() guard at all — the plan's "client-guarded" gate.
  // hasWsClients() is already the app's link evidence (the .ino's webClient
  // and ModeManager's wsClients read it), so an idle car now returns here
  // before the JSON exists. A client that just disconnected gets skipped on
  // its last request — there is nobody left to deliver it to.
  if (!hasWsClients()) return;
  // 1536: status JSON + ssid(32) + ap(21) + caps map(~90) + networks (6×33 ≈ 200)
  // + scan (D3, 12 × ~48 ≈ 600) + the U-95 crash fields (~120). Grown for the
  // scan field, then again for the crash record.
  //
  // WHY THE NUMBER IS NOT OPTIMISED AGAIN: `buildStatusJson` bounds every
  // fragment it writes, so the failure mode of a buffer that is too small is
  // SILENT — snprintf truncates and the client receives malformed JSON, which
  // the browser reports as a parse error and the owner reports as "the page is
  // not working properly". A silently truncated diagnostic is worse than no
  // diagnostic. tools/check-relay-contract.mjs asserts this buffer is larger
  // than the sum of the fragment buffers it has to hold, so the next person to
  // add a field without growing it fails CI instead of shipping a broken page.
  char buf[1536];
  buildStatusJson(buf, sizeof(buf));
  // U-95 Phase 1c: the write gets its OWN fresh window. To a client that
  // stopped draining TCP this call retried for 5 s of no progress with each
  // inner tcp->write() blocked up to 3 s (the U-77 measurement) — and it can
  // run AFTER a ws.loop() that already spent 5000 ms of the same window, so
  // feeding only before ws.loop() does not cover it. Fed here, the worst write
  // has all of LOOP_TASK_WDT_TIMEOUT_MS (10000 ms, 2000 ms over the measured
  // 8000 ms worst) to itself. The long-lived staller is the heartbeat's
  // problem (dropped in ~8-9 s, U-77); this covers the FIRST wedge. The gate
  // rule is `ws-broadcast-feed`.
  esp_task_wdt_reset();
  ws.broadcastTXT(buf);
}

// --- HTTP handlers ---

// U-77: cap every write of the CURRENT request's socket. WebServer's default
// socket send timeout is the core's 3 s connect default, so a stalled client
// turned every chunk of a response into a multi-second block inside loop().
// 500 ms per write keeps each handler bounded; the root page additionally
// gets a TOTAL budget (serveRootBounded).
void WebServerComm::armHttpGuard() {
  NetworkClient& c = server.client();
  c.setNoDelay(true);
  c.setTimeout(HTTP_WRITE_TIMEOUT_MS);
}

// U-77: the ~18 kB root page with a TOTAL write budget. `server.send_P` had
// NO total bound: to a slow-but-moving client (weak signal right after a
// network switch — the owner's "car restarted as soon as I tried to access
// the webpage") chunk after chunk blocked loop() past the 5 s task watchdog
// even though no single write exceeded its timeout. Here: headers via
// server.send() with the exact Content-Length, then the body written in
// bounded chunks; if the total budget or the connection runs out the page
// arrives truncated — the client is dead anyway, and a truncated page beats
// a rebooted car.
//
// U-81 (2026-10-03): the truncation was HONEST Content-Length + a SHORT body,
// and then the handler simply returned. The browser had been promised N bytes
// and had received fewer, so it sat waiting for the rest until its own
// timeout — a page that loads as a blank/hanging document, which is exactly
// the "the webpage is not functioning properly" report. Two changes:
//   1. the budget is now HTTP_BODY_BUDGET_MS = 6 s (was 3 s). The per-write cap
//      (500 ms) plus the yield() below are what actually keep loop() safe, so
//      the total wall-clock budget was never the thing protecting the watchdog;
//      3 s was simply tight enough to truncate on an ordinary phone.
//   2. on ANY short write we CLOSE the socket, so the client is told "this
//      response ended" immediately instead of hanging on a promise the car
//      will never keep.
void WebServerComm::serveRootBounded() {
  armHttpGuard();
  // U-95/F-79: name the operation in the crash record. This was the #1 suspect
  // for the owner's reboot loop — the send loop below is bounded to
  // HTTP_BODY_BUDGET_MS, which until F-77 was LARGER than the 5 s task watchdog,
  // so a client that stopped reading deterministically reset the car here. If
  // the car ever resets again while a phone is loading the page, the record
  // will say "http-root" and the hunt starts in the right place instead of
  // everywhere. Cleared at the end of loop(), so it is written on the way out.
  crashLogPhase("http-root");
  const size_t pageLen = strlen_P(PAGE_HTML);
  server.setContentLength(pageLen);
  server.send(200, "text/html; charset=utf-8", "");
  NetworkClient& c = server.client();
  const unsigned long start = millis();
  size_t off = 0;
  char chunk[512];
  while (off < pageLen) {
    // U-95/F-77, part one: the budget is checked BEFORE the write, so the loop
    // can never begin an iteration it already knows is out of time. It was 6000
    // ms against a 5000 ms watchdog, which made the guard a countdown to the
    // reset rather than a bound on the work.
    if (millis() - start > HTTP_BODY_BUDGET_MS) {
      FW_LOG("[HTTP] root page aborted (client too slow): %u/%u bytes\n",
             static_cast<unsigned>(off), static_cast<unsigned>(pageLen));
      break;
    }
    if (!c.connected()) break;
    const size_t n = (pageLen - off < sizeof(chunk)) ? (pageLen - off) : sizeof(chunk);
    memcpy_P(chunk, PAGE_HTML + off, n);
    const size_t w = c.write((const uint8_t*)chunk, n);
    // U-95/F-77, part two — THE ACTUAL DEFECT, and the part that was easy to
    // miss because it looks like a courtesy: on a zero-byte write this loop did
    // yield(); continue; — no progress, no watchdog feed, and it could spin for
    // the whole budget. esp_task_wdt_reset() is what keeps loop() alive, so a
    // loop that spins without feeding it is a reset generator no matter how
    // carefully the surrounding numbers were chosen. This is the line the
    // comment used to justify itself with.
    //
    // The order matters and is not interchangeable: feed the watchdog, wait out
    // the gap so a stalled client cannot become a busy spin, and only then
    // re-check the budget. Feeding last would still be correct here but reads
    // as if the budget check were the thing keeping the task alive.
    esp_task_wdt_reset();
    if (w == 0) {
      // A real gap, not a bare yield(): yield() returns immediately to the same
      // instruction, so 28 kB of stalled page was ~millions of wasted
      // iterations inside one loop pass. delay() parks the task, which also
      // gives the TCP window time to reopen instead of re-testing it instantly.
      const unsigned long gapStart = millis();
      while (millis() - gapStart < HTTP_WRITE_RETRY_GAP_MS) {
        esp_task_wdt_reset();
        delay(1);
      }
      continue;
    }
    off += w;
    // Feed on the progress path too. The 512 B chunk usually returns in
    // microseconds, but a write that fills the socket buffer blocks for up to
    // HTTP_WRITE_TIMEOUT_MS, and a run of those can reach the budget; feeding
    // here means the watchdog is satisfied by progress, not only by the stall
    // path above.
    esp_task_wdt_reset();
  }
  // A short body after an honest Content-Length leaves the browser waiting for
  // bytes that will never arrive. End the response instead of hanging it.
  if (off < pageLen && c.connected()) {
    // We are about to break a promise made in a header we already sent. Close
    // the connection so the client stops waiting for bytes that will never come.
    c.stop();
  }
}

void WebServerComm::handleRoot() {
  FW_LOG_VERBOSE("DBG: HTTP GET / received\n");
  serveRootBounded();
}

void WebServerComm::handleMove(const char* cmd) {
  armHttpGuard();  // U-77: bounded writes on every HTTP response
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
  armHttpGuard();  // U-77
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
  armHttpGuard();  // U-77
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
  armHttpGuard();  // U-77
  // Same size and same reason as broadcastStatus()'s buffer — and it MUST be the
  // same size. These two had drifted apart once (1200 here vs a smaller value in
  // broadcastStatus) with no symptom at all except that the two copies of the
  // same JSON disagreed on which one got truncated. See broadcastStatus().
  char buf[1536];
  buildStatusJson(buf, sizeof(buf));
  server.send(200, "application/json", buf);
}

void WebServerComm::handleNotFound() {
  FW_LOG_VERBOSE("DBG: HTTP NotFound for URI: %s\n", server.uri().c_str());
  armHttpGuard();  // U-77
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
  // U-70 (2026-10-02): the JSON `ssid` is the network the car is ACTUALLY on.
  // It used to be the stored target, which is written the moment `ROUTERS;USE`
  // stores the pair and BEFORE any association is attempted — so a failed switch
  // told the app "on HomeNet" while the car was still on its own hotspot, and the
  // phone then looked for it on a network it had never reached. The owner's
  // report: *"the car is displaying the new router in its oled but not switching
  // actually to that new router network and the car still show the website only
  // on its own esp hotspot."* `want` carries the requested target, so a switch
  // in progress is visible instead of indistinguishable from a finished one.
  const String joinedSsidStr = joinedSsid();
  const char* ssidOut = joinedSsidStr.c_str();
  const char* wantOut =
    (hasStoredWifi_ && joinedSsidStr != storedSsid_) ? storedSsid_ : "";
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
  // U-95/F-79: the restart record, over the wire. Until this existed, the only
  // reason the car had rebooted was a Serial line printed at the instant of the
  // fault, which is exactly the moment nobody is watching. `/status` is a place
  // the owner already goes, so the evidence now lives where they already are.
  //
  // The two phase strings are firmware string LITERALS passed to
  // crashLogPhase() — never anything a phone or browser sent — so they carry no
  // quotes or backslashes and need no JSON escaping. Stated explicitly because
  // the next person to add a phase from a runtime string will get this wrong,
  // and "it was only ever internal literals" is not a property any tool checks.
  const CrashInfo& ci = crashLogInfo();
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
       "\"reset_reason\":\"%s\","
       "\"boot_count\":%lu,"
       "\"crash_count\":%lu,"
       "\"last_crash_phase\":\"%s\","
       "\"last_crash_heap\":%lu,"
       "\"phase\":\"%s\","
       "\"speed\":%d,"
       "\"trim\":%d,"
       "\"min_speed\":%d,"
       "\"max_speed\":%d,"
       "\"ssid\":\"%s\","
       "\"want\":\"%s\","
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
      ci.label,
      static_cast<unsigned long>(ci.bootCount),
      static_cast<unsigned long>(ci.crashCount),
      ci.phase,
      static_cast<unsigned long>(ci.heapAtBoot),
      crashLogLivePhase(),
     speed,
     static_cast<int>(modeManager ? modeManager->getTrim() : 0),
     static_cast<int>(MIN_SPEED),
     static_cast<int>(MAX_SPEED),
     ssidOut,
     wantOut,
     apOut,
     boardId,
     networks,
     scanJson
  );
}
