#define MOISTURE_PIN A0
#define PUMP_PIN 4
#define ESP_RX_PIN 10
#define ESP_TX_PIN 11

#define THRESHOLD_DRY 30
#define THRESHOLD_WET 70
#define PUMP_DURATION_MS 10000
#define PUMP_MAX_MS 60000
#define SENSOR_READ_INTERVAL 5000
#define SENSOR_SAMPLES 5
#define CMD_BUFFER_SIZE 32

#define ADC_DRY 3500
#define ADC_WET 800

#define USE_ESP8266 1
#define USE_SERIAL_DEBUG 0

#include <EEPROM.h>

enum Mode { MODE_AUTO, MODE_MANUAL };
enum PumpState { PUMP_OFF, PUMP_ON };

Mode currentMode = MODE_AUTO;
PumpState pumpState = PUMP_OFF;
int thresholdDry = THRESHOLD_DRY;
int thresholdWet = THRESHOLD_WET;
unsigned long pumpStartTime = 0;
unsigned long lastSensorRead = 0;
unsigned long lastMqttPublish = 0;
int lastMoisturePercent = -1;
char cmdBuffer[CMD_BUFFER_SIZE];
byte cmdIndex = 0;

#if USE_ESP8266
#include <SoftwareSerial.h>
SoftwareSerial espSerial(ESP_RX_PIN, ESP_TX_PIN);
#define WIFI_SERIAL espSerial
#else
#define WIFI_SERIAL Serial
#endif

#define MQTT_PUBLISH_INTERVAL 60000

void setup() {
  pinMode(MOISTURE_PIN, INPUT);
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);
  analogReference(DEFAULT);

  Serial.begin(115200);
#if USE_ESP8266
  espSerial.begin(115200);
#endif

  loadThresholdFromEeprom();
}

void loadThresholdFromEeprom() {
  int saved = (EEPROM.read(0) << 8) | EEPROM.read(1);
  if (saved >= 1 && saved <= 99) {
    thresholdDry = saved;
  }
}

void saveThresholdToEeprom() {
  EEPROM.write(0, (thresholdDry >> 8) & 0xFF);
  EEPROM.write(1, thresholdDry & 0xFF);
}

int readMoistureRaw() {
  long sum = 0;
  for (int i = 0; i < SENSOR_SAMPLES; i++) {
    sum += analogRead(MOISTURE_PIN);
    delay(10);
  }
  return sum / SENSOR_SAMPLES;
}

int moistureToPercent(int raw) {
  int pct = map(raw, ADC_DRY, ADC_WET, 0, 100);
  return constrain(pct, 0, 100);
}

void setPump(bool on) {
  if (on) {
    if (pumpState == PUMP_OFF) {
      pumpStartTime = millis();
    }
    unsigned long elapsed = millis() - pumpStartTime;
    if (elapsed < PUMP_MAX_MS) {
      digitalWrite(PUMP_PIN, HIGH);
      pumpState = PUMP_ON;
    } else {
      digitalWrite(PUMP_PIN, LOW);
      pumpState = PUMP_OFF;
    }
  } else {
    digitalWrite(PUMP_PIN, LOW);
    pumpState = PUMP_OFF;
  }
}

void runAutoMode(int moisturePercent) {
  if (pumpState == PUMP_ON) {
    unsigned long elapsed = millis() - pumpStartTime;
    if (elapsed >= PUMP_DURATION_MS || moisturePercent >= thresholdWet) {
      setPump(false);
      sendStatus();
    }
    return;
  }
  if (moisturePercent < thresholdDry) {
    setPump(true);
    sendStatus();
  }
}

void processCommand(const char* cmd) {
  if (strcmp(cmd, "PUMP_ON") == 0) {
    currentMode = MODE_MANUAL;
    setPump(true);
    sendStatus();
  } else if (strcmp(cmd, "PUMP_OFF") == 0) {
    setPump(false);
    currentMode = MODE_AUTO;
    sendStatus();
  } else if (strcmp(cmd, "GET_STATUS") == 0) {
    sendStatus();
  } else if (strncmp(cmd, "SET_THRESHOLD:", 14) == 0) {
    int val = atoi(cmd + 14);
    if (val >= 1 && val <= 99) {
      thresholdDry = val;
      saveThresholdToEeprom();
      sendStatus();
    }
  } else if (strcmp(cmd, "MODE_AUTO") == 0) {
    currentMode = MODE_AUTO;
    sendStatus();
  } else if (strcmp(cmd, "MODE_MANUAL") == 0) {
    currentMode = MODE_MANUAL;
    sendStatus();
  }
}

void sendStatus() {
  int raw = readMoistureRaw();
  int pct = moistureToPercent(raw);
  lastMoisturePercent = pct;

  char buf[64];
  snprintf(buf, sizeof(buf), "MOISTURE:%d|PUMP:%s|MODE:%s|THRESHOLD:%d\n",
           pct,
           pumpState == PUMP_ON ? "ON" : "OFF",
           currentMode == MODE_AUTO ? "AUTO" : "MANUAL",
           thresholdDry);
  WIFI_SERIAL.print(buf);

#if USE_SERIAL_DEBUG
  Serial.print("-> ");
  Serial.print(buf);
#endif
}

void pollSerial() {
  while (WIFI_SERIAL.available()) {
    char c = WIFI_SERIAL.read();
    if (c == '\n' || c == '\r') {
      if (cmdIndex > 0) {
        cmdBuffer[cmdIndex] = '\0';
        processCommand(cmdBuffer);
        cmdIndex = 0;
      }
    } else if (cmdIndex < CMD_BUFFER_SIZE - 1) {
      cmdBuffer[cmdIndex++] = c;
    }
  }
}

#if USE_ESP8266
void mqttPublish() {
  if (lastMoisturePercent < 0) return;
  if (millis() - lastMqttPublish < MQTT_PUBLISH_INTERVAL) return;
  lastMqttPublish = millis();

  char msg[16];
  snprintf(msg, sizeof(msg), "%d", lastMoisturePercent);
  mqttPublishTopic("plant/moisture", msg);
  mqttPublishTopic("plant/pump", pumpState == PUMP_ON ? "ON" : "OFF");
  mqttPublishTopic("plant/mode", currentMode == MODE_AUTO ? "AUTO" : "MANUAL");
}

void mqttPublishTopic(const char* topic, const char* payload) {
  WIFI_SERIAL.print("MQTT_PUB:");
  WIFI_SERIAL.print(topic);
  WIFI_SERIAL.print(":");
  WIFI_SERIAL.println(payload);
}
#endif

void loop() {
  unsigned long now = millis();
  pollSerial();

  if (now - lastSensorRead >= SENSOR_READ_INTERVAL) {
    lastSensorRead = now;
    int raw = readMoistureRaw();
    int pct = moistureToPercent(raw);

    if (currentMode == MODE_AUTO) {
      runAutoMode(pct);
    } else if (pumpState == PUMP_ON) {
      unsigned long elapsed = now - pumpStartTime;
      if (elapsed >= PUMP_MAX_MS) {
        setPump(false);
      }
    }

    if (pct != lastMoisturePercent) {
      lastMoisturePercent = pct;
      char buf[24];
      snprintf(buf, sizeof(buf), "MOISTURE:%d\n", pct);
      WIFI_SERIAL.print(buf);
    }
  }

#if USE_ESP8266
  mqttPublish();
#endif
}
