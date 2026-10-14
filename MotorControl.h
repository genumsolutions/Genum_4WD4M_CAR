#pragma once

// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: MotorControl.h — abstract motor driver + L298NControl (Action enum)
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include <Arduino.h>
#include "Config.h"
#include <stdint.h>

enum class Action : uint8_t { STOP = 0, FORWARD, BACKWARD, LEFT, RIGHT };

class MotorControl {
public:
  virtual bool begin() = 0;
  virtual void drive(Action action, uint16_t speed) = 0;
  virtual ~MotorControl() = default;
};

#if MOTOR_DRIVER == L298N
class L298NControl : public MotorControl {
public:
  L298NControl() = default;
  ~L298NControl() override = default;

  bool begin() override;
  void drive(Action action, uint16_t speed) override;

  // Allow speed updates from "SPDxx" commands
  void setSpeed(uint16_t speed);

private:
  uint32_t mapSpeedToDuty(uint16_t speed) const;
  // U-109: false until begin() configures the LEDC channels. applyBootState()
  // stops the motors during setup(), BEFORE motor.begin() runs (motors are
  // deferred to the end of setup by the U-96 power budget), and an unguarded
  // drive() reached ledc_set_duty on unconfigured channels — the boot log's
  // "E ledc: LEDC is not initialized" noise. Before begin() the pins are inputs
  // and the bridge is inert, so a no-op is the correct answer.
  bool begun_ = false;
};
#endif