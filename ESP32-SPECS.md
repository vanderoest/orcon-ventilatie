# ESP32-SPECS: Orcon Controller Hardware & ESPHome Specification

Complete technical specification of the existing ESP32-based Orcon ventilation controller implementation. This document is derived from the reference implementation (`docs/orcon-reference.yaml`) and serves as the authoritative source for all hardware configuration and ESPHome services that must be preserved during firmware rewrites.

---

## Hardware

### Platform
- **Board**: `esp32dev`
- **Framework**: ESP-IDF

### UART Buses

| ID | RX Pin | TX Pin | Baud Rate | Status | Purpose |
|---|---|---|---|---|---|
| `uart_sensor_1` | GPIO25 | GPIO26 | 9600 | Declared but unused | Sensor interface (reserved) |
| `uart_sensor_2` | GPIO13 | GPIO12 | 9600 | Declared but unused | Sensor interface (reserved) |

**Note**: Neither UART bus is actively used by any sensor or component in the reference implementation. Both are declared in the configuration but no serial sensors are configured to use them.

### I²C Buses

| ID | SDA Pin | SCL Pin | Frequency | Scan | Active Sensors |
|---|---|---|---|---|---|
| `i2c_sensor_1` | GPIO16 | GPIO4 | 400 kHz | No | SHT4x, SGP4x, SCD4x |
| `i2c_sensor_2` | GPIO19 | GPIO18 | 400 kHz | No | None (declared but unused) |

**Note**: `i2c_sensor_2` is configured but not referenced by any sensor component. `i2c_sensor_1` carries all three air quality sensors.

### PWM Output

| ID | Platform | Pin | Inverted | Direction | Connected Hardware |
|---|---|---|---|---|---|
| `orcon_fan` | ledc | GPIO15 | Yes | Output | Fan motor speed control |

The PWM output is controlled via the `fan_motor` component and drives the ventilation motor speed (0–100% duty cycle, inverted signal).

### GPIO Summary

| GPIO | Function | Direction | Interface | Connected Hardware | Notes |
|---|---|---|---|---|---|
| GPIO4 | I²C SCL | Open-drain | i2c_sensor_1 | SHT4x, SGP4x, SCD4x | 400 kHz, scan disabled |
| GPIO12 | UART TX | Output | uart_sensor_2 | (Unused) | Serial sensor bus 2 |
| GPIO13 | UART RX | Input | uart_sensor_2 | (Unused) | Serial sensor bus 2 |
| GPIO14 | Pulse counter | Input | GPIO | Fan tachometer | RPM measurement, 5s update |
| GPIO15 | PWM output | Output | PWM (ledc) | Fan motor | Motor speed control, inverted |
| GPIO16 | I²C SDA | Open-drain | i2c_sensor_1 | SHT4x, SGP4x, SCD4x | 400 kHz, scan disabled |
| GPIO18 | I²C SCL | Open-drain | i2c_sensor_2 | (Unused) | Spare I²C bus |
| GPIO19 | I²C SDA | Open-drain | i2c_sensor_2 | (Unused) | Spare I²C bus |
| GPIO25 | UART RX | Input | uart_sensor_1 | (Unused) | Serial sensor bus 1 |
| GPIO26 | UART TX | Output | uart_sensor_1 | (Unused) | Serial sensor bus 1 |
| GPIO33 | Status LED | Output | GPIO | ESP32 status indicator | WiFi/API status (standard ESPHome behavior) |

### Status LED
- **Pin**: GPIO33
- **Function**: Standard ESPHome status indicator (WiFi connection, API state)
- **Configuration**: Default ESPHome behavior (no custom parameters)

### Fan Tachometer
- **Platform**: `pulse_counter`
- **Pin**: GPIO14
- **Update Interval**: 5 seconds
- **Unit**: RPM
- **ID**: `orcon_rpm`
- **Filters**: None
- **Triggers**: None (diagnostic only, not part of automation logic)

### Motor Output
- **Platform**: `ledc` (PWM)
- **Pin**: GPIO15
- **Inverted**: Yes
- **Controlled by**: `fan_motor` (speed-based fan component)
- **Speed range**: 0–100%

