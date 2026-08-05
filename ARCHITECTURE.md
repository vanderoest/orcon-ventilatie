# ARCHITECTURE — Orcon Ventilation Controller

**Version:** 2.0.0 — describes the live configuration: `orcon.yaml` + `include/orcon_controller.h`.

This document specifies **what the controller does and how it is built**: observable behaviour, hardware wiring, code structure, and configuration reference. An implementation is correct if and only if its observable behaviour matches this document and the state machine in `include/orcon_controller.h`.

`docs/orcon-reference.yaml` is the frozen v1.0 predecessor, kept only so the original single-file approach can be recovered if ever wanted. It is never edited and this document does not describe it; see `.plan` for the defect register that motivated replacing it.

---

## Overview

The controller regulates ventilation autonomously from sensors and time, with **no dependency on Home Assistant or the internet**. HA, physical buttons wired through HA, and HomeKit (via Homebridge) are a layer on top, not a precondition. If that layer goes down, automatic control keeps running and manual control remains available through the ESPHome web GUI.

Core responsibilities:

- **Autonomous control** on CO₂, VOC, humidity and NOx, using hysteresis (not a bare threshold) to avoid pumping.
- **Manual override** to a fixed fan speed.
- **Day/night profiles** (day 07:00–22:00, night 22:00–07:00).
- **Time-source independence**: Home Assistant time or onboard SNTP; degrades to the DAY profile if neither is valid.
- **Fail-safe**: a stale or invalid sensor drives a fixed idle speed and raises a diagnostic instead of freezing.
- **Persistence**: last selected mode and controller state survive reboot.

---

## Code structure

Decision logic lives in C++, not YAML. This is the central architectural choice of v2.0.0: v1.0 held ~100 lines of inline C++ in YAML `lambda:` blocks, which could only be tested by flashing hardware.

| Path | Role |
|---|---|
| `include/orcon_controller.h` | All decision logic. ESPHome-free and hardware-free, so it compiles on a host. Exposes `Inputs` → `Controller::update()` → `Outputs`. |
| `orcon.yaml` | Wiring only: gather sensor values into `Inputs`, call `update()`, apply `Outputs` to the fan and diagnostic entities. No decisions. |
| `test/test_controller.cpp` | Host regression tests (`make -C test`), no hardware needed. |

`Controller` is held as a single shared instance via `orcon::instance()`. The YAML `on_boot:` block loads tunables from substitutions into `Config` and seeds state from the restored globals.

---

## Hardware

Board `esp32dev`, framework `esp-idf` (Open AIR Mini).

| GPIO | Function | Interface | Connected hardware |
|------|----------|-----------|--------------------|
| GPIO4 | I²C SCL | i2c_sensor_1 | SHT4x, SGP4x, SCD4x |
| GPIO12 | UART TX | uart_sensor_2 | Not connected |
| GPIO13 | UART RX | uart_sensor_2 | Not connected |
| GPIO14 | Pulse counter | GPIO | Tachometer input |
| GPIO15 | PWM output | ledc | Fan motor speed (inverted) |
| GPIO16 | I²C SDA | i2c_sensor_1 | SHT4x, SGP4x, SCD4x |
| GPIO18 | I²C SCL | i2c_sensor_2 | Not connected |
| GPIO19 | I²C SDA | i2c_sensor_2 | Not connected |
| GPIO25 | UART RX | uart_sensor_1 | Not connected |
| GPIO26 | UART TX | uart_sensor_1 | Not connected |
| GPIO33 | Status indicator | GPIO | LED output |

Both I²C buses run at 400 kHz; all three sensors sit on `i2c_sensor_1`. Both UART buses (9600 baud) are declared but unused by any component — retained from the reference board layout. Note that from Open AIR Mini v1.4.0 the sensor pins are swapped relative to older examples; the assignment above is the corrected one.

---

## State machine

State (`ctrl_state`) is one of five values, computed explicitly — **never** inferred from fan speed:

```
                 sensors clear + dwell met
        ┌──────────────────────────────────────┐
        v                                      │
     IDLE ──── any signal latched ────> BOOST ──┘
        ^                                 │ release: sensors clear AND dwell met
        │                                 v
        └──────── hold expired ───────── HOLD

     FAULT  <── any input stale/NaN, from any state; -> IDLE on recovery
     MANUAL <── mode != AUTO, from any state; -> IDLE when mode returns to AUTO
```

