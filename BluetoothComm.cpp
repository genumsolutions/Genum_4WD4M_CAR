// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: BluetoothComm.cpp — Bluetooth SPP transport: begin/restart (T-7b),
//       tolerant read, write/send, connected
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include "BluetoothComm.h"
#include "Config.h"

bool BluetoothComm::begin() {
  // Advertised name matches the fleet docs/app test notes ("BT connect as
  // 4WD CAR" — guide RESUME HERE). Keep in sync with CATALOG.md.
  return SerialBT.begin("4WD CAR");
}

// T-7b last-resort recovery (used only by the prolonged-disconnect watchdog
// in the .ino, 2WD1M-car parity). Throttled to once per 60 s; a failed
// re-init (the controller/bluedroid stack can wedge after end()) escalates
// to a full reboot, which is the only guaranteed recovery.
bool BluetoothComm::restart() {
  static unsigned long lastRestartMs = 0;
  unsigned long now = millis();
  if (now - lastRestartMs < 60000) {
    FW_LOG_VERBOSE("[BT] Restart throttled (last restart < 60s ago)\n");
    return false;
  }
  lastRestartMs = now;
  FW_LOG_VERBOSE("[BT] restart requested\n");
  SerialBT.end();
  delay(80);
  if (!begin()) {
    Serial.println("[BT] FATAL: BT re-init failed - rebooting to recover");
    delay(500);
    ESP.restart();
  }
  lastActivityMs = millis();
  return true;
}

size_t BluetoothComm::available() {
  return SerialBT.available();
}

int BluetoothComm::read() {
  int c = SerialBT.read();
  if (c >= 0) lastActivityMs = millis();
  return c;
}

size_t BluetoothComm::readBytes(char* buf, size_t len, unsigned long timeoutMs) {
  if (!buf || len == 0) return 0;
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
  return SerialBT.hasClient();
}

// All command parsing lives in ModeManager::handleCommand() (T-1 single
// command path) — BluetoothComm is a pure transport.