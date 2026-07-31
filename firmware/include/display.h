/*
 * ─────────────────────────────────────────────────────────────
 *   display.h : OLED SSD1306 + PRG button (Heltec WiFi LoRa 32 V3)
 *
 *   OLED 128x64 SSD1306 on I2C (SDA=17, SCL=18, Vext=36, RST=21)
 *   PRG button on GPIO0 (BOOT)
 *
 *   GOATI v4.0 — Tamagotchi-style interface:
 *     BOOT   → wake-up sequence with sleepy→happy face
 *     HOME   → GOATI face, blinks, WiFi status, periodic auto-messages
 *     STATS  → system info
 *     SOCIAL → auto-message mode (GOATI initiates via Telegram too)
 *
 *   PRG button:
 *     Short press → cycle HOME → STATS → SOCIAL → HOME
 *     Long press (HOME)    → toggle WiFi
 *     Long press (SOCIAL)  → trigger auto-message immediately
 * ─────────────────────────────────────────────────────────────
 */
#pragma once

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Forward decl
static void wifi_connect(uint8_t retries = 20);

// ─── Heltec V3 pinout ────────────────────────────────────────────────────────
#define HELTEC_V3_OLED_VEXT  36
#define HELTEC_V3_OLED_RST   21
#define HELTEC_V3_OLED_SDA   17
#define HELTEC_V3_OLED_SCL   18
#define HELTEC_V3_BTN_PRG    0

// ─── Display dims ─────────────────────────────────────────────────────────────
#define DISP_W 128
#define DISP_H 64
#define DISP_ADDR 0x3C

// ─── Timings ────────────────────────────────────────────────────────────────
#define DISP_TICK_MS         33
#define DISP_BOOT_MS         3000
#define DISP_THINK_TIMEOUT_MS 90000  // auto-clear stuck Thinking after 90s
#define SOCIAL_INTERVAL_MS   180000 // 3 min between auto-messages
#define SOCIAL_FIRST_MS      120000 // 2 min after boot first message

// ─── Button timings ───────────────────────────────────────────────────────────
#define BTN_HOLD_IGNORE_MS   3000
#define BTN_NAV_HOLD_MS      800

// ─── Screen states ────────────────────────────────────────────────────────────
enum DisplayState : uint8_t {
  DISP_BOOT            = 0,
  DISP_HOME            = 1,
  DISP_STATS           = 2,
  DISP_SOCIAL          = 3,    // auto-message
  DISP_BLE_SPAM        = 4,    // ESP32Marauder-style BLE advertisement spam
  DISP_BADUSB          = 5,    // BadUSB over Bluetooth (BLE HID)
  DISP_WIFI_CONNECTING = 6,
  DISP_WIFI_OFF        = 7,
  DISP_LLM_THINKING    = 8,
  DISP_LLM_RESPONDING  = 9,
  DISP_WIFI_OFF_USER   = 10,
};

// Cyclable pages (HOME → STATS → SOCIAL → BLE_SPAM → BADUSB → HOME)
static const uint8_t DISP_CYCLE_PAGES[] = {
  DISP_HOME, DISP_STATS, DISP_SOCIAL, DISP_BLE_SPAM, DISP_BADUSB
};
static const uint8_t DISP_CYCLE_COUNT = sizeof(DISP_CYCLE_PAGES) / sizeof(DISP_CYCLE_PAGES[0]);
static uint8_t g_disp_cycle_idx = 0;

// ─── Auto-messages (kawaii Tamagotchi phrases) ──────────────────────────────
static const char* GOATI_MESSAGES[] = {
  "hi! ^_^",
  "i missed u",
  "feed me wifi",
  "i'm thinking...",
  "blip blop :)",
  "<3 u",
  "what's up?",
  "hug?",
  "sing to me",
  "GOATI nap time",
  "is it lunch?",
  "party?",
  "be my friend",
  "poke poke",
  "are u bored?",
  "lalala",
  "i drew u a heart",
};
#define GOATI_MSG_COUNT (sizeof(GOATI_MESSAGES) / sizeof(GOATI_MESSAGES[0]))

// ─── Mood system ────────────────────────────────────────────────────────────
// 0=happy  1=neutral  2=sad/lonely  3=excited
#define MOOD_HAPPY    0
#define MOOD_NEUTRAL  1
#define MOOD_LONELY   2
#define MOOD_EXCITED  3

#define IDLE_LONELY_MS  3600000  // 1 hour
#define IDLE_HAPPY_MS   300000   // 5 min

// ─── Globals ─────────────────────────────────────────────────────────────────
static Adafruit_SSD1306 g_disp_oled(DISP_W, DISP_H, &Wire, -1);
static DisplayState g_disp_state         = DISP_BOOT;
static DisplayState g_disp_prev_state    = DISP_BOOT;
static uint32_t     g_disp_anim_ms       = 0;
static uint32_t     g_disp_state_ms      = 0;
static char         g_disp_resp_buf[256] = {0};
static uint16_t     g_disp_resp_len      = 0;
static uint16_t     g_disp_resp_pos      = 0;
static uint32_t     g_disp_btn_ready_ms  = 0;
static bool         g_disp_ok            = false;

