# GOATI Firmware

PicoClaw-inspired AI assistant firmware for **Heltec WiFi LoRa 32 V3** with SSD1306 OLED and Telegram bot.

## Overview

GOATI (GPIO/Output/AI/Text Interface) is a PicoClaw-style ultra-lightweight AI assistant that:
- Runs on Heltec WiFi LoRa 32 V3 (ESP32-S3, 8MB flash, 320 KB RAM)
- Inspired by [PicoClaw](https://github.com/sipeed/picoclaw) (Sipeed's ultra-lightweight Go AI assistant)
- Talks to MiniMax M3 (or compatible LLM) over WiFi
- Shows a kawaii Tamagotchi face on the OLED
- Sends/receives messages via Telegram bot
- Has mood system (happy/neutral/lonely)
- Auto-sends kawaii messages every 1h if idle
- LED changes pattern per state
- PicoClaw-style slash commands: `/help /show /list /use m=X /check /clear /reload /stop`

## Hardware

- ESP32-S3FN8 (Heltec WiFi LoRa 32 V3)
- 128x64 SSD1306 OLED (built-in, SDA=17, SCL=18, Vext=36, RST=21)
- 1 button (PRG/BOOT on GPIO0)

## Features

### Screens (cycled by short press)
1. **HOME** - GOATI face, WiFi status, mood, last-interaction minutes
2. **STATS** - uptime, heap, model info
3. **SOCIAL** - GOATI auto-message display
4. **BLE SPAM** - ESP32Marauder-style BLE advertisement flooding (Apple / Samsung / FastPair)
5. **BADUSB** - BLE HID keyboard (5 built-in DuckyScript-style payloads)

### GOATI moods
- HAPPY (recent interaction, < 5min)
- NEUTRAL (5min - 1h)
- LONELY (> 1h, LED rapid blink)

### Boot animation
Hacker-style 4-second sequence: blinking cursor → typing effect → HELTEC READY + progress bar

### Page 4 — BLE Spam
ESP32Marauder-style advertisement flooding. Causes nearby iOS / Android devices to display phantom pairing popups. Use short press to cycle mode (APPLE / SAMSUNG / FASTPAIR), long press to start/stop. **Authorized security testing only.**

### Page 5 — BadUSB (BLE)
Pairs as a Bluetooth keyboard ("GOATI-KB") and sends typed DuckyScript-style payloads. Use short press to cycle payload, long press to execute. **Authorized security testing only.**

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

### Security tools (page 4 & 5)
```
ble spam start APPLE | SAMSUNG | FASTPAIR
ble spam stop
ble spam cycle
badusb next
badusb run
```

**Authorized security testing only.** Do not use on devices you do not own.

## Build & flash

Requires PlatformIO with espressif32 platform.

```bash
cd main
pio run -e esp32s3 -t upload
```

## Architecture

Single-translation-unit C++ (all code in `main/src/femtoclaw_mcu.cpp` via includes):
- `display.h` - OLED + GOATI face rendering, state machine (page 1-5)
- `ble_spam.h` - ESP32Marauder-style BLE advertisement flooding (page 4)
- `badusb.h` - BadUSB over Bluetooth (BLE HID keyboard, page 5)
- `telegram.h` - Telegram long-polling
- `shell.h` - serial command interface
- `llm.h` - M3 chat via MiniMax API
- `mcu_wifi.h` - WiFi connect
- `actions.h` - hardware action execution
- `config.h` - persistent config struct

## License

MIT
