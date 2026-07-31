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
4. **BLE SPAM** - ESP32Marauder-style BLE advertisement flooding (Apple / Samsung / FastPair)
5. **BADUSB** - BLE HID keyboard (5 built-in DuckyScript-style payloads)

### GOATI moods
- HAPPY (recent interaction, < 5min)
- NEUTRAL (5min - 1h)
- LONELY (> 1h, LED rapid blink)

### Boot animation
Hacker-style 4-second sequence: blinking cursor → typing effect → HELTEC READY + progress bar

### Page 4 — BLE Spam
ESP32Marauder-style advertisement flooding with a **fresh random MAC per packet**, so nearby iOS / Android devices keep showing phantom pairing popups instead of de-duplicating them. **Hold PRG** to attack (rotates APPLE → SAMSUNG → FASTPAIR); **release** to stop. **Authorized security testing only.**

### Page 5 — BadUSB (BLE)
Pairs as a Bluetooth keyboard ("GOATI-KB") and types DuckyScript-style payloads on the paired host. **Short press** cycles the selected payload (dots on the right show which); **long press (hold PRG)** executes it. Ships with 5 payloads (Notepad, open URL, winver, lock, macOS notes). **Authorized security testing only.**

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
ble spam start APPLE | SAMSUNG | FASTPAIR   # pin one flavour
ble spam stop
ble spam cycle                              # step the displayed flavour
badusb list                                 # show all payloads
badusb next                                 # select next payload
badusb payload <n>                          # select payload by number
badusb run                                  # execute the selected payload
```

> BLE Spam now randomises the advertiser MAC on **every** packet (~30 ms) so
> iOS/Android stop de-duplicating the ads — that is what makes the popups
> actually reappear. The Apple flavour alternates SourApple (nearby-action)
> and AppleJuice (proximity-pairing) payloads.

**Authorized security testing only.** Do not use on devices you do not own.

## Build & flash

Requires PlatformIO with the espressif32 platform.

The default environment targets the **Heltec WiFi LoRa 32 V3** (ESP32-S3), and
it bundles the OLED + BLE Keyboard libraries so BLE Spam and BadUSB build:

```bash
cd firmware
pio run -e heltec_v3 -t upload   # default board (Heltec WiFi LoRa 32 V3)
# other targets: -e esp32s3  |  -e esp32  |  -e esp32c3  |  -e picow
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
