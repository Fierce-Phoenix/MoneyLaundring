// Axis.cpp — see Axis.h for the full contract and rollover note.
//
// Rule order implemented by update(), matching the Module 2 spec exactly:
//   1. If state != RUNNING: run=false, nothing else evaluated.
//   2. Record the previous limit states (for edge detection), then —
//      unless still inside the post-reversal suppression window
//      (lockoutMs + reversePauseMs since the last reversal; this window
//      only exists once a reversal has happened, never right after
//      start()) — check, in order:
//        2a. BOTH_LIMITS_ACTIVE (both limits active at once)
//        2b. OPPOSITE_LIMIT_HIT (limit behind the heading rose from
//            inactive to active since the previous update; no edge is
//            evaluated on the very first update after start())
//   3. TRAVEL_TIMEOUT (time since the segment began >= maxTravelMs). Not
//      subject to the suppression window above.
//   4. Reversal trigger: limit ahead of the heading is active AND at
//      least lockoutMs has passed since the last reversal (or no
//      reversal has happened yet this run) -> reverse, increment
//      reversalCount, restart the segment timer, start the pause,
//      reversed=true, run=false for this one update.
//   5. Otherwise: run=false while still inside reversePauseMs of the last
//      reversal, else run=true in the current heading.
// A FAULT is sticky (state stays FAULT, run always false) until start()
// is called again, which clears it.

#include "Axis.h"

namespace {

AxisDir opposite(AxisDir dir) {
  return dir == AxisDir::TOWARD_MAX ? AxisDir::TOWARD_MIN : AxisDir::TOWARD_MAX;
}

}  // namespace

void Axis::begin(const AxisConfig &config) {
  _config = config;
}

void Axis::start(uint32_t nowMs, AxisDir firstDir) {
  _fault = AxisFault::NONE;
  _state = AxisState::RUNNING;
  _dir = firstDir;
  _reversalCount = 0;
  _segmentStartMs = nowMs;
  _hasReversedSinceStart = false;
  _hasPrevReading = false;
  _prevMinActive = false;
  _prevMaxActive = false;
}

void Axis::halt() {
  _state = AxisState::IDLE;
}

AxisOutput Axis::update(uint32_t nowMs, bool minActive, bool maxActive) {
  // Rule 1: not running -> run=false, no other evaluation.
  if (_state != AxisState::RUNNING) {
    AxisOutput out{false, _dir, _state, AxisFault::NONE, false};
    return out;
  }

  // Rule 2: record the previous reading (for edge detection), capturing
  // what it was *before* this update so the edge check below compares
  // against the right values, then overwrite for next time.
  const bool hadPrev = _hasPrevReading;
  const bool prevMin = _prevMinActive;
  const bool prevMax = _prevMaxActive;
  _prevMinActive = minActive;
  _prevMaxActive = maxActive;
  _hasPrevReading = true;

  const uint32_t sinceReversal = nowMs - _lastReversalMs;  // unsigned, rollover-safe
  const uint32_t suppressWindowMs = _config.lockoutMs + _config.reversePauseMs;
  const bool suppressed = _hasReversedSinceStart && (sinceReversal < suppressWindowMs);

  if (!suppressed) {
    // 2a. BOTH_LIMITS_ACTIVE
    if (minActive && maxActive) {
      _state = AxisState::FAULT;
      _fault = AxisFault::BOTH_LIMITS_ACTIVE;
      AxisOutput out{false, _dir, _state, _fault, false};
      return out;
    }

    // 2b. OPPOSITE_LIMIT_HIT: rising edge on the limit behind the heading.
    // No edge is evaluated on the first update after start() (hadPrev
    // false) since there is nothing valid to compare against yet.
    if (hadPrev) {
      const AxisDir behindDir = opposite(_dir);
      const bool oppositeActiveNow = (behindDir == AxisDir::TOWARD_MIN) ? minActive : maxActive;
      const bool oppositeActivePrev = (behindDir == AxisDir::TOWARD_MIN) ? prevMin : prevMax;
      if (!oppositeActivePrev && oppositeActiveNow) {
        _state = AxisState::FAULT;
        _fault = AxisFault::OPPOSITE_LIMIT_HIT;
        AxisOutput out{false, _dir, _state, _fault, false};
        return out;
      }
    }
  }

  // Rule 3: TRAVEL_TIMEOUT. Always checked, independent of suppression.
  const uint32_t sinceSegmentStart = nowMs - _segmentStartMs;  // unsigned, rollover-safe
  if (sinceSegmentStart >= _config.maxTravelMs) {
    _state = AxisState::FAULT;
    _fault = AxisFault::TRAVEL_TIMEOUT;
    AxisOutput out{false, _dir, _state, _fault, false};
    return out;
  }

  // Rule 4: reversal trigger. The limit ahead of the current heading is
  // active, and either no reversal has happened yet this run (nothing to
  // wait on) or at least lockoutMs has passed since the last one.
  const bool aheadActive = (_dir == AxisDir::TOWARD_MAX) ? maxActive : minActive;
  const bool lockoutSatisfied = !_hasReversedSinceStart || (sinceReversal >= _config.lockoutMs);
  if (aheadActive && lockoutSatisfied) {
    _dir = opposite(_dir);
    _reversalCount++;
    _lastReversalMs = nowMs;
    _segmentStartMs = nowMs;
    _hasReversedSinceStart = true;
    AxisOutput out{false, _dir, _state, AxisFault::NONE, true};
    return out;
  }

  // Rule 5: hold stopped during the post-reversal pause, else run.
  if (_hasReversedSinceStart && (sinceReversal < _config.reversePauseMs)) {
    AxisOutput out{false, _dir, _state, AxisFault::NONE, false};
    return out;
  }

  AxisOutput out{true, _dir, _state, AxisFault::NONE, false};
  return out;
}
