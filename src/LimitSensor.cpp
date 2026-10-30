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

bool LimitSensor::active() {
  int level = digitalRead(_pin);
  unsigned long now = millis();

  if (level != _lastRaw) {
    _lastRaw = level;
    _lastChangeMs = now;
  } else if (now - _lastChangeMs >= DEBOUNCE_MS) {
    _debounced = (level == _activeLevel);
  }

  return _debounced;
}

int LimitSensor::raw() const {
  return digitalRead(_pin);
}
