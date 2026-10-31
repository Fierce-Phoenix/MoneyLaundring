// GantryController.h — hardware glue between the pure Axis bounce logic
// and the Motor/LimitSensor hardware wrappers.
//
// Owns two Axis objects (one per axis) and holds references to the two
// Motor objects and four LimitSensor objects. Every loop, it feeds each
// Axis the debounced active() state of its two limit sensors, maps the
// resulting AxisDir to a Motor::FORWARD/REVERSE command using the
// X_FORWARD_IS_TOWARD_MAX / Y_FORWARD_IS_TOWARD_MAX mapping constants from
// config.h, and only calls Motor::drive()/stop() when the commanded run
// state, direction, or duty actually changed since the previous tick.
//
// This class does not know about BringupConsole or the armed/disarmed
// concept at all — it is driven purely by startRun()/haltRun() and does
// not depend on the console in any direction. BringupConsole holds a
// reference to a GantryController and drives it; GantryController never
// reaches back into BringupConsole.

#pragma once

#include <Arduino.h>

#include "Axis.h"
#include "LimitSensor.h"
#include "Motor.h"

class GantryController {
 public:
  // Wire up the motor/sensor objects and build each Axis's AxisConfig
  // from the config.h tunables. Does not start running.
  void begin(Motor &motorX, Motor &motorY, LimitSensor &xMin, LimitSensor &xMax,
             LimitSensor &yMin, LimitSensor &yMax);

  // Begin autonomous bouncing on both axes from nowMs. Both axes start
  // heading TOWARD_MAX; if an endpoint sensor is already active there,
  // the normal reversal rule (not a fault) handles it on the first tick.
  void startRun(uint32_t nowMs);

  // Stop both motors immediately and halt both axes (back to IDLE).
  // Safe to call even if not currently running.
  void haltRun();

  bool isRunning() const { return _running; }

  // Set the per-axis run duty (0-255), clamped to MAX_DUTY with a printed
  // notice if the requested value was higher. Takes effect on the next
  // update() tick.
  void setSpeedX(uint8_t duty);
  void setSpeedY(uint8_t duty);

  // Print per-axis status: state, heading, speed, reversal count, fault.
  void printStatus();

  // Returns true exactly once per fault (latched by update() when a fault
  // is detected), then clears the latch. The console polls this once per
  // loop and must disarm when it reads true.
  bool takeFaultFlag();

  // Invalidate the per-motor command cache (Module 3), so the next
  // update() call is guaranteed to issue a fresh Motor::drive()/stop()
  // call regardless of what was last commanded, even if the new command
  // happens to be identical. startRun(), haltRun(), and the fault
  // shutdown path already invalidate the cache themselves; this method
  // exists for the one remaining case: anything outside this class's
  // knowledge driving the motors directly, which today is only
  // BringupConsole's xf/xr/yf/yr jog commands. The console calls this
  // both when a jog starts and when it ends, so a stale cache entry can
  // never cause the first command of the next run to be silently skipped.
  void invalidateCommandCache();

  // Call once per loop() iteration, after both LimitSensor::update() have
  // already been called for this tick. No-op if not running (beyond
  // ensuring motors stay stopped, which haltRun()/fault handling already
  // guarantee without needing a repeated stop() every idle tick).
  void update(uint32_t nowMs);

 private:
  // Per-motor "last commanded" state, so update() only issues a new
  // Motor::drive()/stop() call when something about the command changed.
  struct MotorCommandState {
    bool hasCommand = false;
    bool running = false;
    Direction dir = Direction::FORWARD;
    uint8_t duty = 0;
  };

  Motor *_motorX = nullptr;
  Motor *_motorY = nullptr;
  LimitSensor *_xMin = nullptr;
  LimitSensor *_xMax = nullptr;
  LimitSensor *_yMin = nullptr;
  LimitSensor *_yMax = nullptr;

  Axis _axisX;
  Axis _axisY;

  bool _running = false;
  bool _faultLatched = false;

  uint8_t _speedX = 0;
  uint8_t _speedY = 0;

  MotorCommandState _cmdX;
  MotorCommandState _cmdY;

  void applyAxisCommand(Motor &motor, const AxisOutput &out, uint8_t speed,
                         bool forwardIsTowardMax, MotorCommandState &cmd);
  void printFaultLine(const char *axisName, AxisFault fault);
  void printAxisStatusLine(const char *axisName, const Axis &axis, uint8_t speed);
};
