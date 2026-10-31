// Axis.h — hardware-independent bounce-logic decision engine for one axis.
//
// This class is intentionally pure: no Arduino include, no config.h
// include, no Serial, no millis(). It compiles under a plain host g++ so
// it can be unit-tested natively (see test/test_axis/test_axis.cpp) without
// an ESP32 or any physical hardware. All time values are caller-supplied
// uint32_t "now" timestamps (the caller is expected to pass millis() on
// target, or a simulated clock in tests); all time arithmetic here uses
// unsigned subtraction so behaviour is correct across a 32-bit timestamp
// rollover (nowMs wrapping past 0xFFFFFFFF back to 0).
//
// Axis does not touch any motor or sensor. GantryController (hardware
// glue, see src/GantryController.h) feeds it debounced limit-sensor states
// each tick and maps its AxisOutput to a Motor::drive()/stop() call.

#pragma once

#include <cstdint>

// Which end of the axis the carriage is currently heading toward.
enum class AxisDir {
  TOWARD_MIN,
  TOWARD_MAX,
};

// Lifecycle state of the axis's bounce logic.
enum class AxisState {
  IDLE,     // not running; update() always returns run=false
  RUNNING,  // actively bouncing between the two ends
  FAULT,    // a safety fault was detected; sticky until start() is called
};

// Which safety condition caused a FAULT, if any.
enum class AxisFault {
  NONE,
  TRAVEL_TIMEOUT,       // no endpoint sensor seen within maxTravelMs
  BOTH_LIMITS_ACTIVE,   // both limit sensors read active simultaneously
  OPPOSITE_LIMIT_HIT,   // the limit behind the current heading went active
  STUCK_AT_LIMIT,       // the behind limit stayed active for clearMs while
                        // the axis was otherwise allowed to run (the
                        // carriage never left the end it started/reversed
                        // at — classically, inverted motor wiring with the
                        // carriage already resting at that end, so
                        // OPPOSITE_LIMIT_HIT's rising-edge check can never
                        // fire)
};

// Tunables, supplied by the caller via begin(). Axis has no compiled-in
// defaults of its own; config.h's REVERSE_LOCKOUT_MS / REVERSE_PAUSE_MS /
// MAX_TRAVEL_MS / MAX_CLEAR_MS feed this struct on the hardware side (see
// GantryController::begin()).
struct AxisConfig {
  uint32_t lockoutMs;        // minimum time between reversals
  uint32_t reversePauseMs;   // time held stopped after a reversal
  uint32_t maxTravelMs;      // max time one travel segment may run
  uint32_t clearMs;          // max time the behind limit may stay active
                             // while the axis is allowed to run, before
                             // STUCK_AT_LIMIT faults
};

// Result of one update() call: what the caller should do this tick, plus
// the axis's resulting state for status reporting.
struct AxisOutput {
  bool run;             // true if the motor should be driven this tick
  AxisDir dir;           // heading to drive toward (if run is true)
  AxisState state;       // axis state after this update
  AxisFault fault;       // non-NONE only in the same update a fault occurs
                          // (state also becomes FAULT in that case; after
                          // that, fault is NOT re-reported each tick — see
                          // getFault() for the latched/sticky value)
  bool reversed;         // true only in the single update a reversal happened
};

class Axis {
 public:
  // Store the tunables this axis will use. Does not change state; call
  // start() separately to begin running.
  void begin(const AxisConfig &config);

  // Begin (or restart) autonomous bouncing: clears any fault, enters
  // RUNNING, resets the reversal count, and begins a new travel segment
  // timed from nowMs in direction firstDir.
  void start(uint32_t nowMs, AxisDir firstDir);

  // Stop without it being a fault: enters IDLE. update() will report
  // run=false until start() is called again. Does not clear a prior fault
  // code (faults are only cleared by start(), per the class's fault-sticky
  // contract) and does not reset the reversal count.
  void halt();

  // Advance the bounce logic by one tick. `nowMs` is the current time in
  // milliseconds (caller-supplied clock); `minActive`/`maxActive` are the
  // current debounced states of the MIN-end and MAX-end limit sensors.
  // See Axis.cpp for the exact rule order this implements.
  AxisOutput update(uint32_t nowMs, bool minActive, bool maxActive);

  AxisState getState() const { return _state; }
  AxisDir getDir() const { return _dir; }
  AxisFault getFault() const { return _fault; }
  uint32_t getReversalCount() const { return _reversalCount; }

 private:
  AxisConfig _config{0, 0, 0, 0};

  AxisState _state = AxisState::IDLE;
  AxisDir _dir = AxisDir::TOWARD_MAX;
  AxisFault _fault = AxisFault::NONE;
  uint32_t _reversalCount = 0;

  uint32_t _segmentStartMs = 0;
  uint32_t _lastReversalMs = 0;
  bool _hasReversedSinceStart = false;

  // Edge-detection state for OPPOSITE_LIMIT_HIT. hasPrevReading is false
  // only for the single update() call right after start() (no edge can be
  // detected without a previous sample to compare against).
  bool _hasPrevReading = false;
  bool _prevMinActive = false;
  bool _prevMaxActive = false;

  // Stuck-at-limit clock (Module 3): accumulated ms during which the
  // behind limit has been continuously active while the axis was allowed
  // to run (not in the post-reversal pause). Reset to 0 whenever the
  // behind limit is inactive, or at every reversal. Frozen (neither
  // advancing nor resetting) while the behind limit is active but the
  // axis is in the post-reversal pause. See Axis.cpp for the exact
  // accumulation logic.
  uint32_t _stuckClockMs = 0;
  uint32_t _stuckLastTickMs = 0;
};
