// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: Genum_4WD4M_CAR.ino — entry point: boot, boot splash, watchdog,
//       mode-switch wiring (BT ↔ SERVER)
// Property of GENUM SOLUTIONS PVT. LTD.
//
// =====================================================================
#include <Arduino.h>
#include "Config.h"
#include "CrashLog.h"      // U-95/F-79: persisted "why did the car restart"
#include "MotorControl.h"
#include "CommInterface.h"
#include "BluetoothComm.h"
#include "OledDisplay.h"
#include "ModeManager.h"
#include "RelayComm.h"       // U-94 Phase 2: Internet transport
#include "WebServerComm.h"
#include "esp_task_wdt.h"
#include <esp_system.h>  // U-70: esp_reset_reason(). U-96's brownout register write was removed — the threshold is Kconfig-baked and not settable from a sketch (see setup()).
#include <Preferences.h>  // U-79: last-used transport persistence

// U-79: Transport power arbitration mutex definition (matches extern in Config.h)
portMUX_TYPE transportPowerMux = portMUX_INITIALIZER_UNLOCKED;

L298NControl motor;
BluetoothComm btComm;
Oled_1_3 oled;
WebServerComm webServerComm;
// U-94 Phase 2: exactly one RelayComm. The WebSockets event callback is a plain
// function pointer, so the instance has to be reachable statically; the gate
// asserts this is the only one in the sketch.
RelayComm relayComm;
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

// ===== U-79 / U-105 (2026-10-13): TRANSPORT AVAILABILITY (owner ruling) =====
// Every pass the loop enforces two persisted switches instead of the old
// one-client-powers-the-other-down arbitration:
//   - Wi-Fi ON (`botcfg/t_wifi`)  -> web layer up (own hotspot + router link)
//   - Bluetooth ON (`botcfg/t_bt`) -> BT advertising up
// Both default ON, so a first-flash car is open on BOTH transports at once and
// both can be connected SIMULTANEOUSLY (the old "one client switches the other
// off" teardown is gone — the owner asked to connect to both at the same time).
// The car never runs with both off: the command path refuses it and this loop
// clamps again. Boot auto-join of the last-switched router (U-98) is gated by
// `botcfg/boot_autojoin` inside armBootAutoJoin() and no-ops when nothing is
// stored; the own hotspot beacon stays up the whole time (never parked, U-105).
static bool webUp = false;   // web layer (AP + HTTP/WS + router join) is up
static bool btUp = false;    // Bluetooth SPP is up
// Legacy: `last_transport` / the idle-grace counters were removed with the
// teardown arbitration they belonged to (U-105). The NVS key is still wiped on
// a fresh install by wipeRoutersOnFreshFirmware() for residue cleanliness.

// U-110 (owner 2026-10-14): bring Bluetooth advertising up and report the TRUTH.
// Declared here and DEFINED after setup() on purpose: the relay-contract
// crash-record gate looks for the first `(motor|btComm|oled).begin(` in the file
// and requires it to follow crashLogBegin(); defining the helper's body (which
// calls btComm.begin()) above setup() would sort that call before the record.
static bool ensureBtAdvertising();