// Social / auto-message state
static uint32_t     g_social_next_ms     = 0;
static uint8_t      g_social_msg_idx     = 0;
static char         g_social_msg[64]     = {0};
static uint32_t     g_social_show_ms     = 0;
static bool         g_social_active      = false;
static bool         g_social_sent_tg     = false;  // also sent to Telegram?
static uint8_t      g_mood               = MOOD_HAPPY;
static uint32_t     g_last_interaction_ms = 0;  // last Telegram msg or auto msg
static bool         g_tg_was_thinking     = false;

// ─── Button globals ──────────────────────────────────────────────────────────
static volatile uint32_t g_btn_press_start_ms = 0;
static volatile uint32_t g_btn_press_end_ms   = 0;
static volatile bool     g_btn_pressed        = false;
static volatile bool     g_btn_low            = false;

static void IRAM_ATTR btn_isr() {
  bool low = (digitalRead(HELTEC_V3_BTN_PRG) == LOW);
  uint32_t now = millis();
  if (low && !g_btn_low) {
    g_btn_press_start_ms = now;
  } else if (!low && g_btn_low) {
    g_btn_press_end_ms = now;
    g_btn_pressed = true;
  }
  g_btn_low = low;
}

// ─── Helpers ─────────────────────────────────────────────────────────────────
static void disp_show() { g_disp_oled.display(); }
static void disp_clear() { g_disp_oled.clearDisplay(); }
static void disp_set_cursor(uint8_t x, uint8_t y) { g_disp_oled.setCursor(x, y); }
static void disp_print(const char* s) { g_disp_oled.print(s); }
static void disp_println(const char* s) { g_disp_oled.println(s); }
static void disp_hline(int16_t y) {
  for (uint8_t i = 0; i < DISP_W / 6; i++) {
    g_disp_oled.setCursor(i * 6, y);
    g_disp_oled.print('-');
  }
}
static void disp_footer(const char* hint) {
  disp_hline(DISP_H - 10);
  g_disp_oled.setCursor(0, DISP_H - 8);
  g_disp_oled.print(hint);
}

// Inverted top title bar with a page badge on the right (e.g. "1/5").
static void disp_title_bar(const char* title, const char* badge) {
  g_disp_oled.fillRect(0, 0, DISP_W, 11, WHITE);
  g_disp_oled.setTextColor(BLACK, WHITE);
  g_disp_oled.setCursor(3, 2);
  g_disp_oled.print(title);
  if (badge && badge[0]) {
    uint8_t bw = (uint8_t)strlen(badge) * 6;
    g_disp_oled.setCursor(DISP_W - bw - 2, 2);
    g_disp_oled.print(badge);
  }
  g_disp_oled.setTextColor(WHITE, BLACK);
}

// A tidy labelled value row used by the STATS / tool pages.
static void disp_kv(int16_t y, const char* key, const char* val) {
  g_disp_oled.setCursor(2, y);
  g_disp_oled.print(key);
  uint8_t vw = (uint8_t)strlen(val) * 6;
  g_disp_oled.setCursor(DISP_W - vw - 2, y);
  g_disp_oled.print(val);
}

static void disp_heart(int16_t x, int16_t y);  // fwd decl (defined below)

