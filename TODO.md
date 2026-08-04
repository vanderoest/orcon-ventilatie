# TODO — Orcon controller repair

Executable checklist derived from `.plan`. Steps 1–9 were implemented together in one pass (v2.0.0) rather than flashed incrementally step-by-step, since the header-based design made each step easy to verify in isolation via host tests before wiring into YAML. All open questions were resolved per user instruction: use the values already present in `docs/orcon-reference.yaml` (v1.0), or the plan's own stated defaults where v1.0 had no equivalent. See `CHANGELOG.md` for the resolutions applied.

## Step 0 — Deliverables
- [x] Write `.plan`
- [x] Write `TODO.md`

## Step 1 — Unblock and stop the bleeding
- [x] Create `orcon.yaml` in repo root, seeded from `docs/orcon-reference.yaml` (which stays frozen)
- [x] Remove `trigger_source: "watchdog"` from the interval's `script.execute` (defect #1)
- [x] Move `id(fan_hold_until) = 0;` inside the `if (ts >= id(fan_hold_until))` branch only (defect #2) — superseded: hold timer is now set once on HOLD entry in `orcon_controller.h`, never reset mid-hold
- [x] Move `id(last_evaluation_time) = ts;` to after the NaN check (defect #12) — superseded: staleness/NaN check now happens before the cooldown gate entirely
- [x] Verify: `esphome config orcon.yaml` succeeds
- [x] Flash and confirm boot still works — **not done: no physical unit available to this session; `esphome compile orcon.yaml` succeeded (ESP-IDF build, 53.5% flash used), but on-device flash/boot was not performed**

## Step 2 — Time source
- [x] Add `time: - platform: sntp` (id `sntp_time`) alongside `homeassistant_time`
- [x] Add a validity guard helper: prefer one source, fall back to the other, return validity flag
- [x] Convert cooldown, hold, dwell timers from `time_t`/wall-clock to `millis()`
- [x] Add `time_valid` diagnostic binary_sensor
- [ ] Verify: with HA/internet disconnected, log shows valid time and correct day/night profile — **not done: requires physical unit**

## Step 3 — Explicit state machine (still inline)
- [x] Add global `ctrl_state` (IDLE / BOOST / HOLD / FAULT / MANUAL), `restore_value: true`
- [x] Add `text_sensor` exposing `ctrl_state` for HA/web
- [x] Derive setpoint as function of (state, day/night profile) — never from setpoint equality
- [x] Delete `auto_mode_active` global (becomes a local)
- [x] Remove the clean-start speed-set membership check (obsolete once state is explicit)
- [x] Verify: crossing 22:00/07:00 while in BOOST or HOLD changes speed but not state — verified via `test_day_night_rollover_preserves_state` host test

## Step 4 — Persistence and boot
- [x] `restore_value: true` on `ventilation_manual_control` select
- [x] `restore_value: true` on relevant globals
- [x] Replace forced `select.set: "AUTO"` in `on_boot` with restore-or-AUTO-fallback
- [x] Route `on_boot`'s initial fan command through the controller so `current_target_speed` stays consistent (defect #13)
- [ ] Verify: power-cycle in HOOG, confirm it boots back into HOOG; corrupt/clear the restore value, confirm it falls back to AUTO — **not done: requires physical unit**

## Step 5 — Extract logic to C++ header
- [x] Create `include/orcon_controller.h`, ESPHome-free, host-compilable
- [x] Define `OrconInputs` / `OrconOutputs` / `OrconController::update()` per `.plan` §5
- [x] Rewire YAML lambdas to: gather inputs → `update()` → apply outputs (no decisions in YAML)
- [x] Create `test/test_controller.cpp` with a `g++` build command (or Makefile)
- [x] Regression tests: hold lasts 300s (defect #2), day/night rollover doesn't reset state (defect #3)
- [x] Verify: host tests pass (`make -C test` — 7/7 pass); `esphome config orcon.yaml` still succeeds (also `esphome compile` succeeds)

## Step 6 — Split presentation vs. control filters
- [x] Physical sensors (RH, VOC, NOx, CO₂): `internal: true`, drop `delta`/`throttle_average`, add light `median` filter for control use
- [x] Add `platform: copy` sensors for HA presentation, carrying the old `name:`, `delta`, `throttle_average`
- [ ] Verify: HA entity_ids unchanged (check each of the 4 entities pre/post) — **not done: requires a running HA instance paired to a flashed unit**

## Step 7 — Hysteresis, dwell, dRH/dt
- [x] Per-signal assert/release latches (CO₂, VOC, NOx, RH) per `.plan` §3 table — open question 1 unresolved by user, plan defaults applied
- [x] Minimum BOOST dwell (60s) before release allowed
- [x] RH ring buffer (10 slots @ 30s) for dRH/dt shower detection — open question 2 unresolved by user, plan default (+3%RH/5min) applied, no separate longer hold
- [x] Extend host tests: no oscillation at threshold ± noise, shower detection fires/clears correctly

## Step 8 — Fail-safe
- [x] Per-sensor staleness timer (open question 7 unresolved by user: reused v1.0's existing 300s hold constant as the timeout)
- [x] FAULT state: forced idle speed (15%), skip threshold logic
- [x] `problem` binary_sensor (device_class `problem`)
- [ ] Verify: disconnect I²C bus, confirm FAULT entry within timeout, fan sits at 15%, recovery clears FAULT — **not done: requires physical unit** (equivalent behaviour is covered on the host by `test_nan_enters_fault_and_recovers`)

## Step 9 — Tacho + cross-check diagnostics
- [ ] Run calibration pass: log RPM at commanded 0/15/25/30/35/40/55/85% — **not done: open question 3, requires physical unit and cannot be fabricated**
- [ ] Commanded-vs-actual RPM band check with grace window (~20s) after speed changes — **blocked on the calibration pass above; shipped in log/expose-only mode instead** (`Fan Tacho` + `Commanded Fan Speed` sensors present, no `fan_fault` yet)
- [ ] `fan_fault` binary_sensor — **not built, blocked on calibration data**
- [x] SHT4x vs SCD4x humidity/temperature cross-check, `sensor_disagreement` flag
- [ ] Verify: block the fan impeller briefly, confirm `fan_fault` raises — **not applicable yet, `fan_fault` not built**

## Step 10 — Documentation
- [x] Rewrite `FUNCTIONAL-SPEC.md` to specify the new (post-repair) behaviour
- [x] Add header to `REFERENCE-IMPLEMENTATION.md` marking it as describing the frozen `docs/orcon-reference.yaml` only, non-normative
- [x] Fix `README.md`: remove dead `ARCHITECTURE.md` link, update "preserve existing behaviour" framing, point to `orcon.yaml` as the live config
- [x] Add `CHANGELOG.md` documenting v1.0 → v2.0.0

## Remaining before this can be called done
- On-device flashing and the on-unit verification steps marked "not done" above (Steps 1, 2, 4, 6, 8) — this session had no physical unit.
- Tacho calibration pass (Step 9) — needs the physical unit running for a period across the commanded-speed range; cannot be fabricated.
- Once calibration data exists: commanded-vs-actual RPM band check and `fan_fault` binary_sensor.
