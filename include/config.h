// config.h — pin map and tunables for the MoneyLaundring gantry firmware.
//
// This file is generated FROM docs/wiring.md, which is the authoritative
// hardware contract. If the board is rewired, update docs/wiring.md first,
// then mirror the change here. Do not edit the pin numbers here without
// updating that doc too.

#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Axis X — motor (L298N channel A) and its two endpoint sensors
// ---------------------------------------------------------------------------

// L298N channel A drives the X-axis TT motor.
constexpr uint8_t X_ENA = 25;  // PWM speed, axis X (L298N ENA)
constexpr uint8_t X_IN1 = 26;  // direction pin A1, axis X (L298N IN1)
constexpr uint8_t X_IN2 = 27;  // direction pin A2, axis X (L298N IN2)

// X-axis endpoint sensors. GPIO34/35 are input-only ESP32 pins with no
// internal pull-up — that is correct here because these IR obstacle modules
// actively drive their OUT pin HIGH or LOW themselves (no pull-up needed).
constexpr uint8_t X_MIN = 34;  // axis X, "reverse" end — input-only, no internal pull-up
constexpr uint8_t X_MAX = 35;  // axis X, "forward" end — input-only, no internal pull-up

// ---------------------------------------------------------------------------
// Axis Y — motor (L298N channel B) and its two endpoint sensors
// ---------------------------------------------------------------------------

// L298N channel B drives the Y-axis TT motor.
constexpr uint8_t Y_ENB = 33;  // PWM speed, axis Y (L298N ENB)
constexpr uint8_t Y_IN3 = 32;  // direction pin B1, axis Y (L298N IN3)
constexpr uint8_t Y_IN4 = 13;  // direction pin B2, axis Y (L298N IN4)

// Y-axis endpoint sensors. GPIO36/39 are input-only ESP32 pins with no
// internal pull-up — that is correct here because these IR obstacle modules
// actively drive their OUT pin HIGH or LOW themselves (no pull-up needed).
constexpr uint8_t Y_MIN = 36;  // axis Y, "reverse" end — input-only, no internal pull-up
constexpr uint8_t Y_MAX = 39;  // axis Y, "forward" end — input-only, no internal pull-up

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------

// Default PWM duty (0-255) used by the Module 1 diagnostic jog. Module 2/3
// may expose per-axis speed over the serial console; this is just the
// bring-up default.
constexpr uint8_t DEFAULT_SPEED = 180;

// LEDC PWM frequency (Hz) and resolution (bits) for both motor channels.
constexpr uint32_t PWM_FREQ = 1000;
constexpr uint8_t PWM_RES = 8;

// Minimum time (ms) a sensor reading must be stable before LimitSensor
// reports it as the new debounced state.
constexpr unsigned long DEBOUNCE_MS = 20;

// Which digital level means "black-body endpoint detected" on the IR
// sensors' OUT pin. PROVISIONAL DEFAULT: IR obstacle modules vary between
// active-HIGH and active-LOW, and docs/wiring.md explicitly says this is
// "to be determined at bring-up" by reading the Module 1 diagnostic's
// per-sensor printout with a real black-body marker in front of each
// sensor. HIGH is a placeholder until that reading is taken; it is not
// confirmed against hardware yet.
constexpr int SENSOR_ACTIVE_LEVEL = HIGH;

// Minimum time (ms) an axis must keep running in its new direction after a
// reversal before the same endpoint sensor is allowed to trigger another
// reversal (prevents re-triggering on the same marker before the rack
// physically clears it). Not used by Module 1's diagnostic loop — the
// reversing state machine that consumes this constant is Module 2's
// `Axis` class. Declared here now so config.h matches the full tunable set
// the spec and docs/architecture.md call for.
constexpr unsigned long REVERSE_LOCKOUT_MS = 400;
