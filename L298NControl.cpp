// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: L298NControl.cpp — L298N H-bridge skid-steer driver (LEDC PWM 0/1)
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#include "MotorControl.h"
#include <Arduino.h>
#include "driver/ledc.h"
#include <stdint.h>
#include "CrashLog.h"   // U-106: crash-TIME breadcrumbs on the motor path

#if MOTOR_DRIVER == L298N

static constexpr int PIN_ENA = 25;
static constexpr int PIN_IN1 = 16;
static constexpr int PIN_IN2 = 17;
static constexpr int PIN_IN3 = 18;
static constexpr int PIN_IN4 = 19;
static constexpr int PIN_ENB = 26;

static const ledc_mode_t MODE = LEDC_HIGH_SPEED_MODE;
static const ledc_timer_t TIMER = LEDC_TIMER_0;
static const ledc_channel_t CH_A = LEDC_CHANNEL_0;
static const ledc_channel_t CH_B = LEDC_CHANNEL_1;

static_assert(PWM_RESOLUTION > 0 && PWM_RESOLUTION <= 16, "PWM_RESOLUTION must be 1..16");
static_assert(MIN_SPEED < MAX_SPEED, "MIN_SPEED must be less than MAX_SPEED");

uint32_t L298NControl::mapSpeedToDuty(uint16_t speed) const {
  const uint32_t maxDuty = (1u << PWM_RESOLUTION) - 1u;
  uint32_t s = speed;
  if (s < MIN_SPEED) s = MIN_SPEED;
  if (s > MAX_SPEED) s = MAX_SPEED;
  // U-133 (owner 2026-11-24: "motors are too slow"): the old form mapped the
  // SPD floor to duty 0 — `(MIN_SPEED - MIN_SPEED) * maxDuty / ...` is literally
  // zero — so the app's slowest selectable speed stopped the car instead of
  // moving it, and its default SPD 170 landed at only ~45% duty. Neither is a
  // ramp/brownout problem: the whole scale was compressed into the upper half.
  // The floor now delivers MOTOR_MIN_DUTY_PERCENT of the rail (enough to turn a
  // rolling car without the stall current U-132 exists to limit) and the rest of
  // the SPD range spreads linearly up to full duty, so SPD 170 lands near 60%
  // and 255 still means 100%.
  const uint32_t floorDuty = (maxDuty * MOTOR_MIN_DUTY_PERCENT) / 100u;
  return floorDuty +
         (((s - MIN_SPEED) * (maxDuty - floorDuty)) / (MAX_SPEED - MIN_SPEED));
}

bool L298NControl::begin() {
  pinMode(PIN_IN1, OUTPUT); pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_IN3, OUTPUT); pinMode(PIN_IN4, OUTPUT);

  digitalWrite(PIN_IN1, LOW); digitalWrite(PIN_IN2, LOW);
  digitalWrite(PIN_IN3, LOW); digitalWrite(PIN_IN4, LOW);

  ledc_timer_config_t timer = {
    .speed_mode       = MODE,
    .duty_resolution  = static_cast<ledc_timer_bit_t>(PWM_RESOLUTION),
    .timer_num        = TIMER,
    .freq_hz          = PWM_FREQ,
    .clk_cfg          = LEDC_AUTO_CLK
  };
  ledc_timer_config(&timer);

  ledc_channel_config_t chA = {
    .gpio_num   = PIN_ENA,
    .speed_mode = MODE,
    .channel    = CH_A,
    .intr_type  = LEDC_INTR_DISABLE,
    .timer_sel  = TIMER,
    .duty       = 0,
    .hpoint     = 0
  };
  ledc_channel_config(&chA);

  ledc_channel_config_t chB = {
    .gpio_num   = PIN_ENB,
    .speed_mode = MODE,
    .channel    = CH_B,
    .intr_type  = LEDC_INTR_DISABLE,
    .timer_sel  = TIMER,
    .duty       = 0,
    .hpoint     = 0
  };
  ledc_channel_config(&chB);

  ledc_set_duty(MODE, CH_A, 0); ledc_update_duty(MODE, CH_A);
  ledc_set_duty(MODE, CH_B, 0); ledc_update_duty(MODE, CH_B);

  begun_ = true;  // U-109: the LEDC channels are live from here on
  return true;
}