// ─── GOATI creature (kawaii Tamagotchi) ─────────────────────────────────────
// Draws a full little creature (rounded body + antenna + face) centred at cx.
// Mood: 0=happy, 1=neutral, 2=sleepy, 3=thinking, 4=talking, 5=surprised, 6=love
// `frame` drives blinking, mouth animation and a gentle idle bob.
static void disp_goati_face(uint8_t mood, bool blink, uint8_t frame) {
  const int16_t cx = 64;              // horizontal centre
  const int16_t topY = 14;            // just below the title bar
  // Gentle idle bob (±1px) — a tiny bit of life.
  int16_t bob = ((frame / 8) % 2) ? 1 : 0;
  int16_t bodyY = topY + bob;
  const int16_t bodyW = 44, bodyH = 40;
  const int16_t bx = cx - bodyW / 2;

  // Antenna with a blinking bulb.
  g_disp_oled.drawFastVLine(cx, bodyY - 5, 5, WHITE);
  if ((frame / 4) % 2)
    g_disp_oled.fillCircle(cx, bodyY - 6, 2, WHITE);
  else
    g_disp_oled.drawCircle(cx, bodyY - 6, 2, WHITE);

  // Rounded body/head.
  g_disp_oled.drawRoundRect(bx, bodyY, bodyW, bodyH, 12, WHITE);
  // Little feet.
  g_disp_oled.fillRect(bx + 8, bodyY + bodyH - 1, 6, 3, WHITE);
  g_disp_oled.fillRect(bx + bodyW - 14, bodyY + bodyH - 1, 6, 3, WHITE);

  // Eyes.
  const int16_t eyeY = bodyY + 15;
  const int16_t leftCx = cx - 9, rightCx = cx + 9;
  bool sleepy = (mood == 2);
  if (blink || sleepy) {
    g_disp_oled.drawLine(leftCx - 3, eyeY, leftCx + 3, eyeY, WHITE);
    g_disp_oled.drawLine(rightCx - 3, eyeY, rightCx + 3, eyeY, WHITE);
  } else {
    int8_t px = 0, py = 0;
    if (mood == 3) py = -1;           // thinking: look up
    if (mood == 5) py =  0;           // surprised: wide
    if (mood == 0) py =  1;           // happy: look down slightly
    uint8_t r = (mood == 5) ? 4 : 3;  // surprised → bigger eyes
    g_disp_oled.fillCircle(leftCx, eyeY, r, WHITE);
    g_disp_oled.fillCircle(rightCx, eyeY, r, WHITE);
    // Pupils (punched out) + sparkle.
    g_disp_oled.fillCircle(leftCx + px, eyeY + py, 1, BLACK);
    g_disp_oled.fillCircle(rightCx + px, eyeY + py, 1, BLACK);
    g_disp_oled.drawPixel(leftCx - 1, eyeY - 1, BLACK);
    g_disp_oled.drawPixel(rightCx - 1, eyeY - 1, BLACK);
  }

  // Blush cheeks (happy / love).
  if (mood == 0 || mood == 6) {
    g_disp_oled.drawPixel(leftCx - 6, eyeY + 5, WHITE);
    g_disp_oled.drawPixel(leftCx - 5, eyeY + 6, WHITE);
    g_disp_oled.drawPixel(rightCx + 6, eyeY + 5, WHITE);
    g_disp_oled.drawPixel(rightCx + 5, eyeY + 6, WHITE);
  }

  // Mouth.
  const int16_t my = bodyY + 27;
  switch (mood) {
    case 0:  // happy smile
      g_disp_oled.drawLine(cx - 6, my, cx, my + 4, WHITE);
      g_disp_oled.drawLine(cx, my + 4, cx + 6, my, WHITE);
      break;
    case 1:  // neutral
      g_disp_oled.drawFastHLine(cx - 5, my + 1, 10, WHITE);
      break;
    case 2:  // sleepy — small "zzz"
      g_disp_oled.drawFastHLine(cx - 3, my + 1, 6, WHITE);
      g_disp_oled.setCursor(cx + 14, bodyY + 2);
      g_disp_oled.print("z");
      break;
    case 3:  // thinking (small o)
      g_disp_oled.drawCircle(cx, my, 2, WHITE);
      break;
    case 4:  // talking (open/close)
      if (frame % 2) g_disp_oled.fillRect(cx - 5, my - 1, 10, 4, WHITE);
      else           g_disp_oled.drawFastHLine(cx - 5, my, 10, WHITE);
      break;
    case 5:  // surprised
      g_disp_oled.fillCircle(cx, my + 1, 3, WHITE);
      g_disp_oled.fillCircle(cx, my + 1, 1, BLACK);
      break;
    case 6:  // love (heart)
      disp_heart(cx - 3, my - 1);
      break;
  }
}

static void disp_heart(int16_t x, int16_t y) {
  g_disp_oled.fillRect(x, y, 2, 2, WHITE);
  g_disp_oled.fillRect(x + 2, y, 2, 2, WHITE);
  g_disp_oled.fillRect(x + 4, y, 2, 2, WHITE);
  g_disp_oled.fillRect(x + 2, y + 2, 2, 2, WHITE);
  g_disp_oled.fillRect(x + 1, y + 3, 4, 1, WHITE);
  g_disp_oled.drawPixel(x + 2, y + 4, WHITE);
}

static void disp_wifi_bars(int16_t x, int16_t y) {
  g_disp_oled.fillRect(x, y + 4, 2, 1, WHITE);
  g_disp_oled.fillRect(x + 3, y + 2, 2, 3, WHITE);
  g_disp_oled.fillRect(x + 6, y, 2, 5, WHITE);
}

// ─── Strip <think>...</think> blocks (M3 reasoning tags) ──────────────────
static uint16_t strip_think_tags(const char* src, char* dst, uint16_t dst_cap) {
  uint16_t n = 0;
  bool last_space = true;
  const char* p = src;
  while (*p && n < dst_cap - 1) {
    if (strncmp(p, "<think>", 7) == 0) {
      const char* end = strstr(p + 7, "</think>");
      if (end) {
        p = end + 8;
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
        continue;
      }
    }
    if (*p == '\n' || *p == '\r' || *p == '\t') {
      if (!last_space) { dst[n++] = ' '; last_space = true; }
    } else if (*p == ' ' || *p == ' ') {
      if (!last_space) { dst[n++] = ' '; last_space = true; }
    } else {
      dst[n++] = *p;
      last_space = false;
    }
    p++;
  }
  while (n > 0 && dst[n-1] == ' ') n--;
  dst[n] = '\0';
  return n;
}

