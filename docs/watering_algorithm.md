# Plant Watering System Algorithm Documentation

## Overview
The Plant Watering System is an Arduino-based automated irrigation controller that monitors soil moisture and controls a water pump. It operates in two modes (AUTO and MANUAL) and communicates with an ESP8266 module for MQTT connectivity.

## Constants

### Hardware Pin Definitions
- `MOISTURE_PIN`: Analog pin for moisture sensor (A0)
- `PUMP_PIN`: Digital pin for pump control relay (4)
- `ESP_RX_PIN`: Digital pin for ESP8266 RX (SoftwareSerial RX) (10)
- `ESP_TX_PIN`: Digital pin for ESP8266 TX (SoftwareSerial TX) (11)

### Threshold Constants
- `THRESHOLD_DRY`: Default dry soil threshold percentage (30%)
  - When moisture falls below this, pump activates in AUTO mode
- `THRESHOLD_WET`: Wet soil threshold percentage (70%)
  - When moisture reaches this, pump stops in AUTO mode

### Pump Control Constants
- `PUMP_DURATION_MS`: Default pump runtime duration (10000 ms = 10 seconds)
  - Minimum time pump runs before checking moisture again
- `PUMP_MAX_MS`: Maximum allowed pump runtime (60000 ms = 60 seconds)
  - Safety limit to prevent pump damage or flooding
  - Applies in both AUTO and MANUAL modes

### Sensor Constants
- `SENSOR_READ_INTERVAL`: Time between sensor readings (5000 ms = 5 seconds)
- `SENSOR_SAMPLES`: Number of analog samples to average (5)
  - Reduces noise in moisture readings

### ADC Calibration Constants
- `ADC_DRY`: ADC value representing completely dry soil (3500)
  - Higher ADC value = lower conductivity = drier soil
- `ADC_WET`: ADC value representing completely wet soil (800)
  - Lower ADC value = higher conductivity = wetter soil

### Buffer and Feature Flags
- `CMD_BUFFER_SIZE`: Size of command input buffer (32 bytes)
- `USE_ESP8266`: Enable ESP8266 communication via SoftwareSerial (1 = enabled)
- `USE_SERIAL_DEBUG`: Enable debug output to Serial port (0 = disabled)

### MQTT Publishing
- `MQTT_PUBLISH_INTERVAL`: Minimum interval between MQTT status updates (60000 ms = 60 seconds)

## Enumerations

- `Mode`: Operation mode
  - `MODE_AUTO`: Automatic watering based on moisture thresholds
  - `MODE_MANUAL`: Manual pump control via commands
- `PumpState`: Current pump state
  - `PUMP_OFF`: Pump is inactive
  - `PUMP_ON`: Pump is active

## Global State Variables

- `currentMode`: Current operation mode (default: `MODE_AUTO`)
- `pumpState`: Current pump state (default: `PUMP_OFF`)
- `thresholdDry`: Dynamic dry threshold (initialized from `THRESHOLD_DRY`, can be changed via command)
- `thresholdWet`: Wet threshold (initialized from `THRESHOLD_WET`, fixed)
- `pumpStartTime`: Timestamp when pump was last activated (milliseconds)
- `lastSensorRead`: Timestamp of last sensor reading (milliseconds)
- `lastMqttPublish`: Timestamp of last MQTT publish (milliseconds)
- `lastMoisturePercent`: Last calculated moisture percentage (-1 = uninitialized)
- `cmdBuffer[CMD_BUFFER_SIZE]`: Buffer for accumulating serial command characters
- `cmdIndex`: Current index in command buffer (0-31)

## Serial Communication Setup

- **Primary Serial**: Hardware Serial at 115200 baud (for debugging if enabled)
- **ESP8266 Serial**: SoftwareSerial on pins 10 (RX) and 11 (TX) at 115200 baud
- **Macro**: `WIFI_SERIAL` points to ESP8266 serial when `USE_ESP8266` is enabled, otherwise Hardware Serial

## Setup Algorithm (`setup`)

1. **Pin Configuration**:
   - Set `MOISTURE_PIN` as INPUT (analog)
   - Set `PUMP_PIN` as OUTPUT (digital)
   - Initialize pump to LOW (OFF state)

2. **Analog Reference**: Set to DEFAULT (5V for most Arduino boards)

3. **Serial Initialization**:
   - Initialize Hardware Serial at 115200 baud
   - If `USE_ESP8266` enabled: Initialize SoftwareSerial at 115200 baud

4. **EEPROM Load**: Load saved threshold from EEPROM (if valid)

## EEPROM Storage Algorithm

### Load Threshold (`loadThresholdFromEeprom`)
- **Storage Format**: 16-bit integer stored in two bytes
  - Byte 0: High byte `(thresholdDry >> 8) & 0xFF`
  - Byte 1: Low byte `thresholdDry & 0xFF`
- **Reading**: Combine bytes: `(EEPROM.read(0) << 8) | EEPROM.read(1)`
- **Validation**: Only loads if value is between 1 and 99 (valid percentage range)
- **Default**: If invalid, uses `THRESHOLD_DRY` constant

