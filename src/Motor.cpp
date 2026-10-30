#include "Motor.h"

void Motor::begin(uint8_t enaPin, uint8_t in1Pin, uint8_t in2Pin,
                   uint8_t pwmChannel, uint32_t freqHz, uint8_t resBits) {
  _enaPin = enaPin;
  _in1Pin = in1Pin;
  _in2Pin = in2Pin;
  _pwmChannel = pwmChannel;

  pinMode(_in1Pin, OUTPUT);
  pinMode(_in2Pin, OUTPUT);
  digitalWrite(_in1Pin, LOW);
  digitalWrite(_in2Pin, LOW);

  // arduino-esp32 2.0.x LEDC API: configure the channel, then attach the
  // enable pin to it. Do not use the 3.x ledcAttach(pin, freq, res) call.
  ledcSetup(_pwmChannel, freqHz, resBits);
  ledcAttachPin(_enaPin, _pwmChannel);
  ledcWrite(_pwmChannel, 0);
}

void Motor::drive(Direction dir, uint8_t duty) {
  if (dir == Direction::FORWARD) {
    digitalWrite(_in1Pin, HIGH);
    digitalWrite(_in2Pin, LOW);
  } else {
    digitalWrite(_in1Pin, LOW);
    digitalWrite(_in2Pin, HIGH);
  }
  ledcWrite(_pwmChannel, duty);
}

void Motor::stop() {
  digitalWrite(_in1Pin, LOW);
  digitalWrite(_in2Pin, LOW);
  ledcWrite(_pwmChannel, 0);
}
