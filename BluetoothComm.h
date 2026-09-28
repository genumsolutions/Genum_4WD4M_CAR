#pragma once

// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: BluetoothComm.h — Bluetooth SPP transport (pure transport; T-1);
//       restart() feeds the T-7b hard-restart watchdog
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include "CommInterface.h"
#include <BluetoothSerial.h>
#include <Arduino.h>

class BluetoothComm : public CommInterface {
public:
  BluetoothComm() = default;
  ~BluetoothComm() override = default;

  BluetoothComm(const BluetoothComm&) = delete;
  BluetoothComm& operator=(const BluetoothComm&) = delete;

  bool begin() override;
  bool restart();
  size_t available() override;
  int read() override;
  size_t readBytes(char* buf, size_t len, unsigned long timeoutMs = 0) override;
  int peek() override;
  size_t write(const char* buf, size_t len) override;
  bool send(const char* msg) override;
  void flush() override;
  bool connected() override;

  unsigned long getLastActivityMs() const { return lastActivityMs; }

private:
  BluetoothSerial SerialBT;
  unsigned long lastActivityMs = 0;
};