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

void setup() {
  Serial.begin(115200);
  delay(50);

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
  // applyBootState). Runs AFTER webServerComm.init() so a restored SERVER
  // session can join WiFi from setup(); a fresh boot with no saved state
  // keeps the constructor defaults (BT / MIN_SPEED). The previous blanket
  // setMode(BT) here is gone — restored state is now authoritative.
  // v1.4.0 provisioning: WIFICFG lines (app → BT) are intercepted before any
  // other command path, in every mode. Replies ride STATE as REPLY=….
  // T-48 (round-5): ROUTERS;… registry commands ride the SAME hook (single
  // system-command entry point — FAILSAFE W-14/F-21).
  modeManager.setSystemCommandHook([](const char* cmd) -> bool {
    const bool isRouterCmd = webServerComm.handleRouterCommand(cmd);
    const bool isWifiCmd = webServerComm.handleWifiConfigCommand(cmd);
    if (!isRouterCmd && !isWifiCmd) return false;
    if (webServerComm.lastWifiReply().length() > 0) {
      modeManager.queueReply(webServerComm.lastWifiReply().c_str());
      // T-35: while already in ESP_SER, comm_ is webServerComm whose write()
      // is a transport no-op — the queued WIFICFG;STORED;… confirmation would
      // DIE there and the app would keep showing the stale/default SSID.
      // T-63 (round-9): sendState() fans the reply out over EVERY connected
      // transport — the Bluetooth channel after a BT provision, and the
      // website's WebSocket after an in-SERVER router change (the app's
      // Remote window). Pairs with app optimistic SSID A-15.
      modeManager.sendState();
      webServerComm.clearLastWifiReply();
    }
    return true;
  });
  webServerComm.loadSavedRouters();
  modeManager.applyBootState();

  modeManager.setAuxComm(&btComm);
  modeManager.setComm(&btComm);
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

  if (!btComm.connected()) {
    if (btLastConnectedOkMs == 0) btLastConnectedOkMs = millis();
    else if (millis() - btLastConnectedOkMs > BT_HARD_RESTART_MS) {
      Serial.println("[BT-WD] Hard restart of BT via btComm due to prolonged disconnect");
      btComm.restart();
      btLastConnectedOkMs = millis();
    }
  } else {
    btLastConnectedOkMs = 0;
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
    if (current == ModeManager::Mode::SERVER) {
      FW_LOG_VERBOSE("Switching to SERVER mode, connecting WiFi...\n");
      bool ok = webServerComm.beginStation();
      FW_LOG_VERBOSE("beginStation() returned: %s\n", ok ? "OK" : "FAIL");
      if (ok) webServerComm.refreshExtraInfo();
      modeManager.setComm(&webServerComm);
    } else {
      if (lastMode == ModeManager::Mode::SERVER) {
        FW_LOG_VERBOSE("Leaving SERVER mode, stopping WebServer...\n");
        webServerComm.stop();
      }
      modeManager.setComm(&btComm);
      modeManager.setExtraInfo("");
    }
    lastMode = current;
  }

  if (webServerComm.isRunning()) {
    webServerComm.handle();
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