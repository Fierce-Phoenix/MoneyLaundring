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
constexpr unsigned long SCAN_BASELINE_MS = 1000;   // `scan` command baseline-capture duration, before SCAN_WINDOW_MS begins

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

// ---------------------------------------------------------------------------
// Axis direction mapping (Module 2) — PROVISIONAL
// ---------------------------------------------------------------------------

// Whether driving a motor FORWARD moves its axis toward the MAX-end sensor.
// PROVISIONAL: which end FORWARD drives toward depends on motor wiring
// polarity, which has not been observed on the real board. During bring-up,
// use `xf`/`yf` together with `mon` to see which sensor activates, then set
// these to match before ever using `run`. If an autonomous run faults with
// OPPOSITE_LIMIT_HIT, that is the signal to flip the corresponding constant.
constexpr bool X_FORWARD_IS_TOWARD_MAX = true;  // provisional
constexpr bool Y_FORWARD_IS_TOWARD_MAX = true;  // provisional

// ---------------------------------------------------------------------------
// Axis bounce logic tunables (Module 2)
// ---------------------------------------------------------------------------

// Minimum time (ms) that must pass between reversals (rejects chatter if a
// sensor flickers near the endpoint). Also the first half of the window,
// starting at a reversal, during which Axis suppresses its BOTH_LIMITS_ACTIVE
// and OPPOSITE_LIMIT_HIT fault checks (see docs/architecture.md and Axis.cpp).
constexpr uint32_t REVERSE_LOCKOUT_MS = 400;

// Time (ms) the motor is held stopped after a reversal before running in the
// new heading, so the H-bridge and rack gear never see an instant direction
// change. Also the second half of the fault-check-suppression window above.
constexpr uint32_t REVERSE_PAUSE_MS = 150;

// Maximum time (ms) one travel segment (from start() or from a reversal) may
// run before Axis reports TRAVEL_TIMEOUT (no endpoint sensor was ever seen).
// PROVISIONAL: a low run speed slows travel and could false-trip this if the
// configured speed turns out to be much lower than assumed here; revisit
// once real travel times are observed on the physical gantry.
constexpr uint32_t MAX_TRAVEL_MS = 20000;

// Maximum time (ms) the limit sensor behind the current heading may stay
// continuously active while the axis is otherwise allowed to run before
// Axis reports STUCK_AT_LIMIT (Module 3). PROVISIONAL: this is meant to be
// much shorter than MAX_TRAVEL_MS, since it should only ever need to cover
// the brief moment right at a reversal, not real travel time — if a
// legitimate reversal pattern ever takes this long to clear the behind
// sensor on the real hardware, this value should be revisited.
constexpr uint32_t MAX_CLEAR_MS = 3000;

// Default autonomous-run speed per axis (duty 0-255). Both start equal to
// DEFAULT_SPEED; adjustable at runtime via the console's `sx`/`sy` commands,
// always clamped to MAX_DUTY by GantryController.
constexpr uint8_t RUN_SPEED_X = DEFAULT_SPEED;
constexpr uint8_t RUN_SPEED_Y = DEFAULT_SPEED;

// ---------------------------------------------------------------------------
// Run telemetry (Module 4) — observation only, never affects motor commands
// ---------------------------------------------------------------------------

// When true, GantryController prints a line on every reversal (time since
// the last reversal/start, including the post-reversal pause, and the
// running reversal count) and a line when the carriage clears the limit
// sensor it just reversed off of (time from when the axis was first
// allowed to run again after the reversal to the moment that behind-limit
// sensor goes inactive). It also tracks the longest segment time and the
// longest clear time seen since the last `run`, shown by `st`, reset at
// the start of every run. Purely observational: nothing in this feature
// ever changes a motor command.
constexpr bool RUN_TELEMETRY = true;

// ---------------------------------------------------------------------------
// Auto-run on boot (Module 4) — defaults OFF
// ---------------------------------------------------------------------------

// When true, BringupConsole::begin() schedules an automatic arm+run after
// AUTO_RUN_DELAY_MS, but ONLY if the ESP32's reset reason (esp_reset_reason())
// is ESP_RST_POWERON (a genuine cold power-on, not a software reset, panic,
// watchdog reset, brownout, or wake from deep sleep). This is meant for the
// finished, standalone-power operator mode (see docs/hardware-test.md),
// never for bring-up or bench testing — hence the default of false. Any
// byte received from serial during the countdown cancels it (and is
// discarded, not treated as a command). On expiry, it calls exactly the
// same internal arm/run code paths the typed `arm`/`run` commands use;
// there is no separate auto-run motion path. If a fault occurs, the
// console's existing fault-disarm behavior applies as normal and auto-run
// never retries — a fault always requires a manual `arm`/`run` afterward.
constexpr bool AUTO_RUN_ON_BOOT = false;

// Countdown (ms) between setup() calling BringupConsole::begin() and the
// scheduled auto-run actually firing, during which any received serial
// byte cancels it.
constexpr unsigned long AUTO_RUN_DELAY_MS = 10000;
