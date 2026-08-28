// Orcon ventilation controller — pure decision logic, ESPHome-free.
//
// Host-compilable: no Arduino/ESPHome headers, only the standard library.
// Loaded by the ESPHome external component in components/orcon. YAML is wiring only —
// gather Inputs, call update(), apply Outputs. No decisions live in YAML.
//
// See .plan §5 for the design this implements, and test/test_controller.cpp
// for the regression suite (host-run via g++, see that file's header).
#pragma once

#include <cstdint>
#include <cmath>

namespace orcon {

// Bump on every change to this header. It is logged at boot (orcon.yaml's
// priority-800 on_boot block), so the running firmware can be matched to an
// exact header revision. A stale copy sitting in an ESPHome build/config
// directory is otherwise completely invisible: the build succeeds, the build
// timestamp updates, and the old logic keeps running.
inline constexpr const char *kHeaderVersion = "2.1.0";

enum class Mode { AUTO, UIT, RUST, LAAG, MEDIUM, HOOG };

// IDLE/BOOST/HOLD is the sensor-driven cycle. FAULT and MANUAL are entered
// from any state and always return to IDLE (never BOOST/HOLD) on recovery,
// so a stale sensor or a manual excursion never leaves stale hold timers.
enum class State : int { IDLE = 0, BOOST = 1, HOLD = 2, FAULT = 3, MANUAL = 4 };

inline const char *state_name(State s) {
  switch (s) {
    case State::IDLE: return "IDLE";
    case State::BOOST: return "BOOST";
    case State::HOLD: return "HOLD";
    case State::FAULT: return "FAULT";
    case State::MANUAL: return "MANUAL";
  }
  return "UNKNOWN";
}

struct Config {
  // Hysteresis: assert (enter BOOST) / release (allow BOOST to end) per signal.
  // Assert values are v1.0's single thresholds, unchanged. Release values are
  // new in v2.0.0 — .plan §3 defaults, open question 1 unresolved by the user.
  float co2_assert = 800.0f, co2_release = 700.0f;
  float voc_assert = 150.0f, voc_release = 120.0f;
  float nox_assert = 5.0f, nox_release = 3.0f;
  float rh_assert = 60.0f, rh_release = 55.0f;

  // Shower (dRH/dt) detection — new in v2.0.0, .plan §3, open question 2 unresolved.
  float shower_rate_assert_pct = 3.0f;    // %RH rise within shower_window_ms
  float shower_rate_release_pct = 1.0f;
  float shower_release_margin_pct = 3.0f; // RH must also fall back within baseline + this
  uint32_t shower_window_ms = 300000;     // 5 min
  uint32_t rh_sample_min_interval_ms = 25000;

  // Fan speeds (%). Values unchanged from v1.0 substitutions.
  int speed_idle = 15;
  int speed_high_day = 40, speed_high_night = 30;
  int speed_hold_day = 35, speed_hold_night = 25;
  int speed_fault = 15;
  int speed_uit = 0, speed_rust = 15, speed_laag = 35, speed_medium = 55, speed_hoog = 85;

  // Day/night boundary, unchanged from v1.0 (open question 5: kept fixed schedule).
  int night_start_hour = 22, night_end_hour = 7;

  // Timing, all monotonic (millis()), never wall-clock.
  uint32_t hold_ms = 300000;            // unchanged from v1.0's hold_time_seconds
  uint32_t boost_min_dwell_ms = 60000;  // new in v2.0.0, .plan §3
  uint32_t cooldown_ms = 30000;         // unchanged from v1.0's cooldown_seconds
  uint32_t staleness_timeout_ms = 300000; // open question 7: reuses v1.0's 300s hold constant
};

struct Inputs {
  float voc = NAN, co2 = NAN, rh = NAN, nox = NAN;
  bool voc_ok = false, co2_ok = false, rh_ok = false, nox_ok = false;
  uint32_t now_ms = 0;
  int hour = 0;
  bool time_valid = true;
  Mode mode = Mode::AUTO;
};

struct Outputs {
  int target_speed = 15;
  State state = State::IDLE;
  bool fault = false;
  const char *reason = "init";
  bool speed_changed = false;
};

class Controller {
 public:
  explicit Controller(const Config &cfg = Config()) : cfg_(cfg) {}

  // Overrides the config (YAML substitutions are the source of truth on
  // device; defaults above are for host tests only).
  void configure(const Config &cfg) { cfg_ = cfg; configured_ = true; }

  // Primes state/speed from restored (flash-persisted) values at boot, and
  // primes the HOLD/BOOST timers relative to now_ms so a restored HOLD/BOOST
  // doesn't immediately expire/release on the first evaluation. Call
  // configure() first so cfg_.hold_ms reflects the YAML config, not defaults.
  void seed(State s, int speed, uint32_t now_ms) {
    state_ = s;
    current_speed_ = speed;
    hold_until_ms_ = now_ms + cfg_.hold_ms;
    boost_entered_ms_ = now_ms;
  }

  State state() const { return state_; }
  int current_speed() const { return current_speed_; }

