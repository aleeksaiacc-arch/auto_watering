#define MOISTURE_PIN A0
#define SENSOR_POWER_PIN 7
#define PUMP_PIN 4
#define ESP_RX_PIN 10
#define ESP_TX_PIN 11

#define THRESHOLD_DRY 30
#define THRESHOLD_WET 70
#define SENSOR_READ_NORMAL   300000UL
#define SENSOR_READ_WATERING  30000UL
#define PUMP_ON_DURATION       2000UL
#define PUMP_PAUSE_DURATION   10000UL
#define PUMP_SESSION_MAX_MS  600000UL
#define SENSOR_SAMPLES 5
#define CMD_BUFFER_SIZE 32
#define LOG_INTERVAL 3000UL
#define PUMP_TEST_MS 1000


#define ADC_DRY 1023
#define ADC_WET 300

#define USE_ESP8266 1
#define USE_SERIAL_DEBUG 0

#include <EEPROM.h>

enum Mode { MODE_AUTO, MODE_MANUAL };
enum PumpCycle { CYCLE_IDLE, CYCLE_PUMP_ON, CYCLE_PUMP_PAUSE };

Mode currentMode = MODE_AUTO;
PumpCycle pumpCycle = CYCLE_IDLE;
int thresholdDry = THRESHOLD_DRY;
int thresholdWet = THRESHOLD_WET;
unsigned long wateringStart = 0;
unsigned long cyclePhaseStart = 0;
unsigned long lastSensorRead = 0;
unsigned long lastLogTime = 0;
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

void logStatus() {
  char buf[64];
  if (pumpCycle != CYCLE_IDLE) {
    snprintf(buf, sizeof(buf), "Moisture: %d%% | Stop watering at: %d%%",
             lastMoisturePercent, thresholdWet);
  } else {
    snprintf(buf, sizeof(buf), "Moisture: %d%% | Start watering at: %d%%",
             lastMoisturePercent, thresholdDry);
  }
  Serial.println(buf);
}

void setup() {
  pinMode(MOISTURE_PIN, INPUT);
  pinMode(SENSOR_POWER_PIN, OUTPUT);
  digitalWrite(SENSOR_POWER_PIN, LOW);
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);
  analogReference(DEFAULT);

  Serial.begin(9600);
#if USE_ESP8266
  espSerial.begin(9600);
#endif

  loadThresholdFromEeprom();

  digitalWrite(PUMP_PIN, HIGH);
  delay(PUMP_TEST_MS);
  digitalWrite(PUMP_PIN, LOW);
  Serial.println("Motor test OK");

  int pct = moistureToPercent(readMoistureRaw());
  lastMoisturePercent = pct;
  logStatus();

  lastSensorRead = millis();
  lastLogTime = millis();

  if (currentMode == MODE_AUTO) {
    runAutoMode(pct);
  }
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
  digitalWrite(SENSOR_POWER_PIN, HIGH);
  delay(100);
  long sum = 0;
  for (int i = 0; i < SENSOR_SAMPLES; i++) {
    sum += analogRead(MOISTURE_PIN);
    delay(10);
  }
  digitalWrite(SENSOR_POWER_PIN, LOW);
  return sum / SENSOR_SAMPLES;
}

int moistureToPercent(int raw) {

  int pct = map(raw, ADC_DRY, ADC_WET, 0, 100);
  return constrain(pct, 0, 100);
}

void startWatering() {
  wateringStart = millis();
  cyclePhaseStart = wateringStart;
  pumpCycle = CYCLE_PUMP_ON;
  digitalWrite(PUMP_PIN, HIGH);
}

void stopWatering() {
  digitalWrite(PUMP_PIN, LOW);
  pumpCycle = CYCLE_IDLE;
  sendStatus();
}

void runPumpCycle(unsigned long now) {
  if (pumpCycle == CYCLE_IDLE) return;

  unsigned long elapsed = now - cyclePhaseStart;

  if (pumpCycle == CYCLE_PUMP_ON) {
    if (elapsed >= PUMP_ON_DURATION) {
      digitalWrite(PUMP_PIN, LOW);
      pumpCycle = CYCLE_PUMP_PAUSE;
      cyclePhaseStart = now;
    }
  } else if (pumpCycle == CYCLE_PUMP_PAUSE) {
    if (elapsed >= PUMP_PAUSE_DURATION) {
      if (now - wateringStart >= PUMP_SESSION_MAX_MS) {
        stopWatering();
      } else {
        digitalWrite(PUMP_PIN, HIGH);
        pumpCycle = CYCLE_PUMP_ON;
        cyclePhaseStart = now;
      }
    }
  }
}

void runAutoMode(int moisturePercent) {
  if (pumpCycle != CYCLE_IDLE) {
    if (moisturePercent >= thresholdWet) {
      stopWatering();
    }
    return;
  }
  if (moisturePercent < thresholdDry) {
    startWatering();
    sendStatus();
  }
}

void sendStatus() {
  char buf[64];
  snprintf(buf, sizeof(buf), "MOISTURE:%d|PUMP:%s|MODE:%s|THRESHOLD:%d\n",
           lastMoisturePercent,
           pumpCycle != CYCLE_IDLE ? "ON" : "OFF",
           currentMode == MODE_AUTO ? "AUTO" : "MANUAL",
           thresholdDry);
  WIFI_SERIAL.print(buf);

#if USE_SERIAL_DEBUG
  Serial.print("-> ");
  Serial.print(buf);
#endif
}

void processCommand(const char* cmd) {
  if (strcmp(cmd, "PUMP_ON") == 0) {
    currentMode = MODE_MANUAL;
    startWatering();
    sendStatus();
  } else if (strcmp(cmd, "PUMP_OFF") == 0) {
    digitalWrite(PUMP_PIN, LOW);
    pumpCycle = CYCLE_IDLE;
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

void loop() {
  unsigned long now = millis();
  pollSerial();
  runPumpCycle(now);

  if (now - lastLogTime >= LOG_INTERVAL) {
    lastLogTime = now;
    logStatus();
  }

  unsigned long interval = (pumpCycle != CYCLE_IDLE) ? SENSOR_READ_WATERING : SENSOR_READ_NORMAL;
  if (now - lastSensorRead >= interval) {
    lastSensorRead = now;
    int pct = moistureToPercent(readMoistureRaw());
    if (pct != lastMoisturePercent) {
      char buf[24];
      snprintf(buf, sizeof(buf), "MOISTURE:%d\n", pct);
      WIFI_SERIAL.print(buf);
    }
    lastMoisturePercent = pct;

    if (currentMode == MODE_AUTO) {
      runAutoMode(pct);
    }
  }
}