### Save Threshold (`saveThresholdToEeprom`)
- **Writing**: Splits 16-bit value into two bytes
  - Writes high byte to address 0
  - Writes low byte to address 1
- **Called When**: Threshold changed via `SET_THRESHOLD` command

## Moisture Sensor Algorithm

### Raw Reading (`readMoistureRaw`)
1. **Averaging**: Takes `SENSOR_SAMPLES` (5) consecutive readings
2. **Timing**: 10ms delay between samples (allows ADC stabilization)
3. **Calculation**: Returns integer average of all samples
4. **Purpose**: Reduces noise and provides stable readings

### Percentage Conversion (`moistureToPercent`)
1. **Mapping**: Uses Arduino `map()` function
   - Maps from ADC range `[ADC_DRY, ADC_WET]` to percentage range `[0, 100]`
   - Formula: `map(raw, 3500, 800, 0, 100)`
2. **Constraining**: Clamps result to `[0, 100]` using `constrain()`
3. **Inverse Relationship**: Higher ADC = drier soil = lower percentage
   - Dry (3500 ADC) → 0%
   - Wet (800 ADC) → 100%

## Pump Control Algorithm (`setPump`)

### Turning Pump ON
1. **State Check**: If pump is currently OFF, record start time (`pumpStartTime = millis()`)
2. **Safety Check**: Calculate elapsed time since pump started
3. **Time Limit**: If elapsed < `PUMP_MAX_MS` (60 seconds):
   - Set pin HIGH (activate pump)
   - Update state to `PUMP_ON`
4. **Safety Shutoff**: If elapsed >= `PUMP_MAX_MS`:
   - Set pin LOW (deactivate pump)
   - Update state to `PUMP_OFF`
   - **Purpose**: Prevents pump damage from extended operation

### Turning Pump OFF
1. Set pin LOW (deactivate pump)
2. Update state to `PUMP_OFF`
3. **Note**: Does not reset `pumpStartTime` (preserved for next activation)

## Auto Mode Algorithm (`runAutoMode`)

### When Pump is ON
1. **Calculate Elapsed Time**: `elapsed = millis() - pumpStartTime`
2. **Stop Conditions** (either condition stops pump):
   - **Time-Based**: `elapsed >= PUMP_DURATION_MS` (10 seconds minimum)
   - **Moisture-Based**: `moisturePercent >= thresholdWet` (70% reached)
3. **Action**: If either condition met:
   - Call `setPump(false)` to stop pump
   - Send status update via `sendStatus()`
4. **Early Return**: Exit function (pump continues running if conditions not met)

### When Pump is OFF
1. **Dry Soil Check**: If `moisturePercent < thresholdDry` (below 30%):
   - Call `setPump(true)` to start pump
   - Send status update via `sendStatus()`
2. **No Action**: If moisture is above threshold, do nothing

**Principle**: Hysteresis control - uses different thresholds for activation (dry) and deactivation (wet) to prevent rapid cycling.

## Command Processing Algorithm (`processCommand`)

Processes text commands received via serial. All commands trigger status update except where noted.

### Command: `"PUMP_ON"`
1. Switch mode to `MODE_MANUAL`
2. Activate pump via `setPump(true)`
3. Send status update

### Command: `"PUMP_OFF"`
1. Deactivate pump via `setPump(false)`
2. Switch mode to `MODE_AUTO`
3. Send status update

### Command: `"GET_STATUS"`
1. Send status update only (no state changes)

### Command: `"SET_THRESHOLD:<value>"`
1. Extract numeric value after `"SET_THRESHOLD:"` (14 characters)
2. Validate value is between 1 and 99
3. If valid:
   - Update `thresholdDry` variable
   - Save to EEPROM via `saveThresholdToEeprom()`
   - Send status update
4. If invalid: Ignore command (no action)

### Command: `"MODE_AUTO"`
1. Switch `currentMode` to `MODE_AUTO`
2. Send status update
3. **Note**: Does not stop pump if running (pump continues until conditions met)

### Command: `"MODE_MANUAL"`
1. Switch `currentMode` to `MODE_MANUAL`
2. Send status update
3. **Note**: Does not change pump state

## Status Reporting Algorithm (`sendStatus`)

Generates and sends complete system status via serial.

1. **Read Sensor**: Get fresh moisture reading
   - Calls `readMoistureRaw()` for current ADC value
   - Converts to percentage via `moistureToPercent()`
   - Updates `lastMoisturePercent`

2. **Format Message**: Creates status string
   - Format: `"MOISTURE:%d|PUMP:%s|MODE:%s|THRESHOLD:%d\n"`
   - Values:
     - Moisture: Current percentage (0-100)
     - Pump: "ON" or "OFF" based on `pumpState`
     - Mode: "AUTO" or "MANUAL" based on `currentMode`
     - Threshold: Current `thresholdDry` value

3. **Send**: Transmits via `WIFI_SERIAL` (ESP8266 or Serial)

4. **Debug Output**: If `USE_SERIAL_DEBUG` enabled, also prints to Hardware Serial with "-> " prefix

