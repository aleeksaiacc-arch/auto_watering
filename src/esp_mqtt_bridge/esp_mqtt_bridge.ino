#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#define WIFI_SSID "YOUR_SSID"
#define WIFI_PASS "YOUR_PASSWORD"
#define MQTT_BROKER "broker.hivemq.com"
#define MQTT_PORT 1883
#define MQTT_CLIENT_ID "plant_watering_esp"
#define MQTT_TOPIC_CMD "plant/command"
#define MQTT_TOPIC_MOISTURE "plant/moisture"
#define MQTT_TOPIC_PUMP "plant/pump"
#define MQTT_TOPIC_MODE "plant/mode"
#define SERIAL_BUF_SIZE 128
#define RECONNECT_DELAY 5000

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
char serialBuf[SERIAL_BUF_SIZE];
byte serialIdx = 0;
unsigned long lastReconnect = 0;

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, MQTT_TOPIC_CMD) != 0) return;
  if (length >= SERIAL_BUF_SIZE) return;
  char cmd[SERIAL_BUF_SIZE];
  memcpy(cmd, payload, length);
  cmd[length] = '\0';
  Serial.println(cmd);
}

void connectMqtt() {
  if (mqtt.connected()) return;
  if (mqtt.connect(MQTT_CLIENT_ID)) {
    mqtt.subscribe(MQTT_TOPIC_CMD);
  }
}

void processSerialLine(const char* line) {
  if (strstr(line, "|PUMP:") != nullptr) {
    int moist = -1;
    char pump[8] = "";
    char mode[8] = "";
    int thresh = -1;
    sscanf(line, "MOISTURE:%d|PUMP:%7[^|]|MODE:%7[^|]|THRESHOLD:%d",
           &moist, pump, mode, &thresh);
    if (moist >= 0) {
      char buf[8];
      snprintf(buf, sizeof(buf), "%d", moist);
      mqtt.publish(MQTT_TOPIC_MOISTURE, buf);
    }
    if (pump[0]) mqtt.publish(MQTT_TOPIC_PUMP, pump);
    if (mode[0]) mqtt.publish(MQTT_TOPIC_MODE, mode);
    if (thresh >= 0) {
      char buf[8];
      snprintf(buf, sizeof(buf), "%d", thresh);
      mqtt.publish("plant/threshold", buf);
    }
    return;
  }
  if (strncmp(line, "MOISTURE:", 9) == 0) {
    mqtt.publish(MQTT_TOPIC_MOISTURE, line + 9);
    return;
  }
  if (strncmp(line, "MQTT_PUB:", 9) == 0) {
    const char* rest = line + 9;
    const char* colon = strchr(rest, ':');
    if (colon && colon > rest) {
      size_t topicLen = colon - rest;
      char topic[64];
      if (topicLen >= sizeof(topic)) topicLen = sizeof(topic) - 1;
      strncpy(topic, rest, topicLen);
      topic[topicLen] = '\0';
      mqtt.publish(topic, colon + 1);
    }
  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    delay(100);
    return;
  }
  if (!mqtt.connected()) {
    if (millis() - lastReconnect > RECONNECT_DELAY) {
      lastReconnect = millis();
      connectMqtt();
    }
  } else {
    mqtt.loop();
  }

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialIdx > 0) {
        serialBuf[serialIdx] = '\0';
        processSerialLine(serialBuf);
        serialIdx = 0;
      }
    } else if (serialIdx < SERIAL_BUF_SIZE - 1) {
      serialBuf[serialIdx++] = c;
    }
  }
}
