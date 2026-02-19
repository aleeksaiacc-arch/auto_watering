# ESP MQTT Bridge Algorithm Documentation

## Overview
The ESP MQTT Bridge is an ESP8266-based firmware that acts as a communication bridge between an Arduino plant watering system and an MQTT broker. It translates serial commands from the Arduino into MQTT messages and forwards MQTT commands to the Arduino via serial.

## Constants

### WiFi Configuration
- `WIFI_SSID`: WiFi network name (must be configured)
- `WIFI_PASS`: WiFi password (must be configured)

### MQTT Configuration
- `MQTT_BROKER`: MQTT broker hostname (default: "broker.hivemq.com")
- `MQTT_PORT`: MQTT broker port (default: 1883, unencrypted)
- `MQTT_CLIENT_ID`: Unique client identifier for MQTT connection ("plant_watering_esp")

### MQTT Topics
- `MQTT_TOPIC_CMD`: Topic for receiving commands from MQTT ("plant/command")
- `MQTT_TOPIC_MOISTURE`: Topic for publishing moisture sensor readings ("plant/moisture")
- `MQTT_TOPIC_PUMP`: Topic for publishing pump state ("plant/pump")
- `MQTT_TOPIC_MODE`: Topic for publishing operation mode ("plant/mode")
- `"plant/threshold"`: Topic for publishing threshold value (hardcoded string)

### Buffer and Timing Constants
- `SERIAL_BUF_SIZE`: Maximum size of serial input buffer (128 bytes)
- `RECONNECT_DELAY`: Minimum delay between MQTT reconnection attempts (5000 ms)

## Global Variables

- `wifiClient`: WiFiClient instance for network communication
- `mqtt`: PubSubClient instance for MQTT communication
- `serialBuf[SERIAL_BUF_SIZE]`: Buffer for accumulating serial input characters
- `serialIdx`: Current index in serial buffer (0-127)
- `lastReconnect`: Timestamp of last reconnection attempt (milliseconds)

## Setup Algorithm

1. Initialize Serial communication at 115200 baud
2. Set WiFi mode to Station (WIFI_STA) - client mode only
3. Begin WiFi connection using SSID and password
4. Configure MQTT server address and port
5. Register MQTT callback function for incoming messages

**Note**: WiFi connection is asynchronous and continues in the background after `WiFi.begin()`.

## MQTT Callback Algorithm (`mqttCallback`)

Processes incoming MQTT messages:

1. **Topic Validation**: Check if message topic matches `MQTT_TOPIC_CMD`
   - If not matching, exit immediately
2. **Payload Size Check**: Verify payload length is less than `SERIAL_BUF_SIZE`
   - Prevents buffer overflow
   - If too large, exit without processing
3. **Payload Extraction**: Copy payload bytes to local buffer
4. **Null Termination**: Add null terminator to create valid C string
5. **Serial Forwarding**: Send complete command to Arduino via `Serial.println()`

**Purpose**: Forwards MQTT commands directly to Arduino without modification.

## MQTT Connection Algorithm (`connectMqtt`)

Manages MQTT broker connection:

1. **Connection Check**: If already connected, exit immediately
2. **Connection Attempt**: Call `mqtt.connect()` with client ID
3. **Subscription**: If connection successful, subscribe to `MQTT_TOPIC_CMD`
   - Enables receiving commands from MQTT broker
4. **Failure Handling**: If connection fails, function returns silently
   - Reconnection will be retried in main loop

**Note**: Uses no authentication (no username/password). Connection may fail silently.

## Serial Line Processing Algorithm (`processSerialLine`)

Parses and processes serial messages from Arduino. Processes three message formats:

### Format 1: Complete Status Message
**Pattern**: `"MOISTURE:%d|PUMP:%7[^|]|MODE:%7[^|]|THRESHOLD:%d"`

**Detection**: Checks if line contains `"|PUMP:"` substring

**Parsing**:
- Uses `sscanf()` to extract:
  - `moist`: Moisture percentage (integer, -1 if not found)
  - `pump`: Pump state string (max 7 chars, "ON" or "OFF")
  - `mode`: Mode string (max 7 chars, "AUTO" or "MANUAL")
  - `thresh`: Threshold value (integer, -1 if not found)

**Publishing Logic**:
- If `moist >= 0`: Publish to `MQTT_TOPIC_MOISTURE` as string
- If `pump[0]` is not null: Publish pump state to `MQTT_TOPIC_PUMP`
- If `mode[0]` is not null: Publish mode to `MQTT_TOPIC_MODE`
- If `thresh >= 0`: Publish to `"plant/threshold"` as string

**Early Return**: After processing, function returns immediately (prevents processing other formats)

### Format 2: Moisture-Only Message
**Pattern**: `"MOISTURE:%d"`

**Detection**: Checks if line starts with `"MOISTURE:"` (first 9 characters)

