# Orcon ESP32 Controller

An ESPHome-based replacement controller for Orcon mechanical ventilation units.

This project replaces the original Orcon controller PCB with an ESP32 while preserving the existing motor, fan housing and ventilation system.

The controller provides fully autonomous ventilation based on indoor air quality while remaining fully integrated with Home Assistant, Apple HomeKit and the built-in ESPHome web interface.

The controller continues to operate autonomously even when Home Assistant is unavailable — this is a hard requirement, not best-effort: see `ARCHITECTURE.md`.

**Current version: 2.0.0.** See `CHANGELOG.md` for what changed from v1.0.

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

v1.0 (`docs/orcon-reference.yaml`) was a functional but defective single-file ESPHome YAML — see `.plan` § Defect register. v2.0.0 is a **repair**, not a preservation exercise: known defects are fixed and explicit autonomy/fail-safe guarantees are added, while control decision logic moves out of YAML into a testable C++ header.

Long-term objectives include:

* Reduced inline C++ code in YAML (done in v2.0.0 — see `include/orcon_controller.h`)
* Reusable, host-testable controller logic (done — see `test/test_controller.cpp`)
* An ESPHome external component (still a documented future option, not built yet)
* Proportional CO₂ control (documented future option, not built yet)
* Commanded-vs-actual RPM fault detection (blocked on a calibration pass — see `ARCHITECTURE.md` § Known gaps)

---

# Repository Structure

```text
.
├── orcon.yaml                    # Live configuration (v2.0.0)
├── include/
│   └── orcon_controller.h        # Controller decision logic, ESPHome-free, host-testable
├── test/
│   └── test_controller.cpp       # Host regression tests (g++, no hardware)
├── docs/
│   └── orcon-reference.yaml      # Frozen v1.0 config — kept only as a fallback
├── ARCHITECTURE.md               # Behaviour, hardware and configuration reference
├── BUGFIX.md                     # Open defects, tuning recommendations, pending on-device checks
├── CHANGELOG.md
├── .plan                         # Historical design record for the v1.0 -> v2.0.0 repair
├── README.md
└── CLAUDE.md
```

---

# Design Philosophy

* One source of truth: no control logic duplicated into HA automations.
* The ESP32 is autonomous by construction — HA/HomeKit are a layer on top, never a precondition.
* Decisions live in `include/orcon_controller.h`; YAML is wiring only (gather inputs → `update()` → apply outputs).
* Fail safe, not fail silent: a stale sensor forces a known-safe fan speed and raises a diagnostic, never freezes control.
* Keep custom C++ reusable and hardware-independent (host-compilable, no ESPHome/Arduino includes).

---

# Current Status

`orcon.yaml` (repo root) is the live configuration, version 2.0.0. `docs/orcon-reference.yaml` is frozen at v1.0, kept only so the original single-file approach can be recovered if ever wanted.

Open items: two known defects, settings recommendations from field logs, and the on-device verification and tacho calibration that still need the physical unit — all tracked in `BUGFIX.md`. Longer-term options (external component, proportional control) are in `ARCHITECTURE.md` § Known gaps.

---

# Building and Flashing

```sh
cp secrets.yaml.example secrets.yaml   # fill in real wifi/api/ota/web credentials; gitignored
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