  // Feeds the RH ring buffer used for shower (dRH/dt) detection. Safe to call
  // more often than the sensor's own cadence — a minimum-interval guard
  // keeps the buffer spanning shower_window_ms regardless of caller rate.
  void note_rh(uint32_t now_ms, float rh) {
    if (rh_hist_count_ > 0 && (now_ms - rh_last_push_ms_) < cfg_.rh_sample_min_interval_ms)
      return;
    rh_hist_[rh_hist_idx_] = {now_ms, rh, true};
    rh_hist_idx_ = (rh_hist_idx_ + 1) % kRhHistorySlots;
    rh_hist_count_++;
    rh_last_push_ms_ = now_ms;
  }

  Outputs update(const Inputs &in) {
    Outputs out;

    // Not yet configured (boot ordering race, .plan §2): explicit no-op that
    // cannot command the fan or mutate latches/cooldown. Returns before any
    // other state is touched.
    if (!configured_) {
      out.state = state_;
      out.target_speed = current_speed_;
      out.reason = "not_configured";
      out.speed_changed = false;
      out.fault = false;
      return out;
    }

    const bool night = is_night(in);
    const int high_speed = night ? cfg_.speed_high_night : cfg_.speed_high_day;
    const int hold_speed = night ? cfg_.speed_hold_night : cfg_.speed_hold_day;

    if (in.rh_ok) note_rh(in.now_ms, in.rh);

    const bool any_bad = !in.voc_ok || !in.co2_ok || !in.rh_ok || !in.nox_ok;

    // Manual modes never read the sensors, so sensor validity must not block
    // them (BUGFIX.md #1) — mode is checked before any_bad. any_bad only
    // gates the AUTO path, below.
    if (in.mode != Mode::AUTO) {
      state_ = State::MANUAL;
      out.reason = "manual_mode";
      out.target_speed = manual_speed(in.mode);
    } else {
      if (state_ == State::MANUAL || state_ == State::FAULT) {
        state_ = State::IDLE;
        clear_latches();
        // A manual excursion changes airflow across the RH sensor, so the
        // samples spanning it describe the fan, not the room. Keeping them
        // let the post-excursion rebound read as a shower (orcon8.log
        // 14:11:19: +4 %RH against a pre-excursion baseline -> BOOST). Same
        // reasoning as clear_latches(): a manual excursion must leave no
        // residue that steers AUTO. The buffer re-seeds on the next sample.
        clear_rh_history();
      }

      if (any_bad) {
        state_ = State::FAULT;
        out.reason = "sensor_stale_or_invalid";
        out.target_speed = cfg_.speed_fault;
      } else {
        const bool first = !evaluated_once_;
        const bool cooling_down = !first && (in.now_ms - last_eval_ms_ < cfg_.cooldown_ms);

        if (cooling_down) {
          out.reason = "cooldown";
          // Speed is always a function of (state, profile) — never the last
          // commanded value. Reusing current_speed_ here leaked the MANUAL
          // speed into AUTO: leaving MANUAL 85% during the cooldown window
          // reported state=IDLE while the fan kept running at 85% until the
          // cooldown expired (orcon6.log 13:44:22 → 13:44:46). It also
          // pinned the old profile's speed across a day/night rollover.
          out.target_speed = speed_for_state(state_, high_speed, hold_speed);
        } else {
          last_eval_ms_ = in.now_ms;
          evaluated_once_ = true;
          evaluate_latches(in);
          const bool any_high = co2_latch_ || voc_latch_ || nox_latch_ || rh_latch_ || shower_latch_;
          out.reason = step_state(in.now_ms, any_high);
          out.target_speed = speed_for_state(state_, high_speed, hold_speed);
        }
      }
    }

    out.state = state_;
    // Reports "control sensors are bad", independent of state — true in
    // MANUAL too, since MANUAL no longer implies sensors are fine.
    out.fault = any_bad;
    // The physical fan always boots off (orcon.yaml's restore_mode:
    // ALWAYS_OFF), regardless of what current_speed_ was seeded to. Without
    // this, a fresh boot whose first computed target (FAULT/IDLE, both
    // speed_idle) happens to equal the seeded current_speed_ (also
    // speed_idle) would never issue a real fan command — the software
    // believes it's already at the right speed while the hardware sits off.
    // Force exactly one real command on the first evaluation after
    // configure(), regardless of whether the target matches current_speed_.
    out.speed_changed = (out.target_speed != current_speed_) || !commanded_once_;
    commanded_once_ = true;
    current_speed_ = out.target_speed;
    return out;
  }

 private:
  static constexpr int kRhHistorySlots = 10; // 5 min @ >=30s cadence

  Config cfg_;
  bool configured_ = false;
  bool commanded_once_ = false;
  State state_ = State::IDLE;
  int current_speed_ = 15;
  bool evaluated_once_ = false;
  uint32_t last_eval_ms_ = 0;
  uint32_t hold_until_ms_ = 0;
  uint32_t boost_entered_ms_ = 0;

