# REFERENCE-IMPLEMENTATION.md

> **Historical / non-normative.** This document describes `docs/orcon-reference.yaml` **only** — the frozen v1.0 configuration, kept for historical reference and never edited again. It does not describe the live configuration.
>
> The live configuration is **`orcon.yaml`** (repo root), v2.0.0, specified by the current `FUNCTIONAL-SPEC.md`. v2.0.0 repairs the defects documented below (see `.plan` § Defect register) and adds autonomy/fail-safe guarantees v1.0 did not have. Do not use this document to validate `orcon.yaml`.

This document describes the Orcon controller implementation found in `docs/orcon-reference.yaml`, as it shipped in v1.0.

The YAML file is the single authoritative source for what it describes. This document is derived from that YAML and contains only factual descriptions of what is configured. It contains no design decisions, architectural recommendations, or inferred intentions — including no assessment of whether the described behaviour is correct (see `.plan` for that).

---

## Configuration

### Hardware Platform

- Board: `esp32dev`
- Framework: `esp-idf`

### GPIO Assignment

| GPIO | Function | Interface | Connected Hardware |
|------|----------|-----------|-------------------|
| GPIO4 | I²C SCL | i2c_sensor_1 | SHT4x, SGP4x, SCD4x |
| GPIO12 | UART TX | uart_sensor_2 | Not connected |
| GPIO13 | UART RX | uart_sensor_2 | Not connected |
| GPIO14 | Pulse counter | GPIO | Tachometer input |
| GPIO15 | PWM output | ledc | Fan motor speed control (inverted) |
| GPIO16 | I²C SDA | i2c_sensor_1 | SHT4x, SGP4x, SCD4x |
| GPIO18 | I²C SCL | i2c_sensor_2 | Not connected |
| GPIO19 | I²C SDA | i2c_sensor_2 | Not connected |
| GPIO25 | UART RX | uart_sensor_1 | Not connected |
| GPIO26 | UART TX | uart_sensor_1 | Not connected |
| GPIO33 | Status indicator | GPIO | LED output |

### UART Buses

| ID | RX | TX | Baud |
|---|---|---|------|
| uart_sensor_1 | GPIO25 | GPIO26 | 9600 |
| uart_sensor_2 | GPIO13 | GPIO12 | 9600 |

Neither UART bus is referenced by any sensor component in the YAML.

### I²C Buses

| ID | SDA | SCL | Frequency | Sensors |
|---|---|---|-----------|---------|
| i2c_sensor_1 | GPIO16 | GPIO4 | 400 kHz | SHT4x, SGP4x, SCD4x |
| i2c_sensor_2 | GPIO19 | GPIO18 | 400 kHz | None |

### Substitutions

All values are configured via YAML substitutions:

| Key | Value |
|-----|-------|
| night_mode_start | 22 |
| night_mode_end | 7 |
| voc_threshold | 150 |
| co2_threshold | 800 |
| humidity_threshold | 60 |
| nox_threshold | 5 |
| fan_speed_high_day | 40 |
| fan_speed_high_night | 30 |
| fan_speed_hold_day | 35 |
| fan_speed_hold_night | 25 |
| fan_speed_low | 15 |
| manual_idle | 15 |
| manual_low | 35 |
| manual_medium | 55 |
| manual_high | 85 |
| hold_time_seconds | 300 |
| cooldown_seconds | 30 |

### Global Variables

| ID | Type | Initial |
|---|---|---|
| fan_hold_until | time_t | 0 |
| last_evaluation_time | time_t | 0 |
| current_target_speed | int | 15 |
| auto_mode_active | bool | true |

### Sensors

#### SHT4x (i2c_sensor_1)

**Temperature** (id: `sht4x_air_temperature`)
- Update interval: 30 seconds
- Accuracy: 2 decimals
- Device class: temperature
- State class: measurement

**Humidity** (id: `sht4x_air_humidity`)
- Update interval: 30 seconds
- Accuracy: 2 decimals
- Filters: delta 2%, throttle_average 30s
- On value: execute `evaluate_air_quality`
- Device class: humidity
- State class: measurement

#### SGP4x (i2c_sensor_1)

Compensation: temperature from `sht4x_air_temperature`, humidity from `sht4x_air_humidity`
Update interval: 30 seconds

**VOC Index** (id: `sgp4x_voc_index`)
- Filters: delta 5%, throttle_average 30s
- On value: execute `evaluate_air_quality`
- Device class: volatile_organic_compounds
- State class: measurement

**NOx Index** (id: `sgp4x_nox_index`)
- Filters: delta 2%, throttle_average 30s
- On value: execute `evaluate_air_quality`
- Device class: nitrous_oxide
- State class: measurement

#### SCD4x (i2c_sensor_1)

Update interval: 30 seconds

**CO₂** (id: `scd4x_co2`)
- Filters: delta 25 ppm, throttle_average 30s
- On value: execute `evaluate_air_quality`
- Device class: carbon_dioxide
- State class: measurement

**Temperature** (id: `scd4x_temperature`)
- Accuracy: 2 decimals
- Device class: temperature
- State class: measurement
- No filters, no triggers

**Humidity** (id: `scd4x_humidity`)
- Accuracy: 2 decimals
- Device class: humidity
- State class: measurement
- No filters, no triggers

#### WiFi Signal Strength

**dB** (id: `wifi_signal_db`)
- Platform: wifi_signal
- Update interval: 30 seconds
- Unit: dB
- Entity category: diagnostic
- Device class: signal_strength

