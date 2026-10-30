// main.cpp — Module 1 bring-up diagnostic.
//
// Purpose: let the operator confirm the pin map, motor direction, and IR
// sensor polarity on the real board BEFORE any reversing/bounce logic is
// written. This file deliberately does NOT implement the bounce state
// machine (that is Module 2's `Axis` class) and does NOT add a serial
// command console (that is Module 3's `Console`).
//
// loop() is fully non-blocking (no delay() calls at all): a small timed
// state machine drives the jog sequence by comparing millis() against
// JOG_PULSE_MS deadlines, so the continuous ~200ms sensor printout keeps
// running even while a jog pulse is in progress.
//
// What it does, on independent timed cycles in loop():
//   1. Every JOG_CYCLE_MS it jogs axis X FORWARD for ~JOG_PULSE_MS, stops,
//      jogs X REVERSE for ~JOG_PULSE_MS, stops, then does the same for
//      axis Y — printing which axis/direction is being driven so the
//      operator can watch the gantry and confirm FORWARD actually moves
//      the intended way.
//   2. Every SENSOR_PRINT_MS it prints one line with all four sensors' raw
//      level (0/1) and their active-interpretation under the current
//      SENSOR_ACTIVE_LEVEL, so the operator can cover each endpoint with a
//      black-body marker and read off the true polarity.

#include <Arduino.h>

#include "config.h"
#include "LimitSensor.h"
#include "Motor.h"

namespace {

constexpr uint8_t X_PWM_CHANNEL = 0;
constexpr uint8_t Y_PWM_CHANNEL = 1;

// Duration (ms) of each drive pulse and each stop pause between pulses.
constexpr unsigned long JOG_PULSE_MS = 600;

// Time (ms) from the start of one full jog sequence to the start of the
// next. The sequence itself (4 drive pulses + 4 stop pauses) takes
// 8 * JOG_PULSE_MS = 4800ms, so this leaves a visible gap before it repeats.
constexpr unsigned long JOG_CYCLE_MS = 6000;

// How often (ms) the continuous sensor status line is printed.
constexpr unsigned long SENSOR_PRINT_MS = 200;

Motor motorX;
Motor motorY;

LimitSensor sensorXMin;
LimitSensor sensorXMax;
LimitSensor sensorYMin;
LimitSensor sensorYMax;

// Jog sequence state machine. Each step drives one motor in one direction
// for JOG_PULSE_MS, then stops it for JOG_PULSE_MS, in this fixed order:
//   X FORWARD -> X stop -> X REVERSE -> X stop ->
//   Y FORWARD -> Y stop -> Y REVERSE -> Y stop -> (wait for next cycle)
enum class JogStep {
  X_FORWARD_RUN,
  X_FORWARD_STOP,
  X_REVERSE_RUN,
  X_REVERSE_STOP,
  Y_FORWARD_RUN,
  Y_FORWARD_STOP,
  Y_REVERSE_RUN,
  Y_REVERSE_STOP,
  WAITING,
};

JogStep jogStep = JogStep::WAITING;
unsigned long jogStepStartMs = 0;
unsigned long lastSensorPrintMs = 0;

void announceJog(const char *axisName, const char *dirName) {
  Serial.print("[jog] axis ");
  Serial.print(axisName);
  Serial.print(" ");
  Serial.print(dirName);
  Serial.print(" @ duty ");
  Serial.println(DEFAULT_SPEED);
}

// Advance the jog state machine. Called every loop() iteration; only acts
// when enough time has passed for the current step, so it never blocks.
void updateJogSequence(unsigned long now) {
  unsigned long elapsed = now - jogStepStartMs;

  switch (jogStep) {
    case JogStep::WAITING:
      if (elapsed >= JOG_CYCLE_MS) {
        announceJog("X", "FORWARD");
        motorX.drive(Direction::FORWARD, DEFAULT_SPEED);
        jogStep = JogStep::X_FORWARD_RUN;
        jogStepStartMs = now;
      }
      break;

    case JogStep::X_FORWARD_RUN:
      if (elapsed >= JOG_PULSE_MS) {
        motorX.stop();
        jogStep = JogStep::X_FORWARD_STOP;
        jogStepStartMs = now;
      }
      break;

    case JogStep::X_FORWARD_STOP:
      if (elapsed >= JOG_PULSE_MS) {
        announceJog("X", "REVERSE");
        motorX.drive(Direction::REVERSE, DEFAULT_SPEED);
        jogStep = JogStep::X_REVERSE_RUN;
        jogStepStartMs = now;
      }
      break;

    case JogStep::X_REVERSE_RUN:
      if (elapsed >= JOG_PULSE_MS) {
        motorX.stop();
        jogStep = JogStep::X_REVERSE_STOP;
        jogStepStartMs = now;
      }
      break;

    case JogStep::X_REVERSE_STOP:
      if (elapsed >= JOG_PULSE_MS) {
        announceJog("Y", "FORWARD");
        motorY.drive(Direction::FORWARD, DEFAULT_SPEED);
        jogStep = JogStep::Y_FORWARD_RUN;
        jogStepStartMs = now;
      }
      break;

    case JogStep::Y_FORWARD_RUN:
      if (elapsed >= JOG_PULSE_MS) {
        motorY.stop();
        jogStep = JogStep::Y_FORWARD_STOP;
        jogStepStartMs = now;
      }
      break;

    case JogStep::Y_FORWARD_STOP:
      if (elapsed >= JOG_PULSE_MS) {
        announceJog("Y", "REVERSE");
        motorY.drive(Direction::REVERSE, DEFAULT_SPEED);
        jogStep = JogStep::Y_REVERSE_RUN;
        jogStepStartMs = now;
      }
      break;

    case JogStep::Y_REVERSE_RUN:
      if (elapsed >= JOG_PULSE_MS) {
        motorY.stop();
        jogStep = JogStep::Y_REVERSE_STOP;
        jogStepStartMs = now;
      }
      break;

    case JogStep::Y_REVERSE_STOP:
      if (elapsed >= JOG_PULSE_MS) {
        jogStep = JogStep::WAITING;
        jogStepStartMs = now;
      }
      break;
  }
}

void printSensorLine() {
  Serial.print("[sensors] X_MIN raw=");
  Serial.print(sensorXMin.raw());
  Serial.print(" active=");
  Serial.print(sensorXMin.active());

  Serial.print("  X_MAX raw=");
  Serial.print(sensorXMax.raw());
  Serial.print(" active=");
  Serial.print(sensorXMax.active());

  Serial.print("  Y_MIN raw=");
  Serial.print(sensorYMin.raw());
  Serial.print(" active=");
  Serial.print(sensorYMin.active());

  Serial.print("  Y_MAX raw=");
  Serial.print(sensorYMax.raw());
  Serial.print(" active=");
  Serial.println(sensorYMax.active());
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);  // give the USB-serial bridge a moment to come up; not loop()