  bool co2_latch_ = false, voc_latch_ = false, nox_latch_ = false, rh_latch_ = false, shower_latch_ = false;

  struct RhSample { uint32_t t_ms; float rh; bool valid = false; };
  RhSample rh_hist_[kRhHistorySlots] = {};
  int rh_hist_idx_ = 0;
  int rh_hist_count_ = 0;
  uint32_t rh_last_push_ms_ = 0;

  const char *step_state(uint32_t now_ms, bool any_high) {
    switch (state_) {
      case State::IDLE:
        if (any_high) {
          state_ = State::BOOST;
          boost_entered_ms_ = now_ms;
          return "boost_triggered";
        }
        return "idle";
      case State::BOOST: {
        const bool dwell_met = (now_ms - boost_entered_ms_) >= cfg_.boost_min_dwell_ms;
        if (!any_high && dwell_met) {
          state_ = State::HOLD;
          hold_until_ms_ = now_ms + cfg_.hold_ms;
          return "hold_started";
        }
        return any_high ? "boost_active" : "boost_dwell";
      }
      case State::HOLD:
        if (any_high) {
          state_ = State::BOOST;
          boost_entered_ms_ = now_ms;
          return "boost_retrigger";
        }
        if (now_ms >= hold_until_ms_) {
          state_ = State::IDLE;
          return "hold_expired";
        }
        return "hold_active";
      default:
        state_ = State::IDLE;
        return "idle";
    }
  }

  void evaluate_latches(const Inputs &in) {
    if (in.co2 > cfg_.co2_assert) co2_latch_ = true;
    else if (in.co2 < cfg_.co2_release) co2_latch_ = false;

    if (in.voc > cfg_.voc_assert) voc_latch_ = true;
    else if (in.voc < cfg_.voc_release) voc_latch_ = false;

    if (in.nox > cfg_.nox_assert) nox_latch_ = true;
    else if (in.nox < cfg_.nox_release) nox_latch_ = false;

    if (in.rh > cfg_.rh_assert) rh_latch_ = true;
    else if (in.rh < cfg_.rh_release) rh_latch_ = false;

    float rate = 0.0f;
    const float baseline = rh_baseline(in.now_ms, in.rh, &rate);
    if (rate >= cfg_.shower_rate_assert_pct) {
      shower_latch_ = true;
    } else if (rate < cfg_.shower_rate_release_pct && in.rh < baseline + cfg_.shower_release_margin_pct) {
      shower_latch_ = false;
    }
  }

  float rh_baseline(uint32_t now_ms, float current_rh, float *rate_out) {
    float oldest_rh = current_rh;
    uint32_t oldest_age = 0;
    bool found = false;
    for (const auto &s : rh_hist_) {
      if (!s.valid) continue;
      const uint32_t age = now_ms - s.t_ms;
      if (age <= cfg_.shower_window_ms && age >= oldest_age) {
        oldest_age = age;
        oldest_rh = s.rh;
        found = true;
      }
    }
    if (rate_out) *rate_out = found ? (current_rh - oldest_rh) : 0.0f;
    return oldest_rh;
  }

  void clear_latches() { co2_latch_ = voc_latch_ = nox_latch_ = rh_latch_ = shower_latch_ = false; }

  // Drops every RH sample. rh_hist_count_ == 0 makes the next note_rh() push
  // immediately regardless of rh_sample_min_interval_ms, so the baseline
  // re-seeds from the first reading after the excursion rather than stalling.
  void clear_rh_history() {
    for (auto &s : rh_hist_) s.valid = false;
    rh_hist_idx_ = 0;
    rh_hist_count_ = 0;
    rh_last_push_ms_ = 0;
  }

  bool is_night(const Inputs &in) const {
    // No valid time: fail to the DAY profile (more ventilation) rather than
    // guessing — safety over quietness, per .plan §2.
    if (!in.time_valid) return false;
    const int h = in.hour;
    if (cfg_.night_start_hour < cfg_.night_end_hour)
      return h >= cfg_.night_start_hour && h < cfg_.night_end_hour;
    return h >= cfg_.night_start_hour || h < cfg_.night_end_hour;
  }

  int manual_speed(Mode m) const {
    switch (m) {
      case Mode::UIT: return cfg_.speed_uit;
      case Mode::RUST: return cfg_.speed_rust;
      case Mode::LAAG: return cfg_.speed_laag;
      case Mode::MEDIUM: return cfg_.speed_medium;
      case Mode::HOOG: return cfg_.speed_hoog;
      default: return cfg_.speed_idle;
    }
  }

  int speed_for_state(State s, int high_speed, int hold_speed) const {
    switch (s) {
      case State::IDLE: return cfg_.speed_idle;
      case State::BOOST: return high_speed;
      case State::HOLD: return hold_speed;
      case State::FAULT: return cfg_.speed_fault;
      default: return cfg_.speed_idle;
    }
  }
};

// Meyer's singleton: one controller instance shared across all YAML lambdas.
// `inline` keeps this header safe to include from multiple translation units.
inline Controller &instance() {
  static Controller c;
  return c;
}

} // namespace orcon