**Processing**: 
- Extracts substring after `"MOISTURE:"` (line + 9)
- Publishes directly to `MQTT_TOPIC_MOISTURE` without conversion

**Early Return**: Returns after publishing

### Format 3: Generic MQTT Publish Command
**Pattern**: `"MQTT_PUB:<topic>:<payload>"`

**Detection**: Checks if line starts with `"MQTT_PUB:"` (first 9 characters)

**Parsing**:
1. Extract substring after `"MQTT_PUB:"` (rest = line + 9)
2. Find first colon (`:`) separator
3. Validate colon exists and is not at start position
4. Calculate topic length (distance from start to colon)
5. Copy topic to local buffer (max 63 chars, prevents overflow)
6. Null-terminate topic string
7. Extract payload (everything after colon)
8. Publish to extracted topic with payload

**Purpose**: Allows Arduino to publish arbitrary MQTT messages through ESP8266.

## Main Loop Algorithm (`loop`)

Primary execution loop with three main responsibilities:

### 1. WiFi Connection Check
- Check `WiFi.status()` for connection state
- If not connected (`WL_CONNECTED`):
  - Wait 100ms
  - Exit loop iteration (skip MQTT and serial processing)
- **Principle**: All functionality disabled until WiFi is connected

### 2. MQTT Connection Management
- **If Not Connected**:
  - Check if `RECONNECT_DELAY` has elapsed since `lastReconnect`
  - If delay elapsed:
    - Update `lastReconnect` to current time
    - Call `connectMqtt()` to attempt reconnection
  - **Principle**: Throttles reconnection attempts to prevent flooding

- **If Connected**:
  - Call `mqtt.loop()` to process incoming messages and maintain connection
  - This triggers `mqttCallback()` for any received messages

### 3. Serial Input Processing
- **Character-by-Character Reading**:
  - Read all available characters from Serial port
  - For each character:
    - **Newline Detection** (`\n` or `\r`):
      - If buffer has content (`serialIdx > 0`):
        - Null-terminate buffer
        - Process complete line via `processSerialLine()`
        - Reset buffer index to 0
    - **Character Accumulation**:
      - If buffer has space (`serialIdx < SERIAL_BUF_SIZE - 1`):
        - Store character in `serialBuf[serialIdx]`
        - Increment `serialIdx`
      - **Overflow Protection**: Characters beyond buffer size are discarded

**Principle**: Line-based protocol - commands must end with newline character.

## Communication Protocol

### Arduino → ESP8266 (Serial)
- **Status Message**: `"MOISTURE:%d|PUMP:%s|MODE:%s|THRESHOLD:%d\n"`
- **Moisture Update**: `"MOISTURE:%d\n"`
- **MQTT Publish Request**: `"MQTT_PUB:<topic>:<payload>\n"`

### MQTT → Arduino (via Serial)
- **Command Format**: Plain text command ending with newline
- **Supported Commands**: Forwarded as-is (parsed by Arduino)
  - `"PUMP_ON\n"`
  - `"PUMP_OFF\n"`
  - `"GET_STATUS\n"`
  - `"SET_THRESHOLD:<value>\n"`
  - `"MODE_AUTO\n"`
  - `"MODE_MANUAL\n"`

### ESP8266 → MQTT Broker
- **Moisture**: `"plant/moisture"` → numeric string (e.g., "45")
- **Pump State**: `"plant/pump"` → "ON" or "OFF"
- **Mode**: `"plant/mode"` → "AUTO" or "MANUAL"
- **Threshold**: `"plant/threshold"` → numeric string (e.g., "30")

## Error Handling

1. **Buffer Overflow Protection**:
   - Serial buffer: Discards characters beyond `SERIAL_BUF_SIZE - 1`
   - MQTT payload: Rejects messages larger than buffer
   - Topic buffer: Limits to 63 characters

2. **Connection Failures**:
   - WiFi: Loop waits indefinitely for connection
   - MQTT: Reconnection attempts throttled by `RECONNECT_DELAY`

3. **Parsing Failures**:
   - `sscanf()` failures result in uninitialized variables (-1 or empty strings)
   - Publishing only occurs if values are valid (checked before publishing)

## Timing Characteristics

- **Serial Baud Rate**: 115200 bps
- **Reconnection Throttle**: Minimum 5 seconds between attempts
- **WiFi Check Delay**: 100ms when disconnected
- **MQTT Loop**: Called every iteration when connected (non-blocking)

## Design Principles

1. **Bidirectional Bridge**: Translates between serial and MQTT protocols
2. **Stateless Processing**: Each message processed independently
3. **Non-Blocking**: Uses `mqtt.loop()` for asynchronous MQTT handling
4. **Fail-Safe**: Continues operating even if MQTT disconnects (serial still works)
5. **Protocol Transparency**: MQTT commands forwarded without modification
