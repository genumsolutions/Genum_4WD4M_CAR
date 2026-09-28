#pragma once

// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: CommInterface.h — abstract command transport (comm_/auxComm_):
//       read/write/send/connected contract for BT + WebServerComm
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// All commands (F/B/L/R/S, SPD<n>, #<n>, mode tokens, REQ_STATE) are parsed
// in exactly one place: ModeManager::handleCommand(). No command-type enums
// live here anymore (T-1 single command path).

class CommInterface {
public:
  virtual bool begin() = 0;
  virtual size_t available() = 0;
  virtual int read() = 0;
  virtual size_t readBytes(char* buf, size_t len, unsigned long timeoutMs = 0) = 0;
  virtual int peek() = 0;
  virtual size_t write(const char* buf, size_t len) = 0;
  virtual bool send(const char* msg) = 0;
  virtual void flush() = 0;
  virtual bool connected() = 0;
  virtual ~CommInterface() = default;
};