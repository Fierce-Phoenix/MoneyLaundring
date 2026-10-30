// Motor.h — thin wrapper around one L298N H-bridge channel.
//
// Uses the arduino-esp32 2.0.x LEDC API only: ledcSetup / ledcAttachPin /
// ledcWrite. Do NOT switch this to the 3.x ledcAttach(pin, freq, res)
// signature — platformio.ini pins espressif32@6.9.0, which bundles the
// 2.0.x arduino-esp32 core.

#pragma once

#include <Arduino.h>

enum class Direction {
  FORWARD,
  REVERSE,
};

class Motor {
 public:
  // Configure the direction pins as OUTPUT, attach the enable pin to the
  // given LEDC channel at freqHz/resBits, and leave the motor stopped.
  void begin(uint8_t enaPin, uint8_t in1Pin, uint8_t in2Pin,
              uint8_t pwmChannel, uint32_t freqHz, uint8_t resBits);

  // Set the direction pins for `dir`, then write `duty` (0..2^resBits - 1)
  // to the enable pin's PWM channel.
  void drive(Direction dir, uint8_t duty);

  // Drive both direction pins LOW and set PWM duty to 0.
  void stop();

 private:
  uint8_t _enaPin = 0;
  uint8_t _in1Pin = 0;
  uint8_t _in2Pin = 0;
  uint8_t _pwmChannel = 0;
};
