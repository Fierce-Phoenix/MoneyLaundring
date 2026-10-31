// Axis.cpp — see Axis.h for the full contract and rollover note.
//
// Rule order implemented by update(), matching the Module 2/3 spec exactly:
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
//   3. STUCK_AT_LIMIT (Module 3): evaluated immediately after the
//      OPPOSITE_LIMIT_HIT check and BEFORE TRAVEL_TIMEOUT, REGARDLESS of
//      the suppression window above. A clock accumulates the time during
//      which the limit behind the current heading is active while the
//      axis would otherwise be allowed to run (i.e. not held in the
//      post-reversal pause); it resets to zero whenever the behind limit
//      goes inactive, and at every reversal. Once that accumulated time
//      reaches clearMs, the axis faults STUCK_AT_LIMIT. This catches the
//      case OPPOSITE_LIMIT_HIT cannot: the carriage already resting at an
//      end with inverted motor wiring, so the behind limit was already
//      active before any edge could be observed, and (because the real
//      motion is inverted) never clears — without this rule the axis
//      would otherwise just keep "running" into its end stop until the
//      much longer TRAVEL_TIMEOUT eventually catches it.
//   4. TRAVEL_TIMEOUT (time since the segment began >= maxTravelMs). Not
//      subject to the suppression window above.
//   5. Reversal trigger: limit ahead of the heading is active AND at
//      least lockoutMs has passed since the last reversal (or no
//      reversal has happened yet this run) -> reverse, increment
//      reversalCount, restart the segment timer, start the pause,
//      reversed=true, run=false for this one update. Also resets the
//      stuck-at-limit clock (rule 3) per its reset contract.
//   6. Otherwise: run=false while still inside reversePauseMs of the last
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
  _stuckClockMs = 0;
  _stuckLastTickMs = nowMs;
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

  const AxisDir behindDir = opposite(_dir);
  const bool behindActiveNow = (behindDir == AxisDir::TOWARD_MIN) ? minActive : maxActive;

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
      const bool behindActivePrev = (behindDir == AxisDir::TOWARD_MIN) ? prevMin : prevMax;
      if (!behindActivePrev && behindActiveNow) {
        _state = AxisState::FAULT;
        _fault = AxisFault::OPPOSITE_LIMIT_HIT;
        AxisOutput out{false, _dir, _state, _fault, false};
        return out;
      }
    }
  }

  // Rule 3: STUCK_AT_LIMIT (Module 3). Evaluated immediately after the
  // OPPOSITE_LIMIT_HIT check and before TRAVEL_TIMEOUT, regardless of the
  // suppression window above (the whole point is to catch the case where
  // the behind limit was already active before start() / before any edge
  // could ever be observed, so 2b can never fire). The clock accumulates
  // elapsed time while the behind limit is active and the axis is not
  // currently held in the post-reversal pause; it is frozen (neither
  // advanced nor reset) while the behind limit is active but the axis IS
  // in the pause, and reset to zero the instant the behind limit goes
  // inactive. It is also reset at every reversal (rule 5, below).
  {
    const bool inPause = _hasReversedSinceStart && (sinceReversal < _config.reversePauseMs);
    const uint32_t elapsedSinceLastTick = nowMs - _stuckLastTickMs;  // unsigned, rollover-safe
    if (behindActiveNow) {
      if (!inPause) {
        _stuckClockMs += elapsedSinceLastTick;
      }
      // else: frozen during the pause — neither advance nor reset.
    } else {
      _stuckClockMs = 0;
    }
    _stuckLastTickMs = nowMs;

    if (_stuckClockMs >= _config.clearMs) {
      _state = AxisState::FAULT;
      _fault = AxisFault::STUCK_AT_LIMIT;
      AxisOutput out{false, _dir, _state, _fault, false};
      return out;
    }
  }

  // Rule 4: TRAVEL_TIMEOUT. Always checked, independent of suppression.
  const uint32_t sinceSegmentStart = nowMs - _segmentStartMs;  // unsigned, rollover-safe
  if (sinceSegmentStart >= _config.maxTravelMs) {
    _state = AxisState::FAULT;
    _fault = AxisFault::TRAVEL_TIMEOUT;
    AxisOutput out{false, _dir, _state, _fault, false};
    return out;
  }

  // Rule 5: reversal trigger. The limit ahead of the current heading is
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
    _stuckClockMs = 0;
    _stuckLastTickMs = nowMs;
    AxisOutput out{false, _dir, _state, AxisFault::NONE, true};
    return out;
  }

  // Rule 6: hold stopped during the post-reversal pause, else run.
  if (_hasReversedSinceStart && (sinceReversal < _config.reversePauseMs)) {
    AxisOutput out{false, _dir, _state, AxisFault::NONE, false};
    return out;
  }

  AxisOutput out{true, _dir, _state, AxisFault::NONE, false};
  return out;
}