- **IDLE** — idle speed, waiting for a trigger.
- **BOOST** — at least one signal latched high; day/night high speed.
- **HOLD** — signals cleared and dwell elapsed; day/night hold speed until the hold timer expires, then → IDLE.
- **FAULT** — any control sensor NaN or stale beyond timeout. Fan forced to idle speed. Exits to IDLE when all sensors are valid again.
- **MANUAL** — mode select is not AUTO. Fan follows the mode's fixed speed. Returning to AUTO always re-enters at IDLE, never resuming a prior BOOST/HOLD.

Setpoint is a function of (state, profile), recomputed each evaluation. A day/night rollover, or a manual excursion and return, changes the **speed** a state maps to — never the **state**. Crossing 22:00 or 07:00 while in BOOST or HOLD changes commanded speed without restarting or resetting state.

---

## Operating modes

Selected via `ventilation_manual_control`.

| Mode | Behaviour |
|---|---|
| **AUTO** | State machine governs fan speed. |
| **UIT** | Fan off (0%), fixed. |
| **RUST** | 15%, fixed. |
| **LAAG** | 35%, fixed. |
| **MEDIUM** | 55%, fixed. |
| **HOOG** | 85%, fixed. |

Any non-AUTO mode is the MANUAL state: thresholds are not evaluated and the fan holds the mode's fixed speed regardless of air quality.

---

## Boot

1. Delay 15 s for hardware stabilization.
2. Load tunables from YAML substitutions into `Config`.
3. Restore `ctrl_state` and `current_target_speed` from flash into the controller.
4. Run one evaluation, which issues the first fan command through the same path as every other evaluation.

The mode select restores its own last value via `restore_value`. With no restored value (first boot, or cleared/corrupt restore) it falls back to `AUTO` — not the unconditional forced AUTO of v1.0.

---

## Hysteresis and dwell (AUTO)

Each signal has an independent assert/release latch. BOOST is entered when any latch sets and held while any remains set.

| Signal | Enter BOOST (assert) | Clear (release) |
|---|---|---|
| CO₂ | > 800 ppm | < 700 ppm |
| VOC index | > 150 | < 120 |
| NOx index | > 5 | < 3 |
| RH (absolute) | > 60 % | < 55 % |
| RH rate (shower) | ≥ +3 %RH within 5 min | < +1 %RH within 5 min **and** RH within 3 % of pre-rise baseline |

A value between release and assert leaves the latch unchanged — this is what prevents oscillation at the boundary.

**Minimum BOOST dwell** 60 s: even if all signals clear immediately, BOOST is held at least this long before HOLD may begin. **Hold duration** 300 s, set once on HOLD entry and never reset by later evaluations while still in HOLD.

Shower detection uses a 10-slot RH ring buffer spanning ~5 minutes, with a minimum sample interval so the window stays correct regardless of how often evaluations run. Its release compares against a *rolling* baseline, so after a long plateau the rate naturally reads ~0 and the rate latch clears on its own — leaving the absolute RH latch as the thing that holds BOOST until RH crosses its release threshold.

---

## Time source and timing

Two independent sources: Home Assistant time (preferred) and onboard SNTP (fallback). Either alone satisfies the autonomy requirement.

All durations — cooldown, dwell, hold, staleness — are measured on monotonic `millis()`, never wall-clock. Wall clock is used for exactly one thing: day/night profile selection. If neither source is valid, the **DAY profile** is used (more ventilation — safety over quietness) and the `Time Valid` binary_sensor turns off.

**Cooldown**: in AUTO only, evaluations are skipped if fewer than 30 s have elapsed since the last, except the first evaluation after boot. FAULT and MANUAL transitions are never gated — a stale sensor or mode change takes effect immediately.

---

## Fail-safe

Each control sensor (VOC, CO₂, RH, NOx) has its own staleness timer, reset on every non-NaN reading. If any sensor is NaN or its last valid reading is older than 5 minutes, the controller enters FAULT: fan forced to 15% rather than freezing at its last speed, and the `Problem` binary_sensor turns on. Recovery is automatic on the next evaluation once all four sensors are valid.

---

## Presentation vs. control input

The four control sensors are split into two paths:

- **Control** (`internal: true`, not exposed to HA): light median filter (window 3), unthrottled, feeding the controller as fast as the sensor updates (30 s).
- **Presentation** (`platform: copy`, exposed to HA): carries the original v1.0 `name:`, `delta` and `throttle_average` filters, so HA entity_ids and dashboard behaviour are unchanged.

This keeps display smoothing from degrading the control input, which in v1.0 shared a single filtered path.

---

## Entities

