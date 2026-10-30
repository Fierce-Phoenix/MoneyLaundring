// main.cpp — Module 1 hardware bring-up.
//
// Purpose: a safe, serial-driven console that lets the operator find and
// verify the four sensor GPIOs, the sensor polarity, and each motor's
// direction on the real board BEFORE any reversing/bounce control logic
// exists. There is no bounce state machine here — that is Module 2's
// `Axis` class (see docs/architecture.md). There is no blocking delay()
// anywhere in loop(); all timing is done by comparing millis() against
// deadlines inside BringupConsole.
//
// On boot, everything is safe: both direction pins LOW, both PWM duties 0,
// and the console starts disarmed, so no motor can move until the operator
// types `arm`.

#include <Arduino.h>

#include "BringupConsole.h"
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

BringupConsole console;

}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);

  // Both motors begin stopped: direction pins LOW, PWM duty 0. Nothing
  // here can move anything before the operator arms the console.
  motorX.begin(X_ENA, X_IN1, X_IN2, X_PWM_CHANNEL, PWM_FREQ, PWM_RES);
  motorY.begin(Y_ENB, Y_IN3, Y_IN4, Y_PWM_CHANNEL, PWM_FREQ, PWM_RES);

  sensorXMin.begin(X_MIN, SENSOR_ACTIVE_LEVEL);
  sensorXMax.begin(X_MAX, SENSOR_ACTIVE_LEVEL);
  sensorYMin.begin(Y_MIN, SENSOR_ACTIVE_LEVEL);
  sensorYMax.begin(Y_MAX, SENSOR_ACTIVE_LEVEL);

  console.begin(motorX, motorY, sensorXMin, sensorXMax, sensorYMin, sensorYMax);
  console.printBanner();
}

void loop() {
  console.update(millis());
}