// ─── Animation renderers ──────────────────────────────────────────────────────
static void disp_draw_boot(uint32_t now) {
  uint32_t elapsed = now - g_disp_state_ms;
  disp_clear();
  g_disp_oled.setTextSize(1);
  g_disp_oled.setTextColor(SSD1306_WHITE);

  // Phase 1 (0-1200ms):  GOATI wakes up (sleepy -> neutral)
  // Phase 2 (1200-2600): title "GOATI" types in + creature happy
  // Phase 3 (2600ms+):   "READY" + version

  uint8_t frame = now / 120;

  if (elapsed < 1200) {
    // Sleepy creature waking up (face-mood 2 = sleepy).
    disp_goati_face(2, false, frame);
    disp_set_cursor(40, 2);
    g_disp_oled.print(F("waking up"));
    // animated dots
    uint8_t dots = (elapsed / 300) % 4;
    for (uint8_t i = 0; i < dots; i++) g_disp_oled.print('.');
  } else if (elapsed < 2600) {
    // Creature is now happy; type the name in the title bar.
    disp_goati_face(MOOD_HAPPY, ((frame / 6) % 8) == 0, frame);
    g_disp_oled.fillRect(0, 0, DISP_W, 11, WHITE);
    g_disp_oled.setTextColor(BLACK, WHITE);
    g_disp_oled.setTextSize(1);
    char h[] = "G O A T I";
    uint8_t nchars = ((elapsed - 1200) * 9) / 1200;
    if (nchars > 9) nchars = 9;
    g_disp_oled.setCursor(38, 2);
    for (uint8_t i = 0; i < nchars; i++) g_disp_oled.print(h[i]);
    g_disp_oled.setTextColor(WHITE, BLACK);
  } else {
    disp_goati_face(MOOD_HAPPY, false, frame);
    g_disp_oled.fillRect(0, 0, DISP_W, 11, WHITE);
    g_disp_oled.setTextColor(BLACK, WHITE);
    g_disp_oled.setCursor(46, 2);
    g_disp_oled.print(F("GOATI"));
    g_disp_oled.setTextColor(WHITE, BLACK);
    g_disp_oled.setCursor(28, 56);
    g_disp_oled.print(F("M3  ready  v5"));
  }

  // Slim progress bar pinned to the very bottom edge.
  uint8_t fill = (uint8_t)((elapsed * (DISP_W - 4)) / 3600);
  if (fill > DISP_W - 4) fill = DISP_W - 4;
  g_disp_oled.fillRect(2, DISP_H - 2, fill, 2, WHITE);

  disp_show();
}

static void disp_draw_home(uint32_t now) {
  disp_clear();
  g_disp_oled.setTextSize(1);
  g_disp_oled.setTextColor(SSD1306_WHITE);

  // Title bar: SSID/name + WiFi state badge.
  const char* name = g_cfg.wifi_ssid[0] ? g_cfg.wifi_ssid : "GOATI";
  bool online = (WiFi.status() == WL_CONNECTED);
  disp_title_bar(name, online ? "ON" : "OFF");

  // Creature reflecting the current mood.  Map GOATI moods onto face moods:
  //   HAPPY->happy, NEUTRAL->neutral, LONELY->love(pouty), EXCITED->surprised.
  bool blink = ((now / 100) % 33) == 0;
  uint8_t face_mood;
  switch (g_mood) {
    case MOOD_NEUTRAL: face_mood = 1; break;  // neutral
    case MOOD_LONELY:  face_mood = 6; break;  // love (wants attention)
    case MOOD_EXCITED: face_mood = 5; break;  // surprised
    default:           face_mood = 0; break;  // happy
  }
  if (!online) face_mood = 2;                 // sleepy when offline
  disp_goati_face(face_mood, blink, now / 120);

  // WiFi signal bars in the free upper-right corner.
  if (online) disp_wifi_bars(DISP_W - 9, 14);

  // Status in the free bottom corners (avoids the centred creature).
  const char* mood_lbl = "happy";
  if (g_mood == MOOD_NEUTRAL) mood_lbl = "idle";
  if (g_mood == MOOD_LONELY)  mood_lbl = "lonely";
  if (g_mood == MOOD_EXCITED) mood_lbl = "yay!";
  if (!online)                mood_lbl = "zzz";
  disp_set_cursor(2, 56);
  disp_print(mood_lbl);
  if (g_last_interaction_ms) {
    uint32_t ago = (millis() - g_last_interaction_ms) / 60000;
    char ml[12];
    snprintf(ml, sizeof(ml), "%lum", (unsigned long)ago);
    uint8_t w = (uint8_t)strlen(ml) * 6;
    disp_set_cursor(DISP_W - w - 2, 56);
    disp_print(ml);
  }
  disp_show();
}

static void disp_draw_stats(uint32_t now) {
  disp_clear();
  g_disp_oled.setTextSize(1);
  g_disp_oled.setTextColor(SSD1306_WHITE);
  disp_title_bar("STATS", "2/5");

  char v[16];
  uint32_t up = millis() / 1000;
  if (up < 3600) snprintf(v, sizeof(v), "%lum%02lus", (unsigned long)(up / 60), (unsigned long)(up % 60));
  else           snprintf(v, sizeof(v), "%luh%02lum", (unsigned long)(up / 3600), (unsigned long)((up % 3600) / 60));
  disp_kv(16, "uptime", v);

  snprintf(v, sizeof(v), "%luK", (unsigned long)(ESP.getFreeHeap() / 1024));
  disp_kv(26, "free heap", v);

  disp_kv(36, "wifi", (WiFi.status() == WL_CONNECTED) ? "linked" : "down");

  if (WiFi.status() == WL_CONNECTED) {
    snprintf(v, sizeof(v), "%ddBm", (int)WiFi.RSSI());
    disp_kv(46, "signal", v);
  } else {
    disp_kv(46, "signal", "--");
  }

  disp_footer("GOATI M3 * v5");
  disp_show();
}