**Called When**:
- Pump state changes
- Mode changes
- Threshold changes
- `GET_STATUS` command received
- Auto mode stops pump

## Serial Command Polling (`pollSerial`)

Reads and processes commands from serial input.

1. **Character-by-Character Reading**:
   - Read all available characters from `WIFI_SERIAL`
   - For each character:
     - **Newline Detection** (`\n` or `\r`):
       - If buffer has content (`cmdIndex > 0`):
         - Null-terminate buffer
         - Process command via `processCommand()`
         - Reset buffer index to 0
     - **Character Accumulation**:
       - If buffer has space (`cmdIndex < CMD_BUFFER_SIZE - 1`):
         - Store character in `cmdBuffer[cmdIndex]`
         - Increment `cmdIndex`
     - **Overflow Protection**: Characters beyond buffer size are discarded

**Principle**: Line-based protocol - commands must end with newline character.

## MQTT Publishing (Conditional, `USE_ESP8266`)

### Periodic Publishing (`mqttPublish`)
1. **Preconditions**:
   - `lastMoisturePercent >= 0` (sensor has been read)
   - `millis() - lastMqttPublish >= MQTT_PUBLISH_INTERVAL` (60 seconds elapsed)
2. **Update Timestamp**: Set `lastMqttPublish = millis()`
3. **Publish Topics**:
   - `"plant/moisture"`: Current moisture percentage as string
   - `"plant/pump"`: "ON" or "OFF" based on pump state
   - `"plant/mode"`: "AUTO" or "MANUAL" based on current mode

**Purpose**: Periodic status updates to MQTT broker (independent of status changes).

### Topic Publishing (`mqttPublishTopic`)
1. **Format**: Sends `"MQTT_PUB:<topic>:<payload>\n"` to ESP8266
2. **ESP8266 Processing**: ESP8266 bridge parses and publishes to MQTT broker
3. **Purpose**: Allows Arduino to publish arbitrary MQTT messages

## Main Loop Algorithm (`loop`)

Primary execution loop with three main responsibilities:

### 1. Command Polling
- Call `pollSerial()` to process any incoming commands
- **Non-Blocking**: Processes all available characters, then continues

### 2. Periodic Sensor Reading and Control
- **Timing Check**: If `millis() - lastSensorRead >= SENSOR_READ_INTERVAL` (5 seconds):
  1. **Update Timestamp**: Set `lastSensorRead = now`
  2. **Read Sensor**: Get raw ADC value and convert to percentage
  3. **Mode-Specific Control**:
     - **AUTO Mode**: Call `runAutoMode(pct)` to manage pump automatically
     - **MANUAL Mode**: If pump is ON, check safety timeout
       - If `elapsed >= PUMP_MAX_MS`: Force pump OFF (safety limit)
  4. **Moisture Change Detection**: If `pct != lastMoisturePercent`:
     - Update `lastMoisturePercent`
     - Send moisture update: `"MOISTURE:%d\n"` to ESP8266
     - **Purpose**: Immediate notification of moisture changes

### 3. MQTT Publishing (if enabled)
- If `USE_ESP8266` enabled: Call `mqttPublish()`
- **Throttled**: Only publishes if 60 seconds elapsed since last publish

## Timing Characteristics

- **Sensor Reading Interval**: Every 5 seconds
- **MQTT Publish Interval**: Minimum 60 seconds between publishes
- **Pump Minimum Duration**: 10 seconds (in AUTO mode)
- **Pump Maximum Duration**: 60 seconds (safety limit, all modes)
- **Sensor Sampling**: 5 samples with 10ms delay each (~50ms per reading)
- **Serial Baud Rate**: 115200 bps

## State Transitions

### Mode Transitions
- **AUTO → MANUAL**: Triggered by `PUMP_ON` command or `MODE_MANUAL` command
- **MANUAL → AUTO**: Triggered by `PUMP_OFF` command or `MODE_AUTO` command

### Pump State Transitions
- **OFF → ON**:
  - AUTO mode: Moisture < `thresholdDry`
  - MANUAL mode: `PUMP_ON` command
- **ON → OFF**:
  - AUTO mode: Duration >= `PUMP_DURATION_MS` OR moisture >= `thresholdWet`
  - MANUAL mode: `PUMP_OFF` command OR duration >= `PUMP_MAX_MS` (safety)

## Safety Features

1. **Maximum Pump Runtime**: Hard limit of 60 seconds prevents pump damage
2. **Hysteresis Control**: Different thresholds prevent rapid pump cycling
3. **EEPROM Persistence**: Threshold survives power cycles
4. **Input Validation**: Commands validated before execution
5. **Buffer Overflow Protection**: Serial buffers have size limits

## Design Principles

1. **Hysteresis Control**: Uses separate thresholds for activation and deactivation
2. **Safety First**: Maximum runtime limits prevent hardware damage
3. **Dual Mode Operation**: Supports both automatic and manual control
4. **Non-Blocking**: Serial and sensor operations don't block main loop
5. **State Persistence**: Critical settings saved to EEPROM
6. **Modular Communication**: Can work with or without ESP8266 module