---

## Sensors

### SHT4x Temperature & Humidity Sensor

| Property | Value |
|---|---|
| **Component** | sht4x |
| **I²C Bus** | i2c_sensor_1 |
| **Update Interval** | 30 seconds |
| **Compensation** | None |

#### Temperature
- **ID**: `sht4x_air_temperature`
- **Name**: "SHT4x Temperature"
- **Unit**: °C
- **Accuracy**: 2 decimals
- **Filters**: None |
| **Triggers**: None (not part of automation) |
| **Device Class**: temperature |
| **State Class**: measurement |

#### Humidity
- **ID**: `sht4x_air_humidity`
- **Name**: "SHT4x Humidity"
- **Unit**: %
- **Accuracy**: 2 decimals
- **Filters**: delta 2%, throttle_average 30s
- **Triggers**: `on_value` → execute `evaluate_air_quality`
- **Device Class**: humidity
- **State Class**: measurement
- **Purpose**: Primary humidity measurement; triggers controller evaluation when humidity changes by >2%

---

### SGP4x Air Quality Sensor (VOC & NOx)

| Property | Value |
|---|---|
| **Component** | sgp4x |
| **I²C Bus** | i2c_sensor_1 |
| **Update Interval** | 30 seconds |
| **Compensation** | Temperature (from `sht4x_air_temperature`), Humidity (from `sht4x_air_humidity`) |

#### VOC (Volatile Organic Compounds) Index
- **ID**: `sgp4x_voc_index`
- **Name**: "SGP4x VOC Index"
- **Unit**: (dimensionless index)
- **Filters**: delta 5%, throttle_average 30s
- **Triggers**: `on_value` → execute `evaluate_air_quality`
- **Device Class**: volatile_organic_compounds
- **State Class**: measurement
- **Purpose**: Air quality indicator; triggers controller evaluation when VOC changes by >5%

#### NOx (Nitrogen Oxides) Index
- **ID**: `sgp4x_nox_index`
- **Name**: "SGP4x NOx Index"
- **Unit**: (dimensionless index)
- **Filters**: delta 2%, throttle_average 30s
- **Triggers**: `on_value` → execute `evaluate_air_quality`
- **Device Class**: nitrous_oxide
- **State Class**: measurement
- **Purpose**: Air quality indicator; triggers controller evaluation when NOx changes by >2%

---

### SCD4x CO₂ Sensor

| Property | Value |
|---|---|
| **Component** | scd4x |
| **I²C Bus** | i2c_sensor_1 |
| **Update Interval** | 30 seconds |

#### CO₂ Concentration
- **ID**: `scd4x_co2`
- **Name**: "SCD4x CO2"
- **Unit**: ppm
- **Filters**: delta 25 ppm, throttle_average 30s
- **Triggers**: `on_value` → execute `evaluate_air_quality`
- **Device Class**: carbon_dioxide
- **State Class**: measurement
- **Purpose**: Primary CO₂ measurement; triggers controller evaluation when CO₂ changes by >25 ppm

#### Temperature (Diagnostic)
- **ID**: `scd4x_temperature`
- **Name**: "SCD4x Temperature"
- **Unit**: °C
- **Accuracy**: 2 decimals
- **Filters**: None
- **Triggers**: None (diagnostic only)
- **Device Class**: temperature
- **State Class**: measurement
- **Purpose**: Diagnostic; not part of automation logic

#### Humidity (Diagnostic)
- **ID**: `scd4x_humidity`
- **Name**: "SCD4x Humidity"
- **Unit**: %
- **Accuracy**: 2 decimals
- **Filters**: None
- **Triggers**: None (diagnostic only)
- **Device Class**: humidity
- **State Class**: measurement
- **Purpose**: Diagnostic; not part of automation logic

---

### WiFi Signal Strength

#### Signal Strength (dB)
- **Platform**: wifi_signal
- **ID**: `wifi_signal_db`
- **Name**: "WiFi Signal dB"
- **Update Interval**: 30 seconds
- **Unit**: dB
- **Entity Category**: diagnostic
- **Device Class**: signal_strength
- **Purpose**: Diagnostic indicator of WiFi connection quality

