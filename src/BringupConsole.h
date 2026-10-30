// BringupConsole.h — non-blocking serial command console for Module 1
// hardware bring-up.
//
// Purpose: let the operator find and verify the four sensor GPIOs, the
// sensor polarity, and each motor's direction on the real board, with the
// motors disabled until the operator explicitly arms them. This class
// implements no bounce/reversing logic at all — that is Module 2's `Axis`
// class (see docs/architecture.md).
//
// Every method that can run a motor goes through drive(), which clamps to
// MAX_DUTY (see Motor.cpp); every motor-moving command also checks the
// armed flag first and refuses to run otherwise. There is no command that
// bypasses either check.

#pragma once

#include <Arduino.h>

#include "LimitSensor.h"
#include "Motor.h"

class BringupConsole {
 public:
  // Wire up the motor and sensor objects this console will drive and
  // report on. Does not touch any hardware itself (Motor/LimitSensor
  // begin() must already have been called).
  void begin(Motor &motorX, Motor &motorY, LimitSensor &xMin,
             LimitSensor &xMax, LimitSensor &yMin, LimitSensor &yMax);

  // Print the startup banner: pin map, tunables, and the "unconfirmed,
  // check before arming" warning. Call once from setup(), after begin().
  void printBanner();

  // Call every loop() iteration with the current millis(). Non-blocking:
  // reads any available serial input, advances the jog/scan state
  // machines, checks the arm timeout, and prints the periodic sensor
  // monitor line. Never calls delay().
  void update(unsigned long nowMs);

 private:
  enum class JogAxis { NONE, X, Y };
  enum class ScanPhase { IDLE, BASELINE, WINDOW };

  Motor *_motorX = nullptr;
  Motor *_motorY = nullptr;
  LimitSensor *_xMin = nullptr;
  LimitSensor *_xMax = nullptr;
  LimitSensor *_yMin = nullptr;
  LimitSensor *_yMax = nullptr;

  bool _armed = false;
  unsigned long _lastCommandMs = 0;

  bool _monitoring = true;
  unsigned long _lastMonitorMs = 0;

  int _activeLevel = HIGH;  // current sensor polarity, flipped by `pol`

  uint8_t _jogDuty = 0;  // set from DEFAULT_SPEED in begin(), clamp-checked by `speed`
  JogAxis _jogAxis = JogAxis::NONE;
  Direction _jogDir = Direction::FORWARD;
  unsigned long _jogStartMs = 0;

  static constexpr size_t SCAN_MAX_PINS = 20;
  ScanPhase _scanPhase = ScanPhase::IDLE;
  unsigned long _scanPhaseStartMs = 0;
  uint8_t _scanPins[SCAN_MAX_PINS] = {0};
  int _scanLastLevel[SCAN_MAX_PINS] = {0};
  size_t _scanPinCount = 0;

  String _inputBuffer;

  void handleLine(String line, unsigned long nowMs);

  void cmdHelp();
  void cmdMap();
  void cmdMon();
  void cmdPol();
  void cmdScanStart(unsigned long nowMs);
  void cmdArm(unsigned long nowMs);
  void cmdDisarm(const char *reason);
  void cmdStop();
  void cmdSpeed(const String &arg);
  void cmdJog(JogAxis axis, Direction dir, const char *label, unsigned long nowMs);

  void updateArmTimeout(unsigned long nowMs);
  void updateJog(unsigned long nowMs);
  void updateScan(unsigned long nowMs);
  void updateMonitor(unsigned long nowMs);

  void printPinMapAndTunables();
  void printSensorLine();
  void applyActiveLevelToSensors();
  Motor *motorFor(JogAxis axis);
};