void L298NControl::applyDirection(Action action) {
  switch (action) {
    case Action::FORWARD:
      digitalWrite(PIN_IN1, LOW); digitalWrite(PIN_IN2, HIGH);
      digitalWrite(PIN_IN3, LOW); digitalWrite(PIN_IN4, HIGH);
      break;

    case Action::BACKWARD:
      digitalWrite(PIN_IN1, HIGH); digitalWrite(PIN_IN2, LOW);
      digitalWrite(PIN_IN3, HIGH); digitalWrite(PIN_IN4, LOW);
      break;

    case Action::LEFT:
      // Pivot left: left motor backward, right motor forward
      digitalWrite(PIN_IN1, HIGH); digitalWrite(PIN_IN2, LOW);
      digitalWrite(PIN_IN3, LOW);  digitalWrite(PIN_IN4, HIGH);
      break;

    case Action::RIGHT:
      // Pivot right: left motor forward, right motor backward
      digitalWrite(PIN_IN1, LOW);  digitalWrite(PIN_IN2, HIGH);
      digitalWrite(PIN_IN3, HIGH); digitalWrite(PIN_IN4, LOW);
      break;

    case Action::STOP:
    default:
      digitalWrite(PIN_IN1, LOW); digitalWrite(PIN_IN2, LOW);
      digitalWrite(PIN_IN3, LOW); digitalWrite(PIN_IN4, LOW);
      break;
  }
}

void L298NControl::writeDuty(uint32_t duty) {
  ledc_set_duty(MODE, CH_A, duty); ledc_update_duty(MODE, CH_A);
  ledc_set_duty(MODE, CH_B, duty); ledc_update_duty(MODE, CH_B);
  lastDuty_ = duty;
}

void L298NControl::stopNow() {
  // U-132: de-energise, then neutralise the direction pins. Both steps matter:
  // duty 0 with the pins still set coasts, pins low with duty high brakes. Doing
  // them in this order leaves the bridge inert either way.
  writeDuty(0);
  applyDirection(Action::STOP);
  pending_ = false;
  ramping_ = false;
  applied_ = Action::STOP;
  targetDuty_ = 0;
  rampFromDuty_ = 0;
}

void L298NControl::drive(Action action, uint16_t speed) {
  if (!begun_) return;  // U-109: no LEDC/pin writes before begin()
  crashRtcMark("motor");

  // U-132: a stop is immediate and never ramped.
  if (action == Action::STOP) {
    stopNow();
    return;
  }

  const uint32_t duty = mapSpeedToDuty(speed);

  // U-133 (owner 2026-11-24: "motors too slow"): a REPEATED identical command
  // must not restart the ramp. The app and the page both hold-resend the drive
  // line every ~30 ms, and `update()` below re-targets from `lastDuty_` with a
  // fresh `rampStartMs_` on every one of those. Against a 250 ms window that is
  // an exponential approach — each 30 ms hold only closes ~12% of the remaining
  // gap — so a held stick never reaches its duty and the car crawls. Retargeting
  // exists for a CHANGED demand; the same demand has nothing to re-target, so
  // letting the in-flight ramp finish is both faster and gentler on the rail.
  if (!pending_ && action == applied_ && ramping_ && duty == targetDuty_) return;

  // Already turning that way: retarget the ramp from where the duty is now, so
  // a speed change climbs or eases instead of stepping the rail.
  if (!pending_ && action == applied_) {
    rampFromDuty_ = lastDuty_;
    targetDuty_ = duty;
    rampStartMs_ = millis();
    ramping_ = true;
    return;
  }

  // Already waiting to turn that way: just move the target it will ramp to.
  if (pending_ && action == pendingAction_) {
    pendingDuty_ = duty;
    targetDuty_ = duty;
    return;
  }

  // A different direction. The bridge is conducting, so cut it and let the
  // dead-time run before the opposite pair is energised - that is the
  // zero-crossing pause the brownout reports asked for.
  if (applied_ == Action::STOP) {
    // From a standstill there is nothing to wait for: set the pins and ramp.
    applyDirection(action);
    applied_ = action;
    rampFromDuty_ = 0;
    targetDuty_ = duty;
    rampStartMs_ = millis();
    ramping_ = true;
    return;
  }

  writeDuty(0);
  pending_ = true;
  pendingAction_ = action;
  pendingDuty_ = duty;
  targetDuty_ = duty;
  deadUntilMs_ = millis() + MOTOR_REVERSAL_DEADTIME_MS;
}