void setup() {
  Serial.begin(115200);
  delay(50);

  // U-95 / F-79: say WHY the car is booting, and REMEMBER it.
  //
  // This block used to be a switch that printed a reset reason to the serial
  // line and nothing else. Two separate failures were sitting in it:
  //
  //  1. The serial line is unreadable in the field. The owner reported "the car
  //     resets again and again" and no cause could be attached, because the
  //     cause was printed at the exact moment of the reset on a wire nobody was
  //     attached to. A diagnostic that can only be read with a bench tool at
  //     the instant of failure is not a diagnostic the owner has.
  //  2. The label the owner was told to look for was UNREACHABLE. The banner
  //     said "if it says TASK WATCHDOG, loop() blocked" - but the watchdog was
  //     configured with .trigger_panic = true below, which routes a task
  //     watchdog trip through the panic handler and reports ESP_RST_PANIC.
  //     So ESP_RST_TASK_WDT could never be printed, and the one line that would
  //     have identified a hung loop() always read "crash" instead. That is how a
  //     watchdog starvation turned into a week of "why does it reset".
  //
  // crashLogBegin() fixes both: the reason is mapped in ONE place (two
  // independent mappers of the same enum is exactly the shape that lets one of
  // them go stale - F-82), the record is persisted, and it counts boots, so a
  // reboot loop is a number instead of a feeling. Call it FIRST: the heap it
  // records is the boot heap only if nothing has allocated yet.
  crashLogBegin();
  {
    const CrashInfo& ci = crashLogInfo();
    Serial.printf("[BOOT] reset reason: %s\n", ci.label);
    if (ci.crashCount > 0) {
      // The line that matters: it means the car restarted on its own. Say HOW
      // MANY times, because "it resets again and again" and "it reset 41 times
      // in 10 minutes" lead to completely different investigations.
      Serial.printf("[BOOT] *** UNSOLICITED RESTART #%u - last was %s, phase '%s', heap %u ***\n",
                    (unsigned)ci.crashCount, ci.label, ci.phase, (unsigned)ci.heapAtBoot);
      Serial.println("[BOOT] *** TASK_WDT means loop() blocked; PANIC means a crash ***");
      Serial.println("[BOOT] *** also on the OLED header and in /status JSON ***");
    }
  }

  // U-106: the boot breadcrumb is armed by crashLogBegin() ("boot"); name the
  // bring-up.  Every subsystem adds its own mark as it runs.
  crashRtcMark("setup");

  // F-24: esp32 core 3.x task watchdog - reconfigure FIRST (the core
  // pre-initializes the TWDT), loopTask ONLY (idle_core_mask = 0; subscribing
  // idle tasks caused a ~19s reboot loop). A genuine loop hang still aborts
  // after LOOP_TASK_WDT_TIMEOUT_MS (10 s since U-95 Phase 1b, 5 s before — the
  // WebSockets library's untunable 5 s read / 5-8 s write fit strictly under
  // it now, each fed immediately before it runs); normal loop work can never
  // starve an idle task.
  //
  // U-95/F-79: .trigger_panic = false. This is the fix for the unreachable
  // label described above. With panic escalation on, a starved loop() is
  // reported as ESP_RST_PANIC and is indistinguishable from a real crash, so
  // the record could not name the cause it existed to name. With it off, a
  // watchdog trip resets the chip directly and esp_reset_reason() reports
  // ESP_RST_TASK_WDT, which is the precise answer. Nothing is lost: the panic
  // handler's backtrace was only reachable via the serial line anyway, which is
  // the channel we just established nobody was reading.
  // U-95/F-77: timeout_ms is not just a number here, it is the OTHER HALF of
  // the invariant that HTTP_BODY_BUDGET_MS has to respect, so the two are tied
  // together at compile time rather than left to agree by inspection. They
  // previously read 5000 and 6000 in two files with a comment asserting the
  // relationship was fine — which is how the guard ended up longer than the
  // thing it was guarding (static_assert in WebServerComm.cpp enforces it now).
  esp_task_wdt_config_t wdt_cfg = {
    .timeout_ms = LOOP_TASK_WDT_TIMEOUT_MS,
    .idle_core_mask = 0,
    .trigger_panic = false,
  };
  if (esp_task_wdt_reconfigure(&wdt_cfg) != ESP_OK) {
    esp_task_wdt_init(&wdt_cfg);
  }
  esp_task_wdt_add(NULL);

  // U-96 (CORRECTED): brownout threshold — there is nothing to set here, and
  // the earlier attempt to set it was doubly wrong.
  //
  // The two mistakes this replaces, both of which cost a broken build:
  //
  //  1. THE REGISTER IS NOT REACHABLE. It used to be written directly as
  //     RTC_CNTL_BROWN_OUT_DET_LVL_SEL_REG. On core 3.3.x there is no such
  //     symbol anywhere — grepping all of esp32-libs finds no DET_LVL register
  //     at all. Brownout moved to a Kconfig option compiled into the
  //     prebuilt esp32-libs; the only public entry point, esp_brownout_init(),
  //     takes no level argument. A sketch CANNOT change this threshold on this
  //     core. The first fix swapped the old name for CONFIG_BROWNOUT_
  //     DET_LVL_SEL_REG, which is simply the Kconfig symbol misread as a
  //     register — it is not one, and it does not exist either.
  //
  //  2. THE INTENT WAS BACKWARDS. The old comment claimed "0 = 2.69V,
  //     1 = 2.42V (default 1)" and wrote 0 to "raise" it. IDF's actual table
  //     runs the other way: SEL_0 is the LOWEST of the eight levels (~2.43V)
  //     and SEL_7 the highest (~2.80V). Writing 0 would have LOWERED the
  //     threshold. So the change was not merely impossible, it was aimed the
  //     wrong way round.
  //
  // The good news is that the mitigation is already in effect: the prebuilt
  // core ships CONFIG_ESP_BROWNOUT_DET_LVL_SEL_0 — i.e. level 0, the LOWEST
  // and therefore most tolerant setting available. The brief WiFi AP+STA init
  // spike does not fake a brownout on this build. The compile-time guard below
  // pins that fact so a future core bump that changes the default fails the
  // BUILD rather than silently reintroducing spurious resets.
  //
  // This is still mitigation, not a cure. If the supply cannot sustain 3.3V
  // under load the car WILL brownout — that is hardware, and the owner has to
  // fix the power source. See FAILSAFE.md.
#if defined(CONFIG_ESP_BROWNOUT_DET_LVL) && (CONFIG_ESP_BROWNOUT_DET_LVL != 0)
#error "Brownout level is no longer 0 (most tolerant). U-96 assumed the prebuilt core ships level 0; it does not. Re-check the brownout mitigation in setup() against this core's IDF brownout table before removing this guard."
#endif

  // U-96: power-budgeted boot. Stagger power-intensive init so marginal supplies
  // don't brownout-reset the car. Motors (H-bridge) deferred to end of setup.
  // U-110: Bluetooth is NOT started here — it goes up after the Wi-Fi radio has
  // settled into its resting AP_STA mode (see the BT bring-up below); the
  // OLED stays first because it is the lightest load and the earliest visible
  // sign of life.
  oled.begin();

  pinMode(SWITCH_PIN, INPUT_PULLUP);
  delay(80);  // let supply settle after OLED init

  webServerComm.init();
  webServerComm.setModeManager(&modeManager);
  // U-98 (owner 2026-10-11): a firmware re-install starts with a CLEAN router
  // surface. Arduino flashing does NOT erase NVS, so saved routers and the
  // stored/last-good credentials survive every re-flash and the next boot then
  // re-joins an outside router the owner had deleted. This erases them when the
  // running firmware differs from the one that last wrote `botcfg/fw_mark`.
  // Runs BEFORE the loads below so they read the wiped NVS directly.
  webServerComm.wipeRoutersOnFreshFirmware();
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
    // U-94 Phase 2: RELAYCFG rides the SAME single system-command entry point as
    // ROUTERS/WIFICFG. It is not a drive command and must never reach
    // handleCommand(); and it is the only way a relay token gets onto a car,
    // which is why it is deliberately reachable over Bluetooth AND the page -
    // the owner can always get at it without re-flashing.
    const bool isRelayCmd = relayComm.handleRelayConfigCommand(cmd);
    // U-108 Phase 7: FACTORY;RESET (fresh start) + TIME;<epoch> (the app's clock)
    // ride the SAME single system-command entry point — they are not drive
    // commands and must work in every mode over every transport.
    const bool isFactoryCmd = webServerComm.handleFactoryCommand(cmd);
    if (!isRouterCmd && !isWifiCmd && !isRelayCmd && !isFactoryCmd) return false;
    if (webServerComm.lastWifiReply().length() > 0) {
      modeManager.queueReply(webServerComm.lastWifiReply().c_str());
      // T-35/T-63: fan the reply out over EVERY connected transport (STATE
      // carries REPLY=…). v2: no ESP_SER special case — both transports are
      // permanent, so the confirmation always reaches the app.
      modeManager.sendState();
      webServerComm.clearLastWifiReply();
    }
    // Every RELAYCFG reply is a fixed literal — "STORED", "CLEARED", "SET",
    // "NOT_SET" — and never contains the token, so queueing it wholesale is
    // safe on every transport including STATE.
    if (isRelayCmd && relayComm.lastReply()[0] != '\0') {
      modeManager.queueReply(relayComm.lastReply());
      modeManager.sendState();
      relayComm.clearLastReply();
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
  delay(80);  // let supply settle after WiFi AP+STA power spike
  // U-110 (owner 2026-10-14): bring Bluetooth up AFTER the Wi-Fi radio has been
  // put into its resting AP_STA mode.
  //
  // This call used to run BEFORE beginAlwaysOn() — i.e. before the first
  // esp_wifi_set_mode() — and that mode set STOP AND RESTARTS THE RADIO (see the
  // U-99 note in WebServerComm::beginAlwaysOn()). Starting classic BT/SPP first
  // and then re-initialising the radio left the SPP stack wedged: the car
  // advertised and APPEARED in the phone's Bluetooth list, but every pairing
  // attempt failed (owner field report 2026-10-14, "it appears but doesn't
  // pair"). Bringing SPP up against an already-settled radio/coexistence arbiter
  // is the supported order.
  //
  // btUp mirrors the ACTUAL bring-up. The old unconditional `btUp = true` marked
  // a failed SerialBT.begin() as "up", so the loop's start-when-down branch never
  // retried and the car could sit the whole uptime with no Bluetooth at all.
  btUp = ensureBtAdvertising();
  modeManager.setAuxComm(&btComm);   // Bluetooth stays the aux transport
  modeManager.setComm(&webServerComm);  // comm_ = always-on web transport
  // U-94 Phase 2: the Internet transport, registered permanently like the other
  // two. begin() returns false when RELAY_HOST is unset or no token is stored,
  // which is the DEFAULT build - the relay is therefore inert unless someone
  // deliberately turned it on and provisioned it. handle() still runs every
  // loop and no-ops when !started_, so this line is safe on a donor car.
  relayComm.begin();
  modeManager.setRelayComm(&relayComm);
  // U-98: a power-up ALWAYS returns to the last-switched router when one is
  // stored AND the auto-join setting is ON (U-105 `botcfg/boot_autojoin`,
  // default ON) — the owner's rule: *"the car should always go to the last
  // switched router network always; if it is not available it should show its
  // own router"*. armBootAutoJoin() no-ops when nothing is stored or the
  // setting is off, uses the full retry budget, and the own hotspot stays up
  // the whole time — an unreachable router just means the give-up path
  // restores the hotspot after the budget is spent.
  webServerComm.armBootAutoJoin();
  // U-96: motors deferred to end of setup — the H-bridge is the biggest
  // non-comms power load and the car is reachable before it loads.
  delay(500);  // give the supply time to stabilize after WiFi+Bluetooth+relay
  motor.begin();

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

// U-110 (owner 2026-10-14): bring Bluetooth advertising up and report the TRUTH.
// begin() is idempotent, so this is nearly free once SPP is running; while it
// keeps REFUSING (the radio can be momentarily busy right after a Wi-Fi mode
// change or a stop/start cycle) it is retried on a 2 s cadence rather than once
// per loop pass — a per-pass retry would hammer the BT controller during a
// fault, and the old code never retried at all (it set btUp = true without
// looking at the result), which could leave the car with no Bluetooth for the
// whole uptime.
static bool ensureBtAdvertising() {
  if (btComm.isStarted()) return true;
  static constexpr unsigned long BT_BEGIN_RETRY_MS = 2000;
  static unsigned long lastAttemptMs = 0;
  const unsigned long now = millis();
  if (now - lastAttemptMs < BT_BEGIN_RETRY_MS) return false;
  lastAttemptMs = now;
  return btComm.begin();
}

void loop() {
  // F-24: feed the loopTask watchdog every iteration.
  esp_task_wdt_reset();
  // U-106: refresh the flash-free crash breadcrumb (free-heap low-water + millis)
  // every pass so a crash always has a recent heap figure and timestamp.
  crashRtcHeartbeat();

  if (checkSwitchPressed()) {
    crashRtcMark("switch");
    modeManager.cycleMode();
  }

  // T-1 single command path: ALL commands (BT via auxComm_, primary via
  // comm_) are read line-by-line in ModeManager::run() and dispatched through
  // handleCommand() — mode tokens, REQ_STATE/STATE, SPD/SPD0, F/B/L/R/S.
  crashRtcMark("cmd");
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
  crashRtcMark("web");
  webServerComm.handle();

  // U-94 Phase 2: pump the relay client. Like the web layer this is
  // unconditional every loop and a no-op when the transport is not started -
  // the WebSockets library does its socket I/O and its reconnection from
  // loop(), so skipping this is the same as not having a relay at all.
  crashRtcMark("relay");
  relayComm.handle();

  // ===== U-79 / U-105: transport availability enforcement =============
  // The two persisted toggles (WIFICFG;WIFI / WIFICFG;BT) decide what may
  // broadcast; this loop forces reality to match every pass. A transport that
  // is ENABLED stays up no matter what the other transport is doing — the old
  // "one client on one transport powers the OTHER down" teardown is gone
  // (owner: *"both should be on and working at the same time if the user
  // wants"*). Both-off is clamped to Wi-Fi-on so the car can never vanish.
  // CRITICAL: Use portMUX ONLY for atomic state reads/writes, NOT for
  // transport operations which can block for hundreds of milliseconds.
  bool wifiEn = webServerComm.wifiEnabled();
  bool btEn = webServerComm.btEnabled();
  if (!wifiEn && !btEn) wifiEn = true;  // clamp: never both off

  if (wifiEn && btEn) {
    // Both enabled: start whatever is down, idempotently. The own hotspot is
    // ALWAYS a way in (never parked, U-105) and a stored router is (re)joined
    // per boot_autojoin inside armBootAutoJoin().
    bool shouldStartBt = false, shouldStartWeb = false;
    {
      TRANSPORT_POWER_ENTER_CRITICAL();
      shouldStartBt = !btUp;
      shouldStartWeb = !webUp;
      if (shouldStartWeb) webUp = true;
      TRANSPORT_POWER_EXIT_CRITICAL();
    }
    if (shouldStartBt) {
      // U-110: btUp follows the REAL bring-up (see ensureBtAdvertising()).
      btUp = ensureBtAdvertising();
      if (btUp) Serial.println("[POWER] Bluetooth advertising (both transports on)");
    }
    if (shouldStartWeb) {
      webServerComm.beginAlwaysOn();
      webServerComm.armBootAutoJoin();  // own hotspot stays up
      Serial.println("[POWER] Wi-Fi up (both transports on)");
    }
  } else if (btEn) {
    // Wi-Fi transport OFF: the whole web layer is a deliberate stop (any live
    // WS/HTTP/relay client was dropped by the owner's own switch, not by a
    // fault), while Bluetooth is the only transport and must be up.
    bool shouldStopWeb = false;
    {
      TRANSPORT_POWER_ENTER_CRITICAL();
      shouldStopWeb = webUp;
      if (shouldStopWeb) webUp = false;
      TRANSPORT_POWER_EXIT_CRITICAL();
    }
    if (shouldStopWeb) {
      webServerComm.stop();
      Serial.println("[POWER] Wi-Fi off - transport disabled (WIFICFG;WIFI;OFF)");
    }
    bool shouldStartBt = false;
    {
      TRANSPORT_POWER_ENTER_CRITICAL();
      shouldStartBt = !btUp;
      TRANSPORT_POWER_EXIT_CRITICAL();
    }
    if (shouldStartBt) {
      // U-110: btUp follows the REAL bring-up (see ensureBtAdvertising()).
      btUp = ensureBtAdvertising();
      if (btUp) Serial.println("[POWER] Bluetooth advertising (Wi-Fi off)");
    }
  } else {
    // Bluetooth transport OFF: stop the BT stack; Wi-Fi (own hotspot + the
    // stored router per boot_autojoin) is the only transport and must be up.
    bool shouldStopBt = false, shouldStartWeb = false;
    {
      TRANSPORT_POWER_ENTER_CRITICAL();
      shouldStopBt = btUp;
      shouldStartWeb = !webUp;
      if (shouldStopBt) btUp = false;
      if (shouldStartWeb) webUp = true;
      TRANSPORT_POWER_EXIT_CRITICAL();
    }
    if (shouldStopBt) {
      btComm.stop();
      Serial.println("[POWER] Bluetooth off - transport disabled (WIFICFG;BT;OFF)");
    }
    if (shouldStartWeb) {
      webServerComm.beginAlwaysOn();
      webServerComm.armBootAutoJoin();  // own hotspot stays up
      Serial.println("[POWER] Wi-Fi up (Bluetooth off)");
    }
  }

  // U-95/F-79: back to idle. The phase markers set above (http-root,
  // relay-tls) are cleared HERE, at the end of a full loop pass, not at the end
  // of the operation. That ordering is deliberate: if the car dies in the
  // middle of any single pass, the record still names the operation it was in,
  // and clearing early would let a later hang inside the SAME pass be reported
  // as idle. One write per pass at most, and crashLogPhase() suppresses it
  // entirely when the phase did not change, so a parked car writes nothing.
  crashLogPhase("");

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