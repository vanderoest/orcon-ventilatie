# Orcon ESP32 Controller

An ESPHome-based replacement controller for Orcon mechanical ventilation units.

This project replaces the original Orcon controller PCB with an ESP32 while preserving the existing motor, fan housing and ventilation system.

The controller provides fully autonomous ventilation based on indoor air quality while remaining fully integrated with Home Assistant, Apple HomeKit and the built-in ESPHome web interface.

The controller continues to operate autonomously even when Home Assistant is unavailable — this is a hard requirement, not best-effort: see `ARCHITECTURE.md`.

**Current version: 2.1.1.** See `CHANGELOG.md` for what changed from v1.0.

---

# Features

* ESP32-based replacement for the original Orcon controller
* Automatic ventilation based on:

  * CO₂
  * VOC
  * NOx
  * Relative humidity
* Configurable day and night profiles
* Hold timer after elevated air quality measurements
* Manual operating modes
* Home Assistant integration
* Apple HomeKit support (via Home Assistant)
* ESPHome Web Server
* OTA firmware updates
* Fan RPM monitoring
* Extensive diagnostic logging
* Hysteresis + minimum dwell (no threshold pumping)
* Dual time source (Home Assistant + SNTP), safe degradation if both are unavailable
* Fail-safe FAULT state on stale/invalid sensors (fan keeps running at a fixed idle speed)
* State and last-selected mode persist across reboot

---

# Operating Modes

| Mode       | Description                                               |
| ---------- | --------------------------------------------------------- |
| **AUTO**   | Fully automatic ventilation based on sensor measurements. |
| **RUST**   | Quiet manual mode intended for night-time operation.      |
| **LAAG**   | Manual low-speed operation.                               |
| **MEDIUM** | Manual medium-speed operation.                            |
| **HOOG**   | Manual high-speed operation.                              |
| **UIT**    | Fan disabled.                                             |

---

# Project Goals

v1.0 (`docs/orcon-reference.yaml`) was a functional but defective single-file ESPHome YAML. v2 is a **repair**, not a preservation exercise: known defects are fixed and explicit autonomy/fail-safe guarantees are added, while control decision logic moves out of YAML into a testable C++ header. See `CHANGELOG.md` for the defect-by-defect record.

Long-term objectives include:

* Reduced inline C++ code in YAML (done in v2.0.0 — see `components/orcon/orcon_controller.h`)
* Reusable, host-testable controller logic (done — see `test/test_controller.cpp`)
* An ESPHome external component, loadable directly from this GitHub repository (done in v2.1.0)
* Proportional CO₂ control (documented future option, not built yet)
* Commanded-vs-actual RPM fault detection (blocked on a calibration pass — see `ARCHITECTURE.md` § Known gaps)

---

# Repository Structure

```text
.
├── orcon.yaml                    # Live configuration (v2.1.1)
├── components/
│   └── orcon/
│       ├── __init__.py           # ESPHome external-component loader
│       └── orcon_controller.h    # Controller decision logic, ESPHome-free, host-testable
├── test/
│   └── test_controller.cpp       # Host regression tests (g++, no hardware)
├── docs/
│   └── orcon-reference.yaml      # Frozen v1.0 config — kept only as a fallback
├── ARCHITECTURE.md               # Behaviour, hardware and configuration reference
├── BUGFIX.md                     # Known open defects (fixed defects are in CHANGELOG.md)
├── TODO.md                       # Open hardware verification and design decisions
├── CHANGELOG.md
├── README.md
```

---

# Design Philosophy

* One source of truth: no control logic duplicated into HA automations.
* The ESP32 is autonomous by construction — HA/HomeKit are a layer on top, never a precondition.
* Decisions live in `components/orcon/orcon_controller.h`; YAML is wiring only (gather inputs → `update()` → apply outputs).
* Fail safe, not fail silent: a stale sensor forces a known-safe fan speed and raises a diagnostic, never freezes control.
* Keep custom C++ reusable and hardware-independent (host-compilable, no ESPHome/Arduino includes).

---

# Current Status

`orcon.yaml` (repo root) is the live configuration, version 2.1.1. `docs/orcon-reference.yaml` is frozen at v1.0, kept only so the original single-file approach can be recovered if ever wanted.

One open item: the seasonal RH baseline drift design decision (`BUGFIX.md` item 8) — analysed, not implemented, needs a decision before any code is written. Proportional control remains a longer-term option in `ARCHITECTURE.md` § Known gaps.

---

# External Component

Load the controller directly from GitHub in an ESPHome configuration:

```yaml
external_components:
  - source: github://vanderoest/orcon-ventilatie
    components: [orcon]
    refresh: 0s

orcon:
```

The `orcon:` entry activates the component and makes the controller types available to YAML lambdas. For local component development, use `type: local` with `path: components` instead of the GitHub source.

---

# Building and Flashing

```sh
cp secrets.yaml secrets.yaml           # fill in real wifi/api/ota/web credentials; gitignored
esphome config orcon.yaml              # validate
esphome run orcon.yaml                 # compile and flash
```

Host-run the controller logic tests (no hardware, no ESPHome):

```sh
make -C test
```

---

# License

MIT License.
