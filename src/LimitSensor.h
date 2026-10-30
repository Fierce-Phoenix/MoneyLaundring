// LimitSensor.h — debounced wrapper around one IR endpoint sensor.

#pragma once

#include <Arduino.h>

class LimitSensor {
 public:
  // Configure `pin` as INPUT (no internal pull-up: these IR modules drive
  // OUT actively) and remember which digital level counts as "active"
  // (endpoint detected).
  void begin(uint8_t pin, int activeLevel);

  // Non-blocking debounce step. Call this every loop() iteration with the
  // current millis(). Samples digitalRead(pin) and, once the raw level has
  // been stable for DEBOUNCE_MS, updates the debounced state that active()
  // reports.
  void update(unsigned long nowMs);

  // Instantaneous digitalRead(pin), with no debounce — for diagnostics.
  int raw() const;

  // Debounced reading: true only when the last-settled level equals the
  // current active level. Reflects the state as of the most recent
  // update() call.
  bool active() const;

  // Change which digital level counts as "active" at runtime (used by the
  // console's `pol` command). Does not itself re-sample the pin; the next
  // update() call will re-evaluate against the new level.
  void setActiveLevel(int activeLevel);

 private:
  uint8_t _pin = 0;
  int _activeLevel = HIGH;

  int _lastRaw = -1;
  unsigned long _lastChangeMs = 0;
  bool _debounced = false;
};
