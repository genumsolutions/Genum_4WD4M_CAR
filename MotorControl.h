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
  // U-132: tick the soft-start / reversal dead-time state machine. Called once
  // per ModeManager::run() pass, so a driver with no ramp behaviour (or one
  // stopped at the moment) leaves the default no-op in place.
  virtual void update() {}
  virtual ~MotorControl() = default;
};

#if MOTOR_DRIVER == L298N
class L298NControl : public MotorControl {
public:
  L298NControl() = default;
  ~L298NControl() override = default;

  bool begin() override;
  void drive(Action action, uint16_t speed) override;
  void update() override;

  // Allow speed updates from "SPDxx" commands
  void setSpeed(uint16_t speed);

private:
  uint32_t mapSpeedToDuty(uint16_t speed) const;
  // U-132: the three primitives the ramp is built from. applyDirection() writes
  // only the IN pins (the bridge is already de-energised when it is called),
  // writeDuty() drives both enables, stopNow() de-energises unconditionally.
  void applyDirection(Action action);
  void writeDuty(uint32_t duty);
  void stopNow();

  // U-132 ramp / dead-time state. `applied_` is what the pins are actually
  // energising (STOP between a reversal and its dead-time), `targetDuty_` is
  // where the current ramp is heading, and `lastDuty_` is what the LEDC
  // channels hold so a ramp only writes when the value really moves.
  Action applied_ = Action::STOP;
  Action pendingAction_ = Action::STOP;
  bool pending_ = false;        // a direction change is waiting out the dead-time
  bool ramping_ = false;
  uint32_t pendingDuty_ = 0;
  uint32_t deadUntilMs_ = 0;
  uint32_t targetDuty_ = 0;
  uint32_t rampFromDuty_ = 0;
  uint32_t rampStartMs_ = 0;
  uint32_t lastDuty_ = 0;
  // U-109: false until begin() configures the LEDC channels. applyBootState()
  // stops the motors during setup(), BEFORE motor.begin() runs (motors are
  // deferred to the end of setup by the U-96 power budget), and an unguarded
  // drive() reached ledc_set_duty on unconfigured channels — the boot log's
  // "E ledc: LEDC is not initialized" noise. Before begin() the pins are inputs
  // and the bridge is inert, so a no-op is the correct answer.
  bool begun_ = false;
};
#endif