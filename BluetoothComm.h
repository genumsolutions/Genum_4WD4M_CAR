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
  // U-79 / U-105: the deliberate transport switch — take BT down only when the
  // owner turns Bluetooth off. begin() is idempotent, so a later begin() brings
  // it back.
  void stop();
  bool restart();
  // U-110: is SPP advertising right now? The .ino's transport-enforcement loop
  // uses this to keep `btUp` honest and to decide whether to retry a refused
  // SerialBT.begin() — `connected()` is false with no client, so it cannot.
  bool isStarted() const { return started_; }
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
  // U-142: SPP liveness. `txSinceAttach_` arms the timeout only after this
  // attachment has successfully pushed at least one byte, so a fresh client is
  // never judged against a timestamp left over from a previous session.
  // `sppWedged()` is the whole rule: attached, has written before, and has
  // accepted nothing for BT_SPP_TX_STALL_MS. See Config.h for why TX progress
  // and not hasClient() is the honest liveness signal.
  bool sppWedged() const;

  BluetoothSerial SerialBT;
  bool started_ = false;
  unsigned long lastActivityMs = 0;
  bool txSinceAttach_ = false;
  unsigned long lastTxOkMs_ = 0;
};