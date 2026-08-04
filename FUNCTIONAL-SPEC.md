# FUNCTIONAL-SPEC: Orcon Ventilation Controller Behavioural Specification

**Version:** 2.0.0 — describes `orcon.yaml` (the live config) plus `include/orcon_controller.h`.
**Supersedes:** the v1.0 version of this document, which described `docs/orcon-reference.yaml`. That file is frozen; see `REFERENCE-IMPLEMENTATION.md` for its behaviour as shipped.

## Purpose

This document specifies **what the controller does** — its observable behaviour and functional requirements — independent of ESPHome YAML wiring or hardware.

An implementation is correct if and only if its observable behaviour matches this specification and the state machine in `include/orcon_controller.h` (the authoritative decision engine; host-testable via `test/test_controller.cpp`).

---

## Controller Overview

The Orcon controller is an autonomous ventilation system. It regulates on sensors and time, with **no dependency on Home Assistant or the internet**. HA, physical buttons wired through HA, and HomeKit (via Homebridge) are a layer on top of the controller, not a precondition for it. If that layer goes down, automatic control keeps running, and manual control remains available through the ESPHome web GUI.

### Core Responsibilities

- **Autonomous ventilation control**: monitors CO₂, VOC, humidity, and NOx; boosts fan speed when air quality degrades, using hysteresis (not a single threshold) to avoid pumping.
- **Manual ventilation control**: user can override automation and select a fixed fan speed.
- **Day/night profiles**: different fan speeds during day (07:00–22:00) and night (22:00–07:00).
- **Time-source independence**: works from either Home Assistant time or onboard SNTP; degrades safely (to the DAY profile) if neither is valid, rather than breaking.
- **Fail-safe**: a stale or invalid sensor drives the controller to a fixed idle speed and raises a diagnostic, instead of freezing indefinitely.
- **Persistence**: the last selected mode and controller state survive a reboot.

---

## State Machine

The controller's state (`ctrl_state`) is one of five values, computed explicitly — **never** inferred from the current fan speed:

```
                 sensors clear + dwell met
        ┌──────────────────────────────────────┐
        v                                      │
     IDLE ──── any signal latched ────> BOOST ──┘
        ^                                 │ release: sensors clear AND dwell met
        │                                 v
        └──────── hold expired ───────── HOLD
        │
     FAULT  <── any input stale/NaN, from any state; -> IDLE on recovery
     MANUAL <── mode != AUTO, from any state; -> IDLE when mode returns to AUTO
```

- **IDLE**: idle speed, waiting for a trigger.
- **BOOST**: at least one sensor signal is latched high; fan at day/night high speed.
- **HOLD**: signals cleared and minimum dwell elapsed; fan at day/night hold speed until the hold timer expires, then → IDLE.
- **FAULT**: entered immediately from any state when any control sensor is NaN or stale beyond its timeout. Fan forced to idle speed (15%). Exits to IDLE once all sensors are valid again.
- **MANUAL**: entered immediately from any state whenever the mode select is not AUTO. Fan follows the mode's fixed speed. Returning to AUTO always re-enters at IDLE (never resumes a prior BOOST/HOLD).

A day/night rollover, or a manual excursion and return, changes the **speed** a state maps to — never the **state itself**. Crossing 22:00 or 07:00 while in BOOST or HOLD changes the commanded speed but does not restart or reset the state.

---

## Operating Modes

Selected via `ventilation_manual_control` (options: UIT, AUTO, RUST, LAAG, MEDIUM, HOOG).

| Mode | Behaviour |
|---|---|
| **AUTO** | State machine above governs fan speed. |
| **UIT** | Fan off (0%), fixed. |
| **RUST** | 15%, fixed. |
| **LAAG** | 35%, fixed. |
| **MEDIUM** | 55%, fixed. |
| **HOOG** | 85%, fixed. |

Any non-AUTO mode is the `MANUAL` state: sensor thresholds are not evaluated, and the fan is held at the mode's fixed speed regardless of air quality.

---

## Boot Behaviour

1. Delay 15 s for hardware stabilization.
2. Load tunables (thresholds, speeds, timing) from YAML substitutions into the controller.
3. Restore `ctrl_state` and `current_target_speed` from flash (`restore_value`) into the controller.
4. Run one evaluation, which issues the first fan command through the same code path as every other evaluation (no separate, unaccounted-for boot fan command).

The mode select (`ventilation_manual_control`) restores its own last value independently via ESPHome's `restore_value`. If no restored value exists (first-ever boot, or a cleared/corrupt restore), it falls back to `AUTO` — **not** the forced `AUTO` v1.0 applied unconditionally on every boot.

---

## Hysteresis and Dwell (AUTO mode)

Each signal has an independent assert/release latch — not a single threshold. `BOOST` is entered when any latch is set, and held while any latch remains set.

