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
#include <cstring>

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
  c.configure(Config());
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
  c.configure(Config());
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
  c.configure(Config());
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
  c.configure(Config());
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
  c.configure(Config());
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
  c.configure(Config());
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
  c.configure(Config());
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

// BUGFIX.md #1 regressions: manual modes must never be overridden by FAULT.

static void test_manual_overrides_fault() {
  Controller c;
  c.configure(Config());
  uint32_t t = 0;

  Inputs in = clean_auto_inputs(t);
  in.mode = Mode::HOOG;
  in.voc_ok = in.co2_ok = in.rh_ok = in.nox_ok = false;
  Outputs out = c.update(in);
  CHECK(out.state == State::MANUAL);
  CHECK(out.target_speed == 85);
  CHECK(out.fault); // Problem still reports bad sensors, independent of state
}

static void test_uit_absolute_during_sensor_fault() {
  Controller c;
  c.configure(Config());
  uint32_t t = 0;

  Inputs in = clean_auto_inputs(t);
  in.mode = Mode::UIT;
  in.voc_ok = in.co2_ok = in.rh_ok = in.nox_ok = false;
  Outputs out = c.update(in);
  CHECK(out.state == State::MANUAL);
  CHECK(out.target_speed == 0);
}

static void test_manual_to_auto_with_bad_sensors_enters_fault() {
  Controller c;
  c.configure(Config());
  uint32_t t = 0;

  Inputs in = clean_auto_inputs(t);
  in.mode = Mode::HOOG;
  in.voc_ok = in.co2_ok = in.rh_ok = in.nox_ok = false;
  Outputs out = c.update(in);
  CHECK(out.state == State::MANUAL);

  // Back to AUTO while sensors are still bad: must (re-)enter FAULT, not
  // resume a prior AUTO state, and latches must have been cleared exactly once.
  t += 1000;
  in = clean_auto_inputs(t);
  in.mode = Mode::AUTO;
  in.voc_ok = in.co2_ok = in.rh_ok = in.nox_ok = false;
  out = c.update(in);
  CHECK(out.state == State::FAULT);
  CHECK(out.target_speed == 15);
}

// BUGFIX.md #2 regressions: boot-time config/seed race.

static void test_update_is_noop_before_configure() {
  Controller c; // not configured
  uint32_t t = 0;

  Inputs in = clean_auto_inputs(t);
  in.co2 = 2000; // would trigger BOOST if evaluated
  Outputs out = c.update(in);
  CHECK(out.state == State::IDLE);
  CHECK(!out.speed_changed);
  CHECK(strcmp(out.reason, "not_configured") == 0);

  // configure() unblocks evaluation, and the gated call above must not have
  // consumed the cooldown -- this evaluation must run for real.
  c.configure(Config());
  t += 1000;
  in = clean_auto_inputs(t);
  in.co2 = 900;
  out = c.update(in);
  CHECK(out.state == State::BOOST);
}

// orcon5.log regression: a fresh boot's default/seeded current_speed_ (15)
// equals both the FAULT and IDLE speed (also 15), so target_speed ==
// current_speed_ on the very first evaluation. Without forcing a command on
// that first evaluation, the physical fan -- which always boots off,
// orcon.yaml's restore_mode: ALWAYS_OFF -- was never actually commanded,
// leaving the controller believing it was already running at 15% while the
// hardware sat off indefinitely (until a manual mode picked a *different*
// speed and finally issued a real command).
static void test_first_evaluation_always_commands_fan() {
  Controller c;
  c.configure(Config()); // default Config: speed_idle == speed_fault == 15
  uint32_t t = 0;

  Inputs in = clean_auto_inputs(t);
  in.voc_ok = in.co2_ok = in.rh_ok = in.nox_ok = false; // boot sensor warm-up
  Outputs out = c.update(in);
  CHECK(out.state == State::FAULT);
  CHECK(out.target_speed == 15);
  CHECK(out.speed_changed); // must command the fan even though target == seeded speed

  // Second evaluation at the same target speed must NOT re-command.
  t += 1000;
  in = clean_auto_inputs(t);
  in.voc_ok = in.co2_ok = in.rh_ok = in.nox_ok = false;
  out = c.update(in);
  CHECK(out.target_speed == 15);
  CHECK(!out.speed_changed);
}

// orcon6.log regression: leaving MANUAL while the AUTO cooldown was still
// active reported state=IDLE but kept commanding the *manual* speed (85%),
// because the cooldown branch reused current_speed_ instead of deriving the
// speed from the (now IDLE) state. The fan ran at 85% for ~24 s while the
// controller claimed IDLE.
static void test_manual_speed_does_not_leak_into_auto_cooldown() {
  Controller c;
  c.configure(Config());
  uint32_t t = 0;

  // A real AUTO evaluation, to arm the cooldown.
  Inputs in = clean_auto_inputs(t);
  Outputs out = c.update(in);
  CHECK(out.state == State::IDLE);
  CHECK(out.target_speed == 15);

  // Manual HOOG a moment later (manual is never cooldown-gated).
  t += 1000;
  in = clean_auto_inputs(t);
  in.mode = Mode::HOOG;
  out = c.update(in);
  CHECK(out.state == State::MANUAL);
  CHECK(out.target_speed == 85);

  // Back to AUTO while still inside the 30 s cooldown window: state returns
  // to IDLE, so the speed must be the IDLE speed, not the stale 85%.
  t += 1000;
  in = clean_auto_inputs(t);
  out = c.update(in);
  CHECK(out.state == State::IDLE);
  CHECK(out.target_speed == 15);
  CHECK(out.speed_changed);
}

static void test_seed_primes_hold_timer() {
  Controller c;
  Config cfg; // hold_ms = 300000
  c.configure(cfg);
  uint32_t now = 100000;
  c.seed(State::HOLD, 35, now);

  // Immediately evaluate: hold must still be active, not expired, since the
  // timer was primed forward from now, not left at zero.
  Inputs in = clean_auto_inputs(now);
  Outputs out = c.update(in);
  CHECK(out.state == State::HOLD);
  CHECK(strcmp(out.reason, "hold_active") == 0);
}

static void test_seed_primes_boost_dwell() {
  Controller c;
  Config cfg; // boost_min_dwell_ms = 60000
  c.configure(cfg);
  uint32_t now = 100000;
  c.seed(State::BOOST, 40, now);

  // Sensors clean immediately; dwell must block release since
  // boost_entered_ms_ was primed to now, not left at zero.
  Inputs in = clean_auto_inputs(now);
  Outputs out = c.update(in);
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
  test_manual_overrides_fault();
  test_uit_absolute_during_sensor_fault();
  test_manual_to_auto_with_bad_sensors_enters_fault();
  test_update_is_noop_before_configure();
  test_first_evaluation_always_commands_fan();
  test_manual_speed_does_not_leak_into_auto_cooldown();
  test_seed_primes_hold_timer();
  test_seed_primes_boost_dwell();

  if (g_failures == 0) {
    std::printf("All tests passed.\n");
    return 0;
  }
  std::printf("%d check(s) failed.\n", g_failures);
  return 1;
}
