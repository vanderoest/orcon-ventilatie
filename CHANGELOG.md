# Changelog

## v2.0.0

Repair round, not a redesign: fixes the defects found in v1.0 and adds the autonomy/fail-safe guarantees required going forward. See `.plan` for the full design record and defect register, `TODO.md` for the step-by-step build log.

**Live config moved**: `docs/orcon-reference.yaml` (v1.0) is now frozen and never edited again. The live config is `orcon.yaml` in the repo root.

### Fixed

- `esphome config` compile failure from an undeclared `trigger_source` parameter on the watchdog interval (defect #1).
- Hold timer collapsing to ~one evaluation cycle instead of running its full 300 s (defect #2).
- Fan state inferred from speed-value equality, which broke across day/night rollovers and overlapped with manual speeds — replaced with an explicit state machine (defect #3).
- No time-source validity check; all timing broke silently when Home Assistant was unreachable (defect #4).
- NaN sensor reading caused control to freeze indefinitely with no alarm (defect #5).
- `last_evaluation_time` was written before the NaN check, so a NaN-aborted evaluation still consumed the cooldown window (defect #12).
- Boot's direct `fan.turn_on` bypassed controller bookkeeping (`current_target_speed`), and the select's `initial_option` fired an evaluation before the boot sequence finished (defect #13).
- `select.set: AUTO` forced unconditionally on every boot, discarding the user's last choice (defect #11).

### Added

- Explicit `ctrl_state` (IDLE/BOOST/HOLD/FAULT/MANUAL), persisted across reboot.
- Dual time source: SNTP alongside Home Assistant time, with a validity guard and fallback. All durations (cooldown, dwell, hold, staleness) moved from wall-clock to monotonic `millis()`.
- Per-signal hysteresis (assert/release thresholds) for CO₂, VOC, NOx, RH, replacing single-threshold comparison (defect #6, #7).
- Rate-of-change (dRH/dt) shower detection.
- Minimum 60 s dwell in BOOST before release is allowed.
- FAULT state: per-sensor staleness timeout, forced idle speed, `Problem` binary_sensor.
- Control logic extracted to `include/orcon_controller.h` — ESPHome-free, host-compilable, unit-tested via `test/test_controller.cpp` (defect #10).
- Presentation/control filter split: physical sensors carry only a light median filter for control; `platform: copy` sensors carry the old `delta`/`throttle_average` filters for HA display, preserving entity_ids (defect #8).
- `Commanded Fan Speed` sensor and `Sensor Disagreement` binary_sensor (SHT4x vs SCD4x cross-check) (defect #9, partial — see Known Gaps below).
- `restore_value` on the mode select and on the persisted globals; boot fallback is restore-or-AUTO instead of forced AUTO.

### Known gaps (not built this round)

- Commanded-vs-actual fan RPM fault detection: needs a calibration pass (RPM at each commanded speed) not yet run. `Fan Tacho` and `Commanded Fan Speed` are exposed for manual comparison; no `fan_fault` binary_sensor yet.
- Proportional CO₂ control and an ESPHome external component remain documented future options, not built.

### Open questions left unresolved by the user (defaults applied)

Per user instruction, `.plan`'s open questions were resolved using values already present in `docs/orcon-reference.yaml` (v1.0) or, where v1.0 had no equivalent, the plan's own stated defaults:

- Hysteresis release margins and shower trigger value: plan defaults (no field data available).
- Night idle speed: kept at 15% for both day and night — v1.0 had one idle speed with no night-specific value.
- Day/night boundary: kept the fixed 22:00/07:00 schedule from v1.0.
- Staleness timeout: 300 s, reusing v1.0's existing hold-timer constant as the budget.
- Tacho calibration: deferred; shipped in log/expose-only mode, no fault-raising yet.

## v1.0

Initial working configuration, `docs/orcon-reference.yaml`. See `REFERENCE-IMPLEMENTATION.md` for what it does and `.plan` for the defects later found in it.