#### Signal Strength (Percentage)
- **Platform**: copy
- **Source**: `wifi_signal_db`
- **ID**: (unnamed)
- **Name**: "WiFi Signal Percent"
- **Update Interval**: 30 seconds (inherited from source)
- **Unit**: %
- **Filters**: Lambda conversion: `min(max(2 * (x + 100.0), 0.0), 100.0)`
- **Entity Category**: diagnostic
- **Device Class**: signal_strength
- **Purpose**: Converts dB measurement to percentage for readability; diagnostic only

---

## ESPHome Components

### Core System

#### esphome
- **Purpose**: System initialization and boot sequence
- **Configuration**: 
  - Name: `orcon`
  - Friendly name: `Orcon`
  - On boot: 15s delay → set manual control to AUTO → execute `evaluate_air_quality` → turn on fan at 15%
- **Mandatory for rewrite**: Yes
- **Contribution**: Sets boot behavior and device identity

#### esp32
- **Purpose**: ESP32 platform configuration
- **Configuration**: 
  - Board: `esp32dev`
  - Framework: ESP-IDF
- **Mandatory for rewrite**: Yes
- **Contribution**: Specifies hardware platform and build framework

#### logger
- **Purpose**: Debugging and diagnostic logging
- **Configuration**: 
  - Level: DEBUG
- **Mandatory for rewrite**: Yes
- **Contribution**: Provides visibility into system behavior via serial/web console

---

### Network & Connectivity

#### wifi
- **Purpose**: WiFi network connection
- **Configuration**: 
  - SSID and password (from secrets)
  - Fallback AP: `Orcon_Fallback` with configurable password
- **Mandatory for rewrite**: Yes
- **Contribution**: Enables WiFi connectivity and local recovery access

#### captive_portal
- **Purpose**: Fallback web interface for setup when WiFi disconnected
- **Configuration**: Default ESPHome captive portal
- **Mandatory for rewrite**: Yes
- **Contribution**: Allows setup and WiFi configuration when primary network unavailable

#### time
- **Purpose**: Provides current time for time-based logic and day/night profile switching
- **Platform**: homeassistant
- **Configuration**: 
  - ID: `homeassistant_time`
  - Source: Home Assistant time service
- **Mandatory for rewrite**: Yes
- **Contribution**: Enables day/night profile switching in autonomous controller logic

---

### Home Assistant Integration

#### api
- **Purpose**: Home Assistant API communication
- **Configuration**: 
  - Encryption enabled with key from secrets
- **Mandatory for rewrite**: Yes
- **Contribution**: Enables bidirectional communication with Home Assistant, exposes entities for automation and dashboards

---

### OTA (Over-The-Air) Updates

#### ota
- **Purpose**: Firmware updates without physical access
- **Platform**: esphome
- **Configuration**: 
  - Password-protected (from secrets)
- **Mandatory for rewrite**: Yes
- **Contribution**: Enables remote firmware updates and maintenance

---

### Web Interface

#### web_server
- **Purpose**: Built-in ESPHome web interface
- **Configuration**: 
  - Port: 80
  - Authentication: Digest (username and password from secrets)
  - Version: 3 (latest)
- **Mandatory for rewrite**: Yes
- **Contribution**: Provides local web UI for status monitoring, diagnostics, and manual control

---

### User Control Interface

#### select (Manual Control)
- **Platform**: template
- **ID**: `ventilation_manual_control`
- **Name**: "Manual Control"
- **Icon**: mdi:fan
- **Optimistic**: true
- **Initial Option**: "RUST"
- **Options**: 
  - UIT (off)
  - AUTO (automatic)
  - RUST (quiet/night mode)
  - LAAG (low speed)
  - MEDIUM (medium speed)
  - HOOG (high speed)
- **Triggers**: 
  - `on_value`: Log mode change, execute `evaluate_air_quality`
- **Mandatory for rewrite**: Yes
- **Contribution**: Exposes operating mode selection to Home Assistant, HomeKit, and local web interface

---

### Hardware Control

