/*
 * ─────────────────────────────────────────────────────────
 * GOATI : BadUSB over Bluetooth (BLE HID keyboard)
 *
 * Single-payload mode: opens Notepad on a paired Windows /
 * Mac host and types "HOLA MUNDO".  Short press cycles
 * pages (no payload cycling).  Long press runs the payload.
 *
 * INTENDED FOR AUTHORIZED SECURITY TESTING AND EDUCATIONAL
 * USE ONLY on devices you own or have explicit permission
 * to test.
 *
 * Depends on: t-vk/ESP32 BLE Keyboard ^0.3.2
 * ─────────────────────────────────────────────────────────
 */

#pragma once

#include <BleKeyboard.h>

// ─── Single payload (DuckyScript subset) ─────────────────────────────────
//   GUI <key>          press Win/Cmd + key
//   ENTER / TAB / ESC  modifier keys
//   STRING <text>      type literal text
//   DELAY <ms>         wait N milliseconds
//
// Lines without a known command are typed as text (STRING fallback).
static const char* const BADUSB_PAYLOAD =
  "GUI r\n"
  "DELAY 500\n"
  "STRING notepad\n"
  "ENTER\n"
  "DELAY 700\n"
  "STRING HOLA MUNDO\n";

static const char* const BADUSB_PAYLOAD_NAME = "HOLA MUNDO (Notepad)";

// ─── State ───────────────────────────────────────────────────────────────
static BleKeyboard g_ble_kbd("GOATI-KB", "GOATI", 100);
static bool        g_badusb_running  = false;
static uint32_t    g_badusb_start_ms = 0;

// ─── DuckyScript parser (minimal) ────────────────────────────────────────
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
    char buf[128];
    int n = min(len, (int)sizeof(buf) - 1);
    memcpy(buf, line, n);
    buf[n] = '\0';
    g_ble_kbd.print(buf);
  }
  delay(8);
}

static void badusb_run_payload() {
  if (!g_ble_kbd.isConnected()) {
    Serial.println(F("[BadUSB] not paired — pair with 'GOATI-KB' first"));
    return;
  }
  Serial.println(F("[BadUSB] running payload"));
  g_badusb_running  = true;
  g_badusb_start_ms = millis();

  const char* p = BADUSB_PAYLOAD;
  while (*p) {
    const char* eol = strchr(p, '\n');
    int len = eol ? (int)(eol - p) : (int)strlen(p);
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
  Serial.println(F("[BadUSB] pair with your device, then long-press PRG on page 5"));
}

static void badusb_loop() {
  // nothing; payload runs on demand
}

static bool badusb_is_connected() {
  return g_ble_kbd.isConnected();
}
