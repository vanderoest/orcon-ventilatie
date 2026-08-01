# Orcon ESP32 Controller

An ESPHome-based replacement controller for Orcon mechanical ventilation units.

This project replaces the original Orcon controller PCB with an ESP32 while preserving the existing motor, fan housing and ventilation system.

The controller provides fully autonomous ventilation based on indoor air quality while remaining fully integrated with Home Assistant, Apple HomeKit and the built-in ESPHome web interface.

The controller continues to operate autonomously even when Home Assistant is unavailable.

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

The current implementation is a functional ESPHome YAML configuration.

The goal of this repository is to evolve that implementation into a clean, modular and maintainable controller while preserving the existing behaviour.

Long-term objectives include:

* Improved software architecture
* Better separation of concerns
* Reduced inline C++ code
* Reusable controller logic
* An ESPHome external component
* YAML-first configuration

---

# Repository Structure

```text
.
├── docs/
│   └── orcon_reference.yaml      # Original reference implementation
├── external_components/          # Future reusable ESPHome component
├── README.md
├── ARCHITECTURE.md
├── CLAUDE.md
└── TODO.md
```

---

# Design Philosophy

This project follows a few simple principles:

* Preserve existing behaviour.
* Refactor before redesigning.
* Keep hardware and controller logic separated.
* Prefer declarative ESPHome YAML where practical.
* Keep custom C++ reusable and hardware-independent.
* Build towards a reusable ESPHome external component.

See `ARCHITECTURE.md` for the complete design principles.

---

# Current Status

The existing ESPHome YAML located in `docs/` is considered the functional reference implementation.

All refactoring work should reproduce the behaviour of this implementation before introducing new functionality.

---

# License

MIT License.