#### status_led
- **Purpose**: Visual WiFi/API status indicator
- **Pin**: GPIO33
- **Configuration**: Default ESPHome status behavior
- **Mandatory for rewrite**: Yes
- **Contribution**: Provides LED feedback for connectivity status

#### output (PWM)
- **ID**: `orcon_fan`
- **Platform**: ledc
- **Pin**: GPIO15
- **Inverted**: true
- **Mandatory for rewrite**: Yes
- **Contribution**: Drives motor speed via PWM signal

#### fan (Speed-based)
- **ID**: `fan_motor`
- **Name**: "Fan"
- **Platform**: speed
- **Output**: `orcon_fan`
- **Mandatory for rewrite**: Yes
- **Contribution**: Provides fan speed control interface (0–100%) to scripts and Home Assistant

---

### Automation & Logic

#### script (Air Quality Evaluation)
- **ID**: `evaluate_air_quality`
- **Mode**: single (one execution at a time, queued)
- **Purpose**: Central decision logic for autonomous ventilation control
- **Execution triggers**: 
  1. On boot (after 15s delay)
  2. Manual control mode change
  3. SHT4x humidity change (>2% delta)
  4. SGP4x VOC change (>5% delta)
  5. SGP4x NOx change (>2% delta)
  6. SCD4x CO₂ change (>25 ppm delta)
  7. 2-minute watchdog interval
- **Mandatory for rewrite**: Yes
- **Contribution**: Implements automatic ventilation logic, manual mode routing, hold timer management, and day/night profile selection

#### interval (Watchdog)
- **Interval**: 2 minutes
- **Trigger**: Execute `evaluate_air_quality` with `trigger_source: "watchdog"`
- **Purpose**: Periodic evaluation fallback; ensures evaluation even when sensor changes are small (below delta thresholds)
- **Mandatory for rewrite**: Yes
- **Contribution**: Provides minimum evaluation frequency to detect slow air quality changes and prevent stale controller state

---

### Global State

#### globals
- **ID**: `fan_hold_until`
  - **Type**: time_t (timestamp)
  - **Initial**: 0
  - **Purpose**: Tracks hold timer expiration time; resets when transitioning from high speed
  
- **ID**: `last_evaluation_time`
  - **Type**: time_t (timestamp)
  - **Initial**: 0
  - **Purpose**: Tracks time of last controller evaluation; used for cooldown gating
  
- **ID**: `current_target_speed`
  - **Type**: int (0–100%)
  - **Initial**: 15
  - **Purpose**: Tracks desired fan speed; compared against actual speed to detect needed changes
  
- **ID**: `auto_mode_active`
  - **Type**: bool
  - **Initial**: true
  - **Purpose**: Cached flag indicating whether AUTO mode is selected; used by evaluation script

---

## Automation

The controller is fully event-driven. The single central logic routine (`evaluate_air_quality` script) is triggered by the following events:

### Automation Triggers

| # | Trigger | Source | When | Purpose |
|---|---|---|---|---|
| 1 | Boot | esphome `on_boot` | System startup after 15s delay | Force AUTO mode, execute initial evaluation, turn on fan at 15% |
| 2 | Mode change | `select.on_value` (ventilation_manual_control) | User selects operating mode | Log selection, re-evaluate controller state (switch between auto/manual or change manual speed) |
| 3 | Humidity update | `sensor.on_value` (sht4x_air_humidity) | Humidity changes >2% RH (throttled to 30s) | Trigger evaluation; may indicate occupancy or moisture event |
| 4 | VOC update | `sensor.on_value` (sgp4x_voc_index) | VOC changes >5% (throttled to 30s) | Trigger evaluation; may indicate air quality degradation |
| 5 | NOx update | `sensor.on_value` (sgp4x_nox_index) | NOx changes >2% (throttled to 30s) | Trigger evaluation; may indicate air quality degradation |
| 6 | CO₂ update | `sensor.on_value` (scd4x_co2) | CO₂ changes >25 ppm (throttled to 30s) | Trigger evaluation; primary air quality indicator |
| 7 | Watchdog tick | `interval: 2min` | Every 2 minutes | Periodic fallback evaluation; catches slow air quality changes below delta thresholds |

