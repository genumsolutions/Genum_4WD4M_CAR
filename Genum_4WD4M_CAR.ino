// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: Genum_4WD4M_CAR.ino — entry point: boot, boot splash, watchdog,
//       mode-switch wiring (BT ↔ SERVER)
// Property of GENUM SOLUTIONS PVT. LTD.

// =====================================================================
#include "Config.h"
#include "MotorControl.h"
#include "CommInterface.h"
#include "BluetoothComm.h"
#include "OledDisplay.h"
#include "ModeManager.h"
#include "WebServerComm.h"
#include <Arduino.h>
#include "esp_task_wdt.h"
#include "esp_system.h"   // U-70: esp_reset_reason() — why did the car boot?
#include <Preferences.h>  // U-79: last-used transport persistence

L298NControl motor;
BluetoothComm btComm;
Oled_1_3 oled;
WebServerComm webServerComm;
ModeManager modeManager(&motor, &btComm, &oled);

static ModeManager::Mode lastMode = ModeManager::Mode::BT;

// T-7b (2WD1M-car parity): first disconnect sets the "healthy since" marker;
// once the link has been down for BT_HARD_RESTART_MS the BT stack is
// re-initialized (restart(), throttled to once per 60 s).
static unsigned long btLastConnectedOkMs = 0;

// F-60 (owner bug 2026-10-02, "the car resets when I switch the wifi router
// from the app"): the watchdog below used to treat "no BT client" as "the BT
// stack is wedged". Those are NOT the same thing. A car parked on its own AP
// or on the home router with nobody connected over BT is perfectly HEALTHY -
// and switching the car to the home router is EXACTLY that state (the phone
// leaves BT to follow the car onto the router). So after BT_HARD_RESTART_MS
// the watchdog tore the stack down, and the old escalation rebooted the car.
//
// The discriminator is not time, it is TRANSITION: only a link that was
// CONNECTED and then dropped can be a fault. A stack that has never had a
// client since boot is simply unused. Armed on the first real connection and
// re-armed by a fresh connection, so a genuine mid-session drop still recovers.
static bool btEverConnected = false;

// ===== U-79 (2026-10-03): TRANSPORT POWER POLICY (owner ruling) ==========
//  - IDLE (no client on anything): Bluetooth AND Wi-Fi (hotspot; router per
//    boot preference) both broadcast until the user picks one.
//  - A client on ONE transport switches the OTHER off (power save). The
//    transport the client is using is never touched.
//  - When that connection goes away, both broadcast again after a short
//    grace period — the car is always reachable somehow.
//  - Boot/resume prefer the LAST USED transport: Wi-Fi last -> auto-join the
//    last good router (short budget, hotspot fallback of last resort);
//    Bluetooth last -> stay on hotspot + BT, no router hop.
enum class LastTransport : uint8_t { WIFI = 0, BT = 1 };
static LastTransport lastTransport = LastTransport::WIFI;
static bool webUp = false;   // web layer (AP + HTTP/WS + router join) is up
static bool btUp = false;    // Bluetooth SPP is up
static bool transportsIdle = false;
static unsigned long transportsIdleSinceMs = 0;
constexpr unsigned long TRANSPORT_GRACE_MS = 3000;

static void saveLastTransport(LastTransport t) {
  lastTransport = t;
  Preferences prefs;
  prefs.begin("botcfg", false);
  prefs.putUChar("last_transport", static_cast<uint8_t>(t));
  prefs.end();
}

static void loadLastTransport() {
  Preferences prefs;
  prefs.begin("botcfg", true);
  const uint8_t v = prefs.getUChar("last_transport",
                                   static_cast<uint8_t>(LastTransport::WIFI));
  prefs.end();
  lastTransport = (v == static_cast<uint8_t>(LastTransport::BT))
                      ? LastTransport::BT
                      : LastTransport::WIFI;
}

