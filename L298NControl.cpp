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
  return ((s - MIN_SPEED) * maxDuty) / (MAX_SPEED - MIN_SPEED);
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

  return true;
}

void L298NControl::drive(Action action, uint16_t speed) {
  crashRtcMark("motor");
  uint32_t duty = mapSpeedToDuty(speed);

  switch (action) {
    case Action::FORWARD:
      digitalWrite(PIN_IN1, LOW); digitalWrite(PIN_IN2, HIGH);
      digitalWrite(PIN_IN3, LOW); digitalWrite(PIN_IN4, HIGH);
      ledc_set_duty(MODE, CH_A, duty); ledc_update_duty(MODE, CH_A);
      ledc_set_duty(MODE, CH_B, duty); ledc_update_duty(MODE, CH_B);
      break;

    case Action::BACKWARD:
      digitalWrite(PIN_IN1, HIGH); digitalWrite(PIN_IN2, LOW);
      digitalWrite(PIN_IN3, HIGH); digitalWrite(PIN_IN4, LOW);
      ledc_set_duty(MODE, CH_A, duty); ledc_update_duty(MODE, CH_A);
      ledc_set_duty(MODE, CH_B, duty); ledc_update_duty(MODE, CH_B);
      break;

    case Action::LEFT:
      // Pivot left: left motor backward, right motor forward
      digitalWrite(PIN_IN1, HIGH); digitalWrite(PIN_IN2, LOW);
      digitalWrite(PIN_IN3, LOW);  digitalWrite(PIN_IN4, HIGH);
      ledc_set_duty(MODE, CH_A, duty); ledc_update_duty(MODE, CH_A);
      ledc_set_duty(MODE, CH_B, duty); ledc_update_duty(MODE, CH_B);
      break;

    case Action::RIGHT:
      // Pivot right: left motor forward, right motor backward
      digitalWrite(PIN_IN1, LOW);  digitalWrite(PIN_IN2, HIGH);
      digitalWrite(PIN_IN3, HIGH); digitalWrite(PIN_IN4, LOW);
      ledc_set_duty(MODE, CH_A, duty); ledc_update_duty(MODE, CH_A);
      ledc_set_duty(MODE, CH_B, duty); ledc_update_duty(MODE, CH_B);
      break;

    case Action::STOP:
    default:
      digitalWrite(PIN_IN1, LOW); digitalWrite(PIN_IN2, LOW);
      digitalWrite(PIN_IN3, LOW); digitalWrite(PIN_IN4, LOW);
      ledc_set_duty(MODE, CH_A, 0); ledc_update_duty(MODE, CH_A);
      ledc_set_duty(MODE, CH_B, 0); ledc_update_duty(MODE, CH_B);
      break;
  }
}

void L298NControl::setSpeed(uint16_t speed) {
  uint32_t duty = mapSpeedToDuty(speed);
  ledc_set_duty(MODE, CH_A, duty);
  ledc_update_duty(MODE, CH_A);
  ledc_set_duty(MODE, CH_B, duty);
  ledc_update_duty(MODE, CH_B);
}

#endif