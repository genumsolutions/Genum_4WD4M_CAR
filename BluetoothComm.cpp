// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: BluetoothComm.cpp — Bluetooth SPP transport: begin/restart (T-7b),
//       tolerant read, write/send, connected
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include "BluetoothComm.h"
#include "Config.h"
#include "CrashLog.h"   // U-106: crash-TIME breadcrumbs on the BT hot paths

bool BluetoothComm::begin() {
  // Advertised name comes from BT_SPP_NAME (Config.h, ONE source) — since
  // U-110 it is "4WD4M Car", matching the OLED frame and the app. Keep in sync
  // with CATALOG.md.
  // U-79: idempotent — the transport power policy stops/starts this at will.
  TRANSPORT_POWER_ENTER_CRITICAL();
  bool alreadyStarted = started_;
  TRANSPORT_POWER_EXIT_CRITICAL();
  
  if (alreadyStarted) return true;

  // U-111 (owner 2026-10-14: "it appears but doesn't pair"): configure pairing
  // SECURITY here, because both settings are read once, inside
  // BluetoothSerial::_init_bt(), and are silently ignored afterwards.
  //
  // THE DEFECT. The Arduino BluetoothSerial default is `_enableSSP = false` and
  // `_pin_code_len = 0`, and this firmware changed neither. Pairing therefore
  // ran as LEGACY pairing, where the car answers Android's PIN request with that
  // ZERO-LENGTH PIN (BluetoothSerial.cpp: `esp_bt_gap_pin_reply(bda, true, 0,
  // NULL)`). Authentication could never succeed: the phone listed the car (the
  // radio was discoverable and connectable) and then failed the bond on every
  // attempt. That is the whole symptom — the car was never "not advertising", it
  // was advertising a pairing it was guaranteed to lose.
  //
  // enableSSP(false, false) selects IO capability NONE = SSP "Just Works": the
  // phone bonds with no PIN and no on-car confirmation, which is the only sane
  // choice for a car with no keypad. The library's bare enableSSP() would instead
  // advertise IO capability IO (both input and output), which REQUIRES the sketch
  // to answer the numeric-comparison callback — with no callback registered it
  // actively REFUSES the bond (BluetoothSerial.cpp CFM_REQ), so it is
  // deliberately not used here.
  //
  // setPin("1234") is the belt-and-braces legacy path: a remote that initiates
  // LEGACY pairing still gets the documented PIN instead of an empty one. It is
  // inert while SSP negotiates.
  SerialBT.enableSSP(false, false);
  SerialBT.setPin("1234", 4);

  // SerialBT.begin() can take time - do it outside critical section
  crashRtcMark("bt-begin");
  bool success = SerialBT.begin(BT_SPP_NAME);
  
  TRANSPORT_POWER_ENTER_CRITICAL();
  started_ = success;
  if (started_) lastActivityMs = millis();
  TRANSPORT_POWER_EXIT_CRITICAL();

  // P4/E1 (heap waterfall 2/6): the heap with the classic-BT controller/host
  // allocated. This is the single biggest drop in the waterfall (P4 suspects (a)
  // BLE memory allocated but never used, and (b) default ACL/bt config buffers),
  // so it is logged on the FIRST bring-up only (begin() is idempotent).
  if (started_) heapWaterfall(HEAPW_STAGE_BT);

  return started_;
}

// U-79 / U-105: the deliberate transport switch. Called ONLY when the owner
// turns Bluetooth off (WIFICFG;BT;OFF) — the old "one transport connected
// powers the OTHER down" power-save teardown is gone (U-105; both transports
// stay up together when both are enabled). begin() brings it back; connected()
// reports false while stopped so the rest of the system sees an honest "no BT
// client".
void BluetoothComm::stop() {
  TRANSPORT_POWER_ENTER_CRITICAL();
  if (!started_) {
    TRANSPORT_POWER_EXIT_CRITICAL();
    return;
  }
  started_ = false;
  TRANSPORT_POWER_EXIT_CRITICAL();
  
  // SerialBT.end() can take time - do it outside critical section
  SerialBT.end();
  
  Serial.println("[BT] stopped (transport disabled)");
}

