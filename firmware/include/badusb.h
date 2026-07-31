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
// Last confirmed connection state (debounced to avoid OLED flicker)
static bool        g_badusb_connected_latched = false;
static uint32_t    g_badusb_last_conn_ms = 0;

// ─── DuckyScript parser (minimal) ────────────────────────────────────────
static void badusb_run_line(const char* line, int len) {
  if (len <= 0) return;
  // Bail if we lose the connection mid-stream
  if (!g_ble_kbd.isConnected()) {
    Serial.println(F("[BadUSB] connection lost mid-payload"));
    return;
  }
  if (strncmp(line, "GUI ", 4) == 0) {
    char c = line[4];
    if (c) {
      g_ble_kbd.press(KEY_LEFT_GUI);
      delay(20);
      g_ble_kbd.print(c);
      delay(20);
      g_ble_kbd.releaseAll();
    }
  } else if (strncmp(line, "CTRL ", 5) == 0) {
    char c = line[5];
    if (c) {
      g_ble_kbd.press(KEY_LEFT_CTRL);
      delay(20);
      g_ble_kbd.print(c);
      delay(20);
      g_ble_kbd.releaseAll();
    }
  } else if (strncmp(line, "ALT ", 4) == 0) {
    char c = line[4];
    if (c) {
      g_ble_kbd.press(KEY_LEFT_ALT);
      delay(20);
      g_ble_kbd.print(c);
      delay(20);
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
  delay(50); // inter-keystroke delay (host HID stack needs > 20ms)
}

static bool badusb_wait_stable(uint32_t max_ms = 3000) {
  // Require the connection to be stable for ~500 ms before sending.
  uint32_t start = millis();
  uint32_t last_seen = 0;
  while (millis() - start < max_ms) {
    if (g_ble_kbd.isConnected()) {
      if (last_seen == 0) last_seen = millis();
      if (millis() - last_seen > 500) return true;
    } else {
      last_seen = 0;
    }
    delay(50);
  }
  return false;
}

static void badusb_run_payload() {
  if (!g_ble_kbd.isConnected()) {
    Serial.println(F("[BadUSB] not paired — pair with 'GOATI-KB' first"));
    Serial.println(F("[BadUSB] Windows: Settings > Bluetooth > GOATI-KB > Connect"));
    return;
  }
  Serial.println(F("[BadUSB] verifying stable connection..."));
  if (!badusb_wait_stable(3000)) {
    Serial.println(F("[BadUSB] connection unstable — payload aborted"));
    Serial.println(F("[BadUSB] try: remove pairing, then re-pair from scratch"));
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
    if (!g_ble_kbd.isConnected()) {
      Serial.println(F("[BadUSB] LOST connection mid-payload — aborted"));
      break;
    }
  }
  g_badusb_running = false;
  Serial.println(F("[BadUSB] done"));
}

// ─── Public API ──────────────────────────────────────────────────────────
static void badusb_init() {
  g_ble_kbd.begin();
  // Battery level helps Windows recognise HID devices
  g_ble_kbd.setBatteryLevel(100);
  // Full Tx power for stable connection
  BLEDevice::setPower(ESP_PWR_LVL_P9);
  Serial.println(F("[BadUSB] BLE keyboard started as 'GOATI-KB'"));
  Serial.println(F("[BadUSB] pair with your device in OS Bluetooth settings"));
  Serial.println(F("[BadUSB] on Windows: Settings > Bluetooth > GOATI-KB > Connect"));
  Serial.println(F("[BadUSB] then long-press PRG on page 5 to run payload"));
}

static void badusb_loop() {
  // Debounce connection state — only update if stable for 1 s
  bool raw = g_ble_kbd.isConnected();
  if (raw != g_badusb_connected_latched) {
    if (raw && g_badusb_last_conn_ms == 0) g_badusb_last_conn_ms = millis();
    if (raw && (millis() - g_badusb_last_conn_ms) > 1000) {
      g_badusb_connected_latched = true;
    } else if (!raw) {
      g_badusb_connected_latched = false;
      g_badusb_last_conn_ms = 0;
    }
  } else if (raw) {
    g_badusb_last_conn_ms = millis(); // keep refreshing
  }
}

static bool badusb_is_connected() {
  return g_badusb_connected_latched;
}