static void disp_draw_social(uint32_t now) {
  // GOATI auto-message on OLED
  disp_clear();
  g_disp_oled.setTextSize(1);
  g_disp_oled.setTextColor(SSD1306_WHITE);

  disp_title_bar("SOCIAL", "3/5");

  // Happy creature (nudged up to leave room for the message line).
  disp_goati_face(6, false, now / 120);

  // Message centred in a footer strip.
  if (g_social_msg[0]) {
    uint8_t w = (uint8_t)strlen(g_social_msg) * 6;
    int16_t x = (w < DISP_W) ? (DISP_W - w) / 2 : 0;
    disp_hline(DISP_H - 10);
    disp_set_cursor(x, DISP_H - 8);
    disp_print(g_social_msg);
  } else {
    disp_footer("hold PRG: new msg");
  }
  disp_show();
}

static void disp_draw_wifi_connecting(uint32_t now) {
  static const char sp[] = "|/-";
  uint8_t idx = (now / 200) % 4;
  char spin[2] = { sp[idx], 0 };
  disp_clear();
  g_disp_oled.setTextSize(2);
  g_disp_oled.setTextColor(SSD1306_WHITE);
  disp_set_cursor(58, 18); disp_print(spin);
  g_disp_oled.setTextSize(1);
  disp_set_cursor(30, 40); disp_print("Connecting");
  if (g_cfg.wifi_ssid[0]) {
    disp_set_cursor(0, 54);
    disp_print(g_cfg.wifi_ssid);
  }
  disp_show();
}

static void disp_draw_wifi_off(uint32_t now) {
  disp_clear();
  g_disp_oled.setTextSize(1);
  g_disp_oled.setTextColor(SSD1306_WHITE);
  bool on = ((now / 400) % 2) == 0;
  if (on) {
    g_disp_oled.setTextSize(2);
    disp_set_cursor(58, 20); g_disp_oled.print('X');
    g_disp_oled.setTextSize(1);
  }
  disp_set_cursor(28, 44); disp_print("NO WIFI");
  disp_footer("PRG (long): ON");
  disp_show();
}

static void disp_draw_thinking(uint32_t now) {
  uint32_t elapsed = now - g_disp_state_ms;
  disp_clear();
  g_disp_oled.setTextSize(1);
  g_disp_oled.setTextColor(SSD1306_WHITE);

  // Hacker-style header
  disp_set_cursor(0, 0);
  disp_print(">> THINK");
  // Glitch text effect
  if ((elapsed / 100) % 4 == 0) {
    disp_set_cursor(DISP_W - 24, 0);
    disp_print("[!!]");
  }

  // GOATI face thinking (eyes up, small mouth)
  disp_goati_face(3, false, now / 200);

  // Animated loading bar
  disp_set_cursor(0, 50);
  static const char spinner[] = "|/-\\";
  g_disp_oled.print(spinner[(now / 100) % 4]);
  g_disp_oled.print(F(" M3 thinking..."));

  // Loading progress
  disp_set_cursor(0, 56);
  for (uint8_t i = 0; i < 21; i++) {
    bool filled = (i * 80) < elapsed;
    g_disp_oled.print(filled ? '#' : '.');
  }
  disp_show();
}

static void disp_draw_responding(uint32_t now) {
  // Just show "-> Telegram" briefly — full message is in Telegram
  disp_clear();
  g_disp_oled.setTextSize(1);
  g_disp_oled.setTextColor(SSD1306_WHITE);

  disp_set_cursor(0, 0);
  disp_print("GOATI says");
  disp_hline(10);

  // GOATI face: happy (sending)
  disp_goati_face(0, false, now / 100);

  // Speech bubble with "ok!" or sending indicator
  disp_set_cursor(38, 30);
  disp_print("-> Telegram");
  disp_set_cursor(0, 50);
  disp_print("message sent :)");
  disp_set_cursor(0, 56);
  disp_print("...");
  disp_show();
}

static void disp_draw_wifi_off_user() {
  disp_clear();
  g_disp_oled.setTextSize(1);
  g_disp_oled.setTextColor(SSD1306_WHITE);
  disp_set_cursor(0, 18); disp_print("WiFi: OFF");
  disp_set_cursor(0, 32); disp_print("hold PRG to");
  disp_set_cursor(0, 42); disp_print("reconnect");
  disp_show();
}

// ─── Master renderer ──────────────────────────────────────────────────────────
static void disp_draw_ble_spam(uint32_t now) {
  disp_clear();
  g_disp_oled.setTextSize(1);
  g_disp_oled.setTextColor(SSD1306_WHITE);

  bool run = g_ble_spam_running;
  disp_title_bar("BLE SPAM", "4/5");

  // Big blinking status banner.
  if (run) {
    bool on = ((now / 300) % 2) == 0;
    if (on) g_disp_oled.fillRect(0, 13, DISP_W, 13, WHITE);
    g_disp_oled.setTextColor(on ? BLACK : WHITE, on ? WHITE : BLACK);
    g_disp_oled.setCursor(30, 16);
    g_disp_oled.print(F("ATTACKING"));
    g_disp_oled.setTextColor(WHITE, BLACK);
  } else {
    g_disp_oled.setCursor(42, 16);
    g_disp_oled.print(F("READY"));
  }

  char v[16];
  disp_kv(30, "flavour", run ? BLE_SPAM_NAMES[g_ble_spam_mode] : "auto");
  snprintf(v, sizeof(v), "%lu", (unsigned long)g_ble_spam_pkt_count);
  disp_kv(40, "packets", v);
  if (run) {
    uint32_t secs = (millis() - g_ble_spam_start_ms) / 1000;
    snprintf(v, sizeof(v), "%lus", (unsigned long)secs);
  } else {
    strcpy(v, "--");
  }
  disp_kv(50, "elapsed", v);

  disp_footer(run ? "hold PRG: stop" : "hold PRG: attack");
  disp_show();
}