| Signal | Enter BOOST (assert) | Clear (release) |
|---|---|---|
| CO₂ | > 800 ppm | < 700 ppm |
| VOC index | > 150 | < 120 |
| NOx index | > 5 | < 3 |
| RH (absolute) | > 60 % | < 55 % |
| RH rate (shower) | ≥ +3 %RH within 5 min | < +1 %RH within 5 min **and** RH within 3 % of its pre-rise baseline |

A value between the release and assert thresholds does not change the latch — this is what prevents oscillation at the boundary.

**Minimum BOOST dwell**: 60 s. Even if all signals clear immediately, the controller stays in BOOST for at least 60 s before HOLD is allowed to begin.

**Hold duration**: 300 s, counted from HOLD entry. Unlike v1.0, the hold timer is set once on entry and is never reset by subsequent evaluations while still in HOLD — it always runs the full duration (v1.0 defect: the timer was cleared on almost every pass, collapsing the hold to ~one evaluation cycle).

---

## Time Source and Timing

- Two independent time sources: Home Assistant time and onboard SNTP. HA time is preferred; SNTP is the fallback. Either alone satisfies the autonomy requirement.
- All durations (cooldown, dwell, hold, staleness) are measured on `millis()` — monotonic, never wall-clock. Wall-clock time is used for exactly one thing: choosing the day/night profile.
- If neither time source is valid, the controller uses the **DAY profile** (more ventilation — safety over quietness) and a diagnostic (`Time Valid` binary_sensor) turns off to make the degradation visible.

## Cooldown

In AUTO mode only (unchanged from v1.0): evaluations are skipped if fewer than 30 s have elapsed since the last one, except the very first evaluation after boot. FAULT and MANUAL transitions are never gated by cooldown — a stale sensor or a mode change takes effect immediately.

## Fail-Safe

Each of the four control sensors (VOC, CO₂, RH, NOx) has its own staleness timer, reset whenever a non-NaN reading arrives. If any sensor is NaN, or its last valid reading is older than 5 minutes, the controller enters `FAULT`:

- Fan is forced to a fixed idle speed (15%) rather than freezing at its last commanded speed.
- The `Problem` binary_sensor (device_class `problem`) turns on.
- Recovery is automatic: once all four sensors are valid again, the next evaluation exits FAULT to IDLE.

---

## Diagnostics

| Entity | Purpose |
|---|---|
| `Controller State` (text_sensor) | Current `ctrl_state` (IDLE/BOOST/HOLD/FAULT/MANUAL). |
| `Last Decision Reason` (text_sensor) | Why the last evaluation did what it did (e.g. `boost_triggered`, `cooldown`, `hold_active`). |
| `Problem` (binary_sensor, device_class `problem`) | On while in FAULT. |
| `Time Valid` (binary_sensor) | Off when neither time source is valid. |
| `Sensor Disagreement` (binary_sensor, device_class `problem`) | On when SHT4x and SCD4x humidity or temperature readings diverge beyond a margin — indicates a miscalibrated or failing sensor. |
| `Commanded Fan Speed` (sensor) | The controller's current output, for correlating against `Fan Tacho` (RPM) by eye. |
| `Fan Tacho` (sensor, rpm) | Unchanged from v1.0. Present for future commanded-vs-actual fault detection — **not yet implemented**; see Known Gaps. |

---

## HA Presentation vs. Control Input

The four control sensors (RH, VOC, NOx, CO₂) are split into two paths:

- **Control** (internal, not exposed to HA): a light median filter (window 3), unthrottled, feeding the controller as fast as the sensor itself updates (30 s).
- **Presentation** (`platform: copy`, exposed to HA): carries the original v1.0 `name:`, `delta`, and `throttle_average` filters, so HA entity_ids and dashboard behaviour are unchanged.

---

## Known Gaps (documented, not built this round)

- **Commanded-vs-actual RPM fault detection**: requires a calibration pass (RPM at each commanded speed) that hadn't been run as of v2.0.0. The `Fan Tacho` and `Commanded Fan Speed` entities are exposed for manual/logged comparison; no `fan_fault` binary_sensor exists yet.
- **Proportional CO₂ control**: threshold + hysteresis only, as decided for this round. Documented as a future option.
- **ESPHome external component**: the header-extraction in `include/orcon_controller.h` was chosen over a full external component as the smaller step for this round. Remains a documented future option.

---

## Out of Scope

GPIO assignments, wiring, and other pure-hardware description live in `REFERENCE-IMPLEMENTATION.md` (as shipped in v1.0) and `orcon.yaml` itself (current).

---

## Compliance

An implementation is compliant if:

- The state machine and hysteresis/dwell behaviour above are observable exactly as described.
- `test/test_controller.cpp` passes (`g++ -std=c++17 -I include test/test_controller.cpp -o /tmp/t && /tmp/t` from the repo root, or `make -C test`).
- `esphome config orcon.yaml` succeeds.
- The autonomy and fail-safe behaviours (time-source independence, FAULT on sensor loss) hold when verified on-device per `.plan` § Verification.
