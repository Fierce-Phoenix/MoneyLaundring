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

AxisConfig defaultConfig() {
  return AxisConfig{400, 150, 5000};
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
  AxisConfig cfg{400, 150, 5000};
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
  AxisConfig cfg{400, 150, 20000};
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
  AxisConfig cfg{10, 5, 100};
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
  AxisConfig cfg{10, 5, 50};
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

  AxisConfig cfg{400, 150, 20000};  // the real Module 2 config.h values
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
  return UNITY_END();
}
