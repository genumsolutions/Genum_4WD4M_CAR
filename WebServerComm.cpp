// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: WebServerComm.cpp — web + WebSocket server transport: command
//       queue bridge, JSON status broadcast, WIFICFG provisioning
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include "WebServerComm.h"
#include "WebPage.h"
#include "CrashLog.h"     // U-95/F-79: the restart record, surfaced in /status
#include "Config.h"       // U-79: transportPowerMux for atomic state transitions
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
  connectingPending_(false), pendingRejoin_(false), retryBeginPending_(false), connectStartMs_(0), lastWifiRetryMs_(0),
  staRetryCount_(0), staGiveUpLogged_(false), lastApRetryMs_(0),
  mdnsRunning_(false), rejoinStep_(REJOIN_STEP_IDLE), modeWaitStartMs_(0), modeDropAttempts_(0), staConnectedAtMs_(0), staTimeoutMs_(WIFI_STA_TIMEOUT_MS), staRetryCap_(STA_MAX_RETRIES),
  scanPending_(false), scanBusy_(false), scanStartMs_(0), scanWaitMs_(0), scanCount_(0),
    serverRunning(false), startedAsAP(false), apRunning_(false), apTxPowerLow_(false),
    modeManager(nullptr), pendingModeAtMs(0)
{
  for (size_t i = 0; i < CMD_QUEUE_LEN; ++i) cmdQueue[i][0] = '\0';
  pendingModeBuf[0] = '\0';
  hasStoredWifi_ = false;
  storedSsid_[0] = '\0';
  storedPass_[0] = '\0';
  // U-108 Phase 7: the app-provided clock starts UNSYNCED — the first `TIME;`
  // line from the app sets it. Until then nowEpoch() returns 0 and no record is
  // stamped, which is honest rather than inventing a time.
  epochSynced_ = false;
  epochBase_ = 0;
  epochSetMs_ = 0;
  configEpoch_ = 0;
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
//   WIFICFG;WIFI;ON|OFF        Wi-Fi transport toggle (U-105, NVS `t_wifi`); bare / `?` reports
//   WIFICFG;BT;ON|OFF          Bluetooth transport toggle (U-105, NVS `t_bt`); bare / `?` reports
//   WIFICFG;AUTOJOIN;ON|OFF    boot auto-join of the last-switched router (U-105, NVS `boot_autojoin`)
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
    // U-98: the last-good pair must die with the stored pair, or a later boot
    // could re-join the very router RESET just un-forgot (the deleted-router
    // resurrection the owner reported). Boot auto-join reads storedSsid_ only
    // now, but a stale last-good would still be a trap on any later USE replay.
    prefs.remove("last_ok_ssid");
    prefs.remove("last_ok_pass");
    stampConfigWrite(prefs);
    prefs.end();
    lastGoodSsid_[0] = '\0';
    lastGoodPass_[0] = '\0';
    hasStoredWifi_ = false;
    storedSsid_[0] = '\0';
    storedPass_[0] = '\0';
    snprintf(reply, sizeof(reply), "WIFICFG;CLEARED");
  } else if (strncasecmp(rest, ";WIFI", 5) == 0) {
    // U-105 (owner 2026-10-13): two transport toggles (Wi-Fi / Bluetooth)
    // REPLACE the old parallel-links + space-power-save (APSAVE) switches —
    // their command branches, NVS keys and app settings are gone (removed as
    // "residues"). `WIFICFG;WIFI;ON|OFF` switches the Wi-Fi transport (own
    // hotspot + router link); persisted to NVS `botcfg/t_wifi`. The U-79 loop
    // in the .ino enforces this switch every pass (OFF = web layer stopped).
    const char* arg = rest + 5;  // "" | ";ON" | ";OFF" | ";?"
    if (arg[0] == '\0' || (arg[0] == ';' && (arg[1] == '?' || arg[1] == '\0'))) {
      snprintf(reply, sizeof(reply), "WIFICFG;WIFI;%s", wifiEnabled_ ? "ON" : "OFF");
    } else if (strcasecmp(arg, ";ON") == 0 || strcasecmp(arg, ";OFF") == 0) {
      bool next = (strcasecmp(arg, ";ON") == 0);
      // The car can never have BOTH transports off (owner: *"both cant be off
      // at the same time"*) — turning the last one off is refused and the real
      // state is re-answered, so the reply always matches reality.
      if (!next && !btEnabled_) {
        snprintf(reply, sizeof(reply), "WIFICFG;WIFI;%s", wifiEnabled_ ? "ON" : "OFF");
      } else {
        wifiEnabled_ = next;
        Preferences prefs;
        prefs.begin("botcfg", false);
        prefs.putBool("t_wifi", wifiEnabled_);
        stampConfigWrite(prefs);
        prefs.end();
        FW_LOG_VERBOSE("[WIFICFG] Wi-Fi transport %s\n", wifiEnabled_ ? "ON" : "OFF");
        snprintf(reply, sizeof(reply), "WIFICFG;WIFI;%s", wifiEnabled_ ? "ON" : "OFF");
      }
    } else {
      snprintf(reply, sizeof(reply), "WIFICFG;ERROR;Syntax");
    }
  } else if (strncasecmp(rest, ";BT", 3) == 0) {
    // `WIFICFG;BT;ON|OFF` — the Bluetooth transport. Same clamp: neither can
    // be off while the other one is; persisted to NVS `botcfg/t_bt`.
    const char* arg = rest + 3;  // "" | ";ON" | ";OFF" | ";?"
    if (arg[0] == '\0' || (arg[0] == ';' && (arg[1] == '?' || arg[1] == '\0'))) {
      snprintf(reply, sizeof(reply), "WIFICFG;BT;%s", btEnabled_ ? "ON" : "OFF");
    } else if (strcasecmp(arg, ";ON") == 0 || strcasecmp(arg, ";OFF") == 0) {
      bool next = (strcasecmp(arg, ";ON") == 0);
      if (!next && !wifiEnabled_) {
        snprintf(reply, sizeof(reply), "WIFICFG;BT;%s", btEnabled_ ? "ON" : "OFF");
      } else {
        btEnabled_ = next;
        Preferences prefs;
        prefs.begin("botcfg", false);
        prefs.putBool("t_bt", btEnabled_);
        stampConfigWrite(prefs);
        prefs.end();
        FW_LOG_VERBOSE("[WIFICFG] Bluetooth transport %s\n", btEnabled_ ? "ON" : "OFF");
        snprintf(reply, sizeof(reply), "WIFICFG;BT;%s", btEnabled_ ? "ON" : "OFF");
      }
    } else {
      snprintf(reply, sizeof(reply), "WIFICFG;ERROR;Syntax");
    }
  } else if (strncasecmp(rest, ";AUTOJOIN", 9) == 0) {
    // `WIFICFG;AUTOJOIN;ON|OFF` — boot auto-join the last-switched router
    // (U-98). Default ON; persisted to NVS `botcfg/boot_autojoin`. The .ino's
    // boot path and the U-79 resume path both honor it via armBootAutoJoin().
    const char* arg = rest + 9;  // "" | ";ON" | ";OFF" | ";?"
    if (arg[0] == '\0' || (arg[0] == ';' && (arg[1] == '?' || arg[1] == '\0'))) {
      snprintf(reply, sizeof(reply), "WIFICFG;AUTOJOIN;%s", bootAutojoin_ ? "ON" : "OFF");
    } else if (strcasecmp(arg, ";ON") == 0 || strcasecmp(arg, ";OFF") == 0) {
      bootAutojoin_ = (strcasecmp(arg, ";ON") == 0);
      Preferences prefs;
      prefs.begin("botcfg", false);
      prefs.putBool("boot_autojoin", bootAutojoin_);
      stampConfigWrite(prefs);
      prefs.end();
      FW_LOG_VERBOSE("[WIFICFG] boot auto-join %s\n", bootAutojoin_ ? "ON" : "OFF");
      snprintf(reply, sizeof(reply), "WIFICFG;AUTOJOIN;%s", bootAutojoin_ ? "ON" : "OFF");
    } else {
      snprintf(reply, sizeof(reply), "WIFICFG;ERROR;Syntax");
    }
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
      stampConfigWrite(prefs);
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

// ---- U-108 Phase 7 (owner 2026-10-13): fresh start + timestamps ----
//
//   TIME;<unixEpoch>   the APP's clock (the car has no RTC). Sent on every
//                      connect; adopted so config writes can be stamped.
//   TIME? / TIME       report `TIME;<now>;<configEpoch>`.
//   FACTORY;RESET      wipe the WHOLE `botcfg` namespace + restore RAM defaults
//                      + reset the boot/crash counters; reply `FACTORY;CLEARED`.
//
// The wire format mirrors WIFICFG/ROUTERS: consumed inside the single
// system-command hook, answered as `REPLY=…` on STATE.
bool WebServerComm::handleFactoryCommand(const char* cmd) {
  if (!cmd) return false;
  const bool isFactory = strncasecmp(cmd, "FACTORY", 7) == 0 &&
                         (cmd[7] == '\0' || cmd[7] == '?' || cmd[7] == ';');
  const bool isTime = strncasecmp(cmd, "TIME", 4) == 0 &&
                      (cmd[4] == '\0' || cmd[4] == '?' || cmd[4] == ';');
  if (!isFactory && !isTime) return false;

  char reply[96];

  if (isTime) {
    const char* arg = cmd + 4;  // "" | "?" | ";" | ";<epoch>"
    if (arg[0] == '\0' || arg[0] == '?' || (arg[0] == ';' && arg[1] == '\0')) {
      snprintf(reply, sizeof(reply), "TIME;%lu;%lu",
               (unsigned long)nowEpoch(), (unsigned long)configEpoch_);
    } else if (arg[0] == ';') {
      const char* num = arg + 1;
      uint32_t v = 0;
      bool ok = num[0] != '\0';
      for (const char* c = num; *c; ++c) {
        if (*c < '0' || *c > '9') { ok = false; break; }
        v = v * 10u + static_cast<uint32_t>(*c - '0');
      }
      if (!ok) {
        snprintf(reply, sizeof(reply), "TIME;ERROR;Syntax");
      } else {
        syncEpoch(v);
        snprintf(reply, sizeof(reply), "TIME;%lu;%lu",
                 (unsigned long)nowEpoch(), (unsigned long)configEpoch_);
      }
    } else {
      snprintf(reply, sizeof(reply), "TIME;ERROR;Syntax");
    }
    lastWifiReply_ = reply;
    return true;
  }

  // FACTORY
  const char* arg = cmd + 7;  // "" | "?" | ";RESET"
  if (strncasecmp(arg, ";RESET", 6) == 0) {
    // 1) Clear the WHOLE namespace. `Preferences::clear()` removes every key in
    //    "botcfg": routers, credentials, last-good, toggles, mode/speed,
    //    fw_mark and the crash counters. Arduino flashing does NOT erase
    //    NVS, so this is the only way to get a virgin board without a re-flash.
    Preferences p;
    if (p.begin("botcfg", false)) {
      p.clear();
      p.end();
    }
    // 2) RAM back to a virgin board's defaults (the running session reports
    //    honestly instead of holding stale values until the next boot).
    hasStoredWifi_ = false;
    storedSsid_[0] = '\0';
    storedPass_[0] = '\0';
    lastGoodSsid_[0] = '\0';
    lastGoodPass_[0] = '\0';
    savedRouterCount_ = 0;
    for (int i = 0; i < MAX_SAVED_ROUTERS; ++i) {
      savedRouterSsid_[i][0] = '\0';
      savedRouterPass_[i][0] = '\0';
    }
    wifiEnabled_ = true;
    btEnabled_ = true;
    bootAutojoin_ = true;
    configEpoch_ = 0;
    crashLogResetCounters();
    Serial.println("[FACTORY] botcfg namespace cleared - fresh start");
    snprintf(reply, sizeof(reply), "FACTORY;CLEARED");
  } else {
    snprintf(reply, sizeof(reply), "FACTORY;ERROR;Syntax");
  }
  lastWifiReply_ = reply;
  return true;
}

void WebServerComm::syncEpoch(uint32_t epochSeconds) {
  epochBase_ = epochSeconds;
  epochSetMs_ = millis();
  epochSynced_ = (epochSeconds != 0);
}

uint32_t WebServerComm::nowEpoch() const {
  if (!epochSynced_ || epochBase_ == 0) return 0;
  return epochBase_ + static_cast<uint32_t>((millis() - epochSetMs_) / 1000ul);
}

void WebServerComm::stampConfigWrite(Preferences& p) {
  const uint32_t e = nowEpoch();
  if (e == 0) return;  // unsynced: never invent a time
  p.putUInt("config_epoch", e);
  configEpoch_ = e;
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
  // U-105 (1.3.5): the two transport toggles + the boot auto-join switch.
  // Absent on a virgin board -> ALL true: a first-flash car is open on both
  // transports at once (owner: *"for the first flash please allow both to
  // connect by opening both methods"*). The old parallel (allow_parallel) and
  // power-save (ap_psave) keys are gone; leftover values are wiped by
  // wipeRoutersOnFreshFirmware() (residue cleanup).
  wifiEnabled_ = prefs.getBool("t_wifi", true);
  btEnabled_ = prefs.getBool("t_bt", true);
  bootAutojoin_ = prefs.getBool("boot_autojoin", true);
  // Belt-and-braces clamp: the car can never run with BOTH transports off.
  // The command path refuses it too; this guards a corrupted/partial NVS.
  if (!wifiEnabled_ && !btEnabled_) wifiEnabled_ = true;
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

// U-98 (owner 2026-10-11): *"clear all the other saved router datas and only
// have its own data each time i flash the car so that i can always test from
// the start and the car is clean and fresh always."*
//
// Arduino flashing does NOT erase NVS, so a re-flash keeps every saved router,
// the stored credentials AND the last-good pair — and the next boot then
// silently re-joins an outside router the owner had deleted (the session's
// exact report: *"i deleted the saved router but the car restarted with the old
// router that i was previously connected"*). "First boot after a flash" is not
// a power cycle: it is the first boot where the RUNNING firmware differs from
// the one that last wrote the mark. `botcfg/fw_mark` stores the FW_VERSION; a
// mismatch (absent on a virgin board too) means a fresh install, so every
// router-related key is erased and the new mark written. Power cycles with the
// SAME firmware keep the saved routers (U-84: they must survive a power cycle)
// — only a firmware (re-)install resets them. Mode/speed prefs share the
// namespace but are deliberately NOT touched: this is the router surface only.
bool WebServerComm::wipeRoutersOnFreshFirmware() {
  Preferences prefs;
  prefs.begin("botcfg", true);
  const String mark = prefs.getString("fw_mark", "");
  prefs.end();
  if (mark.length() > 0 && mark == FW_VERSION) return false;  // same install

  Preferences w;
  w.begin("botcfg", false);
  static const char* sKeys[] = { "rnet0s", "rnet1s", "rnet2s", "rnet3s", "rnet4s", "rnet5s" };
  static const char* pKeys[] = { "rnet0p", "rnet1p", "rnet2p", "rnet3p", "rnet4p", "rnet5p" };
  for (int i = 0; i < MAX_SAVED_ROUTERS; ++i) {
    w.remove(sKeys[i]);
    w.remove(pKeys[i]);
  }
  w.remove("wifi_ssid");
  w.remove("wifi_pass");
  w.remove("last_ok_ssid");
  w.remove("last_ok_pass");
  w.remove("last_transport");
  // U-105: a fresh install returns transport config to its clean defaults
  // (Wi-Fi ON, Bluetooth ON, boot auto-join ON). The retired switches are
  // erased too: `ap_psave` / `allow_parallel` from U-100 (and any partial
  // write of the new keys), so no residue from an older firmware can survive.
  w.remove("t_wifi");
  w.remove("t_bt");
  w.remove("boot_autojoin");
  w.remove("ap_psave");
  w.remove("allow_parallel");
  w.putString("fw_mark", FW_VERSION);
  w.end();
  Serial.printf("[FIRST-BOOT] firmware %s is a fresh install - saved "
                "router/config NVS erased for a clean test state\n", FW_VERSION);
  return true;
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
  // U-77: close the READ-ONLY handle BEFORE any write below. NVS shares one
  // handle per namespace, and the first open's mode governs — writing through a
  // namespace that was opened read-only is silently dropped. (The old purge and
  // promotion blocks opened their own writable handle while this read-only one
  // was still live, so their NVS rewrites could not persist.)
  prefs.end();
  // U-92: a registry row named like the car's OWN hotspot is poison — it got
  // in through the unguarded WIFICFG/ADD paths (their Reserved checks compared
  // the whole unterminated field, so a password after the name bypassed them)
  // and the promotion below then copied the active pair in. The owner's
  // "double entry of esp's own hotspot". Purge at load and rewrite NVS so an
  // already-poisoned car heals itself without owner action.
  bool purged = false;
  {
    int w = 0;
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
    }
  }
  // Promote the stored active pair (wifi_ssid) up front so the connected
  // router always appears in the saved list even after a WIFICFG-only setup.
  // U-92: never promote the car's own hotspot — that is the other half of the
  // double-entry mechanism.
  bool promoted = false;
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
      promoted = true;
    }
  }
  // ONE writable pass, and only when the RAM registry actually changed (a
  // purge or a promotion). The read-only handle above is already closed, so
  // this open is read-write from the start and the rewrite persists.
  if (purged || promoted) {
    Preferences wprefs;
    if (wprefs.begin("botcfg", false)) {
      static const char* sKeys[] = { "rnet0s", "rnet1s", "rnet2s", "rnet3s", "rnet4s", "rnet5s" };
      static const char* pKeys[] = { "rnet0p", "rnet1p", "rnet2p", "rnet3p", "rnet4p", "rnet5p" };
      for (int i = 0; i < MAX_SAVED_ROUTERS; ++i) {
        if (i < savedRouterCount_) {
          wprefs.putString(sKeys[i], savedRouterSsid_[i]);
          wprefs.putString(pKeys[i], savedRouterPass_[i]);
        } else {
          wprefs.remove(sKeys[i]);
          wprefs.remove(pKeys[i]);
        }
      }
      wprefs.end();
    }
  }
  if (purged) {
    Serial.println(
        "[ROUTERS] removed the car's own hotspot from the saved list (U-92)");
  }
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
        stampConfigWrite(prefs);
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
      // T-66: "USE 4WD4MCar_WiFi" = switch back to the car's own network
      // (nothing stored → rejoin to the own AP as the default).
      hasStoredWifi_ = false;
      storedSsid_[0] = '\0';
      storedPass_[0] = '\0';
      // U-98: back on the own hotspot means the last-good router is no longer
      // the car's network; drop it so a later boot cannot hop back on its own.
      lastGoodSsid_[0] = '\0';
      lastGoodPass_[0] = '\0';
      Preferences prefs;
      prefs.begin("botcfg", false);
      prefs.remove("wifi_ssid");
      prefs.remove("wifi_pass");
      prefs.remove("last_ok_ssid");
      prefs.remove("last_ok_pass");
      stampConfigWrite(prefs);
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
        stampConfigWrite(prefs);
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
            stampConfigWrite(prefs);
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
            // the pair is simply stored — the next rejoinNetwork() uses it.
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
        // U-105: match case-insensitively. The old strcmp let a delete whose
        // casing differed from the stored row fall through to "Not saved", so
        // the router stayed in the registry AND (when it was the active pair)
        // in storedSsid_ — the exact "deleted network survives a power cycle"
        // report. Router names are case-insensitive on the wire.
        if (strcasecmp(savedRouterSsid_[i], ssid) == 0) {
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
      stampConfigWrite(prefs);
      prefs.end();
      if (!removed) {
        snprintf(reply, sizeof(reply), "ROUTERS;ERROR;Not saved:%s", ssid);
      } else {
        // If the deleted router was the ACTIVE pair, fall back to the car's
        // own WiFi (default). T-62: rejoin immediately so the car doesn't hang
        // on the removed network until the STA timeout.
        bool wasActive = (hasStoredWifi_ && strcasecmp(storedSsid_, ssid) == 0);
        if (wasActive) {
          hasStoredWifi_ = false;
          storedSsid_[0] = '\0';
          storedPass_[0] = '\0';
          // U-98: deleting the ACTIVE router must also erase the last-good
          // pair. This is the owner's exact report — DEL cleared `wifi_ssid`,
          // but `last_ok_ssid` survived, so the next boot re-joined the router
          // that had just been deleted.
          lastGoodSsid_[0] = '\0';
          lastGoodPass_[0] = '\0';
          Preferences wprefs;
          wprefs.begin("botcfg", false);
          wprefs.remove("last_ok_ssid");
          wprefs.remove("last_ok_pass");
          wprefs.end();
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
    // network (4WD4MCar_WiFi) is virtual/protected — it is never removed.
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
    // U-98: clear the last-good pair too, so a wiped registry cannot be
    // re-joined from a stale boot target.
    prefs.remove("last_ok_ssid");
    prefs.remove("last_ok_pass");
    stampConfigWrite(prefs);
    prefs.end();
    hasStoredWifi_ = false;
    storedSsid_[0] = '\0';
    storedPass_[0] = '\0';
    lastGoodSsid_[0] = '\0';
    lastGoodPass_[0] = '\0';
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
    //
    // U-95 Phase 2 (honest scan): the answer must be TRUE or BUSY. startScan()
    // used to refuse silently when a scan was already armed/in flight (or the
    // STA was associating) while this handler answered STARTED
    // unconditionally — so a double-tap's second request reported success for
    // a scan that never existed, and the app waited for a result that was
    // never coming. A check that did not run must not read as one that
    // passed (F-82/F-83). Gate rule: `scan-honest`.
    if (startScan()) {
      snprintf(reply, sizeof(reply), "ROUTERS;SCAN;STARTED");
    } else {
      snprintf(reply, sizeof(reply), "ROUTERS;SCAN;BUSY");
    }
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
//                       pass and remembers when it started; REFUSES (false)
//                       when a scan is already in flight or the STA is
//                       mid-join, so the caller can answer BUSY honestly;
//   runScanIfDue()      is that kick, called from handle();
//   collectScanIfDone() reads the result once the radio reports it finished,
//                       dedupes per BSSID (one row per RADIO — a multi-AP
//                       router legitimately shows each AP once, while the same
//                       radio never repeats), sorts strongest-first, and only
//                       then caps at SCAN_MAX_RESULTS. The cap used to apply
//                       at INSERT time, so the list kept the FIRST N in
//                       radio-report order — whichever the scan saw first,
//                       not the strongest — and dropped the rest.
//
// The result is delivered two ways, so both link types and both app parsers
// work without either being special:
//   - as its own SPP line   `SCAN;<ssid>,<rssi>,<open>;…`  (or `SCAN;NONE`)
//   - in the status JSON    `"scan":[{"ssid":…,"rssi":…,"open":…,
//     "channel":…,"own":…,"saved":…,"current":…}]`  (the last four are
//     U-95 Phase 3: per-row car truth for the page's network table)
// Names only — a scan can never reveal a password (W-14). The BSSID is the
// dedupe key only; it is deliberately NOT in either wire form (no consumer
// needs the radio's MAC).
//
// While the STA is ASSOCIATED, ESP32 scan results are unreliable (the radio is
// busy and the channel follows the router), so strength is reported only as a
// hint. The saved-router list remains the source of truth for what the car
// will actually join. U-95 Phase 2: while the STA is ASSOCIATING (between
// WiFi.begin() and association) a scan is not even STARTED — startScan()
// refuses and the handler answers BUSY — because kicking a scan into a
// driver that is mid-join races the join itself and returns garbage; the
// hint above covers the settled case, BUSY covers the transition.

// U-95 Phase 2: startScan() now ANSWERS. The bool lets the handler report
// `ROUTERS;SCAN;BUSY` instead of claiming STARTED for a scan it silently
// dropped. Gate rule: `scan-honest`.
bool WebServerComm::startScan() {
  if (scanPending_ || scanBusy_) return false;  // armed or in flight already
  if (connectingPending_) return false;         // associating - do not race the join
  // U-112: the WS coexistence scan is the largest transient allocation of the
  // system (driver buffers + the vector reallocation in collectScanIfDone()).
  // On a heap at the field floor (13-18 kB measured, boots 1026-1029) the kick
  // can fail to allocate and the scan lands 8 s later on a SILENTLY EMPTY list
  // (collectScanIfDone's timeout clears busy - it recovers, but the owner sees
  // "no networks found" with no reason, which reads as the list being broken).
  // Refuse UP FRONT with an honest false: the handler already answers
  // ROUTERS;SCAN;BUSY, which the app renders as busy and can retry. Counted +
  // logged so the serial capture shows WHY, not just that it did not scan.
  if (ESP.getFreeHeap() < SCAN_HEAP_FLOOR_BYTES) {
    scanFloorRefusals_++;
    FW_LOG("[SCAN] refused: low heap (free=%u, floor=%u, refusals=%u)\n",
           static_cast<unsigned>(ESP.getFreeHeap()),
           static_cast<unsigned>(SCAN_HEAP_FLOOR_BYTES),
           static_cast<unsigned>(scanFloorRefusals_));
    return false;
  }
  scanPending_ = true;
  scanStartMs_ = 0;  // armed; handle() kicks it on a later pass
  return true;
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

  // U-95 Phase 2 (scan quality): collect EVERY result, dedupe per BSSID,
  // sort strongest-first, and only then cap at SCAN_MAX_RESULTS.
  //
  // The old shape capped TWICE — before the loop (`if (n > SCAN_MAX_RESULTS)`)
  // and at insert (`scanNetworks_.size() < SCAN_MAX_RESULTS`) — and never
  // sorted, so the list kept whichever N results the radio reported first,
  // dropping the strong networks in favour of radio-order luck. It also
  // deduped on NAME, which silently merged two DIFFERENT routers broadcasting
  // the same SSID (the owner's SSIDs are often shared across homes) and hid
  // a multi-AP router's second radio.
  //
  // The BSSID is the honest dedupe key: one row per RADIO. Same radio seen
  // twice across channels → merged (strongest wins); different radios, same
  // name → BOTH shown, strongest-first; that is the truth of the air. The
  // sort is hand-rolled on purpose — no <algorithm> on the Arduino TU (CI
  // proved what an extra include costs) — insertion sort over at most a few
  // dozen entries is noise next to the scan itself.
  scanNetworks_.clear();
  for (int i = 0; i < scanCount_; ++i) {
    const char* name = WiFi.SSID(i).c_str();
    if (!name || !name[0]) continue;
    const int32_t rssi = WiFi.RSSI(i);
    const bool open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    const String bssid = WiFi.BSSIDstr(i);
    bool merged = false;
    for (size_t k = 0; k < scanNetworks_.size(); ++k) {
      if (strcasecmp(scanNetworks_[k].bssid, bssid.c_str()) == 0) {
        if (rssi > scanNetworks_[k].rssi) {
          scanNetworks_[k].rssi = rssi;
          scanNetworks_[k].open = open;
        }
        merged = true;
        break;
      }
    }
    if (!merged) {
      ScannedNetwork entry;
      snprintf(entry.ssid, sizeof(entry.ssid), "%s", name);
      snprintf(entry.bssid, sizeof(entry.bssid), "%s", bssid.c_str());
      entry.rssi = rssi;
      entry.channel = static_cast<uint8_t>(WiFi.channel(i));
      entry.open = open;
      scanNetworks_.push_back(entry);
    }
  }

  // Strongest-first: insertion sort, descending RSSI. Runs BEFORE the cap so
  // the rows that survive are the strongest, not the earliest.
  for (size_t a = 1; a < scanNetworks_.size(); ++a) {
    ScannedNetwork key = scanNetworks_[a];
    size_t b = a;
    while (b > 0 && scanNetworks_[b - 1].rssi < key.rssi) {
      scanNetworks_[b] = scanNetworks_[b - 1];
      --b;
    }
    scanNetworks_[b] = key;
  }

  // The ONE cap, last: the wire forms are buffer-bounded downstream, but an
  // unbounded vector would still bloat the status broadcast (and the app's
  // table) with noise rows nobody asked for.
  if (scanNetworks_.size() > SCAN_MAX_RESULTS) {
    scanNetworks_.resize(SCAN_MAX_RESULTS);
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
  // U-95 Phase 3: per-row car truth for the page's network table (and the app):
  //   channel - the radio channel the result arrived on (captured at collect
  //             time, while the driver still held it);
  //   own     - this row NAMES the car's own hotspot (first-class, selectable
  //             — the owner's ruling; the page labels it);
  //   saved   - the car has this SSID in its saved-router list;
  //   current - the STA is ASSOCIATED to this row right now.
  // own/saved/current are computed HERE and shipped as facts: three app
  // modules already disagree about the own-network rule (U-95 Phase 4 will
  // unify them), and the car's answer is the one every consumer converges on.
  const bool staNow = (WiFi.status() == WL_CONNECTED);
  const String curSsid = staNow ? WiFi.SSID() : String("");
  const char* ownName = apName_.length() > 0 ? apName_.c_str() : WIFI_AP_NAME;
  size_t used = 0;
  int n = snprintf(buf, bufLen, "[");
  if (n > 0) used = static_cast<size_t>(n);
  for (size_t i = 0; i < scanNetworks_.size() && used < bufLen; ++i) {
    const char* rowSsid = scanNetworks_[i].ssid;
    bool saved = false;
    if (strcasecmp(rowSsid, WIFI_AP_NAME) == 0) saved = true;
    for (int s = 0; !saved && s < savedRouterCount_; ++s) {
      if (strcasecmp(rowSsid, savedRouterSsid_[s]) == 0) saved = true;
    }
    const bool own = (strcasecmp(rowSsid, ownName) == 0);
    const bool current = staNow && curSsid.equalsIgnoreCase(rowSsid);
    n = snprintf(buf + used, bufLen - used,
                 "%s{\"ssid\":\"%s\",\"rssi\":%d,\"open\":%s,"
                 "\"channel\":%u,\"own\":%s,\"saved\":%s,\"current\":%s}",
                 i ? "," : "", rowSsid,
                 static_cast<int>(scanNetworks_[i].rssi),
                 scanNetworks_[i].open ? "true" : "false",
                 static_cast<unsigned>(scanNetworks_[i].channel),
                 own ? "true" : "false",
                 saved ? "true" : "false",
                 current ? "true" : "false");
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
// network (4WD4MCar_WiFi) leads the list first while in webserver mode.
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
  crashRtcMark("wifi-ap");
  if (apRunning_) return;
  // U-109: state the intended broadcast id BEFORE any early return. The name is
  // a constant, and buildStatusJson() reads apName_ for the `ap` field with no
  // apRunning_ guard — the app's discovery sweep identifies the car by that name
  // before it has ever paired. An early return here (the staBusy() guard below,
  // or a softAP() that has not retried yet) used to leave apName_ empty, so
  // `ap` came back "" and the car was unfindable. apRunning_ still carries
  // whether the beacon is actually up; the name is identity, not state.
  if (apName_.length() == 0) apName_ = String(WIFI_AP_NAME);
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
  // join `4WD4MCar_WiFi`, which surfaces as Android's intermittent "unable
  // to connect" from the phone's Wi-Fi search/connect menus (owner report).
  // setSleep(false) removes those gaps; explicit open-AP params (no password,
  // channel 1 follows the STA router in AP_STA, visible, up to 4 clients) keep
  // the broadcast rock-solid. Companion phone step (one time): forget the
  // network and re-add — Android can cache a stale profile against an open AP.
  // FIN-40/D1 (2026-09-18 device gate, T-71a FAIL): softAP() can fail while the
  // radio is busy (mode entry / BT stack / scan). The old code logged the
  // warning but STILL set apRunning_ = true, so startOwnAP() never retried and
  // the car could sit with NO broadcast. Now: only a real success marks the AP
  // running; a failure leaves apRunning_ false and handle() retries every
  // AP_RETRY_INTERVAL_MS until it sticks.
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
  // U-96 / U-111: the bring-up uses minimum AP power (WIFI_POWER_11dBm) to keep
  // the init current spike small — the car's own hotspot only needs to reach a
  // phone across a room and 11dBm (~12mW) draws noticeably less than the default
  // 20dBm during the vulnerable boot window. It is a BOOT-WINDOW cap, not a
  // permanent one: handle() restores a normal level once WIFI_BOOT_SETTLE_MS has
  // passed, because a permanent ~8.5 dB handicap on top of BT coexistence
  // time-slicing the antenna makes the association marginal (owner 2026-10-14:
  // "the car's own hotspot won't connect").
  WiFi.setTxPower(WIFI_POWER_11dBm);
  apTxPowerLow_ = true;  // handle() raises this once the boot window has passed
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
  // U-95 Phase 2 (NVS wear): the Arduino-ESP32 default is persistent(true),
  // which routes every esp_wifi_set_config through FLASH — so each
  // WiFi.begin() rewrites the station credentials and each softAP() rewrites
  // the AP config. A single router switch runs begin() up to STA_MAX_RETRIES
  // times, the boot auto-join adds more, and none of it is data this
  // firmware ever reads back: its credentials live in its OWN `botcfg`
  // Preferences (storedSsid_/storedPass_, T-33), and the Arduino layer's
  // shadow copy is pure write amplification against ~100k erase cycles.
  // Set ONCE, before the first radio call of the session (beginStation below
  // is the bring-up), and flash stops being written on every WiFi decision.
  // Gate rule: `wifi-persistent`.
  WiFi.persistent(false);
  
  // U-99 (owner 2026-10-12): set the FINAL mode BEFORE the AP is brought up, so
  // the own hotspot is configured exactly ONCE — in AP_STA — with its real name
  // and IP.
  //
  // This used to be beginStation() in WIFI_AP followed by WiFi.mode(AP_STA).
  // On this core esp_wifi_set_mode() stops and restarts the radio, so the second
  // set re-created the just-started AP from the SDK-default config: the panel
  // then held the real hotspot (4WDCar_Wifi · 192.168.245.1) AND a nameless
  // default one (· 192.168.4.1) — the owner's "duplicate hotspot, two IPs during
  // first start". One mode set, one softAP, one identity.
  WiFi.mode(WIFI_AP_STA);  // resting mode: AP now, STA joins without a mode dance
  beginStation();  // startOwnAP() + server.begin() + ws.begin() (mode already AP_STA)
  updateExtraInfo();

  // U-109: DO NOT force apRunning_ = true here. beginStation() -> startOwnAP()
  // has already claimed exactly what the radio achieved; forcing the claim true
  // regardless is what defeated the FIN-40/D1 retry in handle() (which keys on
  // `!apRunning_`). The boot softAP was in fact being skipped by a staBusy()
  // misread — `WiFi.status()` is WL_STOPPED (254) for the first moments after
  // mode(WIFI_AP_STA) until the arduino_events task delivers STA_START (fixed at
  // the allow-list in staBusy()) — and forcing the claim true then marked that
  // skipped bring-up as done, so the retry never ran and the car sat with NO own
  // hotspot for the WHOLE uptime: no phone could join 4WD4MCar_WiFi, and /status
  // `ap` stayed empty so the app's discovery sweep could not find it either —
  // "no connection on any method". With the claim left honest, handle() retries
  // the bring-up and the hotspot comes up by itself. `startedAsAP` is owned by
  // beginStation().

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
  // U-99: the caller (beginAlwaysOn) sets WIFI_AP_STA before this runs. Setting
  // the mode HERE would be a SECOND radio restart AFTER startOwnAP() — the move
  // that re-created the AP with the SDK-default name / 192.168.4.1 and produced
  // the duplicate-hotspot OLED. The AP is brought up once, in the caller's
  // (AP_STA) mode, on channel 1 while nothing is joined (startOwnAP's staSharing
  // rule), so R-16's fixed-channel-1 own AP is unchanged.
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
  // U-95 Phase 2: a deferred retry begin must not survive a power-policy
  // suspend/resume — the resume path's join decision belongs to the .ino
  // (U-79), not to a retry armed before the suspend.
  retryBeginPending_ = false;

  server.begin();
  serverRunning = true;
  ws.begin();
  FW_LOG_VERBOSE("DBG: WebSocket server begun\n");

  updateExtraInfo();
  return serverRunning;
}

void WebServerComm::rejoinNetwork() {
  crashRtcMark("wifi-connect");
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
  // U-95 Phase 2: an explicit switch CANCELS an in-flight retry begin (the
  // U-75 give-up pattern: a stale step must never land after the user asked
  // for something else) and re-STAMPS the backoff. Without the re-stamp the
  // retry branch fires on pass 1 of the step machine — its 10 s backoff is
  // stale after any successful session — and disconnects the very teardown
  // that is mid-walk. The T-34 comment that used to justify prompt retries
  // here described a rejoinNetwork() that still did its own disconnect;
  // U-71 removed that, but the stale stamp stayed.
  retryBeginPending_ = false;
  lastWifiRetryMs_ = millis();
  startedAsAP = true;
  rejoinStep_ = REJOIN_STEP_MODE;
  // U-95 Phase 2: a fresh request gets a fresh drop attempt budget, or the
  // second switch of a session would inherit the first one's spent retries.
  modeDropAttempts_ = 0;
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
// U-105 (owner 2026-10-13): the own-hotspot beacon is NEVER parked.
//
// The old power-save switch (U-86/U-100 `ap_psave` + `WIFICFG;APSAVE`) and the
// parallel-links switch (U-100 Phase 4 `allow_parallel`) are REMOVED per the
// owner (*"remove the parallel thing from the setting entirely and power saving
// buttons and their residues"*). That leaves exactly one job for this function:
// GUARANTEE the beacon. While the Wi-Fi transport is enabled the own hotspot is
// always a way in — first flash, on a router, mid-transition. The only way the
// hotspot is down is `WIFICFG;WIFI;OFF`, which stops the whole web layer and is
// enforced by the U-79 loop in the .ino (this function deliberately no-ops then,
// or it would fight the user's own switch).
void WebServerComm::updateApPowerPolicy() {
  if (!wifiEnabled_) return;  // Wi-Fi transport deliberately off (U-105)
  const bool staUp = (WiFi.status() == WL_CONNECTED);

  // U-87 history (kept as the why): a phone driving the car from the own
  // hotspot raised ws.connectedClients(), and the first policy counted that as
  // a reason to PARK the beacon it was being driven over - ~20 s after the page
  // opened the car dropped the very link running it. Parking is gone now, but
  // the lesson remains: the beacon is the setup path and must never be a cost
  // of being reachable.

  if (!staUp) {
    // Router gone or never joined: the beacon is the fallback. Do NOT bring it
    // up from here (U-111). This probe runs EVERY loop pass, so an ungated
    // startOwnAP() here turns a softAP() that keeps failing into a per-pass
    // radio churn (setSleep / setTxPower / softAP) that hammers the driver and
    // starves loop() — the exact shape of the owner's "the car side freezes
    // totally" report. handle() owns that retry with an AP_RETRY_INTERVAL_MS
    // backoff and a staBusy() guard; nothing is lost by leaving it there.
    staConnectedAtMs_ = 0;
    return;
  }

  // STA is up and the Wi-Fi transport is enabled: the beacon stays broadcast
  // (no power-save park exists in 1.3.5). Track the association for /status.
  if (staConnectedAtMs_ == 0) staConnectedAtMs_ = millis();
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
      // U-105: no parked flag to clear — parking was removed with the
      // power-save switch; the beacon is simply back up.
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
    // U-109: WL_STOPPED (254) is the core's INITIAL value for STAClass::_status
    // (STA.cpp: `_status(WL_STOPPED)`) and it is what `WiFi.status()` still
    // reports in the first moments after `mode(WIFI_AP_STA)` returns — the
    // arduino_events task has not yet delivered WIFI_EVENT_STA_START, which is
    // what flips it to WL_DISCONNECTED. It means the STA interface is stopped,
    // so it is settled by definition. Omitting it made the very first
    // `startOwnAP()` at boot read 254, fall through to `default`, and skip the
    // softAP ("[RADIO] skip-softAP/sta-busy"). The forced `apRunning_ = true`
    // that used to follow marked the skipped bring-up as done, so no retry ever
    // ran and the car came up with NO own hotspot — the field "no connection on
    // any method".
    case WL_STOPPED:
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
      // U-95 Phase 2 (2026-10-07, plan: "make the teardown real"): U-90 made
      // this step unconditional, and the claim written here at the time —
      // "WiFi.mode(WIFI_AP_STA) restarts the WiFi driver, the stale STA state
      // is torn down" — was verified FALSE against the installed core:
      // WiFiGeneric.cpp:602+ returns early when the requested mode EQUALS the
      // current one, and after the FIRST switch of the car's life the driver
      // is already WIFI_AP_STA (beginAlwaysOn() leaves it there). So every
      // later switch did no teardown at all — the stale association survived,
      // the U-76 resolve defect ("stuck on one router") had a second life,
      // and BEGIN raced a driver that was still busy (the serial evidence
      // `E wifi:sta is connecting, cannot set config`).
      //
      // The teardown must therefore DROP the mode, not re-request it:
      //   MODE (WIFI_AP — always unequal, so the call always does real work)
      //   -> MODE_WAIT (poll getMode(), 0 radio calls, bounded)
      //   -> AP (startOwnAP() while the STA is down — the one window
      //      startOwnAP()'s staBusy() guard can never refuse, so the phone's
      //      way in exists for the WHOLE switch: the plan's "reachable
      //      throughout" requirement)
      //   -> RAISE (WIFI_AP_STA — also always unequal, always works)
      //   -> BEGIN (a clean slate: no association left to race).
      // One radio call per pass (U-71) untouched; the watchdog rule (F-24)
      // untouched. Gate rule: `switch-teardown`.
      noteRadioStep("rejoin/mode-AP");
      WiFi.mode(WIFI_AP);
      modeWaitStartMs_ = millis();
      modeDropAttempts_ = 1;
      rejoinStep_ = REJOIN_STEP_MODE_WAIT;
      return;

    case REJOIN_STEP_MODE_WAIT:
      // The drop is asynchronous: mode() returns once the set is ACCEPTED,
      // not once the core's cached mode agrees. Poll it (no radio work).
      if (WiFi.getMode() == WIFI_AP) {
        rejoinStep_ = REJOIN_STEP_AP;
        return;
      }
      if (millis() - modeWaitStartMs_ <= REJOIN_MODE_SETTLE_MS) return;  // still settling
      // Not taken: re-issue the drop, bounded — a core that never confirms
      // gets breadcrumbs and then PROCEEDS (give-up path's rule: never wedge
      // a state machine, degrade honestly and say so).
      if (modeDropAttempts_ < REJOIN_MODE_DROP_ATTEMPTS) {
        ++modeDropAttempts_;
        noteRadioStep("rejoin/mode-AP-retry");
        WiFi.mode(WIFI_AP);
        modeWaitStartMs_ = millis();
        return;
      }
      noteRadioStep("rejoin/mode-AP-not-taken");
      rejoinStep_ = REJOIN_STEP_AP;
      return;

    case REJOIN_STEP_AP:
      // T-65: the own hotspot is never torn down during a network change, so
      // the phone keeps a way in for the whole switch.
      //
      // U-105: that guarantee is now UNCONDITIONAL — parking was removed with
      // the power-save switch, so the beacon stays up before, during and after
      // every network change. (The old U-86 scoped guarantee dropped it once a
      // new router held for AP_PARK_GRACE_MS; that park path no longer exists.)
      //
      // U-95 Phase 2: this step now runs with the STA DOWN (the MODE drop
      // above), so staBusy() — which refused this very call while the old
      // association was settling (FIN-40/D1 "skip-softAP/sta-busy") — cannot
      // refuse it: guaranteed legal window. The claim is forced false HERE,
      // right before the restore, mirroring GIVEUP_AP: the AP_STA->AP switch
      // can bounce the beacon (FIN-40/D1), so never trust apRunning_ across
      // it. The RAISE step below does NOT touch the flag: AP->AP_STA only
      // adds a mode bit (esp_wifi_set_mode, no stop), proven by
      // beginAlwaysOn(), which raises WIFI_AP->WIFI_AP_STA at boot without
      // touching apRunning_ — the path the owner uses every day.
      noteRadioStep("rejoin/softAP");
      apRunning_ = false;  // forced, not trusted (FIN-40/D1)
      startOwnAP();
      rejoinStep_ = REJOIN_STEP_RAISE;
      return;

    case REJOIN_STEP_RAISE:
      // The STA side returns on a clean slate: the driver stopped the old
      // association at the MODE drop, so there is no stale link for
      // REJOIN_STEP_BEGIN to resolve against (the U-90/U-95 defect) and no
      // busy STA to refuse the begin (`sta is connecting, cannot set config`
      // can no longer occur). Unequal mode again — this call also always
      // does real work.
      noteRadioStep("rejoin/mode-AP_STA");
      WiFi.mode(WIFI_AP_STA);
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
        // cleared), so the car ANSWERED `ROUTERS;USED;4WD4MCar_WiFi` and then
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
// They are written only from the connect-success path. U-98 (2026-10-11): they
// are now INFORMATIONAL — the boot auto-join reads `storedSsid_` (the
// last-SWITCHED router), NOT this pair, because a router the owner deleted
// stayed here and a boot re-joined it. The pair is still maintained (and is
// erased alongside the stored pair on DEL/CLEAR/RESET) so "what we tried" and
// "what worked" remain separate facts.
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
  // U-98 (2026-10-11, owner: *"the car should always go to the last switched
  // router network always; if it is not available it should show its own router
  // for the user to connect or control through the car's own webpage"*).
  //
  // The target is the last-SWITCHED router: `storedSsid_`, written by every
  // ROUTERS;USE / WIFICFG. It is deliberately NOT `lastGoodSsid_`
  // (`last_ok_ssid`) any more: a router the owner DELETED stayed in
  // `last_ok_ssid` (DEL only cleared `wifi_ssid`), so the old boot silently
  // re-joined the very network the owner just removed — the session's report
  // (*"i deleted the saved router but the car restarted with the old router
  // that i was previously connected"*). `storedSsid_` cannot resurrect a
  // deleted network: DEL/CLEAR/WIFICFG;RESET all wipe it, the last-good pair is
  // wiped with it (U-98), and a fresh flash starts empty.
  //
  // The budget is now the FULL explicit-switch budget (WIFI_STA_TIMEOUT_MS /
  // STA_MAX_RETRIES), not the old SHORT boot window (BOOT_STA_*, removed). That
  // reversal of R-16/T-64 is safe because the own hotspot beacon stays UP for
  // the whole retry — parking only ever runs while the car is CONNECTED and in
  // use — so a long attempt can never strand the owner; and an unreachable
  // router still ends in the give-up path, which restores the own hotspot the
  // moment the budget is spent.
  if (!bootAutojoin_) {
    FW_LOG_VERBOSE("[WIFICFG] boot auto-join disabled - staying on own hotspot\n");
    return;
  }
  if (!storedSsid_[0]) {
    FW_LOG_VERBOSE("[WIFICFG] boot: no last-switched router, staying on own hotspot\n");
    return;
  }
  hasStoredWifi_ = true;
  // Already on the router it would join (a power-policy resume that never tore
  // the association down): do not tear it down just to re-join it.
  if (WiFi.status() == WL_CONNECTED &&
      strcmp(WiFi.SSID().c_str(), storedSsid_) == 0) {
    FW_LOG_VERBOSE("[WIFICFG] boot: already on \"%s\", no rejoin needed\n",
                   storedSsid_);
    return;
  }
  FW_LOG("[WIFICFG] boot: returning to last-switched router \"%s\" (full budget)\n",
         storedSsid_);
  // rejoinNetwork() arms the full budget and does no radio work itself; handle()
  // walks the drop -> restore -> begin step machine, one radio call per pass
  // (U-71/U-95 Phase 2).
  rejoinNetwork();
}

void WebServerComm::stop() {
  crashRtcMark("wifi-stop");
  FW_LOG_VERBOSE("DBG: stop() called, serverRunning=%d\n", serverRunning ? 1 : 0);

  // U-95 Phase 2: shutdown must not leave a deferred begin armed for a
  // later start() to fire.
  retryBeginPending_ = false;

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

  // Protect state variables with critical section
  TRANSPORT_POWER_ENTER_CRITICAL();
  connectingPending_ = false;
  apRunning_ = false;
  apTxPowerLow_ = false;  // U-111: a later startOwnAP() re-arms it
  staConnectedAtMs_ = 0;
  startedAsAP = false;
  queueClear();
  TRANSPORT_POWER_EXIT_CRITICAL();

  FW_LOG_VERBOSE("Free heap after stop: %d\n", static_cast<int>(ESP.getFreeHeap()));
}

bool WebServerComm::isRunning() const { return serverRunning; }

// v1.0.1 (owner OLED directive): differentiated link truth for the OLED.
bool WebServerComm::hasWsClients() { return ws.connectedClients() > 0; }

// U-112: is the status broadcast currently below the heap floor? The one
// place the threshold lives; broadcastStatus() applies the time-box policy.
bool WebServerComm::broadcastDeferred() {
  return ESP.getFreeHeap() < STATUS_HEAP_FLOOR_BYTES;
}

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
  crashRtcMark("web");
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

  crashRtcMark("wifi-rejoin");
  advanceRejoinStep();

  // D3 (U-68): drive the car-side scan from the loop, never from the command
  // path. runScanIfDue kicks a non-blocking pass once the settle delay has
  // passed; collectScanIfDone reads it when the radio finishes and broadcasts.
  crashRtcMark("wifi-scan");
  runScanIfDue();
  collectScanIfDone();

  // U-95 Phase 2 (retry split): the begin deferred by the retry branch below
  // lands here, on its OWN pass — one radio call per pass (U-71). The guard
  // takes precedence over the resolve/retry chain for exactly one pass, then
  // clears itself, so the fresh connect window is picked up by the normal
  // resolve block on the next pass. One flag, one consumer, no interleaving:
  // the arm and the begin can never be swept away by the give-up branch (it
  // sits behind this chain) or re-entered by a second retry tick.
  if (retryBeginPending_) {
    retryBeginPending_ = false;
    connectingPending_ = true;
    connectStartMs_ = millis();
    noteRadioStep("retry/begin");
    WiFi.begin(storedSsid_, storedPass_);
  } else if (connectingPending_) {
    // F-20a: resolve the pending STA connect without blocking the loop.
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
        // association down every WIFI_RETRY_INTERVAL_MS. The flags are armed
        // when the begin actually RUNS (the deferred `retryBeginPending_`
        // block above), which hands the outcome back to the resolve block,
        // which clears both flags on success and re-arms the budget on the
        // next timeout. WIFI_STA_TIMEOUT_MS is therefore the real per-attempt
        // spacing; WIFI_RETRY_INTERVAL_MS is only the floor.
        // U-77: this branch had the only silent radio calls left — a hang here
        // produced a WDT reboot with no breadcrumb (U-75 rule: name the step
        // BEFORE it runs). U-95 Phase 2: this pass DISCONNECTS and arms the
        // begin; the begin lands on the NEXT pass. Two radio calls in one
        // pass is the U-75 shape that rebooted the car on the give-up path,
        // and begin-immediately-after-disconnect races a driver that is still
        // tearing the old association down (the plan: separate the
        // disconnect/begin across passes).
        noteRadioStep("retry/disconnect");
        WiFi.disconnect();
        retryBeginPending_ = true;
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
  // real success, so the car always ends up broadcasting its own hotspot.
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

  // U-111: the bring-up capped the radio at 11 dBm for the boot window (U-96).
  // Restore a normal level ONCE the settle window has passed, so the beacon
  // reaches the phone the way any other 2.4 GHz AP does — with BT coexistence
  // also time-slicing the antenna, a permanent ~8.5 dB handicap made the
  // association marginal. `apTxPowerLow_` is re-armed by every startOwnAP(), so
  // a bring-up that only succeeds late still ends at full power.
  if (apTxPowerLow_ && millis() > WIFI_BOOT_SETTLE_MS) {
    apTxPowerLow_ = false;
    WiFi.setTxPower(WIFI_POWER_17dBm);
  }

  // U-98 Phase 2 (OLED "pronto"): the OLED's network/extra row refreshes on its
  // OWN tick, decoupled from the WebSocket broadcast. They used to share the one
  // 1 s tick here, so the only way to make the OLED pronto was to double the
  // status JSON to every client too - the wrong trade, and it still left the row
  // up to a full second stale after a network changed. Now the extra row polls
  // twice a second while the broadcast stays at 1 Hz. Gate rule:
  // `extra-status-decoupled`.
  static unsigned long lastExtraMs = 0;
  const unsigned long EXTRA_INTERVAL = 500;
  unsigned long now = millis();
  if (now - lastExtraMs >= EXTRA_INTERVAL) {
    lastExtraMs = now;
    updateExtraInfo();
  }

  static unsigned long lastStatusMs = 0;
  const unsigned long STATUS_INTERVAL = 1000;
  if (now - lastStatusMs >= STATUS_INTERVAL) {
    lastStatusMs = now;
    // U-112: the heap floor lives INSIDE broadcastStatus() (time-boxed defer),
    // so this tick and the REQ_STATE/REQ_NETW paths share ONE policy.
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
  // U-104 (2026-10-13): sendStateOn() delivers STATE/CAPS/NETW/SCAN through
  // THIS function — up to four ws.broadcastTXT() calls per transport per 1.5 s
  // tick — and it was the one WS-write path with NO watchdog feed of its own
  // (broadcastStatus feeds before its build+write, and handle() feeds before
  // ws.loop(), but neither covers a write that starts HERE). To a client that
  // stopped draining TCP this call can block for up to 5 s with each inner
  // tcp->write() waiting up to 3 s (the U-77 measurement). Fed here, the write
  // gets its own fresh LOOP_TASK_WDT_TIMEOUT_MS window; the heartbeat (U-77)
  // still owns the long-lived staller. Gate rule: `ws-broadcast-feed`.
  esp_task_wdt_reset();
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
// The hostname is deliberately NOT the AP name: `4WD4MCar_WiFi` contains an
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
    // instead of the generic "Connected" (owner report 2026-09-15). The same
    // ipStr becomes the OLED's "IP : <addr>" line (v1.2.4, owner directive
    // 2026-10-09 — the OLED shows the car's CURRENT IP, like the app). The
    // directed broadcast stays on /status broadcast_ip + the page's Bcast row.
    snprintf(extra, sizeof(extra), "%d,%ddBm,%s,%s", pct, rssi, ipStr,
             WiFi.SSID().c_str());
  } else if (apRunning_) {
    // Own-network session: show the broadcast ID + the softAP IP on the
    // OLED so the user knows which network to join (the IP contains a dot,
    // which keeps isConnectedNow() true for the SERVER branch).
    // 2026-10-10: this row no longer lists saved routers - that list fed only
    // the removed "[n/m]" badge, and the routers are shown in full on the page
    // and in the app.
    char ipStr[32];
    WiFi.softAPIP().toString().toCharArray(ipStr, sizeof(ipStr));
    // 2026-10-10: the "|Nets:<saved routers>" tail is gone with the OLED's
    // "[n/m]" badge. It had exactly one reader - the cycling index - and it was
    // a truncation hazard on the line the owner actually asked for: this buffer
    // is 128 B and three long router names pushed the IP out of the snprintf.
    // extraInfo is OLED-internal (no app or page parses it), so the shorter
    // form is invisible everywhere except that it now always fits.
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
  crashRtcMark("ws-evt");
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
  // U-112: THE heap floor for the status path, in ONE place so every caller
  // (the 1 s tick, REQ_STATE/REQ_NETW, the scan-complete push) obeys the same
  // rule. Deferred, never silently dropped: while below the floor the defer is
  // TIME-BOXED (STATUS_HEAP_DEFER_MAX_MS) - after the window one attempt goes
  // through regardless, so telemetry degrades to one attempt / 4 s under
  // chronic pressure instead of black-holing, and returns to 1 Hz the pass the
  // heap is back over the floor. Each defer edge is logged once, so the serial
  // capture shows the defer instead of a dashboard that quietly went quiet.
  if (broadcastDeferred()) {
    if (statusDeferSinceMs_ == 0) statusDeferSinceMs_ = millis();
    if (millis() - statusDeferSinceMs_ < STATUS_HEAP_DEFER_MAX_MS) {
      if (!statusDeferStreak_) {
        statusDeferStreak_ = true;
        FW_LOG("[HEAP] status broadcast deferred (free=%u, floor=%u)\n",
               static_cast<unsigned>(ESP.getFreeHeap()),
               static_cast<unsigned>(STATUS_HEAP_FLOOR_BYTES));
      }
      return;
    }
    statusDeferSinceMs_ = 0; // window spent - this attempt goes through
  }
  if (statusDeferStreak_) {
    statusDeferStreak_ = false;
    FW_LOG("[HEAP] status broadcast resumed (free=%u)\n",
           static_cast<unsigned>(ESP.getFreeHeap()));
  }
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
  // + scan (D3, 12 × ~48 ≈ 600) + the U-95 crash fields (~120) + the U-106
  // crash-TIME fields (heap-at-crash/min/task/millis + live min ≈ 110). Grown for
  // the scan field, then for the crash record, then for the RTC breadcrumb.
  //
  // WHY THE NUMBER IS NOT OPTIMISED AGAIN: `buildStatusJson` bounds every
  // fragment it writes, so the failure mode of a buffer that is too small is
  // SILENT — snprintf truncates and the client receives malformed JSON, which
  // the browser reports as a parse error and the owner reports as "the page is
  // not working properly". A silently truncated diagnostic is worse than no
  // diagnostic. tools/check-firmware-contract.mjs asserts this buffer is larger
  // than the sum of the fragment buffers it has to hold, so the next person to
  // add a field without growing it fails CI instead of shipping a broken page.
  // U-95 Phase 3: static, and 3584 not 1536 — see the matching line in
  // handleStatus() for why (network fields doubled the JSON; the loopTask
  // stack is 8 KB; this buffer used to be a frame inside it).
  static char buf[3584];
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
  // U-77: every HTTP handler runs this first to cap socket writes. The old
  // U-87 `lastHttpAtMs_` stamp it also used to make here was removed with the
  // beacon-parking feature (U-105) — nothing reads the stamp any more.
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
  // U-112 (the owner's "freeze using the page while the app is on Wi-Fi"): an
  // UNSTALLED client keeps this loop running for the full 3500 ms budget -
  // that is what the budget is FOR (a page must still arrive over a slow link)
  // - but during those 3500 ms loop() is not running: no ws.loop() for the
  // app, no modeTicker, no BT poll, so every link looks frozen while the page
  // loads, and the cheap pages (handlers below) make it look intermittent.
  // A client that is NOT draining TCP - the wedge shape this function exists
  // to survive - produced the same 3500 ms every pass, for free, because a
  // zero-byte write makes no progress at all. From NOW a stalled (zero-write)
  // pass ends the transfer: the connection is closed below, so the browser
  // retries a fresh request (nothing is corrupted - Content-Length is honest
  // and the socket close tells the client the response ended) instead of the
  // car spending 3500 ms per pass on a client that will never read it.
  // Moving traffic keeps its full budget - only a provably dead reader is
  // cut short.
  uint8_t stallRetries = 0;  // consecutive zero-byte writes this pass
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
      stallRetries++;
      const unsigned long gapStart = millis();
      while (millis() - gapStart < HTTP_WRITE_RETRY_GAP_MS) {
        esp_task_wdt_reset();
        delay(1);
      }
      // U-112: one retry gap already spent with NO progress. A second
      // consecutive zero-write means the reader is not draining at all - end
      // THIS pass (the close below tells the client the response ended) rather
      // than spend HTTP_BODY_BUDGET_MS of loop() per pass on it: while this
      // loop runs, ws.loop()/BT/pins do not, which is the app "freezing". A
      // client that moves resets the count on the very next byte and keeps the
      // full budget, so slow-but-alive readers are untouched.
      if (stallRetries >= HTTP_STALL_ABORT_AFTER) {
        FW_LOG("[HTTP] root page paused (no client progress after %u attempts)\n",
               static_cast<unsigned>(stallRetries));
        break;
      }
      continue;
    }
    stallRetries = 0;  // progress: a moving reader keeps the full budget
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
  // U-112: the stall-abort break above lands here too (off < pageLen), so an
  // unresponsive reader costs HTTP_STALL_ABORT_AFTER attempts + one gap per
  // pass instead of the whole budget; the browser may retry, and a retry that
  // MOVES gets the full budget again.
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
  // U-88 (extended to HTTP): there is ONE command path. This handler used to
  // call modeManager->handleCommand() immediately AND queuePush(), so every
  // HTTP move ran TWICE (and the `S` branch then queueClear()'d the just-queued
  // stop along with every pending line). Queue only; ModeManager::run() drains
  // and dispatches it on the next loop pass, exactly like the WS path.
  if (modeManager) {
    queuePush(cmd);
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
  // U-88 (extended to HTTP): queue the `#<n>` line only. The direct setSpeed()
  // here applied the clamped speed a SECOND time when ModeManager::run()
  // drained the queue.
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
  // U-95 Phase 3: static, and 3584 not 1536 — the network fields (scan rows
  // with channel/own/saved/current, plus station_ip/ap_ip/broadcast_ip/subnet)
  // roughly doubled the JSON, and a 3.5 KB frame on the 8 KB loopTask stack,
  // nested under the send path, is exactly the kind of quiet risk F-60 was
  // about. Single-task by construction (every caller pumps on loop); if a
  // second task ever reaches buildStatusJson, revisit this line first.
  static char buf[3584];
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
    // the JSON `ip` is never empty on the own AP (app deck + tap-to-open).
    WiFi.softAPIP().toString().toCharArray(ipStr, sizeof(ipStr));
  }
  // U-95 Phase 3: the contract had ONE `ip` slot, which carries the STA lease
  // when connected and the softAP gateway otherwise — so a client could not
  // tell which network the car was on beyond that, nor reach it by directed
  // broadcast (the "find the car on this network" path needs exactly that,
  // and WiFi.softAPBroadcastIP() was never called anywhere in this repo).
  // Four car-computed fields; empty string = that interface is down. The
  // single `ip` slot stays as-is for older clients — additive only.
  char stationIp[16] = "";
  char apIp[16] = "";
  char bcastIp[16] = "";
  char subnetMask[16] = "";
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.localIP().toString().toCharArray(stationIp, sizeof(stationIp));
    // v1.2.3 (owner: "the last line shows the Broadcast IP of the current used
    // network"): broadcast_ip/subnet were AP-only since U-95 Phase 3 — on a
    // joined LAN they came back empty and the page/app Bcast field showed '--'.
    // Compute the STA directed broadcast (ip | ~mask) so the JSON matches the
    // OLED's Bcast line for both interfaces.
    {
      IPAddress ipA = WiFi.localIP(), maskA = WiFi.subnetMask();
      IPAddress bcA(ipA[0] | (uint8_t)~maskA[0], ipA[1] | (uint8_t)~maskA[1],
                    ipA[2] | (uint8_t)~maskA[2], ipA[3] | (uint8_t)~maskA[3]);
      bcA.toString().toCharArray(bcastIp, sizeof(bcastIp));
      maskA.toString().toCharArray(subnetMask, sizeof(subnetMask));
    }
  }
  if (apRunning_) {
    WiFi.softAPIP().toString().toCharArray(apIp, sizeof(apIp));
    WiFi.softAPBroadcastIP().toString().toCharArray(bcastIp, sizeof(bcastIp));
    WiFi.softAPSubnetMask().toString().toCharArray(subnetMask, sizeof(subnetMask));
  }
  int speed = modeManager ? modeManager->getSpeed() : static_cast<int>(MIN_SPEED);
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
  // AP broadcast id — the app deck shows both. T-64: when nothing is stored,
  // the car's OWN network (4WD4MCar_WiFi) IS the network — the compile-time
  // secrets.h fallback is never joined nor named.
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
  // U-95 Phase 3: rows grew (channel/own/saved/current) — 12 rows at the worst
  // case widths need ~1720 bytes, so the fragment is 1856, not 512. The
  // status-buffer gate re-derives the sum and will fail before this truncates.
  char scanJson[1856] = "";
  buildScanJson(scanJson, sizeof(scanJson));
  // Connections-Hub: the stable board id (fw:<id> profile key for WiFi cars).
  char boardId[8] = "";
  boardIdHex(boardId, sizeof(boardId));
  // U-96i: the firmware's own version, on the wire. The app had NO way to know
  // what firmware a car runs — the registry's bundled catalog value (a static
  // guess) was presented as truth, and the fleet crash table's fw_version column
  // was filled from it. `boardId` is identity, not a version; the two must never
  // be conflated again (the app had exactly that bug: `fw: telemetry.id`).
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
       "\"station_ip\":\"%s\","
       "\"ap_ip\":\"%s\","
       "\"broadcast_ip\":\"%s\","
       "\"subnet\":\"%s\","
       "\"rssi\":%d,"
       "\"signal\":%d,"
       "\"uptime_ms\":%lu,"
       "\"free_heap\":%u,"
       "\"reset_reason\":\"%s\","
       "\"boot_count\":%lu,"
       "\"crash_count\":%lu,"
       "\"last_crash_phase\":\"%s\","
       "\"last_crash_heap\":%lu,"
       "\"last_crash_heap_at_crash\":%lu,"
       "\"last_crash_heap_min\":%lu,"
       "\"last_crash_task\":\"%s\","
       "\"last_crash_millis\":%lu,"
       "\"live_min_heap\":%u,"
       "\"phase\":\"%s\","
       "\"speed\":%d,"
       "\"trim\":%d,"
       "\"min_speed\":%d,"
       "\"max_speed\":%d,"
       "\"ssid\":\"%s\","
       "\"want\":\"%s\","
       "\"ap\":\"%s\","
       "\"id\":\"%s\","
       "\"fw\":\"%s\","
       "\"networks\":[%s],"
       "\"scan\":%s,"
       "\"wifi_enabled\":%s,"
       "\"bt_enabled\":%s,"
       "\"boot_autojoin\":%s"
     "}",
     statusWord,
     modeToken,
     modeName,
     stubMode ? "true" : "false",
     caps,
     conn ? "true" : "false",
      ipStr,
      stationIp,
      apIp,
      bcastIp,
      subnetMask,
      rssi,
      pct,
      uptime,
      static_cast<unsigned int>(free_heap),
      ci.label,
      static_cast<unsigned long>(ci.bootCount),
      static_cast<unsigned long>(ci.crashCount),
      ci.phase,
      static_cast<unsigned long>(ci.heapAtBoot),
      static_cast<unsigned long>(ci.heapAtCrash),
      static_cast<unsigned long>(ci.minHeap),
      ci.task,
      static_cast<unsigned long>(ci.millisAtCrash),
      static_cast<unsigned int>(crashLogMinHeap()),
      crashLogLivePhase(),
      speed,
      static_cast<int>(modeManager ? modeManager->getTrim() : 0),
      static_cast<int>(MIN_SPEED),
      static_cast<int>(MAX_SPEED),
      ssidOut,
      wantOut,
      apOut,
      boardId,
      FW_VERSION,
      networks,
      scanJson,
      wifiEnabled_ ? "true" : "false",
      btEnabled_ ? "true" : "false",
      bootAutojoin_ ? "true" : "false"
  );
}