void setup() {
  Serial.begin(115200);
  delay(50);

  // U-70 (2026-10-02): say WHY the car is booting.
  //
  // The owner reported "the car restarts when the app directs to cars webpage"
  // — with no other symptom and no way to tell a watchdog abort from a stack
  // smash from a brownout from an intentional reset. Every one of those looks
  // identical from the outside ("it restarted"), which is why the report could
  // not be acted on: the cause is printed on the serial line at the exact moment
  // it happens, and nobody had been reading it.
  //
  // `esp_reset_reason()` is latched by the ROM before our code runs, so this
  // always describes THIS boot. A non-power-on reason is announced loudly,
  // because that is the case worth noticing.
  {
    const esp_reset_reason_t why = esp_reset_reason();
    const char* label = "unknown";
    switch (why) {
      case ESP_RST_POWERON:  label = "power-on"; break;
      case ESP_RST_EXT:      label = "external pin"; break;
      case ESP_RST_SW:       label = "software restart"; break;
      case ESP_RST_PANIC:    label = "PANIC (crash/exception)"; break;
      case ESP_RST_INT_WDT:  label = "interrupt watchdog"; break;
      case ESP_RST_TASK_WDT: label = "TASK WATCHDOG (loop hung >5s)"; break;
      case ESP_RST_WDT:      label = "other watchdog"; break;
      case ESP_RST_DEEPSLEEP:label = "deep sleep wake"; break;
      case ESP_RST_BROWNOUT: label = "BROWNOUT (undervoltage)"; break;
      case ESP_RST_SDIO:     label = "SDIO"; break;
      default: break;
    }
    Serial.printf("[BOOT] reset reason: %d (%s)\n", (int)why, label);
    if (why != ESP_RST_POWERON && why != ESP_RST_EXT && why != ESP_RST_DEEPSLEEP) {
      // The line that matters: it means the car restarted on its own.
      Serial.println("[BOOT] *** UNSOLICITED RESTART - see the reason above ***");
      Serial.println("[BOOT] *** if it says TASK WATCHDOG, loop() blocked; PANIC means a crash ***");
    }
  }

  // F-24: esp32 core 3.x task watchdog — reconfigure FIRST (the core
  // pre-initializes the TWDT), loopTask ONLY (idle_core_mask = 0; subscribing
  // idle tasks caused a ~19s reboot loop). A genuine loop hang still aborts
  // after 5s; normal loop work can never starve an idle task.
  esp_task_wdt_config_t wdt_cfg = {
    .timeout_ms = 5000,
    .idle_core_mask = 0,
    .trigger_panic = true,
  };
  if (esp_task_wdt_reconfigure(&wdt_cfg) != ESP_OK) {
    esp_task_wdt_init(&wdt_cfg);
  }
  esp_task_wdt_add(NULL);

  motor.begin();
  btComm.begin();
  oled.begin();

  pinMode(SWITCH_PIN, INPUT_PULLUP);

  webServerComm.init();
  webServerComm.setModeManager(&modeManager);
  // T-33: load app-provisioned credentials NOW that NVS is live. The
  // WebServerComm GLOBAL constructor runs before the Arduino core's
  // nvs_flash_init(), so reading Preferences there always failed — the car
  // silently reverted to the compile-time default WiFi after every power
  // cycle. Must run BEFORE applyBootState() so a restored SERVER session
  // rejoins the user's router, not the default.
  webServerComm.loadStoredWifi();
  // T-13: restore the last mode + speed from NVS (validated + clamped in
  // applyBootState). v2: mode is a DRIVE concept only (4WD4M is the single
  // live mode); the web layer below is started unconditionally BEFORE this
  // so the car is always reachable. The donor's restored-SERVER special case
  // is gone — no mode implies a transport anymore.
  // v1.4.0 provisioning: WIFICFG lines (app → BT) are intercepted before any
  // other command path, in every mode. Replies ride STATE as REPLY=….
  // T-48 (round-5): ROUTERS;… registry commands ride the SAME hook (single
  // system-command entry point — FAILSAFE W-14/F-21).
  // v2: WIFICFG NO LONGER SWITCHES MODE — the car stays in 4WD4M; the web
  // layer rejoins the new network while every transport stays live.
  modeManager.setSystemCommandHook([](const char* cmd) -> bool {
    const bool isRouterCmd = webServerComm.handleRouterCommand(cmd);
    const bool isWifiCmd = webServerComm.handleWifiConfigCommand(cmd);
    if (!isRouterCmd && !isWifiCmd) return false;
    if (webServerComm.lastWifiReply().length() > 0) {
      modeManager.queueReply(webServerComm.lastWifiReply().c_str());
      // T-35/T-63: fan the reply out over EVERY connected transport (STATE
      // carries REPLY=…). v2: no ESP_SER special case — both transports are
      // permanent, so the confirmation always reaches the app.
      modeManager.sendState();
      webServerComm.clearLastWifiReply();
    }
    return true;
  });
  webServerComm.loadSavedRouters();
  modeManager.applyBootState();

  // v2 ARCHITECTURE (owner plan 2026-09-28, Phase 1): the web layer is an
  // ALWAYS-ON connection/setup channel — NEVER a mode. Bring the AP + HTTP/WS
  // servers up once here and leave them running for the whole uptime; both
  // transports are wired permanently (comm_ = web, auxComm_ = BT) and polled
  // every loop(). No mode-based comm swapping exists anymore.
  webServerComm.beginAlwaysOn();
  webUp = true;
  btUp = true;
  modeManager.setAuxComm(&btComm);   // Bluetooth stays the aux transport
  modeManager.setComm(&webServerComm);  // comm_ = always-on web transport
  // U-79: boot network preference. Wi-Fi was the transport the user LAST
  // used -> try the last good router (short budget; the own hotspot is the
  // fallback of last resort). Bluetooth was last -> skip the router hop
  // entirely; hotspot + BT are already broadcasting.
  loadLastTransport();
  if (lastTransport == LastTransport::WIFI) {
    webServerComm.armBootAutoJoin();
  } else {
    Serial.println("[BOOT] Last used: Bluetooth - staying on hotspot + BT (router join skipped)");
  }
  lastMode = modeManager.getMode();
}

