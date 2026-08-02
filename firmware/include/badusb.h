/*
 * ─────────────────────────────────────────────────────────
 * GOATI : BadUSB over Bluetooth (BLE HID keyboard)
 *
 * Pairs as a Bluetooth keyboard ("GOATI-KB") and types
 * DuckyScript-style payloads on the paired host.
 *
 * Stability fixes (vs. v0/goati-fix-improve):
 *   • `badusb_init()` relies on `BleKeyboard.begin()` for the Bluedroid
 *     bring-up and never calls `BLEDevice::init` a second time.
 *   • Captures the keyboard's `BLEAdvertising*` so `badusb_resume_advertising`
 *     can put "GOATI-KB" back on the air after BLE Spam steals the radio.
 *   • Removes the 300 ms pre-payload settle + the 500 ms "stable" gate.
 *     iOS only confirms the HID link once; gating the first keystroke on
 *     a sustained `isConnected()` window rejects every first connection.
 *   • Inter-keystroke delay kept at 25-50 ms — fast enough for natural
 *     typing but well above the USB/BLE HID 20 ms floor.
 *
 * INTENDED FOR AUTHORIZED SECURITY TESTING AND EDUCATIONAL USE ONLY on
 * devices you own or have explicit permission to test.
 *
 * Depends on: t-vk/ESP32 BLE Keyboard ^0.3.2
 * ─────────────────────────────────────────────────────────
 */

#pragma once

#include <BleKeyboard.h>

// ─── Payload library (DuckyScript subset) ────────────────────────────────
//   REM <text>         comment (ignored)
//   GUI <key>          Win/Cmd + key (chainable: GUI r, CTRL ALT t)
//   CTRL / ALT / SHIFT <key>   modifier + key
//   ENTER TAB ESC SPACE DELETE BACKSPACE UP DOWN LEFT RIGHT HOME END
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

// ─── Forward decls ───────────────────────────────────────────────────────
static void badusb_init();

// ─── State ───────────────────────────────────────────────────────────────
static BleKeyboard g_ble_kbd("GOATI-KB", "GOATI", 100);
static bool        g_badusb_running              = false;
static uint32_t    g_badusb_start_ms             = 0;
static uint8_t     g_badusb_idx                  = 0;
static bool        g_badusb_connected_latched    = false;
static uint32_t    g_badusb_conn_since_ms        = 0;
static bool        g_badusb_inited               = false;
static BLEAdvertising* g_badusb_adv              = nullptr;  // captured in begin()

// ─── Accessors ───────────────────────────────────────────────────────────
static const char* badusb_payload_name() { return BADUSB_PAYLOADS[g_badusb_idx].name; }

static void badusb_next() {
  g_badusb_idx = (g_badusb_idx + 1) % BADUSB_PAYLOAD_COUNT;
  Serial.printf("[BadUSB] payload -> %s\r\n", BADUSB_PAYLOADS[g_badusb_idx].name);
}

static void badusb_select(uint8_t idx) {
  if (idx < BADUSB_PAYLOAD_COUNT) g_badusb_idx = idx;
}

// Restart HID advertising on the existing Bluedroid server. Used after
// BLE Spam releases the controller with a random address.
static void badusb_resume_advertising() {
  if (g_badusb_adv && !g_ble_kbd.isConnected()) {
    g_badusb_adv->start();
  }
}

// ─── Key name → HID code ─────────────────────────────────────────────────
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