// T-7b recovery for a WEDGED BT stack (used only by the prolonged-disconnect
// watchdog in the .ino). Throttled to once per 60 s.
//
// OWNER BUG 2026-10-02 ("the car resets when I switch the wifi router from the
// app") - ROOT CAUSE, and it was NOT the ROUTERS;USE path (that is cd3158f,
// separate). Switching the car to the home router takes the phone OFF the
// car's BT, so `hasClient()` goes false and STAYS false. After
// BT_HARD_RESTART_MS the watchdog called this, and the old escalation here
// did `ESP.restart()` when the re-init failed - so the car REBOOTED mid
// handoff, came back on its own AP, and the switch never completed. Worse:
// this ran on a 60 s throttle FOREVER, so an unattended car parked on the
// router rebooted on a loop.
//
// THE RULE (F-60): a transport failure must NEVER take down the whole car.
// The WiFi side (own AP + STA + the app's control path) is what keeps the car
// reachable and driveable; if BT cannot come back, that is a BT problem to
// report, not a reason to reset a working car. So:
//   - no ESP.restart() from here, ever;
//   - a failed re-init is reported and the car KEEPS RUNNING (the watchdog
//     re-arms from the .ino, so a later attempt still happens).
bool BluetoothComm::restart() {
  static unsigned long lastRestartMs = 0;
  unsigned long now = millis();
  if (now - lastRestartMs < 60000) {
    FW_LOG_VERBOSE("[BT] Restart throttled (last restart < 60s ago)\n");
    return false;
  }
  lastRestartMs = now;
  FW_LOG_VERBOSE("[BT] restart requested\n");
  
  // SerialBT.end() can take time - do it outside critical section
  TRANSPORT_POWER_ENTER_CRITICAL();
  bool wasStarted = started_;
  started_ = false;
  TRANSPORT_POWER_EXIT_CRITICAL();
  
  if (wasStarted) {
    SerialBT.end();
  }
  
  delay(80);
  
  if (!begin()) {
    // F-60: report, do NOT reset. The car stays on its AP / router and stays
    // controllable; the watchdog re-arms and we try again later.
    Serial.println("[BT] WARN: BT re-init failed - continuing without BT "
                    "(car stays reachable; no reboot)");
    return false;
  }
  lastActivityMs = millis();
  return true;
}

size_t BluetoothComm::available() {
  return SerialBT.available();
}

int BluetoothComm::read() {
  crashRtcMark("bt-read");
  int c = SerialBT.read();
  if (c >= 0) lastActivityMs = millis();
  return c;
}

size_t BluetoothComm::readBytes(char* buf, size_t len, unsigned long timeoutMs) {
  if (!buf || len == 0) return 0;
  crashRtcMark("bt-read");
  size_t n = SerialBT.readBytes(buf, len);
  if (n > 0) {
    lastActivityMs = millis();
    buf[(n < len) ? n : len - 1] = '\0';
  }
  return n;
}

int BluetoothComm::peek() {
  return SerialBT.peek();
}

size_t BluetoothComm::write(const char* buf, size_t len) {
  crashRtcMark("bt-write");
  size_t n = SerialBT.write((const uint8_t*)buf, len);
  if (n > 0) lastActivityMs = millis();
  return n;
}

bool BluetoothComm::send(const char* msg) {
  if (!msg) return false;
  return write(msg, strlen(msg)) > 0;
}

void BluetoothComm::flush() {
  SerialBT.flush();
}

bool BluetoothComm::connected() {
  return started_ && SerialBT.hasClient();
}

// All command parsing lives in ModeManager::handleCommand() (T-1 single
// command path) — BluetoothComm is a pure transport.