static bool checkSwitchPressed() {
  static int lastReading = HIGH;
  static int stableState = HIGH;
  static unsigned long lastDebounce = 0;
  int reading = digitalRead(SWITCH_PIN);
  if (reading != lastReading) lastDebounce = millis();
  if ((millis() - lastDebounce) > SWITCH_DEBOUNCE_MS) {
    if (reading != stableState) {
      stableState = reading;
      lastReading = reading;
      return (stableState == LOW);
    }
  }
  lastReading = reading;
  return false;
}

// T-7b/T-7c (2WD1M-car parity): low-voltage emergency stop (only when a
// divider is wired — VOLTAGE_MONITOR_PIN/VOLTAGE_CRITICAL_V in Config.h)
// plus the BT hard-restart watchdog for prolonged disconnects.
static void maybeCheckSupplyAndBTWatchdog() {
#ifdef VOLTAGE_MONITOR_PIN
  int raw = analogRead(VOLTAGE_MONITOR_PIN);
  float measuredV = (raw / 4095.0f) * 3.3f * 2.0f;
  if (measuredV < VOLTAGE_CRITICAL_V) {
    Serial.printf("[POWER] Low V=%.2fV — stopping motors\n", measuredV);
    motor.drive(Action::STOP, 0);
    delay(50);
  }
#endif

  // F-60: arm the watchdog only on a link that was CONNECTED and then dropped.
  // A car that has never had a BT client is idle, not wedged - so parking it
  // on the router (or its own AP) can never trigger a stack teardown.
  // U-79: the whole watchdog is gated on btUp — while BT is deliberately OFF
  // (web client active, power policy) a "prolonged disconnect" is the POLICY,
  // not a wedged stack, and must never trigger a restart that would undo it.
  if (btUp) {
    if (btComm.connected()) {
      btEverConnected = true;
      btLastConnectedOkMs = 0;
    } else if (btEverConnected) {
      if (btLastConnectedOkMs == 0) btLastConnectedOkMs = millis();
      else if (millis() - btLastConnectedOkMs > BT_HARD_RESTART_MS) {
        Serial.println("[BT-WD] Hard restart of BT via btComm due to prolonged disconnect");
        btComm.restart();
        btLastConnectedOkMs = millis();
      }
    }
  }
}

