// config.h — pin map and tunables for the MoneyLaundring gantry firmware.
//
// This is Module 1 (hardware bring-up). Every pin number below is an
// UNCONFIRMED DEFAULT: the exact GPIO each wire lands on could not be read
// from the build photos. Do not treat any of this as hardware-verified.
// Check it against the physical board (see docs/wiring.md and the `scan`
// console command) before trusting it, and especially before typing `arm`.
//
// This file is the single source of named constants for pins and tunables.
// Nothing in src/ should hardcode a pin number or tunable value directly —
// everything should reference a constant from here.

#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Axis X — motor (L298N channel A) and its two endpoint sensors
// ---------------------------------------------------------------------------

// L298N channel A drives the X-axis TT motor.
constexpr uint8_t X_ENA = 25;  // PWM speed, axis X (L298N ENA)
constexpr uint8_t X_IN1 = 26;  // direction pin, axis X (L298N IN1)
constexpr uint8_t X_IN2 = 27;  // direction pin, axis X (L298N IN2)

// X-axis endpoint sensors. GPIO34/35 are input-only ESP32 pins with no
// internal pull-up. That is fine here because the IR obstacle modules drive
// their OUT pin actively (no pull-up needed).
constexpr uint8_t X_MIN = 34;  // axis X, one end
constexpr uint8_t X_MAX = 35;  // axis X, other end

// ---------------------------------------------------------------------------
// Axis Y — motor (L298N channel B) and its two endpoint sensors
// ---------------------------------------------------------------------------

// L298N channel B drives the Y-axis TT motor.
constexpr uint8_t Y_ENB = 33;  // PWM speed, axis Y (L298N ENB)
constexpr uint8_t Y_IN3 = 32;  // direction pin, axis Y (L298N IN3)
constexpr uint8_t Y_IN4 = 13;  // direction pin, axis Y (L298N IN4)

// Y-axis endpoint sensors. GPIO36/39 are input-only ESP32 pins with no
// internal pull-up. That is fine here because the IR obstacle modules drive
// their OUT pin actively (no pull-up needed).
constexpr uint8_t Y_MIN = 36;  // axis Y, one end
constexpr uint8_t Y_MAX = 39;  // axis Y, other end

// ---------------------------------------------------------------------------
// PWM (LEDC) configuration
// ---------------------------------------------------------------------------

constexpr uint32_t PWM_FREQ = 5000;  // Hz, both motor channels
constexpr uint8_t PWM_RES = 8;       // bits -> duty range 0..255

constexpr uint8_t X_PWM_CHANNEL = 0;  // LEDC channel for axis X (ENA)
constexpr uint8_t Y_PWM_CHANNEL = 1;  // LEDC channel for axis Y (ENB)

// ---------------------------------------------------------------------------
// Serial console
// ---------------------------------------------------------------------------

constexpr unsigned long SERIAL_BAUD = 115200;

// ---------------------------------------------------------------------------
// Sensor debounce and polarity
// ---------------------------------------------------------------------------

// Minimum time (ms) a raw sensor reading must be stable before LimitSensor
// reports it as the new debounced level.
constexpr unsigned long DEBOUNCE_MS = 20;

// Which digital level means "black-body endpoint detected" on an IR
// sensor's OUT pin. PROVISIONAL: an LM393 obstacle module idles HIGH and
// pulls LOW over a reflective surface, so reading HIGH on black is only a
// starting guess, not a confirmed value. Use the `mon` and `pol` console
// commands at bring-up to find the real polarity on each sensor before
// relying on this.
constexpr int SENSOR_ACTIVE_LEVEL = HIGH;

// ---------------------------------------------------------------------------
// Bring-up console timing
// ---------------------------------------------------------------------------

constexpr unsigned long JOG_MS = 400;              // duration of one xf/xr/yf/yr pulse
constexpr unsigned long MONITOR_PERIOD_MS = 250;   // sensor status line period
constexpr unsigned long ARM_TIMEOUT_MS = 60000;    // auto-disarm after this long idle
constexpr unsigned long SCAN_WINDOW_MS = 15000;    // `scan` command observation window

// ---------------------------------------------------------------------------
// Motor duty limit
// ---------------------------------------------------------------------------

// The L298N is fed from a 12V supply, but the TT motors are rated for
// 3-6V. Full PWM duty at 12V would overdrive them, so every commanded duty
// is clamped to MAX_DUTY, which approximates the duty cycle that puts the
// motor's rated voltage across it (the L298N drops roughly 2V across its
// internal transistors regardless of input voltage).
constexpr double SUPPLY_VOLTS = 12.0;
constexpr double MOTOR_RATED_VOLTS = 6.0;

constexpr uint8_t MAX_DUTY =
    (uint8_t)(255.0 * MOTOR_RATED_VOLTS / (SUPPLY_VOLTS - 2.0) + 0.5);

// Default jog duty for the bring-up console's xf/xr/yf/yr commands. Always
// clamped to MAX_DUTY (and is, by construction, below it).
constexpr uint8_t DEFAULT_SPEED = 110;