  Serial.println();
  Serial.println("==================================================");
  Serial.println(" MoneyLaundring - Module 1 bring-up diagnostic");
  Serial.println(" Confirm pin map, motor direction, sensor polarity.");
  Serial.println(" No reversing logic in this build.");
  Serial.println("==================================================");

  motorX.begin(X_ENA, X_IN1, X_IN2, X_PWM_CHANNEL, PWM_FREQ, PWM_RES);
  motorY.begin(Y_ENB, Y_IN3, Y_IN4, Y_PWM_CHANNEL, PWM_FREQ, PWM_RES);

  sensorXMin.begin(X_MIN, SENSOR_ACTIVE_LEVEL);
  sensorXMax.begin(X_MAX, SENSOR_ACTIVE_LEVEL);
  sensorYMin.begin(Y_MIN, SENSOR_ACTIVE_LEVEL);
  sensorYMax.begin(Y_MAX, SENSOR_ACTIVE_LEVEL);

  Serial.println("Pin map:");
  Serial.printf("  X: ENA=%d IN1=%d IN2=%d  MIN=%d MAX=%d\n", X_ENA, X_IN1,
                X_IN2, X_MIN, X_MAX);
  Serial.printf("  Y: ENB=%d IN3=%d IN4=%d  MIN=%d MAX=%d\n", Y_ENB, Y_IN3,
                Y_IN4, Y_MIN, Y_MAX);
  Serial.printf("  SENSOR_ACTIVE_LEVEL=%s  DEFAULT_SPEED=%d\n",
                SENSOR_ACTIVE_LEVEL == HIGH ? "HIGH" : "LOW", DEFAULT_SPEED);
  Serial.println();

  unsigned long now = millis();
  jogStep = JogStep::WAITING;
  jogStepStartMs = now;
  lastSensorPrintMs = now;
}

void loop() {
  unsigned long now = millis();

  updateJogSequence(now);

  if (now - lastSensorPrintMs >= SENSOR_PRINT_MS) {
    lastSensorPrintMs = now;
    printSensorLine();
  }
}
