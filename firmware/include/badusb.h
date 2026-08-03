/*
 * ─────────────────────────────────────────────────────────
 * GOATI : BadUSB over USB HID (Heltec WiFi LoRa 32 V3)
 *
 * The Heltec V3's USB-C port goes to the native USB of the ESP32-S3
 * (GPIO 19 D-, GPIO 20 D+).  When the Heltec is plugged into a host
 * via this USB-C, it enumerates as a USB HID keyboard ("GOATI-KB")
 * using the Adafruit_TinyUSB library, and types DuckyScript-style
 * payloads on the host.
 *
 * Why USB and not BLE: the previous v0 BLE HID path had persistent
 * pairing issues on modern Windows (the BLE controller on the host
 * side flapped connection state under load).  USB HID is rock-solid.
 *
 * Side effect: while the Heltec is plugged in as a HID device, the
 * native USB cannot also be a CDC (Serial) on the same port.  The
 * serial monitor over USB is therefore unavailable.  Debug prints go
 * to UART0 (the LoRa header pins) — or rely on the OLED + Serial
 * via a USB-to-serial adapter on the external UART.
 *
 * INTENDED FOR AUTHORIZED SECURITY TESTING AND EDUCATIONAL USE ONLY
 * on devices you own or have explicit permission to test.
 *
 * Depends on: Adafruit_TinyUSB_Library ^3.5.0
 * ─────────────────────────────────────────────────────────
 */

#pragma once

#include <Adafruit_TinyUSB.h>
#include <arduino/Adafruit_USBD_Device.h>
#include <class/hid/hid.h>  // USB HID keycodes (KEY_A, KEY_LEFT_GUI, etc.)

// ─── State ─────────────────────────────────────────────────────────────
static uint8_t  g_badusb_inited             = false;
static bool     g_badusb_running            = false;
static uint32_t g_badusb_start_ms           = 0;
static bool     g_badusb_connected_latched  = false;  // host enumerated
static uint32_t g_badusb_conn_since_ms      = 0;

// ─── USB HID device descriptor ───────────────────────────────────────────
// We declare a single-instance HID keyboard.
static Adafruit_USBD_HID USBHid;

// ─── ASCII -> HID keycode + shift mask ────────────────────────────────
//
// The BleKeyboard library's print() does this conversion internally; for
// USB HID we have to do it ourselves.  Returns the HID keycode for the
// ASCII char and the modifier bitmask (KEYBOARD_MODIFIER_LEFTSHIFT for
// shifted characters).
static uint8_t ascii_to_hid(char c, uint8_t& modifier) {
  modifier = 0;
  if (c >= 'a' && c <= 'z') return (uint8_t)(HID_KEY_A + (c - 'a'));
  if (c >= 'A' && c <= 'Z') { modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return (uint8_t)(HID_KEY_A + (c - 'A')); }
  if (c >= '1' && c <= '9') return (uint8_t)(HID_KEY_1 + (c - '1'));
  if (c == '0') return HID_KEY_0;
  if (c == ' ') return HID_KEY_SPACE;
  if (c == '\n') return HID_KEY_ENTER;
  if (c == '\t') return HID_KEY_TAB;
  if (c == '-') return HID_KEY_MINUS;
  if (c == '=') return HID_KEY_EQUAL;
  if (c == '[') return HID_KEY_BRACKET_LEFT;
  if (c == ']') return HID_KEY_BRACKET_RIGHT;
  if (c == '\\') return HID_KEY_BACKSLASH;
  if (c == ';') return HID_KEY_SEMICOLON;
  if (c == '\'') return HID_KEY_APOSTROPHE;
  if (c == '`') return HID_KEY_GRAVE;
  if (c == ',') return HID_KEY_COMMA;
  if (c == '.') return HID_KEY_PERIOD;
  if (c == '/') return HID_KEY_SLASH;
  // Shifted punctuation
  if (c == '_') { modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return HID_KEY_MINUS; }
  if (c == '+') { modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return HID_KEY_EQUAL; }
  if (c == '{') { modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return HID_KEY_BRACKET_LEFT; }
  if (c == '}') { modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return HID_KEY_BRACKET_RIGHT; }
  if (c == '|') { modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return HID_KEY_BACKSLASH; }
  if (c == ':') { modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return HID_KEY_SEMICOLON; }
  if (c == '"') { modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return HID_KEY_APOSTROPHE; }
  if (c == '~') { modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return HID_KEY_GRAVE; }
  if (c == '<') { modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return HID_KEY_COMMA; }
  if (c == '>') { modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return HID_KEY_PERIOD; }
  if (c == '?') { modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return HID_KEY_SLASH; }
  if (c == '!') { modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return HID_KEY_1; }
  if (c == '@') { modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return HID_KEY_2; }
  if (c == '#') { modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return HID_KEY_3; }
  if (c == '$') { modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return HID_KEY_4; }
  if (c == '%') { modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return HID_KEY_5; }
  if (c == '^') { modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return HID_KEY_6; }
  if (c == '&') { modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return HID_KEY_7; }
  if (c == '*') { modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return HID_KEY_8; }
  if (c == '(') { modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return HID_KEY_9; }
  if (c == ')') { modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return HID_KEY_0; }
  return 0;  // unsupported
}

