#include "LimitSensor.h"

#include "config.h"

void LimitSensor::begin(uint8_t pin, int activeLevel) {
  _pin = pin;
  _activeLevel = activeLevel;
  pinMode(_pin, INPUT);

  int level = digitalRead(_pin);
  _lastRaw = level;
  _lastChangeMs = millis();
  _debounced = (level == _activeLevel);
}

void LimitSensor::update(unsigned long nowMs) {
  int level = digitalRead(_pin);

  if (level != _lastRaw) {
    _lastRaw = level;
    _lastChangeMs = nowMs;
  } else if (nowMs - _lastChangeMs >= DEBOUNCE_MS) {
    _debounced = (level == _activeLevel);
  }
}

int LimitSensor::raw() const {
  return digitalRead(_pin);
}

bool LimitSensor::active() const {
  return _debounced;
}

void LimitSensor::setActiveLevel(int activeLevel) {
  _activeLevel = activeLevel;
  // Re-evaluate immediately against the last-settled raw level so a
  // runtime polarity flip (the `pol` console command) is reflected by
  // active() right away, rather than waiting for the next debounce window.
  _debounced = (_lastRaw == _activeLevel);
}