static void disp_draw_badusb(uint32_t now) {
  disp_clear();
  g_disp_oled.setTextSize(1);
  g_disp_oled.setTextColor(SSD1306_WHITE);

  disp_title_bar("BADUSB HID", "5/5");

  bool paired = badusb_is_connected();
  bool run    = g_badusb_running;

  // Selected payload, highlighted.
  g_disp_oled.fillRect(0, 13, DISP_W, 12, WHITE);
  g_disp_oled.setTextColor(BLACK, WHITE);
  g_disp_oled.setCursor(3, 15);
  g_disp_oled.print(badusb_payload_name());
  g_disp_oled.setTextColor(WHITE, BLACK);
  // payload index dots on the right.
  for (uint8_t i = 0; i < BADUSB_PAYLOAD_COUNT && i < 6; i++) {
    int16_t dx = DISP_W - 4 - (BADUSB_PAYLOAD_COUNT - i) * 5;
    if (i == g_badusb_idx) g_disp_oled.fillCircle(dx, 19, 2, BLACK);
    else                   g_disp_oled.drawCircle(dx, 19, 2, BLACK);
  }

  disp_kv(30, "bluetooth", paired ? "paired" : "waiting");
  disp_kv(40, "status", run ? "RUNNING" : "idle");

  disp_footer(run ? "running..."
                  : (paired ? "hold PRG: run" : "pair a host first"));
  disp_show();
}

static void disp_draw_state(uint32_t now) {
  switch (g_disp_state) {
    case DISP_BOOT:            disp_draw_boot(now);            break;
    case DISP_HOME:            disp_draw_home(now);            break;
    case DISP_STATS:           disp_draw_stats(now);           break;
    case DISP_SOCIAL:          disp_draw_social(now);          break;
    case DISP_BLE_SPAM:        disp_draw_ble_spam(now);        break;
    case DISP_BADUSB:          disp_draw_badusb(now);          break;
    case DISP_WIFI_CONNECTING: disp_draw_wifi_connecting(now); break;
    case DISP_WIFI_OFF:        disp_draw_wifi_off(now);        break;
    case DISP_LLM_THINKING:    disp_draw_thinking(now);       break;
    case DISP_LLM_RESPONDING:  disp_draw_responding(now);     break;
    case DISP_WIFI_OFF_USER:   disp_draw_wifi_off_user();     break;
  }
}

static void disp_force_redraw() { g_disp_anim_ms = 0; }

// ─── Public API ──────────────────────────────────────────────────────��───────
static void disp_set_state(DisplayState s) {
  if (g_disp_state == s) return;
  g_disp_prev_state = g_disp_state;
  g_disp_state      = s;
  g_disp_state_ms   = millis();
  g_disp_resp_pos   = 0;
  disp_force_redraw();
  // Defensive: entering page 5 — make sure the BLE HID keyboard is advertising
  // again.  We only reset the random address when the BLE spammer is NOT
  // running, otherwise we would clobber its per-packet random MAC.
  if (s == DISP_BADUSB) {
    if (!g_ble_spam_running) {
      uint8_t zero_addr[6] = {0};
      esp_ble_gap_set_rand_addr(zero_addr);
    }
    badusb_resume_advertising();
  }
}

// ─── Auto-message (GOATI social) ────────────────────────────────────────────
static void disp_pick_message() {
  strlcpy(g_social_msg, GOATI_MESSAGES[g_social_msg_idx % GOATI_MSG_COUNT], sizeof(g_social_msg));
  g_social_msg_idx++;
  g_social_active = true;
  g_social_show_ms = millis();
  disp_set_state(DISP_SOCIAL);
}

static void disp_init() {
  pinMode(HELTEC_V3_OLED_VEXT, OUTPUT);
  digitalWrite(HELTEC_V3_OLED_VEXT, HIGH); delay(300);
  digitalWrite(HELTEC_V3_OLED_VEXT, LOW);  delay(500);

  pinMode(HELTEC_V3_OLED_RST, OUTPUT);
  digitalWrite(HELTEC_V3_OLED_RST, LOW);  delay(100);
  digitalWrite(HELTEC_V3_OLED_RST, HIGH); delay(100);

  Wire.begin(HELTEC_V3_OLED_SDA, HELTEC_V3_OLED_SCL);
  g_disp_oled.begin(SSD1306_SWITCHCAPVCC, DISP_ADDR);
  // Flip 180° via raw SSD1306 commands
  Wire.beginTransmission(DISP_ADDR);
  Wire.write(0x00);
  Wire.write(0xA1);  // segment remap
  Wire.write(0xC8);  // COM scan direction
  Wire.endTransmission();
  g_disp_ok = true;
  Serial.println(F("[Disp] GOATI v3.0 ready (RST + flip applied)"));

  pinMode(HELTEC_V3_BTN_PRG, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(HELTEC_V3_BTN_PRG), btn_isr, CHANGE);

  g_disp_state    = DISP_BOOT;
  g_disp_state_ms = millis();
  disp_force_redraw();
  g_disp_btn_ready_ms = millis() + BTN_HOLD_IGNORE_MS;
  g_social_next_ms = millis() + SOCIAL_FIRST_MS;
}

