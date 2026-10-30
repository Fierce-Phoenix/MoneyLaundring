// main.cpp — Module 1 hardware bring-up console, extended in Module 2 with
// autonomous axis bounce ("run mode").
//
// Purpose: a safe, serial-driven console that lets the operator find and
// verify the four sensor GPIOs, the sensor polarity, and each motor's
// direction on the real board, PLUS (new in Module 2) run both axes
// autonomously once armed, each bouncing between its two endpoint sensors
// with the pure `Axis` decision logic (see src/Axis.h) driving the
// hardware through `GantryController` (see src/GantryController.h). There
// is no blocking delay() anywhere in loop(); all timing is done by
// comparing millis() against deadlines inside BringupConsole/Axis.
//
// On boot, everything is safe: both direction pins LOW, both PWM duties 0,
// and the console starts disarmed, so no motor can move until the operator
// types `arm`, and autonomous running additionally requires `run`.
//
// loop() ordering (fixed, see docs/architecture.md): every LimitSensor is
// updated exactly once, then GantryController::update() (which reads the
// now-current debounced sensor states and may command the motors), then
// BringupConsole::update() (which reads commands and the gantry's fault
// flag). Nothing updates a sensor a second time in the same tick.

#include <Arduino.h>

#include "BringupConsole.h"
#include "GantryController.h"
#include "LimitSensor.h"
#include "Motor.h"
#include "config.h"

namespace {

Motor motorX;
Motor motorY;

LimitSensor sensorXMin;
LimitSensor sensorXMax;
LimitSensor sensorYMin;
LimitSensor sensorYMax;

GantryController gantry;
BringupConsole console;

}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);

  // Both motors begin stopped: direction pins LOW, PWM duty 0. Nothing
  // here can move anything before the operator arms the console, and
  // nothing auto-starts a run on boot.
  motorX.begin(X_ENA, X_IN1, X_IN2, X_PWM_CHANNEL, PWM_FREQ, PWM_RES);
  motorY.begin(Y_ENB, Y_IN3, Y_IN4, Y_PWM_CHANNEL, PWM_FREQ, PWM_RES);

  sensorXMin.begin(X_MIN, SENSOR_ACTIVE_LEVEL);
  sensorXMax.begin(X_MAX, SENSOR_ACTIVE_LEVEL);
  sensorYMin.begin(Y_MIN, SENSOR_ACTIVE_LEVEL);
  sensorYMax.begin(Y_MAX, SENSOR_ACTIVE_LEVEL);

  gantry.begin(motorX, motorY, sensorXMin, sensorXMax, sensorYMin, sensorYMax);

  console.begin(motorX, motorY, sensorXMin, sensorXMax, sensorYMin, sensorYMax,
                gantry);
  console.printBanner();
}

void loop() {
  unsigned long now = millis();

  // Each sensor is updated exactly once per loop, before anything reads
  // its debounced state this tick.
  sensorXMin.update(now);
  sensorXMax.update(now);
  sensorYMin.update(now);
  sensorYMax.update(now);

  // GantryController reads the now-current sensor states and may command
  // the motors (autonomous bounce). No-op if not running.
  gantry.update(now);

  // BringupConsole reads serial input, advances its own jog/scan/monitor
  // state machines, and polls the gantry's fault flag.
  console.update(now);
}