**Note**: Each trigger executes the same script (`evaluate_air_quality`). The script handles both automatic and manual mode routing internally. Sensor triggers are throttled (30s minimum between updates) to prevent excessive script execution.

---

## Configuration

All configurable values are defined as YAML substitutions and global variables.

### Substitutions (Day/Night Profile)

| Key | Value | Purpose | Unit |
|---|---|---|---|
| `night_mode_start` | 22 | Start of night window (24h format) | hour |
| `night_mode_end` | 7 | End of night window (24h format) | hour |

The controller automatically switches fan speeds based on current time. Night mode is active from 22:00 to 07:00. When night_mode_start > night_mode_end (e.g., 22 > 7), night wraps across midnight.

### Substitutions (Sensor Thresholds)

| Key | Value | Purpose | Unit |
|---|---|---|---|
| `voc_threshold` | 150 | VOC level triggering high-speed fan | VOC index (dimensionless) |
| `co2_threshold` | 800 | CO₂ level triggering high-speed fan | ppm |
| `humidity_threshold` | 60 | Humidity level triggering high-speed fan | % RH |
| `nox_threshold` | 5 | NOx level triggering high-speed fan | NOx index (dimensionless) |

When any sensor exceeds its threshold, the fan accelerates to high speed (day or night profile). When all sensors fall below thresholds, the fan enters hold mode.

### Substitutions (Auto Mode Fan Speeds)

| Key | Value | Purpose | Unit |
|---|---|---|---|
| `fan_speed_high_day` | 40 | Fan speed during day when thresholds exceeded | % |
| `fan_speed_high_night` | 30 | Fan speed during night when thresholds exceeded | % |
| `fan_speed_hold_day` | 35 | Fan speed during day hold period after high state | % |
| `fan_speed_hold_night` | 25 | Fan speed during night hold period after high state | % |
| `fan_speed_low` | 15 | Idle fan speed (clean air) | % |

In AUTO mode, the fan follows this pattern:
- At idle (all sensors below thresholds): `fan_speed_low` (15%)
- When threshold exceeded: jump to `fan_speed_high_day` or `fan_speed_high_night`
- When thresholds clear: transition to `fan_speed_hold_day` or `fan_speed_hold_night` for hold period
- After hold expires: return to `fan_speed_low`

### Substitutions (Manual Mode Fan Speeds)

| Key | Value | Purpose | Unit |
|---|---|---|---|
| `manual_idle` | 15 | RUST mode speed (quiet night operation) | % |
| `manual_low` | 35 | LAAG mode speed | % |
| `manual_medium` | 55 | MEDIUM mode speed | % |
| `manual_high` | 85 | HOOG mode speed | % |

In manual modes (RUST, LAAG, MEDIUM, HOOG, UIT), the fan runs at a fixed speed regardless of air quality. UIT (off) uses implicit speed 0.

### Substitutions (Timing)

| Key | Value | Purpose | Unit |
|---|---|---|---|
| `hold_time_seconds` | 300 | Duration to remain at hold speed after high threshold clears | seconds (5 min) |
| `cooldown_seconds` | 30 | Minimum interval between successive evaluations | seconds |

The hold timer ensures the fan continues running briefly after air quality recovers, allowing residual pollutants to exit. The cooldown timer prevents excessive script execution when multiple sensor updates arrive in rapid succession.

### Global Variables

All global variables are initialized in the `globals:` section:

| ID | Type | Initial | Purpose |
|---|---|---|---|
| `fan_hold_until` | time_t | 0 | Unix timestamp when hold period expires; 0 = not in hold |
| `last_evaluation_time` | time_t | 0 | Unix timestamp of last script execution |
| `current_target_speed` | int | 15 | Last commanded fan speed; used to detect when new command needed |
| `auto_mode_active` | bool | true | Cached flag: true = AUTO mode active, false = manual mode active |

---

## External Interfaces

The Orcon controller exposes the following interfaces for monitoring, control, and integration:

### Home Assistant API