static void disp_show_response(const char* text) {
  if (!text) return;
  uint16_t n = strlen(text);
  if (n >= sizeof(g_disp_resp_buf)) n = sizeof(g_disp_resp_buf) - 1;
  memcpy(g_disp_resp_buf, text, n);
  g_disp_resp_buf[n] = '\0';
  g_disp_resp_len = n;
  g_disp_resp_pos = 0;
}

// ─── Loop ───────────────────────────────────────────────────────────────────
static void disp_loop() {
  if (!g_disp_ok) return;
  uint32_t now = millis();

  // Sync state with actual WiFi
  if (WiFi.status() == WL_CONNECTED) {
    if (g_disp_state == DISP_WIFI_OFF || g_disp_state == DISP_WIFI_OFF_USER) {
      disp_set_state(DISP_HOME);
    }
  } else {
    if (g_disp_state == DISP_WIFI_CONNECTING) {
      if ((now - g_disp_state_ms) > 12000) {
        disp_set_state(DISP_WIFI_OFF);
      }
    }
  }

  // Auto-clear stuck Thinking state (90s)
  if (g_disp_state == DISP_LLM_THINKING &&
      (now - g_disp_state_ms) > DISP_THINK_TIMEOUT_MS) {
    Serial.println(F("[Disp] Thinking timeout, returning to HOME"));
    disp_set_state(DISP_HOME);
  }

  // Force minimum 1.5s of THINKING before transitioning to RESPONDING
  if (g_disp_state == DISP_LLM_THINKING &&
      (now - g_disp_state_ms) >= 1500) {
    // After 1.5s, allow transition (telegram handler or chat will set RESPONDING)
  }
  // Auto-clear responding after 5s (OLED just shows "-> Telegram" briefly)
  if (g_disp_state == DISP_LLM_RESPONDING &&
      (now - g_disp_state_ms) > 5000) {
    disp_set_state(DISP_HOME);
  }

  // Auto-clear SOCIAL after 6s
  if (g_disp_state == DISP_SOCIAL && g_social_active &&
      (now - g_social_show_ms) > 6000) {
    g_social_active = false;
    disp_set_state(DISP_HOME);
  }

  // Auto BOOT to HOME after 4s (longer so user sees the animation)
  if (g_disp_state == DISP_BOOT && (now - g_disp_state_ms) > 4000) {
    disp_set_state(DISP_HOME);
    g_last_interaction_ms = now;
  }

  // ─── Mood system ────────────────────────────────────────────────────────────
  // Update mood based on idle time + WiFi
  if (WiFi.status() == WL_CONNECTED && g_last_interaction_ms > 0) {
    uint32_t idle_ms = now - g_last_interaction_ms;
    if (idle_ms < IDLE_HAPPY_MS) {
      g_mood = MOOD_HAPPY;
    } else if (idle_ms < IDLE_LONELY_MS) {
      g_mood = MOOD_NEUTRAL;
    } else {
      g_mood = MOOD_LONELY;
    }
  } else if (WiFi.status() != WL_CONNECTED) {
    g_mood = MOOD_NEUTRAL;
  }

  // ─── Autonomous message after 1h idle (no force) ─────────────────────────
  // If lonely for a while and we haven't shown a message recently, trigger one.
  if (g_disp_state == DISP_HOME &&
      WiFi.status() == WL_CONNECTED &&
      g_mood == MOOD_LONELY &&
      (int32_t)(now - g_social_next_ms) > 0 &&
      g_social_active == false) {
    // Pick kawaii message
    strlcpy(g_social_msg, GOATI_MESSAGES[g_social_msg_idx % GOATI_MSG_COUNT], sizeof(g_social_msg));
    g_social_msg_idx++;
    g_social_active = true;
    g_social_show_ms = now;
    g_last_interaction_ms = now;  // pretend it was an interaction
    g_mood = MOOD_NEUTRAL;  // back to neutral after sending
    disp_set_state(DISP_SOCIAL);
    Serial.printf("[GOATI auto] %s\n", g_social_msg);
  }

  // Periodic social auto-message (only if WiFi connected and on HOME for a while)
  if (WiFi.status() == WL_CONNECTED &&
      g_disp_state == DISP_HOME &&
      (int32_t)(now - g_social_next_ms) > 0 &&
      g_social_active == false &&
      g_mood != MOOD_LONELY) {  // don't compete with lonely auto
    g_social_next_ms = now + SOCIAL_INTERVAL_MS;
    disp_pick_message();
  }

  // Throttle to ~30 fps
  if ((now - g_disp_anim_ms) < DISP_TICK_MS) return;
  g_disp_anim_ms = now;
  disp_draw_state(now);

  // ─── LED behavior per state (Heltec V3 has LED on GPIO 35) ────────────────
  // LED is a simple GPIO so we do basic patterns (no PWM needed).
  // Use millis-based patterns for each state.
  static uint32_t s_led_last = 0;
  static bool     s_led_state = false;
  if (now - s_led_last > 100) {  // 10 Hz LED update
    s_led_last = now;
    pinMode(35, OUTPUT);  // LED_PIN on Heltec V3
    uint16_t phase = (now / 100) % 100;  // 0-99 cycle
    switch (g_disp_state) {
      case DISP_BOOT:
        digitalWrite(35, phase < 50);
        break;
      case DISP_HOME:
        // Mood-based LED: happy=solid, neutral=slow breathe, lonely=red rapid (rapid blink since we have 1 LED)
        if (g_mood == MOOD_HAPPY) {
          digitalWrite(35, HIGH);
        } else if (g_mood == MOOD_LONELY) {
          // Rapid double-blink = "lonely/red" (simulated)
          digitalWrite(35, (phase % 14) < 4);
        } else {
          // Neutral: slow breathe
          digitalWrite(35, (phase < 30) ? HIGH : LOW);
        }
        break;
      case DISP_LLM_THINKING:
        digitalWrite(35, (phase % 20) < 12);
        break;
      case DISP_LLM_RESPONDING:
        digitalWrite(35, (phase < 8) || (phase > 14 && phase < 22));
        break;
      case DISP_SOCIAL:
        digitalWrite(35, (phase % 30) < 6);
        break;
      case DISP_WIFI_OFF:
      case DISP_WIFI_OFF_USER:
        digitalWrite(35, LOW);
        break;
      default:
        digitalWrite(35, HIGH);
        break;
    }
  }

  // Safety: every 30s force redraw
  static uint32_t s_last_full = 0;
  if (now - s_last_full > 30000) {
    s_last_full = now;
    g_disp_oled.clearDisplay();
    disp_draw_state(now);
  }
}

