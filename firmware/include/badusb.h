/*
 * ─────────────────────────────────────────────────────────
 * GOATI : BadUSB over Bluetooth (BLE HID keyboard)
 *
 * Pairs as a Bluetooth keyboard ("GOATI-KB") and types
 * DuckyScript-style payloads on the paired host.  Short press
 * cycles the payload on page 5, long press executes it.
 *
 * What changed vs the old single-payload version:
 *   • Multiple built-in payloads again, cycled with short press.
 *   • Robust DuckyScript parser: REM, STRING/STRINGLN, DELAY,
 *     ENTER/TAB/ESC/GUI/CTRL/ALT/SHIFT (+ combos like "GUI r",
 *     "CTRL ALT t"), arrow/function keys.
 *   • Connection handling relaxed — a live BLE HID link is enough;
 *     we only settle briefly instead of demanding 500 ms of perfect
 *     stability (which caused most "nothing happens" reports).
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

// ─── Payload library (DuckyScript subset) ────────────────────────────────
//   REM <text>         comment (ignored)
//   GUI <key>          Win/Cmd + key
//   CTRL / ALT / SHIFT <key>   modifier + key
//   ENTER TAB ESC DELETE BACKSPACE UP DOWN LEFT RIGHT
//   STRING <text>      type literal text (no newline)
//   STRINGLN <text>    type text then press ENTER
//   DELAY <ms>         wait N milliseconds
struct BadUsbPayload {
  const char* name;
  const char* script;
};

static const BadUsbPayload BADUSB_PAYLOADS[] = {
  { "Hola Mundo (Notepad)",
    "GUI r\n"
    "DELAY 600\n"
    "STRINGLN notepad\n"
    "DELAY 900\n"
    "STRING GOATI says: HOLA MUNDO :)\n" },

  { "Open URL (browser)",
    "GUI r\n"
    "DELAY 600\n"
    "STRINGLN https://vercel.com\n" },

  { "Win: About dialog",
    "GUI r\n"
    "DELAY 600\n"
    "STRINGLN winver\n" },

  { "Lock workstation",
    "GUI l\n" },

  { "Mac: Spotlight note",
    "GUI SPACE\n"
    "DELAY 500\n"
    "STRINGLN notes\n"
    "DELAY 900\n"
    "STRING GOATI was here\n" },
};

#define BADUSB_PAYLOAD_COUNT (sizeof(BADUSB_PAYLOADS) / sizeof(BADUSB_PAYLOADS[0]))

// ─── Forward decls (badusb_init / badusb_run_payload are mutually recursive
// because run_payload lazily calls init when invoked from the shell) ──────
static void badusb_init();

// ─── State ───────────────────────────────────────────────────────────────
static BleKeyboard g_ble_kbd("GOATI-KB", "GOATI", 100);
static bool        g_badusb_running   = false;
static uint32_t    g_badusb_start_ms  = 0;
static uint8_t     g_badusb_idx       = 0;   // selected payload
static bool        g_badusb_connected_latched = false;
static uint32_t    g_badusb_conn_since_ms     = 0;
static bool        g_badusb_inited    = false; // set true after badusb_init()

// ─── Accessors (used by display.h / shell.h) ─────────────────────────────
static const char* badusb_payload_name()  { return BADUSB_PAYLOADS[g_badusb_idx].name; }

static void badusb_next() {
  g_badusb_idx = (g_badusb_idx + 1) % BADUSB_PAYLOAD_COUNT;
  Serial.printf("[BadUSB] payload -> %s\r\n", BADUSB_PAYLOADS[g_badusb_idx].name);
}

static void badusb_select(uint8_t idx) {
  if (idx < BADUSB_PAYLOAD_COUNT) g_badusb_idx = idx;
}

// Restart HID advertising (called when returning to the BadUSB page).
static void badusb_resume_advertising() {
  if (!g_ble_kbd.isConnected()) {
    BLEAdvertising* adv = BLEDevice::getAdvertising();
    if (adv) adv->start();
  }
}

// ─── Key name → HID code ──────────────────────────────────────────────────
static uint8_t badusb_named_key(const char* tok, int len) {
  auto eq = [&](const char* k) { int n = strlen(k); return n == len && strncmp(tok, k, n) == 0; };
  if (eq("ENTER"))     return KEY_RETURN;
  if (eq("TAB"))       return KEY_TAB;
  if (eq("ESC"))       return KEY_ESC;
  if (eq("SPACE"))     return ' ';
  if (eq("DELETE"))    return KEY_DELETE;
  if (eq("BACKSPACE")) return KEY_BACKSPACE;
  if (eq("UP"))        return KEY_UP_ARROW;
  if (eq("DOWN"))      return KEY_DOWN_ARROW;
  if (eq("LEFT"))      return KEY_LEFT_ARROW;
  if (eq("RIGHT"))     return KEY_RIGHT_ARROW;
  if (eq("HOME"))      return KEY_HOME;
  if (eq("END"))       return KEY_END;
  return 0;
}

// press a modifier + optional trailing key token (e.g. "GUI r", "CTRL ALT t")
static void badusb_run_combo(uint8_t mod, const char* rest) {
  while (*rest == ' ') rest++;
  g_ble_kbd.press(mod);
  delay(30);
  if (*rest) {
    // Chained modifier? (CTRL ALT t)
    uint8_t m2 = 0;
    if      (strncmp(rest, "ALT ", 4) == 0)   { m2 = KEY_LEFT_ALT;  rest += 4; }
    else if (strncmp(rest, "CTRL ", 5) == 0)  { m2 = KEY_LEFT_CTRL; rest += 5; }
    else if (strncmp(rest, "SHIFT ", 6) == 0) { m2 = KEY_LEFT_SHIFT; rest += 6; }
    if (m2) { g_ble_kbd.press(m2); delay(30); while (*rest == ' ') rest++; }

    int len = 0; while (rest[len] && rest[len] != ' ') len++;
    uint8_t k = badusb_named_key(rest, len);
    if (k) g_ble_kbd.press(k);
    else if (len >= 1) g_ble_kbd.press((uint8_t)rest[0]);
    delay(40);
  }
  g_ble_kbd.releaseAll();
}

// ─── DuckyScript line executor ────────────────────────────────────────────
static void badusb_run_line(const char* line, int len) {
  // Trim leading whitespace
  while (len > 0 && (*line == ' ' || *line == '\t')) { line++; len--; }
  if (len <= 0) return;
  if (!g_ble_kbd.isConnected()) return;

  if (strncmp(line, "REM", 3) == 0) {
    return;                                  // comment
  } else if (strncmp(line, "STRINGLN ", 9) == 0) {
    char buf[160]; int n = min(len - 9, (int)sizeof(buf) - 1);
    memcpy(buf, line + 9, n); buf[n] = '\0';
    g_ble_kbd.print(buf);
    delay(30);
    g_ble_kbd.write(KEY_RETURN);
  } else if (strncmp(line, "STRING ", 7) == 0) {
    char buf[160]; int n = min(len - 7, (int)sizeof(buf) - 1);
    memcpy(buf, line + 7, n); buf[n] = '\0';
    g_ble_kbd.print(buf);
  } else if (strncmp(line, "DELAY ", 6) == 0) {
    int ms = atoi(line + 6);
    if (ms > 0) delay((uint32_t)min(ms, 10000));
  } else if (strncmp(line, "GUI ", 4) == 0) {
    badusb_run_combo(KEY_LEFT_GUI, line + 4);
  } else if (strncmp(line, "CTRL ", 5) == 0) {
    badusb_run_combo(KEY_LEFT_CTRL, line + 5);
  } else if (strncmp(line, "ALT ", 4) == 0) {
    badusb_run_combo(KEY_LEFT_ALT, line + 4);
  } else if (strncmp(line, "SHIFT ", 6) == 0) {
    badusb_run_combo(KEY_LEFT_SHIFT, line + 6);
  } else {
    // Bare key name (ENTER/TAB/...) or literal text fallback.
    uint8_t k = badusb_named_key(line, len);
    if (k) {
      g_ble_kbd.write(k);
    } else {
      char buf[160]; int n = min(len, (int)sizeof(buf) - 1);
      memcpy(buf, line, n); buf[n] = '\0';
      g_ble_kbd.print(buf);
    }
  }
  delay(40); // host HID stacks need > ~20 ms between reports
}

static void badusb_run_payload() {
  // Lazy init: BLE is normally brought up on first navigation to the BadUSB
  // or BLE Spam page (see disp_set_state in display.h).  But the shell
  // command `badusb run` can fire without ever visiting the page, so make
  // sure the Bluedroid stack is up before we touch the BLE keyboard.
  if (!g_badusb_inited) badusb_init();
  if (!g_ble_kbd.isConnected()) {
    Serial.println(F("[BadUSB] not paired — pair with 'GOATI-KB' first"));
    Serial.println(F("[BadUSB] Windows: Settings > Bluetooth > GOATI-KB > Connect"));
    return;
  }
  // Brief settle so the host finishes setting up the HID pipe.
  delay(300);
  if (!g_ble_kbd.isConnected()) {
    Serial.println(F("[BadUSB] link dropped before start — aborted"));
    return;
  }

  const BadUsbPayload& pl = BADUSB_PAYLOADS[g_badusb_idx];
  Serial.printf("[BadUSB] running payload: %s\r\n", pl.name);
  g_badusb_running  = true;
  g_badusb_start_ms = millis();

  const char* p = pl.script;
  while (*p) {
    const char* eol = strchr(p, '\n');
    int len = eol ? (int)(eol - p) : (int)strlen(p);
    if (len > 0 && p[len - 1] == '\r') len--;
    badusb_run_line(p, len);
    if (!g_ble_kbd.isConnected()) {
      Serial.println(F("[BadUSB] LOST connection mid-payload — aborted"));
      break;
    }
    if (!eol) break;
    p = eol + 1;
  }
  g_badusb_running = false;
  Serial.println(F("[BadUSB] done"));
}

// ─── Public API ──────────────────────────────────────────────────────────
static void badusb_init() {
  if (g_badusb_inited) return;
  g_badusb_inited    = true;
  g_ble_kbd.begin();
  g_ble_kbd.setBatteryLevel(100);           // helps hosts recognise the HID
  BLEDevice::setPower(ESP_PWR_LVL_P9);      // full Tx power for a stable link
  Serial.println(F("[BadUSB] BLE keyboard started as 'GOATI-KB'"));
  Serial.println(F("[BadUSB] pair it in your OS Bluetooth settings, then"));
  Serial.println(F("[BadUSB] short-press PRG to pick a payload, long-press to run"));
}

static void badusb_loop() {
  // BLE is normally lazy-inited the first time the user navigates to the
  // BadUSB / BLE Spam page.  Until then, g_ble_kbd is not usable — skip the
  // connection-debounce work and keep state clean so the first real
  // transition latches correctly.
  if (!g_badusb_inited) {
    g_badusb_connected_latched = false;
    g_badusb_conn_since_ms     = 0;
    return;
  }
  // Debounce connection state so the OLED doesn't flicker.
  bool raw = g_ble_kbd.isConnected();
  if (raw) {
    if (g_badusb_conn_since_ms == 0) g_badusb_conn_since_ms = millis();
    if (!g_badusb_connected_latched && (millis() - g_badusb_conn_since_ms) > 300) {
      g_badusb_connected_latched = true;
    }
  } else {
    g_badusb_conn_since_ms     = 0;
    g_badusb_connected_latched = false;
  }
}

static bool badusb_is_connected() {
  return g_badusb_connected_latched;
}