// Press a modifier + optional trailing key (chainable: "GUI r", "CTRL ALT t").
static void badusb_run_combo(uint8_t mod, const char* rest) {
  while (*rest == ' ') rest++;
  g_ble_kbd.press(mod);
  delay(25);
  if (*rest) {
    uint8_t m2 = 0;
    if      (strncmp(rest, "ALT ", 4)   == 0) { m2 = KEY_LEFT_ALT;  rest += 4; }
    else if (strncmp(rest, "CTRL ", 5)  == 0) { m2 = KEY_LEFT_CTRL; rest += 5; }
    else if (strncmp(rest, "SHIFT ", 6) == 0) { m2 = KEY_LEFT_SHIFT;rest += 6; }
    if (m2) { g_ble_kbd.press(m2); delay(25); while (*rest == ' ') rest++; }

    int len = 0; while (rest[len] && rest[len] != ' ') len++;
    uint8_t k = badusb_named_key(rest, len);
    if (k) g_ble_kbd.press(k);
    else if (len >= 1) g_ble_kbd.press((uint8_t)rest[0]);
    delay(25);
  }
  g_ble_kbd.releaseAll();
}

// ─── DuckyScript line executor ────────────────────────────────────────────
static void badusb_run_line(const char* line, int len) {
  while (len > 0 && (*line == ' ' || *line == '\t')) { line++; len--; }
  if (len <= 0) return;
  if (!g_ble_kbd.isConnected()) return;

  if (strncmp(line, "REM", 3) == 0) {
    return;
  } else if (strncmp(line, "STRINGLN ", 9) == 0) {
    char buf[160]; int n = min(len - 9, (int)sizeof(buf) - 1);
    memcpy(buf, line + 9, n); buf[n] = '\0';
    g_ble_kbd.print(buf);
    delay(25);
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
    uint8_t k = badusb_named_key(line, len);
    if (k) g_ble_kbd.write(k);
    else {
      char buf[160]; int n = min(len, (int)sizeof(buf) - 1);
      memcpy(buf, line, n); buf[n] = '\0';
      g_ble_kbd.print(buf);
    }
  }
  delay(40);  // host HID stacks need > ~20 ms between reports
}

static void badusb_run_payload() {
  // Lazy init so shell `badusb run` works without first visiting page 5.
  if (!g_badusb_inited) badusb_init();

  // Trust the onConnect callback (latched true). iOS only confirms the
  // HID link once, so a sustained "is the link still up?" gate rejects
  // every first connection. Instead we bail the moment the link drops
  // mid-payload.
  if (!g_badusb_connected_latched) {
    Serial.println(F("[BadUSB] not paired — pair with 'GOATI-KB' first"));
    Serial.println(F("[BadUSB] iOS/macOS: Settings > Bluetooth > GOATI-KB"));
    Serial.println(F("[BadUSB] Android: Settings > Connected devices > Pair new"));
    Serial.println(F("[BadUSB] Windows: Settings > Bluetooth > GOATI-KB > Connect"));
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
  g_badusb_inited = true;
  // BleKeyboard::begin() calls BLEDevice::init() once. Do NOT call init
  // again — Bluedroid's controller becomes "stuck" and connections flap.
  g_ble_kbd.begin();
  g_ble_kbd.setBatteryLevel(100);              // helps hosts recognise the HID
  BLEDevice::setPower(ESP_PWR_LVL_P9);         // full Tx power for a stable link
  // Cache the BLEAdvertising* the keyboard library created so we can
  // re-start it after BLE Spam stops (without re-creating services).
  g_badusb_adv = BLEDevice::getAdvertising();
  Serial.println(F("[BadUSB] BLE keyboard started as 'GOATI-KB'"));
  Serial.println(F("[BadUSB] pair it in your OS Bluetooth settings, then"));
  Serial.println(F("[BadUSB] short-press PRG to pick a payload, long-press to run"));
}

static void badusb_loop() {
  // Debounce: latch on after 200 ms of "connected" (latches quickly but
  // tolerates link-layer jitter). Reset on any disconnect.
  if (!g_badusb_inited) {
    g_badusb_connected_latched = false;
    g_badusb_conn_since_ms     = 0;
    return;
  }
  bool raw = g_ble_kbd.isConnected();
  if (raw) {
    if (g_badusb_conn_since_ms == 0) g_badusb_conn_since_ms = millis();
    if (!g_badusb_connected_latched && (millis() - g_badusb_conn_since_ms) > 200) {
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
