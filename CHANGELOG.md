# Changelog

## v2.0.1

Bugfix round from field-log analysis. See `BUGFIX.md` for the findings and `BUGFIX.plan` for the implementation plan.

### Fixed

- **FAULT overriding MANUAL/UIT.** `Controller::update()` checked sensor validity before mode, so a manual selection (including `UIT`) made while any control sensor was stale/NaN was silently overridden to the FAULT idle speed. Mode is now checked first — manual modes never read the sensors and are never blocked by them. `Problem` now reports "control sensors are bad" independent of state (also visible while in MANUAL), rather than "state == FAULT" (`BUGFIX.md` #1).
- **Boot-time config/seed race.** A sensor's `on_value` or the mode select's `restore_value` could trigger a real evaluation before `on_boot`'s `configure()`/`seed()` had run, evaluating against the header's hardcoded defaults and an unseeded state instead of the YAML config and the restored globals. `configure()`/`seed()` now run at `on_boot` priority 800, before Wi-Fi/API and other components' `setup()`; `Controller::update()` is additionally a no-op until `configure()` has run, as a fail-safe independent of ESPHome's priority ordering. `seed()` now also primes the HOLD/BOOST timers relative to `millis()` at boot, so a restored HOLD or BOOST doesn't immediately expire/release on the first evaluation (`BUGFIX.md` #2).
- **Fan never actually started on a fresh boot** (found on-device testing v2.0.1 itself, `orcon5.log`). The default/seeded `current_speed_` (15) equals both the FAULT and IDLE speed (also 15), so `target_speed == current_speed_` on the very first evaluation and `speed_changed` stayed false — the controller believed it was already running at 15% while the physical fan, which always boots off (`restore_mode: ALWAYS_OFF`), was never actually commanded. It stayed silently off until a manual mode picked a *different* speed. `Controller::update()` now forces exactly one real fan command on the first evaluation after `configure()`, regardless of whether the computed target happens to match the seeded speed.

### Changed

- `humidity_disagreement_margin`: 15 → 10 points. Field data showed a stable 6–8.5 point gap between SHT4x and SCD4x; at 15 the `Sensor Disagreement` diagnostic could never fire for this sensor pairing (`BUGFIX.md` #3).

### Not changed (analysed, no action)

- `fan_speed_high_day` (40%) — ruled out as a cause of the observed long BOOST; CO₂ and tacho data confirm the fan was exchanging air correctly at commanded speed.
- Shower release/HOLD/dwell cycle and the hysteresis dead-band holding BOOST between the RH assert/release thresholds — both confirmed working as designed on-device.

### Known gap (design decision open, not implemented)

- Fixed absolute RH thresholds (60%/55%) don't survive seasonal baseline drift — a shower BOOST ran for ≈3 hours in humid weather before a window was opened. See `BUGFIX.md` item 8 / `BUGFIX.plan` §8 for four candidate solutions; none is scheduled until the open questions there are decided.

## v2.0.0

Repair round, not a redesign: fixes the defects found in v1.0 and adds the autonomy/fail-safe guarantees required going forward. See `.plan` for the full design record and defect register, `ARCHITECTURE.md` for the resulting system.

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

Initial working configuration, `docs/orcon-reference.yaml` — frozen, and the file itself is the reference for what it does. See `.plan` for the defects later found in it.