- **Type**: ESPHome native API
- **Protocol**: TCP, encrypted
- **Port**: Not explicitly specified (ESPHome default: 6053)
- **Authentication**: Encryption key (from secrets)
- **Entities Exposed**: All sensors, manual control select, fan speed
- **Bidirectional**: Yes (can read state and send commands)
- **Purpose**: Primary integration point with Home Assistant; enables automation, dashboards, and HomeKit bridging
- **Mandatory for rewrite**: Yes

### Apple HomeKit

- **Implementation**: Not implemented in ESPHome YAML
- **Bridge**: Provided externally via Home Assistant to HomeKit integration (reference to "HomeBridge" in YAML comments)
- **Exposed Services**: Fan (via `fan_motor`), select (via `ventilation_manual_control`), sensors
- **Purpose**: Allow HomeKit clients (iPhone, iPad, Siri) to monitor and control the Orcon unit
- **Dependency**: Requires Home Assistant and HomeKit bridge (external to firmware)
- **Mandatory for rewrite**: HomeKit compatibility must be maintained through HA API; firmware implementation itself not required
- **Note**: The exact bridge implementation (HomeBridge, native HA HomeKit, or other) is not specified in the YAML

### ESPHome Web Server

- **Host**: 0.0.0.0 (all interfaces)
- **Port**: 80
- **Authentication**: HTTP Digest (username and password from secrets)
- **Version**: 3 (latest)
- **Entities Exposed**: All sensors, manual control, diagnostic info, logs
- **Purpose**: Local web UI for status monitoring, diagnostics, configuration, and emergency manual control
- **Mandatory for rewrite**: Yes
- **Note**: Accessible from any device on the same network (if password known)

### OTA (Over-The-Air) Firmware Updates

- **Platform**: esphome
- **Protocol**: HTTPS
- **Server**: ESPHome cloud or local network
- **Authentication**: Password (from secrets)
- **Purpose**: Remote firmware updates without physical access
- **Mandatory for rewrite**: Yes

### ESPHome API (Local)

- **Protocol**: TCP/IP (encrypted)
- **Purpose**: Discovery and communication with ESPHome devices
- **Default Port**: 6053
- **Use**: Primary channel for local Home Assistant integration
- **Mandatory for rewrite**: Yes

---

## Runtime Behaviour

### Boot Sequence

1. **System initialization** (0s): ESP32 boots, ESPHome framework loads
2. **Hardware setup** (0s): GPIO, I²C, UART, sensors initialized
3. **Wait** (15s): Delay to allow hardware stability
4. **Force AUTO mode** (15s): Set `ventilation_manual_control` to "AUTO"
5. **Execute evaluation** (15s): Run `evaluate_air_quality` script (will enter AUTO logic)
6. **Start fan** (15s): Turn on `fan_motor` at 15% speed (`fan_speed_low`)
7. **Network activation** (ongoing): WiFi connection and Home Assistant sync

### Initialization

- I²C buses and sensors begin polling at their configured intervals (30s for air quality, 5s for RPM)
- WiFi connection initiated; fallback AP available if primary network unavailable
- Time source syncs from Home Assistant (once API connection established)
- Logger starts at DEBUG level
- Web server activates on port 80

### Normal Operation (Event-Driven)

The controller operates in a purely event-driven model:

1. **Sensor updates**: SHT4x, SGP4x, SCD4x, RPM poll at configured intervals
2. **Filtered triggers**: Humidity, VOC, NOx, CO₂ updates pass delta filters; if threshold met, trigger `evaluate_air_quality`
3. **Throttle gate**: ESPHome's `throttle_average: 30s` ensures minimum 30s between repeated triggers from same sensor
4. **Watchdog fallback**: Every 2 minutes, unconditionally execute `evaluate_air_quality` (catches slow changes)
5. **Script execution**: Single script instance (`mode: single`) queues overlapping executions
6. **Decision & output**: Script updates `fan_motor` speed only if target has changed
7. **Logging**: All decisions logged at INFO level with sensor values and reasoning
8. **State caching**: Global variables track hold timer, last evaluation time, current speed, mode flag

### Manual Operation (Non-AUTO Modes)

When `ventilation_manual_control` is set to RUST, LAAG, MEDIUM, HOOG, or UIT:

