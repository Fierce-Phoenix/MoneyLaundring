// LimitSensor.h — debounced wrapper around one IR endpoint sensor.

#pragma once

#include <Arduino.h>

class LimitSensor {
 public:
  // Configure `pin` as INPUT (no internal pull-up: these IR modules drive
  // OUT actively) and remember which digital level counts as "active"
  // (endpoint detected).
  void begin(uint8_t pin, int activeLevel);

  // Debounced reading: true only when the pin has read `activeLevel`
  // continuously for at least DEBOUNCE_MS. Call this every loop(); it
  // samples digitalRead(pin) internally and tracks how long the current
  // raw level has been stable.
  bool active();

  // Instantaneous digitalRead(pin), with no debounce — for diagnostics.
  int raw() const;

 private:
  uint8_t _pin = 0;
  int _activeLevel = HIGH;

  int _lastRaw = -1;
  unsigned long _lastChangeMs = 0;
  bool _debounced = false;
};