// ─── Helpers ──────────────────────────────────────────────────────────
static void badusb_type_char(char c) {
  uint8_t mod = 0;
  uint8_t keycode = ascii_to_hid(c, mod);
  if (keycode == 0) return;
  uint8_t keys[6] = {keycode, 0, 0, 0, 0, 0};
  USBHid.keyboardReport(0, mod, keys);
  delay(12);
  uint8_t empty[6] = {0};
  USBHid.keyboardReport(0, 0, empty);
}

static void badusb_press_key(uint8_t mod, uint8_t keycode) {
  uint8_t keys[6] = {keycode, 0, 0, 0, 0, 0};
  USBHid.keyboardReport(0, mod, keys);
}

static void badusb_release_all() {
  uint8_t empty[6] = {0};
  USBHid.keyboardReport(0, 0, empty);
}

// ─── DuckyScript payload (kept identical to the BLE-era API) ─────────
static const char BADUSB_PAYLOAD[] =
  "GUI r\n"
  "DELAY 500\n"
  "STRING notepad\n"
  "ENTER\n"
  "DELAY 700\n"
  "STRING HOLA MUNDO\n";

// ─── Forward decls (badusb_run_payload is mutually recursive with shell) ──
static void badusb_init();

static void badusb_run_line(const char* line, int len) {
  if (len <= 0) return;
  if (!g_badusb_connected_latched) {
    Serial.println(F("[BadUSB] host not enumerated, skipping"));
    return;
  }
  // Trim leading whitespace
  while (len > 0 && (*line == ' ' || *line == '\t')) { line++; len--; }
  if (len <= 0) return;

  if (strncmp(line, "REM", 3) == 0) {
    return;                                  // comment
  } else if (strncmp(line, "STRINGLN ", 9) == 0) {
    char buf[160]; int n = min(len - 9, (int)sizeof(buf) - 1);
    memcpy(buf, line + 9, n); buf[n] = '\0';
    for (int i = 0; i < n; ++i) badusb_type_char(buf[i]);
    badusb_type_char('\n');
  } else if (strncmp(line, "STRING ", 7) == 0) {
    for (const char* p = line + 7; *p; ++p) badusb_type_char(*p);
  } else if (strncmp(line, "GUI ", 4) == 0) {
    uint8_t mod = 0, kc = 0;
    char c = line[4];
    if (c == 'r' || c == 'R') kc = HID_KEY_R;
    else if (c == 'd' || c == 'D') kc = HID_KEY_D;
    else if (c == 'e' || c == 'E') kc = HID_KEY_E;
    else if (c == 'l' || c == 'L') kc = HID_KEY_L;
    else if (c == 'i' || c == 'I') kc = HID_KEY_I;
    else if (c == ' ') kc = HID_KEY_SPACE;
    if (kc) badusb_press_key(KEYBOARD_MODIFIER_LEFTGUI, kc);
  } else if (strncmp(line, "CTRL ", 5) == 0) {
    uint8_t kc = 0;
    char c = line[5];
    if (c == 'c' || c == 'C') kc = HID_KEY_C;
    else if (c == 'v' || c == 'V') kc = HID_KEY_V;
    if (kc) badusb_press_key(KEYBOARD_MODIFIER_LEFTCTRL, kc);
  } else if (strncmp(line, "ALT ", 4) == 0) {
    uint8_t kc = 0;
    char c = line[4];
    if (c == 'F' || c == 'f') kc = HID_KEY_F;
    if (kc) badusb_press_key(KEYBOARD_MODIFIER_LEFTALT, kc);
  } else if (strncmp(line, "SHIFT ", 6) == 0) {
    // modifier pressed, no real key
  } else if (strncmp(line, "ENTER", 5) == 0) {
    badusb_type_char('\n');
  } else if (strncmp(line, "TAB", 3) == 0) {
    badusb_press_key(0, HID_KEY_TAB);
  } else if (strncmp(line, "ESC", 3) == 0) {
    badusb_press_key(0, HID_KEY_ESCAPE);
  } else if (strncmp(line, "BACKSPACE", 9) == 0) {
    badusb_press_key(0, HID_KEY_BACKSPACE);
  } else if (strncmp(line, "DELAY ", 6) == 0) {
    int ms = atoi(line + 6);
    if (ms > 0) delay((uint32_t)ms);
  } else {
    // Default: type the line as text
    for (int i = 0; i < len; ++i) badusb_type_char(line[i]);
  }
  badusb_release_all();
  delay(15);
}

