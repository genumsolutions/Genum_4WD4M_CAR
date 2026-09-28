#pragma once

// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: TwoWD1M.h — signed-SPD + SERVO drive handler (tank steer via
//       servo deviation; 2WD1M is a stub on this car)
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include <Arduino.h>
#include "MotorControl.h"

// Forward declare ModeManager
class ModeManager;

/**
 * 2WD1M compatibility drive mode (D-2 approved standard).
 *
 * The remote sends signed speed ("SPD<±255>", positive = forward) and servo
 * steering ("SERVO<0..180>", center 90). On this differential-drive car the
 * servo deviation from 90 is mapped to LEFT/RIGHT turning, so the joystick
 * layout behaves like tank steering. Ported from the proven
 * Genum_SELF_BALANCE_CAR/2WD1M.cpp reference.
 *
 * Status text is routed through ModeManager::setStatusText() so the OLED
 * never flickers between the drive status and the manager's own status text.
 */
class TwoWD1M {
public:
  TwoWD1M() = default;
  ~TwoWD1M() = default;

  bool handleCommand(const char* cmdBuf, MotorControl* motor, uint16_t &speed, ModeManager* modeMgr);

  // Clear joystick state (called when entering the mode).
  void reset();

  // Current servo angle 0..180 (center 90) for the OLED steer display.
  int getServo() const { return servo_; }
  // Current trim offset.
  int getTrim() const { return trim_; }

 private:
  void applyDrive(MotorControl* motor, ModeManager* modeMgr) const;

  int16_t speed_ = 0;  // signed -255..255 (0 = stop)
  int servo_ = 90;     // 0..180, center 90
  int trim_ = 0;       // steering trim offset (-90..90), persisted on car
};
