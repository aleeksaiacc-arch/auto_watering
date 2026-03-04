# Система автоматизированного полива растений

Система на базе: 

- **LGT8F328P** - (основной контроллер, управляет логикой полива и опросом датчиков); 

- **резистивного датчика влажности** - (измеряет сопротивление почвы — чем выше влажность, тем ниже сопротивление); 

- **насоса** - (подаёт воду в грунт, управляется через IRF540); 

- **Wemos D1 Mini** - (модуль Wi-Fi на ESP8266, связь с MQTT-брокером для мониторинга и удалённого управления; встроенный USB через CH340).

## Схема подключения

| Компонент | Подключение к LGT8F328P |
|-----------|-------------------------|
| Резистивный датчик влажности | SIG → A0<br>VCC → 5V<br>GND → GND |
| Насос (через IRF540) | D4 → Gate<br>Насос (−) → Drain<br>Source → GND |
| D1 Mini | TX → D10 (RX)<br>RX → D11 (TX) ¹<br>GND → GND<br>5V → 5V |

### Насос через IRF540

N‑канальный MOSFET IRF540 используется как низкоомный ключ на «минус» насоса. LGT выдаёт 5V на затвор — достаточная величина для отпирания (Vgs(th)=2–4V). IRF540 имеет более низкое сопротивление в открытом состоянии (Rds(on) ≈ 0.04Ω) и больший ток (33A), что обеспечивает меньшие потери мощности и лучшую работу с насосами.

```
5–12V ──┬── Насос (+) ── Насос (−) ── Drain IRF540
        │                        │
        └─────── D1 ────────────┘
                 (катод к +, анод к Drain)

D4 ──[R1 220Ω]── Gate IRF540
                 Source ── GND
```

| Компонент | Обозначение | Параметры |
|-----------|-------------|-----------|
| MOSFET | IRF540 | N-ch, 33A, 100V, TO-220, Rds(on)≈0.04Ω @ Vgs=10V |
| Диод защиты | D1 | 1N4007 / UF4007 / FR107, анод→Drain, катод→+питания насоса |
| Резистор затвора | R1 | 220Ω (ограничение тока, подавление дребезга) |
| Pull-down | R2 | 10kΩ Gate→GND (защита от случайного открытия) |

**Важно:** Насос питается от отдельного источника 5–12V, не от пина LGT. Диод D1 обязателен — без него выброс ЭДС при отключении может вывести MOSFET из строя.

### D1 Mini

D1 Mini питается от 5V (через встроенный LDO → 3.3V). TX (3.3V) подаётся напрямую на D10 LGT — совместимо. RX D1 Mini рассчитан на 3.3V, поэтому 5V-сигнал с D11 LGT нужно понизить делителем напряжения.

¹ **Делитель на линии LGT D11 → D1 Mini RX:**
```
LGT D11 ──[1kΩ]──┬── D1 Mini RX
                  │
                [2kΩ]
                  │
                 GND
```

| Параметр | Значение |
|----------|----------|
| Входное питание | 5V (pin 5V) |
| Логика | 3.3V |
| USB | CH340 (встроенный) |

## Структура проекта

```
arduino_cursor_proj/
├── src/
│   ├── plant_watering/       # Скетч для LGT8F328P
│   │   └── plant_watering.ino
│   └── esp_mqtt_bridge/     # Скетч для Wemos D1 Mini
│       └── esp_mqtt_bridge.ino
├── lib/
├── docs/
│   ├── README.md
│   └── components_info/
│       ├── board_info.txt
│       └── d1_mini_info.txt
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

2. **Wemos D1 Mini**
   - File → Preferences → Additional boards manager URLs: `https://arduino.esp8266.com/stable/package_esp8266com_index.json`
   - Tools → Board → Boards Manager → найти «esp8266 by ESP8266 Community» → Install
   - Tools → Board → esp8266 → LOLIN(WEMOS) D1 Mini
   - Tools → Port → COMx (D1 Mini подключается напрямую по USB)
   - Открыть `src/esp_mqtt_bridge/esp_mqtt_bridge.ino`
   - Установить Wi-Fi и MQTT в начале файла:
     - `WIFI_SSID`, `WIFI_PASS`
     - `MQTT_BROKER`
   - Sketch → Include Library → Manage Libraries → найти «PubSubClient» → Install
   - Загрузить скетч

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

- D1 Mini потребляет до 340 mA от 5V (при передаче Wi-Fi); при ограниченном питании — отдельный источник 5V.
- Насос подключать через реле или MOSFET, не напрямую к пину.
- Брокер MQTT: HiveMQ Cloud (бесплатно) или локальный Mosquitto.
- Telegram-бот может подписаться на MQTT и отправлять уведомления.
