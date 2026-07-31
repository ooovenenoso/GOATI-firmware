/*
 * ─────────────────────────────────────────────────────────
 * GOATI : BadUSB over Bluetooth (BLE HID keyboard)
 *
 * Pairs as a generic Bluetooth keyboard and sends typed
 * DuckyScript-style payloads to the host device.
 *
 * INTENDED FOR AUTHORIZED SECURITY TESTING AND EDUCATIONAL
 * USE ONLY on devices you own or have explicit permission
 * to test.  Unauthorized keystroke injection may violate
 * computer-misuse laws (CFAA US, EU Directive 2013/40, etc.).
 *
 * Depends on: T-vK ESP32-BLE-Keyboard (T-vK/ESP32-BLE-Keyboard @ ^0.4.0)
 * ─────────────────────────────────────────────────────────
 */

#pragma once

#include <BleKeyboard.h>

// ─── Built-in payloads (DuckyScript subset) ─────────────────────────────
// Commands supported in this implementation:
//   GUI <key>          press Win/Cmd + key
//   CTRL <key>         press Ctrl + key
//   ALT <key>          press Alt + key
//   ENTER / TAB / ESC  modifier keys
//   STRING <text>      type literal text
//   DELAY <ms>         wait N milliseconds
//   REPEAT <n>         repeat previous line N times
//
// Lines without a known command are typed as-is (STRING fallback).
static const char* const BADUSB_PAYLOADS[] = {
  // 0 : hello-notepad — opens Notepad and greets
  "GUI r\n"
  "DELAY 400\n"
  "STRING notepad\n"
  "ENTER\n"
  "DELAY 600\n"
  "STRING Hello from GOATI!\n"
  "STRING This is a BadUSB BLE test payload.\n",

  // 1 : rickroll — opens browser to a YouTube URL
  "GUI r\n"
  "DELAY 400\n"
  "STRING https://www.youtube.com/watch?v=dQw4w9WgXcQ\n"
  "ENTER\n",

  // 2 : sysinfo — opens msinfo32 (Windows)
  "GUI r\n"
  "DELAY 400\n"
  "STRING msinfo32\n"
  "ENTER\n",

  // 3 : terminal-sysinfo — PowerShell system info
  "GUI r\n"
  "DELAY 400\n"
  "STRING powershell -NoExit -Command Get-ComputerInfo\n"
  "ENTER\n",

  // 4 : linux-shell — opens terminal profile (Linux, GNOME)
  "CTRL ALT t\n"
  "DELAY 600\n"
  "STRING uname -a; whoami; pwd\n"
  "ENTER\n"
};

static const char* const BADUSB_PAYLOAD_NAMES[] = {
  "Hello/Notepad",
  "Rickroll",
  "System Info",
  "PowerShell Info",
  "Linux Shell"
};

static const uint8_t BADUSB_PAYLOAD_COUNT = sizeof(BADUSB_PAYLOADS) / sizeof(BADUSB_PAYLOADS[0]);

// ─── State ───────────────────────────────────────────────────────────────
static BleKeyboard        g_ble_kbd("GOATI-KB", "GOATI", 100);
static uint8_t            g_badusb_payload_idx = 0;
static bool               g_badusb_running     = false;
static uint32_t           g_badusb_start_ms    = 0;

// ─── DuckyScript parser (minimal) ────────────────────────────────────────
// The ESP32-BLE-Keyboard library exposes only special keys + a print() method
// which handles shift for uppercase ASCII.  We use print() for typing text
// and the modifier keys for special commands.
static void badusb_run_line(const char* line, int len) {
  if (len <= 0) return;
  if (strncmp(line, "GUI ", 4) == 0) {
    char c = line[4];
    if (c) {
      g_ble_kbd.press(KEY_LEFT_GUI);
      g_ble_kbd.print(c);
      g_ble_kbd.releaseAll();
    }
  } else if (strncmp(line, "CTRL ", 5) == 0) {
    char c = line[5];
    if (c) {
      g_ble_kbd.press(KEY_LEFT_CTRL);
      g_ble_kbd.print(c);
      g_ble_kbd.releaseAll();
    }
  } else if (strncmp(line, "ALT ", 4) == 0) {
    char c = line[4];
    if (c) {
      g_ble_kbd.press(KEY_LEFT_ALT);
      g_ble_kbd.print(c);
      g_ble_kbd.releaseAll();
    }
  } else if (strncmp(line, "ENTER", 5) == 0) {
    g_ble_kbd.press(KEY_RETURN);
    g_ble_kbd.releaseAll();
  } else if (strncmp(line, "TAB", 3) == 0) {
    g_ble_kbd.press(KEY_TAB);
    g_ble_kbd.releaseAll();
  } else if (strncmp(line, "ESC", 3) == 0) {
    g_ble_kbd.press(KEY_ESC);
    g_ble_kbd.releaseAll();
  } else if (strncmp(line, "BACKSPACE", 9) == 0) {
    g_ble_kbd.press(KEY_BACKSPACE);
    g_ble_kbd.releaseAll();
  } else if (strncmp(line, "DELAY ", 6) == 0) {
    int ms = atoi(line + 6);
    if (ms > 0) delay((uint32_t)ms);
  } else if (strncmp(line, "STRING ", 7) == 0) {
    g_ble_kbd.print(line + 7);
  } else {
    // Fallback: type the line as text
    char buf[128];
    int n = min(len, (int)sizeof(buf) - 1);
    memcpy(buf, line, n);
    buf[n] = '\0';
    g_ble_kbd.print(buf);
  }
  delay(8); // small inter-keystroke delay for host HID stack
}

static void badusb_run_payload(uint8_t idx) {
  if (idx >= BADUSB_PAYLOAD_COUNT) return;
  if (!g_ble_kbd.isConnected()) {
    Serial.println(F("[BadUSB] not connected — pair device first"));
    return;
  }
  Serial.printf("[BadUSB] running payload %u (%s)\r\n", idx, BADUSB_PAYLOAD_NAMES[idx]);
  g_badusb_running    = true;
  g_badusb_start_ms   = millis();

  const char* p = BADUSB_PAYLOADS[idx];
  while (*p) {
    const char* eol = strchr(p, '\n');
    int len = eol ? (int)(eol - p) : (int)strlen(p);
    // trim trailing \r
    if (len > 0 && p[len - 1] == '\r') len--;
    badusb_run_line(p, len);
    if (!eol) break;
    p = eol + 1;
  }
  g_badusb_running = false;
  Serial.println(F("[BadUSB] done"));
}

// ─── Public API ──────────────────────────────────────────────────────────
static void badusb_init() {
  g_ble_kbd.begin();
  Serial.println(F("[BadUSB] BLE keyboard started as 'GOATI-KB'"));
  Serial.println(F("[BadUSB] pair with your device, then send 'badusb run'"));
}

static void badusb_loop() {
  // nothing; payloads are run-on-demand
}

static void badusb_cycle() {
  g_badusb_payload_idx = (g_badusb_payload_idx + 1) % BADUSB_PAYLOAD_COUNT;
  Serial.printf("[BadUSB] payload %u: %s\r\n",
                g_badusb_payload_idx,
                BADUSB_PAYLOAD_NAMES[g_badusb_payload_idx]);
}

static bool badusb_is_connected() {
  return g_ble_kbd.isConnected();
}