1. Script enters manual branch
2. Mode string mapped to fixed speed constant (or 0 for UIT)
3. Fan output updated only if:
   - Target speed differs from current target, OR
   - Fan is on but should be off (UIT), OR
   - Fan is off but should be on
4. No threshold evaluation, no hold timer, no day/night switching
5. Manual mode persists until user selects different mode or system reboots

### Autonomous Operation (AUTO Mode)

When `ventilation_manual_control` is set to AUTO:

1. **Mode verification**: Check if AUTO is truly active (fallback from manual modes during boot)
2. **Cooldown gate**: If last evaluation was <30 seconds ago, skip evaluation (log "Cooldown active")
3. **Clean start**: If entering AUTO from manual mode, reset to `fan_speed_low` and clear hold timer
4. **Sensor read**: Fetch current values of all 4 sensors (VOC, CO₂, RH, NOx)
5. **Validity check**: If any sensor is NaN/invalid, log warning and skip (fail-safe: maintain current speed)
6. **Threshold eval**: Compare each sensor against its threshold
7. **Day/night profile**: Select speed set (day or night) based on current hour
8. **Transition logic**:
   - If any threshold exceeded: Jump to high speed (day or night)
   - If no thresholds exceeded AND currently at high speed: Transition to hold speed, start hold timer
   - If at hold speed AND hold timer expired: Transition to low speed (idle)
   - If at hold speed AND hold timer not expired: Log remaining hold time (no change)
9. **Output update**: Only update fan if computed target speed differs from `current_target_speed`
10. **Logging**: Log reasoning (which sensor triggered, hold status, etc.)

### Periodic Evaluation Fallback (Watchdog)

- **Interval**: Every 2 minutes
- **Trigger**: `interval: 2min` block
- **Effect**: Executes `evaluate_air_quality` unconditionally, regardless of sensor updates
- **Purpose**: Ensures evaluation proceeds even when all sensor changes fall below delta thresholds
- **Example scenario**: Slow, steady air quality degradation (1% per minute); would never trigger sensor-based delta filters but needs controller response

---

## Requirements for the New Implementation

The following is a complete checklist of all features and hardware capabilities that **must** be preserved or implemented in any firmware rewrite to maintain functional equivalence with the reference implementation.

### Networking & Connectivity
- [ ] WiFi connection with configurable SSID and password
- [ ] WiFi fallback access point (SSID: `Orcon_Fallback`, password configurable)
- [ ] Captive portal for setup and recovery
- [ ] Time sync from external source (Home Assistant clock)

### Integration Interfaces
- [ ] Home Assistant API with encryption support
- [ ] ESPHome Web Server on port 80 with HTTP Digest authentication
- [ ] Over-The-Air (OTA) firmware updates with password protection
- [ ] Apple HomeKit compatibility (via Home Assistant bridge, not direct in firmware)

### Hardware Control & Monitoring
- [ ] PWM motor output on GPIO15 with inverted signal (via ledc platform)
- [ ] Fan speed control interface (0–100% speed commands)
- [ ] Fan RPM tachometer (pulse counter on GPIO14, 5s update interval)
- [ ] Status LED on GPIO33 (WiFi/API indicator)

### Air Quality Sensors
- [ ] SHT4x sensor on i2c_sensor_1 (GPIO16 SDA, GPIO4 SCL)
  - Temperature measurement (30s update, 2 decimal places)
  - Humidity measurement (30s update, 2 decimal places, delta 2% filter, on_value trigger)
- [ ] SGP4x sensor on i2c_sensor_1
  - VOC index (30s update, delta 5% filter, on_value trigger)
  - NOx index (30s update, delta 2% filter, on_value trigger)
  - Temperature/humidity compensation from SHT4x
- [ ] SCD4x sensor on i2c_sensor_1
  - CO₂ measurement (30s update, delta 25 ppm filter, on_value trigger)
  - Temperature measurement (30s update, diagnostic only)
  - Humidity measurement (30s update, diagnostic only)
- [ ] All sensor values exposed to Home Assistant with proper device classes and state classes

### Diagnostic Sensors
- [ ] WiFi signal strength (dB and percentage)
- [ ] Fan RPM measurement

