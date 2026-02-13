# Система автоматизированного полива растений

Система на базе: 

- **LGT8F328P** - (основной контроллер, управляет логикой полива и опросом датчиков); 

- **резистивного датчика влажности** - (измеряет сопротивление почвы — чем выше влажность, тем ниже сопротивление); 

- **насоса** - (подаёт воду в грунт, управляется через IRF840); 

- **ESP8266 ESP-01** - (модуль Wi-Fi, связь с MQTT-брокером для мониторинга и удалённого управления);

- **адаптер ESP-01** - (LDO 3.3V, встроенный level shifter 5V↔3.3V; питание 4.5–5.5V).

## Схема подключения

| Компонент | Подключение к LGT8F328P |
|-----------|-------------------------|
| Резистивный датчик влажности | SIG → A0<br>VCC → 5V<br>GND → GND |
| Насос (через IRF840) | D4 → Gate<br>Насос (−) → Drain<br>Source → GND |
| ESP-01 (через адаптер) | TX → D10 (RX)<br>RX → D11 (TX)<br>GND → GND<br>VCC → 5V |

### Насос через IRF840

N‑канальный MOSFET IRF840 используется как низкоомный ключ на «минус» насоса. LGT выдаёт 5V на затвор — достаточная величина для отпирания (Vgs(th)=2–4V).

```
5–12V ──┬── Насос (+) ── Насос (−) ── Drain IRF840
        │                        │
        └─────── D1 ────────────┘
                 (катод к +, анод к Drain)

D4 ──[R1 220Ω]── Gate IRF840
                 Source ── GND
```

| Компонент | Обозначение | Параметры |
|-----------|-------------|-----------|
| MOSFET | IRF840 | N-ch, 8A, 500V, TO-220 |
| Диод защиты | D1 | 1N4007 / UF4007 / FR107, анод→Drain, катод→+питания насоса |
| Резистор затвора | R1 | 220Ω (ограничение тока, подавление дребезга) |
| Pull-down | R2 | 10kΩ Gate→GND (защита от случайного открытия) |

**Важно:** Насос питается от отдельного источника 5–12V, не от пина LGT. Диод D1 обязателен — без него выброс ЭДС при отключении может вывести MOSFET из строя.

### Адаптер ESP-01

Используется модуль-адаптер с LDO 3.3V и встроенным level shifter. LGT подключается к адаптеру напрямую (5V, GND, TX, RX); ESP-01 устанавливается в разъём адаптера. Дополнительные делители или стабилизаторы не требуются.

| Параметр | Значение |
|----------|----------|
| Входное питание | 4.5–5.5V |
| Ток | до 240 mA |
| Интерфейс | 5V/3.3V (автоматический сдвиг уровней) |


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
├── esp-01_adapter_info.txt
└── package.json
```

## Прошивка

### Arduino IDE

1. **Плата LGT8F328P**
   - File → Preferences → Additional Board URLs: `https://raw.githubusercontent.com/dbuezas/lgt8fx/master/package_lgt8fx_index.json`
   - Tools → Board → Boards Manager → найти «lgt8fx» → Install
   - Tools → Board → LGT8fx Boards → LGT8F328P-LQFP32 MiniEVB (или LGT8F328P-LQFP48 MiniEVB для Nano)
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

- Адаптер ESP-01 потребляет до 240 mA от 5V; при ограниченном питании — отдельный источник 5V.
- Насос подключать через реле или MOSFET, не напрямую к пину.
- Брокер MQTT: HiveMQ Cloud (бесплатно) или локальный Mosquitto.
- Telegram-бот может подписаться на MQTT и отправлять уведомления.