void L298NControl::update() {
  if (!begun_) return;
  const uint32_t now = millis();

  if (pending_) {
    // Wrap-safe: millis() rolls over every ~49.7 days, and the difference of two
    // unsigned values stays correct across the rollover.
    if (static_cast<int32_t>(now - deadUntilMs_) < 0) return;  // still pausing
    pending_ = false;
    applyDirection(pendingAction_);
    applied_ = pendingAction_;
    rampFromDuty_ = 0;
    rampStartMs_ = now;
    ramping_ = true;
    return;  // duty stays 0 this pass; the climb starts from here
  }

  if (!ramping_) return;

  const uint32_t elapsed = now - rampStartMs_;
  int32_t duty;
  if (elapsed >= MOTOR_SOFTSTART_MS) {
    duty = static_cast<int32_t>(targetDuty_);
    ramping_ = false;
  } else {
    // Signed on purpose: (target - from) is negative when easing off, and the
    // unsigned form would wrap to ~4e9 and drive full duty instead.
    const int32_t delta = static_cast<int32_t>(targetDuty_) - static_cast<int32_t>(rampFromDuty_);
    duty = static_cast<int32_t>(rampFromDuty_) +
           (delta * static_cast<int32_t>(elapsed)) / static_cast<int32_t>(MOTOR_SOFTSTART_MS);
  }
  if (duty < 0) duty = 0;
  const uint32_t out = static_cast<uint32_t>(duty);
  if (out != lastDuty_) writeDuty(out);
}

void L298NControl::setSpeed(uint16_t speed) {
  if (!begun_) return;  // U-109: no LEDC writes before begin()
  // U-132: mid-reversal or stopped there is nothing turning to re-target; the
  // pending/next drive() carries the new magnitude.
  if (applied_ == Action::STOP || pending_) return;
  // U-133: same reasoning as drive() — an unchanged demand leaves the in-flight
  // ramp alone instead of restarting its window every hold-resend.
  if (ramping_ && mapSpeedToDuty(speed) == targetDuty_) return;
  // U-141: smooth speed changes — only start a new ramp if the requested
  // speed differs significantly from the current applied duty (deadband ~5%)
  // to prevent jitter from tiny fluctuations in the app's speed value.
  uint32_t newDuty = mapSpeedToDuty(speed);
  if (ramping_) {
    // Use the old maxDuty from mapSpeedToDuty's scope; approximate with
    // full-scale duty = (1 << PWM_RESOLUTION) - 1. Compute 5% of that.
    uint32_t fullDuty = (1u << PWM_RESOLUTION) - 1u;
    uint32_t dutyDelta = labs(newDuty - targetDuty_);
    if (dutyDelta < (fullDuty / 20)) return;  // <5% change: let ramp complete
  }
  rampFromDuty_ = lastDuty_;
}
  targetDuty_ = newDuty;
  rampStartMs_ = millis();
  ramping_ = true;


#endif