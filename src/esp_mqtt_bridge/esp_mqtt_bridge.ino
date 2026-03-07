#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <time.h>

#define WIFI_SSID "ZTE70"
#define WIFI_PASS "6867068670"
#define MQTT_BROKER "t0311730.ala.eu-central-1.emqxsl.com"
#define MQTT_PORT 8883
#define MQTT_CLIENT_ID "plant_watering_esp"
#define MQTT_USER "wifi_access"
#define MQTT_PASS "qazxsw123wifi"
#define MQTT_TOPIC_CMD "plant/command"
#define MQTT_TOPIC_MOISTURE "plant/moisture"
#define MQTT_TOPIC_PUMP "plant/pump"
#define MQTT_TOPIC_MODE "plant/mode"
#define SERIAL_BUF_SIZE 128
#define RECONNECT_DELAY 5000

static const char CA_CERT[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----
)EOF";

BearSSL::X509List caCert(CA_CERT);
BearSSL::WiFiClientSecure wifiClient;
PubSubClient mqtt(wifiClient);
char serialBuf[SERIAL_BUF_SIZE];
byte serialIdx = 0;
unsigned long lastReconnect = 0;

void setup() {
  Serial.begin(9600);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  wifiClient.setTrustAnchors(&caCert);
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
  if (mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
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
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    delay(100);
    return;
  }

  if (time(nullptr) < 1000000000UL) {
    configTime(0, 0, "pool.ntp.org");
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