| Entity | Type | Purpose |
|---|---|---|
| `Manual Control` | select | Mode selection; `restore_value`. |
| `Fan` | fan (speed) | The fan itself. |
| `Controller State` | text_sensor | IDLE/BOOST/HOLD/FAULT/MANUAL. |
| `Last Decision Reason` | text_sensor | Why the last evaluation acted as it did. |
| `Problem` | binary_sensor (`problem`) | On while in FAULT. |
| `Time Valid` | binary_sensor | Off when neither time source is valid. |
| `Sensor Disagreement` | binary_sensor (`problem`) | SHT4x vs SCD4x humidity/temperature divergence. |
| `Commanded Fan Speed` | sensor (%) | Controller output, for comparison against tacho. |
| `Fan Tacho` | sensor (rpm) | Pulse counter on GPIO14, 5 s interval. |
| `SHT4x Humidity` / `SGP4x VOC Index` / `SGP4x NOx Index` / `SCD4x CO2` | sensor (copy) | HA presentation of the four control sensors. |
| `SHT4x Temperature`, `SCD4x Temperature`, `SCD4x Humidity` | sensor | Not in the control loop; used for the cross-check. |
| `WiFi Signal dB` / `WiFi Signal Percent` | sensor | Diagnostics. |

Sensor update interval is 30 s for SHT4x, SGP4x and SCD4x. SGP4x takes compensation from `sht4x_air_temperature` and `sht4x_air_humidity`, and requires ~90 samples of stabilization after boot before VOC/NOx read meaningfully.

---

## Evaluation triggers

`evaluate_air_quality` runs on: boot (after the delay), mode change (`on_value` of the select), each control sensor's `on_value`, and a 2-minute watchdog interval. The script is `mode: single`. The watchdog exists because filtered sensors may not fire `on_value` on slow drift; unlike v1.0 it passes no parameters.

---

## Configuration reference

All tunables are YAML substitutions in `orcon.yaml`, loaded into `Config` at boot.

| Key | Value | |
|---|---|---|
| `night_mode_start` / `night_mode_end` | 22 / 7 | Day/night boundary (hours) |
| `voc_assert` / `voc_release` | 150 / 120 | |
| `co2_assert` / `co2_release` | 800 / 700 | ppm |
| `humidity_assert` / `humidity_release` | 60 / 55 | %RH |
| `nox_assert` / `nox_release` | 5 / 3 | |
| `shower_rate_assert` / `shower_rate_release` | 3.0 / 1.0 | %RH within the 5-min window |
| `shower_release_margin` | 3.0 | %RH above baseline |
| `fan_speed_high_day` / `fan_speed_high_night` | 40 / 30 | % |
| `fan_speed_hold_day` / `fan_speed_hold_night` | 35 / 25 | % |
| `fan_speed_low` | 15 | % — idle, and the FAULT speed |
| `manual_uit` / `rust` / `laag` / `medium` / `hoog` | 0 / 15 / 35 / 55 / 85 | % |
| `hold_time_ms` | 300000 | |
| `boost_min_dwell_ms` | 60000 | |
| `cooldown_ms` | 30000 | |
| `staleness_timeout_ms` | 300000 | |
| `humidity_disagreement_margin` | 15 | points — see `BUGFIX.md`, likely too loose |
| `temperature_disagreement_margin` | 3 | °C |

Persisted globals: `ctrl_state` and `current_target_speed` (both `restore_value: true`). The four `*_last_valid_ms` staleness timers are runtime-only.

---

## Known gaps

- **Commanded-vs-actual RPM fault detection** — needs a calibration pass (RPM at each commanded speed) that has not been run. `Fan Tacho` and `Commanded Fan Speed` are exposed for manual comparison; there is no `fan_fault` entity.
- **Open defects** — see `BUGFIX.md` (FAULT overriding MANUAL/UIT; boot-time config/seed race).
- **Proportional CO₂ control** and an **ESPHome external component** remain documented future options, not built.

---

## Verification

- Host tests: `make -C test` (or `g++ -std=c++17 -I include test/test_controller.cpp -o /tmp/t && /tmp/t`).
- Config: `esphome config orcon.yaml`, and `esphome compile orcon.yaml` for the full ESP-IDF build.
- On device: the autonomy test (stop Home Assistant; confirm SNTP time, continued AUTO evaluations, correct profile, working web-GUI mode changes) and the fail-safe test (disconnect the I²C bus; confirm FAULT within the staleness timeout, fan at 15%, clean recovery). Neither has been run yet — see `BUGFIX.md` § Open verification.
