// test_axis.cpp — native unit tests for the pure Axis bounce-logic class.
//
// Built only by [env:native] (platformio.ini), which compiles just
// src/Axis.cpp alongside this file via build_src_filter, so this runs as a
// plain host executable with no Arduino, no ESP32, no physical hardware.

#include <unity.h>

#include <cstdint>

#include "../../src/Axis.h"

namespace {

int asInt(AxisDir d) { return static_cast<int>(d); }
int asInt(AxisState s) { return static_cast<int>(s); }
int asInt(AxisFault f) { return static_cast<int>(f); }

// clearMs = 3000 here matches config.h's real MAX_CLEAR_MS default so the
// general-purpose tests below exercise realistic numbers; it is large
// enough that none of the short multi-tick sequences in those tests
// accidentally accumulate enough "behind limit active" time to trip
// STUCK_AT_LIMIT as a side effect of something else they're testing.
AxisConfig defaultConfig() {
  return AxisConfig{400, 150, 5000, 3000};
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// 1. Heading and run=true with no limits active.
void test_initial_run_with_no_limits(void) {
  Axis axis;
  axis.begin(defaultConfig());
  axis.start(0, AxisDir::TOWARD_MAX);

  AxisOutput out = axis.update(10, false, false);

  TEST_ASSERT_TRUE(out.run);
  TEST_ASSERT_EQUAL(asInt(AxisDir::TOWARD_MAX), asInt(out.dir));
  TEST_ASSERT_EQUAL(asInt(AxisState::RUNNING), asInt(out.state));
  TEST_ASSERT_FALSE(out.reversed);
}

// 2. Reversal when the limit ahead goes active: run=false during the
// pause, run=true in the opposite heading once the pause elapses.
void test_reversal_pause_then_opposite_heading(void) {
  Axis axis;
  AxisConfig cfg{400, 150, 5000, 3000};
  axis.begin(cfg);
  axis.start(0, AxisDir::TOWARD_MAX);

  // Establish a previous reading so edge detection has a baseline.
  axis.update(10, false, false);

  // Ahead limit (MAX) goes active -> reversal.
  AxisOutput out = axis.update(20, false, true);
  TEST_ASSERT_TRUE(out.reversed);
  TEST_ASSERT_FALSE(out.run);
  TEST_ASSERT_EQUAL(asInt(AxisDir::TOWARD_MIN), asInt(out.dir));
  TEST_ASSERT_EQUAL_UINT32(1, axis.getReversalCount());

  // Still inside the pause (150ms from the reversal at t=20).
  out = axis.update(169, false, false);
  TEST_ASSERT_FALSE(out.run);

  // Pause has just elapsed (sinceReversal == reversePauseMs).
  out = axis.update(170, false, false);
  TEST_ASSERT_TRUE(out.run);
  TEST_ASSERT_EQUAL(asInt(AxisDir::TOWARD_MIN), asInt(out.dir));
}

// 3. The limit behind the heading already being active at start causes
// neither a fault nor a reversal.
void test_limit_behind_active_at_start_is_harmless(void) {
  Axis axis;
  axis.begin(defaultConfig());
  axis.start(0, AxisDir::TOWARD_MAX);

  // MIN (behind, since heading is TOWARD_MAX) is already active on the
  // very first update after start() -> no edge can be detected, no fault.
  AxisOutput out = axis.update(10, /*minActive=*/true, /*maxActive=*/false);

  TEST_ASSERT_EQUAL(asInt(AxisState::RUNNING), asInt(out.state));
  TEST_ASSERT_TRUE(out.run);
  TEST_ASSERT_FALSE(out.reversed);
  TEST_ASSERT_EQUAL(asInt(AxisFault::NONE), asInt(axis.getFault()));
  TEST_ASSERT_EQUAL_UINT32(0, axis.getReversalCount());
}

// 4. A second reversal is suppressed inside lockoutMs and allowed once it
// elapses.
void test_second_reversal_suppressed_until_lockout_elapses(void) {
  Axis axis;
  AxisConfig cfg{400, 150, 20000, 3000};
  axis.begin(cfg);
  axis.start(0, AxisDir::TOWARD_MAX);

  axis.update(10, false, false);               // baseline
  AxisOutput out = axis.update(20, false, true);  // reversal #1 @ t=20
  TEST_ASSERT_TRUE(out.reversed);
  TEST_ASSERT_EQUAL_UINT32(1, axis.getReversalCount());

  out = axis.update(170, false, false);  // pause elapsed, now heading MIN
  TEST_ASSERT_TRUE(out.run);

  // MIN goes active early (chatter), well inside lockoutMs of reversal #1.
  out = axis.update(250, true, false);  // sinceReversal = 230 < 400
  TEST_ASSERT_FALSE(out.reversed);
  TEST_ASSERT_EQUAL_UINT32(1, axis.getReversalCount());
  TEST_ASSERT_TRUE(out.run);  // still heading MIN, chatter ignored

  // Exactly at the lockout boundary (sinceReversal == lockoutMs): allowed.
  out = axis.update(420, true, false);  // sinceReversal = 400
  TEST_ASSERT_TRUE(out.reversed);
  TEST_ASSERT_EQUAL_UINT32(2, axis.getReversalCount());
  TEST_ASSERT_EQUAL(asInt(AxisDir::TOWARD_MAX), asInt(out.dir));
}

// 5. BOTH_LIMITS_ACTIVE fault.
void test_both_limits_active_fault(void) {
  Axis axis;
  axis.begin(defaultConfig());
  axis.start(0, AxisDir::TOWARD_MAX);

  AxisOutput out = axis.update(10, true, true);

  TEST_ASSERT_EQUAL(asInt(AxisState::FAULT), asInt(out.state));
  TEST_ASSERT_EQUAL(asInt(AxisFault::BOTH_LIMITS_ACTIVE), asInt(out.fault));
  TEST_ASSERT_FALSE(out.run);
  TEST_ASSERT_EQUAL(asInt(AxisFault::BOTH_LIMITS_ACTIVE), asInt(axis.getFault()));
}

// 6. OPPOSITE_LIMIT_HIT on a rising edge of the opposite (behind) limit.
void test_opposite_limit_hit_on_rising_edge(void) {
  Axis axis;
  axis.begin(defaultConfig());
  axis.start(0, AxisDir::TOWARD_MAX);

  axis.update(10, false, false);  // baseline: MIN inactive

  // MIN (behind, heading TOWARD_MAX) rises from inactive to active.
  AxisOutput out = axis.update(20, true, false);

  TEST_ASSERT_EQUAL(asInt(AxisState::FAULT), asInt(out.state));
  TEST_ASSERT_EQUAL(asInt(AxisFault::OPPOSITE_LIMIT_HIT), asInt(out.fault));
  TEST_ASSERT_FALSE(out.run);
}

// 7. TRAVEL_TIMEOUT, with the timer restarting after each reversal.
void test_travel_timeout_restarts_after_reversal(void) {
  Axis axis;
  AxisConfig cfg{10, 5, 100, 1000};
  axis.begin(cfg);
  axis.start(0, AxisDir::TOWARD_MAX);

  AxisOutput out = axis.update(99, false, false);
  TEST_ASSERT_EQUAL(asInt(AxisState::RUNNING), asInt(out.state));

  out = axis.update(100, false, false);  // sinceSegmentStart == maxTravelMs
  TEST_ASSERT_EQUAL(asInt(AxisState::FAULT), asInt(out.state));
  TEST_ASSERT_EQUAL(asInt(AxisFault::TRAVEL_TIMEOUT), asInt(out.fault));

  // Fresh run: reverse partway through, confirm the segment timer resets.
  axis.start(0, AxisDir::TOWARD_MAX);
  axis.update(5, false, false);
  out = axis.update(30, false, true);  // reversal @ t=30
  TEST_ASSERT_TRUE(out.reversed);

  out = axis.update(30 + 99, false, false);  // 99ms into the new segment
  TEST_ASSERT_EQUAL(asInt(AxisState::RUNNING), asInt(out.state));

  out = axis.update(30 + 100, false, false);  // 100ms into the new segment
  TEST_ASSERT_EQUAL(asInt(AxisState::FAULT), asInt(out.state));
  TEST_ASSERT_EQUAL(asInt(AxisFault::TRAVEL_TIMEOUT), asInt(out.fault));
}

// 8. A fault is sticky and cleared only by start().
void test_fault_is_sticky_until_start(void) {
  Axis axis;
  axis.begin(defaultConfig());
  axis.start(0, AxisDir::TOWARD_MAX);

  axis.update(10, true, true);  // BOTH_LIMITS_ACTIVE
  TEST_ASSERT_EQUAL(asInt(AxisFault::BOTH_LIMITS_ACTIVE), asInt(axis.getFault()));

  AxisOutput out = axis.update(20, false, false);
  TEST_ASSERT_FALSE(out.run);
  TEST_ASSERT_EQUAL(asInt(AxisState::FAULT), asInt(axis.getState()));
  TEST_ASSERT_EQUAL(asInt(AxisFault::BOTH_LIMITS_ACTIVE), asInt(axis.getFault()));

  out = axis.update(1000, false, false);  // still sticky, much later
  TEST_ASSERT_FALSE(out.run);
  TEST_ASSERT_EQUAL(asInt(AxisFault::BOTH_LIMITS_ACTIVE), asInt(axis.getFault()));

  axis.start(1010, AxisDir::TOWARD_MAX);
  TEST_ASSERT_EQUAL(asInt(AxisFault::NONE), asInt(axis.getFault()));
  TEST_ASSERT_EQUAL(asInt(AxisState::RUNNING), asInt(axis.getState()));

  out = axis.update(1020, false, false);
  TEST_ASSERT_TRUE(out.run);
}

// 9. halt() stops output without being a fault.
void test_halt_stops_output(void) {
  Axis axis;
  axis.begin(defaultConfig());
  axis.start(0, AxisDir::TOWARD_MAX);
  axis.update(10, false, false);

  axis.halt();
  TEST_ASSERT_EQUAL(asInt(AxisState::IDLE), asInt(axis.getState()));

  AxisOutput out = axis.update(20, false, false);
  TEST_ASSERT_FALSE(out.run);
  TEST_ASSERT_EQUAL(asInt(AxisState::IDLE), asInt(out.state));
}

// 10. Correct behaviour when nowMs wraps past 0xFFFFFFFF.
void test_millis_rollover(void) {
  Axis axis;
  AxisConfig cfg{10, 5, 50, 1000};
  axis.begin(cfg);

  const uint32_t startMs = 0xFFFFFFF0u;  // 4294967280
  axis.start(startMs, AxisDir::TOWARD_MAX);

  // 20ms later, nowMs has wrapped around past 0xFFFFFFFF back to 4.
  const uint32_t t1 = startMs + 20u;  // wraps to 4
  TEST_ASSERT_EQUAL_UINT32(4u, t1);

  // sinceSegmentStart should still read as 20ms (not a huge bogus value),
  // and the ahead limit being active should trigger a normal reversal.
  AxisOutput out = axis.update(t1, false, true);
  TEST_ASSERT_TRUE(out.reversed);
  TEST_ASSERT_EQUAL(asInt(AxisDir::TOWARD_MIN), asInt(out.dir));

  // Still inside the pause 3ms after the (wrapped) reversal time.
  const uint32_t t2 = t1 + 3u;
  out = axis.update(t2, false, false);
  TEST_ASSERT_FALSE(out.run);

  // Pause elapsed 5ms after the reversal.
  const uint32_t t3 = t1 + 5u;
  out = axis.update(t3, false, false);
  TEST_ASSERT_TRUE(out.run);
  TEST_ASSERT_EQUAL(asInt(AxisDir::TOWARD_MIN), asInt(out.dir));
}

// 11. Simulated carriage: 300mm track, 80mm/s, 5mm sensor zones at each
// end, 20ms ticks, 120 simulated seconds. Reversals must alternate
// direction, at least eight must occur, and no fault may be raised.
void test_simulated_carriage_bounces_without_fault(void) {
  const double trackMm = 300.0;
  const double speedMmPerSec = 80.0;
  const double zoneMm = 5.0;
  const uint32_t dtMs = 20;
  const int totalTicks = (120 * 1000) / static_cast<int>(dtMs);  // 6000
  const double distPerTick = speedMmPerSec * (static_cast<double>(dtMs) / 1000.0);

  AxisConfig cfg{400, 150, 20000, 3000};  // the real config.h values
  Axis axis;
  axis.begin(cfg);

  uint32_t nowMs = 0;
  axis.start(nowMs, AxisDir::TOWARD_MAX);

  double pos = 0.0;  // start at the MIN end
  int reversals = 0;
  bool haveLastDir = false;
  AxisDir lastDir = AxisDir::TOWARD_MAX;

  for (int i = 0; i < totalTicks; i++) {
    nowMs += dtMs;

    bool minActive = pos <= zoneMm;
    bool maxActive = pos >= (trackMm - zoneMm);

    AxisOutput out = axis.update(nowMs, minActive, maxActive);

    TEST_ASSERT_NOT_EQUAL(asInt(AxisState::FAULT), asInt(out.state));

    if (out.reversed) {
      if (haveLastDir) {
        TEST_ASSERT_NOT_EQUAL(asInt(lastDir), asInt(out.dir));
      }
      lastDir = out.dir;
      haveLastDir = true;
      reversals++;
    }

    if (out.run) {
      if (out.dir == AxisDir::TOWARD_MAX) {
        pos += distPerTick;
      } else {
        pos -= distPerTick;
      }
      if (pos < 0.0) pos = 0.0;
      if (pos > trackMm) pos = trackMm;
    }
  }

  TEST_ASSERT_EQUAL(asInt(AxisState::RUNNING), asInt(axis.getState()));
  TEST_ASSERT_EQUAL(asInt(AxisFault::NONE), asInt(axis.getFault()));
  TEST_ASSERT_GREATER_OR_EQUAL(8, reversals);
}

// ---------------------------------------------------------------------------
// Module 3: boundary tests (1ms before / exactly at each threshold)
// ---------------------------------------------------------------------------

// 12. lockoutMs boundary: a second reversal is refused 1ms before the
// lockout elapses and allowed exactly at it.
void test_boundary_lockout_ms(void) {
  Axis axis;
  AxisConfig cfg{/*lockoutMs=*/400, /*reversePauseMs=*/0, /*maxTravelMs=*/1000000,
                 /*clearMs=*/1000000};
  axis.begin(cfg);
  axis.start(0, AxisDir::TOWARD_MAX);

  axis.update(10, false, false);                  // baseline
  AxisOutput out = axis.update(20, false, true);     // reversal #1 @ t=20
  TEST_ASSERT_TRUE(out.reversed);
  TEST_ASSERT_EQUAL_UINT32(1, axis.getReversalCount());
  // reversePauseMs=0, so the axis is already running toward MIN again.

  // One millisecond before the lockout elapses (sinceReversal = 399).
  out = axis.update(20 + 399, true, false);
  TEST_ASSERT_FALSE(out.reversed);
  TEST_ASSERT_EQUAL_UINT32(1, axis.getReversalCount());

  // Exactly at the lockout boundary (sinceReversal = 400).
  out = axis.update(20 + 400, true, false);
  TEST_ASSERT_TRUE(out.reversed);
  TEST_ASSERT_EQUAL_UINT32(2, axis.getReversalCount());
}

// 13. reversePauseMs boundary: run is false 1ms before the pause elapses
// and true exactly at it.
void test_boundary_reverse_pause_ms(void) {
  Axis axis;
  AxisConfig cfg{/*lockoutMs=*/0, /*reversePauseMs=*/200, /*maxTravelMs=*/1000000,
                 /*clearMs=*/1000000};
  axis.begin(cfg);
  axis.start(0, AxisDir::TOWARD_MAX);

  axis.update(5, false, false);               // baseline
  AxisOutput out = axis.update(10, false, true);  // reversal @ t=10
  TEST_ASSERT_TRUE(out.reversed);

  // One millisecond before the pause elapses (sinceReversal = 199).
  out = axis.update(10 + 199, false, false);
  TEST_ASSERT_FALSE(out.run);

  // Exactly at the pause boundary (sinceReversal = 200).
  out = axis.update(10 + 200, false, false);
  TEST_ASSERT_TRUE(out.run);
}

// 14. maxTravelMs boundary: still RUNNING 1ms before the timeout, FAULT
// exactly at it.
void test_boundary_max_travel_ms(void) {
  Axis axis;
  AxisConfig cfg{/*lockoutMs=*/1000000, /*reversePauseMs=*/0, /*maxTravelMs=*/500,
                 /*clearMs=*/1000000};
  axis.begin(cfg);
  axis.start(0, AxisDir::TOWARD_MAX);

  // One millisecond before the travel timeout (sinceSegmentStart = 499).
  AxisOutput out = axis.update(499, false, false);
  TEST_ASSERT_EQUAL(asInt(AxisState::RUNNING), asInt(out.state));
  TEST_ASSERT_TRUE(out.run);

  // Exactly at the travel-timeout boundary (sinceSegmentStart = 500).
  out = axis.update(500, false, false);
  TEST_ASSERT_EQUAL(asInt(AxisState::FAULT), asInt(out.state));
  TEST_ASSERT_EQUAL(asInt(AxisFault::TRAVEL_TIMEOUT), asInt(out.fault));
}

// 15. clearMs boundary: still RUNNING (no fault) 1ms before the
// stuck-at-limit clock reaches clearMs, FAULT exactly at it.
void test_boundary_clear_ms(void) {
  Axis axis;
  AxisConfig cfg{/*lockoutMs=*/0, /*reversePauseMs=*/0, /*maxTravelMs=*/1000000,
                 /*clearMs=*/300};
  axis.begin(cfg);
  axis.start(0, AxisDir::TOWARD_MAX);  // behind = MIN

  // One millisecond before the stuck clock reaches clearMs.
  AxisOutput out = axis.update(299, /*minActive=*/true, /*maxActive=*/false);
  TEST_ASSERT_EQUAL(asInt(AxisState::RUNNING), asInt(out.state));
  TEST_ASSERT_TRUE(out.run);

  // Exactly at the clearMs boundary.
  out = axis.update(300, /*minActive=*/true, /*maxActive=*/false);
  TEST_ASSERT_EQUAL(asInt(AxisState::FAULT), asInt(out.state));
  TEST_ASSERT_EQUAL(asInt(AxisFault::STUCK_AT_LIMIT), asInt(out.fault));
}

// ---------------------------------------------------------------------------
// Module 3: stuck-at-limit
// ---------------------------------------------------------------------------

// 16. The behind limit stays active from start(); the fault appears
// exactly at clearMs, and not before, stepping in 50ms ticks.
void test_stuck_at_limit_from_start(void) {
  Axis axis;
  AxisConfig cfg{/*lockoutMs=*/0, /*reversePauseMs=*/0, /*maxTravelMs=*/1000000,
                 /*clearMs=*/250};
  axis.begin(cfg);
  axis.start(0, AxisDir::TOWARD_MAX);  // behind = MIN

  // Behind limit (MIN) active continuously from the very first tick. No
  // rising edge is ever observed (it was already active on the first
  // post-start update), so OPPOSITE_LIMIT_HIT can never fire here — this
  // is exactly the scenario STUCK_AT_LIMIT exists to catch.
  for (uint32_t t = 50; t < 250; t += 50) {
    AxisOutput out = axis.update(t, /*minActive=*/true, /*maxActive=*/false);
    TEST_ASSERT_EQUAL(asInt(AxisState::RUNNING), asInt(out.state));
    TEST_ASSERT_EQUAL(asInt(AxisFault::NONE), asInt(out.fault));
  }

  // t=250: the stuck clock (50+50+50+50=200 so far, plus this tick's 50)
  // reaches exactly clearMs=250.
  AxisOutput out = axis.update(250, /*minActive=*/true, /*maxActive=*/false);
  TEST_ASSERT_EQUAL(asInt(AxisState::FAULT), asInt(out.state));
  TEST_ASSERT_EQUAL(asInt(AxisFault::STUCK_AT_LIMIT), asInt(out.fault));
}

// ---------------------------------------------------------------------------
// Module 3: simulated carriage with inverted direction mapping
// ---------------------------------------------------------------------------

// 17. Inverted mapping, starting mid-track: Axis thinks it is driving
// TOWARD_MAX but (because the mapping is inverted) the physical carriage
// actually moves toward MIN. Starting mid-track means the behind limit
// (MIN) is inactive at first and only becomes active after some travel,
// so this must be caught as a rising edge -> OPPOSITE_LIMIT_HIT.
void test_inverted_mapping_mid_track_hits_opposite_limit(void) {
  const double trackMm = 300.0;
  const double speedMmPerSec = 80.0;
  const double zoneMm = 5.0;
  const uint32_t dtMs = 20;
  const double distPerTick = speedMmPerSec * (static_cast<double>(dtMs) / 1000.0);
  // Real config.h values; maxTravelMs is intentionally large so a fault
  // caused by anything other than OPPOSITE_LIMIT_HIT would be a surprise.
  AxisConfig cfg{400, 150, 20000, 3000};

  Axis axis;
  axis.begin(cfg);

  uint32_t nowMs = 0;
  axis.start(nowMs, AxisDir::TOWARD_MAX);

  double pos = 150.0;  // mid-track
  const int maxTicks = 1200;  // generous cap; real travel time is ~1813ms (~91 ticks)

  AxisFault observedFault = AxisFault::NONE;
  int tickOfFault = -1;

  for (int i = 0; i < maxTicks; i++) {
    nowMs += dtMs;
    bool minActive = pos <= zoneMm;
    bool maxActive = pos >= (trackMm - zoneMm);

    AxisOutput out = axis.update(nowMs, minActive, maxActive);

    if (out.state == AxisState::FAULT) {
      observedFault = out.fault;
      tickOfFault = i;
      break;
    }

    if (out.run) {
      // Inverted mapping: a commanded TOWARD_MAX heading actually drives
      // the physical carriage toward MIN, and vice versa.
      if (out.dir == AxisDir::TOWARD_MAX) {
        pos -= distPerTick;
      } else {
        pos += distPerTick;
      }
      if (pos < 0.0) pos = 0.0;
      if (pos > trackMm) pos = trackMm;
    }
  }

  TEST_ASSERT_TRUE(tickOfFault >= 0);
  TEST_ASSERT_EQUAL(asInt(AxisFault::OPPOSITE_LIMIT_HIT), asInt(observedFault));
}

// 18. Inverted mapping, starting at the end the axis is heading away
// from: Axis starts heading TOWARD_MAX (nominally away from MIN), but the
// carriage is physically already resting at the MIN end when start() is
// called. With the mapping inverted, "driving away" from MIN actually
// keeps pushing into it, so the behind limit (MIN) is active from the
// very first tick and never clears — no rising edge is ever possible, so
// OPPOSITE_LIMIT_HIT cannot fire. STUCK_AT_LIMIT must catch this well
// before maxTravelMs.
void test_inverted_mapping_starting_at_end_hits_stuck_at_limit(void) {
  const double trackMm = 300.0;
  const double speedMmPerSec = 80.0;
  const double zoneMm = 5.0;
  const uint32_t dtMs = 20;
  const double distPerTick = speedMmPerSec * (static_cast<double>(dtMs) / 1000.0);
  AxisConfig cfg{400, 150, 20000, 3000};  // real config.h values

  Axis axis;
  axis.begin(cfg);

  uint32_t nowMs = 0;
  axis.start(nowMs, AxisDir::TOWARD_MAX);  // "away from" MIN, nominally

  double pos = 0.0;  // already resting at the MIN end
  const int maxTravelTicks = 20000 / static_cast<int>(dtMs);  // 1000
  const int maxTicks = maxTravelTicks + 10;  // small margin past maxTravelMs

  AxisFault observedFault = AxisFault::NONE;
  int tickOfFault = -1;

  for (int i = 0; i < maxTicks; i++) {
    nowMs += dtMs;
    bool minActive = pos <= zoneMm;
    bool maxActive = pos >= (trackMm - zoneMm);

    AxisOutput out = axis.update(nowMs, minActive, maxActive);

    if (out.state == AxisState::FAULT) {
      observedFault = out.fault;
      tickOfFault = i;
      break;
    }

    if (out.run) {
      // Inverted mapping, same as test 17.
      if (out.dir == AxisDir::TOWARD_MAX) {
        pos -= distPerTick;  // actually pushes further into the MIN end
      } else {
        pos += distPerTick;
      }
      if (pos < 0.0) pos = 0.0;
      if (pos > trackMm) pos = trackMm;
    }
  }

  TEST_ASSERT_TRUE(tickOfFault >= 0);
  TEST_ASSERT_EQUAL(asInt(AxisFault::STUCK_AT_LIMIT), asInt(observedFault));
  // "Well before maxTravelMs": clearMs=3000 means the fault should land
  // around tick 3000/20=150, far short of the maxTravelTicks=1000 budget.
  TEST_ASSERT_TRUE(tickOfFault < (maxTravelTicks / 2));
}

// ---------------------------------------------------------------------------
// Module 3: deterministic fuzz test
// ---------------------------------------------------------------------------

namespace {

// Minimal xorshift32 PRNG, fixed seed, used only by this test so the fuzz
// run is fully deterministic and reproducible.
class Xorshift32 {
 public:
  explicit Xorshift32(uint32_t seed) : state_(seed ? seed : 1u) {}

  uint32_t next() {
    uint32_t x = state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state_ = x;
    return x;
  }

  // Inclusive range [lo, hi].
  uint32_t range(uint32_t lo, uint32_t hi) {
    uint32_t span = hi - lo + 1u;
    return lo + (next() % span);
  }

 private:
  uint32_t state_;
};

}  // namespace

// 19. Deterministic fuzz test: >=100000 ticks of random limit flips with
// random 1-50ms time steps and occasional start()/halt() calls, checking
// the five invariants listed in the Module 3 spec on every single tick.
void test_fuzz_random_ticks_hold_invariants(void) {
  Xorshift32 rng(0xC0FFEE42u);

  AxisConfig cfg{/*lockoutMs=*/50, /*reversePauseMs=*/30, /*maxTravelMs=*/500,
                 /*clearMs=*/80};
  Axis axis;
  axis.begin(cfg);

  const int totalTicks = 150000;
  uint32_t nowMs = 0;

  bool minState = false;
  bool maxState = false;

  bool everStarted = false;
  bool haveLastReversalTime = false;
  uint32_t lastReversalTimeMs = 0;

  for (int i = 0; i < totalTicks; i++) {
    // Occasionally recover from FAULT/IDLE with a fresh start().
    if (axis.getState() != AxisState::RUNNING) {
      if (rng.range(0, 9) == 0) {  // ~10% chance per tick while not running
        AxisDir firstDir = (rng.range(0, 1) == 0) ? AxisDir::TOWARD_MAX : AxisDir::TOWARD_MIN;
        axis.start(nowMs, firstDir);
        everStarted = true;
        haveLastReversalTime = false;  // pause/lockout history resets with the run
      }
    } else {
      // Occasionally halt a running axis.
      if (rng.range(0, 1999) == 0) {  // ~0.05% chance per tick while running
        axis.halt();
      }
    }

    // Advance the clock by a random 1-50ms step.
    nowMs += rng.range(1, 50);

    // Random limit flips: each limit independently has a small chance of
    // toggling this tick (a "flip"), rather than being fully randomized
    // every tick.
    if (rng.range(0, 19) == 0) minState = !minState;
    if (rng.range(0, 19) == 0) maxState = !maxState;

    const uint32_t prevReversalCount = axis.getReversalCount();

    AxisOutput out = axis.update(nowMs, minState, maxState);

    // Invariant 1 (+2): whenever the resulting state is not RUNNING,
    // run must be false. This covers both "not RUNNING this tick" and,
    // by the same mechanism on every subsequent tick, "a FAULT keeps run
    // false until start() is called" — update() never clears run=false
    // while state stays FAULT, by construction of rule 1.
    if (out.state != AxisState::RUNNING) {
      TEST_ASSERT_FALSE(out.run);
    }

    // Invariant 3: reversed==true means the reversal count rose by
    // exactly one on this tick (and only on this tick).
    if (out.reversed) {
      TEST_ASSERT_EQUAL_UINT32(prevReversalCount + 1, axis.getReversalCount());
    } else {
      TEST_ASSERT_EQUAL_UINT32(prevReversalCount, axis.getReversalCount());
    }

    // Invariant 4: consecutive reversals are at least lockoutMs apart.
    if (out.reversed) {
      if (haveLastReversalTime) {
        uint32_t gap = nowMs - lastReversalTimeMs;  // unsigned, rollover-safe
        TEST_ASSERT_TRUE(gap >= cfg.lockoutMs);
      }
      lastReversalTimeMs = nowMs;
      haveLastReversalTime = true;
    }

    // Invariant 5: run is never true during the post-reversal pause.
    if (out.run && haveLastReversalTime) {
      uint32_t sinceReversal = nowMs - lastReversalTimeMs;  // unsigned, rollover-safe
      TEST_ASSERT_TRUE(sinceReversal >= cfg.reversePauseMs);
    }
  }

  // Sanity: the fuzz run actually exercised start() at least once and
  // produced at least one reversal somewhere, so these assertions were
  // not vacuously true throughout.
  TEST_ASSERT_TRUE(everStarted);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_initial_run_with_no_limits);
  RUN_TEST(test_reversal_pause_then_opposite_heading);
  RUN_TEST(test_limit_behind_active_at_start_is_harmless);
  RUN_TEST(test_second_reversal_suppressed_until_lockout_elapses);
  RUN_TEST(test_both_limits_active_fault);
  RUN_TEST(test_opposite_limit_hit_on_rising_edge);
  RUN_TEST(test_travel_timeout_restarts_after_reversal);
  RUN_TEST(test_fault_is_sticky_until_start);
  RUN_TEST(test_halt_stops_output);
  RUN_TEST(test_millis_rollover);
  RUN_TEST(test_simulated_carriage_bounces_without_fault);
  RUN_TEST(test_boundary_lockout_ms);
  RUN_TEST(test_boundary_reverse_pause_ms);
  RUN_TEST(test_boundary_max_travel_ms);
  RUN_TEST(test_boundary_clear_ms);
  RUN_TEST(test_stuck_at_limit_from_start);
  RUN_TEST(test_inverted_mapping_mid_track_hits_opposite_limit);
  RUN_TEST(test_inverted_mapping_starting_at_end_hits_stuck_at_limit);
  RUN_TEST(test_fuzz_random_ticks_hold_invariants);
  return UNITY_END();
}
