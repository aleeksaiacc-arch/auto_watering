# Система автоматизированного полива растений

Система на базе LGT8F328P, резистивного датчика влажности, насоса и ESP8266 ESP-01 для мониторинга и удалённого управления через Wi-Fi/MQTT.

## Оборудование

| Компонент | Подключение к LGT8F328P |
|-----------|-------------------------|
| Резистивный датчик влажности | Сигнал → A0, VCC → 5V, GND → GND |
| Насос (через реле/транзистор) | Управление → D4 |
| ESP-01 | TX (ESP) → RX D10 (LGT), RX (ESP) → TX D11 (LGT), GND → GND, VCC → 3.3V |

**Схема подключения**

```
LGT8F328P          Датчик          Насос
   A0 ───────────── SIG
   GND ──────────── GND
   5V ───────────── VCC

   D4 ──────[Реле/Транзистор]────── Насос (+)
   GND ──────────────────────────── Насос (-)

LGT8F328P          ESP-01
   D10 (RX) ─────── TX (ESP)
   D11 (TX) ─────── RX (ESP)  [делитель 5V→3.3V на RX ESP]
   GND ──────────── GND
```

**Уровни:** ESP-01 питается от 3.3V. Между TX LGT (5V) и RX ESP желательно делитель 1:1.5 или резисторы 1k+2k.

## Структура проекта

```
arduino_cursor_proj/
├── src/
│   ├── plant_watering/       # Скетч для LGT8F328P
│   │   └── plant_watering.ino
│   └── esp_mqtt_bridge/     # Скетч для ESP8266 ESP-01
│       └── esp_mqtt_bridge.ino
├── lib/
├── docs/
│   └── README.md
├── board_info.txt
└── package.json
```

## Прошивка

### Arduino IDE

1. **Плата LGT8F328P**
   - File → Preferences → Additional Board URLs: `https://raw.githubusercontent.com/LogicGreen/arduino-nano-atmega328/master/package_logicgreen_index.json`
   - Tools → Board → LogicGreen AVR → LGT8F328P
   - Tools → Port → COMx
   - Открыть `src/plant_watering/plant_watering.ino`, загрузить скетч

2. **ESP8266 ESP-01**
   - Tools → Board → ESP8266 Boards → Generic ESP8266 Module
   - CPU Frequency: 80 MHz, Flash Size: 1MB
   - Открыть `src/esp_mqtt_bridge/esp_mqtt_bridge.ino`
   - Установить Wi-Fi и MQTT в начале файла:
     - `WIFI_SSID`, `WIFI_PASS`
     - `MQTT_BROKER` (например broker.hivemq.com)
   - Установить библиотеку PubSubClient (Sketch → Include Library → Manage Libraries)
   - Загрузить скетч (потребуется USB-UART адаптер для ESP-01)

### Калибровка датчика

1. Загрузить скетч, открыть Serial Monitor (115200).
2. Измерить значение в **сухой** почве (или на воздухе) — это `ADC_DRY`.
3. Измерить значение в **воде** — это `ADC_WET`.
4. В коде изменить:
   ```cpp
   #define ADC_DRY 3500   // значение при сухой почве
   #define ADC_WET 800    // значение в воде
   ```
5. Для резистивного датчика: сухая почва = выше значение, влажная = ниже.

## Конфигурация (plant_watering.ino)

| Константа | Значение | Описание |
|-----------|----------|----------|
| MOISTURE_PIN | A0 | Пин датчика влажности |
| PUMP_PIN | 4 | Пин управления насосом |
| ESP_RX_PIN | 10 | RX для ESP (SoftwareSerial) |
| ESP_TX_PIN | 11 | TX для ESP |
| THRESHOLD_DRY | 30 | Порог «сухо» (%), запуск полива |
| THRESHOLD_WET | 70 | Порог «достаточно» (%), остановка |
| PUMP_DURATION_MS | 10000 | Время работы насоса за цикл (мс) |
| PUMP_MAX_MS | 60000 | Макс. время за сессию (мс) |
| SENSOR_READ_INTERVAL | 5000 | Интервал опроса (мс) |
| ADC_DRY, ADC_WET | 3500, 800 | Калибровка АЦП |

## Протокол LGT ↔ ESP (Serial)

**LGT → ESP (115200, `\n`):**
- `MOISTURE:45` — влажность в %
- `MOISTURE:45|PUMP:ON|MODE:AUTO|THRESHOLD:30` — полный статус
- `MQTT_PUB:topic:payload` — публикация в MQTT

**ESP → LGT:**
- `PUMP_ON` — включить насос (режим ручной)
- `PUMP_OFF` — выключить насос
- `GET_STATUS` — запрос статуса
- `SET_THRESHOLD:35` — установить порог сухости
- `MODE_AUTO` / `MODE_MANUAL` — переключить режим

## MQTT топики

| Топик | Направление | Описание |
|-------|-------------|----------|
| plant/moisture | публикация | Влажность 0–100 |
| plant/pump | публикация | ON / OFF |
| plant/mode | публикация | AUTO / MANUAL |
| plant/threshold | публикация | Порог в % |
| plant/command | подписка | Команды: PUMP_ON, PUMP_OFF, SET_THRESHOLD:35 и др. |

Отправка в топик `plant/command` управляет системой.

## Режимы работы

- **AUTO** — полив при влажности < THRESHOLD_DRY до THRESHOLD_WET или PUMP_DURATION_MS.
- **MANUAL** — насос только по команде PUMP_ON; ограничение PUMP_MAX_MS сохраняется.

## Рекомендации

- Использовать отдельный блок питания 3.3V для ESP-01 при токе ≥ 200 mA.
- Насос подключать через реле или MOSFET, не напрямую к пину.
- Брокер MQTT: HiveMQ Cloud (бесплатно) или локальный Mosquitto.
- Telegram-бот может подписаться на MQTT и отправлять уведомления.