### User Interface & Control
- [ ] Operating mode selector with 6 options: UIT, AUTO, RUST, LAAG, MEDIUM, HOOG
- [ ] Manual control exposed to Home Assistant and HomeKit
- [ ] Initial mode: RUST (on boot, before AUTO takes effect)
- [ ] Web server UI for local control and diagnostics

### Configuration Values (Must Be Externally Configurable)
- [ ] Night mode start hour (substitution: `night_mode_start`, default 22)
- [ ] Night mode end hour (substitution: `night_mode_end`, default 7)
- [ ] VOC threshold (substitution: `voc_threshold`, default 150)
- [ ] CO₂ threshold (substitution: `co2_threshold`, default 800 ppm)
- [ ] Humidity threshold (substitution: `humidity_threshold`, default 60%)
- [ ] NOx threshold (substitution: `nox_threshold`, default 5)
- [ ] High fan speed (day) (substitution: `fan_speed_high_day`, default 40%)
- [ ] High fan speed (night) (substitution: `fan_speed_high_night`, default 30%)
- [ ] Hold fan speed (day) (substitution: `fan_speed_hold_day`, default 35%)
- [ ] Hold fan speed (night) (substitution: `fan_speed_hold_night`, default 25%)
- [ ] Idle fan speed (substitution: `fan_speed_low`, default 15%)
- [ ] Manual mode speeds: RUST, LAAG, MEDIUM, HOOG (substitutions: `manual_idle`, `manual_low`, `manual_medium`, `manual_high`)
- [ ] Hold timer duration (substitution: `hold_time_seconds`, default 300 = 5 minutes)
- [ ] Cooldown interval between evaluations (substitution: `cooldown_seconds`, default 30)

### Automation & Control Logic
- [ ] Automatic air quality evaluation on boot (after 15s delay, forcing AUTO mode, starting fan at 15%)
- [ ] Event-driven evaluation triggering:
  1. Mode selection changes
  2. SHT4x humidity changes by >2%
  3. SGP4x VOC changes by >5%
  4. SGP4x NOx changes by >2%
  5. SCD4x CO₂ changes by >25 ppm
  6. Every 2 minutes (watchdog, regardless of sensor changes)
- [ ] Automatic mode logic:
  - Continuous evaluation of air quality thresholds
  - Day/night profile switching based on time-of-day
  - Idle (low) → High → Hold → Low state transitions
  - Hold timer: remain at reduced speed for 5 minutes after air quality recovers
  - Cooldown gate: skip evaluation if <30 seconds since last evaluation
  - Sensor validity checking: fail-safe if any sensor is NaN
- [ ] Manual mode logic:
  - Fixed speed operation for all manual modes
  - No threshold evaluation or state transitions
  - Immediate speed changes on mode selection
  - UIT (off) mode support

### Logging & Diagnostics
- [ ] Debug-level logging
- [ ] Detailed log messages explaining controller decisions:
  - Current sensor values at each evaluation
  - Threshold status (which sensors are high/ok)
  - Day/night profile active
  - Mode selection and changes
  - Fan speed transitions and reasoning
  - Hold timer status
  - Cooldown gate blocking
- [ ] Serial console output
- [ ] Web server log viewer

### Reserved/Unused Hardware (Must Be Declared but May Remain Unused)
- [ ] UART buses (`uart_sensor_1` and `uart_sensor_2`) at 9600 baud — declared in reference but unused by any component
- [ ] I²C bus 2 (`i2c_sensor_2` at GPIO19 SDA, GPIO18 SCL) — declared in reference but unused by any component
- [ ] Note: These are present in the reference wiring but not actively used; they may be reserved for future expansion or represent legacy configuration

### System Reliability
- [ ] Autonomous operation: must function without Home Assistant (local decision-making)
- [ ] Graceful degradation: if Home Assistant unavailable, controller continues in last known state
- [ ] Invalid sensor handling: if any sensor reading is invalid (NaN), skip evaluation and maintain current fan speed
- [ ] Watchdog fallback: periodic evaluation ensures no stale state even if sensor triggers fail