// ─── Button handling ─��────────────────────────────────────────────────────────
static void btn_loop() {
  if (!g_disp_ok) return;
  uint32_t now = millis();
  if (now < g_disp_btn_ready_ms) return;

  static bool s_action_fired = false;

  if (g_btn_pressed) {
    g_btn_pressed = false;
    Serial.println(F("[Btn] short press"));
    // On the BadUSB page, short press cycles the selected payload.  Once the
    // last payload has been shown, the next short press advances to the next
    // page (so a single button can still both pick a payload and navigate).
    if (g_disp_state == DISP_BADUSB && (g_badusb_idx + 1) < BADUSB_PAYLOAD_COUNT) {
      badusb_next();
      disp_force_redraw();
    } else {
      if (g_disp_state == DISP_BADUSB) g_badusb_idx = 0;
      g_disp_cycle_idx = (g_disp_cycle_idx + 1) % DISP_CYCLE_COUNT;
      disp_set_state((DisplayState)DISP_CYCLE_PAGES[g_disp_cycle_idx]);
    }
  }

  if (g_btn_low) {
    uint32_t held = now - g_btn_press_start_ms;

    if (!s_action_fired) {
      // HOME: long press = toggle WiFi
      if (held >= BTN_NAV_HOLD_MS && g_disp_state == DISP_HOME) {
        s_action_fired = true;
        if (WiFi.status() == WL_CONNECTED) {
          WiFi.disconnect(true);
          disp_set_state(DISP_WIFI_OFF_USER);
          Serial.println(F("[Btn] WiFi OFF"));
        } else {
          disp_set_state(DISP_WIFI_CONNECTING);
          Serial.println(F("[Btn] WiFi ON..."));
          wifi_connect(40);
        }
      }
      // STATS: long press = reboot (kept for backwards compat)
      else if (held >= BTN_NAV_HOLD_MS && g_disp_state == DISP_STATS) {
        s_action_fired = true;
        Serial.println(F("[Btn] (reboot disabled, would reboot here)"));
      }
      // SOCIAL: long press = send new auto-message immediately
      else if (held >= BTN_NAV_HOLD_MS && g_disp_state == DISP_SOCIAL) {
        s_action_fired = true;
        Serial.println(F("[Btn] new social msg"));
        g_social_next_ms = now;
      }
      // BLE_SPAM: long press = start combined attack (released by `else` branch below)
      else if (held >= BTN_NAV_HOLD_MS && g_disp_state == DISP_BLE_SPAM) {
        if (!g_ble_spam_running) {
          s_action_fired = true;
          ble_spam_start_combined();
        }
      }
      // BADUSB: long press = run the single payload (HOLA MUNDO in Notepad)
      else if (held >= BTN_NAV_HOLD_MS && g_disp_state == DISP_BADUSB) {
        s_action_fired = true;
        badusb_run_payload();
      }
    }
  } else {
    // Button released: stop any BLE Spam in progress (combined attack is
    // hold-to-attack, so releasing must stop it).
    if (g_ble_spam_running) {
      ble_spam_stop();
    }
    s_action_fired = false;
  }
}
