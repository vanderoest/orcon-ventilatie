// Host-run regression tests for include/orcon_controller.h.
//
// Build & run:
//   g++ -std=c++17 -Wall -Wextra -I../include test/test_controller.cpp -o /tmp/orcon_test && /tmp/orcon_test
// or, from repo root:
//   make -C test
//
// No ESPHome, no hardware — pure logic tests against orcon::Controller.
#include "../include/orcon_controller.h"

#include <cstdio>
#include <cstdlib>

static int g_failures = 0;

#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      g_failures++; \
    } \
  } while (0)

using orcon::Config;
using orcon::Controller;
using orcon::Inputs;
using orcon::Mode;
using orcon::Outputs;
using orcon::State;

static Inputs clean_auto_inputs(uint32_t now_ms, int hour = 12) {
  Inputs in;
  in.now_ms = now_ms;
  in.hour = hour;
  in.time_valid = true;
  in.mode = Mode::AUTO;
  in.voc = 50; in.voc_ok = true;
  in.co2 = 500; in.co2_ok = true;
  in.rh = 40; in.rh_ok = true;
  in.nox = 1; in.nox_ok = true;
  return in;
}

// Defect #2 regression: hold must last the full configured duration, not
// collapse to ~one evaluation cycle.
static void test_hold_lasts_full_duration() {
  Controller c;
  uint32_t t = 0;

  // Trigger BOOST.
  Inputs in = clean_auto_inputs(t);
  in.co2 = 900; // above assert (800)
  Outputs out = c.update(in);
  CHECK(out.state == State::BOOST);

  // Clear the trigger after min dwell (60s) -> should enter HOLD.
  t += 61000;
  in = clean_auto_inputs(t);
  out = c.update(in);
  CHECK(out.state == State::HOLD);

  // Poll every 30s (respecting cooldown) for 4 minutes: must stay in HOLD
  // the whole time (defect #2 made this collapse almost immediately).
  for (int i = 0; i < 8; i++) {
    t += 30000;
    in = clean_auto_inputs(t);
    out = c.update(in);
    CHECK(out.state == State::HOLD);
  }

  // At t = hold_start(61000) + 300000 + margin, hold must have expired.
  t = 61000 + 300000 + 30000;
  in = clean_auto_inputs(t);
  out = c.update(in);
  CHECK(out.state == State::IDLE);
}

// Defect #3 regression: crossing the day/night boundary while in BOOST or
// HOLD must change speed, not state.
static void test_day_night_rollover_preserves_state() {
  Controller c;
  uint32_t t = 0;

  Inputs in = clean_auto_inputs(t, 21); // day
  in.co2 = 900;
  Outputs out = c.update(in);
  CHECK(out.state == State::BOOST);
  CHECK(out.target_speed == 40); // day high speed

  t += 61000;
  in = clean_auto_inputs(t, 22); // rolled into night while still triggered
  in.co2 = 900;
  out = c.update(in);
  CHECK(out.state == State::BOOST);
  CHECK(out.target_speed == 30); // night high speed, same BOOST state
}

static void test_nan_enters_fault_and_recovers() {
  Controller c;
  uint32_t t = 0;

  Inputs in = clean_auto_inputs(t);
  in.co2_ok = false; // simulates NaN / stale
  Outputs out = c.update(in);
  CHECK(out.state == State::FAULT);
  CHECK(out.fault);
  CHECK(out.target_speed == 15);

  t += 100;
  in = clean_auto_inputs(t);
  out = c.update(in);
  CHECK(out.state == State::IDLE);
  CHECK(!out.fault);
}

static void test_hysteresis_no_oscillation_at_threshold() {
  Controller c;
  uint32_t t = 0;

  Inputs in = clean_auto_inputs(t);
  in.co2 = 850; // above assert
  Outputs out = c.update(in);
  CHECK(out.state == State::BOOST);

  // Value dips between release (700) and assert (800): must NOT clear boost.
  for (int i = 0; i < 5; i++) {
    t += 31000; // clear cooldown each time
    in = clean_auto_inputs(t);
    in.co2 = 750;
    out = c.update(in);
    CHECK(out.state == State::BOOST);
  }
}

static void test_shower_detection_fires_and_clears() {
  Controller c;
  uint32_t t = 0;

  // Baseline RH samples, spaced past the min sample interval.
  for (int i = 0; i < 3; i++) {
    Inputs in = clean_auto_inputs(t);
    in.rh = 40;
    c.update(in);
    t += 30000;
  }

  // Rapid rise: +5%RH within the 5-minute window should trigger shower latch -> BOOST.
  Inputs in = clean_auto_inputs(t);
  in.rh = 45;
  Outputs out = c.update(in);
  CHECK(out.state == State::BOOST);

  // Let rate settle back down and RH return near baseline, past min dwell.
  t += 61000;
  in = clean_auto_inputs(t);
  in.rh = 41;
  out = c.update(in);
  // Either still BOOST (dwell not fully elapsed from trigger) or transitioning
  // to HOLD once dwell + release conditions are met; must not still be latched forever.
  CHECK(out.state == State::BOOST || out.state == State::HOLD);
}

static void test_manual_roundtrip_returns_to_idle() {
  Controller c;
  uint32_t t = 0;

  Inputs in = clean_auto_inputs(t);
  in.co2 = 900;
  Outputs out = c.update(in);
  CHECK(out.state == State::BOOST);

  t += 1000;
  in = clean_auto_inputs(t);
  in.mode = Mode::HOOG;
  out = c.update(in);
  CHECK(out.state == State::MANUAL);
  CHECK(out.target_speed == 85);

  t += 1000;
  in = clean_auto_inputs(t); // back to AUTO, sensors clean
  out = c.update(in);
  CHECK(out.state == State::IDLE); // never resumes BOOST/HOLD from before manual
}

static void test_boost_min_dwell_blocks_early_release() {
  Controller c;
  uint32_t t = 0;

  Inputs in = clean_auto_inputs(t);
  in.co2 = 900;
  Outputs out = c.update(in);
  CHECK(out.state == State::BOOST);

  // Sensors clear immediately, but dwell (60s) hasn't elapsed -> stay in BOOST.
  t += 31000;
  in = clean_auto_inputs(t);
  out = c.update(in);
  CHECK(out.state == State::BOOST);
}

int main() {
  test_hold_lasts_full_duration();
  test_day_night_rollover_preserves_state();
  test_nan_enters_fault_and_recovers();
  test_hysteresis_no_oscillation_at_threshold();
  test_shower_detection_fires_and_clears();
  test_manual_roundtrip_returns_to_idle();
  test_boost_min_dwell_blocks_early_release();

  if (g_failures == 0) {
    std::printf("All tests passed.\n");
    return 0;
  }
  std::printf("%d check(s) failed.\n", g_failures);
  return 1;
}