void loop() {
  // F-24: feed the loopTask watchdog every iteration.
  esp_task_wdt_reset();

  if (checkSwitchPressed()) {
    modeManager.cycleMode();
  }

  // T-1 single command path: ALL commands (BT via auxComm_, primary via
  // comm_) are read line-by-line in ModeManager::run() and dispatched through
  // handleCommand() — mode tokens, REQ_STATE/STATE, SPD/SPD0, F/B/L/R/S.
  modeManager.run();

  ModeManager::Mode current = modeManager.getMode();
  if (current != lastMode) {
    // U-79: drive-mode changes no longer touch transports or the web layer.
    // The retired ESP_SER/ESP_CLI branches are gone with the modes themselves;
    // extraInfo refreshes every second from handle() regardless.
    lastMode = current;
  }

  // v2: the web layer is ALWAYS-ON — handle() runs unconditionally every
  // loop (HTTP + WS + STA/AP resolution + 1 s JSON broadcast). The donor's
  // `if (webServerComm.isRunning())` mode-gate is retired.
  webServerComm.handle();

  // ===== U-79: transport power arbitration ============================
  // One client on one transport switches the OTHER off; when nothing is
  // connected for longer than the grace period, both broadcast again. The
  // transport WITH a client is never touched, so a live control path can
  // never be yanked out from under the user.
  {
    const bool btClient = btComm.connected();
    const bool webClient = webServerComm.hasWsClients();
    if (btClient && !webClient) {
      if (webUp) {
        webServerComm.stop();
        webUp = false;
        saveLastTransport(LastTransport::BT);
        Serial.println("[POWER] Wi-Fi off - Bluetooth client active");
      }
      transportsIdle = false;
    } else if (webClient && !btClient) {
      if (btUp) {
        btComm.stop();
        btUp = false;
        saveLastTransport(LastTransport::WIFI);
        Serial.println("[POWER] Bluetooth off - web client active");
      }
      transportsIdle = false;
    } else if (!btClient && !webClient) {
      if (!transportsIdle) {
        transportsIdle = true;
        transportsIdleSinceMs = millis();
      } else if (millis() - transportsIdleSinceMs >= TRANSPORT_GRACE_MS) {
        if (!btUp) {
          btUp = btComm.begin();  // idempotent; true = advertising again
          if (btUp) Serial.println("[POWER] Bluetooth advertising (no client)");
        }
        if (!webUp) {
          webServerComm.beginAlwaysOn();
          webUp = true;
          if (lastTransport == LastTransport::WIFI) {
            webServerComm.armBootAutoJoin();  // short budget; hotspot fallback
          }
          Serial.println("[POWER] Wi-Fi up (no client)");
        }
        transportsIdle = false;
      }
    } else {
      transportsIdle = false;  // both connected: leave both alone
    }
  }

  // T-7b/T-7c (2WD1M-car parity): prolonged BT disconnect → hard-restart the
  // BT stack; low battery (when a divider is wired) or critically low heap →
  // emergency motor stop.
  maybeCheckSupplyAndBTWatchdog();

  if (ESP.getFreeHeap() < HEAP_CRITICAL_BYTES) {
    Serial.printf("[EMERGENCY] Low heap %d — stopping motors\n", ESP.getFreeHeap());
    motor.drive(Action::STOP, 0);
    delay(50);
  }

  yield();
}