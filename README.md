# GOATI Firmware

AI assistant firmware for **Heltec WiFi LoRa 32 V3** with SSD1306 OLED and Telegram bot.

## Overview

GOATI (GPIO/Output/AI/Text Interface) is a Tamagotchi-style AI assistant that:
- Runs on Heltec WiFi LoRa 32 V3 (ESP32-S3, 8MB flash)
- Talks to MiniMax M3 (or compatible LLM) over WiFi
- Shows a kawaii face on the OLED
- Sends/receives messages via Telegram bot
- Has mood system (happy/neutral/lonely)
- Auto-sends kawaii messages every 1h if idle
- LED changes pattern per state

## Hardware

- ESP32-S3FN8 (Heltec WiFi LoRa 32 V3)
- 128x64 SSD1306 OLED (built-in, SDA=17, SCL=18, Vext=36, RST=21)
- 1 button (PRG/BOOT on GPIO0)

## Features

### Screens (cycled by short press)
1. **HOME** - GOATI face, WiFi status, mood, last-interaction minutes
2. **STATS** - uptime, heap, model info
3. **SOCIAL** - GOATI auto-message display

### GOATI moods
- HAPPY (recent interaction, < 5min)
- NEUTRAL (5min - 1h)
- LONELY (> 1h, LED rapid blink)

### Boot animation
Hacker-style 4-second sequence: blinking cursor → typing effect → HELTEC READY + progress bar

## Configuration

Edit via serial (115200 baud):
```
set wifi_ssid "YourSSID"
set wifi_pass "YourPassword"
set tg_token "BotToken"
set tg_enabled 1
set llm_model "MiniMax-M3"
set llm_api_base "https://api.minimax.io/v1"
set llm_api_key "YourKey"
connect
```

Or set via `cfg_save` after manual config.

## Build & flash

Requires PlatformIO with espressif32 platform.

```bash
cd main
pio run -e esp32s3 -t upload
```

## Architecture

Single-translation-unit C++ (all code in `main/src/femtoclaw_mcu.cpp` via includes):
- `display.h` - OLED + GOATI face rendering, state machine
- `telegram.h` - Telegram long-polling
- `shell.h` - serial command interface
- `llm.h` - M3 chat via MiniMax API
- `mcu_wifi.h` - WiFi connect
- `actions.h` - hardware action execution
- `config.h` - persistent config struct

## License

MIT
