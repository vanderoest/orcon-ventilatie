# ARCHITECTURE.md

# Architecture

## Purpose

This project replaces the original Orcon controller PCB with an ESP32 running ESPHome.

The controller is responsible for:

* Driving the ventilation motor.
* Measuring fan RPM.
* Monitoring indoor air quality.
* Providing autonomous ventilation control.
* Allowing manual control through multiple interfaces.
* Integrating with Home Assistant and Apple HomeKit.

The controller must continue operating autonomously, even when Home Assistant is unavailable.

---

# Reference Implementation

The current ESPHome YAML configuration, located in `docs/`, is the functional reference implementation.

The behaviour of the controller must remain functionally identical during refactoring.

The objective of this project is **not** to redesign the controller logic, but to improve its architecture, readability, maintainability and reusability.

Any behavioural changes should be introduced only after the existing implementation has been faithfully reproduced.

---

# Design Principles

## Preserve Behaviour

The existing controller behaviour is considered the reference.

This includes:

* Automatic ventilation logic
* Manual operating modes
* Day and night profiles
* Threshold evaluation
* Hold timer behaviour
* Cooldown behaviour
* Home Assistant integration
* HomeKit integration
* ESPHome Web Server functionality

Refactoring must never unintentionally change controller behaviour.

---

## ESPHome First

ESPHome should provide as much functionality as possible.

Custom C++ code should only be introduced when it significantly improves maintainability, readability or enables functionality that cannot reasonably be expressed in YAML.

The long-term objective is to move the controller logic into a reusable ESPHome external component.

---

## Separation of Concerns

Responsibilities should be clearly separated.

### Hardware

Responsible for interacting with physical devices.

Examples:

* PWM output
* Fan RPM
* Air quality sensors
* Status LED

---

### Controller

Responsible for all decision making.

This includes:

* Operating mode
* Automatic ventilation logic
* Timer management
* State transitions
* Target fan speed

The controller should not directly depend on GPIO numbers or hardware implementation details.

---

### User Interfaces

Responsible for presenting and modifying controller state.

Examples include:

* ESPHome Web Server
* Home Assistant
* Apple HomeKit

User interfaces must not contain business logic.

---

# Single Source of Truth

At every moment there is exactly one desired operating state.

All outputs must derive from that state.

The controller must avoid duplicated logic and independent state machines.

---

# Event Driven

The controller reacts to events.

Typical events include:

* Sensor updates
* Manual mode changes
* Boot
* Periodic watchdog evaluation

Each event triggers a controller evaluation.

Hardware should only be updated when the desired operating state changes.

---

# Configuration

User configurable values belong in YAML.

Examples include:

* Thresholds
* Fan speeds
* Hold duration
* Cooldown duration
* Day and night configuration

Changing controller behaviour should not require modifying C++ source code.

---

# Logging

Logging should explain controller decisions.

Typical log messages answer questions such as:

* Why did the fan speed change?
* Which sensor triggered the decision?
* Which operating mode is active?
* Why was a transition ignored?

Logging exists primarily as a diagnostic tool.

---

# Reliability

The controller should always fail safely.

Invalid or unavailable sensor values must never result in undefined behaviour.

Whenever possible, the controller should continue operating using safe fallback behaviour.

---

# Future Direction

The desired end state is a reusable ESPHome external component.

The project YAML should eventually describe only:

* Hardware
* Configuration
* Entity definitions
* Home Assistant integration

The controller implementation itself should become reusable without modification across multiple ventilation systems.

---

# Operating Modes

The controller exposes the following operating modes.

| Mode       | Description                                                                                                                                                                       |
| ---------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **AUTO**   | Fully autonomous operation. The controller continuously evaluates air quality and adjusts the fan speed according to the configured thresholds, hold timer and day/night profile. |
| **RUST**   | Quiet manual mode intended for night-time or low-noise operation. The fan runs at a fixed low speed.                                                                              |
| **LAAG**   | Manual low-speed operation. The fan runs continuously at the configured low speed.                                                                                                |
| **MEDIUM** | Manual medium-speed operation. The fan runs continuously at the configured medium speed.                                                                                          |
| **HOOG**   | Manual high-speed operation. The fan runs continuously at the configured high speed.                                                                                              |
| **UIT**    | The fan is switched off regardless of sensor values.                                                                                                                              |

These operating modes are part of the controller's public interface.

The functional behaviour of these modes must remain identical to the reference implementation.

Only **AUTO** contains controller logic.

All other modes bypass the automatic controller and directly select a fixed fan speed.