**Percentage** (unnamed)
- Platform: copy
- Source: `wifi_signal_db`
- Unit: %
- Filters: lambda `min(max(2 * (x + 100.0), 0.0), 100.0)`
- Entity category: diagnostic
- Device class: signal_strength

#### Fan RPM (id: `orcon_rpm`)

- Platform: pulse_counter
- Pin: GPIO14
- Unit: rpm
- Update interval: 5 seconds
- No filters, no triggers

### ESPHome Components

#### esphome

- name: orcon
- friendly_name: Orcon
- on_boot:
  - delay: 15s
  - select.set: ventilation_manual_control to "AUTO"
  - script.execute: evaluate_air_quality
  - fan.turn_on: fan_motor at speed 15

#### esp32

- board: esp32dev
- framework: esp-idf

#### logger

- level: DEBUG

#### wifi

- ssid: !secret wifi_ssid
- password: !secret wifi_password
- ap:
  - ssid: "Orcon_Fallback"
  - password: !secret wifi_password

#### captive_portal

Default ESPHome captive portal

#### time

- platform: homeassistant
- id: homeassistant_time

#### api

- encryption:
  - key: !secret api_encryption_key

#### ota

- platform: esphome
- password: !secret ota_password

#### web_server

- port: 80
- auth:
  - type: digest
  - username: !secret web_username
  - password: !secret web_password
- version: 3

#### status_led

- pin: GPIO33

#### output (PWM)

- id: orcon_fan
- platform: ledc
- pin: GPIO15
- inverted: true

#### fan

- id: fan_motor
- name: "Fan"
- platform: speed
- output: orcon_fan

#### select

- platform: template
- id: ventilation_manual_control
- name: "Manual Control"
- icon: "mdi:fan"
- optimistic: true
- initial_option: "RUST"
- options: UIT, AUTO, RUST, LAAG, MEDIUM, HOOG
- on_value:
  - logger.log: "Manual control changed to: %s"
  - script.execute: evaluate_air_quality

#### script (id: evaluate_air_quality)

- mode: single
- Triggered by:
  - esphome on_boot (after 15s delay)
  - select on_value (ventilation_manual_control)
  - sensor on_value (sht4x_air_humidity)
  - sensor on_value (sgp4x_voc_index)
  - sensor on_value (sgp4x_nox_index)
  - sensor on_value (scd4x_co2)
  - interval (every 2 minutes)

#### interval

- interval: 2min
- Executes: script.execute evaluate_air_quality

---

## Operating Modes

The select entity `ventilation_manual_control` contains six options:

| Option | Initial |
|--------|---------|
| UIT | No |
| AUTO | No |
| RUST | Yes |
| LAAG | No |
| MEDIUM | No |
| HOOG | No |

The initial option is "RUST".

---

## Runtime Behaviour

### Boot Sequence

When the system starts, the `on_boot` block executes:

1. Delay 15 seconds
2. Set `ventilation_manual_control` to "AUTO"
3. Execute `evaluate_air_quality` script
4. Turn on `fan_motor` at speed 15

### Script Execution (evaluate_air_quality)

The script executes when triggered by any of the configured events.

The script reads the current value of `ventilation_manual_control`.

#### If not AUTO:

Maps the mode to a fixed fan speed:
- UIT → 0
- RUST → 15
- LAAG → 35
- MEDIUM → 55
- HOOG → 85

Updates `fan_motor` to that speed only if `current_target_speed` differs from the target.

#### If AUTO:

Checks if `current_target_speed` is one of the valid AUTO speeds (15, 25, 30, 35, 40). If not, sets to 15 and clears hold timer.

Reads current time.

Calculates `is_night`:
- If night_mode_start (22) < night_mode_end (7):
  - is_night = (hour >= 22 && hour < 7)
- Else:
  - is_night = (hour >= 22 || hour < 7)

Selects day or night speeds based on is_night.

Checks cooldown: if `current_time - last_evaluation_time < 30`, skip evaluation.

Reads sensor values: `sgp4x_voc_index`, `scd4x_co2`, `sht4x_air_humidity`, `sgp4x_nox_index`.

If any sensor is NaN, skips evaluation.

Compares each sensor to its threshold:
- voc > 150?
- co2 > 800?
- humidity > 60?
- nox > 5?

Sets `any_high` = true if any sensor exceeds threshold.

#### Speed decision:

If `any_high` and `current_target_speed` ≠ high_speed:
- Set fan to high_speed

Else if not `any_high` and `current_target_speed` = high_speed:
- Set fan to hold_speed
- Set `fan_hold_until` = current_time + 300

Else if not `any_high` and `current_target_speed` = hold_speed:
- If current_time ≥ `fan_hold_until`:
  - Set fan to 15
  - Clear hold timer
- Else: no change

Else: no change

Updates `last_evaluation_time`.

Returns.

### Sensor Triggers

The humidity, VOC, NOx, and CO₂ sensors have `on_value` triggers that execute `evaluate_air_quality`.

These triggers pass delta filters:
- Humidity: delta 2%, throttle_average 30s
- VOC: delta 5%, throttle_average 30s
- NOx: delta 2%, throttle_average 30s
- CO₂: delta 25 ppm, throttle_average 30s

The throttle_average means no more than one trigger per 30 seconds from each sensor.

### Watchdog

Every 2 minutes, the interval block executes `evaluate_air_quality`.

This execution is still subject to the cooldown check within the script.