static void badusb_run_payload() {
  if (!g_badusb_inited) badusb_init();
  if (!g_badusb_connected_latched) {
    Serial.println(F("[BadUSB] not enumerated — plug into a host"));
    return;
  }
  Serial.println(F("[BadUSB] running USB payload"));
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

// ─── badusb_send_text: type an arbitrary string on the host (psnkeys API) ─
static void badusb_send_text(const char* text) {
  if (!text) return;
  if (!g_badusb_connected_latched) {
    Serial.println(F("[BadUSB] send_text: host not enumerated"));
    return;
  }
  Serial.printf("[BadUSB] send_text (%u bytes)\n", (unsigned)strlen(text));
  for (const char* p = text; *p; ++p) {
    if (*p == '\n') {
      badusb_press_key(0, HID_KEY_ENTER);
    } else {
      badusb_type_char(*p);
    }
  }
  badusb_release_all();
  Serial.println(F("[BadUSB] send_text done"));
}

// ─── Public API ──────────────────────────────────────────────────────────
static void badusb_init() {
  if (g_badusb_inited) return;
  g_badusb_inited = true;
  // USB device init must happen BEFORE Serial.begin() if we want the
  // HID to enumerate.  We let Adafruit_TinyUSB call TinyUSBDevice.start() for
  // us by calling USBHid.begin().
  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }
  USBHid.begin();
  // Wait up to 200 ms for the host to enumerate the device
  uint32_t start = millis();
  while (!TinyUSBDevice.mounted() && millis() - start < 200) delay(10);
  Serial.println(F("[BadUSB] USB HID keyboard started as 'GOATI-KB'"));
  Serial.println(F("[BadUSB] plug into a host (USB-C), then long-press PRG"));
}

static void badusb_loop() {
  if (!g_badusb_inited) return;
  bool mounted = TinyUSBDevice.mounted();
  if (mounted) {
    if (g_badusb_conn_since_ms == 0) g_badusb_conn_since_ms = millis();
    if (!g_badusb_connected_latched && (millis() - g_badusb_conn_since_ms) > 200) {
      g_badusb_connected_latched = true;
    }
  } else {
    g_badusb_connected_latched = false;
    g_badusb_conn_since_ms = 0;
  }
}

static bool badusb_is_connected() {
  return g_badusb_connected_latched;
}

static const char* badusb_current_name() {
  return "HOLA MUNDO (Notepad)";
}
