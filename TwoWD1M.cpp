// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: TwoWD1M.cpp — signed SPD + SERVO drive handler (2WD1M stub on this
//       car); servo deviation (center 90) maps to LEFT/RIGHT tank steer
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
// 2WD1M.cpp
// Signed SPD + SERVO drive handler for the remote's 2WD1M mode.
// Maps servo deviation (center 90) to LEFT/RIGHT on the differential car.
// Ported 1:1 from Genum_SELF_BALANCE_CAR/2WD1M.cpp (proven in-fleet).

#include "TwoWD1M.h"
#include "ModeManager.h"
#include "Config.h"
#include <string.h>
#include <stdlib.h>

void TwoWD1M::reset() {
  speed_ = 0;
  servo_ = 90;
  trim_ = 0;
}

bool TwoWD1M::handleCommand(const char* cmdBuf, MotorControl* motor, uint16_t &speed, ModeManager* modeMgr) {
  if (!cmdBuf || cmdBuf[0] == '\0') return false;

  // Signed speed: SPD-255 .. SPD255 (0 = stop)
  if (strncasecmp(cmdBuf, "SPD", 3) == 0) {
    int val = atoi(cmdBuf + 3);
    if (val < -255) val = -255;
    if (val > 255) val = 255;
    speed_ = static_cast<int16_t>(val);
    if (speed_ != 0) {
      speed = static_cast<uint16_t>(abs(speed_));  // expose magnitude for OLED
    }
    applyDrive(motor, modeMgr);
    return true;
  }

  // Servo steering: SERVO0 .. SERVO180 (center 90)
  if (strncasecmp(cmdBuf, "SERVO", 5) == 0) {
    int val = atoi(cmdBuf + 5);
    if (val < 0) val = 0;
    if (val > 180) val = 180;
    servo_ = val;
    applyDrive(motor, modeMgr);
    return true;
  }

  // Trim offset: TRIM<offset> (-90..90), persisted on the car
  if (strncasecmp(cmdBuf, "TRIM", 4) == 0) {
    int val = atoi(cmdBuf + 4);
    if (val < -90) val = -90;
    if (val > 90) val = 90;
    trim_ = val;
    applyDrive(motor, modeMgr);
    if (modeMgr) modeMgr->setStatusText("Trim updated");
    return true;
  }

  return false;
}

void TwoWD1M::applyDrive(MotorControl* motor, ModeManager* modeMgr) const {
  if (!motor) return;

  const int steerDeadband = 12;  // servo deadband around center 90
  const int deviation = servo_ - 90 + trim_;  // apply trim offset to deviation

  if (speed_ == 0) {
    motor->drive(Action::STOP, 0);
    if (modeMgr) modeMgr->setStatusText("Stopped");
    return;
  }

  uint16_t mag = static_cast<uint16_t>(abs(speed_));
  if (mag < static_cast<uint16_t>(MIN_SPEED)) mag = MIN_SPEED;
  if (mag > static_cast<uint16_t>(MAX_SPEED)) mag = MAX_SPEED;

  Action action;
  const char* statusText;
  if (deviation > steerDeadband)      { action = Action::RIGHT;    statusText = "Right"; }
  else if (deviation < -steerDeadband){ action = Action::LEFT;     statusText = "Left"; }
  else if (speed_ > 0)                { action = Action::FORWARD;  statusText = "Forward"; }
  else                                { action = Action::BACKWARD; statusText = "Backward"; }

  motor->drive(action, mag);

  // Route through the single status channel; the OLED extra buffer (with the
  // STEER value) is rebuilt by ModeManager::setStatusText() for this mode.
  if (modeMgr) modeMgr->setStatusText(statusText);
}
